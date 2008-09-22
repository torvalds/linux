/*
 * SPCA500 chip based cameras initialization data
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
 *
 */

#define MODULE_NAME "spca500"

#include "gspca.h"
#include "jpeg.h"

MODULE_AUTHOR("Michel Xhaard <mxhaard@users.sourceforge.net>");
MODULE_DESCRIPTION("GSPCA/SPCA500 USB Camera Driver");
MODULE_LICENSE("GPL");

/* specific webcam descriptor */
struct sd {
	struct gspca_dev gspca_dev;		/* !! must be the first item */

	__u8 packet[ISO_MAX_SIZE + 128];
				 /* !! no more than 128 ff in an ISO packet */

	unsigned char brightness;
	unsigned char contrast;
	unsigned char colors;

	char qindex;
	char subtype;
#define AgfaCl20 0
#define AiptekPocketDV 1
#define BenqDC1016 2
#define CreativePCCam300 3
#define DLinkDSC350 4
#define Gsmartmini 5
#define IntelPocketPCCamera 6
#define KodakEZ200 7
#define LogitechClickSmart310 8
#define LogitechClickSmart510 9
#define LogitechTraveler 10
#define MustekGsmart300 11
#define Optimedia 12
#define PalmPixDC85 13
#define ToptroIndus 14
};

