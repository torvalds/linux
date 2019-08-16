/*
 * Driver for USB webcams based on Konica chipset. This
 * chipset is used in Intel YC76 camera.
 *
 * Copyright (C) 2010 Hans de Goede <hdegoede@redhat.com>
 *
 * Based on the usbvideo v4l1 konicawc driver which is:
 *
 * Copyright (C) 2002 Simon Evans <spse@secret.org.uk>
 *
 * The code for making gspca work with a webcam with 2 isoc endpoints was
 * taken from the benq gspca subdriver which is:
 *
 * Copyright (C) 2009 Jean-Francois Moine (http://moinejf.free.fr)
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
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define MODULE_NAME "konica"

#include <linux/input.h>
#include "gspca.h"

MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_DESCRIPTION("Konica chipset USB Camera Driver");
MODULE_LICENSE("GPL");

#define WHITEBAL_REG   0x01
#define BRIGHTNESS_REG 0x02
#define SHARPNESS_REG  0x03
#define CONTRAST_REG   0x04
#define SATURATION_REG 0x05

/* specific webcam descriptor */
struct sd {
	struct gspca_dev gspca_dev;	/* !! must be the first item */
	struct urb *last_data_urb;
	u8 snapshot_pressed;
};


/* .priv is what goes to register 8 for this mode, known working values:
   0x00 -> 176x144, cropped
   0x01 -> 176x144, cropped
   0x02 -> 176x144, cropped
   0x03 -> 176x144, cropped
   0x04 -> 176x144, binned
   0x05 -> 320x240
   0x06 -> 320x240
   0x07 -> 160x120, cropped
   0x08 -> 160x120, cropped
   0x09 -> 160x120, binned (note has 136 lines)
   0x0a -> 160x120, binned (note has 136 lines)
   0x0b -> 160x120, cropped
*/
static const struct v4l2_pix_format vga_mode[] = {
	{160, 120, V4L2_PIX_FMT_KONICA420, V4L2_FIELD_NONE,
		.bytesperline = 160,
		.sizeimage = 160 * 136 * 3 / 2 + 960,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0x0a},
	{176, 144, V4L2_PIX_FMT_KONICA420, V4L2_FIELD_NONE,
		.bytesperline = 176,
		.sizeimage = 176 * 144 * 3 / 2 + 960,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0x04},
	{320, 240, V4L2_PIX_FMT_KONICA420, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240 * 3 / 2 + 960,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0x05},
};

static void sd_isoc_irq(struct urb *urb);

static void reg_w(struct gspca_dev *gspca_dev, u16 value, u16 index)
{
	struct usb_device *dev = gspca_dev->dev;
	int ret;

	if (gspca_dev->usb_err < 0)
		return;
	ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
			0x02,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			value,
			index,
			NULL,
			0,
			1000);
	if (ret < 0) {
		pr_err("reg_w err writing %02x to %02x: %d\n",
		       value, index, ret);
		gspca_dev->usb_err = ret;
	}
}

static void reg_r(struct gspca_dev *gspca_dev, u16 value, u16 index)
{
	struct usb_device *dev = gspca_dev->dev;
	int ret;

	if (gspca_dev->usb_err < 0)
		return;
	ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
			0x03,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			value,
			index,
			gspca_dev->usb_buf,
			2,
			1000);
	if (ret < 0) {
		pr_err("reg_r err %d\n", ret);
		gspca_dev->usb_err = ret;
		/*
		 * Make sure the buffer is zeroed to avoid uninitialized
		 * values.
		 */
		memset(gspca_dev->usb_buf, 0, 2);
	}
}

static void konica_stream_on(struct gspca_dev *gspca_dev)
{
	reg_w(gspca_dev, 1, 0x0b);
}

static void konica_stream_off(struct gspca_dev *gspca_dev)
{
	reg_w(gspca_dev, 0, 0x0b);
}

/* this function is called at probe time */
static int sd_config(struct gspca_dev *gspca_dev,
			const struct usb_device_id *id)
{
	gspca_dev->cam.cam_mode = vga_mode;
	gspca_dev->cam.nmodes = ARRAY_SIZE(vga_mode);
	gspca_dev->cam.no_urb_create = 1;

	return 0;
}

/* this function is called at probe and resume time */
static int sd_init(struct gspca_dev *gspca_dev)
{
	int i;

	/*
	 * The konica needs a freaking large time to "boot" (approx 6.5 sec.),
	 * and does not want to be bothered while doing so :|
	 * Register 0x10 counts from 1 - 3, with 3 being "ready"
	 */
	msleep(6000);
	for (i = 0; i < 20; i++) {
		reg_r(gspca_dev, 0, 0x10);
		if (gspca_dev->usb_buf[0] == 3)
			break;
		msleep(100);
	}
	reg_w(gspca_dev, 0, 0x0d);

	return gspca_dev->usb_err;
}

