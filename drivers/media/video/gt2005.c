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
#define SENSOR_NAME RK29_CAM_SENSOR_GT2005
#define SENSOR_V4L2_IDENT  V4L2_IDENT_GT2005
#define SENSOR_ID 0x5138
#define SENSOR_MIN_WIDTH    640
#define SENSOR_MIN_HEIGHT   480
#define SENSOR_MAX_WIDTH    1600
#define SENSOR_MAX_HEIGHT   1200
#define SENSOR_INIT_WIDTH	640			/* Sensor pixel size for sensor_init_data array */
#define SENSOR_INIT_HEIGHT  480
#define SENSOR_INIT_WINSEQADR sensor_vga
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

struct reginfo
{
    u16 reg;
    u8 val;
};

/* init 352X288 SVGA */
static struct reginfo sensor_init_data[] =
{
    {0x0101 , 0x00},
	{0x0103 , 0x00}, 
	{0x0105 , 0x00},
	{0x0106 , 0xF0},
	{0x0107 , 0x00},
	{0x0108 , 0x1C},
	{0x0109 , 0x01},
	{0x010A , 0x00},
	{0x010B , 0x00},
	{0x010C , 0x00},
	{0x010D , 0x08},
	{0x010E , 0x00},
	{0x010F , 0x08},
	{0x0110 , 0x06},
	{0x0111 , 0x40},
	{0x0112 , 0x04},
	{0x0113 , 0xB0},
{0x0114 , 0x00},
	{0x0115 , 0x00},
	{0x0116 , 0x02},
	{0x0117 , 0x00},
{0x0118 , 0x67},
	{0x0119 , 0x02},
	{0x011A , 0x04},
	{0x011B , 0x01},
{0x011C , 0x01},
{0x011D , 0x02},
{0x011E , 0x00},
{0x011F , 0x00},
{0x0120 , 0x1C},
{0x0121 , 0x00},
{0x0122 , 0x04},
{0x0123 , 0x00},
{0x0124 , 0x00},
{0x0125 , 0x00},
{0x0126 , 0x00},
{0x0127 , 0x00},
{0x0128 , 0x00},
{0x0200 , 0x00},
{0x0201 , 0x00},
{0x0202 , 0x40},
{0x0203 , 0x00},
{0x0204 , 0x03},
{0x0205 , 0x1F},
{0x0206 , 0x0B},
{0x0207 , 0x20},
{0x0208 , 0x00},
{0x0209 , 0x2A},
{0x020A , 0x01},
{0x020B , 0x48},
{0x020C , 0x64},
{0x020D , 0xC8},
{0x020E , 0xBC},
{0x020F , 0x08},
{0x0210 , 0xD6},
{0x0211 , 0x00},
{0x0212 , 0x20},
{0x0213 , 0x81},
{0x0214 , 0x15},
{0x0215 , 0x00},
{0x0216 , 0x00},
{0x0217 , 0x00},
{0x0218 , 0x46},
{0x0219 , 0x30},
{0x021A , 0x03},
{0x021B , 0x28},
{0x021C , 0x02},
{0x021D , 0x60},
{0x021E , 0x00},
{0x021F , 0x00},
{0x0220 , 0x08},
{0x0221 , 0x08},
{0x0222 , 0x04},
{0x0223 , 0x00},
{0x0224 , 0x1F},
{0x0225 , 0x1E},
{0x0226 , 0x18},
{0x0227 , 0x1D},
{0x0228 , 0x1F},
{0x0229 , 0x1F},
{0x022A , 0x01},
{0x022B , 0x04},
{0x022C , 0x05},
{0x022D , 0x05},
{0x022E , 0x04},
{0x022F , 0x03},
{0x0230 , 0x02},
{0x0231 , 0x1F},
{0x0232 , 0x1A},
{0x0233 , 0x19},
{0x0234 , 0x19},
{0x0235 , 0x1B},
{0x0236 , 0x1F},
{0x0237 , 0x04},
{0x0238 , 0xEE},
{0x0239 , 0xFF},
{0x023A , 0x00},
{0x023B , 0x00},
{0x023C , 0x00},
{0x023D , 0x00},
{0x023E , 0x00},
{0x023F , 0x00},
{0x0240 , 0x00},
{0x0241 , 0x00},
{0x0242 , 0x00},
{0x0243 , 0x21},
{0x0244 , 0x42},
{0x0245 , 0x53},
{0x0246 , 0x54},
{0x0247 , 0x54},
{0x0248 , 0x54},
{0x0249 , 0x33},
{0x024A , 0x11},
{0x024B , 0x00},
{0x024C , 0x00},
{0x024D , 0xFF},
{0x024E , 0xEE},
{0x024F , 0xDD},
{0x0250 , 0x00},
{0x0251 , 0x00},
{0x0252 , 0x00},
{0x0253 , 0x00},
{0x0254 , 0x00},
{0x0255 , 0x00},
{0x0256 , 0x00},
{0x0257 , 0x00},
{0x0258 , 0x00},
{0x0259 , 0x00},
{0x025A , 0x00},
{0x025B , 0x00},
{0x025C , 0x00},
{0x025D , 0x00},
{0x025E , 0x00},
{0x025F , 0x00},
{0x0260 , 0x00},
{0x0261 , 0x00},
{0x0262 , 0x00},
{0x0263 , 0x00},
{0x0264 , 0x00},
{0x0265 , 0x00},
{0x0266 , 0x00},
{0x0267 , 0x00},
{0x0268 , 0x8F},
{0x0269 , 0xA3},
{0x026A , 0xB4},
{0x026B , 0x90},
{0x026C , 0x00},
{0x026D , 0xD0},
{0x026E , 0x60},
{0x026F , 0xA0},
{0x0270 , 0x40},
{0x0300 , 0x81},
{0x0301 , 0x80},
{0x0302 , 0x22},
{0x0303 , 0x06},
{0x0304 , 0x03},
{0x0305 , 0x83},
{0x0306 , 0x00},
{0x0307 , 0x22},
{0x0308 , 0x00},
{0x0309 , 0x55},
{0x030A , 0x55},
{0x030B , 0x55},
{0x030C , 0x54},
{0x030D , 0x1F},
{0x030E , 0x13},
{0x030F , 0x10},
{0x0310 , 0x04},
{0x0311 , 0xFF},
{0x0312 , 0x08},
{0x0313 , 0x28},
{0x0314 , 0x66},
{0x0315 , 0x16},
{0x0316 , 0x26},
{0x0317 , 0x02},
{0x0318 , 0x08},
{0x0319 , 0x0C},
{0x031A , 0x81},
{0x031B , 0x00},
{0x031C , 0x3D},
{0x031D , 0x00},
{0x031E , 0xF9},
{0x031F , 0x00},
{0x0320 , 0x24},
{0x0321 , 0x14},
{0x0322 , 0x1A},
{0x0323 , 0x24},
{0x0324 , 0x08},
{0x0325 , 0xF0},
{0x0326 , 0x30}, 
{0x0327 , 0x17},
{0x0328 , 0x11},
{0x0329 , 0x22},
{0x032A , 0x2F},
{0x032B , 0x21},
{0x032C , 0xDA},
{0x032D , 0x10},
{0x032E , 0xEA},
{0x032F , 0x18},
{0x0330 , 0x29},
{0x0331 , 0x25},
{0x0332 , 0x12},
{0x0333 , 0x0F},
{0x0334 , 0xE0},
{0x0335 , 0x13},
{0x0336 , 0xFF},
{0x0337 , 0x20},
{0x0338 , 0x46},
{0x0339 , 0x04},
{0x033A , 0x04},
{0x033B , 0xFF},
{0x033C , 0x01},
{0x033D , 0x00},
{0x033E , 0x03},
{0x033F , 0x28},
{0x0340 , 0x02},
{0x0341 , 0x60},
{0x0342 , 0xAC},
{0x0343 , 0x97},
{0x0344 , 0x7F},
{0x0400 , 0xE8},
{0x0401 , 0x40},
{0x0402 , 0x00},
{0x0403 , 0x00},
{0x0404 , 0xF8},
{0x0405 , 0x03},
{0x0406 , 0x03},
{0x0407 , 0x85},
{0x0408 , 0x44},
{0x0409 , 0x1F},
{0x040A , 0x40},
{0x040B , 0x33},
{0x040C , 0xA0},
{0x040D , 0x00},
{0x040E , 0x00},
{0x040F , 0x00},
{0x0410 , 0x0D},
{0x0411 , 0x0D},
{0x0412 , 0x0C},
{0x0413 , 0x04},
{0x0414 , 0x00},
{0x0415 , 0x00},
{0x0416 , 0x07},
{0x0417 , 0x09},
{0x0418 , 0x16},
{0x0419 , 0x14},
{0x041A , 0x11},
{0x041B , 0x14},
{0x041C , 0x07},
{0x041D , 0x07},
{0x041E , 0x06},
{0x041F , 0x02},
{0x0420 , 0x42},
{0x0421 , 0x42},
{0x0422 , 0x47},
{0x0423 , 0x39},
{0x0424 , 0x3E},
{0x0425 , 0x4D},
{0x0426 , 0x46},
{0x0427 , 0x3A},
{0x0428 , 0x21},
{0x0429 , 0x21},
{0x042A , 0x26},
{0x042B , 0x1C},
{0x042C , 0x25},
{0x042D , 0x25},
{0x042E , 0x28},
{0x042F , 0x20},
{0x0430 , 0x3E},
{0x0431 , 0x3E},
{0x0432 , 0x33},
{0x0433 , 0x2E},
{0x0434 , 0x54},
{0x0435 , 0x53},
{0x0436 , 0x3C},
{0x0437 , 0x51},
{0x0438 , 0x2B},
{0x0439 , 0x2B},
{0x043A , 0x38},
{0x043B , 0x22},
{0x043C , 0x3B},
{0x043D , 0x3B},
{0x043E , 0x31},
{0x043F , 0x37},
{0x0440 , 0x00},
{0x0441 , 0x4B},
{0x0442 , 0x00},
{0x0443 , 0x00},
{0x0444 , 0x31},
{0x0445 , 0x00},
{0x0446 , 0x00},
{0x0447 , 0x00},
{0x0448 , 0x00},
{0x0449 , 0x00},
{0x044A , 0x00},
{0x044D , 0xE0},
{0x044E , 0x05},
{0x044F , 0x07},
{0x0450 , 0x00},
{0x0451 , 0x00},
{0x0452 , 0x00},
{0x0453 , 0x00},
{0x0454 , 0x00},
{0x0455 , 0x00},
{0x0456 , 0x00},
{0x0457 , 0x00},
{0x0458 , 0x00},
{0x0459 , 0x00},
{0x045A , 0x00},
{0x045B , 0x00},
{0x045C , 0x00},
{0x045D , 0x00},
{0x045E , 0x00},
{0x045F , 0x00},
{0x0460 , 0x80},
{0x0461 , 0x10},
{0x0462 , 0x10},
{0x0463 , 0x10},
{0x0464 , 0x08},
{0x0465 , 0x08},
{0x0466 , 0x11},
{0x0467 , 0x09},
{0x0468 , 0x23},
{0x0469 , 0x2A},
{0x046A , 0x2A},
{0x046B , 0x47},
{0x046C , 0x52},
{0x046D , 0x42},
{0x046E , 0x36},
{0x046F , 0x46},
{0x0470 , 0x3A},
{0x0471 , 0x32},
{0x0472 , 0x32},
{0x0473 , 0x38},
{0x0474 , 0x3D},
{0x0475 , 0x2F},
{0x0476 , 0x29},
{0x0477 , 0x48},
{0x0600 , 0x00},
{0x0601 , 0x24},
{0x0602 , 0x45},
{0x0603 , 0x0E},
{0x0604 , 0x14},
{0x0605 , 0x2F},
{0x0606 , 0x01},
{0x0607 , 0x0E},
{0x0608 , 0x0E},
{0x0609 , 0x37},
{0x060A , 0x18},
{0x060B , 0xA0},
{0x060C , 0x20},
{0x060D , 0x07},
{0x060E , 0x47},
{0x060F , 0x90},
{0x0610 , 0x06},
{0x0611 , 0x0C},
{0x0612 , 0x28},
{0x0613 , 0x13},
{0x0614 , 0x0B},
{0x0615 , 0x10},
{0x0616 , 0x14},
{0x0617 , 0x19},
{0x0618 , 0x52},
{0x0619 , 0xA0},
{0x061A , 0x11},
{0x061B , 0x33},
{0x061C , 0x56},
{0x061D , 0x20},
{0x061E , 0x28},
{0x061F , 0x2B},
{0x0620 , 0x22},
{0x0621 , 0x11},
{0x0622 , 0x75},
{0x0623 , 0x49},
{0x0624 , 0x6E},
{0x0625 , 0x80},
{0x0626 , 0x02},
{0x0627 , 0x0C},
{0x0628 , 0x51},
{0x0629 , 0x25},
{0x062A , 0x01},
{0x062B , 0x3D},
{0x062C , 0x04},
{0x062D , 0x01},
{0x062E , 0x0C},
{0x062F , 0x2C},
{0x0630 , 0x0D},
{0x0631 , 0x14},
{0x0632 , 0x12},
{0x0633 , 0x34},
{0x0634 , 0x00},
{0x0635 , 0x00},
{0x0636 , 0x00},
{0x0637 , 0xB1},
{0x0638 , 0x22},
{0x0639 , 0x32},
{0x063A , 0x0E},
{0x063B , 0x18},
{0x063C , 0x88},
{0x0640 , 0xB2},
{0x0641 , 0xC0},
{0x0642 , 0x01},
{0x0643 , 0x26},
{0x0644 , 0x13},
{0x0645 , 0x88},
{0x0646 , 0x64},
{0x0647 , 0x00},
{0x0681 , 0x1B},
{0x0682 , 0xA0},
{0x0683 , 0x28},
{0x0684 , 0x00},
{0x0685 , 0xB0},
{0x0686 , 0x6F},
{0x0687 , 0x33},
{0x0688 , 0x1F},
{0x0689 , 0x44},
{0x068A , 0xA8},
{0x068B , 0x44},
{0x068C , 0x08},
{0x068D , 0x08},
{0x068E , 0x00},
{0x068F , 0x00},
{0x0690 , 0x01},
{0x0691 , 0x00},
{0x0692 , 0x01},
{0x0693 , 0x00},
{0x0694 , 0x00},
{0x0695 , 0x00},
{0x0696 , 0x00},
{0x0697 , 0x00},
{0x0698 , 0x2A},
{0x0699 , 0x80},
{0x069A , 0x1F},
{0x069B , 0x00},
{0x069C , 0x02},
{0x069D , 0xF5},
{0x069E , 0x03},
{0x069F , 0x6D},
{0x06A0 , 0x0C},
{0x06A1 , 0xB8},
{0x06A2 , 0x0D},
{0x06A3 , 0x74},
{0x06A4 , 0x00},
{0x06A5 , 0x2F},
{0x06A6 , 0x00},
{0x06A7 , 0x2F},
{0x0F00 , 0x00},
{0x0F01 , 0x00},
{0x0100 , 0x01},
{0x0102 , 0x02},
{0x0104 , 0x03},
{0x0101 , 0x02},

///////////////////////////
{0x020B , 0x48},
{0x020C , 0x64},
{0x040A , 0x40},
{0x040B , 0x33},
{0x0109 , 0x00},
{0x010A , 0x04},
{0x010B , 0x03},
#if 0
{0x0110 , 0x02},
{0x0111 , 0x80},
{0x0112 , 0x01},
{0x0113 , 0xe0},
#else
{0x0110, 0x03},
{0x0111, 0x20},
{0x0112, 0x02},
{0x0113, 0x58},

#endif
{0x0116 , 0x02},
{0x0118 , 0x40},
{0x0119 , 0x01},
{0x011a , 0x04},
{0x011B , 0x00},
{0x0313 , 0x35},
{0x0314 , 0x36},
{0x0315 , 0x16},

};
	


