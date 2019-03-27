/*
 * Copyright (c) 2000-2001 Proofpoint, Inc. and its suppliers.
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
SM_RCSID("@(#)$Id: strerror.c,v 1.24 2013-11-22 20:51:43 ca Exp $")

/*
**  define strerror for platforms that lack it.
*/

#include <errno.h>
#include <stdio.h>	/* sys_errlist, on some platforms */

#include <sm/io.h>	/* sm_snprintf */
#include <sm/string.h>
#include <sm/conf.h>
#include <sm/errstring.h>

#if !defined(ERRLIST_PREDEFINED)
extern char *sys_errlist[];
extern int sys_nerr;
#endif /* !defined(ERRLIST_PREDEFINED) */

#if !HASSTRERROR

/*
**  STRERROR -- return error message string corresponding to an error number.
**
**	Parameters:
**		err -- error number.
**
**	Returns:
**		Error string (might be pointer to static buffer).
*/

char *
strerror(err)
	int err;
{
	static char buf[64];

	if (err >= 0 && err < sys_nerr)
		return (char *) sys_errlist[err];
	else
	{
		(void) sm_snprintf(buf, sizeof(buf), "Error %d", err);
		return buf;
	}
}
#endif /* !HASSTRERROR */
