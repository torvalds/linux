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
static const char sccsid[] = "$Id: v_zexit.c,v 10.7 2001/06/25 15:19:37 skimo Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/time.h>

#include <bitstring.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "../common/common.h"
#include "vi.h"

/*
 * v_zexit -- ZZ
 *	Save the file and exit.
 *
 * PUBLIC: int v_zexit(SCR *, VICMD *);
 */
int
v_zexit(SCR *sp, VICMD *vp)
{
	/* Write back any modifications. */
	if (F_ISSET(sp->ep, F_MODIFIED) &&
	    file_write(sp, NULL, NULL, NULL, FS_ALL))
		return (1);

	/* Check to make sure it's not a temporary file. */
	if (file_m3(sp, 0))
		return (1);

	/* Check for more files to edit. */
	if (ex_ncheck(sp, 0))
		return (1);

	F_SET(sp, SC_EXIT);
	return (0);
}