/* 1600X1200 UXGA */
static struct reginfo sensor_uxga[] = 
{
    {0x010c , 0x00},
	{0x010d , 0x08},
	{0x010e , 0x00},
	{0x010f , 0x08},
	{0x010a , 0x00},
	{0x0110 , 0x06},
	{0x0111 , 0x40},
	{0x0112 , 0x04},
	{0x0113 , 0xb0},
	{0x0, 0x0},
};

	

/* 1280X1024 SXGA */
static struct reginfo sensor_sxga[] =
{
    {0x010c , 0x00},
	{0x010d , 0xa8},
	{0x010e , 0x00},
	{0x010f , 0x60},
	{0x010a , 0x00},
	{0x0110 , 0x05},
	{0x0111 , 0x00},
	{0x0112 , 0x04},
	{0x0113 , 0x00}, 
	{0x00, 0x00},
};

/* 800X600 SVGA*/
static struct reginfo sensor_svga[] =
{	
#if 0	
	{0x0101, 0x00},
	{0x0103, 0x00},
	{0x0105, 0x00},
	{0x0106, 0xF0},
	{0x0107, 0x00},
	{0x0108, 0x1C},
	{0x0109, 0x01},
	{0x010A, 0x00},
	{0x010B, 0x00},
	{0x010C, 0x00},
	{0x010D, 0x08},
	{0x010E, 0x00},
	{0x010F, 0x08},
	{0x0110, 0x06},
	{0x0111, 0x40},
	{0x0112, 0x04},
	{0x0113, 0xB0},
	{0x0114, 0x04},
	{0x0115, 0x00},
	{0x0116, 0x02},
	{0x0117, 0x00},
	{0x0118, 0x40},
	{0x0119, 0x02},
	{0x011A, 0x04},
	{0x011B, 0x01},
	{0x011C, 0x00},
	{0x011D, 0x01},
	{0x011E, 0x36},
	{0x011F, 0x00},
	{0x0120, 0x1C},
	{0x0121, 0x00},
	{0x0122, 0x04},
	{0x0123, 0x00},
	{0x0124, 0x00},
	{0x0125, 0x00},
	{0x0126, 0x00},
	{0x0127, 0x00},
	{0x0128, 0x00},
	{0x0200, 0x1f},
	{0x0201, 0x0c},
	{0x0202, 0x38},
	{0x0203, 0x00},
	{0x0204, 0x03},
	{0x0205, 0x1F},
	{0x0206, 0x0B},
	{0x0207, 0x20},
	{0x0208, 0x00},
	{0x0209, 0x2A},
	{0x020A, 0x01},
	{0x020B, 0x28},
	{0x020C, 0x44},
	{0x020D, 0xC8},
	{0x020E, 0xBC},
	{0x020F, 0x08},
	{0x0210, 0xD6},
	{0x0211, 0x00},
	{0x0212, 0x20},
	{0x0213, 0x81},
	{0x0214, 0x15},
	{0x0215, 0x00},
	{0x0216, 0x00},
	{0x0217, 0x00},
	{0x0218, 0x46},
	{0x0219, 0x30},
	{0x021A, 0x03},
	{0x021B, 0x28},
	{0x021C, 0x02},
	{0x021D, 0x60},
	{0x021E, 0x00},
	{0x021F, 0x00},
	{0x0220, 0x08},
	{0x0221, 0x08},
	{0x0222, 0x04},
	{0x0223, 0x00},
	{0x0224, 0x1F},
	{0x0225, 0x1E},
	{0x0226, 0x18},
	{0x0227, 0x1D},
	{0x0228, 0x1F},
	{0x0229, 0x1F},
	{0x022A, 0x01},
	{0x022B, 0x04},
	{0x022C, 0x05},
	{0x022D, 0x05},
	{0x022E, 0x04},
	{0x022F, 0x03},
	{0x0230, 0x02},
	{0x0231, 0x1F},
	{0x0232, 0x1A},
	{0x0233, 0x19},
	{0x0234, 0x19},
	{0x0235, 0x1B},
	{0x0236, 0x1F},
	{0x0237, 0x04},
	{0x0238, 0xEE},
	{0x0239, 0xFF},
	{0x023A, 0x00},
	{0x023B, 0x00},
	{0x023C, 0x00},
	{0x023D, 0x00},
	{0x023E, 0x00},
	{0x023F, 0x00},
	{0x0240, 0x00},
	{0x0241, 0x00},
	{0x0242, 0x00},
	{0x0243, 0x21},
	{0x0244, 0x42},
	{0x0245, 0x53},
	{0x0246, 0x54},
	{0x0247, 0x54},
	{0x0248, 0x54},
	{0x0249, 0x33},
	{0x024A, 0x11},
	{0x024B, 0x00},
	{0x024C, 0x00},
	{0x024D, 0xFF},
	{0x024E, 0xEE},
	{0x024F, 0xDD},
	{0x0250, 0x00},
	{0x0251, 0x00},
	{0x0252, 0x00},
	{0x0253, 0x00},
	{0x0254, 0x00},
	{0x0255, 0x00},
	{0x0256, 0x00},
	{0x0257, 0x00},
	{0x0258, 0x00},
	{0x0259, 0x00},
	{0x025A, 0x00},
	{0x025B, 0x00},
	{0x025C, 0x00},
	{0x025D, 0x00},
	{0x025E, 0x00},
	{0x025F, 0x00},
	{0x0260, 0x00},
	{0x0261, 0x00},
	{0x0262, 0x00},
	{0x0263, 0x00},
	{0x0264, 0x00},
	{0x0265, 0x00},
	{0x0266, 0x00},
	{0x0267, 0x00},
	{0x0268, 0x8F},
	{0x0269, 0xA3},
	{0x026A, 0xB4},
	{0x026B, 0x90},
	{0x026C, 0x00},
	{0x026D, 0xD0},
	{0x026E, 0x60},
	{0x026F, 0xA0},
	{0x0270, 0x40},
	{0x0300, 0x81},
	{0x0301, 0x80},
	{0x0302, 0x22},
	{0x0303, 0x06},
	{0x0304, 0x03},
	{0x0305, 0x83},
	{0x0306, 0x00},
	{0x0307, 0x22},
	{0x0308, 0x00},
	{0x0309, 0x55},
	{0x030A, 0x55},
	{0x030B, 0x55},
	{0x030C, 0x54},
	{0x030D, 0x1F},
	{0x030E, 0x0A},
	{0x030F, 0x10},
	{0x0310, 0x04},
	{0x0311, 0xFF},
	{0x0312, 0x08},
	{0x0313, 0x35},
	{0x0314, 0x36},
	{0x0315, 0x15},
	{0x0316, 0x26},
	{0x0317, 0x02},
	{0x0318, 0x08},
	{0x0319, 0x0C},
	{0x031A, 0x81},
	{0x031B, 0x00},
	{0x031C, 0x3D},
	{0x031D, 0x00},
	{0x031E, 0xF9},
	{0x031F, 0x00},
	{0x0320, 0x24},
	{0x0321, 0x14},
	{0x0322, 0x1A},
	{0x0323, 0x24},
	{0x0324, 0x08},
	{0x0325, 0xF0},
	{0x0326, 0x30},
	{0x0327, 0x17},
	{0x0328, 0x11},
	{0x0329, 0x22},
	{0x032A, 0x2F},
	{0x032B, 0x21},
	{0x032C, 0xDA},
	{0x032D, 0x10},
	{0x032E, 0xEA},
	{0x032F, 0x18},
	{0x0330, 0x29},
	{0x0331, 0x25},
	{0x0332, 0x12},
	{0x0333, 0x0F},
	{0x0334, 0xE0},
	{0x0335, 0x13},
	{0x0336, 0xFF},
	{0x0337, 0x20},
	{0x0338, 0x46},
	{0x0339, 0x04},
	{0x033A, 0x04},
	{0x033B, 0xFF},
	{0x033C, 0x01},
	{0x033D, 0x00},
	{0x033E, 0x03},
	{0x033F, 0x28},
	{0x0340, 0x02},
	{0x0341, 0x60},
	{0x0342, 0xAC},
	{0x0343, 0x97},
	{0x0344, 0x7F},
	{0x0400, 0xE8},
	{0x0401, 0x40},
	{0x0402, 0x00},
	{0x0403, 0x00},
	{0x0404, 0xF8},
	{0x0405, 0x03},
	{0x0406, 0x03},
	{0x0407, 0x85},
	{0x0408, 0x44},
	{0x0409, 0x1F},
	{0x040A, 0x40},
	{0x040B, 0x33},
	{0x040C, 0xA0},
	{0x040D, 0x00},
	{0x040E, 0x00},
	{0x040F, 0x00},
	{0x0410, 0x0D},
	{0x0411, 0x0D},
	{0x0412, 0x0C},
	{0x0413, 0x04},
	{0x0414, 0x00},
	{0x0415, 0x00},
	{0x0416, 0x07},
	{0x0417, 0x09},
	{0x0418, 0x16},
	{0x0419, 0x14},
	{0x041A, 0x11},
	{0x041B, 0x14},
	{0x041C, 0x07},
	{0x041D, 0x07},
	{0x041E, 0x06},
	{0x041F, 0x02},
	{0x0420, 0x42},
	{0x0421, 0x42},
	{0x0422, 0x47},
	{0x0423, 0x39},
	{0x0424, 0x3E},
	{0x0425, 0x4D},
	{0x0426, 0x46},
	{0x0427, 0x3A},
	{0x0428, 0x21},
	{0x0429, 0x21},
	{0x042A, 0x26},
	{0x042B, 0x1C},
	{0x042C, 0x25},
	{0x042D, 0x25},
	{0x042E, 0x28},
	{0x042F, 0x20},
	{0x0430, 0x3E},
	{0x0431, 0x3E},
	{0x0432, 0x33},
	{0x0433, 0x2E},
	{0x0434, 0x54},
	{0x0435, 0x53},
	{0x0436, 0x3C},
	{0x0437, 0x51},
	{0x0438, 0x2B},
	{0x0439, 0x2B},
	{0x043A, 0x38},
	{0x043B, 0x22},
	{0x043C, 0x3B},
	{0x043D, 0x3B},
	{0x043E, 0x31},
	{0x043F, 0x37},
	{0x0440, 0x00},
	{0x0441, 0x4B},
	{0x0442, 0x00},
	{0x0443, 0x00},
	{0x0444, 0x31},
	{0x0445, 0x00},
	{0x0446, 0x00},
	{0x0447, 0x00},
	{0x0448, 0x00},
	{0x0449, 0x00},
	{0x044A, 0x00},
	{0x044D, 0xE0},
	{0x044E, 0x05},
	{0x044F, 0x07},
	{0x0450, 0x00},
	{0x0451, 0x00},
	{0x0452, 0x00},
	{0x0453, 0x00},
	{0x0454, 0x00},
	{0x0455, 0x00},
	{0x0456, 0x00},
	{0x0457, 0x00},
	{0x0458, 0x00},
	{0x0459, 0x00},
	{0x045A, 0x00},
	{0x045B, 0x00},
	{0x045C, 0x00},
	{0x045D, 0x00},
	{0x045E, 0x00},
	{0x045F, 0x00},
	{0x0460, 0x80},
	{0x0461, 0x10},
	{0x0462, 0x10},
	{0x0463, 0x10},
	{0x0464, 0x08},
	{0x0465, 0x08},
	{0x0466, 0x11},
	{0x0467, 0x09},
	{0x0468, 0x23},
	{0x0469, 0x2A},
	{0x046A, 0x2A},
	{0x046B, 0x47},
	{0x046C, 0x52},
	{0x046D, 0x42},
	{0x046E, 0x36},
	{0x046F, 0x46},
	{0x0470, 0x3A},
	{0x0471, 0x32},
	{0x0472, 0x32},
	{0x0473, 0x38},
	{0x0474, 0x3D},
	{0x0475, 0x2F},
	{0x0476, 0x29},
	{0x0477, 0x48},
	{0x0600, 0x00},
	{0x0601, 0x24},
	{0x0602, 0x45},
	{0x0603, 0x0E},
	{0x0604, 0x14},
	{0x0605, 0x2F},
	{0x0606, 0x01},
	{0x0607, 0x0E},
	{0x0608, 0x0E},
	{0x0609, 0x37},
	{0x060A, 0x18},
	{0x060B, 0xA0},
	{0x060C, 0x20},
	{0x060D, 0x07},
	{0x060E, 0x47},
	{0x060F, 0x90},
	{0x0610, 0x06},
	{0x0611, 0x0C},
	{0x0612, 0x28},
	{0x0613, 0x13},
	{0x0614, 0x0B},
	{0x0615, 0x10},
	{0x0616, 0x14},
	{0x0617, 0x19},
	{0x0618, 0x52},
	{0x0619, 0xA0},
	{0x061A, 0x11},
	{0x061B, 0x33},
	{0x061C, 0x56},
	{0x061D, 0x20},
	{0x061E, 0x28},
	{0x061F, 0x2B},
	{0x0620, 0x22},
	{0x0621, 0x11},
	{0x0622, 0x75},
	{0x0623, 0x49},
	{0x0624, 0x6E},
	{0x0625, 0x80},
	{0x0626, 0x02},
	{0x0627, 0x0C},
	{0x0628, 0x51},
	{0x0629, 0x25},
	{0x062A, 0x01},
	{0x062B, 0x3D},
	{0x062C, 0x04},
	{0x062D, 0x01},
	{0x062E, 0x0C},
	{0x062F, 0x2C},
	{0x0630, 0x0D},
	{0x0631, 0x14},
	{0x0632, 0x12},
	{0x0633, 0x34},
	{0x0634, 0x00},
	{0x0635, 0x00},
	{0x0636, 0x00},
	{0x0637, 0xB1},
	{0x0638, 0x22},
	{0x0639, 0x32},
	{0x063A, 0x0E},
	{0x063B, 0x18},
	{0x063C, 0x88},
	{0x0640, 0xB2},
	{0x0641, 0xC0},
	{0x0642, 0x01},
	{0x0643, 0x26},
	{0x0644, 0x13},
	{0x0645, 0x88},
	{0x0646, 0x64},
	{0x0647, 0x00},
	{0x0681, 0x1B},
	{0x0682, 0xA0},
	{0x0683, 0x28},
	{0x0684, 0x00},
	{0x0685, 0xB0},
	{0x0686, 0x6F},
	{0x0687, 0x33},
	{0x0688, 0x1F},
	{0x0689, 0x44},
	{0x068A, 0xA8},
	{0x068B, 0x44},
	{0x068C, 0x08},
	{0x068D, 0x08},
	{0x068E, 0x00},
	{0x068F, 0x00},
	{0x0690, 0x01},
	{0x0691, 0x00},
	{0x0692, 0x01},
	{0x0693, 0x00},
	{0x0694, 0x00},
	{0x0695, 0x00},
	{0x0696, 0x00},
	{0x0697, 0x00},
	{0x0698, 0x2A},
	{0x0699, 0x80},
	{0x069A, 0x1F},
	{0x069B, 0x00},
	{0x069C, 0x02},
	{0x069D, 0xF5},
	{0x069E, 0x03},
	{0x069F, 0x6D},
	{0x06A0, 0x0C},
	{0x06A1, 0xB8},
	{0x06A2, 0x0D},
	{0x06A3, 0x74},
	{0x06A4, 0x00},
	{0x06A5, 0x2F},
	{0x06A6, 0x00},
	{0x06A7, 0x2F},
	{0x0F00, 0x00},
	{0x0F01, 0x00},
	{0x0100, 0x01},
	{0x0102, 0x02},
	{0x0104, 0x03},
#endif
	{0x020B, 0x48},
	{0x020C, 0x64},
	{0x040A, 0x40},
	{0x040B, 0x33},
	{0x010c , 0x00},
	{0x010d , 0x08},
	{0x010e , 0x00},
	{0x010f , 0x08},
	{0x010a , 0x00},
	{0x0109, 0x00},
	{0x010A, 0x04},
	{0x010B, 0x03},
	{0x0110, 0x03},
	{0x0111, 0x20},
	{0x0112, 0x02},
	{0x0113, 0x58},
	{0x0116, 0x02},
	{0x0118, 0x40},
	{0x0119, 0x02},
	{0x011a, 0x04},
	{0x011B, 0x01},
	{0x0, 0x0},
};
	
	

