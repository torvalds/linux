
#include "generic_sensor.h"

/*
*      Driver Version Note
*v0.0.1: this driver is compatible with generic_sensor
*v0.0.3:
*        add sensor_focus_af_const_pause_usr_cb;
*/
static int version = KERNEL_VERSION(0,0,3);
module_param(version, int, S_IRUGO);
static int debug =1;
module_param(debug, int, S_IRUGO|S_IWUSR);

#define dprintk(level, fmt, arg...) do {			\
	if (debug >= level) 					\
	printk(KERN_WARNING fmt , ## arg); } while (0)

/* Sensor Driver Configuration Begin */
#define SENSOR_NAME RK29_CAM_SENSOR_GC0328
#define SENSOR_V4L2_IDENT V4L2_IDENT_GC0328
#define SENSOR_ID 0x9d
#define SENSOR_BUS_PARAM					 (SOCAM_MASTER |\
											 SOCAM_PCLK_SAMPLE_RISING|SOCAM_HSYNC_ACTIVE_HIGH| SOCAM_VSYNC_ACTIVE_LOW|\
											 SOCAM_DATA_ACTIVE_HIGH | SOCAM_DATAWIDTH_8  |SOCAM_MCLK_24MHZ)
#define SENSOR_PREVIEW_W					 640
#define SENSOR_PREVIEW_H					 480
#define SENSOR_PREVIEW_FPS					 15000	   // 15fps 
#define SENSOR_FULLRES_L_FPS				 7500	   // 7.5fps
#define SENSOR_FULLRES_H_FPS				 7500	   // 7.5fps
#define SENSOR_720P_FPS 					 0
#define SENSOR_1080P_FPS					 0

#define SENSOR_REGISTER_LEN 				 1		   // sensor register address bytes
#define SENSOR_VALUE_LEN					 1		   // sensor register value bytes
static unsigned int SensorConfiguration = (CFG_WhiteBalance|CFG_Effect|CFG_Scene);									
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
	unsigned short int preview_gain;

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
*  sensor_init_data :				Sensor initial setting;
*  sensor_fullres_lowfps_data : 	Sensor full resolution setting with best auality, recommand for video;
*  sensor_preview_data :			Sensor preview resolution setting, recommand it is vga or svga;
*  sensor_softreset_data :			Sensor software reset register;
*  sensor_check_id_data :			Sensir chip id register;
*
*  Optional filled:
*  sensor_fullres_highfps_data: 	Sensor full resolution setting with high framerate, recommand for video;
*  sensor_720p: 					Sensor 720p setting, it is for video;
*  sensor_1080p:					Sensor 1080p setting, it is for video;
*
*  :::::WARNING:::::
*  The SensorEnd which is the setting end flag must be filled int the last of each setting;
*/
static unsigned int SensorChipID[] = {SENSOR_ID};
/* Sensor initial setting */
static struct rk_sensor_reg sensor_init_data[] ={
			{0xfe , 0x80},
			{0xfe , 0x80},
			{0xfc , 0x16}, //clock enable
			{0xfc , 0x16}, //clock enable
			{0xfc , 0x16}, //clock enable
			{0xfc , 0x16}, //clock enable
	
			{0x42 , 0x02},
	
			{0xfe , 0x01},
			{0x4c , 0x01}, //AWB Buffer reset
			{0xfe , 0x00},
	
			{0xfe , 0x00},
			{0x0d , 0x01}, //{8}cis_win_height	486 1e6
			{0x0e , 0xe8}, //{7:0}cis_win_height
			{0x0f , 0x02}, //{9:8}cis_win_width 646 286
			{0x10 , 0x88}, //{7:0}cis_win_width
			{0x09 , 0x00}, //row_start
			{0x0a , 0x00},
			{0x0b , 0x00}, //col_start
			{0x0c , 0x00},
			{0x16 , 0x80},
			{0x17 , 0x14}, //[7]hsync_always, [6]close_2_frame_dbrow, [5:4]CFA_seqence, [3:2]dark_CFA_seqence,	[1]updown, [0]mirror
			{0x19 , 0x06}, //AD Pipe line number
			{0x1f , 0xC8}, //txlow
			{0x20 , 0x01},
			{0x21 , 0x78}, //68  
			{0x22 , 0xb0},
			{0x24 , 0x16},
			{0x26 , 0x01},
	
