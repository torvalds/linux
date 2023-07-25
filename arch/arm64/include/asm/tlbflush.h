/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Based on arch/arm/include/asm/tlbflush.h
 *
 * Copyright (C) 1999-2003 Russell King
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_TLBFLUSH_H
#define __ASM_TLBFLUSH_H

#ifndef __ASSEMBLY__

#include <linux/bitfield.h>
#include <linux/mm_types.h>
#include <linux/sched.h>
#include <linux/mmu_notifier.h>
#include <asm/cputype.h>
#include <asm/mmu.h>

/*
 * Raw TLBI operations.
 *
 * Where necessary, use the __tlbi() macro to avoid asm()
 * boilerplate. Drivers and most kernel code should use the TLB
 * management routines in preference to the macro below.
 *
 * The macro can be used as __tlbi(op) or __tlbi(op, arg), depending
 * on whether a particular TLBI operation takes an argument or
 * not. The macros handles invoking the asm with or without the
 * register argument as appropriate.
 */
#define __TLBI_0(op, arg) asm (ARM64_ASM_PREAMBLE			       \
			       "tlbi " #op "\n"				       \
		   ALTERNATIVE("nop\n			nop",		       \
			       "dsb ish\n		tlbi " #op,	       \
			       ARM64_WORKAROUND_REPEAT_TLBI,		       \
			       CONFIG_ARM64_WORKAROUND_REPEAT_TLBI)	       \
			    : : )

#define __TLBI_1(op, arg) asm (ARM64_ASM_PREAMBLE			       \
			       "tlbi " #op ", %0\n"			       \
		   ALTERNATIVE("nop\n			nop",		       \
			       "dsb ish\n		tlbi " #op ", %0",     \
			       ARM64_WORKAROUND_REPEAT_TLBI,		       \
			       CONFIG_ARM64_WORKAROUND_REPEAT_TLBI)	       \
			    : : "r" (arg))

#define __TLBI_N(op, arg, n, ...) __TLBI_##n(op, arg)

#define __tlbi(op, ...)		__TLBI_N(op, ##__VA_ARGS__, 1, 0)

#define __tlbi_user(op, arg) do {						\
	if (arm64_kernel_unmapped_at_el0())					\
		__tlbi(op, (arg) | USER_ASID_FLAG);				\
} while (0)

/* This macro creates a properly formatted VA operand for the TLBI */
#define __TLBI_VADDR(addr, asid)				\
	({							\
		unsigned long __ta = (addr) >> 12;		\
		__ta &= GENMASK_ULL(43, 0);			\
		__ta |= (unsigned long)(asid) << 48;		\
		__ta;						\
	})

/*
 * Get translation granule of the system, which is decided by
 * PAGE_SIZE.  Used by TTL.
 *  - 4KB	: 1
 *  - 16KB	: 2
 *  - 64KB	: 3
 */
#define TLBI_TTL_TG_4K		1
#define TLBI_TTL_TG_16K		2
#define TLBI_TTL_TG_64K		3

static inline unsigned long get_trans_granule(void)
{
	switch (PAGE_SIZE) {
	case SZ_4K:
		return TLBI_TTL_TG_4K;
	case SZ_16K:
		return TLBI_TTL_TG_16K;
	case SZ_64K:
		return TLBI_TTL_TG_64K;
	default:
		return 0;
	}
}

/*
 * Level-based TLBI operations.
 *
 * When ARMv8.4-TTL exists, TLBI operations take an additional hint for
 * the level at which the invalidation must take place. If the level is
 * wrong, no invalidation may take place. In the case where the level
 * cannot be easily determined, a 0 value for the level parameter will
 * perform a non-hinted invalidation.
 *
 * For Stage-2 invalidation, use the level values provided to that effect
 * in asm/stage2_pgtable.h.
 */
#define TLBI_TTL_MASK		GENMASK_ULL(47, 44)

#define __tlbi_level(op, addr, level) do {				\
	u64 arg = addr;							\
									\
	if (cpus_have_const_cap(ARM64_HAS_ARMv8_4_TTL) &&		\
	    level) {							\
		u64 ttl = level & 3;					\
		ttl |= get_trans_granule() << 2;			\
		arg &= ~TLBI_TTL_MASK;					\
		arg |= FIELD_PREP(TLBI_TTL_MASK, ttl);			\
	}								\
									\
	__tlbi(op, arg);						\
} while(0)

