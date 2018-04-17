/*
 * Actions Semi Owl timer
 *
 * Copyright 2012 Actions Semi Inc.
 * Author: Actions Semi, Inc.
 *
 * Copyright (c) 2017 SUSE Linux GmbH
 * Author: Andreas Färber
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqreturn.h>
#include <linux/sched_clock.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#define OWL_Tx_CTL		0x0
#define OWL_Tx_CMP		0x4
#define OWL_Tx_VAL		0x8

#define OWL_Tx_CTL_PD		BIT(0)
#define OWL_Tx_CTL_INTEN	BIT(1)
#define OWL_Tx_CTL_EN		BIT(2)

static void __iomem *owl_timer_base;
static void __iomem *owl_clksrc_base;
static void __iomem *owl_clkevt_base;

static inline void owl_timer_reset(void __iomem *base)
{
	writel(0, base + OWL_Tx_CTL);
	writel(0, base + OWL_Tx_VAL);
	writel(0, base + OWL_Tx_CMP);
}

static inline void owl_timer_set_enabled(void __iomem *base, bool enabled)
{
	u32 ctl = readl(base + OWL_Tx_CTL);

	/* PD bit is cleared when set */
	ctl &= ~OWL_Tx_CTL_PD;

	if (enabled)
		ctl |= OWL_Tx_CTL_EN;
	else
		ctl &= ~OWL_Tx_CTL_EN;

	writel(ctl, base + OWL_Tx_CTL);
}

static u64 notrace owl_timer_sched_read(void)
{
	return (u64)readl(owl_clksrc_base + OWL_Tx_VAL);
}

static int owl_timer_set_state_shutdown(struct clock_event_device *evt)
{
	owl_timer_set_enabled(owl_clkevt_base, false);

	return 0;
}

static int owl_timer_set_state_oneshot(struct clock_event_device *evt)
{
	owl_timer_reset(owl_clkevt_base);

	return 0;
}

static int owl_timer_tick_resume(struct clock_event_device *evt)
{
	return 0;
}

static int owl_timer_set_next_event(unsigned long evt,
				    struct clock_event_device *ev)
{
	void __iomem *base = owl_clkevt_base;

	owl_timer_set_enabled(base, false);
	writel(OWL_Tx_CTL_INTEN, base + OWL_Tx_CTL);
	writel(0, base + OWL_Tx_VAL);
	writel(evt, base + OWL_Tx_CMP);
	owl_timer_set_enabled(base, true);

	return 0;
}

static struct clock_event_device owl_clockevent = {
	.name			= "owl_tick",
	.rating			= 200,
	.features		= CLOCK_EVT_FEAT_ONESHOT |
				  CLOCK_EVT_FEAT_DYNIRQ,
	.set_state_shutdown	= owl_timer_set_state_shutdown,
	.set_state_oneshot	= owl_timer_set_state_oneshot,
	.tick_resume		= owl_timer_tick_resume,
	.set_next_event		= owl_timer_set_next_event,
};

static irqreturn_t owl_timer1_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = (struct clock_event_device *)dev_id;

	writel(OWL_Tx_CTL_PD, owl_clkevt_base + OWL_Tx_CTL);

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static int __init owl_timer_init(struct device_node *node)
{
	struct clk *clk;
	unsigned long rate;
	int timer1_irq, ret;

	owl_timer_base = of_io_request_and_map(node, 0, "owl-timer");
	if (IS_ERR(owl_timer_base)) {
		pr_err("Can't map timer registers\n");
		return PTR_ERR(owl_timer_base);
	}

	owl_clksrc_base = owl_timer_base + 0x08;
	owl_clkevt_base = owl_timer_base + 0x14;

	timer1_irq = of_irq_get_byname(node, "timer1");
	if (timer1_irq <= 0) {
		pr_err("Can't parse timer1 IRQ\n");
		return -EINVAL;
	}

	clk = of_clk_get(node, 0);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	rate = clk_get_rate(clk);

	owl_timer_reset(owl_clksrc_base);
	owl_timer_set_enabled(owl_clksrc_base, true);

	sched_clock_register(owl_timer_sched_read, 32, rate);
	clocksource_mmio_init(owl_clksrc_base + OWL_Tx_VAL, node->name,
			      rate, 200, 32, clocksource_mmio_readl_up);

	owl_timer_reset(owl_clkevt_base);

	ret = request_irq(timer1_irq, owl_timer1_interrupt, IRQF_TIMER,
			  "owl-timer", &owl_clockevent);
	if (ret) {
		pr_err("failed to request irq %d\n", timer1_irq);
		return ret;
	}

	owl_clockevent.cpumask = cpumask_of(0);
	owl_clockevent.irq = timer1_irq;

	clockevents_config_and_register(&owl_clockevent, rate,
					0xf, 0xffffffff);

	return 0;
}
TIMER_OF_DECLARE(owl_s500, "actions,s500-timer", owl_timer_init);
TIMER_OF_DECLARE(owl_s700, "actions,s700-timer", owl_timer_init);
TIMER_OF_DECLARE(owl_s900, "actions,s900-timer", owl_timer_init);