/* 640X480 VGA */
static struct reginfo sensor_vga[] =
{
   {0x020B , 0x48},
   {0x020C , 0x64},
   {0x040A , 0x40},
   {0x040B , 0x33},
   {0x0109 , 0x00},
   {0x010A , 0x04},
   {0x010B , 0x03},
   {0x010c , 0x00},
   {0x010d , 0xa8},
   {0x010e , 0x00},
   {0x010f , 0x60},
   {0x010a , 0x04},
   #if 1
   {0x0110 , 0x02},
   {0x0111 , 0x80},
   {0x0112 , 0x01},
   {0x0113 , 0xe0},
   #else
   {0x0110, 0x03},
   {0x0111, 0x20},
   {0x0112, 0x02},
   {0x0113, 0x58},
   #endif
   {0x0116 , 0x02},
   {0x0118 , 0x40},
   {0x0119 , 0x01},
   {0x011a , 0x04},
   {0x011B , 0x00},
   {0x0313 , 0x35},
   {0x0314 , 0x36},
   {0x0315 , 0x16}, 
   {0x0, 0x0},
};

/* 352X288 CIF */
static struct reginfo sensor_cif[] =
{
    {0x0, 0x0},
};

/* 320*240 QVGA */
static  struct reginfo sensor_qvga[] =
{
    

