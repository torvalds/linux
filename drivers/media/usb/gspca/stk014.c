// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Syntek DV4000 (STK014) subdriver
 *
 * Copyright (C) 2008 Jean-Francois Moine (http://moinejf.free.fr)
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define MODULE_NAME "stk014"

#include "gspca.h"
#include "jpeg.h"

MODULE_AUTHOR("Jean-Francois Moine <http://moinejf.free.fr>");
MODULE_DESCRIPTION("Syntek DV4000 (STK014) USB Camera Driver");
MODULE_LICENSE("GPL");

#define QUALITY 50

/* specific webcam descriptor */
struct sd {
	struct gspca_dev gspca_dev;	/* !! must be the first item */
	u8 jpeg_hdr[JPEG_HDR_SZ];
};

static const struct v4l2_pix_format vga_mode[] = {
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

/* -- read a register -- */
static u8 reg_r(struct gspca_dev *gspca_dev,
			__u16 index)
{
	struct usb_device *dev = gspca_dev->dev;
	int ret;

	if (gspca_dev->usb_err < 0)
		return 0;
	ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
			0x00,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0x00,
			index,
			gspca_dev->usb_buf, 1,
			500);
	if (ret < 0) {
		pr_err("reg_r err %d\n", ret);
		gspca_dev->usb_err = ret;
		return 0;
	}
	return gspca_dev->usb_buf[0];
}

/* -- write a register -- */
static void reg_w(struct gspca_dev *gspca_dev,
			__u16 index, __u16 value)
{
	struct usb_device *dev = gspca_dev->dev;
	int ret;

	if (gspca_dev->usb_err < 0)
		return;
	ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
			0x01,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			value,
			index,
			NULL,
			0,
			500);
	if (ret < 0) {
		pr_err("reg_w err %d\n", ret);
		gspca_dev->usb_err = ret;
	}
}

/* -- get a bulk value (4 bytes) -- */
static void rcv_val(struct gspca_dev *gspca_dev,
			int ads)
{
	struct usb_device *dev = gspca_dev->dev;
	int alen, ret;

	reg_w(gspca_dev, 0x634, (ads >> 16) & 0xff);
	reg_w(gspca_dev, 0x635, (ads >> 8) & 0xff);
	reg_w(gspca_dev, 0x636, ads & 0xff);
	reg_w(gspca_dev, 0x637, 0);
	reg_w(gspca_dev, 0x638, 4);	/* len & 0xff */
	reg_w(gspca_dev, 0x639, 0);	/* len >> 8 */
	reg_w(gspca_dev, 0x63a, 0);
	reg_w(gspca_dev, 0x63b, 0);
	reg_w(gspca_dev, 0x630, 5);
	if (gspca_dev->usb_err < 0)
		return;
	ret = usb_bulk_msg(dev,
			usb_rcvbulkpipe(dev, 0x05),
			gspca_dev->usb_buf,
			4,		/* length */
			&alen,
			500);		/* timeout in milliseconds */
	if (ret < 0) {
		pr_err("rcv_val err %d\n", ret);
		gspca_dev->usb_err = ret;
	}
}

/* -- send a bulk value -- */
static void snd_val(struct gspca_dev *gspca_dev,
			int ads,
			unsigned int val)
{
	struct usb_device *dev = gspca_dev->dev;
	int alen, ret;
	__u8 seq = 0;

	if (ads == 0x003f08) {
		reg_r(gspca_dev, 0x0704);
		seq = reg_r(gspca_dev, 0x0705);
		reg_r(gspca_dev, 0x0650);
		reg_w(gspca_dev, 0x654, seq);
	} else {
		reg_w(gspca_dev, 0x654, (ads >> 16) & 0xff);
	}
	reg_w(gspca_dev, 0x655, (ads >> 8) & 0xff);
	reg_w(gspca_dev, 0x656, ads & 0xff);
	reg_w(gspca_dev, 0x657, 0);
	reg_w(gspca_dev, 0x658, 0x04);	/* size */
	reg_w(gspca_dev, 0x659, 0);
	reg_w(gspca_dev, 0x65a, 0);
	reg_w(gspca_dev, 0x65b, 0);
	reg_w(gspca_dev, 0x650, 5);
	if (gspca_dev->usb_err < 0)
		return;
	gspca_dev->usb_buf[0] = val >> 24;
	gspca_dev->usb_buf[1] = val >> 16;
	gspca_dev->usb_buf[2] = val >> 8;
	gspca_dev->usb_buf[3] = val;
	ret = usb_bulk_msg(dev,
			usb_sndbulkpipe(dev, 6),
			gspca_dev->usb_buf,
			4,
			&alen,
			500);	/* timeout in milliseconds */
	if (ret < 0) {
		pr_err("snd_val err %d\n", ret);
		gspca_dev->usb_err = ret;
	} else {
		if (ads == 0x003f08) {
			seq += 4;
			seq &= 0x3f;
			reg_w(gspca_dev, 0x705, seq);
		}
	}
}

