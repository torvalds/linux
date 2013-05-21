
#include "generic_sensor.h"
/*
*      Driver Version Note
*v0.0.1: this driver is compatible with generic_sensor
*v0.0.3:
*        add sensor_focus_af_const_pause_usr_cb;
*/
static int version = KERNEL_VERSION(0,0,3);
module_param(version, int, S_IRUGO);



static int debug;
module_param(debug, int, S_IRUGO|S_IWUSR);

#define dprintk(level, fmt, arg...) do {			\
	if (debug >= level) 					\
	printk(KERN_WARNING fmt , ## arg); } while (0)

/* Sensor Driver Configuration Begin */
#define SENSOR_NAME RK29_CAM_SENSOR_GC2015
#define SENSOR_V4L2_IDENT V4L2_IDENT_GC2015
#define SENSOR_ID 0x2005
#define SENSOR_BUS_PARAM					 (SOCAM_MASTER |\
											 SOCAM_PCLK_SAMPLE_RISING|SOCAM_HSYNC_ACTIVE_HIGH| SOCAM_VSYNC_ACTIVE_LOW|\
											 SOCAM_DATA_ACTIVE_HIGH | SOCAM_DATAWIDTH_8  |SOCAM_MCLK_24MHZ)
#define SENSOR_PREVIEW_W					 800
#define SENSOR_PREVIEW_H					 600
#define SENSOR_PREVIEW_FPS					 15000	   // 15fps 
#define SENSOR_FULLRES_L_FPS				 7500	   // 7.5fps
#define SENSOR_FULLRES_H_FPS				 7500	   // 7.5fps
#define SENSOR_720P_FPS 					 0
#define SENSOR_1080P_FPS					 0

#define SENSOR_REGISTER_LEN 				 1		   // sensor register address bytes
#define SENSOR_VALUE_LEN					 1		   // sensor register value bytes
static unsigned int SensorConfiguration = (CFG_WhiteBalance|CFG_Effect|CFG_Scene);
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
	unsigned short int preview_gain;

	unsigned short int PreviewPclk;
	unsigned short int CapturePclk;
	char awb[6];
};

struct specific_sensor{
	struct generic_sensor common_sensor;
	//define user data below
	struct sensor_parameter parameter;
	u16 shutter;

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

/* Sensor initial setting */
static struct rk_sensor_reg sensor_init_data[] ={
		{0xfe, 0x80}, //soft reset
		{0xfe, 0x80}, //soft reset
		{0xfe, 0x80}, //soft reset
	
		{0xfe, 0x00}, //page0
		{0x45, 0x00}, //output_disable
	
		//////////////////////////////////////////////////////////////////////////////////////
		//////////////////////////preview capture switch /////////////////////////////////////
		//////////////////////////////////////////////////////////////////////////////////////
		//preview
		{0x02, 0x01}, //preview mode
		{0x2a, 0xca}, //[7]col_binning, 0x[6]even skip
		{0x48, 0x40}, //manual_gain
	
		////////////////////////////////////////////////////////////////////////
		////////////////////////// preview LSC /////////////////////////////////
		////////////////////////////////////////////////////////////////////////
		{0xfe, 0x01}, //page1
		{0xb0, 0x03}, //[4]Y_LSC_en [3]lsc_compensate [2]signed_b4 [1:0]pixel array select
		{0xb1, 0x46}, //P_LSC_red_b2
		{0xb2, 0x40}, //P_LSC_green_b2
		{0xb3, 0x40}, //P_LSC_blue_b2
		{0xb4, 0x24}, //P_LSC_red_b4
		{0xb5, 0x20}, //P_LSC_green_b4
		{0xb6, 0x22}, //P_LSC_blue_b4
		{0xb7, 0x00}, //P_LSC_compensate_b2
		{0xb8, 0x80}, //P_LSC_row_center, 0x344, 0x (1200/2-344)/2=128, 0x, 0x
		{0xb9, 0x80}, //P_LSC_col_center, 0x544, 0x (1600/2-544)/2=128
	
	
		////////////////////////////////////////////////////////////////////////
		////////////////////////// capture LSC /////////////////////////////////
		////////////////////////////////////////////////////////////////////////
		{0xba, 0x03}, //[4]Y_LSC_en [3]lsc_compensate [2]signed_b4 [1:0]pixel array select
		{0xbb, 0x46}, //C_LSC_red_b2
		{0xbc, 0x40}, //C_LSC_green_b2
		{0xbd, 0x40}, //C_LSC_blue_b2
		{0xbe, 0x24}, //C_LSC_red_b4
		{0xbf, 0x20}, //C_LSC_green_b4
		{0xc0, 0x22}, //C_LSC_blue_b4
		{0xc1, 0x00}, //C_Lsc_compensate_b2
		{0xc2, 0x80}, //C_LSC_row_center, 0x344, 0x (1200/2-344)/2=128
		{0xc3, 0x80}, //C_LSC_col_center, 0x544, 0x (1600/2-544)/2=128
		{0xfe, 0x00}, //page0
	
