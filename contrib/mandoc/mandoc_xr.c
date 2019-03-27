/*	$Id: mandoc_xr.c,v 1.3 2017/07/02 21:18:29 schwarze Exp $ */
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
#include <sys/types.h>

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "mandoc_aux.h"
#include "mandoc_ohash.h"
#include "mandoc_xr.h"

static struct ohash	 *xr_hash = NULL;
static struct mandoc_xr	 *xr_first = NULL;
static struct mandoc_xr	 *xr_last = NULL;

static void		  mandoc_xr_clear(void);


static void
mandoc_xr_clear(void)
{
	struct mandoc_xr	*xr;
	unsigned int		 slot;

	if (xr_hash == NULL)
		return;
	for (xr = ohash_first(xr_hash, &slot); xr != NULL;
	     xr = ohash_next(xr_hash, &slot))
		free(xr);
	ohash_delete(xr_hash);
}

void
mandoc_xr_reset(void)
{
	if (xr_hash == NULL)
		xr_hash = mandoc_malloc(sizeof(*xr_hash));
	else
		mandoc_xr_clear();
	mandoc_ohash_init(xr_hash, 5,
	    offsetof(struct mandoc_xr, hashkey));
	xr_first = xr_last = NULL;
}

int
mandoc_xr_add(const char *sec, const char *name, int line, int pos)
{
	struct mandoc_xr	 *xr, *oxr;
	const char		 *pend;
	size_t			  ssz, nsz, tsz;
	unsigned int		  slot;
	int			  ret;
	uint32_t		  hv;

	if (xr_hash == NULL)
		return 0;

	ssz = strlen(sec) + 1;
	nsz = strlen(name) + 1;
	tsz = ssz + nsz;
	xr = mandoc_malloc(sizeof(*xr) + tsz);
	xr->next = NULL;
	xr->sec = xr->hashkey;
	xr->name = xr->hashkey + ssz;
	xr->line = line;
	xr->pos = pos;
	xr->count = 1;
	memcpy(xr->sec, sec, ssz);
	memcpy(xr->name, name, nsz);

	pend = xr->hashkey + tsz;
	hv = ohash_interval(xr->hashkey, &pend);
	slot = ohash_lookup_memory(xr_hash, xr->hashkey, tsz, hv);
	if ((oxr = ohash_find(xr_hash, slot)) == NULL) {
		ohash_insert(xr_hash, slot, xr);
		if (xr_first == NULL)
			xr_first = xr;
		else
			xr_last->next = xr;
		xr_last = xr;
		return 0;
	}

	oxr->count++;
	ret = (oxr->line == -1) ^ (xr->line == -1);
	if (xr->line == -1)
		oxr->line = -1;
	free(xr);
	return ret;
}

struct mandoc_xr *
mandoc_xr_get(void)
{
	return xr_first;
}

void
mandoc_xr_free(void)
{
	mandoc_xr_clear();
	free(xr_hash);
	xr_hash = NULL;
}
