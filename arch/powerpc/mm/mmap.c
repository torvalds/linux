/*
 *  flexible mmap layout support
 *
 * Copyright 2003-2004 Red Hat Inc., Durham, North Carolina.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *
 * Started by Ingo Molnar <mingo@elte.hu>
 */

#include <linux/personality.h>
#include <linux/mm.h>

/*
 * Top of mmap area (just below the process stack).
 *
 * Leave an at least ~128 MB hole.
 */
#define MIN_GAP (128*1024*1024)
#define MAX_GAP (TASK_SIZE/6*5)

static inline unsigned long mmap_base(void)
{
	unsigned long gap = current->signal->rlim[RLIMIT_STACK].rlim_cur;

	if (gap < MIN_GAP)
		gap = MIN_GAP;
	else if (gap > MAX_GAP)
		gap = MAX_GAP;

	return TASK_SIZE - (gap & PAGE_MASK);
}

static inline int mmap_is_legacy(void)
{
	/*
	 * Force standard allocation for 64 bit programs.
	 */
	if (!test_thread_flag(TIF_32BIT))
		return 1;

	if (current->personality & ADDR_COMPAT_LAYOUT)
		return 1;

	if (current->signal->rlim[RLIMIT_STACK].rlim_cur == RLIM_INFINITY)
		return 1;

	return sysctl_legacy_va_layout;
}

/*
 * This function, called very early during the creation of a new
 * process VM image, sets up which VM layout function to use:
 */
void arch_pick_mmap_layout(struct mm_struct *mm)
{
	/*
	 * Fall back to the standard layout if the personality
	 * bit is set, or if the expected stack growth is unlimited:
	 */
	if (mmap_is_legacy()) {
		mm->mmap_base = TASK_UNMAPPED_BASE;
		mm->get_unmapped_area = arch_get_unmapped_area;
		mm->unmap_area = arch_unmap_area;
	} else {
		mm->mmap_base = mmap_base();
		mm->get_unmapped_area = arch_get_unmapped_area_topdown;
		mm->unmap_area = arch_unmap_area_topdown;
	}
}