		////////////////////////////////////////////////////////////////////////
		////////////////////////// analog configure ///////////////////////////
		////////////////////////////////////////////////////////////////////////
		{0xfe, 0x00}, //page0
		{0x29, 0x00}, //cisctl mode 1
		{0x2b, 0x06}, //cisctl mode 3	
		{0x32, 0x1c}, //analog mode 1
		{0x33, 0x0f}, //analog mode 2
		{0x34, 0x30}, //[6:4]da_rsg
	
		{0x35, 0x88}, //Vref_A25
		{0x37, 0x16}, //Drive Current
	
		/////////////////////////////////////////////////////////////////////
		/////////////////////////// ISP Related /////////////////////////////
		/////////////////////////////////////////////////////////////////////
		{0x40, 0xff}, 
		{0x41, 0x20}, //[5]skin_detectionenable[2]auto_gray, 0x[1]y_gamma
		{0x42, 0xf6}, //[7]auto_sa[6]auto_ee[5]auto_dndd[4]auto_lsc[3]na[2]abs, 0x[1]awb
		{0x4b, 0xe8}, //[1]AWB_gain_mode, 0x1:atpregain0:atpostgain
		{0x4d, 0x03}, //[1]inbf_en
		{0x4f, 0x01}, //AEC enable
	
		////////////////////////////////////////////////////////////////////
		///////////////////////////  BLK  //////////////////////////////////
		////////////////////////////////////////////////////////////////////
		{0x63, 0x77}, //BLK mode 1
		{0x66, 0x00}, //BLK global offset
		{0x6d, 0x00},
		{0x6e, 0x1a}, //BLK offset submode,offset R
		{0x6f, 0x20},
		{0x70, 0x1a},
		{0x71, 0x20},
		{0x73, 0x00},
		{0x77, 0x80},
		{0x78, 0x80},
		{0x79, 0x90},
	
		////////////////////////////////////////////////////////////////////
		/////////////////////////// DNDD ///////////////////////////////////
		////////////////////////////////////////////////////////////////////
		{0x80, 0x07}, //[7]dn_inc_or_dec [4]zero_weight_mode[3]share [2]c_weight_adap [1]dn_lsc_mode [0]dn_b
		{0x82, 0x0c}, //DN lilat b base
		{0x83, 0x03},
	
		////////////////////////////////////////////////////////////////////
		/////////////////////////// EEINTP ////////////////////////////////
		////////////////////////////////////////////////////////////////////
		{0x8a, 0x7c},
		{0x8c, 0x02},
		{0x8e, 0x02},
		{0x8f, 0x45},
	
	
		/////////////////////////////////////////////////////////////////////
		/////////////////////////// CC_t ////////////////////////////////////
		/////////////////////////////////////////////////////////////////////
		{0xb0, 0x40},	// 0x48
		{0xb1, 0xfe},
		{0xb2, 0x00},
		{0xb3, 0xf0},
		{0xb4, 0x50},
		{0xb5, 0xf8},
		{0xb6, 0x00},
		{0xb7, 0x00},
		{0xb8, 0x00},
	
	
		/////////////////////////////////////////////////////////////////////
		/////////////////////////// GAMMA ///////////////////////////////////
		/////////////////////////////////////////////////////////////////////
		//RGB_GAMMA
		{0xbf, 0x08}, 
		{0xc0, 0x1e},
		{0xc1, 0x33},
		{0xc2, 0x47},
		{0xc3, 0x59},
		{0xc4, 0x68},
		{0xc5, 0x74},
		{0xc6, 0x86},
		{0xc7, 0x97},
		{0xc8, 0xA5},
		{0xc9, 0xB1},
		{0xca, 0xBd},
		{0xcb, 0xC8},
		{0xcc, 0xD3},
		{0xcd, 0xE4},
		{0xce, 0xF4},
		{0xcf, 0xff},
		
