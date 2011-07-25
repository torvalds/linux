/*
 * Copyright (C) ST-Ericsson AB 2010
 * Contact: Sjur Brendeland / sjur.brandeland@stericsson.com
 * Author:  Daniel Martensson / daniel.martensson@stericsson.com
 *	    Dmitry.Tarnyagin  / dmitry.tarnyagin@stericsson.com
 * License terms: GNU General Public License (GPL) version 2.
 */

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
#include <net/caif/caif_layer.h>
#include <net/caif/caif_hsi.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Daniel Martensson<daniel.martensson@stericsson.com>");
MODULE_DESCRIPTION("CAIF HSI driver");

/* Returns the number of padding bytes for alignment. */
#define PAD_POW2(x, pow) ((((x)&((pow)-1)) == 0) ? 0 :\
				(((pow)-((x)&((pow)-1)))))

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

static void cfhsi_abort_tx(struct cfhsi *cfhsi)
{
	struct sk_buff *skb;

	for (;;) {
		spin_lock_bh(&cfhsi->lock);
		skb = skb_dequeue(&cfhsi->qhead);
		if (!skb)
			break;

		cfhsi->ndev->stats.tx_errors++;
		cfhsi->ndev->stats.tx_dropped++;
		spin_unlock_bh(&cfhsi->lock);
		kfree_skb(skb);
	}
	cfhsi->tx_state = CFHSI_TX_STATE_IDLE;
	if (!test_bit(CFHSI_SHUTDOWN, &cfhsi->bits))
		mod_timer(&cfhsi->timer, jiffies + CFHSI_INACTIVITY_TOUT);
	spin_unlock_bh(&cfhsi->lock);
}

static int cfhsi_flush_fifo(struct cfhsi *cfhsi)
{
	char buffer[32]; /* Any reasonable value */
	size_t fifo_occupancy;
	int ret;

	dev_dbg(&cfhsi->ndev->dev, "%s.\n",
		__func__);


	ret = cfhsi->dev->cfhsi_wake_up(cfhsi->dev);
	if (ret) {
		dev_warn(&cfhsi->ndev->dev,
			"%s: can't wake up HSI interface: %d.\n",
			__func__, ret);
		return ret;
	}

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
		wait_event_interruptible_timeout(cfhsi->flush_fifo_wait,
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

	cfhsi->dev->cfhsi_wake_down(cfhsi->dev);

	return ret;
}

static int cfhsi_tx_frm(struct cfhsi_desc *desc, struct cfhsi *cfhsi)
{
	int nfrms = 0;
	int pld_len = 0;
	struct sk_buff *skb;
	u8 *pfrm = desc->emb_frm + CFHSI_MAX_EMB_FRM_SZ;

	skb = skb_dequeue(&cfhsi->qhead);
	if (!skb)
		return 0;

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
			cfhsi->ndev->stats.tx_packets++;
			cfhsi->ndev->stats.tx_bytes += skb->len;

			/* Copy in embedded CAIF frame. */
			skb_copy_bits(skb, 0, pemb, skb->len);
			consume_skb(skb);
			skb = NULL;
		}
	} else
		/* Clear offset. */
		desc->offset = 0;

	/* Create payload CAIF frames. */
	pfrm = desc->emb_frm + CFHSI_MAX_EMB_FRM_SZ;
	while (nfrms < CFHSI_MAX_PKTS) {
		struct caif_payload_info *info;
		int hpad = 0;
		int tpad = 0;

		if (!skb)
			skb = skb_dequeue(&cfhsi->qhead);

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
		cfhsi->ndev->stats.tx_packets++;
		cfhsi->ndev->stats.tx_bytes += skb->len;

		/* Copy in CAIF frame. */
		skb_copy_bits(skb, 0, pfrm, skb->len);

		/* Update payload length. */
		pld_len += desc->cffrm_len[nfrms];

		/* Update frame pointer. */
		pfrm += skb->len + tpad;
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
	skb = skb_peek(&cfhsi->qhead);
	if (skb)
		desc->header |= CFHSI_PIGGY_DESC;
	else
		desc->header &= ~CFHSI_PIGGY_DESC;

	return CFHSI_DESC_SZ + pld_len;
}

