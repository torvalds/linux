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
#include <mach/rk29_camera.h>
#include <mach/gpio.h>
#include <linux/delay.h>
#include "s5k5ca.h"

#define CURRENT_8MA

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
#define SENSOR_NAME RK29_CAM_SENSOR_S5K5CA
#define SENSOR_V4L2_IDENT V4L2_IDENT_S5K5CA 
#define SENSOR_ID 0x05ca
#define SENSOR_ID_REG SEQUENCE_END
#define SENSOR_RESET_REG SEQUENCE_END
#define SENSOR_RESET_VAL    0x00
#define SENSOR_MIN_WIDTH    640
#define SENSOR_MIN_HEIGHT   480
#define SENSOR_MAX_WIDTH    2048
#define SENSOR_MAX_HEIGHT   1536
#define SENSOR_INIT_WIDTH	640			/* Sensor pixel size for sensor_init_data array */
#define SENSOR_INIT_HEIGHT  480
#define SENSOR_INIT_WINSEQADR sensor_vga
#define SENSOR_INIT_PIXFMT V4L2_MBUS_FMT_UYVY8_2X8

#define CONFIG_SENSOR_WhiteBalance	1
#define CONFIG_SENSOR_Brightness	0
#define CONFIG_SENSOR_Contrast      0
#define CONFIG_SENSOR_Saturation    0
#define CONFIG_SENSOR_Effect        1
#define CONFIG_SENSOR_Scene         1
#define CONFIG_SENSOR_AntiBanding   0
#define CONFIG_SENSOR_DigitalZoom   0
#define CONFIG_SENSOR_Focus         0
#define CONFIG_SENSOR_Exposure      0
#define CONFIG_SENSOR_Flash         0
#define CONFIG_SENSOR_Mirror        0
#define CONFIG_SENSOR_Flip          0
#define CONFIG_SENSOR_Focus         0


#define CONFIG_SENSOR_I2C_SPEED     400000       /* Hz */
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

/* init 640X480 SVGA */
static struct reginfo sensor_init_data[] =
{
	{0xFCFC, 0xD000},	//Reset                                  //
	{0x0010, 0x0001},   //Clear host interrupt so main will wait //
	{0x1030, 0x0000},   //ARM go                                 //
	{0x0014, 0x0001},   //Wait100mSec                            //
	{SEQUENCE_WAIT_MS,100},	//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20 

	//tp
	{0x0028, 0x7000},
	{0x002A, 0x2CF8},
	{0x0F12, 0xB510},
	{0x0F12, 0x4827},
	{0x0F12, 0x21C0},
	{0x0F12, 0x8041},
	{0x0F12, 0x4825},
	{0x0F12, 0x4A26},
	{0x0F12, 0x3020},
	{0x0F12, 0x8382},
	{0x0F12, 0x1D12},
	{0x0F12, 0x83C2},
	{0x0F12, 0x4822},
	{0x0F12, 0x3040},
	{0x0F12, 0x8041},
	{0x0F12, 0x4821},
	{0x0F12, 0x4922},
	{0x0F12, 0x3060},
	{0x0F12, 0x8381},
	{0x0F12, 0x1D09},
	{0x0F12, 0x83C1},
	{0x0F12, 0x4821},
	{0x0F12, 0x491D},
	{0x0F12, 0x8802},
	{0x0F12, 0x3980},
	{0x0F12, 0x804A},
	{0x0F12, 0x8842},
	{0x0F12, 0x808A},
	{0x0F12, 0x8882},
	{0x0F12, 0x80CA},
	{0x0F12, 0x88C2},
	{0x0F12, 0x810A},
	{0x0F12, 0x8902},
	{0x0F12, 0x491C},
	{0x0F12, 0x80CA},
	{0x0F12, 0x8942},
	{0x0F12, 0x814A},
	{0x0F12, 0x8982},
	{0x0F12, 0x830A},
	{0x0F12, 0x89C2},
	{0x0F12, 0x834A},
	{0x0F12, 0x8A00},
	{0x0F12, 0x4918},
	{0x0F12, 0x8188},
	{0x0F12, 0x4918},
	{0x0F12, 0x4819},
	{0x0F12, 0xF000},
    {0x0F12, 0xFA0C},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20 
	{0x0F12, 0x4918},
	{0x0F12, 0x4819},
	{0x0F12, 0x6341},
	{0x0F12, 0x4919},
	{0x0F12, 0x4819},
	{0x0F12, 0xF000},
    {0x0F12, 0xFA05},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20 
	{0x0F12, 0x4816},
	{0x0F12, 0x4918},
	{0x0F12, 0x3840},
	{0x0F12, 0x62C1},
	{0x0F12, 0x4918},
	{0x0F12, 0x3880},
	{0x0F12, 0x63C1},
	{0x0F12, 0x4917},
	{0x0F12, 0x6301},
	{0x0F12, 0x4917},
	{0x0F12, 0x3040},
	{0x0F12, 0x6181},
	{0x0F12, 0x4917},
	{0x0F12, 0x4817},
	{0x0F12, 0xF000},
    {0x0F12, 0xF9F5},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20 
	{0x0F12, 0x4917},
	{0x0F12, 0x4817},
	{0x0F12, 0xF000},
    {0x0F12, 0xF9F1},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20 
	{0x0F12, 0x4917},
	{0x0F12, 0x4817},
	{0x0F12, 0xF000},
    {0x0F12, 0xF9ED},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20 
	{0x0F12, 0xBC10},
	{0x0F12, 0xBC08},
	{0x0F12, 0x4718},
	{0x0F12, 0x1100},
	{0x0F12, 0xD000},
	{0x0F12, 0x267C},
	{0x0F12, 0x0000},
	{0x0F12, 0x2CE8},
	{0x0F12, 0x0000},
	{0x0F12, 0x3274},
	{0x0F12, 0x7000},
	{0x0F12, 0xF400},
	{0x0F12, 0xD000},
	{0x0F12, 0xF520},
	{0x0F12, 0xD000},
	{0x0F12, 0x2DF1},
	{0x0F12, 0x7000},
	{0x0F12, 0x89A9},
	{0x0F12, 0x0000},
	{0x0F12, 0x2E43},
	{0x0F12, 0x7000},
	{0x0F12, 0x0140},
	{0x0F12, 0x7000},
    {0x0F12, 0x2E75},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20 
	{0x0F12, 0x7000},
	{0x0F12, 0xB4F7},
	{0x0F12, 0x0000},
    {0x0F12, 0x2EFF},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20 
	{0x0F12, 0x7000},
    {0x0F12, 0x2F23},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20 
	{0x0F12, 0x7000},
    {0x0F12, 0x2FCD},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20 
	{0x0F12, 0x7000},
    {0x0F12, 0x2FE1},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20 
	{0x0F12, 0x7000},
    {0x0F12, 0x2FB5},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20 
	{0x0F12, 0x7000},
	{0x0F12, 0x013D},
	{0x0F12, 0x0001},
    {0x0F12, 0x3067},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20 
	{0x0F12, 0x7000},
	{0x0F12, 0x5823},
	{0x0F12, 0x0000},
    {0x0F12, 0x30B5},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20 
	{0x0F12, 0x7000},
	{0x0F12, 0xD789},
	{0x0F12, 0x0000},
	{0x0F12, 0xB570},
	{0x0F12, 0x6804},
	{0x0F12, 0x6845},
	{0x0F12, 0x6881},
	{0x0F12, 0x6840},
	{0x0F12, 0x2900},
	{0x0F12, 0x6880},
	{0x0F12, 0xD007},
    {0x0F12, 0x49C2},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20 
	{0x0F12, 0x8949},
	{0x0F12, 0x084A},
	{0x0F12, 0x1880},
	{0x0F12, 0xF000},
    {0x0F12, 0xF9B8},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20 
	{0x0F12, 0x80A0},
	{0x0F12, 0xE000},
	{0x0F12, 0x80A0},
	{0x0F12, 0x88A0},
	{0x0F12, 0x2800},
	{0x0F12, 0xD010},
	{0x0F12, 0x68A9},
	{0x0F12, 0x6828},
	{0x0F12, 0x084A},
	{0x0F12, 0x1880},
	{0x0F12, 0xF000},
    {0x0F12, 0xF9AC},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20 
	{0x0F12, 0x8020},
	{0x0F12, 0x1D2D},
	{0x0F12, 0xCD03},
	{0x0F12, 0x084A},
	{0x0F12, 0x1880},
	{0x0F12, 0xF000},
    {0x0F12, 0xF9A5},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	{0x0F12, 0x8060},
	{0x0F12, 0xBC70},
	{0x0F12, 0xBC08},
	{0x0F12, 0x4718},
	{0x0F12, 0x2000},
	{0x0F12, 0x8060},
	{0x0F12, 0x8020},
	{0x0F12, 0xE7F8},
	{0x0F12, 0xB510},
	{0x0F12, 0xF000},
    //BEGIN: DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
    {0x0F12, 0xF9A0},
	//{0x0F12, 0x48B2},
	//{0x0F12, 0x8A40},
	//{0x0F12, 0x2800},
	//{0x0F12, 0xD00C},
    //END: DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	{0x0F12, 0x48B1},
	{0x0F12, 0x49B2},
	{0x0F12, 0x8800},
	{0x0F12, 0x4AB2},
	{0x0F12, 0x2805},
	{0x0F12, 0xD003},
	{0x0F12, 0x4BB1},
	{0x0F12, 0x795B},
	{0x0F12, 0x2B00},
	{0x0F12, 0xD005},
	{0x0F12, 0x2001},
	{0x0F12, 0x8008},
	{0x0F12, 0x8010},
	{0x0F12, 0xBC10},
	{0x0F12, 0xBC08},
	{0x0F12, 0x4718},
	{0x0F12, 0x2800},
	{0x0F12, 0xD1FA},
	{0x0F12, 0x2000},
	{0x0F12, 0x8008},
	{0x0F12, 0x8010},
	{0x0F12, 0xE7F6},
	{0x0F12, 0xB5F8},
	{0x0F12, 0x2407},
	{0x0F12, 0x2C06},
	{0x0F12, 0xD035},
	{0x0F12, 0x2C07},
	{0x0F12, 0xD033},
	{0x0F12, 0x48A3},
	{0x0F12, 0x8BC1},
	{0x0F12, 0x2900},
	{0x0F12, 0xD02A},
	{0x0F12, 0x00A2},
	{0x0F12, 0x1815},
	{0x0F12, 0x4AA4},
	{0x0F12, 0x6DEE},
	{0x0F12, 0x8A92},
	{0x0F12, 0x4296},
	{0x0F12, 0xD923},
	{0x0F12, 0x0028},
	{0x0F12, 0x3080},
	{0x0F12, 0x0007},
	{0x0F12, 0x69C0},
	{0x0F12, 0xF000},
    {0x0F12, 0xF96D},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	{0x0F12, 0x1C71},
	{0x0F12, 0x0280},
	{0x0F12, 0xF000},
    {0x0F12, 0xF969},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	{0x0F12, 0x0006},
	{0x0F12, 0x4898},
	{0x0F12, 0x0061},
	{0x0F12, 0x1808},
	{0x0F12, 0x8D80},
	{0x0F12, 0x0A01},
	{0x0F12, 0x0600},
	{0x0F12, 0x0E00},
	{0x0F12, 0x1A08},
	{0x0F12, 0xF000},
    {0x0F12, 0xF96C},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	{0x0F12, 0x0002},
	{0x0F12, 0x6DE9},
	{0x0F12, 0x6FE8},
	{0x0F12, 0x1A08},
	{0x0F12, 0x4351},
	{0x0F12, 0x0300},
	{0x0F12, 0x1C49},
	{0x0F12, 0xF000},
    {0x0F12, 0xF955},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	{0x0F12, 0x0401},
	{0x0F12, 0x0430},
	{0x0F12, 0x0C00},
	{0x0F12, 0x4301},
	{0x0F12, 0x61F9},
	{0x0F12, 0xE004},
	{0x0F12, 0x00A2},
	{0x0F12, 0x4990},
	{0x0F12, 0x1810},
	{0x0F12, 0x3080},
	{0x0F12, 0x61C1},
	{0x0F12, 0x1E64},
	{0x0F12, 0xD2C5},
	{0x0F12, 0x2006},
	{0x0F12, 0xF000},
    {0x0F12, 0xF95B},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	{0x0F12, 0x2007},
	{0x0F12, 0xF000},
    {0x0F12, 0xF958},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	{0x0F12, 0xBCF8},
	{0x0F12, 0xBC08},
	{0x0F12, 0x4718},
	{0x0F12, 0xB510},
	{0x0F12, 0xF000},
    {0x0F12, 0xF95A},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	{0x0F12, 0x2800},
	{0x0F12, 0xD00A},
	{0x0F12, 0x4881},
	{0x0F12, 0x8B81},
	{0x0F12, 0x0089},
	{0x0F12, 0x1808},
	{0x0F12, 0x6DC1},
	{0x0F12, 0x4883},
	{0x0F12, 0x8A80},
	{0x0F12, 0x4281},
	{0x0F12, 0xD901},
	{0x0F12, 0x2001},
	{0x0F12, 0xE7A1},
	{0x0F12, 0x2000},
	{0x0F12, 0xE79F},
	{0x0F12, 0xB5F8},
	{0x0F12, 0x0004},
	{0x0F12, 0x4F80},
	{0x0F12, 0x227D},
	{0x0F12, 0x8938},
	{0x0F12, 0x0152},
	{0x0F12, 0x4342},
	{0x0F12, 0x487E},
	{0x0F12, 0x9000},
	{0x0F12, 0x8A01},
	{0x0F12, 0x0848},
	{0x0F12, 0x1810},
	{0x0F12, 0xF000},
    {0x0F12, 0xF91F},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	{0x0F12, 0x210F},
	{0x0F12, 0xF000},
    {0x0F12, 0xF942},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	{0x0F12, 0x497A},
	{0x0F12, 0x8C49},
	{0x0F12, 0x090E},
	{0x0F12, 0x0136},
	{0x0F12, 0x4306},
	{0x0F12, 0x4979},
	{0x0F12, 0x2C00},
	{0x0F12, 0xD003},
	{0x0F12, 0x2001},
	{0x0F12, 0x0240},
	{0x0F12, 0x4330},
	{0x0F12, 0x8108},
	{0x0F12, 0x4876},
	{0x0F12, 0x2C00},
	{0x0F12, 0x8D00},
	{0x0F12, 0xD001},
	{0x0F12, 0x2501},
	{0x0F12, 0xE000},
	{0x0F12, 0x2500},
	{0x0F12, 0x4972},
	{0x0F12, 0x4328},
	{0x0F12, 0x8008},
	{0x0F12, 0x207D},
	{0x0F12, 0x00C0},
	{0x0F12, 0xF000},
    {0x0F12, 0xF930},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	{0x0F12, 0x2C00},
	{0x0F12, 0x496E},
	{0x0F12, 0x0328},
	{0x0F12, 0x4330},
	{0x0F12, 0x8108},
	{0x0F12, 0x88F8},
	{0x0F12, 0x2C00},
	{0x0F12, 0x01AA},
	{0x0F12, 0x4310},
	{0x0F12, 0x8088},
    {0x0F12, 0x2C00},//DTS2012071201781: add by huyouhua 00136760 at 2012-7-20
    {0x0F12, 0xD00B},//DTS2012071201781: add by huyouhua 00136760 at 2012-7-20
	{0x0F12, 0x9800},
	{0x0F12, 0x8A01},
    {0x0F12, 0x4869},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	{0x0F12, 0xF000},
	{0x0F12, 0xF8F1},
    {0x0F12, 0x4969},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	{0x0F12, 0x8809},
	{0x0F12, 0x4348},
	{0x0F12, 0x0400},
	{0x0F12, 0x0C00},
	{0x0F12, 0xF000},
	{0x0F12, 0xF918},
	{0x0F12, 0x0020},
	{0x0F12, 0xF000},
	{0x0F12, 0xF91D},
    {0x0F12, 0x4865},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	{0x0F12, 0x7004},
    {0x0F12, 0xE7A1},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	{0x0F12, 0xB510},
	{0x0F12, 0x0004},
	{0x0F12, 0xF000},
	{0x0F12, 0xF91E},
	{0x0F12, 0x6020},
    {0x0F12, 0x4962},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	{0x0F12, 0x8B49},
	{0x0F12, 0x0789},
	{0x0F12, 0xD001},
	{0x0F12, 0x0040},
	{0x0F12, 0x6020},
    {0x0F12, 0xE74A},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	{0x0F12, 0xB510},
	{0x0F12, 0xF000},
	{0x0F12, 0xF91B},
    {0x0F12, 0x485E},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	{0x0F12, 0x8880},
	{0x0F12, 0x0601},
    {0x0F12, 0x4853},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	{0x0F12, 0x1609},
	{0x0F12, 0x8141},
    {0x0F12, 0xE740},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	{0x0F12, 0xB5F8},
	{0x0F12, 0x000F},
    {0x0F12, 0x4C54},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	{0x0F12, 0x3420},
	{0x0F12, 0x2500},
	{0x0F12, 0x5765},
	{0x0F12, 0x0039},
	{0x0F12, 0xF000},
	{0x0F12, 0xF913},
	{0x0F12, 0x9000},
	{0x0F12, 0x2600},
	{0x0F12, 0x57A6},
    {0x0F12, 0x4C4B},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	{0x0F12, 0x42AE},
	{0x0F12, 0xD01B},
    {0x0F12, 0x4D53},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	{0x0F12, 0x8AE8},
	{0x0F12, 0x2800},
	{0x0F12, 0xD013},
    {0x0F12, 0x484C},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	{0x0F12, 0x8A01},
	{0x0F12, 0x8B80},
	{0x0F12, 0x4378},
	{0x0F12, 0xF000},
	{0x0F12, 0xF8B5},
	{0x0F12, 0x89A9},
	{0x0F12, 0x1A41},
    {0x0F12, 0x484D},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	{0x0F12, 0x3820},
	{0x0F12, 0x8AC0},
	{0x0F12, 0x4348},
	{0x0F12, 0x17C1},
	{0x0F12, 0x0D89},
	{0x0F12, 0x1808},
	{0x0F12, 0x1280},
	{0x0F12, 0x8961},
	{0x0F12, 0x1A08},
	{0x0F12, 0x8160},
	{0x0F12, 0xE003},
	{0x0F12, 0x88A8},
	{0x0F12, 0x0600},
	{0x0F12, 0x1600},
	{0x0F12, 0x8160},
	{0x0F12, 0x200A},
	{0x0F12, 0x5E20},
	{0x0F12, 0x42B0},
	{0x0F12, 0xD011},
	{0x0F12, 0xF000},
	{0x0F12, 0xF8AB},
	{0x0F12, 0x1D40},
	{0x0F12, 0x00C3},
	{0x0F12, 0x1A18},
	{0x0F12, 0x214B},
	{0x0F12, 0xF000},
	{0x0F12, 0xF897},
	{0x0F12, 0x211F},
	{0x0F12, 0xF000},
	{0x0F12, 0xF8BA},
	{0x0F12, 0x210A},
	{0x0F12, 0x5E61},
	{0x0F12, 0x0FC9},
	{0x0F12, 0x0149},
	{0x0F12, 0x4301},
    {0x0F12, 0x483C},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	{0x0F12, 0x81C1},
	{0x0F12, 0x9800},
    {0x0F12, 0xE748},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	{0x0F12, 0xB5F1},
	{0x0F12, 0xB082},
	{0x0F12, 0x2500},
    {0x0F12, 0x4839},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	{0x0F12, 0x9001},
	{0x0F12, 0x2400},
	{0x0F12, 0x2028},
	{0x0F12, 0x4368},
	{0x0F12, 0x4A39},
    {0x0F12, 0x4937},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	{0x0F12, 0x1887},
	{0x0F12, 0x1840},
	{0x0F12, 0x9000},
	{0x0F12, 0x9800},
	{0x0F12, 0x0066},
	{0x0F12, 0x9A01},
	{0x0F12, 0x1980},
	{0x0F12, 0x218C},
	{0x0F12, 0x5A09},
	{0x0F12, 0x8A80},
	{0x0F12, 0x8812},
	{0x0F12, 0xF000},
	{0x0F12, 0xF8CA},
	{0x0F12, 0x53B8},
	{0x0F12, 0x1C64},
	{0x0F12, 0x2C14},
	{0x0F12, 0xDBF1},
	{0x0F12, 0x1C6D},
	{0x0F12, 0x2D03},
	{0x0F12, 0xDBE6},
	{0x0F12, 0x9802},
	{0x0F12, 0x6800},
	{0x0F12, 0x0600},
	{0x0F12, 0x0E00},
	{0x0F12, 0xF000},
	{0x0F12, 0xF8C5},
	{0x0F12, 0xBCFE},
	{0x0F12, 0xBC08},
	{0x0F12, 0x4718},
	{0x0F12, 0xB570},
	{0x0F12, 0x6805},
	{0x0F12, 0x2404},
	{0x0F12, 0xF000},
	{0x0F12, 0xF8C5},
	{0x0F12, 0x2800},
	{0x0F12, 0xD103},
	{0x0F12, 0xF000},
	{0x0F12, 0xF8C9},
	{0x0F12, 0x2800},
	{0x0F12, 0xD000},
	{0x0F12, 0x2400},
	{0x0F12, 0x3540},
	{0x0F12, 0x88E8},
	{0x0F12, 0x0500},
	{0x0F12, 0xD403},
	{0x0F12, 0x4822},
	{0x0F12, 0x89C0},
	{0x0F12, 0x2800},
	{0x0F12, 0xD002},
	{0x0F12, 0x2008},
	{0x0F12, 0x4304},
	{0x0F12, 0xE001},
	{0x0F12, 0x2010},
	{0x0F12, 0x4304},
	{0x0F12, 0x481F},
	{0x0F12, 0x8B80},
	{0x0F12, 0x0700},
	{0x0F12, 0x0F81},
	{0x0F12, 0x2001},
	{0x0F12, 0x2900},
	{0x0F12, 0xD000},
	{0x0F12, 0x4304},
	{0x0F12, 0x491C},
	{0x0F12, 0x8B0A},
	{0x0F12, 0x42A2},
	{0x0F12, 0xD004},
	{0x0F12, 0x0762},
	{0x0F12, 0xD502},
	{0x0F12, 0x4A19},
	{0x0F12, 0x3220},
	{0x0F12, 0x8110},
	{0x0F12, 0x830C},
    {0x0F12, 0xE693},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	{0x0F12, 0x0C3C},
    //BEGIN: DTS2012071201781: del by huyouhua 00136760 at 2012-7-20
	//{0x0F12, 0x7000},
	//{0x0F12, 0x3274},
    //END: DTS2012071201781: del by huyouhua 00136760 at 2012-7-20
	{0x0F12, 0x7000},
	{0x0F12, 0x26E8},
	{0x0F12, 0x7000},
	{0x0F12, 0x6100},
	{0x0F12, 0xD000},
	{0x0F12, 0x6500},
	{0x0F12, 0xD000},
	{0x0F12, 0x1A7C},
	{0x0F12, 0x7000},
	{0x0F12, 0x1120},
	{0x0F12, 0x7000},
	{0x0F12, 0xFFFF},
	{0x0F12, 0x0000},
	{0x0F12, 0x3374},
	{0x0F12, 0x7000},
	{0x0F12, 0x1D6C},
	{0x0F12, 0x7000},
	{0x0F12, 0x167C},
	{0x0F12, 0x7000},
	{0x0F12, 0xF400},
	{0x0F12, 0xD000},
	{0x0F12, 0x2C2C},
	{0x0F12, 0x7000},
	{0x0F12, 0x40A0},
	{0x0F12, 0x00DD},
	{0x0F12, 0xF520},
	{0x0F12, 0xD000},
	{0x0F12, 0x2C29},
	{0x0F12, 0x7000},
	{0x0F12, 0x1A54},
	{0x0F12, 0x7000},
	{0x0F12, 0x1564},
	{0x0F12, 0x7000},
	{0x0F12, 0xF2A0},
	{0x0F12, 0xD000},
	{0x0F12, 0x2440},
    {0x0F12, 0x7000},//DTS2012071201781: add by huyouhua 00136760 at 2012-7-20
    {0x0F12, 0x3274},//DTS2012071201781: add by huyouhua 00136760 at 2012-7-20
	{0x0F12, 0x7000},
	{0x0F12, 0x05A0},
	{0x0F12, 0x7000},
	{0x0F12, 0x2894},
	{0x0F12, 0x7000},
	{0x0F12, 0x1224},
	{0x0F12, 0x7000},
	{0x0F12, 0xB000},
	{0x0F12, 0xD000},
	{0x0F12, 0x4778},
	{0x0F12, 0x46C0},
	{0x0F12, 0xC000},
	{0x0F12, 0xE59F},
	{0x0F12, 0xFF1C},
	{0x0F12, 0xE12F},
	{0x0F12, 0x1A3F},
	{0x0F12, 0x0001},
	{0x0F12, 0x4778},
	{0x0F12, 0x46C0},
	{0x0F12, 0xF004},
	{0x0F12, 0xE51F},
	{0x0F12, 0x1F48},
	{0x0F12, 0x0001},
	{0x0F12, 0x4778},
	{0x0F12, 0x46C0},
	{0x0F12, 0xC000},
	{0x0F12, 0xE59F},
	{0x0F12, 0xFF1C},
	{0x0F12, 0xE12F},
	{0x0F12, 0x24BD},
	{0x0F12, 0x0000},
	{0x0F12, 0x4778},
	{0x0F12, 0x46C0},
	{0x0F12, 0xC000},
	{0x0F12, 0xE59F},
	{0x0F12, 0xFF1C},
	{0x0F12, 0xE12F},
	{0x0F12, 0x36DD},
	{0x0F12, 0x0000},
	{0x0F12, 0x4778},
	{0x0F12, 0x46C0},
	{0x0F12, 0xC000},
	{0x0F12, 0xE59F},
	{0x0F12, 0xFF1C},
	{0x0F12, 0xE12F},
	{0x0F12, 0xB4CF},
	{0x0F12, 0x0000},
	{0x0F12, 0x4778},
	{0x0F12, 0x46C0},
	{0x0F12, 0xC000},
	{0x0F12, 0xE59F},
	{0x0F12, 0xFF1C},
	{0x0F12, 0xE12F},
	{0x0F12, 0xB5D7},
	{0x0F12, 0x0000},
	{0x0F12, 0x4778},
	{0x0F12, 0x46C0},
	{0x0F12, 0xC000},
	{0x0F12, 0xE59F},
	{0x0F12, 0xFF1C},
	{0x0F12, 0xE12F},
	{0x0F12, 0x36ED},
	{0x0F12, 0x0000},
	{0x0F12, 0x4778},
	{0x0F12, 0x46C0},
	{0x0F12, 0xC000},
	{0x0F12, 0xE59F},
	{0x0F12, 0xFF1C},
	{0x0F12, 0xE12F},
	{0x0F12, 0xF53F},
	{0x0F12, 0x0000},
	{0x0F12, 0x4778},
	{0x0F12, 0x46C0},
	{0x0F12, 0xC000},
	{0x0F12, 0xE59F},
	{0x0F12, 0xFF1C},
	{0x0F12, 0xE12F},
	{0x0F12, 0xF5D9},
	{0x0F12, 0x0000},
	{0x0F12, 0x4778},
	{0x0F12, 0x46C0},
	{0x0F12, 0xC000},
	{0x0F12, 0xE59F},
	{0x0F12, 0xFF1C},
	{0x0F12, 0xE12F},
	{0x0F12, 0x013D},
	{0x0F12, 0x0001},
	{0x0F12, 0x4778},
	{0x0F12, 0x46C0},
	{0x0F12, 0xC000},
	{0x0F12, 0xE59F},
	{0x0F12, 0xFF1C},
	{0x0F12, 0xE12F},
	{0x0F12, 0xF5C9},
	{0x0F12, 0x0000},
	{0x0F12, 0x4778},
	{0x0F12, 0x46C0},
	{0x0F12, 0xC000},
	{0x0F12, 0xE59F},
	{0x0F12, 0xFF1C},
	{0x0F12, 0xE12F},
	{0x0F12, 0xFAA9},
	{0x0F12, 0x0000},
	{0x0F12, 0x4778},
	{0x0F12, 0x46C0},
	{0x0F12, 0xC000},
	{0x0F12, 0xE59F},
	{0x0F12, 0xFF1C},
	{0x0F12, 0xE12F},
	{0x0F12, 0x3723},
	{0x0F12, 0x0000},
	{0x0F12, 0x4778},
	{0x0F12, 0x46C0},
	{0x0F12, 0xC000},
	{0x0F12, 0xE59F},
	{0x0F12, 0xFF1C},
	{0x0F12, 0xE12F},
	{0x0F12, 0x5823},
	{0x0F12, 0x0000},
	{0x0F12, 0x4778},
	{0x0F12, 0x46C0},
	{0x0F12, 0xC000},
	{0x0F12, 0xE59F},
	{0x0F12, 0xFF1C},
	{0x0F12, 0xE12F},
	{0x0F12, 0xD771},
	{0x0F12, 0x0000},
	{0x0F12, 0x4778},
	{0x0F12, 0x46C0},
	{0x0F12, 0xC000},
	{0x0F12, 0xE59F},
	{0x0F12, 0xFF1C},
	{0x0F12, 0xE12F},
	{0x0F12, 0xD75B},
	{0x0F12, 0x0000},
    {0x0F12, 0x7E77},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	{0x0F12, 0x0000},
	//end tp

	//========================================================                      
	// CIs/APs/An setting        - 400LsB  sYsCLK 32MHz                             
	//========================================================                      
	// This regis are for FACTORY ONLY. If you change it without prior notification,
	// YOU are REsIBLE for the FAILURE that will happen in the future.              
	//========================================================                      
    {0x0028, 0x7000},//DTS2012071201781: add by huyouhua 00136760 at 2012-7-20                                                    

	{0x002A, 0x157A},
	{0x0F12, 0x0001},
	{0x002A, 0x1578},
	{0x0F12, 0x0001},
	{0x002A, 0x1576},
	{0x0F12, 0x0020},
	{0x002A, 0x1574},
	{0x0F12, 0x0006},
	{0x002A, 0x156E},
	{0x0F12, 0x0001},	// slope calibration tolerance in units of 1/256	
	{0x002A, 0x1568},
	{0x0F12, 0x00FC},

	//ADC control 
	{0x002A, 0x155A},
	{0x0F12, 0x01CC},	//ADC sAT of 450mV for 10bit default in EVT1                         	
	{0x002A, 0x157E},                                                                        
	{0x0F12, 0x0C80},	// 3200 Max. Reset ramp DCLK counts {default 2048 0x800}             
	{0x0F12, 0x0578},	// 1400 Max. Reset ramp DCLK counts for x3.5                         
	{0x002A, 0x157C},                                                                        
	{0x0F12, 0x0190},	// 400 Reset ramp for x1 in DCLK counts                              
	{0x002A, 0x1570},                                                                        
	{0x0F12, 0x00A0},	// 160 LsB                                                           
	{0x0F12, 0x0010},	// reset threshold                                                   
	{0x002A, 0x12C4},                                                                        
	{0x0F12, 0x006A},	// 106 additional timing columns.                                    
	{0x002A, 0x12C8},                                                                        
	{0x0F12, 0x08AC},	// 2220 ADC columns in normal mode including Hold & Latch            
	{0x0F12, 0x0050},	// 80 addition of ADC columns in Y-ave mode {default 244 0x74}

