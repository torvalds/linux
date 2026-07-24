// SPDX-License-Identifier: GPL-2.0
/*
 * mctp-usblib.c - MCTP-over-USB (DMTF DSP0283) transport helper library
 *
 * DSP0283 is available at:
 * https://www.dmtf.org/sites/default/files/standards/documents/DSP0283_1.0.1.pdf
 *
 * Copyright (C) 2024-2026 Code Construct Pty Ltd
 */

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/usb/mctp-usb.h>
#include <net/mctp.h>

void mctp_usblib_rx_init(struct mctp_usblib_rx *rx)
{
	memset(rx, 0, sizeof(*rx));
}
EXPORT_SYMBOL_GPL(mctp_usblib_rx_init);

void mctp_usblib_rx_fini(struct mctp_usblib_rx *rx)
{
	kfree_skb(rx->skb);
}
EXPORT_SYMBOL_GPL(mctp_usblib_rx_fini);

/*
 * Prepare a transfer buffer for future completion; *bufp and *lenp will
 * be populated on success.
 */
int mctp_usblib_rx_prepare(struct net_device *netdev,
			   struct mctp_usblib_rx *rx,
			   void **bufp, size_t *lenp, gfp_t gfp)
{
	const unsigned int len = MCTP_USB_1_0_XFER_SIZE;
	struct sk_buff *skb;

	skb = __netdev_alloc_skb(netdev, len, gfp);
	if (!skb)
		return -ENOMEM;

	rx->skb = skb;

	*bufp = skb_tail_pointer(skb);
	*lenp = len;

	return 0;
}
EXPORT_SYMBOL_GPL(mctp_usblib_rx_prepare);

static void mctp_usblib_rx(struct net_device *netdev, struct sk_buff *skb)
{
	struct pcpu_dstats *dstats = this_cpu_ptr(netdev->dstats);
	struct mctp_skb_cb *cb;
	unsigned long flags;

	/* we're called from an URB completion handler, and cannot assume local
	 * irqs are always disabled
	 */
	flags = u64_stats_update_begin_irqsave(&dstats->syncp);
	u64_stats_inc(&dstats->rx_packets);
	u64_stats_add(&dstats->rx_bytes, skb->len);
	u64_stats_update_end_irqrestore(&dstats->syncp, flags);

	skb->protocol = htons(ETH_P_MCTP);
	skb_reset_network_header(skb);
	cb = __mctp_cb(skb);
	cb->halen = 0;
	netif_rx(skb);
}

static void mctp_usblib_rx_stats_single_drop(struct net_device *dev)
{
	struct pcpu_dstats *dstats = this_cpu_ptr(dev->dstats);
	unsigned long flags;

	flags = u64_stats_update_begin_irqsave(&dstats->syncp);
	u64_stats_inc(&dstats->rx_drops);
	u64_stats_update_end_irqrestore(&dstats->syncp, flags);
}

/*
 * Receive a USB completion of @len bytes of incoming data. We will then split
 * this into packets and netif_rx() each. Intended to be called in atomic
 * contexts - ie., URB completion.
 *
 * Assumes @netdev uses dstats.
 */
int mctp_usblib_rx_complete(struct net_device *netdev,
			    struct mctp_usblib_rx *rx, size_t len)
{
	struct sk_buff *skb = rx->skb;
	int rc = 0;

	__skb_put(skb, len);

	while (skb) {
		struct sk_buff *skb2 = NULL;
		struct mctp_usb_hdr *hdr;
		/* length of MCTP packet, no USB header */
		u8 pkt_len;

		skb_reset_mac_header(skb);
		hdr = skb_pull_data(skb, sizeof(*hdr));
		if (!hdr) {
			rc = -ENOMSG;
			break;
		}

		if (be16_to_cpu(hdr->id) != MCTP_USB_DMTF_ID) {
			netdev_dbg(netdev, "rx: invalid id %04x\n",
				   be16_to_cpu(hdr->id));
			rc = -EPROTO;
			break;
		}

		if (hdr->len <
		    sizeof(struct mctp_hdr) + sizeof(struct mctp_usb_hdr)) {
			netdev_dbg(netdev, "rx: short packet (hdr) %d\n",
				   hdr->len);
			rc = -EPROTO;
			break;
		}

		/* we know we have at least sizeof(struct mctp_usb_hdr) here */
		pkt_len = hdr->len - sizeof(struct mctp_usb_hdr);
		if (pkt_len > skb->len) {
			rc = -EPROTO;
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
					dev_kfree_skb_any(skb2);
					skb2 = NULL;
				}
			} else {
				mctp_usblib_rx_stats_single_drop(netdev);
			}
			skb_trim(skb, pkt_len);
		}

		mctp_usblib_rx(netdev, skb);
		skb = skb2;
	}

	if (skb)
		dev_kfree_skb_any(skb);

	rx->skb = NULL;

	return rc;
}
EXPORT_SYMBOL_GPL(mctp_usblib_rx_complete);

/*
 * Cancel a rx context; subsequent prepare/complete calls will not be a
 * continuation of any data already received.
 */
void mctp_usblib_rx_cancel(struct mctp_usblib_rx *rx)
{
	dev_kfree_skb_any(rx->skb);
	rx->skb = NULL;
}
EXPORT_SYMBOL_GPL(mctp_usblib_rx_cancel);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jeremy Kerr <jk@codeconstruct.com.au>");
MODULE_DESCRIPTION("MCTP USB transport library");
