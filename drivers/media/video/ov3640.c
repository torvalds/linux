/*
 * Driver for MT9M001 CMOS Image Sensor from Micron
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
#include <mach/rk29_camera.h>
#include "ov3640.h"

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
#define SENSOR_NAME RK29_CAM_SENSOR_OV3640
#define SENSOR_V4L2_IDENT V4L2_IDENT_OV3640
#define SENSOR_ID 0x364c
#define SENSOR_MIN_WIDTH    176
#define SENSOR_MIN_HEIGHT   144
#define SENSOR_MAX_WIDTH    2048
#define SENSOR_MAX_HEIGHT   1536
#define SENSOR_INIT_WIDTH	640			/* Sensor pixel size for sensor_init_data array */
#define SENSOR_INIT_HEIGHT  480
#define SENSOR_INIT_WINSEQADR sensor_init_data
#define SENSOR_INIT_PIXFMT V4L2_MBUS_FMT_YUYV8_2X8

#define CONFIG_SENSOR_WhiteBalance	1
#define CONFIG_SENSOR_Brightness	0
#define CONFIG_SENSOR_Contrast      0
#define CONFIG_SENSOR_Saturation    0
#define CONFIG_SENSOR_Effect        1
#define CONFIG_SENSOR_Scene         1
#define CONFIG_SENSOR_DigitalZoom   0
#define CONFIG_SENSOR_Exposure      0
#define CONFIG_SENSOR_Flash         0
#define CONFIG_SENSOR_Mirror        0
#define CONFIG_SENSOR_Flip          0
#ifdef CONFIG_OV3640_AUTOFOCUS
#define CONFIG_SENSOR_Focus         1
#include "ov3640_af_firmware.c"
#else
#define CONFIG_SENSOR_Focus         0
#endif

#define CONFIG_SENSOR_I2C_SPEED     100000       /* Hz */
/* Sensor write register continues by preempt_disable/preempt_enable for current process not be scheduled */
#define CONFIG_SENSOR_I2C_NOSCHED   0
#define CONFIG_SENSOR_I2C_RDWRCHK   0


#define SENSOR_BUS_PARAM  (SOCAM_MASTER | SOCAM_PCLK_SAMPLE_RISING |\
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

#if CONFIG_SENSOR_Focus
#define SENSOR_AF_MODE_INFINITY    0
#define SENSOR_AF_MODE_MACRO       1
#define SENSOR_AF_MODE_FIXED       2
#define SENSOR_AF_MODE_AUTO        3
#define SENSOR_AF_MODE_CONTINUOUS  4
#define SENSOR_AF_MODE_CLOSE       5

#endif

#define VCM_FIRMWARE				1

#define VCM_STARTADDR				0x8000

#define S_IDLE						0x00
#if (VCM_FIRMWARE==1)
#define S_FOCUSING					0x01
#define S_FOCUSED					0x02
#define S_CAPTURE					0x12
#define STA_FOCUS					0x3f07
#elif (VCM_FIRMWARE==2)
#define S_FOCUSING					0x01
#define S_FOCUSED					0x02
#define S_CAPTURE					0x12
#define STA_FOCUS					0x3f01
#else
#define S_FOCUSING					0x65
#define S_FOCUSED					0x46
#define S_CAPTURE					0x47
#define STA_FOCUS					0x3f01
#endif


#if CONFIG_SENSOR_Focus
/* ov3640 VCM Command and Status Registers */
#define CMD_MAIN_Reg      0x3F00
#define CMD_TAG_Reg       0x3F01
#define CMD_PARA0_Reg     0x3F05
#define CMD_PARA1_Reg     0x3F04
#define CMD_PARA2_Reg     0x3F03
#define CMD_PARA3_Reg     0x3F02
#define STA_ZONE_Reg      0x3F06
#define STA_FOCUS_Reg     0x3F07

/* ov3640 VCM Command  */
#define OverlayEn_Cmd     0x01
#define OverlayDis_Cmd    0x02
#define SingleFocus_Cmd   0x03
#define ConstFocus_Cmd    0x04
#define StepMode_Cmd      0x05
#define PauseFocus_Cmd    0x06
#define ReturnIdle_Cmd    0x08
#define SetZone_Cmd       0x10
#define UpdateZone_Cmd    0x12
#define SetMotor_Cmd      0x20

/* ov3640 Focus State */
#define S_FIRWRE          0xFF
#define S_STARTUP         0xFA
#define S_ERROR           0xFE
#define S_DRVICERR        0xEE
#define S_IDLE            0x00
#define S_FOCUSING        0x01
#define S_FOCUSED         0x02
#define S_CAPTURE         0x12
#define S_STEP            0x20

/* ovxxxx Zone State */
#define Zone_Is_Focused(a, zone_val)    (zone_val&(1<<(a-3)))
#define Zone_Get_ID(zone_val)           (zone_val&0x03)

#define Zone_CenterMode   0x01
#define Zone_5xMode       0x02
#define Zone_5PlusMode    0x03
#define Zone_4fMode       0x04

#define ZoneSel_Auto      0x0b
#define ZoneSel_SemiAuto  0x0c
#define ZoneSel_Manual    0x0d
#define ZoneSel_Rotate    0x0e

/* ovxxxx Step Focus Commands */
#define StepFocus_Near_Tag       0x01
#define StepFocus_Far_Tag        0x02
#define StepFocus_Furthest_Tag   0x03
#define StepFocus_Nearest_Tag    0x04
#define StepFocus_Spec_Tag       0x10
#endif

/* init VGA 640*480 */
static struct reginfo sensor_init_data[] =
{
#if 1
	{0x3078, 0x02},
	{0x304d, 0x45},
	{0x30a7, 0x5e},
	{0x3087, 0x16},
	{0x309C, 0x1a},
	{0x30a2, 0xe4},
	{0x30aa, 0x42},
	{0x30b0, 0xff},
	{0x30b1, 0xff},
	//{0x30b2, 0x10},
	{0x30b2, 0x18}, // by FAE
	{0x300e, 0x39},
	{0x300f, 0x21},
	{0x3010, 0x20},
	{0x304c, 0x81},
	{0x30d7, 0x10},
	{0x30d9, 0x0d},
	{0x30db, 0x08},
	{0x3016, 0x82},
	{0x3018, 0x48},
	{0x3019, 0x40},
	{0x301a, 0x82},

       {0x30a9, 0xbd},//disable internal DVDD, by FAE.
       
	{0x307d, 0x00},
	{0x3087, 0x02},
	{0x3082, 0x20},
	{0x3015, 0x12},
	{0x3014, 0x84},
	{0x3013, 0xf7},
	{0x303c, 0x08},
	{0x303d, 0x18},
	{0x303e, 0x06},
	{0x303F, 0x0c},
	{0x3030, 0x62},
	{0x3031, 0x26},
	{0x3032, 0xe6},
	{0x3033, 0x6e},
	{0x3034, 0xea},
	{0x3035, 0xae},
	{0x3036, 0xa6},
	{0x3037, 0x6a},
	{0x3104, 0x02},
	{0x3105, 0xfd},
	{0x3106, 0x00},
	{0x3107, 0xff},
	{0x3300, 0x13},
	{0x3301, 0xde},
	{0x3302, 0xcf},
	{0x3312, 0x26},
	{0x3314, 0x42},
	{0x3313, 0x2b},
	{0x3315, 0x42},
	{0x3310, 0xd0},
	{0x3311, 0xbd},
	{0x330c, 0x18},
	{0x330d, 0x18},
	{0x330e, 0x56},
	{0x330f, 0x5c},
	{0x330b, 0x1c},
	{0x3306, 0x5c},
	{0x3307, 0x11},
	{0x336a, 0x52},
	{0x3370, 0x46},
	{0x3376, 0x38},
	{0x30b8, 0x20},
	{0x30b9, 0x17},
	{0x30ba, 0x00},
	{0x30bb, 0x08},
	{0x3507, 0x06},
	{0x350a, 0x4f},
	{0x3100, 0x02},
	{0x3301, 0xde},
	{0x3304, 0xfc},
	{0x3400, 0x00},
	{0x3404, 0x00},//YUV, sequence
	{0x3600, 0xc0},
	{0x3088, 0x08},
	{0x3089, 0x00},
	{0x308a, 0x06},
	{0x308b, 0x00},
	{0x308d, 0x04},
	{0x3086, 0x03},
	{0x3086, 0x00},
	{0x30a9, 0xb5},
	{0x3317, 0x04},
	{0x3316, 0xf8},
	{0x3312, 0x31},
	{0x3314, 0x57},
	{0x3313, 0x28},
	{0x3315, 0x3d},
	{0x3311, 0xd0},
	{0x3310, 0xb6},
	{0x330c, 0x16},
	{0x330d, 0x16},
	{0x330e, 0x5f},
	{0x330f, 0x5c},
	{0x330b, 0x18},
	{0x3306, 0x5c},
	{0x3307, 0x11},
	{0x3308, 0x25},
	{0x3340, 0x20},
	{0x3341, 0x58},
	{0x3342, 0x08},
	{0x3343, 0x21},
	{0x3344, 0xbe},
	{0x3345, 0xe0},
	{0x3346, 0xca},
	{0x3347, 0xc6},
	{0x3348, 0x04},
	{0x3349, 0x98},
	{0x3355, 0x02},
	{0x3358, 0x44},
	{0x3359, 0x44},
	{0x3300, 0x13},
	{0x3367, 0x23},
	{0x3368, 0xBB},
	{0x3369, 0xD6},
	{0x336A, 0x2A},
	{0x336B, 0x07},
	{0x336C, 0x00},
	{0x336D, 0x23},
	{0x336E, 0xC3},
	{0x336F, 0xDE},
	{0x3370, 0x2b},
	{0x3371, 0x07},
	{0x3372, 0x00},
	{0x3373, 0x23},
	{0x3374, 0x9e},
	{0x3375, 0xD6},
	{0x3376, 0x29},
	{0x3377, 0x07},
	{0x3378, 0x00},
	{0x332a, 0x1d},
	{0x331b, 0x08},
	{0x331c, 0x16},
	{0x331d, 0x2d},
	{0x331e, 0x54},
	{0x331f, 0x66},
	{0x3320, 0x73},
	{0x3321, 0x80},
	{0x3322, 0x8c},
	{0x3323, 0x95},
	{0x3324, 0x9d},
	{0x3325, 0xac},
	{0x3326, 0xb8},
	{0x3327, 0xcc},
	{0x3328, 0xdd},
	{0x3329, 0xee},
	{0x332e, 0x04},
	{0x332f, 0x06},
	{0x3331, 0x03},
#ifdef CONFIG_RK29_NEWTON
        {0x307c, 0x10}, // flip && mirror ,for newton
        {0x3090, 0xc0},
#else