	{0x002A, 0x1696},	// based on APs guidelines                        
	{0x0F12, 0x0000},   // based on APs guidelines                        
	{0x0F12, 0x0000},   // default. 1492 used for ADC dark characteristics
	{0x0F12, 0x00C6},   // default. 1492 used for ADC dark characteristics
	{0x0F12, 0x00C6},                                                                                                       
    //BEGIN: DTS2012071201781: add by huyouhua 00136760 at 2012-7-20
    {0x002A, 0x12B8}, 
    {0x0F12, 0x0B00}, //#senHal_ExpMinPixels //disable CINTR 0  
    //END: DTS2012071201781: add by huyouhua 00136760 at 2012-7-20
	{0x002A, 0x1690},   // when set double sampling is activated - requires different set of pointers                                                 
	{0x0F12, 0x0001},                                                     

	{0x002A, 0x12B0},   // comp and pixel bias control 0xF40E - default for EVT1                                                                      
	{0x0F12, 0x0055},   // comp and pixel bias control 0xF40E for binning mode                                                                        
	{0x0F12, 0x005A},                                                     

	{0x002A, 0x337A},   // [7] - is used for rest-only mode {EVT0 value is 0xD and HW 0x6}                                                            
	{0x0F12, 0x0006},
	{0x0F12, 0x0068},
    //BEGIN:DTS2012071201781: del by huyouhua 00136760 at 2012-7-20
	//{0x002A, 0x169E},
	//{0x0F12, 0x0007},
	//{0x002A, 0x0BF6},
	//{0x0F12, 0x0000},
    //END:DTS2012071201781: del by huyouhua 00136760 at 2012-7-20

	{0x002A, 0x327C},
	{0x0F12, 0x1000},
	{0x0F12, 0x6998},
	{0x0F12, 0x0078},
	{0x0F12, 0x04FE},
	{0x0F12, 0x8800},
#if defined(CURRENT_4MA)
	{0x002A, 0x3274},
	{0x0F12, 0x0155},	//set IO driving current 4mA for Data 
	{0x0F12, 0x0155},   //set IO driving current 4ma for Data             
	{0x0F12, 0x1555},   //set IO driving current              
	{0x0F12, 0x0555},   //set IO driving current 4ma for PCLK/HSYNC/Vsync             
#elif defined(CURRENT_8MA)
	{0x002A, 0x3274},
	{0x0F12, 0x02aa},	//set IO driving current 8mA for Data
	{0x0F12, 0x02aa},   //set IO driving current              
	{0x0F12, 0x1555},   //set IO driving current              
	{0x0F12, 0x059a},   //set IO driving current              
#elif defined(CURRENT_12MA)
	{0x002A, 0x3274},
	{0x0F12, 0x03ff},	//set IO driving current 12mA for Data 
	{0x0F12, 0x03ff},   //set IO driving current              
	{0x0F12, 0x1555},   //set IO driving current              
	{0x0F12, 0x05df},   //set IO driving current              
    //BEGIN: DTS2012071201781 add by huyouhua 00136760 at 2012-7-23
#elif defined(CURRENT_SLIM)
    {0x002A, 0x3274},
    {0x0F12, 0x0155},//------- //#Tune_TP_IO_DrivingCurrent_D0_D4_cs10Set IO driving current--------------- 
    {0x0F12, 0x0155},//------- //#Tune_TP_IO_DrivingCurrent_D9_D5_cs10Set IO driving current--------------- 
    {0x0F12, 0x1555},//------- //#Tune_TP_IO_DrivingCurrent_GPIO_cd10 Set IO driving current--------------- 
    //{0x0F12, 0x05DF}//0x0555},//------- - //059A//05DF//#Tune_TP_IO_DrivingCurrent_CLKs_cd10 Set IO driving current-
    {0x0F12, 0x059A},//0x0555},//------- - //059A//05DF//#Tune_TP_IO_DrivingCurrent_CLKs_cd10 Set IO driving current-
    //595,59A,5DA,5DF
    //END: DTS2012071201781 add by huyouhua 00136760 at 2012-7-23
#endif

    //BEGIN: DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
    {0x002A, 0x169E},
    {0x0F12, 0x0007}, //evt1_senHal_Dblr2Value //	 [3:0]- specifies the target (default 7)- DCLK = 64MHz instead of 116MHz. 
    {0x002A, 0x0BF6},                                                                                     
    {0x0F12, 0x0000}, //from ALEX //Enable Bayer Downscaler  
    //END: DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20                                             
	{0x0028, 0x7000},	
	{0x002A, 0x12D2},	   
	{0x0F12, 0x0003},	//senHal_pContSenModesRegsArray[0][0]2 700012D2     
	{0x0F12, 0x0003},   //senHal_pContSenModesRegsArray[0][1]2 700012D4  
	{0x0F12, 0x0003},   //senHal_pContSenModesRegsArray[0][2]2 700012D6  
	{0x0F12, 0x0003},   //senHal_pContSenModesRegsArray[0][3]2 700012D8  
	{0x0F12, 0x0884},   //senHal_pContSenModesRegsArray[1][0]2 700012DA  
	{0x0F12, 0x08CF},   //senHal_pContSenModesRegsArray[1][1]2 700012DC  
	{0x0F12, 0x0500},   //senHal_pContSenModesRegsArray[1][2]2 700012DE  
	{0x0F12, 0x054B},   //senHal_pContSenModesRegsArray[1][3]2 700012E0  
	{0x0F12, 0x0001},   //senHal_pContSenModesRegsArray[2][0]2 700012E2  
	{0x0F12, 0x0001},   //senHal_pContSenModesRegsArray[2][1]2 700012E4  
	{0x0F12, 0x0001},   //senHal_pContSenModesRegsArray[2][2]2 700012E6  
	{0x0F12, 0x0001},   //senHal_pContSenModesRegsArray[2][3]2 700012E8  
	{0x0F12, 0x0885},   //senHal_pContSenModesRegsArray[3][0]2 700012EA  
	{0x0F12, 0x0467},   //senHal_pContSenModesRegsArray[3][1]2 700012EC  
	{0x0F12, 0x0501},   //senHal_pContSenModesRegsArray[3][2]2 700012EE  
	{0x0F12, 0x02A5},   //senHal_pContSenModesRegsArray[3][3]2 700012F0  
	{0x0F12, 0x0001},   //senHal_pContSenModesRegsArray[4][0]2 700012F2  
	{0x0F12, 0x046A},   //senHal_pContSenModesRegsArray[4][1]2 700012F4  
	{0x0F12, 0x0001},   //senHal_pContSenModesRegsArray[4][2]2 700012F6  
	{0x0F12, 0x02A8},   //senHal_pContSenModesRegsArray[4][3]2 700012F8  
	{0x0F12, 0x0885},   //senHal_pContSenModesRegsArray[5][0]2 700012FA  
	{0x0F12, 0x08D0},   //senHal_pContSenModesRegsArray[5][1]2 700012FC  
	{0x0F12, 0x0501},   //senHal_pContSenModesRegsArray[5][2]2 700012FE  
	{0x0F12, 0x054C},   //senHal_pContSenModesRegsArray[5][3]2 70001300  
	{0x0F12, 0x0006},   //senHal_pContSenModesRegsArray[6][0]2 70001302  
	{0x0F12, 0x0020},   //senHal_pContSenModesRegsArray[6][1]2 70001304  
	{0x0F12, 0x0006},   //senHal_pContSenModesRegsArray[6][2]2 70001306  
	{0x0F12, 0x0020},   //senHal_pContSenModesRegsArray[6][3]2 70001308  
	{0x0F12, 0x0881},   //senHal_pContSenModesRegsArray[7][0]2 7000130A  
	{0x0F12, 0x0463},   //senHal_pContSenModesRegsArray[7][1]2 7000130C  
	{0x0F12, 0x04FD},   //senHal_pContSenModesRegsArray[7][2]2 7000130E  
	{0x0F12, 0x02A1},   //senHal_pContSenModesRegsArray[7][3]2 70001310  
	{0x0F12, 0x0006},   //senHal_pContSenModesRegsArray[8][0]2 70001312  
	{0x0F12, 0x0489},   //senHal_pContSenModesRegsArray[8][1]2 70001314  
	{0x0F12, 0x0006},   //senHal_pContSenModesRegsArray[8][2]2 70001316  
	{0x0F12, 0x02C7},   //senHal_pContSenModesRegsArray[8][3]2 70001318  
	{0x0F12, 0x0881},   //senHal_pContSenModesRegsArray[9][0]2 7000131A  
	{0x0F12, 0x08CC},   //senHal_pContSenModesRegsArray[9][1]2 7000131C  
	{0x0F12, 0x04FD},   //senHal_pContSenModesRegsArray[9][2]2 7000131E  
	{0x0F12, 0x0548},   //senHal_pContSenModesRegsArray[9][3]2 70001320  
	{0x0F12, 0x03A2},   //senHal_pContSenModesRegsArray[10][0] 2 70001322
	{0x0F12, 0x01D3},   //senHal_pContSenModesRegsArray[10][1] 2 70001324
	{0x0F12, 0x01E0},   //senHal_pContSenModesRegsArray[10][2] 2 70001326
	{0x0F12, 0x00F2},   //senHal_pContSenModesRegsArray[10][3] 2 70001328
	{0x0F12, 0x03F2},   //senHal_pContSenModesRegsArray[11][0] 2 7000132A
	{0x0F12, 0x0223},   //senHal_pContSenModesRegsArray[11][1] 2 7000132C
	{0x0F12, 0x0230},   //senHal_pContSenModesRegsArray[11][2] 2 7000132E
	{0x0F12, 0x0142},   //senHal_pContSenModesRegsArray[11][3] 2 70001330
	{0x0F12, 0x03A2},   //senHal_pContSenModesRegsArray[12][0] 2 70001332
	{0x0F12, 0x063C},   //senHal_pContSenModesRegsArray[12][1] 2 70001334
	{0x0F12, 0x01E0},   //senHal_pContSenModesRegsArray[12][2] 2 70001336
	{0x0F12, 0x0399},   //senHal_pContSenModesRegsArray[12][3] 2 70001338
	{0x0F12, 0x03F2},   //senHal_pContSenModesRegsArray[13][0] 2 7000133A
	{0x0F12, 0x068C},   //senHal_pContSenModesRegsArray[13][1] 2 7000133C
	{0x0F12, 0x0230},   //senHal_pContSenModesRegsArray[13][2] 2 7000133E
	{0x0F12, 0x03E9},   //senHal_pContSenModesRegsArray[13][3] 2 70001340
	{0x0F12, 0x0002},   //senHal_pContSenModesRegsArray[14][0] 2 70001342
	{0x0F12, 0x0002},   //senHal_pContSenModesRegsArray[14][1] 2 70001344
	{0x0F12, 0x0002},   //senHal_pContSenModesRegsArray[14][2] 2 70001346
	{0x0F12, 0x0002},   //senHal_pContSenModesRegsArray[14][3] 2 70001348
	{0x0F12, 0x003C},   //senHal_pContSenModesRegsArray[15][0] 2 7000134A
	{0x0F12, 0x003C},   //senHal_pContSenModesRegsArray[15][1] 2 7000134C
	{0x0F12, 0x003C},   //senHal_pContSenModesRegsArray[15][2] 2 7000134E
	{0x0F12, 0x003C},   //senHal_pContSenModesRegsArray[15][3] 2 70001350
	{0x0F12, 0x01D3},   //senHal_pContSenModesRegsArray[16][0] 2 70001352
	{0x0F12, 0x01D3},   //senHal_pContSenModesRegsArray[16][1] 2 70001354
	{0x0F12, 0x00F2},   //senHal_pContSenModesRegsArray[16][2] 2 70001356
	{0x0F12, 0x00F2},   //senHal_pContSenModesRegsArray[16][3] 2 70001358
	{0x0F12, 0x020B},   //senHal_pContSenModesRegsArray[17][0] 2 7000135A
	{0x0F12, 0x024A},   //senHal_pContSenModesRegsArray[17][1] 2 7000135C
	{0x0F12, 0x012A},   //senHal_pContSenModesRegsArray[17][2] 2 7000135E
	{0x0F12, 0x0169},   //senHal_pContSenModesRegsArray[17][3] 2 70001360
	{0x0F12, 0x0002},   //senHal_pContSenModesRegsArray[18][0] 2 70001362
	{0x0F12, 0x046B},   //senHal_pContSenModesRegsArray[18][1] 2 70001364
	{0x0F12, 0x0002},   //senHal_pContSenModesRegsArray[18][2] 2 70001366
	{0x0F12, 0x02A9},   //senHal_pContSenModesRegsArray[18][3] 2 70001368
	{0x0F12, 0x0419},   //senHal_pContSenModesRegsArray[19][0] 2 7000136A
	{0x0F12, 0x04A5},   //senHal_pContSenModesRegsArray[19][1] 2 7000136C
	{0x0F12, 0x0257},   //senHal_pContSenModesRegsArray[19][2] 2 7000136E
	{0x0F12, 0x02E3},   //senHal_pContSenModesRegsArray[19][3] 2 70001370
	{0x0F12, 0x0630},   //senHal_pContSenModesRegsArray[20][0] 2 70001372
	{0x0F12, 0x063C},   //senHal_pContSenModesRegsArray[20][1] 2 70001374
	{0x0F12, 0x038D},   //senHal_pContSenModesRegsArray[20][2] 2 70001376
	{0x0F12, 0x0399},   //senHal_pContSenModesRegsArray[20][3] 2 70001378
	{0x0F12, 0x0668},   //senHal_pContSenModesRegsArray[21][0] 2 7000137A
	{0x0F12, 0x06B3},   //senHal_pContSenModesRegsArray[21][1] 2 7000137C
	{0x0F12, 0x03C5},   //senHal_pContSenModesRegsArray[21][2] 2 7000137E
	{0x0F12, 0x0410},   //senHal_pContSenModesRegsArray[21][3] 2 70001380
	{0x0F12, 0x0001},   //senHal_pContSenModesRegsArray[22][0] 2 70001382
	{0x0F12, 0x0001},   //senHal_pContSenModesRegsArray[22][1] 2 70001384
	{0x0F12, 0x0001},   //senHal_pContSenModesRegsArray[22][2] 2 70001386
	{0x0F12, 0x0001},   //senHal_pContSenModesRegsArray[22][3] 2 70001388
	{0x0F12, 0x03A2},   //senHal_pContSenModesRegsArray[23][0] 2 7000138A
	{0x0F12, 0x01D3},   //senHal_pContSenModesRegsArray[23][1] 2 7000138C
	{0x0F12, 0x01E0},   //senHal_pContSenModesRegsArray[23][2] 2 7000138E
	{0x0F12, 0x00F2},   //senHal_pContSenModesRegsArray[23][3] 2 70001390
	{0x0F12, 0x0000},   //senHal_pContSenModesRegsArray[24][0] 2 70001392
	{0x0F12, 0x0461},   //senHal_pContSenModesRegsArray[24][1] 2 70001394
	{0x0F12, 0x0000},   //senHal_pContSenModesRegsArray[24][2] 2 70001396
	{0x0F12, 0x029F},   //senHal_pContSenModesRegsArray[24][3] 2 70001398
	{0x0F12, 0x0000},   //senHal_pContSenModesRegsArray[25][0] 2 7000139A
	{0x0F12, 0x063C},   //senHal_pContSenModesRegsArray[25][1] 2 7000139C
	{0x0F12, 0x0000},   //senHal_pContSenModesRegsArray[25][2] 2 7000139E
	{0x0F12, 0x0399},   //senHal_pContSenModesRegsArray[25][3] 2 700013A0
	{0x0F12, 0x003D},   //senHal_pContSenModesRegsArray[26][0] 2 700013A2
	{0x0F12, 0x003D},   //senHal_pContSenModesRegsArray[26][1] 2 700013A4
	{0x0F12, 0x003D},   //senHal_pContSenModesRegsArray[26][2] 2 700013A6
	{0x0F12, 0x003D},   //senHal_pContSenModesRegsArray[26][3] 2 700013A8
	{0x0F12, 0x01D0},   //senHal_pContSenModesRegsArray[27][0] 2 700013AA
	{0x0F12, 0x01D0},   //senHal_pContSenModesRegsArray[27][1] 2 700013AC
	{0x0F12, 0x00EF},   //senHal_pContSenModesRegsArray[27][2] 2 700013AE
	{0x0F12, 0x00EF},   //senHal_pContSenModesRegsArray[27][3] 2 700013B0
	{0x0F12, 0x020C},   //senHal_pContSenModesRegsArray[28][0] 2 700013B2
	{0x0F12, 0x024B},   //senHal_pContSenModesRegsArray[28][1] 2 700013B4
	{0x0F12, 0x012B},   //senHal_pContSenModesRegsArray[28][2] 2 700013B6
	{0x0F12, 0x016A},   //senHal_pContSenModesRegsArray[28][3] 2 700013B8
	{0x0F12, 0x039F},   //senHal_pContSenModesRegsArray[29][0] 2 700013BA
	{0x0F12, 0x045E},   //senHal_pContSenModesRegsArray[29][1] 2 700013BC
	{0x0F12, 0x01DD},   //senHal_pContSenModesRegsArray[29][2] 2 700013BE
	{0x0F12, 0x029C},   //senHal_pContSenModesRegsArray[29][3] 2 700013C0
	{0x0F12, 0x041A},   //senHal_pContSenModesRegsArray[30][0] 2 700013C2
	{0x0F12, 0x04A6},   //senHal_pContSenModesRegsArray[30][1] 2 700013C4
	{0x0F12, 0x0258},   //senHal_pContSenModesRegsArray[30][2] 2 700013C6
	{0x0F12, 0x02E4},   //senHal_pContSenModesRegsArray[30][3] 2 700013C8
	{0x0F12, 0x062D},   //senHal_pContSenModesRegsArray[31][0] 2 700013CA
	{0x0F12, 0x0639},   //senHal_pContSenModesRegsArray[31][1] 2 700013CC
	{0x0F12, 0x038A},   //senHal_pContSenModesRegsArray[31][2] 2 700013CE
	{0x0F12, 0x0396},   //senHal_pContSenModesRegsArray[31][3] 2 700013D0
	{0x0F12, 0x0669},   //senHal_pContSenModesRegsArray[32][0] 2 700013D2
	{0x0F12, 0x06B4},   //senHal_pContSenModesRegsArray[32][1] 2 700013D4
	{0x0F12, 0x03C6},   //senHal_pContSenModesRegsArray[32][2] 2 700013D6
	{0x0F12, 0x0411},   //senHal_pContSenModesRegsArray[32][3] 2 700013D8
	{0x0F12, 0x087C},   //senHal_pContSenModesRegsArray[33][0] 2 700013DA
	{0x0F12, 0x08C7},   //senHal_pContSenModesRegsArray[33][1] 2 700013DC
	{0x0F12, 0x04F8},   //senHal_pContSenModesRegsArray[33][2] 2 700013DE
	{0x0F12, 0x0543},   //senHal_pContSenModesRegsArray[33][3] 2 700013E0
	{0x0F12, 0x0040},   //senHal_pContSenModesRegsArray[34][0] 2 700013E2
	{0x0F12, 0x0040},   //senHal_pContSenModesRegsArray[34][1] 2 700013E4
	{0x0F12, 0x0040},   //senHal_pContSenModesRegsArray[34][2] 2 700013E6
	{0x0F12, 0x0040},   //senHal_pContSenModesRegsArray[34][3] 2 700013E8
	{0x0F12, 0x01D0},   //senHal_pContSenModesRegsArray[35][0] 2 700013EA
	{0x0F12, 0x01D0},   //senHal_pContSenModesRegsArray[35][1] 2 700013EC
	{0x0F12, 0x00EF},   //senHal_pContSenModesRegsArray[35][2] 2 700013EE
	{0x0F12, 0x00EF},   //senHal_pContSenModesRegsArray[35][3] 2 700013F0
	{0x0F12, 0x020F},   //senHal_pContSenModesRegsArray[36][0] 2 700013F2
	{0x0F12, 0x024E},   //senHal_pContSenModesRegsArray[36][1] 2 700013F4
	{0x0F12, 0x012E},   //senHal_pContSenModesRegsArray[36][2] 2 700013F6
	{0x0F12, 0x016D},   //senHal_pContSenModesRegsArray[36][3] 2 700013F8
	{0x0F12, 0x039F},   //senHal_pContSenModesRegsArray[37][0] 2 700013FA
	{0x0F12, 0x045E},   //senHal_pContSenModesRegsArray[37][1] 2 700013FC
	{0x0F12, 0x01DD},   //senHal_pContSenModesRegsArray[37][2] 2 700013FE
	{0x0F12, 0x029C},   //senHal_pContSenModesRegsArray[37][3] 2 70001400
	{0x0F12, 0x041D},   //senHal_pContSenModesRegsArray[38][0] 2 70001402
	{0x0F12, 0x04A9},   //senHal_pContSenModesRegsArray[38][1] 2 70001404
	{0x0F12, 0x025B},   //senHal_pContSenModesRegsArray[38][2] 2 70001406
	{0x0F12, 0x02E7},   //senHal_pContSenModesRegsArray[38][3] 2 70001408
	{0x0F12, 0x062D},   //senHal_pContSenModesRegsArray[39][0] 2 7000140A
	{0x0F12, 0x0639},   //senHal_pContSenModesRegsArray[39][1] 2 7000140C
	{0x0F12, 0x038A},   //senHal_pContSenModesRegsArray[39][2] 2 7000140E
	{0x0F12, 0x0396},   //senHal_pContSenModesRegsArray[39][3] 2 70001410
	{0x0F12, 0x066C},   //senHal_pContSenModesRegsArray[40][0] 2 70001412
	{0x0F12, 0x06B7},   //senHal_pContSenModesRegsArray[40][1] 2 70001414
	{0x0F12, 0x03C9},   //senHal_pContSenModesRegsArray[40][2] 2 70001416
	{0x0F12, 0x0414},   //senHal_pContSenModesRegsArray[40][3] 2 70001418
	{0x0F12, 0x087C},   //senHal_pContSenModesRegsArray[41][0] 2 7000141A
	{0x0F12, 0x08C7},   //senHal_pContSenModesRegsArray[41][1] 2 7000141C
	{0x0F12, 0x04F8},   //senHal_pContSenModesRegsArray[41][2] 2 7000141E
	{0x0F12, 0x0543},   //senHal_pContSenModesRegsArray[41][3] 2 70001420
	{0x0F12, 0x0040},   //senHal_pContSenModesRegsArray[42][0] 2 70001422
	{0x0F12, 0x0040},   //senHal_pContSenModesRegsArray[42][1] 2 70001424
	{0x0F12, 0x0040},   //senHal_pContSenModesRegsArray[42][2] 2 70001426
	{0x0F12, 0x0040},   //senHal_pContSenModesRegsArray[42][3] 2 70001428
	{0x0F12, 0x01D0},   //senHal_pContSenModesRegsArray[43][0] 2 7000142A
	{0x0F12, 0x01D0},   //senHal_pContSenModesRegsArray[43][1] 2 7000142C
	{0x0F12, 0x00EF},   //senHal_pContSenModesRegsArray[43][2] 2 7000142E
	{0x0F12, 0x00EF},   //senHal_pContSenModesRegsArray[43][3] 2 70001430
	{0x0F12, 0x020F},   //senHal_pContSenModesRegsArray[44][0] 2 70001432
	{0x0F12, 0x024E},   //senHal_pContSenModesRegsArray[44][1] 2 70001434
	{0x0F12, 0x012E},   //senHal_pContSenModesRegsArray[44][2] 2 70001436
	{0x0F12, 0x016D},   //senHal_pContSenModesRegsArray[44][3] 2 70001438
	{0x0F12, 0x039F},   //senHal_pContSenModesRegsArray[45][0] 2 7000143A
	{0x0F12, 0x045E},   //senHal_pContSenModesRegsArray[45][1] 2 7000143C
	{0x0F12, 0x01DD},   //senHal_pContSenModesRegsArray[45][2] 2 7000143E
	{0x0F12, 0x029C},   //senHal_pContSenModesRegsArray[45][3] 2 70001440
	{0x0F12, 0x041D},   //senHal_pContSenModesRegsArray[46][0] 2 70001442
	{0x0F12, 0x04A9},   //senHal_pContSenModesRegsArray[46][1] 2 70001444
	{0x0F12, 0x025B},   //senHal_pContSenModesRegsArray[46][2] 2 70001446
	{0x0F12, 0x02E7},   //senHal_pContSenModesRegsArray[46][3] 2 70001448
	{0x0F12, 0x062D},   //senHal_pContSenModesRegsArray[47][0] 2 7000144A
	{0x0F12, 0x0639},   //senHal_pContSenModesRegsArray[47][1] 2 7000144C
	{0x0F12, 0x038A},   //senHal_pContSenModesRegsArray[47][2] 2 7000144E
	{0x0F12, 0x0396},   //senHal_pContSenModesRegsArray[47][3] 2 70001450
	{0x0F12, 0x066C},   //senHal_pContSenModesRegsArray[48][0] 2 70001452
	{0x0F12, 0x06B7},   //senHal_pContSenModesRegsArray[48][1] 2 70001454
	{0x0F12, 0x03C9},   //senHal_pContSenModesRegsArray[48][2] 2 70001456
	{0x0F12, 0x0414},   //senHal_pContSenModesRegsArray[48][3] 2 70001458
	{0x0F12, 0x087C},   //senHal_pContSenModesRegsArray[49][0] 2 7000145A
	{0x0F12, 0x08C7},   //senHal_pContSenModesRegsArray[49][1] 2 7000145C
	{0x0F12, 0x04F8},   //senHal_pContSenModesRegsArray[49][2] 2 7000145E
	{0x0F12, 0x0543},   //senHal_pContSenModesRegsArray[49][3] 2 70001460
	{0x0F12, 0x003D},   //senHal_pContSenModesRegsArray[50][0] 2 70001462
	{0x0F12, 0x003D},   //senHal_pContSenModesRegsArray[50][1] 2 70001464
	{0x0F12, 0x003D},   //senHal_pContSenModesRegsArray[50][2] 2 70001466
	{0x0F12, 0x003D},   //senHal_pContSenModesRegsArray[50][3] 2 70001468
	{0x0F12, 0x01D2},   //senHal_pContSenModesRegsArray[51][0] 2 7000146A
	{0x0F12, 0x01D2},   //senHal_pContSenModesRegsArray[51][1] 2 7000146C
	{0x0F12, 0x00F1},   //senHal_pContSenModesRegsArray[51][2] 2 7000146E
	{0x0F12, 0x00F1},   //senHal_pContSenModesRegsArray[51][3] 2 70001470
	{0x0F12, 0x020C},   //senHal_pContSenModesRegsArray[52][0] 2 70001472
	{0x0F12, 0x024B},   //senHal_pContSenModesRegsArray[52][1] 2 70001474
	{0x0F12, 0x012B},   //senHal_pContSenModesRegsArray[52][2] 2 70001476
	{0x0F12, 0x016A},   //senHal_pContSenModesRegsArray[52][3] 2 70001478
	{0x0F12, 0x03A1},   //senHal_pContSenModesRegsArray[53][0] 2 7000147A
	{0x0F12, 0x0460},   //senHal_pContSenModesRegsArray[53][1] 2 7000147C
	{0x0F12, 0x01DF},   //senHal_pContSenModesRegsArray[53][2] 2 7000147E
	{0x0F12, 0x029E},   //senHal_pContSenModesRegsArray[53][3] 2 70001480
	{0x0F12, 0x041A},   //senHal_pContSenModesRegsArray[54][0] 2 70001482
	{0x0F12, 0x04A6},   //senHal_pContSenModesRegsArray[54][1] 2 70001484
	{0x0F12, 0x0258},   //senHal_pContSenModesRegsArray[54][2] 2 70001486
	{0x0F12, 0x02E4},   //senHal_pContSenModesRegsArray[54][3] 2 70001488
	{0x0F12, 0x062F},   //senHal_pContSenModesRegsArray[55][0] 2 7000148A
	{0x0F12, 0x063B},   //senHal_pContSenModesRegsArray[55][1] 2 7000148C
	{0x0F12, 0x038C},   //senHal_pContSenModesRegsArray[55][2] 2 7000148E
	{0x0F12, 0x0398},   //senHal_pContSenModesRegsArray[55][3] 2 70001490
	{0x0F12, 0x0669},   //senHal_pContSenModesRegsArray[56][0] 2 70001492
	{0x0F12, 0x06B4},   //senHal_pContSenModesRegsArray[56][1] 2 70001494
	{0x0F12, 0x03C6},   //senHal_pContSenModesRegsArray[56][2] 2 70001496
	{0x0F12, 0x0411},   //senHal_pContSenModesRegsArray[56][3] 2 70001498
	{0x0F12, 0x087E},   //senHal_pContSenModesRegsArray[57][0] 2 7000149A
	{0x0F12, 0x08C9},   //senHal_pContSenModesRegsArray[57][1] 2 7000149C
	{0x0F12, 0x04FA},   //senHal_pContSenModesRegsArray[57][2] 2 7000149E
	{0x0F12, 0x0545},   //senHal_pContSenModesRegsArray[57][3] 2 700014A0
	{0x0F12, 0x03A2},   //senHal_pContSenModesRegsArray[58][0] 2 700014A2
	{0x0F12, 0x01D3},   //senHal_pContSenModesRegsArray[58][1] 2 700014A4
	{0x0F12, 0x01E0},   //senHal_pContSenModesRegsArray[58][2] 2 700014A6
	{0x0F12, 0x00F2},   //senHal_pContSenModesRegsArray[58][3] 2 700014A8
	{0x0F12, 0x03AF},   //senHal_pContSenModesRegsArray[59][0] 2 700014AA
	{0x0F12, 0x01E0},   //senHal_pContSenModesRegsArray[59][1] 2 700014AC
	{0x0F12, 0x01ED},   //senHal_pContSenModesRegsArray[59][2] 2 700014AE
	{0x0F12, 0x00FF},   //senHal_pContSenModesRegsArray[59][3] 2 700014B0
	{0x0F12, 0x0000},   //senHal_pContSenModesRegsArray[60][0] 2 700014B2
	{0x0F12, 0x0461},   //senHal_pContSenModesRegsArray[60][1] 2 700014B4
	{0x0F12, 0x0000},   //senHal_pContSenModesRegsArray[60][2] 2 700014B6
	{0x0F12, 0x029F},   //senHal_pContSenModesRegsArray[60][3] 2 700014B8
	{0x0F12, 0x0000},   //senHal_pContSenModesRegsArray[61][0] 2 700014BA
	{0x0F12, 0x046E},   //senHal_pContSenModesRegsArray[61][1] 2 700014BC
	{0x0F12, 0x0000},   //senHal_pContSenModesRegsArray[61][2] 2 700014BE
	{0x0F12, 0x02AC},   //senHal_pContSenModesRegsArray[61][3] 2 700014C0
	{0x0F12, 0x0000},   //senHal_pContSenModesRegsArray[62][0] 2 700014C2
	{0x0F12, 0x063C},   //senHal_pContSenModesRegsArray[62][1] 2 700014C4
	{0x0F12, 0x0000},   //senHal_pContSenModesRegsArray[62][2] 2 700014C6
	{0x0F12, 0x0399},   //senHal_pContSenModesRegsArray[62][3] 2 700014C8
	{0x0F12, 0x0000},   //senHal_pContSenModesRegsArray[63][0] 2 700014CA
	{0x0F12, 0x0649},   //senHal_pContSenModesRegsArray[63][1] 2 700014CC
	{0x0F12, 0x0000},   //senHal_pContSenModesRegsArray[63][2] 2 700014CE
	{0x0F12, 0x03A6},   //senHal_pContSenModesRegsArray[63][3] 2 700014D0
	{0x0F12, 0x0000},   //senHal_pContSenModesRegsArray[64][0] 2 700014D2
	{0x0F12, 0x0000},   //senHal_pContSenModesRegsArray[64][1] 2 700014D4
	{0x0F12, 0x0000},   //senHal_pContSenModesRegsArray[64][2] 2 700014D6
	{0x0F12, 0x0000},   //senHal_pContSenModesRegsArray[64][3] 2 700014D8
	{0x0F12, 0x0000},   //senHal_pContSenModesRegsArray[65][0] 2 700014DA
	{0x0F12, 0x0000},   //senHal_pContSenModesRegsArray[65][1] 2 700014DC
	{0x0F12, 0x0000},   //senHal_pContSenModesRegsArray[65][2] 2 700014DE
	{0x0F12, 0x0000},   //senHal_pContSenModesRegsArray[65][3] 2 700014E0
	{0x0F12, 0x03AA},   //senHal_pContSenModesRegsArray[66][0] 2 700014E2
	{0x0F12, 0x01DB},   //senHal_pContSenModesRegsArray[66][1] 2 700014E4
	{0x0F12, 0x01E8},   //senHal_pContSenModesRegsArray[66][2] 2 700014E6
	{0x0F12, 0x00FA},   //senHal_pContSenModesRegsArray[66][3] 2 700014E8
	{0x0F12, 0x03B7},   //senHal_pContSenModesRegsArray[67][0] 2 700014EA
	{0x0F12, 0x01E8},   //senHal_pContSenModesRegsArray[67][1] 2 700014EC
	{0x0F12, 0x01F5},   //senHal_pContSenModesRegsArray[67][2] 2 700014EE
	{0x0F12, 0x0107},   //senHal_pContSenModesRegsArray[67][3] 2 700014F0
	{0x0F12, 0x0000},   //senHal_pContSenModesRegsArray[68][0] 2 700014F2
	{0x0F12, 0x0469},   //senHal_pContSenModesRegsArray[68][1] 2 700014F4
	{0x0F12, 0x0000},   //senHal_pContSenModesRegsArray[68][2] 2 700014F6
	{0x0F12, 0x02A7},   //senHal_pContSenModesRegsArray[68][3] 2 700014F8
	{0x0F12, 0x0000},   //senHal_pContSenModesRegsArray[69][0] 2 700014FA
	{0x0F12, 0x0476},   //senHal_pContSenModesRegsArray[69][1] 2 700014FC
	{0x0F12, 0x0000},   //senHal_pContSenModesRegsArray[69][2] 2 700014FE
	{0x0F12, 0x02B4},   //senHal_pContSenModesRegsArray[69][3] 2 70001500
	{0x0F12, 0x0000},   //senHal_pContSenModesRegsArray[70][0] 2 70001502
	{0x0F12, 0x0644},   //senHal_pContSenModesRegsArray[70][1] 2 70001504
	{0x0F12, 0x0000},   //senHal_pContSenModesRegsArray[70][2] 2 70001506
	{0x0F12, 0x03A1},   //senHal_pContSenModesRegsArray[70][3] 2 70001508
	{0x0F12, 0x0000},   //senHal_pContSenModesRegsArray[71][0] 2 7000150A
	{0x0F12, 0x0651},   //senHal_pContSenModesRegsArray[71][1] 2 7000150C
	{0x0F12, 0x0000},   //senHal_pContSenModesRegsArray[71][2] 2 7000150E
	{0x0F12, 0x03AE},   //senHal_pContSenModesRegsArray[71][3] 2 70001510
	{0x0F12, 0x0000},   //senHal_pContSenModesRegsArray[72][0] 2 70001512
	{0x0F12, 0x0000},   //senHal_pContSenModesRegsArray[72][1] 2 70001514
	{0x0F12, 0x0000},   //senHal_pContSenModesRegsArray[72][2] 2 70001516
	{0x0F12, 0x0000},   //senHal_pContSenModesRegsArray[72][3] 2 70001518
	{0x0F12, 0x0000},   //senHal_pContSenModesRegsArray[73][0] 2 7000151A
	{0x0F12, 0x0000},   //senHal_pContSenModesRegsArray[73][1] 2 7000151C
	{0x0F12, 0x0000},   //senHal_pContSenModesRegsArray[73][2] 2 7000151E
	{0x0F12, 0x0000},   //senHal_pContSenModesRegsArray[73][3] 2 70001520
	{0x0F12, 0x0001},   //senHal_pContSenModesRegsArray[74][0] 2 70001522
	{0x0F12, 0x0001},   //senHal_pContSenModesRegsArray[74][1] 2 70001524
	{0x0F12, 0x0001},   //senHal_pContSenModesRegsArray[74][2] 2 70001526
	{0x0F12, 0x0001},   //senHal_pContSenModesRegsArray[74][3] 2 70001528
	{0x0F12, 0x000F},   //senHal_pContSenModesRegsArray[75][0] 2 7000152A
	{0x0F12, 0x000F},   //senHal_pContSenModesRegsArray[75][1] 2 7000152C
	{0x0F12, 0x000F},   //senHal_pContSenModesRegsArray[75][2] 2 7000152E
	{0x0F12, 0x000F},   //senHal_pContSenModesRegsArray[75][3] 2 70001530
	{0x0F12, 0x05AD},   //senHal_pContSenModesRegsArray[76][0] 2 70001532
	{0x0F12, 0x03DE},   //senHal_pContSenModesRegsArray[76][1] 2 70001534
	{0x0F12, 0x030A},   //senHal_pContSenModesRegsArray[76][2] 2 70001536
	{0x0F12, 0x021C},   //senHal_pContSenModesRegsArray[76][3] 2 70001538
	{0x0F12, 0x062F},   //senHal_pContSenModesRegsArray[77][0] 2 7000153A
	{0x0F12, 0x0460},   //senHal_pContSenModesRegsArray[77][1] 2 7000153C
	{0x0F12, 0x038C},   //senHal_pContSenModesRegsArray[77][2] 2 7000153E
	{0x0F12, 0x029E},   //senHal_pContSenModesRegsArray[77][3] 2 70001540
	{0x0F12, 0x07FC},   //senHal_pContSenModesRegsArray[78][0] 2 70001542
	{0x0F12, 0x0847},   //senHal_pContSenModesRegsArray[78][1] 2 70001544
	{0x0F12, 0x0478},   //senHal_pContSenModesRegsArray[78][2] 2 70001546
	{0x0F12, 0x04C3},   //senHal_pContSenModesRegsArray[78][3] 2 70001548
	{0x0F12, 0x0000},   //senHal_pContSenModesRegsArray[79][0] 2 7000154A
	{0x0F12, 0x0000},   //senHal_pContSenModesRegsArray[79][1] 2 7000154C
	{0x0F12, 0x0000},   //senHal_pContSenModesRegsArray[79][2] 2 7000154E
	{0x0F12, 0x0000},   //senHal_pContSenModesRegsArray[79][3] 2 70001550


