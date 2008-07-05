/*
 *		sonix sn9c102 (bayer) library
 *		Copyright (C) 2003 2004 Michel Xhaard mxhaard@magic.fr
 * Add Pas106 Stefano Mozzi (C) 2004
 *
 * V4L2 by Jean-Francois Moine <http://moinejf.free.fr>
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

#define MODULE_NAME "sonixb"

#include "gspca.h"

#define DRIVER_VERSION_NUMBER	KERNEL_VERSION(2, 1, 5)
static const char version[] = "2.1.5";

MODULE_AUTHOR("Michel Xhaard <mxhaard@users.sourceforge.net>");
MODULE_DESCRIPTION("GSPCA/SN9C102 USB Camera Driver");
MODULE_LICENSE("GPL");

/* specific webcam descriptor */
struct sd {
	struct gspca_dev gspca_dev;	/* !! must be the first item */

	unsigned char brightness;
	unsigned char contrast;

	char sensor;			/* Type of image sensor chip */
#define SENSOR_HV7131R 0
#define SENSOR_OV6650 1
#define SENSOR_OV7630 2
#define SENSOR_OV7630_3 3
#define SENSOR_PAS106 4
#define SENSOR_PAS202 5
#define SENSOR_TAS5110 6
#define SENSOR_TAS5130CXX 7
};

#define COMP2 0x8f
#define COMP 0xc7		/* 0x87 //0x07 */
#define COMP1 0xc9		/* 0x89 //0x09 */

#define MCK_INIT 0x63
#define MCK_INIT1 0x20		/*fixme: Bayer - 0x50 for JPEG ??*/

#define SYS_CLK 0x04

