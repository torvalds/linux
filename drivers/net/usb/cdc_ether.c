/*
 * CDC Ethernet based networking peripherals
 * Copyright (C) 2003-2005 by David Brownell
 * Copyright (C) 2006 by Ole Andre Vadla Ravnas (ActiveSync)
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

// #define	DEBUG			// error path messages, extra info
// #define	VERBOSE			// more; success messages

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/workqueue.h>
#include <linux/mii.h>
#include <linux/usb.h>
#include <linux/usb/cdc.h>
#include <linux/usb/usbnet.h>


#if IS_ENABLED(CONFIG_USB_NET_RNDIS_HOST)

static int is_rndis(struct usb_interface_descriptor *desc)
{
	return (desc->bInterfaceClass == USB_CLASS_COMM &&
		desc->bInterfaceSubClass == 2 &&
		desc->bInterfaceProtocol == 0xff);
}

static int is_activesync(struct usb_interface_descriptor *desc)
{
	return (desc->bInterfaceClass == USB_CLASS_MISC &&
		desc->bInterfaceSubClass == 1 &&
		desc->bInterfaceProtocol == 1);
}

static int is_wireless_rndis(struct usb_interface_descriptor *desc)
{
	return (desc->bInterfaceClass == USB_CLASS_WIRELESS_CONTROLLER &&
		desc->bInterfaceSubClass == 1 &&
		desc->bInterfaceProtocol == 3);
}

#else

#define is_rndis(desc)		0
#define is_activesync(desc)	0
#define is_wireless_rndis(desc)	0

#endif

static const u8 mbm_guid[16] = {
	0xa3, 0x17, 0xa8, 0x8b, 0x04, 0x5e, 0x4f, 0x01,
	0xa6, 0x07, 0xc0, 0xff, 0xcb, 0x7e, 0x39, 0x2a,
};

static void usbnet_cdc_update_filter(struct usbnet *dev)
{
	struct cdc_state	*info = (void *) &dev->data;
	struct usb_interface	*intf = info->control;
	struct net_device	*net = dev->net;

	u16 cdc_filter = USB_CDC_PACKET_TYPE_DIRECTED
			| USB_CDC_PACKET_TYPE_BROADCAST;

	/* filtering on the device is an optional feature and not worth
	 * the hassle so we just roughly care about snooping and if any
	 * multicast is requested, we take every multicast
	 */
	if (net->flags & IFF_PROMISC)
		cdc_filter |= USB_CDC_PACKET_TYPE_PROMISCUOUS;
	if (!netdev_mc_empty(net) || (net->flags & IFF_ALLMULTI))
		cdc_filter |= USB_CDC_PACKET_TYPE_ALL_MULTICAST;

	usb_control_msg(dev->udev,
			usb_sndctrlpipe(dev->udev, 0),
			USB_CDC_SET_ETHERNET_PACKET_FILTER,
			USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			cdc_filter,
			intf->cur_altsetting->desc.bInterfaceNumber,
			NULL,
			0,
			USB_CTRL_SET_TIMEOUT
		);
}

/* probes control interface, claims data interface, collects the bulk
 * endpoints, activates data interface (if needed), maybe sets MTU.
 * all pure cdc, except for certain firmware workarounds, and knowing
 * that rndis uses one different rule.
 */
