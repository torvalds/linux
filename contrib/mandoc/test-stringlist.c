/*	$Id: test-stringlist.c,v 1.2 2015/10/06 18:32:20 schwarze Exp $	*/
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

#include <stringlist.h>

int
main(void)
{
	StringList	*sl;
	char		 teststr[] = "test";

	if ((sl = sl_init()) == NULL)
		return 1;
	if (sl_add(sl, teststr))
		return 2;
	if (sl->sl_cur != 1)
		return 3;
	if (sl->sl_str[0] != teststr)
		return 4;

	sl_free(sl, 0);
	return 0;
}
