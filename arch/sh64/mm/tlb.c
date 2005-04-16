/*
 * arch/sh64/mm/tlb.c
 *
 * Copyright (C) 2003  Paul Mundt <lethal@linux-sh.org>
 * Copyright (C) 2003  Richard Curnow <richard.curnow@superh.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 */
#include <linux/mm.h>
#include <linux/init.h>
#include <asm/page.h>
#include <asm/tlb.h>
#include <asm/mmu_context.h>

/**
 * sh64_tlb_init
 *
 * Perform initial setup for the DTLB and ITLB.
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
 * sh64_next_free_dtlb_entry
 *
 * Find the next available DTLB entry
 */
unsigned long long sh64_next_free_dtlb_entry(void)
{
	return cpu_data->dtlb.next;
}

/**
 * sh64_get_wired_dtlb_entry
 *
 * Allocate a wired (locked-in) entry in the DTLB
 */
unsigned long long sh64_get_wired_dtlb_entry(void)
{
	unsigned long long entry = sh64_next_free_dtlb_entry();

	cpu_data->dtlb.first += cpu_data->dtlb.step;
	cpu_data->dtlb.next  += cpu_data->dtlb.step;

	return entry;
}

/**
 * sh64_put_wired_dtlb_entry
 *
 * @entry:	Address of TLB slot.
 *
 * Free a wired (locked-in) entry in the DTLB.
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
 * sh64_setup_tlb_slot
 *
 * @config_addr:	Address of TLB slot.
 * @eaddr:		Virtual address.
 * @asid:		Address Space Identifier.
 * @paddr:		Physical address.
 *
 * Load up a virtual<->physical translation for @eaddr<->@paddr in the
 * pre-allocated TLB slot @config_addr (see sh64_get_wired_dtlb_entry).
 */
inline void sh64_setup_tlb_slot(unsigned long long config_addr,
				unsigned long eaddr,
				unsigned long asid,
				unsigned long paddr)
{
	unsigned long long pteh, ptel;

	/* Sign extension */
#if (NEFF == 32)
	pteh = (unsigned long long)(signed long long)(signed long) eaddr;
#else
#error "Can't sign extend more than 32 bits yet"
#endif
	pteh &= PAGE_MASK;
	pteh |= (asid << PTEH_ASID_SHIFT) | PTEH_VALID;
#if (NEFF == 32)
	ptel = (unsigned long long)(signed long long)(signed long) paddr;
#else
#error "Can't sign extend more than 32 bits yet"
#endif
	ptel &= PAGE_MASK;
	ptel |= (_PAGE_CACHABLE | _PAGE_READ | _PAGE_WRITE);

	asm volatile("putcfg %0, 1, %1\n\t"
			"putcfg %0, 0, %2\n"
			: : "r" (config_addr), "r" (ptel), "r" (pteh));
}

/**
 * sh64_teardown_tlb_slot
 *
 * @config_addr:	Address of TLB slot.
 *
 * Teardown any existing mapping in the TLB slot @config_addr.
 */
inline void sh64_teardown_tlb_slot(unsigned long long config_addr)
	__attribute__ ((alias("__flush_tlb_slot")));