static int sd_start(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct urb *urb;
	int i, n, packet_size;
	struct usb_host_interface *alt;
	struct usb_interface *intf;

	intf = usb_ifnum_to_if(sd->gspca_dev.dev, sd->gspca_dev.iface);
	alt = usb_altnum_to_altsetting(intf, sd->gspca_dev.alt);
	if (!alt) {
		pr_err("Couldn't get altsetting\n");
		return -EIO;
	}

	if (alt->desc.bNumEndpoints < 2)
		return -ENODEV;

	packet_size = le16_to_cpu(alt->endpoint[0].desc.wMaxPacketSize);

	n = gspca_dev->cam.cam_mode[gspca_dev->curr_mode].priv;
	reg_w(gspca_dev, n, 0x08);

	konica_stream_on(gspca_dev);

	if (gspca_dev->usb_err)
		return gspca_dev->usb_err;

	/* create 4 URBs - 2 on endpoint 0x83 and 2 on 0x082 */
#if MAX_NURBS < 4
#error "Not enough URBs in the gspca table"
#endif
#define SD_NPKT 32
	for (n = 0; n < 4; n++) {
		i = n & 1 ? 0 : 1;
		packet_size =
			le16_to_cpu(alt->endpoint[i].desc.wMaxPacketSize);
		urb = usb_alloc_urb(SD_NPKT, GFP_KERNEL);
		if (!urb)
			return -ENOMEM;
		gspca_dev->urb[n] = urb;
		urb->transfer_buffer = usb_alloc_coherent(gspca_dev->dev,
						packet_size * SD_NPKT,
						GFP_KERNEL,
						&urb->transfer_dma);
		if (urb->transfer_buffer == NULL) {
			pr_err("usb_buffer_alloc failed\n");
			return -ENOMEM;
		}

		urb->dev = gspca_dev->dev;
		urb->context = gspca_dev;
		urb->transfer_buffer_length = packet_size * SD_NPKT;
		urb->pipe = usb_rcvisocpipe(gspca_dev->dev,
					n & 1 ? 0x81 : 0x82);
		urb->transfer_flags = URB_ISO_ASAP
					| URB_NO_TRANSFER_DMA_MAP;
		urb->interval = 1;
		urb->complete = sd_isoc_irq;
		urb->number_of_packets = SD_NPKT;
		for (i = 0; i < SD_NPKT; i++) {
			urb->iso_frame_desc[i].length = packet_size;
			urb->iso_frame_desc[i].offset = packet_size * i;
		}
	}

	return 0;
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	struct sd *sd __maybe_unused = (struct sd *) gspca_dev;

	konica_stream_off(gspca_dev);
#if IS_ENABLED(CONFIG_INPUT)
	/* Don't keep the button in the pressed state "forever" if it was
	   pressed when streaming is stopped */
	if (sd->snapshot_pressed) {
		input_report_key(gspca_dev->input_dev, KEY_CAMERA, 0);
		input_sync(gspca_dev->input_dev);
		sd->snapshot_pressed = 0;
	}
#endif
}

