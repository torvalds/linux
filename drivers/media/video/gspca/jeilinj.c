/*
 * Jeilinj subdriver
 *
 * Supports some Jeilin dual-mode cameras which use bulk transport and
 * download raw JPEG data.
 *
 * Copyright (C) 2009 Theodore Kilgore
 * Copyright (C) 2011 Patrice Chotard
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

#define MODULE_NAME "jeilinj"

#include <linux/slab.h>
#include "gspca.h"
#include "jpeg.h"

MODULE_AUTHOR("Theodore Kilgore <kilgota@auburn.edu>");
MODULE_DESCRIPTION("GSPCA/JEILINJ USB Camera Driver");
MODULE_LICENSE("GPL");

/* Default timeouts, in ms */
#define JEILINJ_CMD_TIMEOUT 500
#define JEILINJ_DATA_TIMEOUT 1000

/* Maximum transfer size to use. */
#define JEILINJ_MAX_TRANSFER 0x200
#define FRAME_HEADER_LEN 0x10
#define FRAME_START 0xFFFFFFFF

/* Structure to hold all of our device specific stuff */
struct sd {
	struct gspca_dev gspca_dev;	/* !! must be the first item */
	int blocks_left;
	const struct v4l2_pix_format *cap_mode;
	/* Driver stuff */
	u8 quality;				 /* image quality */
	u8 jpeg_hdr[JPEG_HDR_SZ];
};

struct jlj_command {
	unsigned char instruction[2];
	unsigned char ack_wanted;
};

/* AFAICT these cameras will only do 320x240. */
static struct v4l2_pix_format jlj_mode[] = {
	{ 320, 240, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 0}
};

/*
 * cam uses endpoint 0x03 to send commands, 0x84 for read commands,
 * and 0x82 for bulk transfer.
 */

/* All commands are two bytes only */
static void jlj_write2(struct gspca_dev *gspca_dev, unsigned char *command)
{
	int retval;

	if (gspca_dev->usb_err < 0)
		return;
	memcpy(gspca_dev->usb_buf, command, 2);
	retval = usb_bulk_msg(gspca_dev->dev,
			usb_sndbulkpipe(gspca_dev->dev, 3),
			gspca_dev->usb_buf, 2, NULL, 500);
	if (retval < 0) {
		err("command write [%02x] error %d",
				gspca_dev->usb_buf[0], retval);
		gspca_dev->usb_err = retval;
	}
}

/* Responses are one byte only */
static void jlj_read1(struct gspca_dev *gspca_dev, unsigned char response)
{
	int retval;

	if (gspca_dev->usb_err < 0)
		return;
	retval = usb_bulk_msg(gspca_dev->dev,
	usb_rcvbulkpipe(gspca_dev->dev, 0x84),
				gspca_dev->usb_buf, 1, NULL, 500);
	response = gspca_dev->usb_buf[0];
	if (retval < 0) {
		err("read command [%02x] error %d",
				gspca_dev->usb_buf[0], retval);
		gspca_dev->usb_err = retval;
	}
}

static int jlj_start(struct gspca_dev *gspca_dev)
{
	int i;
	u8 response = 0xff;
	struct sd *sd = (struct sd *) gspca_dev;
	struct jlj_command start_commands[] = {
		{{0x71, 0x81}, 0},
		{{0x70, 0x05}, 0},
		{{0x95, 0x70}, 1},
		{{0x71, 0x81}, 0},
		{{0x70, 0x04}, 0},
		{{0x95, 0x70}, 1},
		{{0x71, 0x00}, 0},
		{{0x70, 0x08}, 0},
		{{0x95, 0x70}, 1},
		{{0x94, 0x02}, 0},
		{{0xde, 0x24}, 0},
		{{0x94, 0x02}, 0},
		{{0xdd, 0xf0}, 0},
		{{0x94, 0x02}, 0},
		{{0xe3, 0x2c}, 0},
		{{0x94, 0x02}, 0},
		{{0xe4, 0x00}, 0},
		{{0x94, 0x02}, 0},
		{{0xe5, 0x00}, 0},
		{{0x94, 0x02}, 0},
		{{0xe6, 0x2c}, 0},
		{{0x94, 0x03}, 0},
		{{0xaa, 0x00}, 0},
		{{0x71, 0x1e}, 0},
		{{0x70, 0x06}, 0},
		{{0x71, 0x80}, 0},
		{{0x70, 0x07}, 0}
	};

	sd->blocks_left = 0;
	for (i = 0; i < ARRAY_SIZE(start_commands); i++) {
		jlj_write2(gspca_dev, start_commands[i].instruction);
		if (start_commands[i].ack_wanted)
			jlj_read1(gspca_dev, response);
	}
	if (gspca_dev->usb_err < 0)
		PDEBUG(D_ERR, "Start streaming command failed");
	return gspca_dev->usb_err;
}

