#include "cinder/app/AppNative.h"
#include "cinder/gl/gl.h"
#include "cinder/gl/Texture.h"
#include "pxcsensemanager.h"

using namespace ci;
using namespace ci::app;
using namespace std;

class MinimusApp : public AppNative {
public:
	void prepareSettings(Settings *pSettings);
	void setup();
	void mouseDown( MouseEvent event );	
	void update();
	void draw();
	void quit();

	//These, of course, could be one more general function
	void updateDepthSurface();
	void updateIrSurface();

private:
	PXCSenseManager *mPXC;
	PXCSizeI32 mDepthSize, mColorSize;
	gl::Texture mRgbTexture, mDepthTexture, mIrTexture;

	//These are just intermediaries for convenience, SurfaceT::Iter just makes it easy
	Surface8u mDepthSurface, mIrSurface;
	uint16_t *mDepthBuffer;
	uint16_t *mIrBuffer;
};

void MinimusApp::prepareSettings(Settings *pSettings)
{
	pSettings->setWindowSize(960,240);
	pSettings->setFrameRate(30);
}

void MinimusApp::setup()
{
	if(PXCSenseManager::CreateInstance(&mPXC)>=PXC_STATUS_NO_ERROR)
	{
		mPXC->EnableVideoStream(PXCImage::COLOR_FORMAT_RGB24,640,480,0);

		/* We pull the raw IR feed along with the depth map (on hardware that uses ir),
		we can also pull a UV map for simple depth to color registration by passing in
		PXCImage::IMAGE_OPTION_REQUIRE_UV_MAP */
		mPXC->EnableVideoStream(PXCImage::COLOR_FORMAT_DEPTH,640,480,0,PXCImage::IMAGE_OPTION_REQUIRE_IR_MAP);
	
		if(mPXC->Init()>=PXC_STATUS_NO_ERROR)
		{
			console() << "Init" << endl;

			/* Most Query function must be called after Init(), even simple things like image size
			 * Query functions related to the SenseManager context or device itself (e.g. QueryCaptureManager)
			 * should be called prior to Init()
			 */
			mColorSize = mPXC->QueryImageSizeByType(PXCImage::IMAGE_TYPE_COLOR);
			console() <<"Color Dims: " << mColorSize.width << ", " << mColorSize.height << endl;

			mDepthSize = mPXC->QueryImageSizeByType(PXCImage::IMAGE_TYPE_DEPTH);
			console() <<"Depth Dims: " << mDepthSize.width << ", " << mDepthSize.height << endl;
			
			mDepthSurface = Surface8u(mDepthSize.width, mDepthSize.height, false, SurfaceChannelOrder::RGB);
			mIrSurface = Surface8u(mDepthSize.width, mDepthSize.height, false, SurfaceChannelOrder::RGB);			
			
			mDepthBuffer = new uint16_t[mDepthSize.width*mDepthSize.height];
			mIrBuffer = new uint16_t[mDepthSize.width*mDepthSize.height];
		}
		else
			console() << "No Init" << endl;
	}
}

void MinimusApp::mouseDown( MouseEvent event )
{
}

