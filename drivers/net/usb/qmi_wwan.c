/*
 * Copyright (c) 2012  Bjørn Mork <bjorn@mork.no>
 *
 * The probing code is heavily inspired by cdc_ether, which is:
 * Copyright (C) 2003-2005 by David Brownell
 * Copyright (C) 2006 by Ole Andre Vadla Ravnas (ActiveSync)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/etherdevice.h>
#include <linux/mii.h>
#include <linux/usb.h>
#include <linux/usb/cdc.h>
#include <linux/usb/usbnet.h>
#include <linux/usb/cdc-wdm.h>

/* This driver supports wwan (3G/LTE/?) devices using a vendor
 * specific management protocol called Qualcomm MSM Interface (QMI) -
 * in addition to the more common AT commands over serial interface
 * management
 *
 * QMI is wrapped in CDC, using CDC encapsulated commands on the
 * control ("master") interface of a two-interface CDC Union
 * resembling standard CDC ECM.  The devices do not use the control
 * interface for any other CDC messages.  Most likely because the
 * management protocol is used in place of the standard CDC
 * notifications NOTIFY_NETWORK_CONNECTION and NOTIFY_SPEED_CHANGE
 *
 * Alternatively, control and data functions can be combined in a
 * single USB interface.
 *
 * Handling a protocol like QMI is out of the scope for any driver.
 * It is exported as a character device using the cdc-wdm driver as
 * a subdriver, enabling userspace applications ("modem managers") to
 * handle it.
 *
 * These devices may alternatively/additionally be configured using AT
 * commands on a serial interface
 */

/* driver specific data */
struct qmi_wwan_state {
	struct usb_driver *subdriver;
	atomic_t pmcount;
	unsigned long unused;
	struct usb_interface *control;
	struct usb_interface *data;
};

/* default ethernet address used by the modem */
static const u8 default_modem_addr[ETH_ALEN] = {0x02, 0x50, 0xf3};

/* Make up an ethernet header if the packet doesn't have one.
 *
 * A firmware bug common among several devices cause them to send raw
 * IP packets under some circumstances.  There is no way for the
 * driver/host to know when this will happen.  And even when the bug
 * hits, some packets will still arrive with an intact header.
 *
 * The supported devices are only capably of sending IPv4, IPv6 and
 * ARP packets on a point-to-point link. Any packet with an ethernet
 * header will have either our address or a broadcast/multicast
 * address as destination.  ARP packets will always have a header.
 *
 * This means that this function will reliably add the appropriate
 * header iff necessary, provided our hardware address does not start
 * with 4 or 6.
 *
 * Another common firmware bug results in all packets being addressed
 * to 00:a0:c6:00:00:00 despite the host address being different.
 * This function will also fixup such packets.
 */
static int qmi_wwan_rx_fixup(struct usbnet *dev, struct sk_buff *skb)
{
	__be16 proto;

	/* This check is no longer done by usbnet */
	if (skb->len < dev->net->hard_header_len)
		return 0;

	switch (skb->data[0] & 0xf0) {
	case 0x40:
		proto = htons(ETH_P_IP);
		break;
	case 0x60:
		proto = htons(ETH_P_IPV6);
		break;
	case 0x00:
		if (is_multicast_ether_addr(skb->data))
			return 1;
		/* possibly bogus destination - rewrite just in case */
		skb_reset_mac_header(skb);
		goto fix_dest;
	default:
		/* pass along other packets without modifications */
		return 1;
	}
	if (skb_headroom(skb) < ETH_HLEN)
		return 0;
	skb_push(skb, ETH_HLEN);
	skb_reset_mac_header(skb);
	eth_hdr(skb)->h_proto = proto;
	memset(eth_hdr(skb)->h_source, 0, ETH_ALEN);
fix_dest:
	memcpy(eth_hdr(skb)->h_dest, dev->net->dev_addr, ETH_ALEN);
	return 1;
}

/* very simplistic detection of IPv4 or IPv6 headers */
static bool possibly_iphdr(const char *data)
{
	return (data[0] & 0xd0) == 0x40;
}

/* disallow addresses which may be confused with IP headers */
static int qmi_wwan_mac_addr(struct net_device *dev, void *p)
{
	int ret;
	struct sockaddr *addr = p;

	ret = eth_prepare_mac_addr_change(dev, p);
	if (ret < 0)
		return ret;
	if (possibly_iphdr(addr->sa_data))
		return -EADDRNOTAVAIL;
	eth_commit_mac_addr_change(dev, p);
	return 0;
}

static const struct net_device_ops qmi_wwan_netdev_ops = {
	.ndo_open		= usbnet_open,
	.ndo_stop		= usbnet_stop,
	.ndo_start_xmit		= usbnet_start_xmit,
	.ndo_tx_timeout		= usbnet_tx_timeout,
	.ndo_change_mtu		= usbnet_change_mtu,
	.ndo_set_mac_address	= qmi_wwan_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
};

/* using a counter to merge subdriver requests with our own into a
 * combined state
 */
static int qmi_wwan_manage_power(struct usbnet *dev, int on)
{
	struct qmi_wwan_state *info = (void *)&dev->data;
	int rv;

	dev_dbg(&dev->intf->dev, "%s() pmcount=%d, on=%d\n", __func__,
		atomic_read(&info->pmcount), on);

	if ((on && atomic_add_return(1, &info->pmcount) == 1) ||
	    (!on && atomic_dec_and_test(&info->pmcount))) {
		/* need autopm_get/put here to ensure the usbcore sees
		 * the new value
		 */
		rv = usb_autopm_get_interface(dev->intf);
		dev->intf->needs_remote_wakeup = on;
		if (!rv)
			usb_autopm_put_interface(dev->intf);
	}
	return 0;
}

static int qmi_wwan_cdc_wdm_manage_power(struct usb_interface *intf, int on)
{
	struct usbnet *dev = usb_get_intfdata(intf);

	/* can be called while disconnecting */
	if (!dev)
		return 0;
	return qmi_wwan_manage_power(dev, on);
}