int usbnet_generic_cdc_bind(struct usbnet *dev, struct usb_interface *intf)
{
	u8				*buf = intf->cur_altsetting->extra;
	int				len = intf->cur_altsetting->extralen;
	struct usb_interface_descriptor	*d;
	struct cdc_state		*info = (void *) &dev->data;
	int				status;
	int				rndis;
	bool				android_rndis_quirk = false;
	struct usb_driver		*driver = driver_of(intf);
	struct usb_cdc_parsed_header header;

	if (sizeof(dev->data) < sizeof(*info))
		return -EDOM;

	/* expect strict spec conformance for the descriptors, but
	 * cope with firmware which stores them in the wrong place
	 */
	if (len == 0 && dev->udev->actconfig->extralen) {
		/* Motorola SB4100 (and others: Brad Hards says it's
		 * from a Broadcom design) put CDC descriptors here
		 */
		buf = dev->udev->actconfig->extra;
		len = dev->udev->actconfig->extralen;
		dev_dbg(&intf->dev, "CDC descriptors on config\n");
	}

	/* Maybe CDC descriptors are after the endpoint?  This bug has
	 * been seen on some 2Wire Inc RNDIS-ish products.
	 */
	if (len == 0) {
		struct usb_host_endpoint	*hep;

		hep = intf->cur_altsetting->endpoint;
		if (hep) {
			buf = hep->extra;
			len = hep->extralen;
		}
		if (len)
			dev_dbg(&intf->dev,
				"CDC descriptors on endpoint\n");
	}

	/* this assumes that if there's a non-RNDIS vendor variant
	 * of cdc-acm, it'll fail RNDIS requests cleanly.
	 */
	rndis = (is_rndis(&intf->cur_altsetting->desc) ||
		 is_activesync(&intf->cur_altsetting->desc) ||
		 is_wireless_rndis(&intf->cur_altsetting->desc));

	memset(info, 0, sizeof(*info));
	info->control = intf;

	cdc_parse_cdc_header(&header, intf, buf, len);

	info->u = header.usb_cdc_union_desc;
	info->header = header.usb_cdc_header_desc;
	info->ether = header.usb_cdc_ether_desc;
	if (!info->u) {
		if (rndis)
			goto skip;
		else /* in that case a quirk is mandatory */
			goto bad_desc;
	}
	/* we need a master/control interface (what we're
	 * probed with) and a slave/data interface; union
	 * descriptors sort this all out.
	 */
	info->control = usb_ifnum_to_if(dev->udev,
	info->u->bMasterInterface0);
	info->data = usb_ifnum_to_if(dev->udev,
		info->u->bSlaveInterface0);
	if (!info->control || !info->data) {
		dev_dbg(&intf->dev,
			"master #%u/%p slave #%u/%p\n",
			info->u->bMasterInterface0,
			info->control,
			info->u->bSlaveInterface0,
			info->data);
		/* fall back to hard-wiring for RNDIS */
		if (rndis) {
			android_rndis_quirk = true;
			goto skip;
		}
		goto bad_desc;
	}
	if (info->control != intf) {
		dev_dbg(&intf->dev, "bogus CDC Union\n");
		/* Ambit USB Cable Modem (and maybe others)
		 * interchanges master and slave interface.
		 */
		if (info->data == intf) {
			info->data = info->control;
			info->control = intf;
		} else
			goto bad_desc;
	}

	/* some devices merge these - skip class check */
	if (info->control == info->data)
		goto skip;

	/* a data interface altsetting does the real i/o */
	d = &info->data->cur_altsetting->desc;
	if (d->bInterfaceClass != USB_CLASS_CDC_DATA) {
		dev_dbg(&intf->dev, "slave class %u\n",
			d->bInterfaceClass);
		goto bad_desc;
	}
skip:
	if (	rndis &&
		header.usb_cdc_acm_descriptor &&
		header.usb_cdc_acm_descriptor->bmCapabilities) {
			dev_dbg(&intf->dev,
				"ACM capabilities %02x, not really RNDIS?\n",
				header.usb_cdc_acm_descriptor->bmCapabilities);
			goto bad_desc;
	}

	if (header.usb_cdc_ether_desc) {
		dev->hard_mtu = le16_to_cpu(info->ether->wMaxSegmentSize);
		/* because of Zaurus, we may be ignoring the host
		 * side link address we were given.
		 */
	}

	if (header.usb_cdc_mdlm_desc &&
		memcmp(header.usb_cdc_mdlm_desc->bGUID, mbm_guid, 16)) {
		dev_dbg(&intf->dev, "GUID doesn't match\n");
		goto bad_desc;
	}

	if (header.usb_cdc_mdlm_detail_desc &&
		header.usb_cdc_mdlm_detail_desc->bLength <
			(sizeof(struct usb_cdc_mdlm_detail_desc) + 1)) {
		dev_dbg(&intf->dev, "Descriptor too short\n");
		goto bad_desc;
	}



	/* Microsoft ActiveSync based and some regular RNDIS devices lack the
	 * CDC descriptors, so we'll hard-wire the interfaces and not check
	 * for descriptors.
	 *
	 * Some Android RNDIS devices have a CDC Union descriptor pointing
	 * to non-existing interfaces.  Ignore that and attempt the same
	 * hard-wired 0 and 1 interfaces.
	 */
	if (rndis && (!info->u || android_rndis_quirk)) {
		info->control = usb_ifnum_to_if(dev->udev, 0);
		info->data = usb_ifnum_to_if(dev->udev, 1);
		if (!info->control || !info->data || info->control != intf) {
			dev_dbg(&intf->dev,
				"rndis: master #0/%p slave #1/%p\n",
				info->control,
				info->data);
			goto bad_desc;
		}

	} else if (!info->header || (!rndis && !info->ether)) {
		dev_dbg(&intf->dev, "missing cdc %s%s%sdescriptor\n",
			info->header ? "" : "header ",
			info->u ? "" : "union ",
			info->ether ? "" : "ether ");
		goto bad_desc;
	}

	/* claim data interface and set it up ... with side effects.
	 * network traffic can't flow until an altsetting is enabled.
	 */
	if (info->data != info->control) {
		status = usb_driver_claim_interface(driver, info->data, dev);
		if (status < 0)
			return status;
	}
	status = usbnet_get_endpoints(dev, info->data);
	if (status < 0) {
		/* ensure immediate exit from usbnet_disconnect */
		usb_set_intfdata(info->data, NULL);
		if (info->data != info->control)
			usb_driver_release_interface(driver, info->data);
		return status;
	}

	/* status endpoint: optional for CDC Ethernet, not RNDIS (or ACM) */
	if (info->data != info->control)
		dev->status = NULL;
	if (info->control->cur_altsetting->desc.bNumEndpoints == 1) {
		struct usb_endpoint_descriptor	*desc;

		dev->status = &info->control->cur_altsetting->endpoint [0];
		desc = &dev->status->desc;
		if (!usb_endpoint_is_int_in(desc) ||
		    (le16_to_cpu(desc->wMaxPacketSize)
		     < sizeof(struct usb_cdc_notification)) ||
		    !desc->bInterval) {
			dev_dbg(&intf->dev, "bad notification endpoint\n");
			dev->status = NULL;
		}
	}
	if (rndis && !dev->status) {
		dev_dbg(&intf->dev, "missing RNDIS status endpoint\n");
		usb_set_intfdata(info->data, NULL);
		usb_driver_release_interface(driver, info->data);
		return -ENODEV;
	}

	/* Some devices don't initialise properly. In particular
	 * the packet filter is not reset. There are devices that
	 * don't do reset all the way. So the packet filter should
	 * be set to a sane initial value.
	 */
	usbnet_cdc_update_filter(dev);

	return 0;

bad_desc:
	dev_info(&dev->udev->dev, "bad CDC descriptors\n");
	return -ENODEV;
}
EXPORT_SYMBOL_GPL(usbnet_generic_cdc_bind);