		/*{0xbf, 0x06},
		{0xc0, 0x1f},
		{0xc1, 0x38},
		{0xc2, 0x4c},
		{0xc3, 0x5b},
		{0xc4, 0x6b},
		{0xc5, 0x76},
		{0xc6, 0x8b},
		{0xc7, 0x9b},
		{0xc8, 0xac},
		{0xc9, 0xbb},
		{0xca, 0xc7},
		{0xcb, 0xd2},
		{0xcc, 0xdb},
		{0xcd, 0xea},
		{0xce, 0xf5},
		{0xcf, 0xff},	*/
	
		/////////////////////////////////////////////////////////////////////
		/////////////////////////// YCP_t ///////////////////////////////////
		/////////////////////////////////////////////////////////////////////
		{0xd1, 0x40}, //saturation	 38
		{0xd2, 0x40}, //saturation	 38
		
		{0xd3, 0x46},  // 2011-08-11 kim add
		
		{0xde, 0x21}, //auto_gray
	
		////////////////////////////////////////////////////////////////////
		/////////////////////////// ASDE ///////////////////////////////////
		////////////////////////////////////////////////////////////////////
		{0x98, 0x3a}, 
		{0x99, 0x60}, 
		{0x9b, 0x00}, 
		{0x9f, 0x12}, 
		{0xa1, 0x80}, 
		{0xa2, 0x21}, 
		
		{0xfe, 0x01}, //page1
		{0xc5, 0x10}, 
		{0xc6, 0xff}, 
		{0xc7, 0xff}, 
		{0xc8, 0xff}, 
	
		////////////////////////////////////////////////////////////////////
		/////////////////////////// AEC ////////////////////////////////////
		////////////////////////////////////////////////////////////////////
		{0x10, 0x09}, //AEC mode 1
		{0x11, 0x92}, //[7]fix target  // 0xb2	2011-08-11 kim 
		{0x12, 0x20}, 
		{0x13, 0x78},	// 0x78  2011-08-11 kim 
		{0x17, 0x00}, 
		{0x1c, 0x96}, 
		{0x1d, 0x04}, // sunlight step 
		{0x1e, 0x11}, 
		{0x21, 0xc0}, //max_post_gain
		{0x22, 0x40}, //max_pre_gain   // 0x60	2011-08-11 kim 
		{0x2d, 0x06}, //P_N_AEC_exp_level_1[12:8]
		{0x2e, 0x00}, //P_N_AEC_exp_level_1[7:0]
		{0x1e, 0x32}, 
		{0x33, 0x00}, //[6:5]max_exp_level [4:0]min_exp_level
		{0x34, 0x04}, // min exp
	
		////////////////////////////////////////////////////////////////////
		/////////////////////////// Measure Window /////////////////////////
		////////////////////////////////////////////////////////////////////
		{0x06, 0x07},
		{0x07, 0x03},
		{0x08, 0x64},
		{0x09, 0x4a},
	
		////////////////////////////////////////////////////////////////////
		/////////////////////////// AWB ////////////////////////////////////
		////////////////////////////////////////////////////////////////////
		{0x57, 0x40}, //number limit
		{0x5d, 0x44}, //
		{0x5c, 0x35}, //show mode,close dark_mode
		{0x5e, 0x29}, //close color temp
		{0x5f, 0x50},
		{0x60, 0x50}, 
		{0x65, 0xc0},
		////////////////////////////////////////////////////////////////////
		/////////////////////////// ABS ////////////////////////////////////
		////////////////////////////////////////////////////////////////////
		{0x80, 0x82},
		{0x81, 0x00},
		
		{0x82, 0x03},  /// 
		
		{0x83, 0x10}, //ABS Y stretch limit
		{0xfe, 0x00},
		////////////////////////////////////////////////////////////////////
		/////////////////////////// OUT ////////////////////////////////////
		////////////////////////////////////////////////////////////////////
		{0xfe, 0x00},
		//crop 
		{0x50, 0x01},
		{0x51, 0x00},
		{0x52, 0x00},
		{0x53, 0x00},
		{0x54, 0x00},
		{0x55, 0x02},
		{0x56, 0x58},
		{0x57, 0x03},
		{0x58, 0x20},
	
		{0x44, 0xa0}, //YUV sequence
		{0x45, 0x0f}, //output enable
		{0x46, 0x02}, //sync mode
		
