/*
 * Copyright (c) 1999-2001 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 * Contributed by Exactis.com, Inc.
 *
 */

#include <sm/gen.h>
SM_RCSID("@(#)$Id: timers.c,v 8.27 2013-11-22 20:51:57 ca Exp $")

#if _FFR_TIMERS
# include <sys/types.h>
# include <sm/time.h>
# include "sendmail.h"
# include <sys/resource.h>	/* Must be after sendmail.h for NCR MP-RAS */

static TIMER	BaseTimer;		/* current baseline */
static int	NTimers;		/* current pointer into stack */
static TIMER	*TimerStack[MAXTIMERSTACK];

static void
# ifdef __STDC__
warntimer(const char *msg, ...)
# else /* __STDC__ */
warntimer(msg, va_alist)
	const char *msg;
	va_dcl
# endif /* __STDC__ */
{
	char buf[MAXLINE];
	SM_VA_LOCAL_DECL

# if 0
	if (!tTd(98, 30))
		return;
# endif /* 0 */
	SM_VA_START(ap, msg);
	(void) sm_vsnprintf(buf, sizeof(buf), msg, ap);
	SM_VA_END(ap);
	sm_syslog(LOG_NOTICE, CurEnv->e_id, "%s; e_timers=0x%lx",
		  buf, (unsigned long) &CurEnv->e_timers);
}

static void
zerotimer(ptimer)
	TIMER *ptimer;
{
	memset(ptimer, '\0', sizeof(*ptimer));
}

static void
addtimer(ta, tb)
	TIMER *ta;
	TIMER *tb;
{
	tb->ti_wall_sec += ta->ti_wall_sec;
	tb->ti_wall_usec += ta->ti_wall_usec;
	if (tb->ti_wall_usec > 1000000)
	{
		tb->ti_wall_sec++;
		tb->ti_wall_usec -= 1000000;
	}
	tb->ti_cpu_sec += ta->ti_cpu_sec;
	tb->ti_cpu_usec += ta->ti_cpu_usec;
	if (tb->ti_cpu_usec > 1000000)
	{
		tb->ti_cpu_sec++;
		tb->ti_cpu_usec -= 1000000;
	}
}

static void
subtimer(ta, tb)
	TIMER *ta;
	TIMER *tb;
{
	tb->ti_wall_sec -= ta->ti_wall_sec;
	tb->ti_wall_usec -= ta->ti_wall_usec;
	if (tb->ti_wall_usec < 0)
	{
		tb->ti_wall_sec--;
		tb->ti_wall_usec += 1000000;
	}
	tb->ti_cpu_sec -= ta->ti_cpu_sec;
	tb->ti_cpu_usec -= ta->ti_cpu_usec;
	if (tb->ti_cpu_usec < 0)
	{
		tb->ti_cpu_sec--;
		tb->ti_cpu_usec += 1000000;
	}
}

static int
getcurtimer(ptimer)
	TIMER *ptimer;
{
	struct rusage ru;
	struct timeval now;

	if (getrusage(RUSAGE_SELF, &ru) < 0 || gettimeofday(&now, NULL) < 0)
		return -1;
	ptimer->ti_wall_sec = now.tv_sec;
	ptimer->ti_wall_usec = now.tv_usec;
	ptimer->ti_cpu_sec = ru.ru_utime.tv_sec + ru.ru_stime.tv_sec;
	ptimer->ti_cpu_usec = ru.ru_utime.tv_usec + ru.ru_stime.tv_usec;
	if (ptimer->ti_cpu_usec > 1000000)
	{
		ptimer->ti_cpu_sec++;
		ptimer->ti_cpu_usec -= 1000000;
	}
	return 0;
}

static void
getinctimer(ptimer)
	TIMER *ptimer;
{
	TIMER cur;

	if (getcurtimer(&cur) < 0)
	{
		zerotimer(ptimer);
		return;
	}
	if (BaseTimer.ti_wall_sec == 0)
	{
		/* first call */
		memset(ptimer, '\0', sizeof(*ptimer));
	}
	else
	{
		*ptimer = cur;
		subtimer(&BaseTimer, ptimer);
	}
	BaseTimer = cur;
}

void
flushtimers()
{
	NTimers = 0;
	(void) getcurtimer(&BaseTimer);
}

void
pushtimer(ptimer)
	TIMER *ptimer;
{
	int i;
	int save_errno = errno;
	TIMER incr;

	/* find how much time has changed since last call */
	getinctimer(&incr);

	/* add that into the old timers */
	i = NTimers;
	if (i > MAXTIMERSTACK)
		i = MAXTIMERSTACK;
	while (--i >= 0)
	{
		addtimer(&incr, TimerStack[i]);
		if (TimerStack[i] == ptimer)
		{
			warntimer("Timer@0x%lx already on stack, index=%d, NTimers=%d",
				  (unsigned long) ptimer, i, NTimers);
			errno = save_errno;
			return;
		}
	}
	errno = save_errno;

	/* handle stack overflow */
	if (NTimers >= MAXTIMERSTACK)
		return;

	/* now add the timer to the stack */
	TimerStack[NTimers++] = ptimer;
}

void
poptimer(ptimer)
	TIMER *ptimer;
{
	int i;
	int save_errno = errno;
	TIMER incr;

	/* find how much time has changed since last call */
	getinctimer(&incr);

	/* add that into the old timers */
	i = NTimers;
	if (i > MAXTIMERSTACK)
		i = MAXTIMERSTACK;
	while (--i >= 0)
		addtimer(&incr, TimerStack[i]);

	/* pop back to this timer */
	for (i = 0; i < NTimers; i++)
	{
		if (TimerStack[i] == ptimer)
			break;
	}

	if (i != NTimers - 1)
		warntimer("poptimer: odd pop (timer=0x%lx, index=%d, NTimers=%d)",
			  (unsigned long) ptimer, i, NTimers);
	NTimers = i;

	/* clean up and return */
	errno = save_errno;
}

char *
strtimer(ptimer)
	TIMER *ptimer;
{
	static char buf[40];

	(void) sm_snprintf(buf, sizeof(buf), "%ld.%06ldr/%ld.%06ldc",
		ptimer->ti_wall_sec, ptimer->ti_wall_usec,
		ptimer->ti_cpu_sec, ptimer->ti_cpu_usec);
	return buf;
}
#endif /* _FFR_TIMERS */
