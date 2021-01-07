// SPDX-License-Identifier: GPL-2.0
/* TI K3 AM65x Common Platform Time Sync
 *
 * Copyright (C) 2020 Texas Instruments Incorporated - http://www.ti.com
 *
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/if_vlan.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/net_tstamp.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/ptp_classify.h>
#include <linux/ptp_clock_kernel.h>

#include "am65-cpts.h"

struct am65_genf_regs {
	u32 comp_lo;	/* Comparison Low Value 0:31 */
	u32 comp_hi;	/* Comparison High Value 32:63 */
	u32 control;	/* control */
	u32 length;	/* Length */
	u32 ppm_low;	/* PPM Load Low Value 0:31 */
	u32 ppm_hi;	/* PPM Load High Value 32:63 */
	u32 ts_nudge;	/* Nudge value */
} __aligned(32) __packed;

#define AM65_CPTS_GENF_MAX_NUM 9
#define AM65_CPTS_ESTF_MAX_NUM 8

struct am65_cpts_regs {
	u32 idver;		/* Identification and version */
	u32 control;		/* Time sync control */
	u32 rftclk_sel;		/* Reference Clock Select Register */
	u32 ts_push;		/* Time stamp event push */
	u32 ts_load_val_lo;	/* Time Stamp Load Low Value 0:31 */
	u32 ts_load_en;		/* Time stamp load enable */
	u32 ts_comp_lo;		/* Time Stamp Comparison Low Value 0:31 */
	u32 ts_comp_length;	/* Time Stamp Comparison Length */
	u32 intstat_raw;	/* Time sync interrupt status raw */
	u32 intstat_masked;	/* Time sync interrupt status masked */
	u32 int_enable;		/* Time sync interrupt enable */
	u32 ts_comp_nudge;	/* Time Stamp Comparison Nudge Value */
	u32 event_pop;		/* Event interrupt pop */
	u32 event_0;		/* Event Time Stamp lo 0:31 */
	u32 event_1;		/* Event Type Fields */
	u32 event_2;		/* Event Type Fields domain */
	u32 event_3;		/* Event Time Stamp hi 32:63 */
	u32 ts_load_val_hi;	/* Time Stamp Load High Value 32:63 */
	u32 ts_comp_hi;		/* Time Stamp Comparison High Value 32:63 */
	u32 ts_add_val;		/* Time Stamp Add value */
	u32 ts_ppm_low;		/* Time Stamp PPM Load Low Value 0:31 */
	u32 ts_ppm_hi;		/* Time Stamp PPM Load High Value 32:63 */
	u32 ts_nudge;		/* Time Stamp Nudge value */
	u32 reserv[33];
	struct am65_genf_regs genf[AM65_CPTS_GENF_MAX_NUM];
	struct am65_genf_regs estf[AM65_CPTS_ESTF_MAX_NUM];
};

/* CONTROL_REG */
#define AM65_CPTS_CONTROL_EN			BIT(0)
#define AM65_CPTS_CONTROL_INT_TEST		BIT(1)
#define AM65_CPTS_CONTROL_TS_COMP_POLARITY	BIT(2)
#define AM65_CPTS_CONTROL_TSTAMP_EN		BIT(3)
#define AM65_CPTS_CONTROL_SEQUENCE_EN		BIT(4)
#define AM65_CPTS_CONTROL_64MODE		BIT(5)
#define AM65_CPTS_CONTROL_TS_COMP_TOG		BIT(6)
#define AM65_CPTS_CONTROL_TS_PPM_DIR		BIT(7)
#define AM65_CPTS_CONTROL_HW1_TS_PUSH_EN	BIT(8)
#define AM65_CPTS_CONTROL_HW2_TS_PUSH_EN	BIT(9)
#define AM65_CPTS_CONTROL_HW3_TS_PUSH_EN	BIT(10)
#define AM65_CPTS_CONTROL_HW4_TS_PUSH_EN	BIT(11)
#define AM65_CPTS_CONTROL_HW5_TS_PUSH_EN	BIT(12)
#define AM65_CPTS_CONTROL_HW6_TS_PUSH_EN	BIT(13)
#define AM65_CPTS_CONTROL_HW7_TS_PUSH_EN	BIT(14)
#define AM65_CPTS_CONTROL_HW8_TS_PUSH_EN	BIT(15)
#define AM65_CPTS_CONTROL_HW1_TS_PUSH_OFFSET	(8)

#define AM65_CPTS_CONTROL_TX_GENF_CLR_EN	BIT(17)

#define AM65_CPTS_CONTROL_TS_SYNC_SEL_MASK	(0xF)
#define AM65_CPTS_CONTROL_TS_SYNC_SEL_SHIFT	(28)

/* RFTCLK_SEL_REG */
#define AM65_CPTS_RFTCLK_SEL_MASK		(0x1F)

/* TS_PUSH_REG */
#define AM65_CPTS_TS_PUSH			BIT(0)

/* TS_LOAD_EN_REG */
#define AM65_CPTS_TS_LOAD_EN			BIT(0)

