/*	$Id: test-wchar.c,v 1.4 2016/07/31 09:29:13 schwarze Exp $	*/
/*
 * Copyright (c) 2014 Ingo Schwarze <schwarze@openbsd.org>
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
#define _GNU_SOURCE /* wcwidth() */
#endif

#include <locale.h>
#include <stdio.h>
#include <wchar.h>
#include <unistd.h>

int
main(void)
{
	wchar_t	 wc;
	int	 width;

	if (setlocale(LC_ALL, "") == NULL) {
		fputs("setlocale(LC_ALL, \"\") failed\n", stderr);
		return 1;
	}

	if (setlocale(LC_CTYPE, UTF8_LOCALE) == NULL) {
		fprintf(stderr, "setlocale(LC_CTYPE, \"%s\") failed\n",
		    UTF8_LOCALE);
		return 1;
	}

	if (sizeof(wchar_t) < 4) {
		fprintf(stderr, "wchar_t is only %zu bytes\n",
		    sizeof(wchar_t));
		return 1;
	}

	if ((width = wcwidth(L' ')) != 1) {
		fprintf(stderr, "wcwidth(L' ') returned %d\n", width);
		return 1;
	}

	dup2(STDERR_FILENO, STDOUT_FILENO);
	wc = L'*';
	if (putwchar(wc) != (wint_t)wc) {
		fputs("bad putwchar return value\n", stderr);
		return 1;
	}

	return 0;
}
