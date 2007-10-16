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
#include "os.h"
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

	if(setitimer(ITIMER_VIRTUAL, &time, &time) < 0)
		printk(UM_KERN_ERR "disable_timer - setitimer failed, "
		       "errno = %d\n", errno);

	return timeval_to_ns(&time.it_value);
}

long long os_nsecs(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return timeval_to_ns(&tv);
}

extern void alarm_handler(int sig, struct sigcontext *sc);

void idle_sleep(unsigned long long nsecs)
{
	struct timespec ts = { .tv_sec	= nsecs / UM_NSEC_PER_SEC,
			       .tv_nsec = nsecs % UM_NSEC_PER_SEC };

	if (nanosleep(&ts, &ts) == 0)
		alarm_handler(SIGVTALRM, NULL);
}
