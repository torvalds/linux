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

#include <linux/sched.h>
#include <asm/cputype.h>

/*
 *	TLB Management
 *	==============
 *
 *	The TLB specific code is expected to perform whatever tests it needs
 *	to determine if it should invalidate the TLB for each call.  Start
 *	addresses are inclusive and end addresses are exclusive; it is safe to
 *	round these addresses down.
 *
 *	flush_tlb_all()
 *
 *		Invalidate the entire TLB.
 *
 *	flush_tlb_mm(mm)
 *
 *		Invalidate all TLB entries in a particular address space.
 *		- mm	- mm_struct describing address space
 *
 *	flush_tlb_range(mm,start,end)
 *
 *		Invalidate a range of TLB entries in the specified address
 *		space.
 *		- mm	- mm_struct describing address space
 *		- start - start address (may not be aligned)
 *		- end	- end address (exclusive, may not be aligned)
 *
 *	flush_tlb_page(vaddr,vma)
 *
 *		Invalidate the specified page in the specified address range.
 *		- vaddr - virtual address (may not be aligned)
 *		- vma	- vma_struct describing address range
 *
 *	flush_kern_tlb_page(kaddr)
 *
 *		Invalidate the TLB entry for the specified page.  The address
 *		will be in the kernels virtual memory space.  Current uses
 *		only require the D-TLB to be invalidated.
 *		- kaddr - Kernel virtual memory address
 */
static inline void flush_tlb_all(void)
{
	dsb(ishst);
	asm("tlbi	vmalle1is");
	dsb(ish);
	isb();
}

static inline void flush_tlb_mm(struct mm_struct *mm)
{
	unsigned long asid = (unsigned long)ASID(mm) << 48;

	dsb(ishst);
	asm("tlbi	aside1is, %0" : : "r" (asid));
	dsb(ish);
}

static inline void flush_tlb_page(struct vm_area_struct *vma,
				  unsigned long uaddr)
{
	unsigned long addr = uaddr >> 12 |
		((unsigned long)ASID(vma->vm_mm) << 48);

	dsb(ishst);
	asm("tlbi	vale1is, %0" : : "r" (addr));
	dsb(ish);
}

/*
 * This is meant to avoid soft lock-ups on large TLB flushing ranges and not
 * necessarily a performance improvement.
 */
#define MAX_TLB_RANGE	(1024UL << PAGE_SHIFT)

static inline void __flush_tlb_range(struct vm_area_struct *vma,
				     unsigned long start, unsigned long end,
				     bool last_level)
{
	unsigned long asid = (unsigned long)ASID(vma->vm_mm) << 48;
	unsigned long addr;

	if ((end - start) > MAX_TLB_RANGE) {
		flush_tlb_mm(vma->vm_mm);
		return;
	}

	start = asid | (start >> 12);
	end = asid | (end >> 12);

	dsb(ishst);
	for (addr = start; addr < end; addr += 1 << (PAGE_SHIFT - 12)) {
		if (last_level)
			asm("tlbi vale1is, %0" : : "r"(addr));
		else
			asm("tlbi vae1is, %0" : : "r"(addr));
	}
	dsb(ish);
}

static inline void flush_tlb_range(struct vm_area_struct *vma,
				   unsigned long start, unsigned long end)
{
	__flush_tlb_range(vma, start, end, false);
}

static inline void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	unsigned long addr;

	if ((end - start) > MAX_TLB_RANGE) {
		flush_tlb_all();
		return;
	}

	start >>= 12;
	end >>= 12;

	dsb(ishst);
	for (addr = start; addr < end; addr += 1 << (PAGE_SHIFT - 12))
		asm("tlbi vaae1is, %0" : : "r"(addr));
	dsb(ish);
	isb();
}

/*
 * Used to invalidate the TLB (walk caches) corresponding to intermediate page
 * table levels (pgd/pud/pmd).
 */
static inline void __flush_tlb_pgtable(struct mm_struct *mm,
				       unsigned long uaddr)
{
	unsigned long addr = uaddr >> 12 | ((unsigned long)ASID(mm) << 48);

	dsb(ishst);
	asm("tlbi	vae1is, %0" : : "r" (addr));
	dsb(ish);
}

#endif

#endif
