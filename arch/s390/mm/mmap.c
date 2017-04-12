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

#include <linux/elf-randomize.h>
#include <linux/personality.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/sched/signal.h>
#include <linux/sched/mm.h>
#include <linux/random.h>
#include <linux/compat.h>
#include <linux/security.h>
#include <asm/pgalloc.h>
#include <asm/elf.h>

static unsigned long stack_maxrandom_size(void)
{
	if (!(current->flags & PF_RANDOMIZE))
		return 0;
	if (current->personality & ADDR_NO_RANDOMIZE)
		return 0;
	return STACK_RND_MASK << PAGE_SHIFT;
}

/*
 * Top of mmap area (just below the process stack).
 *
 * Leave at least a ~32 MB hole.
 */
#define MIN_GAP (32*1024*1024)
#define MAX_GAP (STACK_TOP/6*5)

static inline int mmap_is_legacy(void)
{
	if (current->personality & ADDR_COMPAT_LAYOUT)
		return 1;
	if (rlimit(RLIMIT_STACK) == RLIM_INFINITY)
		return 1;
	return sysctl_legacy_va_layout;
}

unsigned long arch_mmap_rnd(void)
{
	return (get_random_int() & MMAP_RND_MASK) << PAGE_SHIFT;
}

static unsigned long mmap_base_legacy(unsigned long rnd)
{
	return TASK_UNMAPPED_BASE + rnd;
}

static inline unsigned long mmap_base(unsigned long rnd)
{
	unsigned long gap = rlimit(RLIMIT_STACK);

	if (gap < MIN_GAP)
		gap = MIN_GAP;
	else if (gap > MAX_GAP)
		gap = MAX_GAP;
	gap &= PAGE_MASK;
	return STACK_TOP - stack_maxrandom_size() - rnd - gap;
}

unsigned long
arch_get_unmapped_area(struct file *filp, unsigned long addr,
		unsigned long len, unsigned long pgoff, unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	struct vm_unmapped_area_info info;

	if (len > TASK_SIZE - mmap_min_addr)
		return -ENOMEM;

	if (flags & MAP_FIXED)
		return addr;

	if (addr) {
		addr = PAGE_ALIGN(addr);
		vma = find_vma(mm, addr);
		if (TASK_SIZE - len >= addr && addr >= mmap_min_addr &&
		    (!vma || addr + len <= vma->vm_start))
			return addr;
	}

	info.flags = 0;
	info.length = len;
	info.low_limit = mm->mmap_base;
	info.high_limit = TASK_SIZE;
	if (filp || (flags & MAP_SHARED))
		info.align_mask = MMAP_ALIGN_MASK << PAGE_SHIFT;
	else
		info.align_mask = 0;
	info.align_offset = pgoff << PAGE_SHIFT;
	return vm_unmapped_area(&info);
}

unsigned long
arch_get_unmapped_area_topdown(struct file *filp, const unsigned long addr0,
			  const unsigned long len, const unsigned long pgoff,
			  const unsigned long flags)
{
	struct vm_area_struct *vma;
	struct mm_struct *mm = current->mm;
	unsigned long addr = addr0;
	struct vm_unmapped_area_info info;

	/* requested length too big for entire address space */
	if (len > TASK_SIZE - mmap_min_addr)
		return -ENOMEM;

	if (flags & MAP_FIXED)
		return addr;

	/* requesting a specific address */
	if (addr) {
		addr = PAGE_ALIGN(addr);
		vma = find_vma(mm, addr);
		if (TASK_SIZE - len >= addr && addr >= mmap_min_addr &&
				(!vma || addr + len <= vma->vm_start))
			return addr;
	}

	info.flags = VM_UNMAPPED_AREA_TOPDOWN;
	info.length = len;
	info.low_limit = max(PAGE_SIZE, mmap_min_addr);
	info.high_limit = mm->mmap_base;
	if (filp || (flags & MAP_SHARED))
		info.align_mask = MMAP_ALIGN_MASK << PAGE_SHIFT;
	else
		info.align_mask = 0;
	info.align_offset = pgoff << PAGE_SHIFT;
	addr = vm_unmapped_area(&info);

	/*
	 * A failed mmap() very likely causes application failure,
	 * so fall back to the bottom-up function here. This scenario
	 * can happen with large stack limits and large mmap()
	 * allocations.
	 */
	if (addr & ~PAGE_MASK) {
		VM_BUG_ON(addr != -ENOMEM);
		info.flags = 0;
		info.low_limit = TASK_UNMAPPED_BASE;
		info.high_limit = TASK_SIZE;
		addr = vm_unmapped_area(&info);
	}

	return addr;
}

int s390_mmap_check(unsigned long addr, unsigned long len, unsigned long flags)
{
	if (is_compat_task() || TASK_SIZE >= TASK_MAX_SIZE)
		return 0;
	if (!(flags & MAP_FIXED))
		addr = 0;
	if ((addr + len) >= TASK_SIZE)
		return crst_table_upgrade(current->mm);
	return 0;
}

static unsigned long
s390_get_unmapped_area(struct file *filp, unsigned long addr,
		unsigned long len, unsigned long pgoff, unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	unsigned long area;
	int rc;

	area = arch_get_unmapped_area(filp, addr, len, pgoff, flags);
	if (!(area & ~PAGE_MASK))
		return area;
	if (area == -ENOMEM && !is_compat_task() && TASK_SIZE < TASK_MAX_SIZE) {
		/* Upgrade the page table to 4 levels and retry. */
		rc = crst_table_upgrade(mm);
		if (rc)
			return (unsigned long) rc;
		area = arch_get_unmapped_area(filp, addr, len, pgoff, flags);
	}
	return area;
}

static unsigned long
s390_get_unmapped_area_topdown(struct file *filp, const unsigned long addr,
			  const unsigned long len, const unsigned long pgoff,
			  const unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	unsigned long area;
	int rc;

	area = arch_get_unmapped_area_topdown(filp, addr, len, pgoff, flags);
	if (!(area & ~PAGE_MASK))
		return area;
	if (area == -ENOMEM && !is_compat_task() && TASK_SIZE < TASK_MAX_SIZE) {
		/* Upgrade the page table to 4 levels and retry. */
		rc = crst_table_upgrade(mm);
		if (rc)
			return (unsigned long) rc;
		area = arch_get_unmapped_area_topdown(filp, addr, len,
						      pgoff, flags);
	}
	return area;
}
/*
 * This function, called very early during the creation of a new
 * process VM image, sets up which VM layout function to use:
 */
void arch_pick_mmap_layout(struct mm_struct *mm)
{
	unsigned long random_factor = 0UL;

	if (current->flags & PF_RANDOMIZE)
		random_factor = arch_mmap_rnd();

	/*
	 * Fall back to the standard layout if the personality
	 * bit is set, or if the expected stack growth is unlimited:
	 */
	if (mmap_is_legacy()) {
		mm->mmap_base = mmap_base_legacy(random_factor);
		mm->get_unmapped_area = s390_get_unmapped_area;
	} else {
		mm->mmap_base = mmap_base(random_factor);
		mm->get_unmapped_area = s390_get_unmapped_area_topdown;
	}
}
