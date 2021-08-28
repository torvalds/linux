// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PTP 1588 clock using the IXP46X
 *
 * Copyright (C) 2010 OMICRON electronics GmbH
 */
#include <linux/device.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/platform_device.h>
#include <linux/soc/ixp4xx/cpu.h>
#include <linux/module.h>
#include <mach/ixp4xx-regs.h>

#include "ixp46x_ts.h"

#define DRIVER		"ptp_ixp46x"
#define N_EXT_TS	2

struct ixp_clock {
	struct ixp46x_ts_regs *regs;
	struct ptp_clock *ptp_clock;
	struct ptp_clock_info caps;
	int exts0_enabled;
	int exts1_enabled;
	int slave_irq;
	int master_irq;
};

static DEFINE_SPINLOCK(register_lock);

/*
 * Register access functions
 */

static u64 ixp_systime_read(struct ixp46x_ts_regs *regs)
{
	u64 ns;
	u32 lo, hi;

	lo = __raw_readl(&regs->systime_lo);
	hi = __raw_readl(&regs->systime_hi);

	ns = ((u64) hi) << 32;
	ns |= lo;
	ns <<= TICKS_NS_SHIFT;

	return ns;
}

static void ixp_systime_write(struct ixp46x_ts_regs *regs, u64 ns)
{
	u32 hi, lo;

	ns >>= TICKS_NS_SHIFT;
	hi = ns >> 32;
	lo = ns & 0xffffffff;

	__raw_writel(lo, &regs->systime_lo);
	__raw_writel(hi, &regs->systime_hi);
}

/*
 * Interrupt service routine
 */

static irqreturn_t isr(int irq, void *priv)
{
	struct ixp_clock *ixp_clock = priv;
	struct ixp46x_ts_regs *regs = ixp_clock->regs;
	struct ptp_clock_event event;
	u32 ack = 0, lo, hi, val;

	val = __raw_readl(&regs->event);

	if (val & TSER_SNS) {
		ack |= TSER_SNS;
		if (ixp_clock->exts0_enabled) {
			hi = __raw_readl(&regs->asms_hi);
			lo = __raw_readl(&regs->asms_lo);
			event.type = PTP_CLOCK_EXTTS;
			event.index = 0;
			event.timestamp = ((u64) hi) << 32;
			event.timestamp |= lo;
			event.timestamp <<= TICKS_NS_SHIFT;
			ptp_clock_event(ixp_clock->ptp_clock, &event);
		}
	}

	if (val & TSER_SNM) {
		ack |= TSER_SNM;
		if (ixp_clock->exts1_enabled) {
			hi = __raw_readl(&regs->amms_hi);
			lo = __raw_readl(&regs->amms_lo);
			event.type = PTP_CLOCK_EXTTS;
			event.index = 1;
			event.timestamp = ((u64) hi) << 32;
			event.timestamp |= lo;
			event.timestamp <<= TICKS_NS_SHIFT;
			ptp_clock_event(ixp_clock->ptp_clock, &event);
		}
	}

	if (val & TTIPEND)
		ack |= TTIPEND; /* this bit seems to be always set */

	if (ack) {
		__raw_writel(ack, &regs->event);
		return IRQ_HANDLED;
	} else
		return IRQ_NONE;
}

/*
 * PTP clock operations
 */

static int ptp_ixp_adjfreq(struct ptp_clock_info *ptp, s32 ppb)
{
	u64 adj;
	u32 diff, addend;
	int neg_adj = 0;
	struct ixp_clock *ixp_clock = container_of(ptp, struct ixp_clock, caps);
	struct ixp46x_ts_regs *regs = ixp_clock->regs;

	if (ppb < 0) {
		neg_adj = 1;
		ppb = -ppb;
	}
	addend = DEFAULT_ADDEND;
	adj = addend;
	adj *= ppb;
	diff = div_u64(adj, 1000000000ULL);

	addend = neg_adj ? addend - diff : addend + diff;

	__raw_writel(addend, &regs->addend);

	return 0;
}

static int ptp_ixp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	s64 now;
	unsigned long flags;
	struct ixp_clock *ixp_clock = container_of(ptp, struct ixp_clock, caps);
	struct ixp46x_ts_regs *regs = ixp_clock->regs;

	spin_lock_irqsave(&register_lock, flags);

	now = ixp_systime_read(regs);
	now += delta;
	ixp_systime_write(regs, now);

	spin_unlock_irqrestore(&register_lock, flags);

	return 0;
}

static int ptp_ixp_gettime(struct ptp_clock_info *ptp, struct timespec64 *ts)
{
	u64 ns;
	unsigned long flags;
	struct ixp_clock *ixp_clock = container_of(ptp, struct ixp_clock, caps);
	struct ixp46x_ts_regs *regs = ixp_clock->regs;

	spin_lock_irqsave(&register_lock, flags);

	ns = ixp_systime_read(regs);

	spin_unlock_irqrestore(&register_lock, flags);

	*ts = ns_to_timespec64(ns);
	return 0;
}

