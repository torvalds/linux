/*
 * dummy_hcd.c -- Dummy/Loopback USB host and device emulator driver.
 *
 * Maintainer: Alan Stern <stern@rowland.harvard.edu>
 *
 * Copyright (C) 2003 David Brownell
 * Copyright (C) 2003-2005 Alan Stern
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */


/*
 * This exposes a device side "USB gadget" API, driven by requests to a
 * Linux-USB host controller driver.  USB traffic is simulated; there's
 * no need for USB hardware.  Use this with two other drivers:
 *
 *  - Gadget driver, responding to requests (slave);
 *  - Host-side device driver, as already familiar in Linux.
 *
 * Having this all in one kernel can help some stages of development,
 * bypassing some hardware (and driver) issues.  UML could help too.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/usb.h>
#include <linux/usb/gadget.h>
#include <linux/usb/hcd.h>
#include <linux/scatterlist.h>

#include <asm/byteorder.h>
#include <linux/io.h>
#include <asm/irq.h>
#include <asm/unaligned.h>

#define DRIVER_DESC	"USB Host+Gadget Emulator"
#define DRIVER_VERSION	"02 May 2005"

#define POWER_BUDGET	500	/* in mA; use 8 for low-power port testing */

static const char	driver_name[] = "dummy_hcd";
static const char	driver_desc[] = "USB Host+Gadget Emulator";

static const char	gadget_name[] = "dummy_udc";

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("David Brownell");
MODULE_LICENSE("GPL");

struct dummy_hcd_module_parameters {
	bool is_super_speed;
	bool is_high_speed;
	unsigned int num;
};

static struct dummy_hcd_module_parameters mod_data = {
	.is_super_speed = false,
	.is_high_speed = true,
	.num = 1,
};
module_param_named(is_super_speed, mod_data.is_super_speed, bool, S_IRUGO);
MODULE_PARM_DESC(is_super_speed, "true to simulate SuperSpeed connection");
module_param_named(is_high_speed, mod_data.is_high_speed, bool, S_IRUGO);
MODULE_PARM_DESC(is_high_speed, "true to simulate HighSpeed connection");
module_param_named(num, mod_data.num, uint, S_IRUGO);
MODULE_PARM_DESC(num, "number of emulated controllers");
/*-------------------------------------------------------------------------*/

/* gadget side driver data structres */
struct dummy_ep {
	struct list_head		queue;
	unsigned long			last_io;	/* jiffies timestamp */
	struct usb_gadget		*gadget;
	const struct usb_endpoint_descriptor *desc;
	struct usb_ep			ep;
	unsigned			halted:1;
	unsigned			wedged:1;
	unsigned			already_seen:1;
	unsigned			setup_stage:1;
	unsigned			stream_en:1;
};

struct dummy_request {
	struct list_head		queue;		/* ep's requests */
	struct usb_request		req;
};

static inline struct dummy_ep *usb_ep_to_dummy_ep(struct usb_ep *_ep)
{
	return container_of(_ep, struct dummy_ep, ep);
}

static inline struct dummy_request *usb_request_to_dummy_request
		(struct usb_request *_req)
{
	return container_of(_req, struct dummy_request, req);
}

/*-------------------------------------------------------------------------*/

/*
 * Every device has ep0 for control requests, plus up to 30 more endpoints,
 * in one of two types:
 *
 *   - Configurable:  direction (in/out), type (bulk, iso, etc), and endpoint
 *     number can be changed.  Names like "ep-a" are used for this type.
 *
 *   - Fixed Function:  in other cases.  some characteristics may be mutable;
 *     that'd be hardware-specific.  Names like "ep12out-bulk" are used.
 *
 * Gadget drivers are responsible for not setting up conflicting endpoint
 * configurations, illegal or unsupported packet lengths, and so on.
 */

static const char ep0name[] = "ep0";

static const char *const ep_name[] = {
	ep0name,				/* everyone has ep0 */

	/* act like a pxa250: fifteen fixed function endpoints */
	"ep1in-bulk", "ep2out-bulk", "ep3in-iso", "ep4out-iso", "ep5in-int",
	"ep6in-bulk", "ep7out-bulk", "ep8in-iso", "ep9out-iso", "ep10in-int",
	"ep11in-bulk", "ep12out-bulk", "ep13in-iso", "ep14out-iso",
		"ep15in-int",

	/* or like sa1100: two fixed function endpoints */
	"ep1out-bulk", "ep2in-bulk",

	/* and now some generic EPs so we have enough in multi config */
	"ep3out", "ep4in", "ep5out", "ep6out", "ep7in", "ep8out", "ep9in",
	"ep10out", "ep11out", "ep12in", "ep13out", "ep14in", "ep15out",
};
#define DUMMY_ENDPOINTS	ARRAY_SIZE(ep_name)

/*-------------------------------------------------------------------------*/

#define FIFO_SIZE		64

struct urbp {
	struct urb		*urb;
	struct list_head	urbp_list;
	struct sg_mapping_iter	miter;
	u32			miter_started;
};


enum dummy_rh_state {
	DUMMY_RH_RESET,
	DUMMY_RH_SUSPENDED,
	DUMMY_RH_RUNNING
};

struct dummy_hcd {
	struct dummy			*dum;
	enum dummy_rh_state		rh_state;
	struct timer_list		timer;
	u32				port_status;
	u32				old_status;
	unsigned long			re_timeout;

	struct usb_device		*udev;
	struct list_head		urbp_list;
	u32				stream_en_ep;
	u8				num_stream[30 / 2];

	unsigned			active:1;
	unsigned			old_active:1;
	unsigned			resuming:1;
};

struct dummy {
	spinlock_t			lock;

	/*
	 * SLAVE/GADGET side support
	 */
	struct dummy_ep			ep[DUMMY_ENDPOINTS];
	int				address;
	struct usb_gadget		gadget;
	struct usb_gadget_driver	*driver;
	struct dummy_request		fifo_req;
	u8				fifo_buf[FIFO_SIZE];
	u16				devstatus;
	unsigned			udc_suspended:1;
	unsigned			pullup:1;

	/*
	 * MASTER/HOST side support
	 */
	struct dummy_hcd		*hs_hcd;
	struct dummy_hcd		*ss_hcd;
};

static inline struct dummy_hcd *hcd_to_dummy_hcd(struct usb_hcd *hcd)
{
	return (struct dummy_hcd *) (hcd->hcd_priv);
}

static inline struct usb_hcd *dummy_hcd_to_hcd(struct dummy_hcd *dum)
{
	return container_of((void *) dum, struct usb_hcd, hcd_priv);
}

static inline struct device *dummy_dev(struct dummy_hcd *dum)
{
	return dummy_hcd_to_hcd(dum)->self.controller;
}

static inline struct device *udc_dev(struct dummy *dum)
{
	return dum->gadget.dev.parent;
}

static inline struct dummy *ep_to_dummy(struct dummy_ep *ep)
{
	return container_of(ep->gadget, struct dummy, gadget);
}

static inline struct dummy_hcd *gadget_to_dummy_hcd(struct usb_gadget *gadget)
{
	struct dummy *dum = container_of(gadget, struct dummy, gadget);
	if (dum->gadget.speed == USB_SPEED_SUPER)
		return dum->ss_hcd;
	else
		return dum->hs_hcd;
}

static inline struct dummy *gadget_dev_to_dummy(struct device *dev)
{
	return container_of(dev, struct dummy, gadget.dev);
}

/*-------------------------------------------------------------------------*/

/* SLAVE/GADGET SIDE UTILITY ROUTINES */

/* called with spinlock held */
static void nuke(struct dummy *dum, struct dummy_ep *ep)
{
	while (!list_empty(&ep->queue)) {
		struct dummy_request	*req;

		req = list_entry(ep->queue.next, struct dummy_request, queue);
		list_del_init(&req->queue);
		req->req.status = -ESHUTDOWN;

		spin_unlock(&dum->lock);
		req->req.complete(&ep->ep, &req->req);
		spin_lock(&dum->lock);
	}
}

/* caller must hold lock */
static void stop_activity(struct dummy *dum)
{
	struct dummy_ep	*ep;

	/* prevent any more requests */
	dum->address = 0;

	/* The timer is left running so that outstanding URBs can fail */

	/* nuke any pending requests first, so driver i/o is quiesced */
	list_for_each_entry(ep, &dum->gadget.ep_list, ep.ep_list)
		nuke(dum, ep);

	/* driver now does any non-usb quiescing necessary */
}

/**
 * set_link_state_by_speed() - Sets the current state of the link according to
 *	the hcd speed
 * @dum_hcd: pointer to the dummy_hcd structure to update the link state for
 *
 * This function updates the port_status according to the link state and the
 * speed of the hcd.
 */
static void set_link_state_by_speed(struct dummy_hcd *dum_hcd)
{
	struct dummy *dum = dum_hcd->dum;

	if (dummy_hcd_to_hcd(dum_hcd)->speed == HCD_USB3) {
		if ((dum_hcd->port_status & USB_SS_PORT_STAT_POWER) == 0) {
			dum_hcd->port_status = 0;
		} else if (!dum->pullup || dum->udc_suspended) {
			/* UDC suspend must cause a disconnect */
			dum_hcd->port_status &= ~(USB_PORT_STAT_CONNECTION |
						USB_PORT_STAT_ENABLE);
			if ((dum_hcd->old_status &
			     USB_PORT_STAT_CONNECTION) != 0)
				dum_hcd->port_status |=
					(USB_PORT_STAT_C_CONNECTION << 16);
		} else {
			/* device is connected and not suspended */
			dum_hcd->port_status |= (USB_PORT_STAT_CONNECTION |
						 USB_PORT_STAT_SPEED_5GBPS) ;
			if ((dum_hcd->old_status &
			     USB_PORT_STAT_CONNECTION) == 0)
				dum_hcd->port_status |=
					(USB_PORT_STAT_C_CONNECTION << 16);
			if ((dum_hcd->port_status &
			     USB_PORT_STAT_ENABLE) == 1 &&
				(dum_hcd->port_status &
				 USB_SS_PORT_LS_U0) == 1 &&
				dum_hcd->rh_state != DUMMY_RH_SUSPENDED)
				dum_hcd->active = 1;
		}
	} else {
		if ((dum_hcd->port_status & USB_PORT_STAT_POWER) == 0) {
			dum_hcd->port_status = 0;
		} else if (!dum->pullup || dum->udc_suspended) {
			/* UDC suspend must cause a disconnect */
			dum_hcd->port_status &= ~(USB_PORT_STAT_CONNECTION |
						USB_PORT_STAT_ENABLE |
						USB_PORT_STAT_LOW_SPEED |
						USB_PORT_STAT_HIGH_SPEED |
						USB_PORT_STAT_SUSPEND);
			if ((dum_hcd->old_status &
			     USB_PORT_STAT_CONNECTION) != 0)
				dum_hcd->port_status |=
					(USB_PORT_STAT_C_CONNECTION << 16);
		} else {
			dum_hcd->port_status |= USB_PORT_STAT_CONNECTION;
			if ((dum_hcd->old_status &
			     USB_PORT_STAT_CONNECTION) == 0)
				dum_hcd->port_status |=
					(USB_PORT_STAT_C_CONNECTION << 16);
			if ((dum_hcd->port_status & USB_PORT_STAT_ENABLE) == 0)
				dum_hcd->port_status &= ~USB_PORT_STAT_SUSPEND;
			else if ((dum_hcd->port_status &
				  USB_PORT_STAT_SUSPEND) == 0 &&
					dum_hcd->rh_state != DUMMY_RH_SUSPENDED)
				dum_hcd->active = 1;
		}
	}
}

