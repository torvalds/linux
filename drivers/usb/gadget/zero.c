/*
 * zero.c -- Gadget Zero, for USB development
 *
 * Copyright (C) 2003-2007 David Brownell
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


/*
 * Gadget Zero only needs two bulk endpoints, and is an example of how you
 * can write a hardware-agnostic gadget driver running inside a USB device.
 *
 * Hardware details are visible (see CONFIG_USB_ZERO_* below) but don't
 * affect most of the driver.
 *
 * Use it with the Linux host/master side "usbtest" driver to get a basic
 * functional test of your device-side usb stack, or with "usb-skeleton".
 *
 * It supports two similar configurations.  One sinks whatever the usb host
 * writes, and in return sources zeroes.  The other loops whatever the host
 * writes back, so the host can read it.  Module options include:
 *
 *   buflen=N		default N=4096, buffer size used
 *   qlen=N		default N=32, how many buffers in the loopback queue
 *   loopdefault	default false, list loopback config first
 *
 * Many drivers will only have one configuration, letting them be much
 * simpler if they also don't support high speed operation (like this
 * driver does).
 *
 * Why is *this* driver using two configurations, rather than setting up
 * two interfaces with different functions?  To help verify that multiple
 * configuration infrastucture is working correctly; also, so that it can
 * work with low capability USB controllers without four bulk endpoints.
 */

/* #define VERBOSE_DEBUG */

#include <linux/kernel.h>
#include <linux/utsname.h>
#include <linux/device.h>

#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>

#include "gadget_chips.h"


/*-------------------------------------------------------------------------*/

#define DRIVER_VERSION		"Lughnasadh, 2007"

static const char shortname [] = "zero";
static const char longname [] = "Gadget Zero";

static const char source_sink [] = "source and sink data";
static const char loopback [] = "loop input to output";

/*-------------------------------------------------------------------------*/

/*
 * driver assumes self-powered hardware, and
 * has no way for users to trigger remote wakeup.
 *
 * this version autoconfigures as much as possible,
 * which is reasonable for most "bulk-only" drivers.
 */
static const char *EP_IN_NAME;		/* source */
static const char *EP_OUT_NAME;		/* sink */

/*-------------------------------------------------------------------------*/

/* big enough to hold our biggest descriptor */
#define USB_BUFSIZ	256

struct zero_dev {
	spinlock_t		lock;
	struct usb_gadget	*gadget;
	struct usb_request	*req;		/* for control responses */

	/* when configured, we have one of two configs:
	 * - source data (in to host) and sink it (out from host)
	 * - or loop it back (out from host back in to host)
	 */
	u8			config;
	struct usb_ep		*in_ep, *out_ep;

	/* autoresume timer */
	struct timer_list	resume;
};

