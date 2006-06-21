/*
 * (C) Copyright Linus Torvalds 1999
 * (C) Copyright Johannes Erdfelt 1999-2001
 * (C) Copyright Andreas Gal 1999
 * (C) Copyright Gregory P. Smith 1999
 * (C) Copyright Deti Fliegl 1999
 * (C) Copyright Randy Dunlap 2000
 * (C) Copyright David Brownell 2000-2002
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/completion.h>
#include <linux/utsname.h>
#include <linux/mm.h>
#include <asm/io.h>
#include <asm/scatterlist.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/mutex.h>
#include <asm/irq.h>
#include <asm/byteorder.h>

#include <linux/usb.h>

#include "usb.h"
#include "hcd.h"
#include "hub.h"


// #define USB_BANDWIDTH_MESSAGES

/*-------------------------------------------------------------------------*/

/*
 * USB Host Controller Driver framework
 *
 * Plugs into usbcore (usb_bus) and lets HCDs share code, minimizing
 * HCD-specific behaviors/bugs.
 *
 * This does error checks, tracks devices and urbs, and delegates to a
 * "hc_driver" only for code (and data) that really needs to know about
 * hardware differences.  That includes root hub registers, i/o queues,
 * and so on ... but as little else as possible.
 *
 * Shared code includes most of the "root hub" code (these are emulated,
 * though each HC's hardware works differently) and PCI glue, plus request
 * tracking overhead.  The HCD code should only block on spinlocks or on
 * hardware handshaking; blocking on software events (such as other kernel
 * threads releasing resources, or completing actions) is all generic.
 *
 * Happens the USB 2.0 spec says this would be invisible inside the "USBD",
 * and includes mostly a "HCDI" (HCD Interface) along with some APIs used
 * only by the hub driver ... and that neither should be seen or used by
 * usb client device drivers.
 *
 * Contributors of ideas or unattributed patches include: David Brownell,
 * Roman Weissgaerber, Rory Bolt, Greg Kroah-Hartman, ...
 *
 * HISTORY:
 * 2002-02-21	Pull in most of the usb_bus support from usb.c; some
 *		associated cleanup.  "usb_hcd" still != "usb_bus".
 * 2001-12-12	Initial patch version for Linux 2.5.1 kernel.
 */

/*-------------------------------------------------------------------------*/

/* host controllers we manage */
LIST_HEAD (usb_bus_list);
EXPORT_SYMBOL_GPL (usb_bus_list);

/* used when allocating bus numbers */
#define USB_MAXBUS		64
struct usb_busmap {
	unsigned long busmap [USB_MAXBUS / (8*sizeof (unsigned long))];
};
static struct usb_busmap busmap;

/* used when updating list of hcds */
DEFINE_MUTEX(usb_bus_list_lock);	/* exported only for usbfs */
EXPORT_SYMBOL_GPL (usb_bus_list_lock);

/* used for controlling access to virtual root hubs */
static DEFINE_SPINLOCK(hcd_root_hub_lock);

/* used when updating hcd data */
static DEFINE_SPINLOCK(hcd_data_lock);

/* wait queue for synchronous unlinks */
DECLARE_WAIT_QUEUE_HEAD(usb_kill_urb_queue);

/*-------------------------------------------------------------------------*/

/*
 * Sharable chunks of root hub code.
 */

/*-------------------------------------------------------------------------*/

#define KERNEL_REL	((LINUX_VERSION_CODE >> 16) & 0x0ff)
#define KERNEL_VER	((LINUX_VERSION_CODE >> 8) & 0x0ff)

/* usb 2.0 root hub device descriptor */
static const u8 usb2_rh_dev_descriptor [18] = {
	0x12,       /*  __u8  bLength; */
	0x01,       /*  __u8  bDescriptorType; Device */
	0x00, 0x02, /*  __le16 bcdUSB; v2.0 */

	0x09,	    /*  __u8  bDeviceClass; HUB_CLASSCODE */
	0x00,	    /*  __u8  bDeviceSubClass; */
	0x01,       /*  __u8  bDeviceProtocol; [ usb 2.0 single TT ]*/
	0x40,       /*  __u8  bMaxPacketSize0; 64 Bytes */

	0x00, 0x00, /*  __le16 idVendor; */
 	0x00, 0x00, /*  __le16 idProduct; */
	KERNEL_VER, KERNEL_REL, /*  __le16 bcdDevice */

	0x03,       /*  __u8  iManufacturer; */
	0x02,       /*  __u8  iProduct; */
	0x01,       /*  __u8  iSerialNumber; */
	0x01        /*  __u8  bNumConfigurations; */
};

/* no usb 2.0 root hub "device qualifier" descriptor: one speed only */

/* usb 1.1 root hub device descriptor */
static const u8 usb11_rh_dev_descriptor [18] = {
	0x12,       /*  __u8  bLength; */
	0x01,       /*  __u8  bDescriptorType; Device */
	0x10, 0x01, /*  __le16 bcdUSB; v1.1 */

	0x09,	    /*  __u8  bDeviceClass; HUB_CLASSCODE */
	0x00,	    /*  __u8  bDeviceSubClass; */
	0x00,       /*  __u8  bDeviceProtocol; [ low/full speeds only ] */
	0x40,       /*  __u8  bMaxPacketSize0; 64 Bytes */

	0x00, 0x00, /*  __le16 idVendor; */
 	0x00, 0x00, /*  __le16 idProduct; */
	KERNEL_VER, KERNEL_REL, /*  __le16 bcdDevice */

	0x03,       /*  __u8  iManufacturer; */
	0x02,       /*  __u8  iProduct; */
	0x01,       /*  __u8  iSerialNumber; */
	0x01        /*  __u8  bNumConfigurations; */
};


/*-------------------------------------------------------------------------*/

/* Configuration descriptors for our root hubs */

static const u8 fs_rh_config_descriptor [] = {

	/* one configuration */
	0x09,       /*  __u8  bLength; */
	0x02,       /*  __u8  bDescriptorType; Configuration */
	0x19, 0x00, /*  __le16 wTotalLength; */
	0x01,       /*  __u8  bNumInterfaces; (1) */
	0x01,       /*  __u8  bConfigurationValue; */
	0x00,       /*  __u8  iConfiguration; */
	0xc0,       /*  __u8  bmAttributes; 
				 Bit 7: must be set,
				     6: Self-powered,
				     5: Remote wakeup,
				     4..0: resvd */
	0x00,       /*  __u8  MaxPower; */
      
	/* USB 1.1:
	 * USB 2.0, single TT organization (mandatory):
	 *	one interface, protocol 0
	 *
	 * USB 2.0, multiple TT organization (optional):
	 *	two interfaces, protocols 1 (like single TT)
	 *	and 2 (multiple TT mode) ... config is
	 *	sometimes settable
	 *	NOT IMPLEMENTED
	 */

	/* one interface */
	0x09,       /*  __u8  if_bLength; */
	0x04,       /*  __u8  if_bDescriptorType; Interface */
	0x00,       /*  __u8  if_bInterfaceNumber; */
	0x00,       /*  __u8  if_bAlternateSetting; */
	0x01,       /*  __u8  if_bNumEndpoints; */
	0x09,       /*  __u8  if_bInterfaceClass; HUB_CLASSCODE */
	0x00,       /*  __u8  if_bInterfaceSubClass; */
	0x00,       /*  __u8  if_bInterfaceProtocol; [usb1.1 or single tt] */
	0x00,       /*  __u8  if_iInterface; */
     
	/* one endpoint (status change endpoint) */
	0x07,       /*  __u8  ep_bLength; */
	0x05,       /*  __u8  ep_bDescriptorType; Endpoint */
	0x81,       /*  __u8  ep_bEndpointAddress; IN Endpoint 1 */
 	0x03,       /*  __u8  ep_bmAttributes; Interrupt */
 	0x02, 0x00, /*  __le16 ep_wMaxPacketSize; 1 + (MAX_ROOT_PORTS / 8) */
	0xff        /*  __u8  ep_bInterval; (255ms -- usb 2.0 spec) */
};

