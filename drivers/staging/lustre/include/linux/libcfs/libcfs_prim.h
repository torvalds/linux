// SPDX-License-Identifier: GPL-2.0
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
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * libcfs/include/libcfs/libcfs_prim.h
 *
 * General primitives.
 *
 */

#ifndef __LIBCFS_PRIM_H__
#define __LIBCFS_PRIM_H__

/*
 * Memory
 */
#if BITS_PER_LONG == 32
/* limit to lowmem on 32-bit systems */
#define NUM_CACHEPAGES \
	min(totalram_pages, 1UL << (30 - PAGE_SHIFT) * 3 / 4)
#else
#define NUM_CACHEPAGES totalram_pages
#endif

static inline unsigned int memory_pressure_get(void)
{
	return current->flags & PF_MEMALLOC;
}

static inline void memory_pressure_set(void)
{
	current->flags |= PF_MEMALLOC;
}

static inline void memory_pressure_clr(void)
{
	current->flags &= ~PF_MEMALLOC;
}

static inline int cfs_memory_pressure_get_and_set(void)
{
	int old = memory_pressure_get();

	if (!old)
		memory_pressure_set();
	return old;
}

static inline void cfs_memory_pressure_restore(int old)
{
	if (old)
		memory_pressure_set();
	else
		memory_pressure_clr();
}
#endif
