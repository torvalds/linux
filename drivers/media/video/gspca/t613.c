/*
 * T613 subdriver
 *
 * Copyright (C) 2010 Jean-Francois Moine (http://moinejf.free.fr)
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
 *
 *Notes: * t613  + tas5130A
 *	* Focus to light do not balance well as in win.
 *	  Quality in win is not good, but its kinda better.
 *	 * Fix some "extraneous bytes", most of apps will show the image anyway
 *	 * Gamma table, is there, but its really doing something?
 *	 * 7~8 Fps, its ok, max on win its 10.
 *			Costantino Leandro
 */

#define MODULE_NAME "t613"

#include <linux/slab.h>
#include "gspca.h"

#define V4L2_CID_EFFECTS (V4L2_CID_PRIVATE_BASE + 0)

MODULE_AUTHOR("Leandro Costantino <le_costantino@pixartargentina.com.ar>");
MODULE_DESCRIPTION("GSPCA/T613 (JPEG Compliance) USB Camera Driver");
MODULE_LICENSE("GPL");

struct sd {
	struct gspca_dev gspca_dev;	/* !! must be the first item */

	u8 brightness;
	u8 contrast;
	u8 colors;
	u8 autogain;
	u8 gamma;
	u8 sharpness;
	u8 freq;
	u8 red_gain;
	u8 blue_gain;
	u8 green_gain;
	u8 awb; /* set default r/g/b and activate */
	u8 mirror;
	u8 effect;

	u8 sensor;
};
enum sensors {
	SENSOR_OM6802,
	SENSOR_OTHER,
	SENSOR_TAS5130A,
	SENSOR_LT168G,		/* must verify if this is the actual model */
};