static const u8 hs_rh_config_descriptor [] = {

	/* one configuration */
	0x09,       /*  __u8  bLength; */
	0x02,       /*  __u8  bDescriptorType; Configuration */
	0x19, 0x00, /*  __le16 wTotalLength; */
	0x01,       /*  __u8  bNumInterfaces; (1) */
	0x01,       /*  __u8  bConfigurationValue; */
	0x00,       /*  __u8  iConfiguration; */
	0xc0,       /*  __u8  bmAttributes; 
				 Bit 7: must be set,
				     6: Self-powered,
				     5: Remote wakeup,
				     4..0: resvd */
	0x00,       /*  __u8  MaxPower; */
      
	/* USB 1.1:
	 * USB 2.0, single TT organization (mandatory):
	 *	one interface, protocol 0
	 *
	 * USB 2.0, multiple TT organization (optional):
	 *	two interfaces, protocols 1 (like single TT)
	 *	and 2 (multiple TT mode) ... config is
	 *	sometimes settable
	 *	NOT IMPLEMENTED
	 */

	/* one interface */
	0x09,       /*  __u8  if_bLength; */
	0x04,       /*  __u8  if_bDescriptorType; Interface */
	0x00,       /*  __u8  if_bInterfaceNumber; */
	0x00,       /*  __u8  if_bAlternateSetting; */
	0x01,       /*  __u8  if_bNumEndpoints; */
	0x09,       /*  __u8  if_bInterfaceClass; HUB_CLASSCODE */
	0x00,       /*  __u8  if_bInterfaceSubClass; */
	0x00,       /*  __u8  if_bInterfaceProtocol; [usb1.1 or single tt] */
	0x00,       /*  __u8  if_iInterface; */
     
	/* one endpoint (status change endpoint) */
	0x07,       /*  __u8  ep_bLength; */
	0x05,       /*  __u8  ep_bDescriptorType; Endpoint */
	0x81,       /*  __u8  ep_bEndpointAddress; IN Endpoint 1 */
 	0x03,       /*  __u8  ep_bmAttributes; Interrupt */
 	0x02, 0x00, /*  __le16 ep_wMaxPacketSize; 1 + (MAX_ROOT_PORTS / 8) */
	0x0c        /*  __u8  ep_bInterval; (256ms -- usb 2.0 spec) */
};

/*-------------------------------------------------------------------------*/

/*
 * helper routine for returning string descriptors in UTF-16LE
 * input can actually be ISO-8859-1; ASCII is its 7-bit subset
 */
static int ascii2utf (char *s, u8 *utf, int utfmax)
{
	int retval;

	for (retval = 0; *s && utfmax > 1; utfmax -= 2, retval += 2) {
		*utf++ = *s++;
		*utf++ = 0;
	}
	if (utfmax > 0) {
		*utf = *s;
		++retval;
	}
	return retval;
}

/*
 * rh_string - provides manufacturer, product and serial strings for root hub
 * @id: the string ID number (1: serial number, 2: product, 3: vendor)
 * @hcd: the host controller for this root hub
 * @type: string describing our driver 
 * @data: return packet in UTF-16 LE
 * @len: length of the return packet
 *
 * Produces either a manufacturer, product or serial number string for the
 * virtual root hub device.
 */
static int rh_string (
	int		id,
	struct usb_hcd	*hcd,
	u8		*data,
	int		len
) {
	char buf [100];

	// language ids
	if (id == 0) {
		buf[0] = 4;    buf[1] = 3;	/* 4 bytes string data */
		buf[2] = 0x09; buf[3] = 0x04;	/* MSFT-speak for "en-us" */
		len = min (len, 4);
		memcpy (data, buf, len);
		return len;

	// serial number
	} else if (id == 1) {
		strlcpy (buf, hcd->self.bus_name, sizeof buf);

	// product description
	} else if (id == 2) {
		strlcpy (buf, hcd->product_desc, sizeof buf);

 	// id 3 == vendor description
	} else if (id == 3) {
		snprintf (buf, sizeof buf, "%s %s %s", system_utsname.sysname,
			system_utsname.release, hcd->driver->description);

	// unsupported IDs --> "protocol stall"
	} else
		return -EPIPE;

	switch (len) {		/* All cases fall through */
	default:
		len = 2 + ascii2utf (buf, data + 2, len - 2);
	case 2:
		data [1] = 3;	/* type == string */
	case 1:
		data [0] = 2 * (strlen (buf) + 1);
	case 0:
		;		/* Compiler wants a statement here */
	}
	return len;
}


/* Root hub control transfers execute synchronously */
static int rh_call_control (struct usb_hcd *hcd, struct urb *urb)
{
	struct usb_ctrlrequest *cmd;
 	u16		typeReq, wValue, wIndex, wLength;
	u8		*ubuf = urb->transfer_buffer;
	u8		tbuf [sizeof (struct usb_hub_descriptor)];
	const u8	*bufp = tbuf;
	int		len = 0;
	int		patch_wakeup = 0;
	unsigned long	flags;
	int		status = 0;
	int		n;

	cmd = (struct usb_ctrlrequest *) urb->setup_packet;
	typeReq  = (cmd->bRequestType << 8) | cmd->bRequest;
	wValue   = le16_to_cpu (cmd->wValue);
	wIndex   = le16_to_cpu (cmd->wIndex);
	wLength  = le16_to_cpu (cmd->wLength);

	if (wLength > urb->transfer_buffer_length)
		goto error;

	urb->actual_length = 0;
	switch (typeReq) {

	/* DEVICE REQUESTS */

	/* The root hub's remote wakeup enable bit is implemented using
	 * driver model wakeup flags.  If this system supports wakeup
	 * through USB, userspace may change the default "allow wakeup"
	 * policy through sysfs or these calls.
	 *
	 * Most root hubs support wakeup from downstream devices, for
	 * runtime power management (disabling USB clocks and reducing
	 * VBUS power usage).  However, not all of them do so; silicon,
	 * board, and BIOS bugs here are not uncommon, so these can't
	 * be treated quite like external hubs.
	 *
	 * Likewise, not all root hubs will pass wakeup events upstream,
	 * to wake up the whole system.  So don't assume root hub and
	 * controller capabilities are identical.
	 */

	case DeviceRequest | USB_REQ_GET_STATUS:
		tbuf [0] = (device_may_wakeup(&hcd->self.root_hub->dev)
					<< USB_DEVICE_REMOTE_WAKEUP)
				| (1 << USB_DEVICE_SELF_POWERED);
		tbuf [1] = 0;
		len = 2;
		break;
	case DeviceOutRequest | USB_REQ_CLEAR_FEATURE:
		if (wValue == USB_DEVICE_REMOTE_WAKEUP)
			device_set_wakeup_enable(&hcd->self.root_hub->dev, 0);
		else
			goto error;
		break;
	case DeviceOutRequest | USB_REQ_SET_FEATURE:
		if (device_can_wakeup(&hcd->self.root_hub->dev)
				&& wValue == USB_DEVICE_REMOTE_WAKEUP)
			device_set_wakeup_enable(&hcd->self.root_hub->dev, 1);
		else
			goto error;
		break;
	case DeviceRequest | USB_REQ_GET_CONFIGURATION:
		tbuf [0] = 1;
		len = 1;
			/* FALLTHROUGH */
	case DeviceOutRequest | USB_REQ_SET_CONFIGURATION:
		break;
	case DeviceRequest | USB_REQ_GET_DESCRIPTOR:
		switch (wValue & 0xff00) {
		case USB_DT_DEVICE << 8:
			if (hcd->driver->flags & HCD_USB2)
				bufp = usb2_rh_dev_descriptor;
			else if (hcd->driver->flags & HCD_USB11)
				bufp = usb11_rh_dev_descriptor;
			else
				goto error;
			len = 18;
			break;
		case USB_DT_CONFIG << 8:
			if (hcd->driver->flags & HCD_USB2) {
				bufp = hs_rh_config_descriptor;
				len = sizeof hs_rh_config_descriptor;
			} else {
				bufp = fs_rh_config_descriptor;
				len = sizeof fs_rh_config_descriptor;
			}
			if (device_can_wakeup(&hcd->self.root_hub->dev))
				patch_wakeup = 1;
			break;
		case USB_DT_STRING << 8:
			n = rh_string (wValue & 0xff, hcd, ubuf, wLength);
			if (n < 0)
				goto error;
			urb->actual_length = n;
			break;
		default:
			goto error;
		}
		break;
	case DeviceRequest | USB_REQ_GET_INTERFACE:
		tbuf [0] = 0;
		len = 1;
			/* FALLTHROUGH */
	case DeviceOutRequest | USB_REQ_SET_INTERFACE:
		break;
	case DeviceOutRequest | USB_REQ_SET_ADDRESS:
		// wValue == urb->dev->devaddr
		dev_dbg (hcd->self.controller, "root hub device address %d\n",
			wValue);
		break;

	/* INTERFACE REQUESTS (no defined feature/status flags) */

	/* ENDPOINT REQUESTS */

	case EndpointRequest | USB_REQ_GET_STATUS:
		// ENDPOINT_HALT flag
		tbuf [0] = 0;
		tbuf [1] = 0;
		len = 2;
			/* FALLTHROUGH */
	case EndpointOutRequest | USB_REQ_CLEAR_FEATURE:
	case EndpointOutRequest | USB_REQ_SET_FEATURE:
		dev_dbg (hcd->self.controller, "no endpoint features yet\n");
		break;

	/* CLASS REQUESTS (and errors) */

	default:
		/* non-generic request */
		switch (typeReq) {
		case GetHubStatus:
		case GetPortStatus:
			len = 4;
			break;
		case GetHubDescriptor:
			len = sizeof (struct usb_hub_descriptor);
			break;
		}
		status = hcd->driver->hub_control (hcd,
			typeReq, wValue, wIndex,
			tbuf, wLength);
		break;
error:
		/* "protocol stall" on error */
		status = -EPIPE;
	}

	if (status) {
		len = 0;
		if (status != -EPIPE) {
			dev_dbg (hcd->self.controller,
				"CTRL: TypeReq=0x%x val=0x%x "
				"idx=0x%x len=%d ==> %d\n",
				typeReq, wValue, wIndex,
				wLength, status);
		}
	}
	if (len) {
		if (urb->transfer_buffer_length < len)
			len = urb->transfer_buffer_length;
		urb->actual_length = len;
		// always USB_DIR_IN, toward host
		memcpy (ubuf, bufp, len);

		/* report whether RH hardware supports remote wakeup */
		if (patch_wakeup &&
				len > offsetof (struct usb_config_descriptor,
						bmAttributes))
			((struct usb_config_descriptor *)ubuf)->bmAttributes
				|= USB_CONFIG_ATT_WAKEUP;
	}

	/* any errors get returned through the urb completion */
	local_irq_save (flags);
	spin_lock (&urb->lock);
	if (urb->status == -EINPROGRESS)
		urb->status = status;
	spin_unlock (&urb->lock);
	usb_hcd_giveback_urb (hcd, urb, NULL);
	local_irq_restore (flags);
	return 0;
}