/* caller must hold lock */
static void set_link_state(struct dummy_hcd *dum_hcd)
{
	struct dummy *dum = dum_hcd->dum;

	dum_hcd->active = 0;
	if (dum->pullup)
		if ((dummy_hcd_to_hcd(dum_hcd)->speed == HCD_USB3 &&
		     dum->gadget.speed != USB_SPEED_SUPER) ||
		    (dummy_hcd_to_hcd(dum_hcd)->speed != HCD_USB3 &&
		     dum->gadget.speed == USB_SPEED_SUPER))
			return;

	set_link_state_by_speed(dum_hcd);

	if ((dum_hcd->port_status & USB_PORT_STAT_ENABLE) == 0 ||
	     dum_hcd->active)
		dum_hcd->resuming = 0;

	/* if !connected or reset */
	if ((dum_hcd->port_status & USB_PORT_STAT_CONNECTION) == 0 ||
			(dum_hcd->port_status & USB_PORT_STAT_RESET) != 0) {
		/*
		 * We're connected and not reset (reset occurred now),
		 * and driver attached - disconnect!
		 */
		if ((dum_hcd->old_status & USB_PORT_STAT_CONNECTION) != 0 &&
		    (dum_hcd->old_status & USB_PORT_STAT_RESET) == 0 &&
		    dum->driver) {
			stop_activity(dum);
			spin_unlock(&dum->lock);
			dum->driver->disconnect(&dum->gadget);
			spin_lock(&dum->lock);
		}
	} else if (dum_hcd->active != dum_hcd->old_active) {
		if (dum_hcd->old_active && dum->driver->suspend) {
			spin_unlock(&dum->lock);
			dum->driver->suspend(&dum->gadget);
			spin_lock(&dum->lock);
		} else if (!dum_hcd->old_active &&  dum->driver->resume) {
			spin_unlock(&dum->lock);
			dum->driver->resume(&dum->gadget);
			spin_lock(&dum->lock);
		}
	}

	dum_hcd->old_status = dum_hcd->port_status;
	dum_hcd->old_active = dum_hcd->active;
}

/*-------------------------------------------------------------------------*/

/* SLAVE/GADGET SIDE DRIVER
 *
 * This only tracks gadget state.  All the work is done when the host
 * side tries some (emulated) i/o operation.  Real device controller
 * drivers would do real i/o using dma, fifos, irqs, timers, etc.
 */

#define is_enabled(dum) \
	(dum->port_status & USB_PORT_STAT_ENABLE)

static int dummy_enable(struct usb_ep *_ep,
		const struct usb_endpoint_descriptor *desc)
{
	struct dummy		*dum;
	struct dummy_hcd	*dum_hcd;
	struct dummy_ep		*ep;
	unsigned		max;
	int			retval;

	ep = usb_ep_to_dummy_ep(_ep);
	if (!_ep || !desc || ep->desc || _ep->name == ep0name
			|| desc->bDescriptorType != USB_DT_ENDPOINT)
		return -EINVAL;
	dum = ep_to_dummy(ep);
	if (!dum->driver)
		return -ESHUTDOWN;

	dum_hcd = gadget_to_dummy_hcd(&dum->gadget);
	if (!is_enabled(dum_hcd))
		return -ESHUTDOWN;

	/*
	 * For HS/FS devices only bits 0..10 of the wMaxPacketSize represent the
	 * maximum packet size.
	 * For SS devices the wMaxPacketSize is limited by 1024.
	 */
	max = usb_endpoint_maxp(desc) & 0x7ff;

	/* drivers must not request bad settings, since lower levels
	 * (hardware or its drivers) may not check.  some endpoints
	 * can't do iso, many have maxpacket limitations, etc.
	 *
	 * since this "hardware" driver is here to help debugging, we
	 * have some extra sanity checks.  (there could be more though,
	 * especially for "ep9out" style fixed function ones.)
	 */
	retval = -EINVAL;
	switch (usb_endpoint_type(desc)) {
	case USB_ENDPOINT_XFER_BULK:
		if (strstr(ep->ep.name, "-iso")
				|| strstr(ep->ep.name, "-int")) {
			goto done;
		}
		switch (dum->gadget.speed) {
		case USB_SPEED_SUPER:
			if (max == 1024)
				break;
			goto done;
		case USB_SPEED_HIGH:
			if (max == 512)
				break;
			goto done;
		case USB_SPEED_FULL:
			if (max == 8 || max == 16 || max == 32 || max == 64)
				/* we'll fake any legal size */
				break;
			/* save a return statement */
		default:
			goto done;
		}
		break;
	case USB_ENDPOINT_XFER_INT:
		if (strstr(ep->ep.name, "-iso")) /* bulk is ok */
			goto done;
		/* real hardware might not handle all packet sizes */
		switch (dum->gadget.speed) {
		case USB_SPEED_SUPER:
		case USB_SPEED_HIGH:
			if (max <= 1024)
				break;
			/* save a return statement */
		case USB_SPEED_FULL:
			if (max <= 64)
				break;
			/* save a return statement */
		default:
			if (max <= 8)
				break;
			goto done;
		}
		break;
	case USB_ENDPOINT_XFER_ISOC:
		if (strstr(ep->ep.name, "-bulk")
				|| strstr(ep->ep.name, "-int"))
			goto done;
		/* real hardware might not handle all packet sizes */
		switch (dum->gadget.speed) {
		case USB_SPEED_SUPER:
		case USB_SPEED_HIGH:
			if (max <= 1024)
				break;
			/* save a return statement */
		case USB_SPEED_FULL:
			if (max <= 1023)
				break;
			/* save a return statement */
		default:
			goto done;
		}
		break;
	default:
		/* few chips support control except on ep0 */
		goto done;
	}

	_ep->maxpacket = max;
	if (usb_ss_max_streams(_ep->comp_desc)) {
		if (!usb_endpoint_xfer_bulk(desc)) {
			dev_err(udc_dev(dum), "Can't enable stream support on "
					"non-bulk ep %s\n", _ep->name);
			return -EINVAL;
		}
		ep->stream_en = 1;
	}
	ep->desc = desc;

	dev_dbg(udc_dev(dum), "enabled %s (ep%d%s-%s) maxpacket %d stream %s\n",
		_ep->name,
		desc->bEndpointAddress & 0x0f,
		(desc->bEndpointAddress & USB_DIR_IN) ? "in" : "out",
		({ char *val;
		 switch (usb_endpoint_type(desc)) {
		 case USB_ENDPOINT_XFER_BULK:
			 val = "bulk";
			 break;
		 case USB_ENDPOINT_XFER_ISOC:
			 val = "iso";
			 break;
		 case USB_ENDPOINT_XFER_INT:
			 val = "intr";
			 break;
		 default:
			 val = "ctrl";
			 break;
		 }; val; }),
		max, ep->stream_en ? "enabled" : "disabled");

	/* at this point real hardware should be NAKing transfers
	 * to that endpoint, until a buffer is queued to it.
	 */
	ep->halted = ep->wedged = 0;
	retval = 0;
done:
	return retval;
}

static int dummy_disable(struct usb_ep *_ep)
{
	struct dummy_ep		*ep;
	struct dummy		*dum;
	unsigned long		flags;
	int			retval;

	ep = usb_ep_to_dummy_ep(_ep);
	if (!_ep || !ep->desc || _ep->name == ep0name)
		return -EINVAL;
	dum = ep_to_dummy(ep);

	spin_lock_irqsave(&dum->lock, flags);
	ep->desc = NULL;
	ep->stream_en = 0;
	retval = 0;
	nuke(dum, ep);
	spin_unlock_irqrestore(&dum->lock, flags);

	dev_dbg(udc_dev(dum), "disabled %s\n", _ep->name);
	return retval;
}

static struct usb_request *dummy_alloc_request(struct usb_ep *_ep,
		gfp_t mem_flags)
{
	struct dummy_ep		*ep;
	struct dummy_request	*req;

	if (!_ep)
		return NULL;
	ep = usb_ep_to_dummy_ep(_ep);

	req = kzalloc(sizeof(*req), mem_flags);
	if (!req)
		return NULL;
	INIT_LIST_HEAD(&req->queue);
	return &req->req;
}

static void dummy_free_request(struct usb_ep *_ep, struct usb_request *_req)
{
	struct dummy_request	*req;

	if (!_ep || !_req) {
		WARN_ON(1);
		return;
	}

	req = usb_request_to_dummy_request(_req);
	WARN_ON(!list_empty(&req->queue));
	kfree(req);
}

static void fifo_complete(struct usb_ep *ep, struct usb_request *req)
{
}

static int dummy_queue(struct usb_ep *_ep, struct usb_request *_req,
		gfp_t mem_flags)
{
	struct dummy_ep		*ep;
	struct dummy_request	*req;
	struct dummy		*dum;
	struct dummy_hcd	*dum_hcd;
	unsigned long		flags;

	req = usb_request_to_dummy_request(_req);
	if (!_req || !list_empty(&req->queue) || !_req->complete)
		return -EINVAL;

	ep = usb_ep_to_dummy_ep(_ep);
	if (!_ep || (!ep->desc && _ep->name != ep0name))
		return -EINVAL;

	dum = ep_to_dummy(ep);
	dum_hcd = gadget_to_dummy_hcd(&dum->gadget);
	if (!dum->driver || !is_enabled(dum_hcd))
		return -ESHUTDOWN;

#if 0
	dev_dbg(udc_dev(dum), "ep %p queue req %p to %s, len %d buf %p\n",
			ep, _req, _ep->name, _req->length, _req->buf);
#endif
	_req->status = -EINPROGRESS;
	_req->actual = 0;
	spin_lock_irqsave(&dum->lock, flags);

	/* implement an emulated single-request FIFO */
	if (ep->desc && (ep->desc->bEndpointAddress & USB_DIR_IN) &&
			list_empty(&dum->fifo_req.queue) &&
			list_empty(&ep->queue) &&
			_req->length <= FIFO_SIZE) {
		req = &dum->fifo_req;
		req->req = *_req;
		req->req.buf = dum->fifo_buf;
		memcpy(dum->fifo_buf, _req->buf, _req->length);
		req->req.context = dum;
		req->req.complete = fifo_complete;

		list_add_tail(&req->queue, &ep->queue);
		spin_unlock(&dum->lock);
		_req->actual = _req->length;
		_req->status = 0;
		_req->complete(_ep, _req);
		spin_lock(&dum->lock);
	}  else
		list_add_tail(&req->queue, &ep->queue);
	spin_unlock_irqrestore(&dum->lock, flags);

	/* real hardware would likely enable transfers here, in case
	 * it'd been left NAKing.
	 */
	return 0;
}

static int dummy_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct dummy_ep		*ep;
	struct dummy		*dum;
	int			retval = -EINVAL;
	unsigned long		flags;
	struct dummy_request	*req = NULL;

	if (!_ep || !_req)
		return retval;
	ep = usb_ep_to_dummy_ep(_ep);
	dum = ep_to_dummy(ep);

	if (!dum->driver)
		return -ESHUTDOWN;

	local_irq_save(flags);
	spin_lock(&dum->lock);
	list_for_each_entry(req, &ep->queue, queue) {
		if (&req->req == _req) {
			list_del_init(&req->queue);
			_req->status = -ECONNRESET;
			retval = 0;
			break;
		}
	}
	spin_unlock(&dum->lock);

	if (retval == 0) {
		dev_dbg(udc_dev(dum),
				"dequeued req %p from %s, len %d buf %p\n",
				req, _ep->name, _req->length, _req->buf);
		_req->complete(_ep, _req);
	}
	local_irq_restore(flags);
	return retval;
}

