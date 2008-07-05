/*
 *		Sunplus spca504(abc) spca533 spca536 library
 *		Copyright (C) 2005 Michel Xhaard mxhaard@magic.fr
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

#define MODULE_NAME "sunplus"

#include "gspca.h"
#include "jpeg.h"

#define DRIVER_VERSION_NUMBER	KERNEL_VERSION(2, 1, 5)
static const char version[] = "2.1.5";

MODULE_AUTHOR("Michel Xhaard <mxhaard@users.sourceforge.net>");
MODULE_DESCRIPTION("GSPCA/SPCA5xx USB Camera Driver");
MODULE_LICENSE("GPL");

/* specific webcam descriptor */
struct sd {
	struct gspca_dev gspca_dev;	/* !! must be the first item */

	__u8 packet[ISO_MAX_SIZE + 128];
				/* !! no more than 128 ff in an ISO packet */

	unsigned char brightness;
	unsigned char contrast;
	unsigned char colors;
	unsigned char autogain;

	char qindex;
	char bridge;
#define BRIDGE_SPCA504 0
#define BRIDGE_SPCA504B 1
#define BRIDGE_SPCA504C 2
#define BRIDGE_SPCA533 3
#define BRIDGE_SPCA536 4
	char subtype;
#define AiptekMiniPenCam13 1
#define LogitechClickSmart420 2
#define LogitechClickSmart820 3
#define MegapixV4 4
};