static void cfhsi_tx_done_work(struct work_struct *work)
{
	struct cfhsi *cfhsi = NULL;
	struct cfhsi_desc *desc = NULL;
	int len = 0;
	int res;

	cfhsi = container_of(work, struct cfhsi, tx_done_work);
	dev_dbg(&cfhsi->ndev->dev, "%s.\n",
		__func__);

	if (test_bit(CFHSI_SHUTDOWN, &cfhsi->bits))
		return;

	desc = (struct cfhsi_desc *)cfhsi->tx_buf;

	do {
		/*
		 * Send flow on if flow off has been previously signalled
		 * and number of packets is below low water mark.
		 */
		spin_lock_bh(&cfhsi->lock);
		if (cfhsi->flow_off_sent &&
				cfhsi->qhead.qlen <= cfhsi->q_low_mark &&
				cfhsi->cfdev.flowctrl) {

			cfhsi->flow_off_sent = 0;
			cfhsi->cfdev.flowctrl(cfhsi->ndev, ON);
		}
		spin_unlock_bh(&cfhsi->lock);

		/* Create HSI frame. */
		len = cfhsi_tx_frm(desc, cfhsi);
		if (!len) {
			cfhsi->tx_state = CFHSI_TX_STATE_IDLE;
			/* Start inactivity timer. */
			mod_timer(&cfhsi->timer,
					jiffies + CFHSI_INACTIVITY_TOUT);
			break;
		}

		/* Set up new transfer. */
		res = cfhsi->dev->cfhsi_tx(cfhsi->tx_buf, len, cfhsi->dev);
		if (WARN_ON(res < 0)) {
			dev_err(&cfhsi->ndev->dev, "%s: TX error %d.\n",
				__func__, res);
		}
	} while (res < 0);
}

