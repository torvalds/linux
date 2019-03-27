/*
 * xsbprintf.c - string buffer formatting helpers
 *
 * Written by Juergen Perlinger (perlinger@ntp.org) for the NTP project.
 * The contents of 'html/copyright.html' apply.
 */

#include <config.h>
#include <sys/types.h>

#include "ntp_stdlib.h"

/* eXtended Varlist String Buffer printf
 *
 * Formats via 'vsnprintf' into a string buffer, with some semantic
 * specialties:
 *
 * - The start of the buffer pointer is updated according to the number
 *   of characters written.
 * - If the buffer is insufficient to format the number of charactes,
 *   the partial result will be be discarded, and zero is returned to
 *   indicate nothing was written to the buffer.
 * - On successful formatting, the return code is the return value of
 *   the inner call to 'vsnprintf()'.
 * - If there is any error, the state of the buffer will not be
 *   changed. (Bytes in the buffer might be smashed, but the buffer
 *   position does not change, and the NUL marker stays in place at the
 *   current buffer position.)
 * - If '(*ppbuf - pend) <= 0' (or ppbuf is NULL), fail with EINVAL.
 */
int
xvsbprintf(
	char       **ppbuf,	/* pointer to buffer pointer (I/O) */
	char * const pend,	/* buffer end (I)		   */
	char const  *pfmt,	/* printf-like format string       */
	va_list      va		/* formatting args for above       */
	)
{
	char *pbuf = (ppbuf) ? *ppbuf : NULL;
	int   rc   = -1;
	if (pbuf && (pend - pbuf > 0)) {
		size_t blen = (size_t)(pend - pbuf);
		rc = vsnprintf(pbuf, blen, pfmt, va);
		if (rc > 0) {
		    if ((size_t)rc >= blen)
			rc = 0;
		    pbuf += rc;
		}
		*pbuf = '\0'; /* fear of bad vsnprintf */
		*ppbuf = pbuf;
	} else {
		errno = EINVAL;
	}
	return rc;
}

/* variadic wrapper around the buffer string formatter */
int
xsbprintf(
	char       **ppbuf,	/* pointer to buffer pointer (I/O) */
	char * const pend,	/* buffer end (I)		   */
	char const  *pfmt,	/* printf-like format string       */
	...			/* formatting args for above       */
	)
{
	va_list va;
	int     rc;
	
	va_start(va, pfmt);
	rc = xvsbprintf(ppbuf, pend, pfmt, va);
	va_end(va);
	return rc;
}

/* that's all folks! */
