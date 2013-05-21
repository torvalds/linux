/*
o* Driver for MT9M001 CMOS Image Sensor from Micron
 *
 * Copyright (C) 2008, Guennadi Liakhovetski <kernel@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/videodev2.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/log2.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/circ_buf.h>
#include <linux/miscdevice.h>
#include <media/v4l2-common.h>
#include <media/v4l2-chip-ident.h>
#include <media/soc_camera.h>
#include <plat/rk_camera.h>



static int debug;



module_param(debug, int, S_IRUGO|S_IWUSR);

#define dprintk(level, fmt, ...) do {			\
	if (debug >= level) 					\
	printk(KERN_WARNING fmt , ## __VA_ARGS__); } while (0)

#define SENSOR_TR(format, ...) printk(KERN_ERR format, ## __VA_ARGS__)
#define SENSOR_DG(format, ...) dprintk(1, format, ## __VA_ARGS__)


#define _CONS(a,b) a##b
#define CONS(a,b) _CONS(a,b)

#define __STR(x) #x
#define _STR(x) __STR(x)
#define STR(x) _STR(x)

#define MIN(x,y)   ((x<y) ? x: y)
#define MAX(x,y)    ((x>y) ? x: y)

/* Sensor Driver Configuration */
#define SENSOR_NAME ov3660
#define SENSOR_V4L2_IDENT  V4L2_IDENT_OV3660
#define SENSOR_ID 0x3660
#define SENSOR_MIN_WIDTH    800
#define SENSOR_MIN_HEIGHT   600
#define SENSOR_MAX_WIDTH    2048
#define SENSOR_MAX_HEIGHT   1536
#define SENSOR_INIT_WIDTH	800			/* Sensor pixel size for sensor_init_data array */
#define SENSOR_INIT_HEIGHT  600
#define SENSOR_INIT_WINSEQADR sensor_svga
#define SENSOR_INIT_PIXFMT V4L2_MBUS_FMT_YUYV8_2X8

#define CONFIG_SENSOR_WhiteBalance	1
#define CONFIG_SENSOR_Brightness	0
#define CONFIG_SENSOR_Contrast      0
#define CONFIG_SENSOR_Saturation    0
#define CONFIG_SENSOR_Effect        1
#define CONFIG_SENSOR_Scene         1
#define CONFIG_SENSOR_DigitalZoom   0
#define CONFIG_SENSOR_Focus         0
#define CONFIG_SENSOR_Exposure      0
#define CONFIG_SENSOR_Flash         1
#define CONFIG_SENSOR_Mirror        0
#define CONFIG_SENSOR_Flip          0

#define CONFIG_SENSOR_I2C_SPEED     350000       /* Hz */
/* Sensor write register continues by preempt_disable/preempt_enable for current process not be scheduled */
#define CONFIG_SENSOR_I2C_NOSCHED   0
#define CONFIG_SENSOR_I2C_RDWRCHK   0

#define SENSOR_BUS_PARAM  (SOCAM_MASTER | SOCAM_PCLK_SAMPLE_RISING|\
                          SOCAM_HSYNC_ACTIVE_HIGH | SOCAM_VSYNC_ACTIVE_LOW |\
                          SOCAM_DATA_ACTIVE_HIGH | SOCAM_DATAWIDTH_8  |SOCAM_MCLK_24MHZ)

#define COLOR_TEMPERATURE_CLOUDY_DN  6500
#define COLOR_TEMPERATURE_CLOUDY_UP    8000
#define COLOR_TEMPERATURE_CLEARDAY_DN  5000
#define COLOR_TEMPERATURE_CLEARDAY_UP    6500
#define COLOR_TEMPERATURE_OFFICE_DN     3500
#define COLOR_TEMPERATURE_OFFICE_UP     5000
#define COLOR_TEMPERATURE_HOME_DN       2500
#define COLOR_TEMPERATURE_HOME_UP       3500

#define SENSOR_NAME_STRING(a) STR(CONS(SENSOR_NAME, a))
#define SENSOR_NAME_VARFUN(a) CONS(SENSOR_NAME, a)

#define SENSOR_AF_IS_ERR    (0x00<<0)
#define SENSOR_AF_IS_OK		(0x01<<0)
#define SENSOR_INIT_IS_ERR   (0x00<<28)
#define SENSOR_INIT_IS_OK    (0x01<<28)

struct reginfo
{
    u16 reg;
    u8 val;
};

//flash off in fixed time to prevent from too hot , zyc
struct  flash_timer{
    struct soc_camera_device *icd;
    struct hrtimer timer;
};
static enum hrtimer_restart flash_off_func(struct hrtimer *timer);

static struct  flash_timer flash_off_timer;
//for user defined if user want to customize the series , zyc
/* init 800*600 SVGA */
static struct reginfo sensor_init_data[] =
{
	//Jacky_M
	{0x3008,0x82},
	{0x3103,0x13},
	{0x3008,0x42},
	{0x3017,0xff},
	{0x3018,0xff},
	//{0x302c,0x43},
	{0x3032,0x00},
	{0x3901,0x13},
	{0x3704,0x80},
	{0x3717,0x00},
	{0x371b,0x60},
	{0x370b,0x10},
	{0x3624,0x03},
	{0x3622,0x80},
	{0x3614,0x80},
	{0x3630,0x52},
	{0x3632,0x07},
	{0x3633,0xd2},
	{0x3619,0x75},
	{0x371c,0x00},
	{0x370b,0x12},
	{0x3704,0x80},
	{0x3600,0x08},
	{0x3620,0x43},
	{0x3702,0x20},
	{0x3739,0x48},
	{0x3730,0x20},
	{0x370c,0x0c},
	{0x3a18,0x00},
	{0x3a19,0xf8},
	{0x3000,0x10},
	{0x3004,0xef},
	{0x6700,0x05},
	{0x6701,0x19},
	{0x6702,0xfd},
	{0x6703,0xd1},
	{0x6704,0xff},
	{0x6705,0xff},
	{0x3002,0x1c},
	{0x3006,0xc3},
	{0x3800,0x00},
	{0x3801,0x00},
	{0x3802,0x00},
	{0x3803,0x00},
	{0x3804,0x08},
	{0x3805,0x1f},
	{0x3806,0x06},
	{0x3807,0x07},
	{0x3808,0x04},
	{0x3809,0x00},
	{0x380a,0x03},
	{0x380b,0x00},
	{0x380c,0x08},
	{0x380d,0xfc},
	{0x380e,0x03},
	{0x380f,0x14},
	{0x3810,0x00},
	{0x3811,0x08},
	{0x3812,0x00},
	{0x3813,0x02},
	{0x3814,0x31},
	{0x3815,0x31},
	{0x3820,0x01},
	{0x3821,0x01},
	{0x3824,0x01},
	{0x3826,0x23},
	{0x3a02,0x30},
	{0x3a03,0xc0},
	{0x3a08,0x00},
	{0x3a09,0xec},
	{0x3a0a,0x00},
	{0x3a0b,0xc5},
	{0x3a0d,0x04},
	{0x3a0e,0x03},
	{0x3a14,0x30},
	{0x3a15,0x72},
	{0x3618,0x00},
	{0x3623,0x00},
	{0x3708,0x64},
	{0x3709,0x52},
	{0x4300,0x30},//0x32},//0x31},//0x30},
	{0x440e,0x09},
	{0x4514,0xaa},
	{0x4520,0x0b},
	{0x460b,0x37},
	{0x460c,0x20},
	{0x4713,0x02},
	{0x471c,0xd0},
	{0x5086,0x00},
	{0x5000,0x07},
	{0x5001,0x03},
	{0x5002,0x00},
	{0x501f,0x00},
	{0x3008,0x02},
	{0x302c,0x43},  
	{0x3a00,0x38},
	{0x303c,0x12},  //0x15//jack change to 0x15
	{0x440e,0x08}, 
	
	{0x5180,0xff},
	{0x5181,0xf2},
	{0x5182,0x0 },
	{0x5183,0x14},
	{0x5184,0x25},
	{0x5185,0x24},
	{0x5186,0x9 },
	{0x5187,0x9 },
	{0x5188,0x9 },
	{0x5189,0x75},
	{0x518a,0x54},
	{0x518b,0xe0},
	{0x518c,0xb2},
	{0x518d,0x42},
	{0x518e,0x3d},
	{0x518f,0x56},
	{0x5190,0x46},
	{0x5191,0xf8},
	{0x5192,0x4 },
	{0x5193,0x70},
	{0x5194,0xf0},
	{0x5195,0xf0},
	{0x5196,0x3 },
	{0x5197,0x1 },
	{0x5198,0x4 },
	{0x5199,0x12},
	{0x519a,0x4 },
	{0x519b,0x0 },
	{0x519c,0x6 },
	{0x519d,0x82},
	{0x519e,0x38},
	{0x5381,0x1c},  
	{0x5382,0x5a},
	{0x5383,0x6 },
	{0x5384,0xa },
	{0x5385,0x7e},
	{0x5386,0x88},
	{0x5387,0x7c},
	{0x5388,0x6c},
	{0x5389,0x10},
	{0x538a,0x1 },
	{0x538b,0x98},
	{0x5000,0x27}, 
	{0x5481,0x8 },
	{0x5482,0x14},
	{0x5483,0x28},
	{0x5484,0x51},
	{0x5485,0x65},
	{0x5486,0x71},
	{0x5487,0x7d},
	{0x5488,0x87},
	{0x5489,0x91},
	{0x548a,0x9a},
	{0x548b,0xaa},
	{0x548c,0xb8},
	{0x548d,0xcd},
	{0x548e,0xdd},
	{0x548f,0xea},
	{0x5490,0x1d},
	{0x3821,0x06},
	{0x4514,0xbb},
	{0x3a0f,0x38},
	{0x3a10,0x30},
	{0x3a1b,0x38},
	{0x3a1e,0x30},
	{0x3a11,0x70},
	{0x3a1f,0x14},
	{0x3a18,0x0 },
	{0x3808,0x03},
	{0x3809,0x20},
	{0x380a,0x02},
	{0x380b,0x58},

//  preview vga
{0x303c, 0x12},
{0x3824, 0x02},
{0x460c, 0x22},	
//XGA Windowing
{0x5001, 0x83},
{0x3800, 0x00},
{0x3801, 0x00},
{0x3802, 0x00},
{0x3803, 0x00},
{0x3804, 0x08},
{0x3805, 0x1f},
{0x3806, 0x06},
{0x3807, 0x09},
{0x3808, 0x04},
{0x3809, 0x00},
{0x380a, 0x03},
{0x380b, 0x00},
{0x380c, 0x08},
{0x380d, 0xfc},
{0x380e, 0x03},
{0x380f, 0x14},
{0x3810, 0x00},
{0x3811, 0x08},
{0x3812, 0x00},
{0x3813, 0x02},
{0x3814, 0x31},
{0x3815, 0x31},
	
{0x3708, 0x66},
{0x3709, 0x12},
{0x370c, 0x0c},
{0x4520, 0x0b},
{0x302c, 0x03},

//Bining mode
{0x3820, 0x01},
{0x3821, 0x03},//0x01
{0x4514, 0xaa},	

//vga	
{0x5001, 0xa3},
{0x3808, 0x02},
{0x3809, 0x80},
{0x380a, 0x01},
{0x380b, 0xe0},
{0x380c, 0x08},
{0x380d, 0xfc},
{0x380e, 0x03},
{0x380f, 0x14},
{0x3810, 0x00},
{0x3811, 0x08},
{0x3812, 0x00},
{0x3813, 0x02},
{0x3814, 0x31},
{0x3815, 0x31},


	{0x5001,0xa7},
	//ISP
	{0x5000, 0xa7},
{0x5001, 0x03},
//OV3660 LENC setting
{0x5800, 0x23},
{0x5801, 0x10},
{0x5802, 0xe },
{0x5803, 0xe },
{0x5804, 0x11},
{0x5805, 0x1e},
{0x5806, 0xa },
{0x5807, 0x7 },
{0x5808, 0x5 },
{0x5809, 0x5 },
{0x580a, 0x7 },
{0x580b, 0xc },
{0x580c, 0x7 },
{0x580d, 0x3 },
{0x580e, 0x0 },
{0x580f, 0x0 },
{0x5810, 0x3 },
{0x5811, 0x8 },
{0x5812, 0x8 },
{0x5813, 0x3 },
{0x5814, 0x0 },
{0x5815, 0x0 },
{0x5816, 0x3 },
{0x5817, 0x9 },
{0x5818, 0xb },
{0x5819, 0x7 },
{0x581a, 0x5 },
{0x581b, 0x5 },
{0x581c, 0x8 },
{0x581d, 0xc },
{0x581e, 0x1d},
{0x581f, 0x10},
{0x5820, 0xf },
{0x5821, 0xf },
{0x5822, 0x11},
{0x5823, 0x1e},
{0x5824, 0x45},
{0x5825, 0x29},
{0x5826, 0x1b},
{0x5827, 0x29},
{0x5828, 0x44},
{0x5829, 0x1b},
{0x582a, 0x28},
{0x582b, 0x36},
{0x582c, 0x27},
{0x582d, 0x1a},
{0x582e, 0x1b},
{0x582f, 0x33},
{0x5830, 0x50},
{0x5831, 0x43},
{0x5832, 0x19},
{0x5833, 0x1b},
{0x5834, 0x27},
{0x5835, 0x34},
{0x5836, 0x37},
{0x5837, 0x1a},
{0x5838, 0x47},
{0x5839, 0x19},
{0x583a, 0xa },
{0x583b, 0x19},
{0x583c, 0x35},
{0x583d, 0xce},
	
	
	{0x5180,0xff},//	 //;IQ setting example //;Sample_AWB
	{0x5181,0xf2},//
	{0x5182,0x0 },//
	{0x5183,0x14},//
	{0x5184,0x25},//
	{0x5185,0x24},//
	{0x5186,0x0d},//
	{0x5187,0x0f},//
	{0x5188,0x0f},//
	{0x5189,0x75},//
	{0x518a,0x54},//
	{0x518b,0xe0},//
	{0x518c,0xb2},//
	{0x518d,0x3e},//
	{0x518e,0x39},//
	{0x518f,0x52},//
	{0x5190,0x47},//
	{0x5191,0xf8},//
	{0x5192,0x4 },//
	{0x5193,0x70},//
	{0x5194,0xf0},//
	{0x5195,0xf0},//
	{0x5196,0x3 },//
	{0x5197,0x1 },//
	{0x5198,0x4 },//
	{0x5199,0x68},//
	{0x519a,0x4 },//
	{0x519b,0x0 },//
	{0x519c,0x07 },//
	{0x519d,0xa5},//
	{0x519e,0x38},//
	{0x5381,0x1c},//	 //;Sample_CMX
	{0x5382,0x5a},//
	{0x5383,0x6 },//
	{0x5384,0x09 },//
	{0x5385,0x71},//
	{0x5386,0x7a},//
	{0x5387,0x70},//
	{0x5388,0x61},//
	{0x5389,0x0e},//

