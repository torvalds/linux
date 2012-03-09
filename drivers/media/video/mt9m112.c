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
#include <linux/hardirq.h>
#include <linux/miscdevice.h>
#include <media/v4l2-common.h>
#include <media/v4l2-chip-ident.h>
#include <media/soc_camera.h>
#include <mach/rk_camera.h>
#include "mt9m112.h"

static int debug;
module_param(debug, int, S_IRUGO|S_IWUSR);

#define dprintk(level, fmt, arg...) do {			\
	if (debug >= level) 					\
	printk(KERN_WARNING fmt , ## arg); } while (0)

#define SENSOR_TR(format, ...) printk(KERN_ERR format, ## __VA_ARGS__)
#define SENSOR_DG(format, ...) dprintk(0, format, ## __VA_ARGS__)

#define _CONS(a,b) a##b
#define CONS(a,b) _CONS(a,b)

#define __STR(x) #x
#define _STR(x) __STR(x)
#define STR(x) _STR(x)

#define MIN(x,y)   ((x<y) ? x: y)
#define MAX(x,y)    ((x>y) ? x: y)

/* Sensor Driver Configuration */
#define SENSOR_NAME mt9m112
#define SENSOR_V4L2_IDENT V4L2_IDENT_MT9M112
#define SENSOR_ID SEQUENCE_END
#define SENSOR_ID_REG SEQUENCE_END
#define SENSOR_RESET_REG SEQUENCE_END
#define SENSOR_RESET_VAL SEQUENCE_END
#define SENSOR_RESET_REG_LEN  WORD_LEN
#define SENSOR_MIN_WIDTH    640
#define SENSOR_MIN_HEIGHT   480
#define SENSOR_MAX_WIDTH    1280
#define SENSOR_MAX_HEIGHT   1024
#define SENSOR_INIT_WIDTH	640			/* Sensor pixel size for sensor_init_data array */
#define SENSOR_INIT_HEIGHT  480
#define SENSOR_INIT_WINSEQADR sensor_vga
#define SENSOR_INIT_PIXFMT V4L2_PIX_FMT_UYVY

#define CONFIG_SENSOR_WhiteBalance	0
#define CONFIG_SENSOR_Brightness	0
#define CONFIG_SENSOR_Contrast      0
#define CONFIG_SENSOR_Saturation    0
#define CONFIG_SENSOR_Effect        0
#define CONFIG_SENSOR_Scene         0
#define CONFIG_SENSOR_DigitalZoom   0
#define CONFIG_SENSOR_Exposure      0
#define CONFIG_SENSOR_Flash         0
#define CONFIG_SENSOR_Mirror        0
#define CONFIG_SENSOR_Flip          0
#define CONFIG_SENSOR_Focus         0


#define CONFIG_SENSOR_I2C_SPEED     100000       /* Hz */
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

#if CONFIG_SENSOR_Focus
#define SENSOR_AF_MODE_INFINITY    0
#define SENSOR_AF_MODE_MACRO       1
#define SENSOR_AF_MODE_FIXED       2
#define SENSOR_AF_MODE_AUTO        3
#define SENSOR_AF_MODE_CONTINUOUS  4
#define SENSOR_AF_MODE_CLOSE       5
#endif

/* init 640X480 VGA */
static struct reginfo sensor_init_data[] =
{
	{0xf0, 0x0000},
    {0x35, 0x0022},
    //0x20, 0x0103,  // mirror
    {0x0c, 0x0000},
    {0x21, 0x8400},

    {0x05,0x022e},
   // 0x06,0x0300,

    {0xf0, 0x0001},
    {0x06,0x708e},
    {0x05,0x000e},

   { 0xf0, 0x0002},
    {0xCB, 0x0001},		  // PROGRAM_ADVANCE
    {0xCC, 0x0004},		  // PROGRAM_SELECT
    {0xD2, 0x0000},		  // DEFAULT_CONFIG
    {0x5B, 0x0001},

    {0xf0, 0x0001},

    //[Lens Correction 05/16/08 11:19:07]
    {0x80, 0x000A }, //LENS_CORRECT_CONTROL
    {0x81, 0xED0F }, //LENS_ADJ_VERT_RED_0
    {0x82, 0xF8F1 }, //LENS_ADJ_VERT_RED_1_2
    {0x83, 0x00FD }, //LENS_ADJ_VERT_RED_3_4
    {0x84, 0xEF0D }, //LENS_ADJ_VERT_GREEN_0
    {0x85, 0xF9F3 }, //LENS_ADJ_VERT_GREEN_1_2
    {0x86, 0x00FD }, //LENS_ADJ_VERT_GREEN_3_4
    {0x87, 0xF30C }, //LENS_ADJ_VERT_BLUE_0
    {0x88, 0xF9F3 }, //LENS_ADJ_VERT_BLUE_1_2
    {0x89, 0x00FD }, //LENS_ADJ_VERT_BLUE_3_4
    {0x8A, 0xE616 }, //LENS_ADJ_HORIZ_RED_0
    {0x8B, 0xF5F0 }, //LENS_ADJ_HORIZ_RED_1_2
    {0x8C, 0xFDF9 }, //LENS_ADJ_HORIZ_RED_3_4
    {0x8D, 0x0002 }, //LENS_ADJ_HORIZ_RED_5
    {0x8E, 0xE714 }, //LENS_ADJ_HORIZ_GREEN_0
    {0x8F, 0xF5F1 }, //LENS_ADJ_HORIZ_GREEN_1_2
    {0x90, 0xFEFA }, //LENS_ADJ_HORIZ_GREEN_3_4
    {0x91, 0x0001} , //LENS_ADJ_HORIZ_GREEN_5
    {0x92, 0xE913} , //LENS_ADJ_HORIZ_BLUE_0
    {0x93, 0xF7F2 }, //LENS_ADJ_HORIZ_BLUE_1_2
    {0x94, 0xFEFB }, //LENS_ADJ_HORIZ_BLUE_3_4
    {0x95, 0x0002} , //LENS_ADJ_HORIZ_BLUE_5
    {0xB6, 0x0904} , //LENS_ADJ_VERT_RED_5_6
    {0xB7, 0x1E0C }, //LENS_ADJ_VERT_RED_7_8
    {0xB8, 0x0904 }, //LENS_ADJ_VERT_GREEN_5_6
    {0xB9, 0x1B0A} , //LENS_ADJ_VERT_GREEN_7_8
    {0xBA, 0x0704} , //LENS_ADJ_VERT_BLUE_5_6
    {0xBB, 0x1A0A} , //LENS_ADJ_VERT_BLUE_7_8
    {0xBC, 0x0906 }, //LENS_ADJ_HORIZ_RED_6_7
    {0xBD, 0x1410} , //LENS_ADJ_HORIZ_RED_8_9
    {0xBE, 0x0026 }, //LENS_ADJ_HORIZ_RED_10
    {0xBF, 0x0806}, //LENS_ADJ_HORIZ_GREEN_6_7
    {0xC0, 0x110E} , //LENS_ADJ_HORIZ_GREEN_8_9
    {0xC1, 0x0024} , //LENS_ADJ_HORIZ_GREEN_10
    {0xC2, 0x0A05} , //LENS_ADJ_HORIZ_BLUE_6_7
    {0xC3, 0x100F} , //LENS_ADJ_HORIZ_BLUE_8_9
    {0xC4, 0x0027} , //LENS_ADJ_HORIZ_BLUE_10
    {0x06, 0x640e}, //LENS_CORRECTION

    {0x53,0x0602},
    {0x54,0x4416},
    {0x55,0xb68a},
    {0x56,0xe0cf},
    {0x57,0xf7e0},
    {0x58,0xff00},

    //输出窗口大小
    {0xA7, 640},
    {0xaa, 512},

    //背光初始化
    {0xf0,	0x0002},
    {0x37,   0x0080},//0x0300,
    {0x2f,   0x9100},
    {0x2e,   0x0c44},//0x0c30, //
    //0x39, 0x03D2,  // AE Line size Context A d:6A6
    //0x3A, 0x03D2,  // AE Line size Context B d:6A6
    //0x3B, 0x055B,  // AE shutter delay limit Context A
    //0x3C, 0x055B,  // AE shutter delay limit Context B
    {0x57, 0x2CA},  // Context A Flicker full frame time (60Hz)
    {0x58, 0x2CA},		//0x01E9,  // Context A Flicker full frame time (50Hz)
    {0x59, 0x2CA},  // Context B Flicker full frame time (60Hz)
    {0x5A, 0x2CA},  //0x01E9,  // Context B Flicker full frame time (50Hz)
    {0x5C, 0x0E0A},  // 60Hz Flicker Search Range
    {0x5D, 0x120E},  // 50Hz Flicker Search Range
    {0x64, 0x5E1C},  // Flicker parameter
    {0x5B,   0x0001},

	{SEQUENCE_END, 0x00}
};

/* 720p 15fps @ 1280x720 */

static struct reginfo sensor_720p[]=
{
	{SEQUENCE_END, 0x00}
};

/* 	1080p, 0x15fps, 0xyuv @1920x1080 */

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
	{SEQUENCE_PROPERTY,SEQUENCE_CAPTURE},
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
	{SEQUENCE_END, 0x00}
};

