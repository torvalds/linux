/*
 * f_sourcesink.c - USB peripheral source/sink configuration driver
 *
 * Copyright (C) 2003-2008 David Brownell
 * Copyright (C) 2008 by Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* #define VERBOSE_DEBUG */

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/usb/composite.h>
#include <linux/err.h>

#include "g_zero.h"
#include "gadget_chips.h"
#include "u_f.h"

/*
 * SOURCE/SINK FUNCTION ... a primary testing vehicle for USB peripheral
 * controller drivers.
 *
 * This just sinks bulk packets OUT to the peripheral and sources them IN
 * to the host, optionally with specific data patterns for integrity tests.
 * As such it supports basic functionality and load tests.
 *
 * In terms of control messaging, this supports all the standard requests
 * plus two that support control-OUT tests.  If the optional "autoresume"
 * mode is enabled, it provides good functional coverage for the "USBCV"
 * test harness from USB-IF.
 *
 * Note that because this doesn't queue more than one request at a time,
 * some other function must be used to test queueing logic.  The network
 * link (g_ether) is the best overall option for that, since its TX and RX
 * queues are relatively independent, will receive a range of packet sizes,
 * and can often be made to run out completely.  Those issues are important
 * when stress testing peripheral controller drivers.
 *
 *
 * This is currently packaged as a configuration driver, which can't be
 * combined with other functions to make composite devices.  However, it
 * can be combined with other independent configurations.
 */
struct f_sourcesink {
	struct usb_function	function;

	struct usb_ep		*in_ep;
	struct usb_ep		*out_ep;
	struct usb_ep		*iso_in_ep;
	struct usb_ep		*iso_out_ep;
	int			cur_alt;
};

static inline struct f_sourcesink *func_to_ss(struct usb_function *f)
{
	return container_of(f, struct f_sourcesink, function);
}

static unsigned pattern;
static unsigned isoc_interval;
static unsigned isoc_maxpacket;
static unsigned isoc_mult;
static unsigned isoc_maxburst;
static unsigned buflen;

/*-------------------------------------------------------------------------*/

static struct usb_interface_descriptor source_sink_intf_alt0 = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,

	.bAlternateSetting =	0,
	.bNumEndpoints =	2,
	.bInterfaceClass =	USB_CLASS_VENDOR_SPEC,
	/* .iInterface		= DYNAMIC */
};

static struct usb_interface_descriptor source_sink_intf_alt1 = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,

	.bAlternateSetting =	1,
	.bNumEndpoints =	4,
	.bInterfaceClass =	USB_CLASS_VENDOR_SPEC,
	/* .iInterface		= DYNAMIC */
};

/* full speed support: */

static struct usb_endpoint_descriptor fs_source_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_endpoint_descriptor fs_sink_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_endpoint_descriptor fs_iso_source_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_ISOC,
	.wMaxPacketSize =	cpu_to_le16(1023),
	.bInterval =		4,
};

static struct usb_endpoint_descriptor fs_iso_sink_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_ISOC,
	.wMaxPacketSize =	cpu_to_le16(1023),
	.bInterval =		4,
};

static struct usb_descriptor_header *fs_source_sink_descs[] = {
	(struct usb_descriptor_header *) &source_sink_intf_alt0,
	(struct usb_descriptor_header *) &fs_sink_desc,
	(struct usb_descriptor_header *) &fs_source_desc,
	(struct usb_descriptor_header *) &source_sink_intf_alt1,
#define FS_ALT_IFC_1_OFFSET	3
	(struct usb_descriptor_header *) &fs_sink_desc,
	(struct usb_descriptor_header *) &fs_source_desc,
	(struct usb_descriptor_header *) &fs_iso_sink_desc,
	(struct usb_descriptor_header *) &fs_iso_source_desc,
	NULL,
};

/* high speed support: */

static struct usb_endpoint_descriptor hs_source_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(512),
};

static struct usb_endpoint_descriptor hs_sink_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(512),
};

static struct usb_endpoint_descriptor hs_iso_source_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_ISOC,
	.wMaxPacketSize =	cpu_to_le16(1024),
	.bInterval =		4,
};

static struct usb_endpoint_descriptor hs_iso_sink_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_ISOC,
	.wMaxPacketSize =	cpu_to_le16(1024),
	.bInterval =		4,
};

static struct usb_descriptor_header *hs_source_sink_descs[] = {
	(struct usb_descriptor_header *) &source_sink_intf_alt0,
	(struct usb_descriptor_header *) &hs_source_desc,
	(struct usb_descriptor_header *) &hs_sink_desc,
	(struct usb_descriptor_header *) &source_sink_intf_alt1,
#define HS_ALT_IFC_1_OFFSET	3
	(struct usb_descriptor_header *) &hs_source_desc,
	(struct usb_descriptor_header *) &hs_sink_desc,
	(struct usb_descriptor_header *) &hs_iso_source_desc,
	(struct usb_descriptor_header *) &hs_iso_sink_desc,
	NULL,
};

