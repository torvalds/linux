/*
 * linux/arch/arm/mach-mmp/time.c
 *
 *   Support for clocksource and clockevents
 *
 * Copyright (C) 2008 Marvell International Ltd.
 * All rights reserved.
 *
 *   2008-04-11: Jason Chagas <Jason.chagas@marvell.com>
 *   2008-10-08: Bin Yang <bin.yang@marvell.com>
 *
 * The timers module actually includes three timers, each timer with up to
 * three match comparators. Timer #0 is used here in free-running mode as
 * the clock source, and match comparator #1 used as clock event device.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/clockchips.h>

#include <linux/io.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include <asm/sched_clock.h>
#include <mach/addr-map.h>
#include <mach/regs-timers.h>
#include <mach/regs-apbc.h>
#include <mach/irqs.h>
#include <mach/cputype.h>
#include <asm/mach/time.h>

#include "clock.h"

#define TIMERS_VIRT_BASE	TIMERS1_VIRT_BASE

#define MAX_DELTA		(0xfffffffe)
#define MIN_DELTA		(16)

static void __iomem *mmp_timer_base = TIMERS_VIRT_BASE;

/*
 * FIXME: the timer needs some delay to stablize the counter capture
 */
static inline uint32_t timer_read(void)
{
	int delay = 100;

	__raw_writel(1, mmp_timer_base + TMR_CVWR(1));

	while (delay--)
		cpu_relax();

	return __raw_readl(mmp_timer_base + TMR_CVWR(1));
}

static u32 notrace mmp_read_sched_clock(void)
{
	return timer_read();
}

static irqreturn_t timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *c = dev_id;

	/*
	 * Clear pending interrupt status.
	 */
	__raw_writel(0x01, mmp_timer_base + TMR_ICR(0));

	/*
	 * Disable timer 0.
	 */
	__raw_writel(0x02, mmp_timer_base + TMR_CER);

	c->event_handler(c);

	return IRQ_HANDLED;
}

static int timer_set_next_event(unsigned long delta,
				struct clock_event_device *dev)
{
	unsigned long flags;

	local_irq_save(flags);

	/*
	 * Disable timer 0.
	 */
	__raw_writel(0x02, mmp_timer_base + TMR_CER);

	/*
	 * Clear and enable timer match 0 interrupt.
	 */
	__raw_writel(0x01, mmp_timer_base + TMR_ICR(0));
	__raw_writel(0x01, mmp_timer_base + TMR_IER(0));

	/*
	 * Setup new clockevent timer value.
	 */
	__raw_writel(delta - 1, mmp_timer_base + TMR_TN_MM(0, 0));

	/*
	 * Enable timer 0.
	 */
	__raw_writel(0x03, mmp_timer_base + TMR_CER);

	local_irq_restore(flags);

	return 0;
}

static void timer_set_mode(enum clock_event_mode mode,
			   struct clock_event_device *dev)
{
	unsigned long flags;

	local_irq_save(flags);
	switch (mode) {
	case CLOCK_EVT_MODE_ONESHOT:
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		/* disable the matching interrupt */
		__raw_writel(0x00, mmp_timer_base + TMR_IER(0));
		break;
	case CLOCK_EVT_MODE_RESUME:
	case CLOCK_EVT_MODE_PERIODIC:
		break;
	}
	local_irq_restore(flags);
}

static struct clock_event_device ckevt = {
	.name		= "clockevent",
	.features	= CLOCK_EVT_FEAT_ONESHOT,
	.rating		= 200,
	.set_next_event	= timer_set_next_event,
	.set_mode	= timer_set_mode,
};

static cycle_t clksrc_read(struct clocksource *cs)
{
	return timer_read();
}

static struct clocksource cksrc = {
	.name		= "clocksource",
	.rating		= 200,
	.read		= clksrc_read,
	.mask		= CLOCKSOURCE_MASK(32),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static void __init timer_config(void)
{
	uint32_t ccr = __raw_readl(mmp_timer_base + TMR_CCR);

	__raw_writel(0x0, mmp_timer_base + TMR_CER); /* disable */

	ccr &= (cpu_is_mmp2()) ? (TMR_CCR_CS_0(0) | TMR_CCR_CS_1(0)) :
		(TMR_CCR_CS_0(3) | TMR_CCR_CS_1(3));
	__raw_writel(ccr, mmp_timer_base + TMR_CCR);

	/* set timer 0 to periodic mode, and timer 1 to free-running mode */
	__raw_writel(0x2, mmp_timer_base + TMR_CMR);

	__raw_writel(0x1, mmp_timer_base + TMR_PLCR(0)); /* periodic */
	__raw_writel(0x7, mmp_timer_base + TMR_ICR(0));  /* clear status */
	__raw_writel(0x0, mmp_timer_base + TMR_IER(0));

	__raw_writel(0x0, mmp_timer_base + TMR_PLCR(1)); /* free-running */
	__raw_writel(0x7, mmp_timer_base + TMR_ICR(1));  /* clear status */
	__raw_writel(0x0, mmp_timer_base + TMR_IER(1));

	/* enable timer 1 counter */
	__raw_writel(0x2, mmp_timer_base + TMR_CER);
}

static struct irqaction timer_irq = {
	.name		= "timer",
	.flags		= IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= timer_interrupt,
	.dev_id		= &ckevt,
};

void __init timer_init(int irq)
{
	timer_config();

	setup_sched_clock(mmp_read_sched_clock, 32, CLOCK_TICK_RATE);

	ckevt.cpumask = cpumask_of(0);

	setup_irq(irq, &timer_irq);

	clocksource_register_hz(&cksrc, CLOCK_TICK_RATE);
	clockevents_config_and_register(&ckevt, CLOCK_TICK_RATE,
					MIN_DELTA, MAX_DELTA);
}

#ifdef CONFIG_OF
static struct of_device_id mmp_timer_dt_ids[] = {
	{ .compatible = "mrvl,mmp-timer", },
	{}
};

void __init mmp_dt_init_timer(void)
{
	struct device_node *np;
	int irq, ret;

	np = of_find_matching_node(NULL, mmp_timer_dt_ids);
	if (!np) {
		ret = -ENODEV;
		goto out;
	}

	irq = irq_of_parse_and_map(np, 0);
	if (!irq) {
		ret = -EINVAL;
		goto out;
	}
	mmp_timer_base = of_iomap(np, 0);
	if (!mmp_timer_base) {
		ret = -ENOMEM;
		goto out;
	}
	timer_init(irq);
	return;
out:
	pr_err("Failed to get timer from device tree with error:%d\n", ret);
}
#endif
