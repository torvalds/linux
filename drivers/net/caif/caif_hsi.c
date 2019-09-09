// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) ST-Ericsson AB 2010
 * Author:  Daniel Martensson
 *	    Dmitry.Tarnyagin  / dmitry.tarnyagin@lockless.no
 */

#define pr_fmt(fmt) KBUILD_MODNAME fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/netdevice.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/if_arp.h>
#include <linux/timer.h>
#include <net/rtnetlink.h>
#include <linux/pkt_sched.h>
#include <net/caif/caif_layer.h>
#include <net/caif/caif_hsi.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Daniel Martensson");
MODULE_DESCRIPTION("CAIF HSI driver");

/* Returns the number of padding bytes for alignment. */
#define PAD_POW2(x, pow) ((((x)&((pow)-1)) == 0) ? 0 :\
				(((pow)-((x)&((pow)-1)))))

static const struct cfhsi_config  hsi_default_config = {

	/* Inactivity timeout on HSI, ms */
	.inactivity_timeout = HZ,

	/* Aggregation timeout (ms) of zero means no aggregation is done*/
	.aggregation_timeout = 1,

	/*
	 * HSI link layer flow-control thresholds.
	 * Threshold values for the HSI packet queue. Flow-control will be
	 * asserted when the number of packets exceeds q_high_mark. It will
	 * not be de-asserted before the number of packets drops below
	 * q_low_mark.
	 * Warning: A high threshold value might increase throughput but it
	 * will at the same time prevent channel prioritization and increase
	 * the risk of flooding the modem. The high threshold should be above
	 * the low.
	 */
	.q_high_mark = 100,
	.q_low_mark = 50,

	/*
	 * HSI padding options.
	 * Warning: must be a base of 2 (& operation used) and can not be zero !
	 */
	.head_align = 4,
	.tail_align = 4,
};

#define ON 1
#define OFF 0

static LIST_HEAD(cfhsi_list);

static void cfhsi_inactivity_tout(struct timer_list *t)
{
	struct cfhsi *cfhsi = from_timer(cfhsi, t, inactivity_timer);

	netdev_dbg(cfhsi->ndev, "%s.\n",
		__func__);

	/* Schedule power down work queue. */
	if (!test_bit(CFHSI_SHUTDOWN, &cfhsi->bits))
		queue_work(cfhsi->wq, &cfhsi->wake_down_work);
}

static void cfhsi_update_aggregation_stats(struct cfhsi *cfhsi,
					   const struct sk_buff *skb,
					   int direction)
{
	struct caif_payload_info *info;
	int hpad, tpad, len;

	info = (struct caif_payload_info *)&skb->cb;
	hpad = 1 + PAD_POW2((info->hdr_len + 1), cfhsi->cfg.head_align);
	tpad = PAD_POW2((skb->len + hpad), cfhsi->cfg.tail_align);
	len = skb->len + hpad + tpad;

	if (direction > 0)
		cfhsi->aggregation_len += len;
	else if (direction < 0)
		cfhsi->aggregation_len -= len;
}

static bool cfhsi_can_send_aggregate(struct cfhsi *cfhsi)
{
	int i;

	if (cfhsi->cfg.aggregation_timeout == 0)
		return true;

	for (i = 0; i < CFHSI_PRIO_BEBK; ++i) {
		if (cfhsi->qhead[i].qlen)
			return true;
	}

	/* TODO: Use aggregation_len instead */
	if (cfhsi->qhead[CFHSI_PRIO_BEBK].qlen >= CFHSI_MAX_PKTS)
		return true;

	return false;
}

static struct sk_buff *cfhsi_dequeue(struct cfhsi *cfhsi)
{
	struct sk_buff *skb;
	int i;

	for (i = 0; i < CFHSI_PRIO_LAST; ++i) {
		skb = skb_dequeue(&cfhsi->qhead[i]);
		if (skb)
			break;
	}

	return skb;
}

static int cfhsi_tx_queue_len(struct cfhsi *cfhsi)
{
	int i, len = 0;
	for (i = 0; i < CFHSI_PRIO_LAST; ++i)
		len += skb_queue_len(&cfhsi->qhead[i]);
	return len;
}

static void cfhsi_abort_tx(struct cfhsi *cfhsi)
{
	struct sk_buff *skb;

	for (;;) {
		spin_lock_bh(&cfhsi->lock);
		skb = cfhsi_dequeue(cfhsi);
		if (!skb)
			break;

		cfhsi->ndev->stats.tx_errors++;
		cfhsi->ndev->stats.tx_dropped++;
		cfhsi_update_aggregation_stats(cfhsi, skb, -1);
		spin_unlock_bh(&cfhsi->lock);
		kfree_skb(skb);
	}
	cfhsi->tx_state = CFHSI_TX_STATE_IDLE;
	if (!test_bit(CFHSI_SHUTDOWN, &cfhsi->bits))
		mod_timer(&cfhsi->inactivity_timer,
			jiffies + cfhsi->cfg.inactivity_timeout);
	spin_unlock_bh(&cfhsi->lock);
}

static int cfhsi_flush_fifo(struct cfhsi *cfhsi)
{
	char buffer[32]; /* Any reasonable value */
	size_t fifo_occupancy;
	int ret;

	netdev_dbg(cfhsi->ndev, "%s.\n",
		__func__);

	do {
		ret = cfhsi->ops->cfhsi_fifo_occupancy(cfhsi->ops,
				&fifo_occupancy);
		if (ret) {
			netdev_warn(cfhsi->ndev,
				"%s: can't get FIFO occupancy: %d.\n",
				__func__, ret);
			break;
		} else if (!fifo_occupancy)
			/* No more data, exitting normally */
			break;

		fifo_occupancy = min(sizeof(buffer), fifo_occupancy);
		set_bit(CFHSI_FLUSH_FIFO, &cfhsi->bits);
		ret = cfhsi->ops->cfhsi_rx(buffer, fifo_occupancy,
				cfhsi->ops);
		if (ret) {
			clear_bit(CFHSI_FLUSH_FIFO, &cfhsi->bits);
			netdev_warn(cfhsi->ndev,
				"%s: can't read data: %d.\n",
				__func__, ret);
			break;
		}

		ret = 5 * HZ;
		ret = wait_event_interruptible_timeout(cfhsi->flush_fifo_wait,
			 !test_bit(CFHSI_FLUSH_FIFO, &cfhsi->bits), ret);

		if (ret < 0) {
			netdev_warn(cfhsi->ndev,
				"%s: can't wait for flush complete: %d.\n",
				__func__, ret);
			break;
		} else if (!ret) {
			ret = -ETIMEDOUT;
			netdev_warn(cfhsi->ndev,
				"%s: timeout waiting for flush complete.\n",
				__func__);
			break;
		}
	} while (1);

	return ret;
}