/* collect all three endpoints and register subdriver */
static int qmi_wwan_register_subdriver(struct usbnet *dev)
{
	int rv;
	struct usb_driver *subdriver = NULL;
	struct qmi_wwan_state *info = (void *)&dev->data;

	/* collect bulk endpoints */
	rv = usbnet_get_endpoints(dev, info->data);
	if (rv < 0)
		goto err;

	/* update status endpoint if separate control interface */
	if (info->control != info->data)
		dev->status = &info->control->cur_altsetting->endpoint[0];

	/* require interrupt endpoint for subdriver */
	if (!dev->status) {
		rv = -EINVAL;
		goto err;
	}

	/* for subdriver power management */
	atomic_set(&info->pmcount, 0);

	/* register subdriver */
	subdriver = usb_cdc_wdm_register(info->control, &dev->status->desc,
					 4096, &qmi_wwan_cdc_wdm_manage_power);
	if (IS_ERR(subdriver)) {
		dev_err(&info->control->dev, "subdriver registration failed\n");
		rv = PTR_ERR(subdriver);
		goto err;
	}

	/* prevent usbnet from using status endpoint */
	dev->status = NULL;

	/* save subdriver struct for suspend/resume wrappers */
	info->subdriver = subdriver;

err:
	return rv;
}

static int qmi_wwan_bind(struct usbnet *dev, struct usb_interface *intf)
{
	int status = -1;
	u8 *buf = intf->cur_altsetting->extra;
	int len = intf->cur_altsetting->extralen;
	struct usb_interface_descriptor *desc = &intf->cur_altsetting->desc;
	struct usb_cdc_union_desc *cdc_union = NULL;
	struct usb_cdc_ether_desc *cdc_ether = NULL;
	u32 found = 0;
	struct usb_driver *driver = driver_of(intf);
	struct qmi_wwan_state *info = (void *)&dev->data;

	BUILD_BUG_ON((sizeof(((struct usbnet *)0)->data) <
		      sizeof(struct qmi_wwan_state)));

	/* set up initial state */
	info->control = intf;
	info->data = intf;

	/* and a number of CDC descriptors */
	while (len > 3) {
		struct usb_descriptor_header *h = (void *)buf;

		/* ignore any misplaced descriptors */
		if (h->bDescriptorType != USB_DT_CS_INTERFACE)
			goto next_desc;

		/* buf[2] is CDC descriptor subtype */
		switch (buf[2]) {
		case USB_CDC_HEADER_TYPE:
			if (found & 1 << USB_CDC_HEADER_TYPE) {
				dev_dbg(&intf->dev, "extra CDC header\n");
				goto err;
			}
			if (h->bLength != sizeof(struct usb_cdc_header_desc)) {
				dev_dbg(&intf->dev, "CDC header len %u\n",
					h->bLength);
				goto err;
			}
			break;
		case USB_CDC_UNION_TYPE:
			if (found & 1 << USB_CDC_UNION_TYPE) {
				dev_dbg(&intf->dev, "extra CDC union\n");
				goto err;
			}
			if (h->bLength != sizeof(struct usb_cdc_union_desc)) {
				dev_dbg(&intf->dev, "CDC union len %u\n",
					h->bLength);
				goto err;
			}
			cdc_union = (struct usb_cdc_union_desc *)buf;
			break;
		case USB_CDC_ETHERNET_TYPE:
			if (found & 1 << USB_CDC_ETHERNET_TYPE) {
				dev_dbg(&intf->dev, "extra CDC ether\n");
				goto err;
			}
			if (h->bLength != sizeof(struct usb_cdc_ether_desc)) {
				dev_dbg(&intf->dev, "CDC ether len %u\n",
					h->bLength);
				goto err;
			}
			cdc_ether = (struct usb_cdc_ether_desc *)buf;
			break;
		}

		/* Remember which CDC functional descriptors we've seen.  Works
		 * for all types we care about, of which USB_CDC_ETHERNET_TYPE
		 * (0x0f) is the highest numbered
		 */
		if (buf[2] < 32)
			found |= 1 << buf[2];

next_desc:
		len -= h->bLength;
		buf += h->bLength;
	}

	/* Use separate control and data interfaces if we found a CDC Union */
	if (cdc_union) {
		info->data = usb_ifnum_to_if(dev->udev,
					     cdc_union->bSlaveInterface0);
		if (desc->bInterfaceNumber != cdc_union->bMasterInterface0 ||
		    !info->data) {
			dev_err(&intf->dev,
				"bogus CDC Union: master=%u, slave=%u\n",
				cdc_union->bMasterInterface0,
				cdc_union->bSlaveInterface0);
			goto err;
		}
	}

	/* errors aren't fatal - we can live with the dynamic address */
	if (cdc_ether) {
		dev->hard_mtu = le16_to_cpu(cdc_ether->wMaxSegmentSize);
		usbnet_get_ethernet_addr(dev, cdc_ether->iMACAddress);
	}

	/* claim data interface and set it up */
	if (info->control != info->data) {
		status = usb_driver_claim_interface(driver, info->data, dev);
		if (status < 0)
			goto err;
	}

	status = qmi_wwan_register_subdriver(dev);
	if (status < 0 && info->control != info->data) {
		usb_set_intfdata(info->data, NULL);
		usb_driver_release_interface(driver, info->data);
	}

	/* Never use the same address on both ends of the link, even
	 * if the buggy firmware told us to.
	 */
	if (ether_addr_equal(dev->net->dev_addr, default_modem_addr))
		eth_hw_addr_random(dev->net);

	/* make MAC addr easily distinguishable from an IP header */
	if (possibly_iphdr(dev->net->dev_addr)) {
		dev->net->dev_addr[0] |= 0x02;	/* set local assignment bit */
		dev->net->dev_addr[0] &= 0xbf;	/* clear "IP" bit */
	}
	dev->net->netdev_ops = &qmi_wwan_netdev_ops;
err:
	return status;
}

