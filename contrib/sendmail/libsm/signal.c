/*
 * Copyright (c) 2000-2001 Proofpoint, Inc. and its suppliers.
 *      All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 */

#include <sm/gen.h>
SM_RCSID("@(#)$Id: signal.c,v 1.18 2013-11-22 20:51:43 ca Exp $")

#if SM_CONF_SETITIMER
# include <sm/time.h>
#endif /* SM_CONF_SETITIMER */
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sm/clock.h>
#include <sm/signal.h>
#include <signal.h>
#include <sm/string.h>

unsigned int	volatile InCriticalSection; /* >0 if inside critical section */
int		volatile PendingSignal;	/* pending signal to resend */

/*
**  SM_SIGNAL -- set a signal handler
**
**	This is essentially old BSD "signal(3)".
**
**	NOTE:	THIS CAN BE CALLED FROM A SIGNAL HANDLER.  DO NOT ADD
**		ANYTHING TO THIS ROUTINE UNLESS YOU KNOW WHAT YOU ARE
**		DOING.
*/

sigfunc_t
sm_signal(sig, handler)
	int sig;
	sigfunc_t handler;
{
# if defined(SA_RESTART) || (!defined(SYS5SIGNALS) && !defined(BSD4_3))
	struct sigaction n, o;
# endif /* defined(SA_RESTART) || (!defined(SYS5SIGNALS) && !defined(BSD4_3)) */

	/*
	**  First, try for modern signal calls
	**  and restartable syscalls
	*/

# ifdef SA_RESTART
	(void) memset(&n, '\0', sizeof n);
#  if USE_SA_SIGACTION
	n.sa_sigaction = (void(*)(int, siginfo_t *, void *)) handler;
	n.sa_flags = SA_RESTART|SA_SIGINFO;
#  else /* USE_SA_SIGACTION */
	n.sa_handler = handler;
	n.sa_flags = SA_RESTART;
#  endif /* USE_SA_SIGACTION */
	if (sigaction(sig, &n, &o) < 0)
		return SIG_ERR;
	return o.sa_handler;
# else /* SA_RESTART */

	/*
	**  Else check for SYS5SIGNALS or
	**  BSD4_3 signals
	*/

#  if defined(SYS5SIGNALS) || defined(BSD4_3)
#   ifdef BSD4_3
	return signal(sig, handler);
#   else /* BSD4_3 */
	return sigset(sig, handler);
#   endif /* BSD4_3 */
#  else /* defined(SYS5SIGNALS) || defined(BSD4_3) */

	/*
	**  Finally, if nothing else is available,
	**  go for a default
	*/

	(void) memset(&n, '\0', sizeof n);
	n.sa_handler = handler;
	if (sigaction(sig, &n, &o) < 0)
		return SIG_ERR;
	return o.sa_handler;
#  endif /* defined(SYS5SIGNALS) || defined(BSD4_3) */
# endif /* SA_RESTART */
}
/*
**  SM_BLOCKSIGNAL -- hold a signal to prevent delivery
**
**	Parameters:
**		sig -- the signal to block.
**
**	Returns:
**		1 signal was previously blocked
**		0 signal was not previously blocked
**		-1 on failure.
*/

int
sm_blocksignal(sig)
	int sig;
{
# ifdef BSD4_3
#  ifndef sigmask
#   define sigmask(s)	(1 << ((s) - 1))
#  endif /* ! sigmask */
	return (sigblock(sigmask(sig)) & sigmask(sig)) != 0;
# else /* BSD4_3 */
#  ifdef ALTOS_SYSTEM_V
	sigfunc_t handler;

	handler = sigset(sig, SIG_HOLD);
	if (handler == SIG_ERR)
		return -1;
	else
		return handler == SIG_HOLD;
#  else /* ALTOS_SYSTEM_V */
	sigset_t sset, oset;

	(void) sigemptyset(&sset);
	(void) sigaddset(&sset, sig);
	if (sigprocmask(SIG_BLOCK, &sset, &oset) < 0)
		return -1;
	else
		return sigismember(&oset, sig);
#  endif /* ALTOS_SYSTEM_V */
# endif /* BSD4_3 */
}
/*
**  SM_RELEASESIGNAL -- release a held signal
**
**	Parameters:
**		sig -- the signal to release.
**
**	Returns:
**		1 signal was previously blocked
**		0 signal was not previously blocked
**		-1 on failure.
*/

int
sm_releasesignal(sig)
	int sig;
{
# ifdef BSD4_3
	return (sigsetmask(sigblock(0) & ~sigmask(sig)) & sigmask(sig)) != 0;
# else /* BSD4_3 */
#  ifdef ALTOS_SYSTEM_V
	sigfunc_t handler;

	handler = sigset(sig, SIG_HOLD);
	if (sigrelse(sig) < 0)
		return -1;
	else
		return handler == SIG_HOLD;
#  else /* ALTOS_SYSTEM_V */
	sigset_t sset, oset;

	(void) sigemptyset(&sset);
	(void) sigaddset(&sset, sig);
	if (sigprocmask(SIG_UNBLOCK, &sset, &oset) < 0)
		return -1;
	else
		return sigismember(&oset, sig);
#  endif /* ALTOS_SYSTEM_V */
# endif /* BSD4_3 */
}
/*
**  PEND_SIGNAL -- Add a signal to the pending signal list
**
**	Parameters:
**		sig -- signal to add
**
**	Returns:
**		none.
**
**	NOTE:	THIS CAN BE CALLED FROM A SIGNAL HANDLER.  DO NOT ADD
**		ANYTHING TO THIS ROUTINE UNLESS YOU KNOW WHAT YOU ARE
**		DOING.
*/

