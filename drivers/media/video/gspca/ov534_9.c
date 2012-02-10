/*
 * ov534-ov9xxx gspca driver
 *
 * Copyright (C) 2009-2011 Jean-Francois Moine http://moinejf.free.fr
 * Copyright (C) 2008 Antonio Ospite <ospite@studenti.unina.it>
 * Copyright (C) 2008 Jim Paris <jim@jtan.com>
 *
 * Based on a prototype written by Mark Ferrell <majortrips@gmail.com>
 * USB protocol reverse engineered by Jim Paris <jim@jtan.com>
 * https://jim.sh/svn/jim/devl/playstation/ps3/eye/test/
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define MODULE_NAME "ov534_9"

#include "gspca.h"

#define OV534_REG_ADDRESS	0xf1	/* sensor address */
#define OV534_REG_SUBADDR	0xf2
#define OV534_REG_WRITE		0xf3
#define OV534_REG_READ		0xf4
#define OV534_REG_OPERATION	0xf5
#define OV534_REG_STATUS	0xf6

#define OV534_OP_WRITE_3	0x37
#define OV534_OP_WRITE_2	0x33
#define OV534_OP_READ_2		0xf9

#define CTRL_TIMEOUT 500

MODULE_AUTHOR("Jean-Francois Moine <moinejf@free.fr>");
MODULE_DESCRIPTION("GSPCA/OV534_9 USB Camera Driver");
MODULE_LICENSE("GPL");

/* controls */
enum e_ctrl {
	BRIGHTNESS,
	CONTRAST,
	AUTOGAIN,
	EXPOSURE,
	SHARPNESS,
	SATUR,
	LIGHTFREQ,
	NCTRLS		/* number of controls */
};

/* specific webcam descriptor */
struct sd {
	struct gspca_dev gspca_dev;	/* !! must be the first item */
	struct gspca_ctrl ctrls[NCTRLS];
	__u32 last_pts;
	u8 last_fid;

	u8 sensor;
};
enum sensors {
	SENSOR_OV965x,		/* ov9657 */
	SENSOR_OV971x,		/* ov9712 */
	SENSOR_OV562x,		/* ov5621 */
	NSENSORS
};

/* V4L2 controls supported by the driver */
static void setbrightness(struct gspca_dev *gspca_dev);
static void setcontrast(struct gspca_dev *gspca_dev);
static void setautogain(struct gspca_dev *gspca_dev);
static void setexposure(struct gspca_dev *gspca_dev);
static void setsharpness(struct gspca_dev *gspca_dev);
static void setsatur(struct gspca_dev *gspca_dev);
static void setlightfreq(struct gspca_dev *gspca_dev);

static const struct ctrl sd_ctrls[NCTRLS] = {
[BRIGHTNESS] = {
	{
		.id      = V4L2_CID_BRIGHTNESS,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Brightness",
		.minimum = 0,
		.maximum = 15,
		.step    = 1,
		.default_value = 7
	},
	.set_control = setbrightness
    },
[CONTRAST] = {
	{
		.id      = V4L2_CID_CONTRAST,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Contrast",
		.minimum = 0,
		.maximum = 15,
		.step    = 1,
		.default_value = 3
	},
	.set_control = setcontrast
    },
[AUTOGAIN] = {
	{
		.id      = V4L2_CID_AUTOGAIN,
		.type    = V4L2_CTRL_TYPE_BOOLEAN,
		.name    = "Autogain",
		.minimum = 0,
		.maximum = 1,
		.step    = 1,
#define AUTOGAIN_DEF 1
		.default_value = AUTOGAIN_DEF,
	},
	.set_control = setautogain
    },
[EXPOSURE] = {
	{
		.id      = V4L2_CID_EXPOSURE,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Exposure",
		.minimum = 0,
		.maximum = 3,
		.step    = 1,
		.default_value = 0
	},
	.set_control = setexposure
    },
[SHARPNESS] = {
	{
		.id      = V4L2_CID_SHARPNESS,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Sharpness",
		.minimum = -1,		/* -1 = auto */
		.maximum = 4,
		.step    = 1,
		.default_value = -1
	},
	.set_control = setsharpness
    },
[SATUR] = {
	{
		.id      = V4L2_CID_SATURATION,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Saturation",
		.minimum = 0,
		.maximum = 4,
		.step    = 1,
		.default_value = 2
	},
	.set_control = setsatur
    },
[LIGHTFREQ] = {
	{
		.id	 = V4L2_CID_POWER_LINE_FREQUENCY,
		.type    = V4L2_CTRL_TYPE_MENU,
		.name    = "Light frequency filter",
		.minimum = 0,
		.maximum = 2,	/* 0: 0, 1: 50Hz, 2:60Hz */
		.step    = 1,
		.default_value = 0
	},
	.set_control = setlightfreq
    },
};

static const struct v4l2_pix_format ov965x_mode[] = {
#define QVGA_MODE 0
	{320, 240, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240 * 3 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG},
#define VGA_MODE 1
	{640, 480, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 640,
		.sizeimage = 640 * 480 * 3 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG},
#define SVGA_MODE 2
	{800, 600, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 800,
		.sizeimage = 800 * 600 * 3 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG},
#define XGA_MODE 3
	{1024, 768, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 1024,
		.sizeimage = 1024 * 768 * 3 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG},
#define SXGA_MODE 4
	{1280, 1024, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 1280,
		.sizeimage = 1280 * 1024 * 3 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG},
};

static const struct v4l2_pix_format ov971x_mode[] = {
	{640, 480, V4L2_PIX_FMT_SBGGR8, V4L2_FIELD_NONE,
		.bytesperline = 640,
		.sizeimage = 640 * 480,
		.colorspace = V4L2_COLORSPACE_SRGB
	}
};

static const struct v4l2_pix_format ov562x_mode[] = {
	{2592, 1680, V4L2_PIX_FMT_SBGGR8, V4L2_FIELD_NONE,
		.bytesperline = 2592,
		.sizeimage = 2592 * 1680,
		.colorspace = V4L2_COLORSPACE_SRGB
	}
};

static const u8 bridge_init[][2] = {
	{0x88, 0xf8},
	{0x89, 0xff},
	{0x76, 0x03},
	{0x92, 0x03},
	{0x95, 0x10},
	{0xe2, 0x00},
	{0xe7, 0x3e},
	{0x8d, 0x1c},
	{0x8e, 0x00},
	{0x8f, 0x00},
	{0x1f, 0x00},
	{0xc3, 0xf9},
	{0x89, 0xff},
	{0x88, 0xf8},
	{0x76, 0x03},
	{0x92, 0x01},
	{0x93, 0x18},
	{0x1c, 0x0a},
	{0x1d, 0x48},
	{0xc0, 0x50},
	{0xc1, 0x3c},
	{0x34, 0x05},
	{0xc2, 0x0c},
	{0xc3, 0xf9},
	{0x34, 0x05},
	{0xe7, 0x2e},
	{0x31, 0xf9},
	{0x35, 0x02},
	{0xd9, 0x10},
	{0x25, 0x42},
	{0x94, 0x11},
};