    {0x0, 0x0},
};

/* 176X144 QCIF*/
static struct reginfo sensor_qcif[] =
{
	
    {0x0, 0x0},
};
#if 0
/* 160X120 QQVGA*/
static struct reginfo gt2005_qqvga[] =
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



static  struct reginfo gt2005_Sharpness_auto[] =
{
    {0x3306, 0x00},
};

static  struct reginfo gt2005_Sharpness1[] =
{
    {0x3306, 0x08},
    {0x3371, 0x00},
};

static  struct reginfo gt2005_Sharpness2[][3] =
{
    //Sharpness 2
    {0x3306, 0x08},
    {0x3371, 0x01},
};

static  struct reginfo gt2005_Sharpness3[] =
{
    //default
    {0x3306, 0x08},
    {0x332d, 0x02},
};
static  struct reginfo gt2005_Sharpness4[]=
{
    //Sharpness 4
    {0x3306, 0x08},
    {0x332d, 0x03},
};

static  struct reginfo gt2005_Sharpness5[] =
{
    //Sharpness 5
    {0x3306, 0x08},
    {0x332d, 0x04},
};
#endif

static  struct reginfo sensor_ClrFmt_YUYV[]=
{
    //{0x3400, 0x00},
    {0x0000, 0x00}
};

static  struct reginfo sensor_ClrFmt_UYVY[]=
{
    //{0x3400, 0x02},
    {0x0000, 0x00}
};