	//{0x307c, 0x13}, // flip && mirror
	{0x307c, 0x11}, // flip && mirror ,by FAE.
  	{0x3090, 0xc8},
#endif 

#else
//640 480 ;XGA-&gt;XGA;;
     {0x3012, 0x10},
     {0x3023, 0x06},
     {0x3026, 0x03},
     {0x3027, 0x04},
     {0x302a, 0x03},
     {0x302b, 0x10},
     {0x3075, 0x24},
     {0x300d, 0x01},
     {0x30d7, 0x90},
     {0x3069, 0x04},
     {0x303e, 0x00},
     {0x303f, 0xc0},
     {0x3302, 0xef},
     {0x335f, 0x34},
     {0x3360, 0x0c},
     {0x3361, 0x04},
     {0x3362, 0x34},
     {0x3363, 0x08},
     {0x3364, 0x04},
     {0x3403, 0x42},
     {0x3088, 0x04},
     {0x3089, 0x00},
     {0x308a, 0x03},
     {0x308b, 0x00},
     {0x300e, 0x32},
     {0x300f, 0x21},
     {0x3010, 0x20},
     {0x3011, 0x01},
     {0x304c, 0x82},

//;XGA-&gt;VGA
     {0x3302, 0xef},
     {0x335f, 0x34},
     {0x3360, 0x0c},
     {0x3361, 0x04},
     {0x3362, 0x12},
     {0x3363, 0x88},
     {0x3364, 0xe4},
     {0x3403, 0x42},
     {0x3088, 0x12},
     {0x3089, 0x80},
     {0x308a, 0x01},
     {0x308b, 0xe0},
     {0x304c, 0x85},
#endif
	{0x0000 ,0x00},
};

/* 2048X1536 QXGA */
static struct reginfo sensor_qxga_preview[] =
{
#if 1
	{0x3012, 0x00},
	{0x3366, 0x10},
	{0x3020, 0x01},
	{0x3021, 0x1d},
	{0x3022, 0x00},
	{0x3023, 0x0a},
	{0x3024, 0x08},
	{0x3025, 0x18},
	{0x3026, 0x06},
	{0x3027, 0x0c},
	{0x302a, 0x06},
	{0x302b, 0x20},
	{0x3075, 0x44},
	{0x300d, 0x00},
	{0x30d7, 0x10},
	{0x3069, 0x44},
	{0x303e, 0x01},
	{0x303f, 0x80},
		
	{0x3302, 0xcf},
	{0x335f, 0x68},
	{0x3360, 0x18},
	{0x3361, 0x0c},
	{0x3362, 0x68},
	{0x3363, 0x08},
	{0x3364, 0x04},
	{0x3366, 0x10},
	{0x3403, 0x42},
	{0x3088, 0x08},
	{0x3089, 0x00},
	{0x308a, 0x06},
	{0x308b, 0x00},
	{0x300e, 0x39},
	{0x300f, 0x21},
	{0x3010, 0x20},
	{0x3011, 0x01},
	{0x304c, 0x81},

#else
//[Sensor.YUV.2048x1536] XGA-&gt;QXGA
  {0x3012, 0x00},
  {0x3020, 0x01},
  {0x3021, 0x1d},
  {0x3022, 0x00},
  {0x3023, 0x0a},
  {0x3024, 0x08},
  {0x3025, 0x18},
  {0x3026, 0x06},
  {0x3027, 0x0c},
  {0x302a, 0x06},
  {0x302b, 0x20},
  {0x3075, 0x44},
  {0x300d, 0x00},
  {0x30d7, 0x10},
  {0x3069, 0x44},
  {0x303e, 0x01},
  {0x303f, 0x80},
  {0x3302, 0xcf},
  {0x335f, 0x68},
  {0x3360, 0x18},
  {0x3361, 0x0c},
  {0x3362, 0x68},
  {0x3363, 0x08},
  {0x3364, 0x04},
  {0x3403, 0x42},
  {0x3088, 0x08},
  {0x3089, 0x00},
  {0x308a, 0x06},
  {0x308b, 0x00},
  {0x300e, 0x39},
  {0x300f, 0x21},
  {0x3010, 0x20},
  {0x3011, 0x01},
  {0x304c, 0x81},
#endif		
	{0x0000 ,0x00}
};
static struct reginfo sensor_qxga_capture[] = {
	{0x3012, 0x00},
	{0x3366, 0x10},
	{0x3020, 0x01},
	{0x3021, 0x1d},
	{0x3022, 0x00},
	{0x3023, 0x0a},
	{0x3024, 0x08},
	{0x3025, 0x18},
	{0x3026, 0x06},
	{0x3027, 0x0c},
	{0x302a, 0x06},
	{0x302b, 0x20},
	{0x3075, 0x44},	
	{0x300d, 0x00},
	{0x30d7, 0x10},
	{0x3069, 0x44},
	{0x303e, 0x01},
	{0x303f, 0x80},
		
	{0x3302, 0xcf},
	{0x335f, 0x68},
	{0x3360, 0x18},
	{0x3361, 0x0c},
	{0x3362, 0x68},
	{0x3363, 0x08},
	{0x3364, 0x04},
	{0x3366, 0x10},
	{0x3403, 0x42},
	{0x3088, 0x08},
	{0x3089, 0x00},
	{0x308a, 0x06},
	{0x308b, 0x00},
	{0x300e, 0x39},
	{0x300f, 0x21},
	{0x3010, 0x20},
	{0x3011, 0x01},
	{0x304c, 0x81},
	
		
	{0x0000 ,0x00}
};
static struct reginfo *sensor_qxga[2] = {
	sensor_qxga_preview,
	sensor_qxga_capture,	
};

/* 1600X1200 UXGA */
static struct reginfo sensor_uxga_preview[] =
{
	{0x3012, 0x00},
	{0x3366, 0x10},
	{0x3020, 0x01},
	{0x3021, 0x1d},
	{0x3022, 0x00},
	{0x3023, 0x0a},
	{0x3024, 0x08},
	{0x3025, 0x18},
	{0x3026, 0x06},
	{0x3027, 0x0c},
	{0x302a, 0x06},
	{0x302b, 0x20},
	{0x3075, 0x44},
	{0x300d, 0x00},
	{0x30d7, 0x10},
	{0x3069, 0x44},
	{0x303e, 0x01},
	{0x303f, 0x80},
	//qxga -> uxga
	{0x300e, 0x39},
	{0x300f, 0x21},
	{0x3010, 0x20},
	{0x3302, 0xef},
	{0x335f, 0x68},
	{0x3360, 0x18},
	{0x3361, 0x0C},
	{0x3362, 0x46},
	{0x3363, 0x48},
	{0x3364, 0xb4},
	{0x3403, 0x22},
	{0x3088, 0x06},
	{0x3089, 0x40},
	{0x308a, 0x04},
	{0x308b, 0xb0},
	{0x304c, 0x81},//56Mhz PCLK output
	
	{0x0000 ,0x00},
};
static struct reginfo sensor_uxga_capture[] =
{
	{0x3302, 0xef},
	{0x335f, 0x68},
	{0x3360, 0x18},
	{0x3361, 0x0C},
	{0x3362, 0x46},
	{0x3363, 0x48},
	{0x3364, 0xb4},
	{0x3403, 0x22},
	{0x3088, 0x06},
	{0x3089, 0x40},
	{0x308a, 0x04},
	{0x308b, 0xb0},
	{0x304c, 0x81},//56Mhz PCLK output

	{0x0000 ,0x00},
};
static struct reginfo *sensor_uxga[2] = {
	sensor_uxga_preview,
	sensor_uxga_capture,	
};

/* 1280X960 SXGA  */
static struct reginfo sensor_sxga_preview[] =
{
	{0x3012, 0x00},
	{0x3366, 0x10},
	{0x3020, 0x01},
	{0x3021, 0x1d},
	{0x3022, 0x00},
	{0x3023, 0x0a},
	{0x3024, 0x08},
	{0x3025, 0x18},
	{0x3026, 0x06},
	{0x3027, 0x0c},
	{0x302a, 0x06},
	{0x302b, 0x20},
	{0x3075, 0x44},
	{0x300d, 0x00},
	{0x30d7, 0x10},
	{0x3069, 0x44},
	{0x303e, 0x01},
	{0x303f, 0x80},
	//qxga -> sxga
	{0x300e, 0x39},
	{0x300f, 0x21},
	{0x3010, 0x20},
	{0x3302, 0xef},
	{0x335f, 0x68},
	{0x3360, 0x18},
	{0x3361, 0x0c},
	{0x3362, 0x35},
	{0x3363, 0x08},
	{0x3364, 0xc4},
	{0x3403, 0x42},
	{0x3088, 0x05},
	{0x3089, 0x00},
	{0x308a, 0x03},
	{0x308b, 0xc0},
	{0x304c, 0x81},//56Mhz PCLK output
	
	{0x0000 ,0x00},
};
static struct reginfo sensor_sxga_capture[] =
{
	{0x3302, 0xef},
	{0x335f, 0x68},
	{0x3360, 0x18},
	{0x3361, 0x0c},
	{0x3362, 0x35},
	{0x3363, 0x08},
	{0x3364, 0xc4},
	{0x3403, 0x42},
	{0x3088, 0x05},
	{0x3089, 0x00},
	{0x308a, 0x03},
	{0x308b, 0xc0},
	{0x304c, 0x81},//56Mhz PCLK output

	{0x0000 ,0x00},
};
static struct reginfo *sensor_sxga[2] = {
	sensor_sxga_preview,
	sensor_sxga_capture,
};

