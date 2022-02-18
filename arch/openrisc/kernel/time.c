// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * OpenRISC time.c
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * Modifications for the OpenRISC architecture:
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
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
#include <linux/of_clk.h>

#include <asm/cpuinfo.h>

/* Test the timer ticks to count, used in sync routine */
inline void openrisc_timer_set(unsigned long count)
{
	mtspr(SPR_TTCR, count);
}

/* Set the timer to trigger in delta cycles */
inline void openrisc_timer_set_next(unsigned long delta)
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
}

static int openrisc_timer_set_next_event(unsigned long delta,
					 struct clock_event_device *dev)
{
	openrisc_timer_set_next(delta);
	return 0;
}

/* This is the clock event device based on the OR1K tick timer.
 * As the timer is being used as a continuous clock-source (required for HR
 * timers) we cannot enable the PERIODIC feature.  The tick timer can run using
 * one-shot events, so no problem.
 */
DEFINE_PER_CPU(struct clock_event_device, clockevent_openrisc_timer);

void openrisc_clockevent_init(void)
{
	unsigned int cpu = smp_processor_id();
	struct clock_event_device *evt =
		&per_cpu(clockevent_openrisc_timer, cpu);
	struct cpuinfo_or1k *cpuinfo = &cpuinfo_or1k[cpu];

	mtspr(SPR_TTMR, SPR_TTMR_CR);

#ifdef CONFIG_SMP
	evt->broadcast = tick_broadcast;
#endif
	evt->name = "openrisc_timer_clockevent",
	evt->features = CLOCK_EVT_FEAT_ONESHOT,
	evt->rating = 300,
	evt->set_next_event = openrisc_timer_set_next_event,

	evt->cpumask = cpumask_of(cpu);

	/* We only have 28 bits */
	clockevents_config_and_register(evt, cpuinfo->clock_frequency,
					100, 0x0fffffff);

}

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
	unsigned int cpu = smp_processor_id();
	struct clock_event_device *evt =
		&per_cpu(clockevent_openrisc_timer, cpu);

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

/*
 * Clocksource: Based on OpenRISC timer/counter
 *
 * This sets up the OpenRISC Tick Timer as a clock source.  The tick timer
 * is 32 bits wide and runs at the CPU clock frequency.
 */
static u64 openrisc_timer_read(struct clocksource *cs)
{
	return (u64) mfspr(SPR_TTCR);
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
	struct cpuinfo_or1k *cpuinfo = &cpuinfo_or1k[smp_processor_id()];

	if (clocksource_register_hz(&openrisc_timer, cpuinfo->clock_frequency))
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

	of_clk_init(NULL);
	timer_probe();
}
