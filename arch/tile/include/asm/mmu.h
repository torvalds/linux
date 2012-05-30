/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#ifndef _ASM_TILE_MMU_H
#define _ASM_TILE_MMU_H

/* Capture any arch- and mm-specific information. */
struct mm_context {
	/*
	 * Written under the mmap_sem semaphore; read without the
	 * semaphore but atomically, but it is conservatively set.
	 */
	unsigned long priority_cached;
};

typedef struct mm_context mm_context_t;

void leave_mm(int cpu);

#endif /* _ASM_TILE_MMU_H */
