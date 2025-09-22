/*	$OpenBSD: utf8.c,v 1.1 2016/01/10 14:04:16 schwarze Exp $	*/

/*
 * Copyright (c) 2015 Ingo Schwarze <schwarze@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * witH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vis.h>
#include <wchar.h>

int
mbswprint(const char *mbs, int maxwidth, int trail)
{
	char	  buf[5];
	wchar_t	  wc;
	int	  len;  /* length in bytes of UTF-8 encoded string */
	int	  width;  /* display width of a single Unicode char */
	int	  total_width;  /* display width of what is printed */

	total_width = 0;
	while (*mbs != '\0' && total_width < maxwidth) {
		len = mbtowc(&wc, mbs, MB_CUR_MAX);
		if (len == -1) {
			(void)mbtowc(NULL, NULL, MB_CUR_MAX);
			len = 1;
		}
		if (len == 1)
			width = vis(buf, mbs[0],
			    VIS_TAB | VIS_NL | VIS_CSTYLE, mbs[1]) - buf;
		else if ((width = wcwidth(wc)) == -1) {
			/* U+FFFD replacement character */
			memcpy(buf, "\357\277\275\0", 4);
			width = 1;
		} else {
			memcpy(buf, mbs, len);
			buf[len] = '\0';
		}
		if (total_width + width > maxwidth)
			break;
		fputs(buf, stdout);
		total_width += width;
		mbs += len;
	}
	if (trail)
		while (total_width++ < maxwidth)
			putchar(' ');
	return total_width;
}
