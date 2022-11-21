// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Jeilin JL2005B/C/D library
 *
 * Copyright (C) 2011 Theodore Kilgore <kilgota@auburn.edu>
 */

#define MODULE_NAME "jl2005bcd"

#include <linux/workqueue.h>
#include <linux/slab.h>
#include "gspca.h"


MODULE_AUTHOR("Theodore Kilgore <kilgota@auburn.edu>");
MODULE_DESCRIPTION("JL2005B/C/D USB Camera Driver");
MODULE_LICENSE("GPL");

/* Default timeouts, in ms */
#define JL2005C_CMD_TIMEOUT 500
#define JL2005C_DATA_TIMEOUT 1000

/* Maximum transfer size to use. */
#define JL2005C_MAX_TRANSFER 0x200
#define FRAME_HEADER_LEN 16


/* specific webcam descriptor */
struct sd {
	struct gspca_dev gspca_dev;  /* !! must be the first item */
	unsigned char firmware_id[6];
	const struct v4l2_pix_format *cap_mode;
	/* Driver stuff */
	struct work_struct work_struct;
	u8 frame_brightness;
	int block_size;	/* block size of camera */
	int vga;	/* 1 if vga cam, 0 if cif cam */
};


/* Camera has two resolution settings. What they are depends on model. */
static const struct v4l2_pix_format cif_mode[] = {
	{176, 144, V4L2_PIX_FMT_JL2005BCD, V4L2_FIELD_NONE,
		.bytesperline = 176,
		.sizeimage = 176 * 144,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0},
	{352, 288, V4L2_PIX_FMT_JL2005BCD, V4L2_FIELD_NONE,
		.bytesperline = 352,
		.sizeimage = 352 * 288,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0},
};

static const struct v4l2_pix_format vga_mode[] = {
	{320, 240, V4L2_PIX_FMT_JL2005BCD, V4L2_FIELD_NONE,
		.bytesperline = 320,
		.sizeimage = 320 * 240,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0},
	{640, 480, V4L2_PIX_FMT_JL2005BCD, V4L2_FIELD_NONE,
		.bytesperline = 640,
		.sizeimage = 640 * 480,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0},
};

/*
 * cam uses endpoint 0x03 to send commands, 0x84 for read commands,
 * and 0x82 for bulk data transfer.
 */

/* All commands are two bytes only */
static int jl2005c_write2(struct gspca_dev *gspca_dev, unsigned char *command)
{
	int retval;

	memcpy(gspca_dev->usb_buf, command, 2);
	retval = usb_bulk_msg(gspca_dev->dev,
			usb_sndbulkpipe(gspca_dev->dev, 3),
			gspca_dev->usb_buf, 2, NULL, 500);
	if (retval < 0)
		pr_err("command write [%02x] error %d\n",
		       gspca_dev->usb_buf[0], retval);
	return retval;
}

/* Response to a command is one byte in usb_buf[0], only if requested. */
static int jl2005c_read1(struct gspca_dev *gspca_dev)
{
	int retval;

	retval = usb_bulk_msg(gspca_dev->dev,
				usb_rcvbulkpipe(gspca_dev->dev, 0x84),
				gspca_dev->usb_buf, 1, NULL, 500);
	if (retval < 0)
		pr_err("read command [0x%02x] error %d\n",
		       gspca_dev->usb_buf[0], retval);
	return retval;
}

/* Response appears in gspca_dev->usb_buf[0] */
static int jl2005c_read_reg(struct gspca_dev *gspca_dev, unsigned char reg)
{
	int retval;

	static u8 instruction[2] = {0x95, 0x00};
	/* put register to read in byte 1 */
	instruction[1] = reg;
	/* Send the read request */
	retval = jl2005c_write2(gspca_dev, instruction);
	if (retval < 0)
		return retval;
	retval = jl2005c_read1(gspca_dev);

	return retval;
}

