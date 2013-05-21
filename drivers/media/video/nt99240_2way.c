
#include "generic_sensor.h"
/*
*      Driver Version Note
*v0.0.1: this driver is compatible with generic_sensor
*v0.1.1:
*        add sensor_focus_af_const_pause_usr_cb;
*/
static int version = KERNEL_VERSION(0,1,1);
module_param(version, int, S_IRUGO);

static int debug =1;
module_param(debug, int, S_IRUGO|S_IWUSR);

#define dprintk(level, fmt, arg...) do {			\
	if (debug >= level) 					\
	printk(KERN_WARNING fmt , ## arg); } while (0)

/* Sensor Driver Configuration Begin */
#define SENSOR_NAME RK29_CAM_SENSOR_NT99240
#define SENSOR_V4L2_IDENT V4L2_IDENT_NT99240
#define SENSOR_ID 0x2400
#define SENSOR_BUS_PARAM                     (SOCAM_MASTER |\
                                             SOCAM_PCLK_SAMPLE_RISING|SOCAM_HSYNC_ACTIVE_HIGH| SOCAM_VSYNC_ACTIVE_HIGH|\
                                             SOCAM_DATA_ACTIVE_HIGH | SOCAM_DATAWIDTH_8  |SOCAM_MCLK_24MHZ)
#define SENSOR_PREVIEW_W                     800
#define SENSOR_PREVIEW_H                     600
#define SENSOR_PREVIEW_FPS                   15000     // 15fps 
#define SENSOR_FULLRES_L_FPS                 5000      // 5fps
#define SENSOR_FULLRES_H_FPS                 10000      // 10fps
#define SENSOR_720P_FPS                      15000
#define SENSOR_1080P_FPS                     0

#define SENSOR_REGISTER_LEN                  2         // sensor register address bytes
#define SENSOR_VALUE_LEN                     1         // sensor register value bytes
                                    
static unsigned int SensorConfiguration = 0;
static unsigned int SensorChipID[] = {SENSOR_ID};

/* Sensor Driver Configuration End */


#define SENSOR_NAME_STRING(a) STR(CONS(SENSOR_NAME, a))
#define SENSOR_NAME_VARFUN(a) CONS(SENSOR_NAME, a)

#define SensorRegVal(a,b) CONS4(SensorReg,SENSOR_REGISTER_LEN,Val,SENSOR_VALUE_LEN)(a,b)
#define sensor_write(client,reg,v) CONS4(sensor_write_reg,SENSOR_REGISTER_LEN,val,SENSOR_VALUE_LEN)(client,(reg),(v))
#define sensor_read(client,reg,v) CONS4(sensor_read_reg,SENSOR_REGISTER_LEN,val,SENSOR_VALUE_LEN)(client,(reg),(v))
#define sensor_write_array generic_sensor_write_array

struct sensor_parameter
{
	unsigned int PreviewDummyPixels;
	unsigned int CaptureDummyPixels;
	unsigned int preview_exposure;
	unsigned short int preview_line_width;
	unsigned short int preview_gain_dr;
	unsigned short int preview_gain_ar;
	unsigned short int preview_gain_dgr;
	unsigned short int preview_gain_agr;
	unsigned short int preview_gain_dgb;
	unsigned short int preview_gain_agb;
	unsigned short int preview_gain_db;
	unsigned short int preview_gain_ab;
	unsigned short int preview_gain_dglobal;
	unsigned short int preview_gain_aglobal;
	unsigned short int preview_awb_r;
	unsigned short int preview_awb_b;
	

	unsigned short int PreviewPclk;
	unsigned short int CapturePclk;
	char awb[6];
};

struct specific_sensor{
	struct generic_sensor common_sensor;
	//define user data below
	struct sensor_parameter parameter;

};

/*
*  The follow setting need been filled.
*  
*  Must Filled:
*  sensor_init_data :               Sensor initial setting;
*  sensor_fullres_lowfps_data :     Sensor full resolution setting with best auality, recommand for video;
*  sensor_preview_data :            Sensor preview resolution setting, recommand it is vga or svga;
*  sensor_softreset_data :          Sensor software reset register;
*  sensor_check_id_data :           Sensir chip id register;
*
*  Optional filled:
*  sensor_fullres_highfps_data:     Sensor full resolution setting with high framerate, recommand for video;
*  sensor_720p:                     Sensor 720p setting, it is for video;
*  sensor_1080p:                    Sensor 1080p setting, it is for video;
*
*  :::::WARNING:::::
*  The SensorEnd which is the setting end flag must be filled int the last of each setting;
*/

