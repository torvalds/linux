// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Surface2.0/SUR40/PixelSense input driver
 *
 * Copyright (c) 2014 by Florian 'floe' Echtler <floe@butterbrot.org>
 *
 * Derived from the USB Skeleton driver 1.1,
 * Copyright (c) 2003 Greg Kroah-Hartman (greg@kroah.com)
 *
 * and from the Apple USB BCM5974 multitouch driver,
 * Copyright (c) 2008 Henrik Rydberg (rydberg@euromail.se)
 *
 * and from the generic hid-multitouch driver,
 * Copyright (c) 2010-2012 Stephane Chatty <chatty@enac.fr>
 *
 * and from the v4l2-pci-skeleton driver,
 * Copyright (c) Copyright 2014 Cisco Systems, Inc.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/completion.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/printk.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/usb/input.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-sg.h>

/* read 512 bytes from endpoint 0x86 -> get header + blobs */
struct sur40_header {

	__le16 type;       /* always 0x0001 */
	__le16 count;      /* count of blobs (if 0: continue prev. packet) */

	__le32 packet_id;  /* unique ID for all packets in one frame */

	__le32 timestamp;  /* milliseconds (inc. by 16 or 17 each frame) */
	__le32 unknown;    /* "epoch?" always 02/03 00 00 00 */

} __packed;

struct sur40_blob {

	__le16 blob_id;

	u8 action;         /* 0x02 = enter/exit, 0x03 = update (?) */
	u8 type;           /* bitmask (0x01 blob,  0x02 touch, 0x04 tag) */

	__le16 bb_pos_x;   /* upper left corner of bounding box */
	__le16 bb_pos_y;

	__le16 bb_size_x;  /* size of bounding box */
	__le16 bb_size_y;

	__le16 pos_x;      /* finger tip position */
	__le16 pos_y;

	__le16 ctr_x;      /* centroid position */
	__le16 ctr_y;

	__le16 axis_x;     /* somehow related to major/minor axis, mostly: */
	__le16 axis_y;     /* axis_x == bb_size_y && axis_y == bb_size_x */

	__le32 angle;      /* orientation in radians relative to x axis -
	                      actually an IEEE754 float, don't use in kernel */

	__le32 area;       /* size in pixels/pressure (?) */

	u8 padding[24];

	__le32 tag_id;     /* valid when type == 0x04 (SUR40_TAG) */
	__le32 unknown;

} __packed;

/* combined header/blob data */
struct sur40_data {
	struct sur40_header header;
	struct sur40_blob   blobs[];
} __packed;

/* read 512 bytes from endpoint 0x82 -> get header below
 * continue reading 16k blocks until header.size bytes read */
struct sur40_image_header {
	__le32 magic;     /* "SUBF" */
	__le32 packet_id;
	__le32 size;      /* always 0x0007e900 = 960x540 */
	__le32 timestamp; /* milliseconds (increases by 16 or 17 each frame) */
	__le32 unknown;   /* "epoch?" always 02/03 00 00 00 */
} __packed;

/* version information */
#define DRIVER_SHORT   "sur40"
#define DRIVER_LONG    "Samsung SUR40"
#define DRIVER_AUTHOR  "Florian 'floe' Echtler <floe@butterbrot.org>"
#define DRIVER_DESC    "Surface2.0/SUR40/PixelSense input driver"

/* vendor and device IDs */
#define ID_MICROSOFT 0x045e
#define ID_SUR40     0x0775

/* sensor resolution */
#define SENSOR_RES_X 1920
#define SENSOR_RES_Y 1080

/* touch data endpoint */
#define TOUCH_ENDPOINT 0x86

/* video data endpoint */
#define VIDEO_ENDPOINT 0x82

/* video header fields */
#define VIDEO_HEADER_MAGIC 0x46425553
#define VIDEO_PACKET_SIZE  16384

/* polling interval (ms) */
#define POLL_INTERVAL 1

/* maximum number of contacts FIXME: this is a guess? */
#define MAX_CONTACTS 64

/* control commands */
#define SUR40_GET_VERSION 0xb0 /* 12 bytes string    */
#define SUR40_ACCEL_CAPS  0xb3 /*  5 bytes           */
#define SUR40_SENSOR_CAPS 0xc1 /* 24 bytes           */

#define SUR40_POKE        0xc5 /* poke register byte */
#define SUR40_PEEK        0xc4 /* 48 bytes registers */

#define SUR40_GET_STATE   0xc5 /*  4 bytes state (?) */
#define SUR40_GET_SENSORS 0xb1 /*  8 bytes sensors   */

#define SUR40_BLOB	0x01
#define SUR40_TOUCH	0x02
#define SUR40_TAG	0x04

/* video controls */
#define SUR40_BRIGHTNESS_MAX 0xff
#define SUR40_BRIGHTNESS_MIN 0x00
#define SUR40_BRIGHTNESS_DEF 0xff

#define SUR40_CONTRAST_MAX 0x0f
#define SUR40_CONTRAST_MIN 0x00
#define SUR40_CONTRAST_DEF 0x0a

