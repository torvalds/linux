/*
 * TI Common Platform Time Sync
 *
 * Copyright (C) 2012 Richard Cochran <richardcochran@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <linux/err.h>
#include <linux/if.h>
#include <linux/hrtimer.h>
#include <linux/module.h>
#include <linux/net_tstamp.h>
#include <linux/ptp_classify.h>
#include <linux/time.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>

#include "cpts.h"

#ifdef CONFIG_TI_CPTS

static struct sock_filter ptp_filter[] = {
	PTP_FILTER
};

#define cpts_read32(c, r)	__raw_readl(&c->reg->r)
#define cpts_write32(c, v, r)	__raw_writel(v, &c->reg->r)

static int event_expired(struct cpts_event *event)
{
	return time_after(jiffies, event->tmo);
}

static int event_type(struct cpts_event *event)
{
	return (event->high >> EVENT_TYPE_SHIFT) & EVENT_TYPE_MASK;
}

static int cpts_fifo_pop(struct cpts *cpts, u32 *high, u32 *low)
{
	u32 r = cpts_read32(cpts, intstat_raw);

	if (r & TS_PEND_RAW) {
		*high = cpts_read32(cpts, event_high);
		*low  = cpts_read32(cpts, event_low);
		cpts_write32(cpts, EVENT_POP, event_pop);
		return 0;
	}
	return -1;
}

/*
 * Returns zero if matching event type was found.
 */
static int cpts_fifo_read(struct cpts *cpts, int match)
{
	int i, type = -1;
	u32 hi, lo;
	struct cpts_event *event;

	for (i = 0; i < CPTS_FIFO_DEPTH; i++) {
		if (cpts_fifo_pop(cpts, &hi, &lo))
			break;
		if (list_empty(&cpts->pool)) {
			pr_err("cpts: event pool is empty\n");
			return -1;
		}
		event = list_first_entry(&cpts->pool, struct cpts_event, list);
		event->tmo = jiffies + 2;
		event->high = hi;
		event->low = lo;
		type = event_type(event);
		switch (type) {
		case CPTS_EV_PUSH:
		case CPTS_EV_RX:
		case CPTS_EV_TX:
			list_del_init(&event->list);
			list_add_tail(&event->list, &cpts->events);
			break;
		case CPTS_EV_ROLL:
		case CPTS_EV_HALF:
		case CPTS_EV_HW:
			break;
		default:
			pr_err("cpts: unknown event type\n");
			break;
		}
		if (type == match)
			break;
	}
	return type == match ? 0 : -1;
}

static cycle_t cpts_systim_read(const struct cyclecounter *cc)
{
	u64 val = 0;
	struct cpts_event *event;
	struct list_head *this, *next;
	struct cpts *cpts = container_of(cc, struct cpts, cc);

	cpts_write32(cpts, TS_PUSH, ts_push);
	if (cpts_fifo_read(cpts, CPTS_EV_PUSH))
		pr_err("cpts: unable to obtain a time stamp\n");

	list_for_each_safe(this, next, &cpts->events) {
		event = list_entry(this, struct cpts_event, list);
		if (event_type(event) == CPTS_EV_PUSH) {
			list_del_init(&event->list);
			list_add(&event->list, &cpts->pool);
			val = event->low;
			break;
		}
	}

	return val;
}

/* PTP clock operations */

static int cpts_ptp_adjfreq(struct ptp_clock_info *ptp, s32 ppb)
{
	u64 adj;
	u32 diff, mult;
	int neg_adj = 0;
	unsigned long flags;
	struct cpts *cpts = container_of(ptp, struct cpts, info);

	if (ppb < 0) {
		neg_adj = 1;
		ppb = -ppb;
	}
	mult = cpts->cc_mult;
	adj = mult;
	adj *= ppb;
	diff = div_u64(adj, 1000000000ULL);

	spin_lock_irqsave(&cpts->lock, flags);

	timecounter_read(&cpts->tc);

	cpts->cc.mult = neg_adj ? mult - diff : mult + diff;

	spin_unlock_irqrestore(&cpts->lock, flags);

	return 0;
}

static int cpts_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	s64 now;
	unsigned long flags;
	struct cpts *cpts = container_of(ptp, struct cpts, info);

	spin_lock_irqsave(&cpts->lock, flags);
	now = timecounter_read(&cpts->tc);
	now += delta;
	timecounter_init(&cpts->tc, &cpts->cc, now);
	spin_unlock_irqrestore(&cpts->lock, flags);

	return 0;
}