static int cfhsi_tx_frm(struct cfhsi_desc *desc, struct cfhsi *cfhsi)
{
	int nfrms = 0;
	int pld_len = 0;
	struct sk_buff *skb;
	u8 *pfrm = desc->emb_frm + CFHSI_MAX_EMB_FRM_SZ;

	skb = cfhsi_dequeue(cfhsi);
	if (!skb)
		return 0;

	/* Clear offset. */
	desc->offset = 0;

	/* Check if we can embed a CAIF frame. */
	if (skb->len < CFHSI_MAX_EMB_FRM_SZ) {
		struct caif_payload_info *info;
		int hpad;
		int tpad;

		/* Calculate needed head alignment and tail alignment. */
		info = (struct caif_payload_info *)&skb->cb;

		hpad = 1 + PAD_POW2((info->hdr_len + 1), cfhsi->cfg.head_align);
		tpad = PAD_POW2((skb->len + hpad), cfhsi->cfg.tail_align);

		/* Check if frame still fits with added alignment. */
		if ((skb->len + hpad + tpad) <= CFHSI_MAX_EMB_FRM_SZ) {
			u8 *pemb = desc->emb_frm;
			desc->offset = CFHSI_DESC_SHORT_SZ;
			*pemb = (u8)(hpad - 1);
			pemb += hpad;

			/* Update network statistics. */
			spin_lock_bh(&cfhsi->lock);
			cfhsi->ndev->stats.tx_packets++;
			cfhsi->ndev->stats.tx_bytes += skb->len;
			cfhsi_update_aggregation_stats(cfhsi, skb, -1);
			spin_unlock_bh(&cfhsi->lock);

			/* Copy in embedded CAIF frame. */
			skb_copy_bits(skb, 0, pemb, skb->len);

			/* Consume the SKB */
			consume_skb(skb);
			skb = NULL;
		}
	}

	/* Create payload CAIF frames. */
	while (nfrms < CFHSI_MAX_PKTS) {
		struct caif_payload_info *info;
		int hpad;
		int tpad;

		if (!skb)
			skb = cfhsi_dequeue(cfhsi);

		if (!skb)
			break;

		/* Calculate needed head alignment and tail alignment. */
		info = (struct caif_payload_info *)&skb->cb;

		hpad = 1 + PAD_POW2((info->hdr_len + 1), cfhsi->cfg.head_align);
		tpad = PAD_POW2((skb->len + hpad), cfhsi->cfg.tail_align);

		/* Fill in CAIF frame length in descriptor. */
		desc->cffrm_len[nfrms] = hpad + skb->len + tpad;

		/* Fill head padding information. */
		*pfrm = (u8)(hpad - 1);
		pfrm += hpad;

		/* Update network statistics. */
		spin_lock_bh(&cfhsi->lock);
		cfhsi->ndev->stats.tx_packets++;
		cfhsi->ndev->stats.tx_bytes += skb->len;
		cfhsi_update_aggregation_stats(cfhsi, skb, -1);
		spin_unlock_bh(&cfhsi->lock);

		/* Copy in CAIF frame. */
		skb_copy_bits(skb, 0, pfrm, skb->len);

		/* Update payload length. */
		pld_len += desc->cffrm_len[nfrms];

		/* Update frame pointer. */
		pfrm += skb->len + tpad;

		/* Consume the SKB */
		consume_skb(skb);
		skb = NULL;

		/* Update number of frames. */
		nfrms++;
	}

	/* Unused length fields should be zero-filled (according to SPEC). */
	while (nfrms < CFHSI_MAX_PKTS) {
		desc->cffrm_len[nfrms] = 0x0000;
		nfrms++;
	}

	/* Check if we can piggy-back another descriptor. */
	if (cfhsi_can_send_aggregate(cfhsi))
		desc->header |= CFHSI_PIGGY_DESC;
	else
		desc->header &= ~CFHSI_PIGGY_DESC;

	return CFHSI_DESC_SZ + pld_len;
}

static void cfhsi_start_tx(struct cfhsi *cfhsi)
{
	struct cfhsi_desc *desc = (struct cfhsi_desc *)cfhsi->tx_buf;
	int len, res;

	netdev_dbg(cfhsi->ndev, "%s.\n", __func__);

	if (test_bit(CFHSI_SHUTDOWN, &cfhsi->bits))
		return;

	do {
		/* Create HSI frame. */
		len = cfhsi_tx_frm(desc, cfhsi);
		if (!len) {
			spin_lock_bh(&cfhsi->lock);
			if (unlikely(cfhsi_tx_queue_len(cfhsi))) {
				spin_unlock_bh(&cfhsi->lock);
				res = -EAGAIN;
				continue;
			}
			cfhsi->tx_state = CFHSI_TX_STATE_IDLE;
			/* Start inactivity timer. */
			mod_timer(&cfhsi->inactivity_timer,
				jiffies + cfhsi->cfg.inactivity_timeout);
			spin_unlock_bh(&cfhsi->lock);
			break;
		}

		/* Set up new transfer. */
		res = cfhsi->ops->cfhsi_tx(cfhsi->tx_buf, len, cfhsi->ops);
		if (WARN_ON(res < 0))
			netdev_err(cfhsi->ndev, "%s: TX error %d.\n",
				__func__, res);
	} while (res < 0);
}

static void cfhsi_tx_done(struct cfhsi *cfhsi)
{
	netdev_dbg(cfhsi->ndev, "%s.\n", __func__);

	if (test_bit(CFHSI_SHUTDOWN, &cfhsi->bits))
		return;

	/*
	 * Send flow on if flow off has been previously signalled
	 * and number of packets is below low water mark.
	 */
	spin_lock_bh(&cfhsi->lock);
	if (cfhsi->flow_off_sent &&
			cfhsi_tx_queue_len(cfhsi) <= cfhsi->cfg.q_low_mark &&
			cfhsi->cfdev.flowctrl) {

		cfhsi->flow_off_sent = 0;
		cfhsi->cfdev.flowctrl(cfhsi->ndev, ON);
	}

	if (cfhsi_can_send_aggregate(cfhsi)) {
		spin_unlock_bh(&cfhsi->lock);
		cfhsi_start_tx(cfhsi);
	} else {
		mod_timer(&cfhsi->aggregation_timer,
			jiffies + cfhsi->cfg.aggregation_timeout);
		spin_unlock_bh(&cfhsi->lock);
	}

	return;
}