static const u8 ov965x_init[][2] = {
	{0x12, 0x80},	/* com7 - SSCB reset */
	{0x00, 0x00},	/* gain */
	{0x01, 0x80},	/* blue */
	{0x02, 0x80},	/* red */
	{0x03, 0x1b},	/* vref */
	{0x04, 0x03},	/* com1 - exposure low bits */
	{0x0b, 0x57},	/* ver */
	{0x0e, 0x61},	/* com5 */
	{0x0f, 0x42},	/* com6 */
	{0x11, 0x00},	/* clkrc */
	{0x12, 0x02},	/* com7 - 15fps VGA YUYV */
	{0x13, 0xe7},	/* com8 - everything (AGC, AWB and AEC) */
	{0x14, 0x28},	/* com9 */
	{0x16, 0x24},	/* reg16 */
	{0x17, 0x1d},	/* hstart*/
	{0x18, 0xbd},	/* hstop */
	{0x19, 0x01},	/* vstrt */
	{0x1a, 0x81},	/* vstop*/
	{0x1e, 0x04},	/* mvfp */
	{0x24, 0x3c},	/* aew */
	{0x25, 0x36},	/* aeb */
	{0x26, 0x71},	/* vpt */
	{0x27, 0x08},	/* bbias */
	{0x28, 0x08},	/* gbbias */
	{0x29, 0x15},	/* gr com */
	{0x2a, 0x00},	/* exhch */
	{0x2b, 0x00},	/* exhcl */
	{0x2c, 0x08},	/* rbias */
	{0x32, 0xff},	/* href */
	{0x33, 0x00},	/* chlf */
	{0x34, 0x3f},	/* aref1 */
	{0x35, 0x00},	/* aref2 */
	{0x36, 0xf8},	/* aref3 */
	{0x38, 0x72},	/* adc2 */
	{0x39, 0x57},	/* aref4 */
	{0x3a, 0x80},	/* tslb - yuyv */
	{0x3b, 0xc4},	/* com11 - night mode 1/4 frame rate */
	{0x3d, 0x99},	/* com13 */
	{0x3f, 0xc1},	/* edge */
	{0x40, 0xc0},	/* com15 */
	{0x41, 0x40},	/* com16 */
	{0x42, 0xc0},	/* com17 */
	{0x43, 0x0a},	/* rsvd */
	{0x44, 0xf0},
	{0x45, 0x46},
	{0x46, 0x62},
	{0x47, 0x2a},
	{0x48, 0x3c},
	{0x4a, 0xfc},
	{0x4b, 0xfc},
	{0x4c, 0x7f},
	{0x4d, 0x7f},
	{0x4e, 0x7f},
	{0x4f, 0x98},	/* matrix */
	{0x50, 0x98},
	{0x51, 0x00},
	{0x52, 0x28},
	{0x53, 0x70},
	{0x54, 0x98},
	{0x58, 0x1a},	/* matrix coef sign */
	{0x59, 0x85},	/* AWB control */
	{0x5a, 0xa9},
	{0x5b, 0x64},
	{0x5c, 0x84},
	{0x5d, 0x53},
	{0x5e, 0x0e},
	{0x5f, 0xf0},	/* AWB blue limit */
	{0x60, 0xf0},	/* AWB red limit */
	{0x61, 0xf0},	/* AWB green limit */
	{0x62, 0x00},	/* lcc1 */
	{0x63, 0x00},	/* lcc2 */
	{0x64, 0x02},	/* lcc3 */
	{0x65, 0x16},	/* lcc4 */
	{0x66, 0x01},	/* lcc5 */
	{0x69, 0x02},	/* hv */
	{0x6b, 0x5a},	/* dbvl */
	{0x6c, 0x04},
	{0x6d, 0x55},
	{0x6e, 0x00},
	{0x6f, 0x9d},
	{0x70, 0x21},	/* dnsth */
	{0x71, 0x78},
	{0x72, 0x00},	/* poidx */
	{0x73, 0x01},	/* pckdv */
	{0x74, 0x3a},	/* xindx */
	{0x75, 0x35},	/* yindx */
	{0x76, 0x01},
	{0x77, 0x02},
	{0x7a, 0x12},	/* gamma curve */
	{0x7b, 0x08},
	{0x7c, 0x16},
	{0x7d, 0x30},
	{0x7e, 0x5e},
	{0x7f, 0x72},
	{0x80, 0x82},
	{0x81, 0x8e},
	{0x82, 0x9a},
	{0x83, 0xa4},
	{0x84, 0xac},
	{0x85, 0xb8},
	{0x86, 0xc3},
	{0x87, 0xd6},
	{0x88, 0xe6},
	{0x89, 0xf2},
	{0x8a, 0x03},
	{0x8c, 0x89},	/* com19 */
	{0x14, 0x28},	/* com9 */
	{0x90, 0x7d},
	{0x91, 0x7b},
	{0x9d, 0x03},	/* lcc6 */
	{0x9e, 0x04},	/* lcc7 */
	{0x9f, 0x7a},
	{0xa0, 0x79},
	{0xa1, 0x40},	/* aechm */
	{0xa4, 0x50},	/* com21 */
	{0xa5, 0x68},	/* com26 */
	{0xa6, 0x4a},	/* AWB green */
	{0xa8, 0xc1},	/* refa8 */
	{0xa9, 0xef},	/* refa9 */
	{0xaa, 0x92},
	{0xab, 0x04},
	{0xac, 0x80},	/* black level control */
	{0xad, 0x80},
	{0xae, 0x80},
	{0xaf, 0x80},
	{0xb2, 0xf2},
	{0xb3, 0x20},
	{0xb4, 0x20},	/* ctrlb4 */
	{0xb5, 0x00},
	{0xb6, 0xaf},
	{0xbb, 0xae},
	{0xbc, 0x7f},	/* ADC channel offsets */
	{0xdb, 0x7f},
	{0xbe, 0x7f},
	{0xbf, 0x7f},
	{0xc0, 0xe2},
	{0xc1, 0xc0},
	{0xc2, 0x01},
	{0xc3, 0x4e},
	{0xc6, 0x85},
	{0xc7, 0x80},	/* com24 */
	{0xc9, 0xe0},
	{0xca, 0xe8},
	{0xcb, 0xf0},
	{0xcc, 0xd8},
	{0xcd, 0xf1},
	{0x4f, 0x98},	/* matrix */
	{0x50, 0x98},
	{0x51, 0x00},
	{0x52, 0x28},
	{0x53, 0x70},
	{0x54, 0x98},
	{0x58, 0x1a},
	{0xff, 0x41},	/* read 41, write ff 00 */
	{0x41, 0x40},	/* com16 */

	{0xc5, 0x03},	/* 60 Hz banding filter */
	{0x6a, 0x02},	/* 50 Hz banding filter */

	{0x12, 0x62},	/* com7 - 30fps VGA YUV */
	{0x36, 0xfa},	/* aref3 */
	{0x69, 0x0a},	/* hv */
	{0x8c, 0x89},	/* com22 */
	{0x14, 0x28},	/* com9 */
	{0x3e, 0x0c},
	{0x41, 0x40},	/* com16 */
	{0x72, 0x00},
	{0x73, 0x00},
	{0x74, 0x3a},
	{0x75, 0x35},
	{0x76, 0x01},
	{0xc7, 0x80},
	{0x03, 0x12},	/* vref */
	{0x17, 0x16},	/* hstart */
	{0x18, 0x02},	/* hstop */
	{0x19, 0x01},	/* vstrt */
	{0x1a, 0x3d},	/* vstop */
	{0x32, 0xff},	/* href */
	{0xc0, 0xaa},
};