/* Sensor initial setting */
static struct rk_sensor_reg sensor_init_data[] ={
	{0x32F0, 0x01},
	{0x3028, 0x07},
	{0x3029, 0x00},
	{0x302a, 0x04},
	{0x3290, 0x01},
	{0x3291, 0x94},
	{0x3296, 0x01},
	{0x3297, 0x6D},
	{0x3210, 0x3A},
	{0x3211, 0x3A},
	{0x3212, 0x3A},
	{0x3213, 0x3A},
	{0x3214, 0x2A},
	{0x3215, 0x2A},
	{0x3216, 0x2A},
	{0x3217, 0x2A},
	{0x3218, 0x2A},
	{0x3219, 0x2A},
	{0x321A, 0x2A},
	{0x321B, 0x2A},
	{0x321C, 0x22},
	{0x321D, 0x22},
	{0x321E, 0x22},
	{0x321F, 0x22},
	{0x3230, 0x30},
	{0x3231, 0x00},
	{0x3232, 0xCF},
	{0x3233, 0x00},
	{0x3234, 0x05},
	{0x3302, 0x00},
	{0x3303, 0x5F},
	{0x3304, 0x00},
	{0x3305, 0x7E},
	{0x3306, 0x00},
	{0x3307, 0x22},
	{0x3308, 0x07},
	{0x3309, 0xC3},
	{0x330A, 0x07},
	{0x330B, 0x33},
	{0x330C, 0x01},
	{0x330D, 0x0A},
	{0x330E, 0x00},
	{0x330F, 0xD3},
	{0x3310, 0x07},
	{0x3311, 0x3B},
	{0x3312, 0x07},
	{0x3313, 0xF2},
	{0x3024, 0x00},
	{0x303E, 0x04},
	{0x303F, 0x02},
	{0x3040, 0xFF},
	{0x3041, 0x02},
	{0x3051, 0xE0},
	{0x3052, 0x10},
	{0x305f, 0x22},
	{0x32b0, 0x00},
	{0x32b1, 0x90},
	{0x32BB, 0x0b},
	{0x32bd, 0x08},
	{0x32be, 0x06},
	{0x32bf, 0x4a},
	{0x32c0, 0x20},
	{0x32C3, 0x06},
	{0x32c5, 0x28},
	{0x32cd, 0x02},
	{0x32d3, 0x12},
	{0x3118, 0xF2},
	{0x3119, 0xF2},
	{0x311A, 0x13},
	{0x3106, 0x03},
	{0x3108, 0x00},
	{0x3112, 0xF1},
	{0x3113, 0x55},
	{0x3114, 0x05},
	{0x3012, 0x03},
	{0x3013, 0xC0},
	{0x3326, 0x02},
	{0x3327, 0x04},
	{0x3328, 0x04},
	{0x3329, 0x02},
	{0x332A, 0x02},
	{0x332B, 0x1D},
	{0x332C, 0x1D},
	{0x332D, 0x04},
	{0x332E, 0x1E},
	{0x332F, 0x1F},
	{0x32f6, 0x0B},
	{0x3343, 0xE0},
	{0x333B, 0x10},
	{0x333C, 0x14},
	{0x333D, 0x30},
	{0x333E, 0x30},
	{0x333F, 0x88},
	{0x3340, 0x84},
	{0x3341, 0x50},
	{0x3342, 0x50},
	{0x3344, 0x20},
	{0x3345, 0x28},
	{0x3346, 0x3F},
	{0x3347, 0x3F},
	{0x3348, 0xF0},
	{0x3349, 0x40},
	{0x334A, 0x40},
	{0x334B, 0x20},
	{0x334C, 0x20},
	{0x334D, 0x00},
	{0x32f6, 0x0B},
	{0x32f9, 0x63},
	{0x32fA, 0x36},
	{0x3338, 0x18},
	{0x3339, 0xC6},
	{0x333A, 0x6C},
	{0x3109, 0x82},
	SensorEnd
};
/* Senor full resolution setting: recommand for capture */
static struct rk_sensor_reg sensor_fullres_lowfps_data[] ={
	{0x3200, 0x3e},
	{0x3201, 0x0f},
	{0x3028, 0x07},
	{0x3029, 0x00},
	{0x302a, 0x04},
	{0x3022, 0x24},
	{0x3023, 0x24},
	{0x3128, 0x01},
	{0x3002, 0x00},
	{0x3003, 0x04},
	{0x3004, 0x00},
	{0x3005, 0x04},
	{0x3006, 0x06},
	{0x3007, 0x43},
	{0x3008, 0x04},
	{0x3009, 0xb3},
	{0x300a, 0x09},
	{0x300b, 0xc4},
	{0x300c, 0x07},
	{0x300d, 0x80},
	{0x300e, 0x06},
	{0x300f, 0x40},
	{0x3010, 0x04},
	{0x3011, 0xb0},
	{0x3052, 0x10},
	{0x32bb, 0x1b},
	{0x32bc, 0x40},
	{0x32c1, 0x27},
	{0x32c2, 0x80},
	{0x32c8, 0x60},
	{0x32c9, 0x50},
	{0x32c4, 0x00},
	{0x3201, 0x3f},
	{0x3021, 0x06},
	{0x3060, 0x01},
	SensorEnd
};
/* Senor full resolution setting: recommand for video */
static struct rk_sensor_reg sensor_fullres_highfps_data[] ={
	{0x3200, 0x3e},
	{0x3201, 0x0f},
	{0x3028, 0x07},
	{0x3029, 0x00},
	{0x302a, 0x04},
	{0x3022, 0x24},
	{0x3023, 0x24},
	{0x3128, 0x01},
	{0x3002, 0x00},
	{0x3003, 0x04},
	{0x3004, 0x00},
	{0x3005, 0x04},
	{0x3006, 0x06},
	{0x3007, 0x43},
	{0x3008, 0x04},
	{0x3009, 0xb3},
	{0x300a, 0x08},
	{0x300b, 0x34},
	{0x300c, 0x04},
	{0x300d, 0xbd},
	{0x300e, 0x06},
	{0x300f, 0x40},
	{0x3010, 0x04},
	{0x3011, 0xb0},
	{0x3052, 0x10},
	{0x32bb, 0x1b},
	{0x32bc, 0x40},
	{0x32c1, 0x24},
	{0x32c2, 0x74},
	{0x32c8, 0x72},
	{0x32c9, 0x5f},
	{0x32c4, 0x00},
	{0x3201, 0x3f},
	{0x3021, 0x16},
	{0x3060, 0x01},
	SensorEnd
};
/* Preview resolution setting*/
static struct rk_sensor_reg sensor_preview_data[] =
{
	{0x32e0, 0x03},
	{0x32e1, 0x20},
	{0x32e2, 0x02},
	{0x32e3, 0x58},
	{0x32e4, 0x01},
	{0x32e5, 0x00},
	{0x32e6, 0x00},
	{0x32e7, 0x00},
	{0x3200, 0x3e},
	{0x3201, 0x0f},
	{0x3028, 0x07},
	{0x3029, 0x00},
	{0x302a, 0x04},
	{0x3022, 0x24},
	{0x3023, 0x64},
	{0x3128, 0x01},
	{0x3002, 0x00},
	{0x3003, 0x04},
	{0x3004, 0x00},
	{0x3005, 0x04},
	{0x3006, 0x06},
	{0x3007, 0x43},
	{0x3008, 0x04},
	{0x3009, 0xb3},
	{0x300a, 0x09},
	{0x300b, 0xc4},
	{0x300c, 0x02},
	{0x300d, 0x80},
	{0x300e, 0x06},
	{0x300f, 0x40},
	{0x3010, 0x02},
	{0x3011, 0x58},
	{0x3052, 0x10},
	{0x32bb, 0x1b},
	{0x32bc, 0x40},
	{0x32c1, 0x22},
	{0x32c2, 0xa0},
	{0x32c8, 0x60},
	{0x32c9, 0x50},
	{0x32c4, 0x00},
	{0x3201, 0x7f},
	{0x3021, 0x06},
	{0x3060, 0x01},
	SensorEnd
};
/* 1280x720 */
static struct rk_sensor_reg sensor_720p[]={
	{0x3200, 0x3e},
	{0x3201, 0x0f},
	{0x3028, 0x07},
	{0x3029, 0x00},
	{0x302a, 0x04},
	{0x3022, 0x24},
	{0x3023, 0x24},
	{0x3128, 0x01},
	{0x3002, 0x00},
	{0x3003, 0xa4},
	{0x3004, 0x00},
	{0x3005, 0xf4},
	{0x3006, 0x05},
	{0x3007, 0xa3},
	{0x3008, 0x03},
	{0x3009, 0xc3},
	{0x300a, 0x06},
	{0x300b, 0xf4},
	{0x300c, 0x03},
	{0x300d, 0x82},
	{0x300e, 0x05},
	{0x300f, 0x00},
	{0x3010, 0x02},
	{0x3011, 0xd0},
	{0x3052, 0x10},
	{0x32bb, 0x1b},
	{0x32bc, 0x40},
	{0x32c1, 0x23},
	{0x32c2, 0xaa},
	{0x32c8, 0x86},
	{0x32c9, 0x70},
	{0x32c4, 0x00},
	{0x3201, 0x3f},
	{0x3021, 0x16},
	{0x3060, 0x01},
	SensorEnd
};

