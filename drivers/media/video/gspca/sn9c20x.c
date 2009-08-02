/*
 *	Sonix sn9c201 sn9c202 library
 *	Copyright (C) 2008-2009 microdia project <microdia@googlegroups.com>
 *	Copyright (C) 2009 Brian Johnson <brijohn@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifdef CONFIG_USB_GSPCA_SN9C20X_EVDEV
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/usb/input.h>
#include <linux/input.h>
#endif

#include "gspca.h"
#include "jpeg.h"

#include <media/v4l2-chip-ident.h>

MODULE_AUTHOR("Brian Johnson <brijohn@gmail.com>, "
		"microdia project <microdia@googlegroups.com>");
MODULE_DESCRIPTION("GSPCA/SN9C20X USB Camera Driver");
MODULE_LICENSE("GPL");

#define MODULE_NAME "sn9c20x"

#define MODE_RAW	0x10
#define MODE_JPEG	0x20
#define MODE_SXGA	0x80

#define SENSOR_OV9650	0
#define SENSOR_OV9655	1
#define SENSOR_SOI968	2
#define SENSOR_OV7660	3
#define SENSOR_OV7670	4
#define SENSOR_MT9V011	5
#define SENSOR_MT9V111	6
#define SENSOR_MT9V112	7
#define SENSOR_MT9M001	8
#define SENSOR_MT9M111	9
#define SENSOR_HV7131R	10
#define SENSOR_MT9VPRB	20

/* specific webcam descriptor */
struct sd {
	struct gspca_dev gspca_dev;

#define MIN_AVG_LUM 80
#define MAX_AVG_LUM 130
	atomic_t avg_lum;
	u8 old_step;
	u8 older_step;
	u8 exposure_step;

	u8 brightness;
	u8 contrast;
	u8 saturation;
	s16 hue;
	u8 gamma;
	u8 red;
	u8 blue;

	u8 hflip;
	u8 vflip;
	u8 gain;
	u16 exposure;
	u8 auto_exposure;

	u8 i2c_addr;
	u8 sensor;
	u8 hstart;
	u8 vstart;

	u8 *jpeg_hdr;
	u8 quality;

#ifdef CONFIG_USB_GSPCA_SN9C20X_EVDEV
	struct input_dev *input_dev;
	u8 input_gpio;
	struct task_struct *input_task;
#endif
};

static int sd_setbrightness(struct gspca_dev *gspca_dev, s32 val);
static int sd_getbrightness(struct gspca_dev *gspca_dev, s32 *val);
static int sd_setcontrast(struct gspca_dev *gspca_dev, s32 val);
static int sd_getcontrast(struct gspca_dev *gspca_dev, s32 *val);
static int sd_setsaturation(struct gspca_dev *gspca_dev, s32 val);
static int sd_getsaturation(struct gspca_dev *gspca_dev, s32 *val);
static int sd_sethue(struct gspca_dev *gspca_dev, s32 val);
static int sd_gethue(struct gspca_dev *gspca_dev, s32 *val);
static int sd_setgamma(struct gspca_dev *gspca_dev, s32 val);
static int sd_getgamma(struct gspca_dev *gspca_dev, s32 *val);
static int sd_setredbalance(struct gspca_dev *gspca_dev, s32 val);
static int sd_getredbalance(struct gspca_dev *gspca_dev, s32 *val);
static int sd_setbluebalance(struct gspca_dev *gspca_dev, s32 val);
static int sd_getbluebalance(struct gspca_dev *gspca_dev, s32 *val);
static int sd_setvflip(struct gspca_dev *gspca_dev, s32 val);
static int sd_getvflip(struct gspca_dev *gspca_dev, s32 *val);
static int sd_sethflip(struct gspca_dev *gspca_dev, s32 val);
static int sd_gethflip(struct gspca_dev *gspca_dev, s32 *val);
static int sd_setgain(struct gspca_dev *gspca_dev, s32 val);
static int sd_getgain(struct gspca_dev *gspca_dev, s32 *val);
static int sd_setexposure(struct gspca_dev *gspca_dev, s32 val);
static int sd_getexposure(struct gspca_dev *gspca_dev, s32 *val);
static int sd_setautoexposure(struct gspca_dev *gspca_dev, s32 val);
static int sd_getautoexposure(struct gspca_dev *gspca_dev, s32 *val);

static struct ctrl sd_ctrls[] = {
	{
#define BRIGHTNESS_IDX 0
	    {
		.id      = V4L2_CID_BRIGHTNESS,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Brightness",
		.minimum = 0,
		.maximum = 0xff,
		.step    = 1,
#define BRIGHTNESS_DEFAULT 0x7f
		.default_value = BRIGHTNESS_DEFAULT,
	    },
	    .set = sd_setbrightness,
	    .get = sd_getbrightness,
	},
	{
#define CONTRAST_IDX 1
	    {
		.id      = V4L2_CID_CONTRAST,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Contrast",
		.minimum = 0,
		.maximum = 0xff,
		.step    = 1,
#define CONTRAST_DEFAULT 0x7f
		.default_value = CONTRAST_DEFAULT,
	    },
	    .set = sd_setcontrast,
	    .get = sd_getcontrast,
	},
	{
#define SATURATION_IDX 2
	    {
		.id      = V4L2_CID_SATURATION,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Saturation",
		.minimum = 0,
		.maximum = 0xff,
		.step    = 1,
#define SATURATION_DEFAULT 0x7f
		.default_value = SATURATION_DEFAULT,
	    },
	    .set = sd_setsaturation,
	    .get = sd_getsaturation,
	},
	{
#define HUE_IDX 3
	    {
		.id      = V4L2_CID_HUE,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Hue",
		.minimum = -180,
		.maximum = 180,
		.step    = 1,
#define HUE_DEFAULT 0
		.default_value = HUE_DEFAULT,
	    },
	    .set = sd_sethue,
	    .get = sd_gethue,
	},
	{
#define GAMMA_IDX 4
	    {
		.id      = V4L2_CID_GAMMA,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Gamma",
		.minimum = 0,
		.maximum = 0xff,
		.step    = 1,
#define GAMMA_DEFAULT 0x10
		.default_value = GAMMA_DEFAULT,
	    },
	    .set = sd_setgamma,
	    .get = sd_getgamma,
	},
	{
#define BLUE_IDX 5
	    {
		.id	 = V4L2_CID_BLUE_BALANCE,
		.type	 = V4L2_CTRL_TYPE_INTEGER,
		.name	 = "Blue Balance",
		.minimum = 0,
		.maximum = 0x7f,
		.step	 = 1,
#define BLUE_DEFAULT 0x28
		.default_value = BLUE_DEFAULT,
	    },
	    .set = sd_setbluebalance,
	    .get = sd_getbluebalance,
	},
	{
#define RED_IDX 6
	    {
		.id	 = V4L2_CID_RED_BALANCE,
		.type	 = V4L2_CTRL_TYPE_INTEGER,
		.name	 = "Red Balance",
		.minimum = 0,
		.maximum = 0x7f,
		.step	 = 1,
#define RED_DEFAULT 0x28
		.default_value = RED_DEFAULT,
	    },
	    .set = sd_setredbalance,
	    .get = sd_getredbalance,
	},
	{
#define HFLIP_IDX 7
	    {
		.id      = V4L2_CID_HFLIP,
		.type    = V4L2_CTRL_TYPE_BOOLEAN,
		.name    = "Horizontal Flip",
		.minimum = 0,
		.maximum = 1,
		.step    = 1,
#define HFLIP_DEFAULT 0
		.default_value = HFLIP_DEFAULT,
	    },
	    .set = sd_sethflip,
	    .get = sd_gethflip,
	},
	{
#define VFLIP_IDX 8
	    {
		.id      = V4L2_CID_VFLIP,
		.type    = V4L2_CTRL_TYPE_BOOLEAN,
		.name    = "Vertical Flip",
		.minimum = 0,
		.maximum = 1,
		.step    = 1,
#define VFLIP_DEFAULT 0
		.default_value = VFLIP_DEFAULT,
	    },
	    .set = sd_setvflip,
	    .get = sd_getvflip,
	},
	{
#define EXPOSURE_IDX 9
	    {
		.id      = V4L2_CID_EXPOSURE,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Exposure",
		.minimum = 0,
		.maximum = 0x1780,
		.step    = 1,
#define EXPOSURE_DEFAULT 0x33
		.default_value = EXPOSURE_DEFAULT,
	    },
	    .set = sd_setexposure,
	    .get = sd_getexposure,
	},
	{
#define GAIN_IDX 10
	    {
		.id      = V4L2_CID_GAIN,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Gain",
		.minimum = 0,
		.maximum = 28,
		.step    = 1,
#define GAIN_DEFAULT 0x00
		.default_value = GAIN_DEFAULT,
	    },
	    .set = sd_setgain,
	    .get = sd_getgain,
	},
	{
#define AUTOGAIN_IDX 11
	    {
		.id      = V4L2_CID_AUTOGAIN,
		.type    = V4L2_CTRL_TYPE_BOOLEAN,
		.name    = "Auto Exposure",
		.minimum = 0,
		.maximum = 1,
		.step    = 1,
#define AUTO_EXPOSURE_DEFAULT 1
		.default_value = AUTO_EXPOSURE_DEFAULT,
	    },
	    .set = sd_setautoexposure,
	    .get = sd_getautoexposure,
	},
};

static const struct v4l2_pix_format vga_mode[] = {
	{160, 120, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 240,
		.sizeimage = 240 * 120,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 0 | MODE_JPEG},
	{160, 120, V4L2_PIX_FMT_SBGGR8, V4L2_FIELD_NONE,
		.bytesperline = 160,
		.sizeimage = 160 * 120,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0 | MODE_RAW},
	{160, 120, V4L2_PIX_FMT_SN9C20X_I420, V4L2_FIELD_NONE,
		.bytesperline = 240,
		.sizeimage = 240 * 120,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0},
	{320, 240, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 480,
		.sizeimage = 480 * 240 ,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 1 | MODE_JPEG},
	{320, 240, V4L2_PIX_FMT_SBGGR8, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240 ,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 1 | MODE_RAW},
	{320, 240, V4L2_PIX_FMT_SN9C20X_I420, V4L2_FIELD_NONE,
		.bytesperline = 480,
		.sizeimage = 480 * 240 ,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 1},
	{640, 480, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 960,
		.sizeimage = 960 * 480,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 2 | MODE_JPEG},
	{640, 480, V4L2_PIX_FMT_SBGGR8, V4L2_FIELD_NONE,
		.bytesperline = 640,
		.sizeimage = 640 * 480,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 2 | MODE_RAW},
	{640, 480, V4L2_PIX_FMT_SN9C20X_I420, V4L2_FIELD_NONE,
		.bytesperline = 960,
		.sizeimage = 960 * 480,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 2},
};

static const struct v4l2_pix_format sxga_mode[] = {
	{160, 120, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 240,
		.sizeimage = 240 * 120,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 0 | MODE_JPEG},
	{160, 120, V4L2_PIX_FMT_SBGGR8, V4L2_FIELD_NONE,
		.bytesperline = 160,
		.sizeimage = 160 * 120,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0 | MODE_RAW},
	{160, 120, V4L2_PIX_FMT_SN9C20X_I420, V4L2_FIELD_NONE,
		.bytesperline = 240,
		.sizeimage = 240 * 120,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0},
	{320, 240, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 480,
		.sizeimage = 480 * 240 ,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 1 | MODE_JPEG},
	{320, 240, V4L2_PIX_FMT_SBGGR8, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240 ,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 1 | MODE_RAW},
	{320, 240, V4L2_PIX_FMT_SN9C20X_I420, V4L2_FIELD_NONE,
		.bytesperline = 480,
		.sizeimage = 480 * 240 ,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 1},
	{640, 480, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 960,
		.sizeimage = 960 * 480,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 2 | MODE_JPEG},
	{640, 480, V4L2_PIX_FMT_SBGGR8, V4L2_FIELD_NONE,
		.bytesperline = 640,
		.sizeimage = 640 * 480,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 2 | MODE_RAW},
	{640, 480, V4L2_PIX_FMT_SN9C20X_I420, V4L2_FIELD_NONE,
		.bytesperline = 960,
		.sizeimage = 960 * 480,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 2},
	{1280, 1024, V4L2_PIX_FMT_SBGGR8, V4L2_FIELD_NONE,
		.bytesperline = 1280,
		.sizeimage = (1280 * 1024) + 64,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 3 | MODE_RAW | MODE_SXGA},
};