#define SUR40_GAIN_MAX 0x09
#define SUR40_GAIN_MIN 0x00
#define SUR40_GAIN_DEF 0x08

#define SUR40_BACKLIGHT_MAX 0x01
#define SUR40_BACKLIGHT_MIN 0x00
#define SUR40_BACKLIGHT_DEF 0x01

#define sur40_str(s) #s
#define SUR40_PARAM_RANGE(lo, hi) " (range " sur40_str(lo) "-" sur40_str(hi) ")"

/* module parameters */
static uint brightness = SUR40_BRIGHTNESS_DEF;
module_param(brightness, uint, 0644);
MODULE_PARM_DESC(brightness, "set initial brightness"
	SUR40_PARAM_RANGE(SUR40_BRIGHTNESS_MIN, SUR40_BRIGHTNESS_MAX));
static uint contrast = SUR40_CONTRAST_DEF;
module_param(contrast, uint, 0644);
MODULE_PARM_DESC(contrast, "set initial contrast"
	SUR40_PARAM_RANGE(SUR40_CONTRAST_MIN, SUR40_CONTRAST_MAX));
static uint gain = SUR40_GAIN_DEF;
module_param(gain, uint, 0644);
MODULE_PARM_DESC(gain, "set initial gain"
	SUR40_PARAM_RANGE(SUR40_GAIN_MIN, SUR40_GAIN_MAX));

static const struct v4l2_pix_format sur40_pix_format[] = {
	{
		.pixelformat = V4L2_TCH_FMT_TU08,
		.width  = SENSOR_RES_X / 2,
		.height = SENSOR_RES_Y / 2,
		.field = V4L2_FIELD_NONE,
		.colorspace = V4L2_COLORSPACE_RAW,
		.bytesperline = SENSOR_RES_X / 2,
		.sizeimage = (SENSOR_RES_X/2) * (SENSOR_RES_Y/2),
	},
	{
		.pixelformat = V4L2_PIX_FMT_GREY,
		.width  = SENSOR_RES_X / 2,
		.height = SENSOR_RES_Y / 2,
		.field = V4L2_FIELD_NONE,
		.colorspace = V4L2_COLORSPACE_RAW,
		.bytesperline = SENSOR_RES_X / 2,
		.sizeimage = (SENSOR_RES_X/2) * (SENSOR_RES_Y/2),
	}
};

/* master device state */
struct sur40_state {

	struct usb_device *usbdev;
	struct device *dev;
	struct input_dev *input;

	struct v4l2_device v4l2;
	struct video_device vdev;
	struct mutex lock;
	struct v4l2_pix_format pix_fmt;
	struct v4l2_ctrl_handler hdl;

	struct vb2_queue queue;
	struct list_head buf_list;
	spinlock_t qlock;
	int sequence;

	struct sur40_data *bulk_in_buffer;
	size_t bulk_in_size;
	u8 bulk_in_epaddr;
	u8 vsvideo;

	char phys[64];
};

struct sur40_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head list;
};

/* forward declarations */
static const struct video_device sur40_video_device;
static const struct vb2_queue sur40_queue;
static void sur40_process_video(struct sur40_state *sur40);
static int sur40_s_ctrl(struct v4l2_ctrl *ctrl);

static const struct v4l2_ctrl_ops sur40_ctrl_ops = {
	.s_ctrl = sur40_s_ctrl,
};

/*
 * Note: an earlier, non-public version of this driver used USB_RECIP_ENDPOINT
 * here by mistake which is very likely to have corrupted the firmware EEPROM
 * on two separate SUR40 devices. Thanks to Alan Stern who spotted this bug.
 * Should you ever run into a similar problem, the background story to this
 * incident and instructions on how to fix the corrupted EEPROM are available
 * at https://floe.butterbrot.org/matrix/hacking/surface/brick.html
*/

/* command wrapper */
static int sur40_command(struct sur40_state *dev,
			 u8 command, u16 index, void *buffer, u16 size)
{
	return usb_control_msg(dev->usbdev, usb_rcvctrlpipe(dev->usbdev, 0),
			       command,
			       USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN,
			       0x00, index, buffer, size, 1000);
}

/* poke a byte in the panel register space */
static int sur40_poke(struct sur40_state *dev, u8 offset, u8 value)
{
	int result;
	u8 index = 0x96; // 0xae for permanent write

	result = usb_control_msg(dev->usbdev, usb_sndctrlpipe(dev->usbdev, 0),
		SUR40_POKE, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_OUT,
		0x32, index, NULL, 0, 1000);
	if (result < 0)
		goto error;
	msleep(5);

	result = usb_control_msg(dev->usbdev, usb_sndctrlpipe(dev->usbdev, 0),
		SUR40_POKE, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_OUT,
		0x72, offset, NULL, 0, 1000);
	if (result < 0)
		goto error;
	msleep(5);

	result = usb_control_msg(dev->usbdev, usb_sndctrlpipe(dev->usbdev, 0),
		SUR40_POKE, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_OUT,
		0xb2, value, NULL, 0, 1000);
	if (result < 0)
		goto error;
	msleep(5);

error:
	return result;
}

