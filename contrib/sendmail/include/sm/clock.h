/*
 * Copyright (c) 1998-2001, 2004 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 * Copyright (c) 1983, 1995-1997 Eric P. Allman.  All rights reserved.
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 *	$Id: clock.h,v 1.14 2013-11-22 20:51:31 ca Exp $
 */

/*
**  CLOCK.H -- for co-ordinating timed events
*/

#ifndef _SM_CLOCK_H
# define _SM_CLOCK_H 1

# include <sm/signal.h>
# if SM_CONF_SETITIMER
#  include <sys/time.h>
# endif /* SM_CONF_SETITIMER */

/*
**  STRUCT SM_EVENT -- event queue.
**
**	Maintained in sorted order.
**
**	We store the pid of the process that set this event to insure
**	that when we fork we will not take events intended for the parent.
*/

struct sm_event
{
# if SM_CONF_SETITIMER
	struct timeval	ev_time;	/* time of the call (microseconds) */
# else /* SM_CONF_SETITIMER */
	time_t		ev_time;	/* time of the call (seconds) */
# endif /* SM_CONF_SETITIMER */
	void		(*ev_func)__P((int));
					/* function to call */
	int		ev_arg;		/* argument to ev_func */
	pid_t		ev_pid;		/* pid that set this event */
	struct sm_event	*ev_link;	/* link to next item */
};

typedef struct sm_event	SM_EVENT;

/* functions */
extern void	sm_clrevent __P((SM_EVENT *));
extern void	sm_clear_events __P((void));
extern SM_EVENT	*sm_seteventm __P((int, void(*)__P((int)), int));
extern SM_EVENT	*sm_sigsafe_seteventm __P((int, void(*)__P((int)), int));
extern SIGFUNC_DECL	sm_tick __P((int));

/*
**  SM_SETEVENT -- set an event to happen at a specific time in seconds.
**
**	Translates the seconds into milliseconds and calls sm_seteventm()
**	to get a specific event to happen in the future at a specific time.
**
**	Parameters:
**		t -- intvl until next event occurs (seconds).
**		f -- function to call on event.
**		a -- argument to func on event.
**
**	Returns:
**		result of sm_seteventm().
**
**	Side Effects:
**		Any that sm_seteventm() have.
*/

#define sm_setevent(t, f, a) sm_seteventm((int)((t) * 1000), (f), (a))
#define sm_sigsafe_setevent(t, f, a) sm_sigsafe_seteventm((int)((t) * 1000), (f), (a))

#endif /* _SM_CLOCK_H */
