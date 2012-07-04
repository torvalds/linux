/*
 * Copyright (C) ST-Ericsson AB 2010
 * Contact: Sjur Brendeland / sjur.brandeland@stericsson.com
 * Author:  Daniel Martensson / daniel.martensson@stericsson.com
 *	    Dmitry.Tarnyagin  / dmitry.tarnyagin@stericsson.com
 * License terms: GNU General Public License (GPL) version 2.
 */

#define pr_fmt(fmt) KBUILD_MODNAME fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/netdevice.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/if_arp.h>
#include <linux/timer.h>
#include <linux/rtnetlink.h>
#include <linux/pkt_sched.h>
#include <net/caif/caif_layer.h>
#include <net/caif/caif_hsi.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Daniel Martensson<daniel.martensson@stericsson.com>");
MODULE_DESCRIPTION("CAIF HSI driver");

/* Returns the number of padding bytes for alignment. */
#define PAD_POW2(x, pow) ((((x)&((pow)-1)) == 0) ? 0 :\
				(((pow)-((x)&((pow)-1)))))

static int inactivity_timeout = 1000;
module_param(inactivity_timeout, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(inactivity_timeout, "Inactivity timeout on HSI, ms.");

static int aggregation_timeout = 1;
module_param(aggregation_timeout, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(aggregation_timeout, "Aggregation timeout on HSI, ms.");

/*
 * HSI padding options.
 * Warning: must be a base of 2 (& operation used) and can not be zero !
 */
static int hsi_head_align = 4;
module_param(hsi_head_align, int, S_IRUGO);
MODULE_PARM_DESC(hsi_head_align, "HSI head alignment.");

static int hsi_tail_align = 4;
module_param(hsi_tail_align, int, S_IRUGO);
MODULE_PARM_DESC(hsi_tail_align, "HSI tail alignment.");

/*
 * HSI link layer flowcontrol thresholds.
 * Warning: A high threshold value migth increase throughput but it will at
 * the same time prevent channel prioritization and increase the risk of
 * flooding the modem. The high threshold should be above the low.
 */
static int hsi_high_threshold = 100;
module_param(hsi_high_threshold, int, S_IRUGO);
MODULE_PARM_DESC(hsi_high_threshold, "HSI high threshold (FLOW OFF).");

static int hsi_low_threshold = 50;
module_param(hsi_low_threshold, int, S_IRUGO);
MODULE_PARM_DESC(hsi_low_threshold, "HSI high threshold (FLOW ON).");

#define ON 1
#define OFF 0

/*
 * Threshold values for the HSI packet queue. Flowcontrol will be asserted
 * when the number of packets exceeds HIGH_WATER_MARK. It will not be
 * de-asserted before the number of packets drops below LOW_WATER_MARK.
 */
#define LOW_WATER_MARK   hsi_low_threshold
#define HIGH_WATER_MARK  hsi_high_threshold

static LIST_HEAD(cfhsi_list);
static spinlock_t cfhsi_list_lock;

static void cfhsi_inactivity_tout(unsigned long arg)
{
	struct cfhsi *cfhsi = (struct cfhsi *)arg;

	dev_dbg(&cfhsi->ndev->dev, "%s.\n",
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
	hpad = 1 + PAD_POW2((info->hdr_len + 1), hsi_head_align);
	tpad = PAD_POW2((skb->len + hpad), hsi_tail_align);
	len = skb->len + hpad + tpad;

	if (direction > 0)
		cfhsi->aggregation_len += len;
	else if (direction < 0)
		cfhsi->aggregation_len -= len;
}

static bool cfhsi_can_send_aggregate(struct cfhsi *cfhsi)
{
	int i;

	if (cfhsi->aggregation_timeout < 0)
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
			jiffies + cfhsi->inactivity_timeout);
	spin_unlock_bh(&cfhsi->lock);
}

static int cfhsi_flush_fifo(struct cfhsi *cfhsi)
{
	char buffer[32]; /* Any reasonable value */
	size_t fifo_occupancy;
	int ret;

	dev_dbg(&cfhsi->ndev->dev, "%s.\n",
		__func__);

	do {
		ret = cfhsi->dev->cfhsi_fifo_occupancy(cfhsi->dev,
				&fifo_occupancy);
		if (ret) {
			dev_warn(&cfhsi->ndev->dev,
				"%s: can't get FIFO occupancy: %d.\n",
				__func__, ret);
			break;
		} else if (!fifo_occupancy)
			/* No more data, exitting normally */
			break;

		fifo_occupancy = min(sizeof(buffer), fifo_occupancy);
		set_bit(CFHSI_FLUSH_FIFO, &cfhsi->bits);
		ret = cfhsi->dev->cfhsi_rx(buffer, fifo_occupancy,
				cfhsi->dev);
		if (ret) {
			clear_bit(CFHSI_FLUSH_FIFO, &cfhsi->bits);
			dev_warn(&cfhsi->ndev->dev,
				"%s: can't read data: %d.\n",
				__func__, ret);
			break;
		}

		ret = 5 * HZ;
		ret = wait_event_interruptible_timeout(cfhsi->flush_fifo_wait,
			 !test_bit(CFHSI_FLUSH_FIFO, &cfhsi->bits), ret);

		if (ret < 0) {
			dev_warn(&cfhsi->ndev->dev,
				"%s: can't wait for flush complete: %d.\n",
				__func__, ret);
			break;
		} else if (!ret) {
			ret = -ETIMEDOUT;
			dev_warn(&cfhsi->ndev->dev,
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
		int hpad = 0;
		int tpad = 0;

		/* Calculate needed head alignment and tail alignment. */
		info = (struct caif_payload_info *)&skb->cb;

		hpad = 1 + PAD_POW2((info->hdr_len + 1), hsi_head_align);
		tpad = PAD_POW2((skb->len + hpad), hsi_tail_align);

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
	pfrm = desc->emb_frm + CFHSI_MAX_EMB_FRM_SZ;
	while (nfrms < CFHSI_MAX_PKTS) {
		struct caif_payload_info *info;
		int hpad = 0;
		int tpad = 0;

		if (!skb)
			skb = cfhsi_dequeue(cfhsi);

		if (!skb)
			break;

		/* Calculate needed head alignment and tail alignment. */
		info = (struct caif_payload_info *)&skb->cb;

		hpad = 1 + PAD_POW2((info->hdr_len + 1), hsi_head_align);
		tpad = PAD_POW2((skb->len + hpad), hsi_tail_align);

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

	dev_dbg(&cfhsi->ndev->dev, "%s.\n", __func__);

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
				jiffies + cfhsi->inactivity_timeout);
			spin_unlock_bh(&cfhsi->lock);
			break;
		}

		/* Set up new transfer. */
		res = cfhsi->dev->cfhsi_tx(cfhsi->tx_buf, len, cfhsi->dev);
		if (WARN_ON(res < 0))
			dev_err(&cfhsi->ndev->dev, "%s: TX error %d.\n",
				__func__, res);
	} while (res < 0);
}

static void cfhsi_tx_done(struct cfhsi *cfhsi)
{
	dev_dbg(&cfhsi->ndev->dev, "%s.\n", __func__);

	if (test_bit(CFHSI_SHUTDOWN, &cfhsi->bits))
		return;

	/*
	 * Send flow on if flow off has been previously signalled
	 * and number of packets is below low water mark.
	 */
	spin_lock_bh(&cfhsi->lock);
	if (cfhsi->flow_off_sent &&
			cfhsi_tx_queue_len(cfhsi) <= cfhsi->q_low_mark &&
			cfhsi->cfdev.flowctrl) {

		cfhsi->flow_off_sent = 0;
		cfhsi->cfdev.flowctrl(cfhsi->ndev, ON);
	}

	if (cfhsi_can_send_aggregate(cfhsi)) {
		spin_unlock_bh(&cfhsi->lock);
		cfhsi_start_tx(cfhsi);
	} else {
		mod_timer(&cfhsi->aggregation_timer,
			jiffies + cfhsi->aggregation_timeout);
		spin_unlock_bh(&cfhsi->lock);
	}

	return;
}

static void cfhsi_tx_done_cb(struct cfhsi_drv *drv)
{
	struct cfhsi *cfhsi;

	cfhsi = container_of(drv, struct cfhsi, drv);
	dev_dbg(&cfhsi->ndev->dev, "%s.\n",
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
		dev_err(&cfhsi->ndev->dev, "%s: Invalid descriptor.\n",
			__func__);
		return -EPROTO;
	}

	/* Check for embedded CAIF frame. */
	if (desc->offset) {
		struct sk_buff *skb;
		u8 *dst = NULL;
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
			dev_err(&cfhsi->ndev->dev, "%s: Invalid length.\n",
				__func__);
			return -EPROTO;
		}

		/* Allocate SKB (OK even in IRQ context). */
		skb = alloc_skb(len + 1, GFP_ATOMIC);
		if (!skb) {
			dev_err(&cfhsi->ndev->dev, "%s: Out of memory !\n",
				__func__);
			return -ENOMEM;
		}
		caif_assert(skb != NULL);

		dst = skb_put(skb, len);
		memcpy(dst, pfrm, len);

		skb->protocol = htons(ETH_P_CAIF);
		skb_reset_mac_header(skb);
		skb->dev = cfhsi->ndev;

		/*
		 * We are called from a arch specific platform device.
		 * Unfortunately we don't know what context we're
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
		dev_err(&cfhsi->ndev->dev,
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
		dev_err(&cfhsi->ndev->dev, "%s: Invalid descriptor.\n",
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
		u8 *dst = NULL;
		u8 *pcffrm = NULL;
		int len = 0;

		/* CAIF frame starts after head padding. */
		pcffrm = pfrm + *pfrm + 1;

		/* Read length of CAIF frame (little endian). */
		len = *pcffrm;
		len |= ((*(pcffrm + 1)) << 8) & 0xFF00;
		len += 2;	/* Add FCS fields. */

		/* Sanity check length of CAIF frames. */
		if (unlikely(len > CFHSI_MAX_CAIF_FRAME_SZ)) {
			dev_err(&cfhsi->ndev->dev, "%s: Invalid length.\n",
				__func__);
			return -EPROTO;
		}

		/* Allocate SKB (OK even in IRQ context). */
		skb = alloc_skb(len + 1, GFP_ATOMIC);
		if (!skb) {
			dev_err(&cfhsi->ndev->dev, "%s: Out of memory !\n",
				__func__);
			cfhsi->rx_state.nfrms = nfrms;
			return -ENOMEM;
		}
		caif_assert(skb != NULL);

		dst = skb_put(skb, len);
		memcpy(dst, pcffrm, len);

		skb->protocol = htons(ETH_P_CAIF);
		skb_reset_mac_header(skb);
		skb->dev = cfhsi->ndev;

		/*
		 * We're called from a platform device,
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

	dev_dbg(&cfhsi->ndev->dev, "%s\n", __func__);

	if (test_bit(CFHSI_SHUTDOWN, &cfhsi->bits))
		return;

	/* Update inactivity timer if pending. */
	spin_lock_bh(&cfhsi->lock);
	mod_timer_pending(&cfhsi->inactivity_timer,
			jiffies + cfhsi->inactivity_timeout);
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

			if (desc_pld_len > 0)
				rx_len = desc_pld_len;

			if (desc_pld_len > 0 &&
					(piggy_desc->header & CFHSI_PIGGY_DESC))
				rx_len += CFHSI_DESC_SZ;

			/*
			 * Copy needed information from the piggy-backed
			 * descriptor to the descriptor in the start.
			 */
			memcpy(rx_buf, (u8 *)piggy_desc,
					CFHSI_DESC_SHORT_SZ);
			if (desc_pld_len == -EPROTO)
				goto out_of_sync;
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
		dev_dbg(&cfhsi->ndev->dev, "%s: Start RX.\n",
				__func__);

		res = cfhsi->dev->cfhsi_rx(rx_ptr, rx_len,
				cfhsi->dev);
		if (WARN_ON(res < 0)) {
			dev_err(&cfhsi->ndev->dev, "%s: RX error %d.\n",
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
	dev_err(&cfhsi->ndev->dev, "%s: Out of sync.\n", __func__);
	print_hex_dump_bytes("--> ", DUMP_PREFIX_NONE,
			cfhsi->rx_buf, CFHSI_DESC_SZ);
	schedule_work(&cfhsi->out_of_sync_work);
}

static void cfhsi_rx_slowpath(unsigned long arg)
{
	struct cfhsi *cfhsi = (struct cfhsi *)arg;

	dev_dbg(&cfhsi->ndev->dev, "%s.\n",
		__func__);

	cfhsi_rx_done(cfhsi);
}

static void cfhsi_rx_done_cb(struct cfhsi_drv *drv)
{
	struct cfhsi *cfhsi;

	cfhsi = container_of(drv, struct cfhsi, drv);
	dev_dbg(&cfhsi->ndev->dev, "%s.\n",
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
	cfhsi->dev->cfhsi_wake_up(cfhsi->dev);

	dev_dbg(&cfhsi->ndev->dev, "%s: Start waiting.\n",
		__func__);

	/* Wait for acknowledge. */
	ret = CFHSI_WAKE_TOUT;
	ret = wait_event_interruptible_timeout(cfhsi->wake_up_wait,
					test_and_clear_bit(CFHSI_WAKE_UP_ACK,
							&cfhsi->bits), ret);
	if (unlikely(ret < 0)) {
		/* Interrupted by signal. */
		dev_err(&cfhsi->ndev->dev, "%s: Signalled: %ld.\n",
			__func__, ret);

		clear_bit(CFHSI_WAKE_UP, &cfhsi->bits);
		cfhsi->dev->cfhsi_wake_down(cfhsi->dev);
		return;
	} else if (!ret) {
		bool ca_wake = false;
		size_t fifo_occupancy = 0;

		/* Wakeup timeout */
		dev_dbg(&cfhsi->ndev->dev, "%s: Timeout.\n",
			__func__);

		/* Check FIFO to check if modem has sent something. */
		WARN_ON(cfhsi->dev->cfhsi_fifo_occupancy(cfhsi->dev,
					&fifo_occupancy));

		dev_dbg(&cfhsi->ndev->dev, "%s: Bytes in FIFO: %u.\n",
				__func__, (unsigned) fifo_occupancy);

		/* Check if we misssed the interrupt. */
		WARN_ON(cfhsi->dev->cfhsi_get_peer_wake(cfhsi->dev,
							&ca_wake));

		if (ca_wake) {
			dev_err(&cfhsi->ndev->dev, "%s: CA Wake missed !.\n",
				__func__);

			/* Clear the CFHSI_WAKE_UP_ACK bit to prevent race. */
			clear_bit(CFHSI_WAKE_UP_ACK, &cfhsi->bits);

			/* Continue execution. */
			goto wake_ack;
		}

		clear_bit(CFHSI_WAKE_UP, &cfhsi->bits);
		cfhsi->dev->cfhsi_wake_down(cfhsi->dev);
		return;
	}
wake_ack:
	dev_dbg(&cfhsi->ndev->dev, "%s: Woken.\n",
		__func__);

	/* Clear power up bit. */
	set_bit(CFHSI_AWAKE, &cfhsi->bits);
	clear_bit(CFHSI_WAKE_UP, &cfhsi->bits);

	/* Resume read operation. */
	dev_dbg(&cfhsi->ndev->dev, "%s: Start RX.\n", __func__);
	res = cfhsi->dev->cfhsi_rx(cfhsi->rx_ptr, cfhsi->rx_len, cfhsi->dev);

	if (WARN_ON(res < 0))
		dev_err(&cfhsi->ndev->dev, "%s: RX err %d.\n", __func__, res);

	/* Clear power up acknowledment. */
	clear_bit(CFHSI_WAKE_UP_ACK, &cfhsi->bits);

	spin_lock_bh(&cfhsi->lock);

	/* Resume transmit if queues are not empty. */
	if (!cfhsi_tx_queue_len(cfhsi)) {
		dev_dbg(&cfhsi->ndev->dev, "%s: Peer wake, start timer.\n",
			__func__);
		/* Start inactivity timer. */
		mod_timer(&cfhsi->inactivity_timer,
				jiffies + cfhsi->inactivity_timeout);
		spin_unlock_bh(&cfhsi->lock);
		return;
	}

	dev_dbg(&cfhsi->ndev->dev, "%s: Host wake.\n",
		__func__);

	spin_unlock_bh(&cfhsi->lock);

	/* Create HSI frame. */
	len = cfhsi_tx_frm((struct cfhsi_desc *)cfhsi->tx_buf, cfhsi);

	if (likely(len > 0)) {
		/* Set up new transfer. */
		res = cfhsi->dev->cfhsi_tx(cfhsi->tx_buf, len, cfhsi->dev);
		if (WARN_ON(res < 0)) {
			dev_err(&cfhsi->ndev->dev, "%s: TX error %d.\n",
				__func__, res);
			cfhsi_abort_tx(cfhsi);
		}
	} else {
		dev_err(&cfhsi->ndev->dev,
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
	dev_dbg(&cfhsi->ndev->dev, "%s.\n", __func__);

	if (test_bit(CFHSI_SHUTDOWN, &cfhsi->bits))
		return;

	/* Deactivate wake line. */
	cfhsi->dev->cfhsi_wake_down(cfhsi->dev);

	/* Wait for acknowledge. */
	ret = CFHSI_WAKE_TOUT;
	ret = wait_event_interruptible_timeout(cfhsi->wake_down_wait,
					test_and_clear_bit(CFHSI_WAKE_DOWN_ACK,
							&cfhsi->bits), ret);
	if (ret < 0) {
		/* Interrupted by signal. */
		dev_err(&cfhsi->ndev->dev, "%s: Signalled: %ld.\n",
			__func__, ret);
		return;
	} else if (!ret) {
		bool ca_wake = true;

		/* Timeout */
		dev_err(&cfhsi->ndev->dev, "%s: Timeout.\n", __func__);

		/* Check if we misssed the interrupt. */
		WARN_ON(cfhsi->dev->cfhsi_get_peer_wake(cfhsi->dev,
							&ca_wake));
		if (!ca_wake)
			dev_err(&cfhsi->ndev->dev, "%s: CA Wake missed !.\n",
				__func__);
	}

	/* Check FIFO occupancy. */
	while (retry) {
		WARN_ON(cfhsi->dev->cfhsi_fifo_occupancy(cfhsi->dev,
							&fifo_occupancy));

		if (!fifo_occupancy)
			break;

		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(1);
		retry--;
	}

	if (!retry)
		dev_err(&cfhsi->ndev->dev, "%s: FIFO Timeout.\n", __func__);

	/* Clear AWAKE condition. */
	clear_bit(CFHSI_AWAKE, &cfhsi->bits);

	/* Cancel pending RX requests. */
	cfhsi->dev->cfhsi_rx_cancel(cfhsi->dev);

}

static void cfhsi_out_of_sync(struct work_struct *work)
{
	struct cfhsi *cfhsi = NULL;

	cfhsi = container_of(work, struct cfhsi, out_of_sync_work);

	rtnl_lock();
	dev_close(cfhsi->ndev);
	rtnl_unlock();
}

static void cfhsi_wake_up_cb(struct cfhsi_drv *drv)
{
	struct cfhsi *cfhsi = NULL;

	cfhsi = container_of(drv, struct cfhsi, drv);
	dev_dbg(&cfhsi->ndev->dev, "%s.\n",
		__func__);

	set_bit(CFHSI_WAKE_UP_ACK, &cfhsi->bits);
	wake_up_interruptible(&cfhsi->wake_up_wait);

	if (test_bit(CFHSI_SHUTDOWN, &cfhsi->bits))
		return;

	/* Schedule wake up work queue if the peer initiates. */
	if (!test_and_set_bit(CFHSI_WAKE_UP, &cfhsi->bits))
		queue_work(cfhsi->wq, &cfhsi->wake_up_work);
}

static void cfhsi_wake_down_cb(struct cfhsi_drv *drv)
{
	struct cfhsi *cfhsi = NULL;

	cfhsi = container_of(drv, struct cfhsi, drv);
	dev_dbg(&cfhsi->ndev->dev, "%s.\n",
		__func__);

	/* Initiating low power is only permitted by the host (us). */
	set_bit(CFHSI_WAKE_DOWN_ACK, &cfhsi->bits);
	wake_up_interruptible(&cfhsi->wake_down_wait);
}

static void cfhsi_aggregation_tout(unsigned long arg)
{
	struct cfhsi *cfhsi = (struct cfhsi *)arg;

	dev_dbg(&cfhsi->ndev->dev, "%s.\n",
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
		cfhsi_tx_queue_len(cfhsi) > cfhsi->q_high_mark &&
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
		res = cfhsi->dev->cfhsi_tx(cfhsi->tx_buf, len, cfhsi->dev);
		if (WARN_ON(res < 0)) {
			dev_err(&cfhsi->ndev->dev, "%s: TX error %d.\n",
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

static const struct net_device_ops cfhsi_ops;

static void cfhsi_setup(struct net_device *dev)
{
	int i;
	struct cfhsi *cfhsi = netdev_priv(dev);
	dev->features = 0;
	dev->netdev_ops = &cfhsi_ops;
	dev->type = ARPHRD_CAIF;
	dev->flags = IFF_POINTOPOINT | IFF_NOARP;
	dev->mtu = CFHSI_MAX_CAIF_FRAME_SZ;
	dev->tx_queue_len = 0;
	dev->destructor = free_netdev;
	for (i = 0; i < CFHSI_PRIO_LAST; ++i)
		skb_queue_head_init(&cfhsi->qhead[i]);
	cfhsi->cfdev.link_select = CAIF_LINK_HIGH_BANDW;
	cfhsi->cfdev.use_frag = false;
	cfhsi->cfdev.use_stx = false;
	cfhsi->cfdev.use_fcs = false;
	cfhsi->ndev = dev;
}

int cfhsi_probe(struct platform_device *pdev)
{
	struct cfhsi *cfhsi = NULL;
	struct net_device *ndev;

	int res;

	ndev = alloc_netdev(sizeof(struct cfhsi), "cfhsi%d", cfhsi_setup);
	if (!ndev)
		return -ENODEV;

	cfhsi = netdev_priv(ndev);
	cfhsi->ndev = ndev;
	cfhsi->pdev = pdev;

	/* Assign the HSI device. */
	cfhsi->dev = pdev->dev.platform_data;

	/* Assign the driver to this HSI device. */
	cfhsi->dev->drv = &cfhsi->drv;

	/* Register network device. */
	res = register_netdev(ndev);
	if (res) {
		dev_err(&ndev->dev, "%s: Registration error: %d.\n",
			__func__, res);
		free_netdev(ndev);
		return -ENODEV;
	}
	/* Add CAIF HSI device to list. */
	spin_lock(&cfhsi_list_lock);
	list_add_tail(&cfhsi->list, &cfhsi_list);
	spin_unlock(&cfhsi_list_lock);

	return res;
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
	cfhsi->q_low_mark = LOW_WATER_MARK;
	cfhsi->q_high_mark = HIGH_WATER_MARK;


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

	/* Pre-calculate inactivity timeout. */
	if (inactivity_timeout != -1) {
		cfhsi->inactivity_timeout =
				inactivity_timeout * HZ / 1000;
		if (!cfhsi->inactivity_timeout)
			cfhsi->inactivity_timeout = 1;
		else if (cfhsi->inactivity_timeout > NEXT_TIMER_MAX_DELTA)
			cfhsi->inactivity_timeout = NEXT_TIMER_MAX_DELTA;
	} else {
		cfhsi->inactivity_timeout = NEXT_TIMER_MAX_DELTA;
	}

	/* Initialize aggregation timeout */
	cfhsi->aggregation_timeout = aggregation_timeout;

	/* Initialize recieve vaiables. */
	cfhsi->rx_ptr = cfhsi->rx_buf;
	cfhsi->rx_len = CFHSI_DESC_SZ;

	/* Initialize spin locks. */
	spin_lock_init(&cfhsi->lock);

	/* Set up the driver. */
	cfhsi->drv.tx_done_cb = cfhsi_tx_done_cb;
	cfhsi->drv.rx_done_cb = cfhsi_rx_done_cb;
	cfhsi->drv.wake_up_cb = cfhsi_wake_up_cb;
	cfhsi->drv.wake_down_cb = cfhsi_wake_down_cb;

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
	cfhsi->wq = create_singlethread_workqueue(cfhsi->pdev->name);
	if (!cfhsi->wq) {
		dev_err(&cfhsi->ndev->dev, "%s: Failed to create work queue.\n",
			__func__);
		res = -ENODEV;
		goto err_create_wq;
	}

	/* Initialize wait queues. */
	init_waitqueue_head(&cfhsi->wake_up_wait);
	init_waitqueue_head(&cfhsi->wake_down_wait);
	init_waitqueue_head(&cfhsi->flush_fifo_wait);

	/* Setup the inactivity timer. */
	init_timer(&cfhsi->inactivity_timer);
	cfhsi->inactivity_timer.data = (unsigned long)cfhsi;
	cfhsi->inactivity_timer.function = cfhsi_inactivity_tout;
	/* Setup the slowpath RX timer. */
	init_timer(&cfhsi->rx_slowpath_timer);
	cfhsi->rx_slowpath_timer.data = (unsigned long)cfhsi;
	cfhsi->rx_slowpath_timer.function = cfhsi_rx_slowpath;
	/* Setup the aggregation timer. */
	init_timer(&cfhsi->aggregation_timer);
	cfhsi->aggregation_timer.data = (unsigned long)cfhsi;
	cfhsi->aggregation_timer.function = cfhsi_aggregation_tout;

	/* Activate HSI interface. */
	res = cfhsi->dev->cfhsi_up(cfhsi->dev);
	if (res) {
		dev_err(&cfhsi->ndev->dev,
			"%s: can't activate HSI interface: %d.\n",
			__func__, res);
		goto err_activate;
	}

	/* Flush FIFO */
	res = cfhsi_flush_fifo(cfhsi);
	if (res) {
		dev_err(&cfhsi->ndev->dev, "%s: Can't flush FIFO: %d.\n",
			__func__, res);
		goto err_net_reg;
	}
	return res;

 err_net_reg:
	cfhsi->dev->cfhsi_down(cfhsi->dev);
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

	/* Flush workqueue */
	flush_workqueue(cfhsi->wq);

	/* Delete timers if pending */
	del_timer_sync(&cfhsi->inactivity_timer);
	del_timer_sync(&cfhsi->rx_slowpath_timer);
	del_timer_sync(&cfhsi->aggregation_timer);

	/* Cancel pending RX request (if any) */
	cfhsi->dev->cfhsi_rx_cancel(cfhsi->dev);

	/* Destroy workqueue */
	destroy_workqueue(cfhsi->wq);

	/* Store bufferes: will be freed later. */
	tx_buf = cfhsi->tx_buf;
	rx_buf = cfhsi->rx_buf;
	flip_buf = cfhsi->rx_flip_buf;
	/* Flush transmit queues. */
	cfhsi_abort_tx(cfhsi);

	/* Deactivate interface */
	cfhsi->dev->cfhsi_down(cfhsi->dev);

	/* Free buffers. */
	kfree(tx_buf);
	kfree(rx_buf);
	kfree(flip_buf);
	return 0;
}

static const struct net_device_ops cfhsi_ops = {
	.ndo_open = cfhsi_open,
	.ndo_stop = cfhsi_close,
	.ndo_start_xmit = cfhsi_xmit
};

int cfhsi_remove(struct platform_device *pdev)
{
	struct list_head *list_node;
	struct list_head *n;
	struct cfhsi *cfhsi = NULL;
	struct cfhsi_dev *dev;

	dev = (struct cfhsi_dev *)pdev->dev.platform_data;
	spin_lock(&cfhsi_list_lock);
	list_for_each_safe(list_node, n, &cfhsi_list) {
		cfhsi = list_entry(list_node, struct cfhsi, list);
		/* Find the corresponding device. */
		if (cfhsi->dev == dev) {
			/* Remove from list. */
			list_del(list_node);
			spin_unlock(&cfhsi_list_lock);
			return 0;
		}
	}
	spin_unlock(&cfhsi_list_lock);
	return -ENODEV;
}

struct platform_driver cfhsi_plat_drv = {
	.probe = cfhsi_probe,
	.remove = cfhsi_remove,
	.driver = {
		   .name = "cfhsi",
		   .owner = THIS_MODULE,
		   },
};

static void __exit cfhsi_exit_module(void)
{
	struct list_head *list_node;
	struct list_head *n;
	struct cfhsi *cfhsi = NULL;

	spin_lock(&cfhsi_list_lock);
	list_for_each_safe(list_node, n, &cfhsi_list) {
		cfhsi = list_entry(list_node, struct cfhsi, list);

		/* Remove from list. */
		list_del(list_node);
		spin_unlock(&cfhsi_list_lock);

		unregister_netdevice(cfhsi->ndev);

		spin_lock(&cfhsi_list_lock);
	}
	spin_unlock(&cfhsi_list_lock);

	/* Unregister platform driver. */
	platform_driver_unregister(&cfhsi_plat_drv);
}

static int __init cfhsi_init_module(void)
{
	int result;

	/* Initialize spin lock. */
	spin_lock_init(&cfhsi_list_lock);

	/* Register platform driver. */
	result = platform_driver_register(&cfhsi_plat_drv);
	if (result) {
		printk(KERN_ERR "Could not register platform HSI driver: %d.\n",
			result);
		goto err_dev_register;
	}

 err_dev_register:
	return result;
}

module_init(cfhsi_init_module);
module_exit(cfhsi_exit_module);
