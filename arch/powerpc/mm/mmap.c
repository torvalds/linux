// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  flexible mmap layout support
 *
 * Copyright 2003-2004 Red Hat Inc., Durham, North Carolina.
 * All Rights Reserved.
 *
 * Started by Ingo Molnar <mingo@elte.hu>
 */

#include <linux/personality.h>
#include <linux/mm.h>
#include <linux/random.h>
#include <linux/sched/signal.h>
#include <linux/sched/mm.h>
#include <linux/elf-randomize.h>
#include <linux/security.h>
#include <linux/mman.h>

/*
 * Top of mmap area (just below the process stack).
 *
 * Leave at least a ~128 MB hole.
 */
#define MIN_GAP (128*1024*1024)
#define MAX_GAP (TASK_SIZE/6*5)

static inline int mmap_is_legacy(struct rlimit *rlim_stack)
{
	if (current->personality & ADDR_COMPAT_LAYOUT)
		return 1;

	if (rlim_stack->rlim_cur == RLIM_INFINITY)
		return 1;

	return sysctl_legacy_va_layout;
}

unsigned long arch_mmap_rnd(void)
{
	unsigned long shift, rnd;

	shift = mmap_rnd_bits;
#ifdef CONFIG_COMPAT
	if (is_32bit_task())
		shift = mmap_rnd_compat_bits;
#endif
	rnd = get_random_long() % (1ul << shift);

	return rnd << PAGE_SHIFT;
}

static inline unsigned long stack_maxrandom_size(void)
{
	if (!(current->flags & PF_RANDOMIZE))
		return 0;

	/* 8MB for 32bit, 1GB for 64bit */
	if (is_32bit_task())
		return (1<<23);
	else
		return (1<<30);
}

static inline unsigned long mmap_base(unsigned long rnd,
				      struct rlimit *rlim_stack)
{
	unsigned long gap = rlim_stack->rlim_cur;
	unsigned long pad = stack_maxrandom_size() + stack_guard_gap;

	/* Values close to RLIM_INFINITY can overflow. */
	if (gap + pad > gap)
		gap += pad;

	if (gap < MIN_GAP)
		gap = MIN_GAP;
	else if (gap > MAX_GAP)
		gap = MAX_GAP;

	return PAGE_ALIGN(DEFAULT_MAP_WINDOW - gap - rnd);
}

#ifdef HAVE_ARCH_UNMAPPED_AREA
#ifdef CONFIG_PPC_RADIX_MMU
/*
 * Same function as generic code used only for radix, because we don't need to overload
 * the generic one. But we will have to duplicate, because hash select
 * HAVE_ARCH_UNMAPPED_AREA
 */
static unsigned long
radix__arch_get_unmapped_area(struct file *filp, unsigned long addr,
			     unsigned long len, unsigned long pgoff,
			     unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	int fixed = (flags & MAP_FIXED);
	unsigned long high_limit;
	struct vm_unmapped_area_info info;

	high_limit = DEFAULT_MAP_WINDOW;
	if (addr >= high_limit || (fixed && (addr + len > high_limit)))
		high_limit = TASK_SIZE;

	if (len > high_limit)
		return -ENOMEM;

	if (fixed) {
		if (addr > high_limit - len)
			return -ENOMEM;
		return addr;
	}

	if (addr) {
		addr = PAGE_ALIGN(addr);
		vma = find_vma(mm, addr);
		if (high_limit - len >= addr && addr >= mmap_min_addr &&
		    (!vma || addr + len <= vm_start_gap(vma)))
			return addr;
	}

	info.flags = 0;
	info.length = len;
	info.low_limit = mm->mmap_base;
	info.high_limit = high_limit;
	info.align_mask = 0;

	return vm_unmapped_area(&info);
}

