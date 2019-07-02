// SPDX-License-Identifier: GPL-2.0-only
/*
 * Based on arch/arm/mm/mmap.c
 *
 * Copyright (C) 2012 ARM Ltd.
 */

#include <linux/elf.h>
#include <linux/fs.h>
#include <linux/memblock.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/export.h>
#include <linux/shm.h>
#include <linux/sched/signal.h>
#include <linux/sched/mm.h>
#include <linux/io.h>
#include <linux/personality.h>
#include <linux/random.h>

#include <asm/cputype.h>

/*
 * Leave enough space between the mmap area and the stack to honour ulimit in
 * the face of randomisation.
 */
#define MIN_GAP (SZ_128M)
#define MAX_GAP	(STACK_TOP/6*5)

static int mmap_is_legacy(struct rlimit *rlim_stack)
{
	if (current->personality & ADDR_COMPAT_LAYOUT)
		return 1;

	if (rlim_stack->rlim_cur == RLIM_INFINITY)
		return 1;

	return sysctl_legacy_va_layout;
}

unsigned long arch_mmap_rnd(void)
{
	unsigned long rnd;

#ifdef CONFIG_COMPAT
	if (test_thread_flag(TIF_32BIT))
		rnd = get_random_long() & ((1UL << mmap_rnd_compat_bits) - 1);
	else
#endif
		rnd = get_random_long() & ((1UL << mmap_rnd_bits) - 1);
	return rnd << PAGE_SHIFT;
}

static unsigned long mmap_base(unsigned long rnd, struct rlimit *rlim_stack)
{
	unsigned long gap = rlim_stack->rlim_cur;
	unsigned long pad = (STACK_RND_MASK << PAGE_SHIFT) + stack_guard_gap;

	/* Values close to RLIM_INFINITY can overflow. */
	if (gap + pad > gap)
		gap += pad;

	if (gap < MIN_GAP)
		gap = MIN_GAP;
	else if (gap > MAX_GAP)
		gap = MAX_GAP;

	return PAGE_ALIGN(STACK_TOP - gap - rnd);
}

/*
 * This function, called very early during the creation of a new process VM
 * image, sets up which VM layout function to use:
 */
void arch_pick_mmap_layout(struct mm_struct *mm, struct rlimit *rlim_stack)
{
	unsigned long random_factor = 0UL;

	if (current->flags & PF_RANDOMIZE)
		random_factor = arch_mmap_rnd();

	/*
	 * Fall back to the standard layout if the personality bit is set, or
	 * if the expected stack growth is unlimited:
	 */
	if (mmap_is_legacy(rlim_stack)) {
		mm->mmap_base = TASK_UNMAPPED_BASE + random_factor;
		mm->get_unmapped_area = arch_get_unmapped_area;
	} else {
		mm->mmap_base = mmap_base(random_factor, rlim_stack);
		mm->get_unmapped_area = arch_get_unmapped_area_topdown;
	}
}

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
	return memblock_is_region_memory(addr, size) &&
	       memblock_is_map_memory(addr);
}

/*
 * Do not allow /dev/mem mappings beyond the supported physical range.
 */
int valid_mmap_phys_addr_range(unsigned long pfn, size_t size)
{
	return !(((pfn << PAGE_SHIFT) + size) & ~PHYS_MASK);
}

#ifdef CONFIG_STRICT_DEVMEM

#include <linux/ioport.h>

/*
 * devmem_is_allowed() checks to see if /dev/mem access to a certain address
 * is valid. The argument is a physical page number.  We mimic x86 here by
 * disallowing access to system RAM as well as device-exclusive MMIO regions.
 * This effectively disable read()/write() on /dev/mem.
 */
int devmem_is_allowed(unsigned long pfn)
{
	if (iomem_is_exclusive(pfn << PAGE_SHIFT))
		return 0;
	if (!page_is_ram(pfn))
		return 1;
	return 0;
}

#endif