/* INTSTAT_RAW_REG */
#define AM65_CPTS_INTSTAT_RAW_TS_PEND		BIT(0)

/* INTSTAT_MASKED_REG */
#define AM65_CPTS_INTSTAT_MASKED_TS_PEND	BIT(0)

/* INT_ENABLE_REG */
#define AM65_CPTS_INT_ENABLE_TS_PEND_EN		BIT(0)

/* TS_COMP_NUDGE_REG */
#define AM65_CPTS_TS_COMP_NUDGE_MASK		(0xFF)

/* EVENT_POP_REG */
#define AM65_CPTS_EVENT_POP			BIT(0)

/* EVENT_1_REG */
#define AM65_CPTS_EVENT_1_SEQUENCE_ID_MASK	GENMASK(15, 0)

#define AM65_CPTS_EVENT_1_MESSAGE_TYPE_MASK	GENMASK(19, 16)
#define AM65_CPTS_EVENT_1_MESSAGE_TYPE_SHIFT	(16)

#define AM65_CPTS_EVENT_1_EVENT_TYPE_MASK	GENMASK(23, 20)
#define AM65_CPTS_EVENT_1_EVENT_TYPE_SHIFT	(20)

#define AM65_CPTS_EVENT_1_PORT_NUMBER_MASK	GENMASK(28, 24)
#define AM65_CPTS_EVENT_1_PORT_NUMBER_SHIFT	(24)

/* EVENT_2_REG */
#define AM65_CPTS_EVENT_2_REG_DOMAIN_MASK	(0xFF)
#define AM65_CPTS_EVENT_2_REG_DOMAIN_SHIFT	(0)

enum {
	AM65_CPTS_EV_PUSH,	/* Time Stamp Push Event */
	AM65_CPTS_EV_ROLL,	/* Time Stamp Rollover Event */
	AM65_CPTS_EV_HALF,	/* Time Stamp Half Rollover Event */
	AM65_CPTS_EV_HW,		/* Hardware Time Stamp Push Event */
	AM65_CPTS_EV_RX,		/* Ethernet Receive Event */
	AM65_CPTS_EV_TX,		/* Ethernet Transmit Event */
	AM65_CPTS_EV_TS_COMP,	/* Time Stamp Compare Event */
	AM65_CPTS_EV_HOST,	/* Host Transmit Event */
};

struct am65_cpts_event {
	struct list_head list;
	unsigned long tmo;
	u32 event1;
	u32 event2;
	u64 timestamp;
};

#define AM65_CPTS_FIFO_DEPTH		(16)
#define AM65_CPTS_MAX_EVENTS		(32)
#define AM65_CPTS_EVENT_RX_TX_TIMEOUT	(20) /* ms */
#define AM65_CPTS_SKB_TX_WORK_TIMEOUT	1 /* jiffies */
#define AM65_CPTS_MIN_PPM		0x400

struct am65_cpts {
	struct device *dev;
	struct am65_cpts_regs __iomem *reg;
	struct ptp_clock_info ptp_info;
	struct ptp_clock *ptp_clock;
	int phc_index;
	struct clk_hw *clk_mux_hw;
	struct device_node *clk_mux_np;
	struct clk *refclk;
	u32 refclk_freq;
	struct list_head events;
	struct list_head pool;
	struct am65_cpts_event pool_data[AM65_CPTS_MAX_EVENTS];
	spinlock_t lock; /* protects events lists*/
	u32 ext_ts_inputs;
	u32 genf_num;
	u32 ts_add_val;
	int irq;
	struct mutex ptp_clk_lock; /* PHC access sync */
	u64 timestamp;
	u32 genf_enable;
	u32 hw_ts_enable;
	struct sk_buff_head txq;
};

struct am65_cpts_skb_cb_data {
	unsigned long tmo;
	u32 skb_mtype_seqid;
};

#define am65_cpts_write32(c, v, r) writel(v, &(c)->reg->r)
#define am65_cpts_read32(c, r) readl(&(c)->reg->r)

static void am65_cpts_settime(struct am65_cpts *cpts, u64 start_tstamp)
{
	u32 val;

	val = upper_32_bits(start_tstamp);
	am65_cpts_write32(cpts, val, ts_load_val_hi);
	val = lower_32_bits(start_tstamp);
	am65_cpts_write32(cpts, val, ts_load_val_lo);

	am65_cpts_write32(cpts, AM65_CPTS_TS_LOAD_EN, ts_load_en);
}

static void am65_cpts_set_add_val(struct am65_cpts *cpts)
{
	/* select coefficient according to the rate */
	cpts->ts_add_val = (NSEC_PER_SEC / cpts->refclk_freq - 1) & 0x7;

	am65_cpts_write32(cpts, cpts->ts_add_val, ts_add_val);
}

static void am65_cpts_disable(struct am65_cpts *cpts)
{
	am65_cpts_write32(cpts, 0, control);
	am65_cpts_write32(cpts, 0, int_enable);
}