static void cfhsi_tx_done_cb(struct cfhsi_cb_ops *cb_ops)
{
	struct cfhsi *cfhsi;

	cfhsi = container_of(cb_ops, struct cfhsi, cb_ops);
	netdev_dbg(cfhsi->ndev, "%s.\n",
		__func__);

	if (test_bit(CFHSI_SHUTDOWN, &cfhsi->bits))
		return;
	cfhsi_tx_done(cfhsi);
}

static int cfhsi_rx_desc(struct cfhsi_desc *desc, struct cfhsi *cfhsi)
{
	int xfer_sz = 0;
	int nfrms = 0;
	u16 *plen = NULL;
	u8 *pfrm = NULL;

	if ((desc->header & ~CFHSI_PIGGY_DESC) ||
			(desc->offset > CFHSI_MAX_EMB_FRM_SZ)) {
		netdev_err(cfhsi->ndev, "%s: Invalid descriptor.\n",
			__func__);
		return -EPROTO;
	}

	/* Check for embedded CAIF frame. */
	if (desc->offset) {
		struct sk_buff *skb;
		int len = 0;
		pfrm = ((u8 *)desc) + desc->offset;

		/* Remove offset padding. */
		pfrm += *pfrm + 1;

		/* Read length of CAIF frame (little endian). */
		len = *pfrm;
		len |= ((*(pfrm+1)) << 8) & 0xFF00;
		len += 2;	/* Add FCS fields. */

		/* Sanity check length of CAIF frame. */
		if (unlikely(len > CFHSI_MAX_CAIF_FRAME_SZ)) {
			netdev_err(cfhsi->ndev, "%s: Invalid length.\n",
				__func__);
			return -EPROTO;
		}

		/* Allocate SKB (OK even in IRQ context). */
		skb = alloc_skb(len + 1, GFP_ATOMIC);
		if (!skb) {
			netdev_err(cfhsi->ndev, "%s: Out of memory !\n",
				__func__);
			return -ENOMEM;
		}
		caif_assert(skb != NULL);

		skb_put_data(skb, pfrm, len);

		skb->protocol = htons(ETH_P_CAIF);
		skb_reset_mac_header(skb);
		skb->dev = cfhsi->ndev;

		/*
		 * We are in a callback handler and
		 * unfortunately we don't know what context we're
		 * running in.
		 */
		if (in_interrupt())
			netif_rx(skb);
		else
			netif_rx_ni(skb);

		/* Update network statistics. */
		cfhsi->ndev->stats.rx_packets++;
		cfhsi->ndev->stats.rx_bytes += len;
	}

	/* Calculate transfer length. */
	plen = desc->cffrm_len;
	while (nfrms < CFHSI_MAX_PKTS && *plen) {
		xfer_sz += *plen;
		plen++;
		nfrms++;
	}

	/* Check for piggy-backed descriptor. */
	if (desc->header & CFHSI_PIGGY_DESC)
		xfer_sz += CFHSI_DESC_SZ;

	if ((xfer_sz % 4) || (xfer_sz > (CFHSI_BUF_SZ_RX - CFHSI_DESC_SZ))) {
		netdev_err(cfhsi->ndev,
				"%s: Invalid payload len: %d, ignored.\n",
			__func__, xfer_sz);
		return -EPROTO;
	}
	return xfer_sz;
}

static int cfhsi_rx_desc_len(struct cfhsi_desc *desc)
{
	int xfer_sz = 0;
	int nfrms = 0;
	u16 *plen;

	if ((desc->header & ~CFHSI_PIGGY_DESC) ||
			(desc->offset > CFHSI_MAX_EMB_FRM_SZ)) {

		pr_err("Invalid descriptor. %x %x\n", desc->header,
				desc->offset);
		return -EPROTO;
	}

	/* Calculate transfer length. */
	plen = desc->cffrm_len;
	while (nfrms < CFHSI_MAX_PKTS && *plen) {
		xfer_sz += *plen;
		plen++;
		nfrms++;
	}

	if (xfer_sz % 4) {
		pr_err("Invalid payload len: %d, ignored.\n", xfer_sz);
		return -EPROTO;
	}
	return xfer_sz;
}

static int cfhsi_rx_pld(struct cfhsi_desc *desc, struct cfhsi *cfhsi)
{
	int rx_sz = 0;
	int nfrms = 0;
	u16 *plen = NULL;
	u8 *pfrm = NULL;

	/* Sanity check header and offset. */
	if (WARN_ON((desc->header & ~CFHSI_PIGGY_DESC) ||
			(desc->offset > CFHSI_MAX_EMB_FRM_SZ))) {
		netdev_err(cfhsi->ndev, "%s: Invalid descriptor.\n",
			__func__);
		return -EPROTO;
	}

	/* Set frame pointer to start of payload. */
	pfrm = desc->emb_frm + CFHSI_MAX_EMB_FRM_SZ;
	plen = desc->cffrm_len;

	/* Skip already processed frames. */
	while (nfrms < cfhsi->rx_state.nfrms) {
		pfrm += *plen;
		rx_sz += *plen;
		plen++;
		nfrms++;
	}

	/* Parse payload. */
	while (nfrms < CFHSI_MAX_PKTS && *plen) {
		struct sk_buff *skb;
		u8 *pcffrm = NULL;
		int len;

		/* CAIF frame starts after head padding. */
		pcffrm = pfrm + *pfrm + 1;

		/* Read length of CAIF frame (little endian). */
		len = *pcffrm;
		len |= ((*(pcffrm + 1)) << 8) & 0xFF00;
		len += 2;	/* Add FCS fields. */

		/* Sanity check length of CAIF frames. */
		if (unlikely(len > CFHSI_MAX_CAIF_FRAME_SZ)) {
			netdev_err(cfhsi->ndev, "%s: Invalid length.\n",
				__func__);
			return -EPROTO;
		}

		/* Allocate SKB (OK even in IRQ context). */
		skb = alloc_skb(len + 1, GFP_ATOMIC);
		if (!skb) {
			netdev_err(cfhsi->ndev, "%s: Out of memory !\n",
				__func__);
			cfhsi->rx_state.nfrms = nfrms;
			return -ENOMEM;
		}
		caif_assert(skb != NULL);

		skb_put_data(skb, pcffrm, len);

		skb->protocol = htons(ETH_P_CAIF);
		skb_reset_mac_header(skb);
		skb->dev = cfhsi->ndev;

		/*
		 * We're called in callback from HSI
		 * and don't know the context we're running in.
		 */
		if (in_interrupt())
			netif_rx(skb);
		else
			netif_rx_ni(skb);

		/* Update network statistics. */
		cfhsi->ndev->stats.rx_packets++;
		cfhsi->ndev->stats.rx_bytes += len;

		pfrm += *plen;
		rx_sz += *plen;
		plen++;
		nfrms++;
	}

	return rx_sz;
}