static unsigned long
radix__arch_get_unmapped_area_topdown(struct file *filp,
				     const unsigned long addr0,
				     const unsigned long len,
				     const unsigned long pgoff,
				     const unsigned long flags)
{
	struct vm_area_struct *vma;
	struct mm_struct *mm = current->mm;
	unsigned long addr = addr0;
	int fixed = (flags & MAP_FIXED);
	unsigned long high_limit;
	struct vm_unmapped_area_info info;

	high_limit = DEFAULT_MAP_WINDOW;
	if (addr >= high_limit || (fixed && (addr + len > high_limit)))
		high_limit = TASK_SIZE;

	if (len > high_limit)
		return -ENOMEM;

	if (fixed) {
		if (addr > high_limit - len)
			return -ENOMEM;
		return addr;
	}

	if (addr) {
		addr = PAGE_ALIGN(addr);
		vma = find_vma(mm, addr);
		if (high_limit - len >= addr && addr >= mmap_min_addr &&
		    (!vma || addr + len <= vm_start_gap(vma)))
			return addr;
	}

	info.flags = VM_UNMAPPED_AREA_TOPDOWN;
	info.length = len;
	info.low_limit = max(PAGE_SIZE, mmap_min_addr);
	info.high_limit = mm->mmap_base + (high_limit - DEFAULT_MAP_WINDOW);
	info.align_mask = 0;

	addr = vm_unmapped_area(&info);
	if (!(addr & ~PAGE_MASK))
		return addr;
	VM_BUG_ON(addr != -ENOMEM);

	/*
	 * A failed mmap() very likely causes application failure,
	 * so fall back to the bottom-up function here. This scenario
	 * can happen with large stack limits and large mmap()
	 * allocations.
	 */
	return radix__arch_get_unmapped_area(filp, addr0, len, pgoff, flags);
}
#endif

unsigned long arch_get_unmapped_area(struct file *filp,
				     unsigned long addr,
				     unsigned long len,
				     unsigned long pgoff,
				     unsigned long flags)
{
#ifdef CONFIG_PPC_MM_SLICES
	return slice_get_unmapped_area(addr, len, flags,
				       mm_ctx_user_psize(&current->mm->context), 0);
#else
	BUG();
#endif
}

unsigned long arch_get_unmapped_area_topdown(struct file *filp,
					     const unsigned long addr0,
					     const unsigned long len,
					     const unsigned long pgoff,
					     const unsigned long flags)
{
#ifdef CONFIG_PPC_MM_SLICES
	return slice_get_unmapped_area(addr0, len, flags,
				       mm_ctx_user_psize(&current->mm->context), 1);
#else
	BUG();
#endif
}
#endif /* HAVE_ARCH_UNMAPPED_AREA */

static void radix__arch_pick_mmap_layout(struct mm_struct *mm,
					unsigned long random_factor,
					struct rlimit *rlim_stack)
{
#ifdef CONFIG_PPC_RADIX_MMU
	if (mmap_is_legacy(rlim_stack)) {
		mm->mmap_base = TASK_UNMAPPED_BASE;
		mm->get_unmapped_area = radix__arch_get_unmapped_area;
	} else {
		mm->mmap_base = mmap_base(random_factor, rlim_stack);
		mm->get_unmapped_area = radix__arch_get_unmapped_area_topdown;
	}
#endif
}

/*
 * This function, called very early during the creation of a new
 * process VM image, sets up which VM layout function to use:
 */
void arch_pick_mmap_layout(struct mm_struct *mm, struct rlimit *rlim_stack)
{
	unsigned long random_factor = 0UL;

	if (current->flags & PF_RANDOMIZE)
		random_factor = arch_mmap_rnd();

	if (radix_enabled())
		return radix__arch_pick_mmap_layout(mm, random_factor,
						    rlim_stack);
	/*
	 * Fall back to the standard layout if the personality
	 * bit is set, or if the expected stack growth is unlimited:
	 */
	if (mmap_is_legacy(rlim_stack)) {
		mm->mmap_base = TASK_UNMAPPED_BASE;
		mm->get_unmapped_area = arch_get_unmapped_area;
	} else {
		mm->mmap_base = mmap_base(random_factor, rlim_stack);
		mm->get_unmapped_area = arch_get_unmapped_area_topdown;
	}
}
