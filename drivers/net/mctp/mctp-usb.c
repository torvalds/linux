// SPDX-License-Identifier: GPL-2.0
/*
 * mctp-usb.c - MCTP-over-USB (DMTF DSP0283) transport binding driver.
 *
 * DSP0283 is available at:
 * https://www.dmtf.org/sites/default/files/standards/documents/DSP0283_1.0.1.pdf
 *
 * Copyright (C) 2024-2025 Code Construct Pty Ltd
 */

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/usb.h>
#include <linux/usb/mctp-usb.h>

#include <net/mctp.h>
#include <net/mctpdevice.h>
#include <net/pkt_sched.h>

#include <uapi/linux/if_arp.h>

struct mctp_usb {
	struct usb_device *usbdev;
	struct usb_interface *intf;
	bool stopped;

	struct net_device *netdev;

	u8 ep_in;
	u8 ep_out;

	struct urb *tx_urb;
	struct urb *rx_urb;

	struct delayed_work rx_retry_work;
};

static void mctp_usb_out_complete(struct urb *urb)
{
	struct sk_buff *skb = urb->context;
	struct net_device *netdev = skb->dev;
	int status;

	status = urb->status;

	switch (status) {
	case -ENOENT:
	case -ECONNRESET:
	case -ESHUTDOWN:
	case -EPROTO:
		dev_dstats_tx_dropped(netdev);
		break;
	case 0:
		dev_dstats_tx_add(netdev, skb->len);
		netif_wake_queue(netdev);
		consume_skb(skb);
		return;
	default:
		netdev_dbg(netdev, "unexpected tx urb status: %d\n", status);
		dev_dstats_tx_dropped(netdev);
	}

	kfree_skb(skb);
}

static netdev_tx_t mctp_usb_start_xmit(struct sk_buff *skb,
				       struct net_device *dev)
{
	struct mctp_usb *mctp_usb = netdev_priv(dev);
	struct mctp_usb_hdr *hdr;
	unsigned int plen;
	struct urb *urb;
	int rc;

	plen = skb->len;

	if (plen + sizeof(*hdr) > MCTP_USB_XFER_SIZE)
		goto err_drop;

	rc = skb_cow_head(skb, sizeof(*hdr));
	if (rc)
		goto err_drop;

	hdr = skb_push(skb, sizeof(*hdr));
	if (!hdr)
		goto err_drop;

	hdr->id = cpu_to_be16(MCTP_USB_DMTF_ID);
	hdr->rsvd = 0;
	hdr->len = plen + sizeof(*hdr);

	urb = mctp_usb->tx_urb;

	usb_fill_bulk_urb(urb, mctp_usb->usbdev,
			  usb_sndbulkpipe(mctp_usb->usbdev, mctp_usb->ep_out),
			  skb->data, skb->len,
			  mctp_usb_out_complete, skb);

	rc = usb_submit_urb(urb, GFP_ATOMIC);
	if (rc)
		goto err_drop;
	else
		netif_stop_queue(dev);

	return NETDEV_TX_OK;

err_drop:
	dev_dstats_tx_dropped(dev);
	kfree_skb(skb);
	return NETDEV_TX_OK;
}

static void mctp_usb_in_complete(struct urb *urb);

/* If we fail to queue an in urb atomically (either due to skb allocation or
 * urb submission), we will schedule a rx queue in nonatomic context
 * after a delay, specified in jiffies
 */
static const unsigned long RX_RETRY_DELAY = HZ / 4;

static int mctp_usb_rx_queue(struct mctp_usb *mctp_usb, gfp_t gfp)
{
	struct sk_buff *skb;
	int rc;

	skb = __netdev_alloc_skb(mctp_usb->netdev, MCTP_USB_XFER_SIZE, gfp);
	if (!skb) {
		rc = -ENOMEM;
		goto err_retry;
	}

	usb_fill_bulk_urb(mctp_usb->rx_urb, mctp_usb->usbdev,
			  usb_rcvbulkpipe(mctp_usb->usbdev, mctp_usb->ep_in),
			  skb->data, MCTP_USB_XFER_SIZE,
			  mctp_usb_in_complete, skb);

	rc = usb_submit_urb(mctp_usb->rx_urb, gfp);
	if (rc) {
		netdev_dbg(mctp_usb->netdev, "rx urb submit failure: %d\n", rc);
		kfree_skb(skb);
		if (rc == -ENOMEM)
			goto err_retry;
	}

	return rc;

err_retry:
	schedule_delayed_work(&mctp_usb->rx_retry_work, RX_RETRY_DELAY);
	return rc;
}

