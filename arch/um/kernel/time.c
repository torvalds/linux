// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015 Anton Ivanov (aivanov@{brocade.com,kot-begemot.co.uk})
 * Copyright (C) 2015 Thomas Meyer (thomas@m3y3r.de)
 * Copyright (C) 2012-2014 Cisco Systems
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Copyright (C) 2019 Intel Corporation
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
#include <linux/time-internal.h>
#include <shared/init.h>

#ifdef CONFIG_UML_TIME_TRAVEL_SUPPORT
enum time_travel_mode time_travel_mode;

static bool time_travel_start_set;
static unsigned long long time_travel_start;
static unsigned long long time_travel_time;
static LIST_HEAD(time_travel_events);
static unsigned long long time_travel_timer_interval;
static unsigned long long time_travel_next_event;
static struct time_travel_event time_travel_timer_event;

static void time_travel_set_time(unsigned long long ns)
{
	if (unlikely(ns < time_travel_time))
		panic("time-travel: time goes backwards %lld -> %lld\n",
		      time_travel_time, ns);
	time_travel_time = ns;
}

static struct time_travel_event *time_travel_first_event(void)
{
	return list_first_entry_or_null(&time_travel_events,
					struct time_travel_event,
					list);
}

static void __time_travel_add_event(struct time_travel_event *e,
				    unsigned long long time)
{
	struct time_travel_event *tmp;
	bool inserted = false;

	if (WARN(time_travel_mode == TT_MODE_BASIC &&
		 e != &time_travel_timer_event,
		 "only timer events can be handled in basic mode"))
		return;

	if (e->pending)
		return;

	e->pending = true;
	e->time = time;

	list_for_each_entry(tmp, &time_travel_events, list) {
		/*
		 * Add the new entry before one with higher time,
		 * or if they're equal and both on stack, because
		 * in that case we need to unwind the stack in the
		 * right order, and the later event (timer sleep
		 * or such) must be dequeued first.
		 */
		if ((tmp->time > e->time) ||
		    (tmp->time == e->time && tmp->onstack && e->onstack)) {
			list_add_tail(&e->list, &tmp->list);
			inserted = true;
			break;
		}
	}

	if (!inserted)
		list_add_tail(&e->list, &time_travel_events);

	tmp = time_travel_first_event();
	time_travel_next_event = tmp->time;
}

static void time_travel_add_event(struct time_travel_event *e,
				  unsigned long long time)
{
	if (WARN_ON(!e->fn))
		return;

	__time_travel_add_event(e, time);
}

void time_travel_periodic_timer(struct time_travel_event *e)
{
	time_travel_add_event(&time_travel_timer_event,
			      time_travel_time + time_travel_timer_interval);
	deliver_alarm();
}

static void time_travel_deliver_event(struct time_travel_event *e)
{
	/* this is basically just deliver_alarm(), handles IRQs itself */
	e->fn(e);
}

static bool time_travel_del_event(struct time_travel_event *e)
{
	if (!e->pending)
		return false;
	list_del(&e->list);
	e->pending = false;
	return true;
}

static void time_travel_update_time(unsigned long long next, bool retearly)
{
	struct time_travel_event ne = {
		.onstack = true,
	};
	struct time_travel_event *e;
	bool finished = retearly;

	/* add it without a handler - we deal with that specifically below */
	__time_travel_add_event(&ne, next);

	do {
		e = time_travel_first_event();

		BUG_ON(!e);
		time_travel_set_time(e->time);

		/* new events may have been inserted while we were waiting */
		if (e == time_travel_first_event()) {
			BUG_ON(!time_travel_del_event(e));
			BUG_ON(time_travel_time != e->time);

			if (e == &ne) {
				finished = true;
			} else {
				if (e->onstack)
					panic("On-stack event dequeued outside of the stack! time=%lld, event time=%lld, event=%pS\n",
					      time_travel_time, e->time, e);
				time_travel_deliver_event(e);
			}
		}
	} while (!finished);

	time_travel_del_event(&ne);
}

static void time_travel_oneshot_timer(struct time_travel_event *e)
{
	deliver_alarm();
}