static const u8 bridge_init_2[][2] = {
	{0x94, 0xaa},
	{0xf1, 0x60},
	{0xe5, 0x04},
	{0xc0, 0x50},
	{0xc1, 0x3c},
	{0x8c, 0x00},
	{0x8d, 0x1c},
	{0x34, 0x05},

	{0xc2, 0x0c},
	{0xc3, 0xf9},
	{0xda, 0x01},
	{0x50, 0x00},
	{0x51, 0xa0},
	{0x52, 0x3c},
	{0x53, 0x00},
	{0x54, 0x00},
	{0x55, 0x00},
	{0x57, 0x00},
	{0x5c, 0x00},
	{0x5a, 0xa0},
	{0x5b, 0x78},
	{0x35, 0x02},
	{0xd9, 0x10},
	{0x94, 0x11},
};

static const u8 ov965x_init_2[][2] = {
	{0x3b, 0xc4},
	{0x1e, 0x04},	/* mvfp */
	{0x13, 0xe0},	/* com8 */
	{0x00, 0x00},	/* gain */
	{0x13, 0xe7},	/* com8 - everything (AGC, AWB and AEC) */
	{0x11, 0x03},	/* clkrc */
	{0x6b, 0x5a},	/* dblv */
	{0x6a, 0x05},
	{0xc5, 0x07},
	{0xa2, 0x4b},
	{0xa3, 0x3e},
	{0x2d, 0x00},
	{0xff, 0x42},	/* read 42, write ff 00 */
	{0x42, 0xc0},	/* com17 */
	{0x2d, 0x00},
	{0xff, 0x42},	/* read 42, write ff 00 */
	{0x42, 0xc1},	/* com17 */
/* sharpness */
	{0x3f, 0x01},
	{0xff, 0x42},	/* read 42, write ff 00 */
	{0x42, 0xc1},	/* com17 */
/* saturation */
	{0x4f, 0x98},	/* matrix */
	{0x50, 0x98},
	{0x51, 0x00},
	{0x52, 0x28},
	{0x53, 0x70},
	{0x54, 0x98},
	{0x58, 0x1a},
	{0xff, 0x41},	/* read 41, write ff 00 */
	{0x41, 0x40},	/* com16 */
/* contrast */
	{0x56, 0x40},
/* brightness */
	{0x55, 0x8f},
/* expo */
	{0x10, 0x25},	/* aech - exposure high bits */
	{0xff, 0x13},	/* read 13, write ff 00 */
	{0x13, 0xe7},	/* com8 - everything (AGC, AWB and AEC) */
};

static const u8 ov971x_init[][2] = {
	{0x12, 0x80},
	{0x09, 0x10},
	{0x1e, 0x07},
	{0x5f, 0x18},
	{0x69, 0x04},
	{0x65, 0x2a},
	{0x68, 0x0a},
	{0x39, 0x28},
	{0x4d, 0x90},
	{0xc1, 0x80},
	{0x0c, 0x30},
	{0x6d, 0x02},
	{0x96, 0xf1},
	{0xbc, 0x68},
	{0x12, 0x00},
	{0x3b, 0x00},
	{0x97, 0x80},
	{0x17, 0x25},
	{0x18, 0xa2},
	{0x19, 0x01},
	{0x1a, 0xca},
	{0x03, 0x0a},
	{0x32, 0x07},
	{0x98, 0x40},	/*{0x98, 0x00},*/
	{0x99, 0xA0},	/*{0x99, 0x00},*/
	{0x9a, 0x01},	/*{0x9a, 0x00},*/
	{0x57, 0x00},
	{0x58, 0x78},	/*{0x58, 0xc8},*/
	{0x59, 0x50},	/*{0x59, 0xa0},*/
	{0x4c, 0x13},
	{0x4b, 0x36},
	{0x3d, 0x3c},
	{0x3e, 0x03},
	{0xbd, 0x50},	/*{0xbd, 0xa0},*/
	{0xbe, 0x78},	/*{0xbe, 0xc8},*/
	{0x4e, 0x55},
	{0x4f, 0x55},
	{0x50, 0x55},
	{0x51, 0x55},
	{0x24, 0x55},
	{0x25, 0x40},
	{0x26, 0xa1},
	{0x5c, 0x59},
	{0x5d, 0x00},
	{0x11, 0x00},
	{0x2a, 0x98},
	{0x2b, 0x06},
	{0x2d, 0x00},
	{0x2e, 0x00},
	{0x13, 0xa5},
	{0x14, 0x40},
	{0x4a, 0x00},
	{0x49, 0xce},
	{0x22, 0x03},
	{0x09, 0x00}
};

static const u8 ov965x_start_1_vga[][2] = {	/* same for qvga */
	{0x12, 0x62},	/* com7 - 30fps VGA YUV */
	{0x36, 0xfa},	/* aref3 */
	{0x69, 0x0a},	/* hv */
	{0x8c, 0x89},	/* com22 */
	{0x14, 0x28},	/* com9 */
	{0x3e, 0x0c},	/* com14 */
	{0x41, 0x40},	/* com16 */
	{0x72, 0x00},
	{0x73, 0x00},
	{0x74, 0x3a},
	{0x75, 0x35},
	{0x76, 0x01},
	{0xc7, 0x80},	/* com24 */
	{0x03, 0x12},	/* vref */
	{0x17, 0x16},	/* hstart */
	{0x18, 0x02},	/* hstop */
	{0x19, 0x01},	/* vstrt */
	{0x1a, 0x3d},	/* vstop */
	{0x32, 0xff},	/* href */
	{0xc0, 0xaa},
};

static const u8 ov965x_start_1_svga[][2] = {
	{0x12, 0x02},	/* com7 - YUYV - VGA 15 full resolution */
	{0x36, 0xf8},	/* aref3 */
	{0x69, 0x02},	/* hv */
	{0x8c, 0x0d},	/* com22 */
	{0x3e, 0x0c},	/* com14 */
	{0x41, 0x40},	/* com16 */
	{0x72, 0x00},
	{0x73, 0x01},
	{0x74, 0x3a},
	{0x75, 0x35},
	{0x76, 0x01},
	{0xc7, 0x80},	/* com24 */
	{0x03, 0x1b},	/* vref */
	{0x17, 0x1d},	/* hstart */
	{0x18, 0xbd},	/* hstop */
	{0x19, 0x01},	/* vstrt */
	{0x1a, 0x81},	/* vstop */
	{0x32, 0xff},	/* href */
	{0xc0, 0xe2},
};