static void cfhsi_tx_done_cb(struct cfhsi_drv *drv)
{
	struct cfhsi *cfhsi;

	cfhsi = container_of(drv, struct cfhsi, drv);
	dev_dbg(&cfhsi->ndev->dev, "%s.\n",
		__func__);

	if (test_bit(CFHSI_SHUTDOWN, &cfhsi->bits))
		return;

	queue_work(cfhsi->wq, &cfhsi->tx_done_work);
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
		return 0;
	}

	/* Check for embedded CAIF frame. */
	if (desc->offset) {
		struct sk_buff *skb;
		u8 *dst = NULL;
		int len = 0, retries = 0;
		pfrm = ((u8 *)desc) + desc->offset;

		/* Remove offset padding. */
		pfrm += *pfrm + 1;

		/* Read length of CAIF frame (little endian). */
		len = *pfrm;
		len |= ((*(pfrm+1)) << 8) & 0xFF00;
		len += 2;	/* Add FCS fields. */


		/* Allocate SKB (OK even in IRQ context). */
		skb = alloc_skb(len + 1, GFP_KERNEL);
		while (!skb) {
			retries++;
			schedule_timeout(1);
			skb = alloc_skb(len + 1, GFP_KERNEL);
			if (skb) {
				printk(KERN_WARNING "%s: slept for %u "
						"before getting memory\n",
						__func__, retries);
				break;
			}
			if (retries > HZ) {
				printk(KERN_ERR "%s: slept for 1HZ and "
						"did not get memory\n",
						__func__);
				cfhsi->ndev->stats.rx_dropped++;
				goto drop_frame;
			}
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

drop_frame:
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

	if (xfer_sz % 4) {
		dev_err(&cfhsi->ndev->dev,
				"%s: Invalid payload len: %d, ignored.\n",
			__func__, xfer_sz);
		xfer_sz = 0;
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
		return -EINVAL;
	}

	/* Set frame pointer to start of payload. */
	pfrm = desc->emb_frm + CFHSI_MAX_EMB_FRM_SZ;
	plen = desc->cffrm_len;
	while (nfrms < CFHSI_MAX_PKTS && *plen) {
		struct sk_buff *skb;
		u8 *dst = NULL;
		u8 *pcffrm = NULL;
		int len = 0, retries = 0;

		if (WARN_ON(desc->cffrm_len[nfrms] > CFHSI_MAX_PAYLOAD_SZ)) {
			dev_err(&cfhsi->ndev->dev, "%s: Invalid payload.\n",
				__func__);
			return -EINVAL;
		}

		/* CAIF frame starts after head padding. */
		pcffrm = pfrm + *pfrm + 1;

		/* Read length of CAIF frame (little endian). */
		len = *pcffrm;
		len |= ((*(pcffrm + 1)) << 8) & 0xFF00;
		len += 2;	/* Add FCS fields. */

		/* Allocate SKB (OK even in IRQ context). */
		skb = alloc_skb(len + 1, GFP_KERNEL);
		while (!skb) {
			retries++;
			schedule_timeout(1);
			skb = alloc_skb(len + 1, GFP_KERNEL);
			if (skb) {
				printk(KERN_WARNING "%s: slept for %u "
						"before getting memory\n",
						__func__, retries);
				break;
			}
			if (retries > HZ) {
				printk(KERN_ERR "%s: slept for 1HZ "
						"and did not get memory\n",
						__func__);
				cfhsi->ndev->stats.rx_dropped++;
				goto drop_frame;
			}
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

drop_frame:
		pfrm += *plen;
		rx_sz += *plen;
		plen++;
		nfrms++;
	}

	return rx_sz;
}

static void cfhsi_rx_done_work(struct work_struct *work)
{
	int res;
	int desc_pld_len = 0;
	struct cfhsi *cfhsi = NULL;
	struct cfhsi_desc *desc = NULL;

	cfhsi = container_of(work, struct cfhsi, rx_done_work);
	desc = (struct cfhsi_desc *)cfhsi->rx_buf;

	dev_dbg(&cfhsi->ndev->dev, "%s: Kick timer if pending.\n",
		__func__);

	if (test_bit(CFHSI_SHUTDOWN, &cfhsi->bits))
		return;

	/* Update inactivity timer if pending. */
	mod_timer_pending(&cfhsi->timer, jiffies + CFHSI_INACTIVITY_TOUT);

	if (cfhsi->rx_state == CFHSI_RX_STATE_DESC) {
		desc_pld_len = cfhsi_rx_desc(desc, cfhsi);
	} else {
		int pld_len;

		pld_len = cfhsi_rx_pld(desc, cfhsi);

		if ((pld_len > 0) && (desc->header & CFHSI_PIGGY_DESC)) {
			struct cfhsi_desc *piggy_desc;
			piggy_desc = (struct cfhsi_desc *)
				(desc->emb_frm + CFHSI_MAX_EMB_FRM_SZ +
						pld_len);

			/* Extract piggy-backed descriptor. */
			desc_pld_len = cfhsi_rx_desc(piggy_desc, cfhsi);

			/*
			 * Copy needed information from the piggy-backed
			 * descriptor to the descriptor in the start.
			 */
			memcpy((u8 *)desc, (u8 *)piggy_desc,
					CFHSI_DESC_SHORT_SZ);
		}
	}

	if (desc_pld_len) {
		cfhsi->rx_state = CFHSI_RX_STATE_PAYLOAD;
		cfhsi->rx_ptr = cfhsi->rx_buf + CFHSI_DESC_SZ;
		cfhsi->rx_len = desc_pld_len;
	} else {
		cfhsi->rx_state = CFHSI_RX_STATE_DESC;
		cfhsi->rx_ptr = cfhsi->rx_buf;
		cfhsi->rx_len = CFHSI_DESC_SZ;
	}
	clear_bit(CFHSI_PENDING_RX, &cfhsi->bits);

	if (test_bit(CFHSI_AWAKE, &cfhsi->bits)) {
		/* Set up new transfer. */
		dev_dbg(&cfhsi->ndev->dev, "%s: Start RX.\n",
			__func__);
		res = cfhsi->dev->cfhsi_rx(cfhsi->rx_ptr, cfhsi->rx_len,
				cfhsi->dev);
		if (WARN_ON(res < 0)) {
			dev_err(&cfhsi->ndev->dev, "%s: RX error %d.\n",
				__func__, res);
			cfhsi->ndev->stats.rx_errors++;
			cfhsi->ndev->stats.rx_dropped++;
		}
	}
}

static void cfhsi_rx_done_cb(struct cfhsi_drv *drv)
{
	struct cfhsi *cfhsi;

	cfhsi = container_of(drv, struct cfhsi, drv);
	dev_dbg(&cfhsi->ndev->dev, "%s.\n",
		__func__);

	if (test_bit(CFHSI_SHUTDOWN, &cfhsi->bits))
		return;

	set_bit(CFHSI_PENDING_RX, &cfhsi->bits);

	if (test_and_clear_bit(CFHSI_FLUSH_FIFO, &cfhsi->bits))
		wake_up_interruptible(&cfhsi->flush_fifo_wait);
	else
		queue_work(cfhsi->wq, &cfhsi->rx_done_work);
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
		return;
	}

	/* Activate wake line. */
	cfhsi->dev->cfhsi_wake_up(cfhsi->dev);

	dev_dbg(&cfhsi->ndev->dev, "%s: Start waiting.\n",
		__func__);

	/* Wait for acknowledge. */
	ret = CFHSI_WAKEUP_TOUT;
	wait_event_interruptible_timeout(cfhsi->wake_up_wait,
					test_bit(CFHSI_WAKE_UP_ACK,
							&cfhsi->bits), ret);
	if (unlikely(ret < 0)) {
		/* Interrupted by signal. */
		dev_info(&cfhsi->ndev->dev, "%s: Signalled: %ld.\n",
			__func__, ret);
		clear_bit(CFHSI_WAKE_UP, &cfhsi->bits);
		cfhsi->dev->cfhsi_wake_down(cfhsi->dev);
		return;
	} else if (!ret) {
		/* Wakeup timeout */
		dev_err(&cfhsi->ndev->dev, "%s: Timeout.\n",
			__func__);
		clear_bit(CFHSI_WAKE_UP, &cfhsi->bits);
		cfhsi->dev->cfhsi_wake_down(cfhsi->dev);
		return;
	}
	dev_dbg(&cfhsi->ndev->dev, "%s: Woken.\n",
		__func__);

	/* Clear power up bit. */
	set_bit(CFHSI_AWAKE, &cfhsi->bits);
	clear_bit(CFHSI_WAKE_UP, &cfhsi->bits);

	/* Resume read operation. */
	if (!test_bit(CFHSI_PENDING_RX, &cfhsi->bits)) {
		dev_dbg(&cfhsi->ndev->dev, "%s: Start RX.\n",
			__func__);
		res = cfhsi->dev->cfhsi_rx(cfhsi->rx_ptr,
				cfhsi->rx_len, cfhsi->dev);
		if (WARN_ON(res < 0)) {
			dev_err(&cfhsi->ndev->dev, "%s: RX error %d.\n",
				__func__, res);
		}
	}

	/* Clear power up acknowledment. */
	clear_bit(CFHSI_WAKE_UP_ACK, &cfhsi->bits);

	spin_lock_bh(&cfhsi->lock);

	/* Resume transmit if queue is not empty. */
	if (!skb_peek(&cfhsi->qhead)) {
		dev_dbg(&cfhsi->ndev->dev, "%s: Peer wake, start timer.\n",
			__func__);
		/* Start inactivity timer. */
		mod_timer(&cfhsi->timer,
				jiffies + CFHSI_INACTIVITY_TOUT);
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
	size_t fifo_occupancy;

	cfhsi = container_of(work, struct cfhsi, wake_down_work);
	dev_dbg(&cfhsi->ndev->dev, "%s.\n",
		__func__);

	if (test_bit(CFHSI_SHUTDOWN, &cfhsi->bits))
		return;

	/* Check if there is something in FIFO. */
	if (WARN_ON(cfhsi->dev->cfhsi_fifo_occupancy(cfhsi->dev,
							&fifo_occupancy)))
		fifo_occupancy = 0;

	if (fifo_occupancy) {
		dev_dbg(&cfhsi->ndev->dev,
				"%s: %u words in RX FIFO, restart timer.\n",
				__func__, (unsigned) fifo_occupancy);
		spin_lock_bh(&cfhsi->lock);
		mod_timer(&cfhsi->timer,
				jiffies + CFHSI_INACTIVITY_TOUT);
		spin_unlock_bh(&cfhsi->lock);
		return;
	}

	/* Cancel pending RX requests */
	cfhsi->dev->cfhsi_rx_cancel(cfhsi->dev);

	/* Deactivate wake line. */
	cfhsi->dev->cfhsi_wake_down(cfhsi->dev);

	/* Wait for acknowledge. */
	ret = CFHSI_WAKEUP_TOUT;
	ret = wait_event_interruptible_timeout(cfhsi->wake_down_wait,
					test_bit(CFHSI_WAKE_DOWN_ACK,
							&cfhsi->bits),
					ret);
	if (ret < 0) {
		/* Interrupted by signal. */
		dev_info(&cfhsi->ndev->dev, "%s: Signalled: %ld.\n",
			__func__, ret);
		return;
	} else if (!ret) {
		/* Timeout */
		dev_err(&cfhsi->ndev->dev, "%s: Timeout.\n",
			__func__);
	}

	/* Clear power down acknowledment. */
	clear_bit(CFHSI_WAKE_DOWN_ACK, &cfhsi->bits);
	clear_bit(CFHSI_AWAKE, &cfhsi->bits);

	/* Check if there is something in FIFO. */
	if (WARN_ON(cfhsi->dev->cfhsi_fifo_occupancy(cfhsi->dev,
							&fifo_occupancy)))
		fifo_occupancy = 0;

	if (fifo_occupancy) {
		dev_dbg(&cfhsi->ndev->dev,
				"%s: %u words in RX FIFO, wakeup forced.\n",
				__func__, (unsigned) fifo_occupancy);
		if (!test_and_set_bit(CFHSI_WAKE_UP, &cfhsi->bits))
			queue_work(cfhsi->wq, &cfhsi->wake_up_work);
	} else
		dev_dbg(&cfhsi->ndev->dev, "%s: Done.\n",
			__func__);
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

static int cfhsi_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct cfhsi *cfhsi = NULL;
	int start_xfer = 0;
	int timer_active;

	if (!dev)
		return -EINVAL;

	cfhsi = netdev_priv(dev);

	spin_lock_bh(&cfhsi->lock);

	skb_queue_tail(&cfhsi->qhead, skb);

	/* Sanity check; xmit should not be called after unregister_netdev */
	if (WARN_ON(test_bit(CFHSI_SHUTDOWN, &cfhsi->bits))) {
		spin_unlock_bh(&cfhsi->lock);
		cfhsi_abort_tx(cfhsi);
		return -EINVAL;
	}

	/* Send flow off if number of packets is above high water mark. */
	if (!cfhsi->flow_off_sent &&
		cfhsi->qhead.qlen > cfhsi->q_high_mark &&
		cfhsi->cfdev.flowctrl) {
		cfhsi->flow_off_sent = 1;
		cfhsi->cfdev.flowctrl(cfhsi->ndev, OFF);
	}

	if (cfhsi->tx_state == CFHSI_TX_STATE_IDLE) {
		cfhsi->tx_state = CFHSI_TX_STATE_XFER;
		start_xfer = 1;
	}

	spin_unlock_bh(&cfhsi->lock);

	if (!start_xfer)
		return 0;

	/* Delete inactivity timer if started. */
#ifdef CONFIG_SMP
	timer_active = del_timer_sync(&cfhsi->timer);
#else
	timer_active = del_timer(&cfhsi->timer);
#endif /* CONFIG_SMP */

	if (timer_active) {
		struct cfhsi_desc *desc = (struct cfhsi_desc *)cfhsi->tx_buf;
		int len;
		int res;

		/* Create HSI frame. */
		len = cfhsi_tx_frm(desc, cfhsi);
		BUG_ON(!len);

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

static int cfhsi_open(struct net_device *dev)
{
	netif_wake_queue(dev);

	return 0;
}

static int cfhsi_close(struct net_device *dev)
{
	netif_stop_queue(dev);

	return 0;
}

static const struct net_device_ops cfhsi_ops = {
	.ndo_open = cfhsi_open,
	.ndo_stop = cfhsi_close,
	.ndo_start_xmit = cfhsi_xmit
};

static void cfhsi_setup(struct net_device *dev)
{
	struct cfhsi *cfhsi = netdev_priv(dev);
	dev->features = 0;
	dev->netdev_ops = &cfhsi_ops;
	dev->type = ARPHRD_CAIF;
	dev->flags = IFF_POINTOPOINT | IFF_NOARP;
	dev->mtu = CFHSI_MAX_PAYLOAD_SZ;
	dev->tx_queue_len = 0;
	dev->destructor = free_netdev;
	skb_queue_head_init(&cfhsi->qhead);
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
	struct cfhsi_dev *dev;
	int res;

	ndev = alloc_netdev(sizeof(struct cfhsi), "cfhsi%d", cfhsi_setup);
	if (!ndev) {
		dev_err(&pdev->dev, "%s: alloc_netdev failed.\n",
			__func__);
		return -ENODEV;
	}

	cfhsi = netdev_priv(ndev);
	cfhsi->ndev = ndev;
	cfhsi->pdev = pdev;

	/* Initialize state vaiables. */
	cfhsi->tx_state = CFHSI_TX_STATE_IDLE;
	cfhsi->rx_state = CFHSI_RX_STATE_DESC;

	/* Set flow info */
	cfhsi->flow_off_sent = 0;
	cfhsi->q_low_mark = LOW_WATER_MARK;
	cfhsi->q_high_mark = HIGH_WATER_MARK;

	/* Assign the HSI device. */
	dev = (struct cfhsi_dev *)pdev->dev.platform_data;
	cfhsi->dev = dev;

	/* Assign the driver to this HSI device. */
	dev->drv = &cfhsi->drv;

	/*
	 * Allocate a TX buffer with the size of a HSI packet descriptors
	 * and the necessary room for CAIF payload frames.
	 */
	cfhsi->tx_buf = kzalloc(CFHSI_BUF_SZ_TX, GFP_KERNEL);
	if (!cfhsi->tx_buf) {
		dev_err(&ndev->dev, "%s: Failed to allocate TX buffer.\n",
			__func__);
		res = -ENODEV;
		goto err_alloc_tx;
	}

	/*
	 * Allocate a RX buffer with the size of two HSI packet descriptors and
	 * the necessary room for CAIF payload frames.
	 */
	cfhsi->rx_buf = kzalloc(CFHSI_BUF_SZ_RX, GFP_KERNEL);
	if (!cfhsi->rx_buf) {
		dev_err(&ndev->dev, "%s: Failed to allocate RX buffer.\n",
			__func__);
		res = -ENODEV;
		goto err_alloc_rx;
	}

	/* Initialize receive variables. */
	cfhsi->rx_ptr = cfhsi->rx_buf;
	cfhsi->rx_len = CFHSI_DESC_SZ;

	/* Initialize spin locks. */
	spin_lock_init(&cfhsi->lock);

	/* Set up the driver. */
	cfhsi->drv.tx_done_cb = cfhsi_tx_done_cb;
	cfhsi->drv.rx_done_cb = cfhsi_rx_done_cb;

	/* Initialize the work queues. */
	INIT_WORK(&cfhsi->wake_up_work, cfhsi_wake_up);
	INIT_WORK(&cfhsi->wake_down_work, cfhsi_wake_down);
	INIT_WORK(&cfhsi->rx_done_work, cfhsi_rx_done_work);
	INIT_WORK(&cfhsi->tx_done_work, cfhsi_tx_done_work);

	/* Clear all bit fields. */
	clear_bit(CFHSI_WAKE_UP_ACK, &cfhsi->bits);
	clear_bit(CFHSI_WAKE_DOWN_ACK, &cfhsi->bits);
	clear_bit(CFHSI_WAKE_UP, &cfhsi->bits);
	clear_bit(CFHSI_AWAKE, &cfhsi->bits);
	clear_bit(CFHSI_PENDING_RX, &cfhsi->bits);

	/* Create work thread. */
	cfhsi->wq = create_singlethread_workqueue(pdev->name);
	if (!cfhsi->wq) {
		dev_err(&ndev->dev, "%s: Failed to create work queue.\n",
			__func__);
		res = -ENODEV;
		goto err_create_wq;
	}

	/* Initialize wait queues. */
	init_waitqueue_head(&cfhsi->wake_up_wait);
	init_waitqueue_head(&cfhsi->wake_down_wait);
	init_waitqueue_head(&cfhsi->flush_fifo_wait);

	/* Setup the inactivity timer. */
	init_timer(&cfhsi->timer);
	cfhsi->timer.data = (unsigned long)cfhsi;
	cfhsi->timer.function = cfhsi_inactivity_tout;

	/* Add CAIF HSI device to list. */
	spin_lock(&cfhsi_list_lock);
	list_add_tail(&cfhsi->list, &cfhsi_list);
	spin_unlock(&cfhsi_list_lock);

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
		dev_err(&ndev->dev, "%s: Can't flush FIFO: %d.\n",
			__func__, res);
		goto err_net_reg;
	}

	cfhsi->drv.wake_up_cb = cfhsi_wake_up_cb;
	cfhsi->drv.wake_down_cb = cfhsi_wake_down_cb;

	/* Register network device. */
	res = register_netdev(ndev);
	if (res) {
		dev_err(&ndev->dev, "%s: Registration error: %d.\n",
			__func__, res);
		goto err_net_reg;
	}

	netif_stop_queue(ndev);

	return res;

 err_net_reg:
	cfhsi->dev->cfhsi_down(cfhsi->dev);
 err_activate:
	destroy_workqueue(cfhsi->wq);
 err_create_wq:
	kfree(cfhsi->rx_buf);
 err_alloc_rx:
	kfree(cfhsi->tx_buf);
 err_alloc_tx:
	free_netdev(ndev);

	return res;
}

static void cfhsi_shutdown(struct cfhsi *cfhsi, bool remove_platform_dev)
{
	u8 *tx_buf, *rx_buf;

	/* Stop TXing */
	netif_tx_stop_all_queues(cfhsi->ndev);

	/* going to shutdown driver */
	set_bit(CFHSI_SHUTDOWN, &cfhsi->bits);

	if (remove_platform_dev) {
		/* Flush workqueue */
		flush_workqueue(cfhsi->wq);

		/* Notify device. */
		platform_device_unregister(cfhsi->pdev);
	}

	/* Flush workqueue */
	flush_workqueue(cfhsi->wq);

	/* Delete timer if pending */
#ifdef CONFIG_SMP
	del_timer_sync(&cfhsi->timer);
#else
	del_timer(&cfhsi->timer);
#endif /* CONFIG_SMP */

	/* Cancel pending RX request (if any) */
	cfhsi->dev->cfhsi_rx_cancel(cfhsi->dev);

	/* Flush again and destroy workqueue */
	destroy_workqueue(cfhsi->wq);

	/* Store bufferes: will be freed later. */
	tx_buf = cfhsi->tx_buf;
	rx_buf = cfhsi->rx_buf;

	/* Flush transmit queues. */
	cfhsi_abort_tx(cfhsi);

	/* Deactivate interface */
	cfhsi->dev->cfhsi_down(cfhsi->dev);

	/* Finally unregister the network device. */
	unregister_netdev(cfhsi->ndev);

	/* Free buffers. */
	kfree(tx_buf);
	kfree(rx_buf);
}

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

			/* Shutdown driver. */
			cfhsi_shutdown(cfhsi, false);

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

		/* Shutdown driver. */
		cfhsi_shutdown(cfhsi, true);

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

	return result;

 err_dev_register:
	return result;
}

module_init(cfhsi_init_module);
module_exit(cfhsi_exit_module);