#define DBG(d, fmt, args...) \
	dev_dbg(&(d)->gadget->dev , fmt , ## args)
#define VDBG(d, fmt, args...) \
	dev_vdbg(&(d)->gadget->dev , fmt , ## args)
#define ERROR(d, fmt, args...) \
	dev_err(&(d)->gadget->dev , fmt , ## args)
#define WARN(d, fmt, args...) \
	dev_warn(&(d)->gadget->dev , fmt , ## args)
#define INFO(d, fmt, args...) \
	dev_info(&(d)->gadget->dev , fmt , ## args)

/*-------------------------------------------------------------------------*/

static unsigned buflen = 4096;
static unsigned qlen = 32;
static unsigned pattern = 0;

module_param (buflen, uint, S_IRUGO);
module_param (qlen, uint, S_IRUGO);
module_param (pattern, uint, S_IRUGO|S_IWUSR);

/*
 * if it's nonzero, autoresume says how many seconds to wait
 * before trying to wake up the host after suspend.
 */
static unsigned autoresume = 0;
module_param (autoresume, uint, 0);

/*
 * Normally the "loopback" configuration is second (index 1) so
 * it's not the default.  Here's where to change that order, to
 * work better with hosts where config changes are problematic.
 * Or controllers (like superh) that only support one config.
 */
static int loopdefault = 0;

module_param (loopdefault, bool, S_IRUGO|S_IWUSR);

/*-------------------------------------------------------------------------*/

/* Thanks to NetChip Technologies for donating this product ID.
 *
 * DO NOT REUSE THESE IDs with a protocol-incompatible driver!!  Ever!!
 * Instead:  allocate your own, using normal USB-IF procedures.
 */
#ifndef	CONFIG_USB_ZERO_HNPTEST
#define DRIVER_VENDOR_NUM	0x0525		/* NetChip */
#define DRIVER_PRODUCT_NUM	0xa4a0		/* Linux-USB "Gadget Zero" */
#else
#define DRIVER_VENDOR_NUM	0x1a0a		/* OTG test device IDs */
#define DRIVER_PRODUCT_NUM	0xbadd
#endif

/*-------------------------------------------------------------------------*/

/*
 * DESCRIPTORS ... most are static, but strings and (full)
 * configuration descriptors are built on demand.
 */

#define STRING_MANUFACTURER		25
#define STRING_PRODUCT			42
#define STRING_SERIAL			101
#define STRING_SOURCE_SINK		250
#define STRING_LOOPBACK			251

/*
 * This device advertises two configurations; these numbers work
 * on a pxa250 as well as more flexible hardware.
 */
#define	CONFIG_SOURCE_SINK	3
#define	CONFIG_LOOPBACK		2

static struct usb_device_descriptor
device_desc = {
	.bLength =		sizeof device_desc,
	.bDescriptorType =	USB_DT_DEVICE,

	.bcdUSB =		__constant_cpu_to_le16 (0x0200),
	.bDeviceClass =		USB_CLASS_VENDOR_SPEC,

	.idVendor =		__constant_cpu_to_le16 (DRIVER_VENDOR_NUM),
	.idProduct =		__constant_cpu_to_le16 (DRIVER_PRODUCT_NUM),
	.iManufacturer =	STRING_MANUFACTURER,
	.iProduct =		STRING_PRODUCT,
	.iSerialNumber =	STRING_SERIAL,
	.bNumConfigurations =	2,
};

static struct usb_config_descriptor
source_sink_config = {
	.bLength =		sizeof source_sink_config,
	.bDescriptorType =	USB_DT_CONFIG,

	/* compute wTotalLength on the fly */
	.bNumInterfaces =	1,
	.bConfigurationValue =	CONFIG_SOURCE_SINK,
	.iConfiguration =	STRING_SOURCE_SINK,
	.bmAttributes =		USB_CONFIG_ATT_ONE | USB_CONFIG_ATT_SELFPOWER,
	.bMaxPower =		1,	/* self-powered */
};

static struct usb_config_descriptor
loopback_config = {
	.bLength =		sizeof loopback_config,
	.bDescriptorType =	USB_DT_CONFIG,

	/* compute wTotalLength on the fly */
	.bNumInterfaces =	1,
	.bConfigurationValue =	CONFIG_LOOPBACK,
	.iConfiguration =	STRING_LOOPBACK,
	.bmAttributes =		USB_CONFIG_ATT_ONE | USB_CONFIG_ATT_SELFPOWER,
	.bMaxPower =		1,	/* self-powered */
};

static struct usb_otg_descriptor
otg_descriptor = {
	.bLength =		sizeof otg_descriptor,
	.bDescriptorType =	USB_DT_OTG,

	.bmAttributes =		USB_OTG_SRP,
};

/* one interface in each configuration */

static const struct usb_interface_descriptor
source_sink_intf = {
	.bLength =		sizeof source_sink_intf,
	.bDescriptorType =	USB_DT_INTERFACE,

	.bNumEndpoints =	2,
	.bInterfaceClass =	USB_CLASS_VENDOR_SPEC,
	.iInterface =		STRING_SOURCE_SINK,
};

static const struct usb_interface_descriptor
loopback_intf = {
	.bLength =		sizeof loopback_intf,
	.bDescriptorType =	USB_DT_INTERFACE,

	.bNumEndpoints =	2,
	.bInterfaceClass =	USB_CLASS_VENDOR_SPEC,
	.iInterface =		STRING_LOOPBACK,
};

/* two full speed bulk endpoints; their use is config-dependent */

static struct usb_endpoint_descriptor
fs_source_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_endpoint_descriptor
fs_sink_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static const struct usb_descriptor_header *fs_source_sink_function [] = {
	(struct usb_descriptor_header *) &otg_descriptor,
	(struct usb_descriptor_header *) &source_sink_intf,
	(struct usb_descriptor_header *) &fs_sink_desc,
	(struct usb_descriptor_header *) &fs_source_desc,
	NULL,
};

static const struct usb_descriptor_header *fs_loopback_function [] = {
	(struct usb_descriptor_header *) &otg_descriptor,
	(struct usb_descriptor_header *) &loopback_intf,
	(struct usb_descriptor_header *) &fs_sink_desc,
	(struct usb_descriptor_header *) &fs_source_desc,
	NULL,
};

/*
 * usb 2.0 devices need to expose both high speed and full speed
 * descriptors, unless they only run at full speed.
 *
 * that means alternate endpoint descriptors (bigger packets)
 * and a "device qualifier" ... plus more construction options
 * for the config descriptor.
 */

static struct usb_endpoint_descriptor
hs_source_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	__constant_cpu_to_le16 (512),
};

static struct usb_endpoint_descriptor
hs_sink_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	__constant_cpu_to_le16 (512),
};

static struct usb_qualifier_descriptor
dev_qualifier = {
	.bLength =		sizeof dev_qualifier,
	.bDescriptorType =	USB_DT_DEVICE_QUALIFIER,

	.bcdUSB =		__constant_cpu_to_le16 (0x0200),
	.bDeviceClass =		USB_CLASS_VENDOR_SPEC,

	.bNumConfigurations =	2,
};

static const struct usb_descriptor_header *hs_source_sink_function [] = {
	(struct usb_descriptor_header *) &otg_descriptor,
	(struct usb_descriptor_header *) &source_sink_intf,
	(struct usb_descriptor_header *) &hs_source_desc,
	(struct usb_descriptor_header *) &hs_sink_desc,
	NULL,
};

static const struct usb_descriptor_header *hs_loopback_function [] = {
	(struct usb_descriptor_header *) &otg_descriptor,
	(struct usb_descriptor_header *) &loopback_intf,
	(struct usb_descriptor_header *) &hs_source_desc,
	(struct usb_descriptor_header *) &hs_sink_desc,
	NULL,
};

/* maxpacket and other transfer characteristics vary by speed. */
static inline struct usb_endpoint_descriptor *
ep_desc(struct usb_gadget *g, struct usb_endpoint_descriptor *hs,
		struct usb_endpoint_descriptor *fs)
{
	if (gadget_is_dualspeed(g) && g->speed == USB_SPEED_HIGH)
		return hs;
	return fs;
}

static char manufacturer[50];

/* default serial number takes at least two packets */
static char serial[] = "0123456789.0123456789.0123456789";


/* static strings, in UTF-8 */
static struct usb_string		strings [] = {
	{ STRING_MANUFACTURER, manufacturer, },
	{ STRING_PRODUCT, longname, },
	{ STRING_SERIAL, serial, },
	{ STRING_LOOPBACK, loopback, },
	{ STRING_SOURCE_SINK, source_sink, },
	{  }			/* end of list */
};

static struct usb_gadget_strings	stringtab = {
	.language	= 0x0409,	/* en-us */
	.strings	= strings,
};

/*
 * config descriptors are also handcrafted.  these must agree with code
 * that sets configurations, and with code managing interfaces and their
 * altsettings.  other complexity may come from:
 *
 *  - high speed support, including "other speed config" rules
 *  - multiple configurations
 *  - interfaces with alternate settings
 *  - embedded class or vendor-specific descriptors
 *
 * this handles high speed, and has a second config that could as easily
 * have been an alternate interface setting (on most hardware).
 *
 * NOTE:  to demonstrate (and test) more USB capabilities, this driver
 * should include an altsetting to test interrupt transfers, including
 * high bandwidth modes at high speed.  (Maybe work like Intel's test
 * device?)
 */
static int
config_buf (struct usb_gadget *gadget,
		u8 *buf, u8 type, unsigned index)
{
	int				is_source_sink;
	int				len;
	const struct usb_descriptor_header **function;
	int				hs = 0;

	/* two configurations will always be index 0 and index 1 */
	if (index > 1)
		return -EINVAL;
	is_source_sink = loopdefault ? (index == 1) : (index == 0);

	if (gadget_is_dualspeed(gadget)) {
		hs = (gadget->speed == USB_SPEED_HIGH);
		if (type == USB_DT_OTHER_SPEED_CONFIG)
			hs = !hs;
	}
	if (hs)
		function = is_source_sink
			? hs_source_sink_function
			: hs_loopback_function;
	else
		function = is_source_sink
			? fs_source_sink_function
			: fs_loopback_function;

	/* for now, don't advertise srp-only devices */
	if (!gadget_is_otg(gadget))
		function++;

	len = usb_gadget_config_buf (is_source_sink
					? &source_sink_config
					: &loopback_config,
			buf, USB_BUFSIZ, function);
	if (len < 0)
		return len;
	((struct usb_config_descriptor *) buf)->bDescriptorType = type;
	return len;
}

/*-------------------------------------------------------------------------*/

static struct usb_request *
alloc_ep_req (struct usb_ep *ep, unsigned length)
{
	struct usb_request	*req;

	req = usb_ep_alloc_request (ep, GFP_ATOMIC);
	if (req) {
		req->length = length;
		req->buf = kmalloc(length, GFP_ATOMIC);
		if (!req->buf) {
			usb_ep_free_request (ep, req);
			req = NULL;
		}
	}
	return req;
}

static void free_ep_req (struct usb_ep *ep, struct usb_request *req)
{
	kfree(req->buf);
	usb_ep_free_request (ep, req);
}

/*-------------------------------------------------------------------------*/

/*
 * SOURCE/SINK FUNCTION ... a primary testing vehicle for USB peripherals,
 * this just sinks bulk packets OUT to the peripheral and sources them IN
 * to the host, optionally with specific data patterns.
 *
 * In terms of control messaging, this supports all the standard requests
 * plus two that support control-OUT tests.
 *
 * Note that because this doesn't queue more than one request at a time,
 * some other function must be used to test queueing logic.  The network
 * link (g_ether) is probably the best option for that.
 */

/* optionally require specific source/sink data patterns  */

static int
check_read_data (
	struct zero_dev		*dev,
	struct usb_ep		*ep,
	struct usb_request	*req
)
{
	unsigned	i;
	u8		*buf = req->buf;

	for (i = 0; i < req->actual; i++, buf++) {
		switch (pattern) {
		/* all-zeroes has no synchronization issues */
		case 0:
			if (*buf == 0)
				continue;
			break;
		/* mod63 stays in sync with short-terminated transfers,
		 * or otherwise when host and gadget agree on how large
		 * each usb transfer request should be.  resync is done
		 * with set_interface or set_config.
		 */
		case 1:
			if (*buf == (u8)(i % 63))
				continue;
			break;
		}
		ERROR (dev, "bad OUT byte, buf [%d] = %d\n", i, *buf);
		usb_ep_set_halt (ep);
		return -EINVAL;
	}
	return 0;
}

static void reinit_write_data(struct usb_ep *ep, struct usb_request *req)
{
	unsigned	i;
	u8		*buf = req->buf;

	switch (pattern) {
	case 0:
		memset (req->buf, 0, req->length);
		break;
	case 1:
		for  (i = 0; i < req->length; i++)
			*buf++ = (u8) (i % 63);
		break;
	}
}

/* if there is only one request in the queue, there'll always be an
 * irq delay between end of one request and start of the next.
 * that prevents using hardware dma queues.
 */
static void source_sink_complete (struct usb_ep *ep, struct usb_request *req)
{
	struct zero_dev	*dev = ep->driver_data;
	int		status = req->status;

	switch (status) {

	case 0:				/* normal completion? */
		if (ep == dev->out_ep) {
			check_read_data (dev, ep, req);
			memset (req->buf, 0x55, req->length);
		} else
			reinit_write_data(ep, req);
		break;

	/* this endpoint is normally active while we're configured */
	case -ECONNABORTED:		/* hardware forced ep reset */
	case -ECONNRESET:		/* request dequeued */
	case -ESHUTDOWN:		/* disconnect from host */
		VDBG (dev, "%s gone (%d), %d/%d\n", ep->name, status,
				req->actual, req->length);
		if (ep == dev->out_ep)
			check_read_data (dev, ep, req);
		free_ep_req (ep, req);
		return;

	case -EOVERFLOW:		/* buffer overrun on read means that
					 * we didn't provide a big enough
					 * buffer.
					 */
	default:
#if 1
		DBG (dev, "%s complete --> %d, %d/%d\n", ep->name,
				status, req->actual, req->length);
#endif
	case -EREMOTEIO:		/* short read */
		break;
	}

	status = usb_ep_queue (ep, req, GFP_ATOMIC);
	if (status) {
		ERROR (dev, "kill %s:  resubmit %d bytes --> %d\n",
				ep->name, req->length, status);
		usb_ep_set_halt (ep);
		/* FIXME recover later ... somehow */
	}
}

static struct usb_request *source_sink_start_ep(struct usb_ep *ep)
{
	struct usb_request	*req;
	int			status;

	req = alloc_ep_req (ep, buflen);
	if (!req)
		return NULL;

	memset (req->buf, 0, req->length);
	req->complete = source_sink_complete;

	if (strcmp (ep->name, EP_IN_NAME) == 0)
		reinit_write_data(ep, req);
	else
		memset (req->buf, 0x55, req->length);

	status = usb_ep_queue(ep, req, GFP_ATOMIC);
	if (status) {
		struct zero_dev	*dev = ep->driver_data;

		ERROR (dev, "start %s --> %d\n", ep->name, status);
		free_ep_req (ep, req);
		req = NULL;
	}

	return req;
}

static int set_source_sink_config(struct zero_dev *dev)
{
	int			result = 0;
	struct usb_ep		*ep;
	struct usb_gadget	*gadget = dev->gadget;

	gadget_for_each_ep (ep, gadget) {
		const struct usb_endpoint_descriptor	*d;

		/* one endpoint writes (sources) zeroes in (to the host) */
		if (strcmp (ep->name, EP_IN_NAME) == 0) {
			d = ep_desc (gadget, &hs_source_desc, &fs_source_desc);
			result = usb_ep_enable (ep, d);
			if (result == 0) {
				ep->driver_data = dev;
				if (source_sink_start_ep(ep) != NULL) {
					dev->in_ep = ep;
					continue;
				}
				usb_ep_disable (ep);
				result = -EIO;
			}

		/* one endpoint reads (sinks) anything out (from the host) */
		} else if (strcmp (ep->name, EP_OUT_NAME) == 0) {
			d = ep_desc (gadget, &hs_sink_desc, &fs_sink_desc);
			result = usb_ep_enable (ep, d);
			if (result == 0) {
				ep->driver_data = dev;
				if (source_sink_start_ep(ep) != NULL) {
					dev->out_ep = ep;
					continue;
				}
				usb_ep_disable (ep);
				result = -EIO;
			}

		/* ignore any other endpoints */
		} else
			continue;

		/* stop on error */
		ERROR (dev, "can't start %s, result %d\n", ep->name, result);
		break;
	}
	if (result == 0)
		DBG (dev, "buflen %d\n", buflen);

	/* caller is responsible for cleanup on error */
	return result;
}

/*-------------------------------------------------------------------------*/

static void loopback_complete (struct usb_ep *ep, struct usb_request *req)
{
	struct zero_dev	*dev = ep->driver_data;
	int		status = req->status;

	switch (status) {

	case 0:				/* normal completion? */
		if (ep == dev->out_ep) {
			/* loop this OUT packet back IN to the host */
			req->zero = (req->actual < req->length);
			req->length = req->actual;
			status = usb_ep_queue (dev->in_ep, req, GFP_ATOMIC);
			if (status == 0)
				return;

			/* "should never get here" */
			ERROR (dev, "can't loop %s to %s: %d\n",
				ep->name, dev->in_ep->name,
				status);
		}

		/* queue the buffer for some later OUT packet */
		req->length = buflen;
		status = usb_ep_queue (dev->out_ep, req, GFP_ATOMIC);
		if (status == 0)
			return;

		/* "should never get here" */
		/* FALLTHROUGH */

	default:
		ERROR (dev, "%s loop complete --> %d, %d/%d\n", ep->name,
				status, req->actual, req->length);
		/* FALLTHROUGH */

	/* NOTE:  since this driver doesn't maintain an explicit record
	 * of requests it submitted (just maintains qlen count), we
	 * rely on the hardware driver to clean up on disconnect or
	 * endpoint disable.
	 */
	case -ECONNABORTED:		/* hardware forced ep reset */
	case -ECONNRESET:		/* request dequeued */
	case -ESHUTDOWN:		/* disconnect from host */
		free_ep_req (ep, req);
		return;
	}
}

static int set_loopback_config(struct zero_dev *dev)
{
	int			result = 0;
	struct usb_ep		*ep;
	struct usb_gadget	*gadget = dev->gadget;

	gadget_for_each_ep (ep, gadget) {
		const struct usb_endpoint_descriptor	*d;

		/* one endpoint writes data back IN to the host */
		if (strcmp (ep->name, EP_IN_NAME) == 0) {
			d = ep_desc (gadget, &hs_source_desc, &fs_source_desc);
			result = usb_ep_enable (ep, d);
			if (result == 0) {
				ep->driver_data = dev;
				dev->in_ep = ep;
				continue;
			}

		/* one endpoint just reads OUT packets */
		} else if (strcmp (ep->name, EP_OUT_NAME) == 0) {
			d = ep_desc (gadget, &hs_sink_desc, &fs_sink_desc);
			result = usb_ep_enable (ep, d);
			if (result == 0) {
				ep->driver_data = dev;
				dev->out_ep = ep;
				continue;
			}

		/* ignore any other endpoints */
		} else
			continue;

		/* stop on error */
		ERROR (dev, "can't enable %s, result %d\n", ep->name, result);
		break;
	}

	/* allocate a bunch of read buffers and queue them all at once.
	 * we buffer at most 'qlen' transfers; fewer if any need more
	 * than 'buflen' bytes each.
	 */
	if (result == 0) {
		struct usb_request	*req;
		unsigned		i;

		ep = dev->out_ep;
		for (i = 0; i < qlen && result == 0; i++) {
			req = alloc_ep_req (ep, buflen);
			if (req) {
				req->complete = loopback_complete;
				result = usb_ep_queue (ep, req, GFP_ATOMIC);
				if (result)
					DBG (dev, "%s queue req --> %d\n",
							ep->name, result);
			} else
				result = -ENOMEM;
		}
	}
	if (result == 0)
		DBG (dev, "qlen %d, buflen %d\n", qlen, buflen);

	/* caller is responsible for cleanup on error */
	return result;
}

/*-------------------------------------------------------------------------*/

static void zero_reset_config (struct zero_dev *dev)
{
	if (dev->config == 0)
		return;

	DBG (dev, "reset config\n");

	/* just disable endpoints, forcing completion of pending i/o.
	 * all our completion handlers free their requests in this case.
	 */
	if (dev->in_ep) {
		usb_ep_disable (dev->in_ep);
		dev->in_ep = NULL;
	}
	if (dev->out_ep) {
		usb_ep_disable (dev->out_ep);
		dev->out_ep = NULL;
	}
	dev->config = 0;
	del_timer (&dev->resume);
}

/* change our operational config.  this code must agree with the code
 * that returns config descriptors, and altsetting code.
 *
 * it's also responsible for power management interactions. some
 * configurations might not work with our current power sources.
 *
 * note that some device controller hardware will constrain what this
 * code can do, perhaps by disallowing more than one configuration or
 * by limiting configuration choices (like the pxa2xx).
 */
static int zero_set_config(struct zero_dev *dev, unsigned number)
{
	int			result = 0;
	struct usb_gadget	*gadget = dev->gadget;

	if (number == dev->config)
		return 0;

	if (gadget_is_sa1100 (gadget) && dev->config) {
		/* tx fifo is full, but we can't clear it...*/
		ERROR(dev, "can't change configurations\n");
		return -ESPIPE;
	}
	zero_reset_config (dev);

	switch (number) {
	case CONFIG_SOURCE_SINK:
		result = set_source_sink_config(dev);
		break;
	case CONFIG_LOOPBACK:
		result = set_loopback_config(dev);
		break;
	default:
		result = -EINVAL;
		/* FALL THROUGH */
	case 0:
		return result;
	}

	if (!result && (!dev->in_ep || !dev->out_ep))
		result = -ENODEV;
	if (result)
		zero_reset_config (dev);
	else {
		char *speed;

		switch (gadget->speed) {
		case USB_SPEED_LOW:	speed = "low"; break;
		case USB_SPEED_FULL:	speed = "full"; break;
		case USB_SPEED_HIGH:	speed = "high"; break;
		default:		speed = "?"; break;
		}

		dev->config = number;
		INFO (dev, "%s speed config #%d: %s\n", speed, number,
				(number == CONFIG_SOURCE_SINK)
					? source_sink : loopback);
	}
	return result;
}

/*-------------------------------------------------------------------------*/

static void zero_setup_complete (struct usb_ep *ep, struct usb_request *req)
{
	if (req->status || req->actual != req->length)
		DBG ((struct zero_dev *) ep->driver_data,
				"setup complete --> %d, %d/%d\n",
				req->status, req->actual, req->length);
}

/*
 * The setup() callback implements all the ep0 functionality that's
 * not handled lower down, in hardware or the hardware driver (like
 * device and endpoint feature flags, and their status).  It's all
 * housekeeping for the gadget function we're implementing.  Most of
 * the work is in config-specific setup.
 */
static int
zero_setup (struct usb_gadget *gadget, const struct usb_ctrlrequest *ctrl)
{
	struct zero_dev		*dev = get_gadget_data (gadget);
	struct usb_request	*req = dev->req;
	int			value = -EOPNOTSUPP;
	u16			w_index = le16_to_cpu(ctrl->wIndex);
	u16			w_value = le16_to_cpu(ctrl->wValue);
	u16			w_length = le16_to_cpu(ctrl->wLength);

	/* usually this stores reply data in the pre-allocated ep0 buffer,
	 * but config change events will reconfigure hardware.
	 */
	req->zero = 0;
	switch (ctrl->bRequest) {

	case USB_REQ_GET_DESCRIPTOR:
		if (ctrl->bRequestType != USB_DIR_IN)
			goto unknown;
		switch (w_value >> 8) {

		case USB_DT_DEVICE:
			value = min (w_length, (u16) sizeof device_desc);
			memcpy (req->buf, &device_desc, value);
			break;
		case USB_DT_DEVICE_QUALIFIER:
			if (!gadget_is_dualspeed(gadget))
				break;
			value = min (w_length, (u16) sizeof dev_qualifier);
			memcpy (req->buf, &dev_qualifier, value);
			break;

		case USB_DT_OTHER_SPEED_CONFIG:
			if (!gadget_is_dualspeed(gadget))
				break;
			// FALLTHROUGH
		case USB_DT_CONFIG:
			value = config_buf (gadget, req->buf,
					w_value >> 8,
					w_value & 0xff);
			if (value >= 0)
				value = min (w_length, (u16) value);
			break;

		case USB_DT_STRING:
			/* wIndex == language code.
			 * this driver only handles one language, you can
			 * add string tables for other languages, using
			 * any UTF-8 characters
			 */
			value = usb_gadget_get_string (&stringtab,
					w_value & 0xff, req->buf);
			if (value >= 0)
				value = min (w_length, (u16) value);
			break;
		}
		break;

	/* currently two configs, two speeds */
	case USB_REQ_SET_CONFIGURATION:
		if (ctrl->bRequestType != 0)
			goto unknown;
		if (gadget->a_hnp_support)
			DBG (dev, "HNP available\n");
		else if (gadget->a_alt_hnp_support)
			DBG (dev, "HNP needs a different root port\n");
		else
			VDBG (dev, "HNP inactive\n");
		spin_lock (&dev->lock);
		value = zero_set_config(dev, w_value);
		spin_unlock (&dev->lock);
		break;
	case USB_REQ_GET_CONFIGURATION:
		if (ctrl->bRequestType != USB_DIR_IN)
			goto unknown;
		*(u8 *)req->buf = dev->config;
		value = min (w_length, (u16) 1);
		break;

	/* until we add altsetting support, or other interfaces,
	 * only 0/0 are possible.  pxa2xx only supports 0/0 (poorly)
	 * and already killed pending endpoint I/O.
	 */
	case USB_REQ_SET_INTERFACE:
		if (ctrl->bRequestType != USB_RECIP_INTERFACE)
			goto unknown;
		spin_lock (&dev->lock);
		if (dev->config && w_index == 0 && w_value == 0) {
			u8		config = dev->config;

			/* resets interface configuration, forgets about
			 * previous transaction state (queued bufs, etc)
			 * and re-inits endpoint state (toggle etc)
			 * no response queued, just zero status == success.
			 * if we had more than one interface we couldn't
			 * use this "reset the config" shortcut.
			 */
			zero_reset_config (dev);
			zero_set_config(dev, config);
			value = 0;
		}
		spin_unlock (&dev->lock);
		break;
	case USB_REQ_GET_INTERFACE:
		if (ctrl->bRequestType != (USB_DIR_IN|USB_RECIP_INTERFACE))
			goto unknown;
		if (!dev->config)
			break;
		if (w_index != 0) {
			value = -EDOM;
			break;
		}
		*(u8 *)req->buf = 0;
		value = min (w_length, (u16) 1);
		break;

	/*
	 * These are the same vendor-specific requests supported by
	 * Intel's USB 2.0 compliance test devices.  We exceed that
	 * device spec by allowing multiple-packet requests.
	 */
	case 0x5b:	/* control WRITE test -- fill the buffer */
		if (ctrl->bRequestType != (USB_DIR_OUT|USB_TYPE_VENDOR))
			goto unknown;
		if (w_value || w_index)
			break;
		/* just read that many bytes into the buffer */
		if (w_length > USB_BUFSIZ)
			break;
		value = w_length;
		break;
	case 0x5c:	/* control READ test -- return the buffer */
		if (ctrl->bRequestType != (USB_DIR_IN|USB_TYPE_VENDOR))
			goto unknown;
		if (w_value || w_index)
			break;
		/* expect those bytes are still in the buffer; send back */
		if (w_length > USB_BUFSIZ
				|| w_length != req->length)
			break;
		value = w_length;
		break;

	default:
unknown:
		VDBG (dev,
			"unknown control req%02x.%02x v%04x i%04x l%d\n",
			ctrl->bRequestType, ctrl->bRequest,
			w_value, w_index, w_length);
	}

	/* respond with data transfer before status phase? */
	if (value >= 0) {
		req->length = value;
		req->zero = value < w_length;
		value = usb_ep_queue (gadget->ep0, req, GFP_ATOMIC);
		if (value < 0) {
			DBG (dev, "ep_queue --> %d\n", value);
			req->status = 0;
			zero_setup_complete (gadget->ep0, req);
		}
	}

	/* device either stalls (value < 0) or reports success */
	return value;
}

static void
zero_disconnect (struct usb_gadget *gadget)
{
	struct zero_dev		*dev = get_gadget_data (gadget);
	unsigned long		flags;

	spin_lock_irqsave (&dev->lock, flags);
	zero_reset_config (dev);

	/* a more significant application might have some non-usb
	 * activities to quiesce here, saving resources like power
	 * or pushing the notification up a network stack.
	 */
	spin_unlock_irqrestore (&dev->lock, flags);

	/* next we may get setup() calls to enumerate new connections;
	 * or an unbind() during shutdown (including removing module).
	 */
}

static void
zero_autoresume (unsigned long _dev)
{
	struct zero_dev	*dev = (struct zero_dev *) _dev;
	int		status;

	/* normally the host would be woken up for something
	 * more significant than just a timer firing...
	 */
	if (dev->gadget->speed != USB_SPEED_UNKNOWN) {
		status = usb_gadget_wakeup (dev->gadget);
		DBG (dev, "wakeup --> %d\n", status);
	}
}

/*-------------------------------------------------------------------------*/

static void /* __init_or_exit */
zero_unbind (struct usb_gadget *gadget)
{
	struct zero_dev		*dev = get_gadget_data (gadget);

	DBG (dev, "unbind\n");

	/* we've already been disconnected ... no i/o is active */
	if (dev->req) {
		dev->req->length = USB_BUFSIZ;
		free_ep_req (gadget->ep0, dev->req);
	}
	del_timer_sync (&dev->resume);
	kfree (dev);
	set_gadget_data (gadget, NULL);
}

static int __init
zero_bind (struct usb_gadget *gadget)
{
	struct zero_dev		*dev;
	struct usb_ep		*ep;
	int			gcnum;

	/* FIXME this can't yet work right with SH ... it has only
	 * one configuration, numbered one.
	 */
	if (gadget_is_sh(gadget))
		return -ENODEV;

	/* Bulk-only drivers like this one SHOULD be able to
	 * autoconfigure on any sane usb controller driver,
	 * but there may also be important quirks to address.
	 */
	usb_ep_autoconfig_reset (gadget);
	ep = usb_ep_autoconfig (gadget, &fs_source_desc);
	if (!ep) {
autoconf_fail:
		pr_err("%s: can't autoconfigure on %s\n",
			shortname, gadget->name);
		return -ENODEV;
	}
	EP_IN_NAME = ep->name;
	ep->driver_data = ep;	/* claim */

	ep = usb_ep_autoconfig (gadget, &fs_sink_desc);
	if (!ep)
		goto autoconf_fail;
	EP_OUT_NAME = ep->name;
	ep->driver_data = ep;	/* claim */

	gcnum = usb_gadget_controller_number (gadget);
	if (gcnum >= 0)
		device_desc.bcdDevice = cpu_to_le16 (0x0200 + gcnum);
	else {
		/* gadget zero is so simple (for now, no altsettings) that
		 * it SHOULD NOT have problems with bulk-capable hardware.
		 * so warn about unrcognized controllers, don't panic.
		 *
		 * things like configuration and altsetting numbering
		 * can need hardware-specific attention though.
		 */
		pr_warning("%s: controller '%s' not recognized\n",
			shortname, gadget->name);
		device_desc.bcdDevice = __constant_cpu_to_le16 (0x9999);
	}


	/* ok, we made sense of the hardware ... */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;
	spin_lock_init (&dev->lock);
	dev->gadget = gadget;
	set_gadget_data (gadget, dev);

	/* preallocate control response and buffer */
	dev->req = usb_ep_alloc_request (gadget->ep0, GFP_KERNEL);
	if (!dev->req)
		goto enomem;
	dev->req->buf = kmalloc(USB_BUFSIZ, GFP_KERNEL);
	if (!dev->req->buf)
		goto enomem;

	dev->req->complete = zero_setup_complete;

	device_desc.bMaxPacketSize0 = gadget->ep0->maxpacket;

	if (gadget_is_dualspeed(gadget)) {
		/* assume ep0 uses the same value for both speeds ... */
		dev_qualifier.bMaxPacketSize0 = device_desc.bMaxPacketSize0;

		/* and that all endpoints are dual-speed */
		hs_source_desc.bEndpointAddress =
				fs_source_desc.bEndpointAddress;
		hs_sink_desc.bEndpointAddress =
				fs_sink_desc.bEndpointAddress;
	}

	if (gadget_is_otg(gadget)) {
		otg_descriptor.bmAttributes |= USB_OTG_HNP,
		source_sink_config.bmAttributes |= USB_CONFIG_ATT_WAKEUP;
		loopback_config.bmAttributes |= USB_CONFIG_ATT_WAKEUP;
	}

	usb_gadget_set_selfpowered (gadget);

	init_timer (&dev->resume);
	dev->resume.function = zero_autoresume;
	dev->resume.data = (unsigned long) dev;
	if (autoresume) {
		source_sink_config.bmAttributes |= USB_CONFIG_ATT_WAKEUP;
		loopback_config.bmAttributes |= USB_CONFIG_ATT_WAKEUP;
	}

	gadget->ep0->driver_data = dev;

	INFO (dev, "%s, version: " DRIVER_VERSION "\n", longname);
	INFO (dev, "using %s, OUT %s IN %s\n", gadget->name,
		EP_OUT_NAME, EP_IN_NAME);

	snprintf (manufacturer, sizeof manufacturer, "%s %s with %s",
		init_utsname()->sysname, init_utsname()->release,
		gadget->name);

	return 0;

enomem:
	zero_unbind (gadget);
	return -ENOMEM;
}

/*-------------------------------------------------------------------------*/

static void
zero_suspend (struct usb_gadget *gadget)
{
	struct zero_dev		*dev = get_gadget_data (gadget);

	if (gadget->speed == USB_SPEED_UNKNOWN)
		return;

	if (autoresume) {
		mod_timer (&dev->resume, jiffies + (HZ * autoresume));
		DBG (dev, "suspend, wakeup in %d seconds\n", autoresume);
	} else
		DBG (dev, "suspend\n");
}

static void
zero_resume (struct usb_gadget *gadget)
{
	struct zero_dev		*dev = get_gadget_data (gadget);

	DBG (dev, "resume\n");
	del_timer (&dev->resume);
}


/*-------------------------------------------------------------------------*/

static struct usb_gadget_driver zero_driver = {
#ifdef CONFIG_USB_GADGET_DUALSPEED
	.speed		= USB_SPEED_HIGH,
#else
	.speed		= USB_SPEED_FULL,
#endif
	.function	= (char *) longname,
	.bind		= zero_bind,
	.unbind		= __exit_p(zero_unbind),

	.setup		= zero_setup,
	.disconnect	= zero_disconnect,

	.suspend	= zero_suspend,
	.resume		= zero_resume,

	.driver		= {
		.name		= (char *) shortname,
		.owner		= THIS_MODULE,
	},
};

MODULE_AUTHOR("David Brownell");
MODULE_LICENSE("GPL");


static int __init init (void)
{
	return usb_gadget_register_driver (&zero_driver);
}
module_init (init);

static void __exit cleanup (void)
{
	usb_gadget_unregister_driver (&zero_driver);
}
module_exit (cleanup);