#if CONFIG_SENSOR_WhiteBalance
static  struct reginfo sensor_WhiteB_Auto[]=
{
    {0x3306, 0x00},  //AWB auto, bit[1]:0,auto
    {0x0000, 0x00}
};
/* Cloudy Colour Temperature : 6500K - 8000K  */
static  struct reginfo sensor_WhiteB_Cloudy[]=
{
    {0x3306, 0x82},
    {0x3337, 0x68},
    {0x3338, 0x40},
    {0x3339, 0x4e},
    {0x0000, 0x00}
};
/* ClearDay Colour Temperature : 5000K - 6500K  */
static  struct reginfo sensor_WhiteB_ClearDay[]=
{
    //Sunny
    {0x3306, 0x02}, //AWB off
    {0x3337, 0x5e},
    {0x3338, 0x40},
    {0x3339, 0x46},
    {0x0000, 0x00}
};
/* Office Colour Temperature : 3500K - 5000K  */
static  struct reginfo sensor_WhiteB_TungstenLamp1[]=
{
    //Office
    {0x3306, 0x02},
    {0x3337, 0x52},
    {0x3338, 0x40},
    {0x3339, 0x58},
    {0x0000, 0x00}

};
/* Home Colour Temperature : 2500K - 3500K  */
static  struct reginfo sensor_WhiteB_TungstenLamp2[]=
{
    //Home
    {0x3306, 0x02},
    {0x3337, 0x44},
    {0x3338, 0x40},
    {0x3339, 0x70},
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
    {0x3301, 0xff},//bit[7]:1, enable SDE
    {0x3391, 0x04},
    {0x3390, 0x49},
    {0x339a, 0x20},
    {0x0000, 0x00}
};

