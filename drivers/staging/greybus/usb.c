// SPDX-License-Identifier: GPL-2.0
/*
 * USB host driver for the Greybus "generic" USB module.
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/greybus.h>

#include "gbphy.h"

/* Greybus USB request types */
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
	struct gbphy_device *gbphy_dev;
};

static inline struct gb_usb_device *to_gb_usb_device(struct usb_hcd *hcd)
{
	return (struct gb_usb_device *)hcd->hcd_priv;
}

static inline struct usb_hcd *gb_usb_device_to_hcd(struct gb_usb_device *dev)
{
	return container_of((void *)dev, struct usb_hcd, hcd_priv);
}

static void hcd_stop(struct usb_hcd *hcd)
{
	struct gb_usb_device *dev = to_gb_usb_device(hcd);
	int ret;

	ret = gb_operation_sync(dev->connection, GB_USB_TYPE_HCD_STOP,
				NULL, 0, NULL, 0);
	if (ret)
		dev_err(&dev->gbphy_dev->dev, "HCD stop failed '%d'\n", ret);
}

static int hcd_start(struct usb_hcd *hcd)
{
	struct usb_bus *bus = hcd_to_bus(hcd);
	struct gb_usb_device *dev = to_gb_usb_device(hcd);
	int ret;

	ret = gb_operation_sync(dev->connection, GB_USB_TYPE_HCD_START,
				NULL, 0, NULL, 0);
	if (ret) {
		dev_err(&dev->gbphy_dev->dev, "HCD start failed '%d'\n", ret);
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
	struct gb_usb_device *dev = to_gb_usb_device(hcd);
	struct gb_operation *operation;
	struct gb_usb_hub_control_request *request;
	struct gb_usb_hub_control_response *response;
	size_t response_size;
	int ret;

	/* FIXME: handle unspecified lengths */
	response_size = sizeof(*response) + wLength;

	operation = gb_operation_create(dev->connection,
					GB_USB_TYPE_HUB_CONTROL,
					sizeof(*request),
					response_size,
					GFP_KERNEL);
	if (!operation)
		return -ENOMEM;

	request = operation->request->payload;
	request->typeReq = cpu_to_le16(typeReq);
	request->wValue = cpu_to_le16(wValue);
	request->wIndex = cpu_to_le16(wIndex);
	request->wLength = cpu_to_le16(wLength);

	ret = gb_operation_request_send_sync(operation);
	if (ret)
		goto out;

	if (wLength) {
		/* Greybus core has verified response size */
		response = operation->response->payload;
		memcpy(buf, response->buf, wLength);
	}
out:
	gb_operation_put(operation);

	return ret;
}

static const struct hc_driver usb_gb_hc_driver = {
	.description = "greybus-hcd",
	.product_desc = "Greybus USB Host Controller",
	.hcd_priv_size = sizeof(struct gb_usb_device),

	.flags = HCD_USB2,

	.start = hcd_start,
	.stop = hcd_stop,

	.urb_enqueue = urb_enqueue,
	.urb_dequeue = urb_dequeue,

	.get_frame_number = get_frame_number,
	.hub_status_data = hub_status_data,
	.hub_control = hub_control,
};

static int gb_usb_probe(struct gbphy_device *gbphy_dev,
			const struct gbphy_device_id *id)
{
	struct gb_connection *connection;
	struct device *dev = &gbphy_dev->dev;
	struct gb_usb_device *gb_usb_dev;
	struct usb_hcd *hcd;
	int retval;

	hcd = usb_create_hcd(&usb_gb_hc_driver, dev, dev_name(dev));
	if (!hcd)
		return -ENOMEM;

	connection = gb_connection_create(gbphy_dev->bundle,
					  le16_to_cpu(gbphy_dev->cport_desc->id),
					  NULL);
	if (IS_ERR(connection)) {
		retval = PTR_ERR(connection);
		goto exit_usb_put;
	}

	gb_usb_dev = to_gb_usb_device(hcd);
	gb_usb_dev->connection = connection;
	gb_connection_set_data(connection, gb_usb_dev);
	gb_usb_dev->gbphy_dev = gbphy_dev;
	gb_gbphy_set_data(gbphy_dev, gb_usb_dev);

	hcd->has_tt = 1;

	retval = gb_connection_enable(connection);
	if (retval)
		goto exit_connection_destroy;

	/*
	 * FIXME: The USB bridged-PHY protocol driver depends on changes to
	 *        USB core which are not yet upstream.
	 *
	 *        Disable for now.
	 */
	if (1) {
		dev_warn(dev, "USB protocol disabled\n");
		retval = -EPROTONOSUPPORT;
		goto exit_connection_disable;
	}

	retval = usb_add_hcd(hcd, 0, 0);
	if (retval)
		goto exit_connection_disable;

	return 0;

exit_connection_disable:
	gb_connection_disable(connection);
exit_connection_destroy:
	gb_connection_destroy(connection);
exit_usb_put:
	usb_put_hcd(hcd);

	return retval;
}

static void gb_usb_remove(struct gbphy_device *gbphy_dev)
{
	struct gb_usb_device *gb_usb_dev = gb_gbphy_get_data(gbphy_dev);
	struct gb_connection *connection = gb_usb_dev->connection;
	struct usb_hcd *hcd = gb_usb_device_to_hcd(gb_usb_dev);

	usb_remove_hcd(hcd);
	gb_connection_disable(connection);
	gb_connection_destroy(connection);
	usb_put_hcd(hcd);
}

static const struct gbphy_device_id gb_usb_id_table[] = {
	{ GBPHY_PROTOCOL(GREYBUS_PROTOCOL_USB) },
	{ },
};
MODULE_DEVICE_TABLE(gbphy, gb_usb_id_table);

static struct gbphy_driver usb_driver = {
	.name		= "usb",
	.probe		= gb_usb_probe,
	.remove		= gb_usb_remove,
	.id_table	= gb_usb_id_table,
};

module_gbphy_driver(usb_driver);
MODULE_LICENSE("GPL v2");
