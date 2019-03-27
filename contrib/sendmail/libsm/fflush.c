/*
 * Copyright (c) 2000-2001, 2005, 2006 Proofpoint, Inc. and its suppliers.
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
SM_RCSID("@(#)$Id: fflush.c,v 1.46 2013-11-22 20:51:42 ca Exp $")
#include <unistd.h>
#include <errno.h>
#include <sm/time.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <sm/io.h>
#include <sm/assert.h>
#include <sm/setjmp.h>
#include "local.h"
#include <sm/conf.h>

/*
**  SM_IO_FLUSH -- flush the buffer for a 'fp' to the "file"
**
**  Flush a single file. We don't allow this function to flush
**  all open files when fp==NULL any longer.
**
**	Parameters:
**		fp -- the file pointer buffer to flush
**		timeout -- time to complete the flush
**
**	Results:
**		Failure: SM_IO_EOF and sets errno
**		Success: 0 (zero)
*/

int
sm_io_flush(fp, timeout)
	register SM_FILE_T *fp;
	int SM_NONVOLATILE timeout;
{
	int fd;
	struct timeval to;

	SM_REQUIRE_ISA(fp, SmFileMagic);

	if ((fp->f_flags & (SMWR | SMRW)) == 0)
	{
		/*
		**  The file is not opened for writing, so it cannot be flushed
		**  (writable means SMWR [write] or SMRW [read/write].
		*/

		errno = EBADF;
		return SM_IO_EOF;
	}

	SM_CONVERT_TIME(fp, fd, timeout, &to);

	/* Now do the flush */
	return sm_flush(fp, (int *) &timeout);
}

/*
**  SM_FLUSH -- perform the actual flush
**
**  Assumes that 'fp' has been validated before this function called.
**
**	Parameters:
**		fp -- file pointer to be flushed
**		timeout -- max time allowed for flush (milliseconds)
**
**	Results:
**		Success: 0 (zero)
**		Failure: SM_IO_EOF and errno set
**
**	Side Effects:
**		timeout will get updated with the time remaining (if any)
*/

int
sm_flush(fp, timeout)
	register SM_FILE_T *fp;
	int *timeout;
{
	register unsigned char *p;
	register int n, t;
	int fd;

	SM_REQUIRE_ISA(fp, SmFileMagic);

	t = fp->f_flags;
	if ((t & SMWR) == 0)
		return 0;

	if (t & SMSTR)
	{
		*fp->f_p = '\0';
		return 0;
	}

	if ((p = fp->f_bf.smb_base) == NULL)
		return 0;

	n = fp->f_p - p;		/* write this much */

	if ((fd = sm_io_getinfo(fp, SM_IO_WHAT_FD, NULL)) == -1)
	{
		/* can't get an fd, likely internal 'fake' fp */
		errno = 0;
		fd = -1;
	}

	/*
	**  Set these immediately to avoid problems with longjmp and to allow
	**  exchange buffering (via setvbuf) in user write function.
	*/

	fp->f_p = p;
	fp->f_w = t & (SMLBF|SMNBF) ? 0 : fp->f_bf.smb_size; /* implies SMFBF */

	for (; n > 0; n -= t, p += t)
	{
		errno = 0; /* needed to ensure EOF correctly found */

		/* Call the file type's write function */
		t = (*fp->f_write)(fp, (char *)p, n);
		if (t <= 0)
		{
			if (t == 0 && errno == 0)
				break; /* EOF found */

			if (IS_IO_ERROR(fd, t, *timeout))
			{
				fp->f_flags |= SMERR;

				/* errno set by fp->f_write */
				return SM_IO_EOF;
			}
			SM_IO_WR_TIMEOUT(fp, fd, *timeout);
			t = 0;
		}
	}
	return 0;
}