	//============================================================ 
	// AF Interface setting
	//============================================================ 
    {0x0028, 0x7000},//DTS2012071201781: add by huyouhua 00136760 at 2012-7-20
	{0x002A, 0x01D4},
	{0x0F12, 0x0000}, //REG_TC_IPRM_AuxGpios : 0 - no Flash
	{0x002A, 0x01DE},
	{0x0F12, 0x0003}, //REG_TC_IPRM_CM_Init_AfModeType : 3 - AFD_VCM_I2C
	{0x0F12, 0x0000}, //REG_TC_IPRM_CM_Init_PwmConfig1 : 0 - no PWM
	{0x002A, 0x01E4},
	{0x0F12, 0x0041}, //REG_TC_IPRM_CM_Init_GpioConfig1 : 4 -  GPIO4 
	{0x002A, 0x01E8},
	{0x0F12, 0x2A0C}, //REG_TC_IPRM_CM_Init_Mi2cBits : MSCL - GPIO1 MSDA - GPIO2 Device ID {0C}
	{0x0F12, 0x0190}, //REG_TC_IPRM_CM_Init_Mi2cRateKhz : MI2C Speed - 400KHz
    //BEGIN: DTS2012071201781: add by huyouhua 00136760 at 2012-7-20
    {0x002A, 0x1196},  //afd_usFlags   0100=low,0000=high                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   
    {0x0F12, 0x0000},                                    
    //END: DTS2012071201781: add by huyouhua 00136760 at 2012-7-20
	//============================================================ 
	// AF Parameter setting
	//============================================================ 
	// AF Window Settings
	{0x002A, 0x025A},
	{0x0F12, 0x0100}, //#REG_TC_AF_FstWinStartX
    {0x0F12, 0x00E3}, //#REG_TC_AF_FstWinStartY //DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               
	{0x0F12, 0x0200}, //#REG_TC_AF_FstWinSizeX
	{0x0F12, 0x0238}, //#REG_TC_AF_FstWinSizeY
	{0x0F12, 0x018C}, //#REG_TC_AF_ScndWinStartX
    {0x0F12, 0x0166}, //#REG_TC_AF_ScndWinStartY //DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	{0x0F12, 0x00E6}, //#REG_TC_AF_ScndWinSizeX
	{0x0F12, 0x0132}, //#REG_TC_AF_ScndWinSizeY
	{0x0F12, 0x0001}, //#REG_TC_AF_WinSizesUpdated

	// AF Setot Settings 
	{0x002A, 0x0586},
	{0x0F12, 0x00FF}, //#skl_af_StatOvlpExpFactor

	// AF Scene Settings 
	{0x002A, 0x115E},
	{0x0F12, 0x0003}, //#af_scene_usSaturatedScene

	// AF Fine Search Settings 
	{0x002A, 0x10D4},
	{0x0F12, 0x1000}, //FineSearch Disable //#af_search_usSingleAfFlags
	{0x002A, 0x10DE},
	{0x0F12, 0x0004}, //#af_search_usFinePeakCount
	{0x002A, 0x106C},
	{0x0F12, 0x0202}, //#af_pos_usFineStepNumSize

	// AF Peak Threshold Setting
	{0x002A, 0x10CA}, //#af_search_usPeakThr
	{0x0F12, 0x00C0}, 

	// AF Default Position 
	{0x002A, 0x1060},
    {0x0F12, 0x003C}, //#af_pos_usHomePos //DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
    {0x0F12, 0x783C}, //#af_pos_usLowConfPos //DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20

	// AF LowConfThr Setting
	{0x002A, 0x10F4}, //LowEdgeBoth GRAD
	{0x0F12, 0x0280}, 
	{0x002A, 0x1100}, //LowLight HPF
	{0x0F12, 0x03A0},  
	{0x0F12, 0x0320}, 

	{0x002A, 0x1134},
	{0x0F12, 0x0030},//af_stat_usMinStatVal

	// AF low Br Th
	{0x002A, 0x1154}, // normBrThr
	{0x0F12, 0x0060},

	// AF Policy
	{0x002A, 0x10E2},
	{0x0F12, 0x0000}, //#af_search_usCapturePolicy: Focus_Priority, 0002 : Shutter_Priority_Fixed, 0001 : Shutter_Priority_Last_BFP 0000: Shutter_Priority_Current
	{0x002A, 0x1072},
	{0x0F12, 0x003C}, //#af_pos_usCaptureFixedPo// 0x0008

	// AF Lens Position Table Settings 
	{0x002A, 0x1074},
    //BEGIN: DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
    {0x0F12, 0x0010}, //#af_pos_usTableLastInd// 16 Steps 091222                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               
    {0x0F12, 0x003C}, //#af_pos_usTable_0_// af_pos_usTable                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    
    {0x0F12, 0x003F}, //#af_pos_usTable_1_                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     
    {0x0F12, 0x0042}, //#af_pos_usTable_2_                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     
    {0x0F12, 0x0045}, //#af_pos_usTable_3_                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     
    {0x0F12, 0x0048}, //#af_pos_usTable_4_                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     
    {0x0F12, 0x004B}, //#af_pos_usTable_5_                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     
    {0x0F12, 0x004E}, //#af_pos_usTable_6_                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     
    {0x0F12, 0x0051}, //#af_pos_usTable_7_                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     
    {0x0F12, 0x0054}, //#af_pos_usTable_8_                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     
    {0x0F12, 0x0057}, //#af_pos_usTable_9_                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     
    {0x0F12, 0x005A}, //#af_pos_usTable_10_                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    
    {0x0F12, 0x005E}, //#af_pos_usTable_11_                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    
    {0x0F12, 0x0061}, //#af_pos_usTable_12_                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    
    {0x0F12, 0x0064}, //#af_pos_usTable_13_                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    
    {0x0F12, 0x0068}, //#af_pos_usTable_14_                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    
    {0x0F12, 0x006C}, //#af_pos_usTable_15_                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    
    {0x0F12, 0x0078}, //#af_pos_usTable_16_                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    
    //END: DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	{0x002A, 0x0252},
	{0x0F12, 0x0003}, //init 
    //BEGIN: DTS2012071201781: del by huyouhua 00136760 at 2012-7-20
	//{0x002A, 0x12B8},   //disable CINTR 0                                 
	//{0x0F12, 0x1000},        
    //END: DTS2012071201781: del by huyouhua 00136760 at 2012-7-20
	//============================================================
	// ISP-FE Setting
	//============================================================                    
	{0x002A, 0x158A},   
	{0x0F12, 0xEAF0},   
	{0x002A, 0x15C6},   
	{0x0F12, 0x0020},   
	{0x0F12, 0x0060},   
	{0x002A, 0x15BC},   
	{0x0F12, 0x0200},   

	{0x002A, 0x1608},   
	{0x0F12, 0x0100},   
	{0x0F12, 0x0100},   
	{0x0F12, 0x0100},   
	{0x0F12, 0x0100},   
    //BEGIN: DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
    {0x002A, 0x0530},                              
    {0x0F12, 0x5DC0}, //#lt_uMaxExp1 60ms          
    {0x0F12, 0x0000},                              
    {0x0F12, 0x6D60}, //#lt_uMaxExp2 70ms          
    {0x0F12, 0x0000},                              
    {0x002A, 0x167C},                              
    {0x0F12, 0x9C40}, //#evt1_lt_uMaxExp3 100ms    
    {0x0F12, 0x0000},                              
    {0x0F12, 0x3880},//BB80 //#evt1_lt_uMaxExp4 120ms    
    {0x0F12, 0x0001},//0000                              
    // Set capture exposure time           
    {0x002A, 0x0538},                              
    {0x0F12, 0x5DC0}, //#lt_uCapMaxExp160ms        
    {0x0F12, 0x0000},                              
    {0x0F12, 0x6D60}, //#lt_uCapMaxExp270ms        
    {0x0F12, 0x0000},                              
    {0x002A, 0x1684},                              
    {0x0F12, 0x9C40}, //#evt1_lt_uCapMaxExp3 100ms 
    {0x0F12, 0x0000},                              
    {0x0F12, 0xBB80}, //#evt1_lt_uCapMaxExp4 120ms 
    {0x0F12, 0x0000},                              
    // Set gain                         
    {0x002A, 0x0540},                           
    {0x0F12, 0x0150}, //#lt_uMaxAnGain1         
    {0x0F12, 0x0280}, //#lt_uMaxAnGain2         
    {0x002A, 0x168C},                           
    {0x0F12, 0x02A0}, //#evt1_lt_uMaxAnGain3    
    {0x0F12, 0x0800}, //#evt1_lt_uMaxAnGain4    
                                  
    {0x002A, 0x0544},                           
    {0x0F12, 0x0100}, //#lt_uMaxDigGain         
    {0x0F12, 0x0A00}, //#lt_uMaxTotGain         

    {0x002A, 1694},                                 
    {0x0F12, 0001}, //#evt1_senHal_bExpandForbid    //expand forbidde zone

    //002A 051A                      
    //0F12 0111 //#lt_uLimitHigh          
    //0F12 00F0 //#lt_uLimitLow     