static int am65_cpts_event_get_port(struct am65_cpts_event *event)
{
	return (event->event1 & AM65_CPTS_EVENT_1_PORT_NUMBER_MASK) >>
		AM65_CPTS_EVENT_1_PORT_NUMBER_SHIFT;
}

static int am65_cpts_event_get_type(struct am65_cpts_event *event)
{
	return (event->event1 & AM65_CPTS_EVENT_1_EVENT_TYPE_MASK) >>
		AM65_CPTS_EVENT_1_EVENT_TYPE_SHIFT;
}

static int am65_cpts_cpts_purge_events(struct am65_cpts *cpts)
{
	struct list_head *this, *next;
	struct am65_cpts_event *event;
	int removed = 0;

	list_for_each_safe(this, next, &cpts->events) {
		event = list_entry(this, struct am65_cpts_event, list);
		if (time_after(jiffies, event->tmo)) {
			list_del_init(&event->list);
			list_add(&event->list, &cpts->pool);
			++removed;
		}
	}

	if (removed)
		dev_dbg(cpts->dev, "event pool cleaned up %d\n", removed);
	return removed ? 0 : -1;
}

static bool am65_cpts_fifo_pop_event(struct am65_cpts *cpts,
				     struct am65_cpts_event *event)
{
	u32 r = am65_cpts_read32(cpts, intstat_raw);

	if (r & AM65_CPTS_INTSTAT_RAW_TS_PEND) {
		event->timestamp = am65_cpts_read32(cpts, event_0);
		event->event1 = am65_cpts_read32(cpts, event_1);
		event->event2 = am65_cpts_read32(cpts, event_2);
		event->timestamp |= (u64)am65_cpts_read32(cpts, event_3) << 32;
		am65_cpts_write32(cpts, AM65_CPTS_EVENT_POP, event_pop);
		return false;
	}
	return true;
}

static int am65_cpts_fifo_read(struct am65_cpts *cpts)
{
	struct ptp_clock_event pevent;
	struct am65_cpts_event *event;
	bool schedule = false;
	int i, type, ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&cpts->lock, flags);
	for (i = 0; i < AM65_CPTS_FIFO_DEPTH; i++) {
		event = list_first_entry_or_null(&cpts->pool,
						 struct am65_cpts_event, list);

		if (!event) {
			if (am65_cpts_cpts_purge_events(cpts)) {
				dev_err(cpts->dev, "cpts: event pool empty\n");
				ret = -1;
				goto out;
			}
			continue;
		}

		if (am65_cpts_fifo_pop_event(cpts, event))
			break;

		type = am65_cpts_event_get_type(event);
		switch (type) {
		case AM65_CPTS_EV_PUSH:
			cpts->timestamp = event->timestamp;
			dev_dbg(cpts->dev, "AM65_CPTS_EV_PUSH t:%llu\n",
				cpts->timestamp);
			break;
		case AM65_CPTS_EV_RX:
		case AM65_CPTS_EV_TX:
			event->tmo = jiffies +
				msecs_to_jiffies(AM65_CPTS_EVENT_RX_TX_TIMEOUT);

			list_del_init(&event->list);
			list_add_tail(&event->list, &cpts->events);

			dev_dbg(cpts->dev,
				"AM65_CPTS_EV_TX e1:%08x e2:%08x t:%lld\n",
				event->event1, event->event2,
				event->timestamp);
			schedule = true;
			break;
		case AM65_CPTS_EV_HW:
			pevent.index = am65_cpts_event_get_port(event) - 1;
			pevent.timestamp = event->timestamp;
			pevent.type = PTP_CLOCK_EXTTS;
			dev_dbg(cpts->dev, "AM65_CPTS_EV_HW p:%d t:%llu\n",
				pevent.index, event->timestamp);

			ptp_clock_event(cpts->ptp_clock, &pevent);
			break;
		case AM65_CPTS_EV_HOST:
			break;
		case AM65_CPTS_EV_ROLL:
		case AM65_CPTS_EV_HALF:
		case AM65_CPTS_EV_TS_COMP:
			dev_dbg(cpts->dev,
				"AM65_CPTS_EVT: %d e1:%08x e2:%08x t:%lld\n",
				type,
				event->event1, event->event2,
				event->timestamp);
			break;
		default:
			dev_err(cpts->dev, "cpts: unknown event type\n");
			ret = -1;
			goto out;
		}
	}

out:
	spin_unlock_irqrestore(&cpts->lock, flags);

	if (schedule)
		ptp_schedule_worker(cpts->ptp_clock, 0);

	return ret;
}

static u64 am65_cpts_gettime(struct am65_cpts *cpts,
			     struct ptp_system_timestamp *sts)
{
	unsigned long flags;
	u64 val = 0;

	/* temporarily disable cpts interrupt to avoid intentional
	 * doubled read. Interrupt can be in-flight - it's Ok.
	 */
	am65_cpts_write32(cpts, 0, int_enable);

