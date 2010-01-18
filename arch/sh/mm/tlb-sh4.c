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
	ctrl_outl(vpn, MMU_PTEH);

	pteval = pte.pte_low;

	/* Set PTEA register */
#ifdef CONFIG_X2TLB
	/*
	 * For the extended mode TLB this is trivial, only the ESZ and
	 * EPR bits need to be written out to PTEA, with the remainder of
	 * the protection bits (with the exception of the compat-mode SZ
	 * and PR bits, which are cleared) being written out in PTEL.
	 */
	ctrl_outl(pte.pte_high, MMU_PTEA);
#else
	if (cpu_data->flags & CPU_HAS_PTEA) {
		/* The last 3 bits and the first one of pteval contains
		 * the PTEA timing control and space attribute bits
		 */
		ctrl_outl(copy_ptea_attributes(pteval), MMU_PTEA);
	}
#endif

	/* Set PTEL register */
	pteval &= _PAGE_FLAGS_HARDWARE_MASK; /* drop software flags */
#ifdef CONFIG_CACHE_WRITETHROUGH
	pteval |= _PAGE_WT;
#endif
	/* conveniently, we want all the software flags to be 0 anyway */
	ctrl_outl(pteval, MMU_PTEL);

	/* Load the TLB */
	asm volatile("ldtlb": /* no output */ : /* no input */ : "memory");
	local_irq_restore(flags);
}

void __uses_jump_to_uncached local_flush_tlb_one(unsigned long asid,
						 unsigned long page)
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
	ctrl_outl(data, addr);
	back_to_cached();
}

/*
 * Load the entry for 'addr' into the TLB and wire the entry.
 */
void tlb_wire_entry(struct vm_area_struct *vma, unsigned long addr, pte_t pte)
{
	unsigned long status, flags;
	int urb;

	local_irq_save(flags);

	/* Load the entry into the TLB */
	__update_tlb(vma, addr, pte);

	/* ... and wire it up. */
	status = ctrl_inl(MMUCR);
	urb = (status & MMUCR_URB) >> MMUCR_URB_SHIFT;
	status &= ~MMUCR_URB;

	/*
	 * Make sure we're not trying to wire the last TLB entry slot.
	 */
	BUG_ON(!--urb);

	urb = urb % MMUCR_URB_NENTRIES;

	status |= (urb << MMUCR_URB_SHIFT);
	ctrl_outl(status, MMUCR);
	ctrl_barrier();

	local_irq_restore(flags);
}

/*
 * Unwire the last wired TLB entry.
 *
 * It should also be noted that it is not possible to wire and unwire
 * TLB entries in an arbitrary order. If you wire TLB entry N, followed
 * by entry N+1, you must unwire entry N+1 first, then entry N. In this
 * respect, it works like a stack or LIFO queue.
 */
void tlb_unwire_entry(void)
{
	unsigned long status, flags;
	int urb;

	local_irq_save(flags);

	status = ctrl_inl(MMUCR);
	urb = (status & MMUCR_URB) >> MMUCR_URB_SHIFT;
	status &= ~MMUCR_URB;

	/*
	 * Make sure we're not trying to unwire a TLB entry when none
	 * have been wired.
	 */
	BUG_ON(urb++ == MMUCR_URB_NENTRIES);

	urb = urb % MMUCR_URB_NENTRIES;

	status |= (urb << MMUCR_URB_SHIFT);
	ctrl_outl(status, MMUCR);
	ctrl_barrier();

	local_irq_restore(flags);
}
