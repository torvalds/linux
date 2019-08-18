// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Socionext Inc.
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqreturn.h>
#include <linux/sched_clock.h>
#include "timer-of.h"

#define MLB_TMR_TMCSR_OFS	0x0
#define MLB_TMR_TMR_OFS		0x4
#define MLB_TMR_TMRLR1_OFS	0x8
#define MLB_TMR_TMRLR2_OFS	0xc
#define MLB_TMR_REGSZPCH	0x10

#define MLB_TMR_TMCSR_OUTL	BIT(5)
#define MLB_TMR_TMCSR_RELD	BIT(4)
#define MLB_TMR_TMCSR_INTE	BIT(3)
#define MLB_TMR_TMCSR_UF	BIT(2)
#define MLB_TMR_TMCSR_CNTE	BIT(1)
#define MLB_TMR_TMCSR_TRG	BIT(0)

#define MLB_TMR_TMCSR_CSL_DIV2	0
#define MLB_TMR_DIV_CNT		2

#define MLB_TMR_SRC_CH  (1)
#define MLB_TMR_EVT_CH  (0)

#define MLB_TMR_SRC_CH_OFS	(MLB_TMR_REGSZPCH * MLB_TMR_SRC_CH)
#define MLB_TMR_EVT_CH_OFS	(MLB_TMR_REGSZPCH * MLB_TMR_EVT_CH)

#define MLB_TMR_SRC_TMCSR_OFS	(MLB_TMR_SRC_CH_OFS + MLB_TMR_TMCSR_OFS)
#define MLB_TMR_SRC_TMR_OFS	(MLB_TMR_SRC_CH_OFS + MLB_TMR_TMR_OFS)
#define MLB_TMR_SRC_TMRLR1_OFS	(MLB_TMR_SRC_CH_OFS + MLB_TMR_TMRLR1_OFS)
#define MLB_TMR_SRC_TMRLR2_OFS	(MLB_TMR_SRC_CH_OFS + MLB_TMR_TMRLR2_OFS)

#define MLB_TMR_EVT_TMCSR_OFS	(MLB_TMR_EVT_CH_OFS + MLB_TMR_TMCSR_OFS)
#define MLB_TMR_EVT_TMR_OFS	(MLB_TMR_EVT_CH_OFS + MLB_TMR_TMR_OFS)
#define MLB_TMR_EVT_TMRLR1_OFS	(MLB_TMR_EVT_CH_OFS + MLB_TMR_TMRLR1_OFS)
#define MLB_TMR_EVT_TMRLR2_OFS	(MLB_TMR_EVT_CH_OFS + MLB_TMR_TMRLR2_OFS)

#define MLB_TIMER_RATING	500

static irqreturn_t mlb_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *clk = dev_id;
	struct timer_of *to = to_timer_of(clk);
	u32 val;

	val = readl_relaxed(timer_of_base(to) + MLB_TMR_EVT_TMCSR_OFS);
	val &= ~MLB_TMR_TMCSR_UF;
	writel_relaxed(val, timer_of_base(to) + MLB_TMR_EVT_TMCSR_OFS);

	clk->event_handler(clk);

	return IRQ_HANDLED;
}

static int mlb_set_state_periodic(struct clock_event_device *clk)
{
	struct timer_of *to = to_timer_of(clk);
	u32 val = MLB_TMR_TMCSR_CSL_DIV2;

	writel_relaxed(val, timer_of_base(to) + MLB_TMR_EVT_TMCSR_OFS);

	writel_relaxed(to->of_clk.period, timer_of_base(to) +
				MLB_TMR_EVT_TMRLR1_OFS);
	val |= MLB_TMR_TMCSR_RELD | MLB_TMR_TMCSR_CNTE |
		MLB_TMR_TMCSR_TRG | MLB_TMR_TMCSR_INTE;
	writel_relaxed(val, timer_of_base(to) + MLB_TMR_EVT_TMCSR_OFS);
	return 0;
}

static int mlb_set_state_oneshot(struct clock_event_device *clk)
{
	struct timer_of *to = to_timer_of(clk);
	u32 val = MLB_TMR_TMCSR_CSL_DIV2;

	writel_relaxed(val, timer_of_base(to) + MLB_TMR_EVT_TMCSR_OFS);
	return 0;
}

static int mlb_clkevt_next_event(unsigned long event,
				   struct clock_event_device *clk)
{
	struct timer_of *to = to_timer_of(clk);

	writel_relaxed(event, timer_of_base(to) + MLB_TMR_EVT_TMRLR1_OFS);
	writel_relaxed(MLB_TMR_TMCSR_CSL_DIV2 |
			MLB_TMR_TMCSR_CNTE | MLB_TMR_TMCSR_INTE |
			MLB_TMR_TMCSR_TRG, timer_of_base(to) +
			MLB_TMR_EVT_TMCSR_OFS);
	return 0;
}

static int mlb_config_clock_source(struct timer_of *to)
{
	writel_relaxed(0, timer_of_base(to) + MLB_TMR_SRC_TMCSR_OFS);
	writel_relaxed(~0, timer_of_base(to) + MLB_TMR_SRC_TMR_OFS);
	writel_relaxed(~0, timer_of_base(to) + MLB_TMR_SRC_TMRLR1_OFS);
	writel_relaxed(~0, timer_of_base(to) + MLB_TMR_SRC_TMRLR2_OFS);
	writel_relaxed(BIT(4) | BIT(1) | BIT(0), timer_of_base(to) +
		MLB_TMR_SRC_TMCSR_OFS);
	return 0;
}

static int mlb_config_clock_event(struct timer_of *to)
{
	writel_relaxed(0, timer_of_base(to) + MLB_TMR_EVT_TMCSR_OFS);
	return 0;
}

static struct timer_of to = {
	.flags = TIMER_OF_IRQ | TIMER_OF_BASE | TIMER_OF_CLOCK,

	.clkevt = {
		.name = "mlb-clkevt",
		.rating = MLB_TIMER_RATING,
		.cpumask = cpu_possible_mask,
		.features = CLOCK_EVT_FEAT_DYNIRQ | CLOCK_EVT_FEAT_ONESHOT,
		.set_state_oneshot = mlb_set_state_oneshot,
		.set_state_periodic = mlb_set_state_periodic,
		.set_next_event = mlb_clkevt_next_event,
	},

	.of_irq = {
		.flags = IRQF_TIMER | IRQF_IRQPOLL,
		.handler = mlb_timer_interrupt,
	},
};

static u64 notrace mlb_timer_sched_read(void)
{
	return ~readl_relaxed(timer_of_base(&to) + MLB_TMR_SRC_TMR_OFS);
}

static int __init mlb_timer_init(struct device_node *node)
{
	int ret;
	unsigned long rate;

	ret = timer_of_init(node, &to);
	if (ret)
		return ret;

	rate = timer_of_rate(&to) / MLB_TMR_DIV_CNT;
	mlb_config_clock_source(&to);
	clocksource_mmio_init(timer_of_base(&to) + MLB_TMR_SRC_TMR_OFS,
		node->name, rate, MLB_TIMER_RATING, 32,
		clocksource_mmio_readl_down);
	sched_clock_register(mlb_timer_sched_read, 32, rate);
	mlb_config_clock_event(&to);
	clockevents_config_and_register(&to.clkevt, timer_of_rate(&to), 15,
		0xffffffff);
	return 0;
}
TIMER_OF_DECLARE(mlb_peritimer, "socionext,milbeaut-timer",
		mlb_timer_init);
