/*
 * Copyright (C) 2004-2007 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/cpu.h>

#include <asm/sysreg.h>

#include <mach/pm.h>

static bool disable_cpu_idle_poll;

static cycle_t read_cycle_count(struct clocksource *cs)
{
	return (cycle_t)sysreg_read(COUNT);
}

/*
 * The architectural cycle count registers are a fine clocksource unless
 * the system idle loop use sleep states like "idle":  the CPU cycles
 * measured by COUNT (and COMPARE) don't happen during sleep states.
 * Their duration also changes if cpufreq changes the CPU clock rate.
 * So we rate the clocksource using COUNT as very low quality.
 */
static struct clocksource counter = {
	.name		= "avr32_counter",
	.rating		= 50,
	.read		= read_cycle_count,
	.mask		= CLOCKSOURCE_MASK(32),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static irqreturn_t timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evdev = dev_id;

	if (unlikely(!(intc_get_pending(0) & 1)))
		return IRQ_NONE;

	/*
	 * Disable the interrupt until the clockevent subsystem
	 * reprograms it.
	 */
	sysreg_write(COMPARE, 0);

	evdev->event_handler(evdev);
	return IRQ_HANDLED;
}

static struct irqaction timer_irqaction = {
	.handler	= timer_interrupt,
	/* Oprofile uses the same irq as the timer, so allow it to be shared */
	.flags		= IRQF_TIMER | IRQF_SHARED,
	.name		= "avr32_comparator",
};

static int comparator_next_event(unsigned long delta,
		struct clock_event_device *evdev)
{
	unsigned long	flags;

	raw_local_irq_save(flags);

	/* The time to read COUNT then update COMPARE must be less
	 * than the min_delta_ns value for this clockevent source.
	 */
	sysreg_write(COMPARE, (sysreg_read(COUNT) + delta) ? : 1);

	raw_local_irq_restore(flags);

	return 0;
}

static int comparator_shutdown(struct clock_event_device *evdev)
{
	pr_debug("%s: %s\n", __func__, evdev->name);
	sysreg_write(COMPARE, 0);

	if (disable_cpu_idle_poll) {
		disable_cpu_idle_poll = false;
		/*
		 * Only disable idle poll if we have forced that
		 * in a previous call.
		 */
		cpu_idle_poll_ctrl(false);
	}
	return 0;
}

static int comparator_set_oneshot(struct clock_event_device *evdev)
{
	pr_debug("%s: %s\n", __func__, evdev->name);

	disable_cpu_idle_poll = true;
	/*
	 * If we're using the COUNT and COMPARE registers we
	 * need to force idle poll.
	 */
	cpu_idle_poll_ctrl(true);

	return 0;
}

static struct clock_event_device comparator = {
	.name			= "avr32_comparator",
	.features		= CLOCK_EVT_FEAT_ONESHOT,
	.shift			= 16,
	.rating			= 50,
	.set_next_event		= comparator_next_event,
	.set_state_shutdown	= comparator_shutdown,
	.set_state_oneshot	= comparator_set_oneshot,
	.tick_resume		= comparator_set_oneshot,
};

void read_persistent_clock(struct timespec *ts)
{
	ts->tv_sec = mktime(2007, 1, 1, 0, 0, 0);
	ts->tv_nsec = 0;
}

void __init time_init(void)
{
	unsigned long counter_hz;
	int ret;

	/* figure rate for counter */
	counter_hz = clk_get_rate(boot_cpu_data.clk);
	ret = clocksource_register_hz(&counter, counter_hz);
	if (ret)
		pr_debug("timer: could not register clocksource: %d\n", ret);

	/* setup COMPARE clockevent */
	comparator.mult = div_sc(counter_hz, NSEC_PER_SEC, comparator.shift);
	comparator.max_delta_ns = clockevent_delta2ns((u32)~0, &comparator);
	comparator.min_delta_ns = clockevent_delta2ns(50, &comparator) + 1;
	comparator.cpumask = cpumask_of(0);

	sysreg_write(COMPARE, 0);
	timer_irqaction.dev_id = &comparator;

	ret = setup_irq(0, &timer_irqaction);
	if (ret)
		pr_debug("timer: could not request IRQ 0: %d\n", ret);
	else {
		clockevents_register_device(&comparator);

		pr_info("%s: irq 0, %lu.%03lu MHz\n", comparator.name,
				((counter_hz + 500) / 1000) / 1000,
				((counter_hz + 500) / 1000) % 1000);
	}
}
