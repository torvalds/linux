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

#define DEBUG

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/version.h>

#include <linux/usb.h>
#include <linux/usb_gadget.h>

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/unaligned.h>


#include "../core/hcd.h"


#define DRIVER_DESC	"USB Host+Gadget Emulator"
#define DRIVER_VERSION	"17 Dec 2004"

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
#define DUMMY_ENDPOINTS	(sizeof(ep_name)/sizeof(char *))

#define FIFO_SIZE		64

struct urbp {
	struct urb		*urb;
	struct list_head	urbp_list;
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

	/*
	 * MASTER/HOST side support
	 */
	struct timer_list		timer;
	u32				port_status;
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

/*
 * This "hardware" may look a bit odd in diagnostics since it's got both
 * host and device sides; and it binds different drivers to each side.
 */
static struct platform_device		the_pdev;

static struct device_driver dummy_driver = {
	.name		= (char *) driver_name,
	.bus		= &platform_bus_type,
};

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
			/* conserve return statements */
		default:
			switch (max) {
			case 8: case 16: case 32: case 64:
				/* we'll fake any legal size */
				break;
			default:
		case USB_SPEED_LOW:
				goto done;
			}
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

	dev_dbg (dummy_dev(dum), "enabled %s (ep%d%s-%s) maxpacket %d\n",
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
	retval = 0;
done:
	return retval;
}

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

	dev_dbg (dummy_dev(dum), "disabled %s\n", _ep->name);
	return retval;
}

static struct usb_request *
dummy_alloc_request (struct usb_ep *_ep, int mem_flags)
{
	struct dummy_ep		*ep;
	struct dummy_request	*req;

	if (!_ep)
		return NULL;
	ep = usb_ep_to_dummy_ep (_ep);

	req = kmalloc (sizeof *req, mem_flags);
	if (!req)
		return NULL;
	memset (req, 0, sizeof *req);
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

static void *
dummy_alloc_buffer (
	struct usb_ep *_ep,
	unsigned bytes,
	dma_addr_t *dma,
	int mem_flags
) {
	char			*retval;
	struct dummy_ep		*ep;
	struct dummy		*dum;

	ep = usb_ep_to_dummy_ep (_ep);
	dum = ep_to_dummy (ep);

	if (!dum->driver)
		return NULL;
	retval = kmalloc (bytes, mem_flags);
	*dma = (dma_addr_t) retval;
	return retval;
}

static void
dummy_free_buffer (
	struct usb_ep *_ep,
	void *buf,
	dma_addr_t dma,
	unsigned bytes
) {
	if (bytes)
		kfree (buf);
}

static void
fifo_complete (struct usb_ep *ep, struct usb_request *req)
{
}

static int
dummy_queue (struct usb_ep *_ep, struct usb_request *_req, int mem_flags)
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
	dev_dbg (dummy_dev(dum), "ep %p queue req %p to %s, len %d buf %p\n",
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

		spin_unlock (&dum->lock);
		_req->actual = _req->length;
		_req->status = 0;
		_req->complete (_ep, _req);
		spin_lock (&dum->lock);
	}
	list_add_tail (&req->queue, &ep->queue);
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

	spin_lock_irqsave (&dum->lock, flags);
	list_for_each_entry (req, &ep->queue, queue) {
		if (&req->req == _req) {
			list_del_init (&req->queue);
			_req->status = -ECONNRESET;
			retval = 0;
			break;
		}
	}
	spin_unlock_irqrestore (&dum->lock, flags);

	if (retval == 0) {
		dev_dbg (dummy_dev(dum),
				"dequeued req %p from %s, len %d buf %p\n",
				req, _ep->name, _req->length, _req->buf);
		_req->complete (_ep, _req);
	}
	return retval;
}

static int
dummy_set_halt (struct usb_ep *_ep, int value)
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
		ep->halted = 0;
	else if (ep->desc && (ep->desc->bEndpointAddress & USB_DIR_IN) &&
			!list_empty (&ep->queue))
		return -EAGAIN;
	else
		ep->halted = 1;
	/* FIXME clear emulated data toggle too */
	return 0;
}

