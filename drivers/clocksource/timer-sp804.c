// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  linux/drivers/clocksource/timer-sp.c
 *
 *  Copyright (C) 1999 - 2003 ARM Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
 */

#define pr_fmt(fmt)    KBUILD_MODNAME ": " fmt

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

#include "timer-sp.h"

/* Hisilicon 64-bit timer(a variant of ARM SP804) */
#define HISI_TIMER_1_BASE	0x00
#define HISI_TIMER_2_BASE	0x40
#define HISI_TIMER_LOAD		0x00
#define HISI_TIMER_LOAD_H	0x04
#define HISI_TIMER_VALUE	0x08
#define HISI_TIMER_VALUE_H	0x0c
#define HISI_TIMER_CTRL		0x10
#define HISI_TIMER_INTCLR	0x14
#define HISI_TIMER_RIS		0x18
#define HISI_TIMER_MIS		0x1c
#define HISI_TIMER_BGLOAD	0x20
#define HISI_TIMER_BGLOAD_H	0x24

static struct sp804_timer arm_sp804_timer __initdata = {
	.load		= TIMER_LOAD,
	.value		= TIMER_VALUE,
	.ctrl		= TIMER_CTRL,
	.intclr		= TIMER_INTCLR,
	.timer_base	= {TIMER_1_BASE, TIMER_2_BASE},
	.width		= 32,
};

static struct sp804_timer hisi_sp804_timer __initdata = {
	.load		= HISI_TIMER_LOAD,
	.load_h		= HISI_TIMER_LOAD_H,
	.value		= HISI_TIMER_VALUE,
	.value_h	= HISI_TIMER_VALUE_H,
	.ctrl		= HISI_TIMER_CTRL,
	.intclr		= HISI_TIMER_INTCLR,
	.timer_base	= {HISI_TIMER_1_BASE, HISI_TIMER_2_BASE},
	.width		= 64,
};

static struct sp804_clkevt sp804_clkevt[NR_TIMERS];

static long __init sp804_get_clock_rate(struct clk *clk, const char *name)
{
	int err;

	if (!clk)
		clk = clk_get_sys("sp804", name);
	if (IS_ERR(clk)) {
		pr_err("%s clock not found: %ld\n", name, PTR_ERR(clk));
		return PTR_ERR(clk);
	}

	err = clk_prepare_enable(clk);
	if (err) {
		pr_err("clock failed to enable: %d\n", err);
		clk_put(clk);
		return err;
	}

	return clk_get_rate(clk);
}

static struct sp804_clkevt * __init sp804_clkevt_get(void __iomem *base)
{
	int i;

	for (i = 0; i < NR_TIMERS; i++) {
		if (sp804_clkevt[i].base == base)
			return &sp804_clkevt[i];
	}

	/* It's impossible to reach here */
	WARN_ON(1);

	return NULL;
}

static struct sp804_clkevt *sched_clkevt;

static u64 notrace sp804_read(void)
{
	return ~readl_relaxed(sched_clkevt->value);
}

static int __init sp804_clocksource_and_sched_clock_init(void __iomem *base,
							 const char *name,
							 struct clk *clk,
							 int use_sched_clock)
{
	long rate;
	struct sp804_clkevt *clkevt;

	rate = sp804_get_clock_rate(clk, name);
	if (rate < 0)
		return -EINVAL;

	clkevt = sp804_clkevt_get(base);

	writel(0, clkevt->ctrl);
	writel(0xffffffff, clkevt->load);
	writel(0xffffffff, clkevt->value);
	if (clkevt->width == 64) {
		writel(0xffffffff, clkevt->load_h);
		writel(0xffffffff, clkevt->value_h);
	}
	writel(TIMER_CTRL_32BIT | TIMER_CTRL_ENABLE | TIMER_CTRL_PERIODIC,
		clkevt->ctrl);

	clocksource_mmio_init(clkevt->value, name,
		rate, 200, 32, clocksource_mmio_readl_down);

	if (use_sched_clock) {
		sched_clkevt = clkevt;
		sched_clock_register(sp804_read, 32, rate);
	}

	return 0;
}


static struct sp804_clkevt *common_clkevt;

/*
 * IRQ handler for the timer
 */
static irqreturn_t sp804_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	/* clear the interrupt */
	writel(1, common_clkevt->intclr);

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static inline void timer_shutdown(struct clock_event_device *evt)
{
	writel(0, common_clkevt->ctrl);
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
	writel(common_clkevt->reload, common_clkevt->load);
	writel(ctrl, common_clkevt->ctrl);
	return 0;
}

static int sp804_set_next_event(unsigned long next,
	struct clock_event_device *evt)
{
	unsigned long ctrl = TIMER_CTRL_32BIT | TIMER_CTRL_IE |
			     TIMER_CTRL_ONESHOT | TIMER_CTRL_ENABLE;