	{0x538b,0x98},//
	{0x538a,0x1 },//
 
	{0x3a0f,0x38},//
	{0x3a10,0x30},//
	{0x3a1b,0x38},//
	{0x3a1e,0x30},//
	{0x3a11,0x70},//
	{0x3a1f,0x14},//
//	{0x3a18,0x0 },//
{0x5000,0xa7},
	{0x3503,0x00},
	{0x0000, 0x00}
};

/* 1280x720 */
static struct reginfo sensor_720p[]=
  {
 
	{0x3008,0x82},//
	{0x3103,0x13},//
	{0x3008,0x42},//
	{0x3017,0xff},//
	{0x3018,0xff},//
	{0x302c,0x43},//
	{0x3032,0x00},//
	{0x3901,0x13},//
	{0x3704,0x80},//
	{0x3717,0x00},//
	{0x371b,0x60},//
	{0x370b,0x10},//
	{0x3624,0x03},//
	{0x3622,0x80},//
	{0x3614,0x80},//
	{0x3630,0x52},//
	{0x3632,0x07},//
	{0x3633,0xd2},//
	{0x3619,0x75},//
	{0x371c,0x00},//
	{0x370b,0x12},//
	{0x3704,0x80},//
	{0x3600,0x08},//
	{0x3620,0x43},//
	{0x3702,0x20},//
	{0x3739,0x48},//
	{0x3730,0x20},//
	{0x370c,0x0c},//
	{0x3a18,0x00},//
	{0x3a19,0xf8},//
	{0x3000,0x10},//
	{0x3004,0xef},//
	{0x6700,0x05},//
	{0x6701,0x19},//
	{0x6702,0xfd},//
	{0x6703,0xd1},//
	{0x6704,0xff},//
	{0x6705,0xff},//
	{0x3002,0x1c},//
	{0x3006,0xc3},//
	{0x3800,0x01},//
	{0x3801,0x80},//
	{0x3802,0x01},//
	{0x3803,0x98},//
	{0x3804,0x06},//
	{0x3805,0x9f},//
	{0x3806,0x04},//
	{0x3807,0x73},//
	{0x3808,0x05},//
	{0x3809,0x00},//
	{0x380a,0x02},//
	{0x380b,0xd0},//
	{0x380c,0x08},//
	{0x380d,0xfc},//
	{0x380e,0x02},//
	{0x380f,0xec},//
	{0x3810,0x00},//
	{0x3811,0x10},//
	{0x3812,0x00},//
	{0x3813,0x06},//
	{0x3814,0x11},//
	{0x3815,0x11},//
	{0x3820,0x40},//
	{0x3821,0x00},//
	{0x3824,0x01},//
	{0x3826,0x23},//
	{0x3a02,0x30},//
	{0x3a03,0xc0},//
	{0x3a08,0x00},//
	{0x3a09,0xe0},//
	{0x3a0a,0x00},//
	{0x3a0b,0xbb},//
	{0x3a0d,0x04},//
	{0x3a0e,0x03},//
	{0x3a14,0x30},//
	{0x3a15,0x72},//
	{0x3618,0x78},//
	{0x3623,0x00},//
	{0x3708,0x21},//
	{0x3709,0x12},//
	{0x4300,0x30},//
	{0x440e,0x09},//
	{0x4514,0x00},//
	{0x4520,0xb0},//
	{0x460b,0x37},//
	{0x460c,0x20},//
	{0x4713,0x02},//
	{0x471c,0xd0},//
	{0x5086,0x00},//
	{0x5000,0x07},//
	{0x5001,0x03},//
	{0x5002,0x00},//
	{0x501f,0x00},//
	{0x302c,0x43},//  
	{0x3a00,0x38},//
	{0x303c,0x14},//  ;15fps
	{0x440e,0x08},//  ;for MBIST
	{0x3821,0x06},//  ;mirror on
	{0x3008,0x02},//
	//ISP
	{0x5180,0xff},//	 //;IQ setting example //;Sample_AWB
	{0x5181,0xf2},//
	{0x5182,0x0 },//
	{0x5183,0x14},//
	{0x5184,0x25},//
	{0x5185,0x24},//
	{0x5186,0x9 },//
	{0x5187,0x9 },//
	{0x5188,0x9 },//
	{0x5189,0x75},//
	{0x518a,0x54},//
	{0x518b,0xe0},//
	{0x518c,0xb2},//
	{0x518d,0x42},//
	{0x518e,0x3d},//
	{0x518f,0x56},//
	{0x5190,0x46},//
	{0x5191,0xf8},//
	{0x5192,0x4 },//
	{0x5193,0x70},//
	{0x5194,0xf0},//
	{0x5195,0xf0},//
	{0x5196,0x3 },//
	{0x5197,0x1 },//
	{0x5198,0x4 },//
	{0x5199,0x12},//
	{0x519a,0x4 },//
	{0x519b,0x0 },//
	{0x519c,0x6 },//
	{0x519d,0x82},//
	{0x519e,0x38},//
	{0x5381,0x1c},//	 //;Sample_CMX
	{0x5382,0x5a},//
	{0x5383,0x6 },//
	{0x5384,0xa },//
	{0x5385,0x7e},//
	{0x5386,0x88},//
	{0x5387,0x7c},//
	{0x5388,0x6c},//
	{0x5389,0x10},//
	{0x538a,0x1 },//
	{0x538b,0x98},//
	{0x5000,0x27},// //;Sample_gamma;enable gamma
	{0x5481,0x8 },//
	{0x5482,0x14},//
	{0x5483,0x28},//
	{0x5484,0x51},//
	{0x5485,0x65},//
	{0x5486,0x71},//
	{0x5487,0x7d},//
	{0x5488,0x87},//
	{0x5489,0x91},//
	{0x548a,0x9a},//
	{0x548b,0xaa},//
	{0x548c,0xb8},//
	{0x548d,0xcd},//
	{0x548e,0xdd},//
	{0x548f,0xea},//
	{0x5490,0x1d},//
	{0x3a0f,0x38},//	//;Example_EV
	{0x3a10,0x30},//
	{0x3a1b,0x38},//
	{0x3a1e,0x30},//
	{0x3a11,0x70},//
	{0x3a1f,0x14},//
	{0x3a18,0x0 },//

	{0x0000, 0x00}
};
/* 2048*1536 QXGA */
static struct reginfo sensor_qxga[] =
{

{0x303b,0x1b},
{0x303c,0x13},// ;13
{0x303d,0x30},

{0x3108,0x16},

{0x3824,0x01},
{0x460C,0x20},

{0x5001,0x03},
{0x3800,0x00},
{0x3801,0x00},
{0x3802,0x00},
{0x3803,0x00},
{0x3804,0x08},
{0x3805,0x1f},
{0x3806,0x06},
{0x3807,0x0b},
{0x3808,0x08},
{0x3809,0x00},
{0x380a,0x06},
{0x380b,0x00},
{0x380c,0x08},
{0x380d,0xfc},
{0x380e,0x06},
{0x380f,0x1c},
{0x3810,0x00},
{0x3811,0x10},
{0x3812,0x00},
{0x3813,0x06},
{0x3814,0x11},
{0x3815,0x11},

{0x3708,0x63},
{0x3709,0x12},
{0x370c,0x0c},
{0x4520,0xb0},
{0x302c,0x03},

	
{0x3820,0x10},//0x40
{0x3821,0x06},//0x00
{0x4514,0x00},
{0x5000,0xa7},
{0x0000, 0x00}
 
};

/* 1600X1200 UXGA */
static struct reginfo sensor_uxga[] =
 {
 
	{0x0000, 0x00}
};

/* 1280X1024 SXGA */
static struct reginfo sensor_sxga[] =
{
	{0x0, 0x0}
};
/* 1024X768 SXGA */
static struct reginfo sensor_xga[] =
{
	{0x0, 0x0}
};
/* 800X600 SVGA*/
static struct reginfo sensor_svga[] =
{

	{0x3008,0x82},//
	{0x3103,0x13},//
	{0x3008,0x42},//
	{0x3017,0xff},//
	{0x3018,0xff},//
	{0x302c,0x43},//
	{0x3032,0x00},//
	{0x3901,0x13},//
	{0x3704,0x80},//
	{0x3717,0x00},//
	{0x371b,0x60},//
	{0x370b,0x10},//
	{0x3624,0x03},//
	{0x3622,0x80},//
	{0x3614,0x80},//
	{0x3630,0x52},//
	{0x3632,0x07},//
	{0x3633,0xd2},//
	{0x3619,0x75},//
	{0x371c,0x00},//
	{0x370b,0x12},//
	{0x3704,0x80},//
	{0x3600,0x08},//
	{0x3620,0x43},//
	{0x3702,0x20},//
	{0x3739,0x48},//
	{0x3730,0x20},//
	{0x370c,0x0c},//
	{0x3a18,0x00},//
	{0x3a19,0xf8},//
	{0x3000,0x10},//
	{0x3004,0xef},//
	{0x6700,0x05},//
	{0x6701,0x19},//
	{0x6702,0xfd},//
	{0x6703,0xd1},//
	{0x6704,0xff},//
	{0x6705,0xff},//
	{0x3002,0x1c},//
	{0x3006,0xc3},//
	{0x3800,0x00},//
	{0x3801,0x00},//
	{0x3802,0x00},//
	{0x3803,0x00},//
	{0x3804,0x08},//
	{0x3805,0x1f},//
	{0x3806,0x06},//
	{0x3807,0x0b},//
	{0x3808,0x08},//
	{0x3809,0x00},//
	{0x380a,0x06},//
	{0x380b,0x00},//
	{0x380c,0x08},//
	{0x380d,0xfc},//
	{0x380e,0x06},//
	{0x380f,0x1c},//
	{0x3810,0x00},//
	{0x3811,0x10},//
	{0x3812,0x00},//
	{0x3813,0x06},//
	{0x3814,0x11},//
	{0x3815,0x11},//
	{0x3820,0x40},//
	{0x3821,0x00},//
	{0x3824,0x01},//
	{0x3826,0x23},//
	{0x3a02,0x30},//
	{0x3a03,0xc0},//
	{0x3a08,0x00},//
	{0x3a09,0xea},//
	{0x3a0a,0x00},//
	{0x3a0b,0xc3},//
	{0x3a0d,0x08},//
	{0x3a0e,0x06},//
	{0x3a14,0x30},//
	{0x3a15,0x72},//
	{0x3618,0x78},//
	{0x3623,0x00},//
	{0x3708,0x21},//
	{0x3709,0x12},//
	{0x4300,0x30},//
	{0x440e,0x09},//
	{0x4514,0x00},//
	{0x4520,0xb0},//
	{0x460b,0x37},//
	{0x460c,0x20},//
	{0x4713,0x02},//
	{0x471c,0xd0},//
	{0x5086,0x00},//
	{0x5000,0x07},//
	{0x5001,0x03},//
	{0x5002,0x00},//
	{0x501f,0x00},//
	{0x302c,0x43},//  
	{0x3a00,0x38},//
	{0x303c,0x14},//  ;5fps
	{0x440e,0x08},//  ;for MBIST
	{0x3821,0x06},//  ;mirror on
	{0x3008,0x02},//
	{0x5180,0x00},//     //;IQ setting example //;Sample_AWB
	{0x5181,0xf2},//
	{0x5182,0x0 },//
	{0x5183,0x14},//
	{0x5184,0x25},//
	{0x5185,0x24},//
	{0x5186,0x9 },//
	{0x5187,0x9 },//
	{0x5188,0x9 },//
	{0x5189,0x75},//
	{0x518a,0x54},//
	{0x518b,0xe0},//
	{0x518c,0xb2},//
	{0x518d,0x42},//
	{0x518e,0x3d},//
	{0x518f,0x56},//
	{0x5190,0x46},//
	{0x5191,0xf8},//
	{0x5192,0x4 },//
	{0x5193,0x70},//
	{0x5194,0xf0},//
	{0x5195,0xf0},//
	{0x5196,0x3 },//
	{0x5197,0x1 },//
	{0x5198,0x4 },//
	{0x5199,0x12},//
	{0x519a,0x4 },//
	{0x519b,0x0 },//
	{0x519c,0x6 },//
	{0x519d,0x82},//
	{0x519e,0x38},//
	{0x5381,0x1c},//     //;Sample_CMX
	{0x5382,0x5a},//
	{0x5383,0x6 },//
	{0x5384,0xa },//
	{0x5385,0x7e},//
	{0x5386,0x88},//
	{0x5387,0x7c},//
	{0x5388,0x6c},//
	{0x5389,0x10},//
	{0x538a,0x1 },//
	{0x538b,0x98},//
	{0x5000,0x27},// //;Sample_gamma;enable gamma
	{0x5481,0x8 },//
	{0x5482,0x14},//
	{0x5483,0x28},//
	{0x5484,0x51},//
	{0x5485,0x65},//
	{0x5486,0x71},//
	{0x5487,0x7d},//
	{0x5488,0x87},//
	{0x5489,0x91},//
	{0x548a,0x9a},//
	{0x548b,0xaa},//
	{0x548c,0xb8},//
	{0x548d,0xcd},//
	{0x548e,0xdd},//
	{0x548f,0xea},//
	{0x5490,0x1d},//
	{0x3a0f,0x38},//    //;Example_EV
	{0x3a10,0x30},//
	{0x3a1b,0x38},//
	{0x3a1e,0x30},//
	{0x3a11,0x70},//
	{0x3a1f,0x14},//
	{0x3a18,0x0 },//
    {0x3406, 0x00},
    {0x3400, 0x07},
    {0x3401, 0x02},
    {0x3402, 0x04},
    {0x3403, 0x00},
    {0x3404, 0x05},
    {0x3405, 0x15},
    //AWB 
{0x5180 ,0xff},
{0x5181 ,0xf2},
{0x5182 ,0x0 },
{0x5183 ,0x14},
{0x5184 ,0x25},
{0x5185 ,0x24},
{0x5186 ,0xd },
{0x5187 ,0xf },
{0x5188 ,0xf },
{0x5189 ,0x75},
{0x518a ,0x54},
{0x518b ,0xe0},
{0x518c ,0xb2},
{0x518d ,0x3e},
{0x518e ,0x39},
{0x518f ,0x52},


{0x5190 ,0x47},
{0x5191 ,0xf8},
{0x5192 ,0x4 },
{0x5193 ,0x70},
{0x5194 ,0xf0},
{0x5195 ,0xf0},
{0x5196 ,0x3 },
{0x5197 ,0x1 },
{0x5198 ,0x4 },
{0x5199 ,0x68},
{0x519a ,0x4 },
{0x519b ,0x0 },
{0x519c ,0x7 },
{0x519d ,0xa5},
{0x519e ,0x38},

