/*
 * Copyright (c) 2000-2001, 2004 Proofpoint, Inc. and its suppliers.
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
SM_RCSID("@(#)$Id: fseek.c,v 1.48 2013-11-22 20:51:42 ca Exp $")
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <setjmp.h>
#include <sm/time.h>
#include <sm/signal.h>
#include <sm/io.h>
#include <sm/assert.h>
#include <sm/clock.h>
#include "local.h"

#define POS_ERR	(-(off_t)1)

static void	seekalrm __P((int));
static jmp_buf SeekTimeOut;

/*
**  SEEKALRM -- handler when timeout activated for sm_io_seek()
**
**  Returns flow of control to where setjmp(SeekTimeOut) was set.
**
**	Parameters:
**		sig -- unused
**
**	Returns:
**		does not return
**
**	Side Effects:
**		returns flow of control to setjmp(SeekTimeOut).
**
**	NOTE:	THIS CAN BE CALLED FROM A SIGNAL HANDLER.  DO NOT ADD
**		ANYTHING TO THIS ROUTINE UNLESS YOU KNOW WHAT YOU ARE
**		DOING.
*/

/* ARGSUSED0 */
static void
seekalrm(sig)
	int sig;
{
	longjmp(SeekTimeOut, 1);
}

/*
**  SM_IO_SEEK -- position the file pointer
**
**	Parameters:
**		fp -- the file pointer to be seek'd
**		timeout -- time to complete seek (milliseconds)
**		offset -- seek offset based on 'whence'
**		whence -- indicates where seek is relative from.
**			One of SM_IO_SEEK_{CUR,SET,END}.
**	Returns:
**		Failure: returns -1 (minus 1) and sets errno
**		Success: returns 0 (zero)
*/