static int cpts_ptp_gettime(struct ptp_clock_info *ptp, struct timespec *ts)
{
	u64 ns;
	u32 remainder;
	unsigned long flags;
	struct cpts *cpts = container_of(ptp, struct cpts, info);

	spin_lock_irqsave(&cpts->lock, flags);
	ns = timecounter_read(&cpts->tc);
	spin_unlock_irqrestore(&cpts->lock, flags);

	ts->tv_sec = div_u64_rem(ns, 1000000000, &remainder);
	ts->tv_nsec = remainder;

	return 0;
}

static int cpts_ptp_settime(struct ptp_clock_info *ptp,
			    const struct timespec *ts)
{
	u64 ns;
	unsigned long flags;
	struct cpts *cpts = container_of(ptp, struct cpts, info);

	ns = ts->tv_sec * 1000000000ULL;
	ns += ts->tv_nsec;

	spin_lock_irqsave(&cpts->lock, flags);
	timecounter_init(&cpts->tc, &cpts->cc, ns);
	spin_unlock_irqrestore(&cpts->lock, flags);

	return 0;
}

static int cpts_ptp_enable(struct ptp_clock_info *ptp,
			   struct ptp_clock_request *rq, int on)
{
	return -EOPNOTSUPP;
}

static struct ptp_clock_info cpts_info = {
	.owner		= THIS_MODULE,
	.name		= "CTPS timer",
	.max_adj	= 1000000,
	.n_ext_ts	= 0,
	.pps		= 0,
	.adjfreq	= cpts_ptp_adjfreq,
	.adjtime	= cpts_ptp_adjtime,
	.gettime	= cpts_ptp_gettime,
	.settime	= cpts_ptp_settime,
	.enable		= cpts_ptp_enable,
};

static void cpts_overflow_check(struct work_struct *work)
{
	struct timespec ts;
	struct cpts *cpts = container_of(work, struct cpts, overflow_work.work);

	cpts_write32(cpts, CPTS_EN, control);
	cpts_write32(cpts, TS_PEND_EN, int_enable);
	cpts_ptp_gettime(&cpts->info, &ts);
	pr_debug("cpts overflow check at %ld.%09lu\n", ts.tv_sec, ts.tv_nsec);
	schedule_delayed_work(&cpts->overflow_work, CPTS_OVERFLOW_PERIOD);
}

#define CPTS_REF_CLOCK_NAME "cpsw_cpts_rft_clk"

static void cpts_clk_init(struct cpts *cpts)
{
	cpts->refclk = clk_get(NULL, CPTS_REF_CLOCK_NAME);
	if (IS_ERR(cpts->refclk)) {
		pr_err("Failed to clk_get %s\n", CPTS_REF_CLOCK_NAME);
		cpts->refclk = NULL;
		return;
	}
	clk_prepare_enable(cpts->refclk);
}

static void cpts_clk_release(struct cpts *cpts)
{
	clk_disable(cpts->refclk);
	clk_put(cpts->refclk);
}

static int cpts_match(struct sk_buff *skb, unsigned int ptp_class,
		      u16 ts_seqid, u8 ts_msgtype)
{
	u16 *seqid;
	unsigned int offset;
	u8 *msgtype, *data = skb->data;

	switch (ptp_class) {
	case PTP_CLASS_V1_IPV4:
	case PTP_CLASS_V2_IPV4:
		offset = ETH_HLEN + IPV4_HLEN(data) + UDP_HLEN;
		break;
	case PTP_CLASS_V1_IPV6:
	case PTP_CLASS_V2_IPV6:
		offset = OFF_PTP6;
		break;
	case PTP_CLASS_V2_L2:
		offset = ETH_HLEN;
		break;
	case PTP_CLASS_V2_VLAN:
		offset = ETH_HLEN + VLAN_HLEN;
		break;
	default:
		return 0;
	}

	if (skb->len + ETH_HLEN < offset + OFF_PTP_SEQUENCE_ID + sizeof(*seqid))
		return 0;

	if (unlikely(ptp_class & PTP_CLASS_V1))
		msgtype = data + offset + OFF_PTP_CONTROL;
	else
		msgtype = data + offset;

	seqid = (u16 *)(data + offset + OFF_PTP_SEQUENCE_ID);

	return (ts_msgtype == (*msgtype & 0xf) && ts_seqid == ntohs(*seqid));
}