static int
dummy_set_halt_and_wedge(struct usb_ep *_ep, int value, int wedged)
{
	struct dummy_ep		*ep;
	struct dummy		*dum;

	if (!_ep)
		return -EINVAL;
	ep = usb_ep_to_dummy_ep(_ep);
	dum = ep_to_dummy(ep);
	if (!dum->driver)
		return -ESHUTDOWN;
	if (!value)
		ep->halted = ep->wedged = 0;
	else if (ep->desc && (ep->desc->bEndpointAddress & USB_DIR_IN) &&
			!list_empty(&ep->queue))
		return -EAGAIN;
	else {
		ep->halted = 1;
		if (wedged)
			ep->wedged = 1;
	}
	/* FIXME clear emulated data toggle too */
	return 0;
}

static int
dummy_set_halt(struct usb_ep *_ep, int value)
{
	return dummy_set_halt_and_wedge(_ep, value, 0);
}

static int dummy_set_wedge(struct usb_ep *_ep)
{
	if (!_ep || _ep->name == ep0name)
		return -EINVAL;
	return dummy_set_halt_and_wedge(_ep, 1, 1);
}

static const struct usb_ep_ops dummy_ep_ops = {
	.enable		= dummy_enable,
	.disable	= dummy_disable,

	.alloc_request	= dummy_alloc_request,
	.free_request	= dummy_free_request,

	.queue		= dummy_queue,
	.dequeue	= dummy_dequeue,

	.set_halt	= dummy_set_halt,
	.set_wedge	= dummy_set_wedge,
};

/*-------------------------------------------------------------------------*/

/* there are both host and device side versions of this call ... */
static int dummy_g_get_frame(struct usb_gadget *_gadget)
{
	struct timeval	tv;

	do_gettimeofday(&tv);
	return tv.tv_usec / 1000;
}

static int dummy_wakeup(struct usb_gadget *_gadget)
{
	struct dummy_hcd *dum_hcd;

	dum_hcd = gadget_to_dummy_hcd(_gadget);
	if (!(dum_hcd->dum->devstatus & ((1 << USB_DEVICE_B_HNP_ENABLE)
				| (1 << USB_DEVICE_REMOTE_WAKEUP))))
		return -EINVAL;
	if ((dum_hcd->port_status & USB_PORT_STAT_CONNECTION) == 0)
		return -ENOLINK;
	if ((dum_hcd->port_status & USB_PORT_STAT_SUSPEND) == 0 &&
			 dum_hcd->rh_state != DUMMY_RH_SUSPENDED)
		return -EIO;

	/* FIXME: What if the root hub is suspended but the port isn't? */

	/* hub notices our request, issues downstream resume, etc */
	dum_hcd->resuming = 1;
	dum_hcd->re_timeout = jiffies + msecs_to_jiffies(20);
	mod_timer(&dummy_hcd_to_hcd(dum_hcd)->rh_timer, dum_hcd->re_timeout);
	return 0;
}

static int dummy_set_selfpowered(struct usb_gadget *_gadget, int value)
{
	struct dummy	*dum;

	dum = gadget_to_dummy_hcd(_gadget)->dum;
	if (value)
		dum->devstatus |= (1 << USB_DEVICE_SELF_POWERED);
	else
		dum->devstatus &= ~(1 << USB_DEVICE_SELF_POWERED);
	return 0;
}

static void dummy_udc_update_ep0(struct dummy *dum)
{
	if (dum->gadget.speed == USB_SPEED_SUPER)
		dum->ep[0].ep.maxpacket = 9;
	else
		dum->ep[0].ep.maxpacket = 64;
}

static int dummy_pullup(struct usb_gadget *_gadget, int value)
{
	struct dummy_hcd *dum_hcd;
	struct dummy	*dum;
	unsigned long	flags;

	dum = gadget_dev_to_dummy(&_gadget->dev);

	if (value && dum->driver) {
		if (mod_data.is_super_speed)
			dum->gadget.speed = dum->driver->max_speed;
		else if (mod_data.is_high_speed)
			dum->gadget.speed = min_t(u8, USB_SPEED_HIGH,
					dum->driver->max_speed);
		else
			dum->gadget.speed = USB_SPEED_FULL;
		dummy_udc_update_ep0(dum);

		if (dum->gadget.speed < dum->driver->max_speed)
			dev_dbg(udc_dev(dum), "This device can perform faster"
				" if you connect it to a %s port...\n",
				usb_speed_string(dum->driver->max_speed));
	}
	dum_hcd = gadget_to_dummy_hcd(_gadget);

	spin_lock_irqsave(&dum->lock, flags);
	dum->pullup = (value != 0);
	set_link_state(dum_hcd);
	spin_unlock_irqrestore(&dum->lock, flags);

	usb_hcd_poll_rh_status(dummy_hcd_to_hcd(dum_hcd));
	return 0;
}

static int dummy_udc_start(struct usb_gadget *g,
		struct usb_gadget_driver *driver);
static int dummy_udc_stop(struct usb_gadget *g,
		struct usb_gadget_driver *driver);

static const struct usb_gadget_ops dummy_ops = {
	.get_frame	= dummy_g_get_frame,
	.wakeup		= dummy_wakeup,
	.set_selfpowered = dummy_set_selfpowered,
	.pullup		= dummy_pullup,
	.udc_start	= dummy_udc_start,
	.udc_stop	= dummy_udc_stop,
};

/*-------------------------------------------------------------------------*/

/* "function" sysfs attribute */
static ssize_t show_function(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct dummy	*dum = gadget_dev_to_dummy(dev);

	if (!dum->driver || !dum->driver->function)
		return 0;
	return scnprintf(buf, PAGE_SIZE, "%s\n", dum->driver->function);
}
static DEVICE_ATTR(function, S_IRUGO, show_function, NULL);

/*-------------------------------------------------------------------------*/

/*
 * Driver registration/unregistration.
 *
 * This is basically hardware-specific; there's usually only one real USB
 * device (not host) controller since that's how USB devices are intended
 * to work.  So most implementations of these api calls will rely on the
 * fact that only one driver will ever bind to the hardware.  But curious
 * hardware can be built with discrete components, so the gadget API doesn't
 * require that assumption.
 *
 * For this emulator, it might be convenient to create a usb slave device
 * for each driver that registers:  just add to a big root hub.
 */

static int dummy_udc_start(struct usb_gadget *g,
		struct usb_gadget_driver *driver)
{
	struct dummy_hcd	*dum_hcd = gadget_to_dummy_hcd(g);
	struct dummy		*dum = dum_hcd->dum;

	if (driver->max_speed == USB_SPEED_UNKNOWN)
		return -EINVAL;

	/*
	 * SLAVE side init ... the layer above hardware, which
	 * can't enumerate without help from the driver we're binding.
	 */

	dum->devstatus = 0;

	dum->driver = driver;
	dev_dbg(udc_dev(dum), "binding gadget driver '%s'\n",
			driver->driver.name);
	return 0;
}

static int dummy_udc_stop(struct usb_gadget *g,
		struct usb_gadget_driver *driver)
{
	struct dummy_hcd	*dum_hcd = gadget_to_dummy_hcd(g);
	struct dummy		*dum = dum_hcd->dum;

	dev_dbg(udc_dev(dum), "unregister gadget driver '%s'\n",
			driver->driver.name);

	dum->driver = NULL;

	return 0;
}

#undef is_enabled

/* The gadget structure is stored inside the hcd structure and will be
 * released along with it. */
static void init_dummy_udc_hw(struct dummy *dum)
{
	int i;

	INIT_LIST_HEAD(&dum->gadget.ep_list);
	for (i = 0; i < DUMMY_ENDPOINTS; i++) {
		struct dummy_ep	*ep = &dum->ep[i];

		if (!ep_name[i])
			break;
		ep->ep.name = ep_name[i];
		ep->ep.ops = &dummy_ep_ops;
		list_add_tail(&ep->ep.ep_list, &dum->gadget.ep_list);
		ep->halted = ep->wedged = ep->already_seen =
				ep->setup_stage = 0;
		ep->ep.maxpacket = ~0;
		ep->ep.max_streams = 16;
		ep->last_io = jiffies;
		ep->gadget = &dum->gadget;
		ep->desc = NULL;
		INIT_LIST_HEAD(&ep->queue);
	}

	dum->gadget.ep0 = &dum->ep[0].ep;
	list_del_init(&dum->ep[0].ep.ep_list);
	INIT_LIST_HEAD(&dum->fifo_req.queue);

#ifdef CONFIG_USB_OTG
	dum->gadget.is_otg = 1;
#endif
}

static int dummy_udc_probe(struct platform_device *pdev)
{
	struct dummy	*dum;
	int		rc;

	dum = *((void **)dev_get_platdata(&pdev->dev));
	dum->gadget.name = gadget_name;
	dum->gadget.ops = &dummy_ops;
	dum->gadget.max_speed = USB_SPEED_SUPER;

	dum->gadget.dev.parent = &pdev->dev;
	init_dummy_udc_hw(dum);

	rc = usb_add_gadget_udc(&pdev->dev, &dum->gadget);
	if (rc < 0)
		goto err_udc;

	rc = device_create_file(&dum->gadget.dev, &dev_attr_function);
	if (rc < 0)
		goto err_dev;
	platform_set_drvdata(pdev, dum);
	return rc;

err_dev:
	usb_del_gadget_udc(&dum->gadget);
err_udc:
	return rc;
}

static int dummy_udc_remove(struct platform_device *pdev)
{
	struct dummy	*dum = platform_get_drvdata(pdev);

	usb_del_gadget_udc(&dum->gadget);
	device_remove_file(&dum->gadget.dev, &dev_attr_function);
	return 0;
}

static void dummy_udc_pm(struct dummy *dum, struct dummy_hcd *dum_hcd,
		int suspend)
{
	spin_lock_irq(&dum->lock);
	dum->udc_suspended = suspend;
	set_link_state(dum_hcd);
	spin_unlock_irq(&dum->lock);
}

static int dummy_udc_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct dummy		*dum = platform_get_drvdata(pdev);
	struct dummy_hcd	*dum_hcd = gadget_to_dummy_hcd(&dum->gadget);

	dev_dbg(&pdev->dev, "%s\n", __func__);
	dummy_udc_pm(dum, dum_hcd, 1);
	usb_hcd_poll_rh_status(dummy_hcd_to_hcd(dum_hcd));
	return 0;
}

static int dummy_udc_resume(struct platform_device *pdev)
{
	struct dummy		*dum = platform_get_drvdata(pdev);
	struct dummy_hcd	*dum_hcd = gadget_to_dummy_hcd(&dum->gadget);

	dev_dbg(&pdev->dev, "%s\n", __func__);
	dummy_udc_pm(dum, dum_hcd, 0);
	usb_hcd_poll_rh_status(dummy_hcd_to_hcd(dum_hcd));
	return 0;
}

static struct platform_driver dummy_udc_driver = {
	.probe		= dummy_udc_probe,
	.remove		= dummy_udc_remove,
	.suspend	= dummy_udc_suspend,
	.resume		= dummy_udc_resume,
	.driver		= {
		.name	= (char *) gadget_name,
		.owner	= THIS_MODULE,
	},
};

/*-------------------------------------------------------------------------*/

