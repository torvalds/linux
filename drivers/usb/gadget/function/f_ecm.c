// SPDX-License-Identifier: GPL-2.0+
/*
 * f_ecm.c -- USB CDC Ethernet (ECM) link function driver
 *
 * Copyright (C) 2003-2005,2008 David Brownell
 * Copyright (C) 2008 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* #define VERBOSE_DEBUG */

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/etherdevice.h>

#include "u_ether.h"
#include "u_ether_configfs.h"
#include "u_ecm.h"


/*
 * This function is a "CDC Ethernet Networking Control Model" (CDC ECM)
 * Ethernet link.  The data transfer model is simple (packets sent and
 * received over bulk endpoints using normal short packet termination),
 * and the control model exposes various data and optional notifications.
 *
 * ECM is well standardized and (except for Microsoft) supported by most
 * operating systems with USB host support.  It's the preferred interop
 * solution for Ethernet over USB, at least for firmware based solutions.
 * (Hardware solutions tend to be more minimalist.)  A newer and simpler
 * "Ethernet Emulation Model" (CDC EEM) hasn't yet caught on.
 *
 * Note that ECM requires the use of "alternate settings" for its data
 * interface.  This means that the set_alt() method has real work to do,
 * and also means that a get_alt() method is required.
 */


enum ecm_notify_state {
	ECM_NOTIFY_NONE,		/* don't notify */
	ECM_NOTIFY_CONNECT,		/* issue CONNECT next */
	ECM_NOTIFY_SPEED,		/* issue SPEED_CHANGE next */
};

struct f_ecm {
	struct gether			port;
	u8				ctrl_id, data_id;

	char				ethaddr[14];

	struct usb_ep			*notify;
	struct usb_request		*notify_req;
	u8				notify_state;
	bool				is_open;

	/* FIXME is_open needs some irq-ish locking
	 * ... possibly the same as port.ioport
	 */
};

static inline struct f_ecm *func_to_ecm(struct usb_function *f)
{
	return container_of(f, struct f_ecm, port.func);
}

/* peak (theoretical) bulk transfer rate in bits-per-second */
static inline unsigned ecm_bitrate(struct usb_gadget *g)
{
	if (gadget_is_superspeed(g) && g->speed == USB_SPEED_SUPER)
		return 13 * 1024 * 8 * 1000 * 8;
	else if (gadget_is_dualspeed(g) && g->speed == USB_SPEED_HIGH)
		return 13 * 512 * 8 * 1000 * 8;
	else
		return 19 * 64 * 1 * 1000 * 8;
}

/*-------------------------------------------------------------------------*/

/*
 * Include the status endpoint if we can, even though it's optional.
 *
 * Use wMaxPacketSize big enough to fit CDC_NOTIFY_SPEED_CHANGE in one
 * packet, to simplify cancellation; and a big transfer interval, to
 * waste less bandwidth.
 *
 * Some drivers (like Linux 2.4 cdc-ether!) "need" it to exist even
 * if they ignore the connect/disconnect notifications that real aether
 * can provide.  More advanced cdc configurations might want to support
 * encapsulated commands (vendor-specific, using control-OUT).
 */

#define ECM_STATUS_INTERVAL_MS		32
#define ECM_STATUS_BYTECOUNT		16	/* 8 byte header + data */


/* interface descriptor: */

static struct usb_interface_assoc_descriptor
ecm_iad_descriptor = {
	.bLength =		sizeof ecm_iad_descriptor,
	.bDescriptorType =	USB_DT_INTERFACE_ASSOCIATION,

	/* .bFirstInterface =	DYNAMIC, */
	.bInterfaceCount =	2,	/* control + data */
	.bFunctionClass =	USB_CLASS_COMM,
	.bFunctionSubClass =	USB_CDC_SUBCLASS_ETHERNET,
	.bFunctionProtocol =	USB_CDC_PROTO_NONE,
	/* .iFunction =		DYNAMIC */
};


static struct usb_interface_descriptor ecm_control_intf = {
	.bLength =		sizeof ecm_control_intf,
	.bDescriptorType =	USB_DT_INTERFACE,

