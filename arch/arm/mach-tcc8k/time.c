/*
 * TCC8000 system timer setup
 *
 * (C) 2009 Hans J. Koch <hjk@linutronix.de>
 *
 * Licensed under the terms of the GPL version 2.
 *
 */

#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>

#include <asm/mach/time.h>

#include <mach/tcc8k-regs.h>
#include <mach/irqs.h>

#include "common.h"

static void __iomem *timer_base;

static cycle_t tcc_get_cycles(struct clocksource *cs)
{
	return __raw_readl(timer_base + TC32MCNT_OFFS);
}

static struct clocksource clocksource_tcc = {
	.name		= "tcc_tc32",
	.rating		= 200,
	.read		= tcc_get_cycles,
	.mask		= CLOCKSOURCE_MASK(32),
	.shift		= 28,
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static int tcc_set_next_event(unsigned long evt,
			      struct clock_event_device *unused)
{
	unsigned long reg = __raw_readl(timer_base + TC32MCNT_OFFS);

	__raw_writel(reg + evt, timer_base + TC32CMP0_OFFS);
	return 0;
}

static void tcc_set_mode(enum clock_event_mode mode,
				struct clock_event_device *evt)
{
	unsigned long tc32irq;

	switch (mode) {
	case CLOCK_EVT_MODE_ONESHOT:
		tc32irq = __raw_readl(timer_base + TC32IRQ_OFFS);
		tc32irq |= TC32IRQ_IRQEN0;
		__raw_writel(tc32irq, timer_base + TC32IRQ_OFFS);
		break;
	case CLOCK_EVT_MODE_SHUTDOWN:
	case CLOCK_EVT_MODE_UNUSED:
		tc32irq = __raw_readl(timer_base + TC32IRQ_OFFS);
		tc32irq &= ~TC32IRQ_IRQEN0;
		__raw_writel(tc32irq, timer_base + TC32IRQ_OFFS);
		break;
	case CLOCK_EVT_MODE_PERIODIC:
	case CLOCK_EVT_MODE_RESUME:
		break;
	}
}

static irqreturn_t tcc8k_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	/* Acknowledge TC32 interrupt by reading TC32IRQ */
	__raw_readl(timer_base + TC32IRQ_OFFS);

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static struct clock_event_device clockevent_tcc = {
	.name		= "tcc_timer1",
	.features	= CLOCK_EVT_FEAT_ONESHOT,
	.shift		= 32,
	.set_mode	= tcc_set_mode,
	.set_next_event	= tcc_set_next_event,
	.rating		= 200,
};

static struct irqaction tcc8k_timer_irq = {
	.name		= "TC32_timer",
	.flags		= IRQF_DISABLED | IRQF_TIMER,
	.handler	= tcc8k_timer_interrupt,
	.dev_id		= &clockevent_tcc,
};

static int __init tcc_clockevent_init(struct clk *clock)
{
	unsigned int c = clk_get_rate(clock);

	clocksource_tcc.mult = clocksource_hz2mult(c,
					clocksource_tcc.shift);
	clocksource_register(&clocksource_tcc);

	clockevent_tcc.mult = div_sc(c, NSEC_PER_SEC,
					clockevent_tcc.shift);
	clockevent_tcc.max_delta_ns =
			clockevent_delta2ns(0xfffffffe, &clockevent_tcc);
	clockevent_tcc.min_delta_ns =
			clockevent_delta2ns(0xff, &clockevent_tcc);

	clockevent_tcc.cpumask = cpumask_of(0);

	clockevents_register_device(&clockevent_tcc);

	return 0;
}

void __init tcc8k_timer_init(struct clk *clock, void __iomem *base, int irq)
{
	u32 reg;

	timer_base = base;
	tcc8k_timer_irq.irq = irq;

	/* Enable clocks */
	clk_enable(clock);

	/* Initialize 32-bit timer */
	reg = __raw_readl(timer_base + TC32EN_OFFS);
	reg &= ~TC32EN_ENABLE; /* Disable timer */
	__raw_writel(reg, timer_base + TC32EN_OFFS);
	/* Free running timer, counting from 0 to 0xffffffff */
	__raw_writel(0, timer_base + TC32EN_OFFS);
	__raw_writel(0, timer_base + TC32LDV_OFFS);
	reg = __raw_readl(timer_base + TC32IRQ_OFFS);
	reg |= TC32IRQ_IRQEN0; /* irq at match with CMP0 */
	__raw_writel(reg, timer_base + TC32IRQ_OFFS);

	__raw_writel(TC32EN_ENABLE, timer_base + TC32EN_OFFS);

	tcc_clockevent_init(clock);
	setup_irq(irq, &tcc8k_timer_irq);
}
