/*
 * Copyright (c) 2000-2001 Proofpoint, Inc. and its suppliers.
 *      All rights reserved.
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 */

#include <sm/gen.h>
SM_RCSID("@(#)$Id: fwalk.c,v 1.22 2013-11-22 20:51:43 ca Exp $")
#include <errno.h>
#include <sm/io.h>
#include "local.h"
#include "glue.h"

/*
**  SM_FWALK -- apply a function to all found-open file pointers
**
**	Parameters:
**		function -- a function vector to be applied
**		timeout -- time to complete actions (milliseconds)
**
**	Returns:
**		The (binary) OR'd result of each function call
*/

int
sm_fwalk(function, timeout)
	int (*function) __P((SM_FILE_T *, int *));
	int *timeout;
{
	register SM_FILE_T *fp;
	register int n, ret;
	register struct sm_glue *g;
	int fptimeout;

	ret = 0;
	for (g = &smglue; g != NULL; g = g->gl_next)
	{
		for (fp = g->gl_iobs, n = g->gl_niobs; --n >= 0; fp++)
		{
			if (fp->f_flags != 0)
			{
				if (*timeout == SM_TIME_DEFAULT)
					fptimeout = fp->f_timeout;
				else
					fptimeout = *timeout;
				if (fptimeout == SM_TIME_IMMEDIATE)
					continue; /* skip it */
				ret |= (*function)(fp, &fptimeout);
			}
		}
	}
	return ret;
}
