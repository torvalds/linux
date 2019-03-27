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
SM_IDSTR(id, "@(#)$Id: ungetc.c,v 1.31 2013-11-22 20:51:44 ca Exp $")

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sm/time.h>
#include <errno.h>
#include <sm/io.h>
#include <sm/heap.h>
#include <sm/assert.h>
#include <sm/conf.h>
#include "local.h"

static void	sm_submore_x __P((SM_FILE_T *));

/*
**  SM_SUBMORE_X -- expand ungetc buffer
**
**  Expand the ungetc buffer `in place'.  That is, adjust fp->f_p when
**  the buffer moves, so that it points the same distance from the end,
**  and move the bytes in the buffer around as necessary so that they
**  are all at the end (stack-style).
**
**	Parameters:
**		fp -- the file pointer
**
**	Results:
**		none.
**
**	Exceptions:
**		F:sm_heap -- out of memory
*/

static void
sm_submore_x(fp)
	SM_FILE_T *fp;
{
	register int i;
	register unsigned char *p;

	if (fp->f_ub.smb_base == fp->f_ubuf)
	{
		/* Get a buffer; f_ubuf is fixed size. */
		p = sm_malloc_x((size_t) SM_IO_BUFSIZ);
		fp->f_ub.smb_base = p;
		fp->f_ub.smb_size = SM_IO_BUFSIZ;
		p += SM_IO_BUFSIZ - sizeof(fp->f_ubuf);
		for (i = sizeof(fp->f_ubuf); --i >= 0;)
			p[i] = fp->f_ubuf[i];
		fp->f_p = p;
		return;
	}
	i = fp->f_ub.smb_size;
	p = sm_realloc_x(fp->f_ub.smb_base, i << 1);

	/* no overlap (hence can use memcpy) because we doubled the size */
	(void) memcpy((void *) (p + i), (void *) p, (size_t) i);
	fp->f_p = p + i;
	fp->f_ub.smb_base = p;
	fp->f_ub.smb_size = i << 1;
}

/*
**  SM_IO_UNGETC -- place a character back into the buffer just read
**
**	Parameters:
**		fp -- the file pointer affected
**		timeout -- time to complete ungetc
**		c -- the character to place back
**
**	Results:
**		On success, returns value of character placed back, 0-255.
**		Returns SM_IO_EOF if c == SM_IO_EOF or if last operation
**		was a write and flush failed.
**
**	Exceptions:
**		F:sm_heap -- out of memory
*/

int
sm_io_ungetc(fp, timeout, c)
	register SM_FILE_T *fp;
	int timeout;
	int c;
{
	SM_REQUIRE_ISA(fp, SmFileMagic);
	if (c == SM_IO_EOF)
		return SM_IO_EOF;
	if (timeout == SM_TIME_IMMEDIATE)
	{
		/*
		**  Ungetting the buffer will take time and we are wanted to
		**  return immediately. So...
		*/

		errno = EAGAIN;
		return SM_IO_EOF;
	}

	if (!Sm_IO_DidInit)
		sm_init();
	if ((fp->f_flags & SMRD) == 0)
	{
		/*
		**  Not already reading: no good unless reading-and-writing.
		**  Otherwise, flush any current write stuff.
		*/

		if ((fp->f_flags & SMRW) == 0)
			return SM_IO_EOF;
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
	c = (unsigned char) c;

	/*
	**  If we are in the middle of ungetc'ing, just continue.
	**  This may require expanding the current ungetc buffer.
	*/

	if (HASUB(fp))
	{
		if (fp->f_r >= fp->f_ub.smb_size)
			sm_submore_x(fp);
		*--fp->f_p = c;
		fp->f_r++;
		return c;
	}
	fp->f_flags &= ~SMFEOF;

	/*
	**  If we can handle this by simply backing up, do so,
	**  but never replace the original character.
	**  (This makes sscanf() work when scanning `const' data.)
	*/

	if (fp->f_bf.smb_base != NULL && fp->f_p > fp->f_bf.smb_base &&
	    fp->f_p[-1] == c)
	{
		fp->f_p--;
		fp->f_r++;
		return c;
	}

	/*
	**  Create an ungetc buffer.
	**  Initially, we will use the `reserve' buffer.
	*/

	fp->f_ur = fp->f_r;
	fp->f_up = fp->f_p;
	fp->f_ub.smb_base = fp->f_ubuf;
	fp->f_ub.smb_size = sizeof(fp->f_ubuf);
	fp->f_ubuf[sizeof(fp->f_ubuf) - 1] = c;
	fp->f_p = &fp->f_ubuf[sizeof(fp->f_ubuf) - 1];
	fp->f_r = 1;

	return c;
}