/* 640X480 VGA */
static struct reginfo sensor_vga[] =
{
	{0xf0, 0x0000},
    {0x35, 0x0022},
    //0x20, 0x0103,  // mirror
    {0x0c, 0x0000},
    {0x21, 0x8400},

    {0x05,0x022e},
   // 0x06,0x0300,

    {0xf0, 0x0001},
    {0x06,0x708e},
    {0x05,0x000e},

   { 0xf0, 0x0002},
    {0xCB, 0x0001},		  // PROGRAM_ADVANCE
    {0xCC, 0x0004},		  // PROGRAM_SELECT
    {0xD2, 0x0000},		  // DEFAULT_CONFIG
    {0x5B, 0x0001},

    {0xf0, 0x0001},

    //[Lens Correction 05/16/08 11:19:07]
    {0x80, 0x000A }, //LENS_CORRECT_CONTROL
    {0x81, 0xED0F }, //LENS_ADJ_VERT_RED_0
    {0x82, 0xF8F1 }, //LENS_ADJ_VERT_RED_1_2
    {0x83, 0x00FD }, //LENS_ADJ_VERT_RED_3_4
    {0x84, 0xEF0D }, //LENS_ADJ_VERT_GREEN_0
    {0x85, 0xF9F3 }, //LENS_ADJ_VERT_GREEN_1_2
    {0x86, 0x00FD }, //LENS_ADJ_VERT_GREEN_3_4
    {0x87, 0xF30C }, //LENS_ADJ_VERT_BLUE_0
    {0x88, 0xF9F3 }, //LENS_ADJ_VERT_BLUE_1_2
    {0x89, 0x00FD }, //LENS_ADJ_VERT_BLUE_3_4
    {0x8A, 0xE616 }, //LENS_ADJ_HORIZ_RED_0
    {0x8B, 0xF5F0 }, //LENS_ADJ_HORIZ_RED_1_2
    {0x8C, 0xFDF9 }, //LENS_ADJ_HORIZ_RED_3_4
    {0x8D, 0x0002 }, //LENS_ADJ_HORIZ_RED_5
    {0x8E, 0xE714 }, //LENS_ADJ_HORIZ_GREEN_0
    {0x8F, 0xF5F1 }, //LENS_ADJ_HORIZ_GREEN_1_2
    {0x90, 0xFEFA }, //LENS_ADJ_HORIZ_GREEN_3_4
    {0x91, 0x0001} , //LENS_ADJ_HORIZ_GREEN_5
    {0x92, 0xE913} , //LENS_ADJ_HORIZ_BLUE_0
    {0x93, 0xF7F2 }, //LENS_ADJ_HORIZ_BLUE_1_2
    {0x94, 0xFEFB }, //LENS_ADJ_HORIZ_BLUE_3_4
    {0x95, 0x0002} , //LENS_ADJ_HORIZ_BLUE_5
    {0xB6, 0x0904} , //LENS_ADJ_VERT_RED_5_6
    {0xB7, 0x1E0C }, //LENS_ADJ_VERT_RED_7_8
    {0xB8, 0x0904 }, //LENS_ADJ_VERT_GREEN_5_6
    {0xB9, 0x1B0A} , //LENS_ADJ_VERT_GREEN_7_8
    {0xBA, 0x0704} , //LENS_ADJ_VERT_BLUE_5_6
    {0xBB, 0x1A0A} , //LENS_ADJ_VERT_BLUE_7_8
    {0xBC, 0x0906 }, //LENS_ADJ_HORIZ_RED_6_7
    {0xBD, 0x1410} , //LENS_ADJ_HORIZ_RED_8_9
    {0xBE, 0x0026 }, //LENS_ADJ_HORIZ_RED_10
    {0xBF, 0x0806}, //LENS_ADJ_HORIZ_GREEN_6_7
    {0xC0, 0x110E} , //LENS_ADJ_HORIZ_GREEN_8_9
    {0xC1, 0x0024} , //LENS_ADJ_HORIZ_GREEN_10
    {0xC2, 0x0A05} , //LENS_ADJ_HORIZ_BLUE_6_7
    {0xC3, 0x100F} , //LENS_ADJ_HORIZ_BLUE_8_9
    {0xC4, 0x0027} , //LENS_ADJ_HORIZ_BLUE_10
    {0x06, 0x640e}, //LENS_CORRECTION