    //============================================================           
    //Tuning part                          
    //============================================================  
    //Calibrations go here
    //============================================================
    //Anti-Shading
    //============================================================
    //	param_start	TVAR_ash_GASAlpha
    {0x0028, 0x7000},
    {0x002A, 0x06CE},
    {0x0F12, 0x00FE},	//TVAR_ash_GASAlpha[0]
    {0x0F12, 0x00E0},	//TVAR_ash_GASAlpha[1]
    {0x0F12, 0x00F0},	//TVAR_ash_GASAlpha[2]
    {0x0F12, 0x00FE},	//TVAR_ash_GASAlpha[3]
    {0x0F12, 0x00F0},	//TVAR_ash_GASAlpha[4]
    {0x0F12, 0x00E0},	//TVAR_ash_GASAlpha[5]
    {0x0F12, 0x00C0},	//TVAR_ash_GASAlpha[6]
    {0x0F12, 0x00E0},	//TVAR_ash_GASAlpha[7]
    {0x0F12, 0x00EF},	//TVAR_ash_GASAlpha[8]
    {0x0F12, 0x00FF},	//TVAR_ash_GASAlpha[9]
    {0x0F12, 0x00F9},	//TVAR_ash_GASAlpha[10]
    {0x0F12, 0x00E0},	//TVAR_ash_GASAlpha[11]
    {0x0F12, 0x00CE},	//TVAR_ash_GASAlpha[12]
    {0x0F12, 0x00FF},	//TVAR_ash_GASAlpha[13]
    {0x0F12, 0x0100},	//TVAR_ash_GASAlpha[14]
    {0x0F12, 0x00E0},	//TVAR_ash_GASAlpha[15]
    {0x0F12, 0x00DF},	//TVAR_ash_GASAlpha[16]
    {0x0F12, 0x00F4},	//TVAR_ash_GASAlpha[17]
    {0x0F12, 0x00F3},	//TVAR_ash_GASAlpha[18]
    {0x0F12, 0x00F5},	//TVAR_ash_GASAlpha[19]
    {0x0F12, 0x00E0}, //TVAR_ash_GASAlpha[20]
    {0x0F12, 0x00E9},	//TVAR_ash_GASAlpha[21]
    {0x0F12, 0x00E6},	//TVAR_ash_GASAlpha[22]
    {0x0F12, 0x00F6},	//TVAR_ash_GASAlpha[23]
    {0x0F12, 0x00CE},	//TVAR_ash_GASAlpha[24]
    {0x0F12, 0x00E9},	//TVAR_ash_GASAlpha[25]
    {0x0F12, 0x00E6},	//TVAR_ash_GASAlpha[26]
    {0x0F12, 0x00F6},	//TVAR_ash_GASAlpha[27]
    //	param_end	TVAR_ash_GASAlpha
    //	param_start	TVAR_ash_GASOutdoorAlpha 
    {0x002A, 0x0706},
    {0x0F12, 0x00E4},	//TVAR_ash_GASOutdoorAlpha[0]
    {0x0F12, 0x00E9},	//TVAR_ash_GASOutdoorAlpha[1]
    {0x0F12, 0x00E6},	//TVAR_ash_GASOutdoorAlpha[2]
    {0x0F12, 0x00F6},	//TVAR_ash_GASOutdoorAlpha[3]
    //	param_end	TVAR_ash_GASOutdoorAlpha
    //	param_start	ash_GASBeta
    {0x002A, 0x070E},
    {0x0F12, 0x0060},	//ash_GASBeta[0]
    {0x0F12, 0x002B},	//ash_GASBeta[1]
    {0x0F12, 0x002F},	//ash_GASBeta[2]
    {0x0F12, 0x0000},	//ash_GASBeta[3]
    {0x0F12, 0x0052},	//ash_GASBeta[4]
    {0x0F12, 0x0026},	//ash_GASBeta[5]
    {0x0F12, 0x0029},	//ash_GASBeta[6]
    {0x0F12, 0x0000},	//ash_GASBeta[7]
    {0x0F12, 0x0010},	//ash_GASBeta[8]
    {0x0F12, 0x0026},	//ash_GASBeta[9]
    {0x0F12, 0x0028},	//ash_GASBeta[10]
    {0x0F12, 0x0000},	//ash_GASBeta[11]
    {0x0F12, 0x0010},	//ash_GASBeta[12]
    {0x0F12, 0x0026},	//ash_GASBeta[13]
    {0x0F12, 0x0027},	//ash_GASBeta[14]
    {0x0F12, 0x0000},	//ash_GASBeta[15]
    {0x0F12, 0x0023},	//ash_GASBeta[16]
    {0x0F12, 0x001F},	//ash_GASBeta[17]
    {0x0F12, 0x001F},	//ash_GASBeta[18]
    {0x0F12, 0x0000},	//ash_GASBeta[19]
    {0x0F12, 0x0025},	//ash_GASBeta[20]
    {0x0F12, 0x0017},	//ash_GASBeta[21]
    {0x0F12, 0x0016},	//ash_GASBeta[22]
    {0x0F12, 0x0000},	//ash_GASBeta[23]
    {0x0F12, 0x0025},	//ash_GASBeta[24]
    {0x0F12, 0x0017},	//ash_GASBeta[25]
    {0x0F12, 0x0016},	//ash_GASBeta[26]
    {0x0F12, 0x0000},	//ash_GASBeta[27]
    //	param_end	ash_GASBeta
    //	param_start	ash_GASOutdoorBeta 
    {0x002A, 0x0746},
    {0x0F12, 0x0025},	//ash_GASOutdoorBeta[0]
    {0x0F12, 0x0017},	//ash_GASOutdoorBeta[1]
    {0x0F12, 0x0016},	//ash_GASOutdoorBeta[2]
    {0x0F12, 0x0000},	//ash_GASOutdoorBeta[3]
    //	param_end	ash_GASOutdoorBeta
    {0x002A, 0x075A},
    {0x0F12, 0x0000},	//ash_bParabolicEstimation
    {0x0F12, 0x0400},	//ash_uParabolicCenterX
    {0x0F12, 0x0300},	//ash_uParabolicCenterY
    {0x0F12, 0x0010},	//ash_uParabolicScalingA
    {0x0F12, 0x0011},	//ash_uParabolicScalingB
    //	param_start	TVAR_ash_pGAS
    {0x002A,    0x347C},
    {0x0F12,	0x025B},	//TVAR_ash_pGAS[0]
    {0x0F12,	0x01B4},	//TVAR_ash_pGAS[1]
    {0x0F12,	0x0156},	//TVAR_ash_pGAS[2]
    {0x0F12,	0x0112},	//TVAR_ash_pGAS[3]
    {0x0F12,	0x00EE},	//TVAR_ash_pGAS[4]
    {0x0F12,	0x00D8},	//TVAR_ash_pGAS[5]
    {0x0F12,	0x00CE},	//TVAR_ash_pGAS[6]
    {0x0F12,	0x00D4},	//TVAR_ash_pGAS[7]
    {0x0F12,	0x00E6},	//TVAR_ash_pGAS[8]
    {0x0F12,	0x010B},	//TVAR_ash_pGAS[9]
    {0x0F12,	0x0141},	//TVAR_ash_pGAS[10]
    {0x0F12,	0x019F},	//TVAR_ash_pGAS[11]
    {0x0F12,	0x027A},	//TVAR_ash_pGAS[12]
    {0x0F12,	0x01CD},	//TVAR_ash_pGAS[13]
    {0x0F12,	0x0174},	//TVAR_ash_pGAS[14]
    {0x0F12,	0x0115},	//TVAR_ash_pGAS[15]
    {0x0F12,	0x00D8},	//TVAR_ash_pGAS[16]
    {0x0F12,	0x00B2},	//TVAR_ash_pGAS[17]
    {0x0F12,	0x009C},	//TVAR_ash_pGAS[18]
    {0x0F12,	0x0092},	//TVAR_ash_pGAS[19]
    {0x0F12,	0x009A},	//TVAR_ash_pGAS[20]
    {0x0F12,	0x00B0},	//TVAR_ash_pGAS[21]
    {0x0F12,	0x00D9},	//TVAR_ash_pGAS[22]
    {0x0F12,	0x0111},	//TVAR_ash_pGAS[23]
    {0x0F12,	0x015F},	//TVAR_ash_pGAS[24]
    {0x0F12,	0x01DD},	//TVAR_ash_pGAS[25]
    {0x0F12,	0x0176},	//TVAR_ash_pGAS[26]
    {0x0F12,	0x0128},	//TVAR_ash_pGAS[27]
    {0x0F12,	0x00D1},	//TVAR_ash_pGAS[28]
    {0x0F12,	0x0097},	//TVAR_ash_pGAS[29]
    {0x0F12,	0x006F},	//TVAR_ash_pGAS[30]
    {0x0F12,	0x0056},	//TVAR_ash_pGAS[31]
    {0x0F12,	0x004D},	//TVAR_ash_pGAS[32]
    {0x0F12,	0x0057},	//TVAR_ash_pGAS[33]
    {0x0F12,	0x0073},	//TVAR_ash_pGAS[34]
    {0x0F12,	0x009E},	//TVAR_ash_pGAS[35]
    {0x0F12,	0x00D9},	//TVAR_ash_pGAS[36]
    {0x0F12,	0x0122},	//TVAR_ash_pGAS[37]
    {0x0F12,	0x0173},	//TVAR_ash_pGAS[38]
    {0x0F12,	0x0144},	//TVAR_ash_pGAS[39]
    {0x0F12,	0x00FE},	//TVAR_ash_pGAS[40]
    {0x0F12,	0x00A9},	//TVAR_ash_pGAS[41]
    {0x0F12,	0x006F},	//TVAR_ash_pGAS[42]
    {0x0F12,	0x0043},	//TVAR_ash_pGAS[43]
    {0x0F12,	0x0028},	//TVAR_ash_pGAS[44]
    {0x0F12,	0x001F},	//TVAR_ash_pGAS[45]
    {0x0F12,	0x002A},	//TVAR_ash_pGAS[46]
    {0x0F12,	0x0049},	//TVAR_ash_pGAS[47]
    {0x0F12,	0x0079},	//TVAR_ash_pGAS[48]
    {0x0F12,	0x00B6},	//TVAR_ash_pGAS[49]
    {0x0F12,	0x0102},	//TVAR_ash_pGAS[50]
    {0x0F12,	0x014F},	//TVAR_ash_pGAS[51]
    {0x0F12,	0x012D},	//TVAR_ash_pGAS[52]
    {0x0F12,	0x00E7},	//TVAR_ash_pGAS[53]
    {0x0F12,	0x0094},	//TVAR_ash_pGAS[54]
    {0x0F12,	0x0056},	//TVAR_ash_pGAS[55]
    {0x0F12,	0x0028},	//TVAR_ash_pGAS[56]
    {0x0F12,	0x000E},	//TVAR_ash_pGAS[57]
    {0x0F12,	0x0007},	//TVAR_ash_pGAS[58]
    {0x0F12,	0x0011},	//TVAR_ash_pGAS[59]
    {0x0F12,	0x0030},	//TVAR_ash_pGAS[60]
    {0x0F12,	0x0063},	//TVAR_ash_pGAS[61]
    {0x0F12,	0x00A6},	//TVAR_ash_pGAS[62]
    {0x0F12,	0x00F2},	//TVAR_ash_pGAS[63]
    {0x0F12,	0x013D},	//TVAR_ash_pGAS[64]
    {0x0F12,	0x0126},	//TVAR_ash_pGAS[65]
    {0x0F12,	0x00E1},	//TVAR_ash_pGAS[66]
    {0x0F12,	0x008D},	//TVAR_ash_pGAS[67]
    {0x0F12,	0x004E},	//TVAR_ash_pGAS[68]
    {0x0F12,	0x0022},	//TVAR_ash_pGAS[69]
    {0x0F12,	0x0007},	//TVAR_ash_pGAS[70]
    {0x0F12,	0x0000},	//TVAR_ash_pGAS[71]
    {0x0F12,	0x000A},	//TVAR_ash_pGAS[72]
    {0x0F12,	0x0028},	//TVAR_ash_pGAS[73]
    {0x0F12,	0x005E},	//TVAR_ash_pGAS[74]
    {0x0F12,	0x00A3},	//TVAR_ash_pGAS[75]
    {0x0F12,	0x00F0},	//TVAR_ash_pGAS[76]
    {0x0F12,	0x013B},	//TVAR_ash_pGAS[77]
    {0x0F12,	0x0133},	//TVAR_ash_pGAS[78]
    {0x0F12,	0x00EE},	//TVAR_ash_pGAS[79]
    {0x0F12,	0x0099},	//TVAR_ash_pGAS[80]
    {0x0F12,	0x005A},	//TVAR_ash_pGAS[81]
    {0x0F12,	0x002D},	//TVAR_ash_pGAS[82]
    {0x0F12,	0x0013},	//TVAR_ash_pGAS[83]
    {0x0F12,	0x000B},	//TVAR_ash_pGAS[84]
    {0x0F12,	0x0016},	//TVAR_ash_pGAS[85]
    {0x0F12,	0x0035},	//TVAR_ash_pGAS[86]
    {0x0F12,	0x006C},	//TVAR_ash_pGAS[87]
    {0x0F12,	0x00B2},	//TVAR_ash_pGAS[88]
    {0x0F12,	0x0100},	//TVAR_ash_pGAS[89]
    {0x0F12,	0x0148},	//TVAR_ash_pGAS[90]
    {0x0F12,	0x0153},	//TVAR_ash_pGAS[91]
    {0x0F12,	0x0107},	//TVAR_ash_pGAS[92]
    {0x0F12,	0x00B4},	//TVAR_ash_pGAS[93]
    {0x0F12,	0x0078},	//TVAR_ash_pGAS[94]
    {0x0F12,	0x004B},	//TVAR_ash_pGAS[95]
    {0x0F12,	0x0030},	//TVAR_ash_pGAS[96]
    {0x0F12,	0x002A},	//TVAR_ash_pGAS[97]
    {0x0F12,	0x0036},	//TVAR_ash_pGAS[98]
    {0x0F12,	0x0057},	//TVAR_ash_pGAS[99]
    {0x0F12,	0x008B},	//TVAR_ash_pGAS[100]
    {0x0F12,	0x00D1},	//TVAR_ash_pGAS[101]
    {0x0F12,	0x0121},	//TVAR_ash_pGAS[102]
    {0x0F12,	0x016A},	//TVAR_ash_pGAS[103]
    {0x0F12,	0x0189},	//TVAR_ash_pGAS[104]
    {0x0F12,	0x0138},	//TVAR_ash_pGAS[105]
    {0x0F12,	0x00DF},	//TVAR_ash_pGAS[106]
    {0x0F12,	0x00A8},	//TVAR_ash_pGAS[107]
    {0x0F12,	0x007E},	//TVAR_ash_pGAS[108]
    {0x0F12,	0x0065},	//TVAR_ash_pGAS[109]
    {0x0F12,	0x005F},	//TVAR_ash_pGAS[110]
    {0x0F12,	0x006A},	//TVAR_ash_pGAS[111]
    {0x0F12,	0x008C},	//TVAR_ash_pGAS[112]
    {0x0F12,	0x00BF},	//TVAR_ash_pGAS[113]
    {0x0F12,	0x00FE},	//TVAR_ash_pGAS[114]
    {0x0F12,	0x0150},	//TVAR_ash_pGAS[115]
    {0x0F12,	0x019E},	//TVAR_ash_pGAS[116]
    {0x0F12,	0x01D5},	//TVAR_ash_pGAS[117]
    {0x0F12,	0x0181},	//TVAR_ash_pGAS[118]
    {0x0F12,	0x0125},	//TVAR_ash_pGAS[119]
    {0x0F12,	0x00EA},	//TVAR_ash_pGAS[120]
    {0x0F12,	0x00C7},	//TVAR_ash_pGAS[121]
    {0x0F12,	0x00B2},	//TVAR_ash_pGAS[122]
    {0x0F12,	0x00AC},	//TVAR_ash_pGAS[123]
    {0x0F12,	0x00BA},	//TVAR_ash_pGAS[124]
    {0x0F12,	0x00D5},	//TVAR_ash_pGAS[125]
    {0x0F12,	0x0100},	//TVAR_ash_pGAS[126]
    {0x0F12,	0x0140},	//TVAR_ash_pGAS[127]
    {0x0F12,	0x019B},	//TVAR_ash_pGAS[128]
    {0x0F12,	0x0217},	//TVAR_ash_pGAS[129]
    {0x0F12,	0x0280},	//TVAR_ash_pGAS[130]
    {0x0F12,	0x01CC},	//TVAR_ash_pGAS[131]
    {0x0F12,	0x0176},	//TVAR_ash_pGAS[132]
    {0x0F12,	0x0131},	//TVAR_ash_pGAS[133]
    {0x0F12,	0x010E},	//TVAR_ash_pGAS[134]
    {0x0F12,	0x00FF},	//TVAR_ash_pGAS[135]
    {0x0F12,	0x00FC},	//TVAR_ash_pGAS[136]
    {0x0F12,	0x0103},	//TVAR_ash_pGAS[137]
    {0x0F12,	0x011B},	//TVAR_ash_pGAS[138]
    {0x0F12,	0x0146},	//TVAR_ash_pGAS[139]
    {0x0F12,	0x018B},	//TVAR_ash_pGAS[140]
    {0x0F12,	0x01F8},	//TVAR_ash_pGAS[141]
    {0x0F12,	0x02DD},	//TVAR_ash_pGAS[142]
    {0x0F12,	0x0232},	//TVAR_ash_pGAS[143]
    {0x0F12,	0x018A},	//TVAR_ash_pGAS[144]
    {0x0F12,	0x0127},	//TVAR_ash_pGAS[145]
    {0x0F12,	0x00EF},	//TVAR_ash_pGAS[146]
    {0x0F12,	0x00CA},	//TVAR_ash_pGAS[147]
    {0x0F12,	0x00B4},	//TVAR_ash_pGAS[148]
    {0x0F12,	0x00AB},	//TVAR_ash_pGAS[149]
    {0x0F12,	0x00B1},	//TVAR_ash_pGAS[150]
    {0x0F12,	0x00C3},	//TVAR_ash_pGAS[151]
    {0x0F12,	0x00E5},	//TVAR_ash_pGAS[152]
    {0x0F12,	0x0118},	//TVAR_ash_pGAS[153]
    {0x0F12,	0x0168},	//TVAR_ash_pGAS[154]
    {0x0F12,	0x023A},	//TVAR_ash_pGAS[155]
    {0x0F12,	0x019A},	//TVAR_ash_pGAS[156]
    {0x0F12,	0x013E},	//TVAR_ash_pGAS[157]
    {0x0F12,	0x00E5},	//TVAR_ash_pGAS[158]
    {0x0F12,	0x00B1},	//TVAR_ash_pGAS[159]
    {0x0F12,	0x008E},	//TVAR_ash_pGAS[160]
    {0x0F12,	0x0079},	//TVAR_ash_pGAS[161]
    {0x0F12,	0x0072},	//TVAR_ash_pGAS[162]
    {0x0F12,	0x0078},	//TVAR_ash_pGAS[163]
    {0x0F12,	0x008E},	//TVAR_ash_pGAS[164]
    {0x0F12,	0x00B2},	//TVAR_ash_pGAS[165]
    {0x0F12,	0x00E1},	//TVAR_ash_pGAS[166]
    {0x0F12,	0x0121},	//TVAR_ash_pGAS[167]
    {0x0F12,	0x019A},	//TVAR_ash_pGAS[168]
    {0x0F12,	0x014D},	//TVAR_ash_pGAS[169]
    {0x0F12,	0x0100},	//TVAR_ash_pGAS[170]
    {0x0F12,	0x00AE},	//TVAR_ash_pGAS[171]
    {0x0F12,	0x007F},	//TVAR_ash_pGAS[172]
    {0x0F12,	0x005B},	//TVAR_ash_pGAS[173]
    {0x0F12,	0x0043},	//TVAR_ash_pGAS[174]
    {0x0F12,	0x003B},	//TVAR_ash_pGAS[175]
    {0x0F12,	0x0045},	//TVAR_ash_pGAS[176]
    {0x0F12,	0x005E},	//TVAR_ash_pGAS[177]
    {0x0F12,	0x0083},	//TVAR_ash_pGAS[178]
    {0x0F12,	0x00B4},	//TVAR_ash_pGAS[179]
    {0x0F12,	0x00EF},	//TVAR_ash_pGAS[180]
    {0x0F12,	0x0139},	//TVAR_ash_pGAS[181]
    {0x0F12,	0x0120},	//TVAR_ash_pGAS[182]
    {0x0F12,	0x00DB},	//TVAR_ash_pGAS[183]
    {0x0F12,	0x008D},	//TVAR_ash_pGAS[184]
    {0x0F12,	0x005D},	//TVAR_ash_pGAS[185]
    {0x0F12,	0x0037},	//TVAR_ash_pGAS[186]
    {0x0F12,	0x001F},	//TVAR_ash_pGAS[187]
    {0x0F12,	0x0017},	//TVAR_ash_pGAS[188]
    {0x0F12,	0x0022},	//TVAR_ash_pGAS[189]
    {0x0F12,	0x003D},	//TVAR_ash_pGAS[190]
    {0x0F12,	0x0066},	//TVAR_ash_pGAS[191]
    {0x0F12,	0x009A},	//TVAR_ash_pGAS[192]
    {0x0F12,	0x00D7},	//TVAR_ash_pGAS[193]
    {0x0F12,	0x011A},	//TVAR_ash_pGAS[194]
    {0x0F12,	0x0108},	//TVAR_ash_pGAS[195]
    {0x0F12,	0x00C7},	//TVAR_ash_pGAS[196]
    {0x0F12,	0x007B},	//TVAR_ash_pGAS[197]
    {0x0F12,	0x004A},	//TVAR_ash_pGAS[198]
    {0x0F12,	0x0023},	//TVAR_ash_pGAS[199]
    {0x0F12,	0x000B},	//TVAR_ash_pGAS[200]
    {0x0F12,	0x0005},	//TVAR_ash_pGAS[201]
    {0x0F12,	0x000E},	//TVAR_ash_pGAS[202]
    {0x0F12,	0x002A},	//TVAR_ash_pGAS[203]
    {0x0F12,	0x0055},	//TVAR_ash_pGAS[204]
    {0x0F12,	0x008D},	//TVAR_ash_pGAS[205]
    {0x0F12,	0x00C9},	//TVAR_ash_pGAS[206]
    {0x0F12,	0x0104},	//TVAR_ash_pGAS[207]
    {0x0F12,	0x0100},	//TVAR_ash_pGAS[208]
    {0x0F12,	0x00C0},	//TVAR_ash_pGAS[209]
    {0x0F12,	0x0076},	//TVAR_ash_pGAS[210]
    {0x0F12,	0x0044},	//TVAR_ash_pGAS[211]
    {0x0F12,	0x001E},	//TVAR_ash_pGAS[212]
    {0x0F12,	0x0006},	//TVAR_ash_pGAS[213]
    {0x0F12,	0x0000},	//TVAR_ash_pGAS[214]
    {0x0F12,	0x0009},	//TVAR_ash_pGAS[215]
    {0x0F12,	0x0024},	//TVAR_ash_pGAS[216]
    {0x0F12,	0x0052},	//TVAR_ash_pGAS[217]
    {0x0F12,	0x008B},	//TVAR_ash_pGAS[218]
    {0x0F12,	0x00C8},	//TVAR_ash_pGAS[219]
    {0x0F12,	0x0104},	//TVAR_ash_pGAS[220]
    {0x0F12,	0x0109},	//TVAR_ash_pGAS[221]
    {0x0F12,	0x00C9},	//TVAR_ash_pGAS[222]
    {0x0F12,	0x007F},	//TVAR_ash_pGAS[223]
    {0x0F12,	0x004D},	//TVAR_ash_pGAS[224]
    {0x0F12,	0x0027},	//TVAR_ash_pGAS[225]
    {0x0F12,	0x0010},	//TVAR_ash_pGAS[226]
    {0x0F12,	0x000A},	//TVAR_ash_pGAS[227]
    {0x0F12,	0x0014},	//TVAR_ash_pGAS[228]
    {0x0F12,	0x002F},	//TVAR_ash_pGAS[229]
    {0x0F12,	0x005D},	//TVAR_ash_pGAS[230]
    {0x0F12,	0x0098},	//TVAR_ash_pGAS[231]
    {0x0F12,	0x00D7},	//TVAR_ash_pGAS[232]
    {0x0F12,	0x010E},	//TVAR_ash_pGAS[233]
    {0x0F12,	0x0125},	//TVAR_ash_pGAS[234]
    {0x0F12,	0x00E0},	//TVAR_ash_pGAS[235]
    {0x0F12,	0x0094},	//TVAR_ash_pGAS[236]
    {0x0F12,	0x0066},	//TVAR_ash_pGAS[237]
    {0x0F12,	0x0040},	//TVAR_ash_pGAS[238]
    {0x0F12,	0x002A},	//TVAR_ash_pGAS[239]
    {0x0F12,	0x0024},	//TVAR_ash_pGAS[240]
    {0x0F12,	0x002F},	//TVAR_ash_pGAS[241]
    {0x0F12,	0x004C},	//TVAR_ash_pGAS[242]
    {0x0F12,	0x0078},	//TVAR_ash_pGAS[243]
    {0x0F12,	0x00B3},	//TVAR_ash_pGAS[244]
    {0x0F12,	0x00F4},	//TVAR_ash_pGAS[245]
    {0x0F12,	0x012F},	//TVAR_ash_pGAS[246]
    {0x0F12,	0x0153},	//TVAR_ash_pGAS[247]
    {0x0F12,	0x0108},	//TVAR_ash_pGAS[248]
    {0x0F12,	0x00B9},	//TVAR_ash_pGAS[249]
    {0x0F12,	0x008D},	//TVAR_ash_pGAS[250]
    {0x0F12,	0x006B},	//TVAR_ash_pGAS[251]
    {0x0F12,	0x0055},	//TVAR_ash_pGAS[252]
    {0x0F12,	0x0050},	//TVAR_ash_pGAS[253]
    {0x0F12,	0x005D},	//TVAR_ash_pGAS[254]
    {0x0F12,	0x007A},	//TVAR_ash_pGAS[255]
    {0x0F12,	0x00A4},	//TVAR_ash_pGAS[256]
    {0x0F12,	0x00D9},	//TVAR_ash_pGAS[257]
    {0x0F12,	0x011D},	//TVAR_ash_pGAS[258]
    {0x0F12,	0x015E},	//TVAR_ash_pGAS[259]
    {0x0F12,	0x019C},	//TVAR_ash_pGAS[260]
    {0x0F12,	0x0144},	//TVAR_ash_pGAS[261]
    {0x0F12,	0x00F3},	//TVAR_ash_pGAS[262]
    {0x0F12,	0x00C2},	//TVAR_ash_pGAS[263]
    {0x0F12,	0x00A4},	//TVAR_ash_pGAS[264]
    {0x0F12,	0x0093},	//TVAR_ash_pGAS[265]
    {0x0F12,	0x008F},	//TVAR_ash_pGAS[266]
    {0x0F12,	0x009C},	//TVAR_ash_pGAS[267]
    {0x0F12,	0x00B5},	//TVAR_ash_pGAS[268]
    {0x0F12,	0x00DA},	//TVAR_ash_pGAS[269]
    {0x0F12,	0x010E},	//TVAR_ash_pGAS[270]
    {0x0F12,	0x015E},	//TVAR_ash_pGAS[271]
    {0x0F12,	0x01C9},	//TVAR_ash_pGAS[272]
    {0x0F12,	0x0239},	//TVAR_ash_pGAS[273]
    {0x0F12,	0x0186},	//TVAR_ash_pGAS[274]
    {0x0F12,	0x0137},	//TVAR_ash_pGAS[275]
    {0x0F12,	0x00FF},	//TVAR_ash_pGAS[276]
    {0x0F12,	0x00DE},	//TVAR_ash_pGAS[277]
    {0x0F12,	0x00CF},	//TVAR_ash_pGAS[278]
    {0x0F12,	0x00CE},	//TVAR_ash_pGAS[279]
    {0x0F12,	0x00D5},	//TVAR_ash_pGAS[280]
    {0x0F12,	0x00ED},	//TVAR_ash_pGAS[281]
    {0x0F12,	0x0113},	//TVAR_ash_pGAS[282]
    {0x0F12,	0x014E},	//TVAR_ash_pGAS[283]
    {0x0F12,	0x01B0},	//TVAR_ash_pGAS[284]
    {0x0F12,	0x0285},	//TVAR_ash_pGAS[285]
    {0x0F12,	0x020F},	//TVAR_ash_pGAS[286]
    {0x0F12,	0x0173},	//TVAR_ash_pGAS[287]
    {0x0F12,	0x0112},	//TVAR_ash_pGAS[288]
    {0x0F12,	0x00DE},	//TVAR_ash_pGAS[289]
    {0x0F12,	0x00BF},	//TVAR_ash_pGAS[290]
    {0x0F12,	0x00AD},	//TVAR_ash_pGAS[291]
    {0x0F12,	0x00A8},	//TVAR_ash_pGAS[292]
    {0x0F12,	0x00B3},	//TVAR_ash_pGAS[293]
    {0x0F12,	0x00CC},	//TVAR_ash_pGAS[294]
    {0x0F12,	0x00F2},	//TVAR_ash_pGAS[295]
    {0x0F12,	0x0126},	//TVAR_ash_pGAS[296]
    {0x0F12,	0x0176},	//TVAR_ash_pGAS[297]
    {0x0F12,	0x024B},	//TVAR_ash_pGAS[298]
    {0x0F12,	0x0180},	//TVAR_ash_pGAS[299]
    {0x0F12,	0x012F},	//TVAR_ash_pGAS[300]
    {0x0F12,	0x00D5},	//TVAR_ash_pGAS[301]
    {0x0F12,	0x00A7},	//TVAR_ash_pGAS[302]
    {0x0F12,	0x0087},	//TVAR_ash_pGAS[303]
    {0x0F12,	0x0075},	//TVAR_ash_pGAS[304]
    {0x0F12,	0x0072},	//TVAR_ash_pGAS[305]
    {0x0F12,	0x007E},	//TVAR_ash_pGAS[306]
    {0x0F12,	0x0096},	//TVAR_ash_pGAS[307]
    {0x0F12,	0x00BE},	//TVAR_ash_pGAS[308]
    {0x0F12,	0x00EF},	//TVAR_ash_pGAS[309]
    {0x0F12,	0x012F},	//TVAR_ash_pGAS[310]
    {0x0F12,	0x01A7},	//TVAR_ash_pGAS[311]
    {0x0F12,	0x013C},	//TVAR_ash_pGAS[312]
    {0x0F12,	0x00F5},	//TVAR_ash_pGAS[313]
    {0x0F12,	0x00A5},	//TVAR_ash_pGAS[314]
    {0x0F12,	0x0078},	//TVAR_ash_pGAS[315]
    {0x0F12,	0x0057},	//TVAR_ash_pGAS[316]
    {0x0F12,	0x0042},	//TVAR_ash_pGAS[317]
    {0x0F12,	0x003D},	//TVAR_ash_pGAS[318]
    {0x0F12,	0x0049},	//TVAR_ash_pGAS[319]
    {0x0F12,	0x0066},	//TVAR_ash_pGAS[320]
    {0x0F12,	0x008F},	//TVAR_ash_pGAS[321]
    {0x0F12,	0x00C0},	//TVAR_ash_pGAS[322]
    {0x0F12,	0x00FD},	//TVAR_ash_pGAS[323]
    {0x0F12,	0x0144},	//TVAR_ash_pGAS[324]
    {0x0F12,	0x0115},	//TVAR_ash_pGAS[325]
    {0x0F12,	0x00D6},	//TVAR_ash_pGAS[326]
    {0x0F12,	0x0089},	//TVAR_ash_pGAS[327]
    {0x0F12,	0x005B},	//TVAR_ash_pGAS[328]
    {0x0F12,	0x0036},	//TVAR_ash_pGAS[329]
    {0x0F12,	0x001F},	//TVAR_ash_pGAS[330]
    {0x0F12,	0x0018},	//TVAR_ash_pGAS[331]
    {0x0F12,	0x0024},	//TVAR_ash_pGAS[332]
    {0x0F12,	0x0043},	//TVAR_ash_pGAS[333]
    {0x0F12,	0x006F},	//TVAR_ash_pGAS[334]
    {0x0F12,	0x00A5},	//TVAR_ash_pGAS[335]
    {0x0F12,	0x00E0},	//TVAR_ash_pGAS[336]
    {0x0F12,	0x0122},	//TVAR_ash_pGAS[337]
    {0x0F12,	0x0104},	//TVAR_ash_pGAS[338]
    {0x0F12,	0x00C7},	//TVAR_ash_pGAS[339]
    {0x0F12,	0x007B},	//TVAR_ash_pGAS[340]
    {0x0F12,	0x004A},	//TVAR_ash_pGAS[341]
    {0x0F12,	0x0023},	//TVAR_ash_pGAS[342]
    {0x0F12,	0x000B},	//TVAR_ash_pGAS[343]
    {0x0F12,	0x0005},	//TVAR_ash_pGAS[344]
    {0x0F12,	0x0010},	//TVAR_ash_pGAS[345]
    {0x0F12,	0x002E},	//TVAR_ash_pGAS[346]
    {0x0F12,	0x005B},	//TVAR_ash_pGAS[347]
    {0x0F12,	0x0094},	//TVAR_ash_pGAS[348]
    {0x0F12,	0x00D0},	//TVAR_ash_pGAS[349]
    {0x0F12,	0x010C},	//TVAR_ash_pGAS[350]
    {0x0F12,	0x0105},	//TVAR_ash_pGAS[351]
    {0x0F12,	0x00C5},	//TVAR_ash_pGAS[352]
    {0x0F12,	0x007A},	//TVAR_ash_pGAS[353]
    {0x0F12,	0x0047},	//TVAR_ash_pGAS[354]
    {0x0F12,	0x0021},	//TVAR_ash_pGAS[355]
    {0x0F12,	0x0008},	//TVAR_ash_pGAS[356]
    {0x0F12,	0x0000},	//TVAR_ash_pGAS[357]
    {0x0F12,	0x0009},	//TVAR_ash_pGAS[358]
    {0x0F12,	0x0026},	//TVAR_ash_pGAS[359]
    {0x0F12,	0x0054},	//TVAR_ash_pGAS[360]
    {0x0F12,	0x008E},	//TVAR_ash_pGAS[361]
    {0x0F12,	0x00CA},	//TVAR_ash_pGAS[362]
    {0x0F12,	0x0106},	//TVAR_ash_pGAS[363]
    {0x0F12,	0x0116},	//TVAR_ash_pGAS[364]
    {0x0F12,	0x00D3},	//TVAR_ash_pGAS[365]
    {0x0F12,	0x0088},	//TVAR_ash_pGAS[366]
    {0x0F12,	0x0054},	//TVAR_ash_pGAS[367]
    {0x0F12,	0x002C},	//TVAR_ash_pGAS[368]
    {0x0F12,	0x0012},	//TVAR_ash_pGAS[369]
    {0x0F12,	0x0009},	//TVAR_ash_pGAS[370]
    {0x0F12,	0x0013},	//TVAR_ash_pGAS[371]
    {0x0F12,	0x002E},	//TVAR_ash_pGAS[372]
    {0x0F12,	0x005C},	//TVAR_ash_pGAS[373]
    {0x0F12,	0x0095},	//TVAR_ash_pGAS[374]
    {0x0F12,	0x00D4},	//TVAR_ash_pGAS[375]
    {0x0F12,	0x010D},	//TVAR_ash_pGAS[376]
    {0x0F12,	0x013A},	//TVAR_ash_pGAS[377]
    {0x0F12,	0x00EE},	//TVAR_ash_pGAS[378]
    {0x0F12,	0x00A1},	//TVAR_ash_pGAS[379]
    {0x0F12,	0x006F},	//TVAR_ash_pGAS[380]
    {0x0F12,	0x0047},	//TVAR_ash_pGAS[381]
    {0x0F12,	0x002C},	//TVAR_ash_pGAS[382]
    {0x0F12,	0x0023},	//TVAR_ash_pGAS[383]
    {0x0F12,	0x002D},	//TVAR_ash_pGAS[384]
    {0x0F12,	0x0047},	//TVAR_ash_pGAS[385]
    {0x0F12,	0x0072},	//TVAR_ash_pGAS[386]
    {0x0F12,	0x00AB},	//TVAR_ash_pGAS[387]
    {0x0F12,	0x00EB},	//TVAR_ash_pGAS[388]
    {0x0F12,	0x0127},	//TVAR_ash_pGAS[389]
    {0x0F12,	0x016C},	//TVAR_ash_pGAS[390]
    {0x0F12,	0x011B},	//TVAR_ash_pGAS[391]
    {0x0F12,	0x00CA},	//TVAR_ash_pGAS[392]
    {0x0F12,	0x0099},	//TVAR_ash_pGAS[393]
    {0x0F12,	0x0074},	//TVAR_ash_pGAS[394]
    {0x0F12,	0x0059},	//TVAR_ash_pGAS[395]
    {0x0F12,	0x0050},	//TVAR_ash_pGAS[396]
    {0x0F12,	0x0057},	//TVAR_ash_pGAS[397]
    {0x0F12,	0x0072},	//TVAR_ash_pGAS[398]
    {0x0F12,	0x0099},	//TVAR_ash_pGAS[399]
    {0x0F12,	0x00CB},	//TVAR_ash_pGAS[400]
    {0x0F12,	0x0111},	//TVAR_ash_pGAS[401]
    {0x0F12,	0x0155},	//TVAR_ash_pGAS[402]
    {0x0F12,	0x01BA},	//TVAR_ash_pGAS[403]
    {0x0F12,	0x015D},	//TVAR_ash_pGAS[404]
    {0x0F12,	0x0107},	//TVAR_ash_pGAS[405]
    {0x0F12,	0x00D3},	//TVAR_ash_pGAS[406]
    {0x0F12,	0x00AE},	//TVAR_ash_pGAS[407]
    {0x0F12,	0x0099},	//TVAR_ash_pGAS[408]
    {0x0F12,	0x008F},	//TVAR_ash_pGAS[409]
    {0x0F12,	0x0096},	//TVAR_ash_pGAS[410]
    {0x0F12,	0x00AB},	//TVAR_ash_pGAS[411]
    {0x0F12,	0x00CD},	//TVAR_ash_pGAS[412]
    {0x0F12,	0x00FE},	//TVAR_ash_pGAS[413]
    {0x0F12,	0x014D},	//TVAR_ash_pGAS[414]
    {0x0F12,	0x01BC},	//TVAR_ash_pGAS[415]
    {0x0F12,	0x025D},	//TVAR_ash_pGAS[416]
    {0x0F12,	0x01A6},	//TVAR_ash_pGAS[417]
    {0x0F12,	0x0151},	//TVAR_ash_pGAS[418]
    {0x0F12,	0x0115},	//TVAR_ash_pGAS[419]
    {0x0F12,	0x00F0},	//TVAR_ash_pGAS[420]
    {0x0F12,	0x00DA},	//TVAR_ash_pGAS[421]
    {0x0F12,	0x00D0},	//TVAR_ash_pGAS[422]
    {0x0F12,	0x00D3},	//TVAR_ash_pGAS[423]
    {0x0F12,	0x00E5},	//TVAR_ash_pGAS[424]
    {0x0F12,	0x0108},	//TVAR_ash_pGAS[425]
    {0x0F12,	0x013F},	//TVAR_ash_pGAS[426]
    {0x0F12,	0x019E},	//TVAR_ash_pGAS[427]
    {0x0F12,	0x0271},	//TVAR_ash_pGAS[428]
    {0x0F12,	0x01B8},	//TVAR_ash_pGAS[429]
    {0x0F12,	0x013B},	//TVAR_ash_pGAS[430]
    {0x0F12,	0x00E6},	//TVAR_ash_pGAS[431]
    {0x0F12,	0x00BB},	//TVAR_ash_pGAS[432]
    {0x0F12,	0x00A5},	//TVAR_ash_pGAS[433]
    {0x0F12,	0x009A},	//TVAR_ash_pGAS[434]
    {0x0F12,	0x0096},	//TVAR_ash_pGAS[435]
    {0x0F12,	0x00A0},	//TVAR_ash_pGAS[436]
    {0x0F12,	0x00B1},	//TVAR_ash_pGAS[437]
    {0x0F12,	0x00D0},	//TVAR_ash_pGAS[438]
    {0x0F12,	0x00FE},	//TVAR_ash_pGAS[439]
    {0x0F12,	0x0148},	//TVAR_ash_pGAS[440]
    {0x0F12,	0x01EF},	//TVAR_ash_pGAS[441]
    {0x0F12,	0x0133},	//TVAR_ash_pGAS[442]
    {0x0F12,	0x00F9},	//TVAR_ash_pGAS[443]
    {0x0F12,	0x00B0},	//TVAR_ash_pGAS[444]
    {0x0F12,	0x008A},	//TVAR_ash_pGAS[445]
    {0x0F12,	0x0074},	//TVAR_ash_pGAS[446]
    {0x0F12,	0x0069},	//TVAR_ash_pGAS[447]
    {0x0F12,	0x0068},	//TVAR_ash_pGAS[448]
    {0x0F12,	0x0071},	//TVAR_ash_pGAS[449]
    {0x0F12,	0x0085},	//TVAR_ash_pGAS[450]
    {0x0F12,	0x00A4},	//TVAR_ash_pGAS[451]
    {0x0F12,	0x00CC},	//TVAR_ash_pGAS[452]
    {0x0F12,	0x0106},	//TVAR_ash_pGAS[453]
    {0x0F12,	0x0161},	//TVAR_ash_pGAS[454]
    {0x0F12,	0x00F6},	//TVAR_ash_pGAS[455]
    {0x0F12,	0x00C0},	//TVAR_ash_pGAS[456]
    {0x0F12,	0x0080},	//TVAR_ash_pGAS[457]
    {0x0F12,	0x005F},	//TVAR_ash_pGAS[458]
    {0x0F12,	0x0048},	//TVAR_ash_pGAS[459]
    {0x0F12,	0x003B},	//TVAR_ash_pGAS[460]
    {0x0F12,	0x0039},	//TVAR_ash_pGAS[461]
    {0x0F12,	0x0043},	//TVAR_ash_pGAS[462]
    {0x0F12,	0x0059},	//TVAR_ash_pGAS[463]
    {0x0F12,	0x0078},	//TVAR_ash_pGAS[464]
    {0x0F12,	0x00A0},	//TVAR_ash_pGAS[465]
    {0x0F12,	0x00D5},	//TVAR_ash_pGAS[466]
    {0x0F12,	0x0107},	//TVAR_ash_pGAS[467]
    {0x0F12,	0x00CE},	//TVAR_ash_pGAS[468]
    {0x0F12,	0x00A0},	//TVAR_ash_pGAS[469]
    {0x0F12,	0x0063},	//TVAR_ash_pGAS[470]
    {0x0F12,	0x0043},	//TVAR_ash_pGAS[471]
    {0x0F12,	0x0029},	//TVAR_ash_pGAS[472]
    {0x0F12,	0x001B},	//TVAR_ash_pGAS[473]
    {0x0F12,	0x0018},	//TVAR_ash_pGAS[474]
    {0x0F12,	0x0020},	//TVAR_ash_pGAS[475]
    {0x0F12,	0x0037},	//TVAR_ash_pGAS[476]
    {0x0F12,	0x0059},	//TVAR_ash_pGAS[477]
    {0x0F12,	0x0082},	//TVAR_ash_pGAS[478]
    {0x0F12,	0x00B6},	//TVAR_ash_pGAS[479]
    {0x0F12,	0x00E5},	//TVAR_ash_pGAS[480]
    {0x0F12,	0x00BC},	//TVAR_ash_pGAS[481]
    {0x0F12,	0x008F},	//TVAR_ash_pGAS[482]
    {0x0F12,	0x0054},	//TVAR_ash_pGAS[483]
    {0x0F12,	0x0031},	//TVAR_ash_pGAS[484]
    {0x0F12,	0x0017},	//TVAR_ash_pGAS[485]
    {0x0F12,	0x0008},	//TVAR_ash_pGAS[486]
    {0x0F12,	0x0006},	//TVAR_ash_pGAS[487]
    {0x0F12,	0x000D},	//TVAR_ash_pGAS[488]
    {0x0F12,	0x0022},	//TVAR_ash_pGAS[489]
    {0x0F12,	0x0044},	//TVAR_ash_pGAS[490]
    {0x0F12,	0x0070},	//TVAR_ash_pGAS[491]
    {0x0F12,	0x00A3},	//TVAR_ash_pGAS[492]
    {0x0F12,	0x00CF},	//TVAR_ash_pGAS[493]
    {0x0F12,	0x00B7},	//TVAR_ash_pGAS[494]
    {0x0F12,	0x008A},	//TVAR_ash_pGAS[495]
    {0x0F12,	0x0051},	//TVAR_ash_pGAS[496]
    {0x0F12,	0x002D},	//TVAR_ash_pGAS[497]
    {0x0F12,	0x0013},	//TVAR_ash_pGAS[498]
    {0x0F12,	0x0005},	//TVAR_ash_pGAS[499]
    {0x0F12,	0x0000},	//TVAR_ash_pGAS[500]
    {0x0F12,	0x0006},	//TVAR_ash_pGAS[501]
    {0x0F12,	0x001A},	//TVAR_ash_pGAS[502]
    {0x0F12,	0x003B},	//TVAR_ash_pGAS[503]
    {0x0F12,	0x0069},	//TVAR_ash_pGAS[504]
    {0x0F12,	0x009A},	//TVAR_ash_pGAS[505]
    {0x0F12,	0x00C8},	//TVAR_ash_pGAS[506]
    {0x0F12,	0x00C3},	//TVAR_ash_pGAS[507]
    {0x0F12,	0x0093},	//TVAR_ash_pGAS[508]
    {0x0F12,	0x005A},	//TVAR_ash_pGAS[509]
    {0x0F12,	0x0036},	//TVAR_ash_pGAS[510]
    {0x0F12,	0x001B},	//TVAR_ash_pGAS[511]
    {0x0F12,	0x000D},	//TVAR_ash_pGAS[512]
    {0x0F12,	0x0008},	//TVAR_ash_pGAS[513]
    {0x0F12,	0x000D},	//TVAR_ash_pGAS[514]
    {0x0F12,	0x0020},	//TVAR_ash_pGAS[515]
    {0x0F12,	0x0041},	//TVAR_ash_pGAS[516]
    {0x0F12,	0x006C},	//TVAR_ash_pGAS[517]
    {0x0F12,	0x00A0},	//TVAR_ash_pGAS[518]
    {0x0F12,	0x00CC},	//TVAR_ash_pGAS[519]
    {0x0F12,	0x00DE},	//TVAR_ash_pGAS[520]
    {0x0F12,	0x00AA},	//TVAR_ash_pGAS[521]
    {0x0F12,	0x0070},	//TVAR_ash_pGAS[522]
    {0x0F12,	0x004D},	//TVAR_ash_pGAS[523]
    {0x0F12,	0x0032},	//TVAR_ash_pGAS[524]
    {0x0F12,	0x0023},	//TVAR_ash_pGAS[525]
    {0x0F12,	0x001E},	//TVAR_ash_pGAS[526]
    {0x0F12,	0x0024},	//TVAR_ash_pGAS[527]
    {0x0F12,	0x0036},	//TVAR_ash_pGAS[528]
    {0x0F12,	0x0055},	//TVAR_ash_pGAS[529]
    {0x0F12,	0x007E},	//TVAR_ash_pGAS[530]
    {0x0F12,	0x00B3},	//TVAR_ash_pGAS[531]
    {0x0F12,	0x00E3},	//TVAR_ash_pGAS[532]
    {0x0F12,	0x010B},	//TVAR_ash_pGAS[533]
    {0x0F12,	0x00D0},	//TVAR_ash_pGAS[534]
    {0x0F12,	0x0091},	//TVAR_ash_pGAS[535]
    {0x0F12,	0x0070},	//TVAR_ash_pGAS[536]
    {0x0F12,	0x0058},	//TVAR_ash_pGAS[537]
    {0x0F12,	0x0048},	//TVAR_ash_pGAS[538]
    {0x0F12,	0x0044},	//TVAR_ash_pGAS[539]
    {0x0F12,	0x0048},	//TVAR_ash_pGAS[540]
    {0x0F12,	0x005A},	//TVAR_ash_pGAS[541]
    {0x0F12,	0x0075},	//TVAR_ash_pGAS[542]
    {0x0F12,	0x009A},	//TVAR_ash_pGAS[543]
    {0x0F12,	0x00D2},	//TVAR_ash_pGAS[544]
    {0x0F12,	0x010B},	//TVAR_ash_pGAS[545]
    {0x0F12,	0x014F},	//TVAR_ash_pGAS[546]
    {0x0F12,	0x010B},	//TVAR_ash_pGAS[547]
    {0x0F12,	0x00C5},	//TVAR_ash_pGAS[548]
    {0x0F12,	0x00A0},	//TVAR_ash_pGAS[549]
    {0x0F12,	0x008A},	//TVAR_ash_pGAS[550]
    {0x0F12,	0x007F},	//TVAR_ash_pGAS[551]
    {0x0F12,	0x0079},	//TVAR_ash_pGAS[552]
    {0x0F12,	0x007D},	//TVAR_ash_pGAS[553]
    {0x0F12,	0x008A},	//TVAR_ash_pGAS[554]
    {0x0F12,	0x00A1},	//TVAR_ash_pGAS[555]
    {0x0F12,	0x00C6},	//TVAR_ash_pGAS[556]
    {0x0F12,	0x0107},	//TVAR_ash_pGAS[557]
    {0x0F12,	0x0164},	//TVAR_ash_pGAS[558]
    {0x0F12,	0x01E0},	//TVAR_ash_pGAS[559]
    {0x0F12,	0x014B},	//TVAR_ash_pGAS[560]
    {0x0F12,	0x0107},	//TVAR_ash_pGAS[561]
    {0x0F12,	0x00D7},	//TVAR_ash_pGAS[562]
    {0x0F12,	0x00BF},	//TVAR_ash_pGAS[563]
    {0x0F12,	0x00B2},	//TVAR_ash_pGAS[564]
    {0x0F12,	0x00AE},	//TVAR_ash_pGAS[565]
    {0x0F12,	0x00AF},	//TVAR_ash_pGAS[566]
    {0x0F12,	0x00B8},	//TVAR_ash_pGAS[567]
    {0x0F12,	0x00D1},	//TVAR_ash_pGAS[568]
    {0x0F12,	0x00FE},	//TVAR_ash_pGAS[569]
    {0x0F12,	0x014D},	//TVAR_ash_pGAS[570]
    {0x0F12,	0x01FD},	//TVAR_ash_pGAS[571]
    //	param_end	TVAR_ash_pGAS
    {0x002A,    0x074E},
    {0x0F12,	0x0001},	//ash_bLumaMode 
    {0x002A,    0x0D30},
    {0x0F12,	0x025F},	//awbb_GLocusR
    {0x002A,    0x0D32},
    {0x0F12,	0x0376},	//awbb_GLocusB
    //	param_start	TVAR_ash_AwbAshCord
    {0x002A,    0x06B8},
    {0x0F12,	0x00C0},	//TVAR_ash_AwbAshCord[0]
    {0x0F12,	0x00E0},	//TVAR_ash_AwbAshCord[1]
    {0x0F12,	0x00FA},	//TVAR_ash_AwbAshCord[2]
    {0x0F12,	0x011D},	//TVAR_ash_AwbAshCord[3]
    {0x0F12,	0x0144},	//TVAR_ash_AwbAshCord[4]
    {0x0F12,	0x0173},	//TVAR_ash_AwbAshCord[5]
    {0x0F12,	0x0180},	//TVAR_ash_AwbAshCord[6]
    //	param_end	TVAR_ash_AwbAshCord
    {0x002A,    0x0664},
    {0x0F12,	0x013E},	//seti_uContrastCenter

