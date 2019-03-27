/*
 * Copyright (c) 2000-2002 Proofpoint, Inc. and its suppliers.
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
SM_RCSID("@(#)$Id: wsetup.c,v 1.21 2013-11-22 20:51:44 ca Exp $")
#include <stdlib.h>
#include <errno.h>
#include <sm/io.h>
#include "local.h"

/*
**  SM_WSETUP -- check writing is safe
**
**  Various output routines call wsetup to be sure it is safe to write,
**  because either flags does not include SMMWR, or buf is NULL.
**  Used in the macro "cantwrite" found in "local.h".
**
**	Parameters:
**		fp -- the file pointer
**
**	Results:
**		Failure: SM_IO_EOF and sets errno
**		Success: 0 (zero)
*/

int
sm_wsetup(fp)
	register SM_FILE_T *fp;
{
	/* make sure stdio is set up */
	if (!Sm_IO_DidInit)
		sm_init();

	/* If we are not writing, we had better be reading and writing. */
	if ((fp->f_flags & SMWR) == 0)
	{
		if ((fp->f_flags & SMRW) == 0)
		{
			errno = EBADF;
			return SM_IO_EOF;
		}
		if (fp->f_flags & SMRD)
		{
			/* clobber any ungetc data */
			if (HASUB(fp))
				FREEUB(fp);

			/* discard read buffer */
			fp->f_flags &= ~(SMRD|SMFEOF);
			fp->f_r = 0;
			fp->f_p = fp->f_bf.smb_base;
		}
		fp->f_flags |= SMWR;
	}

	/* Make a buffer if necessary, then set w. */
	if (fp->f_bf.smb_base == NULL)
		sm_makebuf(fp);
	if (fp->f_flags & SMLBF)
	{
		/*
		**  It is line buffered, so make lbfsize be -bufsize
		**  for the sm_putc() macro.  We will change lbfsize back
		**  to 0 whenever we turn off SMWR.
		*/

		fp->f_w = 0;
		fp->f_lbfsize = -fp->f_bf.smb_size;
	}
	else
		fp->f_w = fp->f_flags & SMNBF ? 0 : fp->f_bf.smb_size;
	return 0;
}