static const u8 ov965x_start_1_xga[][2] = {
	{0x12, 0x02},	/* com7 */
	{0x36, 0xf8},	/* aref3 */
	{0x69, 0x02},	/* hv */
	{0x8c, 0x89},	/* com22 */
	{0x14, 0x28},	/* com9 */
	{0x3e, 0x0c},	/* com14 */
	{0x41, 0x40},	/* com16 */
	{0x72, 0x00},
	{0x73, 0x01},
	{0x74, 0x3a},
	{0x75, 0x35},
	{0x76, 0x01},
	{0xc7, 0x80},	/* com24 */
	{0x03, 0x1b},	/* vref */
	{0x17, 0x1d},	/* hstart */
	{0x18, 0xbd},	/* hstop */
	{0x19, 0x01},	/* vstrt */
	{0x1a, 0x81},	/* vstop */
	{0x32, 0xff},	/* href */
	{0xc0, 0xe2},
};

static const u8 ov965x_start_1_sxga[][2] = {
	{0x12, 0x02},	/* com7 */
	{0x36, 0xf8},	/* aref3 */
	{0x69, 0x02},	/* hv */
	{0x8c, 0x89},	/* com22 */
	{0x14, 0x28},	/* com9 */
	{0x3e, 0x0c},	/* com14 */
	{0x41, 0x40},	/* com16 */
	{0x72, 0x00},
	{0x73, 0x01},
	{0x74, 0x3a},
	{0x75, 0x35},
	{0x76, 0x01},
	{0xc7, 0x80},	/* com24 */
	{0x03, 0x1b},	/* vref */
	{0x17, 0x1d},	/* hstart */
	{0x18, 0x02},	/* hstop */
	{0x19, 0x01},	/* vstrt */
	{0x1a, 0x81},	/* vstop */
	{0x32, 0xff},	/* href */
	{0xc0, 0xe2},
};

static const u8 bridge_start_qvga[][2] = {
	{0x94, 0xaa},
	{0xf1, 0x60},
	{0xe5, 0x04},
	{0xc0, 0x50},
	{0xc1, 0x3c},
	{0x8c, 0x00},
	{0x8d, 0x1c},
	{0x34, 0x05},

	{0xc2, 0x4c},
	{0xc3, 0xf9},
	{0xda, 0x00},
	{0x50, 0x00},
	{0x51, 0xa0},
	{0x52, 0x78},
	{0x53, 0x00},
	{0x54, 0x00},
	{0x55, 0x00},
	{0x57, 0x00},
	{0x5c, 0x00},
	{0x5a, 0x50},
	{0x5b, 0x3c},
	{0x35, 0x02},
	{0xd9, 0x10},
	{0x94, 0x11},
};

static const u8 bridge_start_vga[][2] = {
	{0x94, 0xaa},
	{0xf1, 0x60},
	{0xe5, 0x04},
	{0xc0, 0x50},
	{0xc1, 0x3c},
	{0x8c, 0x00},
	{0x8d, 0x1c},
	{0x34, 0x05},
	{0xc2, 0x0c},
	{0xc3, 0xf9},
	{0xda, 0x01},
	{0x50, 0x00},
	{0x51, 0xa0},
	{0x52, 0x3c},
	{0x53, 0x00},
	{0x54, 0x00},
	{0x55, 0x00},
	{0x57, 0x00},
	{0x5c, 0x00},
	{0x5a, 0xa0},
	{0x5b, 0x78},
	{0x35, 0x02},
	{0xd9, 0x10},
	{0x94, 0x11},
};

static const u8 bridge_start_svga[][2] = {
	{0x94, 0xaa},
	{0xf1, 0x60},
	{0xe5, 0x04},
	{0xc0, 0xa0},
	{0xc1, 0x80},
	{0x8c, 0x00},
	{0x8d, 0x1c},
	{0x34, 0x05},
	{0xc2, 0x4c},
	{0xc3, 0xf9},
	{0x50, 0x00},
	{0x51, 0x40},
	{0x52, 0x00},
	{0x53, 0x00},
	{0x54, 0x00},
	{0x55, 0x88},
	{0x57, 0x00},
	{0x5c, 0x00},
	{0x5a, 0xc8},
	{0x5b, 0x96},
	{0x35, 0x02},
	{0xd9, 0x10},
	{0xda, 0x00},
	{0x94, 0x11},
};

static const u8 bridge_start_xga[][2] = {
	{0x94, 0xaa},
	{0xf1, 0x60},
	{0xe5, 0x04},
	{0xc0, 0xa0},
	{0xc1, 0x80},
	{0x8c, 0x00},
	{0x8d, 0x1c},
	{0x34, 0x05},
	{0xc2, 0x4c},
	{0xc3, 0xf9},
	{0x50, 0x00},
	{0x51, 0x40},
	{0x52, 0x00},
	{0x53, 0x00},
	{0x54, 0x00},
	{0x55, 0x88},
	{0x57, 0x00},
	{0x5c, 0x01},
	{0x5a, 0x00},
	{0x5b, 0xc0},
	{0x35, 0x02},
	{0xd9, 0x10},
	{0xda, 0x01},
	{0x94, 0x11},
};

static const u8 bridge_start_sxga[][2] = {
	{0x94, 0xaa},
	{0xf1, 0x60},
	{0xe5, 0x04},
	{0xc0, 0xa0},
	{0xc1, 0x80},
	{0x8c, 0x00},
	{0x8d, 0x1c},
	{0x34, 0x05},
	{0xc2, 0x0c},
	{0xc3, 0xf9},
	{0xda, 0x00},
	{0x35, 0x02},
	{0xd9, 0x10},
	{0x94, 0x11},
};

static const u8 ov965x_start_2_qvga[][2] = {
	{0x3b, 0xe4},	/* com11 - night mode 1/4 frame rate */
	{0x1e, 0x04},	/* mvfp */
	{0x13, 0xe0},	/* com8 */
	{0x00, 0x00},
	{0x13, 0xe7},	/* com8 - everything (AGC, AWB and AEC) */
	{0x11, 0x01},	/* clkrc */
	{0x6b, 0x5a},	/* dblv */
	{0x6a, 0x02},	/* 50 Hz banding filter */
	{0xc5, 0x03},	/* 60 Hz banding filter */
	{0xa2, 0x96},	/* bd50 */
	{0xa3, 0x7d},	/* bd60 */

	{0xff, 0x13},	/* read 13, write ff 00 */
	{0x13, 0xe7},
	{0x3a, 0x80},	/* tslb - yuyv */
};

