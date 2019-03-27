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
SM_RCSID("@(#)$Id: fpurge.c,v 1.21 2013-11-22 20:51:42 ca Exp $")
#include <stdlib.h>
#include <errno.h>
#include <sm/io.h>
#include <sm/assert.h>
#include "local.h"

/*
**  SM_IO_PURGE -- purge/empty the buffer without committing buffer content
**
**	Parameters:
**		fp -- file pointer to purge
**
**	Returns:
**		Failure: returns SM_IO_EOF and sets errno
**		Success: returns 0 (zero)
*/

int
sm_io_purge(fp)
	register SM_FILE_T *fp;
{
	SM_REQUIRE_ISA(fp, SmFileMagic);
	if (!fp->f_flags)
	{
		errno = EBADF;
		return SM_IO_EOF;
	}

	if (HASUB(fp))
		FREEUB(fp);
	fp->f_p = fp->f_bf.smb_base;
	fp->f_r = 0;

	/* implies SMFBF */
	fp->f_w = fp->f_flags & (SMLBF|SMNBF) ? 0 : fp->f_bf.smb_size;
	return 0;
}
