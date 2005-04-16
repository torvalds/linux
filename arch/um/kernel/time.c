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
#include "signal_user.h"
#include "time_user.h"
#include "kern_constants.h"

/* XXX This really needs to be declared and initialized in a kernel file since
 * it's in <linux/time.h>
 */
extern struct timespec wall_to_monotonic;

extern struct timeval xtime;

struct timeval local_offset = { 0, 0 };

void timer(void)
{
	gettimeofday(&xtime, NULL);
	timeradd(&xtime, &local_offset, &xtime);
}

void set_interval(int timer_type)
{
	int usec = 1000000/hz();
	struct itimerval interval = ((struct itimerval) { { 0, usec },
							  { 0, usec } });

	if(setitimer(timer_type, &interval, NULL) == -1)
		panic("setitimer failed - errno = %d\n", errno);
}

void enable_timer(void)
{
	int usec = 1000000/hz();
	struct itimerval enable = ((struct itimerval) { { 0, usec },
							{ 0, usec }});
	if(setitimer(ITIMER_VIRTUAL, &enable, NULL))
		printk("enable_timer - setitimer failed, errno = %d\n",
		       errno);
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

extern int do_posix_clock_monotonic_gettime(struct timespec *tp);

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

/* Declared in linux/time.h, which can't be included here */
extern void clock_was_set(void);

void do_gettimeofday(struct timeval *tv)
{
	unsigned long flags;

	flags = time_lock();
	gettimeofday(tv, NULL);
	timeradd(tv, &local_offset, tv);
	time_unlock(flags);
	clock_was_set();
}

int do_settimeofday(struct timespec *tv)
{
	struct timeval now;
	unsigned long flags;
	struct timeval tv_in;

	if ((unsigned long) tv->tv_nsec >= UM_NSEC_PER_SEC)
		return -EINVAL;

	tv_in.tv_sec = tv->tv_sec;
	tv_in.tv_usec = tv->tv_nsec / 1000;

	flags = time_lock();
	gettimeofday(&now, NULL);
	timersub(&tv_in, &now, &local_offset);
	time_unlock(flags);

	return(0);
}

void idle_sleep(int secs)
{
	struct timespec ts;

	ts.tv_sec = secs;
	ts.tv_nsec = 0;
	nanosleep(&ts, NULL);
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