static const u8 ov965x_start_2_vga[][2] = {
	{0x3b, 0xc4},	/* com11 - night mode 1/4 frame rate */
	{0x1e, 0x04},	/* mvfp */
	{0x13, 0xe0},	/* com8 */
	{0x00, 0x00},
	{0x13, 0xe7},	/* com8 - everything (AGC, AWB and AEC) */
	{0x11, 0x03},	/* clkrc */
	{0x6b, 0x5a},	/* dblv */
	{0x6a, 0x05},	/* 50 Hz banding filter */
	{0xc5, 0x07},	/* 60 Hz banding filter */
	{0xa2, 0x4b},	/* bd50 */
	{0xa3, 0x3e},	/* bd60 */

	{0x2d, 0x00},	/* advfl */
};

static const u8 ov965x_start_2_svga[][2] = {	/* same for xga */
	{0x3b, 0xc4},	/* com11 - night mode 1/4 frame rate */
	{0x1e, 0x04},	/* mvfp */
	{0x13, 0xe0},	/* com8 */
	{0x00, 0x00},
	{0x13, 0xe7},	/* com8 - everything (AGC, AWB and AEC) */
	{0x11, 0x01},	/* clkrc */
	{0x6b, 0x5a},	/* dblv */
	{0x6a, 0x0c},	/* 50 Hz banding filter */
	{0xc5, 0x0f},	/* 60 Hz banding filter */
	{0xa2, 0x4e},	/* bd50 */
	{0xa3, 0x41},	/* bd60 */
};

static const u8 ov965x_start_2_sxga[][2] = {
	{0x13, 0xe0},	/* com8 */
	{0x00, 0x00},
	{0x13, 0xe7},	/* com8 - everything (AGC, AWB and AEC) */
	{0x3b, 0xc4},	/* com11 - night mode 1/4 frame rate */
	{0x1e, 0x04},	/* mvfp */
	{0x11, 0x01},	/* clkrc */
	{0x6b, 0x5a},	/* dblv */
	{0x6a, 0x0c},	/* 50 Hz banding filter */
	{0xc5, 0x0f},	/* 60 Hz banding filter */
	{0xa2, 0x4e},	/* bd50 */
	{0xa3, 0x41},	/* bd60 */
};

static const u8 ov562x_init[][2] = {
	{0x88, 0x20},
	{0x89, 0x0a},
	{0x8a, 0x90},
	{0x8b, 0x06},
	{0x8c, 0x01},
	{0x8d, 0x10},
	{0x1c, 0x00},
	{0x1d, 0x48},
	{0x1d, 0x00},
	{0x1d, 0xff},
	{0x1c, 0x0a},
	{0x1d, 0x2e},
	{0x1d, 0x1e},
};

static const u8 ov562x_init_2[][2] = {
	{0x12, 0x80},
	{0x11, 0x41},
	{0x13, 0x00},
	{0x10, 0x1e},
	{0x3b, 0x07},
	{0x5b, 0x40},
	{0x39, 0x07},
	{0x53, 0x02},
	{0x54, 0x60},
	{0x04, 0x20},
	{0x27, 0x04},
	{0x3d, 0x40},
	{0x36, 0x00},
	{0xc5, 0x04},
	{0x4e, 0x00},
	{0x4f, 0x93},
	{0x50, 0x7b},
	{0xca, 0x0c},
	{0xcb, 0x0f},
	{0x39, 0x07},
	{0x4a, 0x10},
	{0x3e, 0x0a},
	{0x3d, 0x00},
	{0x0c, 0x38},
	{0x38, 0x90},
	{0x46, 0x30},
	{0x4f, 0x93},
	{0x50, 0x7b},
	{0xab, 0x00},
	{0xca, 0x0c},
	{0xcb, 0x0f},
	{0x37, 0x02},
	{0x44, 0x48},
	{0x8d, 0x44},
	{0x2a, 0x00},
	{0x2b, 0x00},
	{0x32, 0x00},
	{0x38, 0x90},
	{0x53, 0x02},
	{0x54, 0x60},
	{0x12, 0x00},
	{0x17, 0x12},
	{0x18, 0xb4},
	{0x19, 0x0c},
	{0x1a, 0xf4},
	{0x03, 0x4a},
	{0x89, 0x20},
	{0x83, 0x80},
	{0xb7, 0x9d},
	{0xb6, 0x11},
	{0xb5, 0x55},
	{0xb4, 0x00},
	{0xa9, 0xf0},
	{0xa8, 0x0a},
	{0xb8, 0xf0},
	{0xb9, 0xf0},
	{0xba, 0xf0},
	{0x81, 0x07},
	{0x63, 0x44},
	{0x13, 0xc7},
	{0x14, 0x60},
	{0x33, 0x75},
	{0x2c, 0x00},
	{0x09, 0x00},
	{0x35, 0x30},
	{0x27, 0x04},
	{0x3c, 0x07},
	{0x3a, 0x0a},
	{0x3b, 0x07},
	{0x01, 0x40},
	{0x02, 0x40},
	{0x16, 0x40},
	{0x52, 0xb0},
	{0x51, 0x83},
	{0x21, 0xbb},
	{0x22, 0x10},
	{0x23, 0x03},
	{0x35, 0x38},
	{0x20, 0x90},
	{0x28, 0x30},
	{0x73, 0xe1},
	{0x6c, 0x00},
	{0x6d, 0x80},
	{0x6e, 0x00},
	{0x70, 0x04},
	{0x71, 0x00},
	{0x8d, 0x04},
	{0x64, 0x00},
	{0x65, 0x00},
	{0x66, 0x00},
	{0x67, 0x00},
	{0x68, 0x00},
	{0x69, 0x00},
	{0x6a, 0x00},
	{0x6b, 0x00},
	{0x71, 0x94},
	{0x74, 0x20},
	{0x80, 0x09},
	{0x85, 0xc0},
};

static void reg_w_i(struct gspca_dev *gspca_dev, u16 reg, u8 val)
{
	struct usb_device *udev = gspca_dev->dev;
	int ret;

	if (gspca_dev->usb_err < 0)
		return;
	gspca_dev->usb_buf[0] = val;
	ret = usb_control_msg(udev,
			      usb_sndctrlpipe(udev, 0),
			      0x01,
			      USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			      0x00, reg, gspca_dev->usb_buf, 1, CTRL_TIMEOUT);
	if (ret < 0) {
		pr_err("reg_w failed %d\n", ret);
		gspca_dev->usb_err = ret;
	}
}

static void reg_w(struct gspca_dev *gspca_dev, u16 reg, u8 val)
{
	PDEBUG(D_USBO, "reg_w [%04x] = %02x", reg, val);
	reg_w_i(gspca_dev, reg, val);
}

