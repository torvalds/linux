/*
 * arch/sh/mm/tlb-sh5.c
 *
 * Copyright (C) 2003  Paul Mundt <lethal@linux-sh.org>
 * Copyright (C) 2003  Richard Curnow <richard.curnow@superh.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/mm.h>
#include <linux/init.h>
#include <asm/page.h>
#include <asm/tlb.h>
#include <asm/mmu_context.h>

/**
 * sh64_tlb_init - Perform initial setup for the DTLB and ITLB.
 */
int __init sh64_tlb_init(void)
{
	/* Assign some sane DTLB defaults */
	cpu_data->dtlb.entries	= 64;
	cpu_data->dtlb.step	= 0x10;

	cpu_data->dtlb.first	= DTLB_FIXED | cpu_data->dtlb.step;
	cpu_data->dtlb.next	= cpu_data->dtlb.first;

	cpu_data->dtlb.last	= DTLB_FIXED |
				  ((cpu_data->dtlb.entries - 1) *
				   cpu_data->dtlb.step);

	/* And again for the ITLB */
	cpu_data->itlb.entries	= 64;
	cpu_data->itlb.step	= 0x10;

	cpu_data->itlb.first	= ITLB_FIXED | cpu_data->itlb.step;
	cpu_data->itlb.next	= cpu_data->itlb.first;
	cpu_data->itlb.last	= ITLB_FIXED |
				  ((cpu_data->itlb.entries - 1) *
				   cpu_data->itlb.step);

	return 0;
}

/**
 * sh64_next_free_dtlb_entry - Find the next available DTLB entry
 */
unsigned long long sh64_next_free_dtlb_entry(void)
{
	return cpu_data->dtlb.next;
}

/**
 * sh64_get_wired_dtlb_entry - Allocate a wired (locked-in) entry in the DTLB
 */
unsigned long long sh64_get_wired_dtlb_entry(void)
{
	unsigned long long entry = sh64_next_free_dtlb_entry();

	cpu_data->dtlb.first += cpu_data->dtlb.step;
	cpu_data->dtlb.next  += cpu_data->dtlb.step;

	return entry;
}

/**
 * sh64_put_wired_dtlb_entry - Free a wired (locked-in) entry in the DTLB.
 *
 * @entry:	Address of TLB slot.
 *
 * Works like a stack, last one to allocate must be first one to free.
 */
int sh64_put_wired_dtlb_entry(unsigned long long entry)
{
	__flush_tlb_slot(entry);

	/*
	 * We don't do any particularly useful tracking of wired entries,
	 * so this approach works like a stack .. last one to be allocated
	 * has to be the first one to be freed.
	 *
	 * We could potentially load wired entries into a list and work on
	 * rebalancing the list periodically (which also entails moving the
	 * contents of a TLB entry) .. though I have a feeling that this is
	 * more trouble than it's worth.
	 */

	/*
	 * Entry must be valid .. we don't want any ITLB addresses!
	 */
	if (entry <= DTLB_FIXED)
		return -EINVAL;

	/*
	 * Next, check if we're within range to be freed. (ie, must be the
	 * entry beneath the first 'free' entry!
	 */
	if (entry < (cpu_data->dtlb.first - cpu_data->dtlb.step))
		return -EINVAL;

	/* If we are, then bring this entry back into the list */
	cpu_data->dtlb.first	-= cpu_data->dtlb.step;
	cpu_data->dtlb.next	= entry;

	return 0;
}

/**
 * sh64_setup_tlb_slot - Load up a translation in a wired slot.
 *
 * @config_addr:	Address of TLB slot.
 * @eaddr:		Virtual address.
 * @asid:		Address Space Identifier.
 * @paddr:		Physical address.
 *
 * Load up a virtual<->physical translation for @eaddr<->@paddr in the
 * pre-allocated TLB slot @config_addr (see sh64_get_wired_dtlb_entry).
 */
void sh64_setup_tlb_slot(unsigned long long config_addr, unsigned long eaddr,
			 unsigned long asid, unsigned long paddr)
{
	unsigned long long pteh, ptel;

	pteh = neff_sign_extend(eaddr);
	pteh &= PAGE_MASK;
	pteh |= (asid << PTEH_ASID_SHIFT) | PTEH_VALID;
	ptel = neff_sign_extend(paddr);
	ptel &= PAGE_MASK;
	ptel |= (_PAGE_CACHABLE | _PAGE_READ | _PAGE_WRITE);

	asm volatile("putcfg %0, 1, %1\n\t"
			"putcfg %0, 0, %2\n"
			: : "r" (config_addr), "r" (ptel), "r" (pteh));
}

/**
 * sh64_teardown_tlb_slot - Teardown a translation.
 *
 * @config_addr:	Address of TLB slot.
 *
 * Teardown any existing mapping in the TLB slot @config_addr.
 */
void sh64_teardown_tlb_slot(unsigned long long config_addr)
	__attribute__ ((alias("__flush_tlb_slot")));

static int dtlb_entry;
static unsigned long long dtlb_entries[64];

void tlb_wire_entry(struct vm_area_struct *vma, unsigned long addr, pte_t pte)
{
	unsigned long long entry;
	unsigned long paddr, flags;

	BUG_ON(dtlb_entry == ARRAY_SIZE(dtlb_entries));

	local_irq_save(flags);

	entry = sh64_get_wired_dtlb_entry();
	dtlb_entries[dtlb_entry++] = entry;

	paddr = pte_val(pte) & _PAGE_FLAGS_HARDWARE_MASK;
	paddr &= ~PAGE_MASK;

	sh64_setup_tlb_slot(entry, addr, get_asid(), paddr);

	local_irq_restore(flags);
}

void tlb_unwire_entry(void)
{
	unsigned long long entry;
	unsigned long flags;

	BUG_ON(!dtlb_entry);

	local_irq_save(flags);
	entry = dtlb_entries[dtlb_entry--];

	sh64_teardown_tlb_slot(entry);
	sh64_put_wired_dtlb_entry(entry);

	local_irq_restore(flags);
}

void __update_tlb(struct vm_area_struct *vma, unsigned long address, pte_t pte)
{
	unsigned long long ptel;
	unsigned long long pteh=0;
	struct tlb_info *tlbp;
	unsigned long long next;
	unsigned int fault_code = get_thread_fault_code();

	/* Get PTEL first */
	ptel = pte.pte_low;

	/*
	 * Set PTEH register
	 */
	pteh = neff_sign_extend(address & MMU_VPN_MASK);

	/* Set the ASID. */
	pteh |= get_asid() << PTEH_ASID_SHIFT;
	pteh |= PTEH_VALID;

	/* Set PTEL register, set_pte has performed the sign extension */
	ptel &= _PAGE_FLAGS_HARDWARE_MASK; /* drop software flags */

	if (fault_code & FAULT_CODE_ITLB)
		tlbp = &cpu_data->itlb;
	else
		tlbp = &cpu_data->dtlb;

	next = tlbp->next;
	__flush_tlb_slot(next);
	asm volatile ("putcfg %0,1,%2\n\n\t"
		      "putcfg %0,0,%1\n"
		      :  : "r" (next), "r" (pteh), "r" (ptel) );

	next += TLB_STEP;
	if (next > tlbp->last)
		next = tlbp->first;
	tlbp->next = next;
}
