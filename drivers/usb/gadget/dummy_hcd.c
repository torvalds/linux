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

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/unaligned.h>


#include "../core/hcd.h"


#define DRIVER_DESC	"USB Host+Gadget Emulator"
#define DRIVER_VERSION	"02 May 2005"

#define POWER_BUDGET	500	/* in mA; use 8 for low-power port testing */

static const char	driver_name [] = "dummy_hcd";
static const char	driver_desc [] = "USB Host+Gadget Emulator";

static const char	gadget_name [] = "dummy_udc";

MODULE_DESCRIPTION (DRIVER_DESC);
MODULE_AUTHOR ("David Brownell");
MODULE_LICENSE ("GPL");

/*-------------------------------------------------------------------------*/

/* gadget side driver data structres */
struct dummy_ep {
	struct list_head		queue;
	unsigned long			last_io;	/* jiffies timestamp */
	struct usb_gadget		*gadget;
	const struct usb_endpoint_descriptor *desc;
	struct usb_ep			ep;
	unsigned			halted : 1;
	unsigned			wedged : 1;
	unsigned			already_seen : 1;
	unsigned			setup_stage : 1;
};

struct dummy_request {
	struct list_head		queue;		/* ep's requests */
	struct usb_request		req;
};

static inline struct dummy_ep *usb_ep_to_dummy_ep (struct usb_ep *_ep)
{
	return container_of (_ep, struct dummy_ep, ep);
}

