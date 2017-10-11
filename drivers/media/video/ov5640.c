/*
 * drivers/media/video/ov5640.c
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
*v0.1.1:
*        add sensor_focus_af_const_pause_usr_cb;
*v0.1.3:
*        config sensor io H-Z in sensor_deactive_cb;
*/
static int version = KERNEL_VERSION(0,1,3);
module_param(version, int, S_IRUGO);


static int debug;
module_param(debug, int, S_IRUGO|S_IWUSR);

#define dprintk(level, fmt, arg...) do {			\
	if (debug >= level) 					\
	printk(KERN_WARNING fmt , ## arg); } while (0)

/* Sensor Driver Configuration Begin */
#define SENSOR_NAME RK29_CAM_SENSOR_OV5640
#define SENSOR_V4L2_IDENT V4L2_IDENT_OV5640
#define SENSOR_ID 0x5640
#define SENSOR_BUS_PARAM                     (V4L2_MBUS_MASTER |\
												 V4L2_MBUS_PCLK_SAMPLE_RISING|V4L2_MBUS_HSYNC_ACTIVE_HIGH| V4L2_MBUS_VSYNC_ACTIVE_LOW|\
												 V4L2_MBUS_DATA_ACTIVE_HIGH  |SOCAM_MCLK_24MHZ)
#define SENSOR_PREVIEW_W					 800
#define SENSOR_PREVIEW_H					 600
#define SENSOR_PREVIEW_FPS					 15000	   // 15fps 
#define SENSOR_FULLRES_L_FPS				 7500	   // 7.5fps
#define SENSOR_FULLRES_H_FPS				 7500	   // 7.5fps
#define SENSOR_720P_FPS 					 30000
#define SENSOR_1080P_FPS					 15000

#define SENSOR_REGISTER_LEN 				 2		   // sensor register address bytes
#define SENSOR_VALUE_LEN					 1		   // sensor register value bytes
									
static unsigned int SensorConfiguration = (CFG_WhiteBalance|CFG_Effect
                                           |CFG_Scene|CFG_Focus
                                           |CFG_FocusZone);

static unsigned int SensorChipID[] = {SENSOR_ID};
/* Sensor Driver Configuration End */


#define SENSOR_NAME_STRING(a) STR(CONS(SENSOR_NAME, a))
#define SENSOR_NAME_VARFUN(a) CONS(SENSOR_NAME, a)

#define SensorRegVal(a,b) CONS4(SensorReg,SENSOR_REGISTER_LEN,Val,SENSOR_VALUE_LEN)(a,b)
#define sensor_write(client,reg,v) CONS4(sensor_write_reg,SENSOR_REGISTER_LEN,val,SENSOR_VALUE_LEN)(client,(reg),(v))
#define sensor_read(client,reg,v) CONS4(sensor_read_reg,SENSOR_REGISTER_LEN,val,SENSOR_VALUE_LEN)(client,(reg),(v))
#define sensor_write_array generic_sensor_write_array

#if 1//CONFIG_SENSOR_Focus
#include "ov5640_af_firmware.c"
/* ov5640 VCM Command and Status Registers */
#define CONFIG_SENSOR_FocusCenterInCapture	  0
#define CMD_MAIN_Reg		0x3022
//#define CMD_TAG_Reg			0x3023
#define CMD_ACK_Reg 		0x3023
#define CMD_PARA0_Reg		0x3024
#define CMD_PARA1_Reg		0x3025
#define CMD_PARA2_Reg		0x3026
#define CMD_PARA3_Reg		0x3027
#define CMD_PARA4_Reg		0x3028

//#define STA_ZONE_Reg			0x3026
#define STA_FOCUS_Reg		0x3029

/* ov5640 VCM Command  */

#define ConstFocus_Cmd	  0x04
#define StepMode_Cmd	  0x05
#define PauseFocus_Cmd	  0x06
#define ReturnIdle_Cmd	  0x08
#define SetZone_Cmd 	  0x10
#define UpdateZone_Cmd	  0x12
#define SetMotor_Cmd	  0x20
#define SingleFocus_Cmd 			0x03
#define GetFocusResult_Cmd			0x07
#define ReleaseFocus_Cmd			0x08
#define ZoneRelaunch_Cmd			0x12
#define DefaultZoneConfig_Cmd		0x80
#define TouchZoneConfig_Cmd 		0x81
#define CustomZoneConfig_Cmd		0x8f


/* ov5640 Focus State */
//#define S_FIRWRE				0xFF		/*Firmware is downloaded and not run*/
#define S_STARTUP			0x7e		/*Firmware is initializing*/
#define S_ERROR 			0x7f
#define S_IDLE				0x70		/*Idle state, focus is released; lens is located at the furthest position.*/
#define S_FOCUSING			0x00		/*Auto Focus is running.*/
#define S_FOCUSED			0x10		/*Auto Focus is completed.*/

#define S_CAPTURE			0x12
#define S_STEP					0x20

/* ov5640 Zone State */
#define Zone_Is_Focused(a, zone_val)	(zone_val&(1<<(a-3)))
#define Zone_Get_ID(zone_val)			(zone_val&0x03)

#define Zone_CenterMode   0x01
#define Zone_5xMode 	  0x02
#define Zone_5PlusMode	  0x03
#define Zone_4fMode 	  0x04

