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
#include "kern_util.h"
#include "user.h"
#include "process.h"
#include "kern_constants.h"
#include "os.h"
#include "uml-config.h"

int set_interval(int is_virtual)
{
	int usec = 1000000/hz();
	int timer_type = is_virtual ? ITIMER_VIRTUAL : ITIMER_REAL;
	struct itimerval interval = ((struct itimerval) { { 0, usec },
							  { 0, usec } });

	if(setitimer(timer_type, &interval, NULL) == -1)
		return -errno;

	return 0;
}

#ifdef UML_CONFIG_MODE_TT
void enable_timer(void)
{
	set_interval(1);
}
#endif

void disable_timer(void)
{
	struct itimerval disable = ((struct itimerval) { { 0, 0 }, { 0, 0 }});
	if((setitimer(ITIMER_VIRTUAL, &disable, NULL) < 0) ||
	   (setitimer(ITIMER_REAL, &disable, NULL) < 0))
		printk("disnable_timer - setitimer failed, errno = %d\n",
		       errno);
	/* If there are signals already queued, after unblocking ignore them */
	signal(SIGALRM, SIG_IGN);
	signal(SIGVTALRM, SIG_IGN);
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

#ifdef UML_CONFIG_MODE_TT
void uml_idle_timer(void)
{
	if(signal(SIGVTALRM, SIG_IGN) == SIG_ERR)
		panic("Couldn't unset SIGVTALRM handler");

	set_handler(SIGALRM, (__sighandler_t) alarm_handler,
		    SA_RESTART, SIGUSR1, SIGIO, SIGWINCH, SIGVTALRM, -1);
	set_interval(0);
}
#endif

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