/* 1024*768 XGA */
static struct reginfo sensor_xga_preview[] =
{
	{0x3012, 0x10},
	{0x3366, 0x15},
	{0x3023, 0x06},
	{0x3026, 0x03},
	{0x3027, 0x04},
	{0x302a, 0x03},
	{0x302b, 0x39},
	{0x3075, 0x24},
	{0x300d, 0x01},
	{0x30d7, 0x90},
	{0x3069, 0x04},
	{0x303e, 0x00},
	{0x303f, 0xc0},
	{0x300e, 0x32},
	{0x300f, 0x21},
	{0x3010, 0x20},
	{0x3011, 0x01},
	//XGA--XGA
	{0x3302, 0xef},
	{0x335f, 0x34},
	{0x3360, 0x0c},
	{0x3361, 0x04},
	{0x3362, 0x34},
	{0x3363, 0x08},
	{0x3364, 0x04},
	{0x3403, 0x42},
	{0x3088, 0x04},
	{0x3089, 0x00},
	{0x308a, 0x03},
	{0x308b, 0x00},
	{0x304c, 0x82},//28Mhz PCLK output


	{0x0000 ,0x00},
};
static struct reginfo sensor_xga_capture[] =
{
	{0x3302, 0xef},
	{0x335f, 0x68},
	{0x3360, 0x18},
	{0x3361, 0x0c},
	{0x3362, 0x34},
	{0x3363, 0x08},
	{0x3364, 0x06},
	{0x3403, 0x42},
	{0x3088, 0x04},
	{0x3089, 0x00},
	{0x308a, 0x03},
	{0x308b, 0x00},
	{0x304c, 0x82},//28Mhz PCLK output

	{0x0000 ,0x00},
};
static struct reginfo *sensor_xga[2] = {
	sensor_xga_preview,
	sensor_xga_capture,
};

/* 800X600 SVGA*/
static struct reginfo sensor_svga_preview[] =
{
	{0x3012, 0x10},
	{0x3366, 0x15},
	{0x3023, 0x06},
	{0x3026, 0x03},
	{0x3027, 0x04},
	{0x302a, 0x03},
	{0x302b, 0x39},
	{0x3075, 0x24},
	{0x300d, 0x01},
	{0x30d7, 0x90},
	{0x3069, 0x04},
	{0x303e, 0x00},
	{0x303f, 0xc0},
	{0x300e, 0x32},
	{0x300f, 0x21},
	{0x3010, 0x20},
	{0x3011, 0x01},
	// XGA-SVGA
	{0x3302, 0xef},
	{0x335f, 0x34},
	{0x3360, 0x0c},
	{0x3361, 0x04},
	{0x3362, 0x23},
	{0x3363, 0x28},
	{0x3364, 0x5c},
	{0x3403, 0x42},
	{0x3088, 0x03},
	{0x3089, 0x20},
	{0x308a, 0x02},
	{0x308b, 0x58},
	{0x304c, 0x83},//28Mhz PCLK output

	{0x0000 ,0x00},
};
static struct reginfo sensor_svga_capture[] =
{
	{0x3302, 0xef},
	{0x335f, 0x68},
	{0x3360, 0x18},
	{0x3361, 0x0c},
	{0x3362, 0x23},
	{0x3363, 0x28},
	{0x3364, 0x5c},
	{0x3403, 0x42},
	{0x3088, 0x03},
	{0x3089, 0x20},
	{0x308a, 0x02},
	{0x308b, 0x58},
	{0x304c, 0x82},//28Mhz PCLK output

	{0x0000 ,0x00},
};
static struct reginfo * sensor_svga[2] = {
	sensor_svga_preview,
	sensor_svga_capture,
};

/* 640X480 VGA */
static struct reginfo sensor_vga_preview[] =
{
	{0x3012, 0x10},
	{0x3366, 0x15},
	{0x3023, 0x06},
	{0x3026, 0x03},
	{0x3027, 0x04},
	{0x302a, 0x03},
	{0x302b, 0x39},
	{0x3075, 0x24},
	{0x300d, 0x01},
	{0x30d7, 0x90},
	{0x3069, 0x04},
	{0x303e, 0x00},
	{0x303f, 0xc0},
	{0x300e, 0x32},
	{0x300f, 0x21},
	{0x3010, 0x20},
	{0x3011, 0x01},
	// XGA-VGA
	{0x3302, 0xef},
	{0x335f, 0x34},
	{0x3360, 0x0c},
	{0x3361, 0x04},
	{0x3362, 0x12},
	{0x3363, 0x88},
	{0x3364, 0xe4},
	{0x3403, 0x42},
	{0x3088, 0x02},
	{0x3089, 0x88},// 0x80, by FAE.
	{0x308a, 0x01},
	{0x308b, 0xe4},// 0xe0, by FAE.
	{0x304c, 0x84}, //0x83, by FAE.

// by FAE.
//AWB short
      // {0x33a7, 0x60},
	//{0x33a8, 0x40},
	//{0x33a9, 0x68},
	//{0x332b, 0x08},
	//{0x330a, 0x22},
	
	//{0x332b, 0x00},
	//{0x330a, 0x02},
//end
	

	{0x0000 ,0x00},
};

static struct reginfo sensor_vga_capture[] =
{
	{0x3302, 0xef},
	{0x335f, 0x68},
	{0x3360, 0x18},
	{0x3361, 0x0c},
	{0x3362, 0x12},
	{0x3363, 0x88},
	{0x3364, 0xe4},
	{0x3403, 0x42},
	{0x3088, 0x02},
	{0x3089, 0x80},
	{0x308a, 0x01},
	{0x308b, 0xe0},
	{0x304c, 0x82},//14Mhz PCLK output 84

	{0x0000 ,0x00},
};
static struct reginfo *sensor_vga[2] = {
	sensor_vga_preview,
	sensor_vga_capture,
};

/* 352X288 CIF */
static struct reginfo sensor_cif_preview[] =
{
	{0x3012, 0x10},
	{0x3366, 0x15},	
	{0x3023, 0x06},
	{0x3026, 0x03},
	{0x3027, 0x04},
	{0x302a, 0x03},
	{0x302b, 0x39},
	{0x3075, 0x24},
	{0x300d, 0x01},
	{0x30d7, 0x90},
	{0x3069, 0x04},
	{0x303e, 0x00},
	{0x303f, 0xc0},
	{0x300e, 0x32},
	{0x300f, 0x21},
	{0x3010, 0x20},
	{0x3011, 0x01},
	//XGA->CIF(352*288)
#if 0	// by FAE.
	{0x3302, 0xef},
	{0x335f, 0x34},
	{0x3360, 0x0c},
	{0x3361, 0x04},
	{0x3362, 0x11},
	{0x3363, 0x6c},
	{0x3364, 0x26},
	{0x3403, 0x42},
	{0x3088, 0x01},
	{0x3089, 0x60},
	{0x308a, 0x01},
	{0x308b, 0x20},
	{0x304c, 0x82}, //89
#else	
	{0x3302, 0xef},
	{0x335f, 0x34},
	{0x3360, 0x0c},
	{0x3361, 0x04},
	{0x3362, 0x11},
	{0x3363, 0x68}, //?
	{0x3364, 0x24},//?
	{0x3403, 0x42},
	{0x3088, 0x01},
	{0x3089, 0x68},
	{0x308a, 0x01},
	{0x308b, 0x24},
	{0x304c, 0x85}, //89	
#endif	

	{0x0000 ,0x00},
};
static struct reginfo sensor_cif_capture[] =
{
	{0x3302, 0xef},
	{0x335f, 0x68},
	{0x3360, 0x18},
	{0x3361, 0x0c},
	{0x3362, 0x11},
	{0x3363, 0x68},
	{0x3364, 0x24},
	{0x3403, 0x42},
	{0x3088, 0x01},
	{0x3089, 0x60},
	{0x308a, 0x01},
	{0x308b, 0x20},
	{0x304c, 0x84},//14Mhz PCLK output

	{0x0000 ,0x00},
};
static struct reginfo *sensor_cif[2] = {
	sensor_cif_preview,
	sensor_cif_capture,
};

/* 320*240 QVGA */
static struct reginfo sensor_qvga_preview[] =
{
	{0x3012, 0x10},
	{0x3366, 0x15},
	{0x3023, 0x06},
	{0x3026, 0x03},
	{0x3027, 0x04},
	{0x302a, 0x03},
	{0x302b, 0x39},
	{0x3075, 0x24},
	{0x300d, 0x01},
	{0x30d7, 0x90},
	{0x3069, 0x04},
	{0x303e, 0x00},
	{0x303f, 0xc0},
	{0x300e, 0x32},
	{0x300f, 0x21},
	{0x3010, 0x20},
	{0x3011, 0x01},
	//XGA->QVGA
	{0x3302, 0xef},
	{0x335f, 0x34},
	{0x3360, 0x0c},
	{0x3361, 0x04},
	{0x3362, 0x01},
	{0x3363, 0x48},
	{0x3364, 0xf4},
	{0x3403, 0x42},
	{0x3088, 0x01},
	{0x3089, 0x40},
	{0x308a, 0x00},
	{0x308b, 0xf0},
	{0x304c, 0x89},//14Mhz PCLK output 

	{0x0000 ,0x00},
};
static struct reginfo sensor_qvga_capture[] =
{
	{0x3302, 0xef},
	{0x335f, 0x68},
	{0x3360, 0x18},
	{0x3361, 0x0c},
	{0x3362, 0x01},
	{0x3363, 0x48},
	{0x3364, 0xf4},
	{0x3403, 0x42},
	{0x3088, 0x01},
	{0x3089, 0x40},
	{0x308a, 0x00},
	{0x308b, 0xf0},
	{0x304c, 0x84},//14Mhz PCLK output

	{0x0000 ,0x00},
};
static  struct reginfo *sensor_qvga[2] = {
	sensor_qvga_preview,
	sensor_qvga_capture,
};

