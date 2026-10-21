// SPDX-License-Identifier: GPL-2.0
/*
 * mctp-usb.c - MCTP-over-USB (DMTF DSP0283) transport binding driver.
 *
 * DSP0283 is available at:
 * https://www.dmtf.org/sites/default/files/standards/documents/DSP0283_1.1.0.pdf
 *
 * Copyright (C) 2024-2026 Code Construct Pty Ltd
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
	bool span;

	struct net_device *netdev;

	u8 ep_in;
	u8 ep_out;

	struct mctp_usblib_rx rx;
	struct urb *rx_urb;
	int in_err_count;
	int in_err_orig;
	bool clear_halt;

	/* enforces atomic access to rx_stopped and requeuing the retry work */
	spinlock_t rx_lock;
	bool rx_stopped;
	struct delayed_work rx_retry_work;

	struct mctp_usblib_tx tx;
	struct usb_anchor tx_anchor;
	/* serialises tx_qmem updates to netdev queue states */
	spinlock_t tx_qmem_lock;
	int tx_qmem;
};

enum {
	MCTP_USB_SUBCLASS_BASE = 0x00,
	MCTP_USB_SUBCLASS_SPAN = 0x02,
};

/* We use a total-size limit for outstanding URBs, as the transfer counts
 * may vary a lot between spanning- and non-spanning modes. In spanning mode,
 * this will allow for a couple of max-sized transfers to be in flight. In
 * non-spanning mode, 32.
 *
 * We want to avoid disabling the tx queue if possible; doing so will end up
 * requeueing to gso_skb, and we only dequeue from that one skb at a time,
 * so can no longer perform transfer packing.
 */
static const unsigned int TX_QMEM_MAX = 16384;

static void mctp_usb_out_complete(struct urb *urb)
{
	struct mctp_usblib_tx_ctx *tx_ctx = urb->context;
	struct mctp_usb *mctp_usb = mctp_usblib_tx_ctx_priv(tx_ctx);
	unsigned int len = urb->transfer_buffer_length;
	struct net_device *netdev = mctp_usb->netdev;
	unsigned long flags;

	mctp_usblib_tx_send_complete(tx_ctx, netdev, urb->status == 0);

	usb_free_urb(urb);

	spin_lock_irqsave(&mctp_usb->tx_qmem_lock, flags);
	mctp_usb->tx_qmem -= len;
	if (mctp_usb->tx_qmem < TX_QMEM_MAX && netif_running(netdev))
		netif_wake_queue(netdev);
	spin_unlock_irqrestore(&mctp_usb->tx_qmem_lock, flags);
}

static int mctp_usb_tx_send(struct mctp_usblib_tx_ctx *tx_ctx,
			    void *data, size_t len)
{
	struct mctp_usb *mctp_usb = mctp_usblib_tx_ctx_priv(tx_ctx);
	unsigned long flags;
	struct urb *urb;
	int rc;

	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb)
		return -ENOMEM;

	usb_fill_bulk_urb(urb, mctp_usb->usbdev,
			  usb_sndbulkpipe(mctp_usb->usbdev, mctp_usb->ep_out),
			  data, len, mctp_usb_out_complete, tx_ctx);

	if (mctp_usb->span)
		urb->transfer_flags |= URB_ZERO_PACKET;

	usb_anchor_urb(urb, &mctp_usb->tx_anchor);

	rc = usb_submit_urb(urb, GFP_ATOMIC);
	if (rc) {
		netdev_dbg(mctp_usb->netdev, "TX urb submit failed, %d\n", rc);
		usb_unanchor_urb(urb);
		usb_free_urb(urb);
	} else {
		spin_lock_irqsave(&mctp_usb->tx_qmem_lock, flags);
		mctp_usb->tx_qmem += len;
		if (mctp_usb->tx_qmem >= TX_QMEM_MAX)
			netif_stop_queue(mctp_usb->netdev);
		spin_unlock_irqrestore(&mctp_usb->tx_qmem_lock, flags);
	}

	return rc;
}

static const struct mctp_usblib_tx_ops tx_ops = {
	.send = mctp_usb_tx_send,
};

static netdev_tx_t mctp_usb_start_xmit(struct sk_buff *skb,
				       struct net_device *dev)
{
	struct mctp_usb *mctp_usb = netdev_priv(dev);
	bool more = netdev_xmit_more();

	mctp_usblib_tx_push(dev, &mctp_usb->tx, skb, more);

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
	unsigned long flags;
	size_t len;
	void *buf;
	int rc;

