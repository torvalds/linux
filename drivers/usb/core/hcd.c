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

#include <linux/bcd.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/completion.h>
#include <linux/utsname.h>
#include <linux/mm.h>
#include <asm/io.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/mutex.h>
#include <asm/irq.h>
#include <asm/byteorder.h>
#include <asm/unaligned.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/pm_runtime.h>
#include <linux/types.h>

#include <linux/phy/phy.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/usb/phy.h>

#include "usb.h"


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

/* Keep track of which host controller drivers are loaded */
unsigned long usb_hcds_loaded;
EXPORT_SYMBOL_GPL(usb_hcds_loaded);

/* host controllers we manage */
LIST_HEAD (usb_bus_list);
EXPORT_SYMBOL_GPL (usb_bus_list);

/* used when allocating bus numbers */
#define USB_MAXBUS		64
static DECLARE_BITMAP(busmap, USB_MAXBUS);

/* used when updating list of hcds */
DEFINE_MUTEX(usb_bus_list_lock);	/* exported only for usbfs */
EXPORT_SYMBOL_GPL (usb_bus_list_lock);

/* used for controlling access to virtual root hubs */
static DEFINE_SPINLOCK(hcd_root_hub_lock);

/* used when updating an endpoint's URB list */
static DEFINE_SPINLOCK(hcd_urb_list_lock);

/* used to protect against unlinking URBs after the device is gone */
static DEFINE_SPINLOCK(hcd_urb_unlink_lock);

/* wait queue for synchronous unlinks */
DECLARE_WAIT_QUEUE_HEAD(usb_kill_urb_queue);

static inline int is_root_hub(struct usb_device *udev)
{
	return (udev->parent == NULL);
}

/*-------------------------------------------------------------------------*/

/*
 * Sharable chunks of root hub code.
 */

/*-------------------------------------------------------------------------*/
#define KERNEL_REL	bin2bcd(((LINUX_VERSION_CODE >> 16) & 0x0ff))
#define KERNEL_VER	bin2bcd(((LINUX_VERSION_CODE >> 8) & 0x0ff))

/* usb 3.0 root hub device descriptor */
static const u8 usb3_rh_dev_descriptor[18] = {
	0x12,       /*  __u8  bLength; */
	0x01,       /*  __u8  bDescriptorType; Device */
	0x00, 0x03, /*  __le16 bcdUSB; v3.0 */

	0x09,	    /*  __u8  bDeviceClass; HUB_CLASSCODE */
	0x00,	    /*  __u8  bDeviceSubClass; */
	0x03,       /*  __u8  bDeviceProtocol; USB 3.0 hub */
	0x09,       /*  __u8  bMaxPacketSize0; 2^9 = 512 Bytes */

	0x6b, 0x1d, /*  __le16 idVendor; Linux Foundation 0x1d6b */
	0x03, 0x00, /*  __le16 idProduct; device 0x0003 */
	KERNEL_VER, KERNEL_REL, /*  __le16 bcdDevice */

	0x03,       /*  __u8  iManufacturer; */
	0x02,       /*  __u8  iProduct; */
	0x01,       /*  __u8  iSerialNumber; */
	0x01        /*  __u8  bNumConfigurations; */
};

/* usb 2.5 (wireless USB 1.0) root hub device descriptor */
static const u8 usb25_rh_dev_descriptor[18] = {
	0x12,       /*  __u8  bLength; */
	0x01,       /*  __u8  bDescriptorType; Device */
	0x50, 0x02, /*  __le16 bcdUSB; v2.5 */

	0x09,	    /*  __u8  bDeviceClass; HUB_CLASSCODE */
	0x00,	    /*  __u8  bDeviceSubClass; */
	0x00,       /*  __u8  bDeviceProtocol; [ usb 2.0 no TT ] */
	0xFF,       /*  __u8  bMaxPacketSize0; always 0xFF (WUSB Spec 7.4.1). */

	0x6b, 0x1d, /*  __le16 idVendor; Linux Foundation 0x1d6b */
	0x02, 0x00, /*  __le16 idProduct; device 0x0002 */
	KERNEL_VER, KERNEL_REL, /*  __le16 bcdDevice */

	0x03,       /*  __u8  iManufacturer; */
	0x02,       /*  __u8  iProduct; */
	0x01,       /*  __u8  iSerialNumber; */
	0x01        /*  __u8  bNumConfigurations; */
};

/* usb 2.0 root hub device descriptor */
static const u8 usb2_rh_dev_descriptor[18] = {
	0x12,       /*  __u8  bLength; */
	0x01,       /*  __u8  bDescriptorType; Device */
	0x00, 0x02, /*  __le16 bcdUSB; v2.0 */

	0x09,	    /*  __u8  bDeviceClass; HUB_CLASSCODE */
	0x00,	    /*  __u8  bDeviceSubClass; */
	0x00,       /*  __u8  bDeviceProtocol; [ usb 2.0 no TT ] */
	0x40,       /*  __u8  bMaxPacketSize0; 64 Bytes */

	0x6b, 0x1d, /*  __le16 idVendor; Linux Foundation 0x1d6b */
	0x02, 0x00, /*  __le16 idProduct; device 0x0002 */
	KERNEL_VER, KERNEL_REL, /*  __le16 bcdDevice */

	0x03,       /*  __u8  iManufacturer; */
	0x02,       /*  __u8  iProduct; */
	0x01,       /*  __u8  iSerialNumber; */
	0x01        /*  __u8  bNumConfigurations; */
};

/* no usb 2.0 root hub "device qualifier" descriptor: one speed only */

/* usb 1.1 root hub device descriptor */
static const u8 usb11_rh_dev_descriptor[18] = {
	0x12,       /*  __u8  bLength; */
	0x01,       /*  __u8  bDescriptorType; Device */
	0x10, 0x01, /*  __le16 bcdUSB; v1.1 */

	0x09,	    /*  __u8  bDeviceClass; HUB_CLASSCODE */
	0x00,	    /*  __u8  bDeviceSubClass; */
	0x00,       /*  __u8  bDeviceProtocol; [ low/full speeds only ] */
	0x40,       /*  __u8  bMaxPacketSize0; 64 Bytes */

	0x6b, 0x1d, /*  __le16 idVendor; Linux Foundation 0x1d6b */
	0x01, 0x00, /*  __le16 idProduct; device 0x0001 */
	KERNEL_VER, KERNEL_REL, /*  __le16 bcdDevice */

	0x03,       /*  __u8  iManufacturer; */
	0x02,       /*  __u8  iProduct; */
	0x01,       /*  __u8  iSerialNumber; */
	0x01        /*  __u8  bNumConfigurations; */
};


/*-------------------------------------------------------------------------*/

/* Configuration descriptors for our root hubs */