#define ZoneSel_Auto	  0x0b
#define ZoneSel_SemiAuto  0x0c
#define ZoneSel_Manual	  0x0d
#define ZoneSel_Rotate	  0x0e

/* ov5640 Step Focus Commands */
#define StepFocus_Near_Tag		 0x01
#define StepFocus_Far_Tag		 0x02
#define StepFocus_Furthest_Tag	 0x03
#define StepFocus_Nearest_Tag	 0x04
#define StepFocus_Spec_Tag		 0x10
#endif

struct sensor_parameter
{
	unsigned int PreviewDummyPixels;
	unsigned int CaptureDummyPixels;
	unsigned int preview_exposure;
	unsigned short int preview_line_width;
	unsigned short int preview_gain;
	unsigned short int preview_maxlines;

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
	{0x3103, 0x11},
	{0x3008, 0x82},
	SensorWaitMs(5),
	{0x3008, 0x42},
	{0x3103, 0x03},
	{0x3017, 0xff},
	{0x3018, 0xff},
	{0x3034, 0x1a},
	{0x3035, 0x21},
	{0x3036, 0x46},
	{0x3037, 0x12},
	{0x3108, 0x01},
	{0x3630, 0x36},
	{0x3631, 0x0e},
	{0x3632, 0xe2},
	{0x3633, 0x12},
	{0x3621, 0xe0},
	{0x3704, 0xa0},
	{0x3703, 0x5a},
	{0x3715, 0x78},
	{0x3717, 0x01},
	{0x370b, 0x60},
	{0x3705, 0x1a},
	{0x3905, 0x02},
	{0x3906, 0x10},
	{0x3901, 0x0a},
	{0x3731, 0x12},
	{0x3600, 0x08},
	{0x3601, 0x33},
	{0x302d, 0x60},
	{0x3620, 0x52},
	{0x371b, 0x20},
	{0x471c, 0x50},
	{0x3a13, 0x43},
	{0x3a18, 0x00},
	{0x3a19, 0xf8},
	{0x3635, 0x13},
	{0x3636, 0x03},
	{0x3634, 0x40},
	{0x3622, 0x01},
	{0x3c01, 0x34},
	{0x3c04, 0x28},
	{0x3c05, 0x98},
	{0x3c06, 0x00},
	{0x3c07, 0x08},
	{0x3c08, 0x00},
	{0x3c09, 0x1c},
	{0x3c0a, 0x9c},
	{0x3c0b, 0x40},
	{0x3820, 0x41},
	{0x3821, 0x07},
	{0x3814, 0x31},
	{0x3815, 0x31},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x04},
	{0x3804, 0x0a},
	{0x3805, 0x3f},
	{0x3806, 0x07},
	{0x3807, 0x9b},
	{0x3808, 0x03},
	{0x3809, 0x20},
	{0x380a, 0x02},
	{0x380b, 0x58},
	{0x380c, 0x07},
	{0x380d, 0x68},
	{0x380e, 0x03},
	{0x380f, 0xd8},
	{0x3810, 0x00},
	{0x3811, 0x10},
	{0x3812, 0x00},
	{0x3813, 0x06},
	{0x3618, 0x00},
	{0x3612, 0x29},
	{0x3708, 0x64},
	{0x3709, 0x52},
	{0x370c, 0x03},
	{0x3a02, 0x03},
	{0x3a03, 0xd8},
	{0x3a08, 0x01},
	{0x3a09, 0x27},
	{0x3a0a, 0x00},
	{0x3a0b, 0xf6},
	{0x3a0e, 0x03},
	{0x3a0d, 0x04},
	{0x3a14, 0x03},
	{0x3a15, 0xd8},
	{0x4001, 0x02},
	{0x4004, 0x02},
	{0x3000, 0x00},
	{0x3002, 0x1c},
	{0x3004, 0xff},
	{0x3006, 0xc3},
	{0x300e, 0x58},
	{0x302e, 0x00},
	{0x4740, 0x20},
	{0x4300, 0x32},//uyvy:0x32,yuyv:0x30
	{0x501f, 0x00},
	{0x4713, 0x03},
	{0x4407, 0x04},
	{0x440e, 0x00},
	{0x460b, 0x35},
	{0x460c, 0x20},
	{0x4837, 0x22},
	{0x3824, 0x02},
	{0x5000, 0xa7},
	{0x5001, 0xa3},
	{0x5180, 0xff},
	{0x5181, 0xf2},
	{0x5182, 0x00},
	{0x5183, 0x14},
	{0x5184, 0x25},
	{0x5185, 0x24},
	{0x5186, 0x09},
	{0x5187, 0x09},
	{0x5188, 0x09},
	{0x5189, 0x75},
	{0x518a, 0x54},
	{0x518b, 0xe0},
	{0x518c, 0xb2},
	{0x518d, 0x42},
	{0x518e, 0x3d},
	{0x518f, 0x56},
	{0x5190, 0x46},
	{0x5191, 0xf8},
	{0x5192, 0x04},
	{0x5193, 0x70},
	{0x5194, 0xf0},
	{0x5195, 0xf0},
	{0x5196, 0x03},
	{0x5197, 0x01},
	{0x5198, 0x04},
	{0x5199, 0x12},
	{0x519a, 0x04},
	{0x519b, 0x00},
	{0x519c, 0x06},
	{0x519d, 0x82},
	{0x519e, 0x38},
	{0x5381, 0x1e},
	{0x5382, 0x5b},
	{0x5383, 0x08},
	{0x5384, 0x0a},
	{0x5385, 0x7e},
	{0x5386, 0x88},
	{0x5387, 0x7c},
	{0x5388, 0x6c},
	{0x5389, 0x10},
	{0x538a, 0x01},
	{0x538b, 0x98},
	{0x5300, 0x08},
	{0x5301, 0x30},
	{0x5302, 0x10},
	{0x5303, 0x00},
	{0x5304, 0x08},
	{0x5305, 0x30},
	{0x5306, 0x08},
	{0x5307, 0x16},
	{0x5309, 0x08},
	{0x530a, 0x30},
	{0x530b, 0x04},
	{0x530c, 0x06},
	{0x5480, 0x01},
	{0x5481, 0x08},
	{0x5482, 0x14},
	{0x5483, 0x28},
	{0x5484, 0x51},
	{0x5485, 0x65},
	{0x5486, 0x71},
	{0x5487, 0x7d},
	{0x5488, 0x87},
	{0x5489, 0x91},
	{0x548a, 0x9a},
	{0x548b, 0xaa},
	{0x548c, 0xb8},
	{0x548d, 0xcd},
	{0x548e, 0xdd},
	{0x548f, 0xea},
	{0x5490, 0x1d},
	{0x5580, 0x02},
	{0x5583, 0x40},
	{0x5584, 0x10},
	{0x5589, 0x10},
	{0x558a, 0x00},
	{0x558b, 0xf8},
	{0x5800, 0x23},
	{0x5801, 0x14},
	{0x5802, 0x0f},
	{0x5803, 0x0f},
	{0x5804, 0x12},
	{0x5805, 0x26},
	{0x5806, 0x0c},
	{0x5807, 0x08},
	{0x5808, 0x05},
	{0x5809, 0x05},
	{0x580a, 0x08},
	{0x580b, 0x0d},
	{0x580c, 0x08},
	{0x580d, 0x03},
	{0x580e, 0x00},
	{0x580f, 0x00},
	{0x5810, 0x03},
	{0x5811, 0x09},
	{0x5812, 0x07},
	{0x5813, 0x03},
	{0x5814, 0x00},
	{0x5815, 0x01},
	{0x5816, 0x03},
	{0x5817, 0x08},
	{0x5818, 0x0d},
	{0x5819, 0x08},
	{0x581a, 0x05},
	{0x581b, 0x06},
	{0x581c, 0x08},
	{0x581d, 0x0e},
	{0x581e, 0x29},
	{0x581f, 0x17},
	{0x5820, 0x11},
	{0x5821, 0x11},
	{0x5822, 0x15},
	{0x5823, 0x28},
	{0x5824, 0x46},
	{0x5825, 0x26},
	{0x5826, 0x08},
	{0x5827, 0x26},
	{0x5828, 0x64},
	{0x5829, 0x26},
	{0x582a, 0x24},
	{0x582b, 0x22},
	{0x582c, 0x24},
	{0x582d, 0x24},
	{0x582e, 0x06},
	{0x582f, 0x22},
	{0x5830, 0x40},
	{0x5831, 0x42},
	{0x5832, 0x24},
	{0x5833, 0x26},
	{0x5834, 0x24},
	{0x5835, 0x22},
	{0x5836, 0x22},
	{0x5837, 0x26},
	{0x5838, 0x44},
	{0x5839, 0x24},
	{0x583a, 0x26},
	{0x583b, 0x28},
	{0x583c, 0x42},
	{0x583d, 0xce},
	{0x5025, 0x00},
	{0x3a0f, 0x30},
	{0x3a10, 0x28},
	{0x3a1b, 0x30},
	{0x3a1e, 0x26},
	{0x3a11, 0x60},
	{0x3a1f, 0x14},
	{0x3008, 0x02},
	{0x302c, 0xc2},
	SensorWaitUs(1000),
	SensorEnd
};
/* Senor full resolution setting: recommand for capture */
static struct rk_sensor_reg sensor_fullres_lowfps_data[] ={