/* V4L2 controls supported by the driver */
static int sd_setbrightness(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getbrightness(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setcontrast(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getcontrast(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setcolors(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getcolors(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setlowlight(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getlowlight(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setgamma(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getgamma(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setsharpness(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getsharpness(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setfreq(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getfreq(struct gspca_dev *gspca_dev, __s32 *val);

static int sd_setawb(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getawb(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setblue_gain(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getblue_gain(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setred_gain(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getred_gain(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setgain(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getgain(struct gspca_dev *gspca_dev, __s32 *val);

static int sd_setmirror(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getmirror(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_seteffect(struct gspca_dev *gspca_dev, __s32 val);
static int sd_geteffect(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_querymenu(struct gspca_dev *gspca_dev,
			struct v4l2_querymenu *menu);

static const struct ctrl sd_ctrls[] = {
	{
	 {
	  .id = V4L2_CID_BRIGHTNESS,
	  .type = V4L2_CTRL_TYPE_INTEGER,
	  .name = "Brightness",
	  .minimum = 0,
	  .maximum = 14,
	  .step = 1,
#define BRIGHTNESS_DEF 8
	  .default_value = BRIGHTNESS_DEF,
	  },
	 .set = sd_setbrightness,
	 .get = sd_getbrightness,
	 },
	{
	 {
	  .id = V4L2_CID_CONTRAST,
	  .type = V4L2_CTRL_TYPE_INTEGER,
	  .name = "Contrast",
	  .minimum = 0,
	  .maximum = 0x0d,
	  .step = 1,
#define CONTRAST_DEF 0x07
	  .default_value = CONTRAST_DEF,
	  },
	 .set = sd_setcontrast,
	 .get = sd_getcontrast,
	 },
	{
	 {
	  .id = V4L2_CID_SATURATION,
	  .type = V4L2_CTRL_TYPE_INTEGER,
	  .name = "Color",
	  .minimum = 0,
	  .maximum = 0x0f,
	  .step = 1,
#define COLORS_DEF 0x05
	  .default_value = COLORS_DEF,
	  },
	 .set = sd_setcolors,
	 .get = sd_getcolors,
	 },
#define GAMMA_MAX 16
#define GAMMA_DEF 10
	{
	 {
	  .id = V4L2_CID_GAMMA,	/* (gamma on win) */
	  .type = V4L2_CTRL_TYPE_INTEGER,
	  .name = "Gamma",
	  .minimum = 0,
	  .maximum = GAMMA_MAX - 1,
	  .step = 1,
	  .default_value = GAMMA_DEF,
	  },
	 .set = sd_setgamma,
	 .get = sd_getgamma,
	 },
	{
	 {
	  .id = V4L2_CID_BACKLIGHT_COMPENSATION, /* Activa lowlight,
				 * some apps dont bring up the
				 * backligth_compensation control) */
	  .type = V4L2_CTRL_TYPE_INTEGER,
	  .name = "Low Light",
	  .minimum = 0,
	  .maximum = 1,
	  .step = 1,
#define AUTOGAIN_DEF 0x01
	  .default_value = AUTOGAIN_DEF,
	  },
	 .set = sd_setlowlight,
	 .get = sd_getlowlight,
	 },
	{
	 {
	  .id = V4L2_CID_HFLIP,
	  .type = V4L2_CTRL_TYPE_BOOLEAN,
	  .name = "Mirror Image",
	  .minimum = 0,
	  .maximum = 1,
	  .step = 1,
#define MIRROR_DEF 0
	  .default_value = MIRROR_DEF,
	  },
	 .set = sd_setmirror,
	 .get = sd_getmirror
	},
	{
	 {
	  .id = V4L2_CID_POWER_LINE_FREQUENCY,
	  .type = V4L2_CTRL_TYPE_MENU,
	  .name = "Light Frequency Filter",
	  .minimum = 1,		/* 1 -> 0x50, 2->0x60 */
	  .maximum = 2,
	  .step = 1,
#define FREQ_DEF 1
	  .default_value = FREQ_DEF,
	  },
	 .set = sd_setfreq,
	 .get = sd_getfreq},

	{
	 {
	  .id =  V4L2_CID_AUTO_WHITE_BALANCE,
	  .type = V4L2_CTRL_TYPE_INTEGER,
	  .name = "Auto White Balance",
	  .minimum = 0,
	  .maximum = 1,
	  .step = 1,
#define AWB_DEF 0
	  .default_value = AWB_DEF,
	  },
	 .set = sd_setawb,
	 .get = sd_getawb
	},
	{
	 {
	  .id = V4L2_CID_SHARPNESS,
	  .type = V4L2_CTRL_TYPE_INTEGER,
	  .name = "Sharpness",
	  .minimum = 0,
	  .maximum = 15,
	  .step = 1,
#define SHARPNESS_DEF 0x06
	  .default_value = SHARPNESS_DEF,
	  },
	 .set = sd_setsharpness,
	 .get = sd_getsharpness,
	 },
	{
	 {
	  .id = V4L2_CID_EFFECTS,
	  .type = V4L2_CTRL_TYPE_MENU,
	  .name = "Webcam Effects",
	  .minimum = 0,
	  .maximum = 4,
	  .step = 1,
#define EFFECTS_DEF 0
	  .default_value = EFFECTS_DEF,
	  },
	 .set = sd_seteffect,
	 .get = sd_geteffect
	},
	{
	 {
	    .id      = V4L2_CID_BLUE_BALANCE,
	    .type    = V4L2_CTRL_TYPE_INTEGER,
	    .name    = "Blue Balance",
	    .minimum = 0x10,
	    .maximum = 0x40,
	    .step    = 1,
#define BLUE_GAIN_DEF 0x20
	    .default_value = BLUE_GAIN_DEF,
	 },
	.set = sd_setblue_gain,
	.get = sd_getblue_gain,
	},
	{
	 {
	    .id      = V4L2_CID_RED_BALANCE,
	    .type    = V4L2_CTRL_TYPE_INTEGER,
	    .name    = "Red Balance",
	    .minimum = 0x10,
	    .maximum = 0x40,
	    .step    = 1,
#define RED_GAIN_DEF 0x20
	    .default_value = RED_GAIN_DEF,
	 },
	.set = sd_setred_gain,
	.get = sd_getred_gain,
	},
	{
	 {
	    .id      = V4L2_CID_GAIN,
	    .type    = V4L2_CTRL_TYPE_INTEGER,
	    .name    = "Gain",
	    .minimum = 0x10,
	    .maximum = 0x40,
	    .step    = 1,
#define GAIN_DEF  0x20
	    .default_value = GAIN_DEF,
	 },
	.set = sd_setgain,
	.get = sd_getgain,
	},
};

static const struct v4l2_pix_format vga_mode_t16[] = {
	{160, 120, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 160,
		.sizeimage = 160 * 120 * 4 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 4},
	{176, 144, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 176,
		.sizeimage = 176 * 144 * 3 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 3},
	{320, 240, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240 * 3 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 2},
	{352, 288, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 352,
		.sizeimage = 352 * 288 * 3 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 1},
	{640, 480, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 640,
		.sizeimage = 640 * 480 * 3 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 0},
};

/* sensor specific data */
struct additional_sensor_data {
	const u8 n3[6];
	const u8 *n4, n4sz;
	const u8 reg80, reg8e;
	const u8 nset8[6];
	const u8 data1[10];
	const u8 data2[9];
	const u8 data3[9];
	const u8 data5[6];
	const u8 stream[4];
};

static const u8 n4_om6802[] = {
	0x09, 0x01, 0x12, 0x04, 0x66, 0x8a, 0x80, 0x3c,
	0x81, 0x22, 0x84, 0x50, 0x8a, 0x78, 0x8b, 0x68,
	0x8c, 0x88, 0x8e, 0x33, 0x8f, 0x24, 0xaa, 0xb1,
	0xa2, 0x60, 0xa5, 0x30, 0xa6, 0x3a, 0xa8, 0xe8,
	0xae, 0x05, 0xb1, 0x00, 0xbb, 0x04, 0xbc, 0x48,
	0xbe, 0x36, 0xc6, 0x88, 0xe9, 0x00, 0xc5, 0xc0,
	0x65, 0x0a, 0xbb, 0x86, 0xaf, 0x58, 0xb0, 0x68,
	0x87, 0x40, 0x89, 0x2b, 0x8d, 0xff, 0x83, 0x40,
	0xac, 0x84, 0xad, 0x86, 0xaf, 0x46
};
static const u8 n4_other[] = {
	0x66, 0x00, 0x7f, 0x00, 0x80, 0xac, 0x81, 0x69,
	0x84, 0x40, 0x85, 0x70, 0x86, 0x20, 0x8a, 0x68,
	0x8b, 0x58, 0x8c, 0x88, 0x8d, 0xff, 0x8e, 0xb8,
	0x8f, 0x28, 0xa2, 0x60, 0xa5, 0x40, 0xa8, 0xa8,
	0xac, 0x84, 0xad, 0x84, 0xae, 0x24, 0xaf, 0x56,
	0xb0, 0x68, 0xb1, 0x00, 0xb2, 0x88, 0xbb, 0xc5,
	0xbc, 0x4a, 0xbe, 0x36, 0xc2, 0x88, 0xc5, 0xc0,
	0xc6, 0xda, 0xe9, 0x26, 0xeb, 0x00
};
static const u8 n4_tas5130a[] = {
	0x80, 0x3c, 0x81, 0x68, 0x83, 0xa0, 0x84, 0x20,
	0x8a, 0x68, 0x8b, 0x58, 0x8c, 0x88, 0x8e, 0xb4,
	0x8f, 0x24, 0xa1, 0xb1, 0xa2, 0x30, 0xa5, 0x10,
	0xa6, 0x4a, 0xae, 0x03, 0xb1, 0x44, 0xb2, 0x08,
	0xb7, 0x06, 0xb9, 0xe7, 0xbb, 0xc4, 0xbc, 0x4a,
	0xbe, 0x36, 0xbf, 0xff, 0xc2, 0x88, 0xc5, 0xc8,
	0xc6, 0xda
};
static const u8 n4_lt168g[] = {
	0x66, 0x01, 0x7f, 0x00, 0x80, 0x7c, 0x81, 0x28,
	0x83, 0x44, 0x84, 0x20, 0x86, 0x20, 0x8a, 0x70,
	0x8b, 0x58, 0x8c, 0x88, 0x8d, 0xa0, 0x8e, 0xb3,
	0x8f, 0x24, 0xa1, 0xb0, 0xa2, 0x38, 0xa5, 0x20,
	0xa6, 0x4a, 0xa8, 0xe8, 0xaf, 0x38, 0xb0, 0x68,
	0xb1, 0x44, 0xb2, 0x88, 0xbb, 0x86, 0xbd, 0x40,
	0xbe, 0x26, 0xc1, 0x05, 0xc2, 0x88, 0xc5, 0xc0,
	0xda, 0x8e, 0xdb, 0xca, 0xdc, 0xa8, 0xdd, 0x8c,
	0xde, 0x44, 0xdf, 0x0c, 0xe9, 0x80
};

static const struct additional_sensor_data sensor_data[] = {
[SENSOR_OM6802] = {
	.n3 =
		{0x61, 0x68, 0x65, 0x0a, 0x60, 0x04},
	.n4 = n4_om6802,
	.n4sz = sizeof n4_om6802,
	.reg80 = 0x3c,
	.reg8e = 0x33,
	.nset8 = {0xa8, 0xf0, 0xc6, 0x88, 0xc0, 0x00},
	.data1 =
		{0xc2, 0x28, 0x0f, 0x22, 0xcd, 0x27, 0x2c, 0x06,
		 0xb3, 0xfc},
	.data2 =
		{0x80, 0xff, 0xff, 0x80, 0xff, 0xff, 0x80, 0xff,
		 0xff},
	.data3 =
		{0x80, 0xff, 0xff, 0x80, 0xff, 0xff, 0x80, 0xff,
		 0xff},
	.data5 =	/* this could be removed later */
		{0x0c, 0x03, 0xab, 0x13, 0x81, 0x23},
	.stream =
		{0x0b, 0x04, 0x0a, 0x78},
    },
[SENSOR_OTHER] = {
	.n3 =
		{0x61, 0xc2, 0x65, 0x88, 0x60, 0x00},
	.n4 = n4_other,
	.n4sz = sizeof n4_other,
	.reg80 = 0xac,
	.reg8e = 0xb8,
	.nset8 = {0xa8, 0xa8, 0xc6, 0xda, 0xc0, 0x00},
	.data1 =
		{0xc1, 0x48, 0x04, 0x1b, 0xca, 0x2e, 0x33, 0x3a,
		 0xe8, 0xfc},
	.data2 =
		{0x4e, 0x9c, 0xec, 0x40, 0x80, 0xc0, 0x48, 0x96,
		 0xd9},
	.data3 =
		{0x4e, 0x9c, 0xec, 0x40, 0x80, 0xc0, 0x48, 0x96,
		 0xd9},
	.data5 =
		{0x0c, 0x03, 0xab, 0x29, 0x81, 0x69},
	.stream =
		{0x0b, 0x04, 0x0a, 0x00},
    },
[SENSOR_TAS5130A] = {
	.n3 =
		{0x61, 0xc2, 0x65, 0x0d, 0x60, 0x08},
	.n4 = n4_tas5130a,
	.n4sz = sizeof n4_tas5130a,
	.reg80 = 0x3c,
	.reg8e = 0xb4,
	.nset8 = {0xa8, 0xf0, 0xc6, 0xda, 0xc0, 0x00},
	.data1 =
		{0xbb, 0x28, 0x10, 0x10, 0xbb, 0x28, 0x1e, 0x27,
		 0xc8, 0xfc},
	.data2 =
		{0x60, 0xa8, 0xe0, 0x60, 0xa8, 0xe0, 0x60, 0xa8,
		 0xe0},
	.data3 =
		{0x60, 0xa8, 0xe0, 0x60, 0xa8, 0xe0, 0x60, 0xa8,
		 0xe0},
	.data5 =
		{0x0c, 0x03, 0xab, 0x10, 0x81, 0x20},
	.stream =
		{0x0b, 0x04, 0x0a, 0x40},
    },
[SENSOR_LT168G] = {
	.n3 = {0x61, 0xc2, 0x65, 0x68, 0x60, 0x00},
	.n4 = n4_lt168g,
	.n4sz = sizeof n4_lt168g,
	.reg80 = 0x7c,
	.reg8e = 0xb3,
	.nset8 = {0xa8, 0xf0, 0xc6, 0xba, 0xc0, 0x00},
	.data1 = {0xc0, 0x38, 0x08, 0x10, 0xc0, 0x30, 0x10, 0x40,
		 0xb0, 0xf4},
	.data2 = {0x40, 0x80, 0xc0, 0x50, 0xa0, 0xf0, 0x53, 0xa6,
		 0xff},
	.data3 = {0x40, 0x80, 0xc0, 0x50, 0xa0, 0xf0, 0x53, 0xa6,
		 0xff},
	.data5 = {0x0c, 0x03, 0xab, 0x4b, 0x81, 0x2b},
	.stream = {0x0b, 0x04, 0x0a, 0x28},
    },
};

#define MAX_EFFECTS 7
/* easily done by soft, this table could be removed,
 * i keep it here just in case */
static char *effects_control[MAX_EFFECTS] = {
	"Normal",
	"Emboss",		/* disabled */
	"Monochrome",
	"Sepia",
	"Sketch",
	"Sun Effect",		/* disabled */
	"Negative",
};
static const u8 effects_table[MAX_EFFECTS][6] = {
	{0xa8, 0xe8, 0xc6, 0xd2, 0xc0, 0x00},	/* Normal */
	{0xa8, 0xc8, 0xc6, 0x52, 0xc0, 0x04},	/* Repujar */
	{0xa8, 0xe8, 0xc6, 0xd2, 0xc0, 0x20},	/* Monochrome */
	{0xa8, 0xe8, 0xc6, 0xd2, 0xc0, 0x80},	/* Sepia */
	{0xa8, 0xc8, 0xc6, 0x52, 0xc0, 0x02},	/* Croquis */
	{0xa8, 0xc8, 0xc6, 0xd2, 0xc0, 0x10},	/* Sun Effect */
	{0xa8, 0xc8, 0xc6, 0xd2, 0xc0, 0x40},	/* Negative */
};

static const u8 gamma_table[GAMMA_MAX][17] = {
/* gamma table from cam1690.ini */
	{0x00, 0x00, 0x01, 0x04, 0x08, 0x0e, 0x16, 0x21,	/* 0 */
	 0x2e, 0x3d, 0x50, 0x65, 0x7d, 0x99, 0xb8, 0xdb,
	 0xff},
	{0x00, 0x01, 0x03, 0x08, 0x0e, 0x16, 0x21, 0x2d,	/* 1 */
	 0x3c, 0x4d, 0x60, 0x75, 0x8d, 0xa6, 0xc2, 0xe1,
	 0xff},
	{0x00, 0x01, 0x05, 0x0b, 0x12, 0x1c, 0x28, 0x35,	/* 2 */
	 0x45, 0x56, 0x69, 0x7e, 0x95, 0xad, 0xc7, 0xe3,
	 0xff},
	{0x00, 0x02, 0x07, 0x0f, 0x18, 0x24, 0x30, 0x3f,	/* 3 */
	 0x4f, 0x61, 0x73, 0x88, 0x9d, 0xb4, 0xcd, 0xe6,
	 0xff},
	{0x00, 0x04, 0x0B, 0x15, 0x20, 0x2d, 0x3b, 0x4a,	/* 4 */
	 0x5b, 0x6c, 0x7f, 0x92, 0xa7, 0xbc, 0xd2, 0xe9,
	 0xff},
	{0x00, 0x07, 0x11, 0x15, 0x20, 0x2d, 0x48, 0x58,	/* 5 */
	 0x68, 0x79, 0x8b, 0x9d, 0xb0, 0xc4, 0xd7, 0xec,
	 0xff},
	{0x00, 0x0c, 0x1a, 0x29, 0x38, 0x47, 0x57, 0x67,	/* 6 */
	 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee,
	 0xff},
	{0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70,	/* 7 */
	 0x80, 0x90, 0xa0, 0xb0, 0xc0, 0xd0, 0xe0, 0xf0,
	 0xff},
	{0x00, 0x15, 0x27, 0x38, 0x49, 0x59, 0x69, 0x79,	/* 8 */
	 0x88, 0x97, 0xa7, 0xb6, 0xc4, 0xd3, 0xe2, 0xf0,
	 0xff},
	{0x00, 0x1c, 0x30, 0x43, 0x54, 0x65, 0x75, 0x84,	/* 9 */
	 0x93, 0xa1, 0xb0, 0xbd, 0xca, 0xd8, 0xe5, 0xf2,
	 0xff},
	{0x00, 0x24, 0x3b, 0x4f, 0x60, 0x70, 0x80, 0x8e,	/* 10 */
	 0x9c, 0xaa, 0xb7, 0xc4, 0xd0, 0xdc, 0xe8, 0xf3,
	 0xff},
	{0x00, 0x2a, 0x3c, 0x5d, 0x6e, 0x7e, 0x8d, 0x9b,	/* 11 */
	 0xa8, 0xb4, 0xc0, 0xcb, 0xd6, 0xe1, 0xeb, 0xf5,
	 0xff},
	{0x00, 0x3f, 0x5a, 0x6e, 0x7f, 0x8e, 0x9c, 0xa8,	/* 12 */
	 0xb4, 0xbf, 0xc9, 0xd3, 0xdc, 0xe5, 0xee, 0xf6,
	 0xff},
	{0x00, 0x54, 0x6f, 0x83, 0x93, 0xa0, 0xad, 0xb7,	/* 13 */
	 0xc2, 0xcb, 0xd4, 0xdc, 0xe4, 0xeb, 0xf2, 0xf9,
	 0xff},
	{0x00, 0x6e, 0x88, 0x9a, 0xa8, 0xb3, 0xbd, 0xc6,	/* 14 */
	 0xcf, 0xd6, 0xdd, 0xe3, 0xe9, 0xef, 0xf4, 0xfa,
	 0xff},
	{0x00, 0x93, 0xa8, 0xb7, 0xc1, 0xca, 0xd2, 0xd8,	/* 15 */
	 0xde, 0xe3, 0xe8, 0xed, 0xf1, 0xf5, 0xf8, 0xfc,
	 0xff}
};

static const u8 tas5130a_sensor_init[][8] = {
	{0x62, 0x08, 0x63, 0x70, 0x64, 0x1d, 0x60, 0x09},
	{0x62, 0x20, 0x63, 0x01, 0x64, 0x02, 0x60, 0x09},
	{0x62, 0x07, 0x63, 0x03, 0x64, 0x00, 0x60, 0x09},
};

static u8 sensor_reset[] = {0x61, 0x68, 0x62, 0xff, 0x60, 0x07};

/* read 1 byte */
static u8 reg_r(struct gspca_dev *gspca_dev,
		   u16 index)
{
	usb_control_msg(gspca_dev->dev,
			usb_rcvctrlpipe(gspca_dev->dev, 0),
			0,		/* request */
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0,		/* value */
			index,
			gspca_dev->usb_buf, 1, 500);
	return gspca_dev->usb_buf[0];
}

static void reg_w(struct gspca_dev *gspca_dev,
		  u16 index)
{
	usb_control_msg(gspca_dev->dev,
			usb_sndctrlpipe(gspca_dev->dev, 0),
			0,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0, index,
			NULL, 0, 500);
}

static void reg_w_buf(struct gspca_dev *gspca_dev,
		  const u8 *buffer, u16 len)
{
	if (len <= USB_BUF_SZ) {
		memcpy(gspca_dev->usb_buf, buffer, len);
		usb_control_msg(gspca_dev->dev,
				usb_sndctrlpipe(gspca_dev->dev, 0),
				0,
			   USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
				0x01, 0,
				gspca_dev->usb_buf, len, 500);
	} else {
		u8 *tmpbuf;

		tmpbuf = kmemdup(buffer, len, GFP_KERNEL);
		if (!tmpbuf) {
			err("Out of memory");
			return;
		}
		usb_control_msg(gspca_dev->dev,
				usb_sndctrlpipe(gspca_dev->dev, 0),
				0,
			   USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
				0x01, 0,
				tmpbuf, len, 500);
		kfree(tmpbuf);
	}
}

/* write values to consecutive registers */
static void reg_w_ixbuf(struct gspca_dev *gspca_dev,
			u8 reg,
			const u8 *buffer, u16 len)
{
	int i;
	u8 *p, *tmpbuf;

	if (len * 2 <= USB_BUF_SZ) {
		p = tmpbuf = gspca_dev->usb_buf;
	} else {
		p = tmpbuf = kmalloc(len * 2, GFP_KERNEL);
		if (!tmpbuf) {
			err("Out of memory");
			return;
		}
	}
	i = len;
	while (--i >= 0) {
		*p++ = reg++;
		*p++ = *buffer++;
	}
	usb_control_msg(gspca_dev->dev,
			usb_sndctrlpipe(gspca_dev->dev, 0),
			0,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0x01, 0,
			tmpbuf, len * 2, 500);
	if (len * 2 > USB_BUF_SZ)
		kfree(tmpbuf);
}

static void om6802_sensor_init(struct gspca_dev *gspca_dev)
{
	int i;
	const u8 *p;
	u8 byte;
	u8 val[6] = {0x62, 0, 0x64, 0, 0x60, 0x05};
	static const u8 sensor_init[] = {
		0xdf, 0x6d,
		0xdd, 0x18,
		0x5a, 0xe0,
		0x5c, 0x07,
		0x5d, 0xb0,
		0x5e, 0x1e,
		0x60, 0x71,
		0xef, 0x00,
		0xe9, 0x00,
		0xea, 0x00,
		0x90, 0x24,
		0x91, 0xb2,
		0x82, 0x32,
		0xfd, 0x41,
		0x00			/* table end */
	};

	reg_w_buf(gspca_dev, sensor_reset, sizeof sensor_reset);
	msleep(100);
	i = 4;
	while (--i > 0) {
		byte = reg_r(gspca_dev, 0x0060);
		if (!(byte & 0x01))
			break;
		msleep(100);
	}
	byte = reg_r(gspca_dev, 0x0063);
	if (byte != 0x17) {
		err("Bad sensor reset %02x", byte);
		/* continue? */
	}

	p = sensor_init;
	while (*p != 0) {
		val[1] = *p++;
		val[3] = *p++;
		if (*p == 0)
			reg_w(gspca_dev, 0x3c80);
		reg_w_buf(gspca_dev, val, sizeof val);
		i = 4;
		while (--i >= 0) {
			msleep(15);
			byte = reg_r(gspca_dev, 0x60);
			if (!(byte & 0x01))
				break;
		}
	}
	msleep(15);
	reg_w(gspca_dev, 0x3c80);
}

/* this function is called at probe time */
static int sd_config(struct gspca_dev *gspca_dev,
		     const struct usb_device_id *id)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct cam *cam;

	cam = &gspca_dev->cam;

	cam->cam_mode = vga_mode_t16;
	cam->nmodes = ARRAY_SIZE(vga_mode_t16);

	sd->brightness = BRIGHTNESS_DEF;
	sd->contrast = CONTRAST_DEF;
	sd->colors = COLORS_DEF;
	sd->gamma = GAMMA_DEF;
	sd->autogain = AUTOGAIN_DEF;
	sd->mirror = MIRROR_DEF;
	sd->freq = FREQ_DEF;
	sd->awb = AWB_DEF;
	sd->sharpness = SHARPNESS_DEF;
	sd->effect = EFFECTS_DEF;
	sd->red_gain = RED_GAIN_DEF;
	sd->blue_gain = BLUE_GAIN_DEF;
	sd->green_gain = GAIN_DEF * 3 - RED_GAIN_DEF - BLUE_GAIN_DEF;

	return 0;
}

static void setbrightness(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	unsigned int brightness;
	u8 set6[4] = { 0x8f, 0x24, 0xc3, 0x00 };

	brightness = sd->brightness;
	if (brightness < 7) {
		set6[1] = 0x26;
		set6[3] = 0x70 - brightness * 0x10;
	} else {
		set6[3] = 0x00 + ((brightness - 7) * 0x10);
	}

	reg_w_buf(gspca_dev, set6, sizeof set6);
}

static void setcontrast(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	unsigned int contrast = sd->contrast;
	u16 reg_to_write;

	if (contrast < 7)
		reg_to_write = 0x8ea9 - contrast * 0x200;
	else
		reg_to_write = 0x00a9 + (contrast - 7) * 0x200;

	reg_w(gspca_dev, reg_to_write);
}

static void setcolors(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u16 reg_to_write;

	reg_to_write = 0x80bb + sd->colors * 0x100;	/* was 0xc0 */
	reg_w(gspca_dev, reg_to_write);
}

static void setgamma(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	PDEBUG(D_CONF, "Gamma: %d", sd->gamma);
	reg_w_ixbuf(gspca_dev, 0x90,
		gamma_table[sd->gamma], sizeof gamma_table[0]);
}

static void setRGB(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 all_gain_reg[6] =
		{0x87, 0x00, 0x88, 0x00, 0x89, 0x00};

	all_gain_reg[1] = sd->red_gain;
	all_gain_reg[3] = sd->blue_gain;
	all_gain_reg[5] = sd->green_gain;
	reg_w_buf(gspca_dev, all_gain_reg, sizeof all_gain_reg);
}

/* Generic fnc for r/b balance, exposure and awb */
static void setawb(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u16 reg80;

	reg80 = (sensor_data[sd->sensor].reg80 << 8) | 0x80;

	/* on awb leave defaults values */
	if (!sd->awb) {
		/* shoud we wait here.. */
		/* update and reset RGB gains with webcam values */
		sd->red_gain = reg_r(gspca_dev, 0x0087);
		sd->blue_gain = reg_r(gspca_dev, 0x0088);
		sd->green_gain = reg_r(gspca_dev, 0x0089);
		reg80 &= ~0x0400;		/* AWB off */
	}
	reg_w(gspca_dev, reg80);
	reg_w(gspca_dev, reg80);
}

static void init_gains(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u16 reg80;
	u8 all_gain_reg[8] =
		{0x87, 0x00, 0x88, 0x00, 0x89, 0x00, 0x80, 0x00};

	all_gain_reg[1] = sd->red_gain;
	all_gain_reg[3] = sd->blue_gain;
	all_gain_reg[5] = sd->green_gain;
	reg80 = sensor_data[sd->sensor].reg80;
	if (!sd->awb)
		reg80 &= ~0x04;
	all_gain_reg[7] = reg80;
	reg_w_buf(gspca_dev, all_gain_reg, sizeof all_gain_reg);

	reg_w(gspca_dev, (sd->red_gain  << 8) + 0x87);
	reg_w(gspca_dev, (sd->blue_gain << 8) + 0x88);
	reg_w(gspca_dev, (sd->green_gain  << 8) + 0x89);
}

static void setsharpness(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u16 reg_to_write;

	reg_to_write = 0x0aa6 + 0x1000 * sd->sharpness;

	reg_w(gspca_dev, reg_to_write);
}

static void setfreq(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 reg66;
	u8 freq[4] = { 0x66, 0x00, 0xa8, 0xe8 };

	switch (sd->sensor) {
	case SENSOR_LT168G:
		if (sd->freq != 0)
			freq[3] = 0xa8;
		reg66 = 0x41;
		break;
	case SENSOR_OM6802:
		reg66 = 0xca;
		break;
	default:
		reg66 = 0x40;
		break;
	}
	switch (sd->freq) {
	case 0:				/* no flicker */
		freq[3] = 0xf0;
		break;
	case 2:				/* 60Hz */
		reg66 &= ~0x40;
		break;
	}
	freq[1] = reg66;

	reg_w_buf(gspca_dev, freq, sizeof freq);
}

/* this function is called at probe and resume time */
static int sd_init(struct gspca_dev *gspca_dev)
{
	/* some of this registers are not really neded, because
	 * they are overriden by setbrigthness, setcontrast, etc,
	 * but wont hurt anyway, and can help someone with similar webcam
	 * to see the initial parameters.*/
	struct sd *sd = (struct sd *) gspca_dev;
	const struct additional_sensor_data *sensor;
	int i;
	u16 sensor_id;
	u8 test_byte = 0;

	static const u8 read_indexs[] =
		{ 0x0a, 0x0b, 0x66, 0x80, 0x81, 0x8e, 0x8f, 0xa5,
		  0xa6, 0xa8, 0xbb, 0xbc, 0xc6, 0x00 };
	static const u8 n1[] =
			{0x08, 0x03, 0x09, 0x03, 0x12, 0x04};
	static const u8 n2[] =
			{0x08, 0x00};

	sensor_id = (reg_r(gspca_dev, 0x06) << 8)
			| reg_r(gspca_dev, 0x07);
	switch (sensor_id & 0xff0f) {
	case 0x0801:
		PDEBUG(D_PROBE, "sensor tas5130a");
		sd->sensor = SENSOR_TAS5130A;
		break;
	case 0x0802:
		PDEBUG(D_PROBE, "sensor lt168g");
		sd->sensor = SENSOR_LT168G;
		break;
	case 0x0803:
		PDEBUG(D_PROBE, "sensor 'other'");
		sd->sensor = SENSOR_OTHER;
		break;
	case 0x0807:
		PDEBUG(D_PROBE, "sensor om6802");
		sd->sensor = SENSOR_OM6802;
		break;
	default:
		PDEBUG(D_ERR|D_PROBE, "unknown sensor %04x", sensor_id);
		return -EINVAL;
	}

	if (sd->sensor == SENSOR_OM6802) {
		reg_w_buf(gspca_dev, n1, sizeof n1);
		i = 5;
		while (--i >= 0) {
			reg_w_buf(gspca_dev, sensor_reset, sizeof sensor_reset);
			test_byte = reg_r(gspca_dev, 0x0063);
			msleep(100);
			if (test_byte == 0x17)
				break;		/* OK */
		}
		if (i < 0) {
			err("Bad sensor reset %02x", test_byte);
			return -EIO;
		}
		reg_w_buf(gspca_dev, n2, sizeof n2);
	}

	i = 0;
	while (read_indexs[i] != 0x00) {
		test_byte = reg_r(gspca_dev, read_indexs[i]);
		PDEBUG(D_STREAM, "Reg 0x%02x = 0x%02x", read_indexs[i],
		       test_byte);
		i++;
	}

	sensor = &sensor_data[sd->sensor];
	reg_w_buf(gspca_dev, sensor->n3, sizeof sensor->n3);
	reg_w_buf(gspca_dev, sensor->n4, sensor->n4sz);

	if (sd->sensor == SENSOR_LT168G) {
		test_byte = reg_r(gspca_dev, 0x80);
		PDEBUG(D_STREAM, "Reg 0x%02x = 0x%02x", 0x80,
		       test_byte);
		reg_w(gspca_dev, 0x6c80);
	}

	reg_w_ixbuf(gspca_dev, 0xd0, sensor->data1, sizeof sensor->data1);
	reg_w_ixbuf(gspca_dev, 0xc7, sensor->data2, sizeof sensor->data2);
	reg_w_ixbuf(gspca_dev, 0xe0, sensor->data3, sizeof sensor->data3);

	reg_w(gspca_dev, (sensor->reg80 << 8) + 0x80);
	reg_w(gspca_dev, (sensor->reg80 << 8) + 0x80);
	reg_w(gspca_dev, (sensor->reg8e << 8) + 0x8e);

	setbrightness(gspca_dev);
	setcontrast(gspca_dev);
	setgamma(gspca_dev);
	setcolors(gspca_dev);
	setsharpness(gspca_dev);
	init_gains(gspca_dev);
	setfreq(gspca_dev);

	reg_w_buf(gspca_dev, sensor->data5, sizeof sensor->data5);
	reg_w_buf(gspca_dev, sensor->nset8, sizeof sensor->nset8);
	reg_w_buf(gspca_dev, sensor->stream, sizeof sensor->stream);

	if (sd->sensor == SENSOR_LT168G) {
		test_byte = reg_r(gspca_dev, 0x80);
		PDEBUG(D_STREAM, "Reg 0x%02x = 0x%02x", 0x80,
		       test_byte);
		reg_w(gspca_dev, 0x6c80);
	}

	reg_w_ixbuf(gspca_dev, 0xd0, sensor->data1, sizeof sensor->data1);
	reg_w_ixbuf(gspca_dev, 0xc7, sensor->data2, sizeof sensor->data2);
	reg_w_ixbuf(gspca_dev, 0xe0, sensor->data3, sizeof sensor->data3);

	return 0;
}

static void setmirror(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 hflipcmd[8] =
		{0x62, 0x07, 0x63, 0x03, 0x64, 0x00, 0x60, 0x09};

	if (sd->mirror)
		hflipcmd[3] = 0x01;

	reg_w_buf(gspca_dev, hflipcmd, sizeof hflipcmd);
}

static void seteffect(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	reg_w_buf(gspca_dev, effects_table[sd->effect],
				sizeof effects_table[0]);
	if (sd->effect == 1 || sd->effect == 5) {
		PDEBUG(D_CONF,
		       "This effect have been disabled for webcam \"safety\"");
		return;
	}

	if (sd->effect == 1 || sd->effect == 4)
		reg_w(gspca_dev, 0x4aa6);
	else
		reg_w(gspca_dev, 0xfaa6);
}

/* Is this really needed?
 * i added some module parameters for test with some users */
static void poll_sensor(struct gspca_dev *gspca_dev)
{
	static const u8 poll1[] =
		{0x67, 0x05, 0x68, 0x81, 0x69, 0x80, 0x6a, 0x82,
		 0x6b, 0x68, 0x6c, 0x69, 0x72, 0xd9, 0x73, 0x34,
		 0x74, 0x32, 0x75, 0x92, 0x76, 0x00, 0x09, 0x01,
		 0x60, 0x14};
	static const u8 poll2[] =
		{0x67, 0x02, 0x68, 0x71, 0x69, 0x72, 0x72, 0xa9,
		 0x73, 0x02, 0x73, 0x02, 0x60, 0x14};
	static const u8 noise03[] =	/* (some differences / ms-drv) */
		{0xa6, 0x0a, 0xea, 0xcf, 0xbe, 0x26, 0xb1, 0x5f,
		 0xa1, 0xb1, 0xda, 0x6b, 0xdb, 0x98, 0xdf, 0x0c,
		 0xc2, 0x80, 0xc3, 0x10};

	PDEBUG(D_STREAM, "[Sensor requires polling]");
	reg_w_buf(gspca_dev, poll1, sizeof poll1);
	reg_w_buf(gspca_dev, poll2, sizeof poll2);
	reg_w_buf(gspca_dev, noise03, sizeof noise03);
}

static int sd_start(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	const struct additional_sensor_data *sensor;
	int i, mode;
	u8 t2[] = { 0x07, 0x00, 0x0d, 0x60, 0x0e, 0x80 };
	static const u8 t3[] =
		{ 0x07, 0x00, 0x88, 0x02, 0x06, 0x00, 0xe7, 0x01 };

	mode = gspca_dev->cam.cam_mode[gspca_dev->curr_mode].priv;
	switch (mode) {
	case 0:		/* 640x480 (0x00) */
		break;
	case 1:		/* 352x288 */
		t2[1] = 0x40;
		break;
	case 2:		/* 320x240 */
		t2[1] = 0x10;
		break;
	case 3:		/* 176x144 */
		t2[1] = 0x50;
		break;
	default:
/*	case 4:		 * 160x120 */
		t2[1] = 0x20;
		break;
	}

	switch (sd->sensor) {
	case SENSOR_OM6802:
		om6802_sensor_init(gspca_dev);
		break;
	case SENSOR_TAS5130A:
		i = 0;
		for (;;) {
			reg_w_buf(gspca_dev, tas5130a_sensor_init[i],
					 sizeof tas5130a_sensor_init[0]);
			if (i >= ARRAY_SIZE(tas5130a_sensor_init) - 1)
				break;
			i++;
		}
		reg_w(gspca_dev, 0x3c80);
		/* just in case and to keep sync with logs (for mine) */
		reg_w_buf(gspca_dev, tas5130a_sensor_init[i],
				 sizeof tas5130a_sensor_init[0]);
		reg_w(gspca_dev, 0x3c80);
		break;
	}
	sensor = &sensor_data[sd->sensor];
	setfreq(gspca_dev);
	reg_r(gspca_dev, 0x0012);
	reg_w_buf(gspca_dev, t2, sizeof t2);
	reg_w_ixbuf(gspca_dev, 0xb3, t3, sizeof t3);
	reg_w(gspca_dev, 0x0013);
	msleep(15);
	reg_w_buf(gspca_dev, sensor->stream, sizeof sensor->stream);
	reg_w_buf(gspca_dev, sensor->stream, sizeof sensor->stream);

	if (sd->sensor == SENSOR_OM6802)
		poll_sensor(gspca_dev);

	return 0;
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	reg_w_buf(gspca_dev, sensor_data[sd->sensor].stream,
			sizeof sensor_data[sd->sensor].stream);
	reg_w_buf(gspca_dev, sensor_data[sd->sensor].stream,
			sizeof sensor_data[sd->sensor].stream);
	if (sd->sensor == SENSOR_OM6802) {
		msleep(20);
		reg_w(gspca_dev, 0x0309);
	}
}

static void sd_pkt_scan(struct gspca_dev *gspca_dev,
			u8 *data,			/* isoc packet */
			int len)			/* iso packet length */
{
	int pkt_type;

	if (data[0] == 0x5a) {
		/* Control Packet, after this came the header again,
		 * but extra bytes came in the packet before this,
		 * sometimes an EOF arrives, sometimes not... */
		return;
	}
	data += 2;
	len -= 2;
	if (data[0] == 0xff && data[1] == 0xd8)
		pkt_type = FIRST_PACKET;
	else if (data[len - 2] == 0xff && data[len - 1] == 0xd9)
		pkt_type = LAST_PACKET;
	else
		pkt_type = INTER_PACKET;
	gspca_frame_add(gspca_dev, pkt_type, data, len);
}

static int sd_setblue_gain(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->blue_gain = val;
	if (gspca_dev->streaming)
		reg_w(gspca_dev, (val << 8) + 0x88);
	return 0;
}

static int sd_getblue_gain(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->blue_gain;
	return 0;
}

static int sd_setred_gain(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->red_gain = val;
	if (gspca_dev->streaming)
		reg_w(gspca_dev, (val << 8) + 0x87);

	return 0;
}

static int sd_getred_gain(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->red_gain;
	return 0;
}

static int sd_setgain(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u16 psg, nsg;

	psg = sd->red_gain + sd->blue_gain + sd->green_gain;
	nsg = val * 3;
	sd->red_gain = sd->red_gain * nsg / psg;
	if (sd->red_gain > 0x40)
		sd->red_gain = 0x40;
	else if (sd->red_gain < 0x10)
		sd->red_gain = 0x10;
	sd->blue_gain = sd->blue_gain * nsg / psg;
	if (sd->blue_gain > 0x40)
		sd->blue_gain = 0x40;
	else if (sd->blue_gain < 0x10)
		sd->blue_gain = 0x10;
	sd->green_gain = sd->green_gain * nsg / psg;
	if (sd->green_gain > 0x40)
		sd->green_gain = 0x40;
	else if (sd->green_gain < 0x10)
		sd->green_gain = 0x10;

	if (gspca_dev->streaming)
		setRGB(gspca_dev);
	return 0;
}

static int sd_getgain(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = (sd->red_gain + sd->blue_gain + sd->green_gain) / 3;
	return 0;
}

static int sd_setbrightness(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->brightness = val;
	if (gspca_dev->streaming)
		setbrightness(gspca_dev);
	return 0;
}

static int sd_getbrightness(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->brightness;
	return *val;
}

static int sd_setawb(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->awb = val;
	if (gspca_dev->streaming)
		setawb(gspca_dev);
	return 0;
}

static int sd_getawb(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->awb;
	return *val;
}

static int sd_setmirror(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->mirror = val;
	if (gspca_dev->streaming)
		setmirror(gspca_dev);
	return 0;
}

static int sd_getmirror(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->mirror;
	return *val;
}

static int sd_seteffect(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->effect = val;
	if (gspca_dev->streaming)
		seteffect(gspca_dev);
	return 0;
}

static int sd_geteffect(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->effect;
	return *val;
}

static int sd_setcontrast(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->contrast = val;
	if (gspca_dev->streaming)
		setcontrast(gspca_dev);
	return 0;
}

static int sd_getcontrast(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->contrast;
	return *val;
}

static int sd_setcolors(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->colors = val;
	if (gspca_dev->streaming)
		setcolors(gspca_dev);
	return 0;
}

static int sd_getcolors(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->colors;
	return 0;
}

static int sd_setgamma(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->gamma = val;
	if (gspca_dev->streaming)
		setgamma(gspca_dev);
	return 0;
}

static int sd_getgamma(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->gamma;
	return 0;
}

static int sd_setfreq(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->freq = val;
	if (gspca_dev->streaming)
		setfreq(gspca_dev);
	return 0;
}

static int sd_getfreq(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->freq;
	return 0;
}

static int sd_setsharpness(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->sharpness = val;
	if (gspca_dev->streaming)
		setsharpness(gspca_dev);
	return 0;
}

static int sd_getsharpness(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->sharpness;
	return 0;
}

/* Low Light set  here......*/
static int sd_setlowlight(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->autogain = val;
	if (val != 0)
		reg_w(gspca_dev, 0xf48e);
	else
		reg_w(gspca_dev, 0xb48e);
	return 0;
}

static int sd_getlowlight(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->autogain;
	return 0;
}

static int sd_querymenu(struct gspca_dev *gspca_dev,
			struct v4l2_querymenu *menu)
{
	switch (menu->id) {
	case V4L2_CID_POWER_LINE_FREQUENCY:
		switch (menu->index) {
		case 1:		/* V4L2_CID_POWER_LINE_FREQUENCY_50HZ */
			strcpy((char *) menu->name, "50 Hz");
			return 0;
		case 2:		/* V4L2_CID_POWER_LINE_FREQUENCY_60HZ */
			strcpy((char *) menu->name, "60 Hz");
			return 0;
		}
		break;
	case V4L2_CID_EFFECTS:
		if ((unsigned) menu->index < ARRAY_SIZE(effects_control)) {
			strncpy((char *) menu->name,
				effects_control[menu->index],
				sizeof menu->name);
			return 0;
		}
		break;
	}
	return -EINVAL;
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
	.pkt_scan = sd_pkt_scan,
	.querymenu = sd_querymenu,
};

/* -- module initialisation -- */
static const __devinitdata struct usb_device_id device_table[] = {
	{USB_DEVICE(0x17a1, 0x0128)},
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

static struct usb_driver sd_driver = {
	.name = MODULE_NAME,
	.id_table = device_table,
	.probe = sd_probe,
	.disconnect = gspca_disconnect,
#ifdef CONFIG_PM
	.suspend = gspca_suspend,
	.resume = gspca_resume,
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
