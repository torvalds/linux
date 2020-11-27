// SPDX-License-Identifier: GPL-2.0
/*
 * PPC64 Huge TLB Page Support for hash based MMUs (POWER4 and later)
 *
 * Copyright (C) 2003 David Gibson, IBM Corporation.
 *
 * Based on the IA-32 version:
 * Copyright (C) 2002, Rohit Seth <rohit.seth@intel.com>
 */

#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <asm/cacheflush.h>
#include <asm/machdep.h>

unsigned int hpage_shift;
EXPORT_SYMBOL(hpage_shift);

extern long hpte_insert_repeating(unsigned long hash, unsigned long vpn,
				  unsigned long pa, unsigned long rlags,
				  unsigned long vflags, int psize, int ssize);

int __hash_page_huge(unsigned long ea, unsigned long access, unsigned long vsid,
		     pte_t *ptep, unsigned long trap, unsigned long flags,
		     int ssize, unsigned int shift, unsigned int mmu_psize)
{
	real_pte_t rpte;
	unsigned long vpn;
	unsigned long old_pte, new_pte;
	unsigned long rflags, pa;
	long slot, offset;

	BUG_ON(shift != mmu_psize_defs[mmu_psize].shift);

	/* Search the Linux page table for a match with va */
	vpn = hpt_vpn(ea, vsid, ssize);

	/*
	 * At this point, we have a pte (old_pte) which can be used to build
	 * or update an HPTE. There are 2 cases:
	 *
	 * 1. There is a valid (present) pte with no associated HPTE (this is
	 *	the most common case)
	 * 2. There is a valid (present) pte with an associated HPTE. The
	 *	current values of the pp bits in the HPTE prevent access
	 *	because we are doing software DIRTY bit management and the
	 *	page is currently not DIRTY.
	 */


	do {
		old_pte = pte_val(*ptep);
		/* If PTE busy, retry the access */
		if (unlikely(old_pte & H_PAGE_BUSY))
			return 0;
		/* If PTE permissions don't match, take page fault */
		if (unlikely(!check_pte_access(access, old_pte)))
			return 1;

		/*
		 * Try to lock the PTE, add ACCESSED and DIRTY if it was
		 * a write access
		 */
		new_pte = old_pte | H_PAGE_BUSY | _PAGE_ACCESSED;
		if (access & _PAGE_WRITE)
			new_pte |= _PAGE_DIRTY;
	} while(!pte_xchg(ptep, __pte(old_pte), __pte(new_pte)));

	/* Make sure this is a hugetlb entry */
	if (old_pte & (H_PAGE_THP_HUGE | _PAGE_DEVMAP))
		return 0;

	rflags = htab_convert_pte_flags(new_pte, flags);
	if (unlikely(mmu_psize == MMU_PAGE_16G))
		offset = PTRS_PER_PUD;
	else
		offset = PTRS_PER_PMD;
	rpte = __real_pte(__pte(old_pte), ptep, offset);

	if (!cpu_has_feature(CPU_FTR_COHERENT_ICACHE))
		/*
		 * No CPU has hugepages but lacks no execute, so we
		 * don't need to worry about that case
		 */
		rflags = hash_page_do_lazy_icache(rflags, __pte(old_pte), trap);

	/* Check if pte already has an hpte (case 2) */
	if (unlikely(old_pte & H_PAGE_HASHPTE)) {
		/* There MIGHT be an HPTE for this pte */
		unsigned long gslot;

		gslot = pte_get_hash_gslot(vpn, shift, ssize, rpte, 0);
		if (mmu_hash_ops.hpte_updatepp(gslot, rflags, vpn, mmu_psize,
					       mmu_psize, ssize, flags) == -1)
			old_pte &= ~_PAGE_HPTEFLAGS;
	}

	if (likely(!(old_pte & H_PAGE_HASHPTE))) {
		unsigned long hash = hpt_hash(vpn, shift, ssize);

		pa = pte_pfn(__pte(old_pte)) << PAGE_SHIFT;

		/* clear HPTE slot informations in new PTE */
		new_pte = (new_pte & ~_PAGE_HPTEFLAGS) | H_PAGE_HASHPTE;

		slot = hpte_insert_repeating(hash, vpn, pa, rflags, 0,
					     mmu_psize, ssize);

		/*
		 * Hypervisor failure. Restore old pte and return -1
		 * similar to __hash_page_*
		 */
		if (unlikely(slot == -2)) {
			*ptep = __pte(old_pte);
			hash_failure_debug(ea, access, vsid, trap, ssize,
					   mmu_psize, mmu_psize, old_pte);
			return -1;
		}

		new_pte |= pte_set_hidx(ptep, rpte, 0, slot, offset);
	}

	/*
	 * No need to use ldarx/stdcx here
	 */
	*ptep = __pte(new_pte & ~H_PAGE_BUSY);
	return 0;
}

pte_t huge_ptep_modify_prot_start(struct vm_area_struct *vma,
				  unsigned long addr, pte_t *ptep)
{
	unsigned long pte_val;
	/*
	 * Clear the _PAGE_PRESENT so that no hardware parallel update is
	 * possible. Also keep the pte_present true so that we don't take
	 * wrong fault.
	 */
	pte_val = pte_update(vma->vm_mm, addr, ptep,
			     _PAGE_PRESENT, _PAGE_INVALID, 1);

	return __pte(pte_val);
}

void huge_ptep_modify_prot_commit(struct vm_area_struct *vma, unsigned long addr,
				  pte_t *ptep, pte_t old_pte, pte_t pte)
{

	if (radix_enabled())
		return radix__huge_ptep_modify_prot_commit(vma, addr, ptep,
							   old_pte, pte);
	set_huge_pte_at(vma->vm_mm, addr, ptep, pte);
}

void hugetlbpage_init_default(void)
{
	/* Set default large page size. Currently, we pick 16M or 1M
	 * depending on what is available
	 */
	if (mmu_psize_defs[MMU_PAGE_16M].shift)
		hpage_shift = mmu_psize_defs[MMU_PAGE_16M].shift;
	else if (mmu_psize_defs[MMU_PAGE_1M].shift)
		hpage_shift = mmu_psize_defs[MMU_PAGE_1M].shift;
	else if (mmu_psize_defs[MMU_PAGE_2M].shift)
		hpage_shift = mmu_psize_defs[MMU_PAGE_2M].shift;
}