static void sd_pkt_scan(struct gspca_dev *gspca_dev,
			u8 *data, int len)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int packet_type;
	u32 header_marker;

	PDEBUG(D_STREAM, "Got %d bytes out of %d for Block 0",
			len, JEILINJ_MAX_TRANSFER);
	if (len != JEILINJ_MAX_TRANSFER) {
		PDEBUG(D_PACK, "bad length");
		goto discard;
	}
	/* check if it's start of frame */
	header_marker = ((u32 *)data)[0];
	if (header_marker == FRAME_START) {
		sd->blocks_left = data[0x0a] - 1;
		PDEBUG(D_STREAM, "blocks_left = 0x%x", sd->blocks_left);
		/* Start a new frame, and add the JPEG header, first thing */
		gspca_frame_add(gspca_dev, FIRST_PACKET,
				sd->jpeg_hdr, JPEG_HDR_SZ);
		/* Toss line 0 of data block 0, keep the rest. */
		gspca_frame_add(gspca_dev, INTER_PACKET,
				data + FRAME_HEADER_LEN,
				JEILINJ_MAX_TRANSFER - FRAME_HEADER_LEN);
	} else if (sd->blocks_left > 0) {
		PDEBUG(D_STREAM, "%d blocks remaining for frame",
				sd->blocks_left);
		sd->blocks_left -= 1;
		if (sd->blocks_left == 0)
			packet_type = LAST_PACKET;
		else
			packet_type = INTER_PACKET;
		gspca_frame_add(gspca_dev, packet_type,
				data, JEILINJ_MAX_TRANSFER);
	} else
		goto discard;
	return;
discard:
	/* Discard data until a new frame starts. */
	gspca_dev->last_packet_type = DISCARD_PACKET;
}

/* This function is called at probe time just before sd_init */
static int sd_config(struct gspca_dev *gspca_dev,
		const struct usb_device_id *id)
{
	struct cam *cam = &gspca_dev->cam;
	struct sd *dev  = (struct sd *) gspca_dev;

	dev->quality  = 85;
	PDEBUG(D_PROBE,
		"JEILINJ camera detected"
		" (vid/pid 0x%04X:0x%04X)", id->idVendor, id->idProduct);
	cam->cam_mode = jlj_mode;
	cam->nmodes = 1;
	cam->bulk = 1;
	cam->bulk_nurbs = 1;
	cam->bulk_size = JEILINJ_MAX_TRANSFER;
	return 0;
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	int i;
	u8 *buf;
	u8 stop_commands[][2] = {
		{0x71, 0x00},
		{0x70, 0x09},
		{0x71, 0x80},
		{0x70, 0x05}
	};

	for (;;) {
		/* get the image remaining blocks */
		usb_bulk_msg(gspca_dev->dev,
				gspca_dev->urb[0]->pipe,
				gspca_dev->urb[0]->transfer_buffer,
				JEILINJ_MAX_TRANSFER, NULL,
				JEILINJ_DATA_TIMEOUT);

		/* search for 0xff 0xd9  (EOF for JPEG) */
		i = 0;
		buf = gspca_dev->urb[0]->transfer_buffer;
		while ((i < (JEILINJ_MAX_TRANSFER - 1)) &&
			((buf[i] != 0xff) || (buf[i+1] != 0xd9)))
			i++;

		if (i != (JEILINJ_MAX_TRANSFER - 1))
			/* last remaining block found */
			break;
		}

	for (i = 0; i < ARRAY_SIZE(stop_commands); i++)
		jlj_write2(gspca_dev, stop_commands[i]);
}

/* this function is called at probe and resume time */
static int sd_init(struct gspca_dev *gspca_dev)
{
	return gspca_dev->usb_err;
}

/* Set up for getting frames. */
static int sd_start(struct gspca_dev *gspca_dev)
{
	struct sd *dev = (struct sd *) gspca_dev;

	/* create the JPEG header */
	jpeg_define(dev->jpeg_hdr, gspca_dev->height, gspca_dev->width,
			0x21);          /* JPEG 422 */
	jpeg_set_qual(dev->jpeg_hdr, dev->quality);
	PDEBUG(D_STREAM, "Start streaming at 320x240");
	jlj_start(gspca_dev);
	return gspca_dev->usb_err;
}

/* Table of supported USB devices */
static const struct usb_device_id device_table[] = {
	{USB_DEVICE(0x0979, 0x0280)},
	{}
};

MODULE_DEVICE_TABLE(usb, device_table);

/* sub-driver description */
static const struct sd_desc sd_desc = {
	.name   = MODULE_NAME,
	.config = sd_config,
	.init   = sd_init,
	.start  = sd_start,
	.stopN  = sd_stopN,
	.pkt_scan = sd_pkt_scan,
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
#endif
};

/* -- module insert / remove -- */
static int __init sd_mod_init(void)
{
	return usb_register(&sd_driver);
}

static void __exit sd_mod_exit(void)
{
	usb_deregister(&sd_driver);
}

module_init(sd_mod_init);
module_exit(sd_mod_exit);
