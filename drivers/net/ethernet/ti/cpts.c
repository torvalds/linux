// SPDX-License-Identifier: GPL-2.0+
/*
 * TI Common Platform Time Sync
 *
 * Copyright (C) 2012 Richard Cochran <richardcochran@gmail.com>
 *
 */
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/if.h>
#include <linux/hrtimer.h>
#include <linux/module.h>
#include <linux/net_tstamp.h>
#include <linux/ptp_classify.h>
#include <linux/time.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>

#include "cpts.h"

#define CPTS_SKB_TX_WORK_TIMEOUT 1 /* jiffies */
#define CPTS_SKB_RX_TX_TMO 100 /*ms */
#define CPTS_EVENT_RX_TX_TIMEOUT (100) /* ms */

struct cpts_skb_cb_data {
	u32 skb_mtype_seqid;
	unsigned long tmo;
};

#define cpts_read32(c, r)	readl_relaxed(&c->reg->r)
#define cpts_write32(c, v, r)	writel_relaxed(v, &c->reg->r)

static int cpts_event_port(struct cpts_event *event)
{
	return (event->high >> PORT_NUMBER_SHIFT) & PORT_NUMBER_MASK;
}

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

static int cpts_purge_events(struct cpts *cpts)
{
	struct list_head *this, *next;
	struct cpts_event *event;
	int removed = 0;

	list_for_each_safe(this, next, &cpts->events) {
		event = list_entry(this, struct cpts_event, list);
		if (event_expired(event)) {
			list_del_init(&event->list);
			list_add(&event->list, &cpts->pool);
			++removed;
		}
	}

	if (removed)
		dev_dbg(cpts->dev, "cpts: event pool cleaned up %d\n", removed);
	return removed ? 0 : -1;
}

static void cpts_purge_txq(struct cpts *cpts)
{
	struct cpts_skb_cb_data *skb_cb;
	struct sk_buff *skb, *tmp;
	int removed = 0;

	skb_queue_walk_safe(&cpts->txq, skb, tmp) {
		skb_cb = (struct cpts_skb_cb_data *)skb->cb;
		if (time_after(jiffies, skb_cb->tmo)) {
			__skb_unlink(skb, &cpts->txq);
			dev_consume_skb_any(skb);
			++removed;
		}
	}

	if (removed)
		dev_dbg(cpts->dev, "txq cleaned up %d\n", removed);
}

/*
 * Returns zero if matching event type was found.
 */
static int cpts_fifo_read(struct cpts *cpts, int match)
{
	struct ptp_clock_event pevent;
	bool need_schedule = false;
	struct cpts_event *event;
	unsigned long flags;
	int i, type = -1;
	u32 hi, lo;

	spin_lock_irqsave(&cpts->lock, flags);

	for (i = 0; i < CPTS_FIFO_DEPTH; i++) {
		if (cpts_fifo_pop(cpts, &hi, &lo))
			break;

		if (list_empty(&cpts->pool) && cpts_purge_events(cpts)) {
			dev_warn(cpts->dev, "cpts: event pool empty\n");
			break;
		}

		event = list_first_entry(&cpts->pool, struct cpts_event, list);
		event->high = hi;
		event->low = lo;
		event->timestamp = timecounter_cyc2time(&cpts->tc, event->low);
		type = event_type(event);

		dev_dbg(cpts->dev, "CPTS_EV: %d high:%08X low:%08x\n",
			type, event->high, event->low);
		switch (type) {
		case CPTS_EV_PUSH:
			WRITE_ONCE(cpts->cur_timestamp, lo);
			timecounter_read(&cpts->tc);
			if (cpts->mult_new) {
				cpts->cc.mult = cpts->mult_new;
				cpts->mult_new = 0;
			}
			if (!cpts->irq_poll)
				complete(&cpts->ts_push_complete);
			break;
		case CPTS_EV_TX:
		case CPTS_EV_RX:
			event->tmo = jiffies +
				msecs_to_jiffies(CPTS_EVENT_RX_TX_TIMEOUT);

			list_del_init(&event->list);
			list_add_tail(&event->list, &cpts->events);
			need_schedule = true;
			break;
		case CPTS_EV_ROLL:
		case CPTS_EV_HALF:
			break;
		case CPTS_EV_HW:
			pevent.timestamp = event->timestamp;
			pevent.type = PTP_CLOCK_EXTTS;
			pevent.index = cpts_event_port(event) - 1;
			ptp_clock_event(cpts->clock, &pevent);
			break;
		default:
			dev_err(cpts->dev, "cpts: unknown event type\n");
			break;
		}
		if (type == match)
			break;
	}

	spin_unlock_irqrestore(&cpts->lock, flags);

	if (!cpts->irq_poll && need_schedule)
		ptp_schedule_worker(cpts->clock, 0);

	return type == match ? 0 : -1;
}

