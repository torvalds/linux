/*
 *	Sonix sn9c201 sn9c202 library
 *
 * Copyright (C) 2012 Jean-Francois Moine <http://moinejf.free.fr>
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/input.h>

#include "gspca.h"
#include "jpeg.h"

#include <media/v4l2-chip-ident.h>
#include <linux/dmi.h>

MODULE_AUTHOR("Brian Johnson <brijohn@gmail.com>, "
		"microdia project <microdia@googlegroups.com>");
MODULE_DESCRIPTION("GSPCA/SN9C20X USB Camera Driver");
MODULE_LICENSE("GPL");

/*
 * Pixel format private data
 */
#define SCALE_MASK	0x0f
#define SCALE_160x120	0
#define SCALE_320x240	1
#define SCALE_640x480	2
#define SCALE_1280x1024	3
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
#define SENSOR_MT9M112  10
#define SENSOR_HV7131R	11
#define SENSOR_MT9VPRB	12

/* camera flags */
#define HAS_NO_BUTTON	0x1
#define LED_REVERSE	0x2 /* some cameras unset gpio to turn on leds */
#define FLIP_DETECT	0x4

/* specific webcam descriptor */
struct sd {
	struct gspca_dev gspca_dev;

	struct { /* color control cluster */
		struct v4l2_ctrl *brightness;
		struct v4l2_ctrl *contrast;
		struct v4l2_ctrl *saturation;
		struct v4l2_ctrl *hue;
	};
	struct { /* blue/red balance control cluster */
		struct v4l2_ctrl *blue;
		struct v4l2_ctrl *red;
	};
	struct { /* h/vflip control cluster */
		struct v4l2_ctrl *hflip;
		struct v4l2_ctrl *vflip;
	};
	struct v4l2_ctrl *gamma;
	struct { /* autogain and exposure or gain control cluster */
		struct v4l2_ctrl *autogain;
		struct v4l2_ctrl *exposure;
		struct v4l2_ctrl *gain;
	};
	struct v4l2_ctrl *jpegqual;

	struct work_struct work;
	struct workqueue_struct *work_thread;

	u32 pktsz;			/* (used by pkt_scan) */
	u16 npkt;
	s8 nchg;
	u8 fmt;				/* (used for JPEG QTAB update */

#define MIN_AVG_LUM 80
#define MAX_AVG_LUM 130
	atomic_t avg_lum;
	u8 old_step;
	u8 older_step;
	u8 exposure_step;

	u8 i2c_addr;
	u8 i2c_intf;
	u8 sensor;
	u8 hstart;
	u8 vstart;

	u8 jpeg_hdr[JPEG_HDR_SZ];

	u8 flags;
};

static void qual_upd(struct work_struct *work);

struct i2c_reg_u8 {
	u8 reg;
	u8 val;
};

struct i2c_reg_u16 {
	u8 reg;
	u16 val;
};

static const struct dmi_system_id flip_dmi_table[] = {
	{
		.ident = "MSI MS-1034",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "MICRO-STAR INT'L CO.,LTD."),
			DMI_MATCH(DMI_PRODUCT_NAME, "MS-1034"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "0341")
		}
	},
	{
		.ident = "MSI MS-1632",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "MSI"),
			DMI_MATCH(DMI_BOARD_NAME, "MS-1632")
		}
	},
	{
		.ident = "MSI MS-1633X",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "MSI"),
			DMI_MATCH(DMI_BOARD_NAME, "MS-1633X")
		}
	},
	{
		.ident = "MSI MS-1635X",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "MSI"),
			DMI_MATCH(DMI_BOARD_NAME, "MS-1635X")
		}
	},
	{
		.ident = "ASUSTeK W7J",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "ASUSTeK Computer Inc."),
			DMI_MATCH(DMI_BOARD_NAME, "W7J       ")
		}
	},
	{}
};

static const struct v4l2_pix_format vga_mode[] = {
	{160, 120, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 160,
		.sizeimage = 160 * 120 * 4 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = SCALE_160x120 | MODE_JPEG},
	{160, 120, V4L2_PIX_FMT_SBGGR8, V4L2_FIELD_NONE,
		.bytesperline = 160,
		.sizeimage = 160 * 120,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = SCALE_160x120 | MODE_RAW},
	{160, 120, V4L2_PIX_FMT_SN9C20X_I420, V4L2_FIELD_NONE,
		.bytesperline = 160,
		.sizeimage = 240 * 120,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = SCALE_160x120},
	{320, 240, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240 * 4 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = SCALE_320x240 | MODE_JPEG},
	{320, 240, V4L2_PIX_FMT_SBGGR8, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240 ,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = SCALE_320x240 | MODE_RAW},
	{320, 240, V4L2_PIX_FMT_SN9C20X_I420, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 480 * 240 ,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = SCALE_320x240},
	{640, 480, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 640,
		.sizeimage = 640 * 480 * 4 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = SCALE_640x480 | MODE_JPEG},
	{640, 480, V4L2_PIX_FMT_SBGGR8, V4L2_FIELD_NONE,
		.bytesperline = 640,
		.sizeimage = 640 * 480,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = SCALE_640x480 | MODE_RAW},
	{640, 480, V4L2_PIX_FMT_SN9C20X_I420, V4L2_FIELD_NONE,
		.bytesperline = 640,
		.sizeimage = 960 * 480,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = SCALE_640x480},
};

static const struct v4l2_pix_format sxga_mode[] = {
	{160, 120, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 160,
		.sizeimage = 160 * 120 * 4 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = SCALE_160x120 | MODE_JPEG},
	{160, 120, V4L2_PIX_FMT_SBGGR8, V4L2_FIELD_NONE,
		.bytesperline = 160,
		.sizeimage = 160 * 120,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = SCALE_160x120 | MODE_RAW},
	{160, 120, V4L2_PIX_FMT_SN9C20X_I420, V4L2_FIELD_NONE,
		.bytesperline = 160,
		.sizeimage = 240 * 120,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = SCALE_160x120},
	{320, 240, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240 * 4 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = SCALE_320x240 | MODE_JPEG},
	{320, 240, V4L2_PIX_FMT_SBGGR8, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240 ,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = SCALE_320x240 | MODE_RAW},
	{320, 240, V4L2_PIX_FMT_SN9C20X_I420, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 480 * 240 ,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = SCALE_320x240},
	{640, 480, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 640,
		.sizeimage = 640 * 480 * 4 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = SCALE_640x480 | MODE_JPEG},
	{640, 480, V4L2_PIX_FMT_SBGGR8, V4L2_FIELD_NONE,
		.bytesperline = 640,
		.sizeimage = 640 * 480,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = SCALE_640x480 | MODE_RAW},
	{640, 480, V4L2_PIX_FMT_SN9C20X_I420, V4L2_FIELD_NONE,
		.bytesperline = 640,
		.sizeimage = 960 * 480,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = SCALE_640x480},
	{1280, 1024, V4L2_PIX_FMT_SBGGR8, V4L2_FIELD_NONE,
		.bytesperline = 1280,
		.sizeimage = 1280 * 1024,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = SCALE_1280x1024 | MODE_RAW | MODE_SXGA},
};

static const struct v4l2_pix_format mono_mode[] = {
	{160, 120, V4L2_PIX_FMT_GREY, V4L2_FIELD_NONE,
		.bytesperline = 160,
		.sizeimage = 160 * 120,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = SCALE_160x120 | MODE_RAW},
	{320, 240, V4L2_PIX_FMT_GREY, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240 ,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = SCALE_320x240 | MODE_RAW},
	{640, 480, V4L2_PIX_FMT_GREY, V4L2_FIELD_NONE,
		.bytesperline = 640,
		.sizeimage = 640 * 480,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = SCALE_640x480 | MODE_RAW},
	{1280, 1024, V4L2_PIX_FMT_GREY, V4L2_FIELD_NONE,
		.bytesperline = 1280,
		.sizeimage = 1280 * 1024,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = SCALE_1280x1024 | MODE_RAW | MODE_SXGA},
};

