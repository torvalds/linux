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
SM_RCSID("@(#)$Id: fopen.c,v 1.63 2013-11-22 20:51:42 ca Exp $")
#include <errno.h>
#include <setjmp.h>
#include <sm/time.h>
#include <sm/heap.h>
#include <sm/signal.h>
#include <sm/assert.h>
#include <sm/io.h>
#include <sm/clock.h>
#include "local.h"

static void	openalrm __P((int));
static void	reopenalrm __P((int));
extern int      sm_io_fclose __P((SM_FILE_T *));

static jmp_buf OpenTimeOut, ReopenTimeOut;

/*
**  OPENALRM -- handler when timeout activated for sm_io_open()
**
**  Returns flow of control to where setjmp(OpenTimeOut) was set.
**
**	Parameters:
**		sig -- unused
**
**	Returns:
**		does not return
**
**	Side Effects:
**		returns flow of control to setjmp(OpenTimeOut).
**
**	NOTE:	THIS CAN BE CALLED FROM A SIGNAL HANDLER.  DO NOT ADD
**		ANYTHING TO THIS ROUTINE UNLESS YOU KNOW WHAT YOU ARE
**		DOING.
*/

/* ARGSUSED0 */
static void
openalrm(sig)
	int sig;
{
	longjmp(OpenTimeOut, 1);
}
/*
**  REOPENALRM -- handler when timeout activated for sm_io_reopen()
**
**  Returns flow of control to where setjmp(ReopenTimeOut) was set.
**
**	Parameters:
**		sig -- unused
**
**	Returns:
**		does not return
**
**	Side Effects:
**		returns flow of control to setjmp(ReopenTimeOut).
**
**	NOTE:	THIS CAN BE CALLED FROM A SIGNAL HANDLER.  DO NOT ADD
**		ANYTHING TO THIS ROUTINE UNLESS YOU KNOW WHAT YOU ARE
**		DOING.
*/

/* ARGSUSED0 */
static void
reopenalrm(sig)
	int sig;
{
	longjmp(ReopenTimeOut, 1);
}

/*
**  SM_IO_OPEN -- open a file of a specific type
**
**	Parameters:
**		type -- type of file to open
**		timeout -- time to complete the open
**		info -- info describing what is to be opened (type dependant)
**		flags -- user selected flags
**		rpool -- pointer to rpool to be used for this open
**
**	Returns:
**		Raises exception on heap exhaustion.
**		Aborts if type is invalid.
**		Returns NULL and sets errno
**			- when the type specific open fails
**			- when open vector errors
**			- when flags not set or invalid
**		Success returns a file pointer to the opened file type.
*/

SM_FILE_T *
sm_io_open(type, timeout, info, flags, rpool)
	const SM_FILE_T *type;
	int SM_NONVOLATILE timeout;	/* this is not the file type timeout */
	const void *info;
	int flags;
	const void *rpool;
{
	register SM_FILE_T *fp;
	int ioflags;
	SM_EVENT *evt = NULL;

	ioflags = sm_flags(flags);

	if (ioflags == 0)
	{
		/* must give some indication/intent */
		errno = EINVAL;
		return NULL;
	}

	if (timeout == SM_TIME_DEFAULT)
		timeout = SM_TIME_FOREVER;
	if (timeout == SM_TIME_IMMEDIATE)
	{
		errno = EAGAIN;
		return NULL;
	}

	fp = sm_fp(type, ioflags, NULL);

	/*  Okay, this is where we set the timeout.  */
	if (timeout != SM_TIME_FOREVER)
	{
		if (setjmp(OpenTimeOut) != 0)
		{
			errno = EAGAIN;
			return NULL;
		}
		evt = sm_seteventm(timeout, openalrm, 0);
	}

	if ((*fp->f_open)(fp, info, flags, rpool) < 0)
	{
		fp->f_flags = 0;	/* release */
		fp->sm_magic = NULL;	/* release */
		return NULL;
	}

	/*  We're back. So undo our timeout and handler */
	if (evt != NULL)
		sm_clrevent(evt);

#if SM_RPOOL
	if (rpool != NULL)
		sm_rpool_attach_x(rpool, sm_io_fclose, fp);
#endif /* SM_RPOOL */

	return fp;
}
/*
**  SM_IO_DUP -- duplicate a file pointer
**
**	Parameters:
**		fp -- file pointer to duplicate
**
**	Returns:
**		Success - the duplicated file pointer
**		Failure - NULL (was an invalid file pointer or too many open)
**
**	Increments the duplicate counter (dup_cnt) for the open file pointer.
**	The counter counts the number of duplicates. When the duplicate
**	counter is 0 (zero) then the file pointer is the only one left
**	(no duplicates, it is the only one).
*/