/* reception of an URB */
static void sd_isoc_irq(struct urb *urb)
{
	struct gspca_dev *gspca_dev = (struct gspca_dev *) urb->context;
	struct sd *sd = (struct sd *) gspca_dev;
	struct urb *data_urb, *status_urb;
	u8 *data;
	int i, st;

	gspca_dbg(gspca_dev, D_PACK, "sd isoc irq\n");
	if (!gspca_dev->streaming)
		return;

	if (urb->status != 0) {
		if (urb->status == -ESHUTDOWN)
			return;		/* disconnection */
#ifdef CONFIG_PM
		if (gspca_dev->frozen)
			return;
#endif
		gspca_err(gspca_dev, "urb status: %d\n", urb->status);
		st = usb_submit_urb(urb, GFP_ATOMIC);
		if (st < 0)
			pr_err("resubmit urb error %d\n", st);
		return;
	}

	/* if this is a data URB (ep 0x82), wait */
	if (urb->transfer_buffer_length > 32) {
		sd->last_data_urb = urb;
		return;
	}

	status_urb = urb;
	data_urb   = sd->last_data_urb;
	sd->last_data_urb = NULL;

	if (!data_urb || data_urb->start_frame != status_urb->start_frame) {
		gspca_err(gspca_dev, "lost sync on frames\n");
		goto resubmit;
	}

	if (data_urb->number_of_packets != status_urb->number_of_packets) {
		gspca_err(gspca_dev, "no packets does not match, data: %d, status: %d\n",
			  data_urb->number_of_packets,
			  status_urb->number_of_packets);
		goto resubmit;
	}

	for (i = 0; i < status_urb->number_of_packets; i++) {
		if (data_urb->iso_frame_desc[i].status ||
		    status_urb->iso_frame_desc[i].status) {
			gspca_err(gspca_dev, "pkt %d data-status %d, status-status %d\n",
				  i,
				  data_urb->iso_frame_desc[i].status,
				  status_urb->iso_frame_desc[i].status);
			gspca_dev->last_packet_type = DISCARD_PACKET;
			continue;
		}

		if (status_urb->iso_frame_desc[i].actual_length != 1) {
			gspca_err(gspca_dev, "bad status packet length %d\n",
				  status_urb->iso_frame_desc[i].actual_length);
			gspca_dev->last_packet_type = DISCARD_PACKET;
			continue;
		}

		st = *((u8 *)status_urb->transfer_buffer
				+ status_urb->iso_frame_desc[i].offset);

		data = (u8 *)data_urb->transfer_buffer
				+ data_urb->iso_frame_desc[i].offset;

		/* st: 0x80-0xff: frame start with frame number (ie 0-7f)
		 * otherwise:
		 * bit 0 0: keep packet
		 *	 1: drop packet (padding data)
		 *
		 * bit 4 0 button not clicked
		 *       1 button clicked
		 * button is used to `take a picture' (in software)
		 */
		if (st & 0x80) {
			gspca_frame_add(gspca_dev, LAST_PACKET, NULL, 0);
			gspca_frame_add(gspca_dev, FIRST_PACKET, NULL, 0);
		} else {
#if IS_ENABLED(CONFIG_INPUT)
			u8 button_state = st & 0x40 ? 1 : 0;
			if (sd->snapshot_pressed != button_state) {
				input_report_key(gspca_dev->input_dev,
						 KEY_CAMERA,
						 button_state);
				input_sync(gspca_dev->input_dev);
				sd->snapshot_pressed = button_state;
			}
#endif
			if (st & 0x01)
				continue;
		}
		gspca_frame_add(gspca_dev, INTER_PACKET, data,
				data_urb->iso_frame_desc[i].actual_length);
	}

resubmit:
	if (data_urb) {
		st = usb_submit_urb(data_urb, GFP_ATOMIC);
		if (st < 0)
			gspca_err(gspca_dev, "usb_submit_urb(data_urb) ret %d\n",
				  st);
	}
	st = usb_submit_urb(status_urb, GFP_ATOMIC);
	if (st < 0)
		gspca_err(gspca_dev, "usb_submit_urb(status_urb) ret %d\n", st);
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
		konica_stream_off(gspca_dev);
		reg_w(gspca_dev, ctrl->val, BRIGHTNESS_REG);
		konica_stream_on(gspca_dev);
		break;
	case V4L2_CID_CONTRAST:
		konica_stream_off(gspca_dev);
		reg_w(gspca_dev, ctrl->val, CONTRAST_REG);
		konica_stream_on(gspca_dev);
		break;
	case V4L2_CID_SATURATION:
		konica_stream_off(gspca_dev);
		reg_w(gspca_dev, ctrl->val, SATURATION_REG);
		konica_stream_on(gspca_dev);
		break;
	case V4L2_CID_WHITE_BALANCE_TEMPERATURE:
		konica_stream_off(gspca_dev);
		reg_w(gspca_dev, ctrl->val, WHITEBAL_REG);
		konica_stream_on(gspca_dev);
		break;
	case V4L2_CID_SHARPNESS:
		konica_stream_off(gspca_dev);
		reg_w(gspca_dev, ctrl->val, SHARPNESS_REG);
		konica_stream_on(gspca_dev);
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
	v4l2_ctrl_handler_init(hdl, 5);
	v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
			V4L2_CID_BRIGHTNESS, 0, 9, 1, 4);
	/* Needs to be verified */
	v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
			V4L2_CID_CONTRAST, 0, 9, 1, 4);
	v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
			V4L2_CID_SATURATION, 0, 9, 1, 4);
	v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
			V4L2_CID_WHITE_BALANCE_TEMPERATURE,
			0, 33, 1, 25);
	v4l2_ctrl_new_std(hdl, &sd_ctrl_ops,
			V4L2_CID_SHARPNESS, 0, 9, 1, 4);

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
#if IS_ENABLED(CONFIG_INPUT)
	.other_input = 1,
#endif
};

/* -- module initialisation -- */
static const struct usb_device_id device_table[] = {
	{USB_DEVICE(0x04c8, 0x0720)}, /* Intel YC 76 */
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