static const u8 fs_rh_config_descriptor[] = {

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

static const u8 hs_rh_config_descriptor[] = {

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
		    /* __le16 ep_wMaxPacketSize; 1 + (MAX_ROOT_PORTS / 8)
		     * see hub.c:hub_configure() for details. */
	(USB_MAXCHILDREN + 1 + 7) / 8, 0x00,
	0x0c        /*  __u8  ep_bInterval; (256ms -- usb 2.0 spec) */
};

static const u8 ss_rh_config_descriptor[] = {
	/* one configuration */
	0x09,       /*  __u8  bLength; */
	0x02,       /*  __u8  bDescriptorType; Configuration */
	0x1f, 0x00, /*  __le16 wTotalLength; */
	0x01,       /*  __u8  bNumInterfaces; (1) */
	0x01,       /*  __u8  bConfigurationValue; */
	0x00,       /*  __u8  iConfiguration; */
	0xc0,       /*  __u8  bmAttributes;
				 Bit 7: must be set,
				     6: Self-powered,
				     5: Remote wakeup,
				     4..0: resvd */
	0x00,       /*  __u8  MaxPower; */

	/* one interface */
	0x09,       /*  __u8  if_bLength; */
	0x04,       /*  __u8  if_bDescriptorType; Interface */
	0x00,       /*  __u8  if_bInterfaceNumber; */
	0x00,       /*  __u8  if_bAlternateSetting; */
	0x01,       /*  __u8  if_bNumEndpoints; */
	0x09,       /*  __u8  if_bInterfaceClass; HUB_CLASSCODE */
	0x00,       /*  __u8  if_bInterfaceSubClass; */
	0x00,       /*  __u8  if_bInterfaceProtocol; */
	0x00,       /*  __u8  if_iInterface; */

	/* one endpoint (status change endpoint) */
	0x07,       /*  __u8  ep_bLength; */
	0x05,       /*  __u8  ep_bDescriptorType; Endpoint */
	0x81,       /*  __u8  ep_bEndpointAddress; IN Endpoint 1 */
	0x03,       /*  __u8  ep_bmAttributes; Interrupt */
		    /* __le16 ep_wMaxPacketSize; 1 + (MAX_ROOT_PORTS / 8)
		     * see hub.c:hub_configure() for details. */
	(USB_MAXCHILDREN + 1 + 7) / 8, 0x00,
	0x0c,       /*  __u8  ep_bInterval; (256ms -- usb 2.0 spec) */

	/* one SuperSpeed endpoint companion descriptor */
	0x06,        /* __u8 ss_bLength */
	0x30,        /* __u8 ss_bDescriptorType; SuperSpeed EP Companion */
	0x00,        /* __u8 ss_bMaxBurst; allows 1 TX between ACKs */
	0x00,        /* __u8 ss_bmAttributes; 1 packet per service interval */
	0x02, 0x00   /* __le16 ss_wBytesPerInterval; 15 bits for max 15 ports */
};

/* authorized_default behaviour:
 * -1 is authorized for all devices except wireless (old behaviour)
 * 0 is unauthorized for all devices
 * 1 is authorized for all devices
 */
static int authorized_default = -1;
module_param(authorized_default, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(authorized_default,
		"Default USB device authorization: 0 is not authorized, 1 is "
		"authorized, -1 is authorized except for wireless USB (default, "
		"old behaviour");
/*-------------------------------------------------------------------------*/

/**
 * ascii2desc() - Helper routine for producing UTF-16LE string descriptors
 * @s: Null-terminated ASCII (actually ISO-8859-1) string
 * @buf: Buffer for USB string descriptor (header + UTF-16LE)
 * @len: Length (in bytes; may be odd) of descriptor buffer.
 *
 * Return: The number of bytes filled in: 2 + 2*strlen(s) or @len,
 * whichever is less.
 *
 * Note:
 * USB String descriptors can contain at most 126 characters; input
 * strings longer than that are truncated.
 */
static unsigned
ascii2desc(char const *s, u8 *buf, unsigned len)
{
	unsigned n, t = 2 + 2*strlen(s);

	if (t > 254)
		t = 254;	/* Longest possible UTF string descriptor */
	if (len > t)
		len = t;

	t += USB_DT_STRING << 8;	/* Now t is first 16 bits to store */

	n = len;
	while (n--) {
		*buf++ = t;
		if (!n--)
			break;
		*buf++ = t >> 8;
		t = (unsigned char)*s++;
	}
	return len;
}

/**
 * rh_string() - provides string descriptors for root hub
 * @id: the string ID number (0: langids, 1: serial #, 2: product, 3: vendor)
 * @hcd: the host controller for this root hub
 * @data: buffer for output packet
 * @len: length of the provided buffer
 *
 * Produces either a manufacturer, product or serial number string for the
 * virtual root hub device.
 *
 * Return: The number of bytes filled in: the length of the descriptor or
 * of the provided buffer, whichever is less.
 */
static unsigned
rh_string(int id, struct usb_hcd const *hcd, u8 *data, unsigned len)
{
	char buf[100];
	char const *s;
	static char const langids[4] = {4, USB_DT_STRING, 0x09, 0x04};

	/* language ids */
	switch (id) {
	case 0:
		/* Array of LANGID codes (0x0409 is MSFT-speak for "en-us") */
		/* See http://www.usb.org/developers/docs/USB_LANGIDs.pdf */
		if (len > 4)
			len = 4;
		memcpy(data, langids, len);
		return len;
	case 1:
		/* Serial number */
		s = hcd->self.bus_name;
		break;
	case 2:
		/* Product name */
		s = hcd->product_desc;
		break;
	case 3:
		/* Manufacturer */
		snprintf (buf, sizeof buf, "%s %s %s", init_utsname()->sysname,
			init_utsname()->release, hcd->driver->description);
		s = buf;
		break;
	default:
		/* Can't happen; caller guarantees it */
		return 0;
	}

	return ascii2desc(s, data, len);
}


/* Root hub control transfers execute synchronously */
static int rh_call_control (struct usb_hcd *hcd, struct urb *urb)
{
	struct usb_ctrlrequest *cmd;
	u16		typeReq, wValue, wIndex, wLength;
	u8		*ubuf = urb->transfer_buffer;
	unsigned	len = 0;
	int		status;
	u8		patch_wakeup = 0;
	u8		patch_protocol = 0;
	u16		tbuf_size;
	u8		*tbuf = NULL;
	const u8	*bufp;

	might_sleep();

	spin_lock_irq(&hcd_root_hub_lock);
	status = usb_hcd_link_urb_to_ep(hcd, urb);
	spin_unlock_irq(&hcd_root_hub_lock);
	if (status)
		return status;
	urb->hcpriv = hcd;	/* Indicate it's queued */

	cmd = (struct usb_ctrlrequest *) urb->setup_packet;
	typeReq  = (cmd->bRequestType << 8) | cmd->bRequest;
	wValue   = le16_to_cpu (cmd->wValue);
	wIndex   = le16_to_cpu (cmd->wIndex);
	wLength  = le16_to_cpu (cmd->wLength);

	if (wLength > urb->transfer_buffer_length)
		goto error;

	/*
	 * tbuf should be at least as big as the
	 * USB hub descriptor.
	 */
	tbuf_size =  max_t(u16, sizeof(struct usb_hub_descriptor), wLength);
	tbuf = kzalloc(tbuf_size, GFP_KERNEL);
	if (!tbuf)
		return -ENOMEM;

	bufp = tbuf;


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
		tbuf[0] = (device_may_wakeup(&hcd->self.root_hub->dev)
					<< USB_DEVICE_REMOTE_WAKEUP)
				| (1 << USB_DEVICE_SELF_POWERED);
		tbuf[1] = 0;
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
		tbuf[0] = 1;
		len = 1;
			/* FALLTHROUGH */
	case DeviceOutRequest | USB_REQ_SET_CONFIGURATION:
		break;
	case DeviceRequest | USB_REQ_GET_DESCRIPTOR:
		switch (wValue & 0xff00) {
		case USB_DT_DEVICE << 8:
			switch (hcd->speed) {
			case HCD_USB3:
				bufp = usb3_rh_dev_descriptor;
				break;
			case HCD_USB25:
				bufp = usb25_rh_dev_descriptor;
				break;
			case HCD_USB2:
				bufp = usb2_rh_dev_descriptor;
				break;
			case HCD_USB11:
				bufp = usb11_rh_dev_descriptor;
				break;
			default:
				goto error;
			}
			len = 18;
			if (hcd->has_tt)
				patch_protocol = 1;
			break;
		case USB_DT_CONFIG << 8:
			switch (hcd->speed) {
			case HCD_USB3:
				bufp = ss_rh_config_descriptor;
				len = sizeof ss_rh_config_descriptor;
				break;
			case HCD_USB25:
			case HCD_USB2:
				bufp = hs_rh_config_descriptor;
				len = sizeof hs_rh_config_descriptor;
				break;
			case HCD_USB11:
				bufp = fs_rh_config_descriptor;
				len = sizeof fs_rh_config_descriptor;
				break;
			default:
				goto error;
			}
			if (device_can_wakeup(&hcd->self.root_hub->dev))
				patch_wakeup = 1;
			break;
		case USB_DT_STRING << 8:
			if ((wValue & 0xff) < 4)
				urb->actual_length = rh_string(wValue & 0xff,
						hcd, ubuf, wLength);
			else /* unsupported IDs --> "protocol stall" */
				goto error;
			break;
		case USB_DT_BOS << 8:
			goto nongeneric;
		default:
			goto error;
		}
		break;
	case DeviceRequest | USB_REQ_GET_INTERFACE:
		tbuf[0] = 0;
		len = 1;
			/* FALLTHROUGH */
	case DeviceOutRequest | USB_REQ_SET_INTERFACE:
		break;
	case DeviceOutRequest | USB_REQ_SET_ADDRESS:
		/* wValue == urb->dev->devaddr */
		dev_dbg (hcd->self.controller, "root hub device address %d\n",
			wValue);
		break;

	/* INTERFACE REQUESTS (no defined feature/status flags) */

	/* ENDPOINT REQUESTS */

	case EndpointRequest | USB_REQ_GET_STATUS:
		/* ENDPOINT_HALT flag */
		tbuf[0] = 0;
		tbuf[1] = 0;
		len = 2;
			/* FALLTHROUGH */
	case EndpointOutRequest | USB_REQ_CLEAR_FEATURE:
	case EndpointOutRequest | USB_REQ_SET_FEATURE:
		dev_dbg (hcd->self.controller, "no endpoint features yet\n");
		break;

	/* CLASS REQUESTS (and errors) */

	default:
nongeneric:
		/* non-generic request */
		switch (typeReq) {
		case GetHubStatus:
		case GetPortStatus:
			len = 4;
			break;
		case GetHubDescriptor:
			len = sizeof (struct usb_hub_descriptor);
			break;
		case DeviceRequest | USB_REQ_GET_DESCRIPTOR:
			/* len is returned by hub_control */
			break;
		}
		status = hcd->driver->hub_control (hcd,
			typeReq, wValue, wIndex,
			tbuf, wLength);

		if (typeReq == GetHubDescriptor)
			usb_hub_adjust_deviceremovable(hcd->self.root_hub,
				(struct usb_hub_descriptor *)tbuf);
		break;
error:
		/* "protocol stall" on error */
		status = -EPIPE;
	}

	if (status < 0) {
		len = 0;
		if (status != -EPIPE) {
			dev_dbg (hcd->self.controller,
				"CTRL: TypeReq=0x%x val=0x%x "
				"idx=0x%x len=%d ==> %d\n",
				typeReq, wValue, wIndex,
				wLength, status);
		}
	} else if (status > 0) {
		/* hub_control may return the length of data copied. */
		len = status;
		status = 0;
	}
	if (len) {
		if (urb->transfer_buffer_length < len)
			len = urb->transfer_buffer_length;
		urb->actual_length = len;
		/* always USB_DIR_IN, toward host */
		memcpy (ubuf, bufp, len);

		/* report whether RH hardware supports remote wakeup */
		if (patch_wakeup &&
				len > offsetof (struct usb_config_descriptor,
						bmAttributes))
			((struct usb_config_descriptor *)ubuf)->bmAttributes
				|= USB_CONFIG_ATT_WAKEUP;

		/* report whether RH hardware has an integrated TT */
		if (patch_protocol &&
				len > offsetof(struct usb_device_descriptor,
						bDeviceProtocol))
			((struct usb_device_descriptor *) ubuf)->
				bDeviceProtocol = USB_HUB_PR_HS_SINGLE_TT;
	}

	kfree(tbuf);

	/* any errors get returned through the urb completion */
	spin_lock_irq(&hcd_root_hub_lock);
	usb_hcd_unlink_urb_from_ep(hcd, urb);
	usb_hcd_giveback_urb(hcd, urb, status);
	spin_unlock_irq(&hcd_root_hub_lock);
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
	char		buffer[6];	/* Any root hubs with > 31 ports? */

	if (unlikely(!hcd->rh_pollable))
		return;
	if (!hcd->uses_new_polling && !hcd->status_urb)
		return;

	length = hcd->driver->hub_status_data(hcd, buffer);
	if (length > 0) {

		/* try to complete the status urb */
		spin_lock_irqsave(&hcd_root_hub_lock, flags);
		urb = hcd->status_urb;
		if (urb) {
			clear_bit(HCD_FLAG_POLL_PENDING, &hcd->flags);
			hcd->status_urb = NULL;
			urb->actual_length = length;
			memcpy(urb->transfer_buffer, buffer, length);

			usb_hcd_unlink_urb_from_ep(hcd, urb);
			usb_hcd_giveback_urb(hcd, urb, 0);
		} else {
			length = 0;
			set_bit(HCD_FLAG_POLL_PENDING, &hcd->flags);
		}
		spin_unlock_irqrestore(&hcd_root_hub_lock, flags);
	}

	/* The USB 2.0 spec says 256 ms.  This is close enough and won't
	 * exceed that limit if HZ is 100. The math is more clunky than
	 * maybe expected, this is to make sure that all timers for USB devices
	 * fire at the same time to give the CPU a break in between */
	if (hcd->uses_new_polling ? HCD_POLL_RH(hcd) :
			(length == 0 && hcd->status_urb != NULL))
		mod_timer (&hcd->rh_timer, (jiffies/(HZ/4) + 1) * (HZ/4));
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
	unsigned	len = 1 + (urb->dev->maxchild / 8);

	spin_lock_irqsave (&hcd_root_hub_lock, flags);
	if (hcd->status_urb || urb->transfer_buffer_length < len) {
		dev_dbg (hcd->self.controller, "not queuing rh status urb\n");
		retval = -EINVAL;
		goto done;
	}

	retval = usb_hcd_link_urb_to_ep(hcd, urb);
	if (retval)
		goto done;

	hcd->status_urb = urb;
	urb->hcpriv = hcd;	/* indicate it's queued */
	if (!hcd->uses_new_polling)
		mod_timer(&hcd->rh_timer, (jiffies/(HZ/4) + 1) * (HZ/4));

	/* If a status change has already occurred, report it ASAP */
	else if (HCD_POLL_PENDING(hcd))
		mod_timer(&hcd->rh_timer, jiffies);
	retval = 0;
 done:
	spin_unlock_irqrestore (&hcd_root_hub_lock, flags);
	return retval;
}

static int rh_urb_enqueue (struct usb_hcd *hcd, struct urb *urb)
{
	if (usb_endpoint_xfer_int(&urb->ep->desc))
		return rh_queue_status (hcd, urb);
	if (usb_endpoint_xfer_control(&urb->ep->desc))
		return rh_call_control (hcd, urb);
	return -EINVAL;
}

/*-------------------------------------------------------------------------*/

/* Unlinks of root-hub control URBs are legal, but they don't do anything
 * since these URBs always execute synchronously.
 */
static int usb_rh_urb_dequeue(struct usb_hcd *hcd, struct urb *urb, int status)
{
	unsigned long	flags;
	int		rc;

	spin_lock_irqsave(&hcd_root_hub_lock, flags);
	rc = usb_hcd_check_unlink_urb(hcd, urb, status);
	if (rc)
		goto done;

	if (usb_endpoint_num(&urb->ep->desc) == 0) {	/* Control URB */
		;	/* Do nothing */

	} else {				/* Status URB */
		if (!hcd->uses_new_polling)
			del_timer (&hcd->rh_timer);
		if (urb == hcd->status_urb) {
			hcd->status_urb = NULL;
			usb_hcd_unlink_urb_from_ep(hcd, urb);
			usb_hcd_giveback_urb(hcd, urb, status);
		}
	}
 done:
	spin_unlock_irqrestore(&hcd_root_hub_lock, flags);
	return rc;
}



/*
 * Show & store the current value of authorized_default
 */
static ssize_t authorized_default_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct usb_device *rh_usb_dev = to_usb_device(dev);
	struct usb_bus *usb_bus = rh_usb_dev->bus;
	struct usb_hcd *usb_hcd;

	usb_hcd = bus_to_hcd(usb_bus);
	return snprintf(buf, PAGE_SIZE, "%u\n", usb_hcd->authorized_default);
}