void
pend_signal(sig)
	int sig;
{
	int sigbit;
	int save_errno = errno;
#if SM_CONF_SETITIMER
	struct itimerval clr;
#endif /* SM_CONF_SETITIMER */

	/*
	**  Don't want to interrupt something critical, hence delay
	**  the alarm for one second.  Hopefully, by then we
	**  will be out of the critical section.  If not, then
	**  we will just delay again.  The events to be run will
	**  still all be run, maybe just a little bit late.
	*/

	switch (sig)
	{
	  case SIGHUP:
		sigbit = PEND_SIGHUP;
		break;

	  case SIGINT:
		sigbit = PEND_SIGINT;
		break;

	  case SIGTERM:
		sigbit = PEND_SIGTERM;
		break;

	  case SIGUSR1:
		sigbit = PEND_SIGUSR1;
		break;

	  case SIGALRM:
		/* don't have to pend these */
		sigbit = 0;
		break;

	  default:
		/* If we get here, we are in trouble */
		abort();

		/* NOTREACHED */
		/* shut up stupid compiler warning on HP-UX 11 */
		sigbit = 0;
		break;
	}

	if (sigbit != 0)
		PendingSignal |= sigbit;
	(void) sm_signal(SIGALRM, sm_tick);
#if SM_CONF_SETITIMER
	clr.it_interval.tv_sec = 0;
	clr.it_interval.tv_usec = 0;
	clr.it_value.tv_sec = 1;
	clr.it_value.tv_usec = 0;
	(void) setitimer(ITIMER_REAL, &clr, NULL);
#else /* SM_CONF_SETITIMER */
	(void) alarm(1);
#endif /* SM_CONF_SETITIMER */
	errno = save_errno;
}
/*
**  SM_ALLSIGNALS -- act on all signals
**
**	Parameters:
**		block -- whether to block or release all signals.
**
**	Returns:
**		none.
*/

void
sm_allsignals(block)
	bool block;
{
# ifdef BSD4_3
#  ifndef sigmask
#   define sigmask(s)	(1 << ((s) - 1))
#  endif /* ! sigmask */
	if (block)
	{
		int mask = 0;

		mask |= sigmask(SIGALRM);
		mask |= sigmask(SIGCHLD);
		mask |= sigmask(SIGHUP);
		mask |= sigmask(SIGINT);
		mask |= sigmask(SIGTERM);
		mask |= sigmask(SIGUSR1);

		(void) sigblock(mask);
	}
	else
		sigsetmask(0);
# else /* BSD4_3 */
#  ifdef ALTOS_SYSTEM_V
	if (block)
	{
		(void) sigset(SIGALRM, SIG_HOLD);
		(void) sigset(SIGCHLD, SIG_HOLD);
		(void) sigset(SIGHUP, SIG_HOLD);
		(void) sigset(SIGINT, SIG_HOLD);
		(void) sigset(SIGTERM, SIG_HOLD);
		(void) sigset(SIGUSR1, SIG_HOLD);
	}
	else
	{
		(void) sigset(SIGALRM, SIG_DFL);
		(void) sigset(SIGCHLD, SIG_DFL);
		(void) sigset(SIGHUP, SIG_DFL);
		(void) sigset(SIGINT, SIG_DFL);
		(void) sigset(SIGTERM, SIG_DFL);
		(void) sigset(SIGUSR1, SIG_DFL);
	}
#  else /* ALTOS_SYSTEM_V */
	sigset_t sset;

	(void) sigemptyset(&sset);
	(void) sigaddset(&sset, SIGALRM);
	(void) sigaddset(&sset, SIGCHLD);
	(void) sigaddset(&sset, SIGHUP);
	(void) sigaddset(&sset, SIGINT);
	(void) sigaddset(&sset, SIGTERM);
	(void) sigaddset(&sset, SIGUSR1);
	(void) sigprocmask(block ? SIG_BLOCK : SIG_UNBLOCK, &sset, NULL);
#  endif /* ALTOS_SYSTEM_V */
# endif /* BSD4_3 */
}
/*
**  SM_SIGNAL_NOOP -- A signal no-op function
**
**	Parameters:
**		sig -- signal received
**
**	Returns:
**		SIGFUNC_RETURN
*/

/* ARGSUSED */
SIGFUNC_DECL
sm_signal_noop(sig)
	int sig;
{
	int save_errno = errno;

	FIX_SYSV_SIGNAL(sig, sm_signal_noop);
	errno = save_errno;
	return SIGFUNC_RETURN;
}

