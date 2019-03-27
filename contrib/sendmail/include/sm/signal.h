/*
 * Copyright (c) 1998-2001 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 * Copyright (c) 1983, 1995-1997 Eric P. Allman.  All rights reserved.
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 *	$Id: signal.h,v 1.17 2013-11-22 20:51:31 ca Exp $
 */

/*
**  SIGNAL.H -- libsm (and sendmail) signal facilities
**		Extracted from sendmail/conf.h and focusing
**		on signal configuration.
*/

#ifndef SM_SIGNAL_H
#define SM_SIGNAL_H 1

#include <sys/types.h>
#include <limits.h>
#include <signal.h>
#include <sm/cdefs.h>
#include <sm/conf.h>

/*
**  Critical signal sections
*/

#define PEND_SIGHUP	0x0001
#define PEND_SIGINT	0x0002
#define PEND_SIGTERM	0x0004
#define PEND_SIGUSR1	0x0008

#define ENTER_CRITICAL()	InCriticalSection++

#define LEAVE_CRITICAL()						\
do									\
{									\
	if (InCriticalSection > 0)					\
		InCriticalSection--;					\
} while (0)

#define CHECK_CRITICAL(sig)						\
do									\
{									\
	if (InCriticalSection > 0 && (sig) != 0)			\
	{								\
		pend_signal((sig));					\
		return SIGFUNC_RETURN;					\
	}								\
} while (0)

/* variables */
extern unsigned int	volatile InCriticalSection;	/* >0 if in critical section */
extern int		volatile PendingSignal;	/* pending signal to resend */

/* functions */
extern void		pend_signal __P((int));

/* reset signal in case System V semantics */
#ifdef SYS5SIGNALS
# define FIX_SYSV_SIGNAL(sig, handler)					\
{									\
	if ((sig) != 0)							\
		(void) sm_signal((sig), (handler));			\
}
#else /* SYS5SIGNALS */
# define FIX_SYSV_SIGNAL(sig, handler)	{ /* EMPTY */ }
#endif /* SYS5SIGNALS */

extern void		sm_allsignals __P((bool));
extern int		sm_blocksignal __P((int));
extern int		sm_releasesignal __P((int));
extern sigfunc_t	sm_signal __P((int, sigfunc_t));
extern SIGFUNC_DECL	sm_signal_noop __P((int));
#endif /* SM_SIGNAL_H */
