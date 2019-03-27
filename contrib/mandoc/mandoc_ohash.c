/*	$Id: mandoc_ohash.c,v 1.2 2015/10/19 18:58:47 schwarze Exp $	*/
/*
 * Copyright (c) 2014, 2015 Ingo Schwarze <schwarze@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHORS DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <sys/types.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "mandoc_aux.h"
#include "mandoc_ohash.h"

static	void	 *hash_alloc(size_t, void *);
static	void	 *hash_calloc(size_t, size_t, void *);
static	void	  hash_free(void *, void *);


void
mandoc_ohash_init(struct ohash *h, unsigned int sz, ptrdiff_t ko)
{
	struct ohash_info info;

	info.alloc = hash_alloc;
	info.calloc = hash_calloc;
	info.free = hash_free;
	info.data = NULL;
	info.key_offset = ko;

	ohash_init(h, sz, &info);
}

static void *
hash_alloc(size_t sz, void *arg)
{

	return mandoc_malloc(sz);
}

static void *
hash_calloc(size_t n, size_t sz, void *arg)
{

	return mandoc_calloc(n, sz);
}

static void
hash_free(void *p, void *arg)
{

	free(p);
}
