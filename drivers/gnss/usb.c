// SPDX-License-Identifier: GPL-2.0
/*
 * Generic USB GNSS receiver driver
 *
 * Copyright (C) 2021 Johan Hovold <johan@kernel.org>
 */

#include <linux/errno.h>
#include <linux/gnss.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>

#define GNSS_USB_READ_BUF_LEN	512
#define GNSS_USB_WRITE_TIMEOUT	1000

static const struct usb_device_id gnss_usb_id_table[] = {
	{ USB_DEVICE(0x1199, 0xb000) },		/* Sierra Wireless XM1210 */
	{ }
};
MODULE_DEVICE_TABLE(usb, gnss_usb_id_table);

struct gnss_usb {
	struct usb_device *udev;
	struct usb_interface *intf;
	struct gnss_device *gdev;
	struct urb *read_urb;
	unsigned int write_pipe;
};

static void gnss_usb_rx_complete(struct urb *urb)
{
	struct gnss_usb *gusb = urb->context;
	struct gnss_device *gdev = gusb->gdev;
	int status = urb->status;
	int len;
	int ret;

	switch (status) {
	case 0:
		break;
	case -ENOENT:
	case -ECONNRESET:
	case -ESHUTDOWN:
		dev_dbg(&gdev->dev, "urb stopped: %d\n", status);
		return;
	case -EPIPE:
		dev_err(&gdev->dev, "urb stopped: %d\n", status);
		return;
	default:
		dev_dbg(&gdev->dev, "nonzero urb status: %d\n", status);
		goto resubmit;
	}

	len = urb->actual_length;
	if (len == 0)
		goto resubmit;

	ret = gnss_insert_raw(gdev, urb->transfer_buffer, len);
	if (ret < len)
		dev_dbg(&gdev->dev, "dropped %d bytes\n", len - ret);
resubmit:
	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret && ret != -EPERM && ret != -ENODEV)
		dev_err(&gdev->dev, "failed to resubmit urb: %d\n", ret);
}

static int gnss_usb_open(struct gnss_device *gdev)
{
	struct gnss_usb *gusb = gnss_get_drvdata(gdev);
	int ret;

	ret = usb_submit_urb(gusb->read_urb, GFP_KERNEL);
	if (ret) {
		if (ret != -EPERM && ret != -ENODEV)
			dev_err(&gdev->dev, "failed to submit urb: %d\n", ret);
		return ret;
	}

	return 0;
}

static void gnss_usb_close(struct gnss_device *gdev)
{
	struct gnss_usb *gusb = gnss_get_drvdata(gdev);

	usb_kill_urb(gusb->read_urb);
}

static int gnss_usb_write_raw(struct gnss_device *gdev,
		const unsigned char *buf, size_t count)
{
	struct gnss_usb *gusb = gnss_get_drvdata(gdev);
	void *tbuf;
	int ret;

	tbuf = kmemdup(buf, count, GFP_KERNEL);
	if (!tbuf)
		return -ENOMEM;

	ret = usb_bulk_msg(gusb->udev, gusb->write_pipe, tbuf, count, NULL,
			GNSS_USB_WRITE_TIMEOUT);
	kfree(tbuf);
	if (ret)
		return ret;

	return count;
}

static const struct gnss_operations gnss_usb_gnss_ops = {
	.open		= gnss_usb_open,
	.close		= gnss_usb_close,
	.write_raw	= gnss_usb_write_raw,
};

static int gnss_usb_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct usb_endpoint_descriptor *in, *out;
	struct gnss_device *gdev;
	struct gnss_usb *gusb;
	struct urb *urb;
	size_t buf_len;
	void *buf;
	int ret;

	ret = usb_find_common_endpoints(intf->cur_altsetting, &in, &out, NULL,
			NULL);
	if (ret)
		return ret;

	gusb = kzalloc(sizeof(*gusb), GFP_KERNEL);
	if (!gusb)
		return -ENOMEM;

	gdev = gnss_allocate_device(&intf->dev);
	if (!gdev) {
		ret = -ENOMEM;
		goto err_free_gusb;
	}

	gdev->ops = &gnss_usb_gnss_ops;
	gdev->type = GNSS_TYPE_NMEA;
	gnss_set_drvdata(gdev, gusb);

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb) {
		ret = -ENOMEM;
		goto err_put_gdev;
	}

	buf_len = max(usb_endpoint_maxp(in), GNSS_USB_READ_BUF_LEN);

	buf = kzalloc(buf_len, GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto err_free_urb;
	}

	usb_fill_bulk_urb(urb, udev,
			usb_rcvbulkpipe(udev, usb_endpoint_num(in)),
			buf, buf_len, gnss_usb_rx_complete, gusb);

	gusb->intf = intf;
	gusb->udev = udev;
	gusb->gdev = gdev;
	gusb->read_urb = urb;
	gusb->write_pipe = usb_sndbulkpipe(udev, usb_endpoint_num(out));

	ret = gnss_register_device(gdev);
	if (ret)
		goto err_free_buf;

	usb_set_intfdata(intf, gusb);

	return 0;

err_free_buf:
	kfree(buf);
err_free_urb:
	usb_free_urb(urb);
err_put_gdev:
	gnss_put_device(gdev);
err_free_gusb:
	kfree(gusb);

	return ret;
}

static void gnss_usb_disconnect(struct usb_interface *intf)
{
	struct gnss_usb *gusb = usb_get_intfdata(intf);

	gnss_deregister_device(gusb->gdev);

	kfree(gusb->read_urb->transfer_buffer);
	usb_free_urb(gusb->read_urb);
	gnss_put_device(gusb->gdev);
	kfree(gusb);
}

static struct usb_driver gnss_usb_driver = {
	.name		= "gnss-usb",
	.probe		= gnss_usb_probe,
	.disconnect	= gnss_usb_disconnect,
	.id_table	= gnss_usb_id_table,
};
module_usb_driver(gnss_usb_driver);

MODULE_AUTHOR("Johan Hovold <johan@kernel.org>");
MODULE_DESCRIPTION("Generic USB GNSS receiver driver");
MODULE_LICENSE("GPL v2");