/* V4L2 controls supported by the driver */
static int sd_setbrightness(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getbrightness(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setcontrast(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getcontrast(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setcolors(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getcolors(struct gspca_dev *gspca_dev, __s32 *val);

static struct ctrl sd_ctrls[] = {
	{
	    {
		.id      = V4L2_CID_BRIGHTNESS,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Brightness",
		.minimum = 0,
		.maximum = 255,
		.step    = 1,
#define BRIGHTNESS_DEF 127
		.default_value = BRIGHTNESS_DEF,
	    },
	    .set = sd_setbrightness,
	    .get = sd_getbrightness,
	},
	{
	    {
		.id      = V4L2_CID_CONTRAST,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Contrast",
		.minimum = 0,
		.maximum = 63,
		.step    = 1,
#define CONTRAST_DEF 31
		.default_value = CONTRAST_DEF,
	    },
	    .set = sd_setcontrast,
	    .get = sd_getcontrast,
	},
	{
	    {
		.id      = V4L2_CID_SATURATION,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Color",
		.minimum = 0,
		.maximum = 63,
		.step    = 1,
#define COLOR_DEF 31
		.default_value = COLOR_DEF,
	    },
	    .set = sd_setcolors,
	    .get = sd_getcolors,
	},
};

static struct v4l2_pix_format vga_mode[] = {
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

static struct v4l2_pix_format sif_mode[] = {
	{176, 144, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 176,
		.sizeimage = 176 * 144 * 3 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 1},
	{352, 288, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 352,
		.sizeimage = 352 * 288 * 3 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 0},
};

/* Frame packet header offsets for the spca500 */
#define SPCA500_OFFSET_PADDINGLB 2
#define SPCA500_OFFSET_PADDINGHB 3
#define SPCA500_OFFSET_MODE      4
#define SPCA500_OFFSET_IMGWIDTH  5
#define SPCA500_OFFSET_IMGHEIGHT 6
#define SPCA500_OFFSET_IMGMODE   7
#define SPCA500_OFFSET_QTBLINDEX 8
#define SPCA500_OFFSET_FRAMSEQ   9
#define SPCA500_OFFSET_CDSPINFO  10
#define SPCA500_OFFSET_GPIO      11
#define SPCA500_OFFSET_AUGPIO    12
#define SPCA500_OFFSET_DATA      16


static const __u16 spca500_visual_defaults[][3] = {
	{0x00, 0x0003, 0x816b},	/* SSI not active sync with vsync,
				 * hue (H byte) = 0,
				 * saturation/hue enable,
				 * brightness/contrast enable.
				 */
	{0x00, 0x0000, 0x8167},	/* brightness = 0 */
	{0x00, 0x0020, 0x8168},	/* contrast = 0 */
	{0x00, 0x0003, 0x816b},	/* SSI not active sync with vsync,
				 * hue (H byte) = 0, saturation/hue enable,
				 * brightness/contrast enable.
				 * was 0x0003, now 0x0000.
				 */
	{0x00, 0x0000, 0x816a},	/* hue (L byte) = 0 */
	{0x00, 0x0020, 0x8169},	/* saturation = 0x20 */
	{0x00, 0x0050, 0x8157},	/* edge gain high threshold */
	{0x00, 0x0030, 0x8158},	/* edge gain low threshold */
	{0x00, 0x0028, 0x8159},	/* edge bandwidth high threshold */
	{0x00, 0x000a, 0x815a},	/* edge bandwidth low threshold */
	{0x00, 0x0001, 0x8202},	/* clock rate compensation = 1/25 sec/frame */
	{0x0c, 0x0004, 0x0000},
	/* set interface */
	{}
};
static const __u16 Clicksmart510_defaults[][3] = {
	{0x00, 0x00, 0x8211},
	{0x00, 0x01, 0x82c0},
	{0x00, 0x10, 0x82cb},
	{0x00, 0x0f, 0x800d},
	{0x00, 0x82, 0x8225},
	{0x00, 0x21, 0x8228},
	{0x00, 0x00, 0x8203},
	{0x00, 0x00, 0x8204},
	{0x00, 0x08, 0x8205},
	{0x00, 0xf8, 0x8206},
	{0x00, 0x28, 0x8207},
	{0x00, 0xa0, 0x8208},
	{0x00, 0x08, 0x824a},
	{0x00, 0x08, 0x8214},
	{0x00, 0x80, 0x82c1},
	{0x00, 0x00, 0x82c2},
	{0x00, 0x00, 0x82ca},
	{0x00, 0x80, 0x82c1},
	{0x00, 0x04, 0x82c2},
	{0x00, 0x00, 0x82ca},
	{0x00, 0xfc, 0x8100},
	{0x00, 0xfc, 0x8105},
	{0x00, 0x30, 0x8101},
	{0x00, 0x00, 0x8102},
	{0x00, 0x00, 0x8103},
	{0x00, 0x66, 0x8107},
	{0x00, 0x00, 0x816b},
	{0x00, 0x00, 0x8155},
	{0x00, 0x01, 0x8156},
	{0x00, 0x60, 0x8157},
	{0x00, 0x40, 0x8158},
	{0x00, 0x0a, 0x8159},
	{0x00, 0x06, 0x815a},
	{0x00, 0x00, 0x813f},
	{0x00, 0x00, 0x8200},
	{0x00, 0x19, 0x8201},
	{0x00, 0x00, 0x82c1},
	{0x00, 0xa0, 0x82c2},
	{0x00, 0x00, 0x82ca},
	{0x00, 0x00, 0x8117},
	{0x00, 0x00, 0x8118},
	{0x00, 0x65, 0x8119},
	{0x00, 0x00, 0x811a},
	{0x00, 0x00, 0x811b},
	{0x00, 0x55, 0x811c},
	{0x00, 0x65, 0x811d},
	{0x00, 0x55, 0x811e},
	{0x00, 0x16, 0x811f},
	{0x00, 0x19, 0x8120},
	{0x00, 0x80, 0x8103},
	{0x00, 0x83, 0x816b},
	{0x00, 0x25, 0x8168},
	{0x00, 0x01, 0x820f},
	{0x00, 0xff, 0x8115},
	{0x00, 0x48, 0x8116},
	{0x00, 0x50, 0x8151},
	{0x00, 0x40, 0x8152},
	{0x00, 0x78, 0x8153},
	{0x00, 0x40, 0x8154},
	{0x00, 0x00, 0x8167},
	{0x00, 0x20, 0x8168},
	{0x00, 0x00, 0x816a},
	{0x00, 0x03, 0x816b},
	{0x00, 0x20, 0x8169},
	{0x00, 0x60, 0x8157},
	{0x00, 0x00, 0x8190},
	{0x00, 0x00, 0x81a1},
	{0x00, 0x00, 0x81b2},
	{0x00, 0x27, 0x8191},
	{0x00, 0x27, 0x81a2},
	{0x00, 0x27, 0x81b3},
	{0x00, 0x4b, 0x8192},
	{0x00, 0x4b, 0x81a3},
	{0x00, 0x4b, 0x81b4},
	{0x00, 0x66, 0x8193},
	{0x00, 0x66, 0x81a4},
	{0x00, 0x66, 0x81b5},
	{0x00, 0x79, 0x8194},
	{0x00, 0x79, 0x81a5},
	{0x00, 0x79, 0x81b6},
	{0x00, 0x8a, 0x8195},
	{0x00, 0x8a, 0x81a6},
	{0x00, 0x8a, 0x81b7},
	{0x00, 0x9b, 0x8196},
	{0x00, 0x9b, 0x81a7},
	{0x00, 0x9b, 0x81b8},
	{0x00, 0xa6, 0x8197},
	{0x00, 0xa6, 0x81a8},
	{0x00, 0xa6, 0x81b9},
	{0x00, 0xb2, 0x8198},
	{0x00, 0xb2, 0x81a9},
	{0x00, 0xb2, 0x81ba},
	{0x00, 0xbe, 0x8199},
	{0x00, 0xbe, 0x81aa},
	{0x00, 0xbe, 0x81bb},
	{0x00, 0xc8, 0x819a},
	{0x00, 0xc8, 0x81ab},
	{0x00, 0xc8, 0x81bc},
	{0x00, 0xd2, 0x819b},
	{0x00, 0xd2, 0x81ac},
	{0x00, 0xd2, 0x81bd},
	{0x00, 0xdb, 0x819c},
	{0x00, 0xdb, 0x81ad},
	{0x00, 0xdb, 0x81be},
	{0x00, 0xe4, 0x819d},
	{0x00, 0xe4, 0x81ae},
	{0x00, 0xe4, 0x81bf},
	{0x00, 0xed, 0x819e},
	{0x00, 0xed, 0x81af},
	{0x00, 0xed, 0x81c0},
	{0x00, 0xf7, 0x819f},
	{0x00, 0xf7, 0x81b0},
	{0x00, 0xf7, 0x81c1},
	{0x00, 0xff, 0x81a0},
	{0x00, 0xff, 0x81b1},
	{0x00, 0xff, 0x81c2},
	{0x00, 0x03, 0x8156},
	{0x00, 0x00, 0x8211},
	{0x00, 0x20, 0x8168},
	{0x00, 0x01, 0x8202},
	{0x00, 0x30, 0x8101},
	{0x00, 0x00, 0x8111},
	{0x00, 0x00, 0x8112},
	{0x00, 0x00, 0x8113},
	{0x00, 0x00, 0x8114},
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

static const __u8 qtable_kodak_ez200[2][64] = {
	{				/* Q-table Y-components */
	 0x02, 0x01, 0x01, 0x02, 0x02, 0x04, 0x05, 0x06,
	 0x01, 0x01, 0x01, 0x02, 0x03, 0x06, 0x06, 0x06,
	 0x01, 0x01, 0x02, 0x02, 0x04, 0x06, 0x07, 0x06,
	 0x01, 0x02, 0x02, 0x03, 0x05, 0x09, 0x08, 0x06,
	 0x02, 0x02, 0x04, 0x06, 0x07, 0x0b, 0x0a, 0x08,
	 0x02, 0x04, 0x06, 0x06, 0x08, 0x0a, 0x0b, 0x09,
	 0x05, 0x06, 0x08, 0x09, 0x0a, 0x0c, 0x0c, 0x0a,
	 0x07, 0x09, 0x0a, 0x0a, 0x0b, 0x0a, 0x0a, 0x0a},
	{				/* Q-table C-components */
	 0x02, 0x02, 0x02, 0x05, 0x0a, 0x0a, 0x0a, 0x0a,
	 0x02, 0x02, 0x03, 0x07, 0x0a, 0x0a, 0x0a, 0x0a,
	 0x02, 0x03, 0x06, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a,
	 0x05, 0x07, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a,
	 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a,
	 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a,
	 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a,
	 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a}
};

static const __u8 qtable_pocketdv[2][64] = {
	{		/* Q-table Y-components start registers 0x8800 */
	 0x06, 0x04, 0x04, 0x06, 0x0a, 0x10, 0x14, 0x18,
	 0x05, 0x05, 0x06, 0x08, 0x0a, 0x17, 0x18, 0x16,
	 0x06, 0x05, 0x06, 0x0a, 0x10, 0x17, 0x1c, 0x16,
	 0x06, 0x07, 0x09, 0x0c, 0x14, 0x23, 0x20, 0x19,
	 0x07, 0x09, 0x0f, 0x16, 0x1b, 0x2c, 0x29, 0x1f,
	 0x0a, 0x0e, 0x16, 0x1a, 0x20, 0x2a, 0x2d, 0x25,
	 0x14, 0x1a, 0x1f, 0x23, 0x29, 0x30, 0x30, 0x28,
	 0x1d, 0x25, 0x26, 0x27, 0x2d, 0x28, 0x29, 0x28,
	 },
	{		/* Q-table C-components start registers 0x8840 */
	 0x07, 0x07, 0x0a, 0x13, 0x28, 0x28, 0x28, 0x28,
	 0x07, 0x08, 0x0a, 0x1a, 0x28, 0x28, 0x28, 0x28,
	 0x0a, 0x0a, 0x16, 0x28, 0x28, 0x28, 0x28, 0x28,
	 0x13, 0x1a, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28,
	 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28,
	 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28,
	 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28,
	 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28}
};

/* read 'len' bytes to gspca_dev->usb_buf */
static void reg_r(struct gspca_dev *gspca_dev,
		  __u16 index,
		  __u16 length)
{
	usb_control_msg(gspca_dev->dev,
			usb_rcvctrlpipe(gspca_dev->dev, 0),
			0,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0,		/* value */
			index, gspca_dev->usb_buf, length, 500);
}

static int reg_w(struct gspca_dev *gspca_dev,
		     __u16 req, __u16 index, __u16 value)
{
	int ret;

	PDEBUG(D_USBO, "reg write: [0x%02x] = 0x%02x", index, value);
	ret = usb_control_msg(gspca_dev->dev,
			usb_sndctrlpipe(gspca_dev->dev, 0),
			req,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			value, index, NULL, 0, 500);
	if (ret < 0)
		PDEBUG(D_ERR, "reg write: error %d", ret);
	return ret;
}

/* returns: negative is error, pos or zero is data */
static int reg_r_12(struct gspca_dev *gspca_dev,
			__u16 req,	/* bRequest */
			__u16 index,	/* wIndex */
			__u16 length)	/* wLength (1 or 2 only) */
{
	int ret;

	gspca_dev->usb_buf[1] = 0;
	ret = usb_control_msg(gspca_dev->dev,
			usb_rcvctrlpipe(gspca_dev->dev, 0),
			req,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0,		/* value */
			index,
			gspca_dev->usb_buf, length,
			500);		/* timeout */
	if (ret < 0) {
		PDEBUG(D_ERR, "reg_r_12 err %d", ret);
		return -1;
	}
	return (gspca_dev->usb_buf[1] << 8) + gspca_dev->usb_buf[0];
}

/*
 * Simple function to wait for a given 8-bit value to be returned from
 * a reg_read call.
 * Returns: negative is error or timeout, zero is success.
 */
static int reg_r_wait(struct gspca_dev *gspca_dev,
			__u16 reg, __u16 index, __u16 value)
{
	int ret, cnt = 20;

	while (--cnt > 0) {
		ret = reg_r_12(gspca_dev, reg, index, 1);
		if (ret == value)
			return 0;
		msleep(50);
	}
	return -EIO;
}

static int write_vector(struct gspca_dev *gspca_dev,
			const __u16 data[][3])
{
	int ret, i = 0;

	while (data[i][0] != 0 || data[i][1] != 0 || data[i][2] != 0) {
		ret = reg_w(gspca_dev, data[i][0], data[i][2], data[i][1]);
		if (ret < 0)
			return ret;
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
	int i, err;

	/* loop over y components */
	for (i = 0; i < 64; i++) {
		err = reg_w(gspca_dev, request, ybase + i, qtable[0][i]);
		if (err < 0)
			return err;
	}

	/* loop over c components */
	for (i = 0; i < 64; i++) {
		err = reg_w(gspca_dev, request, cbase + i, qtable[1][i]);
		if (err < 0)
			return err;
	}
	return 0;
}

static void spca500_ping310(struct gspca_dev *gspca_dev)
{
	reg_r(gspca_dev, 0x0d04, 2);
	PDEBUG(D_STREAM, "ClickSmart310 ping 0x0d04 0x%02x 0x%02x",
		gspca_dev->usb_buf[0], gspca_dev->usb_buf[1]);
}

static void spca500_clksmart310_init(struct gspca_dev *gspca_dev)
{
	reg_r(gspca_dev, 0x0d05, 2);
	PDEBUG(D_STREAM, "ClickSmart310 init 0x0d05 0x%02x 0x%02x",
		gspca_dev->usb_buf[0], gspca_dev->usb_buf[1]);
	reg_w(gspca_dev, 0x00, 0x8167, 0x5a);
	spca500_ping310(gspca_dev);

	reg_w(gspca_dev, 0x00, 0x8168, 0x22);
	reg_w(gspca_dev, 0x00, 0x816a, 0xc0);
	reg_w(gspca_dev, 0x00, 0x816b, 0x0b);
	reg_w(gspca_dev, 0x00, 0x8169, 0x25);
	reg_w(gspca_dev, 0x00, 0x8157, 0x5b);
	reg_w(gspca_dev, 0x00, 0x8158, 0x5b);
	reg_w(gspca_dev, 0x00, 0x813f, 0x03);
	reg_w(gspca_dev, 0x00, 0x8151, 0x4a);
	reg_w(gspca_dev, 0x00, 0x8153, 0x78);
	reg_w(gspca_dev, 0x00, 0x0d01, 0x04);
						/* 00 for adjust shutter */
	reg_w(gspca_dev, 0x00, 0x0d02, 0x01);
	reg_w(gspca_dev, 0x00, 0x8169, 0x25);
	reg_w(gspca_dev, 0x00, 0x0d01, 0x02);
}

static void spca500_setmode(struct gspca_dev *gspca_dev,
			__u8 xmult, __u8 ymult)
{
	int mode;

	/* set x multiplier */
	reg_w(gspca_dev, 0, 0x8001, xmult);

	/* set y multiplier */
	reg_w(gspca_dev, 0, 0x8002, ymult);

	/* use compressed mode, VGA, with mode specific subsample */
	mode = gspca_dev->cam.cam_mode[(int) gspca_dev->curr_mode].priv;
	reg_w(gspca_dev, 0, 0x8003, mode << 4);
}

static int spca500_full_reset(struct gspca_dev *gspca_dev)
{
	int err;

	/* send the reset command */
	err = reg_w(gspca_dev, 0xe0, 0x0001, 0x0000);
	if (err < 0)
		return err;

	/* wait for the reset to complete */
	err = reg_r_wait(gspca_dev, 0x06, 0x0000, 0x0000);
	if (err < 0)
		return err;
	err = reg_w(gspca_dev, 0xe0, 0x0000, 0x0000);
	if (err < 0)
		return err;
	err = reg_r_wait(gspca_dev, 0x06, 0, 0);
	if (err < 0) {
		PDEBUG(D_ERR, "reg_r_wait() failed");
		return err;
	}
	/* all ok */
	return 0;
}

/* Synchro the Bridge with sensor */
/* Maybe that will work on all spca500 chip */
/* because i only own a clicksmart310 try for that chip */
/* using spca50x_set_packet_size() cause an Ooops here */
/* usb_set_interface from kernel 2.6.x clear all the urb stuff */
/* up-port the same feature as in 2.4.x kernel */
static int spca500_synch310(struct gspca_dev *gspca_dev)
{
	if (usb_set_interface(gspca_dev->dev, gspca_dev->iface, 0) < 0) {
		PDEBUG(D_ERR, "Set packet size: set interface error");
		goto error;
	}
	spca500_ping310(gspca_dev);

	reg_r(gspca_dev, 0x0d00, 1);

	/* need alt setting here */
	PDEBUG(D_PACK, "ClickSmart310 sync alt: %d", gspca_dev->alt);

	/* Windoze use pipe with altsetting 6 why 7 here */
	if (usb_set_interface(gspca_dev->dev,
				gspca_dev->iface,
				gspca_dev->alt) < 0) {
		PDEBUG(D_ERR, "Set packet size: set interface error");
		goto error;
	}
	return 0;
error:
	return -EBUSY;
}

static void spca500_reinit(struct gspca_dev *gspca_dev)
{
	int err;
	__u8 Data;

	/* some unknow command from Aiptek pocket dv and family300 */

	reg_w(gspca_dev, 0x00, 0x0d01, 0x01);
	reg_w(gspca_dev, 0x00, 0x0d03, 0x00);
	reg_w(gspca_dev, 0x00, 0x0d02, 0x01);

	/* enable drop packet */
	reg_w(gspca_dev, 0x00, 0x850a, 0x0001);

	err = spca50x_setup_qtable(gspca_dev, 0x00, 0x8800, 0x8840,
				 qtable_pocketdv);
	if (err < 0)
		PDEBUG(D_ERR|D_STREAM, "spca50x_setup_qtable failed on init");

	/* set qtable index */
	reg_w(gspca_dev, 0x00, 0x8880, 2);
	/* family cam Quicksmart stuff */
	reg_w(gspca_dev, 0x00, 0x800a, 0x00);
	/* Set agc transfer: synced inbetween frames */
	reg_w(gspca_dev, 0x00, 0x820f, 0x01);
	/* Init SDRAM - needed for SDRAM access */
	reg_w(gspca_dev, 0x00, 0x870a, 0x04);
	/*Start init sequence or stream */
	reg_w(gspca_dev, 0, 0x8003, 0x00);
	/* switch to video camera mode */
	reg_w(gspca_dev, 0x00, 0x8000, 0x0004);
	msleep(2000);
	if (reg_r_wait(gspca_dev, 0, 0x8000, 0x44) != 0) {
		reg_r(gspca_dev, 0x816b, 1);
		Data = gspca_dev->usb_buf[0];
		reg_w(gspca_dev, 0x00, 0x816b, Data);
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
	sd->subtype = id->driver_info;
	if (sd->subtype != LogitechClickSmart310) {
		cam->cam_mode = vga_mode;
		cam->nmodes = sizeof vga_mode / sizeof vga_mode[0];
	} else {
		cam->cam_mode = sif_mode;
		cam->nmodes = sizeof sif_mode / sizeof sif_mode[0];
	}
	sd->qindex = 5;
	sd->brightness = BRIGHTNESS_DEF;
	sd->contrast = CONTRAST_DEF;
	sd->colors = COLOR_DEF;
	return 0;
}

/* this function is called at probe and resume time */
static int sd_init(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	/* initialisation of spca500 based cameras is deferred */
	PDEBUG(D_STREAM, "SPCA500 init");
	if (sd->subtype == LogitechClickSmart310)
		spca500_clksmart310_init(gspca_dev);
/*	else
		spca500_initialise(gspca_dev); */
	PDEBUG(D_STREAM, "SPCA500 init done");
	return 0;
}

static void sd_start(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int err;
	__u8 Data;
	__u8 xmult, ymult;

	if (sd->subtype == LogitechClickSmart310) {
		xmult = 0x16;
		ymult = 0x12;
	} else {
		xmult = 0x28;
		ymult = 0x1e;
	}

	/* is there a sensor here ? */
	reg_r(gspca_dev, 0x8a04, 1);
	PDEBUG(D_STREAM, "Spca500 Sensor Address 0x%02x",
		gspca_dev->usb_buf[0]);
	PDEBUG(D_STREAM, "Spca500 curr_mode: %d Xmult: 0x%02x, Ymult: 0x%02x",
		gspca_dev->curr_mode, xmult, ymult);

	/* setup qtable */
	switch (sd->subtype) {
	case LogitechClickSmart310:
		 spca500_setmode(gspca_dev, xmult, ymult);

		/* enable drop packet */
		reg_w(gspca_dev, 0x00, 0x850a, 0x0001);
		reg_w(gspca_dev, 0x00, 0x8880, 3);
		err = spca50x_setup_qtable(gspca_dev,
					   0x00, 0x8800, 0x8840,
					   qtable_creative_pccam);
		if (err < 0)
			PDEBUG(D_ERR, "spca50x_setup_qtable failed");
		/* Init SDRAM - needed for SDRAM access */
		reg_w(gspca_dev, 0x00, 0x870a, 0x04);

		/* switch to video camera mode */
		reg_w(gspca_dev, 0x00, 0x8000, 0x0004);
		msleep(500);
		if (reg_r_wait(gspca_dev, 0, 0x8000, 0x44) != 0)
			PDEBUG(D_ERR, "reg_r_wait() failed");

		reg_r(gspca_dev, 0x816b, 1);
		Data = gspca_dev->usb_buf[0];
		reg_w(gspca_dev, 0x00, 0x816b, Data);

		spca500_synch310(gspca_dev);

		write_vector(gspca_dev, spca500_visual_defaults);
		spca500_setmode(gspca_dev, xmult, ymult);
		/* enable drop packet */
		reg_w(gspca_dev, 0x00, 0x850a, 0x0001);
			PDEBUG(D_ERR, "failed to enable drop packet");
		reg_w(gspca_dev, 0x00, 0x8880, 3);
		err = spca50x_setup_qtable(gspca_dev,
					   0x00, 0x8800, 0x8840,
					   qtable_creative_pccam);
		if (err < 0)
			PDEBUG(D_ERR, "spca50x_setup_qtable failed");

		/* Init SDRAM - needed for SDRAM access */
		reg_w(gspca_dev, 0x00, 0x870a, 0x04);

		/* switch to video camera mode */
		reg_w(gspca_dev, 0x00, 0x8000, 0x0004);

		if (reg_r_wait(gspca_dev, 0, 0x8000, 0x44) != 0)
			PDEBUG(D_ERR, "reg_r_wait() failed");

		reg_r(gspca_dev, 0x816b, 1);
		Data = gspca_dev->usb_buf[0];
		reg_w(gspca_dev, 0x00, 0x816b, Data);
		break;
	case CreativePCCam300:		/* Creative PC-CAM 300 640x480 CCD */
	case IntelPocketPCCamera:	/* FIXME: Temporary fix for
					 *	Intel Pocket PC Camera
					 *	- NWG (Sat 29th March 2003) */

		/* do a full reset */
		err = spca500_full_reset(gspca_dev);
		if (err < 0)
			PDEBUG(D_ERR, "spca500_full_reset failed");

		/* enable drop packet */
		err = reg_w(gspca_dev, 0x00, 0x850a, 0x0001);
		if (err < 0)
			PDEBUG(D_ERR, "failed to enable drop packet");
		reg_w(gspca_dev, 0x00, 0x8880, 3);
		err = spca50x_setup_qtable(gspca_dev,
					   0x00, 0x8800, 0x8840,
					   qtable_creative_pccam);
		if (err < 0)
			PDEBUG(D_ERR, "spca50x_setup_qtable failed");

		spca500_setmode(gspca_dev, xmult, ymult);
		reg_w(gspca_dev, 0x20, 0x0001, 0x0004);

		/* switch to video camera mode */
		reg_w(gspca_dev, 0x00, 0x8000, 0x0004);

		if (reg_r_wait(gspca_dev, 0, 0x8000, 0x44) != 0)
			PDEBUG(D_ERR, "reg_r_wait() failed");

		reg_r(gspca_dev, 0x816b, 1);
		Data = gspca_dev->usb_buf[0];
		reg_w(gspca_dev, 0x00, 0x816b, Data);

/*		write_vector(gspca_dev, spca500_visual_defaults); */
		break;
	case KodakEZ200:		/* Kodak EZ200 */

		/* do a full reset */
		err = spca500_full_reset(gspca_dev);
		if (err < 0)
			PDEBUG(D_ERR, "spca500_full_reset failed");
		/* enable drop packet */
		reg_w(gspca_dev, 0x00, 0x850a, 0x0001);
		reg_w(gspca_dev, 0x00, 0x8880, 0);
		err = spca50x_setup_qtable(gspca_dev,
					   0x00, 0x8800, 0x8840,
					   qtable_kodak_ez200);
		if (err < 0)
			PDEBUG(D_ERR, "spca50x_setup_qtable failed");
		spca500_setmode(gspca_dev, xmult, ymult);

		reg_w(gspca_dev, 0x20, 0x0001, 0x0004);

		/* switch to video camera mode */
		reg_w(gspca_dev, 0x00, 0x8000, 0x0004);

		if (reg_r_wait(gspca_dev, 0, 0x8000, 0x44) != 0)
			PDEBUG(D_ERR, "reg_r_wait() failed");

		reg_r(gspca_dev, 0x816b, 1);
		Data = gspca_dev->usb_buf[0];
		reg_w(gspca_dev, 0x00, 0x816b, Data);

/*		write_vector(gspca_dev, spca500_visual_defaults); */
		break;

	case BenqDC1016:
	case DLinkDSC350:		/* FamilyCam 300 */
	case AiptekPocketDV:		/* Aiptek PocketDV */
	case Gsmartmini:		/*Mustek Gsmart Mini */
	case MustekGsmart300:		/* Mustek Gsmart 300 */
	case PalmPixDC85:
	case Optimedia:
	case ToptroIndus:
	case AgfaCl20:
		spca500_reinit(gspca_dev);
		reg_w(gspca_dev, 0x00, 0x0d01, 0x01);
		/* enable drop packet */
		reg_w(gspca_dev, 0x00, 0x850a, 0x0001);

		err = spca50x_setup_qtable(gspca_dev,
				   0x00, 0x8800, 0x8840, qtable_pocketdv);
		if (err < 0)
			PDEBUG(D_ERR, "spca50x_setup_qtable failed");
		reg_w(gspca_dev, 0x00, 0x8880, 2);

		/* familycam Quicksmart pocketDV stuff */
		reg_w(gspca_dev, 0x00, 0x800a, 0x00);
		/* Set agc transfer: synced inbetween frames */
		reg_w(gspca_dev, 0x00, 0x820f, 0x01);
		/* Init SDRAM - needed for SDRAM access */
		reg_w(gspca_dev, 0x00, 0x870a, 0x04);

		spca500_setmode(gspca_dev, xmult, ymult);
		/* switch to video camera mode */
		reg_w(gspca_dev, 0x00, 0x8000, 0x0004);

		reg_r_wait(gspca_dev, 0, 0x8000, 0x44);

		reg_r(gspca_dev, 0x816b, 1);
		Data = gspca_dev->usb_buf[0];
		reg_w(gspca_dev, 0x00, 0x816b, Data);
		break;
	case LogitechTraveler:
	case LogitechClickSmart510:
		reg_w(gspca_dev, 0x02, 0x00, 0x00);
		/* enable drop packet */
		reg_w(gspca_dev, 0x00, 0x850a, 0x0001);

		err = spca50x_setup_qtable(gspca_dev,
					0x00, 0x8800,
					0x8840, qtable_creative_pccam);
		if (err < 0)
			PDEBUG(D_ERR, "spca50x_setup_qtable failed");
		reg_w(gspca_dev, 0x00, 0x8880, 3);
		reg_w(gspca_dev, 0x00, 0x800a, 0x00);
		/* Init SDRAM - needed for SDRAM access */
		reg_w(gspca_dev, 0x00, 0x870a, 0x04);

		spca500_setmode(gspca_dev, xmult, ymult);

		/* switch to video camera mode */
		reg_w(gspca_dev, 0x00, 0x8000, 0x0004);
		reg_r_wait(gspca_dev, 0, 0x8000, 0x44);

		reg_r(gspca_dev, 0x816b, 1);
		Data = gspca_dev->usb_buf[0];
		reg_w(gspca_dev, 0x00, 0x816b, Data);
		write_vector(gspca_dev, Clicksmart510_defaults);
		break;
	}
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	reg_w(gspca_dev, 0, 0x8003, 0x00);

	/* switch to video camera mode */
	reg_w(gspca_dev, 0x00, 0x8000, 0x0004);
	reg_r(gspca_dev, 0x8000, 1);
	PDEBUG(D_STREAM, "stop SPCA500 done reg8000: 0x%2x",
		gspca_dev->usb_buf[0]);
}

static void sd_pkt_scan(struct gspca_dev *gspca_dev,
			struct gspca_frame *frame,	/* target */
			__u8 *data,			/* isoc packet */
			int len)			/* iso packet length */
{
	struct sd *sd = (struct sd *) gspca_dev;
	int i;
	__u8 *s, *d;
	static __u8 ffd9[] = {0xff, 0xd9};

/* frames are jpeg 4.1.1 without 0xff escape */
	if (data[0] == 0xff) {
		if (data[1] != 0x01) {	/* drop packet */
/*			gspca_dev->last_packet_type = DISCARD_PACKET; */
			return;
		}
		frame = gspca_frame_add(gspca_dev, LAST_PACKET, frame,
					ffd9, 2);

		/* put the JPEG header in the new frame */
		jpeg_put_header(gspca_dev, frame, sd->qindex, 0x22);

		data += SPCA500_OFFSET_DATA;
		len -= SPCA500_OFFSET_DATA;
	} else {
		data += 1;
		len -= 1;
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

	reg_w(gspca_dev, 0x00, 0x8167,
			(__u8) (sd->brightness - 128));
}

static void getbrightness(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int ret;

	ret = reg_r_12(gspca_dev, 0x00, 0x8167, 1);
	if (ret >= 0)
		sd->brightness = ret + 128;
}

static void setcontrast(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	reg_w(gspca_dev, 0x00, 0x8168, sd->contrast);
}

static void getcontrast(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int ret;

	ret = reg_r_12(gspca_dev, 0x0, 0x8168, 1);
	if (ret >= 0)
		sd->contrast = ret;
}

static void setcolors(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	reg_w(gspca_dev, 0x00, 0x8169, sd->colors);
}

static void getcolors(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int ret;

	ret = reg_r_12(gspca_dev, 0x0, 0x8169, 1);
	if (ret >= 0)
		sd->colors = ret;
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

/* sub-driver description */
static struct sd_desc sd_desc = {
	.name = MODULE_NAME,
	.ctrls = sd_ctrls,
	.nctrls = ARRAY_SIZE(sd_ctrls),
	.config = sd_config,
	.init = sd_init,
	.start = sd_start,
	.stopN = sd_stopN,
	.pkt_scan = sd_pkt_scan,
};

/* -- module initialisation -- */
static const __devinitdata struct usb_device_id device_table[] = {
	{USB_DEVICE(0x040a, 0x0300), .driver_info = KodakEZ200},
	{USB_DEVICE(0x041e, 0x400a), .driver_info = CreativePCCam300},
	{USB_DEVICE(0x046d, 0x0890), .driver_info = LogitechTraveler},
	{USB_DEVICE(0x046d, 0x0900), .driver_info = LogitechClickSmart310},
	{USB_DEVICE(0x046d, 0x0901), .driver_info = LogitechClickSmart510},
	{USB_DEVICE(0x04a5, 0x300c), .driver_info = BenqDC1016},
	{USB_DEVICE(0x04fc, 0x7333), .driver_info = PalmPixDC85},
	{USB_DEVICE(0x055f, 0xc200), .driver_info = MustekGsmart300},
	{USB_DEVICE(0x055f, 0xc220), .driver_info = Gsmartmini},
	{USB_DEVICE(0x06bd, 0x0404), .driver_info = AgfaCl20},
	{USB_DEVICE(0x06be, 0x0800), .driver_info = Optimedia},
	{USB_DEVICE(0x084d, 0x0003), .driver_info = DLinkDSC350},
	{USB_DEVICE(0x08ca, 0x0103), .driver_info = AiptekPocketDV},
	{USB_DEVICE(0x2899, 0x012c), .driver_info = ToptroIndus},
	{USB_DEVICE(0x8086, 0x0630), .driver_info = IntelPocketPCCamera},
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
