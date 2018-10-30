/*
 * Pixart PAC207BCA library
 *
 * Copyright (C) 2008 Hans de Goede <hdegoede@redhat.com>
 * Copyright (C) 2005 Thomas Kaiser thomas@kaiser-linux.li
 * Copyleft (C) 2005 Michel Xhaard mxhaard@magic.fr
 *
 * V4L2 by Jean-Francois Moine <http://moinejf.free.fr>
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
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define MODULE_NAME "pac207"

#include <linux/input.h>
#include "gspca.h"
/* Include pac common sof detection functions */
#include "pac_common.h"

MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_DESCRIPTION("Pixart PAC207");
MODULE_LICENSE("GPL");

#define PAC207_CTRL_TIMEOUT		100  /* ms */

#define PAC207_BRIGHTNESS_MIN		0
#define PAC207_BRIGHTNESS_MAX		255
#define PAC207_BRIGHTNESS_DEFAULT	46
#define PAC207_BRIGHTNESS_REG		0x08

#define PAC207_EXPOSURE_MIN		3
#define PAC207_EXPOSURE_MAX		90 /* 1 sec expo time / 1 fps */
#define PAC207_EXPOSURE_DEFAULT		5 /* power on default: 3 */
#define PAC207_EXPOSURE_REG		0x02

#define PAC207_GAIN_MIN			0
#define PAC207_GAIN_MAX			31
#define PAC207_GAIN_DEFAULT		7 /* power on default: 9 */
#define PAC207_GAIN_REG			0x0e

#define PAC207_AUTOGAIN_DEADZONE	30

/* global parameters */
static int led_invert;
module_param(led_invert, int, 0644);
MODULE_PARM_DESC(led_invert, "Invert led");

/* specific webcam descriptor */
struct sd {
	struct gspca_dev gspca_dev;		/* !! must be the first item */

	struct v4l2_ctrl *brightness;

	u8 mode;
	u8 sof_read;
	u8 header_read;
	u8 autogain_ignore_frames;

	atomic_t avg_lum;
};

static const struct v4l2_pix_format sif_mode[] = {
	{176, 144, V4L2_PIX_FMT_PAC207, V4L2_FIELD_NONE,
		.bytesperline = 176,
		.sizeimage = (176 + 2) * 144,
			/* uncompressed, add 2 bytes / line for line header */
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 1},
	{352, 288, V4L2_PIX_FMT_PAC207, V4L2_FIELD_NONE,
		.bytesperline = 352,
			/* compressed, but only when needed (not compressed
			   when the framerate is low) */
		.sizeimage = (352 + 2) * 288,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0},
};

static const __u8 pac207_sensor_init[][8] = {
	{0x10, 0x12, 0x0d, 0x12, 0x0c, 0x01, 0x29, 0x84},
	{0x49, 0x64, 0x64, 0x64, 0x04, 0x10, 0xf0, 0x30},
	{0x00, 0x00, 0x00, 0x70, 0xa0, 0xf8, 0x00, 0x00},
	{0x32, 0x00, 0x96, 0x00, 0xa2, 0x02, 0xaf, 0x00},
};

static void pac207_write_regs(struct gspca_dev *gspca_dev, u16 index,
	const u8 *buffer, u16 length)
{
	struct usb_device *udev = gspca_dev->dev;
	int err;

	if (gspca_dev->usb_err < 0)
		return;

	memcpy(gspca_dev->usb_buf, buffer, length);

	err = usb_control_msg(udev, usb_sndctrlpipe(udev, 0), 0x01,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_INTERFACE,
			0x00, index,
			gspca_dev->usb_buf, length, PAC207_CTRL_TIMEOUT);
	if (err < 0) {
		pr_err("Failed to write registers to index 0x%04X, error %d\n",
		       index, err);
		gspca_dev->usb_err = err;
	}
}

static void pac207_write_reg(struct gspca_dev *gspca_dev, u16 index, u16 value)
{
	struct usb_device *udev = gspca_dev->dev;
	int err;

	if (gspca_dev->usb_err < 0)
		return;

	err = usb_control_msg(udev, usb_sndctrlpipe(udev, 0), 0x00,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_INTERFACE,
			value, index, NULL, 0, PAC207_CTRL_TIMEOUT);
	if (err) {
		pr_err("Failed to write a register (index 0x%04X, value 0x%02X, error %d)\n",
		       index, value, err);
		gspca_dev->usb_err = err;
	}
}