static void qmi_wwan_unbind(struct usbnet *dev, struct usb_interface *intf)
{
	struct qmi_wwan_state *info = (void *)&dev->data;
	struct usb_driver *driver = driver_of(intf);
	struct usb_interface *other;

	if (info->subdriver && info->subdriver->disconnect)
		info->subdriver->disconnect(info->control);

	/* allow user to unbind using either control or data */
	if (intf == info->control)
		other = info->data;
	else
		other = info->control;

	/* only if not shared */
	if (other && intf != other) {
		usb_set_intfdata(other, NULL);
		usb_driver_release_interface(driver, other);
	}

	info->subdriver = NULL;
	info->data = NULL;
	info->control = NULL;
}

/* suspend/resume wrappers calling both usbnet and the cdc-wdm
 * subdriver if present.
 *
 * NOTE: cdc-wdm also supports pre/post_reset, but we cannot provide
 * wrappers for those without adding usbnet reset support first.
 */
static int qmi_wwan_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct usbnet *dev = usb_get_intfdata(intf);
	struct qmi_wwan_state *info = (void *)&dev->data;
	int ret;

	/* Both usbnet_suspend() and subdriver->suspend() MUST return 0
	 * in system sleep context, otherwise, the resume callback has
	 * to recover device from previous suspend failure.
	 */
	ret = usbnet_suspend(intf, message);
	if (ret < 0)
		goto err;

	if (intf == info->control && info->subdriver &&
	    info->subdriver->suspend)
		ret = info->subdriver->suspend(intf, message);
	if (ret < 0)
		usbnet_resume(intf);
err:
	return ret;
}

static int qmi_wwan_resume(struct usb_interface *intf)
{
	struct usbnet *dev = usb_get_intfdata(intf);
	struct qmi_wwan_state *info = (void *)&dev->data;
	int ret = 0;
	bool callsub = (intf == info->control && info->subdriver &&
			info->subdriver->resume);

	if (callsub)
		ret = info->subdriver->resume(intf);
	if (ret < 0)
		goto err;
	ret = usbnet_resume(intf);
	if (ret < 0 && callsub)
		info->subdriver->suspend(intf, PMSG_SUSPEND);
err:
	return ret;
}

static const struct driver_info	qmi_wwan_info = {
	.description	= "WWAN/QMI device",
	.flags		= FLAG_WWAN,
	.bind		= qmi_wwan_bind,
	.unbind		= qmi_wwan_unbind,
	.manage_power	= qmi_wwan_manage_power,
	.rx_fixup       = qmi_wwan_rx_fixup,
};

#define HUAWEI_VENDOR_ID	0x12D1

/* map QMI/wwan function by a fixed interface number */
#define QMI_FIXED_INTF(vend, prod, num) \
	USB_DEVICE_INTERFACE_NUMBER(vend, prod, num), \
	.driver_info = (unsigned long)&qmi_wwan_info

/* Gobi 1000 QMI/wwan interface number is 3 according to qcserial */
#define QMI_GOBI1K_DEVICE(vend, prod) \
	QMI_FIXED_INTF(vend, prod, 3)

/* Gobi 2000/3000 QMI/wwan interface number is 0 according to qcserial */
#define QMI_GOBI_DEVICE(vend, prod) \
	QMI_FIXED_INTF(vend, prod, 0)