    //ash_CGrasAlphas
    {0x002A,    0x06C6},
    {0x0F12,	0x010B},    	//ash_CGrasAlphas[0]
    {0x0F12,	0x0103},    	//ash_CGrasAlphas[1]
    {0x0F12,	0x00FC},    	//ash_CGrasAlphas[2]
    {0x0F12,	0x010C},    	//ash_CGrasAlphas[3]
    //============================================================
    //End GAS 
    //============================================================
    //White Balance
    //============================================================
    //	param_start	awbb_IndoorGrZones_m_BGrid
    {0x002A,    0x0C48},
    {0x0F12,	0x03C9},	//awbb_IndoorGrZones_m_BGrid[0]
    {0x0F12,	0x040A},	//awbb_IndoorGrZones_m_BGrid[1]
    {0x0F12,	0x038B},	//awbb_IndoorGrZones_m_BGrid[2]
    {0x0F12,	0x0405},	//awbb_IndoorGrZones_m_BGrid[3]
    {0x0F12,	0x0356},	//awbb_IndoorGrZones_m_BGrid[4]
    {0x0F12,	0x0400},	//awbb_IndoorGrZones_m_BGrid[5]
    {0x0F12,	0x0322},	//awbb_IndoorGrZones_m_BGrid[6]
    {0x0F12,	0x03DF},	//awbb_IndoorGrZones_m_BGrid[7]
    {0x0F12,	0x02E9},	//awbb_IndoorGrZones_m_BGrid[8]
    {0x0F12,	0x03B0},	//awbb_IndoorGrZones_m_BGrid[9]
    {0x0F12,	0x02B6},	//awbb_IndoorGrZones_m_BGrid[10]
    {0x0F12,	0x0380},	//awbb_IndoorGrZones_m_BGrid[11]
    {0x0F12,	0x0296},	//awbb_IndoorGrZones_m_BGrid[12]
    {0x0F12,	0x034F},	//awbb_IndoorGrZones_m_BGrid[13]
    {0x0F12,	0x027F},	//awbb_IndoorGrZones_m_BGrid[14]
    {0x0F12,	0x031A},	//awbb_IndoorGrZones_m_BGrid[15]
    {0x0F12,	0x026C},	//awbb_IndoorGrZones_m_BGrid[16]
    {0x0F12,	0x02F5},	//awbb_IndoorGrZones_m_BGrid[17]
    {0x0F12,	0x0256},	//awbb_IndoorGrZones_m_BGrid[18]
    {0x0F12,	0x02DA},	//awbb_IndoorGrZones_m_BGrid[19]
    {0x0F12,	0x0241},	//awbb_IndoorGrZones_m_BGrid[20]
    {0x0F12,	0x02C4},	//awbb_IndoorGrZones_m_BGrid[21]
    {0x0F12,	0x0228},	//awbb_IndoorGrZones_m_BGrid[22]
    {0x0F12,	0x02AB},	//awbb_IndoorGrZones_m_BGrid[23]
    {0x0F12,	0x0213},	//awbb_IndoorGrZones_m_BGrid[24]
    {0x0F12,	0x0292},	//awbb_IndoorGrZones_m_BGrid[25]
    {0x0F12,	0x01FF},	//awbb_IndoorGrZones_m_BGrid[26]
    {0x0F12,	0x0278},	//awbb_IndoorGrZones_m_BGrid[27]
    {0x0F12,	0x01F4},	//awbb_IndoorGrZones_m_BGrid[28]
    {0x0F12,	0x025F},	//awbb_IndoorGrZones_m_BGrid[29]
    {0x0F12,	0x0202},	//awbb_IndoorGrZones_m_BGrid[30]
    {0x0F12,	0x0234},	//awbb_IndoorGrZones_m_BGrid[31]
    {0x0F12,	0x0000},	//awbb_IndoorGrZones_m_BGrid[32]
    {0x0F12,	0x0000},	//awbb_IndoorGrZones_m_BGrid[33]
    {0x0F12,	0x0000},	//awbb_IndoorGrZones_m_BGrid[34]
    {0x0F12,	0x0000},	//awbb_IndoorGrZones_m_BGrid[35]
    {0x0F12,	0x0000},	//awbb_IndoorGrZones_m_BGrid[36]
    {0x0F12,	0x0000},	//awbb_IndoorGrZones_m_BGrid[37]
    {0x0F12,	0x0000},	//awbb_IndoorGrZones_m_BGrid[38]
    {0x0F12,	0x0000},	//awbb_IndoorGrZones_m_BGrid[39]
    //	param_end	awbb_IndoorGrZones_m_BGrid
    {0x002A,    0x0C98},
    {0x0F12,	0x0005},
    {0x0F12,    0x0000},	//awbb_IndoorGrZones_m_GridStep
    {0x002A,    0x0CA0},
    {0x0F12,	0x00E8},
    {0x0F12,    0x0000},	//awbb_IndoorGrZones_m_Boffs
    //	param_start	awbb_LowBrGrZones_m_BGrid
    {0x002A,    0x0CE0},
	{0x0F12,    0x03D4},	//awbb_LowBrGrZones_m_BGrid[0] //                                                                          
	{0x0F12,    0x043E},	//awbb_LowBrGrZones_m_BGrid[1] //                                                                          
	{0x0F12,    0x035C},	//awbb_LowBrGrZones_m_BGrid[2] //                                                                          
	{0x0F12,    0x0438},	//awbb_LowBrGrZones_m_BGrid[3] //                                                                          
	{0x0F12,    0x02F0},	//awbb_LowBrGrZones_m_BGrid[4] //                                                                          
	{0x0F12,    0x042D},	//awbb_LowBrGrZones_m_BGrid[5] //                                                                          
	{0x0F12,    0x029A},	//awbb_LowBrGrZones_m_BGrid[6] //                                                                          
	{0x0F12,    0x03EF},	//awbb_LowBrGrZones_m_BGrid[7] //                                                                          
	{0x0F12,    0x025E},	//awbb_LowBrGrZones_m_BGrid[8] //                                                                          
	{0x0F12,    0x0395},	//awbb_LowBrGrZones_m_BGrid[9] //                                                                          
	{0x0F12,    0x022E},	//awbb_LowBrGrZones_m_BGrid[10] //                                                                         
	{0x0F12,    0x0346},	//awbb_LowBrGrZones_m_BGrid[11] //                                                                         
	{0x0F12,    0x0200},	//awbb_LowBrGrZones_m_BGrid[12] //                                                                         
	{0x0F12,    0x02F6},	//awbb_LowBrGrZones_m_BGrid[13] //                                                                         
	{0x0F12,    0x01CE},	//awbb_LowBrGrZones_m_BGrid[14] //                                                                         
	{0x0F12,    0x02C8},	//awbb_LowBrGrZones_m_BGrid[15] //                                                                         
	{0x0F12,    0x01BB},	//awbb_LowBrGrZones_m_BGrid[16] //                                                                         
	{0x0F12,    0x0287},	//awbb_LowBrGrZones_m_BGrid[17] //                                                                         
	{0x0F12,    0x01E2},	//awbb_LowBrGrZones_m_BGrid[18] //                                                                         
	{0x0F12,    0x0239},	//awbb_LowBrGrZones_m_BGrid[19] //                                                                         
	{0x0F12,    0x0000},	//awbb_LowBrGrZones_m_BGrid[20] //                                                                         
	{0x0F12,    0x0000},	//awbb_LowBrGrZones_m_BGrid[21] //                                                                         
	{0x0F12,    0x0000},	//awbb_LowBrGrZones_m_BGrid[22] //                                                                         
	{0x0F12,    0x0000},	//awbb_LowBrGrZones_m_BGrid[23] //                                                                         
    //	param_end	awbb_LowBrGrZones_m_BGrid  
    {0x002A,    0x0D10},
    {0x0F12,	0x0006},
    {0x0F12,    0x0000},	//awbb_LowBrGrZones_m_GridStep
    {0x002A,    0x0D18},
    {0x0F12,	0x00AE},
    {0x0F12,    0x0000},	//awbb_LowBrGrZones_m_Boffs
    //	param_start	awbb_OutdoorGrZones_m_BGrid
    {0x002A,    0x0CA4},
    {0x0F12,	0x031A},//02A8	//awbb_OutdoorGrZones_m_BGrid[0]
    {0x0F12,	0x0382},//02C2	//awbb_OutdoorGrZones_m_BGrid[1]
    {0x0F12,	0x02E3},//0270	//awbb_OutdoorGrZones_m_BGrid[2]
    {0x0F12,	0x038B},//02B4	//awbb_OutdoorGrZones_m_BGrid[3]
    {0x0F12,	0x02B7},//025C	//awbb_OutdoorGrZones_m_BGrid[4]
    {0x0F12,	0x035D},//02A0	//awbb_OutdoorGrZones_m_BGrid[5]
    {0x0F12,	0x0297},//0240	//awbb_OutdoorGrZones_m_BGrid[6]
    {0x0F12,	0x032A},//0288	//awbb_OutdoorGrZones_m_BGrid[7]
    {0x0F12,	0x0285},//0230	//awbb_OutdoorGrZones_m_BGrid[8]
    {0x0F12,	0x0302},//026E	//awbb_OutdoorGrZones_m_BGrid[9]
    {0x0F12,	0x0271},//0222	//awbb_OutdoorGrZones_m_BGrid[10]
    {0x0F12,	0x02DF},//025A	//awbb_OutdoorGrZones_m_BGrid[11]
    {0x0F12,	0x025D},//0220	//awbb_OutdoorGrZones_m_BGrid[12]
    {0x0F12,	0x02C7},//023A	//awbb_OutdoorGrZones_m_BGrid[13]
    {0x0F12,	0x0241},//0000	//awbb_OutdoorGrZones_m_BGrid[14]
    {0x0F12,	0x02B5},//0000	//awbb_OutdoorGrZones_m_BGrid[15]
    {0x0F12,	0x0229},//0000	//awbb_OutdoorGrZones_m_BGrid[16]
    {0x0F12,	0x029B},//0000	//awbb_OutdoorGrZones_m_BGrid[17]
    {0x0F12,	0x0212},//0000	//awbb_OutdoorGrZones_m_BGrid[18]
    {0x0F12,	0x0280},//0000	//awbb_OutdoorGrZones_m_BGrid[19]
    {0x0F12,	0x0205},//0000	//awbb_OutdoorGrZones_m_BGrid[20]
    {0x0F12,	0x026A},//0000	//awbb_OutdoorGrZones_m_BGrid[21]
    {0x0F12,	0x020A},//0000	//awbb_OutdoorGrZones_m_BGrid[22]
    {0x0F12,	0x023F},//0000	//awbb_OutdoorGrZones_m_BGrid[23]
    //	param_end	awbb_OutdoorGrZones_m_BGrid 
    {0x002A,    0x0CD4},
    {0x0F12, 	0x0005},
    {0x0F12,    0x0000},	//awbb_OutdoorGrZones_m_GridStep
    {0x002A,    0x0CDC},
    {0x0F12,	0x0157},  //01FE
    {0x0F12,    0x0000},	//awbb_OutdoorGrZones_m_Boffs
    //Low illumiantion
    {0x002A,    0x0D1C},
    {0x0F12,	0x037C},
    {0x0F12,    0x0000},	//awbb_CrclLowT_R_c
    {0x002A,    0x0D20},
    {0x0F12, 	0x0157},
    {0x0F12,    0x0000},	//awbb_CrclLowT_B_c
    {0x002A,    0x0D24},
    {0x0F12, 	0x3EB8},
    {0x0F12,    0x0000},	//awbb_CrclLowT_Rad_c
    //White locus 
    {0x002A,    0x0D2C},
    {0x0F12,	0x013D},	//awbb_IntcR
    {0x0F12,	0x011E},	//awbb_IntcB
    {0x002A,    0x0D46},
    {0x0F12,	0x0396},	//04A2	//awbb_MvEq_RBthresh
    {0x002A,    0x0D5C},
    {0x0F12,	0x0584},	//awbb_LowTempRB

    //Grid Correction
    //	param_start	awbb_GridCorr_R
    {0x002A,    0x0DD4},
    {0x0F12,	0x003C},	//awbb_GridCorr_R[0]
    {0x0F12,	0x0050},	//awbb_GridCorr_R[1]
    {0x0F12,	0x0064},	//awbb_GridCorr_R[2]
    {0x0F12,	0x0000},	//awbb_GridCorr_R[3]
    {0x0F12,	0x0014},	//awbb_GridCorr_R[4]
    {0x0F12,	0x0014},	//awbb_GridCorr_R[5]
    {0x0F12,	0x003C},	//awbb_GridCorr_R[6]
    {0x0F12,	0x0050},	//awbb_GridCorr_R[7]
    {0x0F12,	0x0064},	//awbb_GridCorr_R[8]
    {0x0F12,	0x0000},	//awbb_GridCorr_R[9]
    {0x0F12,	0x0014},	//awbb_GridCorr_R[10]
    {0x0F12,	0x0014},	//awbb_GridCorr_R[11]
    {0x0F12,	0x003C},	//awbb_GridCorr_R[12]
    {0x0F12,	0x0050},	//awbb_GridCorr_R[13]
    {0x0F12,	0x0064},	//awbb_GridCorr_R[14]
    {0x0F12,	0x0000},	//awbb_GridCorr_R[15]
    {0x0F12,	0x0014},	//awbb_GridCorr_R[16]
    {0x0F12,	0x0014},	//awbb_GridCorr_R[17]
    //	param_end	awbb_GridCorr_R
    //	param_start	awbb_GridCorr_B
    {0x002A,    0x0DF8},
    {0x0F12,	0xFFEC},	//awbb_GridCorr_B[0]
    {0x0F12,	0xFFEC},	//awbb_GridCorr_B[1]
    {0x0F12,	0x0078},	//awbb_GridCorr_B[2]
    {0x0F12,	0xFFE2},	//awbb_GridCorr_B[3]
    {0x0F12,	0xFFE2},	//awbb_GridCorr_B[4]
    {0x0F12,	0xFFB0},	//awbb_GridCorr_B[5]
    {0x0F12,	0xFFEC},	//awbb_GridCorr_B[6]
    {0x0F12,	0xFFEC},	//awbb_GridCorr_B[7]
    {0x0F12,	0x0078},	//awbb_GridCorr_B[8]
    {0x0F12,	0xFFE2},	//awbb_GridCorr_B[9]
    {0x0F12,	0xFFE2},	//awbb_GridCorr_B[10]
    {0x0F12,	0xFFB0},	//awbb_GridCorr_B[11]
    {0x0F12,	0xFFEC},	//awbb_GridCorr_B[12]
    {0x0F12,	0xFFEC},	//awbb_GridCorr_B[13]
    {0x0F12,	0x0078},	//awbb_GridCorr_B[14]
    {0x0F12,	0xFFE2},	//awbb_GridCorr_B[15]
    {0x0F12,	0xFFE2},	//awbb_GridCorr_B[16]
    {0x0F12,	0xFFB0},	//awbb_GridCorr_B[17]
    //	param_end	awbb_GridCorr_B
    //	param_start	awbb_GridConst_1 
    {0x002A,    0x0E1C},
    {0x0F12,	0x02D9},	//awbb_GridConst_1[0]
    {0x0F12,	0x0357},	//awbb_GridConst_1[1]
    {0x0F12,	0x03D1},	//awbb_GridConst_1[2]
    //	param_end	awbb_GridConst_1
    //	param_start	awbb_GridConst_2
    {0x002A,    0x0E22},
    {0x0F12,	0x0DE9},	//awbb_GridConst_2[0]
    {0x0F12,	0x0EDD},	//awbb_GridConst_2[1]
    {0x0F12,	0x0F42},	//awbb_GridConst_2[2]
    {0x0F12,	0x0F54},	//awbb_GridConst_2[3]
    {0x0F12,	0x0FAE},	//awbb_GridConst_2[4]
    {0x0F12,	0x1011},	//awbb_GridConst_2[5]
    //	param_end	awbb_GridConst_2 
    {0x002A,    0x0E2E},
    {0x0F12,	0x00BA},	//awbb_GridCoeff_R_1
    {0x0F12,	0x00AF},	//awbb_GridCoeff_B_1
    {0x0F12,	0x0049},	//awbb_GridCoeff_R_2
    {0x0F12,	0x00F5},	//awbb_GridCoeff_B_2

    {0x002A,    0x0E4A},
    {0x0F12,    0x0002},    	//awbb_GridEnable
    //============================================================
    //End AWB
    //============================================================
    //AE 
    //============================================================
    //AE variation
    {0x002A,    0x051A},
    {0x0F12,	0x010E},	//lt_uLimitHigh
    {0x0F12,	0x00F5},	//lt_uLimitLow
    //disable contrast, enable illumination
    {0x002A,    0x0F76},
    {0x0F12,	0x0001},  //

    {0x002A,    0x1034}, 
    {0x0F12,    0x00C0},	//SARR_IllumType[0]
    {0x0F12,    0x00E0},  //SARR_IllumType[1]
    {0x0F12,    0x00F0},  //SARR_IllumType[2]
    {0x0F12,    0x0129},  //SARR_IllumType[3]
    {0x0F12,    0x0156},  //SARR_IllumType[4]
    {0x0F12,    0x017F},  //SARR_IllumType[5]
    {0x0F12,    0x018F},  //SARR_IllumType[6]

    {0x002A,    0x1042},
    {0x0F12,	0x0120},    //SARR_IllumTypeF[0]
    {0x0F12,	0x0120},    //SARR_IllumTypeF[1]
    {0x0F12,	0x0120},    //SARR_IllumTypeF[2]
    {0x0F12,	0x0100},    //SARR_IllumTypeF[3]
    {0x0F12,	0x0100},    //SARR_IllumTypeF[4]
    {0x0F12,	0x0100},    //SARR_IllumTypeF[5]
    {0x0F12,	0x0100},    //SARR_IllumTypeF[6]

    //AE target  
    {0x002A,    0x0F70},
    {0x0F12,    0x0046},  //TVAR_ae_BrAve
    //AE Weight               
    {0x0028,	0x7000},                
    {0x002A,	0x0f7e},    //AE weight/
    {0x0F12,	0x0101},//0000                
    {0x0F12,	0x0101},//0000                
    {0x0F12,	0x0101},//0000                
    {0x0F12,	0x0101},//0000                
    {0x0F12,	0x0101},//0101                
    {0x0F12,	0x0101},//0101                
    {0x0F12,	0x0101},//0101                
    {0x0F12,	0x0101},//0101                
    {0x0F12,	0x0101},//0101                
    {0x0F12,	0x0303},//0101                
    {0x0F12,	0x0303},//0101                
    {0x0F12,	0x0101},//0101                
    {0x0F12,	0x0101},//0101                
    {0x0F12,	0x0303},//0401                
    {0x0F12,	0x0303},//0104                
    {0x0F12,	0x0101},//0101                
    {0x0F12,	0x0101},//0101                
    {0x0F12,	0x0303},//0401                
    {0x0F12,	0x0303},//0104                
    {0x0F12,	0x0101},//0101                
    {0x0F12,	0x0101},//0101                
    {0x0F12,	0x0303},//0201                
    {0x0F12,	0x0303},//0102                
    {0x0F12,	0x0101},//0101                
    {0x0F12,	0x0101},//0101                
    {0x0F12,	0x0101},//0201                
    {0x0F12,	0x0101},//0102                
    {0x0F12,	0x0101},//0101                
    {0x0F12,	0x0101},//0101                
    {0x0F12,	0x0101},//0101                
    {0x0F12,	0x0101},//0101                
    {0x0F12,	0x0101},//0101                
    //============================================================   
    //Gamma
    //============================================================     
    {0x002A,  0x3288},
    {0x0F12,  0x0000}, //#SARR_usDualGammaLutRGBIndoor[0][0]		0000		
    {0x0F12,  0x0008}, //#SARR_usDualGammaLutRGBIndoor[0][1]		0008		
    {0x0F12,  0x0013}, //#SARR_usDualGammaLutRGBIndoor[0][2]		0013		
    {0x0F12,  0x002C}, //#SARR_usDualGammaLutRGBIndoor[0][3]		002C		
    {0x0F12,  0x0061}, //#SARR_usDualGammaLutRGBIndoor[0][4]		0061		
    {0x0F12,  0x00C8}, //#SARR_usDualGammaLutRGBIndoor[0][5]		00C8		
    {0x0F12,  0x0113}, //#SARR_usDualGammaLutRGBIndoor[0][6]		0113		
    {0x0F12,  0x0132}, //#SARR_usDualGammaLutRGBIndoor[0][7]		0132		
    {0x0F12,  0x014C}, //#SARR_usDualGammaLutRGBIndoor[0][8]		014C		
    {0x0F12,  0x0179}, //#SARR_usDualGammaLutRGBIndoor[0][9]		0179		
    {0x0F12,  0x01A4}, //#SARR_usDualGammaLutRGBIndoor[0][10]	  01A4			
    {0x0F12,  0x01CD}, //#SARR_usDualGammaLutRGBIndoor[0][11]	  01CD			
    {0x0F12,  0x01F4}, //#SARR_usDualGammaLutRGBIndoor[0][12]	  01F4			
    {0x0F12,  0x0239}, //#SARR_usDualGammaLutRGBIndoor[0][13]	  0239			
    {0x0F12,  0x0278}, //#SARR_usDualGammaLutRGBIndoor[0][14]	  0278			
    {0x0F12,  0x02E0}, //#SARR_usDualGammaLutRGBIndoor[0][15]	  02E0			
    {0x0F12,  0x0333}, //#SARR_usDualGammaLutRGBIndoor[0][16]	  0333			
    {0x0F12,  0x037B}, //#SARR_usDualGammaLutRGBIndoor[0][17]	  037B			
    {0x0F12,  0x03BF}, //#SARR_usDualGammaLutRGBIndoor[0][18]	  03BF			
    {0x0F12,  0x03FF}, //#SARR_usDualGammaLutRGBIndoor[0][19]	  03FF			
    {0x0F12,  0x0000}, //#SARR_usDualGammaLutRGBIndoor[1][0]		0000		
    {0x0F12,  0x0008}, //#SARR_usDualGammaLutRGBIndoor[1][1]		0008		
    {0x0F12,  0x0013}, //#SARR_usDualGammaLutRGBIndoor[1][2]		0013		
    {0x0F12,  0x002C}, //#SARR_usDualGammaLutRGBIndoor[1][3]		002C		
    {0x0F12,  0x0061}, //#SARR_usDualGammaLutRGBIndoor[1][4]		0061		
    {0x0F12,  0x00C8}, //#SARR_usDualGammaLutRGBIndoor[1][5]		00C8		
    {0x0F12,  0x0113}, //#SARR_usDualGammaLutRGBIndoor[1][6]		0113		
    {0x0F12,  0x0132}, //#SARR_usDualGammaLutRGBIndoor[1][7]		0132		
    {0x0F12,  0x014C}, //#SARR_usDualGammaLutRGBIndoor[1][8]		014C		
    {0x0F12,  0x0179}, //#SARR_usDualGammaLutRGBIndoor[1][9]		0179		
    {0x0F12,  0x01A4}, //#SARR_usDualGammaLutRGBIndoor[1][10]	  01A4			
    {0x0F12,  0x01CD}, //#SARR_usDualGammaLutRGBIndoor[1][11]	  01CD			
    {0x0F12,  0x01F4}, //#SARR_usDualGammaLutRGBIndoor[1][12]	  01F4			
    {0x0F12,  0x0239}, //#SARR_usDualGammaLutRGBIndoor[1][13]	  0239			
    {0x0F12,  0x0278}, //#SARR_usDualGammaLutRGBIndoor[1][14]	  0278			
    {0x0F12,  0x02E0}, //#SARR_usDualGammaLutRGBIndoor[1][15]	  02E0			
    {0x0F12,  0x0333}, //#SARR_usDualGammaLutRGBIndoor[1][16]	  0333			
    {0x0F12,  0x037B}, //#SARR_usDualGammaLutRGBIndoor[1][17]	  037B			
    {0x0F12,  0x03BF}, //#SARR_usDualGammaLutRGBIndoor[1][18]	  03BF			
    {0x0F12,  0x03FF}, //#SARR_usDualGammaLutRGBIndoor[1][19]	  03FF			
    {0x0F12,  0x0000}, //#SARR_usDualGammaLutRGBIndoor[2][0]		0000		
    {0x0F12,  0x0008}, //#SARR_usDualGammaLutRGBIndoor[2][1]		0008		
    {0x0F12,  0x0013}, //#SARR_usDualGammaLutRGBIndoor[2][2]		0013		
    {0x0F12,  0x002C}, //#SARR_usDualGammaLutRGBIndoor[2][3]		002C		
    {0x0F12,  0x0061}, //#SARR_usDualGammaLutRGBIndoor[2][4]		0061		
    {0x0F12,  0x00C8}, //#SARR_usDualGammaLutRGBIndoor[2][5]		00C8		
    {0x0F12,  0x0113}, //#SARR_usDualGammaLutRGBIndoor[2][6]		0113		
    {0x0F12,  0x0132}, //#SARR_usDualGammaLutRGBIndoor[2][7]		0132		
    {0x0F12,  0x014C}, //#SARR_usDualGammaLutRGBIndoor[2][8]		014C		
    {0x0F12,  0x0179}, //#SARR_usDualGammaLutRGBIndoor[2][9]		0179		
    {0x0F12,  0x01A4}, //#SARR_usDualGammaLutRGBIndoor[2][10]	  01A4			
    {0x0F12,  0x01CD}, //#SARR_usDualGammaLutRGBIndoor[2][11]	  01CD			
    {0x0F12,  0x01F4}, //#SARR_usDualGammaLutRGBIndoor[2][12]	  01F4			
    {0x0F12,  0x0239}, //#SARR_usDualGammaLutRGBIndoor[2][13]	  0239			
    {0x0F12,  0x0278}, //#SARR_usDualGammaLutRGBIndoor[2][14]	  0278			
    {0x0F12,  0x02E0}, //#SARR_usDualGammaLutRGBIndoor[2][15]	  02E0			
    {0x0F12,  0x0333}, //#SARR_usDualGammaLutRGBIndoor[2][16]	  0333			
    {0x0F12,  0x037B}, //#SARR_usDualGammaLutRGBIndoor[2][17]	  037B			
    {0x0F12,  0x03BF}, //#SARR_usDualGammaLutRGBIndoor[2][18]	  03BF			
    {0x0F12,  0x03FF}, //#SARR_usDualGammaLutRGBIndoor[2][19]	  03FF			

