/*	$Id: dba_array.h,v 1.1 2016/07/19 21:31:55 schwarze Exp $ */
/*
 * Copyright (c) 2016 Ingo Schwarze <schwarze@openbsd.org>
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
 *
 * Public interface for allocation-based arrays
 * for the mandoc database, for read-write access.
 * To be used by dba*.c and by makewhatis(8).
 */

struct dba_array;

#define	DBA_STR		0x01	/* Map contains strings, not pointers. */
#define	DBA_GROW	0x02	/* Allow the array to grow. */

#define	dba_array_FOREACH(a, e) \
	dba_array_start(a); \
	while (((e) = dba_array_next(a)) != NULL)

typedef int dba_compare_func(const void *, const void *);

struct dba_array *dba_array_new(int32_t, int);
void		 dba_array_free(struct dba_array *);
void		 dba_array_set(struct dba_array *, int32_t, void *);
void		 dba_array_add(struct dba_array *, void *);
void		*dba_array_get(struct dba_array *, int32_t);
void		 dba_array_start(struct dba_array *);
void		*dba_array_next(struct dba_array *);
void		 dba_array_del(struct dba_array *);
void		 dba_array_undel(struct dba_array *);
void		 dba_array_setpos(struct dba_array *, int32_t, int32_t);
int32_t		 dba_array_getpos(struct dba_array *);
void		 dba_array_sort(struct dba_array *, dba_compare_func);
int32_t		 dba_array_writelen(struct dba_array *, int32_t);
void		 dba_array_writepos(struct dba_array *);
void		 dba_array_writelst(struct dba_array *);