static unsigned int dummy_get_ep_idx(const struct usb_endpoint_descriptor *desc)
{
	unsigned int index;

	index = usb_endpoint_num(desc) << 1;
	if (usb_endpoint_dir_in(desc))
		index |= 1;
	return index;
}

/* MASTER/HOST SIDE DRIVER
 *
 * this uses the hcd framework to hook up to host side drivers.
 * its root hub will only have one device, otherwise it acts like
 * a normal host controller.
 *
 * when urbs are queued, they're just stuck on a list that we
 * scan in a timer callback.  that callback connects writes from
 * the host with reads from the device, and so on, based on the
 * usb 2.0 rules.
 */

static int dummy_ep_stream_en(struct dummy_hcd *dum_hcd, struct urb *urb)
{
	const struct usb_endpoint_descriptor *desc = &urb->ep->desc;
	u32 index;

	if (!usb_endpoint_xfer_bulk(desc))
		return 0;

	index = dummy_get_ep_idx(desc);
	return (1 << index) & dum_hcd->stream_en_ep;
}

/*
 * The max stream number is saved as a nibble so for the 30 possible endpoints
 * we only 15 bytes of memory. Therefore we are limited to max 16 streams (0
 * means we use only 1 stream). The maximum according to the spec is 16bit so
 * if the 16 stream limit is about to go, the array size should be incremented
 * to 30 elements of type u16.
 */
static int get_max_streams_for_pipe(struct dummy_hcd *dum_hcd,
		unsigned int pipe)
{
	int max_streams;

	max_streams = dum_hcd->num_stream[usb_pipeendpoint(pipe)];
	if (usb_pipeout(pipe))
		max_streams >>= 4;
	else
		max_streams &= 0xf;
	max_streams++;
	return max_streams;
}

static void set_max_streams_for_pipe(struct dummy_hcd *dum_hcd,
		unsigned int pipe, unsigned int streams)
{
	int max_streams;

	streams--;
	max_streams = dum_hcd->num_stream[usb_pipeendpoint(pipe)];
	if (usb_pipeout(pipe)) {
		streams <<= 4;
		max_streams &= 0xf;
	} else {
		max_streams &= 0xf0;
	}
	max_streams |= streams;
	dum_hcd->num_stream[usb_pipeendpoint(pipe)] = max_streams;
}

static int dummy_validate_stream(struct dummy_hcd *dum_hcd, struct urb *urb)
{
	unsigned int max_streams;
	int enabled;

	enabled = dummy_ep_stream_en(dum_hcd, urb);
	if (!urb->stream_id) {
		if (enabled)
			return -EINVAL;
		return 0;
	}
	if (!enabled)
		return -EINVAL;

	max_streams = get_max_streams_for_pipe(dum_hcd,
			usb_pipeendpoint(urb->pipe));
	if (urb->stream_id > max_streams) {
		dev_err(dummy_dev(dum_hcd), "Stream id %d is out of range.\n",
				urb->stream_id);
		BUG();
		return -EINVAL;
	}
	return 0;
}

static int dummy_urb_enqueue(
	struct usb_hcd			*hcd,
	struct urb			*urb,
	gfp_t				mem_flags
) {
	struct dummy_hcd *dum_hcd;
	struct urbp	*urbp;
	unsigned long	flags;
	int		rc;

	urbp = kmalloc(sizeof *urbp, mem_flags);
	if (!urbp)
		return -ENOMEM;
	urbp->urb = urb;
	urbp->miter_started = 0;

	dum_hcd = hcd_to_dummy_hcd(hcd);
	spin_lock_irqsave(&dum_hcd->dum->lock, flags);

	rc = dummy_validate_stream(dum_hcd, urb);
	if (rc) {
		kfree(urbp);
		goto done;
	}

	rc = usb_hcd_link_urb_to_ep(hcd, urb);
	if (rc) {
		kfree(urbp);
		goto done;
	}

	if (!dum_hcd->udev) {
		dum_hcd->udev = urb->dev;
		usb_get_dev(dum_hcd->udev);
	} else if (unlikely(dum_hcd->udev != urb->dev))
		dev_err(dummy_dev(dum_hcd), "usb_device address has changed!\n");

	list_add_tail(&urbp->urbp_list, &dum_hcd->urbp_list);
	urb->hcpriv = urbp;
	if (usb_pipetype(urb->pipe) == PIPE_CONTROL)
		urb->error_count = 1;		/* mark as a new urb */

	/* kick the scheduler, it'll do the rest */
	if (!timer_pending(&dum_hcd->timer))
		mod_timer(&dum_hcd->timer, jiffies + 1);

 done:
	spin_unlock_irqrestore(&dum_hcd->dum->lock, flags);
	return rc;
}

static int dummy_urb_dequeue(struct usb_hcd *hcd, struct urb *urb, int status)
{
	struct dummy_hcd *dum_hcd;
	unsigned long	flags;
	int		rc;

	/* giveback happens automatically in timer callback,
	 * so make sure the callback happens */
	dum_hcd = hcd_to_dummy_hcd(hcd);
	spin_lock_irqsave(&dum_hcd->dum->lock, flags);

	rc = usb_hcd_check_unlink_urb(hcd, urb, status);
	if (!rc && dum_hcd->rh_state != DUMMY_RH_RUNNING &&
			!list_empty(&dum_hcd->urbp_list))
		mod_timer(&dum_hcd->timer, jiffies);

	spin_unlock_irqrestore(&dum_hcd->dum->lock, flags);
	return rc;
}

static int dummy_perform_transfer(struct urb *urb, struct dummy_request *req,
		u32 len)
{
	void *ubuf, *rbuf;
	struct urbp *urbp = urb->hcpriv;
	int to_host;
	struct sg_mapping_iter *miter = &urbp->miter;
	u32 trans = 0;
	u32 this_sg;
	bool next_sg;

	to_host = usb_pipein(urb->pipe);
	rbuf = req->req.buf + req->req.actual;

	if (!urb->num_sgs) {
		ubuf = urb->transfer_buffer + urb->actual_length;
		if (to_host)
			memcpy(ubuf, rbuf, len);
		else
			memcpy(rbuf, ubuf, len);
		return len;
	}

	if (!urbp->miter_started) {
		u32 flags = SG_MITER_ATOMIC;

		if (to_host)
			flags |= SG_MITER_TO_SG;
		else
			flags |= SG_MITER_FROM_SG;

		sg_miter_start(miter, urb->sg, urb->num_sgs, flags);
		urbp->miter_started = 1;
	}
	next_sg = sg_miter_next(miter);
	if (next_sg == false) {
		WARN_ON_ONCE(1);
		return -EINVAL;
	}
	do {
		ubuf = miter->addr;
		this_sg = min_t(u32, len, miter->length);
		miter->consumed = this_sg;
		trans += this_sg;

		if (to_host)
			memcpy(ubuf, rbuf, this_sg);
		else
			memcpy(rbuf, ubuf, this_sg);
		len -= this_sg;

		if (!len)
			break;
		next_sg = sg_miter_next(miter);
		if (next_sg == false) {
			WARN_ON_ONCE(1);
			return -EINVAL;
		}

		rbuf += this_sg;
	} while (1);

	sg_miter_stop(miter);
	return trans;
}

/* transfer up to a frame's worth; caller must own lock */
static int transfer(struct dummy_hcd *dum_hcd, struct urb *urb,
		struct dummy_ep *ep, int limit, int *status)
{
	struct dummy		*dum = dum_hcd->dum;
	struct dummy_request	*req;

top:
	/* if there's no request queued, the device is NAKing; return */
	list_for_each_entry(req, &ep->queue, queue) {
		unsigned	host_len, dev_len, len;
		int		is_short, to_host;
		int		rescan = 0;

		if (dummy_ep_stream_en(dum_hcd, urb)) {
			if ((urb->stream_id != req->req.stream_id))
				continue;
		}

		/* 1..N packets of ep->ep.maxpacket each ... the last one
		 * may be short (including zero length).
		 *
		 * writer can send a zlp explicitly (length 0) or implicitly
		 * (length mod maxpacket zero, and 'zero' flag); they always
		 * terminate reads.
		 */
		host_len = urb->transfer_buffer_length - urb->actual_length;
		dev_len = req->req.length - req->req.actual;
		len = min(host_len, dev_len);

		/* FIXME update emulated data toggle too */

		to_host = usb_pipein(urb->pipe);
		if (unlikely(len == 0))
			is_short = 1;
		else {
			/* not enough bandwidth left? */
			if (limit < ep->ep.maxpacket && limit < len)
				break;
			len = min_t(unsigned, len, limit);
			if (len == 0)
				break;

			/* use an extra pass for the final short packet */
			if (len > ep->ep.maxpacket) {
				rescan = 1;
				len -= (len % ep->ep.maxpacket);
			}
			is_short = (len % ep->ep.maxpacket) != 0;

			len = dummy_perform_transfer(urb, req, len);

			ep->last_io = jiffies;
			if ((int)len < 0) {
				req->req.status = len;
			} else {
				limit -= len;
				urb->actual_length += len;
				req->req.actual += len;
			}
		}

		/* short packets terminate, maybe with overflow/underflow.
		 * it's only really an error to write too much.
		 *
		 * partially filling a buffer optionally blocks queue advances
		 * (so completion handlers can clean up the queue) but we don't
		 * need to emulate such data-in-flight.
		 */
		if (is_short) {
			if (host_len == dev_len) {
				req->req.status = 0;
				*status = 0;
			} else if (to_host) {
				req->req.status = 0;
				if (dev_len > host_len)
					*status = -EOVERFLOW;
				else
					*status = 0;
			} else if (!to_host) {
				*status = 0;
				if (host_len > dev_len)
					req->req.status = -EOVERFLOW;
				else
					req->req.status = 0;
			}

		/* many requests terminate without a short packet */
		} else {
			if (req->req.length == req->req.actual
					&& !req->req.zero)
				req->req.status = 0;
			if (urb->transfer_buffer_length == urb->actual_length
					&& !(urb->transfer_flags
						& URB_ZERO_PACKET))
				*status = 0;
		}

		/* device side completion --> continuable */
		if (req->req.status != -EINPROGRESS) {
			list_del_init(&req->queue);

			spin_unlock(&dum->lock);
			req->req.complete(&ep->ep, &req->req);
			spin_lock(&dum->lock);

			/* requests might have been unlinked... */
			rescan = 1;
		}

		/* host side completion --> terminate */
		if (*status != -EINPROGRESS)
			break;

		/* rescan to continue with any other queued i/o */
		if (rescan)
			goto top;
	}
	return limit;
}

static int periodic_bytes(struct dummy *dum, struct dummy_ep *ep)
{
	int	limit = ep->ep.maxpacket;

	if (dum->gadget.speed == USB_SPEED_HIGH) {
		int	tmp;

		/* high bandwidth mode */
		tmp = usb_endpoint_maxp(ep->desc);
		tmp = (tmp >> 11) & 0x03;
		tmp *= 8 /* applies to entire frame */;
		limit += limit * tmp;
	}
	if (dum->gadget.speed == USB_SPEED_SUPER) {
		switch (usb_endpoint_type(ep->desc)) {
		case USB_ENDPOINT_XFER_ISOC:
			/* Sec. 4.4.8.2 USB3.0 Spec */
			limit = 3 * 16 * 1024 * 8;
			break;
		case USB_ENDPOINT_XFER_INT:
			/* Sec. 4.4.7.2 USB3.0 Spec */
			limit = 3 * 1024 * 8;
			break;
		case USB_ENDPOINT_XFER_BULK:
		default:
			break;
		}
	}
	return limit;
}