/*-------------------------------------------------------------------------*/

/*
 * Root Hub interrupt transfers are polled using a timer if the
 * driver requests it; otherwise the driver is responsible for
 * calling usb_hcd_poll_rh_status() when an event occurs.
 *
 * Completions are called in_interrupt(), but they may or may not
 * be in_irq().
 */
void usb_hcd_poll_rh_status(struct usb_hcd *hcd)
{
	struct urb	*urb;
	int		length;
	unsigned long	flags;
	char		buffer[4];	/* Any root hubs with > 31 ports? */

	if (!hcd->uses_new_polling && !hcd->status_urb)
		return;

	length = hcd->driver->hub_status_data(hcd, buffer);
	if (length > 0) {

		/* try to complete the status urb */
		local_irq_save (flags);
		spin_lock(&hcd_root_hub_lock);
		urb = hcd->status_urb;
		if (urb) {
			spin_lock(&urb->lock);
			if (urb->status == -EINPROGRESS) {
				hcd->poll_pending = 0;
				hcd->status_urb = NULL;
				urb->status = 0;
				urb->hcpriv = NULL;
				urb->actual_length = length;
				memcpy(urb->transfer_buffer, buffer, length);
			} else		/* urb has been unlinked */
				length = 0;
			spin_unlock(&urb->lock);
		} else
			length = 0;
		spin_unlock(&hcd_root_hub_lock);

		/* local irqs are always blocked in completions */
		if (length > 0)
			usb_hcd_giveback_urb (hcd, urb, NULL);
		else
			hcd->poll_pending = 1;
		local_irq_restore (flags);
	}

	/* The USB 2.0 spec says 256 ms.  This is close enough and won't
	 * exceed that limit if HZ is 100. */
	if (hcd->uses_new_polling ? hcd->poll_rh :
			(length == 0 && hcd->status_urb != NULL))
		mod_timer (&hcd->rh_timer, jiffies + msecs_to_jiffies(250));
}
EXPORT_SYMBOL_GPL(usb_hcd_poll_rh_status);

/* timer callback */
static void rh_timer_func (unsigned long _hcd)
{
	usb_hcd_poll_rh_status((struct usb_hcd *) _hcd);
}

/*-------------------------------------------------------------------------*/

static int rh_queue_status (struct usb_hcd *hcd, struct urb *urb)
{
	int		retval;
	unsigned long	flags;
	int		len = 1 + (urb->dev->maxchild / 8);

	spin_lock_irqsave (&hcd_root_hub_lock, flags);
	if (urb->status != -EINPROGRESS)	/* already unlinked */
		retval = urb->status;
	else if (hcd->status_urb || urb->transfer_buffer_length < len) {
		dev_dbg (hcd->self.controller, "not queuing rh status urb\n");
		retval = -EINVAL;
	} else {
		hcd->status_urb = urb;
		urb->hcpriv = hcd;	/* indicate it's queued */

		if (!hcd->uses_new_polling)
			mod_timer (&hcd->rh_timer, jiffies +
					msecs_to_jiffies(250));

		/* If a status change has already occurred, report it ASAP */
		else if (hcd->poll_pending)
			mod_timer (&hcd->rh_timer, jiffies);
		retval = 0;
	}
	spin_unlock_irqrestore (&hcd_root_hub_lock, flags);
	return retval;
}

static int rh_urb_enqueue (struct usb_hcd *hcd, struct urb *urb)
{
	if (usb_pipeint (urb->pipe))
		return rh_queue_status (hcd, urb);
	if (usb_pipecontrol (urb->pipe))
		return rh_call_control (hcd, urb);
	return -EINVAL;
}

/*-------------------------------------------------------------------------*/

/* Asynchronous unlinks of root-hub control URBs are legal, but they
 * don't do anything.  Status URB unlinks must be made in process context
 * with interrupts enabled.
 */
static int usb_rh_urb_dequeue (struct usb_hcd *hcd, struct urb *urb)
{
	if (usb_pipeendpoint(urb->pipe) == 0) {	/* Control URB */
		if (in_interrupt())
			return 0;		/* nothing to do */

		spin_lock_irq(&urb->lock);	/* from usb_kill_urb */
		++urb->reject;
		spin_unlock_irq(&urb->lock);

		wait_event(usb_kill_urb_queue,
				atomic_read(&urb->use_count) == 0);

		spin_lock_irq(&urb->lock);
		--urb->reject;
		spin_unlock_irq(&urb->lock);

	} else {				/* Status URB */
		if (!hcd->uses_new_polling)
			del_timer_sync (&hcd->rh_timer);
		local_irq_disable ();
		spin_lock (&hcd_root_hub_lock);
		if (urb == hcd->status_urb) {
			hcd->status_urb = NULL;
			urb->hcpriv = NULL;
		} else
			urb = NULL;		/* wasn't fully queued */
		spin_unlock (&hcd_root_hub_lock);
		if (urb)
			usb_hcd_giveback_urb (hcd, urb, NULL);
		local_irq_enable ();
	}

	return 0;
}

/*-------------------------------------------------------------------------*/

/* exported only within usbcore */
struct usb_bus *usb_bus_get(struct usb_bus *bus)
{
	if (bus)
		kref_get(&bus->kref);
	return bus;
}

static void usb_host_release(struct kref *kref)
{
	struct usb_bus *bus = container_of(kref, struct usb_bus, kref);

	if (bus->release)
		bus->release(bus);
}

/* exported only within usbcore */
void usb_bus_put(struct usb_bus *bus)
{
	if (bus)
		kref_put(&bus->kref, usb_host_release);
}

/*-------------------------------------------------------------------------*/

static struct class *usb_host_class;

int usb_host_init(void)
{
	int retval = 0;

	usb_host_class = class_create(THIS_MODULE, "usb_host");
	if (IS_ERR(usb_host_class))
		retval = PTR_ERR(usb_host_class);
	return retval;
}

void usb_host_cleanup(void)
{
	class_destroy(usb_host_class);
}

/**
 * usb_bus_init - shared initialization code
 * @bus: the bus structure being initialized
 *
 * This code is used to initialize a usb_bus structure, memory for which is
 * separately managed.
 */
static void usb_bus_init (struct usb_bus *bus)
{
	memset (&bus->devmap, 0, sizeof(struct usb_devmap));

	bus->devnum_next = 1;

	bus->root_hub = NULL;
	bus->hcpriv = NULL;
	bus->busnum = -1;
	bus->bandwidth_allocated = 0;
	bus->bandwidth_int_reqs  = 0;
	bus->bandwidth_isoc_reqs = 0;

	INIT_LIST_HEAD (&bus->bus_list);

	kref_init(&bus->kref);
}

