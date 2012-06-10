/*
 * phonet.c -- USB CDC Phonet host driver
 *
 * Copyright (C) 2008-2009 Nokia Corporation. All rights reserved.
 *
 * Author: Rémi Denis-Courmont
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/gfp.h>
#include <linux/usb.h>
#include <linux/usb/cdc.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/if_phonet.h>
#include <linux/phonet.h>

#define PN_MEDIA_USB	0x1B

static const unsigned rxq_size = 17;

struct usbpn_dev {
	struct net_device	*dev;

	struct usb_interface	*intf, *data_intf;
	struct usb_device	*usb;
	unsigned int		tx_pipe, rx_pipe;
	u8 active_setting;
	u8 disconnected;

	unsigned		tx_queue;
	spinlock_t		tx_lock;

	spinlock_t		rx_lock;
	struct sk_buff		*rx_skb;
	struct urb		*urbs[0];
};

static void tx_complete(struct urb *req);
static void rx_complete(struct urb *req);

/*
 * Network device callbacks
 */
static netdev_tx_t usbpn_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct usbpn_dev *pnd = netdev_priv(dev);
	struct urb *req = NULL;
	unsigned long flags;
	int err;

	if (skb->protocol != htons(ETH_P_PHONET))
		goto drop;

	req = usb_alloc_urb(0, GFP_ATOMIC);
	if (!req)
		goto drop;
	usb_fill_bulk_urb(req, pnd->usb, pnd->tx_pipe, skb->data, skb->len,
				tx_complete, skb);
	req->transfer_flags = URB_ZERO_PACKET;
	err = usb_submit_urb(req, GFP_ATOMIC);
	if (err) {
		usb_free_urb(req);
		goto drop;
	}

	spin_lock_irqsave(&pnd->tx_lock, flags);
	pnd->tx_queue++;
	if (pnd->tx_queue >= dev->tx_queue_len)
		netif_stop_queue(dev);
	spin_unlock_irqrestore(&pnd->tx_lock, flags);
	return NETDEV_TX_OK;

drop:
	dev_kfree_skb(skb);
	dev->stats.tx_dropped++;
	return NETDEV_TX_OK;
}

static void tx_complete(struct urb *req)
{
	struct sk_buff *skb = req->context;
	struct net_device *dev = skb->dev;
	struct usbpn_dev *pnd = netdev_priv(dev);
	int status = req->status;

	switch (status) {
	case 0:
		dev->stats.tx_bytes += skb->len;
		break;

	case -ENOENT:
	case -ECONNRESET:
	case -ESHUTDOWN:
		dev->stats.tx_aborted_errors++;
	default:
		dev->stats.tx_errors++;
		dev_dbg(&dev->dev, "TX error (%d)\n", status);
	}
	dev->stats.tx_packets++;

	spin_lock(&pnd->tx_lock);
	pnd->tx_queue--;
	netif_wake_queue(dev);
	spin_unlock(&pnd->tx_lock);

	dev_kfree_skb_any(skb);
	usb_free_urb(req);
}

static int rx_submit(struct usbpn_dev *pnd, struct urb *req, gfp_t gfp_flags)
{
	struct net_device *dev = pnd->dev;
	struct page *page;
	int err;

	page = alloc_page(gfp_flags);
	if (!page)
		return -ENOMEM;

	usb_fill_bulk_urb(req, pnd->usb, pnd->rx_pipe, page_address(page),
				PAGE_SIZE, rx_complete, dev);
	req->transfer_flags = 0;
	err = usb_submit_urb(req, gfp_flags);
	if (unlikely(err)) {
		dev_dbg(&dev->dev, "RX submit error (%d)\n", err);
		put_page(page);
	}
	return err;
}

static void rx_complete(struct urb *req)
{
	struct net_device *dev = req->context;
	struct usbpn_dev *pnd = netdev_priv(dev);
	struct page *page = virt_to_page(req->transfer_buffer);
	struct sk_buff *skb;
	unsigned long flags;
	int status = req->status;

	switch (status) {
	case 0:
		spin_lock_irqsave(&pnd->rx_lock, flags);
		skb = pnd->rx_skb;
		if (!skb) {
			skb = pnd->rx_skb = netdev_alloc_skb(dev, 12);
			if (likely(skb)) {
				/* Can't use pskb_pull() on page in IRQ */
				memcpy(skb_put(skb, 1), page_address(page), 1);
				skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags,
						page, 1, req->actual_length,
						PAGE_SIZE);
				page = NULL;
			}
		} else {
			skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags,
					page, 0, req->actual_length,
					PAGE_SIZE);
			page = NULL;
		}
		if (req->actual_length < PAGE_SIZE)
			pnd->rx_skb = NULL; /* Last fragment */
		else
			skb = NULL;
		spin_unlock_irqrestore(&pnd->rx_lock, flags);
		if (skb) {
			skb->protocol = htons(ETH_P_PHONET);
			skb_reset_mac_header(skb);
			__skb_pull(skb, 1);
			skb->dev = dev;
			dev->stats.rx_packets++;
			dev->stats.rx_bytes += skb->len;

			netif_rx(skb);
		}
		goto resubmit;

	case -ENOENT:
	case -ECONNRESET:
	case -ESHUTDOWN:
		req = NULL;
		break;

	case -EOVERFLOW:
		dev->stats.rx_over_errors++;
		dev_dbg(&dev->dev, "RX overflow\n");
		break;

	case -EILSEQ:
		dev->stats.rx_crc_errors++;
		break;
	}

	dev->stats.rx_errors++;
