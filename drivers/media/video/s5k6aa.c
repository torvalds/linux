/*
 * Driver for OV5642 CMOS Image Sensor from OmniVision
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
#include <linux/delay.h>
#include "s5k6aa.h"
static int debug;
module_param(debug, int, S_IRUGO|S_IWUSR);

#define dprintk(level, fmt, arg...) do {			\
	if (debug >= level) 					\
	printk(KERN_WARNING fmt , ## arg); } while (0)

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
#define SENSOR_NAME RK29_CAM_SENSOR_S5K6AA
#define SENSOR_V4L2_IDENT V4L2_IDENT_S5K66A 
#define SENSOR_ID 0x06aa
#define SENSOR_ID_REG SEQUENCE_END//0x015a
#define SENSOR_RESET_REG SEQUENCE_END
#define SENSOR_RESET_VAL 0x00
#define SENSOR_MIN_WIDTH    640
#define SENSOR_MIN_HEIGHT   480
#define SENSOR_MAX_WIDTH    1280
#define SENSOR_MAX_HEIGHT   1024
#define SENSOR_INIT_WIDTH	640			/* Sensor pixel size for sensor_init_data array */
#define SENSOR_INIT_HEIGHT  480
#define SENSOR_INIT_WINSEQADR sensor_vga
#define SENSOR_INIT_PIXFMT V4L2_MBUS_FMT_YUYV8_2X8

#define CONFIG_SENSOR_WhiteBalance	0
#define CONFIG_SENSOR_Brightness	0
#define CONFIG_SENSOR_Contrast      0
#define CONFIG_SENSOR_Saturation    0
#define CONFIG_SENSOR_Effect        0
#define CONFIG_SENSOR_Scene         0
#define CONFIG_SENSOR_DigitalZoom   0
#define CONFIG_SENSOR_Focus         0
#define CONFIG_SENSOR_Exposure      0
#define CONFIG_SENSOR_Flash         0
#define CONFIG_SENSOR_Mirror        1
#define CONFIG_SENSOR_Flip          0
#define CONFIG_SENSOR_Focus         0


#define CONFIG_SENSOR_I2C_SPEED     250000       /* Hz */
/* Sensor write register continues by preempt_disable/preempt_enable for current process not be scheduled */
#define CONFIG_SENSOR_I2C_NOSCHED   0
#define CONFIG_SENSOR_I2C_RDWRCHK   0


#define SENSOR_BUS_PARAM  (SOCAM_MASTER | SOCAM_PCLK_SAMPLE_RISING |\
                          SOCAM_HSYNC_ACTIVE_HIGH | SOCAM_VSYNC_ACTIVE_HIGH |\
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

#if CONFIG_SENSOR_Focus
#define SENSOR_AF_MODE_INFINITY    0
#define SENSOR_AF_MODE_MACRO       1
#define SENSOR_AF_MODE_FIXED       2
#define SENSOR_AF_MODE_AUTO        3
#define SENSOR_AF_MODE_CONTINUOUS  4
#define SENSOR_AF_MODE_CLOSE       5
#endif

//flash off in fixed time to prevent from too hot , zyc
struct  flash_timer{
    struct soc_camera_device *icd;
	struct hrtimer timer;
};
static enum hrtimer_restart flash_off_func(struct hrtimer *timer);

static struct  flash_timer flash_off_timer;
//for user defined if user want to customize the series , zyc
#if CONFIG_S5K6AA_USER_DEFINED_SERIES
#include "s5k6aa_user_series.c"
#else

/* init 640X480 SVGA */
static struct reginfo sensor_init_data[] =
{
	{0x0010,0x0001}, 	// Reset
	{0x1030,0x0000}, 	// Clear host interrupt so main will wait
	{0x0014,0x0001}, 	// ARM go
	// Wait100mSec
	{SEQUENCE_WAIT_MS,50},
	// Start TP part
	{0x0028,0x7000}, 	// start add MSW
	{0x002A,0x1d60}, 	// start add LSW
	{0x0F12,0xb570},
	{0x0F12,0x4928},
	{0x0F12,0x4828},
	{0x0F12,0x2205},
	{0x0F12,0xf000},
	{0x0F12,0xf922},
	{0x0F12,0x4927},
	{0x0F12,0x2002},
	{0x0F12,0x83c8},
	{0x0F12,0x2001},
	{0x0F12,0x3120},
	{0x0F12,0x8088},
	{0x0F12,0x4925},
	{0x0F12,0x4826},
	{0x0F12,0x2204},
	{0x0F12,0xf000},
	{0x0F12,0xf917},
	{0x0F12,0x4925},
	{0x0F12,0x4825},
	{0x0F12,0x2206},
	{0x0F12,0xf000},
	{0x0F12,0xf912},
	{0x0F12,0x4924},
	{0x0F12,0x4825},
	{0x0F12,0x2207},
	{0x0F12,0xf000},
	{0x0F12,0xf90d},
	{0x0F12,0x4924},
	{0x0F12,0x4824},
	{0x0F12,0x2208},
	{0x0F12,0xf000},
	{0x0F12,0xf908},
	{0x0F12,0x4923},
	{0x0F12,0x4824},
	{0x0F12,0x2209},
	{0x0F12,0xf000},
	{0x0F12,0xf903},
	{0x0F12,0x4923},
	{0x0F12,0x4823},
	{0x0F12,0x60c1},
	{0x0F12,0x6882},
	{0x0F12,0x1a51},
	{0x0F12,0x8201},
	{0x0F12,0x4c22},
	{0x0F12,0x2607},
	{0x0F12,0x6821},
	{0x0F12,0x0736},
	{0x0F12,0x42b1},
	{0x0F12,0xda05},
	{0x0F12,0x4820},
	{0x0F12,0x22d8},
	{0x0F12,0x1c05},
	{0x0F12,0xf000},
	{0x0F12,0xf8fa},
	{0x0F12,0x6025},
	{0x0F12,0x68a1},
	{0x0F12,0x42b1},
	{0x0F12,0xda07},
	{0x0F12,0x481b},
	{0x0F12,0x2224},
	{0x0F12,0x3824},
	{0x0F12,0xf000},
	{0x0F12,0xf8f1},
	{0x0F12,0x4819},
	{0x0F12,0x3824},
	{0x0F12,0x60a0},
	{0x0F12,0x4d18},
	{0x0F12,0x6d29},
	{0x0F12,0x42b1},
	{0x0F12,0xda07},
	{0x0F12,0x4815},
	{0x0F12,0x228f},
	{0x0F12,0x00d2},
	{0x0F12,0x30d8},
	{0x0F12,0x1c04},
	{0x0F12,0xf000},
	{0x0F12,0xf8e3},
	{0x0F12,0x652c},
	{0x0F12,0xbc70},
	{0x0F12,0xbc08},
	{0x0F12,0x4718},
	{0x0F12,0x0000},
	{0x0F12,0x1f53},
	{0x0F12,0x7000},
	{0x0F12,0x127b},
	{0x0F12,0x0000},
	{0x0F12,0x0398},
	{0x0F12,0x7000},
	{0x0F12,0x1e4d},
	{0x0F12,0x7000},
	{0x0F12,0x890d},
	{0x0F12,0x0000},
	{0x0F12,0x1e73},
	{0x0F12,0x7000},
	{0x0F12,0x27a9},
	{0x0F12,0x0000},
	{0x0F12,0x1e91},
	{0x0F12,0x7000},
	{0x0F12,0x27c5},
	{0x0F12,0x0000},
	{0x0F12,0x1ef7},
	{0x0F12,0x7000},
	{0x0F12,0x285f},
	{0x0F12,0x0000},
	{0x0F12,0x1eb3},
	{0x0F12,0x7000},
	{0x0F12,0x28ff},
	{0x0F12,0x0000},
	{0x0F12,0x206c},
	{0x0F12,0x7000},
	{0x0F12,0x04ac},
	{0x0F12,0x7000},
	{0x0F12,0x06cc},
	{0x0F12,0x7000},
	{0x0F12,0x23a4},
	{0x0F12,0x7000},
	{0x0F12,0x0704},
	{0x0F12,0x7000},
	{0x0F12,0xb510},
	{0x0F12,0x1c04},
	{0x0F12,0x484d},
	{0x0F12,0xf000},
	{0x0F12,0xf8bb},
	{0x0F12,0x4a4d},
	{0x0F12,0x4b4d},
	{0x0F12,0x8811},
	{0x0F12,0x885b},
	{0x0F12,0x8852},
	{0x0F12,0x4359},
	{0x0F12,0x1889},
	{0x0F12,0x4288},
	{0x0F12,0xd800},
	{0x0F12,0x1c08},
	{0x0F12,0x6020},
	{0x0F12,0xbc10},
	{0x0F12,0xbc08},
	{0x0F12,0x4718},
	{0x0F12,0xb510},
	{0x0F12,0x1c04},
	{0x0F12,0xf000},
	{0x0F12,0xf8b1},
	{0x0F12,0x4944},
	{0x0F12,0x8989},
	{0x0F12,0x4348},
	{0x0F12,0x0200},
	{0x0F12,0x0c00},
	{0x0F12,0x2101},
	{0x0F12,0x0349},
	{0x0F12,0xf000},
	{0x0F12,0xf8b0},
	{0x0F12,0x6020},
	{0x0F12,0xe7ed},
	{0x0F12,0xb510},
	{0x0F12,0x1c04},
	{0x0F12,0xf000},
	{0x0F12,0xf8b2},
	{0x0F12,0x6821},
	{0x0F12,0x0409},
	{0x0F12,0x0c09},
	{0x0F12,0x1a40},
	{0x0F12,0x493a},
	{0x0F12,0x6849},
	{0x0F12,0x4281},
	{0x0F12,0xd800},
	{0x0F12,0x1c08},
	{0x0F12,0xf000},
	{0x0F12,0xf8af},
	{0x0F12,0x6020},
	{0x0F12,0xe7dc},
	{0x0F12,0xb570},
	{0x0F12,0x6801},
	{0x0F12,0x040d},
	{0x0F12,0x0c2d},
	{0x0F12,0x6844},
	{0x0F12,0x4833},
	{0x0F12,0x8981},
	{0x0F12,0x1c28},
	{0x0F12,0xf000},
	{0x0F12,0xf893},
	{0x0F12,0x8060},
	{0x0F12,0x4932},
	{0x0F12,0x69c9},
	{0x0F12,0xf000},
	{0x0F12,0xf8a6},
	{0x0F12,0x1c01},
	{0x0F12,0x80a0},
	{0x0F12,0x0228},
	{0x0F12,0xf000},
	{0x0F12,0xf8a9},
	{0x0F12,0x0400},
	{0x0F12,0x0c00},
	{0x0F12,0x8020},
	{0x0F12,0x492d},
	{0x0F12,0x2300},
	{0x0F12,0x5ec9},
	{0x0F12,0x4288},
	{0x0F12,0xda02},
	{0x0F12,0x20ff},
	{0x0F12,0x3001},
	{0x0F12,0x8020},
	{0x0F12,0xbc70},
	{0x0F12,0xbc08},
	{0x0F12,0x4718},
	{0x0F12,0xb570},
	{0x0F12,0x1c04},
	{0x0F12,0x4828},
	{0x0F12,0x4926},
	{0x0F12,0x7803},
	{0x0F12,0x6a8a},
	{0x0F12,0x2b00},
	{0x0F12,0xd100},
	{0x0F12,0x6a0a},
	{0x0F12,0x4d20},
	{0x0F12,0x2b00},
	{0x0F12,0x68a8},
	{0x0F12,0xd100},
	{0x0F12,0x6868},
	{0x0F12,0x6823},
	{0x0F12,0x8dc9},
	{0x0F12,0x434a},
	{0x0F12,0x0a12},
	{0x0F12,0x429a},
	{0x0F12,0xd30d},
	{0x0F12,0x4d20},
	{0x0F12,0x26ff},
	{0x0F12,0x8828},
	{0x0F12,0x3601},
	{0x0F12,0x43b0},
	{0x0F12,0x8028},
	{0x0F12,0x6820},
	{0x0F12,0xf000},
	{0x0F12,0xf884},
	{0x0F12,0x6020},
	{0x0F12,0x8828},
	{0x0F12,0x4330},
	{0x0F12,0x8028},
	{0x0F12,0xe7da},
	{0x0F12,0x1c0a},
	{0x0F12,0x4342},
	{0x0F12,0x0a12},
	{0x0F12,0x429a},
	{0x0F12,0xd304},
	{0x0F12,0x0218},
	{0x0F12,0xf000},
	{0x0F12,0xf871},
	{0x0F12,0x6020},
	{0x0F12,0xe7f4},
	{0x0F12,0x6020},
	{0x0F12,0xe7f2},
	{0x0F12,0xb510},
	{0x0F12,0x4913},
	{0x0F12,0x8fc8},
	{0x0F12,0x2800},
	{0x0F12,0xd007},
	{0x0F12,0x2000},
	{0x0F12,0x87c8},
	{0x0F12,0x8f88},
	{0x0F12,0x4c11},
	{0x0F12,0x2800},
	{0x0F12,0xd002},
	{0x0F12,0x2008},
	{0x0F12,0x8020},
	{0x0F12,0xe77e},
	{0x0F12,0x480d},
	{0x0F12,0x3060},
	{0x0F12,0x8900},
	{0x0F12,0x2800},
	{0x0F12,0xd103},
	{0x0F12,0x480c},
	{0x0F12,0x2101},
	{0x0F12,0xf000},
	{0x0F12,0xf864},
	{0x0F12,0x2010},
	{0x0F12,0x8020},
	{0x0F12,0xe7f2},
	{0x0F12,0x0000},
	{0x0F12,0xf4b0},
	{0x0F12,0x0000},
	{0x0F12,0x2058},
	{0x0F12,0x7000},
	{0x0F12,0x1554},
	{0x0F12,0x7000},
	{0x0F12,0x0080},
	{0x0F12,0x7000},
	{0x0F12,0x046c},
	{0x0F12,0x7000},
	{0x0F12,0x0468},
	{0x0F12,0x7000},
	{0x0F12,0x1100},
	{0x0F12,0xd000},
	{0x0F12,0x01b8},
	{0x0F12,0x7000},
	{0x0F12,0x044e},
	{0x0F12,0x7000},
	{0x0F12,0x0450},
	{0x0F12,0x7000},
	{0x0F12,0x4778},
	{0x0F12,0x46c0},
	{0x0F12,0xc000},
	{0x0F12,0xe59f},
	{0x0F12,0xff1c},
	{0x0F12,0xe12f},
	{0x0F12,0x9ce7},
	{0x0F12,0x0000},
	{0x0F12,0x4778},
	{0x0F12,0x46c0},
	{0x0F12,0xf004},
	{0x0F12,0xe51f},
	{0x0F12,0x9fb8},
	{0x0F12,0x0000},
	{0x0F12,0x4778},
	{0x0F12,0x46c0},
	{0x0F12,0xc000},
	{0x0F12,0xe59f},
	{0x0F12,0xff1c},
	{0x0F12,0xe12f},
	{0x0F12,0x88df},
	{0x0F12,0x0000},
	{0x0F12,0x4778},
	{0x0F12,0x46c0},
	{0x0F12,0xc000},
	{0x0F12,0xe59f},
	{0x0F12,0xff1c},
	{0x0F12,0xe12f},
	{0x0F12,0x275d},
	{0x0F12,0x0000},
	{0x0F12,0x4778},
	{0x0F12,0x46c0},
	{0x0F12,0xc000},
	{0x0F12,0xe59f},
	{0x0F12,0xff1c},
	{0x0F12,0xe12f},
	{0x0F12,0x1ed3},
	{0x0F12,0x0000},
	{0x0F12,0x4778},
	{0x0F12,0x46c0},
	{0x0F12,0xc000},
	{0x0F12,0xe59f},
	{0x0F12,0xff1c},
	{0x0F12,0xe12f},
	{0x0F12,0x26f9},
	{0x0F12,0x0000},
	{0x0F12,0x4778},
	{0x0F12,0x46c0},
	{0x0F12,0xc000},
	{0x0F12,0xe59f},
	{0x0F12,0xff1c},
	{0x0F12,0xe12f},
	{0x0F12,0x4027},
	{0x0F12,0x0000},
	{0x0F12,0x4778},
	{0x0F12,0x46c0},
	{0x0F12,0xc000},
	{0x0F12,0xe59f},
	{0x0F12,0xff1c},
	{0x0F12,0xe12f},
	{0x0F12,0x9f03},
	{0x0F12,0x0000},
	{0x0F12,0x4778},
	{0x0F12,0x46c0},
	{0x0F12,0xf004},
	{0x0F12,0xe51f},
	{0x0F12,0xa144},
	{0x0F12,0x0000},
	{0x0F12,0x4778},
	{0x0F12,0x46c0},
	{0x0F12,0xc000},
	{0x0F12,0xe59f},
	{0x0F12,0xff1c},
	{0x0F12,0xe12f},
	{0x0F12,0x285f},
	{0x0F12,0x0000},
	{0x0F12,0x4778},
	{0x0F12,0x46c0},
	{0x0F12,0xc000},
	{0x0F12,0xe59f},
	{0x0F12,0xff1c},
	{0x0F12,0xe12f},
	{0x0F12,0x2001},
	{0x0F12,0x0000},
	{0x0F12,0x0000},
	{0x0F12,0x0000},
	{0x0F12,0xe848},
	{0x0F12,0x0001},
	{0x0F12,0xe848},
	{0x0F12,0x0001},
	{0x0F12,0x0500},
	{0x0F12,0x0000},
	{0x0F12,0x0000},
	{0x0F12,0x0000},
	// End TP part


	// Start tuning part

	//	Write memory example:
	//	m70003000 10CE
	//	Write HW register example:
	//	1004 5678

	{0x002A,0x0e3a},  // #awbb_Alpha_Comp_Mode
	{0x0F12,0x02C2},

	// Analog Settings
	{0x002A,0x112a},  //#senHal_SenRegsModes3_pSenModesRegsArray3[8]
	{0x0F12,0x0000},
	{0x002A,0x1132},  //#senHal_SenRegsModes3_pSenModesRegsArray3[12]
	{0x0F12,0x0000},
	{0x002A,0x113e},  //#senHal_SenRegsModes3_pSenModesRegsArray3[18]
	{0x0F12,0x0000},
	{0x002A,0x115c},  //#senHal_SenRegsModes3_pSenModesRegsArray3[33]
	{0x0F12,0x0000},
	{0x002A,0x1164},  //#senHal_SenRegsModes3_pSenModesRegsArray3[37]
	{0x0F12,0x0000},
	{0x002A,0x1174},  //#senHal_SenRegsModes3_pSenModesRegsArray3[45]
	{0x0F12,0x0000},
	{0x002A,0x1178},  //#senHal_SenRegsModes3_pSenModesRegsArray3[47]
	{0x0F12,0x0000},

	{0x002A,0x077a},  //#msm_uOffsetNoBin[0][0]
	{0x0F12,0x0000},  //#msm_uOffsetNoBin[0][1]
	{0x0F12,0x0000}, 	//#msm_uOffsetNoBin[1][0]
	{0x0F12,0x0000}, 	//#msm_uOffsetNoBin[1][1]
	{0x0F12,0x0000}, 	//#msm_uOffsetNoBin[2][0]
	{0x0F12,0x0000}, 	//#msm_uOffsetNoBin[2][1]
	{0x0F12,0x0000}, 	//#msm_uOffsetNoBin[3][0]
	{0x0F12,0x0000}, 	//#msm_uOffsetNoBin[3][1]
	{0x0F12,0x0000},

	{0x002A,0x07a2},  //#msm_sAnalogOffset[0]
	{0x0F12,0x0000}, 	 //#msm_sAnalogOffset[1]
	{0x0F12,0x0000}, 	 //#msm_sAnalogOffset[2]
	{0x0F12,0x0000}, 	 //#msm_sAnalogOffset[3]
	{0x0F12,0x0000},

	{0x002A,0x07b6},  //#msm_NonLinearOfsOutput[0]
	{0x0F12,0x0000}, 	 //#msm_NonLinearOfsOutput[1]
	{0x0F12,0x0002}, 	 //#msm_NonLinearOfsOutput[2]
	{0x0F12,0x0004}, 	 //#msm_NonLinearOfsOutput[3]
	{0x0F12,0x0004}, 	 //#msm_NonLinearOfsOutput[4]
	{0x0F12,0x0005}, 	 //#msm_NonLinearOfsOutput[5]
	{0x0F12,0x0005},

	//	param_start	TVAR_ash_GASAlpha
	{0x002A,0x0712},
	{0x0F12,0x0149},
	{0x0F12,0x011B},
	{0x0F12,0x0120},
	{0x0F12,0x00EF},
	{0x0F12,0x00C3},
	{0x0F12,0x00D2},
	{0x0F12,0x00DC},
	{0x0F12,0x00B8},
	{0x0F12,0x0106},
	{0x0F12,0x00DE},
	{0x0F12,0x00E3},
	{0x0F12,0x00CC},
	{0x0F12,0x00BD},
	{0x0F12,0x00E1},
	{0x0F12,0x00D8},
	{0x0F12,0x00D0},
	{0x0F12,0x00BE},
	{0x0F12,0x00EC},
	{0x0F12,0x00EA},
	{0x0F12,0x00F2},
	{0x0F12,0x00BE},
	{0x0F12,0x00EB},
	{0x0F12,0x00E5},
	{0x0F12,0x00F9},
	{0x0F12,0x0100},  //0x00BE
	{0x0F12,0x00D5},
	{0x0F12,0x00D8},
	{0x0F12,0x00E6},