	/* use spin_lock_irqsave() here as it has to run very fast */
	spin_lock_irqsave(&cpts->lock, flags);
	ptp_read_system_prets(sts);
	am65_cpts_write32(cpts, AM65_CPTS_TS_PUSH, ts_push);
	am65_cpts_read32(cpts, ts_push);
	ptp_read_system_postts(sts);
	spin_unlock_irqrestore(&cpts->lock, flags);

	am65_cpts_fifo_read(cpts);

	am65_cpts_write32(cpts, AM65_CPTS_INT_ENABLE_TS_PEND_EN, int_enable);

	val = cpts->timestamp;

	return val;
}

static irqreturn_t am65_cpts_interrupt(int irq, void *dev_id)
{
	struct am65_cpts *cpts = dev_id;

	if (am65_cpts_fifo_read(cpts))
		dev_dbg(cpts->dev, "cpts: unable to obtain a time stamp\n");

	return IRQ_HANDLED;
}

/* PTP clock operations */
static int am65_cpts_ptp_adjfreq(struct ptp_clock_info *ptp, s32 ppb)
{
	struct am65_cpts *cpts = container_of(ptp, struct am65_cpts, ptp_info);
	int neg_adj = 0;
	u64 adj_period;
	u32 val;

	if (ppb < 0) {
		neg_adj = 1;
		ppb = -ppb;
	}

	/* base freq = 1GHz = 1 000 000 000
	 * ppb_norm = ppb * base_freq / clock_freq;
	 * ppm_norm = ppb_norm / 1000
	 * adj_period = 1 000 000 / ppm_norm
	 * adj_period = 1 000 000 000 / ppb_norm
	 * adj_period = 1 000 000 000 / (ppb * base_freq / clock_freq)
	 * adj_period = (1 000 000 000 * clock_freq) / (ppb * base_freq)
	 * adj_period = clock_freq / ppb
	 */
	adj_period = div_u64(cpts->refclk_freq, ppb);

	mutex_lock(&cpts->ptp_clk_lock);

	val = am65_cpts_read32(cpts, control);
	if (neg_adj)
		val |= AM65_CPTS_CONTROL_TS_PPM_DIR;
	else
		val &= ~AM65_CPTS_CONTROL_TS_PPM_DIR;
	am65_cpts_write32(cpts, val, control);

	val = upper_32_bits(adj_period) & 0x3FF;
	am65_cpts_write32(cpts, val, ts_ppm_hi);
	val = lower_32_bits(adj_period);
	am65_cpts_write32(cpts, val, ts_ppm_low);

	mutex_unlock(&cpts->ptp_clk_lock);

	return 0;
}

static int am65_cpts_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct am65_cpts *cpts = container_of(ptp, struct am65_cpts, ptp_info);
	s64 ns;

	mutex_lock(&cpts->ptp_clk_lock);
	ns = am65_cpts_gettime(cpts, NULL);
	ns += delta;
	am65_cpts_settime(cpts, ns);
	mutex_unlock(&cpts->ptp_clk_lock);

	return 0;
}

static int am65_cpts_ptp_gettimex(struct ptp_clock_info *ptp,
				  struct timespec64 *ts,
				  struct ptp_system_timestamp *sts)
{
	struct am65_cpts *cpts = container_of(ptp, struct am65_cpts, ptp_info);
	u64 ns;

	mutex_lock(&cpts->ptp_clk_lock);
	ns = am65_cpts_gettime(cpts, sts);
	mutex_unlock(&cpts->ptp_clk_lock);
	*ts = ns_to_timespec64(ns);

	return 0;
}

u64 am65_cpts_ns_gettime(struct am65_cpts *cpts)
{
	u64 ns;

	/* reuse ptp_clk_lock as it serialize ts push */
	mutex_lock(&cpts->ptp_clk_lock);
	ns = am65_cpts_gettime(cpts, NULL);
	mutex_unlock(&cpts->ptp_clk_lock);

	return ns;
}
EXPORT_SYMBOL_GPL(am65_cpts_ns_gettime);

static int am65_cpts_ptp_settime(struct ptp_clock_info *ptp,
				 const struct timespec64 *ts)
{
	struct am65_cpts *cpts = container_of(ptp, struct am65_cpts, ptp_info);
	u64 ns;

	ns = timespec64_to_ns(ts);
	mutex_lock(&cpts->ptp_clk_lock);
	am65_cpts_settime(cpts, ns);
	mutex_unlock(&cpts->ptp_clk_lock);

	return 0;
}

static void am65_cpts_extts_enable_hw(struct am65_cpts *cpts, u32 index, int on)
{
	u32 v;

	v = am65_cpts_read32(cpts, control);
	if (on) {
		v |= BIT(AM65_CPTS_CONTROL_HW1_TS_PUSH_OFFSET + index);
		cpts->hw_ts_enable |= BIT(index);
	} else {
		v &= ~BIT(AM65_CPTS_CONTROL_HW1_TS_PUSH_OFFSET + index);
		cpts->hw_ts_enable &= ~BIT(index);
	}
	am65_cpts_write32(cpts, v, control);
}

