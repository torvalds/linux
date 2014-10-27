/*
 * Abilis Systems Single DVB-T Receiver
 * Copyright (C) 2008 Pierrick Hascoet <pierrick.hascoet@abilis.com>
 * Copyright (C) 2010 Devin Heitmueller <dheitmueller@kernellabs.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/usb.h>

#include "as102_drv.h"
#include "as102_usb_drv.h"
#include "as102_fw.h"

static void as102_usb_disconnect(struct usb_interface *interface);
static int as102_usb_probe(struct usb_interface *interface,
			   const struct usb_device_id *id);

static int as102_usb_start_stream(struct as102_dev_t *dev);
static void as102_usb_stop_stream(struct as102_dev_t *dev);

static int as102_open(struct inode *inode, struct file *file);
static int as102_release(struct inode *inode, struct file *file);

static struct usb_device_id as102_usb_id_table[] = {
	{ USB_DEVICE(AS102_USB_DEVICE_VENDOR_ID, AS102_USB_DEVICE_PID_0001) },
	{ USB_DEVICE(PCTV_74E_USB_VID, PCTV_74E_USB_PID) },
	{ USB_DEVICE(ELGATO_EYETV_DTT_USB_VID, ELGATO_EYETV_DTT_USB_PID) },
	{ USB_DEVICE(NBOX_DVBT_DONGLE_USB_VID, NBOX_DVBT_DONGLE_USB_PID) },
	{ USB_DEVICE(SKY_IT_DIGITAL_KEY_USB_VID, SKY_IT_DIGITAL_KEY_USB_PID) },
	{ } /* Terminating entry */
};

/* Note that this table must always have the same number of entries as the
   as102_usb_id_table struct */
static const char * const as102_device_names[] = {
	AS102_REFERENCE_DESIGN,
	AS102_PCTV_74E,
	AS102_ELGATO_EYETV_DTT_NAME,
	AS102_NBOX_DVBT_DONGLE_NAME,
	AS102_SKY_IT_DIGITAL_KEY_NAME,
	NULL /* Terminating entry */
};

/* eLNA configuration: devices built on the reference design work best
   with 0xA0, while custom designs seem to require 0xC0 */
static uint8_t const as102_elna_cfg[] = {
	0xA0,
	0xC0,
	0xC0,
	0xA0,
	0xA0,
	0x00 /* Terminating entry */
};

struct usb_driver as102_usb_driver = {
	.name		= DRIVER_FULL_NAME,
	.probe		= as102_usb_probe,
	.disconnect	= as102_usb_disconnect,
	.id_table	= as102_usb_id_table
};

static const struct file_operations as102_dev_fops = {
	.owner		= THIS_MODULE,
	.open		= as102_open,
	.release	= as102_release,
};

static struct usb_class_driver as102_usb_class_driver = {
	.name		= "aton2-%d",
	.fops		= &as102_dev_fops,
	.minor_base	= AS102_DEVICE_MAJOR,
};

