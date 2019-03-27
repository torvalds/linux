/*	$Id: mandoc_aux.h,v 1.7 2017/06/12 19:05:47 schwarze Exp $ */
/*
 * Copyright (c) 2009, 2011 Kristaps Dzonsons <kristaps@bsd.lv>
 * Copyright (c) 2014, 2017 Ingo Schwarze <schwarze@openbsd.org>
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

int		  mandoc_asprintf(char **, const char *, ...)
			__attribute__((__format__ (__printf__, 2, 3)));
void		 *mandoc_calloc(size_t, size_t);
void		 *mandoc_malloc(size_t);
void		 *mandoc_realloc(void *, size_t);
void		 *mandoc_reallocarray(void *, size_t, size_t);
void		 *mandoc_recallocarray(void *, size_t, size_t, size_t);
char		 *mandoc_strdup(const char *);
char		 *mandoc_strndup(const char *, size_t);