static void mctp_usb_in_complete(struct urb *urb)
{
	struct sk_buff *skb = urb->context;
	struct net_device *netdev = skb->dev;
	struct mctp_usb *mctp_usb = netdev_priv(netdev);
	struct mctp_skb_cb *cb;
	unsigned int len;
	int status;

	status = urb->status;

	switch (status) {
	case -ENOENT:
	case -ECONNRESET:
	case -ESHUTDOWN:
	case -EPROTO:
		kfree_skb(skb);
		return;
	case 0:
		break;
	default:
		netdev_dbg(netdev, "unexpected rx urb status: %d\n", status);
		kfree_skb(skb);
		return;
	}

	len = urb->actual_length;
	__skb_put(skb, len);

	while (skb) {
		struct sk_buff *skb2 = NULL;
		struct mctp_usb_hdr *hdr;
		u8 pkt_len; /* length of MCTP packet, no USB header */

		hdr = skb_pull_data(skb, sizeof(*hdr));
		if (!hdr)
			break;

		if (be16_to_cpu(hdr->id) != MCTP_USB_DMTF_ID) {
			netdev_dbg(netdev, "rx: invalid id %04x\n",
				   be16_to_cpu(hdr->id));
			break;
		}

		if (hdr->len <
		    sizeof(struct mctp_hdr) + sizeof(struct mctp_usb_hdr)) {
			netdev_dbg(netdev, "rx: short packet (hdr) %d\n",
				   hdr->len);
			break;
		}

		/* we know we have at least sizeof(struct mctp_usb_hdr) here */
		pkt_len = hdr->len - sizeof(struct mctp_usb_hdr);
		if (pkt_len > skb->len) {
			netdev_dbg(netdev,
				   "rx: short packet (xfer) %d, actual %d\n",
				   hdr->len, skb->len);
			break;
		}

		if (pkt_len < skb->len) {
			/* more packets may follow - clone to a new
			 * skb to use on the next iteration
			 */
			skb2 = skb_clone(skb, GFP_ATOMIC);
			if (skb2) {
				if (!skb_pull(skb2, pkt_len)) {
					kfree_skb(skb2);
					skb2 = NULL;
				}
			}
			skb_trim(skb, pkt_len);
		}

		dev_dstats_rx_add(netdev, skb->len);

		skb->protocol = htons(ETH_P_MCTP);
		skb_reset_network_header(skb);
		cb = __mctp_cb(skb);
		cb->halen = 0;
		netif_rx(skb);

		skb = skb2;
	}

	if (skb)
		kfree_skb(skb);

	mctp_usb_rx_queue(mctp_usb, GFP_ATOMIC);
}

static void mctp_usb_rx_retry_work(struct work_struct *work)
{
	struct mctp_usb *mctp_usb = container_of(work, struct mctp_usb,
						 rx_retry_work.work);

	if (READ_ONCE(mctp_usb->stopped))
		return;

	mctp_usb_rx_queue(mctp_usb, GFP_KERNEL);
}

static int mctp_usb_open(struct net_device *dev)
{
	struct mctp_usb *mctp_usb = netdev_priv(dev);

	WRITE_ONCE(mctp_usb->stopped, false);

	netif_start_queue(dev);

	return mctp_usb_rx_queue(mctp_usb, GFP_KERNEL);
}

static int mctp_usb_stop(struct net_device *dev)
{
	struct mctp_usb *mctp_usb = netdev_priv(dev);

	netif_stop_queue(dev);

	/* prevent RX submission retry */
	WRITE_ONCE(mctp_usb->stopped, true);

	usb_kill_urb(mctp_usb->rx_urb);
	usb_kill_urb(mctp_usb->tx_urb);

	cancel_delayed_work_sync(&mctp_usb->rx_retry_work);

	return 0;
}