    {0x002A,  0x3300},
    {0x0F12,  0x0000},	 //#SARR_usDualGammaLutRGBOutdoor[0][0]	0000			
    {0x0F12,  0x0008},	 //#SARR_usDualGammaLutRGBOutdoor[0][1]	0008			
    {0x0F12,  0x0013},	 //#SARR_usDualGammaLutRGBOutdoor[0][2]	0013			
    {0x0F12,  0x002C},	 //#SARR_usDualGammaLutRGBOutdoor[0][3]	002C			
    {0x0F12,  0x0061},	 //#SARR_usDualGammaLutRGBOutdoor[0][4]	0061			
    {0x0F12,  0x00C8},	 //#SARR_usDualGammaLutRGBOutdoor[0][5]	00C8			
    {0x0F12,  0x0113},	 //#SARR_usDualGammaLutRGBOutdoor[0][6]	0113			
    {0x0F12,  0x0132},	 //#SARR_usDualGammaLutRGBOutdoor[0][7]	0132			
    {0x0F12,  0x014C},	 //#SARR_usDualGammaLutRGBOutdoor[0][8]	014C			
    {0x0F12,  0x0179},	 //#SARR_usDualGammaLutRGBOutdoor[0][9]	0179			
    {0x0F12,  0x01A4},	 //#SARR_usDualGammaLutRGBOutdoor[0][10]	01A4		
    {0x0F12,  0x01CD},	 //#SARR_usDualGammaLutRGBOutdoor[0][11]	01CD		
    {0x0F12,  0x01F4},	 //#SARR_usDualGammaLutRGBOutdoor[0][12]	01F4		
    {0x0F12,  0x0239},	 //#SARR_usDualGammaLutRGBOutdoor[0][13]	0239		
    {0x0F12,  0x0278},	 //#SARR_usDualGammaLutRGBOutdoor[0][14]	0278		
    {0x0F12,  0x02E0},	 //#SARR_usDualGammaLutRGBOutdoor[0][15]	02E0		
    {0x0F12,  0x0333},	 //#SARR_usDualGammaLutRGBOutdoor[0][16]	0333		
    {0x0F12,  0x037B},	 //#SARR_usDualGammaLutRGBOutdoor[0][17]	037B		
    {0x0F12,  0x03BF},	 //#SARR_usDualGammaLutRGBOutdoor[0][18]	03BF		
    {0x0F12,  0x03FF},	 //#SARR_usDualGammaLutRGBOutdoor[0][19]	03FF		
    {0x0F12,  0x0000},	 //#SARR_usDualGammaLutRGBOutdoor[1][0]	0000			
    {0x0F12,  0x0008},	 //#SARR_usDualGammaLutRGBOutdoor[1][1]	0008			
    {0x0F12,  0x0013},	 //#SARR_usDualGammaLutRGBOutdoor[1][2]	0013			
    {0x0F12,  0x002C},	 //#SARR_usDualGammaLutRGBOutdoor[1][3]	002C			
    {0x0F12,  0x0061},	 //#SARR_usDualGammaLutRGBOutdoor[1][4]	0061			
    {0x0F12,  0x00C8},	 //#SARR_usDualGammaLutRGBOutdoor[1][5]	00C8			
    {0x0F12,  0x0113},	 //#SARR_usDualGammaLutRGBOutdoor[1][6]	0113			
    {0x0F12,  0x0132},	 //#SARR_usDualGammaLutRGBOutdoor[1][7]	0132			
    {0x0F12,  0x014C},	 //#SARR_usDualGammaLutRGBOutdoor[1][8]	014C			
    {0x0F12,  0x0179},	 //#SARR_usDualGammaLutRGBOutdoor[1][9]	0179			
    {0x0F12,  0x01A4},	 //#SARR_usDualGammaLutRGBOutdoor[1][10]	01A4		
    {0x0F12,  0x01CD},	 //#SARR_usDualGammaLutRGBOutdoor[1][11]	01CD		
    {0x0F12,  0x01F4},	 //#SARR_usDualGammaLutRGBOutdoor[1][12]	01F4		
    {0x0F12,  0x0239},	 //#SARR_usDualGammaLutRGBOutdoor[1][13]	0239		
    {0x0F12,  0x0278},	 //#SARR_usDualGammaLutRGBOutdoor[1][14]	0278		
    {0x0F12,  0x02E0},	 //#SARR_usDualGammaLutRGBOutdoor[1][15]	02E0		
    {0x0F12,  0x0333},	 //#SARR_usDualGammaLutRGBOutdoor[1][16]	0333		
    {0x0F12,  0x037B},	 //#SARR_usDualGammaLutRGBOutdoor[1][17]	037B		
    {0x0F12,  0x03BF},	 //#SARR_usDualGammaLutRGBOutdoor[1][18]	03BF		
    {0x0F12,  0x03FF},	 //#SARR_usDualGammaLutRGBOutdoor[1][19]	03FF		
    {0x0F12,  0x0000},	 //#SARR_usDualGammaLutRGBOutdoor[2][0]	0000			
    {0x0F12,  0x0008},	 //#SARR_usDualGammaLutRGBOutdoor[2][1]	0008			
    {0x0F12,  0x0013},	 //#SARR_usDualGammaLutRGBOutdoor[2][2]	0013			
    {0x0F12,  0x002C},	 //#SARR_usDualGammaLutRGBOutdoor[2][3]	002C			
    {0x0F12,  0x0061},	 //#SARR_usDualGammaLutRGBOutdoor[2][4]	0061			
    {0x0F12,  0x00C8},	 //#SARR_usDualGammaLutRGBOutdoor[2][5]	00C8			
    {0x0F12,  0x0113},	 //#SARR_usDualGammaLutRGBOutdoor[2][6]	0113			
    {0x0F12,  0x0132},	 //#SARR_usDualGammaLutRGBOutdoor[2][7]	0132			
    {0x0F12,  0x014C},	 //#SARR_usDualGammaLutRGBOutdoor[2][8]	014C			
    {0x0F12,  0x0179},	 //#SARR_usDualGammaLutRGBOutdoor[2][9]	0179			
    {0x0F12,  0x01A4},	 //#SARR_usDualGammaLutRGBOutdoor[2][10]	01A4		
    {0x0F12,  0x01CD},	 //#SARR_usDualGammaLutRGBOutdoor[2][11]	01CD		
    {0x0F12,  0x01F4},	 //#SARR_usDualGammaLutRGBOutdoor[2][12]	01F4		
    {0x0F12,  0x0239},	 //#SARR_usDualGammaLutRGBOutdoor[2][13]	0239		
    {0x0F12,  0x0278},	 //#SARR_usDualGammaLutRGBOutdoor[2][14]	0278		
    {0x0F12,  0x02E0},	 //#SARR_usDualGammaLutRGBOutdoor[2][15]	02E0		
    {0x0F12,  0x0333},	 //#SARR_usDualGammaLutRGBOutdoor[2][16]	0333		
    {0x0F12,  0x037B},	 //#SARR_usDualGammaLutRGBOutdoor[2][17]	037B		
    {0x0F12,  0x03BF},	 //#SARR_usDualGammaLutRGBOutdoor[2][18]	03BF		
    {0x0F12,  0x03FF},	 //#SARR_usDualGammaLutRGBOutdoor[2][19]	03FF		

    //============================================================
    //CCM
    //============================================================ 
    {0x002A,  0x06A6},
    {0x0F12,  0x00C0},    	//SARR_AwbCcmCord[0]                        	   
    {0x0F12,  0x00F8},    	//SARR_AwbCcmCord[1]                        	   
    {0x0F12,  0x0112},    	//SARR_AwbCcmCord[2]                        	   
    {0x0F12,  0x014A},    	//SARR_AwbCcmCord[3]                        	   
    {0x0F12,  0x0156},    	//SARR_AwbCcmCord[4]                        	   
    {0x0F12,  0x017F},    	//SARR_AwbCcmCord[5]    

