// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#include <linux/export.h>
#include <linux/mm.h>
#include <linux/mman.h>

unsigned long shm_align_mask = PAGE_SIZE - 1;	/* Sane caches */
EXPORT_SYMBOL(shm_align_mask);

#define COLOUR_ALIGN(addr, pgoff)				\
	((((addr) + shm_align_mask) & ~shm_align_mask) +	\
	 (((pgoff) << PAGE_SHIFT) & shm_align_mask))

enum mmap_allocation_direction {UP, DOWN};

static unsigned long arch_get_unmapped_area_common(struct file *filp,
	unsigned long addr0, unsigned long len, unsigned long pgoff,
	unsigned long flags, enum mmap_allocation_direction dir)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long addr = addr0;
	int do_color_align;
	struct vm_unmapped_area_info info;

	if (unlikely(len > TASK_SIZE))
		return -ENOMEM;

	if (flags & MAP_FIXED) {
		/* Even MAP_FIXED mappings must reside within TASK_SIZE */
		if (TASK_SIZE - len < addr)
			return -EINVAL;

		/*
		 * We do not accept a shared mapping if it would violate
		 * cache aliasing constraints.
		 */
		if ((flags & MAP_SHARED) &&
		    ((addr - (pgoff << PAGE_SHIFT)) & shm_align_mask))
			return -EINVAL;
		return addr;
	}

	do_color_align = 0;
	if (filp || (flags & MAP_SHARED))
		do_color_align = 1;

	/* requesting a specific address */
	if (addr) {
		if (do_color_align)
			addr = COLOUR_ALIGN(addr, pgoff);
		else
			addr = PAGE_ALIGN(addr);

		vma = find_vma(mm, addr);
		if (TASK_SIZE - len >= addr &&
		    (!vma || addr + len <= vm_start_gap(vma)))
			return addr;
	}

	info.length = len;
	info.align_mask = do_color_align ? (PAGE_MASK & shm_align_mask) : 0;
	info.align_offset = pgoff << PAGE_SHIFT;

	if (dir == DOWN) {
		info.flags = VM_UNMAPPED_AREA_TOPDOWN;
		info.low_limit = PAGE_SIZE;
		info.high_limit = mm->mmap_base;
		addr = vm_unmapped_area(&info);

		if (!(addr & ~PAGE_MASK))
			return addr;

		/*
		 * A failed mmap() very likely causes application failure,
		 * so fall back to the bottom-up function here. This scenario
		 * can happen with large stack limits and large mmap()
		 * allocations.
		 */
	}

	info.flags = 0;
	info.low_limit = mm->mmap_base;
	info.high_limit = TASK_SIZE;
	return vm_unmapped_area(&info);
}

unsigned long arch_get_unmapped_area(struct file *filp, unsigned long addr0,
	unsigned long len, unsigned long pgoff, unsigned long flags)
{
	return arch_get_unmapped_area_common(filp,
			addr0, len, pgoff, flags, UP);
}

/*
 * There is no need to export this but sched.h declares the function as
 * extern so making it static here results in an error.
 */
unsigned long arch_get_unmapped_area_topdown(struct file *filp,
	unsigned long addr0, unsigned long len, unsigned long pgoff,
	unsigned long flags)
{
	return arch_get_unmapped_area_common(filp,
			addr0, len, pgoff, flags, DOWN);
}

int __virt_addr_valid(volatile void *kaddr)
{
	unsigned long vaddr = (unsigned long)kaddr;

	if ((vaddr < PAGE_OFFSET) || (vaddr >= vm_map_base))
		return 0;

	return pfn_valid(PFN_DOWN(PHYSADDR(kaddr)));
}
EXPORT_SYMBOL_GPL(__virt_addr_valid);