static const int hsv_red_x[] = {
	41,  44,  46,  48,  50,  52,  54,  56,
	58,  60,  62,  64,  66,  68,  70,  72,
	74,  76,  78,  80,  81,  83,  85,  87,
	88,  90,  92,  93,  95,  97,  98, 100,
	101, 102, 104, 105, 107, 108, 109, 110,
	112, 113, 114, 115, 116, 117, 118, 119,
	120, 121, 122, 123, 123, 124, 125, 125,
	126, 127, 127, 128, 128, 129, 129, 129,
	130, 130, 130, 130, 131, 131, 131, 131,
	131, 131, 131, 131, 130, 130, 130, 130,
	129, 129, 129, 128, 128, 127, 127, 126,
	125, 125, 124, 123, 122, 122, 121, 120,
	119, 118, 117, 116, 115, 114, 112, 111,
	110, 109, 107, 106, 105, 103, 102, 101,
	99,  98,  96,  94,  93,  91,  90,  88,
	86,  84,  83,  81,  79,  77,  75,  74,
	72,  70,  68,  66,  64,  62,  60,  58,
	56,  54,  52,  49,  47,  45,  43,  41,
	39,  36,  34,  32,  30,  28,  25,  23,
	21,  19,  16,  14,  12,   9,   7,   5,
	3,   0,  -1,  -3,  -6,  -8, -10, -12,
	-15, -17, -19, -22, -24, -26, -28, -30,
	-33, -35, -37, -39, -41, -44, -46, -48,
	-50, -52, -54, -56, -58, -60, -62, -64,
	-66, -68, -70, -72, -74, -76, -78, -80,
	-81, -83, -85, -87, -88, -90, -92, -93,
	-95, -97, -98, -100, -101, -102, -104, -105,
	-107, -108, -109, -110, -112, -113, -114, -115,
	-116, -117, -118, -119, -120, -121, -122, -123,
	-123, -124, -125, -125, -126, -127, -127, -128,
	-128, -128, -128, -128, -128, -128, -128, -128,
	-128, -128, -128, -128, -128, -128, -128, -128,
	-128, -128, -128, -128, -128, -128, -128, -128,
	-128, -127, -127, -126, -125, -125, -124, -123,
	-122, -122, -121, -120, -119, -118, -117, -116,
	-115, -114, -112, -111, -110, -109, -107, -106,
	-105, -103, -102, -101, -99, -98, -96, -94,
	-93, -91, -90, -88, -86, -84, -83, -81,
	-79, -77, -75, -74, -72, -70, -68, -66,
	-64, -62, -60, -58, -56, -54, -52, -49,
	-47, -45, -43, -41, -39, -36, -34, -32,
	-30, -28, -25, -23, -21, -19, -16, -14,
	-12,  -9,  -7,  -5,  -3,   0,   1,   3,
	6,   8,  10,  12,  15,  17,  19,  22,
	24,  26,  28,  30,  33,  35,  37,  39, 41
};

static const int hsv_red_y[] = {
	82,  80,  78,  76,  74,  73,  71,  69,
	67,  65,  63,  61,  58,  56,  54,  52,
	50,  48,  46,  44,  41,  39,  37,  35,
	32,  30,  28,  26,  23,  21,  19,  16,
	14,  12,  10,   7,   5,   3,   0,  -1,
	-3,  -6,  -8, -10, -13, -15, -17, -19,
	-22, -24, -26, -29, -31, -33, -35, -38,
	-40, -42, -44, -46, -48, -51, -53, -55,
	-57, -59, -61, -63, -65, -67, -69, -71,
	-73, -75, -77, -79, -81, -82, -84, -86,
	-88, -89, -91, -93, -94, -96, -98, -99,
	-101, -102, -104, -105, -106, -108, -109, -110,
	-112, -113, -114, -115, -116, -117, -119, -120,
	-120, -121, -122, -123, -124, -125, -126, -126,
	-127, -128, -128, -128, -128, -128, -128, -128,
	-128, -128, -128, -128, -128, -128, -128, -128,
	-128, -128, -128, -128, -128, -128, -128, -128,
	-128, -128, -128, -128, -128, -128, -128, -128,
	-127, -127, -126, -125, -125, -124, -123, -122,
	-121, -120, -119, -118, -117, -116, -115, -114,
	-113, -111, -110, -109, -107, -106, -105, -103,
	-102, -100, -99, -97, -96, -94, -92, -91,
	-89, -87, -85, -84, -82, -80, -78, -76,
	-74, -73, -71, -69, -67, -65, -63, -61,
	-58, -56, -54, -52, -50, -48, -46, -44,
	-41, -39, -37, -35, -32, -30, -28, -26,
	-23, -21, -19, -16, -14, -12, -10,  -7,
	-5,  -3,   0,   1,   3,   6,   8,  10,
	13,  15,  17,  19,  22,  24,  26,  29,
	31,  33,  35,  38,  40,  42,  44,  46,
	48,  51,  53,  55,  57,  59,  61,  63,
	65,  67,  69,  71,  73,  75,  77,  79,
	81,  82,  84,  86,  88,  89,  91,  93,
	94,  96,  98,  99, 101, 102, 104, 105,
	106, 108, 109, 110, 112, 113, 114, 115,
	116, 117, 119, 120, 120, 121, 122, 123,
	124, 125, 126, 126, 127, 128, 128, 129,
	129, 130, 130, 131, 131, 131, 131, 132,
	132, 132, 132, 132, 132, 132, 132, 132,
	132, 132, 132, 131, 131, 131, 130, 130,
	130, 129, 129, 128, 127, 127, 126, 125,
	125, 124, 123, 122, 121, 120, 119, 118,
	117, 116, 115, 114, 113, 111, 110, 109,
	107, 106, 105, 103, 102, 100,  99,  97,
	96, 94, 92, 91, 89, 87, 85, 84, 82
};

static const int hsv_green_x[] = {
	-124, -124, -125, -125, -125, -125, -125, -125,
	-125, -126, -126, -125, -125, -125, -125, -125,
	-125, -124, -124, -124, -123, -123, -122, -122,
	-121, -121, -120, -120, -119, -118, -117, -117,
	-116, -115, -114, -113, -112, -111, -110, -109,
	-108, -107, -105, -104, -103, -102, -100, -99,
	-98, -96, -95, -93, -92, -91, -89, -87,
	-86, -84, -83, -81, -79, -77, -76, -74,
	-72, -70, -69, -67, -65, -63, -61, -59,
	-57, -55, -53, -51, -49, -47, -45, -43,
	-41, -39, -37, -35, -33, -30, -28, -26,
	-24, -22, -20, -18, -15, -13, -11,  -9,
	-7,  -4,  -2,   0,   1,   3,   6,   8,
	10,  12,  14,  17,  19,  21,  23,  25,
	27,  29,  32,  34,  36,  38,  40,  42,
	44,  46,  48,  50,  52,  54,  56,  58,
	60,  62,  64,  66,  68,  70,  71,  73,
	75,  77,  78,  80,  82,  83,  85,  87,
	88,  90,  91,  93,  94,  96,  97,  98,
	100, 101, 102, 104, 105, 106, 107, 108,
	109, 111, 112, 113, 113, 114, 115, 116,
	117, 118, 118, 119, 120, 120, 121, 122,
	122, 123, 123, 124, 124, 124, 125, 125,
	125, 125, 125, 125, 125, 126, 126, 125,
	125, 125, 125, 125, 125, 124, 124, 124,
	123, 123, 122, 122, 121, 121, 120, 120,
	119, 118, 117, 117, 116, 115, 114, 113,
	112, 111, 110, 109, 108, 107, 105, 104,
	103, 102, 100,  99,  98,  96,  95,  93,
	92,  91,  89,  87,  86,  84,  83,  81,
	79,  77,  76,  74,  72,  70,  69,  67,
	65,  63,  61,  59,  57,  55,  53,  51,
	49,  47,  45,  43,  41,  39,  37,  35,
	33,  30,  28,  26,  24,  22,  20,  18,
	15,  13,  11,   9,   7,   4,   2,   0,
	-1,  -3,  -6,  -8, -10, -12, -14, -17,
	-19, -21, -23, -25, -27, -29, -32, -34,
	-36, -38, -40, -42, -44, -46, -48, -50,
	-52, -54, -56, -58, -60, -62, -64, -66,
	-68, -70, -71, -73, -75, -77, -78, -80,
	-82, -83, -85, -87, -88, -90, -91, -93,
	-94, -96, -97, -98, -100, -101, -102, -104,
	-105, -106, -107, -108, -109, -111, -112, -113,
	-113, -114, -115, -116, -117, -118, -118, -119,
	-120, -120, -121, -122, -122, -123, -123, -124, -124
};

static const int hsv_green_y[] = {
	-100, -99, -98, -97, -95, -94, -93, -91,
	-90, -89, -87, -86, -84, -83, -81, -80,
	-78, -76, -75, -73, -71, -70, -68, -66,
	-64, -63, -61, -59, -57, -55, -53, -51,
	-49, -48, -46, -44, -42, -40, -38, -36,
	-34, -32, -30, -27, -25, -23, -21, -19,
	-17, -15, -13, -11,  -9,  -7,  -4,  -2,
	0,   1,   3,   5,   7,   9,  11,  14,
	16,  18,  20,  22,  24,  26,  28,  30,
	32,  34,  36,  38,  40,  42,  44,  46,
	48,  50,  52,  54,  56,  58,  59,  61,
	63,  65,  67,  68,  70,  72,  74,  75,
	77,  78,  80,  82,  83,  85,  86,  88,
	89,  90,  92,  93,  95,  96,  97,  98,
	100, 101, 102, 103, 104, 105, 106, 107,
	108, 109, 110, 111, 112, 112, 113, 114,
	115, 115, 116, 116, 117, 117, 118, 118,
	119, 119, 119, 120, 120, 120, 120, 120,
	121, 121, 121, 121, 121, 121, 120, 120,
	120, 120, 120, 119, 119, 119, 118, 118,
	117, 117, 116, 116, 115, 114, 114, 113,
	112, 111, 111, 110, 109, 108, 107, 106,
	105, 104, 103, 102, 100,  99,  98,  97,
	95,  94,  93,  91,  90,  89,  87,  86,
	84,  83,  81,  80,  78,  76,  75,  73,
	71,  70,  68,  66,  64,  63,  61,  59,
	57,  55,  53,  51,  49,  48,  46,  44,
	42,  40,  38,  36,  34,  32,  30,  27,
	25,  23,  21,  19,  17,  15,  13,  11,
	9,   7,   4,   2,   0,  -1,  -3,  -5,
	-7,  -9, -11, -14, -16, -18, -20, -22,
	-24, -26, -28, -30, -32, -34, -36, -38,
	-40, -42, -44, -46, -48, -50, -52, -54,
	-56, -58, -59, -61, -63, -65, -67, -68,
	-70, -72, -74, -75, -77, -78, -80, -82,
	-83, -85, -86, -88, -89, -90, -92, -93,
	-95, -96, -97, -98, -100, -101, -102, -103,
	-104, -105, -106, -107, -108, -109, -110, -111,
	-112, -112, -113, -114, -115, -115, -116, -116,
	-117, -117, -118, -118, -119, -119, -119, -120,
	-120, -120, -120, -120, -121, -121, -121, -121,
	-121, -121, -120, -120, -120, -120, -120, -119,
	-119, -119, -118, -118, -117, -117, -116, -116,
	-115, -114, -114, -113, -112, -111, -111, -110,
	-109, -108, -107, -106, -105, -104, -103, -102, -100
};

static const int hsv_blue_x[] = {
	112, 113, 114, 114, 115, 116, 117, 117,
	118, 118, 119, 119, 120, 120, 120, 121,
	121, 121, 122, 122, 122, 122, 122, 122,
	122, 122, 122, 122, 122, 122, 121, 121,
	121, 120, 120, 120, 119, 119, 118, 118,
	117, 116, 116, 115, 114, 113, 113, 112,
	111, 110, 109, 108, 107, 106, 105, 104,
	103, 102, 100,  99,  98,  97,  95,  94,
	93,  91,  90,  88,  87,  85,  84,  82,
	80,  79,  77,  76,  74,  72,  70,  69,
	67,  65,  63,  61,  60,  58,  56,  54,
	52,  50,  48,  46,  44,  42,  40,  38,
	36,  34,  32,  30,  28,  26,  24,  22,
	19,  17,  15,  13,  11,   9,   7,   5,
	2,   0,  -1,  -3,  -5,  -7,  -9, -12,
	-14, -16, -18, -20, -22, -24, -26, -28,
	-31, -33, -35, -37, -39, -41, -43, -45,
	-47, -49, -51, -53, -54, -56, -58, -60,
	-62, -64, -66, -67, -69, -71, -73, -74,
	-76, -78, -79, -81, -83, -84, -86, -87,
	-89, -90, -92, -93, -94, -96, -97, -98,
	-99, -101, -102, -103, -104, -105, -106, -107,
	-108, -109, -110, -111, -112, -113, -114, -114,
	-115, -116, -117, -117, -118, -118, -119, -119,
	-120, -120, -120, -121, -121, -121, -122, -122,
	-122, -122, -122, -122, -122, -122, -122, -122,
	-122, -122, -121, -121, -121, -120, -120, -120,
	-119, -119, -118, -118, -117, -116, -116, -115,
	-114, -113, -113, -112, -111, -110, -109, -108,
	-107, -106, -105, -104, -103, -102, -100, -99,
	-98, -97, -95, -94, -93, -91, -90, -88,
	-87, -85, -84, -82, -80, -79, -77, -76,
	-74, -72, -70, -69, -67, -65, -63, -61,
	-60, -58, -56, -54, -52, -50, -48, -46,
	-44, -42, -40, -38, -36, -34, -32, -30,
	-28, -26, -24, -22, -19, -17, -15, -13,
	-11,  -9,  -7,  -5,  -2,   0,   1,   3,
	5,   7,   9,  12,  14,  16,  18,  20,
	22,  24,  26,  28,  31,  33,  35,  37,
	39,  41,  43,  45,  47,  49,  51,  53,
	54,  56,  58,  60,  62,  64,  66,  67,
	69,  71,  73,  74,  76,  78,  79,  81,
	83,  84,  86,  87,  89,  90,  92,  93,
	94,  96,  97,  98,  99, 101, 102, 103,
	104, 105, 106, 107, 108, 109, 110, 111, 112
};