/* 176*144 QCIF */
static struct reginfo sensor_qcif_preview[] =
{
	{0x3012, 0x10},
	{0x3366, 0x15},
	{0x3023, 0x06},
	{0x3026, 0x03},
	{0x3027, 0x04},
	{0x302a, 0x03},
	{0x302b, 0x39},
	{0x3075, 0x24},
	{0x300d, 0x01},
	{0x30d7, 0x90},
	{0x3069, 0x04},
	{0x303e, 0x00},
	{0x303f, 0xc0},
	{0x300e, 0x32},
	{0x300f, 0x21},
	{0x3010, 0x20},
	{0x3011, 0x01},
	//XGA->QCIF	
	{0x3302, 0xef},
	{0x335f, 0x34},
	{0x3360, 0x0c},
	{0x3361, 0x04},
	{0x3362, 0x00},
	{0x3363, 0xb8},
	{0x3364, 0x94},
	{0x3403, 0x42},
	{0x3088, 0x00},
	{0x3089, 0xb0},
	{0x308a, 0x00},
	{0x308b, 0x90},
	{0x304c, 0x82},//14Mhz PCLK output 89

	{0x0000 ,0x00},
};
static struct reginfo sensor_qcif_capture[] =
{
	{0x3302, 0xef},
	{0x335f, 0x68},
	{0x3360, 0x18},
	{0x3361, 0x0c},
	{0x3362, 0x00},
	{0x3363, 0xb8},
	{0x3364, 0x94},
	{0x3403, 0x42},
	{0x3088, 0x00},
	{0x3089, 0xb0},
	{0x308a, 0x00},
	{0x308b, 0x90},
	{0x304c, 0x84},//14Mhz PCLK output

	{0x0000 ,0x00},
};
static  struct reginfo *sensor_qcif[2] = {
	sensor_qcif_preview,
	sensor_qcif_capture,
};

#if 0

static  struct reginfo ov3640_Sharpness_auto[] =
{
    {0x3306, 0x00},
};

static  struct reginfo ov3640_Sharpness1[] =
{
    {0x3306, 0x08},
    {0x3371, 0x00},
};

static  struct reginfo ov3640_Sharpness2[][3] =
{
    //Sharpness 2
    {0x3306, 0x08},
    {0x3371, 0x01},
};

static  struct reginfo ov3640_Sharpness3[] =
{
    //default
    {0x3306, 0x08},
    {0x332d, 0x02},
};
static  struct reginfo ov3640_Sharpness4[]=
{
    //Sharpness 4
    {0x3306, 0x08},
    {0x332d, 0x03},
};

static  struct reginfo ov3640_Sharpness5[] =
{
    //Sharpness 5
    {0x3306, 0x08},
    {0x332d, 0x04},
};
#endif

static  struct reginfo sensor_ClrFmt_YUYV[]=
{
    {0x3400, 0x00},
    {0x0000, 0x00}
};

static  struct reginfo sensor_ClrFmt_UYVY[]=
{
    {0x3400, 0x02},
    {0x0000, 0x00}
};

#if CONFIG_SENSOR_WhiteBalance
static  struct reginfo sensor_WhiteB_Auto[]=
{
		{0x332b, 0x00},//AWB auto, bit[3]:0,auto
	
		{0x0000, 0x00}
};
/* Cloudy Colour Temperature : 6500K - 8000K  */
static  struct reginfo sensor_WhiteB_Cloudy[]=
{
		{0x332b, 0x08},
		{0x33a7, 0x68},
		{0x33a8, 0x40},
		{0x33a9, 0x4e},
		
		{0x0000, 0x00}
};
/* ClearDay Colour Temperature : 5000K - 6500K  */
static  struct reginfo sensor_WhiteB_ClearDay[]=
{
		//Sunny
		{0x332b, 0x08}, //AWB off
		{0x33a7, 0x5e},
		{0x33a8, 0x40},
		{0x33a9, 0x46},
		
		{0x0000, 0x00}
};
/* Office Colour Temperature : 3500K - 5000K  */
static  struct reginfo sensor_WhiteB_TungstenLamp1[]=
{
		//Office
		{0x332b, 0x08},
		{0x33a7, 0x52},
		{0x33a8, 0x40},
		{0x33a9, 0x58},
		
		{0x0000, 0x00}
};
/* Home Colour Temperature : 2500K - 3500K  */
static  struct reginfo sensor_WhiteB_TungstenLamp2[]=
{
		//Home
		{0x332b, 0x08},
		{0x33a7, 0x44},
		{0x33a8, 0x40},
		{0x33a9, 0x70},
		
		{0x0000, 0x00}
};
static struct reginfo *sensor_WhiteBalanceSeqe[] = {sensor_WhiteB_Auto, sensor_WhiteB_TungstenLamp1,sensor_WhiteB_TungstenLamp2,
    sensor_WhiteB_ClearDay, sensor_WhiteB_Cloudy,NULL,
};
#endif

#if CONFIG_SENSOR_Brightness
static  struct reginfo sensor_Brightness0[]=
{
		// Brightness -3
		{0x3302, 0xef},
		{0x3355, 0x04},
		{0x3354, 0x09},
		{0x335e, 0x30},
		
		{0x0000, 0x00}
};

static  struct reginfo sensor_Brightness1[]=
{
		// Brightness -2
		{0x3302, 0xef},
		{0x3355, 0x04},
		{0x3354, 0x09},
		{0x335e, 0x20},
		
		{0x0000, 0x00}
};

static  struct reginfo sensor_Brightness2[]=
{
		// Brightness -1
		{0x3302, 0xef},
		{0x3355, 0x04},
		{0x3354, 0x09},
		{0x335e, 0x10},
		
		{0x0000, 0x00}
};

static  struct reginfo sensor_Brightness3[]=
{
		// Brightness 0
		{0x3302, 0xef},
		{0x3355, 0x04},
		{0x3354, 0x01},
		{0x335e, 0x00},
		
		{0x0000, 0x00}
};

static  struct reginfo sensor_Brightness4[]=
{
		//  Brightness +1
		{0x3302, 0xef},
		{0x3355, 0x04},
		{0x3354, 0x01},
		{0x335e, 0x10},
		
		{0x0000, 0x00}
};

static  struct reginfo sensor_Brightness5[]=
{
		//  Brightness +2
		{0x3302, 0xef},
		{0x3355, 0x04},
		{0x3354, 0x01},
		{0x335e, 0x20},
		
		{0x0000, 0x00}
};

static  struct reginfo sensor_Brightness6[]=
{
		//  Brightness +3
		{0x3302, 0xef},
		{0x3355, 0x04}, //bit[2] enable
		{0x3354, 0x01}, //bit[3] sign of brightness
		{0x335e, 0x30},
		
		{0x0000, 0x00}
};

static struct reginfo *sensor_BrightnessSeqe[] = {sensor_Brightness0, sensor_Brightness1, sensor_Brightness2, sensor_Brightness3,
    sensor_Brightness4, sensor_Brightness5,sensor_Brightness6,NULL,
};

#endif

#if CONFIG_SENSOR_Effect
static  struct reginfo sensor_Effect_Normal[] =
{
		//  Normal
		{0x3302, 0xef},
		{0x3355, 0x00},
		
		{0x0000, 0x00}
};

static  struct reginfo sensor_Effect_WandB[] =
{
		// B&W
		{0x3302, 0xef},
		{0x3355, 0x18}, //bit[4]fix u enable, bit[3]fix v enable
		{0x335a, 0x80},
		{0x335b, 0x80},
		
		{0x0000, 0x00}
};

static  struct reginfo sensor_Effect_Sepia[] =
{
		// Sepia
		{0x3302, 0xef},
		{0x3355, 0x18},
		{0x335a, 0x40},
		{0x335b, 0xa6},
		
		{0x0000, 0x00}
};

static  struct reginfo sensor_Effect_Negative[] =
{
		//  Negative
		{0x3302, 0xef},
		{0x3355, 0x40}, //bit[6] negative
		
		{0x0000, 0x00}
};
static  struct reginfo sensor_Effect_Bluish[] =
{
		//  Bluish
		{0x3302, 0xef},
		{0x3355, 0x18},
		{0x335a, 0xa0},
		{0x335b, 0x40},
		
		{0x0000, 0x00}
};

static  struct reginfo sensor_Effect_Green[] =
{
		// Greenish
		{0x3302, 0xef},
		{0x3355, 0x18},
		{0x335a, 0x60},
		{0x335b, 0x60},
		
		{0x0000, 0x00}
};

static  struct reginfo sensor_Effect_Red[] =
{
		// Reddish
		{0x3302, 0xef},
		{0x3355, 0x18},
		{0x335a, 0x80},
		{0x335b, 0xc0},
		
		{0x0000, 0x00}
};
static struct reginfo *sensor_EffectSeqe[] = {sensor_Effect_Normal, sensor_Effect_WandB, sensor_Effect_Negative,sensor_Effect_Sepia,
    sensor_Effect_Bluish, sensor_Effect_Green,sensor_Effect_Red,NULL,
};
#endif
#if CONFIG_SENSOR_Exposure
static  struct reginfo sensor_Exposure0[]=
{
		// -1.7EV
		{0x3047, 0x00},
		{0x3018, 0x10},
		{0x3019, 0x08},
		{0x301a, 0x21},
		
		{0x0000, 0x00}
};

static  struct reginfo sensor_Exposure1[]=
{
		// -1.3EV
		{0x3047, 0x00},
		{0x3018, 0x18},
		{0x3019, 0x10},
		{0x301a, 0x31},
		
		{0x0000, 0x00}
};

static  struct reginfo sensor_Exposure2[]=
{
		// -1.0EV
		{0x3047, 0x00},
		{0x3018, 0x20},
		{0x3019, 0x18},
		{0x301a, 0x41},
		
		{0x0000, 0x00}
};

static  struct reginfo sensor_Exposure3[]=
{
		// -0.7EV
		{0x3047, 0x00},
		{0x3018, 0x28},
		{0x3019, 0x20},
		{0x301a, 0x51},
		
		{0x0000, 0x00}
};

static  struct reginfo sensor_Exposure4[]=
{
		// -0.3EV
		{0x3047, 0x00},
		{0x3018, 0x30},
		{0x3019, 0x28},
		{0x301a, 0x61},
		
		{0x0000, 0x00}
};

static  struct reginfo sensor_Exposure5[]=
{
		// default
		{0x3047, 0x00},
		{0x3018, 0x38},
		{0x3019, 0x30},
		{0x301a, 0x61},
		
		{0x0000, 0x00}
};

