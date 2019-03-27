/*
 * Copyright (c) 1999-2000 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 *	$Id: timers.h,v 8.7 2013-11-22 20:51:57 ca Exp $
 *
 * Contributed by Exactis.com, Inc.
 *
 */

#ifndef TIMERS_H
#define TIMERS_H 1

#define MAXTIMERSTACK	20		/* maximum timer depth */

#define TIMER	struct _timer

TIMER
{
	long	ti_wall_sec;		/* wall clock seconds */
	long	ti_wall_usec;		/* ... microseconds */
	long	ti_cpu_sec;		/* cpu time seconds */
	long	ti_cpu_usec;		/* ... microseconds */
};

extern void	pushtimer __P((TIMER *));
extern void	poptimer __P((TIMER *));
extern char	*strtimer __P((TIMER *));
#endif /* ! TIMERS_H */
