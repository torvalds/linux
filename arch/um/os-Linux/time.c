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

static int is_real_timer = 0;

int set_interval(void)
{
	int usec = 1000000/UM_HZ;
	struct itimerval interval = ((struct itimerval) { { 0, usec },
							  { 0, usec } });

	if (setitimer(ITIMER_VIRTUAL, &interval, NULL) == -1)
		return -errno;

	return 0;
}

int timer_one_shot(int ticks)
{
	unsigned long usec = ticks * 1000000 / UM_HZ;
	unsigned long sec = usec / 1000000;
	struct itimerval interval;

	usec %= 1000000;
	interval = ((struct itimerval) { { 0, 0 }, { sec, usec } });

	if (setitimer(ITIMER_VIRTUAL, &interval, NULL) == -1)
		return -errno;

	return 0;
}

void disable_timer(void)
{
	struct itimerval disable = ((struct itimerval) { { 0, 0 }, { 0, 0 }});

	if ((setitimer(ITIMER_VIRTUAL, &disable, NULL) < 0) ||
	    (setitimer(ITIMER_REAL, &disable, NULL) < 0))
		printk(UM_KERN_ERR "disable_timer - setitimer failed, "
		       "errno = %d\n", errno);
}

int switch_timers(int to_real)
{
	struct itimerval disable = ((struct itimerval) { { 0, 0 }, { 0, 0 }});
	struct itimerval enable;
	int old, new, old_type = is_real_timer;

	if(to_real == old_type)
		return to_real;

	if (to_real) {
		old = ITIMER_VIRTUAL;
		new = ITIMER_REAL;
	}
	else {
		old = ITIMER_REAL;
		new = ITIMER_VIRTUAL;
	}

	if (setitimer(old, &disable, &enable) < 0)
		printk(UM_KERN_ERR "switch_timers - setitimer disable failed, "
		       "errno = %d\n", errno);

	if((enable.it_value.tv_sec == 0) && (enable.it_value.tv_usec == 0))
		enable.it_value = enable.it_interval;

	if (setitimer(new, &enable, NULL))
		printk(UM_KERN_ERR "switch_timers - setitimer enable failed, "
		       "errno = %d\n", errno);

	is_real_timer = to_real;
	return old_type;
}

unsigned long long os_nsecs(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return timeval_to_ns(&tv);
}

void idle_sleep(int secs)
{
	struct timespec ts;

	ts.tv_sec = secs;
	ts.tv_nsec = 0;
	nanosleep(&ts, NULL);
}
