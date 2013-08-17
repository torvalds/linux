/*
 * Jeilinj subdriver
 *
 * Supports some Jeilin dual-mode cameras which use bulk transport and
 * download raw JPEG data.
 *
 * Copyright (C) 2009 Theodore Kilgore
 *
 * Sportscam DV15 support and control settings are
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define MODULE_NAME "jeilinj"

#include <linux/slab.h>
#include "gspca.h"
#include "jpeg.h"

MODULE_AUTHOR("Theodore Kilgore <kilgota@auburn.edu>");
MODULE_DESCRIPTION("GSPCA/JEILINJ USB Camera Driver");
MODULE_LICENSE("GPL");

/* Default timeouts, in ms */
#define JEILINJ_CMD_TIMEOUT 500
#define JEILINJ_CMD_DELAY 160
#define JEILINJ_DATA_TIMEOUT 1000

/* Maximum transfer size to use. */
#define JEILINJ_MAX_TRANSFER 0x200
#define FRAME_HEADER_LEN 0x10
#define FRAME_START 0xFFFFFFFF

enum {
	SAKAR_57379,
	SPORTSCAM_DV15,
};

#define CAMQUALITY_MIN 0	/* highest cam quality */
#define CAMQUALITY_MAX 97	/* lowest cam quality  */

enum e_ctrl {
	LIGHTFREQ,
	AUTOGAIN,
	RED,
	GREEN,
	BLUE,
	NCTRLS		/* number of controls */
};

/* Structure to hold all of our device specific stuff */
struct sd {
	struct gspca_dev gspca_dev;	/* !! must be the first item */
	struct gspca_ctrl ctrls[NCTRLS];
	int blocks_left;
	const struct v4l2_pix_format *cap_mode;
	/* Driver stuff */
	u8 type;
	u8 quality;				 /* image quality */
#define QUALITY_MIN 35
#define QUALITY_MAX 85
#define QUALITY_DEF 85
	u8 jpeg_hdr[JPEG_HDR_SZ];
};

struct jlj_command {
	unsigned char instruction[2];
	unsigned char ack_wanted;
	unsigned char delay;
};