/* super speed support: */

static struct usb_endpoint_descriptor ss_source_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor ss_source_comp_desc = {
	.bLength =		USB_DT_SS_EP_COMP_SIZE,
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,

	.bMaxBurst =		0,
	.bmAttributes =		0,
	.wBytesPerInterval =	0,
};

static struct usb_endpoint_descriptor ss_sink_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor ss_sink_comp_desc = {
	.bLength =		USB_DT_SS_EP_COMP_SIZE,
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,

	.bMaxBurst =		0,
	.bmAttributes =		0,
	.wBytesPerInterval =	0,
};

static struct usb_endpoint_descriptor ss_iso_source_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_ISOC,
	.wMaxPacketSize =	cpu_to_le16(1024),
	.bInterval =		4,
};

static struct usb_ss_ep_comp_descriptor ss_iso_source_comp_desc = {
	.bLength =		USB_DT_SS_EP_COMP_SIZE,
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,

	.bMaxBurst =		0,
	.bmAttributes =		0,
	.wBytesPerInterval =	cpu_to_le16(1024),
};

static struct usb_endpoint_descriptor ss_iso_sink_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_ISOC,
	.wMaxPacketSize =	cpu_to_le16(1024),
	.bInterval =		4,
};

static struct usb_ss_ep_comp_descriptor ss_iso_sink_comp_desc = {
	.bLength =		USB_DT_SS_EP_COMP_SIZE,
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,

	.bMaxBurst =		0,
	.bmAttributes =		0,
	.wBytesPerInterval =	cpu_to_le16(1024),
};

static struct usb_descriptor_header *ss_source_sink_descs[] = {
	(struct usb_descriptor_header *) &source_sink_intf_alt0,
	(struct usb_descriptor_header *) &ss_source_desc,
	(struct usb_descriptor_header *) &ss_source_comp_desc,
	(struct usb_descriptor_header *) &ss_sink_desc,
	(struct usb_descriptor_header *) &ss_sink_comp_desc,
	(struct usb_descriptor_header *) &source_sink_intf_alt1,
#define SS_ALT_IFC_1_OFFSET	5
	(struct usb_descriptor_header *) &ss_source_desc,
	(struct usb_descriptor_header *) &ss_source_comp_desc,
	(struct usb_descriptor_header *) &ss_sink_desc,
	(struct usb_descriptor_header *) &ss_sink_comp_desc,
	(struct usb_descriptor_header *) &ss_iso_source_desc,
	(struct usb_descriptor_header *) &ss_iso_source_comp_desc,
	(struct usb_descriptor_header *) &ss_iso_sink_desc,
	(struct usb_descriptor_header *) &ss_iso_sink_comp_desc,
	NULL,
};

/* function-specific strings: */

static struct usb_string strings_sourcesink[] = {
	[0].s = "source and sink data",
	{  }			/* end of list */
};

static struct usb_gadget_strings stringtab_sourcesink = {
	.language	= 0x0409,	/* en-us */
	.strings	= strings_sourcesink,
};

static struct usb_gadget_strings *sourcesink_strings[] = {
	&stringtab_sourcesink,
	NULL,
};

/*-------------------------------------------------------------------------*/

static inline struct usb_request *ss_alloc_ep_req(struct usb_ep *ep, int len)
{
	return alloc_ep_req(ep, len, buflen);
}

void free_ep_req(struct usb_ep *ep, struct usb_request *req)
{
	kfree(req->buf);
	usb_ep_free_request(ep, req);
}

static void disable_ep(struct usb_composite_dev *cdev, struct usb_ep *ep)
{
	int			value;

	if (ep->driver_data) {
		value = usb_ep_disable(ep);
		if (value < 0)
			DBG(cdev, "disable %s --> %d\n",
					ep->name, value);
		ep->driver_data = NULL;
	}
}

void disable_endpoints(struct usb_composite_dev *cdev,
		struct usb_ep *in, struct usb_ep *out,
		struct usb_ep *iso_in, struct usb_ep *iso_out)
{
	disable_ep(cdev, in);
	disable_ep(cdev, out);
	if (iso_in)
		disable_ep(cdev, iso_in);
	if (iso_out)
		disable_ep(cdev, iso_out);
}