static ssize_t authorized_default_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
	ssize_t result;
	unsigned val;
	struct usb_device *rh_usb_dev = to_usb_device(dev);
	struct usb_bus *usb_bus = rh_usb_dev->bus;
	struct usb_hcd *usb_hcd;

	usb_hcd = bus_to_hcd(usb_bus);
	result = sscanf(buf, "%u\n", &val);
	if (result == 1) {
		usb_hcd->authorized_default = val ? 1 : 0;
		result = size;
	} else {
		result = -EINVAL;
	}
	return result;
}
static DEVICE_ATTR_RW(authorized_default);

/* Group all the USB bus attributes */
static struct attribute *usb_bus_attrs[] = {
		&dev_attr_authorized_default.attr,
		NULL,
};

static struct attribute_group usb_bus_attr_group = {
	.name = NULL,	/* we want them in the same directory */
	.attrs = usb_bus_attrs,
};



/*-------------------------------------------------------------------------*/

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
	bus->busnum = -1;
	bus->bandwidth_allocated = 0;
	bus->bandwidth_int_reqs  = 0;
	bus->bandwidth_isoc_reqs = 0;
	mutex_init(&bus->usb_address0_mutex);

	INIT_LIST_HEAD (&bus->bus_list);
}

/*-------------------------------------------------------------------------*/

/**
 * usb_register_bus - registers the USB host controller with the usb core
 * @bus: pointer to the bus to register
 * Context: !in_interrupt()
 *
 * Assigns a bus number, and links the controller into usbcore data
 * structures so that it can be seen by scanning the bus list.
 *
 * Return: 0 if successful. A negative error code otherwise.
 */
static int usb_register_bus(struct usb_bus *bus)
{
	int result = -E2BIG;
	int busnum;

	mutex_lock(&usb_bus_list_lock);
	busnum = find_next_zero_bit(busmap, USB_MAXBUS, 1);
	if (busnum >= USB_MAXBUS) {
		printk (KERN_ERR "%s: too many buses\n", usbcore_name);
		goto error_find_busnum;
	}
	set_bit(busnum, busmap);
	bus->busnum = busnum;

	/* Add it to the local list of buses */
	list_add (&bus->bus_list, &usb_bus_list);
	mutex_unlock(&usb_bus_list_lock);

	usb_notify_add_bus(bus);

	dev_info (bus->controller, "new USB bus registered, assigned bus "
		  "number %d\n", bus->busnum);
	return 0;

error_find_busnum:
	mutex_unlock(&usb_bus_list_lock);
	return result;
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

	clear_bit(bus->busnum, busmap);
}

/**
 * register_root_hub - called by usb_add_hcd() to register a root hub
 * @hcd: host controller for this root hub
 *
 * This function registers the root hub with the USB subsystem.  It sets up
 * the device properly in the device tree and then calls usb_new_device()
 * to register the usb device.  It also assigns the root hub's USB address
 * (always 1).
 *
 * Return: 0 if successful. A negative error code otherwise.
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

	usb_dev->ep0.desc.wMaxPacketSize = cpu_to_le16(64);
	retval = usb_get_device_descriptor(usb_dev, USB_DT_DEVICE_SIZE);
	if (retval != sizeof usb_dev->descriptor) {
		mutex_unlock(&usb_bus_list_lock);
		dev_dbg (parent_dev, "can't read %s device descriptor %d\n",
				dev_name(&usb_dev->dev), retval);
		return (retval < 0) ? retval : -EMSGSIZE;
	}
	if (usb_dev->speed == USB_SPEED_SUPER) {
		retval = usb_get_bos_descriptor(usb_dev);
		if (retval < 0) {
			mutex_unlock(&usb_bus_list_lock);
			dev_dbg(parent_dev, "can't read %s bos descriptor %d\n",
					dev_name(&usb_dev->dev), retval);
			return retval;
		}
	}

	retval = usb_new_device (usb_dev);
	if (retval) {
		dev_err (parent_dev, "can't register root hub for %s, %d\n",
				dev_name(&usb_dev->dev), retval);
	} else {
		spin_lock_irq (&hcd_root_hub_lock);
		hcd->rh_registered = 1;
		spin_unlock_irq (&hcd_root_hub_lock);

		/* Did the HC die before the root hub was registered? */
		if (HCD_DEAD(hcd))
			usb_hc_died (hcd);	/* This time clean up */
	}
	mutex_unlock(&usb_bus_list_lock);

	return retval;
}

/*
 * usb_hcd_start_port_resume - a root-hub port is sending a resume signal
 * @bus: the bus which the root hub belongs to
 * @portnum: the port which is being resumed
 *
 * HCDs should call this function when they know that a resume signal is
 * being sent to a root-hub port.  The root hub will be prevented from
 * going into autosuspend until usb_hcd_end_port_resume() is called.
 *
 * The bus's private lock must be held by the caller.
 */
void usb_hcd_start_port_resume(struct usb_bus *bus, int portnum)
{
	unsigned bit = 1 << portnum;

	if (!(bus->resuming_ports & bit)) {
		bus->resuming_ports |= bit;
		pm_runtime_get_noresume(&bus->root_hub->dev);
	}
}
EXPORT_SYMBOL_GPL(usb_hcd_start_port_resume);

/*
 * usb_hcd_end_port_resume - a root-hub port has stopped sending a resume signal
 * @bus: the bus which the root hub belongs to
 * @portnum: the port which is being resumed
 *
 * HCDs should call this function when they know that a resume signal has
 * stopped being sent to a root-hub port.  The root hub will be allowed to
 * autosuspend again.
 *
 * The bus's private lock must be held by the caller.
 */
void usb_hcd_end_port_resume(struct usb_bus *bus, int portnum)
{
	unsigned bit = 1 << portnum;

	if (bus->resuming_ports & bit) {
		bus->resuming_ports &= ~bit;
		pm_runtime_put_noidle(&bus->root_hub->dev);
	}
}
EXPORT_SYMBOL_GPL(usb_hcd_end_port_resume);

/*-------------------------------------------------------------------------*/

/**
 * usb_calc_bus_time - approximate periodic transaction time in nanoseconds
 * @speed: from dev->speed; USB_SPEED_{LOW,FULL,HIGH}
 * @is_input: true iff the transaction sends data to the host
 * @isoc: true for isochronous transactions, false for interrupt ones
 * @bytecount: how many bytes in the transaction.
 *
 * Return: Approximate bus time in nanoseconds for a periodic transaction.
 *
 * Note:
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
			return 64060L + (2 * BW_HUB_LS_SETUP) + BW_HOST_DELAY + tmp;
		} else {
			tmp = (66700L * (31L + 10L * BitTime (bytecount))) / 1000L;
			return 64107L + (2 * BW_HUB_LS_SETUP) + BW_HOST_DELAY + tmp;
		}
	case USB_SPEED_FULL:	/* ISOC or INTR */
		if (isoc) {
			tmp = (8354L * (31L + 10L * BitTime (bytecount))) / 1000L;
			return ((is_input) ? 7268L : 6265L) + BW_HOST_DELAY + tmp;
		} else {
			tmp = (8354L * (31L + 10L * BitTime (bytecount))) / 1000L;
			return 9107L + BW_HOST_DELAY + tmp;
		}
	case USB_SPEED_HIGH:	/* ISOC or INTR */
		/* FIXME adjust for input vs output */
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
EXPORT_SYMBOL_GPL(usb_calc_bus_time);


/*-------------------------------------------------------------------------*/

/*
 * Generic HC operations.
 */

/*-------------------------------------------------------------------------*/

/**
 * usb_hcd_link_urb_to_ep - add an URB to its endpoint queue
 * @hcd: host controller to which @urb was submitted
 * @urb: URB being submitted
 *
 * Host controller drivers should call this routine in their enqueue()
 * method.  The HCD's private spinlock must be held and interrupts must
 * be disabled.  The actions carried out here are required for URB
 * submission, as well as for endpoint shutdown and for usb_kill_urb.
 *
 * Return: 0 for no error, otherwise a negative error code (in which case
 * the enqueue() method must fail).  If no error occurs but enqueue() fails
 * anyway, it must call usb_hcd_unlink_urb_from_ep() before releasing
 * the private spinlock and returning.
 */
int usb_hcd_link_urb_to_ep(struct usb_hcd *hcd, struct urb *urb)
{
	int		rc = 0;

	spin_lock(&hcd_urb_list_lock);

	/* Check that the URB isn't being killed */
	if (unlikely(atomic_read(&urb->reject))) {
		rc = -EPERM;
		goto done;
	}

	if (unlikely(!urb->ep->enabled)) {
		rc = -ENOENT;
		goto done;
	}

	if (unlikely(!urb->dev->can_submit)) {
		rc = -EHOSTUNREACH;
		goto done;
	}

	/*
	 * Check the host controller's state and add the URB to the
	 * endpoint's queue.
	 */
	if (HCD_RH_RUNNING(hcd)) {
		urb->unlinked = 0;
		list_add_tail(&urb->urb_list, &urb->ep->urb_list);
	} else {
		rc = -ESHUTDOWN;
		goto done;
	}
 done:
	spin_unlock(&hcd_urb_list_lock);
	return rc;
}
EXPORT_SYMBOL_GPL(usb_hcd_link_urb_to_ep);

/**
 * usb_hcd_check_unlink_urb - check whether an URB may be unlinked
 * @hcd: host controller to which @urb was submitted
 * @urb: URB being checked for unlinkability
 * @status: error code to store in @urb if the unlink succeeds
 *
 * Host controller drivers should call this routine in their dequeue()
 * method.  The HCD's private spinlock must be held and interrupts must
 * be disabled.  The actions carried out here are required for making
 * sure than an unlink is valid.
 *
 * Return: 0 for no error, otherwise a negative error code (in which case
 * the dequeue() method must fail).  The possible error codes are:
 *
 *	-EIDRM: @urb was not submitted or has already completed.
 *		The completion function may not have been called yet.
 *
 *	-EBUSY: @urb has already been unlinked.
 */
int usb_hcd_check_unlink_urb(struct usb_hcd *hcd, struct urb *urb,
		int status)
{
	struct list_head	*tmp;

	/* insist the urb is still queued */
	list_for_each(tmp, &urb->ep->urb_list) {
		if (tmp == &urb->urb_list)
			break;
	}
	if (tmp != &urb->urb_list)
		return -EIDRM;

	/* Any status except -EINPROGRESS means something already started to
	 * unlink this URB from the hardware.  So there's no more work to do.
	 */
	if (urb->unlinked)
		return -EBUSY;
	urb->unlinked = status;
	return 0;
}
EXPORT_SYMBOL_GPL(usb_hcd_check_unlink_urb);

/**
 * usb_hcd_unlink_urb_from_ep - remove an URB from its endpoint queue
 * @hcd: host controller to which @urb was submitted
 * @urb: URB being unlinked
 *
 * Host controller drivers should call this routine before calling
 * usb_hcd_giveback_urb().  The HCD's private spinlock must be held and
 * interrupts must be disabled.  The actions carried out here are required
 * for URB completion.
 */
