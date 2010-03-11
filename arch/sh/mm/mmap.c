/*
 * arch/sh/mm/mmap.c
 *
 * Copyright (C) 2008 - 2009  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/module.h>
#include <asm/page.h>
#include <asm/processor.h>

unsigned long shm_align_mask = PAGE_SIZE - 1;	/* Sane caches */
EXPORT_SYMBOL(shm_align_mask);

#ifdef CONFIG_MMU
/*
 * To avoid cache aliases, we map the shared page with same color.
 */
static inline unsigned long COLOUR_ALIGN(unsigned long addr,
					 unsigned long pgoff)
{
	unsigned long base = (addr + shm_align_mask) & ~shm_align_mask;
	unsigned long off = (pgoff << PAGE_SHIFT) & shm_align_mask;

	return base + off;
}

static inline unsigned long COLOUR_ALIGN_DOWN(unsigned long addr,
					      unsigned long pgoff)
{
	unsigned long base = addr & ~shm_align_mask;
	unsigned long off = (pgoff << PAGE_SHIFT) & shm_align_mask;

	if (base + off <= addr)
		return base + off;

	return base - off;
}

unsigned long arch_get_unmapped_area(struct file *filp, unsigned long addr,
	unsigned long len, unsigned long pgoff, unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long start_addr;
	int do_colour_align;

	if (flags & MAP_FIXED) {
		/* We do not accept a shared mapping if it would violate
		 * cache aliasing constraints.
		 */
		if ((flags & MAP_SHARED) &&
		    ((addr - (pgoff << PAGE_SHIFT)) & shm_align_mask))
			return -EINVAL;
		return addr;
	}

	if (unlikely(len > TASK_SIZE))
		return -ENOMEM;

	do_colour_align = 0;
	if (filp || (flags & MAP_SHARED))
		do_colour_align = 1;

	if (addr) {
		if (do_colour_align)
			addr = COLOUR_ALIGN(addr, pgoff);
		else
			addr = PAGE_ALIGN(addr);

		vma = find_vma(mm, addr);
		if (TASK_SIZE - len >= addr &&
		    (!vma || addr + len <= vma->vm_start))
			return addr;
	}

	if (len > mm->cached_hole_size) {
		start_addr = addr = mm->free_area_cache;
	} else {
	        mm->cached_hole_size = 0;
		start_addr = addr = TASK_UNMAPPED_BASE;
	}

full_search:
	if (do_colour_align)
		addr = COLOUR_ALIGN(addr, pgoff);
	else
		addr = PAGE_ALIGN(mm->free_area_cache);

	for (vma = find_vma(mm, addr); ; vma = vma->vm_next) {
		/* At this point:  (!vma || addr < vma->vm_end). */
		if (unlikely(TASK_SIZE - len < addr)) {
			/*
			 * Start a new search - just in case we missed
			 * some holes.
			 */
			if (start_addr != TASK_UNMAPPED_BASE) {
				start_addr = addr = TASK_UNMAPPED_BASE;
				mm->cached_hole_size = 0;
				goto full_search;
			}
			return -ENOMEM;
		}
		if (likely(!vma || addr + len <= vma->vm_start)) {
			/*
			 * Remember the place where we stopped the search:
			 */
			mm->free_area_cache = addr + len;
			return addr;
		}
		if (addr + mm->cached_hole_size < vma->vm_start)
		        mm->cached_hole_size = vma->vm_start - addr;

		addr = vma->vm_end;
		if (do_colour_align)
			addr = COLOUR_ALIGN(addr, pgoff);
	}
}

unsigned long
arch_get_unmapped_area_topdown(struct file *filp, const unsigned long addr0,
			  const unsigned long len, const unsigned long pgoff,
			  const unsigned long flags)
{
	struct vm_area_struct *vma;
	struct mm_struct *mm = current->mm;
	unsigned long addr = addr0;
	int do_colour_align;

	if (flags & MAP_FIXED) {
		/* We do not accept a shared mapping if it would violate
		 * cache aliasing constraints.
		 */
		if ((flags & MAP_SHARED) &&
		    ((addr - (pgoff << PAGE_SHIFT)) & shm_align_mask))
			return -EINVAL;
		return addr;
	}

	if (unlikely(len > TASK_SIZE))
		return -ENOMEM;

	do_colour_align = 0;
	if (filp || (flags & MAP_SHARED))
		do_colour_align = 1;

	/* requesting a specific address */
	if (addr) {
		if (do_colour_align)
			addr = COLOUR_ALIGN(addr, pgoff);
		else
			addr = PAGE_ALIGN(addr);

		vma = find_vma(mm, addr);
		if (TASK_SIZE - len >= addr &&
		    (!vma || addr + len <= vma->vm_start))
			return addr;
	}

	/* check if free_area_cache is useful for us */
	if (len <= mm->cached_hole_size) {
	        mm->cached_hole_size = 0;
		mm->free_area_cache = mm->mmap_base;
	}

	/* either no address requested or can't fit in requested address hole */
	addr = mm->free_area_cache;
	if (do_colour_align) {
		unsigned long base = COLOUR_ALIGN_DOWN(addr-len, pgoff);

		addr = base + len;
	}

	/* make sure it can fit in the remaining address space */
	if (likely(addr > len)) {
		vma = find_vma(mm, addr-len);
		if (!vma || addr <= vma->vm_start) {
			/* remember the address as a hint for next time */
			return (mm->free_area_cache = addr-len);
		}
	}

	if (unlikely(mm->mmap_base < len))
		goto bottomup;

	addr = mm->mmap_base-len;
	if (do_colour_align)
		addr = COLOUR_ALIGN_DOWN(addr, pgoff);

	do {
		/*
		 * Lookup failure means no vma is above this address,
		 * else if new region fits below vma->vm_start,
		 * return with success:
		 */
		vma = find_vma(mm, addr);
		if (likely(!vma || addr+len <= vma->vm_start)) {
			/* remember the address as a hint for next time */
			return (mm->free_area_cache = addr);
		}

		/* remember the largest hole we saw so far */
		if (addr + mm->cached_hole_size < vma->vm_start)
		        mm->cached_hole_size = vma->vm_start - addr;

		/* try just below the current vma->vm_start */
		addr = vma->vm_start-len;
		if (do_colour_align)
			addr = COLOUR_ALIGN_DOWN(addr, pgoff);
	} while (likely(len < vma->vm_start));

bottomup:
	/*
	 * A failed mmap() very likely causes application failure,
	 * so fall back to the bottom-up function here. This scenario
	 * can happen with large stack limits and large mmap()
	 * allocations.
	 */
	mm->cached_hole_size = ~0UL;
	mm->free_area_cache = TASK_UNMAPPED_BASE;
	addr = arch_get_unmapped_area(filp, addr0, len, pgoff, flags);
	/*
	 * Restore the topdown base:
	 */
	mm->free_area_cache = mm->mmap_base;
	mm->cached_hole_size = ~0UL;

	return addr;
}
#endif /* CONFIG_MMU */

/*
 * You really shouldn't be using read() or write() on /dev/mem.  This
 * might go away in the future.
 */
int valid_phys_addr_range(unsigned long addr, size_t count)
{
	if (addr < __MEMORY_START)
		return 0;
	if (addr + count > __pa(high_memory))
		return 0;

	return 1;
}

int valid_mmap_phys_addr_range(unsigned long pfn, size_t size)
{
	return 1;
}