/* 1920x1080 */
static struct rk_sensor_reg sensor_1080p[]={
	SensorEnd
};


static struct rk_sensor_reg sensor_softreset_data[]={
    SensorRegVal(0x3021,0x61),
	SensorEnd
};

static struct rk_sensor_reg sensor_check_id_data[]={
    SensorRegVal(0x3000,0),
    SensorRegVal(0x3001,0),
	SensorEnd
};
/*
*  The following setting must been filled, if the function is turn on by CONFIG_SENSOR_xxxx
*/
static struct rk_sensor_reg sensor_WhiteB_Auto[]=
{
	{0x3201, 0x7F},
	SensorEnd
};
/* Cloudy Colour Temperature : 6500K - 8000K  */
static	struct rk_sensor_reg sensor_WhiteB_Cloudy[]=
{
	//[WB-CLOUDY]
	{0x3201, 0x6F},
	{0x3290, 0x01},
	{0x3291, 0x51},
	{0x3296, 0x01},
	{0x3297, 0x00},
	SensorEnd
};
/* ClearDay Colour Temperature : 5000K - 6500K	*/
static	struct rk_sensor_reg sensor_WhiteB_ClearDay[]=
{
	//[WB-DAYLIGHT]
	{0x3201, 0x6F},
	{0x3290, 0x01},
	{0x3291, 0xD8},
	{0x3296, 0x01},
	{0x3297, 0x58}, 
	SensorEnd
};
/* Office Colour Temperature : 3500K - 5000K  */
static	struct rk_sensor_reg sensor_WhiteB_TungstenLamp1[]=
{
	//[WB-INCANDESCENCE]
	{0x3201, 0x6F},
	{0x3290, 0x01},
	{0x3291, 0x30},
	{0x3296, 0x01},
	{0x3297, 0xCB},
	SensorEnd

};
/* Home Colour Temperature : 2500K - 3500K	*/
static	struct rk_sensor_reg sensor_WhiteB_TungstenLamp2[]=
{
	//[WB-FLUORESCENT]
	{0x3201, 0x6F},
	{0x3290, 0x02},
	{0x3291, 0x00},
	{0x3296, 0x02},
	{0x3297, 0x00},
	SensorEnd
};
static	struct rk_sensor_reg sensor_WhiteB_TungstenLamp3[]=
{
	//[WB-TUNGSTEN]
	{0x3201, 0x6F},
	{0x3290, 0x01},
	{0x3291, 0x00},
	{0x3296, 0x02},
	{0x3297, 0x20},
	SensorEnd
};