/* set a camera parameter */
static void set_par(struct gspca_dev *gspca_dev,
		   int parval)
{
	snd_val(gspca_dev, 0x003f08, parval);
}

static void setbrightness(struct gspca_dev *gspca_dev, s32 val)
{
	int parval;

	parval = 0x06000000		/* whiteness */
		+ (val << 16);
	set_par(gspca_dev, parval);
}

static void setcontrast(struct gspca_dev *gspca_dev, s32 val)
{
	int parval;

	parval = 0x07000000		/* contrast */
		+ (val << 16);
	set_par(gspca_dev, parval);
}

static void setcolors(struct gspca_dev *gspca_dev, s32 val)
{
	int parval;

	parval = 0x08000000		/* saturation */
		+ (val << 16);
	set_par(gspca_dev, parval);
}

static void setlightfreq(struct gspca_dev *gspca_dev, s32 val)
{
	set_par(gspca_dev, val == 1
			? 0x33640000		/* 50 Hz */
			: 0x33780000);		/* 60 Hz */
}

/* this function is called at probe time */
static int sd_config(struct gspca_dev *gspca_dev,
			const struct usb_device_id *id)
{
	gspca_dev->cam.cam_mode = vga_mode;
	gspca_dev->cam.nmodes = ARRAY_SIZE(vga_mode);
	return 0;
}

/* this function is called at probe and resume time */
static int sd_init(struct gspca_dev *gspca_dev)
{
	u8 ret;

	/* check if the device responds */
	usb_set_interface(gspca_dev->dev, gspca_dev->iface, 1);
	ret = reg_r(gspca_dev, 0x0740);
	if (gspca_dev->usb_err >= 0) {
		if (ret != 0xff) {
			pr_err("init reg: 0x%02x\n", ret);
			gspca_dev->usb_err = -EIO;
		}
	}
	return gspca_dev->usb_err;
}

/* -- start the camera -- */
static int sd_start(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int ret, value;

	/* create the JPEG header */
	jpeg_define(sd->jpeg_hdr, gspca_dev->pixfmt.height,
			gspca_dev->pixfmt.width,
			0x22);		/* JPEG 411 */
	jpeg_set_qual(sd->jpeg_hdr, QUALITY);

	/* work on alternate 1 */
	usb_set_interface(gspca_dev->dev, gspca_dev->iface, 1);

	set_par(gspca_dev, 0x10000000);
	set_par(gspca_dev, 0x00000000);
	set_par(gspca_dev, 0x8002e001);
	set_par(gspca_dev, 0x14000000);
	if (gspca_dev->pixfmt.width > 320)
		value = 0x8002e001;		/* 640x480 */
	else
		value = 0x4001f000;		/* 320x240 */
	set_par(gspca_dev, value);
	ret = usb_set_interface(gspca_dev->dev,
					gspca_dev->iface,
					gspca_dev->alt);
	if (ret < 0) {
		pr_err("set intf %d %d failed\n",
		       gspca_dev->iface, gspca_dev->alt);
		gspca_dev->usb_err = ret;
		goto out;
	}
	reg_r(gspca_dev, 0x0630);
	rcv_val(gspca_dev, 0x000020);	/* << (value ff ff ff ff) */
	reg_r(gspca_dev, 0x0650);
	snd_val(gspca_dev, 0x000020, 0xffffffff);
	reg_w(gspca_dev, 0x0620, 0);
	reg_w(gspca_dev, 0x0630, 0);
	reg_w(gspca_dev, 0x0640, 0);
	reg_w(gspca_dev, 0x0650, 0);
	reg_w(gspca_dev, 0x0660, 0);
	set_par(gspca_dev, 0x09800000);		/* Red ? */
	set_par(gspca_dev, 0x0a800000);		/* Green ? */
	set_par(gspca_dev, 0x0b800000);		/* Blue ? */
	set_par(gspca_dev, 0x0d030000);		/* Gamma ? */

	/* start the video flow */
	set_par(gspca_dev, 0x01000000);
	set_par(gspca_dev, 0x01000000);
	if (gspca_dev->usb_err >= 0)
		gspca_dbg(gspca_dev, D_STREAM, "camera started alt: 0x%02x\n",
			  gspca_dev->alt);
out:
	return gspca_dev->usb_err;
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	struct usb_device *dev = gspca_dev->dev;

	set_par(gspca_dev, 0x02000000);
	set_par(gspca_dev, 0x02000000);
	usb_set_interface(dev, gspca_dev->iface, 1);
	reg_r(gspca_dev, 0x0630);
	rcv_val(gspca_dev, 0x000020);	/* << (value ff ff ff ff) */
	reg_r(gspca_dev, 0x0650);
	snd_val(gspca_dev, 0x000020, 0xffffffff);
	reg_w(gspca_dev, 0x0620, 0);
	reg_w(gspca_dev, 0x0630, 0);
	reg_w(gspca_dev, 0x0640, 0);
	reg_w(gspca_dev, 0x0650, 0);
	reg_w(gspca_dev, 0x0660, 0);
	gspca_dbg(gspca_dev, D_STREAM, "camera stopped\n");
}