static  struct reginfo sensor_Brightness1[]=
{
    // Brightness -1
    {0x3301, 0xff},//bit[7]:1, enable SDE
    {0x3391, 0x04},
    {0x3390, 0x49},
    {0x339a, 0x10},
    {0x0000, 0x00}
};

static  struct reginfo sensor_Brightness2[]=
{
    //  Brightness 0
    {0x3301, 0xff},//bit[7]:1, enable SDE
    {0x3391, 0x00},
    {0x3390, 0x41},
    {0x339a, 0x00},
    {0x0000, 0x00}
};

static  struct reginfo sensor_Brightness3[]=
{
    // Brightness +1
    {0x3301, 0xff},//bit[7]:1, enable SDE
    {0x3391, 0x04},
    {0x3390, 0x41},
    {0x339a, 0x10},
    {0x0000, 0x00}
};

static  struct reginfo sensor_Brightness4[]=
{
    //  Brightness +2
    {0x3301, 0xff},//bit[7]:1, enable SDE
    {0x3391, 0x04},
    {0x3390, 0x41},
    {0x339a, 0x20},
    {0x0000, 0x00}
};

static  struct reginfo sensor_Brightness5[]=
{
    //  Brightness +3
    {0x3301, 0xff},//bit[7]:1, enable SDE
    {0x3391, 0x04}, //bit[2] enable
    {0x3390, 0x41}, //bit[3] sign of brightness
    {0x339a, 0x30},
    {0x0000, 0x00}
};
static struct reginfo *sensor_BrightnessSeqe[] = {sensor_Brightness0, sensor_Brightness1, sensor_Brightness2, sensor_Brightness3,
    sensor_Brightness4, sensor_Brightness5,NULL,
};

#endif

#if CONFIG_SENSOR_Effect
static  struct reginfo sensor_Effect_Normal[] =
{
    {0x3391, 0x00},
    {0x0000, 0x00}
};

static  struct reginfo sensor_Effect_WandB[] =
{
    {0x3391, 0x20},
    {0x0000, 0x00}
};

static  struct reginfo sensor_Effect_Sepia[] =
{
    {0x3391, 0x18},
    {0x3396, 0x40},
    {0x3397, 0xa6},
    {0x0000, 0x00}
};

static  struct reginfo sensor_Effect_Negative[] =
{
    //Negative
    {0x3391, 0x40}, //bit[6] negative
    {0x0000, 0x00}
};
static  struct reginfo sensor_Effect_Bluish[] =
{
    // Bluish
    {0x3391, 0x18},
    {0x3396, 0xa0},
    {0x3397, 0x40},
    {0x0000, 0x00}
};

static  struct reginfo sensor_Effect_Green[] =
{
    //  Greenish
    {0x3391, 0x18},
    {0x3396, 0x60},
    {0x3397, 0x60},
    {0x0000, 0x00}
};
static struct reginfo *sensor_EffectSeqe[] = {sensor_Effect_Normal, sensor_Effect_WandB, sensor_Effect_Negative,sensor_Effect_Sepia,
    sensor_Effect_Bluish, sensor_Effect_Green,NULL,
};
#endif
#if CONFIG_SENSOR_Exposure
static  struct reginfo sensor_Exposure0[]=
{
    //-3
    {0x3047, 0x05},
    {0x3018, 0x40},
    {0x3019, 0x30},
    {0x301a, 0x71},
    {0x0000, 0x00}
};

static  struct reginfo sensor_Exposure1[]=
{
    //-2
    {0x3047, 0x05},
    {0x3018, 0x5a},
    {0x3019, 0x4a},
    {0x301a, 0xc2},
    {0x0000, 0x00}
};

static  struct reginfo sensor_Exposure2[]=
{
    //-0.3EV
    {0x3047, 0x05},
    {0x3018, 0x6a},
    {0x3019, 0x5a},
    {0x301a, 0xd4},
    {0x0000, 0x00}
};