	/* .bInterfaceNumber = DYNAMIC */
	/* status endpoint is optional; this could be patched later */
	.bNumEndpoints =	1,
	.bInterfaceClass =	USB_CLASS_COMM,
	.bInterfaceSubClass =	USB_CDC_SUBCLASS_ETHERNET,
	.bInterfaceProtocol =	USB_CDC_PROTO_NONE,
	/* .iInterface = DYNAMIC */
};

static struct usb_cdc_header_desc ecm_header_desc = {
	.bLength =		sizeof ecm_header_desc,
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubType =	USB_CDC_HEADER_TYPE,

	.bcdCDC =		cpu_to_le16(0x0110),
};

static struct usb_cdc_union_desc ecm_union_desc = {
	.bLength =		sizeof(ecm_union_desc),
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubType =	USB_CDC_UNION_TYPE,
	/* .bMasterInterface0 =	DYNAMIC */
	/* .bSlaveInterface0 =	DYNAMIC */
};

static struct usb_cdc_ether_desc ecm_desc = {
	.bLength =		sizeof ecm_desc,
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubType =	USB_CDC_ETHERNET_TYPE,

	/* this descriptor actually adds value, surprise! */
	/* .iMACAddress = DYNAMIC */
	.bmEthernetStatistics =	cpu_to_le32(0), /* no statistics */
	.wMaxSegmentSize =	cpu_to_le16(ETH_FRAME_LEN),
	.wNumberMCFilters =	cpu_to_le16(0),
	.bNumberPowerFilters =	0,
};

/* the default data interface has no endpoints ... */

static struct usb_interface_descriptor ecm_data_nop_intf = {
	.bLength =		sizeof ecm_data_nop_intf,
	.bDescriptorType =	USB_DT_INTERFACE,

	.bInterfaceNumber =	1,
	.bAlternateSetting =	0,
	.bNumEndpoints =	0,
	.bInterfaceClass =	USB_CLASS_CDC_DATA,
	.bInterfaceSubClass =	0,
	.bInterfaceProtocol =	0,
	/* .iInterface = DYNAMIC */
};

/* ... but the "real" data interface has two bulk endpoints */

static struct usb_interface_descriptor ecm_data_intf = {
	.bLength =		sizeof ecm_data_intf,
	.bDescriptorType =	USB_DT_INTERFACE,

	.bInterfaceNumber =	1,
	.bAlternateSetting =	1,
	.bNumEndpoints =	2,
	.bInterfaceClass =	USB_CLASS_CDC_DATA,
	.bInterfaceSubClass =	0,
	.bInterfaceProtocol =	0,
	/* .iInterface = DYNAMIC */
};

/* full speed support: */

static struct usb_endpoint_descriptor fs_ecm_notify_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_INT,
	.wMaxPacketSize =	cpu_to_le16(ECM_STATUS_BYTECOUNT),
	.bInterval =		ECM_STATUS_INTERVAL_MS,
};

static struct usb_endpoint_descriptor fs_ecm_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_endpoint_descriptor fs_ecm_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_descriptor_header *ecm_fs_function[] = {
	/* CDC ECM control descriptors */
	(struct usb_descriptor_header *) &ecm_iad_descriptor,
	(struct usb_descriptor_header *) &ecm_control_intf,
	(struct usb_descriptor_header *) &ecm_header_desc,
	(struct usb_descriptor_header *) &ecm_union_desc,
	(struct usb_descriptor_header *) &ecm_desc,

	/* NOTE: status endpoint might need to be removed */
	(struct usb_descriptor_header *) &fs_ecm_notify_desc,

	/* data interface, altsettings 0 and 1 */
	(struct usb_descriptor_header *) &ecm_data_nop_intf,
	(struct usb_descriptor_header *) &ecm_data_intf,
	(struct usb_descriptor_header *) &fs_ecm_in_desc,
	(struct usb_descriptor_header *) &fs_ecm_out_desc,
	NULL,
};

/* high speed support: */