	//	param_end	TVAR_ash_GASAlpha
	//	param_start	TVAR_ash_GASOutdoorAlpha
	{0x0F12,0x00FD},
	{0x0F12,0x00F7},
	{0x0F12,0x00F3},
	{0x0F12,0x00FF},
	//	param_end	TVAR_ash_GASOutdoorAlpha
	{0x002A,0x075A},
	{0x0F12,0x0001},
	{0x0F12,0x02A1},
	{0x0F12,0x0225},
	{0x0F12,0x000D},
	{0x0F12,0x000D},
	//	param_start	TVAR_ash_pGAS
	{0x002A,0x247C},
	{0x0F12,0x0164},	//TVAR_ash_pGAS[0]   0128  //018C
	{0x0F12,0x0122},	//TVAR_ash_pGAS[1]   00F6  //0147
	{0x0F12,0x00E4},	//TVAR_ash_pGAS[2]   00C7  //010E
	{0x0F12,0x00B8},	//TVAR_ash_pGAS[3]   00B7  //00E5
	{0x0F12,0x009D},	//TVAR_ash_pGAS[4]   00AA  //00C5
	{0x0F12,0x008D},	//TVAR_ash_pGAS[5]   009D  //00B0
	{0x0F12,0x0083},	//TVAR_ash_pGAS[6]   0097  //00A7
	{0x0F12,0x0088},	//TVAR_ash_pGAS[7]   0097  //00AA
	{0x0F12,0x009C},	//TVAR_ash_pGAS[8]   009E  //00B8
	{0x0F12,0x00BB},	//TVAR_ash_pGAS[9]   00A9  //00D1
	{0x0F12,0x00E5},	//TVAR_ash_pGAS[10]  00B4  //00F7
	{0x0F12,0x0121},	//TVAR_ash_pGAS[11]  00CA  //0130
	{0x0F12,0x0169},	//TVAR_ash_pGAS[12]  0105  //016F
	{0x0F12,0x0138},	//TVAR_ash_pGAS[13]  00DA  //0148
	{0x0F12,0x00FB},	//TVAR_ash_pGAS[14]  00B4  //010A
	{0x0F12,0x00B9},	//TVAR_ash_pGAS[15]  0099  //00D2
	{0x0F12,0x008C},	//TVAR_ash_pGAS[16]  0089  //00AB
	{0x0F12,0x006E},	//TVAR_ash_pGAS[17]  0077  //008F
	{0x0F12,0x005B},	//TVAR_ash_pGAS[18]  006A  //007B
	{0x0F12,0x0053},	//TVAR_ash_pGAS[19]  0063  //0073
	{0x0F12,0x0059},	//TVAR_ash_pGAS[20]  0064  //0075
	{0x0F12,0x006B},	//TVAR_ash_pGAS[21]  006D  //0085
	{0x0F12,0x0086},	//TVAR_ash_pGAS[22]  007B  //009D
	{0x0F12,0x00B1},	//TVAR_ash_pGAS[23]  008B  //00C1
	{0x0F12,0x00E9},	//TVAR_ash_pGAS[24]  009C  //00F7
	{0x0F12,0x012F},	//TVAR_ash_pGAS[25]  00BD  //0134
	{0x0F12,0x0111},	//TVAR_ash_pGAS[26]  00A4  //0111
	{0x0F12,0x00D1},	//TVAR_ash_pGAS[27]  0093  //00D5
	{0x0F12,0x0094},	//TVAR_ash_pGAS[28]  0079  //00A1
	{0x0F12,0x0069},	//TVAR_ash_pGAS[29]  0063  //007B
	{0x0F12,0x004A},	//TVAR_ash_pGAS[30]  004F  //005F
	{0x0F12,0x0036},	//TVAR_ash_pGAS[31]  0040  //004B
	{0x0F12,0x002C},	//TVAR_ash_pGAS[32]  0039  //0043
	{0x0F12,0x0032},	//TVAR_ash_pGAS[33]  003B  //0046
	{0x0F12,0x0042},	//TVAR_ash_pGAS[34]  0044  //0057
	{0x0F12,0x0060},	//TVAR_ash_pGAS[35]  0054  //0072
	{0x0F12,0x008A},	//TVAR_ash_pGAS[36]  006A  //0093
	{0x0F12,0x00C0},	//TVAR_ash_pGAS[37]  0081  //00C7
	{0x0F12,0x0103},	//TVAR_ash_pGAS[38]  009B  //00FD
	{0x0F12,0x00F9},	//TVAR_ash_pGAS[39]  008D  //00EA
	{0x0F12,0x00B7},	//TVAR_ash_pGAS[40]  007E  //00B1
	{0x0F12,0x007E},	//TVAR_ash_pGAS[41]  005F  //0080
	{0x0F12,0x0054},	//TVAR_ash_pGAS[42]  0046  //005A
	{0x0F12,0x0033},	//TVAR_ash_pGAS[43]  0032  //003D
	{0x0F12,0x001B},	//TVAR_ash_pGAS[44]  0023  //0029
	{0x0F12,0x0013},	//TVAR_ash_pGAS[45]  001C  //0021
	{0x0F12,0x0018},	//TVAR_ash_pGAS[46]  001E  //0025
	{0x0F12,0x002A},	//TVAR_ash_pGAS[47]  0026  //0036
	{0x0F12,0x004A},	//TVAR_ash_pGAS[48]  0038  //0051
	{0x0F12,0x0075},	//TVAR_ash_pGAS[49]  004F  //0074
	{0x0F12,0x00AC},	//TVAR_ash_pGAS[50]  006A  //00A3
	{0x0F12,0x00F0},	//TVAR_ash_pGAS[51]  0088  //00DA
	{0x0F12,0x00EF},	//TVAR_ash_pGAS[52]  0081  //00D2
	{0x0F12,0x00AE},	//TVAR_ash_pGAS[53]  0070  //009B
	{0x0F12,0x0076},	//TVAR_ash_pGAS[54]  004F  //006B
	{0x0F12,0x004A},	//TVAR_ash_pGAS[55]  0034  //0043
	{0x0F12,0x0025},	//TVAR_ash_pGAS[56]  001F  //0027
	{0x0F12,0x000D},	//TVAR_ash_pGAS[57]  0010  //0013
	{0x0F12,0x0004},	//TVAR_ash_pGAS[58]  0009  //000A
	{0x0F12,0x000A},	//TVAR_ash_pGAS[59]  000B  //000F
	{0x0F12,0x001E},	//TVAR_ash_pGAS[60]  0015  //0021
	{0x0F12,0x003F},	//TVAR_ash_pGAS[61]  0026  //003D
	{0x0F12,0x006C},	//TVAR_ash_pGAS[62]  0040  //0060
	{0x0F12,0x00A7},	//TVAR_ash_pGAS[63]  005D  //0091
	{0x0F12,0x00EA},	//TVAR_ash_pGAS[64]  007E  //00C2
	{0x0F12,0x00F6},	//TVAR_ash_pGAS[65]  007C  //00C6
	{0x0F12,0x00B3},	//TVAR_ash_pGAS[66]  0068  //0094
	{0x0F12,0x007B},	//TVAR_ash_pGAS[67]  0047  //0063
	{0x0F12,0x004C},	//TVAR_ash_pGAS[68]  002C  //003B
	{0x0F12,0x0024},	//TVAR_ash_pGAS[69]  0017  //001E
	{0x0F12,0x000B},	//TVAR_ash_pGAS[70]  0007  //0009
	{0x0F12,0x0000},	//TVAR_ash_pGAS[71]  0000  //0000
	{0x0F12,0x0007},	//TVAR_ash_pGAS[72]  0002  //0006
	{0x0F12,0x001E},	//TVAR_ash_pGAS[73]  000D  //0019
	{0x0F12,0x0041},	//TVAR_ash_pGAS[74]  0020  //0035
	{0x0F12,0x0071},	//TVAR_ash_pGAS[75]  003A  //005A
	{0x0F12,0x00AD},	//TVAR_ash_pGAS[76]  0059  //0089
	{0x0F12,0x00F3},	//TVAR_ash_pGAS[77]  007E  //00BA
	{0x0F12,0x010B},	//TVAR_ash_pGAS[78]  007E  //00C8
	{0x0F12,0x00C6},	//TVAR_ash_pGAS[79]  006A  //0095
	{0x0F12,0x0089},	//TVAR_ash_pGAS[80]  0048  //0063
	{0x0F12,0x0058},	//TVAR_ash_pGAS[81]  002D  //003D
	{0x0F12,0x0032},	//TVAR_ash_pGAS[82]  0018  //0020
	{0x0F12,0x0017},	//TVAR_ash_pGAS[83]  0008  //000D
	{0x0F12,0x000D},	//TVAR_ash_pGAS[84]  0001  //0004
	{0x0F12,0x0015},	//TVAR_ash_pGAS[85]  0003  //0009
	{0x0F12,0x002D},	//TVAR_ash_pGAS[86]  000E  //001C
	{0x0F12,0x0053},	//TVAR_ash_pGAS[87]  0021  //0038
	{0x0F12,0x0085},	//TVAR_ash_pGAS[88]  003D  //005F
	{0x0F12,0x00C4},	//TVAR_ash_pGAS[89]  0060  //008E
	{0x0F12,0x010B},	//TVAR_ash_pGAS[90]  0086  //00BE
	{0x0F12,0x012F},	//TVAR_ash_pGAS[91]  0087  //00D7
	{0x0F12,0x00E7},	//TVAR_ash_pGAS[92]  0073  //00A1
	{0x0F12,0x00A7},	//TVAR_ash_pGAS[93]  0051  //006E
	{0x0F12,0x0073},	//TVAR_ash_pGAS[94]  0036  //0049
	{0x0F12,0x004E},	//TVAR_ash_pGAS[95]  0022  //002D
	{0x0F12,0x0033},	//TVAR_ash_pGAS[96]  0013  //001B
	{0x0F12,0x002A},	//TVAR_ash_pGAS[97]  000B  //0014
	{0x0F12,0x0032},	//TVAR_ash_pGAS[98]  000D  //001A
	{0x0F12,0x004B},	//TVAR_ash_pGAS[99]  0019  //002C
	{0x0F12,0x0072},	//TVAR_ash_pGAS[100] 002C  //0048
	{0x0F12,0x00A7},	//TVAR_ash_pGAS[101] 004A  //006C
	{0x0F12,0x00E9},	//TVAR_ash_pGAS[102] 006F  //009D
	{0x0F12,0x0133},	//TVAR_ash_pGAS[103] 0093  //00D1
	{0x0F12,0x0162},	//TVAR_ash_pGAS[104] 0099  //00F3
	{0x0F12,0x0110},	//TVAR_ash_pGAS[105] 0086  //00B9
	{0x0F12,0x00CE},	//TVAR_ash_pGAS[106] 0066  //0086
	{0x0F12,0x009B},	//TVAR_ash_pGAS[107] 004A  //0063
	{0x0F12,0x0078},	//TVAR_ash_pGAS[108] 0035  //0047
	{0x0F12,0x0060},	//TVAR_ash_pGAS[109] 0025  //0034
	{0x0F12,0x0057},	//TVAR_ash_pGAS[110] 001E  //002E
	{0x0F12,0x0061},	//TVAR_ash_pGAS[111] 0021  //0034
	{0x0F12,0x0079},	//TVAR_ash_pGAS[112] 002C  //0046
	{0x0F12,0x00A2},	//TVAR_ash_pGAS[113] 0041  //0062
	{0x0F12,0x00D8},	//TVAR_ash_pGAS[114] 0061  //0087
	{0x0F12,0x011E},	//TVAR_ash_pGAS[115] 0087  //00B8
	{0x0F12,0x016E},	//TVAR_ash_pGAS[116] 00A8  //00F2
	{0x0F12,0x01A8},	//TVAR_ash_pGAS[117] 00B8  //011D
	{0x0F12,0x014D},	//TVAR_ash_pGAS[118] 00A1  //00E5
	{0x0F12,0x0104},	//TVAR_ash_pGAS[119] 0084  //00B0
	{0x0F12,0x00D5},	//TVAR_ash_pGAS[120] 0069  //0089
	{0x0F12,0x00B3},	//TVAR_ash_pGAS[121] 0054  //006F
	{0x0F12,0x009B},	//TVAR_ash_pGAS[122] 0044  //005D
	{0x0F12,0x0095},	//TVAR_ash_pGAS[123] 003E  //0057
	{0x0F12,0x009E},	//TVAR_ash_pGAS[124] 0040  //005D
	{0x0F12,0x00B7},	//TVAR_ash_pGAS[125] 004C  //006E
	{0x0F12,0x00E1},	//TVAR_ash_pGAS[126] 0063  //008B
	{0x0F12,0x0119},	//TVAR_ash_pGAS[127] 0083  //00B3
	{0x0F12,0x0166},	//TVAR_ash_pGAS[128] 00A8  //00E6
	{0x0F12,0x01C6},	//TVAR_ash_pGAS[129] 00C9  //0129
	{0x0F12,0x01F1},	//TVAR_ash_pGAS[130] 00EE  //013E
	{0x0F12,0x0199},	//TVAR_ash_pGAS[131] 00C4  //010D
	{0x0F12,0x014D},	//TVAR_ash_pGAS[132] 00AB  //00D8
	{0x0F12,0x011A},	//TVAR_ash_pGAS[133] 0091  //00B2
	{0x0F12,0x00F5},	//TVAR_ash_pGAS[134] 007B  //0096
	{0x0F12,0x00DD},	//TVAR_ash_pGAS[135] 006D  //0084
	{0x0F12,0x00D8},	//TVAR_ash_pGAS[136] 0066  //007E
	{0x0F12,0x00E4},	//TVAR_ash_pGAS[137] 0068  //0083
	{0x0F12,0x00FB},	//TVAR_ash_pGAS[138] 0075  //0096
	{0x0F12,0x0128},	//TVAR_ash_pGAS[139] 008E  //00B3
	{0x0F12,0x0166},	//TVAR_ash_pGAS[140] 00AF  //00DB
	{0x0F12,0x01BC},	//TVAR_ash_pGAS[141] 00D0  //0117
	{0x0F12,0x0228},	//TVAR_ash_pGAS[142] 00F7  //0151
	{0x0F12,0x0133},	//TVAR_ash_pGAS[143] 00E3  //014A
	{0x0F12,0x00F7},	//TVAR_ash_pGAS[144] 00AB  //0108
	{0x0F12,0x00BF},	//TVAR_ash_pGAS[145] 0086  //00D7
	{0x0F12,0x0097},	//TVAR_ash_pGAS[146] 0081  //00B5
	{0x0F12,0x0081},	//TVAR_ash_pGAS[147] 0078  //009E
	{0x0F12,0x0074},	//TVAR_ash_pGAS[148] 0071  //0090
	{0x0F12,0x006A},	//TVAR_ash_pGAS[149] 006B  //0089
	{0x0F12,0x006B},	//TVAR_ash_pGAS[150] 0069  //0089
	{0x0F12,0x007B},	//TVAR_ash_pGAS[151] 006A  //0090
	{0x0F12,0x008F},	//TVAR_ash_pGAS[152] 006E  //00A0
	{0x0F12,0x00B1},	//TVAR_ash_pGAS[153] 0071  //00B8
	{0x0F12,0x00E4},	//TVAR_ash_pGAS[154] 007D  //00E6
	{0x0F12,0x0128},	//TVAR_ash_pGAS[155] 00B0  //011F
	{0x0F12,0x010B},	//TVAR_ash_pGAS[156] 0096  //010B
	{0x0F12,0x00D4},	//TVAR_ash_pGAS[157] 0071  //00CE
	{0x0F12,0x0098},	//TVAR_ash_pGAS[158] 0061  //00A4
	{0x0F12,0x006F},	//TVAR_ash_pGAS[159] 005B  //0085
	{0x0F12,0x0056},	//TVAR_ash_pGAS[160] 0051  //0073
	{0x0F12,0x0045},	//TVAR_ash_pGAS[161] 0049  //0064
	{0x0F12,0x003D},	//TVAR_ash_pGAS[162] 0043  //005D
	{0x0F12,0x0040},	//TVAR_ash_pGAS[163] 0042  //005E
	{0x0F12,0x004D},	//TVAR_ash_pGAS[164] 0044  //0066
	{0x0F12,0x0060},	//TVAR_ash_pGAS[165] 004C  //0076
	{0x0F12,0x0081},	//TVAR_ash_pGAS[166] 0051  //008F
	{0x0F12,0x00B0},	//TVAR_ash_pGAS[167] 0056  //00B7
	{0x0F12,0x00EB},	//TVAR_ash_pGAS[168] 006F  //00EB
	{0x0F12,0x00EC},	//TVAR_ash_pGAS[169] 006B  //00D7
	{0x0F12,0x00B0},	//TVAR_ash_pGAS[170] 005B  //00A3
	{0x0F12,0x007C},	//TVAR_ash_pGAS[171] 004E  //007C
	{0x0F12,0x0054},	//TVAR_ash_pGAS[172] 0043  //0061
	{0x0F12,0x0039},	//TVAR_ash_pGAS[173] 0037  //004C
	{0x0F12,0x0029},	//TVAR_ash_pGAS[174] 002E  //003F
	{0x0F12,0x001E},	//TVAR_ash_pGAS[175] 0029  //0038
	{0x0F12,0x0021},	//TVAR_ash_pGAS[176] 0028  //003A
	{0x0F12,0x002D},	//TVAR_ash_pGAS[177] 002B  //0044
	{0x0F12,0x0044},	//TVAR_ash_pGAS[178] 0033  //0056
	{0x0F12,0x0064},	//TVAR_ash_pGAS[179] 003D  //006D
	{0x0F12,0x008F},	//TVAR_ash_pGAS[180] 0046  //0091
	{0x0F12,0x00C8},	//TVAR_ash_pGAS[181] 0056  //00BD
	{0x0F12,0x00D6},	//TVAR_ash_pGAS[182] 0058  //00B7
	{0x0F12,0x009B},	//TVAR_ash_pGAS[183] 004C  //0086
	{0x0F12,0x0068},	//TVAR_ash_pGAS[184] 003B  //0062
	{0x0F12,0x0043},	//TVAR_ash_pGAS[185] 002E  //0046
	{0x0F12,0x0026},	//TVAR_ash_pGAS[186] 0022  //0031
	{0x0F12,0x0013},	//TVAR_ash_pGAS[187] 0019  //0022
	{0x0F12,0x000B},	//TVAR_ash_pGAS[188] 0014  //001C
	{0x0F12,0x000E},	//TVAR_ash_pGAS[189] 0014  //001E
	{0x0F12,0x001C},	//TVAR_ash_pGAS[190] 0017  //0029
	{0x0F12,0x0033},	//TVAR_ash_pGAS[191] 0020  //003B
	{0x0F12,0x0054},	//TVAR_ash_pGAS[192] 002C  //0055
	{0x0F12,0x0080},	//TVAR_ash_pGAS[193] 0039  //0076
	{0x0F12,0x00B9},	//TVAR_ash_pGAS[194] 004A  //00A0
	{0x0F12,0x00D1},	//TVAR_ash_pGAS[195] 004F  //00A1
	{0x0F12,0x0094},	//TVAR_ash_pGAS[196] 0043  //0074
	{0x0F12,0x0061},	//TVAR_ash_pGAS[197] 002F  //0050
	{0x0F12,0x003B},	//TVAR_ash_pGAS[198] 0022  //0033
	{0x0F12,0x001C},	//TVAR_ash_pGAS[199] 0015  //001E
	{0x0F12,0x0008},	//TVAR_ash_pGAS[200] 000C  //000F
	{0x0F12,0x0001},	//TVAR_ash_pGAS[201] 0007  //0009
	{0x0F12,0x0004},	//TVAR_ash_pGAS[202] 0007  //000B
	{0x0F12,0x0012},	//TVAR_ash_pGAS[203] 000B  //0017
	{0x0F12,0x002B},	//TVAR_ash_pGAS[204] 0016  //002A
	{0x0F12,0x0050},	//TVAR_ash_pGAS[205] 0023  //0044
	{0x0F12,0x007E},	//TVAR_ash_pGAS[206] 0032  //0066
	{0x0F12,0x00B6},	//TVAR_ash_pGAS[207] 0046  //008C
	{0x0F12,0x00D6},	//TVAR_ash_pGAS[208] 004C  //0098
	{0x0F12,0x0098},	//TVAR_ash_pGAS[209] 003E  //006D
	{0x0F12,0x0067},	//TVAR_ash_pGAS[210] 002a  //0049
	{0x0F12,0x003D},	//TVAR_ash_pGAS[211] 001C  //002B
	{0x0F12,0x001D},	//TVAR_ash_pGAS[212] 000F  //0016
	{0x0F12,0x0009},	//TVAR_ash_pGAS[213] 0006  //0006
	{0x0F12,0x0000},	//TVAR_ash_pGAS[214] 0001  //0000
	{0x0F12,0x0004},	//TVAR_ash_pGAS[215] 0002  //0004
	{0x0F12,0x0014},	//TVAR_ash_pGAS[216] 0007  //0010
	{0x0F12,0x002F},	//TVAR_ash_pGAS[217] 0013  //0023
	{0x0F12,0x0057},	//TVAR_ash_pGAS[218] 0021  //003E
	{0x0F12,0x0085},	//TVAR_ash_pGAS[219] 0032  //0060
	{0x0F12,0x00C1},	//TVAR_ash_pGAS[220] 0048  //0085
	{0x0F12,0x00EA},	//TVAR_ash_pGAS[221] 004D  //0098
	{0x0F12,0x00AA},	//TVAR_ash_pGAS[222] 0040  //006E
	{0x0F12,0x0076},	//TVAR_ash_pGAS[223] 002C  //0048
	{0x0F12,0x004B},	//TVAR_ash_pGAS[224] 001E  //002C
	{0x0F12,0x002A},	//TVAR_ash_pGAS[225] 0011  //0018
	{0x0F12,0x0015},	//TVAR_ash_pGAS[226] 0008  //0009
	{0x0F12,0x000C},	//TVAR_ash_pGAS[227] 0004  //0003
	{0x0F12,0x0012},	//TVAR_ash_pGAS[228] 0004  //0006
	{0x0F12,0x0024},	//TVAR_ash_pGAS[229] 000A  //0012
	{0x0F12,0x0041},	//TVAR_ash_pGAS[230] 0017  //0026
	{0x0F12,0x006A},	//TVAR_ash_pGAS[231] 0026  //0042
	{0x0F12,0x009C},	//TVAR_ash_pGAS[232] 0039  //0063
	{0x0F12,0x00D9},	//TVAR_ash_pGAS[233] 0051  //0089
	{0x0F12,0x010A},	//TVAR_ash_pGAS[234] 0054  //00A3
	{0x0F12,0x00C9},	//TVAR_ash_pGAS[235] 0048  //0076
	{0x0F12,0x0092},	//TVAR_ash_pGAS[236] 0034  //0051
	{0x0F12,0x0065},	//TVAR_ash_pGAS[237] 0026  //0035
	{0x0F12,0x0045},	//TVAR_ash_pGAS[238] 001A  //0021
	{0x0F12,0x002E},	//TVAR_ash_pGAS[239] 0012  //0013
	{0x0F12,0x0027},	//TVAR_ash_pGAS[240] 000D  //000E
	{0x0F12,0x002E},	//TVAR_ash_pGAS[241] 000E  //0012
	{0x0F12,0x0041},	//TVAR_ash_pGAS[242] 0014  //001E
	{0x0F12,0x005F},	//TVAR_ash_pGAS[243] 0021  //0032
	{0x0F12,0x008A},	//TVAR_ash_pGAS[244] 0033  //004D
	{0x0F12,0x00BF},	//TVAR_ash_pGAS[245] 0048  //006F
	{0x0F12,0x00FE},	//TVAR_ash_pGAS[246] 005C  //0097
	{0x0F12,0x0137},	//TVAR_ash_pGAS[247] 0063  //00B7
	{0x0F12,0x00EF},	//TVAR_ash_pGAS[248] 0057  //0089
	{0x0F12,0x00B4},	//TVAR_ash_pGAS[249] 0045  //0062
	{0x0F12,0x0089},	//TVAR_ash_pGAS[250] 0036  //0048
	{0x0F12,0x006C},	//TVAR_ash_pGAS[251] 002B  //0034
	{0x0F12,0x0058},	//TVAR_ash_pGAS[252] 0022  //0028
	{0x0F12,0x0051},	//TVAR_ash_pGAS[253] 001E  //0023
	{0x0F12,0x0059},	//TVAR_ash_pGAS[254] 001F  //0027
	{0x0F12,0x006B},	//TVAR_ash_pGAS[255] 0026  //0033
	{0x0F12,0x008B},	//TVAR_ash_pGAS[256] 0034  //0047
	{0x0F12,0x00B7},	//TVAR_ash_pGAS[257] 0047  //0061
	{0x0F12,0x00EE},	//TVAR_ash_pGAS[258] 005D  //0085
	{0x0F12,0x0135},	//TVAR_ash_pGAS[259] 0070  //00B1
	{0x0F12,0x0175},	//TVAR_ash_pGAS[260] 007C  //00DE
	{0x0F12,0x0123},	//TVAR_ash_pGAS[261] 006B  //00AA
	{0x0F12,0x00E3},	//TVAR_ash_pGAS[262] 005C  //0082
	{0x0F12,0x00BB},	//TVAR_ash_pGAS[263] 004E  //0067
	{0x0F12,0x00A0},	//TVAR_ash_pGAS[264] 0043  //0055
	{0x0F12,0x008E},	//TVAR_ash_pGAS[265] 003B  //0049
	{0x0F12,0x008A},	//TVAR_ash_pGAS[266] 0037  //0044
	{0x0F12,0x0091},	//TVAR_ash_pGAS[267] 0039  //0048
	{0x0F12,0x00A5},	//TVAR_ash_pGAS[268] 0040  //0054
	{0x0F12,0x00C4},	//TVAR_ash_pGAS[269] 0050  //0066
	{0x0F12,0x00F1},	//TVAR_ash_pGAS[270] 0064  //0083
	{0x0F12,0x012F},	//TVAR_ash_pGAS[271] 007A  //00A8
	{0x0F12,0x0183},	//TVAR_ash_pGAS[272] 008D  //00E0
	{0x0F12,0x01B5},	//TVAR_ash_pGAS[273] 00AD  //00FE
	{0x0F12,0x0163},	//TVAR_ash_pGAS[274] 0088  //00CC
	{0x0F12,0x0124},	//TVAR_ash_pGAS[275] 007B  //00A2
	{0x0F12,0x00F8},	//TVAR_ash_pGAS[276] 006D  //0085
	{0x0F12,0x00DA},	//TVAR_ash_pGAS[277] 0062  //0072
	{0x0F12,0x00C9},	//TVAR_ash_pGAS[278] 005B  //0066
	{0x0F12,0x00C4},	//TVAR_ash_pGAS[279] 0057  //0062
	{0x0F12,0x00CE},	//TVAR_ash_pGAS[280] 005A  //0065
	{0x0F12,0x00E1},	//TVAR_ash_pGAS[281] 0061  //0072
	{0x0F12,0x0103},	//TVAR_ash_pGAS[282] 0073  //0085
	{0x0F12,0x0136},	//TVAR_ash_pGAS[283] 0088  //00A2
	{0x0F12,0x017F},	//TVAR_ash_pGAS[284] 009E  //00D0
	{0x0F12,0x01E0},	//TVAR_ash_pGAS[285] 00BB  //0103
	{0x0F12,0x0148},	//TVAR_ash_pGAS[286] 00F3  //0153
	{0x0F12,0x0108},	//TVAR_ash_pGAS[287] 00B8  //010D
	{0x0F12,0x00C8},	//TVAR_ash_pGAS[288] 008D  //00D8
	{0x0F12,0x009F},	//TVAR_ash_pGAS[289] 0082  //00B3
	{0x0F12,0x0082},	//TVAR_ash_pGAS[290] 0077  //009A
	{0x0F12,0x0073},	//TVAR_ash_pGAS[291] 006E  //008A
	{0x0F12,0x0068},	//TVAR_ash_pGAS[292] 0069  //0083
	{0x0F12,0x006C},	//TVAR_ash_pGAS[293] 006A  //0087
	{0x0F12,0x007E},	//TVAR_ash_pGAS[294] 0071  //0095
	{0x0F12,0x009B},	//TVAR_ash_pGAS[295] 007B  //00AC
	{0x0F12,0x00C2},	//TVAR_ash_pGAS[296] 0086  //00CF
	{0x0F12,0x0100},	//TVAR_ash_pGAS[297] 0098  //0106
	{0x0F12,0x014D},	//TVAR_ash_pGAS[298] 00D3  //0149
	{0x0F12,0x0121},	//TVAR_ash_pGAS[299] 00A4  //0116
	{0x0F12,0x00E6},	//TVAR_ash_pGAS[300] 007E  //00D6
	{0x0F12,0x00A4},	//TVAR_ash_pGAS[301] 006A  //00A8
	{0x0F12,0x0078},	//TVAR_ash_pGAS[302] 0060  //0087
	{0x0F12,0x005A},	//TVAR_ash_pGAS[303] 0053  //0071
	{0x0F12,0x0046},	//TVAR_ash_pGAS[304] 0048  //0061
	{0x0F12,0x003E},	//TVAR_ash_pGAS[305] 0044  //005B
	{0x0F12,0x0043},	//TVAR_ash_pGAS[306] 0045  //005E
	{0x0F12,0x0052},	//TVAR_ash_pGAS[307] 004C  //006D
	{0x0F12,0x006B},	//TVAR_ash_pGAS[308] 0059  //0083
	{0x0F12,0x0093},	//TVAR_ash_pGAS[309] 0066  //00A4
	{0x0F12,0x00C9},	//TVAR_ash_pGAS[310] 006F  //00D5
	{0x0F12,0x010D},	//TVAR_ash_pGAS[311] 008C  //0113
	{0x0F12,0x0103},	//TVAR_ash_pGAS[312] 007A  //00E6
	{0x0F12,0x00C3},	//TVAR_ash_pGAS[313] 0069  //00AE
	{0x0F12,0x0089},	//TVAR_ash_pGAS[314] 0058  //0084
	{0x0F12,0x005E},	//TVAR_ash_pGAS[315] 004A  //0065
	{0x0F12,0x003E},	//TVAR_ash_pGAS[316] 003A  //004D
	{0x0F12,0x002A},	//TVAR_ash_pGAS[317] 002F  //003D
	{0x0F12,0x001F},	//TVAR_ash_pGAS[318] 0029  //0037
	{0x0F12,0x0023},	//TVAR_ash_pGAS[319] 002a  //003B
	{0x0F12,0x0032},	//TVAR_ash_pGAS[320] 0031  //004B
	{0x0F12,0x004E},	//TVAR_ash_pGAS[321] 003F  //0063
	{0x0F12,0x0074},	//TVAR_ash_pGAS[322] 004F  //0080
	{0x0F12,0x00A5},	//TVAR_ash_pGAS[323] 005C  //00AB
	{0x0F12,0x00E6},	//TVAR_ash_pGAS[324] 006F  //00DF
	{0x0F12,0x00EF},	//TVAR_ash_pGAS[325] 0068  //00C5
	{0x0F12,0x00AD},	//TVAR_ash_pGAS[326] 005C  //0092
	{0x0F12,0x0076},	//TVAR_ash_pGAS[327] 0047  //006B
	{0x0F12,0x004D},	//TVAR_ash_pGAS[328] 0037  //004B
	{0x0F12,0x002C},	//TVAR_ash_pGAS[329] 0027  //0034
	{0x0F12,0x0015},	//TVAR_ash_pGAS[330] 001B  //0023
	{0x0F12,0x000C},	//TVAR_ash_pGAS[331] 0015  //001C
	{0x0F12,0x0010},	//TVAR_ash_pGAS[332] 0016  //0020
	{0x0F12,0x001F},	//TVAR_ash_pGAS[333] 001C  //002E
	{0x0F12,0x003B},	//TVAR_ash_pGAS[334] 0029  //0047
	{0x0F12,0x0061},	//TVAR_ash_pGAS[335] 003A  //0066
	{0x0F12,0x0092},	//TVAR_ash_pGAS[336] 004B  //008C
	{0x0F12,0x00D2},	//TVAR_ash_pGAS[337] 005E  //00BB
	{0x0F12,0x00E8},	//TVAR_ash_pGAS[338] 0060  //00B0
	{0x0F12,0x00A8},	//TVAR_ash_pGAS[339] 0053  //0081
	{0x0F12,0x0071},	//TVAR_ash_pGAS[340] 003B  //005A
	{0x0F12,0x0046},	//TVAR_ash_pGAS[341] 002a  //0039
	{0x0F12,0x0023},	//TVAR_ash_pGAS[342] 001A  //0021
	{0x0F12,0x000B},	//TVAR_ash_pGAS[343] 000D  //0010
	{0x0F12,0x0002},	//TVAR_ash_pGAS[344] 0007  //0009
	{0x0F12,0x0005},	//TVAR_ash_pGAS[345] 0007  //000C
	{0x0F12,0x0014},	//TVAR_ash_pGAS[346] 000D  //001B
	{0x0F12,0x0030},	//TVAR_ash_pGAS[347] 001A  //0033
	{0x0F12,0x0058},	//TVAR_ash_pGAS[348] 002B  //0050
	{0x0F12,0x008A},	//TVAR_ash_pGAS[349] 003D  //0078
	{0x0F12,0x00C8},	//TVAR_ash_pGAS[350] 0053  //00A1
	{0x0F12,0x00EF},	//TVAR_ash_pGAS[351] 005C  //00A8
	{0x0F12,0x00AD},	//TVAR_ash_pGAS[352] 004E  //007B
	{0x0F12,0x0077},	//TVAR_ash_pGAS[353] 0036  //0054
	{0x0F12,0x0048},	//TVAR_ash_pGAS[354] 0023  //0032
	{0x0F12,0x0023},	//TVAR_ash_pGAS[355] 0014  //0019
	{0x0F12,0x000B},	//TVAR_ash_pGAS[356] 0007  //0008
	{0x0F12,0x0000},	//TVAR_ash_pGAS[357] 0000  //0000
	{0x0F12,0x0003},	//TVAR_ash_pGAS[358] 0000  //0004
	{0x0F12,0x0013},	//TVAR_ash_pGAS[359] 0006  //0012
	{0x0F12,0x0030},	//TVAR_ash_pGAS[360] 0012  //0028
	{0x0F12,0x0059},	//TVAR_ash_pGAS[361] 0023  //0046
	{0x0F12,0x008A},	//TVAR_ash_pGAS[362] 0034  //006B
	{0x0F12,0x00CA},	//TVAR_ash_pGAS[363] 004C  //0093
	{0x0F12,0x0104},	//TVAR_ash_pGAS[364] 005E  //00A9
	{0x0F12,0x00C0},	//TVAR_ash_pGAS[365] 004F  //007C
	{0x0F12,0x0087},	//TVAR_ash_pGAS[366] 0037  //0054
	{0x0F12,0x0057},	//TVAR_ash_pGAS[367] 0024  //0034
	{0x0F12,0x0031},	//TVAR_ash_pGAS[368] 0015  //001B
	{0x0F12,0x0018},	//TVAR_ash_pGAS[369] 0008  //000A
	{0x0F12,0x000C},	//TVAR_ash_pGAS[370] 0001  //0002
	{0x0F12,0x0010},	//TVAR_ash_pGAS[371] 0000  //0004
	{0x0F12,0x0020},	//TVAR_ash_pGAS[372] 0005  //0012
	{0x0F12,0x003D},	//TVAR_ash_pGAS[373] 0010  //0027
	{0x0F12,0x0067},	//TVAR_ash_pGAS[374] 0021  //0045
	{0x0F12,0x009A},	//TVAR_ash_pGAS[375] 0033  //0069
	{0x0F12,0x00D9},	//TVAR_ash_pGAS[376] 004A  //0090
	{0x0F12,0x0126},	//TVAR_ash_pGAS[377] 0064  //00B4
	{0x0F12,0x00E1},	//TVAR_ash_pGAS[378] 0056  //0086
	{0x0F12,0x00A3},	//TVAR_ash_pGAS[379] 003F  //005C
	{0x0F12,0x0071},	//TVAR_ash_pGAS[380] 002C  //003C
	{0x0F12,0x004C},	//TVAR_ash_pGAS[381] 001D  //0024
	{0x0F12,0x0031},	//TVAR_ash_pGAS[382] 0011  //0014
	{0x0F12,0x0025},	//TVAR_ash_pGAS[383] 0008  //000D
	{0x0F12,0x0029},	//TVAR_ash_pGAS[384] 0006  //000F
	{0x0F12,0x003A},	//TVAR_ash_pGAS[385] 000B  //001B
	{0x0F12,0x0057},	//TVAR_ash_pGAS[386] 0015  //002F
	{0x0F12,0x0081},	//TVAR_ash_pGAS[387] 0025  //004B
	{0x0F12,0x00B4},	//TVAR_ash_pGAS[388] 0038  //006E
	{0x0F12,0x00F4},	//TVAR_ash_pGAS[389] 004B  //0097
	{0x0F12,0x0154},	//TVAR_ash_pGAS[390] 0072  //00CA
	{0x0F12,0x0107},	//TVAR_ash_pGAS[391] 0064  //0098
	{0x0F12,0x00C6},	//TVAR_ash_pGAS[392] 004E  //006E
	{0x0F12,0x0095},	//TVAR_ash_pGAS[393] 003B  //0050
	{0x0F12,0x0074},	//TVAR_ash_pGAS[394] 002C  //0037
	{0x0F12,0x005A},	//TVAR_ash_pGAS[395] 001F  //0027
	{0x0F12,0x004F},	//TVAR_ash_pGAS[396] 0018  //0020
	{0x0F12,0x0053},	//TVAR_ash_pGAS[397] 0015  //0022
	{0x0F12,0x0062},	//TVAR_ash_pGAS[398] 0018  //002D
	{0x0F12,0x007F},	//TVAR_ash_pGAS[399] 0022  //0042
	{0x0F12,0x00A8},	//TVAR_ash_pGAS[400] 0032  //005B
	{0x0F12,0x00DC},	//TVAR_ash_pGAS[401] 0044  //0080
	{0x0F12,0x0120},	//TVAR_ash_pGAS[402] 0054  //00AA
	{0x0F12,0x0193},	//TVAR_ash_pGAS[403] 0089  //00F0
	{0x0F12,0x013B},	//TVAR_ash_pGAS[404] 0077  //00BA
	{0x0F12,0x00F6},	//TVAR_ash_pGAS[405] 0065  //008E
	{0x0F12,0x00C9},	//TVAR_ash_pGAS[406] 0052  //006F
	{0x0F12,0x00A8},	//TVAR_ash_pGAS[407] 0043  //0058
	{0x0F12,0x0090},	//TVAR_ash_pGAS[408] 0037  //0048
	{0x0F12,0x0086},	//TVAR_ash_pGAS[409] 002F  //0041
	{0x0F12,0x008A},	//TVAR_ash_pGAS[410] 002C  //0042
	{0x0F12,0x0099},	//TVAR_ash_pGAS[411] 002F  //004C
	{0x0F12,0x00B5},	//TVAR_ash_pGAS[412] 0038  //005F
	{0x0F12,0x00DD},	//TVAR_ash_pGAS[413] 0048  //007A
	{0x0F12,0x0117},	//TVAR_ash_pGAS[414] 0058  //009D
	{0x0F12,0x0166},	//TVAR_ash_pGAS[415] 0068  //00D2
	{0x0F12,0x01D6},	//TVAR_ash_pGAS[416] 00BC  //0112
	{0x0F12,0x017D},	//TVAR_ash_pGAS[417] 0092  //00DD
	{0x0F12,0x0138},	//TVAR_ash_pGAS[418] 0082  //00AF
	{0x0F12,0x0108},	//TVAR_ash_pGAS[419] 006F  //008F
	{0x0F12,0x00E3},	//TVAR_ash_pGAS[420] 0061  //0077
	{0x0F12,0x00CD},	//TVAR_ash_pGAS[421] 0055  //0066
	{0x0F12,0x00C2},	//TVAR_ash_pGAS[422] 004E  //005F
	{0x0F12,0x00C7},	//TVAR_ash_pGAS[423] 004A  //005F
	{0x0F12,0x00D4},	//TVAR_ash_pGAS[424] 004C  //006A
	{0x0F12,0x00F1},	//TVAR_ash_pGAS[425] 0056  //007C
	{0x0F12,0x011F},	//TVAR_ash_pGAS[426] 0066  //0097
	{0x0F12,0x015F},	//TVAR_ash_pGAS[427] 0073  //00C2
	{0x0F12,0x01B7},	//TVAR_ash_pGAS[428] 008B  //00F2
	{0x0F12,0x00D1},	//TVAR_ash_pGAS[429] 0087  //00EE
	{0x0F12,0x00A9},	//TVAR_ash_pGAS[430] 0069  //00C2
	{0x0F12,0x0080},	//TVAR_ash_pGAS[431] 0053  //009B
	{0x0F12,0x0064},	//TVAR_ash_pGAS[432] 0052  //0085
	{0x0F12,0x0055},	//TVAR_ash_pGAS[433] 0050  //0074
	{0x0F12,0x004E},	//TVAR_ash_pGAS[434] 0050  //006A
	{0x0F12,0x0047},	//TVAR_ash_pGAS[435] 004F  //0067
	{0x0F12,0x004B},	//TVAR_ash_pGAS[436] 0052  //006B
	{0x0F12,0x005E},	//TVAR_ash_pGAS[437] 0057  //0076
	{0x0F12,0x0077},	//TVAR_ash_pGAS[438] 005D  //0089
	{0x0F12,0x0094},	//TVAR_ash_pGAS[439] 0060  //00A3
	{0x0F12,0x00BF},	//TVAR_ash_pGAS[440] 0068  //00CE
	{0x0F12,0x00F1},	//TVAR_ash_pGAS[441] 0090  //0102
	{0x0F12,0x00BB},	//TVAR_ash_pGAS[442] 0050  //00C2
	{0x0F12,0x0095},	//TVAR_ash_pGAS[443] 003F  //0097
	{0x0F12,0x0067},	//TVAR_ash_pGAS[444] 003A  //0076
	{0x0F12,0x0049},	//TVAR_ash_pGAS[445] 003A  //0062
	{0x0F12,0x0036},	//TVAR_ash_pGAS[446] 0035  //0054
	{0x0F12,0x002C},	//TVAR_ash_pGAS[447] 0033  //004B
	{0x0F12,0x0027},	//TVAR_ash_pGAS[448] 0033  //0047
	{0x0F12,0x002E},	//TVAR_ash_pGAS[449] 0036  //004C
	{0x0F12,0x003C},	//TVAR_ash_pGAS[450] 003B  //0057
	{0x0F12,0x004F},	//TVAR_ash_pGAS[451] 0043  //006A
	{0x0F12,0x006F},	//TVAR_ash_pGAS[452] 0048  //0084
	{0x0F12,0x0097},	//TVAR_ash_pGAS[453] 004A  //00AE
	{0x0F12,0x00C7},	//TVAR_ash_pGAS[454] 005D  //00DF
	{0x0F12,0x00A5},	//TVAR_ash_pGAS[455] 002D  //009A
	{0x0F12,0x0079},	//TVAR_ash_pGAS[456] 002D  //0075
	{0x0F12,0x0052},	//TVAR_ash_pGAS[457] 002B  //0058
	{0x0F12,0x0036},	//TVAR_ash_pGAS[458] 0029  //0045
	{0x0F12,0x0023},	//TVAR_ash_pGAS[459] 0023  //0039
	{0x0F12,0x0017},	//TVAR_ash_pGAS[460] 0020  //002F
	{0x0F12,0x0012},	//TVAR_ash_pGAS[461] 001F  //002C
	{0x0F12,0x0017},	//TVAR_ash_pGAS[462] 0023  //0030
	{0x0F12,0x0024},	//TVAR_ash_pGAS[463] 0027  //003D
	{0x0F12,0x0039},	//TVAR_ash_pGAS[464] 002F  //0050
	{0x0F12,0x0055},	//TVAR_ash_pGAS[465] 0036  //0067
	{0x0F12,0x007A},	//TVAR_ash_pGAS[466] 003A  //008B
	{0x0F12,0x00A8},	//TVAR_ash_pGAS[467] 0043  //00B4
	{0x0F12,0x0097},	//TVAR_ash_pGAS[468] 001F  //0080
	{0x0F12,0x0069},	//TVAR_ash_pGAS[469] 0023  //005E
	{0x0F12,0x0045},	//TVAR_ash_pGAS[470] 001D  //0044
	{0x0F12,0x002B},	//TVAR_ash_pGAS[471] 001A  //0032
	{0x0F12,0x0018},	//TVAR_ash_pGAS[472] 0015  //0024
	{0x0F12,0x000B},	//TVAR_ash_pGAS[473] 0012  //001A
	{0x0F12,0x0006},	//TVAR_ash_pGAS[474] 0010  //0017
	{0x0F12,0x000A},	//TVAR_ash_pGAS[475] 0013  //001B
	{0x0F12,0x0017},	//TVAR_ash_pGAS[476] 0017  //0027
	{0x0F12,0x002B},	//TVAR_ash_pGAS[477] 001E  //0039
	{0x0F12,0x0047},	//TVAR_ash_pGAS[478] 0025  //004F
	{0x0F12,0x006B},	//TVAR_ash_pGAS[479] 002B  //006E
	{0x0F12,0x0097},	//TVAR_ash_pGAS[480] 0034  //0092
	{0x0F12,0x0092},	//TVAR_ash_pGAS[481] 0017  //006E
	{0x0F12,0x0065},	//TVAR_ash_pGAS[482] 001B  //004F
	{0x0F12,0x0042},	//TVAR_ash_pGAS[483] 0015  //0037
	{0x0F12,0x0028},	//TVAR_ash_pGAS[484] 0011  //0023
	{0x0F12,0x0013},	//TVAR_ash_pGAS[485] 000B  //0016
	{0x0F12,0x0005},	//TVAR_ash_pGAS[486] 0008  //000C
	{0x0F12,0x0000},	//TVAR_ash_pGAS[487] 0006  //0008
	{0x0F12,0x0004},	//TVAR_ash_pGAS[488] 0008  //000C
	{0x0F12,0x000F},	//TVAR_ash_pGAS[489] 000B  //0017
	{0x0F12,0x0025},	//TVAR_ash_pGAS[490] 0012  //0028
	{0x0F12,0x0040},	//TVAR_ash_pGAS[491] 0018  //003D
	{0x0F12,0x0064},	//TVAR_ash_pGAS[492] 001E  //005B
	{0x0F12,0x008F},	//TVAR_ash_pGAS[493] 0028  //007A
	{0x0F12,0x0098},	//TVAR_ash_pGAS[494] 0014  //0068
	{0x0F12,0x006A},	//TVAR_ash_pGAS[495] 0016  //004A
	{0x0F12,0x004A},	//TVAR_ash_pGAS[496] 0011  //0032
	{0x0F12,0x002E},	//TVAR_ash_pGAS[497] 000C  //001D
	{0x0F12,0x0016},	//TVAR_ash_pGAS[498] 0007  //0011
	{0x0F12,0x0007},	//TVAR_ash_pGAS[499] 0002  //0005
	{0x0F12,0x0000},	//TVAR_ash_pGAS[500] 0000  //0000
	{0x0F12,0x0004},	//TVAR_ash_pGAS[501] 0001  //0005
	{0x0F12,0x0011},	//TVAR_ash_pGAS[502] 0005  //000F
	{0x0F12,0x0026},	//TVAR_ash_pGAS[503] 000A  //001E
	{0x0F12,0x0042},	//TVAR_ash_pGAS[504] 000F  //0033
	{0x0F12,0x0064},	//TVAR_ash_pGAS[505] 0014  //004E
	{0x0F12,0x008F},	//TVAR_ash_pGAS[506] 001E  //006C
	{0x0F12,0x00A9},	//TVAR_ash_pGAS[507] 0015  //006C
	{0x0F12,0x007B},	//TVAR_ash_pGAS[508] 0016  //004C
	{0x0F12,0x0057},	//TVAR_ash_pGAS[509] 0011  //0033
	{0x0F12,0x0039},	//TVAR_ash_pGAS[510] 000D  //0020
	{0x0F12,0x0021},	//TVAR_ash_pGAS[511] 0007  //0013
	{0x0F12,0x0012},	//TVAR_ash_pGAS[512] 0003  //0008
	{0x0F12,0x000C},	//TVAR_ash_pGAS[513] 0000  //0003
	{0x0F12,0x0010},	//TVAR_ash_pGAS[514] 0000  //0006
	{0x0F12,0x001D},	//TVAR_ash_pGAS[515] 0002  //000E
	{0x0F12,0x0031},	//TVAR_ash_pGAS[516] 0007  //001C
	{0x0F12,0x004E},	//TVAR_ash_pGAS[517] 000C  //0032
	{0x0F12,0x0071},	//TVAR_ash_pGAS[518] 0010  //004B
	{0x0F12,0x009C},	//TVAR_ash_pGAS[519] 0019  //0067
	{0x0F12,0x00C6},	//TVAR_ash_pGAS[520] 0019  //0076
	{0x0F12,0x0097},	//TVAR_ash_pGAS[521] 001C  //0055
	{0x0F12,0x006E},	//TVAR_ash_pGAS[522] 0016  //003A
	{0x0F12,0x004E},	//TVAR_ash_pGAS[523] 0012  //0028
	{0x0F12,0x0038},	//TVAR_ash_pGAS[524] 000D  //001A
	{0x0F12,0x0028},	//TVAR_ash_pGAS[525] 0009  //0010
	{0x0F12,0x0022},	//TVAR_ash_pGAS[526] 0005  //000C
	{0x0F12,0x0027},	//TVAR_ash_pGAS[527] 0003  //000E
	{0x0F12,0x0034},	//TVAR_ash_pGAS[528] 0005  //0016
	{0x0F12,0x0047},	//TVAR_ash_pGAS[529] 0009  //0022
	{0x0F12,0x0064},	//TVAR_ash_pGAS[530] 000D  //0035
	{0x0F12,0x0088},	//TVAR_ash_pGAS[531] 0011  //004F
	{0x0F12,0x00B3},	//TVAR_ash_pGAS[532] 0017  //006C
	{0x0F12,0x00EB},	//TVAR_ash_pGAS[533] 0024  //0089
	{0x0F12,0x00B3},	//TVAR_ash_pGAS[534] 0026  //0066
	{0x0F12,0x008A},	//TVAR_ash_pGAS[535] 0021  //0049
	{0x0F12,0x006C},	//TVAR_ash_pGAS[536] 001D  //0037
	{0x0F12,0x005A},	//TVAR_ash_pGAS[537] 0018  //002A
	{0x0F12,0x004B},	//TVAR_ash_pGAS[538] 0014  //0020
	{0x0F12,0x0047},	//TVAR_ash_pGAS[539] 000F  //001C
	{0x0F12,0x004C},	//TVAR_ash_pGAS[540] 000D  //001E
	{0x0F12,0x0057},	//TVAR_ash_pGAS[541] 000E  //0024
	{0x0F12,0x006A},	//TVAR_ash_pGAS[542] 0012  //0030
	{0x0F12,0x0086},	//TVAR_ash_pGAS[543] 0016  //0042
	{0x0F12,0x00A9},	//TVAR_ash_pGAS[544] 0019  //005D
	{0x0F12,0x00D9},	//TVAR_ash_pGAS[545] 001B  //007D
	{0x0F12,0x011D},	//TVAR_ash_pGAS[546] 0036  //00A9
	{0x0F12,0x00DE},	//TVAR_ash_pGAS[547] 0035  //0084
	{0x0F12,0x00B2},	//TVAR_ash_pGAS[548] 0032  //0066
	{0x0F12,0x0097},	//TVAR_ash_pGAS[549] 002E  //0052
	{0x0F12,0x0087},	//TVAR_ash_pGAS[550] 002a  //0045
	{0x0F12,0x007A},	//TVAR_ash_pGAS[551] 0026  //003B
	{0x0F12,0x0077},	//TVAR_ash_pGAS[552] 0021  //0036
	{0x0F12,0x007A},	//TVAR_ash_pGAS[553] 001F  //0038
	{0x0F12,0x0086},	//TVAR_ash_pGAS[554] 001F  //003E
	{0x0F12,0x0098},	//TVAR_ash_pGAS[555] 0024  //004A
	{0x0F12,0x00B3},	//TVAR_ash_pGAS[556] 0027  //005E
	{0x0F12,0x00DA},	//TVAR_ash_pGAS[557] 002a  //0077
	{0x0F12,0x0112},	//TVAR_ash_pGAS[558] 0028  //009E
	{0x0F12,0x0149},	//TVAR_ash_pGAS[559] 005C  //00BC
	{0x0F12,0x0114},	//TVAR_ash_pGAS[560] 004A  //009C
	{0x0F12,0x00EB},	//TVAR_ash_pGAS[561] 0049  //007D
	{0x0F12,0x00CE},	//TVAR_ash_pGAS[562] 0046  //0069
	{0x0F12,0x00B9},	//TVAR_ash_pGAS[563] 0041  //005A
	{0x0F12,0x00AD},	//TVAR_ash_pGAS[564] 003E  //0052
	{0x0F12,0x00A8},	//TVAR_ash_pGAS[565] 003A  //004C
	{0x0F12,0x00AF},	//TVAR_ash_pGAS[566] 0037  //004D
	{0x0F12,0x00B8},	//TVAR_ash_pGAS[567] 0037  //0054
	{0x0F12,0x00CB},	//TVAR_ash_pGAS[568] 003B  //0060
	{0x0F12,0x00EC},	//TVAR_ash_pGAS[569] 003F  //0072
	{0x0F12,0x0119},	//TVAR_ash_pGAS[570] 0040  //0093
	{0x0F12,0x0153},	//TVAR_ash_pGAS[571] 0041  //00B6
	//	param_end	TVAR_ash_pGAS
	{0x002A,0x0F12},
	{0x0F12,0x02BC},
	{0x0F12,0x0347},
	//	param_start	TVAR_ash_AwbAshCord
	{0x002A,0x0704},
	{0x0F12,0x00BF},
	{0x0F12,0x00E6},
	{0x0F12,0x00F2},
	{0x0F12,0x0143},
	{0x0F12,0x0178},
	{0x0F12,0x01A3},
	{0x0F12,0x01B5},
	//	param_end	TVAR_ash_AwbAshCord

