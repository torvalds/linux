// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  linux/drivers/clocksource/timer-sp.c
 *
 *  Copyright (C) 1999 - 2003 ARM Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
 */
#include <linux/clk.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_clk.h>
#include <linux/of_irq.h>
#include <linux/sched_clock.h>

#include <clocksource/timer-sp804.h>

#include "timer-sp.h"

static long __init sp804_get_clock_rate(struct clk *clk)
{
	long rate;
	int err;

	err = clk_prepare(clk);
	if (err) {
		pr_err("sp804: clock failed to prepare: %d\n", err);
		clk_put(clk);
		return err;
	}

	err = clk_enable(clk);
	if (err) {
		pr_err("sp804: clock failed to enable: %d\n", err);
		clk_unprepare(clk);
		clk_put(clk);
		return err;
	}

	rate = clk_get_rate(clk);
	if (rate < 0) {
		pr_err("sp804: clock failed to get rate: %ld\n", rate);
		clk_disable(clk);
		clk_unprepare(clk);
		clk_put(clk);
	}

	return rate;
}

static void __iomem *sched_clock_base;

static u64 notrace sp804_read(void)
{
	return ~readl_relaxed(sched_clock_base + TIMER_VALUE);
}

void __init sp804_timer_disable(void __iomem *base)
{
	writel(0, base + TIMER_CTRL);
}

int  __init __sp804_clocksource_and_sched_clock_init(void __iomem *base,
						     const char *name,
						     struct clk *clk,
						     int use_sched_clock)
{
	long rate;

	if (!clk) {
		clk = clk_get_sys("sp804", name);
		if (IS_ERR(clk)) {
			pr_err("sp804: clock not found: %d\n",
			       (int)PTR_ERR(clk));
			return PTR_ERR(clk);
		}
	}

	rate = sp804_get_clock_rate(clk);
	if (rate < 0)
		return -EINVAL;

	/* setup timer 0 as free-running clocksource */
	writel(0, base + TIMER_CTRL);
	writel(0xffffffff, base + TIMER_LOAD);
	writel(0xffffffff, base + TIMER_VALUE);
	writel(TIMER_CTRL_32BIT | TIMER_CTRL_ENABLE | TIMER_CTRL_PERIODIC,
		base + TIMER_CTRL);

	clocksource_mmio_init(base + TIMER_VALUE, name,
		rate, 200, 32, clocksource_mmio_readl_down);

	if (use_sched_clock) {
		sched_clock_base = base;
		sched_clock_register(sp804_read, 32, rate);
	}

	return 0;
}


static void __iomem *clkevt_base;
static unsigned long clkevt_reload;

/*
 * IRQ handler for the timer
 */
static irqreturn_t sp804_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	/* clear the interrupt */
	writel(1, clkevt_base + TIMER_INTCLR);

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static inline void timer_shutdown(struct clock_event_device *evt)
{
	writel(0, clkevt_base + TIMER_CTRL);
}

static int sp804_shutdown(struct clock_event_device *evt)
{
	timer_shutdown(evt);
	return 0;
}

static int sp804_set_periodic(struct clock_event_device *evt)
{
	unsigned long ctrl = TIMER_CTRL_32BIT | TIMER_CTRL_IE |
			     TIMER_CTRL_PERIODIC | TIMER_CTRL_ENABLE;

	timer_shutdown(evt);
	writel(clkevt_reload, clkevt_base + TIMER_LOAD);
	writel(ctrl, clkevt_base + TIMER_CTRL);
	return 0;
}

static int sp804_set_next_event(unsigned long next,
	struct clock_event_device *evt)
{
	unsigned long ctrl = TIMER_CTRL_32BIT | TIMER_CTRL_IE |
			     TIMER_CTRL_ONESHOT | TIMER_CTRL_ENABLE;

	writel(next, clkevt_base + TIMER_LOAD);
	writel(ctrl, clkevt_base + TIMER_CTRL);

	return 0;
}

static struct clock_event_device sp804_clockevent = {
	.features		= CLOCK_EVT_FEAT_PERIODIC |
				  CLOCK_EVT_FEAT_ONESHOT |
				  CLOCK_EVT_FEAT_DYNIRQ,
	.set_state_shutdown	= sp804_shutdown,
	.set_state_periodic	= sp804_set_periodic,
	.set_state_oneshot	= sp804_shutdown,
	.tick_resume		= sp804_shutdown,
	.set_next_event		= sp804_set_next_event,
	.rating			= 300,
};

