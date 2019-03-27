/*
 * Copyright (c) 2000-2001, 2013 Proofpoint, Inc. and its suppliers.
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
SM_RCSID("@(#)$Id: fget.c,v 1.26 2013-11-22 20:51:42 ca Exp $")
#include <stdlib.h>
#include <string.h>
#include <sm/io.h>
#include <sm/assert.h>
#include "local.h"

/*
**  SM_IO_FGETS -- get a string from a file
**
**  Read at most n-1 characters from the given file.
**  Stop when a newline has been read, or the count ('n') runs out.
**
**	Parameters:
**		fp -- the file to read from
**		timeout -- time to complete reading the string in milliseconds
**		buf -- buffer to place read string in
**		n -- size of 'buf'
**
**	Returns:
**		success: number of characters
**		failure: -1
**		timeout: -1 and errno set to EAGAIN
**
**	Side Effects:
**		may move the file pointer
*/

int
sm_io_fgets(fp, timeout, buf, n)
	register SM_FILE_T *fp;
	int timeout;
	char *buf;
	register int n;
{
	int len, r;
	char *s;
	unsigned char *p, *t;

	SM_REQUIRE_ISA(fp, SmFileMagic);
	if (n <= 0)		/* sanity check */
		return -1;

	s = buf;
	n--;			/* leave space for NUL */
	r = 0;
	while (n > 0)
	{
		/* If the buffer is empty, refill it. */
		if ((len = fp->f_r) <= 0)
		{

			/*
			**  Timeout is only passed if we can't get the data
			**  from the buffer (which is counted as immediately).
			*/

			if (sm_refill(fp, timeout) != 0)
			{
				/* EOF/error: stop with partial or no line */
				if (s == buf)
					return -1;
				break;
			}
			len = fp->f_r;
		}
		p = fp->f_p;

		/*
		**  Scan through at most n bytes of the current buffer,
		**  looking for '\n'.  If found, copy up to and including
		**  newline, and stop.  Otherwise, copy entire chunk
		**  and loop.
		*/

		if (len > n)
			len = n;
		t = (unsigned char *) memchr((void *) p, '\n', len);
		if (t != NULL)
		{
			len = ++t - p;
			r += len;
			fp->f_r -= len;
			fp->f_p = t;
			(void) memcpy((void *) s, (void *) p, len);
			s[len] = 0;
			return r;
		}
		fp->f_r -= len;
		fp->f_p += len;
		(void) memcpy((void *) s, (void *) p, len);
		s += len;
		r += len;
		n -= len;
	}
	*s = 0;
	return r;
}