int
sm_io_seek(fp, timeout, offset, whence)
	register SM_FILE_T *fp;
	int SM_NONVOLATILE timeout;
	long SM_NONVOLATILE offset;
	int SM_NONVOLATILE whence;
{
	bool havepos;
	off_t target, curoff;
	size_t n;
	struct stat st;
	int ret;
	SM_EVENT *evt = NULL;
	register off_t (*seekfn) __P((SM_FILE_T *, off_t, int));

	SM_REQUIRE_ISA(fp, SmFileMagic);

	/* make sure stdio is set up */
	if (!Sm_IO_DidInit)
		sm_init();

	/* Have to be able to seek. */
	if ((seekfn = fp->f_seek) == NULL)
	{
		errno = ESPIPE;			/* historic practice */
		return -1;
	}

	if (timeout == SM_TIME_DEFAULT)
		timeout = fp->f_timeout;
	if (timeout == SM_TIME_IMMEDIATE)
	{
		/*
		**  Filling the buffer will take time and we are wanted to
		**  return immediately. So...
		*/

		errno = EAGAIN;
		return -1;
	}

#define SM_SET_ALARM()						\
	if (timeout != SM_TIME_FOREVER)				\
	{							\
		if (setjmp(SeekTimeOut) != 0)			\
		{						\
			errno = EAGAIN;				\
			return -1;				\
		}						\
		evt = sm_seteventm(timeout, seekalrm, 0);	\
	}

	/*
	**  Change any SM_IO_SEEK_CUR to SM_IO_SEEK_SET, and check `whence'
	**  argument. After this, whence is either SM_IO_SEEK_SET or
	**  SM_IO_SEEK_END.
	*/

	switch (whence)
	{
	  case SM_IO_SEEK_CUR:

		/*
		**  In order to seek relative to the current stream offset,
		**  we have to first find the current stream offset a la
		**  ftell (see ftell for details).
		*/

		/* may adjust seek offset on append stream */
		sm_flush(fp, (int *) &timeout);
		SM_SET_ALARM();
		if (fp->f_flags & SMOFF)
			curoff = fp->f_lseekoff;
		else
		{
			curoff = (*seekfn)(fp, (off_t) 0, SM_IO_SEEK_CUR);
			if (curoff == -1L)
			{
				ret = -1;
				goto clean;
			}
		}
		if (fp->f_flags & SMRD)
		{
			curoff -= fp->f_r;
			if (HASUB(fp))
				curoff -= fp->f_ur;
		}
		else if (fp->f_flags & SMWR && fp->f_p != NULL)
			curoff += fp->f_p - fp->f_bf.smb_base;

		offset += curoff;
		whence = SM_IO_SEEK_SET;
		havepos = true;
		break;

	  case SM_IO_SEEK_SET:
	  case SM_IO_SEEK_END:
		SM_SET_ALARM();
		curoff = 0;		/* XXX just to keep gcc quiet */
		havepos = false;
		break;

	  default:
		errno = EINVAL;
		return -1;
	}

	/*
	**  Can only optimise if:
	**	reading (and not reading-and-writing);
	**	not unbuffered; and
	**	this is a `regular' Unix file (and hence seekfn==sm_stdseek).
	**  We must check SMNBF first, because it is possible to have SMNBF
	**  and SMSOPT both set.
	*/

	if (fp->f_bf.smb_base == NULL)
		sm_makebuf(fp);
	if (fp->f_flags & (SMWR | SMRW | SMNBF | SMNPT))
		goto dumb;
	if ((fp->f_flags & SMOPT) == 0)
	{
		if (seekfn != sm_stdseek ||
		    fp->f_file < 0 || fstat(fp->f_file, &st) ||
		    (st.st_mode & S_IFMT) != S_IFREG)
		{
			fp->f_flags |= SMNPT;
			goto dumb;
		}
		fp->f_blksize = st.st_blksize;
		fp->f_flags |= SMOPT;
	}

	/*
	**  We are reading; we can try to optimise.
	**  Figure out where we are going and where we are now.
	*/

	if (whence == SM_IO_SEEK_SET)
		target = offset;
	else
	{
		if (fstat(fp->f_file, &st))
			goto dumb;
		target = st.st_size + offset;
	}

	if (!havepos)
	{
		if (fp->f_flags & SMOFF)
			curoff = fp->f_lseekoff;
		else
		{
			curoff = (*seekfn)(fp, (off_t) 0, SM_IO_SEEK_CUR);
			if (curoff == POS_ERR)
				goto dumb;
		}
		curoff -= fp->f_r;
		if (HASUB(fp))
			curoff -= fp->f_ur;
	}

	/*
	**  Compute the number of bytes in the input buffer (pretending
	**  that any ungetc() input has been discarded).  Adjust current
	**  offset backwards by this count so that it represents the
	**  file offset for the first byte in the current input buffer.
	*/

	if (HASUB(fp))
	{
		curoff += fp->f_r;	/* kill off ungetc */
		n = fp->f_up - fp->f_bf.smb_base;
		curoff -= n;
		n += fp->f_ur;
	}
	else
	{
		n = fp->f_p - fp->f_bf.smb_base;
		curoff -= n;
		n += fp->f_r;
	}

	/*
	**  If the target offset is within the current buffer,
	**  simply adjust the pointers, clear SMFEOF, undo ungetc(),
	**  and return.  (If the buffer was modified, we have to
	**  skip this; see getln in fget.c.)
	*/

	if (target >= curoff && target < curoff + (off_t) n)
	{
		register int o = target - curoff;

		fp->f_p = fp->f_bf.smb_base + o;
		fp->f_r = n - o;
		if (HASUB(fp))
			FREEUB(fp);
		fp->f_flags &= ~SMFEOF;
		ret = 0;
		goto clean;
	}

	/*
	**  The place we want to get to is not within the current buffer,
	**  but we can still be kind to the kernel copyout mechanism.
	**  By aligning the file offset to a block boundary, we can let
	**  the kernel use the VM hardware to map pages instead of
	**  copying bytes laboriously.  Using a block boundary also
	**  ensures that we only read one block, rather than two.
	*/

	curoff = target & ~(fp->f_blksize - 1);
	if ((*seekfn)(fp, curoff, SM_IO_SEEK_SET) == POS_ERR)
		goto dumb;
	fp->f_r = 0;
	fp->f_p = fp->f_bf.smb_base;
	if (HASUB(fp))
		FREEUB(fp);
	fp->f_flags &= ~SMFEOF;
	n = target - curoff;
	if (n)
	{
		/* Note: SM_TIME_FOREVER since fn timeout already set */
		if (sm_refill(fp, SM_TIME_FOREVER) || fp->f_r < (int) n)
			goto dumb;
		fp->f_p += n;
		fp->f_r -= n;
	}

	ret = 0;
clean:
	/*  We're back. So undo our timeout and handler */
	if (evt != NULL)
		sm_clrevent(evt);
	return ret;
dumb:
	/*
	**  We get here if we cannot optimise the seek ... just
	**  do it.  Allow the seek function to change fp->f_bf.smb_base.
	*/

	/* Note: SM_TIME_FOREVER since fn timeout already set */
	ret = SM_TIME_FOREVER;
	if (sm_flush(fp, &ret) != 0 ||
	    (*seekfn)(fp, (off_t) offset, whence) == POS_ERR)
	{
		ret = -1;
		goto clean;
	}

	/* success: clear SMFEOF indicator and discard ungetc() data */
	if (HASUB(fp))
		FREEUB(fp);
	fp->f_p = fp->f_bf.smb_base;
	fp->f_r = 0;
	fp->f_flags &= ~SMFEOF;
	ret = 0;
	goto clean;
}