void time_travel_sleep(unsigned long long duration)
{
	unsigned long long next = time_travel_time + duration;

	if (time_travel_mode == TT_MODE_BASIC)
		os_timer_disable();

	time_travel_update_time(next, true);

	if (time_travel_mode == TT_MODE_BASIC &&
	    time_travel_timer_event.pending) {
		if (time_travel_timer_event.fn == time_travel_periodic_timer) {
			/*
			 * This is somewhat wrong - we should get the first
			 * one sooner like the os_timer_one_shot() below...
			 */
			os_timer_set_interval(time_travel_timer_interval);
		} else {
			os_timer_one_shot(time_travel_timer_event.time - next);
		}
	}
}

static void time_travel_handle_real_alarm(void)
{
	time_travel_set_time(time_travel_next_event);

	time_travel_del_event(&time_travel_timer_event);

	if (time_travel_timer_event.fn == time_travel_periodic_timer)
		time_travel_add_event(&time_travel_timer_event,
				      time_travel_time +
				      time_travel_timer_interval);
}

static void time_travel_set_interval(unsigned long long interval)
{
	time_travel_timer_interval = interval;
}
#else /* CONFIG_UML_TIME_TRAVEL_SUPPORT */
#define time_travel_start_set 0
#define time_travel_start 0
#define time_travel_time 0

static inline void time_travel_update_time(unsigned long long ns, bool retearly)
{
}

static inline void time_travel_handle_real_alarm(void)
{
}

static void time_travel_set_interval(unsigned long long interval)
{
}

/* these are empty macros so the struct/fn need not exist */
#define time_travel_add_event(e, time) do { } while (0)
#define time_travel_del_event(e) do { } while (0)
#endif

void timer_handler(int sig, struct siginfo *unused_si, struct uml_pt_regs *regs)
{
	unsigned long flags;

	/*
	 * In basic time-travel mode we still get real interrupts
	 * (signals) but since we don't read time from the OS, we
	 * must update the simulated time here to the expiry when
	 * we get a signal.
	 * This is not the case in inf-cpu mode, since there we
	 * never get any real signals from the OS.
	 */
	if (time_travel_mode == TT_MODE_BASIC)
		time_travel_handle_real_alarm();

	local_irq_save(flags);
	do_IRQ(TIMER_IRQ, regs);
	local_irq_restore(flags);
}

static int itimer_shutdown(struct clock_event_device *evt)
{
	if (time_travel_mode != TT_MODE_OFF)
		time_travel_del_event(&time_travel_timer_event);

	if (time_travel_mode != TT_MODE_INFCPU)
		os_timer_disable();

	return 0;
}

static int itimer_set_periodic(struct clock_event_device *evt)
{
	unsigned long long interval = NSEC_PER_SEC / HZ;

	if (time_travel_mode != TT_MODE_OFF) {
		time_travel_del_event(&time_travel_timer_event);
		time_travel_set_event_fn(&time_travel_timer_event,
					 time_travel_periodic_timer);
		time_travel_set_interval(interval);
		time_travel_add_event(&time_travel_timer_event,
				      time_travel_time + interval);
	}

	if (time_travel_mode != TT_MODE_INFCPU)
		os_timer_set_interval(interval);

	return 0;
}

static int itimer_next_event(unsigned long delta,
			     struct clock_event_device *evt)
{
	delta += 1;

	if (time_travel_mode != TT_MODE_OFF) {
		time_travel_del_event(&time_travel_timer_event);
		time_travel_set_event_fn(&time_travel_timer_event,
					 time_travel_oneshot_timer);
		time_travel_add_event(&time_travel_timer_event,
				      time_travel_time + delta);
	}

	if (time_travel_mode != TT_MODE_INFCPU)
		return os_timer_one_shot(delta);

	return 0;
}

static int itimer_one_shot(struct clock_event_device *evt)
{
	return itimer_next_event(0, evt);
}

