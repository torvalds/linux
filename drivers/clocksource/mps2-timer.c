/*
 * Copyright (C) 2015 ARM Limited
 *
 * Author: Vladimir Murzin <vladimir.murzin@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/of_address.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/sched_clock.h>
#include <linux/slab.h>

#define TIMER_CTRL		0x0
#define TIMER_CTRL_ENABLE	BIT(0)
#define TIMER_CTRL_IE		BIT(3)

#define TIMER_VALUE		0x4
#define TIMER_RELOAD		0x8
#define TIMER_INT		0xc

struct clockevent_mps2 {
	void __iomem *reg;
	u32 clock_count_per_tick;
	struct clock_event_device clkevt;
};

static void __iomem *sched_clock_base;

static u64 notrace mps2_sched_read(void)
{
	return ~readl_relaxed(sched_clock_base + TIMER_VALUE);
}

static inline struct clockevent_mps2 *to_mps2_clkevt(struct clock_event_device *c)
{
	return container_of(c, struct clockevent_mps2, clkevt);
}

static void clockevent_mps2_writel(u32 val, struct clock_event_device *c, u32 offset)
{
	writel_relaxed(val, to_mps2_clkevt(c)->reg + offset);
}

static int mps2_timer_shutdown(struct clock_event_device *ce)
{
	clockevent_mps2_writel(0, ce, TIMER_RELOAD);
	clockevent_mps2_writel(0, ce, TIMER_CTRL);

	return 0;
}

static int mps2_timer_set_next_event(unsigned long next, struct clock_event_device *ce)
{
	clockevent_mps2_writel(next, ce, TIMER_VALUE);
	clockevent_mps2_writel(TIMER_CTRL_IE | TIMER_CTRL_ENABLE, ce, TIMER_CTRL);

	return 0;
}

static int mps2_timer_set_periodic(struct clock_event_device *ce)
{
	u32 clock_count_per_tick = to_mps2_clkevt(ce)->clock_count_per_tick;

	clockevent_mps2_writel(clock_count_per_tick, ce, TIMER_RELOAD);
	clockevent_mps2_writel(clock_count_per_tick, ce, TIMER_VALUE);
	clockevent_mps2_writel(TIMER_CTRL_IE | TIMER_CTRL_ENABLE, ce, TIMER_CTRL);

	return 0;
}

static irqreturn_t mps2_timer_interrupt(int irq, void *dev_id)
{
	struct clockevent_mps2 *ce = dev_id;
	u32 status = readl_relaxed(ce->reg + TIMER_INT);

	if (!status) {
		pr_warn("spurious interrupt\n");
		return IRQ_NONE;
	}

	writel_relaxed(1, ce->reg + TIMER_INT);

	ce->clkevt.event_handler(&ce->clkevt);

	return IRQ_HANDLED;
}

static int __init mps2_clockevent_init(struct device_node *np)
{
	void __iomem *base;
	struct clk *clk = NULL;
	struct clockevent_mps2 *ce;
	u32 rate;
	int irq, ret;
	const char *name = "mps2-clkevt";

	ret = of_property_read_u32(np, "clock-frequency", &rate);
	if (ret) {
		clk = of_clk_get(np, 0);
		if (IS_ERR(clk)) {
			ret = PTR_ERR(clk);
			pr_err("failed to get clock for clockevent: %d\n", ret);
			goto out;
		}

		ret = clk_prepare_enable(clk);
		if (ret) {
			pr_err("failed to enable clock for clockevent: %d\n", ret);
			goto out_clk_put;
		}

		rate = clk_get_rate(clk);
	}

	base = of_iomap(np, 0);
	if (!base) {
		ret = -EADDRNOTAVAIL;
		pr_err("failed to map register for clockevent: %d\n", ret);
		goto out_clk_disable;
	}

	irq = irq_of_parse_and_map(np, 0);
	if (!irq) {
		ret = -ENOENT;
		pr_err("failed to get irq for clockevent: %d\n", ret);
		goto out_iounmap;
	}

	ce = kzalloc(sizeof(*ce), GFP_KERNEL);
	if (!ce) {
		ret = -ENOMEM;
		goto out_iounmap;
	}

	ce->reg = base;
	ce->clock_count_per_tick = DIV_ROUND_CLOSEST(rate, HZ);
	ce->clkevt.irq = irq;
	ce->clkevt.name = name;
	ce->clkevt.rating = 200;
	ce->clkevt.features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT;
	ce->clkevt.cpumask = cpu_possible_mask;
	ce->clkevt.set_state_shutdown	= mps2_timer_shutdown,
	ce->clkevt.set_state_periodic	= mps2_timer_set_periodic,
	ce->clkevt.set_state_oneshot	= mps2_timer_shutdown,
	ce->clkevt.set_next_event	= mps2_timer_set_next_event;

	/* Ensure timer is disabled */
	writel_relaxed(0, base + TIMER_CTRL);

	ret = request_irq(irq, mps2_timer_interrupt, IRQF_TIMER, name, ce);
	if (ret) {
		pr_err("failed to request irq for clockevent: %d\n", ret);
		goto out_kfree;
	}

	clockevents_config_and_register(&ce->clkevt, rate, 0xf, 0xffffffff);

	return 0;