/**
 * usb_alloc_bus - creates a new USB host controller structure
 * @op: pointer to a struct usb_operations that this bus structure should use
 * Context: !in_interrupt()
 *
 * Creates a USB host controller bus structure with the specified 
 * usb_operations and initializes all the necessary internal objects.
 *
 * If no memory is available, NULL is returned.
 *
 * The caller should call usb_put_bus() when it is finished with the structure.
 */
struct usb_bus *usb_alloc_bus (struct usb_operations *op)
{
	struct usb_bus *bus;

	bus = kzalloc (sizeof *bus, GFP_KERNEL);
	if (!bus)
		return NULL;
	usb_bus_init (bus);
	bus->op = op;
	return bus;
}

/*-------------------------------------------------------------------------*/

/**
 * usb_register_bus - registers the USB host controller with the usb core
 * @bus: pointer to the bus to register
 * Context: !in_interrupt()
 *
 * Assigns a bus number, and links the controller into usbcore data
 * structures so that it can be seen by scanning the bus list.
 */
static int usb_register_bus(struct usb_bus *bus)
{
	int busnum;

	mutex_lock(&usb_bus_list_lock);
	busnum = find_next_zero_bit (busmap.busmap, USB_MAXBUS, 1);
	if (busnum < USB_MAXBUS) {
		set_bit (busnum, busmap.busmap);
		bus->busnum = busnum;
	} else {
		printk (KERN_ERR "%s: too many buses\n", usbcore_name);
		mutex_unlock(&usb_bus_list_lock);
		return -E2BIG;
	}

	bus->class_dev = class_device_create(usb_host_class, NULL, MKDEV(0,0),
					     bus->controller, "usb_host%d", busnum);
	if (IS_ERR(bus->class_dev)) {
		clear_bit(busnum, busmap.busmap);
		mutex_unlock(&usb_bus_list_lock);
		return PTR_ERR(bus->class_dev);
	}

	class_set_devdata(bus->class_dev, bus);

	/* Add it to the local list of buses */
	list_add (&bus->bus_list, &usb_bus_list);
	mutex_unlock(&usb_bus_list_lock);

	usb_notify_add_bus(bus);

	dev_info (bus->controller, "new USB bus registered, assigned bus number %d\n", bus->busnum);
	return 0;
}

/**
 * usb_deregister_bus - deregisters the USB host controller
 * @bus: pointer to the bus to deregister
 * Context: !in_interrupt()
 *
 * Recycles the bus number, and unlinks the controller from usbcore data
 * structures so that it won't be seen by scanning the bus list.
 */
static void usb_deregister_bus (struct usb_bus *bus)
{
	dev_info (bus->controller, "USB bus %d deregistered\n", bus->busnum);

	/*
	 * NOTE: make sure that all the devices are removed by the
	 * controller code, as well as having it call this when cleaning
	 * itself up
	 */
	mutex_lock(&usb_bus_list_lock);
	list_del (&bus->bus_list);
	mutex_unlock(&usb_bus_list_lock);

	usb_notify_remove_bus(bus);

	clear_bit (bus->busnum, busmap.busmap);

	class_device_unregister(bus->class_dev);
}

/**
 * register_root_hub - called by usb_add_hcd() to register a root hub
 * @hcd: host controller for this root hub
 *
 * This function registers the root hub with the USB subsystem.  It sets up
 * the device properly in the device tree and then calls usb_new_device()
 * to register the usb device.  It also assigns the root hub's USB address
 * (always 1).
 */
static int register_root_hub(struct usb_hcd *hcd)
{
	struct device *parent_dev = hcd->self.controller;
	struct usb_device *usb_dev = hcd->self.root_hub;
	const int devnum = 1;
	int retval;

	usb_dev->devnum = devnum;
	usb_dev->bus->devnum_next = devnum + 1;
	memset (&usb_dev->bus->devmap.devicemap, 0,
			sizeof usb_dev->bus->devmap.devicemap);
	set_bit (devnum, usb_dev->bus->devmap.devicemap);
	usb_set_device_state(usb_dev, USB_STATE_ADDRESS);

	mutex_lock(&usb_bus_list_lock);

	usb_dev->ep0.desc.wMaxPacketSize = __constant_cpu_to_le16(64);
	retval = usb_get_device_descriptor(usb_dev, USB_DT_DEVICE_SIZE);
	if (retval != sizeof usb_dev->descriptor) {
		mutex_unlock(&usb_bus_list_lock);
		dev_dbg (parent_dev, "can't read %s device descriptor %d\n",
				usb_dev->dev.bus_id, retval);
		return (retval < 0) ? retval : -EMSGSIZE;
	}

	retval = usb_new_device (usb_dev);
	if (retval) {
		dev_err (parent_dev, "can't register root hub for %s, %d\n",
				usb_dev->dev.bus_id, retval);
	}
	mutex_unlock(&usb_bus_list_lock);

	if (retval == 0) {
		spin_lock_irq (&hcd_root_hub_lock);
		hcd->rh_registered = 1;
		spin_unlock_irq (&hcd_root_hub_lock);

		/* Did the HC die before the root hub was registered? */
		if (hcd->state == HC_STATE_HALT)
			usb_hc_died (hcd);	/* This time clean up */
	}

	return retval;
}

void usb_enable_root_hub_irq (struct usb_bus *bus)
{
	struct usb_hcd *hcd;

	hcd = container_of (bus, struct usb_hcd, self);
	if (hcd->driver->hub_irq_enable && !hcd->poll_rh &&
			hcd->state != HC_STATE_HALT)
		hcd->driver->hub_irq_enable (hcd);
}


/*-------------------------------------------------------------------------*/

/**
 * usb_calc_bus_time - approximate periodic transaction time in nanoseconds
 * @speed: from dev->speed; USB_SPEED_{LOW,FULL,HIGH}
 * @is_input: true iff the transaction sends data to the host
 * @isoc: true for isochronous transactions, false for interrupt ones
 * @bytecount: how many bytes in the transaction.
 *
 * Returns approximate bus time in nanoseconds for a periodic transaction.
 * See USB 2.0 spec section 5.11.3; only periodic transfers need to be
 * scheduled in software, this function is only used for such scheduling.
 */
long usb_calc_bus_time (int speed, int is_input, int isoc, int bytecount)
{
	unsigned long	tmp;

	switch (speed) {
	case USB_SPEED_LOW: 	/* INTR only */
		if (is_input) {
			tmp = (67667L * (31L + 10L * BitTime (bytecount))) / 1000L;
			return (64060L + (2 * BW_HUB_LS_SETUP) + BW_HOST_DELAY + tmp);
		} else {
			tmp = (66700L * (31L + 10L * BitTime (bytecount))) / 1000L;
			return (64107L + (2 * BW_HUB_LS_SETUP) + BW_HOST_DELAY + tmp);
		}
	case USB_SPEED_FULL:	/* ISOC or INTR */
		if (isoc) {
			tmp = (8354L * (31L + 10L * BitTime (bytecount))) / 1000L;
			return (((is_input) ? 7268L : 6265L) + BW_HOST_DELAY + tmp);
		} else {
			tmp = (8354L * (31L + 10L * BitTime (bytecount))) / 1000L;
			return (9107L + BW_HOST_DELAY + tmp);
		}
	case USB_SPEED_HIGH:	/* ISOC or INTR */
		// FIXME adjust for input vs output
		if (isoc)
			tmp = HS_NSECS_ISO (bytecount);
		else
			tmp = HS_NSECS (bytecount);
		return tmp;
	default:
		pr_debug ("%s: bogus device speed!\n", usbcore_name);
		return -1;
	}
}
EXPORT_SYMBOL (usb_calc_bus_time);

/*
 * usb_check_bandwidth():
 *
 * old_alloc is from host_controller->bandwidth_allocated in microseconds;
 * bustime is from calc_bus_time(), but converted to microseconds.
 *
 * returns <bustime in us> if successful,
 * or -ENOSPC if bandwidth request fails.
 *
 * FIXME:
 * This initial implementation does not use Endpoint.bInterval
 * in managing bandwidth allocation.
 * It probably needs to be expanded to use Endpoint.bInterval.
 * This can be done as a later enhancement (correction).
 *
 * This will also probably require some kind of
 * frame allocation tracking...meaning, for example,
 * that if multiple drivers request interrupts every 10 USB frames,
 * they don't all have to be allocated at
 * frame numbers N, N+10, N+20, etc.  Some of them could be at
 * N+11, N+21, N+31, etc., and others at
 * N+12, N+22, N+32, etc.
 *
 * Similarly for isochronous transfers...
 *
 * Individual HCDs can schedule more directly ... this logic
 * is not correct for high speed transfers.
 */
