/*
 * SQ905C subdriver
 *
 * Copyright (C) 2009 Theodore Kilgore
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

/*
 *
 * This driver uses work done in
 * libgphoto2/camlibs/digigr8, Copyright (C) Theodore Kilgore.
 *
 * This driver has also used as a base the sq905c driver
 * and may contain code fragments from it.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define MODULE_NAME "sq905c"

#include <linux/workqueue.h>
#include <linux/slab.h>
#include "gspca.h"

MODULE_AUTHOR("Theodore Kilgore <kilgota@auburn.edu>");
MODULE_DESCRIPTION("GSPCA/SQ905C USB Camera Driver");
MODULE_LICENSE("GPL");

/* Default timeouts, in ms */
#define SQ905C_CMD_TIMEOUT 500
#define SQ905C_DATA_TIMEOUT 1000

/* Maximum transfer size to use. */
#define SQ905C_MAX_TRANSFER 0x8000

#define FRAME_HEADER_LEN 0x50

/* Commands. These go in the "value" slot. */
#define SQ905C_CLEAR   0xa0		/* clear everything */
#define SQ905C_GET_ID  0x14f4		/* Read version number */
#define SQ905C_CAPTURE_LOW 0xa040	/* Starts capture at 160x120 */
#define SQ905C_CAPTURE_MED 0x1440	/* Starts capture at 320x240 */
#define SQ905C_CAPTURE_HI 0x2840	/* Starts capture at 320x240 */

/* For capture, this must go in the "index" slot. */
#define SQ905C_CAPTURE_INDEX 0x110f

/* Structure to hold all of our device specific stuff */
struct sd {
	struct gspca_dev gspca_dev;	/* !! must be the first item */
	const struct v4l2_pix_format *cap_mode;
	/* Driver stuff */
	struct work_struct work_struct;
	struct workqueue_struct *work_thread;
};

/*
 * Most of these cameras will do 640x480 and 320x240. 160x120 works
 * in theory but gives very poor output. Therefore, not supported.
 * The 0x2770:0x9050 cameras have max resolution of 320x240.
 */
static struct v4l2_pix_format sq905c_mode[] = {
	{ 320, 240, V4L2_PIX_FMT_SQ905C, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0},
	{ 640, 480, V4L2_PIX_FMT_SQ905C, V4L2_FIELD_NONE,
		.bytesperline = 640,
		.sizeimage = 640 * 480,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0}
};

/* Send a command to the camera. */
static int sq905c_command(struct gspca_dev *gspca_dev, u16 command, u16 index)
{
	int ret;

	ret = usb_control_msg(gspca_dev->dev,
			      usb_sndctrlpipe(gspca_dev->dev, 0),
			      USB_REQ_SYNCH_FRAME,                /* request */
			      USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			      command, index, NULL, 0,
			      SQ905C_CMD_TIMEOUT);
	if (ret < 0) {
		pr_err("%s: usb_control_msg failed (%d)\n", __func__, ret);
		return ret;
	}

	return 0;
}

static int sq905c_read(struct gspca_dev *gspca_dev, u16 command, u16 index,
		       int size)
{
	int ret;

	ret = usb_control_msg(gspca_dev->dev,
			      usb_rcvctrlpipe(gspca_dev->dev, 0),
			      USB_REQ_SYNCH_FRAME,		/* request */
			      USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			      command, index, gspca_dev->usb_buf, size,
			      SQ905C_CMD_TIMEOUT);
	if (ret < 0) {
		pr_err("%s: usb_control_msg failed (%d)\n", __func__, ret);
		return ret;
	}

	return 0;
}

/*
 * This function is called as a workqueue function and runs whenever the camera
 * is streaming data. Because it is a workqueue function it is allowed to sleep
 * so we can use synchronous USB calls. To avoid possible collisions with other
 * threads attempting to use gspca_dev->usb_buf we take the usb_lock when
 * performing USB operations using it. In practice we don't really need this
 * as the camera doesn't provide any controls.
 */
static void sq905c_dostream(struct work_struct *work)
{
	struct sd *dev = container_of(work, struct sd, work_struct);
	struct gspca_dev *gspca_dev = &dev->gspca_dev;
	int bytes_left; /* bytes remaining in current frame. */
	int data_len;   /* size to use for the next read. */
	int act_len;
	int packet_type;
	int ret;
	u8 *buffer;

	buffer = kmalloc(SQ905C_MAX_TRANSFER, GFP_KERNEL | GFP_DMA);
	if (!buffer) {
		pr_err("Couldn't allocate USB buffer\n");
		goto quit_stream;
	}

	while (gspca_dev->present && gspca_dev->streaming) {
#ifdef CONFIG_PM
		if (gspca_dev->frozen)
			break;
#endif
		/* Request the header, which tells the size to download */
		ret = usb_bulk_msg(gspca_dev->dev,
				usb_rcvbulkpipe(gspca_dev->dev, 0x81),
				buffer, FRAME_HEADER_LEN, &act_len,
				SQ905C_DATA_TIMEOUT);
		PDEBUG(D_STREAM,
			"Got %d bytes out of %d for header",
			act_len, FRAME_HEADER_LEN);
		if (ret < 0 || act_len < FRAME_HEADER_LEN)
			goto quit_stream;
		/* size is read from 4 bytes starting 0x40, little endian */
		bytes_left = buffer[0x40]|(buffer[0x41]<<8)|(buffer[0x42]<<16)
					|(buffer[0x43]<<24);
		PDEBUG(D_STREAM, "bytes_left = 0x%x", bytes_left);
		/* We keep the header. It has other information, too. */
		packet_type = FIRST_PACKET;
		gspca_frame_add(gspca_dev, packet_type,
				buffer, FRAME_HEADER_LEN);
		while (bytes_left > 0 && gspca_dev->present) {
			data_len = bytes_left > SQ905C_MAX_TRANSFER ?
				SQ905C_MAX_TRANSFER : bytes_left;
			ret = usb_bulk_msg(gspca_dev->dev,
				usb_rcvbulkpipe(gspca_dev->dev, 0x81),
				buffer, data_len, &act_len,
				SQ905C_DATA_TIMEOUT);
			if (ret < 0 || act_len < data_len)
				goto quit_stream;
			PDEBUG(D_STREAM,
				"Got %d bytes out of %d for frame",
				data_len, bytes_left);
			bytes_left -= data_len;
			if (bytes_left == 0)
				packet_type = LAST_PACKET;
			else
				packet_type = INTER_PACKET;
			gspca_frame_add(gspca_dev, packet_type,
					buffer, data_len);
		}
	}
quit_stream:
	if (gspca_dev->present) {
		mutex_lock(&gspca_dev->usb_lock);
		sq905c_command(gspca_dev, SQ905C_CLEAR, 0);
		mutex_unlock(&gspca_dev->usb_lock);
	}
	kfree(buffer);
}