void usb_hcd_unlink_urb_from_ep(struct usb_hcd *hcd, struct urb *urb)
{
	/* clear all state linking urb to this dev (and hcd) */
	spin_lock(&hcd_urb_list_lock);
	list_del_init(&urb->urb_list);
	spin_unlock(&hcd_urb_list_lock);
}
EXPORT_SYMBOL_GPL(usb_hcd_unlink_urb_from_ep);

/*
 * Some usb host controllers can only perform dma using a small SRAM area.
 * The usb core itself is however optimized for host controllers that can dma
 * using regular system memory - like pci devices doing bus mastering.
 *
 * To support host controllers with limited dma capabilities we provide dma
 * bounce buffers. This feature can be enabled using the HCD_LOCAL_MEM flag.
 * For this to work properly the host controller code must first use the
 * function dma_declare_coherent_memory() to point out which memory area
 * that should be used for dma allocations.
 *
 * The HCD_LOCAL_MEM flag then tells the usb code to allocate all data for
 * dma using dma_alloc_coherent() which in turn allocates from the memory
 * area pointed out with dma_declare_coherent_memory().
 *
 * So, to summarize...
 *
 * - We need "local" memory, canonical example being
 *   a small SRAM on a discrete controller being the
 *   only memory that the controller can read ...
 *   (a) "normal" kernel memory is no good, and
 *   (b) there's not enough to share
 *
 * - The only *portable* hook for such stuff in the
 *   DMA framework is dma_declare_coherent_memory()
 *
 * - So we use that, even though the primary requirement
 *   is that the memory be "local" (hence addressable
 *   by that device), not "coherent".
 *
 */

static int hcd_alloc_coherent(struct usb_bus *bus,
			      gfp_t mem_flags, dma_addr_t *dma_handle,
			      void **vaddr_handle, size_t size,
			      enum dma_data_direction dir)
{
	unsigned char *vaddr;

	if (*vaddr_handle == NULL) {
		WARN_ON_ONCE(1);
		return -EFAULT;
	}

	vaddr = hcd_buffer_alloc(bus, size + sizeof(vaddr),
				 mem_flags, dma_handle);
	if (!vaddr)
		return -ENOMEM;

	/*
	 * Store the virtual address of the buffer at the end
	 * of the allocated dma buffer. The size of the buffer
	 * may be uneven so use unaligned functions instead
	 * of just rounding up. It makes sense to optimize for
	 * memory footprint over access speed since the amount
	 * of memory available for dma may be limited.
	 */
	put_unaligned((unsigned long)*vaddr_handle,
		      (unsigned long *)(vaddr + size));

	if (dir == DMA_TO_DEVICE)
		memcpy(vaddr, *vaddr_handle, size);

	*vaddr_handle = vaddr;
	return 0;
}

static void hcd_free_coherent(struct usb_bus *bus, dma_addr_t *dma_handle,
			      void **vaddr_handle, size_t size,
			      enum dma_data_direction dir)
{
	unsigned char *vaddr = *vaddr_handle;

	vaddr = (void *)get_unaligned((unsigned long *)(vaddr + size));

	if (dir == DMA_FROM_DEVICE)
		memcpy(vaddr, *vaddr_handle, size);

	hcd_buffer_free(bus, size + sizeof(vaddr), *vaddr_handle, *dma_handle);

	*vaddr_handle = vaddr;
	*dma_handle = 0;
}

void usb_hcd_unmap_urb_setup_for_dma(struct usb_hcd *hcd, struct urb *urb)
{
	if (urb->transfer_flags & URB_SETUP_MAP_SINGLE)
		dma_unmap_single(hcd->self.controller,
				urb->setup_dma,
				sizeof(struct usb_ctrlrequest),
				DMA_TO_DEVICE);
	else if (urb->transfer_flags & URB_SETUP_MAP_LOCAL)
		hcd_free_coherent(urb->dev->bus,
				&urb->setup_dma,
				(void **) &urb->setup_packet,
				sizeof(struct usb_ctrlrequest),
				DMA_TO_DEVICE);

	/* Make it safe to call this routine more than once */
	urb->transfer_flags &= ~(URB_SETUP_MAP_SINGLE | URB_SETUP_MAP_LOCAL);
}
EXPORT_SYMBOL_GPL(usb_hcd_unmap_urb_setup_for_dma);

static void unmap_urb_for_dma(struct usb_hcd *hcd, struct urb *urb)
{
	if (hcd->driver->unmap_urb_for_dma)
		hcd->driver->unmap_urb_for_dma(hcd, urb);
	else
		usb_hcd_unmap_urb_for_dma(hcd, urb);
}

void usb_hcd_unmap_urb_for_dma(struct usb_hcd *hcd, struct urb *urb)
{
	enum dma_data_direction dir;

	usb_hcd_unmap_urb_setup_for_dma(hcd, urb);

	dir = usb_urb_dir_in(urb) ? DMA_FROM_DEVICE : DMA_TO_DEVICE;
	if (urb->transfer_flags & URB_DMA_MAP_SG)
		dma_unmap_sg(hcd->self.controller,
				urb->sg,
				urb->num_sgs,
				dir);
	else if (urb->transfer_flags & URB_DMA_MAP_PAGE)
		dma_unmap_page(hcd->self.controller,
				urb->transfer_dma,
				urb->transfer_buffer_length,
				dir);
	else if (urb->transfer_flags & URB_DMA_MAP_SINGLE)
		dma_unmap_single(hcd->self.controller,
				urb->transfer_dma,
				urb->transfer_buffer_length,
				dir);
	else if (urb->transfer_flags & URB_MAP_LOCAL)
		hcd_free_coherent(urb->dev->bus,
				&urb->transfer_dma,
				&urb->transfer_buffer,
				urb->transfer_buffer_length,
				dir);

	/* Make it safe to call this routine more than once */
	urb->transfer_flags &= ~(URB_DMA_MAP_SG | URB_DMA_MAP_PAGE |
			URB_DMA_MAP_SINGLE | URB_MAP_LOCAL);
}
EXPORT_SYMBOL_GPL(usb_hcd_unmap_urb_for_dma);

static int map_urb_for_dma(struct usb_hcd *hcd, struct urb *urb,
			   gfp_t mem_flags)
{
	if (hcd->driver->map_urb_for_dma)
		return hcd->driver->map_urb_for_dma(hcd, urb, mem_flags);
	else
		return usb_hcd_map_urb_for_dma(hcd, urb, mem_flags);
}

int usb_hcd_map_urb_for_dma(struct usb_hcd *hcd, struct urb *urb,
			    gfp_t mem_flags)
{
	enum dma_data_direction dir;
	int ret = 0;

	/* Map the URB's buffers for DMA access.
	 * Lower level HCD code should use *_dma exclusively,
	 * unless it uses pio or talks to another transport,
	 * or uses the provided scatter gather list for bulk.
	 */

	if (usb_endpoint_xfer_control(&urb->ep->desc)) {
		if (hcd->self.uses_pio_for_control)
			return ret;
		if (hcd->self.uses_dma) {
			urb->setup_dma = dma_map_single(
					hcd->self.controller,
					urb->setup_packet,
					sizeof(struct usb_ctrlrequest),
					DMA_TO_DEVICE);
			if (dma_mapping_error(hcd->self.controller,
						urb->setup_dma))
				return -EAGAIN;
			urb->transfer_flags |= URB_SETUP_MAP_SINGLE;
		} else if (hcd->driver->flags & HCD_LOCAL_MEM) {
			ret = hcd_alloc_coherent(
					urb->dev->bus, mem_flags,
					&urb->setup_dma,
					(void **)&urb->setup_packet,
					sizeof(struct usb_ctrlrequest),
					DMA_TO_DEVICE);
			if (ret)
				return ret;
			urb->transfer_flags |= URB_SETUP_MAP_LOCAL;
		}
	}

	dir = usb_urb_dir_in(urb) ? DMA_FROM_DEVICE : DMA_TO_DEVICE;
	if (urb->transfer_buffer_length != 0
	    && !(urb->transfer_flags & URB_NO_TRANSFER_DMA_MAP)) {
		if (hcd->self.uses_dma) {
			if (urb->num_sgs) {
				int n;

				/* We don't support sg for isoc transfers ! */
				if (usb_endpoint_xfer_isoc(&urb->ep->desc)) {
					WARN_ON(1);
					return -EINVAL;
				}

				n = dma_map_sg(
						hcd->self.controller,
						urb->sg,
						urb->num_sgs,
						dir);
				if (n <= 0)
					ret = -EAGAIN;
				else
					urb->transfer_flags |= URB_DMA_MAP_SG;
				urb->num_mapped_sgs = n;
				if (n != urb->num_sgs)
					urb->transfer_flags |=
							URB_DMA_SG_COMBINED;
			} else if (urb->sg) {
				struct scatterlist *sg = urb->sg;
				urb->transfer_dma = dma_map_page(
						hcd->self.controller,
						sg_page(sg),
						sg->offset,
						urb->transfer_buffer_length,
						dir);
				if (dma_mapping_error(hcd->self.controller,
						urb->transfer_dma))
					ret = -EAGAIN;
				else
					urb->transfer_flags |= URB_DMA_MAP_PAGE;
			} else if (is_vmalloc_addr(urb->transfer_buffer)) {
				WARN_ONCE(1, "transfer buffer not dma capable\n");
				ret = -EAGAIN;
			} else {
				urb->transfer_dma = dma_map_single(
						hcd->self.controller,
						urb->transfer_buffer,
						urb->transfer_buffer_length,
						dir);
				if (dma_mapping_error(hcd->self.controller,
						urb->transfer_dma))
					ret = -EAGAIN;
				else
					urb->transfer_flags |= URB_DMA_MAP_SINGLE;
			}
		} else if (hcd->driver->flags & HCD_LOCAL_MEM) {
			ret = hcd_alloc_coherent(
					urb->dev->bus, mem_flags,
					&urb->transfer_dma,
					&urb->transfer_buffer,
					urb->transfer_buffer_length,
					dir);
			if (ret == 0)
				urb->transfer_flags |= URB_MAP_LOCAL;
		}
		if (ret && (urb->transfer_flags & (URB_SETUP_MAP_SINGLE |
				URB_SETUP_MAP_LOCAL)))
			usb_hcd_unmap_urb_for_dma(hcd, urb);
	}
	return ret;
}
EXPORT_SYMBOL_GPL(usb_hcd_map_urb_for_dma);

/*-------------------------------------------------------------------------*/

/* may be called in any context with a valid urb->dev usecount
 * caller surrenders "ownership" of urb
 * expects usb_submit_urb() to have sanity checked and conditioned all
 * inputs in the urb
 */
int usb_hcd_submit_urb (struct urb *urb, gfp_t mem_flags)
{
	int			status;
	struct usb_hcd		*hcd = bus_to_hcd(urb->dev->bus);

	/* increment urb's reference count as part of giving it to the HCD
	 * (which will control it).  HCD guarantees that it either returns
	 * an error or calls giveback(), but not both.
	 */
	usb_get_urb(urb);
	atomic_inc(&urb->use_count);
	atomic_inc(&urb->dev->urbnum);
	usbmon_urb_submit(&hcd->self, urb);

	/* NOTE requirements on root-hub callers (usbfs and the hub
	 * driver, for now):  URBs' urb->transfer_buffer must be
	 * valid and usb_buffer_{sync,unmap}() not be needed, since
	 * they could clobber root hub response data.  Also, control
	 * URBs must be submitted in process context with interrupts
	 * enabled.
	 */

	if (is_root_hub(urb->dev)) {
		status = rh_urb_enqueue(hcd, urb);
	} else {
		status = map_urb_for_dma(hcd, urb, mem_flags);
		if (likely(status == 0)) {
			status = hcd->driver->urb_enqueue(hcd, urb, mem_flags);
			if (unlikely(status))
				unmap_urb_for_dma(hcd, urb);
		}
	}

	if (unlikely(status)) {
		usbmon_urb_submit_error(&hcd->self, urb, status);
		urb->hcpriv = NULL;
		INIT_LIST_HEAD(&urb->urb_list);
		atomic_dec(&urb->use_count);
		atomic_dec(&urb->dev->urbnum);
		if (atomic_read(&urb->reject))
			wake_up(&usb_kill_urb_queue);
		usb_put_urb(urb);
	}
	return status;
}

