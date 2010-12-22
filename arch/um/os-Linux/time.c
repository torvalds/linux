/*
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include <stddef.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include "kern_constants.h"
#include "kern_util.h"
#include "os.h"
#include "process.h"
#include "user.h"

int set_interval(void)
{
	int usec = UM_USEC_PER_SEC / UM_HZ;
	struct itimerval interval = ((struct itimerval) { { 0, usec },
							  { 0, usec } });

	if (setitimer(ITIMER_VIRTUAL, &interval, NULL) == -1)
		return -errno;

	return 0;
}

int timer_one_shot(int ticks)
{
	unsigned long usec = ticks * UM_USEC_PER_SEC / UM_HZ;
	unsigned long sec = usec / UM_USEC_PER_SEC;
	struct itimerval interval;

	usec %= UM_USEC_PER_SEC;
	interval = ((struct itimerval) { { 0, 0 }, { sec, usec } });

	if (setitimer(ITIMER_VIRTUAL, &interval, NULL) == -1)
		return -errno;

	return 0;
}

/**
 * timeval_to_ns - Convert timeval to nanoseconds
 * @ts:		pointer to the timeval variable to be converted
 *
 * Returns the scalar nanosecond representation of the timeval
 * parameter.
 *
 * Ripped from linux/time.h because it's a kernel header, and thus
 * unusable from here.
 */
static inline long long timeval_to_ns(const struct timeval *tv)
{
	return ((long long) tv->tv_sec * UM_NSEC_PER_SEC) +
		tv->tv_usec * UM_NSEC_PER_USEC;
}

long long disable_timer(void)
{
	struct itimerval time = ((struct itimerval) { { 0, 0 }, { 0, 0 } });
	long long remain, max = UM_NSEC_PER_SEC / UM_HZ;

	if (setitimer(ITIMER_VIRTUAL, &time, &time) < 0)
		printk(UM_KERN_ERR "disable_timer - setitimer failed, "
		       "errno = %d\n", errno);

	remain = timeval_to_ns(&time.it_value);
	if (remain > max)
		remain = max;

	return remain;
}

long long os_nsecs(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return timeval_to_ns(&tv);
}

#ifdef UML_CONFIG_NO_HZ
static int after_sleep_interval(struct timespec *ts)
{
	return 0;
}

static void deliver_alarm(void)
{
	alarm_handler(SIGVTALRM, NULL);
}

static unsigned long long sleep_time(unsigned long long nsecs)
{
	return nsecs;
}

#else
unsigned long long last_tick;
unsigned long long skew;

static void deliver_alarm(void)
{
	unsigned long long this_tick = os_nsecs();
	int one_tick = UM_NSEC_PER_SEC / UM_HZ;

	/* Protection against the host's time going backwards */
	if ((last_tick != 0) && (this_tick < last_tick))
		this_tick = last_tick;

	if (last_tick == 0)
		last_tick = this_tick - one_tick;

	skew += this_tick - last_tick;

	while (skew >= one_tick) {
		alarm_handler(SIGVTALRM, NULL);
		skew -= one_tick;
	}

	last_tick = this_tick;
}

static unsigned long long sleep_time(unsigned long long nsecs)
{
	return nsecs > skew ? nsecs - skew : 0;
}

static inline long long timespec_to_us(const struct timespec *ts)
{
	return ((long long) ts->tv_sec * UM_USEC_PER_SEC) +
		ts->tv_nsec / UM_NSEC_PER_USEC;
}

static int after_sleep_interval(struct timespec *ts)
{
	int usec = UM_USEC_PER_SEC / UM_HZ;
	long long start_usecs = timespec_to_us(ts);
	struct timeval tv;
	struct itimerval interval;

	/*
	 * It seems that rounding can increase the value returned from
	 * setitimer to larger than the one passed in.  Over time,
	 * this will cause the remaining time to be greater than the
	 * tick interval.  If this happens, then just reduce the first
	 * tick to the interval value.
	 */
	if (start_usecs > usec)
		start_usecs = usec;

	start_usecs -= skew / UM_NSEC_PER_USEC;
	if (start_usecs < 0)
		start_usecs = 0;

	tv = ((struct timeval) { .tv_sec  = start_usecs / UM_USEC_PER_SEC,
				 .tv_usec = start_usecs % UM_USEC_PER_SEC });
	interval = ((struct itimerval) { { 0, usec }, tv });

	if (setitimer(ITIMER_VIRTUAL, &interval, NULL) == -1)
		return -errno;

	return 0;
}
#endif

void idle_sleep(unsigned long long nsecs)
{
	struct timespec ts;

	/*
	 * nsecs can come in as zero, in which case, this starts a
	 * busy loop.  To prevent this, reset nsecs to the tick
	 * interval if it is zero.
	 */
	if (nsecs == 0)
		nsecs = UM_NSEC_PER_SEC / UM_HZ;

	nsecs = sleep_time(nsecs);
	ts = ((struct timespec) { .tv_sec	= nsecs / UM_NSEC_PER_SEC,
				  .tv_nsec	= nsecs % UM_NSEC_PER_SEC });

	if (nanosleep(&ts, &ts) == 0)
		deliver_alarm();
	after_sleep_interval(&ts);
}