static int sur40_set_preprocessor(struct sur40_state *dev, u8 value)
{
	u8 setting_07[2] = { 0x01, 0x00 };
	u8 setting_17[2] = { 0x85, 0x80 };
	int result;

	if (value > 1)
		return -ERANGE;

	result = usb_control_msg(dev->usbdev, usb_sndctrlpipe(dev->usbdev, 0),
		SUR40_POKE, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_OUT,
		0x07, setting_07[value], NULL, 0, 1000);
	if (result < 0)
		goto error;
	msleep(5);

	result = usb_control_msg(dev->usbdev, usb_sndctrlpipe(dev->usbdev, 0),
		SUR40_POKE, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_OUT,
		0x17, setting_17[value], NULL, 0, 1000);
	if (result < 0)
		goto error;
	msleep(5);

error:
	return result;
}

static void sur40_set_vsvideo(struct sur40_state *handle, u8 value)
{
	int i;

	for (i = 0; i < 4; i++)
		sur40_poke(handle, 0x1c+i, value);
	handle->vsvideo = value;
}

static void sur40_set_irlevel(struct sur40_state *handle, u8 value)
{
	int i;

	for (i = 0; i < 8; i++)
		sur40_poke(handle, 0x08+(2*i), value);
}

/* Initialization routine, called from sur40_open */
static int sur40_init(struct sur40_state *dev)
{
	int result;
	u8 *buffer;

	buffer = kmalloc(24, GFP_KERNEL);
	if (!buffer) {
		result = -ENOMEM;
		goto error;
	}

	/* stupidly replay the original MS driver init sequence */
	result = sur40_command(dev, SUR40_GET_VERSION, 0x00, buffer, 12);
	if (result < 0)
		goto error;

	result = sur40_command(dev, SUR40_GET_VERSION, 0x01, buffer, 12);
	if (result < 0)
		goto error;

	result = sur40_command(dev, SUR40_GET_VERSION, 0x02, buffer, 12);
	if (result < 0)
		goto error;

	result = sur40_command(dev, SUR40_SENSOR_CAPS, 0x00, buffer, 24);
	if (result < 0)
		goto error;

	result = sur40_command(dev, SUR40_ACCEL_CAPS, 0x00, buffer, 5);
	if (result < 0)
		goto error;

	result = sur40_command(dev, SUR40_GET_VERSION, 0x03, buffer, 12);
	if (result < 0)
		goto error;

	result = 0;

	/*
	 * Discard the result buffer - no known data inside except
	 * some version strings, maybe extract these sometime...
	 */
error:
	kfree(buffer);
	return result;
}

/*
 * Callback routines from input_dev
 */

/* Enable the device, polling will now start. */
static int sur40_open(struct input_dev *input)
{
	struct sur40_state *sur40 = input_get_drvdata(input);

	dev_dbg(sur40->dev, "open\n");
	return sur40_init(sur40);
}

/* Disable device, polling has stopped. */
static void sur40_close(struct input_dev *input)
{
	struct sur40_state *sur40 = input_get_drvdata(input);

	dev_dbg(sur40->dev, "close\n");
	/*
	 * There is no known way to stop the device, so we simply
	 * stop polling.
	 */
}

/*
 * This function is called when a whole contact has been processed,
 * so that it can assign it to a slot and store the data there.
 */
static void sur40_report_blob(struct sur40_blob *blob, struct input_dev *input)
{
	int wide, major, minor;
	int bb_size_x, bb_size_y, pos_x, pos_y, ctr_x, ctr_y, slotnum;

	if (blob->type != SUR40_TOUCH)
		return;

	slotnum = input_mt_get_slot_by_key(input, blob->blob_id);
	if (slotnum < 0 || slotnum >= MAX_CONTACTS)
		return;

	bb_size_x = le16_to_cpu(blob->bb_size_x);
	bb_size_y = le16_to_cpu(blob->bb_size_y);

	pos_x = le16_to_cpu(blob->pos_x);
	pos_y = le16_to_cpu(blob->pos_y);

	ctr_x = le16_to_cpu(blob->ctr_x);
	ctr_y = le16_to_cpu(blob->ctr_y);

	input_mt_slot(input, slotnum);
	input_mt_report_slot_state(input, MT_TOOL_FINGER, 1);
	wide = (bb_size_x > bb_size_y);
	major = max(bb_size_x, bb_size_y);
	minor = min(bb_size_x, bb_size_y);

	input_report_abs(input, ABS_MT_POSITION_X, pos_x);
	input_report_abs(input, ABS_MT_POSITION_Y, pos_y);
	input_report_abs(input, ABS_MT_TOOL_X, ctr_x);
	input_report_abs(input, ABS_MT_TOOL_Y, ctr_y);

	/* TODO: use a better orientation measure */
	input_report_abs(input, ABS_MT_ORIENTATION, wide);
	input_report_abs(input, ABS_MT_TOUCH_MAJOR, major);
	input_report_abs(input, ABS_MT_TOUCH_MINOR, minor);
}

