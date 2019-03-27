/*	$Id: test-vasprintf.c,v 1.4 2016/07/18 18:35:05 schwarze Exp $	*/
/*
 * Copyright (c) 2015 Ingo Schwarze <schwarze@openbsd.org>
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

#if defined(__linux__) || defined(__MINT__)
#define _GNU_SOURCE /* vasprintf() */
#endif

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int	 testfunc(char **, const char *, ...);


static int
testfunc(char **ret, const char *format, ...)
{
	va_list	 ap;
	int	 irc;

	va_start(ap, format);
	irc = vasprintf(ret, format, ap);
	va_end(ap);

	return irc;
}

int
main(void)
{
	char	*ret;

	if (testfunc(&ret, "%s.", "Text") != 5)
		return 1;
	if (strcmp(ret, "Text."))
		return 2;
	return 0;
}