    //	param_start	TVAR_wbt_pBaseCcms 
    // Horizon
    {0x002A,    0x33A4},
    {0x0F12,	0x01C3},//02D5	//TVAR_wbt_pBaseCcms[0]
    {0x0F12,	0xFF89},//FF53	//TVAR_wbt_pBaseCcms[1]
    {0x0F12,	0xFFE5},//FF83	//TVAR_wbt_pBaseCcms[2]
    {0x0F12,	0xFF26},//FEE9	//TVAR_wbt_pBaseCcms[3]
    {0x0F12,	0x028E},//01A2	//TVAR_wbt_pBaseCcms[4]
    {0x0F12,	0xFF80},//FDFE	//TVAR_wbt_pBaseCcms[5]
    {0x0F12,	0x0002},//FFFC	//TVAR_wbt_pBaseCcms[6]
    {0x0F12,	0xFFA8},//FF5F	//TVAR_wbt_pBaseCcms[7]
    {0x0F12,	0x01F0},//02AD	//TVAR_wbt_pBaseCcms[8]
    {0x0F12,	0x0125},//0125	//TVAR_wbt_pBaseCcms[9]
    {0x0F12,	0x0119},//0119	//TVAR_wbt_pBaseCcms[10]
    {0x0F12,	0xFE5A},//FE5A	//TVAR_wbt_pBaseCcms[11]
    {0x0F12,	0x0179},//00D9	//TVAR_wbt_pBaseCcms[12]
    {0x0F12,	0xFF8A},//FF26	//TVAR_wbt_pBaseCcms[13]
    {0x0F12,	0x0180},//013C	//TVAR_wbt_pBaseCcms[14]
    {0x0F12,	0xFEC2},//FEC0	//TVAR_wbt_pBaseCcms[15]
    {0x0F12,	0x0176},//01F3	//TVAR_wbt_pBaseCcms[16]
    {0x0F12,	0x0094},//0109	//TVAR_wbt_pBaseCcms[17] 
    //inca                                       
    {0x0F12,	0x01C3},//022B	//TVAR_wbt_pBaseCcms[18]
    {0x0F12,	0xFF89},//FF73	//TVAR_wbt_pBaseCcms[19]
    {0x0F12,	0xFFE5},//FFC3	//TVAR_wbt_pBaseCcms[20]
    {0x0F12,	0xFF26},//FEED	//TVAR_wbt_pBaseCcms[21]
    {0x0F12,	0x028E},//01B9	//TVAR_wbt_pBaseCcms[22]
    {0x0F12,	0xFF80},//FF0D	//TVAR_wbt_pBaseCcms[23]
    {0x0F12,	0x0002},//FFE7	//TVAR_wbt_pBaseCcms[24]
    {0x0F12,	0xFFA8},//FFD3	//TVAR_wbt_pBaseCcms[25]
    {0x0F12,	0x01F0},//022F	//TVAR_wbt_pBaseCcms[26]
    {0x0F12,	0x0125},//0118	//TVAR_wbt_pBaseCcms[27]
    {0x0F12,	0x0119},//012C	//TVAR_wbt_pBaseCcms[28]
    {0x0F12,	0xFE5A},//FED0	//TVAR_wbt_pBaseCcms[29]
    {0x0F12,	0x0179},//0212	//TVAR_wbt_pBaseCcms[30]
    {0x0F12,	0xFF8A},//FF46	//TVAR_wbt_pBaseCcms[31]
    {0x0F12,	0x0180},//01F2	//TVAR_wbt_pBaseCcms[32]
    {0x0F12,	0xFEC2},//FED4	//TVAR_wbt_pBaseCcms[33]
    {0x0F12,	0x0176},//018C	//TVAR_wbt_pBaseCcms[34]
    {0x0F12,	0x0094},//013C	//TVAR_wbt_pBaseCcms[35]
    //WW
    {0x0F12,	0x01CA},//0121	//TVAR_wbt_pBaseCcms[36]
    {0x0F12,	0xFF89},//FF8D	//TVAR_wbt_pBaseCcms[37]
    {0x0F12,	0xFFE0},//FFA7	//TVAR_wbt_pBaseCcms[38]
    {0x0F12,	0xFF26},//FF3D	//TVAR_wbt_pBaseCcms[39]
    {0x0F12,	0x028E},//02D0	//TVAR_wbt_pBaseCcms[40]
    {0x0F12,	0xFF80},//FE31	//TVAR_wbt_pBaseCcms[41]
    {0x0F12,	0x0020},//0016	//TVAR_wbt_pBaseCcms[42]
    {0x0F12,	0xFFF8},//003B	//TVAR_wbt_pBaseCcms[43]
    {0x0F12,	0x01E0},//02E1	//TVAR_wbt_pBaseCcms[44]
    {0x0F12,	0x0120},//0175	//TVAR_wbt_pBaseCcms[45]
    {0x0F12,	0x00FA},//009C	//TVAR_wbt_pBaseCcms[46]
    {0x0F12,	0xFF12},//FE3A	//TVAR_wbt_pBaseCcms[47]
    {0x0F12,	0x0179},//0189	//TVAR_wbt_pBaseCcms[48]
    {0x0F12,	0xFF8A},//FF6A	//TVAR_wbt_pBaseCcms[49]
    {0x0F12,	0x0180},//0298	//TVAR_wbt_pBaseCcms[50]
    {0x0F12,	0xFEC2},//FF14	//TVAR_wbt_pBaseCcms[51]
    {0x0F12,	0x0176},//01BD	//TVAR_wbt_pBaseCcms[52]
    {0x0F12,	0x0094},//00FB	//TVAR_wbt_pBaseCcms[53]
    //CWF                                       
    {0x0F12,	0x01CA},//018A//0262	//TVAR_wbt_pBaseCcms[54]
    {0x0F12,	0xFF89},//FFC9//FFAB	//TVAR_wbt_pBaseCcms[55]
    {0x0F12,	0xFFE0},//0005//FFFB	//TVAR_wbt_pBaseCcms[56]
    {0x0F12,	0xFF26},//FFC1	//TVAR_wbt_pBaseCcms[57]
    {0x0F12,	0x028E},//0292	//TVAR_wbt_pBaseCcms[58]
    {0x0F12,	0xFF80},//FF81	//TVAR_wbt_pBaseCcms[59]
    {0x0F12,	0x0020},//0069	//TVAR_wbt_pBaseCcms[60]
    {0x0F12,	0xFFF8},//0057	//TVAR_wbt_pBaseCcms[61]
    {0x0F12,	0x01E0},//0296	//TVAR_wbt_pBaseCcms[62]
    {0x0F12,	0x0120},//00FF//015F	//TVAR_wbt_pBaseCcms[63]
    {0x0F12,	0x00FA},//00E2//0112	//TVAR_wbt_pBaseCcms[64]
    {0x0F12,	0xFF12},//FF4D//FF17	//TVAR_wbt_pBaseCcms[65]
    {0x0F12,	0x0179},//024E	//TVAR_wbt_pBaseCcms[66]
    {0x0F12,	0xFF8A},//0001	//TVAR_wbt_pBaseCcms[67]
    {0x0F12,	0x0180},//0276	//TVAR_wbt_pBaseCcms[68]
    {0x0F12,	0xFEC2},//FEE2	//TVAR_wbt_pBaseCcms[69]
    {0x0F12,	0x0176},//0236	//TVAR_wbt_pBaseCcms[70]
    {0x0F12,	0x0094},//014A	//TVAR_wbt_pBaseCcms[71] 
    //D50
    //BEGIN: DTS2012071201781 modify by huyouhua at 2012-7-24 for D65 Sat too small
    //{0x0F12,	0x018A},//01C5	//TVAR_wbt_pBaseCcms[72]
    //{0x0F12,	0xFFC9},//FFA8	//TVAR_wbt_pBaseCcms[73]
    //{0x0F12,	0x0005},//FFBA	//TVAR_wbt_pBaseCcms[74]
    {0x0F12,	0x01A9},//01C5	//TVAR_wbt_pBaseCcms[72]
    {0x0F12,	0xFFB6},//FFA8	//TVAR_wbt_pBaseCcms[73]                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               
    {0x0F12,	0xFFF9},//FFBA	//TVAR_wbt_pBaseCcms[74]                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               
    //END: DTS2012071201781 modify by huyouhua at 2012-7-24 for D65 Sat too small
    {0x0F12,	0xFF56},//FF06	//TVAR_wbt_pBaseCcms[75]
    {0x0F12,	0x0230},//0258	//TVAR_wbt_pBaseCcms[76]
    {0x0F12,	0xFFA0},//FF52	//TVAR_wbt_pBaseCcms[77]
    //BEGIN: DTS2012071201781 modify by huyouhua at 2012-7-24 for D65 Sat too small
    //{0x0F12,	0x0020},//0036	//TVAR_wbt_pBaseCcms[78]
    //{0x0F12,	0xFFF8},//FFFA	//TVAR_wbt_pBaseCcms[79]
    //{0x0F12,	0x01E0},//01D7	//TVAR_wbt_pBaseCcms[80]
    //{0x0F12,	0x00FF},//00DE	//TVAR_wbt_pBaseCcms[81]
    //{0x0F12,	0x00E2},//013B	//TVAR_wbt_pBaseCcms[82]
    //{0x0F12,	0xFF4D},//FF30	//TVAR_wbt_pBaseCcms[83]
    {0x0F12,	0x000E},//0036	//TVAR_wbt_pBaseCcms[78]                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               
    {0x0F12,	0xFFE0},//FFFA	//TVAR_wbt_pBaseCcms[79]                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               
    {0x0F12,	0x020A},//01D7	//TVAR_wbt_pBaseCcms[80]                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               
    {0x0F12,	0x010C},//00DE	//TVAR_wbt_pBaseCcms[81]                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               
    {0x0F12,	0x00EC},//013B	//TVAR_wbt_pBaseCcms[82]                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               
    {0x0F12,	0xFF36},//FF30	//TVAR_wbt_pBaseCcms[83]                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               
    //END: DTS2012071201781 modify by huyouhua at 2012-7-24 for D65 Sat too small
    {0x0F12,	0x0179},//0168	//TVAR_wbt_pBaseCcms[84]
    {0x0F12,	0xFF8A},//FF6F	//TVAR_wbt_pBaseCcms[85]
    {0x0F12,	0x0180},//0164	//TVAR_wbt_pBaseCcms[86]
    {0x0F12,	0xFEC2},//FEDB	//TVAR_wbt_pBaseCcms[87]
    {0x0F12,	0x0176},//0195	//TVAR_wbt_pBaseCcms[88]
    {0x0F12,	0x0094},//00E7	//TVAR_wbt_pBaseCcms[89]
    //D65
    //BEGIN: DTS2012071201781 modify by huyouhua at 2012-7-24 for D65 Sat too small
    //{0x0F12,	0x018A},//019E	//TVAR_wbt_pBaseCcms[90]
    //{0x0F12,	0xFFC9},//FF8A	//TVAR_wbt_pBaseCcms[91]
    //{0x0F12,	0x0005},//FFCE	//TVAR_wbt_pBaseCcms[92]
    {0x0F12,	0x01A9},//019E	//TVAR_wbt_pBaseCcms[90]                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               
    {0x0F12,	0xFFB6},//FF8A	//TVAR_wbt_pBaseCcms[91]                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               
    {0x0F12,	0xFFF9},//FFCE	//TVAR_wbt_pBaseCcms[92]                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               
    //END: DTS2012071201781 modify by huyouhua at 2012-7-24 for D65 Sat too small
    {0x0F12,	0xFF56},//FF06	//TVAR_wbt_pBaseCcms[93]
    {0x0F12,	0x0230},//0258	//TVAR_wbt_pBaseCcms[94]
    {0x0F12,	0xFFA0},//FF52	//TVAR_wbt_pBaseCcms[95]
    //BEGIN: DTS2012071201781 modify by huyouhua at 2012-7-24 for D65 Sat too small
    //{0x0F12,	0x0020},//0036	//TVAR_wbt_pBaseCcms[96]
    //{0x0F12,	0xFFF8},//FFFA	//TVAR_wbt_pBaseCcms[97]
    //{0x0F12,	0x01E0},//01D7	//TVAR_wbt_pBaseCcms[98]
    //{0x0F12,	0x00FF},//00DE	//TVAR_wbt_pBaseCcms[99]
    //{0x0F12,	0x00E2},//013B	//TVAR_wbt_pBaseCcms[100]
    //{0x0F12,	0xFF4D},//FF30	//TVAR_wbt_pBaseCcms[101]
    {0x0F12,	0x000E},//0036	//TVAR_wbt_pBaseCcms[96]                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               
    {0x0F12,	0xFFE0},//FFFA	//TVAR_wbt_pBaseCcms[97]                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               
    {0x0F12,	0x020A},//01D7	//TVAR_wbt_pBaseCcms[98]                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               
    {0x0F12,	0x010C},//00DE	//TVAR_wbt_pBaseCcms[99]                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               
    {0x0F12,	0x00EC},//013B	//TVAR_wbt_pBaseCcms[100]                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              
    {0x0F12,	0xFF36},//FF30	//TVAR_wbt_pBaseCcms[101]                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              
    //END: DTS2012071201781 modify by huyouhua at 2012-7-24 for D65 Sat too small
    {0x0F12,	0x0179},//0168	//TVAR_wbt_pBaseCcms[102]
    {0x0F12,	0xFF8A},//FF6F	//TVAR_wbt_pBaseCcms[103]
    {0x0F12,	0x0180},//0164	//TVAR_wbt_pBaseCcms[104]
    {0x0F12,	0xFEC2},//FEDB	//TVAR_wbt_pBaseCcms[105]
    {0x0F12,	0x0176},//0195	//TVAR_wbt_pBaseCcms[106]
    {0x0F12,	0x0094},//00E7	//TVAR_wbt_pBaseCcms[107]
    //	param_end	TVAR_wbt_pBaseCcms
    //	param_start	TVAR_wbt_pOutdoorCcm  
    {0x002A,    0x3380},
    {0x0F12,	0x018A},//01C7	//TVAR_wbt_pOutdoorCcm[0]
    {0x0F12,	0xFFC9},//FFA0	//TVAR_wbt_pOutdoorCcm[1]
    {0x0F12,	0x0005},//FFE8	//TVAR_wbt_pOutdoorCcm[2]
    {0x0F12,	0xFF26},//FF11	//TVAR_wbt_pOutdoorCcm[3]
    {0x0F12,	0x028E},//01F4	//TVAR_wbt_pOutdoorCcm[4]
    {0x0F12,	0xFF80},//FF38	//TVAR_wbt_pOutdoorCcm[5]
    {0x0F12,	0x0020},//FFDF	//TVAR_wbt_pOutdoorCcm[6]
    {0x0F12,	0xFFF8},//FFD3	//TVAR_wbt_pOutdoorCcm[7]
    {0x0F12,	0x01E0},//01CC	//TVAR_wbt_pOutdoorCcm[8]
    {0x0F12,	0x00FF},//011D	//TVAR_wbt_pOutdoorCcm[9]
    {0x0F12,	0x00E2},//0157	//TVAR_wbt_pOutdoorCcm[10]
    {0x0F12,	0xFF4D},//FF16	//TVAR_wbt_pOutdoorCcm[11]
    {0x0F12,	0x0179},//01DA	//TVAR_wbt_pOutdoorCcm[12]
    {0x0F12,	0xFF8A},//FF3A	//TVAR_wbt_pOutdoorCcm[13]
    {0x0F12,	0x0180},//01B6	//TVAR_wbt_pOutdoorCcm[14]
    {0x0F12,	0xFEC2},//FF2A	//TVAR_wbt_pOutdoorCcm[15]
    {0x0F12,	0x0176},//0176	//TVAR_wbt_pOutdoorCcm[16]
    {0x0F12,	0x0094},//0114	//TVAR_wbt_pOutdoorCcm[17]
    //	param_end	TVAR_wbt_pOutdoorCcm
    //============================================================
    //AFIT
    //============================================================
    //	param_start	afit_uNoiseIndInDoor
    //============================================================  
    {0x002A,    0x0764},
    {0x0F12,	0x0041},	//afit_uNoiseIndInDoor[0]
    {0x0F12,	0x00A5},	//afit_uNoiseIndInDoor[1]
    {0x0F12,	0x016B},	//afit_uNoiseIndInDoor[2]
    {0x0F12,	0x0226},	//afit_uNoiseIndInDoor[3]
    {0x0F12,	0x02EA},	//afit_uNoiseIndInDoor[4]
    //	param_end	afit_uNoiseIndInDoor
    //	param_start	TVAR_afit_pBaseVals
    {0x002A,    0x07C4},
    {0x0F12,	0x0000},	//TVAR_afit_pBaseVals[0]
    {0x0F12,	0x0000},	//TVAR_afit_pBaseVals[1]
    {0x0F12,	0x0000},	//TVAR_afit_pBaseVals[2]
    {0x0F12,	0x0000},	//TVAR_afit_pBaseVals[3]
    {0x0F12,	0x0000},	//TVAR_afit_pBaseVals[4]
    {0x0F12,	0x001E},	//TVAR_afit_pBaseVals[5]
    {0x0F12,	0x03FF},	//TVAR_afit_pBaseVals[6]
    {0x0F12,	0x009C},	//TVAR_afit_pBaseVals[7]
    {0x0F12,	0x017C},	//TVAR_afit_pBaseVals[8]
    {0x0F12,	0x03FF},	//TVAR_afit_pBaseVals[9]
    {0x0F12,	0x000C},	//TVAR_afit_pBaseVals[10]
    {0x0F12,	0x0010},	//TVAR_afit_pBaseVals[11]
    {0x0F12,	0x0104},	//TVAR_afit_pBaseVals[12]
    {0x0F12,	0x03E8},	//TVAR_afit_pBaseVals[13]
    {0x0F12,	0x0023},	//TVAR_afit_pBaseVals[14]
    {0x0F12,	0x012C},	//TVAR_afit_pBaseVals[15]
    {0x0F12,	0x0070},	//TVAR_afit_pBaseVals[16]
    {0x0F12,	0x0004},	//TVAR_afit_pBaseVals[17]
    {0x0F12,	0x0004},	//TVAR_afit_pBaseVals[18]
    {0x0F12,	0x01AA},	//TVAR_afit_pBaseVals[19]
    {0x0F12,	0x0064},	//TVAR_afit_pBaseVals[20]
    {0x0F12,	0x0064},	//TVAR_afit_pBaseVals[21]
    {0x0F12,	0x000A},	//TVAR_afit_pBaseVals[22]
    {0x0F12,	0x000A},	//TVAR_afit_pBaseVals[23]
    {0x0F12,	0x002A},	//TVAR_afit_pBaseVals[24]
    {0x0F12,	0x0024},	//TVAR_afit_pBaseVals[25]
    {0x0F12,	0x002A},	//TVAR_afit_pBaseVals[26]
    {0x0F12,	0x0024},	//TVAR_afit_pBaseVals[27]
    {0x0F12,	0x002A},	//TVAR_afit_pBaseVals[28]
    {0x0F12,	0x0024},	//TVAR_afit_pBaseVals[29]
    {0x0F12,	0x0A0F},	//TVAR_afit_pBaseVals[30]
    {0x0F12,	0x1701},	//TVAR_afit_pBaseVals[31]
    {0x0F12,	0x0229},	//TVAR_afit_pBaseVals[32]
    {0x0F12,	0x1403},	//TVAR_afit_pBaseVals[33]
    {0x0F12,	0x0000},	//TVAR_afit_pBaseVals[34]
    {0x0F12,	0x0000},	//TVAR_afit_pBaseVals[35]
    {0x0F12,	0x0000},	//TVAR_afit_pBaseVals[36]
    {0x0F12,	0x00FF},	//TVAR_afit_pBaseVals[37]
    {0x0F12,	0x053B},	//TVAR_afit_pBaseVals[38]
    {0x0F12,	0x0505},	//TVAR_afit_pBaseVals[39]
    {0x0F12,	0x0301},	//TVAR_afit_pBaseVals[40]
    {0x0F12,	0x8007},	//TVAR_afit_pBaseVals[41]
    {0x0F12,	0x051E},	//TVAR_afit_pBaseVals[42]
    {0x0F12,	0x0A1E},	//TVAR_afit_pBaseVals[43]
    {0x0F12,	0x0F0F},	//TVAR_afit_pBaseVals[44]
    {0x0F12,	0x0A05},	//TVAR_afit_pBaseVals[45]
    {0x0F12,	0x103C},	//TVAR_afit_pBaseVals[46]
    {0x0F12,	0x0A28},	//TVAR_afit_pBaseVals[47]
    {0x0F12,	0x0002},	//TVAR_afit_pBaseVals[48]
    {0x0F12,	0x00FF},	//TVAR_afit_pBaseVals[49]
    {0x0F12,	0x1002},	//TVAR_afit_pBaseVals[50]
    {0x0F12,	0x001D},	//TVAR_afit_pBaseVals[51]
    {0x0F12,	0x0900},	//TVAR_afit_pBaseVals[52]
    {0x0F12,	0x0600},	//TVAR_afit_pBaseVals[53]
    {0x0F12,	0x0504},	//TVAR_afit_pBaseVals[54]
    {0x0F12,	0x0305},	//TVAR_afit_pBaseVals[55]
    {0x0F12,	0x5A03},	//TVAR_afit_pBaseVals[56]
    {0x0F12,	0x006E},	//TVAR_afit_pBaseVals[57]
    {0x0F12,	0x0A78},	//TVAR_afit_pBaseVals[58]
    {0x0F12,	0x0080},	//TVAR_afit_pBaseVals[59]
    {0x0F12,	0x3232},	//TVAR_afit_pBaseVals[60]
    {0x0F12,	0x0101},	//TVAR_afit_pBaseVals[61]
    {0x0F12,	0x5001},	//TVAR_afit_pBaseVals[62]
    {0x0F12,	0x7850},	//TVAR_afit_pBaseVals[63]
    {0x0F12,	0x2878},	//TVAR_afit_pBaseVals[64]
    {0x0F12,	0x0A00},	//TVAR_afit_pBaseVals[65]
    {0x0F12,	0x1403},	//TVAR_afit_pBaseVals[66]
    {0x0F12,	0x1E07},	//TVAR_afit_pBaseVals[67]
    {0x0F12,	0x070A},	//TVAR_afit_pBaseVals[68]
    {0x0F12,	0x32FF},	//TVAR_afit_pBaseVals[69]
    {0x0F12,	0x5004},	//TVAR_afit_pBaseVals[70]
    {0x0F12,	0x0F40},	//TVAR_afit_pBaseVals[71]
    {0x0F12,	0x400F},	//TVAR_afit_pBaseVals[72]
    {0x0F12,	0x0204},	//TVAR_afit_pBaseVals[73]
    {0x0F12,	0x3203},	//TVAR_afit_pBaseVals[74]
    {0x0F12,	0x0132},	//TVAR_afit_pBaseVals[75]
    {0x0F12,	0x0101},	//TVAR_afit_pBaseVals[76]
    {0x0F12,	0x5050},	//TVAR_afit_pBaseVals[77]
    {0x0F12,	0x7878},	//TVAR_afit_pBaseVals[78]
    {0x0F12,	0x0028},	//TVAR_afit_pBaseVals[79]
    {0x0F12,	0x030A},	//TVAR_afit_pBaseVals[80]
    {0x0F12,	0x0714},	//TVAR_afit_pBaseVals[81]
    {0x0F12,	0x0A1E},	//TVAR_afit_pBaseVals[82]
    {0x0F12,	0xFF07},	//TVAR_afit_pBaseVals[83]
    {0x0F12,	0x0432},	//TVAR_afit_pBaseVals[84]
    {0x0F12,	0x4050},	//TVAR_afit_pBaseVals[85]
    {0x0F12,	0x0F0F},	//TVAR_afit_pBaseVals[86]
    {0x0F12,	0x0440},	//TVAR_afit_pBaseVals[87]
    {0x0F12,	0x0302},	//TVAR_afit_pBaseVals[88]
    {0x0F12,	0x3232},	//TVAR_afit_pBaseVals[89]
    {0x0F12,	0x0101},	//TVAR_afit_pBaseVals[90]
    {0x0F12,	0x5001},	//TVAR_afit_pBaseVals[91]
    {0x0F12,	0x7850},	//TVAR_afit_pBaseVals[92]
    {0x0F12,	0x2878},	//TVAR_afit_pBaseVals[93]
    {0x0F12,	0x0A00},	//TVAR_afit_pBaseVals[94]
    {0x0F12,	0x1403},	//TVAR_afit_pBaseVals[95]
    {0x0F12,	0x1E07},	//TVAR_afit_pBaseVals[96]
    {0x0F12,	0x070A},	//TVAR_afit_pBaseVals[97]
    {0x0F12,	0x32FF},	//TVAR_afit_pBaseVals[98]
    {0x0F12,	0x5004},	//TVAR_afit_pBaseVals[99]
    {0x0F12,	0x0F40},	//TVAR_afit_pBaseVals[100]
    {0x0F12,	0x400F},	//TVAR_afit_pBaseVals[101]
    {0x0F12,	0x0204},	//TVAR_afit_pBaseVals[102]
    {0x0F12,	0x0003},	//TVAR_afit_pBaseVals[103]
    {0x0F12,	0x0000},	//TVAR_afit_pBaseVals[104]
    {0x0F12,	0x0000},	//TVAR_afit_pBaseVals[105]
    {0x0F12,	0x0000},	//TVAR_afit_pBaseVals[106]
    {0x0F12,	0x0000},	//TVAR_afit_pBaseVals[107]
    {0x0F12,	0x0000},	//TVAR_afit_pBaseVals[108]
    {0x0F12,	0x001E},	//TVAR_afit_pBaseVals[109]
    {0x0F12,	0x03FF},	//TVAR_afit_pBaseVals[110]
    {0x0F12,	0x009C},	//TVAR_afit_pBaseVals[111]
    {0x0F12,	0x017C},	//TVAR_afit_pBaseVals[112]
    {0x0F12,	0x03FF},	//TVAR_afit_pBaseVals[113]
    {0x0F12,	0x000C},	//TVAR_afit_pBaseVals[114]
    {0x0F12,	0x0010},	//TVAR_afit_pBaseVals[115]
    {0x0F12,	0x0104},	//TVAR_afit_pBaseVals[116]
    {0x0F12,	0x03E8},	//TVAR_afit_pBaseVals[117]
    {0x0F12,	0x0023},	//TVAR_afit_pBaseVals[118]
    {0x0F12,	0x012C},	//TVAR_afit_pBaseVals[119]
    {0x0F12,	0x0070},	//TVAR_afit_pBaseVals[120]
    {0x0F12,	0x0004},	//TVAR_afit_pBaseVals[121]
    {0x0F12,	0x0004},	//TVAR_afit_pBaseVals[122]
    {0x0F12,	0x01AA},	//TVAR_afit_pBaseVals[123]
    {0x0F12,	0x001E},	//TVAR_afit_pBaseVals[124]
    {0x0F12,	0x001E},	//TVAR_afit_pBaseVals[125]
    {0x0F12,	0x0005},	//TVAR_afit_pBaseVals[126]
    {0x0F12,	0x0005},	//TVAR_afit_pBaseVals[127]
    {0x0F12,	0x002A},	//TVAR_afit_pBaseVals[128]
    {0x0F12,	0x0024},	//TVAR_afit_pBaseVals[129]
    {0x0F12,	0x002A},	//TVAR_afit_pBaseVals[130]
    {0x0F12,	0x0024},	//TVAR_afit_pBaseVals[131]
    {0x0F12,	0x002A},	//TVAR_afit_pBaseVals[132]
    {0x0F12,	0x0024},	//TVAR_afit_pBaseVals[133]
    {0x0F12,	0x0A0F},	//TVAR_afit_pBaseVals[134]
    {0x0F12,	0x1701},	//TVAR_afit_pBaseVals[135]
    {0x0F12,	0x0229},	//TVAR_afit_pBaseVals[136]
    {0x0F12,	0x1403},	//TVAR_afit_pBaseVals[137]
    {0x0F12,	0x0000},	//TVAR_afit_pBaseVals[138]
    {0x0F12,	0x0000},	//TVAR_afit_pBaseVals[139]
    {0x0F12,	0x0101},	//TVAR_afit_pBaseVals[140]
    {0x0F12,	0x00FF},	//TVAR_afit_pBaseVals[141]
    {0x0F12,	0x053B},	//TVAR_afit_pBaseVals[142]
    {0x0F12,	0x0505},	//TVAR_afit_pBaseVals[143]
    {0x0F12,	0x0301},	//TVAR_afit_pBaseVals[144]
    {0x0F12,	0x8007},	//TVAR_afit_pBaseVals[145]
    {0x0F12,	0x051E},	//TVAR_afit_pBaseVals[146]
    {0x0F12,	0x0A1E},	//TVAR_afit_pBaseVals[147]
    {0x0F12,	0x0F0F},	//TVAR_afit_pBaseVals[148]
    {0x0F12,	0x0A04},	//TVAR_afit_pBaseVals[149]
    {0x0F12,	0x103C},	//TVAR_afit_pBaseVals[150]
    {0x0F12,	0x0A28},	//TVAR_afit_pBaseVals[151]
    {0x0F12,	0x0002},	//TVAR_afit_pBaseVals[152]
    {0x0F12,	0x00FF},	//TVAR_afit_pBaseVals[153]
    {0x0F12,	0x1002},	//TVAR_afit_pBaseVals[154]
    {0x0F12,	0x001D},	//TVAR_afit_pBaseVals[155]
    {0x0F12,	0x0900},	//TVAR_afit_pBaseVals[156]
    {0x0F12,	0x0600},	//TVAR_afit_pBaseVals[157]
    {0x0F12,	0x0504},	//TVAR_afit_pBaseVals[158]
    {0x0F12,	0x0305},	//TVAR_afit_pBaseVals[159]
    {0x0F12,	0x5F03},	//TVAR_afit_pBaseVals[160]
    {0x0F12,	0x0080},	//TVAR_afit_pBaseVals[161]
    {0x0F12,	0x0080},	//TVAR_afit_pBaseVals[162]
    {0x0F12,	0x0080},	//TVAR_afit_pBaseVals[163]
    {0x0F12,	0x3232},	//TVAR_afit_pBaseVals[164]
    {0x0F12,	0x0101},	//TVAR_afit_pBaseVals[165]
    {0x0F12,	0x2A01},	//TVAR_afit_pBaseVals[166]
    {0x0F12,	0x3232},	//TVAR_afit_pBaseVals[167]
    {0x0F12,	0x2830},	//TVAR_afit_pBaseVals[168]
    {0x0F12,	0x0A00},	//TVAR_afit_pBaseVals[169]
    {0x0F12,	0x1403},	//TVAR_afit_pBaseVals[170]
    {0x0F12,	0x1E07},	//TVAR_afit_pBaseVals[171]
    {0x0F12,	0x070A},	//TVAR_afit_pBaseVals[172]
    {0x0F12,	0x32FF},	//TVAR_afit_pBaseVals[173]
    {0x0F12,	0x5004},	//TVAR_afit_pBaseVals[174]
    {0x0F12,	0x0F40},	//TVAR_afit_pBaseVals[175]
    {0x0F12,	0x400F},	//TVAR_afit_pBaseVals[176]
    {0x0F12,	0x0204},	//TVAR_afit_pBaseVals[177]
    {0x0F12,	0x3203},	//TVAR_afit_pBaseVals[178]
    {0x0F12,	0x0132},	//TVAR_afit_pBaseVals[179]
    {0x0F12,	0x0101},	//TVAR_afit_pBaseVals[180]
    {0x0F12,	0x262A},	//TVAR_afit_pBaseVals[181]
    {0x0F12,	0x3032},	//TVAR_afit_pBaseVals[182]
    {0x0F12,	0x0028},	//TVAR_afit_pBaseVals[183]
    {0x0F12,	0x030A},	//TVAR_afit_pBaseVals[184]
    {0x0F12,	0x0714},	//TVAR_afit_pBaseVals[185]
    {0x0F12,	0x0A1E},	//TVAR_afit_pBaseVals[186]
    {0x0F12,	0xFF07},	//TVAR_afit_pBaseVals[187]
    {0x0F12,	0x0432},	//TVAR_afit_pBaseVals[188]
    {0x0F12,	0x4050},	//TVAR_afit_pBaseVals[189]
    {0x0F12,	0x0F0F},	//TVAR_afit_pBaseVals[190]
    {0x0F12,	0x0440},	//TVAR_afit_pBaseVals[191]
    {0x0F12,	0x0302},	//TVAR_afit_pBaseVals[192]
    {0x0F12,	0x3232},	//TVAR_afit_pBaseVals[193]
    {0x0F12,	0x0101},	//TVAR_afit_pBaseVals[194]
    {0x0F12,	0x2A01},	//TVAR_afit_pBaseVals[195]
    {0x0F12,	0x3226},	//TVAR_afit_pBaseVals[196]
    {0x0F12,	0x2830},	//TVAR_afit_pBaseVals[197]
    {0x0F12,	0x0A00},	//TVAR_afit_pBaseVals[198]
    {0x0F12,	0x1403},	//TVAR_afit_pBaseVals[199]
    {0x0F12,	0x1E07},	//TVAR_afit_pBaseVals[200]
    {0x0F12,	0x070A},	//TVAR_afit_pBaseVals[201]
    {0x0F12,	0x32FF},	//TVAR_afit_pBaseVals[202]
    {0x0F12,	0x5004},	//TVAR_afit_pBaseVals[203]
    {0x0F12,	0x0F40},	//TVAR_afit_pBaseVals[204]
    {0x0F12,	0x400F},	//TVAR_afit_pBaseVals[205]
    {0x0F12,	0x0204},	//TVAR_afit_pBaseVals[206]
    {0x0F12,	0x0003},	//TVAR_afit_pBaseVals[207]
    {0x0F12,	0x0000},	//TVAR_afit_pBaseVals[208]
    {0x0F12,	0x0000},	//TVAR_afit_pBaseVals[209]
    {0x0F12,	0x000f},	//TVAR_afit_pBaseVals[210]//DTS2012071201781:modify by huyouhua at 2012-7-23
    {0x0F12,	0x0000},	//TVAR_afit_pBaseVals[211]
    {0x0F12,	0x0000},	//TVAR_afit_pBaseVals[212]
    {0x0F12,	0x001E},	//TVAR_afit_pBaseVals[213]
    {0x0F12,	0x03FF},	//TVAR_afit_pBaseVals[214]
    {0x0F12,	0x009C},	//TVAR_afit_pBaseVals[215]
    {0x0F12,	0x017C},	//TVAR_afit_pBaseVals[216]
    {0x0F12,	0x03FF},	//TVAR_afit_pBaseVals[217]
    {0x0F12,	0x000C},	//TVAR_afit_pBaseVals[218]
    {0x0F12,	0x0010},	//TVAR_afit_pBaseVals[219]
    {0x0F12,	0x012C},	//TVAR_afit_pBaseVals[220]
    {0x0F12,	0x03E8},	//TVAR_afit_pBaseVals[221]
    {0x0F12,	0x0041},	//TVAR_afit_pBaseVals[222]
    {0x0F12,	0x005A},	//TVAR_afit_pBaseVals[223]
    {0x0F12,	0x0070},	//TVAR_afit_pBaseVals[224]
    {0x0F12,	0x0004},	//TVAR_afit_pBaseVals[225]
    {0x0F12,	0x0004},	//TVAR_afit_pBaseVals[226]
    {0x0F12,	0x01AA},	//TVAR_afit_pBaseVals[227]
    {0x0F12,	0x001E},	//TVAR_afit_pBaseVals[228]
    {0x0F12,	0x001E},	//TVAR_afit_pBaseVals[229]
    {0x0F12,	0x000A},	//TVAR_afit_pBaseVals[230]
    {0x0F12,	0x000A},	//TVAR_afit_pBaseVals[231]
    {0x0F12,	0x0032},	//TVAR_afit_pBaseVals[232]
    {0x0F12,	0x0028},	//TVAR_afit_pBaseVals[233]
    {0x0F12,	0x0032},	//TVAR_afit_pBaseVals[234]
    {0x0F12,	0x0028},	//TVAR_afit_pBaseVals[235]
    {0x0F12,	0x0032},	//TVAR_afit_pBaseVals[236]
    {0x0F12,	0x0028},	//TVAR_afit_pBaseVals[237]
    {0x0F12,	0x0A0F},	//TVAR_afit_pBaseVals[238]
    {0x0F12,	0x1701},	//TVAR_afit_pBaseVals[239]
    {0x0F12,	0x0229},	//TVAR_afit_pBaseVals[240]
    {0x0F12,	0x1403},	//TVAR_afit_pBaseVals[241]
    {0x0F12,	0x0000},	//TVAR_afit_pBaseVals[242]
    {0x0F12,	0x0000},	//TVAR_afit_pBaseVals[243]
    {0x0F12,	0x0404},	//TVAR_afit_pBaseVals[244]
    {0x0F12,	0x00FF},	//TVAR_afit_pBaseVals[245]
    {0x0F12,	0x043B},	//TVAR_afit_pBaseVals[246]
    {0x0F12,	0x0505},	//TVAR_afit_pBaseVals[247]
    {0x0F12,	0x0301},	//TVAR_afit_pBaseVals[248]
    {0x0F12,	0x8007},	//TVAR_afit_pBaseVals[249]
    {0x0F12,	0x051E},	//TVAR_afit_pBaseVals[250]
    {0x0F12,	0x0A1E},	//TVAR_afit_pBaseVals[251]
    {0x0F12,	0x0F0F},	//TVAR_afit_pBaseVals[252]
    {0x0F12,	0x0A03},	//TVAR_afit_pBaseVals[253]
    {0x0F12,	0x0A3C},	//TVAR_afit_pBaseVals[254]
    {0x0F12,	0x0528},	//TVAR_afit_pBaseVals[255]
    {0x0F12,	0x0002},	//TVAR_afit_pBaseVals[256]
    {0x0F12,	0x00FF},	//TVAR_afit_pBaseVals[257]
    {0x0F12,	0x1002},	//TVAR_afit_pBaseVals[258]
    {0x0F12,	0x001D},	//TVAR_afit_pBaseVals[259]
    {0x0F12,	0x0900},	//TVAR_afit_pBaseVals[260]
    {0x0F12,	0x0600},	//TVAR_afit_pBaseVals[261]
    {0x0F12,	0x0504},	//TVAR_afit_pBaseVals[262]
    {0x0F12,	0x0305},	//TVAR_afit_pBaseVals[263]
    {0x0F12,	0x6902},	//TVAR_afit_pBaseVals[264]
    {0x0F12,	0x0080},	//TVAR_afit_pBaseVals[265]
    {0x0F12,	0x0080},	//TVAR_afit_pBaseVals[266]
    {0x0F12,	0x0080},	//TVAR_afit_pBaseVals[267]
    {0x0F12,	0x2D2D},	//TVAR_afit_pBaseVals[268]
    {0x0F12,	0x0101},	//TVAR_afit_pBaseVals[269]
    {0x0F12,	0x2001},	//TVAR_afit_pBaseVals[270]
    {0x0F12,	0x2026},	//TVAR_afit_pBaseVals[271]
    {0x0F12,	0x281E},	//TVAR_afit_pBaseVals[272]
    {0x0F12,	0x0A00},	//TVAR_afit_pBaseVals[273]
    {0x0F12,	0x0A03},	//TVAR_afit_pBaseVals[274]
    {0x0F12,	0x1E0A},	//TVAR_afit_pBaseVals[275]
    {0x0F12,	0x070A},	//TVAR_afit_pBaseVals[276]
    {0x0F12,	0x32FF},	//TVAR_afit_pBaseVals[277]
    {0x0F12,	0x5004},	//TVAR_afit_pBaseVals[278]
    {0x0F12,	0x0F40},	//TVAR_afit_pBaseVals[279]
    {0x0F12,	0x400F},	//TVAR_afit_pBaseVals[280]
    {0x0F12,	0x0204},	//TVAR_afit_pBaseVals[281]
    {0x0F12,	0x3203},	//TVAR_afit_pBaseVals[282]
    {0x0F12,	0x0132},	//TVAR_afit_pBaseVals[283]
    {0x0F12,	0x0101},	//TVAR_afit_pBaseVals[284]
    {0x0F12,	0x1C20},	//TVAR_afit_pBaseVals[285]
    {0x0F12,	0x1C1E},	//TVAR_afit_pBaseVals[286]
    {0x0F12,	0x0028},	//TVAR_afit_pBaseVals[287]
    {0x0F12,	0x030A},	//TVAR_afit_pBaseVals[288]
    {0x0F12,	0x0A0A},	//TVAR_afit_pBaseVals[289]
    {0x0F12,	0x0A1E},	//TVAR_afit_pBaseVals[290]
    {0x0F12,	0xFF07},	//TVAR_afit_pBaseVals[291]
    {0x0F12,	0x0432},	//TVAR_afit_pBaseVals[292]
    {0x0F12,	0x4050},	//TVAR_afit_pBaseVals[293]
    {0x0F12,	0x0F0F},	//TVAR_afit_pBaseVals[294]
    {0x0F12,	0x0440},	//TVAR_afit_pBaseVals[295]
    {0x0F12,	0x0302},	//TVAR_afit_pBaseVals[296]
    {0x0F12,	0x3232},	//TVAR_afit_pBaseVals[297]
    {0x0F12,	0x0101},	//TVAR_afit_pBaseVals[298]
    {0x0F12,	0x2001},	//TVAR_afit_pBaseVals[299]
    {0x0F12,	0x1E1C},	//TVAR_afit_pBaseVals[300]
    {0x0F12,	0x281C},	//TVAR_afit_pBaseVals[301]
    {0x0F12,	0x0A00},	//TVAR_afit_pBaseVals[302]
    {0x0F12,	0x0A03},	//TVAR_afit_pBaseVals[303]
    {0x0F12,	0x1E0A},	//TVAR_afit_pBaseVals[304]
    {0x0F12,	0x070A},	//TVAR_afit_pBaseVals[305]
    {0x0F12,	0x32FF},	//TVAR_afit_pBaseVals[306]
    {0x0F12,	0x5004},	//TVAR_afit_pBaseVals[307]
    {0x0F12,	0x0F40},	//TVAR_afit_pBaseVals[308]
    {0x0F12,	0x400F},	//TVAR_afit_pBaseVals[309]
    {0x0F12,	0x0204},	//TVAR_afit_pBaseVals[310]
    {0x0F12,	0x0003},	//TVAR_afit_pBaseVals[311]
    {0x0F12,	0x0000},	//TVAR_afit_pBaseVals[312]
    {0x0F12,	0x0000},	//TVAR_afit_pBaseVals[313]
    {0x0F12,	0x000f},	//TVAR_afit_pBaseVals[314]//DTS2012071201781:modify by huyouhua at 2012-7-23                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   
    {0x0F12,	0x0000},	//TVAR_afit_pBaseVals[315]
    {0x0F12,	0x0000},	//TVAR_afit_pBaseVals[316]
    {0x0F12,	0x001E},	//TVAR_afit_pBaseVals[317]
    {0x0F12,	0x03FF},	//TVAR_afit_pBaseVals[318]
    {0x0F12,	0x009C},	//TVAR_afit_pBaseVals[319]
    {0x0F12,	0x017C},	//TVAR_afit_pBaseVals[320]
    {0x0F12,	0x03FF},	//TVAR_afit_pBaseVals[321]
    {0x0F12,	0x000C},	//TVAR_afit_pBaseVals[322]
    {0x0F12,	0x0010},	//TVAR_afit_pBaseVals[323]
    {0x0F12,	0x012C},	//TVAR_afit_pBaseVals[324]
    {0x0F12,	0x03E8},	//TVAR_afit_pBaseVals[325]
    {0x0F12,	0x0050},	//TVAR_afit_pBaseVals[326]
    {0x0F12,	0x00C8},	//TVAR_afit_pBaseVals[327]
    {0x0F12,	0x0070},	//TVAR_afit_pBaseVals[328]
    {0x0F12,	0x0004},	//TVAR_afit_pBaseVals[329]
    {0x0F12,	0x0004},	//TVAR_afit_pBaseVals[330]
    {0x0F12,	0x01AA},	//TVAR_afit_pBaseVals[331]
    {0x0F12,	0x0014},	//TVAR_afit_pBaseVals[332]
    {0x0F12,	0x0014},	//TVAR_afit_pBaseVals[333]
    {0x0F12,	0x000A},	//TVAR_afit_pBaseVals[334]
    {0x0F12,	0x000A},	//TVAR_afit_pBaseVals[335]
    {0x0F12,	0x0032},	//TVAR_afit_pBaseVals[336]
    {0x0F12,	0x0023},	//TVAR_afit_pBaseVals[337]
    {0x0F12,	0x0032},	//TVAR_afit_pBaseVals[338]
    {0x0F12,	0x0028},	//TVAR_afit_pBaseVals[339]
    {0x0F12,	0x0032},	//TVAR_afit_pBaseVals[340]
    {0x0F12,	0x0028},	//TVAR_afit_pBaseVals[341]
    {0x0F12,	0x0A0F},	//TVAR_afit_pBaseVals[342]
    {0x0F12,	0x1701},	//TVAR_afit_pBaseVals[343]
    {0x0F12,	0x0229},	//TVAR_afit_pBaseVals[344]
    {0x0F12,	0x1403},	//TVAR_afit_pBaseVals[345]
    {0x0F12,	0x0000},	//TVAR_afit_pBaseVals[346]
    {0x0F12,	0x0000},	//TVAR_afit_pBaseVals[347]
    {0x0F12,	0x0404},	//TVAR_afit_pBaseVals[348]
    {0x0F12,	0x00FF},	//TVAR_afit_pBaseVals[349]
    {0x0F12,	0x033B},	//TVAR_afit_pBaseVals[350]
    {0x0F12,	0x0505},	//TVAR_afit_pBaseVals[351]
    {0x0F12,	0x0301},	//TVAR_afit_pBaseVals[352]
    {0x0F12,	0x8007},	//TVAR_afit_pBaseVals[353]
    {0x0F12,	0x051E},	//TVAR_afit_pBaseVals[354]
    {0x0F12,	0x0A1E},	//TVAR_afit_pBaseVals[355]
    {0x0F12,	0x0F0F},	//TVAR_afit_pBaseVals[356]
    {0x0F12,	0x0A03},	//TVAR_afit_pBaseVals[357]
    {0x0F12,	0x0A3C},	//TVAR_afit_pBaseVals[358]
    {0x0F12,	0x0828},	//TVAR_afit_pBaseVals[359]
    {0x0F12,	0x0002},	//TVAR_afit_pBaseVals[360]
    {0x0F12,	0x00FF},	//TVAR_afit_pBaseVals[361]
    {0x0F12,	0x1002},	//TVAR_afit_pBaseVals[362]
    {0x0F12,	0x001D},	//TVAR_afit_pBaseVals[363]
    {0x0F12,	0x0900},	//TVAR_afit_pBaseVals[364]
    {0x0F12,	0x0600},	//TVAR_afit_pBaseVals[365]
    {0x0F12,	0x0504},	//TVAR_afit_pBaseVals[366]
    {0x0F12,	0x0305},	//TVAR_afit_pBaseVals[367]
    {0x0F12,	0x6F02},	//TVAR_afit_pBaseVals[368]
    {0x0F12,	0x0080},	//TVAR_afit_pBaseVals[369]
    {0x0F12,	0x0080},	//TVAR_afit_pBaseVals[370]
    {0x0F12,	0x0080},	//TVAR_afit_pBaseVals[371]
    {0x0F12,	0x323C},	//TVAR_afit_pBaseVals[372]
    {0x0F12,	0x0101},	//TVAR_afit_pBaseVals[373]
    {0x0F12,	0x1A01},	//TVAR_afit_pBaseVals[374]
    {0x0F12,	0x1A1E},	//TVAR_afit_pBaseVals[375]
    {0x0F12,	0x2818},	//TVAR_afit_pBaseVals[376]
    {0x0F12,	0x0A00},	//TVAR_afit_pBaseVals[377]
    {0x0F12,	0x1403},	//TVAR_afit_pBaseVals[378]
    {0x0F12,	0x1905},	//TVAR_afit_pBaseVals[379]
    {0x0F12,	0x060E},	//TVAR_afit_pBaseVals[380]
    {0x0F12,	0x32FF},	//TVAR_afit_pBaseVals[381]
    {0x0F12,	0x5004},	//TVAR_afit_pBaseVals[382]
    {0x0F12,	0x1440},	//TVAR_afit_pBaseVals[383]
    {0x0F12,	0x4015},	//TVAR_afit_pBaseVals[384]
    {0x0F12,	0x0204},	//TVAR_afit_pBaseVals[385]
    {0x0F12,	0x3C03},	//TVAR_afit_pBaseVals[386]
    {0x0F12,	0x013C},	//TVAR_afit_pBaseVals[387]
    {0x0F12,	0x0101},	//TVAR_afit_pBaseVals[388]
    {0x0F12,	0x141A},	//TVAR_afit_pBaseVals[389]
    {0x0F12,	0x181A},	//TVAR_afit_pBaseVals[390]
    {0x0F12,	0x0028},	//TVAR_afit_pBaseVals[391]
    {0x0F12,	0x030A},	//TVAR_afit_pBaseVals[392]
    {0x0F12,	0x0614},	//TVAR_afit_pBaseVals[393]
    {0x0F12,	0x0A19},	//TVAR_afit_pBaseVals[394]
    {0x0F12,	0xFF06},	//TVAR_afit_pBaseVals[395]
    {0x0F12,	0x0432},	//TVAR_afit_pBaseVals[396]
    {0x0F12,	0x4050},	//TVAR_afit_pBaseVals[397]
    {0x0F12,	0x1514},	//TVAR_afit_pBaseVals[398]
    {0x0F12,	0x0440},	//TVAR_afit_pBaseVals[399]
    {0x0F12,	0x0302},	//TVAR_afit_pBaseVals[400]
    {0x0F12,	0x3C3C},	//TVAR_afit_pBaseVals[401]
    {0x0F12,	0x0101},	//TVAR_afit_pBaseVals[402]
    {0x0F12,	0x1A01},	//TVAR_afit_pBaseVals[403]
    {0x0F12,	0x1A14},	//TVAR_afit_pBaseVals[404]
    {0x0F12,	0x2818},	//TVAR_afit_pBaseVals[405]
    {0x0F12,	0x0A00},	//TVAR_afit_pBaseVals[406]
    {0x0F12,	0x1403},	//TVAR_afit_pBaseVals[407]
    {0x0F12,	0x1906},	//TVAR_afit_pBaseVals[408]
    {0x0F12,	0x060A},	//TVAR_afit_pBaseVals[409]
    {0x0F12,	0x32FF},	//TVAR_afit_pBaseVals[410]
    {0x0F12,	0x5004},	//TVAR_afit_pBaseVals[411]
    {0x0F12,	0x1440},	//TVAR_afit_pBaseVals[412]
    {0x0F12,	0x4015},	//TVAR_afit_pBaseVals[413]
    {0x0F12,	0x0204},	//TVAR_afit_pBaseVals[414]
    {0x0F12,	0x0003},	//TVAR_afit_pBaseVals[415]
    {0x0F12,	0x0000},	//TVAR_afit_pBaseVals[416]
    {0x0F12,	0x0000},	//TVAR_afit_pBaseVals[417]
    {0x0F12,	0x000f},	//TVAR_afit_pBaseVals[418]//DTS2012071201781:modify by huyouhua at 2012-7-23                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   
    {0x0F12,	0x0000},	//TVAR_afit_pBaseVals[419]
    {0x0F12,	0x0000},	//TVAR_afit_pBaseVals[420]
    {0x0F12,	0x001E},	//TVAR_afit_pBaseVals[421]
    {0x0F12,	0x03FF},	//TVAR_afit_pBaseVals[422]
    {0x0F12,	0x009C},	//TVAR_afit_pBaseVals[423]
    {0x0F12,	0x017C},	//TVAR_afit_pBaseVals[424]
    {0x0F12,	0x03FF},	//TVAR_afit_pBaseVals[425]
    {0x0F12,	0x000C},	//TVAR_afit_pBaseVals[426]
    {0x0F12,	0x0010},	//TVAR_afit_pBaseVals[427]
    {0x0F12,	0x0000},	//TVAR_afit_pBaseVals[428]
    {0x0F12,	0x0000},	//TVAR_afit_pBaseVals[429]
    {0x0F12,	0x0046},	//TVAR_afit_pBaseVals[430]
    {0x0F12,	0x0050},	//TVAR_afit_pBaseVals[431]
    {0x0F12,	0x0070},	//TVAR_afit_pBaseVals[432]
    {0x0F12,	0x0004},	//TVAR_afit_pBaseVals[433]
    {0x0F12,	0x0004},	//TVAR_afit_pBaseVals[434]
    {0x0F12,	0x01AA},	//TVAR_afit_pBaseVals[435]
    {0x0F12,	0x0014},	//TVAR_afit_pBaseVals[436]
    {0x0F12,	0x0014},	//TVAR_afit_pBaseVals[437]
    {0x0F12,	0x000A},	//TVAR_afit_pBaseVals[438]
    {0x0F12,	0x000A},	//TVAR_afit_pBaseVals[439]
    {0x0F12,	0x002D},	//TVAR_afit_pBaseVals[440]
    {0x0F12,	0x0019},	//TVAR_afit_pBaseVals[441]
    {0x0F12,	0x0023},	//TVAR_afit_pBaseVals[442]
    {0x0F12,	0x0023},	//TVAR_afit_pBaseVals[443]
    {0x0F12,	0x0023},	//TVAR_afit_pBaseVals[444]
    {0x0F12,	0x0023},	//TVAR_afit_pBaseVals[445]
    {0x0F12,	0x0A0F},	//TVAR_afit_pBaseVals[446]
    {0x0F12,	0x1701},	//TVAR_afit_pBaseVals[447]
    {0x0F12,	0x0229},	//TVAR_afit_pBaseVals[448]
    {0x0F12,	0x1403},	//TVAR_afit_pBaseVals[449]
    {0x0F12,	0x0000},	//TVAR_afit_pBaseVals[450]
    {0x0F12,	0x0000},	//TVAR_afit_pBaseVals[451]
    {0x0F12,	0x0606},	//TVAR_afit_pBaseVals[452]
    {0x0F12,	0x00FF},	//TVAR_afit_pBaseVals[453]
    {0x0F12,	0x033B},	//TVAR_afit_pBaseVals[454]
    {0x0F12,	0x0505},	//TVAR_afit_pBaseVals[455]
    {0x0F12,	0x0301},	//TVAR_afit_pBaseVals[456]
    {0x0F12,	0x8007},	//TVAR_afit_pBaseVals[457]
    {0x0F12,	0x051E},	//TVAR_afit_pBaseVals[458]
    {0x0F12,	0x0A1E},	//TVAR_afit_pBaseVals[459]
    {0x0F12,	0x0000},	//TVAR_afit_pBaseVals[460]
    {0x0F12,	0x0A03},	//TVAR_afit_pBaseVals[461]
    {0x0F12,	0x1E3C},	//TVAR_afit_pBaseVals[462]
    {0x0F12,	0x1028},	//TVAR_afit_pBaseVals[463]
    {0x0F12,	0x0002},	//TVAR_afit_pBaseVals[464]
    {0x0F12,	0x00FF},	//TVAR_afit_pBaseVals[465]
    {0x0F12,	0x1002},	//TVAR_afit_pBaseVals[466]
    {0x0F12,	0x001E},	//TVAR_afit_pBaseVals[467]
    {0x0F12,	0x0900},	//TVAR_afit_pBaseVals[468]
    {0x0F12,	0x0600},	//TVAR_afit_pBaseVals[469]
    {0x0F12,	0x0504},	//TVAR_afit_pBaseVals[470]
    {0x0F12,	0x0305},	//TVAR_afit_pBaseVals[471]
    {0x0F12,	0x8002},	//TVAR_afit_pBaseVals[472]
    {0x0F12,	0x0080},	//TVAR_afit_pBaseVals[473]
    {0x0F12,	0x0080},	//TVAR_afit_pBaseVals[474]
    {0x0F12,	0x0080},	//TVAR_afit_pBaseVals[475]
    {0x0F12,	0x4646},	//TVAR_afit_pBaseVals[476]
    {0x0F12,	0x0101},	//TVAR_afit_pBaseVals[477]
    {0x0F12,	0x1801},	//TVAR_afit_pBaseVals[478]
    {0x0F12,	0x141C},	//TVAR_afit_pBaseVals[479]
    {0x0F12,	0x2812},	//TVAR_afit_pBaseVals[480]
    {0x0F12,	0x0A00},	//TVAR_afit_pBaseVals[481]
    {0x0F12,	0x1003},	//TVAR_afit_pBaseVals[482]
    {0x0F12,	0x1405},	//TVAR_afit_pBaseVals[483]
    {0x0F12,	0x050C},	//TVAR_afit_pBaseVals[484]
    {0x0F12,	0x32FF},	//TVAR_afit_pBaseVals[485]
    {0x0F12,	0x5204},	//TVAR_afit_pBaseVals[486]
    {0x0F12,	0x1440},	//TVAR_afit_pBaseVals[487]
    {0x0F12,	0x4015},	//TVAR_afit_pBaseVals[488]
    {0x0F12,	0x0204},	//TVAR_afit_pBaseVals[489]
    {0x0F12,	0x5003},	//TVAR_afit_pBaseVals[490]
    {0x0F12,	0x0150},	//TVAR_afit_pBaseVals[491]
    {0x0F12,	0x0101},	//TVAR_afit_pBaseVals[492]
    {0x0F12,	0x1418},	//TVAR_afit_pBaseVals[493]
    {0x0F12,	0x1214},	//TVAR_afit_pBaseVals[494]
    {0x0F12,	0x0028},	//TVAR_afit_pBaseVals[495]
    {0x0F12,	0x030A},	//TVAR_afit_pBaseVals[496]
    {0x0F12,	0x0A10},	//TVAR_afit_pBaseVals[497]
    {0x0F12,	0x0819},	//TVAR_afit_pBaseVals[498]
    {0x0F12,	0xFF05},	//TVAR_afit_pBaseVals[499]
    {0x0F12,	0x0432},	//TVAR_afit_pBaseVals[500]
    {0x0F12,	0x4052},	//TVAR_afit_pBaseVals[501]
    {0x0F12,	0x1514},	//TVAR_afit_pBaseVals[502]
    {0x0F12,	0x0440},	//TVAR_afit_pBaseVals[503]
    {0x0F12,	0x0302},	//TVAR_afit_pBaseVals[504]
    {0x0F12,	0x5050},	//TVAR_afit_pBaseVals[505]
    {0x0F12,	0x0101},	//TVAR_afit_pBaseVals[506]
    {0x0F12,	0x1801},	//TVAR_afit_pBaseVals[507]
    {0x0F12,	0x1414},	//TVAR_afit_pBaseVals[508]
    {0x0F12,	0x2812},	//TVAR_afit_pBaseVals[509]
    {0x0F12,	0x0A00},	//TVAR_afit_pBaseVals[510]
    {0x0F12,	0x1003},	//TVAR_afit_pBaseVals[511]
    {0x0F12,	0x190A},	//TVAR_afit_pBaseVals[512]
    {0x0F12,	0x0508},	//TVAR_afit_pBaseVals[513]
    {0x0F12,	0x32FF},	//TVAR_afit_pBaseVals[514]
    {0x0F12,	0x5204},	//TVAR_afit_pBaseVals[515]
    {0x0F12,	0x1440},	//TVAR_afit_pBaseVals[516]
    {0x0F12,	0x4015},	//TVAR_afit_pBaseVals[517]
    {0x0F12,	0x0204},	//TVAR_afit_pBaseVals[518]
    {0x0F12,	0x0003},	//TVAR_afit_pBaseVals[519]
    //	param_end	TVAR_afit_pBaseVals
    //	param_start	afit_pConstBaseVals 
    {0x002A,    0x0BD4},
    {0x0F12,	0x7F7A},	//afit_pConstBaseVals[0]
    {0x0F12,	0x779D},	//afit_pConstBaseVals[1]
    {0x0F12,	0xBE7E},	//afit_pConstBaseVals[2]
    {0x0F12,	0xF7BC},	//afit_pConstBaseVals[3]
    {0x0F12,	0x7E06},	//afit_pConstBaseVals[4]
    {0x0F12,	0x0053},	//afit_pConstBaseVals[5]
    //	param_end	afit_pConstBaseVals

