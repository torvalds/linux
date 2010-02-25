/* MN10300 MMU context allocation and management
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#include <linux/sched.h>
#include <linux/mm.h>
#include <asm/mmu_context.h>
#include <asm/tlbflush.h>

/*
 * list of the MMU contexts last allocated on each CPU
 */
unsigned long mmu_context_cache[NR_CPUS] = {
	[0 ... NR_CPUS - 1] = MMU_CONTEXT_FIRST_VERSION * 2 - 1,
};

/*
 * flush the specified TLB entry
 */
void flush_tlb_page(struct vm_area_struct *vma, unsigned long addr)
{
	unsigned long pteu, cnx, flags;

	addr &= PAGE_MASK;

	/* make sure the context doesn't migrate and defend against
	 * interference from vmalloc'd regions */
	local_irq_save(flags);

	cnx = mm_context(vma->vm_mm);

	if (cnx != MMU_NO_CONTEXT) {
		pteu = addr | (cnx & 0x000000ffUL);
		IPTEU = pteu;
		DPTEU = pteu;
		if (IPTEL & xPTEL_V)
			IPTEL = 0;
		if (DPTEL & xPTEL_V)
			DPTEL = 0;
	}

	local_irq_restore(flags);
}

/*
 * preemptively set a TLB entry
 */
void update_mmu_cache(struct vm_area_struct *vma, unsigned long addr, pte_t *ptep)
{
	unsigned long pteu, ptel, cnx, flags;
	pte_t pte = *ptep;

	addr &= PAGE_MASK;
	ptel = pte_val(pte) & ~(xPTEL_UNUSED1 | xPTEL_UNUSED2);

	/* make sure the context doesn't migrate and defend against
	 * interference from vmalloc'd regions */
	local_irq_save(flags);

	cnx = mm_context(vma->vm_mm);

	if (cnx != MMU_NO_CONTEXT) {
		pteu = addr | (cnx & 0x000000ffUL);
		if (!(pte_val(pte) & _PAGE_NX)) {
			IPTEU = pteu;
			if (IPTEL & xPTEL_V)
				IPTEL = ptel;
		}
		DPTEU = pteu;
		if (DPTEL & xPTEL_V)
			DPTEL = ptel;
	}

	local_irq_restore(flags);
}
