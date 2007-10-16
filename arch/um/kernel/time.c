/*
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include "linux/clockchips.h"
#include "linux/interrupt.h"
#include "linux/jiffies.h"
#include "linux/threads.h"
#include "asm/irq.h"
#include "asm/param.h"
#include "kern_util.h"
#include "os.h"

/*
 * Scheduler clock - returns current time in nanosec units.
 */
unsigned long long sched_clock(void)
{
	return (unsigned long long)jiffies_64 * (NSEC_PER_SEC / HZ);
}

void timer_handler(int sig, struct uml_pt_regs *regs)
{
	unsigned long flags;

	local_irq_save(flags);
	do_IRQ(TIMER_IRQ, regs);
	local_irq_restore(flags);
}

static void itimer_set_mode(enum clock_event_mode mode,
			    struct clock_event_device *evt)
{
	switch(mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		set_interval();
		break;

	case CLOCK_EVT_MODE_SHUTDOWN:
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_ONESHOT:
		disable_timer();
		break;

	case CLOCK_EVT_MODE_RESUME:
		break;
	}
}

static int itimer_next_event(unsigned long delta,
			     struct clock_event_device *evt)
{
	return timer_one_shot(delta + 1);
}

static struct clock_event_device itimer_clockevent = {
	.name		= "itimer",
	.rating		= 250,
	.cpumask	= CPU_MASK_ALL,
	.features	= CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.set_mode	= itimer_set_mode,
	.set_next_event = itimer_next_event,
	.shift		= 32,
	.irq		= 0,
};

static irqreturn_t um_timer(int irq, void *dev)
{
	(*itimer_clockevent.event_handler)(&itimer_clockevent);

	return IRQ_HANDLED;
}

static cycle_t itimer_read(void)
{
	return os_nsecs();
}

static struct clocksource itimer_clocksource = {
	.name		= "itimer",
	.rating		= 300,
	.read		= itimer_read,
	.mask		= CLOCKSOURCE_MASK(64),
	.mult		= 1,
	.shift		= 0,
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static void __init setup_itimer(void)
{
	int err;

	err = request_irq(TIMER_IRQ, um_timer, IRQF_DISABLED, "timer", NULL);
	if (err != 0)
		printk(KERN_ERR "register_timer : request_irq failed - "
		       "errno = %d\n", -err);

	itimer_clockevent.mult = div_sc(HZ, NSEC_PER_SEC, 32);
	itimer_clockevent.max_delta_ns =
		clockevent_delta2ns(60 * HZ, &itimer_clockevent);
	itimer_clockevent.min_delta_ns =
		clockevent_delta2ns(1, &itimer_clockevent);
	err = clocksource_register(&itimer_clocksource);
	if (err) {
		printk(KERN_ERR "clocksource_register returned %d\n", err);
		return;
	}
	clockevents_register_device(&itimer_clockevent);
}

extern void (*late_time_init)(void);

void __init time_init(void)
{
	long long nsecs;

	timer_init();

	nsecs = os_nsecs();
	set_normalized_timespec(&wall_to_monotonic, -nsecs / NSEC_PER_SEC,
				-nsecs % NSEC_PER_SEC);
	set_normalized_timespec(&xtime, nsecs / NSEC_PER_SEC,
				nsecs % NSEC_PER_SEC);
	late_time_init = setup_itimer;
}