/* V4L2 controls supported by the driver */
static int sd_setbrightness(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getbrightness(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setcontrast(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getcontrast(struct gspca_dev *gspca_dev, __s32 *val);

static struct ctrl sd_ctrls[] = {
#define SD_BRIGHTNESS 0
	{
	    {
		.id      = V4L2_CID_BRIGHTNESS,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Brightness",
		.minimum = 0,
		.maximum = 255,
		.step    = 1,
		.default_value = 127,
	    },
	    .set = sd_setbrightness,
	    .get = sd_getbrightness,
	},
#define SD_CONTRAST 1
	{
	    {
		.id      = V4L2_CID_CONTRAST,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Contrast",
		.minimum = 0,
		.maximum = 255,
		.step    = 1,
		.default_value = 127,
	    },
	    .set = sd_setcontrast,
	    .get = sd_getcontrast,
	},
};

static struct v4l2_pix_format vga_mode[] = {
	{160, 120, V4L2_PIX_FMT_SN9C10X, V4L2_FIELD_NONE,
		.bytesperline = 160,
		.sizeimage = 160 * 120,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 2},
	{320, 240, V4L2_PIX_FMT_SN9C10X, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 1},
	{640, 480, V4L2_PIX_FMT_SN9C10X, V4L2_FIELD_NONE,
		.bytesperline = 640,
		.sizeimage = 640 * 480,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0},
};
static struct v4l2_pix_format sif_mode[] = {
	{176, 144, V4L2_PIX_FMT_SN9C10X, V4L2_FIELD_NONE,
		.bytesperline = 176,
		.sizeimage = 176 * 144,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 1},
	{352, 288, V4L2_PIX_FMT_SN9C10X, V4L2_FIELD_NONE,
		.bytesperline = 352,
		.sizeimage = 352 * 288,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0},
};

static const __u8 probe_ov7630[] = {0x08, 0x44};

static const __u8 initHv7131[] = {
	0x46, 0x77, 0x00, 0x04, 0x00, 0x00, 0x00, 0x80, 0x11, 0x00, 0x00, 0x00,
	0x00, 0x00,
	0x00, 0x00, 0x00, 0x03, 0x01, 0x00,	/* shift from 0x02 0x01 0x00 */
	0x28, 0x1e, 0x60, 0x8a, 0x20,
	0x1d, 0x10, 0x02, 0x03, 0x0f, 0x0c
};
static const __u8 hv7131_sensor_init[][8] = {
	{0xc0, 0x11, 0x31, 0x38, 0x2a, 0x2e, 0x00, 0x10},
	{0xa0, 0x11, 0x01, 0x08, 0x2a, 0x2e, 0x00, 0x10},
	{0xb0, 0x11, 0x20, 0x00, 0xd0, 0x2e, 0x00, 0x10},
	{0xc0, 0x11, 0x25, 0x03, 0x0e, 0x28, 0x00, 0x16},
	{0xa0, 0x11, 0x30, 0x10, 0x0e, 0x28, 0x00, 0x15},
};
static const __u8 initOv6650[] = {
	0x44, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80,
	0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x02, 0x01, 0x0a, 0x16, 0x12, 0x68, 0x0b,
	0x10, 0x1d, 0x10, 0x00, 0x06, 0x1f, 0x00
};
static const __u8 ov6650_sensor_init[][8] =
{
	/* Bright, contrast, etc are set througth SCBB interface.
	 * AVCAP on win2 do not send any data on this 	controls. */
	/* Anyway, some registers appears to alter bright and constrat */
	{0xa0, 0x60, 0x12, 0x80, 0x00, 0x00, 0x00, 0x10},
	{0xd0, 0x60, 0x11, 0xc0, 0x1b, 0x18, 0xc1, 0x10},
	{0xb0, 0x60, 0x15, 0x00, 0x02, 0x18, 0xc1, 0x10},
/*	{0xa0, 0x60, 0x1b, 0x01, 0x02, 0x18, 0xc1, 0x10},
		 * THIS SET GREEN SCREEN
		 * (pixels could be innverted in decode kind of "brg",
		 * but blue wont be there. Avoid this data ... */
	{0xd0, 0x60, 0x26, 0x01, 0x14, 0xd8, 0xa4, 0x10}, /* format out? */
	{0xd0, 0x60, 0x26, 0x01, 0x14, 0xd8, 0xa4, 0x10},
	{0xa0, 0x60, 0x30, 0x3d, 0x0A, 0xd8, 0xa4, 0x10},
	{0xb0, 0x60, 0x60, 0x66, 0x68, 0xd8, 0xa4, 0x10},
	{0xa0, 0x60, 0x68, 0x04, 0x68, 0xd8, 0xa4, 0x10},
	{0xd0, 0x60, 0x17, 0x24, 0xd6, 0x04, 0x94, 0x10}, /* Clipreg */
	{0xa0, 0x60, 0x10, 0x5d, 0x99, 0x04, 0x94, 0x16},
	{0xa0, 0x60, 0x2d, 0x0a, 0x99, 0x04, 0x94, 0x16},
	{0xa0, 0x60, 0x32, 0x00, 0x99, 0x04, 0x94, 0x16},
	{0xa0, 0x60, 0x33, 0x40, 0x99, 0x04, 0x94, 0x16},
	{0xa0, 0x60, 0x11, 0xc0, 0x99, 0x04, 0x94, 0x16},
	{0xa0, 0x60, 0x00, 0x16, 0x99, 0x04, 0x94, 0x15}, /* bright / Lumino */
	{0xa0, 0x60, 0x2b, 0xab, 0x99, 0x04, 0x94, 0x15},
							/* ?flicker o brillo */
	{0xa0, 0x60, 0x2d, 0x2a, 0x99, 0x04, 0x94, 0x15},
	{0xa0, 0x60, 0x2d, 0x2b, 0x99, 0x04, 0x94, 0x16},
	{0xa0, 0x60, 0x32, 0x00, 0x99, 0x04, 0x94, 0x16},
	{0xa0, 0x60, 0x33, 0x00, 0x99, 0x04, 0x94, 0x16},
	{0xa0, 0x60, 0x10, 0x57, 0x99, 0x04, 0x94, 0x16},
	{0xa0, 0x60, 0x2d, 0x2b, 0x99, 0x04, 0x94, 0x16},
	{0xa0, 0x60, 0x32, 0x00, 0x99, 0x04, 0x94, 0x16},
		/* Low Light (Enabled: 0x32 0x1 | Disabled: 0x32 0x00) */
	{0xa0, 0x60, 0x33, 0x29, 0x99, 0x04, 0x94, 0x16},
		/* Low Ligth (Enabled: 0x33 0x13 | Disabled: 0x33 0x29) */
/*	{0xa0, 0x60, 0x11, 0xc1, 0x99, 0x04, 0x94, 0x16}, */
	{0xa0, 0x60, 0x00, 0x17, 0x99, 0x04, 0x94, 0x15}, /* clip? r */
	{0xa0, 0x60, 0x00, 0x18, 0x99, 0x04, 0x94, 0x15}, /* clip? r */
};
static const __u8 initOv7630[] = {
	0x04, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80,	/* r01 .. r08 */
	0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* r09 .. r10 */
	0x00, 0x02, 0x01, 0x0a,				/* r11 .. r14 */
	0x28, 0x1e,			/* H & V sizes     r15 .. r16 */
	0x68, COMP1, MCK_INIT1,				/* r17 .. r19 */
	0x1d, 0x10, 0x02, 0x03, 0x0f, 0x0c		/* r1a .. r1f */
};
static const __u8 initOv7630_3[] = {
	0x44, 0x44, 0x00, 0x1a, 0x20, 0x20, 0x20, 0x80,	/* r01 .. r08 */
	0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04,	/* r09 .. r10 */
	0x00, 0x02, 0x01, 0x0a,				/* r11 .. r14 */
	0x28, 0x1e,			/* H & V sizes     r15 .. r16 */
	0x68, COMP1, MCK_INIT1,				/* r17 .. r19 */
	0x1d, 0x10, 0x02, 0x03, 0x0f, 0x0c		/* r1a .. r1f */
};
static const __u8 ov7630_sensor_init_com[][8] = {
	{0xa0, 0x21, 0x12, 0x80, 0x00, 0x00, 0x00, 0x10},
	{0xb0, 0x21, 0x01, 0x77, 0x3a, 0x00, 0x00, 0x10},
/*	{0xd0, 0x21, 0x12, 0x7c, 0x01, 0x80, 0x34, 0x10},	   jfm */
	{0xd0, 0x21, 0x12, 0x78, 0x00, 0x80, 0x34, 0x10},	/* jfm */
	{0xa0, 0x21, 0x1b, 0x04, 0x00, 0x80, 0x34, 0x10},
	{0xa0, 0x21, 0x20, 0x44, 0x00, 0x80, 0x34, 0x10},
	{0xa0, 0x21, 0x23, 0xee, 0x00, 0x80, 0x34, 0x10},
	{0xd0, 0x21, 0x26, 0xa0, 0x9a, 0xa0, 0x30, 0x10},
	{0xb0, 0x21, 0x2a, 0x80, 0x00, 0xa0, 0x30, 0x10},
	{0xb0, 0x21, 0x2f, 0x3d, 0x24, 0xa0, 0x30, 0x10},
	{0xa0, 0x21, 0x32, 0x86, 0x24, 0xa0, 0x30, 0x10},
/*	{0xb0, 0x21, 0x60, 0xa9, 0x4a, 0xa0, 0x30, 0x10},	   jfm */
	{0xb0, 0x21, 0x60, 0xa9, 0x42, 0xa0, 0x30, 0x10},	/* jfm */
	{0xa0, 0x21, 0x65, 0x00, 0x42, 0xa0, 0x30, 0x10},
	{0xa0, 0x21, 0x69, 0x38, 0x42, 0xa0, 0x30, 0x10},
	{0xc0, 0x21, 0x6f, 0x88, 0x0b, 0x00, 0x30, 0x10},
	{0xc0, 0x21, 0x74, 0x21, 0x8e, 0x00, 0x30, 0x10},
	{0xa0, 0x21, 0x7d, 0xf7, 0x8e, 0x00, 0x30, 0x10},
	{0xd0, 0x21, 0x17, 0x1c, 0xbd, 0x06, 0xf6, 0x10},
};
static const __u8 ov7630_sensor_init[][8] = {
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /* delay 200ms */
	{0xa0, 0x21, 0x11, 0x01, 0xbd, 0x06, 0xf6, 0x10},	/* jfm */
	{0xa0, 0x21, 0x10, 0x57, 0xbd, 0x06, 0xf6, 0x16},
	{0xa0, 0x21, 0x76, 0x02, 0xbd, 0x06, 0xf6, 0x16},
	{0xa0, 0x21, 0x00, 0x10, 0xbd, 0x06, 0xf6, 0x15},	/* gain */
};
static const __u8 ov7630_sensor_init_3[][8] = {
	{0xa0, 0x21, 0x10, 0x36, 0xbd, 0x06, 0xf6, 0x16},	/* exposure */
	{0xa0, 0x21, 0x76, 0x03, 0xbd, 0x06, 0xf6, 0x16},
	{0xa0, 0x21, 0x11, 0x01, 0xbd, 0x06, 0xf6, 0x16},
	{0xa0, 0x21, 0x00, 0x10, 0xbd, 0x06, 0xf6, 0x15},	/* gain */
/*	{0xb0, 0x21, 0x2a, 0xc0, 0x3c, 0x06, 0xf6, 0x1d},
		* a0 1c,a0 1f,c0 3c frame rate ?line interval from ov6630 */
/*	{0xb0, 0x21, 0x2a, 0xa0, 0x1f, 0x06, 0xf6, 0x1d},	 * from win */
	{0xb0, 0x21, 0x2a, 0xa0, 0x1c, 0x06, 0xf6, 0x1d},
};

static const __u8 initPas106[] = {
	0x04, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x81, 0x40, 0x00, 0x00, 0x00,
	0x00, 0x00,
	0x00, 0x00, 0x00, 0x05, 0x01, 0x00,
	0x16, 0x12, 0x28, COMP1, MCK_INIT1,
	0x18, 0x10, 0x04, 0x03, 0x11, 0x0c
};
/* compression 0x86 mckinit1 0x2b */
static const __u8 pas106_data[][2] = {
	{0x02, 0x04},		/* Pixel Clock Divider 6 */
	{0x03, 0x13},		/* Frame Time MSB */
/*	{0x03, 0x12},		 * Frame Time MSB */
	{0x04, 0x06},		/* Frame Time LSB */
/*	{0x04, 0x05},		 * Frame Time LSB */
	{0x05, 0x65},		/* Shutter Time Line Offset */
/*	{0x05, 0x6d},		 * Shutter Time Line Offset */
/*	{0x06, 0xb1},		 * Shutter Time Pixel Offset */
	{0x06, 0xcd},		/* Shutter Time Pixel Offset */
	{0x07, 0xc1},		/* Black Level Subtract Sign */
/*	{0x07, 0x00},		 * Black Level Subtract Sign */
	{0x08, 0x06},		/* Black Level Subtract Level */
	{0x08, 0x06},		/* Black Level Subtract Level */
/*	{0x08, 0x01},		 * Black Level Subtract Level */
	{0x09, 0x05},		/* Color Gain B Pixel 5 a */
	{0x0a, 0x04},		/* Color Gain G1 Pixel 1 5 */
	{0x0b, 0x04},		/* Color Gain G2 Pixel 1 0 5 */
	{0x0c, 0x05},		/* Color Gain R Pixel 3 1 */
	{0x0d, 0x00},		/* Color GainH  Pixel */
	{0x0e, 0x0e},		/* Global Gain */
	{0x0f, 0x00},		/* Contrast */
	{0x10, 0x06},		/* H&V synchro polarity */
	{0x11, 0x06},		/* ?default */
	{0x12, 0x06},		/* DAC scale */
	{0x14, 0x02},		/* ?default */
	{0x13, 0x01},		/* Validate Settings */
};
static const __u8 initPas202[] = {
	0x44, 0x44, 0x21, 0x30, 0x00, 0x00, 0x00, 0x80, 0x40, 0x00, 0x00, 0x00,
	0x00, 0x00,
	0x00, 0x00, 0x00, 0x07, 0x03, 0x0a,	/* 6 */
	0x28, 0x1e, 0x28, 0x89, 0x30,
	0x00, 0x00, 0x02, 0x03, 0x0f, 0x0c
};
static const __u8 pas202_sensor_init[][8] = {
	{0xa0, 0x40, 0x02, 0x03, 0x00, 0x00, 0x00, 0x10},
	{0xd0, 0x40, 0x04, 0x07, 0x34, 0x00, 0x09, 0x10},
	{0xd0, 0x40, 0x08, 0x01, 0x00, 0x00, 0x01, 0x10},
	{0xd0, 0x40, 0x0C, 0x00, 0x0C, 0x00, 0x32, 0x10},
	{0xd0, 0x40, 0x10, 0x00, 0x01, 0x00, 0x63, 0x10},
	{0xa0, 0x40, 0x15, 0x70, 0x01, 0x00, 0x63, 0x10},
	{0xa0, 0x40, 0x18, 0x00, 0x01, 0x00, 0x63, 0x10},
	{0xa0, 0x40, 0x11, 0x01, 0x01, 0x00, 0x63, 0x10},
	{0xa0, 0x40, 0x03, 0x56, 0x01, 0x00, 0x63, 0x10},
	{0xa0, 0x40, 0x11, 0x01, 0x01, 0x00, 0x63, 0x10},
	{0xb0, 0x40, 0x04, 0x07, 0x2a, 0x00, 0x63, 0x10},
	{0xb0, 0x40, 0x0e, 0x00, 0x3d, 0x00, 0x63, 0x10},

	{0xa0, 0x40, 0x11, 0x01, 0x3d, 0x00, 0x63, 0x16},
	{0xa0, 0x40, 0x10, 0x08, 0x3d, 0x00, 0x63, 0x15},
	{0xa0, 0x40, 0x02, 0x04, 0x3d, 0x00, 0x63, 0x16},
	{0xa0, 0x40, 0x11, 0x01, 0x3d, 0x00, 0x63, 0x16},
	{0xb0, 0x40, 0x0e, 0x00, 0x31, 0x00, 0x63, 0x16},
	{0xa0, 0x40, 0x11, 0x01, 0x31, 0x00, 0x63, 0x16},
	{0xa0, 0x40, 0x10, 0x0e, 0x31, 0x00, 0x63, 0x15},
	{0xa0, 0x40, 0x11, 0x01, 0x31, 0x00, 0x63, 0x16},
};

static const __u8 initTas5110[] = {
	0x44, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x11, 0x00, 0x00, 0x00,
	0x00, 0x00,
	0x00, 0x01, 0x00, 0x46, 0x09, 0x0a,	/* shift from 0x45 0x09 0x0a */
	0x16, 0x12, 0x60, 0x86, 0x2b,
	0x14, 0x0a, 0x02, 0x02, 0x09, 0x07
};
static const __u8 tas5110_sensor_init[][8] = {
	{0x30, 0x11, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x10},
	{0x30, 0x11, 0x02, 0x20, 0xa9, 0x00, 0x00, 0x10},
	{0xa0, 0x61, 0x9a, 0xca, 0x00, 0x00, 0x00, 0x17},
};

static const __u8 initTas5130[] = {
	0x04, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x11, 0x00, 0x00, 0x00,
	0x00, 0x00,
	0x00, 0x01, 0x00, 0x69, 0x0c, 0x0a,
	0x28, 0x1e, 0x60, COMP, MCK_INIT,
	0x18, 0x10, 0x04, 0x03, 0x11, 0x0c
};
static const __u8 tas5130_sensor_init[][8] = {
/* 	{0x30, 0x11, 0x00, 0x40, 0x47, 0x00, 0x00, 0x10},
					* shutter 0x47 short exposure? */
	{0x30, 0x11, 0x00, 0x40, 0x01, 0x00, 0x00, 0x10},
					/* shutter 0x01 long exposure */
	{0x30, 0x11, 0x02, 0x20, 0x70, 0x00, 0x00, 0x10},
};

static void reg_r(struct usb_device *dev,
			 __u16 value, __u8 *buffer)
{
	usb_control_msg(dev,
			usb_rcvctrlpipe(dev, 0),
			0,			/* request */
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_INTERFACE,
			value,
			0,			/* index */
			buffer, 1,
			500);
}

static void reg_w(struct usb_device *dev,
			  __u16 value,
			  const __u8 *buffer,
			  int len)
{
	__u8 tmpbuf[32];

#ifdef CONFIG_VIDEO_ADV_DEBUG
	if (len > sizeof tmpbuf) {
		PDEBUG(D_ERR|D_PACK, "reg_w: buffer overflow");
		return;
	}
#endif
	memcpy(tmpbuf, buffer, len);
	usb_control_msg(dev,
			usb_sndctrlpipe(dev, 0),
			0x08,			/* request */
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_INTERFACE,
			value,
			0,			/* index */
			tmpbuf, len,
			500);
}

static int i2c_w(struct usb_device *dev, const __u8 *buffer)
{
	int retry = 60;
	__u8 ByteReceive;

	/* is i2c ready */
	reg_w(dev, 0x08, buffer, 8);
	while (retry--) {
		msleep(10);
		reg_r(dev, 0x08, &ByteReceive);
		if (ByteReceive == 4)
			return 0;
	}
	return -1;
}

static void i2c_w_vector(struct usb_device *dev,
			const __u8 buffer[][8], int len)
{
	for (;;) {
		reg_w(dev, 0x08, *buffer, 8);
		len -= 8;
		if (len <= 0)
			break;
		buffer++;
	}
}

static void setbrightness(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	__u8 value;

	switch (sd->sensor) {
	case SENSOR_OV6650: {
		__u8 i2cOV6650[] =
			{0xa0, 0x60, 0x06, 0x11, 0x99, 0x04, 0x94, 0x15};

		i2cOV6650[3] = sd->brightness;
		if (i2c_w(gspca_dev->dev, i2cOV6650) < 0)
			 goto err;
		break;
	    }
	case  SENSOR_OV7630: {
		__u8 i2cOV[] =
			{0xa0, 0x21, 0x06, 0x36, 0xbd, 0x06, 0xf6, 0x16};

		/* change reg 0x06 */
		i2cOV[3] = sd->brightness;
		if (i2c_w(gspca_dev->dev, i2cOV) < 0)
			goto err;
		break;
	    }
	case SENSOR_PAS106: {
		__u8 i2c1[] =
			{0xa1, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14};

		i2c1[3] = sd->brightness >> 3;
		i2c1[2] = 0x0e;
		if (i2c_w(gspca_dev->dev, i2c1) < 0)
			goto err;
		i2c1[3] = 0x01;
		i2c1[2] = 0x13;
		if (i2c_w(gspca_dev->dev, i2c1) < 0)
			goto err;
		break;
	    }
	case SENSOR_PAS202: {
		/* __u8 i2cpexpo1[] =
			{0xb0, 0x40, 0x04, 0x07, 0x2a, 0x00, 0x63, 0x16}; */
		__u8 i2cpexpo[] =
			{0xb0, 0x40, 0x0e, 0x01, 0xab, 0x00, 0x63, 0x16};
		__u8 i2cp202[] =
			{0xa0, 0x40, 0x10, 0x0e, 0x31, 0x00, 0x63, 0x15};
		static __u8 i2cpdoit[] =
			{0xa0, 0x40, 0x11, 0x01, 0x31, 0x00, 0x63, 0x16};

		/* change reg 0x10 */
		i2cpexpo[4] = 0xff - sd->brightness;
/*		if(i2c_w(gspca_dev->dev,i2cpexpo1) < 0)
			goto err; */
/*		if(i2c_w(gspca_dev->dev,i2cpdoit) < 0)
			goto err; */
		if (i2c_w(gspca_dev->dev, i2cpexpo) < 0)
			goto err;
		if (i2c_w(gspca_dev->dev, i2cpdoit) < 0)
			goto err;
		i2cp202[3] = sd->brightness >> 3;
		if (i2c_w(gspca_dev->dev, i2cp202) < 0)
			goto err;
		if (i2c_w(gspca_dev->dev, i2cpdoit) < 0)
			goto err;
		break;
	    }
	case SENSOR_TAS5130CXX:
	case SENSOR_TAS5110: {
		__u8 i2c[] =
			{0x30, 0x11, 0x02, 0x20, 0x70, 0x00, 0x00, 0x10};

		value = 0xff - sd->brightness;
		i2c[4] = value;
		PDEBUG(D_CONF, "brightness %d : %d", value, i2c[4]);
		if (i2c_w(gspca_dev->dev, i2c) < 0)
			goto err;
		break;
	    }
	}
	return;
err:
	PDEBUG(D_ERR, "i2c error brightness");
}
static void setcontrast(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	__u8 gain;
	__u8 rgb_value;

	gain = sd->contrast >> 4;
	/* red and blue gain */
	rgb_value = gain << 4 | gain;
	reg_w(gspca_dev->dev, 0x10, &rgb_value, 1);
	/* green gain */
	rgb_value = gain;
	reg_w(gspca_dev->dev, 0x11, &rgb_value, 1);
}

/* this function is called at probe time */
static int sd_config(struct gspca_dev *gspca_dev,
			const struct usb_device_id *id)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct cam *cam;
/*	__u16 vendor; */
	__u16 product;
	int sif = 0;

/*	vendor = id->idVendor; */
	product = id->idProduct;
/*	switch (vendor) { */
/*	case 0x0c45:				 * Sonix */
		switch (product) {
		case 0x6001:			/* SN9C102 */
		case 0x6005:			/* SN9C101 */
		case 0x6007:			/* SN9C101 */
			sd->sensor = SENSOR_TAS5110;
			sif = 1;
			break;
		case 0x6009:			/* SN9C101 */
		case 0x600d:			/* SN9C101 */
		case 0x6029:			/* SN9C101 */
			sd->sensor = SENSOR_PAS106;
			sif = 1;
			break;
		case 0x6011:			/* SN9C101 - SN9C101G */
			sd->sensor = SENSOR_OV6650;
			sif = 1;
			break;
		case 0x6019:			/* SN9C101 */
		case 0x602c:			/* SN9C102 */
		case 0x602e:			/* SN9C102 */
			sd->sensor = SENSOR_OV7630;
			break;
		case 0x60b0:			/* SN9C103 */
			sd->sensor = SENSOR_OV7630_3;
			break;
		case 0x6024:			/* SN9C102 */
		case 0x6025:			/* SN9C102 */
			sd->sensor = SENSOR_TAS5130CXX;
			break;
		case 0x6028:			/* SN9C102 */
			sd->sensor = SENSOR_PAS202;
			break;
		case 0x602d:			/* SN9C102 */
			sd->sensor = SENSOR_HV7131R;
			break;
		case 0x60af:			/* SN9C103 */
			sd->sensor = SENSOR_PAS202;
			break;
		}
/*		break; */
/*	} */

	cam = &gspca_dev->cam;
	cam->dev_name = (char *) id->driver_info;
	cam->epaddr = 0x01;
	if (!sif) {
		cam->cam_mode = vga_mode;
		cam->nmodes = sizeof vga_mode / sizeof vga_mode[0];
	} else {
		cam->cam_mode = sif_mode;
		cam->nmodes = sizeof sif_mode / sizeof sif_mode[0];
	}
	sd->brightness = sd_ctrls[SD_BRIGHTNESS].qctrl.default_value;
	sd->contrast = sd_ctrls[SD_CONTRAST].qctrl.default_value;
	if (sd->sensor == SENSOR_OV7630_3)	/* jfm: from win trace */
		reg_w(gspca_dev->dev, 0x01, probe_ov7630, sizeof probe_ov7630);
	return 0;
}

/* this function is called at open time */
static int sd_open(struct gspca_dev *gspca_dev)
{
	__u8 ByteReceive;

	reg_r(gspca_dev->dev, 0x00, &ByteReceive);
	if (ByteReceive != 0x10)
		return -ENODEV;
	return 0;
}

static void pas106_i2cinit(struct usb_device *dev)
{
	int i;
	const __u8 *data;
	__u8 i2c1[] = { 0xa1, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14 };

	i = ARRAY_SIZE(pas106_data);
	data = pas106_data[0];
	while (--i >= 0) {
		memcpy(&i2c1[2], data, 2);
					/* copy 2 bytes from the template */
		if (i2c_w(dev, i2c1) < 0)
			PDEBUG(D_ERR, "i2c error pas106");
		data += 2;
	}
}

/* -- start the camera -- */
static void sd_start(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct usb_device *dev = gspca_dev->dev;
	int mode, l;
	const __u8 *sn9c10x;
	__u8 reg01, reg17;
	__u8 reg17_19[3];

	mode = gspca_dev->cam.cam_mode[(int) gspca_dev->curr_mode].priv;
	switch (sd->sensor) {
	case SENSOR_HV7131R:
		sn9c10x = initHv7131;
		reg17_19[0] = 0x60;
		reg17_19[1] = (mode << 4) | 0x8a;
		reg17_19[2] = 0x20;
		break;
	case SENSOR_OV6650:
		sn9c10x = initOv6650;
		reg17_19[0] = 0x68;
		reg17_19[1] = (mode << 4) | 0x8b;
		reg17_19[2] = 0x20;
		break;
	case SENSOR_OV7630:
		sn9c10x = initOv7630;
		reg17_19[0] = 0x68;
		reg17_19[1] = (mode << 4) | COMP2;
		reg17_19[2] = MCK_INIT1;
		break;
	case SENSOR_OV7630_3:
		sn9c10x = initOv7630_3;
		reg17_19[0] = 0x68;
		reg17_19[1] = (mode << 4) | COMP2;
		reg17_19[2] = MCK_INIT1;
		break;
	case SENSOR_PAS106:
		sn9c10x = initPas106;
		reg17_19[0] = 0x24;		/* 0x28 */
		reg17_19[1] = (mode << 4) | COMP1;
		reg17_19[2] = MCK_INIT1;
		break;
	case SENSOR_PAS202:
		sn9c10x = initPas202;
		reg17_19[0] = mode ? 0x24 : 0x20;
		reg17_19[1] = (mode << 4) | 0x89;
		reg17_19[2] = 0x20;
		break;
	case SENSOR_TAS5110:
		sn9c10x = initTas5110;
		reg17_19[0] = 0x60;
		reg17_19[1] = (mode << 4) | 0x86;
		reg17_19[2] = 0x2b;		/* 0xf3; */
		break;
	default:
/*	case SENSOR_TAS5130CXX: */
		sn9c10x = initTas5130;
		reg17_19[0] = 0x60;
		reg17_19[1] = (mode << 4) | COMP;
		reg17_19[2] = mode ? 0x23 : 0x43;
		break;
	}
	switch (sd->sensor) {
	case SENSOR_OV7630:
		reg01 = 0x06;
		reg17 = 0x29;
		l = 0x10;
		break;
	case SENSOR_OV7630_3:
		reg01 = 0x44;
		reg17 = 0x68;
		l = 0x10;
		break;
	default:
		reg01 = sn9c10x[0];
		reg17 = sn9c10x[0x17 - 1];
		l = 0x1f;
		break;
	}

	/* reg 0x01 bit 2 video transfert on */
	reg_w(dev, 0x01, &reg01, 1);
	/* reg 0x17 SensorClk enable inv Clk 0x60 */
	reg_w(dev, 0x17, &reg17, 1);
/*fixme: for ov7630 102
	reg_w(dev, 0x01, {0x06, sn9c10x[1]}, 2); */
	/* Set the registers from the template */
	reg_w(dev, 0x01, sn9c10x, l);
	switch (sd->sensor) {
	case SENSOR_HV7131R:
		i2c_w_vector(dev, hv7131_sensor_init,
				sizeof hv7131_sensor_init);
		break;
	case SENSOR_OV6650:
		i2c_w_vector(dev, ov6650_sensor_init,
				sizeof ov6650_sensor_init);
		break;
	case SENSOR_OV7630:
		i2c_w_vector(dev, ov7630_sensor_init_com,
				sizeof ov7630_sensor_init_com);
		msleep(200);
		i2c_w_vector(dev, ov7630_sensor_init,
				sizeof ov7630_sensor_init);
		break;
	case SENSOR_OV7630_3:
		i2c_w_vector(dev, ov7630_sensor_init_com,
				sizeof ov7630_sensor_init_com);
		msleep(200);
		i2c_w_vector(dev, ov7630_sensor_init_3,
				sizeof ov7630_sensor_init_3);
		break;
	case SENSOR_PAS106:
		pas106_i2cinit(dev);
		break;
	case SENSOR_PAS202:
		i2c_w_vector(dev, pas202_sensor_init,
				sizeof pas202_sensor_init);
		break;
	case SENSOR_TAS5110:
		i2c_w_vector(dev, tas5110_sensor_init,
				sizeof tas5110_sensor_init);
		break;
	default:
/*	case SENSOR_TAS5130CXX: */
		i2c_w_vector(dev, tas5130_sensor_init,
				sizeof tas5130_sensor_init);
		break;
	}
	/* H_size V_size  0x28, 0x1e maybe 640x480 */
	reg_w(dev, 0x15, &sn9c10x[0x15 - 1], 2);
	/* compression register */
	reg_w(dev, 0x18, &reg17_19[1], 1);
	/* H_start */		/*fixme: not ov7630*/
	reg_w(dev, 0x12, &sn9c10x[0x12 - 1], 1);
	/* V_START */		/*fixme: not ov7630*/
	reg_w(dev, 0x13, &sn9c10x[0x13 - 1], 1);
	/* reset 0x17 SensorClk enable inv Clk 0x60 */
				/*fixme: ov7630 [17]=68 8f (+20 if 102)*/
	reg_w(dev, 0x17, &reg17_19[0], 1);
	/*MCKSIZE ->3 */	/*fixme: not ov7630*/
	reg_w(dev, 0x19, &reg17_19[2], 1);
	/* AE_STRX AE_STRY AE_ENDX AE_ENDY */
	reg_w(dev, 0x1c, &sn9c10x[0x1c - 1], 4);
	/* Enable video transfert */
	reg_w(dev, 0x01, &sn9c10x[0], 1);
	/* Compression */
	reg_w(dev, 0x18, &reg17_19[1], 2);
	msleep(20);

	setcontrast(gspca_dev);
	setbrightness(gspca_dev);
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	__u8 ByteSend = 0;

	ByteSend = 0x09;	/* 0X00 */
	reg_w(gspca_dev->dev, 0x01, &ByteSend, 1);
}

static void sd_stop0(struct gspca_dev *gspca_dev)
{
}

static void sd_close(struct gspca_dev *gspca_dev)
{
}

static void sd_pkt_scan(struct gspca_dev *gspca_dev,
			struct gspca_frame *frame,	/* target */
			unsigned char *data,		/* isoc packet */
			int len)			/* iso packet length */
{
	int i;

	if (len > 6 && len < 24) {
		for (i = 0; i < len - 6; i++) {
			if (data[0 + i] == 0xff
			    && data[1 + i] == 0xff
			    && data[2 + i] == 0x00
			    && data[3 + i] == 0xc4
			    && data[4 + i] == 0xc4
			    && data[5 + i] == 0x96) {	/* start of frame */
				frame = gspca_frame_add(gspca_dev, LAST_PACKET,
							frame, data, 0);
				data += i + 12;
				len -= i + 12;
				gspca_frame_add(gspca_dev, FIRST_PACKET,
						frame, data, len);
				return;
			}
		}
	}
	gspca_frame_add(gspca_dev, INTER_PACKET,
			frame, data, len);
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
	return 0;
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
	return 0;
}

/* sub-driver description */
static struct sd_desc sd_desc = {
	.name = MODULE_NAME,
	.ctrls = sd_ctrls,
	.nctrls = ARRAY_SIZE(sd_ctrls),
	.config = sd_config,
	.open = sd_open,
	.start = sd_start,
	.stopN = sd_stopN,
	.stop0 = sd_stop0,
	.close = sd_close,
	.pkt_scan = sd_pkt_scan,
};

/* -- module initialisation -- */
#define DVNM(name) .driver_info = (kernel_ulong_t) name
static __devinitdata struct usb_device_id device_table[] = {
	{USB_DEVICE(0x0c45, 0x6001), DVNM("Genius VideoCAM NB")},
	{USB_DEVICE(0x0c45, 0x6005), DVNM("Sweex Tas5110")},
	{USB_DEVICE(0x0c45, 0x6007), DVNM("Sonix sn9c101 + Tas5110D")},
	{USB_DEVICE(0x0c45, 0x6009), DVNM("spcaCam@120")},
	{USB_DEVICE(0x0c45, 0x600d), DVNM("spcaCam@120")},
	{USB_DEVICE(0x0c45, 0x6011), DVNM("MAX Webcam Microdia")},
	{USB_DEVICE(0x0c45, 0x6019), DVNM("Generic Sonix OV7630")},
	{USB_DEVICE(0x0c45, 0x6024), DVNM("Generic Sonix Tas5130c")},
	{USB_DEVICE(0x0c45, 0x6025), DVNM("Xcam Shanga")},
	{USB_DEVICE(0x0c45, 0x6028), DVNM("Sonix Btc Pc380")},
	{USB_DEVICE(0x0c45, 0x6029), DVNM("spcaCam@150")},
	{USB_DEVICE(0x0c45, 0x602c), DVNM("Generic Sonix OV7630")},
	{USB_DEVICE(0x0c45, 0x602d), DVNM("LIC-200 LG")},
	{USB_DEVICE(0x0c45, 0x602e), DVNM("Genius VideoCam Messenger")},
	{USB_DEVICE(0x0c45, 0x60af), DVNM("Trust WB3100P")},
	{USB_DEVICE(0x0c45, 0x60b0), DVNM("Genius VideoCam Look")},
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
};

/* -- module insert / remove -- */
static int __init sd_mod_init(void)
{
	if (usb_register(&sd_driver) < 0)
		return -1;
	PDEBUG(D_PROBE, "v%s registered", version);
	return 0;
}
static void __exit sd_mod_exit(void)
{
	usb_deregister(&sd_driver);
	PDEBUG(D_PROBE, "deregistered");
}

module_init(sd_mod_init);
module_exit(sd_mod_exit);