	{0x3820, 0x40}, 
	{0x3821, 0x06}, 
	{0x3814, 0x11}, 
	{0x3815, 0x11}, 
	{0x3803, 0x00}, 
	{0x3807, 0x9f}, 
	{0x3808, 0x0a}, 
	{0x3809, 0x20}, 
	{0x380a, 0x07}, 
	{0x380b, 0x98}, 
	{0x380c, 0x0b}, 
	{0x380d, 0x1c}, 
	{0x380e, 0x07}, 
	{0x380f, 0xb0}, 
	{0x3811, 0x10}, //
	{0x3813, 0x04}, 
	{0x3618, 0x04}, 
	{0x3612, 0x2b}, //4b 
	{0x3708, 0x64},
	{0x3709, 0x12}, 
	{0x370c, 0x00}, 
	{0x3a02, 0x07}, 
	{0x3a03, 0xb0}, 
	{0x3a0e, 0x06}, 
	{0x3a0d, 0x08}, 
	{0x3a14, 0x07}, 
	{0x3a15, 0xb0}, 
	{0x4004, 0x06},
	{0x5000, 0xa7}, 
	{0x5001, 0x83},
	{0x519e, 0x38},
	{0x5381, 0x1e},
	{0x5382, 0x5b},
	{0x5383, 0x08},
	{0x460b, 0x37}, 
	{0x460c, 0x20}, 
	{0x3824, 0x01}, 
	{0x4005, 0x1A}, 
	SensorEnd
};
/* Senor full resolution setting: recommand for video */
static struct rk_sensor_reg sensor_fullres_highfps_data[] ={
	SensorEnd
};
/* Preview resolution setting*/
static struct rk_sensor_reg sensor_preview_data[] =
{
	{0x3503, 0x00},
	{0x3c07, 0x08},
	{0x3820, 0x41},
	{0x3821, 0x07},
	{0x3814, 0x31},
	{0x3815, 0x31},
	{0x3803, 0x04},
	{0x3806, 0x07},///
	{0x3807, 0x9b},
	{0x3808, 0x03},
	{0x3809, 0x20},
	{0x380a, 0x02},
	{0x380b, 0x58},
	{0x380c, 0x07},
	{0x380d, 0x68},
	{0x380e, 0x03},
	{0x380f, 0xd8},
	{0x3813, 0x06},
	{0x3618, 0x00},
	{0x3612, 0x29},
	{0x3709, 0x52},
	{0x370c, 0x03},
	{0x3a02, 0x03},
	{0x3a03, 0xd8},
	{0x3a08 ,0x01},///
	{0x3a09, 0x27},///
	{0x3a0a, 0x00},///
	{0x3a0b, 0xf6},///
	{0x3a0e, 0x03},
	{0x3a0d, 0x04},
	{0x3a14, 0x03},
	{0x3a15, 0xd8},
	{0x4004, 0x02},
	{0x3002, 0x1c},////
	{0x4713, 0x03},////
	{0x3035, 0x21},
	{0x3036, 0x46},
	{0x4837, 0x22},
	{0x3824, 0x02},////
	{0x5001, 0xa3},