	/*	{0xbF, 0x0B}, 
		{0xc0, 0x16}, 
		{0xc1, 0x29}, 
		{0xc2, 0x3C}, 
		{0xc3, 0x4F}, 
		{0xc4, 0x5F}, 
		{0xc5, 0x6F}, 
		{0xc6, 0x8A}, 
		{0xc7, 0x9F}, 
		{0xc8, 0xB4}, 
		{0xc9, 0xC6}, 
		{0xcA, 0xD3}, 
		{0xcB, 0xDD},  
		{0xcC, 0xE5},  
		{0xcD, 0xF1}, 
		{0xcE, 0xFA}, 
		{0xcF, 0xFF},*/
		
		{0x05, 0x01},//HB
		{0x06, 0xc1},
		{0x07, 0x00},//VB
		{0x08, 0x40},
		
		{0xfe, 0x01},
		{0x29, 0x00},//Anti Step 128
		{0x2a, 0x80},
		
		{0x2b, 0x05},//Level_0	10.00fps
		{0x2c, 0x00},
		{0x2d, 0x06},//Level_1	 8.33fps
		{0x2e, 0x00},
		{0x2f, 0x08},//Level_2	 6.25fps
		{0x30, 0x00},
		{0x31, 0x09},//Level_3	 5.55fps
		{0x32, 0x00},
		{0x33, 0x20},
		{0xfe, 0x00},
		
	//--------------------Updated By Mormo 2011/08/08 Start --------------------//
		{0xfe, 0x00},
		{0x32, 0x34},
		{0x34, 0x00},
	//--------------------Updated By Mormo 2011/08/08 End ---------------------//	
		{0x7d, 0x80}, //
		{0x7e, 0x80},
		{0x7f, 0x84},
	SensorEnd
};
/* Senor full resolution setting: recommand for capture */
static struct rk_sensor_reg sensor_fullres_lowfps_data[] ={
	{0xfe, 0x00},
	
	{0x48, 0x80},  // 68
	
	{0x4f, 0x00},	// aec off
	
	{0x02, 0x00},
	{0x2a, 0x0a},
	
	//subsample 1/1
	{0x59,	0x11},
	{0x5a,	0x06},
	{0x5b,	0x00},
	{0x5c,	0x00},
	{0x5d,	0x00},
	{0x5e , 0x00},
	{0x5f,	0x00},
	{0x60,	0x00},
	{0x61,	0x00},
	{0x62,	0x00},
	
	//crop 
	{0x50,	0x01},
	{0x51,	0x00},
	{0x52,	0x00},
	{0x53,	0x00},
	{0x54,	0x00},
	{0x55,	0x04},
	{0x56,	0xb0},
	{0x57,	0x06},
	{0x58,	0x40},
	SensorEnd

};

/* Senor full resolution setting: recommand for video */
static struct rk_sensor_reg sensor_fullres_highfps_data[] ={
	SensorEnd
};
/* Preview resolution setting*/
static struct rk_sensor_reg sensor_preview_data[] =
{
	{0xfe, 0x00},
	
	{0x48, 0x40},
	{0x4f, 0x01},	// aec on
	
	{0x02, 0x01},
	{0x2a, 0xca},
	
	//subsample 1/1
	{0x59,	0x11},
	{0x5a,	0x06},
	{0x5b,	0x00},
	{0x5c,	0x00},
	{0x5d,	0x00},
	{0x5e , 0x00},
	{0x5f,	0x00},
	{0x60,	0x00},
	{0x61,	0x00},
	{0x62,	0x00},
	