static const s16 hsv_red_x[] = {
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

static const s16 hsv_red_y[] = {
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

static const s16 hsv_green_x[] = {
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

static const s16 hsv_green_y[] = {
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

static const s16 hsv_blue_x[] = {
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

static const s16 hsv_blue_y[] = {
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

static const u16 i2c_ident[] = {
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
	V4L2_IDENT_MT9M112,
	V4L2_IDENT_HV7131R,
[SENSOR_MT9VPRB] = V4L2_IDENT_UNKNOWN,
};

static const u16 bridge_init[][2] = {
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
	{0x1183, 0x00},	{0x1184, 0x50},	{0x1185, 0x80},
	{0x1007, 0x00}
};

/* Gain = (bit[3:0] / 16 + 1) * (bit[4] + 1) * (bit[5] + 1) * (bit[6] + 1) */
static const u8 ov_gain[] = {
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
static const u16 micron1_gain[] = {
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
static const u16 micron2_gain[] = {
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
static const u8 hv7131r_gain[] = {
	0x08 /* 1x */, 0x0c /* 1.25x */, 0x10 /* 1.5x */, 0x14 /* 1.75x */,
	0x18 /* 2x */, 0x1c /* 2.25x */, 0x20 /* 2.5x */, 0x24 /* 2.75x */,
	0x28 /* 3x */, 0x2c /* 3.25x */, 0x30 /* 3.5x */, 0x34 /* 3.75x */,
	0x38 /* 4x */, 0x3c /* 4.25x */, 0x40 /* 4.5x */, 0x44 /* 4.75x */,
	0x48 /* 5x */, 0x4c /* 5.25x */, 0x50 /* 5.5x */, 0x54 /* 5.75x */,
	0x58 /* 6x */, 0x5c /* 6.25x */, 0x60 /* 6.5x */, 0x64 /* 6.75x */,
	0x68 /* 7x */, 0x6c /* 7.25x */, 0x70 /* 7.5x */, 0x74 /* 7.75x */,
	0x78 /* 8x */
};

static const struct i2c_reg_u8 soi968_init[] = {
	{0x0c, 0x00}, {0x0f, 0x1f},
	{0x11, 0x80}, {0x38, 0x52}, {0x1e, 0x00},
	{0x33, 0x08}, {0x35, 0x8c}, {0x36, 0x0c},
	{0x37, 0x04}, {0x45, 0x04}, {0x47, 0xff},
	{0x3e, 0x00}, {0x3f, 0x00}, {0x3b, 0x20},
	{0x3a, 0x96}, {0x3d, 0x0a}, {0x14, 0x8e},
	{0x13, 0x8b}, {0x12, 0x40}, {0x17, 0x13},
	{0x18, 0x63}, {0x19, 0x01}, {0x1a, 0x79},
	{0x32, 0x24}, {0x03, 0x00}, {0x11, 0x40},
	{0x2a, 0x10}, {0x2b, 0xe0}, {0x10, 0x32},
	{0x00, 0x00}, {0x01, 0x80}, {0x02, 0x80},
};

static const struct i2c_reg_u8 ov7660_init[] = {
	{0x0e, 0x80}, {0x0d, 0x08}, {0x0f, 0xc3},
	{0x04, 0xc3}, {0x10, 0x40}, {0x11, 0x40},
	{0x12, 0x05}, {0x13, 0xba}, {0x14, 0x2a},
	/* HDG Set hstart and hstop, datasheet default 0x11, 0x61, using
	   0x10, 0x61 and sd->hstart, vstart = 3, fixes ugly colored borders */
	{0x17, 0x10}, {0x18, 0x61},
	{0x37, 0x0f}, {0x38, 0x02}, {0x39, 0x43},
	{0x3a, 0x00}, {0x69, 0x90}, {0x2d, 0x00},
	{0x2e, 0x00}, {0x01, 0x78}, {0x02, 0x50},
};

static const struct i2c_reg_u8 ov7670_init[] = {
	{0x11, 0x80}, {0x3a, 0x04}, {0x12, 0x01},
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

static const struct i2c_reg_u8 ov9650_init[] = {
	{0x00, 0x00}, {0x01, 0x78},
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

static const struct i2c_reg_u8 ov9655_init[] = {
	{0x0e, 0x61}, {0x11, 0x80}, {0x13, 0xba},
	{0x14, 0x2e}, {0x16, 0x24}, {0x1e, 0x04}, {0x27, 0x08},
	{0x28, 0x08}, {0x29, 0x15}, {0x2c, 0x08}, {0x34, 0x3d},
	{0x35, 0x00}, {0x38, 0x12}, {0x0f, 0x42}, {0x39, 0x57},
	{0x3a, 0x00}, {0x3b, 0xcc}, {0x3c, 0x0c}, {0x3d, 0x19},
	{0x3e, 0x0c}, {0x3f, 0x01}, {0x41, 0x40}, {0x42, 0x80},
	{0x45, 0x46}, {0x46, 0x62}, {0x47, 0x2a}, {0x48, 0x3c},
	{0x4a, 0xf0}, {0x4b, 0xdc}, {0x4c, 0xdc}, {0x4d, 0xdc},
	{0x4e, 0xdc}, {0x6c, 0x04}, {0x6f, 0x9e}, {0x70, 0x05},
	{0x71, 0x78}, {0x77, 0x02}, {0x8a, 0x23}, {0x90, 0x7e},
	{0x91, 0x7c}, {0x9f, 0x6e}, {0xa0, 0x6e}, {0xa5, 0x68},
	{0xa6, 0x60}, {0xa8, 0xc1}, {0xa9, 0xfa}, {0xaa, 0x92},
	{0xab, 0x04}, {0xac, 0x80}, {0xad, 0x80}, {0xae, 0x80},
	{0xaf, 0x80}, {0xb2, 0xf2}, {0xb3, 0x20}, {0xb5, 0x00},
	{0xb6, 0xaf}, {0xbb, 0xae}, {0xbc, 0x44}, {0xbd, 0x44},
	{0xbe, 0x3b}, {0xbf, 0x3a}, {0xc1, 0xc8}, {0xc2, 0x01},
	{0xc4, 0x00}, {0xc6, 0x85}, {0xc7, 0x81}, {0xc9, 0xe0},
	{0xca, 0xe8}, {0xcc, 0xd8}, {0xcd, 0x93}, {0x2d, 0x00},
	{0x2e, 0x00}, {0x01, 0x80}, {0x02, 0x80}, {0x12, 0x61},
	{0x36, 0xfa}, {0x8c, 0x8d}, {0xc0, 0xaa}, {0x69, 0x0a},
	{0x03, 0x09}, {0x17, 0x16}, {0x18, 0x6e}, {0x19, 0x01},
	{0x1a, 0x3e}, {0x32, 0x09}, {0x2a, 0x10}, {0x2b, 0x0a},
	{0x92, 0x00}, {0x93, 0x00}, {0xa1, 0x00}, {0x10, 0x7c},
	{0x04, 0x03}, {0x00, 0x13},
};

static const struct i2c_reg_u16 mt9v112_init[] = {
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

static const struct i2c_reg_u16 mt9v111_init[] = {
	{0x01, 0x0004}, {0x0d, 0x0001}, {0x0d, 0x0000},
	{0x01, 0x0001}, {0x05, 0x0004}, {0x2d, 0xe0a0},
	{0x2e, 0x0c64},	{0x2f, 0x0064}, {0x06, 0x600e},
	{0x08, 0x0480}, {0x01, 0x0004}, {0x02, 0x0016},
	{0x03, 0x01e7}, {0x04, 0x0287}, {0x05, 0x0004},
	{0x06, 0x002d},	{0x07, 0x3002}, {0x08, 0x0008},
	{0x0e, 0x0008}, {0x20, 0x0000}
};

static const struct i2c_reg_u16 mt9v011_init[] = {
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

static const struct i2c_reg_u16 mt9m001_init[] = {
	{0x0d, 0x0001},
	{0x0d, 0x0000},
	{0x04, 0x0500},		/* hres = 1280 */
	{0x03, 0x0400},		/* vres = 1024 */
	{0x20, 0x1100},
	{0x06, 0x0010},
	{0x2b, 0x0024},
	{0x2e, 0x0024},
	{0x35, 0x0024},
	{0x2d, 0x0020},
	{0x2c, 0x0020},
	{0x09, 0x0ad4},
	{0x35, 0x0057},
};

static const struct i2c_reg_u16 mt9m111_init[] = {
	{0xf0, 0x0000}, {0x0d, 0x0021}, {0x0d, 0x0008},
	{0xf0, 0x0001}, {0x3a, 0x4300}, {0x9b, 0x4300},
	{0x06, 0x708e}, {0xf0, 0x0002}, {0x2e, 0x0a1e},
	{0xf0, 0x0000},
};

static const struct i2c_reg_u16 mt9m112_init[] = {
	{0xf0, 0x0000}, {0x0d, 0x0021}, {0x0d, 0x0008},
	{0xf0, 0x0001}, {0x3a, 0x4300}, {0x9b, 0x4300},
	{0x06, 0x708e}, {0xf0, 0x0002}, {0x2e, 0x0a1e},
	{0xf0, 0x0000},
};

static const struct i2c_reg_u8 hv7131r_init[] = {
	{0x02, 0x08}, {0x02, 0x00}, {0x01, 0x08},
	{0x02, 0x00}, {0x20, 0x00}, {0x21, 0xd0},
	{0x22, 0x00}, {0x23, 0x09}, {0x01, 0x08},
	{0x01, 0x08}, {0x01, 0x08}, {0x25, 0x07},
	{0x26, 0xc3}, {0x27, 0x50}, {0x30, 0x62},
	{0x31, 0x10}, {0x32, 0x06}, {0x33, 0x10},
	{0x20, 0x00}, {0x21, 0xd0}, {0x22, 0x00},
	{0x23, 0x09}, {0x01, 0x08},
};

static void reg_r(struct gspca_dev *gspca_dev, u16 reg, u16 length)
{
	struct usb_device *dev = gspca_dev->dev;
	int result;

	if (gspca_dev->usb_err < 0)
		return;
	result = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
			0x00,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_INTERFACE,
			reg,
			0x00,
			gspca_dev->usb_buf,
			length,
			500);
	if (unlikely(result < 0 || result != length)) {
		pr_err("Read register %02x failed %d\n", reg, result);
		gspca_dev->usb_err = result;
	}
}

static void reg_w(struct gspca_dev *gspca_dev, u16 reg,
		 const u8 *buffer, int length)
{
	struct usb_device *dev = gspca_dev->dev;
	int result;

	if (gspca_dev->usb_err < 0)
		return;
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
		pr_err("Write register %02x failed %d\n", reg, result);
		gspca_dev->usb_err = result;
	}
}

static void reg_w1(struct gspca_dev *gspca_dev, u16 reg, const u8 value)
{
	reg_w(gspca_dev, reg, &value, 1);
}

static void i2c_w(struct gspca_dev *gspca_dev, const u8 *buffer)
{
	int i;

	reg_w(gspca_dev, 0x10c0, buffer, 8);
	for (i = 0; i < 5; i++) {
		reg_r(gspca_dev, 0x10c0, 1);
		if (gspca_dev->usb_err < 0)
			return;
		if (gspca_dev->usb_buf[0] & 0x04) {
			if (gspca_dev->usb_buf[0] & 0x08) {
				pr_err("i2c_w error\n");
				gspca_dev->usb_err = -EIO;
			}
			return;
		}
		msleep(10);
	}
	pr_err("i2c_w reg %02x no response\n", buffer[2]);
/*	gspca_dev->usb_err = -EIO;	fixme: may occur */
}

static void i2c_w1(struct gspca_dev *gspca_dev, u8 reg, u8 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 row[8];

	/*
	 * from the point of view of the bridge, the length
	 * includes the address
	 */
	row[0] = sd->i2c_intf | (2 << 4);
	row[1] = sd->i2c_addr;
	row[2] = reg;
	row[3] = val;
	row[4] = 0x00;
	row[5] = 0x00;
	row[6] = 0x00;
	row[7] = 0x10;

	i2c_w(gspca_dev, row);
}

static void i2c_w1_buf(struct gspca_dev *gspca_dev,
			const struct i2c_reg_u8 *buf, int sz)
{
	while (--sz >= 0) {
		i2c_w1(gspca_dev, buf->reg, buf->val);
		buf++;
	}
}

static void i2c_w2(struct gspca_dev *gspca_dev, u8 reg, u16 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 row[8];

	/*
	 * from the point of view of the bridge, the length
	 * includes the address
	 */
	row[0] = sd->i2c_intf | (3 << 4);
	row[1] = sd->i2c_addr;
	row[2] = reg;
	row[3] = val >> 8;
	row[4] = val;
	row[5] = 0x00;
	row[6] = 0x00;
	row[7] = 0x10;

	i2c_w(gspca_dev, row);
}

static void i2c_w2_buf(struct gspca_dev *gspca_dev,
			const struct i2c_reg_u16 *buf, int sz)
{
	while (--sz >= 0) {
		i2c_w2(gspca_dev, buf->reg, buf->val);
		buf++;
	}
}

static void i2c_r1(struct gspca_dev *gspca_dev, u8 reg, u8 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 row[8];

	row[0] = sd->i2c_intf | (1 << 4);
	row[1] = sd->i2c_addr;
	row[2] = reg;
	row[3] = 0;
	row[4] = 0;
	row[5] = 0;
	row[6] = 0;
	row[7] = 0x10;
	i2c_w(gspca_dev, row);
	row[0] = sd->i2c_intf | (1 << 4) | 0x02;
	row[2] = 0;
	i2c_w(gspca_dev, row);
	reg_r(gspca_dev, 0x10c2, 5);
	*val = gspca_dev->usb_buf[4];
}

static void i2c_r2(struct gspca_dev *gspca_dev, u8 reg, u16 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 row[8];

	row[0] = sd->i2c_intf | (1 << 4);
	row[1] = sd->i2c_addr;
	row[2] = reg;
	row[3] = 0;
	row[4] = 0;
	row[5] = 0;
	row[6] = 0;
	row[7] = 0x10;
	i2c_w(gspca_dev, row);
	row[0] = sd->i2c_intf | (2 << 4) | 0x02;
	row[2] = 0;
	i2c_w(gspca_dev, row);
	reg_r(gspca_dev, 0x10c2, 5);
	*val = (gspca_dev->usb_buf[3] << 8) | gspca_dev->usb_buf[4];
}

static void ov9650_init_sensor(struct gspca_dev *gspca_dev)
{
	u16 id;
	struct sd *sd = (struct sd *) gspca_dev;

	i2c_r2(gspca_dev, 0x1c, &id);
	if (gspca_dev->usb_err < 0)
		return;

	if (id != 0x7fa2) {
		pr_err("sensor id for ov9650 doesn't match (0x%04x)\n", id);
		gspca_dev->usb_err = -ENODEV;
		return;
	}

	i2c_w1(gspca_dev, 0x12, 0x80);		/* sensor reset */
	msleep(200);
	i2c_w1_buf(gspca_dev, ov9650_init, ARRAY_SIZE(ov9650_init));
	if (gspca_dev->usb_err < 0)
		pr_err("OV9650 sensor initialization failed\n");
	sd->hstart = 1;
	sd->vstart = 7;
}

static void ov9655_init_sensor(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	i2c_w1(gspca_dev, 0x12, 0x80);		/* sensor reset */
	msleep(200);
	i2c_w1_buf(gspca_dev, ov9655_init, ARRAY_SIZE(ov9655_init));
	if (gspca_dev->usb_err < 0)
		pr_err("OV9655 sensor initialization failed\n");

	sd->hstart = 1;
	sd->vstart = 2;
}

static void soi968_init_sensor(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	i2c_w1(gspca_dev, 0x12, 0x80);		/* sensor reset */
	msleep(200);
	i2c_w1_buf(gspca_dev, soi968_init, ARRAY_SIZE(soi968_init));
	if (gspca_dev->usb_err < 0)
		pr_err("SOI968 sensor initialization failed\n");

	sd->hstart = 60;
	sd->vstart = 11;
}

static void ov7660_init_sensor(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	i2c_w1(gspca_dev, 0x12, 0x80);		/* sensor reset */
	msleep(200);
	i2c_w1_buf(gspca_dev, ov7660_init, ARRAY_SIZE(ov7660_init));
	if (gspca_dev->usb_err < 0)
		pr_err("OV7660 sensor initialization failed\n");
	sd->hstart = 3;
	sd->vstart = 3;
}

static void ov7670_init_sensor(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	i2c_w1(gspca_dev, 0x12, 0x80);		/* sensor reset */
	msleep(200);
	i2c_w1_buf(gspca_dev, ov7670_init, ARRAY_SIZE(ov7670_init));
	if (gspca_dev->usb_err < 0)
		pr_err("OV7670 sensor initialization failed\n");

	sd->hstart = 0;
	sd->vstart = 1;
}

static void mt9v_init_sensor(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u16 value;

	sd->i2c_addr = 0x5d;
	i2c_r2(gspca_dev, 0xff, &value);
	if (gspca_dev->usb_err >= 0
	 && value == 0x8243) {
		i2c_w2_buf(gspca_dev, mt9v011_init, ARRAY_SIZE(mt9v011_init));
		if (gspca_dev->usb_err < 0) {
			pr_err("MT9V011 sensor initialization failed\n");
			return;
		}
		sd->hstart = 2;
		sd->vstart = 2;
		sd->sensor = SENSOR_MT9V011;
		pr_info("MT9V011 sensor detected\n");
		return;
	}

	gspca_dev->usb_err = 0;
	sd->i2c_addr = 0x5c;
	i2c_w2(gspca_dev, 0x01, 0x0004);
	i2c_r2(gspca_dev, 0xff, &value);
	if (gspca_dev->usb_err >= 0
	 && value == 0x823a) {
		i2c_w2_buf(gspca_dev, mt9v111_init, ARRAY_SIZE(mt9v111_init));
		if (gspca_dev->usb_err < 0) {
			pr_err("MT9V111 sensor initialization failed\n");
			return;
		}
		sd->hstart = 2;
		sd->vstart = 2;
		sd->sensor = SENSOR_MT9V111;
		pr_info("MT9V111 sensor detected\n");
		return;
	}

	gspca_dev->usb_err = 0;
	sd->i2c_addr = 0x5d;
	i2c_w2(gspca_dev, 0xf0, 0x0000);
	if (gspca_dev->usb_err < 0) {
		gspca_dev->usb_err = 0;
		sd->i2c_addr = 0x48;
		i2c_w2(gspca_dev, 0xf0, 0x0000);
	}
	i2c_r2(gspca_dev, 0x00, &value);
	if (gspca_dev->usb_err >= 0
	 && value == 0x1229) {
		i2c_w2_buf(gspca_dev, mt9v112_init, ARRAY_SIZE(mt9v112_init));
		if (gspca_dev->usb_err < 0) {
			pr_err("MT9V112 sensor initialization failed\n");
			return;
		}
		sd->hstart = 6;
		sd->vstart = 2;
		sd->sensor = SENSOR_MT9V112;
		pr_info("MT9V112 sensor detected\n");
		return;
	}

	gspca_dev->usb_err = -ENODEV;
}

static void mt9m112_init_sensor(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	i2c_w2_buf(gspca_dev, mt9m112_init, ARRAY_SIZE(mt9m112_init));
	if (gspca_dev->usb_err < 0)
		pr_err("MT9M112 sensor initialization failed\n");

	sd->hstart = 0;
	sd->vstart = 2;
}

static void mt9m111_init_sensor(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	i2c_w2_buf(gspca_dev, mt9m111_init, ARRAY_SIZE(mt9m111_init));
	if (gspca_dev->usb_err < 0)
		pr_err("MT9M111 sensor initialization failed\n");

	sd->hstart = 0;
	sd->vstart = 2;
}

static void mt9m001_init_sensor(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u16 id;

	i2c_r2(gspca_dev, 0x00, &id);
	if (gspca_dev->usb_err < 0)
		return;

	/* must be 0x8411 or 0x8421 for colour sensor and 8431 for bw */
	switch (id) {
	case 0x8411:
	case 0x8421:
		pr_info("MT9M001 color sensor detected\n");
		break;
	case 0x8431:
		pr_info("MT9M001 mono sensor detected\n");
		break;
	default:
		pr_err("No MT9M001 chip detected, ID = %x\n\n", id);
		gspca_dev->usb_err = -ENODEV;
		return;
	}

	i2c_w2_buf(gspca_dev, mt9m001_init, ARRAY_SIZE(mt9m001_init));
	if (gspca_dev->usb_err < 0)
		pr_err("MT9M001 sensor initialization failed\n");

	sd->hstart = 1;
	sd->vstart = 1;
}

static void hv7131r_init_sensor(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	i2c_w1_buf(gspca_dev, hv7131r_init, ARRAY_SIZE(hv7131r_init));
	if (gspca_dev->usb_err < 0)
		pr_err("HV7131R Sensor initialization failed\n");

	sd->hstart = 0;
	sd->vstart = 1;
}

static void set_cmatrix(struct gspca_dev *gspca_dev,
		s32 brightness, s32 contrast, s32 satur, s32 hue)
{
	s32 hue_coord, hue_index = 180 + hue;
	u8 cmatrix[21];

	memset(cmatrix, 0, sizeof cmatrix);
	cmatrix[2] = (contrast * 0x25 / 0x100) + 0x26;
	cmatrix[0] = 0x13 + (cmatrix[2] - 0x26) * 0x13 / 0x25;
	cmatrix[4] = 0x07 + (cmatrix[2] - 0x26) * 0x07 / 0x25;
	cmatrix[18] = brightness - 0x80;

	hue_coord = (hsv_red_x[hue_index] * satur) >> 8;
	cmatrix[6] = hue_coord;
	cmatrix[7] = (hue_coord >> 8) & 0x0f;

	hue_coord = (hsv_red_y[hue_index] * satur) >> 8;
	cmatrix[8] = hue_coord;
	cmatrix[9] = (hue_coord >> 8) & 0x0f;

	hue_coord = (hsv_green_x[hue_index] * satur) >> 8;
	cmatrix[10] = hue_coord;
	cmatrix[11] = (hue_coord >> 8) & 0x0f;

	hue_coord = (hsv_green_y[hue_index] * satur) >> 8;
	cmatrix[12] = hue_coord;
	cmatrix[13] = (hue_coord >> 8) & 0x0f;

	hue_coord = (hsv_blue_x[hue_index] * satur) >> 8;
	cmatrix[14] = hue_coord;
	cmatrix[15] = (hue_coord >> 8) & 0x0f;

	hue_coord = (hsv_blue_y[hue_index] * satur) >> 8;
	cmatrix[16] = hue_coord;
	cmatrix[17] = (hue_coord >> 8) & 0x0f;

	reg_w(gspca_dev, 0x10e1, cmatrix, 21);
}

static void set_gamma(struct gspca_dev *gspca_dev, s32 val)
{
	u8 gamma[17];
	u8 gval = val * 0xb8 / 0x100;

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

	reg_w(gspca_dev, 0x1190, gamma, 17);
}

static void set_redblue(struct gspca_dev *gspca_dev, s32 blue, s32 red)
{
	reg_w1(gspca_dev, 0x118c, red);
	reg_w1(gspca_dev, 0x118f, blue);
}

static void set_hvflip(struct gspca_dev *gspca_dev, s32 hflip, s32 vflip)
{
	u8 value, tslb;
	u16 value2;
	struct sd *sd = (struct sd *) gspca_dev;

	if ((sd->flags & FLIP_DETECT) && dmi_check_system(flip_dmi_table)) {
		hflip = !hflip;
		vflip = !vflip;
	}

	switch (sd->sensor) {
	case SENSOR_OV7660:
		value = 0x01;
		if (hflip)
			value |= 0x20;
		if (vflip) {
			value |= 0x10;
			sd->vstart = 2;
		} else {
			sd->vstart = 3;
		}
		reg_w1(gspca_dev, 0x1182, sd->vstart);
		i2c_w1(gspca_dev, 0x1e, value);
		break;
	case SENSOR_OV9650:
		i2c_r1(gspca_dev, 0x1e, &value);
		value &= ~0x30;
		tslb = 0x01;
		if (hflip)
			value |= 0x20;
		if (vflip) {
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
		if (hflip)
			value2 |= 0x8080;
		if (vflip)
			value2 |= 0x4020;
		i2c_w2(gspca_dev, 0x20, value2);
		break;
	case SENSOR_MT9M112:
	case SENSOR_MT9M111:
	case SENSOR_MT9V112:
		i2c_r2(gspca_dev, 0x20, &value2);
		value2 &= ~0x0003;
		if (hflip)
			value2 |= 0x0002;
		if (vflip)
			value2 |= 0x0001;
		i2c_w2(gspca_dev, 0x20, value2);
		break;
	case SENSOR_HV7131R:
		i2c_r1(gspca_dev, 0x01, &value);
		value &= ~0x03;
		if (vflip)
			value |= 0x01;
		if (hflip)
			value |= 0x02;
		i2c_w1(gspca_dev, 0x01, value);
		break;
	}
}

static void set_exposure(struct gspca_dev *gspca_dev, s32 expo)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 exp[8] = {sd->i2c_intf, sd->i2c_addr,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x10};
	int expo2;

	if (gspca_dev->streaming)
		exp[7] = 0x1e;

	switch (sd->sensor) {
	case SENSOR_OV7660:
	case SENSOR_OV7670:
	case SENSOR_OV9655:
	case SENSOR_OV9650:
		if (expo > 547)
			expo2 = 547;
		else
			expo2 = expo;
		exp[0] |= (2 << 4);
		exp[2] = 0x10;			/* AECH */
		exp[3] = expo2 >> 2;
		exp[7] = 0x10;
		i2c_w(gspca_dev, exp);
		exp[2] = 0x04;			/* COM1 */
		exp[3] = expo2 & 0x0003;
		exp[7] = 0x10;
		i2c_w(gspca_dev, exp);
		expo -= expo2;
		exp[7] = 0x1e;
		exp[0] |= (3 << 4);
		exp[2] = 0x2d;			/* ADVFL & ADVFH */
		exp[3] = expo;
		exp[4] = expo >> 8;
		break;
	case SENSOR_MT9M001:
	case SENSOR_MT9V112:
	case SENSOR_MT9V011:
		exp[0] |= (3 << 4);
		exp[2] = 0x09;
		exp[3] = expo >> 8;
		exp[4] = expo;
		break;
	case SENSOR_HV7131R:
		exp[0] |= (4 << 4);
		exp[2] = 0x25;
		exp[3] = expo >> 5;
		exp[4] = expo << 3;
		exp[5] = 0;
		break;
	default:
		return;
	}
	i2c_w(gspca_dev, exp);
}

static void set_gain(struct gspca_dev *gspca_dev, s32 g)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 gain[8] = {sd->i2c_intf, sd->i2c_addr,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x10};

	if (gspca_dev->streaming)
		gain[7] = 0x15;		/* or 1d ? */

	switch (sd->sensor) {
	case SENSOR_OV7660:
	case SENSOR_OV7670:
	case SENSOR_SOI968:
	case SENSOR_OV9655:
	case SENSOR_OV9650:
		gain[0] |= (2 << 4);
		gain[3] = ov_gain[g];
		break;
	case SENSOR_MT9V011:
		gain[0] |= (3 << 4);
		gain[2] = 0x35;
		gain[3] = micron1_gain[g] >> 8;
		gain[4] = micron1_gain[g];
		break;
	case SENSOR_MT9V112:
		gain[0] |= (3 << 4);
		gain[2] = 0x2f;
		gain[3] = micron1_gain[g] >> 8;
		gain[4] = micron1_gain[g];
		break;
	case SENSOR_MT9M001:
		gain[0] |= (3 << 4);
		gain[2] = 0x2f;
		gain[3] = micron2_gain[g] >> 8;
		gain[4] = micron2_gain[g];
		break;
	case SENSOR_HV7131R:
		gain[0] |= (2 << 4);
		gain[2] = 0x30;
		gain[3] = hv7131r_gain[g];
		break;
	default:
		return;
	}
	i2c_w(gspca_dev, gain);
}

static void set_quality(struct gspca_dev *gspca_dev, s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	jpeg_set_qual(sd->jpeg_hdr, val);
	reg_w1(gspca_dev, 0x1061, 0x01);	/* stop transfer */
	reg_w1(gspca_dev, 0x10e0, sd->fmt | 0x20); /* write QTAB */
	reg_w(gspca_dev, 0x1100, &sd->jpeg_hdr[JPEG_QT0_OFFSET], 64);
	reg_w(gspca_dev, 0x1140, &sd->jpeg_hdr[JPEG_QT1_OFFSET], 64);
	reg_w1(gspca_dev, 0x1061, 0x03);	/* restart transfer */
	reg_w1(gspca_dev, 0x10e0, sd->fmt);
	sd->fmt ^= 0x0c;			/* invert QTAB use + write */
	reg_w1(gspca_dev, 0x10e0, sd->fmt);
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
		reg_r(gspca_dev, reg->reg, 1);
		reg->val = gspca_dev->usb_buf[0];
		return gspca_dev->usb_err;
	case V4L2_CHIP_MATCH_I2C_ADDR:
		if (reg->match.addr != sd->i2c_addr)
			return -EINVAL;
		if (sd->sensor >= SENSOR_MT9V011 &&
		    sd->sensor <= SENSOR_MT9M112) {
			i2c_r2(gspca_dev, reg->reg, (u16 *) &reg->val);
		} else {
			i2c_r1(gspca_dev, reg->reg, (u8 *) &reg->val);
		}
		return gspca_dev->usb_err;
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
		reg_w1(gspca_dev, reg->reg, reg->val);
		return gspca_dev->usb_err;
	case V4L2_CHIP_MATCH_I2C_ADDR:
		if (reg->match.addr != sd->i2c_addr)
			return -EINVAL;
		if (sd->sensor >= SENSOR_MT9V011 &&
		    sd->sensor <= SENSOR_MT9M112) {
			i2c_w2(gspca_dev, reg->reg, reg->val);
		} else {
			i2c_w1(gspca_dev, reg->reg, reg->val);
		}
		return gspca_dev->usb_err;
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
	cam->needs_full_bandwidth = 1;

	sd->sensor = id->driver_info >> 8;
	sd->i2c_addr = id->driver_info;
	sd->flags = id->driver_info >> 16;
	sd->i2c_intf = 0x80;			/* i2c 100 Kb/s */

	switch (sd->sensor) {
	case SENSOR_MT9M112:
	case SENSOR_MT9M111:
	case SENSOR_OV9650:
	case SENSOR_SOI968:
		cam->cam_mode = sxga_mode;
		cam->nmodes = ARRAY_SIZE(sxga_mode);
		break;
	case SENSOR_MT9M001:
		cam->cam_mode = mono_mode;
		cam->nmodes = ARRAY_SIZE(mono_mode);
		break;
	case SENSOR_HV7131R:
		sd->i2c_intf = 0x81;			/* i2c 400 Kb/s */
		/* fall thru */
	default:
		cam->cam_mode = vga_mode;
		cam->nmodes = ARRAY_SIZE(vga_mode);
		break;
	}

	sd->old_step = 0;
	sd->older_step = 0;
	sd->exposure_step = 16;

	INIT_WORK(&sd->work, qual_upd);

	return 0;
}

static int sd_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gspca_dev *gspca_dev =
		container_of(ctrl->handler, struct gspca_dev, ctrl_handler);
	struct sd *sd = (struct sd *)gspca_dev;

	gspca_dev->usb_err = 0;

	if (!gspca_dev->streaming)
		return 0;

	switch (ctrl->id) {
	/* color control cluster */
	case V4L2_CID_BRIGHTNESS:
		set_cmatrix(gspca_dev, sd->brightness->val,
			sd->contrast->val, sd->saturation->val, sd->hue->val);
		break;
	case V4L2_CID_GAMMA:
		set_gamma(gspca_dev, ctrl->val);
		break;
	/* blue/red balance cluster */
	case V4L2_CID_BLUE_BALANCE:
		set_redblue(gspca_dev, sd->blue->val, sd->red->val);
		break;
	/* h/vflip cluster */
	case V4L2_CID_HFLIP:
		set_hvflip(gspca_dev, sd->hflip->val, sd->vflip->val);
		break;
	/* standalone exposure control */
	case V4L2_CID_EXPOSURE:
		set_exposure(gspca_dev, ctrl->val);
		break;
	/* standalone gain control */
	case V4L2_CID_GAIN:
		set_gain(gspca_dev, ctrl->val);
		break;
	/* autogain + exposure or gain control cluster */
	case V4L2_CID_AUTOGAIN:
		if (sd->sensor == SENSOR_SOI968)
			set_gain(gspca_dev, sd->gain->val);
		else
			set_exposure(gspca_dev, sd->exposure->val);
		break;
	case V4L2_CID_JPEG_COMPRESSION_QUALITY:
		set_quality(gspca_dev, ctrl->val);
		break;
	}
	return gspca_dev->usb_err;
}

static const struct v4l2_ctrl_ops sd_ctrl_ops = {
	.s_ctrl = sd_s_ctrl,
};

static int sd_init_controls(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct v4l2_ctrl_handler *hdl = &gspca_dev->ctrl_handler;

	gspca_dev->vdev.ctrl_handler = hdl;
	v4l2_ctrl_handler_init(hdl, 13);

	sd->brightness = v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
			V4L2_CID_BRIGHTNESS, 0, 255, 1, 127);
	sd->contrast = v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
			V4L2_CID_CONTRAST, 0, 255, 1, 127);
	sd->saturation = v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
			V4L2_CID_SATURATION, 0, 255, 1, 127);
	sd->hue = v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
			V4L2_CID_HUE, -180, 180, 1, 0);
	v4l2_ctrl_cluster(4, &sd->brightness);

	sd->gamma = v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
			V4L2_CID_GAMMA, 0, 255, 1, 0x10);

	sd->blue = v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
			V4L2_CID_BLUE_BALANCE, 0, 127, 1, 0x28);
	sd->red = v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
			V4L2_CID_RED_BALANCE, 0, 127, 1, 0x28);
	v4l2_ctrl_cluster(2, &sd->blue);

	if (sd->sensor != SENSOR_OV9655 && sd->sensor != SENSOR_SOI968 &&
	    sd->sensor != SENSOR_OV7670 && sd->sensor != SENSOR_MT9M001 &&
	    sd->sensor != SENSOR_MT9VPRB) {
		sd->hflip = v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
			V4L2_CID_HFLIP, 0, 1, 1, 0);
		sd->vflip = v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
			V4L2_CID_VFLIP, 0, 1, 1, 0);
		v4l2_ctrl_cluster(2, &sd->hflip);
	}

	if (sd->sensor != SENSOR_SOI968 && sd->sensor != SENSOR_MT9VPRB &&
	    sd->sensor != SENSOR_MT9M112 && sd->sensor != SENSOR_MT9M111 &&
	    sd->sensor != SENSOR_MT9V111)
		sd->exposure = v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
			V4L2_CID_EXPOSURE, 0, 0x1780, 1, 0x33);

	if (sd->sensor != SENSOR_MT9VPRB && sd->sensor != SENSOR_MT9M112 &&
	    sd->sensor != SENSOR_MT9M111 && sd->sensor != SENSOR_MT9V111) {
		sd->gain = v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
			V4L2_CID_GAIN, 0, 28, 1, 0);
		sd->autogain = v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
			V4L2_CID_AUTOGAIN, 0, 1, 1, 1);
		if (sd->sensor == SENSOR_SOI968)
			/* this sensor doesn't have the exposure control and
			   autogain is clustered with gain instead. This works
			   because sd->exposure == NULL. */
			v4l2_ctrl_auto_cluster(3, &sd->autogain, 0, false);
		else
			/* Otherwise autogain is clustered with exposure. */
			v4l2_ctrl_auto_cluster(2, &sd->autogain, 0, false);
	}

	sd->jpegqual = v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
			V4L2_CID_JPEG_COMPRESSION_QUALITY, 50, 90, 1, 80);
	if (hdl->error) {
		pr_err("Could not initialize controls\n");
		return hdl->error;
	}
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
		reg_w(gspca_dev, bridge_init[i][0], &value, 1);
		if (gspca_dev->usb_err < 0) {
			pr_err("Device initialization failed\n");
			return gspca_dev->usb_err;
		}
	}

	if (sd->flags & LED_REVERSE)
		reg_w1(gspca_dev, 0x1006, 0x00);
	else
		reg_w1(gspca_dev, 0x1006, 0x20);

	reg_w(gspca_dev, 0x10c0, i2c_init, 9);
	if (gspca_dev->usb_err < 0) {
		pr_err("Device initialization failed\n");
		return gspca_dev->usb_err;
	}

	switch (sd->sensor) {
	case SENSOR_OV9650:
		ov9650_init_sensor(gspca_dev);
		if (gspca_dev->usb_err < 0)
			break;
		pr_info("OV9650 sensor detected\n");
		break;
	case SENSOR_OV9655:
		ov9655_init_sensor(gspca_dev);
		if (gspca_dev->usb_err < 0)
			break;
		pr_info("OV9655 sensor detected\n");
		break;
	case SENSOR_SOI968:
		soi968_init_sensor(gspca_dev);
		if (gspca_dev->usb_err < 0)
			break;
		pr_info("SOI968 sensor detected\n");
		break;
	case SENSOR_OV7660:
		ov7660_init_sensor(gspca_dev);
		if (gspca_dev->usb_err < 0)
			break;
		pr_info("OV7660 sensor detected\n");
		break;
	case SENSOR_OV7670:
		ov7670_init_sensor(gspca_dev);
		if (gspca_dev->usb_err < 0)
			break;
		pr_info("OV7670 sensor detected\n");
		break;
	case SENSOR_MT9VPRB:
		mt9v_init_sensor(gspca_dev);
		if (gspca_dev->usb_err < 0)
			break;
		pr_info("MT9VPRB sensor detected\n");
		break;
	case SENSOR_MT9M111:
		mt9m111_init_sensor(gspca_dev);
		if (gspca_dev->usb_err < 0)
			break;
		pr_info("MT9M111 sensor detected\n");
		break;
	case SENSOR_MT9M112:
		mt9m112_init_sensor(gspca_dev);
		if (gspca_dev->usb_err < 0)
			break;
		pr_info("MT9M112 sensor detected\n");
		break;
	case SENSOR_MT9M001:
		mt9m001_init_sensor(gspca_dev);
		if (gspca_dev->usb_err < 0)
			break;
		break;
	case SENSOR_HV7131R:
		hv7131r_init_sensor(gspca_dev);
		if (gspca_dev->usb_err < 0)
			break;
		pr_info("HV7131R sensor detected\n");
		break;
	default:
		pr_err("Unsupported sensor\n");
		gspca_dev->usb_err = -ENODEV;
	}
	return gspca_dev->usb_err;
}