#define is_active(dum_hcd)	((dum_hcd->port_status & \
		(USB_PORT_STAT_CONNECTION | USB_PORT_STAT_ENABLE | \
			USB_PORT_STAT_SUSPEND)) \
		== (USB_PORT_STAT_CONNECTION | USB_PORT_STAT_ENABLE))

static struct dummy_ep *find_endpoint(struct dummy *dum, u8 address)
{
	int		i;

	if (!is_active((dum->gadget.speed == USB_SPEED_SUPER ?
			dum->ss_hcd : dum->hs_hcd)))
		return NULL;
	if ((address & ~USB_DIR_IN) == 0)
		return &dum->ep[0];
	for (i = 1; i < DUMMY_ENDPOINTS; i++) {
		struct dummy_ep	*ep = &dum->ep[i];

		if (!ep->desc)
			continue;
		if (ep->desc->bEndpointAddress == address)
			return ep;
	}
	return NULL;
}

#undef is_active

#define Dev_Request	(USB_TYPE_STANDARD | USB_RECIP_DEVICE)
#define Dev_InRequest	(Dev_Request | USB_DIR_IN)
#define Intf_Request	(USB_TYPE_STANDARD | USB_RECIP_INTERFACE)
#define Intf_InRequest	(Intf_Request | USB_DIR_IN)
#define Ep_Request	(USB_TYPE_STANDARD | USB_RECIP_ENDPOINT)
#define Ep_InRequest	(Ep_Request | USB_DIR_IN)


/**
 * handle_control_request() - handles all control transfers
 * @dum: pointer to dummy (the_controller)
 * @urb: the urb request to handle
 * @setup: pointer to the setup data for a USB device control
 *	 request
 * @status: pointer to request handling status
 *
 * Return 0 - if the request was handled
 *	  1 - if the request wasn't handles
 *	  error code on error
 */
static int handle_control_request(struct dummy_hcd *dum_hcd, struct urb *urb,
				  struct usb_ctrlrequest *setup,
				  int *status)
{
	struct dummy_ep		*ep2;
	struct dummy		*dum = dum_hcd->dum;
	int			ret_val = 1;
	unsigned	w_index;
	unsigned	w_value;

	w_index = le16_to_cpu(setup->wIndex);
	w_value = le16_to_cpu(setup->wValue);
	switch (setup->bRequest) {
	case USB_REQ_SET_ADDRESS:
		if (setup->bRequestType != Dev_Request)
			break;
		dum->address = w_value;
		*status = 0;
		dev_dbg(udc_dev(dum), "set_address = %d\n",
				w_value);
		ret_val = 0;
		break;
	case USB_REQ_SET_FEATURE:
		if (setup->bRequestType == Dev_Request) {
			ret_val = 0;
			switch (w_value) {
			case USB_DEVICE_REMOTE_WAKEUP:
				break;
			case USB_DEVICE_B_HNP_ENABLE:
				dum->gadget.b_hnp_enable = 1;
				break;
			case USB_DEVICE_A_HNP_SUPPORT:
				dum->gadget.a_hnp_support = 1;
				break;
			case USB_DEVICE_A_ALT_HNP_SUPPORT:
				dum->gadget.a_alt_hnp_support = 1;
				break;
			case USB_DEVICE_U1_ENABLE:
				if (dummy_hcd_to_hcd(dum_hcd)->speed ==
				    HCD_USB3)
					w_value = USB_DEV_STAT_U1_ENABLED;
				else
					ret_val = -EOPNOTSUPP;
				break;
			case USB_DEVICE_U2_ENABLE:
				if (dummy_hcd_to_hcd(dum_hcd)->speed ==
				    HCD_USB3)
					w_value = USB_DEV_STAT_U2_ENABLED;
				else
					ret_val = -EOPNOTSUPP;
				break;
			case USB_DEVICE_LTM_ENABLE:
				if (dummy_hcd_to_hcd(dum_hcd)->speed ==
				    HCD_USB3)
					w_value = USB_DEV_STAT_LTM_ENABLED;
				else
					ret_val = -EOPNOTSUPP;
				break;
			default:
				ret_val = -EOPNOTSUPP;
			}
			if (ret_val == 0) {
				dum->devstatus |= (1 << w_value);
				*status = 0;
			}
		} else if (setup->bRequestType == Ep_Request) {
			/* endpoint halt */
			ep2 = find_endpoint(dum, w_index);
			if (!ep2 || ep2->ep.name == ep0name) {
				ret_val = -EOPNOTSUPP;
				break;
			}
			ep2->halted = 1;
			ret_val = 0;
			*status = 0;
		}
		break;
	case USB_REQ_CLEAR_FEATURE:
		if (setup->bRequestType == Dev_Request) {
			ret_val = 0;
			switch (w_value) {
			case USB_DEVICE_REMOTE_WAKEUP:
				w_value = USB_DEVICE_REMOTE_WAKEUP;
				break;
			case USB_DEVICE_U1_ENABLE:
				if (dummy_hcd_to_hcd(dum_hcd)->speed ==
				    HCD_USB3)
					w_value = USB_DEV_STAT_U1_ENABLED;
				else
					ret_val = -EOPNOTSUPP;
				break;
			case USB_DEVICE_U2_ENABLE:
				if (dummy_hcd_to_hcd(dum_hcd)->speed ==
				    HCD_USB3)
					w_value = USB_DEV_STAT_U2_ENABLED;
				else
					ret_val = -EOPNOTSUPP;
				break;
			case USB_DEVICE_LTM_ENABLE:
				if (dummy_hcd_to_hcd(dum_hcd)->speed ==
				    HCD_USB3)
					w_value = USB_DEV_STAT_LTM_ENABLED;
				else
					ret_val = -EOPNOTSUPP;
				break;
			default:
				ret_val = -EOPNOTSUPP;
				break;
			}
			if (ret_val == 0) {
				dum->devstatus &= ~(1 << w_value);
				*status = 0;
			}
		} else if (setup->bRequestType == Ep_Request) {
			/* endpoint halt */
			ep2 = find_endpoint(dum, w_index);
			if (!ep2) {
				ret_val = -EOPNOTSUPP;
				break;
			}
			if (!ep2->wedged)
				ep2->halted = 0;
			ret_val = 0;
			*status = 0;
		}
		break;
	case USB_REQ_GET_STATUS:
		if (setup->bRequestType == Dev_InRequest
				|| setup->bRequestType == Intf_InRequest
				|| setup->bRequestType == Ep_InRequest) {
			char *buf;
			/*
			 * device: remote wakeup, selfpowered
			 * interface: nothing
			 * endpoint: halt
			 */
			buf = (char *)urb->transfer_buffer;
			if (urb->transfer_buffer_length > 0) {
				if (setup->bRequestType == Ep_InRequest) {
					ep2 = find_endpoint(dum, w_index);
					if (!ep2) {
						ret_val = -EOPNOTSUPP;
						break;
					}
					buf[0] = ep2->halted;
				} else if (setup->bRequestType ==
					   Dev_InRequest) {
					buf[0] = (u8)dum->devstatus;
				} else
					buf[0] = 0;
			}
			if (urb->transfer_buffer_length > 1)
				buf[1] = 0;
			urb->actual_length = min_t(u32, 2,
				urb->transfer_buffer_length);
			ret_val = 0;
			*status = 0;
		}
		break;
	}
	return ret_val;
}

/* drive both sides of the transfers; looks like irq handlers to
 * both drivers except the callbacks aren't in_irq().
 */
static void dummy_timer(unsigned long _dum_hcd)
{
	struct dummy_hcd	*dum_hcd = (struct dummy_hcd *) _dum_hcd;
	struct dummy		*dum = dum_hcd->dum;
	struct urbp		*urbp, *tmp;
	unsigned long		flags;
	int			limit, total;
	int			i;

	/* simplistic model for one frame's bandwidth */
	switch (dum->gadget.speed) {
	case USB_SPEED_LOW:
		total = 8/*bytes*/ * 12/*packets*/;
		break;
	case USB_SPEED_FULL:
		total = 64/*bytes*/ * 19/*packets*/;
		break;
	case USB_SPEED_HIGH:
		total = 512/*bytes*/ * 13/*packets*/ * 8/*uframes*/;
		break;
	case USB_SPEED_SUPER:
		/* Bus speed is 500000 bytes/ms, so use a little less */
		total = 490000;
		break;
	default:
		dev_err(dummy_dev(dum_hcd), "bogus device speed\n");
		return;
	}

	/* FIXME if HZ != 1000 this will probably misbehave ... */

	/* look at each urb queued by the host side driver */
	spin_lock_irqsave(&dum->lock, flags);

	if (!dum_hcd->udev) {
		dev_err(dummy_dev(dum_hcd),
				"timer fired with no URBs pending?\n");
		spin_unlock_irqrestore(&dum->lock, flags);
		return;
	}

	for (i = 0; i < DUMMY_ENDPOINTS; i++) {
		if (!ep_name[i])
			break;
		dum->ep[i].already_seen = 0;
	}

restart:
	list_for_each_entry_safe(urbp, tmp, &dum_hcd->urbp_list, urbp_list) {
		struct urb		*urb;
		struct dummy_request	*req;
		u8			address;
		struct dummy_ep		*ep = NULL;
		int			type;
		int			status = -EINPROGRESS;

		urb = urbp->urb;
		if (urb->unlinked)
			goto return_urb;
		else if (dum_hcd->rh_state != DUMMY_RH_RUNNING)
			continue;
		type = usb_pipetype(urb->pipe);

		/* used up this frame's non-periodic bandwidth?
		 * FIXME there's infinite bandwidth for control and
		 * periodic transfers ... unrealistic.
		 */
		if (total <= 0 && type == PIPE_BULK)
			continue;

		/* find the gadget's ep for this request (if configured) */
		address = usb_pipeendpoint (urb->pipe);
		if (usb_pipein(urb->pipe))
			address |= USB_DIR_IN;
		ep = find_endpoint(dum, address);
		if (!ep) {
			/* set_configuration() disagreement */
			dev_dbg(dummy_dev(dum_hcd),
				"no ep configured for urb %p\n",
				urb);
			status = -EPROTO;
			goto return_urb;
		}

		if (ep->already_seen)
			continue;
		ep->already_seen = 1;
		if (ep == &dum->ep[0] && urb->error_count) {
			ep->setup_stage = 1;	/* a new urb */
			urb->error_count = 0;
		}
		if (ep->halted && !ep->setup_stage) {
			/* NOTE: must not be iso! */
			dev_dbg(dummy_dev(dum_hcd), "ep %s halted, urb %p\n",
					ep->ep.name, urb);
			status = -EPIPE;
			goto return_urb;
		}
		/* FIXME make sure both ends agree on maxpacket */

		/* handle control requests */
		if (ep == &dum->ep[0] && ep->setup_stage) {
			struct usb_ctrlrequest		setup;
			int				value = 1;

			setup = *(struct usb_ctrlrequest *) urb->setup_packet;
			/* paranoia, in case of stale queued data */
			list_for_each_entry(req, &ep->queue, queue) {
				list_del_init(&req->queue);
				req->req.status = -EOVERFLOW;
				dev_dbg(udc_dev(dum), "stale req = %p\n",
						req);

				spin_unlock(&dum->lock);
				req->req.complete(&ep->ep, &req->req);
				spin_lock(&dum->lock);
				ep->already_seen = 0;
				goto restart;
			}

			/* gadget driver never sees set_address or operations
			 * on standard feature flags.  some hardware doesn't
			 * even expose them.
			 */
			ep->last_io = jiffies;
			ep->setup_stage = 0;
			ep->halted = 0;

			value = handle_control_request(dum_hcd, urb, &setup,
						       &status);

			/* gadget driver handles all other requests.  block
			 * until setup() returns; no reentrancy issues etc.
			 */
			if (value > 0) {
				spin_unlock(&dum->lock);
				value = dum->driver->setup(&dum->gadget,
						&setup);
				spin_lock(&dum->lock);

				if (value >= 0) {
					/* no delays (max 64KB data stage) */
					limit = 64*1024;
					goto treat_control_like_bulk;
				}
				/* error, see below */
			}

			if (value < 0) {
				if (value != -EOPNOTSUPP)
					dev_dbg(udc_dev(dum),
						"setup --> %d\n",
						value);
				status = -EPIPE;
				urb->actual_length = 0;
			}

			goto return_urb;
		}

		/* non-control requests */
		limit = total;
		switch (usb_pipetype(urb->pipe)) {
		case PIPE_ISOCHRONOUS:
			/* FIXME is it urb->interval since the last xfer?
			 * use urb->iso_frame_desc[i].
			 * complete whether or not ep has requests queued.
			 * report random errors, to debug drivers.
			 */
			limit = max(limit, periodic_bytes(dum, ep));
			status = -ENOSYS;
			break;

		case PIPE_INTERRUPT:
			/* FIXME is it urb->interval since the last xfer?
			 * this almost certainly polls too fast.
			 */
			limit = max(limit, periodic_bytes(dum, ep));
			/* FALLTHROUGH */

		default:
treat_control_like_bulk:
			ep->last_io = jiffies;
			total = transfer(dum_hcd, urb, ep, limit, &status);
			break;
		}

		/* incomplete transfer? */
		if (status == -EINPROGRESS)
			continue;

return_urb:
		list_del(&urbp->urbp_list);
		kfree(urbp);
		if (ep)
			ep->already_seen = ep->setup_stage = 0;

		usb_hcd_unlink_urb_from_ep(dummy_hcd_to_hcd(dum_hcd), urb);
		spin_unlock(&dum->lock);
		usb_hcd_giveback_urb(dummy_hcd_to_hcd(dum_hcd), urb, status);
		spin_lock(&dum->lock);

		goto restart;
	}

	if (list_empty(&dum_hcd->urbp_list)) {
		usb_put_dev(dum_hcd->udev);
		dum_hcd->udev = NULL;
	} else if (dum_hcd->rh_state == DUMMY_RH_RUNNING) {
		/* want a 1 msec delay here */
		mod_timer(&dum_hcd->timer, jiffies + msecs_to_jiffies(1));
	}

	spin_unlock_irqrestore(&dum->lock, flags);
}

