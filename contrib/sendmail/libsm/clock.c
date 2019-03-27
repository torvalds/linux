/*
 * Copyright (c) 1998-2004 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 * Copyright (c) 1983, 1995-1997 Eric P. Allman.  All rights reserved.
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#include <sm/gen.h>
SM_RCSID("@(#)$Id: clock.c,v 1.48 2013-11-22 20:51:42 ca Exp $")
#include <unistd.h>
#include <time.h>
#include <errno.h>
#if SM_CONF_SETITIMER
# include <sm/time.h>
#endif /* SM_CONF_SETITIMER */
#include <sm/heap.h>
#include <sm/debug.h>
#include <sm/bitops.h>
#include <sm/clock.h>
#include "local.h"
#if _FFR_SLEEP_USE_SELECT > 0
# include <sys/types.h>
#endif /* _FFR_SLEEP_USE_SELECT > 0 */
#if defined(_FFR_MAX_SLEEP_TIME) && _FFR_MAX_SLEEP_TIME > 2
# include <syslog.h>
#endif /* defined(_FFR_MAX_SLEEP_TIME) && _FFR_MAX_SLEEP_TIME > 2 */

#ifndef sigmask
# define sigmask(s)	(1 << ((s) - 1))
#endif /* ! sigmask */


/*
**  SM_SETEVENTM -- set an event to happen at a specific time in milliseconds.
**
**	Events are stored in a sorted list for fast processing.
**	An event only applies to the process that set it.
**	Source is #ifdef'd to work with older OS's that don't have setitimer()
**	(that is, don't have a timer granularity less than 1 second).
**
**	Parameters:
**		intvl -- interval until next event occurs (milliseconds).
**		func -- function to call on event.
**		arg -- argument to func on event.
**
**	Returns:
**		On success returns the SM_EVENT entry created.
**		On failure returns NULL.
**
**	Side Effects:
**		none.
*/

static SM_EVENT	*volatile SmEventQueue;		/* head of event queue */
static SM_EVENT	*volatile SmFreeEventList;	/* list of free events */

SM_EVENT *
sm_seteventm(intvl, func, arg)
	int intvl;
	void (*func)__P((int));
	int arg;
{
	ENTER_CRITICAL();
	if (SmFreeEventList == NULL)
	{
		SmFreeEventList = (SM_EVENT *) sm_pmalloc_x(sizeof *SmFreeEventList);
		SmFreeEventList->ev_link = NULL;
	}
	LEAVE_CRITICAL();

	return sm_sigsafe_seteventm(intvl, func, arg);
}

/*
**	NOTE:	THIS CAN BE CALLED FROM A SIGNAL HANDLER.  DO NOT ADD
**		ANYTHING TO THIS ROUTINE UNLESS YOU KNOW WHAT YOU ARE
**		DOING.
*/