/* core function: poll for new input data */
static void sur40_poll(struct input_dev *input)
{
	struct sur40_state *sur40 = input_get_drvdata(input);
	int result, bulk_read, need_blobs, packet_blobs, i;
	struct sur40_header *header = &sur40->bulk_in_buffer->header;
	struct sur40_blob *inblob = &sur40->bulk_in_buffer->blobs[0];

	dev_dbg(sur40->dev, "poll\n");

	need_blobs = -1;

	do {

		/* perform a blocking bulk read to get data from the device */
		result = usb_bulk_msg(sur40->usbdev,
			usb_rcvbulkpipe(sur40->usbdev, sur40->bulk_in_epaddr),
			sur40->bulk_in_buffer, sur40->bulk_in_size,
			&bulk_read, 1000);

		dev_dbg(sur40->dev, "received %d bytes\n", bulk_read);

		if (result < 0) {
			dev_err(sur40->dev, "error in usb_bulk_read\n");
			return;
		}

		result = bulk_read - sizeof(struct sur40_header);

		if (result % sizeof(struct sur40_blob) != 0) {
			dev_err(sur40->dev, "transfer size mismatch\n");
			return;
		}

		/* first packet? */
		if (need_blobs == -1) {
			need_blobs = le16_to_cpu(header->count);
			dev_dbg(sur40->dev, "need %d blobs\n", need_blobs);
			/* packet_id = le32_to_cpu(header->packet_id); */
		}

		/*
		 * Sanity check. when video data is also being retrieved, the
		 * packet ID will usually increase in the middle of a series
		 * instead of at the end. However, the data is still consistent,
		 * so the packet ID is probably just valid for the first packet
		 * in a series.

		if (packet_id != le32_to_cpu(header->packet_id))
			dev_dbg(sur40->dev, "packet ID mismatch\n");
		 */

		packet_blobs = result / sizeof(struct sur40_blob);
		dev_dbg(sur40->dev, "received %d blobs\n", packet_blobs);

		/* packets always contain at least 4 blobs, even if empty */
		if (packet_blobs > need_blobs)
			packet_blobs = need_blobs;

		for (i = 0; i < packet_blobs; i++) {
			need_blobs--;
			dev_dbg(sur40->dev, "processing blob\n");
			sur40_report_blob(&(inblob[i]), input);
		}

	} while (need_blobs > 0);

	input_mt_sync_frame(input);
	input_sync(input);

	sur40_process_video(sur40);
}

