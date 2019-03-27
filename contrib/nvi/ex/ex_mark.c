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
static const char sccsid[] = "$Id: ex_mark.c,v 10.9 2001/06/25 15:19:17 skimo Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/time.h>

#include <bitstring.h>
#include <limits.h>
#include <stdio.h>

#include "../common/common.h"

/*
 * ex_mark -- :mark char
 *	      :k char
 *	Mark lines.
 *
 *
 * PUBLIC: int ex_mark(SCR *, EXCMD *);
 */
int
ex_mark(SCR *sp, EXCMD *cmdp)
{
	NEEDFILE(sp, cmdp);

	if (cmdp->argv[0]->len != 1) {
		msgq(sp, M_ERR, "136|Mark names must be a single character");
		return (1);
	}
	return (mark_set(sp, cmdp->argv[0]->bp[0], &cmdp->addr1, 1));
}