static int
sourcesink_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_composite_dev *cdev = c->cdev;
	struct f_sourcesink	*ss = func_to_ss(f);
	int	id;
	int ret;

	/* allocate interface ID(s) */
	id = usb_interface_id(c, f);
	if (id < 0)
		return id;
	source_sink_intf_alt0.bInterfaceNumber = id;
	source_sink_intf_alt1.bInterfaceNumber = id;

	/* allocate bulk endpoints */
	ss->in_ep = usb_ep_autoconfig(cdev->gadget, &fs_source_desc);
	if (!ss->in_ep) {
autoconf_fail:
		ERROR(cdev, "%s: can't autoconfigure on %s\n",
			f->name, cdev->gadget->name);
		return -ENODEV;
	}
	ss->in_ep->driver_data = cdev;	/* claim */

	ss->out_ep = usb_ep_autoconfig(cdev->gadget, &fs_sink_desc);
	if (!ss->out_ep)
		goto autoconf_fail;
	ss->out_ep->driver_data = cdev;	/* claim */

	/* sanity check the isoc module parameters */
	if (isoc_interval < 1)
		isoc_interval = 1;
	if (isoc_interval > 16)
		isoc_interval = 16;
	if (isoc_mult > 2)
		isoc_mult = 2;
	if (isoc_maxburst > 15)
		isoc_maxburst = 15;

	/* fill in the FS isoc descriptors from the module parameters */
	fs_iso_source_desc.wMaxPacketSize = isoc_maxpacket > 1023 ?
						1023 : isoc_maxpacket;
	fs_iso_source_desc.bInterval = isoc_interval;
	fs_iso_sink_desc.wMaxPacketSize = isoc_maxpacket > 1023 ?
						1023 : isoc_maxpacket;
	fs_iso_sink_desc.bInterval = isoc_interval;

	/* allocate iso endpoints */
	ss->iso_in_ep = usb_ep_autoconfig(cdev->gadget, &fs_iso_source_desc);
	if (!ss->iso_in_ep)
		goto no_iso;
	ss->iso_in_ep->driver_data = cdev;	/* claim */

	ss->iso_out_ep = usb_ep_autoconfig(cdev->gadget, &fs_iso_sink_desc);
	if (ss->iso_out_ep) {
		ss->iso_out_ep->driver_data = cdev;	/* claim */
	} else {
		ss->iso_in_ep->driver_data = NULL;
		ss->iso_in_ep = NULL;
no_iso:
		/*
		 * We still want to work even if the UDC doesn't have isoc
		 * endpoints, so null out the alt interface that contains
		 * them and continue.
		 */
		fs_source_sink_descs[FS_ALT_IFC_1_OFFSET] = NULL;
		hs_source_sink_descs[HS_ALT_IFC_1_OFFSET] = NULL;
		ss_source_sink_descs[SS_ALT_IFC_1_OFFSET] = NULL;
	}

	if (isoc_maxpacket > 1024)
		isoc_maxpacket = 1024;

	/* support high speed hardware */
	hs_source_desc.bEndpointAddress = fs_source_desc.bEndpointAddress;
	hs_sink_desc.bEndpointAddress = fs_sink_desc.bEndpointAddress;

	/*
	 * Fill in the HS isoc descriptors from the module parameters.
	 * We assume that the user knows what they are doing and won't
	 * give parameters that their UDC doesn't support.
	 */
	hs_iso_source_desc.wMaxPacketSize = isoc_maxpacket;
	hs_iso_source_desc.wMaxPacketSize |= isoc_mult << 11;
	hs_iso_source_desc.bInterval = isoc_interval;
	hs_iso_source_desc.bEndpointAddress =
		fs_iso_source_desc.bEndpointAddress;

	hs_iso_sink_desc.wMaxPacketSize = isoc_maxpacket;
	hs_iso_sink_desc.wMaxPacketSize |= isoc_mult << 11;
	hs_iso_sink_desc.bInterval = isoc_interval;
	hs_iso_sink_desc.bEndpointAddress = fs_iso_sink_desc.bEndpointAddress;

	/* support super speed hardware */
	ss_source_desc.bEndpointAddress =
		fs_source_desc.bEndpointAddress;
	ss_sink_desc.bEndpointAddress =
		fs_sink_desc.bEndpointAddress;

	/*
	 * Fill in the SS isoc descriptors from the module parameters.
	 * We assume that the user knows what they are doing and won't
	 * give parameters that their UDC doesn't support.
	 */
	ss_iso_source_desc.wMaxPacketSize = isoc_maxpacket;
	ss_iso_source_desc.bInterval = isoc_interval;
	ss_iso_source_comp_desc.bmAttributes = isoc_mult;
	ss_iso_source_comp_desc.bMaxBurst = isoc_maxburst;
	ss_iso_source_comp_desc.wBytesPerInterval =
		isoc_maxpacket * (isoc_mult + 1) * (isoc_maxburst + 1);
	ss_iso_source_desc.bEndpointAddress =
		fs_iso_source_desc.bEndpointAddress;

	ss_iso_sink_desc.wMaxPacketSize = isoc_maxpacket;
	ss_iso_sink_desc.bInterval = isoc_interval;
	ss_iso_sink_comp_desc.bmAttributes = isoc_mult;
	ss_iso_sink_comp_desc.bMaxBurst = isoc_maxburst;
	ss_iso_sink_comp_desc.wBytesPerInterval =
		isoc_maxpacket * (isoc_mult + 1) * (isoc_maxburst + 1);
	ss_iso_sink_desc.bEndpointAddress = fs_iso_sink_desc.bEndpointAddress;

	ret = usb_assign_descriptors(f, fs_source_sink_descs,
			hs_source_sink_descs, ss_source_sink_descs);
	if (ret)
		return ret;

	DBG(cdev, "%s speed %s: IN/%s, OUT/%s, ISO-IN/%s, ISO-OUT/%s\n",
	    (gadget_is_superspeed(c->cdev->gadget) ? "super" :
	     (gadget_is_dualspeed(c->cdev->gadget) ? "dual" : "full")),
			f->name, ss->in_ep->name, ss->out_ep->name,
			ss->iso_in_ep ? ss->iso_in_ep->name : "<none>",
			ss->iso_out_ep ? ss->iso_out_ep->name : "<none>");
	return 0;
}