void usbnet_cdc_unbind(struct usbnet *dev, struct usb_interface *intf)
{
	struct cdc_state		*info = (void *) &dev->data;
	struct usb_driver		*driver = driver_of(intf);

	/* combined interface - nothing  to do */
	if (info->data == info->control)
		return;

	/* disconnect master --> disconnect slave */
	if (intf == info->control && info->data) {
		/* ensure immediate exit from usbnet_disconnect */
		usb_set_intfdata(info->data, NULL);
		usb_driver_release_interface(driver, info->data);
		info->data = NULL;
	}

	/* and vice versa (just in case) */
	else if (intf == info->data && info->control) {
		/* ensure immediate exit from usbnet_disconnect */
		usb_set_intfdata(info->control, NULL);
		usb_driver_release_interface(driver, info->control);
		info->control = NULL;
	}
}
EXPORT_SYMBOL_GPL(usbnet_cdc_unbind);

/* Communications Device Class, Ethernet Control model
 *
 * Takes two interfaces.  The DATA interface is inactive till an altsetting
 * is selected.  Configuration data includes class descriptors.  There's
 * an optional status endpoint on the control interface.
 *
 * This should interop with whatever the 2.4 "CDCEther.c" driver
 * (by Brad Hards) talked with, with more functionality.
 */