	{0x002A,0x0754},  //#TVAR_ash_pGAS
	{0x0F12,0x247C},
	{0x0F12,0x7000},

	//
	//	param_start	awbb_IndoorGrZones_m_BGrid
	{0x002A,0x0C50},
	{0x0F12,0x03AE},  //03BE	//awbb_IndoorGrZones_m_BGrid[0]            03C6
	{0x0F12,0x03DB},  //03EB	//awbb_IndoorGrZones_m_BGrid[1]            03F3
	{0x0F12,0x0399},  //03A9	//awbb_IndoorGrZones_m_BGrid[2]            03B1
	{0x0F12,0x03DB},  //03EB	//awbb_IndoorGrZones_m_BGrid[3]            03F3
	{0x0F12,0x0382},  //0392	//awbb_IndoorGrZones_m_BGrid[4]            039A
	{0x0F12,0x03DB},  //03EB	//awbb_IndoorGrZones_m_BGrid[5]            03F3
	{0x0F12,0x036B},  //037B	//awbb_IndoorGrZones_m_BGrid[6]            0383
	{0x0F12,0x03CE},  //03DE	//awbb_IndoorGrZones_m_BGrid[7]            03E6
	{0x0F12,0x034E},  //035E	//awbb_IndoorGrZones_m_BGrid[8]            0366
	{0x0F12,0x03BD},  //03CD	//awbb_IndoorGrZones_m_BGrid[9]            03D5
	{0x0F12,0x0330},  //0340	//awbb_IndoorGrZones_m_BGrid[10]           0348
	{0x0F12,0x03A8},  //03B8	//awbb_IndoorGrZones_m_BGrid[11]           03C0
	{0x0F12,0x0316},  //0326	//awbb_IndoorGrZones_m_BGrid[12]           032E
	{0x0F12,0x0391},  //03A1	//awbb_IndoorGrZones_m_BGrid[13]           03A9
	{0x0F12,0x02FA},  //030A	//awbb_IndoorGrZones_m_BGrid[14]           0312
	{0x0F12,0x0376},  //0386	//awbb_IndoorGrZones_m_BGrid[15]           038E
	{0x0F12,0x02E0},  //02F0	//awbb_IndoorGrZones_m_BGrid[16]           02F8
	{0x0F12,0x035D},  //036D	//awbb_IndoorGrZones_m_BGrid[17]           0375
	{0x0F12,0x02CC},  //02DC	//awbb_IndoorGrZones_m_BGrid[18]           02E4
	{0x0F12,0x0344},  //0354	//awbb_IndoorGrZones_m_BGrid[19]           035C
	{0x0F12,0x02B7},  //02C7	//awbb_IndoorGrZones_m_BGrid[20]           02CF
	{0x0F12,0x032F},  //033F	//awbb_IndoorGrZones_m_BGrid[21]           0347
	{0x0F12,0x02A7},  //02B7	//awbb_IndoorGrZones_m_BGrid[22]           02BF
	{0x0F12,0x0318},  //0328	//awbb_IndoorGrZones_m_BGrid[23]           0330
	{0x0F12,0x0296},  //02A6	//awbb_IndoorGrZones_m_BGrid[24]           02AE
	{0x0F12,0x0301},  //0311	//awbb_IndoorGrZones_m_BGrid[25]           0319
	{0x0F12,0x0286},  //0296	//awbb_IndoorGrZones_m_BGrid[26]           029E
	{0x0F12,0x02ED},  //02FD	//awbb_IndoorGrZones_m_BGrid[27]           0305
	{0x0F12,0x0279},  //0289	//awbb_IndoorGrZones_m_BGrid[28]           0291
	{0x0F12,0x02DB},  //02EB	//awbb_IndoorGrZones_m_BGrid[29]           02F3
	{0x0F12,0x026C},  //027C	//awbb_IndoorGrZones_m_BGrid[30]           0284
	{0x0F12,0x02CD},  //02DD	//awbb_IndoorGrZones_m_BGrid[31]           02E5
	{0x0F12,0x025E},  //026E	//awbb_IndoorGrZones_m_BGrid[32]           0276
	{0x0F12,0x02BF},  //02CF	//awbb_IndoorGrZones_m_BGrid[33]           02D7
	{0x0F12,0x0252},  //0262	//awbb_IndoorGrZones_m_BGrid[34]           026A
	{0x0F12,0x02B2},  //02C2	//awbb_IndoorGrZones_m_BGrid[35]           02CA
	{0x0F12,0x0249},  //0259	//awbb_IndoorGrZones_m_BGrid[36]           0261
	{0x0F12,0x02A7},  //02B7	//awbb_IndoorGrZones_m_BGrid[37]           02BF
	{0x0F12,0x023B},  //024B	//awbb_IndoorGrZones_m_BGrid[38]           0253
	{0x0F12,0x029B},  //02AB	//awbb_IndoorGrZones_m_BGrid[39]           02B3
	{0x0F12,0x0231},  //0241	//awbb_IndoorGrZones_m_BGrid[40]           0249
	{0x0F12,0x028D},  //029D	//awbb_IndoorGrZones_m_BGrid[41]           02A5
	{0x0F12,0x0227},  //0237	//awbb_IndoorGrZones_m_BGrid[42]           023F
	{0x0F12,0x0281},  //0291	//awbb_IndoorGrZones_m_BGrid[43]           0299
	{0x0F12,0x021D},  //022D	//awbb_IndoorGrZones_m_BGrid[44]           0235
	{0x0F12,0x0277},  //0287	//awbb_IndoorGrZones_m_BGrid[45]           028F
	{0x0F12,0x0216},  //0226	//awbb_IndoorGrZones_m_BGrid[46]           022E
	{0x0F12,0x026E},  //027E	//awbb_IndoorGrZones_m_BGrid[47]           0286
	{0x0F12,0x020C},  //021C	//awbb_IndoorGrZones_m_BGrid[48]           0224
	{0x0F12,0x0263},  //0273	//awbb_IndoorGrZones_m_BGrid[49]           027B
	{0x0F12,0x0204},  //0214	//awbb_IndoorGrZones_m_BGrid[50]           021C
	{0x0F12,0x0257},  //0267	//awbb_IndoorGrZones_m_BGrid[51]           026F
	{0x0F12,0x01F7},  //0207	//awbb_IndoorGrZones_m_BGrid[52]           020F
	{0x0F12,0x024A},  //025A	//awbb_IndoorGrZones_m_BGrid[53]           0262
	{0x0F12,0x01E4},  //01F4	//awbb_IndoorGrZones_m_BGrid[54]           01FC
	{0x0F12,0x023D},  //024D	//awbb_IndoorGrZones_m_BGrid[55]           0255
	{0x0F12,0x01CC},  //01DC	//awbb_IndoorGrZones_m_BGrid[56]           01F3
	{0x0F12,0x022A},  //023A	//awbb_IndoorGrZones_m_BGrid[57]           0242
	{0x0F12,0x01E5},  //01F5	//awbb_IndoorGrZones_m_BGrid[58]           01FD
	{0x0F12,0x020E},  //021E	//awbb_IndoorGrZones_m_BGrid[59]           0226
	{0x0F12,0x0000},  //0000	//awbb_IndoorGrZones_m_BGrid[60]           0000
	{0x0F12,0x0000},  //0000	//awbb_IndoorGrZones_m_BGrid[61]           0000
	{0x0F12,0x0000},  //0000	//awbb_IndoorGrZones_m_BGrid[62]           0000
	{0x0F12,0x0000},  //0000	//awbb_IndoorGrZones_m_BGrid[63]           0000
	{0x0F12,0x0000},  //0000	//awbb_IndoorGrZones_m_BGrid[64]           0000
	{0x0F12,0x0000},  //0000	//awbb_IndoorGrZones_m_BGrid[65]           0000
	{0x0F12,0x0000},  //0000	//awbb_IndoorGrZones_m_BGrid[66]           0000
	{0x0F12,0x0000},  //0000	//awbb_IndoorGrZones_m_BGrid[67]           0000
	{0x0F12,0x0000},  //0000	//awbb_IndoorGrZones_m_BGrid[68]           0000
	{0x0F12,0x0000},  //0000	//awbb_IndoorGrZones_m_BGrid[69]           0000
	{0x0F12,0x0000},  //0000	//awbb_IndoorGrZones_m_BGrid[70]           0000
	{0x0F12,0x0000},  //0000	//awbb_IndoorGrZones_m_BGrid[71]           0000
	{0x0F12,0x0000},  //0000	//awbb_IndoorGrZones_m_BGrid[72]           0000
	{0x0F12,0x0000},  //0000	//awbb_IndoorGrZones_m_BGrid[73]           0000
	{0x0F12,0x0000},  //0000	//awbb_IndoorGrZones_m_BGrid[74]           0000
	{0x0F12,0x0000},  //0000	//awbb_IndoorGrZones_m_BGrid[75]           0000
	{0x0F12,0x0000},  //0000	//awbb_IndoorGrZones_m_BGrid[76]           0000
	{0x0F12,0x0000},  //0000	//awbb_IndoorGrZones_m_BGrid[77]           0000
	{0x0F12,0x0000},  //0000	//awbb_IndoorGrZones_m_BGrid[78]           0000
	{0x0F12,0x0000},  //0000	//awbb_IndoorGrZones_m_BGrid[79]           0000
	//	param_end	awbb_IndoorGrZones_m_BGrid	0cf0 //#awbb_IndoorGrZones_m_GridStep
	{0x002a,0x0cf0},
	{0x0F12,0x0004},
	{0x0F12,0x0000},
	{0x002A,0x0cf8},  //#awbb_IndoorGrZones_m_Boffs
	{0x0F12,0x00f7}, //EC
	{0x0F12,0x0000},
	//	param_start	awbb_LowBrGrZones_m_BGrid
	{0x002A,0x0D84},
	{0x0F12,0x0406},  //0406	//awbb_LowBrGrZones_m_BGrid[0] 0406
	{0x0F12,0x0467},  //0467	//awbb_LowBrGrZones_m_BGrid[1] 0467
	{0x0F12,0x0371},  //0371	//awbb_LowBrGrZones_m_BGrid[2] 0371
	{0x0F12,0x04B0},  //04B0	//awbb_LowBrGrZones_m_BGrid[3] 04B0
	{0x0F12,0x02E5},  //02E5	//awbb_LowBrGrZones_m_BGrid[4] 02E5
	{0x0F12,0x0481},  //0481	//awbb_LowBrGrZones_m_BGrid[5] 0481
	{0x0F12,0x0298},  //0298	//awbb_LowBrGrZones_m_BGrid[6] 0298
	{0x0F12,0x042E},  //042E	//awbb_LowBrGrZones_m_BGrid[7] 042E
	{0x0F12,0x0260},  //0260	//awbb_LowBrGrZones_m_BGrid[8] 0260
	{0x0F12,0x03DE},  //03DE	//awbb_LowBrGrZones_m_BGrid[9] 03DE
	{0x0F12,0x022F},  //022F	//awbb_LowBrGrZones_m_BGrid[10]022F
	{0x0F12,0x0391},  //0391	//awbb_LowBrGrZones_m_BGrid[11]0391
	{0x0F12,0x0201},  //0201	//awbb_LowBrGrZones_m_BGrid[12]0201
	{0x0F12,0x034D},  //034D	//awbb_LowBrGrZones_m_BGrid[13]034D
	{0x0F12,0x01DA},  //01DA	//awbb_LowBrGrZones_m_BGrid[14]01DA
	{0x0F12,0x0310},  //0310	//awbb_LowBrGrZones_m_BGrid[15]0310
	{0x0F12,0x01B3},  //01B3	//awbb_LowBrGrZones_m_BGrid[16]01B3
	{0x0F12,0x02D4},  //02D4	//awbb_LowBrGrZones_m_BGrid[17]02D4
	{0x0F12,0x018F},  //018F	//awbb_LowBrGrZones_m_BGrid[18]018F
	{0x0F12,0x0297},  //0297	//awbb_LowBrGrZones_m_BGrid[19]0297
	{0x0F12,0x0181},  //0181	//awbb_LowBrGrZones_m_BGrid[20]0181
	{0x0F12,0x0271},  //0271	//awbb_LowBrGrZones_m_BGrid[21]0271
	{0x0F12,0x0181},  //0181	//awbb_LowBrGrZones_m_BGrid[22]0181
	{0x0F12,0x022A},  //022A	//awbb_LowBrGrZones_m_BGrid[23]022A
	{0x0F12,0x0000},  //0000	//awbb_LowBrGrZones_m_BGrid[24]0000
	{0x0F12,0x0000},  //0000	//awbb_LowBrGrZones_m_BGrid[25]0000
	{0x0F12,0x0000},  //0000	//awbb_LowBrGrZones_m_BGrid[26]0000
	{0x0F12,0x0000},  //0000	//awbb_LowBrGrZones_m_BGrid[27]0000
	{0x0F12,0x0000},  //0000	//awbb_LowBrGrZones_m_BGrid[28]0000
	{0x0F12,0x0000},  //0000	//awbb_LowBrGrZones_m_BGrid[29]0000
	{0x0F12,0x0000},  //0000	//awbb_LowBrGrZones_m_BGrid[30]0000
	{0x0F12,0x0000},  //0000	//awbb_LowBrGrZones_m_BGrid[31]0000
	{0x0F12,0x0000},  //0000	//awbb_LowBrGrZones_m_BGrid[32]0000
	{0x0F12,0x0000},  //0000	//awbb_LowBrGrZones_m_BGrid[33]0000
	{0x0F12,0x0000},  //0000	//awbb_LowBrGrZones_m_BGrid[34]0000
	{0x0F12,0x0000},  //0000	//awbb_LowBrGrZones_m_BGrid[35]0000
	{0x0F12,0x0000},  //0000	//awbb_LowBrGrZones_m_BGrid[36]0000
	{0x0F12,0x0000},  //0000	//awbb_LowBrGrZones_m_BGrid[37]0000
	{0x0F12,0x0000},  //0000	//awbb_LowBrGrZones_m_BGrid[38]0000
	{0x0F12,0x0000},  //0000	//awbb_LowBrGrZones_m_BGrid[39]0000
	{0x0F12,0x0000},  //0000	//awbb_LowBrGrZones_m_BGrid[40]0000
	{0x0F12,0x0000},  //0000	//awbb_LowBrGrZones_m_BGrid[41]0000
	{0x0F12,0x0000},  //0000	//awbb_LowBrGrZones_m_BGrid[42]0000
	{0x0F12,0x0000},  //0000	//awbb_LowBrGrZones_m_BGrid[43]0000
	{0x0F12,0x0000},  //0000	//awbb_LowBrGrZones_m_BGrid[44]0000
	{0x0F12,0x0000},  //0000	//awbb_LowBrGrZones_m_BGrid[45]0000
	{0x0F12,0x0000},  //0000	//awbb_LowBrGrZones_m_BGrid[46]0000
	{0x0F12,0x0000},  //0000	//awbb_LowBrGrZones_m_BGrid[47]0000
	{0x0F12,0x0000},  //0000	//awbb_LowBrGrZones_m_BGrid[48]0000
	{0x0F12,0x0000},  //0000	//awbb_LowBrGrZones_m_BGrid[49]0000
	//	param_end	awbb_LowBrGrZones_m_BGrid
	{0x002A,0x0de8},  //#awbb_LowBrGrZones_m_GridStep
	{0x0F12,0x0006},
	{0x0F12,0x0000},
	{0x002A,0x0df0},  //#awbb_LowBrGrZones_m_Boffs
	{0x0F12,0x0081},
	{0x0F12,0x0000},
	//	param_start	awbb_OutdoorGrZones_m_BGrid
	{0x002A,0x0D08},
	{0x0F12,0x0257},  //02C5  //0349  //035E	//awbb_OutdoorGrZones_m_BGrid[0] 036B
	{0x0F12,0x0287},  //032D  //0363  //0378	//awbb_OutdoorGrZones_m_BGrid[1] 0385
	{0x0F12,0x0245},  //02B2  //032C  //0341	//awbb_OutdoorGrZones_m_BGrid[2] 034E
	{0x0F12,0x0287},  //0315  //036E  //0383	//awbb_OutdoorGrZones_m_BGrid[3] 0390
	{0x0F12,0x0237},  //02A0  //030F  //0324	//awbb_OutdoorGrZones_m_BGrid[4] 0331
	{0x0F12,0x027D},  //02FE  //036E  //0383	//awbb_OutdoorGrZones_m_BGrid[5] 0390
	{0x0F12,0x022A},  //0290  //02F3  //0308	//awbb_OutdoorGrZones_m_BGrid[6] 0315
	{0x0F12,0x0270},  //02E9  //035C  //0371	//awbb_OutdoorGrZones_m_BGrid[7] 037E
	{0x0F12,0x0220},  //0280  //02D8  //02ED	//awbb_OutdoorGrZones_m_BGrid[8] 02FA
	{0x0F12,0x0263},  //02D7  //0343  //0358	//awbb_OutdoorGrZones_m_BGrid[9] 0365
	{0x0F12,0x0217},  //0274  //02C5  //02DA	//awbb_OutdoorGrZones_m_BGrid[10]02E7
	{0x0F12,0x0256},  //02C7  //032D  //0342	//awbb_OutdoorGrZones_m_BGrid[11]034F
	{0x0F12,0x020F},  //0266  //02B2  //02C7	//awbb_OutdoorGrZones_m_BGrid[12]02D4
	{0x0F12,0x024A},  //02B9  //0315  //032A	//awbb_OutdoorGrZones_m_BGrid[13]0337
	{0x0F12,0x020F},  //0259  //02A0  //02B5	//awbb_OutdoorGrZones_m_BGrid[14]02C2
	{0x0F12,0x023E},  //02AF  //02FE  //0313	//awbb_OutdoorGrZones_m_BGrid[15]0320
	{0x0F12,0x0000},  //024F  //0290  //02A5	//awbb_OutdoorGrZones_m_BGrid[16]02B2
	{0x0F12,0x0000},  //02A3  //02E9  //02FB	//awbb_OutdoorGrZones_m_BGrid[17]0308
	{0x0F12,0x0000},  //0241  //0280  //0295	//awbb_OutdoorGrZones_m_BGrid[18]02A2
	{0x0F12,0x0000},  //0297  //02D7  //02EB	//awbb_OutdoorGrZones_m_BGrid[19]02F8
	{0x0F12,0x0000},  //0235  //0274  //0289	//awbb_OutdoorGrZones_m_BGrid[20]0296
	{0x0F12,0x0000},  //0287  //02C7  //02DB	//awbb_OutdoorGrZones_m_BGrid[21]02E8
	{0x0F12,0x0000},  //022C  //0266  //027B	//awbb_OutdoorGrZones_m_BGrid[22]0288
	{0x0F12,0x0000},  //027C  //02B9  //02CC	//awbb_OutdoorGrZones_m_BGrid[23]02D9
	{0x0F12,0x0000},  //0222  //0259  //026E	//awbb_OutdoorGrZones_m_BGrid[24]027B
	{0x0F12,0x0000},  //0271  //02AF  //02C0	//awbb_OutdoorGrZones_m_BGrid[25]02CD
	{0x0F12,0x0000},  //021A  //024F  //0260	//awbb_OutdoorGrZones_m_BGrid[26]026D
	{0x0F12,0x0000},  //026A  //02A3  //02B3	//awbb_OutdoorGrZones_m_BGrid[27]02C0
	{0x0F12,0x0000},  //0213  //0241  //0256	//awbb_OutdoorGrZones_m_BGrid[28]0263
	{0x0F12,0x0000},  //0261  //0297  //02A7	//awbb_OutdoorGrZones_m_BGrid[29]02B4
	{0x0F12,0x0000},  //0209  //0235  //024A	//awbb_OutdoorGrZones_m_BGrid[30]0257
	{0x0F12,0x0000},  //0254  //0287  //029C	//awbb_OutdoorGrZones_m_BGrid[31]02A9
	{0x0F12,0x0000},  //01FE  //022C  //023F	//awbb_OutdoorGrZones_m_BGrid[32]024C
	{0x0F12,0x0000},  //0248  //027C  //028D	//awbb_OutdoorGrZones_m_BGrid[33]029A
	{0x0F12,0x0000},  //01F0  //0222  //0234	//awbb_OutdoorGrZones_m_BGrid[34]0241
	{0x0F12,0x0000},  //023A  //0271  //0283	//awbb_OutdoorGrZones_m_BGrid[35]0290
	{0x0F12,0x0000},  //01D4  //021A  //022C	//awbb_OutdoorGrZones_m_BGrid[36]0239
	{0x0F12,0x0000},  //0228  //026A  //0279	//awbb_OutdoorGrZones_m_BGrid[37]0286
	{0x0F12,0x0000},  //01EB  //0213  //0225	//awbb_OutdoorGrZones_m_BGrid[38]0232
	{0x0F12,0x0000},  //0209  //0261  //0271	//awbb_OutdoorGrZones_m_BGrid[39]027E
	{0x0F12,0x0000},  //0000  //0209  //021A	//awbb_OutdoorGrZones_m_BGrid[40]0227
	{0x0F12,0x0000},  //0000  //0254  //0266	//awbb_OutdoorGrZones_m_BGrid[41]0273
	{0x0F12,0x0000},  //0000  //01FE  //0210	//awbb_OutdoorGrZones_m_BGrid[42]021D
	{0x0F12,0x0000},  //0000  //0248  //0259	//awbb_OutdoorGrZones_m_BGrid[43]0266
	{0x0F12,0x0000},  //0000  //01F0  //0205	//awbb_OutdoorGrZones_m_BGrid[44]0212
	{0x0F12,0x0000},  //0000  //023A  //024A	//awbb_OutdoorGrZones_m_BGrid[45]0257
	{0x0F12,0x0000},  //0000  //01D4  //01E9	//awbb_OutdoorGrZones_m_BGrid[46]0202
	{0x0F12,0x0000},  //0000  //0228  //0239	//awbb_OutdoorGrZones_m_BGrid[47]0246
	{0x0F12,0x0000},  //0000  //01EB  //01F8	//awbb_OutdoorGrZones_m_BGrid[48]0209
	{0x0F12,0x0000},  //0000  //0209  //021E	//awbb_OutdoorGrZones_m_BGrid[49]022B
	//	param_end	awbb_OutdoorGrZones_m_BGrid
	{0x002A,0x0d6c},  //#awbb_OutdoorGrZones_m_GridStep
	{0x0F12,0x0004},
	{0x0F12,0x0000},
	{0x002A,0x0d74},  //#awbb_OutdoorGrZones_m_Boffs
	{0x0F12,0x0222}, //0195 //0145 //013d //0131
	{0x0F12,0x0000},
	{0x002A,0x0e00},  //#awbb_CrclLowT_R_c
	{0x0F12,0x03cf}, //034A
	{0x0F12,0x0000},
	{0x002A,0x0e04},  //#awbb_CrclLowT_B_c
	{0x0F12,0x012e}, //0176
	{0x0F12,0x0000},
	{0x002A,0x0e08},  //#awbb_CrclLowT_Rad_c
	{0x0F12,0xdf21}, //67C3 //6fc3 //71B8
	{0x0F12,0x0000},
	{0x002A,0x0E1A},
	{0x0F12,0x0135}, //012F
	{0x0F12,0x0120},

