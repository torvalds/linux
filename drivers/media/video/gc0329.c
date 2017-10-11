/*
 * drivers/media/video/gc0329.c
 *
 * Copyright (C) ROCKCHIP, Inc.
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

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
#define SENSOR_NAME RK29_CAM_SENSOR_GC0329
#define SENSOR_V4L2_IDENT V4L2_IDENT_GC0329
#define SENSOR_ID 0xc0
#define SENSOR_BUS_PARAM                     (V4L2_MBUS_MASTER |\
                                             V4L2_MBUS_PCLK_SAMPLE_RISING|V4L2_MBUS_HSYNC_ACTIVE_HIGH| V4L2_MBUS_VSYNC_ACTIVE_HIGH|\
                                             V4L2_MBUS_DATA_ACTIVE_HIGH  |SOCAM_MCLK_24MHZ)
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
	// TODO: add initial code here
	  {0xfe, 0x80},
	  {0xfc, 0x12}, 
	  {0xfc, 0x12}, //[4]Clock_en [2] A25_en [1]D18_en [0]Apwdn
	  {0xfe, 0x00},
	  {0xf0, 0x07}, //vsync_en
	  {0xf1, 0x01}, //data_en
	
	  {0x73, 0x90}, //R channle gain
	  {0x74, 0x80}, //G1 channle gain	
	  {0x75, 0x80}, //G2 channle gain  
	  {0x76, 0x94}, //B channle gain 
	
		//{0x42, 0x00},
		//{0x77, 0x57},
		//{0x78, 0x4d},
		//{0x79, 0x45},
		//{0x42, 0xfc},
	
	////////////////////analog////////////////////
	  {0xfc, 0x16}, 
	
	  {0x0a, 0x00}, //row_start_low   
	  {0x0c, 0x00}, //col_start_low    
	  {0x17, 0x14}, //cisctl_mode1//[7]hsync_always },[6] NA},	[5:4] CFA sequence [3:2]NA,  [1]upside_down,  [0] mirror 
		//[3:2]NA  [1]upside_down},  [0] mirror 
	
	  {0x19, 0x05}, //cisctl_mode3
	  {0x1b, 0x24}, //rsh_width
	  {0x1c, 0x04}, //Tsp_width
	  {0x1e, 0x00},  //Analog_mode1//[7:6]rsv1},rsv0[5:3] Column bias(coln_r)[1] clk_delay_en
	  {0x1f, 0xc0}, //Analog_mode2//[7:6] comv_r
	  {0x20, 0x00}, //Analog_mode3//[6:4] cap_low_r for MPW [3:2] da18_r [1] rowclk_mode [0]adclk_mode
	  {0x21, 0x48}, //Hrst_rsg//[7] hrst[6:4] da_rsg[3]txhigh_en
	  {0x23, 0x22}, //ADC_r//[6:5]opa_r [1:0]sRef
	  {0x24, 0x16}, //PAD_drv//[7:6]NA},[5:4]sync_drv [3:2]data_drv [1:0]pclk_drv
	
	
	////////////////////blk////////////////////
	  {0x26, 0xf7}, 
	  {0x32, 0x04},
	  {0x33, 0x20},
	  {0x34, 0x20},
	  {0x35, 0x20},
	  {0x36, 0x20},
	
	////////////////////ISP BLOCK ENABLE////////////////////
	  {0x40, 0xff}, 
	  {0x41, 0x00},
	  {0x42, 0xfe}, 
	  {0x46, 0x03}, //sync mode
	  {0x4b, 0xcb},
	  {0x4d, 0x01}, 
	  {0x4f, 0x01},
	  {0x70, 0x48},//global gain
	
		//{0xb0, 0x00},
		//{0xbc, 0x00},
		//{0xbd, 0x00},
		//{0xbe, 0x00},
	
	////////////////////DNDD////////////////////
	  {0x80, 0xe7}, //[7]auto_en [6]one_pixel [5]two_pixel
	  {0x82, 0x55}, 
	  {0x87, 0x4a}, 
	
	////////////////////ASDE////////////////////
	  {0xfe, 0x01},
	  {0x18, 0x22},  //[7:4]AWB LUMA X	[3:0]ASDE LUMA X
	  {0xfe, 0x00},//ASDE dn b slope
	  {0x9c, 0x0a}, //ASDE dn b slope
	  {0xa4, 0x50}, // Auto Sa slope
	  {0xa5, 0x21}, // [7:4]Saturation limit x10
	  {0xa7, 0x35}, //low luma value th
	  {0xdd, 0x54}, //edge dec sat enable & slopes
	  {0x95, 0x35}, //Edge effect 
	
	////////////////////RGB gamma////////////////////
	  {0xfe, 0x00},
	  {0xbf, 0x06},
	  {0xc0, 0x14},
	  {0xc1, 0x27},
	  {0xc2, 0x3b},
	  {0xc3, 0x4f},
	  {0xc4, 0x62},
	  {0xc5, 0x72},
	  {0xc6, 0x8d},
	  {0xc7, 0xa4},
	  {0xc8, 0xb8},
	  {0xc9, 0xc9},
	  {0xca, 0xd6},
	  {0xcb, 0xe0},
	  {0xcc, 0xe8},
	  {0xcd, 0xf4},
	  {0xce, 0xfc},
	  {0xcf, 0xff},
	
		//////////////////CC///////////////////
	  {0xfe, 0x00},
	  {0xb3, 0x44},
	  {0xb4, 0xfd},
	  {0xb5, 0x02},
	  {0xb6, 0xfa},
	  {0xb7, 0x48},
	  {0xb8, 0xf0},
	
		//skin
		//{0xb3, 0x3c},
		//{0xb4, 0xFF},
		//{0xb5, 0x03},
		//{0xb6, 0x01},
		//{0xb7, 0x3f},
		//{0xb8, 0xF3},
	
		// crop 
	  {0x50, 0x01},
	  {0x19, 0x05},
	  {0x20, 0x01},
	  {0x22, 0xba},
	  {0x21, 0x48},
	
		////////////////////YCP////////////////////
	  {0xfe, 0x00},
	  {0xd1, 0x34}, //saturation Cb
	  {0xd2, 0x34}, //saturation Cr
	
		////////////////////AEC////////////////////
	  {0xfe, 0x01},
	  {0x10, 0x40},
	  {0x11, 0x21},//a1
	  {0x12, 0x07}, 
	  {0x13, 0x50}, //Y target
	  {0x17, 0x88}, 
	  {0x21, 0xb0},
	  {0x22, 0x48},
	  {0x3c, 0x95},
	  {0x3d, 0x50},
	  {0x3e, 0x48},
	
		////////////////////AWB////////////////////
	  {0xfe, 0x01},
	#if 0
	  {0x06, 0x06},
	  {0x07, 0x06},
	  {0x08, 0xa6},
	  {0x09, 0xee},
	  {0x50, 0xfc},
	  {0x51, 0x28},
	  {0x52, 0x10},
	  {0x53, 0x1d},
	  {0x54, 0x16},
	  {0x55, 0x20},
	  {0x56, 0x60},
		//{0x57, 0x40}, 
	  {0x58, 0x60},
	  {0x59, 0x28},
	  {0x5a, 0x02},
	  {0x5b, 0x63},
	  {0x5c, 0x34},
	  {0x5d, 0x73},
	  {0x5e, 0x11},
	  {0x5f, 0x40},
	  {0x60, 0x40},
	  {0x61, 0xc8},
	  {0x62, 0xa0},
	  {0x63, 0x40},
	  {0x64, 0x50},
	  {0x65, 0x98},
	  {0x66, 0xfa},
	  {0x67, 0x70},
	  {0x68, 0x58},
	  {0x69, 0x85},
	  {0x6a, 0x40},
	  {0x6b, 0x39},
	  {0x6c, 0x40},
	  {0x6d, 0x80},
	  {0x6e, 0x41},
	  {0x70, 0x50},
	  {0x71, 0x00},
	  {0x72, 0x10},
	  {0x73, 0x40},
	  {0x74, 0x32},
	  {0x75, 0x40},
	  {0x76, 0x30},
	  {0x77, 0x48},
	  {0x7a, 0x50},
	  {0x7b, 0x20},
	  {0x80, 0x4a},
	  {0x81, 0x43},
	  {0x82, 0x43},
	  {0x83, 0x40},
	  {0x84, 0x40},
	  {0x85, 0x40},
	#elif 1
	  {0x06, 0x08},
	  {0x07, 0x06},
	  {0x08, 0xa6},
	  {0x09, 0xee},
	  {0x50,0xfc}, //RGB high
	  {0x51,0x28}, //Y2C diff
	  {0x52,0x10}, //18
	  {0x53,0x08},//  //1d //20// 
	  {0x54,0x12}, //16 //30//C inter
	  {0x55,0x10}, //16
	  {0x56,0x10}, //20//60
	  {0x58,0x80}, //a0//60//number limit  X4
	  {0x59,0x08}, //0c //08 //AWB adjust temp curve //0c
	  {0x5a,0x02}, //01 //03//25//[3:0]light gain range x10
	  {0x5b,0x63}, //62
	  {0x5c,0x34}, //37 //show and mode [2]big C mode [1]dark mode [0] block move mode
	  {0x5d,0x73}, //72 //52//AWB margin
	  {0x5e,0x29}, //11 //20 //11 //19//temp curve_enable
	  {0x5f,0x40}, //5K gain
	  {0x60,0x40}, //5K gain
	  {0x61,0xc8}, //sinT
	  {0x62,0xa0}, //cosT
	  {0x63,0x40}, //38//30//AWB X1 cut
	  {0x64,0x38}, //50 //38 //30 //20 //40 //50 //60//AWB X2 cut
	  {0x65,0x98}, //a0 //98 //a0//AWB Y1 cut
	  {0x66,0xfa}, //ea//AWB Y2 cut
	  {0x67,0x80}, //70 //AWB R gain limit
	  {0x68,0x60}, //58 //58 //AWB G gain Limit
	  {0x69,0x90}, //85 //7d //AWB B gain limit
	  {0x6a,0x40},
	  {0x6b,0x39},
	  {0x6c,0x28}, //40
	  {0x6d,0x28}, //40//80
	  {0x6e,0x41}, //outdoor gain limit enable [7]use exp or luma value to adjust outdoor 
	  {0x70,0x10}, //50 //10
	  {0x71,0x00}, //when outdoor , add high luma gray pixel weight
	  {0x72,0x08}, //10
	  {0x73,0x40}, //95// when exp < th, outdoor mode open
	  {0x80,0x70}, //R gain high limit
	  {0x81,0x58}, //G gain high limit
	  {0x82,0x42}, //B gain high limit
	  {0x83,0x40}, //R gain low limit
	  {0x84,0x40}, //G gain low limit
	  {0x85,0x40}, //B gain low limit
		
	#else
	  {0x06, 0x06},
	  {0x07, 0x06},
	  {0x08, 0xa6},
	  {0x09, 0xee},
	  {0x50, 0xfc},
	  {0x51, 0x30},//28
	  {0x52, 0x20},//10
	  {0x53, 0x0b}, //	1d
	  {0x54, 0x0b},// 16
	  {0x55, 0x12},//20
	  {0x56, 0x10},//60
		//{0x57, 0x40}, 
	  {0x58, 0x80},//60
	  {0x59, 0x28},
	  {0x5a, 0x02},
	  {0x5b, 0x63},
	  {0x5c, 0x37},//34
	  {0x5d, 0x72},//73
	  {0x5e, 0x11},
	  {0x5f, 0x40},
	  {0x60, 0x40},
	  {0x61, 0xc8},
	  {0x62, 0xa0},
	  {0x63, 0x40},
	  {0x64, 0x60},//50
	  {0x65, 0x9c},//98
	  {0x66, 0xf0},//fa
	  {0x67, 0x70},
	  {0x68, 0x58},
	  {0x69, 0x85},
	  {0x6a, 0x40},
	  {0x6b, 0x39},
	  {0x6c, 0x40},
	  {0x6d, 0x40},//80
	  {0x6e, 0x41},
	  {0x70, 0x10},//50
	  {0x71, 0x00},
	  {0x72, 0x20},//10
	  {0x73, 0x40},
	  {0x74, 0x32},
	  {0x75, 0x40},
	  {0x76, 0x30},
	  {0x77, 0x48},
	  {0x7a, 0x50},
	  {0x7b, 0x20},
	  {0x80, 0x70},//4// 4a
	  {0x81, 0x58},//43
	  {0x82, 0x42},//43
	  {0x83, 0x40},
	  {0x84, 0x40},
	  {0x85, 0x40},
	
	  {0x86, 0x50},
	  {0x87, 0x50},
	  {0x88, 0xd0},
	  {0x88, 0x90},
	#endif
	
		////////////////////CC-AWB////////////////////
	  {0xd0, 0x00},
	  {0xd2, 0x2c}, 
	  {0xd3, 0x80},
	
		///////////////////ABS///////////////////////
	  {0x9c, 0x02},
	  {0x9d, 0x10},
	
		///////////////////LSC //////////////////////
		//// for xuye062d lens setting
	  {0xfe, 0x01},
	  {0xa0, 0x00},
	  {0xa1, 0x3c},
	  {0xa2, 0x50},
	  {0xa3, 0x00},
	  {0xa8, 0x0f},
	  {0xa9, 0x08},
	  {0xaa, 0x00},
	  {0xab, 0x04},
	  {0xac, 0x00},
	  {0xad, 0x07},
	  {0xae, 0x0e},
	  {0xaf, 0x00},
	  {0xb0, 0x00},
	  {0xb1, 0x09},
	  {0xb2, 0x00},
	  {0xb3, 0x00},
	  {0xb4, 0x31},
	  {0xb5, 0x19},
	  {0xb6, 0x24},
	  {0xba, 0x3a},
	  {0xbb, 0x24},
	  {0xbc, 0x2a},
	  {0xc0, 0x17},
	  {0xc1, 0x13},
	  {0xc2, 0x17},
	  {0xc6, 0x21},
	  {0xc7, 0x1c},
	  {0xc8, 0x1c},
	  {0xb7, 0x00},
	  {0xb8, 0x00},
	  {0xb9, 0x00},
	  {0xbd, 0x00},
	  {0xbe, 0x00},
	  {0xbf, 0x00},
	  {0xc3, 0x00},
	  {0xc4, 0x00},
	  {0xc5, 0x00},
	  {0xc9, 0x00},
	  {0xca, 0x00},
	  {0xcb, 0x00},
	  {0xa4, 0x00},
	  {0xa5, 0x00},
	  {0xa6, 0x00},
	  {0xa7, 0x00},
	
	  {0xfe, 0x00},
		////////////////////asde ///////////////////
	  {0xa0, 0xaf},
	  {0xa2, 0xff},
	
	  {0x44, 0xa2},  // YUV  order

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
	SensorEnd
};
/*
*  The following setting must been filled, if the function is turn on by CONFIG_SENSOR_xxxx
*/
static struct rk_sensor_reg sensor_WhiteB_Auto[]=
{
	{0x77, 0x57},
	{0x78, 0x4d},
	{0x79, 0x45},
	{0x42, 0xfe},
	SensorEnd
};
/* Cloudy Colour Temperature : 6500K - 8000K  */
static	struct rk_sensor_reg sensor_WhiteB_Cloudy[]=
{
	{0x42, 0xfc},
	{0x77, 0x8c}, //WB_manual_gain 
	{0x78, 0x50},
	{0x79, 0x40},
	SensorEnd
};
/* ClearDay Colour Temperature : 5000K - 6500K	*/
static	struct rk_sensor_reg sensor_WhiteB_ClearDay[]=
{
	//Sunny
			{0x42, 0xfc},
			{0x77, 0x74}, 
			{0x78, 0x52},
			{0x79, 0x40},	
	SensorEnd
};
/* Office Colour Temperature : 3500K - 5000K  */
static	struct rk_sensor_reg sensor_WhiteB_TungstenLamp1[]=
{
	//Office
			{0x42, 0xfc},
			{0x77, 0x40},
			{0x78, 0x42},
			{0x79, 0x50},
	SensorEnd

};
/* Home Colour Temperature : 2500K - 3500K	*/
static	struct rk_sensor_reg sensor_WhiteB_TungstenLamp2[]=
{
	//Home
	{0x42, 0xfc},
	{0x77, 0x40},
	{0x78, 0x54},
	{0x79, 0x70},
	SensorEnd
};
static struct rk_sensor_reg *sensor_WhiteBalanceSeqe[] = {sensor_WhiteB_Auto, sensor_WhiteB_TungstenLamp1,sensor_WhiteB_TungstenLamp2,
	sensor_WhiteB_ClearDay, sensor_WhiteB_Cloudy,NULL,
};