/* deal with video data */
static void sur40_process_video(struct sur40_state *sur40)
{

	struct sur40_image_header *img = (void *)(sur40->bulk_in_buffer);
	struct sur40_buffer *new_buf;
	struct usb_sg_request sgr;
	struct sg_table *sgt;
	int result, bulk_read;

	if (!vb2_start_streaming_called(&sur40->queue))
		return;

	/* get a new buffer from the list */
	spin_lock(&sur40->qlock);
	if (list_empty(&sur40->buf_list)) {
		dev_dbg(sur40->dev, "buffer queue empty\n");
		spin_unlock(&sur40->qlock);
		return;
	}
	new_buf = list_entry(sur40->buf_list.next, struct sur40_buffer, list);
	list_del(&new_buf->list);
	spin_unlock(&sur40->qlock);

	dev_dbg(sur40->dev, "buffer acquired\n");

	/* retrieve data via bulk read */
	result = usb_bulk_msg(sur40->usbdev,
			usb_rcvbulkpipe(sur40->usbdev, VIDEO_ENDPOINT),
			sur40->bulk_in_buffer, sur40->bulk_in_size,
			&bulk_read, 1000);

	if (result < 0) {
		dev_err(sur40->dev, "error in usb_bulk_read\n");
		goto err_poll;
	}

	if (bulk_read != sizeof(struct sur40_image_header)) {
		dev_err(sur40->dev, "received %d bytes (%zd expected)\n",
			bulk_read, sizeof(struct sur40_image_header));
		goto err_poll;
	}

	if (le32_to_cpu(img->magic) != VIDEO_HEADER_MAGIC) {
		dev_err(sur40->dev, "image magic mismatch\n");
		goto err_poll;
	}

	if (le32_to_cpu(img->size) != sur40->pix_fmt.sizeimage) {
		dev_err(sur40->dev, "image size mismatch\n");
		goto err_poll;
	}

	dev_dbg(sur40->dev, "header acquired\n");

	sgt = vb2_dma_sg_plane_desc(&new_buf->vb.vb2_buf, 0);

	result = usb_sg_init(&sgr, sur40->usbdev,
		usb_rcvbulkpipe(sur40->usbdev, VIDEO_ENDPOINT), 0,
		sgt->sgl, sgt->nents, sur40->pix_fmt.sizeimage, 0);
	if (result < 0) {
		dev_err(sur40->dev, "error %d in usb_sg_init\n", result);
		goto err_poll;
	}

	usb_sg_wait(&sgr);
	if (sgr.status < 0) {
		dev_err(sur40->dev, "error %d in usb_sg_wait\n", sgr.status);
		goto err_poll;
	}

	dev_dbg(sur40->dev, "image acquired\n");

	/* return error if streaming was stopped in the meantime */
	if (sur40->sequence == -1)
		return;

	/* mark as finished */
	new_buf->vb.vb2_buf.timestamp = ktime_get_ns();
	new_buf->vb.sequence = sur40->sequence++;
	new_buf->vb.field = V4L2_FIELD_NONE;
	vb2_buffer_done(&new_buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
	dev_dbg(sur40->dev, "buffer marked done\n");
	return;

err_poll:
	vb2_buffer_done(&new_buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
}

/* Initialize input device parameters. */
static int sur40_input_setup_events(struct input_dev *input_dev)
{
	int error;

	input_set_abs_params(input_dev, ABS_MT_POSITION_X,
			     0, SENSOR_RES_X, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y,
			     0, SENSOR_RES_Y, 0, 0);

	input_set_abs_params(input_dev, ABS_MT_TOOL_X,
			     0, SENSOR_RES_X, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TOOL_Y,
			     0, SENSOR_RES_Y, 0, 0);

	/* max value unknown, but major/minor axis
	 * can never be larger than screen */
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR,
			     0, SENSOR_RES_X, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MINOR,
			     0, SENSOR_RES_Y, 0, 0);

	input_set_abs_params(input_dev, ABS_MT_ORIENTATION, 0, 1, 0, 0);

	error = input_mt_init_slots(input_dev, MAX_CONTACTS,
				    INPUT_MT_DIRECT | INPUT_MT_DROP_UNUSED);
	if (error) {
		dev_err(input_dev->dev.parent, "failed to set up slots\n");
		return error;
	}

	return 0;
}

/* Check candidate USB interface. */
static int sur40_probe(struct usb_interface *interface,
		       const struct usb_device_id *id)
{
	struct usb_device *usbdev = interface_to_usbdev(interface);
	struct sur40_state *sur40;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	struct input_dev *input;
	int error;

	/* Check if we really have the right interface. */
	iface_desc = interface->cur_altsetting;
	if (iface_desc->desc.bInterfaceClass != 0xFF)
		return -ENODEV;

	if (iface_desc->desc.bNumEndpoints < 5)
		return -ENODEV;

	/* Use endpoint #4 (0x86). */
	endpoint = &iface_desc->endpoint[4].desc;
	if (endpoint->bEndpointAddress != TOUCH_ENDPOINT)
		return -ENODEV;

	/* Allocate memory for our device state and initialize it. */
	sur40 = kzalloc(sizeof(struct sur40_state), GFP_KERNEL);
	if (!sur40)
		return -ENOMEM;

	input = input_allocate_device();
	if (!input) {
		error = -ENOMEM;
		goto err_free_dev;
	}

	/* initialize locks/lists */
	INIT_LIST_HEAD(&sur40->buf_list);
	spin_lock_init(&sur40->qlock);
	mutex_init(&sur40->lock);

	/* Set up regular input device structure */
	input->name = DRIVER_LONG;
	usb_to_input_id(usbdev, &input->id);
	usb_make_path(usbdev, sur40->phys, sizeof(sur40->phys));
	strlcat(sur40->phys, "/input0", sizeof(sur40->phys));
	input->phys = sur40->phys;
	input->dev.parent = &interface->dev;

	input->open = sur40_open;
	input->close = sur40_close;

	error = sur40_input_setup_events(input);
	if (error)
		goto err_free_input;

	input_set_drvdata(input, sur40);
	error = input_setup_polling(input, sur40_poll);
	if (error) {
		dev_err(&interface->dev, "failed to set up polling");
		goto err_free_input;
	}

	input_set_poll_interval(input, POLL_INTERVAL);

	sur40->usbdev = usbdev;
	sur40->dev = &interface->dev;
	sur40->input = input;

	/* use the bulk-in endpoint tested above */
	sur40->bulk_in_size = usb_endpoint_maxp(endpoint);
	sur40->bulk_in_epaddr = endpoint->bEndpointAddress;
	sur40->bulk_in_buffer = kmalloc(sur40->bulk_in_size, GFP_KERNEL);
	if (!sur40->bulk_in_buffer) {
		dev_err(&interface->dev, "Unable to allocate input buffer.");
		error = -ENOMEM;
		goto err_free_input;
	}

	/* register the polled input device */
	error = input_register_device(input);
	if (error) {
		dev_err(&interface->dev,
			"Unable to register polled input device.");
		goto err_free_buffer;
	}

	/* register the video master device */
	snprintf(sur40->v4l2.name, sizeof(sur40->v4l2.name), "%s", DRIVER_LONG);
	error = v4l2_device_register(sur40->dev, &sur40->v4l2);
	if (error) {
		dev_err(&interface->dev,
			"Unable to register video master device.");
		goto err_unreg_v4l2;
	}

	/* initialize the lock and subdevice */
	sur40->queue = sur40_queue;
	sur40->queue.drv_priv = sur40;
	sur40->queue.lock = &sur40->lock;
	sur40->queue.dev = sur40->dev;

	/* initialize the queue */
	error = vb2_queue_init(&sur40->queue);
	if (error)
		goto err_unreg_v4l2;

	sur40->pix_fmt = sur40_pix_format[0];
	sur40->vdev = sur40_video_device;
	sur40->vdev.v4l2_dev = &sur40->v4l2;
	sur40->vdev.lock = &sur40->lock;
	sur40->vdev.queue = &sur40->queue;
	video_set_drvdata(&sur40->vdev, sur40);

	/* initialize the control handler for 4 controls */
	v4l2_ctrl_handler_init(&sur40->hdl, 4);
	sur40->v4l2.ctrl_handler = &sur40->hdl;
	sur40->vsvideo = (SUR40_CONTRAST_DEF << 4) | SUR40_GAIN_DEF;

	v4l2_ctrl_new_std(&sur40->hdl, &sur40_ctrl_ops, V4L2_CID_BRIGHTNESS,
	  SUR40_BRIGHTNESS_MIN, SUR40_BRIGHTNESS_MAX, 1, clamp(brightness,
	  (uint)SUR40_BRIGHTNESS_MIN, (uint)SUR40_BRIGHTNESS_MAX));

	v4l2_ctrl_new_std(&sur40->hdl, &sur40_ctrl_ops, V4L2_CID_CONTRAST,
	  SUR40_CONTRAST_MIN, SUR40_CONTRAST_MAX, 1, clamp(contrast,
	  (uint)SUR40_CONTRAST_MIN, (uint)SUR40_CONTRAST_MAX));

	v4l2_ctrl_new_std(&sur40->hdl, &sur40_ctrl_ops, V4L2_CID_GAIN,
	  SUR40_GAIN_MIN, SUR40_GAIN_MAX, 1, clamp(gain,
	  (uint)SUR40_GAIN_MIN, (uint)SUR40_GAIN_MAX));

	v4l2_ctrl_new_std(&sur40->hdl, &sur40_ctrl_ops,
	  V4L2_CID_BACKLIGHT_COMPENSATION, SUR40_BACKLIGHT_MIN,
	  SUR40_BACKLIGHT_MAX, 1, SUR40_BACKLIGHT_DEF);

	v4l2_ctrl_handler_setup(&sur40->hdl);

	if (sur40->hdl.error) {
		dev_err(&interface->dev,
			"Unable to register video controls.");
		v4l2_ctrl_handler_free(&sur40->hdl);
		goto err_unreg_v4l2;
	}

	error = video_register_device(&sur40->vdev, VFL_TYPE_TOUCH, -1);
	if (error) {
		dev_err(&interface->dev,
			"Unable to register video subdevice.");
		goto err_unreg_video;
	}

	/* we can register the device now, as it is ready */
	usb_set_intfdata(interface, sur40);
	dev_dbg(&interface->dev, "%s is now attached\n", DRIVER_DESC);

	return 0;

err_unreg_video:
	video_unregister_device(&sur40->vdev);
err_unreg_v4l2:
	v4l2_device_unregister(&sur40->v4l2);
err_free_buffer:
	kfree(sur40->bulk_in_buffer);
err_free_input:
	input_free_device(input);
err_free_dev:
	kfree(sur40);

	return error;
}

/* Unregister device & clean up. */
static void sur40_disconnect(struct usb_interface *interface)
{
	struct sur40_state *sur40 = usb_get_intfdata(interface);

	v4l2_ctrl_handler_free(&sur40->hdl);
	video_unregister_device(&sur40->vdev);
	v4l2_device_unregister(&sur40->v4l2);

	input_unregister_device(sur40->input);
	kfree(sur40->bulk_in_buffer);
	kfree(sur40);

	usb_set_intfdata(interface, NULL);
	dev_dbg(&interface->dev, "%s is now disconnected\n", DRIVER_DESC);
}

/*
 * Setup the constraints of the queue: besides setting the number of planes
 * per buffer and the size and allocation context of each plane, it also
 * checks if sufficient buffers have been allocated. Usually 3 is a good
 * minimum number: many DMA engines need a minimum of 2 buffers in the
 * queue and you need to have another available for userspace processing.
 */
static int sur40_queue_setup(struct vb2_queue *q,
		       unsigned int *nbuffers, unsigned int *nplanes,
		       unsigned int sizes[], struct device *alloc_devs[])
{
	struct sur40_state *sur40 = vb2_get_drv_priv(q);

	if (q->num_buffers + *nbuffers < 3)
		*nbuffers = 3 - q->num_buffers;

	if (*nplanes)
		return sizes[0] < sur40->pix_fmt.sizeimage ? -EINVAL : 0;

	*nplanes = 1;
	sizes[0] = sur40->pix_fmt.sizeimage;

	return 0;
}

/*
 * Prepare the buffer for queueing to the DMA engine: check and set the
 * payload size.
 */
static int sur40_buffer_prepare(struct vb2_buffer *vb)
{
	struct sur40_state *sur40 = vb2_get_drv_priv(vb->vb2_queue);
	unsigned long size = sur40->pix_fmt.sizeimage;

	if (vb2_plane_size(vb, 0) < size) {
		dev_err(&sur40->usbdev->dev, "buffer too small (%lu < %lu)\n",
			 vb2_plane_size(vb, 0), size);
		return -EINVAL;
	}

	vb2_set_plane_payload(vb, 0, size);
	return 0;
}

/*
 * Queue this buffer to the DMA engine.
 */
static void sur40_buffer_queue(struct vb2_buffer *vb)
{
	struct sur40_state *sur40 = vb2_get_drv_priv(vb->vb2_queue);
	struct sur40_buffer *buf = (struct sur40_buffer *)vb;

	spin_lock(&sur40->qlock);
	list_add_tail(&buf->list, &sur40->buf_list);
	spin_unlock(&sur40->qlock);
}

static void return_all_buffers(struct sur40_state *sur40,
			       enum vb2_buffer_state state)
{
	struct sur40_buffer *buf, *node;

	spin_lock(&sur40->qlock);
	list_for_each_entry_safe(buf, node, &sur40->buf_list, list) {
		vb2_buffer_done(&buf->vb.vb2_buf, state);
		list_del(&buf->list);
	}
	spin_unlock(&sur40->qlock);
}

/*
 * Start streaming. First check if the minimum number of buffers have been
 * queued. If not, then return -ENOBUFS and the vb2 framework will call
 * this function again the next time a buffer has been queued until enough
 * buffers are available to actually start the DMA engine.
 */
static int sur40_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct sur40_state *sur40 = vb2_get_drv_priv(vq);

	sur40->sequence = 0;
	return 0;
}