SM_EVENT *
sm_sigsafe_seteventm(intvl, func, arg)
	int intvl;
	void (*func)__P((int));
	int arg;
{
	register SM_EVENT **evp;
	register SM_EVENT *ev;
#if SM_CONF_SETITIMER
	auto struct timeval now, nowi, ival;
	auto struct itimerval itime;
#else /*  SM_CONF_SETITIMER */
	auto time_t now, nowi;
#endif /*  SM_CONF_SETITIMER */
	int wasblocked;

	/* negative times are not allowed */
	if (intvl <= 0)
		return NULL;

	wasblocked = sm_blocksignal(SIGALRM);
#if SM_CONF_SETITIMER
	ival.tv_sec = intvl / 1000;
	ival.tv_usec = (intvl - ival.tv_sec * 1000) * 10;
	(void) gettimeofday(&now, NULL);
	nowi = now;
	timeradd(&now, &ival, &nowi);
#else /*  SM_CONF_SETITIMER */
	now = time(NULL);
	nowi = now + (time_t)(intvl / 1000);
#endif /*  SM_CONF_SETITIMER */

	/* search event queue for correct position */
	for (evp = (SM_EVENT **) (&SmEventQueue);
	     (ev = *evp) != NULL;
	     evp = &ev->ev_link)
	{
#if SM_CONF_SETITIMER
		if (timercmp(&(ev->ev_time), &nowi, >=))
#else /* SM_CONF_SETITIMER */
		if (ev->ev_time >= nowi)
#endif /* SM_CONF_SETITIMER */
			break;
	}

	ENTER_CRITICAL();
	if (SmFreeEventList == NULL)
	{
		/*
		**  This shouldn't happen.  If called from sm_seteventm(),
		**  we have just malloced a SmFreeEventList entry.  If
		**  called from a signal handler, it should have been
		**  from an existing event which sm_tick() just added to
		**  SmFreeEventList.
		*/

		LEAVE_CRITICAL();
		if (wasblocked == 0)
			(void) sm_releasesignal(SIGALRM);
		return NULL;
	}
	else
	{
		ev = SmFreeEventList;
		SmFreeEventList = ev->ev_link;
	}
	LEAVE_CRITICAL();

	/* insert new event */
	ev->ev_time = nowi;
	ev->ev_func = func;
	ev->ev_arg = arg;
	ev->ev_pid = getpid();
	ENTER_CRITICAL();
	ev->ev_link = *evp;
	*evp = ev;
	LEAVE_CRITICAL();

	(void) sm_signal(SIGALRM, sm_tick);
# if SM_CONF_SETITIMER
	timersub(&SmEventQueue->ev_time, &now, &itime.it_value);
	itime.it_interval.tv_sec = 0;
	itime.it_interval.tv_usec = 0;
	if (itime.it_value.tv_sec < 0)
		itime.it_value.tv_sec = 0;
	if (itime.it_value.tv_sec == 0 && itime.it_value.tv_usec == 0)
		itime.it_value.tv_usec = 1000;
	(void) setitimer(ITIMER_REAL, &itime, NULL);
# else /* SM_CONF_SETITIMER */
	intvl = SmEventQueue->ev_time - now;
	(void) alarm((unsigned) (intvl < 1 ? 1 : intvl));
# endif /* SM_CONF_SETITIMER */
	if (wasblocked == 0)
		(void) sm_releasesignal(SIGALRM);
	return ev;
}
/*
**  SM_CLREVENT -- remove an event from the event queue.
**
**	Parameters:
**		ev -- pointer to event to remove.
**
**	Returns:
**		none.
**
**	Side Effects:
**		arranges for event ev to not happen.
*/

void
sm_clrevent(ev)
	register SM_EVENT *ev;
{
	register SM_EVENT **evp;
	int wasblocked;
# if SM_CONF_SETITIMER
	struct itimerval clr;
# endif /* SM_CONF_SETITIMER */

	if (ev == NULL)
		return;

	/* find the parent event */
	wasblocked = sm_blocksignal(SIGALRM);
	for (evp = (SM_EVENT **) (&SmEventQueue);
	     *evp != NULL;
	     evp = &(*evp)->ev_link)
	{
		if (*evp == ev)
			break;
	}

	/* now remove it */
	if (*evp != NULL)
	{
		ENTER_CRITICAL();
		*evp = ev->ev_link;
		ev->ev_link = SmFreeEventList;
		SmFreeEventList = ev;
		LEAVE_CRITICAL();
	}

	/* restore clocks and pick up anything spare */
	if (wasblocked == 0)
		(void) sm_releasesignal(SIGALRM);
	if (SmEventQueue != NULL)
		(void) kill(getpid(), SIGALRM);
	else
	{
		/* nothing left in event queue, no need for an alarm */
# if SM_CONF_SETITIMER
		clr.it_interval.tv_sec = 0;
		clr.it_interval.tv_usec = 0;
		clr.it_value.tv_sec = 0;
		clr.it_value.tv_usec = 0;
		(void) setitimer(ITIMER_REAL, &clr, NULL);
# else /* SM_CONF_SETITIMER */
		(void) alarm(0);
# endif /* SM_CONF_SETITIMER */
	}
}
/*
**  SM_CLEAR_EVENTS -- remove all events from the event queue.
**
**	Parameters:
**		none.
**
**	Returns:
**		none.
*/