/*-------------------------------------------------------------------------*/

#define PORT_C_MASK \
	((USB_PORT_STAT_C_CONNECTION \
	| USB_PORT_STAT_C_ENABLE \
	| USB_PORT_STAT_C_SUSPEND \
	| USB_PORT_STAT_C_OVERCURRENT \
	| USB_PORT_STAT_C_RESET) << 16)

static int dummy_hub_status(struct usb_hcd *hcd, char *buf)
{
	struct dummy_hcd	*dum_hcd;
	unsigned long		flags;
	int			retval = 0;

	dum_hcd = hcd_to_dummy_hcd(hcd);

	spin_lock_irqsave(&dum_hcd->dum->lock, flags);
	if (!HCD_HW_ACCESSIBLE(hcd))
		goto done;

	if (dum_hcd->resuming && time_after_eq(jiffies, dum_hcd->re_timeout)) {
		dum_hcd->port_status |= (USB_PORT_STAT_C_SUSPEND << 16);
		dum_hcd->port_status &= ~USB_PORT_STAT_SUSPEND;
		set_link_state(dum_hcd);
	}

	if ((dum_hcd->port_status & PORT_C_MASK) != 0) {
		*buf = (1 << 1);
		dev_dbg(dummy_dev(dum_hcd), "port status 0x%08x has changes\n",
				dum_hcd->port_status);
		retval = 1;
		if (dum_hcd->rh_state == DUMMY_RH_SUSPENDED)
			usb_hcd_resume_root_hub(hcd);
	}
done:
	spin_unlock_irqrestore(&dum_hcd->dum->lock, flags);
	return retval;
}

/* usb 3.0 root hub device descriptor */
static struct {
	struct usb_bos_descriptor bos;
	struct usb_ss_cap_descriptor ss_cap;
} __packed usb3_bos_desc = {

	.bos = {
		.bLength		= USB_DT_BOS_SIZE,
		.bDescriptorType	= USB_DT_BOS,
		.wTotalLength		= cpu_to_le16(sizeof(usb3_bos_desc)),
		.bNumDeviceCaps		= 1,
	},
	.ss_cap = {
		.bLength		= USB_DT_USB_SS_CAP_SIZE,
		.bDescriptorType	= USB_DT_DEVICE_CAPABILITY,
		.bDevCapabilityType	= USB_SS_CAP_TYPE,
		.wSpeedSupported	= cpu_to_le16(USB_5GBPS_OPERATION),
		.bFunctionalitySupport	= ilog2(USB_5GBPS_OPERATION),
	},
};

static inline void
ss_hub_descriptor(struct usb_hub_descriptor *desc)
{
	memset(desc, 0, sizeof *desc);
	desc->bDescriptorType = 0x2a;
	desc->bDescLength = 12;
	desc->wHubCharacteristics = cpu_to_le16(0x0001);
	desc->bNbrPorts = 1;
	desc->u.ss.bHubHdrDecLat = 0x04; /* Worst case: 0.4 micro sec*/
	desc->u.ss.DeviceRemovable = 0xffff;
}

static inline void hub_descriptor(struct usb_hub_descriptor *desc)
{
	memset(desc, 0, sizeof *desc);
	desc->bDescriptorType = 0x29;
	desc->bDescLength = 9;
	desc->wHubCharacteristics = cpu_to_le16(0x0001);
	desc->bNbrPorts = 1;
	desc->u.hs.DeviceRemovable[0] = 0xff;
	desc->u.hs.DeviceRemovable[1] = 0xff;
}

static int dummy_hub_control(
	struct usb_hcd	*hcd,
	u16		typeReq,
	u16		wValue,
	u16		wIndex,
	char		*buf,
	u16		wLength
) {
	struct dummy_hcd *dum_hcd;
	int		retval = 0;
	unsigned long	flags;

	if (!HCD_HW_ACCESSIBLE(hcd))
		return -ETIMEDOUT;

	dum_hcd = hcd_to_dummy_hcd(hcd);

	spin_lock_irqsave(&dum_hcd->dum->lock, flags);
	switch (typeReq) {
	case ClearHubFeature:
		break;
	case ClearPortFeature:
		switch (wValue) {
		case USB_PORT_FEAT_SUSPEND:
			if (hcd->speed == HCD_USB3) {
				dev_dbg(dummy_dev(dum_hcd),
					 "USB_PORT_FEAT_SUSPEND req not "
					 "supported for USB 3.0 roothub\n");
				goto error;
			}
			if (dum_hcd->port_status & USB_PORT_STAT_SUSPEND) {
				/* 20msec resume signaling */
				dum_hcd->resuming = 1;
				dum_hcd->re_timeout = jiffies +
						msecs_to_jiffies(20);
			}
			break;
		case USB_PORT_FEAT_POWER:
			if (hcd->speed == HCD_USB3) {
				if (dum_hcd->port_status & USB_PORT_STAT_POWER)
					dev_dbg(dummy_dev(dum_hcd),
						"power-off\n");
			} else
				if (dum_hcd->port_status &
							USB_SS_PORT_STAT_POWER)
					dev_dbg(dummy_dev(dum_hcd),
						"power-off\n");
			/* FALLS THROUGH */
		default:
			dum_hcd->port_status &= ~(1 << wValue);
			set_link_state(dum_hcd);
		}
		break;
	case GetHubDescriptor:
		if (hcd->speed == HCD_USB3 &&
				(wLength < USB_DT_SS_HUB_SIZE ||
				 wValue != (USB_DT_SS_HUB << 8))) {
			dev_dbg(dummy_dev(dum_hcd),
				"Wrong hub descriptor type for "
				"USB 3.0 roothub.\n");
			goto error;
		}
		if (hcd->speed == HCD_USB3)
			ss_hub_descriptor((struct usb_hub_descriptor *) buf);
		else
			hub_descriptor((struct usb_hub_descriptor *) buf);
		break;

	case DeviceRequest | USB_REQ_GET_DESCRIPTOR:
		if (hcd->speed != HCD_USB3)
			goto error;

		if ((wValue >> 8) != USB_DT_BOS)
			goto error;

		memcpy(buf, &usb3_bos_desc, sizeof(usb3_bos_desc));
		retval = sizeof(usb3_bos_desc);
		break;

	case GetHubStatus:
		*(__le32 *) buf = cpu_to_le32(0);
		break;
	case GetPortStatus:
		if (wIndex != 1)
			retval = -EPIPE;

		/* whoever resets or resumes must GetPortStatus to
		 * complete it!!
		 */
		if (dum_hcd->resuming &&
				time_after_eq(jiffies, dum_hcd->re_timeout)) {
			dum_hcd->port_status |= (USB_PORT_STAT_C_SUSPEND << 16);
			dum_hcd->port_status &= ~USB_PORT_STAT_SUSPEND;
		}
		if ((dum_hcd->port_status & USB_PORT_STAT_RESET) != 0 &&
				time_after_eq(jiffies, dum_hcd->re_timeout)) {
			dum_hcd->port_status |= (USB_PORT_STAT_C_RESET << 16);
			dum_hcd->port_status &= ~USB_PORT_STAT_RESET;
			if (dum_hcd->dum->pullup) {
				dum_hcd->port_status |= USB_PORT_STAT_ENABLE;

				if (hcd->speed < HCD_USB3) {
					switch (dum_hcd->dum->gadget.speed) {
					case USB_SPEED_HIGH:
						dum_hcd->port_status |=
						      USB_PORT_STAT_HIGH_SPEED;
						break;
					case USB_SPEED_LOW:
						dum_hcd->dum->gadget.ep0->
							maxpacket = 8;
						dum_hcd->port_status |=
							USB_PORT_STAT_LOW_SPEED;
						break;
					default:
						dum_hcd->dum->gadget.speed =
							USB_SPEED_FULL;
						break;
					}
				}
			}
		}
		set_link_state(dum_hcd);
		((__le16 *) buf)[0] = cpu_to_le16(dum_hcd->port_status);
		((__le16 *) buf)[1] = cpu_to_le16(dum_hcd->port_status >> 16);
		break;
	case SetHubFeature:
		retval = -EPIPE;
		break;
	case SetPortFeature:
		switch (wValue) {
		case USB_PORT_FEAT_LINK_STATE:
			if (hcd->speed != HCD_USB3) {
				dev_dbg(dummy_dev(dum_hcd),
					 "USB_PORT_FEAT_LINK_STATE req not "
					 "supported for USB 2.0 roothub\n");
				goto error;
			}
			/*
			 * Since this is dummy we don't have an actual link so
			 * there is nothing to do for the SET_LINK_STATE cmd
			 */
			break;
		case USB_PORT_FEAT_U1_TIMEOUT:
		case USB_PORT_FEAT_U2_TIMEOUT:
			/* TODO: add suspend/resume support! */
			if (hcd->speed != HCD_USB3) {
				dev_dbg(dummy_dev(dum_hcd),
					 "USB_PORT_FEAT_U1/2_TIMEOUT req not "
					 "supported for USB 2.0 roothub\n");
				goto error;
			}
			break;
		case USB_PORT_FEAT_SUSPEND:
			/* Applicable only for USB2.0 hub */
			if (hcd->speed == HCD_USB3) {
				dev_dbg(dummy_dev(dum_hcd),
					 "USB_PORT_FEAT_SUSPEND req not "
					 "supported for USB 3.0 roothub\n");
				goto error;
			}
			if (dum_hcd->active) {
				dum_hcd->port_status |= USB_PORT_STAT_SUSPEND;

				/* HNP would happen here; for now we
				 * assume b_bus_req is always true.
				 */
				set_link_state(dum_hcd);
				if (((1 << USB_DEVICE_B_HNP_ENABLE)
						& dum_hcd->dum->devstatus) != 0)
					dev_dbg(dummy_dev(dum_hcd),
							"no HNP yet!\n");
			}
			break;
		case USB_PORT_FEAT_POWER:
			if (hcd->speed == HCD_USB3)
				dum_hcd->port_status |= USB_SS_PORT_STAT_POWER;
			else
				dum_hcd->port_status |= USB_PORT_STAT_POWER;
			set_link_state(dum_hcd);
			break;
		case USB_PORT_FEAT_BH_PORT_RESET:
			/* Applicable only for USB3.0 hub */
			if (hcd->speed != HCD_USB3) {
				dev_dbg(dummy_dev(dum_hcd),
					 "USB_PORT_FEAT_BH_PORT_RESET req not "
					 "supported for USB 2.0 roothub\n");
				goto error;
			}
			/* FALLS THROUGH */
		case USB_PORT_FEAT_RESET:
			/* if it's already enabled, disable */
			if (hcd->speed == HCD_USB3) {
				dum_hcd->port_status = 0;
				dum_hcd->port_status =
					(USB_SS_PORT_STAT_POWER |
					 USB_PORT_STAT_CONNECTION |
					 USB_PORT_STAT_RESET);
			} else
				dum_hcd->port_status &= ~(USB_PORT_STAT_ENABLE
					| USB_PORT_STAT_LOW_SPEED
					| USB_PORT_STAT_HIGH_SPEED);
			/*
			 * We want to reset device status. All but the
			 * Self powered feature
			 */
			dum_hcd->dum->devstatus &=
				(1 << USB_DEVICE_SELF_POWERED);
			/*
			 * FIXME USB3.0: what is the correct reset signaling
			 * interval? Is it still 50msec as for HS?
			 */
			dum_hcd->re_timeout = jiffies + msecs_to_jiffies(50);
			/* FALLS THROUGH */
		default:
			if (hcd->speed == HCD_USB3) {
				if ((dum_hcd->port_status &
				     USB_SS_PORT_STAT_POWER) != 0) {
					dum_hcd->port_status |= (1 << wValue);
					set_link_state(dum_hcd);
				}
			} else
				if ((dum_hcd->port_status &
				     USB_PORT_STAT_POWER) != 0) {
					dum_hcd->port_status |= (1 << wValue);
					set_link_state(dum_hcd);
				}
		}
		break;
	case GetPortErrorCount:
		if (hcd->speed != HCD_USB3) {
			dev_dbg(dummy_dev(dum_hcd),
				 "GetPortErrorCount req not "
				 "supported for USB 2.0 roothub\n");
			goto error;
		}
		/* We'll always return 0 since this is a dummy hub */
		*(__le32 *) buf = cpu_to_le32(0);
		break;
	case SetHubDepth:
		if (hcd->speed != HCD_USB3) {
			dev_dbg(dummy_dev(dum_hcd),
				 "SetHubDepth req not supported for "
				 "USB 2.0 roothub\n");
			goto error;
		}
		break;
	default:
		dev_dbg(dummy_dev(dum_hcd),
			"hub control req%04x v%04x i%04x l%d\n",
			typeReq, wValue, wIndex, wLength);
error:
		/* "protocol stall" on error */
		retval = -EPIPE;
	}
	spin_unlock_irqrestore(&dum_hcd->dum->lock, flags);

	if ((dum_hcd->port_status & PORT_C_MASK) != 0)
		usb_hcd_poll_rh_status(hcd);
	return retval;
}

