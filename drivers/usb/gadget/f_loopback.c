/*
 * f_loopback.c - USB peripheral loopback configuration driver
 *
 * Copyright (C) 2003-2008 David Brownell
 * Copyright (C) 2008 by Nokia Corporation
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

/* #define VERBOSE_DEBUG */

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/device.h>

#include "g_zero.h"
#include "gadget_chips.h"


/*
 * LOOPBACK FUNCTION ... a testing vehicle for USB peripherals,
 *
 * This takes messages of various sizes written OUT to a device, and loops
 * them back so they can be read IN from it.  It has been used by certain
 * test applications.  It supports limited testing of data queueing logic.
 *
 *
 * This is currently packaged as a configuration driver, which can't be
 * combined with other functions to make composite devices.  However, it
 * can be combined with other independent configurations.
 */
struct f_loopback {
	struct usb_function	function;

	struct usb_ep		*in_ep;
	struct usb_ep		*out_ep;
};

static inline struct f_loopback *func_to_loop(struct usb_function *f)
{
	return container_of(f, struct f_loopback, function);
}

static unsigned qlen = 32;
module_param(qlen, uint, 0);
MODULE_PARM_DESC(qlenn, "depth of loopback queue");

/*-------------------------------------------------------------------------*/

static struct usb_interface_descriptor loopback_intf = {
	.bLength =		sizeof loopback_intf,
	.bDescriptorType =	USB_DT_INTERFACE,

	.bNumEndpoints =	2,
	.bInterfaceClass =	USB_CLASS_VENDOR_SPEC,
	/* .iInterface = DYNAMIC */
};

/* full speed support: */

static struct usb_endpoint_descriptor fs_loop_source_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_endpoint_descriptor fs_loop_sink_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_descriptor_header *fs_loopback_descs[] = {
	(struct usb_descriptor_header *) &loopback_intf,
	(struct usb_descriptor_header *) &fs_loop_sink_desc,
	(struct usb_descriptor_header *) &fs_loop_source_desc,
	NULL,
};

/* high speed support: */

static struct usb_endpoint_descriptor hs_loop_source_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(512),
};

static struct usb_endpoint_descriptor hs_loop_sink_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(512),
};

static struct usb_descriptor_header *hs_loopback_descs[] = {
	(struct usb_descriptor_header *) &loopback_intf,
	(struct usb_descriptor_header *) &hs_loop_source_desc,
	(struct usb_descriptor_header *) &hs_loop_sink_desc,
	NULL,
};

/* function-specific strings: */

static struct usb_string strings_loopback[] = {
	[0].s = "loop input to output",
	{  }			/* end of list */
};

static struct usb_gadget_strings stringtab_loop = {
	.language	= 0x0409,	/* en-us */
	.strings	= strings_loopback,
};

static struct usb_gadget_strings *loopback_strings[] = {
	&stringtab_loop,
	NULL,
};

/*-------------------------------------------------------------------------*/

static int __init
loopback_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_composite_dev *cdev = c->cdev;
	struct f_loopback	*loop = func_to_loop(f);
	int			id;

	/* allocate interface ID(s) */
	id = usb_interface_id(c, f);
	if (id < 0)
		return id;
	loopback_intf.bInterfaceNumber = id;

	/* allocate endpoints */

	loop->in_ep = usb_ep_autoconfig(cdev->gadget, &fs_loop_source_desc);
	if (!loop->in_ep) {
autoconf_fail:
		ERROR(cdev, "%s: can't autoconfigure on %s\n",
			f->name, cdev->gadget->name);
		return -ENODEV;
	}
	loop->in_ep->driver_data = cdev;	/* claim */

	loop->out_ep = usb_ep_autoconfig(cdev->gadget, &fs_loop_sink_desc);
	if (!loop->out_ep)
		goto autoconf_fail;
	loop->out_ep->driver_data = cdev;	/* claim */

	/* support high speed hardware */
	if (gadget_is_dualspeed(c->cdev->gadget)) {
		hs_loop_source_desc.bEndpointAddress =
				fs_loop_source_desc.bEndpointAddress;
		hs_loop_sink_desc.bEndpointAddress =
				fs_loop_sink_desc.bEndpointAddress;
		f->hs_descriptors = hs_loopback_descs;
	}

	DBG(cdev, "%s speed %s: IN/%s, OUT/%s\n",
			gadget_is_dualspeed(c->cdev->gadget) ? "dual" : "full",
			f->name, loop->in_ep->name, loop->out_ep->name);
	return 0;
}

static void
loopback_unbind(struct usb_configuration *c, struct usb_function *f)
{
	kfree(func_to_loop(f));
}