static void configure_sensor_output(struct gspca_dev *gspca_dev, int mode)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 value;

	switch (sd->sensor) {
	case SENSOR_SOI968:
		if (mode & MODE_SXGA) {
			i2c_w1(gspca_dev, 0x17, 0x1d);
			i2c_w1(gspca_dev, 0x18, 0xbd);
			i2c_w1(gspca_dev, 0x19, 0x01);
			i2c_w1(gspca_dev, 0x1a, 0x81);
			i2c_w1(gspca_dev, 0x12, 0x00);
			sd->hstart = 140;
			sd->vstart = 19;
		} else {
			i2c_w1(gspca_dev, 0x17, 0x13);
			i2c_w1(gspca_dev, 0x18, 0x63);
			i2c_w1(gspca_dev, 0x19, 0x01);
			i2c_w1(gspca_dev, 0x1a, 0x79);
			i2c_w1(gspca_dev, 0x12, 0x40);
			sd->hstart = 60;
			sd->vstart = 11;
		}
		break;
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
	case SENSOR_MT9M112:
	case SENSOR_MT9M111:
		if (mode & MODE_SXGA) {
			i2c_w2(gspca_dev, 0xf0, 0x0002);
			i2c_w2(gspca_dev, 0xc8, 0x970b);
			i2c_w2(gspca_dev, 0xf0, 0x0000);
		} else {
			i2c_w2(gspca_dev, 0xf0, 0x0002);
			i2c_w2(gspca_dev, 0xc8, 0x8000);
			i2c_w2(gspca_dev, 0xf0, 0x0000);
		}
		break;
	}
}