static const int hsv_blue_y[] = {
	-11, -13, -15, -17, -19, -21, -23, -25,
	-27, -29, -31, -33, -35, -37, -39, -41,
	-43, -45, -46, -48, -50, -52, -54, -55,
	-57, -59, -61, -62, -64, -66, -67, -69,
	-71, -72, -74, -75, -77, -78, -80, -81,
	-83, -84, -86, -87, -88, -90, -91, -92,
	-93, -95, -96, -97, -98, -99, -100, -101,
	-102, -103, -104, -105, -106, -106, -107, -108,
	-109, -109, -110, -111, -111, -112, -112, -113,
	-113, -114, -114, -114, -115, -115, -115, -115,
	-116, -116, -116, -116, -116, -116, -116, -116,
	-116, -115, -115, -115, -115, -114, -114, -114,
	-113, -113, -112, -112, -111, -111, -110, -110,
	-109, -108, -108, -107, -106, -105, -104, -103,
	-102, -101, -100, -99, -98, -97, -96, -95,
	-94, -93, -91, -90, -89, -88, -86, -85,
	-84, -82, -81, -79, -78, -76, -75, -73,
	-71, -70, -68, -67, -65, -63, -62, -60,
	-58, -56, -55, -53, -51, -49, -47, -45,
	-44, -42, -40, -38, -36, -34, -32, -30,
	-28, -26, -24, -22, -20, -18, -16, -14,
	-12, -10,  -8,  -6,  -4,  -2,   0,   1,
	3,   5,   7,   9,  11,  13,  15,  17,
	19,  21,  23,  25,  27,  29,  31,  33,
	35,  37,  39,  41,  43,  45,  46,  48,
	50,  52,  54,  55,  57,  59,  61,  62,
	64,  66,  67,  69,  71,  72,  74,  75,
	77,  78,  80,  81,  83,  84,  86,  87,
	88,  90,  91,  92,  93,  95,  96,  97,
	98,  99, 100, 101, 102, 103, 104, 105,
	106, 106, 107, 108, 109, 109, 110, 111,
	111, 112, 112, 113, 113, 114, 114, 114,
	115, 115, 115, 115, 116, 116, 116, 116,
	116, 116, 116, 116, 116, 115, 115, 115,
	115, 114, 114, 114, 113, 113, 112, 112,
	111, 111, 110, 110, 109, 108, 108, 107,
	106, 105, 104, 103, 102, 101, 100,  99,
	98,  97,  96,  95,  94,  93,  91,  90,
	89,  88,  86,  85,  84,  82,  81,  79,
	78,  76,  75,  73,  71,  70,  68,  67,
	65,  63,  62,  60,  58,  56,  55,  53,
	51,  49,  47,  45,  44,  42,  40,  38,
	36,  34,  32,  30,  28,  26,  24,  22,
	20,  18,  16,  14,  12,  10,   8,   6,
	4,   2,   0,  -1,  -3,  -5,  -7,  -9, -11
};

static u16 i2c_ident[] = {
	V4L2_IDENT_OV9650,
	V4L2_IDENT_OV9655,
	V4L2_IDENT_SOI968,
	V4L2_IDENT_OV7660,
	V4L2_IDENT_OV7670,
	V4L2_IDENT_MT9V011,
	V4L2_IDENT_MT9V111,
	V4L2_IDENT_MT9V112,
	V4L2_IDENT_MT9M001C12ST,
	V4L2_IDENT_MT9M111,
	V4L2_IDENT_HV7131R,
};

static u16 bridge_init[][2] = {
	{0x1000, 0x78}, {0x1001, 0x40}, {0x1002, 0x1c},
	{0x1020, 0x80}, {0x1061, 0x01}, {0x1067, 0x40},
	{0x1068, 0x30}, {0x1069, 0x20},	{0x106a, 0x10},
	{0x106b, 0x08},	{0x1188, 0x87},	{0x11a1, 0x00},
	{0x11a2, 0x00},	{0x11a3, 0x6a},	{0x11a4, 0x50},
	{0x11ab, 0x00},	{0x11ac, 0x00},	{0x11ad, 0x50},
	{0x11ae, 0x3c},	{0x118a, 0x04},	{0x0395, 0x04},
	{0x11b8, 0x3a},	{0x118b, 0x0e},	{0x10f7, 0x05},
	{0x10f8, 0x14},	{0x10fa, 0xff},	{0x10f9, 0x00},
	{0x11ba, 0x0a},	{0x11a5, 0x2d},	{0x11a6, 0x2d},
	{0x11a7, 0x3a},	{0x11a8, 0x05},	{0x11a9, 0x04},
	{0x11aa, 0x3f},	{0x11af, 0x28},	{0x11b0, 0xd8},
	{0x11b1, 0x14},	{0x11b2, 0xec},	{0x11b3, 0x32},
	{0x11b4, 0xdd},	{0x11b5, 0x32},	{0x11b6, 0xdd},
	{0x10e0, 0x2c},	{0x11bc, 0x40},	{0x11bd, 0x01},
	{0x11be, 0xf0},	{0x11bf, 0x00},	{0x118c, 0x1f},
	{0x118d, 0x1f},	{0x118e, 0x1f},	{0x118f, 0x1f},
	{0x1180, 0x01},	{0x1181, 0x00},	{0x1182, 0x01},
	{0x1183, 0x00},	{0x1184, 0x50},	{0x1185, 0x80}
};

/* Gain = (bit[3:0] / 16 + 1) * (bit[4] + 1) * (bit[5] + 1) * (bit[6] + 1) */
static u8 ov_gain[] = {
	0x00 /* 1x */, 0x04 /* 1.25x */, 0x08 /* 1.5x */, 0x0c /* 1.75x */,
	0x10 /* 2x */, 0x12 /* 2.25x */, 0x14 /* 2.5x */, 0x16 /* 2.75x */,
	0x18 /* 3x */, 0x1a /* 3.25x */, 0x1c /* 3.5x */, 0x1e /* 3.75x */,
	0x30 /* 4x */, 0x31 /* 4.25x */, 0x32 /* 4.5x */, 0x33 /* 4.75x */,
	0x34 /* 5x */, 0x35 /* 5.25x */, 0x36 /* 5.5x */, 0x37 /* 5.75x */,
	0x38 /* 6x */, 0x39 /* 6.25x */, 0x3a /* 6.5x */, 0x3b /* 6.75x */,
	0x3c /* 7x */, 0x3d /* 7.25x */, 0x3e /* 7.5x */, 0x3f /* 7.75x */,
	0x70 /* 8x */
};

/* Gain = (bit[8] + 1) * (bit[7] + 1) * (bit[6:0] * 0.03125) */
static u16 micron1_gain[] = {
	/* 1x   1.25x   1.5x    1.75x */
	0x0020, 0x0028, 0x0030, 0x0038,
	/* 2x   2.25x   2.5x    2.75x */
	0x00a0, 0x00a4, 0x00a8, 0x00ac,
	/* 3x   3.25x   3.5x    3.75x */
	0x00b0, 0x00b4, 0x00b8, 0x00bc,
	/* 4x   4.25x   4.5x    4.75x */
	0x00c0, 0x00c4, 0x00c8, 0x00cc,
	/* 5x   5.25x   5.5x    5.75x */
	0x00d0, 0x00d4, 0x00d8, 0x00dc,
	/* 6x   6.25x   6.5x    6.75x */
	0x00e0, 0x00e4, 0x00e8, 0x00ec,
	/* 7x   7.25x   7.5x    7.75x */
	0x00f0, 0x00f4, 0x00f8, 0x00fc,
	/* 8x */
	0x01c0
};

/* mt9m001 sensor uses a different gain formula then other micron sensors */
/* Gain = (bit[6] + 1) * (bit[5-0] * 0.125) */
static u16 micron2_gain[] = {
	/* 1x   1.25x   1.5x    1.75x */
	0x0008, 0x000a, 0x000c, 0x000e,
	/* 2x   2.25x   2.5x    2.75x */
	0x0010, 0x0012, 0x0014, 0x0016,
	/* 3x   3.25x   3.5x    3.75x */
	0x0018, 0x001a, 0x001c, 0x001e,
	/* 4x   4.25x   4.5x    4.75x */
	0x0020, 0x0051, 0x0052, 0x0053,
	/* 5x   5.25x   5.5x    5.75x */
	0x0054, 0x0055, 0x0056, 0x0057,
	/* 6x   6.25x   6.5x    6.75x */
	0x0058, 0x0059, 0x005a, 0x005b,
	/* 7x   7.25x   7.5x    7.75x */
	0x005c, 0x005d, 0x005e, 0x005f,
	/* 8x */
	0x0060
};

/* Gain = .5 + bit[7:0] / 16 */
static u8 hv7131r_gain[] = {
	0x08 /* 1x */, 0x0c /* 1.25x */, 0x10 /* 1.5x */, 0x14 /* 1.75x */,
	0x18 /* 2x */, 0x1c /* 2.25x */, 0x20 /* 2.5x */, 0x24 /* 2.75x */,
	0x28 /* 3x */, 0x2c /* 3.25x */, 0x30 /* 3.5x */, 0x34 /* 3.75x */,
	0x38 /* 4x */, 0x3c /* 4.25x */, 0x40 /* 4.5x */, 0x44 /* 4.75x */,
	0x48 /* 5x */, 0x4c /* 5.25x */, 0x50 /* 5.5x */, 0x54 /* 5.75x */,
	0x58 /* 6x */, 0x5c /* 6.25x */, 0x60 /* 6.5x */, 0x64 /* 6.75x */,
	0x68 /* 7x */, 0x6c /* 7.25x */, 0x70 /* 7.5x */, 0x74 /* 7.75x */,
	0x78 /* 8x */
};

static u8 soi968_init[][2] = {
	{0x12, 0x80}, {0x0c, 0x00}, {0x0f, 0x1f},
	{0x11, 0x80}, {0x38, 0x52}, {0x1e, 0x00},
	{0x33, 0x08}, {0x35, 0x8c}, {0x36, 0x0c},
	{0x37, 0x04}, {0x45, 0x04}, {0x47, 0xff},
	{0x3e, 0x00}, {0x3f, 0x00}, {0x3b, 0x20},
	{0x3a, 0x96}, {0x3d, 0x0a}, {0x14, 0x8e},
	{0x13, 0x8a}, {0x12, 0x40}, {0x17, 0x13},
	{0x18, 0x63}, {0x19, 0x01}, {0x1a, 0x79},
	{0x32, 0x24}, {0x03, 0x00}, {0x11, 0x40},
	{0x2a, 0x10}, {0x2b, 0xe0}, {0x10, 0x32},
	{0x00, 0x00}, {0x01, 0x80}, {0x02, 0x80},
};

static u8 ov7660_init[][2] = {
	{0x0e, 0x80}, {0x0d, 0x08}, {0x0f, 0xc3},
	{0x04, 0xc3}, {0x10, 0x40}, {0x11, 0x40},
	{0x12, 0x05}, {0x13, 0xba}, {0x14, 0x2a},
	{0x37, 0x0f}, {0x38, 0x02}, {0x39, 0x43},
	{0x3a, 0x00}, {0x69, 0x90}, {0x2d, 0xf6},
	{0x2e, 0x0b}, {0x01, 0x78}, {0x02, 0x50},
};