	//// awbb_LowTempRB
	//002A 0E68
	//0F12 04F2

	//	param_start	SARR_usGammaLutRGBIndoor
	{0x002A,0x04C8},
	{0x0F12,0x0000},
	{0x0F12,0x0004},
	{0x0F12,0x0009},
	{0x0F12,0x0015},
	{0x0F12,0x0034},
	{0x0F12,0x0088},
	{0x0F12,0x00ED},
	{0x0F12,0x0155},
	{0x0F12,0x0201},
	{0x0F12,0x0276},
	{0x0F12,0x0307},
	{0x0F12,0x0362},
	{0x0F12,0x03A9},
	{0x0F12,0x03D5},
	{0x0F12,0x03F4},
	{0x0F12,0x03FF},
	{0x0F12,0x0000},
	{0x0F12,0x0004},
	{0x0F12,0x0009},
	{0x0F12,0x0015},
	{0x0F12,0x0034},
	{0x0F12,0x0088},
	{0x0F12,0x00ED},
	{0x0F12,0x0155},
	{0x0F12,0x0201},
	{0x0F12,0x0276},
	{0x0F12,0x0307},
	{0x0F12,0x0362},
	{0x0F12,0x03A9},
	{0x0F12,0x03D5},
	{0x0F12,0x03F4},
	{0x0F12,0x03FF},
	{0x0F12,0x0000},
	{0x0F12,0x0004},
	{0x0F12,0x0009},
	{0x0F12,0x0015},
	{0x0F12,0x0034},
	{0x0F12,0x0088},
	{0x0F12,0x00ED},
	{0x0F12,0x0155},
	{0x0F12,0x0201},
	{0x0F12,0x0276},
	{0x0F12,0x0307},
	{0x0F12,0x0362},
	{0x0F12,0x03A9},
	{0x0F12,0x03D5},
	{0x0F12,0x03F4},
	{0x0F12,0x03FF},
	//	param_end	SARR_usGammaLutRGBIndoor