/*-------------------------------------------------------------------------*/

/* this makes the hcd giveback() the urb more quickly, by kicking it
 * off hardware queues (which may take a while) and returning it as
 * soon as practical.  we've already set up the urb's return status,
 * but we can't know if the callback completed already.
 */
static int unlink1(struct usb_hcd *hcd, struct urb *urb, int status)
{
	int		value;

	if (is_root_hub(urb->dev))
		value = usb_rh_urb_dequeue(hcd, urb, status);
	else {

		/* The only reason an HCD might fail this call is if
		 * it has not yet fully queued the urb to begin with.
		 * Such failures should be harmless. */
		value = hcd->driver->urb_dequeue(hcd, urb, status);
	}
	return value;
}

/*
 * called in any context
 *
 * caller guarantees urb won't be recycled till both unlink()
 * and the urb's completion function return
 */
int usb_hcd_unlink_urb (struct urb *urb, int status)
{
	struct usb_hcd		*hcd;
	struct usb_device	*udev = urb->dev;
	int			retval = -EIDRM;
	unsigned long		flags;

	/* Prevent the device and bus from going away while
	 * the unlink is carried out.  If they are already gone
	 * then urb->use_count must be 0, since disconnected
	 * devices can't have any active URBs.
	 */
	spin_lock_irqsave(&hcd_urb_unlink_lock, flags);
	if (atomic_read(&urb->use_count) > 0) {
		retval = 0;
		usb_get_dev(udev);
	}
	spin_unlock_irqrestore(&hcd_urb_unlink_lock, flags);
	if (retval == 0) {
		hcd = bus_to_hcd(urb->dev->bus);
		retval = unlink1(hcd, urb, status);
		if (retval == 0)
			retval = -EINPROGRESS;
		else if (retval != -EIDRM && retval != -EBUSY)
			dev_dbg(&udev->dev, "hcd_unlink_urb %p fail %d\n",
					urb, retval);
		usb_put_dev(udev);
	}
	return retval;
}

/*-------------------------------------------------------------------------*/

static void __usb_hcd_giveback_urb(struct urb *urb)
{
	struct usb_hcd *hcd = bus_to_hcd(urb->dev->bus);
	struct usb_anchor *anchor = urb->anchor;
	int status = urb->unlinked;
	unsigned long flags;

	urb->hcpriv = NULL;
	if (unlikely((urb->transfer_flags & URB_SHORT_NOT_OK) &&
	    urb->actual_length < urb->transfer_buffer_length &&
	    !status))
		status = -EREMOTEIO;

	unmap_urb_for_dma(hcd, urb);
	usbmon_urb_complete(&hcd->self, urb, status);
	usb_anchor_suspend_wakeups(anchor);
	usb_unanchor_urb(urb);
	if (likely(status == 0))
		usb_led_activity(USB_LED_EVENT_HOST);

	/* pass ownership to the completion handler */
	urb->status = status;

	/*
	 * We disable local IRQs here avoid possible deadlock because
	 * drivers may call spin_lock() to hold lock which might be
	 * acquired in one hard interrupt handler.
	 *
	 * The local_irq_save()/local_irq_restore() around complete()
	 * will be removed if current USB drivers have been cleaned up
	 * and no one may trigger the above deadlock situation when
	 * running complete() in tasklet.
	 */
	local_irq_save(flags);
	urb->complete(urb);
	local_irq_restore(flags);

	usb_anchor_resume_wakeups(anchor);
	atomic_dec(&urb->use_count);
	if (unlikely(atomic_read(&urb->reject)))
		wake_up(&usb_kill_urb_queue);
	usb_put_urb(urb);
}

static void usb_giveback_urb_bh(unsigned long param)
{
	struct giveback_urb_bh *bh = (struct giveback_urb_bh *)param;
	struct list_head local_list;

	spin_lock_irq(&bh->lock);
	bh->running = true;
 restart:
	list_replace_init(&bh->head, &local_list);
	spin_unlock_irq(&bh->lock);

	while (!list_empty(&local_list)) {
		struct urb *urb;

		urb = list_entry(local_list.next, struct urb, urb_list);
		list_del_init(&urb->urb_list);
		bh->completing_ep = urb->ep;
		__usb_hcd_giveback_urb(urb);
		bh->completing_ep = NULL;
	}

	/* check if there are new URBs to giveback */
	spin_lock_irq(&bh->lock);
	if (!list_empty(&bh->head))
		goto restart;
	bh->running = false;
	spin_unlock_irq(&bh->lock);
}

/**
 * usb_hcd_giveback_urb - return URB from HCD to device driver
 * @hcd: host controller returning the URB
 * @urb: urb being returned to the USB device driver.
 * @status: completion status code for the URB.
 * Context: in_interrupt()
 *
 * This hands the URB from HCD to its USB device driver, using its
 * completion function.  The HCD has freed all per-urb resources
 * (and is done using urb->hcpriv).  It also released all HCD locks;
 * the device driver won't cause problems if it frees, modifies,
 * or resubmits this URB.
 *
 * If @urb was unlinked, the value of @status will be overridden by
 * @urb->unlinked.  Erroneous short transfers are detected in case
 * the HCD hasn't checked for them.
 */
void usb_hcd_giveback_urb(struct usb_hcd *hcd, struct urb *urb, int status)
{
	struct giveback_urb_bh *bh;
	bool running, high_prio_bh;

	/* pass status to tasklet via unlinked */
	if (likely(!urb->unlinked))
		urb->unlinked = status;

	if (!hcd_giveback_urb_in_bh(hcd) && !is_root_hub(urb->dev)) {
		__usb_hcd_giveback_urb(urb);
		return;
	}

	if (usb_pipeisoc(urb->pipe) || usb_pipeint(urb->pipe)) {
		bh = &hcd->high_prio_bh;
		high_prio_bh = true;
	} else {
		bh = &hcd->low_prio_bh;
		high_prio_bh = false;
	}

	spin_lock(&bh->lock);
	list_add_tail(&urb->urb_list, &bh->head);
	running = bh->running;
	spin_unlock(&bh->lock);

	if (running)
		;
	else if (high_prio_bh)
		tasklet_hi_schedule(&bh->bh);
	else
		tasklet_schedule(&bh->bh);
}
EXPORT_SYMBOL_GPL(usb_hcd_giveback_urb);

/*-------------------------------------------------------------------------*/

/* Cancel all URBs pending on this endpoint and wait for the endpoint's
 * queue to drain completely.  The caller must first insure that no more
 * URBs can be submitted for this endpoint.
 */
void usb_hcd_flush_endpoint(struct usb_device *udev,
		struct usb_host_endpoint *ep)
{
	struct usb_hcd		*hcd;
	struct urb		*urb;

	if (!ep)
		return;
	might_sleep();
	hcd = bus_to_hcd(udev->bus);

	/* No more submits can occur */
	spin_lock_irq(&hcd_urb_list_lock);
rescan:
	list_for_each_entry (urb, &ep->urb_list, urb_list) {
		int	is_in;

		if (urb->unlinked)
			continue;
		usb_get_urb (urb);
		is_in = usb_urb_dir_in(urb);
		spin_unlock(&hcd_urb_list_lock);

		/* kick hcd */
		unlink1(hcd, urb, -ESHUTDOWN);
		dev_dbg (hcd->self.controller,
			"shutdown urb %p ep%d%s%s\n",
			urb, usb_endpoint_num(&ep->desc),
			is_in ? "in" : "out",
			({	char *s;

				 switch (usb_endpoint_type(&ep->desc)) {
				 case USB_ENDPOINT_XFER_CONTROL:
					s = ""; break;
				 case USB_ENDPOINT_XFER_BULK:
					s = "-bulk"; break;
				 case USB_ENDPOINT_XFER_INT:
					s = "-intr"; break;
				 default:
					s = "-iso"; break;
				};
				s;
			}));
		usb_put_urb (urb);

		/* list contents may have changed */
		spin_lock(&hcd_urb_list_lock);
		goto rescan;
	}
	spin_unlock_irq(&hcd_urb_list_lock);

	/* Wait until the endpoint queue is completely empty */
	while (!list_empty (&ep->urb_list)) {
		spin_lock_irq(&hcd_urb_list_lock);

		/* The list may have changed while we acquired the spinlock */
		urb = NULL;
		if (!list_empty (&ep->urb_list)) {
			urb = list_entry (ep->urb_list.prev, struct urb,
					urb_list);
			usb_get_urb (urb);
		}
		spin_unlock_irq(&hcd_urb_list_lock);

		if (urb) {
			usb_kill_urb (urb);
			usb_put_urb (urb);
		}
	}
}

/**
 * usb_hcd_alloc_bandwidth - check whether a new bandwidth setting exceeds
 *				the bus bandwidth
 * @udev: target &usb_device
 * @new_config: new configuration to install
 * @cur_alt: the current alternate interface setting
 * @new_alt: alternate interface setting that is being installed
 *
 * To change configurations, pass in the new configuration in new_config,
 * and pass NULL for cur_alt and new_alt.
 *
 * To reset a device's configuration (put the device in the ADDRESSED state),
 * pass in NULL for new_config, cur_alt, and new_alt.
 *
 * To change alternate interface settings, pass in NULL for new_config,
 * pass in the current alternate interface setting in cur_alt,
 * and pass in the new alternate interface setting in new_alt.
 *
 * Return: An error if the requested bandwidth change exceeds the
 * bus bandwidth or host controller internal resources.
 */
int usb_hcd_alloc_bandwidth(struct usb_device *udev,
		struct usb_host_config *new_config,
		struct usb_host_interface *cur_alt,
		struct usb_host_interface *new_alt)
{
	int num_intfs, i, j;
	struct usb_host_interface *alt = NULL;
	int ret = 0;
	struct usb_hcd *hcd;
	struct usb_host_endpoint *ep;

	hcd = bus_to_hcd(udev->bus);
	if (!hcd->driver->check_bandwidth)
		return 0;