	{0x0000, 0x00}
};

/* 640X480 VGA *///preview
static struct reginfo sensor_vga[] =
{
//  preview vga
{0x303c, 0x12},
{0x3824, 0x02},
{0x460c, 0x22},	
//XGA Windowing
{0x5001, 0x83},
{0x3800, 0x00},
{0x3801, 0x00},
{0x3802, 0x00},
{0x3803, 0x00},
{0x3804, 0x08},
{0x3805, 0x1f},
{0x3806, 0x06},
{0x3807, 0x09},
{0x3808, 0x04},
{0x3809, 0x00},
{0x380a, 0x03},
{0x380b, 0x00},
{0x380c, 0x08},
{0x380d, 0xfc},
{0x380e, 0x03},
{0x380f, 0x14},
{0x3810, 0x00},
{0x3811, 0x08},
{0x3812, 0x00},
{0x3813, 0x02},
{0x3814, 0x31},
{0x3815, 0x31},
	
{0x3708, 0x66},
{0x3709, 0x12},
{0x370c, 0x0c},
{0x4520, 0x0b},
{0x302c, 0x03},

//Bining mode
{0x3820, 0x01},
{0x3821, 0x03},//0x01
{0x4514, 0xaa},	

//vga	
{0x5000, 0xa7},
{0x5001, 0xa3},
{0x3808, 0x02},
{0x3809, 0x80},
{0x380a, 0x01},
{0x380b, 0xe0},
{0x380c, 0x08},
{0x380d, 0xfc},
{0x380e, 0x03},
{0x380f, 0x14},
{0x3810, 0x00},
{0x3811, 0x08},
{0x3812, 0x00},
{0x3813, 0x02},
{0x3814, 0x31},
{0x3815, 0x31},
	{0x3503,0x00},
	{0x0000, 0x00}
};

/* 352X288 CIF */
static struct reginfo sensor_cif[] =
{
	{0x0, 0x0}
};

/* 320*240 QVGA */
static  struct reginfo sensor_qvga[] =
{
  

	{0x0000, 0x00}
};

/* 176X144 QCIF*/
static struct reginfo sensor_qcif[] =
{
	{0x0, 0x0}
};

static  struct reginfo sensor_ClrFmt_YUYV[]=
{
    {0x4300, 0x30},
    {0x0000, 0x00}
};

static  struct reginfo sensor_ClrFmt_UYVY[]=
{
    {0x4300, 0x32},
    {0x0000, 0x00}
};

#if CONFIG_SENSOR_WhiteBalance
static  struct reginfo sensor_WhiteB_Auto[]=
{

{0x3406, 0x00},
	{0x5180,0xff},//	 //;IQ setting example //;Sample_AWB
	{0x5181,0xf2},//
	{0x5182,0x0 },//
	{0x5183,0x14},//
	{0x5184,0x25},//
	{0x5185,0x24},//
	{0x5186,0x0d},//
	{0x5187,0x0f},//
	{0x5188,0x0f},//
	{0x5189,0x75},//
	{0x518a,0x54},//
	{0x518b,0xe0},//
	{0x518c,0xb2},//
	{0x518d,0x3e},//
	{0x518e,0x39},//
	{0x518f,0x52},//
	{0x5190,0x47},//
	{0x5191,0xf8},//
	{0x5192,0x4 },//
	{0x5193,0x70},//
	{0x5194,0xf0},//
	{0x5195,0xf0},//
	{0x5196,0x3 },//
	{0x5197,0x1 },//
	{0x5198,0x4 },//
	{0x5199,0x68},//
	{0x519a,0x4 },//
	{0x519b,0x0 },//
	{0x519c,0x07 },//
	{0x519d,0xa5},//
	{0x519e,0x38},//
{0x0000, 0x00}
};
/* Cloudy Colour Temperature : 6500K - 8000K  */
static  struct reginfo sensor_WhiteB_Cloudy[]=
{
   
    {0x3406, 0x01},
    {0x3400, 0x07},
    {0x3401, 0x02},
    {0x3402, 0x04},
    {0x3403, 0x00},
    {0x3404, 0x05},
    {0x3405, 0x15},
    {0x0000, 0x00}
};
/* ClearDay Colour Temperature : 5000K - 6500K  */
static  struct reginfo sensor_WhiteB_ClearDay[]=
{
    //Sunny
    {0x3406, 0x01},
    {0x3400, 0x07},
    {0x3401, 0x02},
    {0x3402, 0x04},
    {0x3403, 0x00},
    {0x3404, 0x05},
    {0x3405, 0x15},
    {0x0000, 0x00}
};
/* Office Colour Temperature : 3500K - 5000K  */
static  struct reginfo sensor_WhiteB_TungstenLamp1[]=
{
    //Office
    {0x3406, 0x01},
    {0x3400, 0x06},
    {0x3401, 0x2a},
    {0x3402, 0x04},
    {0x3403, 0x00},
    {0x3404, 0x07},
    {0x3405, 0x24},
    {0x0000, 0x00}

};
/* Home Colour Temperature : 2500K - 3500K  */
static  struct reginfo sensor_WhiteB_TungstenLamp2[]=
{
    //Home
    {0x3406, 0x01},
    {0x3400, 0x04},
    {0x3401, 0x58},
    {0x3402, 0x04},
    {0x3403, 0x00},
    {0x3404, 0x07},
    {0x3405, 0x24},
    {0x0000, 0x00}
};
static struct reginfo *sensor_WhiteBalanceSeqe[] = {sensor_WhiteB_Auto, sensor_WhiteB_TungstenLamp1,sensor_WhiteB_TungstenLamp2,
    sensor_WhiteB_ClearDay, sensor_WhiteB_Cloudy,NULL,
};
#endif

#if CONFIG_SENSOR_Brightness
static  struct reginfo sensor_Brightness0[]=
{
    // Brightness -2
    {0x0000, 0x00}
};

static  struct reginfo sensor_Brightness1[]=
{
    // Brightness -1

    {0x0000, 0x00}
};

static  struct reginfo sensor_Brightness2[]=
{
    //  Brightness 0

    {0x0000, 0x00}
};

static  struct reginfo sensor_Brightness3[]=
{
    // Brightness +1

    {0x0000, 0x00}
};

static  struct reginfo sensor_Brightness4[]=
{
    //  Brightness +2

    {0x0000, 0x00}
};

static  struct reginfo sensor_Brightness5[]=
{
    //  Brightness +3

    {0x0000, 0x00}
};
static struct reginfo *sensor_BrightnessSeqe[] = {sensor_Brightness0, sensor_Brightness1, sensor_Brightness2, sensor_Brightness3,
    sensor_Brightness4, sensor_Brightness5,NULL,
};

#endif

#if CONFIG_SENSOR_Effect
static  struct reginfo sensor_Effect_Normal[] =
{
    {0x507b, 0x00},
    {0x0000, 0x00}
};

static  struct reginfo sensor_Effect_WandB[] =
{
    {0x507b, 0x20},
    {0x0000, 0x00}
};

static  struct reginfo sensor_Effect_Sepia[] =
{
    {0x507b, 0x18},
    {0x507e, 0x40},
    {0x507f, 0xa0},
    {0x0000, 0x00}
};

static  struct reginfo sensor_Effect_Negative[] =
{
    //Negative
    {0x507b, 0x40}, //bit[6] negative
    {0x0000, 0x00}
};
static  struct reginfo sensor_Effect_Bluish[] =
{
    // Bluish
    {0x507b, 0x18},
    {0x507e, 0xa0},
    {0x507f, 0x40},
    {0x0000, 0x00}
};

static  struct reginfo sensor_Effect_Green[] =
{
    //  Greenish
    {0x507b, 0x18},
    {0x507e, 0x60},
    {0x507f, 0x60},
    {0x0000, 0x00}
};
static struct reginfo *sensor_EffectSeqe[] = {sensor_Effect_Normal, sensor_Effect_WandB, sensor_Effect_Negative,sensor_Effect_Sepia,
    sensor_Effect_Bluish, sensor_Effect_Green,NULL,
};
#endif
#if CONFIG_SENSOR_Exposure
static  struct reginfo sensor_Exposure0[]=
{
	{0x0000, 0x00}
};

static  struct reginfo sensor_Exposure1[]=
{
    {0x0000, 0x00}
};

static  struct reginfo sensor_Exposure2[]=
{
    {0x0000, 0x00}
};

static  struct reginfo sensor_Exposure3[]=
{
    {0x0000, 0x00}
};

static  struct reginfo sensor_Exposure4[]=
{
    {0x0000, 0x00}
};

static  struct reginfo sensor_Exposure5[]=
{
    {0x0000, 0x00}
};

static  struct reginfo sensor_Exposure6[]=
{
    {0x0000, 0x00}
};

static struct reginfo *sensor_ExposureSeqe[] = {sensor_Exposure0, sensor_Exposure1, sensor_Exposure2, sensor_Exposure3,
    sensor_Exposure4, sensor_Exposure5,sensor_Exposure6,NULL,
};
#endif
#if CONFIG_SENSOR_Saturation
static  struct reginfo sensor_Saturation0[]=
{
    {0x0000, 0x00}
};

static  struct reginfo sensor_Saturation1[]=
{
    {0x0000, 0x00}
};

static  struct reginfo sensor_Saturation2[]=
{
    {0x0000, 0x00}
};
static struct reginfo *sensor_SaturationSeqe[] = {sensor_Saturation0, sensor_Saturation1, sensor_Saturation2, NULL,};

#endif
#if CONFIG_SENSOR_Contrast
static  struct reginfo sensor_Contrast0[]=
{
    {0x0000, 0x00}
};

static  struct reginfo sensor_Contrast1[]=
{
    {0x0000, 0x00}
};

static  struct reginfo sensor_Contrast2[]=
{
    {0x0000, 0x00}
};

static  struct reginfo sensor_Contrast3[]=
{
    {0x0000, 0x00}
};

static  struct reginfo sensor_Contrast4[]=
{
    {0x0000, 0x00}
};


static  struct reginfo sensor_Contrast5[]=
{
    {0x0000, 0x00}
};

static  struct reginfo sensor_Contrast6[]=
{
    {0x0000, 0x00}
};
static struct reginfo *sensor_ContrastSeqe[] = {sensor_Contrast0, sensor_Contrast1, sensor_Contrast2, sensor_Contrast3,
    sensor_Contrast4, sensor_Contrast5, sensor_Contrast6, NULL,
};

#endif
#if CONFIG_SENSOR_Mirror
static  struct reginfo sensor_MirrorOn[]=
{
    {0x0000, 0x00}
};

static  struct reginfo sensor_MirrorOff[]=
{
    {0x0000, 0x00}
};
static struct reginfo *sensor_MirrorSeqe[] = {sensor_MirrorOff, sensor_MirrorOn,NULL,};
#endif
#if CONFIG_SENSOR_Flip
static  struct reginfo sensor_FlipOn[]=
{
    {0x0000, 0x00}
};

static  struct reginfo sensor_FlipOff[]=
{
    {0x0000, 0x00}
};
static struct reginfo *sensor_FlipSeqe[] = {sensor_FlipOff, sensor_FlipOn,NULL,};

#endif
#if CONFIG_SENSOR_Scene
static  struct reginfo sensor_SceneAuto[] =
{
	//Jacky_M
    {0x3a00, 0x78},
    {0x0000, 0x00}
};

static  struct reginfo sensor_SceneNight[] =
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
    {0x0000, 0x00}
};
static struct reginfo *sensor_SceneSeqe[] = {sensor_SceneAuto, sensor_SceneNight,NULL,};

#endif
#if CONFIG_SENSOR_DigitalZoom
static struct reginfo sensor_Zoom0[] =
{
    {0x0, 0x0},
};

static struct reginfo sensor_Zoom1[] =
{
     {0x0, 0x0},
};

static struct reginfo sensor_Zoom2[] =
{
    {0x0, 0x0},
};


static struct reginfo sensor_Zoom3[] =
{
    {0x0, 0x0},
};
static struct reginfo *sensor_ZoomSeqe[] = {sensor_Zoom0, sensor_Zoom1, sensor_Zoom2, sensor_Zoom3, NULL,};
#endif

#if 0
static struct reginfo sensor_ISO100[] =
{

    {0x0, 0x0},
};

static struct reginfo sensor_ISO200[] =
{

     {0x0, 0x0},
};

static struct reginfo sensor_ISO400[] =
{

    {0x0, 0x0},
};


static struct reginfo sensor_ISO800[] =
{

    {0x0, 0x0},
};
static struct reginfo *sensor_ISOSeqe[] = {sensor_ISO100, sensor_ISO200, sensor_ISO400, sensor_ISO800, NULL,};
#endif

static struct v4l2_querymenu sensor_menus[] =
{
	#if CONFIG_SENSOR_WhiteBalance
    { .id = V4L2_CID_DO_WHITE_BALANCE,  .index = 0,  .name = "auto",  .reserved = 0, }, {  .id = V4L2_CID_DO_WHITE_BALANCE,  .index = 1, .name = "incandescent",  .reserved = 0,},
    { .id = V4L2_CID_DO_WHITE_BALANCE,  .index = 2,  .name = "fluorescent", .reserved = 0,}, {  .id = V4L2_CID_DO_WHITE_BALANCE, .index = 3,  .name = "daylight", .reserved = 0,},
    { .id = V4L2_CID_DO_WHITE_BALANCE,  .index = 4,  .name = "cloudy-daylight", .reserved = 0,},
    #endif