	{0x002A,0x1000},
	{0x0F12,0x003e}, // AE Target

	// SAAR_IllumType
	{0x002A,0x108E},
	{0x0F12,0x00C0},
	{0x0F12,0x00E7},
	{0x0F12,0x00F9},
	{0x0F12,0x0142},
	{0x0F12,0x0179},
	{0x0F12,0x01A4},
	{0x0F12,0x01B8},   //SAAR_IllumF
	{0x0F12,0x0112},
	{0x0F12,0x0122},
	{0x0F12,0x0136},
	{0x0F12,0x00F6},
	{0x0F12,0x0100},
	{0x0F12,0x00FE},
	{0x0F12,0x0100},

	{0x002A,0x1AC8},
	{0x0F12,0x0000},   //	param_start	TVAR_wbt_pBaseCcms
	{0x002A,0x23A4},
	{0x0F12,0x0213},
	{0x0F12,0xFF95},
	{0x0F12,0xFFF6},
	{0x0F12,0x0067},
	{0x0F12,0x0118},
	{0x0F12,0xFF1F},
	{0x0F12,0xFF0A},
	{0x0F12,0x01A9},
	{0x0F12,0xFF6E},
	{0x0F12,0xFECE},
	{0x0F12,0x01C7},
	{0x0F12,0x012E},
	{0x0F12,0xFFE9},
	{0x0F12,0x0009},
	{0x0F12,0x01FD},
	{0x0F12,0x015B},
	{0x0F12,0xFF0C},
	{0x0F12,0x014F},
	{0x0F12,0x0213},
	{0x0F12,0xFF95},
	{0x0F12,0xFFF6},
	{0x0F12,0x0067},
	{0x0F12,0x0118},
	{0x0F12,0xFF1F},
	{0x0F12,0xFF0A},
	{0x0F12,0x01A9},
	{0x0F12,0xFF6E},
	{0x0F12,0xFECE},
	{0x0F12,0x01C7},
	{0x0F12,0x012E},
	{0x0F12,0xFFE9},
	{0x0F12,0x0009},
	{0x0F12,0x01FD},
	{0x0F12,0x015B},
	{0x0F12,0xFF0C},
	{0x0F12,0x014F},
	{0x0F12,0x0213},
	{0x0F12,0xFF95},
	{0x0F12,0xFFF6},
	{0x0F12,0x0067},
	{0x0F12,0x0118},
	{0x0F12,0xFF1F},
	{0x0F12,0xFF0A},
	{0x0F12,0x01A9},
	{0x0F12,0xFF6E},
	{0x0F12,0xFECE},
	{0x0F12,0x01C7},
	{0x0F12,0x012E},
	{0x0F12,0xFFE9},
	{0x0F12,0x0009},
	{0x0F12,0x01FD},
	{0x0F12,0x015B},
	{0x0F12,0xFF0C},
	{0x0F12,0x014F},
	{0x0F12,0x0213},
	{0x0F12,0xFF95},
	{0x0F12,0xFFF6},
	{0x0F12,0x0067},
	{0x0F12,0x0118},
	{0x0F12,0xFF1F},
	{0x0F12,0xFF0A},
	{0x0F12,0x01A9},
	{0x0F12,0xFF6E},
	{0x0F12,0xFECE},
	{0x0F12,0x01C7},
	{0x0F12,0x012E},
	{0x0F12,0xFFE9},
	{0x0F12,0x0009},
	{0x0F12,0x01FD},
	{0x0F12,0x015B},
	{0x0F12,0xFF0C},
	{0x0F12,0x014F},
	{0x0F12,0x0213},
	{0x0F12,0xFF95},
	{0x0F12,0xFFF6},
	{0x0F12,0x0067},
	{0x0F12,0x0118},
	{0x0F12,0xFF1F},
	{0x0F12,0xFF0A},
	{0x0F12,0x01A9},
	{0x0F12,0xFF6E},
	{0x0F12,0xFECE},
	{0x0F12,0x01C7},
	{0x0F12,0x012E},
	{0x0F12,0xFFE9},
	{0x0F12,0x0009},
	{0x0F12,0x01FD},
	{0x0F12,0x015B},
	{0x0F12,0xFF0C},
	{0x0F12,0x014F},
	{0x0F12,0x0213},
	{0x0F12,0xFF95},
	{0x0F12,0xFFF6},
	{0x0F12,0x0067},
	{0x0F12,0x0118},
	{0x0F12,0xFF1F},
	{0x0F12,0xFF0A},
	{0x0F12,0x01A9},
	{0x0F12,0xFF6E},
	{0x0F12,0xFECE},
	{0x0F12,0x01C7},
	{0x0F12,0x012E},
	{0x0F12,0xFFE9},
	{0x0F12,0x0009},
	{0x0F12,0x01FD},
	{0x0F12,0x015B},
	{0x0F12,0xFF0C},
	{0x0F12,0x014F},
	//	param_end	TVAR_wbt_pBaseCcms
	//	param_start	TVAR_wbt_pOutdoorCcm
	{0x002A,0x2380},
	{0x0F12,0x01AF},
	{0x0F12,0xFFD5},
	{0x0F12,0x001D},
	{0x0F12,0x0080},
	{0x0F12,0x00BA},
	{0x0F12,0xFF61},
	{0x0F12,0xFF21},
	{0x0F12,0x0164},
	{0x0F12,0xFF96},
	{0x0F12,0xFF0F},
	{0x0F12,0x019A},
	{0x0F12,0x0117},
	{0x0F12,0xFFE8},
	{0x0F12,0x0041},
	{0x0F12,0x01C8},
	{0x0F12,0x0173},
	{0x0F12,0xFF35},
	{0x0F12,0x013C},   //	param_end	TVAR_wbt_pOutdoorCcm

