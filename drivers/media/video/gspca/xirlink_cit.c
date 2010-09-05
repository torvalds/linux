/*
 * USB IBM C-It Video Camera driver
 *
 * Supports Xirlink C-It Video Camera, IBM PC Camera,
 * IBM NetCamera and Veo Stingray.
 *
 * Copyright (C) 2010 Hans de Goede <hdgoede@redhat.com>
 *
 * This driver is based on earlier work of:
 *
 * (C) Copyright 1999 Johannes Erdfelt
 * (C) Copyright 1999 Randy Dunlap
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#define MODULE_NAME "xirlink-cit"

#include "gspca.h"

MODULE_AUTHOR("Hans de Goede <hdgoede@redhat.com>");
MODULE_DESCRIPTION("Xirlink C-IT");
MODULE_LICENSE("GPL");

/* FIXME we should autodetect this */
static int ibm_netcam_pro;
module_param(ibm_netcam_pro, int, 0);
MODULE_PARM_DESC(ibm_netcam_pro,
		 "Use IBM Netcamera Pro init sequences for Model 3 cams");

/* FIXME this should be handled through the V4L2 input selection API */
static int rca_input;
module_param(rca_input, int, 0644);
MODULE_PARM_DESC(rca_input,
		 "Use rca input instead of ccd sensor on Model 3 cams");

/* specific webcam descriptor */
struct sd {
	struct gspca_dev gspca_dev;		/* !! must be the first item */
	u8 model;
#define CIT_MODEL1 0 /* The model 1 - 4 nomenclature comes from the old */
#define CIT_MODEL2 1 /* ibmcam driver */
#define CIT_MODEL3 2
#define CIT_MODEL4 3
#define CIT_IBM_NETCAM_PRO 4
	u8 input_index;
	u8 sof_read;
	u8 contrast;
	u8 brightness;
	u8 hue;
	u8 sharpness;
};

