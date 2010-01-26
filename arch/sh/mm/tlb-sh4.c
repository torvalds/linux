/*
 * arch/sh/mm/tlb-sh4.c
 *
 * SH-4 specific TLB operations
 *
 * Copyright (C) 1999  Niibe Yutaka
 * Copyright (C) 2002 - 2007 Paul Mundt
 *
 * Released under the terms of the GNU GPL v2.0.
 */
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <asm/system.h>
#include <asm/mmu_context.h>
#include <asm/cacheflush.h>

void __update_tlb(struct vm_area_struct *vma, unsigned long address, pte_t pte)
{
	unsigned long flags, pteval, vpn;

	/*
	 * Handle debugger faulting in for debugee.
	 */
	if (vma && current->active_mm != vma->vm_mm)
		return;

	local_irq_save(flags);

	/* Set PTEH register */
	vpn = (address & MMU_VPN_MASK) | get_asid();
	__raw_writel(vpn, MMU_PTEH);

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
#else
	if (cpu_data->flags & CPU_HAS_PTEA) {
		/* The last 3 bits and the first one of pteval contains
		 * the PTEA timing control and space attribute bits
		 */
		__raw_writel(copy_ptea_attributes(pteval), MMU_PTEA);
	}
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

void local_flush_tlb_one(unsigned long asid, unsigned long page)
{
	unsigned long addr, data;

	/*
	 * NOTE: PTEH.ASID should be set to this MM
	 *       _AND_ we need to write ASID to the array.
	 *
	 * It would be simple if we didn't need to set PTEH.ASID...
	 */
	addr = MMU_UTLB_ADDRESS_ARRAY | MMU_PAGE_ASSOC_BIT;
	data = page | asid; /* VALID bit is off */
	jump_to_uncached();
	__raw_writel(data, addr);
	back_to_cached();
}
