/*
 * arch/sh/mm/tlb-pteaex.c
 *
 * TLB operations for SH-X3 CPUs featuring PTE ASID Extensions.
 *
 * Copyright (C) 2009 Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <asm/system.h>
#include <asm/mmu_context.h>
#include <asm/cacheflush.h>

void update_mmu_cache(struct vm_area_struct * vma,
		      unsigned long address, pte_t pte)
{
	unsigned long flags;
	unsigned long pteval;
	unsigned long vpn;

	/* Ptrace may call this routine. */
	if (vma && current->active_mm != vma->vm_mm)
		return;

#ifndef CONFIG_CACHE_OFF
	{
		unsigned long pfn = pte_pfn(pte);

		if (pfn_valid(pfn)) {
			struct page *page = pfn_to_page(pfn);

			if (!test_bit(PG_mapped, &page->flags)) {
				unsigned long phys = pte_val(pte) & PTE_PHYS_MASK;
				__flush_wback_region((void *)P1SEGADDR(phys),
						     PAGE_SIZE);
				__set_bit(PG_mapped, &page->flags);
			}
		}
	}
#endif

	local_irq_save(flags);

	/* Set PTEH register */
	vpn = address & MMU_VPN_MASK;
	__raw_writel(vpn, MMU_PTEH);

	/* Set PTEAEX */
	__raw_writel(get_asid(), MMU_PTEAEX);

	pteval = pte.pte_low;

	/* Set PTEA register */
#ifdef CONFIG_X2TLB
	/*
	 * For the extended mode TLB this is trivial, only the ESZ and
	 * EPR bits need to be written out to PTEA, with the remainder of
	 * the protection bits (with the exception of the compat-mode SZ
	 * and PR bits, which are cleared) being written out in PTEL.
	 */
	__raw_writel(pte.pte_high, MMU_PTEA);
#endif

	/* Set PTEL register */
	pteval &= _PAGE_FLAGS_HARDWARE_MASK; /* drop software flags */
#ifdef CONFIG_CACHE_WRITETHROUGH
	pteval |= _PAGE_WT;
#endif
	/* conveniently, we want all the software flags to be 0 anyway */
	__raw_writel(pteval, MMU_PTEL);

	/* Load the TLB */
	asm volatile("ldtlb": /* no output */ : /* no input */ : "memory");
	local_irq_restore(flags);
}

/*
 * While SH-X2 extended TLB mode splits out the memory-mapped I/UTLB
 * data arrays, SH-X3 cores with PTEAEX split out the memory-mapped
 * address arrays. In compat mode the second array is inaccessible, while
 * in extended mode, the legacy 8-bit ASID field in address array 1 has
 * undefined behaviour.
 */
void __uses_jump_to_uncached local_flush_tlb_one(unsigned long asid,
						 unsigned long page)
{
	jump_to_uncached();
	__raw_writel(page, MMU_UTLB_ADDRESS_ARRAY | MMU_PAGE_ASSOC_BIT);
	__raw_writel(asid, MMU_UTLB_ADDRESS_ARRAY2 | MMU_PAGE_ASSOC_BIT);
	back_to_cached();
}
