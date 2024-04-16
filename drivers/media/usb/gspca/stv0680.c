// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * STV0680 USB Camera Driver
 *
 * Copyright (C) 2009 Hans de Goede <hdegoede@redhat.com>
 *
 * This module is adapted from the in kernel v4l1 stv680 driver:
 *
 *  STV0680 USB Camera Driver, by Kevin Sisson (kjsisson@bellsouth.net)
 *
 * Thanks to STMicroelectronics for information on the usb commands, and
 * to Steve Miller at STM for his help and encouragement while I was
 * writing this driver.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define MODULE_NAME "stv0680"

#include "gspca.h"

MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_DESCRIPTION("STV0680 USB Camera Driver");
MODULE_LICENSE("GPL");

/* specific webcam descriptor */
struct sd {
	struct gspca_dev gspca_dev;		/* !! must be the first item */
	struct v4l2_pix_format mode;
	u8 orig_mode;
	u8 video_mode;
	u8 current_mode;
};

static int stv_sndctrl(struct gspca_dev *gspca_dev, int set, u8 req, u16 val,
		       int size)
{
	int ret;
	u8 req_type = 0;
	unsigned int pipe = 0;

	switch (set) {
	case 0: /*  0xc1  */
		req_type = USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_ENDPOINT;
		pipe = usb_rcvctrlpipe(gspca_dev->dev, 0);
		break;
	case 1: /*  0x41  */
		req_type = USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_ENDPOINT;
		pipe = usb_sndctrlpipe(gspca_dev->dev, 0);
		break;
	case 2:	/*  0x80  */
		req_type = USB_DIR_IN | USB_RECIP_DEVICE;
		pipe = usb_rcvctrlpipe(gspca_dev->dev, 0);
		break;
	case 3:	/*  0x40  */
		req_type = USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE;
		pipe = usb_sndctrlpipe(gspca_dev->dev, 0);
		break;
	}

	ret = usb_control_msg(gspca_dev->dev, pipe,
			      req, req_type,
			      val, 0, gspca_dev->usb_buf, size, 500);

	if ((ret < 0) && (req != 0x0a))
		pr_err("usb_control_msg error %i, request = 0x%x, error = %i\n",
		       set, req, ret);

	return ret;
}

static int stv0680_handle_error(struct gspca_dev *gspca_dev, int ret)
{
	stv_sndctrl(gspca_dev, 0, 0x80, 0, 0x02); /* Get Last Error */
	gspca_err(gspca_dev, "last error: %i,  command = 0x%x\n",
		  gspca_dev->usb_buf[0], gspca_dev->usb_buf[1]);
	return ret;
}

static int stv0680_get_video_mode(struct gspca_dev *gspca_dev)
{
	/* Note not sure if this init of usb_buf is really necessary */
	memset(gspca_dev->usb_buf, 0, 8);
	gspca_dev->usb_buf[0] = 0x0f;

	if (stv_sndctrl(gspca_dev, 0, 0x87, 0, 0x08) != 0x08) {
		gspca_err(gspca_dev, "Get_Camera_Mode failed\n");
		return stv0680_handle_error(gspca_dev, -EIO);
	}

	return gspca_dev->usb_buf[0]; /* 01 = VGA, 03 = QVGA, 00 = CIF */
}

static int stv0680_set_video_mode(struct gspca_dev *gspca_dev, u8 mode)
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (sd->current_mode == mode)
		return 0;

	memset(gspca_dev->usb_buf, 0, 8);
	gspca_dev->usb_buf[0] = mode;

	if (stv_sndctrl(gspca_dev, 3, 0x07, 0x0100, 0x08) != 0x08) {
		gspca_err(gspca_dev, "Set_Camera_Mode failed\n");
		return stv0680_handle_error(gspca_dev, -EIO);
	}

	/* Verify we got what we've asked for */
	if (stv0680_get_video_mode(gspca_dev) != mode) {
		gspca_err(gspca_dev, "Error setting camera video mode!\n");
		return -EIO;
	}

	sd->current_mode = mode;

	return 0;
}

