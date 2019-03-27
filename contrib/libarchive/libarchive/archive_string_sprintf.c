/*-
 * Copyright (c) 2003-2007 Tim Kientzle
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "archive_platform.h"
__FBSDID("$FreeBSD$");

/*
 * The use of printf()-family functions can be troublesome
 * for space-constrained applications.  In addition, correctly
 * implementing this function in terms of vsnprintf() requires
 * two calls (one to determine the size, another to format the
 * result), which in turn requires duplicating the argument list
 * using va_copy, which isn't yet universally available. <sigh>
 *
 * So, I've implemented a bare minimum of printf()-like capability
 * here.  This is only used to format error messages, so doesn't
 * require any floating-point support or field-width handling.
 */
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#include <stdio.h>

#include "archive_string.h"
#include "archive_private.h"

/*
 * Utility functions to format signed/unsigned integers and append
 * them to an archive_string.
 */
static void
append_uint(struct archive_string *as, uintmax_t d, unsigned base)
{
	static const char digits[] = "0123456789abcdef";
	if (d >= base)
		append_uint(as, d/base, base);
	archive_strappend_char(as, digits[d % base]);
}

static void
append_int(struct archive_string *as, intmax_t d, unsigned base)
{
	uintmax_t ud;

	if (d < 0) {
		archive_strappend_char(as, '-');
		ud = (d == INTMAX_MIN) ? (uintmax_t)(INTMAX_MAX) + 1 : (uintmax_t)(-d);
	} else
		ud = d;
	append_uint(as, ud, base);
}


void
archive_string_sprintf(struct archive_string *as, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	archive_string_vsprintf(as, fmt, ap);
	va_end(ap);
}

/*
 * Like 'vsprintf', but ensures the target is big enough, resizing if
 * necessary.
 */
void
archive_string_vsprintf(struct archive_string *as, const char *fmt,
    va_list ap)
{
	char long_flag;
	intmax_t s; /* Signed integer temp. */
	uintmax_t u; /* Unsigned integer temp. */
	const char *p, *p2;
	const wchar_t *pw;

	if (archive_string_ensure(as, 64) == NULL)
		__archive_errx(1, "Out of memory");

	if (fmt == NULL) {
		as->s[0] = 0;
		return;
	}

	for (p = fmt; *p != '\0'; p++) {
		const char *saved_p = p;

		if (*p != '%') {
			archive_strappend_char(as, *p);
			continue;
		}

		p++;

		long_flag = '\0';
		switch(*p) {
		case 'j':
		case 'l':
		case 'z':
			long_flag = *p;
			p++;
			break;
		}

		switch (*p) {
		case '%':
			archive_strappend_char(as, '%');
			break;
		case 'c':
			s = va_arg(ap, int);
			archive_strappend_char(as, (char)s);
			break;
		case 'd':
			switch(long_flag) {
			case 'j': s = va_arg(ap, intmax_t); break;
			case 'l': s = va_arg(ap, long); break;
			case 'z': s = va_arg(ap, ssize_t); break;
			default:  s = va_arg(ap, int); break;
			}
		        append_int(as, s, 10);
			break;
		case 's':
			switch(long_flag) {
			case 'l':
				pw = va_arg(ap, wchar_t *);
				if (pw == NULL)
					pw = L"(null)";
				if (archive_string_append_from_wcs(as, pw,
				    wcslen(pw)) != 0 && errno == ENOMEM)
					__archive_errx(1, "Out of memory");
				break;
			default:
				p2 = va_arg(ap, char *);
				if (p2 == NULL)
					p2 = "(null)";
				archive_strcat(as, p2);
				break;
			}
			break;
		case 'S':
			pw = va_arg(ap, wchar_t *);
			if (pw == NULL)
				pw = L"(null)";
			if (archive_string_append_from_wcs(as, pw,
			    wcslen(pw)) != 0 && errno == ENOMEM)
				__archive_errx(1, "Out of memory");
			break;
		case 'o': case 'u': case 'x': case 'X':
			/* Common handling for unsigned integer formats. */
			switch(long_flag) {
			case 'j': u = va_arg(ap, uintmax_t); break;
			case 'l': u = va_arg(ap, unsigned long); break;
			case 'z': u = va_arg(ap, size_t); break;
			default:  u = va_arg(ap, unsigned int); break;
			}
			/* Format it in the correct base. */
			switch (*p) {
			case 'o': append_uint(as, u, 8); break;
			case 'u': append_uint(as, u, 10); break;
			default: append_uint(as, u, 16); break;
			}
			break;
		default:
			/* Rewind and print the initial '%' literally. */
			p = saved_p;
			archive_strappend_char(as, *p);
		}
	}
}