static int jl2005c_start_new_frame(struct gspca_dev *gspca_dev)
{
	int i;
	int retval;
	int frame_brightness = 0;

	static u8 instruction[2] = {0x7f, 0x01};

	retval = jl2005c_write2(gspca_dev, instruction);
	if (retval < 0)
		return retval;

	i = 0;
	while (i < 20 && !frame_brightness) {
		/* If we tried 20 times, give up. */
		retval = jl2005c_read_reg(gspca_dev, 0x7e);
		if (retval < 0)
			return retval;
		frame_brightness = gspca_dev->usb_buf[0];
		retval = jl2005c_read_reg(gspca_dev, 0x7d);
		if (retval < 0)
			return retval;
		i++;
	}
	gspca_dbg(gspca_dev, D_FRAM, "frame_brightness is 0x%02x\n",
		  gspca_dev->usb_buf[0]);
	return retval;
}

static int jl2005c_write_reg(struct gspca_dev *gspca_dev, unsigned char reg,
						    unsigned char value)
{
	int retval;
	u8 instruction[2];

	instruction[0] = reg;
	instruction[1] = value;

	retval = jl2005c_write2(gspca_dev, instruction);
	if (retval < 0)
			return retval;

	return retval;
}

static int jl2005c_get_firmware_id(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *)gspca_dev;
	int i = 0;
	int retval;
	static const unsigned char regs_to_read[] = {
		0x57, 0x02, 0x03, 0x5d, 0x5e, 0x5f
	};

	gspca_dbg(gspca_dev, D_PROBE, "Running jl2005c_get_firmware_id\n");
	/* Read the first ID byte once for warmup */
	retval = jl2005c_read_reg(gspca_dev, regs_to_read[0]);
	gspca_dbg(gspca_dev, D_PROBE, "response is %02x\n",
		  gspca_dev->usb_buf[0]);
	if (retval < 0)
		return retval;
	/* Now actually get the ID string */
	for (i = 0; i < 6; i++) {
		retval = jl2005c_read_reg(gspca_dev, regs_to_read[i]);
		if (retval < 0)
			return retval;
		sd->firmware_id[i] = gspca_dev->usb_buf[0];
	}
	gspca_dbg(gspca_dev, D_PROBE, "firmware ID is %02x%02x%02x%02x%02x%02x\n",
		  sd->firmware_id[0],
		  sd->firmware_id[1],
		  sd->firmware_id[2],
		  sd->firmware_id[3],
		  sd->firmware_id[4],
		  sd->firmware_id[5]);
	return 0;
}

static int jl2005c_stream_start_vga_lg
		    (struct gspca_dev *gspca_dev)
{
	int i;
	int retval = -1;
	static u8 instruction[][2] = {
		{0x05, 0x00},
		{0x7c, 0x00},
		{0x7d, 0x18},
		{0x02, 0x00},
		{0x01, 0x00},
		{0x04, 0x52},
	};

	for (i = 0; i < ARRAY_SIZE(instruction); i++) {
		msleep(60);
		retval = jl2005c_write2(gspca_dev, instruction[i]);
		if (retval < 0)
			return retval;
	}
	msleep(60);
	return retval;
}

static int jl2005c_stream_start_vga_small(struct gspca_dev *gspca_dev)
{
	int i;
	int retval = -1;
	static u8 instruction[][2] = {
		{0x06, 0x00},
		{0x7c, 0x00},
		{0x7d, 0x1a},
		{0x02, 0x00},
		{0x01, 0x00},
		{0x04, 0x52},
	};

	for (i = 0; i < ARRAY_SIZE(instruction); i++) {
		msleep(60);
		retval = jl2005c_write2(gspca_dev, instruction[i]);
		if (retval < 0)
			return retval;
	}
	msleep(60);
	return retval;
}

static int jl2005c_stream_start_cif_lg(struct gspca_dev *gspca_dev)
{
	int i;
	int retval = -1;
	static u8 instruction[][2] = {
		{0x05, 0x00},
		{0x7c, 0x00},
		{0x7d, 0x30},
		{0x02, 0x00},
		{0x01, 0x00},
		{0x04, 0x42},
	};

	for (i = 0; i < ARRAY_SIZE(instruction); i++) {
		msleep(60);
		retval = jl2005c_write2(gspca_dev, instruction[i]);
		if (retval < 0)
			return retval;
	}
	msleep(60);
	return retval;
}