resubmit:
	if (page)
		put_page(page);
	if (req)
		rx_submit(pnd, req, GFP_ATOMIC | __GFP_COLD);
}

static int usbpn_close(struct net_device *dev);

static int usbpn_open(struct net_device *dev)
{
	struct usbpn_dev *pnd = netdev_priv(dev);
	int err;
	unsigned i;
	unsigned num = pnd->data_intf->cur_altsetting->desc.bInterfaceNumber;

	err = usb_set_interface(pnd->usb, num, pnd->active_setting);
	if (err)
		return err;

	for (i = 0; i < rxq_size; i++) {
		struct urb *req = usb_alloc_urb(0, GFP_KERNEL);

		if (!req || rx_submit(pnd, req, GFP_KERNEL | __GFP_COLD)) {
			usbpn_close(dev);
			return -ENOMEM;
		}
		pnd->urbs[i] = req;
	}

	netif_wake_queue(dev);
	return 0;
}

static int usbpn_close(struct net_device *dev)
{
	struct usbpn_dev *pnd = netdev_priv(dev);
	unsigned i;
	unsigned num = pnd->data_intf->cur_altsetting->desc.bInterfaceNumber;

	netif_stop_queue(dev);

	for (i = 0; i < rxq_size; i++) {
		struct urb *req = pnd->urbs[i];

		if (!req)
			continue;
		usb_kill_urb(req);
		usb_free_urb(req);
		pnd->urbs[i] = NULL;
	}

	return usb_set_interface(pnd->usb, num, !pnd->active_setting);
}

static int usbpn_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct if_phonet_req *req = (struct if_phonet_req *)ifr;

	switch (cmd) {
	case SIOCPNGAUTOCONF:
		req->ifr_phonet_autoconf.device = PN_DEV_PC;
		return 0;
	}
	return -ENOIOCTLCMD;
}

static int usbpn_set_mtu(struct net_device *dev, int new_mtu)
{
	if ((new_mtu < PHONET_MIN_MTU) || (new_mtu > PHONET_MAX_MTU))
		return -EINVAL;

	dev->mtu = new_mtu;
	return 0;
}

static const struct net_device_ops usbpn_ops = {
	.ndo_open	= usbpn_open,
	.ndo_stop	= usbpn_close,
	.ndo_start_xmit = usbpn_xmit,
	.ndo_do_ioctl	= usbpn_ioctl,
	.ndo_change_mtu = usbpn_set_mtu,
};

static void usbpn_setup(struct net_device *dev)
{
	dev->features		= 0;
	dev->netdev_ops		= &usbpn_ops,
	dev->header_ops		= &phonet_header_ops;
	dev->type		= ARPHRD_PHONET;
	dev->flags		= IFF_POINTOPOINT | IFF_NOARP;
	dev->mtu		= PHONET_MAX_MTU;
	dev->hard_header_len	= 1;
	dev->dev_addr[0]	= PN_MEDIA_USB;
	dev->addr_len		= 1;
	dev->tx_queue_len	= 3;

	dev->destructor		= free_netdev;
}

/*
 * USB driver callbacks
 */
static struct usb_device_id usbpn_ids[] = {
	{
		.match_flags = USB_DEVICE_ID_MATCH_VENDOR
			| USB_DEVICE_ID_MATCH_INT_CLASS
			| USB_DEVICE_ID_MATCH_INT_SUBCLASS,
		.idVendor = 0x0421, /* Nokia */
		.bInterfaceClass = USB_CLASS_COMM,
		.bInterfaceSubClass = 0xFE,
	},
	{ },
};

MODULE_DEVICE_TABLE(usb, usbpn_ids);

static struct usb_driver usbpn_driver;

