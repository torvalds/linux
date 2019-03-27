/*
 *  Copyright (c) 1999-2004, 2006 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#include <sm/gen.h>
SM_RCSID("@(#)$Id: signal.c,v 8.45 2013-11-22 20:51:36 ca Exp $")

#include "libmilter.h"

/*
**  thread to handle signals
*/

static smutex_t M_Mutex;

static int MilterStop = MILTER_CONT;

static void	*mi_signal_thread __P((void *));
static int	 mi_spawn_signal_thread __P((char *));

/*
**  MI_STOP -- return value of MilterStop
**
**	Parameters:
**		none.
**
**	Returns:
**		value of MilterStop
*/

int
mi_stop()
{
	return MilterStop;
}
/*
**  MI_STOP_MILTERS -- set value of MilterStop
**
**	Parameters:
**		v -- new value for MilterStop.
**
**	Returns:
**		none.
*/

void
mi_stop_milters(v)
	int v;
{
	(void) smutex_lock(&M_Mutex);
	if (MilterStop < v)
		MilterStop = v;

	/* close listen socket */
	mi_closener();
	(void) smutex_unlock(&M_Mutex);
}
/*
**  MI_CLEAN_SIGNALS -- clean up signal handler thread
**
**	Parameters:
**		none.
**
**	Returns:
**		none.
*/

void
mi_clean_signals()
{
	(void) smutex_destroy(&M_Mutex);
}
/*
**  MI_SIGNAL_THREAD -- thread to deal with signals
**
**	Parameters:
**		name -- name of milter
**
**	Returns:
**		NULL
*/

static void *
mi_signal_thread(name)
	void *name;
{
	int sig, errs, sigerr;
	sigset_t set;

	(void) sigemptyset(&set);
	(void) sigaddset(&set, SIGHUP);
	(void) sigaddset(&set, SIGTERM);

	/* Handle Ctrl-C gracefully for debugging */
	(void) sigaddset(&set, SIGINT);
	errs = 0;

	for (;;)
	{
		sigerr = sig = 0;
#if SIGWAIT_TAKES_1_ARG
		if ((sig = sigwait(&set)) < 0)
#else
		if ((sigerr = sigwait(&set, &sig)) != 0)
#endif
		{
			/* some OS return -1 and set errno: copy it */
			if (sigerr <= 0)
				sigerr = errno;

			/* this can happen on OSF/1 (at least) */
			if (sigerr == EINTR)
				continue;
			smi_log(SMI_LOG_ERR,
				"%s: sigwait returned error: %d",
				(char *)name, sigerr);
			if (++errs > MAX_FAILS_T)
			{
				mi_stop_milters(MILTER_ABRT);
				return NULL;
			}
			continue;
		}
		errs = 0;

		switch (sig)
		{
		  case SIGHUP:
		  case SIGTERM:
			mi_stop_milters(MILTER_STOP);
			return NULL;
		  case SIGINT:
			mi_stop_milters(MILTER_ABRT);
			return NULL;
		  default:
			smi_log(SMI_LOG_ERR,
				"%s: sigwait returned unmasked signal: %d",
				(char *)name, sig);
			break;
		}
	}
	/* NOTREACHED */
}
/*
**  MI_SPAWN_SIGNAL_THREAD -- spawn thread to handle signals
**
**	Parameters:
**		name -- name of milter
**
**	Returns:
**		MI_SUCCESS/MI_FAILURE
*/

static int
mi_spawn_signal_thread(name)
	char *name;
{
	sthread_t tid;
	int r;
	sigset_t set;

	/* Mask HUP and KILL signals */
	(void) sigemptyset(&set);
	(void) sigaddset(&set, SIGHUP);
	(void) sigaddset(&set, SIGTERM);
	(void) sigaddset(&set, SIGINT);

	if (pthread_sigmask(SIG_BLOCK, &set, NULL) != 0)
	{
		smi_log(SMI_LOG_ERR,
			"%s: Couldn't mask HUP and KILL signals", name);
		return MI_FAILURE;
	}
	r = thread_create(&tid, mi_signal_thread, (void *)name);
	if (r != 0)
	{
		smi_log(SMI_LOG_ERR,
			"%s: Couldn't start signal thread: %d",
			name, r);
		return MI_FAILURE;
	}
	return MI_SUCCESS;
}
/*
**  MI_CONTROL_STARTUP -- startup for thread to handle signals
**
**	Parameters:
**		name -- name of milter
**
**	Returns:
**		MI_SUCCESS/MI_FAILURE
*/

int
mi_control_startup(name)
	char *name;
{

	if (!smutex_init(&M_Mutex))
	{
		smi_log(SMI_LOG_ERR,
			"%s: Couldn't initialize control pipe mutex", name);
		return MI_FAILURE;
	}

	/*
	**  spawn_signal_thread must happen before other threads are spawned
	**  off so that it can mask the right signals and other threads
	**  will inherit that mask.
	*/
	if (mi_spawn_signal_thread(name) == MI_FAILURE)
	{
		smi_log(SMI_LOG_ERR,
			"%s: Couldn't spawn signal thread", name);
		(void) smutex_destroy(&M_Mutex);
		return MI_FAILURE;
	}
	return MI_SUCCESS;
}