	{0x002A,0x06DA},
	{0x0F12,0x00BF},
	{0x0F12,0x00E6},
	{0x0F12,0x00F2},
	{0x0F12,0x0143},
	{0x0F12,0x0178},
	{0x0F12,0x01A3},  //	param_start	SARR_uNormBrInDoor
	{0x002A,0x07E8},
	{0x0F12,0x000A},
	{0x0F12,0x0019},
	{0x0F12,0x007D},
	{0x0F12,0x01F4},
	{0x0F12,0x1388},
	//	param_end	SARR_uNormBrInDoor
	//	param_start	afit_uNoiseIndInDoor
	{0x002A,0x07D0},
	{0x0F12,0x0030},
	{0x0F12,0x0046},
	{0x0F12,0x0088},
	{0x0F12,0x0205},
	{0x0F12,0x02BC},
	//	param_end	afit_uNoiseIndInDoor
	{0x002A,0x07E6},
	{0x0F12,0x0001},
	//	param_start	TVAR_afit_pBaseVals
	{0x002A,0x0828},
	{0x0F12,0x000B},   //TVAR_afit_pBaseVals[0]
	{0x0F12,0x0000}, 	 //TVAR_afit_pBaseVals[1]
	{0x0F12,0x0000}, 	 //TVAR_afit_pBaseVals[2]
	{0x0F12,0xFFAA}, 	 //TVAR_afit_pBaseVals[3]
	{0x0F12,0x0019}, 	 //TVAR_afit_pBaseVals[4]
	{0x0F12,0x03FF}, 	 //TVAR_afit_pBaseVals[5]
	{0x0F12,0x0032}, 	 //TVAR_afit_pBaseVals[6]
	{0x0F12,0x000F}, 	 //TVAR_afit_pBaseVals[7]
	{0x0F12,0x00FF}, 	 //TVAR_afit_pBaseVals[8]
	{0x0F12,0x000A}, 	 //TVAR_afit_pBaseVals[9]
	{0x0F12,0x0004}, 	 //TVAR_afit_pBaseVals[10]
	{0x0F12,0x000A}, 	 //TVAR_afit_pBaseVals[11]
	{0x0F12,0x0014}, 	 //TVAR_afit_pBaseVals[12]
	{0x0F12,0x0064}, 	 //TVAR_afit_pBaseVals[13]
	{0x0F12,0x03FF}, 	 //TVAR_afit_pBaseVals[14]
	{0x0F12,0x03FF}, 	 //TVAR_afit_pBaseVals[15]
	{0x0F12,0x0000}, 	 //TVAR_afit_pBaseVals[16]
	{0x0F12,0x03FF}, 	 //TVAR_afit_pBaseVals[17]
	{0x0F12,0x03FF}, 	 //TVAR_afit_pBaseVals[18]
	{0x0F12,0x03FF}, 	 //TVAR_afit_pBaseVals[19]
	{0x0F12,0x03FF}, 	 //TVAR_afit_pBaseVals[20]
	{0x0F12,0x0028}, 	 //TVAR_afit_pBaseVals[21]
	{0x0F12,0x0028}, 	 //TVAR_afit_pBaseVals[22]
	{0x0F12,0x0028}, 	 //TVAR_afit_pBaseVals[23]
	{0x0F12,0x0028}, 	 //TVAR_afit_pBaseVals[24]
	{0x0F12,0x0028}, 	 //TVAR_afit_pBaseVals[25]
	{0x0F12,0x0028}, 	 //TVAR_afit_pBaseVals[26]
	{0x0F12,0x0028}, 	 //TVAR_afit_pBaseVals[27]
	{0x0F12,0x0028}, 	 //TVAR_afit_pBaseVals[28]
	{0x0F12,0x0106}, 	 //TVAR_afit_pBaseVals[29]
	{0x0F12,0x006F}, 	 //TVAR_afit_pBaseVals[30]
	{0x0F12,0x054C}, 	 //TVAR_afit_pBaseVals[31]
	{0x0F12,0x0A4C}, 	 //TVAR_afit_pBaseVals[32]
	{0x0F12,0x0203}, 	 //TVAR_afit_pBaseVals[33]
	{0x0F12,0x0203}, 	 //TVAR_afit_pBaseVals[34]
	{0x0F12,0x0000}, 	 //TVAR_afit_pBaseVals[35]
	{0x0F12,0x0000}, 	 //TVAR_afit_pBaseVals[36]
	{0x0F12,0x2828}, 	 //TVAR_afit_pBaseVals[37]
	{0x0F12,0x0000}, 	 //TVAR_afit_pBaseVals[38]
	{0x0F12,0x0230}, 	 //TVAR_afit_pBaseVals[39]
	{0x0F12,0x0480}, 	 //TVAR_afit_pBaseVals[40]
	{0x0F12,0x0804}, 	 //TVAR_afit_pBaseVals[41]
	{0x0F12,0x030A}, 	 //TVAR_afit_pBaseVals[42]
	{0x0F12,0x1403}, 	 //TVAR_afit_pBaseVals[43]
	{0x0F12,0x1414}, 	 //TVAR_afit_pBaseVals[44]
	{0x0F12,0x0014}, 	 //TVAR_afit_pBaseVals[45]
	{0x0F12,0x0500}, 	 //TVAR_afit_pBaseVals[46]
	{0x0F12,0x1414}, 	 //TVAR_afit_pBaseVals[47]
	{0x0F12,0x1414}, 	 //TVAR_afit_pBaseVals[48]
	{0x0F12,0x0A0A}, 	 //TVAR_afit_pBaseVals[49]
	{0x0F12,0x0005}, 	 //TVAR_afit_pBaseVals[50]
	{0x0F12,0x0200}, 	 //TVAR_afit_pBaseVals[51]
	{0x0F12,0x0400}, 	 //TVAR_afit_pBaseVals[52]
	{0x0F12,0x0200}, 	 //TVAR_afit_pBaseVals[53]
	{0x0F12,0x0000}, 	 //TVAR_afit_pBaseVals[54]
	{0x0F12,0x0A00}, 	 //TVAR_afit_pBaseVals[55]
	{0x0F12,0x100A}, 	 //TVAR_afit_pBaseVals[56]
	{0x0F12,0x0150}, 	 //TVAR_afit_pBaseVals[57]
	{0x0F12,0x0130}, 	 //TVAR_afit_pBaseVals[58]
	{0x0F12,0x0100}, 	 //TVAR_afit_pBaseVals[59]
	{0x0F12,0x8032},   //805A	 //TVAR_afit_pBaseVals[60]
	{0x0F12,0x0180}, 	 //TVAR_afit_pBaseVals[61]
	{0x0F12,0x001C}, 	 //TVAR_afit_pBaseVals[62]
	{0x0F12,0x0000}, 	 //TVAR_afit_pBaseVals[63]
	{0x0F12,0x0000}, 	 //TVAR_afit_pBaseVals[64]
	{0x0F12,0x0000}, 	 //TVAR_afit_pBaseVals[65]
	{0x0F12,0x0000}, 	 //TVAR_afit_pBaseVals[66]
	{0x0F12,0x000C}, 	 //TVAR_afit_pBaseVals[67]
	{0x0F12,0x03FF}, 	 //TVAR_afit_pBaseVals[68]
	{0x0F12,0x0014}, 	 //TVAR_afit_pBaseVals[69]
	{0x0F12,0x000F}, 	 //TVAR_afit_pBaseVals[70]
	{0x0F12,0x00A0}, 	 //TVAR_afit_pBaseVals[71]
	{0x0F12,0x000A}, 	 //TVAR_afit_pBaseVals[72]
	{0x0F12,0x0004}, 	 //TVAR_afit_pBaseVals[73]
	{0x0F12,0x0012}, 	 //TVAR_afit_pBaseVals[74]
	{0x0F12,0x0014}, 	 //TVAR_afit_pBaseVals[75]
	{0x0F12,0x0064}, 	 //TVAR_afit_pBaseVals[76]
	{0x0F12,0x03FF}, 	 //TVAR_afit_pBaseVals[77]
	{0x0F12,0x03FF}, 	 //TVAR_afit_pBaseVals[78]
	{0x0F12,0x0000}, 	 //TVAR_afit_pBaseVals[79]
	{0x0F12,0x0200}, 	 //TVAR_afit_pBaseVals[80]
	{0x0F12,0x0200}, 	 //TVAR_afit_pBaseVals[81]
	{0x0F12,0x03FF}, 	 //TVAR_afit_pBaseVals[82]
	{0x0F12,0x03FF}, 	 //TVAR_afit_pBaseVals[83]
	{0x0F12,0x0028}, 	 //TVAR_afit_pBaseVals[84]
	{0x0F12,0x0028}, 	 //TVAR_afit_pBaseVals[85]
	{0x0F12,0x0028}, 	 //TVAR_afit_pBaseVals[86]
	{0x0F12,0x0028}, 	 //TVAR_afit_pBaseVals[87]
	{0x0F12,0x0028}, 	 //TVAR_afit_pBaseVals[88]
	{0x0F12,0x0028}, 	 //TVAR_afit_pBaseVals[89]
	{0x0F12,0x0028}, 	 //TVAR_afit_pBaseVals[90]
	{0x0F12,0x0028}, 	 //TVAR_afit_pBaseVals[91]
	{0x0F12,0x0106}, 	 //TVAR_afit_pBaseVals[92]
	{0x0F12,0x006F}, 	 //TVAR_afit_pBaseVals[93]
	{0x0F12,0x053D}, 	 //TVAR_afit_pBaseVals[94]
	{0x0F12,0x0A3D}, 	 //TVAR_afit_pBaseVals[95]
	{0x0F12,0x0203}, 	 //TVAR_afit_pBaseVals[96]
	{0x0F12,0x0203}, 	 //TVAR_afit_pBaseVals[97]
	{0x0F12,0x0C23}, 	 //TVAR_afit_pBaseVals[98]
	{0x0F12,0x230C}, 	 //TVAR_afit_pBaseVals[99]
	{0x0F12,0x1919}, 	 //TVAR_afit_pBaseVals[100]
	{0x0F12,0x0303}, 	 //TVAR_afit_pBaseVals[101]
	{0x0F12,0x0220}, 	 //TVAR_afit_pBaseVals[102]
	{0x0F12,0x0480}, 	 //TVAR_afit_pBaseVals[103]
	{0x0F12,0x0804}, 	 //TVAR_afit_pBaseVals[104]
	{0x0F12,0x030A}, 	 //TVAR_afit_pBaseVals[105]
	{0x0F12,0x1403}, 	 //TVAR_afit_pBaseVals[106]
	{0x0F12,0x1414}, 	 //TVAR_afit_pBaseVals[107]
	{0x0F12,0x0014}, 	 //TVAR_afit_pBaseVals[108]
	{0x0F12,0x0500}, 	 //TVAR_afit_pBaseVals[109]
	{0x0F12,0x1414}, 	 //TVAR_afit_pBaseVals[110]
	{0x0F12,0x1414}, 	 //TVAR_afit_pBaseVals[111]
	{0x0F12,0x0A0A}, 	 //TVAR_afit_pBaseVals[112]
	{0x0F12,0x0005}, 	 //TVAR_afit_pBaseVals[113]
	{0x0F12,0x0200}, 	 //TVAR_afit_pBaseVals[114]
	{0x0F12,0x0400}, 	 //TVAR_afit_pBaseVals[115]
	{0x0F12,0x0200}, 	 //TVAR_afit_pBaseVals[116]
	{0x0F12,0x0000}, 	 //TVAR_afit_pBaseVals[117]
	{0x0F12,0x0500}, 	 //TVAR_afit_pBaseVals[118]
	{0x0F12,0x1008}, 	 //TVAR_afit_pBaseVals[119]
	{0x0F12,0x0160}, 	 //TVAR_afit_pBaseVals[120]
	{0x0F12,0x0150}, 	 //TVAR_afit_pBaseVals[121]
	{0x0F12,0x0100}, 	 //TVAR_afit_pBaseVals[122]
	{0x0F12,0x803C},   //8064	 //TVAR_afit_pBaseVals[123]
	{0x0F12,0x0180}, 	 //TVAR_afit_pBaseVals[124]
	{0x0F12,0x0016}, 	 //TVAR_afit_pBaseVals[125]
	{0x0F12,0x0000}, 	 //TVAR_afit_pBaseVals[126]
	{0x0F12,0x0000}, 	 //TVAR_afit_pBaseVals[127]
	{0x0F12,0x0000}, 	 //TVAR_afit_pBaseVals[128]
	{0x0F12,0x0000}, 	 //TVAR_afit_pBaseVals[129]
	{0x0F12,0x0000}, 	 //TVAR_afit_pBaseVals[130]
	{0x0F12,0x03FF}, 	 //TVAR_afit_pBaseVals[131]
	{0x0F12,0x000F}, 	 //TVAR_afit_pBaseVals[132]
	{0x0F12,0x000F}, 	 //TVAR_afit_pBaseVals[133]
	{0x0F12,0x0050}, 	 //TVAR_afit_pBaseVals[134]
	{0x0F12,0x000A}, 	 //TVAR_afit_pBaseVals[135]
	{0x0F12,0x0003}, 	 //TVAR_afit_pBaseVals[136]
	{0x0F12,0x0019}, 	 //TVAR_afit_pBaseVals[137]
	{0x0F12,0x0014}, 	 //TVAR_afit_pBaseVals[138]
	{0x0F12,0x0064}, 	 //TVAR_afit_pBaseVals[139]
	{0x0F12,0x03FF}, 	 //TVAR_afit_pBaseVals[140]
	{0x0F12,0x03FF}, 	 //TVAR_afit_pBaseVals[141]
	{0x0F12,0x0000}, 	 //TVAR_afit_pBaseVals[142]
	{0x0F12,0x0002}, 	 //TVAR_afit_pBaseVals[143]
	{0x0F12,0x0002}, 	 //TVAR_afit_pBaseVals[144]
	{0x0F12,0x03FF}, 	 //TVAR_afit_pBaseVals[145]
	{0x0F12,0x03FF}, 	 //TVAR_afit_pBaseVals[146]
	{0x0F12,0x0028}, 	 //TVAR_afit_pBaseVals[147]
	{0x0F12,0x0028}, 	 //TVAR_afit_pBaseVals[148]
	{0x0F12,0x0028}, 	 //TVAR_afit_pBaseVals[149]
	{0x0F12,0x0028}, 	 //TVAR_afit_pBaseVals[150]
	{0x0F12,0x0028}, 	 //TVAR_afit_pBaseVals[151]
	{0x0F12,0x0028}, 	 //TVAR_afit_pBaseVals[152]
	{0x0F12,0x0028}, 	 //TVAR_afit_pBaseVals[153]
	{0x0F12,0x0028}, 	 //TVAR_afit_pBaseVals[154]
	{0x0F12,0x0106}, 	 //TVAR_afit_pBaseVals[155]
	{0x0F12,0x006F}, 	 //TVAR_afit_pBaseVals[156]
	{0x0F12,0x050A}, 	 //TVAR_afit_pBaseVals[157]
	{0x0F12,0x0A0A}, 	 //TVAR_afit_pBaseVals[158]
	{0x0F12,0x0203}, 	 //TVAR_afit_pBaseVals[159]
	{0x0F12,0x0203}, 	 //TVAR_afit_pBaseVals[160]
	{0x0F12,0x1946}, 	 //TVAR_afit_pBaseVals[161]
	{0x0F12,0x4619}, 	 //TVAR_afit_pBaseVals[162]
	{0x0F12,0x0F0F}, 	 //TVAR_afit_pBaseVals[163]
	{0x0F12,0x0606}, 	 //TVAR_afit_pBaseVals[164]
	{0x0F12,0x020F}, 	 //TVAR_afit_pBaseVals[165]
	{0x0F12,0x0480}, 	 //TVAR_afit_pBaseVals[166]
	{0x0F12,0x0804}, 	 //TVAR_afit_pBaseVals[167]
	{0x0F12,0x030A}, 	 //TVAR_afit_pBaseVals[168]
	{0x0F12,0x1403}, 	 //TVAR_afit_pBaseVals[169]
	{0x0F12,0x1414}, 	 //TVAR_afit_pBaseVals[170]
	{0x0F12,0x0014}, 	 //TVAR_afit_pBaseVals[171]
	{0x0F12,0x0500}, 	 //TVAR_afit_pBaseVals[172]
	{0x0F12,0x1414}, 	 //TVAR_afit_pBaseVals[173]
	{0x0F12,0x1414}, 	 //TVAR_afit_pBaseVals[174]
	{0x0F12,0x0A0A}, 	 //TVAR_afit_pBaseVals[175]
	{0x0F12,0x0005}, 	 //TVAR_afit_pBaseVals[176]
	{0x0F12,0x0202}, 	 //TVAR_afit_pBaseVals[177]
	{0x0F12,0x0404}, 	 //TVAR_afit_pBaseVals[178]
	{0x0F12,0x0202}, 	 //TVAR_afit_pBaseVals[179]
	{0x0F12,0x0402}, 	 //TVAR_afit_pBaseVals[180]
	{0x0F12,0x0002}, 	 //TVAR_afit_pBaseVals[181]
	{0x0F12,0x1006}, 	 //TVAR_afit_pBaseVals[182]
	{0x0F12,0x0180}, 	 //TVAR_afit_pBaseVals[183]
	{0x0F12,0x0180}, 	 //TVAR_afit_pBaseVals[184]
	{0x0F12,0x0100}, 	 //TVAR_afit_pBaseVals[185]
	{0x0F12,0x8046},  //0x8067	 //TVAR_afit_pBaseVals[186]
	{0x0F12,0x0180}, 	 //TVAR_afit_pBaseVals[187]
	{0x0F12,0x0000}, 	 //TVAR_afit_pBaseVals[188]
	{0x0F12,0x0000}, 	 //TVAR_afit_pBaseVals[189]
	{0x0F12,0x0000}, 	 //TVAR_afit_pBaseVals[190]
	{0x0F12,0x0000}, 	 //TVAR_afit_pBaseVals[191]
	{0x0F12,0x0000}, 	 //TVAR_afit_pBaseVals[192]
	{0x0F12,0x0000}, 	 //TVAR_afit_pBaseVals[193]
	{0x0F12,0x03FF}, 	 //TVAR_afit_pBaseVals[194]
	{0x0F12,0x000F}, 	 //TVAR_afit_pBaseVals[195]
	{0x0F12,0x000F}, 	 //TVAR_afit_pBaseVals[196]
	{0x0F12,0x000A}, 	 //TVAR_afit_pBaseVals[197]
	{0x0F12,0x000A}, 	 //TVAR_afit_pBaseVals[198]
	{0x0F12,0x0003}, 	 //TVAR_afit_pBaseVals[199]
	{0x0F12,0x0019}, 	 //TVAR_afit_pBaseVals[200]
	{0x0F12,0x0014}, 	 //TVAR_afit_pBaseVals[201]
	{0x0F12,0x0064}, 	 //TVAR_afit_pBaseVals[202]
	{0x0F12,0x03FF}, 	 //TVAR_afit_pBaseVals[203]
	{0x0F12,0x03FF}, 	 //TVAR_afit_pBaseVals[204]
	{0x0F12,0x0000}, 	 //TVAR_afit_pBaseVals[205]
	{0x0F12,0x0002}, 	 //TVAR_afit_pBaseVals[206]
	{0x0F12,0x0002}, 	 //TVAR_afit_pBaseVals[207]
	{0x0F12,0x03FF}, 	 //TVAR_afit_pBaseVals[208]
	{0x0F12,0x03FF}, 	 //TVAR_afit_pBaseVals[209]
	{0x0F12,0x0028}, 	 //TVAR_afit_pBaseVals[210]
	{0x0F12,0x0028}, 	 //TVAR_afit_pBaseVals[211]
	{0x0F12,0x0028}, 	 //TVAR_afit_pBaseVals[212]
	{0x0F12,0x0028}, 	 //TVAR_afit_pBaseVals[213]
	{0x0F12,0x0028}, 	 //TVAR_afit_pBaseVals[214]
	{0x0F12,0x0028}, 	 //TVAR_afit_pBaseVals[215]
	{0x0F12,0x0028}, 	 //TVAR_afit_pBaseVals[216]
	{0x0F12,0x0028}, 	 //TVAR_afit_pBaseVals[217]
	{0x0F12,0x0106}, 	 //TVAR_afit_pBaseVals[218]
	{0x0F12,0x006F}, 	 //TVAR_afit_pBaseVals[219]
	{0x0F12,0x0505}, 	 //TVAR_afit_pBaseVals[220]
	{0x0F12,0x0A0A}, 	 //TVAR_afit_pBaseVals[221]
	{0x0F12,0x0202}, 	 //TVAR_afit_pBaseVals[222]
	{0x0F12,0x0202}, 	 //TVAR_afit_pBaseVals[223]
	{0x0F12,0x1946}, 	 //TVAR_afit_pBaseVals[224]
	{0x0F12,0x4619}, 	 //TVAR_afit_pBaseVals[225]
	{0x0F12,0x0F0F}, 	 //TVAR_afit_pBaseVals[226]
	{0x0F12,0x0606}, 	 //TVAR_afit_pBaseVals[227]
	{0x0F12,0x020F}, 	 //TVAR_afit_pBaseVals[228]
	{0x0F12,0x0480}, 	 //TVAR_afit_pBaseVals[229]
	{0x0F12,0x0804}, 	 //TVAR_afit_pBaseVals[230]
	{0x0F12,0x030A}, 	 //TVAR_afit_pBaseVals[231]
	{0x0F12,0x1403}, 	 //TVAR_afit_pBaseVals[232]
	{0x0F12,0x1414}, 	 //TVAR_afit_pBaseVals[233]
	{0x0F12,0x0014}, 	 //TVAR_afit_pBaseVals[234]
	{0x0F12,0x0500}, 	 //TVAR_afit_pBaseVals[235]
	{0x0F12,0x1414}, 	 //TVAR_afit_pBaseVals[236]
	{0x0F12,0x1414}, 	 //TVAR_afit_pBaseVals[237]
	{0x0F12,0x0A0A}, 	 //TVAR_afit_pBaseVals[238]
	{0x0F12,0x0005}, 	 //TVAR_afit_pBaseVals[239]
	{0x0F12,0x0202}, 	 //TVAR_afit_pBaseVals[240]
	{0x0F12,0x0404}, 	 //TVAR_afit_pBaseVals[241]
	{0x0F12,0x0202}, 	 //TVAR_afit_pBaseVals[242]
	{0x0F12,0x0402}, 	 //TVAR_afit_pBaseVals[243]
	{0x0F12,0x0002}, 	 //TVAR_afit_pBaseVals[244]
	{0x0F12,0x1006}, 	 //TVAR_afit_pBaseVals[245]
	{0x0F12,0x0180}, 	 //TVAR_afit_pBaseVals[246]
	{0x0F12,0x0180}, 	 //TVAR_afit_pBaseVals[247]
	{0x0F12,0x0100}, 	 //TVAR_afit_pBaseVals[248]
	{0x0F12,0x8046}, //5A  //0x806B	 //TVAR_afit_pBaseVals[249]
	{0x0F12,0x0180}, 	 //TVAR_afit_pBaseVals[250]
	{0x0F12,0x0000}, 	 //TVAR_afit_pBaseVals[251]
	{0x0F12,0x0000}, 	 //TVAR_afit_pBaseVals[252]
	{0x0F12,0x0000}, 	 //TVAR_afit_pBaseVals[253]
	{0x0F12,0x0000}, 	 //TVAR_afit_pBaseVals[254]
	{0x0F12,0x0000}, 	 //TVAR_afit_pBaseVals[255]
	{0x0F12,0x0000}, 	 //TVAR_afit_pBaseVals[256]
	{0x0F12,0x03FF}, 	 //TVAR_afit_pBaseVals[257]
	{0x0F12,0x0005}, 	 //TVAR_afit_pBaseVals[258]
	{0x0F12,0x0005}, 	 //TVAR_afit_pBaseVals[259]
	{0x0F12,0x0005}, 	 //TVAR_afit_pBaseVals[260]
	{0x0F12,0x0005}, 	 //TVAR_afit_pBaseVals[261]
	{0x0F12,0x0003}, 	 //TVAR_afit_pBaseVals[262]
	{0x0F12,0x0019}, 	 //TVAR_afit_pBaseVals[263]
	{0x0F12,0x0014}, 	 //TVAR_afit_pBaseVals[264]
	{0x0F12,0x0064}, 	 //TVAR_afit_pBaseVals[265]
	{0x0F12,0x03FF}, 	 //TVAR_afit_pBaseVals[266]
	{0x0F12,0x03FF}, 	 //TVAR_afit_pBaseVals[267]
	{0x0F12,0x0000}, 	 //TVAR_afit_pBaseVals[268]
	{0x0F12,0x000F}, 	 //TVAR_afit_pBaseVals[269]
	{0x0F12,0x000F}, 	 //TVAR_afit_pBaseVals[270]
	{0x0F12,0x03FF}, 	 //TVAR_afit_pBaseVals[271]
	{0x0F12,0x03FF}, 	 //TVAR_afit_pBaseVals[272]
	{0x0F12,0x0028}, 	 //TVAR_afit_pBaseVals[273]
	{0x0F12,0x0028}, 	 //TVAR_afit_pBaseVals[274]
	{0x0F12,0x0028}, 	 //TVAR_afit_pBaseVals[275]
	{0x0F12,0x0028}, 	 //TVAR_afit_pBaseVals[276]
	{0x0F12,0x0028}, 	 //TVAR_afit_pBaseVals[277]
	{0x0F12,0x0028}, 	 //TVAR_afit_pBaseVals[278]
	{0x0F12,0x0028}, 	 //TVAR_afit_pBaseVals[279]
	{0x0F12,0x0028}, 	 //TVAR_afit_pBaseVals[280]
	{0x0F12,0x0106}, 	 //TVAR_afit_pBaseVals[281]
	{0x0F12,0x006F}, 	 //TVAR_afit_pBaseVals[282]
	{0x0F12,0x0505}, 	 //TVAR_afit_pBaseVals[283]
	{0x0F12,0x0A0A}, 	 //TVAR_afit_pBaseVals[284]
	{0x0F12,0x0202}, 	 //TVAR_afit_pBaseVals[285]
	{0x0F12,0x0202}, 	 //TVAR_afit_pBaseVals[286]
	{0x0F12,0x1946}, 	 //TVAR_afit_pBaseVals[287]
	{0x0F12,0x4619}, 	 //TVAR_afit_pBaseVals[288]
	{0x0F12,0x0F0F}, 	 //TVAR_afit_pBaseVals[289]
	{0x0F12,0x0606}, 	 //TVAR_afit_pBaseVals[290]
	{0x0F12,0x020F}, 	 //TVAR_afit_pBaseVals[291]
	{0x0F12,0x0480}, 	 //TVAR_afit_pBaseVals[292]
	{0x0F12,0x0804}, 	 //TVAR_afit_pBaseVals[293]
	{0x0F12,0x030A}, 	 //TVAR_afit_pBaseVals[294]
	{0x0F12,0x1403}, 	 //TVAR_afit_pBaseVals[295]
	{0x0F12,0x1414}, 	 //TVAR_afit_pBaseVals[296]
	{0x0F12,0x0014}, 	 //TVAR_afit_pBaseVals[297]
	{0x0F12,0x0500}, 	 //TVAR_afit_pBaseVals[298]
	{0x0F12,0x1414}, 	 //TVAR_afit_pBaseVals[299]
	{0x0F12,0x1414}, 	 //TVAR_afit_pBaseVals[300]
	{0x0F12,0x0A0A}, 	 //TVAR_afit_pBaseVals[301]
	{0x0F12,0x0005}, 	 //TVAR_afit_pBaseVals[302]
	{0x0F12,0x0303}, 	 //TVAR_afit_pBaseVals[303]
	{0x0F12,0x0707}, 	 //TVAR_afit_pBaseVals[304]
	{0x0F12,0x0303}, 	 //TVAR_afit_pBaseVals[305]
	{0x0F12,0x0806}, 	 //TVAR_afit_pBaseVals[306]
	{0x0F12,0x0004}, 	 //TVAR_afit_pBaseVals[307]
	{0x0F12,0x1006}, 	 //TVAR_afit_pBaseVals[308]
	{0x0F12,0x0180}, 	 //TVAR_afit_pBaseVals[309]
	{0x0F12,0x0180}, 	 //TVAR_afit_pBaseVals[310]
	{0x0F12,0x0100}, 	 //TVAR_afit_pBaseVals[311]
	{0x0F12,0x8046}, //5A   //0x806B //TVAR_afit_pBaseVals[312]
	{0x0F12,0x0180}, 	 //TVAR_afit_pBaseVals[313]
	{0x0F12,0x0000},      ////TVAR_afit_pBaseVals[314]  	param_end	TVAR_afit_pBaseVals
	              	  //	param_sta afit_pConstBaseVals
	{0x0F12,0x03FC},
	{0x0F12,0x03FF},
	{0x0F12,0x0500},
	{0x0F12,0x0400},
	{0x0F12,0x0000},
	{0x0F12,0x0000},
	{0x0F12,0x0000},
	{0x0F12,0x0300},
	{0x0F12,0x0002},
	{0x0F12,0x0300},
	{0x0F12,0x0203},
	{0x0F12,0x0003},
	{0x0F12,0x0000},
	{0x0F12,0x0204},
	{0x0F12,0x0000},
	{0x0F12,0xFDD6},
	{0x0F12,0x43C1},
	{0x0F12,0xF0FA},
	{0x0F12,0x0001},  //	param_end	afit_pConstBaseVals

	{0x002A,0x06d4},  //#TVAR_wbt_pOutdoorCcm
	{0x0F12,0x2380},
	{0x0F12,0x7000},
	{0x002A,0x06cc},  //#TVAR_wbt_pBaseCcms
	{0x0F12,0x23A4},
	{0x0F12,0x7000},
	{0x002A,0x06e8},  //#TVAR_wbt_pBaseCcmsAddr[0]
	{0x0F12,0x23A4},
	{0x0F12,0x7000},
	{0x002A,0x06EC},  //#TVAR_wbt_pBaseCcmsAddr[1]
	{0x0F12,0x23C8},
	{0x0F12,0x7000},
	{0x002A,0x06f0},  //#TVAR_wbt_pBaseCcmsAddr[2]
	{0x0F12,0x23EC},
	{0x0F12,0x7000},
	{0x002A,0x06f4},  //#TVAR_wbt_pBaseCcmsAddr[3]
	{0x0F12,0x2410},
	{0x0F12,0x7000},
	{0x002A,0x06f8},  //#TVAR_wbt_pBaseCcmsAddr[4]
	{0x0F12,0x2434},
	{0x0F12,0x7000},
	{0x002A,0x06fc},  //#TVAR_wbt_pBaseCcmsAddr[5]
	{0x0F12,0x2458},
	{0x0F12,0x7000},  // End tuning part
	{0x1000,0x0001},
	//1000	0001	// Set host interrupt so main start run
	//p10	// Wait10mSec
	{SEQUENCE_WAIT_MS,20},



	// Set Flicker

	//=================================================================================================
	// Set Flicker
	//=================================================================================================

	////1. Auto Flicker 60Hz Start
	//{0x002A, AFC_Default60Hz},
	//{0x0F12, 0x0001},  // #AFC_Default60Hz  1: Auto Flicker 60Hz start 0: Auto Flicker 50Hz start
	//{0x002A, REG_TC_DBG_AutoAlgEnBits},
	//{0x0F12, 0x007F},  // #REG_TC_DBG_AutoAlgEnBits  //default : 007F

	////2. Auto Flicker 50Hz Start
	//{0x002A, 0xAFC_Default60Hz},
	//{0x0F12, 0x0000},     //1: Auto Flicker 60Hz start 0: Auto Flicker 50Hz start
	//{0x002A, 0xREG_TC_DBG_AutoAlgEnBits},
	//{0x0F12, 0x007F},     //default : 007F

	//3. Manual Flicker 60Hz
	//{0x002A, 0xREG_TC_DBG_AutoAlgEnBits},  //default : 007F
	//{0x0F12, 0x005F},
	//{0x002A, 0xREG_SF_USER_FlickerQuant},  //Flicker 50Hz:0001/60Hz:0002/off:0000
	//{0x0F12, 0x0002},
	//{0x002A, 0xREG_SF_USER_FlickerQuantChanged}, //if change 0001 write
	//{0x0F12, 0x0001},

	//4. Manual Flicker 50Hz
	{0x002A, REG_TC_DBG_AutoAlgEnBits}, //default : 007F
	{0x0F12, 0x005F},
	{0x002A, REG_SF_USER_FlickerQuant},  //Flicker 50Hz:0001/60Hz:0002/off:0000
	{0x0F12, 0x0001},
	{0x002A, REG_SF_USER_FlickerQuantChanged}, //if change 0001 write
	{0x0F12, 0x0001},

	//5. Flicker Off
	//{0x002A, 0xREG_TC_DBG_AutoAlgEnBits},  //default : 007F
	//{0x0F12, 0x005F},
	//{0x002A, 0xREG_SF_USER_FlickerQuant},  //Flicker 50Hz:0001/60Hz:0002/off:0000
	//{0x0F12, 0x0000},
	//{0x002A, 0xREG_SF_USER_FlickerQuantChanged}, //if change 0001 write
	//{0x0F12, 0x0001},

	//{0x002A, 0xAFC_ManualQuant},
	//{0x0F12, 0x0001},


	{0x002A,lt_uMaxExp1},
	{0x0F12,0xC350},  	// #lt_uMaxExp1 	: 66.6ms		//30fps:  30fps-> 33.3ms->410A
	{0x0F12,0x0000},
	{0x002A,lt_uMaxExp2},
	{0x0F12,0x8488},  	// #lt_uMaxExp2 	: 250ms
	{0x0F12,0x0002},
	{0x002A,lt_uCapMaxExp1},
	{0x0F12,0xE848},  	// #lt_uCapMaxExp1	: 133.3ms = 0x1045a  //edison : 10fps = 100ms=0xc350
	{0x0F12,0x0001},
	{0x002A,lt_uCapMaxExp2},
	{0x0F12,0x8488},  	// #lt_uCapMaxExp2	: 250ms
	{0x0F12,0x0002},
	{0x002A,lt_uMaxAnGain1},
	{0x0F12,0x0280}, //0280  	// #lt_uMaxAnGain1	: X 2.51：：
	{0x002A,lt_uMaxAnGain2},
	{0x0F12,0x0600}, //0500  	// #lt_uMaxAnGain2	: X 6.01：：
	{0x002A,lt_uMaxDigGain},
	{0x0F12,0x0100}, //0200  	// #lt_uMaxDigGain
	//{0x0F12  0007  	// #ae_StatMode

	//clk Settings
	{0x002A,0x01b8},   // input=24MHz
	{0x0F12,0x5dc0},   // #REG_TC_IPRM_InClockLSBs
	{0x002A,0x01ba},
	{0x0F12,0x0000},
	{0x002A,0x01c6},   // #REG_TC_IPRM_UseNPviClocks 1 PLL configurations
	{0x0F12,0x0002},
	{0x002A,0x01cc},   // 1st system CLK
	{0x0F12,0x0bb8}, //1964   //#REG_TC_IPRM_sysClocks[0]
	{0x0F12,0x0bb8},   //#REG_TC_IPRM_MinOutRate4KHz_0
	{0x0F12,0x0bb8},   //#REG_TC_IPRM_MaxOutRate4KHz_0

	{0x0F12,0x1770},   //#REG_TC_IPRM_sysClocks[1]
	{0x0F12,0x1770}, //2ee0   //#REG_TC_IPRM_MinOutRate4KHz_1
	{0x0F12,0x1770}, //2ee0   //#REG_TC_IPRM_MaxOutRate4KHz_1
	{0x002A,0x01e0},   //#REG_TC_IPRM_InitParamsUpdated
	{0x0F12,0x0001},
	//p10