	#if CONFIG_SENSOR_Effect
    { .id = V4L2_CID_EFFECT,  .index = 0,  .name = "none",  .reserved = 0, }, {  .id = V4L2_CID_EFFECT,  .index = 1, .name = "mono",  .reserved = 0,},
    { .id = V4L2_CID_EFFECT,  .index = 2,  .name = "negative", .reserved = 0,}, {  .id = V4L2_CID_EFFECT, .index = 3,  .name = "sepia", .reserved = 0,},
    { .id = V4L2_CID_EFFECT,  .index = 4, .name = "posterize", .reserved = 0,} ,{ .id = V4L2_CID_EFFECT,  .index = 5,  .name = "aqua", .reserved = 0,},
    #endif

	#if CONFIG_SENSOR_Scene
    { .id = V4L2_CID_SCENE,  .index = 0, .name = "auto", .reserved = 0,} ,{ .id = V4L2_CID_SCENE,  .index = 1,  .name = "night", .reserved = 0,},
    #endif

	#if CONFIG_SENSOR_Flash
    { .id = V4L2_CID_FLASH,  .index = 0,  .name = "off",  .reserved = 0, }, {  .id = V4L2_CID_FLASH,  .index = 1, .name = "auto",  .reserved = 0,},
    { .id = V4L2_CID_FLASH,  .index = 2,  .name = "on", .reserved = 0,}, {  .id = V4L2_CID_FLASH, .index = 3,  .name = "torch", .reserved = 0,},
    #endif
};




static struct v4l2_queryctrl sensor_controls[] =
{
	#if CONFIG_SENSOR_WhiteBalance
    {
        .id		= V4L2_CID_DO_WHITE_BALANCE,
        .type		= V4L2_CTRL_TYPE_MENU,
        .name		= "White Balance Control",
        .minimum	= 0,
        .maximum	= 4,
        .step		= 1,
        .default_value = 0,
    },
    #endif

	#if CONFIG_SENSOR_Brightness
	{
        .id		= V4L2_CID_BRIGHTNESS,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "Brightness Control",
        .minimum	= -3,
        .maximum	= 2,
        .step		= 1,
        .default_value = 0,
    },
    #endif

	#if CONFIG_SENSOR_Effect
	{
        .id		= V4L2_CID_EFFECT,
        .type		= V4L2_CTRL_TYPE_MENU,
        .name		= "Effect Control",
        .minimum	= 0,
        .maximum	= 5,
        .step		= 1,
        .default_value = 0,
    },
	#endif

	#if CONFIG_SENSOR_Exposure
	{
        .id		= V4L2_CID_EXPOSURE,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "Exposure Control",
        .minimum	= 0,
        .maximum	= 6,
        .step		= 1,
        .default_value = 0,
    },
	#endif

	#if CONFIG_SENSOR_Saturation
	{
        .id		= V4L2_CID_SATURATION,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "Saturation Control",
        .minimum	= 0,
        .maximum	= 2,
        .step		= 1,
        .default_value = 0,
    },
    #endif

	#if CONFIG_SENSOR_Contrast
	{
        .id		= V4L2_CID_CONTRAST,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "Contrast Control",
        .minimum	= -3,
        .maximum	= 3,
        .step		= 1,
        .default_value = 0,
    },
	#endif

	#if CONFIG_SENSOR_Mirror
	{
        .id		= V4L2_CID_HFLIP,
        .type		= V4L2_CTRL_TYPE_BOOLEAN,
        .name		= "Mirror Control",
        .minimum	= 0,
        .maximum	= 1,
        .step		= 1,
        .default_value = 1,
    },
    #endif

	#if CONFIG_SENSOR_Flip
	{
        .id		= V4L2_CID_VFLIP,
        .type		= V4L2_CTRL_TYPE_BOOLEAN,
        .name		= "Flip Control",
        .minimum	= 0,
        .maximum	= 1,
        .step		= 1,
        .default_value = 1,
    },
    #endif

	#if CONFIG_SENSOR_Scene
    {
        .id		= V4L2_CID_SCENE,
        .type		= V4L2_CTRL_TYPE_MENU,
        .name		= "Scene Control",
        .minimum	= 0,
        .maximum	= 1,
        .step		= 1,
        .default_value = 0,
    },
    #endif

	#if CONFIG_SENSOR_DigitalZoom
    {
        .id		= V4L2_CID_ZOOM_RELATIVE,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "DigitalZoom Control",
        .minimum	= -1,
        .maximum	= 1,
        .step		= 1,
        .default_value = 0,
    }, {
        .id		= V4L2_CID_ZOOM_ABSOLUTE,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "DigitalZoom Control",
        .minimum	= 0,
        .maximum	= 3,
        .step		= 1,
        .default_value = 0,
    },
    #endif

	#if CONFIG_SENSOR_Focus
	{
        .id		= V4L2_CID_FOCUS_RELATIVE,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "Focus Control",
        .minimum	= -1,
        .maximum	= 1,
        .step		= 1,
        .default_value = 0,
    }, {
        .id		= V4L2_CID_FOCUS_ABSOLUTE,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "Focus Control",
        .minimum	= 0,
        .maximum	= 255,
        .step		= 1,
        .default_value = 125,
    },
    #endif

	#if CONFIG_SENSOR_Flash
	{
        .id		= V4L2_CID_FLASH,
        .type		= V4L2_CTRL_TYPE_MENU,
        .name		= "Flash Control",
        .minimum	= 0,
        .maximum	= 3,
        .step		= 1,
        .default_value = 0,
    },
	#endif
};

static int sensor_probe(struct i2c_client *client, const struct i2c_device_id *did);
static int sensor_video_probe(struct soc_camera_device *icd, struct i2c_client *client);
static int sensor_g_control(struct v4l2_subdev *sd, struct v4l2_control *ctrl);
static int sensor_s_control(struct v4l2_subdev *sd, struct v4l2_control *ctrl);
static int sensor_g_ext_controls(struct v4l2_subdev *sd,  struct v4l2_ext_controls *ext_ctrl);
static int sensor_s_ext_controls(struct v4l2_subdev *sd,  struct v4l2_ext_controls *ext_ctrl);
static int sensor_suspend(struct soc_camera_device *icd, pm_message_t pm_msg);
static int sensor_resume(struct soc_camera_device *icd);
static int sensor_set_bus_param(struct soc_camera_device *icd,unsigned long flags);
static unsigned long sensor_query_bus_param(struct soc_camera_device *icd);
#if CONFIG_SENSOR_Effect
static int sensor_set_effect(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value);
#endif
#if CONFIG_SENSOR_WhiteBalance
static int sensor_set_whiteBalance(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value);
#endif
static int sensor_deactivate(struct i2c_client *client);

static struct soc_camera_ops sensor_ops =
{
    .suspend                     = sensor_suspend,
    .resume                       = sensor_resume,
    .set_bus_param		= sensor_set_bus_param,
    .query_bus_param	= sensor_query_bus_param,
    .controls		= sensor_controls,
    .menus                         = sensor_menus,
    .num_controls		= ARRAY_SIZE(sensor_controls),
    .num_menus		= ARRAY_SIZE(sensor_menus),
};

/* only one fixed colorspace per pixelcode */
struct sensor_datafmt {
	enum v4l2_mbus_pixelcode code;
	enum v4l2_colorspace colorspace;
};

/* Find a data format by a pixel code in an array */
static const struct sensor_datafmt *sensor_find_datafmt(
	enum v4l2_mbus_pixelcode code, const struct sensor_datafmt *fmt,
	int n)
{
	int i;
	for (i = 0; i < n; i++)
		if (fmt[i].code == code)
			return fmt + i;

	return NULL;
}

static const struct sensor_datafmt sensor_colour_fmts[] = {
	{V4L2_MBUS_FMT_YUYV8_2X8, V4L2_COLORSPACE_JPEG},
   	{V4L2_MBUS_FMT_UYVY8_2X8, V4L2_COLORSPACE_JPEG}    	
};


typedef struct sensor_info_priv_s
{
    int whiteBalance;
    int brightness;
    int contrast;
    int saturation;
    int effect;
    int scene;
    int digitalzoom;
    int focus;
    int flash;
    int exposure;
	bool snap2preview;
	bool video2preview;
    unsigned char mirror;                                        /* HFLIP */
    unsigned char flip;                                          /* VFLIP */
    unsigned int winseqe_cur_addr;
	struct sensor_datafmt fmt;
    unsigned int funmodule_state;
} sensor_info_priv_t;

struct SENSOR
{
    struct v4l2_subdev subdev;
    struct i2c_client *client;
    sensor_info_priv_t info_priv;
    int model;	/* V4L2_IDENT_OV* codes from v4l2-chip-ident.h */
#if CONFIG_SENSOR_I2C_NOSCHED
	atomic_t tasklock_cnt;
#endif
	struct rk29camera_platform_data *sensor_io_request;
    struct rk29camera_gpio_res *sensor_gpio_res;
};


static struct SENSOR* to_sensor(const struct i2c_client *client)
{
    return container_of(i2c_get_clientdata(client), struct SENSOR, subdev);
}

static int sensor_task_lock(struct i2c_client *client, int lock)
{
#if CONFIG_SENSOR_I2C_NOSCHED
	int cnt = 3;
    struct SENSOR *sensor = to_sensor(client);

	if (lock) {
		if (atomic_read(&sensor->tasklock_cnt) == 0) {
			while ((atomic_read(&client->adapter->bus_lock.count) < 1) && (cnt>0)) {
				SENSOR_TR("\n %s will obtain i2c in atomic, but i2c bus is locked! Wait...\n",SENSOR_NAME_STRING());
				msleep(35);
				cnt--;
			}
			if ((atomic_read(&client->adapter->bus_lock.count) < 1) && (cnt<=0)) {
				SENSOR_TR("\n %s obtain i2c fail in atomic!!\n",SENSOR_NAME_STRING());
				goto sensor_task_lock_err;
			}
			preempt_disable();
		}

		atomic_add(1, &sensor->tasklock_cnt);
	} else {
		if (atomic_read(&sensor->tasklock_cnt) > 0) {
			atomic_sub(1, &sensor->tasklock_cnt);

			if (atomic_read(&sensor->tasklock_cnt) == 0)
				preempt_enable();
		}
	}
	return 0;
sensor_task_lock_err:
	return -1;  
#else
    return 0;
#endif

}

/* sensor register write */
static int sensor_write(struct i2c_client *client, u16 reg, u8 val)
{
    int err,cnt;
    u8 buf[3];
    struct i2c_msg msg[1];

    buf[0] = reg >> 8;
    buf[1] = reg & 0xFF;
    buf[2] = val;

    msg->addr = client->addr;
    msg->flags = client->flags;
    msg->buf = buf;
    msg->len = sizeof(buf);
    msg->scl_rate = CONFIG_SENSOR_I2C_SPEED;         /* ddl@rock-chips.com : 100kHz */
    msg->read_type = 0;               /* fpga i2c:0==I2C_NORMAL : direct use number not enum for don't want include spi_fpga.h */

    cnt = 3;
    err = -EAGAIN;
    while ((cnt-- > 0) && (err < 0)) {                       /* ddl@rock-chips.com :  Transfer again if transent is failed   */
        err = i2c_transfer(client->adapter, msg, 1);

        if (err >= 0) {
            return 0;
        } else {
        	SENSOR_TR("\n %s write reg(0x%x, val:0x%x) failed, try to write again!\n",SENSOR_NAME_STRING(),reg, val);
	        udelay(10);
        }
    }

    return err;
}

/* sensor register read */
static int sensor_read(struct i2c_client *client, u16 reg, u8 *val)
{
    int err,cnt;
    u8 buf[2];
    struct i2c_msg msg[2];

    buf[0] = reg >> 8;
    buf[1] = reg & 0xFF;

    msg[0].addr = client->addr;
    msg[0].flags = client->flags;
    msg[0].buf = buf;
    msg[0].len = sizeof(buf);
    msg[0].scl_rate = CONFIG_SENSOR_I2C_SPEED;       /* ddl@rock-chips.com : 100kHz */
    msg[0].read_type = 2;   /* fpga i2c:0==I2C_NO_STOP : direct use number not enum for don't want include spi_fpga.h */

    msg[1].addr = client->addr;
    msg[1].flags = client->flags|I2C_M_RD;
    msg[1].buf = buf;
    msg[1].len = 1;
    msg[1].scl_rate = CONFIG_SENSOR_I2C_SPEED;                       /* ddl@rock-chips.com : 100kHz */
    msg[1].read_type = 2;                             /* fpga i2c:0==I2C_NO_STOP : direct use number not enum for don't want include spi_fpga.h */

    cnt = 3;
    err = -EAGAIN;
    while ((cnt-- > 0) && (err < 0)) {                       /* ddl@rock-chips.com :  Transfer again if transent is failed   */
        err = i2c_transfer(client->adapter, msg, 2);

        if (err >= 0) {
            *val = buf[0];
            return 0;
        } else {
        	SENSOR_TR("\n %s read reg(0x%x val:0x%x) failed, try to read again! \n",SENSOR_NAME_STRING(),reg, *val);
            udelay(10);
        }
    }

    return err;
}