static struct usb_endpoint_descriptor hs_ecm_notify_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_INT,
	.wMaxPacketSize =	cpu_to_le16(ECM_STATUS_BYTECOUNT),
	.bInterval =		USB_MS_TO_HS_INTERVAL(ECM_STATUS_INTERVAL_MS),
};

static struct usb_endpoint_descriptor hs_ecm_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(512),
};

static struct usb_endpoint_descriptor hs_ecm_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(512),
};

static struct usb_descriptor_header *ecm_hs_function[] = {
	/* CDC ECM control descriptors */
	(struct usb_descriptor_header *) &ecm_iad_descriptor,
	(struct usb_descriptor_header *) &ecm_control_intf,
	(struct usb_descriptor_header *) &ecm_header_desc,
	(struct usb_descriptor_header *) &ecm_union_desc,
	(struct usb_descriptor_header *) &ecm_desc,

	/* NOTE: status endpoint might need to be removed */
	(struct usb_descriptor_header *) &hs_ecm_notify_desc,

	/* data interface, altsettings 0 and 1 */
	(struct usb_descriptor_header *) &ecm_data_nop_intf,
	(struct usb_descriptor_header *) &ecm_data_intf,
	(struct usb_descriptor_header *) &hs_ecm_in_desc,
	(struct usb_descriptor_header *) &hs_ecm_out_desc,
	NULL,
};

/* super speed support: */

static struct usb_endpoint_descriptor ss_ecm_notify_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_INT,
	.wMaxPacketSize =	cpu_to_le16(ECM_STATUS_BYTECOUNT),
	.bInterval =		USB_MS_TO_HS_INTERVAL(ECM_STATUS_INTERVAL_MS),
};

static struct usb_ss_ep_comp_descriptor ss_ecm_intr_comp_desc = {
	.bLength =		sizeof ss_ecm_intr_comp_desc,
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,

	/* the following 3 values can be tweaked if necessary */
	/* .bMaxBurst =		0, */
	/* .bmAttributes =	0, */
	.wBytesPerInterval =	cpu_to_le16(ECM_STATUS_BYTECOUNT),
};

static struct usb_endpoint_descriptor ss_ecm_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(1024),
};

static struct usb_endpoint_descriptor ss_ecm_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor ss_ecm_bulk_comp_desc = {
	.bLength =		sizeof ss_ecm_bulk_comp_desc,
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,

	/* the following 2 values can be tweaked if necessary */
	/* .bMaxBurst =		0, */
	/* .bmAttributes =	0, */
};

static struct usb_descriptor_header *ecm_ss_function[] = {
	/* CDC ECM control descriptors */
	(struct usb_descriptor_header *) &ecm_iad_descriptor,
	(struct usb_descriptor_header *) &ecm_control_intf,
	(struct usb_descriptor_header *) &ecm_header_desc,
	(struct usb_descriptor_header *) &ecm_union_desc,
	(struct usb_descriptor_header *) &ecm_desc,

	/* NOTE: status endpoint might need to be removed */
	(struct usb_descriptor_header *) &ss_ecm_notify_desc,
	(struct usb_descriptor_header *) &ss_ecm_intr_comp_desc,

	/* data interface, altsettings 0 and 1 */
	(struct usb_descriptor_header *) &ecm_data_nop_intf,
	(struct usb_descriptor_header *) &ecm_data_intf,
	(struct usb_descriptor_header *) &ss_ecm_in_desc,
	(struct usb_descriptor_header *) &ss_ecm_bulk_comp_desc,
	(struct usb_descriptor_header *) &ss_ecm_out_desc,
	(struct usb_descriptor_header *) &ss_ecm_bulk_comp_desc,
	NULL,
};

/* string descriptors: */

static struct usb_string ecm_string_defs[] = {
	[0].s = "CDC Ethernet Control Model (ECM)",
	[1].s = "",
	[2].s = "CDC Ethernet Data",
	[3].s = "CDC ECM",
	{  } /* end of list */
};

static struct usb_gadget_strings ecm_string_table = {
	.language =		0x0409,	/* en-us */
	.strings =		ecm_string_defs,
};

static struct usb_gadget_strings *ecm_strings[] = {
	&ecm_string_table,
	NULL,
};

