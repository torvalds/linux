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
 *
 * Taken from the i386 architecture and simplified.
 */

#include <linux/mm.h>
#include <linux/random.h>
#include <linux/limits.h>
#include <linux/sched.h>
#include <linux/mman.h>
#include <linux/compat.h>

/*
 * Top of mmap area (just below the process stack).
 *
 * Leave an at least ~128 MB hole.
 */
#define MIN_GAP (128*1024*1024)
#define MAX_GAP (TASK_SIZE/6*5)

static inline unsigned long mmap_base(struct mm_struct *mm)
{
	unsigned long gap = rlimit(RLIMIT_STACK);
	unsigned long random_factor = 0;

	if (current->flags & PF_RANDOMIZE)
		random_factor = get_random_int() % (1024*1024);

	if (gap < MIN_GAP)
		gap = MIN_GAP;
	else if (gap > MAX_GAP)
		gap = MAX_GAP;

	return PAGE_ALIGN(TASK_SIZE - gap - random_factor);
}

/*
 * This function, called very early during the creation of a new
 * process VM image, sets up which VM layout function to use:
 */
void arch_pick_mmap_layout(struct mm_struct *mm)
{
#if !defined(__tilegx__)
	int is_32bit = 1;
#elif defined(CONFIG_COMPAT)
	int is_32bit = is_compat_task();
#else
	int is_32bit = 0;
#endif

	/*
	 * Use standard layout if the expected stack growth is unlimited
	 * or we are running native 64 bits.
	 */
	if (!is_32bit || rlimit(RLIMIT_STACK) == RLIM_INFINITY) {
		mm->mmap_base = TASK_UNMAPPED_BASE;
		mm->get_unmapped_area = arch_get_unmapped_area;
	} else {
		mm->mmap_base = mmap_base(mm);
		mm->get_unmapped_area = arch_get_unmapped_area_topdown;
	}
}