static const struct usb_ep_ops dummy_ep_ops = {
	.enable		= dummy_enable,
	.disable	= dummy_disable,

	.alloc_request	= dummy_alloc_request,
	.free_request	= dummy_free_request,

	.alloc_buffer	= dummy_alloc_buffer,
	.free_buffer	= dummy_free_buffer,
	/* map, unmap, ... eventually hook the "generic" dma calls */

	.queue		= dummy_queue,
	.dequeue	= dummy_dequeue,

	.set_halt	= dummy_set_halt,
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
	if ((dum->devstatus & (1 << USB_DEVICE_REMOTE_WAKEUP)) == 0
			|| !(dum->port_status & (1 << USB_PORT_FEAT_SUSPEND)))
		return -EINVAL;

	/* hub notices our request, issues downstream resume, etc */
	dum->resuming = 1;
	dum->port_status |= (1 << USB_PORT_FEAT_C_SUSPEND);
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

static const struct usb_gadget_ops dummy_ops = {
	.get_frame	= dummy_g_get_frame,
	.wakeup		= dummy_wakeup,
	.set_selfpowered = dummy_set_selfpowered,
};

/*-------------------------------------------------------------------------*/

/* "function" sysfs attribute */
static ssize_t
show_function (struct device *dev, char *buf)
{
	struct dummy	*dum = gadget_dev_to_dummy (dev);

	if (!dum->driver || !dum->driver->function)
		return 0;
	return scnprintf (buf, PAGE_SIZE, "%s\n", dum->driver->function);
}
DEVICE_ATTR (function, S_IRUGO, show_function, NULL);

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

static void
dummy_udc_release (struct device *dev)
{
}

static void
dummy_pdev_release (struct device *dev)
{
}

static int
dummy_register_udc (struct dummy *dum)
{
	int		rc;

	strcpy (dum->gadget.dev.bus_id, "udc");
	dum->gadget.dev.parent = dummy_dev(dum);
	dum->gadget.dev.release = dummy_udc_release;

	rc = device_register (&dum->gadget.dev);
	if (rc == 0)
		device_create_file (&dum->gadget.dev, &dev_attr_function);
	return rc;
}

static void
dummy_unregister_udc (struct dummy *dum)
{
	device_remove_file (&dum->gadget.dev, &dev_attr_function);
	device_unregister (&dum->gadget.dev);
}

int
usb_gadget_register_driver (struct usb_gadget_driver *driver)
{
	struct dummy	*dum = the_controller;
	int		retval, i;

	if (!dum)
		return -EINVAL;
	if (dum->driver)
		return -EBUSY;
	if (!driver->bind || !driver->unbind || !driver->setup
			|| driver->speed == USB_SPEED_UNKNOWN)
		return -EINVAL;

	/*
	 * SLAVE side init ... the layer above hardware, which
	 * can't enumerate without help from the driver we're binding.
	 */
	dum->gadget.name = gadget_name;
	dum->gadget.ops = &dummy_ops;
	dum->gadget.is_dualspeed = 1;

	dum->devstatus = 0;
	dum->resuming = 0;

	INIT_LIST_HEAD (&dum->gadget.ep_list);
	for (i = 0; i < DUMMY_ENDPOINTS; i++) {
		struct dummy_ep	*ep = &dum->ep [i];

		if (!ep_name [i])
			break;
		ep->ep.name = ep_name [i];
		ep->ep.ops = &dummy_ep_ops;
		list_add_tail (&ep->ep.ep_list, &dum->gadget.ep_list);
		ep->halted = ep->already_seen = ep->setup_stage = 0;
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

	dum->driver = driver;
	dum->gadget.dev.driver = &driver->driver;
	dev_dbg (dummy_dev(dum), "binding gadget driver '%s'\n",
			driver->driver.name);
	if ((retval = driver->bind (&dum->gadget)) != 0) {
		dum->driver = NULL;
		dum->gadget.dev.driver = NULL;
		return retval;
	}

	// FIXME: Check these calls for errors and re-order
	driver->driver.bus = dum->gadget.dev.parent->bus;
	driver_register (&driver->driver);

	device_bind_driver (&dum->gadget.dev);

	/* khubd will enumerate this in a while */
	dum->port_status |= USB_PORT_STAT_CONNECTION
		| (1 << USB_PORT_FEAT_C_CONNECTION);
	return 0;
}
EXPORT_SYMBOL (usb_gadget_register_driver);

/* caller must hold lock */
static void
stop_activity (struct dummy *dum, struct usb_gadget_driver *driver)
{
	struct dummy_ep	*ep;

	/* prevent any more requests */
	dum->address = 0;

	/* The timer is left running so that outstanding URBs can fail */

	/* nuke any pending requests first, so driver i/o is quiesced */
	list_for_each_entry (ep, &dum->gadget.ep_list, ep.ep_list)
		nuke (dum, ep);

	/* driver now does any non-usb quiescing necessary */
	if (driver) {
		spin_unlock (&dum->lock);
		driver->disconnect (&dum->gadget);
		spin_lock (&dum->lock);
	}
}

int
usb_gadget_unregister_driver (struct usb_gadget_driver *driver)
{
	struct dummy	*dum = the_controller;
	unsigned long	flags;

	if (!dum)
		return -ENODEV;
	if (!driver || driver != dum->driver)
		return -EINVAL;

	dev_dbg (dummy_dev(dum), "unregister gadget driver '%s'\n",
			driver->driver.name);

	spin_lock_irqsave (&dum->lock, flags);
	stop_activity (dum, driver);
	dum->port_status &= ~(USB_PORT_STAT_CONNECTION | USB_PORT_STAT_ENABLE |
			USB_PORT_STAT_LOW_SPEED | USB_PORT_STAT_HIGH_SPEED);
	dum->port_status |= (1 << USB_PORT_FEAT_C_CONNECTION);
	spin_unlock_irqrestore (&dum->lock, flags);

	driver->unbind (&dum->gadget);
	dum->driver = NULL;

	device_release_driver (&dum->gadget.dev);

	driver_unregister (&driver->driver);

	return 0;
}
EXPORT_SYMBOL (usb_gadget_unregister_driver);

#undef is_enabled

int net2280_set_fifo_mode (struct usb_gadget *gadget, int mode)
{
	return -ENOSYS;
}
EXPORT_SYMBOL (net2280_set_fifo_mode);

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
	struct usb_host_endpoint	*ep,
	struct urb			*urb,
	int				mem_flags
) {
	struct dummy	*dum;
	struct urbp	*urbp;
	unsigned long	flags;

	if (!urb->transfer_buffer && urb->transfer_buffer_length)
		return -EINVAL;

	urbp = kmalloc (sizeof *urbp, mem_flags);
	if (!urbp)
		return -ENOMEM;
	urbp->urb = urb;

	dum = hcd_to_dummy (hcd);
	spin_lock_irqsave (&dum->lock, flags);

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

	spin_unlock_irqrestore (&dum->lock, flags);
	return 0;
}

static int dummy_urb_dequeue (struct usb_hcd *hcd, struct urb *urb)
{
	/* giveback happens automatically in timer callback */
	return 0;
}

static void maybe_set_status (struct urb *urb, int status)
{
	spin_lock (&urb->lock);
	if (urb->status == -EINPROGRESS)
		urb->status = status;
	spin_unlock (&urb->lock);
}

/* transfer up to a frame's worth; caller must own lock */
static int
transfer (struct dummy *dum, struct urb *urb, struct dummy_ep *ep, int limit)
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
		 * need to emulate such data-in-flight.  so we only show part
		 * of the URB_SHORT_NOT_OK effect: completion status.
		 */
		if (is_short) {
			if (host_len == dev_len) {
				req->req.status = 0;
				maybe_set_status (urb, 0);
			} else if (to_host) {
				req->req.status = 0;
				if (dev_len > host_len)
					maybe_set_status (urb, -EOVERFLOW);
				else
					maybe_set_status (urb,
						(urb->transfer_flags
							& URB_SHORT_NOT_OK)
						? -EREMOTEIO : 0);
			} else if (!to_host) {
				maybe_set_status (urb, 0);
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
						& URB_ZERO_PACKET)) {
				maybe_set_status (urb, 0);
			}
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
		if (urb->status != -EINPROGRESS)
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
		tmp = le16_to_cpu (tmp);
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

		urb = urbp->urb;
		if (urb->status != -EINPROGRESS) {
			/* likely it was just unlinked */
			goto return_urb;
		}
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
			maybe_set_status (urb, -EPROTO);
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
			maybe_set_status (urb, -EPIPE);
			goto return_urb;
		}
		/* FIXME make sure both ends agree on maxpacket */

		/* handle control requests */
		if (ep == &dum->ep [0] && ep->setup_stage) {
			struct usb_ctrlrequest		setup;
			int				value = 1;
			struct dummy_ep			*ep2;

			setup = *(struct usb_ctrlrequest*) urb->setup_packet;
			le16_to_cpus (&setup.wIndex);
			le16_to_cpus (&setup.wValue);
			le16_to_cpus (&setup.wLength);
			if (setup.wLength != urb->transfer_buffer_length) {
				maybe_set_status (urb, -EOVERFLOW);
				goto return_urb;
			}

			/* paranoia, in case of stale queued data */
			list_for_each_entry (req, &ep->queue, queue) {
				list_del_init (&req->queue);
				req->req.status = -EOVERFLOW;
				dev_dbg (dummy_dev(dum), "stale req = %p\n",
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
				dum->address = setup.wValue;
				maybe_set_status (urb, 0);
				dev_dbg (dummy_dev(dum), "set_address = %d\n",
						setup.wValue);
				value = 0;
				break;
			case USB_REQ_SET_FEATURE:
				if (setup.bRequestType == Dev_Request) {
					value = 0;
					switch (setup.wValue) {
					case USB_DEVICE_REMOTE_WAKEUP:
						break;
					default:
						value = -EOPNOTSUPP;
					}
					if (value == 0) {
						dum->devstatus |=
							(1 << setup.wValue);
						maybe_set_status (urb, 0);
					}

				} else if (setup.bRequestType == Ep_Request) {
					// endpoint halt
					ep2 = find_endpoint (dum,
							setup.wIndex);
					if (!ep2) {
						value = -EOPNOTSUPP;
						break;
					}
					ep2->halted = 1;
					value = 0;
					maybe_set_status (urb, 0);
				}
				break;
			case USB_REQ_CLEAR_FEATURE:
				if (setup.bRequestType == Dev_Request) {
					switch (setup.wValue) {
					case USB_DEVICE_REMOTE_WAKEUP:
						dum->devstatus &= ~(1 <<
							USB_DEVICE_REMOTE_WAKEUP);
						value = 0;
						maybe_set_status (urb, 0);
						break;
					default:
						value = -EOPNOTSUPP;
						break;
					}
				} else if (setup.bRequestType == Ep_Request) {
					// endpoint halt
					ep2 = find_endpoint (dum,
							setup.wIndex);
					if (!ep2) {
						value = -EOPNOTSUPP;
						break;
					}
					ep2->halted = 0;
					value = 0;
					maybe_set_status (urb, 0);
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
	ep2 = find_endpoint (dum, setup.wIndex);
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
					urb->actual_length = min (2,
						urb->transfer_buffer_length);
					value = 0;
					maybe_set_status (urb, 0);
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
					dev_dbg (dummy_dev(dum),
						"setup --> %d\n",
						value);
				maybe_set_status (urb, -EPIPE);
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
			maybe_set_status (urb, -ENOSYS);
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
			total = transfer (dum, urb, ep, limit);
			break;
		}

		/* incomplete transfer? */
		if (urb->status == -EINPROGRESS)
			continue;

return_urb:
		urb->hcpriv = NULL;
		list_del (&urbp->urbp_list);
		kfree (urbp);
		if (ep)
			ep->already_seen = ep->setup_stage = 0;

		spin_unlock (&dum->lock);
		usb_hcd_giveback_urb (dummy_to_hcd(dum), urb, NULL);
		spin_lock (&dum->lock);

		goto restart;
	}

	/* want a 1 msec delay here */
	if (!list_empty (&dum->urbp_list))
		mod_timer (&dum->timer, jiffies + msecs_to_jiffies(1));
	else {
		usb_put_dev (dum->udev);
		dum->udev = NULL;
	}

	spin_unlock_irqrestore (&dum->lock, flags);
}

/*-------------------------------------------------------------------------*/

#define PORT_C_MASK \
	 ((1 << USB_PORT_FEAT_C_CONNECTION) \
	| (1 << USB_PORT_FEAT_C_ENABLE) \
	| (1 << USB_PORT_FEAT_C_SUSPEND) \
	| (1 << USB_PORT_FEAT_C_OVER_CURRENT) \
	| (1 << USB_PORT_FEAT_C_RESET))

static int dummy_hub_status (struct usb_hcd *hcd, char *buf)
{
	struct dummy		*dum;
	unsigned long		flags;
	int			retval;

	dum = hcd_to_dummy (hcd);

	spin_lock_irqsave (&dum->lock, flags);
	if (!(dum->port_status & PORT_C_MASK))
		retval = 0;
	else {
		*buf = (1 << 1);
		dev_dbg (dummy_dev(dum), "port status 0x%08x has changes\n",
			dum->port_status);
		retval = 1;
	}
	spin_unlock_irqrestore (&dum->lock, flags);
	return retval;
}

static inline void
hub_descriptor (struct usb_hub_descriptor *desc)
{
	memset (desc, 0, sizeof *desc);
	desc->bDescriptorType = 0x29;
	desc->bDescLength = 9;
	desc->wHubCharacteristics = __constant_cpu_to_le16 (0x0001);
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

	dum = hcd_to_dummy (hcd);
	spin_lock_irqsave (&dum->lock, flags);
	switch (typeReq) {
	case ClearHubFeature:
		break;
	case ClearPortFeature:
		switch (wValue) {
		case USB_PORT_FEAT_SUSPEND:
			if (dum->port_status & (1 << USB_PORT_FEAT_SUSPEND)) {
				/* 20msec resume signaling */
				dum->resuming = 1;
				dum->re_timeout = jiffies +
							msecs_to_jiffies(20);
			}
			break;
		case USB_PORT_FEAT_POWER:
			dum->port_status = 0;
			dum->resuming = 0;
			stop_activity(dum, dum->driver);
			break;
		default:
			dum->port_status &= ~(1 << wValue);
		}
		break;
	case GetHubDescriptor:
		hub_descriptor ((struct usb_hub_descriptor *) buf);
		break;
	case GetHubStatus:
		*(u32 *) buf = __constant_cpu_to_le32 (0);
		break;
	case GetPortStatus:
		if (wIndex != 1)
			retval = -EPIPE;

		/* whoever resets or resumes must GetPortStatus to
		 * complete it!!
		 */
		if (dum->resuming && time_after (jiffies, dum->re_timeout)) {
			dum->port_status |= (1 << USB_PORT_FEAT_C_SUSPEND);
			dum->port_status &= ~(1 << USB_PORT_FEAT_SUSPEND);
			dum->resuming = 0;
			dum->re_timeout = 0;
			if (dum->driver && dum->driver->resume) {
				spin_unlock (&dum->lock);
				dum->driver->resume (&dum->gadget);
				spin_lock (&dum->lock);
			}
		}
		if ((dum->port_status & (1 << USB_PORT_FEAT_RESET)) != 0
				&& time_after (jiffies, dum->re_timeout)) {
			dum->port_status |= (1 << USB_PORT_FEAT_C_RESET);
			dum->port_status &= ~(1 << USB_PORT_FEAT_RESET);
			dum->re_timeout = 0;
			if (dum->driver) {
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
		((u16 *) buf)[0] = cpu_to_le16 (dum->port_status);
		((u16 *) buf)[1] = cpu_to_le16 (dum->port_status >> 16);
		break;
	case SetHubFeature:
		retval = -EPIPE;
		break;
	case SetPortFeature:
		switch (wValue) {
		case USB_PORT_FEAT_SUSPEND:
			if ((dum->port_status & (1 << USB_PORT_FEAT_SUSPEND))
					== 0) {
				dum->port_status |=
						(1 << USB_PORT_FEAT_SUSPEND);
				if (dum->driver && dum->driver->suspend) {
					spin_unlock (&dum->lock);
					dum->driver->suspend (&dum->gadget);
					spin_lock (&dum->lock);
				}
			}
			break;
		case USB_PORT_FEAT_RESET:
			/* if it's already running, disconnect first */
			if (dum->port_status & USB_PORT_STAT_ENABLE) {
				dum->port_status &= ~(USB_PORT_STAT_ENABLE
						| USB_PORT_STAT_LOW_SPEED
						| USB_PORT_STAT_HIGH_SPEED);
				if (dum->driver) {
					dev_dbg (dummy_dev(dum),
							"disconnect\n");
					stop_activity (dum, dum->driver);
				}

				/* FIXME test that code path! */
			}
			/* 50msec reset signaling */
			dum->re_timeout = jiffies + msecs_to_jiffies(50);
			/* FALLTHROUGH */
		default:
			dum->port_status |= (1 << wValue);
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
	return retval;
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
show_urbs (struct device *dev, char *buf)
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
	struct usb_device	*root;
	int			retval;

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

	INIT_LIST_HEAD (&dum->urbp_list);

	root = usb_alloc_dev (NULL, &hcd->self, 0);
	if (!root)
		return -ENOMEM;

	/* root hub enters addressed state... */
	hcd->state = HC_STATE_RUNNING;
	root->speed = USB_SPEED_HIGH;

	/* ...then configured, so khubd sees us. */
	if ((retval = usb_hcd_register_root_hub (root, hcd)) != 0) {
		goto err1;
	}

	/* only show a low-power port: just 8mA */
	hub_set_power_budget (root, 8);

	if ((retval = dummy_register_udc (dum)) != 0)
		goto err2;

	/* FIXME 'urbs' should be a per-device thing, maybe in usbcore */
	device_create_file (dummy_dev(dum), &dev_attr_urbs);
	return 0;

 err2:
	usb_disconnect (&hcd->self.root_hub);
 err1:
	usb_put_dev (root);
	hcd->state = HC_STATE_QUIESCING;
	return retval;
}

static void dummy_stop (struct usb_hcd *hcd)
{
	struct dummy		*dum;

	dum = hcd_to_dummy (hcd);

	device_remove_file (dummy_dev(dum), &dev_attr_urbs);

	usb_gadget_unregister_driver (dum->driver);
	dummy_unregister_udc (dum);

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
};

static int dummy_probe (struct device *dev)
{
	struct usb_hcd		*hcd;
	int			retval;

	dev_info (dev, "%s, driver " DRIVER_VERSION "\n", driver_desc);

	hcd = usb_create_hcd (&dummy_hcd, dev, dev->bus_id);
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

static void dummy_remove (struct device *dev)
{
	struct usb_hcd		*hcd;

	hcd = dev_get_drvdata (dev);
	usb_remove_hcd (hcd);
	usb_put_hcd (hcd);
	the_controller = NULL;
}

/*-------------------------------------------------------------------------*/

static int dummy_pdev_detect (void)
{
	int			retval;

	retval = driver_register (&dummy_driver);
	if (retval < 0)
		return retval;

	the_pdev.name = "hc";
	the_pdev.dev.driver = &dummy_driver;
	the_pdev.dev.release = dummy_pdev_release;

	retval = platform_device_register (&the_pdev);
	if (retval < 0)
		driver_unregister (&dummy_driver);
	return retval;
}

static void dummy_pdev_remove (void)
{
	platform_device_unregister (&the_pdev);
	driver_unregister (&dummy_driver);
}

/*-------------------------------------------------------------------------*/

static int __init init (void)
{
	int	retval;

	if (usb_disabled ())
		return -ENODEV;
	if ((retval = dummy_pdev_detect ()) != 0)
		return retval;
	if ((retval = dummy_probe (&the_pdev.dev)) != 0)
		dummy_pdev_remove ();
	return retval;
}
module_init (init);

static void __exit cleanup (void)
{
	dummy_remove (&the_pdev.dev);
	dummy_pdev_remove ();
}
module_exit (cleanup);
