/*
 * drivers/media/video/ov2659.c
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
#include <linux/moduleparam.h>
/*
*      Driver Version Note
*v0.0.1: this driver is compatible with generic_sensor
*v0.1.1:
*        add sensor_focus_af_const_pause_usr_cb;
*/
static int version = KERNEL_VERSION(0,1,3);
module_param(version, int, S_IRUGO);

static int debug;
module_param(debug, int, S_IRUGO|S_IWUSR);

#define dprintk(level, fmt, arg...) do {			\
	if (debug >= level) 					\
	printk(KERN_WARNING fmt , ## arg); } while (0)
#define debug_printk(format, ...) dprintk(1, format, ## __VA_ARGS__)  
/* Sensor Driver Configuration Begin */
#define SENSOR_NAME RK29_CAM_SENSOR_OV2659
#define SENSOR_V4L2_IDENT V4L2_IDENT_OV2659
#define SENSOR_ID 0x2656
#define SENSOR_BUS_PARAM                     (V4L2_MBUS_MASTER |\
                                             V4L2_MBUS_PCLK_SAMPLE_RISING|V4L2_MBUS_HSYNC_ACTIVE_HIGH| V4L2_MBUS_VSYNC_ACTIVE_LOW|\
                                             V4L2_MBUS_DATA_ACTIVE_HIGH  |SOCAM_MCLK_24MHZ)
#define SENSOR_PREVIEW_W                     800
#define SENSOR_PREVIEW_H                     600
#define SENSOR_PREVIEW_FPS                   15000     // 15fps 
#define SENSOR_FULLRES_L_FPS                 7500      // 7.5fps
#define SENSOR_FULLRES_H_FPS                 7500      // 7.5fps
#define SENSOR_720P_FPS                      0
#define SENSOR_1080P_FPS                     0

