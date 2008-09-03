/*
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

#include "gspca.h"

#define MAX_GAMMA 0x10		/* 0 to 15 */

#define V4L2_CID_EFFECTS (V4L2_CID_PRIVATE_BASE + 0)

MODULE_AUTHOR("Leandro Costantino <le_costantino@pixartargentina.com.ar>");
MODULE_DESCRIPTION("GSPCA/T613 (JPEG Compliance) USB Camera Driver");
MODULE_LICENSE("GPL");

struct sd {
	struct gspca_dev gspca_dev;	/* !! must be the first item */

	unsigned char brightness;
	unsigned char contrast;
	unsigned char colors;
	unsigned char autogain;
	unsigned char gamma;
	unsigned char sharpness;
	unsigned char freq;
	unsigned char whitebalance;
	unsigned char mirror;
	unsigned char effect;
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
static int sd_setwhitebalance(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getwhitebalance(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setflip(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getflip(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_seteffect(struct gspca_dev *gspca_dev, __s32 val);
static int sd_geteffect(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_querymenu(struct gspca_dev *gspca_dev,
			struct v4l2_querymenu *menu);

static struct ctrl sd_ctrls[] = {
#define SD_BRIGHTNESS 0
	{
	 {
	  .id = V4L2_CID_BRIGHTNESS,
	  .type = V4L2_CTRL_TYPE_INTEGER,
	  .name = "Brightness",
	  .minimum = 0,
	  .maximum = 0x0f,
	  .step = 1,
	  .default_value = 0x09,
	  },
	 .set = sd_setbrightness,
	 .get = sd_getbrightness,
	 },
#define SD_CONTRAST 1
	{
	 {
	  .id = V4L2_CID_CONTRAST,
	  .type = V4L2_CTRL_TYPE_INTEGER,
	  .name = "Contrast",
	  .minimum = 0,
	  .maximum = 0x0d,
	  .step = 1,
	  .default_value = 0x07,
	  },
	 .set = sd_setcontrast,
	 .get = sd_getcontrast,
	 },
#define SD_COLOR 2
	{
	 {
	  .id = V4L2_CID_SATURATION,
	  .type = V4L2_CTRL_TYPE_INTEGER,
	  .name = "Color",
	  .minimum = 0,
	  .maximum = 0x0f,
	  .step = 1,
	  .default_value = 0x05,
	  },
	 .set = sd_setcolors,
	 .get = sd_getcolors,
	 },
#define SD_GAMMA 3
	{
	 {
	  .id = V4L2_CID_GAMMA,	/* (gamma on win) */
	  .type = V4L2_CTRL_TYPE_INTEGER,
	  .name = "Gamma (Untested)",
	  .minimum = 0,
	  .maximum = MAX_GAMMA,
	  .step = 1,
	  .default_value = 0x09,
	  },
	 .set = sd_setgamma,
	 .get = sd_getgamma,
	 },
#define SD_AUTOGAIN 4
	{
	 {
	  .id = V4L2_CID_GAIN,	/* here, i activate only the lowlight,
				 * some apps dont bring up the
				 * backligth_compensation control) */
	  .type = V4L2_CTRL_TYPE_INTEGER,
	  .name = "Low Light",
	  .minimum = 0,
	  .maximum = 1,
	  .step = 1,
	  .default_value = 0x01,
	  },
	 .set = sd_setlowlight,
	 .get = sd_getlowlight,
	 },
#define SD_MIRROR 5
	{
	 {
	  .id = V4L2_CID_HFLIP,
	  .type = V4L2_CTRL_TYPE_BOOLEAN,
	  .name = "Mirror Image",
	  .minimum = 0,
	  .maximum = 1,
	  .step = 1,
	  .default_value = 0,
	  },
	 .set = sd_setflip,
	 .get = sd_getflip
	},
#define SD_LIGHTFREQ 6
	{
	 {
	  .id = V4L2_CID_POWER_LINE_FREQUENCY,
	  .type = V4L2_CTRL_TYPE_MENU,
	  .name = "Light Frequency Filter",
	  .minimum = 1,		/* 1 -> 0x50, 2->0x60 */
	  .maximum = 2,
	  .step = 1,
	  .default_value = 1,
	  },
	 .set = sd_setfreq,
	 .get = sd_getfreq},

#define SD_WHITE_BALANCE 7
	{
	 {
	  .id = V4L2_CID_WHITE_BALANCE_TEMPERATURE,
	  .type = V4L2_CTRL_TYPE_INTEGER,
	  .name = "White Balance",
	  .minimum = 0,
	  .maximum = 1,
	  .step = 1,
	  .default_value = 1,
	  },
	 .set = sd_setwhitebalance,
	 .get = sd_getwhitebalance
	},
#define SD_SHARPNESS 8		/* (aka definition on win) */
	{
	 {
	  .id = V4L2_CID_SHARPNESS,
	  .type = V4L2_CTRL_TYPE_INTEGER,
	  .name = "Sharpness",
	  .minimum = 0,
	  .maximum = MAX_GAMMA,	/* 0 to 16 */
	  .step = 1,
	  .default_value = 0x06,
	  },
	 .set = sd_setsharpness,
	 .get = sd_getsharpness,
	 },
#define SD_EFFECTS 9
	{
	 {
	  .id = V4L2_CID_EFFECTS,
	  .type = V4L2_CTRL_TYPE_MENU,
	  .name = "Webcam Effects",
	  .minimum = 0,
	  .maximum = 4,
	  .step = 1,
	  .default_value = 0,
	  },
	 .set = sd_seteffect,
	 .get = sd_geteffect
	},
};

static char *effects_control[] = {
	"Normal",
	"Emboss",		/* disabled */
	"Monochrome",
	"Sepia",
	"Sketch",
	"Sun Effect",		/* disabled */
	"Negative",
};

static struct v4l2_pix_format vga_mode_t16[] = {
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

#define T16_OFFSET_DATA 631
#define MAX_EFFECTS 7
/* easily done by soft, this table could be removed,
 * i keep it here just in case */
static const __u8 effects_table[MAX_EFFECTS][6] = {
	{0xa8, 0xe8, 0xc6, 0xd2, 0xc0, 0x00},	/* Normal */
	{0xa8, 0xc8, 0xc6, 0x52, 0xc0, 0x04},	/* Repujar */
	{0xa8, 0xe8, 0xc6, 0xd2, 0xc0, 0x20},	/* Monochrome */
	{0xa8, 0xe8, 0xc6, 0xd2, 0xc0, 0x80},	/* Sepia */
	{0xa8, 0xc8, 0xc6, 0x52, 0xc0, 0x02},	/* Croquis */
	{0xa8, 0xc8, 0xc6, 0xd2, 0xc0, 0x10},	/* Sun Effect */
	{0xa8, 0xc8, 0xc6, 0xd2, 0xc0, 0x40},	/* Negative */
};

static const __u8 gamma_table[MAX_GAMMA][34] = {
	{0x90, 0x00, 0x91, 0x3e, 0x92, 0x69, 0x93, 0x85,
	 0x94, 0x95, 0x95, 0xa1, 0x96, 0xae, 0x97, 0xb9,
	 0x98, 0xc2, 0x99, 0xcb, 0x9a, 0xd4, 0x9b, 0xdb,
	 0x9c, 0xe3, 0x9d, 0xea, 0x9e, 0xf1, 0x9f, 0xf8,
	 0xa0, 0xff},
	{0x90, 0x00, 0x91, 0x33, 0x92, 0x5A, 0x93, 0x75,
	 0x94, 0x85, 0x95, 0x93, 0x96, 0xA1, 0x97, 0xAD,
	 0x98, 0xB7, 0x99, 0xC2, 0x9A, 0xCB, 0x9B, 0xD4,
	 0x9C, 0xDE, 0x9D, 0xE7, 0x9E, 0xF0, 0x9F, 0xF7,
	 0xa0, 0xff},
	{0x90, 0x00, 0x91, 0x2F, 0x92, 0x51, 0x93, 0x6B,
	 0x94, 0x7C, 0x95, 0x8A, 0x96, 0x99, 0x97, 0xA6,
	 0x98, 0xB1, 0x99, 0xBC, 0x9A, 0xC6, 0x9B, 0xD0,
	 0x9C, 0xDB, 0x9D, 0xE4, 0x9E, 0xED, 0x9F, 0xF6,
	 0xa0, 0xff},
	{0x90, 0x00, 0x91, 0x29, 0x92, 0x48, 0x93, 0x60,
	 0x94, 0x72, 0x95, 0x81, 0x96, 0x90, 0x97, 0x9E,
	 0x98, 0xAA, 0x99, 0xB5, 0x9A, 0xBF, 0x9B, 0xCB,
	 0x9C, 0xD6, 0x9D, 0xE1, 0x9E, 0xEB, 0x9F, 0xF5,
	 0xa0, 0xff},
	{0x90, 0x00, 0x91, 0x23, 0x92, 0x3F, 0x93, 0x55,
	 0x94, 0x68, 0x95, 0x77, 0x96, 0x86, 0x97, 0x95,
	 0x98, 0xA2, 0x99, 0xAD, 0x9A, 0xB9, 0x9B, 0xC6,
	 0x9C, 0xD2, 0x9D, 0xDE, 0x9E, 0xE9, 0x9F, 0xF4,
	 0xa0, 0xff},
	{0x90, 0x00, 0x91, 0x1B, 0x92, 0x33, 0x93, 0x48,
	 0x94, 0x59, 0x95, 0x69, 0x96, 0x79, 0x97, 0x87,
	 0x98, 0x96, 0x99, 0xA3, 0x9A, 0xB1, 0x9B, 0xBE,
	 0x9C, 0xCC, 0x9D, 0xDA, 0x9E, 0xE7, 0x9F, 0xF3,
	 0xa0, 0xff},
	{0x90, 0x00, 0x91, 0x02, 0x92, 0x10, 0x93, 0x20,
	 0x94, 0x32, 0x95, 0x40, 0x96, 0x57, 0x97, 0x67,
	 0x98, 0x77, 0x99, 0x88, 0x9a, 0x99, 0x9b, 0xaa,
	 0x9c, 0xbb, 0x9d, 0xcc, 0x9e, 0xdd, 0x9f, 0xee,
	 0xa0, 0xff},
	{0x90, 0x00, 0x91, 0x02, 0x92, 0x14, 0x93, 0x26,
	 0x94, 0x38, 0x95, 0x4A, 0x96, 0x60, 0x97, 0x70,
	 0x98, 0x80, 0x99, 0x90, 0x9A, 0xA0, 0x9B, 0xB0,
	 0x9C, 0xC0, 0x9D, 0xD0, 0x9E, 0xE0, 0x9F, 0xF0,
	 0xa0, 0xff},
	{0x90, 0x00, 0x91, 0x10, 0x92, 0x22, 0x93, 0x35,
	 0x94, 0x47, 0x95, 0x5A, 0x96, 0x69, 0x97, 0x79,
	 0x98, 0x88, 0x99, 0x97, 0x9A, 0xA7, 0x9B, 0xB6,
	 0x9C, 0xC4, 0x9D, 0xD3, 0x9E, 0xE0, 0x9F, 0xF0,
	 0xa0, 0xff},
	{0x90, 0x00, 0x91, 0x10, 0x92, 0x26, 0x93, 0x40,
	 0x94, 0x54, 0x95, 0x65, 0x96, 0x75, 0x97, 0x84,
	 0x98, 0x93, 0x99, 0xa1, 0x9a, 0xb0, 0x9b, 0xbd,
	 0x9c, 0xca, 0x9d, 0xd6, 0x9e, 0xe0, 0x9f, 0xf0,
	 0xa0, 0xff},
	{0x90, 0x00, 0x91, 0x18, 0x92, 0x2B, 0x93, 0x44,
	 0x94, 0x60, 0x95, 0x70, 0x96, 0x80, 0x97, 0x8E,
	 0x98, 0x9C, 0x99, 0xAA, 0x9A, 0xB7, 0x9B, 0xC4,
	 0x9C, 0xD0, 0x9D, 0xD8, 0x9E, 0xE2, 0x9F, 0xF0,
	 0xa0, 0xff},
	{0x90, 0x00, 0x91, 0x1A, 0x92, 0x34, 0x93, 0x52,
	 0x94, 0x66, 0x95, 0x7E, 0x96, 0x8D, 0x97, 0x9B,
	 0x98, 0xA8, 0x99, 0xB4, 0x9A, 0xC0, 0x9B, 0xCB,
	 0x9C, 0xD6, 0x9D, 0xE1, 0x9E, 0xEB, 0x9F, 0xF5,
	 0xa0, 0xff},
	{0x90, 0x00, 0x91, 0x3F, 0x92, 0x5A, 0x93, 0x6E,
	 0x94, 0x7F, 0x95, 0x8E, 0x96, 0x9C, 0x97, 0xA8,
	 0x98, 0xB4, 0x99, 0xBF, 0x9A, 0xC9, 0x9B, 0xD3,
	 0x9C, 0xDC, 0x9D, 0xE5, 0x9E, 0xEE, 0x9F, 0xF6,
	 0xA0, 0xFF},
	{0x90, 0x00, 0x91, 0x54, 0x92, 0x6F, 0x93, 0x83,
	 0x94, 0x93, 0x95, 0xA0, 0x96, 0xAD, 0x97, 0xB7,
	 0x98, 0xC2, 0x99, 0xCB, 0x9A, 0xD4, 0x9B, 0xDC,
	 0x9C, 0xE4, 0x9D, 0xEB, 0x9E, 0xF2, 0x9F, 0xF9,
	 0xa0, 0xff},
	{0x90, 0x00, 0x91, 0x6E, 0x92, 0x88, 0x93, 0x9A,
	 0x94, 0xA8, 0x95, 0xB3, 0x96, 0xBD, 0x97, 0xC6,
	 0x98, 0xCF, 0x99, 0xD6, 0x9A, 0xDD, 0x9B, 0xE3,
	 0x9C, 0xE9, 0x9D, 0xEF, 0x9E, 0xF4, 0x9F, 0xFA,
	 0xa0, 0xff},
	{0x90, 0x00, 0x91, 0x93, 0x92, 0xA8, 0x93, 0xB7,
	 0x94, 0xC1, 0x95, 0xCA, 0x96, 0xD2, 0x97, 0xD8,
	 0x98, 0xDE, 0x99, 0xE3, 0x9A, 0xE8, 0x9B, 0xED,
	 0x9C, 0xF1, 0x9D, 0xF5, 0x9E, 0xF8, 0x9F, 0xFC,
	 0xA0, 0xFF}
};

static const __u8 tas5130a_sensor_init[][8] = {
	{0x62, 0x08, 0x63, 0x70, 0x64, 0x1d, 0x60, 0x09},
	{0x62, 0x20, 0x63, 0x01, 0x64, 0x02, 0x60, 0x09},
	{0x62, 0x07, 0x63, 0x03, 0x64, 0x00, 0x60, 0x09},
	{0x62, 0x07, 0x63, 0x03, 0x64, 0x00, 0x60, 0x09},
	{},
};

/* read 1 byte */
static int reg_r_1(struct gspca_dev *gspca_dev,
		   __u16 index)
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
			__u16 value,
			__u16 index,
			const __u8 *buffer, __u16 len)
{
	if (buffer == NULL) {
		usb_control_msg(gspca_dev->dev,
				usb_sndctrlpipe(gspca_dev->dev, 0),
				0,
			   USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_INTERFACE,
				value, index,
				NULL, 0, 500);
		return;
	}
	if (len <= sizeof gspca_dev->usb_buf) {
		memcpy(gspca_dev->usb_buf, buffer, len);
		usb_control_msg(gspca_dev->dev,
				usb_sndctrlpipe(gspca_dev->dev, 0),
				0,
			   USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_INTERFACE,
				value, index,
				gspca_dev->usb_buf, len, 500);
	} else {
		__u8 *tmpbuf;

		tmpbuf = kmalloc(len, GFP_KERNEL);
		memcpy(tmpbuf, buffer, len);
		usb_control_msg(gspca_dev->dev,
				usb_sndctrlpipe(gspca_dev->dev, 0),
				0,
			   USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_INTERFACE,
				value, index,
				tmpbuf, len, 500);
		kfree(tmpbuf);
	}
}

/* this function is called at probe time */
static int sd_config(struct gspca_dev *gspca_dev,
		     const struct usb_device_id *id)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct cam *cam;

	cam = &gspca_dev->cam;
	cam->epaddr = 0x01;

	cam->cam_mode = vga_mode_t16;
	cam->nmodes = ARRAY_SIZE(vga_mode_t16);

	sd->brightness = sd_ctrls[SD_BRIGHTNESS].qctrl.default_value;
	sd->contrast = sd_ctrls[SD_CONTRAST].qctrl.default_value;
	sd->colors = sd_ctrls[SD_COLOR].qctrl.default_value;
	sd->gamma = sd_ctrls[SD_GAMMA].qctrl.default_value;
	sd->mirror = sd_ctrls[SD_MIRROR].qctrl.default_value;
	sd->freq = sd_ctrls[SD_LIGHTFREQ].qctrl.default_value;
	sd->whitebalance = sd_ctrls[SD_WHITE_BALANCE].qctrl.default_value;
	sd->sharpness = sd_ctrls[SD_SHARPNESS].qctrl.default_value;
	sd->effect = sd_ctrls[SD_EFFECTS].qctrl.default_value;
	return 0;
}

static int init_default_parameters(struct gspca_dev *gspca_dev)
{
	/* some of this registers are not really neded, because
	 * they are overriden by setbrigthness, setcontrast, etc,
	 * but wont hurt anyway, and can help someone with similar webcam
	 * to see the initial parameters.*/
	int i = 0;
	__u8 test_byte;

	static const __u8 read_indexs[] =
		{ 0x06, 0x07, 0x0a, 0x0b, 0x66, 0x80, 0x81, 0x8e, 0x8f, 0xa5,
		  0xa6, 0xa8, 0xbb, 0xbc, 0xc6, 0x00, 0x00 };
	static const __u8 n1[6] =
			{0x08, 0x03, 0x09, 0x03, 0x12, 0x04};
	static const __u8 n2[2] =
			{0x08, 0x00};
	static const __u8 nset[6] =
		{ 0x61, 0x68, 0x62, 0xff, 0x60, 0x07 };
	static const __u8 n3[6] =
			{0x61, 0x68, 0x65, 0x0a, 0x60, 0x04};
	static const __u8 n4[0x46] =
		{0x09, 0x01, 0x12, 0x04, 0x66, 0x8a, 0x80, 0x3c,
		 0x81, 0x22, 0x84, 0x50, 0x8a, 0x78, 0x8b, 0x68,
		 0x8c, 0x88, 0x8e, 0x33, 0x8f, 0x24, 0xaa, 0xb1,
		 0xa2, 0x60, 0xa5, 0x30, 0xa6, 0x3a, 0xa8, 0xe8,
		 0xae, 0x05, 0xb1, 0x00, 0xbb, 0x04, 0xbc, 0x48,
		 0xbe, 0x36, 0xc6, 0x88, 0xe9, 0x00, 0xc5, 0xc0,
		 0x65, 0x0a, 0xbb, 0x86, 0xaf, 0x58, 0xb0, 0x68,
		 0x87, 0x40, 0x89, 0x2b, 0x8d, 0xff, 0x83, 0x40,
		 0xac, 0x84, 0xad, 0x86, 0xaf, 0x46};
	static const __u8 nset4[18] = {
		0xe0, 0x60, 0xe1, 0xa8, 0xe2, 0xe0, 0xe3, 0x60, 0xe4, 0xa8,
		0xe5, 0xe0, 0xe6, 0x60, 0xe7, 0xa8,
		0xe8, 0xe0
	};
	/* ojo puede ser 0xe6 en vez de 0xe9 */
	static const __u8 nset2[20] = {
		0xd0, 0xbb, 0xd1, 0x28, 0xd2, 0x10, 0xd3, 0x10, 0xd4, 0xbb,
		0xd5, 0x28, 0xd6, 0x1e, 0xd7, 0x27,
		0xd8, 0xc8, 0xd9, 0xfc
	};
	static const __u8 missing[8] =
		{ 0x87, 0x20, 0x88, 0x20, 0x89, 0x20, 0x80, 0x38 };
	static const __u8 nset3[18] = {
		0xc7, 0x60, 0xc8, 0xa8, 0xc9, 0xe0, 0xca, 0x60, 0xcb, 0xa8,
		0xcc, 0xe0, 0xcd, 0x60, 0xce, 0xa8,
		0xcf, 0xe0
	};
	static const __u8 nset5[4] =
		{ 0x8f, 0x24, 0xc3, 0x00 };	/* bright */
	static const __u8 nset6[34] = {
		0x90, 0x00, 0x91, 0x1c, 0x92, 0x30, 0x93, 0x43, 0x94, 0x54,
		0x95, 0x65, 0x96, 0x75, 0x97, 0x84,
		0x98, 0x93, 0x99, 0xa1, 0x9a, 0xb0, 0x9b, 0xbd, 0x9c, 0xca,
		0x9d, 0xd8, 0x9e, 0xe5, 0x9f, 0xf2,
		0xa0, 0xff
	};			/* Gamma */
	static const __u8 nset7[4] =
			{ 0x66, 0xca, 0xa8, 0xf8 };	/* 50/60 Hz */
	static const __u8 nset9[4] =
			{ 0x0b, 0x04, 0x0a, 0x78 };
	static const __u8 nset8[6] =
			{ 0xa8, 0xf0, 0xc6, 0x88, 0xc0, 0x00 };
	static const __u8 nset10[6] =
			{ 0x0c, 0x03, 0xab, 0x10, 0x81, 0x20 };

	reg_w(gspca_dev, 0x01, 0x0000, n1, 0x06);
	reg_w(gspca_dev, 0x01, 0x0000, nset, 0x06);
	reg_r_1(gspca_dev, 0x0063);
	reg_w(gspca_dev, 0x01, 0x0000, n2, 0x02);

	while (read_indexs[i] != 0x00) {
		test_byte = reg_r_1(gspca_dev, read_indexs[i]);
		PDEBUG(D_CONF, "Reg 0x%02x => 0x%02x", read_indexs[i],
		       test_byte);
		i++;
	}

	reg_w(gspca_dev, 0x01, 0x0000, n3, 0x06);
	reg_w(gspca_dev, 0x01, 0x0000, n4, 0x46);
	reg_r_1(gspca_dev, 0x0080);
	reg_w(gspca_dev, 0x00, 0x2c80, NULL, 0);
	reg_w(gspca_dev, 0x01, 0x0000, nset2, 0x14);
	reg_w(gspca_dev, 0x01, 0x0000, nset3, 0x12);
	reg_w(gspca_dev, 0x01, 0x0000, nset4, 0x12);
	reg_w(gspca_dev, 0x00, 0x3880, NULL, 0);
	reg_w(gspca_dev, 0x00, 0x3880, NULL, 0);
	reg_w(gspca_dev, 0x00, 0x338e, NULL, 0);
	reg_w(gspca_dev, 0x01, 0x0000, nset5, 0x04);
	reg_w(gspca_dev, 0x00, 0x00a9, NULL, 0);
	reg_w(gspca_dev, 0x01, 0x0000, nset6, 0x22);
	reg_w(gspca_dev, 0x00, 0x86bb, NULL, 0);
	reg_w(gspca_dev, 0x00, 0x4aa6, NULL, 0);

	reg_w(gspca_dev, 0x01, 0x0000, missing, 0x08);

	reg_w(gspca_dev, 0x00, 0x2087, NULL, 0);
	reg_w(gspca_dev, 0x00, 0x2088, NULL, 0);
	reg_w(gspca_dev, 0x00, 0x2089, NULL, 0);

	reg_w(gspca_dev, 0x01, 0x0000, nset7, 0x04);
	reg_w(gspca_dev, 0x01, 0x0000, nset10, 0x06);
	reg_w(gspca_dev, 0x01, 0x0000, nset8, 0x06);
	reg_w(gspca_dev, 0x01, 0x0000, nset9, 0x04);

	reg_w(gspca_dev, 0x00, 0x2880, NULL, 0);
	reg_w(gspca_dev, 0x01, 0x0000, nset2, 0x14);
	reg_w(gspca_dev, 0x01, 0x0000, nset3, 0x12);
	reg_w(gspca_dev, 0x01, 0x0000, nset4, 0x12);

	return 0;
}

/* this function is called at probe and resume time */
static int sd_init(struct gspca_dev *gspca_dev)
{
	init_default_parameters(gspca_dev);
	return 0;
}

static void setbrightness(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	unsigned int brightness;
	__u8 set6[4] = { 0x8f, 0x26, 0xc3, 0x80 };
	brightness = sd->brightness;

	if (brightness < 7) {
		set6[3] = 0x70 - (brightness * 0xa);
	} else {
		set6[1] = 0x24;
		set6[3] = 0x00 + ((brightness - 7) * 0xa);
	}

	reg_w(gspca_dev, 0x01, 0x0000, set6, 4);
}

static void setflip(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	__u8 flipcmd[8] =
	    { 0x62, 0x07, 0x63, 0x03, 0x64, 0x00, 0x60, 0x09 };

	if (sd->mirror == 1)
		flipcmd[3] = 0x01;

	reg_w(gspca_dev, 0x01, 0x0000, flipcmd, 8);
}

static void seteffect(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	reg_w(gspca_dev, 0x01, 0x0000, effects_table[sd->effect], 0x06);
	if (sd->effect == 1 || sd->effect == 5) {
		PDEBUG(D_CONF,
		       "This effect have been disabled for webcam \"safety\"");
		return;
	}

	if (sd->effect == 1 || sd->effect == 4)
		reg_w(gspca_dev, 0x00, 0x4aa6, NULL, 0);
	else
		reg_w(gspca_dev, 0x00, 0xfaa6, NULL, 0);
}

static void setwhitebalance(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	__u8 white_balance[8] =
	    { 0x87, 0x20, 0x88, 0x20, 0x89, 0x20, 0x80, 0x38 };

	if (sd->whitebalance == 1)
		white_balance[7] = 0x3c;

	reg_w(gspca_dev, 0x01, 0x0000, white_balance, 8);
}

static void setlightfreq(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	__u8 freq[4] = { 0x66, 0x40, 0xa8, 0xe8 };

	if (sd->freq == 2)	/* 60hz */
		freq[1] = 0x00;

	reg_w(gspca_dev, 0x1, 0x0000, freq, 0x4);
}

static void setcontrast(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	unsigned int contrast = sd->contrast;
	__u16 reg_to_write = 0x00;

	if (contrast < 7)
		reg_to_write = 0x8ea9 - (0x200 * contrast);
	else
		reg_to_write = (0x00a9 + ((contrast - 7) * 0x200));

	reg_w(gspca_dev, 0x00, reg_to_write, NULL, 0);
}

static void setcolors(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	__u16 reg_to_write;

	reg_to_write = 0xc0bb + sd->colors * 0x100;
	reg_w(gspca_dev, 0x00, reg_to_write, NULL, 0);
}

static void setgamma(struct gspca_dev *gspca_dev)
{
}

static void setsharpness(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	__u16 reg_to_write;

	reg_to_write = 0x0aa6 + 0x1000 * sd->sharpness;

	reg_w(gspca_dev, 0x00, reg_to_write, NULL, 0);
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

static int sd_setwhitebalance(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->whitebalance = val;
	if (gspca_dev->streaming)
		setwhitebalance(gspca_dev);
	return 0;
}

static int sd_getwhitebalance(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->whitebalance;
	return *val;
}

static int sd_setflip(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->mirror = val;
	if (gspca_dev->streaming)
		setflip(gspca_dev);
	return 0;
}

static int sd_getflip(struct gspca_dev *gspca_dev, __s32 *val)
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
		setlightfreq(gspca_dev);
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
		reg_w(gspca_dev, 0x00, 0xf48e, NULL, 0);
	else
		reg_w(gspca_dev, 0x00, 0xb48e, NULL, 0);
	return 0;
}

static int sd_getlowlight(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->autogain;
	return 0;
}

static void sd_start(struct gspca_dev *gspca_dev)
{
	int mode;

	static const __u8 t1[] = { 0x66, 0x00, 0xa8, 0xe8 };
	__u8 t2[] = { 0x07, 0x00, 0x0d, 0x60, 0x0e, 0x80 };
	static const __u8 t3[] =
		{ 0xb3, 0x07, 0xb4, 0x00, 0xb5, 0x88, 0xb6, 0x02, 0xb7, 0x06,
		  0xb8, 0x00, 0xb9, 0xe7, 0xba, 0x01 };
	static const __u8 t4[] = { 0x0b, 0x04, 0x0a, 0x40 };

	mode = gspca_dev->cam.cam_mode[(int) gspca_dev->curr_mode]. priv;
	switch (mode) {
	case 1:		/* 352x288 */
		t2[1] = 0x40;
		break;
	case 2:		/* 320x240 */
		t2[1] = 0x10;
		break;
	case 3:		/* 176x144 */
		t2[1] = 0x50;
		break;
	case 4:		/* 160x120 */
		t2[1] = 0x20;
		break;
	default:	/* 640x480 (0x00) */
		break;
	}

	reg_w(gspca_dev, 0x01, 0x0000, tas5130a_sensor_init[0], 0x8);
	reg_w(gspca_dev, 0x01, 0x0000, tas5130a_sensor_init[1], 0x8);
	reg_w(gspca_dev, 0x01, 0x0000, tas5130a_sensor_init[2], 0x8);
	reg_w(gspca_dev, 0x01, 0x0000, tas5130a_sensor_init[3], 0x8);
	reg_w(gspca_dev, 0x00, 0x3c80, NULL, 0);
		/* just in case and to keep sync with logs  (for mine) */
	reg_w(gspca_dev, 0x01, 0x0000, tas5130a_sensor_init[3], 0x8);
	reg_w(gspca_dev, 0x00, 0x3c80, NULL, 0);
		/* just in case and to keep sync with logs  (for mine) */
	reg_w(gspca_dev, 0x01, 0x0000, t1, 4);
	reg_w(gspca_dev, 0x01, 0x0000, t2, 6);
	reg_r_1(gspca_dev, 0x0012);
	reg_w(gspca_dev, 0x01, 0x0000, t3, 0x10);
	reg_w(gspca_dev, 0x00, 0x0013, NULL, 0);
	reg_w(gspca_dev, 0x01, 0x0000, t4, 0x4);
	/* restart on each start, just in case, sometimes regs goes wrong
	 * when using controls from app */
	setbrightness(gspca_dev);
	setcontrast(gspca_dev);
	setcolors(gspca_dev);
}

static void sd_pkt_scan(struct gspca_dev *gspca_dev,
			struct gspca_frame *frame,	/* target */
			__u8 *data,			/* isoc packet */
			int len)			/* iso packet length */
{
	int sof = 0;
	static __u8 ffd9[] = { 0xff, 0xd9 };

	if (data[0] == 0x5a) {
		/* Control Packet, after this came the header again,
		 * but extra bytes came in the packet before this,
		 * sometimes an EOF arrives, sometimes not... */
		return;
	}

	if (data[len - 1] == 0xff && data[len] == 0xd9) {
		/* Just in case, i have seen packets with the marker,
		 * other's do not include it... */
		data += 2;
		len -= 4;
	} else if (data[2] == 0xff && data[3] == 0xd8) {
		sof = 1;
		data += 2;
		len -= 2;
	} else {
		data += 2;
		len -= 2;
	}

	if (sof) {
		/* extra bytes....., could be processed too but would be
		 * a waste of time, right now leave the application and
		 * libjpeg do it for ourserlves.. */
		frame = gspca_frame_add(gspca_dev, LAST_PACKET, frame,
					ffd9, 2);
		gspca_frame_add(gspca_dev, FIRST_PACKET, frame, data, len);
		return;
	}

	gspca_frame_add(gspca_dev, INTER_PACKET, frame, data, len);
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
				effects_control[menu->index], 32);
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
	if (usb_register(&sd_driver) < 0)
		return -1;
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