static int jl2005c_stream_start_cif_small(struct gspca_dev *gspca_dev)
{
	int i;
	int retval = -1;
	static u8 instruction[][2] = {
		{0x06, 0x00},
		{0x7c, 0x00},
		{0x7d, 0x32},
		{0x02, 0x00},
		{0x01, 0x00},
		{0x04, 0x42},
	};

	for (i = 0; i < ARRAY_SIZE(instruction); i++) {
		msleep(60);
		retval = jl2005c_write2(gspca_dev, instruction[i]);
		if (retval < 0)
			return retval;
	}
	msleep(60);
	return retval;
}


static int jl2005c_stop(struct gspca_dev *gspca_dev)
{
	return jl2005c_write_reg(gspca_dev, 0x07, 0x00);
}

/*
 * This function is called as a workqueue function and runs whenever the camera
 * is streaming data. Because it is a workqueue function it is allowed to sleep
 * so we can use synchronous USB calls. To avoid possible collisions with other
 * threads attempting to use gspca_dev->usb_buf we take the usb_lock when
 * performing USB operations using it. In practice we don't really need this
 * as the camera doesn't provide any controls.
 */
static void jl2005c_dostream(struct work_struct *work)
{
	struct sd *dev = container_of(work, struct sd, work_struct);
	struct gspca_dev *gspca_dev = &dev->gspca_dev;
	int bytes_left = 0; /* bytes remaining in current frame. */
	int data_len;   /* size to use for the next read. */
	int header_read = 0;
	unsigned char header_sig[2] = {0x4a, 0x4c};
	int act_len;
	int packet_type;
	int ret;
	u8 *buffer;

	buffer = kmalloc(JL2005C_MAX_TRANSFER, GFP_KERNEL);
	if (!buffer) {
		pr_err("Couldn't allocate USB buffer\n");
		goto quit_stream;
	}

	while (gspca_dev->present && gspca_dev->streaming) {
#ifdef CONFIG_PM
		if (gspca_dev->frozen)
			break;
#endif
		/* Check if this is a new frame. If so, start the frame first */
		if (!header_read) {
			mutex_lock(&gspca_dev->usb_lock);
			ret = jl2005c_start_new_frame(gspca_dev);
			mutex_unlock(&gspca_dev->usb_lock);
			if (ret < 0)
				goto quit_stream;
			ret = usb_bulk_msg(gspca_dev->dev,
				usb_rcvbulkpipe(gspca_dev->dev, 0x82),
				buffer, JL2005C_MAX_TRANSFER, &act_len,
				JL2005C_DATA_TIMEOUT);
			gspca_dbg(gspca_dev, D_PACK,
				  "Got %d bytes out of %d for header\n",
				  act_len, JL2005C_MAX_TRANSFER);
			if (ret < 0 || act_len < JL2005C_MAX_TRANSFER)
				goto quit_stream;
			/* Check whether we actually got the first blodk */
			if (memcmp(header_sig, buffer, 2) != 0) {
				pr_err("First block is not the first block\n");
				goto quit_stream;
			}
			/* total size to fetch is byte 7, times blocksize
			 * of which we already got act_len */
			bytes_left = buffer[0x07] * dev->block_size - act_len;
			gspca_dbg(gspca_dev, D_PACK, "bytes_left = 0x%x\n",
				  bytes_left);
			/* We keep the header. It has other information, too.*/
			packet_type = FIRST_PACKET;
			gspca_frame_add(gspca_dev, packet_type,
					buffer, act_len);
			header_read = 1;
		}
		while (bytes_left > 0 && gspca_dev->present) {
			data_len = bytes_left > JL2005C_MAX_TRANSFER ?
				JL2005C_MAX_TRANSFER : bytes_left;
			ret = usb_bulk_msg(gspca_dev->dev,
				usb_rcvbulkpipe(gspca_dev->dev, 0x82),
				buffer, data_len, &act_len,
				JL2005C_DATA_TIMEOUT);
			if (ret < 0 || act_len < data_len)
				goto quit_stream;
			gspca_dbg(gspca_dev, D_PACK,
				  "Got %d bytes out of %d for frame\n",
				  data_len, bytes_left);
			bytes_left -= data_len;
			if (bytes_left == 0) {
				packet_type = LAST_PACKET;
				header_read = 0;
			} else
				packet_type = INTER_PACKET;
			gspca_frame_add(gspca_dev, packet_type,
					buffer, data_len);
		}
	}
quit_stream:
	if (gspca_dev->present) {
		mutex_lock(&gspca_dev->usb_lock);
		jl2005c_stop(gspca_dev);
		mutex_unlock(&gspca_dev->usb_lock);
	}
	kfree(buffer);
}




