// SPDX-License-Identifier: GPL-2.0
/*
 * mctp-usblib.c - MCTP-over-USB (DMTF DSP0283) transport helper library
 *
 * DSP0283 is available at:
 * https://www.dmtf.org/sites/default/files/standards/documents/DSP0283_1.1.0.pdf
 *
 * Copyright (C) 2024-2026 Code Construct Pty Ltd
 */

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/usb/ch9.h>
#include <linux/usb/mctp-usb.h>
#include <net/mctp.h>

int mctp_usblib_rx_init(struct mctp_usblib_rx *rx, u16 ep_pktlen, bool span)
{
	if (!ep_pktlen)
		return -EINVAL;

	if (ep_pktlen & ~USB_ENDPOINT_MAXP_MASK)
		return -EINVAL;

	memset(rx, 0, sizeof(*rx));
	rx->span = span;
	rx->ep_pktlen = ep_pktlen;

	return 0;
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
	struct sk_buff *skb = rx->skb;
	unsigned int len = 0;

	if (skb && skb->len >= MCTP_USB_1_1_PKTLEN_MAX) {
		/* something must have gone terribly wrong. clear and restart */
		mctp_usblib_rx_cancel(rx);
		skb = NULL;
	}

	len = rx->span ? roundup(MCTP_USB_1_1_PKTLEN_MAX, rx->ep_pktlen)
		: MCTP_USB_1_0_XFER_SIZE;

	if (!skb) {
		skb = __netdev_alloc_skb(netdev, len, gfp);
		if (!skb)
			return -ENOMEM;

	} else if (skb->cloned || skb_tailroom(skb) < rx->ep_pktlen) {
		/* We always need to realloc if ->cloned, as we cannot
		 * resubmit the (now-shared) skb buffer for possible DMA.
		 *
		 * Otherwise (if we have an un-cloned SKB): just ensure we
		 * have sufficient space to prevent babble. Since we allocated
		 * for max size in the last prepare (and have not consumed any
		 * of that space for a prior MCTP packet, because !cloned), we
		 * have sufficient data to finish the current MCTP packet.
		 */
		struct sk_buff *skb2;

		skb2 = skb_copy_expand(skb, 0, len, gfp);
		if (!skb2)
			return -ENOMEM;
		dev_kfree_skb_any(skb);
		skb = skb2;
	}

	rx->skb = skb;

	/* Spanning mode allows ZLPs, so we don't require exactly one
	 * transfer packet. If we have extra tailroom, may as well use it,
	 * and we have ensured that the tailroom >= ep_pktlen.
	 */
	if (rx->span)
		len = rounddown(skb_tailroom(skb), rx->ep_pktlen);

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

	skb_reset_mac_header(skb);
	skb_pull(skb, sizeof(struct mctp_usb_hdr));

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

	for (;;) {
		struct mctp_usb_hdr *hdr;
		struct sk_buff *skb2;
		/* length of MCTP packet, including USB header */
		u16 pkt_len;

		/* no header yet, resubmit for the rest of the packet */
		if (skb->len < sizeof(*hdr)) {
			if (!rx->span) {
				netdev_dbg(netdev,
					   "rx: tiny xfer (%d) in non-span mode",
					   skb->len);
				rc = -ENOMSG;
				goto err_reset;
			}
			break;
		}

		hdr = (struct mctp_usb_hdr *)skb->data;

		if (be16_to_cpu(hdr->id) != MCTP_USB_DMTF_ID) {
			/* By resetting here, will start the next IN transfer
			 * at the beginning of the new skb. This will mean
			 * we re-sync when we next see a spanned packet aligned
			 * with the start of a transfer.
			 *
			 * In non-spanning mode, this just means we'll drop
			 * the current transfer only
			 */
			netdev_dbg(netdev, "rx: invalid id %04x\n",
				   be16_to_cpu(hdr->id));
			rc = -EPROTO;
			goto err_reset;
		}

		pkt_len = be16_to_cpu(hdr->len);
		/* v1.1, with span enabled, has a 13-bit length */
		pkt_len &= rx->span ?
			MCTP_USB_1_1_PKTLEN_MAX : MCTP_USB_1_0_PKTLEN_MAX;
		if (pkt_len < sizeof(*hdr) + sizeof(struct mctp_hdr)) {
			netdev_dbg(netdev, "rx: invalid len %d\n", pkt_len);
			rc = -EPROTO;
			goto err_reset;
		}

		/* span continues to the next transfer, resubmit */
		if (pkt_len > skb->len) {
			if (!rx->span) {
				netdev_dbg(netdev,
					   "rx: short xfer (%d vs %d) in non-span mode",
					   pkt_len, skb->len);
				rc = -EPROTO;
				goto err_reset;
			}
			break;
		}

		/* we have (exactly) a complete packet, RX it directly */
		if (pkt_len == skb->len) {
			mctp_usblib_rx(netdev, skb);
			rx->skb = NULL;
			break;
		}

		/* more packets follow - RX a clone so that we can continue
		 * processing the current SKB, which may be the start of a
		 * span.
		 */
		skb2 = skb_clone(skb, GFP_ATOMIC);
		if (skb2) {
			skb_trim(skb2, pkt_len);
			mctp_usblib_rx(netdev, skb2);
		} else {
			mctp_usblib_rx_stats_single_drop(netdev);
		}
		skb_pull(skb, pkt_len);
	}

	return 0;

err_reset:
	dev_kfree_skb_any(rx->skb);
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
	struct sk_buff_head skbs;
	unsigned int buf_len, len;
	enum mctp_usblib_tx_buf_type {
		TX_SINGLE,
		TX_FLAT,
	} buf_type;
	u8 buf[] ____cacheline_aligned;
};