static int sd_isoc_init(struct gspca_dev *gspca_dev)
{
	struct usb_interface *intf;
	u32 flags = gspca_dev->cam.cam_mode[(int)gspca_dev->curr_mode].priv;

	/*
	 * When using the SN9C20X_I420 fmt the sn9c20x needs more bandwidth
	 * than our regular bandwidth calculations reserve, so we force the
	 * use of a specific altsetting when using the SN9C20X_I420 fmt.
	 */
	if (!(flags & (MODE_RAW | MODE_JPEG))) {
		intf = usb_ifnum_to_if(gspca_dev->dev, gspca_dev->iface);

		if (intf->num_altsetting != 9) {
			pr_warn("sn9c20x camera with unknown number of alt "
				"settings (%d), please report!\n",
				intf->num_altsetting);
			gspca_dev->alt = intf->num_altsetting;
			return 0;
		}

		switch (gspca_dev->width) {
		case 160: /* 160x120 */
			gspca_dev->alt = 2;
			break;
		case 320: /* 320x240 */
			gspca_dev->alt = 6;
			break;
		default:  /* >= 640x480 */
			gspca_dev->alt = 9;
			break;
		}
	}

	return 0;
}

#define HW_WIN(mode, hstart, vstart) \
((const u8 []){hstart, 0, vstart, 0, \
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

	jpeg_define(sd->jpeg_hdr, height, width,
			0x21);
	jpeg_set_qual(sd->jpeg_hdr, v4l2_ctrl_g_ctrl(sd->jpegqual));

	if (mode & MODE_RAW)
		fmt = 0x2d;
	else if (mode & MODE_JPEG)
		fmt = 0x24;
	else
		fmt = 0x2f;	/* YUV 420 */
	sd->fmt = fmt;

	switch (mode & SCALE_MASK) {
	case SCALE_1280x1024:
		scale = 0xc0;
		pr_info("Set 1280x1024\n");
		break;
	case SCALE_640x480:
		scale = 0x80;
		pr_info("Set 640x480\n");
		break;
	case SCALE_320x240:
		scale = 0x90;
		pr_info("Set 320x240\n");
		break;
	case SCALE_160x120:
		scale = 0xa0;
		pr_info("Set 160x120\n");
		break;
	}

	configure_sensor_output(gspca_dev, mode);
	reg_w(gspca_dev, 0x1100, &sd->jpeg_hdr[JPEG_QT0_OFFSET], 64);
	reg_w(gspca_dev, 0x1140, &sd->jpeg_hdr[JPEG_QT1_OFFSET], 64);
	reg_w(gspca_dev, 0x10fb, CLR_WIN(width, height), 5);
	reg_w(gspca_dev, 0x1180, HW_WIN(mode, sd->hstart, sd->vstart), 6);
	reg_w1(gspca_dev, 0x1189, scale);
	reg_w1(gspca_dev, 0x10e0, fmt);

	set_cmatrix(gspca_dev, v4l2_ctrl_g_ctrl(sd->brightness),
			v4l2_ctrl_g_ctrl(sd->contrast),
			v4l2_ctrl_g_ctrl(sd->saturation),
			v4l2_ctrl_g_ctrl(sd->hue));
	set_gamma(gspca_dev, v4l2_ctrl_g_ctrl(sd->gamma));
	set_redblue(gspca_dev, v4l2_ctrl_g_ctrl(sd->blue),
			v4l2_ctrl_g_ctrl(sd->red));
	set_gain(gspca_dev, v4l2_ctrl_g_ctrl(sd->gain));
	set_exposure(gspca_dev, v4l2_ctrl_g_ctrl(sd->exposure));
	set_hvflip(gspca_dev, v4l2_ctrl_g_ctrl(sd->hflip),
			v4l2_ctrl_g_ctrl(sd->vflip));

	reg_w1(gspca_dev, 0x1007, 0x20);
	reg_w1(gspca_dev, 0x1061, 0x03);

	/* if JPEG, prepare the compression quality update */
	if (mode & MODE_JPEG) {
		sd->pktsz = sd->npkt = 0;
		sd->nchg = 0;
		sd->work_thread =
			create_singlethread_workqueue(KBUILD_MODNAME);
	}

	return gspca_dev->usb_err;
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	reg_w1(gspca_dev, 0x1007, 0x00);
	reg_w1(gspca_dev, 0x1061, 0x01);
}

/* called on streamoff with alt==0 and on disconnect */
/* the usb_lock is held at entry - restore on exit */
static void sd_stop0(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (sd->work_thread != NULL) {
		mutex_unlock(&gspca_dev->usb_lock);
		destroy_workqueue(sd->work_thread);
		mutex_lock(&gspca_dev->usb_lock);
		sd->work_thread = NULL;
	}
}

static void do_autoexposure(struct gspca_dev *gspca_dev, u16 avg_lum)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 cur_exp = v4l2_ctrl_g_ctrl(sd->exposure);
	s32 max = sd->exposure->maximum - sd->exposure_step;
	s32 min = sd->exposure->minimum + sd->exposure_step;
	s16 new_exp;

	/*
	 * some hardcoded values are present
	 * like those for maximal/minimal exposure
	 * and exposure steps
	 */
	if (avg_lum < MIN_AVG_LUM) {
		if (cur_exp > max)
			return;

		new_exp = cur_exp + sd->exposure_step;
		if (new_exp > max)
			new_exp = max;
		if (new_exp < min)
			new_exp = min;
		v4l2_ctrl_s_ctrl(sd->exposure, new_exp);

		sd->older_step = sd->old_step;
		sd->old_step = 1;

		if (sd->old_step ^ sd->older_step)
			sd->exposure_step /= 2;
		else
			sd->exposure_step += 2;
	}
	if (avg_lum > MAX_AVG_LUM) {
		if (cur_exp < min)
			return;
		new_exp = cur_exp - sd->exposure_step;
		if (new_exp > max)
			new_exp = max;
		if (new_exp < min)
			new_exp = min;
		v4l2_ctrl_s_ctrl(sd->exposure, new_exp);
		sd->older_step = sd->old_step;
		sd->old_step = 0;

		if (sd->old_step ^ sd->older_step)
			sd->exposure_step /= 2;
		else
			sd->exposure_step += 2;
	}
}

