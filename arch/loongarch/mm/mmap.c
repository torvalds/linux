// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#include <linux/export.h>
#include <linux/io.h>
#include <linux/memblock.h>
#include <linux/mm.h>
#include <linux/mman.h>

#define SHM_ALIGN_MASK	(SHMLBA - 1)

#define COLOUR_ALIGN(addr, pgoff)			\
	((((addr) + SHM_ALIGN_MASK) & ~SHM_ALIGN_MASK)	\
	 + (((pgoff) << PAGE_SHIFT) & SHM_ALIGN_MASK))

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
		    ((addr - (pgoff << PAGE_SHIFT)) & SHM_ALIGN_MASK))
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
	info.align_mask = do_color_align ? (PAGE_MASK & SHM_ALIGN_MASK) : 0;
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

/*
 * You really shouldn't be using read() or write() on /dev/mem.  This might go
 * away in the future.
 */
int valid_phys_addr_range(phys_addr_t addr, size_t size)
{
	/*
	 * Check whether addr is covered by a memory region without the
	 * MEMBLOCK_NOMAP attribute, and whether that region covers the
	 * entire range. In theory, this could lead to false negatives
	 * if the range is covered by distinct but adjacent memory regions
	 * that only differ in other attributes. However, few of such
	 * attributes have been defined, and it is debatable whether it
	 * follows that /dev/mem read() calls should be able traverse
	 * such boundaries.
	 */
	return memblock_is_region_memory(addr, size) && memblock_is_map_memory(addr);
}

/*
 * Do not allow /dev/mem mappings beyond the supported physical range.
 */
int valid_mmap_phys_addr_range(unsigned long pfn, size_t size)
{
	return !(((pfn << PAGE_SHIFT) + size) & ~(GENMASK_ULL(cpu_pabits, 0)));
}