static void loopback_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct f_loopback	*loop = ep->driver_data;
	struct usb_composite_dev *cdev = loop->function.config->cdev;
	int			status = req->status;

	switch (status) {

	case 0:				/* normal completion? */
		if (ep == loop->out_ep) {
			/* loop this OUT packet back IN to the host */
			req->zero = (req->actual < req->length);
			req->length = req->actual;
			status = usb_ep_queue(loop->in_ep, req, GFP_ATOMIC);
			if (status == 0)
				return;

			/* "should never get here" */
			ERROR(cdev, "can't loop %s to %s: %d\n",
				ep->name, loop->in_ep->name,
				status);
		}

		/* queue the buffer for some later OUT packet */
		req->length = buflen;
		status = usb_ep_queue(loop->out_ep, req, GFP_ATOMIC);
		if (status == 0)
			return;

		/* "should never get here" */
		/* FALLTHROUGH */

	default:
		ERROR(cdev, "%s loop complete --> %d, %d/%d\n", ep->name,
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
		free_ep_req(ep, req);
		return;
	}
}

static void disable_loopback(struct f_loopback *loop)
{
	struct usb_composite_dev	*cdev;

	cdev = loop->function.config->cdev;
	disable_endpoints(cdev, loop->in_ep, loop->out_ep);
	VDBG(cdev, "%s disabled\n", loop->function.name);
}

static int
enable_loopback(struct usb_composite_dev *cdev, struct f_loopback *loop)
{
	int					result = 0;
	const struct usb_endpoint_descriptor	*src, *sink;
	struct usb_ep				*ep;
	struct usb_request			*req;
	unsigned				i;

	src = ep_choose(cdev->gadget,
			&hs_loop_source_desc, &fs_loop_source_desc);
	sink = ep_choose(cdev->gadget,
			&hs_loop_sink_desc, &fs_loop_sink_desc);

	/* one endpoint writes data back IN to the host */
	ep = loop->in_ep;
	result = usb_ep_enable(ep, src);
	if (result < 0)
		return result;
	ep->driver_data = loop;

	/* one endpoint just reads OUT packets */
	ep = loop->out_ep;
	result = usb_ep_enable(ep, sink);
	if (result < 0) {
fail0:
		ep = loop->in_ep;
		usb_ep_disable(ep);
		ep->driver_data = NULL;
		return result;
	}
	ep->driver_data = loop;

	/* allocate a bunch of read buffers and queue them all at once.
	 * we buffer at most 'qlen' transfers; fewer if any need more
	 * than 'buflen' bytes each.
	 */
	for (i = 0; i < qlen && result == 0; i++) {
		req = alloc_ep_req(ep);
		if (req) {
			req->complete = loopback_complete;
			result = usb_ep_queue(ep, req, GFP_ATOMIC);
			if (result)
				ERROR(cdev, "%s queue req --> %d\n",
						ep->name, result);
		} else {
			usb_ep_disable(ep);
			ep->driver_data = NULL;
			result = -ENOMEM;
			goto fail0;
		}
	}

	DBG(cdev, "%s enabled\n", loop->function.name);
	return result;
}

static int loopback_set_alt(struct usb_function *f,
		unsigned intf, unsigned alt)
{
	struct f_loopback	*loop = func_to_loop(f);
	struct usb_composite_dev *cdev = f->config->cdev;

	/* we know alt is zero */
	if (loop->in_ep->driver_data)
		disable_loopback(loop);
	return enable_loopback(cdev, loop);
}

static void loopback_disable(struct usb_function *f)
{
	struct f_loopback	*loop = func_to_loop(f);

	disable_loopback(loop);
}

/*-------------------------------------------------------------------------*/

static int __ref loopback_bind_config(struct usb_configuration *c)
{
	struct f_loopback	*loop;
	int			status;

	loop = kzalloc(sizeof *loop, GFP_KERNEL);
	if (!loop)
		return -ENOMEM;

	loop->function.name = "loopback";
	loop->function.descriptors = fs_loopback_descs;
	loop->function.bind = loopback_bind;
	loop->function.unbind = loopback_unbind;
	loop->function.set_alt = loopback_set_alt;
	loop->function.disable = loopback_disable;

	status = usb_add_function(c, &loop->function);
	if (status)
		kfree(loop);
	return status;
}

static  struct usb_configuration loopback_driver = {
	.label		= "loopback",
	.strings	= loopback_strings,
	.bind		= loopback_bind_config,
	.bConfigurationValue = 2,
	.bmAttributes	= USB_CONFIG_ATT_SELFPOWER,
	/* .iConfiguration = DYNAMIC */
};

/**
 * loopback_add - add a loopback testing configuration to a device
 * @cdev: the device to support the loopback configuration
 */
int __init loopback_add(struct usb_composite_dev *cdev, bool autoresume)
{
	int id;

	/* allocate string ID(s) */
	id = usb_string_id(cdev);
	if (id < 0)
		return id;
	strings_loopback[0].id = id;

	loopback_intf.iInterface = id;
	loopback_driver.iConfiguration = id;

	/* support autoresume for remote wakeup testing */
	if (autoresume)
		sourcesink_driver.bmAttributes |= USB_CONFIG_ATT_WAKEUP;

	/* support OTG systems */
	if (gadget_is_otg(cdev->gadget)) {
		loopback_driver.descriptors = otg_desc;
		loopback_driver.bmAttributes |= USB_CONFIG_ATT_WAKEUP;
	}

	return usb_add_config(cdev, &loopback_driver);
}