static const struct usb_device_id products[] = {
	/* 1. CDC ECM like devices match on the control interface */
	{	/* Huawei E392, E398 and possibly others sharing both device id and more... */
		USB_VENDOR_AND_INTERFACE_INFO(HUAWEI_VENDOR_ID, USB_CLASS_VENDOR_SPEC, 1, 9),
		.driver_info        = (unsigned long)&qmi_wwan_info,
	},
	{	/* Vodafone/Huawei K5005 (12d1:14c8) and similar modems */
		USB_VENDOR_AND_INTERFACE_INFO(HUAWEI_VENDOR_ID, USB_CLASS_VENDOR_SPEC, 1, 57),
		.driver_info        = (unsigned long)&qmi_wwan_info,
	},
	{	/* HUAWEI_INTERFACE_NDIS_CONTROL_QUALCOMM */
		USB_VENDOR_AND_INTERFACE_INFO(HUAWEI_VENDOR_ID, USB_CLASS_VENDOR_SPEC, 0x01, 0x69),
		.driver_info        = (unsigned long)&qmi_wwan_info,
	},

	/* 2. Combined interface devices matching on class+protocol */
	{	/* Huawei E367 and possibly others in "Windows mode" */
		USB_VENDOR_AND_INTERFACE_INFO(HUAWEI_VENDOR_ID, USB_CLASS_VENDOR_SPEC, 1, 7),
		.driver_info        = (unsigned long)&qmi_wwan_info,
	},
	{	/* Huawei E392, E398 and possibly others in "Windows mode" */
		USB_VENDOR_AND_INTERFACE_INFO(HUAWEI_VENDOR_ID, USB_CLASS_VENDOR_SPEC, 1, 17),
		.driver_info        = (unsigned long)&qmi_wwan_info,
	},
	{	/* HUAWEI_NDIS_SINGLE_INTERFACE_VDF */
		USB_VENDOR_AND_INTERFACE_INFO(HUAWEI_VENDOR_ID, USB_CLASS_VENDOR_SPEC, 0x01, 0x37),
		.driver_info        = (unsigned long)&qmi_wwan_info,
	},
	{	/* HUAWEI_INTERFACE_NDIS_HW_QUALCOMM */
		USB_VENDOR_AND_INTERFACE_INFO(HUAWEI_VENDOR_ID, USB_CLASS_VENDOR_SPEC, 0x01, 0x67),
		.driver_info        = (unsigned long)&qmi_wwan_info,
	},
	{	/* Pantech UML290, P4200 and more */
		USB_VENDOR_AND_INTERFACE_INFO(0x106c, USB_CLASS_VENDOR_SPEC, 0xf0, 0xff),
		.driver_info        = (unsigned long)&qmi_wwan_info,
	},
	{	/* Pantech UML290 - newer firmware */
		USB_VENDOR_AND_INTERFACE_INFO(0x106c, USB_CLASS_VENDOR_SPEC, 0xf1, 0xff),
		.driver_info        = (unsigned long)&qmi_wwan_info,
	},
	{	/* Novatel USB551L and MC551 */
		USB_DEVICE_AND_INTERFACE_INFO(0x1410, 0xb001,
		                              USB_CLASS_COMM,
		                              USB_CDC_SUBCLASS_ETHERNET,
		                              USB_CDC_PROTO_NONE),
		.driver_info        = (unsigned long)&qmi_wwan_info,
	},
	{	/* Novatel E362 */
		USB_DEVICE_AND_INTERFACE_INFO(0x1410, 0x9010,
		                              USB_CLASS_COMM,
		                              USB_CDC_SUBCLASS_ETHERNET,
		                              USB_CDC_PROTO_NONE),
		.driver_info        = (unsigned long)&qmi_wwan_info,
	},
	{	/* Novatel Expedite E371 */
		USB_DEVICE_AND_INTERFACE_INFO(0x1410, 0x9011,
		                              USB_CLASS_COMM,
		                              USB_CDC_SUBCLASS_ETHERNET,
		                              USB_CDC_PROTO_NONE),
		.driver_info        = (unsigned long)&qmi_wwan_info,
	},
	{	/* Dell Wireless 5800 (Novatel E362) */
		USB_DEVICE_AND_INTERFACE_INFO(0x413C, 0x8195,
					      USB_CLASS_COMM,
					      USB_CDC_SUBCLASS_ETHERNET,
					      USB_CDC_PROTO_NONE),
		.driver_info        = (unsigned long)&qmi_wwan_info,
	},
	{	/* Dell Wireless 5800 V2 (Novatel E362) */
		USB_DEVICE_AND_INTERFACE_INFO(0x413C, 0x8196,
					      USB_CLASS_COMM,
					      USB_CDC_SUBCLASS_ETHERNET,
					      USB_CDC_PROTO_NONE),
		.driver_info        = (unsigned long)&qmi_wwan_info,
	},
	{	/* Dell Wireless 5804 (Novatel E371) */
		USB_DEVICE_AND_INTERFACE_INFO(0x413C, 0x819b,
					      USB_CLASS_COMM,
					      USB_CDC_SUBCLASS_ETHERNET,
					      USB_CDC_PROTO_NONE),
		.driver_info        = (unsigned long)&qmi_wwan_info,
	},
	{	/* ADU960S */
		USB_DEVICE_AND_INTERFACE_INFO(0x16d5, 0x650a,
					      USB_CLASS_COMM,
					      USB_CDC_SUBCLASS_ETHERNET,
					      USB_CDC_PROTO_NONE),
		.driver_info        = (unsigned long)&qmi_wwan_info,
	},

	/* 3. Combined interface devices matching on interface number */
	{QMI_FIXED_INTF(0x0408, 0xea42, 4)},	/* Yota / Megafon M100-1 */
	{QMI_FIXED_INTF(0x05c6, 0x7000, 0)},
	{QMI_FIXED_INTF(0x05c6, 0x7001, 1)},
	{QMI_FIXED_INTF(0x05c6, 0x7002, 1)},
	{QMI_FIXED_INTF(0x05c6, 0x7101, 1)},
	{QMI_FIXED_INTF(0x05c6, 0x7101, 2)},
	{QMI_FIXED_INTF(0x05c6, 0x7101, 3)},
	{QMI_FIXED_INTF(0x05c6, 0x7102, 1)},
	{QMI_FIXED_INTF(0x05c6, 0x7102, 2)},
	{QMI_FIXED_INTF(0x05c6, 0x7102, 3)},
	{QMI_FIXED_INTF(0x05c6, 0x8000, 7)},
	{QMI_FIXED_INTF(0x05c6, 0x8001, 6)},
	{QMI_FIXED_INTF(0x05c6, 0x9000, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x9003, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x9005, 2)},
	{QMI_FIXED_INTF(0x05c6, 0x900a, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x900b, 2)},
	{QMI_FIXED_INTF(0x05c6, 0x900c, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x900c, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x900c, 6)},
	{QMI_FIXED_INTF(0x05c6, 0x900d, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x900f, 3)},
	{QMI_FIXED_INTF(0x05c6, 0x900f, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x900f, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9010, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x9010, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9011, 3)},
	{QMI_FIXED_INTF(0x05c6, 0x9011, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x9021, 1)},
	{QMI_FIXED_INTF(0x05c6, 0x9022, 2)},
	{QMI_FIXED_INTF(0x05c6, 0x9025, 4)},	/* Alcatel-sbell ASB TL131 TDD LTE  (China Mobile) */
	{QMI_FIXED_INTF(0x05c6, 0x9026, 3)},
	{QMI_FIXED_INTF(0x05c6, 0x902e, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9031, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9032, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x9033, 3)},
	{QMI_FIXED_INTF(0x05c6, 0x9033, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x9033, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9033, 6)},
	{QMI_FIXED_INTF(0x05c6, 0x9034, 3)},
	{QMI_FIXED_INTF(0x05c6, 0x9034, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x9034, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9034, 6)},
	{QMI_FIXED_INTF(0x05c6, 0x9034, 7)},
	{QMI_FIXED_INTF(0x05c6, 0x9035, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x9036, 3)},
	{QMI_FIXED_INTF(0x05c6, 0x9037, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9038, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x903b, 7)},
	{QMI_FIXED_INTF(0x05c6, 0x903c, 6)},
	{QMI_FIXED_INTF(0x05c6, 0x903d, 6)},
	{QMI_FIXED_INTF(0x05c6, 0x903e, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9043, 3)},
	{QMI_FIXED_INTF(0x05c6, 0x9046, 3)},
	{QMI_FIXED_INTF(0x05c6, 0x9046, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x9046, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9047, 2)},
	{QMI_FIXED_INTF(0x05c6, 0x9047, 3)},
	{QMI_FIXED_INTF(0x05c6, 0x9047, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x9048, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x9048, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9048, 6)},
	{QMI_FIXED_INTF(0x05c6, 0x9048, 7)},
	{QMI_FIXED_INTF(0x05c6, 0x9048, 8)},
	{QMI_FIXED_INTF(0x05c6, 0x904c, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x904c, 6)},
	{QMI_FIXED_INTF(0x05c6, 0x904c, 7)},
	{QMI_FIXED_INTF(0x05c6, 0x904c, 8)},
	{QMI_FIXED_INTF(0x05c6, 0x9050, 3)},
	{QMI_FIXED_INTF(0x05c6, 0x9052, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x9053, 6)},
	{QMI_FIXED_INTF(0x05c6, 0x9053, 7)},
	{QMI_FIXED_INTF(0x05c6, 0x9054, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9054, 6)},
	{QMI_FIXED_INTF(0x05c6, 0x9055, 3)},
	{QMI_FIXED_INTF(0x05c6, 0x9055, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x9055, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9055, 6)},
	{QMI_FIXED_INTF(0x05c6, 0x9055, 7)},
	{QMI_FIXED_INTF(0x05c6, 0x9056, 3)},
	{QMI_FIXED_INTF(0x05c6, 0x9062, 2)},
	{QMI_FIXED_INTF(0x05c6, 0x9062, 3)},
	{QMI_FIXED_INTF(0x05c6, 0x9062, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x9062, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9062, 6)},
	{QMI_FIXED_INTF(0x05c6, 0x9062, 7)},
	{QMI_FIXED_INTF(0x05c6, 0x9062, 8)},
	{QMI_FIXED_INTF(0x05c6, 0x9062, 9)},
	{QMI_FIXED_INTF(0x05c6, 0x9064, 3)},
	{QMI_FIXED_INTF(0x05c6, 0x9065, 6)},
	{QMI_FIXED_INTF(0x05c6, 0x9065, 7)},
	{QMI_FIXED_INTF(0x05c6, 0x9066, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9066, 6)},
	{QMI_FIXED_INTF(0x05c6, 0x9067, 1)},
	{QMI_FIXED_INTF(0x05c6, 0x9068, 2)},
	{QMI_FIXED_INTF(0x05c6, 0x9068, 3)},
	{QMI_FIXED_INTF(0x05c6, 0x9068, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x9068, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9068, 6)},
	{QMI_FIXED_INTF(0x05c6, 0x9068, 7)},
	{QMI_FIXED_INTF(0x05c6, 0x9069, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9069, 6)},
	{QMI_FIXED_INTF(0x05c6, 0x9069, 7)},
	{QMI_FIXED_INTF(0x05c6, 0x9069, 8)},
	{QMI_FIXED_INTF(0x05c6, 0x9070, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x9070, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9075, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9076, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x9076, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9076, 6)},
	{QMI_FIXED_INTF(0x05c6, 0x9076, 7)},
	{QMI_FIXED_INTF(0x05c6, 0x9076, 8)},
	{QMI_FIXED_INTF(0x05c6, 0x9077, 3)},
	{QMI_FIXED_INTF(0x05c6, 0x9077, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x9077, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9077, 6)},
	{QMI_FIXED_INTF(0x05c6, 0x9078, 3)},
	{QMI_FIXED_INTF(0x05c6, 0x9079, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x9079, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9079, 6)},
	{QMI_FIXED_INTF(0x05c6, 0x9079, 7)},
	{QMI_FIXED_INTF(0x05c6, 0x9079, 8)},
	{QMI_FIXED_INTF(0x05c6, 0x9080, 5)},
	{QMI_FIXED_INTF(0x05c6, 0x9080, 6)},
	{QMI_FIXED_INTF(0x05c6, 0x9080, 7)},
	{QMI_FIXED_INTF(0x05c6, 0x9080, 8)},
	{QMI_FIXED_INTF(0x05c6, 0x9083, 3)},
	{QMI_FIXED_INTF(0x05c6, 0x9084, 4)},
	{QMI_FIXED_INTF(0x05c6, 0x920d, 0)},
	{QMI_FIXED_INTF(0x05c6, 0x920d, 5)},
	{QMI_FIXED_INTF(0x0846, 0x68a2, 8)},
	{QMI_FIXED_INTF(0x12d1, 0x140c, 1)},	/* Huawei E173 */
	{QMI_FIXED_INTF(0x12d1, 0x14ac, 1)},	/* Huawei E1820 */
	{QMI_FIXED_INTF(0x16d8, 0x6003, 0)},	/* CMOTech 6003 */
	{QMI_FIXED_INTF(0x16d8, 0x6007, 0)},	/* CMOTech CHE-628S */
	{QMI_FIXED_INTF(0x16d8, 0x6008, 0)},	/* CMOTech CMU-301 */
	{QMI_FIXED_INTF(0x16d8, 0x6280, 0)},	/* CMOTech CHU-628 */
	{QMI_FIXED_INTF(0x16d8, 0x7001, 0)},	/* CMOTech CHU-720S */
	{QMI_FIXED_INTF(0x16d8, 0x7002, 0)},	/* CMOTech 7002 */
	{QMI_FIXED_INTF(0x16d8, 0x7003, 4)},	/* CMOTech CHU-629K */
	{QMI_FIXED_INTF(0x16d8, 0x7004, 3)},	/* CMOTech 7004 */
	{QMI_FIXED_INTF(0x16d8, 0x7006, 5)},	/* CMOTech CGU-629 */
	{QMI_FIXED_INTF(0x16d8, 0x700a, 4)},	/* CMOTech CHU-629S */
	{QMI_FIXED_INTF(0x16d8, 0x7211, 0)},	/* CMOTech CHU-720I */
	{QMI_FIXED_INTF(0x16d8, 0x7212, 0)},	/* CMOTech 7212 */
	{QMI_FIXED_INTF(0x16d8, 0x7213, 0)},	/* CMOTech 7213 */
	{QMI_FIXED_INTF(0x16d8, 0x7251, 1)},	/* CMOTech 7251 */
	{QMI_FIXED_INTF(0x16d8, 0x7252, 1)},	/* CMOTech 7252 */
	{QMI_FIXED_INTF(0x16d8, 0x7253, 1)},	/* CMOTech 7253 */
	{QMI_FIXED_INTF(0x19d2, 0x0002, 1)},
	{QMI_FIXED_INTF(0x19d2, 0x0012, 1)},
	{QMI_FIXED_INTF(0x19d2, 0x0017, 3)},
	{QMI_FIXED_INTF(0x19d2, 0x0019, 3)},	/* ONDA MT689DC */
	{QMI_FIXED_INTF(0x19d2, 0x0021, 4)},
	{QMI_FIXED_INTF(0x19d2, 0x0025, 1)},
	{QMI_FIXED_INTF(0x19d2, 0x0031, 4)},
	{QMI_FIXED_INTF(0x19d2, 0x0042, 4)},
	{QMI_FIXED_INTF(0x19d2, 0x0049, 5)},
	{QMI_FIXED_INTF(0x19d2, 0x0052, 4)},
	{QMI_FIXED_INTF(0x19d2, 0x0055, 1)},	/* ZTE (Vodafone) K3520-Z */
	{QMI_FIXED_INTF(0x19d2, 0x0058, 4)},
	{QMI_FIXED_INTF(0x19d2, 0x0063, 4)},	/* ZTE (Vodafone) K3565-Z */
	{QMI_FIXED_INTF(0x19d2, 0x0104, 4)},	/* ZTE (Vodafone) K4505-Z */
	{QMI_FIXED_INTF(0x19d2, 0x0113, 5)},
	{QMI_FIXED_INTF(0x19d2, 0x0118, 5)},
	{QMI_FIXED_INTF(0x19d2, 0x0121, 5)},
	{QMI_FIXED_INTF(0x19d2, 0x0123, 4)},
	{QMI_FIXED_INTF(0x19d2, 0x0124, 5)},
	{QMI_FIXED_INTF(0x19d2, 0x0125, 6)},
	{QMI_FIXED_INTF(0x19d2, 0x0126, 5)},
	{QMI_FIXED_INTF(0x19d2, 0x0130, 1)},
	{QMI_FIXED_INTF(0x19d2, 0x0133, 3)},
	{QMI_FIXED_INTF(0x19d2, 0x0141, 5)},
	{QMI_FIXED_INTF(0x19d2, 0x0157, 5)},	/* ZTE MF683 */
	{QMI_FIXED_INTF(0x19d2, 0x0158, 3)},
	{QMI_FIXED_INTF(0x19d2, 0x0167, 4)},	/* ZTE MF820D */
	{QMI_FIXED_INTF(0x19d2, 0x0168, 4)},
	{QMI_FIXED_INTF(0x19d2, 0x0176, 3)},
	{QMI_FIXED_INTF(0x19d2, 0x0178, 3)},
	{QMI_FIXED_INTF(0x19d2, 0x0191, 4)},	/* ZTE EuFi890 */
	{QMI_FIXED_INTF(0x19d2, 0x0199, 1)},	/* ZTE MF820S */
	{QMI_FIXED_INTF(0x19d2, 0x0200, 1)},
	{QMI_FIXED_INTF(0x19d2, 0x0257, 3)},	/* ZTE MF821 */
	{QMI_FIXED_INTF(0x19d2, 0x0265, 4)},	/* ONDA MT8205 4G LTE */
	{QMI_FIXED_INTF(0x19d2, 0x0284, 4)},	/* ZTE MF880 */
	{QMI_FIXED_INTF(0x19d2, 0x0326, 4)},	/* ZTE MF821D */
	{QMI_FIXED_INTF(0x19d2, 0x0412, 4)},	/* Telewell TW-LTE 4G */
	{QMI_FIXED_INTF(0x19d2, 0x1008, 4)},	/* ZTE (Vodafone) K3570-Z */
	{QMI_FIXED_INTF(0x19d2, 0x1010, 4)},	/* ZTE (Vodafone) K3571-Z */
	{QMI_FIXED_INTF(0x19d2, 0x1012, 4)},
	{QMI_FIXED_INTF(0x19d2, 0x1018, 3)},	/* ZTE (Vodafone) K5006-Z */
	{QMI_FIXED_INTF(0x19d2, 0x1021, 2)},
	{QMI_FIXED_INTF(0x19d2, 0x1245, 4)},
	{QMI_FIXED_INTF(0x19d2, 0x1247, 4)},
	{QMI_FIXED_INTF(0x19d2, 0x1252, 4)},
	{QMI_FIXED_INTF(0x19d2, 0x1254, 4)},
	{QMI_FIXED_INTF(0x19d2, 0x1255, 3)},
	{QMI_FIXED_INTF(0x19d2, 0x1255, 4)},
	{QMI_FIXED_INTF(0x19d2, 0x1256, 4)},
	{QMI_FIXED_INTF(0x19d2, 0x1270, 5)},	/* ZTE MF667 */
	{QMI_FIXED_INTF(0x19d2, 0x1401, 2)},
	{QMI_FIXED_INTF(0x19d2, 0x1402, 2)},	/* ZTE MF60 */
	{QMI_FIXED_INTF(0x19d2, 0x1424, 2)},
	{QMI_FIXED_INTF(0x19d2, 0x1425, 2)},
	{QMI_FIXED_INTF(0x19d2, 0x1426, 2)},	/* ZTE MF91 */
	{QMI_FIXED_INTF(0x19d2, 0x1428, 2)},	/* Telewell TW-LTE 4G v2 */
	{QMI_FIXED_INTF(0x19d2, 0x2002, 4)},	/* ZTE (Vodafone) K3765-Z */
	{QMI_FIXED_INTF(0x0f3d, 0x68a2, 8)},    /* Sierra Wireless MC7700 */
	{QMI_FIXED_INTF(0x114f, 0x68a2, 8)},    /* Sierra Wireless MC7750 */
	{QMI_FIXED_INTF(0x1199, 0x68a2, 8)},	/* Sierra Wireless MC7710 in QMI mode */
	{QMI_FIXED_INTF(0x1199, 0x68a2, 19)},	/* Sierra Wireless MC7710 in QMI mode */
	{QMI_FIXED_INTF(0x1199, 0x68c0, 8)},	/* Sierra Wireless MC73xx */
	{QMI_FIXED_INTF(0x1199, 0x68c0, 10)},	/* Sierra Wireless MC73xx */
	{QMI_FIXED_INTF(0x1199, 0x901c, 8)},    /* Sierra Wireless EM7700 */
	{QMI_FIXED_INTF(0x1199, 0x901f, 8)},    /* Sierra Wireless EM7355 */
	{QMI_FIXED_INTF(0x1199, 0x9041, 8)},	/* Sierra Wireless MC7305/MC7355 */
	{QMI_FIXED_INTF(0x1199, 0x9051, 8)},	/* Netgear AirCard 340U */
	{QMI_FIXED_INTF(0x1199, 0x9053, 8)},	/* Sierra Wireless Modem */
	{QMI_FIXED_INTF(0x1199, 0x9054, 8)},	/* Sierra Wireless Modem */
	{QMI_FIXED_INTF(0x1199, 0x9055, 8)},	/* Netgear AirCard 341U */
	{QMI_FIXED_INTF(0x1199, 0x9056, 8)},	/* Sierra Wireless Modem */
	{QMI_FIXED_INTF(0x1199, 0x9057, 8)},
	{QMI_FIXED_INTF(0x1199, 0x9061, 8)},	/* Sierra Wireless Modem */
	{QMI_FIXED_INTF(0x1bbb, 0x011e, 4)},	/* Telekom Speedstick LTE II (Alcatel One Touch L100V LTE) */
	{QMI_FIXED_INTF(0x1bbb, 0x0203, 2)},	/* Alcatel L800MA */
	{QMI_FIXED_INTF(0x2357, 0x0201, 4)},	/* TP-LINK HSUPA Modem MA180 */
	{QMI_FIXED_INTF(0x2357, 0x9000, 4)},	/* TP-LINK MA260 */
	{QMI_FIXED_INTF(0x1bc7, 0x1200, 5)},	/* Telit LE920 */
	{QMI_FIXED_INTF(0x1bc7, 0x1201, 2)},	/* Telit LE920 */
	{QMI_FIXED_INTF(0x0b3c, 0xc000, 4)},	/* Olivetti Olicard 100 */
	{QMI_FIXED_INTF(0x0b3c, 0xc001, 4)},	/* Olivetti Olicard 120 */
	{QMI_FIXED_INTF(0x0b3c, 0xc002, 4)},	/* Olivetti Olicard 140 */
	{QMI_FIXED_INTF(0x0b3c, 0xc004, 6)},	/* Olivetti Olicard 155 */
	{QMI_FIXED_INTF(0x0b3c, 0xc005, 6)},	/* Olivetti Olicard 200 */
	{QMI_FIXED_INTF(0x0b3c, 0xc00a, 6)},	/* Olivetti Olicard 160 */
	{QMI_FIXED_INTF(0x0b3c, 0xc00b, 4)},	/* Olivetti Olicard 500 */
	{QMI_FIXED_INTF(0x1e2d, 0x0060, 4)},	/* Cinterion PLxx */
	{QMI_FIXED_INTF(0x1e2d, 0x0053, 4)},	/* Cinterion PHxx,PXxx */
	{QMI_FIXED_INTF(0x413c, 0x81a2, 8)},	/* Dell Wireless 5806 Gobi(TM) 4G LTE Mobile Broadband Card */
	{QMI_FIXED_INTF(0x413c, 0x81a3, 8)},	/* Dell Wireless 5570 HSPA+ (42Mbps) Mobile Broadband Card */
	{QMI_FIXED_INTF(0x413c, 0x81a4, 8)},	/* Dell Wireless 5570e HSPA+ (42Mbps) Mobile Broadband Card */
	{QMI_FIXED_INTF(0x413c, 0x81a8, 8)},	/* Dell Wireless 5808 Gobi(TM) 4G LTE Mobile Broadband Card */
	{QMI_FIXED_INTF(0x413c, 0x81a9, 8)},	/* Dell Wireless 5808e Gobi(TM) 4G LTE Mobile Broadband Card */
	{QMI_FIXED_INTF(0x03f0, 0x581d, 4)},	/* HP lt4112 LTE/HSPA+ Gobi 4G Module (Huawei me906e) */

	/* 4. Gobi 1000 devices */
	{QMI_GOBI1K_DEVICE(0x05c6, 0x9212)},	/* Acer Gobi Modem Device */
	{QMI_GOBI1K_DEVICE(0x03f0, 0x1f1d)},	/* HP un2400 Gobi Modem Device */
	{QMI_GOBI1K_DEVICE(0x04da, 0x250d)},	/* Panasonic Gobi Modem device */
	{QMI_GOBI1K_DEVICE(0x413c, 0x8172)},	/* Dell Gobi Modem device */
	{QMI_GOBI1K_DEVICE(0x1410, 0xa001)},	/* Novatel/Verizon USB-1000 */
	{QMI_GOBI1K_DEVICE(0x1410, 0xa002)},	/* Novatel Gobi Modem device */
	{QMI_GOBI1K_DEVICE(0x1410, 0xa003)},	/* Novatel Gobi Modem device */
	{QMI_GOBI1K_DEVICE(0x1410, 0xa004)},	/* Novatel Gobi Modem device */
	{QMI_GOBI1K_DEVICE(0x1410, 0xa005)},	/* Novatel Gobi Modem device */
	{QMI_GOBI1K_DEVICE(0x1410, 0xa006)},	/* Novatel Gobi Modem device */
	{QMI_GOBI1K_DEVICE(0x1410, 0xa007)},	/* Novatel Gobi Modem device */
	{QMI_GOBI1K_DEVICE(0x0b05, 0x1776)},	/* Asus Gobi Modem device */
	{QMI_GOBI1K_DEVICE(0x19d2, 0xfff3)},	/* ONDA Gobi Modem device */
	{QMI_GOBI1K_DEVICE(0x05c6, 0x9001)},	/* Generic Gobi Modem device */
	{QMI_GOBI1K_DEVICE(0x05c6, 0x9002)},	/* Generic Gobi Modem device */
	{QMI_GOBI1K_DEVICE(0x05c6, 0x9202)},	/* Generic Gobi Modem device */
	{QMI_GOBI1K_DEVICE(0x05c6, 0x9203)},	/* Generic Gobi Modem device */
	{QMI_GOBI1K_DEVICE(0x05c6, 0x9222)},	/* Generic Gobi Modem device */
	{QMI_GOBI1K_DEVICE(0x05c6, 0x9009)},	/* Generic Gobi Modem device */

	/* 5. Gobi 2000 and 3000 devices */
	{QMI_GOBI_DEVICE(0x413c, 0x8186)},	/* Dell Gobi 2000 Modem device (N0218, VU936) */
	{QMI_GOBI_DEVICE(0x413c, 0x8194)},	/* Dell Gobi 3000 Composite */
	{QMI_GOBI_DEVICE(0x05c6, 0x920b)},	/* Generic Gobi 2000 Modem device */
	{QMI_GOBI_DEVICE(0x05c6, 0x9225)},	/* Sony Gobi 2000 Modem device (N0279, VU730) */
	{QMI_GOBI_DEVICE(0x05c6, 0x9245)},	/* Samsung Gobi 2000 Modem device (VL176) */
	{QMI_GOBI_DEVICE(0x03f0, 0x251d)},	/* HP Gobi 2000 Modem device (VP412) */
	{QMI_GOBI_DEVICE(0x05c6, 0x9215)},	/* Acer Gobi 2000 Modem device (VP413) */
	{QMI_GOBI_DEVICE(0x05c6, 0x9265)},	/* Asus Gobi 2000 Modem device (VR305) */
	{QMI_GOBI_DEVICE(0x05c6, 0x9235)},	/* Top Global Gobi 2000 Modem device (VR306) */
	{QMI_GOBI_DEVICE(0x05c6, 0x9275)},	/* iRex Technologies Gobi 2000 Modem device (VR307) */
	{QMI_GOBI_DEVICE(0x0af0, 0x8120)},	/* Option GTM681W */
	{QMI_GOBI_DEVICE(0x1199, 0x68a5)},	/* Sierra Wireless Modem */
	{QMI_GOBI_DEVICE(0x1199, 0x68a9)},	/* Sierra Wireless Modem */
	{QMI_GOBI_DEVICE(0x1199, 0x9001)},	/* Sierra Wireless Gobi 2000 Modem device (VT773) */
	{QMI_GOBI_DEVICE(0x1199, 0x9002)},	/* Sierra Wireless Gobi 2000 Modem device (VT773) */
	{QMI_GOBI_DEVICE(0x1199, 0x9003)},	/* Sierra Wireless Gobi 2000 Modem device (VT773) */
	{QMI_GOBI_DEVICE(0x1199, 0x9004)},	/* Sierra Wireless Gobi 2000 Modem device (VT773) */
	{QMI_GOBI_DEVICE(0x1199, 0x9005)},	/* Sierra Wireless Gobi 2000 Modem device (VT773) */
	{QMI_GOBI_DEVICE(0x1199, 0x9006)},	/* Sierra Wireless Gobi 2000 Modem device (VT773) */
	{QMI_GOBI_DEVICE(0x1199, 0x9007)},	/* Sierra Wireless Gobi 2000 Modem device (VT773) */
	{QMI_GOBI_DEVICE(0x1199, 0x9008)},	/* Sierra Wireless Gobi 2000 Modem device (VT773) */
	{QMI_GOBI_DEVICE(0x1199, 0x9009)},	/* Sierra Wireless Gobi 2000 Modem device (VT773) */
	{QMI_GOBI_DEVICE(0x1199, 0x900a)},	/* Sierra Wireless Gobi 2000 Modem device (VT773) */
	{QMI_GOBI_DEVICE(0x1199, 0x9011)},	/* Sierra Wireless Gobi 2000 Modem device (MC8305) */
	{QMI_GOBI_DEVICE(0x16d8, 0x8002)},	/* CMDTech Gobi 2000 Modem device (VU922) */
	{QMI_GOBI_DEVICE(0x05c6, 0x9205)},	/* Gobi 2000 Modem device */
	{QMI_GOBI_DEVICE(0x1199, 0x9013)},	/* Sierra Wireless Gobi 3000 Modem device (MC8355) */
	{QMI_GOBI_DEVICE(0x03f0, 0x371d)},	/* HP un2430 Mobile Broadband Module */
	{QMI_GOBI_DEVICE(0x1199, 0x9015)},	/* Sierra Wireless Gobi 3000 Modem device */
	{QMI_GOBI_DEVICE(0x1199, 0x9019)},	/* Sierra Wireless Gobi 3000 Modem device */
	{QMI_GOBI_DEVICE(0x1199, 0x901b)},	/* Sierra Wireless MC7770 */
	{QMI_GOBI_DEVICE(0x12d1, 0x14f1)},	/* Sony Gobi 3000 Composite */
	{QMI_GOBI_DEVICE(0x1410, 0xa021)},	/* Foxconn Gobi 3000 Modem device (Novatel E396) */

	{ }					/* END */
};
MODULE_DEVICE_TABLE(usb, products);