static void cfhsi_rx_done(struct cfhsi *cfhsi)
{
	int res;
	int desc_pld_len = 0, rx_len, rx_state;
	struct cfhsi_desc *desc = NULL;
	u8 *rx_ptr, *rx_buf;
	struct cfhsi_desc *piggy_desc = NULL;

	desc = (struct cfhsi_desc *)cfhsi->rx_buf;

	netdev_dbg(cfhsi->ndev, "%s\n", __func__);

	if (test_bit(CFHSI_SHUTDOWN, &cfhsi->bits))
		return;

	/* Update inactivity timer if pending. */
	spin_lock_bh(&cfhsi->lock);
	mod_timer_pending(&cfhsi->inactivity_timer,
			jiffies + cfhsi->cfg.inactivity_timeout);
	spin_unlock_bh(&cfhsi->lock);

	if (cfhsi->rx_state.state == CFHSI_RX_STATE_DESC) {
		desc_pld_len = cfhsi_rx_desc_len(desc);

		if (desc_pld_len < 0)
			goto out_of_sync;

		rx_buf = cfhsi->rx_buf;
		rx_len = desc_pld_len;
		if (desc_pld_len > 0 && (desc->header & CFHSI_PIGGY_DESC))
			rx_len += CFHSI_DESC_SZ;
		if (desc_pld_len == 0)
			rx_buf = cfhsi->rx_flip_buf;
	} else {
		rx_buf = cfhsi->rx_flip_buf;

		rx_len = CFHSI_DESC_SZ;
		if (cfhsi->rx_state.pld_len > 0 &&
				(desc->header & CFHSI_PIGGY_DESC)) {

			piggy_desc = (struct cfhsi_desc *)
				(desc->emb_frm + CFHSI_MAX_EMB_FRM_SZ +
						cfhsi->rx_state.pld_len);

			cfhsi->rx_state.piggy_desc = true;

			/* Extract payload len from piggy-backed descriptor. */
			desc_pld_len = cfhsi_rx_desc_len(piggy_desc);
			if (desc_pld_len < 0)
				goto out_of_sync;

			if (desc_pld_len > 0) {
				rx_len = desc_pld_len;
				if (piggy_desc->header & CFHSI_PIGGY_DESC)
					rx_len += CFHSI_DESC_SZ;
			}

			/*
			 * Copy needed information from the piggy-backed
			 * descriptor to the descriptor in the start.
			 */
			memcpy(rx_buf, (u8 *)piggy_desc,
					CFHSI_DESC_SHORT_SZ);
		}
	}

	if (desc_pld_len) {
		rx_state = CFHSI_RX_STATE_PAYLOAD;
		rx_ptr = rx_buf + CFHSI_DESC_SZ;
	} else {
		rx_state = CFHSI_RX_STATE_DESC;
		rx_ptr = rx_buf;
		rx_len = CFHSI_DESC_SZ;
	}

	/* Initiate next read */
	if (test_bit(CFHSI_AWAKE, &cfhsi->bits)) {
		/* Set up new transfer. */
		netdev_dbg(cfhsi->ndev, "%s: Start RX.\n",
				__func__);

		res = cfhsi->ops->cfhsi_rx(rx_ptr, rx_len,
				cfhsi->ops);
		if (WARN_ON(res < 0)) {
			netdev_err(cfhsi->ndev, "%s: RX error %d.\n",
				__func__, res);
			cfhsi->ndev->stats.rx_errors++;
			cfhsi->ndev->stats.rx_dropped++;
		}
	}

	if (cfhsi->rx_state.state == CFHSI_RX_STATE_DESC) {
		/* Extract payload from descriptor */
		if (cfhsi_rx_desc(desc, cfhsi) < 0)
			goto out_of_sync;
	} else {
		/* Extract payload */
		if (cfhsi_rx_pld(desc, cfhsi) < 0)
			goto out_of_sync;
		if (piggy_desc) {
			/* Extract any payload in piggyback descriptor. */
			if (cfhsi_rx_desc(piggy_desc, cfhsi) < 0)
				goto out_of_sync;
			/* Mark no embedded frame after extracting it */
			piggy_desc->offset = 0;
		}
	}

	/* Update state info */
	memset(&cfhsi->rx_state, 0, sizeof(cfhsi->rx_state));
	cfhsi->rx_state.state = rx_state;
	cfhsi->rx_ptr = rx_ptr;
	cfhsi->rx_len = rx_len;
	cfhsi->rx_state.pld_len = desc_pld_len;
	cfhsi->rx_state.piggy_desc = desc->header & CFHSI_PIGGY_DESC;

	if (rx_buf != cfhsi->rx_buf)
		swap(cfhsi->rx_buf, cfhsi->rx_flip_buf);
	return;

out_of_sync:
	netdev_err(cfhsi->ndev, "%s: Out of sync.\n", __func__);
	print_hex_dump_bytes("--> ", DUMP_PREFIX_NONE,
			cfhsi->rx_buf, CFHSI_DESC_SZ);
	schedule_work(&cfhsi->out_of_sync_work);
}

static void cfhsi_rx_slowpath(struct timer_list *t)
{
	struct cfhsi *cfhsi = from_timer(cfhsi, t, rx_slowpath_timer);

	netdev_dbg(cfhsi->ndev, "%s.\n",
		__func__);

	cfhsi_rx_done(cfhsi);
}

static void cfhsi_rx_done_cb(struct cfhsi_cb_ops *cb_ops)
{
	struct cfhsi *cfhsi;

	cfhsi = container_of(cb_ops, struct cfhsi, cb_ops);
	netdev_dbg(cfhsi->ndev, "%s.\n",
		__func__);

	if (test_bit(CFHSI_SHUTDOWN, &cfhsi->bits))
		return;

	if (test_and_clear_bit(CFHSI_FLUSH_FIFO, &cfhsi->bits))
		wake_up_interruptible(&cfhsi->flush_fifo_wait);
	else
		cfhsi_rx_done(cfhsi);
}

