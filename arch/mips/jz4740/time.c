/*
 *  Copyright (C) 2010, Lars-Peter Clausen <lars@metafoo.de>
 *  JZ4740 platform time support
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/time.h>

#include <linux/clockchips.h>

#include <asm/mach-jz4740/irq.h>
#include <asm/time.h>

#include "clock.h"
#include "timer.h"

#define TIMER_CLOCKEVENT 0
#define TIMER_CLOCKSOURCE 1

static uint16_t jz4740_jiffies_per_tick;

static cycle_t jz4740_clocksource_read(struct clocksource *cs)
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

static irqreturn_t jz4740_clockevent_irq(int irq, void *devid)
{
	struct clock_event_device *cd = devid;

	jz4740_timer_ack_full(TIMER_CLOCKEVENT);

	if (cd->mode != CLOCK_EVT_MODE_PERIODIC)
		jz4740_timer_disable(TIMER_CLOCKEVENT);

	cd->event_handler(cd);

	return IRQ_HANDLED;
}

static void jz4740_clockevent_set_mode(enum clock_event_mode mode,
	struct clock_event_device *cd)
{
	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		jz4740_timer_set_count(TIMER_CLOCKEVENT, 0);
		jz4740_timer_set_period(TIMER_CLOCKEVENT, jz4740_jiffies_per_tick);
	case CLOCK_EVT_MODE_RESUME:
		jz4740_timer_irq_full_enable(TIMER_CLOCKEVENT);
		jz4740_timer_enable(TIMER_CLOCKEVENT);
		break;
	case CLOCK_EVT_MODE_ONESHOT:
	case CLOCK_EVT_MODE_SHUTDOWN:
		jz4740_timer_disable(TIMER_CLOCKEVENT);
		break;
	default:
		break;
	}
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
	.set_mode = jz4740_clockevent_set_mode,
	.rating = 200,
	.irq = JZ4740_IRQ_TCU0,
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

	jz4740_timer_init();

	clk_rate = jz4740_clock_bdata.ext_rate >> 4;
	jz4740_jiffies_per_tick = DIV_ROUND_CLOSEST(clk_rate, HZ);

	clockevent_set_clock(&jz4740_clockevent, clk_rate);
	jz4740_clockevent.min_delta_ns = clockevent_delta2ns(100, &jz4740_clockevent);
	jz4740_clockevent.max_delta_ns = clockevent_delta2ns(0xffff, &jz4740_clockevent);
	jz4740_clockevent.cpumask = cpumask_of(0);

	clockevents_register_device(&jz4740_clockevent);

	clocksource_set_clock(&jz4740_clocksource, clk_rate);
	ret = clocksource_register(&jz4740_clocksource);

	if (ret)
		printk(KERN_ERR "Failed to register clocksource: %d\n", ret);

	setup_irq(JZ4740_IRQ_TCU0, &timer_irqaction);

	ctrl = JZ_TIMER_CTRL_PRESCALE_16 | JZ_TIMER_CTRL_SRC_EXT;

	jz4740_timer_set_ctrl(TIMER_CLOCKEVENT, ctrl);
	jz4740_timer_set_ctrl(TIMER_CLOCKSOURCE, ctrl);

	jz4740_timer_set_period(TIMER_CLOCKEVENT, jz4740_jiffies_per_tick);
	jz4740_timer_irq_full_enable(TIMER_CLOCKEVENT);

	jz4740_timer_set_period(TIMER_CLOCKSOURCE, 0xffff);

	jz4740_timer_enable(TIMER_CLOCKEVENT);
	jz4740_timer_enable(TIMER_CLOCKSOURCE);
}