static u8 ov7670_init[][2] = {
	{0x12, 0x80}, {0x11, 0x80}, {0x3a, 0x04}, {0x12, 0x01},
	{0x32, 0xb6}, {0x03, 0x0a}, {0x0c, 0x00}, {0x3e, 0x00},
	{0x70, 0x3a}, {0x71, 0x35}, {0x72, 0x11}, {0x73, 0xf0},
	{0xa2, 0x02}, {0x13, 0xe0}, {0x00, 0x00}, {0x10, 0x00},
	{0x0d, 0x40}, {0x14, 0x28}, {0xa5, 0x05}, {0xab, 0x07},
	{0x24, 0x95}, {0x25, 0x33}, {0x26, 0xe3}, {0x9f, 0x75},
	{0xa0, 0x65}, {0xa1, 0x0b}, {0xa6, 0xd8}, {0xa7, 0xd8},
	{0xa8, 0xf0}, {0xa9, 0x90}, {0xaa, 0x94}, {0x13, 0xe5},
	{0x0e, 0x61}, {0x0f, 0x4b}, {0x16, 0x02}, {0x1e, 0x27},
	{0x21, 0x02}, {0x22, 0x91}, {0x29, 0x07}, {0x33, 0x0b},
	{0x35, 0x0b}, {0x37, 0x1d}, {0x38, 0x71}, {0x39, 0x2a},
	{0x3c, 0x78}, {0x4d, 0x40}, {0x4e, 0x20}, {0x69, 0x00},
	{0x74, 0x19}, {0x8d, 0x4f}, {0x8e, 0x00}, {0x8f, 0x00},
	{0x90, 0x00}, {0x91, 0x00}, {0x96, 0x00}, {0x9a, 0x80},
	{0xb0, 0x84}, {0xb1, 0x0c}, {0xb2, 0x0e}, {0xb3, 0x82},
	{0xb8, 0x0a}, {0x43, 0x0a}, {0x44, 0xf0}, {0x45, 0x20},
	{0x46, 0x7d}, {0x47, 0x29}, {0x48, 0x4a}, {0x59, 0x8c},
	{0x5a, 0xa5}, {0x5b, 0xde}, {0x5c, 0x96}, {0x5d, 0x66},
	{0x5e, 0x10}, {0x6c, 0x0a}, {0x6d, 0x55}, {0x6e, 0x11},
	{0x6f, 0x9e}, {0x6a, 0x40}, {0x01, 0x40}, {0x02, 0x40},
	{0x13, 0xe7}, {0x4f, 0x6e}, {0x50, 0x70}, {0x51, 0x02},
	{0x52, 0x1d}, {0x53, 0x56}, {0x54, 0x73}, {0x55, 0x0a},
	{0x56, 0x55}, {0x57, 0x80}, {0x58, 0x9e}, {0x41, 0x08},
	{0x3f, 0x02}, {0x75, 0x03}, {0x76, 0x63}, {0x4c, 0x04},
	{0x77, 0x06}, {0x3d, 0x02}, {0x4b, 0x09}, {0xc9, 0x30},
	{0x41, 0x08}, {0x56, 0x48}, {0x34, 0x11}, {0xa4, 0x88},
	{0x96, 0x00}, {0x97, 0x30}, {0x98, 0x20}, {0x99, 0x30},
	{0x9a, 0x84}, {0x9b, 0x29}, {0x9c, 0x03}, {0x9d, 0x99},
	{0x9e, 0x7f}, {0x78, 0x04}, {0x79, 0x01}, {0xc8, 0xf0},
	{0x79, 0x0f}, {0xc8, 0x00}, {0x79, 0x10}, {0xc8, 0x7e},
	{0x79, 0x0a}, {0xc8, 0x80}, {0x79, 0x0b}, {0xc8, 0x01},
	{0x79, 0x0c}, {0xc8, 0x0f}, {0x79, 0x0d}, {0xc8, 0x20},
	{0x79, 0x09}, {0xc8, 0x80}, {0x79, 0x02}, {0xc8, 0xc0},
	{0x79, 0x03}, {0xc8, 0x40}, {0x79, 0x05}, {0xc8, 0x30},
	{0x79, 0x26}, {0x62, 0x20}, {0x63, 0x00}, {0x64, 0x06},
	{0x65, 0x00}, {0x66, 0x05}, {0x94, 0x05}, {0x95, 0x0a},
	{0x17, 0x13}, {0x18, 0x01}, {0x19, 0x02}, {0x1a, 0x7a},
	{0x46, 0x59}, {0x47, 0x30}, {0x58, 0x9a}, {0x59, 0x84},
	{0x5a, 0x91}, {0x5b, 0x57}, {0x5c, 0x75}, {0x5d, 0x6d},
	{0x5e, 0x13}, {0x64, 0x07}, {0x94, 0x07}, {0x95, 0x0d},
	{0xa6, 0xdf}, {0xa7, 0xdf}, {0x48, 0x4d}, {0x51, 0x00},
	{0x6b, 0x0a}, {0x11, 0x80}, {0x2a, 0x00}, {0x2b, 0x00},
	{0x92, 0x00}, {0x93, 0x00}, {0x55, 0x0a}, {0x56, 0x60},
	{0x4f, 0x6e}, {0x50, 0x70}, {0x51, 0x00}, {0x52, 0x1d},
	{0x53, 0x56}, {0x54, 0x73}, {0x58, 0x9a}, {0x4f, 0x6e},
	{0x50, 0x70}, {0x51, 0x00}, {0x52, 0x1d}, {0x53, 0x56},
	{0x54, 0x73}, {0x58, 0x9a}, {0x3f, 0x01}, {0x7b, 0x03},
	{0x7c, 0x09}, {0x7d, 0x16}, {0x7e, 0x38}, {0x7f, 0x47},
	{0x80, 0x53}, {0x81, 0x5e}, {0x82, 0x6a}, {0x83, 0x74},
	{0x84, 0x80}, {0x85, 0x8c}, {0x86, 0x9b}, {0x87, 0xb2},
	{0x88, 0xcc}, {0x89, 0xe5}, {0x7a, 0x24}, {0x3b, 0x00},
	{0x9f, 0x76}, {0xa0, 0x65}, {0x13, 0xe2}, {0x6b, 0x0a},
	{0x11, 0x80}, {0x2a, 0x00}, {0x2b, 0x00}, {0x92, 0x00},
	{0x93, 0x00},
};

static u8 ov9650_init[][2] = {
	{0x12, 0x80}, {0x00, 0x00}, {0x01, 0x78},
	{0x02, 0x78}, {0x03, 0x36}, {0x04, 0x03},
	{0x05, 0x00}, {0x06, 0x00}, {0x08, 0x00},
	{0x09, 0x01}, {0x0c, 0x00}, {0x0d, 0x00},
	{0x0e, 0xa0}, {0x0f, 0x52}, {0x10, 0x7c},
	{0x11, 0x80}, {0x12, 0x45}, {0x13, 0xc2},
	{0x14, 0x2e}, {0x15, 0x00}, {0x16, 0x07},
	{0x17, 0x24}, {0x18, 0xc5}, {0x19, 0x00},
	{0x1a, 0x3c}, {0x1b, 0x00}, {0x1e, 0x04},
	{0x1f, 0x00}, {0x24, 0x78}, {0x25, 0x68},
	{0x26, 0xd4}, {0x27, 0x80}, {0x28, 0x80},
	{0x29, 0x30}, {0x2a, 0x00}, {0x2b, 0x00},
	{0x2c, 0x80}, {0x2d, 0x00}, {0x2e, 0x00},
	{0x2f, 0x00}, {0x30, 0x08}, {0x31, 0x30},
	{0x32, 0x84}, {0x33, 0xe2}, {0x34, 0xbf},
	{0x35, 0x81}, {0x36, 0xf9}, {0x37, 0x00},
	{0x38, 0x93}, {0x39, 0x50}, {0x3a, 0x01},
	{0x3b, 0x01}, {0x3c, 0x73}, {0x3d, 0x19},
	{0x3e, 0x0b}, {0x3f, 0x80}, {0x40, 0xc1},
	{0x41, 0x00}, {0x42, 0x08}, {0x67, 0x80},
	{0x68, 0x80}, {0x69, 0x40}, {0x6a, 0x00},
	{0x6b, 0x0a}, {0x8b, 0x06}, {0x8c, 0x20},
	{0x8d, 0x00}, {0x8e, 0x00}, {0x8f, 0xdf},
	{0x92, 0x00}, {0x93, 0x00}, {0x94, 0x88},
	{0x95, 0x88}, {0x96, 0x04}, {0xa1, 0x00},
	{0xa5, 0x80}, {0xa8, 0x80}, {0xa9, 0xb8},
	{0xaa, 0x92}, {0xab, 0x0a},
};

static u8 ov9655_init[][2] = {
	{0x12, 0x80}, {0x12, 0x01}, {0x0d, 0x00}, {0x0e, 0x61},
	{0x11, 0x80}, {0x13, 0xba}, {0x14, 0x2e}, {0x16, 0x24},
	{0x1e, 0x04}, {0x1e, 0x04}, {0x1e, 0x04}, {0x27, 0x08},
	{0x28, 0x08}, {0x29, 0x15}, {0x2c, 0x08}, {0x32, 0xbf},
	{0x34, 0x3d}, {0x35, 0x00}, {0x36, 0xf8}, {0x38, 0x12},
	{0x39, 0x57}, {0x3a, 0x00}, {0x3b, 0xcc}, {0x3c, 0x0c},
	{0x3d, 0x19}, {0x3e, 0x0c}, {0x3f, 0x01}, {0x41, 0x40},
	{0x42, 0x80}, {0x45, 0x46}, {0x46, 0x62}, {0x47, 0x2a},
	{0x48, 0x3c}, {0x4a, 0xf0}, {0x4b, 0xdc}, {0x4c, 0xdc},
	{0x4d, 0xdc}, {0x4e, 0xdc}, {0x69, 0x02}, {0x6c, 0x04},
	{0x6f, 0x9e}, {0x70, 0x05}, {0x71, 0x78}, {0x77, 0x02},
	{0x8a, 0x23}, {0x8c, 0x0d}, {0x90, 0x7e}, {0x91, 0x7c},
	{0x9f, 0x6e}, {0xa0, 0x6e}, {0xa5, 0x68}, {0xa6, 0x60},
	{0xa8, 0xc1}, {0xa9, 0xfa}, {0xaa, 0x92}, {0xab, 0x04},
	{0xac, 0x80}, {0xad, 0x80}, {0xae, 0x80}, {0xaf, 0x80},
	{0xb2, 0xf2}, {0xb3, 0x20}, {0xb5, 0x00}, {0xb6, 0xaf},
	{0xbb, 0xae}, {0xbc, 0x44}, {0xbd, 0x44}, {0xbe, 0x3b},
	{0xbf, 0x3a}, {0xc0, 0xe2}, {0xc1, 0xc8}, {0xc2, 0x01},
	{0xc4, 0x00}, {0xc6, 0x85}, {0xc7, 0x81}, {0xc9, 0xe0},
	{0xca, 0xe8}, {0xcc, 0xd8}, {0xcd, 0x93}, {0x12, 0x61},
	{0x36, 0xfa}, {0x8c, 0x8d}, {0xc0, 0xaa}, {0x69, 0x0a},
	{0x03, 0x12}, {0x17, 0x14}, {0x18, 0x00}, {0x19, 0x01},
	{0x1a, 0x3d}, {0x32, 0xbf}, {0x11, 0x80}, {0x2a, 0x10},
	{0x2b, 0x0a}, {0x92, 0x00}, {0x93, 0x00}, {0x1e, 0x04},
	{0x1e, 0x04}, {0x10, 0x7c}, {0x04, 0x03}, {0xa1, 0x00},
	{0x2d, 0x00}, {0x2e, 0x00}, {0x00, 0x00}, {0x01, 0x80},
	{0x02, 0x80}, {0x12, 0x61}, {0x36, 0xfa}, {0x8c, 0x8d},
	{0xc0, 0xaa}, {0x69, 0x0a}, {0x03, 0x12}, {0x17, 0x14},
	{0x18, 0x00}, {0x19, 0x01}, {0x1a, 0x3d}, {0x32, 0xbf},
	{0x11, 0x80}, {0x2a, 0x10}, {0x2b, 0x0a}, {0x92, 0x00},
	{0x93, 0x00}, {0x04, 0x01}, {0x10, 0x1f}, {0xa1, 0x00},
	{0x00, 0x0a}, {0xa1, 0x00}, {0x10, 0x5d}, {0x04, 0x03},
	{0x00, 0x01}, {0xa1, 0x00}, {0x10, 0x7c}, {0x04, 0x03},
	{0x00, 0x03}, {0x00, 0x0a}, {0x00, 0x10}, {0x00, 0x13},
};

static u16 mt9v112_init[][2] = {
	{0xf0, 0x0000}, {0x0d, 0x0021}, {0x0d, 0x0020},
	{0x34, 0xc019}, {0x0a, 0x0011}, {0x0b, 0x000b},
	{0x20, 0x0703}, {0x35, 0x2022}, {0xf0, 0x0001},
	{0x05, 0x0000}, {0x06, 0x340c}, {0x3b, 0x042a},
	{0x3c, 0x0400}, {0xf0, 0x0002}, {0x2e, 0x0c58},
	{0x5b, 0x0001}, {0xc8, 0x9f0b}, {0xf0, 0x0001},
	{0x9b, 0x5300}, {0xf0, 0x0000}, {0x2b, 0x0020},
	{0x2c, 0x002a}, {0x2d, 0x0032}, {0x2e, 0x0020},
	{0x09, 0x01dc}, {0x01, 0x000c}, {0x02, 0x0020},
	{0x03, 0x01e0}, {0x04, 0x0280}, {0x06, 0x000c},
	{0x05, 0x0098}, {0x20, 0x0703}, {0x09, 0x01f2},
	{0x2b, 0x00a0}, {0x2c, 0x00a0}, {0x2d, 0x00a0},
	{0x2e, 0x00a0}, {0x01, 0x000c}, {0x02, 0x0020},
	{0x03, 0x01e0}, {0x04, 0x0280}, {0x06, 0x000c},
	{0x05, 0x0098}, {0x09, 0x01c1}, {0x2b, 0x00ae},
	{0x2c, 0x00ae}, {0x2d, 0x00ae}, {0x2e, 0x00ae},
};