static void
sourcesink_free_func(struct usb_function *f)
{
	struct f_ss_opts *opts;

	opts = container_of(f->fi, struct f_ss_opts, func_inst);

	mutex_lock(&opts->lock);
	opts->refcnt--;
	mutex_unlock(&opts->lock);

	usb_free_all_descriptors(f);
	kfree(func_to_ss(f));
}

/* optionally require specific source/sink data patterns  */
static int check_read_data(struct f_sourcesink *ss, struct usb_request *req)
{
	unsigned		i;
	u8			*buf = req->buf;
	struct usb_composite_dev *cdev = ss->function.config->cdev;

	if (pattern == 2)
		return 0;

	for (i = 0; i < req->actual; i++, buf++) {
		switch (pattern) {

		/* all-zeroes has no synchronization issues */
		case 0:
			if (*buf == 0)
				continue;
			break;

		/* "mod63" stays in sync with short-terminated transfers,
		 * OR otherwise when host and gadget agree on how large
		 * each usb transfer request should be.  Resync is done
		 * with set_interface or set_config.  (We *WANT* it to
		 * get quickly out of sync if controllers or their drivers
		 * stutter for any reason, including buffer duplication...)
		 */
		case 1:
			if (*buf == (u8)(i % 63))
				continue;
			break;
		}
		ERROR(cdev, "bad OUT byte, buf[%d] = %d\n", i, *buf);
		usb_ep_set_halt(ss->out_ep);
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
		memset(req->buf, 0, req->length);
		break;
	case 1:
		for  (i = 0; i < req->length; i++)
			*buf++ = (u8) (i % 63);
		break;
	case 2:
		break;
	}
}

static void source_sink_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct usb_composite_dev	*cdev;
	struct f_sourcesink		*ss = ep->driver_data;
	int				status = req->status;

	/* driver_data will be null if ep has been disabled */
	if (!ss)
		return;

	cdev = ss->function.config->cdev;

	switch (status) {

	case 0:				/* normal completion? */
		if (ep == ss->out_ep) {
			check_read_data(ss, req);
			if (pattern != 2)
				memset(req->buf, 0x55, req->length);
		}
		break;

	/* this endpoint is normally active while we're configured */
	case -ECONNABORTED:		/* hardware forced ep reset */
	case -ECONNRESET:		/* request dequeued */
	case -ESHUTDOWN:		/* disconnect from host */
		VDBG(cdev, "%s gone (%d), %d/%d\n", ep->name, status,
				req->actual, req->length);
		if (ep == ss->out_ep)
			check_read_data(ss, req);
		free_ep_req(ep, req);
		return;

	case -EOVERFLOW:		/* buffer overrun on read means that
					 * we didn't provide a big enough
					 * buffer.
					 */
	default:
#if 1
		DBG(cdev, "%s complete --> %d, %d/%d\n", ep->name,
				status, req->actual, req->length);
#endif
	case -EREMOTEIO:		/* short read */
		break;
	}

	status = usb_ep_queue(ep, req, GFP_ATOMIC);
	if (status) {
		ERROR(cdev, "kill %s:  resubmit %d bytes --> %d\n",
				ep->name, req->length, status);
		usb_ep_set_halt(ep);
		/* FIXME recover later ... somehow */
	}
}

