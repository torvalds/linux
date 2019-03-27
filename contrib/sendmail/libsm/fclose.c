/*
 * Copyright (c) 2000-2002, 2004 Proofpoint, Inc. and its suppliers.
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
SM_RCSID("@(#)$Id: fclose.c,v 1.45 2013-11-22 20:51:42 ca Exp $")
#include <errno.h>
#include <stdlib.h>
#include <sm/time.h>
#include <setjmp.h>
#include <sm/io.h>
#include <sm/assert.h>
#include <sm/heap.h>
#include <sm/signal.h>
#include <sm/conf.h>
#include <sm/clock.h>
#include "local.h"

static void	closealrm __P((int));
static jmp_buf CloseTimeOut;

/*
**  CLOSEALRM -- handler when timeout activated for sm_io_close()
**
**	Returns flow of control to where setjmp(CloseTimeOut) was set.
**
**	Parameters:
**		sig -- unused
**
**	Returns:
**		does not return
**
**	Side Effects:
**		returns flow of control to setjmp(CloseTimeOut).
**
**	NOTE:	THIS CAN BE CALLED FROM A SIGNAL HANDLER.  DO NOT ADD
**		ANYTHING TO THIS ROUTINE UNLESS YOU KNOW WHAT YOU ARE
**		DOING.
*/

/* ARGSUSED0 */
static void
closealrm(sig)
	int sig;
{
	longjmp(CloseTimeOut, 1);
}

/*
**  SM_IO_CLOSE -- close a file handle/pointer
**
**	Parameters:
**		fp -- file pointer to be closed
**		timeout -- maximum time allowed to perform the close (millisecs)
**
**	Returns:
**		0 on success
**		-1 on failure and sets errno
**
**	Side Effects:
**		file pointer 'fp' will no longer be valid.
*/

int
sm_io_close(fp, timeout)
	register SM_FILE_T *fp;
	int SM_NONVOLATILE timeout;
{
	register int SM_NONVOLATILE r;
	SM_EVENT *evt = NULL;

	if (fp == NULL)
	{
		errno = EBADF;
		return SM_IO_EOF;
	}

	SM_REQUIRE_ISA(fp, SmFileMagic);

	/* XXX this won't be reached if above macro is active */
	if (fp->sm_magic == NULL)
	{
		/* not open! */
		errno = EBADF;
		return SM_IO_EOF;
	}
	if (fp->f_close == NULL)
	{
		/* no close function! */
		errno = ENODEV;
		return SM_IO_EOF;
	}
	if (fp->f_dup_cnt > 0)
	{
		/* decrement file pointer open count */
		fp->f_dup_cnt--;
		return 0;
	}

	/*  Okay, this is where we set the timeout.  */
	if (timeout == SM_TIME_DEFAULT)
		timeout = fp->f_timeout;
	if (timeout == SM_TIME_IMMEDIATE)
	{
		errno = EAGAIN;
		return -1;
	}

	/* No more duplicates of file pointer. Flush buffer and close */
	r = fp->f_flags & SMWR ? sm_flush(fp, (int *) &timeout) : 0;

	/* sm_flush() has updated to.it_value for the time it's used */
	if (timeout != SM_TIME_FOREVER)
	{
		if (setjmp(CloseTimeOut) != 0)
		{
			errno = EAGAIN;
			return SM_IO_EOF;
		}
		evt = sm_seteventm(timeout, closealrm, 0);
	}
	if ((*fp->f_close)(fp) < 0)
		r = SM_IO_EOF;

	/*  We're back. So undo our timeout and handler */
	if (evt != NULL)
		sm_clrevent(evt);
	if (fp->f_flags & SMMBF)
	{
		sm_free((char *)fp->f_bf.smb_base);
		fp->f_bf.smb_base = NULL;
	}
	if (HASUB(fp))
		FREEUB(fp);
	fp->f_flags = 0;	/* clear flags */
	fp->sm_magic = NULL;	/* Release this SM_FILE_T for reuse. */
	fp->f_r = fp->f_w = 0;	/* Mess up if reaccessed. */
	return r;
}