void
sm_clear_events()
{
	register SM_EVENT *ev;
#if SM_CONF_SETITIMER
	struct itimerval clr;
#endif /* SM_CONF_SETITIMER */
	int wasblocked;

	/* nothing will be left in event queue, no need for an alarm */
#if SM_CONF_SETITIMER
	clr.it_interval.tv_sec = 0;
	clr.it_interval.tv_usec = 0;
	clr.it_value.tv_sec = 0;
	clr.it_value.tv_usec = 0;
	(void) setitimer(ITIMER_REAL, &clr, NULL);
#else /* SM_CONF_SETITIMER */
	(void) alarm(0);
#endif /* SM_CONF_SETITIMER */

	if (SmEventQueue == NULL)
		return;

	wasblocked = sm_blocksignal(SIGALRM);

	/* find the end of the EventQueue */
	for (ev = SmEventQueue; ev->ev_link != NULL; ev = ev->ev_link)
		continue;

	ENTER_CRITICAL();
	ev->ev_link = SmFreeEventList;
	SmFreeEventList = SmEventQueue;
	SmEventQueue = NULL;
	LEAVE_CRITICAL();

	/* restore clocks and pick up anything spare */
	if (wasblocked == 0)
		(void) sm_releasesignal(SIGALRM);
}
/*
**  SM_TICK -- take a clock tick
**
**	Called by the alarm clock.  This routine runs events as needed.
**	Always called as a signal handler, so we assume that SIGALRM
**	has been blocked.
**
**	Parameters:
**		One that is ignored; for compatibility with signal handlers.
**
**	Returns:
**		none.
**
**	Side Effects:
**		calls the next function in EventQueue.
**
**	NOTE:	THIS CAN BE CALLED FROM A SIGNAL HANDLER.  DO NOT ADD
**		ANYTHING TO THIS ROUTINE UNLESS YOU KNOW WHAT YOU ARE
**		DOING.
*/