static struct clock_event_device timer_clockevent = {
	.name			= "posix-timer",
	.rating			= 250,
	.cpumask		= cpu_possible_mask,
	.features		= CLOCK_EVT_FEAT_PERIODIC |
				  CLOCK_EVT_FEAT_ONESHOT,
	.set_state_shutdown	= itimer_shutdown,
	.set_state_periodic	= itimer_set_periodic,
	.set_state_oneshot	= itimer_one_shot,
	.set_next_event		= itimer_next_event,
	.shift			= 0,
	.max_delta_ns		= 0xffffffff,
	.max_delta_ticks	= 0xffffffff,
	.min_delta_ns		= TIMER_MIN_DELTA,
	.min_delta_ticks	= TIMER_MIN_DELTA, // microsecond resolution should be enough for anyone, same as 640K RAM
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
	if (time_travel_mode != TT_MODE_OFF) {
		/*
		 * We make reading the timer cost a bit so that we don't get
		 * stuck in loops that expect time to move more than the
		 * exact requested sleep amount, e.g. python's socket server,
		 * see https://bugs.python.org/issue37026.
		 */
		if (!irqs_disabled())
			time_travel_update_time(time_travel_time +
						TIMER_MULTIPLIER,
						false);
		return time_travel_time / TIMER_MULTIPLIER;
	}

	return os_nsecs() / TIMER_MULTIPLIER;
}

static struct clocksource timer_clocksource = {
	.name		= "timer",
	.rating		= 300,
	.read		= timer_read,
	.mask		= CLOCKSOURCE_MASK(64),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static void __init um_timer_setup(void)
{
	int err;

	err = request_irq(TIMER_IRQ, um_timer, IRQF_TIMER, "hr timer", NULL);
	if (err != 0)
		printk(KERN_ERR "register_timer : request_irq failed - "
		       "errno = %d\n", -err);

	err = os_timer_create();
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

void read_persistent_clock64(struct timespec64 *ts)
{
	long long nsecs;

	if (time_travel_start_set)
		nsecs = time_travel_start + time_travel_time;
	else
		nsecs = os_persistent_clock_emulation();

	set_normalized_timespec64(ts, nsecs / NSEC_PER_SEC,
				  nsecs % NSEC_PER_SEC);
}

void __init time_init(void)
{
	timer_set_signal_handler();
	late_time_init = um_timer_setup;
}

#ifdef CONFIG_UML_TIME_TRAVEL_SUPPORT
unsigned long calibrate_delay_is_known(void)
{
	if (time_travel_mode == TT_MODE_INFCPU)
		return 1;
	return 0;
}

int setup_time_travel(char *str)
{
	if (strcmp(str, "=inf-cpu") == 0) {
		time_travel_mode = TT_MODE_INFCPU;
		timer_clockevent.name = "time-travel-timer-infcpu";
		timer_clocksource.name = "time-travel-clock";
		return 1;
	}

	if (!*str) {
		time_travel_mode = TT_MODE_BASIC;
		timer_clockevent.name = "time-travel-timer";
		timer_clocksource.name = "time-travel-clock";
		return 1;
	}

	return -EINVAL;
}

__setup("time-travel", setup_time_travel);
__uml_help(setup_time_travel,
"time-travel\n"
"This option just enables basic time travel mode, in which the clock/timers\n"
"inside the UML instance skip forward when there's nothing to do, rather than\n"
"waiting for real time to elapse. However, instance CPU speed is limited by\n"
"the real CPU speed, so e.g. a 10ms timer will always fire after ~10ms wall\n"
"clock (but quicker when there's nothing to do).\n"
"\n"
"time-travel=inf-cpu\n"
"This enables time travel mode with infinite processing power, in which there\n"
"are no wall clock timers, and any CPU processing happens - as seen from the\n"
"guest - instantly. This can be useful for accurate simulation regardless of\n"
"debug overhead, physical CPU speed, etc. but is somewhat dangerous as it can\n"
"easily lead to getting stuck (e.g. if anything in the system busy loops).\n");

int setup_time_travel_start(char *str)
{
	int err;

	err = kstrtoull(str, 0, &time_travel_start);
	if (err)
		return err;

	time_travel_start_set = 1;
	return 1;
}

__setup("time-travel-start", setup_time_travel_start);
__uml_help(setup_time_travel_start,
"time-travel-start=<seconds>\n"
"Configure the UML instance's wall clock to start at this value rather than\n"
"the host's wall clock at the time of UML boot.\n");
#endif