/*
 * Stop the DMA engine. Any remaining buffers in the DMA queue are dequeued
 * and passed on to the vb2 framework marked as STATE_ERROR.
 */
static void sur40_stop_streaming(struct vb2_queue *vq)
{
	struct sur40_state *sur40 = vb2_get_drv_priv(vq);
	vb2_wait_for_all_buffers(vq);
	sur40->sequence = -1;

	/* Release all active buffers */
	return_all_buffers(sur40, VB2_BUF_STATE_ERROR);
}

/* V4L ioctl */
static int sur40_vidioc_querycap(struct file *file, void *priv,
				 struct v4l2_capability *cap)
{
	struct sur40_state *sur40 = video_drvdata(file);

	strlcpy(cap->driver, DRIVER_SHORT, sizeof(cap->driver));
	strlcpy(cap->card, DRIVER_LONG, sizeof(cap->card));
	usb_make_path(sur40->usbdev, cap->bus_info, sizeof(cap->bus_info));
	return 0;
}

static int sur40_vidioc_enum_input(struct file *file, void *priv,
				   struct v4l2_input *i)
{
	if (i->index != 0)
		return -EINVAL;
	i->type = V4L2_INPUT_TYPE_TOUCH;
	i->std = V4L2_STD_UNKNOWN;
	strlcpy(i->name, "In-Cell Sensor", sizeof(i->name));
	i->capabilities = 0;
	return 0;
}