/* V4L2 controls supported by the driver */
static int sd_setbrightness(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getbrightness(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setcontrast(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getcontrast(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_sethue(struct gspca_dev *gspca_dev, __s32 val);
static int sd_gethue(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setsharpness(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getsharpness(struct gspca_dev *gspca_dev, __s32 *val);
static void sd_stop0(struct gspca_dev *gspca_dev);

static const struct ctrl sd_ctrls[] = {
#define SD_BRIGHTNESS 0
	{
	    {
		.id      = V4L2_CID_BRIGHTNESS,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Brightness",
		.minimum = 0x0c,
		.maximum = 0x3f,
		.step = 1,
#define BRIGHTNESS_DEFAULT 0x20
		.default_value = BRIGHTNESS_DEFAULT,
		.flags = 0,
	    },
	    .set = sd_setbrightness,
	    .get = sd_getbrightness,
	},
#define SD_CONTRAST 1
	{
	    {
		.id = V4L2_CID_CONTRAST,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "contrast",
		.minimum = 0,
		.maximum = 20,
		.step = 1,
#define CONTRAST_DEFAULT 10
		.default_value = CONTRAST_DEFAULT,
		.flags = 0,
	    },
	    .set = sd_setcontrast,
	    .get = sd_getcontrast,
	},
#define SD_HUE 2
	{
	    {
		.id	= V4L2_CID_HUE,
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.name	= "Hue",
		.minimum = 0x05,
		.maximum = 0x37,
		.step	= 1,
#define HUE_DEFAULT 0x20
		.default_value = HUE_DEFAULT,
		.flags = 0,
	    },
	    .set = sd_sethue,
	    .get = sd_gethue,
	},
#define SD_SHARPNESS 3
	{
	    {
		.id = V4L2_CID_SHARPNESS,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Sharpness",
		.minimum = 0,
		.maximum = 6,
		.step = 1,
#define SHARPNESS_DEFAULT 3
		.default_value = SHARPNESS_DEFAULT,
		.flags = 0,
	    },
	    .set = sd_setsharpness,
	    .get = sd_getsharpness,
	},
};

static const struct v4l2_pix_format vga_yuv_mode[] = {
	{160, 120, V4L2_PIX_FMT_CIT_YYVYUY, V4L2_FIELD_NONE,
		.bytesperline = 160,
		.sizeimage = 160 * 120 * 3 / 2,
		.colorspace = V4L2_COLORSPACE_SRGB},
	{320, 240, V4L2_PIX_FMT_CIT_YYVYUY, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240 * 3 / 2,
		.colorspace = V4L2_COLORSPACE_SRGB},
	{640, 480, V4L2_PIX_FMT_CIT_YYVYUY, V4L2_FIELD_NONE,
		.bytesperline = 640,
		.sizeimage = 640 * 480 * 3 / 2,
		.colorspace = V4L2_COLORSPACE_SRGB},
};

/*
 * 01.01.08 - Added for RCA video in support -LO
 * This struct is used to init the Model3 cam to use the RCA video in port
 * instead of the CCD sensor.
 */
static const u16 rca_initdata[][3] = {
	{0, 0x0000, 0x010c},
	{0, 0x0006, 0x012c},
	{0, 0x0078, 0x012d},
	{0, 0x0046, 0x012f},
	{0, 0xd141, 0x0124},
	{0, 0x0000, 0x0127},
	{0, 0xfea8, 0x0124},
	{1, 0x0000, 0x0116},
	{0, 0x0064, 0x0116},
	{1, 0x0000, 0x0115},
	{0, 0x0003, 0x0115},
	{0, 0x0008, 0x0123},
	{0, 0x0000, 0x0117},
	{0, 0x0000, 0x0112},
	{0, 0x0080, 0x0100},
	{0, 0x0000, 0x0100},
	{1, 0x0000, 0x0116},
	{0, 0x0060, 0x0116},
	{0, 0x0002, 0x0112},
	{0, 0x0000, 0x0123},
	{0, 0x0001, 0x0117},
	{0, 0x0040, 0x0108},
	{0, 0x0019, 0x012c},
	{0, 0x0040, 0x0116},
	{0, 0x000a, 0x0115},
	{0, 0x000b, 0x0115},
	{0, 0x0078, 0x012d},
	{0, 0x0046, 0x012f},
	{0, 0xd141, 0x0124},
	{0, 0x0000, 0x0127},
	{0, 0xfea8, 0x0124},
	{0, 0x0064, 0x0116},
	{0, 0x0000, 0x0115},
	{0, 0x0001, 0x0115},
	{0, 0xffff, 0x0124},
	{0, 0xfff9, 0x0124},
	{0, 0x0086, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x00aa, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0000, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0xfffa, 0x0124},
	{0, 0xffff, 0x0124},
	{0, 0xfff9, 0x0124},
	{0, 0x0086, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x00f2, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x000f, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0xfffa, 0x0124},
	{0, 0xffff, 0x0124},
	{0, 0xfff9, 0x0124},
	{0, 0x0086, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x00f8, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x00fc, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0xfffa, 0x0124},
	{0, 0xffff, 0x0124},
	{0, 0xfff9, 0x0124},
	{0, 0x0086, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x00f9, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x003c, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0xfffa, 0x0124},
	{0, 0xffff, 0x0124},
	{0, 0xfff9, 0x0124},
	{0, 0x0086, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0027, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0019, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0xfffa, 0x0124},
	{0, 0xfff9, 0x0124},
	{0, 0x0086, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0037, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0000, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0021, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0xfffa, 0x0124},
	{0, 0xfff9, 0x0124},
	{0, 0x0086, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0038, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0006, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0045, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0xfffa, 0x0124},
	{0, 0xfff9, 0x0124},
	{0, 0x0086, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0037, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0001, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x002a, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0xfffa, 0x0124},
	{0, 0xfff9, 0x0124},
	{0, 0x0086, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0038, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0000, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x000e, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0xfffa, 0x0124},
	{0, 0xfff9, 0x0124},
	{0, 0x0086, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0037, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0001, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x002b, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0xfffa, 0x0124},
	{0, 0xfff9, 0x0124},
	{0, 0x0086, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0038, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0001, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x00f4, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0xfffa, 0x0124},
	{0, 0xfff9, 0x0124},
	{0, 0x0086, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0037, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0001, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x002c, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0xfffa, 0x0124},
	{0, 0xfff9, 0x0124},
	{0, 0x0086, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0038, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0001, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0004, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0xfffa, 0x0124},
	{0, 0xfff9, 0x0124},
	{0, 0x0086, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0037, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0001, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x002d, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0xfffa, 0x0124},
	{0, 0xfff9, 0x0124},
	{0, 0x0086, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0038, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0000, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0014, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0xfffa, 0x0124},
	{0, 0xfff9, 0x0124},
	{0, 0x0086, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0037, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0001, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x002e, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0xfffa, 0x0124},
	{0, 0xfff9, 0x0124},
	{0, 0x0086, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0038, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0003, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0000, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0xfffa, 0x0124},
	{0, 0xfff9, 0x0124},
	{0, 0x0086, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0037, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0001, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x002f, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0xfffa, 0x0124},
	{0, 0xfff9, 0x0124},
	{0, 0x0086, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0038, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0003, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0014, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0xfffa, 0x0124},
	{0, 0xfff9, 0x0124},
	{0, 0x0086, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0037, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0001, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0040, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0xfffa, 0x0124},
	{0, 0xfff9, 0x0124},
	{0, 0x0086, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0038, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0000, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0040, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0xfffa, 0x0124},
	{0, 0xfff9, 0x0124},
	{0, 0x0086, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0037, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0001, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0053, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0xfffa, 0x0124},
	{0, 0xfff9, 0x0124},
	{0, 0x0086, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0038, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0000, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0038, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0xfffa, 0x0124},
	{0, 0x0000, 0x0101},
	{0, 0x00a0, 0x0103},
	{0, 0x0078, 0x0105},
	{0, 0x0000, 0x010a},
	{0, 0x0024, 0x010b},
	{0, 0x0028, 0x0119},
	{0, 0x0088, 0x011b},
	{0, 0x0002, 0x011d},
	{0, 0x0003, 0x011e},
	{0, 0x0000, 0x0129},
	{0, 0x00fc, 0x012b},
	{0, 0x0008, 0x0102},
	{0, 0x0000, 0x0104},
	{0, 0x0008, 0x011a},
	{0, 0x0028, 0x011c},
	{0, 0x0021, 0x012a},
	{0, 0x0000, 0x0118},
	{0, 0x0000, 0x0132},
	{0, 0x0000, 0x0109},
	{0, 0xfff9, 0x0124},
	{0, 0x0086, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0037, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0001, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0031, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0xfffa, 0x0124},
	{0, 0xfff9, 0x0124},
	{0, 0x0086, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0038, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0000, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0000, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0xfffa, 0x0124},
	{0, 0xfff9, 0x0124},
	{0, 0x0086, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0037, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0001, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0040, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0xfffa, 0x0124},
	{0, 0xfff9, 0x0124},
	{0, 0x0086, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0038, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0000, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0040, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0xfffa, 0x0124},
	{0, 0xfff9, 0x0124},
	{0, 0x0086, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0037, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0000, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x00dc, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0xfffa, 0x0124},
	{0, 0xfff9, 0x0124},
	{0, 0x0086, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0038, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0000, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0000, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0xfffa, 0x0124},
	{0, 0xfff9, 0x0124},
	{0, 0x0086, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0037, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0001, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0032, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0xfffa, 0x0124},
	{0, 0xfff9, 0x0124},
	{0, 0x0086, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0038, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0001, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0020, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0xfffa, 0x0124},
	{0, 0xfff9, 0x0124},
	{0, 0x0086, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0037, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0001, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0040, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0xfffa, 0x0124},
	{0, 0xfff9, 0x0124},
	{0, 0x0086, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0038, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0000, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0040, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0xfffa, 0x0124},
	{0, 0xfff9, 0x0124},
	{0, 0x0086, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0037, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0000, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0030, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0xfffa, 0x0124},
	{0, 0xfff9, 0x0124},
	{0, 0x0086, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0038, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0008, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0x0000, 0x0127},
	{0, 0xfff8, 0x0124},
	{0, 0xfffd, 0x0124},
	{0, 0xfffa, 0x0124},
	{0, 0x0003, 0x0111},
};

static int cit_write_reg(struct gspca_dev *gspca_dev, u16 value, u16 index)
{
	struct usb_device *udev = gspca_dev->dev;
	int err;

	err = usb_control_msg(udev, usb_sndctrlpipe(udev, 0), 0x00,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_ENDPOINT,
			value, index, NULL, 0, 1000);
	if (err < 0)
		PDEBUG(D_ERR, "Failed to write a register (index 0x%04X,"
			" value 0x%02X, error %d)", index, value, err);

	return 0;
}

static int cit_read_reg(struct gspca_dev *gspca_dev, u16 index)
{
	struct usb_device *udev = gspca_dev->dev;
	__u8 *buf = gspca_dev->usb_buf;
	int res;

	res = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0), 0x01,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_ENDPOINT,
			0x00, index, buf, 8, 1000);
	if (res < 0) {
		PDEBUG(D_ERR,
			"Failed to read a register (index 0x%04X, error %d)",
			index, res);
		return res;
	}

	PDEBUG(D_PROBE,
	       "Register %04x value: %02x %02x %02x %02x %02x %02x %02x %02x",
	       index,
	       buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);

	return 0;
}

/*
 * ibmcam_model3_Packet1()
 *
 * 00_0078_012d
 * 00_0097_012f
 * 00_d141_0124
 * 00_0096_0127
 * 00_fea8_0124
*/
static void cit_model3_Packet1(struct gspca_dev *gspca_dev, u16 v1, u16 v2)
{
	cit_write_reg(gspca_dev, 0x0078, 0x012d);
	cit_write_reg(gspca_dev, v1,     0x012f);
	cit_write_reg(gspca_dev, 0xd141, 0x0124);
	cit_write_reg(gspca_dev, v2,     0x0127);
	cit_write_reg(gspca_dev, 0xfea8, 0x0124);
}

/* this function is called at probe time */
static int sd_config(struct gspca_dev *gspca_dev,
		     const struct usb_device_id *id)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct cam *cam;

	sd->model = id->driver_info;
	if (sd->model == CIT_MODEL3 && ibm_netcam_pro)
		sd->model = CIT_IBM_NETCAM_PRO;

	cam = &gspca_dev->cam;
	switch (sd->model) {
	case CIT_MODEL3:
		cam->cam_mode = vga_yuv_mode;
		cam->nmodes = ARRAY_SIZE(vga_yuv_mode);
		gspca_dev->ctrl_dis = (1 << SD_HUE);
		break;
	case CIT_IBM_NETCAM_PRO:
		cam->cam_mode = vga_yuv_mode;
		cam->nmodes = 2; /* no 640 x 480 */
		cam->input_flags = V4L2_IN_ST_VFLIP;
		gspca_dev->ctrl_dis = ~(1 << SD_CONTRAST);
		break;
	}

	sd->brightness = BRIGHTNESS_DEFAULT;
	sd->contrast = CONTRAST_DEFAULT;
	sd->hue = HUE_DEFAULT;
	sd->sharpness = SHARPNESS_DEFAULT;

	return 0;
}

static int cit_init_ibm_netcam_pro(struct gspca_dev *gspca_dev)
{
	cit_read_reg(gspca_dev, 0x128);
	cit_write_reg(gspca_dev, 0x0003, 0x0133);
	cit_write_reg(gspca_dev, 0x0000, 0x0117);
	cit_write_reg(gspca_dev, 0x0008, 0x0123);
	cit_write_reg(gspca_dev, 0x0000, 0x0100);
	cit_read_reg(gspca_dev, 0x0116);
	cit_write_reg(gspca_dev, 0x0060, 0x0116);
	cit_write_reg(gspca_dev, 0x0002, 0x0112);
	cit_write_reg(gspca_dev, 0x0000, 0x0133);
	cit_write_reg(gspca_dev, 0x0000, 0x0123);
	cit_write_reg(gspca_dev, 0x0001, 0x0117);
	cit_write_reg(gspca_dev, 0x0040, 0x0108);
	cit_write_reg(gspca_dev, 0x0019, 0x012c);
	cit_write_reg(gspca_dev, 0x0060, 0x0116);
	cit_write_reg(gspca_dev, 0x0002, 0x0115);
	cit_write_reg(gspca_dev, 0x000b, 0x0115);

	cit_write_reg(gspca_dev, 0x0078, 0x012d);
	cit_write_reg(gspca_dev, 0x0001, 0x012f);
	cit_write_reg(gspca_dev, 0xd141, 0x0124);
	cit_write_reg(gspca_dev, 0x0079, 0x012d);
	cit_write_reg(gspca_dev, 0x00ff, 0x0130);
	cit_write_reg(gspca_dev, 0xcd41, 0x0124);
	cit_write_reg(gspca_dev, 0xfffa, 0x0124);
	cit_read_reg(gspca_dev, 0x0126);

	cit_model3_Packet1(gspca_dev, 0x0000, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x0000, 0x0001);
	cit_model3_Packet1(gspca_dev, 0x000b, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x000c, 0x0008);
	cit_model3_Packet1(gspca_dev, 0x000d, 0x003a);
	cit_model3_Packet1(gspca_dev, 0x000e, 0x0060);
	cit_model3_Packet1(gspca_dev, 0x000f, 0x0060);
	cit_model3_Packet1(gspca_dev, 0x0010, 0x0008);
	cit_model3_Packet1(gspca_dev, 0x0011, 0x0004);
	cit_model3_Packet1(gspca_dev, 0x0012, 0x0028);
	cit_model3_Packet1(gspca_dev, 0x0013, 0x0002);
	cit_model3_Packet1(gspca_dev, 0x0014, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x0015, 0x00fb);
	cit_model3_Packet1(gspca_dev, 0x0016, 0x0002);
	cit_model3_Packet1(gspca_dev, 0x0017, 0x0037);
	cit_model3_Packet1(gspca_dev, 0x0018, 0x0036);
	cit_model3_Packet1(gspca_dev, 0x001e, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x001f, 0x0008);
	cit_model3_Packet1(gspca_dev, 0x0020, 0x00c1);
	cit_model3_Packet1(gspca_dev, 0x0021, 0x0034);
	cit_model3_Packet1(gspca_dev, 0x0022, 0x0034);
	cit_model3_Packet1(gspca_dev, 0x0025, 0x0002);
	cit_model3_Packet1(gspca_dev, 0x0028, 0x0022);
	cit_model3_Packet1(gspca_dev, 0x0029, 0x000a);
	cit_model3_Packet1(gspca_dev, 0x002b, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x002c, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x002d, 0x00ff);
	cit_model3_Packet1(gspca_dev, 0x002e, 0x00ff);
	cit_model3_Packet1(gspca_dev, 0x002f, 0x00ff);
	cit_model3_Packet1(gspca_dev, 0x0030, 0x00ff);
	cit_model3_Packet1(gspca_dev, 0x0031, 0x00ff);
	cit_model3_Packet1(gspca_dev, 0x0032, 0x0007);
	cit_model3_Packet1(gspca_dev, 0x0033, 0x0005);
	cit_model3_Packet1(gspca_dev, 0x0037, 0x0040);
	cit_model3_Packet1(gspca_dev, 0x0039, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x003a, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x003b, 0x0001);
	cit_model3_Packet1(gspca_dev, 0x003c, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x0040, 0x000c);
	cit_model3_Packet1(gspca_dev, 0x0041, 0x00fb);
	cit_model3_Packet1(gspca_dev, 0x0042, 0x0002);
	cit_model3_Packet1(gspca_dev, 0x0043, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x0045, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x0046, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x0047, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x0048, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x0049, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x004a, 0x00ff);
	cit_model3_Packet1(gspca_dev, 0x004b, 0x00ff);
	cit_model3_Packet1(gspca_dev, 0x004c, 0x00ff);
	cit_model3_Packet1(gspca_dev, 0x004f, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x0050, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x0051, 0x0002);
	cit_model3_Packet1(gspca_dev, 0x0055, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x0056, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x0057, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x0058, 0x0002);
	cit_model3_Packet1(gspca_dev, 0x0059, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x005c, 0x0016);
	cit_model3_Packet1(gspca_dev, 0x005d, 0x0022);
	cit_model3_Packet1(gspca_dev, 0x005e, 0x003c);
	cit_model3_Packet1(gspca_dev, 0x005f, 0x0050);
	cit_model3_Packet1(gspca_dev, 0x0060, 0x0044);
	cit_model3_Packet1(gspca_dev, 0x0061, 0x0005);
	cit_model3_Packet1(gspca_dev, 0x006a, 0x007e);
	cit_model3_Packet1(gspca_dev, 0x006f, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x0072, 0x001b);
	cit_model3_Packet1(gspca_dev, 0x0073, 0x0005);
	cit_model3_Packet1(gspca_dev, 0x0074, 0x000a);
	cit_model3_Packet1(gspca_dev, 0x0075, 0x001b);
	cit_model3_Packet1(gspca_dev, 0x0076, 0x002a);
	cit_model3_Packet1(gspca_dev, 0x0077, 0x003c);
	cit_model3_Packet1(gspca_dev, 0x0078, 0x0050);
	cit_model3_Packet1(gspca_dev, 0x007b, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x007c, 0x0011);
	cit_model3_Packet1(gspca_dev, 0x007d, 0x0024);
	cit_model3_Packet1(gspca_dev, 0x007e, 0x0043);
	cit_model3_Packet1(gspca_dev, 0x007f, 0x005a);
	cit_model3_Packet1(gspca_dev, 0x0084, 0x0020);
	cit_model3_Packet1(gspca_dev, 0x0085, 0x0033);
	cit_model3_Packet1(gspca_dev, 0x0086, 0x000a);
	cit_model3_Packet1(gspca_dev, 0x0087, 0x0030);
	cit_model3_Packet1(gspca_dev, 0x0088, 0x0070);
	cit_model3_Packet1(gspca_dev, 0x008b, 0x0008);
	cit_model3_Packet1(gspca_dev, 0x008f, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x0090, 0x0006);
	cit_model3_Packet1(gspca_dev, 0x0091, 0x0028);
	cit_model3_Packet1(gspca_dev, 0x0092, 0x005a);
	cit_model3_Packet1(gspca_dev, 0x0093, 0x0082);
	cit_model3_Packet1(gspca_dev, 0x0096, 0x0014);
	cit_model3_Packet1(gspca_dev, 0x0097, 0x0020);
	cit_model3_Packet1(gspca_dev, 0x0098, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x00b0, 0x0046);
	cit_model3_Packet1(gspca_dev, 0x00b1, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x00b2, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x00b3, 0x0004);
	cit_model3_Packet1(gspca_dev, 0x00b4, 0x0007);
	cit_model3_Packet1(gspca_dev, 0x00b6, 0x0002);
	cit_model3_Packet1(gspca_dev, 0x00b7, 0x0004);
	cit_model3_Packet1(gspca_dev, 0x00bb, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x00bc, 0x0001);
	cit_model3_Packet1(gspca_dev, 0x00bd, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x00bf, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x00c0, 0x00c8);
	cit_model3_Packet1(gspca_dev, 0x00c1, 0x0014);
	cit_model3_Packet1(gspca_dev, 0x00c2, 0x0001);
	cit_model3_Packet1(gspca_dev, 0x00c3, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x00c4, 0x0004);
	cit_model3_Packet1(gspca_dev, 0x00cb, 0x00bf);
	cit_model3_Packet1(gspca_dev, 0x00cc, 0x00bf);
	cit_model3_Packet1(gspca_dev, 0x00cd, 0x00bf);
	cit_model3_Packet1(gspca_dev, 0x00ce, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x00cf, 0x0020);
	cit_model3_Packet1(gspca_dev, 0x00d0, 0x0040);
	cit_model3_Packet1(gspca_dev, 0x00d1, 0x00bf);
	cit_model3_Packet1(gspca_dev, 0x00d1, 0x00bf);
	cit_model3_Packet1(gspca_dev, 0x00d2, 0x00bf);
	cit_model3_Packet1(gspca_dev, 0x00d3, 0x00bf);
	cit_model3_Packet1(gspca_dev, 0x00ea, 0x0008);
	cit_model3_Packet1(gspca_dev, 0x00eb, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x00ec, 0x00e8);
	cit_model3_Packet1(gspca_dev, 0x00ed, 0x0001);
	cit_model3_Packet1(gspca_dev, 0x00ef, 0x0022);
	cit_model3_Packet1(gspca_dev, 0x00f0, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x00f2, 0x0028);
	cit_model3_Packet1(gspca_dev, 0x00f4, 0x0002);
	cit_model3_Packet1(gspca_dev, 0x00f5, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x00fa, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x00fb, 0x0001);
	cit_model3_Packet1(gspca_dev, 0x00fc, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x00fd, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x00fe, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x00ff, 0x0000);

	cit_model3_Packet1(gspca_dev, 0x00be, 0x0003);
	cit_model3_Packet1(gspca_dev, 0x00c8, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x00c9, 0x0020);
	cit_model3_Packet1(gspca_dev, 0x00ca, 0x0040);
	cit_model3_Packet1(gspca_dev, 0x0053, 0x0001);
	cit_model3_Packet1(gspca_dev, 0x0082, 0x000e);
	cit_model3_Packet1(gspca_dev, 0x0083, 0x0020);
	cit_model3_Packet1(gspca_dev, 0x0034, 0x003c);
	cit_model3_Packet1(gspca_dev, 0x006e, 0x0055);
	cit_model3_Packet1(gspca_dev, 0x0062, 0x0005);
	cit_model3_Packet1(gspca_dev, 0x0063, 0x0008);
	cit_model3_Packet1(gspca_dev, 0x0066, 0x000a);
	cit_model3_Packet1(gspca_dev, 0x0067, 0x0006);
	cit_model3_Packet1(gspca_dev, 0x006b, 0x0010);
	cit_model3_Packet1(gspca_dev, 0x005a, 0x0001);
	cit_model3_Packet1(gspca_dev, 0x005b, 0x000a);
	cit_model3_Packet1(gspca_dev, 0x0023, 0x0006);
	cit_model3_Packet1(gspca_dev, 0x0026, 0x0004);
	cit_model3_Packet1(gspca_dev, 0x0036, 0x0069);
	cit_model3_Packet1(gspca_dev, 0x0038, 0x0064);
	cit_model3_Packet1(gspca_dev, 0x003d, 0x0003);
	cit_model3_Packet1(gspca_dev, 0x003e, 0x0001);
	cit_model3_Packet1(gspca_dev, 0x00b8, 0x0014);
	cit_model3_Packet1(gspca_dev, 0x00b9, 0x0014);
	cit_model3_Packet1(gspca_dev, 0x00e6, 0x0004);
	cit_model3_Packet1(gspca_dev, 0x00e8, 0x0001);

	return 0;
}

/* this function is called at probe and resume time */
static int sd_init(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	switch (sd->model) {
	case CIT_MODEL3:
		break; /* All is done in sd_start */
	case CIT_IBM_NETCAM_PRO:
		cit_init_ibm_netcam_pro(gspca_dev);
		sd_stop0(gspca_dev);
		break;
	}
	return 0;
}

static int cit_set_brightness(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	switch (sd->model) {
	case CIT_MODEL3:
		/* Model 3: Brightness range 'i' in [0x0C..0x3F] */
		cit_model3_Packet1(gspca_dev, 0x0036, sd->brightness);
		break;
	case CIT_IBM_NETCAM_PRO:
		/* No (known) brightness control for ibm netcam pro */
		break;
	}

	return 0;
}

static int cit_set_contrast(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	switch (sd->model) {
	case CIT_MODEL3:
	{	/* Preset hardware values */
		static const struct {
			unsigned short cv1;
			unsigned short cv2;
			unsigned short cv3;
		} cv[7] = {
			{ 0x05, 0x05, 0x0f },	/* Minimum */
			{ 0x04, 0x04, 0x16 },
			{ 0x02, 0x03, 0x16 },
			{ 0x02, 0x08, 0x16 },
			{ 0x01, 0x0c, 0x16 },
			{ 0x01, 0x0e, 0x16 },
			{ 0x01, 0x10, 0x16 }	/* Maximum */
		};
		int i = sd->contrast / 3;
		cit_model3_Packet1(gspca_dev, 0x0067, cv[i].cv1);
		cit_model3_Packet1(gspca_dev, 0x005b, cv[i].cv2);
		cit_model3_Packet1(gspca_dev, 0x005c, cv[i].cv3);
		break;
	}
	case CIT_IBM_NETCAM_PRO:
		cit_model3_Packet1(gspca_dev, 0x005b, sd->contrast + 1);
		break;
	}
	return 0;
}

static int cit_set_hue(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	switch (sd->model) {
	case CIT_MODEL3:
		/* according to the ibmcam driver this does not work 8/
		/* cit_model3_Packet1(gspca_dev, 0x007e, sd->hue); */
		break;
	case CIT_IBM_NETCAM_PRO:
		/* No hue control for ibm netcam pro */
		break;
	}
	return 0;
}

static int cit_set_sharpness(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	switch (sd->model) {
	case CIT_MODEL3:
	{	/*
		 * "Use a table of magic numbers.
		 *  This setting doesn't really change much.
		 *  But that's how Windows does it."
		 */
		static const struct {
			unsigned short sv1;
			unsigned short sv2;
			unsigned short sv3;
			unsigned short sv4;
		} sv[7] = {
			{ 0x00, 0x00, 0x05, 0x14 },	/* Smoothest */
			{ 0x01, 0x04, 0x05, 0x14 },
			{ 0x02, 0x04, 0x05, 0x14 },
			{ 0x03, 0x04, 0x05, 0x14 },
			{ 0x03, 0x05, 0x05, 0x14 },
			{ 0x03, 0x06, 0x05, 0x14 },
			{ 0x03, 0x07, 0x05, 0x14 }	/* Sharpest */
		};
		cit_model3_Packet1(gspca_dev, 0x0060, sv[sd->sharpness].sv1);
		cit_model3_Packet1(gspca_dev, 0x0061, sv[sd->sharpness].sv2);
		cit_model3_Packet1(gspca_dev, 0x0062, sv[sd->sharpness].sv3);
		cit_model3_Packet1(gspca_dev, 0x0063, sv[sd->sharpness].sv4);
		break;
	}
	case CIT_IBM_NETCAM_PRO:
		/* No sharpness setting on ibm netcamera pro */
		break;
	}
	return 0;
}

static int cit_restart_stream(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	switch (sd->model) {
	case CIT_MODEL3:
	case CIT_IBM_NETCAM_PRO:
		cit_write_reg(gspca_dev, 0x0001, 0x0114);
		cit_write_reg(gspca_dev, 0x00c0, 0x010c); /* Go! */
		usb_clear_halt(gspca_dev->dev, gspca_dev->urb[0]->pipe);
		cit_write_reg(gspca_dev, 0x0001, 0x0113);
	}

	sd->sof_read = 0;

	return 0;
}

static int cit_start_model3(struct gspca_dev *gspca_dev)
{
	const unsigned short compression = 0; /* 0=none, 7=best frame rate */
	int i, clock_div = 0;

	/* HDG not in ibmcam driver, added to see if it helps with
	   auto-detecting between model3 and ibm netcamera pro */
	cit_read_reg(gspca_dev, 0x128);

	cit_write_reg(gspca_dev, 0x0000, 0x0100);
	cit_read_reg(gspca_dev, 0x0116);
	cit_write_reg(gspca_dev, 0x0060, 0x0116);
	cit_write_reg(gspca_dev, 0x0002, 0x0112);
	cit_write_reg(gspca_dev, 0x0000, 0x0123);
	cit_write_reg(gspca_dev, 0x0001, 0x0117);
	cit_write_reg(gspca_dev, 0x0040, 0x0108);
	cit_write_reg(gspca_dev, 0x0019, 0x012c);
	cit_write_reg(gspca_dev, 0x0060, 0x0116);
	cit_write_reg(gspca_dev, 0x0002, 0x0115);
	cit_write_reg(gspca_dev, 0x0003, 0x0115);
	cit_read_reg(gspca_dev, 0x0115);
	cit_write_reg(gspca_dev, 0x000b, 0x0115);

	/* HDG not in ibmcam driver, added to see if it helps with
	   auto-detecting between model3 and ibm netcamera pro */
	if (0) {
		cit_write_reg(gspca_dev, 0x0078, 0x012d);
		cit_write_reg(gspca_dev, 0x0001, 0x012f);
		cit_write_reg(gspca_dev, 0xd141, 0x0124);
		cit_write_reg(gspca_dev, 0x0079, 0x012d);
		cit_write_reg(gspca_dev, 0x00ff, 0x0130);
		cit_write_reg(gspca_dev, 0xcd41, 0x0124);
		cit_write_reg(gspca_dev, 0xfffa, 0x0124);
		cit_read_reg(gspca_dev, 0x0126);
	}

	cit_model3_Packet1(gspca_dev, 0x000a, 0x0040);
	cit_model3_Packet1(gspca_dev, 0x000b, 0x00f6);
	cit_model3_Packet1(gspca_dev, 0x000c, 0x0002);
	cit_model3_Packet1(gspca_dev, 0x000d, 0x0020);
	cit_model3_Packet1(gspca_dev, 0x000e, 0x0033);
	cit_model3_Packet1(gspca_dev, 0x000f, 0x0007);
	cit_model3_Packet1(gspca_dev, 0x0010, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x0011, 0x0070);
	cit_model3_Packet1(gspca_dev, 0x0012, 0x0030);
	cit_model3_Packet1(gspca_dev, 0x0013, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x0014, 0x0001);
	cit_model3_Packet1(gspca_dev, 0x0015, 0x0001);
	cit_model3_Packet1(gspca_dev, 0x0016, 0x0001);
	cit_model3_Packet1(gspca_dev, 0x0017, 0x0001);
	cit_model3_Packet1(gspca_dev, 0x0018, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x001e, 0x00c3);
	cit_model3_Packet1(gspca_dev, 0x0020, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x0028, 0x0010);
	cit_model3_Packet1(gspca_dev, 0x0029, 0x0054);
	cit_model3_Packet1(gspca_dev, 0x002a, 0x0013);
	cit_model3_Packet1(gspca_dev, 0x002b, 0x0007);
	cit_model3_Packet1(gspca_dev, 0x002d, 0x0028);
	cit_model3_Packet1(gspca_dev, 0x002e, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x0031, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x0032, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x0033, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x0034, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x0035, 0x0038);
	cit_model3_Packet1(gspca_dev, 0x003a, 0x0001);
	cit_model3_Packet1(gspca_dev, 0x003c, 0x001e);
	cit_model3_Packet1(gspca_dev, 0x003f, 0x000a);
	cit_model3_Packet1(gspca_dev, 0x0041, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x0046, 0x003f);
	cit_model3_Packet1(gspca_dev, 0x0047, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x0050, 0x0005);
	cit_model3_Packet1(gspca_dev, 0x0052, 0x001a);
	cit_model3_Packet1(gspca_dev, 0x0053, 0x0003);
	cit_model3_Packet1(gspca_dev, 0x005a, 0x006b);
	cit_model3_Packet1(gspca_dev, 0x005d, 0x001e);
	cit_model3_Packet1(gspca_dev, 0x005e, 0x0030);
	cit_model3_Packet1(gspca_dev, 0x005f, 0x0041);
	cit_model3_Packet1(gspca_dev, 0x0064, 0x0008);
	cit_model3_Packet1(gspca_dev, 0x0065, 0x0015);
	cit_model3_Packet1(gspca_dev, 0x0068, 0x000f);
	cit_model3_Packet1(gspca_dev, 0x0079, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x007a, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x007c, 0x003f);
	cit_model3_Packet1(gspca_dev, 0x0082, 0x000f);
	cit_model3_Packet1(gspca_dev, 0x0085, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x0099, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x009b, 0x0023);
	cit_model3_Packet1(gspca_dev, 0x009c, 0x0022);
	cit_model3_Packet1(gspca_dev, 0x009d, 0x0096);
	cit_model3_Packet1(gspca_dev, 0x009e, 0x0096);
	cit_model3_Packet1(gspca_dev, 0x009f, 0x000a);

	switch (gspca_dev->width) {
	case 160:
		cit_write_reg(gspca_dev, 0x0000, 0x0101); /* Same on 160x120, 320x240 */
		cit_write_reg(gspca_dev, 0x00a0, 0x0103); /* Same on 160x120, 320x240 */
		cit_write_reg(gspca_dev, 0x0078, 0x0105); /* Same on 160x120, 320x240 */
		cit_write_reg(gspca_dev, 0x0000, 0x010a); /* Same */
		cit_write_reg(gspca_dev, 0x0024, 0x010b); /* Differs everywhere */
		cit_write_reg(gspca_dev, 0x00a9, 0x0119);
		cit_write_reg(gspca_dev, 0x0016, 0x011b);
		cit_write_reg(gspca_dev, 0x0002, 0x011d); /* Same on 160x120, 320x240 */
		cit_write_reg(gspca_dev, 0x0003, 0x011e); /* Same on 160x120, 640x480 */
		cit_write_reg(gspca_dev, 0x0000, 0x0129); /* Same */
		cit_write_reg(gspca_dev, 0x00fc, 0x012b); /* Same */
		cit_write_reg(gspca_dev, 0x0018, 0x0102);
		cit_write_reg(gspca_dev, 0x0004, 0x0104);
		cit_write_reg(gspca_dev, 0x0004, 0x011a);
		cit_write_reg(gspca_dev, 0x0028, 0x011c);
		cit_write_reg(gspca_dev, 0x0022, 0x012a); /* Same */
		cit_write_reg(gspca_dev, 0x0000, 0x0118);
		cit_write_reg(gspca_dev, 0x0000, 0x0132);
		cit_model3_Packet1(gspca_dev, 0x0021, 0x0001); /* Same */
		cit_write_reg(gspca_dev, compression, 0x0109);
		clock_div = 3;
		break;
	case 320:
		cit_write_reg(gspca_dev, 0x0000, 0x0101); /* Same on 160x120, 320x240 */
		cit_write_reg(gspca_dev, 0x00a0, 0x0103); /* Same on 160x120, 320x240 */
		cit_write_reg(gspca_dev, 0x0078, 0x0105); /* Same on 160x120, 320x240 */
		cit_write_reg(gspca_dev, 0x0000, 0x010a); /* Same */
		cit_write_reg(gspca_dev, 0x0028, 0x010b); /* Differs everywhere */
		cit_write_reg(gspca_dev, 0x0002, 0x011d); /* Same */
		cit_write_reg(gspca_dev, 0x0000, 0x011e);
		cit_write_reg(gspca_dev, 0x0000, 0x0129); /* Same */
		cit_write_reg(gspca_dev, 0x00fc, 0x012b); /* Same */
		/* 4 commands from 160x120 skipped */
		cit_write_reg(gspca_dev, 0x0022, 0x012a); /* Same */
		cit_model3_Packet1(gspca_dev, 0x0021, 0x0001); /* Same */
		cit_write_reg(gspca_dev, compression, 0x0109);
		cit_write_reg(gspca_dev, 0x00d9, 0x0119);
		cit_write_reg(gspca_dev, 0x0006, 0x011b);
		cit_write_reg(gspca_dev, 0x0021, 0x0102); /* Same on 320x240, 640x480 */
		cit_write_reg(gspca_dev, 0x0010, 0x0104);
		cit_write_reg(gspca_dev, 0x0004, 0x011a);
		cit_write_reg(gspca_dev, 0x003f, 0x011c);
		cit_write_reg(gspca_dev, 0x001c, 0x0118);
		cit_write_reg(gspca_dev, 0x0000, 0x0132);
		clock_div = 5;
		break;
	case 640:
		cit_write_reg(gspca_dev, 0x00f0, 0x0105);
		cit_write_reg(gspca_dev, 0x0000, 0x010a); /* Same */
		cit_write_reg(gspca_dev, 0x0038, 0x010b); /* Differs everywhere */
		cit_write_reg(gspca_dev, 0x00d9, 0x0119); /* Same on 320x240, 640x480 */
		cit_write_reg(gspca_dev, 0x0006, 0x011b); /* Same on 320x240, 640x480 */
		cit_write_reg(gspca_dev, 0x0004, 0x011d); /* NC */
		cit_write_reg(gspca_dev, 0x0003, 0x011e); /* Same on 160x120, 640x480 */
		cit_write_reg(gspca_dev, 0x0000, 0x0129); /* Same */
		cit_write_reg(gspca_dev, 0x00fc, 0x012b); /* Same */
		cit_write_reg(gspca_dev, 0x0021, 0x0102); /* Same on 320x240, 640x480 */
		cit_write_reg(gspca_dev, 0x0016, 0x0104); /* NC */
		cit_write_reg(gspca_dev, 0x0004, 0x011a); /* Same on 320x240, 640x480 */
		cit_write_reg(gspca_dev, 0x003f, 0x011c); /* Same on 320x240, 640x480 */
		cit_write_reg(gspca_dev, 0x0022, 0x012a); /* Same */
		cit_write_reg(gspca_dev, 0x001c, 0x0118); /* Same on 320x240, 640x480 */
		cit_model3_Packet1(gspca_dev, 0x0021, 0x0001); /* Same */
		cit_write_reg(gspca_dev, compression, 0x0109);
		cit_write_reg(gspca_dev, 0x0040, 0x0101);
		cit_write_reg(gspca_dev, 0x0040, 0x0103);
		cit_write_reg(gspca_dev, 0x0000, 0x0132); /* Same on 320x240, 640x480 */
		clock_div = 7;
		break;
	}

	cit_model3_Packet1(gspca_dev, 0x007e, 0x000e);	/* Hue */
	cit_model3_Packet1(gspca_dev, 0x0036, 0x0011);	/* Brightness */
	cit_model3_Packet1(gspca_dev, 0x0060, 0x0002);	/* Sharpness */
	cit_model3_Packet1(gspca_dev, 0x0061, 0x0004);	/* Sharpness */
	cit_model3_Packet1(gspca_dev, 0x0062, 0x0005);	/* Sharpness */
	cit_model3_Packet1(gspca_dev, 0x0063, 0x0014);	/* Sharpness */
	cit_model3_Packet1(gspca_dev, 0x0096, 0x00a0);	/* Red sharpness */
	cit_model3_Packet1(gspca_dev, 0x0097, 0x0096);	/* Blue sharpness */
	cit_model3_Packet1(gspca_dev, 0x0067, 0x0001);	/* Contrast */
	cit_model3_Packet1(gspca_dev, 0x005b, 0x000c);	/* Contrast */
	cit_model3_Packet1(gspca_dev, 0x005c, 0x0016);	/* Contrast */
	cit_model3_Packet1(gspca_dev, 0x0098, 0x000b);
	cit_model3_Packet1(gspca_dev, 0x002c, 0x0003);	/* Was 1, broke 640x480 */
	cit_model3_Packet1(gspca_dev, 0x002f, 0x002a);
	cit_model3_Packet1(gspca_dev, 0x0030, 0x0029);
	cit_model3_Packet1(gspca_dev, 0x0037, 0x0002);
	cit_model3_Packet1(gspca_dev, 0x0038, 0x0059);
	cit_model3_Packet1(gspca_dev, 0x003d, 0x002e);
	cit_model3_Packet1(gspca_dev, 0x003e, 0x0028);
	cit_model3_Packet1(gspca_dev, 0x0078, 0x0005);
	cit_model3_Packet1(gspca_dev, 0x007b, 0x0011);
	cit_model3_Packet1(gspca_dev, 0x007d, 0x004b);
	cit_model3_Packet1(gspca_dev, 0x007f, 0x0022);
	cit_model3_Packet1(gspca_dev, 0x0080, 0x000c);
	cit_model3_Packet1(gspca_dev, 0x0081, 0x000b);
	cit_model3_Packet1(gspca_dev, 0x0083, 0x00fd);
	cit_model3_Packet1(gspca_dev, 0x0086, 0x000b);
	cit_model3_Packet1(gspca_dev, 0x0087, 0x000b);
	cit_model3_Packet1(gspca_dev, 0x007e, 0x000e);
	cit_model3_Packet1(gspca_dev, 0x0096, 0x00a0);	/* Red sharpness */
	cit_model3_Packet1(gspca_dev, 0x0097, 0x0096);	/* Blue sharpness */
	cit_model3_Packet1(gspca_dev, 0x0098, 0x000b);

	cit_write_reg(gspca_dev, clock_div, 0x0111); /* Clock Divider */

	switch (gspca_dev->width) {
	case 160:
		cit_model3_Packet1(gspca_dev, 0x001f, 0x0000); /* Same */
		cit_model3_Packet1(gspca_dev, 0x0039, 0x001f); /* Same */
		cit_model3_Packet1(gspca_dev, 0x003b, 0x003c); /* Same */
		cit_model3_Packet1(gspca_dev, 0x0040, 0x000a);
		cit_model3_Packet1(gspca_dev, 0x0051, 0x000a);
		break;
	case 320:
		cit_model3_Packet1(gspca_dev, 0x001f, 0x0000); /* Same */
		cit_model3_Packet1(gspca_dev, 0x0039, 0x001f); /* Same */
		cit_model3_Packet1(gspca_dev, 0x003b, 0x003c); /* Same */
		cit_model3_Packet1(gspca_dev, 0x0040, 0x0008);
		cit_model3_Packet1(gspca_dev, 0x0051, 0x000b);
		break;
	case 640:
		cit_model3_Packet1(gspca_dev, 0x001f, 0x0002);	/* !Same */
		cit_model3_Packet1(gspca_dev, 0x0039, 0x003e);	/* !Same */
		cit_model3_Packet1(gspca_dev, 0x0040, 0x0008);
		cit_model3_Packet1(gspca_dev, 0x0051, 0x000a);
		break;
	}

/*	if (sd->input_index) { */
	if (rca_input) {
		for (i = 0; i < ARRAY_SIZE(rca_initdata); i++) {
			if (rca_initdata[i][0])
				cit_read_reg(gspca_dev, rca_initdata[i][2]);
			else
				cit_write_reg(gspca_dev, rca_initdata[i][1],
					      rca_initdata[i][2]);
		}
	}

	return 0;
}

static int cit_start_ibm_netcam_pro(struct gspca_dev *gspca_dev)
{
	const unsigned short compression = 0; /* 0=none, 7=best frame rate */
	int i, clock_div = 0;

	cit_write_reg(gspca_dev, 0x0003, 0x0133);
	cit_write_reg(gspca_dev, 0x0000, 0x0117);
	cit_write_reg(gspca_dev, 0x0008, 0x0123);
	cit_write_reg(gspca_dev, 0x0000, 0x0100);
	cit_write_reg(gspca_dev, 0x0060, 0x0116);
	/* cit_write_reg(gspca_dev, 0x0002, 0x0112); see sd_stop0 */
	cit_write_reg(gspca_dev, 0x0000, 0x0133);
	cit_write_reg(gspca_dev, 0x0000, 0x0123);
	cit_write_reg(gspca_dev, 0x0001, 0x0117);
	cit_write_reg(gspca_dev, 0x0040, 0x0108);
	cit_write_reg(gspca_dev, 0x0019, 0x012c);
	cit_write_reg(gspca_dev, 0x0060, 0x0116);
	/* cit_write_reg(gspca_dev, 0x000b, 0x0115); see sd_stop0 */

	cit_model3_Packet1(gspca_dev, 0x0049, 0x0000);

	cit_write_reg(gspca_dev, 0x0000, 0x0101); /* Same on 160x120, 320x240 */
	cit_write_reg(gspca_dev, 0x003a, 0x0102); /* Hstart */
	cit_write_reg(gspca_dev, 0x00a0, 0x0103); /* Same on 160x120, 320x240 */
	cit_write_reg(gspca_dev, 0x0078, 0x0105); /* Same on 160x120, 320x240 */
	cit_write_reg(gspca_dev, 0x0000, 0x010a); /* Same */
	cit_write_reg(gspca_dev, 0x0002, 0x011d); /* Same on 160x120, 320x240 */
	cit_write_reg(gspca_dev, 0x0000, 0x0129); /* Same */
	cit_write_reg(gspca_dev, 0x00fc, 0x012b); /* Same */
	cit_write_reg(gspca_dev, 0x0022, 0x012a); /* Same */

	switch (gspca_dev->width) {
	case 160:
		cit_write_reg(gspca_dev, 0x0024, 0x010b);
		cit_write_reg(gspca_dev, 0x0089, 0x0119);
		cit_write_reg(gspca_dev, 0x000a, 0x011b);
		cit_write_reg(gspca_dev, 0x0003, 0x011e);
		cit_write_reg(gspca_dev, 0x0007, 0x0104);
		cit_write_reg(gspca_dev, 0x0009, 0x011a);
		cit_write_reg(gspca_dev, 0x008b, 0x011c);
		cit_write_reg(gspca_dev, 0x0008, 0x0118);
		cit_write_reg(gspca_dev, 0x0000, 0x0132);
		clock_div = 3;
		break;
	case 320:
		cit_write_reg(gspca_dev, 0x0028, 0x010b);
		cit_write_reg(gspca_dev, 0x00d9, 0x0119);
		cit_write_reg(gspca_dev, 0x0006, 0x011b);
		cit_write_reg(gspca_dev, 0x0000, 0x011e);
		cit_write_reg(gspca_dev, 0x000e, 0x0104);
		cit_write_reg(gspca_dev, 0x0004, 0x011a);
		cit_write_reg(gspca_dev, 0x003f, 0x011c);
		cit_write_reg(gspca_dev, 0x000c, 0x0118);
		cit_write_reg(gspca_dev, 0x0000, 0x0132);
		clock_div = 5;
		break;
	}

	cit_model3_Packet1(gspca_dev, 0x0019, 0x0031);
	cit_model3_Packet1(gspca_dev, 0x001a, 0x0003);
	cit_model3_Packet1(gspca_dev, 0x001b, 0x0038);
	cit_model3_Packet1(gspca_dev, 0x001c, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x0024, 0x0001);
	cit_model3_Packet1(gspca_dev, 0x0027, 0x0001);
	cit_model3_Packet1(gspca_dev, 0x002a, 0x0004);
	cit_model3_Packet1(gspca_dev, 0x0035, 0x000b);
	cit_model3_Packet1(gspca_dev, 0x003f, 0x0001);
	cit_model3_Packet1(gspca_dev, 0x0044, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x0054, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x00c4, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x00e7, 0x0001);
	cit_model3_Packet1(gspca_dev, 0x00e9, 0x0001);
	cit_model3_Packet1(gspca_dev, 0x00ee, 0x0000);
	cit_model3_Packet1(gspca_dev, 0x00f3, 0x00c0);

	cit_write_reg(gspca_dev, compression, 0x0109);
	cit_write_reg(gspca_dev, clock_div, 0x0111);

/*	if (sd->input_index) { */
	if (rca_input) {
		for (i = 0; i < ARRAY_SIZE(rca_initdata); i++) {
			if (rca_initdata[i][0])
				cit_read_reg(gspca_dev, rca_initdata[i][2]);
			else
				cit_write_reg(gspca_dev, rca_initdata[i][1],
					      rca_initdata[i][2]);
		}
	}

	return 0;
}

/* -- start the camera -- */
static int sd_start(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct usb_host_interface *alt;
	struct usb_interface *intf;
	int packet_size;

	switch (sd->model) {
	case CIT_MODEL3:
		cit_start_model3(gspca_dev);
		break;
	case CIT_IBM_NETCAM_PRO:
		cit_start_ibm_netcam_pro(gspca_dev);
		break;
	}

	cit_set_brightness(gspca_dev);
	cit_set_contrast(gspca_dev);
	cit_set_hue(gspca_dev);
	cit_set_sharpness(gspca_dev);

	/* Program max isoc packet size, one day we should use this to
	   allow us to work together with other isoc devices on the same
	   root hub. */
	intf = usb_ifnum_to_if(sd->gspca_dev.dev, sd->gspca_dev.iface);
	alt = usb_altnum_to_altsetting(intf, sd->gspca_dev.alt);
	if (!alt) {
		PDEBUG(D_ERR, "Couldn't get altsetting");
		return -EIO;
	}

	packet_size = le16_to_cpu(alt->endpoint[0].desc.wMaxPacketSize);
	cit_write_reg(gspca_dev, packet_size >> 8, 0x0106);
	cit_write_reg(gspca_dev, packet_size & 0xff, 0x0107);

	cit_restart_stream(gspca_dev);

	return 0;
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	switch (sd->model) {
	case CIT_MODEL3:
	case CIT_IBM_NETCAM_PRO:
		cit_write_reg(gspca_dev, 0x0000, 0x010c);
		break;
	}
}

static void sd_stop0(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	/* We cannot use gspca_dev->present here as that is not set when
	   sd_init gets called and we get called from sd_init */
	if (!gspca_dev->dev)
		return;

	switch (sd->model) {
	case CIT_MODEL3:
		cit_write_reg(gspca_dev, 0x0006, 0x012c);
		cit_model3_Packet1(gspca_dev, 0x0046, 0x0000);
		cit_read_reg(gspca_dev, 0x0116);
		cit_write_reg(gspca_dev, 0x0064, 0x0116);
		cit_read_reg(gspca_dev, 0x0115);
		cit_write_reg(gspca_dev, 0x0003, 0x0115);
		cit_write_reg(gspca_dev, 0x0008, 0x0123);
		cit_write_reg(gspca_dev, 0x0000, 0x0117);
		cit_write_reg(gspca_dev, 0x0000, 0x0112);
		cit_write_reg(gspca_dev, 0x0080, 0x0100);
		break;
	case CIT_IBM_NETCAM_PRO:
		cit_model3_Packet1(gspca_dev, 0x0049, 0x00ff);
		cit_write_reg(gspca_dev, 0x0006, 0x012c);
		cit_write_reg(gspca_dev, 0x0000, 0x0116);
		/* HDG windows does this, but I cannot get the camera
		   to restart with this without redoing the entire init
		   sequence which makes switching modes really slow */
		/* cit_write_reg(gspca_dev, 0x0006, 0x0115); */
		cit_write_reg(gspca_dev, 0x0008, 0x0123);
		cit_write_reg(gspca_dev, 0x0000, 0x0117);
		cit_write_reg(gspca_dev, 0x0003, 0x0133);
		cit_write_reg(gspca_dev, 0x0000, 0x0111);
		/* HDG windows does this, but I get a green picture when
		   restarting the stream after this */
		/* cit_write_reg(gspca_dev, 0x0000, 0x0112); */
		cit_write_reg(gspca_dev, 0x00c0, 0x0100);
		break;
	}
}

static u8 *cit_find_sof(struct gspca_dev *gspca_dev, u8 *data, int len)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int i;

	switch (sd->model) {
	case CIT_MODEL3:
	case CIT_IBM_NETCAM_PRO:
		for (i = 0; i < len; i++) {
			switch (sd->sof_read) {
			case 0:
				if (data[i] == 0x00)
					sd->sof_read++;
				break;
			case 1:
				if (data[i] == 0xff)
					sd->sof_read++;
				else
					sd->sof_read = 0;
				break;
			case 2:
				sd->sof_read = 0;
				if (data[i] != 0xff) {
					if (i >= 4)
						PDEBUG(D_FRAM,
						       "header found at offset: %d: %02x %02x 00 ff %02x %02x\n",
						       i - 2,
						       data[i - 4],
						       data[i - 3],
						       data[i],
						       data[i + 1]);
					return data + i + 2;
				}
				break;
			}
		}
		break;
	}
	return NULL;
}

static void sd_pkt_scan(struct gspca_dev *gspca_dev,
			u8 *data, int len)
{
	unsigned char *sof;

	sof = cit_find_sof(gspca_dev, data, len);
	if (sof) {
		int n;

		/* finish decoding current frame */
		n = sof - data;
		if (n > 4)
			n -= 4;
		else
			n = 0;
		gspca_frame_add(gspca_dev, LAST_PACKET,
				data, n);
		gspca_frame_add(gspca_dev, FIRST_PACKET, NULL, 0);
		len -= sof - data;
		data = sof;
	}

	gspca_frame_add(gspca_dev, INTER_PACKET, data, len);
}

static int sd_setbrightness(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->brightness = val;
	if (gspca_dev->streaming) {
		sd_stopN(gspca_dev);
		cit_set_brightness(gspca_dev);
		cit_restart_stream(gspca_dev);
	}

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
	if (gspca_dev->streaming) {
		sd_stopN(gspca_dev);
		cit_set_contrast(gspca_dev);
		cit_restart_stream(gspca_dev);
	}

	return 0;
}

static int sd_getcontrast(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->contrast;

	return 0;
}

static int sd_sethue(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->hue = val;
	if (gspca_dev->streaming) {
		sd_stopN(gspca_dev);
		cit_set_hue(gspca_dev);
		cit_restart_stream(gspca_dev);
	}
	return 0;
}

static int sd_gethue(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->hue;

	return 0;
}

static int sd_setsharpness(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->sharpness = val;
	if (gspca_dev->streaming) {
		sd_stopN(gspca_dev);
		cit_set_sharpness(gspca_dev);
		cit_restart_stream(gspca_dev);
	}
	return 0;
}

static int sd_getsharpness(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->sharpness;

	return 0;
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
};

/* -- module initialisation -- */
static const __devinitdata struct usb_device_id device_table[] = {
	{ USB_DEVICE_VER(0x0545, 0x8080, 0x0002, 0x0002), .driver_info = CIT_MODEL1 },
	{ USB_DEVICE_VER(0x0545, 0x8080, 0x030a, 0x030a), .driver_info = CIT_MODEL2 },
	{ USB_DEVICE_VER(0x0545, 0x8080, 0x0301, 0x0301), .driver_info = CIT_MODEL3 },
	{ USB_DEVICE_VER(0x0545, 0x8002, 0x030a, 0x030a), .driver_info = CIT_MODEL4 },
	{ USB_DEVICE_VER(0x0545, 0x800c, 0x030a, 0x030a), .driver_info = CIT_MODEL2 },
	{ USB_DEVICE_VER(0x0545, 0x800d, 0x030a, 0x030a), .driver_info = CIT_MODEL4 },
	{}
};
MODULE_DEVICE_TABLE(usb, device_table);

/* -- device connect -- */
static int sd_probe(struct usb_interface *intf,
			const struct usb_device_id *id)
{
	return gspca_dev_probe2(intf, id, &sd_desc, sizeof(struct sd),
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