void MinimusApp::update()
{
	/* We can query frames a few different ways, dictated by what we pass to AcquireFrame().
	 * Passing 'true' locks the hardware's loop until all configured frames are available,
	 * while 'false' grabs any frame as soon as it's available.  'true' is slightly less
	 * performant, but unless something goes horribly wrong, you shouldn't see much, if any,
	 * lag or deviation between frames when passing 'false'. */
	if(mPXC->AcquireFrame(false,0)>=PXC_STATUS_NO_ERROR) 
	{
		PXCImage *cRgbImg = mPXC->QueryImageByType(PXCImage::IMAGE_TYPE_COLOR);
		if(cRgbImg)
		{
			PXCImage::ImageData cRgbData;
			if(cRgbImg->AcquireAccess(PXCImage::ACCESS_READ, PXCImage::COLOR_FORMAT_RGB24, &cRgbData)>=PXC_STATUS_NO_ERROR)
			{
				mRgbTexture = gl::Texture(cRgbData.planes[0], GL_BGR, mColorSize.width, mColorSize.height);
				cRgbImg->ReleaseAccess(&cRgbData);
			}
		}

		PXCImage *cDepthImg = mPXC->QueryImageByType(PXCImage::IMAGE_TYPE_DEPTH);
		if(cDepthImg)
		{
			PXCImage::ImageData cDepthData;
			if(cDepthImg->AcquireAccess(PXCImage::ACCESS_READ, PXCImage::COLOR_FORMAT_DEPTH, &cDepthData)>=PXC_STATUS_NO_ERROR)
			{
				/* For convenience, you can read the depth buffer as a color buffer by passing PXCImage::COLOR_FORMAT_RGB32
				* tp AcquireAccess(), in which case, you can load planes[0] directly into an RGBA gl::Texture, otherwise
				* the raw depth is in cDepthData.planes[0] and is stored as pxcU16(uint16_t/unsigned short) in millimeters
				*/
				memcpy(mDepthBuffer, cDepthData.planes[0], (size_t)(cDepthData.pitches[0]*mDepthSize.height));

				/* IR and UV are stored in planes 1 and 2 respectively, but as that's subject to change with hardware, we use
				* the PLANE_IR_MAP and PLANE_UV_MAP enums for compatibility.  IR is also stored as pxcU16, error or low confidence
				is anything <200
				*/
				memcpy(mIrBuffer, cDepthData.planes[PXCImage::PLANE_IR_MAP], (size_t)(cDepthData.pitches[PXCImage::PLANE_IR_MAP]*mDepthSize.height));
				cDepthImg->ReleaseAccess(&cDepthData);
			}
		}
		mPXC->ReleaseFrame();
	}
	updateDepthSurface();

	/* When querying and processing the ir stream, you'll notice quite a bit of lag.
	 * This is a limitation with the current hardware and should be fixed on release,
	 * or sooner (if you need IR for anything).*/
	updateIrSurface();
}

void MinimusApp::draw()
{
	// clear out the window with black
	if(mRgbTexture)
		gl::draw(mRgbTexture, Rectf(0,0,320,240));
	if(mDepthTexture)
		gl::draw(mDepthTexture, Rectf(320,0,640,240));
	if(mIrTexture)
		gl::draw(mIrTexture, Rectf(640,0,960,240));
}

void MinimusApp::quit()
{
	mPXC->Release();
	mPXC = NULL;
}

void MinimusApp::updateDepthSurface()
{
	Surface8u::Iter itr = mDepthSurface.getIter(Area(0,0,mDepthSize.width,mDepthSize.height));		
	while(itr.line())
	{
		while(itr.pixel())
		{
			itr.r() = itr.g() = itr.b() = 0;
			int id = itr.y()*mDepthSize.width+itr.x();
			float cv = (float)mDepthBuffer[id];
			if(cv>0&&cv<2000)
			{
				cv = math<float>::clamp(lmap<float>(cv,1,2000,255,0),0,255);
				itr.r() = cv;
				itr.g() = cv;
				itr.b() = cv;
			}
		}
	}
	mDepthTexture = gl::Texture(mDepthSurface);
}

void MinimusApp::updateIrSurface()
{
	int cir = mIrBuffer[100] >> 8;
	console() << cir << endl;
	Surface8u::Iter itr = mIrSurface.getIter(Area(0,0,mDepthSize.width,mDepthSize.height));		
	while(itr.line())
	{
		while(itr.pixel())
		{
			itr.r() = itr.g() = itr.b() = 0;
			int id = itr.y()*mDepthSize.width+itr.x();
			float cv = (float)mIrBuffer[id];
			if(cv>100&&cv<1000)
			{
				cv = math<float>::clamp(lmap<float>(cv,100,1000,0,255),0,255);
				itr.r() = cv;
				itr.g() = cv*0.125f;
				itr.b() = 0;
			}
		}
	}
	mIrTexture = gl::Texture(mIrSurface);
}

CINDER_APP_NATIVE( MinimusApp, RendererGl )