static	struct rk_sensor_reg sensor_Brightness0[]=
{
	// Brightness -2
	
	{0xfe, 0x01}, 
	{0x13, 0x40},
	{0xfe, 0x00}, 
	{0xd5, 0xe0},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness1[]=
{
	// Brightness -1
	
	{0xfe, 0x01}, 
	{0x13, 0x48},
	{0xfe, 0x00}, 
	{0xd5, 0xf0},

	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness2[]=
{
	//	Brightness 0
	
	
	{0xfe, 0x01}, 
	{0x13, 0x50},
	{0xfe, 0x00}, 
	{0xd5, 0x00},

	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness3[]=
{
	// Brightness +1
	
	{0xfe, 0x01}, 
	{0x13, 0x58},
	{0xfe, 0x00}, 
	{0xd5, 0x10},

	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness4[]=
{
	//	Brightness +2
	
	{0xfe, 0x01}, 
	{0x13, 0x60},
	{0xfe, 0x00}, 
	{0xd5, 0x20},

	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness5[]=
{
	//	Brightness +3
	
	{0xfe, 0x01}, 
	{0x13, 0x68},
	{0xfe, 0x00}, 
	{0xd5, 0x30},

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
	{0x43,0x02},
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
#if 0
static struct rk_sensor_reg sensor_Effect_Grayscale[]=
{
	{0x23,0x02},	
	{0x2d,0x0a},
	{0x20,0xff},
	{0xd2,0x90},
	{0x73,0x00},

	{0xb3,0x40},
	{0xb4,0x80},
	{0xba,0x00},
	{0xbb,0x00},
	{0x00,0x00}
};
#endif
static struct rk_sensor_reg *sensor_EffectSeqe[] = {sensor_Effect_Normal, sensor_Effect_WandB, sensor_Effect_Negative,sensor_Effect_Sepia,
	sensor_Effect_Bluish, sensor_Effect_Green,NULL,
};

static	struct rk_sensor_reg sensor_Exposure0[]=
{
	//-3

	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure1[]=
{
	//-2
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure2[]=
{
	//-1
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure3[]=
{
	//default
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure4[]=
{
	// 1
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure5[]=
{
	// 2

	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure6[]=
{
	// 3
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

	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast1[]=
{

	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast2[]=
{

	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast3[]=
{

	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast4[]=
{

	SensorEnd
};


static	struct rk_sensor_reg sensor_Contrast5[]=
{

	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast6[]=
{

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
	{MEDIA_BUS_FMT_YUYV8_2X8, V4L2_COLORSPACE_JPEG}
};
//static struct soc_camera_ops sensor_ops;


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
	char pid = 0;
	int ret = 0;
	struct generic_sensor *sensor = to_generic_sensor(client);
	
	/* check if it is an sensor sensor */
	ret = sensor_write(client, 0xfc, 0x16);  //before read id should write 0xfc
	msleep(20);
	ret = sensor_read(client, 0x00, &pid);
	if (ret != 0) {
		SENSOR_TR("%s read chip id high byte failed\n",SENSOR_NAME_STRING());
		ret = -ENODEV;
	}
	SENSOR_DG("\n %s  pid = 0x%x\n", SENSOR_NAME_STRING(), pid);
	if (pid == SENSOR_ID) {
		sensor->model = SENSOR_V4L2_IDENT;
	} else {
		SENSOR_TR("error: %s mismatched   pid = 0x%x\n", SENSOR_NAME_STRING(), pid);
		ret = -ENODEV;
	}
	return pid;
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
			if (err == 0) {
				if((val & 0x1) == 0)
					err = sensor_write(client, 0x17, (val |0x1));
				else 
					err = sensor_write(client, 0x17, (val & 0xfe));
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
		if (err == 0) {
			if((val & 0x2) == 0)
				err = sensor_write(client, 0x17, (val |0x2));
			else 
				err = sensor_write(client, 0x17, (val & 0xfc));
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





