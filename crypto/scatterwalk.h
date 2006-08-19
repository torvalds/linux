/*
 * Cryptographic API.
 *
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 * Copyright (c) 2002 Adam J. Richter <adam@yggdrasil.com>
 * Copyright (c) 2004 Jean-Luc Cooke <jlcooke@certainkey.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#ifndef _CRYPTO_SCATTERWALK_H
#define _CRYPTO_SCATTERWALK_H

#include <linux/mm.h>
#include <linux/scatterlist.h>

#include "internal.h"

static inline struct scatterlist *sg_next(struct scatterlist *sg)
{
	return (++sg)->length ? sg : (void *)sg->page;
}

static inline unsigned long scatterwalk_samebuf(struct scatter_walk *walk_in,
						struct scatter_walk *walk_out)
{
	return !(((walk_in->sg->page - walk_out->sg->page) << PAGE_SHIFT) +
		 (int)(walk_in->offset - walk_out->offset));
}

static inline unsigned int scatterwalk_pagelen(struct scatter_walk *walk)
{
	unsigned int len = walk->sg->offset + walk->sg->length - walk->offset;
	unsigned int len_this_page = offset_in_page(~walk->offset) + 1;
	return len_this_page > len ? len : len_this_page;
}

static inline unsigned int scatterwalk_clamp(struct scatter_walk *walk,
					     unsigned int nbytes)
{
	unsigned int len_this_page = scatterwalk_pagelen(walk);
	return nbytes > len_this_page ? len_this_page : nbytes;
}

static inline void scatterwalk_advance(struct scatter_walk *walk,
				       unsigned int nbytes)
{
	walk->offset += nbytes;
}

static inline unsigned int scatterwalk_aligned(struct scatter_walk *walk,
					       unsigned int alignmask)
{
	return !(walk->offset & alignmask);
}

static inline struct page *scatterwalk_page(struct scatter_walk *walk)
{
	return walk->sg->page + (walk->offset >> PAGE_SHIFT);
}

static inline void scatterwalk_unmap(void *vaddr, int out)
{
	crypto_kunmap(vaddr, out);
}

void scatterwalk_start(struct scatter_walk *walk, struct scatterlist *sg);
void scatterwalk_copychunks(void *buf, struct scatter_walk *walk,
			    size_t nbytes, int out);
void *scatterwalk_map(struct scatter_walk *walk, int out);
void scatterwalk_done(struct scatter_walk *walk, int out, int more);

#endif  /* _CRYPTO_SCATTERWALK_H */