	SensorEnd
};
/* 1280x720 */
static struct rk_sensor_reg sensor_720p[]={
	{0x3503, 0x00},
		
	{0x3c07,0x07},
	{0x3803,0xfa},
	{0x3806,0x06},////
	{0x3807,0xa9},
	{0x3808,0x05},
	{0x3809,0x00},
	{0x380a,0x02},
	{0x380b,0xd0},
	{0x380c,0x07},
	{0x380d,0x64},
	{0x380e,0x02},
	{0x380f,0xe4},
	{0x3813,0x04},
	{0x3a02,0x02},
	{0x3a03,0xe4},
	{0x3a08,0x01},///
	{0x3a09,0xbc},////
	{0x3a0a,0x01},///
	{0x3a0b,0x72},////
	{0x3a0e,0x01},
	{0x3a0d,0x02},
	{0x3a14,0x02},
	{0x3a15,0xe4},
	
	{0x3820, 0x41}, //ddl@rock-chips.com add start: qsxvga -> 720p isn't stream on 
	{0x3821, 0x07},
	{0x3814, 0x31},
	{0x3815, 0x31},
	
	{0x3618, 0x00},
	{0x3612, 0x29},
	{0x3709, 0x52},
	{0x370c, 0x03},
	{0x3a02, 0x03},
	{0x3a03, 0xd8},
	{0x3a08 ,0x01},///
	{0x3a09, 0x27},///
	{0x3a0a, 0x00},///
	{0x3a0b, 0xf6},///
	{0x3a0e, 0x03},
	{0x3a0d, 0x04},
	{0x3a14, 0x03},
	{0x3a15, 0xd8},
	{0x4004, 0x02},
	{0x3002, 0x1c},////
	{0x4713, 0x03},//////ddl@rock-chips.com add end
	
	{0x3002,0x00},///
	{0x4713,0x02},///
	{0x4837,0x16},
	{0x3824,0x04},///
	{0x5001,0x83},
	{0x3035,0x21},
	{0x3036,0x46},
	
	{0x4837, 0x22},
	{0x5001, 0xa3},
	SensorEnd
};

/* 1920x1080 */
static struct rk_sensor_reg sensor_1080p[]={
	SensorEnd
};


