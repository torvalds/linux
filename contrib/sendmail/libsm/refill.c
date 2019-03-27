/*
 * Copyright (c) 2000-2001, 2005-2006 Proofpoint, Inc. and its suppliers.
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
SM_RCSID("@(#)$Id: refill.c,v 1.54 2013-11-22 20:51:43 ca Exp $")
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <setjmp.h>
#include <signal.h>
#include <sm/time.h>
#include <fcntl.h>
#include <string.h>
#include <sm/io.h>
#include <sm/conf.h>
#include <sm/assert.h>
#include <sm/fdset.h>
#include "local.h"

static int sm_lflush __P((SM_FILE_T *, int *));

/*
**  SM_IO_RD_TIMEOUT -- measured timeout for reads
**
**  This #define uses a select() to wait for the 'fd' to become readable.
**  The select() can be active for up to 'To' time. The select() may not
**  use all of the the 'To' time. Hence, the amount of "wall-clock" time is
**  measured to decide how much to subtract from 'To' to update it. On some
**  BSD-based/like systems the timeout for a select() is updated for the
**  amount of time used. On many/most systems this does not happen. Therefore
**  the updating of 'To' must be done ourselves; a copy of 'To' is passed
**  since a BSD-like system will have updated it and we don't want to
**  double the time used!
**  Note: if a valid 'fd' doesn't exist yet, don't use this (e.g. the
**  sendmail buffered file type in sendmail/bf.c; see use below).
**
**	Parameters
**		fp -- the file pointer for the active file
**		fd -- raw file descriptor (from 'fp') to use for select()
**		to -- struct timeval of the timeout
**		timeout -- the original timeout value
**		sel_ret -- the return value from the select()
**
**	Returns:
**		nothing, flow through code
*/

#define SM_IO_RD_TIMEOUT(fp, fd, to, timeout, sel_ret)			\
{									\
	struct timeval sm_io_to_before, sm_io_to_after, sm_io_to_diff;	\
	fd_set sm_io_to_mask, sm_io_x_mask;				\
	errno = 0;							\
	if (timeout == SM_TIME_IMMEDIATE)				\
	{								\
		errno = EAGAIN;						\
		return SM_IO_EOF;					\
	}								\
	if (!SM_FD_OK_SELECT(fd))					\
	{								\
		errno = EINVAL;						\
		return SM_IO_EOF;					\
	}								\
	FD_ZERO(&sm_io_to_mask);					\
	FD_SET((fd), &sm_io_to_mask);					\
	FD_ZERO(&sm_io_x_mask);						\
	FD_SET((fd), &sm_io_x_mask);					\
	if (gettimeofday(&sm_io_to_before, NULL) < 0)			\
		return SM_IO_EOF;					\
	do								\
	{								\
		(sel_ret) = select((fd) + 1, &sm_io_to_mask, NULL,	\
			   	&sm_io_x_mask, (to));			\
	} while ((sel_ret) < 0 && errno == EINTR);			\
	if ((sel_ret) < 0)						\
	{								\
		/* something went wrong, errno set */			\
		fp->f_r = 0;						\
		fp->f_flags |= SMERR;					\
		return SM_IO_EOF;					\
	}								\
	else if ((sel_ret) == 0)					\
	{								\
		/* timeout */						\
		errno = EAGAIN;						\
		return SM_IO_EOF;					\
	}								\
	/* calulate wall-clock time used */				\
	if (gettimeofday(&sm_io_to_after, NULL) < 0)			\
		return SM_IO_EOF;					\
	timersub(&sm_io_to_after, &sm_io_to_before, &sm_io_to_diff);	\
	timersub((to), &sm_io_to_diff, (to));				\
}

/*
**  SM_LFLUSH -- flush a file if it is line buffered and writable
**
**	Parameters:
**		fp -- file pointer to flush
**		timeout -- original timeout value (in milliseconds)
**
**	Returns:
**		Failure: returns SM_IO_EOF and sets errno
**		Success: returns 0
*/

static int
sm_lflush(fp, timeout)
	SM_FILE_T *fp;
	int *timeout;
{

	if ((fp->f_flags & (SMLBF|SMWR)) == (SMLBF|SMWR))
		return sm_flush(fp, timeout);
	return 0;
}

/*
**  SM_REFILL -- refill a buffer
**
**	Parameters:
**		fp -- file pointer for buffer refill
**		timeout -- time to complete filling the buffer in milliseconds
**
**	Returns:
**		Success: returns 0
**		Failure: returns SM_IO_EOF
*/