static int source_sink_start_ep(struct f_sourcesink *ss, bool is_in,
		bool is_iso, int speed)
{
	struct usb_ep		*ep;
	struct usb_request	*req;
	int			i, size, status;

	for (i = 0; i < 8; i++) {
		if (is_iso) {
			switch (speed) {
			case USB_SPEED_SUPER:
				size = isoc_maxpacket * (isoc_mult + 1) *
						(isoc_maxburst + 1);
				break;
			case USB_SPEED_HIGH:
				size = isoc_maxpacket * (isoc_mult + 1);
				break;
			default:
				size = isoc_maxpacket > 1023 ?
						1023 : isoc_maxpacket;
				break;
			}
			ep = is_in ? ss->iso_in_ep : ss->iso_out_ep;
			req = ss_alloc_ep_req(ep, size);
		} else {
			ep = is_in ? ss->in_ep : ss->out_ep;
			req = ss_alloc_ep_req(ep, 0);
		}

		if (!req)
			return -ENOMEM;

		req->complete = source_sink_complete;
		if (is_in)
			reinit_write_data(ep, req);
		else if (pattern != 2)
			memset(req->buf, 0x55, req->length);

		status = usb_ep_queue(ep, req, GFP_ATOMIC);
		if (status) {
			struct usb_composite_dev	*cdev;

			cdev = ss->function.config->cdev;
			ERROR(cdev, "start %s%s %s --> %d\n",
			      is_iso ? "ISO-" : "", is_in ? "IN" : "OUT",
			      ep->name, status);
			free_ep_req(ep, req);
		}

		if (!is_iso)
			break;
	}

	return status;
}

static void disable_source_sink(struct f_sourcesink *ss)
{
	struct usb_composite_dev	*cdev;

	cdev = ss->function.config->cdev;
	disable_endpoints(cdev, ss->in_ep, ss->out_ep, ss->iso_in_ep,
			ss->iso_out_ep);
	VDBG(cdev, "%s disabled\n", ss->function.name);
}

static int
enable_source_sink(struct usb_composite_dev *cdev, struct f_sourcesink *ss,
		int alt)
{
	int					result = 0;
	int					speed = cdev->gadget->speed;
	struct usb_ep				*ep;

	/* one bulk endpoint writes (sources) zeroes IN (to the host) */
	ep = ss->in_ep;
	result = config_ep_by_speed(cdev->gadget, &(ss->function), ep);
	if (result)
		return result;
	result = usb_ep_enable(ep);
	if (result < 0)
		return result;
	ep->driver_data = ss;

	result = source_sink_start_ep(ss, true, false, speed);
	if (result < 0) {
fail:
		ep = ss->in_ep;
		usb_ep_disable(ep);
		ep->driver_data = NULL;
		return result;
	}

	/* one bulk endpoint reads (sinks) anything OUT (from the host) */
	ep = ss->out_ep;
	result = config_ep_by_speed(cdev->gadget, &(ss->function), ep);
	if (result)
		goto fail;
	result = usb_ep_enable(ep);
	if (result < 0)
		goto fail;
	ep->driver_data = ss;

	result = source_sink_start_ep(ss, false, false, speed);
	if (result < 0) {
fail2:
		ep = ss->out_ep;
		usb_ep_disable(ep);
		ep->driver_data = NULL;
		goto fail;
	}

	if (alt == 0)
		goto out;

	/* one iso endpoint writes (sources) zeroes IN (to the host) */
	ep = ss->iso_in_ep;
	if (ep) {
		result = config_ep_by_speed(cdev->gadget, &(ss->function), ep);
		if (result)
			goto fail2;
		result = usb_ep_enable(ep);
		if (result < 0)
			goto fail2;
		ep->driver_data = ss;

		result = source_sink_start_ep(ss, true, true, speed);
		if (result < 0) {
fail3:
			ep = ss->iso_in_ep;
			if (ep) {
				usb_ep_disable(ep);
				ep->driver_data = NULL;
			}
			goto fail2;
		}
	}

	/* one iso endpoint reads (sinks) anything OUT (from the host) */
	ep = ss->iso_out_ep;
	if (ep) {
		result = config_ep_by_speed(cdev->gadget, &(ss->function), ep);
		if (result)
			goto fail3;
		result = usb_ep_enable(ep);
		if (result < 0)
			goto fail3;
		ep->driver_data = ss;

		result = source_sink_start_ep(ss, false, true, speed);
		if (result < 0) {
			usb_ep_disable(ep);
			ep->driver_data = NULL;
			goto fail3;
		}
	}
out:
	ss->cur_alt = alt;

	DBG(cdev, "%s enabled, alt intf %d\n", ss->function.name, alt);
	return result;
}

