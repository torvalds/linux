/*
 * Copyright (C) 2015 Anton Ivanov (aivanov@{brocade.com,kot-begemot.co.uk})
 * Copyright (C) 2015 Thomas Meyer (thomas@m3y3r.de)
 * Copyright (C) 2012-2014 Cisco Systems
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include <linux/clockchips.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/threads.h>
#include <asm/irq.h>
#include <asm/param.h>
#include <kern_util.h>
#include <os.h>
#include <timer-internal.h>

void timer_handler(int sig, struct siginfo *unused_si, struct uml_pt_regs *regs)
{
	unsigned long flags;

	local_irq_save(flags);
	do_IRQ(TIMER_IRQ, regs);
	local_irq_restore(flags);
}

static int itimer_shutdown(struct clock_event_device *evt)
{
	os_timer_disable();
	return 0;
}

static int itimer_set_periodic(struct clock_event_device *evt)
{
	os_timer_set_interval(NULL, NULL);
	return 0;
}

static int itimer_next_event(unsigned long delta,
			     struct clock_event_device *evt)
{
	return os_timer_one_shot(delta);
}

static int itimer_one_shot(struct clock_event_device *evt)
{
	os_timer_one_shot(1);
	return 0;
}

static struct clock_event_device timer_clockevent = {
	.name			= "posix-timer",
	.rating			= 250,
	.cpumask		= cpu_all_mask,
	.features		= CLOCK_EVT_FEAT_PERIODIC |
				  CLOCK_EVT_FEAT_ONESHOT,
	.set_state_shutdown	= itimer_shutdown,
	.set_state_periodic	= itimer_set_periodic,
	.set_state_oneshot	= itimer_one_shot,
	.set_next_event		= itimer_next_event,
	.shift			= 0,
	.max_delta_ns		= 0xffffffff,
	.min_delta_ns		= TIMER_MIN_DELTA, //microsecond resolution should be enough for anyone, same as 640K RAM
	.irq			= 0,
	.mult			= 1,
};

static irqreturn_t um_timer(int irq, void *dev)
{
	if (get_current()->mm != NULL)
	{
        /* userspace - relay signal, results in correct userspace timers */
		os_alarm_process(get_current()->mm->context.id.u.pid);
	}

	(*timer_clockevent.event_handler)(&timer_clockevent);

	return IRQ_HANDLED;
}

static u64 timer_read(struct clocksource *cs)
{
	return os_nsecs() / TIMER_MULTIPLIER;
}

static struct clocksource timer_clocksource = {
	.name		= "timer",
	.rating		= 300,
	.read		= timer_read,
	.mask		= CLOCKSOURCE_MASK(64),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static void __init timer_setup(void)
{
	int err;

	err = request_irq(TIMER_IRQ, um_timer, IRQF_TIMER, "hr timer", NULL);
	if (err != 0)
		printk(KERN_ERR "register_timer : request_irq failed - "
		       "errno = %d\n", -err);

	err = os_timer_create(NULL);
	if (err != 0) {
		printk(KERN_ERR "creation of timer failed - errno = %d\n", -err);
		return;
	}

	err = clocksource_register_hz(&timer_clocksource, NSEC_PER_SEC/TIMER_MULTIPLIER);
	if (err) {
		printk(KERN_ERR "clocksource_register_hz returned %d\n", err);
		return;
	}
	clockevents_register_device(&timer_clockevent);
}

void read_persistent_clock(struct timespec *ts)
{
	long long nsecs = os_persistent_clock_emulation();

	set_normalized_timespec(ts, nsecs / NSEC_PER_SEC,
				nsecs % NSEC_PER_SEC);
}

void __init time_init(void)
{
	timer_set_signal_handler();
	late_time_init = timer_setup;
}