static  struct reginfo sensor_Exposure3[]=
{
    //default
    {0x3047, 0x05},
    {0x3018, 0x78},
    {0x3019, 0x68},
    {0x301a, 0xd4},
    {0x0000, 0x00}
};

static  struct reginfo sensor_Exposure4[]=
{
    // 1
    {0x3047, 0x05},
    {0x3018, 0x88},
    {0x3019, 0x78},
    {0x301a, 0xd5},
    {0x0000, 0x00}
};

static  struct reginfo sensor_Exposure5[]=
{
    // 2
    {0x3047, 0x05},
    {0x3018, 0xa8},
    {0x3019, 0x98},
    {0x301a, 0xe6},
    {0x0000, 0x00}
};

static  struct reginfo sensor_Exposure6[]=
{
    // 3
    {0x3047, 0x05},
    {0x3018, 0xc8},
    {0x3019, 0xb8},
    {0x301a, 0xf7},
    {0x0000, 0x00}
};

static struct reginfo *sensor_ExposureSeqe[] = {sensor_Exposure0, sensor_Exposure1, sensor_Exposure2, sensor_Exposure3,
    sensor_Exposure4, sensor_Exposure5,sensor_Exposure6,NULL,
};
#endif
#if CONFIG_SENSOR_Saturation
static  struct reginfo sensor_Saturation0[]=
{
    {0x3301, 0xff},//bit[7]:1, enable SDE
    {0x3391, 0x02},
    {0x3394, 0x40},
    {0x3395, 0x40},
    {0x0000, 0x00}
};

static  struct reginfo sensor_Saturation1[]=
{
    {0x3301, 0xff},//bit[7]:1, enable SDE
    {0x3391, 0x02},
    {0x3394, 0x50},
    {0x3395, 0x50},
    {0x0000, 0x00}
};

static  struct reginfo sensor_Saturation2[]=
{
    {0x3301, 0xff},//bit[7]:1, enable SDE
    {0x3391, 0x02}, //enable color saturation
    {0x3394, 0x70},
    {0x3395, 0x70},
    {0x0000, 0x00}
};
static struct reginfo *sensor_SaturationSeqe[] = {sensor_Saturation0, sensor_Saturation1, sensor_Saturation2, NULL,};

#endif
#if CONFIG_SENSOR_Contrast
static  struct reginfo sensor_Contrast0[]=
{
    //Contrast -3
    {0x3301, 0xff},//bit[7]:1, enable SDE
    {0x3391, 0x04},
    {0x3390, 0x45},
    {0x3398, 0x18},
    {0x3399, 0x18},
    {0x0000, 0x00}
};

static  struct reginfo sensor_Contrast1[]=
{
    //Contrast -2
    {0x3301, 0xff},//bit[7]:1, enable SDE
    {0x3391, 0x04},
    {0x3390, 0x45},
    {0x3398, 0x18},
    {0x3399, 0x18},
    {0x0000, 0x00}
};

static  struct reginfo sensor_Contrast2[]=
{
    // Contrast -1
    {0x3301, 0xff},//bit[7]:1, enable SDE
    {0x3391, 0x04},
    {0x3390, 0x45},
    {0x3398, 0x1c},
    {0x3399, 0x1c},
    {0x0000, 0x00}
};

static  struct reginfo sensor_Contrast3[]=
{
    //Contrast 0
    {0x3301, 0xff},//bit[7]:1, enable SDE
    {0x3391, 0x00},
    {0x3390, 0x41},
    {0x3398, 0x20},
    {0x3399, 0x20},
    {0x0000, 0x00}
};

static  struct reginfo sensor_Contrast4[]=
{
    //Contrast +1
    {0x3301, 0xff},//bit[7]:1, enable SDE
    {0x3391, 0x04},
    {0x3390, 0x45},
    {0x3398, 0x24},
    {0x3399, 0x24},
    {0x0000, 0x00}
};


static  struct reginfo sensor_Contrast5[]=
{
    //Contrast +2
    {0x3301, 0xff},//bit[7]:1, enable SDE
    {0x3391, 0x04},
    {0x3390, 0x45},
    {0x3398, 0x28},
    {0x3399, 0x28},
    {0x0000, 0x00}
};

static  struct reginfo sensor_Contrast6[]=
{
    //Contrast +3
    {0x3301, 0xff},//bit[7]:1, enable SDE
    {0x3391, 0x04}, //bit[2] enable contrast/brightness
    {0x3390, 0x45}, //bit[2] Yoffset sign
    {0x3398, 0x2c},
    {0x3399, 0x2c},
    {0x0000, 0x00}
};
static struct reginfo *sensor_ContrastSeqe[] = {sensor_Contrast0, sensor_Contrast1, sensor_Contrast2, sensor_Contrast3,
    sensor_Contrast4, sensor_Contrast5, sensor_Contrast6, NULL,
};

#endif
#if CONFIG_SENSOR_Mirror
static  struct reginfo sensor_MirrorOn[]=
{
    {0x3069, 0x84},
    {0x307c, 0x13},
    {0x3087, 0x02},
    {0x0000, 0x00}
};

static  struct reginfo sensor_MirrorOff[]=
{
    {0x3069, 0x84},
    {0x307c, 0x10},
    {0x3087, 0x02},
    {0x0000, 0x00}
};
static struct reginfo *sensor_MirrorSeqe[] = {sensor_MirrorOff, sensor_MirrorOn,NULL,};
#endif
#if CONFIG_SENSOR_Flip
static  struct reginfo sensor_FlipOn[]=
{
    {0x300e, 0x34},
    {0x300f, 0xa6},
    {0x3010, 0x81},
    {0x3082, 0x01},
    {0x30f4, 0x01},
    {0x3090, 0x3b},
    {0x3091, 0xc0},
    {0x30ac, 0x42},
    {0x0000, 0x00}
};

static  struct reginfo sensor_FlipOff[]=
{
    {0x300e, 0x34},
    {0x300f, 0xa6},
    {0x3010, 0x81},
    {0x3082, 0x01},
    {0x30f4, 0x01},
    {0x3090, 0x33},
    {0x3091, 0xc0},
    {0x30ac, 0x42},
    {0x0000, 0x00}
};
static struct reginfo *sensor_FlipSeqe[] = {sensor_FlipOff, sensor_FlipOn,NULL,};

#endif
#if CONFIG_SENSOR_Scene
static  struct reginfo sensor_SceneAuto[] =
{
#if 0                           /* ddl@rock-chips.com : */
    {0x3014, 0x04},
    {0x3015, 0x00},
    {0x302e, 0x00},
    {0x302d, 0x00},
    {0x0000, 0x00}
#else
    {0x3014, 0x84},
    {0x3015, 0x02},
    {0x302e, 0x00},
    {0x302d, 0x00},
    {0x0000, 0x00}
#endif
};

