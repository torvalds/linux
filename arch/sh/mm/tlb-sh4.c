/*
 * arch/sh/mm/tlb-sh4.c
 *
 * SH-4 specific TLB operations
 *
 * Copyright (C) 1999  Niibe Yutaka
 * Copyright (C) 2002  Paul Mundt
 *
 * Released under the terms of the GNU GPL v2.0.
 */
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/interrupt.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/pgalloc.h>
#include <asm/mmu_context.h>
#include <asm/cacheflush.h>

void update_mmu_cache(struct vm_area_struct * vma,
		      unsigned long address, pte_t pte)
{
	unsigned long flags;
	unsigned long pteval;
	unsigned long vpn;
	struct page *page;
	unsigned long pfn;

	/* Ptrace may call this routine. */
	if (vma && current->active_mm != vma->vm_mm)
		return;

	pfn = pte_pfn(pte);
	if (pfn_valid(pfn)) {
		page = pfn_to_page(pfn);
		if (!test_bit(PG_mapped, &page->flags)) {
			unsigned long phys = pte_val(pte) & PTE_PHYS_MASK;
			__flush_wback_region((void *)P1SEGADDR(phys), PAGE_SIZE);
			__set_bit(PG_mapped, &page->flags);
		}
	}

	local_irq_save(flags);

	/* Set PTEH register */
	vpn = (address & MMU_VPN_MASK) | get_asid();
	ctrl_outl(vpn, MMU_PTEH);

	pteval = pte_val(pte);

	/* Set PTEA register */
	if (cpu_data->flags & CPU_HAS_PTEA)
		/* TODO: make this look less hacky */
		ctrl_outl(((pteval >> 28) & 0xe) | (pteval & 0x1), MMU_PTEA);

	/* Set PTEL register */
	pteval &= _PAGE_FLAGS_HARDWARE_MASK; /* drop software flags */
#ifdef CONFIG_SH_WRITETHROUGH
	pteval |= _PAGE_WT;
#endif
	/* conveniently, we want all the software flags to be 0 anyway */
	ctrl_outl(pteval, MMU_PTEL);

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
	jump_to_P2();
	ctrl_outl(data, addr);
	back_to_P1();
}