static inline struct dummy_request *usb_request_to_dummy_request
		(struct usb_request *_req)
{
	return container_of (_req, struct dummy_request, req);
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

static const char ep0name [] = "ep0";

static const char *const ep_name [] = {
	ep0name,				/* everyone has ep0 */

	/* act like a net2280: high speed, six configurable endpoints */
	"ep-a", "ep-b", "ep-c", "ep-d", "ep-e", "ep-f",

	/* or like pxa250: fifteen fixed function endpoints */
	"ep1in-bulk", "ep2out-bulk", "ep3in-iso", "ep4out-iso", "ep5in-int",
	"ep6in-bulk", "ep7out-bulk", "ep8in-iso", "ep9out-iso", "ep10in-int",
	"ep11in-bulk", "ep12out-bulk", "ep13in-iso", "ep14out-iso",
		"ep15in-int",

	/* or like sa1100: two fixed function endpoints */
	"ep1out-bulk", "ep2in-bulk",
};
#define DUMMY_ENDPOINTS	ARRAY_SIZE(ep_name)

/*-------------------------------------------------------------------------*/

#define FIFO_SIZE		64

struct urbp {
	struct urb		*urb;
	struct list_head	urbp_list;
};


enum dummy_rh_state {
	DUMMY_RH_RESET,
	DUMMY_RH_SUSPENDED,
	DUMMY_RH_RUNNING
};

struct dummy {
	spinlock_t			lock;

	/*
	 * SLAVE/GADGET side support
	 */
	struct dummy_ep			ep [DUMMY_ENDPOINTS];
	int				address;
	struct usb_gadget		gadget;
	struct usb_gadget_driver	*driver;
	struct dummy_request		fifo_req;
	u8				fifo_buf [FIFO_SIZE];
	u16				devstatus;
	unsigned			udc_suspended:1;
	unsigned			pullup:1;
	unsigned			active:1;
	unsigned			old_active:1;

	/*
	 * MASTER/HOST side support
	 */
	enum dummy_rh_state		rh_state;
	struct timer_list		timer;
	u32				port_status;
	u32				old_status;
	unsigned			resuming:1;
	unsigned long			re_timeout;

	struct usb_device		*udev;
	struct list_head		urbp_list;
};

static inline struct dummy *hcd_to_dummy (struct usb_hcd *hcd)
{
	return (struct dummy *) (hcd->hcd_priv);
}

static inline struct usb_hcd *dummy_to_hcd (struct dummy *dum)
{
	return container_of((void *) dum, struct usb_hcd, hcd_priv);
}

static inline struct device *dummy_dev (struct dummy *dum)
{
	return dummy_to_hcd(dum)->self.controller;
}

static inline struct device *udc_dev (struct dummy *dum)
{
	return dum->gadget.dev.parent;
}

static inline struct dummy *ep_to_dummy (struct dummy_ep *ep)
{
	return container_of (ep->gadget, struct dummy, gadget);
}

static inline struct dummy *gadget_to_dummy (struct usb_gadget *gadget)
{
	return container_of (gadget, struct dummy, gadget);
}

static inline struct dummy *gadget_dev_to_dummy (struct device *dev)
{
	return container_of (dev, struct dummy, gadget.dev);
}

static struct dummy			*the_controller;

/*-------------------------------------------------------------------------*/

/* SLAVE/GADGET SIDE UTILITY ROUTINES */

/* called with spinlock held */
static void nuke (struct dummy *dum, struct dummy_ep *ep)
{
	while (!list_empty (&ep->queue)) {
		struct dummy_request	*req;

		req = list_entry (ep->queue.next, struct dummy_request, queue);
		list_del_init (&req->queue);
		req->req.status = -ESHUTDOWN;

		spin_unlock (&dum->lock);
		req->req.complete (&ep->ep, &req->req);
		spin_lock (&dum->lock);
	}
}

/* caller must hold lock */
static void
stop_activity (struct dummy *dum)
{
	struct dummy_ep	*ep;

	/* prevent any more requests */
	dum->address = 0;

	/* The timer is left running so that outstanding URBs can fail */

	/* nuke any pending requests first, so driver i/o is quiesced */
	list_for_each_entry (ep, &dum->gadget.ep_list, ep.ep_list)
		nuke (dum, ep);

	/* driver now does any non-usb quiescing necessary */
}

/* caller must hold lock */
static void
set_link_state (struct dummy *dum)
{
	dum->active = 0;
	if ((dum->port_status & USB_PORT_STAT_POWER) == 0)
		dum->port_status = 0;

	/* UDC suspend must cause a disconnect */
	else if (!dum->pullup || dum->udc_suspended) {
		dum->port_status &= ~(USB_PORT_STAT_CONNECTION |
					USB_PORT_STAT_ENABLE |
					USB_PORT_STAT_LOW_SPEED |
					USB_PORT_STAT_HIGH_SPEED |
					USB_PORT_STAT_SUSPEND);
		if ((dum->old_status & USB_PORT_STAT_CONNECTION) != 0)
			dum->port_status |= (USB_PORT_STAT_C_CONNECTION << 16);
	} else {
		dum->port_status |= USB_PORT_STAT_CONNECTION;
		if ((dum->old_status & USB_PORT_STAT_CONNECTION) == 0)
			dum->port_status |= (USB_PORT_STAT_C_CONNECTION << 16);
		if ((dum->port_status & USB_PORT_STAT_ENABLE) == 0)
			dum->port_status &= ~USB_PORT_STAT_SUSPEND;
		else if ((dum->port_status & USB_PORT_STAT_SUSPEND) == 0 &&
				dum->rh_state != DUMMY_RH_SUSPENDED)
			dum->active = 1;
	}

	if ((dum->port_status & USB_PORT_STAT_ENABLE) == 0 || dum->active)
		dum->resuming = 0;

	if ((dum->port_status & USB_PORT_STAT_CONNECTION) == 0 ||
			(dum->port_status & USB_PORT_STAT_RESET) != 0) {
		if ((dum->old_status & USB_PORT_STAT_CONNECTION) != 0 &&
				(dum->old_status & USB_PORT_STAT_RESET) == 0 &&
				dum->driver) {
			stop_activity (dum);
			spin_unlock (&dum->lock);
			dum->driver->disconnect (&dum->gadget);
			spin_lock (&dum->lock);
		}
	} else if (dum->active != dum->old_active) {
		if (dum->old_active && dum->driver->suspend) {
			spin_unlock (&dum->lock);
			dum->driver->suspend (&dum->gadget);
			spin_lock (&dum->lock);
		} else if (!dum->old_active && dum->driver->resume) {
			spin_unlock (&dum->lock);
			dum->driver->resume (&dum->gadget);
			spin_lock (&dum->lock);
		}
	}

	dum->old_status = dum->port_status;
	dum->old_active = dum->active;
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

static int
dummy_enable (struct usb_ep *_ep, const struct usb_endpoint_descriptor *desc)
{
	struct dummy		*dum;
	struct dummy_ep		*ep;
	unsigned		max;
	int			retval;

	ep = usb_ep_to_dummy_ep (_ep);
	if (!_ep || !desc || ep->desc || _ep->name == ep0name
			|| desc->bDescriptorType != USB_DT_ENDPOINT)
		return -EINVAL;
	dum = ep_to_dummy (ep);
	if (!dum->driver || !is_enabled (dum))
		return -ESHUTDOWN;
	max = le16_to_cpu(desc->wMaxPacketSize) & 0x3ff;

	/* drivers must not request bad settings, since lower levels
	 * (hardware or its drivers) may not check.  some endpoints
	 * can't do iso, many have maxpacket limitations, etc.
	 *
	 * since this "hardware" driver is here to help debugging, we
	 * have some extra sanity checks.  (there could be more though,
	 * especially for "ep9out" style fixed function ones.)
	 */
	retval = -EINVAL;
	switch (desc->bmAttributes & 0x03) {
	case USB_ENDPOINT_XFER_BULK:
		if (strstr (ep->ep.name, "-iso")
				|| strstr (ep->ep.name, "-int")) {
			goto done;
		}
		switch (dum->gadget.speed) {
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
		if (strstr (ep->ep.name, "-iso")) /* bulk is ok */
			goto done;
		/* real hardware might not handle all packet sizes */
		switch (dum->gadget.speed) {
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
		if (strstr (ep->ep.name, "-bulk")
				|| strstr (ep->ep.name, "-int"))
			goto done;
		/* real hardware might not handle all packet sizes */
		switch (dum->gadget.speed) {
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
	ep->desc = desc;

	dev_dbg (udc_dev(dum), "enabled %s (ep%d%s-%s) maxpacket %d\n",
		_ep->name,
		desc->bEndpointAddress & 0x0f,
		(desc->bEndpointAddress & USB_DIR_IN) ? "in" : "out",
		({ char *val;
		 switch (desc->bmAttributes & 0x03) {
		 case USB_ENDPOINT_XFER_BULK: val = "bulk"; break;
		 case USB_ENDPOINT_XFER_ISOC: val = "iso"; break;
		 case USB_ENDPOINT_XFER_INT: val = "intr"; break;
		 default: val = "ctrl"; break;
		 }; val; }),
		max);

	/* at this point real hardware should be NAKing transfers
	 * to that endpoint, until a buffer is queued to it.
	 */
	ep->halted = ep->wedged = 0;
	retval = 0;
done:
	return retval;
}

static int dummy_disable (struct usb_ep *_ep)
{
	struct dummy_ep		*ep;
	struct dummy		*dum;
	unsigned long		flags;
	int			retval;

	ep = usb_ep_to_dummy_ep (_ep);
	if (!_ep || !ep->desc || _ep->name == ep0name)
		return -EINVAL;
	dum = ep_to_dummy (ep);

	spin_lock_irqsave (&dum->lock, flags);
	ep->desc = NULL;
	retval = 0;
	nuke (dum, ep);
	spin_unlock_irqrestore (&dum->lock, flags);

	dev_dbg (udc_dev(dum), "disabled %s\n", _ep->name);
	return retval;
}

static struct usb_request *
dummy_alloc_request (struct usb_ep *_ep, gfp_t mem_flags)
{
	struct dummy_ep		*ep;
	struct dummy_request	*req;

	if (!_ep)
		return NULL;
	ep = usb_ep_to_dummy_ep (_ep);

	req = kzalloc(sizeof(*req), mem_flags);
	if (!req)
		return NULL;
	INIT_LIST_HEAD (&req->queue);
	return &req->req;
}

static void
dummy_free_request (struct usb_ep *_ep, struct usb_request *_req)
{
	struct dummy_ep		*ep;
	struct dummy_request	*req;

	ep = usb_ep_to_dummy_ep (_ep);
	if (!ep || !_req || (!ep->desc && _ep->name != ep0name))
		return;

	req = usb_request_to_dummy_request (_req);
	WARN_ON (!list_empty (&req->queue));
	kfree (req);
}

static void
fifo_complete (struct usb_ep *ep, struct usb_request *req)
{
}

static int
dummy_queue (struct usb_ep *_ep, struct usb_request *_req,
		gfp_t mem_flags)
{
	struct dummy_ep		*ep;
	struct dummy_request	*req;
	struct dummy		*dum;
	unsigned long		flags;

	req = usb_request_to_dummy_request (_req);
	if (!_req || !list_empty (&req->queue) || !_req->complete)
		return -EINVAL;

	ep = usb_ep_to_dummy_ep (_ep);
	if (!_ep || (!ep->desc && _ep->name != ep0name))
		return -EINVAL;

	dum = ep_to_dummy (ep);
	if (!dum->driver || !is_enabled (dum))
		return -ESHUTDOWN;

#if 0
	dev_dbg (udc_dev(dum), "ep %p queue req %p to %s, len %d buf %p\n",
			ep, _req, _ep->name, _req->length, _req->buf);
#endif

	_req->status = -EINPROGRESS;
	_req->actual = 0;
	spin_lock_irqsave (&dum->lock, flags);

	/* implement an emulated single-request FIFO */
	if (ep->desc && (ep->desc->bEndpointAddress & USB_DIR_IN) &&
			list_empty (&dum->fifo_req.queue) &&
			list_empty (&ep->queue) &&
			_req->length <= FIFO_SIZE) {
		req = &dum->fifo_req;
		req->req = *_req;
		req->req.buf = dum->fifo_buf;
		memcpy (dum->fifo_buf, _req->buf, _req->length);
		req->req.context = dum;
		req->req.complete = fifo_complete;

		list_add_tail(&req->queue, &ep->queue);
		spin_unlock (&dum->lock);
		_req->actual = _req->length;
		_req->status = 0;
		_req->complete (_ep, _req);
		spin_lock (&dum->lock);
	}  else
		list_add_tail(&req->queue, &ep->queue);
	spin_unlock_irqrestore (&dum->lock, flags);

	/* real hardware would likely enable transfers here, in case
	 * it'd been left NAKing.
	 */
	return 0;
}

static int dummy_dequeue (struct usb_ep *_ep, struct usb_request *_req)
{
	struct dummy_ep		*ep;
	struct dummy		*dum;
	int			retval = -EINVAL;
	unsigned long		flags;
	struct dummy_request	*req = NULL;

	if (!_ep || !_req)
		return retval;
	ep = usb_ep_to_dummy_ep (_ep);
	dum = ep_to_dummy (ep);

	if (!dum->driver)
		return -ESHUTDOWN;

	local_irq_save (flags);
	spin_lock (&dum->lock);
	list_for_each_entry (req, &ep->queue, queue) {
		if (&req->req == _req) {
			list_del_init (&req->queue);
			_req->status = -ECONNRESET;
			retval = 0;
			break;
		}
	}
	spin_unlock (&dum->lock);

	if (retval == 0) {
		dev_dbg (udc_dev(dum),
				"dequeued req %p from %s, len %d buf %p\n",
				req, _ep->name, _req->length, _req->buf);
		_req->complete (_ep, _req);
	}
	local_irq_restore (flags);
	return retval;
}

static int
dummy_set_halt_and_wedge(struct usb_ep *_ep, int value, int wedged)
{
	struct dummy_ep		*ep;
	struct dummy		*dum;

	if (!_ep)
		return -EINVAL;
	ep = usb_ep_to_dummy_ep (_ep);
	dum = ep_to_dummy (ep);
	if (!dum->driver)
		return -ESHUTDOWN;
	if (!value)
		ep->halted = ep->wedged = 0;
	else if (ep->desc && (ep->desc->bEndpointAddress & USB_DIR_IN) &&
			!list_empty (&ep->queue))
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
static int dummy_g_get_frame (struct usb_gadget *_gadget)
{
	struct timeval	tv;

	do_gettimeofday (&tv);
	return tv.tv_usec / 1000;
}

static int dummy_wakeup (struct usb_gadget *_gadget)
{
	struct dummy	*dum;

	dum = gadget_to_dummy (_gadget);
	if (!(dum->devstatus &	( (1 << USB_DEVICE_B_HNP_ENABLE)
				| (1 << USB_DEVICE_REMOTE_WAKEUP))))
		return -EINVAL;
	if ((dum->port_status & USB_PORT_STAT_CONNECTION) == 0)
		return -ENOLINK;
	if ((dum->port_status & USB_PORT_STAT_SUSPEND) == 0 &&
			 dum->rh_state != DUMMY_RH_SUSPENDED)
		return -EIO;

	/* FIXME: What if the root hub is suspended but the port isn't? */

	/* hub notices our request, issues downstream resume, etc */
	dum->resuming = 1;
	dum->re_timeout = jiffies + msecs_to_jiffies(20);
	mod_timer (&dummy_to_hcd (dum)->rh_timer, dum->re_timeout);
	return 0;
}

static int dummy_set_selfpowered (struct usb_gadget *_gadget, int value)
{
	struct dummy	*dum;

	dum = gadget_to_dummy (_gadget);
	if (value)
		dum->devstatus |= (1 << USB_DEVICE_SELF_POWERED);
	else
		dum->devstatus &= ~(1 << USB_DEVICE_SELF_POWERED);
	return 0;
}

static int dummy_pullup (struct usb_gadget *_gadget, int value)
{
	struct dummy	*dum;
	unsigned long	flags;

	dum = gadget_to_dummy (_gadget);
	spin_lock_irqsave (&dum->lock, flags);
	dum->pullup = (value != 0);
	set_link_state (dum);
	spin_unlock_irqrestore (&dum->lock, flags);

	usb_hcd_poll_rh_status (dummy_to_hcd (dum));
	return 0;
}

static const struct usb_gadget_ops dummy_ops = {
	.get_frame	= dummy_g_get_frame,
	.wakeup		= dummy_wakeup,
	.set_selfpowered = dummy_set_selfpowered,
	.pullup		= dummy_pullup,
};

/*-------------------------------------------------------------------------*/

/* "function" sysfs attribute */
static ssize_t
show_function (struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dummy	*dum = gadget_dev_to_dummy (dev);

	if (!dum->driver || !dum->driver->function)
		return 0;
	return scnprintf (buf, PAGE_SIZE, "%s\n", dum->driver->function);
}
static DEVICE_ATTR (function, S_IRUGO, show_function, NULL);

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

int
usb_gadget_register_driver (struct usb_gadget_driver *driver)
{
	struct dummy	*dum = the_controller;
	int		retval, i;

	if (!dum)
		return -EINVAL;
	if (dum->driver)
		return -EBUSY;
	if (!driver->bind || !driver->setup
			|| driver->speed == USB_SPEED_UNKNOWN)
		return -EINVAL;

	/*
	 * SLAVE side init ... the layer above hardware, which
	 * can't enumerate without help from the driver we're binding.
	 */

	dum->devstatus = 0;

	INIT_LIST_HEAD (&dum->gadget.ep_list);
	for (i = 0; i < DUMMY_ENDPOINTS; i++) {
		struct dummy_ep	*ep = &dum->ep [i];

		if (!ep_name [i])
			break;
		ep->ep.name = ep_name [i];
		ep->ep.ops = &dummy_ep_ops;
		list_add_tail (&ep->ep.ep_list, &dum->gadget.ep_list);
		ep->halted = ep->wedged = ep->already_seen =
				ep->setup_stage = 0;
		ep->ep.maxpacket = ~0;
		ep->last_io = jiffies;
		ep->gadget = &dum->gadget;
		ep->desc = NULL;
		INIT_LIST_HEAD (&ep->queue);
	}

	dum->gadget.ep0 = &dum->ep [0].ep;
	dum->ep [0].ep.maxpacket = 64;
	list_del_init (&dum->ep [0].ep.ep_list);
	INIT_LIST_HEAD(&dum->fifo_req.queue);

	driver->driver.bus = NULL;
	dum->driver = driver;
	dum->gadget.dev.driver = &driver->driver;
	dev_dbg (udc_dev(dum), "binding gadget driver '%s'\n",
			driver->driver.name);
	retval = driver->bind(&dum->gadget);
	if (retval) {
		dum->driver = NULL;
		dum->gadget.dev.driver = NULL;
		return retval;
	}

	/* khubd will enumerate this in a while */
	spin_lock_irq (&dum->lock);
	dum->pullup = 1;
	set_link_state (dum);
	spin_unlock_irq (&dum->lock);

	usb_hcd_poll_rh_status (dummy_to_hcd (dum));
	return 0;
}
EXPORT_SYMBOL (usb_gadget_register_driver);

int
usb_gadget_unregister_driver (struct usb_gadget_driver *driver)
{
	struct dummy	*dum = the_controller;
	unsigned long	flags;

	if (!dum)
		return -ENODEV;
	if (!driver || driver != dum->driver || !driver->unbind)
		return -EINVAL;

	dev_dbg (udc_dev(dum), "unregister gadget driver '%s'\n",
			driver->driver.name);

	spin_lock_irqsave (&dum->lock, flags);
	dum->pullup = 0;
	set_link_state (dum);
	spin_unlock_irqrestore (&dum->lock, flags);

	driver->unbind (&dum->gadget);
	dum->gadget.dev.driver = NULL;
	dum->driver = NULL;

	spin_lock_irqsave (&dum->lock, flags);
	dum->pullup = 0;
	set_link_state (dum);
	spin_unlock_irqrestore (&dum->lock, flags);

	usb_hcd_poll_rh_status (dummy_to_hcd (dum));
	return 0;
}
EXPORT_SYMBOL (usb_gadget_unregister_driver);

#undef is_enabled

/* just declare this in any driver that really need it */
extern int net2280_set_fifo_mode (struct usb_gadget *gadget, int mode);

int net2280_set_fifo_mode (struct usb_gadget *gadget, int mode)
{
	return -ENOSYS;
}
EXPORT_SYMBOL (net2280_set_fifo_mode);


/* The gadget structure is stored inside the hcd structure and will be
 * released along with it. */
static void
dummy_gadget_release (struct device *dev)
{
	struct dummy	*dum = gadget_dev_to_dummy (dev);

	usb_put_hcd (dummy_to_hcd (dum));
}

static int dummy_udc_probe (struct platform_device *pdev)
{
	struct dummy	*dum = the_controller;
	int		rc;

	dum->gadget.name = gadget_name;
	dum->gadget.ops = &dummy_ops;
	dum->gadget.is_dualspeed = 1;

	/* maybe claim OTG support, though we won't complete HNP */
	dum->gadget.is_otg = (dummy_to_hcd(dum)->self.otg_port != 0);

	dev_set_name(&dum->gadget.dev, "gadget");
	dum->gadget.dev.parent = &pdev->dev;
	dum->gadget.dev.release = dummy_gadget_release;
	rc = device_register (&dum->gadget.dev);
	if (rc < 0)
		return rc;

	usb_get_hcd (dummy_to_hcd (dum));

	platform_set_drvdata (pdev, dum);
	rc = device_create_file (&dum->gadget.dev, &dev_attr_function);
	if (rc < 0)
		device_unregister (&dum->gadget.dev);
	return rc;
}

static int dummy_udc_remove (struct platform_device *pdev)
{
	struct dummy	*dum = platform_get_drvdata (pdev);

	platform_set_drvdata (pdev, NULL);
	device_remove_file (&dum->gadget.dev, &dev_attr_function);
	device_unregister (&dum->gadget.dev);
	return 0;
}

static int dummy_udc_suspend (struct platform_device *pdev, pm_message_t state)
{
	struct dummy	*dum = platform_get_drvdata(pdev);

	dev_dbg (&pdev->dev, "%s\n", __func__);
	spin_lock_irq (&dum->lock);
	dum->udc_suspended = 1;
	set_link_state (dum);
	spin_unlock_irq (&dum->lock);

	usb_hcd_poll_rh_status (dummy_to_hcd (dum));
	return 0;
}

static int dummy_udc_resume (struct platform_device *pdev)
{
	struct dummy	*dum = platform_get_drvdata(pdev);

	dev_dbg (&pdev->dev, "%s\n", __func__);
	spin_lock_irq (&dum->lock);
	dum->udc_suspended = 0;
	set_link_state (dum);
	spin_unlock_irq (&dum->lock);

	usb_hcd_poll_rh_status (dummy_to_hcd (dum));
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

static int dummy_urb_enqueue (
	struct usb_hcd			*hcd,
	struct urb			*urb,
	gfp_t				mem_flags
) {
	struct dummy	*dum;
	struct urbp	*urbp;
	unsigned long	flags;
	int		rc;

	if (!urb->transfer_buffer && urb->transfer_buffer_length)
		return -EINVAL;

	urbp = kmalloc (sizeof *urbp, mem_flags);
	if (!urbp)
		return -ENOMEM;
	urbp->urb = urb;

	dum = hcd_to_dummy (hcd);
	spin_lock_irqsave (&dum->lock, flags);
	rc = usb_hcd_link_urb_to_ep(hcd, urb);
	if (rc) {
		kfree(urbp);
		goto done;
	}

	if (!dum->udev) {
		dum->udev = urb->dev;
		usb_get_dev (dum->udev);
	} else if (unlikely (dum->udev != urb->dev))
		dev_err (dummy_dev(dum), "usb_device address has changed!\n");

	list_add_tail (&urbp->urbp_list, &dum->urbp_list);
	urb->hcpriv = urbp;
	if (usb_pipetype (urb->pipe) == PIPE_CONTROL)
		urb->error_count = 1;		/* mark as a new urb */

	/* kick the scheduler, it'll do the rest */
	if (!timer_pending (&dum->timer))
		mod_timer (&dum->timer, jiffies + 1);

 done:
	spin_unlock_irqrestore(&dum->lock, flags);
	return rc;
}

static int dummy_urb_dequeue(struct usb_hcd *hcd, struct urb *urb, int status)
{
	struct dummy	*dum;
	unsigned long	flags;
	int		rc;

	/* giveback happens automatically in timer callback,
	 * so make sure the callback happens */
	dum = hcd_to_dummy (hcd);
	spin_lock_irqsave (&dum->lock, flags);

	rc = usb_hcd_check_unlink_urb(hcd, urb, status);
	if (!rc && dum->rh_state != DUMMY_RH_RUNNING &&
			!list_empty(&dum->urbp_list))
		mod_timer (&dum->timer, jiffies);

	spin_unlock_irqrestore (&dum->lock, flags);
	return rc;
}

/* transfer up to a frame's worth; caller must own lock */
static int
transfer(struct dummy *dum, struct urb *urb, struct dummy_ep *ep, int limit,
		int *status)
{
	struct dummy_request	*req;

top:
	/* if there's no request queued, the device is NAKing; return */
	list_for_each_entry (req, &ep->queue, queue) {
		unsigned	host_len, dev_len, len;
		int		is_short, to_host;
		int		rescan = 0;

		/* 1..N packets of ep->ep.maxpacket each ... the last one
		 * may be short (including zero length).
		 *
		 * writer can send a zlp explicitly (length 0) or implicitly
		 * (length mod maxpacket zero, and 'zero' flag); they always
		 * terminate reads.
		 */
		host_len = urb->transfer_buffer_length - urb->actual_length;
		dev_len = req->req.length - req->req.actual;
		len = min (host_len, dev_len);

		/* FIXME update emulated data toggle too */

		to_host = usb_pipein (urb->pipe);
		if (unlikely (len == 0))
			is_short = 1;
		else {
			char		*ubuf, *rbuf;

			/* not enough bandwidth left? */
			if (limit < ep->ep.maxpacket && limit < len)
				break;
			len = min (len, (unsigned) limit);
			if (len == 0)
				break;

			/* use an extra pass for the final short packet */
			if (len > ep->ep.maxpacket) {
				rescan = 1;
				len -= (len % ep->ep.maxpacket);
			}
			is_short = (len % ep->ep.maxpacket) != 0;

			/* else transfer packet(s) */
			ubuf = urb->transfer_buffer + urb->actual_length;
			rbuf = req->req.buf + req->req.actual;
			if (to_host)
				memcpy (ubuf, rbuf, len);
			else
				memcpy (rbuf, ubuf, len);
			ep->last_io = jiffies;

			limit -= len;
			urb->actual_length += len;
			req->req.actual += len;
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
			list_del_init (&req->queue);

			spin_unlock (&dum->lock);
			req->req.complete (&ep->ep, &req->req);
			spin_lock (&dum->lock);

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

static int periodic_bytes (struct dummy *dum, struct dummy_ep *ep)
{
	int	limit = ep->ep.maxpacket;

	if (dum->gadget.speed == USB_SPEED_HIGH) {
		int	tmp;

		/* high bandwidth mode */
		tmp = le16_to_cpu(ep->desc->wMaxPacketSize);
		tmp = (tmp >> 11) & 0x03;
		tmp *= 8 /* applies to entire frame */;
		limit += limit * tmp;
	}
	return limit;
}

#define is_active(dum)	((dum->port_status & \
		(USB_PORT_STAT_CONNECTION | USB_PORT_STAT_ENABLE | \
			USB_PORT_STAT_SUSPEND)) \
		== (USB_PORT_STAT_CONNECTION | USB_PORT_STAT_ENABLE))

static struct dummy_ep *find_endpoint (struct dummy *dum, u8 address)
{
	int		i;

	if (!is_active (dum))
		return NULL;
	if ((address & ~USB_DIR_IN) == 0)
		return &dum->ep [0];
	for (i = 1; i < DUMMY_ENDPOINTS; i++) {
		struct dummy_ep	*ep = &dum->ep [i];

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

/* drive both sides of the transfers; looks like irq handlers to
 * both drivers except the callbacks aren't in_irq().
 */
static void dummy_timer (unsigned long _dum)
{
	struct dummy		*dum = (struct dummy *) _dum;
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
	default:
		dev_err (dummy_dev(dum), "bogus device speed\n");
		return;
	}

	/* FIXME if HZ != 1000 this will probably misbehave ... */

	/* look at each urb queued by the host side driver */
	spin_lock_irqsave (&dum->lock, flags);

	if (!dum->udev) {
		dev_err (dummy_dev(dum),
				"timer fired with no URBs pending?\n");
		spin_unlock_irqrestore (&dum->lock, flags);
		return;
	}

	for (i = 0; i < DUMMY_ENDPOINTS; i++) {
		if (!ep_name [i])
			break;
		dum->ep [i].already_seen = 0;
	}

restart:
	list_for_each_entry_safe (urbp, tmp, &dum->urbp_list, urbp_list) {
		struct urb		*urb;
		struct dummy_request	*req;
		u8			address;
		struct dummy_ep		*ep = NULL;
		int			type;
		int			status = -EINPROGRESS;

		urb = urbp->urb;
		if (urb->unlinked)
			goto return_urb;
		else if (dum->rh_state != DUMMY_RH_RUNNING)
			continue;
		type = usb_pipetype (urb->pipe);

		/* used up this frame's non-periodic bandwidth?
		 * FIXME there's infinite bandwidth for control and
		 * periodic transfers ... unrealistic.
		 */
		if (total <= 0 && type == PIPE_BULK)
			continue;

		/* find the gadget's ep for this request (if configured) */
		address = usb_pipeendpoint (urb->pipe);
		if (usb_pipein (urb->pipe))
			address |= USB_DIR_IN;
		ep = find_endpoint(dum, address);
		if (!ep) {
			/* set_configuration() disagreement */
			dev_dbg (dummy_dev(dum),
				"no ep configured for urb %p\n",
				urb);
			status = -EPROTO;
			goto return_urb;
		}

		if (ep->already_seen)
			continue;
		ep->already_seen = 1;
		if (ep == &dum->ep [0] && urb->error_count) {
			ep->setup_stage = 1;	/* a new urb */
			urb->error_count = 0;
		}
		if (ep->halted && !ep->setup_stage) {
			/* NOTE: must not be iso! */
			dev_dbg (dummy_dev(dum), "ep %s halted, urb %p\n",
					ep->ep.name, urb);
			status = -EPIPE;
			goto return_urb;
		}
		/* FIXME make sure both ends agree on maxpacket */

		/* handle control requests */
		if (ep == &dum->ep [0] && ep->setup_stage) {
			struct usb_ctrlrequest		setup;
			int				value = 1;
			struct dummy_ep			*ep2;
			unsigned			w_index;
			unsigned			w_value;

			setup = *(struct usb_ctrlrequest*) urb->setup_packet;
			w_index = le16_to_cpu(setup.wIndex);
			w_value = le16_to_cpu(setup.wValue);

			/* paranoia, in case of stale queued data */
			list_for_each_entry (req, &ep->queue, queue) {
				list_del_init (&req->queue);
				req->req.status = -EOVERFLOW;
				dev_dbg (udc_dev(dum), "stale req = %p\n",
						req);

				spin_unlock (&dum->lock);
				req->req.complete (&ep->ep, &req->req);
				spin_lock (&dum->lock);
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
			switch (setup.bRequest) {
			case USB_REQ_SET_ADDRESS:
				if (setup.bRequestType != Dev_Request)
					break;
				dum->address = w_value;
				status = 0;
				dev_dbg (udc_dev(dum), "set_address = %d\n",
						w_value);
				value = 0;
				break;
			case USB_REQ_SET_FEATURE:
				if (setup.bRequestType == Dev_Request) {
					value = 0;
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
						dum->gadget.a_alt_hnp_support
							= 1;
						break;
					default:
						value = -EOPNOTSUPP;
					}
					if (value == 0) {
						dum->devstatus |=
							(1 << w_value);
						status = 0;
					}

				} else if (setup.bRequestType == Ep_Request) {
					// endpoint halt
					ep2 = find_endpoint (dum, w_index);
					if (!ep2 || ep2->ep.name == ep0name) {
						value = -EOPNOTSUPP;
						break;
					}
					ep2->halted = 1;
					value = 0;
					status = 0;
				}
				break;
			case USB_REQ_CLEAR_FEATURE:
				if (setup.bRequestType == Dev_Request) {
					switch (w_value) {
					case USB_DEVICE_REMOTE_WAKEUP:
						dum->devstatus &= ~(1 <<
							USB_DEVICE_REMOTE_WAKEUP);
						value = 0;
						status = 0;
						break;
					default:
						value = -EOPNOTSUPP;
						break;
					}
				} else if (setup.bRequestType == Ep_Request) {
					// endpoint halt
					ep2 = find_endpoint (dum, w_index);
					if (!ep2) {
						value = -EOPNOTSUPP;
						break;
					}
					if (!ep2->wedged)
						ep2->halted = 0;
					value = 0;
					status = 0;
				}
				break;
			case USB_REQ_GET_STATUS:
				if (setup.bRequestType == Dev_InRequest
						|| setup.bRequestType
							== Intf_InRequest
						|| setup.bRequestType
							== Ep_InRequest
						) {
					char *buf;

					// device: remote wakeup, selfpowered
					// interface: nothing
					// endpoint: halt
					buf = (char *)urb->transfer_buffer;
					if (urb->transfer_buffer_length > 0) {
						if (setup.bRequestType ==
								Ep_InRequest) {
	ep2 = find_endpoint (dum, w_index);
	if (!ep2) {
		value = -EOPNOTSUPP;
		break;
	}
	buf [0] = ep2->halted;
						} else if (setup.bRequestType ==
								Dev_InRequest) {
							buf [0] = (u8)
								dum->devstatus;
						} else
							buf [0] = 0;
					}
					if (urb->transfer_buffer_length > 1)
						buf [1] = 0;
					urb->actual_length = min_t(u32, 2,
						urb->transfer_buffer_length);
					value = 0;
					status = 0;
				}
				break;
			}

			/* gadget driver handles all other requests.  block
			 * until setup() returns; no reentrancy issues etc.
			 */
			if (value > 0) {
				spin_unlock (&dum->lock);
				value = dum->driver->setup (&dum->gadget,
						&setup);
				spin_lock (&dum->lock);

				if (value >= 0) {
					/* no delays (max 64KB data stage) */
					limit = 64*1024;
					goto treat_control_like_bulk;
				}
				/* error, see below */
			}

			if (value < 0) {
				if (value != -EOPNOTSUPP)
					dev_dbg (udc_dev(dum),
						"setup --> %d\n",
						value);
				status = -EPIPE;
				urb->actual_length = 0;
			}

			goto return_urb;
		}

		/* non-control requests */
		limit = total;
		switch (usb_pipetype (urb->pipe)) {
		case PIPE_ISOCHRONOUS:
			/* FIXME is it urb->interval since the last xfer?
			 * use urb->iso_frame_desc[i].
			 * complete whether or not ep has requests queued.
			 * report random errors, to debug drivers.
			 */
			limit = max (limit, periodic_bytes (dum, ep));
			status = -ENOSYS;
			break;

		case PIPE_INTERRUPT:
			/* FIXME is it urb->interval since the last xfer?
			 * this almost certainly polls too fast.
			 */
			limit = max (limit, periodic_bytes (dum, ep));
			/* FALLTHROUGH */

		// case PIPE_BULK:  case PIPE_CONTROL:
		default:
		treat_control_like_bulk:
			ep->last_io = jiffies;
			total = transfer(dum, urb, ep, limit, &status);
			break;
		}

		/* incomplete transfer? */
		if (status == -EINPROGRESS)
			continue;

return_urb:
		list_del (&urbp->urbp_list);
		kfree (urbp);
		if (ep)
			ep->already_seen = ep->setup_stage = 0;

		usb_hcd_unlink_urb_from_ep(dummy_to_hcd(dum), urb);
		spin_unlock (&dum->lock);
		usb_hcd_giveback_urb(dummy_to_hcd(dum), urb, status);
		spin_lock (&dum->lock);

		goto restart;
	}

	if (list_empty (&dum->urbp_list)) {
		usb_put_dev (dum->udev);
		dum->udev = NULL;
	} else if (dum->rh_state == DUMMY_RH_RUNNING) {
		/* want a 1 msec delay here */
		mod_timer (&dum->timer, jiffies + msecs_to_jiffies(1));
	}

	spin_unlock_irqrestore (&dum->lock, flags);
}

/*-------------------------------------------------------------------------*/

#define PORT_C_MASK \
	((USB_PORT_STAT_C_CONNECTION \
	| USB_PORT_STAT_C_ENABLE \
	| USB_PORT_STAT_C_SUSPEND \
	| USB_PORT_STAT_C_OVERCURRENT \
	| USB_PORT_STAT_C_RESET) << 16)

static int dummy_hub_status (struct usb_hcd *hcd, char *buf)
{
	struct dummy		*dum;
	unsigned long		flags;
	int			retval = 0;

	dum = hcd_to_dummy (hcd);

	spin_lock_irqsave (&dum->lock, flags);
	if (!test_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags))
		goto done;

	if (dum->resuming && time_after_eq (jiffies, dum->re_timeout)) {
		dum->port_status |= (USB_PORT_STAT_C_SUSPEND << 16);
		dum->port_status &= ~USB_PORT_STAT_SUSPEND;
		set_link_state (dum);
	}

	if ((dum->port_status & PORT_C_MASK) != 0) {
		*buf = (1 << 1);
		dev_dbg (dummy_dev(dum), "port status 0x%08x has changes\n",
				dum->port_status);
		retval = 1;
		if (dum->rh_state == DUMMY_RH_SUSPENDED)
			usb_hcd_resume_root_hub (hcd);
	}
done:
	spin_unlock_irqrestore (&dum->lock, flags);
	return retval;
}

static inline void
hub_descriptor (struct usb_hub_descriptor *desc)
{
	memset (desc, 0, sizeof *desc);
	desc->bDescriptorType = 0x29;
	desc->bDescLength = 9;
	desc->wHubCharacteristics = cpu_to_le16(0x0001);
	desc->bNbrPorts = 1;
	desc->bitmap [0] = 0xff;
	desc->bitmap [1] = 0xff;
}

static int dummy_hub_control (
	struct usb_hcd	*hcd,
	u16		typeReq,
	u16		wValue,
	u16		wIndex,
	char		*buf,
	u16		wLength
) {
	struct dummy	*dum;
	int		retval = 0;
	unsigned long	flags;

	if (!test_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags))
		return -ETIMEDOUT;

	dum = hcd_to_dummy (hcd);
	spin_lock_irqsave (&dum->lock, flags);
	switch (typeReq) {
	case ClearHubFeature:
		break;
	case ClearPortFeature:
		switch (wValue) {
		case USB_PORT_FEAT_SUSPEND:
			if (dum->port_status & USB_PORT_STAT_SUSPEND) {
				/* 20msec resume signaling */
				dum->resuming = 1;
				dum->re_timeout = jiffies +
						msecs_to_jiffies(20);
			}
			break;
		case USB_PORT_FEAT_POWER:
			if (dum->port_status & USB_PORT_STAT_POWER)
				dev_dbg (dummy_dev(dum), "power-off\n");
			/* FALLS THROUGH */
		default:
			dum->port_status &= ~(1 << wValue);
			set_link_state (dum);
		}
		break;
	case GetHubDescriptor:
		hub_descriptor ((struct usb_hub_descriptor *) buf);
		break;
	case GetHubStatus:
		*(__le32 *) buf = cpu_to_le32 (0);
		break;
	case GetPortStatus:
		if (wIndex != 1)
			retval = -EPIPE;

		/* whoever resets or resumes must GetPortStatus to
		 * complete it!!
		 */
		if (dum->resuming &&
				time_after_eq (jiffies, dum->re_timeout)) {
			dum->port_status |= (USB_PORT_STAT_C_SUSPEND << 16);
			dum->port_status &= ~USB_PORT_STAT_SUSPEND;
		}
		if ((dum->port_status & USB_PORT_STAT_RESET) != 0 &&
				time_after_eq (jiffies, dum->re_timeout)) {
			dum->port_status |= (USB_PORT_STAT_C_RESET << 16);
			dum->port_status &= ~USB_PORT_STAT_RESET;
			if (dum->pullup) {
				dum->port_status |= USB_PORT_STAT_ENABLE;
				/* give it the best speed we agree on */
				dum->gadget.speed = dum->driver->speed;
				dum->gadget.ep0->maxpacket = 64;
				switch (dum->gadget.speed) {
				case USB_SPEED_HIGH:
					dum->port_status |=
						USB_PORT_STAT_HIGH_SPEED;
					break;
				case USB_SPEED_LOW:
					dum->gadget.ep0->maxpacket = 8;
					dum->port_status |=
						USB_PORT_STAT_LOW_SPEED;
					break;
				default:
					dum->gadget.speed = USB_SPEED_FULL;
					break;
				}
			}
		}
		set_link_state (dum);
		((__le16 *) buf)[0] = cpu_to_le16 (dum->port_status);
		((__le16 *) buf)[1] = cpu_to_le16 (dum->port_status >> 16);
		break;
	case SetHubFeature:
		retval = -EPIPE;
		break;
	case SetPortFeature:
		switch (wValue) {
		case USB_PORT_FEAT_SUSPEND:
			if (dum->active) {
				dum->port_status |= USB_PORT_STAT_SUSPEND;

				/* HNP would happen here; for now we
				 * assume b_bus_req is always true.
				 */
				set_link_state (dum);
				if (((1 << USB_DEVICE_B_HNP_ENABLE)
						& dum->devstatus) != 0)
					dev_dbg (dummy_dev(dum),
							"no HNP yet!\n");
			}
			break;
		case USB_PORT_FEAT_POWER:
			dum->port_status |= USB_PORT_STAT_POWER;
			set_link_state (dum);
			break;
		case USB_PORT_FEAT_RESET:
			/* if it's already enabled, disable */
			dum->port_status &= ~(USB_PORT_STAT_ENABLE
					| USB_PORT_STAT_LOW_SPEED
					| USB_PORT_STAT_HIGH_SPEED);
			dum->devstatus = 0;
			/* 50msec reset signaling */
			dum->re_timeout = jiffies + msecs_to_jiffies(50);
			/* FALLS THROUGH */
		default:
			if ((dum->port_status & USB_PORT_STAT_POWER) != 0) {
				dum->port_status |= (1 << wValue);
				set_link_state (dum);
			}
		}
		break;

	default:
		dev_dbg (dummy_dev(dum),
			"hub control req%04x v%04x i%04x l%d\n",
			typeReq, wValue, wIndex, wLength);

		/* "protocol stall" on error */
		retval = -EPIPE;
	}
	spin_unlock_irqrestore (&dum->lock, flags);

	if ((dum->port_status & PORT_C_MASK) != 0)
		usb_hcd_poll_rh_status (hcd);
	return retval;
}

static int dummy_bus_suspend (struct usb_hcd *hcd)
{
	struct dummy *dum = hcd_to_dummy (hcd);

	dev_dbg (&hcd->self.root_hub->dev, "%s\n", __func__);

	spin_lock_irq (&dum->lock);
	dum->rh_state = DUMMY_RH_SUSPENDED;
	set_link_state (dum);
	hcd->state = HC_STATE_SUSPENDED;
	spin_unlock_irq (&dum->lock);
	return 0;
}

static int dummy_bus_resume (struct usb_hcd *hcd)
{
	struct dummy *dum = hcd_to_dummy (hcd);
	int rc = 0;

	dev_dbg (&hcd->self.root_hub->dev, "%s\n", __func__);

	spin_lock_irq (&dum->lock);
	if (!test_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags)) {
		rc = -ESHUTDOWN;
	} else {
		dum->rh_state = DUMMY_RH_RUNNING;
		set_link_state (dum);
		if (!list_empty(&dum->urbp_list))
			mod_timer (&dum->timer, jiffies);
		hcd->state = HC_STATE_RUNNING;
	}
	spin_unlock_irq (&dum->lock);
	return rc;
}

/*-------------------------------------------------------------------------*/

static inline ssize_t
show_urb (char *buf, size_t size, struct urb *urb)
{
	int ep = usb_pipeendpoint (urb->pipe);

	return snprintf (buf, size,
		"urb/%p %s ep%d%s%s len %d/%d\n",
		urb,
		({ char *s;
		 switch (urb->dev->speed) {
		 case USB_SPEED_LOW:	s = "ls"; break;
		 case USB_SPEED_FULL:	s = "fs"; break;
		 case USB_SPEED_HIGH:	s = "hs"; break;
		 default:		s = "?"; break;
		 }; s; }),
		ep, ep ? (usb_pipein (urb->pipe) ? "in" : "out") : "",
		({ char *s; \
		 switch (usb_pipetype (urb->pipe)) { \
		 case PIPE_CONTROL:	s = ""; break; \
		 case PIPE_BULK:	s = "-bulk"; break; \
		 case PIPE_INTERRUPT:	s = "-int"; break; \
		 default: 		s = "-iso"; break; \
		}; s;}),
		urb->actual_length, urb->transfer_buffer_length);
}

static ssize_t
show_urbs (struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_hcd		*hcd = dev_get_drvdata (dev);
	struct dummy		*dum = hcd_to_dummy (hcd);
	struct urbp		*urbp;
	size_t			size = 0;
	unsigned long		flags;

	spin_lock_irqsave (&dum->lock, flags);
	list_for_each_entry (urbp, &dum->urbp_list, urbp_list) {
		size_t		temp;

		temp = show_urb (buf, PAGE_SIZE - size, urbp->urb);
		buf += temp;
		size += temp;
	}
	spin_unlock_irqrestore (&dum->lock, flags);

	return size;
}
static DEVICE_ATTR (urbs, S_IRUGO, show_urbs, NULL);

static int dummy_start (struct usb_hcd *hcd)
{
	struct dummy		*dum;

	dum = hcd_to_dummy (hcd);

	/*
	 * MASTER side init ... we emulate a root hub that'll only ever
	 * talk to one device (the slave side).  Also appears in sysfs,
	 * just like more familiar pci-based HCDs.
	 */
	spin_lock_init (&dum->lock);
	init_timer (&dum->timer);
	dum->timer.function = dummy_timer;
	dum->timer.data = (unsigned long) dum;
	dum->rh_state = DUMMY_RH_RUNNING;

	INIT_LIST_HEAD (&dum->urbp_list);

	hcd->power_budget = POWER_BUDGET;
	hcd->state = HC_STATE_RUNNING;
	hcd->uses_new_polling = 1;

#ifdef CONFIG_USB_OTG
	hcd->self.otg_port = 1;
#endif

	/* FIXME 'urbs' should be a per-device thing, maybe in usbcore */
	return device_create_file (dummy_dev(dum), &dev_attr_urbs);
}

static void dummy_stop (struct usb_hcd *hcd)
{
	struct dummy		*dum;

	dum = hcd_to_dummy (hcd);

	device_remove_file (dummy_dev(dum), &dev_attr_urbs);
	usb_gadget_unregister_driver (dum->driver);
	dev_info (dummy_dev(dum), "stopped\n");
}

/*-------------------------------------------------------------------------*/

static int dummy_h_get_frame (struct usb_hcd *hcd)
{
	return dummy_g_get_frame (NULL);
}

static const struct hc_driver dummy_hcd = {
	.description =		(char *) driver_name,
	.product_desc =		"Dummy host controller",
	.hcd_priv_size =	sizeof(struct dummy),

	.flags =		HCD_USB2,

	.start =		dummy_start,
	.stop =			dummy_stop,

	.urb_enqueue = 		dummy_urb_enqueue,
	.urb_dequeue = 		dummy_urb_dequeue,

	.get_frame_number = 	dummy_h_get_frame,

	.hub_status_data = 	dummy_hub_status,
	.hub_control = 		dummy_hub_control,
	.bus_suspend =		dummy_bus_suspend,
	.bus_resume =		dummy_bus_resume,
};

static int dummy_hcd_probe(struct platform_device *pdev)
{
	struct usb_hcd		*hcd;
	int			retval;

	dev_info(&pdev->dev, "%s, driver " DRIVER_VERSION "\n", driver_desc);

	hcd = usb_create_hcd(&dummy_hcd, &pdev->dev, dev_name(&pdev->dev));
	if (!hcd)
		return -ENOMEM;
	the_controller = hcd_to_dummy (hcd);

	retval = usb_add_hcd(hcd, 0, 0);
	if (retval != 0) {
		usb_put_hcd (hcd);
		the_controller = NULL;
	}
	return retval;
}

static int dummy_hcd_remove (struct platform_device *pdev)
{
	struct usb_hcd		*hcd;

	hcd = platform_get_drvdata (pdev);
	usb_remove_hcd (hcd);
	usb_put_hcd (hcd);
	the_controller = NULL;
	return 0;
}

static int dummy_hcd_suspend (struct platform_device *pdev, pm_message_t state)
{
	struct usb_hcd		*hcd;
	struct dummy		*dum;
	int			rc = 0;

	dev_dbg (&pdev->dev, "%s\n", __func__);

	hcd = platform_get_drvdata (pdev);
	dum = hcd_to_dummy (hcd);
	if (dum->rh_state == DUMMY_RH_RUNNING) {
		dev_warn(&pdev->dev, "Root hub isn't suspended!\n");
		rc = -EBUSY;
	} else
		clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
	return rc;
}

static int dummy_hcd_resume (struct platform_device *pdev)
{
	struct usb_hcd		*hcd;

	dev_dbg (&pdev->dev, "%s\n", __func__);

	hcd = platform_get_drvdata (pdev);
	set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
	usb_hcd_poll_rh_status (hcd);
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

static struct platform_device *the_udc_pdev;
static struct platform_device *the_hcd_pdev;

static int __init init (void)
{
	int	retval = -ENOMEM;

	if (usb_disabled ())
		return -ENODEV;

	the_hcd_pdev = platform_device_alloc(driver_name, -1);
	if (!the_hcd_pdev)
		return retval;
	the_udc_pdev = platform_device_alloc(gadget_name, -1);
	if (!the_udc_pdev)
		goto err_alloc_udc;

	retval = platform_driver_register(&dummy_hcd_driver);
	if (retval < 0)
		goto err_register_hcd_driver;
	retval = platform_driver_register(&dummy_udc_driver);
	if (retval < 0)
		goto err_register_udc_driver;

	retval = platform_device_add(the_hcd_pdev);
	if (retval < 0)
		goto err_add_hcd;
	retval = platform_device_add(the_udc_pdev);
	if (retval < 0)
		goto err_add_udc;
	return retval;

err_add_udc:
	platform_device_del(the_hcd_pdev);
err_add_hcd:
	platform_driver_unregister(&dummy_udc_driver);
err_register_udc_driver:
	platform_driver_unregister(&dummy_hcd_driver);
err_register_hcd_driver:
	platform_device_put(the_udc_pdev);
err_alloc_udc:
	platform_device_put(the_hcd_pdev);
	return retval;
}
module_init (init);

static void __exit cleanup (void)
{
	platform_device_unregister(the_udc_pdev);
	platform_device_unregister(the_hcd_pdev);
	platform_driver_unregister(&dummy_udc_driver);
	platform_driver_unregister(&dummy_hcd_driver);
}
module_exit (cleanup);