static void do_autogain(struct gspca_dev *gspca_dev, u16 avg_lum)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 cur_gain = v4l2_ctrl_g_ctrl(sd->gain);

	if (avg_lum < MIN_AVG_LUM && cur_gain < sd->gain->maximum)
		v4l2_ctrl_s_ctrl(sd->gain, cur_gain + 1);
	if (avg_lum > MAX_AVG_LUM && cur_gain > sd->gain->minimum)
		v4l2_ctrl_s_ctrl(sd->gain, cur_gain - 1);
}

static void sd_dqcallback(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int avg_lum;

	if (!v4l2_ctrl_g_ctrl(sd->autogain))
		return;

	avg_lum = atomic_read(&sd->avg_lum);
	if (sd->sensor == SENSOR_SOI968)
		do_autogain(gspca_dev, avg_lum);
	else
		do_autoexposure(gspca_dev, avg_lum);
}

/* JPEG quality update */
/* This function is executed from a work queue. */
static void qual_upd(struct work_struct *work)
{
	struct sd *sd = container_of(work, struct sd, work);
	struct gspca_dev *gspca_dev = &sd->gspca_dev;
	s32 qual = v4l2_ctrl_g_ctrl(sd->jpegqual);

	mutex_lock(&gspca_dev->usb_lock);
	PDEBUG(D_STREAM, "qual_upd %d%%", qual);
	set_quality(gspca_dev, qual);
	mutex_unlock(&gspca_dev->usb_lock);
}