static  struct reginfo sensor_Exposure6[]=
{
		// +0.3EV
		{0x3047, 0x00},
		{0x3018, 0x40},
		{0x3019, 0x38},
		{0x301a, 0x71},
		
		{0x0000, 0x00}
};

static  struct reginfo sensor_Exposure7[]=
{
		// +0.7EV
		{0x3047, 0x00},
		{0x3018, 0x48},
		{0x3019, 0x40},
		{0x301a, 0x81},
		
		{0x0000, 0x00}
};

static  struct reginfo sensor_Exposure8[]=
{
		// +1.0EV
		{0x3047, 0x00},
		{0x3018, 0x50},
		{0x3019, 0x48},
		{0x301a, 0x91},
		
		{0x0000, 0x00}
};

static  struct reginfo sensor_Exposure9[]=
{
		// 1.3EV
		{0x3047, 0x00},
		{0x3018, 0x58},
		{0x3019, 0x50},
		{0x301a, 0x91},
		
		{0x0000, 0x00}
};

static  struct reginfo sensor_Exposure10[]=
{
		// 1.7EV
		{0x3047, 0x00},
		{0x3018, 0x60},
		{0x3019, 0x58},
		{0x301a, 0xa1},
		
		{0x0000, 0x00}
};

static struct reginfo *sensor_ExposureSeqe[] = {sensor_Exposure0, sensor_Exposure1, sensor_Exposure2, sensor_Exposure3,
    sensor_Exposure4, sensor_Exposure5,sensor_Exposure6,sensor_Exposure7,sensor_Exposure8,sensor_Exposure9,
    sensor_Exposure10,NULL,
};
#endif
#if CONFIG_SENSOR_Saturation
static  struct reginfo sensor_Saturation0[]=
{
		// Saturation – 2
		{0x3302, 0xef},
		{0x3355, 0x02},
		{0x3358, 0x10},
		{0x3359, 0x10},
		
		{0x0000, 0x00}
};

static  struct reginfo sensor_Saturation1[]=
{
		// Saturation – 1
		{0x3302, 0xef},
		{0x3355, 0x02},
		{0x3358, 0x30},
		{0x3359, 0x30},
		
		{0x0000, 0x00}
};

static  struct reginfo sensor_Saturation2[]=
{
		// Saturation 0
		{0x3302, 0xef},
		{0x3355, 0x02},
		{0x3358, 0x40},
		{0x3359, 0x40},
		
		{0x0000, 0x00}
};

static  struct reginfo sensor_Saturation3[]=
{
		// Saturation +1
		{0x3302, 0xef},
		{0x3355, 0x02},
		{0x3358, 0x50},
		{0x3359, 0x50},
		
		{0x0000, 0x00}
};

static  struct reginfo sensor_Saturation4[]=
{
		// Saturation +2
		{0x3302, 0xef}, //bit[7]:1, enable SDE
		{0x3355, 0x02}, //enable color saturation
		{0x3358, 0x70},
		{0x3359, 0x70},
		
		{0x0000, 0x00}
};

static struct reginfo *sensor_SaturationSeqe[] = {sensor_Saturation0, sensor_Saturation1, sensor_Saturation2, 
	sensor_Saturation3, sensor_Saturation4, NULL,};

#endif
#if CONFIG_SENSOR_Contrast
static  struct reginfo sensor_Contrast0[]=
{
   // Contrast -3
	{0x3302, 0xef},
	{0x3355, 0x04},
	{0x3354, 0x01},
	{0x335c, 0x14},
	{0x335d, 0x14},

    {0x0000, 0x00}
};

static  struct reginfo sensor_Contrast1[]=
{
		// Contrast -2
		{0x3302, 0xef},
		{0x3355, 0x04},
		{0x3354, 0x01},
		{0x335c, 0x18},
		{0x335d, 0x18},
		
		{0x0000, 0x00}
};

static  struct reginfo sensor_Contrast2[]=
{
		// Contrast -1
		{0x3302, 0xef},
		{0x3355, 0x04},
		{0x3354, 0x01},
		{0x335c, 0x1c},
		{0x335d, 0x1c},
		
		{0x0000, 0x00}
};

static  struct reginfo sensor_Contrast3[]=
{
		// Contrast 0
		{0x3302, 0xef},
		{0x3355, 0x04},
		{0x3354, 0x01},
		{0x335c, 0x20},
		{0x335d, 0x20},
		
		{0x0000, 0x00}
};

static  struct reginfo sensor_Contrast4[]=
{
		// Contrast +1
		{0x3302, 0xef},
		{0x3355, 0x04},
		{0x3354, 0x01},
		{0x335c, 0x24},
		{0x335d, 0x24},
		
		{0x0000, 0x00}
};


static  struct reginfo sensor_Contrast5[]=
{
		// Contrast +2
		{0x3302, 0xef},
		{0x3355, 0x04},
		{0x3354, 0x01},
		{0x335c, 0x28},
		{0x335d, 0x28},
		
		{0x0000, 0x00}
};

static  struct reginfo sensor_Contrast6[]=
{
		// Contrast +3
		{0x3302, 0xef},
		{0x3355, 0x04}, //bit[2] enable contrast/brightness
		{0x3354, 0x01}, //bit[2] Yoffset sign
		{0x335c, 0x2c},
		{0x335d, 0x2c},
		
		{0x0000, 0x00}
};
static struct reginfo *sensor_ContrastSeqe[] = {sensor_Contrast0, sensor_Contrast1, sensor_Contrast2, sensor_Contrast3,
    sensor_Contrast4, sensor_Contrast5, sensor_Contrast6, NULL,
};

#endif
#if CONFIG_SENSOR_Mirror
static  struct reginfo sensor_MirrorOn[]=
{
	{0x307c, 0x12}, //mirror
	{0x3090, 0xc8},
	{0x3023, 0x0a},

    {0x0000, 0x00}
};

static  struct reginfo sensor_MirrorOff[]=
{
	{0x307c, 0x10},// no mirror/flip
	{0x3090, 0xc0},
	{0x3023, 0x0a},

    {0x0000, 0x00}
};

static struct reginfo *sensor_MirrorSeqe[] = {sensor_MirrorOff, sensor_MirrorOn,NULL,};
#endif
#if CONFIG_SENSOR_Flip
static  struct reginfo sensor_FlipOn[]=
{
		{0x307c, 0x11}, //flip
		{0x3023, 0x09},
		{0x3090, 0xc0},
		
		{0x0000, 0x00}
};

static  struct reginfo sensor_FlipOff[]=
{
		{0x307c, 0x10}, // no mirror/flip
		{0x3090, 0xc0},
		{0x3023, 0x0a},
		
		{0x0000, 0x00}
};
static struct reginfo *sensor_FlipSeqe[] = {sensor_FlipOff, sensor_FlipOn,NULL,};

#endif
#if CONFIG_SENSOR_Scene
static  struct reginfo sensor_SceneAuto[] =
{
	{0x3014, 0x84},//bit[3]=0 disable auto night mode
	{0x3015, 0x12},// or 0x02
	{0x302d, 0x00},//clear dummy frame
	{0x302e, 0x00},//clear dummy frame

    {0x0000, 0x00}
};

static  struct reginfo sensor_SceneNight[] =
{
	{0x3014, 0x8c}, //bit[3] =1 enable auto night mode 根据光线强弱自动降帧
	{0x3015, 0x22},//bit[6:4] 设置不同,降的帧率不一样.可以看datasheet
       			//bit[6:4]
              //000  no dummy frame
              //001  1 dummy frame
              //010  2 dummy frame
              //011  3 dummy frame
              //100  7 dummy frame
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

static const struct v4l2_queryctrl sensor_controls[] =
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
        .maximum	= 3,
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
        .minimum	= -5,
        .maximum	= 5,
        .step		= 1,
        .default_value = 0,
    },
	#endif

	#if CONFIG_SENSOR_Saturation
	{
        .id		= V4L2_CID_SATURATION,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "Saturation Control",
        .minimum	= -2,
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
    {
        .id		= V4L2_CID_FOCUS_AUTO,
        .type		= V4L2_CTRL_TYPE_BOOLEAN,
        .name		= "Focus Control",
        .minimum	= 0,
        .maximum	= 1,
        .step		= 1,
        .default_value = 0,
    },
#if 0 //IF CONTINOUS IS SUPPORT, SET TO 1
    {
        .id		= V4L2_CID_FOCUS_CONTINUOUS,
        .type		= V4L2_CTRL_TYPE_BOOLEAN,
        .name		= "Focus Control",
        .minimum	= 0,
        .maximum	= 1,
        .step		= 1,
        .default_value = 0,
    },
#endif	
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
    {V4L2_MBUS_FMT_UYVY8_2X8, V4L2_COLORSPACE_JPEG},
    {V4L2_MBUS_FMT_YUYV8_2X8, V4L2_COLORSPACE_JPEG}	
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
	bool snap2preview;
	bool video2preview;
    unsigned char mirror;                                        /* HFLIP */
    unsigned char flip;                                          /* VFLIP */
    unsigned int winseqe_cur_addr;
	struct sensor_datafmt fmt;
	unsigned int enable;
	unsigned int funmodule_state;
} sensor_info_priv_t;

struct sensor
{
    struct v4l2_subdev subdev;
    struct i2c_client *client;
    sensor_info_priv_t info_priv;
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
    msg->scl_rate = CONFIG_SENSOR_I2C_SPEED;                                        /* ddl@rock-chips.com : 100kHz */
    msg->read_type =  0;               /* fpga i2c:0==I2C_NORMAL : direct use number not enum for don't want include spi_fpga.h */

    cnt = 1;
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
    msg[0].scl_rate = CONFIG_SENSOR_I2C_SPEED;                                        /* ddl@rock-chips.com : 100kHz */
    msg[0].read_type = 2;   /* fpga i2c:2==I2C_NO_STOP : direct use number not enum for don't want include spi_fpga.h */

    msg[1].addr = client->addr;
    msg[1].flags = client->flags|I2C_M_RD;
    msg[1].buf = buf;
    msg[1].len = 1;
    msg[1].scl_rate = CONFIG_SENSOR_I2C_SPEED;                                        /* ddl@rock-chips.com : 100kHz */
    msg[1].read_type = 2;   /* fpga i2c:2==I2C_NO_STOP : direct use number not enum for don't want include spi_fpga.h */

