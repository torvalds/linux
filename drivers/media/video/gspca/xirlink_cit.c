/*
 * USB IBM C-It Video Camera driver
 *
 * Supports Xirlink C-It Video Camera, IBM PC Camera,
 * IBM NetCamera and Veo Stingray.
 *
 * Copyright (C) 2010 Hans de Goede <hdegoede@redhat.com>
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

#include <linux/input.h>
#include "gspca.h"

MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
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
#define CIT_MODEL0 0 /* bcd version 0.01 cams ie the xvp-500 */
#define CIT_MODEL1 1 /* The model 1 - 4 nomenclature comes from the old */
#define CIT_MODEL2 2 /* ibmcam driver */
#define CIT_MODEL3 3
#define CIT_MODEL4 4
#define CIT_IBM_NETCAM_PRO 5
	u8 input_index;
	u8 button_state;
	u8 stop_on_control_change;
	u8 sof_read;
	u8 sof_len;
	u8 contrast;
	u8 brightness;
	u8 hue;
	u8 sharpness;
	u8 lighting;
	u8 hflip;
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
static int sd_setlighting(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getlighting(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_sethflip(struct gspca_dev *gspca_dev, __s32 val);
static int sd_gethflip(struct gspca_dev *gspca_dev, __s32 *val);
static void sd_stop0(struct gspca_dev *gspca_dev);

static const struct ctrl sd_ctrls[] = {
#define SD_BRIGHTNESS 0
	{
	    {
		.id      = V4L2_CID_BRIGHTNESS,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Brightness",
		.minimum = 0,
		.maximum = 63,
		.step = 1,
#define BRIGHTNESS_DEFAULT 32
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
		.minimum = 0,
		.maximum = 127,
		.step	= 1,
#define HUE_DEFAULT 63
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
#define SD_LIGHTING 4
	{
	    {
		.id = V4L2_CID_BACKLIGHT_COMPENSATION,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Lighting",
		.minimum = 0,
		.maximum = 2,
		.step = 1,
#define LIGHTING_DEFAULT 1
		.default_value = LIGHTING_DEFAULT,
		.flags = 0,
	    },
	    .set = sd_setlighting,
	    .get = sd_getlighting,
	},
#define SD_HFLIP 5
	{
	    {
		.id      = V4L2_CID_HFLIP,
		.type    = V4L2_CTRL_TYPE_BOOLEAN,
		.name    = "Mirror",
		.minimum = 0,
		.maximum = 1,
		.step    = 1,
#define HFLIP_DEFAULT 0
		.default_value = HFLIP_DEFAULT,
	    },
	    .set = sd_sethflip,
	    .get = sd_gethflip,
	},
};

static const struct v4l2_pix_format cif_yuv_mode[] = {
	{176, 144, V4L2_PIX_FMT_CIT_YYVYUY, V4L2_FIELD_NONE,
		.bytesperline = 176,
		.sizeimage = 176 * 144 * 3 / 2 + 4,
		.colorspace = V4L2_COLORSPACE_SRGB},
	{352, 288, V4L2_PIX_FMT_CIT_YYVYUY, V4L2_FIELD_NONE,
		.bytesperline = 352,
		.sizeimage = 352 * 288 * 3 / 2 + 4,
		.colorspace = V4L2_COLORSPACE_SRGB},
};

static const struct v4l2_pix_format vga_yuv_mode[] = {
	{160, 120, V4L2_PIX_FMT_CIT_YYVYUY, V4L2_FIELD_NONE,
		.bytesperline = 160,
		.sizeimage = 160 * 120 * 3 / 2 + 4,
		.colorspace = V4L2_COLORSPACE_SRGB},
	{320, 240, V4L2_PIX_FMT_CIT_YYVYUY, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240 * 3 / 2 + 4,
		.colorspace = V4L2_COLORSPACE_SRGB},
	{640, 480, V4L2_PIX_FMT_CIT_YYVYUY, V4L2_FIELD_NONE,
		.bytesperline = 640,
		.sizeimage = 640 * 480 * 3 / 2 + 4,
		.colorspace = V4L2_COLORSPACE_SRGB},
};

static const struct v4l2_pix_format model0_mode[] = {
	{160, 120, V4L2_PIX_FMT_CIT_YYVYUY, V4L2_FIELD_NONE,
		.bytesperline = 160,
		.sizeimage = 160 * 120 * 3 / 2 + 4,
		.colorspace = V4L2_COLORSPACE_SRGB},
	{176, 144, V4L2_PIX_FMT_CIT_YYVYUY, V4L2_FIELD_NONE,
		.bytesperline = 176,
		.sizeimage = 176 * 144 * 3 / 2 + 4,
		.colorspace = V4L2_COLORSPACE_SRGB},
	{320, 240, V4L2_PIX_FMT_CIT_YYVYUY, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240 * 3 / 2 + 4,
		.colorspace = V4L2_COLORSPACE_SRGB},
};

static const struct v4l2_pix_format model2_mode[] = {
	{160, 120, V4L2_PIX_FMT_CIT_YYVYUY, V4L2_FIELD_NONE,
		.bytesperline = 160,
		.sizeimage = 160 * 120 * 3 / 2 + 4,
		.colorspace = V4L2_COLORSPACE_SRGB},
	{176, 144, V4L2_PIX_FMT_CIT_YYVYUY, V4L2_FIELD_NONE,
		.bytesperline = 176,
		.sizeimage = 176 * 144 * 3 / 2 + 4,
		.colorspace = V4L2_COLORSPACE_SRGB},
	{320, 240, V4L2_PIX_FMT_SGRBG8, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240 + 4,
		.colorspace = V4L2_COLORSPACE_SRGB},
	{352, 288, V4L2_PIX_FMT_SGRBG8, V4L2_FIELD_NONE,
		.bytesperline = 352,
		.sizeimage = 352 * 288 + 4,
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

/* TESTME the old ibmcam driver repeats certain commands to Model1 cameras, we
   do the same for now (testing needed to see if this is really necessary) */
static const int cit_model1_ntries = 5;
static const int cit_model1_ntries2 = 2;

static int cit_write_reg(struct gspca_dev *gspca_dev, u16 value, u16 index)
{
	struct usb_device *udev = gspca_dev->dev;
	int err;

	err = usb_control_msg(udev, usb_sndctrlpipe(udev, 0), 0x00,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_ENDPOINT,
			value, index, NULL, 0, 1000);
	if (err < 0)
		err("Failed to write a register (index 0x%04X,"
			" value 0x%02X, error %d)", index, value, err);

	return 0;
}

static int cit_read_reg(struct gspca_dev *gspca_dev, u16 index, int verbose)
{
	struct usb_device *udev = gspca_dev->dev;
	__u8 *buf = gspca_dev->usb_buf;
	int res;

	res = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0), 0x01,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_ENDPOINT,
			0x00, index, buf, 8, 1000);
	if (res < 0) {
		err("Failed to read a register (index 0x%04X, error %d)",
			index, res);
		return res;
	}

	if (verbose)
		PDEBUG(D_PROBE, "Register %04x value: %02x", index, buf[0]);

	return 0;
}

/*
 * cit_send_FF_04_02()
 *
 * This procedure sends magic 3-command prefix to the camera.
 * The purpose of this prefix is not known.
 *
 * History:
 * 1/2/00   Created.
 */
static void cit_send_FF_04_02(struct gspca_dev *gspca_dev)
{
	cit_write_reg(gspca_dev, 0x00FF, 0x0127);
	cit_write_reg(gspca_dev, 0x0004, 0x0124);
	cit_write_reg(gspca_dev, 0x0002, 0x0124);
}

static void cit_send_00_04_06(struct gspca_dev *gspca_dev)
{
	cit_write_reg(gspca_dev, 0x0000, 0x0127);
	cit_write_reg(gspca_dev, 0x0004, 0x0124);
	cit_write_reg(gspca_dev, 0x0006, 0x0124);
}

static void cit_send_x_00(struct gspca_dev *gspca_dev, unsigned short x)
{
	cit_write_reg(gspca_dev, x,      0x0127);
	cit_write_reg(gspca_dev, 0x0000, 0x0124);
}

static void cit_send_x_00_05(struct gspca_dev *gspca_dev, unsigned short x)
{
	cit_send_x_00(gspca_dev, x);
	cit_write_reg(gspca_dev, 0x0005, 0x0124);
}

static void cit_send_x_00_05_02(struct gspca_dev *gspca_dev, unsigned short x)
{
	cit_write_reg(gspca_dev, x,      0x0127);
	cit_write_reg(gspca_dev, 0x0000, 0x0124);
	cit_write_reg(gspca_dev, 0x0005, 0x0124);
	cit_write_reg(gspca_dev, 0x0002, 0x0124);
}

static void cit_send_x_01_00_05(struct gspca_dev *gspca_dev, u16 x)
{
	cit_write_reg(gspca_dev, x,      0x0127);
	cit_write_reg(gspca_dev, 0x0001, 0x0124);
	cit_write_reg(gspca_dev, 0x0000, 0x0124);
	cit_write_reg(gspca_dev, 0x0005, 0x0124);
}

static void cit_send_x_00_05_02_01(struct gspca_dev *gspca_dev, u16 x)
{
	cit_write_reg(gspca_dev, x,      0x0127);
	cit_write_reg(gspca_dev, 0x0000, 0x0124);
	cit_write_reg(gspca_dev, 0x0005, 0x0124);
	cit_write_reg(gspca_dev, 0x0002, 0x0124);
	cit_write_reg(gspca_dev, 0x0001, 0x0124);
}

static void cit_send_x_00_05_02_08_01(struct gspca_dev *gspca_dev, u16 x)
{
	cit_write_reg(gspca_dev, x,      0x0127);
	cit_write_reg(gspca_dev, 0x0000, 0x0124);
	cit_write_reg(gspca_dev, 0x0005, 0x0124);
	cit_write_reg(gspca_dev, 0x0002, 0x0124);
	cit_write_reg(gspca_dev, 0x0008, 0x0124);
	cit_write_reg(gspca_dev, 0x0001, 0x0124);
}

static void cit_Packet_Format1(struct gspca_dev *gspca_dev, u16 fkey, u16 val)
{
	cit_send_x_01_00_05(gspca_dev, 0x0088);
	cit_send_x_00_05(gspca_dev, fkey);
	cit_send_x_00_05_02_08_01(gspca_dev, val);
	cit_send_x_00_05(gspca_dev, 0x0088);
	cit_send_x_00_05_02_01(gspca_dev, fkey);
	cit_send_x_00_05(gspca_dev, 0x0089);
	cit_send_x_00(gspca_dev, fkey);
	cit_send_00_04_06(gspca_dev);
	cit_read_reg(gspca_dev, 0x0126, 0);
	cit_send_FF_04_02(gspca_dev);
}

static void cit_PacketFormat2(struct gspca_dev *gspca_dev, u16 fkey, u16 val)
{
	cit_send_x_01_00_05(gspca_dev, 0x0088);
	cit_send_x_00_05(gspca_dev, fkey);
	cit_send_x_00_05_02(gspca_dev, val);
}

static void cit_model2_Packet2(struct gspca_dev *gspca_dev)
{
	cit_write_reg(gspca_dev, 0x00ff, 0x012d);
	cit_write_reg(gspca_dev, 0xfea3, 0x0124);
}

static void cit_model2_Packet1(struct gspca_dev *gspca_dev, u16 v1, u16 v2)
{
	cit_write_reg(gspca_dev, 0x00aa, 0x012d);
	cit_write_reg(gspca_dev, 0x00ff, 0x012e);
	cit_write_reg(gspca_dev, v1,     0x012f);
	cit_write_reg(gspca_dev, 0x00ff, 0x0130);
	cit_write_reg(gspca_dev, 0xc719, 0x0124);
	cit_write_reg(gspca_dev, v2,     0x0127);

	cit_model2_Packet2(gspca_dev);
}

/*
 * cit_model3_Packet1()
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

static void cit_model4_Packet1(struct gspca_dev *gspca_dev, u16 v1, u16 v2)
{
	cit_write_reg(gspca_dev, 0x00aa, 0x012d);
	cit_write_reg(gspca_dev, v1,     0x012f);
	cit_write_reg(gspca_dev, 0xd141, 0x0124);
	cit_write_reg(gspca_dev, v2,     0x0127);
	cit_write_reg(gspca_dev, 0xfea8, 0x0124);
}

static void cit_model4_BrightnessPacket(struct gspca_dev *gspca_dev, u16 val)
{
	cit_write_reg(gspca_dev, 0x00aa, 0x012d);
	cit_write_reg(gspca_dev, 0x0026, 0x012f);
	cit_write_reg(gspca_dev, 0xd141, 0x0124);
	cit_write_reg(gspca_dev, val,    0x0127);
	cit_write_reg(gspca_dev, 0x00aa, 0x0130);
	cit_write_reg(gspca_dev, 0x82a8, 0x0124);
	cit_write_reg(gspca_dev, 0x0038, 0x012d);
	cit_write_reg(gspca_dev, 0x0004, 0x012f);
	cit_write_reg(gspca_dev, 0xd145, 0x0124);
	cit_write_reg(gspca_dev, 0xfffa, 0x0124);
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
	case CIT_MODEL0:
		cam->cam_mode = model0_mode;
		cam->nmodes = ARRAY_SIZE(model0_mode);
		cam->reverse_alts = 1;
		gspca_dev->ctrl_dis = ~((1 << SD_CONTRAST) | (1 << SD_HFLIP));
		sd->sof_len = 4;
		break;
	case CIT_MODEL1:
		cam->cam_mode = cif_yuv_mode;
		cam->nmodes = ARRAY_SIZE(cif_yuv_mode);
		cam->reverse_alts = 1;
		gspca_dev->ctrl_dis = (1 << SD_HUE) | (1 << SD_HFLIP);
		sd->sof_len = 4;
		break;
	case CIT_MODEL2:
		cam->cam_mode = model2_mode + 1; /* no 160x120 */
		cam->nmodes = 3;
		gspca_dev->ctrl_dis = (1 << SD_CONTRAST) |
				      (1 << SD_SHARPNESS) |
				      (1 << SD_HFLIP);
		break;
	case CIT_MODEL3:
		cam->cam_mode = vga_yuv_mode;
		cam->nmodes = ARRAY_SIZE(vga_yuv_mode);
		gspca_dev->ctrl_dis = (1 << SD_HUE) |
				      (1 << SD_LIGHTING) |
				      (1 << SD_HFLIP);
		sd->stop_on_control_change = 1;
		sd->sof_len = 4;
		break;
	case CIT_MODEL4:
		cam->cam_mode = model2_mode;
		cam->nmodes = ARRAY_SIZE(model2_mode);
		gspca_dev->ctrl_dis = (1 << SD_CONTRAST) |
				      (1 << SD_SHARPNESS) |
				      (1 << SD_LIGHTING) |
				      (1 << SD_HFLIP);
		break;
	case CIT_IBM_NETCAM_PRO:
		cam->cam_mode = vga_yuv_mode;
		cam->nmodes = 2; /* no 640 x 480 */
		cam->input_flags = V4L2_IN_ST_VFLIP;
		gspca_dev->ctrl_dis = ~(1 << SD_CONTRAST);
		sd->stop_on_control_change = 1;
		sd->sof_len = 4;
		break;
	}

	sd->brightness = BRIGHTNESS_DEFAULT;
	sd->contrast = CONTRAST_DEFAULT;
	sd->hue = HUE_DEFAULT;
	sd->sharpness = SHARPNESS_DEFAULT;
	sd->lighting = LIGHTING_DEFAULT;
	sd->hflip = HFLIP_DEFAULT;

	return 0;
}

static int cit_init_model0(struct gspca_dev *gspca_dev)
{
	cit_write_reg(gspca_dev, 0x0000, 0x0100); /* turn on led */
	cit_write_reg(gspca_dev, 0x0001, 0x0112); /* turn on autogain ? */
	cit_write_reg(gspca_dev, 0x0000, 0x0400);
	cit_write_reg(gspca_dev, 0x0001, 0x0400);
	cit_write_reg(gspca_dev, 0x0000, 0x0420);
	cit_write_reg(gspca_dev, 0x0001, 0x0420);
	cit_write_reg(gspca_dev, 0x000d, 0x0409);
	cit_write_reg(gspca_dev, 0x0002, 0x040a);
	cit_write_reg(gspca_dev, 0x0018, 0x0405);
	cit_write_reg(gspca_dev, 0x0008, 0x0435);
	cit_write_reg(gspca_dev, 0x0026, 0x040b);
	cit_write_reg(gspca_dev, 0x0007, 0x0437);
	cit_write_reg(gspca_dev, 0x0015, 0x042f);
	cit_write_reg(gspca_dev, 0x002b, 0x0439);
	cit_write_reg(gspca_dev, 0x0026, 0x043a);
	cit_write_reg(gspca_dev, 0x0008, 0x0438);
	cit_write_reg(gspca_dev, 0x001e, 0x042b);
	cit_write_reg(gspca_dev, 0x0041, 0x042c);

	return 0;
}

static int cit_init_ibm_netcam_pro(struct gspca_dev *gspca_dev)
{
	cit_read_reg(gspca_dev, 0x128, 1);
	cit_write_reg(gspca_dev, 0x0003, 0x0133);
	cit_write_reg(gspca_dev, 0x0000, 0x0117);
	cit_write_reg(gspca_dev, 0x0008, 0x0123);
	cit_write_reg(gspca_dev, 0x0000, 0x0100);
	cit_read_reg(gspca_dev, 0x0116, 0);
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
	cit_read_reg(gspca_dev, 0x0126, 1);

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
	case CIT_MODEL0:
		cit_init_model0(gspca_dev);
		sd_stop0(gspca_dev);
		break;
	case CIT_MODEL1:
	case CIT_MODEL2:
	case CIT_MODEL3:
	case CIT_MODEL4:
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
	int i;

	switch (sd->model) {
	case CIT_MODEL0:
	case CIT_IBM_NETCAM_PRO:
		/* No (known) brightness control for these */
		break;
	case CIT_MODEL1:
		/* Model 1: Brightness range 0 - 63 */
		cit_Packet_Format1(gspca_dev, 0x0031, sd->brightness);
		cit_Packet_Format1(gspca_dev, 0x0032, sd->brightness);
		cit_Packet_Format1(gspca_dev, 0x0033, sd->brightness);
		break;
	case CIT_MODEL2:
		/* Model 2: Brightness range 0x60 - 0xee */
		/* Scale 0 - 63 to 0x60 - 0xee */
		i = 0x60 + sd->brightness * 2254 / 1000;
		cit_model2_Packet1(gspca_dev, 0x001a, i);
		break;
	case CIT_MODEL3:
		/* Model 3: Brightness range 'i' in [0x0C..0x3F] */
		i = sd->brightness;
		if (i < 0x0c)
			i = 0x0c;
		cit_model3_Packet1(gspca_dev, 0x0036, i);
		break;
	case CIT_MODEL4:
		/* Model 4: Brightness range 'i' in [0x04..0xb4] */
		/* Scale 0 - 63 to 0x04 - 0xb4 */
		i = 0x04 + sd->brightness * 2794 / 1000;
		cit_model4_BrightnessPacket(gspca_dev, i);
		break;
	}

	return 0;
}

static int cit_set_contrast(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	switch (sd->model) {
	case CIT_MODEL0: {
		int i;
		/* gain 0-15, 0-20 -> 0-15 */
		i = sd->contrast * 1000 / 1333;
		cit_write_reg(gspca_dev, i, 0x0422);
		/* gain 0-31, may not be lower then 0x0422, 0-20 -> 0-31 */
		i = sd->contrast * 2000 / 1333;
		cit_write_reg(gspca_dev, i, 0x0423);
		/* gain 0-127, may not be lower then 0x0423, 0-20 -> 0-63  */
		i = sd->contrast * 4000 / 1333;
		cit_write_reg(gspca_dev, i, 0x0424);
		/* gain 0-127, may not be lower then 0x0424, , 0-20 -> 0-127 */
		i = sd->contrast * 8000 / 1333;
		cit_write_reg(gspca_dev, i, 0x0425);
		break;
	}
	case CIT_MODEL2:
	case CIT_MODEL4:
		/* These models do not have this control. */
		break;
	case CIT_MODEL1:
	{
		/* Scale 0 - 20 to 15 - 0 */
		int i, new_contrast = (20 - sd->contrast) * 1000 / 1333;
		for (i = 0; i < cit_model1_ntries; i++) {
			cit_Packet_Format1(gspca_dev, 0x0014, new_contrast);
			cit_send_FF_04_02(gspca_dev);
		}
		break;
	}
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
	case CIT_MODEL0:
	case CIT_MODEL1:
	case CIT_IBM_NETCAM_PRO:
		/* No hue control for these models */
		break;
	case CIT_MODEL2:
		cit_model2_Packet1(gspca_dev, 0x0024, sd->hue);
		/* cit_model2_Packet1(gspca_dev, 0x0020, sat); */
		break;
	case CIT_MODEL3: {
		/* Model 3: Brightness range 'i' in [0x05..0x37] */
		/* TESTME according to the ibmcam driver this does not work */
		if (0) {
			/* Scale 0 - 127 to 0x05 - 0x37 */
			int i = 0x05 + sd->hue * 1000 / 2540;
			cit_model3_Packet1(gspca_dev, 0x007e, i);
		}
		break;
	}
	case CIT_MODEL4:
		/* HDG: taken from ibmcam, setting the color gains does not
		 * really belong here.
		 *
		 * I am not sure r/g/b_gain variables exactly control gain
		 * of those channels. Most likely they subtly change some
		 * very internal image processing settings in the camera.
		 * In any case, here is what they do, and feel free to tweak:
		 *
		 * r_gain: seriously affects red gain
		 * g_gain: seriously affects green gain
		 * b_gain: seriously affects blue gain
		 * hue: changes average color from violet (0) to red (0xFF)
		 */
		cit_write_reg(gspca_dev, 0x00aa, 0x012d);
		cit_write_reg(gspca_dev, 0x001e, 0x012f);
		cit_write_reg(gspca_dev, 0xd141, 0x0124);
		cit_write_reg(gspca_dev,    160, 0x0127);  /* Green gain */
		cit_write_reg(gspca_dev,    160, 0x012e);  /* Red gain */
		cit_write_reg(gspca_dev,    160, 0x0130);  /* Blue gain */
		cit_write_reg(gspca_dev, 0x8a28, 0x0124);
		cit_write_reg(gspca_dev, sd->hue, 0x012d); /* Hue */
		cit_write_reg(gspca_dev, 0xf545, 0x0124);
		break;
	}
	return 0;
}

static int cit_set_sharpness(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	switch (sd->model) {
	case CIT_MODEL0:
	case CIT_MODEL2:
	case CIT_MODEL4:
	case CIT_IBM_NETCAM_PRO:
		/* These models do not have this control */
		break;
	case CIT_MODEL1: {
		int i;
		const unsigned short sa[] = {
			0x11, 0x13, 0x16, 0x18, 0x1a, 0x8, 0x0a };

		for (i = 0; i < cit_model1_ntries; i++)
			cit_PacketFormat2(gspca_dev, 0x0013, sa[sd->sharpness]);
		break;
	}
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
	}
	return 0;
}

/*
 * cit_set_lighting()
 *
 * Camera model 1:
 * We have 3 levels of lighting conditions: 0=Bright, 1=Medium, 2=Low.
 *
 * Camera model 2:
 * We have 16 levels of lighting, 0 for bright light and up to 15 for
 * low light. But values above 5 or so are useless because camera is
 * not really capable to produce anything worth viewing at such light.
 * This setting may be altered only in certain camera state.
 *
 * Low lighting forces slower FPS.
 *
 * History:
 * 1/5/00   Created.
 * 2/20/00  Added support for Model 2 cameras.
 */
static void cit_set_lighting(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	switch (sd->model) {
	case CIT_MODEL0:
	case CIT_MODEL2:
	case CIT_MODEL3:
	case CIT_MODEL4:
	case CIT_IBM_NETCAM_PRO:
		break;
	case CIT_MODEL1: {
		int i;
		for (i = 0; i < cit_model1_ntries; i++)
			cit_Packet_Format1(gspca_dev, 0x0027, sd->lighting);
		break;
	}
	}
}

static void cit_set_hflip(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	switch (sd->model) {
	case CIT_MODEL0:
		if (sd->hflip)
			cit_write_reg(gspca_dev, 0x0020, 0x0115);
		else
			cit_write_reg(gspca_dev, 0x0040, 0x0115);
		break;
	case CIT_MODEL1:
	case CIT_MODEL2:
	case CIT_MODEL3:
	case CIT_MODEL4:
	case CIT_IBM_NETCAM_PRO:
		break;
	}
}

static int cit_restart_stream(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	switch (sd->model) {
	case CIT_MODEL0:
	case CIT_MODEL1:
		cit_write_reg(gspca_dev, 0x0001, 0x0114);
		/* Fall through */
	case CIT_MODEL2:
	case CIT_MODEL4:
		cit_write_reg(gspca_dev, 0x00c0, 0x010c); /* Go! */
		usb_clear_halt(gspca_dev->dev, gspca_dev->urb[0]->pipe);
		break;
	case CIT_MODEL3:
	case CIT_IBM_NETCAM_PRO:
		cit_write_reg(gspca_dev, 0x0001, 0x0114);
		cit_write_reg(gspca_dev, 0x00c0, 0x010c); /* Go! */
		usb_clear_halt(gspca_dev->dev, gspca_dev->urb[0]->pipe);
		/* Clear button events from while we were not streaming */
		cit_write_reg(gspca_dev, 0x0001, 0x0113);
		break;
	}

	sd->sof_read = 0;

	return 0;
}

static int cit_get_packet_size(struct gspca_dev *gspca_dev)
{
	struct usb_host_interface *alt;
	struct usb_interface *intf;

	intf = usb_ifnum_to_if(gspca_dev->dev, gspca_dev->iface);
	alt = usb_altnum_to_altsetting(intf, gspca_dev->alt);
	if (!alt) {
		err("Couldn't get altsetting");
		return -EIO;
	}

	return le16_to_cpu(alt->endpoint[0].desc.wMaxPacketSize);
}

/* Calculate the clockdiv giving us max fps given the available bandwidth */
static int cit_get_clock_div(struct gspca_dev *gspca_dev)
{
	int clock_div = 7; /* 0=30 1=25 2=20 3=15 4=12 5=7.5 6=6 7=3fps ?? */
	int fps[8] = { 30, 25, 20, 15, 12, 8, 6, 3 };
	int packet_size;

	packet_size = cit_get_packet_size(gspca_dev);
	if (packet_size < 0)
		return packet_size;

	while (clock_div > 3 &&
			1000 * packet_size >
			gspca_dev->width * gspca_dev->height *
			fps[clock_div - 1] * 3 / 2)
		clock_div--;

	PDEBUG(D_PROBE,
	       "PacketSize: %d, res: %dx%d -> using clockdiv: %d (%d fps)",
	       packet_size, gspca_dev->width, gspca_dev->height, clock_div,
	       fps[clock_div]);

	return clock_div;
}

static int cit_start_model0(struct gspca_dev *gspca_dev)
{
	const unsigned short compression = 0; /* 0=none, 7=best frame rate */
	int clock_div;

	clock_div = cit_get_clock_div(gspca_dev);
	if (clock_div < 0)
		return clock_div;

	cit_write_reg(gspca_dev, 0x0000, 0x0100); /* turn on led */
	cit_write_reg(gspca_dev, 0x0003, 0x0438);
	cit_write_reg(gspca_dev, 0x001e, 0x042b);
	cit_write_reg(gspca_dev, 0x0041, 0x042c);
	cit_write_reg(gspca_dev, 0x0008, 0x0436);
	cit_write_reg(gspca_dev, 0x0024, 0x0403);
	cit_write_reg(gspca_dev, 0x002c, 0x0404);
	cit_write_reg(gspca_dev, 0x0002, 0x0426);
	cit_write_reg(gspca_dev, 0x0014, 0x0427);

	switch (gspca_dev->width) {
	case 160: /* 160x120 */
		cit_write_reg(gspca_dev, 0x0004, 0x010b);
		cit_write_reg(gspca_dev, 0x0001, 0x010a);
		cit_write_reg(gspca_dev, 0x0010, 0x0102);
		cit_write_reg(gspca_dev, 0x00a0, 0x0103);
		cit_write_reg(gspca_dev, 0x0000, 0x0104);
		cit_write_reg(gspca_dev, 0x0078, 0x0105);
		break;

	case 176: /* 176x144 */
		cit_write_reg(gspca_dev, 0x0006, 0x010b);
		cit_write_reg(gspca_dev, 0x0000, 0x010a);
		cit_write_reg(gspca_dev, 0x0005, 0x0102);
		cit_write_reg(gspca_dev, 0x00b0, 0x0103);
		cit_write_reg(gspca_dev, 0x0000, 0x0104);
		cit_write_reg(gspca_dev, 0x0090, 0x0105);
		break;

	case 320: /* 320x240 */
		cit_write_reg(gspca_dev, 0x0008, 0x010b);
		cit_write_reg(gspca_dev, 0x0004, 0x010a);
		cit_write_reg(gspca_dev, 0x0005, 0x0102);
		cit_write_reg(gspca_dev, 0x00a0, 0x0103);
		cit_write_reg(gspca_dev, 0x0010, 0x0104);
		cit_write_reg(gspca_dev, 0x0078, 0x0105);
		break;
	}

	cit_write_reg(gspca_dev, compression, 0x0109);
	cit_write_reg(gspca_dev, clock_div, 0x0111);

	return 0;
}

static int cit_start_model1(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int i, clock_div;

	clock_div = cit_get_clock_div(gspca_dev);
	if (clock_div < 0)
		return clock_div;

	cit_read_reg(gspca_dev, 0x0128, 1);
	cit_read_reg(gspca_dev, 0x0100, 0);
	cit_write_reg(gspca_dev, 0x01, 0x0100);	/* LED On  */
	cit_read_reg(gspca_dev, 0x0100, 0);
	cit_write_reg(gspca_dev, 0x81, 0x0100);	/* LED Off */
	cit_read_reg(gspca_dev, 0x0100, 0);
	cit_write_reg(gspca_dev, 0x01, 0x0100);	/* LED On  */
	cit_write_reg(gspca_dev, 0x01, 0x0108);

	cit_write_reg(gspca_dev, 0x03, 0x0112);
	cit_read_reg(gspca_dev, 0x0115, 0);
	cit_write_reg(gspca_dev, 0x06, 0x0115);
	cit_read_reg(gspca_dev, 0x0116, 0);
	cit_write_reg(gspca_dev, 0x44, 0x0116);
	cit_read_reg(gspca_dev, 0x0116, 0);
	cit_write_reg(gspca_dev, 0x40, 0x0116);
	cit_read_reg(gspca_dev, 0x0115, 0);
	cit_write_reg(gspca_dev, 0x0e, 0x0115);
	cit_write_reg(gspca_dev, 0x19, 0x012c);

	cit_Packet_Format1(gspca_dev, 0x00, 0x1e);
	cit_Packet_Format1(gspca_dev, 0x39, 0x0d);
	cit_Packet_Format1(gspca_dev, 0x39, 0x09);
	cit_Packet_Format1(gspca_dev, 0x3b, 0x00);
	cit_Packet_Format1(gspca_dev, 0x28, 0x22);
	cit_Packet_Format1(gspca_dev, 0x27, 0x00);
	cit_Packet_Format1(gspca_dev, 0x2b, 0x1f);
	cit_Packet_Format1(gspca_dev, 0x39, 0x08);

	for (i = 0; i < cit_model1_ntries; i++)
		cit_Packet_Format1(gspca_dev, 0x2c, 0x00);

	for (i = 0; i < cit_model1_ntries; i++)
		cit_Packet_Format1(gspca_dev, 0x30, 0x14);

	cit_PacketFormat2(gspca_dev, 0x39, 0x02);
	cit_PacketFormat2(gspca_dev, 0x01, 0xe1);
	cit_PacketFormat2(gspca_dev, 0x02, 0xcd);
	cit_PacketFormat2(gspca_dev, 0x03, 0xcd);
	cit_PacketFormat2(gspca_dev, 0x04, 0xfa);
	cit_PacketFormat2(gspca_dev, 0x3f, 0xff);
	cit_PacketFormat2(gspca_dev, 0x39, 0x00);

	cit_PacketFormat2(gspca_dev, 0x39, 0x02);
	cit_PacketFormat2(gspca_dev, 0x0a, 0x37);
	cit_PacketFormat2(gspca_dev, 0x0b, 0xb8);
	cit_PacketFormat2(gspca_dev, 0x0c, 0xf3);
	cit_PacketFormat2(gspca_dev, 0x0d, 0xe3);
	cit_PacketFormat2(gspca_dev, 0x0e, 0x0d);
	cit_PacketFormat2(gspca_dev, 0x0f, 0xf2);
	cit_PacketFormat2(gspca_dev, 0x10, 0xd5);
	cit_PacketFormat2(gspca_dev, 0x11, 0xba);
	cit_PacketFormat2(gspca_dev, 0x12, 0x53);
	cit_PacketFormat2(gspca_dev, 0x3f, 0xff);
	cit_PacketFormat2(gspca_dev, 0x39, 0x00);

	cit_PacketFormat2(gspca_dev, 0x39, 0x02);
	cit_PacketFormat2(gspca_dev, 0x16, 0x00);
	cit_PacketFormat2(gspca_dev, 0x17, 0x28);
	cit_PacketFormat2(gspca_dev, 0x18, 0x7d);
	cit_PacketFormat2(gspca_dev, 0x19, 0xbe);
	cit_PacketFormat2(gspca_dev, 0x3f, 0xff);
	cit_PacketFormat2(gspca_dev, 0x39, 0x00);

	for (i = 0; i < cit_model1_ntries; i++)
		cit_Packet_Format1(gspca_dev, 0x00, 0x18);
	for (i = 0; i < cit_model1_ntries; i++)
		cit_Packet_Format1(gspca_dev, 0x13, 0x18);
	for (i = 0; i < cit_model1_ntries; i++)
		cit_Packet_Format1(gspca_dev, 0x14, 0x06);

	/* TESTME These are handled through controls
	   KEEP until someone can test leaving this out is ok */
	if (0) {
		/* This is default brightness */
		for (i = 0; i < cit_model1_ntries; i++)
			cit_Packet_Format1(gspca_dev, 0x31, 0x37);
		for (i = 0; i < cit_model1_ntries; i++)
			cit_Packet_Format1(gspca_dev, 0x32, 0x46);
		for (i = 0; i < cit_model1_ntries; i++)
			cit_Packet_Format1(gspca_dev, 0x33, 0x55);
	}

	cit_Packet_Format1(gspca_dev, 0x2e, 0x04);
	for (i = 0; i < cit_model1_ntries; i++)
		cit_Packet_Format1(gspca_dev, 0x2d, 0x04);
	for (i = 0; i < cit_model1_ntries; i++)
		cit_Packet_Format1(gspca_dev, 0x29, 0x80);
	cit_Packet_Format1(gspca_dev, 0x2c, 0x01);
	cit_Packet_Format1(gspca_dev, 0x30, 0x17);
	cit_Packet_Format1(gspca_dev, 0x39, 0x08);
	for (i = 0; i < cit_model1_ntries; i++)
		cit_Packet_Format1(gspca_dev, 0x34, 0x00);

	cit_write_reg(gspca_dev, 0x00, 0x0101);
	cit_write_reg(gspca_dev, 0x00, 0x010a);

	switch (gspca_dev->width) {
	case 128: /* 128x96 */
		cit_write_reg(gspca_dev, 0x80, 0x0103);
		cit_write_reg(gspca_dev, 0x60, 0x0105);
		cit_write_reg(gspca_dev, 0x0c, 0x010b);
		cit_write_reg(gspca_dev, 0x04, 0x011b);	/* Same everywhere */
		cit_write_reg(gspca_dev, 0x0b, 0x011d);
		cit_write_reg(gspca_dev, 0x00, 0x011e);	/* Same everywhere */
		cit_write_reg(gspca_dev, 0x00, 0x0129);
		break;
	case 176: /* 176x144 */
		cit_write_reg(gspca_dev, 0xb0, 0x0103);
		cit_write_reg(gspca_dev, 0x8f, 0x0105);
		cit_write_reg(gspca_dev, 0x06, 0x010b);
		cit_write_reg(gspca_dev, 0x04, 0x011b);	/* Same everywhere */
		cit_write_reg(gspca_dev, 0x0d, 0x011d);
		cit_write_reg(gspca_dev, 0x00, 0x011e);	/* Same everywhere */
		cit_write_reg(gspca_dev, 0x03, 0x0129);
		break;
	case 352: /* 352x288 */
		cit_write_reg(gspca_dev, 0xb0, 0x0103);
		cit_write_reg(gspca_dev, 0x90, 0x0105);
		cit_write_reg(gspca_dev, 0x02, 0x010b);
		cit_write_reg(gspca_dev, 0x04, 0x011b);	/* Same everywhere */
		cit_write_reg(gspca_dev, 0x05, 0x011d);
		cit_write_reg(gspca_dev, 0x00, 0x011e);	/* Same everywhere */
		cit_write_reg(gspca_dev, 0x00, 0x0129);
		break;
	}

	cit_write_reg(gspca_dev, 0xff, 0x012b);

	/* TESTME These are handled through controls
	   KEEP until someone can test leaving this out is ok */
	if (0) {
		/* This is another brightness - don't know why */
		for (i = 0; i < cit_model1_ntries; i++)
			cit_Packet_Format1(gspca_dev, 0x31, 0xc3);
		for (i = 0; i < cit_model1_ntries; i++)
			cit_Packet_Format1(gspca_dev, 0x32, 0xd2);
		for (i = 0; i < cit_model1_ntries; i++)
			cit_Packet_Format1(gspca_dev, 0x33, 0xe1);

		/* Default contrast */
		for (i = 0; i < cit_model1_ntries; i++)
			cit_Packet_Format1(gspca_dev, 0x14, 0x0a);

		/* Default sharpness */
		for (i = 0; i < cit_model1_ntries2; i++)
			cit_PacketFormat2(gspca_dev, 0x13, 0x1a);

		/* Default lighting conditions */
		cit_Packet_Format1(gspca_dev, 0x0027, sd->lighting);
	}

	/* Assorted init */
	switch (gspca_dev->width) {
	case 128: /* 128x96 */
		cit_Packet_Format1(gspca_dev, 0x2b, 0x1e);
		cit_write_reg(gspca_dev, 0xc9, 0x0119);	/* Same everywhere */
		cit_write_reg(gspca_dev, 0x80, 0x0109);	/* Same everywhere */
		cit_write_reg(gspca_dev, 0x36, 0x0102);
		cit_write_reg(gspca_dev, 0x1a, 0x0104);
		cit_write_reg(gspca_dev, 0x04, 0x011a);	/* Same everywhere */
		cit_write_reg(gspca_dev, 0x2b, 0x011c);
		cit_write_reg(gspca_dev, 0x23, 0x012a);	/* Same everywhere */
		break;
	case 176: /* 176x144 */
		cit_Packet_Format1(gspca_dev, 0x2b, 0x1e);
		cit_write_reg(gspca_dev, 0xc9, 0x0119);	/* Same everywhere */
		cit_write_reg(gspca_dev, 0x80, 0x0109);	/* Same everywhere */
		cit_write_reg(gspca_dev, 0x04, 0x0102);
		cit_write_reg(gspca_dev, 0x02, 0x0104);
		cit_write_reg(gspca_dev, 0x04, 0x011a);	/* Same everywhere */
		cit_write_reg(gspca_dev, 0x2b, 0x011c);
		cit_write_reg(gspca_dev, 0x23, 0x012a);	/* Same everywhere */
		break;
	case 352: /* 352x288 */
		cit_Packet_Format1(gspca_dev, 0x2b, 0x1f);
		cit_write_reg(gspca_dev, 0xc9, 0x0119);	/* Same everywhere */
		cit_write_reg(gspca_dev, 0x80, 0x0109);	/* Same everywhere */
		cit_write_reg(gspca_dev, 0x08, 0x0102);
		cit_write_reg(gspca_dev, 0x01, 0x0104);
		cit_write_reg(gspca_dev, 0x04, 0x011a);	/* Same everywhere */
		cit_write_reg(gspca_dev, 0x2f, 0x011c);
		cit_write_reg(gspca_dev, 0x23, 0x012a);	/* Same everywhere */
		break;
	}

	cit_write_reg(gspca_dev, 0x01, 0x0100);	/* LED On  */
	cit_write_reg(gspca_dev, clock_div, 0x0111);

	return 0;
}

static int cit_start_model2(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int clock_div = 0;

	cit_write_reg(gspca_dev, 0x0000, 0x0100);	/* LED on */
	cit_read_reg(gspca_dev, 0x0116, 0);
	cit_write_reg(gspca_dev, 0x0060, 0x0116);
	cit_write_reg(gspca_dev, 0x0002, 0x0112);
	cit_write_reg(gspca_dev, 0x00bc, 0x012c);
	cit_write_reg(gspca_dev, 0x0008, 0x012b);
	cit_write_reg(gspca_dev, 0x0000, 0x0108);
	cit_write_reg(gspca_dev, 0x0001, 0x0133);
	cit_write_reg(gspca_dev, 0x0001, 0x0102);
	switch (gspca_dev->width) {
	case 176: /* 176x144 */
		cit_write_reg(gspca_dev, 0x002c, 0x0103);	/* All except 320x240 */
		cit_write_reg(gspca_dev, 0x0000, 0x0104);	/* Same */
		cit_write_reg(gspca_dev, 0x0024, 0x0105);	/* 176x144, 352x288 */
		cit_write_reg(gspca_dev, 0x00b9, 0x010a);	/* Unique to this mode */
		cit_write_reg(gspca_dev, 0x0038, 0x0119);	/* Unique to this mode */
		/* TESTME HDG: this does not seem right
		   (it is 2 for all other resolutions) */
		sd->sof_len = 10;
		break;
	case 320: /* 320x240 */
		cit_write_reg(gspca_dev, 0x0028, 0x0103);	/* Unique to this mode */
		cit_write_reg(gspca_dev, 0x0000, 0x0104);	/* Same */
		cit_write_reg(gspca_dev, 0x001e, 0x0105);	/* 320x240, 352x240 */
		cit_write_reg(gspca_dev, 0x0039, 0x010a);	/* All except 176x144 */
		cit_write_reg(gspca_dev, 0x0070, 0x0119);	/* All except 176x144 */
		sd->sof_len = 2;
		break;
	/* case VIDEOSIZE_352x240: */
		cit_write_reg(gspca_dev, 0x002c, 0x0103);	/* All except 320x240 */
		cit_write_reg(gspca_dev, 0x0000, 0x0104);	/* Same */
		cit_write_reg(gspca_dev, 0x001e, 0x0105);	/* 320x240, 352x240 */
		cit_write_reg(gspca_dev, 0x0039, 0x010a);	/* All except 176x144 */
		cit_write_reg(gspca_dev, 0x0070, 0x0119);	/* All except 176x144 */
		sd->sof_len = 2;
		break;
	case 352: /* 352x288 */
		cit_write_reg(gspca_dev, 0x002c, 0x0103);	/* All except 320x240 */
		cit_write_reg(gspca_dev, 0x0000, 0x0104);	/* Same */
		cit_write_reg(gspca_dev, 0x0024, 0x0105);	/* 176x144, 352x288 */
		cit_write_reg(gspca_dev, 0x0039, 0x010a);	/* All except 176x144 */
		cit_write_reg(gspca_dev, 0x0070, 0x0119);	/* All except 176x144 */
		sd->sof_len = 2;
		break;
	}

	cit_write_reg(gspca_dev, 0x0000, 0x0100);	/* LED on */

	switch (gspca_dev->width) {
	case 176: /* 176x144 */
		cit_write_reg(gspca_dev, 0x0050, 0x0111);
		cit_write_reg(gspca_dev, 0x00d0, 0x0111);
		break;
	case 320: /* 320x240 */
	case 352: /* 352x288 */
		cit_write_reg(gspca_dev, 0x0040, 0x0111);
		cit_write_reg(gspca_dev, 0x00c0, 0x0111);
		break;
	}
	cit_write_reg(gspca_dev, 0x009b, 0x010f);
	cit_write_reg(gspca_dev, 0x00bb, 0x010f);

	/*
	 * Hardware settings, may affect CMOS sensor; not user controls!
	 * -------------------------------------------------------------
	 * 0x0004: no effect
	 * 0x0006: hardware effect
	 * 0x0008: no effect
	 * 0x000a: stops video stream, probably important h/w setting
	 * 0x000c: changes color in hardware manner (not user setting)
	 * 0x0012: changes number of colors (does not affect speed)
	 * 0x002a: no effect
	 * 0x002c: hardware setting (related to scan lines)
	 * 0x002e: stops video stream, probably important h/w setting
	 */
	cit_model2_Packet1(gspca_dev, 0x000a, 0x005c);
	cit_model2_Packet1(gspca_dev, 0x0004, 0x0000);
	cit_model2_Packet1(gspca_dev, 0x0006, 0x00fb);
	cit_model2_Packet1(gspca_dev, 0x0008, 0x0000);
	cit_model2_Packet1(gspca_dev, 0x000c, 0x0009);
	cit_model2_Packet1(gspca_dev, 0x0012, 0x000a);
	cit_model2_Packet1(gspca_dev, 0x002a, 0x0000);
	cit_model2_Packet1(gspca_dev, 0x002c, 0x0000);
	cit_model2_Packet1(gspca_dev, 0x002e, 0x0008);

	/*
	 * Function 0x0030 pops up all over the place. Apparently
	 * it is a hardware control register, with every bit assigned to
	 * do something.
	 */
	cit_model2_Packet1(gspca_dev, 0x0030, 0x0000);

	/*
	 * Magic control of CMOS sensor. Only lower values like
	 * 0-3 work, and picture shifts left or right. Don't change.
	 */
	switch (gspca_dev->width) {
	case 176: /* 176x144 */
		cit_model2_Packet1(gspca_dev, 0x0014, 0x0002);
		cit_model2_Packet1(gspca_dev, 0x0016, 0x0002); /* Horizontal shift */
		cit_model2_Packet1(gspca_dev, 0x0018, 0x004a); /* Another hardware setting */
		clock_div = 6;
		break;
	case 320: /* 320x240 */
		cit_model2_Packet1(gspca_dev, 0x0014, 0x0009);
		cit_model2_Packet1(gspca_dev, 0x0016, 0x0005); /* Horizontal shift */
		cit_model2_Packet1(gspca_dev, 0x0018, 0x0044); /* Another hardware setting */
		clock_div = 8;
		break;
	/* case VIDEOSIZE_352x240: */
		/* This mode doesn't work as Windows programs it; changed to work */
		cit_model2_Packet1(gspca_dev, 0x0014, 0x0009); /* Windows sets this to 8 */
		cit_model2_Packet1(gspca_dev, 0x0016, 0x0003); /* Horizontal shift */
		cit_model2_Packet1(gspca_dev, 0x0018, 0x0044); /* Windows sets this to 0x0045 */
		clock_div = 10;
		break;
	case 352: /* 352x288 */
		cit_model2_Packet1(gspca_dev, 0x0014, 0x0003);
		cit_model2_Packet1(gspca_dev, 0x0016, 0x0002); /* Horizontal shift */
		cit_model2_Packet1(gspca_dev, 0x0018, 0x004a); /* Another hardware setting */
		clock_div = 16;
		break;
	}

	/* TESTME These are handled through controls
	   KEEP until someone can test leaving this out is ok */
	if (0)
		cit_model2_Packet1(gspca_dev, 0x001a, 0x005a);

	/*
	 * We have our own frame rate setting varying from 0 (slowest) to 6
	 * (fastest). The camera model 2 allows frame rate in range [0..0x1F]
	 # where 0 is also the slowest setting. However for all practical
	 # reasons high settings make no sense because USB is not fast enough
	 # to support high FPS. Be aware that the picture datastream will be
	 # severely disrupted if you ask for frame rate faster than allowed
	 # for the video size - see below:
	 *
	 * Allowable ranges (obtained experimentally on OHCI, K6-3, 450 MHz):
	 * -----------------------------------------------------------------
	 * 176x144: [6..31]
	 * 320x240: [8..31]
	 * 352x240: [10..31]
	 * 352x288: [16..31] I have to raise lower threshold for stability...
	 *
	 * As usual, slower FPS provides better sensitivity.
	 */
	cit_model2_Packet1(gspca_dev, 0x001c, clock_div);

	/*
	 * This setting does not visibly affect pictures; left it here
	 * because it was present in Windows USB data stream. This function
	 * does not allow arbitrary values and apparently is a bit mask, to
	 * be activated only at appropriate time. Don't change it randomly!
	 */
	switch (gspca_dev->width) {
	case 176: /* 176x144 */
		cit_model2_Packet1(gspca_dev, 0x0026, 0x00c2);
		break;
	case 320: /* 320x240 */
		cit_model2_Packet1(gspca_dev, 0x0026, 0x0044);
		break;
	/* case VIDEOSIZE_352x240: */
		cit_model2_Packet1(gspca_dev, 0x0026, 0x0046);
		break;
	case 352: /* 352x288 */
		cit_model2_Packet1(gspca_dev, 0x0026, 0x0048);
		break;
	}

	/* FIXME this cannot be changed while streaming, so we
	   should report a grabbed flag for this control. */
	cit_model2_Packet1(gspca_dev, 0x0028, sd->lighting);
	/* color balance rg2 */
	cit_model2_Packet1(gspca_dev, 0x001e, 0x002f);
	/* saturation */
	cit_model2_Packet1(gspca_dev, 0x0020, 0x0034);
	/* color balance yb */
	cit_model2_Packet1(gspca_dev, 0x0022, 0x00a0);

	/* Hardware control command */
	cit_model2_Packet1(gspca_dev, 0x0030, 0x0004);

	return 0;
}

static int cit_start_model3(struct gspca_dev *gspca_dev)
{
	const unsigned short compression = 0; /* 0=none, 7=best frame rate */
	int i, clock_div = 0;

	/* HDG not in ibmcam driver, added to see if it helps with
	   auto-detecting between model3 and ibm netcamera pro */
	cit_read_reg(gspca_dev, 0x128, 1);

	cit_write_reg(gspca_dev, 0x0000, 0x0100);
	cit_read_reg(gspca_dev, 0x0116, 0);
	cit_write_reg(gspca_dev, 0x0060, 0x0116);
	cit_write_reg(gspca_dev, 0x0002, 0x0112);
	cit_write_reg(gspca_dev, 0x0000, 0x0123);
	cit_write_reg(gspca_dev, 0x0001, 0x0117);
	cit_write_reg(gspca_dev, 0x0040, 0x0108);
	cit_write_reg(gspca_dev, 0x0019, 0x012c);
	cit_write_reg(gspca_dev, 0x0060, 0x0116);
	cit_write_reg(gspca_dev, 0x0002, 0x0115);
	cit_write_reg(gspca_dev, 0x0003, 0x0115);
	cit_read_reg(gspca_dev, 0x0115, 0);
	cit_write_reg(gspca_dev, 0x000b, 0x0115);

	/* TESTME HDG not in ibmcam driver, added to see if it helps with
	   auto-detecting between model3 and ibm netcamera pro */
	if (0) {
		cit_write_reg(gspca_dev, 0x0078, 0x012d);
		cit_write_reg(gspca_dev, 0x0001, 0x012f);
		cit_write_reg(gspca_dev, 0xd141, 0x0124);
		cit_write_reg(gspca_dev, 0x0079, 0x012d);
		cit_write_reg(gspca_dev, 0x00ff, 0x0130);
		cit_write_reg(gspca_dev, 0xcd41, 0x0124);
		cit_write_reg(gspca_dev, 0xfffa, 0x0124);
		cit_read_reg(gspca_dev, 0x0126, 1);
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

	/* FIXME we should probably use cit_get_clock_div() here (in
	   combination with isoc negotiation using the programmable isoc size)
	   like with the IBM netcam pro). */
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
				cit_read_reg(gspca_dev, rca_initdata[i][2], 0);
			else
				cit_write_reg(gspca_dev, rca_initdata[i][1],
					      rca_initdata[i][2]);
		}
	}

	return 0;
}

static int cit_start_model4(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	cit_write_reg(gspca_dev, 0x0000, 0x0100);
	cit_write_reg(gspca_dev, 0x00c0, 0x0111);
	cit_write_reg(gspca_dev, 0x00bc, 0x012c);
	cit_write_reg(gspca_dev, 0x0080, 0x012b);
	cit_write_reg(gspca_dev, 0x0000, 0x0108);
	cit_write_reg(gspca_dev, 0x0001, 0x0133);
	cit_write_reg(gspca_dev, 0x009b, 0x010f);
	cit_write_reg(gspca_dev, 0x00bb, 0x010f);
	cit_model4_Packet1(gspca_dev, 0x0038, 0x0000);
	cit_model4_Packet1(gspca_dev, 0x000a, 0x005c);

	cit_write_reg(gspca_dev, 0x00aa, 0x012d);
	cit_write_reg(gspca_dev, 0x0004, 0x012f);
	cit_write_reg(gspca_dev, 0xd141, 0x0124);
	cit_write_reg(gspca_dev, 0x0000, 0x0127);
	cit_write_reg(gspca_dev, 0x00fb, 0x012e);
	cit_write_reg(gspca_dev, 0x0000, 0x0130);
	cit_write_reg(gspca_dev, 0x8a28, 0x0124);
	cit_write_reg(gspca_dev, 0x00aa, 0x012f);
	cit_write_reg(gspca_dev, 0xd055, 0x0124);
	cit_write_reg(gspca_dev, 0x000c, 0x0127);
	cit_write_reg(gspca_dev, 0x0009, 0x012e);
	cit_write_reg(gspca_dev, 0xaa28, 0x0124);

	cit_write_reg(gspca_dev, 0x00aa, 0x012d);
	cit_write_reg(gspca_dev, 0x0012, 0x012f);
	cit_write_reg(gspca_dev, 0xd141, 0x0124);
	cit_write_reg(gspca_dev, 0x0008, 0x0127);
	cit_write_reg(gspca_dev, 0x00aa, 0x0130);
	cit_write_reg(gspca_dev, 0x82a8, 0x0124);
	cit_write_reg(gspca_dev, 0x002a, 0x012d);
	cit_write_reg(gspca_dev, 0x0000, 0x012f);
	cit_write_reg(gspca_dev, 0xd145, 0x0124);
	cit_write_reg(gspca_dev, 0xfffa, 0x0124);
	cit_model4_Packet1(gspca_dev, 0x0034, 0x0000);

	switch (gspca_dev->width) {
	case 128: /* 128x96 */
		cit_write_reg(gspca_dev, 0x0070, 0x0119);
		cit_write_reg(gspca_dev, 0x00d0, 0x0111);
		cit_write_reg(gspca_dev, 0x0039, 0x010a);
		cit_write_reg(gspca_dev, 0x0001, 0x0102);
		cit_write_reg(gspca_dev, 0x0028, 0x0103);
		cit_write_reg(gspca_dev, 0x0000, 0x0104);
		cit_write_reg(gspca_dev, 0x001e, 0x0105);
		cit_write_reg(gspca_dev, 0x00aa, 0x012d);
		cit_write_reg(gspca_dev, 0x0016, 0x012f);
		cit_write_reg(gspca_dev, 0xd141, 0x0124);
		cit_write_reg(gspca_dev, 0x000a, 0x0127);
		cit_write_reg(gspca_dev, 0x00aa, 0x0130);
		cit_write_reg(gspca_dev, 0x82a8, 0x0124);
		cit_write_reg(gspca_dev, 0x0014, 0x012d);
		cit_write_reg(gspca_dev, 0x0008, 0x012f);
		cit_write_reg(gspca_dev, 0xd145, 0x0124);
		cit_write_reg(gspca_dev, 0x00aa, 0x012e);
		cit_write_reg(gspca_dev, 0x001a, 0x0130);
		cit_write_reg(gspca_dev, 0x8a0a, 0x0124);
		cit_write_reg(gspca_dev, 0x005a, 0x012d);
		cit_write_reg(gspca_dev, 0x9545, 0x0124);
		cit_write_reg(gspca_dev, 0x00aa, 0x0127);
		cit_write_reg(gspca_dev, 0x0018, 0x012e);
		cit_write_reg(gspca_dev, 0x0043, 0x0130);
		cit_write_reg(gspca_dev, 0x8a28, 0x0124);
		cit_write_reg(gspca_dev, 0x00aa, 0x012f);
		cit_write_reg(gspca_dev, 0xd055, 0x0124);
		cit_write_reg(gspca_dev, 0x001c, 0x0127);
		cit_write_reg(gspca_dev, 0x00eb, 0x012e);
		cit_write_reg(gspca_dev, 0xaa28, 0x0124);
		cit_write_reg(gspca_dev, 0x00aa, 0x012d);
		cit_write_reg(gspca_dev, 0x0032, 0x012f);
		cit_write_reg(gspca_dev, 0xd141, 0x0124);
		cit_write_reg(gspca_dev, 0x0000, 0x0127);
		cit_write_reg(gspca_dev, 0x00aa, 0x0130);
		cit_write_reg(gspca_dev, 0x82a8, 0x0124);
		cit_write_reg(gspca_dev, 0x0036, 0x012d);
		cit_write_reg(gspca_dev, 0x0008, 0x012f);
		cit_write_reg(gspca_dev, 0xd145, 0x0124);
		cit_write_reg(gspca_dev, 0xfffa, 0x0124);
		cit_write_reg(gspca_dev, 0x00aa, 0x012d);
		cit_write_reg(gspca_dev, 0x001e, 0x012f);
		cit_write_reg(gspca_dev, 0xd141, 0x0124);
		cit_write_reg(gspca_dev, 0x0017, 0x0127);
		cit_write_reg(gspca_dev, 0x0013, 0x012e);
		cit_write_reg(gspca_dev, 0x0031, 0x0130);
		cit_write_reg(gspca_dev, 0x8a28, 0x0124);
		cit_write_reg(gspca_dev, 0x0017, 0x012d);
		cit_write_reg(gspca_dev, 0x0078, 0x012f);
		cit_write_reg(gspca_dev, 0xd145, 0x0124);
		cit_write_reg(gspca_dev, 0x0000, 0x0127);
		cit_write_reg(gspca_dev, 0xfea8, 0x0124);
		sd->sof_len = 2;
		break;
	case 160: /* 160x120 */
		cit_write_reg(gspca_dev, 0x0038, 0x0119);
		cit_write_reg(gspca_dev, 0x00d0, 0x0111);
		cit_write_reg(gspca_dev, 0x00b9, 0x010a);
		cit_write_reg(gspca_dev, 0x0001, 0x0102);
		cit_write_reg(gspca_dev, 0x0028, 0x0103);
		cit_write_reg(gspca_dev, 0x0000, 0x0104);
		cit_write_reg(gspca_dev, 0x001e, 0x0105);
		cit_write_reg(gspca_dev, 0x00aa, 0x012d);
		cit_write_reg(gspca_dev, 0x0016, 0x012f);
		cit_write_reg(gspca_dev, 0xd141, 0x0124);
		cit_write_reg(gspca_dev, 0x000b, 0x0127);
		cit_write_reg(gspca_dev, 0x00aa, 0x0130);
		cit_write_reg(gspca_dev, 0x82a8, 0x0124);
		cit_write_reg(gspca_dev, 0x0014, 0x012d);
		cit_write_reg(gspca_dev, 0x0008, 0x012f);
		cit_write_reg(gspca_dev, 0xd145, 0x0124);
		cit_write_reg(gspca_dev, 0x00aa, 0x012e);
		cit_write_reg(gspca_dev, 0x001a, 0x0130);
		cit_write_reg(gspca_dev, 0x8a0a, 0x0124);
		cit_write_reg(gspca_dev, 0x005a, 0x012d);
		cit_write_reg(gspca_dev, 0x9545, 0x0124);
		cit_write_reg(gspca_dev, 0x00aa, 0x0127);
		cit_write_reg(gspca_dev, 0x0018, 0x012e);
		cit_write_reg(gspca_dev, 0x0043, 0x0130);
		cit_write_reg(gspca_dev, 0x8a28, 0x0124);
		cit_write_reg(gspca_dev, 0x00aa, 0x012f);
		cit_write_reg(gspca_dev, 0xd055, 0x0124);
		cit_write_reg(gspca_dev, 0x001c, 0x0127);
		cit_write_reg(gspca_dev, 0x00c7, 0x012e);
		cit_write_reg(gspca_dev, 0xaa28, 0x0124);
		cit_write_reg(gspca_dev, 0x00aa, 0x012d);
		cit_write_reg(gspca_dev, 0x0032, 0x012f);
		cit_write_reg(gspca_dev, 0xd141, 0x0124);
		cit_write_reg(gspca_dev, 0x0025, 0x0127);
		cit_write_reg(gspca_dev, 0x00aa, 0x0130);
		cit_write_reg(gspca_dev, 0x82a8, 0x0124);
		cit_write_reg(gspca_dev, 0x0036, 0x012d);
		cit_write_reg(gspca_dev, 0x0008, 0x012f);
		cit_write_reg(gspca_dev, 0xd145, 0x0124);
		cit_write_reg(gspca_dev, 0xfffa, 0x0124);
		cit_write_reg(gspca_dev, 0x00aa, 0x012d);
		cit_write_reg(gspca_dev, 0x001e, 0x012f);
		cit_write_reg(gspca_dev, 0xd141, 0x0124);
		cit_write_reg(gspca_dev, 0x0048, 0x0127);
		cit_write_reg(gspca_dev, 0x0035, 0x012e);
		cit_write_reg(gspca_dev, 0x00d0, 0x0130);
		cit_write_reg(gspca_dev, 0x8a28, 0x0124);
		cit_write_reg(gspca_dev, 0x0048, 0x012d);
		cit_write_reg(gspca_dev, 0x0090, 0x012f);
		cit_write_reg(gspca_dev, 0xd145, 0x0124);
		cit_write_reg(gspca_dev, 0x0001, 0x0127);
		cit_write_reg(gspca_dev, 0xfea8, 0x0124);
		sd->sof_len = 2;
		break;
	case 176: /* 176x144 */
		cit_write_reg(gspca_dev, 0x0038, 0x0119);
		cit_write_reg(gspca_dev, 0x00d0, 0x0111);
		cit_write_reg(gspca_dev, 0x00b9, 0x010a);
		cit_write_reg(gspca_dev, 0x0001, 0x0102);
		cit_write_reg(gspca_dev, 0x002c, 0x0103);
		cit_write_reg(gspca_dev, 0x0000, 0x0104);
		cit_write_reg(gspca_dev, 0x0024, 0x0105);
		cit_write_reg(gspca_dev, 0x00aa, 0x012d);
		cit_write_reg(gspca_dev, 0x0016, 0x012f);
		cit_write_reg(gspca_dev, 0xd141, 0x0124);
		cit_write_reg(gspca_dev, 0x0007, 0x0127);
		cit_write_reg(gspca_dev, 0x00aa, 0x0130);
		cit_write_reg(gspca_dev, 0x82a8, 0x0124);
		cit_write_reg(gspca_dev, 0x0014, 0x012d);
		cit_write_reg(gspca_dev, 0x0001, 0x012f);
		cit_write_reg(gspca_dev, 0xd145, 0x0124);
		cit_write_reg(gspca_dev, 0x00aa, 0x012e);
		cit_write_reg(gspca_dev, 0x001a, 0x0130);
		cit_write_reg(gspca_dev, 0x8a0a, 0x0124);
		cit_write_reg(gspca_dev, 0x005e, 0x012d);
		cit_write_reg(gspca_dev, 0x9545, 0x0124);
		cit_write_reg(gspca_dev, 0x00aa, 0x0127);
		cit_write_reg(gspca_dev, 0x0018, 0x012e);
		cit_write_reg(gspca_dev, 0x0049, 0x0130);
		cit_write_reg(gspca_dev, 0x8a28, 0x0124);
		cit_write_reg(gspca_dev, 0x00aa, 0x012f);
		cit_write_reg(gspca_dev, 0xd055, 0x0124);
		cit_write_reg(gspca_dev, 0x001c, 0x0127);
		cit_write_reg(gspca_dev, 0x00c7, 0x012e);
		cit_write_reg(gspca_dev, 0xaa28, 0x0124);
		cit_write_reg(gspca_dev, 0x00aa, 0x012d);
		cit_write_reg(gspca_dev, 0x0032, 0x012f);
		cit_write_reg(gspca_dev, 0xd141, 0x0124);
		cit_write_reg(gspca_dev, 0x0028, 0x0127);
		cit_write_reg(gspca_dev, 0x00aa, 0x0130);
		cit_write_reg(gspca_dev, 0x82a8, 0x0124);
		cit_write_reg(gspca_dev, 0x0036, 0x012d);
		cit_write_reg(gspca_dev, 0x0008, 0x012f);
		cit_write_reg(gspca_dev, 0xd145, 0x0124);
		cit_write_reg(gspca_dev, 0xfffa, 0x0124);
		cit_write_reg(gspca_dev, 0x00aa, 0x012d);
		cit_write_reg(gspca_dev, 0x001e, 0x012f);
		cit_write_reg(gspca_dev, 0xd141, 0x0124);
		cit_write_reg(gspca_dev, 0x0010, 0x0127);
		cit_write_reg(gspca_dev, 0x0013, 0x012e);
		cit_write_reg(gspca_dev, 0x002a, 0x0130);
		cit_write_reg(gspca_dev, 0x8a28, 0x0124);
		cit_write_reg(gspca_dev, 0x0010, 0x012d);
		cit_write_reg(gspca_dev, 0x006d, 0x012f);
		cit_write_reg(gspca_dev, 0xd145, 0x0124);
		cit_write_reg(gspca_dev, 0x0001, 0x0127);
		cit_write_reg(gspca_dev, 0xfea8, 0x0124);
		/* TESTME HDG: this does not seem right
		   (it is 2 for all other resolutions) */
		sd->sof_len = 10;
		break;
	case 320: /* 320x240 */
		cit_write_reg(gspca_dev, 0x0070, 0x0119);
		cit_write_reg(gspca_dev, 0x00d0, 0x0111);
		cit_write_reg(gspca_dev, 0x0039, 0x010a);
		cit_write_reg(gspca_dev, 0x0001, 0x0102);
		cit_write_reg(gspca_dev, 0x0028, 0x0103);
		cit_write_reg(gspca_dev, 0x0000, 0x0104);
		cit_write_reg(gspca_dev, 0x001e, 0x0105);
		cit_write_reg(gspca_dev, 0x00aa, 0x012d);
		cit_write_reg(gspca_dev, 0x0016, 0x012f);
		cit_write_reg(gspca_dev, 0xd141, 0x0124);
		cit_write_reg(gspca_dev, 0x000a, 0x0127);
		cit_write_reg(gspca_dev, 0x00aa, 0x0130);
		cit_write_reg(gspca_dev, 0x82a8, 0x0124);
		cit_write_reg(gspca_dev, 0x0014, 0x012d);
		cit_write_reg(gspca_dev, 0x0008, 0x012f);
		cit_write_reg(gspca_dev, 0xd145, 0x0124);
		cit_write_reg(gspca_dev, 0x00aa, 0x012e);
		cit_write_reg(gspca_dev, 0x001a, 0x0130);
		cit_write_reg(gspca_dev, 0x8a0a, 0x0124);
		cit_write_reg(gspca_dev, 0x005a, 0x012d);
		cit_write_reg(gspca_dev, 0x9545, 0x0124);
		cit_write_reg(gspca_dev, 0x00aa, 0x0127);
		cit_write_reg(gspca_dev, 0x0018, 0x012e);
		cit_write_reg(gspca_dev, 0x0043, 0x0130);
		cit_write_reg(gspca_dev, 0x8a28, 0x0124);
		cit_write_reg(gspca_dev, 0x00aa, 0x012f);
		cit_write_reg(gspca_dev, 0xd055, 0x0124);
		cit_write_reg(gspca_dev, 0x001c, 0x0127);
		cit_write_reg(gspca_dev, 0x00eb, 0x012e);
		cit_write_reg(gspca_dev, 0xaa28, 0x0124);
		cit_write_reg(gspca_dev, 0x00aa, 0x012d);
		cit_write_reg(gspca_dev, 0x0032, 0x012f);
		cit_write_reg(gspca_dev, 0xd141, 0x0124);
		cit_write_reg(gspca_dev, 0x0000, 0x0127);
		cit_write_reg(gspca_dev, 0x00aa, 0x0130);
		cit_write_reg(gspca_dev, 0x82a8, 0x0124);
		cit_write_reg(gspca_dev, 0x0036, 0x012d);
		cit_write_reg(gspca_dev, 0x0008, 0x012f);
		cit_write_reg(gspca_dev, 0xd145, 0x0124);
		cit_write_reg(gspca_dev, 0xfffa, 0x0124);
		cit_write_reg(gspca_dev, 0x00aa, 0x012d);
		cit_write_reg(gspca_dev, 0x001e, 0x012f);
		cit_write_reg(gspca_dev, 0xd141, 0x0124);
		cit_write_reg(gspca_dev, 0x0017, 0x0127);
		cit_write_reg(gspca_dev, 0x0013, 0x012e);
		cit_write_reg(gspca_dev, 0x0031, 0x0130);
		cit_write_reg(gspca_dev, 0x8a28, 0x0124);
		cit_write_reg(gspca_dev, 0x0017, 0x012d);
		cit_write_reg(gspca_dev, 0x0078, 0x012f);
		cit_write_reg(gspca_dev, 0xd145, 0x0124);
		cit_write_reg(gspca_dev, 0x0000, 0x0127);
		cit_write_reg(gspca_dev, 0xfea8, 0x0124);
		sd->sof_len = 2;
		break;
	case 352: /* 352x288 */
		cit_write_reg(gspca_dev, 0x0070, 0x0119);
		cit_write_reg(gspca_dev, 0x00c0, 0x0111);
		cit_write_reg(gspca_dev, 0x0039, 0x010a);
		cit_write_reg(gspca_dev, 0x0001, 0x0102);
		cit_write_reg(gspca_dev, 0x002c, 0x0103);
		cit_write_reg(gspca_dev, 0x0000, 0x0104);
		cit_write_reg(gspca_dev, 0x0024, 0x0105);
		cit_write_reg(gspca_dev, 0x00aa, 0x012d);
		cit_write_reg(gspca_dev, 0x0016, 0x012f);
		cit_write_reg(gspca_dev, 0xd141, 0x0124);
		cit_write_reg(gspca_dev, 0x0006, 0x0127);
		cit_write_reg(gspca_dev, 0x00aa, 0x0130);
		cit_write_reg(gspca_dev, 0x82a8, 0x0124);
		cit_write_reg(gspca_dev, 0x0014, 0x012d);
		cit_write_reg(gspca_dev, 0x0002, 0x012f);
		cit_write_reg(gspca_dev, 0xd145, 0x0124);
		cit_write_reg(gspca_dev, 0x00aa, 0x012e);
		cit_write_reg(gspca_dev, 0x001a, 0x0130);
		cit_write_reg(gspca_dev, 0x8a0a, 0x0124);
		cit_write_reg(gspca_dev, 0x005e, 0x012d);
		cit_write_reg(gspca_dev, 0x9545, 0x0124);
		cit_write_reg(gspca_dev, 0x00aa, 0x0127);
		cit_write_reg(gspca_dev, 0x0018, 0x012e);
		cit_write_reg(gspca_dev, 0x0049, 0x0130);
		cit_write_reg(gspca_dev, 0x8a28, 0x0124);
		cit_write_reg(gspca_dev, 0x00aa, 0x012f);
		cit_write_reg(gspca_dev, 0xd055, 0x0124);
		cit_write_reg(gspca_dev, 0x001c, 0x0127);
		cit_write_reg(gspca_dev, 0x00cf, 0x012e);
		cit_write_reg(gspca_dev, 0xaa28, 0x0124);
		cit_write_reg(gspca_dev, 0x00aa, 0x012d);
		cit_write_reg(gspca_dev, 0x0032, 0x012f);
		cit_write_reg(gspca_dev, 0xd141, 0x0124);
		cit_write_reg(gspca_dev, 0x0000, 0x0127);
		cit_write_reg(gspca_dev, 0x00aa, 0x0130);
		cit_write_reg(gspca_dev, 0x82a8, 0x0124);
		cit_write_reg(gspca_dev, 0x0036, 0x012d);
		cit_write_reg(gspca_dev, 0x0008, 0x012f);
		cit_write_reg(gspca_dev, 0xd145, 0x0124);
		cit_write_reg(gspca_dev, 0xfffa, 0x0124);
		cit_write_reg(gspca_dev, 0x00aa, 0x012d);
		cit_write_reg(gspca_dev, 0x001e, 0x012f);
		cit_write_reg(gspca_dev, 0xd141, 0x0124);
		cit_write_reg(gspca_dev, 0x0010, 0x0127);
		cit_write_reg(gspca_dev, 0x0013, 0x012e);
		cit_write_reg(gspca_dev, 0x0025, 0x0130);
		cit_write_reg(gspca_dev, 0x8a28, 0x0124);
		cit_write_reg(gspca_dev, 0x0010, 0x012d);
		cit_write_reg(gspca_dev, 0x0048, 0x012f);
		cit_write_reg(gspca_dev, 0xd145, 0x0124);
		cit_write_reg(gspca_dev, 0x0000, 0x0127);
		cit_write_reg(gspca_dev, 0xfea8, 0x0124);
		sd->sof_len = 2;
		break;
	}

	cit_model4_Packet1(gspca_dev, 0x0038, 0x0004);

	return 0;
}

static int cit_start_ibm_netcam_pro(struct gspca_dev *gspca_dev)
{
	const unsigned short compression = 0; /* 0=none, 7=best frame rate */
	int i, clock_div;

	clock_div = cit_get_clock_div(gspca_dev);
	if (clock_div < 0)
		return clock_div;

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
	case 160: /* 160x120 */
		cit_write_reg(gspca_dev, 0x0024, 0x010b);
		cit_write_reg(gspca_dev, 0x0089, 0x0119);
		cit_write_reg(gspca_dev, 0x000a, 0x011b);
		cit_write_reg(gspca_dev, 0x0003, 0x011e);
		cit_write_reg(gspca_dev, 0x0007, 0x0104);
		cit_write_reg(gspca_dev, 0x0009, 0x011a);
		cit_write_reg(gspca_dev, 0x008b, 0x011c);
		cit_write_reg(gspca_dev, 0x0008, 0x0118);
		cit_write_reg(gspca_dev, 0x0000, 0x0132);
		break;
	case 320: /* 320x240 */
		cit_write_reg(gspca_dev, 0x0028, 0x010b);
		cit_write_reg(gspca_dev, 0x00d9, 0x0119);
		cit_write_reg(gspca_dev, 0x0006, 0x011b);
		cit_write_reg(gspca_dev, 0x0000, 0x011e);
		cit_write_reg(gspca_dev, 0x000e, 0x0104);
		cit_write_reg(gspca_dev, 0x0004, 0x011a);
		cit_write_reg(gspca_dev, 0x003f, 0x011c);
		cit_write_reg(gspca_dev, 0x000c, 0x0118);
		cit_write_reg(gspca_dev, 0x0000, 0x0132);
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
				cit_read_reg(gspca_dev, rca_initdata[i][2], 0);
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
	int packet_size;

	packet_size = cit_get_packet_size(gspca_dev);
	if (packet_size < 0)
		return packet_size;

	switch (sd->model) {
	case CIT_MODEL0:
		cit_start_model0(gspca_dev);
		break;
	case CIT_MODEL1:
		cit_start_model1(gspca_dev);
		break;
	case CIT_MODEL2:
		cit_start_model2(gspca_dev);
		break;
	case CIT_MODEL3:
		cit_start_model3(gspca_dev);
		break;
	case CIT_MODEL4:
		cit_start_model4(gspca_dev);
		break;
	case CIT_IBM_NETCAM_PRO:
		cit_start_ibm_netcam_pro(gspca_dev);
		break;
	}

	cit_set_brightness(gspca_dev);
	cit_set_contrast(gspca_dev);
	cit_set_hue(gspca_dev);
	cit_set_sharpness(gspca_dev);
	cit_set_lighting(gspca_dev);
	cit_set_hflip(gspca_dev);

	/* Program max isoc packet size */
	cit_write_reg(gspca_dev, packet_size >> 8, 0x0106);
	cit_write_reg(gspca_dev, packet_size & 0xff, 0x0107);

	cit_restart_stream(gspca_dev);

	return 0;
}

static int sd_isoc_init(struct gspca_dev *gspca_dev)
{
	struct usb_host_interface *alt;
	int max_packet_size;

	switch (gspca_dev->width) {
	case 160:
		max_packet_size = 450;
		break;
	case 176:
		max_packet_size = 600;
		break;
	default:
		max_packet_size = 1022;
		break;
	}

	/* Start isoc bandwidth "negotiation" at max isoc bandwidth */
	alt = &gspca_dev->dev->config->intf_cache[0]->altsetting[1];
	alt->endpoint[0].desc.wMaxPacketSize = cpu_to_le16(max_packet_size);

	return 0;
}

static int sd_isoc_nego(struct gspca_dev *gspca_dev)
{
	int ret, packet_size, min_packet_size;
	struct usb_host_interface *alt;

	switch (gspca_dev->width) {
	case 160:
		min_packet_size = 200;
		break;
	case 176:
		min_packet_size = 266;
		break;
	default:
		min_packet_size = 400;
		break;
	}

	alt = &gspca_dev->dev->config->intf_cache[0]->altsetting[1];
	packet_size = le16_to_cpu(alt->endpoint[0].desc.wMaxPacketSize);
	if (packet_size <= min_packet_size)
		return -EIO;

	packet_size -= 100;
	if (packet_size < min_packet_size)
		packet_size = min_packet_size;
	alt->endpoint[0].desc.wMaxPacketSize = cpu_to_le16(packet_size);

	ret = usb_set_interface(gspca_dev->dev, gspca_dev->iface, 1);
	if (ret < 0)
		err("set alt 1 err %d", ret);

	return ret;
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	cit_write_reg(gspca_dev, 0x0000, 0x010c);
}

static void sd_stop0(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	/* We cannot use gspca_dev->present here as that is not set when
	   sd_init gets called and we get called from sd_init */
	if (!gspca_dev->dev)
		return;

	switch (sd->model) {
	case CIT_MODEL0:
		/* HDG windows does this, but it causes the cams autogain to
		   restart from a gain of 0, which does not look good when
		   changing resolutions. */
		/* cit_write_reg(gspca_dev, 0x0000, 0x0112); */
		cit_write_reg(gspca_dev, 0x00c0, 0x0100); /* LED Off */
		break;
	case CIT_MODEL1:
		cit_send_FF_04_02(gspca_dev);
		cit_read_reg(gspca_dev, 0x0100, 0);
		cit_write_reg(gspca_dev, 0x81, 0x0100);	/* LED Off */
		break;
	case CIT_MODEL2:
	case CIT_MODEL4:
		cit_model2_Packet1(gspca_dev, 0x0030, 0x0004);

		cit_write_reg(gspca_dev, 0x0080, 0x0100);	/* LED Off */
		cit_write_reg(gspca_dev, 0x0020, 0x0111);
		cit_write_reg(gspca_dev, 0x00a0, 0x0111);

		cit_model2_Packet1(gspca_dev, 0x0030, 0x0002);

		cit_write_reg(gspca_dev, 0x0020, 0x0111);
		cit_write_reg(gspca_dev, 0x0000, 0x0112);
		break;
	case CIT_MODEL3:
		cit_write_reg(gspca_dev, 0x0006, 0x012c);
		cit_model3_Packet1(gspca_dev, 0x0046, 0x0000);
		cit_read_reg(gspca_dev, 0x0116, 0);
		cit_write_reg(gspca_dev, 0x0064, 0x0116);
		cit_read_reg(gspca_dev, 0x0115, 0);
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

#if defined(CONFIG_INPUT) || defined(CONFIG_INPUT_MODULE)
	/* If the last button state is pressed, release it now! */
	if (sd->button_state) {
		input_report_key(gspca_dev->input_dev, KEY_CAMERA, 0);
		input_sync(gspca_dev->input_dev);
		sd->button_state = 0;
	}
#endif
}

static u8 *cit_find_sof(struct gspca_dev *gspca_dev, u8 *data, int len)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 byte3 = 0, byte4 = 0;
	int i;

	switch (sd->model) {
	case CIT_MODEL0:
	case CIT_MODEL1:
	case CIT_MODEL3:
	case CIT_IBM_NETCAM_PRO:
		switch (gspca_dev->width) {
		case 160: /* 160x120 */
			byte3 = 0x02;
			byte4 = 0x0a;
			break;
		case 176: /* 176x144 */
			byte3 = 0x02;
			byte4 = 0x0e;
			break;
		case 320: /* 320x240 */
			byte3 = 0x02;
			byte4 = 0x08;
			break;
		case 352: /* 352x288 */
			byte3 = 0x02;
			byte4 = 0x00;
			break;
		case 640:
			byte3 = 0x03;
			byte4 = 0x08;
			break;
		}

		/* These have a different byte3 */
		if (sd->model <= CIT_MODEL1)
			byte3 = 0x00;

		for (i = 0; i < len; i++) {
			/* For this model the SOF always starts at offset 0
			   so no need to search the entire frame */
			if (sd->model == CIT_MODEL0 && sd->sof_read != i)
				break;

			switch (sd->sof_read) {
			case 0:
				if (data[i] == 0x00)
					sd->sof_read++;
				break;
			case 1:
				if (data[i] == 0xff)
					sd->sof_read++;
				else if (data[i] == 0x00)
					sd->sof_read = 1;
				else
					sd->sof_read = 0;
				break;
			case 2:
				if (data[i] == byte3)
					sd->sof_read++;
				else if (data[i] == 0x00)
					sd->sof_read = 1;
				else
					sd->sof_read = 0;
				break;
			case 3:
				if (data[i] == byte4) {
					sd->sof_read = 0;
					return data + i + (sd->sof_len - 3);
				}
				if (byte3 == 0x00 && data[i] == 0xff)
					sd->sof_read = 2;
				else if (data[i] == 0x00)
					sd->sof_read = 1;
				else
					sd->sof_read = 0;
				break;
			}
		}
		break;
	case CIT_MODEL2:
	case CIT_MODEL4:
		/* TESTME we need to find a longer sof signature to avoid
		   false positives */
		for (i = 0; i < len; i++) {
			switch (sd->sof_read) {
			case 0:
				if (data[i] == 0x00)
					sd->sof_read++;
				break;
			case 1:
				sd->sof_read = 0;
				if (data[i] == 0xff) {
					if (i >= 4)
						PDEBUG(D_FRAM,
						       "header found at offset: %d: %02x %02x 00 %02x %02x %02x\n",
						       i - 1,
						       data[i - 4],
						       data[i - 3],
						       data[i],
						       data[i + 1],
						       data[i + 2]);
					else
						PDEBUG(D_FRAM,
						       "header found at offset: %d: 00 %02x %02x %02x\n",
						       i - 1,
						       data[i],
						       data[i + 1],
						       data[i + 2]);
					return data + i + (sd->sof_len - 1);
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
	struct sd *sd = (struct sd *) gspca_dev;
	unsigned char *sof;

	sof = cit_find_sof(gspca_dev, data, len);
	if (sof) {
		int n;

		/* finish decoding current frame */
		n = sof - data;
		if (n > sd->sof_len)
			n -= sd->sof_len;
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
		if (sd->stop_on_control_change)
			sd_stopN(gspca_dev);
		cit_set_brightness(gspca_dev);
		if (sd->stop_on_control_change)
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
		if (sd->stop_on_control_change)
			sd_stopN(gspca_dev);
		cit_set_contrast(gspca_dev);
		if (sd->stop_on_control_change)
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
		if (sd->stop_on_control_change)
			sd_stopN(gspca_dev);
		cit_set_hue(gspca_dev);
		if (sd->stop_on_control_change)
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
		if (sd->stop_on_control_change)
			sd_stopN(gspca_dev);
		cit_set_sharpness(gspca_dev);
		if (sd->stop_on_control_change)
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

static int sd_setlighting(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->lighting = val;
	if (gspca_dev->streaming) {
		if (sd->stop_on_control_change)
			sd_stopN(gspca_dev);
		cit_set_lighting(gspca_dev);
		if (sd->stop_on_control_change)
			cit_restart_stream(gspca_dev);
	}
	return 0;
}

static int sd_getlighting(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->lighting;

	return 0;
}

static int sd_sethflip(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->hflip = val;
	if (gspca_dev->streaming) {
		if (sd->stop_on_control_change)
			sd_stopN(gspca_dev);
		cit_set_hflip(gspca_dev);
		if (sd->stop_on_control_change)
			cit_restart_stream(gspca_dev);
	}
	return 0;
}

static int sd_gethflip(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->hflip;

	return 0;
}

#if defined(CONFIG_INPUT) || defined(CONFIG_INPUT_MODULE)
static void cit_check_button(struct gspca_dev *gspca_dev)
{
	int new_button_state;
	struct sd *sd = (struct sd *)gspca_dev;

	switch (sd->model) {
	case CIT_MODEL3:
	case CIT_IBM_NETCAM_PRO:
		break;
	default: /* TEST ME unknown if this works on other models too */
		return;
	}

	/* Read the button state */
	cit_read_reg(gspca_dev, 0x0113, 0);
	new_button_state = !gspca_dev->usb_buf[0];

	/* Tell the cam we've seen the button press, notice that this
	   is a nop (iow the cam keeps reporting pressed) until the
	   button is actually released. */
	if (new_button_state)
		cit_write_reg(gspca_dev, 0x01, 0x0113);

	if (sd->button_state != new_button_state) {
		input_report_key(gspca_dev->input_dev, KEY_CAMERA,
				 new_button_state);
		input_sync(gspca_dev->input_dev);
		sd->button_state = new_button_state;
	}
}
#endif

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
#if defined(CONFIG_INPUT) || defined(CONFIG_INPUT_MODULE)
	.dq_callback = cit_check_button,
	.other_input = 1,
#endif
};

static const struct sd_desc sd_desc_isoc_nego = {
	.name = MODULE_NAME,
	.ctrls = sd_ctrls,
	.nctrls = ARRAY_SIZE(sd_ctrls),
	.config = sd_config,
	.init = sd_init,
	.start = sd_start,
	.isoc_init = sd_isoc_init,
	.isoc_nego = sd_isoc_nego,
	.stopN = sd_stopN,
	.stop0 = sd_stop0,
	.pkt_scan = sd_pkt_scan,
#if defined(CONFIG_INPUT) || defined(CONFIG_INPUT_MODULE)
	.dq_callback = cit_check_button,
	.other_input = 1,
#endif
};

/* -- module initialisation -- */
static const struct usb_device_id device_table[] = {
	{ USB_DEVICE_VER(0x0545, 0x8080, 0x0001, 0x0001), .driver_info = CIT_MODEL0 },
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
	const struct sd_desc *desc = &sd_desc;

	switch (id->driver_info) {
	case CIT_MODEL0:
	case CIT_MODEL1:
		if (intf->cur_altsetting->desc.bInterfaceNumber != 2)
			return -ENODEV;
		break;
	case CIT_MODEL2:
	case CIT_MODEL4:
		if (intf->cur_altsetting->desc.bInterfaceNumber != 0)
			return -ENODEV;
		break;
	case CIT_MODEL3:
		if (intf->cur_altsetting->desc.bInterfaceNumber != 0)
			return -ENODEV;
		/* FIXME this likely applies to all model3 cams and probably
		   to other models too. */
		if (ibm_netcam_pro)
			desc = &sd_desc_isoc_nego;
		break;
	}

	return gspca_dev_probe2(intf, id, desc, sizeof(struct sd), THIS_MODULE);
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
	return usb_register(&sd_driver);
}
static void __exit sd_mod_exit(void)
{
	usb_deregister(&sd_driver);
}

module_init(sd_mod_init);
module_exit(sd_mod_exit);