	rc = mctp_usblib_rx_prepare(mctp_usb->netdev, &mctp_usb->rx,
				    &buf, &len, gfp);
	if (rc)
		goto err_retry;

	usb_fill_bulk_urb(mctp_usb->rx_urb, mctp_usb->usbdev,
			  usb_rcvbulkpipe(mctp_usb->usbdev, mctp_usb->ep_in),
			  buf, len, mctp_usb_in_complete, mctp_usb);

	rc = usb_submit_urb(mctp_usb->rx_urb, gfp);
	if (rc) {
		netdev_dbg(mctp_usb->netdev, "rx urb submit failure: %d\n", rc);
		mctp_usblib_rx_cancel(&mctp_usb->rx);
		if (rc == -ENOMEM)
			goto err_retry;
	}

	return rc;

err_retry:
	spin_lock_irqsave(&mctp_usb->rx_lock, flags);
	if (!mctp_usb->rx_stopped)
		schedule_delayed_work(&mctp_usb->rx_retry_work, RX_RETRY_DELAY);
	spin_unlock_irqrestore(&mctp_usb->rx_lock, flags);
	return 0;
}

static const unsigned int rx_err_max = 10;

/* Returns -1 if we have hit excessive errors, zero otherwise. */
static int mctp_usb_in_urb_err(struct mctp_usb *mctp_usb, int status,
			       bool stalled)
{
	mctp_usblib_rx_cancel(&mctp_usb->rx);

	if (!mctp_usb->in_err_count++)
		mctp_usb->in_err_orig = status;

	if (mctp_usb->in_err_count >= rx_err_max) {
		netdev_err(mctp_usb->netdev,
			   "excessive errors from%s IN EP, first: %d\n",
			   stalled ? " (stalled)" : "",
			   mctp_usb->in_err_orig);
		return -1;
	}

	return 0;
}

static void mctp_usb_in_complete(struct urb *urb)
{
	struct mctp_usb *mctp_usb = urb->context;
	struct net_device *netdev = mctp_usb->netdev;
	unsigned long flags;
	int rc, status;

	status = urb->status;

	switch (status) {
	case -ENOENT:
	case -ECONNRESET:
	case -ESHUTDOWN:
		/* device shutdown, don't resubmit */
		mctp_usblib_rx_cancel(&mctp_usb->rx);
		return;

	case -EPIPE:
		/* endpoint stall: clear halt, which will cause a resubmit */
		rc = mctp_usb_in_urb_err(mctp_usb, status, true);
		if (rc)
			return;

		mctp_usb->clear_halt = true;
		spin_lock_irqsave(&mctp_usb->rx_lock, flags);
		if (!mctp_usb->rx_stopped)
			schedule_delayed_work(&mctp_usb->rx_retry_work,
					      RX_RETRY_DELAY);
		spin_unlock_irqrestore(&mctp_usb->rx_lock, flags);
		return;

	default:
		netdev_dbg(netdev, "unexpected rx urb status: %d\n", status);
		fallthrough;
	case -ETIME:
	case -EPROTO:
	case -EILSEQ:
	case -EOVERFLOW:
		/* possibly transient; record first failure, resubmit */
		rc = mctp_usb_in_urb_err(mctp_usb, status, false);
		if (rc)
			return;
		break;

	case 0:
		mctp_usblib_rx_complete(netdev, &mctp_usb->rx, urb->actual_length);
		mctp_usb->in_err_count = 0;
		break;
	}

	mctp_usb_rx_queue(mctp_usb, GFP_ATOMIC);
}

static void mctp_usb_rx_retry_work(struct work_struct *work)
{
	struct mctp_usb *mctp_usb = container_of(work, struct mctp_usb,
						 rx_retry_work.work);
	unsigned long flags;
	int rc;

	/* We are only called when rx completions are suspended */
	if (mctp_usb->clear_halt) {
		int pipe = usb_rcvbulkpipe(mctp_usb->usbdev, mctp_usb->ep_in);

		rc = usb_clear_halt(mctp_usb->usbdev, pipe);
		if (rc) {
			netdev_err(mctp_usb->netdev,
				   "can't clear IN EP halt: %d\n", rc);

			if (++mctp_usb->in_err_count >= rx_err_max)
				return;

			spin_lock_irqsave(&mctp_usb->rx_lock, flags);
			if (!mctp_usb->rx_stopped)
				schedule_delayed_work(&mctp_usb->rx_retry_work,
						      RX_RETRY_DELAY);
			spin_unlock_irqrestore(&mctp_usb->rx_lock, flags);
			return;
		}
		mctp_usb->clear_halt = false;
	}

	mctp_usb_rx_queue(mctp_usb, GFP_KERNEL);
}