static u8 reg_r(struct gspca_dev *gspca_dev, u16 reg)
{
	struct usb_device *udev = gspca_dev->dev;
	int ret;

	if (gspca_dev->usb_err < 0)
		return 0;
	ret = usb_control_msg(udev,
			      usb_rcvctrlpipe(udev, 0),
			      0x01,
			      USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			      0x00, reg, gspca_dev->usb_buf, 1, CTRL_TIMEOUT);
	PDEBUG(D_USBI, "reg_r [%04x] -> %02x", reg, gspca_dev->usb_buf[0]);
	if (ret < 0) {
		pr_err("reg_r err %d\n", ret);
		gspca_dev->usb_err = ret;
	}
	return gspca_dev->usb_buf[0];
}

static int sccb_check_status(struct gspca_dev *gspca_dev)
{
	u8 data;
	int i;

	for (i = 0; i < 5; i++) {
		data = reg_r(gspca_dev, OV534_REG_STATUS);

		switch (data) {
		case 0x00:
			return 1;
		case 0x04:
			return 0;
		case 0x03:
			break;
		default:
			PDEBUG(D_USBI|D_USBO,
				"sccb status 0x%02x, attempt %d/5",
				data, i + 1);
		}
	}
	return 0;
}

static void sccb_write(struct gspca_dev *gspca_dev, u8 reg, u8 val)
{
	PDEBUG(D_USBO, "sccb_write [%02x] = %02x", reg, val);
	reg_w_i(gspca_dev, OV534_REG_SUBADDR, reg);
	reg_w_i(gspca_dev, OV534_REG_WRITE, val);
	reg_w_i(gspca_dev, OV534_REG_OPERATION, OV534_OP_WRITE_3);

	if (!sccb_check_status(gspca_dev))
		pr_err("sccb_write failed\n");
}

static u8 sccb_read(struct gspca_dev *gspca_dev, u16 reg)
{
	reg_w(gspca_dev, OV534_REG_SUBADDR, reg);
	reg_w(gspca_dev, OV534_REG_OPERATION, OV534_OP_WRITE_2);
	if (!sccb_check_status(gspca_dev))
		pr_err("sccb_read failed 1\n");

	reg_w(gspca_dev, OV534_REG_OPERATION, OV534_OP_READ_2);
	if (!sccb_check_status(gspca_dev))
		pr_err("sccb_read failed 2\n");

	return reg_r(gspca_dev, OV534_REG_READ);
}

/* output a bridge sequence (reg - val) */
static void reg_w_array(struct gspca_dev *gspca_dev,
			const u8 (*data)[2], int len)
{
	while (--len >= 0) {
		reg_w(gspca_dev, (*data)[0], (*data)[1]);
		data++;
	}
}

/* output a sensor sequence (reg - val) */
static void sccb_w_array(struct gspca_dev *gspca_dev,
			const u8 (*data)[2], int len)
{
	while (--len >= 0) {
		if ((*data)[0] != 0xff) {
			sccb_write(gspca_dev, (*data)[0], (*data)[1]);
		} else {
			sccb_read(gspca_dev, (*data)[1]);
			sccb_write(gspca_dev, 0xff, 0x00);
		}
		data++;
	}
}

/* Two bits control LED: 0x21 bit 7 and 0x23 bit 7.
 * (direction and output)? */
static void set_led(struct gspca_dev *gspca_dev, int status)
{
	u8 data;

	PDEBUG(D_CONF, "led status: %d", status);

	data = reg_r(gspca_dev, 0x21);
	data |= 0x80;
	reg_w(gspca_dev, 0x21, data);

	data = reg_r(gspca_dev, 0x23);
	if (status)
		data |= 0x80;
	else
		data &= ~0x80;

	reg_w(gspca_dev, 0x23, data);

	if (!status) {
		data = reg_r(gspca_dev, 0x21);
		data &= ~0x80;
		reg_w(gspca_dev, 0x21, data);
	}
}

static void setbrightness(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 val;

	if (gspca_dev->ctrl_dis & (1 << BRIGHTNESS))
		return;
	val = sd->ctrls[BRIGHTNESS].val;
	if (val < 8)
		val = 15 - val;		/* f .. 8 */
	else
		val = val - 8;		/* 0 .. 7 */
	sccb_write(gspca_dev, 0x55,	/* brtn - brightness adjustment */
			0x0f | (val << 4));
}

static void setcontrast(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (gspca_dev->ctrl_dis & (1 << CONTRAST))
		return;
	sccb_write(gspca_dev, 0x56,	/* cnst1 - contrast 1 ctrl coeff */
			sd->ctrls[CONTRAST].val << 4);
}

static void setautogain(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 val;

	if (gspca_dev->ctrl_dis & (1 << AUTOGAIN))
		return;
/*fixme: should adjust agc/awb/aec by different controls */
	val = sccb_read(gspca_dev, 0x13);		/* com8 */
	sccb_write(gspca_dev, 0xff, 0x00);
	if (sd->ctrls[AUTOGAIN].val)
		val |= 0x05;		/* agc & aec */
	else
		val &= 0xfa;
	sccb_write(gspca_dev, 0x13, val);
}

static void setexposure(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 val;
	static const u8 expo[4] = {0x00, 0x25, 0x38, 0x5e};

	if (gspca_dev->ctrl_dis & (1 << EXPOSURE))
		return;
	sccb_write(gspca_dev, 0x10,			/* aec[9:2] */
			expo[sd->ctrls[EXPOSURE].val]);

	val = sccb_read(gspca_dev, 0x13);		/* com8 */
	sccb_write(gspca_dev, 0xff, 0x00);
	sccb_write(gspca_dev, 0x13, val);

	val = sccb_read(gspca_dev, 0xa1);		/* aech */
	sccb_write(gspca_dev, 0xff, 0x00);
	sccb_write(gspca_dev, 0xa1, val & 0xe0);	/* aec[15:10] = 0 */
}

static void setsharpness(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s8 val;

	if (gspca_dev->ctrl_dis & (1 << SHARPNESS))
		return;
	val = sd->ctrls[SHARPNESS].val;
	if (val < 0) {				/* auto */
		val = sccb_read(gspca_dev, 0x42);	/* com17 */
		sccb_write(gspca_dev, 0xff, 0x00);
		sccb_write(gspca_dev, 0x42, val | 0x40);
				/* Edge enhancement strength auto adjust */
		return;
	}
	if (val != 0)
		val = 1 << (val - 1);
	sccb_write(gspca_dev, 0x3f,	/* edge - edge enhance. factor */
			val);
	val = sccb_read(gspca_dev, 0x42);		/* com17 */
	sccb_write(gspca_dev, 0xff, 0x00);
	sccb_write(gspca_dev, 0x42, val & 0xbf);
}