    cnt = 1;
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
    int err=0, cnt;
    int i = 0;
#if CONFIG_SENSOR_Focus
	struct sensor *sensor = to_sensor(client);
#endif
#if CONFIG_SENSOR_I2C_RDWRCHK
	char valchk;
#endif

	cnt = 0;
	if (sensor_task_lock(client, 1) < 0)
		goto sensor_write_array_end;
    while (regarray[i].reg != 0)
    {
    #if CONFIG_SENSOR_Focus
    	if ((regarray == sensor_af_firmware) && (sensor->info_priv.enable == 0)) {
			SENSOR_DG("%s disable, Download af firmware terminated!\n",SENSOR_NAME_STRING());
			err = -EINVAL;
			goto sensor_write_array_end;
    	}
		#endif
		
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

		#if CONFIG_SENSOR_Focus
	if (((regarray->reg == SEQUENCE_PROPERTY) && (regarray->val == SEQUENCE_INIT))
		|| (regarray == sensor_init_data)) {
		sensor->info_priv.affm_reinit = 1;
	}
	#endif
	
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
#if CONFIG_SENSOR_Focus
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
		if (cmdinfo->validate_bit & 0x80) {
			if (sensor_write(client, CMD_TAG_Reg, cmdinfo->cmd_tag)) {
				SENSOR_TR("%s write CMD_TAG_Reg(main:0x%x tag:0x%x) error!\n",SENSOR_NAME_STRING(),cmd_main,cmdinfo->cmd_tag);
				goto sensor_af_cmdset_err;
			}
			SENSOR_DG("%s write CMD_TAG_Reg(main:0x%x tag:0x%x) success!\n",SENSOR_NAME_STRING(),cmd_main,cmdinfo->cmd_tag);
		}
		for (i=0; i<4; i++) {
			if (cmdinfo->validate_bit & (1<<i)) {
				if (sensor_write(client, CMD_PARA0_Reg-i, cmdinfo->cmd_para[i])) {
					SENSOR_TR("%s write CMD_PARA_Reg(main:0x%x para%d:0x%x) error!\n",SENSOR_NAME_STRING(),cmd_main,i,cmdinfo->cmd_para[i]);
					goto sensor_af_cmdset_err;
				}
				SENSOR_DG("%s write CMD_PARA_Reg(main:0x%x para%d:0x%x) success!\n",SENSOR_NAME_STRING(),cmd_main,i,cmdinfo->cmd_para[i]);
			}
		}
	} else {
		if (sensor_write(client, CMD_TAG_Reg, 0xff)) {
			SENSOR_TR("%s write CMD_TAG_Reg(main:0x%x no tag) error!\n",SENSOR_NAME_STRING(),cmd_main);
			goto sensor_af_cmdset_err;
		}
		SENSOR_DG("%s write CMD_TAG_Reg(main:0x%x no tag) success!\n",SENSOR_NAME_STRING(),cmd_main);
	}

	if (sensor_write(client, CMD_MAIN_Reg, cmd_main)) {
		SENSOR_TR("%s write CMD_MAIN_Reg(main:0x%x) error!\n",SENSOR_NAME_STRING(),cmd_main);
		goto sensor_af_cmdset_err;
	}

	cnt = 0;
	do
	{
		msleep(5);
		if (sensor_read(client,CMD_TAG_Reg,&read_tag)){
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
	char state,cnt;

	cnt = 0;
	do
	{
		ret = sensor_read(client, STA_FOCUS_Reg, &state);
		if (ret != 0){
		   SENSOR_TR("%s[%d] read focus_status failed\n",SENSOR_NAME_STRING(),__LINE__);
		   ret = -1;
		   goto sensor_af_idlechk_end;
		}

		if (state != S_IDLE) {
			sensor_af_cmdset(client, ReturnIdle_Cmd, NULL);
			msleep(1);
			cnt++;
		}
    } while((state != S_IDLE)&& (cnt<100));

	ret = (state == S_IDLE) ? 0 : -1;

sensor_af_idlechk_end:
	return ret;
}

static int sensor_af_single(struct i2c_client *client)
{
	int ret = 0;
	char state,cnt;

	if (sensor_af_idlechk(client))
		goto sensor_af_single_end;

	if (sensor_af_cmdset(client, SingleFocus_Cmd, NULL)) {
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
    }

	//sensor_af_cmdset(client, ReturnIdle_Cmd, NULL); // by duanyp. fix af blur when taking pictures
sensor_af_single_end:
	return ret;
}

static int sensor_af_const(struct i2c_client *client)
{
	int ret = 0;

	if (sensor_af_idlechk(client))
		goto sensor_af_const_end;

	if (sensor_af_cmdset(client, ConstFocus_Cmd, NULL)) {
		SENSOR_TR("%s const focus mode set error!\n",SENSOR_NAME_STRING());
		ret = -1;
		goto sensor_af_const_end;
	}
sensor_af_const_end:
	return ret;
}
static int sensor_af_pause2capture(struct i2c_client *client)
{
	int ret = 0;
	char state,cnt;

	if (sensor_af_cmdset(client, PauseFocus_Cmd, NULL)) {
		SENSOR_TR("%s pause focus mode set error!\n",SENSOR_NAME_STRING());
		ret = -1;
		goto sensor_af_pause_end;
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
		   goto sensor_af_pause_end;
		}
    }while((state != S_CAPTURE) && (cnt<100));

	if (state != S_CAPTURE) {
        SENSOR_TR("%s[%d] focus state(0x%x) is error!\n",SENSOR_NAME_STRING(),__LINE__,state);
		ret = -1;
		goto sensor_af_pause_end;
    }
sensor_af_pause_end:
	return ret;
}
static int sensor_af_zoneupdate(struct i2c_client *client)
{
	int ret = 0;

	if (sensor_af_idlechk(client))
		goto sensor_af_zoneupdate_end;

	if (sensor_af_cmdset(client, UpdateZone_Cmd, NULL)) {
		SENSOR_TR("%s update zone fail!\n",SENSOR_NAME_STRING());
		ret = -1;
		goto sensor_af_zoneupdate_end;
	}

sensor_af_zoneupdate_end:
	return ret;
}
static int sensor_af_init(struct i2c_client *client)
{
	int ret = 0;
	char state,cnt;

	ret = sensor_write_array(client, sensor_af_firmware);
    if (ret != 0) {
       SENSOR_TR("%s Download firmware failed\n",SENSOR_NAME_STRING());
       ret = -1;
	   goto sensor_af_init_end;
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
		   goto sensor_af_init_end;
		}
    }while((state == S_STARTUP) && (cnt<100));

    if (state != S_IDLE) {
        SENSOR_TR("%s focus state(0x%x) is error!\n",SENSOR_NAME_STRING(),state);
		ret = -1;
		goto sensor_af_init_end;
    }

sensor_af_init_end:
	SENSOR_DG("%s %s ret:0x%x \n",SENSOR_NAME_STRING(),__FUNCTION__,ret);
	return ret;
}

static int sensor_af_wq_function(struct i2c_client *client)
{
	struct sensor *sensor = to_sensor(client);
	struct af_cmdinfo cmdinfo;
	int ret=0, focus_pos = 0xfe;

	SENSOR_DG("%s %s Enter\n",SENSOR_NAME_STRING(), __FUNCTION__);

	mutex_lock(&sensor->wq_lock);
	if (sensor_af_init(client)) {
		sensor->info_priv.funmodule_state &= (~SENSOR_AF_IS_OK);
		ret = -1;
	} else {
		sensor->info_priv.funmodule_state |= SENSOR_AF_IS_OK;

		switch (sensor->info_priv.auto_focus)
		{
			case SENSOR_AF_MODE_INFINITY:
			{
				focus_pos = 0x00;
			}
			case SENSOR_AF_MODE_MACRO:
			{
				if (focus_pos != 0x00)
					focus_pos = 0xff;

				sensor_af_idlechk(client);
				cmdinfo.cmd_tag = StepFocus_Spec_Tag;
				cmdinfo.cmd_para[0] = focus_pos;
				cmdinfo.validate_bit = 0x81;
				ret = sensor_af_cmdset(client, StepMode_Cmd, &cmdinfo);
				break;
			}
			case SENSOR_AF_MODE_AUTO:
			{
				ret = sensor_af_single(client);
				break;
			}
			case SENSOR_AF_MODE_CONTINUOUS:
			{
				ret = sensor_af_const(client);
				break;
			}
			case SENSOR_AF_MODE_CLOSE:
			{
				ret = 0;
				break;
			}
			default:
            {
				SENSOR_DG("%s focus mode(0x%x) is unkonwn\n",SENSOR_NAME_STRING(),sensor->info_priv.auto_focus);
                goto sensor_af_wq_function_end;
			}
		}

		SENSOR_DG("%s sensor_af_wq_function set focus mode(0x%x) ret:0x%x\n",SENSOR_NAME_STRING(), sensor->info_priv.auto_focus,ret);
	}

sensor_af_wq_function_end:
	sensor->sensor_wk.state = sensor_work_ready;
	mutex_unlock(&sensor->wq_lock);
	return ret;
}
static void sensor_af_workqueue(struct work_struct *work)
{
	struct sensor_work *sensor_work = container_of(work, struct sensor_work, dwork.work);
	struct i2c_client *client = sensor_work->client;

	if (sensor_af_wq_function(client) < 0) {
		SENSOR_TR("%s af workqueue return false\n",SENSOR_NAME_STRING());
	}
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
    		struct sensor *sensor = to_sensor(client);

			if (sensor->sensor_io_request && sensor->sensor_io_request->sensor_ioctrl) {
				sensor->sensor_io_request->sensor_ioctrl(icd->pdev,Cam_Flash, on);
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
static int sensor_init(struct v4l2_subdev *sd, u32 val)
{
    struct i2c_client *client = sd->priv;
    struct soc_camera_device *icd = client->dev.platform_data;
    struct sensor *sensor = to_sensor(client);
	const struct v4l2_queryctrl *qctrl;
    const struct sensor_datafmt *fmt;    
    char value;
    int ret,pid = 0;

    SENSOR_DG("\n%s..%s.. \n",SENSOR_NAME_STRING(),__FUNCTION__);

	if (sensor_ioctrl(icd, Sensor_PowerDown, 0) < 0) {
		ret = -ENODEV;
		goto sensor_INIT_ERR;
	}
/* ddl@rock-chips.com : ov3640_powerOn have hardware reset */
#if 0
    /* soft reset */
	if (sensor_task_lock(client,1)<0)
		goto sensor_INIT_ERR;
    ret = sensor_write(client, 0x3012, 0x80);
    if (ret != 0)
    {
        SENSOR_TR("%s soft reset sensor failed\n",SENSOR_NAME_STRING());
        ret = -ENODEV;
		goto sensor_INIT_ERR;
    }

    mdelay(5);  //delay 5 microseconds
#endif
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
	//    sensor_set_focus(); 
       qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_FOCUS_ABSOLUTE);//?,JYK
	if (qctrl)
        sensor->info_priv.focus = qctrl->default_value;
	#endif

	#if CONFIG_SENSOR_Flash
	//sensor_set_flash();
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_FLASH);
	if (qctrl)
        sensor->info_priv.flash = qctrl->default_value;
    #endif

    SENSOR_DG("\n%s..%s.. icd->width = %d.. icd->height %d\n",SENSOR_NAME_STRING(),((val == 0)?__FUNCTION__:"sensor_reinit"),icd->user_width,icd->user_height);

    return 0;
sensor_INIT_ERR:
	sensor_task_lock(client,0);
	sensor_deactivate(client);
    return ret;
}
static int sensor_deactivate(struct i2c_client *client)
{
	struct soc_camera_device *icd = client->dev.platform_data;

	SENSOR_DG("\n%s..%s.. Enter\n",SENSOR_NAME_STRING(),__FUNCTION__);

	/* ddl@rock-chips.com : all sensor output pin must change to input for other sensor */
	sensor_ioctrl(icd, Sensor_PowerDown, 1);
	/* ddl@rock-chips.com : sensor config init width , because next open sensor quickly(soc_camera_open -> Try to configure with default parameters) */
	icd->user_width = SENSOR_INIT_WIDTH;
    icd->user_height = SENSOR_INIT_HEIGHT;
	msleep(100);
	return 0;
}
static  struct reginfo sensor_power_down_sequence[]=
{
		{0x361e, 0x00},
		{0x308d, 0x06},
		{0x30ad, 0x82},
		{0x308d, 0x0f},
		