	/* Configuration is being removed - set configuration 0 */
	if (!new_config && !cur_alt) {
		for (i = 1; i < 16; ++i) {
			ep = udev->ep_out[i];
			if (ep)
				hcd->driver->drop_endpoint(hcd, udev, ep);
			ep = udev->ep_in[i];
			if (ep)
				hcd->driver->drop_endpoint(hcd, udev, ep);
		}
		hcd->driver->check_bandwidth(hcd, udev);
		return 0;
	}
	/* Check if the HCD says there's enough bandwidth.  Enable all endpoints
	 * each interface's alt setting 0 and ask the HCD to check the bandwidth
	 * of the bus.  There will always be bandwidth for endpoint 0, so it's
	 * ok to exclude it.
	 */
	if (new_config) {
		num_intfs = new_config->desc.bNumInterfaces;
		/* Remove endpoints (except endpoint 0, which is always on the
		 * schedule) from the old config from the schedule
		 */
		for (i = 1; i < 16; ++i) {
			ep = udev->ep_out[i];
			if (ep) {
				ret = hcd->driver->drop_endpoint(hcd, udev, ep);
				if (ret < 0)
					goto reset;
			}
			ep = udev->ep_in[i];
			if (ep) {
				ret = hcd->driver->drop_endpoint(hcd, udev, ep);
				if (ret < 0)
					goto reset;
			}
		}
		for (i = 0; i < num_intfs; ++i) {
			struct usb_host_interface *first_alt;
			int iface_num;

			first_alt = &new_config->intf_cache[i]->altsetting[0];
			iface_num = first_alt->desc.bInterfaceNumber;
			/* Set up endpoints for alternate interface setting 0 */
			alt = usb_find_alt_setting(new_config, iface_num, 0);
			if (!alt)
				/* No alt setting 0? Pick the first setting. */
				alt = first_alt;

			for (j = 0; j < alt->desc.bNumEndpoints; j++) {
				ret = hcd->driver->add_endpoint(hcd, udev, &alt->endpoint[j]);
				if (ret < 0)
					goto reset;
			}
		}
	}
	if (cur_alt && new_alt) {
		struct usb_interface *iface = usb_ifnum_to_if(udev,
				cur_alt->desc.bInterfaceNumber);

		if (!iface)
			return -EINVAL;
		if (iface->resetting_device) {
			/*
			 * The USB core just reset the device, so the xHCI host
			 * and the device will think alt setting 0 is installed.
			 * However, the USB core will pass in the alternate
			 * setting installed before the reset as cur_alt.  Dig
			 * out the alternate setting 0 structure, or the first
			 * alternate setting if a broken device doesn't have alt
			 * setting 0.
			 */
			cur_alt = usb_altnum_to_altsetting(iface, 0);
			if (!cur_alt)
				cur_alt = &iface->altsetting[0];
		}

		/* Drop all the endpoints in the current alt setting */
		for (i = 0; i < cur_alt->desc.bNumEndpoints; i++) {
			ret = hcd->driver->drop_endpoint(hcd, udev,
					&cur_alt->endpoint[i]);
			if (ret < 0)
				goto reset;
		}
		/* Add all the endpoints in the new alt setting */
		for (i = 0; i < new_alt->desc.bNumEndpoints; i++) {
			ret = hcd->driver->add_endpoint(hcd, udev,
					&new_alt->endpoint[i]);
			if (ret < 0)
				goto reset;
		}
	}
	ret = hcd->driver->check_bandwidth(hcd, udev);
reset:
	if (ret < 0)
		hcd->driver->reset_bandwidth(hcd, udev);
	return ret;
}

/* Disables the endpoint: synchronizes with the hcd to make sure all
 * endpoint state is gone from hardware.  usb_hcd_flush_endpoint() must
 * have been called previously.  Use for set_configuration, set_interface,
 * driver removal, physical disconnect.
 *
 * example:  a qh stored in ep->hcpriv, holding state related to endpoint
 * type, maxpacket size, toggle, halt status, and scheduling.
 */
void usb_hcd_disable_endpoint(struct usb_device *udev,
		struct usb_host_endpoint *ep)
{
	struct usb_hcd		*hcd;

	might_sleep();
	hcd = bus_to_hcd(udev->bus);
	if (hcd->driver->endpoint_disable)
		hcd->driver->endpoint_disable(hcd, ep);
}

/**
 * usb_hcd_reset_endpoint - reset host endpoint state
 * @udev: USB device.
 * @ep:   the endpoint to reset.
 *
 * Resets any host endpoint state such as the toggle bit, sequence
 * number and current window.
 */
void usb_hcd_reset_endpoint(struct usb_device *udev,
			    struct usb_host_endpoint *ep)
{
	struct usb_hcd *hcd = bus_to_hcd(udev->bus);

	if (hcd->driver->endpoint_reset)
		hcd->driver->endpoint_reset(hcd, ep);
	else {
		int epnum = usb_endpoint_num(&ep->desc);
		int is_out = usb_endpoint_dir_out(&ep->desc);
		int is_control = usb_endpoint_xfer_control(&ep->desc);

		usb_settoggle(udev, epnum, is_out, 0);
		if (is_control)
			usb_settoggle(udev, epnum, !is_out, 0);
	}
}

/**
 * usb_alloc_streams - allocate bulk endpoint stream IDs.
 * @interface:		alternate setting that includes all endpoints.
 * @eps:		array of endpoints that need streams.
 * @num_eps:		number of endpoints in the array.
 * @num_streams:	number of streams to allocate.
 * @mem_flags:		flags hcd should use to allocate memory.
 *
 * Sets up a group of bulk endpoints to have @num_streams stream IDs available.
 * Drivers may queue multiple transfers to different stream IDs, which may
 * complete in a different order than they were queued.
 *
 * Return: On success, the number of allocated streams. On failure, a negative
 * error code.
 */
int usb_alloc_streams(struct usb_interface *interface,
		struct usb_host_endpoint **eps, unsigned int num_eps,
		unsigned int num_streams, gfp_t mem_flags)
{
	struct usb_hcd *hcd;
	struct usb_device *dev;
	int i, ret;

	dev = interface_to_usbdev(interface);
	hcd = bus_to_hcd(dev->bus);
	if (!hcd->driver->alloc_streams || !hcd->driver->free_streams)
		return -EINVAL;
	if (dev->speed != USB_SPEED_SUPER)
		return -EINVAL;
	if (dev->state < USB_STATE_CONFIGURED)
		return -ENODEV;

	for (i = 0; i < num_eps; i++) {
		/* Streams only apply to bulk endpoints. */
		if (!usb_endpoint_xfer_bulk(&eps[i]->desc))
			return -EINVAL;
		/* Re-alloc is not allowed */
		if (eps[i]->streams)
			return -EINVAL;
	}

	ret = hcd->driver->alloc_streams(hcd, dev, eps, num_eps,
			num_streams, mem_flags);
	if (ret < 0)
		return ret;

	for (i = 0; i < num_eps; i++)
		eps[i]->streams = ret;

	return ret;
}
EXPORT_SYMBOL_GPL(usb_alloc_streams);

/**
 * usb_free_streams - free bulk endpoint stream IDs.
 * @interface:	alternate setting that includes all endpoints.
 * @eps:	array of endpoints to remove streams from.
 * @num_eps:	number of endpoints in the array.
 * @mem_flags:	flags hcd should use to allocate memory.
 *
 * Reverts a group of bulk endpoints back to not using stream IDs.
 * Can fail if we are given bad arguments, or HCD is broken.
 *
 * Return: 0 on success. On failure, a negative error code.
 */
int usb_free_streams(struct usb_interface *interface,
		struct usb_host_endpoint **eps, unsigned int num_eps,
		gfp_t mem_flags)
{
	struct usb_hcd *hcd;
	struct usb_device *dev;
	int i, ret;

	dev = interface_to_usbdev(interface);
	hcd = bus_to_hcd(dev->bus);
	if (dev->speed != USB_SPEED_SUPER)
		return -EINVAL;

	/* Double-free is not allowed */
	for (i = 0; i < num_eps; i++)
		if (!eps[i] || !eps[i]->streams)
			return -EINVAL;

	ret = hcd->driver->free_streams(hcd, dev, eps, num_eps, mem_flags);
	if (ret < 0)
		return ret;

	for (i = 0; i < num_eps; i++)
		eps[i]->streams = 0;

	return ret;
}
EXPORT_SYMBOL_GPL(usb_free_streams);

/* Protect against drivers that try to unlink URBs after the device
 * is gone, by waiting until all unlinks for @udev are finished.
 * Since we don't currently track URBs by device, simply wait until
 * nothing is running in the locked region of usb_hcd_unlink_urb().
 */
void usb_hcd_synchronize_unlinks(struct usb_device *udev)
{
	spin_lock_irq(&hcd_urb_unlink_lock);
	spin_unlock_irq(&hcd_urb_unlink_lock);
}

/*-------------------------------------------------------------------------*/

/* called in any context */
int usb_hcd_get_frame_number (struct usb_device *udev)
{
	struct usb_hcd	*hcd = bus_to_hcd(udev->bus);

	if (!HCD_RH_RUNNING(hcd))
		return -ESHUTDOWN;
	return hcd->driver->get_frame_number (hcd);
}

/*-------------------------------------------------------------------------*/

#ifdef	CONFIG_PM

int hcd_bus_suspend(struct usb_device *rhdev, pm_message_t msg)
{
	struct usb_hcd	*hcd = container_of(rhdev->bus, struct usb_hcd, self);
	int		status;
	int		old_state = hcd->state;

	dev_dbg(&rhdev->dev, "bus %ssuspend, wakeup %d\n",
			(PMSG_IS_AUTO(msg) ? "auto-" : ""),
			rhdev->do_remote_wakeup);
	if (HCD_DEAD(hcd)) {
		dev_dbg(&rhdev->dev, "skipped %s of dead bus\n", "suspend");
		return 0;
	}

	if (!hcd->driver->bus_suspend) {
		status = -ENOENT;
	} else {
		clear_bit(HCD_FLAG_RH_RUNNING, &hcd->flags);
		hcd->state = HC_STATE_QUIESCING;
		status = hcd->driver->bus_suspend(hcd);
	}
	if (status == 0) {
		usb_set_device_state(rhdev, USB_STATE_SUSPENDED);
		hcd->state = HC_STATE_SUSPENDED;

		/* Did we race with a root-hub wakeup event? */
		if (rhdev->do_remote_wakeup) {
			char	buffer[6];

			status = hcd->driver->hub_status_data(hcd, buffer);
			if (status != 0) {
				dev_dbg(&rhdev->dev, "suspend raced with wakeup event\n");
				hcd_bus_resume(rhdev, PMSG_AUTO_RESUME);
				status = -EBUSY;
			}
		}
	} else {
		spin_lock_irq(&hcd_root_hub_lock);
		if (!HCD_DEAD(hcd)) {
			set_bit(HCD_FLAG_RH_RUNNING, &hcd->flags);
			hcd->state = old_state;
		}
		spin_unlock_irq(&hcd_root_hub_lock);
		dev_dbg(&rhdev->dev, "bus %s fail, err %d\n",
				"suspend", status);
	}
	return status;
}

int hcd_bus_resume(struct usb_device *rhdev, pm_message_t msg)
{
	struct usb_hcd	*hcd = container_of(rhdev->bus, struct usb_hcd, self);
	int		status;
	int		old_state = hcd->state;

	dev_dbg(&rhdev->dev, "usb %sresume\n",
			(PMSG_IS_AUTO(msg) ? "auto-" : ""));
	if (HCD_DEAD(hcd)) {
		dev_dbg(&rhdev->dev, "skipped %s of dead bus\n", "resume");
		return 0;
	}
	if (!hcd->driver->bus_resume)
		return -ENOENT;
	if (HCD_RH_RUNNING(hcd))
		return 0;

	hcd->state = HC_STATE_RESUMING;
	status = hcd->driver->bus_resume(hcd);
	clear_bit(HCD_FLAG_WAKEUP_PENDING, &hcd->flags);
	if (status == 0) {
		struct usb_device *udev;
		int port1;

		spin_lock_irq(&hcd_root_hub_lock);
		if (!HCD_DEAD(hcd)) {
			usb_set_device_state(rhdev, rhdev->actconfig
					? USB_STATE_CONFIGURED
					: USB_STATE_ADDRESS);
			set_bit(HCD_FLAG_RH_RUNNING, &hcd->flags);
			hcd->state = HC_STATE_RUNNING;
		}
		spin_unlock_irq(&hcd_root_hub_lock);

		/*
		 * Check whether any of the enabled ports on the root hub are
		 * unsuspended.  If they are then a TRSMRCY delay is needed
		 * (this is what the USB-2 spec calls a "global resume").
		 * Otherwise we can skip the delay.
		 */
		usb_hub_for_each_child(rhdev, port1, udev) {
			if (udev->state != USB_STATE_NOTATTACHED &&
					!udev->port_is_suspended) {
				usleep_range(10000, 11000);	/* TRSMRCY */
				break;
			}
		}
	} else {
		hcd->state = old_state;
		dev_dbg(&rhdev->dev, "bus %s fail, err %d\n",
				"resume", status);
		if (status != -ESHUTDOWN)
			usb_hc_died(hcd);
	}
	return status;
}