/* this function is called at probe time */
static int sd_config(struct gspca_dev *gspca_dev,
			const struct usb_device_id *id)
{
	int ret;
	struct sd *sd = (struct sd *) gspca_dev;
	struct cam *cam = &gspca_dev->cam;

	/* Give the camera some time to settle, otherwise initialization will
	   fail on hotplug, and yes it really needs a full second. */
	msleep(1000);

	/* ping camera to be sure STV0680 is present */
	if (stv_sndctrl(gspca_dev, 0, 0x88, 0x5678, 0x02) != 0x02 ||
	    gspca_dev->usb_buf[0] != 0x56 || gspca_dev->usb_buf[1] != 0x78) {
		gspca_err(gspca_dev, "STV(e): camera ping failed!!\n");
		return stv0680_handle_error(gspca_dev, -ENODEV);
	}

	/* get camera descriptor */
	if (stv_sndctrl(gspca_dev, 2, 0x06, 0x0200, 0x09) != 0x09)
		return stv0680_handle_error(gspca_dev, -ENODEV);

	if (stv_sndctrl(gspca_dev, 2, 0x06, 0x0200, 0x22) != 0x22 ||
	    gspca_dev->usb_buf[7] != 0xa0 || gspca_dev->usb_buf[8] != 0x23) {
		gspca_err(gspca_dev, "Could not get descriptor 0200\n");
		return stv0680_handle_error(gspca_dev, -ENODEV);
	}
	if (stv_sndctrl(gspca_dev, 0, 0x8a, 0, 0x02) != 0x02)
		return stv0680_handle_error(gspca_dev, -ENODEV);
	if (stv_sndctrl(gspca_dev, 0, 0x8b, 0, 0x24) != 0x24)
		return stv0680_handle_error(gspca_dev, -ENODEV);
	if (stv_sndctrl(gspca_dev, 0, 0x85, 0, 0x10) != 0x10)
		return stv0680_handle_error(gspca_dev, -ENODEV);

	if (!(gspca_dev->usb_buf[7] & 0x09)) {
		gspca_err(gspca_dev, "Camera supports neither CIF nor QVGA mode\n");
		return -ENODEV;
	}
	if (gspca_dev->usb_buf[7] & 0x01)
		gspca_dbg(gspca_dev, D_PROBE, "Camera supports CIF mode\n");
	if (gspca_dev->usb_buf[7] & 0x02)
		gspca_dbg(gspca_dev, D_PROBE, "Camera supports VGA mode\n");
	if (gspca_dev->usb_buf[7] & 0x04)
		gspca_dbg(gspca_dev, D_PROBE, "Camera supports QCIF mode\n");
	if (gspca_dev->usb_buf[7] & 0x08)
		gspca_dbg(gspca_dev, D_PROBE, "Camera supports QVGA mode\n");

	if (gspca_dev->usb_buf[7] & 0x01)
		sd->video_mode = 0x00; /* CIF */
	else
		sd->video_mode = 0x03; /* QVGA */

	/* FW rev, ASIC rev, sensor ID  */
	gspca_dbg(gspca_dev, D_PROBE, "Firmware rev is %i.%i\n",
		  gspca_dev->usb_buf[0], gspca_dev->usb_buf[1]);
	gspca_dbg(gspca_dev, D_PROBE, "ASIC rev is %i.%i",
		  gspca_dev->usb_buf[2], gspca_dev->usb_buf[3]);
	gspca_dbg(gspca_dev, D_PROBE, "Sensor ID is %i",
		  (gspca_dev->usb_buf[4]*16) + (gspca_dev->usb_buf[5]>>4));


	ret = stv0680_get_video_mode(gspca_dev);
	if (ret < 0)
		return ret;
	sd->current_mode = sd->orig_mode = ret;

	ret = stv0680_set_video_mode(gspca_dev, sd->video_mode);
	if (ret < 0)
		return ret;

	/* Get mode details */
	if (stv_sndctrl(gspca_dev, 0, 0x8f, 0, 0x10) != 0x10)
		return stv0680_handle_error(gspca_dev, -EIO);

	cam->bulk = 1;
	cam->bulk_nurbs = 1; /* The cam cannot handle more */
	cam->bulk_size = (gspca_dev->usb_buf[0] << 24) |
			 (gspca_dev->usb_buf[1] << 16) |
			 (gspca_dev->usb_buf[2] << 8) |
			 (gspca_dev->usb_buf[3]);
	sd->mode.width = (gspca_dev->usb_buf[4] << 8) |
			 (gspca_dev->usb_buf[5]);  /* 322, 356, 644 */
	sd->mode.height = (gspca_dev->usb_buf[6] << 8) |
			  (gspca_dev->usb_buf[7]); /* 242, 292, 484 */
	sd->mode.pixelformat = V4L2_PIX_FMT_STV0680;
	sd->mode.field = V4L2_FIELD_NONE;
	sd->mode.bytesperline = sd->mode.width;
	sd->mode.sizeimage = cam->bulk_size;
	sd->mode.colorspace = V4L2_COLORSPACE_SRGB;

	/* origGain = gspca_dev->usb_buf[12]; */

	cam->cam_mode = &sd->mode;
	cam->nmodes = 1;


	ret = stv0680_set_video_mode(gspca_dev, sd->orig_mode);
	if (ret < 0)
		return ret;

	if (stv_sndctrl(gspca_dev, 2, 0x06, 0x0100, 0x12) != 0x12 ||
	    gspca_dev->usb_buf[8] != 0x53 || gspca_dev->usb_buf[9] != 0x05) {
		pr_err("Could not get descriptor 0100\n");
		return stv0680_handle_error(gspca_dev, -EIO);
	}

	return 0;
}