int usb_check_bandwidth (struct usb_device *dev, struct urb *urb)
{
	unsigned int	pipe = urb->pipe;
	long		bustime;
	int		is_in = usb_pipein (pipe);
	int		is_iso = usb_pipeisoc (pipe);
	int		old_alloc = dev->bus->bandwidth_allocated;
	int		new_alloc;


	bustime = NS_TO_US (usb_calc_bus_time (dev->speed, is_in, is_iso,
			usb_maxpacket (dev, pipe, !is_in)));
	if (is_iso)
		bustime /= urb->number_of_packets;

	new_alloc = old_alloc + (int) bustime;
	if (new_alloc > FRAME_TIME_MAX_USECS_ALLOC) {
#ifdef	DEBUG
		char	*mode = 
#ifdef CONFIG_USB_BANDWIDTH
			"";
#else
			"would have ";
#endif
		dev_dbg (&dev->dev, "usb_check_bandwidth %sFAILED: %d + %ld = %d usec\n",
			mode, old_alloc, bustime, new_alloc);
#endif
#ifdef CONFIG_USB_BANDWIDTH
		bustime = -ENOSPC;	/* report error */
#endif
	}

	return bustime;
}
EXPORT_SYMBOL (usb_check_bandwidth);


/**
 * usb_claim_bandwidth - records bandwidth for a periodic transfer
 * @dev: source/target of request
 * @urb: request (urb->dev == dev)
 * @bustime: bandwidth consumed, in (average) microseconds per frame
 * @isoc: true iff the request is isochronous
 *
 * Bus bandwidth reservations are recorded purely for diagnostic purposes.
 * HCDs are expected not to overcommit periodic bandwidth, and to record such
 * reservations whenever endpoints are added to the periodic schedule.
 *
 * FIXME averaging per-frame is suboptimal.  Better to sum over the HCD's
 * entire periodic schedule ... 32 frames for OHCI, 1024 for UHCI, settable
 * for EHCI (256/512/1024 frames, default 1024) and have the bus expose how
 * large its periodic schedule is.
 */
void usb_claim_bandwidth (struct usb_device *dev, struct urb *urb, int bustime, int isoc)
{
	dev->bus->bandwidth_allocated += bustime;
	if (isoc)
		dev->bus->bandwidth_isoc_reqs++;
	else
		dev->bus->bandwidth_int_reqs++;
	urb->bandwidth = bustime;

#ifdef USB_BANDWIDTH_MESSAGES
	dev_dbg (&dev->dev, "bandwidth alloc increased by %d (%s) to %d for %d requesters\n",
		bustime,
		isoc ? "ISOC" : "INTR",
		dev->bus->bandwidth_allocated,
		dev->bus->bandwidth_int_reqs + dev->bus->bandwidth_isoc_reqs);
#endif
}
EXPORT_SYMBOL (usb_claim_bandwidth);


/**
 * usb_release_bandwidth - reverses effect of usb_claim_bandwidth()
 * @dev: source/target of request
 * @urb: request (urb->dev == dev)
 * @isoc: true iff the request is isochronous
 *
 * This records that previously allocated bandwidth has been released.
 * Bandwidth is released when endpoints are removed from the host controller's
 * periodic schedule.
 */
void usb_release_bandwidth (struct usb_device *dev, struct urb *urb, int isoc)
{
	dev->bus->bandwidth_allocated -= urb->bandwidth;
	if (isoc)
		dev->bus->bandwidth_isoc_reqs--;
	else
		dev->bus->bandwidth_int_reqs--;

#ifdef USB_BANDWIDTH_MESSAGES
	dev_dbg (&dev->dev, "bandwidth alloc reduced by %d (%s) to %d for %d requesters\n",
		urb->bandwidth,
		isoc ? "ISOC" : "INTR",
		dev->bus->bandwidth_allocated,
		dev->bus->bandwidth_int_reqs + dev->bus->bandwidth_isoc_reqs);
#endif
	urb->bandwidth = 0;
}
EXPORT_SYMBOL (usb_release_bandwidth);


/*-------------------------------------------------------------------------*/

/*
 * Generic HC operations.
 */

/*-------------------------------------------------------------------------*/

static void urb_unlink (struct urb *urb)
{
	unsigned long		flags;

	/* Release any periodic transfer bandwidth */
	if (urb->bandwidth)
		usb_release_bandwidth (urb->dev, urb,
			usb_pipeisoc (urb->pipe));

	/* clear all state linking urb to this dev (and hcd) */

	spin_lock_irqsave (&hcd_data_lock, flags);
	list_del_init (&urb->urb_list);
	spin_unlock_irqrestore (&hcd_data_lock, flags);
}


/* may be called in any context with a valid urb->dev usecount
 * caller surrenders "ownership" of urb
 * expects usb_submit_urb() to have sanity checked and conditioned all
 * inputs in the urb
 */
static int hcd_submit_urb (struct urb *urb, gfp_t mem_flags)
{
	int			status;
	struct usb_hcd		*hcd = urb->dev->bus->hcpriv;
	struct usb_host_endpoint *ep;
	unsigned long		flags;

	if (!hcd)
		return -ENODEV;

	usbmon_urb_submit(&hcd->self, urb);

	/*
	 * Atomically queue the urb,  first to our records, then to the HCD.
	 * Access to urb->status is controlled by urb->lock ... changes on
	 * i/o completion (normal or fault) or unlinking.
	 */

	// FIXME:  verify that quiescing hc works right (RH cleans up)

	spin_lock_irqsave (&hcd_data_lock, flags);
	ep = (usb_pipein(urb->pipe) ? urb->dev->ep_in : urb->dev->ep_out)
			[usb_pipeendpoint(urb->pipe)];
	if (unlikely (!ep))
		status = -ENOENT;
	else if (unlikely (urb->reject))
		status = -EPERM;
	else switch (hcd->state) {
	case HC_STATE_RUNNING:
	case HC_STATE_RESUMING:
doit:
		list_add_tail (&urb->urb_list, &ep->urb_list);
		status = 0;
		break;
	case HC_STATE_SUSPENDED:
		/* HC upstream links (register access, wakeup signaling) can work
		 * even when the downstream links (and DMA etc) are quiesced; let
		 * usbcore talk to the root hub.
		 */
		if (hcd->self.controller->power.power_state.event == PM_EVENT_ON
				&& urb->dev->parent == NULL)
			goto doit;
		/* FALL THROUGH */
	default:
		status = -ESHUTDOWN;
		break;
	}
	spin_unlock_irqrestore (&hcd_data_lock, flags);
	if (status) {
		INIT_LIST_HEAD (&urb->urb_list);
		usbmon_urb_submit_error(&hcd->self, urb, status);
		return status;
	}

	/* increment urb's reference count as part of giving it to the HCD
	 * (which now controls it).  HCD guarantees that it either returns
	 * an error or calls giveback(), but not both.
	 */
	urb = usb_get_urb (urb);
	atomic_inc (&urb->use_count);

	if (urb->dev == hcd->self.root_hub) {
		/* NOTE:  requirement on hub callers (usbfs and the hub
		 * driver, for now) that URBs' urb->transfer_buffer be
		 * valid and usb_buffer_{sync,unmap}() not be needed, since
		 * they could clobber root hub response data.
		 */
		status = rh_urb_enqueue (hcd, urb);
		goto done;
	}

	/* lower level hcd code should use *_dma exclusively,
	 * unless it uses pio or talks to another transport.
	 */
	if (hcd->self.controller->dma_mask) {
		if (usb_pipecontrol (urb->pipe)
			&& !(urb->transfer_flags & URB_NO_SETUP_DMA_MAP))
			urb->setup_dma = dma_map_single (
					hcd->self.controller,
					urb->setup_packet,
					sizeof (struct usb_ctrlrequest),
					DMA_TO_DEVICE);
		if (urb->transfer_buffer_length != 0
			&& !(urb->transfer_flags & URB_NO_TRANSFER_DMA_MAP))
			urb->transfer_dma = dma_map_single (
					hcd->self.controller,
					urb->transfer_buffer,
					urb->transfer_buffer_length,
					usb_pipein (urb->pipe)
					    ? DMA_FROM_DEVICE
					    : DMA_TO_DEVICE);
	}

	status = hcd->driver->urb_enqueue (hcd, ep, urb, mem_flags);
done:
	if (unlikely (status)) {
		urb_unlink (urb);
		atomic_dec (&urb->use_count);
		if (urb->reject)
			wake_up (&usb_kill_urb_queue);
		usb_put_urb (urb);
		usbmon_urb_submit_error(&hcd->self, urb, status);
	}
	return status;
}