static int ptp_ixp_settime(struct ptp_clock_info *ptp,
			   const struct timespec64 *ts)
{
	u64 ns;
	unsigned long flags;
	struct ixp_clock *ixp_clock = container_of(ptp, struct ixp_clock, caps);
	struct ixp46x_ts_regs *regs = ixp_clock->regs;

	ns = timespec64_to_ns(ts);

	spin_lock_irqsave(&register_lock, flags);

	ixp_systime_write(regs, ns);

	spin_unlock_irqrestore(&register_lock, flags);

	return 0;
}

static int ptp_ixp_enable(struct ptp_clock_info *ptp,
			  struct ptp_clock_request *rq, int on)
{
	struct ixp_clock *ixp_clock = container_of(ptp, struct ixp_clock, caps);

	switch (rq->type) {
	case PTP_CLK_REQ_EXTTS:
		switch (rq->extts.index) {
		case 0:
			ixp_clock->exts0_enabled = on ? 1 : 0;
			break;
		case 1:
			ixp_clock->exts1_enabled = on ? 1 : 0;
			break;
		default:
			return -EINVAL;
		}
		return 0;
	default:
		break;
	}

	return -EOPNOTSUPP;
}

static const struct ptp_clock_info ptp_ixp_caps = {
	.owner		= THIS_MODULE,
	.name		= "IXP46X timer",
	.max_adj	= 66666655,
	.n_ext_ts	= N_EXT_TS,
	.n_pins		= 0,
	.pps		= 0,
	.adjfreq	= ptp_ixp_adjfreq,
	.adjtime	= ptp_ixp_adjtime,
	.gettime64	= ptp_ixp_gettime,
	.settime64	= ptp_ixp_settime,
	.enable		= ptp_ixp_enable,
};

/* module operations */

static struct ixp_clock ixp_clock;

int ixp46x_ptp_find(struct ixp46x_ts_regs *__iomem *regs, int *phc_index)
{
	*regs = ixp_clock.regs;
	*phc_index = ptp_clock_index(ixp_clock.ptp_clock);

	if (!ixp_clock.ptp_clock)
		return -EPROBE_DEFER;

	return 0;
}
EXPORT_SYMBOL_GPL(ixp46x_ptp_find);

/* Called from the registered devm action */
static void ptp_ixp_unregister_action(void *d)
{
	struct ptp_clock *ptp_clock = d;

	ptp_clock_unregister(ptp_clock);
	ixp_clock.ptp_clock = NULL;
}

static int ptp_ixp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret;

	ixp_clock.regs = devm_platform_ioremap_resource(pdev, 0);
	ixp_clock.master_irq = platform_get_irq(pdev, 0);
	ixp_clock.slave_irq = platform_get_irq(pdev, 1);
	if (IS_ERR(ixp_clock.regs) ||
	    !ixp_clock.master_irq || !ixp_clock.slave_irq)
		return -ENXIO;

	ixp_clock.caps = ptp_ixp_caps;

	ixp_clock.ptp_clock = ptp_clock_register(&ixp_clock.caps, NULL);

	if (IS_ERR(ixp_clock.ptp_clock))
		return PTR_ERR(ixp_clock.ptp_clock);

	ret = devm_add_action_or_reset(dev, ptp_ixp_unregister_action,
				       ixp_clock.ptp_clock);
	if (ret) {
		dev_err(dev, "failed to install clock removal handler\n");
		return ret;
	}

	__raw_writel(DEFAULT_ADDEND, &ixp_clock.regs->addend);
	__raw_writel(1, &ixp_clock.regs->trgt_lo);
	__raw_writel(0, &ixp_clock.regs->trgt_hi);
	__raw_writel(TTIPEND, &ixp_clock.regs->event);

	ret = devm_request_irq(dev, ixp_clock.master_irq, isr,
			       0, DRIVER, &ixp_clock);
	if (ret)
		return dev_err_probe(dev, ret,
				     "request_irq failed for irq %d\n",
				     ixp_clock.master_irq);

	ret = devm_request_irq(dev, ixp_clock.slave_irq, isr,
			       0, DRIVER, &ixp_clock);
	if (ret)
		return dev_err_probe(dev, ret,
				     "request_irq failed for irq %d\n",
				     ixp_clock.slave_irq);

	return 0;
}

static struct platform_driver ptp_ixp_driver = {
	.driver.name = "ptp-ixp46x",
	.driver.suppress_bind_attrs = true,
	.probe = ptp_ixp_probe,
};
module_platform_driver(ptp_ixp_driver);

MODULE_AUTHOR("Richard Cochran <richardcochran@gmail.com>");
MODULE_DESCRIPTION("PTP clock using the IXP46X timer");
MODULE_LICENSE("GPL");