/* AFAICT these cameras will only do 320x240. */
static struct v4l2_pix_format jlj_mode[] = {
	{ 320, 240, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.priv = 0},
	{ 640, 480, V4L2_PIX_FMT_JPEG, V4L2_FIELD_NONE,
		.bytesperline = 640,
		.sizeimage = 640 * 480,
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
		pr_err("command write [%02x] error %d\n",
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
		pr_err("read command [%02x] error %d\n",
		       gspca_dev->usb_buf[0], retval);
		gspca_dev->usb_err = retval;
	}
}

static void setfreq(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 freq_commands[][2] = {
		{0x71, 0x80},
		{0x70, 0x07}
	};

	freq_commands[0][1] |= (sd->ctrls[LIGHTFREQ].val >> 1);

	jlj_write2(gspca_dev, freq_commands[0]);
	jlj_write2(gspca_dev, freq_commands[1]);
}

static void setcamquality(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 quality_commands[][2] = {
		{0x71, 0x1E},
		{0x70, 0x06}
	};
	u8 camquality;

	/* adapt camera quality from jpeg quality */
	camquality = ((QUALITY_MAX - sd->quality) * CAMQUALITY_MAX)
		/ (QUALITY_MAX - QUALITY_MIN);
	quality_commands[0][1] += camquality;

	jlj_write2(gspca_dev, quality_commands[0]);
	jlj_write2(gspca_dev, quality_commands[1]);
}

static void setautogain(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 autogain_commands[][2] = {
		{0x94, 0x02},
		{0xcf, 0x00}
	};

	autogain_commands[1][1] = (sd->ctrls[AUTOGAIN].val << 4);

	jlj_write2(gspca_dev, autogain_commands[0]);
	jlj_write2(gspca_dev, autogain_commands[1]);
}

static void setred(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 setred_commands[][2] = {
		{0x94, 0x02},
		{0xe6, 0x00}
	};

	setred_commands[1][1] = sd->ctrls[RED].val;

	jlj_write2(gspca_dev, setred_commands[0]);
	jlj_write2(gspca_dev, setred_commands[1]);
}

static void setgreen(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 setgreen_commands[][2] = {
		{0x94, 0x02},
		{0xe7, 0x00}
	};

	setgreen_commands[1][1] = sd->ctrls[GREEN].val;

	jlj_write2(gspca_dev, setgreen_commands[0]);
	jlj_write2(gspca_dev, setgreen_commands[1]);
}

static void setblue(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 setblue_commands[][2] = {
		{0x94, 0x02},
		{0xe9, 0x00}
	};

	setblue_commands[1][1] = sd->ctrls[BLUE].val;

	jlj_write2(gspca_dev, setblue_commands[0]);
	jlj_write2(gspca_dev, setblue_commands[1]);
}

static const struct ctrl sd_ctrls[NCTRLS] = {
[LIGHTFREQ] = {
	    {
		.id      = V4L2_CID_POWER_LINE_FREQUENCY,
		.type    = V4L2_CTRL_TYPE_MENU,
		.name    = "Light frequency filter",
		.minimum = V4L2_CID_POWER_LINE_FREQUENCY_DISABLED, /* 1 */
		.maximum = V4L2_CID_POWER_LINE_FREQUENCY_60HZ, /* 2 */
		.step    = 1,
		.default_value = V4L2_CID_POWER_LINE_FREQUENCY_60HZ,
	    },
	    .set_control = setfreq
	},
[AUTOGAIN] = {
	    {
		.id = V4L2_CID_AUTOGAIN,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Automatic Gain (and Exposure)",
		.minimum = 0,
		.maximum = 3,
		.step = 1,
#define AUTOGAIN_DEF 0
		.default_value = AUTOGAIN_DEF,
	   },
	   .set_control = setautogain
	},
[RED] = {
	    {
		.id = V4L2_CID_RED_BALANCE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "red balance",
		.minimum = 0,
		.maximum = 3,
		.step = 1,
#define RED_BALANCE_DEF 2
		.default_value = RED_BALANCE_DEF,
	   },
	   .set_control = setred
	},

[GREEN]	= {
	    {
		.id = V4L2_CID_GAIN,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "green balance",
		.minimum = 0,
		.maximum = 3,
		.step = 1,
#define GREEN_BALANCE_DEF 2
		.default_value = GREEN_BALANCE_DEF,
	   },
	   .set_control = setgreen
	},
[BLUE] = {
	    {
		.id = V4L2_CID_BLUE_BALANCE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "blue balance",
		.minimum = 0,
		.maximum = 3,
		.step = 1,
#define BLUE_BALANCE_DEF 2
		.default_value = BLUE_BALANCE_DEF,
	   },
	   .set_control = setblue
	},
};

static int jlj_start(struct gspca_dev *gspca_dev)
{
	int i;
	int start_commands_size;
	u8 response = 0xff;
	struct sd *sd = (struct sd *) gspca_dev;
	struct jlj_command start_commands[] = {
		{{0x71, 0x81}, 0, 0},
		{{0x70, 0x05}, 0, JEILINJ_CMD_DELAY},
		{{0x95, 0x70}, 1, 0},
		{{0x71, 0x81 - gspca_dev->curr_mode}, 0, 0},
		{{0x70, 0x04}, 0, JEILINJ_CMD_DELAY},
		{{0x95, 0x70}, 1, 0},
		{{0x71, 0x00}, 0, 0},   /* start streaming ??*/
		{{0x70, 0x08}, 0, JEILINJ_CMD_DELAY},
		{{0x95, 0x70}, 1, 0},
#define SPORTSCAM_DV15_CMD_SIZE 9
		{{0x94, 0x02}, 0, 0},
		{{0xde, 0x24}, 0, 0},
		{{0x94, 0x02}, 0, 0},
		{{0xdd, 0xf0}, 0, 0},
		{{0x94, 0x02}, 0, 0},
		{{0xe3, 0x2c}, 0, 0},
		{{0x94, 0x02}, 0, 0},
		{{0xe4, 0x00}, 0, 0},
		{{0x94, 0x02}, 0, 0},
		{{0xe5, 0x00}, 0, 0},
		{{0x94, 0x02}, 0, 0},
		{{0xe6, 0x2c}, 0, 0},
		{{0x94, 0x03}, 0, 0},
		{{0xaa, 0x00}, 0, 0}
	};

	sd->blocks_left = 0;
	/* Under Windows, USB spy shows that only the 9 first start
	 * commands are used for SPORTSCAM_DV15 webcam
	 */
	if (sd->type == SPORTSCAM_DV15)
		start_commands_size = SPORTSCAM_DV15_CMD_SIZE;
	else
		start_commands_size = ARRAY_SIZE(start_commands);

	for (i = 0; i < start_commands_size; i++) {
		jlj_write2(gspca_dev, start_commands[i].instruction);
		if (start_commands[i].delay)
			msleep(start_commands[i].delay);
		if (start_commands[i].ack_wanted)
			jlj_read1(gspca_dev, response);
	}
	setcamquality(gspca_dev);
	msleep(2);
	setfreq(gspca_dev);
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

	dev->type = id->driver_info;
	gspca_dev->cam.ctrls = dev->ctrls;
	dev->quality = QUALITY_DEF;

	cam->cam_mode = jlj_mode;
	cam->nmodes = ARRAY_SIZE(jlj_mode);
	cam->bulk = 1;
	cam->bulk_nurbs = 1;
	cam->bulk_size = JEILINJ_MAX_TRANSFER;
	return 0;
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	int i;
	u8 *buf;
	static u8 stop_commands[][2] = {
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
	PDEBUG(D_STREAM, "Start streaming at %dx%d",
		gspca_dev->height, gspca_dev->width);
	jlj_start(gspca_dev);
	return gspca_dev->usb_err;
}

/* Table of supported USB devices */
static const struct usb_device_id device_table[] = {
	{USB_DEVICE(0x0979, 0x0280), .driver_info = SAKAR_57379},
	{USB_DEVICE(0x0979, 0x0270), .driver_info = SPORTSCAM_DV15},
	{}
};

MODULE_DEVICE_TABLE(usb, device_table);

static int sd_querymenu(struct gspca_dev *gspca_dev,
			struct v4l2_querymenu *menu)
{
	switch (menu->id) {
	case V4L2_CID_POWER_LINE_FREQUENCY:
		switch (menu->index) {
		case 0:	/* V4L2_CID_POWER_LINE_FREQUENCY_DISABLED */
			strcpy((char *) menu->name, "disable");
			return 0;
		case 1:	/* V4L2_CID_POWER_LINE_FREQUENCY_50HZ */
			strcpy((char *) menu->name, "50 Hz");
			return 0;
		case 2:	/* V4L2_CID_POWER_LINE_FREQUENCY_60HZ */
			strcpy((char *) menu->name, "60 Hz");
			return 0;
		}
		break;
	}
	return -EINVAL;
}

static int sd_set_jcomp(struct gspca_dev *gspca_dev,
			struct v4l2_jpegcompression *jcomp)
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (jcomp->quality < QUALITY_MIN)
		sd->quality = QUALITY_MIN;
	else if (jcomp->quality > QUALITY_MAX)
		sd->quality = QUALITY_MAX;
	else
		sd->quality = jcomp->quality;
	if (gspca_dev->streaming) {
		jpeg_set_qual(sd->jpeg_hdr, sd->quality);
		setcamquality(gspca_dev);
	}
	return 0;
}

static int sd_get_jcomp(struct gspca_dev *gspca_dev,
			struct v4l2_jpegcompression *jcomp)
{
	struct sd *sd = (struct sd *) gspca_dev;

