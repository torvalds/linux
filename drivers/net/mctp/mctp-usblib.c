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

/* transmit context: encapsulates one transfer */
struct mctp_usblib_tx_ctx {
	struct mctp_usblib_tx *tx;
	struct sk_buff *skb;
	unsigned int len;
	enum mctp_usblib_tx_buf_type {
		TX_SINGLE,
	} buf_type;
};

void mctp_usblib_tx_init(struct mctp_usblib_tx *tx,
			 const struct mctp_usblib_tx_ops *ops,
			 void *priv)
{
	memset(tx, 0, sizeof(*tx));
	tx->ops = *ops;
	tx->priv = priv;
}
EXPORT_SYMBOL_GPL(mctp_usblib_tx_init);

void mctp_usblib_tx_fini(struct mctp_usblib_tx *tx)
{
}
EXPORT_SYMBOL_GPL(mctp_usblib_tx_fini);

void *mctp_usblib_tx_ctx_priv(struct mctp_usblib_tx_ctx *tx_ctx)
{
	return tx_ctx->tx->priv;
}
EXPORT_SYMBOL_GPL(mctp_usblib_tx_ctx_priv);

static struct mctp_usblib_tx_ctx *
mctp_usblib_tx_ctx_create(struct mctp_usblib_tx *tx, struct sk_buff *skb)
{
	struct mctp_usblib_tx_ctx *ctx;

	ctx = kzalloc_obj(*ctx, GFP_ATOMIC);
	if (!ctx)
		return NULL;

	ctx->tx = tx;
	ctx->buf_type = TX_SINGLE;
	ctx->skb = skb;
	ctx->len += skb->len;

	return ctx;
}

static int mctp_usblib_tx_send(struct mctp_usblib_tx_ctx *ctx)
{
	struct mctp_usblib_tx *tx = ctx->tx;
	void *buf = ctx->skb->data;

	return tx->ops.send(ctx, buf, ctx->len);
}

static void mctp_usblib_tx_ctx_free(struct mctp_usblib_tx_ctx *ctx,
				    enum skb_drop_reason reason)
{
	if (ctx)
		dev_kfree_skb_any_reason(ctx->skb, reason);
	kfree(ctx);
}

static void mctp_usblib_tx_stats_update(struct mctp_usblib_tx_ctx *ctx,
					struct net_device *dev,
					bool ok)
{
	struct pcpu_dstats *dstats = get_cpu_ptr(dev->dstats);
	unsigned long flags;

	flags = u64_stats_update_begin_irqsave(&dstats->syncp);
	if (ok) {
		/* Only include the network-layer data in tx stats; we know
		 * that there is a 4-byte header pushed to all skbs in
		 * tx_skb_prepare()
		 */
		s64 len = ctx->len - sizeof(struct mctp_usb_hdr);

		u64_stats_inc(&dstats->tx_packets);
		u64_stats_add(&dstats->tx_bytes, len);
	} else {
		u64_stats_inc(&dstats->tx_drops);
	}
	u64_stats_update_end_irqrestore(&dstats->syncp, flags);
	put_cpu_ptr(dev->dstats);
}

static void mctp_usblib_tx_stats_single_drop(struct net_device *dev)
{
	struct pcpu_dstats *dstats = get_cpu_ptr(dev->dstats);
	unsigned long flags;

	flags = u64_stats_update_begin_irqsave(&dstats->syncp);
	u64_stats_inc(&dstats->tx_drops);
	u64_stats_update_end_irqrestore(&dstats->syncp, flags);
	put_cpu_ptr(dev->dstats);
}

/*
 * Completion for the ->send() op. This will update netdev stats and
 * free the tx context.
 *
 * Likely called from (atomic) URB completion context.
 */
void mctp_usblib_tx_send_complete(struct mctp_usblib_tx_ctx *tx_ctx,
				  struct net_device *dev, bool ok)
{
	enum skb_drop_reason reason =
		ok ? SKB_CONSUMED : SKB_DROP_REASON_NOT_SPECIFIED;

	mctp_usblib_tx_stats_update(tx_ctx, dev, ok);
	mctp_usblib_tx_ctx_free(tx_ctx, reason);
}
EXPORT_SYMBOL_GPL(mctp_usblib_tx_send_complete);

/* Prepare a skb for push()
 *
 * On error, populates @reason.
 */
static int mctp_usblib_tx_skb_prepare(struct sk_buff *skb,
				      enum skb_drop_reason *reason)
{
	struct mctp_usb_hdr *hdr;
	unsigned long plen;
	int rc;

	plen = skb->len;
	if (plen + sizeof(*hdr) > MCTP_USB_1_0_PKTLEN_MAX) {
		*reason = SKB_DROP_REASON_PKT_TOO_BIG;
		return -EMSGSIZE;
	}

	rc = skb_cow_head(skb, sizeof(*hdr));
	if (rc) {
		*reason = SKB_DROP_REASON_NOMEM;
		return rc;
	}

	hdr = skb_push(skb, sizeof(*hdr));
	if (!hdr) {
		*reason = SKB_DROP_REASON_NOMEM;
		return -ENOMEM;
	}

	hdr->id = cpu_to_be16(MCTP_USB_DMTF_ID);
	hdr->rsvd = 0;
	hdr->len = plen + sizeof(*hdr);

	return 0;
}

/*
 * Push a new skb to the transfer. At present, no send must be in progress,
 * as we only handle single-packet USB transfers.
 *
 * Takes ownership of @skb, including on error.
 */
int mctp_usblib_tx_push(struct net_device *dev,
			struct mctp_usblib_tx *tx,
			struct sk_buff *skb, bool more)
{
	struct mctp_usblib_tx_ctx *ctx;
	enum skb_drop_reason reason;
	int rc;

	if (!skb)
		return 0;

	rc = mctp_usblib_tx_skb_prepare(skb, &reason);
	if (rc)
		goto err_drop_single;

	ctx = mctp_usblib_tx_ctx_create(tx, skb);
	if (!ctx) {
		rc = -ENOMEM;
		reason = SKB_DROP_REASON_NOMEM;
		goto err_drop_single;
	}

	rc = mctp_usblib_tx_send(ctx);
	if (rc) {
		mctp_usblib_tx_stats_update(ctx, dev, false);
		mctp_usblib_tx_ctx_free(ctx, SKB_DROP_REASON_NOT_SPECIFIED);
	}

	return rc;

err_drop_single:
	mctp_usblib_tx_stats_single_drop(dev);
	kfree_skb_reason(skb, reason);
	return rc;
}
EXPORT_SYMBOL_GPL(mctp_usblib_tx_push);

/* Cancel a tx: any un-sent context is released. */
void mctp_usblib_tx_cancel(struct mctp_usblib_tx *tx, struct net_device *dev,
			   enum skb_drop_reason reason)
{
	/* nothing to do at present, no ctx is persistent */
}
EXPORT_SYMBOL_GPL(mctp_usblib_tx_cancel);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jeremy Kerr <jk@codeconstruct.com.au>");
MODULE_DESCRIPTION("MCTP USB transport library");
