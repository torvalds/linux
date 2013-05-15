/*
 * SQ905 subdriver
 *
 * Copyright (C) 2008, 2009 Adam Baker and Theodore Kilgore
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

/*
 * History and Acknowledgments
 *
 * The original Linux driver for SQ905 based cameras was written by
 * Marcell Lengyel and furter developed by many other contributors
 * and is available from http://sourceforge.net/projects/sqcam/
 *
 * This driver takes advantage of the reverse engineering work done for
 * that driver and for libgphoto2 but shares no code with them.
 *
 * This driver has used as a base the finepix driver and other gspca
 * based drivers and may still contain code fragments taken from those
 * drivers.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define MODULE_NAME "sq905"

#include <linux/workqueue.h>
#include <linux/slab.h>
#include "gspca.h"

MODULE_AUTHOR("Adam Baker <linux@baker-net.org.uk>, "
		"Theodore Kilgore <kilgota@auburn.edu>");
MODULE_DESCRIPTION("GSPCA/SQ905 USB Camera Driver");
MODULE_LICENSE("GPL");

/* Default timeouts, in ms */
#define SQ905_CMD_TIMEOUT 500
#define SQ905_DATA_TIMEOUT 1000

/* Maximum transfer size to use. */
#define SQ905_MAX_TRANSFER 0x8000
#define FRAME_HEADER_LEN 64

/* The known modes, or registers. These go in the "value" slot. */

/* 00 is "none" obviously */

#define SQ905_BULK_READ	0x03	/* precedes any bulk read */
#define SQ905_COMMAND	0x06	/* precedes the command codes below */
#define SQ905_PING	0x07	/* when reading an "idling" command */
#define SQ905_READ_DONE 0xc0    /* ack bulk read completed */

/* Any non-zero value in the bottom 2 bits of the 2nd byte of
 * the ID appears to indicate the camera can do 640*480. If the
 * LSB of that byte is set the image is just upside down, otherwise
 * it is rotated 180 degrees. */
#define SQ905_HIRES_MASK	0x00000300
#define SQ905_ORIENTATION_MASK	0x00000100

/* Some command codes. These go in the "index" slot. */

#define SQ905_ID      0xf0	/* asks for model string */
#define SQ905_CONFIG  0x20	/* gets photo alloc. table, not used here */
#define SQ905_DATA    0x30	/* accesses photo data, not used here */
#define SQ905_CLEAR   0xa0	/* clear everything */
#define SQ905_CAPTURE_LOW  0x60	/* Starts capture at 160x120 */
#define SQ905_CAPTURE_MED  0x61	/* Starts capture at 320x240 */
#define SQ905_CAPTURE_HIGH 0x62	/* Starts capture at 640x480 (some cams only) */
/* note that the capture command also controls the output dimensions */

/* Structure to hold all of our device specific stuff */
struct sd {
	struct gspca_dev gspca_dev;	/* !! must be the first item */

	/*
	 * Driver stuff
	 */
	struct work_struct work_struct;
	struct workqueue_struct *work_thread;
};

static struct v4l2_pix_format sq905_mode[] = {
	{ 160, 120, V4L2_PIX_FMT_SBGGR8, V4L2_FIELD_NONE,
		.bytesperline = 160,
		.sizeimage = 160 * 120,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0},
	{ 320, 240, V4L2_PIX_FMT_SBGGR8, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0},
	{ 640, 480, V4L2_PIX_FMT_SBGGR8, V4L2_FIELD_NONE,
		.bytesperline = 640,
		.sizeimage = 640 * 480,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0}
};

/*
 * Send a command to the camera.
 */
static int sq905_command(struct gspca_dev *gspca_dev, u16 index)
{
	int ret;

	gspca_dev->usb_buf[0] = '\0';
	ret = usb_control_msg(gspca_dev->dev,
			      usb_sndctrlpipe(gspca_dev->dev, 0),
			      USB_REQ_SYNCH_FRAME,                /* request */
			      USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			      SQ905_COMMAND, index, gspca_dev->usb_buf, 1,
			      SQ905_CMD_TIMEOUT);
	if (ret < 0) {
		pr_err("%s: usb_control_msg failed (%d)\n", __func__, ret);
		return ret;
	}

	ret = usb_control_msg(gspca_dev->dev,
			      usb_sndctrlpipe(gspca_dev->dev, 0),
			      USB_REQ_SYNCH_FRAME,                /* request */
			      USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			      SQ905_PING, 0, gspca_dev->usb_buf, 1,
			      SQ905_CMD_TIMEOUT);
	if (ret < 0) {
		pr_err("%s: usb_control_msg failed 2 (%d)\n", __func__, ret);
		return ret;
	}

	return 0;
}