static int as102_usb_xfer_cmd(struct as10x_bus_adapter_t *bus_adap,
			      unsigned char *send_buf, int send_buf_len,
			      unsigned char *recv_buf, int recv_buf_len)
{
	int ret = 0;

	if (send_buf != NULL) {
		ret = usb_control_msg(bus_adap->usb_dev,
				      usb_sndctrlpipe(bus_adap->usb_dev, 0),
				      AS102_USB_DEVICE_TX_CTRL_CMD,
				      USB_DIR_OUT | USB_TYPE_VENDOR |
				      USB_RECIP_DEVICE,
				      bus_adap->cmd_xid, /* value */
				      0, /* index */
				      send_buf, send_buf_len,
				      USB_CTRL_SET_TIMEOUT /* 200 */);
		if (ret < 0) {
			dev_dbg(&bus_adap->usb_dev->dev,
				"usb_control_msg(send) failed, err %i\n", ret);
			return ret;
		}

		if (ret != send_buf_len) {
			dev_dbg(&bus_adap->usb_dev->dev,
			"only wrote %d of %d bytes\n", ret, send_buf_len);
			return -1;
		}
	}

	if (recv_buf != NULL) {
#ifdef TRACE
		dev_dbg(bus_adap->usb_dev->dev,
			"want to read: %d bytes\n", recv_buf_len);
#endif
		ret = usb_control_msg(bus_adap->usb_dev,
				      usb_rcvctrlpipe(bus_adap->usb_dev, 0),
				      AS102_USB_DEVICE_RX_CTRL_CMD,
				      USB_DIR_IN | USB_TYPE_VENDOR |
				      USB_RECIP_DEVICE,
				      bus_adap->cmd_xid, /* value */
				      0, /* index */
				      recv_buf, recv_buf_len,
				      USB_CTRL_GET_TIMEOUT /* 200 */);
		if (ret < 0) {
			dev_dbg(&bus_adap->usb_dev->dev,
				"usb_control_msg(recv) failed, err %i\n", ret);
			return ret;
		}
#ifdef TRACE
		dev_dbg(bus_adap->usb_dev->dev,
			"read %d bytes\n", recv_buf_len);
#endif
	}

	return ret;
}

static int as102_send_ep1(struct as10x_bus_adapter_t *bus_adap,
			  unsigned char *send_buf,
			  int send_buf_len,
			  int swap32)
{
	int ret, actual_len;

	ret = usb_bulk_msg(bus_adap->usb_dev,
			   usb_sndbulkpipe(bus_adap->usb_dev, 1),
			   send_buf, send_buf_len, &actual_len, 200);
	if (ret) {
		dev_dbg(&bus_adap->usb_dev->dev,
			"usb_bulk_msg(send) failed, err %i\n", ret);
		return ret;
	}

	if (actual_len != send_buf_len) {
		dev_dbg(&bus_adap->usb_dev->dev, "only wrote %d of %d bytes\n",
			actual_len, send_buf_len);
		return -1;
	}
	return actual_len;
}

static int as102_read_ep2(struct as10x_bus_adapter_t *bus_adap,
		   unsigned char *recv_buf, int recv_buf_len)
{
	int ret, actual_len;

	if (recv_buf == NULL)
		return -EINVAL;

	ret = usb_bulk_msg(bus_adap->usb_dev,
			   usb_rcvbulkpipe(bus_adap->usb_dev, 2),
			   recv_buf, recv_buf_len, &actual_len, 200);
	if (ret) {
		dev_dbg(&bus_adap->usb_dev->dev,
			"usb_bulk_msg(recv) failed, err %i\n", ret);
		return ret;
	}

	if (actual_len != recv_buf_len) {
		dev_dbg(&bus_adap->usb_dev->dev, "only read %d of %d bytes\n",
			actual_len, recv_buf_len);
		return -1;
	}
	return actual_len;
}

static struct as102_priv_ops_t as102_priv_ops = {
	.upload_fw_pkt	= as102_send_ep1,
	.xfer_cmd	= as102_usb_xfer_cmd,
	.as102_read_ep2	= as102_read_ep2,
	.start_stream	= as102_usb_start_stream,
	.stop_stream	= as102_usb_stop_stream,
};

static int as102_submit_urb_stream(struct as102_dev_t *dev, struct urb *urb)
{
	int err;

	usb_fill_bulk_urb(urb,
			  dev->bus_adap.usb_dev,
			  usb_rcvbulkpipe(dev->bus_adap.usb_dev, 0x2),
			  urb->transfer_buffer,
			  AS102_USB_BUF_SIZE,
			  as102_urb_stream_irq,
			  dev);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err)
		dev_dbg(&urb->dev->dev,
			"%s: usb_submit_urb failed\n", __func__);

	return err;
}