static int sur40_vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	return (i == 0) ? 0 : -EINVAL;
}

static int sur40_vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int sur40_vidioc_try_fmt(struct file *file, void *priv,
			    struct v4l2_format *f)
{
	switch (f->fmt.pix.pixelformat) {
	case V4L2_PIX_FMT_GREY:
		f->fmt.pix = sur40_pix_format[1];
		break;

	default:
		f->fmt.pix = sur40_pix_format[0];
		break;
	}

	return 0;
}

static int sur40_vidioc_s_fmt(struct file *file, void *priv,
			    struct v4l2_format *f)
{
	struct sur40_state *sur40 = video_drvdata(file);

	switch (f->fmt.pix.pixelformat) {
	case V4L2_PIX_FMT_GREY:
		sur40->pix_fmt = sur40_pix_format[1];
		break;

	default:
		sur40->pix_fmt = sur40_pix_format[0];
		break;
	}

	f->fmt.pix = sur40->pix_fmt;
	return 0;
}

static int sur40_vidioc_g_fmt(struct file *file, void *priv,
			    struct v4l2_format *f)
{
	struct sur40_state *sur40 = video_drvdata(file);

	f->fmt.pix = sur40->pix_fmt;
	return 0;
}

static int sur40_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct sur40_state *sur40  = container_of(ctrl->handler,
	  struct sur40_state, hdl);
	u8 value = sur40->vsvideo;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		sur40_set_irlevel(sur40, ctrl->val);
		break;
	case V4L2_CID_CONTRAST:
		value = (value & 0x0f) | (ctrl->val << 4);
		sur40_set_vsvideo(sur40, value);
		break;
	case V4L2_CID_GAIN:
		value = (value & 0xf0) | (ctrl->val);
		sur40_set_vsvideo(sur40, value);
		break;
	case V4L2_CID_BACKLIGHT_COMPENSATION:
		sur40_set_preprocessor(sur40, ctrl->val);
		break;
	}
	return 0;
}

static int sur40_ioctl_parm(struct file *file, void *priv,
			    struct v4l2_streamparm *p)
{
	if (p->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	p->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
	p->parm.capture.timeperframe.numerator = 1;
	p->parm.capture.timeperframe.denominator = 60;
	p->parm.capture.readbuffers = 3;
	return 0;
}

static int sur40_vidioc_enum_fmt(struct file *file, void *priv,
				 struct v4l2_fmtdesc *f)
{
	if (f->index >= ARRAY_SIZE(sur40_pix_format))
		return -EINVAL;