static int mctp_usb_open(struct net_device *dev)
{
	struct mctp_usb *mctp_usb = netdev_priv(dev);

	WRITE_ONCE(mctp_usb->rx_stopped, false);
	mctp_usb->clear_halt = false;
	mctp_usb->in_err_count = 0;

	netif_start_queue(dev);

	return mctp_usb_rx_queue(mctp_usb, GFP_KERNEL);
}

static int mctp_usb_stop(struct net_device *dev)
{
	struct mctp_usb *mctp_usb = netdev_priv(dev);
	unsigned long flags;

	netif_stop_queue(dev);

	/* prevent RX submission retry */
	spin_lock_irqsave(&mctp_usb->rx_lock, flags);
	mctp_usb->rx_stopped = true;
	cancel_delayed_work(&mctp_usb->rx_retry_work);
	spin_unlock_irqrestore(&mctp_usb->rx_lock, flags);

	flush_delayed_work(&mctp_usb->rx_retry_work);

	usb_kill_urb(mctp_usb->rx_urb);

	usb_kill_anchored_urbs(&mctp_usb->tx_anchor);

	mctp_usblib_tx_cancel(&mctp_usb->tx, dev, SKB_DROP_REASON_DEV_READY);
	mctp_usblib_rx_cancel(&mctp_usb->rx);

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
	dev->max_mtu = MCTP_USB_1_0_MTU_MAX;

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
	bool span;
	int rc;

	/* only one alternate */
	iface_desc = intf->cur_altsetting;

	rc = usb_find_common_endpoints(iface_desc, &ep_in, &ep_out, NULL, NULL);
	if (rc) {
		dev_err(&intf->dev, "invalid endpoints on device?\n");
		return rc;
	}

	span = iface_desc->desc.bInterfaceSubClass == MCTP_USB_SUBCLASS_SPAN;

	netdev = alloc_netdev(sizeof(*dev), "mctpusb%d", NET_NAME_ENUM,
			      mctp_usb_netdev_setup);
	if (!netdev)
		return -ENOMEM;

	SET_NETDEV_DEV(netdev, &intf->dev);
	dev = netdev_priv(netdev);
	dev->span = span;
	dev->netdev = netdev;
	dev->usbdev = interface_to_usbdev(intf);
	dev->intf = intf;
	spin_lock_init(&dev->rx_lock);
	if (dev->span)
		netdev->max_mtu = MCTP_USB_1_1_MTU_MAX;
	spin_lock_init(&dev->tx_qmem_lock);
	usb_set_intfdata(intf, dev);

	rc = mctp_usblib_rx_init(&dev->rx, le16_to_cpu(ep_in->wMaxPacketSize),
				 dev->span);
	if (rc)
		goto err_free_netdev;
	mctp_usblib_tx_init(&dev->tx, &tx_ops, dev, dev->span);
	init_usb_anchor(&dev->tx_anchor);

	dev->ep_in = ep_in->bEndpointAddress;
	dev->ep_out = ep_out->bEndpointAddress;

	dev->rx_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!dev->rx_urb) {
		rc = -ENOMEM;
		goto err_fini_rxtx;
	}

	INIT_DELAYED_WORK(&dev->rx_retry_work, mctp_usb_rx_retry_work);

	rc = mctp_register_netdev(netdev, NULL, MCTP_PHYS_BINDING_USB);
	if (rc)
		goto err_free_urb;

	return 0;

err_free_urb:
	usb_free_urb(dev->rx_urb);
err_fini_rxtx:
	mctp_usblib_tx_fini(&dev->tx);
	mctp_usblib_rx_fini(&dev->rx);
err_free_netdev:
	free_netdev(netdev);
	return rc;
}

static void mctp_usb_disconnect(struct usb_interface *intf)
{
	struct mctp_usb *dev = usb_get_intfdata(intf);

	mctp_unregister_netdev(dev->netdev);
	mctp_usblib_rx_fini(&dev->rx);
	mctp_usblib_tx_fini(&dev->tx);
	usb_free_urb(dev->rx_urb);
	free_netdev(dev->netdev);
}

static const struct usb_device_id mctp_usb_devices[] = {
	{ USB_INTERFACE_INFO(USB_CLASS_MCTP, MCTP_USB_SUBCLASS_BASE, 0x1) },
	{ USB_INTERFACE_INFO(USB_CLASS_MCTP, MCTP_USB_SUBCLASS_SPAN, 0x1) },
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