static int pac207_read_reg(struct gspca_dev *gspca_dev, u16 index)
{
	struct usb_device *udev = gspca_dev->dev;
	int res;

	if (gspca_dev->usb_err < 0)
		return 0;

	res = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0), 0x00,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_INTERFACE,
			0x00, index,
			gspca_dev->usb_buf, 1, PAC207_CTRL_TIMEOUT);
	if (res < 0) {
		pr_err("Failed to read a register (index 0x%04X, error %d)\n",
		       index, res);
		gspca_dev->usb_err = res;
		return 0;
	}

	return gspca_dev->usb_buf[0];
}

/* this function is called at probe time */
static int sd_config(struct gspca_dev *gspca_dev,
			const struct usb_device_id *id)
{
	struct cam *cam;
	u8 idreg[2];

	idreg[0] = pac207_read_reg(gspca_dev, 0x0000);
	idreg[1] = pac207_read_reg(gspca_dev, 0x0001);
	idreg[0] = ((idreg[0] & 0x0f) << 4) | ((idreg[1] & 0xf0) >> 4);
	idreg[1] = idreg[1] & 0x0f;
	gspca_dbg(gspca_dev, D_PROBE, "Pixart Sensor ID 0x%02X Chips ID 0x%02X\n",
		  idreg[0], idreg[1]);

	if (idreg[0] != 0x27) {
		gspca_dbg(gspca_dev, D_PROBE, "Error invalid sensor ID!\n");
		return -ENODEV;
	}

	gspca_dbg(gspca_dev, D_PROBE,
		  "Pixart PAC207BCA Image Processor and Control Chip detected (vid/pid 0x%04X:0x%04X)\n",
		  id->idVendor, id->idProduct);

	cam = &gspca_dev->cam;
	cam->cam_mode = sif_mode;
	cam->nmodes = ARRAY_SIZE(sif_mode);

	return 0;
}

/* this function is called at probe and resume time */
static int sd_init(struct gspca_dev *gspca_dev)
{
	u8 mode;

	/* mode: Image Format (Bit 0), LED (1), Compr. test mode (2) */
	if (led_invert)
		mode = 0x02;
	else
		mode = 0x00;
	pac207_write_reg(gspca_dev, 0x41, mode);
	pac207_write_reg(gspca_dev, 0x0f, 0x00); /* Power Control */

	return gspca_dev->usb_err;
}

static void setcontrol(struct gspca_dev *gspca_dev, u16 reg, u16 val)
{
	pac207_write_reg(gspca_dev, reg, val);
	pac207_write_reg(gspca_dev, 0x13, 0x01);	/* Bit 0, auto clear */
	pac207_write_reg(gspca_dev, 0x1c, 0x01);	/* not documented */
}

static int sd_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gspca_dev *gspca_dev =
		container_of(ctrl->handler, struct gspca_dev, ctrl_handler);
	struct sd *sd = (struct sd *)gspca_dev;

	gspca_dev->usb_err = 0;

	if (ctrl->id == V4L2_CID_AUTOGAIN && ctrl->is_new && ctrl->val) {
		/* when switching to autogain set defaults to make sure
		   we are on a valid point of the autogain gain /
		   exposure knee graph, and give this change time to
		   take effect before doing autogain. */
		gspca_dev->exposure->val    = PAC207_EXPOSURE_DEFAULT;
		gspca_dev->gain->val        = PAC207_GAIN_DEFAULT;
		sd->autogain_ignore_frames  = PAC_AUTOGAIN_IGNORE_FRAMES;
	}

	if (!gspca_dev->streaming)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		setcontrol(gspca_dev, PAC207_BRIGHTNESS_REG, ctrl->val);
		break;
	case V4L2_CID_AUTOGAIN:
		if (gspca_dev->exposure->is_new || (ctrl->is_new && ctrl->val))
			setcontrol(gspca_dev, PAC207_EXPOSURE_REG,
				   gspca_dev->exposure->val);
		if (gspca_dev->gain->is_new || (ctrl->is_new && ctrl->val))
			setcontrol(gspca_dev, PAC207_GAIN_REG,
				   gspca_dev->gain->val);
		break;
	default:
		return -EINVAL;
	}
	return gspca_dev->usb_err;
}