/* V4L2 controls supported by the driver */
static int sd_setbrightness(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getbrightness(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setcontrast(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getcontrast(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setcolors(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getcolors(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setautogain(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getautogain(struct gspca_dev *gspca_dev, __s32 *val);

static struct ctrl sd_ctrls[] = {
#define SD_BRIGHTNESS 0
	{
	    {
		.id      = V4L2_CID_BRIGHTNESS,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Brightness",
		.minimum = 0,
		.maximum = 0xff,
		.step    = 1,
		.default_value = 0,
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
		.maximum = 0xff,
		.step    = 1,
		.default_value = 0x20,
	    },
	    .set = sd_setcontrast,
	    .get = sd_getcontrast,
	},
#define SD_COLOR 2
	{
	    {
		.id      = V4L2_CID_SATURATION,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Color",
		.minimum = 0,
		.maximum = 0xff,
		.step    = 1,
		.default_value = 0x1a,
	    },
	    .set = sd_setcolors,
	    .get = sd_getcolors,
	},
#define SD_AUTOGAIN 3
	{
	    {
		.id      = V4L2_CID_AUTOGAIN,
		.type    = V4L2_CTRL_TYPE_BOOLEAN,
		.name    = "Auto Gain",
		.minimum = 0,
		.maximum = 1,
		.step    = 1,
		.default_value = 1,
	    },
	    .set = sd_setautogain,
	    .get = sd_getautogain,
	},
};

static struct v4l2_pix_format vga_mode[] = {
	{320, 240, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240 * 3 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 2},
	{640, 480, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 640,
		.sizeimage = 640 * 480 * 3 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 1},
};

static struct v4l2_pix_format custom_mode[] = {
	{320, 240, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240 * 3 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 2},
	{464, 480, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 464,
		.sizeimage = 464 * 480 * 3 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 1},
};

static struct v4l2_pix_format vga_mode2[] = {
	{176, 144, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 176,
		.sizeimage = 176 * 144 * 3 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 4},
	{320, 240, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240 * 3 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 3},
	{352, 288, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 352,
		.sizeimage = 352 * 288 * 3 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 2},
	{640, 480, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 640,
		.sizeimage = 640 * 480 * 3 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 1},
};

#define SPCA50X_OFFSET_DATA 10
#define SPCA504_PCCAM600_OFFSET_SNAPSHOT 3
#define SPCA504_PCCAM600_OFFSET_COMPRESS 4
#define SPCA504_PCCAM600_OFFSET_MODE	 5
#define SPCA504_PCCAM600_OFFSET_DATA	 14
 /* Frame packet header offsets for the spca533 */
#define SPCA533_OFFSET_DATA      16
#define SPCA533_OFFSET_FRAMSEQ	15
/* Frame packet header offsets for the spca536 */
#define SPCA536_OFFSET_DATA      4
#define SPCA536_OFFSET_FRAMSEQ	 1

/* Initialisation data for the Creative PC-CAM 600 */
static const __u16 spca504_pccam600_init_data[][3] = {
/*	{0xa0, 0x0000, 0x0503},  * capture mode */
	{0x00, 0x0000, 0x2000},
	{0x00, 0x0013, 0x2301},
	{0x00, 0x0003, 0x2000},
	{0x00, 0x0001, 0x21ac},
	{0x00, 0x0001, 0x21a6},
	{0x00, 0x0000, 0x21a7},	/* brightness */
	{0x00, 0x0020, 0x21a8},	/* contrast */
	{0x00, 0x0001, 0x21ac},	/* sat/hue */
	{0x00, 0x0000, 0x21ad},	/* hue */
	{0x00, 0x001a, 0x21ae},	/* saturation */
	{0x00, 0x0002, 0x21a3},	/* gamma */
	{0x30, 0x0154, 0x0008},
	{0x30, 0x0004, 0x0006},
	{0x30, 0x0258, 0x0009},
	{0x30, 0x0004, 0x0000},
	{0x30, 0x0093, 0x0004},
	{0x30, 0x0066, 0x0005},
	{0x00, 0x0000, 0x2000},
	{0x00, 0x0013, 0x2301},
	{0x00, 0x0003, 0x2000},
	{0x00, 0x0013, 0x2301},
	{0x00, 0x0003, 0x2000},
	{}
};

/* Creative PC-CAM 600 specific open data, sent before using the
 * generic initialisation data from spca504_open_data.
 */
static const __u16 spca504_pccam600_open_data[][3] = {
	{0x00, 0x0001, 0x2501},
	{0x20, 0x0500, 0x0001},	/* snapshot mode */
	{0x00, 0x0003, 0x2880},
	{0x00, 0x0001, 0x2881},
	{}
};

/* Initialisation data for the logitech clicksmart 420 */
static const __u16 spca504A_clicksmart420_init_data[][3] = {
/*	{0xa0, 0x0000, 0x0503},  * capture mode */
	{0x00, 0x0000, 0x2000},
	{0x00, 0x0013, 0x2301},
	{0x00, 0x0003, 0x2000},
	{0x00, 0x0001, 0x21ac},
	{0x00, 0x0001, 0x21a6},
	{0x00, 0x0000, 0x21a7},	/* brightness */
	{0x00, 0x0020, 0x21a8},	/* contrast */
	{0x00, 0x0001, 0x21ac},	/* sat/hue */
	{0x00, 0x0000, 0x21ad},	/* hue */
	{0x00, 0x001a, 0x21ae},	/* saturation */
	{0x00, 0x0002, 0x21a3},	/* gamma */
	{0x30, 0x0004, 0x000a},
	{0xb0, 0x0001, 0x0000},


	{0x0a1, 0x0080, 0x0001},
	{0x30, 0x0049, 0x0000},
	{0x30, 0x0060, 0x0005},
	{0x0c, 0x0004, 0x0000},
	{0x00, 0x0000, 0x0000},
	{0x00, 0x0000, 0x2000},
	{0x00, 0x0013, 0x2301},
	{0x00, 0x0003, 0x2000},
	{0x00, 0x0000, 0x2000},

	{}
};

/* clicksmart 420 open data ? */
static const __u16 spca504A_clicksmart420_open_data[][3] = {
	{0x00, 0x0001, 0x2501},
	{0x20, 0x0502, 0x0000},
	{0x06, 0x0000, 0x0000},
	{0x00, 0x0004, 0x2880},
	{0x00, 0x0001, 0x2881},
/* look like setting a qTable */
	{0x00, 0x0006, 0x2800},
	{0x00, 0x0004, 0x2801},
	{0x00, 0x0004, 0x2802},
	{0x00, 0x0006, 0x2803},
	{0x00, 0x000a, 0x2804},
	{0x00, 0x0010, 0x2805},
	{0x00, 0x0014, 0x2806},
	{0x00, 0x0018, 0x2807},
	{0x00, 0x0005, 0x2808},
	{0x00, 0x0005, 0x2809},
	{0x00, 0x0006, 0x280a},
	{0x00, 0x0008, 0x280b},
	{0x00, 0x000a, 0x280c},
	{0x00, 0x0017, 0x280d},
	{0x00, 0x0018, 0x280e},
	{0x00, 0x0016, 0x280f},

	{0x00, 0x0006, 0x2810},
	{0x00, 0x0005, 0x2811},
	{0x00, 0x0006, 0x2812},
	{0x00, 0x000a, 0x2813},
	{0x00, 0x0010, 0x2814},
	{0x00, 0x0017, 0x2815},
	{0x00, 0x001c, 0x2816},
	{0x00, 0x0016, 0x2817},
	{0x00, 0x0006, 0x2818},
	{0x00, 0x0007, 0x2819},
	{0x00, 0x0009, 0x281a},
	{0x00, 0x000c, 0x281b},
	{0x00, 0x0014, 0x281c},
	{0x00, 0x0023, 0x281d},
	{0x00, 0x0020, 0x281e},
	{0x00, 0x0019, 0x281f},

	{0x00, 0x0007, 0x2820},
	{0x00, 0x0009, 0x2821},
	{0x00, 0x000f, 0x2822},
	{0x00, 0x0016, 0x2823},
	{0x00, 0x001b, 0x2824},
	{0x00, 0x002c, 0x2825},
	{0x00, 0x0029, 0x2826},
	{0x00, 0x001f, 0x2827},
	{0x00, 0x000a, 0x2828},
	{0x00, 0x000e, 0x2829},
	{0x00, 0x0016, 0x282a},
	{0x00, 0x001a, 0x282b},
	{0x00, 0x0020, 0x282c},
	{0x00, 0x002a, 0x282d},
	{0x00, 0x002d, 0x282e},
	{0x00, 0x0025, 0x282f},

	{0x00, 0x0014, 0x2830},
	{0x00, 0x001a, 0x2831},
	{0x00, 0x001f, 0x2832},
	{0x00, 0x0023, 0x2833},
	{0x00, 0x0029, 0x2834},
	{0x00, 0x0030, 0x2835},
	{0x00, 0x0030, 0x2836},
	{0x00, 0x0028, 0x2837},
	{0x00, 0x001d, 0x2838},
	{0x00, 0x0025, 0x2839},
	{0x00, 0x0026, 0x283a},
	{0x00, 0x0027, 0x283b},
	{0x00, 0x002d, 0x283c},
	{0x00, 0x0028, 0x283d},
	{0x00, 0x0029, 0x283e},
	{0x00, 0x0028, 0x283f},

	{0x00, 0x0007, 0x2840},
	{0x00, 0x0007, 0x2841},
	{0x00, 0x000a, 0x2842},
	{0x00, 0x0013, 0x2843},
	{0x00, 0x0028, 0x2844},
	{0x00, 0x0028, 0x2845},
	{0x00, 0x0028, 0x2846},
	{0x00, 0x0028, 0x2847},
	{0x00, 0x0007, 0x2848},
	{0x00, 0x0008, 0x2849},
	{0x00, 0x000a, 0x284a},
	{0x00, 0x001a, 0x284b},
	{0x00, 0x0028, 0x284c},
	{0x00, 0x0028, 0x284d},
	{0x00, 0x0028, 0x284e},
	{0x00, 0x0028, 0x284f},

	{0x00, 0x000a, 0x2850},
	{0x00, 0x000a, 0x2851},
	{0x00, 0x0016, 0x2852},
	{0x00, 0x0028, 0x2853},
	{0x00, 0x0028, 0x2854},
	{0x00, 0x0028, 0x2855},
	{0x00, 0x0028, 0x2856},
	{0x00, 0x0028, 0x2857},
	{0x00, 0x0013, 0x2858},
	{0x00, 0x001a, 0x2859},
	{0x00, 0x0028, 0x285a},
	{0x00, 0x0028, 0x285b},
	{0x00, 0x0028, 0x285c},
	{0x00, 0x0028, 0x285d},
	{0x00, 0x0028, 0x285e},
	{0x00, 0x0028, 0x285f},

	{0x00, 0x0028, 0x2860},
	{0x00, 0x0028, 0x2861},
	{0x00, 0x0028, 0x2862},
	{0x00, 0x0028, 0x2863},
	{0x00, 0x0028, 0x2864},
	{0x00, 0x0028, 0x2865},
	{0x00, 0x0028, 0x2866},
	{0x00, 0x0028, 0x2867},
	{0x00, 0x0028, 0x2868},
	{0x00, 0x0028, 0x2869},
	{0x00, 0x0028, 0x286a},
	{0x00, 0x0028, 0x286b},
	{0x00, 0x0028, 0x286c},
	{0x00, 0x0028, 0x286d},
	{0x00, 0x0028, 0x286e},
	{0x00, 0x0028, 0x286f},

	{0x00, 0x0028, 0x2870},
	{0x00, 0x0028, 0x2871},
	{0x00, 0x0028, 0x2872},
	{0x00, 0x0028, 0x2873},
	{0x00, 0x0028, 0x2874},
	{0x00, 0x0028, 0x2875},
	{0x00, 0x0028, 0x2876},
	{0x00, 0x0028, 0x2877},
	{0x00, 0x0028, 0x2878},
	{0x00, 0x0028, 0x2879},
	{0x00, 0x0028, 0x287a},
	{0x00, 0x0028, 0x287b},
	{0x00, 0x0028, 0x287c},
	{0x00, 0x0028, 0x287d},
	{0x00, 0x0028, 0x287e},
	{0x00, 0x0028, 0x287f},

	{0xa0, 0x0000, 0x0503},
	{}
};

static const __u8 qtable_creative_pccam[2][64] = {
	{				/* Q-table Y-components */
	 0x05, 0x03, 0x03, 0x05, 0x07, 0x0c, 0x0f, 0x12,
	 0x04, 0x04, 0x04, 0x06, 0x08, 0x11, 0x12, 0x11,
	 0x04, 0x04, 0x05, 0x07, 0x0c, 0x11, 0x15, 0x11,
	 0x04, 0x05, 0x07, 0x09, 0x0f, 0x1a, 0x18, 0x13,
	 0x05, 0x07, 0x0b, 0x11, 0x14, 0x21, 0x1f, 0x17,
	 0x07, 0x0b, 0x11, 0x13, 0x18, 0x1f, 0x22, 0x1c,
	 0x0f, 0x13, 0x17, 0x1a, 0x1f, 0x24, 0x24, 0x1e,
	 0x16, 0x1c, 0x1d, 0x1d, 0x22, 0x1e, 0x1f, 0x1e},
	{				/* Q-table C-components */
	 0x05, 0x05, 0x07, 0x0e, 0x1e, 0x1e, 0x1e, 0x1e,
	 0x05, 0x06, 0x08, 0x14, 0x1e, 0x1e, 0x1e, 0x1e,
	 0x07, 0x08, 0x11, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e,
	 0x0e, 0x14, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e,
	 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e,
	 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e,
	 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e,
	 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e}
};

/* FIXME: This Q-table is identical to the Creative PC-CAM one,
 *		except for one byte. Possibly a typo?
 *		NWG: 18/05/2003.
 */
static const __u8 qtable_spca504_default[2][64] = {
	{				/* Q-table Y-components */
	 0x05, 0x03, 0x03, 0x05, 0x07, 0x0c, 0x0f, 0x12,
	 0x04, 0x04, 0x04, 0x06, 0x08, 0x11, 0x12, 0x11,
	 0x04, 0x04, 0x05, 0x07, 0x0c, 0x11, 0x15, 0x11,
	 0x04, 0x05, 0x07, 0x09, 0x0f, 0x1a, 0x18, 0x13,
	 0x05, 0x07, 0x0b, 0x11, 0x14, 0x21, 0x1f, 0x17,
	 0x07, 0x0b, 0x11, 0x13, 0x18, 0x1f, 0x22, 0x1c,
	 0x0f, 0x13, 0x17, 0x1a, 0x1f, 0x24, 0x24, 0x1e,
	 0x16, 0x1c, 0x1d, 0x1d, 0x1d /* 0x22 */ , 0x1e, 0x1f, 0x1e,
	 },
	{				/* Q-table C-components */
	 0x05, 0x05, 0x07, 0x0e, 0x1e, 0x1e, 0x1e, 0x1e,
	 0x05, 0x06, 0x08, 0x14, 0x1e, 0x1e, 0x1e, 0x1e,
	 0x07, 0x08, 0x11, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e,
	 0x0e, 0x14, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e,
	 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e,
	 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e,
	 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e,
	 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e}
};

static void spca5xxRegRead(struct usb_device *dev,
			   __u16 req,
			   __u16 index,
			   __u8 *buffer, __u16 length)
{
	usb_control_msg(dev,
			usb_rcvctrlpipe(dev, 0),
			req,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0,		/* value */
			index, buffer, length,
			500);
}

static void spca5xxRegWrite(struct usb_device *dev,
			    __u16 req,
			    __u16 value,
			    __u16 index,
			    __u8 *buffer, __u16 length)
{
	usb_control_msg(dev,
			usb_sndctrlpipe(dev, 0),
			req,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			value, index, buffer, length,
			500);
}

static int reg_write(struct usb_device *dev,
		     __u16 req, __u16 index, __u16 value)
{
	int ret;

	ret = usb_control_msg(dev,
			usb_sndctrlpipe(dev, 0),
			req,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			value, index, NULL, 0, 500);
	PDEBUG(D_PACK, "reg write: 0x%02x,0x%02x:0x%02x, 0x%x",
		req, index, value, ret);
	if (ret < 0)
		PDEBUG(D_ERR, "reg write: error %d", ret);
	return ret;
}

static int reg_read_info(struct usb_device *dev,
			__u16 value)	/* wValue */
{
	int ret;
	__u8 data;

	ret = usb_control_msg(dev,
			usb_rcvctrlpipe(dev, 0),
			0x20,			/* request */
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			value,
			0,			/* index */
			&data, 1,
			500);			/* timeout */
	if (ret < 0) {
		PDEBUG(D_ERR, "reg_read_info err %d", ret);
		return 0;
	}
	return data;
}

/* returns: negative is error, pos or zero is data */
static int reg_read(struct usb_device *dev,
			__u16 req,	/* bRequest */
			__u16 index,	/* wIndex */
			__u16 length)	/* wLength (1 or 2 only) */
{
	int ret;
	__u8 buf[2];

	buf[1] = 0;
	ret = usb_control_msg(dev,
			usb_rcvctrlpipe(dev, 0),
			req,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0,		/* value */
			index,
			buf, length,
			500);
	if (ret < 0) {
		PDEBUG(D_ERR, "reg_read err %d", ret);
		return -1;
	}
	return (buf[1] << 8) + buf[0];
}

static int write_vector(struct gspca_dev *gspca_dev,
			const __u16 data[][3])
{
	struct usb_device *dev = gspca_dev->dev;
	int ret, i = 0;

	while (data[i][0] != 0 || data[i][1] != 0 || data[i][2] != 0) {
		ret = reg_write(dev, data[i][0], data[i][2], data[i][1]);
		if (ret < 0) {
			PDEBUG(D_ERR,
				"Register write failed for 0x%x,0x%x,0x%x",
				data[i][0], data[i][1], data[i][2]);
			return ret;
		}
		i++;
	}
	return 0;
}

static int spca50x_setup_qtable(struct gspca_dev *gspca_dev,
				unsigned int request,
				unsigned int ybase,
				unsigned int cbase,
				const __u8 qtable[2][64])
{
	struct usb_device *dev = gspca_dev->dev;
	int i, err;

	/* loop over y components */
	for (i = 0; i < 64; i++) {
		err = reg_write(dev, request, ybase + i, qtable[0][i]);
		if (err < 0)
			return err;
	}

	/* loop over c components */
	for (i = 0; i < 64; i++) {
		err = reg_write(dev, request, cbase + i, qtable[1][i]);
		if (err < 0)
			return err;
	}
	return 0;
}

static void spca504_acknowledged_command(struct gspca_dev *gspca_dev,
			     __u16 req, __u16 idx, __u16 val)
{
	struct usb_device *dev = gspca_dev->dev;
	__u8 notdone;

	reg_write(dev, req, idx, val);
	notdone = reg_read(dev, 0x01, 0x0001, 1);
	reg_write(dev, req, idx, val);

	PDEBUG(D_FRAM, "before wait 0x%x", notdone);

	msleep(200);
	notdone = reg_read(dev, 0x01, 0x0001, 1);
	PDEBUG(D_FRAM, "after wait 0x%x", notdone);
}

static void spca504A_acknowledged_command(struct gspca_dev *gspca_dev,
			__u16 req,
			__u16 idx, __u16 val, __u8 stat, __u8 count)
{
	struct usb_device *dev = gspca_dev->dev;
	__u8 status;
	__u8 endcode;

	reg_write(dev, req, idx, val);
	status = reg_read(dev, 0x01, 0x0001, 1);
	endcode = stat;
	PDEBUG(D_FRAM, "Status 0x%x Need 0x%x", status, stat);
	if (!count)
		return;
	count = 200;
	while (--count > 0) {
		msleep(10);
		/* gsmart mini2 write a each wait setting 1 ms is enought */
/*		reg_write(dev, req, idx, val); */
		status = reg_read(dev, 0x01, 0x0001, 1);
		if (status == endcode) {
			PDEBUG(D_FRAM, "status 0x%x after wait 0x%x",
				status, 200 - count);
				break;
		}
	}
}

static int spca504B_PollingDataReady(struct usb_device *dev)
{
	__u8 DataReady;
	int count = 10;

	while (--count > 0) {
		spca5xxRegRead(dev, 0x21, 0, &DataReady, 1);
		if ((DataReady & 0x01) == 0)
			break;
		msleep(10);
	}
	return DataReady;
}

static void spca504B_WaitCmdStatus(struct gspca_dev *gspca_dev)
{
	struct usb_device *dev = gspca_dev->dev;
	__u8 DataReady;
	int count = 50;

	while (--count > 0) {
		spca5xxRegRead(dev, 0x21, 1, &DataReady, 1);

		if (DataReady) {
			DataReady = 0;
			spca5xxRegWrite(dev, 0x21, 0, 1, &DataReady, 1);
			spca5xxRegRead(dev, 0x21, 1, &DataReady, 1);
			spca504B_PollingDataReady(dev);
			break;
		}
		msleep(10);
	}
}

static void spca50x_GetFirmware(struct gspca_dev *gspca_dev)
{
	struct usb_device *dev = gspca_dev->dev;
	__u8 FW[5];
	__u8 ProductInfo[64];

	spca5xxRegRead(dev, 0x20, 0, FW, 5);
	PDEBUG(D_STREAM, "FirmWare : %d %d %d %d %d ",
		FW[0], FW[1], FW[2], FW[3], FW[4]);
	spca5xxRegRead(dev, 0x23, 0, ProductInfo, 64);
	spca5xxRegRead(dev, 0x23, 1, ProductInfo, 64);
}

static void spca504B_SetSizeType(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct usb_device *dev = gspca_dev->dev;
	__u8 Size;
	__u8 Type;
	int rc;

	Size = gspca_dev->cam.cam_mode[(int) gspca_dev->curr_mode].priv;
	Type = 0;
	switch (sd->bridge) {
	case BRIDGE_SPCA533:
		spca5xxRegWrite(dev, 0x31, 0, 0, NULL, 0);
		spca504B_WaitCmdStatus(gspca_dev);
		rc = spca504B_PollingDataReady(dev);
		spca50x_GetFirmware(gspca_dev);
		Type = 2;
		spca5xxRegWrite(dev, 0x24, 0, 8, &Type, 1);
		spca5xxRegRead(dev, 0x24, 8, &Type, 1);

		spca5xxRegWrite(dev, 0x25, 0, 4, &Size, 1);
		spca5xxRegRead(dev, 0x25, 4, &Size, 1);
		rc = spca504B_PollingDataReady(dev);

		/* Init the cam width height with some values get on init ? */
		spca5xxRegWrite(dev, 0x31, 0, 4, NULL, 0);
		spca504B_WaitCmdStatus(gspca_dev);
		rc = spca504B_PollingDataReady(dev);
		break;
	default:
/* case BRIDGE_SPCA504B: */
/* case BRIDGE_SPCA536: */
		Type = 6;
		spca5xxRegWrite(dev, 0x25, 0, 4, &Size, 1);
		spca5xxRegRead(dev, 0x25, 4, &Size, 1);
		spca5xxRegWrite(dev, 0x27, 0, 0, &Type, 1);
		spca5xxRegRead(dev, 0x27, 0, &Type, 1);
		rc = spca504B_PollingDataReady(dev);
		break;
	case BRIDGE_SPCA504:
		Size += 3;
		if (sd->subtype == AiptekMiniPenCam13) {
			/* spca504a aiptek */
			spca504A_acknowledged_command(gspca_dev,
						0x08, Size, 0,
						0x80 | (Size & 0x0f), 1);
			spca504A_acknowledged_command(gspca_dev,
							1, 3, 0, 0x9f, 0);
		} else {
			spca504_acknowledged_command(gspca_dev, 0x08, Size, 0);
		}
		break;
	case BRIDGE_SPCA504C:
		/* capture mode */
		reg_write(dev, 0xa0, (0x0500 | (Size & 0x0f)), 0x0);
		reg_write(dev, 0x20, 0x01, 0x0500 | (Size & 0x0f));
		break;
	}
}

static void spca504_wait_status(struct gspca_dev *gspca_dev)
{
	struct usb_device *dev = gspca_dev->dev;
	int cnt;

	cnt = 256;
	while (--cnt > 0) {
		/* With this we get the status, when return 0 it's all ok */
		if (reg_read(dev, 0x06, 0x00, 1) == 0)
			return;
		msleep(10);
	}
}

static void spca504B_setQtable(struct gspca_dev *gspca_dev)
{
	struct usb_device *dev = gspca_dev->dev;
	__u8 Data = 3;

	spca5xxRegWrite(dev, 0x26, 0, 0, &Data, 1);
	spca5xxRegRead(dev, 0x26, 0, &Data, 1);
	spca504B_PollingDataReady(dev);
}

static void sp5xx_initContBrigHueRegisters(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct usb_device *dev = gspca_dev->dev;
	int pollreg = 1;

	switch (sd->bridge) {
	case BRIDGE_SPCA504:
	case BRIDGE_SPCA504C:
		pollreg = 0;
		/* fall thru */
	default:
/*	case BRIDGE_SPCA533: */
/*	case BRIDGE_SPCA504B: */
		spca5xxRegWrite(dev, 0, 0, 0x21a7, NULL, 0);	/* brightness */
		spca5xxRegWrite(dev, 0, 0x20, 0x21a8, NULL, 0);	/* contrast */
		spca5xxRegWrite(dev, 0, 0, 0x21ad, NULL, 0);	/* hue */
		spca5xxRegWrite(dev, 0, 1, 0x21ac, NULL, 0);	/* sat/hue */
		spca5xxRegWrite(dev, 0, 0x20, 0x21ae, NULL, 0);	/* saturation */
		spca5xxRegWrite(dev, 0, 0, 0x21a3, NULL, 0);	/* gamma */
		break;
	case BRIDGE_SPCA536:
		spca5xxRegWrite(dev, 0, 0, 0x20f0, NULL, 0);
		spca5xxRegWrite(dev, 0, 0x21, 0x20f1, NULL, 0);
		spca5xxRegWrite(dev, 0, 0x40, 0x20f5, NULL, 0);
		spca5xxRegWrite(dev, 0, 1, 0x20f4, NULL, 0);
		spca5xxRegWrite(dev, 0, 0x40, 0x20f6, NULL, 0);
		spca5xxRegWrite(dev, 0, 0, 0x2089, NULL, 0);
		break;
	}
	if (pollreg)
		spca504B_PollingDataReady(dev);
}

/* this function is called at probe time */
static int sd_config(struct gspca_dev *gspca_dev,
			const struct usb_device_id *id)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct usb_device *dev = gspca_dev->dev;
	struct cam *cam;
	__u16 vendor;
	__u16 product;
	__u8 fw;

	vendor = id->idVendor;
	product = id->idProduct;
	switch (vendor) {
	case 0x041e:		/* Creative cameras */
/*		switch (product) { */
/*		case 0x400b: */
/*		case 0x4012: */
/*		case 0x4013: */
/*			sd->bridge = BRIDGE_SPCA504C; */
/*			break; */
/*		} */
		break;
	case 0x0458:		/* Genius KYE cameras */
/*		switch (product) { */
/*		case 0x7006: */
			sd->bridge = BRIDGE_SPCA504B;
/*			break; */
/*		} */
		break;
	case 0x046d:		/* Logitech Labtec */
		switch (product) {
		case 0x0905:
			sd->subtype = LogitechClickSmart820;
			sd->bridge = BRIDGE_SPCA533;
			break;
		case 0x0960:
			sd->subtype = LogitechClickSmart420;
			sd->bridge = BRIDGE_SPCA504C;
			break;
		}
		break;
	case 0x0471:				/* Philips */
/*		switch (product) { */
/*		case 0x0322: */
			sd->bridge = BRIDGE_SPCA504B;
/*			break; */
/*		} */
		break;
	case 0x04a5:		/* Benq */
		switch (product) {
		case 0x3003:
			sd->bridge = BRIDGE_SPCA504B;
			break;
		case 0x3008:
		case 0x300a:
			sd->bridge = BRIDGE_SPCA533;
			break;
		}
		break;
	case 0x04f1:		/* JVC */
/*		switch (product) { */
/*		case 0x1001: */
			sd->bridge = BRIDGE_SPCA504B;
/*			break; */
/*		} */
		break;
	case 0x04fc:		/* SunPlus */
		switch (product) {
		case 0x500c:
			sd->bridge = BRIDGE_SPCA504B;
			break;
		case 0x504a:
/* try to get the firmware as some cam answer 2.0.1.2.2
 * and should be a spca504b then overwrite that setting */
			spca5xxRegRead(dev, 0x20, 0, &fw, 1);
			if (fw == 1) {
				sd->subtype = AiptekMiniPenCam13;
				sd->bridge = BRIDGE_SPCA504;
			} else if (fw == 2) {
				sd->bridge = BRIDGE_SPCA504B;
			} else
				return -ENODEV;
			break;
		case 0x504b:
			sd->bridge = BRIDGE_SPCA504B;
			break;
		case 0x5330:
			sd->bridge = BRIDGE_SPCA533;
			break;
		case 0x5360:
			sd->bridge = BRIDGE_SPCA536;
			break;
		case 0xffff:
			sd->bridge = BRIDGE_SPCA504B;
			break;
		}
		break;
	case 0x052b:		/* ?? Megapix */
/*		switch (product) { */
/*		case 0x1513: */
			sd->subtype = MegapixV4;
			sd->bridge = BRIDGE_SPCA533;
/*			break; */
/*		} */
		break;
	case 0x0546:		/* Polaroid */
		switch (product) {
		case 0x3155:
			sd->bridge = BRIDGE_SPCA533;
			break;
		case 0x3191:
		case 0x3273:
			sd->bridge = BRIDGE_SPCA504B;
			break;
		}
		break;
	case 0x055f:		/* Mustek cameras */
		switch (product) {
		case 0xc211:
			sd->bridge = BRIDGE_SPCA536;
			break;
		case 0xc230:
		case 0xc232:
			sd->bridge = BRIDGE_SPCA533;
			break;
		case 0xc360:
			sd->bridge = BRIDGE_SPCA536;
			break;
		case 0xc420:
			sd->bridge = BRIDGE_SPCA504;
			break;
		case 0xc430:
		case 0xc440:
			sd->bridge = BRIDGE_SPCA533;
			break;
		case 0xc520:
			sd->bridge = BRIDGE_SPCA504;
			break;
		case 0xc530:
		case 0xc540:
		case 0xc630:
		case 0xc650:
			sd->bridge = BRIDGE_SPCA533;
			break;
		}
		break;
	case 0x05da:		/* Digital Dream cameras */
/*		switch (product) { */
/*		case 0x1018: */
			sd->bridge = BRIDGE_SPCA504B;
/*			break; */
/*		} */
		break;
	case 0x06d6:		/* Trust */
/*		switch (product) { */
/*		case 0x0031: */
			sd->bridge = BRIDGE_SPCA533;	/* SPCA533A */
/*			break; */
/*		} */
		break;
	case 0x0733:	/* Rebadged ViewQuest (Intel) and ViewQuest cameras */
		switch (product) {
		case 0x1311:
		case 0x1314:
		case 0x2211:
		case 0x2221:
			sd->bridge = BRIDGE_SPCA533;
			break;
		case 0x3261:
		case 0x3281:
			sd->bridge = BRIDGE_SPCA536;
			break;
		}
		break;
	case 0x08ca:		/* Aiptek */
		switch (product) {
		case 0x0104:
		case 0x0106:
			sd->bridge = BRIDGE_SPCA533;
			break;
		case 0x2008:
			sd->bridge = BRIDGE_SPCA504B;
			break;
		case 0x2010:
			sd->bridge = BRIDGE_SPCA533;
			break;
		case 0x2016:
		case 0x2018:
			sd->bridge = BRIDGE_SPCA504B;
			break;
		case 0x2020:
		case 0x2022:
			sd->bridge = BRIDGE_SPCA533;
			break;
		case 0x2024:
			sd->bridge = BRIDGE_SPCA536;
			break;
		case 0x2028:
			sd->bridge = BRIDGE_SPCA533;
			break;
		case 0x2040:
		case 0x2042:
		case 0x2060:
			sd->bridge = BRIDGE_SPCA536;
			break;
		}
		break;
	case 0x0d64:		/* SunPlus */
/*		switch (product) { */
/*		case 0x0303: */
			sd->bridge = BRIDGE_SPCA536;
/*			break; */
/*		} */
		break;
	}

	cam = &gspca_dev->cam;
	cam->dev_name = (char *) id->driver_info;
	cam->epaddr = 0x01;

	switch (sd->bridge) {
	default:
/*	case BRIDGE_SPCA504B: */
/*	case BRIDGE_SPCA504: */
/*	case BRIDGE_SPCA536: */
		cam->cam_mode = vga_mode;
		cam->nmodes = sizeof vga_mode / sizeof vga_mode[0];
		break;
	case BRIDGE_SPCA533:
		cam->cam_mode = custom_mode;
		cam->nmodes = sizeof custom_mode / sizeof custom_mode[0];
		break;
	case BRIDGE_SPCA504C:
		cam->cam_mode = vga_mode2;
		cam->nmodes = sizeof vga_mode2 / sizeof vga_mode2[0];
		break;
	}
	sd->qindex = 5;			/* set the quantization table */
	sd->brightness = sd_ctrls[SD_BRIGHTNESS].qctrl.default_value;
	sd->contrast = sd_ctrls[SD_CONTRAST].qctrl.default_value;
	sd->colors = sd_ctrls[SD_COLOR].qctrl.default_value;
	return 0;
}

/* this function is called at open time */
static int sd_open(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct usb_device *dev = gspca_dev->dev;
	int rc;
	__u8 Data;
	__u8 i;
	__u8 info[6];
	int err_code;

	switch (sd->bridge) {
	case BRIDGE_SPCA504B:
		spca5xxRegWrite(dev, 0x1d, 0, 0, NULL, 0);
		spca5xxRegWrite(dev, 0, 1, 0x2306, NULL, 0);
		spca5xxRegWrite(dev, 0, 0, 0x0d04, NULL, 0);
		spca5xxRegWrite(dev, 0, 0, 0x2000, NULL, 0);
		spca5xxRegWrite(dev, 0, 0x13, 0x2301, NULL, 0);
		spca5xxRegWrite(dev, 0, 0, 0x2306, NULL, 0);
		/* fall thru */
	case BRIDGE_SPCA533:
		rc = spca504B_PollingDataReady(dev);
		spca50x_GetFirmware(gspca_dev);
		break;
	case BRIDGE_SPCA536:
		spca50x_GetFirmware(gspca_dev);
		spca5xxRegRead(dev, 0x00, 0x5002, &Data, 1);
		Data = 0;
		spca5xxRegWrite(dev, 0x24, 0, 0, &Data, 1);
		spca5xxRegRead(dev, 0x24, 0, &Data, 1);
		rc = spca504B_PollingDataReady(dev);
		spca5xxRegWrite(dev, 0x34, 0, 0, NULL, 0);
		spca504B_WaitCmdStatus(gspca_dev);
		break;
	case BRIDGE_SPCA504C:	/* pccam600 */
		PDEBUG(D_STREAM, "Opening SPCA504 (PC-CAM 600)");
		reg_write(dev, 0xe0, 0x0000, 0x0000);
		reg_write(dev, 0xe0, 0x0000, 0x0001);	/* reset */
		spca504_wait_status(gspca_dev);
		if (sd->subtype == LogitechClickSmart420)
			write_vector(gspca_dev,
					spca504A_clicksmart420_open_data);
		else
			write_vector(gspca_dev, spca504_pccam600_open_data);
		err_code = spca50x_setup_qtable(gspca_dev,
						0x00, 0x2800,
						0x2840, qtable_creative_pccam);
		if (err_code < 0) {
			PDEBUG(D_ERR|D_STREAM, "spca50x_setup_qtable failed");
			return err_code;
		}
		break;
	default:
/*	case BRIDGE_SPCA504: */
		PDEBUG(D_STREAM, "Opening SPCA504");
		if (sd->subtype == AiptekMiniPenCam13) {
			/*****************************/
			for (i = 0; i < 6; i++)
				info[i] = reg_read_info(dev, i);
			PDEBUG(D_STREAM,
				"Read info: %d %d %d %d %d %d."
				" Should be 1,0,2,2,0,0",
				info[0], info[1], info[2],
				info[3], info[4], info[5]);
			/* spca504a aiptek */
			/* Set AE AWB Banding Type 3-> 50Hz 2-> 60Hz */
			spca504A_acknowledged_command(gspca_dev, 0x24,
							8, 3, 0x9e, 1);
			/* Twice sequencial need status 0xff->0x9e->0x9d */
			spca504A_acknowledged_command(gspca_dev, 0x24,
							8, 3, 0x9e, 0);

			spca504A_acknowledged_command(gspca_dev, 0x24,
							0, 0, 0x9d, 1);
			/******************************/
			/* spca504a aiptek */
			spca504A_acknowledged_command(gspca_dev, 0x08,
							6, 0, 0x86, 1);
/*			reg_write (dev, 0, 0x2000, 0); */
/*			reg_write (dev, 0, 0x2883, 1); */
/*			spca504A_acknowledged_command (gspca_dev, 0x08,
							6, 0, 0x86, 1); */
/*			spca504A_acknowledged_command (gspca_dev, 0x24,
							0, 0, 0x9D, 1); */
			reg_write(dev, 0x0, 0x270c, 0x5); /* L92 sno1t.txt */
			reg_write(dev, 0x0, 0x2310, 0x5);
			spca504A_acknowledged_command(gspca_dev, 0x01,
							0x0f, 0, 0xff, 0);
		}
		/* setup qtable */
		reg_write(dev, 0, 0x2000, 0);
		reg_write(dev, 0, 0x2883, 1);
		err_code = spca50x_setup_qtable(gspca_dev,
						0x00, 0x2800,
						0x2840,
						qtable_spca504_default);
		if (err_code < 0) {
			PDEBUG(D_ERR, "spca50x_setup_qtable failed");
			return err_code;
		}
		break;
	}
	return 0;
}

static void sd_start(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct usb_device *dev = gspca_dev->dev;
	int rc;
	int enable;
	__u8 i;
	__u8 info[6];

	if (sd->bridge == BRIDGE_SPCA504B)
		spca504B_setQtable(gspca_dev);
	spca504B_SetSizeType(gspca_dev);
	switch (sd->bridge) {
	default:
/*	case BRIDGE_SPCA504B: */
/*	case BRIDGE_SPCA533: */
/*	case BRIDGE_SPCA536: */
		if (sd->subtype == MegapixV4 ||
		    sd->subtype == LogitechClickSmart820) {
			spca5xxRegWrite(dev, 0xf0, 0, 0, NULL, 0);
			spca504B_WaitCmdStatus(gspca_dev);
			spca5xxRegRead(dev, 0xf0, 4, NULL, 0);
			spca504B_WaitCmdStatus(gspca_dev);
		} else {
			spca5xxRegWrite(dev, 0x31, 0, 4, NULL, 0);
			spca504B_WaitCmdStatus(gspca_dev);
			rc = spca504B_PollingDataReady(dev);
		}
		break;
	case BRIDGE_SPCA504:
		if (sd->subtype == AiptekMiniPenCam13) {
			for (i = 0; i < 6; i++)
				info[i] = reg_read_info(dev, i);
			PDEBUG(D_STREAM,
				"Read info: %d %d %d %d %d %d."
				" Should be 1,0,2,2,0,0",
				info[0], info[1], info[2],
				info[3], info[4], info[5]);
			/* spca504a aiptek */
			/* Set AE AWB Banding Type 3-> 50Hz 2-> 60Hz */
			spca504A_acknowledged_command(gspca_dev, 0x24,
							8, 3, 0x9e, 1);
			/* Twice sequencial need status 0xff->0x9e->0x9d */
			spca504A_acknowledged_command(gspca_dev, 0x24,
							8, 3, 0x9e, 0);
			spca504A_acknowledged_command(gspca_dev, 0x24,
							0, 0, 0x9d, 1);
		} else {
			spca504_acknowledged_command(gspca_dev, 0x24, 8, 3);
			for (i = 0; i < 6; i++)
				info[i] = reg_read_info(dev, i);
			PDEBUG(D_STREAM,
				"Read info: %d %d %d %d %d %d."
				" Should be 1,0,2,2,0,0",
				info[0], info[1], info[2],
				info[3], info[4], info[5]);
			spca504_acknowledged_command(gspca_dev, 0x24, 8, 3);
			spca504_acknowledged_command(gspca_dev, 0x24, 0, 0);
		}
		spca504B_SetSizeType(gspca_dev);
		reg_write(dev, 0x0, 0x270c, 0x5);	/* L92 sno1t.txt */
		reg_write(dev, 0x0, 0x2310, 0x5);
		break;
	case BRIDGE_SPCA504C:
		if (sd->subtype == LogitechClickSmart420) {
			write_vector(gspca_dev,
					spca504A_clicksmart420_init_data);
		} else {
			write_vector(gspca_dev, spca504_pccam600_init_data);
		}
		enable = (sd->autogain ? 0x4 : 0x1);
		reg_write(dev, 0x0c, 0x0000, enable);	/* auto exposure */
		reg_write(dev, 0xb0, 0x0000, enable);	/* auto whiteness */

		/* set default exposure compensation and whiteness balance */
		reg_write(dev, 0x30, 0x0001, 800);	/* ~ 20 fps */
		reg_write(dev, 0x30, 0x0002, 1600);
		spca504B_SetSizeType(gspca_dev);
		break;
	}
	sp5xx_initContBrigHueRegisters(gspca_dev);
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct usb_device *dev = gspca_dev->dev;

	switch (sd->bridge) {
	default:
/*	case BRIDGE_SPCA533: */
/*	case BRIDGE_SPCA536: */
/*	case BRIDGE_SPCA504B: */
		spca5xxRegWrite(dev, 0x31, 0, 0, NULL, 0);
		spca504B_WaitCmdStatus(gspca_dev);
		spca504B_PollingDataReady(dev);
		break;
	case BRIDGE_SPCA504:
	case BRIDGE_SPCA504C:
		reg_write(dev, 0x00, 0x2000, 0x0000);

		if (sd->subtype == AiptekMiniPenCam13) {
			/* spca504a aiptek */
/*			spca504A_acknowledged_command(gspca_dev, 0x08,
							 6, 0, 0x86, 1); */
			spca504A_acknowledged_command(gspca_dev, 0x24,
							0x00, 0x00, 0x9d, 1);
			spca504A_acknowledged_command(gspca_dev, 0x01,
							0x0f, 0x00, 0xff, 1);
		} else {
			spca504_acknowledged_command(gspca_dev, 0x24, 0, 0);
			reg_write(dev, 0x01, 0x000f, 0x0);
		}
		break;
	}
}

static void sd_stop0(struct gspca_dev *gspca_dev)
{
}

static void sd_close(struct gspca_dev *gspca_dev)
{
}

static void sd_pkt_scan(struct gspca_dev *gspca_dev,
			struct gspca_frame *frame,	/* target */
			__u8 *data,			/* isoc packet */
			int len)			/* iso packet length */
{
	struct sd *sd = (struct sd *) gspca_dev;
	int i, sof = 0;
	unsigned char *s, *d;
	static unsigned char ffd9[] = {0xff, 0xd9};

/* frames are jpeg 4.1.1 without 0xff escape */
	switch (sd->bridge) {
	case BRIDGE_SPCA533:
		if (data[0] == 0xff) {
			if (data[1] != 0x01) {	/* drop packet */
/*				gspca_dev->last_packet_type = DISCARD_PACKET; */
				return;
			}
			sof = 1;
			data += SPCA533_OFFSET_DATA;
			len -= SPCA533_OFFSET_DATA;
		} else {
			data += 1;
			len -= 1;
		}
		break;
	case BRIDGE_SPCA536:
		if (data[0] == 0xff) {
			sof = 1;
			data += SPCA536_OFFSET_DATA;
			len -= SPCA536_OFFSET_DATA;
		} else {
			data += 2;
			len -= 2;
		}
		break;
	default:
/*	case BRIDGE_SPCA504: */
/*	case BRIDGE_SPCA504B: */
		switch (data[0]) {
		case 0xfe:			/* start of frame */
			sof = 1;
			data += SPCA50X_OFFSET_DATA;
			len -= SPCA50X_OFFSET_DATA;
			break;
		case 0xff:			/* drop packet */
/*			gspca_dev->last_packet_type = DISCARD_PACKET; */
			return;
		default:
			data += 1;
			len -= 1;
			break;
		}
		break;
	case BRIDGE_SPCA504C:
		switch (data[0]) {
		case 0xfe:			/* start of frame */
			sof = 1;
			data += SPCA504_PCCAM600_OFFSET_DATA;
			len -= SPCA504_PCCAM600_OFFSET_DATA;
			break;
		case 0xff:			/* drop packet */
/*			gspca_dev->last_packet_type = DISCARD_PACKET; */
			return;
		default:
			data += 1;
			len -= 1;
			break;
		}
		break;
	}
	if (sof) {		/* start of frame */
		frame = gspca_frame_add(gspca_dev, LAST_PACKET, frame,
					ffd9, 2);

		/* put the JPEG header in the new frame */
		jpeg_put_header(gspca_dev, frame,
				((struct sd *) gspca_dev)->qindex,
				0x22);
	}

	/* add 0x00 after 0xff */
	for (i = len; --i >= 0; )
		if (data[i] == 0xff)
			break;
	if (i < 0) {			/* no 0xff */
		gspca_frame_add(gspca_dev, INTER_PACKET, frame, data, len);
		return;
	}
	s = data;
	d = sd->packet;
	for (i = 0; i < len; i++) {
		*d++ = *s++;
		if (s[-1] == 0xff)
			*d++ = 0x00;
	}
	gspca_frame_add(gspca_dev, INTER_PACKET, frame,
			sd->packet, d - sd->packet);
}

static void setbrightness(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct usb_device *dev = gspca_dev->dev;

	switch (sd->bridge) {
	default:
/*	case BRIDGE_SPCA533: */
/*	case BRIDGE_SPCA504B: */
/*	case BRIDGE_SPCA504: */
/*	case BRIDGE_SPCA504C: */
		reg_write(dev, 0x0, 0x21a7, sd->brightness);
		break;
	case BRIDGE_SPCA536:
		reg_write(dev, 0x0, 0x20f0, sd->brightness);
		break;
	}
}

static void getbrightness(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct usb_device *dev = gspca_dev->dev;
	__u16 brightness = 0;

	switch (sd->bridge) {
	default:
/*	case BRIDGE_SPCA533: */
/*	case BRIDGE_SPCA504B: */
/*	case BRIDGE_SPCA504: */
/*	case BRIDGE_SPCA504C: */
		brightness = reg_read(dev, 0x0, 0x21a7, 2);
		break;
	case BRIDGE_SPCA536:
		brightness = reg_read(dev, 0x0, 0x20f0, 2);
		break;
	}
	sd->brightness = ((brightness & 0xff) - 128) % 255;
}

static void setcontrast(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct usb_device *dev = gspca_dev->dev;

	switch (sd->bridge) {
	default:
/*	case BRIDGE_SPCA533: */
/*	case BRIDGE_SPCA504B: */
/*	case BRIDGE_SPCA504: */
/*	case BRIDGE_SPCA504C: */
		reg_write(dev, 0x0, 0x21a8, sd->contrast);
		break;
	case BRIDGE_SPCA536:
		reg_write(dev, 0x0, 0x20f1, sd->contrast);
		break;
	}
}

static void getcontrast(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct usb_device *dev = gspca_dev->dev;

	switch (sd->bridge) {
	default:
/*	case BRIDGE_SPCA533: */
/*	case BRIDGE_SPCA504B: */
/*	case BRIDGE_SPCA504: */
/*	case BRIDGE_SPCA504C: */
		sd->contrast = reg_read(dev, 0x0, 0x21a8, 2);
		break;
	case BRIDGE_SPCA536:
		sd->contrast = reg_read(dev, 0x0, 0x20f1, 2);
		break;
	}
}

static void setcolors(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct usb_device *dev = gspca_dev->dev;

	switch (sd->bridge) {
	default:
/*	case BRIDGE_SPCA533: */
/*	case BRIDGE_SPCA504B: */
/*	case BRIDGE_SPCA504: */
/*	case BRIDGE_SPCA504C: */
		reg_write(dev, 0x0, 0x21ae, sd->colors);
		break;
	case BRIDGE_SPCA536:
		reg_write(dev, 0x0, 0x20f6, sd->colors);
		break;
	}
}

static void getcolors(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct usb_device *dev = gspca_dev->dev;

	switch (sd->bridge) {
	default:
/*	case BRIDGE_SPCA533: */
/*	case BRIDGE_SPCA504B: */
/*	case BRIDGE_SPCA504: */
/*	case BRIDGE_SPCA504C: */
		sd->colors = reg_read(dev, 0x0, 0x21ae, 2) >> 1;
		break;
	case BRIDGE_SPCA536:
		sd->colors = reg_read(dev, 0x0, 0x20f6, 2) >> 1;
		break;
	}
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

	getbrightness(gspca_dev);
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

	getcontrast(gspca_dev);
	*val = sd->contrast;
	return 0;
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

	getcolors(gspca_dev);
	*val = sd->colors;
	return 0;
}

static int sd_setautogain(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->autogain = val;
	return 0;
}

static int sd_getautogain(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->autogain;
	return 0;
}

/* sub-driver description */
static const struct sd_desc sd_desc = {
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
static const __devinitdata struct usb_device_id device_table[] = {
	{USB_DEVICE(0x041e, 0x400b), DVNM("Creative PC-CAM 600")},
	{USB_DEVICE(0x041e, 0x4012), DVNM("PC-Cam350")},
	{USB_DEVICE(0x041e, 0x4013), DVNM("Creative Pccam750")},
	{USB_DEVICE(0x0458, 0x7006), DVNM("Genius Dsc 1.3 Smart")},
	{USB_DEVICE(0x046d, 0x0905), DVNM("Logitech ClickSmart 820")},
	{USB_DEVICE(0x046d, 0x0960), DVNM("Logitech ClickSmart 420")},
	{USB_DEVICE(0x0471, 0x0322), DVNM("Philips DMVC1300K")},
	{USB_DEVICE(0x04a5, 0x3003), DVNM("Benq DC 1300")},
	{USB_DEVICE(0x04a5, 0x3008), DVNM("Benq DC 1500")},
	{USB_DEVICE(0x04a5, 0x300a), DVNM("Benq DC3410")},
	{USB_DEVICE(0x04f1, 0x1001), DVNM("JVC GC A50")},
	{USB_DEVICE(0x04fc, 0x500c), DVNM("Sunplus CA500C")},
	{USB_DEVICE(0x04fc, 0x504a), DVNM("Aiptek Mini PenCam 1.3")},
	{USB_DEVICE(0x04fc, 0x504b), DVNM("Maxell MaxPocket LE 1.3")},
	{USB_DEVICE(0x04fc, 0x5330), DVNM("Digitrex 2110")},
	{USB_DEVICE(0x04fc, 0x5360), DVNM("Sunplus Generic")},
	{USB_DEVICE(0x04fc, 0xffff), DVNM("Pure DigitalDakota")},
	{USB_DEVICE(0x052b, 0x1513), DVNM("Megapix V4")},
	{USB_DEVICE(0x0546, 0x3155), DVNM("Polaroid PDC3070")},
	{USB_DEVICE(0x0546, 0x3191), DVNM("Polaroid Ion 80")},
	{USB_DEVICE(0x0546, 0x3273), DVNM("Polaroid PDC2030")},
	{USB_DEVICE(0x055f, 0xc211), DVNM("Kowa Bs888e Microcamera")},
	{USB_DEVICE(0x055f, 0xc230), DVNM("Mustek Digicam 330K")},
	{USB_DEVICE(0x055f, 0xc232), DVNM("Mustek MDC3500")},
	{USB_DEVICE(0x055f, 0xc360), DVNM("Mustek DV4000 Mpeg4 ")},
	{USB_DEVICE(0x055f, 0xc420), DVNM("Mustek gSmart Mini 2")},
	{USB_DEVICE(0x055f, 0xc430), DVNM("Mustek Gsmart LCD 2")},
	{USB_DEVICE(0x055f, 0xc440), DVNM("Mustek DV 3000")},
	{USB_DEVICE(0x055f, 0xc520), DVNM("Mustek gSmart Mini 3")},
	{USB_DEVICE(0x055f, 0xc530), DVNM("Mustek Gsmart LCD 3")},
	{USB_DEVICE(0x055f, 0xc540), DVNM("Gsmart D30")},
	{USB_DEVICE(0x055f, 0xc630), DVNM("Mustek MDC4000")},
	{USB_DEVICE(0x055f, 0xc650), DVNM("Mustek MDC5500Z")},
	{USB_DEVICE(0x05da, 0x1018), DVNM("Digital Dream Enigma 1.3")},
	{USB_DEVICE(0x06d6, 0x0031), DVNM("Trust 610 LCD PowerC@m Zoom")},
	{USB_DEVICE(0x0733, 0x1311), DVNM("Digital Dream Epsilon 1.3")},
	{USB_DEVICE(0x0733, 0x1314), DVNM("Mercury 2.1MEG Deluxe Classic Cam")},
	{USB_DEVICE(0x0733, 0x2211), DVNM("Jenoptik jdc 21 LCD")},
	{USB_DEVICE(0x0733, 0x2221), DVNM("Mercury Digital Pro 3.1p")},
	{USB_DEVICE(0x0733, 0x3261), DVNM("Concord 3045 spca536a")},
	{USB_DEVICE(0x0733, 0x3281), DVNM("Cyberpix S550V")},
	{USB_DEVICE(0x08ca, 0x0104), DVNM("Aiptek PocketDVII 1.3")},
	{USB_DEVICE(0x08ca, 0x0106), DVNM("Aiptek Pocket DV3100+")},
	{USB_DEVICE(0x08ca, 0x2008), DVNM("Aiptek Mini PenCam 2 M")},
	{USB_DEVICE(0x08ca, 0x2010), DVNM("Aiptek PocketCam 3M")},
	{USB_DEVICE(0x08ca, 0x2016), DVNM("Aiptek PocketCam 2 Mega")},
	{USB_DEVICE(0x08ca, 0x2018), DVNM("Aiptek Pencam SD 2M")},
	{USB_DEVICE(0x08ca, 0x2020), DVNM("Aiptek Slim 3000F")},
	{USB_DEVICE(0x08ca, 0x2022), DVNM("Aiptek Slim 3200")},
	{USB_DEVICE(0x08ca, 0x2024), DVNM("Aiptek DV3500 Mpeg4 ")},
	{USB_DEVICE(0x08ca, 0x2028), DVNM("Aiptek PocketCam4M")},
	{USB_DEVICE(0x08ca, 0x2040), DVNM("Aiptek PocketDV4100M")},
	{USB_DEVICE(0x08ca, 0x2042), DVNM("Aiptek PocketDV5100")},
	{USB_DEVICE(0x08ca, 0x2060), DVNM("Aiptek PocketDV5300")},
	{USB_DEVICE(0x0d64, 0x0303), DVNM("Sunplus FashionCam DXG")},
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