out_kfree:
	kfree(ce);
out_iounmap:
	iounmap(base);
out_clk_disable:
	/* clk_{disable, unprepare, put}() can handle NULL as a parameter */
	clk_disable_unprepare(clk);
out_clk_put:
	clk_put(clk);
out:
	return ret;
}

static int __init mps2_clocksource_init(struct device_node *np)
{
	void __iomem *base;
	struct clk *clk = NULL;
	u32 rate;
	int ret;
	const char *name = "mps2-clksrc";

	ret = of_property_read_u32(np, "clock-frequency", &rate);
	if (ret) {
		clk = of_clk_get(np, 0);
		if (IS_ERR(clk)) {
			ret = PTR_ERR(clk);
			pr_err("failed to get clock for clocksource: %d\n", ret);
			goto out;
		}

		ret = clk_prepare_enable(clk);
		if (ret) {
			pr_err("failed to enable clock for clocksource: %d\n", ret);
			goto out_clk_put;
		}

		rate = clk_get_rate(clk);
	}

	base = of_iomap(np, 0);
	if (!base) {
		ret = -EADDRNOTAVAIL;
		pr_err("failed to map register for clocksource: %d\n", ret);
		goto out_clk_disable;
	}

	/* Ensure timer is disabled */
	writel_relaxed(0, base + TIMER_CTRL);

	/* ... and set it up as free-running clocksource */
	writel_relaxed(0xffffffff, base + TIMER_VALUE);
	writel_relaxed(0xffffffff, base + TIMER_RELOAD);

	writel_relaxed(TIMER_CTRL_ENABLE, base + TIMER_CTRL);

	ret = clocksource_mmio_init(base + TIMER_VALUE, name,
				    rate, 200, 32,
				    clocksource_mmio_readl_down);
	if (ret) {
		pr_err("failed to init clocksource: %d\n", ret);
		goto out_iounmap;
	}

	sched_clock_base = base;
	sched_clock_register(mps2_sched_read, 32, rate);

	return 0;

out_iounmap:
	iounmap(base);
out_clk_disable:
	/* clk_{disable, unprepare, put}() can handle NULL as a parameter */
	clk_disable_unprepare(clk);
out_clk_put:
	clk_put(clk);
out:
	return ret;
}

static void __init mps2_timer_init(struct device_node *np)
{
	static int has_clocksource, has_clockevent;
	int ret;

	if (!has_clocksource) {
		ret = mps2_clocksource_init(np);
		if (!ret) {
			has_clocksource = 1;
			return;
		}
	}

	if (!has_clockevent) {
		ret = mps2_clockevent_init(np);
		if (!ret) {
			has_clockevent = 1;
			return;
		}
	}
}

CLOCKSOURCE_OF_DECLARE(mps2_timer, "arm,mps2-timer", mps2_timer_init);