static const struct v4l2_ctrl_ops sd_ctrl_ops = {
	.s_ctrl = sd_s_ctrl,
};

/* this function is called at probe time */
static int sd_init_controls(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct v4l2_ctrl_handler *hdl = &gspca_dev->ctrl_handler;

	gspca_dev->vdev.ctrl_handler = hdl;
	v4l2_ctrl_handler_init(hdl, 4);

	sd->brightness = v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
				V4L2_CID_BRIGHTNESS,
				PAC207_BRIGHTNESS_MIN, PAC207_BRIGHTNESS_MAX,
				1, PAC207_BRIGHTNESS_DEFAULT);
	gspca_dev->autogain = v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
				V4L2_CID_AUTOGAIN, 0, 1, 1, 1);
	gspca_dev->exposure = v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
				V4L2_CID_EXPOSURE,
				PAC207_EXPOSURE_MIN, PAC207_EXPOSURE_MAX,
				1, PAC207_EXPOSURE_DEFAULT);
	gspca_dev->gain = v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
				V4L2_CID_GAIN,
				PAC207_GAIN_MIN, PAC207_GAIN_MAX,
				1, PAC207_GAIN_DEFAULT);
	if (hdl->error) {
		pr_err("Could not initialize controls\n");
		return hdl->error;
	}
	v4l2_ctrl_auto_cluster(3, &gspca_dev->autogain, 0, false);
	return 0;
}

/* -- start the camera -- */
static int sd_start(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	__u8 mode;

	pac207_write_reg(gspca_dev, 0x0f, 0x10); /* Power control (Bit 6-0) */
	pac207_write_regs(gspca_dev, 0x0002, pac207_sensor_init[0], 8);
	pac207_write_regs(gspca_dev, 0x000a, pac207_sensor_init[1], 8);
	pac207_write_regs(gspca_dev, 0x0012, pac207_sensor_init[2], 8);
	pac207_write_regs(gspca_dev, 0x0042, pac207_sensor_init[3], 8);

	/* Compression Balance */
	if (gspca_dev->pixfmt.width == 176)
		pac207_write_reg(gspca_dev, 0x4a, 0xff);
	else
		pac207_write_reg(gspca_dev, 0x4a, 0x30);
	pac207_write_reg(gspca_dev, 0x4b, 0x00); /* Sram test value */
	pac207_write_reg(gspca_dev, 0x08, v4l2_ctrl_g_ctrl(sd->brightness));

	/* PGA global gain (Bit 4-0) */
	pac207_write_reg(gspca_dev, 0x0e,
		v4l2_ctrl_g_ctrl(gspca_dev->gain));
	pac207_write_reg(gspca_dev, 0x02,
		v4l2_ctrl_g_ctrl(gspca_dev->exposure)); /* PXCK = 12MHz /n */

	/* mode: Image Format (Bit 0), LED (1), Compr. test mode (2) */
	if (led_invert)
		mode = 0x00;
	else
		mode = 0x02;
	if (gspca_dev->pixfmt.width == 176) {	/* 176x144 */
		mode |= 0x01;
		gspca_dbg(gspca_dev, D_STREAM, "pac207_start mode 176x144\n");
	} else {				/* 352x288 */
		gspca_dbg(gspca_dev, D_STREAM, "pac207_start mode 352x288\n");
	}
	pac207_write_reg(gspca_dev, 0x41, mode);

	pac207_write_reg(gspca_dev, 0x13, 0x01); /* Bit 0, auto clear */
	pac207_write_reg(gspca_dev, 0x1c, 0x01); /* not documented */
	msleep(10);
	pac207_write_reg(gspca_dev, 0x40, 0x01); /* Start ISO pipe */

	sd->sof_read = 0;
	sd->autogain_ignore_frames = 0;
	atomic_set(&sd->avg_lum, -1);
	return gspca_dev->usb_err;
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	u8 mode;

	/* mode: Image Format (Bit 0), LED (1), Compr. test mode (2) */
	if (led_invert)
		mode = 0x02;
	else
		mode = 0x00;
	pac207_write_reg(gspca_dev, 0x40, 0x00); /* Stop ISO pipe */
	pac207_write_reg(gspca_dev, 0x41, mode); /* Turn off LED */
	pac207_write_reg(gspca_dev, 0x0f, 0x00); /* Power Control */
}