/*-------------------------------------------------------------------------*/

/* called in any context */
static int hcd_get_frame_number (struct usb_device *udev)
{
	struct usb_hcd	*hcd = (struct usb_hcd *)udev->bus->hcpriv;
	if (!HC_IS_RUNNING (hcd->state))
		return -ESHUTDOWN;
	return hcd->driver->get_frame_number (hcd);
}

/*-------------------------------------------------------------------------*/

/* this makes the hcd giveback() the urb more quickly, by kicking it
 * off hardware queues (which may take a while) and returning it as
 * soon as practical.  we've already set up the urb's return status,
 * but we can't know if the callback completed already.
 */
static int
unlink1 (struct usb_hcd *hcd, struct urb *urb)
{
	int		value;

	if (urb->dev == hcd->self.root_hub)
		value = usb_rh_urb_dequeue (hcd, urb);
	else {

		/* The only reason an HCD might fail this call is if
		 * it has not yet fully queued the urb to begin with.
		 * Such failures should be harmless. */
		value = hcd->driver->urb_dequeue (hcd, urb);
	}

	if (value != 0)
		dev_dbg (hcd->self.controller, "dequeue %p --> %d\n",
				urb, value);
	return value;
}

/*
 * called in any context
 *
 * caller guarantees urb won't be recycled till both unlink()
 * and the urb's completion function return
 */
static int hcd_unlink_urb (struct urb *urb, int status)
{
	struct usb_host_endpoint	*ep;
	struct usb_hcd			*hcd = NULL;
	struct device			*sys = NULL;
	unsigned long			flags;
	struct list_head		*tmp;
	int				retval;

	if (!urb)
		return -EINVAL;
	if (!urb->dev || !urb->dev->bus)
		return -ENODEV;
	ep = (usb_pipein(urb->pipe) ? urb->dev->ep_in : urb->dev->ep_out)
			[usb_pipeendpoint(urb->pipe)];
	if (!ep)
		return -ENODEV;

	/*
	 * we contend for urb->status with the hcd core,
	 * which changes it while returning the urb.
	 *
	 * Caller guaranteed that the urb pointer hasn't been freed, and
	 * that it was submitted.  But as a rule it can't know whether or
	 * not it's already been unlinked ... so we respect the reversed
	 * lock sequence needed for the usb_hcd_giveback_urb() code paths
	 * (urb lock, then hcd_data_lock) in case some other CPU is now
	 * unlinking it.
	 */
	spin_lock_irqsave (&urb->lock, flags);
	spin_lock (&hcd_data_lock);

	sys = &urb->dev->dev;
	hcd = urb->dev->bus->hcpriv;
	if (hcd == NULL) {
		retval = -ENODEV;
		goto done;
	}

	/* insist the urb is still queued */
	list_for_each(tmp, &ep->urb_list) {
		if (tmp == &urb->urb_list)
			break;
	}
	if (tmp != &urb->urb_list) {
		retval = -EIDRM;
		goto done;
	}

	/* Any status except -EINPROGRESS means something already started to
	 * unlink this URB from the hardware.  So there's no more work to do.
	 */
	if (urb->status != -EINPROGRESS) {
		retval = -EBUSY;
		goto done;
	}

	/* IRQ setup can easily be broken so that USB controllers
	 * never get completion IRQs ... maybe even the ones we need to
	 * finish unlinking the initial failed usb_set_address()
	 * or device descriptor fetch.
	 */
	if (!test_bit(HCD_FLAG_SAW_IRQ, &hcd->flags)
	    && hcd->self.root_hub != urb->dev) {
		dev_warn (hcd->self.controller, "Unlink after no-IRQ?  "
			"Controller is probably using the wrong IRQ."
			"\n");
		set_bit(HCD_FLAG_SAW_IRQ, &hcd->flags);
	}

	urb->status = status;

	spin_unlock (&hcd_data_lock);
	spin_unlock_irqrestore (&urb->lock, flags);

	retval = unlink1 (hcd, urb);
	if (retval == 0)
		retval = -EINPROGRESS;
	return retval;

done:
	spin_unlock (&hcd_data_lock);
	spin_unlock_irqrestore (&urb->lock, flags);
	if (retval != -EIDRM && sys && sys->driver)
		dev_dbg (sys, "hcd_unlink_urb %p fail %d\n", urb, retval);
	return retval;
}

/*-------------------------------------------------------------------------*/

/* disables the endpoint: cancels any pending urbs, then synchronizes with
 * the hcd to make sure all endpoint state is gone from hardware. use for
 * set_configuration, set_interface, driver removal, physical disconnect.
 *
 * example:  a qh stored in ep->hcpriv, holding state related to endpoint
 * type, maxpacket size, toggle, halt status, and scheduling.
 */
static void
hcd_endpoint_disable (struct usb_device *udev, struct usb_host_endpoint *ep)
{
	struct usb_hcd		*hcd;
	struct urb		*urb;

	hcd = udev->bus->hcpriv;

	WARN_ON (!HC_IS_RUNNING (hcd->state) && hcd->state != HC_STATE_HALT &&
			udev->state != USB_STATE_NOTATTACHED);

	local_irq_disable ();

	/* FIXME move most of this into message.c as part of its
	 * endpoint disable logic
	 */

	/* ep is already gone from udev->ep_{in,out}[]; no more submits */
rescan:
	spin_lock (&hcd_data_lock);
	list_for_each_entry (urb, &ep->urb_list, urb_list) {
		int	tmp;

		/* another cpu may be in hcd, spinning on hcd_data_lock
		 * to giveback() this urb.  the races here should be
		 * small, but a full fix needs a new "can't submit"
		 * urb state.
		 * FIXME urb->reject should allow that...
		 */
		if (urb->status != -EINPROGRESS)
			continue;
		usb_get_urb (urb);
		spin_unlock (&hcd_data_lock);

		spin_lock (&urb->lock);
		tmp = urb->status;
		if (tmp == -EINPROGRESS)
			urb->status = -ESHUTDOWN;
		spin_unlock (&urb->lock);

		/* kick hcd unless it's already returning this */
		if (tmp == -EINPROGRESS) {
			tmp = urb->pipe;
			unlink1 (hcd, urb);
			dev_dbg (hcd->self.controller,
				"shutdown urb %p pipe %08x ep%d%s%s\n",
				urb, tmp, usb_pipeendpoint (tmp),
				(tmp & USB_DIR_IN) ? "in" : "out",
				({ char *s; \
				 switch (usb_pipetype (tmp)) { \
				 case PIPE_CONTROL:	s = ""; break; \
				 case PIPE_BULK:	s = "-bulk"; break; \
				 case PIPE_INTERRUPT:	s = "-intr"; break; \
				 default: 		s = "-iso"; break; \
				}; s;}));
		}
		usb_put_urb (urb);

		/* list contents may have changed */
		goto rescan;
	}
	spin_unlock (&hcd_data_lock);
	local_irq_enable ();

	/* synchronize with the hardware, so old configuration state
	 * clears out immediately (and will be freed).
	 */
	might_sleep ();
	if (hcd->driver->endpoint_disable)
		hcd->driver->endpoint_disable (hcd, ep);
}

/*-------------------------------------------------------------------------*/

#ifdef	CONFIG_PM

int hcd_bus_suspend (struct usb_bus *bus)
{
	struct usb_hcd		*hcd;
	int			status;

	hcd = container_of (bus, struct usb_hcd, self);
	if (!hcd->driver->bus_suspend)
		return -ENOENT;
	hcd->state = HC_STATE_QUIESCING;
	status = hcd->driver->bus_suspend (hcd);
	if (status == 0)
		hcd->state = HC_STATE_SUSPENDED;
	else
		dev_dbg(&bus->root_hub->dev, "%s fail, err %d\n",
				"suspend", status);
	return status;
}

int hcd_bus_resume (struct usb_bus *bus)
{
	struct usb_hcd		*hcd;
	int			status;

	hcd = container_of (bus, struct usb_hcd, self);
	if (!hcd->driver->bus_resume)
		return -ENOENT;
	if (hcd->state == HC_STATE_RUNNING)
		return 0;
	hcd->state = HC_STATE_RESUMING;
	status = hcd->driver->bus_resume (hcd);
	if (status == 0)
		hcd->state = HC_STATE_RUNNING;
	else {
		dev_dbg(&bus->root_hub->dev, "%s fail, err %d\n",
				"resume", status);
		usb_hc_died(hcd);
	}
	return status;
}

