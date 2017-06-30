/**
 * 1588 PTP support for Cadence GEM device.
 *
 * Copyright (C) 2017 Cadence Design Systems - http://www.cadence.com
 *
 * Authors: Rafal Ozieblo <rafalo@cadence.com>
 *          Bartosz Folta <bfolta@cadence.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2  of
 * the License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/etherdevice.h>
#include <linux/platform_device.h>
#include <linux/time64.h>
#include <linux/ptp_classify.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/net_tstamp.h>
#include <linux/circ_buf.h>
#include <linux/spinlock.h>

#include "macb.h"

#define  GEM_PTP_TIMER_NAME "gem-ptp-timer"

static struct macb_dma_desc_ptp *macb_ptp_desc(struct macb *bp,
					       struct macb_dma_desc *desc)
{
	if (bp->hw_dma_cap == HW_DMA_CAP_PTP)
		return (struct macb_dma_desc_ptp *)
				((u8 *)desc + sizeof(struct macb_dma_desc));
	if (bp->hw_dma_cap == HW_DMA_CAP_64B_PTP)
		return (struct macb_dma_desc_ptp *)
				((u8 *)desc + sizeof(struct macb_dma_desc)
				+ sizeof(struct macb_dma_desc_64));
	return NULL;
}

static int gem_tsu_get_time(struct ptp_clock_info *ptp, struct timespec64 *ts)
{
	struct macb *bp = container_of(ptp, struct macb, ptp_clock_info);
	unsigned long flags;
	long first, second;
	u32 secl, sech;

	spin_lock_irqsave(&bp->tsu_clk_lock, flags);
	first = gem_readl(bp, TN);
	secl = gem_readl(bp, TSL);
	sech = gem_readl(bp, TSH);
	second = gem_readl(bp, TN);

	/* test for nsec rollover */
	if (first > second) {
		/* if so, use later read & re-read seconds
		 * (assume all done within 1s)
		 */
		ts->tv_nsec = gem_readl(bp, TN);
		secl = gem_readl(bp, TSL);
		sech = gem_readl(bp, TSH);
	} else {
		ts->tv_nsec = first;
	}

	spin_unlock_irqrestore(&bp->tsu_clk_lock, flags);
	ts->tv_sec = (((u64)sech << GEM_TSL_SIZE) | secl)
			& TSU_SEC_MAX_VAL;
	return 0;
}

static int gem_tsu_set_time(struct ptp_clock_info *ptp,
			    const struct timespec64 *ts)
{
	struct macb *bp = container_of(ptp, struct macb, ptp_clock_info);
	unsigned long flags;
	u32 ns, sech, secl;

	secl = (u32)ts->tv_sec;
	sech = (ts->tv_sec >> GEM_TSL_SIZE) & ((1 << GEM_TSH_SIZE) - 1);
	ns = ts->tv_nsec;

	spin_lock_irqsave(&bp->tsu_clk_lock, flags);

	/* TSH doesn't latch the time and no atomicity! */
	gem_writel(bp, TN, 0); /* clear to avoid overflow */
	gem_writel(bp, TSH, sech);
	/* write lower bits 2nd, for synchronized secs update */
	gem_writel(bp, TSL, secl);
	gem_writel(bp, TN, ns);

	spin_unlock_irqrestore(&bp->tsu_clk_lock, flags);

	return 0;
}

static int gem_tsu_incr_set(struct macb *bp, struct tsu_incr *incr_spec)
{
	unsigned long flags;

	/* tsu_timer_incr register must be written after
	 * the tsu_timer_incr_sub_ns register and the write operation
	 * will cause the value written to the tsu_timer_incr_sub_ns register
	 * to take effect.
	 */
	spin_lock_irqsave(&bp->tsu_clk_lock, flags);
	gem_writel(bp, TISUBN, GEM_BF(SUBNSINCR, incr_spec->sub_ns));
	gem_writel(bp, TI, GEM_BF(NSINCR, incr_spec->ns));
	spin_unlock_irqrestore(&bp->tsu_clk_lock, flags);

	return 0;
}