    {0x53,0x0602},
    {0x54,0x4416},
    {0x55,0xb68a},
    {0x56,0xe0cf},
    {0x57,0xf7e0},
    {0x58,0xff00},

    //输出窗口大小
    {0xA7, 640},
    {0xaa, 512},

    //背光初始化
    {0xf0,	0x0002},
    {0x37,   0x0080},//0x0300,
    {0x2f,   0x9100},
    {0x2e,   0x0c44},//0x0c30, //
    //0x39, 0x03D2,  // AE Line size Context A d:6A6
    //0x3A, 0x03D2,  // AE Line size Context B d:6A6
    //0x3B, 0x055B,  // AE shutter delay limit Context A
    //0x3C, 0x055B,  // AE shutter delay limit Context B
    {0x57, 0x2CA},  // Context A Flicker full frame time (60Hz)
    {0x58, 0x2CA},		//0x01E9,  // Context A Flicker full frame time (50Hz)
    {0x59, 0x2CA},  // Context B Flicker full frame time (60Hz)
    {0x5A, 0x2CA},  //0x01E9,  // Context B Flicker full frame time (50Hz)
    {0x5C, 0x0E0A},  // 60Hz Flicker Search Range
    {0x5D, 0x120E},  // 50Hz Flicker Search Range
    {0x64, 0x5E1C},  // Flicker parameter
    {0x5B,   0x0001},
    {SEQUENCE_END, 0x00}
};

