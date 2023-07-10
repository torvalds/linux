// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * CDC Ethernet based networking peripherals
 * Copyright (C) 2003-2005 by David Brownell
 * Copyright (C) 2006 by Ole Andre Vadla Ravnas (ActiveSync)
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

static int is_novatel_rndis(struct usb_interface_descriptor *desc)
{
	return (desc->bInterfaceClass == USB_CLASS_MISC &&
		desc->bInterfaceSubClass == 4 &&
		desc->bInterfaceProtocol == 1);
}

#else

#define is_rndis(desc)		0
#define is_activesync(desc)	0
#define is_wireless_rndis(desc)	0
#define is_novatel_rndis(desc)	0

#endif

static const u8 mbm_guid[16] = {
	0xa3, 0x17, 0xa8, 0x8b, 0x04, 0x5e, 0x4f, 0x01,
	0xa6, 0x07, 0xc0, 0xff, 0xcb, 0x7e, 0x39, 0x2a,
};

void usbnet_cdc_update_filter(struct usbnet *dev)
{
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
			dev->intf->cur_altsetting->desc.bInterfaceNumber,
			NULL,
			0,
			USB_CTRL_SET_TIMEOUT
		);
}
EXPORT_SYMBOL_GPL(usbnet_cdc_update_filter);

/* We need to override usbnet_*_link_ksettings in bind() */
static const struct ethtool_ops cdc_ether_ethtool_ops = {
	.get_link		= usbnet_get_link,
	.nway_reset		= usbnet_nway_reset,
	.get_drvinfo		= usbnet_get_drvinfo,
	.get_msglevel		= usbnet_get_msglevel,
	.set_msglevel		= usbnet_set_msglevel,
	.get_ts_info		= ethtool_op_get_ts_info,
	.get_link_ksettings	= usbnet_get_link_ksettings_internal,
	.set_link_ksettings	= NULL,
};

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
		 is_wireless_rndis(&intf->cur_altsetting->desc) ||
		 is_novatel_rndis(&intf->cur_altsetting->desc));

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
	info->control = usb_ifnum_to_if(dev->udev, info->u->bMasterInterface0);
	info->data = usb_ifnum_to_if(dev->udev, info->u->bSlaveInterface0);
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
		dev_dbg(&intf->dev, "slave class %u\n", d->bInterfaceClass);
		goto bad_desc;
	}
skip:
	/* Communication class functions with bmCapabilities are not
	 * RNDIS.  But some Wireless class RNDIS functions use
	 * bmCapabilities for their own purpose. The failsafe is
	 * therefore applied only to Communication class RNDIS
	 * functions.  The rndis test is redundant, but a cheap
	 * optimization.
	 */
	if (rndis && is_rndis(&intf->cur_altsetting->desc) &&
	    header.usb_cdc_acm_descriptor &&
	    header.usb_cdc_acm_descriptor->bmCapabilities) {
		dev_dbg(&intf->dev,
			"ACM capabilities %02x, not really RNDIS?\n",
			header.usb_cdc_acm_descriptor->bmCapabilities);
		goto bad_desc;
	}

	if (header.usb_cdc_ether_desc && info->ether->wMaxSegmentSize) {
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

		dev->status = &info->control->cur_altsetting->endpoint[0];
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

	/* override ethtool_ops */
	dev->net->ethtool_ops = &cdc_ether_ethtool_ops;

	return 0;

bad_desc:
	dev_info(&dev->udev->dev, "bad CDC descriptors\n");
	return -ENODEV;
}
EXPORT_SYMBOL_GPL(usbnet_generic_cdc_bind);


/* like usbnet_generic_cdc_bind() but handles filter initialization
 * correctly
 */
int usbnet_ether_cdc_bind(struct usbnet *dev, struct usb_interface *intf)
{
	int rv;

	rv = usbnet_generic_cdc_bind(dev, intf);
	if (rv < 0)
		goto bail_out;

	/* Some devices don't initialise properly. In particular
	 * the packet filter is not reset. There are devices that
	 * don't do reset all the way. So the packet filter should
	 * be set to a sane initial value.
	 */
	usbnet_cdc_update_filter(dev);

bail_out:
	return rv;
}
EXPORT_SYMBOL_GPL(usbnet_ether_cdc_bind);

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

static void speed_change(struct usbnet *dev, __le32 *speeds)
{
	dev->tx_speed = __le32_to_cpu(speeds[0]);
	dev->rx_speed = __le32_to_cpu(speeds[1]);
}