void cpts_misc_interrupt(struct cpts *cpts)
{
	cpts_fifo_read(cpts, -1);
}
EXPORT_SYMBOL_GPL(cpts_misc_interrupt);

static u64 cpts_systim_read(const struct cyclecounter *cc)
{
	struct cpts *cpts = container_of(cc, struct cpts, cc);

	return READ_ONCE(cpts->cur_timestamp);
}

static void cpts_update_cur_time(struct cpts *cpts, int match,
				 struct ptp_system_timestamp *sts)
{
	unsigned long flags;

	reinit_completion(&cpts->ts_push_complete);

	/* use spin_lock_irqsave() here as it has to run very fast */
	spin_lock_irqsave(&cpts->lock, flags);
	ptp_read_system_prets(sts);
	cpts_write32(cpts, TS_PUSH, ts_push);
	cpts_read32(cpts, ts_push);
	ptp_read_system_postts(sts);
	spin_unlock_irqrestore(&cpts->lock, flags);

	if (cpts->irq_poll && cpts_fifo_read(cpts, match) && match != -1)
		dev_err(cpts->dev, "cpts: unable to obtain a time stamp\n");

	if (!cpts->irq_poll &&
	    !wait_for_completion_timeout(&cpts->ts_push_complete, HZ))
		dev_err(cpts->dev, "cpts: obtain a time stamp timeout\n");
}

/* PTP clock operations */

static int cpts_ptp_adjfreq(struct ptp_clock_info *ptp, s32 ppb)
{
	struct cpts *cpts = container_of(ptp, struct cpts, info);
	int neg_adj = 0;
	u32 diff, mult;
	u64 adj;

	if (ppb < 0) {
		neg_adj = 1;
		ppb = -ppb;
	}
	mult = cpts->cc_mult;
	adj = mult;
	adj *= ppb;
	diff = div_u64(adj, 1000000000ULL);

	mutex_lock(&cpts->ptp_clk_mutex);

	cpts->mult_new = neg_adj ? mult - diff : mult + diff;

	cpts_update_cur_time(cpts, CPTS_EV_PUSH, NULL);

	mutex_unlock(&cpts->ptp_clk_mutex);
	return 0;
}

static int cpts_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct cpts *cpts = container_of(ptp, struct cpts, info);

	mutex_lock(&cpts->ptp_clk_mutex);
	timecounter_adjtime(&cpts->tc, delta);
	mutex_unlock(&cpts->ptp_clk_mutex);

	return 0;
}

static int cpts_ptp_gettimeex(struct ptp_clock_info *ptp,
			      struct timespec64 *ts,
			      struct ptp_system_timestamp *sts)
{
	struct cpts *cpts = container_of(ptp, struct cpts, info);
	u64 ns;

	mutex_lock(&cpts->ptp_clk_mutex);

	cpts_update_cur_time(cpts, CPTS_EV_PUSH, sts);

	ns = timecounter_read(&cpts->tc);
	mutex_unlock(&cpts->ptp_clk_mutex);

	*ts = ns_to_timespec64(ns);

	return 0;
}

static int cpts_ptp_settime(struct ptp_clock_info *ptp,
			    const struct timespec64 *ts)
{
	struct cpts *cpts = container_of(ptp, struct cpts, info);
	u64 ns;

	ns = timespec64_to_ns(ts);

	mutex_lock(&cpts->ptp_clk_mutex);
	timecounter_init(&cpts->tc, &cpts->cc, ns);
	mutex_unlock(&cpts->ptp_clk_mutex);

	return 0;
}

static int cpts_extts_enable(struct cpts *cpts, u32 index, int on)
{
	u32 v;

	if (((cpts->hw_ts_enable & BIT(index)) >> index) == on)
		return 0;

	mutex_lock(&cpts->ptp_clk_mutex);

	v = cpts_read32(cpts, control);
	if (on) {
		v |= BIT(8 + index);
		cpts->hw_ts_enable |= BIT(index);
	} else {
		v &= ~BIT(8 + index);
		cpts->hw_ts_enable &= ~BIT(index);
	}
	cpts_write32(cpts, v, control);

	mutex_unlock(&cpts->ptp_clk_mutex);

	return 0;
}