static int gem_ptp_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct macb *bp = container_of(ptp, struct macb, ptp_clock_info);
	struct tsu_incr incr_spec;
	bool neg_adj = false;
	u32 word;
	u64 adj;

	if (scaled_ppm < 0) {
		neg_adj = true;
		scaled_ppm = -scaled_ppm;
	}

	/* Adjustment is relative to base frequency */
	incr_spec.sub_ns = bp->tsu_incr.sub_ns;
	incr_spec.ns = bp->tsu_incr.ns;

	/* scaling: unused(8bit) | ns(8bit) | fractions(16bit) */
	word = ((u64)incr_spec.ns << GEM_SUBNSINCR_SIZE) + incr_spec.sub_ns;
	adj = (u64)scaled_ppm * word;
	/* Divide with rounding, equivalent to floating dividing:
	 * (temp / USEC_PER_SEC) + 0.5
	 */
	adj += (USEC_PER_SEC >> 1);
	adj >>= GEM_SUBNSINCR_SIZE; /* remove fractions */
	adj = div_u64(adj, USEC_PER_SEC);
	adj = neg_adj ? (word - adj) : (word + adj);

	incr_spec.ns = (adj >> GEM_SUBNSINCR_SIZE)
			& ((1 << GEM_NSINCR_SIZE) - 1);
	incr_spec.sub_ns = adj & ((1 << GEM_SUBNSINCR_SIZE) - 1);
	gem_tsu_incr_set(bp, &incr_spec);
	return 0;
}

static int gem_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct macb *bp = container_of(ptp, struct macb, ptp_clock_info);
	struct timespec64 now, then = ns_to_timespec64(delta);
	u32 adj, sign = 0;

	if (delta < 0) {
		sign = 1;
		delta = -delta;
	}

	if (delta > TSU_NSEC_MAX_VAL) {
		gem_tsu_get_time(&bp->ptp_clock_info, &now);
		if (sign)
			now = timespec64_sub(now, then);
		else
			now = timespec64_add(now, then);

		gem_tsu_set_time(&bp->ptp_clock_info,
				 (const struct timespec64 *)&now);
	} else {
		adj = (sign << GEM_ADDSUB_OFFSET) | delta;

		gem_writel(bp, TA, adj);
	}

	return 0;
}

static int gem_ptp_enable(struct ptp_clock_info *ptp,
			  struct ptp_clock_request *rq, int on)
{
	return -EOPNOTSUPP;
}

static struct ptp_clock_info gem_ptp_caps_template = {
	.owner		= THIS_MODULE,
	.name		= GEM_PTP_TIMER_NAME,
	.max_adj	= 0,
	.n_alarm	= 0,
	.n_ext_ts	= 0,
	.n_per_out	= 0,
	.n_pins		= 0,
	.pps		= 1,
	.adjfine	= gem_ptp_adjfine,
	.adjtime	= gem_ptp_adjtime,
	.gettime64	= gem_tsu_get_time,
	.settime64	= gem_tsu_set_time,
	.enable		= gem_ptp_enable,
};

static void gem_ptp_init_timer(struct macb *bp)
{
	u32 rem = 0;
	u64 adj;

	bp->tsu_incr.ns = div_u64_rem(NSEC_PER_SEC, bp->tsu_rate, &rem);
	if (rem) {
		adj = rem;
		adj <<= GEM_SUBNSINCR_SIZE;
		bp->tsu_incr.sub_ns = div_u64(adj, bp->tsu_rate);
	} else {
		bp->tsu_incr.sub_ns = 0;
	}
}

static void gem_ptp_init_tsu(struct macb *bp)
{
	struct timespec64 ts;

	/* 1. get current system time */
	ts = ns_to_timespec64(ktime_to_ns(ktime_get_real()));

	/* 2. set ptp timer */
	gem_tsu_set_time(&bp->ptp_clock_info, &ts);

	/* 3. set PTP timer increment value to BASE_INCREMENT */
	gem_tsu_incr_set(bp, &bp->tsu_incr);

	gem_writel(bp, TA, 0);
}

static void gem_ptp_clear_timer(struct macb *bp)
{
	bp->tsu_incr.sub_ns = 0;
	bp->tsu_incr.ns = 0;

	gem_writel(bp, TISUBN, GEM_BF(SUBNSINCR, 0));
	gem_writel(bp, TI, GEM_BF(NSINCR, 0));
	gem_writel(bp, TA, 0);
}

static int gem_hw_timestamp(struct macb *bp, u32 dma_desc_ts_1,
			    u32 dma_desc_ts_2, struct timespec64 *ts)
{
	struct timespec64 tsu;

	ts->tv_sec = (GEM_BFEXT(DMA_SECH, dma_desc_ts_2) << GEM_DMA_SECL_SIZE) |
			GEM_BFEXT(DMA_SECL, dma_desc_ts_1);
	ts->tv_nsec = GEM_BFEXT(DMA_NSEC, dma_desc_ts_1);

	/* TSU overlapping workaround
	 * The timestamp only contains lower few bits of seconds,
	 * so add value from 1588 timer
	 */
	gem_tsu_get_time(&bp->ptp_clock_info, &tsu);

	/* If the top bit is set in the timestamp,
	 * but not in 1588 timer, it has rolled over,
	 * so subtract max size
	 */
	if ((ts->tv_sec & (GEM_DMA_SEC_TOP >> 1)) &&
	    !(tsu.tv_sec & (GEM_DMA_SEC_TOP >> 1)))
		ts->tv_sec -= GEM_DMA_SEC_TOP;

	ts->tv_sec += ((~GEM_DMA_SEC_MASK) & tsu.tv_sec);

	return 0;
}