			{0x50 , 0x01}, //crop mode
	
			//global gain for range
			{0x70 , 0x45},
	
			////////////////////////////////////////////////
			////////////	 BLK	  //////////////////////
			////////////////////////////////////////////////
			{0x27 , 0x27},
			{0x28 , 0x7F}, //BLK LIMIT
			{0x29 , 0x20}, //global offset
			{0x33 , 0x1A},//offset_ratio_G1
			{0x34 , 0x1A}, //offset_ratio_R
			{0x35 , 0x1A},
			{0x36 , 0x1A},
			{0x37 , 0x20},		
			{0x38 , 0x20},
			{0x39 , 0x20},
			{0x3a , 0x20},
			{0x47 , 0x00},
			{0x48 , 0x75},//00
	
	
	
			////////////////////////////////////////////////
			////////////	 block enable	   /////////////
			////////////////////////////////////////////////
			{0x40 , 0x7f}, //
			{0x41 , 0x22},//
							
	
			{0x42 , 0xff},//
			{0x45 , 0x00},
			{0x44 , 0x02},
			{0x46 , 0x02},
			{0x4b , 0x01},
			{0x50 , 0x01}, //crop mode
	
			//DN & EEINTP
			{0x7e , 0x0a}, //
			{0x7f , 0x03}, //c3 //1D DN
			{0x81 , 0x15},
			{0x82 , 0x85}, //8f//bilt_base_b
			{0x83 , 0x02}, //02 //DN C weight
			{0x84 , 0xe5},
			{0x90 , 0xac},
			{0x92 , 0x02}, //5
			{0x94 , 0x05},
			{0x95 , 0x63}, 
	
	
			///////YCP
			{0xd1 , 0x38},
			{0xd2 , 0x38}, // cb,cr saturation
			{0xdd , 0x54},
			{0xde , 0x38}, //autogray_mode
			{0xe4 , 0x88},
			{0xe5 , 0x40},
			{0xd7 , 0x0e},
			//rgb gamma
			{0xBF , 0x0E},			
			{0xc0 , 0x1C},	
			{0xc1 , 0x34},
			{0xc2 , 0x48},
			{0xc3 , 0x5A},
			{0xc4 , 0x6B},
			{0xc5 , 0x7B},
			{0xc6 , 0x95},
			{0xc7 , 0xAB},
			{0xc8 , 0xBF},
			{0xc9 , 0xCE},
			{0xcA , 0xD9},
			{0xcB , 0xE4},
			{0xcC , 0xEC},
			{0xcD , 0xF7},
			{0xcE , 0xFD},
			{0xcF , 0xFF},
	
			///Y gamma
			{0xfe , 0x00},
			{0x63 , 0x00},
			{0x64 , 0x05},
			{0x65 , 0x0b},
			{0x66 , 0x19},
			{0x67 , 0x2e},
			{0x68 , 0x40},
			{0x69 , 0x54},
			{0x6a , 0x66},
			{0x6b , 0x86},
			{0x6c , 0xa7},
			{0x6d , 0xc6},
			{0x6e , 0xe4},
			{0x6f , 0xff},
	
			//ASDE
			{0xfe , 0x01},
			{0x18 , 0x02},
			{0xfe , 0x00},
			{0x97 , 0x30}, //Y offset TH
			{0xa4 , 0x10}, //ASDE auto sa slope
			{0xa8 , 0x80}, //ASDE LSC SLOPE
			{0x9c , 0x60}, //auto Y offset Slope
			{0xa2 , 0x23},
			{0xad , 0x01}, //ASDE ee,ddMODE
			{0x9c , 0x01}, //ABS manual K
	
