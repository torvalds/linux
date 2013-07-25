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
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * libcfs/include/libcfs/linux/linux-mem.h
 *
 * Basic library routines.
 */

#ifndef __LIBCFS_LINUX_CFS_MEM_H__
#define __LIBCFS_LINUX_CFS_MEM_H__

#ifndef __LIBCFS_LIBCFS_H__
#error Do not #include this file directly. #include <linux/libcfs/libcfs.h> instead
#endif


#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/memcontrol.h>
#include <linux/mm_inline.h>

#define CFS_PAGE_MASK		   (~((__u64)PAGE_CACHE_SIZE-1))
#define page_index(p)       ((p)->index)

#define memory_pressure_get() (current->flags & PF_MEMALLOC)
#define memory_pressure_set() do { current->flags |= PF_MEMALLOC; } while (0)
#define memory_pressure_clr() do { current->flags &= ~PF_MEMALLOC; } while (0)

#if BITS_PER_LONG == 32
/* limit to lowmem on 32-bit systems */
#define NUM_CACHEPAGES \
	min(num_physpages, 1UL << (30 - PAGE_CACHE_SHIFT) * 3 / 4)
#else
#define NUM_CACHEPAGES num_physpages
#endif

/*
 * In Linux there is no way to determine whether current execution context is
 * blockable.
 */
#define ALLOC_ATOMIC_TRY   GFP_ATOMIC

#define DECL_MMSPACE		mm_segment_t __oldfs
#define MMSPACE_OPEN \
	do { __oldfs = get_fs(); set_fs(get_ds());} while(0)
#define MMSPACE_CLOSE	       set_fs(__oldfs)

/*
 * Shrinker
 */

# define SHRINKER_ARGS(sc, nr_to_scan, gfp_mask)  \
		       struct shrinker *shrinker, \
		       struct shrink_control *sc
# define shrink_param(sc, var) ((sc)->var)

typedef int (*shrinker_t)(SHRINKER_ARGS(sc, nr_to_scan, gfp_mask));

static inline
struct shrinker *set_shrinker(int seek, shrinker_t func)
{
	struct shrinker *s;

	s = kmalloc(sizeof(*s), GFP_KERNEL);
	if (s == NULL)
		return (NULL);

	s->shrink = func;
	s->seeks = seek;

	register_shrinker(s);

	return s;
}

static inline
void remove_shrinker(struct shrinker *shrinker)
{
	if (shrinker == NULL)
		return;

	unregister_shrinker(shrinker);
	kfree(shrinker);
}

#endif /* __LINUX_CFS_MEM_H__ */