static void cfhsi_wake_up(struct work_struct *work)
{
	struct cfhsi *cfhsi = NULL;
	int res;
	int len;
	long ret;

	cfhsi = container_of(work, struct cfhsi, wake_up_work);

	if (test_bit(CFHSI_SHUTDOWN, &cfhsi->bits))
		return;

	if (unlikely(test_bit(CFHSI_AWAKE, &cfhsi->bits))) {
		/* It happenes when wakeup is requested by
		 * both ends at the same time. */
		clear_bit(CFHSI_WAKE_UP, &cfhsi->bits);
		clear_bit(CFHSI_WAKE_UP_ACK, &cfhsi->bits);
		return;
	}

	/* Activate wake line. */
	cfhsi->ops->cfhsi_wake_up(cfhsi->ops);

	netdev_dbg(cfhsi->ndev, "%s: Start waiting.\n",
		__func__);

	/* Wait for acknowledge. */
	ret = CFHSI_WAKE_TOUT;
	ret = wait_event_interruptible_timeout(cfhsi->wake_up_wait,
					test_and_clear_bit(CFHSI_WAKE_UP_ACK,
							&cfhsi->bits), ret);
	if (unlikely(ret < 0)) {
		/* Interrupted by signal. */
		netdev_err(cfhsi->ndev, "%s: Signalled: %ld.\n",
			__func__, ret);

		clear_bit(CFHSI_WAKE_UP, &cfhsi->bits);
		cfhsi->ops->cfhsi_wake_down(cfhsi->ops);
		return;
	} else if (!ret) {
		bool ca_wake = false;
		size_t fifo_occupancy = 0;

		/* Wakeup timeout */
		netdev_dbg(cfhsi->ndev, "%s: Timeout.\n",
			__func__);

		/* Check FIFO to check if modem has sent something. */
		WARN_ON(cfhsi->ops->cfhsi_fifo_occupancy(cfhsi->ops,
					&fifo_occupancy));

		netdev_dbg(cfhsi->ndev, "%s: Bytes in FIFO: %u.\n",
				__func__, (unsigned) fifo_occupancy);

		/* Check if we misssed the interrupt. */
		WARN_ON(cfhsi->ops->cfhsi_get_peer_wake(cfhsi->ops,
							&ca_wake));

		if (ca_wake) {
			netdev_err(cfhsi->ndev, "%s: CA Wake missed !.\n",
				__func__);

			/* Clear the CFHSI_WAKE_UP_ACK bit to prevent race. */
			clear_bit(CFHSI_WAKE_UP_ACK, &cfhsi->bits);

			/* Continue execution. */
			goto wake_ack;
		}

		clear_bit(CFHSI_WAKE_UP, &cfhsi->bits);
		cfhsi->ops->cfhsi_wake_down(cfhsi->ops);
		return;
	}
wake_ack:
	netdev_dbg(cfhsi->ndev, "%s: Woken.\n",
		__func__);

	/* Clear power up bit. */
	set_bit(CFHSI_AWAKE, &cfhsi->bits);
	clear_bit(CFHSI_WAKE_UP, &cfhsi->bits);

	/* Resume read operation. */
	netdev_dbg(cfhsi->ndev, "%s: Start RX.\n", __func__);
	res = cfhsi->ops->cfhsi_rx(cfhsi->rx_ptr, cfhsi->rx_len, cfhsi->ops);

	if (WARN_ON(res < 0))
		netdev_err(cfhsi->ndev, "%s: RX err %d.\n", __func__, res);

	/* Clear power up acknowledment. */
	clear_bit(CFHSI_WAKE_UP_ACK, &cfhsi->bits);

	spin_lock_bh(&cfhsi->lock);

	/* Resume transmit if queues are not empty. */
	if (!cfhsi_tx_queue_len(cfhsi)) {
		netdev_dbg(cfhsi->ndev, "%s: Peer wake, start timer.\n",
			__func__);
		/* Start inactivity timer. */
		mod_timer(&cfhsi->inactivity_timer,
				jiffies + cfhsi->cfg.inactivity_timeout);
		spin_unlock_bh(&cfhsi->lock);
		return;
	}

	netdev_dbg(cfhsi->ndev, "%s: Host wake.\n",
		__func__);

	spin_unlock_bh(&cfhsi->lock);

	/* Create HSI frame. */
	len = cfhsi_tx_frm((struct cfhsi_desc *)cfhsi->tx_buf, cfhsi);

	if (likely(len > 0)) {
		/* Set up new transfer. */
		res = cfhsi->ops->cfhsi_tx(cfhsi->tx_buf, len, cfhsi->ops);
		if (WARN_ON(res < 0)) {
			netdev_err(cfhsi->ndev, "%s: TX error %d.\n",
				__func__, res);
			cfhsi_abort_tx(cfhsi);
		}
	} else {
		netdev_err(cfhsi->ndev,
				"%s: Failed to create HSI frame: %d.\n",
				__func__, len);
	}
}

static void cfhsi_wake_down(struct work_struct *work)
{
	long ret;
	struct cfhsi *cfhsi = NULL;
	size_t fifo_occupancy = 0;
	int retry = CFHSI_WAKE_TOUT;

	cfhsi = container_of(work, struct cfhsi, wake_down_work);
	netdev_dbg(cfhsi->ndev, "%s.\n", __func__);

	if (test_bit(CFHSI_SHUTDOWN, &cfhsi->bits))
		return;

	/* Deactivate wake line. */
	cfhsi->ops->cfhsi_wake_down(cfhsi->ops);

	/* Wait for acknowledge. */
	ret = CFHSI_WAKE_TOUT;
	ret = wait_event_interruptible_timeout(cfhsi->wake_down_wait,
					test_and_clear_bit(CFHSI_WAKE_DOWN_ACK,
							&cfhsi->bits), ret);
	if (ret < 0) {
		/* Interrupted by signal. */
		netdev_err(cfhsi->ndev, "%s: Signalled: %ld.\n",
			__func__, ret);
		return;
	} else if (!ret) {
		bool ca_wake = true;

		/* Timeout */
		netdev_err(cfhsi->ndev, "%s: Timeout.\n", __func__);

		/* Check if we misssed the interrupt. */
		WARN_ON(cfhsi->ops->cfhsi_get_peer_wake(cfhsi->ops,
							&ca_wake));
		if (!ca_wake)
			netdev_err(cfhsi->ndev, "%s: CA Wake missed !.\n",
				__func__);
	}

	/* Check FIFO occupancy. */
	while (retry) {
		WARN_ON(cfhsi->ops->cfhsi_fifo_occupancy(cfhsi->ops,
							&fifo_occupancy));

		if (!fifo_occupancy)
			break;

		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(1);
		retry--;
	}

	if (!retry)
		netdev_err(cfhsi->ndev, "%s: FIFO Timeout.\n", __func__);

	/* Clear AWAKE condition. */
	clear_bit(CFHSI_AWAKE, &cfhsi->bits);

	/* Cancel pending RX requests. */
	cfhsi->ops->cfhsi_rx_cancel(cfhsi->ops);
}