void as102_urb_stream_irq(struct urb *urb)
{
	struct as102_dev_t *as102_dev = urb->context;

	if (urb->actual_length > 0) {
		dvb_dmx_swfilter(&as102_dev->dvb_dmx,
				 urb->transfer_buffer,
				 urb->actual_length);
	} else {
		if (urb->actual_length == 0)
			memset(urb->transfer_buffer, 0, AS102_USB_BUF_SIZE);
	}

	/* is not stopped, re-submit urb */
	if (as102_dev->streaming)
		as102_submit_urb_stream(as102_dev, urb);
}

static void as102_free_usb_stream_buffer(struct as102_dev_t *dev)
{
	int i;

	for (i = 0; i < MAX_STREAM_URB; i++)
		usb_free_urb(dev->stream_urb[i]);

	usb_free_coherent(dev->bus_adap.usb_dev,
			MAX_STREAM_URB * AS102_USB_BUF_SIZE,
			dev->stream,
			dev->dma_addr);
}

static int as102_alloc_usb_stream_buffer(struct as102_dev_t *dev)
{
	int i;

	dev->stream = usb_alloc_coherent(dev->bus_adap.usb_dev,
				       MAX_STREAM_URB * AS102_USB_BUF_SIZE,
				       GFP_KERNEL,
				       &dev->dma_addr);
	if (!dev->stream) {
		dev_dbg(&dev->bus_adap.usb_dev->dev,
			"%s: usb_buffer_alloc failed\n", __func__);
		return -ENOMEM;
	}

	memset(dev->stream, 0, MAX_STREAM_URB * AS102_USB_BUF_SIZE);

	/* init urb buffers */
	for (i = 0; i < MAX_STREAM_URB; i++) {
		struct urb *urb;

		urb = usb_alloc_urb(0, GFP_ATOMIC);
		if (urb == NULL) {
			dev_dbg(&dev->bus_adap.usb_dev->dev,
				"%s: usb_alloc_urb failed\n", __func__);
			as102_free_usb_stream_buffer(dev);
			return -ENOMEM;
		}

		urb->transfer_buffer = dev->stream + (i * AS102_USB_BUF_SIZE);
		urb->transfer_dma = dev->dma_addr + (i * AS102_USB_BUF_SIZE);
		urb->transfer_flags = URB_NO_TRANSFER_DMA_MAP;
		urb->transfer_buffer_length = AS102_USB_BUF_SIZE;

		dev->stream_urb[i] = urb;
	}
	return 0;
}

static void as102_usb_stop_stream(struct as102_dev_t *dev)
{
	int i;

	for (i = 0; i < MAX_STREAM_URB; i++)
		usb_kill_urb(dev->stream_urb[i]);
}

static int as102_usb_start_stream(struct as102_dev_t *dev)
{
	int i, ret = 0;

	for (i = 0; i < MAX_STREAM_URB; i++) {
		ret = as102_submit_urb_stream(dev, dev->stream_urb[i]);
		if (ret) {
			as102_usb_stop_stream(dev);
			return ret;
		}
	}

	return 0;
}

static void as102_usb_release(struct kref *kref)
{
	struct as102_dev_t *as102_dev;

	as102_dev = container_of(kref, struct as102_dev_t, kref);
	if (as102_dev != NULL) {
		usb_put_dev(as102_dev->bus_adap.usb_dev);
		kfree(as102_dev);
	}
}

static void as102_usb_disconnect(struct usb_interface *intf)
{
	struct as102_dev_t *as102_dev;

	/* extract as102_dev_t from usb_device private data */
	as102_dev = usb_get_intfdata(intf);

	/* unregister dvb layer */
	as102_dvb_unregister(as102_dev);

	/* free usb buffers */
	as102_free_usb_stream_buffer(as102_dev);

	usb_set_intfdata(intf, NULL);

	/* usb unregister device */
	usb_deregister_dev(intf, &as102_usb_class_driver);

	/* decrement usage counter */
	kref_put(&as102_dev->kref, as102_usb_release);

	pr_info("%s: device has been disconnected\n", DRIVER_NAME);
}