/* This function is called at probe time just before sd_init */
static int sd_config(struct gspca_dev *gspca_dev,
		const struct usb_device_id *id)
{
	struct cam *cam = &gspca_dev->cam;
	struct sd *dev = (struct sd *) gspca_dev;
	int ret;

	PDEBUG(D_PROBE,
	       "SQ9050 camera detected (vid/pid 0x%04X:0x%04X)",
	       id->idVendor, id->idProduct);

	ret = sq905c_command(gspca_dev, SQ905C_GET_ID, 0);
	if (ret < 0) {
		PERR("Get version command failed");
		return ret;
	}

	ret = sq905c_read(gspca_dev, 0xf5, 0, 20);
	if (ret < 0) {
		PERR("Reading version command failed");
		return ret;
	}
	/* Note we leave out the usb id and the manufacturing date */
	PDEBUG(D_PROBE,
	       "SQ9050 ID string: %02x - %*ph",
		gspca_dev->usb_buf[3], 6, gspca_dev->usb_buf + 14);

	cam->cam_mode = sq905c_mode;
	cam->nmodes = 2;
	if (gspca_dev->usb_buf[15] == 0)
		cam->nmodes = 1;
	/* We don't use the buffer gspca allocates so make it small. */
	cam->bulk_size = 32;
	cam->bulk = 1;
	INIT_WORK(&dev->work_struct, sq905c_dostream);
	return 0;
}

/* called on streamoff with alt==0 and on disconnect */
/* the usb_lock is held at entry - restore on exit */
static void sd_stop0(struct gspca_dev *gspca_dev)
{
	struct sd *dev = (struct sd *) gspca_dev;

	/* wait for the work queue to terminate */
	mutex_unlock(&gspca_dev->usb_lock);
	/* This waits for sq905c_dostream to finish */
	destroy_workqueue(dev->work_thread);
	dev->work_thread = NULL;
	mutex_lock(&gspca_dev->usb_lock);
}

/* this function is called at probe and resume time */
static int sd_init(struct gspca_dev *gspca_dev)
{
	/* connect to the camera and reset it. */
	return sq905c_command(gspca_dev, SQ905C_CLEAR, 0);
}

/* Set up for getting frames. */
static int sd_start(struct gspca_dev *gspca_dev)
{
	struct sd *dev = (struct sd *) gspca_dev;
	int ret;

	dev->cap_mode = gspca_dev->cam.cam_mode;
	/* "Open the shutter" and set size, to start capture */
	switch (gspca_dev->pixfmt.width) {
	case 640:
		PDEBUG(D_STREAM, "Start streaming at high resolution");
		dev->cap_mode++;
		ret = sq905c_command(gspca_dev, SQ905C_CAPTURE_HI,
						SQ905C_CAPTURE_INDEX);
		break;
	default: /* 320 */
	PDEBUG(D_STREAM, "Start streaming at medium resolution");
		ret = sq905c_command(gspca_dev, SQ905C_CAPTURE_MED,
						SQ905C_CAPTURE_INDEX);
	}

	if (ret < 0) {
		PERR("Start streaming command failed");
		return ret;
	}
	/* Start the workqueue function to do the streaming */
	dev->work_thread = create_singlethread_workqueue(MODULE_NAME);
	queue_work(dev->work_thread, &dev->work_struct);

	return 0;
}

/* Table of supported USB devices */
static const struct usb_device_id device_table[] = {
	{USB_DEVICE(0x2770, 0x905c)},
	{USB_DEVICE(0x2770, 0x9050)},
	{USB_DEVICE(0x2770, 0x9051)},
	{USB_DEVICE(0x2770, 0x9052)},
	{USB_DEVICE(0x2770, 0x913d)},
	{}
};

MODULE_DEVICE_TABLE(usb, device_table);

/* sub-driver description */
static const struct sd_desc sd_desc = {
	.name   = MODULE_NAME,
	.config = sd_config,
	.init   = sd_init,
	.start  = sd_start,
	.stop0  = sd_stop0,
};

/* -- device connect -- */
static int sd_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	return gspca_dev_probe(intf, id,
			&sd_desc,
			sizeof(struct sd),
			THIS_MODULE);
}

static struct usb_driver sd_driver = {
	.name       = MODULE_NAME,
	.id_table   = device_table,
	.probe      = sd_probe,
	.disconnect = gspca_disconnect,
#ifdef CONFIG_PM
	.suspend = gspca_suspend,
	.resume  = gspca_resume,
	.reset_resume = gspca_resume,
#endif
};

module_usb_driver(sd_driver);