/*
 * usb_hcd_suspend_root_hub - HCD autosuspends downstream ports
 * @hcd: host controller for this root hub
 *
 * This call arranges that usb_hcd_resume_root_hub() is safe to call later;
 * that the HCD's root hub polling is deactivated; and that the root's hub
 * driver is suspended.  HCDs may call this to autosuspend when their root
 * hub's downstream ports are all inactive:  unpowered, disconnected,
 * disabled, or suspended.
 *
 * The HCD will autoresume on device connect change detection (using SRP
 * or a D+/D- pullup).  The HCD also autoresumes on remote wakeup signaling
 * from any ports that are suspended (if that is enabled).  In most cases,
 * overcurrent signaling (on powered ports) will also start autoresume.
 *
 * Always called with IRQs blocked.
 */
void usb_hcd_suspend_root_hub (struct usb_hcd *hcd)
{
	struct urb	*urb;

	spin_lock (&hcd_root_hub_lock);
	usb_suspend_root_hub (hcd->self.root_hub);

	/* force status urb to complete/unlink while suspended */
	if (hcd->status_urb) {
		urb = hcd->status_urb;
		urb->status = -ECONNRESET;
		urb->hcpriv = NULL;
		urb->actual_length = 0;

		del_timer (&hcd->rh_timer);
		hcd->poll_pending = 0;
		hcd->status_urb = NULL;
	} else
		urb = NULL;
	spin_unlock (&hcd_root_hub_lock);
	hcd->state = HC_STATE_SUSPENDED;

	if (urb)
		usb_hcd_giveback_urb (hcd, urb, NULL);
}
EXPORT_SYMBOL_GPL(usb_hcd_suspend_root_hub);

/**
 * usb_hcd_resume_root_hub - called by HCD to resume its root hub 
 * @hcd: host controller for this root hub
 *
 * The USB host controller calls this function when its root hub is
 * suspended (with the remote wakeup feature enabled) and a remote
 * wakeup request is received.  It queues a request for khubd to
 * resume the root hub (that is, manage its downstream ports again).
 */
void usb_hcd_resume_root_hub (struct usb_hcd *hcd)
{
	unsigned long flags;

	spin_lock_irqsave (&hcd_root_hub_lock, flags);
	if (hcd->rh_registered)
		usb_resume_root_hub (hcd->self.root_hub);
	spin_unlock_irqrestore (&hcd_root_hub_lock, flags);
}
EXPORT_SYMBOL_GPL(usb_hcd_resume_root_hub);

#endif

/*-------------------------------------------------------------------------*/

#ifdef	CONFIG_USB_OTG

/**
 * usb_bus_start_enum - start immediate enumeration (for OTG)
 * @bus: the bus (must use hcd framework)
 * @port_num: 1-based number of port; usually bus->otg_port
 * Context: in_interrupt()
 *
 * Starts enumeration, with an immediate reset followed later by
 * khubd identifying and possibly configuring the device.
 * This is needed by OTG controller drivers, where it helps meet
 * HNP protocol timing requirements for starting a port reset.
 */
int usb_bus_start_enum(struct usb_bus *bus, unsigned port_num)
{
	struct usb_hcd		*hcd;
	int			status = -EOPNOTSUPP;

	/* NOTE: since HNP can't start by grabbing the bus's address0_sem,
	 * boards with root hubs hooked up to internal devices (instead of
	 * just the OTG port) may need more attention to resetting...
	 */
	hcd = container_of (bus, struct usb_hcd, self);
	if (port_num && hcd->driver->start_port_reset)
		status = hcd->driver->start_port_reset(hcd, port_num);

	/* run khubd shortly after (first) root port reset finishes;
	 * it may issue others, until at least 50 msecs have passed.
	 */
	if (status == 0)
		mod_timer(&hcd->rh_timer, jiffies + msecs_to_jiffies(10));
	return status;
}
EXPORT_SYMBOL (usb_bus_start_enum);

#endif

/*-------------------------------------------------------------------------*/

/*
 * usb_hcd_operations - adapts usb_bus framework to HCD framework (bus glue)
 */
static struct usb_operations usb_hcd_operations = {
	.get_frame_number =	hcd_get_frame_number,
	.submit_urb =		hcd_submit_urb,
	.unlink_urb =		hcd_unlink_urb,
	.buffer_alloc =		hcd_buffer_alloc,
	.buffer_free =		hcd_buffer_free,
	.disable =		hcd_endpoint_disable,
};

/*-------------------------------------------------------------------------*/

/**
 * usb_hcd_giveback_urb - return URB from HCD to device driver
 * @hcd: host controller returning the URB
 * @urb: urb being returned to the USB device driver.
 * @regs: pt_regs, passed down to the URB completion handler
 * Context: in_interrupt()
 *
 * This hands the URB from HCD to its USB device driver, using its
 * completion function.  The HCD has freed all per-urb resources
 * (and is done using urb->hcpriv).  It also released all HCD locks;
 * the device driver won't cause problems if it frees, modifies,
 * or resubmits this URB.
 */
void usb_hcd_giveback_urb (struct usb_hcd *hcd, struct urb *urb, struct pt_regs *regs)
{
	int at_root_hub;

	at_root_hub = (urb->dev == hcd->self.root_hub);
	urb_unlink (urb);

	/* lower level hcd code should use *_dma exclusively */
	if (hcd->self.controller->dma_mask && !at_root_hub) {
		if (usb_pipecontrol (urb->pipe)
			&& !(urb->transfer_flags & URB_NO_SETUP_DMA_MAP))
			dma_unmap_single (hcd->self.controller, urb->setup_dma,
					sizeof (struct usb_ctrlrequest),
					DMA_TO_DEVICE);
		if (urb->transfer_buffer_length != 0
			&& !(urb->transfer_flags & URB_NO_TRANSFER_DMA_MAP))
			dma_unmap_single (hcd->self.controller, 
					urb->transfer_dma,
					urb->transfer_buffer_length,
					usb_pipein (urb->pipe)
					    ? DMA_FROM_DEVICE
					    : DMA_TO_DEVICE);
	}

	usbmon_urb_complete (&hcd->self, urb);
	/* pass ownership to the completion handler */
	urb->complete (urb, regs);
	atomic_dec (&urb->use_count);
	if (unlikely (urb->reject))
		wake_up (&usb_kill_urb_queue);
	usb_put_urb (urb);
}
EXPORT_SYMBOL (usb_hcd_giveback_urb);

/*-------------------------------------------------------------------------*/

/**
 * usb_hcd_irq - hook IRQs to HCD framework (bus glue)
 * @irq: the IRQ being raised
 * @__hcd: pointer to the HCD whose IRQ is being signaled
 * @r: saved hardware registers
 *
 * If the controller isn't HALTed, calls the driver's irq handler.
 * Checks whether the controller is now dead.
 */
irqreturn_t usb_hcd_irq (int irq, void *__hcd, struct pt_regs * r)
{
	struct usb_hcd		*hcd = __hcd;
	int			start = hcd->state;

	if (unlikely(start == HC_STATE_HALT ||
	    !test_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags)))
		return IRQ_NONE;
	if (hcd->driver->irq (hcd, r) == IRQ_NONE)
		return IRQ_NONE;

	set_bit(HCD_FLAG_SAW_IRQ, &hcd->flags);

	if (unlikely(hcd->state == HC_STATE_HALT))
		usb_hc_died (hcd);
	return IRQ_HANDLED;
}

/*-------------------------------------------------------------------------*/

/**
 * usb_hc_died - report abnormal shutdown of a host controller (bus glue)
 * @hcd: pointer to the HCD representing the controller
 *
 * This is called by bus glue to report a USB host controller that died
 * while operations may still have been pending.  It's called automatically
 * by the PCI glue, so only glue for non-PCI busses should need to call it. 
 */
void usb_hc_died (struct usb_hcd *hcd)
{
	unsigned long flags;

	dev_err (hcd->self.controller, "HC died; cleaning up\n");

	spin_lock_irqsave (&hcd_root_hub_lock, flags);
	if (hcd->rh_registered) {
		hcd->poll_rh = 0;

		/* make khubd clean up old urbs and devices */
		usb_set_device_state (hcd->self.root_hub,
				USB_STATE_NOTATTACHED);
		usb_kick_khubd (hcd->self.root_hub);
	}
	spin_unlock_irqrestore (&hcd_root_hub_lock, flags);
}
EXPORT_SYMBOL_GPL (usb_hc_died);

/*-------------------------------------------------------------------------*/

static void hcd_release (struct usb_bus *bus)
{
	struct usb_hcd *hcd;

	hcd = container_of(bus, struct usb_hcd, self);
	kfree(hcd);
}

