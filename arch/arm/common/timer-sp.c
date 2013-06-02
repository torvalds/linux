/*
 *  linux/arch/arm/common/timer-sp.c
 *
 *  Copyright (C) 1999 - 2003 ARM Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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
#include <linux/of_irq.h>
#include <linux/sched_clock.h>

#include <asm/hardware/arm_timer.h>
#include <asm/hardware/timer-sp.h>

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

static u32 sp804_read(void)
{
	return ~readl_relaxed(sched_clock_base + TIMER_VALUE);
}

void __init __sp804_clocksource_and_sched_clock_init(void __iomem *base,
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
			return;
		}
	}

	rate = sp804_get_clock_rate(clk);

	if (rate < 0)
		return;

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
		setup_sched_clock(sp804_read, 32, rate);
	}
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

static void sp804_set_mode(enum clock_event_mode mode,
	struct clock_event_device *evt)
{
	unsigned long ctrl = TIMER_CTRL_32BIT | TIMER_CTRL_IE;

	writel(ctrl, clkevt_base + TIMER_CTRL);

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		writel(clkevt_reload, clkevt_base + TIMER_LOAD);
		ctrl |= TIMER_CTRL_PERIODIC | TIMER_CTRL_ENABLE;
		break;

	case CLOCK_EVT_MODE_ONESHOT:
		/* period set, and timer enabled in 'next_event' hook */
		ctrl |= TIMER_CTRL_ONESHOT;
		break;

	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
	default:
		break;
	}

	writel(ctrl, clkevt_base + TIMER_CTRL);
}

static int sp804_set_next_event(unsigned long next,
	struct clock_event_device *evt)
{
	unsigned long ctrl = readl(clkevt_base + TIMER_CTRL);

	writel(next, clkevt_base + TIMER_LOAD);
	writel(ctrl | TIMER_CTRL_ENABLE, clkevt_base + TIMER_CTRL);

	return 0;
}

static struct clock_event_device sp804_clockevent = {
	.features       = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.set_mode	= sp804_set_mode,
	.set_next_event	= sp804_set_next_event,
	.rating		= 300,
};

static struct irqaction sp804_timer_irq = {
	.name		= "timer",
	.flags		= IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= sp804_timer_interrupt,
	.dev_id		= &sp804_clockevent,
};

void __init __sp804_clockevents_init(void __iomem *base, unsigned int irq, struct clk *clk, const char *name)
{
	struct clock_event_device *evt = &sp804_clockevent;
	long rate;

	if (!clk)
		clk = clk_get_sys("sp804", name);
	if (IS_ERR(clk)) {
		pr_err("sp804: %s clock not found: %d\n", name,
			(int)PTR_ERR(clk));
		return;
	}

	rate = sp804_get_clock_rate(clk);
	if (rate < 0)
		return;

	clkevt_base = base;
	clkevt_reload = DIV_ROUND_CLOSEST(rate, HZ);
	evt->name = name;
	evt->irq = irq;
	evt->cpumask = cpu_possible_mask;

	writel(0, base + TIMER_CTRL);

	setup_irq(irq, &sp804_timer_irq);
	clockevents_config_and_register(evt, rate, 0xf, 0xffffffff);
}

static void __init sp804_of_init(struct device_node *np)
{
	static bool initialized = false;
	void __iomem *base;
	int irq;
	u32 irq_num = 0;
	struct clk *clk1, *clk2;
	const char *name = of_get_property(np, "compatible", NULL);

	base = of_iomap(np, 0);
	if (WARN_ON(!base))
		return;

	/* Ensure timers are disabled */
	writel(0, base + TIMER_CTRL);
	writel(0, base + TIMER_2_BASE + TIMER_CTRL);

	if (initialized || !of_device_is_available(np))
		goto err;

	clk1 = of_clk_get(np, 0);
	if (IS_ERR(clk1))
		clk1 = NULL;

	/* Get the 2nd clock if the timer has 2 timer clocks */
	if (of_count_phandle_with_args(np, "clocks", "#clock-cells") == 3) {
		clk2 = of_clk_get(np, 1);
		if (IS_ERR(clk2)) {
			pr_err("sp804: %s clock not found: %d\n", np->name,
				(int)PTR_ERR(clk2));
			goto err;
		}
	} else
		clk2 = clk1;

	irq = irq_of_parse_and_map(np, 0);
	if (irq <= 0)
		goto err;

	of_property_read_u32(np, "arm,sp804-has-irq", &irq_num);
	if (irq_num == 2) {
		__sp804_clockevents_init(base + TIMER_2_BASE, irq, clk2, name);
		__sp804_clocksource_and_sched_clock_init(base, name, clk1, 1);
	} else {
		__sp804_clockevents_init(base, irq, clk1 , name);
		__sp804_clocksource_and_sched_clock_init(base + TIMER_2_BASE,
							 name, clk2, 1);
	}
	initialized = true;

	return;
err:
	iounmap(base);
}
CLOCKSOURCE_OF_DECLARE(sp804, "arm,sp804", sp804_of_init);

static void __init integrator_cp_of_init(struct device_node *np)
{
	static int init_count = 0;
	void __iomem *base;
	int irq;
	const char *name = of_get_property(np, "compatible", NULL);

	base = of_iomap(np, 0);
	if (WARN_ON(!base))
		return;

	/* Ensure timer is disabled */
	writel(0, base + TIMER_CTRL);

	if (init_count == 2 || !of_device_is_available(np))
		goto err;

	if (!init_count)
		sp804_clocksource_init(base, name);
	else {
		irq = irq_of_parse_and_map(np, 0);
		if (irq <= 0)
			goto err;

		sp804_clockevents_init(base, irq, name);
	}

	init_count++;
	return;
err:
	iounmap(base);
}
CLOCKSOURCE_OF_DECLARE(intcp, "arm,integrator-cp-timer", integrator_cp_of_init);
