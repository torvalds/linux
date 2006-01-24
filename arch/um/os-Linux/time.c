/*
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include "user_util.h"
#include "kern_util.h"
#include "user.h"
#include "process.h"
#include "kern_constants.h"
#include "os.h"

/* XXX This really needs to be declared and initialized in a kernel file since
 * it's in <linux/time.h>
 */
extern struct timespec wall_to_monotonic;

static void set_interval(int timer_type)
{
	int usec = 1000000/hz();
	struct itimerval interval = ((struct itimerval) { { 0, usec },
							  { 0, usec } });

	if(setitimer(timer_type, &interval, NULL) == -1)
		panic("setitimer failed - errno = %d\n", errno);
}

void enable_timer(void)
{
	set_interval(ITIMER_VIRTUAL);
}

void disable_timer(void)
{
	struct itimerval disable = ((struct itimerval) { { 0, 0 }, { 0, 0 }});
	if((setitimer(ITIMER_VIRTUAL, &disable, NULL) < 0) ||
	   (setitimer(ITIMER_REAL, &disable, NULL) < 0))
		printk("disnable_timer - setitimer failed, errno = %d\n",
		       errno);
	/* If there are signals already queued, after unblocking ignore them */
	set_handler(SIGALRM, SIG_IGN, 0, -1);
	set_handler(SIGVTALRM, SIG_IGN, 0, -1);
}

void switch_timers(int to_real)
{
	struct itimerval disable = ((struct itimerval) { { 0, 0 }, { 0, 0 }});
	struct itimerval enable = ((struct itimerval) { { 0, 1000000/hz() },
							{ 0, 1000000/hz() }});
	int old, new;

	if(to_real){
		old = ITIMER_VIRTUAL;
		new = ITIMER_REAL;
	}
	else {
		old = ITIMER_REAL;
		new = ITIMER_VIRTUAL;
	}

	if((setitimer(old, &disable, NULL) < 0) ||
	   (setitimer(new, &enable, NULL)))
		printk("switch_timers - setitimer failed, errno = %d\n",
		       errno);
}

void uml_idle_timer(void)
{
	if(signal(SIGVTALRM, SIG_IGN) == SIG_ERR)
		panic("Couldn't unset SIGVTALRM handler");

	set_handler(SIGALRM, (__sighandler_t) alarm_handler,
		    SA_RESTART, SIGUSR1, SIGIO, SIGWINCH, SIGVTALRM, -1);
	set_interval(ITIMER_REAL);
}

extern void ktime_get_ts(struct timespec *ts);
#define do_posix_clock_monotonic_gettime(ts) ktime_get_ts(ts)

void time_init(void)
{
	struct timespec now;

	if(signal(SIGVTALRM, boot_timer_handler) == SIG_ERR)
		panic("Couldn't set SIGVTALRM handler");
	set_interval(ITIMER_VIRTUAL);

	do_posix_clock_monotonic_gettime(&now);
	wall_to_monotonic.tv_sec = -now.tv_sec;
	wall_to_monotonic.tv_nsec = -now.tv_nsec;
}

unsigned long long os_nsecs(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return((unsigned long long) tv.tv_sec * BILLION + tv.tv_usec * 1000);
}

void idle_sleep(int secs)
{
	struct timespec ts;

	ts.tv_sec = secs;
	ts.tv_nsec = 0;
	nanosleep(&ts, NULL);
}

/* XXX This partly duplicates init_irq_signals */

void user_time_init(void)
{
	set_handler(SIGVTALRM, (__sighandler_t) alarm_handler,
		    SA_ONSTACK | SA_RESTART, SIGUSR1, SIGIO, SIGWINCH,
		    SIGALRM, SIGUSR2, -1);
	set_handler(SIGALRM, (__sighandler_t) alarm_handler,
		    SA_ONSTACK | SA_RESTART, SIGUSR1, SIGIO, SIGWINCH,
		    SIGVTALRM, SIGUSR2, -1);
	set_interval(ITIMER_VIRTUAL);
}
