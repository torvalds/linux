/*
 * USB host driver for the Greybus "generic" USB module.
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>

#include "greybus.h"

/* Version of the Greybus USB protocol we support */
#define GB_USB_VERSION_MAJOR		0x00
#define GB_USB_VERSION_MINOR		0x01

/* Greybus USB request types */
#define GB_USB_TYPE_INVALID		0x00
#define GB_USB_TYPE_PROTOCOL_VERSION	0x01
#define GB_USB_TYPE_HCD_START		0x02
#define GB_USB_TYPE_HCD_STOP		0x03
#define GB_USB_TYPE_HUB_CONTROL		0x04

struct gb_usb_hub_control_request {
	__le16 typeReq;
	__le16 wValue;
	__le16 wIndex;
	__le16 wLength;
};

struct gb_usb_hub_control_response {
	u8 buf[0];
};

struct gb_usb_device {
	struct gb_connection *connection;

	u8 version_major;
	u8 version_minor;
};

static inline struct gb_usb_device *to_gb_usb_device(struct usb_hcd *hcd)
{
	return (struct gb_usb_device *)hcd->hcd_priv;
}

static inline struct usb_hcd *gb_usb_device_to_hcd(struct gb_usb_device *dev)
{
	return container_of((void *)dev, struct usb_hcd, hcd_priv);
}

/* Define get_version() routine */
define_get_version(gb_usb_device, USB);

static void hcd_stop(struct usb_hcd *hcd)
{
	struct gb_usb_device *dev = to_gb_usb_device(hcd);
	int ret;

	ret = gb_operation_sync(dev->connection, GB_USB_TYPE_HCD_STOP,
				NULL, 0, NULL, 0);
	if (ret)
		dev_err(&dev->connection->dev, "HCD stop failed '%d'\n", ret);
}

static int hcd_start(struct usb_hcd *hcd)
{
	struct usb_bus *bus = hcd_to_bus(hcd);
	struct gb_usb_device *dev = to_gb_usb_device(hcd);
	int ret;

	ret = gb_operation_sync(dev->connection, GB_USB_TYPE_HCD_START,
				NULL, 0, NULL, 0);
	if (ret) {
		dev_err(&dev->connection->dev, "HCD start failed '%d'\n", ret);
		return ret;
	}

	hcd->state = HC_STATE_RUNNING;
	if (bus->root_hub)
		usb_hcd_resume_root_hub(hcd);
	return 0;
}

static int urb_enqueue(struct usb_hcd *hcd, struct urb *urb, gfp_t mem_flags)
{
	return -ENXIO;
}

static int urb_dequeue(struct usb_hcd *hcd, struct urb *urb, int status)
{
	return -ENXIO;
}

static int get_frame_number(struct usb_hcd *hcd)
{
	return 0;
}

static int hub_status_data(struct usb_hcd *hcd, char *buf)
{
	return 0;
}

static int hub_control(struct usb_hcd *hcd, u16 typeReq, u16 wValue, u16 wIndex,
		       char *buf, u16 wLength)
{
	struct gb_usb_hub_control_request request;
	struct gb_usb_device *dev = to_gb_usb_device(hcd);
	int ret;

	request.typeReq = cpu_to_le16(typeReq);
	request.wValue = cpu_to_le16(wValue);
	request.wIndex = cpu_to_le16(wIndex);
	request.wLength = cpu_to_le16(wLength);

	// FIXME - buf needs to come back in struct gb_usb_hub_control_response
	// for some types of requests, depending on typeReq.  Do we do this in a
	// "generic" way, or only ask for a response for the ones we "know" need
	// a response (a small subset of all valid typeReq, thankfully.)
	ret = gb_operation_sync(dev->connection, GB_USB_TYPE_HUB_CONTROL,
				&request, sizeof(request), NULL, 0);

	return ret;
}

static struct hc_driver usb_gb_hc_driver = {
	.description = "greybus_usb",
	.product_desc = "GB-Bridge USB Controller", /* TODO: Get this from GPB ?*/
	.flags = HCD_MEMORY | HCD_USB2, /* FIXME: Get this from GPB */
	.hcd_priv_size = sizeof(struct gb_usb_device),

	.start = hcd_start,
	.stop = hcd_stop,

	.urb_enqueue = urb_enqueue,
	.urb_dequeue = urb_dequeue,

	.get_frame_number = get_frame_number,
	.hub_status_data = hub_status_data,
	.hub_control = hub_control,
};

static int gb_usb_connection_init(struct gb_connection *connection)
{
	struct device *dev = &connection->dev;
	struct gb_usb_device *gb_usb_dev;
	struct usb_hcd *hcd;

	int retval;

	hcd = usb_create_hcd(&usb_gb_hc_driver, dev, dev_name(dev));
	if (!hcd)
		return -ENOMEM;

	gb_usb_dev = to_gb_usb_device(hcd);
	gb_usb_dev->connection = connection;
	connection->private = gb_usb_dev;

	/* Check for compatible protocol version */
	retval = get_version(gb_usb_dev);
	if (retval)
		goto err_put_hcd;

	hcd->has_tt = 1;

	retval = usb_add_hcd(hcd, 0, 0);
	if (retval)
		goto err_put_hcd;

	return 0;

err_put_hcd:
	usb_put_hcd(hcd);

	return retval;
}

static void gb_usb_connection_exit(struct gb_connection *connection)
{
	struct gb_usb_device *gb_usb_dev = connection->private;
	struct usb_hcd *hcd = gb_usb_device_to_hcd(gb_usb_dev);

	usb_remove_hcd(hcd);
	usb_put_hcd(hcd);
}

static struct gb_protocol usb_protocol = {
	.name			= "usb",
	.id			= GREYBUS_PROTOCOL_USB,
	.major			= 0,
	.minor			= 1,
	.connection_init	= gb_usb_connection_init,
	.connection_exit	= gb_usb_connection_exit,
	.request_recv		= NULL,	/* FIXME we have requests!!! */
};

gb_builtin_protocol_driver(usb_protocol);