/* ARGSUSED */
SIGFUNC_DECL
sm_tick(sig)
	int sig;
{
	register SM_EVENT *ev;
	pid_t mypid;
	int save_errno = errno;
#if SM_CONF_SETITIMER
	struct itimerval clr;
	struct timeval now;
#else /* SM_CONF_SETITIMER */
	register time_t now;
#endif /* SM_CONF_SETITIMER */

#if SM_CONF_SETITIMER
	clr.it_interval.tv_sec = 0;
	clr.it_interval.tv_usec = 0;
	clr.it_value.tv_sec = 0;
	clr.it_value.tv_usec = 0;
	(void) setitimer(ITIMER_REAL, &clr, NULL);
	gettimeofday(&now, NULL);
#else /* SM_CONF_SETITIMER */
	(void) alarm(0);
	now = time(NULL);
#endif /* SM_CONF_SETITIMER */

	FIX_SYSV_SIGNAL(sig, sm_tick);
	errno = save_errno;
	CHECK_CRITICAL(sig);

	mypid = getpid();
	while (PendingSignal != 0)
	{
		int sigbit = 0;
		int sig = 0;

		if (bitset(PEND_SIGHUP, PendingSignal))
		{
			sigbit = PEND_SIGHUP;
			sig = SIGHUP;
		}
		else if (bitset(PEND_SIGINT, PendingSignal))
		{
			sigbit = PEND_SIGINT;
			sig = SIGINT;
		}
		else if (bitset(PEND_SIGTERM, PendingSignal))
		{
			sigbit = PEND_SIGTERM;
			sig = SIGTERM;
		}
		else if (bitset(PEND_SIGUSR1, PendingSignal))
		{
			sigbit = PEND_SIGUSR1;
			sig = SIGUSR1;
		}
		else
		{
			/* If we get here, we are in trouble */
			abort();
		}
		PendingSignal &= ~sigbit;
		kill(mypid, sig);
	}

#if SM_CONF_SETITIMER
	gettimeofday(&now, NULL);
#else /* SM_CONF_SETITIMER */
	now = time(NULL);
#endif /* SM_CONF_SETITIMER */
	while ((ev = SmEventQueue) != NULL &&
	       (ev->ev_pid != mypid ||
#if SM_CONF_SETITIMER
		timercmp(&ev->ev_time, &now, <=)
#else /* SM_CONF_SETITIMER */
		ev->ev_time <= now
#endif /* SM_CONF_SETITIMER */
		))
	{
		void (*f)__P((int));
		int arg;
		pid_t pid;

		/* process the event on the top of the queue */
		ev = SmEventQueue;
		SmEventQueue = SmEventQueue->ev_link;

		/* we must be careful in here because ev_func may not return */
		f = ev->ev_func;
		arg = ev->ev_arg;
		pid = ev->ev_pid;
		ENTER_CRITICAL();
		ev->ev_link = SmFreeEventList;
		SmFreeEventList = ev;
		LEAVE_CRITICAL();
		if (pid != getpid())
			continue;
		if (SmEventQueue != NULL)
		{
#if SM_CONF_SETITIMER
			if (timercmp(&SmEventQueue->ev_time, &now, >))
			{
				timersub(&SmEventQueue->ev_time, &now,
					 &clr.it_value);
				clr.it_interval.tv_sec = 0;
				clr.it_interval.tv_usec = 0;
				if (clr.it_value.tv_sec < 0)
					clr.it_value.tv_sec = 0;
				if (clr.it_value.tv_sec == 0 &&
				    clr.it_value.tv_usec == 0)
					clr.it_value.tv_usec = 1000;
				(void) setitimer(ITIMER_REAL, &clr, NULL);
			}
			else
			{
				clr.it_interval.tv_sec = 0;
				clr.it_interval.tv_usec = 0;
				clr.it_value.tv_sec = 3;
				clr.it_value.tv_usec = 0;
				(void) setitimer(ITIMER_REAL, &clr, NULL);
			}
#else /* SM_CONF_SETITIMER */
			if (SmEventQueue->ev_time > now)
				(void) alarm((unsigned) (SmEventQueue->ev_time
							 - now));
			else
				(void) alarm(3);
#endif /* SM_CONF_SETITIMER */
		}

		/* call ev_func */
		errno = save_errno;
		(*f)(arg);
#if SM_CONF_SETITIMER
		clr.it_interval.tv_sec = 0;
		clr.it_interval.tv_usec = 0;
		clr.it_value.tv_sec = 0;
		clr.it_value.tv_usec = 0;
		(void) setitimer(ITIMER_REAL, &clr, NULL);
		gettimeofday(&now, NULL);
#else /* SM_CONF_SETITIMER */
		(void) alarm(0);
		now = time(NULL);
#endif /* SM_CONF_SETITIMER */
	}
	if (SmEventQueue != NULL)
	{
#if SM_CONF_SETITIMER
		timersub(&SmEventQueue->ev_time, &now, &clr.it_value);
		clr.it_interval.tv_sec = 0;
		clr.it_interval.tv_usec = 0;
		if (clr.it_value.tv_sec < 0)
			clr.it_value.tv_sec = 0;
		if (clr.it_value.tv_sec == 0 && clr.it_value.tv_usec == 0)
			clr.it_value.tv_usec = 1000;
		(void) setitimer(ITIMER_REAL, &clr, NULL);
#else /* SM_CONF_SETITIMER */
		(void) alarm((unsigned) (SmEventQueue->ev_time - now));
#endif /* SM_CONF_SETITIMER */
	}
	errno = save_errno;
	return SIGFUNC_RETURN;
}
/*
**  SLEEP -- a version of sleep that works with this stuff
**
**	Because Unix sleep uses the alarm facility, I must reimplement
**	it here.
**
**	Parameters:
**		intvl -- time to sleep.
**
**	Returns:
**		zero.
**
**	Side Effects:
**		waits for intvl time.  However, other events can
**		be run during that interval.
*/


# if !HAVE_NANOSLEEP
static void	sm_endsleep __P((int));
static bool	volatile SmSleepDone;
# endif /* !HAVE_NANOSLEEP */

#ifndef SLEEP_T
# define SLEEP_T	unsigned int
#endif /* ! SLEEP_T */

