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
	return (unsigned long long)jiffies_64 * (1000000000 / HZ);
}

#ifdef CONFIG_UML_REAL_TIME_CLOCK
static unsigned long long prev_nsecs[NR_CPUS];
static long long delta[NR_CPUS];		/* Deviation per interval */
#endif

void timer_handler(int sig, struct uml_pt_regs *regs)
{
	unsigned long long ticks = 0;
	unsigned long flags;
#ifdef CONFIG_UML_REAL_TIME_CLOCK
	int c = cpu();
	if (prev_nsecs[c]) {
		/* We've had 1 tick */
		unsigned long long nsecs = os_nsecs();

		delta[c] += nsecs - prev_nsecs[c];
		prev_nsecs[c] = nsecs;

		/* Protect against the host clock being set backwards */
		if (delta[c] < 0)
			delta[c] = 0;

		ticks += (delta[c] * HZ) / BILLION;
		delta[c] -= (ticks * BILLION) / HZ;
	}
	else prev_nsecs[c] = os_nsecs();
#else
	ticks = 1;
#endif

	local_irq_save(flags);
	while (ticks > 0) {
		do_IRQ(TIMER_IRQ, regs);
		ticks--;
	}
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
		disable_timer();
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		BUG();
		break;

	case CLOCK_EVT_MODE_RESUME:
		break;
	}
}

static struct clock_event_device itimer_clockevent = {
	.name		= "itimer",
	.rating		= 250,
	.cpumask	= CPU_MASK_ALL,
	.features	= CLOCK_EVT_FEAT_PERIODIC,
	.set_mode	= itimer_set_mode,
	.set_next_event = NULL,
	.shift		= 32,
	.irq		= 0,
};

static irqreturn_t um_timer(int irq, void *dev)
{
	(*itimer_clockevent.event_handler)(&itimer_clockevent);

	return IRQ_HANDLED;
}

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
	clockevents_register_device(&itimer_clockevent);
}

extern void (*late_time_init)(void);

void __init time_init(void)
{
	long long nsecs;

	timer_init();

	nsecs = os_nsecs();
	set_normalized_timespec(&wall_to_monotonic, -nsecs / BILLION,
				-nsecs % BILLION);
	set_normalized_timespec(&xtime, nsecs / BILLION, nsecs % BILLION);
	late_time_init = setup_itimer;
}