static int sourcesink_set_alt(struct usb_function *f,
		unsigned intf, unsigned alt)
{
	struct f_sourcesink		*ss = func_to_ss(f);
	struct usb_composite_dev	*cdev = f->config->cdev;

	if (ss->in_ep->driver_data)
		disable_source_sink(ss);
	return enable_source_sink(cdev, ss, alt);
}

static int sourcesink_get_alt(struct usb_function *f, unsigned intf)
{
	struct f_sourcesink		*ss = func_to_ss(f);

	return ss->cur_alt;
}

static void sourcesink_disable(struct usb_function *f)
{
	struct f_sourcesink	*ss = func_to_ss(f);

	disable_source_sink(ss);
}

/*-------------------------------------------------------------------------*/

static int sourcesink_setup(struct usb_function *f,
		const struct usb_ctrlrequest *ctrl)
{
	struct usb_configuration        *c = f->config;
	struct usb_request	*req = c->cdev->req;
	int			value = -EOPNOTSUPP;
	u16			w_index = le16_to_cpu(ctrl->wIndex);
	u16			w_value = le16_to_cpu(ctrl->wValue);
	u16			w_length = le16_to_cpu(ctrl->wLength);

	req->length = USB_COMP_EP0_BUFSIZ;

	/* composite driver infrastructure handles everything except
	 * the two control test requests.
	 */
	switch (ctrl->bRequest) {

	/*
	 * These are the same vendor-specific requests supported by
	 * Intel's USB 2.0 compliance test devices.  We exceed that
	 * device spec by allowing multiple-packet requests.
	 *
	 * NOTE:  the Control-OUT data stays in req->buf ... better
	 * would be copying it into a scratch buffer, so that other
	 * requests may safely intervene.
	 */
	case 0x5b:	/* control WRITE test -- fill the buffer */
		if (ctrl->bRequestType != (USB_DIR_OUT|USB_TYPE_VENDOR))
			goto unknown;
		if (w_value || w_index)
			break;
		/* just read that many bytes into the buffer */
		if (w_length > req->length)
			break;
		value = w_length;
		break;
	case 0x5c:	/* control READ test -- return the buffer */
		if (ctrl->bRequestType != (USB_DIR_IN|USB_TYPE_VENDOR))
			goto unknown;
		if (w_value || w_index)
			break;
		/* expect those bytes are still in the buffer; send back */
		if (w_length > req->length)
			break;
		value = w_length;
		break;

	default:
unknown:
		VDBG(c->cdev,
			"unknown control req%02x.%02x v%04x i%04x l%d\n",
			ctrl->bRequestType, ctrl->bRequest,
			w_value, w_index, w_length);
	}

	/* respond with data transfer or status phase? */
	if (value >= 0) {
		VDBG(c->cdev, "source/sink req%02x.%02x v%04x i%04x l%d\n",
			ctrl->bRequestType, ctrl->bRequest,
			w_value, w_index, w_length);
		req->zero = 0;
		req->length = value;
		value = usb_ep_queue(c->cdev->gadget->ep0, req, GFP_ATOMIC);
		if (value < 0)
			ERROR(c->cdev, "source/sink response, err %d\n",
					value);
	}

	/* device either stalls (value < 0) or reports success */
	return value;
}

static struct usb_function *source_sink_alloc_func(
		struct usb_function_instance *fi)
{
	struct f_sourcesink     *ss;
	struct f_ss_opts	*ss_opts;

	ss = kzalloc(sizeof(*ss), GFP_KERNEL);
	if (!ss)
		return NULL;

	ss_opts =  container_of(fi, struct f_ss_opts, func_inst);

	mutex_lock(&ss_opts->lock);
	ss_opts->refcnt++;
	mutex_unlock(&ss_opts->lock);

	pattern = ss_opts->pattern;
	isoc_interval = ss_opts->isoc_interval;
	isoc_maxpacket = ss_opts->isoc_maxpacket;
	isoc_mult = ss_opts->isoc_mult;
	isoc_maxburst = ss_opts->isoc_maxburst;
	buflen = ss_opts->bulk_buflen;

	ss->function.name = "source/sink";
	ss->function.bind = sourcesink_bind;
	ss->function.set_alt = sourcesink_set_alt;
	ss->function.get_alt = sourcesink_get_alt;
	ss->function.disable = sourcesink_disable;
	ss->function.setup = sourcesink_setup;
	ss->function.strings = sourcesink_strings;

	ss->function.free_func = sourcesink_free_func;

	return &ss->function;
}

static inline struct f_ss_opts *to_f_ss_opts(struct config_item *item)
{
	return container_of(to_config_group(item), struct f_ss_opts,
			    func_inst.group);
}

CONFIGFS_ATTR_STRUCT(f_ss_opts);
CONFIGFS_ATTR_OPS(f_ss_opts);