		{0x0000, 0x00},
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
    struct i2c_client *client = sd->priv;
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
static int sensor_s_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
    struct i2c_client *client = sd->priv;
    const struct sensor_datafmt *fmt;
	struct soc_camera_device *icd = client->dev.platform_data;
    struct sensor *sensor = to_sensor(client);
    const struct v4l2_queryctrl *qctrl;
    struct reginfo *winseqe_set_addr=NULL;
    int ret=0, set_w,set_h;
	int isCapture = 0; 
	
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
	isCapture = sensor_fmt_capturechk(sd, mf);
	
	if (((set_w <= 176) && (set_h <= 144)) && sensor_qcif[isCapture][0].reg)
	{
		winseqe_set_addr = sensor_qcif[isCapture];
        set_w = 176;
        set_h = 144;
	}
	else if (((set_w <= 320) && (set_h <= 240)) && sensor_qvga[isCapture][0].reg)
    {
        winseqe_set_addr = sensor_qvga[isCapture];
        set_w = 320;
        set_h = 240;
    }
    else if (((set_w <= 352) && (set_h<= 288)) && sensor_cif[isCapture][0].reg)
    {
        winseqe_set_addr = sensor_cif[isCapture];
        set_w = 352;
        set_h = 288;
    }
    else if (((set_w <= 640) && (set_h <= 480)) && sensor_vga[isCapture][0].reg)
    {
        winseqe_set_addr = sensor_vga[isCapture];
        set_w = 640;
        set_h = 480;
    }
    else if (((set_w <= 800) && (set_h <= 600)) && sensor_svga[isCapture][0].reg)
    {
        winseqe_set_addr = sensor_svga[isCapture];
        set_w = 800;
        set_h = 600;
    }
    else if (((set_w <= 1024) && (set_h <= 768)) && sensor_sxga[isCapture][0].reg)
    {
        winseqe_set_addr = sensor_xga[isCapture];
        set_w = 1024;
        set_h = 768;
    }
    else if (((set_w <= 1280) && (set_h <= 960)) && sensor_sxga[isCapture][0].reg)
    {
        winseqe_set_addr = sensor_sxga[isCapture];
        set_w = 1280;
        set_h = 960;
    }
    else if (((set_w <= 1600) && (set_h <= 1200)) && sensor_uxga[isCapture][0].reg)
    {
        winseqe_set_addr = sensor_uxga[isCapture];
        set_w = 1600;
        set_h = 1200;
    }
    else if (((set_w <= 2048) && (set_h <= 1536)) && sensor_uxga[isCapture][0].reg)
    {
        winseqe_set_addr = sensor_qxga[isCapture];
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

    if ((int)winseqe_set_addr  != sensor->info_priv.winseqe_cur_addr){
		//srt --if capture,then should write sensor_qxga[1] first		
		if((winseqe_set_addr != sensor_qxga[isCapture]) && isCapture) {
			SENSOR_DG("%s  write sensor_qxga[1]\n", SENSOR_NAME_STRING());
			ret = sensor_write_array(client, sensor_qxga[isCapture]);
		        if (ret != 0) {
		            SENSOR_TR("%s  write sensor_qxga[1] failed\n", SENSOR_NAME_STRING());
		            return ret;
		        }
		}

		#if CONFIG_SENSOR_Focus
			//sensor_af_idlechk(client);
			if (sensor->info_priv.auto_focus == SENSOR_AF_MODE_CONTINUOUS)
			{
				sensor_af_idlechk(client); // by duanyp
				sensor_af_cmdset(client, PauseFocus_Cmd, NULL);
			}
		#endif

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
		
        ret = sensor_write_array(client, winseqe_set_addr);        
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
        } else {
            sensor->info_priv.winseqe_cur_addr  = (int)winseqe_set_addr;
            if (sensor_fmt_capturechk(sd,mf) == true) {				    /* ddl@rock-chips.com : Capture */
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
    			sensor->info_priv.video2preview = false;
    			sensor->info_priv.snap2preview = false;
    		}
            mdelay(100);  // by FAE.
        }
        SENSOR_DG("\n%s..%s.. icd->width=%d..icd->height=%d..isCapture=%d\n",SENSOR_NAME_STRING(),__FUNCTION__,set_w,set_h,isCapture);
    } else {
        SENSOR_TR("\n %s .. Current Format is validate. icd->width=%d..icd->height=%d..isCapture=%d\n",SENSOR_NAME_STRING(),set_w,set_h,isCapture);
    }
    mf->width = set_w;
	mf->height = set_h;
sensor_s_fmt_end:
    return ret;
}

static int sensor_try_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
    struct i2c_client *client = sd->priv;
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
    struct i2c_client *client = sd->priv;

    if (id->match.type != V4L2_CHIP_MATCH_I2C_ADDR)
        return -EINVAL;

    if (id->match.addr != client->addr)
        return -ENODEV;

    id->ident = SENSOR_V4L2_IDENT;      /* ddl@rock-chips.com :  Return OV3640  identifier */
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

#if CONFIG_SENSOR_Focus
static int sensor_set_focus_absolute(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
	struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
    struct sensor *sensor = to_sensor(client);
	const struct v4l2_queryctrl *qctrl_info;
	struct af_cmdinfo cmdinfo;
	int ret = 0;

	qctrl_info = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_FOCUS_ABSOLUTE);
	if (!qctrl_info)
		return -EINVAL;

	if ((sensor->info_priv.funmodule_state & SENSOR_AF_IS_OK) && (sensor->info_priv.affm_reinit == 0)) {
		if ((value >= qctrl_info->minimum) && (value <= qctrl_info->maximum)) {

			if (sensor_af_idlechk(client))
				goto sensor_set_focus_absolute_end;

			cmdinfo.cmd_tag = StepFocus_Spec_Tag;
			cmdinfo.cmd_para[0] = value;
			cmdinfo.validate_bit = 0x81;
			ret = sensor_af_cmdset(client, StepMode_Cmd, &cmdinfo);
			//ret |= sensor_af_cmdset(client, ReturnIdle_Cmd, NULL);
			SENSOR_DG("%s..%s : %d  ret:0x%x\n",SENSOR_NAME_STRING(),__FUNCTION__, value,ret);
		} else {
			ret = -EINVAL;
			SENSOR_TR("\n %s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
		}
	} else {
		ret = -EACCES;
		SENSOR_TR("\n %s..%s AF module state(0x%x, 0x%x) is error!\n",SENSOR_NAME_STRING(),__FUNCTION__,
			sensor->info_priv.funmodule_state,sensor->info_priv.affm_reinit);
	}

sensor_set_focus_absolute_end:
	return ret;
}
static int sensor_set_focus_relative(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
	struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
	struct sensor *sensor = to_sensor(client);
	const struct v4l2_queryctrl *qctrl_info;
	struct af_cmdinfo cmdinfo;
	int ret = 0;

	qctrl_info = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_FOCUS_RELATIVE);
	if (!qctrl_info)
		return -EINVAL;

	if ((sensor->info_priv.funmodule_state & SENSOR_AF_IS_OK) && (sensor->info_priv.affm_reinit == 0)) {
		if ((value >= qctrl_info->minimum) && (value <= qctrl_info->maximum)) {

			if (sensor_af_idlechk(client))
				goto sensor_set_focus_relative_end;

			if (value > 0) {
				cmdinfo.cmd_tag = StepFocus_Near_Tag;
			} else if (value < 0) {
				cmdinfo.cmd_tag = StepFocus_Far_Tag;
			}
			cmdinfo.validate_bit = 0x80;
			ret = sensor_af_cmdset(client, StepMode_Cmd, &cmdinfo);

			SENSOR_DG("%s..%s : %d  ret:0x%x\n",SENSOR_NAME_STRING(),__FUNCTION__, value,ret);
		} else {
			ret = -EINVAL;
			SENSOR_TR("\n %s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
		}
	} else {
		ret = -EACCES;
		SENSOR_TR("\n %s..%s AF module state(0x%x, 0x%x) is error!\n",SENSOR_NAME_STRING(),__FUNCTION__,
			sensor->info_priv.funmodule_state,sensor->info_priv.affm_reinit);
	}
sensor_set_focus_relative_end:
	return ret;
}