void gem_ptp_rxstamp(struct macb *bp, struct sk_buff *skb,
		     struct macb_dma_desc *desc)
{
	struct skb_shared_hwtstamps *shhwtstamps = skb_hwtstamps(skb);
	struct macb_dma_desc_ptp *desc_ptp;
	struct timespec64 ts;

	if (GEM_BFEXT(DMA_RXVALID, desc->addr)) {
		desc_ptp = macb_ptp_desc(bp, desc);
		gem_hw_timestamp(bp, desc_ptp->ts_1, desc_ptp->ts_2, &ts);
		memset(shhwtstamps, 0, sizeof(struct skb_shared_hwtstamps));
		shhwtstamps->hwtstamp = ktime_set(ts.tv_sec, ts.tv_nsec);
	}
}

static void gem_tstamp_tx(struct macb *bp, struct sk_buff *skb,
			  struct macb_dma_desc_ptp *desc_ptp)
{
	struct skb_shared_hwtstamps shhwtstamps;
	struct timespec64 ts;

	gem_hw_timestamp(bp, desc_ptp->ts_1, desc_ptp->ts_2, &ts);
	memset(&shhwtstamps, 0, sizeof(shhwtstamps));
	shhwtstamps.hwtstamp = ktime_set(ts.tv_sec, ts.tv_nsec);
	skb_tstamp_tx(skb, &shhwtstamps);
}

int gem_ptp_txstamp(struct macb_queue *queue, struct sk_buff *skb,
		    struct macb_dma_desc *desc)
{
	unsigned long tail = READ_ONCE(queue->tx_ts_tail);
	unsigned long head = queue->tx_ts_head;
	struct macb_dma_desc_ptp *desc_ptp;
	struct gem_tx_ts *tx_timestamp;

	if (!GEM_BFEXT(DMA_TXVALID, desc->ctrl))
		return -EINVAL;

	if (CIRC_SPACE(head, tail, PTP_TS_BUFFER_SIZE) == 0)
		return -ENOMEM;

	skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
	desc_ptp = macb_ptp_desc(queue->bp, desc);
	tx_timestamp = &queue->tx_timestamps[head];
	tx_timestamp->skb = skb;
	tx_timestamp->desc_ptp.ts_1 = desc_ptp->ts_1;
	tx_timestamp->desc_ptp.ts_2 = desc_ptp->ts_2;
	/* move head */
	smp_store_release(&queue->tx_ts_head,
			  (head + 1) & (PTP_TS_BUFFER_SIZE - 1));

	schedule_work(&queue->tx_ts_task);
	return 0;
}

static void gem_tx_timestamp_flush(struct work_struct *work)
{
	struct macb_queue *queue =
			container_of(work, struct macb_queue, tx_ts_task);
	unsigned long head, tail;
	struct gem_tx_ts *tx_ts;

	/* take current head */
	head = smp_load_acquire(&queue->tx_ts_head);
	tail = queue->tx_ts_tail;

	while (CIRC_CNT(head, tail, PTP_TS_BUFFER_SIZE)) {
		tx_ts = &queue->tx_timestamps[tail];
		gem_tstamp_tx(queue->bp, tx_ts->skb, &tx_ts->desc_ptp);
		/* cleanup */
		dev_kfree_skb_any(tx_ts->skb);
		/* remove old tail */
		smp_store_release(&queue->tx_ts_tail,
				  (tail + 1) & (PTP_TS_BUFFER_SIZE - 1));
		tail = queue->tx_ts_tail;
	}
}