SLEEP_T
sleep(intvl)
	unsigned int intvl;
{
#if HAVE_NANOSLEEP
	struct timespec rqtp;

	if (intvl == 0)
		return (SLEEP_T) 0;
	rqtp.tv_sec = intvl;
	rqtp.tv_nsec = 0;
	nanosleep(&rqtp, NULL);
	return (SLEEP_T) 0;
#else /* HAVE_NANOSLEEP */
	int was_held;
	SM_EVENT *ev;
#if _FFR_SLEEP_USE_SELECT > 0
	int r;
# if _FFR_SLEEP_USE_SELECT > 0
	struct timeval sm_io_to;
# endif /* _FFR_SLEEP_USE_SELECT > 0 */
#endif /* _FFR_SLEEP_USE_SELECT > 0 */
#if SM_CONF_SETITIMER
	struct timeval now, begin, diff;
# if _FFR_SLEEP_USE_SELECT > 0
	struct timeval slpv;
# endif /* _FFR_SLEEP_USE_SELECT > 0 */
#else /*  SM_CONF_SETITIMER */
	time_t begin, now;
#endif /*  SM_CONF_SETITIMER */

	if (intvl == 0)
		return (SLEEP_T) 0;
#if defined(_FFR_MAX_SLEEP_TIME) && _FFR_MAX_SLEEP_TIME > 2
	if (intvl > _FFR_MAX_SLEEP_TIME)
	{
		syslog(LOG_ERR, "sleep: interval=%u exceeds max value %d",
			intvl, _FFR_MAX_SLEEP_TIME);
# if 0
		SM_ASSERT(intvl < (unsigned int) INT_MAX);
# endif /* 0 */
		intvl = _FFR_MAX_SLEEP_TIME;
	}
#endif /* defined(_FFR_MAX_SLEEP_TIME) && _FFR_MAX_SLEEP_TIME > 2 */
	SmSleepDone = false;

#if SM_CONF_SETITIMER
# if _FFR_SLEEP_USE_SELECT > 0
	slpv.tv_sec = intvl;
	slpv.tv_usec = 0;
# endif /* _FFR_SLEEP_USE_SELECT > 0 */
	(void) gettimeofday(&now, NULL);
	begin = now;
#else /*  SM_CONF_SETITIMER */
	now = begin = time(NULL);
#endif /*  SM_CONF_SETITIMER */

	ev = sm_setevent((time_t) intvl, sm_endsleep, 0);
	if (ev == NULL)
	{
		/* COMPLAIN */
#if 0
		syslog(LOG_ERR, "sleep: sm_setevent(%u) failed", intvl);
#endif /* 0 */
		SmSleepDone = true;
	}
	was_held = sm_releasesignal(SIGALRM);

	while (!SmSleepDone)
	{
#if SM_CONF_SETITIMER
		(void) gettimeofday(&now, NULL);
		timersub(&now, &begin, &diff);
		if (diff.tv_sec < 0 ||
		    (diff.tv_sec == 0 && diff.tv_usec == 0))
			break;
# if _FFR_SLEEP_USE_SELECT > 0
		timersub(&slpv, &diff, &sm_io_to);
# endif /* _FFR_SLEEP_USE_SELECT > 0 */
#else /* SM_CONF_SETITIMER */
		now = time(NULL);

		/*
		**  Check whether time expired before signal is released.
		**  Due to the granularity of time() add 1 to be on the
		**  safe side.
		*/

		if (!(begin + (time_t) intvl + 1 > now))
			break;
# if _FFR_SLEEP_USE_SELECT > 0
		sm_io_to.tv_sec = intvl - (now - begin);
		if (sm_io_to.tv_sec <= 0)
			sm_io_to.tv_sec = 1;
		sm_io_to.tv_usec = 0;
# endif /* _FFR_SLEEP_USE_SELECT > 0 */
#endif /* SM_CONF_SETITIMER */
#if _FFR_SLEEP_USE_SELECT > 0
		if (intvl <= _FFR_SLEEP_USE_SELECT)
		{
			r = select(0, NULL, NULL, NULL, &sm_io_to);
			if (r == 0)
				break;
		}
		else
#endif /* _FFR_SLEEP_USE_SELECT > 0 */
		(void) pause();
	}

	/* if out of the loop without the event being triggered remove it */
	if (!SmSleepDone)
		sm_clrevent(ev);
	if (was_held > 0)
		(void) sm_blocksignal(SIGALRM);
	return (SLEEP_T) 0;
#endif /* HAVE_NANOSLEEP */
}

#if !HAVE_NANOSLEEP
static void
sm_endsleep(ignore)
	int ignore;
{
	/*
	**  NOTE: THIS CAN BE CALLED FROM A SIGNAL HANDLER.  DO NOT ADD
	**	ANYTHING TO THIS ROUTINE UNLESS YOU KNOW WHAT YOU ARE
	**	DOING.
	*/

	SmSleepDone = true;
}
#endif /* !HAVE_NANOSLEEP */