	memset(jcomp, 0, sizeof *jcomp);
	jcomp->quality = sd->quality;
	jcomp->jpeg_markers = V4L2_JPEG_MARKER_DHT
			| V4L2_JPEG_MARKER_DQT;
	return 0;
}


/* sub-driver description */
static const struct sd_desc sd_desc_sakar_57379 = {
	.name   = MODULE_NAME,
	.config = sd_config,
	.init   = sd_init,
	.start  = sd_start,
	.stopN  = sd_stopN,
	.pkt_scan = sd_pkt_scan,
};

/* sub-driver description */
static const struct sd_desc sd_desc_sportscam_dv15 = {
	.name   = MODULE_NAME,
	.config = sd_config,
	.init   = sd_init,
	.start  = sd_start,
	.stopN  = sd_stopN,
	.pkt_scan = sd_pkt_scan,
	.ctrls = sd_ctrls,
	.nctrls = ARRAY_SIZE(sd_ctrls),
	.querymenu = sd_querymenu,
	.get_jcomp = sd_get_jcomp,
	.set_jcomp = sd_set_jcomp,
};

static const struct sd_desc *sd_desc[2] = {
	&sd_desc_sakar_57379,
	&sd_desc_sportscam_dv15
};

/* -- device connect -- */
static int sd_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	return gspca_dev_probe(intf, id,
			sd_desc[id->driver_info],
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

module_usb_driver(sd_driver);