static void cfhsi_out_of_sync(struct work_struct *work)
{
	struct cfhsi *cfhsi = NULL;

	cfhsi = container_of(work, struct cfhsi, out_of_sync_work);

	rtnl_lock();
	dev_close(cfhsi->ndev);
	rtnl_unlock();
}

static void cfhsi_wake_up_cb(struct cfhsi_cb_ops *cb_ops)
{
	struct cfhsi *cfhsi = NULL;

	cfhsi = container_of(cb_ops, struct cfhsi, cb_ops);
	netdev_dbg(cfhsi->ndev, "%s.\n",
		__func__);

	set_bit(CFHSI_WAKE_UP_ACK, &cfhsi->bits);
	wake_up_interruptible(&cfhsi->wake_up_wait);

	if (test_bit(CFHSI_SHUTDOWN, &cfhsi->bits))
		return;

	/* Schedule wake up work queue if the peer initiates. */
	if (!test_and_set_bit(CFHSI_WAKE_UP, &cfhsi->bits))
		queue_work(cfhsi->wq, &cfhsi->wake_up_work);
}

static void cfhsi_wake_down_cb(struct cfhsi_cb_ops *cb_ops)
{
	struct cfhsi *cfhsi = NULL;

	cfhsi = container_of(cb_ops, struct cfhsi, cb_ops);
	netdev_dbg(cfhsi->ndev, "%s.\n",
		__func__);

	/* Initiating low power is only permitted by the host (us). */
	set_bit(CFHSI_WAKE_DOWN_ACK, &cfhsi->bits);
	wake_up_interruptible(&cfhsi->wake_down_wait);
}

static void cfhsi_aggregation_tout(struct timer_list *t)
{
	struct cfhsi *cfhsi = from_timer(cfhsi, t, aggregation_timer);

	netdev_dbg(cfhsi->ndev, "%s.\n",
		__func__);

	cfhsi_start_tx(cfhsi);
}

static int cfhsi_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct cfhsi *cfhsi = NULL;
	int start_xfer = 0;
	int timer_active;
	int prio;

	if (!dev)
		return -EINVAL;

	cfhsi = netdev_priv(dev);

	switch (skb->priority) {
	case TC_PRIO_BESTEFFORT:
	case TC_PRIO_FILLER:
	case TC_PRIO_BULK:
		prio = CFHSI_PRIO_BEBK;
		break;
	case TC_PRIO_INTERACTIVE_BULK:
		prio = CFHSI_PRIO_VI;
		break;
	case TC_PRIO_INTERACTIVE:
		prio = CFHSI_PRIO_VO;
		break;
	case TC_PRIO_CONTROL:
	default:
		prio = CFHSI_PRIO_CTL;
		break;
	}

	spin_lock_bh(&cfhsi->lock);

	/* Update aggregation statistics  */
	cfhsi_update_aggregation_stats(cfhsi, skb, 1);

	/* Queue the SKB */
	skb_queue_tail(&cfhsi->qhead[prio], skb);

	/* Sanity check; xmit should not be called after unregister_netdev */
	if (WARN_ON(test_bit(CFHSI_SHUTDOWN, &cfhsi->bits))) {
		spin_unlock_bh(&cfhsi->lock);
		cfhsi_abort_tx(cfhsi);
		return -EINVAL;
	}

	/* Send flow off if number of packets is above high water mark. */
	if (!cfhsi->flow_off_sent &&
		cfhsi_tx_queue_len(cfhsi) > cfhsi->cfg.q_high_mark &&
		cfhsi->cfdev.flowctrl) {
		cfhsi->flow_off_sent = 1;
		cfhsi->cfdev.flowctrl(cfhsi->ndev, OFF);
	}

	if (cfhsi->tx_state == CFHSI_TX_STATE_IDLE) {
		cfhsi->tx_state = CFHSI_TX_STATE_XFER;
		start_xfer = 1;
	}

	if (!start_xfer) {
		/* Send aggregate if it is possible */
		bool aggregate_ready =
			cfhsi_can_send_aggregate(cfhsi) &&
			del_timer(&cfhsi->aggregation_timer) > 0;
		spin_unlock_bh(&cfhsi->lock);
		if (aggregate_ready)
			cfhsi_start_tx(cfhsi);
		return 0;
	}

	/* Delete inactivity timer if started. */
	timer_active = del_timer_sync(&cfhsi->inactivity_timer);

	spin_unlock_bh(&cfhsi->lock);

	if (timer_active) {
		struct cfhsi_desc *desc = (struct cfhsi_desc *)cfhsi->tx_buf;
		int len;
		int res;

		/* Create HSI frame. */
		len = cfhsi_tx_frm(desc, cfhsi);
		WARN_ON(!len);

		/* Set up new transfer. */
		res = cfhsi->ops->cfhsi_tx(cfhsi->tx_buf, len, cfhsi->ops);
		if (WARN_ON(res < 0)) {
			netdev_err(cfhsi->ndev, "%s: TX error %d.\n",
				__func__, res);
			cfhsi_abort_tx(cfhsi);
		}
	} else {
		/* Schedule wake up work queue if the we initiate. */
		if (!test_and_set_bit(CFHSI_WAKE_UP, &cfhsi->bits))
			queue_work(cfhsi->wq, &cfhsi->wake_up_work);
	}

	return 0;
}

static const struct net_device_ops cfhsi_netdevops;

static void cfhsi_setup(struct net_device *dev)
{
	int i;
	struct cfhsi *cfhsi = netdev_priv(dev);
	dev->features = 0;
	dev->type = ARPHRD_CAIF;
	dev->flags = IFF_POINTOPOINT | IFF_NOARP;
	dev->mtu = CFHSI_MAX_CAIF_FRAME_SZ;
	dev->priv_flags |= IFF_NO_QUEUE;
	dev->needs_free_netdev = true;
	dev->netdev_ops = &cfhsi_netdevops;
	for (i = 0; i < CFHSI_PRIO_LAST; ++i)
		skb_queue_head_init(&cfhsi->qhead[i]);
	cfhsi->cfdev.link_select = CAIF_LINK_HIGH_BANDW;
	cfhsi->cfdev.use_frag = false;
	cfhsi->cfdev.use_stx = false;
	cfhsi->cfdev.use_fcs = false;
	cfhsi->ndev = dev;
	cfhsi->cfg = hsi_default_config;
}