static void pac207_do_auto_gain(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int avg_lum = atomic_read(&sd->avg_lum);

	if (avg_lum == -1)
		return;

	if (sd->autogain_ignore_frames > 0)
		sd->autogain_ignore_frames--;
	else if (gspca_coarse_grained_expo_autogain(gspca_dev, avg_lum,
			90, PAC207_AUTOGAIN_DEADZONE))
		sd->autogain_ignore_frames = PAC_AUTOGAIN_IGNORE_FRAMES;
}

static void sd_pkt_scan(struct gspca_dev *gspca_dev,
			u8 *data,
			int len)
{
	struct sd *sd = (struct sd *) gspca_dev;
	unsigned char *sof;

	sof = pac_find_sof(gspca_dev, &sd->sof_read, data, len);
	if (sof) {
		int n;

		/* finish decoding current frame */
		n = sof - data;
		if (n > sizeof pac_sof_marker)
			n -= sizeof pac_sof_marker;
		else
			n = 0;
		gspca_frame_add(gspca_dev, LAST_PACKET,
				data, n);
		sd->header_read = 0;
		gspca_frame_add(gspca_dev, FIRST_PACKET, NULL, 0);
		len -= sof - data;
		data = sof;
	}
	if (sd->header_read < 11) {
		int needed;

		/* get average lumination from frame header (byte 5) */
		if (sd->header_read < 5) {
			needed = 5 - sd->header_read;
			if (len >= needed)
				atomic_set(&sd->avg_lum, data[needed - 1]);
		}
		/* skip the rest of the header */
		needed = 11 - sd->header_read;
		if (len <= needed) {
			sd->header_read += len;
			return;
		}
		data += needed;
		len -= needed;
		sd->header_read = 11;
	}

	gspca_frame_add(gspca_dev, INTER_PACKET, data, len);
}

#if IS_ENABLED(CONFIG_INPUT)
static int sd_int_pkt_scan(struct gspca_dev *gspca_dev,
			u8 *data,		/* interrupt packet data */
			int len)		/* interrupt packet length */
{
	int ret = -EINVAL;

	if (len == 2 && data[0] == 0x5a && data[1] == 0x5a) {
		input_report_key(gspca_dev->input_dev, KEY_CAMERA, 1);
		input_sync(gspca_dev->input_dev);
		input_report_key(gspca_dev->input_dev, KEY_CAMERA, 0);
		input_sync(gspca_dev->input_dev);
		ret = 0;
	}

	return ret;
}
#endif

/* sub-driver description */
static const struct sd_desc sd_desc = {
	.name = MODULE_NAME,
	.config = sd_config,
	.init = sd_init,
	.init_controls = sd_init_controls,
	.start = sd_start,
	.stopN = sd_stopN,
	.dq_callback = pac207_do_auto_gain,
	.pkt_scan = sd_pkt_scan,
#if IS_ENABLED(CONFIG_INPUT)
	.int_pkt_scan = sd_int_pkt_scan,
#endif
};

/* -- module initialisation -- */
static const struct usb_device_id device_table[] = {
	{USB_DEVICE(0x041e, 0x4028)},
	{USB_DEVICE(0x093a, 0x2460)},
	{USB_DEVICE(0x093a, 0x2461)},
	{USB_DEVICE(0x093a, 0x2463)},
	{USB_DEVICE(0x093a, 0x2464)},
	{USB_DEVICE(0x093a, 0x2468)},
	{USB_DEVICE(0x093a, 0x2470)},
	{USB_DEVICE(0x093a, 0x2471)},
	{USB_DEVICE(0x093a, 0x2472)},
	{USB_DEVICE(0x093a, 0x2474)},
	{USB_DEVICE(0x093a, 0x2476)},
	{USB_DEVICE(0x145f, 0x013a)},
	{USB_DEVICE(0x2001, 0xf115)},
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