/* Workqueue routine for root-hub remote wakeup */
static void hcd_resume_work(struct work_struct *work)
{
	struct usb_hcd *hcd = container_of(work, struct usb_hcd, wakeup_work);
	struct usb_device *udev = hcd->self.root_hub;

	usb_remote_wakeup(udev);
}

/**
 * usb_hcd_resume_root_hub - called by HCD to resume its root hub
 * @hcd: host controller for this root hub
 *
 * The USB host controller calls this function when its root hub is
 * suspended (with the remote wakeup feature enabled) and a remote
 * wakeup request is received.  The routine submits a workqueue request
 * to resume the root hub (that is, manage its downstream ports again).
 */
void usb_hcd_resume_root_hub (struct usb_hcd *hcd)
{
	unsigned long flags;

	spin_lock_irqsave (&hcd_root_hub_lock, flags);
	if (hcd->rh_registered) {
		set_bit(HCD_FLAG_WAKEUP_PENDING, &hcd->flags);
		queue_work(pm_wq, &hcd->wakeup_work);
	}
	spin_unlock_irqrestore (&hcd_root_hub_lock, flags);
}
EXPORT_SYMBOL_GPL(usb_hcd_resume_root_hub);

#endif	/* CONFIG_PM */

/*-------------------------------------------------------------------------*/

#ifdef	CONFIG_USB_OTG

/**
 * usb_bus_start_enum - start immediate enumeration (for OTG)
 * @bus: the bus (must use hcd framework)
 * @port_num: 1-based number of port; usually bus->otg_port
 * Context: in_interrupt()
 *
 * Starts enumeration, with an immediate reset followed later by
 * hub_wq identifying and possibly configuring the device.
 * This is needed by OTG controller drivers, where it helps meet
 * HNP protocol timing requirements for starting a port reset.
 *
 * Return: 0 if successful.
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

	/* allocate hub_wq shortly after (first) root port reset finishes;
	 * it may issue others, until at least 50 msecs have passed.
	 */
	if (status == 0)
		mod_timer(&hcd->rh_timer, jiffies + msecs_to_jiffies(10));
	return status;
}
EXPORT_SYMBOL_GPL(usb_bus_start_enum);

#endif

/*-------------------------------------------------------------------------*/

/**
 * usb_hcd_irq - hook IRQs to HCD framework (bus glue)
 * @irq: the IRQ being raised
 * @__hcd: pointer to the HCD whose IRQ is being signaled
 *
 * If the controller isn't HALTed, calls the driver's irq handler.
 * Checks whether the controller is now dead.
 *
 * Return: %IRQ_HANDLED if the IRQ was handled. %IRQ_NONE otherwise.
 */
irqreturn_t usb_hcd_irq (int irq, void *__hcd)
{
	struct usb_hcd		*hcd = __hcd;
	irqreturn_t		rc;

	if (unlikely(HCD_DEAD(hcd) || !HCD_HW_ACCESSIBLE(hcd)))
		rc = IRQ_NONE;
	else if (hcd->driver->irq(hcd) == IRQ_NONE)
		rc = IRQ_NONE;
	else
		rc = IRQ_HANDLED;

	return rc;
}
EXPORT_SYMBOL_GPL(usb_hcd_irq);

/*-------------------------------------------------------------------------*/

/**
 * usb_hc_died - report abnormal shutdown of a host controller (bus glue)
 * @hcd: pointer to the HCD representing the controller
 *
 * This is called by bus glue to report a USB host controller that died
 * while operations may still have been pending.  It's called automatically
 * by the PCI glue, so only glue for non-PCI busses should need to call it.
 *
 * Only call this function with the primary HCD.
 */
void usb_hc_died (struct usb_hcd *hcd)
{
	unsigned long flags;

	dev_err (hcd->self.controller, "HC died; cleaning up\n");

	spin_lock_irqsave (&hcd_root_hub_lock, flags);
	clear_bit(HCD_FLAG_RH_RUNNING, &hcd->flags);
	set_bit(HCD_FLAG_DEAD, &hcd->flags);
	if (hcd->rh_registered) {
		clear_bit(HCD_FLAG_POLL_RH, &hcd->flags);

		/* make hub_wq clean up old urbs and devices */
		usb_set_device_state (hcd->self.root_hub,
				USB_STATE_NOTATTACHED);
		usb_kick_hub_wq(hcd->self.root_hub);
	}
	if (usb_hcd_is_primary_hcd(hcd) && hcd->shared_hcd) {
		hcd = hcd->shared_hcd;
		if (hcd->rh_registered) {
			clear_bit(HCD_FLAG_POLL_RH, &hcd->flags);

			/* make hub_wq clean up old urbs and devices */
			usb_set_device_state(hcd->self.root_hub,
					USB_STATE_NOTATTACHED);
			usb_kick_hub_wq(hcd->self.root_hub);
		}
	}
	spin_unlock_irqrestore (&hcd_root_hub_lock, flags);
	/* Make sure that the other roothub is also deallocated. */
}
EXPORT_SYMBOL_GPL (usb_hc_died);

/*-------------------------------------------------------------------------*/

static void init_giveback_urb_bh(struct giveback_urb_bh *bh)
{

	spin_lock_init(&bh->lock);
	INIT_LIST_HEAD(&bh->head);
	tasklet_init(&bh->bh, usb_giveback_urb_bh, (unsigned long)bh);
}

/**
 * usb_create_shared_hcd - create and initialize an HCD structure
 * @driver: HC driver that will use this hcd
 * @dev: device for this HC, stored in hcd->self.controller
 * @bus_name: value to store in hcd->self.bus_name
 * @primary_hcd: a pointer to the usb_hcd structure that is sharing the
 *              PCI device.  Only allocate certain resources for the primary HCD
 * Context: !in_interrupt()
 *
 * Allocate a struct usb_hcd, with extra space at the end for the
 * HC driver's private data.  Initialize the generic members of the
 * hcd structure.
 *
 * Return: On success, a pointer to the created and initialized HCD structure.
 * On failure (e.g. if memory is unavailable), %NULL.
 */
struct usb_hcd *usb_create_shared_hcd(const struct hc_driver *driver,
		struct device *dev, const char *bus_name,
		struct usb_hcd *primary_hcd)
{
	struct usb_hcd *hcd;

	hcd = kzalloc(sizeof(*hcd) + driver->hcd_priv_size, GFP_KERNEL);
	if (!hcd) {
		dev_dbg (dev, "hcd alloc failed\n");
		return NULL;
	}
	if (primary_hcd == NULL) {
		hcd->bandwidth_mutex = kmalloc(sizeof(*hcd->bandwidth_mutex),
				GFP_KERNEL);
		if (!hcd->bandwidth_mutex) {
			kfree(hcd);
			dev_dbg(dev, "hcd bandwidth mutex alloc failed\n");
			return NULL;
		}
		mutex_init(hcd->bandwidth_mutex);
		dev_set_drvdata(dev, hcd);
	} else {
		mutex_lock(&usb_port_peer_mutex);
		hcd->bandwidth_mutex = primary_hcd->bandwidth_mutex;
		hcd->primary_hcd = primary_hcd;
		primary_hcd->primary_hcd = primary_hcd;
		hcd->shared_hcd = primary_hcd;
		primary_hcd->shared_hcd = hcd;
		mutex_unlock(&usb_port_peer_mutex);
	}

	kref_init(&hcd->kref);

	usb_bus_init(&hcd->self);
	hcd->self.controller = dev;
	hcd->self.bus_name = bus_name;
	hcd->self.uses_dma = (dev->dma_mask != NULL);

	init_timer(&hcd->rh_timer);
	hcd->rh_timer.function = rh_timer_func;
	hcd->rh_timer.data = (unsigned long) hcd;
#ifdef CONFIG_PM
	INIT_WORK(&hcd->wakeup_work, hcd_resume_work);
#endif

	hcd->driver = driver;
	hcd->speed = driver->flags & HCD_MASK;
	hcd->product_desc = (driver->product_desc) ? driver->product_desc :
			"USB Host Controller";
	return hcd;
}
EXPORT_SYMBOL_GPL(usb_create_shared_hcd);

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
 * Return: On success, a pointer to the created and initialized HCD
 * structure. On failure (e.g. if memory is unavailable), %NULL.
 */
struct usb_hcd *usb_create_hcd(const struct hc_driver *driver,
		struct device *dev, const char *bus_name)
{
	return usb_create_shared_hcd(driver, dev, bus_name, NULL);
}
EXPORT_SYMBOL_GPL(usb_create_hcd);

/*
 * Roothubs that share one PCI device must also share the bandwidth mutex.
 * Don't deallocate the bandwidth_mutex until the last shared usb_hcd is
 * deallocated.
 *
 * Make sure to only deallocate the bandwidth_mutex when the primary HCD is
 * freed.  When hcd_release() is called for either hcd in a peer set
 * invalidate the peer's ->shared_hcd and ->primary_hcd pointers to
 * block new peering attempts
 */
static void hcd_release(struct kref *kref)
{
	struct usb_hcd *hcd = container_of (kref, struct usb_hcd, kref);

	mutex_lock(&usb_port_peer_mutex);
	if (usb_hcd_is_primary_hcd(hcd))
		kfree(hcd->bandwidth_mutex);
	if (hcd->shared_hcd) {
		struct usb_hcd *peer = hcd->shared_hcd;

		peer->shared_hcd = NULL;
		if (peer->primary_hcd == hcd)
			peer->primary_hcd = NULL;
	}
	mutex_unlock(&usb_port_peer_mutex);
	kfree(hcd);
}

struct usb_hcd *usb_get_hcd (struct usb_hcd *hcd)
{
	if (hcd)
		kref_get (&hcd->kref);
	return hcd;
}
EXPORT_SYMBOL_GPL(usb_get_hcd);

void usb_put_hcd (struct usb_hcd *hcd)
{
	if (hcd)
		kref_put (&hcd->kref, hcd_release);
}
EXPORT_SYMBOL_GPL(usb_put_hcd);

int usb_hcd_is_primary_hcd(struct usb_hcd *hcd)
{
	if (!hcd->primary_hcd)
		return 1;
	return hcd == hcd->primary_hcd;
}
EXPORT_SYMBOL_GPL(usb_hcd_is_primary_hcd);

int usb_hcd_find_raw_port_number(struct usb_hcd *hcd, int port1)
{
	if (!hcd->driver->find_raw_port_number)
		return port1;

	return hcd->driver->find_raw_port_number(hcd, port1);
}

static int usb_hcd_request_irqs(struct usb_hcd *hcd,
		unsigned int irqnum, unsigned long irqflags)
{
	int retval;

	if (hcd->driver->irq) {

		snprintf(hcd->irq_descr, sizeof(hcd->irq_descr), "%s:usb%d",
				hcd->driver->description, hcd->self.busnum);
		retval = request_irq(irqnum, &usb_hcd_irq, irqflags,
				hcd->irq_descr, hcd);
		if (retval != 0) {
			dev_err(hcd->self.controller,
					"request interrupt %d failed\n",
					irqnum);
			return retval;
		}
		hcd->irq = irqnum;
		dev_info(hcd->self.controller, "irq %d, %s 0x%08llx\n", irqnum,
				(hcd->driver->flags & HCD_MEMORY) ?
					"io mem" : "io base",
					(unsigned long long)hcd->rsrc_start);
	} else {
		hcd->irq = 0;
		if (hcd->rsrc_start)
			dev_info(hcd->self.controller, "%s 0x%08llx\n",
					(hcd->driver->flags & HCD_MEMORY) ?
					"io mem" : "io base",
					(unsigned long long)hcd->rsrc_start);
	}
	return 0;
}

