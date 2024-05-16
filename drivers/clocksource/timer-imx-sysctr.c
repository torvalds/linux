// SPDX-License-Identifier: GPL-2.0+
//
// Copyright 2017-2019 NXP

#include <linux/interrupt.h>
#include <linux/clockchips.h>
#include <linux/slab.h>

#include "timer-of.h"

#define CMP_OFFSET	0x10000
#define RD_OFFSET	0x20000

#define CNTCV_LO	0x8
#define CNTCV_HI	0xc
#define CMPCV_LO	(CMP_OFFSET + 0x20)
#define CMPCV_HI	(CMP_OFFSET + 0x24)
#define CMPCR		(CMP_OFFSET + 0x2c)
#define CNTCV_LO_IMX95	(RD_OFFSET + 0x8)
#define CNTCV_HI_IMX95	(RD_OFFSET + 0xc)

#define SYS_CTR_EN		0x1
#define SYS_CTR_IRQ_MASK	0x2

#define SYS_CTR_CLK_DIV		0x3

struct sysctr_private {
	u32 cmpcr;
	u32 lo_off;
	u32 hi_off;
};

static void sysctr_timer_enable(struct clock_event_device *evt, bool enable)
{
	struct timer_of *to = to_timer_of(evt);
	struct sysctr_private *priv = to->private_data;
	void __iomem *base = timer_of_base(to);

	writel(enable ? priv->cmpcr | SYS_CTR_EN : priv->cmpcr, base + CMPCR);
}

static void sysctr_irq_acknowledge(struct clock_event_device *evt)
{
	/*
	 * clear the enable bit(EN =0) will clear
	 * the status bit(ISTAT = 0), then the interrupt
	 * signal will be negated(acknowledged).
	 */
	sysctr_timer_enable(evt, false);
}

static inline u64 sysctr_read_counter(struct clock_event_device *evt)
{
	struct timer_of *to = to_timer_of(evt);
	struct sysctr_private *priv = to->private_data;
	void __iomem *base = timer_of_base(to);
	u32 cnt_hi, tmp_hi, cnt_lo;

	do {
		cnt_hi = readl_relaxed(base + priv->hi_off);
		cnt_lo = readl_relaxed(base + priv->lo_off);
		tmp_hi = readl_relaxed(base + priv->hi_off);
	} while (tmp_hi != cnt_hi);

	return  ((u64) cnt_hi << 32) | cnt_lo;
}

static int sysctr_set_next_event(unsigned long delta,
				 struct clock_event_device *evt)
{
	struct timer_of *to = to_timer_of(evt);
	void __iomem *base = timer_of_base(to);
	u32 cmp_hi, cmp_lo;
	u64 next;

	sysctr_timer_enable(evt, false);

	next = sysctr_read_counter(evt);

	next += delta;

	cmp_hi = (next >> 32) & 0x00fffff;
	cmp_lo = next & 0xffffffff;

	writel_relaxed(cmp_hi, base + CMPCV_HI);
	writel_relaxed(cmp_lo, base + CMPCV_LO);

	sysctr_timer_enable(evt, true);

	return 0;
}

static int sysctr_set_state_oneshot(struct clock_event_device *evt)
{
	return 0;
}

static int sysctr_set_state_shutdown(struct clock_event_device *evt)
{
	sysctr_timer_enable(evt, false);

	return 0;
}

static irqreturn_t sysctr_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	sysctr_irq_acknowledge(evt);

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static struct timer_of to_sysctr = {
	.flags = TIMER_OF_IRQ | TIMER_OF_CLOCK | TIMER_OF_BASE,
	.clkevt = {
		.name			= "i.MX system counter timer",
		.features		= CLOCK_EVT_FEAT_ONESHOT |
						CLOCK_EVT_FEAT_DYNIRQ,
		.set_state_oneshot	= sysctr_set_state_oneshot,
		.set_next_event		= sysctr_set_next_event,
		.set_state_shutdown	= sysctr_set_state_shutdown,
		.rating			= 200,
	},
	.of_irq = {
		.handler		= sysctr_timer_interrupt,
		.flags			= IRQF_TIMER,
	},
	.of_clk = {
		.name = "per",
	},
};

static int __init __sysctr_timer_init(struct device_node *np)
{
	struct sysctr_private *priv;
	void __iomem *base;
	int ret;

	priv = kzalloc(sizeof(struct sysctr_private), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ret = timer_of_init(np, &to_sysctr);
	if (ret) {
		kfree(priv);
		return ret;
	}

	if (!of_property_read_bool(np, "nxp,no-divider")) {
		/* system counter clock is divided by 3 internally */
		to_sysctr.of_clk.rate /= SYS_CTR_CLK_DIV;
	}

	to_sysctr.clkevt.cpumask = cpu_possible_mask;
	to_sysctr.private_data = priv;

	base = timer_of_base(&to_sysctr);
	priv->cmpcr = readl(base + CMPCR) & ~SYS_CTR_EN;

	return 0;
}

static int __init sysctr_timer_init(struct device_node *np)
{
	struct sysctr_private *priv;
	int ret;

	ret = __sysctr_timer_init(np);
	if (ret)
		return ret;

	priv = to_sysctr.private_data;
	priv->lo_off = CNTCV_LO;
	priv->hi_off = CNTCV_HI;

	clockevents_config_and_register(&to_sysctr.clkevt,
					timer_of_rate(&to_sysctr),
					0xff, 0x7fffffff);

	return 0;
}

static int __init sysctr_timer_imx95_init(struct device_node *np)
{
	struct sysctr_private *priv;
	int ret;

	ret = __sysctr_timer_init(np);
	if (ret)
		return ret;

	priv = to_sysctr.private_data;
	priv->lo_off = CNTCV_LO_IMX95;
	priv->hi_off = CNTCV_HI_IMX95;

	clockevents_config_and_register(&to_sysctr.clkevt,
					timer_of_rate(&to_sysctr),
					0xff, 0x7fffffff);

	return 0;
}

TIMER_OF_DECLARE(sysctr_timer, "nxp,sysctr-timer", sysctr_timer_init);
TIMER_OF_DECLARE(sysctr_timer_imx95, "nxp,imx95-sysctr-timer", sysctr_timer_imx95_init);