/* write a array of registers  */
static int sensor_write_array(struct i2c_client *client, struct reginfo *regarray)
{
    int err = 0, cnt;
    int i = 0;
#if CONFIG_SENSOR_I2C_RDWRCHK    
	char valchk;
#endif

	cnt = 0;
	if (sensor_task_lock(client, 1) < 0)
		goto sensor_write_array_end;

    while (regarray[i].reg != 0)
    {
        err = sensor_write(client, regarray[i].reg, regarray[i].val);
	
        if (err < 0)
        {
            if (cnt-- > 0) {
			    SENSOR_TR("%s..write failed current reg:0x%x, Write array again !\n", SENSOR_NAME_STRING(),regarray[i].reg);
				i = 0;
				continue;
            } else {
                SENSOR_TR("%s..write array failed!!!\n", SENSOR_NAME_STRING());
                err = -EPERM;
				goto sensor_write_array_end;
            }
        } else {
        #if CONFIG_SENSOR_I2C_RDWRCHK
			sensor_read(client, regarray[i].reg, &valchk);
			if (valchk != regarray[i].val)
				SENSOR_TR("%s Reg:0x%x write(0x%x, 0x%x) fail\n",SENSOR_NAME_STRING(), regarray[i].reg, regarray[i].val, valchk);
		#endif
        }
        i++;
    }

sensor_write_array_end:
	sensor_task_lock(client,0);
	return err;
}
#if CONFIG_SENSOR_I2C_RDWRCHK
static int sensor_readchk_array(struct i2c_client *client, struct reginfo *regarray)
{
    int cnt;
    int i = 0;
	char valchk;

	cnt = 0;
	valchk = 0;
    while (regarray[i].reg != 0)
    {
		sensor_read(client, regarray[i].reg, &valchk);
		if (valchk != regarray[i].val)
			SENSOR_TR("%s Reg:0x%x read(0x%x, 0x%x) error\n",SENSOR_NAME_STRING(), regarray[i].reg, regarray[i].val, valchk);

        i++;
    }
    return 0;
}
#endif
static int sensor_ioctrl(struct soc_camera_device *icd,enum rk29sensor_power_cmd cmd, int on)
{
	struct soc_camera_link *icl = to_soc_camera_link(icd);
	int ret = 0;

    SENSOR_DG("%s %s  cmd(%d) on(%d)\n",SENSOR_NAME_STRING(),__FUNCTION__,cmd,on);
	switch (cmd)
	{
		case Sensor_PowerDown:
		{
			if (icl->powerdown) {
				ret = icl->powerdown(icd->pdev, on);
				if (ret == RK29_CAM_IO_SUCCESS) {
					if (on == 1) {
						mdelay(50);
					}
					if (on == 0) {
						mdelay(2);
						if (icl->reset)
							icl->reset(icd->pdev);
					}
				} else if (ret == RK29_CAM_EIO_REQUESTFAIL) {
					ret = -ENODEV;
					goto sensor_power_end;
				}
			}
			break;
		}
		case Sensor_Flash:
		{
			struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
    		struct SENSOR *sensor = to_sensor(client);

			if (sensor->sensor_io_request && sensor->sensor_io_request->sensor_ioctrl) {
				sensor->sensor_io_request->sensor_ioctrl(icd->pdev,Cam_Flash, on);
                if(on) {
                    //flash off after 2 secs
                    hrtimer_cancel(&(flash_off_timer.timer));
                    hrtimer_start(&(flash_off_timer.timer),ktime_set(0, 800*1000*1000),HRTIMER_MODE_REL);
                }
			}
            break;
		}
		default:
		{
			SENSOR_TR("%s %s cmd(0x%x) is unknown!",SENSOR_NAME_STRING(),__FUNCTION__,cmd);
			break;
		}
	}
sensor_power_end:
	return ret;
}

static enum hrtimer_restart flash_off_func(struct hrtimer *timer)
{
    struct flash_timer *fps_timer = container_of(timer, struct flash_timer, timer);
    
    sensor_ioctrl(fps_timer->icd,Sensor_Flash,0);
   
    return 0;
}

static int sensor_init(struct v4l2_subdev *sd, u32 val)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct soc_camera_device *icd = client->dev.platform_data;
    struct SENSOR *sensor = to_sensor(client);
	const struct v4l2_queryctrl *qctrl;
	const struct sensor_datafmt *fmt;
unsigned char value;
int ret=0,pid = 0;

    SENSOR_DG("\n%s..%s.. \n",SENSOR_NAME_STRING(),__FUNCTION__);

	if (sensor_ioctrl(icd, Sensor_PowerDown, 0) < 0) {
		ret = -ENODEV;
		goto sensor_INIT_ERR;
	}

    /* soft reset */
	printk("jack+sensor_init begin\n");
	if (sensor_task_lock(client,1)<0)
		goto sensor_INIT_ERR;


    if (ret != 0)
    {
        SENSOR_TR("%s soft reset sensor failed\n",SENSOR_NAME_STRING());
        ret = -ENODEV;
		goto sensor_INIT_ERR;
    }
    mdelay(5);  //delay 5 microseconds

	/* check if it is an sensor sensor */
    ret = sensor_read(client, 0x300a, &value);
    if (ret != 0) {
        SENSOR_TR("read chip id high byte failed\n");
        ret = -ENODEV;
        goto sensor_INIT_ERR;
    }

    pid |= (value << 8);

    ret = sensor_read(client, 0x300b, &value);
    if (ret != 0) {
        SENSOR_TR("read chip id low byte failed\n");
        ret = -ENODEV;
        goto sensor_INIT_ERR;
    }

    pid |= (value & 0xff);
    SENSOR_DG("\n %s  pid = 0x%x\n", SENSOR_NAME_STRING(), pid);
    if (pid == SENSOR_ID) {
        sensor->model = SENSOR_V4L2_IDENT;
    } else {
        SENSOR_TR("error: %s mismatched   pid = 0x%x\n", SENSOR_NAME_STRING(), pid);
        ret = -ENODEV;
        goto sensor_INIT_ERR;
    }

    ret = sensor_write_array(client, sensor_init_data);
    if (ret != 0)
    {
        SENSOR_TR("error: %s initial failed\n",SENSOR_NAME_STRING());
        goto sensor_INIT_ERR;
    }
	sensor_task_lock(client,0);
    //icd->user_width = SENSOR_INIT_WIDTH;
    //icd->user_height = SENSOR_INIT_HEIGHT;
    sensor->info_priv.winseqe_cur_addr  = (int)SENSOR_INIT_WINSEQADR;
	fmt = sensor_find_datafmt(SENSOR_INIT_PIXFMT,sensor_colour_fmts, ARRAY_SIZE(sensor_colour_fmts));
    if (!fmt) {
        SENSOR_TR("error: %s initial array colour fmts is not support!!",SENSOR_NAME_STRING());
        ret = -EINVAL;
        goto sensor_INIT_ERR;
    }
	sensor->info_priv.fmt = *fmt;

    /* sensor sensor information for initialization  */
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_DO_WHITE_BALANCE);
	if (qctrl)
    	sensor->info_priv.whiteBalance = qctrl->default_value;
    flash_off_timer.icd = icd;
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_BRIGHTNESS);
	if (qctrl)
    	sensor->info_priv.brightness = qctrl->default_value;
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_EFFECT);
	if (qctrl)
    	sensor->info_priv.effect = qctrl->default_value;
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_EXPOSURE);
	if (qctrl)
        sensor->info_priv.exposure = qctrl->default_value;

	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_SATURATION);
	if (qctrl)
        sensor->info_priv.saturation = qctrl->default_value;
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_CONTRAST);
	if (qctrl)
        sensor->info_priv.contrast = qctrl->default_value;
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_HFLIP);
	if (qctrl)
        sensor->info_priv.mirror = qctrl->default_value;
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_VFLIP);
	if (qctrl)
        sensor->info_priv.flip = qctrl->default_value;
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_SCENE);
	if (qctrl)
        sensor->info_priv.scene = qctrl->default_value;
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_ZOOM_ABSOLUTE);
	if (qctrl)
        sensor->info_priv.digitalzoom = qctrl->default_value;

    /* ddl@rock-chips.com : if sensor support auto focus and flash, programer must run focus and flash code  */
	#if CONFIG_SENSOR_Focus
    sensor_set_focus();
    qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_FOCUS_ABSOLUTE);
	if (qctrl)
        sensor->info_priv.focus = qctrl->default_value;
	#endif

	#if CONFIG_SENSOR_Flash	
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_FLASH);
	if (qctrl)
        sensor->info_priv.flash = qctrl->default_value;
    flash_off_timer.icd = icd;
    #endif

    SENSOR_DG("\n%s..%s.. icd->width = %d.. icd->height %d\n",SENSOR_NAME_STRING(),((val == 0)?__FUNCTION__:"sensor_reinit"),icd->user_width,icd->user_height);
    sensor->info_priv.funmodule_state |= SENSOR_INIT_IS_OK;	
    return 0;
sensor_INIT_ERR:
    sensor->info_priv.funmodule_state &= ~SENSOR_INIT_IS_OK;
	sensor_task_lock(client,0);
	sensor_deactivate(client);
    return ret;
}

static int sensor_deactivate(struct i2c_client *client)
{
	struct soc_camera_device *icd = client->dev.platform_data;
	u8 reg_val;
    struct SENSOR *sensor = to_sensor(client);
    
	SENSOR_DG("\n%s..%s.. Enter\n",SENSOR_NAME_STRING(),__FUNCTION__);
    
	/* ddl@rock-chips.com : all sensor output pin must change to input for other sensor */
    if (sensor->info_priv.funmodule_state & SENSOR_INIT_IS_OK) {
    	sensor_task_lock(client, 1);
    	sensor_read(client,0x3017,&reg_val);
        sensor_write(client, 0x3017, reg_val&0x80);
    	sensor_read(client,0x3018,&reg_val);
        sensor_write(client, 0x3018, reg_val&0x03);
    	sensor_task_lock(client, 0);        
    }
    sensor_ioctrl(icd, Sensor_PowerDown, 1); 
	
    msleep(100); 

	/* ddl@rock-chips.com : sensor config init width , because next open sensor quickly(soc_camera_open -> Try to configure with default parameters) */
	icd->user_width = SENSOR_INIT_WIDTH;
    icd->user_height = SENSOR_INIT_HEIGHT;
    sensor->info_priv.funmodule_state &= ~SENSOR_INIT_IS_OK;
	
	return 0;
}

static  struct reginfo sensor_power_down_sequence[]=
{
    {0x00,0x00}
};
static int sensor_suspend(struct soc_camera_device *icd, pm_message_t pm_msg)
{
    int ret;
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if (pm_msg.event == PM_EVENT_SUSPEND) {
        SENSOR_DG("\n %s Enter Suspend.. \n", SENSOR_NAME_STRING());
        ret = sensor_write_array(client, sensor_power_down_sequence) ;
        if (ret != 0) {
            SENSOR_TR("\n %s..%s WriteReg Fail.. \n", SENSOR_NAME_STRING(),__FUNCTION__);
            return ret;
        } else {
            ret = sensor_ioctrl(icd, Sensor_PowerDown, 1);
            if (ret < 0) {
			    SENSOR_TR("\n %s suspend fail for turn on power!\n", SENSOR_NAME_STRING());
                return -EINVAL;
            }
        }
    } else {
        SENSOR_TR("\n %s cann't suppout Suspend..\n",SENSOR_NAME_STRING());
        return -EINVAL;
    }
    return 0;
}

static int sensor_resume(struct soc_camera_device *icd)
{
	int ret;

    ret = sensor_ioctrl(icd, Sensor_PowerDown, 0);
    if (ret < 0) {
		SENSOR_TR("\n %s resume fail for turn on power!\n", SENSOR_NAME_STRING());
        return -EINVAL;
    }

	SENSOR_DG("\n %s Enter Resume.. \n", SENSOR_NAME_STRING());

    return 0;

}

static int sensor_set_bus_param(struct soc_camera_device *icd,
                                unsigned long flags)
{

    return 0;
}

static unsigned long sensor_query_bus_param(struct soc_camera_device *icd)
{
    struct soc_camera_link *icl = to_soc_camera_link(icd);
    unsigned long flags = SENSOR_BUS_PARAM;

    return soc_camera_apply_sensor_flags(icl, flags);
}

static int sensor_g_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct soc_camera_device *icd = client->dev.platform_data;
    struct SENSOR *sensor = to_sensor(client);

    mf->width	= icd->user_width;
	mf->height	= icd->user_height;
	mf->code	= sensor->info_priv.fmt.code;
	mf->colorspace	= sensor->info_priv.fmt.colorspace;
	mf->field	= V4L2_FIELD_NONE;

    return 0;
}

static bool sensor_fmt_capturechk(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
    bool ret = false;

	if ((mf->width == 1024) && (mf->height == 768)) {
		ret = true;
	} else if ((mf->width == 1280) && (mf->height == 1024)) {
		ret = true;
	} else if ((mf->width == 1600) && (mf->height == 1200)) {
		ret = true;
	} else if ((mf->width == 2048) && (mf->height == 1536)) {
		ret = true;
	} else if ((mf->width == 2592) && (mf->height == 1944)) {
		ret = true;
	}

	if (ret == true)
		SENSOR_DG("%s %dx%d is capture format\n", __FUNCTION__, mf->width, mf->height);
	return ret;
}