/* 352X288 CIF */
static struct reginfo sensor_cif[] =
{
	{SEQUENCE_END, 0x00}
};

/* 320*240 QVGA */
static  struct reginfo sensor_qvga[] =
{
	{SEQUENCE_END, 0x00}
};

/* 176X144 QCIF*/
static struct reginfo sensor_qcif[] =
{
	{SEQUENCE_END, 0x00}
};
#if 0
/* 160X120 QQVGA*/
static struct reginfo ov2655_qqvga[] =
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

    {0x0, 0x0},
};



static  struct reginfo ov2655_Sharpness_auto[] =
{
    {0x3306, 0x00},
};

static  struct reginfo ov2655_Sharpness1[] =
{
    {0x3306, 0x08},
    {0x3371, 0x00},
};

static  struct reginfo ov2655_Sharpness2[][3] =
{
    //Sharpness 2
    {0x3306, 0x08},
    {0x3371, 0x01},
};

static  struct reginfo ov2655_Sharpness3[] =
{
    //default
    {0x3306, 0x08},
    {0x332d, 0x02},
};
static  struct reginfo ov2655_Sharpness4[]=
{
    //Sharpness 4
    {0x3306, 0x08},
    {0x332d, 0x03},
};

static  struct reginfo ov2655_Sharpness5[] =
{
    //Sharpness 5
    {0x3306, 0x08},
    {0x332d, 0x04},
};
#endif
static  struct reginfo sensor_Preview2Capture[]=
{
	//capture2preview
    {0xf0, 0x0002},
    {0xCB, 0x0001},          // PROGRAM_ADVANCE
    {0xCC, 0x0004},          // PROGRAM_SELECT
    {0xD2, 0x007f},          // DEFAULT_CONFIG
	{SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Capture2Preview[]=
{
	//snap2preview
    {0xf0, 0x0002},
    {0xCB, 0x0001},		  // PROGRAM_ADVANCE
    {0xCC, 0x0004},		  // PROGRAM_SELECT
    {0xD2, 0x0000},		  // DEFAULT_CONFIG
	{SEQUENCE_END, 0x00}
};
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
	//Auto
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

static struct reginfo sensor_Effect_Solarize[] =
{
	{SEQUENCE_END, 0x00}
};
static struct reginfo *sensor_EffectSeqe[] = {sensor_Effect_Normal, sensor_Effect_Negative,sensor_Effect_Sepia,
    sensor_Effect_Solarize,NULL,
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
	{SEQUENCE_END, 0x00}
};

static struct reginfo sensor_Zoom1[] =
{
	{SEQUENCE_END, 0x00}
};

static struct reginfo sensor_Zoom2[] =
{
	{SEQUENCE_END, 0x00}
};


static struct reginfo sensor_Zoom3[] =
{
	{SEQUENCE_END, 0x00}
};
static struct reginfo *sensor_ZoomSeqe[] = {sensor_Zoom0, sensor_Zoom1, sensor_Zoom2, sensor_Zoom3, NULL};
#endif
static const struct v4l2_querymenu sensor_menus[] =
{
	#if CONFIG_SENSOR_WhiteBalance
    { .id = V4L2_CID_DO_WHITE_BALANCE,  .index = 0,  .name = "auto",  .reserved = 0, }, {  .id = V4L2_CID_DO_WHITE_BALANCE,  .index = 1, .name = "incandescent",  .reserved = 0,},
    { .id = V4L2_CID_DO_WHITE_BALANCE,  .index = 2,  .name = "fluorescent", .reserved = 0,}, {  .id = V4L2_CID_DO_WHITE_BALANCE, .index = 3,  .name = "daylight", .reserved = 0,},
    { .id = V4L2_CID_DO_WHITE_BALANCE,  .index = 4,  .name = "cloudy-daylight", .reserved = 0,},
    #endif

	#if CONFIG_SENSOR_Effect
    { .id = V4L2_CID_EFFECT,  .index = 0,  .name = "none",  .reserved = 0, }, {  .id = V4L2_CID_EFFECT,  .index = 1, .name = "negative",  .reserved = 0,},
    { .id = V4L2_CID_EFFECT,  .index = 2,  .name = "sepia", .reserved = 0,}, {  .id = V4L2_CID_EFFECT, .index = 3,  .name = "solarize", .reserved = 0,},
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
        .maximum	= 3,
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
	{
        .id		= V4L2_CID_FOCUS_AUTO,
        .type		= V4L2_CTRL_TYPE_BOOLEAN,
        .name		= "Focus Control",
        .minimum	= 0,
        .maximum	= 1,
        .step		= 1,
        .default_value = 0,
    },{
        .id		= V4L2_CID_FOCUS_CONTINUOUS,
        .type		= V4L2_CTRL_TYPE_BOOLEAN,
        .name		= "Focus Control",
        .minimum	= 0,
        .maximum	= 1,
        .step		= 1,
        .default_value = 0,
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

#define COL_FMT(_name, _depth, _fourcc, _colorspace) \
	{ .name = _name, .depth = _depth, .fourcc = _fourcc, \
	.colorspace = _colorspace }

#define JPG_FMT(_name, _depth, _fourcc) \
	COL_FMT(_name, _depth, _fourcc, V4L2_COLORSPACE_JPEG)

static const struct soc_camera_data_format sensor_colour_formats[] = {
	JPG_FMT(SENSOR_NAME_STRING(UYVY), 16, V4L2_PIX_FMT_UYVY),
	JPG_FMT(SENSOR_NAME_STRING(YUYV), 16, V4L2_PIX_FMT_YUYV),
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
	unsigned int pixfmt;
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
	struct rkcamera_platform_data *sensor_io_request;
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
#endif
	return 0;
sensor_task_lock_err:
	return -1;
}

/* sensor register write */
static int sensor_write(struct i2c_client *client, struct reginfo *reg_info)
{
    int err=0,cnt;
    u8 buf[3];
    struct i2c_msg msg[1];

	switch (reg_info->reg)
	{
		case SEQUENCE_WAIT_MS:
		{
			if (in_atomic())
				mdelay(reg_info->val);
			else
				msleep(reg_info->val);
			break;
		}

		case SEQUENCE_WAIT_US:
		{
			udelay(reg_info->val);
			break;
		}

		case SEQUENCE_PROPERTY:
		{
			break;
		}
		default:
		{
		    buf[0] = reg_info->reg & 0xFF;
			buf[1] = reg_info->val >> 8;
			buf[2] = reg_info->val & 0xFF;
			msg->len = 3;
		    msg->addr = client->addr;
		    msg->flags = client->flags;
		    msg->buf = buf;
		    msg->scl_rate = CONFIG_SENSOR_I2C_SPEED;         /* ddl@rock-chips.com : 100kHz */
		    msg->read_type = 0;               /* fpga i2c:0==I2C_NORMAL : direct use number not enum for don't want include spi_fpga.h */

		    cnt = 3;
		    err = -EAGAIN;

		    while ((cnt-- > 0) && (err < 0)) {                       /* ddl@rock-chips.com :  Transfer again if transent is failed   */
		        err = i2c_transfer(client->adapter, msg, 1);

		        if (err >= 0) {
		            return 0;
		        } else {
		            SENSOR_TR("\n %s write reg(0x%x, val:0x%x) failed, try to write again!\n",SENSOR_NAME_STRING(),reg_info->reg, reg_info->val);
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
    int err,cnt;
    u8 buf[1];
    struct i2c_msg msg[2];

    buf[0] = reg & 0xFF;

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
    while (regarray[i].reg != SEQUENCE_END) {
        err = sensor_write(client, &regarray[i]);
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
static struct reginfo sensor_af_init0[] =
{
	{SEQUENCE_END, 0x00}
};
static struct reginfo sensor_af_init1[] =
{
	{SEQUENCE_END, 0x00}
};


static struct reginfo sensor_af_trigger[] =
{
	{SEQUENCE_END, 0x00}
};
static int sensor_af_single(struct i2c_client *client)
{
	int ret = 0;

	ret = sensor_write_array(client, sensor_af_trigger);
	if (ret<0)
		SENSOR_TR("%s sensor auto focus trigger fail!!\n",SENSOR_NAME_STRING());
	else
		SENSOR_DG("%s sensor auto focus trigger success!\n",SENSOR_NAME_STRING());
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

	ret = sensor_write_array(client, sensor_af_init0);
	if (ret<0) {
		SENSOR_TR("%s sensor auto focus init_0 fail!!",SENSOR_NAME_STRING());
	} else {
		if (sensor_af_zoneupdate(client) == 0) {
			ret = sensor_write_array(client, sensor_af_init1);
			if (ret<0) {
				SENSOR_TR("%s sensor auto focus init_1 fail!!",SENSOR_NAME_STRING());
			}
		}
	}

	return ret;
}
#endif

static int sensor_ioctrl(struct soc_camera_device *icd,enum rksensor_power_cmd cmd, int on)
{
	struct soc_camera_link *icl = to_soc_camera_link(icd);
	int ret = 0;


	switch (cmd)
	{
		case Sensor_PowerDown:
		{
			if (icl->powerdown) {
				ret = icl->powerdown(icd->pdev, on);
				if (ret == RK_CAM_IO_SUCCESS) {
					if (on == 0) {
						mdelay(2);
						if (icl->reset)
							icl->reset(icd->pdev);
					}
				} else if (ret == RK_CAM_EIO_REQUESTFAIL) {
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
			SENSOR_TR("%s power cmd(0x%x) is unknown!",SENSOR_NAME_STRING(),cmd);
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
    int ret,pid = 0;

    SENSOR_DG("\n%s..%s.. \n",SENSOR_NAME_STRING(),__FUNCTION__);

	if (sensor_ioctrl(icd, Sensor_PowerDown, 0) < 0) {
		ret = -ENODEV;
		goto sensor_INIT_ERR;
	}

    /* soft reset */
	if (sensor_task_lock(client,1)<0)
		goto sensor_INIT_ERR;

#if (SENSOR_RESET_REG != SEQUENCE_END)
    struct reginfo reg_info;
	reg_info.reg = SENSOR_RESET_REG;
	reg_info.val = SENSOR_RESET_VAL;
	reg_info.reg_len = SENSOR_RESET_REG_LEN;
    ret = sensor_write(client, &reg_info);
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
    sensor->info_priv.preview_w = SENSOR_INIT_WIDTH;
    sensor->info_priv.preview_h = SENSOR_INIT_HEIGHT;
    sensor->info_priv.capture_w = SENSOR_MAX_WIDTH;
    sensor->info_priv.capture_h = SENSOR_MAX_HEIGHT;
    sensor->info_priv.winseqe_cur_addr  = SENSOR_INIT_WINSEQADR;
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
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_FOCUS_ABSOLUTE);
	if (qctrl)
        sensor->info_priv.focus = qctrl->default_value;
	#if CONFIG_SENSOR_Focus
	if (sensor_af_init(client) < 0) {
		sensor->info_priv.funmodule_state &= ~SENSOR_AF_IS_OK;
		SENSOR_TR("%s auto focus module init is fail!\n",SENSOR_NAME_STRING());
	} else {
		sensor->info_priv.funmodule_state |= SENSOR_AF_IS_OK;
		SENSOR_DG("%s auto focus module init is success!\n",SENSOR_NAME_STRING());
	}
	#endif
	#if CONFIG_SENSOR_Flash
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
static bool sensor_fmt_capturechk(struct v4l2_subdev *sd, struct v4l2_format *f)
{
    bool ret = false;

	if ((f->fmt.pix.width == 1024) && (f->fmt.pix.height == 768)) {
		ret = true;
	} else if ((f->fmt.pix.width == 1280) && (f->fmt.pix.height == 1024)) {
		ret = true;
	} else if ((f->fmt.pix.width == 1600) && (f->fmt.pix.height == 1200)) {
		ret = true;
	} else if ((f->fmt.pix.width == 2048) && (f->fmt.pix.height == 1536)) {
		ret = true;
	} else if ((f->fmt.pix.width == 2592) && (f->fmt.pix.height == 1944)) {
		ret = true;
	}

	if (ret == true)
		SENSOR_DG("%s %dx%d is capture format\n", __FUNCTION__, f->fmt.pix.width, f->fmt.pix.height);
	return ret;
}

static bool sensor_fmt_videochk(struct v4l2_subdev *sd, struct v4l2_format *f)
{
    bool ret = false;

	if ((f->fmt.pix.width == 1280) && (f->fmt.pix.height == 720)) {
		ret = true;
	} else if ((f->fmt.pix.width == 1920) && (f->fmt.pix.height == 1080)) {
		ret = true;
	}

	if (ret == true)
		SENSOR_DG("%s %dx%d is video format\n", __FUNCTION__, f->fmt.pix.width, f->fmt.pix.height);
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
    } else if (((set_w <= 800) && (set_h <= 600)) && (sensor_svga[0].reg!=SEQUENCE_END)) {
        winseqe_set_addr = sensor_svga;
        *ret_w = 800;
        *ret_h = 600;
    } else if (((set_w <= 1024) && (set_h <= 768)) && (sensor_xga[0].reg!=SEQUENCE_END)) {
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
	} else if (((set_w <= 1920) && (set_h <= 1080)) && (sensor_1080p[0].reg!=SEQUENCE_END)) {
        winseqe_set_addr = sensor_1080p;
        *ret_w = 1920;
        *ret_h = 1080;
    } else if (((set_w <= 2048) && (set_h <= 1536)) && (sensor_qxga[0].reg!=SEQUENCE_END)) {
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

static int sensor_s_fmt(struct v4l2_subdev *sd, struct v4l2_format *f)
{
    struct i2c_client *client = sd->priv;
    struct sensor *sensor = to_sensor(client);
    struct v4l2_pix_format *pix = &f->fmt.pix;
    struct reginfo *winseqe_set_addr=NULL;
    int ret = 0, set_w,set_h;

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

	winseqe_set_addr = sensor_fmt_catch(set_w, set_h, &set_w, &set_h);

    if ((winseqe_set_addr  != sensor->info_priv.winseqe_cur_addr) && winseqe_set_addr) {
        ret |= sensor_write_array(client, winseqe_set_addr);
        if (ret != 0) {
            SENSOR_TR("%s set format capability failed\n", SENSOR_NAME_STRING());
            goto sensor_s_fmt_end;
        }
        sensor->info_priv.winseqe_cur_addr  = winseqe_set_addr;
		if ((winseqe_set_addr[0].reg==SEQUENCE_PROPERTY) && (winseqe_set_addr[0].val==SEQUENCE_CAPTURE)) {
        	SENSOR_DG("\n%s..%s..Capture icd->width = %d.. icd->height %d\n",SENSOR_NAME_STRING(),__FUNCTION__,set_w,set_h);
		} else {
			SENSOR_DG("\n%s..%s..Video icd->width = %d.. icd->height %d\n",SENSOR_NAME_STRING(),__FUNCTION__,set_w,set_h);
			sensor->info_priv.preview_w = pix->width;
			sensor->info_priv.preview_h = pix->height;
		}
    }

	if (winseqe_set_addr && (winseqe_set_addr[0].reg==SEQUENCE_PROPERTY) && (winseqe_set_addr[0].val==SEQUENCE_CAPTURE)) {
		ret |= sensor_write_array(client, sensor_Preview2Capture);
		if (ret != 0) {
        	SENSOR_TR("%s Preview 2 Capture failed\n", SENSOR_NAME_STRING());
        	goto sensor_s_fmt_end;
    	}
		sensor->info_priv.capture_w = set_w;
		sensor->info_priv.capture_h = set_h;
		sensor->info_priv.snap2preview = true;
	} else if (sensor->info_priv.snap2preview == true) {
		if (winseqe_set_addr || ((sensor->info_priv.preview_w == pix->width) && (sensor->info_priv.preview_h == pix->height))) {
			ret |= sensor_write_array(client, sensor_Capture2Preview);
			if (ret != 0) {
	        	SENSOR_TR("%s Capture 2 Preview failed\n", SENSOR_NAME_STRING());
	        	goto sensor_s_fmt_end;
	    	}
			sensor->info_priv.preview_w = pix->width;
			sensor->info_priv.preview_h = pix->height;
			sensor->info_priv.snap2preview = false;
		} else {
			SENSOR_TR("\n %s..%s Format is Invalidate. pix->width = %d.. pix->height = %d\n",SENSOR_NAME_STRING(),__FUNCTION__,pix->width,pix->height);
		}
	}

	pix->width = set_w;
	pix->height = set_h;
sensor_s_fmt_end:
    return ret;
}

static int sensor_try_fmt(struct v4l2_subdev *sd, struct v4l2_format *f)
{
	int ret = 0;
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

	if (sensor_fmt_catch(pix->width, pix->height, &pix->width, &pix->height) == NULL) {
		pix->width = 0;
		pix->height = 0;
	}

    return ret;
}

 static int sensor_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *id)
{
    struct i2c_client *client = sd->priv;

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
	int ret = 0;

	qctrl_info = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_FOCUS_ABSOLUTE);
	if (!qctrl_info)
		return -EINVAL;

	if ((sensor->info_priv.funmodule_state & SENSOR_AF_IS_OK) && (sensor->info_priv.affm_reinit == 0)) {
		if ((value >= qctrl_info->minimum) && (value <= qctrl_info->maximum)) {

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
	int ret = 0;

	qctrl_info = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_FOCUS_RELATIVE);
	if (!qctrl_info)
		return -EINVAL;

	if ((sensor->info_priv.funmodule_state & SENSOR_AF_IS_OK) && (sensor->info_priv.affm_reinit == 0)) {
		if ((value >= qctrl_info->minimum) && (value <= qctrl_info->maximum)) {

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
                return -EINVAL;
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
			}
#endif
#if CONFIG_SENSOR_Flash
        case V4L2_CID_FLASH:
            {
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

	if (enable == 1) {
		sensor->info_priv.enable = 1;
	} else if (enable == 0) {
		sensor->info_priv.enable = 0;
	}

sensor_s_stream_end:
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
    struct reginfo reg_info;
	reg_info.reg = SENSOR_RESET_REG;
	reg_info.val = SENSOR_RESET_VAL;
	reg_info.reg_len = SENSOR_RESET_REG_LEN;
    ret = sensor_write(client, &reg_info);
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

    icd->formats = sensor_colour_formats;
    icd->num_formats = ARRAY_SIZE(sensor_colour_formats);

    return 0;

sensor_video_probe_err:

    return ret;
}
static long sensor_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct i2c_client *client = sd->priv;
    struct sensor *sensor = to_sensor(client);

	SENSOR_DG("\n%s..%s..cmd:%x \n",SENSOR_NAME_STRING(),__FUNCTION__,cmd);
	switch (cmd)
	{
		case RK_CAM_SUBDEV_DEACTIVATE:
		{
			sensor_deactivate(client);
			break;
		}
		case RK_CAM_SUBDEV_IOREQUEST:
		{
			sensor->sensor_io_request = (struct rkcamera_platform_data*)arg;
			break;
		}
		default:
		{
			SENSOR_TR("%s %s cmd(0x%x) is unknown !\n",SENSOR_NAME_STRING(),__FUNCTION__,cmd);
			break;
		}
	}

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
	.s_fmt		= sensor_s_fmt,
	.g_fmt		= sensor_g_fmt,
	.try_fmt	= sensor_try_fmt,
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
    icd->y_skip_top		= 0;
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