static int cpts_ptp_enable(struct ptp_clock_info *ptp,
			   struct ptp_clock_request *rq, int on)
{
	struct cpts *cpts = container_of(ptp, struct cpts, info);

	switch (rq->type) {
	case PTP_CLK_REQ_EXTTS:
		return cpts_extts_enable(cpts, rq->extts.index, on);
	default:
		break;
	}

	return -EOPNOTSUPP;
}

static bool cpts_match_tx_ts(struct cpts *cpts, struct cpts_event *event)
{
	struct sk_buff_head txq_list;
	struct sk_buff *skb, *tmp;
	unsigned long flags;
	bool found = false;
	u32 mtype_seqid;

	mtype_seqid = event->high &
		      ((MESSAGE_TYPE_MASK << MESSAGE_TYPE_SHIFT) |
		       (SEQUENCE_ID_MASK << SEQUENCE_ID_SHIFT) |
		       (EVENT_TYPE_MASK << EVENT_TYPE_SHIFT));

	__skb_queue_head_init(&txq_list);

	spin_lock_irqsave(&cpts->txq.lock, flags);
	skb_queue_splice_init(&cpts->txq, &txq_list);
	spin_unlock_irqrestore(&cpts->txq.lock, flags);

	skb_queue_walk_safe(&txq_list, skb, tmp) {
		struct skb_shared_hwtstamps ssh;
		struct cpts_skb_cb_data *skb_cb =
					(struct cpts_skb_cb_data *)skb->cb;

		if (mtype_seqid == skb_cb->skb_mtype_seqid) {
			memset(&ssh, 0, sizeof(ssh));
			ssh.hwtstamp = ns_to_ktime(event->timestamp);
			skb_tstamp_tx(skb, &ssh);
			found = true;
			__skb_unlink(skb, &txq_list);
			dev_consume_skb_any(skb);
			dev_dbg(cpts->dev, "match tx timestamp mtype_seqid %08x\n",
				mtype_seqid);
			break;
		}

		if (time_after(jiffies, skb_cb->tmo)) {
			/* timeout any expired skbs over 1s */
			dev_dbg(cpts->dev, "expiring tx timestamp from txq\n");
			__skb_unlink(skb, &txq_list);
			dev_consume_skb_any(skb);
		}
	}

	spin_lock_irqsave(&cpts->txq.lock, flags);
	skb_queue_splice(&txq_list, &cpts->txq);
	spin_unlock_irqrestore(&cpts->txq.lock, flags);

	return found;
}

static void cpts_process_events(struct cpts *cpts)
{
	struct list_head *this, *next;
	struct cpts_event *event;
	LIST_HEAD(events_free);
	unsigned long flags;
	LIST_HEAD(events);

	spin_lock_irqsave(&cpts->lock, flags);
	list_splice_init(&cpts->events, &events);
	spin_unlock_irqrestore(&cpts->lock, flags);

	list_for_each_safe(this, next, &events) {
		event = list_entry(this, struct cpts_event, list);
		if (cpts_match_tx_ts(cpts, event) ||
		    time_after(jiffies, event->tmo)) {
			list_del_init(&event->list);
			list_add(&event->list, &events_free);
		}
	}

	spin_lock_irqsave(&cpts->lock, flags);
	list_splice_tail(&events, &cpts->events);
	list_splice_tail(&events_free, &cpts->pool);
	spin_unlock_irqrestore(&cpts->lock, flags);
}

static long cpts_overflow_check(struct ptp_clock_info *ptp)
{
	struct cpts *cpts = container_of(ptp, struct cpts, info);
	unsigned long delay = cpts->ov_check_period;
	unsigned long flags;
	u64 ns;

	mutex_lock(&cpts->ptp_clk_mutex);

	cpts_update_cur_time(cpts, -1, NULL);
	ns = timecounter_read(&cpts->tc);

	cpts_process_events(cpts);

	spin_lock_irqsave(&cpts->txq.lock, flags);
	if (!skb_queue_empty(&cpts->txq)) {
		cpts_purge_txq(cpts);
		if (!skb_queue_empty(&cpts->txq))
			delay = CPTS_SKB_TX_WORK_TIMEOUT;
	}
	spin_unlock_irqrestore(&cpts->txq.lock, flags);

	dev_dbg(cpts->dev, "cpts overflow check at %lld\n", ns);
	mutex_unlock(&cpts->ptp_clk_mutex);
	return (long)delay;
}