/*-------------------------------------------------------------------------*/

static void ecm_do_notify(struct f_ecm *ecm)
{
	struct usb_request		*req = ecm->notify_req;
	struct usb_cdc_notification	*event;
	struct usb_composite_dev	*cdev = ecm->port.func.config->cdev;
	__le32				*data;
	int				status;

	/* notification already in flight? */
	if (!req)
		return;

	event = req->buf;
	switch (ecm->notify_state) {
	case ECM_NOTIFY_NONE:
		return;

	case ECM_NOTIFY_CONNECT:
		event->bNotificationType = USB_CDC_NOTIFY_NETWORK_CONNECTION;
		if (ecm->is_open)
			event->wValue = cpu_to_le16(1);
		else
			event->wValue = cpu_to_le16(0);
		event->wLength = 0;
		req->length = sizeof *event;

		DBG(cdev, "notify connect %s\n",
				ecm->is_open ? "true" : "false");
		ecm->notify_state = ECM_NOTIFY_SPEED;
		break;

	case ECM_NOTIFY_SPEED:
		event->bNotificationType = USB_CDC_NOTIFY_SPEED_CHANGE;
		event->wValue = cpu_to_le16(0);
		event->wLength = cpu_to_le16(8);
		req->length = ECM_STATUS_BYTECOUNT;

		/* SPEED_CHANGE data is up/down speeds in bits/sec */
		data = req->buf + sizeof *event;
		data[0] = cpu_to_le32(ecm_bitrate(cdev->gadget));
		data[1] = data[0];

		DBG(cdev, "notify speed %d\n", ecm_bitrate(cdev->gadget));
		ecm->notify_state = ECM_NOTIFY_NONE;
		break;
	}
	event->bmRequestType = 0xA1;
	event->wIndex = cpu_to_le16(ecm->ctrl_id);

	ecm->notify_req = NULL;
	status = usb_ep_queue(ecm->notify, req, GFP_ATOMIC);
	if (status < 0) {
		ecm->notify_req = req;
		DBG(cdev, "notify --> %d\n", status);
	}
}

static void ecm_notify(struct f_ecm *ecm)
{
	/* NOTE on most versions of Linux, host side cdc-ethernet
	 * won't listen for notifications until its netdevice opens.
	 * The first notification then sits in the FIFO for a long
	 * time, and the second one is queued.
	 */
	ecm->notify_state = ECM_NOTIFY_CONNECT;
	ecm_do_notify(ecm);
}

static void ecm_notify_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct f_ecm			*ecm = req->context;
	struct usb_composite_dev	*cdev = ecm->port.func.config->cdev;
	struct usb_cdc_notification	*event = req->buf;

	switch (req->status) {
	case 0:
		/* no fault */
		break;
	case -ECONNRESET:
	case -ESHUTDOWN:
		ecm->notify_state = ECM_NOTIFY_NONE;
		break;
	default:
		DBG(cdev, "event %02x --> %d\n",
			event->bNotificationType, req->status);
		break;
	}
	ecm->notify_req = req;
	ecm_do_notify(ecm);
}