int
sm_refill(fp, timeout)
	register SM_FILE_T *fp;
	int timeout;
{
	int ret, r;
	struct timeval to;
	int fd;

	if (timeout == SM_TIME_DEFAULT)
		timeout = fp->f_timeout;
	if (timeout == SM_TIME_IMMEDIATE)
	{
		/*
		**  Filling the buffer will take time and we are wanted to
		**  return immediately. And we're not EOF or ERR really.
		**  So... the failure is we couldn't do it in time.
		*/

		errno = EAGAIN;
		fp->f_r = 0; /* just to be sure */
		return 0;
	}

	/* make sure stdio is set up */
	if (!Sm_IO_DidInit)
		sm_init();

	fp->f_r = 0;		/* largely a convenience for callers */

	if (fp->f_flags & SMFEOF)
		return SM_IO_EOF;

	SM_CONVERT_TIME(fp, fd, timeout, &to);

	/* if not already reading, have to be reading and writing */
	if ((fp->f_flags & SMRD) == 0)
	{
		if ((fp->f_flags & SMRW) == 0)
		{
			errno = EBADF;
			fp->f_flags |= SMERR;
			return SM_IO_EOF;
		}

		/* switch to reading */
		if (fp->f_flags & SMWR)
		{
			if (sm_flush(fp, &timeout))
				return SM_IO_EOF;
			fp->f_flags &= ~SMWR;
			fp->f_w = 0;
			fp->f_lbfsize = 0;
		}
		fp->f_flags |= SMRD;
	}
	else
	{
		/*
		**  We were reading.  If there is an ungetc buffer,
		**  we must have been reading from that.  Drop it,
		**  restoring the previous buffer (if any).  If there
		**  is anything in that buffer, return.
		*/

		if (HASUB(fp))
		{
			FREEUB(fp);
			if ((fp->f_r = fp->f_ur) != 0)
			{
				fp->f_p = fp->f_up;

				/* revert blocking state */
				return 0;
			}
		}
	}

	if (fp->f_bf.smb_base == NULL)
		sm_makebuf(fp);

	/*
	**  Before reading from a line buffered or unbuffered file,
	**  flush all line buffered output files, per the ANSI C standard.
	*/

	if (fp->f_flags & (SMLBF|SMNBF))
		(void) sm_fwalk(sm_lflush, &timeout);

	/*
	**  If this file is linked to another, and we are going to hang
	**  on the read, flush the linked file before continuing.
	*/

	if (fp->f_flushfp != NULL &&
	    (*fp->f_getinfo)(fp, SM_IO_IS_READABLE, NULL) <= 0)
		sm_flush(fp->f_flushfp, &timeout);

	fp->f_p = fp->f_bf.smb_base;

	/*
	**  The do-while loop stops trying to read when something is read
	**  or it appears that the timeout has expired before finding
	**  something available to be read (via select()).
	*/

	ret = 0;
	do
	{
		errno = 0; /* needed to ensure EOF correctly found */
		r = (*fp->f_read)(fp, (char *)fp->f_p, fp->f_bf.smb_size);
		if (r <= 0)
		{
			if (r == 0 && errno == 0)
				break; /* EOF found */
			if (IS_IO_ERROR(fd, r, timeout))
				goto err; /* errno set */

			/* read would block */
			SM_IO_RD_TIMEOUT(fp, fd, &to, timeout, ret);
		}
	} while (r <= 0 && ret > 0);

err:
	if (r <= 0)
	{
		if (r == 0)
			fp->f_flags |= SMFEOF;
		else
			fp->f_flags |= SMERR;
		fp->f_r = 0;
		return SM_IO_EOF;
	}
	fp->f_r = r;
	return 0;
}

/*
**  SM_RGET -- refills buffer and returns first character
**
**  Handle sm_getc() when the buffer ran out:
**  Refill, then return the first character in the newly-filled buffer.
**
**	Parameters:
**		fp -- file pointer to work on
**		timeout -- time to complete refill
**
**	Returns:
**		Success: first character in refilled buffer as an int
**		Failure: SM_IO_EOF
*/

int
sm_rget(fp, timeout)
	register SM_FILE_T *fp;
	int timeout;
{
	if (sm_refill(fp, timeout) == 0)
	{
		fp->f_r--;
		return *fp->f_p++;
	}
	return SM_IO_EOF;
}