			{0xfe , 0x01},
			{0x9c , 0x00}, //ABS manual K
			{0x08 , 0xa0},
			{0x09 , 0xe8},
	
			{0x10 , 0x08},//[7]win_mode,[6]show_mode [3]measure_point
	
			{0x11 , 0x21}, //[7]fix target
			{0x12 , 0x10},		   
			{0x13 , 0x45}, //AEC Y target
			{0x15 , 0xfc}, //high range for count
			{0x21 , 0xf0}, //c0 //post_gain limit
			{0x22 , 0x60},
			{0x23 , 0x30},//20 //dggain max
			{0x25 , 0x00},
			{0x24 , 0x14}, //Max index
	
	
			{0xfe , 0x01},
			{0x51 , 0x20},
			{0x52 , 0x4f},
			{0x53 , 0x40},
			{0x54 , 0x9f},
			{0x55 , 0x01},
			{0x56 , 0x18}, //18
			{0x5b , 0x02},
			{0xb1 , 0xdc},
			{0xb2 , 0xdc}, //R2G STAND
			{0x7c , 0x71}, //AWB speed,AWB margin
			{0x7d , 0x10}, //AWB every N
			{0x76 , 0x8f}, //move mode en,Move mode TH
	
	
			{0x50 , 0x80},
			{0x4f , 0x00},
			{0x4d , 0x01},
			{0x4e , 0x02},
			{0x4d , 0x35},
			{0x4e , 0x01},
			{0x4e , 0x01},
			{0x4e , 0x01},
			{0x4d , 0x44},
			{0x4e , 0x04},
			{0x4e , 0x04},
			{0x4e , 0x02}, 
			{0x4d , 0x53},
			{0x4e , 0x08},
			{0x4e , 0x00},
			{0x4e , 0x04},
			{0x4d , 0x03},
			{0x4e , 0x08},
			{0x4d , 0x72},
			{0x4e , 0x20}, 
			{0x4e , 0x20},
			{0x4d , 0x83}, 
			{0x4e , 0x20},
			{0x4d , 0x92},
			{0x4e , 0x40},
			{0x4e , 0x40},
			{0x4f , 0x01}, 
			{0xfe , 0x00}, //page0
	
			{0xad , 0x00},
	
			{0xfe , 0x01},
			{0xc0 , 0x11},
			{0xc1 , 0x0b},
			{0xc2 , 0x09},
			{0xc6 , 0x0f},
			{0xc7 , 0x0a},
			{0xc8 , 0x07},
			{0xba , 0x2d},
			{0xbb , 0x1e},
			{0xbc , 0x1e},
			{0xb4 , 0x38},
			{0xb5 , 0x26},
			{0xb6 , 0x23},
			{0xc3 , 0x00},
			{0xc4 , 0x00},
			{0xc5 , 0x00},
			{0xc9 , 0x00},
			{0xca , 0x00},
			{0xcb , 0x00},
			{0xbd , 0x0a},
			{0xbe , 0x00},
			{0xbf , 0x00},
			{0xb7 , 0x04},
			{0xb8 , 0x00},
			{0xb9 , 0x00},
			{0xa8 , 0x08},
			{0xa9 , 0x00},
			{0xaa , 0x00},
			{0xab , 0x07},
			{0xac , 0x00},
			{0xad , 0x07},
			{0xae , 0x0e},
			{0xaf , 0x03},
			{0xb0 , 0x03},
			{0xb1 , 0x0d},
			{0xb2 , 0x00},
			{0xb3 , 0x08},
			{0xa4 , 0x00},
			{0xa5 , 0x00},
			{0xa6 , 0x00},
			{0xa7 , 0x00},
			{0xa1 , 0x3c},
			{0xa2 , 0x50},
			{0xfe , 0x00},
	