static void setsatur(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 val1, val2, val3;
	static const u8 matrix[5][2] = {
		{0x14, 0x38},
		{0x1e, 0x54},
		{0x28, 0x70},
		{0x32, 0x8c},
		{0x48, 0x90}
	};

	if (gspca_dev->ctrl_dis & (1 << SATUR))
		return;
	val1 = matrix[sd->ctrls[SATUR].val][0];
	val2 = matrix[sd->ctrls[SATUR].val][1];
	val3 = val1 + val2;
	sccb_write(gspca_dev, 0x4f, val3);	/* matrix coeff */
	sccb_write(gspca_dev, 0x50, val3);
	sccb_write(gspca_dev, 0x51, 0x00);
	sccb_write(gspca_dev, 0x52, val1);
	sccb_write(gspca_dev, 0x53, val2);
	sccb_write(gspca_dev, 0x54, val3);
	sccb_write(gspca_dev, 0x58, 0x1a);	/* mtxs - coeff signs */

	val1 = sccb_read(gspca_dev, 0x41);	/* com16 */
	sccb_write(gspca_dev, 0xff, 0x00);
	sccb_write(gspca_dev, 0x41, val1);
}

static void setlightfreq(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 val;

	if (gspca_dev->ctrl_dis & (1 << LIGHTFREQ))
		return;
	val = sccb_read(gspca_dev, 0x13);		/* com8 */
	sccb_write(gspca_dev, 0xff, 0x00);
	if (sd->ctrls[LIGHTFREQ].val == 0) {
		sccb_write(gspca_dev, 0x13, val & 0xdf);
		return;
	}
	sccb_write(gspca_dev, 0x13, val | 0x20);

	val = sccb_read(gspca_dev, 0x42);		/* com17 */
	sccb_write(gspca_dev, 0xff, 0x00);
	if (sd->ctrls[LIGHTFREQ].val == 1)
		val |= 0x01;
	else
		val &= 0xfe;
	sccb_write(gspca_dev, 0x42, val);
}

/* this function is called at probe time */
static int sd_config(struct gspca_dev *gspca_dev,
		     const struct usb_device_id *id)
{
	struct sd *sd = (struct sd *) gspca_dev;

	gspca_dev->cam.ctrls = sd->ctrls;

#if AUTOGAIN_DEF != 0
	gspca_dev->ctrl_inac |= (1 << EXPOSURE);
#endif
	return 0;
}

/* this function is called at probe and resume time */
static int sd_init(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u16 sensor_id;

	/* reset bridge */
	reg_w(gspca_dev, 0xe7, 0x3a);
	reg_w(gspca_dev, 0xe0, 0x08);
	msleep(100);

	/* initialize the sensor address */
	reg_w(gspca_dev, OV534_REG_ADDRESS, 0x60);

	/* reset sensor */
	sccb_write(gspca_dev, 0x12, 0x80);
	msleep(10);

	/* probe the sensor */
	sccb_read(gspca_dev, 0x0a);
	sensor_id = sccb_read(gspca_dev, 0x0a) << 8;
	sccb_read(gspca_dev, 0x0b);
	sensor_id |= sccb_read(gspca_dev, 0x0b);
	PDEBUG(D_PROBE, "Sensor ID: %04x", sensor_id);

	/* initialize */
	if ((sensor_id & 0xfff0) == 0x9650) {
		sd->sensor = SENSOR_OV965x;

		gspca_dev->cam.cam_mode = ov965x_mode;
		gspca_dev->cam.nmodes = ARRAY_SIZE(ov965x_mode);

		reg_w_array(gspca_dev, bridge_init,
				ARRAY_SIZE(bridge_init));
		sccb_w_array(gspca_dev, ov965x_init,
				ARRAY_SIZE(ov965x_init));
		reg_w_array(gspca_dev, bridge_init_2,
				ARRAY_SIZE(bridge_init_2));
		sccb_w_array(gspca_dev, ov965x_init_2,
				ARRAY_SIZE(ov965x_init_2));
		reg_w(gspca_dev, 0xe0, 0x00);
		reg_w(gspca_dev, 0xe0, 0x01);
		set_led(gspca_dev, 0);
		reg_w(gspca_dev, 0xe0, 0x00);
	} else if ((sensor_id & 0xfff0) == 0x9710) {
		const char *p;
		int l;

		sd->sensor = SENSOR_OV971x;

		gspca_dev->cam.cam_mode = ov971x_mode;
		gspca_dev->cam.nmodes = ARRAY_SIZE(ov971x_mode);

		/* no control yet */
		gspca_dev->ctrl_dis = (1 << NCTRLS) - 1;

		gspca_dev->cam.bulk = 1;
		gspca_dev->cam.bulk_size = 16384;
		gspca_dev->cam.bulk_nurbs = 2;

		sccb_w_array(gspca_dev, ov971x_init,
				ARRAY_SIZE(ov971x_init));

		/* set video format on bridge processor */
		/* access bridge processor's video format registers at: 0x00 */
		reg_w(gspca_dev, 0x1c, 0x00);
		/*set register: 0x00 is 'RAW8', 0x40 is 'YUV422' (YUYV?)*/
		reg_w(gspca_dev, 0x1d, 0x00);

		/* Will W. specific stuff
		 * set VSYNC to
		 *	output (0x1f) if first webcam
		 *	input (0x17) if 2nd or 3rd webcam */
		p = video_device_node_name(&gspca_dev->vdev);
		l = strlen(p) - 1;
		if (p[l] == '0')
			reg_w(gspca_dev, 0x56, 0x1f);
		else
			reg_w(gspca_dev, 0x56, 0x17);
	} else if ((sensor_id & 0xfff0) == 0x5620) {
		sd->sensor = SENSOR_OV562x;

		gspca_dev->cam.cam_mode = ov562x_mode;
		gspca_dev->cam.nmodes = ARRAY_SIZE(ov562x_mode);

		reg_w_array(gspca_dev, ov562x_init,
				ARRAY_SIZE(ov562x_init));
		sccb_w_array(gspca_dev, ov562x_init_2,
				ARRAY_SIZE(ov562x_init_2));
		reg_w(gspca_dev, 0xe0, 0x00);
	} else {
		err("Unknown sensor %04x", sensor_id);
		return -EINVAL;
	}

	return gspca_dev->usb_err;
}