	//PREVIEW CONFIGURATION 0 (VGA, YUV, 6.5~12fps)
	{0x002A,0x0242},
	{0x0F12,0x0280}, //0500  	//Width
	{0x0F12,0x01e0}, //0400   //Height
	{0x0F12,0x0005},   //Format, 5:YUV422
	{0x0F12,0x1770},   //MaxOut4KHzRate, 1770 = 24M
	{0x0F12,0x1770},   //MinOut4KHzRate, 1770 = 24M
	{0x0F12,0x0052},   //PVIMask, default = 42
	{0x0F12,0x0001},   //ClockInd, 0-2
	{0x0F12,0x0000},   //FrTimeType, 0:Dynamic, 1:Fix for not accuarte, 2:Fix for accuarte
	{0x0F12,0x0000},   //FrRateQualityType, 1:Binning enable, 2:Binning disable
	{0x0F12,0x0600}, //0535 //04E2   //MaxFrTime, 14d = 33.3ms(333-6500) //7.5FPS
	{0x0F12,0x0320}, //03E8 //04E2   //MinFrTime, 14d = 33.3ms(333-6500) //15 FPS

	//=================================================================================================
	// Set Preview Config 1 --- For Night Mode     //  4~12fps
	//=================================================================================================
	{0x002A, REG_1TC_PCFG_usWidth},
	{0x0F12, 0x0280},   // #REG_0TC_PCFG_usWidth
	{0x002A, REG_1TC_PCFG_usHeight},
	{0x0F12, 0x01e0},   // #REG_0TC_PCFG_usHeight
	{0x002A, REG_1TC_PCFG_Format},
	{0x0F12, 0x0005},   // #REG_0TC_PCFG_Format  : YUV
	{0x002A, REG_1TC_PCFG_usMaxOut4KHzRate},
	{0x0F12, 0x1770},   // #REG_0TC_PCFG_usMaxOut4KHzRate
	{0x002A, REG_1TC_PCFG_usMinOut4KHzRate},
	{0x0F12, 0x1770},   // #REG_0TC_PCFG_usMinOut4KHzRate
	{0x002A, REG_1TC_PCFG_PVIMask},
	{0x0F12, 0x0052},   // #REG_0TC_PCFG_PVIMask (x2: PCLK rising  # x0: PCLK Falling)
	{0x002A, REG_1TC_PCFG_uClockInd},
	{0x0F12, 0x0001},    // #REG_0TC_PCFG_uClockInd
	{0x002A, REG_1TC_PCFG_usFrTimeType},
	{0x0F12, 0x0000},   // #REG_0TC_PCFG_usFrTimeType
	{0x002A, REG_1TC_PCFG_FrRateQualityType},
	{0x0F12, 0x0000},   // #REG_0TC_PCFG_FrRateQualityType
	{0x002A, REG_1TC_PCFG_usMaxFrTimeMsecMult10},
	{0x0F12, 0x0CE4},  //09C4 // 4FPS 029A  // #REG_0TC_PCFG_usMaxFrTimeMsecMult10 : 5fps
	{0x002A, REG_1TC_PCFG_usMinFrTimeMsecMult10},
	{0x0F12, 0x0320},  //  // #REG_0TC_PCFG_usMinFrTimeMsecMult10 : 10fps
	//WRITE 70000262 0003 // #REG_0TC_PCFG_uPrevMirror
	//WRITE 70000264 0003 // #REG_0TC_PCFG_uCaptureMirror
   /*lzg@rock-chips.com, FIH:image to be mirrored*/
#if  CONFIG_SENSOR_NONE_FLIP_MIRROR
	{0x002A, 0x0262},
	{0x0F12, 0x0000},  
	{0x002A, 0x0264},
	{0x0F12, 0x0000}, 
#elif CONFIG_SENSOR_MIRROR
	{0x002A, 0x0262},
	{0x0F12, 0x0001},  
	{0x002A, 0x0264},
	{0x0F12, 0x0001}, 
#elif CONFIG_SENSOR_FLIPE
	{0x002A, 0x0262},
	{0x0F12, 0x0002},  
	{0x002A, 0x0264},
	{0x0F12, 0x0002}, 
#elif CONFIG_SENSOR_NONE_FLIP_MIRROR
	{0x002A, 0x0262},
	{0x0F12, 0x0003},  
	{0x002A, 0x0264},
	{0x0F12, 0x0003}, 
#endif
   //=================================================================================================
	// Set Preview Config 2 --- For Video record(normal)     // 12fps
	//=================================================================================================
	{0x002A,  REG_2TC_PCFG_usWidth},
	{0x0F12,  0x0280},  // #REG_0TC_PCFG_usWidth
	{0x002A,  REG_2TC_PCFG_usHeight},
	{0x0F12,  0x01e0},  // #REG_0TC_PCFG_usHeight
	{0x002A,  REG_2TC_PCFG_Format},
	{0x0F12,  0x0005},  // #REG_0TC_PCFG_Format  : YUV
	{0x002A,  REG_2TC_PCFG_usMaxOut4KHzRate},
	{0x0F12,  0x0BB8}, //1770  // #REG_0TC_PCFG_usMaxOut4KHzRate
	{0x002A,  REG_2TC_PCFG_usMinOut4KHzRate},
	{0x0F12,  0x0BB8}, //1770  // #REG_0TC_PCFG_usMinOut4KHzRate
	{0x002A,  REG_2TC_PCFG_PVIMask},
	{0x0F12,  0x0052},  // #REG_0TC_PCFG_PVIMask (x2: PCLK rising  # x0: PCLK Falling)
	{0x002A,  REG_2TC_PCFG_uClockInd},
	{0x0F12,  0x0001},   // #REG_0TC_PCFG_uClockInd
	{0x002A,  REG_2TC_PCFG_usFrTimeType},
	{0x0F12,  0x0002}, //0  // #REG_0TC_PCFG_usFrTimeType
	{0x002A,  REG_2TC_PCFG_FrRateQualityType},
	{0x0F12,  0x0000},  // #REG_0TC_PCFG_FrRateQualityType
	{0x002A,  REG_2TC_PCFG_usMaxFrTimeMsecMult10},
	{0x0F12,  0x0320}, //07D0 //029A  // #REG_0TC_PCFG_usMaxFrTimeMsecMult10 : 5fps
	{0x002A,  REG_2TC_PCFG_usMinFrTimeMsecMult10},
	{0x0F12,  0x0000},//0320 //03E8 // #REG_0TC_PCFG_usMinFrTimeMsecMult10 : 10fps


	//=================================================================================================
	// Set Preview Config 3 --- For Video Record(Night Mode)    //  12fps
	//=================================================================================================
	{0x002A, REG_3TC_PCFG_usWidth},
	{0x0F12, 0x0280},  // #REG_0TC_PCFG_usWidth
	{0x002A, REG_3TC_PCFG_usHeight},
	{0x0F12, 0x01e0},  // #REG_0TC_PCFG_usHeight
	{0x002A, REG_3TC_PCFG_Format},
	{0x0F12, 0x0005},  // #REG_0TC_PCFG_Format  : YUV
	{0x002A, REG_3TC_PCFG_usMaxOut4KHzRate},
	{0x0F12, 0x0BB8}, //1770  // #REG_0TC_PCFG_usMaxOut4KHzRate
	{0x002A, REG_3TC_PCFG_usMinOut4KHzRate},
	{0x0F12, 0x0BB8}, //1770  // #REG_0TC_PCFG_usMinOut4KHzRate
	{0x002A, REG_3TC_PCFG_PVIMask},
	{0x0F12, 0x0052},  // #REG_0TC_PCFG_PVIMask (x2: PCLK rising  # x0: PCLK Falling)
	{0x002A, REG_3TC_PCFG_uClockInd},
	{0x0F12, 0x0001},   // #REG_0TC_PCFG_uClockInd
	{0x002A, REG_3TC_PCFG_usFrTimeType},
	{0x0F12, 0x0002}, //0  // #REG_0TC_PCFG_usFrTimeType
	{0x002A, REG_3TC_PCFG_FrRateQualityType},
	{0x0F12, 0x0000},  // #REG_0TC_PCFG_FrRateQualityType
	{0x002A, REG_3TC_PCFG_usMaxFrTimeMsecMult10},
	{0x0F12, 0x03e8}, //029A  // #REG_0TC_PCFG_usMaxFrTimeMsecMult10 : 5fps
	{0x002A, REG_3TC_PCFG_usMinFrTimeMsecMult10},
	{0x0F12, 0x0000},//0320 //03E8 // #REG_0TC_PCFG_usMinFrTimeMsecMult10 : 10fps

	//Preview
	{0x002A,0x021c},   //REG_TC_GP_ActivePrevConfig
	{0x0F12,0x0000},
	{0x002A,0x0220},   //REG_TC_GP_PrevOpenAfterChange
	{0x0F12,0x0001},
	{0x002A,0x01f8},   //REG_TC_GP_NewConfigSync
	{0x0F12,0x0001},
	{0x002A,0x021e},   //REG_TC_GP_PrevConfigChanged
	{0x0F12,0x0001},
	{0x002A,0x01f0},   //REG_TC_GP_EnablePreview
	{0x0F12,0x0001},
	{0x002A,0x01f2},   //REG_TC_GP_EnablePreviewChanged
	{0x0F12,0x0001},


	 //=================================================================================================
	 // Set Capture Config   // normal mode
	 //=================================================================================================
	 {0x002A,REG_0TC_CCFG_uCaptureMode},
	 {0x0F12,0x0000},  // #REG_0TC_CCFG_uCaptureMode : AE/AWB off when caputre
	 {0x002A,REG_0TC_CCFG_usWidth},
	 {0x0F12,0x0500},
	 {0x002A,REG_0TC_CCFG_usHeight},
	 {0x0F12,0x0400},
	 {0x002A,REG_0TC_CCFG_Format},
	 {0x0F12,0x0005},  // #REG_0TC_CCFG_Format  : YUV
	 {0x002A,REG_0TC_CCFG_usMaxOut4KHzRate},
	 {0x0F12,0x0bb8}, //05DC //0BC8 //1964 //0BB8  // #REG_0TC_CCFG_usMaxOut4KHzRate
	 {0x002A,REG_0TC_CCFG_usMinOut4KHzRate},
	 {0x0F12,0x0bb8}, //05DC //0BA8 //1964 //0BB8   // #REG_0TC_CCFG_usMinOut4KHzRate
	 {0x002A,REG_0TC_CCFG_PVIMask},
	 {0x0F12,0x0052},  // #REG_0TC_CCFG_PVIMask (x2: PCLK rising  # x0: PCLK Falling)
	 {0x002A,REG_0TC_CCFG_uClockInd},
	 {0x0F12,0x0000},  // #REG_0TC_CCFG_uClockInd
	 {0x002A,REG_0TC_CCFG_usFrTimeType},
	 {0x0F12,0x0002}, //0  // #REG_0TC_CCFG_usFrTimeType
	 {0x002A,REG_0TC_CCFG_FrRateQualityType},
	 {0x0F12,0x0002},  // #REG_0TC_CCFG_FrRateQualityType
	 {0x002A,REG_0TC_CCFG_usMaxFrTimeMsecMult10},
	 {0x0F12,0x09C4}, //0x09C4  // #REG_0TC_CCFG_usMaxFrTimeMsecMult10 : 4fps
	 {0x002A,REG_0TC_CCFG_usMinFrTimeMsecMult10},
	 {0x0F12,0x0000}, //03e8  // #REG_0TC_CCFG_usMinFrTimeMsecMult10 :
	{SEQUENCE_END, 0x00}
};




static struct reginfo sensor_720p[]=
{
	{SEQUENCE_END, 0x00}
};
static struct reginfo sensor_1080p[]=
{
	{SEQUENCE_END, 0x00}
};
/* 2592X1944 QSXGA */
static struct reginfo sensor_qsxga[] =
{
	{SEQUENCE_END, 0x00}
};
/* 2048*1536 QXGA */
static struct reginfo sensor_qxga[] =
{
	{SEQUENCE_END, 0x00}
};

/* 1600X1200 UXGA */
static struct reginfo sensor_uxga[] =
{
	{SEQUENCE_END, 0x00}
};

/* 1280X1024 SXGA */
static struct reginfo sensor_sxga[] =
{
	{0x0028, 0x7000},
	//002A 03F8
	////0F12 0079 // #REG_TC_DBG_AutoAlgEnBits, default : 007F0028 7000
	//002A 0496
	//0F12 0300 //0220 //0330	// #lt_uMaxAnGain2
	{0x002A, REG_TC_GP_ActiveCapConfig},
	{0x0F12, 0x0000},
	{0x002A, REG_TC_GP_NewConfigSync},
	{0x0F12, 0x0001},
	{0x002A, REG_TC_GP_CapConfigChanged},
	{0x0F12, 0x0001},
	{0x002A, REG_TC_GP_EnableCapture},
	{0x0F12, 0x0001},
	{0x002A, REG_TC_GP_EnableCaptureChanged},
	{0x0F12, 0x0001},
	{SEQUENCE_END, 0x00}
};
/*  1024X768 XGA */
static struct reginfo sensor_xga[] =
{
	{SEQUENCE_END, 0x00}
};
/* 800X600 SVGA*/
static struct reginfo sensor_svga[] =
{
    {SEQUENCE_END, 0x0},
};

/* 640X480 VGA */
static struct reginfo sensor_vga[] =
{
	{0x0028, 0x7000},
	{0x002A, REG_TC_GP_ActivePrevConfig},
	{0x0F12, 0x0000},

	{0x002A, REG_TC_GP_PrevOpenAfterChange},
	{0x0F12, 0x0001},
	{0x002A, REG_TC_GP_NewConfigSync},
	{0x0F12, 0x0001},
	{0x002A, REG_TC_GP_PrevConfigChanged},
	{0x0F12, 0x0001},
    {SEQUENCE_END, 0x0},
};

/* 352X288 CIF */
static struct reginfo sensor_cif[] =
{
    {SEQUENCE_END, 0x0},
};

/* 320*240 QVGA */
static  struct reginfo sensor_qvga[] =
{
    {SEQUENCE_END, 0x0},
};

/* 176X144 QCIF*/
static struct reginfo sensor_qcif[] =
{
    {SEQUENCE_END, 0x0},
};
#endif
#if 0
/* 160X120 QQVGA*/
static struct reginfo s5k6aa_qqvga[] =
{

    {0x300E, 0x34},
    {0x3011, 0x01},
    {0x3012, 0x10},
    {0x302a, 0x02},
    {0x302b, 0xE6},
    {0x306f, 0x14},
    {0x3362, 0x90},

    {0x3070, 0x5d},
    {0x3072, 0x5d},
    {0x301c, 0x07},
    {0x301d, 0x07},

    {0x3020, 0x01},
    {0x3021, 0x18},
    {0x3022, 0x00},
    {0x3023, 0x06},
    {0x3024, 0x06},
    {0x3025, 0x58},
    {0x3026, 0x02},
    {0x3027, 0x61},
    {0x3088, 0x00},
    {0x3089, 0xa0},
    {0x308a, 0x00},
    {0x308b, 0x78},
    {0x3316, 0x64},
    {0x3317, 0x25},
    {0x3318, 0x80},
    {0x3319, 0x08},
    {0x331a, 0x0a},
    {0x331b, 0x07},
    {0x331c, 0x80},
    {0x331d, 0x38},
    {0x3100, 0x00},
    {0x3302, 0x11},

    {SEQUENCE_END, 0x0},
};



static  struct reginfo s5k6aa_Sharpness_auto[] =
{
    {SEQUENCE_END, 0x00},
};

static  struct reginfo s5k6aa_Sharpness1[] =
{
    {0x3306, 0x08},
    {0x3371, 0x00}
};

static  struct reginfo s5k6aa_Sharpness2[][3] =
{
    //Sharpness 2
    {0x3306, 0x08},
    {0x3371, 0x01},
};

static  struct reginfo s5k6aa_Sharpness3[] =
{
    //default
    {0x3306, 0x08},
    {0x332d, 0x02},
};
static  struct reginfo s5k6aa_Sharpness4[]=
{
    //Sharpness 4
    {0x3306, 0x08},
    {0x332d, 0x03},
};

static  struct reginfo s5k6aa_Sharpness5[] =
{
    //Sharpness 5
    {0x3306, 0x08},
    {0x332d, 0x04},
};
#endif

