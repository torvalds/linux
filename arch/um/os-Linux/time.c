// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015 Anton Ivanov (aivanov@{brocade.com,kot-begemot.co.uk})
 * Copyright (C) 2015 Thomas Meyer (thomas@m3y3r.de)
 * Copyright (C) 2012-2014 Cisco Systems
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike{addtoit,linux.intel}.com)
 */

#include <stddef.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <kern_util.h>
#include <os.h>
#include <string.h>
#include "internal.h"

static timer_t event_high_res_timer[CONFIG_NR_CPUS] = { 0 };

static inline long long timespec_to_ns(const struct timespec *ts)
{
	return ((long long) ts->tv_sec * UM_NSEC_PER_SEC) + ts->tv_nsec;
}

long long os_persistent_clock_emulation(void)
{
	struct timespec realtime_tp;

	clock_gettime(CLOCK_REALTIME, &realtime_tp);
	return timespec_to_ns(&realtime_tp);
}

#ifndef sigev_notify_thread_id
#define sigev_notify_thread_id _sigev_un._tid
#endif

/**
 * os_timer_create() - create an new posix (interval) timer
 */
int os_timer_create(void)
{
	timer_t *t = &event_high_res_timer[0];
	struct sigevent sev = {
		.sigev_notify = SIGEV_THREAD_ID,
		.sigev_signo = SIGALRM,
		.sigev_value.sival_ptr = t,
		.sigev_notify_thread_id = gettid(),
	};

	if (timer_create(CLOCK_MONOTONIC, &sev, t) == -1)
		return -1;

	return 0;
}

int os_timer_set_interval(int cpu, unsigned long long nsecs)
{
	struct itimerspec its;

	its.it_value.tv_sec = nsecs / UM_NSEC_PER_SEC;
	its.it_value.tv_nsec = nsecs % UM_NSEC_PER_SEC;

	its.it_interval.tv_sec = nsecs / UM_NSEC_PER_SEC;
	its.it_interval.tv_nsec = nsecs % UM_NSEC_PER_SEC;

	if (timer_settime(event_high_res_timer[cpu], 0, &its, NULL) == -1)
		return -errno;

	return 0;
}

int os_timer_one_shot(int cpu, unsigned long long nsecs)
{
	struct itimerspec its = {
		.it_value.tv_sec = nsecs / UM_NSEC_PER_SEC,
		.it_value.tv_nsec = nsecs % UM_NSEC_PER_SEC,

		.it_interval.tv_sec = 0,
		.it_interval.tv_nsec = 0, // we cheat here
	};

	timer_settime(event_high_res_timer[cpu], 0, &its, NULL);
	return 0;
}

/**
 * os_timer_disable() - disable the posix (interval) timer
 * @cpu: the CPU for which the timer is to be disabled
 */
void os_timer_disable(int cpu)
{
	struct itimerspec its;

	memset(&its, 0, sizeof(struct itimerspec));
	timer_settime(event_high_res_timer[cpu], 0, &its, NULL);
}

long long os_nsecs(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC,&ts);
	return timespec_to_ns(&ts);
}

/**
 * os_idle_sleep() - sleep until interrupted
 */
void os_idle_sleep(void)
{
	sigset_t set, old;

	/* Block SIGALRM while performing the need_resched check. */
	sigemptyset(&set);
	sigaddset(&set, SIGALRM);
	sigprocmask(SIG_BLOCK, &set, &old);

	/*
	 * Because disabling IRQs does not block SIGALRM, it is also
	 * necessary to check for any pending timer alarms.
	 */
	if (!uml_need_resched() && !timer_alarm_pending())
		sigsuspend(&old);

	/* Restore the signal mask. */
	sigprocmask(SIG_UNBLOCK, &set, NULL);
}
