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
SM_RCSID("@(#)$Id: makebuf.c,v 1.27 2013-11-22 20:51:43 ca Exp $")
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sm/io.h>
#include <sm/heap.h>
#include <sm/conf.h>
#include "local.h"

/*
**  SM_MAKEBUF -- make a buffer for the file
**
**	Parameters:
**		fp -- the file to be buffered
**
**	Returns:
**		nothing
**
**	Allocate a file buffer, or switch to unbuffered I/O.
**	By default tty devices default to line buffered.
*/

void
sm_makebuf(fp)
	register SM_FILE_T *fp;
{
	register void *p;
	register int flags;
	size_t size;
	int couldbetty;

	if (fp->f_flags & SMNBF)
	{
		fp->f_bf.smb_base = fp->f_p = fp->f_nbuf;
		fp->f_bf.smb_size = 1;
		return;
	}
	flags = sm_whatbuf(fp, &size, &couldbetty);
	if ((p = sm_malloc(size)) == NULL)
	{
		fp->f_flags |= SMNBF;
		fp->f_bf.smb_base = fp->f_p = fp->f_nbuf;
		fp->f_bf.smb_size = 1;
		return;
	}
	if (!Sm_IO_DidInit)
		sm_init();
	flags |= SMMBF;
	fp->f_bf.smb_base = fp->f_p = p;
	fp->f_bf.smb_size = size;
	if (couldbetty && isatty(fp->f_file))
		flags |= SMLBF;
	fp->f_flags |= flags;
}

/*
**  SM_WHATBUF -- determine proper buffer for a file (internal)
**
**  Plus it fills in 'bufsize' for recommended buffer size and
**  fills in flag to indicate if 'fp' could be a tty (nothing
**  to do with "betty" :-) ).
**
**	Parameters:
**		fp -- file pointer to be buffered
**		bufsize -- new buffer size (a return)
**		couldbetty -- could be a tty (returns)
**
**	Returns:
**		Success:
**		on error:
**			SMNPT -- not seek opimized
**			SMOPT -- seek opimized
*/

int
sm_whatbuf(fp, bufsize, couldbetty)
	register SM_FILE_T *fp;
	size_t *bufsize;
	int *couldbetty;
{
	struct stat st;

	if (fp->f_file < 0 || fstat(fp->f_file, &st) < 0)
	{
		*couldbetty = 0;
		*bufsize = SM_IO_BUFSIZ;
		return SMNPT;
	}

	/* could be a tty iff it is a character device */
	*couldbetty = S_ISCHR(st.st_mode);
	if (st.st_blksize == 0)
	{
		*bufsize = SM_IO_BUFSIZ;
		return SMNPT;
	}

#if SM_IO_MAX_BUF_FILE > 0
	if (S_ISREG(st.st_mode) && st.st_blksize > SM_IO_MAX_BUF_FILE)
		st.st_blksize = SM_IO_MAX_BUF_FILE;
#endif /* SM_IO_MAX_BUF_FILE > 0 */

#if SM_IO_MAX_BUF > 0 || SM_IO_MIN_BUF > 0
	if (!S_ISREG(st.st_mode))
	{
# if SM_IO_MAX_BUF > 0
		if (st.st_blksize > SM_IO_MAX_BUF)
			st.st_blksize = SM_IO_MAX_BUF;
#  if SM_IO_MIN_BUF > 0
		else
#  endif /* SM_IO_MIN_BUF > 0 */
# endif /* SM_IO_MAX_BUF > 0 */
# if SM_IO_MIN_BUF > 0
		if (st.st_blksize < SM_IO_MIN_BUF)
			st.st_blksize = SM_IO_MIN_BUF;
# endif /* SM_IO_MIN_BUF > 0 */
	}
#endif /* SM_IO_MAX_BUF > 0 || SM_IO_MIN_BUF > 0 */

	/*
	**  Optimise fseek() only if it is a regular file.  (The test for
	**  sm_std_seek is mainly paranoia.)  It is safe to set _blksize
	**  unconditionally; it will only be used if SMOPT is also set.
	*/

	if ((fp->f_flags & SMSTR) == 0)
	{
		*bufsize = st.st_blksize;
		fp->f_blksize = st.st_blksize;
	}
	else
		*bufsize = SM_IO_BUFSIZ;
	if ((st.st_mode & S_IFMT) == S_IFREG &&
	    fp->f_seek == sm_stdseek)
		return SMOPT;
	else
		return SMNPT;
}