static const struct ptp_clock_info cpts_info = {
	.owner		= THIS_MODULE,
	.name		= "CTPS timer",
	.max_adj	= 1000000,
	.n_ext_ts	= 0,
	.n_pins		= 0,
	.pps		= 0,
	.adjfreq	= cpts_ptp_adjfreq,
	.adjtime	= cpts_ptp_adjtime,
	.gettimex64	= cpts_ptp_gettimeex,
	.settime64	= cpts_ptp_settime,
	.enable		= cpts_ptp_enable,
	.do_aux_work	= cpts_overflow_check,
};

static int cpts_skb_get_mtype_seqid(struct sk_buff *skb, u32 *mtype_seqid)
{
	unsigned int ptp_class = ptp_classify_raw(skb);
	struct ptp_header *hdr;
	u8 msgtype;
	u16 seqid;

	if (ptp_class == PTP_CLASS_NONE)
		return 0;

	hdr = ptp_parse_header(skb, ptp_class);
	if (!hdr)
		return 0;

	msgtype = ptp_get_msgtype(hdr, ptp_class);
	seqid	= ntohs(hdr->sequence_id);

	*mtype_seqid  = (msgtype & MESSAGE_TYPE_MASK) << MESSAGE_TYPE_SHIFT;
	*mtype_seqid |= (seqid & SEQUENCE_ID_MASK) << SEQUENCE_ID_SHIFT;

	return 1;
}