static u16 mt9v111_init[][2] = {
	{0x01, 0x0004}, {0x0d, 0x0001}, {0x0d, 0x0000},
	{0x01, 0x0001}, {0x02, 0x0016}, {0x03, 0x01e1},
	{0x04, 0x0281}, {0x05, 0x0004}, {0x07, 0x3002},
	{0x21, 0x0000}, {0x25, 0x4024}, {0x26, 0xff03},
	{0x27, 0xff10}, {0x2b, 0x7828}, {0x2c, 0xb43c},
	{0x2d, 0xf0a0},	{0x2e, 0x0c64},	{0x2f, 0x0064},
	{0x67, 0x4010},	{0x06, 0x301e},	{0x08, 0x0480},
	{0x01, 0x0004},	{0x02, 0x0016}, {0x03, 0x01e6},
	{0x04, 0x0286},	{0x05, 0x0004}, {0x06, 0x0000},
	{0x07, 0x3002},	{0x08, 0x0008}, {0x0c, 0x0000},
	{0x0d, 0x0000}, {0x0e, 0x0000}, {0x0f, 0x0000},
	{0x10, 0x0000},	{0x11, 0x0000},	{0x12, 0x00b0},
	{0x13, 0x007c},	{0x14, 0x0000}, {0x15, 0x0000},
	{0x16, 0x0000}, {0x17, 0x0000},	{0x18, 0x0000},
	{0x19, 0x0000},	{0x1a, 0x0000},	{0x1b, 0x0000},
	{0x1c, 0x0000},	{0x1d, 0x0000},	{0x30, 0x0000},
	{0x30, 0x0005},	{0x31, 0x0000},	{0x02, 0x0016},
	{0x03, 0x01e1},	{0x04, 0x0281}, {0x05, 0x0004},
	{0x06, 0x0000},	{0x07, 0x3002},	{0x06, 0x002d},
	{0x05, 0x0004},	{0x09, 0x0064},	{0x2b, 0x00a0},
	{0x2c, 0x00a0},	{0x2d, 0x00a0},	{0x2e, 0x00a0},
	{0x02, 0x0016},	{0x03, 0x01e1},	{0x04, 0x0281},
	{0x05, 0x0004},	{0x06, 0x002d},	{0x07, 0x3002},
	{0x0e, 0x0008},	{0x06, 0x002d},	{0x05, 0x0004},
};

static u16 mt9v011_init[][2] = {
	{0x07, 0x0002},	{0x0d, 0x0001},	{0x0d, 0x0000},
	{0x01, 0x0008},	{0x02, 0x0016},	{0x03, 0x01e1},
	{0x04, 0x0281},	{0x05, 0x0083},	{0x06, 0x0006},
	{0x0d, 0x0002}, {0x0a, 0x0000},	{0x0b, 0x0000},
	{0x0c, 0x0000},	{0x0d, 0x0000},	{0x0e, 0x0000},
	{0x0f, 0x0000},	{0x10, 0x0000},	{0x11, 0x0000},
	{0x12, 0x0000},	{0x13, 0x0000},	{0x14, 0x0000},
	{0x15, 0x0000},	{0x16, 0x0000},	{0x17, 0x0000},
	{0x18, 0x0000},	{0x19, 0x0000},	{0x1a, 0x0000},
	{0x1b, 0x0000},	{0x1c, 0x0000},	{0x1d, 0x0000},
	{0x32, 0x0000},	{0x20, 0x1101},	{0x21, 0x0000},
	{0x22, 0x0000},	{0x23, 0x0000},	{0x24, 0x0000},
	{0x25, 0x0000},	{0x26, 0x0000},	{0x27, 0x0024},
	{0x2f, 0xf7b0},	{0x30, 0x0005},	{0x31, 0x0000},
	{0x32, 0x0000},	{0x33, 0x0000},	{0x34, 0x0100},
	{0x3d, 0x068f},	{0x40, 0x01e0},	{0x41, 0x00d1},
	{0x44, 0x0082},	{0x5a, 0x0000},	{0x5b, 0x0000},
	{0x5c, 0x0000},	{0x5d, 0x0000},	{0x5e, 0x0000},
	{0x5f, 0xa31d},	{0x62, 0x0611},	{0x0a, 0x0000},
	{0x06, 0x0029},	{0x05, 0x0009},	{0x20, 0x1101},
	{0x20, 0x1101},	{0x09, 0x0064},	{0x07, 0x0003},
	{0x2b, 0x0033},	{0x2c, 0x00a0},	{0x2d, 0x00a0},
	{0x2e, 0x0033},	{0x07, 0x0002},	{0x06, 0x0000},
	{0x06, 0x0029},	{0x05, 0x0009},
};

static u16 mt9m001_init[][2] = {
	{0x0d, 0x0001}, {0x0d, 0x0000}, {0x01, 0x000e},
	{0x02, 0x0014}, {0x03, 0x03c1}, {0x04, 0x0501},
	{0x05, 0x0083}, {0x06, 0x0006}, {0x0d, 0x0002},
	{0x0a, 0x0000}, {0x0c, 0x0000}, {0x11, 0x0000},
	{0x1e, 0x8000}, {0x5f, 0x8904}, {0x60, 0x0000},
	{0x61, 0x0000}, {0x62, 0x0498}, {0x63, 0x0000},
	{0x64, 0x0000}, {0x20, 0x111d}, {0x06, 0x00f2},
	{0x05, 0x0013}, {0x09, 0x10f2}, {0x07, 0x0003},
	{0x2b, 0x002a}, {0x2d, 0x002a}, {0x2c, 0x002a},
	{0x2e, 0x0029}, {0x07, 0x0002},
};

static u16 mt9m111_init[][2] = {
	{0xf0, 0x0000}, {0x0d, 0x0008}, {0x0d, 0x0009},
	{0x0d, 0x0008}, {0xf0, 0x0001}, {0x3a, 0x4300},
	{0x9b, 0x4300}, {0xa1, 0x0280}, {0xa4, 0x0200},
	{0x06, 0x308e}, {0xf0, 0x0000},
};

static u8 hv7131r_init[][2] = {
	{0x02, 0x08}, {0x02, 0x00}, {0x01, 0x08},
	{0x02, 0x00}, {0x20, 0x00}, {0x21, 0xd0},
	{0x22, 0x00}, {0x23, 0x09}, {0x01, 0x08},
	{0x01, 0x08}, {0x01, 0x08}, {0x25, 0x07},
	{0x26, 0xc3}, {0x27, 0x50}, {0x30, 0x62},
	{0x31, 0x10}, {0x32, 0x06}, {0x33, 0x10},
	{0x20, 0x00}, {0x21, 0xd0}, {0x22, 0x00},
	{0x23, 0x09}, {0x01, 0x08},
};

int reg_r(struct gspca_dev *gspca_dev, u16 reg, u16 length)
{
	struct usb_device *dev = gspca_dev->dev;
	int result;
	result = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
			0x00,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_INTERFACE,
			reg,
			0x00,
			gspca_dev->usb_buf,
			length,
			500);
	if (unlikely(result < 0 || result != length)) {
		err("Read register failed 0x%02X", reg);
		return -EIO;
	}
	return 0;
}

int reg_w(struct gspca_dev *gspca_dev, u16 reg, const u8 *buffer, int length)
{
	struct usb_device *dev = gspca_dev->dev;
	int result;
	memcpy(gspca_dev->usb_buf, buffer, length);
	result = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
			0x08,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_INTERFACE,
			reg,
			0x00,
			gspca_dev->usb_buf,
			length,
			500);
	if (unlikely(result < 0 || result != length)) {
		err("Write register failed index 0x%02X", reg);
		return -EIO;
	}
	return 0;
}

int reg_w1(struct gspca_dev *gspca_dev, u16 reg, const u8 value)
{
	u8 data[1] = {value};
	return reg_w(gspca_dev, reg, data, 1);
}

int i2c_w(struct gspca_dev *gspca_dev, const u8 *buffer)
{
	int i;
	reg_w(gspca_dev, 0x10c0, buffer, 8);
	for (i = 0; i < 5; i++) {
		reg_r(gspca_dev, 0x10c0, 1);
		if (gspca_dev->usb_buf[0] & 0x04) {
			if (gspca_dev->usb_buf[0] & 0x08)
				return -1;
			return 0;
		}
		msleep(1);
	}
	return -1;
}

int i2c_w1(struct gspca_dev *gspca_dev, u8 reg, u8 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	u8 row[8];

	/*
	 * from the point of view of the bridge, the length
	 * includes the address
	 */
	row[0] = 0x81 | (2 << 4);
	row[1] = sd->i2c_addr;
	row[2] = reg;
	row[3] = val;
	row[4] = 0x00;
	row[5] = 0x00;
	row[6] = 0x00;
	row[7] = 0x10;

	return i2c_w(gspca_dev, row);
}

int i2c_w2(struct gspca_dev *gspca_dev, u8 reg, u16 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 row[8];

	/*
	 * from the point of view of the bridge, the length
	 * includes the address
	 */
	row[0] = 0x81 | (3 << 4);
	row[1] = sd->i2c_addr;
	row[2] = reg;
	row[3] = (val >> 8) & 0xff;
	row[4] = val & 0xff;
	row[5] = 0x00;
	row[6] = 0x00;
	row[7] = 0x10;

	return i2c_w(gspca_dev, row);
}

int i2c_r1(struct gspca_dev *gspca_dev, u8 reg, u8 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 row[8];

	row[0] = 0x81 | 0x10;
	row[1] = sd->i2c_addr;
	row[2] = reg;
	row[3] = 0;
	row[4] = 0;
	row[5] = 0;
	row[6] = 0;
	row[7] = 0x10;
	reg_w(gspca_dev, 0x10c0, row, 8);
	msleep(1);
	row[0] = 0x81 | (2 << 4) | 0x02;
	row[2] = 0;
	reg_w(gspca_dev, 0x10c0, row, 8);
	msleep(1);
	reg_r(gspca_dev, 0x10c2, 5);
	*val = gspca_dev->usb_buf[3];
	return 0;
}

int i2c_r2(struct gspca_dev *gspca_dev, u8 reg, u16 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 row[8];

	row[0] = 0x81 | 0x10;
	row[1] = sd->i2c_addr;
	row[2] = reg;
	row[3] = 0;
	row[4] = 0;
	row[5] = 0;
	row[6] = 0;
	row[7] = 0x10;
	reg_w(gspca_dev, 0x10c0, row, 8);
	msleep(1);
	row[0] = 0x81 | (3 << 4) | 0x02;
	row[2] = 0;
	reg_w(gspca_dev, 0x10c0, row, 8);
	msleep(1);
	reg_r(gspca_dev, 0x10c2, 5);
	*val = (gspca_dev->usb_buf[2] << 8) | gspca_dev->usb_buf[3];
	return 0;
}

static int ov9650_init_sensor(struct gspca_dev *gspca_dev)
{
	int i;
	struct sd *sd = (struct sd *) gspca_dev;

	for (i = 0; i < ARRAY_SIZE(ov9650_init); i++) {
		if (i2c_w1(gspca_dev, ov9650_init[i][0],
				ov9650_init[i][1]) < 0) {
			err("OV9650 sensor initialization failed");
			return -ENODEV;
		}
	}
	sd->hstart = 1;
	sd->vstart = 7;
	return 0;
}

static int ov9655_init_sensor(struct gspca_dev *gspca_dev)
{
	int i;
	struct sd *sd = (struct sd *) gspca_dev;

	for (i = 0; i < ARRAY_SIZE(ov9655_init); i++) {
		if (i2c_w1(gspca_dev, ov9655_init[i][0],
				ov9655_init[i][1]) < 0) {
			err("OV9655 sensor initialization failed");
			return -ENODEV;
		}
	}
	/* disable hflip and vflip */
	gspca_dev->ctrl_dis = (1 << HFLIP_IDX) | (1 << VFLIP_IDX);
	sd->hstart = 0;
	sd->vstart = 7;
	return 0;
}

static int soi968_init_sensor(struct gspca_dev *gspca_dev)
{
	int i;
	struct sd *sd = (struct sd *) gspca_dev;

	for (i = 0; i < ARRAY_SIZE(soi968_init); i++) {
		if (i2c_w1(gspca_dev, soi968_init[i][0],
				soi968_init[i][1]) < 0) {
			err("SOI968 sensor initialization failed");
			return -ENODEV;
		}
	}
	/* disable hflip and vflip */
	gspca_dev->ctrl_dis = (1 << HFLIP_IDX) | (1 << VFLIP_IDX);
	sd->hstart = 60;
	sd->vstart = 11;
	return 0;
}