void mctp_usblib_tx_init(struct mctp_usblib_tx *tx,
			 const struct mctp_usblib_tx_ops *ops,
			 void *priv, bool span)
{
	memset(tx, 0, sizeof(*tx));
	tx->ops = *ops;
	tx->priv = priv;
	tx->span = span;
	spin_lock_init(&tx->lock);
}
EXPORT_SYMBOL_GPL(mctp_usblib_tx_init);

static int mctp_usblib_tx_avail(struct mctp_usblib_tx_ctx *ctx)
{
	return ctx->buf_type == TX_SINGLE ? 0 : ctx->buf_len - ctx->len;
}

static bool mctp_usblib_tx_should_send(struct mctp_usblib_tx_ctx *ctx)
{
	/* Use the baseline length (ie, BTU) as an approximate
	 * "reasonably-sized" packet we could expect. If there is
	 * insufficient capacity for that, then send.
	 */
	const size_t pkt_len = MCTP_USB_BTU + sizeof(struct mctp_usb_hdr);

	return mctp_usblib_tx_avail(ctx) < pkt_len;
}

/*
 * Returns zero on success, non-zero on failure - indicating that the new skb
 * could not be appended. So, errors reported here to the TX path will result
 * in the TX being transmitted.
 */
static int mctp_usblib_tx_append(struct mctp_usblib_tx_ctx *ctx,
				 struct sk_buff *skb)
{
	if (ctx->buf_type == TX_SINGLE)
		return -EINVAL;

	if (mctp_usblib_tx_avail(ctx) < skb->len)
		return -ENOBUFS;

	__skb_queue_tail(&ctx->skbs, skb);

	ctx->len += skb->len;

	return 0;
}

static int mctp_usblib_tx_send(struct mctp_usblib_tx_ctx *ctx)
{
	void *buf;

	/* If we have a qlen of 1, we only ended up packing a single skb,
	 * despite allocating for multiple. Skip the copy and send directly
	 * from the skb data.
	 */
	if (ctx->buf_type == TX_SINGLE || ctx->skbs.qlen == 1) {
		buf = ctx->skbs.next->data;

	} else if (ctx->buf_type == TX_FLAT) {
		struct sk_buff *skb;
		size_t pos = 0;

		skb_queue_walk(&ctx->skbs, skb) {
			skb_copy_bits(skb, 0, ctx->buf + pos, skb->len);
			pos += skb->len;
		}

		buf = ctx->buf;
	} else {
		return -EINVAL;
	}

	return ctx->tx->ops.send(ctx, buf, ctx->len);
}