int usbpn_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	static const char ifname[] = "usbpn%d";
	const struct usb_cdc_union_desc *union_header = NULL;
	const struct usb_host_interface *data_desc;
	struct usb_interface *data_intf;
	struct usb_device *usbdev = interface_to_usbdev(intf);
	struct net_device *dev;
	struct usbpn_dev *pnd;
	u8 *data;
	int phonet = 0;
	int len, err;

	data = intf->altsetting->extra;
	len = intf->altsetting->extralen;
	while (len >= 3) {
		u8 dlen = data[0];
		if (dlen < 3)
			return -EINVAL;

		/* bDescriptorType */
		if (data[1] == USB_DT_CS_INTERFACE) {
			/* bDescriptorSubType */
			switch (data[2]) {
			case USB_CDC_UNION_TYPE:
				if (union_header || dlen < 5)
					break;
				union_header =
					(struct usb_cdc_union_desc *)data;
				break;
			case 0xAB:
				phonet = 1;
				break;
			}
		}
		data += dlen;
		len -= dlen;
	}

	if (!union_header || !phonet)
		return -EINVAL;

	data_intf = usb_ifnum_to_if(usbdev, union_header->bSlaveInterface0);
	if (data_intf == NULL)
		return -ENODEV;
	/* Data interface has one inactive and one active setting */
	if (data_intf->num_altsetting != 2)
		return -EINVAL;
	if (data_intf->altsetting[0].desc.bNumEndpoints == 0 &&
	    data_intf->altsetting[1].desc.bNumEndpoints == 2)
		data_desc = data_intf->altsetting + 1;
	else
	if (data_intf->altsetting[0].desc.bNumEndpoints == 2 &&
	    data_intf->altsetting[1].desc.bNumEndpoints == 0)
		data_desc = data_intf->altsetting;
	else
		return -EINVAL;

	dev = alloc_netdev(sizeof(*pnd) + sizeof(pnd->urbs[0]) * rxq_size,
				ifname, usbpn_setup);
	if (!dev)
		return -ENOMEM;

	pnd = netdev_priv(dev);
	SET_NETDEV_DEV(dev, &intf->dev);

	pnd->dev = dev;
	pnd->usb = usb_get_dev(usbdev);
	pnd->intf = intf;
	pnd->data_intf = data_intf;
	spin_lock_init(&pnd->tx_lock);
	spin_lock_init(&pnd->rx_lock);
	/* Endpoints */
	if (usb_pipein(data_desc->endpoint[0].desc.bEndpointAddress)) {
		pnd->rx_pipe = usb_rcvbulkpipe(usbdev,
			data_desc->endpoint[0].desc.bEndpointAddress);
		pnd->tx_pipe = usb_sndbulkpipe(usbdev,
			data_desc->endpoint[1].desc.bEndpointAddress);
	} else {
		pnd->rx_pipe = usb_rcvbulkpipe(usbdev,
			data_desc->endpoint[1].desc.bEndpointAddress);
		pnd->tx_pipe = usb_sndbulkpipe(usbdev,
			data_desc->endpoint[0].desc.bEndpointAddress);
	}
	pnd->active_setting = data_desc - data_intf->altsetting;

	err = usb_driver_claim_interface(&usbpn_driver, data_intf, pnd);
	if (err)
		goto out;

	/* Force inactive mode until the network device is brought UP */
	usb_set_interface(usbdev, union_header->bSlaveInterface0,
				!pnd->active_setting);
	usb_set_intfdata(intf, pnd);

	err = register_netdev(dev);
	if (err) {
		usb_driver_release_interface(&usbpn_driver, data_intf);
		goto out;
	}

	dev_dbg(&dev->dev, "USB CDC Phonet device found\n");
	return 0;

out:
	usb_set_intfdata(intf, NULL);
	free_netdev(dev);
	return err;
}

static void usbpn_disconnect(struct usb_interface *intf)
{
	struct usbpn_dev *pnd = usb_get_intfdata(intf);
	struct usb_device *usb = pnd->usb;

	if (pnd->disconnected)
		return;

	pnd->disconnected = 1;
	usb_driver_release_interface(&usbpn_driver,
			(pnd->intf == intf) ? pnd->data_intf : pnd->intf);
	unregister_netdev(pnd->dev);
	usb_put_dev(usb);
}

static struct usb_driver usbpn_driver = {
	.name =		"cdc_phonet",
	.probe =	usbpn_probe,
	.disconnect =	usbpn_disconnect,
	.id_table =	usbpn_ids,
	.disable_hub_initiated_lpm = 1,
};

module_usb_driver(usbpn_driver);

MODULE_AUTHOR("Remi Denis-Courmont");
MODULE_DESCRIPTION("USB CDC Phonet host interface");
MODULE_LICENSE("GPL");