static void dumpspeed(struct usbnet *dev, __le32 *speeds)
{
	netif_info(dev, timer, dev->net,
		   "link speeds: %u kbps up, %u kbps down\n",
		   __le32_to_cpu(speeds[0]) / 1000,
		   __le32_to_cpu(speeds[1]) / 1000);
}

void usbnet_cdc_status(struct usbnet *dev, struct urb *urb)
{
	struct usb_cdc_notification	*event;

	if (urb->actual_length < sizeof(*event))
		return;

	/* SPEED_CHANGE can get split into two 8-byte packets */
	if (test_and_clear_bit(EVENT_STS_SPLIT, &dev->flags)) {
		dumpspeed(dev, (__le32 *) urb->transfer_buffer);
		return;
	}

	event = urb->transfer_buffer;
	switch (event->bNotificationType) {
	case USB_CDC_NOTIFY_NETWORK_CONNECTION:
		netif_dbg(dev, timer, dev->net, "CDC: carrier %s\n",
			  event->wValue ? "on" : "off");

		/* Work-around for devices with broken off-notifications */
		if (event->wValue &&
		    !test_bit(__LINK_STATE_NOCARRIER, &dev->net->state))
			usbnet_link_change(dev, 0, 0);

		usbnet_link_change(dev, !!event->wValue, 0);
		break;
	case USB_CDC_NOTIFY_SPEED_CHANGE:	/* tx/rx rates */
		netif_dbg(dev, timer, dev->net, "CDC: speed change (len %d)\n",
			  urb->actual_length);
		if (urb->actual_length != (sizeof(*event) + 8))
			set_bit(EVENT_STS_SPLIT, &dev->flags);
		else
			dumpspeed(dev, (__le32 *) &event[1]);
		break;
	/* USB_CDC_NOTIFY_RESPONSE_AVAILABLE can happen too (e.g. RNDIS),
	 * but there are no standard formats for the response data.
	 */
	default:
		netdev_err(dev->net, "CDC: unexpected notification %02x!\n",
			   event->bNotificationType);
		break;
	}
}
EXPORT_SYMBOL_GPL(usbnet_cdc_status);