static  struct reginfo sensor_SceneNight[] =
{
#if 1
    //30fps ~ 5fps night mode for 60/50Hz light environment, 24Mhz clock input,36Mzh pclk
    {0x300e, 0x34},
    {0x3011, 0x00},
    {0x302c, 0x00},
    {0x3071, 0x00},
    {0x3070, 0xb9},
    {0x301c, 0x02},
    {0x3073, 0x00},
    {0x3072, 0x9a},
    {0x301d, 0x03},
    {0x3014, 0x0c},
    {0x3015, 0x50},//add 5 dummy frame
    {0x302e, 0x00},
    {0x302d, 0x00},
    {0x0000, 0x00}
#else
    //15fps ~ 5fps night mode for 60/50Hz light environment, 24Mhz clock input,18Mhz pclk
    {0x300e, 0x34},
    {0x3011, 0x01},
    {0x302c, 0x00},
    {0x3071, 0x00},
    {0x3070, 0x5d},
    {0x301c, 0x05},
    {0x3073, 0x00},
    {0x3072, 0x4d},
    {0x301d, 0x07},
    {0x3014, 0x0c},
    {0x3015, 0x50},
    {0x302e, 0x00},
    {0x302d, 0x00},
#endif
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
    {V4L2_MBUS_FMT_UYVY8_2X8, V4L2_COLORSPACE_JPEG},
    {V4L2_MBUS_FMT_YUYV8_2X8, V4L2_COLORSPACE_JPEG}	
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

struct sensor
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
static int sensor_check_array(struct i2c_client *client, struct reginfo *regarray)
{
  int ret;
  int i = 0,j=0;  
  u8 value;
  
  SENSOR_DG("%s >>>>>>>>>>>>>>>>>>>>>>\n",__FUNCTION__);
  while(regarray[i].reg != 0)
  {
     ret = sensor_read(client,regarray[i].reg,&value);
	 if(ret !=0)
	 {
	  SENSOR_TR("read value failed\n");

	 }
	 if(regarray[i].val != value)
	 {
	  SENSOR_DG("%s reg[0x%x] check err,writte :0x%x  read:0x%x\n",__FUNCTION__,regarray[i].reg,regarray[i].val,value);
	 }
	 else
	  j++;
	 
	 i++;
  }
  if(i==j)
  	 SENSOR_DG("%s check success\n",__FUNCTION__);
  	
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
			SENSOR_TR("%s %s cmd(0x%x) is unknown!",SENSOR_NAME_STRING(),__FUNCTION__,cmd);
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
    char value;
    int ret,pid = 0;

    SENSOR_DG("\n%s..%s.. \n",SENSOR_NAME_STRING(),__FUNCTION__);

	if (sensor_ioctrl(icd, Sensor_PowerDown, 0) < 0) {
		ret = -ENODEV;
		goto sensor_INIT_ERR;
	}

    /* soft reset */
	if (sensor_task_lock(client,1)<0)
		goto sensor_INIT_ERR;
   /* ret = sensor_write(client, 0x3012, 0x80);
    if (ret != 0)
    {
        SENSOR_TR("%s soft reset sensor failed\n",SENSOR_NAME_STRING());
        ret = -ENODEV;
		goto sensor_INIT_ERR;
    }

    mdelay(5); */ //delay 5 microseconds
	/* check if it is an sensor sensor */
    ret = sensor_read(client, 0x0000, &value);
    if (ret != 0) {
        SENSOR_TR("read chip id high byte failed\n");
        ret = -ENODEV;
        goto sensor_INIT_ERR;
    }

    pid |= (value << 8);

    ret = sensor_read(client, 0x0001, &value);
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
    sensor_set_focus();
    qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_FOCUS_ABSOLUTE);
	if (qctrl)
        sensor->info_priv.focus = qctrl->default_value;
	#endif

	#if CONFIG_SENSOR_Flash	
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_FLASH);
	if (qctrl)
        sensor->info_priv.flash = qctrl->default_value;
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

    struct sensor *sensor = to_sensor(client);
	SENSOR_DG("\n%s..%s.. Enter\n",SENSOR_NAME_STRING(),__FUNCTION__);

	/* ddl@rock-chips.com : all sensor output pin must change to input for other sensor */
    if (sensor->info_priv.funmodule_state & SENSOR_INIT_IS_OK) {
    sensor_write(client, 0x30b0, 0x00);
	sensor_write(client, 0x30b1, 0x00);
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
    {0x30ab, 0x00},
    {0x30ad, 0x0a},
    {0x30ae,0x27},
    {0x363b,0x01},
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
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    const struct sensor_datafmt *fmt;
    struct sensor *sensor = to_sensor(client);
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
            msleep(600);
			sensor->info_priv.video2preview = false;
			sensor->info_priv.snap2preview = false;
		}

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
    struct sensor *sensor = to_sensor(client);
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
    else
    {
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

    id->ident = SENSOR_V4L2_IDENT;      /* ddl@rock-chips.com :  Return gt2005  identifier */
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

    if ((value > 0) && ((digitalzoom_cur + value) > digitalzoom_total))
    {
        value = digitalzoom_total - digitalzoom_cur;
    }

    if ((value < 0) && ((digitalzoom_cur + value) < 0))
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
   /* ret = sensor_write(client, 0x3012, 0x80);
    if (ret != 0)
    {
        SENSOR_TR("soft reset %s failed\n",SENSOR_NAME_STRING());
        return -ENODEV;
    }
    mdelay(5);      */    //delay 5 microseconds

    /* check if it is an sensor sensor */
    ret = sensor_read(client, 0x0000, &value);
    if (ret != 0) {
        SENSOR_TR("read chip id high byte failed\n");
        ret = -ENODEV;
        goto sensor_video_probe_err;
    }

    pid |= (value << 8);

    ret = sensor_read(client, 0x0001, &value);
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
	struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct soc_camera_device *icd = client->dev.platform_data;
    struct sensor *sensor = to_sensor(client);
#if CONFIG_SENSOR_Flash
    int i;
#endif
    int ret = 0;
    
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
                sensor->sensor_gpio_res = NULL;
                for (i=0; i<RK29_CAM_SUPPORT_NUMS;i++) {
                    if (sensor->sensor_io_request->gpio_res[i].dev_name && 
                        (strcmp(sensor->sensor_io_request->gpio_res[i].dev_name, dev_name(icd->pdev)) == 0)) {
                        sensor->sensor_gpio_res = (struct rk29camera_gpio_res*)&sensor->sensor_io_request->gpio_res[i];
                    }
                }
                if (sensor->sensor_gpio_res == NULL) {
                    SENSOR_TR("%s %s obtain gpio resource failed when RK29_CAM_SUBDEV_IOREQUEST \n",SENSOR_NAME_STRING(),__FUNCTION__);
                    ret = -EINVAL;
                    goto sensor_ioctl_end;
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