/*
 * Acknowledge the end of a frame - see warning on sq905_command.
 */
static int sq905_ack_frame(struct gspca_dev *gspca_dev)
{
	int ret;

	gspca_dev->usb_buf[0] = '\0';
	ret = usb_control_msg(gspca_dev->dev,
			      usb_sndctrlpipe(gspca_dev->dev, 0),
			      USB_REQ_SYNCH_FRAME,                /* request */
			      USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			      SQ905_READ_DONE, 0, gspca_dev->usb_buf, 1,
			      SQ905_CMD_TIMEOUT);
	if (ret < 0) {
		pr_err("%s: usb_control_msg failed (%d)\n", __func__, ret);
		return ret;
	}

	return 0;
}

/*
 *  request and read a block of data - see warning on sq905_command.
 */
static int
sq905_read_data(struct gspca_dev *gspca_dev, u8 *data, int size, int need_lock)
{
	int ret;
	int act_len;

	gspca_dev->usb_buf[0] = '\0';
	if (need_lock)
		mutex_lock(&gspca_dev->usb_lock);
	ret = usb_control_msg(gspca_dev->dev,
			      usb_sndctrlpipe(gspca_dev->dev, 0),
			      USB_REQ_SYNCH_FRAME,                /* request */
			      USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			      SQ905_BULK_READ, size, gspca_dev->usb_buf,
			      1, SQ905_CMD_TIMEOUT);
	if (need_lock)
		mutex_unlock(&gspca_dev->usb_lock);
	if (ret < 0) {
		pr_err("%s: usb_control_msg failed (%d)\n", __func__, ret);
		return ret;
	}
	ret = usb_bulk_msg(gspca_dev->dev,
			   usb_rcvbulkpipe(gspca_dev->dev, 0x81),
			   data, size, &act_len, SQ905_DATA_TIMEOUT);

	/* successful, it returns 0, otherwise  negative */
	if (ret < 0 || act_len != size) {
		pr_err("bulk read fail (%d) len %d/%d\n", ret, act_len, size);
		return -EIO;
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
static void sq905_dostream(struct work_struct *work)
{
	struct sd *dev = container_of(work, struct sd, work_struct);
	struct gspca_dev *gspca_dev = &dev->gspca_dev;
	int bytes_left; /* bytes remaining in current frame. */
	int data_len;   /* size to use for the next read. */
	int header_read; /* true if we have already read the frame header. */
	int packet_type;
	int frame_sz;
	int ret;
	u8 *data;
	u8 *buffer;

	buffer = kmalloc(SQ905_MAX_TRANSFER, GFP_KERNEL | GFP_DMA);
	if (!buffer) {
		pr_err("Couldn't allocate USB buffer\n");
		goto quit_stream;
	}

	frame_sz = gspca_dev->cam.cam_mode[gspca_dev->curr_mode].sizeimage
			+ FRAME_HEADER_LEN;

	while (gspca_dev->present && gspca_dev->streaming) {
#ifdef CONFIG_PM
		if (gspca_dev->frozen)
			break;
#endif
		/* request some data and then read it until we have
		 * a complete frame. */
		bytes_left = frame_sz;
		header_read = 0;

		/* Note we do not check for gspca_dev->streaming here, as
		   we must finish reading an entire frame, otherwise the
		   next time we stream we start reading in the middle of a
		   frame. */
		while (bytes_left > 0 && gspca_dev->present) {
			data_len = bytes_left > SQ905_MAX_TRANSFER ?
				SQ905_MAX_TRANSFER : bytes_left;
			ret = sq905_read_data(gspca_dev, buffer, data_len, 1);
			if (ret < 0)
				goto quit_stream;
			PDEBUG(D_PACK,
				"Got %d bytes out of %d for frame",
				data_len, bytes_left);
			bytes_left -= data_len;
			data = buffer;
			if (!header_read) {
				packet_type = FIRST_PACKET;
				/* The first 64 bytes of each frame are
				 * a header full of FF 00 bytes */
				data += FRAME_HEADER_LEN;
				data_len -= FRAME_HEADER_LEN;
				header_read = 1;
			} else if (bytes_left == 0) {
				packet_type = LAST_PACKET;
			} else {
				packet_type = INTER_PACKET;
			}
			gspca_frame_add(gspca_dev, packet_type,
					data, data_len);
			/* If entire frame fits in one packet we still
			   need to add a LAST_PACKET */
			if (packet_type == FIRST_PACKET &&
			    bytes_left == 0)
				gspca_frame_add(gspca_dev, LAST_PACKET,
						NULL, 0);
		}
		if (gspca_dev->present) {
			/* acknowledge the frame */
			mutex_lock(&gspca_dev->usb_lock);
			ret = sq905_ack_frame(gspca_dev);
			mutex_unlock(&gspca_dev->usb_lock);
			if (ret < 0)
				goto quit_stream;
		}
	}
quit_stream:
	if (gspca_dev->present) {
		mutex_lock(&gspca_dev->usb_lock);
		sq905_command(gspca_dev, SQ905_CLEAR);
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

	/* We don't use the buffer gspca allocates so make it small. */
	cam->bulk = 1;
	cam->bulk_size = 64;

	INIT_WORK(&dev->work_struct, sq905_dostream);

	return 0;
}

/* called on streamoff with alt==0 and on disconnect */
/* the usb_lock is held at entry - restore on exit */
static void sd_stop0(struct gspca_dev *gspca_dev)
{
	struct sd *dev = (struct sd *) gspca_dev;

	/* wait for the work queue to terminate */
	mutex_unlock(&gspca_dev->usb_lock);
	/* This waits for sq905_dostream to finish */
	destroy_workqueue(dev->work_thread);
	dev->work_thread = NULL;
	mutex_lock(&gspca_dev->usb_lock);
}

/* this function is called at probe and resume time */
static int sd_init(struct gspca_dev *gspca_dev)
{
	u32 ident;
	int ret;

	/* connect to the camera and read
	 * the model ID and process that and put it away.
	 */
	ret = sq905_command(gspca_dev, SQ905_CLEAR);
	if (ret < 0)
		return ret;
	ret = sq905_command(gspca_dev, SQ905_ID);
	if (ret < 0)
		return ret;
	ret = sq905_read_data(gspca_dev, gspca_dev->usb_buf, 4, 0);
	if (ret < 0)
		return ret;
	/* usb_buf is allocated with kmalloc so is aligned.
	 * Camera model number is the right way round if we assume this
	 * reverse engineered ID is supposed to be big endian. */
	ident = be32_to_cpup((__be32 *)gspca_dev->usb_buf);
	ret = sq905_command(gspca_dev, SQ905_CLEAR);
	if (ret < 0)
		return ret;
	PDEBUG(D_CONF, "SQ905 camera ID %08x detected", ident);
	gspca_dev->cam.cam_mode = sq905_mode;
	gspca_dev->cam.nmodes = ARRAY_SIZE(sq905_mode);
	if (!(ident & SQ905_HIRES_MASK))
		gspca_dev->cam.nmodes--;

	if (ident & SQ905_ORIENTATION_MASK)
		gspca_dev->cam.input_flags = V4L2_IN_ST_VFLIP;
	else
		gspca_dev->cam.input_flags = V4L2_IN_ST_VFLIP |
					     V4L2_IN_ST_HFLIP;
	return 0;
}

/* Set up for getting frames. */
static int sd_start(struct gspca_dev *gspca_dev)
{
	struct sd *dev = (struct sd *) gspca_dev;
	int ret;

	/* "Open the shutter" and set size, to start capture */
	switch (gspca_dev->curr_mode) {
	default:
/*	case 2: */
		PDEBUG(D_STREAM, "Start streaming at high resolution");
		ret = sq905_command(&dev->gspca_dev, SQ905_CAPTURE_HIGH);
		break;
	case 1:
		PDEBUG(D_STREAM, "Start streaming at medium resolution");
		ret = sq905_command(&dev->gspca_dev, SQ905_CAPTURE_MED);
		break;
	case 0:
		PDEBUG(D_STREAM, "Start streaming at low resolution");
		ret = sq905_command(&dev->gspca_dev, SQ905_CAPTURE_LOW);
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
	{USB_DEVICE(0x2770, 0x9120)},
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
