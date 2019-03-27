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
SM_RCSID("@(#)$Id: fvwrite.c,v 1.50 2013-11-22 20:51:42 ca Exp $")
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sm/io.h>
#include <sm/setjmp.h>
#include <sm/conf.h>
#include "local.h"
#include "fvwrite.h"

/*
**  SM_FVWRITE -- write memory regions and buffer for file pointer
**
**	Parameters:
**		fp -- the file pointer to write to
**		timeout -- time length for function to return by
**		uio -- the memory regions to write
**
**	Returns:
**		Failure: returns SM_IO_EOF and sets errno
**		Success: returns 0 (zero)
**
**	This routine is large and unsightly, but most of the ugliness due
**	to the different kinds of output buffering handled here.
*/

#define COPY(n)	  (void)memcpy((void *)fp->f_p, (void *)p, (size_t)(n))
#define GETIOV(extra_work)		\
	while (len == 0)		\
	{				\
		extra_work;		\
		p = iov->iov_base;	\
		len = iov->iov_len;	\
		iov++;			\
	}

int
sm_fvwrite(fp, timeout, uio)
	register SM_FILE_T *fp;
	int timeout;
	register struct sm_uio *uio;
{
	register size_t len;
	register char *p;
	register struct sm_iov *iov;
	register int w, s;
	char *nl;
	int nlknown, nldist;
	int fd;
	struct timeval to;

	if (uio->uio_resid == 0)
		return 0;

	/* make sure we can write */
	if (cantwrite(fp))
	{
		errno = EBADF;
		return SM_IO_EOF;
	}

	SM_CONVERT_TIME(fp, fd, timeout, &to);

	iov = uio->uio_iov;
	p = iov->iov_base;
	len = iov->iov_len;
	iov++;
	if (fp->f_flags & SMNBF)
	{
		/* Unbuffered: write up to BUFSIZ bytes at a time. */
		do
		{
			GETIOV(;);
			errno = 0; /* needed to ensure EOF correctly found */
			w = (*fp->f_write)(fp, p, SM_MIN(len, SM_IO_BUFSIZ));
			if (w <= 0)
			{
				if (w == 0 && errno == 0)
					break; /* EOF found */
				if (IS_IO_ERROR(fd, w, timeout))
					goto err; /* errno set */

				/* write would block */
				SM_IO_WR_TIMEOUT(fp, fd, timeout);
				w = 0;
			}
			else
			{
				p += w;
				len -= w;
			}
		} while ((uio->uio_resid -= w) != 0);
	}
	else if ((fp->f_flags & SMLBF) == 0)
	{
		/*
		**  Not SMLBF (line-buffered). Either SMFBF or SMNOW
		**  buffered: fill partially full buffer, if any,
		**  and then flush.  If there is no partial buffer, write
		**  one bf._size byte chunk directly (without copying).
		**
		**  String output is a special case: write as many bytes
		**  as fit, but pretend we wrote everything.  This makes
		**  snprintf() return the number of bytes needed, rather
		**  than the number used, and avoids its write function
		**  (so that the write function can be invalid).
		*/

		do
		{
			GETIOV(;);
			if ((((fp->f_flags & (SMALC | SMSTR)) == (SMALC | SMSTR))
			    || ((fp->f_flags & SMNOW) != 0))
			    && (size_t) fp->f_w < len)
			{
				size_t blen = fp->f_p - fp->f_bf.smb_base;
				unsigned char *tbase;
				int tsize;

				/* Allocate space exponentially. */
				tsize = fp->f_bf.smb_size;
				do
				{
					tsize = (tsize << 1) + 1;
				} while ((size_t) tsize < blen + len);
				tbase = (unsigned char *) sm_realloc(fp->f_bf.smb_base,
								     tsize + 1);
				if (tbase == NULL)
				{
					errno = ENOMEM;
					goto err; /* errno set */
				}
				fp->f_w += tsize - fp->f_bf.smb_size;
				fp->f_bf.smb_base = tbase;
				fp->f_bf.smb_size = tsize;
				fp->f_p = tbase + blen;
			}
			w = fp->f_w;
			errno = 0; /* needed to ensure EOF correctly found */
			if (fp->f_flags & SMSTR)
			{
				if (len < (size_t) w)
					w = len;
				COPY(w);	/* copy SM_MIN(fp->f_w,len), */
				fp->f_w -= w;
				fp->f_p += w;
				w = len;	/* but pretend copied all */
			}
			else if (fp->f_p > fp->f_bf.smb_base
				 && len > (size_t) w)
			{
				/* fill and flush */
				COPY(w);
				fp->f_p += w;
				if (sm_flush(fp, &timeout))
					goto err; /* errno set */
			}
			else if (len >= (size_t) (w = fp->f_bf.smb_size))
			{
				/* write directly */
				w = (*fp->f_write)(fp, p, w);
				if (w <= 0)
				{
					if (w == 0 && errno == 0)
						break; /* EOF found */
					if (IS_IO_ERROR(fd, w, timeout))
						goto err; /* errno set */

					/* write would block */
					SM_IO_WR_TIMEOUT(fp, fd, timeout);
					w = 0;
				}
			}
			else
			{
				/* fill and done */
				w = len;
				COPY(w);
				fp->f_w -= w;
				fp->f_p += w;
			}
			p += w;
			len -= w;
		} while ((uio->uio_resid -= w) != 0);

		if ((fp->f_flags & SMNOW) != 0 && sm_flush(fp, &timeout))
			goto err; /* errno set */
	}
	else
	{
		/*
		**  Line buffered: like fully buffered, but we
		**  must check for newlines.  Compute the distance
		**  to the first newline (including the newline),
		**  or `infinity' if there is none, then pretend
		**  that the amount to write is SM_MIN(len,nldist).
		*/

		nlknown = 0;
		nldist = 0;	/* XXX just to keep gcc happy */
		do
		{
			GETIOV(nlknown = 0);
			if (!nlknown)
			{
				nl = memchr((void *)p, '\n', len);
				nldist = nl != NULL ? nl + 1 - p : len + 1;
				nlknown = 1;
			}
			s = SM_MIN(len, ((size_t) nldist));
			w = fp->f_w + fp->f_bf.smb_size;
			errno = 0; /* needed to ensure EOF correctly found */
			if (fp->f_p > fp->f_bf.smb_base && s > w)
			{
				COPY(w);
				/* fp->f_w -= w; */
				fp->f_p += w;
				if (sm_flush(fp, &timeout))
					goto err; /* errno set */
			}
			else if (s >= (w = fp->f_bf.smb_size))
			{
				w = (*fp->f_write)(fp, p, w);
				if (w <= 0)
				{
					if (w == 0 && errno == 0)
						break; /* EOF found */
					if (IS_IO_ERROR(fd, w, timeout))
						goto err; /* errno set */

					/* write would block */
					SM_IO_WR_TIMEOUT(fp, fd, timeout);
					w = 0;
				}
			}
			else
			{
				w = s;
				COPY(w);
				fp->f_w -= w;
				fp->f_p += w;
			}
			if ((nldist -= w) == 0)
			{
				/* copied the newline: flush and forget */
				if (sm_flush(fp, &timeout))
					goto err; /* errno set */
				nlknown = 0;
			}
			p += w;
			len -= w;
		} while ((uio->uio_resid -= w) != 0);
	}

	return 0;

err:
	/* errno set before goto places us here */
	fp->f_flags |= SMERR;
	return SM_IO_EOF;
}