#define __tlbi_user_level(op, arg, level) do {				\
	if (arm64_kernel_unmapped_at_el0())				\
		__tlbi_level(op, (arg | USER_ASID_FLAG), level);	\
} while (0)

/*
 * This macro creates a properly formatted VA operand for the TLB RANGE.
 * The value bit assignments are:
 *
 * +----------+------+-------+-------+-------+----------------------+
 * |   ASID   |  TG  | SCALE |  NUM  |  TTL  |        BADDR         |
 * +-----------------+-------+-------+-------+----------------------+
 * |63      48|47  46|45   44|43   39|38   37|36                   0|
 *
 * The address range is determined by below formula:
 * [BADDR, BADDR + (NUM + 1) * 2^(5*SCALE + 1) * PAGESIZE)
 *
 */
#define __TLBI_VADDR_RANGE(addr, asid, scale, num, ttl)		\
	({							\
		unsigned long __ta = (addr) >> PAGE_SHIFT;	\
		__ta &= GENMASK_ULL(36, 0);			\
		__ta |= (unsigned long)(ttl) << 37;		\
		__ta |= (unsigned long)(num) << 39;		\
		__ta |= (unsigned long)(scale) << 44;		\
		__ta |= get_trans_granule() << 46;		\
		__ta |= (unsigned long)(asid) << 48;		\
		__ta;						\
	})

/* These macros are used by the TLBI RANGE feature. */
#define __TLBI_RANGE_PAGES(num, scale)	\
	((unsigned long)((num) + 1) << (5 * (scale) + 1))
#define MAX_TLBI_RANGE_PAGES		__TLBI_RANGE_PAGES(31, 3)

/*
 * Generate 'num' values from -1 to 30 with -1 rejected by the
 * __flush_tlb_range() loop below.
 */
#define TLBI_RANGE_MASK			GENMASK_ULL(4, 0)
#define __TLBI_RANGE_NUM(pages, scale)	\
	((((pages) >> (5 * (scale) + 1)) & TLBI_RANGE_MASK) - 1)

/*
 *	TLB Invalidation
 *	================
 *
 * 	This header file implements the low-level TLB invalidation routines
 *	(sometimes referred to as "flushing" in the kernel) for arm64.
 *
 *	Every invalidation operation uses the following template:
 *
 *	DSB ISHST	// Ensure prior page-table updates have completed
 *	TLBI ...	// Invalidate the TLB
 *	DSB ISH		// Ensure the TLB invalidation has completed
 *      if (invalidated kernel mappings)
 *		ISB	// Discard any instructions fetched from the old mapping
 *
 *
 *	The following functions form part of the "core" TLB invalidation API,
 *	as documented in Documentation/core-api/cachetlb.rst:
 *
 *	flush_tlb_all()
 *		Invalidate the entire TLB (kernel + user) on all CPUs
 *
 *	flush_tlb_mm(mm)
 *		Invalidate an entire user address space on all CPUs.
 *		The 'mm' argument identifies the ASID to invalidate.
 *
 *	flush_tlb_range(vma, start, end)
 *		Invalidate the virtual-address range '[start, end)' on all
 *		CPUs for the user address space corresponding to 'vma->mm'.
 *		Note that this operation also invalidates any walk-cache
 *		entries associated with translations for the specified address
 *		range.
 *
 *	flush_tlb_kernel_range(start, end)
 *		Same as flush_tlb_range(..., start, end), but applies to
 * 		kernel mappings rather than a particular user address space.
 *		Whilst not explicitly documented, this function is used when
 *		unmapping pages from vmalloc/io space.
 *
 *	flush_tlb_page(vma, addr)
 *		Invalidate a single user mapping for address 'addr' in the
 *		address space corresponding to 'vma->mm'.  Note that this
 *		operation only invalidates a single, last-level page-table
 *		entry and therefore does not affect any walk-caches.
 *
 *
 *	Next, we have some undocumented invalidation routines that you probably
 *	don't want to call unless you know what you're doing:
 *
 *	local_flush_tlb_all()
 *		Same as flush_tlb_all(), but only applies to the calling CPU.
 *
 *	__flush_tlb_kernel_pgtable(addr)
 *		Invalidate a single kernel mapping for address 'addr' on all
 *		CPUs, ensuring that any walk-cache entries associated with the
 *		translation are also invalidated.
 *
 *	__flush_tlb_range(vma, start, end, stride, last_level)
 *		Invalidate the virtual-address range '[start, end)' on all
 *		CPUs for the user address space corresponding to 'vma->mm'.
 *		The invalidation operations are issued at a granularity
 *		determined by 'stride' and only affect any walk-cache entries
 *		if 'last_level' is equal to false.
 *
 *
 *	Finally, take a look at asm/tlb.h to see how tlb_flush() is implemented
 *	on top of these routines, since that is our interface to the mmu_gather
 *	API as used by munmap() and friends.
 */