#define SENSOR_REGISTER_LEN                  2         // sensor register address bytes
#define SENSOR_VALUE_LEN                     1         // sensor register value bytes
                                    
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
	{0x3000, 0x0f},
	{0x3001, 0xff},
	{0x3002, 0xff},
	//{0x0100, 0x01},	//software sleep : Sensor vsync singal may not output if haven't sleep the sensor when transfer the array
	{0x3633, 0x3d},
	{0x3620, 0x02},
	{0x3631, 0x11},
	{0x3612, 0x04},
	{0x3630, 0x20},
	{0x4702, 0x02},
	{0x370c, 0x34},
	{0x3004, 0x10},
	{0x3005, 0x18},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x00},
	{0x3804, 0x06},
	{0x3805, 0x5f},
	{0x3806, 0x04},
	{0x3807, 0xb7},
	{0x3808, 0x03},
	{0x3809, 0x20},
	{0x380a, 0x02},
	{0x380b, 0x58},
	{0x380c, 0x05},
	{0x380d, 0x14},
	{0x380e, 0x02},
	{0x380f, 0x68},
	{0x3811, 0x08},
	{0x3813, 0x02},
	{0x3814, 0x31},
	{0x3815, 0x31},
	{0x3a02, 0x02},
	{0x3a03, 0x68},
	{0x3a08, 0x00},
	{0x3a09, 0x5c},
	{0x3a0a, 0x00},
	{0x3a0b, 0x4d},
	{0x3a0d, 0x08},
	{0x3a0e, 0x06},
	{0x3a14, 0x02},
	{0x3a15, 0x28},
		{0x4708, 0x01},
	{0x3623, 0x00},
	{0x3634, 0x76},
	{0x3701, 0x44},
	{0x3702, 0x18},
	{0x3703, 0x24},
	{0x3704, 0x24},
	{0x3705, 0x0c},
	{0x3820, 0x81},
	{0x3821, 0x01},
	{0x370a, 0x52},
	{0x4608, 0x00},
	{0x4609, 0x80},
	{0x4300, 0x32},
	{0x5086, 0x02},
	{0x5000, 0xfb},
	{0x5001, 0x1f},
	{0x5002, 0x00},
	{0x5025, 0x0e},
	{0x5026, 0x18},
	{0x5027, 0x34},
	{0x5028, 0x4c},
	{0x5029, 0x62},
	{0x502a, 0x74},
	{0x502b, 0x85},
	{0x502c, 0x92},
	{0x502d, 0x9e},
	{0x502e, 0xb2},
	{0x502f, 0xc0},
	{0x5030, 0xcc},
	{0x5031, 0xe0},
	{0x5032, 0xee},
	{0x5033, 0xf6},
	{0x5034, 0x11},
	{0x5070, 0x1c},
	{0x5071, 0x5b},
	{0x5072, 0x05},
	{0x5073, 0x20},
	{0x5074, 0x94},
	{0x5075, 0xb4},
	{0x5076, 0xb4},
	{0x5077, 0xaf},
	{0x5078, 0x05},
	{0x5079, 0x98},
	{0x507a, 0x21},
	{0x5035, 0x6a},
	{0x5036, 0x11},
	{0x5037, 0x92},
	{0x5038, 0x21},
	
	{0x5039, 0xe1},
	{0x503a, 0x01},
	{0x503c, 0x05},
	{0x503d, 0x08},
	{0x503e, 0x08},
	{0x503f, 0x64},
	{0x5040, 0x58},
	{0x5041, 0x2a},
	{0x5042, 0xc5},
	{0x5043, 0x2e},
	{0x5044, 0x3a},
	{0x5045, 0x3c},
	{0x5046, 0x44},
	{0x5047, 0xf8},
	{0x5048, 0x08},
	{0x5049, 0x70},
	{0x504a, 0xf0},
	{0x504b, 0xf0},
	{0x500c, 0x03},
	{0x500d, 0x20},
	{0x500e, 0x02},
	{0x500f, 0x5c},
	{0x5010, 0x48},
	{0x5011, 0x00},
	{0x5012, 0x66},
	{0x5013, 0x03},
	{0x5014, 0x30},
	{0x5015, 0x02},
	{0x5016, 0x7c},
	{0x5017, 0x40},
	{0x5018, 0x00},
	{0x5019, 0x66},
	{0x501a, 0x03},
	{0x501b, 0x10},
	{0x501c, 0x02},
	{0x501d, 0x7c},
	{0x501e, 0x3a},
	{0x501f, 0x00},
	{0x5020, 0x66},
	{0x506e, 0x44},
	{0x5064, 0x08},
	{0x5065, 0x10},
	{0x5066, 0x12},
	{0x5067, 0x02},
	{0x506c, 0x08},
	{0x506d, 0x10},
	{0x506f, 0xa6},
	{0x5068, 0x08},
	
	
	{0x5069, 0x10},
	{0x506a, 0x04},
	{0x506b, 0x12},
	{0x507e, 0x40},
	{0x507f, 0x20},
	{0x507b, 0x02},
	{0x507a, 0x01},
	{0x5084, 0x0c},
	{0x5085, 0x3e},
	{0x5005, 0x80},
	{0x3a0f, 0x30},
	{0x3a10, 0x28},
	{0x3a1b, 0x32},
	{0x3a1e, 0x26},
	{0x3a11, 0x60},
	{0x3a1f, 0x14},
	{0x5060, 0x69},
	{0x5061, 0x7d},
	{0x5062, 0x7d},
	{0x5063, 0x69},
	{0x3004, 0x20},
	{0x0100, 0x01},
	SensorEnd
};
/* Senor full resolution setting: recommand for capture */
static struct rk_sensor_reg sensor_fullres_lowfps_data[] ={

	{0x3503,0x03}, 
	{0x506e,0x44},	
	{0x5064,0x08},	
	{0x5065,0x10},
	{0x5066,0x18},	// zenghaihui 20110920 16
	{0x5067,0x10},
	{0x506c,0x08},
	{0x506d,0x10},	
	{0x506f,0xa6},	
	{0x5068,0x08},
	{0x5069,0x10},	
	{0x506a,0x08},
	{0x506b,0x28},
	{0x5084,0x14},//0c
	{0x5085,0x3c},//34	
	{0x5005,0x80}, 