#if defined(CONFIG_INPUT) || defined(CONFIG_INPUT_MODULE)
static int sd_int_pkt_scan(struct gspca_dev *gspca_dev,
			u8 *data,		/* interrupt packet */
			int len)		/* interrupt packet length */
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (!(sd->flags & HAS_NO_BUTTON) && len == 1) {
		input_report_key(gspca_dev->input_dev, KEY_CAMERA, 1);
		input_sync(gspca_dev->input_dev);
		input_report_key(gspca_dev->input_dev, KEY_CAMERA, 0);
		input_sync(gspca_dev->input_dev);
		return 0;
	}
	return -EINVAL;
}
#endif

/* check the JPEG compression */
static void transfer_check(struct gspca_dev *gspca_dev,
			u8 *data)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int new_qual, r;

	new_qual = 0;

	/* if USB error, discard the frame and decrease the quality */
	if (data[6] & 0x08) {				/* USB FIFO full */
		gspca_dev->last_packet_type = DISCARD_PACKET;
		new_qual = -5;
	} else {

		/* else, compute the filling rate and a new JPEG quality */
		r = (sd->pktsz * 100) /
			(sd->npkt *
				gspca_dev->urb[0]->iso_frame_desc[0].length);
		if (r >= 85)
			new_qual = -3;
		else if (r < 75)
			new_qual = 2;
	}
	if (new_qual != 0) {
		sd->nchg += new_qual;
		if (sd->nchg < -6 || sd->nchg >= 12) {
			/* Note: we are in interrupt context, so we can't
			   use v4l2_ctrl_g/s_ctrl here. Access the value
			   directly instead. */
			s32 curqual = sd->jpegqual->cur.val;
			sd->nchg = 0;
			new_qual += curqual;
			if (new_qual < sd->jpegqual->minimum)
				new_qual = sd->jpegqual->minimum;
			else if (new_qual > sd->jpegqual->maximum)
				new_qual = sd->jpegqual->maximum;
			if (new_qual != curqual) {
				sd->jpegqual->cur.val = new_qual;
				queue_work(sd->work_thread, &sd->work);
			}
		}
	} else {
		sd->nchg = 0;
	}
	sd->pktsz = sd->npkt = 0;
}

