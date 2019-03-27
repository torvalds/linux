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
SM_RCSID("@(#)$Id: put.c,v 1.28 2013-11-22 20:51:43 ca Exp $")
#include <string.h>
#include <errno.h>
#include <sm/io.h>
#include <sm/assert.h>
#include <sm/errstring.h>
#include <sm/string.h>
#include "local.h"
#include "fvwrite.h"

/*
**  SM_IO_PUTC -- output a character to the file
**
**  Function version of the macro sm_io_putc (in <sm/io.h>).
**
**	Parameters:
**		fp -- file to output to
**		timeout -- time to complete putc
**		c -- int value of character to output
**
**	Returns:
**		Failure: returns SM_IO_EOF _and_ sets errno
**		Success: returns sm_putc() value.
**
*/

#undef sm_io_putc

int
sm_io_putc(fp, timeout, c)
	SM_FILE_T *fp;
	int timeout;
	int c;
{
	SM_REQUIRE_ISA(fp, SmFileMagic);
	if (cantwrite(fp))
	{
		errno = EBADF;
		return SM_IO_EOF;
	}
	return sm_putc(fp, timeout, c);
}


/*
**  SM_PERROR -- print system error messages to smioerr
**
**	Parameters:
**		s -- message to print
**
**	Returns:
**		none
*/

void
sm_perror(s)
	const char *s;
{
	int save_errno = errno;

	if (s != NULL && *s != '\0')
		(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT, "%s: ", s);
	(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT, "%s\n",
			     sm_errstring(save_errno));
}