static u64 cpts_find_ts(struct cpts *cpts, struct sk_buff *skb,
			int ev_type, u32 skb_mtype_seqid)
{
	struct list_head *this, *next;
	struct cpts_event *event;
	unsigned long flags;
	u32 mtype_seqid;
	u64 ns = 0;

	cpts_fifo_read(cpts, -1);
	spin_lock_irqsave(&cpts->lock, flags);
	list_for_each_safe(this, next, &cpts->events) {
		event = list_entry(this, struct cpts_event, list);
		if (event_expired(event)) {
			list_del_init(&event->list);
			list_add(&event->list, &cpts->pool);
			continue;
		}

		mtype_seqid = event->high &
			      ((MESSAGE_TYPE_MASK << MESSAGE_TYPE_SHIFT) |
			       (SEQUENCE_ID_MASK << SEQUENCE_ID_SHIFT) |
			       (EVENT_TYPE_MASK << EVENT_TYPE_SHIFT));

		if (mtype_seqid == skb_mtype_seqid) {
			ns = event->timestamp;
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
	struct cpts_skb_cb_data *skb_cb = (struct cpts_skb_cb_data *)skb->cb;
	struct skb_shared_hwtstamps *ssh;
	int ret;
	u64 ns;

	/* cpts_rx_timestamp() is called before eth_type_trans(), so
	 * skb MAC Hdr properties are not configured yet. Hence need to
	 * reset skb MAC header here
	 */
	skb_reset_mac_header(skb);
	ret = cpts_skb_get_mtype_seqid(skb, &skb_cb->skb_mtype_seqid);
	if (!ret)
		return;

	skb_cb->skb_mtype_seqid |= (CPTS_EV_RX << EVENT_TYPE_SHIFT);

	dev_dbg(cpts->dev, "%s mtype seqid %08x\n",
		__func__, skb_cb->skb_mtype_seqid);

	ns = cpts_find_ts(cpts, skb, CPTS_EV_RX, skb_cb->skb_mtype_seqid);
	if (!ns)
		return;
	ssh = skb_hwtstamps(skb);
	memset(ssh, 0, sizeof(*ssh));
	ssh->hwtstamp = ns_to_ktime(ns);
}
EXPORT_SYMBOL_GPL(cpts_rx_timestamp);

void cpts_tx_timestamp(struct cpts *cpts, struct sk_buff *skb)
{
	struct cpts_skb_cb_data *skb_cb = (struct cpts_skb_cb_data *)skb->cb;
	int ret;

	if (!(skb_shinfo(skb)->tx_flags & SKBTX_IN_PROGRESS))
		return;

	ret = cpts_skb_get_mtype_seqid(skb, &skb_cb->skb_mtype_seqid);
	if (!ret)
		return;

	skb_cb->skb_mtype_seqid |= (CPTS_EV_TX << EVENT_TYPE_SHIFT);

	dev_dbg(cpts->dev, "%s mtype seqid %08x\n",
		__func__, skb_cb->skb_mtype_seqid);

	/* Always defer TX TS processing to PTP worker */
	skb_get(skb);
	/* get the timestamp for timeouts */
	skb_cb->tmo = jiffies + msecs_to_jiffies(CPTS_SKB_RX_TX_TMO);
	skb_queue_tail(&cpts->txq, skb);
	ptp_schedule_worker(cpts->clock, 0);
}
EXPORT_SYMBOL_GPL(cpts_tx_timestamp);

int cpts_register(struct cpts *cpts)
{
	int err, i;

	skb_queue_head_init(&cpts->txq);
	INIT_LIST_HEAD(&cpts->events);
	INIT_LIST_HEAD(&cpts->pool);
	for (i = 0; i < CPTS_MAX_EVENTS; i++)
		list_add(&cpts->pool_data[i].list, &cpts->pool);

	clk_enable(cpts->refclk);

	cpts_write32(cpts, CPTS_EN, control);
	cpts_write32(cpts, TS_PEND_EN, int_enable);

	timecounter_init(&cpts->tc, &cpts->cc, ktime_get_real_ns());

	cpts->clock = ptp_clock_register(&cpts->info, cpts->dev);
	if (IS_ERR(cpts->clock)) {
		err = PTR_ERR(cpts->clock);
		cpts->clock = NULL;
		goto err_ptp;
	}
	cpts->phc_index = ptp_clock_index(cpts->clock);

	ptp_schedule_worker(cpts->clock, cpts->ov_check_period);
	return 0;

err_ptp:
	clk_disable(cpts->refclk);
	return err;
}
EXPORT_SYMBOL_GPL(cpts_register);

void cpts_unregister(struct cpts *cpts)
{
	if (WARN_ON(!cpts->clock))
		return;

	ptp_clock_unregister(cpts->clock);
	cpts->clock = NULL;
	cpts->phc_index = -1;

	cpts_write32(cpts, 0, int_enable);
	cpts_write32(cpts, 0, control);

	/* Drop all packet */
	skb_queue_purge(&cpts->txq);

	clk_disable(cpts->refclk);
}
EXPORT_SYMBOL_GPL(cpts_unregister);

static void cpts_calc_mult_shift(struct cpts *cpts)
{
	u64 frac, maxsec, ns;
	u32 freq;

	freq = clk_get_rate(cpts->refclk);

	/* Calc the maximum number of seconds which we can run before
	 * wrapping around.
	 */
	maxsec = cpts->cc.mask;
	do_div(maxsec, freq);
	/* limit conversation rate to 10 sec as higher values will produce
	 * too small mult factors and so reduce the conversion accuracy
	 */
	if (maxsec > 10)
		maxsec = 10;

	/* Calc overflow check period (maxsec / 2) */
	cpts->ov_check_period = (HZ * maxsec) / 2;
	dev_info(cpts->dev, "cpts: overflow check period %lu (jiffies)\n",
		 cpts->ov_check_period);

	if (cpts->cc.mult || cpts->cc.shift)
		return;

	clocks_calc_mult_shift(&cpts->cc.mult, &cpts->cc.shift,
			       freq, NSEC_PER_SEC, maxsec);

	frac = 0;
	ns = cyclecounter_cyc2ns(&cpts->cc, freq, cpts->cc.mask, &frac);

	dev_info(cpts->dev,
		 "CPTS: ref_clk_freq:%u calc_mult:%u calc_shift:%u error:%lld nsec/sec\n",
		 freq, cpts->cc.mult, cpts->cc.shift, (ns - NSEC_PER_SEC));
}

static int cpts_of_mux_clk_setup(struct cpts *cpts, struct device_node *node)
{
	struct device_node *refclk_np;
	const char **parent_names;
	unsigned int num_parents;
	struct clk_hw *clk_hw;
	int ret = -EINVAL;
	u32 *mux_table;

	refclk_np = of_get_child_by_name(node, "cpts-refclk-mux");
	if (!refclk_np)
		/* refclk selection supported not for all SoCs */
		return 0;

	num_parents = of_clk_get_parent_count(refclk_np);
	if (num_parents < 1) {
		dev_err(cpts->dev, "mux-clock %s must have parents\n",
			refclk_np->name);
		goto mux_fail;
	}

	parent_names = devm_kcalloc(cpts->dev, num_parents,
				    sizeof(*parent_names), GFP_KERNEL);

	mux_table = devm_kcalloc(cpts->dev, num_parents, sizeof(*mux_table),
				 GFP_KERNEL);
	if (!mux_table || !parent_names) {
		ret = -ENOMEM;
		goto mux_fail;
	}

	of_clk_parent_fill(refclk_np, parent_names, num_parents);

	ret = of_property_read_variable_u32_array(refclk_np, "ti,mux-tbl",
						  mux_table,
						  num_parents, num_parents);
	if (ret < 0)
		goto mux_fail;

	clk_hw = clk_hw_register_mux_table(cpts->dev, refclk_np->name,
					   parent_names, num_parents,
					   0,
					   &cpts->reg->rftclk_sel, 0, 0x1F,
					   0, mux_table, NULL);
	if (IS_ERR(clk_hw)) {
		ret = PTR_ERR(clk_hw);
		goto mux_fail;
	}

	ret = devm_add_action_or_reset(cpts->dev,
				       (void(*)(void *))clk_hw_unregister_mux,
				       clk_hw);
	if (ret) {
		dev_err(cpts->dev, "add clkmux unreg action %d", ret);
		goto mux_fail;
	}

	ret = of_clk_add_hw_provider(refclk_np, of_clk_hw_simple_get, clk_hw);
	if (ret)
		goto mux_fail;

	ret = devm_add_action_or_reset(cpts->dev,
				       (void(*)(void *))of_clk_del_provider,
				       refclk_np);
	if (ret) {
		dev_err(cpts->dev, "add clkmux provider unreg action %d", ret);
		goto mux_fail;
	}

	return ret;

mux_fail:
	of_node_put(refclk_np);
	return ret;
}

static int cpts_of_parse(struct cpts *cpts, struct device_node *node)
{
	int ret = -EINVAL;
	u32 prop;

	if (!of_property_read_u32(node, "cpts_clock_mult", &prop))
		cpts->cc.mult = prop;

	if (!of_property_read_u32(node, "cpts_clock_shift", &prop))
		cpts->cc.shift = prop;

	if ((cpts->cc.mult && !cpts->cc.shift) ||
	    (!cpts->cc.mult && cpts->cc.shift))
		goto of_error;

	return cpts_of_mux_clk_setup(cpts, node);

of_error:
	dev_err(cpts->dev, "CPTS: Missing property in the DT.\n");
	return ret;
}

struct cpts *cpts_create(struct device *dev, void __iomem *regs,
			 struct device_node *node, u32 n_ext_ts)
{
	struct cpts *cpts;
	int ret;

	cpts = devm_kzalloc(dev, sizeof(*cpts), GFP_KERNEL);
	if (!cpts)
		return ERR_PTR(-ENOMEM);

	cpts->dev = dev;
	cpts->reg = (struct cpsw_cpts __iomem *)regs;
	cpts->irq_poll = true;
	spin_lock_init(&cpts->lock);
	mutex_init(&cpts->ptp_clk_mutex);
	init_completion(&cpts->ts_push_complete);

	ret = cpts_of_parse(cpts, node);
	if (ret)
		return ERR_PTR(ret);

	cpts->refclk = devm_get_clk_from_child(dev, node, "cpts");
	if (IS_ERR(cpts->refclk))
		/* try get clk from dev node for compatibility */
		cpts->refclk = devm_clk_get(dev, "cpts");

	if (IS_ERR(cpts->refclk)) {
		dev_err(dev, "Failed to get cpts refclk %ld\n",
			PTR_ERR(cpts->refclk));
		return ERR_CAST(cpts->refclk);
	}

	ret = clk_prepare(cpts->refclk);
	if (ret)
		return ERR_PTR(ret);

	cpts->cc.read = cpts_systim_read;
	cpts->cc.mask = CLOCKSOURCE_MASK(32);
	cpts->info = cpts_info;
	cpts->phc_index = -1;

	if (n_ext_ts)
		cpts->info.n_ext_ts = n_ext_ts;

	cpts_calc_mult_shift(cpts);
	/* save cc.mult original value as it can be modified
	 * by cpts_ptp_adjfreq().
	 */
	cpts->cc_mult = cpts->cc.mult;

	return cpts;
}
EXPORT_SYMBOL_GPL(cpts_create);

void cpts_release(struct cpts *cpts)
{
	if (!cpts)
		return;

	if (WARN_ON(!cpts->refclk))
		return;

	clk_unprepare(cpts->refclk);
}
EXPORT_SYMBOL_GPL(cpts_release);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("TI CPTS driver");
MODULE_AUTHOR("Richard Cochran <richardcochran@gmail.com>");
