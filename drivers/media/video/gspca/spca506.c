/*
 * SPCA506 chip based cameras function
 * M Xhaard 15/04/2004 based on different work Mark Taylor and others
 * and my own snoopy file on a pv-321c donate by a german compagny
 *                "Firma Frank Gmbh" from  Saarbruecken
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

#define MODULE_NAME "spca506"

#include "gspca.h"

MODULE_AUTHOR("Michel Xhaard <mxhaard@users.sourceforge.net>");
MODULE_DESCRIPTION("GSPCA/SPCA506 USB Camera Driver");
MODULE_LICENSE("GPL");

/* specific webcam descriptor */
struct sd {
	struct gspca_dev gspca_dev;	/* !! must be the first item */

	unsigned char brightness;
	unsigned char contrast;
	unsigned char colors;
	unsigned char hue;
	char norme;
	char channel;
};

/* V4L2 controls supported by the driver */
static int sd_setbrightness(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getbrightness(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setcontrast(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getcontrast(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setcolors(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getcolors(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_sethue(struct gspca_dev *gspca_dev, __s32 val);
static int sd_gethue(struct gspca_dev *gspca_dev, __s32 *val);

static const struct ctrl sd_ctrls[] = {
#define SD_BRIGHTNESS 0
	{
	    {
		.id      = V4L2_CID_BRIGHTNESS,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Brightness",
		.minimum = 0,
		.maximum = 0xff,
		.step    = 1,
		.default_value = 0x80,
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
		.default_value = 0x47,
	    },
	    .set = sd_setcontrast,
	    .get = sd_getcontrast,
	},
#define SD_COLOR 2
	{
	    {
		.id      = V4L2_CID_SATURATION,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Saturation",
		.minimum = 0,
		.maximum = 0xff,
		.step    = 1,
		.default_value = 0x40,
	    },
	    .set = sd_setcolors,
	    .get = sd_getcolors,
	},
#define SD_HUE 3
	{
	    {
		.id      = V4L2_CID_HUE,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Hue",
		.minimum = 0,
		.maximum = 0xff,
		.step    = 1,
		.default_value = 0,
	    },
	    .set = sd_sethue,
	    .get = sd_gethue,
	},
};

static const struct v4l2_pix_format vga_mode[] = {
	{160, 120, V4L2_PIX_FMT_SPCA505, V4L2_FIELD_NONE,
		.bytesperline = 160,
		.sizeimage = 160 * 120 * 3 / 2,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 5},
	{176, 144, V4L2_PIX_FMT_SPCA505, V4L2_FIELD_NONE,
		.bytesperline = 176,
		.sizeimage = 176 * 144 * 3 / 2,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 4},
	{320, 240, V4L2_PIX_FMT_SPCA505, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240 * 3 / 2,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 2},
	{352, 288, V4L2_PIX_FMT_SPCA505, V4L2_FIELD_NONE,
		.bytesperline = 352,
		.sizeimage = 352 * 288 * 3 / 2,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 1},
	{640, 480, V4L2_PIX_FMT_SPCA505, V4L2_FIELD_NONE,
		.bytesperline = 640,
		.sizeimage = 640 * 480 * 3 / 2,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0},
};

#define SPCA50X_OFFSET_DATA 10

#define SAA7113_bright 0x0a	/* defaults 0x80 */
#define SAA7113_contrast 0x0b	/* defaults 0x47 */
#define SAA7113_saturation 0x0c	/* defaults 0x40 */
#define SAA7113_hue 0x0d	/* defaults 0x00 */
#define SAA7113_I2C_BASE_WRITE 0x4a

/* read 'len' bytes to gspca_dev->usb_buf */
static void reg_r(struct gspca_dev *gspca_dev,
		  __u16 req,
		  __u16 index,
		  __u16 length)
{
	usb_control_msg(gspca_dev->dev,
			usb_rcvctrlpipe(gspca_dev->dev, 0),
			req,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0,		/* value */
			index, gspca_dev->usb_buf, length,
			500);
}

static void reg_w(struct usb_device *dev,
		  __u16 req,
		  __u16 value,
		  __u16 index)
{
	usb_control_msg(dev,
			usb_sndctrlpipe(dev, 0),
			req,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			value, index,
			NULL, 0, 500);
}

static void spca506_Initi2c(struct gspca_dev *gspca_dev)
{
	reg_w(gspca_dev->dev, 0x07, SAA7113_I2C_BASE_WRITE, 0x0004);
}

static void spca506_WriteI2c(struct gspca_dev *gspca_dev, __u16 valeur,
			     __u16 reg)
{
	int retry = 60;

	reg_w(gspca_dev->dev, 0x07, reg, 0x0001);
	reg_w(gspca_dev->dev, 0x07, valeur, 0x0000);
	while (retry--) {
		reg_r(gspca_dev, 0x07, 0x0003, 2);
		if ((gspca_dev->usb_buf[0] | gspca_dev->usb_buf[1]) == 0x00)
			break;
	}
}

static void spca506_SetNormeInput(struct gspca_dev *gspca_dev,
				 __u16 norme,
				 __u16 channel)
{
	struct sd *sd = (struct sd *) gspca_dev;
/* fixme: check if channel == 0..3 and 6..9 (8 values) */
	__u8 setbit0 = 0x00;
	__u8 setbit1 = 0x00;
	__u8 videomask = 0x00;

	PDEBUG(D_STREAM, "** Open Set Norme **");
	spca506_Initi2c(gspca_dev);
	/* NTSC bit0 -> 1(525 l) PAL SECAM bit0 -> 0 (625 l) */
	/* Composite channel bit1 -> 1 S-video bit 1 -> 0 */
	/* and exclude SAA7113 reserved channel set default 0 otherwise */
	if (norme & V4L2_STD_NTSC)
		setbit0 = 0x01;
	if (channel == 4 || channel == 5 || channel > 9)
		channel = 0;
	if (channel < 4)
		setbit1 = 0x02;
	videomask = (0x48 | setbit0 | setbit1);
	reg_w(gspca_dev->dev, 0x08, videomask, 0x0000);
	spca506_WriteI2c(gspca_dev, (0xc0 | (channel & 0x0F)), 0x02);

	if (norme & V4L2_STD_NTSC)
		spca506_WriteI2c(gspca_dev, 0x33, 0x0e);
					/* Chrominance Control NTSC N */
	else if (norme & V4L2_STD_SECAM)
		spca506_WriteI2c(gspca_dev, 0x53, 0x0e);
					/* Chrominance Control SECAM */
	else
		spca506_WriteI2c(gspca_dev, 0x03, 0x0e);
					/* Chrominance Control PAL BGHIV */

	sd->norme = norme;
	sd->channel = channel;
	PDEBUG(D_STREAM, "Set Video Byte to 0x%2x", videomask);
	PDEBUG(D_STREAM, "Set Norme: %08x Channel %d", norme, channel);
}

static void spca506_GetNormeInput(struct gspca_dev *gspca_dev,
				  __u16 *norme, __u16 *channel)
{
	struct sd *sd = (struct sd *) gspca_dev;

	/* Read the register is not so good value change so
	   we use your own copy in spca50x struct */
	*norme = sd->norme;
	*channel = sd->channel;
	PDEBUG(D_STREAM, "Get Norme: %d Channel %d", *norme, *channel);
}

static void spca506_Setsize(struct gspca_dev *gspca_dev, __u16 code,
			    __u16 xmult, __u16 ymult)
{
	struct usb_device *dev = gspca_dev->dev;

	PDEBUG(D_STREAM, "** SetSize **");
	reg_w(dev, 0x04, (0x18 | (code & 0x07)), 0x0000);
	/* Soft snap 0x40 Hard 0x41 */
	reg_w(dev, 0x04, 0x41, 0x0001);
	reg_w(dev, 0x04, 0x00, 0x0002);
	/* reserved */
	reg_w(dev, 0x04, 0x00, 0x0003);

	/* reserved */
	reg_w(dev, 0x04, 0x00, 0x0004);
	/* reserved */
	reg_w(dev, 0x04, 0x01, 0x0005);
	/* reserced */
	reg_w(dev, 0x04, xmult, 0x0006);
	/* reserved */
	reg_w(dev, 0x04, ymult, 0x0007);
	/* compression 1 */
	reg_w(dev, 0x04, 0x00, 0x0008);
	/* T=64 -> 2 */
	reg_w(dev, 0x04, 0x00, 0x0009);
	/* threshold2D */
	reg_w(dev, 0x04, 0x21, 0x000a);
	/* quantization */
	reg_w(dev, 0x04, 0x00, 0x000b);
}

/* this function is called at probe time */
static int sd_config(struct gspca_dev *gspca_dev,
			const struct usb_device_id *id)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct cam *cam;

	cam = &gspca_dev->cam;
	cam->cam_mode = vga_mode;
	cam->nmodes = ARRAY_SIZE(vga_mode);
	sd->brightness = sd_ctrls[SD_BRIGHTNESS].qctrl.default_value;
	sd->contrast = sd_ctrls[SD_CONTRAST].qctrl.default_value;
	sd->colors = sd_ctrls[SD_COLOR].qctrl.default_value;
	sd->hue = sd_ctrls[SD_HUE].qctrl.default_value;
	return 0;
}

/* this function is called at probe and resume time */
static int sd_init(struct gspca_dev *gspca_dev)
{
	struct usb_device *dev = gspca_dev->dev;

	reg_w(dev, 0x03, 0x00, 0x0004);
	reg_w(dev, 0x03, 0xFF, 0x0003);
	reg_w(dev, 0x03, 0x00, 0x0000);
	reg_w(dev, 0x03, 0x1c, 0x0001);
	reg_w(dev, 0x03, 0x18, 0x0001);
	/* Init on PAL and composite input0 */
	spca506_SetNormeInput(gspca_dev, 0, 0);
	reg_w(dev, 0x03, 0x1c, 0x0001);
	reg_w(dev, 0x03, 0x18, 0x0001);
	reg_w(dev, 0x05, 0x00, 0x0000);
	reg_w(dev, 0x05, 0xef, 0x0001);
	reg_w(dev, 0x05, 0x00, 0x00c1);
	reg_w(dev, 0x05, 0x00, 0x00c2);
	reg_w(dev, 0x06, 0x18, 0x0002);
	reg_w(dev, 0x06, 0xf5, 0x0011);
	reg_w(dev, 0x06, 0x02, 0x0012);
	reg_w(dev, 0x06, 0xfb, 0x0013);
	reg_w(dev, 0x06, 0x00, 0x0014);
	reg_w(dev, 0x06, 0xa4, 0x0051);
	reg_w(dev, 0x06, 0x40, 0x0052);
	reg_w(dev, 0x06, 0x71, 0x0053);
	reg_w(dev, 0x06, 0x40, 0x0054);
	/************************************************/
	reg_w(dev, 0x03, 0x00, 0x0004);
	reg_w(dev, 0x03, 0x00, 0x0003);
	reg_w(dev, 0x03, 0x00, 0x0004);
	reg_w(dev, 0x03, 0xFF, 0x0003);
	reg_w(dev, 0x02, 0x00, 0x0000);
	reg_w(dev, 0x03, 0x60, 0x0000);
	reg_w(dev, 0x03, 0x18, 0x0001);
	/* for a better reading mx :)	  */
	/*sdca506_WriteI2c(value,register) */
	spca506_Initi2c(gspca_dev);
	spca506_WriteI2c(gspca_dev, 0x08, 0x01);
	spca506_WriteI2c(gspca_dev, 0xc0, 0x02);
						/* input composite video */
	spca506_WriteI2c(gspca_dev, 0x33, 0x03);
	spca506_WriteI2c(gspca_dev, 0x00, 0x04);
	spca506_WriteI2c(gspca_dev, 0x00, 0x05);
	spca506_WriteI2c(gspca_dev, 0x0d, 0x06);
	spca506_WriteI2c(gspca_dev, 0xf0, 0x07);
	spca506_WriteI2c(gspca_dev, 0x98, 0x08);
	spca506_WriteI2c(gspca_dev, 0x03, 0x09);
	spca506_WriteI2c(gspca_dev, 0x80, 0x0a);
	spca506_WriteI2c(gspca_dev, 0x47, 0x0b);
	spca506_WriteI2c(gspca_dev, 0x48, 0x0c);
	spca506_WriteI2c(gspca_dev, 0x00, 0x0d);
	spca506_WriteI2c(gspca_dev, 0x03, 0x0e);	/* Chroma Pal adjust */
	spca506_WriteI2c(gspca_dev, 0x2a, 0x0f);
	spca506_WriteI2c(gspca_dev, 0x00, 0x10);
	spca506_WriteI2c(gspca_dev, 0x0c, 0x11);
	spca506_WriteI2c(gspca_dev, 0xb8, 0x12);
	spca506_WriteI2c(gspca_dev, 0x01, 0x13);
	spca506_WriteI2c(gspca_dev, 0x00, 0x14);
	spca506_WriteI2c(gspca_dev, 0x00, 0x15);
	spca506_WriteI2c(gspca_dev, 0x00, 0x16);
	spca506_WriteI2c(gspca_dev, 0x00, 0x17);
	spca506_WriteI2c(gspca_dev, 0x00, 0x18);
	spca506_WriteI2c(gspca_dev, 0x00, 0x19);
	spca506_WriteI2c(gspca_dev, 0x00, 0x1a);
	spca506_WriteI2c(gspca_dev, 0x00, 0x1b);
	spca506_WriteI2c(gspca_dev, 0x00, 0x1c);
	spca506_WriteI2c(gspca_dev, 0x00, 0x1d);
	spca506_WriteI2c(gspca_dev, 0x00, 0x1e);
	spca506_WriteI2c(gspca_dev, 0xa1, 0x1f);
	spca506_WriteI2c(gspca_dev, 0x02, 0x40);
	spca506_WriteI2c(gspca_dev, 0xff, 0x41);
	spca506_WriteI2c(gspca_dev, 0xff, 0x42);
	spca506_WriteI2c(gspca_dev, 0xff, 0x43);
	spca506_WriteI2c(gspca_dev, 0xff, 0x44);
	spca506_WriteI2c(gspca_dev, 0xff, 0x45);
	spca506_WriteI2c(gspca_dev, 0xff, 0x46);
	spca506_WriteI2c(gspca_dev, 0xff, 0x47);
	spca506_WriteI2c(gspca_dev, 0xff, 0x48);
	spca506_WriteI2c(gspca_dev, 0xff, 0x49);
	spca506_WriteI2c(gspca_dev, 0xff, 0x4a);
	spca506_WriteI2c(gspca_dev, 0xff, 0x4b);
	spca506_WriteI2c(gspca_dev, 0xff, 0x4c);
	spca506_WriteI2c(gspca_dev, 0xff, 0x4d);
	spca506_WriteI2c(gspca_dev, 0xff, 0x4e);
	spca506_WriteI2c(gspca_dev, 0xff, 0x4f);
	spca506_WriteI2c(gspca_dev, 0xff, 0x50);
	spca506_WriteI2c(gspca_dev, 0xff, 0x51);
	spca506_WriteI2c(gspca_dev, 0xff, 0x52);
	spca506_WriteI2c(gspca_dev, 0xff, 0x53);
	spca506_WriteI2c(gspca_dev, 0xff, 0x54);
	spca506_WriteI2c(gspca_dev, 0xff, 0x55);
	spca506_WriteI2c(gspca_dev, 0xff, 0x56);
	spca506_WriteI2c(gspca_dev, 0xff, 0x57);
	spca506_WriteI2c(gspca_dev, 0x00, 0x58);
	spca506_WriteI2c(gspca_dev, 0x54, 0x59);
	spca506_WriteI2c(gspca_dev, 0x07, 0x5a);
	spca506_WriteI2c(gspca_dev, 0x83, 0x5b);
	spca506_WriteI2c(gspca_dev, 0x00, 0x5c);
	spca506_WriteI2c(gspca_dev, 0x00, 0x5d);
	spca506_WriteI2c(gspca_dev, 0x00, 0x5e);
	spca506_WriteI2c(gspca_dev, 0x00, 0x5f);
	spca506_WriteI2c(gspca_dev, 0x00, 0x60);
	spca506_WriteI2c(gspca_dev, 0x05, 0x61);
	spca506_WriteI2c(gspca_dev, 0x9f, 0x62);
	PDEBUG(D_STREAM, "** Close Init *");
	return 0;
}

static int sd_start(struct gspca_dev *gspca_dev)
{
	struct usb_device *dev = gspca_dev->dev;
	__u16 norme;
	__u16 channel;

	/**************************************/
	reg_w(dev, 0x03, 0x00, 0x0004);
	reg_w(dev, 0x03, 0x00, 0x0003);
	reg_w(dev, 0x03, 0x00, 0x0004);
	reg_w(dev, 0x03, 0xFF, 0x0003);
	reg_w(dev, 0x02, 0x00, 0x0000);
	reg_w(dev, 0x03, 0x60, 0x0000);
	reg_w(dev, 0x03, 0x18, 0x0001);

	/*sdca506_WriteI2c(value,register) */
	spca506_Initi2c(gspca_dev);
	spca506_WriteI2c(gspca_dev, 0x08, 0x01);	/* Increment Delay */
/*	spca506_WriteI2c(gspca_dev, 0xc0, 0x02); * Analog Input Control 1 */
	spca506_WriteI2c(gspca_dev, 0x33, 0x03);
						/* Analog Input Control 2 */
	spca506_WriteI2c(gspca_dev, 0x00, 0x04);
						/* Analog Input Control 3 */
	spca506_WriteI2c(gspca_dev, 0x00, 0x05);
						/* Analog Input Control 4 */
	spca506_WriteI2c(gspca_dev, 0x0d, 0x06);
					/* Horizontal Sync Start 0xe9-0x0d */
	spca506_WriteI2c(gspca_dev, 0xf0, 0x07);
					/* Horizontal Sync Stop  0x0d-0xf0 */

	spca506_WriteI2c(gspca_dev, 0x98, 0x08);	/* Sync Control */
/*		Defaults value			*/
	spca506_WriteI2c(gspca_dev, 0x03, 0x09);	/* Luminance Control */
	spca506_WriteI2c(gspca_dev, 0x80, 0x0a);
						/* Luminance Brightness */
	spca506_WriteI2c(gspca_dev, 0x47, 0x0b);	/* Luminance Contrast */
	spca506_WriteI2c(gspca_dev, 0x48, 0x0c);
						/* Chrominance Saturation */
	spca506_WriteI2c(gspca_dev, 0x00, 0x0d);
						/* Chrominance Hue Control */
	spca506_WriteI2c(gspca_dev, 0x2a, 0x0f);
						/* Chrominance Gain Control */
	/**************************************/
	spca506_WriteI2c(gspca_dev, 0x00, 0x10);
						/* Format/Delay Control */
	spca506_WriteI2c(gspca_dev, 0x0c, 0x11);	/* Output Control 1 */
	spca506_WriteI2c(gspca_dev, 0xb8, 0x12);	/* Output Control 2 */
	spca506_WriteI2c(gspca_dev, 0x01, 0x13);	/* Output Control 3 */
	spca506_WriteI2c(gspca_dev, 0x00, 0x14);	/* reserved */
	spca506_WriteI2c(gspca_dev, 0x00, 0x15);	/* VGATE START */
	spca506_WriteI2c(gspca_dev, 0x00, 0x16);	/* VGATE STOP */
	spca506_WriteI2c(gspca_dev, 0x00, 0x17);    /* VGATE Control (MSB) */
	spca506_WriteI2c(gspca_dev, 0x00, 0x18);
	spca506_WriteI2c(gspca_dev, 0x00, 0x19);
	spca506_WriteI2c(gspca_dev, 0x00, 0x1a);
	spca506_WriteI2c(gspca_dev, 0x00, 0x1b);
	spca506_WriteI2c(gspca_dev, 0x00, 0x1c);
	spca506_WriteI2c(gspca_dev, 0x00, 0x1d);
	spca506_WriteI2c(gspca_dev, 0x00, 0x1e);
	spca506_WriteI2c(gspca_dev, 0xa1, 0x1f);
	spca506_WriteI2c(gspca_dev, 0x02, 0x40);
	spca506_WriteI2c(gspca_dev, 0xff, 0x41);
	spca506_WriteI2c(gspca_dev, 0xff, 0x42);
	spca506_WriteI2c(gspca_dev, 0xff, 0x43);
	spca506_WriteI2c(gspca_dev, 0xff, 0x44);
	spca506_WriteI2c(gspca_dev, 0xff, 0x45);
	spca506_WriteI2c(gspca_dev, 0xff, 0x46);
	spca506_WriteI2c(gspca_dev, 0xff, 0x47);
	spca506_WriteI2c(gspca_dev, 0xff, 0x48);
	spca506_WriteI2c(gspca_dev, 0xff, 0x49);
	spca506_WriteI2c(gspca_dev, 0xff, 0x4a);
	spca506_WriteI2c(gspca_dev, 0xff, 0x4b);
	spca506_WriteI2c(gspca_dev, 0xff, 0x4c);
	spca506_WriteI2c(gspca_dev, 0xff, 0x4d);
	spca506_WriteI2c(gspca_dev, 0xff, 0x4e);
	spca506_WriteI2c(gspca_dev, 0xff, 0x4f);
	spca506_WriteI2c(gspca_dev, 0xff, 0x50);
	spca506_WriteI2c(gspca_dev, 0xff, 0x51);
	spca506_WriteI2c(gspca_dev, 0xff, 0x52);
	spca506_WriteI2c(gspca_dev, 0xff, 0x53);
	spca506_WriteI2c(gspca_dev, 0xff, 0x54);
	spca506_WriteI2c(gspca_dev, 0xff, 0x55);
	spca506_WriteI2c(gspca_dev, 0xff, 0x56);
	spca506_WriteI2c(gspca_dev, 0xff, 0x57);
	spca506_WriteI2c(gspca_dev, 0x00, 0x58);
	spca506_WriteI2c(gspca_dev, 0x54, 0x59);
	spca506_WriteI2c(gspca_dev, 0x07, 0x5a);
	spca506_WriteI2c(gspca_dev, 0x83, 0x5b);
	spca506_WriteI2c(gspca_dev, 0x00, 0x5c);
	spca506_WriteI2c(gspca_dev, 0x00, 0x5d);
	spca506_WriteI2c(gspca_dev, 0x00, 0x5e);
	spca506_WriteI2c(gspca_dev, 0x00, 0x5f);
	spca506_WriteI2c(gspca_dev, 0x00, 0x60);
	spca506_WriteI2c(gspca_dev, 0x05, 0x61);
	spca506_WriteI2c(gspca_dev, 0x9f, 0x62);
	/**************************************/
	reg_w(dev, 0x05, 0x00, 0x0003);
	reg_w(dev, 0x05, 0x00, 0x0004);
	reg_w(dev, 0x03, 0x10, 0x0001);
	reg_w(dev, 0x03, 0x78, 0x0000);
	switch (gspca_dev->cam.cam_mode[(int) gspca_dev->curr_mode].priv) {
	case 0:
		spca506_Setsize(gspca_dev, 0, 0x10, 0x10);
		break;
	case 1:
		spca506_Setsize(gspca_dev, 1, 0x1a, 0x1a);
		break;
	case 2:
		spca506_Setsize(gspca_dev, 2, 0x1c, 0x1c);
		break;
	case 4:
		spca506_Setsize(gspca_dev, 4, 0x34, 0x34);
		break;
	default:
/*	case 5: */
		spca506_Setsize(gspca_dev, 5, 0x40, 0x40);
		break;
	}

	/* compress setting and size */
	/* set i2c luma */
	reg_w(dev, 0x02, 0x01, 0x0000);
	reg_w(dev, 0x03, 0x12, 0x0000);
	reg_r(gspca_dev, 0x04, 0x0001, 2);
	PDEBUG(D_STREAM, "webcam started");
	spca506_GetNormeInput(gspca_dev, &norme, &channel);
	spca506_SetNormeInput(gspca_dev, norme, channel);
	return 0;
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	struct usb_device *dev = gspca_dev->dev;

	reg_w(dev, 0x02, 0x00, 0x0000);
	reg_w(dev, 0x03, 0x00, 0x0004);
	reg_w(dev, 0x03, 0x00, 0x0003);
}

static void sd_pkt_scan(struct gspca_dev *gspca_dev,
			u8 *data,			/* isoc packet */
			int len)			/* iso packet length */
{
	switch (data[0]) {
	case 0:				/* start of frame */
		gspca_frame_add(gspca_dev, LAST_PACKET, NULL, 0);
		data += SPCA50X_OFFSET_DATA;
		len -= SPCA50X_OFFSET_DATA;
		gspca_frame_add(gspca_dev, FIRST_PACKET, data, len);
		break;
	case 0xff:			/* drop */
/*		gspca_dev->last_packet_type = DISCARD_PACKET; */
		break;
	default:
		data += 1;
		len -= 1;
		gspca_frame_add(gspca_dev, INTER_PACKET, data, len);
		break;
	}
}

static void setbrightness(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	spca506_Initi2c(gspca_dev);
	spca506_WriteI2c(gspca_dev, sd->brightness, SAA7113_bright);
	spca506_WriteI2c(gspca_dev, 0x01, 0x09);
}

static void setcontrast(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	spca506_Initi2c(gspca_dev);
	spca506_WriteI2c(gspca_dev, sd->contrast, SAA7113_contrast);
	spca506_WriteI2c(gspca_dev, 0x01, 0x09);
}

static void setcolors(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	spca506_Initi2c(gspca_dev);
	spca506_WriteI2c(gspca_dev, sd->colors, SAA7113_saturation);
	spca506_WriteI2c(gspca_dev, 0x01, 0x09);
}

static void sethue(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	spca506_Initi2c(gspca_dev);
	spca506_WriteI2c(gspca_dev, sd->hue, SAA7113_hue);
	spca506_WriteI2c(gspca_dev, 0x01, 0x09);
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

static int sd_sethue(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->hue = val;
	if (gspca_dev->streaming)
		sethue(gspca_dev);
	return 0;
}

static int sd_gethue(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->hue;
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
	.pkt_scan = sd_pkt_scan,
};

/* -- module initialisation -- */
static const struct usb_device_id device_table[] = {
	{USB_DEVICE(0x06e1, 0xa190)},
/*fixme: may be IntelPCCameraPro BRIDGE_SPCA505
	{USB_DEVICE(0x0733, 0x0430)}, */
	{USB_DEVICE(0x0734, 0x043b)},
	{USB_DEVICE(0x99fa, 0x8988)},
	{}
};
MODULE_DEVICE_TABLE(usb, device_table);

/* -- device connect -- */
static int __devinit sd_probe(struct usb_interface *intf,
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

module_usb_driver(sd_driver);
