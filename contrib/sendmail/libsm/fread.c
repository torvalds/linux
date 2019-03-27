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
SM_RCSID("@(#)$Id: fread.c,v 1.29 2013-11-22 20:51:42 ca Exp $")
#include <string.h>
#include <errno.h>
#include <sm/io.h>
#include <sm/assert.h>
#include "local.h"

/*
**  SM_IO_READ -- read data from the file pointer
**
**	Parameters:
**		fp -- file pointer to read from
**		timeout -- time to complete the read
**		buf -- location to place read data
**		size -- size of each chunk of data
**
**	Returns:
**		Failure: returns 0 (zero) _and_ sets errno
**		Success: returns the number of whole chunks read.
**
**	A read returning 0 (zero) is only an indication of error when errno
**	has been set.
*/

size_t
sm_io_read(fp, timeout, buf, size)
	SM_FILE_T *fp;
	int timeout;
	void *buf;
	size_t size;
{
	register size_t resid = size;
	register char *p;
	register int r;

	SM_REQUIRE_ISA(fp, SmFileMagic);

	if (fp->f_read == NULL)
	{
		errno = ENODEV;
		return 0;
	}

	/*
	**  The ANSI standard requires a return value of 0 for a count
	**  or a size of 0.  Peculiarily, it imposes no such requirements
	**  on fwrite; it only requires read to be broken.
	*/

	if (resid == 0)
		return 0;
	if (fp->f_r < 0)
		fp->f_r = 0;
	p = buf;
	while ((int) resid > (r = fp->f_r))
	{
		(void) memcpy((void *) p, (void *) fp->f_p, (size_t) r);
		fp->f_p += r;
		/* fp->f_r = 0 ... done in sm_refill */
		p += r;
		resid -= r;
		if ((fp->f_flags & SMNOW) != 0 && r > 0)
		{
			/*
			**  Take whatever we have available. Spend no more time
			**  trying to get all that has been requested.
			**  This is needed on some file types (such as
			**  SASL) that would jam when given extra, untimely
			**  reads.
			*/

			fp->f_r -= r;
			return size - resid;
		}
		if (sm_refill(fp, timeout) != 0)
		{
			/* no more input: return partial result */
			return size - resid;
		}
	}
	(void) memcpy((void *) p, (void *) fp->f_p, resid);
	fp->f_r -= resid;
	fp->f_p += resid;
	return size;
}