static struct rk_sensor_reg *sensor_WhiteBalanceSeqe[] = {sensor_WhiteB_Auto, sensor_WhiteB_TungstenLamp1,sensor_WhiteB_TungstenLamp2,
	sensor_WhiteB_ClearDay, sensor_WhiteB_Cloudy, sensor_WhiteB_TungstenLamp3,NULL,
};

static	struct rk_sensor_reg sensor_Brightness0[]=
{
	// Brightness -2
	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness1[]=
{
	// Brightness -1

	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness2[]=
{
	//	Brightness 0

	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness3[]=
{
	// Brightness +1

	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness4[]=
{
	//	Brightness +2

	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness5[]=
{
	//	Brightness +3

	SensorEnd
};
static struct rk_sensor_reg *sensor_BrightnessSeqe[] = {sensor_Brightness0, sensor_Brightness1, sensor_Brightness2, sensor_Brightness3,
	sensor_Brightness4, sensor_Brightness5,NULL,
};

static	struct rk_sensor_reg sensor_Effect_Normal[] =
{
	{0x32F1, 0x00},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_WandB[] =
{	
	//[SE-GrayScale]
	{0x32F1, 0x01},	
};

static	struct rk_sensor_reg sensor_Effect_Sepia[] =
{
	//[SE-SEPIA]
	{0x32F1, 0x02},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_Inverse[] =
{
	//[SE-Inverse] 
	{0x32F1, 0x03},
	SensorEnd
};
static	struct rk_sensor_reg sensor_Effect_Bluish[] =
{
	//[SE-SEPIABlue]
	{0x32F1, 0x05},
	{0x32F4, 0xF0},
	{0x32F5, 0x80},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_Green[] =
{
	//[SE-SEPIAGreen]
	{0x32F1, 0x05},
	{0x32F4, 0x60},
	{0x32F5, 0x20},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_Solarization[] =
{
	
	//[SE-Solarization]
	{0x32F1, 0x04},
	SensorEnd
};


static struct rk_sensor_reg *sensor_EffectSeqe[] = {sensor_Effect_Normal, sensor_Effect_WandB, sensor_Effect_Inverse,sensor_Effect_Sepia,
	sensor_Effect_Bluish, sensor_Effect_Green,sensor_Effect_Solarization,NULL,
};

static	struct rk_sensor_reg sensor_Exposure04[]=
{
	//[EV-4]				
	{0x32F6, 0X0C},
	{0x32F2, 0x40},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure03[]=
{
	//[EV-3]              
	{0x32F6, 0X0C},
	{0x32F2, 0x50},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure02[]=
{
	//[EV-2]				
	{0x32F6, 0X0C},
	{0x32F2, 0x60},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure01[]=
{
	//[EV-1]				
	{0x32F6, 0X0C},
	{0x32F2, 0x70},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure00[]=
{
	
	//[EV+0]				
	{0x32F6, 0X0C},
	{0x32F2, 0x80},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure11[]=
{
	//[EV+1]              
	{0x32F6, 0X0C},
	{0x32F2, 0x90},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure12[]=
{
	//[EV+2]				
	{0x32F6, 0X0C},
	{0x32F2, 0xA0},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure13[]=
{
	//[EV+3]				
	{0x32F6, 0X0C},
	{0x32F2, 0xB0},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure14[]=
{
	//[EV+4]				
	{0x32F6, 0X0C},
	{0x32F2, 0xC0},
	SensorEnd
};

static struct rk_sensor_reg *sensor_ExposureSeqe[] = {sensor_Exposure04,sensor_Exposure03, sensor_Exposure02, sensor_Exposure01, sensor_Exposure00,
	sensor_Exposure11, sensor_Exposure12,sensor_Exposure13,sensor_Exposure14,NULL,
};

static	struct rk_sensor_reg sensor_Saturation0[]=
{
	SensorEnd
};

static	struct rk_sensor_reg sensor_Saturation1[]=
{
	SensorEnd
};

static	struct rk_sensor_reg sensor_Saturation2[]=
{
	SensorEnd
};
static struct rk_sensor_reg *sensor_SaturationSeqe[] = {sensor_Saturation0, sensor_Saturation1, sensor_Saturation2, NULL,};

static	struct rk_sensor_reg sensor_Contrast04[]=
{
	//[Contrast : -4]   
	{0x32F6, 0x0C}, 
	{0x32C2, 0x40},
	{0x32F2, 0x40},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast03[]=
{
	//[Contrast : -3]	 
	{0x32F6, 0x0C},    
	{0x32C2, 0x30},
	{0x32F2, 0x50},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast02[]=
{
	//[Contrast : -2]    
	{0x32F6, 0x0C},  
	{0x32C2, 0x20},
	{0x32F2, 0x60},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast01[]=
{
	//[Contrast : -1] 
	{0x32F6, 0x0C}, 	
	{0x32C2, 0x10},
	{0x32F2, 0x70},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast00[]=
{
	//[Contrast : 0]	
	{0x32F6, 0x0C},     
	{0x32C2, 0x00},
	{0x32F2, 0x80},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast11[]=
{
	//[Contrast : +1]   
	{0x32F6, 0x0C},   
	{0x32C2, 0xF0},
	{0x32F2, 0x90},
	SensorEnd
};


static	struct rk_sensor_reg sensor_Contrast12[]=
{
	//[Contrast : +2]  
	{0x32F6, 0x0C},    
	{0x32C2, 0xE0},
	{0x32F2, 0xA0},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast13[]=
{
	//[Contrast : +3]	 
	{0x32F6, 0x0C},    
	{0x32C2, 0xD0},
	{0x32F2, 0xB0},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast14[]=
{
	//[Contrast : +4] 
	{0x32F6, 0x0C}, 	
	{0x32C2, 0xC0},
	{0x32F2, 0xC0},
	SensorEnd
};

static struct rk_sensor_reg *sensor_ContrastSeqe[] = {sensor_Contrast04, sensor_Contrast03, sensor_Contrast02, sensor_Contrast01,
	sensor_Contrast00, sensor_Contrast11, sensor_Contrast12, sensor_Contrast13, sensor_Contrast14,NULL,
};

static	struct rk_sensor_reg sensor_SceneAuto[] =
{
	SensorEnd
};

static	struct rk_sensor_reg sensor_SceneNight[] =
{
	SensorEnd
};
static struct rk_sensor_reg *sensor_SceneSeqe[] = {sensor_SceneAuto, sensor_SceneNight,NULL,};

static struct rk_sensor_reg sensor_Zoom0[] =
{
	SensorEnd
};

static struct rk_sensor_reg sensor_Zoom1[] =
{
	SensorEnd
};

static struct rk_sensor_reg sensor_Zoom2[] =
{
	SensorEnd
};


static struct rk_sensor_reg sensor_Zoom3[] =
{
	SensorEnd
};
static struct rk_sensor_reg *sensor_ZoomSeqe[] = {sensor_Zoom0, sensor_Zoom1, sensor_Zoom2, sensor_Zoom3, NULL,};

/*
* User could be add v4l2_querymenu in sensor_controls by new_usr_v4l2menu
*/
static struct v4l2_querymenu sensor_menus[] =
{
	//white balance
	new_usr_v4l2menu(V4L2_CID_DO_WHITE_BALANCE,0,"auto",0),
	new_usr_v4l2menu(V4L2_CID_DO_WHITE_BALANCE,1,"incandescent",0),
	new_usr_v4l2menu(V4L2_CID_DO_WHITE_BALANCE,2,"fluorescent",0),
	new_usr_v4l2menu(V4L2_CID_DO_WHITE_BALANCE,3,"daylight",0),
	new_usr_v4l2menu(V4L2_CID_DO_WHITE_BALANCE,4,"cloudy-daylight",0),
	new_usr_v4l2menu(V4L2_CID_DO_WHITE_BALANCE,5,"tungsten",0),

	//speical effect
	new_usr_v4l2menu(V4L2_CID_EFFECT,0,"none",0),
	new_usr_v4l2menu(V4L2_CID_EFFECT,1,"mono",0),
	new_usr_v4l2menu(V4L2_CID_EFFECT,2,"negative",0),
	new_usr_v4l2menu(V4L2_CID_EFFECT,3,"sepia",0),
	new_usr_v4l2menu(V4L2_CID_EFFECT,4,"posterize",0),
	new_usr_v4l2menu(V4L2_CID_EFFECT,5,"aqua",0),
	new_usr_v4l2menu(V4L2_CID_EFFECT,6,"solarize",0),
};
/*
* User could be add v4l2_queryctrl in sensor_controls by new_user_v4l2ctrl
*/
static struct sensor_v4l2ctrl_usr_s sensor_controls[] =
{
	new_user_v4l2ctrl(V4L2_CID_DO_WHITE_BALANCE,V4L2_CTRL_TYPE_MENU,"White Balance Control", 0, 5, 1, 0,sensor_v4l2ctrl_default_cb, sensor_WhiteBalanceSeqe),
	new_user_v4l2ctrl(V4L2_CID_EXPOSURE,V4L2_CTRL_TYPE_INTEGER,"Exposure Control", -4, 4, 1, 0,sensor_v4l2ctrl_default_cb, sensor_ExposureSeqe),
	new_user_v4l2ctrl(V4L2_CID_EFFECT,V4L2_CTRL_TYPE_MENU,"Effect Control", 0, 6, 1, 0,sensor_v4l2ctrl_default_cb, sensor_EffectSeqe),
	new_user_v4l2ctrl(V4L2_CID_CONTRAST,V4L2_CTRL_TYPE_INTEGER,"Contrast Control", -4, 4, 1, 0,sensor_v4l2ctrl_default_cb, sensor_ContrastSeqe),
};

//MUST define the current used format as the first item   
static struct rk_sensor_datafmt sensor_colour_fmts[] = {
	{V4L2_MBUS_FMT_YUYV8_2X8, V4L2_COLORSPACE_JPEG},
	{V4L2_MBUS_FMT_UYVY8_2X8, V4L2_COLORSPACE_JPEG} 
};
static struct soc_camera_ops sensor_ops;


/*
**********************************************************
* Following is local code:
* 
* Please codeing your program here 
**********************************************************
*/
static int sensor_parameter_record(struct i2c_client *client)
{
	u8 ret_l,ret_h;
	int tp_l,tp_h;
	
	struct generic_sensor *sensor = to_generic_sensor(client);
	struct specific_sensor *spsensor = to_specific_sensor(sensor);

    sensor_read(client, 0x3200, &ret_l);
    sensor_write(client, 0x3200, ret_l&0xf9); 
	sensor_read(client, 0x3201, &ret_l);
    sensor_write(client, 0x3201, ret_l&0xcf);  //stop ae awb

	//read back ae
	sensor_read(client,0x3012,&ret_h);
	sensor_read(client,0x3013, &ret_l);
	tp_l = ret_l;
	tp_h = ret_h;
	spsensor->parameter.preview_exposure = ((tp_h<<8) & 0xF0) |((tp_l) & 0x0F);
	
	//Read back AGC Gain for preview
	sensor_read(client,0x3014, &ret_l);
	spsensor->parameter.preview_gain_dr = ret_l;
	sensor_read(client,0x3015, &ret_l);
	spsensor->parameter.preview_gain_ar = ret_l;
	sensor_read(client,0x3016, &ret_l);
	spsensor->parameter.preview_gain_dgr = ret_l;
	sensor_read(client,0x3017, &ret_l);
	spsensor->parameter.preview_gain_agr = ret_l;
	sensor_read(client,0x3018, &ret_l);
	spsensor->parameter.preview_gain_dgb = ret_l;
	sensor_read(client,0x3019, &ret_l);
	spsensor->parameter.preview_gain_agb = ret_l;
	sensor_read(client,0x301a, &ret_l);
	spsensor->parameter.preview_gain_db = ret_l;
	sensor_read(client,0x301b, &ret_l);
	spsensor->parameter.preview_gain_ab = ret_l;
	sensor_read(client,0x301c, &ret_l);
	spsensor->parameter.preview_gain_dglobal= ret_l;
	sensor_read(client,0x301d, &ret_l);
	spsensor->parameter.preview_gain_aglobal= ret_l;

	spsensor->parameter.CapturePclk = 48000;
	spsensor->parameter.PreviewPclk = 48000;
	sensor_read(client,0x300a, &ret_h);
	sensor_read(client,0x300b, &ret_l);
	tp_l = ret_l;
	tp_h = ret_h;
	spsensor->parameter.PreviewDummyPixels = (tp_l&0x0F) | ((tp_h<<8)&0xF0);
	spsensor->parameter.CaptureDummyPixels = 0;

	#if 0
	sensor_read(client,0x3290, &ret_h);
	sensor_read(client,0x3291, &ret_l);
	tp_l = ret_l;
	tp_h = ret_h;
	spsensor->parameter.preview_awb_r = (tp_l&0x0F) | ((tp_h<<8)&0x10);

	sensor_read(client,0x3296, &ret_h);
	sensor_read(client,0x3297, &ret_l);
	tp_l = ret_l;
	tp_h = ret_h;
	spsensor->parameter.preview_awb_b = (tp_l&0x0F) | ((tp_h<<8)&0x10);
	#endif
	return 0;
}
#define OV2659_FULL_PERIOD_PIXEL_NUMS  (1940)  // default pixel#(w/o dummy pixels) in UXGA mode
#define OV2659_FULL_PERIOD_LINE_NUMS   (1238)  // default line#(w/o dummy lines) in UXGA mode
#define OV2659_PV_PERIOD_PIXEL_NUMS   (970)  // default pixel#(w/o dummy pixels) in SVGA mode
#define OV2659_PV_PERIOD_LINE_NUMS	  (618)   // default line#(w/o dummy lines) in SVGA mode

/* SENSOR EXPOSURE LINE LIMITATION */
#define OV2659_FULL_EXPOSURE_LIMITATION   (1236)
#define OV2659_PV_EXPOSURE_LIMITATION	  (618)

// SENSOR UXGA SIZE
#define OV2659_IMAGE_SENSOR_FULL_WIDTH	  (1600)
#define OV2659_IMAGE_SENSOR_FULL_HEIGHT   (1200)

#define OV2659_FULL_GRAB_WIDTH				(OV2659_IMAGE_SENSOR_FULL_WIDTH - 16)
#define OV2659_FULL_GRAB_HEIGHT 			(OV2659_IMAGE_SENSOR_FULL_HEIGHT - 12)

static int sensor_ae_transfer(struct i2c_client *client)
{
	//unsigned int prev_line_len,cap_line_len,shutter;
	struct generic_sensor *sensor = to_generic_sensor(client);
	struct specific_sensor *spsensor = to_specific_sensor(sensor);
	int preview_ae_integration, capture_ae_integration;
	int capture_pixel;
	u8 ret_h,ret_l;
	int val_h, val_l;
	
	mdelay(100);
	
	sensor_read(client, 0x3200, &ret_h);
	val_h=ret_h;
    sensor_write(client, 0x3200, ret_h&0xfd); 
	sensor_read(client, 0x3201, &ret_l);
	val_l=ret_l;
    sensor_write(client, 0x3201, ret_l&0xdf);  //stop ae awb
	
	sensor_read(client,0x300a, &ret_h);
	sensor_read(client,0x300b, &ret_l);
	capture_pixel = (ret_l&0x0F) | ((ret_h<<8)&0xF0);
	preview_ae_integration = spsensor->parameter.preview_exposure*spsensor->parameter.PreviewDummyPixels;
	capture_ae_integration = preview_ae_integration/capture_pixel;
	
	//write back ae
	sensor_write(client,0x3012,(capture_ae_integration>>8)&0x0F);
	sensor_write(client,0x3013,capture_ae_integration&0x0F);
		
	//write back AGC Gain for preview
	sensor_write(client,0x3014, spsensor->parameter.preview_gain_dr);
	sensor_write(client,0x3015, spsensor->parameter.preview_gain_ar);
	sensor_write(client,0x3016, spsensor->parameter.preview_gain_dgr);
	sensor_write(client,0x3017, spsensor->parameter.preview_gain_agr);
	sensor_write(client,0x3018, spsensor->parameter.preview_gain_dgb);
	sensor_write(client,0x3019, spsensor->parameter.preview_gain_agb);
	sensor_write(client,0x301a, spsensor->parameter.preview_gain_db);
	sensor_write(client,0x301b, spsensor->parameter.preview_gain_ab);
	sensor_write(client,0x301c, spsensor->parameter.preview_gain_dglobal);
	sensor_write(client,0x301d, spsensor->parameter.preview_gain_aglobal);

	#if 0
	sensor_write(client,0x3290, (spsensor->parameter.preview_awb_r>>8)&0x01);
	sensor_write(client,0x3291, spsensor->parameter.preview_awb_r&0x0F);
	sensor_write(client,0x3296, (spsensor->parameter.preview_awb_b>>8)&0x01 );
	sensor_write(client,0x3297, spsensor->parameter.preview_awb_b&0x0F );
	#endif

    //sensor_write(client, 0x3200, val_h); 
    //sensor_write(client, 0x3201, val_l);  //enable ae awb
	return 0;
}
/*
**********************************************************
* Following is callback
* If necessary, you could coding these callback
**********************************************************
*/
/*
* the function is called in open sensor  
*/
static int sensor_activate_cb(struct i2c_client *client)
{
    SENSOR_DG("%s",__FUNCTION__);
	
	
	return 0;
}
/*
* the function is called in close sensor
*/
static int sensor_deactivate_cb(struct i2c_client *client)
{
	struct generic_sensor *sensor = to_generic_sensor(client);

    SENSOR_DG("%s",__FUNCTION__);
    
	/* ddl@rock-chips.com : all sensor output pin must switch into Hi-Z */
	if (sensor->info_priv.funmodule_state & SENSOR_INIT_IS_OK) {
		sensor->info_priv.funmodule_state &= ~SENSOR_INIT_IS_OK;;
	}
	
	return 0;
}
/*
* the function is called before sensor register setting in VIDIOC_S_FMT  
*/
static int sensor_s_fmt_cb_th(struct i2c_client *client,struct v4l2_mbus_framefmt *mf, bool capture)
{
	printk("set fmt set!!!!!!!!!!!!");
	
    if (capture) {
		sensor_parameter_record(client);
		printk("set fmt set!!!!!!!!!!!!");     
    }
	

    return 0;
}
/*
* the function is called after sensor register setting finished in VIDIOC_S_FMT  
*/
static int sensor_s_fmt_cb_bh (struct i2c_client *client,struct v4l2_mbus_framefmt *mf, bool capture)
{
    if (capture) {
		sensor_ae_transfer(client);
		printk("set fmt set!!!!!!!!!!!!");  	
    }
	

    return 0;
}

static int sensor_try_fmt_cb_th(struct i2c_client *client,struct v4l2_mbus_framefmt *mf)
{
	return 0;
}

static int sensor_softrest_usr_cb(struct i2c_client *client,struct rk_sensor_reg *series)
{
	
	return 0;
}
static int sensor_check_id_usr_cb(struct i2c_client *client,struct rk_sensor_reg *series)
{
	return 0;
}

static int sensor_suspend(struct soc_camera_device *icd, pm_message_t pm_msg)
{
	//struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
		
	if (pm_msg.event == PM_EVENT_SUSPEND) {
		SENSOR_DG("Suspend");
		
	} else {
		SENSOR_TR("pm_msg.event(0x%x) != PM_EVENT_SUSPEND\n",pm_msg.event);
		return -EINVAL;
	}
	return 0;
}

static int sensor_resume(struct soc_camera_device *icd)
{

	SENSOR_DG("Resume");

	return 0;

}
static int sensor_mirror_cb (struct i2c_client *client, int mirror)
{
	char val;
	int err = 0;
    
    SENSOR_DG("mirror: %d",mirror);
	if (mirror) {
		err = sensor_read(client, 0x3022, &val);
		if (err == 0) {
			val |= 0x02;
			err = sensor_write(client, 0x3022, val);
		}
	} else {
		err = sensor_read(client, 0x3022, &val);
		if (err == 0) {
			val &= 0xfd;
			err = sensor_write(client, 0x3022, val);
		}
	}

	return err;    
}
/*
* the function is v4l2 control V4L2_CID_HFLIP callback  
*/
static int sensor_v4l2ctrl_mirror_cb(struct soc_camera_device *icd, struct sensor_v4l2ctrl_info_s *ctrl_info, 
                                                     struct v4l2_ext_control *ext_ctrl)
{
	struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if (sensor_mirror_cb(client,ext_ctrl->value) != 0)
		SENSOR_TR("sensor_mirror failed, value:0x%x",ext_ctrl->value);
	
	SENSOR_DG("sensor_mirror success, value:0x%x",ext_ctrl->value);
	return 0;
}

static int sensor_flip_cb(struct i2c_client *client, int flip)
{
	char val;
	int err = 0;	


    SENSOR_DG("flip: %d",flip);
	if (flip) {
		err = sensor_read(client, 0x3022, &val);
		if (err == 0) {
			val |= 0x01;
			err = sensor_write(client, 0x3022, val);
		}
	} else {
		err = sensor_read(client, 0x3022, &val);
		if (err == 0) {
			val &= 0xfe;
			err = sensor_write(client, 0x3022, val);
		}
	}

	return err;       
}
/*
* the function is v4l2 control V4L2_CID_VFLIP callback  
*/
static int sensor_v4l2ctrl_flip_cb(struct soc_camera_device *icd, struct sensor_v4l2ctrl_info_s *ctrl_info, 
                                                     struct v4l2_ext_control *ext_ctrl)
{
	struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if (sensor_flip_cb(client,ext_ctrl->value) != 0)
		SENSOR_TR("sensor_flip failed, value:0x%x",ext_ctrl->value);
	
	SENSOR_DG("sensor_flip success, value:0x%x",ext_ctrl->value);
	return 0;
}

static int sensor_focus_init_usr_cb(struct i2c_client *client){
	return 0;
}

static int sensor_focus_af_single_usr_cb(struct i2c_client *client){
	return 0;
}

static int sensor_focus_af_near_usr_cb(struct i2c_client *client){
	return 0;
}

static int sensor_focus_af_far_usr_cb(struct i2c_client *client){
	return 0;
}

static int sensor_focus_af_specialpos_usr_cb(struct i2c_client *client,int pos){
	return 0;
}

static int sensor_focus_af_const_usr_cb(struct i2c_client *client){
	return 0;
}
static int sensor_focus_af_const_pause_usr_cb(struct i2c_client *client)
{
    return 0;
}
static int sensor_focus_af_close_usr_cb(struct i2c_client *client){
	return 0;
}

static int sensor_focus_af_zoneupdate_usr_cb(struct i2c_client *client, int *zone_tm_pos)
{
	return 0;
}

/*
face defect call back
*/
static int 	sensor_face_detect_usr_cb(struct i2c_client *client,int on){
	return 0;
}

/*
*   The function can been run in sensor_init_parametres which run in sensor_probe, so user can do some
* initialization in the function. 
*/
static void sensor_init_parameters_user(struct specific_sensor* spsensor,struct soc_camera_device *icd)
{
    return;
}

/*
* :::::WARNING:::::
* It is not allowed to modify the following code
*/

sensor_init_parameters_default_code();

sensor_v4l2_struct_initialization();

sensor_probe_default_code();

sensor_remove_default_code();

sensor_driver_default_module_code();




