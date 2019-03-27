/*	$Id: dba.h,v 1.2 2016/08/17 20:46:56 schwarze Exp $ */
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
 * Public interface of the allocation-based version
 * of the mandoc database, for read-write access.
 * To be used by dba.c, dba_read.c, and makewhatis(8).
 */

#define	DBP_NAME	0
#define	DBP_SECT	1
#define	DBP_ARCH	2
#define	DBP_DESC	3
#define	DBP_FILE	4
#define	DBP_MAX		5

struct dba_array;

struct dba {
	struct dba_array	*pages;
	struct dba_array	*macros;
};


struct dba	*dba_new(int32_t);
void		 dba_free(struct dba *);
struct dba	*dba_read(const char *);
int		 dba_write(const char *, struct dba *);

struct dba_array *dba_page_new(struct dba_array *, const char *,
			const char *, const char *, enum form);
void		 dba_page_add(struct dba_array *, int32_t, const char *);
void		 dba_page_alias(struct dba_array *, const char *, uint64_t);

void		 dba_macro_new(struct dba *, int32_t,
			const char *, const int32_t *);
void		 dba_macro_add(struct dba_array *, int32_t,
			const char *, struct dba_array *);
