/*-
 * Copyright (c) 1992, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1992, 1993, 1994, 1995, 1996
 *	Keith Bostic.  All rights reserved.
 *
 * See the LICENSE file for redistribution information.
 */

#include "config.h"

#ifndef lint
static const char sccsid[] = "$Id: v_status.c,v 10.10 2001/06/25 15:19:35 skimo Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/stat.h>

#include <bitstring.h>
#include <limits.h>
#include <stdio.h>

#include "../common/common.h"
#include "vi.h"

/*
 * v_status -- ^G
 *	Show the file status.
 *
 * PUBLIC: int v_status(SCR *, VICMD *);
 */
int
v_status(SCR *sp, VICMD *vp)
{
	(void)msgq_status(sp, vp->m_start.lno, MSTAT_SHOWLAST);
	return (0);
}