static bool sensor_fmt_videochk(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
    bool ret = false;

	if ((mf->width == 1280) && (mf->height == 720)) {
		ret = true;
	} else if ((mf->width == 1920) && (mf->height == 1080)) {
		ret = true;
	}

	if (ret == true)
		SENSOR_DG("%s %dx%d is video format\n", __FUNCTION__, mf->width, mf->height);
	return ret;
}
void button5_Click(struct i2c_client *client,struct reginfo *winseqe_set_addr)
{
    int extra_lines, Preview_Gain, preview_dummy_pixels, Capture_Exposure; 
	char temp_reg1, temp_reg2, temp_reg3;
    int Capture_Line_Width,Capture_Banding_Filter,Capture_Gain, shutter, Gain_Exposure;
    //int PV_EXPOSURE_LIMITATION = 782;
    //int FULL_EXPOSURE_LIMITATION = 1640;
    int Capture_Maximum_Shutter = 1564;   
    int Default_Reg0x380c = 8;
    int Default_Reg0x380d = 252;
    int Capture_PCLK_Frequency = 54 ;
    int Capture_Dummy_Pixels =0 ;
    int Capture_Max_Gain = 256 ;
    int extra_exposure_lines =0 ;
	int ret = 0; 

 	sensor_read(client, 0x3500, &temp_reg1);
 	sensor_read(client, 0x3501, &temp_reg2);
	sensor_read(client, 0x3502, &temp_reg3);
	
    shutter = (temp_reg1 << 16) | (temp_reg2 << 8) | (temp_reg3);
    shutter = shutter / 16;

	sensor_read(client, 0x350d, &temp_reg1);
 	sensor_read(client, 0x350c, &temp_reg2);
    extra_lines = (temp_reg1) | (temp_reg2 << 8);

	sensor_read(client, 0x350b, &temp_reg1);
	 sensor_read(client, 0x350a, &temp_reg2);
    Preview_Gain = (temp_reg2 << 8) | (temp_reg1);

	sensor_read(client, 0x380d, &temp_reg1);
 	sensor_read(client, 0x380c, &temp_reg2);

    preview_dummy_pixels = (((temp_reg1-Default_Reg0x380c) & 0xf0) << 8 ) + temp_reg2-Default_Reg0x380d ;   

    //sensor_write(client, 0x3503, 0x00);    
    sensor_write(client, 0x3503, 0x03);                             
                 
    Capture_Line_Width = 2376 + Capture_Dummy_Pixels;
                          
    //Capture_Exposure = (shutter + extra_lines)/2;
    Capture_Exposure = (shutter + extra_lines)*36/27/2;
    Capture_Banding_Filter = Capture_PCLK_Frequency * 1000000 / 100 / (2 * Capture_Line_Width);      //114
    if(Preview_Gain < 16)
    {
        Preview_Gain = 16 ;
    }
    Gain_Exposure = Preview_Gain * Capture_Exposure;

    if (Gain_Exposure < Capture_Banding_Filter * 16 )  //114
    {
        Capture_Exposure = Gain_Exposure / 16;

        if(0 == Capture_Exposure)
        {
            Capture_Exposure = 1;
        }

        Capture_Gain = (Gain_Exposure*2 / Capture_Exposure + 1 ) / 2;
    } else {
        if (Gain_Exposure > Capture_Maximum_Shutter * 16 )    //1564*16
        {
            Capture_Exposure = Capture_Maximum_Shutter;
            Capture_Gain = (Gain_Exposure*2 /Capture_Maximum_Shutter + 1)/2;
     
            if (Capture_Gain > Capture_Max_Gain)       //512
            {
                Capture_Exposure = Gain_Exposure/Capture_Max_Gain/16 + Gain_Exposure/10/Capture_Max_Gain/16;
                if(0 == Capture_Exposure)
                {
                    Capture_Exposure = 1;
                }

                Capture_Exposure = Capture_Exposure/ Capture_Banding_Filter;
                Capture_Exposure = Capture_Exposure * Capture_Banding_Filter;
                Capture_Gain = (Gain_Exposure *2 / Capture_Exposure + 1) /2;
            }
        } else {
            Capture_Exposure = Gain_Exposure /16/ Capture_Banding_Filter;
            Capture_Exposure = Capture_Exposure * Capture_Banding_Filter;
            if(0 == Capture_Exposure)
            {
                Capture_Exposure = 1;
            }
            Capture_Gain = (Gain_Exposure *2 / Capture_Exposure +1)/2;
        }

        shutter = Capture_Exposure;
    }
    
    ret |= sensor_write_array(client, winseqe_set_addr);
    if (ret != 0) {
        SENSOR_TR("%s set format capability failed\n", SENSOR_NAME_STRING());
    }        
                 
               
    if(shutter <= 1640)
    {
        extra_exposure_lines = 0;
    } else {
        extra_exposure_lines = shutter - 1640;
    }

    if (shutter > 1640)
    {
        shutter = 1640;
    }

    shutter = shutter * 16;

    if(Capture_Gain >= 128)
	    sensor_write(client, 0x5000,0x07); 	

    sensor_write(client, 0x350d, (extra_exposure_lines & 0xFF));
    sensor_write(client, 0x350c, ((extra_exposure_lines & 0xFF00) >> 8));

    sensor_write(client, 0x3502, (shutter & 0xFF));
    sensor_write(client, 0x3501, ((shutter & 0xFF00) >> 8));
    sensor_write(client, 0x3500, ((shutter & 0xFF0000) >> 16));

    sensor_write(client, 0x350b, (Capture_Gain & 0xFF));
    sensor_write(client, 0x350a, (( Capture_Gain & 0xFF00) >> 8));
}

static int sensor_s_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    const struct sensor_datafmt *fmt;
    struct SENSOR *sensor = to_sensor(client);
	const struct v4l2_queryctrl *qctrl;
	struct soc_camera_device *icd = client->dev.platform_data;
    struct reginfo *winseqe_set_addr=NULL;
    int ret=0, set_w,set_h;

	fmt = sensor_find_datafmt(mf->code, sensor_colour_fmts,
				   ARRAY_SIZE(sensor_colour_fmts));
	if (!fmt) {
        ret = -EINVAL;
        goto sensor_s_fmt_end;
    }

	if (sensor->info_priv.fmt.code != mf->code) {
		switch (mf->code)
		{
			case V4L2_MBUS_FMT_YUYV8_2X8:
			{
				winseqe_set_addr = sensor_ClrFmt_YUYV;
				break;
			}
			case V4L2_MBUS_FMT_UYVY8_2X8:
			{
				winseqe_set_addr = sensor_ClrFmt_UYVY;
				break;
			}
			default:
				break;
		}
		if (winseqe_set_addr != NULL) {
            sensor_write_array(client, winseqe_set_addr);
			sensor->info_priv.fmt.code = mf->code;
            sensor->info_priv.fmt.colorspace= mf->colorspace;            
			SENSOR_DG("%s v4l2_mbus_code:%d set success!\n", SENSOR_NAME_STRING(),mf->code);
		} else {
			SENSOR_TR("%s v4l2_mbus_code:%d is invalidate!\n", SENSOR_NAME_STRING(),mf->code);
		}
	}

    set_w = mf->width;
    set_h = mf->height;

	if (((set_w <= 176) && (set_h <= 144)) && sensor_qcif[0].reg)
	{
		winseqe_set_addr = sensor_qcif;
        set_w = 176;
        set_h = 144;
	}
	else if (((set_w <= 320) && (set_h <= 240)) && sensor_qvga[0].reg)
    {
        winseqe_set_addr = sensor_qvga;
        set_w = 320;
        set_h = 240;
    }
    else if (((set_w <= 352) && (set_h<= 288)) && sensor_cif[0].reg)
    {
        winseqe_set_addr = sensor_cif;
        set_w = 352;
        set_h = 288;
    }
    else if (((set_w <= 640) && (set_h <= 480)) && sensor_vga[0].reg)
    {
        winseqe_set_addr = sensor_vga;
        set_w = 640;
        set_h = 480;
    }
    else if (((set_w <= 800) && (set_h <= 600)) && sensor_svga[0].reg)
    {
        winseqe_set_addr = sensor_svga;
        set_w = 800;
        set_h = 600;
    }
    else if (((set_w <= 1280) && (set_h <= 720)) && sensor_720p[0].reg)
    {
        winseqe_set_addr = sensor_720p;
        set_w = 1280;
        set_h = 720;
    }
	else if (((set_w <= 1024) && (set_h <= 768)) && sensor_xga[0].reg)
    {
        winseqe_set_addr = sensor_xga;
        set_w = 1024;
        set_h = 768;
    }
    else if (((set_w <= 1280) && (set_h <= 1024)) && sensor_sxga[0].reg)
    {
        winseqe_set_addr = sensor_sxga;
        set_w = 1280;
        set_h = 1024;
    }
    else if (((set_w <= 1600) && (set_h <= 1200)) && sensor_uxga[0].reg)
    {
        winseqe_set_addr = sensor_uxga;
        set_w = 1600;
        set_h = 1200;
    }
    else if (((set_w <= 2048) && (set_h <= 1536)) && sensor_qxga[0].reg)
    {
        winseqe_set_addr = sensor_qxga;
        set_w = 2048;
        set_h = 1536;
    }
    else
    {
        winseqe_set_addr = SENSOR_INIT_WINSEQADR;               /* ddl@rock-chips.com : Sensor output smallest size if  isn't support app  */
        set_w = SENSOR_INIT_WIDTH;
        set_h = SENSOR_INIT_HEIGHT;
		SENSOR_TR("\n %s..%s Format is Invalidate. pix->width = %d.. pix->height = %d\n",SENSOR_NAME_STRING(),__FUNCTION__,mf->width,mf->height);
    }

    if ((int)winseqe_set_addr  != sensor->info_priv.winseqe_cur_addr) {
        #if CONFIG_SENSOR_Flash
        if (sensor_fmt_capturechk(sd,mf) == true) {      /* ddl@rock-chips.com : Capture */
            if ((sensor->info_priv.flash == 1) || (sensor->info_priv.flash == 2)) {
                sensor_ioctrl(icd, Sensor_Flash, Flash_On);
                SENSOR_DG("%s flash on in capture!\n", SENSOR_NAME_STRING());
            }           
        } else {                                        /* ddl@rock-chips.com : Video */
            if ((sensor->info_priv.flash == 1) || (sensor->info_priv.flash == 2)) {
                sensor_ioctrl(icd, Sensor_Flash, Flash_Off);
                SENSOR_DG("%s flash off in preivew!\n", SENSOR_NAME_STRING());
            }
        }
        #endif
	if (sensor_fmt_capturechk(sd,mf) != true)
	{
        ret |= sensor_write_array(client, winseqe_set_addr);
        if (ret != 0) {
            SENSOR_TR("%s set format capability failed\n", SENSOR_NAME_STRING());
            #if CONFIG_SENSOR_Flash
            if (sensor_fmt_capturechk(sd,mf) == true) {
                if ((sensor->info_priv.flash == 1) || (sensor->info_priv.flash == 2)) {
                    sensor_ioctrl(icd, Sensor_Flash, Flash_Off);
                    SENSOR_TR("%s Capture format set fail, flash off !\n", SENSOR_NAME_STRING());
                }
            }
            #endif
            goto sensor_s_fmt_end;
        }        
	}

		sensor->info_priv.winseqe_cur_addr  = (int)winseqe_set_addr;

		if (sensor_fmt_capturechk(sd,mf) == true) {				    /* ddl@rock-chips.com : Capture */

			button5_Click(client,winseqe_set_addr);
			qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_EFFECT);
			sensor_set_effect(icd, qctrl,sensor->info_priv.effect);
			if (sensor->info_priv.whiteBalance != 0) {
				qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_DO_WHITE_BALANCE);
				sensor_set_whiteBalance(icd, qctrl,sensor->info_priv.whiteBalance);
			}
			sensor->info_priv.snap2preview = true;
		} else if (sensor_fmt_videochk(sd,mf) == true) {			/* ddl@rock-chips.com : Video */
			qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_EFFECT);
			sensor_set_effect(icd, qctrl,sensor->info_priv.effect);
			qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_DO_WHITE_BALANCE);
			sensor_set_whiteBalance(icd, qctrl,sensor->info_priv.whiteBalance);
			sensor->info_priv.video2preview = true;
		} else if ((sensor->info_priv.snap2preview == true) || (sensor->info_priv.video2preview == true)) {
			qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_EFFECT);
			sensor_set_effect(icd, qctrl,sensor->info_priv.effect);
			qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_DO_WHITE_BALANCE);
			sensor_set_whiteBalance(icd, qctrl,sensor->info_priv.whiteBalance);
            msleep(200);
			sensor->info_priv.video2preview = false;
			sensor->info_priv.snap2preview = false;
		}
		else
	{msleep(200);}

        SENSOR_DG("\n%s..%s.. icd->width = %d.. icd->height %d\n",SENSOR_NAME_STRING(),__FUNCTION__,set_w,set_h);
    }
    else
    {
        SENSOR_DG("\n %s .. Current Format is validate. icd->width = %d.. icd->height %d\n",SENSOR_NAME_STRING(),set_w,set_h);
    }

	mf->width = set_w;
    mf->height = set_h;

sensor_s_fmt_end:
    return ret;
}


