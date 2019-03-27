/*	$Id: mandoc_xr.h,v 1.3 2017/07/02 21:18:29 schwarze Exp $ */
/*
 * Copyright (c) 2017 Ingo Schwarze <schwarze@openbsd.org>
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

struct	mandoc_xr {
	struct mandoc_xr *next;
	char		 *sec;
	char		 *name;
	int		  line;  /* Or -1 for this page's own names. */
	int		  pos;
	int		  count;
	char		  hashkey[];
};

void		  mandoc_xr_reset(void);
int		  mandoc_xr_add(const char *, const char *, int, int);
struct mandoc_xr *mandoc_xr_get(void);
void		  mandoc_xr_free(void);