int __init __sp804_clockevents_init(void __iomem *base, unsigned int irq, struct clk *clk, const char *name)
{
	struct clock_event_device *evt = &sp804_clockevent;
	long rate;

	if (!clk)
		clk = clk_get_sys("sp804", name);
	if (IS_ERR(clk)) {
		pr_err("sp804: %s clock not found: %d\n", name,
			(int)PTR_ERR(clk));
		return PTR_ERR(clk);
	}

	rate = sp804_get_clock_rate(clk);
	if (rate < 0)
		return -EINVAL;

	clkevt_base = base;
	clkevt_reload = DIV_ROUND_CLOSEST(rate, HZ);
	evt->name = name;
	evt->irq = irq;
	evt->cpumask = cpu_possible_mask;

	writel(0, base + TIMER_CTRL);

	if (request_irq(irq, sp804_timer_interrupt, IRQF_TIMER | IRQF_IRQPOLL,
			"timer", &sp804_clockevent))
		pr_err("%s: request_irq() failed\n", "timer");
	clockevents_config_and_register(evt, rate, 0xf, 0xffffffff);

	return 0;
}

static int __init sp804_of_init(struct device_node *np)
{
	static bool initialized = false;
	void __iomem *base;
	int irq, ret = -EINVAL;
	u32 irq_num = 0;
	struct clk *clk1, *clk2;
	const char *name = of_get_property(np, "compatible", NULL);

	base = of_iomap(np, 0);
	if (!base)
		return -ENXIO;

	/* Ensure timers are disabled */
	writel(0, base + TIMER_CTRL);
	writel(0, base + TIMER_2_BASE + TIMER_CTRL);

	if (initialized || !of_device_is_available(np)) {
		ret = -EINVAL;
		goto err;
	}

	clk1 = of_clk_get(np, 0);
	if (IS_ERR(clk1))
		clk1 = NULL;

	/* Get the 2nd clock if the timer has 3 timer clocks */
	if (of_clk_get_parent_count(np) == 3) {
		clk2 = of_clk_get(np, 1);
		if (IS_ERR(clk2)) {
			pr_err("sp804: %pOFn clock not found: %d\n", np,
				(int)PTR_ERR(clk2));
			clk2 = NULL;
		}
	} else
		clk2 = clk1;

	irq = irq_of_parse_and_map(np, 0);
	if (irq <= 0)
		goto err;

	of_property_read_u32(np, "arm,sp804-has-irq", &irq_num);
	if (irq_num == 2) {

		ret = __sp804_clockevents_init(base + TIMER_2_BASE, irq, clk2, name);
		if (ret)
			goto err;

		ret = __sp804_clocksource_and_sched_clock_init(base, name, clk1, 1);
		if (ret)
			goto err;
	} else {

		ret = __sp804_clockevents_init(base, irq, clk1 , name);
		if (ret)
			goto err;

		ret =__sp804_clocksource_and_sched_clock_init(base + TIMER_2_BASE,
							      name, clk2, 1);
		if (ret)
			goto err;
	}
	initialized = true;

	return 0;
err:
	iounmap(base);
	return ret;
}
TIMER_OF_DECLARE(sp804, "arm,sp804", sp804_of_init);

static int __init integrator_cp_of_init(struct device_node *np)
{
	static int init_count = 0;
	void __iomem *base;
	int irq, ret = -EINVAL;
	const char *name = of_get_property(np, "compatible", NULL);
	struct clk *clk;

	base = of_iomap(np, 0);
	if (!base) {
		pr_err("Failed to iomap\n");
		return -ENXIO;
	}

	clk = of_clk_get(np, 0);
	if (IS_ERR(clk)) {
		pr_err("Failed to get clock\n");
		return PTR_ERR(clk);
	}

	/* Ensure timer is disabled */
	writel(0, base + TIMER_CTRL);

	if (init_count == 2 || !of_device_is_available(np))
		goto err;

	if (!init_count) {
		ret = __sp804_clocksource_and_sched_clock_init(base, name, clk, 0);
		if (ret)
			goto err;
	} else {
		irq = irq_of_parse_and_map(np, 0);
		if (irq <= 0)
			goto err;

		ret = __sp804_clockevents_init(base, irq, clk, name);
		if (ret)
			goto err;
	}

	init_count++;
	return 0;
err:
	iounmap(base);
	return ret;
}
TIMER_OF_DECLARE(intcp, "arm,integrator-cp-timer", integrator_cp_of_init);
