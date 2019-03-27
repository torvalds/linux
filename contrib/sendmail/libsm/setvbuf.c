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
SM_RCSID("@(#)$Id: setvbuf.c,v 1.33 2013-11-22 20:51:43 ca Exp $")
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sm/io.h>
#include <sm/heap.h>
#include <sm/assert.h>
#include <sm/conf.h>
#include "local.h"

/*
**  SM_IO_SETVBUF -- set the buffering type for a file
**
**  Set one of the different kinds of buffering, optionally including
**  a buffer.
**  If 'size' is == 0 then an "optimal" size will be selected.
**  If 'buf' is == NULL then space will be allocated at 'size'.
**
**	Parameters:
**		fp -- the file that buffering is to be changed for
**		timeout -- time allowed for completing the function
**		buf -- buffer to use
**		mode -- buffering method to use
**		size -- size of 'buf'
**
**	Returns:
**		Failure: SM_IO_EOF
**		Success: 0 (zero)
*/

int
sm_io_setvbuf(fp, timeout, buf, mode, size)
	SM_FILE_T *fp;
	int timeout;
	char *buf;
	int mode;
	size_t size;
{
	int ret, flags;
	size_t iosize;
	int ttyflag;
	int fd;
	struct timeval to;

	SM_REQUIRE_ISA(fp, SmFileMagic);

	/*
	**  Verify arguments.  The `int' limit on `size' is due to this
	**  particular implementation.  Note, buf and size are ignored
	**  when setting SM_IO_NBF.
	*/

	if (mode != SM_IO_NBF)
		if ((mode != SM_IO_FBF && mode != SM_IO_LBF &&
		    mode != SM_IO_NOW) || (int) size < 0)
			return SM_IO_EOF;

	/*
	**  Write current buffer, if any.  Discard unread input (including
	**  ungetc data), cancel line buffering, and free old buffer if
	**  malloc()ed.  We also clear any eof condition, as if this were
	**  a seek.
	*/

	ret = 0;
	SM_CONVERT_TIME(fp, fd, timeout, &to);
	(void) sm_flush(fp, &timeout);
	if (HASUB(fp))
		FREEUB(fp);
	fp->f_r = fp->f_lbfsize = 0;
	flags = fp->f_flags;
	if (flags & SMMBF)
	{
		sm_free((void *) fp->f_bf.smb_base);
		fp->f_bf.smb_base = NULL;
	}
	flags &= ~(SMLBF | SMNBF | SMMBF | SMOPT | SMNPT | SMFEOF | SMNOW |
		   SMFBF);

	/* If setting unbuffered mode, skip all the hard work. */
	if (mode == SM_IO_NBF)
		goto nbf;

	/*
	**  Find optimal I/O size for seek optimization.  This also returns
	**  a `tty flag' to suggest that we check isatty(fd), but we do not
	**  care since our caller told us how to buffer.
	*/

	flags |= sm_whatbuf(fp, &iosize, &ttyflag);
	if (size == 0)
	{
		buf = NULL;	/* force local allocation */
		size = iosize;
	}

	/* Allocate buffer if needed. */
	if (buf == NULL)
	{
		if ((buf = sm_malloc(size)) == NULL)
		{
			/*
			**  Unable to honor user's request.  We will return
			**  failure, but try again with file system size.
			*/

			ret = SM_IO_EOF;
			if (size != iosize)
			{
				size = iosize;
				buf = sm_malloc(size);
			}
		}
		if (buf == NULL)
		{
			/* No luck; switch to unbuffered I/O. */
nbf:
			fp->f_flags = flags | SMNBF;
			fp->f_w = 0;
			fp->f_bf.smb_base = fp->f_p = fp->f_nbuf;
			fp->f_bf.smb_size = 1;
			return ret;
		}
		flags |= SMMBF;
	}

	/*
	**  Kill any seek optimization if the buffer is not the
	**  right size.
	**
	**  SHOULD WE ALLOW MULTIPLES HERE (i.e., ok iff (size % iosize) == 0)?
	*/

	if (size != iosize)
		flags |= SMNPT;

	/*
	**  Fix up the SM_FILE_T fields, and set sm_cleanup for output flush on
	**  exit (since we are buffered in some way).
	*/

	if (mode == SM_IO_LBF)
		flags |= SMLBF;
	else if (mode == SM_IO_NOW)
		flags |= SMNOW;
	else if (mode == SM_IO_FBF)
		flags |= SMFBF;
	fp->f_flags = flags;
	fp->f_bf.smb_base = fp->f_p = (unsigned char *)buf;
	fp->f_bf.smb_size = size;
	/* fp->f_lbfsize is still 0 */
	if (flags & SMWR)
	{
		/*
		**  Begin or continue writing: see sm_wsetup().  Note
		**  that SMNBF is impossible (it was handled earlier).
		*/

		if (flags & SMLBF)
		{
			fp->f_w = 0;
			fp->f_lbfsize = -fp->f_bf.smb_size;
		}
		else
			fp->f_w = size;
	}
	else
	{
		/* begin/continue reading, or stay in intermediate state */
		fp->f_w = 0;
	}

	atexit(sm_cleanup);
	return ret;
}
