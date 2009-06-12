/*
 * ov534 gspca driver
 * Copyright (C) 2008 Antonio Ospite <ospite@studenti.unina.it>
 * Copyright (C) 2008 Jim Paris <jim@jtan.com>
 * Copyright (C) 2009 Jean-Francois Moine http://moinejf.free.fr
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

#define MODULE_NAME "ov534"

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

MODULE_AUTHOR("Antonio Ospite <ospite@studenti.unina.it>");
MODULE_DESCRIPTION("GSPCA/OV534 USB Camera Driver");
MODULE_LICENSE("GPL");

/* specific webcam descriptor */
struct sd {
	struct gspca_dev gspca_dev;	/* !! must be the first item */
	__u32 last_pts;
	u16 last_fid;
	u8 frame_rate;

	u8 sensor;
#define SENSOR_OV772X 0
#define SENSOR_OV965X 1
};

/* V4L2 controls supported by the driver */
static struct ctrl sd_ctrls[] = {
};

static const struct v4l2_pix_format vga_yuyv_mode[] = {
	{640, 480, V4L2_PIX_FMT_YUYV, V4L2_FIELD_NONE,
	 .bytesperline = 640 * 2,
	 .sizeimage = 640 * 480 * 2,
	 .colorspace = V4L2_COLORSPACE_SRGB,
	 .priv = 0},
};

static const struct v4l2_pix_format vga_jpeg_mode[] = {
	{320, 240, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
	 .bytesperline = 320,
	 .sizeimage = 320 * 240 * 3 / 8 + 590,
	 .colorspace = V4L2_COLORSPACE_JPEG,
	 .priv = 1},
	{640, 480, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
	 .bytesperline = 640,
	 .sizeimage = 640 * 480 * 3 / 8 + 590,
	 .colorspace = V4L2_COLORSPACE_JPEG,
	 .priv = 0},
};

static const u8 bridge_init_ov722x[][2] = {
	{ 0xc2, 0x0c },
	{ 0x88, 0xf8 },
	{ 0xc3, 0x69 },
	{ 0x89, 0xff },
	{ 0x76, 0x03 },
	{ 0x92, 0x01 },
	{ 0x93, 0x18 },
	{ 0x94, 0x10 },
	{ 0x95, 0x10 },
	{ 0xe2, 0x00 },
	{ 0xe7, 0x3e },

	{ 0x96, 0x00 },

	{ 0x97, 0x20 },
	{ 0x97, 0x20 },
	{ 0x97, 0x20 },
	{ 0x97, 0x0a },
	{ 0x97, 0x3f },
	{ 0x97, 0x4a },
	{ 0x97, 0x20 },
	{ 0x97, 0x15 },
	{ 0x97, 0x0b },

	{ 0x8e, 0x40 },
	{ 0x1f, 0x81 },
	{ 0x34, 0x05 },
	{ 0xe3, 0x04 },
	{ 0x88, 0x00 },
	{ 0x89, 0x00 },
	{ 0x76, 0x00 },
	{ 0xe7, 0x2e },
	{ 0x31, 0xf9 },
	{ 0x25, 0x42 },
	{ 0x21, 0xf0 },

	{ 0x1c, 0x00 },
	{ 0x1d, 0x40 },
	{ 0x1d, 0x02 }, /* payload size 0x0200 * 4 = 2048 bytes */
	{ 0x1d, 0x00 }, /* payload size */
	{ 0x1d, 0x02 }, /* frame size 0x025800 * 4 = 614400 */
	{ 0x1d, 0x58 }, /* frame size */
	{ 0x1d, 0x00 }, /* frame size */

	{ 0x1c, 0x0a },
	{ 0x1d, 0x08 }, /* turn on UVC header */
	{ 0x1d, 0x0e }, /* .. */

	{ 0x8d, 0x1c },
	{ 0x8e, 0x80 },
	{ 0xe5, 0x04 },

	{ 0xc0, 0x50 },
	{ 0xc1, 0x3c },
	{ 0xc2, 0x0c },
};

