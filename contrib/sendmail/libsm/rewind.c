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
SM_RCSID("@(#)$Id: rewind.c,v 1.19 2013-11-22 20:51:43 ca Exp $")
#include <errno.h>
#include <sm/io.h>
#include <sm/assert.h>
#include "local.h"

/*
**  SM_IO_REWIND -- rewind the file
**
**	Seeks the file to the begining and clears any outstanding errors.
**
**	Parameters:
**		fp -- the flie pointer for rewind
**		timeout -- time to complete the rewind
**
**	Returns:
**		none.
*/

void
sm_io_rewind(fp, timeout)
	register SM_FILE_T *fp;
	int timeout;
{
	SM_REQUIRE_ISA(fp, SmFileMagic);
	(void) sm_io_seek(fp, timeout, 0L, SM_IO_SEEK_SET);
	(void) sm_io_clearerr(fp);
	errno = 0;      /* not required, but seems reasonable */
}
