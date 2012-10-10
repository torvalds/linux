//a4, 52
#define MAX_TOUCH_MAJOR		10		//Charles added
#define MAX_WIDTH_MAJOR		15		//Charles added
#define MAX_TRACKID_ITEM		10	//Charles added

#define REPORT_TOUCH_MAJOR		5		//Charles added
#define REPORT_WIDTH_MAJOR		8		//Charles added

#define REPORT_TPKEY_DOWN		1
#define REPORT_TPKEY_UP			0

//#define RK29xx_ANDROID2_3_REPORT		//if the Android system is V2.3
//#undef RK29xx_ANDROID2_3_REPORT
#define RK29xx_ANDROID4_0_REPORT		//if the Android system is V4.0
//#undef RK29xx_ANDROID4_0_REPORT

//----------------------------------------//
//#define TOUCH_INT_PIN				RK29_PINx_PAx		//define INT Pin	Should be changed to the INT GPIO Port and Pin
//#define TOUCH_RESET_PIN			RK29_PINx_PAx			//define Reset Pin  Should be changed to the Reset GPIO Port and Pin
//#define SW_INT_IRQNO_PIO    TOUCH_INT_PIN

#define	byd693x_I2C_RATE	100*1000   //400KHz

#define USE_TOUCH_KEY

#ifdef USE_TOUCH_KEY
static const uint32_t TPKey_code[4] ={ KEY_SEARCH,KEY_MENU,KEY_HOME,KEY_BACK };
#endif

//struct ChipSetting byd693xcfg_Table1[]={							
//{ 2,0x08,	200/256,	200%256},	//	1	FTHD_H;FTHD_L	//手指按键阈值
//{ 2,0x0A,	120/256,	120%256},	//	2	NTHD_H;NTHD_L	//噪声阈值
//{ 2,0x0C,	SCREEN_MAX_X/256,	SCREEN_MAX_X%256},	//	3 RESX_H;RESX_L	//X分辨率
//{ 2,0x0E,	SCREEN_MAX_Y/256,	SCREEN_MAX_Y%256},	//	4	RESY_H;RESY_L	//Y分辨率
//};

static struct ChipSetting Resume[]={
{ 1, 0x07, 0x01, 0x00},	// Wakeup TP from Sleep mode
};

static struct ChipSetting Suspend[] ={
{ 1, 0x07, 0x00, 0x00}, // Enter Sleep mode
};
