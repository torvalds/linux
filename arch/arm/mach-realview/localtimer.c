/*
 *  linux/arch/arm/mach-realview/localtimer.c
 *
 *  Copyright (C) 2002 ARM Ltd.
 *  All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/smp.h>
#include <linux/jiffies.h>
#include <linux/percpu.h>
#include <linux/clockchips.h>
#include <linux/irq.h>
#include <linux/io.h>

#include <asm/hardware/arm_twd.h>
#include <asm/hardware/gic.h>
#include <mach/hardware.h>
#include <asm/irq.h>

static DEFINE_PER_CPU(struct clock_event_device, local_clockevent);

/*
 * Used on SMP for either the local timer or IPI_TIMER
 */
void local_timer_interrupt(void)
{
	struct clock_event_device *clk = &__get_cpu_var(local_clockevent);

	clk->event_handler(clk);
}

#ifdef CONFIG_LOCAL_TIMERS

/* set up by the platform code */
void __iomem *twd_base;

static unsigned long mpcore_timer_rate;

static void local_timer_set_mode(enum clock_event_mode mode,
				 struct clock_event_device *clk)
{
	unsigned long ctrl;

	switch(mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		/* timer load already set up */
		ctrl = TWD_TIMER_CONTROL_ENABLE | TWD_TIMER_CONTROL_IT_ENABLE
			| TWD_TIMER_CONTROL_PERIODIC;
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		/* period set, and timer enabled in 'next_event' hook */
		ctrl = TWD_TIMER_CONTROL_IT_ENABLE | TWD_TIMER_CONTROL_ONESHOT;
		break;
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
	default:
		ctrl = 0;
	}

	__raw_writel(ctrl, twd_base + TWD_TIMER_CONTROL);
}

static int local_timer_set_next_event(unsigned long evt,
				      struct clock_event_device *unused)
{
	unsigned long ctrl = __raw_readl(twd_base + TWD_TIMER_CONTROL);

	__raw_writel(evt, twd_base + TWD_TIMER_COUNTER);
	__raw_writel(ctrl | TWD_TIMER_CONTROL_ENABLE, twd_base + TWD_TIMER_CONTROL);

	return 0;
}

/*
 * local_timer_ack: checks for a local timer interrupt.
 *
 * If a local timer interrupt has occurred, acknowledge and return 1.
 * Otherwise, return 0.
 */
int local_timer_ack(void)
{
	if (__raw_readl(twd_base + TWD_TIMER_INTSTAT)) {
		__raw_writel(1, twd_base + TWD_TIMER_INTSTAT);
		return 1;
	}

	return 0;
}

static void __cpuinit twd_calibrate_rate(void)
{
	unsigned long load, count;
	u64 waitjiffies;

	/*
	 * If this is the first time round, we need to work out how fast
	 * the timer ticks
	 */
	if (mpcore_timer_rate == 0) {
		printk("Calibrating local timer... ");

		/* Wait for a tick to start */
		waitjiffies = get_jiffies_64() + 1;

		while (get_jiffies_64() < waitjiffies)
			udelay(10);

		/* OK, now the tick has started, let's get the timer going */
		waitjiffies += 5;

				 /* enable, no interrupt or reload */
		__raw_writel(0x1, twd_base + TWD_TIMER_CONTROL);

				 /* maximum value */
		__raw_writel(0xFFFFFFFFU, twd_base + TWD_TIMER_COUNTER);

		while (get_jiffies_64() < waitjiffies)
			udelay(10);

		count = __raw_readl(twd_base + TWD_TIMER_COUNTER);

		mpcore_timer_rate = (0xFFFFFFFFU - count) * (HZ / 5);

		printk("%lu.%02luMHz.\n", mpcore_timer_rate / 1000000,
			(mpcore_timer_rate / 100000) % 100);
	}

	load = mpcore_timer_rate / HZ;

	__raw_writel(load, twd_base + TWD_TIMER_LOAD);
}

/*
 * Setup the local clock events for a CPU.
 */
void __cpuinit local_timer_setup(void)
{
	unsigned int cpu = smp_processor_id();
	struct clock_event_device *clk = &per_cpu(local_clockevent, cpu);
	unsigned long flags;

	twd_calibrate_rate();

	clk->name		= "local_timer";
	clk->features		= CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT;
	clk->rating		= 350;
	clk->set_mode		= local_timer_set_mode;
	clk->set_next_event	= local_timer_set_next_event;
	clk->irq		= IRQ_LOCALTIMER;
	clk->cpumask		= cpumask_of(cpu);
	clk->shift		= 20;
	clk->mult		= div_sc(mpcore_timer_rate, NSEC_PER_SEC, clk->shift);
	clk->max_delta_ns	= clockevent_delta2ns(0xffffffff, clk);
	clk->min_delta_ns	= clockevent_delta2ns(0xf, clk);

	/* Make sure our local interrupt controller has this enabled */
	local_irq_save(flags);
	get_irq_chip(IRQ_LOCALTIMER)->unmask(IRQ_LOCALTIMER);
	local_irq_restore(flags);

	clockevents_register_device(clk);
}

/*
 * take a local timer down
 */
void __cpuexit local_timer_stop(void)
{
	__raw_writel(0, twd_base + TWD_TIMER_CONTROL);
}

#else	/* CONFIG_LOCAL_TIMERS */

static void dummy_timer_set_mode(enum clock_event_mode mode,
				 struct clock_event_device *clk)
{
}

void __cpuinit local_timer_setup(void)
{
	unsigned int cpu = smp_processor_id();
	struct clock_event_device *clk = &per_cpu(local_clockevent, cpu);

	clk->name		= "dummy_timer";
	clk->features		= CLOCK_EVT_FEAT_DUMMY;
	clk->rating		= 200;
	clk->set_mode		= dummy_timer_set_mode;
	clk->broadcast		= smp_timer_broadcast;
	clk->cpumask		= cpumask_of(cpu);

	clockevents_register_device(clk);
}

#endif	/* !CONFIG_LOCAL_TIMERS */