	writel(next, common_clkevt->load);
	writel(ctrl, common_clkevt->ctrl);

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

static int __init sp804_clockevents_init(void __iomem *base, unsigned int irq,
					 struct clk *clk, const char *name)
{
	struct clock_event_device *evt = &sp804_clockevent;
	long rate;

	rate = sp804_get_clock_rate(clk, name);
	if (rate < 0)
		return -EINVAL;

	common_clkevt = sp804_clkevt_get(base);
	common_clkevt->reload = DIV_ROUND_CLOSEST(rate, HZ);
	evt->name = name;
	evt->irq = irq;
	evt->cpumask = cpu_possible_mask;

	writel(0, common_clkevt->ctrl);

	if (request_irq(irq, sp804_timer_interrupt, IRQF_TIMER | IRQF_IRQPOLL,
			"timer", &sp804_clockevent))
		pr_err("request_irq() failed\n");
	clockevents_config_and_register(evt, rate, 0xf, 0xffffffff);

	return 0;
}

static void __init sp804_clkevt_init(struct sp804_timer *timer, void __iomem *base)
{
	int i;

	for (i = 0; i < NR_TIMERS; i++) {
		void __iomem *timer_base;
		struct sp804_clkevt *clkevt;

		timer_base = base + timer->timer_base[i];
		clkevt = &sp804_clkevt[i];
		clkevt->base	= timer_base;
		clkevt->load	= timer_base + timer->load;
		clkevt->load_h	= timer_base + timer->load_h;
		clkevt->value	= timer_base + timer->value;
		clkevt->value_h	= timer_base + timer->value_h;
		clkevt->ctrl	= timer_base + timer->ctrl;
		clkevt->intclr	= timer_base + timer->intclr;
		clkevt->width	= timer->width;
	}
}

static int __init sp804_of_init(struct device_node *np, struct sp804_timer *timer)
{
	static bool initialized = false;
	void __iomem *base;
	void __iomem *timer1_base;
	void __iomem *timer2_base;
	int irq, ret = -EINVAL;
	u32 irq_num = 0;
	struct clk *clk1, *clk2;
	const char *name = of_get_property(np, "compatible", NULL);

	if (initialized) {
		pr_debug("%pOF: skipping further SP804 timer device\n", np);
		return 0;
	}

	base = of_iomap(np, 0);
	if (!base)
		return -ENXIO;

	timer1_base = base + timer->timer_base[0];
	timer2_base = base + timer->timer_base[1];

	/* Ensure timers are disabled */
	writel(0, timer1_base + timer->ctrl);
	writel(0, timer2_base + timer->ctrl);

	clk1 = of_clk_get(np, 0);
	if (IS_ERR(clk1))
		clk1 = NULL;

	/* Get the 2nd clock if the timer has 3 timer clocks */
	if (of_clk_get_parent_count(np) == 3) {
		clk2 = of_clk_get(np, 1);
		if (IS_ERR(clk2)) {
			pr_err("%pOFn clock not found: %d\n", np,
				(int)PTR_ERR(clk2));
			clk2 = NULL;
		}
	} else
		clk2 = clk1;

	irq = irq_of_parse_and_map(np, 0);
	if (irq <= 0)
		goto err;

	sp804_clkevt_init(timer, base);

	of_property_read_u32(np, "arm,sp804-has-irq", &irq_num);
	if (irq_num == 2) {

		ret = sp804_clockevents_init(timer2_base, irq, clk2, name);
		if (ret)
			goto err;

		ret = sp804_clocksource_and_sched_clock_init(timer1_base,
							     name, clk1, 1);
		if (ret)
			goto err;
	} else {

		ret = sp804_clockevents_init(timer1_base, irq, clk1, name);
		if (ret)
			goto err;

		ret = sp804_clocksource_and_sched_clock_init(timer2_base,
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

static int __init arm_sp804_of_init(struct device_node *np)
{
	return sp804_of_init(np, &arm_sp804_timer);
}
TIMER_OF_DECLARE(sp804, "arm,sp804", arm_sp804_of_init);

static int __init hisi_sp804_of_init(struct device_node *np)
{
	return sp804_of_init(np, &hisi_sp804_timer);
}
TIMER_OF_DECLARE(hisi_sp804, "hisilicon,sp804", hisi_sp804_of_init);

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
	writel(0, base + arm_sp804_timer.ctrl);

	if (init_count == 2 || !of_device_is_available(np))
		goto err;

	sp804_clkevt_init(&arm_sp804_timer, base);

	if (!init_count) {
		ret = sp804_clocksource_and_sched_clock_init(base,
							     name, clk, 0);
		if (ret)
			goto err;
	} else {
		irq = irq_of_parse_and_map(np, 0);
		if (irq <= 0)
			goto err;

		ret = sp804_clockevents_init(base, irq, clk, name);
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
