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
SM_RCSID("@(#)$Id: ferror.c,v 1.14 2013-11-22 20:51:42 ca Exp $")
#include <sm/io.h>
#include <sm/assert.h>
#include "local.h"

/*
**  SM_IO_ERROR -- subroutine version of the macro sm_io_error.
**
**	Parameters:
**		fp -- file pointer
**
**	Returns:
**		0 (zero) when 'fp' is not in an error state
**		non-zero when 'fp' is in an error state
*/

#undef sm_io_error

int
sm_io_error(fp)
	SM_FILE_T *fp;
{
	SM_REQUIRE_ISA(fp, SmFileMagic);

	return sm_error(fp);
}
