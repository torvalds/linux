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

#define _CONS(a,b) a##b
#define CONS(a,b) _CONS(a,b)

#define __STR(x) #x
#define _STR(x) __STR(x)
#define STR(x) _STR(x)

/* Sensor Driver Configuration */
#define SENSOR_NAME ov3640
#define SENSOR_V4L2_IDENT V4L2_IDENT_OV3640
#define SENSOR_ID 0x364c
#define SENSOR_MIN_WIDTH    176
#define SENSOR_MIN_HEIGHT   144
#define SENSOR_MAX_WIDTH    2048
#define SENSOR_MAX_HEIGHT   1536
#define SENSOR_INIT_WIDTH	640			/* Sensor pixel size for sensor_init_data array */
#define SENSOR_INIT_HEIGHT  480
#define SENSOR_INIT_WINSEQADR sensor_init_data
#define SENSOR_INIT_PIXFMT V4L2_PIX_FMT_YUYV

#define CONFIG_SENSOR_WhiteBalance	1
#define CONFIG_SENSOR_Brightness	0
#define CONFIG_SENSOR_Contrast      0
#define CONFIG_SENSOR_Saturation    0
#define CONFIG_SENSOR_Effect        1
#define CONFIG_SENSOR_Scene         1
#define CONFIG_SENSOR_DigitalZoom   0
#define CONFIG_SENSOR_Focus         0
#define CONFIG_SENSOR_Exposure      0
#define CONFIG_SENSOR_Flash         0
#define CONFIG_SENSOR_Mirror        0
#define CONFIG_SENSOR_Flip          0
#define CONFIG_SENSOR_I2C_SPEED     200000       /* Hz */

#define CONFIG_SENSOR_TR      1
#define CONFIG_SENSOR_DEBUG	  1

#define SENSOR_NAME_STRING(a) STR(CONS(SENSOR_NAME, a))
#define SENSOR_NAME_VARFUN(a) CONS(SENSOR_NAME, a)

#define MIN(x,y)   ((x<y) ? x: y)
#define MAX(x,y)    ((x>y) ? x: y)

#if (CONFIG_SENSOR_TR)
	#define SENSOR_TR(format, ...)      printk(format, ## __VA_ARGS__)
	#if (CONFIG_SENSOR_DEBUG)
	#define SENSOR_DG(format, ...)      printk(format, ## __VA_ARGS__)
	#else
	#define SENSOR_DG(format, ...)
	#endif
#else
	#define SENSOR_TR(format, ...)
#endif

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

struct reginfo
{
    u16 reg;
    u8 val;
};

/* init VGA 640*480 */
static struct reginfo sensor_init_data[] =
{
	{0x3078, 0x02},
	{0x304d, 0x45},
	{0x30a7, 0x5e},
	{0x3087, 0x16},
	{0x309C, 0x1a},
	{0x30a2, 0xe4},
	{0x30aa, 0x42},
	{0x30b0, 0xff},
	{0x30b1, 0xff},
	{0x30b2, 0x10},
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

	{0x0000 ,0x00},
};

/* 2048X1536 QXGA */
static struct reginfo sensor_qxga_preview[] =
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
	{0x3089, 0x80},
	{0x308a, 0x01},
	{0x308b, 0xe0},
	{0x304c, 0x83}, //85

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
static int sensor_set_flashLed(struct soc_camera_device *icd, int value);

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

#define COL_FMT(_name, _depth, _fourcc, _colorspace) \
	{ .name = _name, .depth = _depth, .fourcc = _fourcc, \
	.colorspace = _colorspace }

#define JPG_FMT(_name, _depth, _fourcc) \
	COL_FMT(_name, _depth, _fourcc, V4L2_COLORSPACE_JPEG)

static const struct soc_camera_data_format sensor_colour_formats[] = {
	JPG_FMT(SENSOR_NAME_STRING(YUYV), 16, V4L2_PIX_FMT_YUYV),
	JPG_FMT(SENSOR_NAME_STRING(YUYV), 16, V4L2_PIX_FMT_YUYV),
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
    unsigned char mirror;                                        /* HFLIP */
    unsigned char flip;                                          /* VFLIP */
    unsigned int winseqe_cur_addr;
	unsigned int pixfmt;
} sensor_info_priv_t;