static void sd_pkt_scan(struct gspca_dev *gspca_dev,
			u8 *data,			/* isoc packet */
			int len)			/* iso packet length */
{
	struct sd *sd = (struct sd *) gspca_dev;
	static unsigned char ffd9[] = {0xff, 0xd9};

	/* a frame starts with:
	 *	- 0xff 0xfe
	 *	- 0x08 0x00	- length (little endian ?!)
	 *	- 4 bytes = size of whole frame (BE - including header)
	 *	- 0x00 0x0c
	 *	- 0xff 0xd8
	 *	- ..	JPEG image with escape sequences (ff 00)
	 *		(without ending - ff d9)
	 */
	if (data[0] == 0xff && data[1] == 0xfe) {
		gspca_frame_add(gspca_dev, LAST_PACKET,
				ffd9, 2);

		/* put the JPEG 411 header */
		gspca_frame_add(gspca_dev, FIRST_PACKET,
			sd->jpeg_hdr, JPEG_HDR_SZ);

		/* beginning of the frame */
#define STKHDRSZ 12
		data += STKHDRSZ;
		len -= STKHDRSZ;
	}
	gspca_frame_add(gspca_dev, INTER_PACKET, data, len);
}

static int sd_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gspca_dev *gspca_dev =
		container_of(ctrl->handler, struct gspca_dev, ctrl_handler);

	gspca_dev->usb_err = 0;

	if (!gspca_dev->streaming)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		setbrightness(gspca_dev, ctrl->val);
		break;
	case V4L2_CID_CONTRAST:
		setcontrast(gspca_dev, ctrl->val);
		break;
	case V4L2_CID_SATURATION:
		setcolors(gspca_dev, ctrl->val);
		break;
	case V4L2_CID_POWER_LINE_FREQUENCY:
		setlightfreq(gspca_dev, ctrl->val);
		break;
	}
	return gspca_dev->usb_err;
}

static const struct v4l2_ctrl_ops sd_ctrl_ops = {
	.s_ctrl = sd_s_ctrl,
};

static int sd_init_controls(struct gspca_dev *gspca_dev)
{
	struct v4l2_ctrl_handler *hdl = &gspca_dev->ctrl_handler;

	gspca_dev->vdev.ctrl_handler = hdl;
	v4l2_ctrl_handler_init(hdl, 4);
	v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
			V4L2_CID_BRIGHTNESS, 0, 255, 1, 127);
	v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
			V4L2_CID_CONTRAST, 0, 255, 1, 127);
	v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
			V4L2_CID_SATURATION, 0, 255, 1, 127);
	v4l2_ctrl_new_std_menu(hdl, &sd_ctrl_ops,
			V4L2_CID_POWER_LINE_FREQUENCY,
			V4L2_CID_POWER_LINE_FREQUENCY_60HZ, 1,
			V4L2_CID_POWER_LINE_FREQUENCY_50HZ);

	if (hdl->error) {
		pr_err("Could not initialize controls\n");
		return hdl->error;
	}
	return 0;
}

/* sub-driver description */
static const struct sd_desc sd_desc = {
	.name = MODULE_NAME,
	.config = sd_config,
	.init = sd_init,
	.init_controls = sd_init_controls,
	.start = sd_start,
	.stopN = sd_stopN,
	.pkt_scan = sd_pkt_scan,
};

/* -- module initialisation -- */
static const struct usb_device_id device_table[] = {
	{USB_DEVICE(0x05e1, 0x0893)},
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
	.reset_resume = gspca_resume,
#endif
};

module_usb_driver(sd_driver);
