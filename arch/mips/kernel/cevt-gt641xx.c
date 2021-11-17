// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  GT641xx clockevent routines.
 *
 *  Copyright (C) 2007	Yoichi Yuasa <yuasa@linux-mips.org>
 */
#include <linux/clockchips.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/irq.h>

#include <asm/gt64120.h>
#include <asm/time.h>

static DEFINE_RAW_SPINLOCK(gt641xx_timer_lock);
static unsigned int gt641xx_base_clock;

void gt641xx_set_base_clock(unsigned int clock)
{
	gt641xx_base_clock = clock;
}

int gt641xx_timer0_state(void)
{
	if (GT_READ(GT_TC0_OFS))
		return 0;

	GT_WRITE(GT_TC0_OFS, gt641xx_base_clock / HZ);
	GT_WRITE(GT_TC_CONTROL_OFS, GT_TC_CONTROL_ENTC0_MSK);

	return 1;
}

static int gt641xx_timer0_set_next_event(unsigned long delta,
					 struct clock_event_device *evt)
{
	u32 ctrl;

	raw_spin_lock(&gt641xx_timer_lock);

	ctrl = GT_READ(GT_TC_CONTROL_OFS);
	ctrl &= ~(GT_TC_CONTROL_ENTC0_MSK | GT_TC_CONTROL_SELTC0_MSK);
	ctrl |= GT_TC_CONTROL_ENTC0_MSK;

	GT_WRITE(GT_TC0_OFS, delta);
	GT_WRITE(GT_TC_CONTROL_OFS, ctrl);

	raw_spin_unlock(&gt641xx_timer_lock);

	return 0;
}

static int gt641xx_timer0_shutdown(struct clock_event_device *evt)
{
	u32 ctrl;

	raw_spin_lock(&gt641xx_timer_lock);

	ctrl = GT_READ(GT_TC_CONTROL_OFS);
	ctrl &= ~(GT_TC_CONTROL_ENTC0_MSK | GT_TC_CONTROL_SELTC0_MSK);
	GT_WRITE(GT_TC_CONTROL_OFS, ctrl);

	raw_spin_unlock(&gt641xx_timer_lock);
	return 0;
}

static int gt641xx_timer0_set_oneshot(struct clock_event_device *evt)
{
	u32 ctrl;

	raw_spin_lock(&gt641xx_timer_lock);

	ctrl = GT_READ(GT_TC_CONTROL_OFS);
	ctrl &= ~GT_TC_CONTROL_SELTC0_MSK;
	ctrl |= GT_TC_CONTROL_ENTC0_MSK;
	GT_WRITE(GT_TC_CONTROL_OFS, ctrl);

	raw_spin_unlock(&gt641xx_timer_lock);
	return 0;
}

static int gt641xx_timer0_set_periodic(struct clock_event_device *evt)
{
	u32 ctrl;

	raw_spin_lock(&gt641xx_timer_lock);

	ctrl = GT_READ(GT_TC_CONTROL_OFS);
	ctrl |= GT_TC_CONTROL_ENTC0_MSK | GT_TC_CONTROL_SELTC0_MSK;
	GT_WRITE(GT_TC_CONTROL_OFS, ctrl);

	raw_spin_unlock(&gt641xx_timer_lock);
	return 0;
}

static void gt641xx_timer0_event_handler(struct clock_event_device *dev)
{
}

static struct clock_event_device gt641xx_timer0_clockevent = {
	.name			= "gt641xx-timer0",
	.features		= CLOCK_EVT_FEAT_PERIODIC |
				  CLOCK_EVT_FEAT_ONESHOT,
	.irq			= GT641XX_TIMER0_IRQ,
	.set_next_event		= gt641xx_timer0_set_next_event,
	.set_state_shutdown	= gt641xx_timer0_shutdown,
	.set_state_periodic	= gt641xx_timer0_set_periodic,
	.set_state_oneshot	= gt641xx_timer0_set_oneshot,
	.tick_resume		= gt641xx_timer0_shutdown,
	.event_handler		= gt641xx_timer0_event_handler,
};

static irqreturn_t gt641xx_timer0_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *cd = &gt641xx_timer0_clockevent;

	cd->event_handler(cd);

	return IRQ_HANDLED;
}

static int __init gt641xx_timer0_clockevent_init(void)
{
	struct clock_event_device *cd;

	if (!gt641xx_base_clock)
		return 0;

	GT_WRITE(GT_TC0_OFS, gt641xx_base_clock / HZ);

	cd = &gt641xx_timer0_clockevent;
	cd->rating = 200 + gt641xx_base_clock / 10000000;
	clockevent_set_clock(cd, gt641xx_base_clock);
	cd->max_delta_ns = clockevent_delta2ns(0x7fffffff, cd);
	cd->max_delta_ticks = 0x7fffffff;
	cd->min_delta_ns = clockevent_delta2ns(0x300, cd);
	cd->min_delta_ticks = 0x300;
	cd->cpumask = cpumask_of(0);

	clockevents_register_device(&gt641xx_timer0_clockevent);

	return request_irq(GT641XX_TIMER0_IRQ, gt641xx_timer0_interrupt,
			   IRQF_PERCPU | IRQF_TIMER, "gt641xx_timer0", NULL);
}
arch_initcall(gt641xx_timer0_clockevent_init);