static int ov7660_init_sensor(struct gspca_dev *gspca_dev)
{
	int i;
	struct sd *sd = (struct sd *) gspca_dev;

	for (i = 0; i < ARRAY_SIZE(ov7660_init); i++) {
		if (i2c_w1(gspca_dev, ov7660_init[i][0],
				ov7660_init[i][1]) < 0) {
			err("OV7660 sensor initialization failed");
			return -ENODEV;
		}
	}
	/* disable hflip and vflip */
	gspca_dev->ctrl_dis = (1 << HFLIP_IDX) | (1 << VFLIP_IDX);
	sd->hstart = 1;
	sd->vstart = 1;
	return 0;
}

static int ov7670_init_sensor(struct gspca_dev *gspca_dev)
{
	int i;
	struct sd *sd = (struct sd *) gspca_dev;

	for (i = 0; i < ARRAY_SIZE(ov7670_init); i++) {
		if (i2c_w1(gspca_dev, ov7670_init[i][0],
				ov7670_init[i][1]) < 0) {
			err("OV7670 sensor initialization failed");
			return -ENODEV;
		}
	}
	/* disable hflip and vflip */
	gspca_dev->ctrl_dis = (1 << HFLIP_IDX) | (1 << VFLIP_IDX);
	sd->hstart = 0;
	sd->vstart = 1;
	return 0;
}

static int mt9v_init_sensor(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int i;
	u16 value;
	int ret;

	sd->i2c_addr = 0x5d;
	ret = i2c_r2(gspca_dev, 0xff, &value);
	if ((ret == 0) && (value == 0x8243)) {
		for (i = 0; i < ARRAY_SIZE(mt9v011_init); i++) {
			if (i2c_w2(gspca_dev, mt9v011_init[i][0],
					mt9v011_init[i][1]) < 0) {
				err("MT9V011 sensor initialization failed");
				return -ENODEV;
			}
		}
		sd->hstart = 2;
		sd->vstart = 2;
		sd->sensor = SENSOR_MT9V011;
		info("MT9V011 sensor detected");
		return 0;
	}

	sd->i2c_addr = 0x5c;
	i2c_w2(gspca_dev, 0x01, 0x0004);
	ret = i2c_r2(gspca_dev, 0xff, &value);
	if ((ret == 0) && (value == 0x823a)) {
		for (i = 0; i < ARRAY_SIZE(mt9v111_init); i++) {
			if (i2c_w2(gspca_dev, mt9v111_init[i][0],
					mt9v111_init[i][1]) < 0) {
				err("MT9V111 sensor initialization failed");
				return -ENODEV;
			}
		}
		sd->hstart = 2;
		sd->vstart = 2;
		sd->sensor = SENSOR_MT9V111;
		info("MT9V111 sensor detected");
		return 0;
	}

	sd->i2c_addr = 0x5d;
	ret = i2c_w2(gspca_dev, 0xf0, 0x0000);
	if (ret < 0) {
		sd->i2c_addr = 0x48;
		i2c_w2(gspca_dev, 0xf0, 0x0000);
	}
	ret = i2c_r2(gspca_dev, 0x00, &value);
	if ((ret == 0) && (value == 0x1229)) {
		for (i = 0; i < ARRAY_SIZE(mt9v112_init); i++) {
			if (i2c_w2(gspca_dev, mt9v112_init[i][0],
					mt9v112_init[i][1]) < 0) {
				err("MT9V112 sensor initialization failed");
				return -ENODEV;
			}
		}
		sd->hstart = 6;
		sd->vstart = 2;
		sd->sensor = SENSOR_MT9V112;
		info("MT9V112 sensor detected");
		return 0;
	}

	return -ENODEV;
}

static int mt9m111_init_sensor(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int i;
	for (i = 0; i < ARRAY_SIZE(mt9m111_init); i++) {
		if (i2c_w2(gspca_dev, mt9m111_init[i][0],
				mt9m111_init[i][1]) < 0) {
			err("MT9M111 sensor initialization failed");
			return -ENODEV;
		}
	}
	sd->hstart = 0;
	sd->vstart = 2;
	return 0;
}

static int mt9m001_init_sensor(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int i;
	for (i = 0; i < ARRAY_SIZE(mt9m001_init); i++) {
		if (i2c_w2(gspca_dev, mt9m001_init[i][0],
				mt9m001_init[i][1]) < 0) {
			err("MT9M001 sensor initialization failed");
			return -ENODEV;
		}
	}
	/* disable hflip and vflip */
	gspca_dev->ctrl_dis = (1 << HFLIP_IDX) | (1 << VFLIP_IDX);
	sd->hstart = 2;
	sd->vstart = 2;
	return 0;
}

static int hv7131r_init_sensor(struct gspca_dev *gspca_dev)
{
	int i;
	struct sd *sd = (struct sd *) gspca_dev;

	for (i = 0; i < ARRAY_SIZE(hv7131r_init); i++) {
		if (i2c_w1(gspca_dev, hv7131r_init[i][0],
				hv7131r_init[i][1]) < 0) {
			err("HV7131R Sensor initialization failed");
			return -ENODEV;
		}
	}
	sd->hstart = 0;
	sd->vstart = 1;
	return 0;
}

#ifdef CONFIG_USB_GSPCA_SN9C20X_EVDEV
static int input_kthread(void *data)
{
	struct gspca_dev *gspca_dev = (struct gspca_dev *)data;
	struct sd *sd = (struct sd *) gspca_dev;

	DECLARE_WAIT_QUEUE_HEAD(wait);
	set_freezable();
	for (;;) {
		if (kthread_should_stop())
			break;

		if (reg_r(gspca_dev, 0x1005, 1) < 0)
			continue;

		input_report_key(sd->input_dev,
				 KEY_CAMERA,
				 gspca_dev->usb_buf[0] & sd->input_gpio);
		input_sync(sd->input_dev);

		wait_event_freezable_timeout(wait,
					     kthread_should_stop(),
					     msecs_to_jiffies(100));
	}
	return 0;
}


static int sn9c20x_input_init(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	if (sd->input_gpio == 0)
		return 0;

	sd->input_dev = input_allocate_device();
	if (!sd->input_dev)
		return -ENOMEM;

	sd->input_dev->name = "SN9C20X Webcam";

	sd->input_dev->phys = kasprintf(GFP_KERNEL, "usb-%s-%s",
					 gspca_dev->dev->bus->bus_name,
					 gspca_dev->dev->devpath);

	if (!sd->input_dev->phys)
		return -ENOMEM;

	usb_to_input_id(gspca_dev->dev, &sd->input_dev->id);
	sd->input_dev->dev.parent = &gspca_dev->dev->dev;

	set_bit(EV_KEY, sd->input_dev->evbit);
	set_bit(KEY_CAMERA, sd->input_dev->keybit);

	if (input_register_device(sd->input_dev))
		return -EINVAL;

	sd->input_task = kthread_run(input_kthread, gspca_dev, "sn9c20x/%d",
				     gspca_dev->vdev.minor);

	if (IS_ERR(sd->input_task))
		return -EINVAL;

	return 0;
}

static void sn9c20x_input_cleanup(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	if (sd->input_task != NULL && !IS_ERR(sd->input_task))
		kthread_stop(sd->input_task);

	if (sd->input_dev != NULL) {
		input_unregister_device(sd->input_dev);
		kfree(sd->input_dev->phys);
		input_free_device(sd->input_dev);
		sd->input_dev = NULL;
	}
}
#endif

static int set_cmatrix(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 hue_coord, hue_index = 180 + sd->hue;
	u8 cmatrix[21];
	memset(cmatrix, 0, 21);

	cmatrix[2] = (sd->contrast * 0x25 / 0x100) + 0x26;
	cmatrix[0] = 0x13 + (cmatrix[2] - 0x26) * 0x13 / 0x25;
	cmatrix[4] = 0x07 + (cmatrix[2] - 0x26) * 0x07 / 0x25;
	cmatrix[18] = sd->brightness - 0x80;

	hue_coord = (hsv_red_x[hue_index] * sd->saturation) >> 8;
	cmatrix[6] = (unsigned char)(hue_coord & 0xff);
	cmatrix[7] = (unsigned char)((hue_coord >> 8) & 0x0f);

	hue_coord = (hsv_red_y[hue_index] * sd->saturation) >> 8;
	cmatrix[8] = (unsigned char)(hue_coord & 0xff);
	cmatrix[9] = (unsigned char)((hue_coord >> 8) & 0x0f);

	hue_coord = (hsv_green_x[hue_index] * sd->saturation) >> 8;
	cmatrix[10] = (unsigned char)(hue_coord & 0xff);
	cmatrix[11] = (unsigned char)((hue_coord >> 8) & 0x0f);

	hue_coord = (hsv_green_y[hue_index] * sd->saturation) >> 8;
	cmatrix[12] = (unsigned char)(hue_coord & 0xff);
	cmatrix[13] = (unsigned char)((hue_coord >> 8) & 0x0f);

	hue_coord = (hsv_blue_x[hue_index] * sd->saturation) >> 8;
	cmatrix[14] = (unsigned char)(hue_coord & 0xff);
	cmatrix[15] = (unsigned char)((hue_coord >> 8) & 0x0f);

	hue_coord = (hsv_blue_y[hue_index] * sd->saturation) >> 8;
	cmatrix[16] = (unsigned char)(hue_coord & 0xff);
	cmatrix[17] = (unsigned char)((hue_coord >> 8) & 0x0f);

	return reg_w(gspca_dev, 0x10e1, cmatrix, 21);
}

static int set_gamma(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 gamma[17];
	u8 gval = sd->gamma * 0xb8 / 0x100;


	gamma[0] = 0x0a;
	gamma[1] = 0x13 + (gval * (0xcb - 0x13) / 0xb8);
	gamma[2] = 0x25 + (gval * (0xee - 0x25) / 0xb8);
	gamma[3] = 0x37 + (gval * (0xfa - 0x37) / 0xb8);
	gamma[4] = 0x45 + (gval * (0xfc - 0x45) / 0xb8);
	gamma[5] = 0x55 + (gval * (0xfb - 0x55) / 0xb8);
	gamma[6] = 0x65 + (gval * (0xfc - 0x65) / 0xb8);
	gamma[7] = 0x74 + (gval * (0xfd - 0x74) / 0xb8);
	gamma[8] = 0x83 + (gval * (0xfe - 0x83) / 0xb8);
	gamma[9] = 0x92 + (gval * (0xfc - 0x92) / 0xb8);
	gamma[10] = 0xa1 + (gval * (0xfc - 0xa1) / 0xb8);
	gamma[11] = 0xb0 + (gval * (0xfc - 0xb0) / 0xb8);
	gamma[12] = 0xbf + (gval * (0xfb - 0xbf) / 0xb8);
	gamma[13] = 0xce + (gval * (0xfb - 0xce) / 0xb8);
	gamma[14] = 0xdf + (gval * (0xfd - 0xdf) / 0xb8);
	gamma[15] = 0xea + (gval * (0xf9 - 0xea) / 0xb8);
	gamma[16] = 0xf5;

	return reg_w(gspca_dev, 0x1190, gamma, 17);
}

static int set_redblue(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	reg_w1(gspca_dev, 0x118c, sd->red);
	reg_w1(gspca_dev, 0x118f, sd->blue);
	return 0;
}

static int set_hvflip(struct gspca_dev *gspca_dev)
{
	u8 value, tslb;
	u16 value2;
	struct sd *sd = (struct sd *) gspca_dev;
	switch (sd->sensor) {
	case SENSOR_OV9650:
		i2c_r1(gspca_dev, 0x1e, &value);
		value &= ~0x30;
		tslb = 0x01;
		if (sd->hflip)
			value |= 0x20;
		if (sd->vflip) {
			value |= 0x10;
			tslb = 0x49;
		}
		i2c_w1(gspca_dev, 0x1e, value);
		i2c_w1(gspca_dev, 0x3a, tslb);
		break;
	case SENSOR_MT9V111:
	case SENSOR_MT9V011:
		i2c_r2(gspca_dev, 0x20, &value2);
		value2 &= ~0xc0a0;
		if (sd->hflip)
			value2 |= 0x8080;
		if (sd->vflip)
			value2 |= 0x4020;
		i2c_w2(gspca_dev, 0x20, value2);
		break;
	case SENSOR_MT9M111:
	case SENSOR_MT9V112:
		i2c_r2(gspca_dev, 0x20, &value2);
		value2 &= ~0x0003;
		if (sd->hflip)
			value2 |= 0x0002;
		if (sd->vflip)
			value2 |= 0x0001;
		i2c_w2(gspca_dev, 0x20, value2);
		break;
	case SENSOR_HV7131R:
		i2c_r1(gspca_dev, 0x01, &value);
		value &= ~0x03;
		if (sd->vflip)
			value |= 0x01;
		if (sd->hflip)
			value |= 0x02;
		i2c_w1(gspca_dev, 0x01, value);
		break;
	}
	return 0;
}