static int ecm_setup(struct usb_function *f, const struct usb_ctrlrequest *ctrl)
{
	struct f_ecm		*ecm = func_to_ecm(f);
	struct usb_composite_dev *cdev = f->config->cdev;
	struct usb_request	*req = cdev->req;
	int			value = -EOPNOTSUPP;
	u16			w_index = le16_to_cpu(ctrl->wIndex);
	u16			w_value = le16_to_cpu(ctrl->wValue);
	u16			w_length = le16_to_cpu(ctrl->wLength);

	/* composite driver infrastructure handles everything except
	 * CDC class messages; interface activation uses set_alt().
	 */
	switch ((ctrl->bRequestType << 8) | ctrl->bRequest) {
	case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
			| USB_CDC_SET_ETHERNET_PACKET_FILTER:
		/* see 6.2.30: no data, wIndex = interface,
		 * wValue = packet filter bitmap
		 */
		if (w_length != 0 || w_index != ecm->ctrl_id)
			goto invalid;
		DBG(cdev, "packet filter %02x\n", w_value);
		/* REVISIT locking of cdc_filter.  This assumes the UDC
		 * driver won't have a concurrent packet TX irq running on
		 * another CPU; or that if it does, this write is atomic...
		 */
		ecm->port.cdc_filter = w_value;
		value = 0;
		break;

	/* and optionally:
	 * case USB_CDC_SEND_ENCAPSULATED_COMMAND:
	 * case USB_CDC_GET_ENCAPSULATED_RESPONSE:
	 * case USB_CDC_SET_ETHERNET_MULTICAST_FILTERS:
	 * case USB_CDC_SET_ETHERNET_PM_PATTERN_FILTER:
	 * case USB_CDC_GET_ETHERNET_PM_PATTERN_FILTER:
	 * case USB_CDC_GET_ETHERNET_STATISTIC:
	 */

	default:
invalid:
		DBG(cdev, "invalid control req%02x.%02x v%04x i%04x l%d\n",
			ctrl->bRequestType, ctrl->bRequest,
			w_value, w_index, w_length);
	}

	/* respond with data transfer or status phase? */
	if (value >= 0) {
		DBG(cdev, "ecm req%02x.%02x v%04x i%04x l%d\n",
			ctrl->bRequestType, ctrl->bRequest,
			w_value, w_index, w_length);
		req->zero = 0;
		req->length = value;
		value = usb_ep_queue(cdev->gadget->ep0, req, GFP_ATOMIC);
		if (value < 0)
			ERROR(cdev, "ecm req %02x.%02x response err %d\n",
					ctrl->bRequestType, ctrl->bRequest,
					value);
	}

	/* device either stalls (value < 0) or reports success */
	return value;
}


static int ecm_set_alt(struct usb_function *f, unsigned intf, unsigned alt)
{
	struct f_ecm		*ecm = func_to_ecm(f);
	struct usb_composite_dev *cdev = f->config->cdev;

	/* Control interface has only altsetting 0 */
	if (intf == ecm->ctrl_id) {
		if (alt != 0)
			goto fail;

		VDBG(cdev, "reset ecm control %d\n", intf);
		usb_ep_disable(ecm->notify);
		if (!(ecm->notify->desc)) {
			VDBG(cdev, "init ecm ctrl %d\n", intf);
			if (config_ep_by_speed(cdev->gadget, f, ecm->notify))
				goto fail;
		}
		usb_ep_enable(ecm->notify);

	/* Data interface has two altsettings, 0 and 1 */
	} else if (intf == ecm->data_id) {
		if (alt > 1)
			goto fail;

		if (ecm->port.in_ep->enabled) {
			DBG(cdev, "reset ecm\n");
			gether_disconnect(&ecm->port);
		}

		if (!ecm->port.in_ep->desc ||
		    !ecm->port.out_ep->desc) {
			DBG(cdev, "init ecm\n");
			if (config_ep_by_speed(cdev->gadget, f,
					       ecm->port.in_ep) ||
			    config_ep_by_speed(cdev->gadget, f,
					       ecm->port.out_ep)) {
				ecm->port.in_ep->desc = NULL;
				ecm->port.out_ep->desc = NULL;
				goto fail;
			}
		}

		/* CDC Ethernet only sends data in non-default altsettings.
		 * Changing altsettings resets filters, statistics, etc.
		 */
		if (alt == 1) {
			struct net_device	*net;

			/* Enable zlps by default for ECM conformance;
			 * override for musb_hdrc (avoids txdma ovhead).
			 */
			ecm->port.is_zlp_ok =
				gadget_is_zlp_supported(cdev->gadget);
			ecm->port.cdc_filter = DEFAULT_FILTER;
			DBG(cdev, "activate ecm\n");
			net = gether_connect(&ecm->port);
			if (IS_ERR(net))
				return PTR_ERR(net);
		}

		/* NOTE this can be a minor disagreement with the ECM spec,
		 * which says speed notifications will "always" follow
		 * connection notifications.  But we allow one connect to
		 * follow another (if the first is in flight), and instead
		 * just guarantee that a speed notification is always sent.
		 */
		ecm_notify(ecm);
	} else
		goto fail;

	return 0;
fail:
	return -EINVAL;
}