static int sensor_try_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct SENSOR *sensor = to_sensor(client);
    const struct sensor_datafmt *fmt;
    int ret = 0,set_w,set_h;
   
	fmt = sensor_find_datafmt(mf->code, sensor_colour_fmts,
				   ARRAY_SIZE(sensor_colour_fmts));
	if (fmt == NULL) {
		fmt = &sensor->info_priv.fmt;
        mf->code = fmt->code;
	} 

    if (mf->height > SENSOR_MAX_HEIGHT)
        mf->height = SENSOR_MAX_HEIGHT;
    else if (mf->height < SENSOR_MIN_HEIGHT)
        mf->height = SENSOR_MIN_HEIGHT;

    if (mf->width > SENSOR_MAX_WIDTH)
        mf->width = SENSOR_MAX_WIDTH;
    else if (mf->width < SENSOR_MIN_WIDTH)
        mf->width = SENSOR_MIN_WIDTH;

    set_w = mf->width;
    set_h = mf->height;

	if (((set_w <= 176) && (set_h <= 144)) && sensor_qcif[0].reg)
	{
        set_w = 176;
        set_h = 144;
	}
	else if (((set_w <= 320) && (set_h <= 240)) && sensor_qvga[0].reg)
    {
        set_w = 320;
        set_h = 240;
    }
    else if (((set_w <= 352) && (set_h<= 288)) && sensor_cif[0].reg)
    {
        set_w = 352;
        set_h = 288;
    }
    else if (((set_w <= 640) && (set_h <= 480)) && sensor_vga[0].reg)
    {
        set_w = 640;
        set_h = 480;
    }
    else if (((set_w <= 800) && (set_h <= 600)) && sensor_svga[0].reg)
    {
        set_w = 800;
        set_h = 600;
    }
    else if (((set_w <= 1280) && (set_h <= 720)) && sensor_720p[0].reg)
    {
        set_w = 1280;
        set_h = 720;
    }
	else if (((set_w <= 1024) && (set_h <= 768)) && sensor_xga[0].reg)
    {
        set_w = 1024;
        set_h = 768;
    }
    else if (((set_w <= 1280) && (set_h <= 1024)) && sensor_sxga[0].reg)
    {
        set_w = 1280;
        set_h = 1024;
    }
    else if (((set_w <= 1600) && (set_h <= 1200)) && sensor_uxga[0].reg)
    {
        set_w = 1600;
        set_h = 1200;
    }
    else if (((set_w <= 2048) && (set_h <= 1536)) && sensor_qxga[0].reg)
    {
        set_w = 2048;
        set_h = 1536;
    }
    else
    {
        set_w = SENSOR_INIT_WIDTH;
        set_h = SENSOR_INIT_HEIGHT;
		SENSOR_TR("\n %s..%s Format is Invalidate. pix->width = %d.. pix->height = %d\n",SENSOR_NAME_STRING(),__FUNCTION__,mf->width,mf->height);
    }

    mf->width = set_w;
    mf->height = set_h;
    mf->colorspace = fmt->colorspace;
    
    return ret;
}


 static int sensor_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *id)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);

    if (id->match.type != V4L2_CHIP_MATCH_I2C_ADDR)
        return -EINVAL;

    if (id->match.addr != client->addr)
        return -ENODEV;

    id->ident = SENSOR_V4L2_IDENT;      /* ddl@rock-chips.com :  Return OV2655  identifier */
    id->revision = 0;

    return 0;
}
#if CONFIG_SENSOR_Brightness
static int sensor_set_brightness(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
    {
        if (sensor_BrightnessSeqe[value - qctrl->minimum] != NULL)
        {
            if (sensor_write_array(client, sensor_BrightnessSeqe[value - qctrl->minimum]) != 0)
            {
                SENSOR_TR("%s..%s WriteReg Fail.. \n",SENSOR_NAME_STRING(), __FUNCTION__);
                return -EINVAL;
            }
            SENSOR_DG("%s..%s : %x\n",SENSOR_NAME_STRING(),__FUNCTION__, value);
            return 0;
        }
    }
	SENSOR_TR("\n %s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
    return -EINVAL;
}
#endif
#if CONFIG_SENSOR_Effect
static int sensor_set_effect(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
    {
        if (sensor_EffectSeqe[value - qctrl->minimum] != NULL)
        {
            if (sensor_write_array(client, sensor_EffectSeqe[value - qctrl->minimum]) != 0)
            {
                SENSOR_TR("%s..%s WriteReg Fail.. \n",SENSOR_NAME_STRING(), __FUNCTION__);
                return -EINVAL;
            }
            SENSOR_DG("%s..%s : %x\n",SENSOR_NAME_STRING(),__FUNCTION__, value);
            return 0;
        }
    }
	SENSOR_TR("\n %s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
    return -EINVAL;
}
#endif
#if CONFIG_SENSOR_Exposure
static int sensor_set_exposure(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
    {
        if (sensor_ExposureSeqe[value - qctrl->minimum] != NULL)
        {
            if (sensor_write_array(client, sensor_ExposureSeqe[value - qctrl->minimum]) != 0)
            {
                SENSOR_TR("%s..%s WriteReg Fail.. \n",SENSOR_NAME_STRING(), __FUNCTION__);
                return -EINVAL;
            }
            SENSOR_DG("%s..%s : %x\n",SENSOR_NAME_STRING(),__FUNCTION__, value);
            return 0;
        }
    }
	SENSOR_TR("\n %s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
    return -EINVAL;
}
#endif
#if CONFIG_SENSOR_Saturation
static int sensor_set_saturation(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
    {
        if (sensor_SaturationSeqe[value - qctrl->minimum] != NULL)
        {
            if (sensor_write_array(client, sensor_SaturationSeqe[value - qctrl->minimum]) != 0)
            {
                SENSOR_TR("%s..%s WriteReg Fail.. \n",SENSOR_NAME_STRING(), __FUNCTION__);
                return -EINVAL;
            }
            SENSOR_DG("%s..%s : %x\n",SENSOR_NAME_STRING(),__FUNCTION__, value);
            return 0;
        }
    }
    SENSOR_TR("\n %s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
    return -EINVAL;
}
#endif
#if CONFIG_SENSOR_Contrast
static int sensor_set_contrast(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
    {
        if (sensor_ContrastSeqe[value - qctrl->minimum] != NULL)
        {
            if (sensor_write_array(client, sensor_ContrastSeqe[value - qctrl->minimum]) != 0)
            {
                SENSOR_TR("%s..%s WriteReg Fail.. \n",SENSOR_NAME_STRING(), __FUNCTION__);
                return -EINVAL;
            }
            SENSOR_DG("%s..%s : %x\n",SENSOR_NAME_STRING(),__FUNCTION__, value);
            return 0;
        }
    }
    SENSOR_TR("\n %s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
    return -EINVAL;
}
#endif
#if CONFIG_SENSOR_Mirror
static int sensor_set_mirror(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
    {
        if (sensor_MirrorSeqe[value - qctrl->minimum] != NULL)
        {
            if (sensor_write_array(client, sensor_MirrorSeqe[value - qctrl->minimum]) != 0)
            {
                SENSOR_TR("%s..%s WriteReg Fail.. \n",SENSOR_NAME_STRING(), __FUNCTION__);
                return -EINVAL;
            }
            SENSOR_DG("%s..%s : %x\n",SENSOR_NAME_STRING(),__FUNCTION__, value);
            return 0;
        }
    }
    SENSOR_TR("\n %s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
    return -EINVAL;
}
#endif
#if CONFIG_SENSOR_Flip
static int sensor_set_flip(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
    {
        if (sensor_FlipSeqe[value - qctrl->minimum] != NULL)
        {
            if (sensor_write_array(client, sensor_FlipSeqe[value - qctrl->minimum]) != 0)
            {
                SENSOR_TR("%s..%s WriteReg Fail.. \n",SENSOR_NAME_STRING(), __FUNCTION__);
                return -EINVAL;
            }
            SENSOR_DG("%s..%s : %x\n",SENSOR_NAME_STRING(),__FUNCTION__, value);
            return 0;
        }
    }
    SENSOR_TR("\n %s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
    return -EINVAL;
}
#endif
#if CONFIG_SENSOR_Scene
static int sensor_set_scene(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
    {
        if (sensor_SceneSeqe[value - qctrl->minimum] != NULL)
        {
            if (sensor_write_array(client, sensor_SceneSeqe[value - qctrl->minimum]) != 0)
            {
                SENSOR_TR("%s..%s WriteReg Fail.. \n",SENSOR_NAME_STRING(), __FUNCTION__);
                return -EINVAL;
            }
            SENSOR_DG("%s..%s : %x\n",SENSOR_NAME_STRING(),__FUNCTION__, value);
            return 0;
        }
    }
    SENSOR_TR("\n %s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
    return -EINVAL;
}
#endif
#if CONFIG_SENSOR_WhiteBalance
static int sensor_set_whiteBalance(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
    {
        if (sensor_WhiteBalanceSeqe[value - qctrl->minimum] != NULL)
        {
            if (sensor_write_array(client, sensor_WhiteBalanceSeqe[value - qctrl->minimum]) != 0)
            {
                SENSOR_TR("%s..%s WriteReg Fail.. \n",SENSOR_NAME_STRING(), __FUNCTION__);
                return -EINVAL;
            }
            SENSOR_DG("%s..%s : %x\n",SENSOR_NAME_STRING(),__FUNCTION__, value);
            return 0;
        }
    }
	SENSOR_TR("\n %s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
    return -EINVAL;
}
#endif
#if CONFIG_SENSOR_DigitalZoom
static int sensor_set_digitalzoom(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int *value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
    struct SENSOR *sensor = to_sensor(client);
	const struct v4l2_queryctrl *qctrl_info;
    int digitalzoom_cur, digitalzoom_total;

	qctrl_info = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_ZOOM_ABSOLUTE);
	if (qctrl_info)
		return -EINVAL;

    digitalzoom_cur = sensor->info_priv.digitalzoom;
    digitalzoom_total = qctrl_info->maximum;

    if ((value > 0) && (digitalzoom_cur >= digitalzoom_total))
    {
        SENSOR_TR("%s digitalzoom is maximum - %x\n", SENSOR_NAME_STRING(), digitalzoom_cur);
        return -EINVAL;
    }

    if  ((value < 0) && (digitalzoom_cur <= qctrl_info->minimum))
    {
        SENSOR_TR("%s digitalzoom is minimum - %x\n", SENSOR_NAME_STRING(), digitalzoom_cur);
        return -EINVAL;
    }

    if ((value > 0) && ((digitalzoom_cur + *value) > digitalzoom_total))
    {
        value = digitalzoom_total - digitalzoom_cur;
    }

    if ((value < 0) && ((digitalzoom_cur + *value) < 0))
    {
        value = 0 - digitalzoom_cur;
    }

    digitalzoom_cur += value;

    if (sensor_ZoomSeqe[digitalzoom_cur] != NULL)
    {
        if (sensor_write_array(client, sensor_ZoomSeqe[digitalzoom_cur]) != 0)
        {
            SENSOR_TR("%s..%s WriteReg Fail.. \n",SENSOR_NAME_STRING(), __FUNCTION__);
            return -EINVAL;
        }
        SENSOR_DG("%s..%s : %x\n",SENSOR_NAME_STRING(),__FUNCTION__, value);
        return 0;
    }

    return -EINVAL;
}
#endif
#if CONFIG_SENSOR_Flash
static int sensor_set_flash(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{    
    if ((value >= qctrl->minimum) && (value <= qctrl->maximum)) {
        if (value == 3) {       /* ddl@rock-chips.com: torch */
            sensor_ioctrl(icd, Sensor_Flash, Flash_Torch);   /* Flash On */
        } else {
            sensor_ioctrl(icd, Sensor_Flash, Flash_Off);
        }
        SENSOR_DG("%s..%s : %x\n",SENSOR_NAME_STRING(),__FUNCTION__, value);
        return 0;
    }
    
	SENSOR_TR("\n %s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
    return -EINVAL;
}
#endif

static int sensor_g_control(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct SENSOR *sensor = to_sensor(client);
    const struct v4l2_queryctrl *qctrl;

    qctrl = soc_camera_find_qctrl(&sensor_ops, ctrl->id);
//jack+
printk("jack+sensor_g_control\n");
//jack-
    if (!qctrl)
    {
        SENSOR_TR("\n %s ioctrl id = %d  is invalidate \n", SENSOR_NAME_STRING(), ctrl->id);
        return -EINVAL;
    }

    switch (ctrl->id)
    {
        case V4L2_CID_BRIGHTNESS:
            {
                ctrl->value = sensor->info_priv.brightness;
                break;
            }
        case V4L2_CID_SATURATION:
            {
                ctrl->value = sensor->info_priv.saturation;
                break;
            }
        case V4L2_CID_CONTRAST:
            {
                ctrl->value = sensor->info_priv.contrast;
                break;
            }
        case V4L2_CID_DO_WHITE_BALANCE:
            {
                ctrl->value = sensor->info_priv.whiteBalance;
                break;
            }
        case V4L2_CID_EXPOSURE:
            {
                ctrl->value = sensor->info_priv.exposure;
                break;
            }
        case V4L2_CID_HFLIP:
            {
                ctrl->value = sensor->info_priv.mirror;
                break;
            }
        case V4L2_CID_VFLIP:
            {
                ctrl->value = sensor->info_priv.flip;
                break;
            }
        default :
                break;
    }
    return 0;
}



static int sensor_s_control(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct SENSOR *sensor = to_sensor(client);
    struct soc_camera_device *icd = client->dev.platform_data;
    const struct v4l2_queryctrl *qctrl;


    qctrl = soc_camera_find_qctrl(&sensor_ops, ctrl->id);
    if (!qctrl)
    {
        SENSOR_TR("\n %s ioctrl id = %d  is invalidate \n", SENSOR_NAME_STRING(), ctrl->id);
        return -EINVAL;
    }

    switch (ctrl->id)
    {
#if CONFIG_SENSOR_Brightness
        case V4L2_CID_BRIGHTNESS:
            {
                if (ctrl->value != sensor->info_priv.brightness)
                {
                    if (sensor_set_brightness(icd, qctrl,ctrl->value) != 0)
                    {
                        return -EINVAL;
                    }
                    sensor->info_priv.brightness = ctrl->value;
                }
                break;
            }
#endif
#if CONFIG_SENSOR_Exposure
        case V4L2_CID_EXPOSURE:
            {
                if (ctrl->value != sensor->info_priv.exposure)
                {
                    if (sensor_set_exposure(icd, qctrl,ctrl->value) != 0)
                    {
                        return -EINVAL;
                    }
                    sensor->info_priv.exposure = ctrl->value;
                }
                break;
            }
#endif
#if CONFIG_SENSOR_Saturation
        case V4L2_CID_SATURATION:
            {
                if (ctrl->value != sensor->info_priv.saturation)
                {
                    if (sensor_set_saturation(icd, qctrl,ctrl->value) != 0)
                    {
                        return -EINVAL;
                    }
                    sensor->info_priv.saturation = ctrl->value;
                }
                break;
            }
#endif
#if CONFIG_SENSOR_Contrast
        case V4L2_CID_CONTRAST:
            {
                if (ctrl->value != sensor->info_priv.contrast)
                {
                    if (sensor_set_contrast(icd, qctrl,ctrl->value) != 0)
                    {
                        return -EINVAL;
                    }
                    sensor->info_priv.contrast = ctrl->value;
                }
                break;
            }
#endif
#if CONFIG_SENSOR_WhiteBalance
        case V4L2_CID_DO_WHITE_BALANCE:
            {
                if (ctrl->value != sensor->info_priv.whiteBalance)
                {
                    if (sensor_set_whiteBalance(icd, qctrl,ctrl->value) != 0)
                    {
                        return -EINVAL;
                    }
                    sensor->info_priv.whiteBalance = ctrl->value;
                }
                break;
            }
#endif
#if CONFIG_SENSOR_Mirror
        case V4L2_CID_HFLIP:
            {
                if (ctrl->value != sensor->info_priv.mirror)
                {
                    if (sensor_set_mirror(icd, qctrl,ctrl->value) != 0)
                        return -EINVAL;
                    sensor->info_priv.mirror = ctrl->value;
                }
                break;
            }
#endif
#if CONFIG_SENSOR_Flip
        case V4L2_CID_VFLIP:
            {
                if (ctrl->value != sensor->info_priv.flip)
                {
                    if (sensor_set_flip(icd, qctrl,ctrl->value) != 0)
                        return -EINVAL;
                    sensor->info_priv.flip = ctrl->value;
                }
                break;
            }
#endif
        default:
            break;
    }

    return 0;
}
static int sensor_g_ext_control(struct soc_camera_device *icd , struct v4l2_ext_control *ext_ctrl)
{
    const struct v4l2_queryctrl *qctrl;
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
    struct SENSOR *sensor = to_sensor(client);

    qctrl = soc_camera_find_qctrl(&sensor_ops, ext_ctrl->id);

    if (!qctrl)
    {
        SENSOR_TR("\n %s ioctrl id = %d  is invalidate \n", SENSOR_NAME_STRING(), ext_ctrl->id);
        return -EINVAL;
    }

    switch (ext_ctrl->id)
    {
        case V4L2_CID_SCENE:
            {
                ext_ctrl->value = sensor->info_priv.scene;
                break;
            }
        case V4L2_CID_EFFECT:
            {
                ext_ctrl->value = sensor->info_priv.effect;
                break;
            }
        case V4L2_CID_ZOOM_ABSOLUTE:
            {
                ext_ctrl->value = sensor->info_priv.digitalzoom;
                break;
            }
        case V4L2_CID_ZOOM_RELATIVE:
            {
                return -EINVAL;
            }
        case V4L2_CID_FOCUS_ABSOLUTE:
            {
                ext_ctrl->value = sensor->info_priv.focus;
                break;
            }
        case V4L2_CID_FOCUS_RELATIVE:
            {
                return -EINVAL;
            }
        case V4L2_CID_FLASH:
            {
                ext_ctrl->value = sensor->info_priv.flash;
                break;
            }
        default :
            break;
    }
    return 0;
}
static int sensor_s_ext_control(struct soc_camera_device *icd, struct v4l2_ext_control *ext_ctrl)
{
    const struct v4l2_queryctrl *qctrl;
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
    struct SENSOR *sensor = to_sensor(client);
    int val_offset;

    qctrl = soc_camera_find_qctrl(&sensor_ops, ext_ctrl->id);
    
    if (!qctrl)
    {
        SENSOR_TR("\n %s ioctrl id = %d  is invalidate \n", SENSOR_NAME_STRING(), ext_ctrl->id);
        return -EINVAL;
    }

	val_offset = 0;
    switch (ext_ctrl->id)
    {
#if CONFIG_SENSOR_Scene
        case V4L2_CID_SCENE:
            {
                if (ext_ctrl->value != sensor->info_priv.scene)
                {
                    if (sensor_set_scene(icd, qctrl,ext_ctrl->value) != 0)
                        return -EINVAL;
                    sensor->info_priv.scene = ext_ctrl->value;
                }
                break;
            }
#endif
#if CONFIG_SENSOR_Effect
        case V4L2_CID_EFFECT:
            {
                if (ext_ctrl->value != sensor->info_priv.effect)
                {
                    if (sensor_set_effect(icd, qctrl,ext_ctrl->value) != 0)
                        return -EINVAL;
                    sensor->info_priv.effect= ext_ctrl->value;
                }
                break;
            }
#endif
#if CONFIG_SENSOR_DigitalZoom
        case V4L2_CID_ZOOM_ABSOLUTE:
            {
                if ((ext_ctrl->value < qctrl->minimum) || (ext_ctrl->value > qctrl->maximum))
                    return -EINVAL;

                if (ext_ctrl->value != sensor->info_priv.digitalzoom)
                {
                    val_offset = ext_ctrl->value -sensor->info_priv.digitalzoom;

                    if (sensor_set_digitalzoom(icd, qctrl,&val_offset) != 0)
                        return -EINVAL;
                    sensor->info_priv.digitalzoom += val_offset;

                    SENSOR_DG("%s digitalzoom is %x\n",SENSOR_NAME_STRING(),  sensor->info_priv.digitalzoom);
                }

                break;
            }
        case V4L2_CID_ZOOM_RELATIVE:
            {
                if (ext_ctrl->value)
                {
                    if (sensor_set_digitalzoom(icd, qctrl,&ext_ctrl->value) != 0)
                        return -EINVAL;
                    sensor->info_priv.digitalzoom += ext_ctrl->value;

                    SENSOR_DG("%s digitalzoom is %x\n", SENSOR_NAME_STRING(), sensor->info_priv.digitalzoom);
                }
                break;
            }
#endif
#if CONFIG_SENSOR_Focus
        case V4L2_CID_FOCUS_ABSOLUTE:
            {
                if ((ext_ctrl->value < qctrl->minimum) || (ext_ctrl->value > qctrl->maximum))
                    return -EINVAL;

                if (ext_ctrl->value != sensor->info_priv.focus)
                {
                    val_offset = ext_ctrl->value -sensor->info_priv.focus;

                    sensor->info_priv.focus += val_offset;
                }

                break;
            }
        case V4L2_CID_FOCUS_RELATIVE:
            {
                if (ext_ctrl->value)
                {
                    sensor->info_priv.focus += ext_ctrl->value;

                    SENSOR_DG("%s focus is %x\n", SENSOR_NAME_STRING(), sensor->info_priv.focus);
                }
                break;
            }
#endif
#if CONFIG_SENSOR_Flash
        case V4L2_CID_FLASH:
            {
                if (sensor_set_flash(icd, qctrl,ext_ctrl->value) != 0)
                    return -EINVAL;
                sensor->info_priv.flash = ext_ctrl->value;

                SENSOR_DG("%s flash is %x\n",SENSOR_NAME_STRING(), sensor->info_priv.flash);
                break;
            }
#endif
        default:
            break;
    }

    return 0;
}

static int sensor_g_ext_controls(struct v4l2_subdev *sd, struct v4l2_ext_controls *ext_ctrl)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct soc_camera_device *icd = client->dev.platform_data;
    int i, error_cnt=0, error_idx=-1;

    for (i=0; i<ext_ctrl->count; i++) {
        if (sensor_g_ext_control(icd, &ext_ctrl->controls[i]) != 0) {
            error_cnt++;
            error_idx = i;
        }
    }

    if (error_cnt > 1)
        error_idx = ext_ctrl->count;

    if (error_idx != -1) {
        ext_ctrl->error_idx = error_idx;
        return -EINVAL;
    } else {
        return 0;
    }
}

static int sensor_s_ext_controls(struct v4l2_subdev *sd, struct v4l2_ext_controls *ext_ctrl)
{
   struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct soc_camera_device *icd = client->dev.platform_data;
    int i, error_cnt=0, error_idx=-1;

    for (i=0; i<ext_ctrl->count; i++) {
        if (sensor_s_ext_control(icd, &ext_ctrl->controls[i]) != 0) {
            error_cnt++;
            error_idx = i;
        }
    }

    if (error_cnt > 1)
        error_idx = ext_ctrl->count;

    if (error_idx != -1) {
        ext_ctrl->error_idx = error_idx;
        return -EINVAL;
    } else {
        return 0;
    }
}

/* Interface active, can use i2c. If it fails, it can indeed mean, that
 * this wasn't our capture interface, so, we wait for the right one */
static int sensor_video_probe(struct soc_camera_device *icd,
			       struct i2c_client *client)
{
unsigned char value;
int ret,pid = 0;
    struct SENSOR *sensor = to_sensor(client);

    /* We must have a parent by now. And it cannot be a wrong one.
     * So this entire test is completely redundant. */
    if (!icd->dev.parent ||
	    to_soc_camera_host(icd->dev.parent)->nr != icd->iface)
		return -ENODEV;

	if (sensor_ioctrl(icd, Sensor_PowerDown, 0) < 0) {
		ret = -ENODEV;
		goto sensor_video_probe_err;
	}

    /* soft reset */
    mdelay(5);          //delay 5 microseconds

    /* check if it is an sensor sensor */
    ret = sensor_read(client, 0x300a, &value);
    if (ret != 0) {
        SENSOR_TR("read chip id high byte failed\n");
        ret = -ENODEV;
        goto sensor_video_probe_err;
    }

    pid |= (value << 8);

    ret = sensor_read(client, 0x300b, &value);
    if (ret != 0) {
        SENSOR_TR("read chip id low byte failed\n");
        ret = -ENODEV;
        goto sensor_video_probe_err;
    }

    pid |= (value & 0xff);
    SENSOR_DG("\n %s  pid = 0x%x\n", SENSOR_NAME_STRING(), pid);
    if (pid == SENSOR_ID) {
        sensor->model = SENSOR_V4L2_IDENT;
    } else {
        SENSOR_TR("error: %s mismatched   pid = 0x%x\n", SENSOR_NAME_STRING(), pid);
        ret = -ENODEV;
        goto sensor_video_probe_err;
    }

    //icd->formats = sensor_colour_formats;
    //icd->num_formats = ARRAY_SIZE(sensor_colour_formats);

    return 0;

sensor_video_probe_err:

    return ret;
}

static long sensor_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct soc_camera_device *icd = client->dev.platform_data;
    struct SENSOR *sensor = to_sensor(client);
    int ret = 0;
	#if CONFIG_SENSOR_Flash	
    int i;
#endif
    printk("jack+sensor_ioctl\n");  //jack++
	SENSOR_DG("\n%s..%s..cmd:%x \n",SENSOR_NAME_STRING(),__FUNCTION__,cmd);
	switch (cmd)
	{
		case RK29_CAM_SUBDEV_DEACTIVATE:
		{
			sensor_deactivate(client);
			break;
		}

		case RK29_CAM_SUBDEV_IOREQUEST:
		{
			sensor->sensor_io_request = (struct rk29camera_platform_data*)arg;           
            if (sensor->sensor_io_request != NULL) { 
                if (sensor->sensor_io_request->gpio_res[0].dev_name && 
                    (strcmp(sensor->sensor_io_request->gpio_res[0].dev_name, dev_name(icd->pdev)) == 0)) {
                    sensor->sensor_gpio_res = (struct rk29camera_gpio_res*)&sensor->sensor_io_request->gpio_res[0];
                } else if (sensor->sensor_io_request->gpio_res[1].dev_name && 
                    (strcmp(sensor->sensor_io_request->gpio_res[1].dev_name, dev_name(icd->pdev)) == 0)) {
                    sensor->sensor_gpio_res = (struct rk29camera_gpio_res*)&sensor->sensor_io_request->gpio_res[1];
                }
            } else {
                SENSOR_TR("%s %s RK29_CAM_SUBDEV_IOREQUEST fail\n",SENSOR_NAME_STRING(),__FUNCTION__);
                ret = -EINVAL;
                goto sensor_ioctl_end;
            }
            /* ddl@rock-chips.com : if gpio_flash havn't been set in board-xxx.c, sensor driver must notify is not support flash control 
               for this project */
            #if CONFIG_SENSOR_Flash	
        	if (sensor->sensor_gpio_res) { 
                if (sensor->sensor_gpio_res->gpio_flash == INVALID_GPIO) {
                    for (i = 0; i < icd->ops->num_controls; i++) {
                		if (V4L2_CID_FLASH == icd->ops->controls[i].id) {
                		//	memset((char*)&icd->ops->controls[i],0x00,sizeof(struct v4l2_queryctrl));                			
						sensor_controls[i].id=0xffff;
                		}
                    }
                    sensor->info_priv.flash = 0xff;
                    SENSOR_DG("%s flash gpio is invalidate!\n",SENSOR_NAME_STRING());
                }else{ //two cameras are the same,need to deal diffrently ,zyc
                    for (i = 0; i < icd->ops->num_controls; i++) {
                           if(0xffff == icd->ops->controls[i].id){
                             sensor_controls[i].id=V4L2_CID_FLASH;
                           }               
                    }
                }
        	}
            #endif
			break;
		}
		default:
		{
			SENSOR_TR("%s %s cmd(0x%x) is unknown !\n",SENSOR_NAME_STRING(),__FUNCTION__,cmd);
			break;
		}
	}
sensor_ioctl_end:
	return ret;

}

static int sensor_enum_fmt(struct v4l2_subdev *sd, unsigned int index,
			    enum v4l2_mbus_pixelcode *code)
{
	if (index >= ARRAY_SIZE(sensor_colour_fmts))
		return -EINVAL;

	*code = sensor_colour_fmts[index].code;
	return 0;
}

static struct v4l2_subdev_core_ops sensor_subdev_core_ops = {
	.init		= sensor_init,
	.g_ctrl		= sensor_g_control,
	.s_ctrl		= sensor_s_control,
	.g_ext_ctrls          = sensor_g_ext_controls,
	.s_ext_ctrls          = sensor_s_ext_controls,
	.g_chip_ident	= sensor_g_chip_ident,
	.ioctl = sensor_ioctl,
};

static struct v4l2_subdev_video_ops sensor_subdev_video_ops = {
	.s_mbus_fmt	= sensor_s_fmt,
	.g_mbus_fmt	= sensor_g_fmt,
	.try_mbus_fmt	= sensor_try_fmt,
	.enum_mbus_fmt	= sensor_enum_fmt,
};


static struct v4l2_subdev_ops sensor_subdev_ops = {
	.core	= &sensor_subdev_core_ops,
	.video = &sensor_subdev_video_ops,
};

static int sensor_probe(struct i2c_client *client,
			 const struct i2c_device_id *did)
{
    struct SENSOR *sensor;
    struct soc_camera_device *icd = client->dev.platform_data;
    struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
    struct soc_camera_link *icl;
    int ret;

    SENSOR_DG("\n%s..%s..%d..\n",__FUNCTION__,__FILE__,__LINE__);
    if (!icd) {
        dev_err(&client->dev, "%s: missing soc-camera data!\n",SENSOR_NAME_STRING());
        return -EINVAL;
    }

    icl = to_soc_camera_link(icd);
    if (!icl) {
        dev_err(&client->dev, "%s driver needs platform data\n", SENSOR_NAME_STRING());
        return -EINVAL;
    }

    if (!i2c_check_functionality(adapter, I2C_FUNC_I2C)) {
        dev_warn(&adapter->dev,
        	 "I2C-Adapter doesn't support I2C_FUNC_I2C\n");
        return -EIO;
    }

    sensor = kzalloc(sizeof(struct SENSOR), GFP_KERNEL);
    if (!sensor)
        return -ENOMEM;

    v4l2_i2c_subdev_init(&sensor->subdev, client, &sensor_subdev_ops);

    /* Second stage probe - when a capture adapter is there */
    icd->ops		= &sensor_ops;
    sensor->info_priv.fmt = sensor_colour_fmts[0];
	#if CONFIG_SENSOR_I2C_NOSCHED
	atomic_set(&sensor->tasklock_cnt,0);
	#endif

    ret = sensor_video_probe(icd, client);
    if (ret < 0) {
        icd->ops = NULL;
        i2c_set_clientdata(client, NULL);
        kfree(sensor);
		sensor = NULL;
    }

    #if CONFIG_SENSOR_Flash
    hrtimer_init(&(flash_off_timer.timer), CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	flash_off_timer.timer.function = flash_off_func;
    #endif
    
    SENSOR_DG("\n%s..%s..%d  ret = %x \n",__FUNCTION__,__FILE__,__LINE__,ret);
    return ret;
}

static int sensor_remove(struct i2c_client *client)
{
    struct SENSOR *sensor = to_sensor(client);
    struct soc_camera_device *icd = client->dev.platform_data;

    icd->ops = NULL;
    i2c_set_clientdata(client, NULL);
    client->driver = NULL;
    kfree(sensor);
	sensor = NULL;
    return 0;
}

static const struct i2c_device_id sensor_id[] = {
	{SENSOR_NAME_STRING(), 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sensor_id);

static struct i2c_driver sensor_i2c_driver = {
	.driver = {
		.name = SENSOR_NAME_STRING(),
	},
	.probe		= sensor_probe,
	.remove		= sensor_remove,
	.id_table	= sensor_id,
};

static int __init sensor_mod_init(void)
{

    SENSOR_DG("\n%s..%s.. \n",__FUNCTION__,SENSOR_NAME_STRING());
    return i2c_add_driver(&sensor_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
    i2c_del_driver(&sensor_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION(SENSOR_NAME_STRING(Camera sensor driver));
MODULE_AUTHOR("ddl <kernel@rock-chips>");
MODULE_LICENSE("GPL");