struct sensor
{
    struct v4l2_subdev subdev;
    struct i2c_client *client;
    sensor_info_priv_t info_priv;
    int model;	/* V4L2_IDENT_OV* codes from v4l2-chip-ident.h */
};

static struct sensor* to_sensor(const struct i2c_client *client)
{
    return container_of(i2c_get_clientdata(client), struct sensor, subdev);
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

    while ((cnt--) && (err < 0)) {                       /* ddl@rock-chips.com :  Transfer again if transent is failed   */
        err = i2c_transfer(client->adapter, msg, 1);

        if (err >= 0) {
            return 0;
        } else {
            SENSOR_TR("\n %s write reg failed, try to write again! reg:0x%x val:0x%x ,err=%d\n",SENSOR_NAME_STRING(), reg, val,err);
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
    while ((cnt--) && (err < 0)) {                       /* ddl@rock-chips.com :  Transfer again if transent is failed   */
        err = i2c_transfer(client->adapter, msg, 2);

        if (err >= 0) {
            *val = buf[0];
            return 0;
        } else {
        	SENSOR_TR("\n %s read reg failed, try to read again! reg:0x%x \n",SENSOR_NAME_STRING(),*val);
            udelay(10);
        }
    }

    return err;
}

/* write a array of registers  */
static int sensor_write_array(struct i2c_client *client, struct reginfo *regarray)
{
    int err, cnt;
    int i = 0;

	cnt = 0;
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
                return -EPERM;
            }
        }
        i++;
    }
    return 0;
}

static int sensor_init(struct v4l2_subdev *sd, u32 val)
{
    struct i2c_client *client = sd->priv;
    struct soc_camera_device *icd = client->dev.platform_data;
    struct sensor *sensor = to_sensor(client);
	const struct v4l2_queryctrl *qctrl;
    char value;
    int ret,pid = 0;

    SENSOR_DG("\n%s..%s.. \n",SENSOR_NAME_STRING(),__FUNCTION__);
    

/* ddl@rock-chips.com : ov3640_powerOn have hardware reset */
#if 0
    /* soft reset */
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

    icd->user_width = SENSOR_INIT_WIDTH;
    icd->user_height = SENSOR_INIT_HEIGHT;
    sensor->info_priv.winseqe_cur_addr  = (int)SENSOR_INIT_WINSEQADR;
	sensor->info_priv.pixfmt = SENSOR_INIT_PIXFMT;

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
    sensor_set_focus();
    qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_FOCUS_ABSOLUTE);
	if (qctrl)
        sensor->info_priv.focus = qctrl->default_value;
	#endif

	#if CONFIG_SENSOR_Flash
	//sensor_set_flash();
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_FLASH);
	if (qctrl)
        sensor->info_priv.flash = qctrl->default_value;
    #endif

    SENSOR_DG("\n%s..%s.. icd->width = %d.. icd->height %d\n",SENSOR_NAME_STRING(),__FUNCTION__,icd->user_width,icd->user_height);

    return 0;
sensor_INIT_ERR:
    return ret;
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
    struct soc_camera_link *icl;


    if (pm_msg.event == PM_EVENT_SUSPEND)
    {
        SENSOR_DG("\n %s Enter Suspend.. \n", SENSOR_NAME_STRING());
        ret = sensor_write_array(client, sensor_power_down_sequence) ;
        if (ret != 0)
        {
            SENSOR_TR("\n %s..%s WriteReg Fail.. \n", SENSOR_NAME_STRING(),__FUNCTION__);
            return ret;
        }
        else
        {
            icl = to_soc_camera_link(icd);
            if (icl->power) {
                ret = icl->power(icd->pdev, 0);
                if (ret < 0) {
				    SENSOR_TR("\n %s suspend fail for turn on power!\n",__FUNCTION__);
                    return -EINVAL;
                }
            }
        }
    }
    else
    {
        SENSOR_TR("\n %s cann't suppout Suspend..\n",SENSOR_NAME_STRING());
        return -EINVAL;
    }
    return 0;
}