	{0x50 , 0x01},//out window
	{0x51 , 0x00},
	{0x52 , 0x00},
	{0x53 , 0x00},
	{0x54 , 0x00},
	{0x55 , 0x02},
	{0x56 , 0x58},// 600
	{0x57 , 0x03},
	{0x58 , 0x20},//800
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
	SensorRegVal(0xfe,80),
	SensorWaitMs(5),
	SensorEnd
};

static struct rk_sensor_reg sensor_check_id_data[]={
	SensorRegVal(0x00,0),
	SensorRegVal(0x01,0),
	SensorEnd
};
/*
*  The following setting must been filled, if the function is turn on by CONFIG_SENSOR_xxxx
*/
static struct rk_sensor_reg sensor_WhiteB_Auto[]=
{
	{0x42,0x76},
	SensorEnd
};
/* Cloudy Colour Temperature : 6500K - 8000K  */
static	struct rk_sensor_reg sensor_WhiteB_Cloudy[]=
{
	{0x42 , 0x74},// [1] AWB enable  功能开关AWB OFF  
	{0x7a , 0x8c},	//AWB_R_gain
	{0x7b , 0x50},	//AWB_G_gain
	{0x7c , 0x40}, //AWB_B_gain
	SensorEnd
};
/* ClearDay Colour Temperature : 5000K - 6500K	*/
static	struct rk_sensor_reg sensor_WhiteB_ClearDay[]=
{
	//Sunny 
	{0x42 , 0x74},// [1] AWB enable  功能开关AWB OFF  
	{0x7a , 0x74},	//AWB_R_gain
	{0x7b , 0x52},	//AWB_G_gain
	{0x7c , 0x40}, //AWB_B_gain
	SensorEnd
};
/* Office Colour Temperature : 3500K - 5000K  */
static	struct rk_sensor_reg sensor_WhiteB_TungstenLamp1[]=
{
	//Office
	{0x42 , 0x74},// [1] AWB enable  功能开关AWB OFF  
	{0x7a , 0x48},	//AWB_R_gain
	{0x7b , 0x40},	//AWB_G_gain
	{0x7c , 0x5c}, //AWB_B_gain  
	SensorEnd

};
/* Home Colour Temperature : 2500K - 3500K	*/
static	struct rk_sensor_reg sensor_WhiteB_TungstenLamp2[]=
{
	//Home
	{0x42 , 0x74},// [1] AWB enable  功能开关AWB OFF  
	{0x7a , 0x40},	//AWB_R_gain
	{0x7b , 0x54},	//AWB_G_gain
	{0x7c , 0x70}, //AWB_B_gain
	SensorEnd
};
static struct rk_sensor_reg *sensor_WhiteBalanceSeqe[] = {sensor_WhiteB_Auto, sensor_WhiteB_TungstenLamp1,sensor_WhiteB_TungstenLamp2,
	sensor_WhiteB_ClearDay, sensor_WhiteB_Cloudy,NULL,
};

static	struct rk_sensor_reg sensor_Brightness0[]=
{
	// Brightness -2
	