    // Fill RAM with alternative op-codes
    {0x0028,	0x7000},	// start add MSW
    {0x002A,	0x2CE8},	// start add LSW
    {0x0F12,	0x0007},	// Modify LSB to control AWBB_YThreshLow
    {0x0F12,	0x00e2},	//
    {0x0F12,	0x0005},	// Modify LSB to control AWBB_YThreshLowBrLow
    {0x0F12,	0x00E2},	//
    //////////////////////////////////////////////////////////////////////////
    //============================================================         
    //END Tuning part                                                          
	//END: DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
    
	//================================================================================================
	//SET PLL      
	//================================================================================================
	//How to set   
	//1. MCLK      
	//hex(CLK you want) * 1000)
	//2. System CLK
	//hex((CLK you want) * 1000 / 4)
	//3. PCLK      
	//hex((CLK you want) * 1000 / 4)
	//================================================================================================
	//Set input CLK //24MHz
	{0x002A, 0x01CC},
	{0x0F12, 0x5DC0},  //5FB4 //5DC0=24MCLK    #REG_TC_IPRM_InClockLSBs
	{0x0F12, 0x0000}, //#REG_TC_IPRM_InClockMSBs
	{0x002A, 0x01EE},
	{0x0F12, 0x0002}, //#REG_TC_IPRM_UseNPviClocks //Number of PLL setting
	//Set system CLK //40MHz
	{0x002A, 0x01F6},
	{0x0F12, 0x38a4 },          //2904           //2BF2 //#REG_TC_IPRM_OpClk4KHz_0
	{0x0F12, 0x37A4}, //3A88 //#REG_TC_IPRM_MinOutRate4KHz_0
	{0x0F12, 0x39A4}, //3AA8 //#REG_TC_IPRM_MaxOutRate4KHz_0
	{0x0F12, 0x38A4},          //2904           //2BF2 //#REG_TC_IPRM_OpClk4KHz_1
	//Set pixel CLK //60MHz (0x3A98)
	{0x0F12, 0x2EA0}, //#REG_TC_IPRM_MinOutRate4KHz_1
	{0x0F12, 0x2EE0}, //#REG_TC_IPRM_MaxOutRate4KHz_1
	//Update PLL   
	{0x002A, 0x0208},
	{0x0F12, 0x0001}, //#REG_TC_IPRM_InitParamsUpdated
	
	{SEQUENCE_WAIT_MS,100},//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	//================================================================================================
	//SET PREVIEW CONFIGURATION_0
	//# Foramt : YUV422
	//# Size: 640x480
	//# FPS : 25fps
	//================================================================================================
	{0x002A, 0x026C},
	{0x0F12, 0x0280}, //0400 //#REG_0TC_PCFG_usWidth//1024
	{0x0F12, 0x01E0}, //0300 //#REG_0TC_PCFG_usHeight //768    026E
	{0x0F12, 0x0005}, //#REG_0TC_PCFG_Format            0270
	{0x0F12, 0x39a4}, //3AA8 //#REG_0TC_PCFG_usMaxOut4KHzRate  0272
	{0x0F12, 0x37a4}, //3A88 //#REG_0TC_PCFG_usMinOut4KHzRate  0274
	{0x0F12, 0x0100}, //#REG_0TC_PCFG_OutClkPerPix88    0276
	{0x0F12, 0x0800}, //#REG_0TC_PCFG_uMaxBpp88         027
	{0x0F12, 0x0052}, //#REG_0TC_PCFG_PVIMask //s0050 = FALSE in MSM6290 : s0052 = TRUE in MSM6800 //reg 027A
	{0x0F12, 0x4000}, //#REG_0TC_PCFG_OIFMask
	{0x0F12, 0x0400}, //0x01E0},//#REG_0TC_PCFG_usJpegPacketSize //DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	{0x0F12, 0x0000}, //#REG_0TC_PCFG_usJpegTotalPackets
	{0x0F12, 0x0000}, //#REG_0TC_PCFG_uClockInd
	{0x0F12, 0x0000}, //#REG_0TC_PCFG_usFrTimeType
	{0x0F12, 0x0001}, //#REG_0TC_PCFG_FrRateQualityType
	{0x0F12, 0x0190}, //03E8 #REG_0TC_PCFG_usMaxFrTimeMsecMult10  min 25fps
	{0x0F12, 0x0190}, //029a #REG_0TC_PCFG_usMinFrTimeMsecMult10  max 25fps
	{0x0F12, 0x0000}, //#REG_0TC_PCFG_bSmearOutput
	{0x0F12, 0x0000}, //#REG_0TC_PCFG_sSaturation
	{0x0F12, 0x0000}, //#REG_0TC_PCFG_sSharpBlur
	{0x0F12, 0x0000}, //#REG_0TC_PCFG_sColorTemp
	{0x0F12, 0x0000}, //#REG_0TC_PCFG_uDeviceGammaIndex
	{0x0F12, 0x0003}, //#REG_0TC_PCFG_uPrevMirror
	{0x0F12, 0x0003}, //#REG_0TC_PCFG_uCaptureMirror
	{0x0F12, 0x0000}, //#REG_0TC_PCFG_uRotation
	//================================================================================================
	//SET PREVIEW CONFIGURATION_1
	//# Foramt : YUV422
	//# Size: 1280x720
	//# FPS : 15~15fps
	//================================================================================================
	{0x002A, 0x029C},
	{0x0F12, 0x0500}, //0400 //#REG_0TC_PCFG_usWidth//1024
	{0x0F12, 0x02D0}, //0300 //#REG_0TC_PCFG_usHeight //768    026E
	{0x0F12, 0x0005}, //#REG_0TC_PCFG_Format            0270
	{0x0F12, 0x2ee0}, //3AA8 //#REG_0TC_PCFG_usMaxOut4KHzRate  0272
	{0x0F12, 0x2ea0}, //3A88 //#REG_0TC_PCFG_usMinOut4KHzRate  0274
	{0x0F12, 0x0100}, //#REG_0TC_PCFG_OutClkPerPix88    0276
	{0x0F12, 0x0800}, //#REG_0TC_PCFG_uMaxBpp88         027
	{0x0F12, 0x0052}, //#REG_0TC_PCFG_PVIMask //s0050 = FALSE in MSM6290 : s0052 = TRUE in MSM6800 //reg 027A
	{0x0F12, 0x4000}, //#REG_0TC_PCFG_OIFMask
	{0x0F12, 0x0400}, //0x01E0}, //#REG_0TC_PCFG_usJpegPacketSize//DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	{0x0F12, 0x0000}, //#REG_0TC_PCFG_usJpegTotalPackets
	{0x0F12, 0x0001}, //#REG_0TC_PCFG_uClockInd
	{0x0F12, 0x0000}, //#REG_0TC_PCFG_usFrTimeType
	{0x0F12, 0x0002}, //1 //#REG_0TC_PCFG_FrRateQualityType
	{0x0F12, 0x029a}, //03E8 #REG_0TC_PCFG_usMaxFrTimeMsecMult10 //15fps
	{0x0F12, 0x029a}, //029a #REG_0TC_PCFG_usMinFrTimeMsecMult10 //15fps
	{0x0F12, 0x0000}, //#REG_0TC_PCFG_bSmearOutput
	{0x0F12, 0x0000}, //#REG_0TC_PCFG_sSaturation
	{0x0F12, 0x0000}, //#REG_0TC_PCFG_sSharpBlur
	{0x0F12, 0x0000}, //#REG_0TC_PCFG_sColorTemp
	{0x0F12, 0x0000}, //#REG_0TC_PCFG_uDeviceGammaIndex
	{0x0F12, 0x0003}, //#REG_0TC_PCFG_uPrevMirror
	{0x0F12, 0x0003}, //#REG_0TC_PCFG_uCaptureMirror
	{0x0F12, 0x0000}, //#REG_0TC_PCFG_uRotation
	//================================================================================================
	//APPLY PREVIEW CONFIGURATION & RUN PREVIEW
	//================================================================================================
	{0x002A, 0x023C},
	{0x0F12, 0x0000}, //#REG_TC_GP_ActivePrevConfig //Select preview configuration_0
	{0x002A, 0x0240},
	{0x0F12, 0x0001}, //#REG_TC_GP_PrevOpenAfterChange
	{0x002A, 0x0230},
	{0x0F12, 0x0001}, //#REG_TC_GP_NewConfigSync //Update preview configuration
	{0x002A, 0x023E},
	{0x0F12, 0x0001}, //#REG_TC_GP_PrevConfigChanged
	{0x002A, 0x0220},
	{0x0F12, 0x0001}, //#REG_TC_GP_EnablePreview //Start preview
	{0x0F12, 0x0001}, //#REG_TC_GP_EnablePreviewChanged
	//================================================================================================
	//SET CAPTURE CONFIGURATION_0
	//# Foramt :YUV
	//# Size: QXGA 
	//# FPS : 5 ~ 7.5fps
	//================================================================================================
	{0x002A, 0x035C},
	{0x0F12, 0x0000}, //#REG_0TC_CCFG_uCaptureModeJpEG
	{0x0F12, 0x0800}, //#REG_0TC_CCFG_usWidth
	{0x0F12, 0x0600}, //#REG_0TC_CCFG_usHeight
	{0x0F12, 0x0005}, //#REG_0TC_CCFG_Format//5:YUV9:JPEG
	//{0x0F12, 0x39ae}, //3AA8 //#REG_0TC_CCFG_usMaxOut4KHzRate
    {0x0F12, 0x39a4}, //#REG_0TC_CCFG_usMaxOut4KHzRate//DTS2012071201781:modify by huyouhua at 2012-7-23
	{0x0F12, 0x37a4}, //3A88 //#REG_0TC_CCFG_usMinOut4KHzRate
	{0x0F12, 0x0100}, //#REG_0TC_CCFG_OutClkPerPix88
	{0x0F12, 0x0800}, //#REG_0TC_CCFG_uMaxBpp88
	{0x0F12, 0x0052}, //#REG_0TC_CCFG_PVIMask
	{0x0F12, 0x0050}, //#REG_0TC_CCFG_OIFMask   edison
	//BEGIN: DTS2012071201781 modify by huyouhua at 2012-7-23
    //{0x0F12, 0x01E0}, //#REG_0TC_CCFG_usJpegPacketSize
	//{0x0F12, 0x08fc}, //#REG_0TC_CCFG_usJpegTotalPackets
    {0x0F12, 0x03C0}, //#REG_0TC_CCFG_usJpegPacketSize
    //END: DTS2012071201781 modify by huyouhua at 2012-7-23
	{0x0F12, 0x0000}, //#REG_0TC_CCFG_uClockInd
	{0x0F12, 0x0000}, //#REG_0TC_CCFG_usFrTimeType
	{0x0F12, 0x0000}, //#REG_0TC_CCFG_FrRateQualityType
    {0x0F12, 0x0002}, //#REG_0TC_CCFG_FrRateQualityType//DTS2012071201781:add by huyouhua at 2012-7-23
	{0x0F12, 0x07D0}, //#REG_0TC_CCFG_usMaxFrTimeMsecMult10 //5fps
	{0x0F12, 0x0535}, //#REG_0TC_CCFG_usMinFrTimeMsecMult10 //7.5fps
	{0x0F12, 0x0000}, //#REG_0TC_CCFG_bSmearOutput
	{0x0F12, 0x0000}, //#REG_0TC_CCFG_sSaturation
	{0x0F12, 0x0000}, //#REG_0TC_CCFG_sSharpBlur
	{0x0F12, 0x0000}, //#REG_0TC_CCFG_sColorTemp
	{0x0F12, 0x0000}, //#REG_0TC_CCFG_uDeviceGammaIndex
	//================================================================================================
	//SET CAPTURE CONFIGURATION_1
	//# Foramt :   
	//# Size:      
	//# FPS :      
	//================================================================================================
	//Not used     
	{0x0028, 0xD000},
	{0x002A, 0x1000},
	{0x0F12, 0x0001},
	
	{SEQUENCE_WAIT_MS,50},
	{SEQUENCE_END, 0x00}
};

static struct reginfo sensor_720p[]=
{
	{0x0028,  0x7000},
	{0x002A,  0x01D6},
	{0x0F12,  0x0800},
	{0x0F12,  0x0600},
	{0x0F12,  0x0000},
	{0x0F12,  0x00C0},
	{0x002A,  0x0208},
	{0x0F12,  0x0001},

	{0x002A,  0x023C},
	{0x0F12,  0x0001},
	{0x002A,  0x0240},
	{0x0F12,  0x0001},
	{0x002A,  0x0230},
	{0x0F12,  0x0001},
	{0x002A,  0x023E},
	{0x0F12,  0x0001},
	{0x002A,  0x0220},
	{0x0F12,  0x0001},
	{0x0F12,  0x0001},

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
	{0x0028 ,0x7000},
    {0x002a ,0x0244},//#REG_TC_GP_ActiveCapConfig  num                                   
	{0x0f12 ,0x0000},                                   
	{0x0f12 ,0x0001},//#REG_TC_GP_CapConfigChanged                                    
	{0x002a ,0x0230},                                   
	{0x0f12 ,0x0001},//#REG_TC_GP_NewConfigSync                                   
	{0x002a ,0x0224},                                   
	{0x0f12 ,0x0001},//#REG_TC_GP_EnableCapture                                    
	{0x0f12 ,0x0001},//#REG_TC_GP_EnableCaptureChanged
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
	{0x0028,  0x7000},
	{0x002A,  0x01D6},
	{0x0F12,  0x0140},
	{0x0F12,  0x00F0},
	{0x0F12,  0x0000},
	{0x0F12,  0x0000},
	{0x002A,  0x0208},
	{0x0F12,  0x0001},

	{0x002A,  0x023C},
	{0x0F12,  0x0000},
	{0x002A,  0x0240},
	{0x0F12,  0x0001},
	{0x002A,  0x0230},
	{0x0F12,  0x0001},
	{0x002A,  0x023E},
	{0x0F12,  0x0001},
	{0x002A,  0x0220},
	{0x0F12,  0x0001},
	{0x0F12,  0x0001},

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
	{0x0028, 0x7000},
	{0x002A, 0X04D2},
    //BEGIN: DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	//{0x0F12, 0x065F},
	{0x0F12, 0x067F},
    //END: DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
    {SEQUENCE_END, 0x00}
};

//incandescent
static  struct reginfo sensor_WhiteB_TungstenLamp1[]=
{
	{0x0028, 0x7000},
	{0x002A, 0x04D2},
    //BEGIN: DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
    //{0x0F12, 0x0657},
    {0x0F12, 0x0677},
    //END: DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	{0x002A, 0x04A0},
	{0x0F12, 0x0380},
	{0x0F12, 0x0001},
	{0x0F12, 0x0400},
	{0x0F12, 0x0001},
	{0x0F12, 0x09C0},
	{0x0F12, 0x0001},

	{SEQUENCE_END, 0x00}
};

//fluorescent荧光2
static  struct reginfo sensor_WhiteB_TungstenLamp2[]=
{
	{0x0028, 0x7000},
	{0x002A, 0x04D2},
	{0x0F12, 0x0657},
	{0x002A, 0x04A0},
	{0x0F12, 0x0400},
	{0x0F12, 0x0001},
	{0x0F12, 0x0400},
	{0x0F12, 0x0001},
	{0x0F12, 0x083C},
	{0x0F12, 0x0001},

	{SEQUENCE_END, 0x00}
};

//daylight日光3
static  struct reginfo sensor_WhiteB_ClearDay[]=
{
	{0x0028, 0x7000},
	{0x002A, 0x04D2},
    //BEGIN: DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	//{0x0F12, 0x0657},
	{0x0F12, 0x0677},
    //END: DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	{0x002A, 0x04A0},
	{0x0F12, 0x05A0},
	{0x0F12, 0x0001},
	{0x0F12, 0x0400},
	{0x0F12, 0x0001},
	{0x0F12, 0x05F0},
	{0x0F12, 0x0001},

	{SEQUENCE_END, 0x00}
};

//阴天4
static  struct reginfo sensor_WhiteB_Cloudy[]=
{
	{0x0028, 0x7000},
	{0x002A, 0x04D2},
    //BEGIN: DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	//{0x0F12, 0x0657},
	{0x0F12, 0x0677},
    //END: DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
	{0x002A, 0x04A0},
	{0x0F12, 0x0540},
	{0x0F12, 0x0001},
	{0x0F12, 0x0400},
	{0x0F12, 0x0001},
	{0x0F12, 0x0500},
	{0x0F12, 0x0001},

    {SEQUENCE_END, 0x00}
};

static struct reginfo *sensor_WhiteBalanceSeqe[] = {sensor_WhiteB_Auto, sensor_WhiteB_TungstenLamp1,sensor_WhiteB_TungstenLamp2,
    sensor_WhiteB_ClearDay, sensor_WhiteB_Cloudy, NULL,};
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
    {0x0028, 0x7000},
    {0x002A, 0x021E},
    {0x0F12, 0x0000},
    {SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Effect_WandB[] =
{
    {0x0028, 0x7000},
    {0x002A, 0x021E},
    {0x0F12, 0x0001},
    {SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Effect_Sepia[] =
{
    {0x0028, 0x7000},
    {0x002A, 0x021E},
    {0x0F12, 0x0004},
    {SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Effect_Negative[] =
{
    {0x0028, 0x7000},
    {0x002A, 0x021E},
    //BEGIN: DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
    //{0x0F12, 0x0002},
    {0x0F12, 0x0003},
    //END: DTS2012071201781: modify by huyouhua 00136760 at 2012-7-20
    {SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Effect_Bluish[] =
{
    {0x0028, 0x7000},
    {0x002A, 0x021E},
    {0x0F12, 0x0003},
    {SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Effect_Green[] =
{
    {0x0028, 0x7000},
    {0x002A, 0x021E},
    {0x0F12, 0x0005},
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
static  struct reginfo sensor_SceneNormal[] =
{ 
	{0x0028,0x7000},
	{0x002A,0x0288},
	{0x0F12,0X0190},
	{0x0F12,0X014D},
	{0x002A,0x023E},
	{0x0F12,0x0001},

	{SEQUENCE_WAIT_MS,50},	

	{0x002A,0x023C},
	{0x0F12,0x0000},
	{0x002A,0x0240},
	{0x0F12,0x0001},
	{0x002A,0x0230},
	{0x0F12,0x0001},
	{0x002A,0x023E},
	{0x0F12,0x0001},
	{0x002A,0x0220},
	{0x0F12,0x0001},
	{0x0F12,0x0001},

    {SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_SceneNight[] =
{
	{0x0028,0x7000},
	{0x002A,0x0288},
	{0x0F12,0X07D0},
	{0x0F12,0X029A},
	{0x002A,0x023E},
	{0x0F12,0x0001},

	{SEQUENCE_WAIT_MS,50},	

	{0x002A,0x023C},
	{0x0F12,0x0000},
	{0x002A,0x0240},
	{0x0F12,0x0001},
	{0x002A,0x0230},
	{0x0F12,0x0001},
	{0x002A,0x023E},
	{0x0F12,0x0001},
	{0x002A,0x0220},
	{0x0F12,0x0001},
	{0x0F12,0x0001},

	{SEQUENCE_END, 0x00}
};
static struct reginfo *sensor_SceneSeqe[] = {sensor_SceneNormal, sensor_SceneNight, NULL,};
#endif

#if CONFIG_SENSOR_AntiBanding
static  struct reginfo sensor_AntiBanding_50HZ[] =
{
	{0x0028, 0x7000},
	{0x002a, 0x04d2},
	{0x0f12, 0x065f},
	{0x002a, 0x04ba},
	{0x0f12, 0x0001},
	{0x002a, 0x04bc},
	{0x0f12, 0x0001},
    {SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_AntiBanding_60HZ[] =
{
	{0x0028, 0x7000},
	{0x002a, 0x04d2},
	{0x0f12, 0x065f},
	{0x002a, 0x04ba},
	{0x0f12, 0x0002},
    {0x002a, 0x04bc},
	{0x0f12, 0x0001},
    {SEQUENCE_END, 0x00}
};
static struct reginfo *sensor_AntiBandingSeqe[] = {sensor_AntiBanding_50HZ, sensor_AntiBanding_60HZ, NULL,};
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
    { .id = V4L2_CID_DO_WHITE_BALANCE,  .index = 0,  .name = "auto",  .reserved = 0, }, 
	{ .id = V4L2_CID_DO_WHITE_BALANCE,  .index = 1,  .name = "incandescent",  .reserved = 0,},
    { .id = V4L2_CID_DO_WHITE_BALANCE,  .index = 2,  .name = "fluorescent", .reserved = 0,}, 
	{ .id = V4L2_CID_DO_WHITE_BALANCE,  .index = 3,  .name = "daylight", .reserved = 0,},
    { .id = V4L2_CID_DO_WHITE_BALANCE,  .index = 4,  .name = "cloudy-daylight", .reserved = 0,},
    #endif

    #if CONFIG_SENSOR_Effect
    { .id = V4L2_CID_EFFECT,  .index = 0,  .name = "none",      .reserved = 0,}, 
    {  .id = V4L2_CID_EFFECT, .index = 1,  .name = "mono",      .reserved = 0,},
    { .id = V4L2_CID_EFFECT,  .index = 2,  .name = "negative",  .reserved = 0,}, 
    {  .id = V4L2_CID_EFFECT, .index = 3,  .name = "sepia",     .reserved = 0,},
    { .id = V4L2_CID_EFFECT,  .index = 4,  .name = "posterize", .reserved = 0,} ,
    //{ .id = V4L2_CID_EFFECT,  .index = 5,  .name = "aqua",      .reserved = 0,},
    #endif

	#if CONFIG_SENSOR_Scene
    { .id = V4L2_CID_SCENE,  .index = 0, .name = "auto",     .reserved = 0,},
    { .id = V4L2_CID_SCENE,  .index = 1, .name = "night",   .reserved = 0,},
    #endif

	#if CONFIG_SENSOR_AntiBanding
    { .id = V4L2_CID_ANTIBANDING,  .index = 0, .name = "50hz",     .reserved = 0,},
	{ .id = V4L2_CID_ANTIBANDING,  .index = 1, .name = "60hz",     .reserved = 0,},
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
        .maximum	= 4,
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

	#if CONFIG_SENSOR_AntiBanding
    {
        .id		= V4L2_CID_ANTIBANDING,
        .type		= V4L2_CTRL_TYPE_MENU,
        .name		= "AntiBanding Control",
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
    int antibanding;
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
			//msleep(val);
			mdelay(val);
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
    #if CONFIG_SENSOR_AntiBanding
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_ANTIBANDING);
	if (qctrl)
        sensor->info_priv.antibanding = qctrl->default_value;
    #endif
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
    #endif
    SENSOR_DG("\n%s..%s.. icd->width = %d.. icd->height %d\n",SENSOR_NAME_STRING(),((val == 0)?__FUNCTION__:"sensor_reinit"),icd->user_width,icd->user_height);
sensor_init_end:    
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
    {SEQUENCE_END,0x00}
};

static struct reginfo sensor_resume_sequence[]=
{
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

        sensor->info_priv.winseqe_cur_addr  = (unsigned int)winseqe_set_addr;

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
    int ret = 0, set_w,set_h;;
   
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
    {              /* ddl@rock-chips.com : Sensor output smallest size if  isn't support app  */
        set_w = SENSOR_INIT_WIDTH;
        set_h = SENSOR_INIT_HEIGHT;		
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
#if CONFIG_SENSOR_AntiBanding
static int sensor_set_antibanding(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
    {
        if (sensor_AntiBandingSeqe[value - qctrl->minimum] != NULL)
        {
            if (sensor_write_array(client, sensor_AntiBandingSeqe[value - qctrl->minimum]) != 0)
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
        #if CONFIG_SENSOR_AntiBanding
        case V4L2_CID_ANTIBANDING:
            {
                ext_ctrl->value = sensor->info_priv.antibanding;
                break;
            }
        #endif
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
#if CONFIG_SENSOR_AntiBanding
        case V4L2_CID_ANTIBANDING:
            {
                if (ext_ctrl->value != sensor->info_priv.antibanding)
                {
                    if (sensor_set_antibanding(icd, qctrl,ext_ctrl->value) != 0)
                        return -EINVAL;
                    sensor->info_priv.antibanding = ext_ctrl->value;
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

	//if senor online
    ret = sensor_write(client, 0x002c, 0x0000);
    if (ret != 0) {
        SENSOR_TR("%s soft reset sensor failed\n",SENSOR_NAME_STRING());
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
                			memset(&icd->ops->controls[i],0x00,sizeof(struct v4l2_queryctrl));                			
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
    struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
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
    //SENSOR_DG("\n%s..%s.. \n",__FUNCTION__,SENSOR_NAME_STRING());
    printk(">>>>>>>>>>\n%s..%s.. \n",__FUNCTION__,SENSOR_NAME_STRING());
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


