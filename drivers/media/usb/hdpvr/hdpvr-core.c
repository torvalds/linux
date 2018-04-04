/*
 * Hauppauge HD PVR USB driver
 *
 * Copyright (C) 2001-2004 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2008      Janne Grunau (j@jannau.net)
 * Copyright (C) 2008      John Poet
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>
#include <linux/usb.h>
#include <linux/mutex.h>
#include <linux/i2c.h>

#include <linux/videodev2.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-common.h>

#include "hdpvr.h"

static int video_nr[HDPVR_MAX] = {[0 ... (HDPVR_MAX - 1)] = UNSET};
module_param_array(video_nr, int, NULL, 0);
MODULE_PARM_DESC(video_nr, "video device number (-1=Auto)");

/* holds the number of currently registered devices */
static atomic_t dev_nr = ATOMIC_INIT(-1);

int hdpvr_debug;
module_param(hdpvr_debug, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(hdpvr_debug, "enable debugging output");

static uint default_video_input = HDPVR_VIDEO_INPUTS;
module_param(default_video_input, uint, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(default_video_input, "default video input: 0=Component / 1=S-Video / 2=Composite");

static uint default_audio_input = HDPVR_AUDIO_INPUTS;
module_param(default_audio_input, uint, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(default_audio_input, "default audio input: 0=RCA back / 1=RCA front / 2=S/PDIF");

static bool boost_audio;
module_param(boost_audio, bool, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(boost_audio, "boost the audio signal");


/* table of devices that work with this driver */
static const struct usb_device_id hdpvr_table[] = {
	{ USB_DEVICE(HD_PVR_VENDOR_ID, HD_PVR_PRODUCT_ID) },
	{ USB_DEVICE(HD_PVR_VENDOR_ID, HD_PVR_PRODUCT_ID1) },
	{ USB_DEVICE(HD_PVR_VENDOR_ID, HD_PVR_PRODUCT_ID2) },
	{ USB_DEVICE(HD_PVR_VENDOR_ID, HD_PVR_PRODUCT_ID3) },
	{ USB_DEVICE(HD_PVR_VENDOR_ID, HD_PVR_PRODUCT_ID4) },
	{ }					/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, hdpvr_table);


void hdpvr_delete(struct hdpvr_device *dev)
{
	hdpvr_free_buffers(dev);
	usb_put_dev(dev->udev);
}

static void challenge(u8 *bytes)
{
	__le64 *i64P;
	u64 tmp64;
	uint i, idx;

	for (idx = 0; idx < 32; ++idx) {

		if (idx & 0x3)
			bytes[(idx >> 3) + 3] = bytes[(idx >> 2) & 0x3];

		switch (idx & 0x3) {
		case 0x3:
			bytes[2] += bytes[3] * 4 + bytes[4] + bytes[5];
			bytes[4] += bytes[(idx & 0x1) * 2] * 9 + 9;
			break;
		case 0x1:
			bytes[0] *= 8;
			bytes[0] += 7*idx + 4;
			bytes[6] += bytes[3] * 3;
			break;
		case 0x0:
			bytes[3 - (idx >> 3)] = bytes[idx >> 2];
			bytes[5] += bytes[6] * 3;
			for (i = 0; i < 3; i++)
				bytes[3] *= bytes[3] + 1;
			break;
		case 0x2:
			for (i = 0; i < 3; i++)
				bytes[1] *= bytes[6] + 1;
			for (i = 0; i < 3; i++) {
				i64P = (__le64 *)bytes;
				tmp64 = le64_to_cpup(i64P);
				tmp64 = tmp64 + (tmp64 << (bytes[7] & 0x0f));
				*i64P = cpu_to_le64(tmp64);
			}
			break;
		}
	}
}

/* try to init the device like the windows driver */
static int device_authorization(struct hdpvr_device *dev)
{

	int ret, retval = -ENOMEM;
	char request_type = 0x38, rcv_request = 0x81;
	char *response;

	mutex_lock(&dev->usbc_mutex);
	ret = usb_control_msg(dev->udev,
			      usb_rcvctrlpipe(dev->udev, 0),
			      rcv_request, 0x80 | request_type,
			      0x0400, 0x0003,
			      dev->usbc_buf, 46,
			      10000);
	if (ret != 46) {
		v4l2_err(&dev->v4l2_dev,
			 "unexpected answer of status request, len %d\n", ret);
		goto unlock;
	}
#ifdef HDPVR_DEBUG
	else {
		v4l2_dbg(MSG_INFO, hdpvr_debug, &dev->v4l2_dev,
			 "Status request returned, len %d: %46ph\n",
			 ret, dev->usbc_buf);
	}
#endif

	dev->fw_ver = dev->usbc_buf[1];

	v4l2_info(&dev->v4l2_dev, "firmware version 0x%x dated %s\n",
			  dev->fw_ver, &dev->usbc_buf[2]);

	if (dev->fw_ver > 0x15) {
		dev->options.brightness	= 0x80;
		dev->options.contrast	= 0x40;
		dev->options.hue	= 0xf;
		dev->options.saturation	= 0x40;
		dev->options.sharpness	= 0x80;
	}

	switch (dev->fw_ver) {
	case HDPVR_FIRMWARE_VERSION:
		dev->flags &= ~HDPVR_FLAG_AC3_CAP;
		break;
	case HDPVR_FIRMWARE_VERSION_AC3:
	case HDPVR_FIRMWARE_VERSION_0X12:
	case HDPVR_FIRMWARE_VERSION_0X15:
	case HDPVR_FIRMWARE_VERSION_0X1E:
		dev->flags |= HDPVR_FLAG_AC3_CAP;
		break;
	default:
		v4l2_info(&dev->v4l2_dev, "untested firmware, the driver might not work.\n");
		if (dev->fw_ver >= HDPVR_FIRMWARE_VERSION_AC3)
			dev->flags |= HDPVR_FLAG_AC3_CAP;
		else
			dev->flags &= ~HDPVR_FLAG_AC3_CAP;
	}

	response = dev->usbc_buf+38;
#ifdef HDPVR_DEBUG
	v4l2_dbg(MSG_INFO, hdpvr_debug, &dev->v4l2_dev, "challenge: %8ph\n",
		 response);
#endif
	challenge(response);
#ifdef HDPVR_DEBUG
	v4l2_dbg(MSG_INFO, hdpvr_debug, &dev->v4l2_dev, " response: %8ph\n",
		 response);
#endif

	msleep(100);
	ret = usb_control_msg(dev->udev,
			      usb_sndctrlpipe(dev->udev, 0),
			      0xd1, 0x00 | request_type,
			      0x0000, 0x0000,
			      response, 8,
			      10000);
	v4l2_dbg(MSG_INFO, hdpvr_debug, &dev->v4l2_dev,
		 "magic request returned %d\n", ret);

	retval = ret != 8;
unlock:
	mutex_unlock(&dev->usbc_mutex);
	return retval;
}

static int hdpvr_device_init(struct hdpvr_device *dev)
{
	int ret;
	u8 *buf;

	if (device_authorization(dev))
		return -EACCES;

	/* default options for init */
	hdpvr_set_options(dev);

	/* set filter options */
	mutex_lock(&dev->usbc_mutex);
	buf = dev->usbc_buf;
	buf[0] = 0x03; buf[1] = 0x03; buf[2] = 0x00; buf[3] = 0x00;
	ret = usb_control_msg(dev->udev,
			      usb_sndctrlpipe(dev->udev, 0),
			      0x01, 0x38,
			      CTRL_LOW_PASS_FILTER_VALUE, CTRL_DEFAULT_INDEX,
			      buf, 4,
			      1000);
	v4l2_dbg(MSG_INFO, hdpvr_debug, &dev->v4l2_dev,
		 "control request returned %d\n", ret);
	mutex_unlock(&dev->usbc_mutex);

	/* enable fan and bling leds */
	mutex_lock(&dev->usbc_mutex);
	buf[0] = 0x1;
	ret = usb_control_msg(dev->udev,
			      usb_sndctrlpipe(dev->udev, 0),
			      0xd4, 0x38, 0, 0, buf, 1,
			      1000);
	v4l2_dbg(MSG_INFO, hdpvr_debug, &dev->v4l2_dev,
		 "control request returned %d\n", ret);

	/* boost analog audio */
	buf[0] = boost_audio;
	ret = usb_control_msg(dev->udev,
			      usb_sndctrlpipe(dev->udev, 0),
			      0xd5, 0x38, 0, 0, buf, 1,
			      1000);
	v4l2_dbg(MSG_INFO, hdpvr_debug, &dev->v4l2_dev,
		 "control request returned %d\n", ret);
	mutex_unlock(&dev->usbc_mutex);

	dev->status = STATUS_IDLE;
	return 0;
}

static const struct hdpvr_options hdpvr_default_options = {
	.video_std	= HDPVR_60HZ,
	.video_input	= HDPVR_COMPONENT,
	.audio_input	= HDPVR_RCA_BACK,
	.bitrate	= 65, /* 6 mbps */
	.peak_bitrate	= 90, /* 9 mbps */
	.bitrate_mode	= HDPVR_CONSTANT,
	.gop_mode	= HDPVR_SIMPLE_IDR_GOP,
	.audio_codec	= V4L2_MPEG_AUDIO_ENCODING_AAC,
	/* original picture controls for firmware version <= 0x15 */
	/* updated in device_authorization() for newer firmware */
	.brightness	= 0x86,
	.contrast	= 0x80,
	.hue		= 0x80,
	.saturation	= 0x80,
	.sharpness	= 0x80,
};

static int hdpvr_probe(struct usb_interface *interface,
		       const struct usb_device_id *id)
{
	struct hdpvr_device *dev;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
#if IS_ENABLED(CONFIG_I2C)
	struct i2c_client *client;
#endif
	size_t buffer_size;
	int i;
	int retval = -ENOMEM;

	/* allocate memory for our device state and initialize it */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		dev_err(&interface->dev, "Out of memory\n");
		goto error;
	}

	/* init video transfer queues first of all */
	/* to prevent oops in hdpvr_delete() on error paths */
	INIT_LIST_HEAD(&dev->free_buff_list);
	INIT_LIST_HEAD(&dev->rec_buff_list);

	/* register v4l2_device early so it can be used for printks */
	if (v4l2_device_register(&interface->dev, &dev->v4l2_dev)) {
		dev_err(&interface->dev, "v4l2_device_register failed\n");
		goto error_free_dev;
	}

	mutex_init(&dev->io_mutex);
	mutex_init(&dev->i2c_mutex);
	mutex_init(&dev->usbc_mutex);
	dev->usbc_buf = kmalloc(64, GFP_KERNEL);
	if (!dev->usbc_buf) {
		v4l2_err(&dev->v4l2_dev, "Out of memory\n");
		goto error_v4l2_unregister;
	}

	init_waitqueue_head(&dev->wait_buffer);
	init_waitqueue_head(&dev->wait_data);

	dev->options = hdpvr_default_options;

	if (default_video_input < HDPVR_VIDEO_INPUTS)
		dev->options.video_input = default_video_input;

	if (default_audio_input < HDPVR_AUDIO_INPUTS) {
		dev->options.audio_input = default_audio_input;
		if (default_audio_input == HDPVR_SPDIF)
			dev->options.audio_codec =
				V4L2_MPEG_AUDIO_ENCODING_AC3;
	}

	dev->udev = usb_get_dev(interface_to_usbdev(interface));

	/* set up the endpoint information */
	/* use only the first bulk-in and bulk-out endpoints */
	iface_desc = interface->cur_altsetting;
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		if (!dev->bulk_in_endpointAddr &&
		    usb_endpoint_is_bulk_in(endpoint)) {
			/* USB interface description is buggy, reported max
			 * packet size is 512 bytes, windows driver uses 8192 */
			buffer_size = 8192;
			dev->bulk_in_size = buffer_size;
			dev->bulk_in_endpointAddr = endpoint->bEndpointAddress;
		}

	}
	if (!dev->bulk_in_endpointAddr) {
		v4l2_err(&dev->v4l2_dev, "Could not find bulk-in endpoint\n");
		goto error_put_usb;
	}

	/* init the device */
	if (hdpvr_device_init(dev)) {
		v4l2_err(&dev->v4l2_dev, "device init failed\n");
		goto error_put_usb;
	}

	mutex_lock(&dev->io_mutex);
	if (hdpvr_alloc_buffers(dev, NUM_BUFFERS)) {
		mutex_unlock(&dev->io_mutex);
		v4l2_err(&dev->v4l2_dev,
			 "allocating transfer buffers failed\n");
		goto error_put_usb;
	}
	mutex_unlock(&dev->io_mutex);

#if IS_ENABLED(CONFIG_I2C)
	retval = hdpvr_register_i2c_adapter(dev);
	if (retval < 0) {
		v4l2_err(&dev->v4l2_dev, "i2c adapter register failed\n");
		goto error_free_buffers;
	}

	client = hdpvr_register_ir_i2c(dev);
	if (!client) {
		v4l2_err(&dev->v4l2_dev, "i2c IR device register failed\n");
		retval = -ENODEV;
		goto reg_fail;
	}
#endif

	retval = hdpvr_register_videodev(dev, &interface->dev,
				    video_nr[atomic_inc_return(&dev_nr)]);
	if (retval < 0) {
		v4l2_err(&dev->v4l2_dev, "registering videodev failed\n");
		goto reg_fail;
	}

	/* let the user know what node this device is now attached to */
	v4l2_info(&dev->v4l2_dev, "device now attached to %s\n",
		  video_device_node_name(&dev->video_dev));
	return 0;

reg_fail:
#if IS_ENABLED(CONFIG_I2C)
	i2c_del_adapter(&dev->i2c_adapter);
error_free_buffers:
#endif
	hdpvr_free_buffers(dev);
error_put_usb:
	usb_put_dev(dev->udev);
	kfree(dev->usbc_buf);
error_v4l2_unregister:
	v4l2_device_unregister(&dev->v4l2_dev);
error_free_dev:
	kfree(dev);
error:
	return retval;
}

static void hdpvr_disconnect(struct usb_interface *interface)
{
	struct hdpvr_device *dev = to_hdpvr_dev(usb_get_intfdata(interface));

	v4l2_info(&dev->v4l2_dev, "device %s disconnected\n",
		  video_device_node_name(&dev->video_dev));
	/* prevent more I/O from starting and stop any ongoing */
	mutex_lock(&dev->io_mutex);
	dev->status = STATUS_DISCONNECTED;
	wake_up_interruptible(&dev->wait_data);
	wake_up_interruptible(&dev->wait_buffer);
	mutex_unlock(&dev->io_mutex);
	v4l2_device_disconnect(&dev->v4l2_dev);
	msleep(100);
	flush_work(&dev->worker);
	mutex_lock(&dev->io_mutex);
	hdpvr_cancel_queue(dev);
	mutex_unlock(&dev->io_mutex);
#if IS_ENABLED(CONFIG_I2C)
	i2c_del_adapter(&dev->i2c_adapter);
#endif
	video_unregister_device(&dev->video_dev);
	atomic_dec(&dev_nr);
}


static struct usb_driver hdpvr_usb_driver = {
	.name =		"hdpvr",
	.probe =	hdpvr_probe,
	.disconnect =	hdpvr_disconnect,
	.id_table =	hdpvr_table,
};

module_usb_driver(hdpvr_usb_driver);

MODULE_LICENSE("GPL");
MODULE_VERSION("0.2.1");
MODULE_AUTHOR("Janne Grunau");
MODULE_DESCRIPTION("Hauppauge HD PVR driver");