static const u8 sensor_init_ov722x[][2] = {
	{ 0x12, 0x80 },
	{ 0x11, 0x01 },

	{ 0x3d, 0x03 },
	{ 0x17, 0x26 },
	{ 0x18, 0xa0 },
	{ 0x19, 0x07 },
	{ 0x1a, 0xf0 },
	{ 0x32, 0x00 },
	{ 0x29, 0xa0 },
	{ 0x2c, 0xf0 },
	{ 0x65, 0x20 },
	{ 0x11, 0x01 },
	{ 0x42, 0x7f },
	{ 0x63, 0xe0 },
	{ 0x64, 0xff },
	{ 0x66, 0x00 },
	{ 0x13, 0xf0 },
	{ 0x0d, 0x41 },
	{ 0x0f, 0xc5 },
	{ 0x14, 0x11 },

	{ 0x22, 0x7f },
	{ 0x23, 0x03 },
	{ 0x24, 0x40 },
	{ 0x25, 0x30 },
	{ 0x26, 0xa1 },
	{ 0x2a, 0x00 },
	{ 0x2b, 0x00 },
	{ 0x6b, 0xaa },
	{ 0x13, 0xff },

	{ 0x90, 0x05 },
	{ 0x91, 0x01 },
	{ 0x92, 0x03 },
	{ 0x93, 0x00 },
	{ 0x94, 0x60 },
	{ 0x95, 0x3c },
	{ 0x96, 0x24 },
	{ 0x97, 0x1e },
	{ 0x98, 0x62 },
	{ 0x99, 0x80 },
	{ 0x9a, 0x1e },
	{ 0x9b, 0x08 },
	{ 0x9c, 0x20 },
	{ 0x9e, 0x81 },

	{ 0xa6, 0x04 },
	{ 0x7e, 0x0c },
	{ 0x7f, 0x16 },
	{ 0x80, 0x2a },
	{ 0x81, 0x4e },
	{ 0x82, 0x61 },
	{ 0x83, 0x6f },
	{ 0x84, 0x7b },
	{ 0x85, 0x86 },
	{ 0x86, 0x8e },
	{ 0x87, 0x97 },
	{ 0x88, 0xa4 },
	{ 0x89, 0xaf },
	{ 0x8a, 0xc5 },
	{ 0x8b, 0xd7 },
	{ 0x8c, 0xe8 },
	{ 0x8d, 0x20 },

	{ 0x0c, 0x90 },

	{ 0x2b, 0x00 },
	{ 0x22, 0x7f },
	{ 0x23, 0x03 },
	{ 0x11, 0x01 },
	{ 0x0c, 0xd0 },
	{ 0x64, 0xff },
	{ 0x0d, 0x41 },

	{ 0x14, 0x41 },
	{ 0x0e, 0xcd },
	{ 0xac, 0xbf },
	{ 0x8e, 0x00 },
	{ 0x0c, 0xd0 }
};

