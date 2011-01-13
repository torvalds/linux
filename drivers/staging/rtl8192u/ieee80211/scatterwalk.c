/*
 * Cryptographic API.
 *
 * Cipher operations.
 *
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 *               2002 Adam J. Richter <adam@yggdrasil.com>
 *               2004 Jean-Luc Cooke <jlcooke@certainkey.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */
#include "kmap_types.h"

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <asm/scatterlist.h>
#include "internal.h"
#include "scatterwalk.h"

enum km_type crypto_km_types[] = {
	KM_USER0,
	KM_USER1,
	KM_SOFTIRQ0,
	KM_SOFTIRQ1,
};

void *scatterwalk_whichbuf(struct scatter_walk *walk, unsigned int nbytes, void *scratch)
{
	if (nbytes <= walk->len_this_page &&
	    (((unsigned long)walk->data) & (PAGE_CACHE_SIZE - 1)) + nbytes <=
	    PAGE_CACHE_SIZE)
		return walk->data;
	else
		return scratch;
}

static void memcpy_dir(void *buf, void *sgdata, size_t nbytes, int out)
{
	if (out)
		memcpy(sgdata, buf, nbytes);
	else
		memcpy(buf, sgdata, nbytes);
}

void scatterwalk_start(struct scatter_walk *walk, struct scatterlist *sg)
{
	unsigned int rest_of_page;

	walk->sg = sg;

	walk->page = sg->page;
	walk->len_this_segment = sg->length;

	rest_of_page = PAGE_CACHE_SIZE - (sg->offset & (PAGE_CACHE_SIZE - 1));
	walk->len_this_page = min(sg->length, rest_of_page);
	walk->offset = sg->offset;
}

void scatterwalk_map(struct scatter_walk *walk, int out)
{
	walk->data = crypto_kmap(walk->page, out) + walk->offset;
}

static void scatterwalk_pagedone(struct scatter_walk *walk, int out,
				 unsigned int more)
{
	/* walk->data may be pointing the first byte of the next page;
	   however, we know we transfered at least one byte.  So,
	   walk->data - 1 will be a virtual address in the mapped page. */

	if (out)
		flush_dcache_page(walk->page);

	if (more) {
		walk->len_this_segment -= walk->len_this_page;

		if (walk->len_this_segment) {
			walk->page++;
			walk->len_this_page = min(walk->len_this_segment,
						  (unsigned)PAGE_CACHE_SIZE);
			walk->offset = 0;
		}
		else
			scatterwalk_start(walk, sg_next(walk->sg));
	}
}

void scatterwalk_done(struct scatter_walk *walk, int out, int more)
{
	crypto_kunmap(walk->data, out);
	if (walk->len_this_page == 0 || !more)
		scatterwalk_pagedone(walk, out, more);
}

/*
 * Do not call this unless the total length of all of the fragments
 * has been verified as multiple of the block size.
 */
int scatterwalk_copychunks(void *buf, struct scatter_walk *walk,
			   size_t nbytes, int out)
{
	if (buf != walk->data) {
		while (nbytes > walk->len_this_page) {
			memcpy_dir(buf, walk->data, walk->len_this_page, out);
			buf += walk->len_this_page;
			nbytes -= walk->len_this_page;

			crypto_kunmap(walk->data, out);
			scatterwalk_pagedone(walk, out, 1);
			scatterwalk_map(walk, out);
		}

		memcpy_dir(buf, walk->data, nbytes, out);
	}

	walk->offset += nbytes;
	walk->len_this_page -= nbytes;
	walk->len_this_segment -= nbytes;
	return 0;
}