static int as102_usb_probe(struct usb_interface *intf,
			   const struct usb_device_id *id)
{
	int ret;
	struct as102_dev_t *as102_dev;
	int i;

	/* This should never actually happen */
	if (ARRAY_SIZE(as102_usb_id_table) !=
	    (sizeof(as102_device_names) / sizeof(const char *))) {
		pr_err("Device names table invalid size");
		return -EINVAL;
	}

	as102_dev = kzalloc(sizeof(struct as102_dev_t), GFP_KERNEL);
	if (as102_dev == NULL)
		return -ENOMEM;

	/* Assign the user-friendly device name */
	for (i = 0; i < ARRAY_SIZE(as102_usb_id_table); i++) {
		if (id == &as102_usb_id_table[i]) {
			as102_dev->name = as102_device_names[i];
			as102_dev->elna_cfg = as102_elna_cfg[i];
		}
	}

	if (as102_dev->name == NULL)
		as102_dev->name = "Unknown AS102 device";

	/* set private callback functions */
	as102_dev->bus_adap.ops = &as102_priv_ops;

	/* init cmd token for usb bus */
	as102_dev->bus_adap.cmd = &as102_dev->bus_adap.token.usb.c;
	as102_dev->bus_adap.rsp = &as102_dev->bus_adap.token.usb.r;

	/* init kernel device reference */
	kref_init(&as102_dev->kref);

	/* store as102 device to usb_device private data */
	usb_set_intfdata(intf, (void *) as102_dev);

	/* store in as102 device the usb_device pointer */
	as102_dev->bus_adap.usb_dev = usb_get_dev(interface_to_usbdev(intf));

	/* we can register the device now, as it is ready */
	ret = usb_register_dev(intf, &as102_usb_class_driver);
	if (ret < 0) {
		/* something prevented us from registering this driver */
		dev_err(&intf->dev,
			"%s: usb_register_dev() failed (errno = %d)\n",
			__func__, ret);
		goto failed;
	}

	pr_info("%s: device has been detected\n", DRIVER_NAME);

	/* request buffer allocation for streaming */
	ret = as102_alloc_usb_stream_buffer(as102_dev);
	if (ret != 0)
		goto failed_stream;

	/* register dvb layer */
	ret = as102_dvb_register(as102_dev);
	if (ret != 0)
		goto failed_dvb;

	return ret;

failed_dvb:
	as102_free_usb_stream_buffer(as102_dev);
failed_stream:
	usb_deregister_dev(intf, &as102_usb_class_driver);
failed:
	usb_put_dev(as102_dev->bus_adap.usb_dev);
	usb_set_intfdata(intf, NULL);
	kfree(as102_dev);
	return ret;
}

static int as102_open(struct inode *inode, struct file *file)
{
	int ret = 0, minor = 0;
	struct usb_interface *intf = NULL;
	struct as102_dev_t *dev = NULL;

	/* read minor from inode */
	minor = iminor(inode);

	/* fetch device from usb interface */
	intf = usb_find_interface(&as102_usb_driver, minor);
	if (intf == NULL) {
		pr_err("%s: can't find device for minor %d\n",
		       __func__, minor);
		ret = -ENODEV;
		goto exit;
	}

	/* get our device */
	dev = usb_get_intfdata(intf);
	if (dev == NULL) {
		ret = -EFAULT;
		goto exit;
	}

	/* save our device object in the file's private structure */
	file->private_data = dev;

	/* increment our usage count for the device */
	kref_get(&dev->kref);

exit:
	return ret;
}

static int as102_release(struct inode *inode, struct file *file)
{
	struct as102_dev_t *dev = NULL;

	dev = file->private_data;
	if (dev != NULL) {
		/* decrement the count on our device */
		kref_put(&dev->kref, as102_usb_release);
	}

	return 0;
}

MODULE_DEVICE_TABLE(usb, as102_usb_id_table);