static int qmi_wwan_probe(struct usb_interface *intf,
			  const struct usb_device_id *prod)
{
	struct usb_device_id *id = (struct usb_device_id *)prod;

	/* Workaround to enable dynamic IDs.  This disables usbnet
	 * blacklisting functionality.  Which, if required, can be
	 * reimplemented here by using a magic "blacklist" value
	 * instead of 0 in the static device id table
	 */
	if (!id->driver_info) {
		dev_dbg(&intf->dev, "setting defaults for dynamic device id\n");
		id->driver_info = (unsigned long)&qmi_wwan_info;
	}

	return usbnet_probe(intf, id);
}

static struct usb_driver qmi_wwan_driver = {
	.name		      = "qmi_wwan",
	.id_table	      = products,
	.probe		      = qmi_wwan_probe,
	.disconnect	      = usbnet_disconnect,
	.suspend	      = qmi_wwan_suspend,
	.resume		      =	qmi_wwan_resume,
	.reset_resume         = qmi_wwan_resume,
	.supports_autosuspend = 1,
	.disable_hub_initiated_lpm = 1,
};

module_usb_driver(qmi_wwan_driver);

MODULE_AUTHOR("Bjørn Mork <bjorn@mork.no>");
MODULE_DESCRIPTION("Qualcomm MSM Interface (QMI) WWAN driver");
MODULE_LICENSE("GPL");