/**
 * usb_create_hcd - create and initialize an HCD structure
 * @driver: HC driver that will use this hcd
 * @dev: device for this HC, stored in hcd->self.controller
 * @bus_name: value to store in hcd->self.bus_name
 * Context: !in_interrupt()
 *
 * Allocate a struct usb_hcd, with extra space at the end for the
 * HC driver's private data.  Initialize the generic members of the
 * hcd structure.
 *
 * If memory is unavailable, returns NULL.
 */
struct usb_hcd *usb_create_hcd (const struct hc_driver *driver,
		struct device *dev, char *bus_name)
{
	struct usb_hcd *hcd;

	hcd = kzalloc(sizeof(*hcd) + driver->hcd_priv_size, GFP_KERNEL);
	if (!hcd) {
		dev_dbg (dev, "hcd alloc failed\n");
		return NULL;
	}
	dev_set_drvdata(dev, hcd);

	usb_bus_init(&hcd->self);
	hcd->self.op = &usb_hcd_operations;
	hcd->self.hcpriv = hcd;
	hcd->self.release = &hcd_release;
	hcd->self.controller = dev;
	hcd->self.bus_name = bus_name;

	init_timer(&hcd->rh_timer);
	hcd->rh_timer.function = rh_timer_func;
	hcd->rh_timer.data = (unsigned long) hcd;

	hcd->driver = driver;
	hcd->product_desc = (driver->product_desc) ? driver->product_desc :
			"USB Host Controller";

	return hcd;
}
EXPORT_SYMBOL (usb_create_hcd);

void usb_put_hcd (struct usb_hcd *hcd)
{
	dev_set_drvdata(hcd->self.controller, NULL);
	usb_bus_put(&hcd->self);
}
EXPORT_SYMBOL (usb_put_hcd);

/**
 * usb_add_hcd - finish generic HCD structure initialization and register
 * @hcd: the usb_hcd structure to initialize
 * @irqnum: Interrupt line to allocate
 * @irqflags: Interrupt type flags
 *
 * Finish the remaining parts of generic HCD initialization: allocate the
 * buffers of consistent memory, register the bus, request the IRQ line,
 * and call the driver's reset() and start() routines.
 */
int usb_add_hcd(struct usb_hcd *hcd,
		unsigned int irqnum, unsigned long irqflags)
{
	int retval;
	struct usb_device *rhdev;

	dev_info(hcd->self.controller, "%s\n", hcd->product_desc);

	set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);

	/* HC is in reset state, but accessible.  Now do the one-time init,
	 * bottom up so that hcds can customize the root hubs before khubd
	 * starts talking to them.  (Note, bus id is assigned early too.)
	 */
	if ((retval = hcd_buffer_create(hcd)) != 0) {
		dev_dbg(hcd->self.controller, "pool alloc failed\n");
		return retval;
	}

	if ((retval = usb_register_bus(&hcd->self)) < 0)
		goto err_register_bus;

	if ((rhdev = usb_alloc_dev(NULL, &hcd->self, 0)) == NULL) {
		dev_err(hcd->self.controller, "unable to allocate root hub\n");
		retval = -ENOMEM;
		goto err_allocate_root_hub;
	}
	rhdev->speed = (hcd->driver->flags & HCD_USB2) ? USB_SPEED_HIGH :
			USB_SPEED_FULL;
	hcd->self.root_hub = rhdev;

	/* wakeup flag init defaults to "everything works" for root hubs,
	 * but drivers can override it in reset() if needed, along with
	 * recording the overall controller's system wakeup capability.
	 */
	device_init_wakeup(&rhdev->dev, 1);

	/* "reset" is misnamed; its role is now one-time init. the controller
	 * should already have been reset (and boot firmware kicked off etc).
	 */
	if (hcd->driver->reset && (retval = hcd->driver->reset(hcd)) < 0) {
		dev_err(hcd->self.controller, "can't setup\n");
		goto err_hcd_driver_setup;
	}

	/* NOTE: root hub and controller capabilities may not be the same */
	if (device_can_wakeup(hcd->self.controller)
			&& device_can_wakeup(&hcd->self.root_hub->dev))
		dev_dbg(hcd->self.controller, "supports USB remote wakeup\n");

	/* enable irqs just before we start the controller */
	if (hcd->driver->irq) {
		snprintf(hcd->irq_descr, sizeof(hcd->irq_descr), "%s:usb%d",
				hcd->driver->description, hcd->self.busnum);
		if ((retval = request_irq(irqnum, &usb_hcd_irq, irqflags,
				hcd->irq_descr, hcd)) != 0) {
			dev_err(hcd->self.controller,
					"request interrupt %d failed\n", irqnum);
			goto err_request_irq;
		}
		hcd->irq = irqnum;
		dev_info(hcd->self.controller, "irq %d, %s 0x%08llx\n", irqnum,
				(hcd->driver->flags & HCD_MEMORY) ?
					"io mem" : "io base",
					(unsigned long long)hcd->rsrc_start);
	} else {
		hcd->irq = -1;
		if (hcd->rsrc_start)
			dev_info(hcd->self.controller, "%s 0x%08llx\n",
					(hcd->driver->flags & HCD_MEMORY) ?
					"io mem" : "io base",
					(unsigned long long)hcd->rsrc_start);
	}

	if ((retval = hcd->driver->start(hcd)) < 0) {
		dev_err(hcd->self.controller, "startup error %d\n", retval);
		goto err_hcd_driver_start;
	}

	/* starting here, usbcore will pay attention to this root hub */
	rhdev->bus_mA = min(500u, hcd->power_budget);
	if ((retval = register_root_hub(hcd)) != 0)
		goto err_register_root_hub;

	if (hcd->uses_new_polling && hcd->poll_rh)
		usb_hcd_poll_rh_status(hcd);
	return retval;

err_register_root_hub:
	hcd->driver->stop(hcd);
err_hcd_driver_start:
	if (hcd->irq >= 0)
		free_irq(irqnum, hcd);
err_request_irq:
err_hcd_driver_setup:
	hcd->self.root_hub = NULL;
	usb_put_dev(rhdev);
err_allocate_root_hub:
	usb_deregister_bus(&hcd->self);
err_register_bus:
	hcd_buffer_destroy(hcd);
	return retval;
} 
EXPORT_SYMBOL (usb_add_hcd);

/**
 * usb_remove_hcd - shutdown processing for generic HCDs
 * @hcd: the usb_hcd structure to remove
 * Context: !in_interrupt()
 *
 * Disconnects the root hub, then reverses the effects of usb_add_hcd(),
 * invoking the HCD's stop() method.
 */
void usb_remove_hcd(struct usb_hcd *hcd)
{
	dev_info(hcd->self.controller, "remove, state %x\n", hcd->state);

	if (HC_IS_RUNNING (hcd->state))
		hcd->state = HC_STATE_QUIESCING;

	dev_dbg(hcd->self.controller, "roothub graceful disconnect\n");
	spin_lock_irq (&hcd_root_hub_lock);
	hcd->rh_registered = 0;
	spin_unlock_irq (&hcd_root_hub_lock);

	mutex_lock(&usb_bus_list_lock);
	usb_disconnect(&hcd->self.root_hub);
	mutex_unlock(&usb_bus_list_lock);

	hcd->poll_rh = 0;
	del_timer_sync(&hcd->rh_timer);

	hcd->driver->stop(hcd);
	hcd->state = HC_STATE_HALT;

	if (hcd->irq >= 0)
		free_irq(hcd->irq, hcd);
	usb_deregister_bus(&hcd->self);
	hcd_buffer_destroy(hcd);
}
EXPORT_SYMBOL (usb_remove_hcd);

/*-------------------------------------------------------------------------*/

#if defined(CONFIG_USB_MON)

struct usb_mon_operations *mon_ops;

/*
 * The registration is unlocked.
 * We do it this way because we do not want to lock in hot paths.
 *
 * Notice that the code is minimally error-proof. Because usbmon needs
 * symbols from usbcore, usbcore gets referenced and cannot be unloaded first.
 */
 
int usb_mon_register (struct usb_mon_operations *ops)
{

	if (mon_ops)
		return -EBUSY;

	mon_ops = ops;
	mb();
	return 0;
}
EXPORT_SYMBOL_GPL (usb_mon_register);

void usb_mon_deregister (void)
{

	if (mon_ops == NULL) {
		printk(KERN_ERR "USB: monitor was not registered\n");
		return;
	}
	mon_ops = NULL;
	mb();
}
EXPORT_SYMBOL_GPL (usb_mon_deregister);

#endif /* CONFIG_USB_MON */
