#include "QSurfaceTexture.h"

#include <QAndroidJniEnvironment>
#include <QSGGeometryNode>
#include <QSGSimpleMaterialShader>
#include <QDateTime>

struct State {
    // the texture transform matrix
    QMatrix4x4 uSTMatrix;
    GLuint textureId = 0;

    int compare(const State *other) const
    {
        return (uSTMatrix == other->uSTMatrix && textureId == other->textureId) ? 0 : -1;
    }
};

class SurfaceTextureShader : QSGSimpleMaterialShader<State>
{
    QSG_DECLARE_SIMPLE_COMPARABLE_SHADER(SurfaceTextureShader, State)
    public:
        // vertex & fragment shaders are shamelessly "stolen" from MyGLSurfaceView.java :)
        const char *vertexShader() const override {
        return
                "uniform mat4 qt_Matrix;                            \n"
                "uniform mat4 uSTMatrix;                            \n"
                "attribute vec4 aPosition;                          \n"
                "attribute vec4 aTextureCoord;                      \n"
                "varying vec2 vTextureCoord;                        \n"
                "void main() {                                      \n"
                "  gl_Position = qt_Matrix * aPosition;             \n"
                "  vTextureCoord = (uSTMatrix * aTextureCoord).xy;  \n"
                "}";
    }

    const char *fragmentShader() const override {
        return
                "#extension GL_OES_EGL_image_external : require                     \n"
                "precision mediump float;                                           \n"
                "varying vec2 vTextureCoord;                                        \n"
                "uniform lowp float qt_Opacity;                                     \n"
                "uniform samplerExternalOES sTexture;                               \n"
                "void main() {                                                      \n"
                "  gl_FragColor = texture2D(sTexture, vTextureCoord) * qt_Opacity;  \n"
                "}";
    }

    QList<QByteArray> attributes() const override
    {
        return QList<QByteArray>() << "aPosition" << "aTextureCoord";
    }

    void updateState(const State *state, const State *) override
    {
        program()->setUniformValue(m_uSTMatrixLoc, state->uSTMatrix);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, state->textureId);
    }

    void resolveUniforms() override
    {
        m_uSTMatrixLoc = program()->uniformLocation("uSTMatrix");
        program()->setUniformValue("sTexture", 0); // we need to set the texture once
    }

private:
    int m_uSTMatrixLoc;
};

class SurfaceTextureNode : public QSGGeometryNode
{
public:
    SurfaceTextureNode(const QAndroidJniObject &surfaceTexture, GLuint textureId)
        : QSGGeometryNode()
        , m_surfaceTexture(surfaceTexture)
        , m_geometry(QSGGeometry::defaultAttributes_TexturedPoint2D(), 4)
        , m_textureId(textureId)
    {
        // we're going to use "preprocess" method to update the texture image
        // and to get the new matrix.
        setFlag(UsePreprocess);

        setGeometry(&m_geometry);

        // Create and set our SurfaceTextureShader
        QSGSimpleMaterial<State> *material = SurfaceTextureShader::createMaterial();
        material->state()->textureId = m_textureId;
        material->setFlag(QSGMaterial::Blending, true);
        setMaterial(material);
        setFlag(OwnsMaterial);

        // We're going to get the transform matrix for every frame
        // so, let's create the array once
        QAndroidJniEnvironment env;
        jfloatArray array = env->NewFloatArray(16);
        m_uSTMatrixArray = jfloatArray(env->NewGlobalRef(array));
        env->DeleteLocalRef(array);

        obj = m_surfaceTexture.object();
        updateTexMethod = env->GetMethodID(env->FindClass("android/graphics/SurfaceTexture"), "updateTexImage", "()V");
        getTransformMatrixMethod = env->GetMethodID(env->FindClass("android/graphics/SurfaceTexture"), "getTransformMatrix", "([F)V");

        qDebug() << Q_FUNC_INFO;
    }

    ~SurfaceTextureNode() override
    {
        // delete the global reference, now the gc is free to free it
        QAndroidJniEnvironment()->DeleteGlobalRef(m_uSTMatrixArray);
    }

    // QSGNode interface
    void preprocess() override;

private:
    QAndroidJniObject m_surfaceTexture;
    QSGGeometry m_geometry;
    jfloatArray m_uSTMatrixArray = nullptr;
    GLuint m_textureId;
    jmethodID updateTexMethod;
    jmethodID getTransformMatrixMethod;
    jobject obj;
    QAndroidJniEnvironment env;

};