static int sensor_set_focus_mode(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
	struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
	struct sensor *sensor = to_sensor(client);
	int ret = 0;

	if ((sensor->info_priv.funmodule_state & SENSOR_AF_IS_OK)  && (sensor->info_priv.affm_reinit == 0)) {
		switch (value)
		{
			case SENSOR_AF_MODE_AUTO:
			{
				ret = sensor_af_single(client);				
				break;
			}

			case SENSOR_AF_MODE_MACRO:
			{
				ret = sensor_set_focus_absolute(icd, qctrl, 0xff);
				break;
			}

			case SENSOR_AF_MODE_INFINITY:
			{
				ret = sensor_set_focus_absolute(icd, qctrl, 0x00);
				break;
			}

			case SENSOR_AF_MODE_CONTINUOUS:
			{
				ret = sensor_af_const(client);
				break;
			}
			default:
				SENSOR_TR("\n %s..%s AF value(0x%x) is error!\n",SENSOR_NAME_STRING(),__FUNCTION__,value);
				break;

		}

		SENSOR_DG("%s..%s : %d  ret:0x%x\n",SENSOR_NAME_STRING(),__FUNCTION__, value,ret);
	} else {
		ret = -EACCES;
		SENSOR_TR("\n %s..%s AF module state(0x%x, 0x%x) is error!\n",SENSOR_NAME_STRING(),__FUNCTION__,
			sensor->info_priv.funmodule_state,sensor->info_priv.affm_reinit);
	}

	return ret;
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
        SENSOR_DG("%s..%s : %d\n",SENSOR_NAME_STRING(),__FUNCTION__, value);
        return 0;
    }
    
	SENSOR_TR("\n %s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
    return -EINVAL;
}
#endif
static int sensor_g_control(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
    struct i2c_client *client = sd->priv;
    struct sensor *sensor = to_sensor(client);
    const struct v4l2_queryctrl *qctrl;

    qctrl = soc_camera_find_qctrl(&sensor_ops, ctrl->id);

    if (!qctrl)
    {
        SENSOR_TR("\n %s ioctrl id = 0x%x  is invalidate \n", SENSOR_NAME_STRING(), ctrl->id);
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
    struct i2c_client *client = sd->priv;
    struct sensor *sensor = to_sensor(client);
    struct soc_camera_device *icd = client->dev.platform_data;
    const struct v4l2_queryctrl *qctrl;


    qctrl = soc_camera_find_qctrl(&sensor_ops, ctrl->id);

    if (!qctrl)
    {
        SENSOR_TR("\n %s ioctrl id = 0x%x  is invalidate \n", SENSOR_NAME_STRING(), ctrl->id);
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
        SENSOR_TR("\n %s ioctrl id = 0x%x  is invalidate \n", SENSOR_NAME_STRING(), ext_ctrl->id);
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
        SENSOR_TR("\n %s ioctrl id = 0x%x  is invalidate \n", SENSOR_NAME_STRING(), ext_ctrl->id);
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

				if (sensor_set_focus_absolute(icd, qctrl,ext_ctrl->value) == 0) {
					if (ext_ctrl->value == qctrl->minimum) {
						sensor->info_priv.auto_focus = SENSOR_AF_MODE_INFINITY;
					} else if (ext_ctrl->value == qctrl->maximum) {
						sensor->info_priv.auto_focus = SENSOR_AF_MODE_MACRO;
					} else {
						sensor->info_priv.auto_focus = SENSOR_AF_MODE_FIXED;
					}
				}

                break;
            }		
         case V4L2_CID_FOCUS_RELATIVE:
            {
                if ((ext_ctrl->value < qctrl->minimum) || (ext_ctrl->value > qctrl->maximum))
                    return -EINVAL;

                sensor_set_focus_relative(icd, qctrl,ext_ctrl->value);
                break;
            }
		case V4L2_CID_FOCUS_AUTO:
			{
				if (ext_ctrl->value == 1) {
					if (sensor_set_focus_mode(icd, qctrl,SENSOR_AF_MODE_AUTO) != 0)
						return -EINVAL;
					sensor->info_priv.auto_focus = SENSOR_AF_MODE_AUTO;
				} else if (SENSOR_AF_MODE_AUTO == sensor->info_priv.auto_focus){
					if (ext_ctrl->value == 0)
						sensor->info_priv.auto_focus = SENSOR_AF_MODE_CLOSE;
				}
				break;
			}
		/*
		case V4L2_CID_FOCUS_CONTINUOUS:
			{
				if (SENSOR_AF_MODE_CONTINUOUS != sensor->info_priv.auto_focus) {
					if (ext_ctrl->value == 1) {
						if (sensor_set_focus_mode(icd, qctrl,SENSOR_AF_MODE_CONTINUOUS) != 0)
							return -EINVAL;
						sensor->info_priv.auto_focus = SENSOR_AF_MODE_CONTINUOUS;
					}
				} else {
					if (ext_ctrl->value == 0)
						sensor->info_priv.auto_focus = SENSOR_AF_MODE_CLOSE;
				}
				break;
			}*/
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
    struct i2c_client *client = sd->priv;
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
    struct i2c_client *client = sd->priv;
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
	struct i2c_client *client = sd->priv;
    struct sensor *sensor = to_sensor(client);
	#if CONFIG_SENSOR_Focus
	struct soc_camera_device *icd = client->dev.platform_data;
	struct v4l2_mbus_framefmt mf;
	#endif

	if (enable == 1) {
		sensor->info_priv.enable = 1;
		#if CONFIG_SENSOR_Focus
		mf.width	= icd->user_width;
    	mf.height	= icd->user_height;
    	mf.code	= sensor->info_priv.fmt.code;
    	mf.colorspace	= sensor->info_priv.fmt.colorspace;
    	mf.field	= V4L2_FIELD_NONE;
		/* If auto focus firmware haven't download success, must download firmware again when in video or preview stream on */
		if (sensor_fmt_capturechk(sd, &mf) == false) {
			if ((sensor->info_priv.affm_reinit == 1) || ((sensor->info_priv.funmodule_state & SENSOR_AF_IS_OK)==0)) {
				if (sensor->sensor_wq != NULL) {
					mutex_lock(&sensor->wq_lock);
					if (sensor->sensor_wk.state == sensor_working) {
						SENSOR_DG("%s sensor af firmware thread is runing, Ingore current work",SENSOR_NAME_STRING());
						mutex_unlock(&sensor->wq_lock);
						goto sensor_s_stream_end;
					}
					sensor->sensor_wk.state = sensor_working;
					mutex_unlock(&sensor->wq_lock);
					sensor->sensor_wk.client = client;
					INIT_WORK(&(sensor->sensor_wk.dwork.work), sensor_af_workqueue);
					queue_delayed_work(sensor->sensor_wq,&(sensor->sensor_wk.dwork), 0);
				}
				sensor->info_priv.affm_reinit = 0;
			}
		}
		#endif
	} else if (enable == 0) {
		sensor->info_priv.enable = 0;
		#if CONFIG_SENSOR_Focus
		flush_work(&(sensor->sensor_wk.dwork.work));
		mutex_lock(&sensor->wq_lock);
		sensor->sensor_wk.state = sensor_work_ready;
		mutex_unlock(&sensor->wq_lock);
		#endif
	}

sensor_s_stream_end:
	return 0;
}

/* Interface active, can use i2c. If it fails, it can indeed mean, that
 * this wasn't our capture interface, so, we wait for the right one */
static int sensor_video_probe(struct soc_camera_device *icd,
			       struct i2c_client *client)
{
    char value;
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
    ret = sensor_write(client, 0x3012, 0x80);
    if (ret != 0) {
        SENSOR_TR("soft reset %s failed\n",SENSOR_NAME_STRING());
        ret = -ENODEV;
		goto sensor_video_probe_err;
    }
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
    return 0;

sensor_video_probe_err:
    return ret;
}
static long sensor_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct i2c_client *client = sd->priv;
    struct soc_camera_device *icd = client->dev.platform_data;
    struct sensor *sensor = to_sensor(client);
    int ret = 0,i;

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
                printk("flash io:%d\n",sensor->sensor_gpio_res->gpio_flash);
                if (sensor->sensor_gpio_res->gpio_flash == INVALID_GPIO) {
                    for (i = 0; i < icd->ops->num_controls; i++) {
                		if (V4L2_CID_FLASH == icd->ops->controls[i].id) {
                			memset((char*)&icd->ops->controls[i],0x00,sizeof(struct v4l2_queryctrl));                			
                		}
                    }
                    sensor->info_priv.flash = 0xff;
                    SENSOR_DG("%s flash gpio is invalidate!\n",SENSOR_NAME_STRING());
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
	#if CONFIG_SENSOR_I2C_NOSCHED
	atomic_set(&sensor->tasklock_cnt,0);
	#endif

    ret = sensor_video_probe(icd, client);
    if (ret < 0) {
        icd->ops = NULL;
        i2c_set_clientdata(client, NULL);
        kfree(sensor);
	sensor = NULL;
    } else {
		#if CONFIG_SENSOR_Focus
		sensor->sensor_wq = create_workqueue(SENSOR_NAME_STRING( wq));
		if (sensor->sensor_wq == NULL)
			SENSOR_TR("%s workqueue create fail!", SENSOR_NAME_STRING( wq));
		mutex_init(&sensor->wq_lock);
		sensor->sensor_wk.state = sensor_work_ready;
		#endif
    }
    SENSOR_DG("\n%s..%s..%d  ret = %x \n",__FUNCTION__,__FILE__,__LINE__,ret);
    return ret;
}

static int sensor_remove(struct i2c_client *client)
{
    struct sensor *sensor = to_sensor(client);
    struct soc_camera_device *icd = client->dev.platform_data;

	#if CONFIG_SENSOR_Focus
	if (sensor->sensor_wq) {
		destroy_workqueue(sensor->sensor_wq);
		sensor->sensor_wq = NULL;
	}
	#endif
	
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