static int sd_start(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (sd->sensor == SENSOR_OV971x || sd->sensor == SENSOR_OV562x)
		return gspca_dev->usb_err;
	switch (gspca_dev->curr_mode) {
	case QVGA_MODE:			/* 320x240 */
		sccb_w_array(gspca_dev, ov965x_start_1_vga,
				ARRAY_SIZE(ov965x_start_1_vga));
		reg_w_array(gspca_dev, bridge_start_qvga,
				ARRAY_SIZE(bridge_start_qvga));
		sccb_w_array(gspca_dev, ov965x_start_2_qvga,
				ARRAY_SIZE(ov965x_start_2_qvga));
		break;
	case VGA_MODE:			/* 640x480 */
		sccb_w_array(gspca_dev, ov965x_start_1_vga,
				ARRAY_SIZE(ov965x_start_1_vga));
		reg_w_array(gspca_dev, bridge_start_vga,
				ARRAY_SIZE(bridge_start_vga));
		sccb_w_array(gspca_dev, ov965x_start_2_vga,
				ARRAY_SIZE(ov965x_start_2_vga));
		break;
	case SVGA_MODE:			/* 800x600 */
		sccb_w_array(gspca_dev, ov965x_start_1_svga,
				ARRAY_SIZE(ov965x_start_1_svga));
		reg_w_array(gspca_dev, bridge_start_svga,
				ARRAY_SIZE(bridge_start_svga));
		sccb_w_array(gspca_dev, ov965x_start_2_svga,
				ARRAY_SIZE(ov965x_start_2_svga));
		break;
	case XGA_MODE:			/* 1024x768 */
		sccb_w_array(gspca_dev, ov965x_start_1_xga,
				ARRAY_SIZE(ov965x_start_1_xga));
		reg_w_array(gspca_dev, bridge_start_xga,
				ARRAY_SIZE(bridge_start_xga));
		sccb_w_array(gspca_dev, ov965x_start_2_svga,
				ARRAY_SIZE(ov965x_start_2_svga));
		break;
	default:
/*	case SXGA_MODE:			 * 1280x1024 */
		sccb_w_array(gspca_dev, ov965x_start_1_sxga,
				ARRAY_SIZE(ov965x_start_1_sxga));
		reg_w_array(gspca_dev, bridge_start_sxga,
				ARRAY_SIZE(bridge_start_sxga));
		sccb_w_array(gspca_dev, ov965x_start_2_sxga,
				ARRAY_SIZE(ov965x_start_2_sxga));
		break;
	}
	setlightfreq(gspca_dev);
	setautogain(gspca_dev);
	setbrightness(gspca_dev);
	setcontrast(gspca_dev);
	setexposure(gspca_dev);
	setsharpness(gspca_dev);
	setsatur(gspca_dev);

	reg_w(gspca_dev, 0xe0, 0x00);
	reg_w(gspca_dev, 0xe0, 0x00);
	set_led(gspca_dev, 1);
	return gspca_dev->usb_err;
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	reg_w(gspca_dev, 0xe0, 0x01);
	set_led(gspca_dev, 0);
	reg_w(gspca_dev, 0xe0, 0x00);
}

/* Values for bmHeaderInfo (Video and Still Image Payload Headers, 2.4.3.3) */
#define UVC_STREAM_EOH	(1 << 7)
#define UVC_STREAM_ERR	(1 << 6)
#define UVC_STREAM_STI	(1 << 5)
#define UVC_STREAM_RES	(1 << 4)
#define UVC_STREAM_SCR	(1 << 3)
#define UVC_STREAM_PTS	(1 << 2)
#define UVC_STREAM_EOF	(1 << 1)
#define UVC_STREAM_FID	(1 << 0)

static void sd_pkt_scan(struct gspca_dev *gspca_dev,
			u8 *data, int len)
{
	struct sd *sd = (struct sd *) gspca_dev;
	__u32 this_pts;
	u8 this_fid;
	int remaining_len = len;
	int payload_len;

	payload_len = gspca_dev->cam.bulk ? 2048 : 2040;
	do {
		len = min(remaining_len, payload_len);

		/* Payloads are prefixed with a UVC-style header.  We
		   consider a frame to start when the FID toggles, or the PTS
		   changes.  A frame ends when EOF is set, and we've received
		   the correct number of bytes. */

		/* Verify UVC header.  Header length is always 12 */
		if (data[0] != 12 || len < 12) {
			PDEBUG(D_PACK, "bad header");
			goto discard;
		}

		/* Check errors */
		if (data[1] & UVC_STREAM_ERR) {
			PDEBUG(D_PACK, "payload error");
			goto discard;
		}

		/* Extract PTS and FID */
		if (!(data[1] & UVC_STREAM_PTS)) {
			PDEBUG(D_PACK, "PTS not present");
			goto discard;
		}
		this_pts = (data[5] << 24) | (data[4] << 16)
						| (data[3] << 8) | data[2];
		this_fid = data[1] & UVC_STREAM_FID;

		/* If PTS or FID has changed, start a new frame. */
		if (this_pts != sd->last_pts || this_fid != sd->last_fid) {
			if (gspca_dev->last_packet_type == INTER_PACKET)
				gspca_frame_add(gspca_dev, LAST_PACKET,
						NULL, 0);
			sd->last_pts = this_pts;
			sd->last_fid = this_fid;
			gspca_frame_add(gspca_dev, FIRST_PACKET,
					data + 12, len - 12);
		/* If this packet is marked as EOF, end the frame */
		} else if (data[1] & UVC_STREAM_EOF) {
			sd->last_pts = 0;
			gspca_frame_add(gspca_dev, LAST_PACKET,
					data + 12, len - 12);
		} else {

			/* Add the data from this payload */
			gspca_frame_add(gspca_dev, INTER_PACKET,
					data + 12, len - 12);
		}

		/* Done this payload */
		goto scan_next;

discard:
		/* Discard data until a new frame starts. */
		gspca_dev->last_packet_type = DISCARD_PACKET;

scan_next:
		remaining_len -= len;
		data += len;
	} while (remaining_len > 0);
}

static int sd_querymenu(struct gspca_dev *gspca_dev,
			struct v4l2_querymenu *menu)
{
	switch (menu->id) {
	case V4L2_CID_POWER_LINE_FREQUENCY:
		switch (menu->index) {
		case 0:		/* V4L2_CID_POWER_LINE_FREQUENCY_DISABLED */
			strcpy((char *) menu->name, "NoFliker");
			return 0;
		case 1:		/* V4L2_CID_POWER_LINE_FREQUENCY_50HZ */
			strcpy((char *) menu->name, "50 Hz");
			return 0;
		case 2:		/* V4L2_CID_POWER_LINE_FREQUENCY_60HZ */
			strcpy((char *) menu->name, "60 Hz");
			return 0;
		}
		break;
	}
	return -EINVAL;
}

/* sub-driver description */
static const struct sd_desc sd_desc = {
	.name     = MODULE_NAME,
	.ctrls    = sd_ctrls,
	.nctrls   = NCTRLS,
	.config   = sd_config,
	.init     = sd_init,
	.start    = sd_start,
	.stopN    = sd_stopN,
	.pkt_scan = sd_pkt_scan,
	.querymenu = sd_querymenu,
};

/* -- module initialisation -- */
static const struct usb_device_id device_table[] = {
	{USB_DEVICE(0x05a9, 0x8065)},
	{USB_DEVICE(0x06f8, 0x3003)},
	{USB_DEVICE(0x05a9, 0x1550)},
	{}
};

MODULE_DEVICE_TABLE(usb, device_table);

/* -- device connect -- */
static int sd_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	return gspca_dev_probe(intf, id, &sd_desc, sizeof(struct sd),
				THIS_MODULE);
}

static struct usb_driver sd_driver = {
	.name       = MODULE_NAME,
	.id_table   = device_table,
	.probe      = sd_probe,
	.disconnect = gspca_disconnect,
#ifdef CONFIG_PM
	.suspend    = gspca_suspend,
	.resume     = gspca_resume,
#endif
};

module_usb_driver(sd_driver);