static int am65_cpts_extts_enable(struct am65_cpts *cpts, u32 index, int on)
{
	if (!!(cpts->hw_ts_enable & BIT(index)) == !!on)
		return 0;

	mutex_lock(&cpts->ptp_clk_lock);
	am65_cpts_extts_enable_hw(cpts, index, on);
	mutex_unlock(&cpts->ptp_clk_lock);

	dev_dbg(cpts->dev, "%s: ExtTS:%u %s\n",
		__func__, index, on ? "enabled" : "disabled");

	return 0;
}

int am65_cpts_estf_enable(struct am65_cpts *cpts, int idx,
			  struct am65_cpts_estf_cfg *cfg)
{
	u64 cycles;
	u32 val;

	cycles = cfg->ns_period * cpts->refclk_freq;
	cycles = DIV_ROUND_UP(cycles, NSEC_PER_SEC);
	if (cycles > U32_MAX)
		return -EINVAL;

	/* according to TRM should be zeroed */
	am65_cpts_write32(cpts, 0, estf[idx].length);

	val = upper_32_bits(cfg->ns_start);
	am65_cpts_write32(cpts, val, estf[idx].comp_hi);
	val = lower_32_bits(cfg->ns_start);
	am65_cpts_write32(cpts, val, estf[idx].comp_lo);
	val = lower_32_bits(cycles);
	am65_cpts_write32(cpts, val, estf[idx].length);

	dev_dbg(cpts->dev, "%s: ESTF:%u enabled\n", __func__, idx);

	return 0;
}
EXPORT_SYMBOL_GPL(am65_cpts_estf_enable);

void am65_cpts_estf_disable(struct am65_cpts *cpts, int idx)
{
	am65_cpts_write32(cpts, 0, estf[idx].length);

	dev_dbg(cpts->dev, "%s: ESTF:%u disabled\n", __func__, idx);
}
EXPORT_SYMBOL_GPL(am65_cpts_estf_disable);

static void am65_cpts_perout_enable_hw(struct am65_cpts *cpts,
				       struct ptp_perout_request *req, int on)
{
	u64 ns_period, ns_start, cycles;
	struct timespec64 ts;
	u32 val;

	if (on) {
		ts.tv_sec = req->period.sec;
		ts.tv_nsec = req->period.nsec;
		ns_period = timespec64_to_ns(&ts);

		cycles = (ns_period * cpts->refclk_freq) / NSEC_PER_SEC;

		ts.tv_sec = req->start.sec;
		ts.tv_nsec = req->start.nsec;
		ns_start = timespec64_to_ns(&ts);

		val = upper_32_bits(ns_start);
		am65_cpts_write32(cpts, val, genf[req->index].comp_hi);
		val = lower_32_bits(ns_start);
		am65_cpts_write32(cpts, val, genf[req->index].comp_lo);
		val = lower_32_bits(cycles);
		am65_cpts_write32(cpts, val, genf[req->index].length);

		cpts->genf_enable |= BIT(req->index);
	} else {
		am65_cpts_write32(cpts, 0, genf[req->index].length);

		cpts->genf_enable &= ~BIT(req->index);
	}
}

static int am65_cpts_perout_enable(struct am65_cpts *cpts,
				   struct ptp_perout_request *req, int on)
{
	if (!!(cpts->genf_enable & BIT(req->index)) == !!on)
		return 0;

	mutex_lock(&cpts->ptp_clk_lock);
	am65_cpts_perout_enable_hw(cpts, req, on);
	mutex_unlock(&cpts->ptp_clk_lock);

	dev_dbg(cpts->dev, "%s: GenF:%u %s\n",
		__func__, req->index, on ? "enabled" : "disabled");

	return 0;
}

static int am65_cpts_ptp_enable(struct ptp_clock_info *ptp,
				struct ptp_clock_request *rq, int on)
{
	struct am65_cpts *cpts = container_of(ptp, struct am65_cpts, ptp_info);

	switch (rq->type) {
	case PTP_CLK_REQ_EXTTS:
		return am65_cpts_extts_enable(cpts, rq->extts.index, on);
	case PTP_CLK_REQ_PEROUT:
		return am65_cpts_perout_enable(cpts, &rq->perout, on);
	default:
		break;
	}

	return -EOPNOTSUPP;
}

static long am65_cpts_ts_work(struct ptp_clock_info *ptp);

static struct ptp_clock_info am65_ptp_info = {
	.owner		= THIS_MODULE,
	.name		= "CTPS timer",
	.adjfreq	= am65_cpts_ptp_adjfreq,
	.adjtime	= am65_cpts_ptp_adjtime,
	.gettimex64	= am65_cpts_ptp_gettimex,
	.settime64	= am65_cpts_ptp_settime,
	.enable		= am65_cpts_ptp_enable,
	.do_aux_work	= am65_cpts_ts_work,
};

static bool am65_cpts_match_tx_ts(struct am65_cpts *cpts,
				  struct am65_cpts_event *event)
{
	struct sk_buff_head txq_list;
	struct sk_buff *skb, *tmp;
	unsigned long flags;
	bool found = false;
	u32 mtype_seqid;

