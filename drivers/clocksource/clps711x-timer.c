/*
 *  Cirrus Logic CLPS711X clocksource driver
 *
 *  Copyright (C) 2014 Alexander Shiyan <shc_work@mail.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/sched_clock.h>
#include <linux/slab.h>

enum {
	CLPS711X_CLKSRC_CLOCKSOURCE,
	CLPS711X_CLKSRC_CLOCKEVENT,
};

static void __iomem *tcd;

static u64 notrace clps711x_sched_clock_read(void)
{
	return ~readw(tcd);
}

static void __init clps711x_clksrc_init(struct clk *clock, void __iomem *base)
{
	unsigned long rate = clk_get_rate(clock);

	tcd = base;

	clocksource_mmio_init(tcd, "clps711x-clocksource", rate, 300, 16,
			      clocksource_mmio_readw_down);

	sched_clock_register(clps711x_sched_clock_read, 16, rate);
}

static irqreturn_t clps711x_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static int __init _clps711x_clkevt_init(struct clk *clock, void __iomem *base,
					unsigned int irq)
{
	struct clock_event_device *clkevt;
	unsigned long rate;

	clkevt = kzalloc(sizeof(*clkevt), GFP_KERNEL);
	if (!clkevt)
		return -ENOMEM;

	rate = clk_get_rate(clock);

	/* Set Timer prescaler */
	writew(DIV_ROUND_CLOSEST(rate, HZ), base);

	clkevt->name = "clps711x-clockevent";
	clkevt->rating = 300;
	clkevt->features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_C3STOP;
	clkevt->cpumask = cpumask_of(0);
	clockevents_config_and_register(clkevt, HZ, 0, 0);

	return request_irq(irq, clps711x_timer_interrupt, IRQF_TIMER,
			   "clps711x-timer", clkevt);
}

static int __init clps711x_timer_init(struct device_node *np)
{
	unsigned int irq = irq_of_parse_and_map(np, 0);
	struct clk *clock = of_clk_get(np, 0);
	void __iomem *base = of_iomap(np, 0);

	if (!base)
		return -ENOMEM;
	if (!irq)
		return -EINVAL;
	if (IS_ERR(clock))
		return PTR_ERR(clock);

	switch (of_alias_get_id(np, "timer")) {
	case CLPS711X_CLKSRC_CLOCKSOURCE:
		clps711x_clksrc_init(clock, base);
		break;
	case CLPS711X_CLKSRC_CLOCKEVENT:
		return _clps711x_clkevt_init(clock, base, irq);
	default:
		return -EINVAL;
	}

	return 0;
}
TIMER_OF_DECLARE(clps711x, "cirrus,ep7209-timer", clps711x_timer_init);