static int cfhsi_open(struct net_device *ndev)
{
	struct cfhsi *cfhsi = netdev_priv(ndev);
	int res;

	clear_bit(CFHSI_SHUTDOWN, &cfhsi->bits);

	/* Initialize state vaiables. */
	cfhsi->tx_state = CFHSI_TX_STATE_IDLE;
	cfhsi->rx_state.state = CFHSI_RX_STATE_DESC;

	/* Set flow info */
	cfhsi->flow_off_sent = 0;

	/*
	 * Allocate a TX buffer with the size of a HSI packet descriptors
	 * and the necessary room for CAIF payload frames.
	 */
	cfhsi->tx_buf = kzalloc(CFHSI_BUF_SZ_TX, GFP_KERNEL);
	if (!cfhsi->tx_buf) {
		res = -ENODEV;
		goto err_alloc_tx;
	}

	/*
	 * Allocate a RX buffer with the size of two HSI packet descriptors and
	 * the necessary room for CAIF payload frames.
	 */
	cfhsi->rx_buf = kzalloc(CFHSI_BUF_SZ_RX, GFP_KERNEL);
	if (!cfhsi->rx_buf) {
		res = -ENODEV;
		goto err_alloc_rx;
	}

	cfhsi->rx_flip_buf = kzalloc(CFHSI_BUF_SZ_RX, GFP_KERNEL);
	if (!cfhsi->rx_flip_buf) {
		res = -ENODEV;
		goto err_alloc_rx_flip;
	}

	/* Initialize aggregation timeout */
	cfhsi->cfg.aggregation_timeout = hsi_default_config.aggregation_timeout;

	/* Initialize recieve vaiables. */
	cfhsi->rx_ptr = cfhsi->rx_buf;
	cfhsi->rx_len = CFHSI_DESC_SZ;

	/* Initialize spin locks. */
	spin_lock_init(&cfhsi->lock);

	/* Set up the driver. */
	cfhsi->cb_ops.tx_done_cb = cfhsi_tx_done_cb;
	cfhsi->cb_ops.rx_done_cb = cfhsi_rx_done_cb;
	cfhsi->cb_ops.wake_up_cb = cfhsi_wake_up_cb;
	cfhsi->cb_ops.wake_down_cb = cfhsi_wake_down_cb;

	/* Initialize the work queues. */
	INIT_WORK(&cfhsi->wake_up_work, cfhsi_wake_up);
	INIT_WORK(&cfhsi->wake_down_work, cfhsi_wake_down);
	INIT_WORK(&cfhsi->out_of_sync_work, cfhsi_out_of_sync);

	/* Clear all bit fields. */
	clear_bit(CFHSI_WAKE_UP_ACK, &cfhsi->bits);
	clear_bit(CFHSI_WAKE_DOWN_ACK, &cfhsi->bits);
	clear_bit(CFHSI_WAKE_UP, &cfhsi->bits);
	clear_bit(CFHSI_AWAKE, &cfhsi->bits);

	/* Create work thread. */
	cfhsi->wq = alloc_ordered_workqueue(cfhsi->ndev->name, WQ_MEM_RECLAIM);
	if (!cfhsi->wq) {
		netdev_err(cfhsi->ndev, "%s: Failed to create work queue.\n",
			__func__);
		res = -ENODEV;
		goto err_create_wq;
	}

	/* Initialize wait queues. */
	init_waitqueue_head(&cfhsi->wake_up_wait);
	init_waitqueue_head(&cfhsi->wake_down_wait);
	init_waitqueue_head(&cfhsi->flush_fifo_wait);

	/* Setup the inactivity timer. */
	timer_setup(&cfhsi->inactivity_timer, cfhsi_inactivity_tout, 0);
	/* Setup the slowpath RX timer. */
	timer_setup(&cfhsi->rx_slowpath_timer, cfhsi_rx_slowpath, 0);
	/* Setup the aggregation timer. */
	timer_setup(&cfhsi->aggregation_timer, cfhsi_aggregation_tout, 0);

	/* Activate HSI interface. */
	res = cfhsi->ops->cfhsi_up(cfhsi->ops);
	if (res) {
		netdev_err(cfhsi->ndev,
			"%s: can't activate HSI interface: %d.\n",
			__func__, res);
		goto err_activate;
	}

	/* Flush FIFO */
	res = cfhsi_flush_fifo(cfhsi);
	if (res) {
		netdev_err(cfhsi->ndev, "%s: Can't flush FIFO: %d.\n",
			__func__, res);
		goto err_net_reg;
	}
	return res;

 err_net_reg:
	cfhsi->ops->cfhsi_down(cfhsi->ops);
 err_activate:
	destroy_workqueue(cfhsi->wq);
 err_create_wq:
	kfree(cfhsi->rx_flip_buf);
 err_alloc_rx_flip:
	kfree(cfhsi->rx_buf);
 err_alloc_rx:
	kfree(cfhsi->tx_buf);
 err_alloc_tx:
	return res;
}

static int cfhsi_close(struct net_device *ndev)
{
	struct cfhsi *cfhsi = netdev_priv(ndev);
	u8 *tx_buf, *rx_buf, *flip_buf;

	/* going to shutdown driver */
	set_bit(CFHSI_SHUTDOWN, &cfhsi->bits);

	/* Delete timers if pending */
	del_timer_sync(&cfhsi->inactivity_timer);
	del_timer_sync(&cfhsi->rx_slowpath_timer);
	del_timer_sync(&cfhsi->aggregation_timer);

	/* Cancel pending RX request (if any) */
	cfhsi->ops->cfhsi_rx_cancel(cfhsi->ops);

	/* Destroy workqueue */
	destroy_workqueue(cfhsi->wq);

	/* Store bufferes: will be freed later. */
	tx_buf = cfhsi->tx_buf;
	rx_buf = cfhsi->rx_buf;
	flip_buf = cfhsi->rx_flip_buf;
	/* Flush transmit queues. */
	cfhsi_abort_tx(cfhsi);

	/* Deactivate interface */
	cfhsi->ops->cfhsi_down(cfhsi->ops);

	/* Free buffers. */
	kfree(tx_buf);
	kfree(rx_buf);
	kfree(flip_buf);
	return 0;
}

static void cfhsi_uninit(struct net_device *dev)
{
	struct cfhsi *cfhsi = netdev_priv(dev);
	ASSERT_RTNL();
	symbol_put(cfhsi_get_device);
	list_del(&cfhsi->list);
}

static const struct net_device_ops cfhsi_netdevops = {
	.ndo_uninit = cfhsi_uninit,
	.ndo_open = cfhsi_open,
	.ndo_stop = cfhsi_close,
	.ndo_start_xmit = cfhsi_xmit
};