int usbnet_cdc_bind(struct usbnet *dev, struct usb_interface *intf)
{
	int				status;
	struct cdc_state		*info = (void *) &dev->data;

	BUILD_BUG_ON((sizeof(((struct usbnet *)0)->data)
			< sizeof(struct cdc_state)));

	status = usbnet_generic_cdc_bind(dev, intf);
	if (status < 0)
		return status;

	status = usbnet_get_ethernet_addr(dev, info->ether->iMACAddress);
	if (status < 0) {
		usb_set_intfdata(info->data, NULL);
		usb_driver_release_interface(driver_of(intf), info->data);
		return status;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(usbnet_cdc_bind);

static int usbnet_cdc_zte_bind(struct usbnet *dev, struct usb_interface *intf)
{
	int status = usbnet_cdc_bind(dev, intf);

	if (!status && (dev->net->dev_addr[0] & 0x02))
		eth_hw_addr_random(dev->net);

	return status;
}

/* Make sure packets have correct destination MAC address
 *
 * A firmware bug observed on some devices (ZTE MF823/831/910) is that the
 * device sends packets with a static, bogus, random MAC address (event if
 * device MAC address has been updated). Always set MAC address to that of the
 * device.
 */
static int usbnet_cdc_zte_rx_fixup(struct usbnet *dev, struct sk_buff *skb)
{
	if (skb->len < ETH_HLEN || !(skb->data[0] & 0x02))
		return 1;

	skb_reset_mac_header(skb);
	ether_addr_copy(eth_hdr(skb)->h_dest, dev->net->dev_addr);

	return 1;
}

static const struct driver_info	cdc_info = {
	.description =	"CDC Ethernet Device",
	.flags =	FLAG_ETHER | FLAG_POINTTOPOINT,
	.bind =		usbnet_cdc_bind,
	.unbind =	usbnet_cdc_unbind,
	.status =	usbnet_cdc_status,
	.set_rx_mode =	usbnet_cdc_update_filter,
	.manage_power =	usbnet_manage_power,
};

static const struct driver_info	zte_cdc_info = {
	.description =	"ZTE CDC Ethernet Device",
	.flags =	FLAG_ETHER | FLAG_POINTTOPOINT,
	.bind =		usbnet_cdc_zte_bind,
	.unbind =	usbnet_cdc_unbind,
	.status =	usbnet_cdc_status,
	.set_rx_mode =	usbnet_cdc_update_filter,
	.manage_power =	usbnet_manage_power,
	.rx_fixup = usbnet_cdc_zte_rx_fixup,
};

static const struct driver_info wwan_info = {
	.description =	"Mobile Broadband Network Device",
	.flags =	FLAG_WWAN,
	.bind =		usbnet_cdc_bind,
	.unbind =	usbnet_cdc_unbind,
	.status =	usbnet_cdc_status,
	.set_rx_mode =	usbnet_cdc_update_filter,
	.manage_power =	usbnet_manage_power,
};

/*-------------------------------------------------------------------------*/

#define HUAWEI_VENDOR_ID	0x12D1
#define NOVATEL_VENDOR_ID	0x1410
#define ZTE_VENDOR_ID		0x19D2
#define DELL_VENDOR_ID		0x413C
#define REALTEK_VENDOR_ID	0x0bda
#define SAMSUNG_VENDOR_ID	0x04e8
#define LENOVO_VENDOR_ID	0x17ef
#define NVIDIA_VENDOR_ID	0x0955

static const struct usb_device_id	products[] = {
/* BLACKLIST !!
 *
 * First blacklist any products that are egregiously nonconformant
 * with the CDC Ethernet specs.  Minor braindamage we cope with; when
 * they're not even trying, needing a separate driver is only the first
 * of the differences to show up.
 */

#define	ZAURUS_MASTER_INTERFACE \
	.bInterfaceClass	= USB_CLASS_COMM, \
	.bInterfaceSubClass	= USB_CDC_SUBCLASS_ETHERNET, \
	.bInterfaceProtocol	= USB_CDC_PROTO_NONE

/* SA-1100 based Sharp Zaurus ("collie"), or compatible;
 * wire-incompatible with true CDC Ethernet implementations.
 * (And, it seems, needlessly so...)
 */
{
	.match_flags	=   USB_DEVICE_ID_MATCH_INT_INFO
			  | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor		= 0x04DD,
	.idProduct		= 0x8004,
	ZAURUS_MASTER_INTERFACE,
	.driver_info		= 0,
},

/* PXA-25x based Sharp Zaurii.  Note that it seems some of these
 * (later models especially) may have shipped only with firmware
 * advertising false "CDC MDLM" compatibility ... but we're not
 * clear which models did that, so for now let's assume the worst.
 */
{
	.match_flags	=   USB_DEVICE_ID_MATCH_INT_INFO
			  | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor		= 0x04DD,
	.idProduct		= 0x8005,	/* A-300 */
	ZAURUS_MASTER_INTERFACE,
	.driver_info		= 0,
}, {
	.match_flags	=   USB_DEVICE_ID_MATCH_INT_INFO
			  | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor		= 0x04DD,
	.idProduct		= 0x8006,	/* B-500/SL-5600 */
	ZAURUS_MASTER_INTERFACE,
	.driver_info		= 0,
}, {
	.match_flags    =   USB_DEVICE_ID_MATCH_INT_INFO
			  | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor		= 0x04DD,
	.idProduct		= 0x8007,	/* C-700 */
	ZAURUS_MASTER_INTERFACE,
	.driver_info		= 0,
}, {
	.match_flags    =   USB_DEVICE_ID_MATCH_INT_INFO
		 | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor               = 0x04DD,
	.idProduct              = 0x9031,	/* C-750 C-760 */
	ZAURUS_MASTER_INTERFACE,
	.driver_info		= 0,
}, {
	.match_flags    =   USB_DEVICE_ID_MATCH_INT_INFO
		 | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor               = 0x04DD,
	.idProduct              = 0x9032,	/* SL-6000 */
	ZAURUS_MASTER_INTERFACE,
	.driver_info		= 0,
}, {
	.match_flags    =   USB_DEVICE_ID_MATCH_INT_INFO
		 | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor               = 0x04DD,
	/* reported with some C860 units */
	.idProduct              = 0x9050,	/* C-860 */
	ZAURUS_MASTER_INTERFACE,
	.driver_info		= 0,
},

/* Olympus has some models with a Zaurus-compatible option.
 * R-1000 uses a FreeScale i.MXL cpu (ARMv4T)
 */
{
	.match_flags    =   USB_DEVICE_ID_MATCH_INT_INFO
		 | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor               = 0x07B4,
	.idProduct              = 0x0F02,	/* R-1000 */
	ZAURUS_MASTER_INTERFACE,
	.driver_info		= 0,
},

/* LG Electronics VL600 wants additional headers on every frame */
{
	USB_DEVICE_AND_INTERFACE_INFO(0x1004, 0x61aa, USB_CLASS_COMM,
			USB_CDC_SUBCLASS_ETHERNET, USB_CDC_PROTO_NONE),
	.driver_info = 0,
},

/* Logitech Harmony 900 - uses the pseudo-MDLM (BLAN) driver */
{
	USB_DEVICE_AND_INTERFACE_INFO(0x046d, 0xc11f, USB_CLASS_COMM,
			USB_CDC_SUBCLASS_MDLM, USB_CDC_PROTO_NONE),
	.driver_info		= 0,
},

/* Novatel USB551L and MC551 - handled by qmi_wwan */
{
	USB_DEVICE_AND_INTERFACE_INFO(NOVATEL_VENDOR_ID, 0xB001, USB_CLASS_COMM,
			USB_CDC_SUBCLASS_ETHERNET, USB_CDC_PROTO_NONE),
	.driver_info = 0,
},

/* Novatel E362 - handled by qmi_wwan */
{
	USB_DEVICE_AND_INTERFACE_INFO(NOVATEL_VENDOR_ID, 0x9010, USB_CLASS_COMM,
			USB_CDC_SUBCLASS_ETHERNET, USB_CDC_PROTO_NONE),
	.driver_info = 0,
},

/* Dell Wireless 5800 (Novatel E362) - handled by qmi_wwan */
{
	USB_DEVICE_AND_INTERFACE_INFO(DELL_VENDOR_ID, 0x8195, USB_CLASS_COMM,
			USB_CDC_SUBCLASS_ETHERNET, USB_CDC_PROTO_NONE),
	.driver_info = 0,
},

/* Dell Wireless 5800 (Novatel E362) - handled by qmi_wwan */
{
	USB_DEVICE_AND_INTERFACE_INFO(DELL_VENDOR_ID, 0x8196, USB_CLASS_COMM,
			USB_CDC_SUBCLASS_ETHERNET, USB_CDC_PROTO_NONE),
	.driver_info = 0,
},

/* Dell Wireless 5804 (Novatel E371) - handled by qmi_wwan */
{
	USB_DEVICE_AND_INTERFACE_INFO(DELL_VENDOR_ID, 0x819b, USB_CLASS_COMM,
			USB_CDC_SUBCLASS_ETHERNET, USB_CDC_PROTO_NONE),
	.driver_info = 0,
},

/* Novatel Expedite E371 - handled by qmi_wwan */
{
	USB_DEVICE_AND_INTERFACE_INFO(NOVATEL_VENDOR_ID, 0x9011, USB_CLASS_COMM,
			USB_CDC_SUBCLASS_ETHERNET, USB_CDC_PROTO_NONE),
	.driver_info = 0,
},

/* AnyDATA ADU960S - handled by qmi_wwan */
{
	USB_DEVICE_AND_INTERFACE_INFO(0x16d5, 0x650a, USB_CLASS_COMM,
			USB_CDC_SUBCLASS_ETHERNET, USB_CDC_PROTO_NONE),
	.driver_info = 0,
},

/* Huawei E1820 - handled by qmi_wwan */
{
	USB_DEVICE_INTERFACE_NUMBER(HUAWEI_VENDOR_ID, 0x14ac, 1),
	.driver_info = 0,
},

/* Realtek RTL8152 Based USB 2.0 Ethernet Adapters */
{
	USB_DEVICE_AND_INTERFACE_INFO(REALTEK_VENDOR_ID, 0x8152, USB_CLASS_COMM,
			USB_CDC_SUBCLASS_ETHERNET, USB_CDC_PROTO_NONE),
	.driver_info = 0,
},

/* Realtek RTL8153 Based USB 3.0 Ethernet Adapters */
{
	USB_DEVICE_AND_INTERFACE_INFO(REALTEK_VENDOR_ID, 0x8153, USB_CLASS_COMM,
			USB_CDC_SUBCLASS_ETHERNET, USB_CDC_PROTO_NONE),
	.driver_info = 0,
},

/* Samsung USB Ethernet Adapters */
{
	USB_DEVICE_AND_INTERFACE_INFO(SAMSUNG_VENDOR_ID, 0xa101, USB_CLASS_COMM,
			USB_CDC_SUBCLASS_ETHERNET, USB_CDC_PROTO_NONE),
	.driver_info = 0,
},

/* Lenovo Thinkpad USB 3.0 Ethernet Adapters (based on Realtek RTL8153) */
{
	USB_DEVICE_AND_INTERFACE_INFO(LENOVO_VENDOR_ID, 0x7205, USB_CLASS_COMM,
			USB_CDC_SUBCLASS_ETHERNET, USB_CDC_PROTO_NONE),
	.driver_info = 0,
},

/* NVIDIA Tegra USB 3.0 Ethernet Adapters (based on Realtek RTL8153) */
{
	USB_DEVICE_AND_INTERFACE_INFO(NVIDIA_VENDOR_ID, 0x09ff, USB_CLASS_COMM,
			USB_CDC_SUBCLASS_ETHERNET, USB_CDC_PROTO_NONE),
	.driver_info = 0,
},

/* WHITELIST!!!
 *
 * CDC Ether uses two interfaces, not necessarily consecutive.
 * We match the main interface, ignoring the optional device
 * class so we could handle devices that aren't exclusively
 * CDC ether.
 *
 * NOTE:  this match must come AFTER entries blacklisting devices
 * because of bugs/quirks in a given product (like Zaurus, above).
 */
{
	/* ZTE (Vodafone) K3805-Z */
	USB_DEVICE_AND_INTERFACE_INFO(ZTE_VENDOR_ID, 0x1003, USB_CLASS_COMM,
				      USB_CDC_SUBCLASS_ETHERNET,
				      USB_CDC_PROTO_NONE),
	.driver_info = (unsigned long)&wwan_info,
}, {
	/* ZTE (Vodafone) K3806-Z */
	USB_DEVICE_AND_INTERFACE_INFO(ZTE_VENDOR_ID, 0x1015, USB_CLASS_COMM,
				      USB_CDC_SUBCLASS_ETHERNET,
				      USB_CDC_PROTO_NONE),
	.driver_info = (unsigned long)&wwan_info,
}, {
	/* ZTE (Vodafone) K4510-Z */
	USB_DEVICE_AND_INTERFACE_INFO(ZTE_VENDOR_ID, 0x1173, USB_CLASS_COMM,
				      USB_CDC_SUBCLASS_ETHERNET,
				      USB_CDC_PROTO_NONE),
	.driver_info = (unsigned long)&wwan_info,
}, {
	/* ZTE (Vodafone) K3770-Z */
	USB_DEVICE_AND_INTERFACE_INFO(ZTE_VENDOR_ID, 0x1177, USB_CLASS_COMM,
				      USB_CDC_SUBCLASS_ETHERNET,
				      USB_CDC_PROTO_NONE),
	.driver_info = (unsigned long)&wwan_info,
}, {
	/* ZTE (Vodafone) K3772-Z */
	USB_DEVICE_AND_INTERFACE_INFO(ZTE_VENDOR_ID, 0x1181, USB_CLASS_COMM,
				      USB_CDC_SUBCLASS_ETHERNET,
				      USB_CDC_PROTO_NONE),
	.driver_info = (unsigned long)&wwan_info,
}, {
	/* Telit modules */
	USB_VENDOR_AND_INTERFACE_INFO(0x1bc7, USB_CLASS_COMM,
			USB_CDC_SUBCLASS_ETHERNET, USB_CDC_PROTO_NONE),
	.driver_info = (kernel_ulong_t) &wwan_info,
}, {
	/* Dell DW5580 modules */
	USB_DEVICE_AND_INTERFACE_INFO(DELL_VENDOR_ID, 0x81ba, USB_CLASS_COMM,
			USB_CDC_SUBCLASS_ETHERNET, USB_CDC_PROTO_NONE),
	.driver_info = (kernel_ulong_t)&wwan_info,
}, {
	/* ZTE modules */
	USB_VENDOR_AND_INTERFACE_INFO(ZTE_VENDOR_ID, USB_CLASS_COMM,
				      USB_CDC_SUBCLASS_ETHERNET,
				      USB_CDC_PROTO_NONE),
	.driver_info = (unsigned long)&zte_cdc_info,
}, {
	USB_INTERFACE_INFO(USB_CLASS_COMM, USB_CDC_SUBCLASS_ETHERNET,
			USB_CDC_PROTO_NONE),
	.driver_info = (unsigned long) &cdc_info,
}, {
	USB_INTERFACE_INFO(USB_CLASS_COMM, USB_CDC_SUBCLASS_MDLM,
			USB_CDC_PROTO_NONE),
	.driver_info = (unsigned long)&wwan_info,

}, {
	/* Various Huawei modems with a network port like the UMG1831 */
	USB_VENDOR_AND_INTERFACE_INFO(HUAWEI_VENDOR_ID, USB_CLASS_COMM,
				      USB_CDC_SUBCLASS_ETHERNET, 255),
	.driver_info = (unsigned long)&wwan_info,
},
	{ },		/* END */
};
MODULE_DEVICE_TABLE(usb, products);

static struct usb_driver cdc_driver = {
	.name =		"cdc_ether",
	.id_table =	products,
	.probe =	usbnet_probe,
	.disconnect =	usbnet_disconnect,
	.suspend =	usbnet_suspend,
	.resume =	usbnet_resume,
	.reset_resume =	usbnet_resume,
	.supports_autosuspend = 1,
	.disable_hub_initiated_lpm = 1,
};

module_usb_driver(cdc_driver);

MODULE_AUTHOR("David Brownell");
MODULE_DESCRIPTION("USB CDC Ethernet devices");
MODULE_LICENSE("GPL");
