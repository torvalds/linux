/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */
#define DEBUG_SUBSYSTEM S_LNET

#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/libcfs/libcfs.h>

/*
 * NB: we will rename some of above functions in another patch:
 * - rename kmalloc to cfs_malloc
 * - rename kmalloc/free_page to cfs_page_alloc/free
 * - rename kmalloc/free_large to cfs_vmalloc/vfree
 */

void *
cfs_cpt_malloc(struct cfs_cpt_table *cptab, int cpt,
	       size_t nr_bytes, unsigned int flags)
{
	void    *ptr;

	ptr = kmalloc_node(nr_bytes, flags,
			   cfs_cpt_spread_node(cptab, cpt));
	if (ptr != NULL && (flags & __GFP_ZERO) != 0)
		memset(ptr, 0, nr_bytes);

	return ptr;
}
EXPORT_SYMBOL(cfs_cpt_malloc);

void *
cfs_cpt_vmalloc(struct cfs_cpt_table *cptab, int cpt, size_t nr_bytes)
{
	return vmalloc_node(nr_bytes, cfs_cpt_spread_node(cptab, cpt));
}
EXPORT_SYMBOL(cfs_cpt_vmalloc);

struct page *
cfs_page_cpt_alloc(struct cfs_cpt_table *cptab, int cpt, unsigned int flags)
{
	return alloc_pages_node(cfs_cpt_spread_node(cptab, cpt), flags, 0);
}
EXPORT_SYMBOL(cfs_page_cpt_alloc);

void *
cfs_mem_cache_cpt_alloc(struct kmem_cache *cachep, struct cfs_cpt_table *cptab,
			int cpt, unsigned int flags)
{
	return kmem_cache_alloc_node(cachep, flags,
				     cfs_cpt_spread_node(cptab, cpt));
}
EXPORT_SYMBOL(cfs_mem_cache_cpt_alloc);
