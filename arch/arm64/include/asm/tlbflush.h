/*
 * Based on arch/arm/include/asm/tlbflush.h
 *
 * Copyright (C) 1999-2003 Russell King
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_TLBFLUSH_H
#define __ASM_TLBFLUSH_H

#ifndef __ASSEMBLY__

#include <linux/mm_types.h>
#include <linux/sched.h>
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
#define __TLBI_0(op, arg) asm ("tlbi " #op "\n"				       \
		   ALTERNATIVE("nop\n			nop",		       \
			       "dsb ish\n		tlbi " #op,	       \
			       ARM64_WORKAROUND_REPEAT_TLBI,		       \
			       CONFIG_ARM64_WORKAROUND_REPEAT_TLBI)	       \
			    : : )

#define __TLBI_1(op, arg) asm ("tlbi " #op ", %0\n"			       \
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
	unsigned long asid = __TLBI_VADDR(0, ASID(mm));

	dsb(ishst);
	__tlbi(aside1is, asid);
	__tlbi_user(aside1is, asid);
	dsb(ish);
}

static inline void flush_tlb_page_nosync(struct vm_area_struct *vma,
					 unsigned long uaddr)
{
	unsigned long addr = __TLBI_VADDR(uaddr, ASID(vma->vm_mm));

	dsb(ishst);
	__tlbi(vale1is, addr);
	__tlbi_user(vale1is, addr);
}

static inline void flush_tlb_page(struct vm_area_struct *vma,
				  unsigned long uaddr)
{
	flush_tlb_page_nosync(vma, uaddr);
	dsb(ish);
}

/*
 * This is meant to avoid soft lock-ups on large TLB flushing ranges and not
 * necessarily a performance improvement.
 */
#define MAX_TLBI_OPS	PTRS_PER_PTE

static inline void __flush_tlb_range(struct vm_area_struct *vma,
				     unsigned long start, unsigned long end,
				     unsigned long stride, bool last_level)
{
	unsigned long asid = ASID(vma->vm_mm);
	unsigned long addr;

	start = round_down(start, stride);
	end = round_up(end, stride);

	if ((end - start) >= (MAX_TLBI_OPS * stride)) {
		flush_tlb_mm(vma->vm_mm);
		return;
	}

	/* Convert the stride into units of 4k */
	stride >>= 12;

	start = __TLBI_VADDR(start, asid);
	end = __TLBI_VADDR(end, asid);

	dsb(ishst);
	for (addr = start; addr < end; addr += stride) {
		if (last_level) {
			__tlbi(vale1is, addr);
			__tlbi_user(vale1is, addr);
		} else {
			__tlbi(vae1is, addr);
			__tlbi_user(vae1is, addr);
		}
	}
	dsb(ish);
}

static inline void flush_tlb_range(struct vm_area_struct *vma,
				   unsigned long start, unsigned long end)
{
	/*
	 * We cannot use leaf-only invalidation here, since we may be invalidating
	 * table entries as part of collapsing hugepages or moving page tables.
	 */
	__flush_tlb_range(vma, start, end, PAGE_SIZE, false);
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
}
#endif

#endif