static void sd_pkt_scan(struct gspca_dev *gspca_dev,
			u8 *data,			/* isoc packet */
			int len)			/* iso packet length */
{
	struct sd *sd = (struct sd *) gspca_dev;
	int avg_lum, is_jpeg;
	static const u8 frame_header[] =
		{0xff, 0xff, 0x00, 0xc4, 0xc4, 0x96};

	is_jpeg = (sd->fmt & 0x03) == 0;
	if (len >= 64 && memcmp(data, frame_header, 6) == 0) {
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

		if (is_jpeg)
			transfer_check(gspca_dev, data);

		gspca_frame_add(gspca_dev, LAST_PACKET, NULL, 0);
		len -= 64;
		if (len == 0)
			return;
		data += 64;
	}
	if (gspca_dev->last_packet_type == LAST_PACKET) {
		if (is_jpeg) {
			gspca_frame_add(gspca_dev, FIRST_PACKET,
				sd->jpeg_hdr, JPEG_HDR_SZ);
			gspca_frame_add(gspca_dev, INTER_PACKET,
				data, len);
		} else {
			gspca_frame_add(gspca_dev, FIRST_PACKET,
				data, len);
		}
	} else {
		/* if JPEG, count the packets and their size */
		if (is_jpeg) {
			sd->npkt++;
			sd->pktsz += len;
		}
		gspca_frame_add(gspca_dev, INTER_PACKET, data, len);
	}
}