			{0xB1 , 0x08},// 40
			{0xB2 , 0x02},
			{0xB3 , 0x07},
			{0xB4 , 0xe0},
			{0xB5 , 0x00},//
			{0xB6 , 0xf0},
	
			{0xf1 , 0x07},
			{0xf1 , 0x07},
			{0xf2 , 0x01},
			
			{0x4f , 0x01},
	
	
	
			{0x05, 0x02},	
			{0x06, 0x2c}, 
			{0x07, 0x00},
			{0x08, 0xb8},
			{0xfe, 0x01},
			//GC0328_SET_PAGE1,
			{0x29, 0x00},	//anti-flicker step [11:8]
			{0x2a, 0x60},	//anti-flicker step [7:0]
			
			{0x2b, 0x02},	//exp level 0  14.28fps
			{0x2c, 0xa0}, 
			{0x2d, 0x03},	//exp level 1  12.50fps
			{0x2e, 0x00}, 
			{0x2f, 0x03},	//exp level 2  10.00fps
			{0x30, 0xc0}, 
			{0x31, 0x05},	//exp level 3  7.14fps
			{0x32, 0x40}, 
	
			{0xfe, 0x00},
	
	
	//-------------H_V_Switch(4)---------------//
			{0x17 , 0x14},	//
	
	
		 /*GC0328_H_V_Switch,
	
			1:	// normal
					{0x17 , 0x14},
					
			2:	// IMAGE_H_MIRROR
					{0x17 , 0x15},
					
			3:	// IMAGE_V_MIRROR
					{0x17 , 0x16},
					
			4:	// IMAGE_HV_MIRROR
					{0x17 , 0x17},
		*/			
		//-------------H_V_Select End--------------//
	SensorEnd
};
/* Senor full resolution setting: recommand for capture */
static struct rk_sensor_reg sensor_fullres_lowfps_data[] ={
	SensorEnd

};

/* Senor full resolution setting: recommand for video */
static struct rk_sensor_reg sensor_fullres_highfps_data[] ={
	SensorEnd
};
/* Preview resolution setting*/
static struct rk_sensor_reg sensor_preview_data[] =
{

	SensorEnd
};
/* 1280x720 */
static struct rk_sensor_reg sensor_720p[]={
	SensorEnd
};

/* 1920x1080 */
static struct rk_sensor_reg sensor_1080p[]={
	SensorEnd
};


static struct rk_sensor_reg sensor_softreset_data[]={
	SensorEnd
};

static struct rk_sensor_reg sensor_check_id_data[]={
	SensorRegVal(0xf0,0),
	SensorEnd
};
/*
*  The following setting must been filled, if the function is turn on by CONFIG_SENSOR_xxxx
*/
static struct rk_sensor_reg sensor_WhiteB_Auto[]=
{
	{0xfe, 0x00},
	{0x77, 0x57}, 
	{0x78, 0x4d},
	{0x79, 0x45},
	{0x42, 0x57},
	SensorEnd
};
/* Cloudy Colour Temperature : 6500K - 8000K  */
static	struct rk_sensor_reg sensor_WhiteB_Cloudy[]=
{
	{0xfe, 0x00},
	{0x42, 0x55},	// Disable AWB 
	{0x77, 0x8c},
	{0x78, 0x50},
	{0x79, 0x40},
	SensorEnd
};
/* ClearDay Colour Temperature : 5000K - 6500K	*/
static	struct rk_sensor_reg sensor_WhiteB_ClearDay[]=
{
	//Sunny
	{0xfe, 0x00},
	{0x42, 0x55},	// Disable AWB 
	{0x77, 0x74},
	{0x78, 0x52},
	{0x79, 0x40},
	SensorEnd
};
/* Office Colour Temperature : 3500K - 5000K  */
static	struct rk_sensor_reg sensor_WhiteB_TungstenLamp1[]=
{
	//Office
		{0xfe, 0x00},
	{0x42, 0x55},	 // Disable AWB 
	{0x77, 0x48},
	{0x78, 0x40},
	{0x79, 0x5c},
	SensorEnd

};
/* Home Colour Temperature : 2500K - 3500K	*/
static	struct rk_sensor_reg sensor_WhiteB_TungstenLamp2[]=
{
	//Home
		{0xfe, 0x00},
	{0x42, 0x55},	// Disable AWB 
	{0x77, 0x40},
	{0x78, 0x42}, 
	{0x79, 0x50},
	SensorEnd
};
static struct rk_sensor_reg *sensor_WhiteBalanceSeqe[] = {sensor_WhiteB_Auto, sensor_WhiteB_TungstenLamp1,sensor_WhiteB_TungstenLamp2,
	sensor_WhiteB_ClearDay, sensor_WhiteB_Cloudy,NULL,
};