static int dummy_bus_suspend(struct usb_hcd *hcd)
{
	struct dummy_hcd *dum_hcd = hcd_to_dummy_hcd(hcd);

	dev_dbg(&hcd->self.root_hub->dev, "%s\n", __func__);

	spin_lock_irq(&dum_hcd->dum->lock);
	dum_hcd->rh_state = DUMMY_RH_SUSPENDED;
	set_link_state(dum_hcd);
	hcd->state = HC_STATE_SUSPENDED;
	spin_unlock_irq(&dum_hcd->dum->lock);
	return 0;
}

static int dummy_bus_resume(struct usb_hcd *hcd)
{
	struct dummy_hcd *dum_hcd = hcd_to_dummy_hcd(hcd);
	int rc = 0;

	dev_dbg(&hcd->self.root_hub->dev, "%s\n", __func__);

	spin_lock_irq(&dum_hcd->dum->lock);
	if (!HCD_HW_ACCESSIBLE(hcd)) {
		rc = -ESHUTDOWN;
	} else {
		dum_hcd->rh_state = DUMMY_RH_RUNNING;
		set_link_state(dum_hcd);
		if (!list_empty(&dum_hcd->urbp_list))
			mod_timer(&dum_hcd->timer, jiffies);
		hcd->state = HC_STATE_RUNNING;
	}
	spin_unlock_irq(&dum_hcd->dum->lock);
	return rc;
}

/*-------------------------------------------------------------------------*/

static inline ssize_t show_urb(char *buf, size_t size, struct urb *urb)
{
	int ep = usb_pipeendpoint(urb->pipe);

	return snprintf(buf, size,
		"urb/%p %s ep%d%s%s len %d/%d\n",
		urb,
		({ char *s;
		switch (urb->dev->speed) {
		case USB_SPEED_LOW:
			s = "ls";
			break;
		case USB_SPEED_FULL:
			s = "fs";
			break;
		case USB_SPEED_HIGH:
			s = "hs";
			break;
		case USB_SPEED_SUPER:
			s = "ss";
			break;
		default:
			s = "?";
			break;
		 }; s; }),
		ep, ep ? (usb_pipein(urb->pipe) ? "in" : "out") : "",
		({ char *s; \
		switch (usb_pipetype(urb->pipe)) { \
		case PIPE_CONTROL: \
			s = ""; \
			break; \
		case PIPE_BULK: \
			s = "-bulk"; \
			break; \
		case PIPE_INTERRUPT: \
			s = "-int"; \
			break; \
		default: \
			s = "-iso"; \
			break; \
		}; s; }),
		urb->actual_length, urb->transfer_buffer_length);
}

static ssize_t show_urbs(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct usb_hcd		*hcd = dev_get_drvdata(dev);
	struct dummy_hcd	*dum_hcd = hcd_to_dummy_hcd(hcd);
	struct urbp		*urbp;
	size_t			size = 0;
	unsigned long		flags;

	spin_lock_irqsave(&dum_hcd->dum->lock, flags);
	list_for_each_entry(urbp, &dum_hcd->urbp_list, urbp_list) {
		size_t		temp;

		temp = show_urb(buf, PAGE_SIZE - size, urbp->urb);
		buf += temp;
		size += temp;
	}
	spin_unlock_irqrestore(&dum_hcd->dum->lock, flags);

	return size;
}
static DEVICE_ATTR(urbs, S_IRUGO, show_urbs, NULL);

static int dummy_start_ss(struct dummy_hcd *dum_hcd)
{
	init_timer(&dum_hcd->timer);
	dum_hcd->timer.function = dummy_timer;
	dum_hcd->timer.data = (unsigned long)dum_hcd;
	dum_hcd->rh_state = DUMMY_RH_RUNNING;
	dum_hcd->stream_en_ep = 0;
	INIT_LIST_HEAD(&dum_hcd->urbp_list);
	dummy_hcd_to_hcd(dum_hcd)->power_budget = POWER_BUDGET;
	dummy_hcd_to_hcd(dum_hcd)->state = HC_STATE_RUNNING;
	dummy_hcd_to_hcd(dum_hcd)->uses_new_polling = 1;
#ifdef CONFIG_USB_OTG
	dummy_hcd_to_hcd(dum_hcd)->self.otg_port = 1;
#endif
	return 0;

	/* FIXME 'urbs' should be a per-device thing, maybe in usbcore */
	return device_create_file(dummy_dev(dum_hcd), &dev_attr_urbs);
}

static int dummy_start(struct usb_hcd *hcd)
{
	struct dummy_hcd	*dum_hcd = hcd_to_dummy_hcd(hcd);

	/*
	 * MASTER side init ... we emulate a root hub that'll only ever
	 * talk to one device (the slave side).  Also appears in sysfs,
	 * just like more familiar pci-based HCDs.
	 */
	if (!usb_hcd_is_primary_hcd(hcd))
		return dummy_start_ss(dum_hcd);

	spin_lock_init(&dum_hcd->dum->lock);
	init_timer(&dum_hcd->timer);
	dum_hcd->timer.function = dummy_timer;
	dum_hcd->timer.data = (unsigned long)dum_hcd;
	dum_hcd->rh_state = DUMMY_RH_RUNNING;

	INIT_LIST_HEAD(&dum_hcd->urbp_list);

	hcd->power_budget = POWER_BUDGET;
	hcd->state = HC_STATE_RUNNING;
	hcd->uses_new_polling = 1;

#ifdef CONFIG_USB_OTG
	hcd->self.otg_port = 1;
#endif

	/* FIXME 'urbs' should be a per-device thing, maybe in usbcore */
	return device_create_file(dummy_dev(dum_hcd), &dev_attr_urbs);
}

static void dummy_stop(struct usb_hcd *hcd)
{
	struct dummy		*dum;

	dum = hcd_to_dummy_hcd(hcd)->dum;
	device_remove_file(dummy_dev(hcd_to_dummy_hcd(hcd)), &dev_attr_urbs);
	usb_gadget_unregister_driver(dum->driver);
	dev_info(dummy_dev(hcd_to_dummy_hcd(hcd)), "stopped\n");
}

/*-------------------------------------------------------------------------*/

static int dummy_h_get_frame(struct usb_hcd *hcd)
{
	return dummy_g_get_frame(NULL);
}

static int dummy_setup(struct usb_hcd *hcd)
{
	struct dummy *dum;

	dum = *((void **)dev_get_platdata(hcd->self.controller));
	hcd->self.sg_tablesize = ~0;
	if (usb_hcd_is_primary_hcd(hcd)) {
		dum->hs_hcd = hcd_to_dummy_hcd(hcd);
		dum->hs_hcd->dum = dum;
		/*
		 * Mark the first roothub as being USB 2.0.
		 * The USB 3.0 roothub will be registered later by
		 * dummy_hcd_probe()
		 */
		hcd->speed = HCD_USB2;
		hcd->self.root_hub->speed = USB_SPEED_HIGH;
	} else {
		dum->ss_hcd = hcd_to_dummy_hcd(hcd);
		dum->ss_hcd->dum = dum;
		hcd->speed = HCD_USB3;
		hcd->self.root_hub->speed = USB_SPEED_SUPER;
	}
	return 0;
}