static void mctp_usblib_tx_ctx_free(struct mctp_usblib_tx_ctx *ctx,
				    enum skb_drop_reason reason)
{
	struct sk_buff *skb;

	if (!ctx)
		return;

	while ((skb = __skb_dequeue(&ctx->skbs)) != NULL)
		dev_kfree_skb_any_reason(skb, reason);
	kfree(ctx);
}

void *mctp_usblib_tx_ctx_priv(struct mctp_usblib_tx_ctx *tx_ctx)
{
	return tx_ctx->tx->priv;
}
EXPORT_SYMBOL_GPL(mctp_usblib_tx_ctx_priv);

/* caller must ensure the tx & completion path is quiesced */
void mctp_usblib_tx_fini(struct mctp_usblib_tx *tx)
{
	mctp_usblib_tx_ctx_free(tx->cur_ctx, SKB_DROP_REASON_NOT_SPECIFIED);
}
EXPORT_SYMBOL_GPL(mctp_usblib_tx_fini);

/* Max size of a spanned TX. Since we allocate a separate span buffer, limit
 * the tx-time allocations to 4k. Larger packets will be sent as single
 * transfers.
 */
static const unsigned int TX_SPAN_MAX = 4096 - sizeof(struct mctp_usblib_tx_ctx);

static struct mctp_usblib_tx_ctx *
mctp_usblib_tx_ctx_create(struct mctp_usblib_tx *tx, struct sk_buff *skb,
			  bool single)
{
	enum mctp_usblib_tx_buf_type type;
	struct mctp_usblib_tx_ctx *ctx;
	size_t sz = 0;

	if (single || skb->len > TX_SPAN_MAX) {
		type = TX_SINGLE;
	} else {
		type = TX_FLAT;
		sz = tx->span ? TX_SPAN_MAX : MCTP_USB_1_0_XFER_SIZE;
	}

	ctx = kzalloc_flex(*ctx, buf, sz, GFP_ATOMIC);
	if (!ctx)
		return NULL;

	ctx->tx = tx;
	ctx->buf_type = type;
	ctx->buf_len = sz;
	ctx->len = skb->len;
	skb_queue_head_init(&ctx->skbs);
	__skb_queue_tail(&ctx->skbs, skb);

	return ctx;
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
		u64 n = ctx->skbs.qlen;
		s64 len = ctx->len - (n * sizeof(struct mctp_usb_hdr));

		u64_stats_add(&dstats->tx_packets, n);
		u64_stats_add(&dstats->tx_bytes, len);
	} else {
		u64_stats_add(&dstats->tx_drops, ctx->skbs.qlen);
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
static int mctp_usblib_tx_skb_prepare(struct sk_buff *skb, bool span,
				      enum skb_drop_reason *reason)
{
	unsigned long plen, max_len;
	struct mctp_usb_hdr *hdr;
	int rc;

	max_len = span ? MCTP_USB_1_1_PKTLEN_MAX : MCTP_USB_1_0_PKTLEN_MAX;

	plen = skb->len;
	if (plen + sizeof(*hdr) > max_len) {
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
	hdr->len = cpu_to_be16(plen + sizeof(*hdr));

	return 0;
}

/*
 * Push a new skb to the transfer. May result in zero or more calls to
 * ops->send().
 *
 * Takes ownership of @skb, including on error.
 */
int mctp_usblib_tx_push(struct net_device *dev,
			struct mctp_usblib_tx *tx,
			struct sk_buff *skb, bool more)
{
	struct mctp_usblib_tx_ctx *ctx, *send_ctx = NULL;
	enum skb_drop_reason reason;
	const int max_tries = 3;
	unsigned long flags;
	int try = 1, rc;

	rc = mctp_usblib_tx_skb_prepare(skb, tx->span, &reason);
	if (rc) {
		mctp_usblib_tx_stats_single_drop(dev);
		kfree_skb_reason(skb, reason);
		/* we may still need to proceed, in case an existing ctx
		 * is now sendable (ie.: !more).
		 */
		skb = NULL;
	}

	reason = SKB_DROP_REASON_NOT_SPECIFIED;
retry:
	/* Try and queue to the current context. We exit this critical section
	 * with a few bits of state:
	 *  - send_ctx: indicating a prior context that needs to be sent
	 *  - skb: indicating that a skb still needs to be queued/sent
	 */
	spin_lock_irqsave(&tx->lock, flags);
	ctx = tx->cur_ctx;
	if (ctx) {
		if (skb) {
			rc = mctp_usblib_tx_append(ctx, skb);
			if (rc) {
				/* can't append to the pending tx - detach for
				 * sending, and we'll create a new tx below.
				 */
				swap(tx->cur_ctx, send_ctx);
			} else {
				/* we have queued */
				skb = NULL;
				if (!more || mctp_usblib_tx_should_send(ctx))
					swap(tx->cur_ctx, send_ctx);
			}
		} else if (!more) {
			swap(tx->cur_ctx, send_ctx);
		}
	}
	spin_unlock_irqrestore(&tx->lock, flags);

	if (send_ctx) {
		rc = mctp_usblib_tx_send(send_ctx);
		if (rc) {
			mctp_usblib_tx_stats_update(send_ctx, dev, false);
			mctp_usblib_tx_ctx_free(send_ctx, reason);
		}
		send_ctx = NULL;
	}

	/* we have either queued, or the prepare failed; nothing more to do */
	if (!skb)
		return 0;

	ctx = mctp_usblib_tx_ctx_create(tx, skb, !more);
	if (!ctx) {
		netdev_dbg(dev, "TX context create failed\n");
		mctp_usblib_tx_stats_single_drop(dev);
		kfree_skb(skb);
		return -ENOMEM;
	}

	/* if we're ready to send now, no need to enqueue */
	if (!more || mctp_usblib_tx_should_send(ctx)) {
		rc = mctp_usblib_tx_send(ctx);
		if (rc) {
			mctp_usblib_tx_stats_update(ctx, dev, false);
			mctp_usblib_tx_ctx_free(ctx, reason);
		}
		return 0;
	}

	spin_lock_irqsave(&tx->lock, flags);
	if (!tx->cur_ctx) {
		tx->cur_ctx = ctx;
		ctx = NULL;
	}
	spin_unlock_irqrestore(&tx->lock, flags);

	/* we may have lost the race with a concurrent tx; shouldn't happen, as
	 * ndo_start_xmit should be serialised over one queue, but try again
	 * from the top, as we may be able to queue the skb to that context.
	 */
	if (ctx) {
		/* unlink the new (sole) skb, we don't want it freed with ctx */
		__skb_queue_head_init(&ctx->skbs);
		mctp_usblib_tx_ctx_free(ctx, reason);
		if (++try > max_tries) {
			kfree_skb(skb);
			mctp_usblib_tx_stats_single_drop(dev);
			return -EBUSY;
		}
		goto retry;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mctp_usblib_tx_push);

/* Cancel a tx: any un-sent context is released. */
void mctp_usblib_tx_cancel(struct mctp_usblib_tx *tx, struct net_device *dev,
			   enum skb_drop_reason reason)
{
	struct mctp_usblib_tx_ctx *ctx = NULL;
	unsigned long flags;

	spin_lock_irqsave(&tx->lock, flags);
	swap(tx->cur_ctx, ctx);
	spin_unlock_irqrestore(&tx->lock, flags);

	if (!ctx)
		return;

	mctp_usblib_tx_stats_update(ctx, dev, false);
	mctp_usblib_tx_ctx_free(ctx, reason);
}
EXPORT_SYMBOL_GPL(mctp_usblib_tx_cancel);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jeremy Kerr <jk@codeconstruct.com.au>");
MODULE_DESCRIPTION("MCTP USB transport library");

#if IS_ENABLED(CONFIG_MCTP_TRANSPORT_USBLIB_TEST)
#include "mctp-usblib-test.c"
#endif