static void ss_attr_release(struct config_item *item)
{
	struct f_ss_opts *ss_opts = to_f_ss_opts(item);

	usb_put_function_instance(&ss_opts->func_inst);
}

static struct configfs_item_operations ss_item_ops = {
	.release		= ss_attr_release,
	.show_attribute		= f_ss_opts_attr_show,
	.store_attribute	= f_ss_opts_attr_store,
};

static ssize_t f_ss_opts_pattern_show(struct f_ss_opts *opts, char *page)
{
	int result;

	mutex_lock(&opts->lock);
	result = sprintf(page, "%u", opts->pattern);
	mutex_unlock(&opts->lock);

	return result;
}

static ssize_t f_ss_opts_pattern_store(struct f_ss_opts *opts,
				       const char *page, size_t len)
{
	int ret;
	u8 num;

	mutex_lock(&opts->lock);
	if (opts->refcnt) {
		ret = -EBUSY;
		goto end;
	}

	ret = kstrtou8(page, 0, &num);
	if (ret)
		goto end;

	if (num != 0 && num != 1 && num != 2) {
		ret = -EINVAL;
		goto end;
	}

	opts->pattern = num;
	ret = len;
end:
	mutex_unlock(&opts->lock);
	return ret;
}

static struct f_ss_opts_attribute f_ss_opts_pattern =
	__CONFIGFS_ATTR(pattern, S_IRUGO | S_IWUSR,
			f_ss_opts_pattern_show,
			f_ss_opts_pattern_store);

static ssize_t f_ss_opts_isoc_interval_show(struct f_ss_opts *opts, char *page)
{
	int result;

	mutex_lock(&opts->lock);
	result = sprintf(page, "%u", opts->isoc_interval);
	mutex_unlock(&opts->lock);

	return result;
}

static ssize_t f_ss_opts_isoc_interval_store(struct f_ss_opts *opts,
				       const char *page, size_t len)
{
	int ret;
	u8 num;

	mutex_lock(&opts->lock);
	if (opts->refcnt) {
		ret = -EBUSY;
		goto end;
	}

	ret = kstrtou8(page, 0, &num);
	if (ret)
		goto end;

	if (num > 16) {
		ret = -EINVAL;
		goto end;
	}

	opts->isoc_interval = num;
	ret = len;
end:
	mutex_unlock(&opts->lock);
	return ret;
}

static struct f_ss_opts_attribute f_ss_opts_isoc_interval =
	__CONFIGFS_ATTR(isoc_interval, S_IRUGO | S_IWUSR,
			f_ss_opts_isoc_interval_show,
			f_ss_opts_isoc_interval_store);

static ssize_t f_ss_opts_isoc_maxpacket_show(struct f_ss_opts *opts, char *page)
{
	int result;

	mutex_lock(&opts->lock);
	result = sprintf(page, "%u", opts->isoc_maxpacket);
	mutex_unlock(&opts->lock);

	return result;
}

static ssize_t f_ss_opts_isoc_maxpacket_store(struct f_ss_opts *opts,
				       const char *page, size_t len)
{
	int ret;
	u16 num;

	mutex_lock(&opts->lock);
	if (opts->refcnt) {
		ret = -EBUSY;
		goto end;
	}

	ret = kstrtou16(page, 0, &num);
	if (ret)
		goto end;

	if (num > 1024) {
		ret = -EINVAL;
		goto end;
	}

	opts->isoc_maxpacket = num;
	ret = len;
end:
	mutex_unlock(&opts->lock);
	return ret;
}

static struct f_ss_opts_attribute f_ss_opts_isoc_maxpacket =
	__CONFIGFS_ATTR(isoc_maxpacket, S_IRUGO | S_IWUSR,
			f_ss_opts_isoc_maxpacket_show,
			f_ss_opts_isoc_maxpacket_store);

static ssize_t f_ss_opts_isoc_mult_show(struct f_ss_opts *opts, char *page)
{
	int result;

	mutex_lock(&opts->lock);
	result = sprintf(page, "%u", opts->isoc_mult);
	mutex_unlock(&opts->lock);

	return result;
}

static ssize_t f_ss_opts_isoc_mult_store(struct f_ss_opts *opts,
				       const char *page, size_t len)
{
	int ret;
	u8 num;

	mutex_lock(&opts->lock);
	if (opts->refcnt) {
		ret = -EBUSY;
		goto end;
	}

	ret = kstrtou8(page, 0, &num);
	if (ret)
		goto end;

	if (num > 2) {
		ret = -EINVAL;
		goto end;
	}

	opts->isoc_mult = num;
	ret = len;
end:
	mutex_unlock(&opts->lock);
	return ret;
}