static struct rk_sensor_reg sensor_softreset_data[]={
	SensorRegVal(0x3008, 0x80),
	SensorWaitMs(5),
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
	{0x3406 ,0x00},
	{0x5192 ,0x04},
	{0x5191 ,0xf8},
	{0x5193 ,0x70},
	{0x5194 ,0xf0},
	{0x5195 ,0xf0},
	{0x518d ,0x3d},
	{0x518f ,0x54},
	{0x518e ,0x3d},
	{0x5190 ,0x54},
	{0x518b ,0xa8},
	{0x518c ,0xa8},
	{0x5187 ,0x18},
	{0x5188 ,0x18},
	{0x5189 ,0x6e},
	{0x518a ,0x68},
	{0x5186 ,0x1c},
	{0x5181 ,0x50},
	{0x5184 ,0x25},
	{0x5182 ,0x11},
	{0x5183 ,0x14},
	{0x5184 ,0x25},
	{0x5185 ,0x24},
	SensorEnd
};
/* Cloudy Colour Temperature : 6500K - 8000K  */
static	struct rk_sensor_reg sensor_WhiteB_Cloudy[]=
{
	{0x3406 ,0x1 },
	{0x3400 ,0x6 },
	{0x3401 ,0x48},
	{0x3402 ,0x4 },
	{0x3403 ,0x0 },
	{0x3404 ,0x4 },
	{0x3405 ,0xd3 },
	SensorEnd
};
/* ClearDay Colour Temperature : 5000K - 6500K	*/
static	struct rk_sensor_reg sensor_WhiteB_ClearDay[]=
{
	{0x3406 ,0x1 },
	{0x3400 ,0x6 },
	{0x3401 ,0x1c},
	{0x3402 ,0x4 },
	{0x3403 ,0x0 },
	{0x3404 ,0x4 },
	{0x3405 ,0xf3},
	SensorEnd
};
/* Office Colour Temperature : 3500K - 5000K  */
static	struct rk_sensor_reg sensor_WhiteB_TungstenLamp1[]=
{
	//Office
	{0x3406 ,0x1 },
	{0x3400 ,0x5 },
	{0x3401 ,0x48},
	{0x3402 ,0x4 },
	{0x3403 ,0x0 },
	{0x3404 ,0x7 },
	{0x3405 ,0xcf},
	SensorEnd

};
/* Home Colour Temperature : 2500K - 3500K	*/
static	struct rk_sensor_reg sensor_WhiteB_TungstenLamp2[]=
{
	//Home
	{0x3406 ,0x1 },
	{0x3400 ,0x4 },
	{0x3401 ,0x10},
	{0x3402 ,0x4 },
	{0x3403 ,0x0 },
	{0x3404 ,0x8 },
	{0x3405 ,0xb6},
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
	{0x5001, 0x7f},
	{0x5580, 0x00},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_WandB[] =
{
	{0x5001, 0xff},
	{0x5580, 0x18},
	{0x5583, 0x80},
	{0x5584, 0x80},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_Sepia[] =
{
	{0x5001, 0xff},
	{0x5580, 0x18},
	{0x5583, 0x40},
	{0x5584, 0xa0},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_Negative[] =
{
	//Negative
	{0x5001, 0xff},
	{0x5580, 0x40},
	SensorEnd
};
static	struct rk_sensor_reg sensor_Effect_Bluish[] =
{
	// Bluish
	{0x5001, 0xff},
	{0x5580, 0x18},
	{0x5583, 0xa0},
	{0x5584, 0x40},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_Green[] =
{
	//	Greenish
	{0x5001, 0xff},
	{0x5580, 0x18},
	{0x5583, 0x60},
	{0x5584, 0x60},
	SensorEnd
};
static struct rk_sensor_reg *sensor_EffectSeqe[] = {sensor_Effect_Normal, sensor_Effect_WandB, sensor_Effect_Negative,sensor_Effect_Sepia,
	sensor_Effect_Bluish, sensor_Effect_Green,NULL,
};

static	struct rk_sensor_reg sensor_Exposure0[]=
{
	{0x3a0f, 0x10},
	{0x3a10, 0x08},
	{0x3a1b, 0x10},
	{0x3a1e, 0x08},
	{0x3a11, 0x20},
	{0x3a1f, 0x10},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure1[]=
{
	{0x3a0f, 0x20},
	{0x3a10, 0x18},
	{0x3a11, 0x41},
	{0x3a1b, 0x20},
	{0x3a1e, 0x18},
	{0x3a1f, 0x10},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure2[]=
{
	{0x3a0f, 0x30},
	{0x3a10, 0x28},
	{0x3a11, 0x61},
	{0x3a1b, 0x30},
	{0x3a1e, 0x28},
	{0x3a1f, 0x10},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure3[]=
{
	{0x3a0f, 0x38},
	{0x3a10, 0x30},
	{0x3a11, 0x61},
	{0x3a1b, 0x38},
	{0x3a1e, 0x30},
	{0x3a1f, 0x10},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure4[]=
{
	{0x3a0f, 0x40},
	{0x3a10, 0x38},
	{0x3a11, 0x71},
	{0x3a1b, 0x40},
	{0x3a1e, 0x38},
	{0x3a1f, 0x10},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure5[]=
{
	{0x3a0f, 0x50},
	{0x3a10, 0x48},
	{0x3a11, 0x90},
	{0x3a1b, 0x50},
	{0x3a1e, 0x48},
	{0x3a1f, 0x20},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure6[]=
{
	{0x3a0f, 0x60},
	{0x3a10, 0x58},
	{0x3a11, 0xa0},
	{0x3a1b, 0x60},
	{0x3a1e, 0x58},
	{0x3a1f, 0x20},
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
	//15fps ~ 3.75fps night mode for 60/50Hz light environment, 24Mhz clock input,24Mzh pclk
	{0x3034 ,0x1a},
	{0x3035 ,0x21},
	{0x3036 ,0x46},
	{0x3037 ,0x13},
	{0x3038 ,0x00},
	{0x3039 ,0x00},
	{0x3a00 ,0x7c},
	{0x3a08 ,0x01},
	{0x3a09 ,0x27},
	{0x3a0a ,0x00},
	{0x3a0b ,0xf6},
	{0x3a0d ,0x04},
	{0x3a0e ,0x04},
	{0x3a02 ,0x0b},
	{0x3a03 ,0x88},
	{0x3a14 ,0x0b},
	{0x3a15 ,0x88},
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
	{MEDIA_BUS_FMT_UYVY8_2X8, V4L2_COLORSPACE_JPEG}
};
/*static struct soc_camera_ops sensor_ops;*/


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
		struct generic_sensor*sensor = to_generic_sensor(client);
		struct specific_sensor *spsensor = to_specific_sensor(sensor);
		sensor_read(client,0x3a00, &ret_l);
		sensor_write(client,0x3a00, ret_l&0xfb);
	
		sensor_write(client,0x3503,0x07);	//stop AE/AG
		sensor_read(client,0x3406, &ret_l);
		sensor_write(client,0x3406, ret_l|0x01);
	
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
		
		SENSOR_DG(" %s Read 0x350b=0x%02x  PreviewExposure:%d 0x3500=0x%02x  0x3501=0x%02x 0x3502=0x%02x \n",
		 SENSOR_NAME_STRING(), tp_l,spsensor->parameter.preview_exposure,tp_h, tp_m, tp_l);
		return 0;
	}
#define OV5640_FULL_PERIOD_PIXEL_NUMS_HTS		  (2844) 
#define OV5640_FULL_PERIOD_LINE_NUMS_VTS		  (1968) 
#define OV5640_PV_PERIOD_PIXEL_NUMS_HTS 		  (1896) 
#define OV5640_PV_PERIOD_LINE_NUMS_VTS			  (984) 
	static int sensor_ae_transfer(struct i2c_client *client)
	{
		u8	ExposureLow;
		u8	ExposureMid;
		u8	ExposureHigh;
		u16 ulCapture_Exposure;
		u16 Preview_Maxlines;
		u8	Gain;
		u16 OV5640_g_iExtra_ExpLines;
		struct generic_sensor*sensor = to_generic_sensor(client);
		struct specific_sensor *spsensor = to_specific_sensor(sensor);
		//Preview_Maxlines = sensor->parameter.preview_line_width;
		Preview_Maxlines = spsensor->parameter.preview_maxlines;
		Gain = spsensor->parameter.preview_gain;
		
		
		ulCapture_Exposure = (spsensor->parameter.preview_exposure*OV5640_PV_PERIOD_PIXEL_NUMS_HTS)/OV5640_FULL_PERIOD_PIXEL_NUMS_HTS;
						
		SENSOR_DG("cap shutter calutaed = %d, 0x%x\n", ulCapture_Exposure,ulCapture_Exposure);
		
		// write the gain and exposure to 0x350* registers	
		sensor_write(client,0x350b, Gain);	
	
		if (ulCapture_Exposure <= 1940) {
			OV5640_g_iExtra_ExpLines = 0;
		}else {
			OV5640_g_iExtra_ExpLines = ulCapture_Exposure - 1940;
		}
		SENSOR_DG("Set Extra-line = %d, iExp = %d \n", OV5640_g_iExtra_ExpLines, ulCapture_Exposure);
	
		ExposureLow = (ulCapture_Exposure<<4)&0xff;
		ExposureMid = (ulCapture_Exposure>>4)&0xff;
		ExposureHigh = (ulCapture_Exposure>>12);
		
		sensor_write(client,0x350c, (OV5640_g_iExtra_ExpLines&0xff00)>>8);
		sensor_write(client,0x350d, OV5640_g_iExtra_ExpLines&0xff);
		sensor_write(client,0x3502, ExposureLow);
		sensor_write(client,0x3501, ExposureMid);
		sensor_write(client,0x3500, ExposureHigh);
	
		//SENSOR_DG(" %s Write 0x350b=0x%02x 0x350c=0x%2x  0x350d=0x%2x 0x3502=0x%02x 0x3501=0x%02x 0x3500=0x%02x\n",SENSOR_NAME_STRING(), Gain, ExposureLow, ExposureMid, ExposureHigh);
		mdelay(100);
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
	if (sensor->info_priv.funmodule_state & SENSOR_INIT_IS_OK) {
	    sensor_write(client, 0x3017, 0x00);  // FREX,VSYNC,HREF,PCLK,D9-D6
        sensor_write(client, 0x3018, 0x03);  // D5-D0
        sensor_write(client,0x3019,0x00);    // STROBE,SDA
    }
    
	return 0;
}
/*
* the function is called before sensor register setting in VIDIOC_S_FMT  
*/
static int sensor_s_fmt_cb_th(struct i2c_client *client,struct v4l2_mbus_framefmt *mf, bool capture)
{
	//struct generic_sensor*sensor = to_generic_sensor(client);
	
	if (capture) {
		sensor_parameter_record(client);
	}
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

/*
* the function is called after sensor register setting finished in VIDIOC_S_FMT  
*/
static int sensor_s_fmt_cb_bh (struct i2c_client *client,struct v4l2_mbus_framefmt *mf, bool capture)
{
	//struct generic_sensor*sensor = to_generic_sensor(client);
	if (capture) {
		sensor_ae_transfer(client);
	}
	msleep(400);
	return 0;
}
static int sensor_try_fmt_cb_th(struct i2c_client *client,struct v4l2_mbus_framefmt *mf)
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
	
	SENSOR_DG("mirror: %d",mirror);

	return 0;    
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
	SENSOR_DG("flip: %d",flip);

	return 0;    
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
/*
for 5640 focus
*/
struct af_cmdinfo
{
	char cmd_tag;
	char cmd_para[4];
	char validate_bit;
};
static int sensor_af_cmdset(struct i2c_client *client, int cmd_main, struct af_cmdinfo *cmdinfo)
{
	int i;
	char read_tag=0xff,cnt;

	if (cmdinfo) {
		for (i=0; i<4; i++) {
			if (cmdinfo->validate_bit & (1<<i)) {
				if (sensor_write(client, CMD_PARA0_Reg+i, cmdinfo->cmd_para[i])) {
					SENSOR_TR("%s write CMD_PARA_Reg(main:0x%x para%d:0x%x) error!\n",SENSOR_NAME_STRING(),cmd_main,i,cmdinfo->cmd_para[i]);
					goto sensor_af_cmdset_err;
				}
				SENSOR_DG("%s write CMD_PARA_Reg(main:0x%x para%d:0x%x) success!\n",SENSOR_NAME_STRING(),cmd_main,i,cmdinfo->cmd_para[i]);
			}
		}
	
		if (cmdinfo->validate_bit & 0x80) {
			if (sensor_write(client, CMD_ACK_Reg, cmdinfo->cmd_tag)) {
				SENSOR_TR("%s write CMD_ACK_Reg(main:0x%x tag:0x%x) error!\n",SENSOR_NAME_STRING(),cmd_main,cmdinfo->cmd_tag);
				goto sensor_af_cmdset_err;
			}
			SENSOR_DG("%s write CMD_ACK_Reg(main:0x%x tag:0x%x) success!\n",SENSOR_NAME_STRING(),cmd_main,cmdinfo->cmd_tag);
		}
		
	} else {
		if (sensor_write(client, CMD_ACK_Reg, 0x01)) {
			SENSOR_TR("%s write CMD_ACK_Reg(main:0x%x no tag) error!\n",SENSOR_NAME_STRING(),cmd_main);
			goto sensor_af_cmdset_err;
		}
		SENSOR_DG("%s write CMD_ACK_Reg(main:0x%x no tag) success!\n",SENSOR_NAME_STRING(),cmd_main);
	}

	if (sensor_write(client, CMD_MAIN_Reg, cmd_main)) {
		SENSOR_TR("%s write CMD_MAIN_Reg(main:0x%x) error!\n",SENSOR_NAME_STRING(),cmd_main);
		goto sensor_af_cmdset_err;
	}

	cnt = 0;
	do
	{
		msleep(5);
		if (sensor_read(client,CMD_ACK_Reg,&read_tag)){
		   SENSOR_TR("%s[%d] read TAG failed\n",SENSOR_NAME_STRING(),__LINE__);
		   break;
		}
	} while((read_tag != 0x00)&& (cnt++<100));

	SENSOR_DG("%s write CMD_MAIN_Reg(main:0x%x read tag:0x%x) success!\n",SENSOR_NAME_STRING(),cmd_main,read_tag);
	return 0;
sensor_af_cmdset_err:
	return -1;
}
static int sensor_af_idlechk(struct i2c_client *client)
{
	int ret = 0;
	char state; 
	struct af_cmdinfo cmdinfo;
	
	SENSOR_DG("%s , %d\n",__FUNCTION__,__LINE__);
	
	cmdinfo.cmd_tag = 0x01;
	cmdinfo.validate_bit = 0x80;
	ret = sensor_af_cmdset(client, ReturnIdle_Cmd, &cmdinfo);
	if(0 != ret) {
		SENSOR_TR("%s[%d] read focus_status failed\n",SENSOR_NAME_STRING(),__LINE__);
		ret = -1;
		goto sensor_af_idlechk_end;
	}
	

	do{
		ret = sensor_read(client, CMD_ACK_Reg, &state);
		if (ret != 0){
		   SENSOR_TR("%s[%d] read focus_status failed\n",SENSOR_NAME_STRING(),__LINE__);
		   ret = -1;
		   goto sensor_af_idlechk_end;
		}
	}while(0x00 != state);

	SENSOR_DG("%s , %d\n",__FUNCTION__,__LINE__);
sensor_af_idlechk_end:
	return ret;
}
/*for 5640 focus end*/
//
static int sensor_focus_af_single_usr_cb(struct i2c_client *client);

static int sensor_focus_init_usr_cb(struct i2c_client *client)
{
    int ret = 0, cnt;
    char state;

    msleep(1);
    ret = sensor_write_array(client, sensor_af_firmware);
    if (ret != 0) {
    	SENSOR_TR("%s Download firmware failed\n",SENSOR_NAME_STRING());
    	ret = -1;
    	goto sensor_af_init_end;
    }

    cnt = 0;
    do
    {
    	msleep(1);
    	if (cnt++ > 500)
    		break;
    	ret = sensor_read(client, STA_FOCUS_Reg, &state);
    	if (ret != 0){
    	   SENSOR_TR("%s[%d] read focus_status failed\n",SENSOR_NAME_STRING(),__LINE__);
    	   ret = -1;
    	   goto sensor_af_init_end;
    	}
    } while (state != S_IDLE);

    if (state != S_IDLE) {
    	SENSOR_TR("%s focus state(0x%x) is error!\n",SENSOR_NAME_STRING(),state);
    	ret = -1;
    	goto sensor_af_init_end;
    }
sensor_af_init_end:
    SENSOR_DG("%s %s ret:0x%x \n",SENSOR_NAME_STRING(),__FUNCTION__,ret);
    return ret;
}

static int sensor_focus_af_single_usr_cb(struct i2c_client *client) 
{
	int ret = 0;
	char state,cnt;
	struct af_cmdinfo cmdinfo;
	//char s_zone[5],i;
	cmdinfo.cmd_tag = 0x01;
	cmdinfo.validate_bit = 0x80;
	ret = sensor_af_cmdset(client, SingleFocus_Cmd, &cmdinfo);
	if(0 != ret) {
		SENSOR_TR("%s single focus mode set error!\n",SENSOR_NAME_STRING());
		ret = -1;
		goto sensor_af_single_end;
	}
	
	cnt = 0;
	do
	{
		if (cnt != 0) {
			msleep(1);
		}
		cnt++;
		ret = sensor_read(client, STA_FOCUS_Reg, &state);
		if (ret != 0){
		   SENSOR_TR("%s[%d] read focus_status failed\n",SENSOR_NAME_STRING(),__LINE__);
		   ret = -1;
		   goto sensor_af_single_end;
		}
	}while((state == S_FOCUSING) && (cnt<100));

	if (state != S_FOCUSED) {
		SENSOR_TR("%s[%d] focus state(0x%x) is error!\n",SENSOR_NAME_STRING(),__LINE__,state);
		ret = -1;
		goto sensor_af_single_end;
	} else {
		SENSOR_DG("%s[%d] single focus mode set success!\n",SENSOR_NAME_STRING(),__LINE__);    
	}
sensor_af_single_end:
	return ret;
}

static int sensor_focus_af_near_usr_cb(struct i2c_client *client)
{
	return 0;
}

static int sensor_focus_af_far_usr_cb(struct i2c_client *client)
{
	
	return 0;
}

static int sensor_focus_af_specialpos_usr_cb(struct i2c_client *client,int pos)
{
	struct af_cmdinfo cmdinfo;
	sensor_af_idlechk(client);
	return 0;
	cmdinfo.cmd_tag = StepFocus_Spec_Tag;
	cmdinfo.cmd_para[0] = pos;
	cmdinfo.validate_bit = 0x81;
	return sensor_af_cmdset(client, StepMode_Cmd, &cmdinfo);
}

static int sensor_focus_af_const_usr_cb(struct i2c_client *client)
{
	int ret = 0;
	struct af_cmdinfo cmdinfo;
	cmdinfo.cmd_tag = 0x01;
	cmdinfo.cmd_para[0] = 0x00;
	cmdinfo.validate_bit = 0x81;
	sensor_af_idlechk(client);
	if (sensor_af_cmdset(client, ConstFocus_Cmd, &cmdinfo)) {
		SENSOR_TR("%s[%d] const focus mode set error!\n",SENSOR_NAME_STRING(),__LINE__);
		ret = -1;
		goto sensor_af_const_end;
	} else {
		SENSOR_DG("%s[%d] const focus mode set success!\n",SENSOR_NAME_STRING(),__LINE__);	  
	}
sensor_af_const_end:
	return ret;
}
static int sensor_focus_af_const_pause_usr_cb(struct i2c_client *client)
{
    return 0;
}
static int sensor_focus_af_close_usr_cb(struct i2c_client *client)
{
	int ret = 0; 
	sensor_af_idlechk(client);
	if (sensor_af_cmdset(client, PauseFocus_Cmd, NULL)) {
		SENSOR_TR("%s pause focus mode set error!\n",SENSOR_NAME_STRING());
		ret = -1;
		goto sensor_af_pause_end;
	}
sensor_af_pause_end:
	return ret;
}

static int sensor_focus_af_zoneupdate_usr_cb(struct i2c_client *client, int *zone_tm_pos)
{
	int ret = 0;
	struct af_cmdinfo cmdinfo;
	//int zone_tm_pos[4];
	int zone_center_pos[2];
	//struct generic_sensor*sensor = to_generic_sensor(client);    
	
	if (zone_tm_pos) {
		zone_tm_pos[0] += 1000;
		zone_tm_pos[1] += 1000;
		zone_tm_pos[2]+= 1000;
		zone_tm_pos[3] += 1000;
		zone_center_pos[0] = ((zone_tm_pos[0] + zone_tm_pos[2])>>1)*80/2000;
		zone_center_pos[1] = ((zone_tm_pos[1] + zone_tm_pos[3])>>1)*60/2000;
	} else {
#if CONFIG_SENSOR_FocusCenterInCapture
		zone_center_pos[0] = 32;
		zone_center_pos[1] = 24;
#else
		zone_center_pos[0] = -1;
		zone_center_pos[1] = -1;
#endif
	}
	if ((zone_center_pos[0] >=0) && (zone_center_pos[1]>=0)){
		cmdinfo.cmd_tag = 0x01;
		cmdinfo.validate_bit = 0x83;
		if (zone_center_pos[0]<=8)
			cmdinfo.cmd_para[0] = 0;
		else if ((zone_center_pos[0]>8) && (zone_center_pos[0]<72))
			cmdinfo.cmd_para[0] = zone_center_pos[0]-8;
		else 
			cmdinfo.cmd_para[0] = 72; 
		
		if (zone_center_pos[1]<=6)
			cmdinfo.cmd_para[1] = 0;
		else if ((zone_center_pos[1]>6) && (zone_center_pos[1]<54))
			cmdinfo.cmd_para[1] = zone_center_pos[1]-6;
		else 
			cmdinfo.cmd_para[1] = 54;
		
		ret = sensor_af_cmdset(client, TouchZoneConfig_Cmd, &cmdinfo);
		if(0 != ret) {
			SENSOR_TR("%s touch zone config error!\n",SENSOR_NAME_STRING());
			ret = -1;
			goto sensor_af_zone_end;
		}  
	}
sensor_af_zone_end:
	return ret;
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

 