	{0x5066, 0x3c}, 		
	{0x5067, 0x1a}, 
	{0x506a, 0x0e},    
	{0x506b, 0x2e},    

	{0x3800, 0x00}, 
	{0x3801, 0x00}, 
	{0x3802, 0x00}, 
	{0x3803, 0x00}, 
	{0x3804, 0x06}, 
	{0x3805, 0x5f}, 
	{0x3806, 0x04}, 
	{0x3807, 0xbb}, 
	{0x3808, 0x06}, 
	{0x3809, 0x40}, 
	{0x380a, 0x04}, 
	{0x380b, 0xb0}, 
	{0x3811, 0x10}, 
	{0x3813, 0x06}, 
	{0x3814, 0x11}, 
	{0x3815, 0x11}, 

	{0x3623, 0x00}, 
	{0x3634, 0x44}, 
	{0x3701, 0x44}, 
	{0x3208, 0xa2}, 
	{0x3705, 0x18}, 	 
	{0x3820, 0x80}, 
	{0x3821, 0x00}, 

	{0x3003, 0x80},//10fps 
	{0x3004, 0x20}, //10		 
	{0x3005, 0x18}, 
	{0x3006, 0x0d}, 

	{0x380c, 0x07}, 
	{0x380d, 0x9f}, 
	{0x380e, 0x04}, 
	{0x380f, 0xd0}, 

	{0x370a, 0x12}, 
	{0x4608, 0x00}, 
	{0x4609, 0x80}, 
	{0x5002, 0x00}, 

	{0x3a08, 0x00}, 
	{0x3a09, 0x3e},//7b 
	{0x3a0e, 0x13},//0a 

	{0x3a0a, 0x00}, 
	{0x3a0b, 0x3e},//7b 				
	{0x3a0d, 0x13},//0a 		

	{0x4003, 0x88},
	SensorEnd
};
/* Senor full resolution setting: recommand for video */
static struct rk_sensor_reg sensor_fullres_highfps_data[] ={
	SensorEnd
};
/* Preview resolution setting*/
static struct rk_sensor_reg sensor_preview_data[] =
{
	{0x0100, 0x00},    //software sleep : Sensor vsync singal may not output if haven't sleep the sensor when transfer the array,
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x00},
	{0x3804, 0x06},
	{0x3805, 0x5f},
	{0x3806, 0x04},
	{0x3807, 0xb7},
	{0x3808, 0x03},
	{0x3809, 0x20},
	{0x380a, 0x02},
	{0x380b, 0x58},
	{0x380c, 0x05},
	{0x380d, 0x14},
	{0x380e, 0x02},
	{0x380f, 0x68},
	{0x3811, 0x08},
	{0x3813, 0x02},
	{0x3814, 0x31},
	{0x3815, 0x31},
	{0x3a02, 0x02},
	{0x3a03, 0x68},
	{0x3a08, 0x00},
	{0x3a09, 0x5c},
	{0x3a0a, 0x00},
	{0x3a0b, 0x4d},
	{0x3a0d, 0x08},
	{0x3a0e, 0x06},
	{0x3a14, 0x02},
	{0x3a15, 0x28},
	{0x3623, 0x00},
	{0x3634, 0x76},
	{0x3701, 0x44},
	{0x3702, 0x18},
	{0x3703, 0x24},
	{0x3704, 0x24},
	{0x3705, 0x0c},
	{0x3820, 0x81},
	{0x3821, 0x01},
	{0x370a, 0x52},
	{0x4608, 0x00},
	{0x4609, 0x80},
	{0x5002, 0x10},
	{0x3005, 0x18},
	{0x3004, 0x20},
	{0x3503,0x00},
	{0x0100, 0x01}, 	//software wake

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
    SensorRegVal(0x0103,0x01),
	SensorEnd
};