static const struct net_device_ops mctp_usb_netdev_ops = {
	.ndo_start_xmit = mctp_usb_start_xmit,
	.ndo_open = mctp_usb_open,
	.ndo_stop = mctp_usb_stop,
};

static void mctp_usb_netdev_setup(struct net_device *dev)
{
	dev->type = ARPHRD_MCTP;

	dev->mtu = MCTP_USB_MTU_MIN;
	dev->min_mtu = MCTP_USB_MTU_MIN;
	dev->max_mtu = MCTP_USB_MTU_MAX;

	dev->hard_header_len = sizeof(struct mctp_usb_hdr);
	dev->tx_queue_len = DEFAULT_TX_QUEUE_LEN;
	dev->flags = IFF_NOARP;
	dev->netdev_ops = &mctp_usb_netdev_ops;
	dev->pcpu_stat_type = NETDEV_PCPU_STAT_DSTATS;
}

static int mctp_usb_probe(struct usb_interface *intf,
			  const struct usb_device_id *id)
{
	struct usb_endpoint_descriptor *ep_in, *ep_out;
	struct usb_host_interface *iface_desc;
	struct net_device *netdev;
	struct mctp_usb *dev;
	int rc;

	/* only one alternate */
	iface_desc = intf->cur_altsetting;

	rc = usb_find_common_endpoints(iface_desc, &ep_in, &ep_out, NULL, NULL);
	if (rc) {
		dev_err(&intf->dev, "invalid endpoints on device?\n");
		return rc;
	}

	netdev = alloc_netdev(sizeof(*dev), "mctpusb%d", NET_NAME_ENUM,
			      mctp_usb_netdev_setup);
	if (!netdev)
		return -ENOMEM;

	SET_NETDEV_DEV(netdev, &intf->dev);
	dev = netdev_priv(netdev);
	dev->netdev = netdev;
	dev->usbdev = usb_get_dev(interface_to_usbdev(intf));
	dev->intf = intf;
	usb_set_intfdata(intf, dev);

	dev->ep_in = ep_in->bEndpointAddress;
	dev->ep_out = ep_out->bEndpointAddress;

	dev->tx_urb = usb_alloc_urb(0, GFP_KERNEL);
	dev->rx_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!dev->tx_urb || !dev->rx_urb) {
		rc = -ENOMEM;
		goto err_free_urbs;
	}

	INIT_DELAYED_WORK(&dev->rx_retry_work, mctp_usb_rx_retry_work);

	rc = mctp_register_netdev(netdev, NULL, MCTP_PHYS_BINDING_USB);
	if (rc)
		goto err_free_urbs;

	return 0;

err_free_urbs:
	usb_free_urb(dev->tx_urb);
	usb_free_urb(dev->rx_urb);
	free_netdev(netdev);
	return rc;
}

static void mctp_usb_disconnect(struct usb_interface *intf)
{
	struct mctp_usb *dev = usb_get_intfdata(intf);

	mctp_unregister_netdev(dev->netdev);
	usb_free_urb(dev->tx_urb);
	usb_free_urb(dev->rx_urb);
	usb_put_dev(dev->usbdev);
	free_netdev(dev->netdev);
}

static const struct usb_device_id mctp_usb_devices[] = {
	{ USB_INTERFACE_INFO(USB_CLASS_MCTP, 0x0, 0x1) },
	{ 0 },
};

MODULE_DEVICE_TABLE(usb, mctp_usb_devices);

static struct usb_driver mctp_usb_driver = {
	.name		= "mctp-usb",
	.id_table	= mctp_usb_devices,
	.probe		= mctp_usb_probe,
	.disconnect	= mctp_usb_disconnect,
};

module_usb_driver(mctp_usb_driver)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jeremy Kerr <jk@codeconstruct.com.au>");
MODULE_DESCRIPTION("MCTP USB transport");
