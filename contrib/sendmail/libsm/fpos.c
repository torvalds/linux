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
SM_RCSID("@(#)$Id: fpos.c,v 1.40 2013-11-22 20:51:42 ca Exp $")
#include <errno.h>
#include <setjmp.h>
#include <sm/time.h>
#include <sm/heap.h>
#include <sm/signal.h>
#include <sm/clock.h>
#include <sm/io.h>
#include <sm/assert.h>
#include "local.h"

static void	tellalrm __P((int));
static jmp_buf TellTimeOut;

/*
**  TELLALRM -- handler when timeout activated for sm_io_tell()
**
**  Returns flow of control to where setjmp(TellTimeOut) was set.
**
**	Parameters:
**		sig -- unused
**
**	Returns:
**		does not return
**
**	Side Effects:
**		returns flow of control to setjmp(TellTimeOut).
**
**	NOTE:	THIS CAN BE CALLED FROM A SIGNAL HANDLER.  DO NOT ADD
**		ANYTHING TO THIS ROUTINE UNLESS YOU KNOW WHAT YOU ARE
**		DOING.
*/

/* ARGSUSED0 */
static void
tellalrm(sig)
	int sig;
{
	longjmp(TellTimeOut, 1);
}

/*
**  SM_IO_TELL -- position the file pointer
**
**	Paramters:
**		fp -- the file pointer to get repositioned
**		timeout -- time to complete the tell (milliseconds)
**
**	Returns:
**		Success -- the repositioned location.
**		Failure -- -1 (minus 1) and sets errno
*/

long
sm_io_tell(fp, timeout)
	register SM_FILE_T *fp;
	int SM_NONVOLATILE timeout;
{
	register off_t pos;
	SM_EVENT *evt = NULL;

	SM_REQUIRE_ISA(fp, SmFileMagic);
	if (fp->f_seek == NULL)
	{
		errno = ESPIPE;			/* historic practice */
		return -1L;
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
		return -1L;
	}

	/*
	**  Find offset of underlying I/O object, then adjust byte position
	**  may adjust seek offset on append stream
	*/

	(void) sm_flush(fp, (int *) &timeout);

	/* This is where we start the timeout */
	if (timeout != SM_TIME_FOREVER)
	{
		if (setjmp(TellTimeOut) != 0)
		{
			errno = EAGAIN;
			return -1L;
		}

		evt = sm_seteventm(timeout, tellalrm, 0);
	}

	if (fp->f_flags & SMOFF)
		pos = fp->f_lseekoff;
	else
	{
		/* XXX only set the timeout here? */
		pos = (*fp->f_seek)(fp, (off_t) 0, SM_IO_SEEK_CUR);
		if (pos == -1L)
			goto clean;
	}
	if (fp->f_flags & SMRD)
	{
		/*
		**  Reading.  Any unread characters (including
		**  those from ungetc) cause the position to be
		**  smaller than that in the underlying object.
		*/

		pos -= fp->f_r;
		if (HASUB(fp))
			pos -= fp->f_ur;
	}
	else if (fp->f_flags & SMWR && fp->f_p != NULL)
	{
		/*
		**  Writing.  Any buffered characters cause the
		**  position to be greater than that in the
		**  underlying object.
		*/

		pos += fp->f_p - fp->f_bf.smb_base;
	}

clean:
	/*  We're back. So undo our timeout and handler */
	if (evt != NULL)
		sm_clrevent(evt);
	return pos;
}