static int set_exposure(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 exp[8] = {0x81, sd->i2c_addr, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1e};
	switch (sd->sensor) {
	case SENSOR_OV7660:
	case SENSOR_OV7670:
	case SENSOR_SOI968:
	case SENSOR_OV9655:
	case SENSOR_OV9650:
		exp[0] |= (3 << 4);
		exp[2] = 0x2d;
		exp[3] = sd->exposure & 0xff;
		exp[4] = sd->exposure >> 8;
		break;
	case SENSOR_MT9M001:
	case SENSOR_MT9M111:
	case SENSOR_MT9V112:
	case SENSOR_MT9V111:
	case SENSOR_MT9V011:
		exp[0] |= (3 << 4);
		exp[2] = 0x09;
		exp[3] = sd->exposure >> 8;
		exp[4] = sd->exposure & 0xff;
		break;
	case SENSOR_HV7131R:
		exp[0] |= (4 << 4);
		exp[2] = 0x25;
		exp[3] = ((sd->exposure * 0xffffff) / 0xffff) >> 16;
		exp[4] = ((sd->exposure * 0xffffff) / 0xffff) >> 8;
		exp[5] = ((sd->exposure * 0xffffff) / 0xffff) & 0xff;
		break;
	}
	i2c_w(gspca_dev, exp);
	return 0;
}

static int set_gain(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 gain[8] = {0x81, sd->i2c_addr, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1d};
	switch (sd->sensor) {
	case SENSOR_OV7660:
	case SENSOR_OV7670:
	case SENSOR_SOI968:
	case SENSOR_OV9655:
	case SENSOR_OV9650:
		gain[0] |= (2 << 4);
		gain[3] = ov_gain[sd->gain];
		break;
	case SENSOR_MT9V011:
	case SENSOR_MT9V111:
		gain[0] |= (3 << 4);
		gain[2] = 0x35;
		gain[3] = micron1_gain[sd->gain] >> 8;
		gain[4] = micron1_gain[sd->gain] & 0xff;
		break;
	case SENSOR_MT9V112:
	case SENSOR_MT9M111:
		gain[0] |= (3 << 4);
		gain[2] = 0x2f;
		gain[3] = micron1_gain[sd->gain] >> 8;
		gain[4] = micron1_gain[sd->gain] & 0xff;
		break;
	case SENSOR_MT9M001:
		gain[0] |= (3 << 4);
		gain[2] = 0x2f;
		gain[3] = micron2_gain[sd->gain] >> 8;
		gain[4] = micron2_gain[sd->gain] & 0xff;
		break;
	case SENSOR_HV7131R:
		gain[0] |= (2 << 4);
		gain[2] = 0x30;
		gain[3] = hv7131r_gain[sd->gain];
		break;
	}
	i2c_w(gspca_dev, gain);
	return 0;
}

static int sd_setbrightness(struct gspca_dev *gspca_dev, s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->brightness = val;
	if (gspca_dev->streaming)
		return set_cmatrix(gspca_dev);
	return 0;
}

static int sd_getbrightness(struct gspca_dev *gspca_dev, s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	*val = sd->brightness;
	return 0;
}


static int sd_setcontrast(struct gspca_dev *gspca_dev, s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->contrast = val;
	if (gspca_dev->streaming)
		return set_cmatrix(gspca_dev);
	return 0;
}

static int sd_getcontrast(struct gspca_dev *gspca_dev, s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	*val = sd->contrast;
	return 0;
}

static int sd_setsaturation(struct gspca_dev *gspca_dev, s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->saturation = val;
	if (gspca_dev->streaming)
		return set_cmatrix(gspca_dev);
	return 0;
}

static int sd_getsaturation(struct gspca_dev *gspca_dev, s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	*val = sd->saturation;
	return 0;
}

static int sd_sethue(struct gspca_dev *gspca_dev, s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->hue = val;
	if (gspca_dev->streaming)
		return set_cmatrix(gspca_dev);
	return 0;
}

static int sd_gethue(struct gspca_dev *gspca_dev, s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	*val = sd->hue;
	return 0;
}

static int sd_setgamma(struct gspca_dev *gspca_dev, s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->gamma = val;
	if (gspca_dev->streaming)
		return set_gamma(gspca_dev);
	return 0;
}

static int sd_getgamma(struct gspca_dev *gspca_dev, s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	*val = sd->gamma;
	return 0;
}

static int sd_setredbalance(struct gspca_dev *gspca_dev, s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->red = val;
	if (gspca_dev->streaming)
		return set_redblue(gspca_dev);
	return 0;
}

static int sd_getredbalance(struct gspca_dev *gspca_dev, s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	*val = sd->red;
	return 0;
}

static int sd_setbluebalance(struct gspca_dev *gspca_dev, s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->blue = val;
	if (gspca_dev->streaming)
		return set_redblue(gspca_dev);
	return 0;
}

static int sd_getbluebalance(struct gspca_dev *gspca_dev, s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	*val = sd->blue;
	return 0;
}

static int sd_sethflip(struct gspca_dev *gspca_dev, s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->hflip = val;
	if (gspca_dev->streaming)
		return set_hvflip(gspca_dev);
	return 0;
}

static int sd_gethflip(struct gspca_dev *gspca_dev, s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	*val = sd->hflip;
	return 0;
}

static int sd_setvflip(struct gspca_dev *gspca_dev, s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->vflip = val;
	if (gspca_dev->streaming)
		return set_hvflip(gspca_dev);
	return 0;
}

static int sd_getvflip(struct gspca_dev *gspca_dev, s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	*val = sd->vflip;
	return 0;
}

static int sd_setexposure(struct gspca_dev *gspca_dev, s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->exposure = val;
	if (gspca_dev->streaming)
		return set_exposure(gspca_dev);
	return 0;
}

static int sd_getexposure(struct gspca_dev *gspca_dev, s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	*val = sd->exposure;
	return 0;
}

static int sd_setgain(struct gspca_dev *gspca_dev, s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->gain = val;
	if (gspca_dev->streaming)
		return set_gain(gspca_dev);
	return 0;
}

static int sd_getgain(struct gspca_dev *gspca_dev, s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	*val = sd->gain;
	return 0;
}

static int sd_setautoexposure(struct gspca_dev *gspca_dev, s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	sd->auto_exposure = val;
	return 0;
}

static int sd_getautoexposure(struct gspca_dev *gspca_dev, s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	*val = sd->auto_exposure;
	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int sd_dbg_g_register(struct gspca_dev *gspca_dev,
			struct v4l2_dbg_register *reg)
{
	struct sd *sd = (struct sd *) gspca_dev;
	switch (reg->match.type) {
	case V4L2_CHIP_MATCH_HOST:
		if (reg->match.addr != 0)
			return -EINVAL;
		if (reg->reg < 0x1000 || reg->reg > 0x11ff)
			return -EINVAL;
		if (reg_r(gspca_dev, reg->reg, 1) < 0)
			return -EINVAL;
		reg->val = gspca_dev->usb_buf[0];
		return 0;
	case V4L2_CHIP_MATCH_I2C_ADDR:
		if (reg->match.addr != sd->i2c_addr)
			return -EINVAL;
		if (sd->sensor >= SENSOR_MT9V011 &&
		    sd->sensor <= SENSOR_MT9M111) {
			if (i2c_r2(gspca_dev, reg->reg, (u16 *)&reg->val) < 0)
				return -EINVAL;
		} else {
			if (i2c_r1(gspca_dev, reg->reg, (u8 *)&reg->val) < 0)
				return -EINVAL;
		}
		return 0;
	}
	return -EINVAL;
}

static int sd_dbg_s_register(struct gspca_dev *gspca_dev,
			struct v4l2_dbg_register *reg)
{
	struct sd *sd = (struct sd *) gspca_dev;
	switch (reg->match.type) {
	case V4L2_CHIP_MATCH_HOST:
		if (reg->match.addr != 0)
			return -EINVAL;
		if (reg->reg < 0x1000 || reg->reg > 0x11ff)
			return -EINVAL;
		if (reg_w1(gspca_dev, reg->reg, reg->val) < 0)
			return -EINVAL;
		return 0;
	case V4L2_CHIP_MATCH_I2C_ADDR:
		if (reg->match.addr != sd->i2c_addr)
			return -EINVAL;
		if (sd->sensor >= SENSOR_MT9V011 &&
		    sd->sensor <= SENSOR_MT9M111) {
			if (i2c_w2(gspca_dev, reg->reg, reg->val) < 0)
				return -EINVAL;
		} else {
			if (i2c_w1(gspca_dev, reg->reg, reg->val) < 0)
				return -EINVAL;
		}
		return 0;
	}
	return -EINVAL;
}
#endif

static int sd_chip_ident(struct gspca_dev *gspca_dev,
			struct v4l2_dbg_chip_ident *chip)
{
	struct sd *sd = (struct sd *) gspca_dev;

	switch (chip->match.type) {
	case V4L2_CHIP_MATCH_HOST:
		if (chip->match.addr != 0)
			return -EINVAL;
		chip->revision = 0;
		chip->ident = V4L2_IDENT_SN9C20X;
		return 0;
	case V4L2_CHIP_MATCH_I2C_ADDR:
		if (chip->match.addr != sd->i2c_addr)
			return -EINVAL;
		chip->revision = 0;
		chip->ident = i2c_ident[sd->sensor];
		return 0;
	}
	return -EINVAL;
}

static int sd_config(struct gspca_dev *gspca_dev,
			const struct usb_device_id *id)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct cam *cam;

	cam = &gspca_dev->cam;

	sd->sensor = (id->driver_info >> 8) & 0xff;
	sd->i2c_addr = id->driver_info & 0xff;

	switch (sd->sensor) {
	case SENSOR_OV9650:
		cam->cam_mode = sxga_mode;
		cam->nmodes = ARRAY_SIZE(sxga_mode);
		break;
	default:
		cam->cam_mode = vga_mode;
		cam->nmodes = ARRAY_SIZE(vga_mode);
	}

	sd->old_step = 0;
	sd->older_step = 0;
	sd->exposure_step = 16;

	sd->brightness = BRIGHTNESS_DEFAULT;
	sd->contrast = CONTRAST_DEFAULT;
	sd->saturation = SATURATION_DEFAULT;
	sd->hue = HUE_DEFAULT;
	sd->gamma = GAMMA_DEFAULT;
	sd->red = RED_DEFAULT;
	sd->blue = BLUE_DEFAULT;

	sd->hflip = HFLIP_DEFAULT;
	sd->vflip = VFLIP_DEFAULT;
	sd->exposure = EXPOSURE_DEFAULT;
	sd->gain = GAIN_DEFAULT;
	sd->auto_exposure = AUTO_EXPOSURE_DEFAULT;

	sd->quality = 95;

#ifdef CONFIG_USB_GSPCA_SN9C20X_EVDEV
	sd->input_gpio = (id->driver_info >> 16) & 0xff;
	if (sn9c20x_input_init(gspca_dev) < 0)
		return -ENODEV;
#endif
	return 0;
}

static int sd_init(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int i;
	u8 value;
	u8 i2c_init[9] =
		{0x80, sd->i2c_addr, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03};

	for (i = 0; i < ARRAY_SIZE(bridge_init); i++) {
		value = bridge_init[i][1];
		if (reg_w(gspca_dev, bridge_init[i][0], &value, 1) < 0) {
			err("Device initialization failed");
			return -ENODEV;
		}
	}

	if (reg_w(gspca_dev, 0x10c0, i2c_init, 9) < 0) {
		err("Device initialization failed");
		return -ENODEV;
	}

	switch (sd->sensor) {
	case SENSOR_OV9650:
		if (ov9650_init_sensor(gspca_dev) < 0)
			return -ENODEV;
		info("OV9650 sensor detected");
		break;
	case SENSOR_OV9655:
		if (ov9655_init_sensor(gspca_dev) < 0)
			return -ENODEV;
		info("OV9655 sensor detected");
		break;
	case SENSOR_SOI968:
		if (soi968_init_sensor(gspca_dev) < 0)
			return -ENODEV;
		info("SOI968 sensor detected");
		break;
	case SENSOR_OV7660:
		if (ov7660_init_sensor(gspca_dev) < 0)
			return -ENODEV;
		info("OV7660 sensor detected");
		break;
	case SENSOR_OV7670:
		if (ov7670_init_sensor(gspca_dev) < 0)
			return -ENODEV;
		info("OV7670 sensor detected");
		break;
	case SENSOR_MT9VPRB:
		if (mt9v_init_sensor(gspca_dev) < 0)
			return -ENODEV;
		break;
	case SENSOR_MT9M111:
		if (mt9m111_init_sensor(gspca_dev) < 0)
			return -ENODEV;
		info("MT9M111 sensor detected");
		break;
	case SENSOR_MT9M001:
		if (mt9m001_init_sensor(gspca_dev) < 0)
			return -ENODEV;
		info("MT9M001 sensor detected");
		break;
	case SENSOR_HV7131R:
		if (hv7131r_init_sensor(gspca_dev) < 0)
			return -ENODEV;
		info("HV7131R sensor detected");
		break;
	default:
		info("Unsupported Sensor");
		return -ENODEV;
	}

	return 0;
}