static struct rk_sensor_reg sensor_check_id_data[]={
    SensorRegVal(0x300a,0),
    SensorRegVal(0x300b,0),
	SensorEnd
};
/*
*  The following setting must been filled, if the function is turn on by CONFIG_SENSOR_xxxx
*/
static struct rk_sensor_reg sensor_WhiteB_Auto[]=
{
	{0x3406, 0x00},  //AWB auto, bit[1]:0,auto
	SensorEnd
};
/* Cloudy Colour Temperature : 6500K - 8000K  */
static	struct rk_sensor_reg sensor_WhiteB_Cloudy[]=
{
	{0x3406, 0x01},
	{0x3400, 0x07},
	{0x3401, 0x08},
	{0x3402, 0x04},
	{0x3403, 0x00},
	{0x3404, 0x05},
	{0x3405, 0x00},
	SensorEnd
};
/* ClearDay Colour Temperature : 5000K - 6500K	*/
static	struct rk_sensor_reg sensor_WhiteB_ClearDay[]=
{
	//Sunny
	{0x3406, 0x01},
	{0x3400, 0x07},
	{0x3401, 0x02},
	{0x3402, 0x04},
	{0x3403, 0x00},
	{0x3404, 0x05},
	{0x3405, 0x15},
	SensorEnd
};
/* Office Colour Temperature : 3500K - 5000K  */
static	struct rk_sensor_reg sensor_WhiteB_TungstenLamp1[]=
{
	//Office
	{0x3406, 0x01},
	{0x3400, 0x06},
	{0x3401, 0x2a},
	{0x3402, 0x04},
	{0x3403, 0x00},
	{0x3404, 0x07},
	{0x3405, 0x24},
	SensorEnd

};
/* Home Colour Temperature : 2500K - 3500K	*/
static	struct rk_sensor_reg sensor_WhiteB_TungstenLamp2[]=
{
	//Home
	{0x3406, 0x01},
	{0x3400, 0x04},
	{0x3401, 0x58},
	{0x3402, 0x04},
	{0x3403, 0x00},
	{0x3404, 0x07},
	{0x3405, 0x24},
	SensorEnd
};
static struct rk_sensor_reg *sensor_WhiteBalanceSeqe[] = {sensor_WhiteB_Auto, sensor_WhiteB_TungstenLamp1,sensor_WhiteB_TungstenLamp2,
	sensor_WhiteB_ClearDay, sensor_WhiteB_Cloudy,NULL,
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
	{0x507b, 0x00},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_WandB[] =
{
	{0x507b, 0x20},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_Sepia[] =
{
	{0x507b, 0x18},
	{0x507e, 0x40},
	{0x507f, 0xa0},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_Negative[] =
{
	//Negative
	{0x507b, 0x40}, //bit[6] negative
	SensorEnd
};
static	struct rk_sensor_reg sensor_Effect_Bluish[] =
{
	// Bluish
	{0x507b, 0x18},
	{0x507e, 0xa0},
	{0x507f, 0x40},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_Green[] =
{
	//	Greenish
	{0x507b, 0x18},
	{0x507e, 0x60},
	{0x507f, 0x60},
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
	{0x3a00, 0x78},
	SensorEnd
};

static	struct rk_sensor_reg sensor_SceneNight[] =
{
	{0x3003, 0x80},
	{0x3004, 0x20},
	{0x3005, 0x18},
	{0x3006, 0x0d},
	{0x3a00, 0x7c},
	{0x3a02 ,0x07},
	{0x3a03 ,0x38},
	{0x3a14 ,0x07},
	{0x3a15 ,0x38},
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
	{MEDIA_BUS_FMT_UYVY8_2X8, V4L2_COLORSPACE_JPEG},
	{MEDIA_BUS_FMT_YUYV8_2X8, V4L2_COLORSPACE_JPEG}
};
//static struct soc_camera_ops sensor_ops;//yzm


/*
**********************************************************
* Following is local code:
* 
* Please codeing your program here 
**********************************************************
*/
static int sensor_parameter_record(struct i2c_client *client)
{
	u8 ret_l,ret_m,ret_h;
	int tp_l,tp_m,tp_h;
	
	struct generic_sensor *sensor = to_generic_sensor(client);
	struct specific_sensor *spsensor = to_specific_sensor(sensor);

	sensor_read(client,0x3a00, &ret_l);
	sensor_write(client,0x3a00, ret_l&0xfb);

	sensor_write(client,0x3503,0x07);	//stop AE/AG

	sensor_read(client,0x3500,&ret_h);
	sensor_read(client,0x3501, &ret_m);
	sensor_read(client,0x3502, &ret_l);
	tp_l = ret_l;
	tp_m = ret_m;
	tp_h = ret_h;
	spsensor->parameter.preview_exposure = ((tp_h<<12) & 0xF000) | ((tp_m<<4) & 0x0FF0) | ((tp_l>>4) & 0x0F);
	
	//Read back AGC Gain for preview
	sensor_read(client,0x350b, &ret_l);
	spsensor->parameter.preview_gain = ret_l;

	spsensor->parameter.CapturePclk = 24000;
	spsensor->parameter.PreviewPclk = 24000;
	spsensor->parameter.PreviewDummyPixels = 0;
	spsensor->parameter.CaptureDummyPixels = 0;
	SENSOR_DG("Read 0x350b=0x%02x  PreviewExposure:%d 0x3500=0x%02x  0x3501=0x%02x 0x3502=0x%02x",
	          ret_l,spsensor->parameter.preview_exposure,tp_h, tp_m, tp_l);
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
static void OV2659SetDummy(struct i2c_client *client,unsigned int dummy_pixels, unsigned int dummy_lines)
{
	unsigned char val;
	unsigned int temp_reg1, temp_reg2;
	unsigned int temp_reg;
	
	if (dummy_pixels > 0)
	{
		sensor_read(client,0x380D,&val);	// HTS[b7~b0]
		temp_reg1 = val;
		sensor_read(client,0x380C,&val);	// HTS[b15~b8]
		temp_reg2 = val;
		temp_reg = (temp_reg1 & 0xFF) | (temp_reg2 << 8);
	
		temp_reg += dummy_pixels;
	
		sensor_write(client,0x380D,(temp_reg&0xFF));		 //HTS[7:0]
		sensor_write(client,0x380C,((temp_reg&0xFF00)>>8));  //HTS[15:8]
	}

	if (dummy_lines > 0)
	{
		sensor_read(client,0x380F,&val);	// VTS[b7~b0]
		temp_reg1 = val;
		sensor_read(client,0x380E,&val);	// VTS[b15~b8]
		temp_reg2 = val;
		temp_reg = (temp_reg1 & 0xFF) | (temp_reg2 << 8);
	
		temp_reg += dummy_lines;
	
		sensor_write(client,0x380F,(temp_reg&0xFF));		 //VTS[7:0]
		sensor_write(client,0x380E,((temp_reg&0xFF00)>>8));  //VTS[15:8]
	}
}	 /* OV2659_set_dummy */

static void OV2659WriteShutter(struct i2c_client *client,bool is_preview, unsigned int shutter)
{
	unsigned int extra_exposure_lines = 0;

	if (shutter < 1)
	{
		shutter = 1;
	}
	
	if (is_preview) 
	{
		if (shutter <= OV2659_PV_EXPOSURE_LIMITATION) 
		{
			extra_exposure_lines = 0;
		}
		else 
		{
			extra_exposure_lines=shutter - OV2659_PV_EXPOSURE_LIMITATION;
		}
		
	}
	else 
	{
		if (shutter <= OV2659_FULL_EXPOSURE_LIMITATION) 
		{
			extra_exposure_lines = 0;
		}
		else 
		{
			extra_exposure_lines = shutter - OV2659_FULL_EXPOSURE_LIMITATION;
		}
		
	}
	
	//AEC PK EXPOSURE
	shutter*=16;
	sensor_write(client,0x3502, (shutter & 0x00FF));		   //AEC[7:0]
	sensor_write(client,0x3501, ((shutter & 0x0FF00) >>8));  //AEC[15:8]
	sensor_write(client,0x3500, ((shutter & 0xFF0000) >> 16));	
	
	if(extra_exposure_lines>0)
	{
		// set extra exposure line [aec add vts]
		sensor_write(client,0x3507, extra_exposure_lines & 0xFF);		   // EXVTS[b7~b0]
		sensor_write(client,0x3506, (extra_exposure_lines & 0xFF00) >> 8); // EXVTS[b15~b8]
	}
	else
	{
		// set extra exposure line [aec add vts]
		sensor_write(client,0x3507, 0x00);			// EXVTS[b7~b0]
		sensor_write(client,0x3506, 0x00); // EXVTS[b15~b8]
	}
	
}	 /* OV2659_write_shutter */
static int sensor_ae_transfer(struct i2c_client *client)
{
	unsigned int prev_line_len,cap_line_len,shutter;
	struct generic_sensor *sensor = to_generic_sensor(client);
	struct specific_sensor *spsensor = to_specific_sensor(sensor);

	mdelay(100);
	shutter = spsensor->parameter.preview_exposure;

	OV2659SetDummy(client,600,0);	
	
	prev_line_len = OV2659_PV_PERIOD_PIXEL_NUMS + spsensor->parameter.PreviewDummyPixels;
	cap_line_len = OV2659_FULL_PERIOD_PIXEL_NUMS + spsensor->parameter.CaptureDummyPixels;
	shutter = (shutter * spsensor->parameter.CapturePclk) / spsensor->parameter.PreviewPclk;
	shutter = (shutter * prev_line_len) / cap_line_len;
	shutter*=2;

	OV2659WriteShutter(client,0,shutter);
	
	
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
    u8 reg_val;

    SENSOR_DG("%s",__FUNCTION__);
	
	sensor_read(client,0x3000,&reg_val);
	sensor_write(client, 0x3000, reg_val|0x03);
	sensor_write(client, 0x3001, 0xff);
	sensor_read(client,0x3002,&reg_val);
	sensor_write(client, 0x3002, reg_val|0xe0);
	
	return 0;
}
/*
* the function is called in close sensor
*/
static int sensor_deactivate_cb(struct i2c_client *client)
{
	u8 reg_val;
	struct generic_sensor *sensor = to_generic_sensor(client);

    SENSOR_DG("%s",__FUNCTION__);
    
	/* ddl@rock-chips.com : all sensor output pin must switch into Hi-Z */
	if (sensor->info_priv.funmodule_state & SENSOR_INIT_IS_OK) {
		sensor_read(client,0x3000,&reg_val);
		sensor_write(client, 0x3000, reg_val&0xfc);
		sensor_write(client, 0x3001, 0x00);
		sensor_read(client,0x3002,&reg_val);
		sensor_write(client, 0x3002, reg_val&0x1f);
	}
	
	return 0;
}
/*
* the function is called before sensor register setting in VIDIOC_S_FMT  
*/
static int sensor_s_fmt_cb_th(struct i2c_client *client,struct v4l2_mbus_framefmt *mf, bool capture)
{
    if (capture) {
        sensor_parameter_record(client);
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
		err = sensor_read(client, 0x3821, &val);
		if (err == 0) {
			val |= 0x06;
			err = sensor_write(client, 0x3821, val);
		}
	} else {
		err = sensor_read(client, 0x3821, &val);
		if (err == 0) {
			val &= 0xf9;
			err = sensor_write(client, 0x3821, val);
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
		err = sensor_read(client, 0x3820, &val);
		if (err == 0) {
			val |= 0x06;
			err = sensor_write(client, 0x3820, val);
		}
	} else {
		err = sensor_read(client, 0x3820, &val);
		if (err == 0) {
			val &= 0xf9;
			err = sensor_write(client, 0x3820, val);
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
static int 	sensor_face_detect_usr_cb(struct i2c_client *client,int on){
	return 0;
}

/*
*   The function can been run in sensor_init_parametres which run in sensor_probe, so user can do some
* initialization in the function. 
*/
static void sensor_init_parameters_user(struct specific_sensor* spsensor,struct soc_camera_device *icd)
{
	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__,__FUNCTION__);
    return;
}

/*
* :::::WARNING:::::
* It is not allowed to modify the following code
*/
#if 1
sensor_init_parameters_default_code();

sensor_v4l2_struct_initialization();

sensor_probe_default_code();

sensor_remove_default_code();
#endif
sensor_driver_default_module_code();