static void cfhsi_netlink_parms(struct nlattr *data[], struct cfhsi *cfhsi)
{
	int i;

	if (!data) {
		pr_debug("no params data found\n");
		return;
	}

	i = __IFLA_CAIF_HSI_INACTIVITY_TOUT;
	/*
	 * Inactivity timeout in millisecs. Lowest possible value is 1,
	 * and highest possible is NEXT_TIMER_MAX_DELTA.
	 */
	if (data[i]) {
		u32 inactivity_timeout = nla_get_u32(data[i]);
		/* Pre-calculate inactivity timeout. */
		cfhsi->cfg.inactivity_timeout =	inactivity_timeout * HZ / 1000;
		if (cfhsi->cfg.inactivity_timeout == 0)
			cfhsi->cfg.inactivity_timeout = 1;
		else if (cfhsi->cfg.inactivity_timeout > NEXT_TIMER_MAX_DELTA)
			cfhsi->cfg.inactivity_timeout = NEXT_TIMER_MAX_DELTA;
	}

	i = __IFLA_CAIF_HSI_AGGREGATION_TOUT;
	if (data[i])
		cfhsi->cfg.aggregation_timeout = nla_get_u32(data[i]);

	i = __IFLA_CAIF_HSI_HEAD_ALIGN;
	if (data[i])
		cfhsi->cfg.head_align = nla_get_u32(data[i]);

	i = __IFLA_CAIF_HSI_TAIL_ALIGN;
	if (data[i])
		cfhsi->cfg.tail_align = nla_get_u32(data[i]);

	i = __IFLA_CAIF_HSI_QHIGH_WATERMARK;
	if (data[i])
		cfhsi->cfg.q_high_mark = nla_get_u32(data[i]);

	i = __IFLA_CAIF_HSI_QLOW_WATERMARK;
	if (data[i])
		cfhsi->cfg.q_low_mark = nla_get_u32(data[i]);
}

static int caif_hsi_changelink(struct net_device *dev, struct nlattr *tb[],
			       struct nlattr *data[],
			       struct netlink_ext_ack *extack)
{
	cfhsi_netlink_parms(data, netdev_priv(dev));
	netdev_state_change(dev);
	return 0;
}

static const struct nla_policy caif_hsi_policy[__IFLA_CAIF_HSI_MAX + 1] = {
	[__IFLA_CAIF_HSI_INACTIVITY_TOUT] = { .type = NLA_U32, .len = 4 },
	[__IFLA_CAIF_HSI_AGGREGATION_TOUT] = { .type = NLA_U32, .len = 4 },
	[__IFLA_CAIF_HSI_HEAD_ALIGN] = { .type = NLA_U32, .len = 4 },
	[__IFLA_CAIF_HSI_TAIL_ALIGN] = { .type = NLA_U32, .len = 4 },
	[__IFLA_CAIF_HSI_QHIGH_WATERMARK] = { .type = NLA_U32, .len = 4 },
	[__IFLA_CAIF_HSI_QLOW_WATERMARK] = { .type = NLA_U32, .len = 4 },
};

static size_t caif_hsi_get_size(const struct net_device *dev)
{
	int i;
	size_t s = 0;
	for (i = __IFLA_CAIF_HSI_UNSPEC + 1; i < __IFLA_CAIF_HSI_MAX; i++)
		s += nla_total_size(caif_hsi_policy[i].len);
	return s;
}

static int caif_hsi_fill_info(struct sk_buff *skb, const struct net_device *dev)
{
	struct cfhsi *cfhsi = netdev_priv(dev);

	if (nla_put_u32(skb, __IFLA_CAIF_HSI_INACTIVITY_TOUT,
			cfhsi->cfg.inactivity_timeout) ||
	    nla_put_u32(skb, __IFLA_CAIF_HSI_AGGREGATION_TOUT,
			cfhsi->cfg.aggregation_timeout) ||
	    nla_put_u32(skb, __IFLA_CAIF_HSI_HEAD_ALIGN,
			cfhsi->cfg.head_align) ||
	    nla_put_u32(skb, __IFLA_CAIF_HSI_TAIL_ALIGN,
			cfhsi->cfg.tail_align) ||
	    nla_put_u32(skb, __IFLA_CAIF_HSI_QHIGH_WATERMARK,
			cfhsi->cfg.q_high_mark) ||
	    nla_put_u32(skb, __IFLA_CAIF_HSI_QLOW_WATERMARK,
			cfhsi->cfg.q_low_mark))
		return -EMSGSIZE;

	return 0;
}

static int caif_hsi_newlink(struct net *src_net, struct net_device *dev,
			    struct nlattr *tb[], struct nlattr *data[],
			    struct netlink_ext_ack *extack)
{
	struct cfhsi *cfhsi = NULL;
	struct cfhsi_ops *(*get_ops)(void);

	ASSERT_RTNL();

	cfhsi = netdev_priv(dev);
	cfhsi_netlink_parms(data, cfhsi);

	get_ops = symbol_get(cfhsi_get_ops);
	if (!get_ops) {
		pr_err("%s: failed to get the cfhsi_ops\n", __func__);
		return -ENODEV;
	}

	/* Assign the HSI device. */
	cfhsi->ops = (*get_ops)();
	if (!cfhsi->ops) {
		pr_err("%s: failed to get the cfhsi_ops\n", __func__);
		goto err;
	}

	/* Assign the driver to this HSI device. */
	cfhsi->ops->cb_ops = &cfhsi->cb_ops;
	if (register_netdevice(dev)) {
		pr_warn("%s: caif_hsi device registration failed\n", __func__);
		goto err;
	}
	/* Add CAIF HSI device to list. */
	list_add_tail(&cfhsi->list, &cfhsi_list);

	return 0;
err:
	symbol_put(cfhsi_get_ops);
	return -ENODEV;
}

static struct rtnl_link_ops caif_hsi_link_ops __read_mostly = {
	.kind		= "cfhsi",
	.priv_size	= sizeof(struct cfhsi),
	.setup		= cfhsi_setup,
	.maxtype	= __IFLA_CAIF_HSI_MAX,
	.policy	= caif_hsi_policy,
	.newlink	= caif_hsi_newlink,
	.changelink	= caif_hsi_changelink,
	.get_size	= caif_hsi_get_size,
	.fill_info	= caif_hsi_fill_info,
};

static void __exit cfhsi_exit_module(void)
{
	struct list_head *list_node;
	struct list_head *n;
	struct cfhsi *cfhsi;

	rtnl_link_unregister(&caif_hsi_link_ops);

	rtnl_lock();
	list_for_each_safe(list_node, n, &cfhsi_list) {
		cfhsi = list_entry(list_node, struct cfhsi, list);
		unregister_netdevice(cfhsi->ndev);
	}
	rtnl_unlock();
}

static int __init cfhsi_init_module(void)
{
	return rtnl_link_register(&caif_hsi_link_ops);
}

module_init(cfhsi_init_module);
module_exit(cfhsi_exit_module);