static  struct reginfo sensor_ClrFmt_YUYV[]=
{
    {SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_ClrFmt_UYVY[]=
{
    {SEQUENCE_END, 0x00}
};

#if CONFIG_SENSOR_WhiteBalance
static  struct reginfo sensor_WhiteB_Auto[]=
{
    {SEQUENCE_END, 0x00}
};
/* Cloudy Colour Temperature : 6500K - 8000K  */
static  struct reginfo sensor_WhiteB_Cloudy[]=
{
    {SEQUENCE_END, 0x00}
};
/* ClearDay Colour Temperature : 5000K - 6500K  */
static  struct reginfo sensor_WhiteB_ClearDay[]=
{
    {SEQUENCE_END, 0x00}
};
/* Office Colour Temperature : 3500K - 5000K  */
static  struct reginfo sensor_WhiteB_TungstenLamp1[]=
{
    {SEQUENCE_END, 0x00}

};
/* Home Colour Temperature : 2500K - 3500K  */
static  struct reginfo sensor_WhiteB_TungstenLamp2[]=
{
    {SEQUENCE_END, 0x00}
};
static struct reginfo *sensor_WhiteBalanceSeqe[] = {sensor_WhiteB_Auto, sensor_WhiteB_TungstenLamp1,sensor_WhiteB_TungstenLamp2,
    sensor_WhiteB_ClearDay, sensor_WhiteB_Cloudy,NULL,
};
#endif

#if CONFIG_SENSOR_Brightness
static  struct reginfo sensor_Brightness0[]=
{
    {SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Brightness1[]=
{
    {SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Brightness2[]=
{
    {SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Brightness3[]=
{
    {SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Brightness4[]=
{
    {SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Brightness5[]=
{
    {SEQUENCE_END, 0x00}
};
static struct reginfo *sensor_BrightnessSeqe[] = {sensor_Brightness0, sensor_Brightness1, sensor_Brightness2, sensor_Brightness3,
    sensor_Brightness4, sensor_Brightness5,NULL,
};

#endif

#if CONFIG_SENSOR_Effect
static  struct reginfo sensor_Effect_Normal[] =
{
    {SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Effect_WandB[] =
{
    {SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Effect_Sepia[] =
{
    {SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Effect_Negative[] =
{
    {SEQUENCE_END, 0x00}
};
static  struct reginfo sensor_Effect_Bluish[] =
{
    {SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Effect_Green[] =
{
    {SEQUENCE_END, 0x00}
};
static struct reginfo *sensor_EffectSeqe[] = {sensor_Effect_Normal, sensor_Effect_WandB, sensor_Effect_Negative,sensor_Effect_Sepia,
    sensor_Effect_Bluish, sensor_Effect_Green,NULL,
};
#endif
#if CONFIG_SENSOR_Exposure
static  struct reginfo sensor_Exposure0[]=
{
    {SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Exposure1[]=
{
    {SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Exposure2[]=
{
    {SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Exposure3[]=
{
    {SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Exposure4[]=
{
    {SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Exposure5[]=
{
    {SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Exposure6[]=
{
    {SEQUENCE_END, 0x00}
};

static struct reginfo *sensor_ExposureSeqe[] = {sensor_Exposure0, sensor_Exposure1, sensor_Exposure2, sensor_Exposure3,
    sensor_Exposure4, sensor_Exposure5,sensor_Exposure6,NULL,
};
#endif
#if CONFIG_SENSOR_Saturation
static  struct reginfo sensor_Saturation0[]=
{
    {SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Saturation1[]=
{
    {SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Saturation2[]=
{
    {SEQUENCE_END, 0x00}
};
static struct reginfo *sensor_SaturationSeqe[] = {sensor_Saturation0, sensor_Saturation1, sensor_Saturation2, NULL,};

#endif
#if CONFIG_SENSOR_Contrast
static  struct reginfo sensor_Contrast0[]=
{
    {SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Contrast1[]=
{
    {SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Contrast2[]=
{
    {SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Contrast3[]=
{
    {SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Contrast4[]=
{
    {SEQUENCE_END, 0x00}
};


static  struct reginfo sensor_Contrast5[]=
{
    {SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Contrast6[]=
{
    {SEQUENCE_END, 0x00}
};
static struct reginfo *sensor_ContrastSeqe[] = {sensor_Contrast0, sensor_Contrast1, sensor_Contrast2, sensor_Contrast3,
    sensor_Contrast4, sensor_Contrast5, sensor_Contrast6, NULL,
};

#endif
#if CONFIG_SENSOR_Mirror
static  struct reginfo sensor_MirrorOn[]=
{
    {SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_MirrorOff[]=
{
    {SEQUENCE_END, 0x00}
};
static struct reginfo *sensor_MirrorSeqe[] = {sensor_MirrorOff, sensor_MirrorOn,NULL,};
#endif
#if CONFIG_SENSOR_Flip
static  struct reginfo sensor_FlipOn[]=
{
    {SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_FlipOff[]=
{
    {SEQUENCE_END, 0x00}
};
static struct reginfo *sensor_FlipSeqe[] = {sensor_FlipOff, sensor_FlipOn,NULL,};

#endif
#if CONFIG_SENSOR_Scene
static  struct reginfo sensor_SceneAuto[] =
{
    {SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_SceneNight[] =
{
    {SEQUENCE_END, 0x00}
};
static struct reginfo *sensor_SceneSeqe[] = {sensor_SceneAuto, sensor_SceneNight,NULL,};

#endif
#if CONFIG_SENSOR_DigitalZoom
static struct reginfo sensor_Zoom0[] =
{
    {SEQUENCE_END, 0x0},
};

static struct reginfo sensor_Zoom1[] =
{
     {SEQUENCE_END, 0x0},
};

static struct reginfo sensor_Zoom2[] =
{
    {SEQUENCE_END, 0x0},
};


static struct reginfo sensor_Zoom3[] =
{
    {SEQUENCE_END, 0x0},
};
static struct reginfo *sensor_ZoomSeqe[] = {sensor_Zoom0, sensor_Zoom1, sensor_Zoom2, sensor_Zoom3, NULL,};
#endif
static const struct v4l2_querymenu sensor_menus[] =
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

static  struct v4l2_queryctrl sensor_controls[] =
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
static int sensor_set_effect(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value);
static int sensor_set_whiteBalance(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value);
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
    {V4L2_MBUS_FMT_UYVY8_2X8, V4L2_COLORSPACE_JPEG}
};
enum sensor_work_state
{
	sensor_work_ready = 0,
	sensor_working,
};
struct sensor_work
{
	struct i2c_client *client;
	struct delayed_work dwork;
	enum sensor_work_state state;
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
	int auto_focus;
	int affm_reinit;
    int flash;
    int exposure;
    unsigned char mirror;                                        /* HFLIP */
    unsigned char flip;                                          /* VFLIP */
	bool snap2preview;
	bool video2preview;
	int capture_w;
	int capture_h;
	int preview_w;
	int preview_h;
    struct reginfo *winseqe_cur_addr;
	struct sensor_datafmt fmt;
	unsigned int enable;
	unsigned int funmodule_state;
} sensor_info_priv_t;



struct sensor_parameter
{
	unsigned short int preview_maxlines;
	unsigned short int preview_exposure;
	unsigned short int preview_line_width;
	unsigned short int preview_gain;

	unsigned short int capture_framerate;
	unsigned short int preview_framerate;
};

struct sensor
{
    struct v4l2_subdev subdev;
    struct i2c_client *client;
    sensor_info_priv_t info_priv;
	struct sensor_parameter parameter;
	struct workqueue_struct *sensor_wq;
	struct sensor_work sensor_wk;
	struct mutex wq_lock;
    int model;	/* V4L2_IDENT_OV* codes from v4l2-chip-ident.h */
#if CONFIG_SENSOR_I2C_NOSCHED
	atomic_t tasklock_cnt;
#endif
	struct rk29camera_platform_data *sensor_io_request;
    struct rk29camera_gpio_res *sensor_gpio_res;
};

static struct sensor* to_sensor(const struct i2c_client *client)
{
    return container_of(i2c_get_clientdata(client), struct sensor, subdev);
}

static int sensor_task_lock(struct i2c_client *client, int lock)
{
#if CONFIG_SENSOR_I2C_NOSCHED
	int cnt = 3;
    struct sensor *sensor = to_sensor(client);

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
static int sensor_write(struct i2c_client *client, u16 reg, u16 val)
{
    int err=0,cnt;
    u8 buf[4];
    struct i2c_msg msg[1];

	switch (reg)
	{
		case SEQUENCE_WAIT_MS:
		{
			msleep(val);
			break;
		}

		case SEQUENCE_WAIT_US:
		{
			udelay(val);
			break;
		}

		case SEQUENCE_PROPERTY:
		{
			break;
		}
		default:
		{
		    buf[0] = reg >> 8;
		    buf[1] = reg & 0xFF;
		    buf[2] = val >> 8;
			buf[3] = val & 0xFF;

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
		}
	}
    return err;
}

/* sensor register read */
static int sensor_read(struct i2c_client *client, u16 reg, u16 *val)
{
	u8 buf[2];
	struct i2c_msg msg[2];
	int err = 0;

	buf[0] = (u8)reg >> 8;
	buf[1] = (u8)reg & 0xFF;

	msg[0].addr = client->addr;
	msg[0].flags = client->flags;
	msg[0].buf = buf;
	msg[0].len = sizeof(buf);
	msg[0].scl_rate = CONFIG_SENSOR_I2C_SPEED;       /* ddl@rock-chips.com : 100kHz */
	msg[0].read_type = 2;   /* fpga i2c:0==I2C_NO_STOP : direct use number not enum for don't want include spi_fpga.h */

	msg[1].addr = client->addr;
	msg[1].flags = client->flags|I2C_M_RD;
	msg[1].buf = buf;
	msg[1].len = sizeof(buf);
	msg[1].scl_rate = CONFIG_SENSOR_I2C_SPEED;                       /* ddl@rock-chips.com : 100kHz */
	msg[1].read_type = 2;                             /* fpga i2c:0==I2C_NO_STOP : direct use number not enum for don't want include spi_fpga.h */

	err = i2c_transfer(client->adapter, msg, 2);

	if(err >= 0) {
		*val = (buf[0] << 8)|(buf[1] & 0xFF);
		SENSOR_DG("%s read reg(0x%x val:0x%x) success\n",SENSOR_NAME_STRING(),reg,*val);
		return 0;
	} else {
		SENSOR_TR("%s read reg(0x%x) failed!",SENSOR_NAME_STRING(),reg);
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
    while (regarray[i].reg != SEQUENCE_END)
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
    while (regarray[i].reg != SEQUENCE_END)
    {
		sensor_read(client, regarray[i].reg, &valchk);
		if (valchk != regarray[i].val)
			SENSOR_TR("%s Reg:0x%x read(0x%x, 0x%x) error\n",SENSOR_NAME_STRING(), regarray[i].reg, regarray[i].val, valchk);

        i++;
    }
    return 0;
}
#endif
#if CONFIG_SENSOR_Focus

static int sensor_af_single(struct i2c_client *client)
{
	int ret = 0;


sensor_af_single_end:
	return ret;
}

static int sensor_af_const(struct i2c_client *client)
{
	int ret = 0;

sensor_af_const_end:
	return ret;
}

static int sensor_af_zoneupdate(struct i2c_client *client)
{
	int ret = 0;

sensor_af_zoneupdate_end:
	return ret;
}

static int sensor_af_init(struct i2c_client *client)
{
	int ret = 0;

	return ret;
}
#endif

static int sensor_ioctrl(struct soc_camera_device *icd,enum rk29sensor_power_cmd cmd, int on)
{
	struct soc_camera_link *icl = to_soc_camera_link(icd);
	struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
    struct sensor *sensor = to_sensor(client);
	int ret = 0;

	switch (cmd)
	{
		case Sensor_Reset:
		{
			if (sensor->sensor_io_request && sensor->sensor_io_request->sensor_ioctrl) {
				sensor->sensor_io_request->sensor_ioctrl(icd->pdev,Cam_Reset, on);
			}
			break;
		}
		case Sensor_PowerDown:
		{
			if (icl->powerdown) {
				ret = icl->powerdown(icd->pdev, on);
				if (ret == RK29_CAM_IO_SUCCESS) {
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
			if (sensor->sensor_io_request && sensor->sensor_io_request->sensor_ioctrl) {
				sensor->sensor_io_request->sensor_ioctrl(icd->pdev,Cam_Flash, on);
                if(on){
                    //flash off after 2 secs
            		hrtimer_cancel(&(flash_off_timer.timer));
            		hrtimer_start(&(flash_off_timer.timer),ktime_set(0, 800*1000*1000),HRTIMER_MODE_REL);
                    }
			}
			break;
		}
		default:
		{
			SENSOR_TR("%s cmd(0x%x) is unknown!",SENSOR_NAME_STRING(),cmd);
			break;
		}
	}

sensor_power_end:
	return ret;
}

static enum hrtimer_restart flash_off_func(struct hrtimer *timer){
	struct flash_timer *fps_timer = container_of(timer, struct flash_timer, timer);
    sensor_ioctrl(fps_timer->icd,Sensor_Flash,0);
	SENSOR_DG("%s %s !!!!!!",SENSOR_NAME_STRING(),__FUNCTION__);
    return 0;
    
}

static int sensor_init(struct v4l2_subdev *sd, u32 val)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct soc_camera_device *icd = client->dev.platform_data;
    struct sensor *sensor = to_sensor(client);
	const struct v4l2_queryctrl *qctrl;
    const struct sensor_datafmt *fmt;
    int ret;
    u16 pid = 0;

    SENSOR_DG("\n%s..%s.. \n",SENSOR_NAME_STRING(),__FUNCTION__);

	sensor_ioctrl(icd,Sensor_Reset, 1);
	if (sensor_ioctrl(icd, Sensor_PowerDown, 0) < 0) {
		ret = -ENODEV;
		goto sensor_INIT_ERR;
	}

	udelay(50);
	sensor_ioctrl(icd,Sensor_Reset, 0);
	mdelay(1);
    /* soft reset */
	if (sensor_task_lock(client,1)<0)
		goto sensor_INIT_ERR;

#if (SENSOR_RESET_REG != SEQUENCE_END)
    ret = sensor_write(client, SENSOR_RESET_REG, SENSOR_RESET_VAL);
    if (ret != 0) {
        SENSOR_TR("%s soft reset sensor failed\n",SENSOR_NAME_STRING());
        ret = -ENODEV;
		goto sensor_INIT_ERR;
    }

    mdelay(5);  //delay 5 microseconds
#endif

	/* check if it is an sensor sensor */
#if (SENSOR_ID_REG != SEQUENCE_END)
    ret = sensor_read(client, SENSOR_ID_REG, &pid);
    if (ret != 0) {
        SENSOR_TR("read chip id failed\n");
        ret = -ENODEV;
        goto sensor_INIT_ERR;
    }

    SENSOR_DG("\n %s  pid = 0x%x \n", SENSOR_NAME_STRING(), pid);
#else
	pid = SENSOR_ID;
#endif
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
    sensor->info_priv.winseqe_cur_addr  = SENSOR_INIT_WINSEQADR;
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
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_FOCUS_ABSOLUTE);
	if (qctrl)
        sensor->info_priv.focus = qctrl->default_value;

	#if CONFIG_SENSOR_Flash
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_FLASH);
	if (qctrl)
        sensor->info_priv.flash = qctrl->default_value;

	hrtimer_init(&(flash_off_timer.timer), CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    flash_off_timer.icd = icd;
	flash_off_timer.timer.function = flash_off_func;

    #endif
    SENSOR_DG("\n%s..%s.. icd->width = %d.. icd->height %d\n",SENSOR_NAME_STRING(),((val == 0)?__FUNCTION__:"sensor_reinit"),icd->user_width,icd->user_height);
    sensor->info_priv.funmodule_state |= SENSOR_INIT_IS_OK;
    return 0;
sensor_INIT_ERR:
    sensor->info_priv.funmodule_state |= SENSOR_INIT_IS_OK;
	sensor_task_lock(client,0);
	sensor_deactivate(client);
    return ret;
}

static int sensor_deactivate(struct i2c_client *client)
{
	struct soc_camera_device *icd = client->dev.platform_data;
    struct sensor *sensor = to_sensor(client);

	SENSOR_DG("\n%s..%s.. Enter\n",SENSOR_NAME_STRING(),__FUNCTION__);

	/* ddl@rock-chips.com : all sensor output pin must change to input for other sensor */
	//sensor_task_lock(client, 1);
	sensor_ioctrl(icd, Sensor_PowerDown, 1);
    msleep(100); 

	/* ddl@rock-chips.com : sensor config init width , because next open sensor quickly(soc_camera_open -> Try to configure with default parameters) */
	icd->user_width = SENSOR_INIT_WIDTH;
    icd->user_height = SENSOR_INIT_HEIGHT;
    sensor->info_priv.funmodule_state &= ~SENSOR_INIT_IS_OK;

	return 0;
}

static  struct reginfo sensor_suspend_sequence[]=
{
	{0x0028, 0x7000},
	{0x002A, setot_usForceClocksSettings},
	{0x0F12, 0x001E},
	{0x002A, setot_usConfigClocksSettings},
	{0x0F12, 0x001E},
	{0x002A, REG_TC_GP_EnablePreview},
	{0x0F12, 0x0000},
	{0x002A, REG_TC_GP_EnablePreviewChanged},
	{0x0F12, 0x0001},
	{SEQUENCE_WAIT_MS, 100},
	{0x0028, 0xD000},
	{0x002A, 0xB0B0},
	{0x0F12, 0x0001},
	{SEQUENCE_WAIT_MS, 100},
	{0x002A, 0xB0B4},
	{0x0F12, 0x0000},
	{0x002A, 0xB0A0},
	{0x0F12, 0x0031},
	{0x002A, 0xB0A0},
	{0x0F12, 0x0010},
	{0x002A, 0xB0A0},
	{0x0F12, 0x0000},
	{0x0028, 0x7000},
	{0x002A, REG_TC_GP_SleepMode},
	{0x0F12, 0x0001},
	{0x002A, REG_TC_GP_SleepModeChanged},
	{0x0F12, 0x0001},

    {SEQUENCE_END,0x00}
};

static struct reginfo sensor_resume_sequence[]=
{
	{0x0028, 0x7000},
	{0x002A, REG_TC_GP_SleepMode},
	{0x0F12, 0x0000},
	{0x002A, REG_TC_GP_SleepModeChanged},
	{0x0F12, 0x0001},
	{0x0028, 0xD000},
	{0x002A, 0x1000},
	{0x0F12, 0x0001},
	{0x002A, 0x003A},
	{0x0F12, 0x0000},
	{SEQUENCE_WAIT_MS, 200},
	{0x002A, 0xB0B0},
	{0x0F12, 0x0000},
	{0x002A, 0xB0A0},
	{0x0F12, 0x003B},
	{0x002A, 0xB0B4},
	{0x0F12, 0x0800},
	{SEQUENCE_WAIT_MS, 10},
	{0x0028, 0x7000},
	{0x002A, REG_TC_GP_EnablePreview},
	{0x0F12, 0x0001},
	{0x002A, REG_TC_GP_EnablePreviewChanged},
	{0x0F12, 0x0001},

    {SEQUENCE_END,0x00}
};
static int sensor_suspend(struct soc_camera_device *icd, pm_message_t pm_msg)
{
    int ret;
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if (pm_msg.event == PM_EVENT_SUSPEND) {
        SENSOR_DG("\n %s Enter Suspend.. \n", SENSOR_NAME_STRING());
        ret = sensor_write_array(client, sensor_suspend_sequence) ;
        if (ret != 0) {
            SENSOR_TR("\n %s..%s WriteReg Fail.. \n", SENSOR_NAME_STRING(),__FUNCTION__);
            return ret;
        } else {
        	mdelay(100);
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
	struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    ret = sensor_ioctrl(icd, Sensor_PowerDown, 0);
    if (ret < 0) {
		SENSOR_TR("\n %s resume fail for turn on power!\n", SENSOR_NAME_STRING());
        return -EINVAL;
    } else {
        udelay(5);
		ret = sensor_write_array(client, sensor_resume_sequence) ;
	    if (ret != 0) {
	        SENSOR_TR("\n %s..%s WriteReg Fail.. \n", SENSOR_NAME_STRING(),__FUNCTION__);
			return -EINVAL;
	    }
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
    struct sensor *sensor = to_sensor(client);

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
static struct reginfo* sensor_fmt_catch(int set_w, int set_h, int *ret_w, int *ret_h)
{
	struct reginfo *winseqe_set_addr = NULL;

	if (((set_w <= 176) && (set_h <= 144)) && (sensor_qcif[0].reg!=SEQUENCE_END)) {
		winseqe_set_addr = sensor_qcif;
        *ret_w = 176;
        *ret_h = 144;
	} else if (((set_w <= 320) && (set_h <= 240)) && (sensor_qvga[0].reg!=SEQUENCE_END)) {
        winseqe_set_addr = sensor_qvga;
        *ret_w = 320;
        *ret_h = 240;
	} else if (((set_w <= 352) && (set_h<= 288)) && (sensor_cif[0].reg!=SEQUENCE_END)) {
        winseqe_set_addr = sensor_cif;
        *ret_w = 352;
        *ret_h = 288;
    } else if (((set_w <= 640) && (set_h <= 480)) && (sensor_vga[0].reg!=SEQUENCE_END)) {
        winseqe_set_addr = sensor_vga;
        *ret_w = 640;
        *ret_h = 480;
    }  
#if CONFIG_SENSOR_FOR_CTS
   /**lzg@rockchip.com: forbid to preview with resolution 1280*1024*/
    else if (((set_w <= 800) && (set_h <= 600)) && (sensor_vga[0].reg!=SEQUENCE_END)) {
            winseqe_set_addr = sensor_vga;
            *ret_w = 640;
            *ret_h = 480;
        } 
#else
    else if (((set_w <= 800) && (set_h <= 600)) && (sensor_svga[0].reg!=SEQUENCE_END)) {
        winseqe_set_addr = sensor_svga;
        *ret_w = 800;
        *ret_h = 600;       
    } 
#endif
    else if (((set_w <= 1024) && (set_h <= 768)) && (sensor_xga[0].reg!=SEQUENCE_END)) {
        winseqe_set_addr = sensor_xga;
        *ret_w = 1024;
        *ret_h = 768;
	} else if (((set_w <= 1280) && (set_h <= 720)) && (sensor_720p[0].reg!=SEQUENCE_END)) {
        winseqe_set_addr = sensor_720p;
        *ret_w = 1280;
        *ret_h = 720;
    } else if (((set_w <= 1280) && (set_h <= 1024)) && (sensor_sxga[0].reg!=SEQUENCE_END)) {
        winseqe_set_addr = sensor_sxga;
        *ret_w = 1280;
        *ret_h = 1024;
    } else if (((set_w <= 1600) && (set_h <= 1200)) && (sensor_uxga[0].reg!=SEQUENCE_END)) {
        winseqe_set_addr = sensor_uxga;
        *ret_w = 1600;
        *ret_h = 1200;
	} 
#if CONFIG_SENSOR_FOR_CTS
   /**lzg@rockchip.com: forbid to preview with resolution 1280*1024*/
    else if (((set_w <= 1920) && (set_h <= 1080)) && (sensor_vga[0].reg!=SEQUENCE_END)) {
        winseqe_set_addr = sensor_vga;
        *ret_w = 640;
        *ret_h = 480;
    }     
#else
    else if (((set_w <= 1920) && (set_h <= 1080)) && (sensor_1080p[0].reg!=SEQUENCE_END)) {
        winseqe_set_addr = sensor_1080p;
        *ret_w = 1920;
        *ret_h = 1080;
    } 
#endif
    else if (((set_w <= 2048) && (set_h <= 1536)) && (sensor_qxga[0].reg!=SEQUENCE_END)) {
        winseqe_set_addr = sensor_qxga;
        *ret_w = 2048;
        *ret_h = 1536;
    } else if (((set_w <= 2592) && (set_h <= 1944)) && (sensor_qsxga[0].reg!=SEQUENCE_END)) {
        winseqe_set_addr = sensor_qsxga;
        *ret_w = 2592;
        *ret_h = 1944;
    }

	return winseqe_set_addr;
}

static int sensor_s_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    const struct sensor_datafmt *fmt;
    struct sensor *sensor = to_sensor(client);    
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

	winseqe_set_addr = sensor_fmt_catch(set_w, set_h, &set_w, &set_h);

    if ((winseqe_set_addr  != sensor->info_priv.winseqe_cur_addr) && winseqe_set_addr) {
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

        sensor->info_priv.winseqe_cur_addr  = (int)winseqe_set_addr;


        SENSOR_DG("\n%s..%s.. icd->width = %d.. icd->height %d\n",SENSOR_NAME_STRING(),__FUNCTION__,set_w,set_h);
    } else {
        SENSOR_TR("\n %s .. Current Format is validate. icd->width = %d.. icd->height %d\n",SENSOR_NAME_STRING(),set_w,set_h);
    }

	mf->width = set_w;
    mf->height = set_h;

sensor_s_fmt_end:
    return ret;
}

static int sensor_try_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct sensor *sensor = to_sensor(client);
    const struct sensor_datafmt *fmt;
    int ret = 0;
   
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

    id->ident = SENSOR_V4L2_IDENT;      /* ddl@rock-chips.com :  Return s5k6aa  identifier */
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
    struct sensor *sensor = to_sensor(client);
	const struct v4l2_queryctrl *qctrl_info;
    int digitalzoom_cur, digitalzoom_total;

	qctrl_info = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_ZOOM_ABSOLUTE);
	if (qctrl_info)
		return -EINVAL;

    digitalzoom_cur = sensor->info_priv.digitalzoom;
    digitalzoom_total = qctrl_info->maximum;

    if ((*value > 0) && (digitalzoom_cur >= digitalzoom_total))
    {
        SENSOR_TR("%s digitalzoom is maximum - %x\n", SENSOR_NAME_STRING(), digitalzoom_cur);
        return -EINVAL;
    }

    if  ((*value < 0) && (digitalzoom_cur <= qctrl_info->minimum))
    {
        SENSOR_TR("%s digitalzoom is minimum - %x\n", SENSOR_NAME_STRING(), digitalzoom_cur);
        return -EINVAL;
    }

    if ((*value > 0) && ((digitalzoom_cur + *value) > digitalzoom_total))
    {
        *value = digitalzoom_total - digitalzoom_cur;
    }

    if ((*value < 0) && ((digitalzoom_cur + *value) < 0))
    {
        *value = 0 - digitalzoom_cur;
    }

    digitalzoom_cur += *value;

    if (sensor_ZoomSeqe[digitalzoom_cur] != NULL)
    {
        if (sensor_write_array(client, sensor_ZoomSeqe[digitalzoom_cur]) != 0)
        {
            SENSOR_TR("%s..%s WriteReg Fail.. \n",SENSOR_NAME_STRING(), __FUNCTION__);
            return -EINVAL;
        }
        SENSOR_DG("%s..%s : %x\n",SENSOR_NAME_STRING(),__FUNCTION__, *value);
        return 0;
    }

    return -EINVAL;
}
#endif
#if CONFIG_SENSOR_Flash
static int sensor_set_flash(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
    struct sensor *sensor = to_sensor(client);
	const struct v4l2_queryctrl *qctrl_info;
    
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
    struct sensor *sensor = to_sensor(client);
    const struct v4l2_queryctrl *qctrl;

    qctrl = soc_camera_find_qctrl(&sensor_ops, ctrl->id);

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
    struct sensor *sensor = to_sensor(client);
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
    struct sensor *sensor = to_sensor(client);

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
    struct sensor *sensor = to_sensor(client);
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
static int sensor_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct sensor *sensor = to_sensor(client);

	if (enable == 1) {
		sensor->info_priv.enable = 1;
	} else if (enable == 0) {
		sensor->info_priv.enable = 0;
	}

	return 0;
}
/* Interface active, can use i2c. If it fails, it can indeed mean, that
 * this wasn't our capture interface, so, we wait for the right one */
static int sensor_video_probe(struct soc_camera_device *icd,
			       struct i2c_client *client)
{
    int ret,pid = 0;
    struct sensor *sensor = to_sensor(client);

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
#if (SENSOR_RESET_REG != SEQUENCE_END)
    ret = sensor_write(client, SENSOR_RESET_REG, SENSOR_RESET_VAL);
    if (ret != 0) {
        SENSOR_TR("%s soft reset sensor failed\n",SENSOR_NAME_STRING());
        ret = -ENODEV;
		goto sensor_video_probe_err;
    }

    mdelay(5);  //delay 5 microseconds
#endif

	/* check if it is an sensor sensor */
#if (SENSOR_ID_REG != SEQUENCE_END)
    ret = sensor_read(client, SENSOR_ID_REG, &pid);
    if (ret != 0) {
        SENSOR_TR("read chip id failed\n");
        ret = -ENODEV;
        goto sensor_video_probe_err;
    }

    SENSOR_DG("\n %s  pid = 0x%x \n", SENSOR_NAME_STRING(), pid);
#else
	pid = SENSOR_ID;
#endif

    if (pid == SENSOR_ID) {
        sensor->model = SENSOR_V4L2_IDENT;
    } else {
        SENSOR_TR("error: %s mismatched   pid = 0x%x\n", SENSOR_NAME_STRING(), pid);
        ret = -ENODEV;
        goto sensor_video_probe_err;
    }

    return 0;

sensor_video_probe_err:

    return ret;
}
static long sensor_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct soc_camera_device *icd = client->dev.platform_data;
    struct sensor *sensor = to_sensor(client);
    int ret = 0;
#if CONFIG_SENSOR_Flash	
    int i;
#endif

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
                			//memset((char*)&icd->ops->controls[i],0x00,sizeof(struct v4l2_queryctrl));  
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
    .s_stream   = sensor_s_stream,	
};
static struct v4l2_subdev_ops sensor_subdev_ops = {
	.core	= &sensor_subdev_core_ops,
	.video = &sensor_subdev_video_ops,
};

static int sensor_probe(struct i2c_client *client,
			 const struct i2c_device_id *did)
{
    struct sensor *sensor;
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

    sensor = kzalloc(sizeof(struct sensor), GFP_KERNEL);
    if (!sensor)
        return -ENOMEM;

    v4l2_i2c_subdev_init(&sensor->subdev, client, &sensor_subdev_ops);

    /* Second stage probe - when a capture adapter is there */
    icd->ops		= &sensor_ops;
    sensor->info_priv.fmt = sensor_colour_fmts[0];

    ret = sensor_video_probe(icd, client);
    if (ret) {
        icd->ops = NULL;
        i2c_set_clientdata(client, NULL);
        kfree(sensor);
    }
    SENSOR_DG("\n%s..%s..%d  ret = %x \n",__FUNCTION__,__FILE__,__LINE__,ret);
    return ret;
}

static int sensor_remove(struct i2c_client *client)
{
    struct sensor *sensor = to_sensor(client);
    struct soc_camera_device *icd = client->dev.platform_data;

    icd->ops = NULL;
    i2c_set_clientdata(client, NULL);
    client->driver = NULL;
    kfree(sensor);

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

