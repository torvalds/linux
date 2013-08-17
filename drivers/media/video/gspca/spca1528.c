/*
 * spca1528 subdriver
 *
 * Copyright (C) 2010-2011 Jean-Francois Moine (http://moinejf.free.fr)
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

#define MODULE_NAME "spca1528"

#include "gspca.h"
#include "jpeg.h"

MODULE_AUTHOR("Jean-Francois Moine <http://moinejf.free.fr>");
MODULE_DESCRIPTION("SPCA1528 USB Camera Driver");
MODULE_LICENSE("GPL");

/* specific webcam descriptor */
struct sd {
	struct gspca_dev gspca_dev;	/* !! must be the first item */

	u8 brightness;
	u8 contrast;
	u8 hue;
	u8 color;
	u8 sharpness;

	u8 pkt_seq;

	u8 jpeg_hdr[JPEG_HDR_SZ];
};

/* V4L2 controls supported by the driver */
static int sd_setbrightness(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getbrightness(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setcontrast(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getcontrast(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_sethue(struct gspca_dev *gspca_dev, __s32 val);
static int sd_gethue(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setcolor(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getcolor(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setsharpness(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getsharpness(struct gspca_dev *gspca_dev, __s32 *val);

static const struct ctrl sd_ctrls[] = {
	{
	    {
		.id      = V4L2_CID_BRIGHTNESS,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Brightness",
		.minimum = 0,
		.maximum = 255,
		.step    = 1,
#define BRIGHTNESS_DEF 128
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
		.maximum = 8,
		.step    = 1,
#define CONTRAST_DEF 1
		.default_value = CONTRAST_DEF,
	    },
	    .set = sd_setcontrast,
	    .get = sd_getcontrast,
	},
	{
	    {
		.id      = V4L2_CID_HUE,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Hue",
		.minimum = 0,
		.maximum = 255,
		.step    = 1,
#define HUE_DEF 0
		.default_value = HUE_DEF,
	    },
	    .set = sd_sethue,
	    .get = sd_gethue,
	},
	{
	    {
		.id      = V4L2_CID_SATURATION,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Saturation",
		.minimum = 0,
		.maximum = 8,
		.step    = 1,
#define COLOR_DEF 1
		.default_value = COLOR_DEF,
	    },
	    .set = sd_setcolor,
	    .get = sd_getcolor,
	},
	{
	    {
		.id	 = V4L2_CID_SHARPNESS,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Sharpness",
		.minimum = 0,
		.maximum = 255,
		.step    = 1,
#define SHARPNESS_DEF 0
		.default_value = SHARPNESS_DEF,
	    },
	    .set = sd_setsharpness,
	    .get = sd_getsharpness,
	},
};

static const struct v4l2_pix_format vga_mode[] = {
/*		(does not work correctly)
	{176, 144, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 176,
		.sizeimage = 176 * 144 * 5 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 3},
*/
	{320, 240, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240 * 4 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 2},
	{640, 480, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 640,
		.sizeimage = 640 * 480 * 3 / 8 + 590,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 1},
};

/* read <len> bytes to gspca usb_buf */
static void reg_r(struct gspca_dev *gspca_dev,
			u8 req,
			u16 index,
			int len)
{
#if USB_BUF_SZ < 64
#error "USB buffer too small"
#endif
	struct usb_device *dev = gspca_dev->dev;
	int ret;

	if (gspca_dev->usb_err < 0)
		return;
	ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
			req,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0x0000,			/* value */
			index,
			gspca_dev->usb_buf, len,
			500);
	PDEBUG(D_USBI, "GET %02x 0000 %04x %02x", req, index,
			 gspca_dev->usb_buf[0]);
	if (ret < 0) {
		pr_err("reg_r err %d\n", ret);
		gspca_dev->usb_err = ret;
	}
}

static void reg_w(struct gspca_dev *gspca_dev,
			u8 req,
			u16 value,
			u16 index)
{
	struct usb_device *dev = gspca_dev->dev;
	int ret;

	if (gspca_dev->usb_err < 0)
		return;
	PDEBUG(D_USBO, "SET %02x %04x %04x", req, value, index);
	ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
			req,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			value, index,
			NULL, 0, 500);
	if (ret < 0) {
		pr_err("reg_w err %d\n", ret);
		gspca_dev->usb_err = ret;
	}
}

static void reg_wb(struct gspca_dev *gspca_dev,
			u8 req,
			u16 value,
			u16 index,
			u8 byte)
{
	struct usb_device *dev = gspca_dev->dev;
	int ret;

	if (gspca_dev->usb_err < 0)
		return;
	PDEBUG(D_USBO, "SET %02x %04x %04x %02x", req, value, index, byte);
	gspca_dev->usb_buf[0] = byte;
	ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
			req,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			value, index,
			gspca_dev->usb_buf, 1, 500);
	if (ret < 0) {
		pr_err("reg_w err %d\n", ret);
		gspca_dev->usb_err = ret;
	}
}

static void wait_status_0(struct gspca_dev *gspca_dev)
{
	int i, w;

	i = 16;
	w = 0;
	do {
		reg_r(gspca_dev, 0x21, 0x0000, 1);
		if (gspca_dev->usb_buf[0] == 0)
			return;
		w += 15;
		msleep(w);
	} while (--i > 0);
	PDEBUG(D_ERR, "wait_status_0 timeout");
	gspca_dev->usb_err = -ETIME;
}

static void wait_status_1(struct gspca_dev *gspca_dev)
{
	int i;

	i = 10;
	do {
		reg_r(gspca_dev, 0x21, 0x0001, 1);
		msleep(10);
		if (gspca_dev->usb_buf[0] == 1) {
			reg_wb(gspca_dev, 0x21, 0x0000, 0x0001, 0x00);
			reg_r(gspca_dev, 0x21, 0x0001, 1);
			return;
		}
	} while (--i > 0);
	PDEBUG(D_ERR, "wait_status_1 timeout");
	gspca_dev->usb_err = -ETIME;
}

static void setbrightness(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	reg_wb(gspca_dev, 0xc0, 0x0000, 0x00c0, sd->brightness);
}

static void setcontrast(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	reg_wb(gspca_dev, 0xc1, 0x0000, 0x00c1, sd->contrast);
}

static void sethue(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	reg_wb(gspca_dev, 0xc2, 0x0000, 0x0000, sd->hue);
}

static void setcolor(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	reg_wb(gspca_dev, 0xc3, 0x0000, 0x00c3, sd->color);
}

static void setsharpness(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	reg_wb(gspca_dev, 0xc4, 0x0000, 0x00c4, sd->sharpness);
}

/* this function is called at probe time */
static int sd_config(struct gspca_dev *gspca_dev,
			const struct usb_device_id *id)
{
	struct sd *sd = (struct sd *) gspca_dev;

	gspca_dev->cam.cam_mode = vga_mode;
	gspca_dev->cam.nmodes = ARRAY_SIZE(vga_mode);
	gspca_dev->cam.npkt = 128; /* number of packets per ISOC message */
			/*fixme: 256 in ms-win traces*/

	sd->brightness = BRIGHTNESS_DEF;
	sd->contrast = CONTRAST_DEF;
	sd->hue = HUE_DEF;
	sd->color = COLOR_DEF;
	sd->sharpness = SHARPNESS_DEF;

	return 0;
}

/* this function is called at probe and resume time */
static int sd_init(struct gspca_dev *gspca_dev)
{
	reg_w(gspca_dev, 0x00, 0x0001, 0x2067);
	reg_w(gspca_dev, 0x00, 0x00d0, 0x206b);
	reg_w(gspca_dev, 0x00, 0x0000, 0x206c);
	reg_w(gspca_dev, 0x00, 0x0001, 0x2069);
	msleep(8);
	reg_w(gspca_dev, 0x00, 0x00c0, 0x206b);
	reg_w(gspca_dev, 0x00, 0x0000, 0x206c);
	reg_w(gspca_dev, 0x00, 0x0001, 0x2069);

	reg_r(gspca_dev, 0x20, 0x0000, 1);
	reg_r(gspca_dev, 0x20, 0x0000, 5);
	reg_r(gspca_dev, 0x23, 0x0000, 64);
	PDEBUG(D_PROBE, "%s%s", &gspca_dev->usb_buf[0x1c],
				&gspca_dev->usb_buf[0x30]);
	reg_r(gspca_dev, 0x23, 0x0001, 64);
	return gspca_dev->usb_err;
}

/* function called at start time before URB creation */
static int sd_isoc_init(struct gspca_dev *gspca_dev)
{
	u8 mode;

	reg_r(gspca_dev, 0x00, 0x2520, 1);
	wait_status_0(gspca_dev);
	reg_w(gspca_dev, 0xc5, 0x0003, 0x0000);
	wait_status_1(gspca_dev);

	wait_status_0(gspca_dev);
	mode = gspca_dev->cam.cam_mode[gspca_dev->curr_mode].priv;
	reg_wb(gspca_dev, 0x25, 0x0000, 0x0004, mode);
	reg_r(gspca_dev, 0x25, 0x0004, 1);
	reg_wb(gspca_dev, 0x27, 0x0000, 0x0000, 0x06);	/* 420 */
	reg_r(gspca_dev, 0x27, 0x0000, 1);

/* not useful..
	gspca_dev->alt = 4;		* use alternate setting 3 */

	return gspca_dev->usb_err;
}

/* -- start the camera -- */
static int sd_start(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	/* initialize the JPEG header */
	jpeg_define(sd->jpeg_hdr, gspca_dev->height, gspca_dev->width,
			0x22);		/* JPEG 411 */

	/* the JPEG quality shall be 85% */
	jpeg_set_qual(sd->jpeg_hdr, 85);

	/* set the controls */
	setbrightness(gspca_dev);
	setcontrast(gspca_dev);
	sethue(gspca_dev);
	setcolor(gspca_dev);
	setsharpness(gspca_dev);

	msleep(5);
	reg_r(gspca_dev, 0x00, 0x2520, 1);
	msleep(8);

	/* start the capture */
	wait_status_0(gspca_dev);
	reg_w(gspca_dev, 0x31, 0x0000, 0x0004);	/* start request */
	wait_status_1(gspca_dev);
	wait_status_0(gspca_dev);
	msleep(200);

	sd->pkt_seq = 0;
	return gspca_dev->usb_err;
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	/* stop the capture */
	wait_status_0(gspca_dev);
	reg_w(gspca_dev, 0x31, 0x0000, 0x0000);	/* stop request */
	wait_status_1(gspca_dev);
	wait_status_0(gspca_dev);
}

/* move a packet adding 0x00 after 0xff */
static void add_packet(struct gspca_dev *gspca_dev,
			u8 *data,
			int len)
{
	int i;

	i = 0;
	do {
		if (data[i] == 0xff) {
			gspca_frame_add(gspca_dev, INTER_PACKET,
					data, i + 1);
			len -= i;
			data += i;
			*data = 0x00;
			i = 0;
		}
	} while (++i < len);
	gspca_frame_add(gspca_dev, INTER_PACKET, data, len);
}

static void sd_pkt_scan(struct gspca_dev *gspca_dev,
			u8 *data,			/* isoc packet */
			int len)			/* iso packet length */
{
	struct sd *sd = (struct sd *) gspca_dev;
	static const u8 ffd9[] = {0xff, 0xd9};

	/* image packets start with:
	 *	02 8n
	 * with <n> bit:
	 *	0x01: even (0) / odd (1) image
	 *	0x02: end of image when set
	 */
	if (len < 3)
		return;				/* empty packet */
	if (*data == 0x02) {
		if (data[1] & 0x02) {
			sd->pkt_seq = !(data[1] & 1);
			add_packet(gspca_dev, data + 2, len - 2);
			gspca_frame_add(gspca_dev, LAST_PACKET,
					ffd9, 2);
			return;
		}
		if ((data[1] & 1) != sd->pkt_seq)
			goto err;
		if (gspca_dev->last_packet_type == LAST_PACKET)
			gspca_frame_add(gspca_dev, FIRST_PACKET,
					sd->jpeg_hdr, JPEG_HDR_SZ);
		add_packet(gspca_dev, data + 2, len - 2);
		return;
	}
err:
	gspca_dev->last_packet_type = DISCARD_PACKET;
}

static int sd_setbrightness(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->brightness = val;
	if (gspca_dev->streaming)
		setbrightness(gspca_dev);
	return gspca_dev->usb_err;
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
	return gspca_dev->usb_err;
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
	if (gspca_dev->streaming)
		sethue(gspca_dev);
	return gspca_dev->usb_err;
}

static int sd_gethue(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->hue;
	return 0;
}

static int sd_setcolor(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->color = val;
	if (gspca_dev->streaming)
		setcolor(gspca_dev);
	return gspca_dev->usb_err;
}

static int sd_getcolor(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->color;
	return 0;
}

static int sd_setsharpness(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->sharpness = val;
	if (gspca_dev->streaming)
		setsharpness(gspca_dev);
	return gspca_dev->usb_err;
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
	.isoc_init = sd_isoc_init,
	.start = sd_start,
	.stopN = sd_stopN,
	.pkt_scan = sd_pkt_scan,
};

/* -- module initialisation -- */
static const struct usb_device_id device_table[] = {
	{USB_DEVICE(0x04fc, 0x1528)},
	{}
};
MODULE_DEVICE_TABLE(usb, device_table);

/* -- device connect -- */
static int sd_probe(struct usb_interface *intf,
			const struct usb_device_id *id)
{
	/* the video interface for isochronous transfer is 1 */
	if (intf->cur_altsetting->desc.bInterfaceNumber != 1)
		return -ENODEV;

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

module_usb_driver(sd_driver);