static void configure_sensor_output(struct gspca_dev *gspca_dev, int mode)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 value;
	switch (sd->sensor) {
	case SENSOR_OV9650:
		if (mode & MODE_SXGA) {
			i2c_w1(gspca_dev, 0x17, 0x1b);
			i2c_w1(gspca_dev, 0x18, 0xbc);
			i2c_w1(gspca_dev, 0x19, 0x01);
			i2c_w1(gspca_dev, 0x1a, 0x82);
			i2c_r1(gspca_dev, 0x12, &value);
			i2c_w1(gspca_dev, 0x12, value & 0x07);
		} else {
			i2c_w1(gspca_dev, 0x17, 0x24);
			i2c_w1(gspca_dev, 0x18, 0xc5);
			i2c_w1(gspca_dev, 0x19, 0x00);
			i2c_w1(gspca_dev, 0x1a, 0x3c);
			i2c_r1(gspca_dev, 0x12, &value);
			i2c_w1(gspca_dev, 0x12, (value & 0x7) | 0x40);
		}
		break;
	}
}

#define HW_WIN(mode, hstart, vstart) \
((const u8 []){hstart & 0xff, hstart >> 8, \
vstart & 0xff, vstart >> 8, \
(mode & MODE_SXGA ? 1280 >> 4 : 640 >> 4), \
(mode & MODE_SXGA ? 1024 >> 3 : 480 >> 3)})

#define CLR_WIN(width, height) \
((const u8 [])\
{0, width >> 2, 0, height >> 1,\
((width >> 10) & 0x01) | ((height >> 8) & 0x6)})

static int sd_start(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int mode = gspca_dev->cam.cam_mode[(int) gspca_dev->curr_mode].priv;
	int width = gspca_dev->width;
	int height = gspca_dev->height;
	u8 fmt, scale = 0;

	sd->jpeg_hdr = kmalloc(JPEG_HDR_SZ, GFP_KERNEL);
	if (sd->jpeg_hdr == NULL)
		return -ENOMEM;

	jpeg_define(sd->jpeg_hdr, height, width,
			0x21);
	jpeg_set_qual(sd->jpeg_hdr, sd->quality);

	if (mode & MODE_RAW)
		fmt = 0x2d;
	else if (mode & MODE_JPEG)
		fmt = 0x2c;
	else
		fmt = 0x2f;

	switch (mode & 0x0f) {
	case 3:
		scale = 0xc0;
		info("Set 1280x1024");
		break;
	case 2:
		scale = 0x80;
		info("Set 640x480");
		break;
	case 1:
		scale = 0x90;
		info("Set 320x240");
		break;
	case 0:
		scale = 0xa0;
		info("Set 160x120");
		break;
	}

	configure_sensor_output(gspca_dev, mode);
	reg_w(gspca_dev, 0x1100, sd->jpeg_hdr + JPEG_QT0_OFFSET, 64);
	reg_w(gspca_dev, 0x1140, sd->jpeg_hdr + JPEG_QT1_OFFSET, 64);
	reg_w(gspca_dev, 0x10fb, CLR_WIN(width, height), 5);
	reg_w(gspca_dev, 0x1180, HW_WIN(mode, sd->hstart, sd->vstart), 6);
	reg_w1(gspca_dev, 0x1189, scale);
	reg_w1(gspca_dev, 0x10e0, fmt);

	set_cmatrix(gspca_dev);
	set_gamma(gspca_dev);
	set_redblue(gspca_dev);
	set_gain(gspca_dev);
	set_exposure(gspca_dev);
	set_hvflip(gspca_dev);

	reg_r(gspca_dev, 0x1061, 1);
	reg_w1(gspca_dev, 0x1061, gspca_dev->usb_buf[0] | 0x02);
	return 0;
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	reg_r(gspca_dev, 0x1061, 1);
	reg_w1(gspca_dev, 0x1061, gspca_dev->usb_buf[0] & ~0x02);
}

static void sd_stop0(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	kfree(sd->jpeg_hdr);
}

static void do_autoexposure(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int avg_lum, new_exp;

	if (!sd->auto_exposure)
		return;

	avg_lum = atomic_read(&sd->avg_lum);

	/*
	 * some hardcoded values are present
	 * like those for maximal/minimal exposure
	 * and exposure steps
	 */
	if (avg_lum < MIN_AVG_LUM) {
		if (sd->exposure > 0x1770)
			return;

		new_exp = sd->exposure + sd->exposure_step;
		if (new_exp > 0x1770)
			new_exp = 0x1770;
		if (new_exp < 0x10)
			new_exp = 0x10;
		sd->exposure = new_exp;
		set_exposure(gspca_dev);

		sd->older_step = sd->old_step;
		sd->old_step = 1;

		if (sd->old_step ^ sd->older_step)
			sd->exposure_step /= 2;
		else
			sd->exposure_step += 2;
	}
	if (avg_lum > MAX_AVG_LUM) {
		if (sd->exposure < 0x10)
			return;
		new_exp = sd->exposure - sd->exposure_step;
		if (new_exp > 0x1700)
			new_exp = 0x1770;
		if (new_exp < 0x10)
			new_exp = 0x10;
		sd->exposure = new_exp;
		set_exposure(gspca_dev);
		sd->older_step = sd->old_step;
		sd->old_step = 0;

		if (sd->old_step ^ sd->older_step)
			sd->exposure_step /= 2;
		else
			sd->exposure_step += 2;
	}
}

static void sd_pkt_scan(struct gspca_dev *gspca_dev,
			struct gspca_frame *frame,	/* target */
			u8 *data,			/* isoc packet */
			int len)			/* iso packet length */
{
	struct sd *sd = (struct sd *) gspca_dev;
	int avg_lum;
	static unsigned char frame_header[] =
		{0xff, 0xff, 0x00, 0xc4, 0xc4, 0x96};
	if (len == 64 && memcmp(data, frame_header, 6) == 0) {
		avg_lum = ((data[35] >> 2) & 3) |
			   (data[20] << 2) |
			   (data[19] << 10);
		avg_lum += ((data[35] >> 4) & 3) |
			    (data[22] << 2) |
			    (data[21] << 10);
		avg_lum += ((data[35] >> 6) & 3) |
			    (data[24] << 2) |
			    (data[23] << 10);
		avg_lum += (data[36] & 3) |
			   (data[26] << 2) |
			   (data[25] << 10);
		avg_lum += ((data[36] >> 2) & 3) |
			    (data[28] << 2) |
			    (data[27] << 10);
		avg_lum += ((data[36] >> 4) & 3) |
			    (data[30] << 2) |
			    (data[29] << 10);
		avg_lum += ((data[36] >> 6) & 3) |
			    (data[32] << 2) |
			    (data[31] << 10);
		avg_lum += ((data[44] >> 4) & 3) |
			    (data[34] << 2) |
			    (data[33] << 10);
		avg_lum >>= 9;
		atomic_set(&sd->avg_lum, avg_lum);
		gspca_frame_add(gspca_dev, LAST_PACKET,
				frame, data, len);
		return;
	}
	if (gspca_dev->last_packet_type == LAST_PACKET) {
		if (gspca_dev->cam.cam_mode[(int) gspca_dev->curr_mode].priv
				& MODE_JPEG) {
			gspca_frame_add(gspca_dev, FIRST_PACKET, frame,
				sd->jpeg_hdr, JPEG_HDR_SZ);
			gspca_frame_add(gspca_dev, INTER_PACKET, frame,
				data, len);
		} else {
			gspca_frame_add(gspca_dev, FIRST_PACKET, frame,
				data, len);
		}
	} else {
		gspca_frame_add(gspca_dev, INTER_PACKET, frame, data, len);
	}
}

/* sub-driver description */
static const struct sd_desc sd_desc = {
	.name = MODULE_NAME,
	.ctrls = sd_ctrls,
	.nctrls = ARRAY_SIZE(sd_ctrls),
	.config = sd_config,
	.init = sd_init,
	.start = sd_start,
	.stopN = sd_stopN,
	.stop0 = sd_stop0,
	.pkt_scan = sd_pkt_scan,
	.dq_callback = do_autoexposure,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.set_register = sd_dbg_s_register,
	.get_register = sd_dbg_g_register,
#endif
	.get_chip_ident = sd_chip_ident,
};

#define SN9C20X(sensor, i2c_addr, button_mask) \
	.driver_info =  (button_mask << 16) \
			| (SENSOR_ ## sensor << 8) \
			| (i2c_addr)

static const __devinitdata struct usb_device_id device_table[] = {
	{USB_DEVICE(0x0c45, 0x6240), SN9C20X(MT9M001, 0x5d, 0)},
	{USB_DEVICE(0x0c45, 0x6242), SN9C20X(MT9M111, 0x5d, 0)},
	{USB_DEVICE(0x0c45, 0x6248), SN9C20X(OV9655, 0x30, 0)},
	{USB_DEVICE(0x0c45, 0x624e), SN9C20X(SOI968, 0x30, 0x10)},
	{USB_DEVICE(0x0c45, 0x624f), SN9C20X(OV9650, 0x30, 0)},
	{USB_DEVICE(0x0c45, 0x6251), SN9C20X(OV9650, 0x30, 0)},
	{USB_DEVICE(0x0c45, 0x6253), SN9C20X(OV9650, 0x30, 0)},
	{USB_DEVICE(0x0c45, 0x6260), SN9C20X(OV7670, 0x21, 0)},
	{USB_DEVICE(0x0c45, 0x6270), SN9C20X(MT9VPRB, 0x00, 0)},
	{USB_DEVICE(0x0c45, 0x627b), SN9C20X(OV7660, 0x21, 0)},
	{USB_DEVICE(0x0c45, 0x627c), SN9C20X(HV7131R, 0x11, 0)},
	{USB_DEVICE(0x0c45, 0x627f), SN9C20X(OV9650, 0x30, 0)},
	{USB_DEVICE(0x0c45, 0x6280), SN9C20X(MT9M001, 0x5d, 0)},
	{USB_DEVICE(0x0c45, 0x6282), SN9C20X(MT9M111, 0x5d, 0)},
	{USB_DEVICE(0x0c45, 0x6288), SN9C20X(OV9655, 0x30, 0)},
	{USB_DEVICE(0x0c45, 0x628e), SN9C20X(SOI968, 0x30, 0)},
	{USB_DEVICE(0x0c45, 0x628f), SN9C20X(OV9650, 0x30, 0)},
	{USB_DEVICE(0x0c45, 0x62a0), SN9C20X(OV7670, 0x21, 0)},
	{USB_DEVICE(0x0c45, 0x62b0), SN9C20X(MT9VPRB, 0x00, 0)},
	{USB_DEVICE(0x0c45, 0x62b3), SN9C20X(OV9655, 0x30, 0)},
	{USB_DEVICE(0x0c45, 0x62bb), SN9C20X(OV7660, 0x21, 0)},
	{USB_DEVICE(0x0c45, 0x62bc), SN9C20X(HV7131R, 0x11, 0)},
	{USB_DEVICE(0x045e, 0x00f4), SN9C20X(OV9650, 0x30, 0)},
	{USB_DEVICE(0x145f, 0x013d), SN9C20X(OV7660, 0x21, 0)},
	{USB_DEVICE(0x0458, 0x7029), SN9C20X(HV7131R, 0x11, 0)},
	{USB_DEVICE(0xa168, 0x0610), SN9C20X(HV7131R, 0x11, 0)},
	{USB_DEVICE(0xa168, 0x0611), SN9C20X(HV7131R, 0x11, 0)},
	{USB_DEVICE(0xa168, 0x0613), SN9C20X(HV7131R, 0x11, 0)},
	{USB_DEVICE(0xa168, 0x0618), SN9C20X(HV7131R, 0x11, 0)},
	{USB_DEVICE(0xa168, 0x0614), SN9C20X(MT9M111, 0x5d, 0)},
	{USB_DEVICE(0xa168, 0x0615), SN9C20X(MT9M111, 0x5d, 0)},
	{USB_DEVICE(0xa168, 0x0617), SN9C20X(MT9M111, 0x5d, 0)},
	{}
};
MODULE_DEVICE_TABLE(usb, device_table);

/* -- device connect -- */
static int sd_probe(struct usb_interface *intf,
		    const struct usb_device_id *id)
{
	return gspca_dev_probe(intf, id, &sd_desc, sizeof(struct sd),
				THIS_MODULE);
}

static void sd_disconnect(struct usb_interface *intf)
{
#ifdef CONFIG_USB_GSPCA_SN9C20X_EVDEV
	struct gspca_dev *gspca_dev = usb_get_intfdata(intf);

	sn9c20x_input_cleanup(gspca_dev);
#endif

	gspca_disconnect(intf);
}

static struct usb_driver sd_driver = {
	.name = MODULE_NAME,
	.id_table = device_table,
	.probe = sd_probe,
	.disconnect = sd_disconnect,
#ifdef CONFIG_PM
	.suspend = gspca_suspend,
	.resume = gspca_resume,
	.reset_resume = gspca_resume,
#endif
};

/* -- module insert / remove -- */
static int __init sd_mod_init(void)
{
	int ret;
	ret = usb_register(&sd_driver);
	if (ret < 0)
		return ret;
	info("registered");
	return 0;
}
static void __exit sd_mod_exit(void)
{
	usb_deregister(&sd_driver);
	info("deregistered");
}

module_init(sd_mod_init);
module_exit(sd_mod_exit);