/* Because the data interface supports multiple altsettings,
 * this ECM function *MUST* implement a get_alt() method.
 */
static int ecm_get_alt(struct usb_function *f, unsigned intf)
{
	struct f_ecm		*ecm = func_to_ecm(f);

	if (intf == ecm->ctrl_id)
		return 0;
	return ecm->port.in_ep->enabled ? 1 : 0;
}

static void ecm_disable(struct usb_function *f)
{
	struct f_ecm		*ecm = func_to_ecm(f);
	struct usb_composite_dev *cdev = f->config->cdev;

	DBG(cdev, "ecm deactivated\n");

	if (ecm->port.in_ep->enabled)
		gether_disconnect(&ecm->port);

	usb_ep_disable(ecm->notify);
	ecm->notify->desc = NULL;
}

/*-------------------------------------------------------------------------*/

/*
 * Callbacks let us notify the host about connect/disconnect when the
 * net device is opened or closed.
 *
 * For testing, note that link states on this side include both opened
 * and closed variants of:
 *
 *   - disconnected/unconfigured
 *   - configured but inactive (data alt 0)
 *   - configured and active (data alt 1)
 *
 * Each needs to be tested with unplug, rmmod, SET_CONFIGURATION, and
 * SET_INTERFACE (altsetting).  Remember also that "configured" doesn't
 * imply the host is actually polling the notification endpoint, and
 * likewise that "active" doesn't imply it's actually using the data
 * endpoints for traffic.
 */

static void ecm_open(struct gether *geth)
{
	struct f_ecm		*ecm = func_to_ecm(&geth->func);

	DBG(ecm->port.func.config->cdev, "%s\n", __func__);

	ecm->is_open = true;
	ecm_notify(ecm);
}

static void ecm_close(struct gether *geth)
{
	struct f_ecm		*ecm = func_to_ecm(&geth->func);

	DBG(ecm->port.func.config->cdev, "%s\n", __func__);

	ecm->is_open = false;
	ecm_notify(ecm);
}

/*-------------------------------------------------------------------------*/

/* ethernet function driver setup/binding */

