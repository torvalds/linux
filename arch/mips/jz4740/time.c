// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2010, Lars-Peter Clausen <lars@metafoo.de>
 *  JZ4740 platform time support
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/time.h>

#include <linux/clockchips.h>
#include <linux/sched_clock.h>

#include <asm/mach-jz4740/irq.h>
#include <asm/mach-jz4740/timer.h>
#include <asm/time.h>

#define TIMER_CLOCKEVENT 0
#define TIMER_CLOCKSOURCE 1

static uint16_t jz4740_jiffies_per_tick;

static u64 jz4740_clocksource_read(struct clocksource *cs)
{
	return jz4740_timer_get_count(TIMER_CLOCKSOURCE);
}

static struct clocksource jz4740_clocksource = {
	.name = "jz4740-timer",
	.rating = 200,
	.read = jz4740_clocksource_read,
	.mask = CLOCKSOURCE_MASK(16),
	.flags = CLOCK_SOURCE_IS_CONTINUOUS,
};

static u64 notrace jz4740_read_sched_clock(void)
{
	return jz4740_timer_get_count(TIMER_CLOCKSOURCE);
}

static irqreturn_t jz4740_clockevent_irq(int irq, void *devid)
{
	struct clock_event_device *cd = devid;

	jz4740_timer_ack_full(TIMER_CLOCKEVENT);

	if (!clockevent_state_periodic(cd))
		jz4740_timer_disable(TIMER_CLOCKEVENT);

	cd->event_handler(cd);

	return IRQ_HANDLED;
}

static int jz4740_clockevent_set_periodic(struct clock_event_device *evt)
{
	jz4740_timer_set_count(TIMER_CLOCKEVENT, 0);
	jz4740_timer_set_period(TIMER_CLOCKEVENT, jz4740_jiffies_per_tick);
	jz4740_timer_irq_full_enable(TIMER_CLOCKEVENT);
	jz4740_timer_enable(TIMER_CLOCKEVENT);

	return 0;
}

static int jz4740_clockevent_resume(struct clock_event_device *evt)
{
	jz4740_timer_irq_full_enable(TIMER_CLOCKEVENT);
	jz4740_timer_enable(TIMER_CLOCKEVENT);

	return 0;
}

static int jz4740_clockevent_shutdown(struct clock_event_device *evt)
{
	jz4740_timer_disable(TIMER_CLOCKEVENT);

	return 0;
}

static int jz4740_clockevent_set_next(unsigned long evt,
	struct clock_event_device *cd)
{
	jz4740_timer_set_count(TIMER_CLOCKEVENT, 0);
	jz4740_timer_set_period(TIMER_CLOCKEVENT, evt);
	jz4740_timer_enable(TIMER_CLOCKEVENT);

	return 0;
}

static struct clock_event_device jz4740_clockevent = {
	.name = "jz4740-timer",
	.features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.set_next_event = jz4740_clockevent_set_next,
	.set_state_shutdown = jz4740_clockevent_shutdown,
	.set_state_periodic = jz4740_clockevent_set_periodic,
	.set_state_oneshot = jz4740_clockevent_shutdown,
	.tick_resume = jz4740_clockevent_resume,
	.rating = 200,
#ifdef CONFIG_MACH_JZ4740
	.irq = JZ4740_IRQ_TCU0,
#endif
#if defined(CONFIG_MACH_JZ4770) || defined(CONFIG_MACH_JZ4780)
	.irq = JZ4780_IRQ_TCU2,
#endif
};

static struct irqaction timer_irqaction = {
	.handler	= jz4740_clockevent_irq,
	.flags		= IRQF_PERCPU | IRQF_TIMER,
	.name		= "jz4740-timerirq",
	.dev_id		= &jz4740_clockevent,
};

void __init plat_time_init(void)
{
	int ret;
	uint32_t clk_rate;
	uint16_t ctrl;
	struct clk *ext_clk;

	of_clk_init(NULL);
	jz4740_timer_init();

	ext_clk = clk_get(NULL, "ext");
	if (IS_ERR(ext_clk))
		panic("unable to get ext clock");
	clk_rate = clk_get_rate(ext_clk) >> 4;
	clk_put(ext_clk);

	jz4740_jiffies_per_tick = DIV_ROUND_CLOSEST(clk_rate, HZ);

	clockevent_set_clock(&jz4740_clockevent, clk_rate);
	jz4740_clockevent.min_delta_ns = clockevent_delta2ns(100, &jz4740_clockevent);
	jz4740_clockevent.min_delta_ticks = 100;
	jz4740_clockevent.max_delta_ns = clockevent_delta2ns(0xffff, &jz4740_clockevent);
	jz4740_clockevent.max_delta_ticks = 0xffff;
	jz4740_clockevent.cpumask = cpumask_of(0);

	clockevents_register_device(&jz4740_clockevent);

	ret = clocksource_register_hz(&jz4740_clocksource, clk_rate);

	if (ret)
		printk(KERN_ERR "Failed to register clocksource: %d\n", ret);

	sched_clock_register(jz4740_read_sched_clock, 16, clk_rate);

	setup_irq(jz4740_clockevent.irq, &timer_irqaction);

	ctrl = JZ_TIMER_CTRL_PRESCALE_16 | JZ_TIMER_CTRL_SRC_EXT;

	jz4740_timer_set_ctrl(TIMER_CLOCKEVENT, ctrl);
	jz4740_timer_set_ctrl(TIMER_CLOCKSOURCE, ctrl);

	jz4740_timer_set_period(TIMER_CLOCKEVENT, jz4740_jiffies_per_tick);
	jz4740_timer_irq_full_enable(TIMER_CLOCKEVENT);

	jz4740_timer_set_period(TIMER_CLOCKSOURCE, 0xffff);

	jz4740_timer_enable(TIMER_CLOCKEVENT);
	jz4740_timer_enable(TIMER_CLOCKSOURCE);
}