static struct f_ss_opts_attribute f_ss_opts_isoc_mult =
	__CONFIGFS_ATTR(isoc_mult, S_IRUGO | S_IWUSR,
			f_ss_opts_isoc_mult_show,
			f_ss_opts_isoc_mult_store);

static ssize_t f_ss_opts_isoc_maxburst_show(struct f_ss_opts *opts, char *page)
{
	int result;

	mutex_lock(&opts->lock);
	result = sprintf(page, "%u", opts->isoc_maxburst);
	mutex_unlock(&opts->lock);

	return result;
}

static ssize_t f_ss_opts_isoc_maxburst_store(struct f_ss_opts *opts,
				       const char *page, size_t len)
{
	int ret;
	u8 num;

	mutex_lock(&opts->lock);
	if (opts->refcnt) {
		ret = -EBUSY;
		goto end;
	}

	ret = kstrtou8(page, 0, &num);
	if (ret)
		goto end;

	if (num > 15) {
		ret = -EINVAL;
		goto end;
	}

	opts->isoc_maxburst = num;
	ret = len;
end:
	mutex_unlock(&opts->lock);
	return ret;
}

static struct f_ss_opts_attribute f_ss_opts_isoc_maxburst =
	__CONFIGFS_ATTR(isoc_maxburst, S_IRUGO | S_IWUSR,
			f_ss_opts_isoc_maxburst_show,
			f_ss_opts_isoc_maxburst_store);

static ssize_t f_ss_opts_bulk_buflen_show(struct f_ss_opts *opts, char *page)
{
	int result;

	mutex_lock(&opts->lock);
	result = sprintf(page, "%u", opts->bulk_buflen);
	mutex_unlock(&opts->lock);

	return result;
}

static ssize_t f_ss_opts_bulk_buflen_store(struct f_ss_opts *opts,
					   const char *page, size_t len)
{
	int ret;
	u32 num;

	mutex_lock(&opts->lock);
	if (opts->refcnt) {
		ret = -EBUSY;
		goto end;
	}

	ret = kstrtou32(page, 0, &num);
	if (ret)
		goto end;

	opts->bulk_buflen = num;
	ret = len;
end:
	mutex_unlock(&opts->lock);
	return ret;
}

static struct f_ss_opts_attribute f_ss_opts_bulk_buflen =
	__CONFIGFS_ATTR(buflen, S_IRUGO | S_IWUSR,
			f_ss_opts_bulk_buflen_show,
			f_ss_opts_bulk_buflen_store);

static struct configfs_attribute *ss_attrs[] = {
	&f_ss_opts_pattern.attr,
	&f_ss_opts_isoc_interval.attr,
	&f_ss_opts_isoc_maxpacket.attr,
	&f_ss_opts_isoc_mult.attr,
	&f_ss_opts_isoc_maxburst.attr,
	&f_ss_opts_bulk_buflen.attr,
	NULL,
};

static struct config_item_type ss_func_type = {
	.ct_item_ops    = &ss_item_ops,
	.ct_attrs	= ss_attrs,
	.ct_owner       = THIS_MODULE,
};

static void source_sink_free_instance(struct usb_function_instance *fi)
{
	struct f_ss_opts *ss_opts;

	ss_opts = container_of(fi, struct f_ss_opts, func_inst);
	kfree(ss_opts);
}

static struct usb_function_instance *source_sink_alloc_inst(void)
{
	struct f_ss_opts *ss_opts;

	ss_opts = kzalloc(sizeof(*ss_opts), GFP_KERNEL);
	if (!ss_opts)
		return ERR_PTR(-ENOMEM);
	mutex_init(&ss_opts->lock);
	ss_opts->func_inst.free_func_inst = source_sink_free_instance;
	ss_opts->isoc_interval = GZERO_ISOC_INTERVAL;
	ss_opts->isoc_maxpacket = GZERO_ISOC_MAXPACKET;
	ss_opts->bulk_buflen = GZERO_BULK_BUFLEN;

	config_group_init_type_name(&ss_opts->func_inst.group, "",
				    &ss_func_type);

	return &ss_opts->func_inst;
}
DECLARE_USB_FUNCTION(SourceSink, source_sink_alloc_inst,
		source_sink_alloc_func);

static int __init sslb_modinit(void)
{
	int ret;

	ret = usb_function_register(&SourceSinkusb_func);
	if (ret)
		return ret;
	ret = lb_modinit();
	if (ret)
		usb_function_unregister(&SourceSinkusb_func);
	return ret;
}
static void __exit sslb_modexit(void)
{
	usb_function_unregister(&SourceSinkusb_func);
	lb_modexit();
}
module_init(sslb_modinit);
module_exit(sslb_modexit);

MODULE_LICENSE("GPL");