static int sensor_resume(struct soc_camera_device *icd)
{
    struct soc_camera_link *icl;
    int ret;

    icl = to_soc_camera_link(icd);
    if (icl->power) {
        ret = icl->power(icd->pdev, 1);
        if (ret < 0) {
			SENSOR_TR("\n %s resume fail for turn on power!\n",__FUNCTION__);
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

static int sensor_g_fmt(struct v4l2_subdev *sd, struct v4l2_format *f)
{
    struct i2c_client *client = sd->priv;
    struct soc_camera_device *icd = client->dev.platform_data;
    struct sensor *sensor = to_sensor(client);
    struct v4l2_pix_format *pix = &f->fmt.pix;

    pix->width		= icd->user_width;
    pix->height		= icd->user_height;
    pix->pixelformat	= sensor->info_priv.pixfmt;
    pix->field		= V4L2_FIELD_NONE;
    pix->colorspace		= V4L2_COLORSPACE_JPEG;

    return 0;
}

static int sensor_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client *client = sd->priv;
	struct soc_camera_device *icd = client->dev.platform_data;

	if (!enable) {
		sensor_set_flashLed(icd,0);
		return 0;
	}

	return 0;
}

static int sensor_s_fmt(struct v4l2_subdev *sd, struct v4l2_format *f)
{
    struct i2c_client *client = sd->priv;
	struct soc_camera_device *icd = client->dev.platform_data;
    struct sensor *sensor = to_sensor(client);
    struct v4l2_pix_format *pix = &f->fmt.pix;
    struct reginfo *winseqe_set_addr=NULL;
    int ret, set_w,set_h;
	int isCapture = 0; // 1--拍照序列 0--预览序列
	
	if (sensor->info_priv.pixfmt != pix->pixelformat) {
		switch (pix->pixelformat)
		{
			case V4L2_PIX_FMT_YUYV:
			{
				winseqe_set_addr = sensor_ClrFmt_YUYV;
				break;
			}
			case V4L2_PIX_FMT_UYVY:
			{
				winseqe_set_addr = sensor_ClrFmt_UYVY;
				break;
			}
			default:
				break;
		}
		if (winseqe_set_addr != NULL) {
            sensor_write_array(client, winseqe_set_addr);
			sensor->info_priv.pixfmt = pix->pixelformat;

			SENSOR_DG("%s Pixelformat(0x%x) set success!\n", SENSOR_NAME_STRING(),pix->pixelformat);
		} else {
			SENSOR_TR("%s Pixelformat(0x%x) is invalidate!\n", SENSOR_NAME_STRING(),pix->pixelformat);
		}
	}

    set_w = pix->width;
    set_h = pix->height;
	isCapture = pix->priv;
	
	if(isCapture)
	   sensor_set_flashLed(icd,1);
	
	SENSOR_DG("%s %c,%c,%c,%c\n", SENSOR_NAME_STRING(),
		((pix->pixelformat) & 0xff),((pix->pixelformat>>8) & 0xff),
		((pix->pixelformat>>16) & 0xff),((pix->pixelformat>>24) & 0xff));
	
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
    else if (sensor_qcif[isCapture][0].reg)
    {
		winseqe_set_addr = sensor_qcif[isCapture]; /* ddl@rock-chips.com : Sensor output smallest size if  isn't support app  */
        set_w = 176;
        set_h = 144;
    }
	else
	{
		SENSOR_TR("\n %s..%s Format is Invalidate. pix->width = %d.. pix->height = %d\n",SENSOR_NAME_STRING(),__FUNCTION__,pix->width,pix->height);
		return -EINVAL;
	}

    if ((int)winseqe_set_addr  != sensor->info_priv.winseqe_cur_addr)
    {
		//srt --if capture,then should write sensor_qxga[1] first
		
		if((winseqe_set_addr != sensor_qxga[isCapture]) && isCapture)
		{
			SENSOR_DG("%s  write sensor_qxga[1]\n", SENSOR_NAME_STRING());
			ret = sensor_write_array(client, sensor_qxga[isCapture]);
	        if (ret != 0)
	        {
	            SENSOR_TR("%s  write sensor_qxga[1] failed\n", SENSOR_NAME_STRING());
	            return ret;
	        }
		}
		
        ret = sensor_write_array(client, winseqe_set_addr);
        if (ret != 0)
        {
            SENSOR_TR("%s set format capability failed\n", SENSOR_NAME_STRING());
            return ret;
        }

        sensor->info_priv.winseqe_cur_addr  = (int)winseqe_set_addr;
        //mdelay(250);

        SENSOR_DG("\n%s..%s.. icd->width=%d..icd->height=%d..isCapture=%d\n",SENSOR_NAME_STRING(),__FUNCTION__,set_w,set_h,isCapture);
    }
    else
    {
        SENSOR_TR("\n %s .. Current Format is validate. icd->width=%d..icd->height=%d..isCapture=%d\n",SENSOR_NAME_STRING(),set_w,set_h,isCapture);
    }

    return 0;
}

static int sensor_try_fmt(struct v4l2_subdev *sd, struct v4l2_format *f)
{
    struct v4l2_pix_format *pix = &f->fmt.pix;
    bool bayer = pix->pixelformat == V4L2_PIX_FMT_UYVY ||
        pix->pixelformat == V4L2_PIX_FMT_YUYV;

    /*
    * With Bayer format enforce even side lengths, but let the user play
    * with the starting pixel
    */

    if (pix->height > SENSOR_MAX_HEIGHT)
        pix->height = SENSOR_MAX_HEIGHT;
    else if (pix->height < SENSOR_MIN_HEIGHT)
        pix->height = SENSOR_MIN_HEIGHT;
    else if (bayer)
        pix->height = ALIGN(pix->height, 2);

    if (pix->width > SENSOR_MAX_WIDTH)
        pix->width = SENSOR_MAX_WIDTH;
    else if (pix->width < SENSOR_MIN_WIDTH)
        pix->width = SENSOR_MIN_WIDTH;
    else if (bayer)
        pix->width = ALIGN(pix->width, 2);

    return 0;
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
 

static int sensor_set_flashLed(struct soc_camera_device *icd, int value)
{
#if CONFIG_SENSOR_Flash	
	struct soc_camera_link *icl = to_soc_camera_link(icd);
	struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
    struct sensor *sensor = to_sensor(client);
	int ret;
	
	SENSOR_DG("sensor_set_flashLed - %d,%d\n", sensor->info_priv.flash,value);
	
	if(icl->flash_led == NULL)
	{
		SENSOR_TR("icl->flash_led == NULL\n ");
		return -1;
	}
		
	switch(sensor->info_priv.flash)
	{
		case 0: // off
		{
			ret = icl->flash_led(icd->pdev, 0);
			break;
		}
		case 1: //auto
		{
			ret = icl->flash_led(icd->pdev,value);
			break;
		}
		case 2: // on
		{
			ret = icl->flash_led(icd->pdev,1);
			break;
		}
		default: // off
		{
			ret = icl->flash_led(icd->pdev,0);
			break;
		}

	}

	if(ret)
	{
		SENSOR_TR("icl->flash_led failed\n ");
	}
#endif

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
static int sensor_g_control(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
    struct i2c_client *client = sd->priv;
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
    struct i2c_client *client = sd->priv;
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
                sensor->info_priv.flash = ext_ctrl->value;
				sensor_set_flashLed(icd,0);
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

    /* soft reset */
    ret = sensor_write(client, 0x3012, 0x80);
    if (ret != 0)
    {
        SENSOR_TR("soft reset %s failed\n",SENSOR_NAME_STRING());
        return -ENODEV;
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

    icd->formats = sensor_colour_formats;
    icd->num_formats = ARRAY_SIZE(sensor_colour_formats);

    return 0;

sensor_video_probe_err:

    return ret;
}

static struct v4l2_subdev_core_ops sensor_subdev_core_ops = {
	.init		= sensor_init,
	.g_ctrl		= sensor_g_control,
	.s_ctrl		= sensor_s_control,
	.g_ext_ctrls          = sensor_g_ext_controls,
	.s_ext_ctrls          = sensor_s_ext_controls,
	.g_chip_ident	= sensor_g_chip_ident,
};

static struct v4l2_subdev_video_ops sensor_subdev_video_ops = {
	.s_stream	= sensor_s_stream,
	.s_fmt		= sensor_s_fmt,
	.g_fmt		= sensor_g_fmt,
	.try_fmt	= sensor_try_fmt,
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
    icd->y_skip_top		= 0;

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
MODULE_AUTHOR("srt <kernel@rock-chips>");
MODULE_LICENSE("GPL");