static	struct rk_sensor_reg sensor_Brightness0[]=
{
	// Brightness -2
	{0xfe, 0x00},
	{0xd5, 0xe0},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness1[]=
{
	// Brightness -1
	{0xfe, 0x00},
	{0xd5, 0xf0},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness2[]=
{
	//	Brightness 0
	{0xfe, 0x00},
	{0xd5, 0x00},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness3[]=
{
	// Brightness +1
	{0xfe, 0x00},
	{0xd5, 0x20},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness4[]=
{
	//	Brightness +2
	{0xfe, 0x00},
	{0xd5, 0x30},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness5[]=
{
	//	Brightness +3
	{0xfe, 0x00},
	{0xd5, 0x40},
	SensorEnd
};
static struct rk_sensor_reg *sensor_BrightnessSeqe[] = {sensor_Brightness0, sensor_Brightness1, sensor_Brightness2, sensor_Brightness3,
	sensor_Brightness4, sensor_Brightness5,NULL,
};

static	struct rk_sensor_reg sensor_Effect_Normal[] =
{
	{0x43,0x00}, 
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_WandB[] =
{
	{0x43,0x02}, 
	{0xda,0x00}, 
	{0xdb,0x00}, 
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_Sepia[] =
{
	{0x43,0x02},
	{0xda,0xd0},
	{0xdb,0x28},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_Negative[] =
{
	//Negative
	{0x43,0x01},
	SensorEnd
};
static	struct rk_sensor_reg sensor_Effect_Bluish[] =
{
	// Bluish
	{0x423,0x02},
	{0xda,0x50},
	{0xdb,0xe0},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_Green[] =
{
	//	Greenish
	{0x43,0x02},
	
	{0xda,0xc0},
	{0xdb,0xc0},
	SensorEnd
};
static struct rk_sensor_reg *sensor_EffectSeqe[] = {sensor_Effect_Normal, sensor_Effect_WandB, sensor_Effect_Negative,sensor_Effect_Sepia,
	sensor_Effect_Bluish, sensor_Effect_Green,NULL,
};

static	struct rk_sensor_reg sensor_Exposure0[]=
{
	{0xfe, 0x01},
	{0x13, 0x30},
	{0xfe, 0x00},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure1[]=
{
	{0xfe, 0x01},
	{0x13, 0x38},
	{0xfe, 0x00},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure2[]=
{
	{0xfe, 0x01},
	{0x13, 0x40},
	{0xfe, 0x00},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure3[]=
{
	{0xfe, 0x01},
	{0x13, 0x45},
	{0xfe, 0x00},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure4[]=
{
	{0xfe, 0x01},
	{0x13, 0x50},
	{0xfe, 0x00},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure5[]=
{
	{0xfe, 0x01},
	{0x13, 0x58},
	{0xfe, 0x00},

	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure6[]=
{
	{0xfe, 0x01},
	{0x13, 0x60},
	{0xfe, 0x00},
	SensorEnd
};

static struct rk_sensor_reg *sensor_ExposureSeqe[] = {sensor_Exposure0, sensor_Exposure1, sensor_Exposure2, sensor_Exposure3,
	sensor_Exposure4, sensor_Exposure5,sensor_Exposure6,NULL,
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

static	struct rk_sensor_reg sensor_Contrast0[]=
{
	{0xb3,0x34},

	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast1[]=
{
	{0xb3,0x38},

	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast2[]=
{
	{0xb3,0x3d},

	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast3[]=
{
	{0xb3,0x40},

	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast4[]=
{
	{0xb3,0x44},

	SensorEnd
};


static	struct rk_sensor_reg sensor_Contrast5[]=
{
	{0xb3,0x48},

	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast6[]=
{
	{0xb3,0x50},

	SensorEnd
};
static struct rk_sensor_reg *sensor_ContrastSeqe[] = {sensor_Contrast0, sensor_Contrast1, sensor_Contrast2, sensor_Contrast3,
	sensor_Contrast4, sensor_Contrast5, sensor_Contrast6, NULL,
};
static	struct rk_sensor_reg sensor_SceneAuto[] =
{
	{0xfe, 0x01},
	{0x33, 0x20},
	{0xfe, 0x00},
	SensorEnd
};

static	struct rk_sensor_reg sensor_SceneNight[] =
{
	{0xfe, 0x01},
	{0x33, 0x30},
	{0xfe, 0x00},
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
};
/*
* User could be add v4l2_queryctrl in sensor_controls by new_user_v4l2ctrl
*/
static struct sensor_v4l2ctrl_usr_s sensor_controls[] =
{
};

//MUST define the current used format as the first item   
static struct rk_sensor_datafmt sensor_colour_fmts[] = {
	{V4L2_MBUS_FMT_YUYV8_2X8, V4L2_COLORSPACE_JPEG} 
};
static struct soc_camera_ops sensor_ops;


/*
**********************************************************
* Following is local code:
* 
* Please codeing your program here 
**********************************************************
*/
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
	
	return 0;
}
/*
* the function is called in close sensor
*/
static int sensor_deactivate_cb(struct i2c_client *client)
{
	
	return 0;
}
/*
* the function is called before sensor register setting in VIDIOC_S_FMT  
*/
static int sensor_s_fmt_cb_th(struct i2c_client *client,struct v4l2_mbus_framefmt *mf, bool capture)
{

	return 0;
}
/*
* the function is called after sensor register setting finished in VIDIOC_S_FMT  
*/
static int sensor_s_fmt_cb_bh (struct i2c_client *client,struct v4l2_mbus_framefmt *mf, bool capture)
{
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
		sensor_write(client, 0xfe, 0);
		err = sensor_read(client, 0x17, &val);
		val-=4;
		if (err == 0) {
			if((val & 0x1) == 0){
				err = sensor_write(client, 0x17, ((val |0x1)+4));
				}
			else 
				err = sensor_write(client, 0x17, ((val & 0xfe)+4));
		}
	} else {
		//do nothing
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
		
		sensor_write(client, 0xfe, 0);
		err = sensor_read(client, 0x17, &val);
		val-=4;
		if (err == 0) {
			if((val & 0x2) == 0){
				err = sensor_write(client, 0x17, ((val |0x2)+4));
				}
			else {
				err = sensor_write(client, 0x17, ((val & 0xfc)+4));
				}
		}
	} else {
		//do nothing
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
/*
* the functions are focus callbacks
*/
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

static int sensor_focus_af_specialpos_usr_cb(struct i2c_client *client,int pos)
{
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
static int	sensor_face_detect_usr_cb(struct i2c_client *client,int on){
	return 0;
}

/*
*	The function can been run in sensor_init_parametres which run in sensor_probe, so user can do some
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