	mtype_seqid = event->event1 &
		      (AM65_CPTS_EVENT_1_MESSAGE_TYPE_MASK |
		       AM65_CPTS_EVENT_1_EVENT_TYPE_MASK |
		       AM65_CPTS_EVENT_1_SEQUENCE_ID_MASK);

	__skb_queue_head_init(&txq_list);

	spin_lock_irqsave(&cpts->txq.lock, flags);
	skb_queue_splice_init(&cpts->txq, &txq_list);
	spin_unlock_irqrestore(&cpts->txq.lock, flags);

	/* no need to grab txq.lock as access is always done under cpts->lock */
	skb_queue_walk_safe(&txq_list, skb, tmp) {
		struct skb_shared_hwtstamps ssh;
		struct am65_cpts_skb_cb_data *skb_cb =
					(struct am65_cpts_skb_cb_data *)skb->cb;

		if (mtype_seqid == skb_cb->skb_mtype_seqid) {
			u64 ns = event->timestamp;

			memset(&ssh, 0, sizeof(ssh));
			ssh.hwtstamp = ns_to_ktime(ns);
			skb_tstamp_tx(skb, &ssh);
			found = true;
			__skb_unlink(skb, &txq_list);
			dev_consume_skb_any(skb);
			dev_dbg(cpts->dev,
				"match tx timestamp mtype_seqid %08x\n",
				mtype_seqid);
			break;
		}

		if (time_after(jiffies, skb_cb->tmo)) {
			/* timeout any expired skbs over 100 ms */
			dev_dbg(cpts->dev,
				"expiring tx timestamp mtype_seqid %08x\n",
				mtype_seqid);
			__skb_unlink(skb, &txq_list);
			dev_consume_skb_any(skb);
		}
	}

	spin_lock_irqsave(&cpts->txq.lock, flags);
	skb_queue_splice(&txq_list, &cpts->txq);
	spin_unlock_irqrestore(&cpts->txq.lock, flags);

	return found;
}

