/*
 * ARC700 mmap
 *
 * (started from arm version - for VIPT alias handling)
 *
 * Copyright (C) 2013 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/sched/mm.h>

#include <asm/cacheflush.h>

#define COLOUR_ALIGN(addr, pgoff)			\
	((((addr) + SHMLBA - 1) & ~(SHMLBA - 1)) +	\
	 (((pgoff) << PAGE_SHIFT) & (SHMLBA - 1)))

/*
 * Ensure that shared mappings are correctly aligned to
 * avoid aliasing issues with VIPT caches.
 * We need to ensure that
 * a specific page of an object is always mapped at a multiple of
 * SHMLBA bytes.
 */
unsigned long
arch_get_unmapped_area(struct file *filp, unsigned long addr,
		unsigned long len, unsigned long pgoff, unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	int do_align = 0;
	int aliasing = cache_is_vipt_aliasing();
	struct vm_unmapped_area_info info;

	/*
	 * We only need to do colour alignment if D cache aliases.
	 */
	if (aliasing)
		do_align = filp || (flags & MAP_SHARED);

	/*
	 * We enforce the MAP_FIXED case.
	 */
	if (flags & MAP_FIXED) {
		if (aliasing && flags & MAP_SHARED &&
		    (addr - (pgoff << PAGE_SHIFT)) & (SHMLBA - 1))
			return -EINVAL;
		return addr;
	}

	if (len > TASK_SIZE)
		return -ENOMEM;

	if (addr) {
		if (do_align)
			addr = COLOUR_ALIGN(addr, pgoff);
		else
			addr = PAGE_ALIGN(addr);

		vma = find_vma(mm, addr);
		if (TASK_SIZE - len >= addr &&
		    (!vma || addr + len <= vma->vm_start))
			return addr;
	}

	info.flags = 0;
	info.length = len;
	info.low_limit = mm->mmap_base;
	info.high_limit = TASK_SIZE;
	info.align_mask = do_align ? (PAGE_MASK & (SHMLBA - 1)) : 0;
	info.align_offset = pgoff << PAGE_SHIFT;
	return vm_unmapped_area(&info);
}