void gem_ptp_init(struct net_device *dev)
{
	struct macb *bp = netdev_priv(dev);
	struct macb_queue *queue;
	unsigned int q;

	bp->ptp_clock_info = gem_ptp_caps_template;

	/* nominal frequency and maximum adjustment in ppb */
	bp->tsu_rate = bp->ptp_info->get_tsu_rate(bp);
	bp->ptp_clock_info.max_adj = bp->ptp_info->get_ptp_max_adj();
	gem_ptp_init_timer(bp);
	bp->ptp_clock = ptp_clock_register(&bp->ptp_clock_info, &dev->dev);
	if (IS_ERR(bp->ptp_clock)) {
		pr_err("ptp clock register failed: %ld\n",
			PTR_ERR(bp->ptp_clock));
		bp->ptp_clock = NULL;
		return;
	} else if (bp->ptp_clock == NULL) {
		pr_err("ptp clock register failed\n");
		return;
	}

	spin_lock_init(&bp->tsu_clk_lock);
	for (q = 0, queue = bp->queues; q < bp->num_queues; ++q, ++queue) {
		queue->tx_ts_head = 0;
		queue->tx_ts_tail = 0;
		INIT_WORK(&queue->tx_ts_task, gem_tx_timestamp_flush);
	}

	gem_ptp_init_tsu(bp);

	dev_info(&bp->pdev->dev, "%s ptp clock registered.\n",
		 GEM_PTP_TIMER_NAME);
}

void gem_ptp_remove(struct net_device *ndev)
{
	struct macb *bp = netdev_priv(ndev);

	if (bp->ptp_clock)
		ptp_clock_unregister(bp->ptp_clock);

	gem_ptp_clear_timer(bp);

	dev_info(&bp->pdev->dev, "%s ptp clock unregistered.\n",
		 GEM_PTP_TIMER_NAME);
}

static int gem_ptp_set_ts_mode(struct macb *bp,
			       enum macb_bd_control tx_bd_control,
			       enum macb_bd_control rx_bd_control)
{
	gem_writel(bp, TXBDCTRL, GEM_BF(TXTSMODE, tx_bd_control));
	gem_writel(bp, RXBDCTRL, GEM_BF(RXTSMODE, rx_bd_control));

	return 0;
}

int gem_get_hwtst(struct net_device *dev, struct ifreq *rq)
{
	struct hwtstamp_config *tstamp_config;
	struct macb *bp = netdev_priv(dev);

	tstamp_config = &bp->tstamp_config;
	if ((bp->hw_dma_cap & HW_DMA_CAP_PTP) == 0)
		return -EOPNOTSUPP;

	if (copy_to_user(rq->ifr_data, tstamp_config, sizeof(*tstamp_config)))
		return -EFAULT;
	else
		return 0;
}

static int gem_ptp_set_one_step_sync(struct macb *bp, u8 enable)
{
	u32 reg_val;

	reg_val = macb_readl(bp, NCR);

	if (enable)
		macb_writel(bp, NCR, reg_val | MACB_BIT(OSSMODE));
	else
		macb_writel(bp, NCR, reg_val & ~MACB_BIT(OSSMODE));

	return 0;
}

int gem_set_hwtst(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	enum macb_bd_control tx_bd_control = TSTAMP_DISABLED;
	enum macb_bd_control rx_bd_control = TSTAMP_DISABLED;
	struct hwtstamp_config *tstamp_config;
	struct macb *bp = netdev_priv(dev);
	u32 regval;

	tstamp_config = &bp->tstamp_config;
	if ((bp->hw_dma_cap & HW_DMA_CAP_PTP) == 0)
		return -EOPNOTSUPP;

	if (copy_from_user(tstamp_config, ifr->ifr_data,
			   sizeof(*tstamp_config)))
		return -EFAULT;

	/* reserved for future extensions */
	if (tstamp_config->flags)
		return -EINVAL;

	switch (tstamp_config->tx_type) {
	case HWTSTAMP_TX_OFF:
		break;
	case HWTSTAMP_TX_ONESTEP_SYNC:
		if (gem_ptp_set_one_step_sync(bp, 1) != 0)
			return -ERANGE;
	case HWTSTAMP_TX_ON:
		tx_bd_control = TSTAMP_ALL_FRAMES;
		break;
	default:
		return -ERANGE;
	}

	switch (tstamp_config->rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
		break;
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
		rx_bd_control =  TSTAMP_ALL_PTP_FRAMES;
		tstamp_config->rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;
		regval = macb_readl(bp, NCR);
		macb_writel(bp, NCR, (regval | MACB_BIT(SRTSM)));
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
	case HWTSTAMP_FILTER_ALL:
		rx_bd_control = TSTAMP_ALL_FRAMES;
		tstamp_config->rx_filter = HWTSTAMP_FILTER_ALL;
		break;
	default:
		tstamp_config->rx_filter = HWTSTAMP_FILTER_NONE;
		return -ERANGE;
	}

	if (gem_ptp_set_ts_mode(bp, tx_bd_control, rx_bd_control) != 0)
		return -ERANGE;

	if (copy_to_user(ifr->ifr_data, tstamp_config, sizeof(*tstamp_config)))
		return -EFAULT;
	else
		return 0;
}