/* Change a group of bulk endpoints to support multiple stream IDs */
static int dummy_alloc_streams(struct usb_hcd *hcd, struct usb_device *udev,
	struct usb_host_endpoint **eps, unsigned int num_eps,
	unsigned int num_streams, gfp_t mem_flags)
{
	struct dummy_hcd *dum_hcd = hcd_to_dummy_hcd(hcd);
	unsigned long flags;
	int max_stream;
	int ret_streams = num_streams;
	unsigned int index;
	unsigned int i;

	if (!num_eps)
		return -EINVAL;

	spin_lock_irqsave(&dum_hcd->dum->lock, flags);
	for (i = 0; i < num_eps; i++) {
		index = dummy_get_ep_idx(&eps[i]->desc);
		if ((1 << index) & dum_hcd->stream_en_ep) {
			ret_streams = -EINVAL;
			goto out;
		}
		max_stream = usb_ss_max_streams(&eps[i]->ss_ep_comp);
		if (!max_stream) {
			ret_streams = -EINVAL;
			goto out;
		}
		if (max_stream < ret_streams) {
			dev_dbg(dummy_dev(dum_hcd), "Ep 0x%x only supports %u "
					"stream IDs.\n",
					eps[i]->desc.bEndpointAddress,
					max_stream);
			ret_streams = max_stream;
		}
	}

	for (i = 0; i < num_eps; i++) {
		index = dummy_get_ep_idx(&eps[i]->desc);
		dum_hcd->stream_en_ep |= 1 << index;
		set_max_streams_for_pipe(dum_hcd,
				usb_endpoint_num(&eps[i]->desc), ret_streams);
	}
out:
	spin_unlock_irqrestore(&dum_hcd->dum->lock, flags);
	return ret_streams;
}

/* Reverts a group of bulk endpoints back to not using stream IDs. */
static int dummy_free_streams(struct usb_hcd *hcd, struct usb_device *udev,
	struct usb_host_endpoint **eps, unsigned int num_eps,
	gfp_t mem_flags)
{
	struct dummy_hcd *dum_hcd = hcd_to_dummy_hcd(hcd);
	unsigned long flags;
	int ret;
	unsigned int index;
	unsigned int i;

	spin_lock_irqsave(&dum_hcd->dum->lock, flags);
	for (i = 0; i < num_eps; i++) {
		index = dummy_get_ep_idx(&eps[i]->desc);
		if (!((1 << index) & dum_hcd->stream_en_ep)) {
			ret = -EINVAL;
			goto out;
		}
	}

	for (i = 0; i < num_eps; i++) {
		index = dummy_get_ep_idx(&eps[i]->desc);
		dum_hcd->stream_en_ep &= ~(1 << index);
		set_max_streams_for_pipe(dum_hcd,
				usb_endpoint_num(&eps[i]->desc), 0);
	}
	ret = 0;
out:
	spin_unlock_irqrestore(&dum_hcd->dum->lock, flags);
	return ret;
}

static struct hc_driver dummy_hcd = {
	.description =		(char *) driver_name,
	.product_desc =		"Dummy host controller",
	.hcd_priv_size =	sizeof(struct dummy_hcd),

	.flags =		HCD_USB3 | HCD_SHARED,

	.reset =		dummy_setup,
	.start =		dummy_start,
	.stop =			dummy_stop,

	.urb_enqueue =		dummy_urb_enqueue,
	.urb_dequeue =		dummy_urb_dequeue,

	.get_frame_number =	dummy_h_get_frame,

	.hub_status_data =	dummy_hub_status,
	.hub_control =		dummy_hub_control,
	.bus_suspend =		dummy_bus_suspend,
	.bus_resume =		dummy_bus_resume,

	.alloc_streams =	dummy_alloc_streams,
	.free_streams =		dummy_free_streams,
};

static int dummy_hcd_probe(struct platform_device *pdev)
{
	struct dummy		*dum;
	struct usb_hcd		*hs_hcd;
	struct usb_hcd		*ss_hcd;
	int			retval;

	dev_info(&pdev->dev, "%s, driver " DRIVER_VERSION "\n", driver_desc);
	dum = *((void **)dev_get_platdata(&pdev->dev));

	if (!mod_data.is_super_speed)
		dummy_hcd.flags = HCD_USB2;
	hs_hcd = usb_create_hcd(&dummy_hcd, &pdev->dev, dev_name(&pdev->dev));
	if (!hs_hcd)
		return -ENOMEM;
	hs_hcd->has_tt = 1;

	retval = usb_add_hcd(hs_hcd, 0, 0);
	if (retval)
		goto put_usb2_hcd;

	if (mod_data.is_super_speed) {
		ss_hcd = usb_create_shared_hcd(&dummy_hcd, &pdev->dev,
					dev_name(&pdev->dev), hs_hcd);
		if (!ss_hcd) {
			retval = -ENOMEM;
			goto dealloc_usb2_hcd;
		}

		retval = usb_add_hcd(ss_hcd, 0, 0);
		if (retval)
			goto put_usb3_hcd;
	}
	return 0;

put_usb3_hcd:
	usb_put_hcd(ss_hcd);
dealloc_usb2_hcd:
	usb_remove_hcd(hs_hcd);
put_usb2_hcd:
	usb_put_hcd(hs_hcd);
	dum->hs_hcd = dum->ss_hcd = NULL;
	return retval;
}

static int dummy_hcd_remove(struct platform_device *pdev)
{
	struct dummy		*dum;

	dum = hcd_to_dummy_hcd(platform_get_drvdata(pdev))->dum;

	if (dum->ss_hcd) {
		usb_remove_hcd(dummy_hcd_to_hcd(dum->ss_hcd));
		usb_put_hcd(dummy_hcd_to_hcd(dum->ss_hcd));
	}

	usb_remove_hcd(dummy_hcd_to_hcd(dum->hs_hcd));
	usb_put_hcd(dummy_hcd_to_hcd(dum->hs_hcd));

	dum->hs_hcd = NULL;
	dum->ss_hcd = NULL;

	return 0;
}

static int dummy_hcd_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct usb_hcd		*hcd;
	struct dummy_hcd	*dum_hcd;
	int			rc = 0;

	dev_dbg(&pdev->dev, "%s\n", __func__);

	hcd = platform_get_drvdata(pdev);
	dum_hcd = hcd_to_dummy_hcd(hcd);
	if (dum_hcd->rh_state == DUMMY_RH_RUNNING) {
		dev_warn(&pdev->dev, "Root hub isn't suspended!\n");
		rc = -EBUSY;
	} else
		clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
	return rc;
}

static int dummy_hcd_resume(struct platform_device *pdev)
{
	struct usb_hcd		*hcd;

	dev_dbg(&pdev->dev, "%s\n", __func__);

	hcd = platform_get_drvdata(pdev);
	set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
	usb_hcd_poll_rh_status(hcd);
	return 0;
}

static struct platform_driver dummy_hcd_driver = {
	.probe		= dummy_hcd_probe,
	.remove		= dummy_hcd_remove,
	.suspend	= dummy_hcd_suspend,
	.resume		= dummy_hcd_resume,
	.driver		= {
		.name	= (char *) driver_name,
		.owner	= THIS_MODULE,
	},
};

/*-------------------------------------------------------------------------*/
#define MAX_NUM_UDC	2
static struct platform_device *the_udc_pdev[MAX_NUM_UDC];
static struct platform_device *the_hcd_pdev[MAX_NUM_UDC];

static int __init init(void)
{
	int	retval = -ENOMEM;
	int	i;
	struct	dummy *dum[MAX_NUM_UDC];

	if (usb_disabled())
		return -ENODEV;

	if (!mod_data.is_high_speed && mod_data.is_super_speed)
		return -EINVAL;

	if (mod_data.num < 1 || mod_data.num > MAX_NUM_UDC) {
		pr_err("Number of emulated UDC must be in range of 1…%d\n",
				MAX_NUM_UDC);
		return -EINVAL;
	}

	for (i = 0; i < mod_data.num; i++) {
		the_hcd_pdev[i] = platform_device_alloc(driver_name, i);
		if (!the_hcd_pdev[i]) {
			i--;
			while (i >= 0)
				platform_device_put(the_hcd_pdev[i--]);
			return retval;
		}
	}
	for (i = 0; i < mod_data.num; i++) {
		the_udc_pdev[i] = platform_device_alloc(gadget_name, i);
		if (!the_udc_pdev[i]) {
			i--;
			while (i >= 0)
				platform_device_put(the_udc_pdev[i--]);
			goto err_alloc_udc;
		}
	}
	for (i = 0; i < mod_data.num; i++) {
		dum[i] = kzalloc(sizeof(struct dummy), GFP_KERNEL);
		if (!dum[i]) {
			retval = -ENOMEM;
			goto err_add_pdata;
		}
		retval = platform_device_add_data(the_hcd_pdev[i], &dum[i],
				sizeof(void *));
		if (retval)
			goto err_add_pdata;
		retval = platform_device_add_data(the_udc_pdev[i], &dum[i],
				sizeof(void *));
		if (retval)
			goto err_add_pdata;
	}

	retval = platform_driver_register(&dummy_hcd_driver);
	if (retval < 0)
		goto err_add_pdata;
	retval = platform_driver_register(&dummy_udc_driver);
	if (retval < 0)
		goto err_register_udc_driver;

	for (i = 0; i < mod_data.num; i++) {
		retval = platform_device_add(the_hcd_pdev[i]);
		if (retval < 0) {
			i--;
			while (i >= 0)
				platform_device_del(the_hcd_pdev[i--]);
			goto err_add_hcd;
		}
	}
	for (i = 0; i < mod_data.num; i++) {
		if (!dum[i]->hs_hcd ||
				(!dum[i]->ss_hcd && mod_data.is_super_speed)) {
			/*
			 * The hcd was added successfully but its probe
			 * function failed for some reason.
			 */
			retval = -EINVAL;
			goto err_add_udc;
		}
	}

	for (i = 0; i < mod_data.num; i++) {
		retval = platform_device_add(the_udc_pdev[i]);
		if (retval < 0) {
			i--;
			while (i >= 0)
				platform_device_del(the_udc_pdev[i]);
			goto err_add_udc;
		}
	}

	for (i = 0; i < mod_data.num; i++) {
		if (!platform_get_drvdata(the_udc_pdev[i])) {
			/*
			 * The udc was added successfully but its probe
			 * function failed for some reason.
			 */
			retval = -EINVAL;
			goto err_probe_udc;
		}
	}
	return retval;

err_probe_udc:
	for (i = 0; i < mod_data.num; i++)
		platform_device_del(the_udc_pdev[i]);
err_add_udc:
	for (i = 0; i < mod_data.num; i++)
		platform_device_del(the_hcd_pdev[i]);
err_add_hcd:
	platform_driver_unregister(&dummy_udc_driver);
err_register_udc_driver:
	platform_driver_unregister(&dummy_hcd_driver);
err_add_pdata:
	for (i = 0; i < mod_data.num; i++)
		kfree(dum[i]);
	for (i = 0; i < mod_data.num; i++)
		platform_device_put(the_udc_pdev[i]);
err_alloc_udc:
	for (i = 0; i < mod_data.num; i++)
		platform_device_put(the_hcd_pdev[i]);
	return retval;
}
module_init(init);

static void __exit cleanup(void)
{
	int i;

	for (i = 0; i < mod_data.num; i++) {
		struct dummy *dum;

		dum = *((void **)dev_get_platdata(&the_udc_pdev[i]->dev));

		platform_device_unregister(the_udc_pdev[i]);
		platform_device_unregister(the_hcd_pdev[i]);
		kfree(dum);
	}
	platform_driver_unregister(&dummy_udc_driver);
	platform_driver_unregister(&dummy_hcd_driver);
}
module_exit(cleanup);