/*
 * Before we free this root hub, flush in-flight peering attempts
 * and disable peer lookups
 */
static void usb_put_invalidate_rhdev(struct usb_hcd *hcd)
{
	struct usb_device *rhdev;

	mutex_lock(&usb_port_peer_mutex);
	rhdev = hcd->self.root_hub;
	hcd->self.root_hub = NULL;
	mutex_unlock(&usb_port_peer_mutex);
	usb_put_dev(rhdev);
}

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

	if (IS_ENABLED(CONFIG_USB_PHY) && !hcd->usb_phy) {
		struct usb_phy *phy = usb_get_phy_dev(hcd->self.controller, 0);

		if (IS_ERR(phy)) {
			retval = PTR_ERR(phy);
			if (retval == -EPROBE_DEFER)
				return retval;
		} else {
			retval = usb_phy_init(phy);
			if (retval) {
				usb_put_phy(phy);
				return retval;
			}
			hcd->usb_phy = phy;
			hcd->remove_phy = 1;
		}
	}

	if (IS_ENABLED(CONFIG_GENERIC_PHY) && !hcd->phy) {
		struct phy *phy = phy_get(hcd->self.controller, "usb");

		if (IS_ERR(phy)) {
			retval = PTR_ERR(phy);
			if (retval == -EPROBE_DEFER)
				goto err_phy;
		} else {
			retval = phy_init(phy);
			if (retval) {
				phy_put(phy);
				goto err_phy;
			}
			retval = phy_power_on(phy);
			if (retval) {
				phy_exit(phy);
				phy_put(phy);
				goto err_phy;
			}
			hcd->phy = phy;
			hcd->remove_phy = 1;
		}
	}

	dev_info(hcd->self.controller, "%s\n", hcd->product_desc);

	/* Keep old behaviour if authorized_default is not in [0, 1]. */
	if (authorized_default < 0 || authorized_default > 1)
		hcd->authorized_default = hcd->wireless ? 0 : 1;
	else
		hcd->authorized_default = authorized_default;
	set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);

	/* HC is in reset state, but accessible.  Now do the one-time init,
	 * bottom up so that hcds can customize the root hubs before hub_wq
	 * starts talking to them.  (Note, bus id is assigned early too.)
	 */
	if ((retval = hcd_buffer_create(hcd)) != 0) {
		dev_dbg(hcd->self.controller, "pool alloc failed\n");
		goto err_create_buf;
	}

	if ((retval = usb_register_bus(&hcd->self)) < 0)
		goto err_register_bus;

	rhdev = usb_alloc_dev(NULL, &hcd->self, 0);
	if (rhdev == NULL) {
		dev_err(hcd->self.controller, "unable to allocate root hub\n");
		retval = -ENOMEM;
		goto err_allocate_root_hub;
	}
	mutex_lock(&usb_port_peer_mutex);
	hcd->self.root_hub = rhdev;
	mutex_unlock(&usb_port_peer_mutex);

	switch (hcd->speed) {
	case HCD_USB11:
		rhdev->speed = USB_SPEED_FULL;
		break;
	case HCD_USB2:
		rhdev->speed = USB_SPEED_HIGH;
		break;
	case HCD_USB25:
		rhdev->speed = USB_SPEED_WIRELESS;
		break;
	case HCD_USB3:
		rhdev->speed = USB_SPEED_SUPER;
		break;
	default:
		retval = -EINVAL;
		goto err_set_rh_speed;
	}

	/* wakeup flag init defaults to "everything works" for root hubs,
	 * but drivers can override it in reset() if needed, along with
	 * recording the overall controller's system wakeup capability.
	 */
	device_set_wakeup_capable(&rhdev->dev, 1);

	/* HCD_FLAG_RH_RUNNING doesn't matter until the root hub is
	 * registered.  But since the controller can die at any time,
	 * let's initialize the flag before touching the hardware.
	 */
	set_bit(HCD_FLAG_RH_RUNNING, &hcd->flags);

	/* "reset" is misnamed; its role is now one-time init. the controller
	 * should already have been reset (and boot firmware kicked off etc).
	 */
	if (hcd->driver->reset && (retval = hcd->driver->reset(hcd)) < 0) {
		dev_err(hcd->self.controller, "can't setup: %d\n", retval);
		goto err_hcd_driver_setup;
	}
	hcd->rh_pollable = 1;

	/* NOTE: root hub and controller capabilities may not be the same */
	if (device_can_wakeup(hcd->self.controller)
			&& device_can_wakeup(&hcd->self.root_hub->dev))
		dev_dbg(hcd->self.controller, "supports USB remote wakeup\n");

	/* initialize tasklets */
	init_giveback_urb_bh(&hcd->high_prio_bh);
	init_giveback_urb_bh(&hcd->low_prio_bh);

	/* enable irqs just before we start the controller,
	 * if the BIOS provides legacy PCI irqs.
	 */
	if (usb_hcd_is_primary_hcd(hcd) && irqnum) {
		retval = usb_hcd_request_irqs(hcd, irqnum, irqflags);
		if (retval)
			goto err_request_irq;
	}

	hcd->state = HC_STATE_RUNNING;
	retval = hcd->driver->start(hcd);
	if (retval < 0) {
		dev_err(hcd->self.controller, "startup error %d\n", retval);
		goto err_hcd_driver_start;
	}

	/* starting here, usbcore will pay attention to this root hub */
	if ((retval = register_root_hub(hcd)) != 0)
		goto err_register_root_hub;

	retval = sysfs_create_group(&rhdev->dev.kobj, &usb_bus_attr_group);
	if (retval < 0) {
		printk(KERN_ERR "Cannot register USB bus sysfs attributes: %d\n",
		       retval);
		goto error_create_attr_group;
	}
	if (hcd->uses_new_polling && HCD_POLL_RH(hcd))
		usb_hcd_poll_rh_status(hcd);

	return retval;

error_create_attr_group:
	clear_bit(HCD_FLAG_RH_RUNNING, &hcd->flags);
	if (HC_IS_RUNNING(hcd->state))
		hcd->state = HC_STATE_QUIESCING;
	spin_lock_irq(&hcd_root_hub_lock);
	hcd->rh_registered = 0;
	spin_unlock_irq(&hcd_root_hub_lock);

#ifdef CONFIG_PM
	cancel_work_sync(&hcd->wakeup_work);
#endif
	mutex_lock(&usb_bus_list_lock);
	usb_disconnect(&rhdev);		/* Sets rhdev to NULL */
	mutex_unlock(&usb_bus_list_lock);
err_register_root_hub:
	hcd->rh_pollable = 0;
	clear_bit(HCD_FLAG_POLL_RH, &hcd->flags);
	del_timer_sync(&hcd->rh_timer);
	hcd->driver->stop(hcd);
	hcd->state = HC_STATE_HALT;
	clear_bit(HCD_FLAG_POLL_RH, &hcd->flags);
	del_timer_sync(&hcd->rh_timer);
err_hcd_driver_start:
	if (usb_hcd_is_primary_hcd(hcd) && hcd->irq > 0)
		free_irq(irqnum, hcd);
err_request_irq:
err_hcd_driver_setup:
err_set_rh_speed:
	usb_put_invalidate_rhdev(hcd);
err_allocate_root_hub:
	usb_deregister_bus(&hcd->self);
err_register_bus:
	hcd_buffer_destroy(hcd);
err_create_buf:
	if (IS_ENABLED(CONFIG_GENERIC_PHY) && hcd->remove_phy && hcd->phy) {
		phy_power_off(hcd->phy);
		phy_exit(hcd->phy);
		phy_put(hcd->phy);
		hcd->phy = NULL;
	}
err_phy:
	if (hcd->remove_phy && hcd->usb_phy) {
		usb_phy_shutdown(hcd->usb_phy);
		usb_put_phy(hcd->usb_phy);
		hcd->usb_phy = NULL;
	}
	return retval;
}
EXPORT_SYMBOL_GPL(usb_add_hcd);

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
	struct usb_device *rhdev = hcd->self.root_hub;

	dev_info(hcd->self.controller, "remove, state %x\n", hcd->state);

	usb_get_dev(rhdev);
	sysfs_remove_group(&rhdev->dev.kobj, &usb_bus_attr_group);

	clear_bit(HCD_FLAG_RH_RUNNING, &hcd->flags);
	if (HC_IS_RUNNING (hcd->state))
		hcd->state = HC_STATE_QUIESCING;

	dev_dbg(hcd->self.controller, "roothub graceful disconnect\n");
	spin_lock_irq (&hcd_root_hub_lock);
	hcd->rh_registered = 0;
	spin_unlock_irq (&hcd_root_hub_lock);

#ifdef CONFIG_PM
	cancel_work_sync(&hcd->wakeup_work);
#endif

	mutex_lock(&usb_bus_list_lock);
	usb_disconnect(&rhdev);		/* Sets rhdev to NULL */
	mutex_unlock(&usb_bus_list_lock);

	/*
	 * tasklet_kill() isn't needed here because:
	 * - driver's disconnect() called from usb_disconnect() should
	 *   make sure its URBs are completed during the disconnect()
	 *   callback
	 *
	 * - it is too late to run complete() here since driver may have
	 *   been removed already now
	 */

	/* Prevent any more root-hub status calls from the timer.
	 * The HCD might still restart the timer (if a port status change
	 * interrupt occurs), but usb_hcd_poll_rh_status() won't invoke
	 * the hub_status_data() callback.
	 */
	hcd->rh_pollable = 0;
	clear_bit(HCD_FLAG_POLL_RH, &hcd->flags);
	del_timer_sync(&hcd->rh_timer);

	hcd->driver->stop(hcd);
	hcd->state = HC_STATE_HALT;

	/* In case the HCD restarted the timer, stop it again. */
	clear_bit(HCD_FLAG_POLL_RH, &hcd->flags);
	del_timer_sync(&hcd->rh_timer);

	if (usb_hcd_is_primary_hcd(hcd)) {
		if (hcd->irq > 0)
			free_irq(hcd->irq, hcd);
	}

	usb_deregister_bus(&hcd->self);
	hcd_buffer_destroy(hcd);

	if (IS_ENABLED(CONFIG_GENERIC_PHY) && hcd->remove_phy && hcd->phy) {
		phy_power_off(hcd->phy);
		phy_exit(hcd->phy);
		phy_put(hcd->phy);
		hcd->phy = NULL;
	}
	if (hcd->remove_phy && hcd->usb_phy) {
		usb_phy_shutdown(hcd->usb_phy);
		usb_put_phy(hcd->usb_phy);
		hcd->usb_phy = NULL;
	}

	usb_put_invalidate_rhdev(hcd);
}
EXPORT_SYMBOL_GPL(usb_remove_hcd);

void
usb_hcd_platform_shutdown(struct platform_device *dev)
{
	struct usb_hcd *hcd = platform_get_drvdata(dev);

	if (hcd->driver->shutdown)
		hcd->driver->shutdown(hcd);
}
EXPORT_SYMBOL_GPL(usb_hcd_platform_shutdown);

/*-------------------------------------------------------------------------*/

#if defined(CONFIG_USB_MON) || defined(CONFIG_USB_MON_MODULE)

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

#endif /* CONFIG_USB_MON || CONFIG_USB_MON_MODULE */
