/*
 * OpenRISC time.c
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * Modifications for the OpenRISC architecture:
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/interrupt.h>
#include <linux/ftrace.h>

#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/irq.h>
#include <linux/io.h>

#include <asm/cpuinfo.h>

static int openrisc_timer_set_next_event(unsigned long delta,
					 struct clock_event_device *dev)
{
	u32 c;

	/* Read 32-bit counter value, add delta, mask off the low 28 bits.
	 * We're guaranteed delta won't be bigger than 28 bits because the
	 * generic timekeeping code ensures that for us.
	 */
	c = mfspr(SPR_TTCR);
	c += delta;
	c &= SPR_TTMR_TP;

	/* Set counter and enable interrupt.
	 * Keep timer in continuous mode always.
	 */
	mtspr(SPR_TTMR, SPR_TTMR_CR | SPR_TTMR_IE | c);

	return 0;
}

static void openrisc_timer_set_mode(enum clock_event_mode mode,
				    struct clock_event_device *evt)
{
	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		pr_debug(KERN_INFO "%s: periodic\n", __func__);
		BUG();
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		pr_debug(KERN_INFO "%s: oneshot\n", __func__);
		break;
	case CLOCK_EVT_MODE_UNUSED:
		pr_debug(KERN_INFO "%s: unused\n", __func__);
		break;
	case CLOCK_EVT_MODE_SHUTDOWN:
		pr_debug(KERN_INFO "%s: shutdown\n", __func__);
		break;
	case CLOCK_EVT_MODE_RESUME:
		pr_debug(KERN_INFO "%s: resume\n", __func__);
		break;
	}
}

/* This is the clock event device based on the OR1K tick timer.
 * As the timer is being used as a continuous clock-source (required for HR
 * timers) we cannot enable the PERIODIC feature.  The tick timer can run using
 * one-shot events, so no problem.
 */

static struct clock_event_device clockevent_openrisc_timer = {
	.name = "openrisc_timer_clockevent",
	.features = CLOCK_EVT_FEAT_ONESHOT,
	.rating = 300,
	.set_next_event = openrisc_timer_set_next_event,
	.set_mode = openrisc_timer_set_mode,
};

static inline void timer_ack(void)
{
	/* Clear the IP bit and disable further interrupts */
	/* This can be done very simply... we just need to keep the timer
	   running, so just maintain the CR bits while clearing the rest
	   of the register
	 */
	mtspr(SPR_TTMR, SPR_TTMR_CR);
}

/*
 * The timer interrupt is mostly handled in generic code nowadays... this
 * function just acknowledges the interrupt and fires the event handler that
 * has been set on the clockevent device by the generic time management code.
 *
 * This function needs to be called by the timer exception handler and that's
 * all the exception handler needs to do.
 */

irqreturn_t __irq_entry timer_interrupt(struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);
	struct clock_event_device *evt = &clockevent_openrisc_timer;

	timer_ack();

	/*
	 * update_process_times() expects us to have called irq_enter().
	 */
	irq_enter();
	evt->event_handler(evt);
	irq_exit();

	set_irq_regs(old_regs);

	return IRQ_HANDLED;
}

static __init void openrisc_clockevent_init(void)
{
	clockevents_calc_mult_shift(&clockevent_openrisc_timer,
				    cpuinfo.clock_frequency, 4);

	/* We only have 28 bits */
	clockevent_openrisc_timer.max_delta_ns =
	    clockevent_delta2ns((u32) 0x0fffffff, &clockevent_openrisc_timer);
	clockevent_openrisc_timer.min_delta_ns =
	    clockevent_delta2ns(1, &clockevent_openrisc_timer);
	clockevent_openrisc_timer.cpumask = cpumask_of(0);
	clockevents_register_device(&clockevent_openrisc_timer);
}

/**
 * Clocksource: Based on OpenRISC timer/counter
 *
 * This sets up the OpenRISC Tick Timer as a clock source.  The tick timer
 * is 32 bits wide and runs at the CPU clock frequency.
 */

static cycle_t openrisc_timer_read(struct clocksource *cs)
{
	return (cycle_t) mfspr(SPR_TTCR);
}

static struct clocksource openrisc_timer = {
	.name = "openrisc_timer",
	.rating = 200,
	.read = openrisc_timer_read,
	.mask = CLOCKSOURCE_MASK(32),
	.flags = CLOCK_SOURCE_IS_CONTINUOUS,
};

static int __init openrisc_timer_init(void)
{
	if (clocksource_register_hz(&openrisc_timer, cpuinfo.clock_frequency))
		panic("failed to register clocksource");

	/* Enable the incrementer: 'continuous' mode with interrupt disabled */
	mtspr(SPR_TTMR, SPR_TTMR_CR);

	return 0;
}

void __init time_init(void)
{
	u32 upr;

	upr = mfspr(SPR_UPR);
	if (!(upr & SPR_UPR_TTP))
		panic("Linux not supported on devices without tick timer");

	openrisc_timer_init();
	openrisc_clockevent_init();
}