static void am65_cpts_find_ts(struct am65_cpts *cpts)
{
	struct am65_cpts_event *event;
	struct list_head *this, *next;
	LIST_HEAD(events_free);
	unsigned long flags;
	LIST_HEAD(events);

	spin_lock_irqsave(&cpts->lock, flags);
	list_splice_init(&cpts->events, &events);
	spin_unlock_irqrestore(&cpts->lock, flags);

	list_for_each_safe(this, next, &events) {
		event = list_entry(this, struct am65_cpts_event, list);
		if (am65_cpts_match_tx_ts(cpts, event) ||
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

static long am65_cpts_ts_work(struct ptp_clock_info *ptp)
{
	struct am65_cpts *cpts = container_of(ptp, struct am65_cpts, ptp_info);
	unsigned long flags;
	long delay = -1;

	am65_cpts_find_ts(cpts);

	spin_lock_irqsave(&cpts->txq.lock, flags);
	if (!skb_queue_empty(&cpts->txq))
		delay = AM65_CPTS_SKB_TX_WORK_TIMEOUT;
	spin_unlock_irqrestore(&cpts->txq.lock, flags);

	return delay;
}

/**
 * am65_cpts_rx_enable - enable rx timestamping
 * @cpts: cpts handle
 * @skb: packet
 *
 * This functions enables rx packets timestamping. The CPTS can timestamp all
 * rx packets.
 */
void am65_cpts_rx_enable(struct am65_cpts *cpts, bool en)
{
	u32 val;

	mutex_lock(&cpts->ptp_clk_lock);
	val = am65_cpts_read32(cpts, control);
	if (en)
		val |= AM65_CPTS_CONTROL_TSTAMP_EN;
	else
		val &= ~AM65_CPTS_CONTROL_TSTAMP_EN;
	am65_cpts_write32(cpts, val, control);
	mutex_unlock(&cpts->ptp_clk_lock);
}
EXPORT_SYMBOL_GPL(am65_cpts_rx_enable);

static int am65_skb_get_mtype_seqid(struct sk_buff *skb, u32 *mtype_seqid)
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

	*mtype_seqid  = (msgtype << AM65_CPTS_EVENT_1_MESSAGE_TYPE_SHIFT) &
			AM65_CPTS_EVENT_1_MESSAGE_TYPE_MASK;
	*mtype_seqid |= (seqid & AM65_CPTS_EVENT_1_SEQUENCE_ID_MASK);

	return 1;
}

/**
 * am65_cpts_tx_timestamp - save tx packet for timestamping
 * @cpts: cpts handle
 * @skb: packet
 *
 * This functions saves tx packet for timestamping if packet can be timestamped.
 * The future processing is done in from PTP auxiliary worker.
 */
void am65_cpts_tx_timestamp(struct am65_cpts *cpts, struct sk_buff *skb)
{
	struct am65_cpts_skb_cb_data *skb_cb = (void *)skb->cb;

	if (!(skb_shinfo(skb)->tx_flags & SKBTX_IN_PROGRESS))
		return;

	/* add frame to queue for processing later.
	 * The periodic FIFO check will handle this.
	 */
	skb_get(skb);
	/* get the timestamp for timeouts */
	skb_cb->tmo = jiffies + msecs_to_jiffies(100);
	skb_queue_tail(&cpts->txq, skb);
	ptp_schedule_worker(cpts->ptp_clock, 0);
}
EXPORT_SYMBOL_GPL(am65_cpts_tx_timestamp);

/**
 * am65_cpts_prep_tx_timestamp - check and prepare tx packet for timestamping
 * @cpts: cpts handle
 * @skb: packet
 *
 * This functions should be called from .xmit().
 * It checks if packet can be timestamped, fills internal cpts data
 * in skb-cb and marks packet as SKBTX_IN_PROGRESS.
 */
void am65_cpts_prep_tx_timestamp(struct am65_cpts *cpts, struct sk_buff *skb)
{
	struct am65_cpts_skb_cb_data *skb_cb = (void *)skb->cb;
	int ret;

	if (!(skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP))
		return;

	ret = am65_skb_get_mtype_seqid(skb, &skb_cb->skb_mtype_seqid);
	if (!ret)
		return;
	skb_cb->skb_mtype_seqid |= (AM65_CPTS_EV_TX <<
				   AM65_CPTS_EVENT_1_EVENT_TYPE_SHIFT);

	skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
}
EXPORT_SYMBOL_GPL(am65_cpts_prep_tx_timestamp);

int am65_cpts_phc_index(struct am65_cpts *cpts)
{
	return cpts->phc_index;
}
EXPORT_SYMBOL_GPL(am65_cpts_phc_index);

static void cpts_free_clk_mux(void *data)
{
	struct am65_cpts *cpts = data;

	of_clk_del_provider(cpts->clk_mux_np);
	clk_hw_unregister_mux(cpts->clk_mux_hw);
	of_node_put(cpts->clk_mux_np);
}

static int cpts_of_mux_clk_setup(struct am65_cpts *cpts,
				 struct device_node *node)
{
	unsigned int num_parents;
	const char **parent_names;
	char *clk_mux_name;
	void __iomem *reg;
	int ret = -EINVAL;

	cpts->clk_mux_np = of_get_child_by_name(node, "refclk-mux");
	if (!cpts->clk_mux_np)
		return 0;

	num_parents = of_clk_get_parent_count(cpts->clk_mux_np);
	if (num_parents < 1) {
		dev_err(cpts->dev, "mux-clock %pOF must have parents\n",
			cpts->clk_mux_np);
		goto mux_fail;
	}

	parent_names = devm_kcalloc(cpts->dev, sizeof(char *), num_parents,
				    GFP_KERNEL);
	if (!parent_names) {
		ret = -ENOMEM;
		goto mux_fail;
	}

	of_clk_parent_fill(cpts->clk_mux_np, parent_names, num_parents);

	clk_mux_name = devm_kasprintf(cpts->dev, GFP_KERNEL, "%s.%pOFn",
				      dev_name(cpts->dev), cpts->clk_mux_np);
	if (!clk_mux_name) {
		ret = -ENOMEM;
		goto mux_fail;
	}

	reg = &cpts->reg->rftclk_sel;
	/* dev must be NULL to avoid recursive incrementing
	 * of module refcnt
	 */
	cpts->clk_mux_hw = clk_hw_register_mux(NULL, clk_mux_name,
					       parent_names, num_parents,
					       0, reg, 0, 5, 0, NULL);
	if (IS_ERR(cpts->clk_mux_hw)) {
		ret = PTR_ERR(cpts->clk_mux_hw);
		goto mux_fail;
	}

	ret = of_clk_add_hw_provider(cpts->clk_mux_np, of_clk_hw_simple_get,
				     cpts->clk_mux_hw);
	if (ret)
		goto clk_hw_register;

	ret = devm_add_action_or_reset(cpts->dev, cpts_free_clk_mux, cpts);
	if (ret)
		dev_err(cpts->dev, "failed to add clkmux reset action %d", ret);

	return ret;

clk_hw_register:
	clk_hw_unregister_mux(cpts->clk_mux_hw);
mux_fail:
	of_node_put(cpts->clk_mux_np);
	return ret;
}

static int am65_cpts_of_parse(struct am65_cpts *cpts, struct device_node *node)
{
	u32 prop[2];

	if (!of_property_read_u32(node, "ti,cpts-ext-ts-inputs", &prop[0]))
		cpts->ext_ts_inputs = prop[0];

	if (!of_property_read_u32(node, "ti,cpts-periodic-outputs", &prop[0]))
		cpts->genf_num = prop[0];

	return cpts_of_mux_clk_setup(cpts, node);
}

static void am65_cpts_release(void *data)
{
	struct am65_cpts *cpts = data;

	ptp_clock_unregister(cpts->ptp_clock);
	am65_cpts_disable(cpts);
	clk_disable_unprepare(cpts->refclk);
}

struct am65_cpts *am65_cpts_create(struct device *dev, void __iomem *regs,
				   struct device_node *node)
{
	struct am65_cpts *cpts;
	int ret, i;

	cpts = devm_kzalloc(dev, sizeof(*cpts), GFP_KERNEL);
	if (!cpts)
		return ERR_PTR(-ENOMEM);

	cpts->dev = dev;
	cpts->reg = (struct am65_cpts_regs __iomem *)regs;

	cpts->irq = of_irq_get_byname(node, "cpts");
	if (cpts->irq <= 0) {
		ret = cpts->irq ?: -ENXIO;
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Failed to get IRQ number (err = %d)\n",
				ret);
		return ERR_PTR(ret);
	}

	ret = am65_cpts_of_parse(cpts, node);
	if (ret)
		return ERR_PTR(ret);

	mutex_init(&cpts->ptp_clk_lock);
	INIT_LIST_HEAD(&cpts->events);
	INIT_LIST_HEAD(&cpts->pool);
	spin_lock_init(&cpts->lock);
	skb_queue_head_init(&cpts->txq);

	for (i = 0; i < AM65_CPTS_MAX_EVENTS; i++)
		list_add(&cpts->pool_data[i].list, &cpts->pool);

	cpts->refclk = devm_get_clk_from_child(dev, node, "cpts");
	if (IS_ERR(cpts->refclk)) {
		ret = PTR_ERR(cpts->refclk);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Failed to get refclk %d\n", ret);
		return ERR_PTR(ret);
	}

	ret = clk_prepare_enable(cpts->refclk);
	if (ret) {
		dev_err(dev, "Failed to enable refclk %d\n", ret);
		return ERR_PTR(ret);
	}

	cpts->refclk_freq = clk_get_rate(cpts->refclk);

	am65_ptp_info.max_adj = cpts->refclk_freq / AM65_CPTS_MIN_PPM;
	cpts->ptp_info = am65_ptp_info;

	if (cpts->ext_ts_inputs)
		cpts->ptp_info.n_ext_ts = cpts->ext_ts_inputs;
	if (cpts->genf_num)
		cpts->ptp_info.n_per_out = cpts->genf_num;

	am65_cpts_set_add_val(cpts);

	am65_cpts_write32(cpts, AM65_CPTS_CONTROL_EN |
			  AM65_CPTS_CONTROL_64MODE |
			  AM65_CPTS_CONTROL_TX_GENF_CLR_EN,
			  control);
	am65_cpts_write32(cpts, AM65_CPTS_INT_ENABLE_TS_PEND_EN, int_enable);

	/* set time to the current system time */
	am65_cpts_settime(cpts, ktime_to_ns(ktime_get_real()));

	cpts->ptp_clock = ptp_clock_register(&cpts->ptp_info, cpts->dev);
	if (IS_ERR_OR_NULL(cpts->ptp_clock)) {
		dev_err(dev, "Failed to register ptp clk %ld\n",
			PTR_ERR(cpts->ptp_clock));
		ret = cpts->ptp_clock ? PTR_ERR(cpts->ptp_clock) : -ENODEV;
		goto refclk_disable;
	}
	cpts->phc_index = ptp_clock_index(cpts->ptp_clock);

	ret = devm_add_action_or_reset(dev, am65_cpts_release, cpts);
	if (ret) {
		dev_err(dev, "failed to add ptpclk reset action %d", ret);
		return ERR_PTR(ret);
	}

	ret = devm_request_threaded_irq(dev, cpts->irq, NULL,
					am65_cpts_interrupt,
					IRQF_ONESHOT, dev_name(dev), cpts);
	if (ret < 0) {
		dev_err(cpts->dev, "error attaching irq %d\n", ret);
		return ERR_PTR(ret);
	}

	dev_info(dev, "CPTS ver 0x%08x, freq:%u, add_val:%u\n",
		 am65_cpts_read32(cpts, idver),
		 cpts->refclk_freq, cpts->ts_add_val);

	return cpts;

refclk_disable:
	clk_disable_unprepare(cpts->refclk);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(am65_cpts_create);

static int am65_cpts_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct am65_cpts *cpts;
	struct resource *res;
	void __iomem *base;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "cpts");
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	cpts = am65_cpts_create(dev, base, node);
	return PTR_ERR_OR_ZERO(cpts);
}

static const struct of_device_id am65_cpts_of_match[] = {
	{ .compatible = "ti,am65-cpts", },
	{ .compatible = "ti,j721e-cpts", },
	{},
};
MODULE_DEVICE_TABLE(of, am65_cpts_of_match);

static struct platform_driver am65_cpts_driver = {
	.probe		= am65_cpts_probe,
	.driver		= {
		.name	= "am65-cpts",
		.of_match_table = am65_cpts_of_match,
	},
};
module_platform_driver(am65_cpts_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Grygorii Strashko <grygorii.strashko@ti.com>");
MODULE_DESCRIPTION("TI K3 AM65 CPTS driver");
