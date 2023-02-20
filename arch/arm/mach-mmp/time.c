// SPDX-License-Identifier: GPL-2.0-only
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
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/clockchips.h>
#include <linux/clk.h>

#include <linux/io.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/sched_clock.h>
#include <asm/mach/time.h>

#include "regs-timers.h"
#include <linux/soc/mmp/cputype.h>

#define MAX_DELTA		(0xfffffffe)
#define MIN_DELTA		(16)

static void __iomem *mmp_timer_base;

/*
 * Read the timer through the CVWR register. Delay is required after requesting
 * a read. The CR register cannot be directly read due to metastability issues
 * documented in the PXA168 software manual.
 */
static inline uint32_t timer_read(void)
{
	uint32_t val;
	int delay = 3;

	__raw_writel(1, mmp_timer_base + TMR_CVWR(1));

	while (delay--)
		val = __raw_readl(mmp_timer_base + TMR_CVWR(1));

	return val;
}

static u64 notrace mmp_read_sched_clock(void)
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

static int timer_set_shutdown(struct clock_event_device *evt)
{
	unsigned long flags;

	local_irq_save(flags);
	/* disable the matching interrupt */
	__raw_writel(0x00, mmp_timer_base + TMR_IER(0));
	local_irq_restore(flags);

	return 0;
}

static struct clock_event_device ckevt = {
	.name			= "clockevent",
	.features		= CLOCK_EVT_FEAT_ONESHOT,
	.rating			= 200,
	.set_next_event		= timer_set_next_event,
	.set_state_shutdown	= timer_set_shutdown,
	.set_state_oneshot	= timer_set_shutdown,
};

static u64 clksrc_read(struct clocksource *cs)
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

	ccr &= (cpu_is_mmp2() || cpu_is_mmp3()) ?
		(TMR_CCR_CS_0(0) | TMR_CCR_CS_1(0)) :
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

static void __init mmp_timer_init(int irq, unsigned long rate)
{
	timer_config();

	sched_clock_register(mmp_read_sched_clock, 32, rate);

	ckevt.cpumask = cpumask_of(0);

	if (request_irq(irq, timer_interrupt, IRQF_TIMER | IRQF_IRQPOLL,
			"timer", &ckevt))
		pr_err("Failed to request irq %d (timer)\n", irq);

	clocksource_register_hz(&cksrc, rate);
	clockevents_config_and_register(&ckevt, rate, MIN_DELTA, MAX_DELTA);
}

static int __init mmp_dt_init_timer(struct device_node *np)
{
	struct clk *clk;
	int irq, ret;
	unsigned long rate;

	clk = of_clk_get(np, 0);
	if (!IS_ERR(clk)) {
		ret = clk_prepare_enable(clk);
		if (ret)
			return ret;
		rate = clk_get_rate(clk);
	} else if (cpu_is_pj4()) {
		rate = 6500000;
	} else {
		rate = 3250000;
	}

	irq = irq_of_parse_and_map(np, 0);
	if (!irq)
		return -EINVAL;

	mmp_timer_base = of_iomap(np, 0);
	if (!mmp_timer_base)
		return -ENOMEM;

	mmp_timer_init(irq, rate);
	return 0;
}

TIMER_OF_DECLARE(mmp_timer, "mrvl,mmp-timer", mmp_dt_init_timer);
