/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2011 Wind River Systems,
 *   written by Ralf Baechle <ralf@linux-mips.org>
 */
#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/module.h>
#include <linux/personality.h>
#include <linux/random.h>
#include <linux/sched.h>

unsigned long shm_align_mask = PAGE_SIZE - 1;	/* Sane caches */
EXPORT_SYMBOL(shm_align_mask);

/* gap between mmap and stack */
#define MIN_GAP (128*1024*1024UL)
#define MAX_GAP ((TASK_SIZE)/6*5)

static int mmap_is_legacy(void)
{
	if (current->personality & ADDR_COMPAT_LAYOUT)
		return 1;

	if (rlimit(RLIMIT_STACK) == RLIM_INFINITY)
		return 1;

	return sysctl_legacy_va_layout;
}

static unsigned long mmap_base(unsigned long rnd)
{
	unsigned long gap = rlimit(RLIMIT_STACK);

	if (gap < MIN_GAP)
		gap = MIN_GAP;
	else if (gap > MAX_GAP)
		gap = MAX_GAP;

	return PAGE_ALIGN(TASK_SIZE - gap - rnd);
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
		    (!vma || addr + len <= vma->vm_start))
			return addr;
	}

	if (dir == UP) {
		addr = mm->mmap_base;
		if (do_color_align)
			addr = COLOUR_ALIGN(addr, pgoff);
		else
			addr = PAGE_ALIGN(addr);

		for (vma = find_vma(current->mm, addr); ; vma = vma->vm_next) {
			/* At this point:  (!vma || addr < vma->vm_end). */
			if (TASK_SIZE - len < addr)
				return -ENOMEM;
			if (!vma || addr + len <= vma->vm_start)
				return addr;
			addr = vma->vm_end;
			if (do_color_align)
				addr = COLOUR_ALIGN(addr, pgoff);
		 }
	 } else {
		/* check if free_area_cache is useful for us */
		if (len <= mm->cached_hole_size) {
			mm->cached_hole_size = 0;
			mm->free_area_cache = mm->mmap_base;
		}

		/*
		 * either no address requested, or the mapping can't fit into
		 * the requested address hole
		 */
		addr = mm->free_area_cache;
		if (do_color_align) {
			unsigned long base =
				COLOUR_ALIGN_DOWN(addr - len, pgoff);
			addr = base + len;
		}

		/* make sure it can fit in the remaining address space */
		if (likely(addr > len)) {
			vma = find_vma(mm, addr - len);
			if (!vma || addr <= vma->vm_start) {
				/* cache the address as a hint for next time */
				return mm->free_area_cache = addr - len;
			}
		}

		if (unlikely(mm->mmap_base < len))
			goto bottomup;

		addr = mm->mmap_base - len;
		if (do_color_align)
			addr = COLOUR_ALIGN_DOWN(addr, pgoff);

		do {
			/*
			 * Lookup failure means no vma is above this address,
			 * else if new region fits below vma->vm_start,
			 * return with success:
			 */
			vma = find_vma(mm, addr);
			if (likely(!vma || addr + len <= vma->vm_start)) {
				/* cache the address as a hint for next time */
				return mm->free_area_cache = addr;
			}

			/* remember the largest hole we saw so far */
			if (addr + mm->cached_hole_size < vma->vm_start)
				mm->cached_hole_size = vma->vm_start - addr;

			/* try just below the current vma->vm_start */
			addr = vma->vm_start - len;
			if (do_color_align)
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

void arch_pick_mmap_layout(struct mm_struct *mm)
{
	unsigned long random_factor = 0UL;

	if (current->flags & PF_RANDOMIZE) {
		random_factor = get_random_int();
		random_factor = random_factor << PAGE_SHIFT;
		if (TASK_IS_32BIT_ADDR)
			random_factor &= 0xfffffful;
		else
			random_factor &= 0xffffffful;
	}

	if (mmap_is_legacy()) {
		mm->mmap_base = TASK_UNMAPPED_BASE + random_factor;
		mm->get_unmapped_area = arch_get_unmapped_area;
		mm->unmap_area = arch_unmap_area;
	} else {
		mm->mmap_base = mmap_base(random_factor);
		mm->get_unmapped_area = arch_get_unmapped_area_topdown;
		mm->unmap_area = arch_unmap_area_topdown;
	}
}

static inline unsigned long brk_rnd(void)
{
	unsigned long rnd = get_random_int();

	rnd = rnd << PAGE_SHIFT;
	/* 8MB for 32bit, 256MB for 64bit */
	if (TASK_IS_32BIT_ADDR)
		rnd = rnd & 0x7ffffful;
	else
		rnd = rnd & 0xffffffful;

	return rnd;
}

unsigned long arch_randomize_brk(struct mm_struct *mm)
{
	unsigned long base = mm->brk;
	unsigned long ret;

	ret = PAGE_ALIGN(base + brk_rnd());

	if (ret < mm->brk)
		return mm->brk;

	return ret;
}