/* sub-driver description */
static const struct sd_desc sd_desc = {
	.name = KBUILD_MODNAME,
	.config = sd_config,
	.init = sd_init,
	.init_controls = sd_init_controls,
	.isoc_init = sd_isoc_init,
	.start = sd_start,
	.stopN = sd_stopN,
	.stop0 = sd_stop0,
	.pkt_scan = sd_pkt_scan,
#if defined(CONFIG_INPUT) || defined(CONFIG_INPUT_MODULE)
	.int_pkt_scan = sd_int_pkt_scan,
#endif
	.dq_callback = sd_dqcallback,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.set_register = sd_dbg_s_register,
	.get_register = sd_dbg_g_register,
#endif
	.get_chip_ident = sd_chip_ident,
};

#define SN9C20X(sensor, i2c_addr, flags) \
	.driver_info =  ((flags & 0xff) << 16) \
			| (SENSOR_ ## sensor << 8) \
			| (i2c_addr)

static const struct usb_device_id device_table[] = {
	{USB_DEVICE(0x0c45, 0x6240), SN9C20X(MT9M001, 0x5d, 0)},
	{USB_DEVICE(0x0c45, 0x6242), SN9C20X(MT9M111, 0x5d, 0)},
	{USB_DEVICE(0x0c45, 0x6248), SN9C20X(OV9655, 0x30, 0)},
	{USB_DEVICE(0x0c45, 0x624c), SN9C20X(MT9M112, 0x5d, 0)},
	{USB_DEVICE(0x0c45, 0x624e), SN9C20X(SOI968, 0x30, LED_REVERSE)},
	{USB_DEVICE(0x0c45, 0x624f), SN9C20X(OV9650, 0x30,
					     (FLIP_DETECT | HAS_NO_BUTTON))},
	{USB_DEVICE(0x0c45, 0x6251), SN9C20X(OV9650, 0x30, 0)},
	{USB_DEVICE(0x0c45, 0x6253), SN9C20X(OV9650, 0x30, 0)},
	{USB_DEVICE(0x0c45, 0x6260), SN9C20X(OV7670, 0x21, 0)},
	{USB_DEVICE(0x0c45, 0x6270), SN9C20X(MT9VPRB, 0x00, 0)},
	{USB_DEVICE(0x0c45, 0x627b), SN9C20X(OV7660, 0x21, FLIP_DETECT)},
	{USB_DEVICE(0x0c45, 0x627c), SN9C20X(HV7131R, 0x11, 0)},
	{USB_DEVICE(0x0c45, 0x627f), SN9C20X(OV9650, 0x30, 0)},
	{USB_DEVICE(0x0c45, 0x6280), SN9C20X(MT9M001, 0x5d, 0)},
	{USB_DEVICE(0x0c45, 0x6282), SN9C20X(MT9M111, 0x5d, 0)},
	{USB_DEVICE(0x0c45, 0x6288), SN9C20X(OV9655, 0x30, 0)},
	{USB_DEVICE(0x0c45, 0x628c), SN9C20X(MT9M112, 0x5d, 0)},
	{USB_DEVICE(0x0c45, 0x628e), SN9C20X(SOI968, 0x30, 0)},
	{USB_DEVICE(0x0c45, 0x628f), SN9C20X(OV9650, 0x30, 0)},
	{USB_DEVICE(0x0c45, 0x62a0), SN9C20X(OV7670, 0x21, 0)},
	{USB_DEVICE(0x0c45, 0x62b0), SN9C20X(MT9VPRB, 0x00, 0)},
	{USB_DEVICE(0x0c45, 0x62b3), SN9C20X(OV9655, 0x30, LED_REVERSE)},
	{USB_DEVICE(0x0c45, 0x62bb), SN9C20X(OV7660, 0x21, LED_REVERSE)},
	{USB_DEVICE(0x0c45, 0x62bc), SN9C20X(HV7131R, 0x11, 0)},
	{USB_DEVICE(0x045e, 0x00f4), SN9C20X(OV9650, 0x30, 0)},
	{USB_DEVICE(0x145f, 0x013d), SN9C20X(OV7660, 0x21, 0)},
	{USB_DEVICE(0x0458, 0x7029), SN9C20X(HV7131R, 0x11, 0)},
	{USB_DEVICE(0x0458, 0x704a), SN9C20X(MT9M112, 0x5d, 0)},
	{USB_DEVICE(0x0458, 0x704c), SN9C20X(MT9M112, 0x5d, 0)},
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

static struct usb_driver sd_driver = {
	.name = KBUILD_MODNAME,
	.id_table = device_table,
	.probe = sd_probe,
	.disconnect = gspca_disconnect,
#ifdef CONFIG_PM
	.suspend = gspca_suspend,
	.resume = gspca_resume,
	.reset_resume = gspca_resume,
#endif
};

module_usb_driver(sd_driver);
