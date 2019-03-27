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
SM_RCSID("@(#)$Id: get.c,v 1.19 2013-11-22 20:51:43 ca Exp $")
#include <sm/io.h>
#include <sm/assert.h>
#include "local.h"

/*
**  SM_IO_GETC -- get a character from a file
**
**	Parameters:
**		fp -- the file to get the character from
**		timeout -- time to complete getc
**
**	Returns:
**		Success: the value of the character read.
**		Failure: SM_IO_EOF
**
**	This is a function version of the macro (in <sm/io.h>).
**	It is guarded with locks (which are currently not functional)
**	for multi-threaded programs.
*/

#undef sm_io_getc

int
sm_io_getc(fp, timeout)
	register SM_FILE_T *fp;
	int timeout;
{
	SM_REQUIRE_ISA(fp, SmFileMagic);
	return sm_getc(fp, timeout);
}