static u64 cpts_find_ts(struct cpts *cpts, struct sk_buff *skb, int ev_type)
{
	u64 ns = 0;
	struct cpts_event *event;
	struct list_head *this, *next;
	unsigned int class = sk_run_filter(skb, ptp_filter);
	unsigned long flags;
	u16 seqid;
	u8 mtype;

	if (class == PTP_CLASS_NONE)
		return 0;

	spin_lock_irqsave(&cpts->lock, flags);
	cpts_fifo_read(cpts, CPTS_EV_PUSH);
	list_for_each_safe(this, next, &cpts->events) {
		event = list_entry(this, struct cpts_event, list);
		if (event_expired(event)) {
			list_del_init(&event->list);
			list_add(&event->list, &cpts->pool);
			continue;
		}
		mtype = (event->high >> MESSAGE_TYPE_SHIFT) & MESSAGE_TYPE_MASK;
		seqid = (event->high >> SEQUENCE_ID_SHIFT) & SEQUENCE_ID_MASK;
		if (ev_type == event_type(event) &&
		    cpts_match(skb, class, seqid, mtype)) {
			ns = timecounter_cyc2time(&cpts->tc, event->low);
			list_del_init(&event->list);
			list_add(&event->list, &cpts->pool);
			break;
		}
	}
	spin_unlock_irqrestore(&cpts->lock, flags);

	return ns;
}

void cpts_rx_timestamp(struct cpts *cpts, struct sk_buff *skb)
{
	u64 ns;
	struct skb_shared_hwtstamps *ssh;

	if (!cpts->rx_enable)
		return;
	ns = cpts_find_ts(cpts, skb, CPTS_EV_RX);
	if (!ns)
		return;
	ssh = skb_hwtstamps(skb);
	memset(ssh, 0, sizeof(*ssh));
	ssh->hwtstamp = ns_to_ktime(ns);
}

void cpts_tx_timestamp(struct cpts *cpts, struct sk_buff *skb)
{
	u64 ns;
	struct skb_shared_hwtstamps ssh;

	if (!(skb_shinfo(skb)->tx_flags & SKBTX_IN_PROGRESS))
		return;
	ns = cpts_find_ts(cpts, skb, CPTS_EV_TX);
	if (!ns)
		return;
	memset(&ssh, 0, sizeof(ssh));
	ssh.hwtstamp = ns_to_ktime(ns);
	skb_tstamp_tx(skb, &ssh);
}

#endif /*CONFIG_TI_CPTS*/

int cpts_register(struct device *dev, struct cpts *cpts,
		  u32 mult, u32 shift)
{
#ifdef CONFIG_TI_CPTS
	int err, i;
	unsigned long flags;

	if (ptp_filter_init(ptp_filter, ARRAY_SIZE(ptp_filter))) {
		pr_err("cpts: bad ptp filter\n");
		return -EINVAL;
	}
	cpts->info = cpts_info;
	cpts->clock = ptp_clock_register(&cpts->info, dev);
	if (IS_ERR(cpts->clock)) {
		err = PTR_ERR(cpts->clock);
		cpts->clock = NULL;
		return err;
	}
	spin_lock_init(&cpts->lock);

	cpts->cc.read = cpts_systim_read;
	cpts->cc.mask = CLOCKSOURCE_MASK(32);
	cpts->cc_mult = mult;
	cpts->cc.mult = mult;
	cpts->cc.shift = shift;

	INIT_LIST_HEAD(&cpts->events);
	INIT_LIST_HEAD(&cpts->pool);
	for (i = 0; i < CPTS_MAX_EVENTS; i++)
		list_add(&cpts->pool_data[i].list, &cpts->pool);

	cpts_clk_init(cpts);
	cpts_write32(cpts, CPTS_EN, control);
	cpts_write32(cpts, TS_PEND_EN, int_enable);

	spin_lock_irqsave(&cpts->lock, flags);
	timecounter_init(&cpts->tc, &cpts->cc, ktime_to_ns(ktime_get_real()));
	spin_unlock_irqrestore(&cpts->lock, flags);

	INIT_DELAYED_WORK(&cpts->overflow_work, cpts_overflow_check);
	schedule_delayed_work(&cpts->overflow_work, CPTS_OVERFLOW_PERIOD);

	cpts->phc_index = ptp_clock_index(cpts->clock);
#endif
	return 0;
}

void cpts_unregister(struct cpts *cpts)
{
#ifdef CONFIG_TI_CPTS
	if (cpts->clock) {
		ptp_clock_unregister(cpts->clock);
		cancel_delayed_work_sync(&cpts->overflow_work);
	}
	if (cpts->refclk)
		cpts_clk_release(cpts);
#endif
}