/* This function is called at probe time */
static int sd_config(struct gspca_dev *gspca_dev,
			const struct usb_device_id *id)
{
	struct cam *cam;
	struct sd *sd = (struct sd *) gspca_dev;

	cam = &gspca_dev->cam;
	/* We don't use the buffer gspca allocates so make it small. */
	cam->bulk_size = 64;
	cam->bulk = 1;
	/* For the rest, the camera needs to be detected */
	jl2005c_get_firmware_id(gspca_dev);
	/* Here are some known firmware IDs
	 * First some JL2005B cameras
	 * {0x41, 0x07, 0x04, 0x2c, 0xe8, 0xf2}	Sakar KidzCam
	 * {0x45, 0x02, 0x08, 0xb9, 0x00, 0xd2}	No-name JL2005B
	 * JL2005C cameras
	 * {0x01, 0x0c, 0x16, 0x10, 0xf8, 0xc8}	Argus DC-1512
	 * {0x12, 0x04, 0x03, 0xc0, 0x00, 0xd8}	ICarly
	 * {0x86, 0x08, 0x05, 0x02, 0x00, 0xd4}	Jazz
	 *
	 * Based upon this scanty evidence, we can detect a CIF camera by
	 * testing byte 0 for 0x4x.
	 */
	if ((sd->firmware_id[0] & 0xf0) == 0x40) {
		cam->cam_mode	= cif_mode;
		cam->nmodes	= ARRAY_SIZE(cif_mode);
		sd->block_size	= 0x80;
	} else {
		cam->cam_mode	= vga_mode;
		cam->nmodes	= ARRAY_SIZE(vga_mode);
		sd->block_size	= 0x200;
	}

	INIT_WORK(&sd->work_struct, jl2005c_dostream);

	return 0;
}

/* this function is called at probe and resume time */
static int sd_init(struct gspca_dev *gspca_dev)
{
	return 0;
}

static int sd_start(struct gspca_dev *gspca_dev)
{

	struct sd *sd = (struct sd *) gspca_dev;
	sd->cap_mode = gspca_dev->cam.cam_mode;

	switch (gspca_dev->pixfmt.width) {
	case 640:
		gspca_dbg(gspca_dev, D_STREAM, "Start streaming at vga resolution\n");
		jl2005c_stream_start_vga_lg(gspca_dev);
		break;
	case 320:
		gspca_dbg(gspca_dev, D_STREAM, "Start streaming at qvga resolution\n");
		jl2005c_stream_start_vga_small(gspca_dev);
		break;
	case 352:
		gspca_dbg(gspca_dev, D_STREAM, "Start streaming at cif resolution\n");
		jl2005c_stream_start_cif_lg(gspca_dev);
		break;
	case 176:
		gspca_dbg(gspca_dev, D_STREAM, "Start streaming at qcif resolution\n");
		jl2005c_stream_start_cif_small(gspca_dev);
		break;
	default:
		pr_err("Unknown resolution specified\n");
		return -1;
	}

	schedule_work(&sd->work_struct);

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
	flush_work(&dev->work_struct);
	mutex_lock(&gspca_dev->usb_lock);
}



/* sub-driver description */
static const struct sd_desc sd_desc = {
	.name = MODULE_NAME,
	.config = sd_config,
	.init = sd_init,
	.start = sd_start,
	.stop0 = sd_stop0,
};

/* -- module initialisation -- */
static const struct usb_device_id device_table[] = {
	{USB_DEVICE(0x0979, 0x0227)},
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