static inline void local_flush_tlb_all(void)
{
	dsb(nshst);
	__tlbi(vmalle1);
	dsb(nsh);
	isb();
}

static inline void flush_tlb_all(void)
{
	dsb(ishst);
	__tlbi(vmalle1is);
	dsb(ish);
	isb();
}

static inline void flush_tlb_mm(struct mm_struct *mm)
{
	unsigned long asid;

	dsb(ishst);
	asid = __TLBI_VADDR(0, ASID(mm));
	__tlbi(aside1is, asid);
	__tlbi_user(aside1is, asid);
	dsb(ish);
	mmu_notifier_invalidate_range(mm, 0, -1UL);
}

static inline void __flush_tlb_page_nosync(struct mm_struct *mm,
					   unsigned long uaddr)
{
	unsigned long addr;

	dsb(ishst);
	addr = __TLBI_VADDR(uaddr, ASID(mm));
	__tlbi(vale1is, addr);
	__tlbi_user(vale1is, addr);
	mmu_notifier_invalidate_range(mm, uaddr & PAGE_MASK,
						(uaddr & PAGE_MASK) + PAGE_SIZE);
}

static inline void flush_tlb_page_nosync(struct vm_area_struct *vma,
					 unsigned long uaddr)
{
	return __flush_tlb_page_nosync(vma->vm_mm, uaddr);
}

static inline void flush_tlb_page(struct vm_area_struct *vma,
				  unsigned long uaddr)
{
	flush_tlb_page_nosync(vma, uaddr);
	dsb(ish);
}

static inline bool arch_tlbbatch_should_defer(struct mm_struct *mm)
{
#ifdef CONFIG_ARM64_WORKAROUND_REPEAT_TLBI
	/*
	 * TLB flush deferral is not required on systems which are affected by
	 * ARM64_WORKAROUND_REPEAT_TLBI, as __tlbi()/__tlbi_user() implementation
	 * will have two consecutive TLBI instructions with a dsb(ish) in between
	 * defeating the purpose (i.e save overall 'dsb ish' cost).
	 */
	if (unlikely(cpus_have_const_cap(ARM64_WORKAROUND_REPEAT_TLBI)))
		return false;
#endif
	return true;
}

static inline void arch_tlbbatch_add_pending(struct arch_tlbflush_unmap_batch *batch,
					     struct mm_struct *mm,
					     unsigned long uaddr)
{
	__flush_tlb_page_nosync(mm, uaddr);
}

static inline void arch_flush_tlb_batched_pending(struct mm_struct *mm)
{
	dsb(ish);
}

static inline void arch_tlbbatch_flush(struct arch_tlbflush_unmap_batch *batch)
{
	dsb(ish);
}

/*
 * This is meant to avoid soft lock-ups on large TLB flushing ranges and not
 * necessarily a performance improvement.
 */
#define MAX_TLBI_OPS	PTRS_PER_PTE