	{0xfe, 0x01},
	{0x13, 0x68}, //AEC_target_Y  
	{0xfe, 0x00},
	{0xd5, 0xe0},// Luma_offset  
	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness1[]=
{
	// Brightness -1
	   {0xfe, 0x01},
	{0x13, 0x70}, //AEC_target_Y  
	{0xfe, 0x00},
	{0xd5, 0xf0},// Luma_offset 
	
	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness2[]=
{
	//	Brightness 0
					
	{0xfe, 0x01},
	{0x13, 0x78}, //AEC_target_Y  48
	{0xfe, 0x00},
	{0xd5, 0x00},// Luma_offset  c0
	
	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness3[]=
{
	// Brightness +1
		{0xfe, 0x01},
	{0x13, 0x80}, //AEC_target_Y  
	{0xfe, 0x00},
	{0xd5, 0x10},// Luma_offset  
	
	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness4[]=
{
	//	Brightness +2
		{0xfe, 0x01},
	{0x13, 0x88}, //AEC_target_Y  
	{0xfe, 0x00},
	{0xd5, 0x20},// Luma_offset 
	

	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness5[]=
{
	//	Brightness +3
	  {0xfe, 0x01},
	{0x13, 0x90}, //AEC_target_Y  
	{0xfe, 0x00},
	{0xd5, 0x30},// Luma_offset 

	SensorEnd
};
static struct rk_sensor_reg *sensor_BrightnessSeqe[] = {sensor_Brightness0, sensor_Brightness1, sensor_Brightness2, sensor_Brightness3,
	sensor_Brightness4, sensor_Brightness5,NULL,
};

static	struct rk_sensor_reg sensor_Effect_Normal[] =
{
	{0x43, 0x00},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_WandB[] =
{
	{0x43, 0x02},
	{0xda, 0x50},
	{0xdb, 0xe0},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_Sepia[] =
{
	{0x43, 0x02},
	{0xda, 0xd0},
	{0xdb, 0x28},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_Negative[] =
{
	//Negative
	{0x43, 0x01},
	//{0xda, 0xc0},
	//{0xdb, 0xc0},
	SensorEnd
};
static	struct rk_sensor_reg sensor_Effect_Bluish[] =
{
	// Bluish
		{0x43, 0x02},
	{0xda, 0x00},
	{0xdb, 0x00},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_Green[] =
{
	//	Greenish
	{0x43, 0x02},
	{0xda, 0xc0},
	{0xdb, 0xc0},
	SensorEnd
};
static struct rk_sensor_reg *sensor_EffectSeqe[] = {sensor_Effect_Normal, sensor_Effect_WandB, sensor_Effect_Negative,sensor_Effect_Sepia,
	sensor_Effect_Bluish, sensor_Effect_Green,NULL,
};

static	struct rk_sensor_reg sensor_Exposure0[]=
{

	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure1[]=
{
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure2[]=
{
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure3[]=
{
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure4[]=
{
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure5[]=
{

	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure6[]=
{
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
	//Contrast -3
	{0xfe, 0x00},	 
	{0xd3, 0x2c}, 

	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast1[]=
{
	//Contrast -2
	{0xfe, 0x00},	 
	{0xd3, 0x30},

	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast2[]=
{
	// Contrast -1
	{0xfe, 0x00},		 
	{0xd3, 0x38},

	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast3[]=
{
	//Contrast 0
	{0xfe, 0x00},	 
	{0xd3, 0x40},

	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast4[]=
{
	//Contrast +1
	{0xfe, 0x00},		 
	{0xd3, 0x48},

	SensorEnd
};


static	struct rk_sensor_reg sensor_Contrast5[]=
{
	//Contrast +2
	{0xfe, 0x00},	 
	{0xd3, 0x50},

	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast6[]=
{
	//Contrast +3
	{0xfe, 0x00},	 
	{0xd3, 0x58},

	SensorEnd
};
static struct rk_sensor_reg *sensor_ContrastSeqe[] = {sensor_Contrast0, sensor_Contrast1, sensor_Contrast2, sensor_Contrast3,
	sensor_Contrast4, sensor_Contrast5, sensor_Contrast6, NULL,
};
static	struct rk_sensor_reg sensor_SceneAuto[] =
{
	{0xfe, 0x01},
	{0x33, 0x00},
	{0xfe, 0x00},
	SensorEnd
};

static	struct rk_sensor_reg sensor_SceneNight[] =
{
	//30fps ~ 5fps night mode for 60/50Hz light environment, 24Mhz clock input,36Mzh pclk
	{0xfe, 0x01},
	{0x33, 0x20},
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
static u16 GC2015_read_shutter(struct i2c_client *client);	// add 2011-08-11 kim
static void GC2015_set_shutter(struct i2c_client *client, u16 shutter);  // add 2011-08-11 kim


////// add 2011-08-11 kim
static u16 GC2015_read_shutter(struct i2c_client *client)
{
	u8 temp_reg1, temp_reg2;
	u16 shutter;
	
	/* Backup the preview mode last shutter & sensor gain. */
	sensor_read(client, 0x03, &temp_reg1);
	sensor_read(client, 0x04, &temp_reg2);
	
	shutter = (temp_reg1 << 8) | (temp_reg2 & 0xFF);
		
	return shutter;
}	 /* GC2015_read_shutter */

static void GC2015_set_shutter(struct i2c_client *client, u16 shutter)
{
	u16 temp_reg;

	temp_reg = shutter * 10 / 20;	//// 

	/*Set Shutter start*/
	if(temp_reg < 1) temp_reg = 1;
	sensor_write(client ,0x03 , (temp_reg>>8)&0xff);		   
	sensor_write(client ,0x04 , temp_reg&0xff); 
	/*Set Shutter end*/
}	
//////// end add kim 
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
	struct generic_sensor *sensor = to_generic_sensor(client);
	struct specific_sensor *spsensor = to_specific_sensor(sensor);
	spsensor->shutter= GC2015_read_shutter(client); // add 2011-08-11 kim

	return 0;
}
/*
* the function is called after sensor register setting finished in VIDIOC_S_FMT  
*/
static int sensor_s_fmt_cb_bh (struct i2c_client *client,struct v4l2_mbus_framefmt *mf, bool capture)
{
	struct generic_sensor *sensor = to_generic_sensor(client);
	struct specific_sensor *spsensor = to_specific_sensor(sensor);
	if(mf->width >=1024)
		GC2015_set_shutter(client, spsensor->shutter); // add 2011-08-11 kim
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
		err = sensor_read(client, 0x29, &val);
		if (err == 0) {
			if((val & 0x1) == 0)
				err = sensor_write(client, 0x29, (val |0x1));
			else 
				err = sensor_write(client, 0x29, (val & 0xfe));
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
		err = sensor_read(client, 0x29, &val);
		if (err == 0) {
			if((val & 0x2) == 0)
				err = sensor_write(client, 0x29, (val |0x2));
			else 
				err = sensor_write(client, 0x29, (val & 0xfc));
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

static int sensor_focus_af_specialpos_usr_cb(struct i2c_client *client,int pos) {
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