void usbnet_cdc_status(struct usbnet *dev, struct urb *urb)
{
	struct usb_cdc_notification	*event;

	if (urb->actual_length < sizeof(*event))
		return;

	/* SPEED_CHANGE can get split into two 8-byte packets */
	if (test_and_clear_bit(EVENT_STS_SPLIT, &dev->flags)) {
		speed_change(dev, (__le32 *) urb->transfer_buffer);
		return;
	}

	event = urb->transfer_buffer;
	switch (event->bNotificationType) {
	case USB_CDC_NOTIFY_NETWORK_CONNECTION:
		netif_dbg(dev, timer, dev->net, "CDC: carrier %s\n",
			  event->wValue ? "on" : "off");
		usbnet_link_change(dev, !!event->wValue, 0);
		break;
	case USB_CDC_NOTIFY_SPEED_CHANGE:	/* tx/rx rates */
		netif_dbg(dev, timer, dev->net, "CDC: speed change (len %d)\n",
			  urb->actual_length);
		if (urb->actual_length != (sizeof(*event) + 8))
			set_bit(EVENT_STS_SPLIT, &dev->flags);
		else
			speed_change(dev, (__le32 *) &event[1]);
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

	status = usbnet_ether_cdc_bind(dev, intf);
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
int usbnet_cdc_zte_rx_fixup(struct usbnet *dev, struct sk_buff *skb)
{
	if (skb->len < ETH_HLEN || !(skb->data[0] & 0x02))
		return 1;

	skb_reset_mac_header(skb);
	ether_addr_copy(eth_hdr(skb)->h_dest, dev->net->dev_addr);

	return 1;
}
EXPORT_SYMBOL_GPL(usbnet_cdc_zte_rx_fixup);

/* Ensure correct link state
 *
 * Some devices (ZTE MF823/831/910) export two carrier on notifications when
 * connected. This causes the link state to be incorrect. Work around this by
 * always setting the state to off, then on.
 */
static void usbnet_cdc_zte_status(struct usbnet *dev, struct urb *urb)
{
	struct usb_cdc_notification *event;

	if (urb->actual_length < sizeof(*event))
		return;

	event = urb->transfer_buffer;

	if (event->bNotificationType != USB_CDC_NOTIFY_NETWORK_CONNECTION) {
		usbnet_cdc_status(dev, urb);
		return;
	}

	netif_dbg(dev, timer, dev->net, "CDC: carrier %s\n",
		  event->wValue ? "on" : "off");

	if (event->wValue &&
	    netif_carrier_ok(dev->net))
		netif_carrier_off(dev->net);

	usbnet_link_change(dev, !!event->wValue, 0);
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
	.status =	usbnet_cdc_zte_status,
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
#define LINKSYS_VENDOR_ID	0x13b1
#define NVIDIA_VENDOR_ID	0x0955
#define HP_VENDOR_ID		0x03f0
#define MICROSOFT_VENDOR_ID	0x045e
#define UBLOX_VENDOR_ID		0x1546
#define TPLINK_VENDOR_ID	0x2357
#define AQUANTIA_VENDOR_ID	0x2eca
#define ASIX_VENDOR_ID		0x0b95

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

#define ZAURUS_FAKE_INTERFACE \
	.bInterfaceClass	= USB_CLASS_COMM, \
	.bInterfaceSubClass	= USB_CDC_SUBCLASS_MDLM, \
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
	.idProduct              = 0x9032,	/* SL-6000 */
	ZAURUS_FAKE_INTERFACE,
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

/* HP lt2523 (Novatel E371) - handled by qmi_wwan */
{
	USB_DEVICE_AND_INTERFACE_INFO(HP_VENDOR_ID, 0x421d, USB_CLASS_COMM,
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

/* Realtek RTL8153 Based USB 3.0 Ethernet Adapters */
{
	USB_DEVICE_AND_INTERFACE_INFO(REALTEK_VENDOR_ID, 0x8153, USB_CLASS_COMM,
			USB_CDC_SUBCLASS_ETHERNET, USB_CDC_PROTO_NONE),
	.driver_info = 0,
},

/* Lenovo Powered USB-C Travel Hub (4X90S92381, based on Realtek RTL8153) */
{
	USB_DEVICE_AND_INTERFACE_INFO(LENOVO_VENDOR_ID, 0x721e, USB_CLASS_COMM,
			USB_CDC_SUBCLASS_ETHERNET, USB_CDC_PROTO_NONE),
	.driver_info = 0,
},

/* Aquantia AQtion USB to 5GbE Controller (based on AQC111U) */
{
	USB_DEVICE_AND_INTERFACE_INFO(AQUANTIA_VENDOR_ID, 0xc101,
				      USB_CLASS_COMM, USB_CDC_SUBCLASS_ETHERNET,
				      USB_CDC_PROTO_NONE),
	.driver_info = 0,
},

/* ASIX USB 3.1 Gen1 to 5G Multi-Gigabit Ethernet Adapter(based on AQC111U) */
{
	USB_DEVICE_AND_INTERFACE_INFO(ASIX_VENDOR_ID, 0x2790, USB_CLASS_COMM,
				      USB_CDC_SUBCLASS_ETHERNET,
				      USB_CDC_PROTO_NONE),
	.driver_info = 0,
},

/* ASIX USB 3.1 Gen1 to 2.5G Multi-Gigabit Ethernet Adapter(based on AQC112U) */
{
	USB_DEVICE_AND_INTERFACE_INFO(ASIX_VENDOR_ID, 0x2791, USB_CLASS_COMM,
				      USB_CDC_SUBCLASS_ETHERNET,
				      USB_CDC_PROTO_NONE),
	.driver_info = 0,
},

/* USB-C 3.1 to 5GBASE-T Ethernet Adapter (based on AQC111U) */
{
	USB_DEVICE_AND_INTERFACE_INFO(0x20f4, 0xe05a, USB_CLASS_COMM,
				      USB_CDC_SUBCLASS_ETHERNET,
				      USB_CDC_PROTO_NONE),
	.driver_info = 0,
},

/* QNAP QNA-UC5G1T USB to 5GbE Adapter (based on AQC111U) */
{
	USB_DEVICE_AND_INTERFACE_INFO(0x1c04, 0x0015, USB_CLASS_COMM,
				      USB_CDC_SUBCLASS_ETHERNET,
				      USB_CDC_PROTO_NONE),
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
	/* Huawei ME906 and ME909 */
	USB_DEVICE_AND_INTERFACE_INFO(HUAWEI_VENDOR_ID, 0x15c1, USB_CLASS_COMM,
				      USB_CDC_SUBCLASS_ETHERNET,
				      USB_CDC_PROTO_NONE),
	.driver_info = (unsigned long)&wwan_info,
}, {
	/* ZTE modules */
	USB_VENDOR_AND_INTERFACE_INFO(ZTE_VENDOR_ID, USB_CLASS_COMM,
				      USB_CDC_SUBCLASS_ETHERNET,
				      USB_CDC_PROTO_NONE),
	.driver_info = (unsigned long)&zte_cdc_info,
}, {
	/* U-blox TOBY-L2 */
	USB_DEVICE_AND_INTERFACE_INFO(UBLOX_VENDOR_ID, 0x1143, USB_CLASS_COMM,
				      USB_CDC_SUBCLASS_ETHERNET,
				      USB_CDC_PROTO_NONE),
	.driver_info = (unsigned long)&wwan_info,
}, {
	/* U-blox SARA-U2 */
	USB_DEVICE_AND_INTERFACE_INFO(UBLOX_VENDOR_ID, 0x1104, USB_CLASS_COMM,
				      USB_CDC_SUBCLASS_ETHERNET,
				      USB_CDC_PROTO_NONE),
	.driver_info = (unsigned long)&wwan_info,
}, {
	/* U-blox LARA-R6 01B */
	USB_DEVICE_AND_INTERFACE_INFO(UBLOX_VENDOR_ID, 0x1313, USB_CLASS_COMM,
				      USB_CDC_SUBCLASS_ETHERNET,
				      USB_CDC_PROTO_NONE),
	.driver_info = (unsigned long)&wwan_info,
}, {
	/* U-blox LARA-L6 */
	USB_DEVICE_AND_INTERFACE_INFO(UBLOX_VENDOR_ID, 0x1343, USB_CLASS_COMM,
				      USB_CDC_SUBCLASS_ETHERNET,
				      USB_CDC_PROTO_NONE),
	.driver_info = (unsigned long)&wwan_info,
}, {
	/* Cinterion PLS8 modem by GEMALTO */
	USB_DEVICE_AND_INTERFACE_INFO(0x1e2d, 0x0061, USB_CLASS_COMM,
				      USB_CDC_SUBCLASS_ETHERNET,
				      USB_CDC_PROTO_NONE),
	.driver_info = (unsigned long)&wwan_info,
}, {
	/* Cinterion AHS3 modem by GEMALTO */
	USB_DEVICE_AND_INTERFACE_INFO(0x1e2d, 0x0055, USB_CLASS_COMM,
				      USB_CDC_SUBCLASS_ETHERNET,
				      USB_CDC_PROTO_NONE),
	.driver_info = (unsigned long)&wwan_info,
}, {
	/* Cinterion PLS62-W modem by GEMALTO/THALES */
	USB_DEVICE_AND_INTERFACE_INFO(0x1e2d, 0x005b, USB_CLASS_COMM,
				      USB_CDC_SUBCLASS_ETHERNET,
				      USB_CDC_PROTO_NONE),
	.driver_info = (unsigned long)&wwan_info,
}, {
	/* Cinterion PLS83/PLS63 modem by GEMALTO/THALES */
	USB_DEVICE_AND_INTERFACE_INFO(0x1e2d, 0x0069, USB_CLASS_COMM,
				      USB_CDC_SUBCLASS_ETHERNET,
				      USB_CDC_PROTO_NONE),
	.driver_info = (unsigned long)&wwan_info,
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
