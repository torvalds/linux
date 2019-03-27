/*
 * emalloc - return new memory obtained from the system.  Belch if none.
 */
#include <config.h>
#include "ntp_types.h"
#include "ntp_malloc.h"
#include "ntp_syslog.h"
#include "ntp_stdlib.h"


/*
 * When using the debug MS CRT allocator, each allocation stores the
 * callsite __FILE__ and __LINE__, which is then displayed at process
 * termination, to track down leaks.  We don't want all of our
 * allocations to show up as coming from emalloc.c, so we preserve the
 * original callsite's source file and line using macros which pass
 * __FILE__ and __LINE__ as parameters to these routines.
 * Other debug malloc implementations can be used by defining
 * EREALLOC_IMPL() as ports/winnt/include/config.h does.
 */

void *
ereallocz(
	void *	ptr,
	size_t	newsz,
	size_t	priorsz,
	int	zero_init
#ifdef EREALLOC_CALLSITE		/* ntp_malloc.h */
			 ,
	const char *	file,
	int		line
#endif
	)
{
	char *	mem;
	size_t	allocsz;

	if (0 == newsz)
		allocsz = 1;
	else
		allocsz = newsz;

	mem = EREALLOC_IMPL(ptr, allocsz, file, line);
	if (NULL == mem) {
		msyslog_term = TRUE;
#ifndef EREALLOC_CALLSITE
		msyslog(LOG_ERR, "fatal out of memory (%lu bytes)",
			(u_long)newsz);
#else
		msyslog(LOG_ERR,
			"fatal out of memory %s line %d (%lu bytes)",
			file, line, (u_long)newsz);
#endif
		exit(1);
	}

	if (zero_init && newsz > priorsz)
		zero_mem(mem + priorsz, newsz - priorsz);

	return mem;
}

/* oreallocarray.c is licensed under the following:
 * Copyright (c) 2008 Otto Moerbeek <otto@drijf.net>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * This is sqrt(SIZE_MAX+1), as s1*s2 <= SIZE_MAX
 * if both s1 < MUL_NO_OVERFLOW and s2 < MUL_NO_OVERFLOW
 */
#define MUL_NO_OVERFLOW	((size_t)1 << (sizeof(size_t) * 4))

void *
oreallocarrayxz(
	void *optr,
	size_t nmemb,
	size_t size,
	size_t extra
#ifdef EREALLOC_CALLSITE		/* ntp_malloc.h */
	,
	const char *	file,
	int		line
#endif
	)
{
	if ((nmemb >= MUL_NO_OVERFLOW || size >= MUL_NO_OVERFLOW) &&
	    nmemb > 0 && SIZE_MAX / nmemb < size) {
#ifndef EREALLOC_CALLSITE
		msyslog(LOG_ERR, "fatal allocation size overflow");
#else
		msyslog(LOG_ERR,
			"fatal allocation size overflow %s line %d",
			file, line);
#endif
		exit(1);
	}
#ifndef EREALLOC_CALLSITE
	return ereallocz(optr, extra + (size * nmemb), 0, TRUE);
#else
	return ereallocz(optr, extra + (size * nmemb), 0, TRUE, file, line);
#endif
}

char *
estrdup_impl(
	const char *	str
#ifdef EREALLOC_CALLSITE
			   ,
	const char *	file,
	int		line
#endif
	)
{
	char *	copy;
	size_t	bytes;

	bytes = strlen(str) + 1;
	copy = ereallocz(NULL, bytes, 0, FALSE
#ifdef EREALLOC_CALLSITE
			 , file, line
#endif
			 );
	memcpy(copy, str, bytes);

	return copy;
}


#if 0
#ifndef EREALLOC_CALLSITE
void *
emalloc(size_t newsz)
{
	return ereallocz(NULL, newsz, 0, FALSE);
}
#endif
#endif