	f->pixelformat = sur40_pix_format[f->index].pixelformat;
	f->flags = 0;
	return 0;
}

static int sur40_vidioc_enum_framesizes(struct file *file, void *priv,
					struct v4l2_frmsizeenum *f)
{
	struct sur40_state *sur40 = video_drvdata(file);

	if ((f->index != 0) || ((f->pixel_format != V4L2_TCH_FMT_TU08)
		&& (f->pixel_format != V4L2_PIX_FMT_GREY)))
		return -EINVAL;

	f->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	f->discrete.width  = sur40->pix_fmt.width;
	f->discrete.height = sur40->pix_fmt.height;
	return 0;
}

static int sur40_vidioc_enum_frameintervals(struct file *file, void *priv,
					    struct v4l2_frmivalenum *f)
{
	struct sur40_state *sur40 = video_drvdata(file);

	if ((f->index > 0) || ((f->pixel_format != V4L2_TCH_FMT_TU08)
		&& (f->pixel_format != V4L2_PIX_FMT_GREY))
		|| (f->width  != sur40->pix_fmt.width)
		|| (f->height != sur40->pix_fmt.height))
		return -EINVAL;

	f->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	f->discrete.denominator  = 60;
	f->discrete.numerator = 1;
	return 0;
}


static const struct usb_device_id sur40_table[] = {
	{ USB_DEVICE(ID_MICROSOFT, ID_SUR40) },  /* Samsung SUR40 */
	{ }                                      /* terminating null entry */
};
MODULE_DEVICE_TABLE(usb, sur40_table);

/* V4L2 structures */
static const struct vb2_ops sur40_queue_ops = {
	.queue_setup		= sur40_queue_setup,
	.buf_prepare		= sur40_buffer_prepare,
	.buf_queue		= sur40_buffer_queue,
	.start_streaming	= sur40_start_streaming,
	.stop_streaming		= sur40_stop_streaming,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
};

static const struct vb2_queue sur40_queue = {
	.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
	/*
	 * VB2_USERPTR in currently not enabled: passing a user pointer to
	 * dma-sg will result in segment sizes that are not a multiple of
	 * 512 bytes, which is required by the host controller.
	*/
	.io_modes = VB2_MMAP | VB2_READ | VB2_DMABUF,
	.buf_struct_size = sizeof(struct sur40_buffer),
	.ops = &sur40_queue_ops,
	.mem_ops = &vb2_dma_sg_memops,
	.timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC,
	.min_buffers_needed = 3,
};

static const struct v4l2_file_operations sur40_video_fops = {
	.owner = THIS_MODULE,
	.open = v4l2_fh_open,
	.release = vb2_fop_release,
	.unlocked_ioctl = video_ioctl2,
	.read = vb2_fop_read,
	.mmap = vb2_fop_mmap,
	.poll = vb2_fop_poll,
};

static const struct v4l2_ioctl_ops sur40_video_ioctl_ops = {

	.vidioc_querycap	= sur40_vidioc_querycap,

	.vidioc_enum_fmt_vid_cap = sur40_vidioc_enum_fmt,
	.vidioc_try_fmt_vid_cap	= sur40_vidioc_try_fmt,
	.vidioc_s_fmt_vid_cap	= sur40_vidioc_s_fmt,
	.vidioc_g_fmt_vid_cap	= sur40_vidioc_g_fmt,

	.vidioc_enum_framesizes = sur40_vidioc_enum_framesizes,
	.vidioc_enum_frameintervals = sur40_vidioc_enum_frameintervals,

	.vidioc_g_parm = sur40_ioctl_parm,
	.vidioc_s_parm = sur40_ioctl_parm,

	.vidioc_enum_input	= sur40_vidioc_enum_input,
	.vidioc_g_input		= sur40_vidioc_g_input,
	.vidioc_s_input		= sur40_vidioc_s_input,

	.vidioc_reqbufs		= vb2_ioctl_reqbufs,
	.vidioc_create_bufs	= vb2_ioctl_create_bufs,
	.vidioc_querybuf	= vb2_ioctl_querybuf,
	.vidioc_qbuf		= vb2_ioctl_qbuf,
	.vidioc_dqbuf		= vb2_ioctl_dqbuf,
	.vidioc_expbuf		= vb2_ioctl_expbuf,

	.vidioc_streamon	= vb2_ioctl_streamon,
	.vidioc_streamoff	= vb2_ioctl_streamoff,
};

static const struct video_device sur40_video_device = {
	.name = DRIVER_LONG,
	.fops = &sur40_video_fops,
	.ioctl_ops = &sur40_video_ioctl_ops,
	.release = video_device_release_empty,
	.device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_TOUCH |
		       V4L2_CAP_READWRITE | V4L2_CAP_STREAMING,
};

/* USB-specific object needed to register this driver with the USB subsystem. */
static struct usb_driver sur40_driver = {
	.name = DRIVER_SHORT,
	.probe = sur40_probe,
	.disconnect = sur40_disconnect,
	.id_table = sur40_table,
};

module_usb_driver(sur40_driver);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