/* this function is called at probe and resume time */
static int sd_init(struct gspca_dev *gspca_dev)
{
	return 0;
}

/* -- start the camera -- */
static int sd_start(struct gspca_dev *gspca_dev)
{
	int ret;
	struct sd *sd = (struct sd *) gspca_dev;

	ret = stv0680_set_video_mode(gspca_dev, sd->video_mode);
	if (ret < 0)
		return ret;

	if (stv_sndctrl(gspca_dev, 0, 0x85, 0, 0x10) != 0x10)
		return stv0680_handle_error(gspca_dev, -EIO);

	/* Start stream at:
	   0x0000 = CIF (352x288)
	   0x0100 = VGA (640x480)
	   0x0300 = QVGA (320x240) */
	if (stv_sndctrl(gspca_dev, 1, 0x09, sd->video_mode << 8, 0x0) != 0x0)
		return stv0680_handle_error(gspca_dev, -EIO);

	return 0;
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	/* This is a high priority command; it stops all lower order cmds */
	if (stv_sndctrl(gspca_dev, 1, 0x04, 0x0000, 0x0) != 0x0)
		stv0680_handle_error(gspca_dev, -EIO);
}

static void sd_stop0(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (!sd->gspca_dev.present)
		return;

	stv0680_set_video_mode(gspca_dev, sd->orig_mode);
}

static void sd_pkt_scan(struct gspca_dev *gspca_dev,
			u8 *data,
			int len)
{
	struct sd *sd = (struct sd *) gspca_dev;

	/* Every now and then the camera sends a 16 byte packet, no idea
	   what it contains, but it is not image data, when this
	   happens the frame received before this packet is corrupt,
	   so discard it. */
	if (len != sd->mode.sizeimage) {
		gspca_dev->last_packet_type = DISCARD_PACKET;
		return;
	}

	/* Finish the previous frame, we do this upon reception of the next
	   packet, even though it is already complete so that the strange 16
	   byte packets send after a corrupt frame can discard it. */
	gspca_frame_add(gspca_dev, LAST_PACKET, NULL, 0);

	/* Store the just received frame */
	gspca_frame_add(gspca_dev, FIRST_PACKET, data, len);
}

/* sub-driver description */
static const struct sd_desc sd_desc = {
	.name = MODULE_NAME,
	.config = sd_config,
	.init = sd_init,
	.start = sd_start,
	.stopN = sd_stopN,
	.stop0 = sd_stop0,
	.pkt_scan = sd_pkt_scan,
};

/* -- module initialisation -- */
static const struct usb_device_id device_table[] = {
	{USB_DEVICE(0x0553, 0x0202)},
	{USB_DEVICE(0x041e, 0x4007)},
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