void SurfaceTextureNode::preprocess()
{
    //    qDebug() << QDateTime::currentDateTime().toMSecsSinceEpoch() << "preprocess start";
    auto mat = static_cast<QSGSimpleMaterial<State> *>(material());
    if (!mat)
        return;

    // update the texture content
    env->CallVoidMethod(obj, updateTexMethod);
//    m_surfaceTexture.callMethod<void>("updateTexImage");

    // get the new texture transform matrix
    env->CallVoidMethod(obj, getTransformMatrixMethod, m_uSTMatrixArray);
//    m_surfaceTexture.callMethod<void>("getTransformMatrix", "([F)V", m_uSTMatrixArray);

    env->GetFloatArrayRegion(m_uSTMatrixArray, 0, 16, mat->state()->uSTMatrix.data());

    //    qDebug() << QDateTime::currentDateTime().toMSecsSinceEpoch() << "preprocess finish";

//    jfloat* data = env->GetFloatArrayElements(m_uSTMatrixArray, NULL);
//    if (data != NULL) {
//        memcpy(mat->state()->uSTMatrix.data(), data, 16 * sizeof (jfloat));
//        env->ReleaseFloatArrayElements(m_uSTMatrixArray, data, JNI_ABORT);
//    }
}

QSurfaceTexture::QSurfaceTexture(QQuickItem *parent)
    : QQuickItem(parent)
{
    qDebug() << Q_FUNC_INFO;
    setFlags(ItemHasContents);
}

QSurfaceTexture::~QSurfaceTexture()
{
    // Delete our texture
    if (mTextureId) {
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
        glDeleteTextures(1, &mTextureId);
    }
}

const QAndroidJniObject &QSurfaceTexture::surfaceTexture() const { return mSurfaceTexture; }

QSGNode *QSurfaceTexture::updatePaintNode(QSGNode *n, QQuickItem::UpdatePaintNodeData *)
{
//    qDebug() << QDateTime::currentDateTime().toMSecsSinceEpoch() << "updatePaintNode start";
    SurfaceTextureNode *node = static_cast<SurfaceTextureNode *>(n);
    if (!node) {
        qDebug() << QDateTime::currentDateTime().toMSecsSinceEpoch() << "updatePaintNode finish";

        // Create texture
        glGenTextures(1, &mTextureId);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, mTextureId);

        // Can't do mipmapping with camera source
        glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_NEAREST);


        // Clamp to edge is the only option
//        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
//        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glDisable(GL_TEXTURE_2D);
        glEnable(GL_TEXTURE_EXTERNAL_OES);

        // Create surface texture Java object
        mSurfaceTexture = QAndroidJniObject("android/graphics/SurfaceTexture", "(I)V", mTextureId);

        // We need to setOnFrameAvailableListener, to be notify when a new frame was decoded
        // and is ready to be displayed. Check android/src/com/kdab/android/SurfaceTextureListener.java
        // file for implementation details.
        mSurfaceTexture.callMethod<void>("setOnFrameAvailableListener",
                                          "(Landroid/graphics/SurfaceTexture$OnFrameAvailableListener;)V",
                                          QAndroidJniObject("com/vadim/android/SurfaceTextureListener",
                                                            "(J)V", jlong(this)).object());

        // Create our SurfaceTextureNode
        node = new SurfaceTextureNode(mSurfaceTexture, mTextureId);
        emit surfaceTextureChanged(this);
    }

    // flip vertical
    QRectF &&rect = boundingRect();
    float &&tmp = rect.top();
    rect.setTop(rect.bottom());
    rect.setBottom(tmp);

    QSGGeometry::updateTexturedRectGeometry(node->geometry(), rect, QRectF(0, 0, 1, 1));
    node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);

    //    qDebug() << QDateTime::currentDateTime().toMSecsSinceEpoch() << "updatePaintNode finish";
    return node;
}

extern "C" void Java_com_vadim_android_SurfaceTextureListener_frameAvailable(JNIEnv */*env*/, jobject /*thiz*/ , jlong ptr)
{
    // a new frame was decoded, let's update our item
//    qDebug() << QDateTime::currentDateTime().toMSecsSinceEpoch() << "frameAvailable";
    QMetaObject::invokeMethod(reinterpret_cast<QSurfaceTexture *>(ptr), "update", Qt::QueuedConnection);
//    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}