SM_FILE_T *
sm_io_dup(fp)
	SM_FILE_T *fp;
{

	SM_REQUIRE_ISA(fp, SmFileMagic);
	if (fp->sm_magic != SmFileMagic)
	{
		errno = EBADF;
		return NULL;
	}
	if (fp->f_dup_cnt >= INT_MAX - 1)
	{
		/* Can't let f_dup_cnt wrap! */
		errno = EMFILE;
		return NULL;
	}
	fp->f_dup_cnt++;
	return fp;
}
/*
**  SM_IO_REOPEN -- open a new file using the old file pointer
**
**	Parameters:
**		type -- file type to be opened
**		timeout -- time to complete the reopen
**		info -- infomation about what is to be "re-opened" (type dep.)
**		flags -- user flags to map to internal flags
**		rpool -- rpool file to be associated with
**		fp -- the file pointer to reuse
**
**	Returns:
**		Raises an exception on heap exhaustion.
**		Aborts if type is invalid.
**		Failure: returns NULL
**		Success: returns "reopened" file pointer
*/

SM_FILE_T *
sm_io_reopen(type, timeout, info, flags, rpool, fp)
	const SM_FILE_T *type;
	int SM_NONVOLATILE timeout;
	const void *info;
	int flags;
	const void *rpool;
	SM_FILE_T *fp;
{
	int ioflags, ret;
	SM_FILE_T *fp2;
	SM_EVENT *evt = NULL;

	if ((ioflags = sm_flags(flags)) == 0)
	{
		(void) sm_io_close(fp, timeout);
		return NULL;
	}

	if (!Sm_IO_DidInit)
		sm_init();

	if (timeout == SM_TIME_DEFAULT)
		timeout = SM_TIME_FOREVER;
	if (timeout == SM_TIME_IMMEDIATE)
	{
		/*
		**  Filling the buffer will take time and we are wanted to
		**  return immediately. So...
		*/

		errno = EAGAIN;
		return NULL;
	}
	/*  Okay, this is where we set the timeout.  */
	if (timeout != SM_TIME_FOREVER)
	{
		if (setjmp(ReopenTimeOut) != 0)
		{
			errno = EAGAIN;
			return NULL;
		}

		evt = sm_seteventm(timeout, reopenalrm, 0);
	}

	/*
	**  There are actually programs that depend on being able to "reopen"
	**  descriptors that weren't originally open.  Keep this from breaking.
	**  Remember whether the stream was open to begin with, and which file
	**  descriptor (if any) was associated with it.  If it was attached to
	**  a descriptor, defer closing it; reopen("/dev/stdin", "r", stdin)
	**  should work.  This is unnecessary if it was not a Unix file.
	*/

	if (fp != NULL)
	{
		if (fp->sm_magic != SmFileMagic)
			fp->f_flags = SMFEOF;	/* hold on to it */
		else
		{
			/* flush the stream; ANSI doesn't require this. */
			(void) sm_io_flush(fp, SM_TIME_FOREVER);
			(void) sm_io_close(fp, SM_TIME_FOREVER);
		}
	}

	fp2 = sm_fp(type, ioflags, fp);
	ret = (*fp2->f_open)(fp2, info, flags, rpool);

	/*  We're back. So undo our timeout and handler */
	if (evt != NULL)
		sm_clrevent(evt);

	if (ret < 0)
	{
		fp2->f_flags = 0;	/* release */
		fp2->sm_magic = NULL;	/* release */
		return NULL;
	}

	/*
	**  We're not preserving this logic (below) for sm_io because it is now
	**  abstracted at least one "layer" away. By closing and reopening
	**  the 1st fd used should be the just released one (when Unix
	**  behavior followed). Old comment::
	**  If reopening something that was open before on a real file, try
	**  to maintain the descriptor.  Various C library routines (perror)
	**  assume stderr is always fd STDERR_FILENO, even if being reopen'd.
	*/

#if SM_RPOOL
	if (rpool != NULL)
		sm_rpool_attach_x(rpool, sm_io_close, fp2);
#endif /* SM_RPOOL */

	return fp2;
}
/*
**  SM_IO_AUTOFLUSH -- link another file to this for auto-flushing
**
**	When a read occurs on fp, fp2 will be flushed iff there is no
**	data waiting on fp.
**
**	Parameters:
**		fp -- the file opened for reading.
**		fp2 -- the file opened for writing.
**
**	Returns:
**		The old flush file pointer.
*/

SM_FILE_T *
sm_io_autoflush(fp, fp2)
	SM_FILE_T *fp;
	SM_FILE_T *fp2;
{
	SM_FILE_T *savefp;

	SM_REQUIRE_ISA(fp, SmFileMagic);
	if (fp2 != NULL)
		SM_REQUIRE_ISA(fp2, SmFileMagic);

	savefp = fp->f_flushfp;
	fp->f_flushfp = fp2;
	return savefp;
}
/*
**  SM_IO_AUTOMODE -- link another file to this for auto-moding
**
**	When the mode (blocking or non-blocking) changes for fp1 then
**	update fp2's mode at the same time. This is to be used when
**	a system dup() has generated a second file descriptor for
**	another sm_io_open() by file descriptor. The modes have been
**	linked in the system and this formalizes it for sm_io internally.
**
**	Parameters:
**		fp1 -- the first file
**		fp2 -- the second file
**
**	Returns:
**		nothing
*/

void
sm_io_automode(fp1, fp2)
	SM_FILE_T *fp1;
	SM_FILE_T *fp2;
{
	SM_REQUIRE_ISA(fp1, SmFileMagic);
	SM_REQUIRE_ISA(fp2, SmFileMagic);

	fp1->f_modefp = fp2;
	fp2->f_modefp = fp1;
}