static int
ecm_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_composite_dev *cdev = c->cdev;
	struct f_ecm		*ecm = func_to_ecm(f);
	struct usb_string	*us;
	int			status;
	struct usb_ep		*ep;

	struct f_ecm_opts	*ecm_opts;

	if (!can_support_ecm(cdev->gadget))
		return -EINVAL;

	ecm_opts = container_of(f->fi, struct f_ecm_opts, func_inst);

	/*
	 * in drivers/usb/gadget/configfs.c:configfs_composite_bind()
	 * configurations are bound in sequence with list_for_each_entry,
	 * in each configuration its functions are bound in sequence
	 * with list_for_each_entry, so we assume no race condition
	 * with regard to ecm_opts->bound access
	 */
	if (!ecm_opts->bound) {
		mutex_lock(&ecm_opts->lock);
		gether_set_gadget(ecm_opts->net, cdev->gadget);
		status = gether_register_netdev(ecm_opts->net);
		mutex_unlock(&ecm_opts->lock);
		if (status)
			return status;
		ecm_opts->bound = true;
	}

	us = usb_gstrings_attach(cdev, ecm_strings,
				 ARRAY_SIZE(ecm_string_defs));
	if (IS_ERR(us))
		return PTR_ERR(us);
	ecm_control_intf.iInterface = us[0].id;
	ecm_data_intf.iInterface = us[2].id;
	ecm_desc.iMACAddress = us[1].id;
	ecm_iad_descriptor.iFunction = us[3].id;

	/* allocate instance-specific interface IDs */
	status = usb_interface_id(c, f);
	if (status < 0)
		goto fail;
	ecm->ctrl_id = status;
	ecm_iad_descriptor.bFirstInterface = status;

	ecm_control_intf.bInterfaceNumber = status;
	ecm_union_desc.bMasterInterface0 = status;

	status = usb_interface_id(c, f);
	if (status < 0)
		goto fail;
	ecm->data_id = status;

	ecm_data_nop_intf.bInterfaceNumber = status;
	ecm_data_intf.bInterfaceNumber = status;
	ecm_union_desc.bSlaveInterface0 = status;

	status = -ENODEV;

	/* allocate instance-specific endpoints */
	ep = usb_ep_autoconfig(cdev->gadget, &fs_ecm_in_desc);
	if (!ep)
		goto fail;
	ecm->port.in_ep = ep;

	ep = usb_ep_autoconfig(cdev->gadget, &fs_ecm_out_desc);
	if (!ep)
		goto fail;
	ecm->port.out_ep = ep;

	/* NOTE:  a status/notification endpoint is *OPTIONAL* but we
	 * don't treat it that way.  It's simpler, and some newer CDC
	 * profiles (wireless handsets) no longer treat it as optional.
	 */
	ep = usb_ep_autoconfig(cdev->gadget, &fs_ecm_notify_desc);
	if (!ep)
		goto fail;
	ecm->notify = ep;

	status = -ENOMEM;

	/* allocate notification request and buffer */
	ecm->notify_req = usb_ep_alloc_request(ep, GFP_KERNEL);
	if (!ecm->notify_req)
		goto fail;
	ecm->notify_req->buf = kmalloc(ECM_STATUS_BYTECOUNT, GFP_KERNEL);
	if (!ecm->notify_req->buf)
		goto fail;
	ecm->notify_req->context = ecm;
	ecm->notify_req->complete = ecm_notify_complete;

	/* support all relevant hardware speeds... we expect that when
	 * hardware is dual speed, all bulk-capable endpoints work at
	 * both speeds
	 */
	hs_ecm_in_desc.bEndpointAddress = fs_ecm_in_desc.bEndpointAddress;
	hs_ecm_out_desc.bEndpointAddress = fs_ecm_out_desc.bEndpointAddress;
	hs_ecm_notify_desc.bEndpointAddress =
		fs_ecm_notify_desc.bEndpointAddress;

	ss_ecm_in_desc.bEndpointAddress = fs_ecm_in_desc.bEndpointAddress;
	ss_ecm_out_desc.bEndpointAddress = fs_ecm_out_desc.bEndpointAddress;
	ss_ecm_notify_desc.bEndpointAddress =
		fs_ecm_notify_desc.bEndpointAddress;

	status = usb_assign_descriptors(f, ecm_fs_function, ecm_hs_function,
			ecm_ss_function, NULL);
	if (status)
		goto fail;

	/* NOTE:  all that is done without knowing or caring about
	 * the network link ... which is unavailable to this code
	 * until we're activated via set_alt().
	 */

	ecm->port.open = ecm_open;
	ecm->port.close = ecm_close;

	DBG(cdev, "CDC Ethernet: %s speed IN/%s OUT/%s NOTIFY/%s\n",
			gadget_is_superspeed(c->cdev->gadget) ? "super" :
			gadget_is_dualspeed(c->cdev->gadget) ? "dual" : "full",
			ecm->port.in_ep->name, ecm->port.out_ep->name,
			ecm->notify->name);
	return 0;

fail:
	if (ecm->notify_req) {
		kfree(ecm->notify_req->buf);
		usb_ep_free_request(ecm->notify, ecm->notify_req);
	}

	ERROR(cdev, "%s: can't bind, err %d\n", f->name, status);

	return status;
}

static inline struct f_ecm_opts *to_f_ecm_opts(struct config_item *item)
{
	return container_of(to_config_group(item), struct f_ecm_opts,
			    func_inst.group);
}

/* f_ecm_item_ops */
USB_ETHERNET_CONFIGFS_ITEM(ecm);

/* f_ecm_opts_dev_addr */
USB_ETHERNET_CONFIGFS_ITEM_ATTR_DEV_ADDR(ecm);

/* f_ecm_opts_host_addr */
USB_ETHERNET_CONFIGFS_ITEM_ATTR_HOST_ADDR(ecm);