static const u8 bridge_init_ov965x[][2] = {
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

static const u8 sensor_init_ov965x[][2] = {
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
	{0x4f, 0x98},
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

static const u8 bridge_init_ov965x_2[][2] = {
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
	{0x55, 0x00},	/* brightness */
	{0x57, 0x00},	/* contrast 2 */
	{0x5c, 0x00},
	{0x5a, 0xa0},
	{0x5b, 0x78},
	{0x35, 0x02},
	{0xd9, 0x10},
	{0x94, 0x11},
};

static const u8 sensor_init_ov965x_2[][2] = {
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
	{0x42, 0xc0},
	{0x2d, 0x00},
	{0xff, 0x42},	/* read 42, write ff 00 */
	{0x42, 0xc1},
	{0x3f, 0x01},
	{0xff, 0x42},	/* read 42, write ff 00 */
	{0x42, 0xc1},
	{0x4f, 0x98},
	{0x50, 0x98},
	{0x51, 0x00},
	{0x52, 0x28},
	{0x53, 0x70},
	{0x54, 0x98},
	{0x58, 0x1a},
	{0xff, 0x41},	/* read 41, write ff 00 */
	{0x41, 0x40},	/* com16 */
	{0x56, 0x40},
	{0x55, 0x8f},
	{0x10, 0x25},	/* aech - exposure high bits */
	{0xff, 0x13},	/* read 13, write ff 00 */
	{0x13, 0xe7},	/* com8 - everything (AGC, AWB and AEC) */
};

static const u8 sensor_start_ov965x[][2] = {
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
	{}
};

static const u8 bridge_start_ov965x[][2] = {
	{0x94, 0xaa},
	{0xf1, 0x60},
	{0xe5, 0x04},
	{0xc0, 0x50},
	{0xc1, 0x3c},
	{0x8c, 0x00},
	{0x8d, 0x1c},
	{0x34, 0x05},
	{}
};

static const u8 bridge_start_ov965x_vga[][2] = {
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
	{}
};

static const u8 bridge_start_ov965x_cif[][2] = {
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
	{}
};

static const u8 sensor_start_ov965x_vga[][2] = {
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
	{}
};

static const u8 sensor_start_ov965x_cif[][2] = {
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
	{}
};

static const u8 sensor_start_ov965x_2[][2] = {
	{0xff, 0x42},	/* read 42, write ff 00 */
	{0x42, 0xc1},	/* com17 - 50 Hz filter */
	{}
};


static void ov534_reg_write(struct gspca_dev *gspca_dev, u16 reg, u8 val)
{
	struct usb_device *udev = gspca_dev->dev;
	int ret;

	PDEBUG(D_USBO, "reg=0x%04x, val=0%02x", reg, val);
	gspca_dev->usb_buf[0] = val;
	ret = usb_control_msg(udev,
			      usb_sndctrlpipe(udev, 0),
			      0x01,
			      USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			      0x00, reg, gspca_dev->usb_buf, 1, CTRL_TIMEOUT);
	if (ret < 0)
		PDEBUG(D_ERR, "write failed");
}

static u8 ov534_reg_read(struct gspca_dev *gspca_dev, u16 reg)
{
	struct usb_device *udev = gspca_dev->dev;
	int ret;

	ret = usb_control_msg(udev,
			      usb_rcvctrlpipe(udev, 0),
			      0x01,
			      USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			      0x00, reg, gspca_dev->usb_buf, 1, CTRL_TIMEOUT);
	PDEBUG(D_USBI, "reg=0x%04x, data=0x%02x", reg, gspca_dev->usb_buf[0]);
	if (ret < 0)
		PDEBUG(D_ERR, "read failed");
	return gspca_dev->usb_buf[0];
}

/* Two bits control LED: 0x21 bit 7 and 0x23 bit 7.
 * (direction and output)? */
static void ov534_set_led(struct gspca_dev *gspca_dev, int status)
{
	u8 data;

	PDEBUG(D_CONF, "led status: %d", status);

	data = ov534_reg_read(gspca_dev, 0x21);
	data |= 0x80;
	ov534_reg_write(gspca_dev, 0x21, data);

	data = ov534_reg_read(gspca_dev, 0x23);
	if (status)
		data |= 0x80;
	else
		data &= ~0x80;

	ov534_reg_write(gspca_dev, 0x23, data);

	if (!status) {
		data = ov534_reg_read(gspca_dev, 0x21);
		data &= ~0x80;
		ov534_reg_write(gspca_dev, 0x21, data);
	}
}

static int sccb_check_status(struct gspca_dev *gspca_dev)
{
	u8 data;
	int i;

	for (i = 0; i < 5; i++) {
		data = ov534_reg_read(gspca_dev, OV534_REG_STATUS);

		switch (data) {
		case 0x00:
			return 1;
		case 0x04:
			return 0;
		case 0x03:
			break;
		default:
			PDEBUG(D_ERR, "sccb status 0x%02x, attempt %d/5",
			       data, i + 1);
		}
	}
	return 0;
}

static void sccb_reg_write(struct gspca_dev *gspca_dev, u8 reg, u8 val)
{
	PDEBUG(D_USBO, "reg: 0x%02x, val: 0x%02x", reg, val);
	ov534_reg_write(gspca_dev, OV534_REG_SUBADDR, reg);
	ov534_reg_write(gspca_dev, OV534_REG_WRITE, val);
	ov534_reg_write(gspca_dev, OV534_REG_OPERATION, OV534_OP_WRITE_3);

	if (!sccb_check_status(gspca_dev))
		PDEBUG(D_ERR, "sccb_reg_write failed");
}

static u8 sccb_reg_read(struct gspca_dev *gspca_dev, u16 reg)
{
	ov534_reg_write(gspca_dev, OV534_REG_SUBADDR, reg);
	ov534_reg_write(gspca_dev, OV534_REG_OPERATION, OV534_OP_WRITE_2);
	if (!sccb_check_status(gspca_dev))
		PDEBUG(D_ERR, "sccb_reg_read failed 1");

	ov534_reg_write(gspca_dev, OV534_REG_OPERATION, OV534_OP_READ_2);
	if (!sccb_check_status(gspca_dev))
		PDEBUG(D_ERR, "sccb_reg_read failed 2");

	return ov534_reg_read(gspca_dev, OV534_REG_READ);
}

/* output a bridge sequence (reg - val) */
static void reg_w_array(struct gspca_dev *gspca_dev,
			const u8 (*data)[2], int len)
{
	while (--len >= 0) {
		ov534_reg_write(gspca_dev, (*data)[0], (*data)[1]);
		data++;
	}
}

/* output a sensor sequence (reg - val) */
static void sccb_w_array(struct gspca_dev *gspca_dev,
			const u8 (*data)[2], int len)
{
	while (--len >= 0) {
		if ((*data)[0] != 0xff) {
			sccb_reg_write(gspca_dev, (*data)[0], (*data)[1]);
		} else {
			sccb_reg_read(gspca_dev, (*data)[1]);
			sccb_reg_write(gspca_dev, 0xff, 0x00);
		}
		data++;
	}
}

/* set framerate */
static void ov534_set_frame_rate(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int fr = sd->frame_rate;

	switch (fr) {
	case 50:
		sccb_reg_write(gspca_dev, 0x11, 0x01);
		sccb_reg_write(gspca_dev, 0x0d, 0x41);
		ov534_reg_write(gspca_dev, 0xe5, 0x02);
		break;
	case 40:
		sccb_reg_write(gspca_dev, 0x11, 0x02);
		sccb_reg_write(gspca_dev, 0x0d, 0xc1);
		ov534_reg_write(gspca_dev, 0xe5, 0x04);
		break;
/*	case 30: */
	default:
		fr = 30;
		sccb_reg_write(gspca_dev, 0x11, 0x04);
		sccb_reg_write(gspca_dev, 0x0d, 0x81);
		ov534_reg_write(gspca_dev, 0xe5, 0x02);
		break;
	case 15:
		sccb_reg_write(gspca_dev, 0x11, 0x03);
		sccb_reg_write(gspca_dev, 0x0d, 0x41);
		ov534_reg_write(gspca_dev, 0xe5, 0x04);
		break;
	}

	sd->frame_rate = fr;
	PDEBUG(D_PROBE, "frame_rate: %d", fr);
}

/* this function is called at probe time */
static int sd_config(struct gspca_dev *gspca_dev,
		     const struct usb_device_id *id)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct cam *cam;

	sd->sensor = id->driver_info;

	cam = &gspca_dev->cam;

	if (sd->sensor == SENSOR_OV772X) {
		cam->cam_mode = vga_yuyv_mode;
		cam->nmodes = ARRAY_SIZE(vga_yuyv_mode);

		cam->bulk = 1;
		cam->bulk_size = 16384;
		cam->bulk_nurbs = 2;
	} else {		/* ov965x */
		cam->cam_mode = vga_jpeg_mode;
		cam->nmodes = ARRAY_SIZE(vga_jpeg_mode);
	}

	return 0;
}

/* this function is called at probe and resume time */
static int sd_init(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u16 sensor_id;
	static const u8 sensor_addr[2] = {
		0x42,			/* 0 SENSOR_OV772X */
		0x60,			/* 1 SENSOR_OV965X */
	};

	/* reset bridge */
	ov534_reg_write(gspca_dev, 0xe7, 0x3a);
	ov534_reg_write(gspca_dev, 0xe0, 0x08);
	msleep(100);

	/* initialize the sensor address */
	ov534_reg_write(gspca_dev, OV534_REG_ADDRESS,
				sensor_addr[sd->sensor]);

	/* reset sensor */
	sccb_reg_write(gspca_dev, 0x12, 0x80);
	msleep(10);

	/* probe the sensor */
	sccb_reg_read(gspca_dev, 0x0a);
	sensor_id = sccb_reg_read(gspca_dev, 0x0a) << 8;
	sccb_reg_read(gspca_dev, 0x0b);
	sensor_id |= sccb_reg_read(gspca_dev, 0x0b);
	PDEBUG(D_PROBE, "Sensor ID: %04x", sensor_id);

	/* initialize */
	switch (sd->sensor) {
	case SENSOR_OV772X:
		reg_w_array(gspca_dev, bridge_init_ov722x,
				ARRAY_SIZE(bridge_init_ov722x));
		ov534_set_led(gspca_dev, 1);
		sccb_w_array(gspca_dev, sensor_init_ov722x,
				ARRAY_SIZE(sensor_init_ov722x));
		ov534_reg_write(gspca_dev, 0xe0, 0x09);
		ov534_set_led(gspca_dev, 0);
		ov534_set_frame_rate(gspca_dev);
		break;
	default:
/*	case SENSOR_OV965X: */
		reg_w_array(gspca_dev, bridge_init_ov965x,
				ARRAY_SIZE(bridge_init_ov965x));
		sccb_w_array(gspca_dev, sensor_init_ov965x,
				ARRAY_SIZE(sensor_init_ov965x));
		reg_w_array(gspca_dev, bridge_init_ov965x_2,
				ARRAY_SIZE(bridge_init_ov965x_2));
		sccb_w_array(gspca_dev, sensor_init_ov965x_2,
				ARRAY_SIZE(sensor_init_ov965x_2));
		ov534_reg_write(gspca_dev, 0xe0, 0x00);
		ov534_reg_write(gspca_dev, 0xe0, 0x01);
		ov534_set_led(gspca_dev, 0);
		ov534_reg_write(gspca_dev, 0xe0, 0x00);
	}

	return 0;
}

static int sd_start(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int mode;

	switch (sd->sensor) {
	case SENSOR_OV772X:
		ov534_set_led(gspca_dev, 1);
		ov534_reg_write(gspca_dev, 0xe0, 0x00);
		break;
	default:
/*	case SENSOR_OV965X: */

		sccb_w_array(gspca_dev, sensor_start_ov965x,
				ARRAY_SIZE(sensor_start_ov965x));
		reg_w_array(gspca_dev, bridge_start_ov965x,
				ARRAY_SIZE(bridge_start_ov965x));
		mode = gspca_dev->cam.cam_mode[gspca_dev->curr_mode].priv;
		if (mode != 0) {	/* 320x240 */
			reg_w_array(gspca_dev, bridge_start_ov965x_cif,
					ARRAY_SIZE(bridge_start_ov965x_cif));
			sccb_w_array(gspca_dev, sensor_start_ov965x_cif,
					ARRAY_SIZE(sensor_start_ov965x_cif));
		} else {		/* 640x480 */
			reg_w_array(gspca_dev, bridge_start_ov965x_vga,
					ARRAY_SIZE(bridge_start_ov965x_vga));
			sccb_w_array(gspca_dev, sensor_start_ov965x_vga,
					ARRAY_SIZE(sensor_start_ov965x_vga));
		}
		sccb_w_array(gspca_dev, sensor_start_ov965x_2,
				ARRAY_SIZE(sensor_start_ov965x_2));
		ov534_reg_write(gspca_dev, 0xe0, 0x00);
		ov534_reg_write(gspca_dev, 0xe0, 0x00);
		ov534_set_led(gspca_dev, 1);
	}
	return 0;
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	switch (sd->sensor) {
	case SENSOR_OV772X:
		ov534_reg_write(gspca_dev, 0xe0, 0x09);
		ov534_set_led(gspca_dev, 0);
		break;
	default:
/*	case SENSOR_OV965X: */
		ov534_reg_write(gspca_dev, 0xe0, 0x01);
		ov534_set_led(gspca_dev, 0);
		ov534_reg_write(gspca_dev, 0xe0, 0x00);
		break;
	}
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

static void sd_pkt_scan(struct gspca_dev *gspca_dev, struct gspca_frame *frame,
			__u8 *data, int len)
{
	struct sd *sd = (struct sd *) gspca_dev;
	__u32 this_pts;
	u16 this_fid;
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
		this_fid = (data[1] & UVC_STREAM_FID) ? 1 : 0;

		/* If PTS or FID has changed, start a new frame. */
		if (this_pts != sd->last_pts || this_fid != sd->last_fid) {
			if (gspca_dev->last_packet_type == INTER_PACKET)
				frame = gspca_frame_add(gspca_dev,
							LAST_PACKET, frame,
							NULL, 0);
			sd->last_pts = this_pts;
			sd->last_fid = this_fid;
			gspca_frame_add(gspca_dev, FIRST_PACKET, frame,
					data + 12, len - 12);
		/* If this packet is marked as EOF, end the frame */
		} else if (data[1] & UVC_STREAM_EOF) {
			sd->last_pts = 0;
			frame = gspca_frame_add(gspca_dev, LAST_PACKET, frame,
						data + 12, len - 12);
		} else {

			/* Add the data from this payload */
			gspca_frame_add(gspca_dev, INTER_PACKET, frame,
						data + 12, len - 12);
		}


		/* Done this payload */
		goto scan_next;

discard:
		/* Discard data until a new frame starts. */
		gspca_frame_add(gspca_dev, DISCARD_PACKET, frame, NULL, 0);

scan_next:
		remaining_len -= len;
		data += len;
	} while (remaining_len > 0);
}

/* get stream parameters (framerate) */
static int sd_get_streamparm(struct gspca_dev *gspca_dev,
			     struct v4l2_streamparm *parm)
{
	struct v4l2_captureparm *cp = &parm->parm.capture;
	struct v4l2_fract *tpf = &cp->timeperframe;
	struct sd *sd = (struct sd *) gspca_dev;

	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	cp->capability |= V4L2_CAP_TIMEPERFRAME;
	tpf->numerator = 1;
	tpf->denominator = sd->frame_rate;

	return 0;
}

/* set stream parameters (framerate) */
static int sd_set_streamparm(struct gspca_dev *gspca_dev,
			     struct v4l2_streamparm *parm)
{
	struct v4l2_captureparm *cp = &parm->parm.capture;
	struct v4l2_fract *tpf = &cp->timeperframe;
	struct sd *sd = (struct sd *) gspca_dev;

	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	/* Set requested framerate */
	sd->frame_rate = tpf->denominator / tpf->numerator;
	ov534_set_frame_rate(gspca_dev);

	/* Return the actual framerate */
	tpf->numerator = 1;
	tpf->denominator = sd->frame_rate;

	return 0;
}

/* sub-driver description */
static const struct sd_desc sd_desc = {
	.name     = MODULE_NAME,
	.ctrls    = sd_ctrls,
	.nctrls   = ARRAY_SIZE(sd_ctrls),
	.config   = sd_config,
	.init     = sd_init,
	.start    = sd_start,
	.stopN    = sd_stopN,
	.pkt_scan = sd_pkt_scan,
	.get_streamparm = sd_get_streamparm,
	.set_streamparm = sd_set_streamparm,
};

/* -- module initialisation -- */
static const __devinitdata struct usb_device_id device_table[] = {
	{USB_DEVICE(0x06f8, 0x3003), .driver_info = SENSOR_OV965X},
	{USB_DEVICE(0x1415, 0x2000), .driver_info = SENSOR_OV772X},
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

/* -- module insert / remove -- */
static int __init sd_mod_init(void)
{
	int ret;
	ret = usb_register(&sd_driver);
	if (ret < 0)
		return ret;
	PDEBUG(D_PROBE, "registered");
	return 0;
}

static void __exit sd_mod_exit(void)
{
	usb_deregister(&sd_driver);
	PDEBUG(D_PROBE, "deregistered");
}

module_init(sd_mod_init);
module_exit(sd_mod_exit);