static inline void __flush_tlb_range(struct vm_area_struct *vma,
				     unsigned long start, unsigned long end,
				     unsigned long stride, bool last_level,
				     int tlb_level)
{
	int num = 0;
	int scale = 0;
	unsigned long asid, addr, pages;

	start = round_down(start, stride);
	end = round_up(end, stride);
	pages = (end - start) >> PAGE_SHIFT;

	/*
	 * When not uses TLB range ops, we can handle up to
	 * (MAX_TLBI_OPS - 1) pages;
	 * When uses TLB range ops, we can handle up to
	 * (MAX_TLBI_RANGE_PAGES - 1) pages.
	 */
	if ((!system_supports_tlb_range() &&
	     (end - start) >= (MAX_TLBI_OPS * stride)) ||
	    pages >= MAX_TLBI_RANGE_PAGES) {
		flush_tlb_mm(vma->vm_mm);
		return;
	}

	dsb(ishst);
	asid = ASID(vma->vm_mm);

	/*
	 * When the CPU does not support TLB range operations, flush the TLB
	 * entries one by one at the granularity of 'stride'. If the TLB
	 * range ops are supported, then:
	 *
	 * 1. If 'pages' is odd, flush the first page through non-range
	 *    operations;
	 *
	 * 2. For remaining pages: the minimum range granularity is decided
	 *    by 'scale', so multiple range TLBI operations may be required.
	 *    Start from scale = 0, flush the corresponding number of pages
	 *    ((num+1)*2^(5*scale+1) starting from 'addr'), then increase it
	 *    until no pages left.
	 *
	 * Note that certain ranges can be represented by either num = 31 and
	 * scale or num = 0 and scale + 1. The loop below favours the latter
	 * since num is limited to 30 by the __TLBI_RANGE_NUM() macro.
	 */
	while (pages > 0) {
		if (!system_supports_tlb_range() ||
		    pages % 2 == 1) {
			addr = __TLBI_VADDR(start, asid);
			if (last_level) {
				__tlbi_level(vale1is, addr, tlb_level);
				__tlbi_user_level(vale1is, addr, tlb_level);
			} else {
				__tlbi_level(vae1is, addr, tlb_level);
				__tlbi_user_level(vae1is, addr, tlb_level);
			}
			start += stride;
			pages -= stride >> PAGE_SHIFT;
			continue;
		}

		num = __TLBI_RANGE_NUM(pages, scale);
		if (num >= 0) {
			addr = __TLBI_VADDR_RANGE(start, asid, scale,
						  num, tlb_level);
			if (last_level) {
				__tlbi(rvale1is, addr);
				__tlbi_user(rvale1is, addr);
			} else {
				__tlbi(rvae1is, addr);
				__tlbi_user(rvae1is, addr);
			}
			start += __TLBI_RANGE_PAGES(num, scale) << PAGE_SHIFT;
			pages -= __TLBI_RANGE_PAGES(num, scale);
		}
		scale++;
	}
	dsb(ish);
	mmu_notifier_invalidate_range(vma->vm_mm, start, end);
}

static inline void flush_tlb_range(struct vm_area_struct *vma,
				   unsigned long start, unsigned long end)
{
	/*
	 * We cannot use leaf-only invalidation here, since we may be invalidating
	 * table entries as part of collapsing hugepages or moving page tables.
	 * Set the tlb_level to 0 because we can not get enough information here.
	 */
	__flush_tlb_range(vma, start, end, PAGE_SIZE, false, 0);
}

static inline void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	unsigned long addr;

	if ((end - start) > (MAX_TLBI_OPS * PAGE_SIZE)) {
		flush_tlb_all();
		return;
	}

	start = __TLBI_VADDR(start, 0);
	end = __TLBI_VADDR(end, 0);

	dsb(ishst);
	for (addr = start; addr < end; addr += 1 << (PAGE_SHIFT - 12))
		__tlbi(vaale1is, addr);
	dsb(ish);
	isb();
}

/*
 * Used to invalidate the TLB (walk caches) corresponding to intermediate page
 * table levels (pgd/pud/pmd).
 */
static inline void __flush_tlb_kernel_pgtable(unsigned long kaddr)
{
	unsigned long addr = __TLBI_VADDR(kaddr, 0);

	dsb(ishst);
	__tlbi(vaae1is, addr);
	dsb(ish);
	isb();
}
#endif

#endif