/* f_ecm_opts_qmult */
USB_ETHERNET_CONFIGFS_ITEM_ATTR_QMULT(ecm);

/* f_ecm_opts_ifname */
USB_ETHERNET_CONFIGFS_ITEM_ATTR_IFNAME(ecm);

static struct configfs_attribute *ecm_attrs[] = {
	&ecm_opts_attr_dev_addr,
	&ecm_opts_attr_host_addr,
	&ecm_opts_attr_qmult,
	&ecm_opts_attr_ifname,
	NULL,
};

static struct config_item_type ecm_func_type = {
	.ct_item_ops	= &ecm_item_ops,
	.ct_attrs	= ecm_attrs,
	.ct_owner	= THIS_MODULE,
};

static void ecm_free_inst(struct usb_function_instance *f)
{
	struct f_ecm_opts *opts;

	opts = container_of(f, struct f_ecm_opts, func_inst);
	if (opts->bound)
		gether_cleanup(netdev_priv(opts->net));
	else
		free_netdev(opts->net);
	kfree(opts);
}

static struct usb_function_instance *ecm_alloc_inst(void)
{
	struct f_ecm_opts *opts;

	opts = kzalloc(sizeof(*opts), GFP_KERNEL);
	if (!opts)
		return ERR_PTR(-ENOMEM);
	mutex_init(&opts->lock);
	opts->func_inst.free_func_inst = ecm_free_inst;
	opts->net = gether_setup_default();
	if (IS_ERR(opts->net)) {
		struct net_device *net = opts->net;
		kfree(opts);
		return ERR_CAST(net);
	}

	config_group_init_type_name(&opts->func_inst.group, "", &ecm_func_type);

	return &opts->func_inst;
}

static void ecm_free(struct usb_function *f)
{
	struct f_ecm *ecm;
	struct f_ecm_opts *opts;

	ecm = func_to_ecm(f);
	opts = container_of(f->fi, struct f_ecm_opts, func_inst);
	kfree(ecm);
	mutex_lock(&opts->lock);
	opts->refcnt--;
	mutex_unlock(&opts->lock);
}

static void ecm_unbind(struct usb_configuration *c, struct usb_function *f)
{
	struct f_ecm		*ecm = func_to_ecm(f);

	DBG(c->cdev, "ecm unbind\n");

	usb_free_all_descriptors(f);

	kfree(ecm->notify_req->buf);
	usb_ep_free_request(ecm->notify, ecm->notify_req);
}

static struct usb_function *ecm_alloc(struct usb_function_instance *fi)
{
	struct f_ecm	*ecm;
	struct f_ecm_opts *opts;
	int status;

	/* allocate and initialize one new instance */
	ecm = kzalloc(sizeof(*ecm), GFP_KERNEL);
	if (!ecm)
		return ERR_PTR(-ENOMEM);

	opts = container_of(fi, struct f_ecm_opts, func_inst);
	mutex_lock(&opts->lock);
	opts->refcnt++;

	/* export host's Ethernet address in CDC format */
	status = gether_get_host_addr_cdc(opts->net, ecm->ethaddr,
					  sizeof(ecm->ethaddr));
	if (status < 12) {
		kfree(ecm);
		mutex_unlock(&opts->lock);
		return ERR_PTR(-EINVAL);
	}
	ecm_string_defs[1].s = ecm->ethaddr;

	ecm->port.ioport = netdev_priv(opts->net);
	mutex_unlock(&opts->lock);
	ecm->port.cdc_filter = DEFAULT_FILTER;

	ecm->port.func.name = "cdc_ethernet";
	/* descriptors are per-instance copies */
	ecm->port.func.bind = ecm_bind;
	ecm->port.func.unbind = ecm_unbind;
	ecm->port.func.set_alt = ecm_set_alt;
	ecm->port.func.get_alt = ecm_get_alt;
	ecm->port.func.setup = ecm_setup;
	ecm->port.func.disable = ecm_disable;
	ecm->port.func.free_func = ecm_free;

	return &ecm->port.func;
}

DECLARE_USB_FUNCTION_INIT(ecm, ecm_alloc_inst, ecm_alloc);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Brownell");
