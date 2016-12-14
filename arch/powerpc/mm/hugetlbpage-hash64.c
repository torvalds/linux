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
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/cacheflush.h>
#include <asm/machdep.h>

extern long hpte_insert_repeating(unsigned long hash, unsigned long vpn,
				  unsigned long pa, unsigned long rlags,
				  unsigned long vflags, int psize, int ssize);

int __hash_page_huge(unsigned long ea, unsigned long access, unsigned long vsid,
		     pte_t *ptep, unsigned long trap, unsigned long flags,
		     int ssize, unsigned int shift, unsigned int mmu_psize)
{
	unsigned long vpn;
	unsigned long old_pte, new_pte;
	unsigned long rflags, pa, sz;
	long slot;

	BUG_ON(shift != mmu_psize_defs[mmu_psize].shift);

	/* Search the Linux page table for a match with va */
	vpn = hpt_vpn(ea, vsid, ssize);

	/* At this point, we have a pte (old_pte) which can be used to build
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

		/* Try to lock the PTE, add ACCESSED and DIRTY if it was
		 * a write access */
		new_pte = old_pte | H_PAGE_BUSY | _PAGE_ACCESSED;
		if (access & _PAGE_WRITE)
			new_pte |= _PAGE_DIRTY;
	} while(!pte_xchg(ptep, __pte(old_pte), __pte(new_pte)));

	rflags = htab_convert_pte_flags(new_pte);

	sz = ((1UL) << shift);
	if (!cpu_has_feature(CPU_FTR_COHERENT_ICACHE))
		/* No CPU has hugepages but lacks no execute, so we
		 * don't need to worry about that case */
		rflags = hash_page_do_lazy_icache(rflags, __pte(old_pte), trap);

	/* Check if pte already has an hpte (case 2) */
	if (unlikely(old_pte & H_PAGE_HASHPTE)) {
		/* There MIGHT be an HPTE for this pte */
		unsigned long hash, slot;

		hash = hpt_hash(vpn, shift, ssize);
		if (old_pte & H_PAGE_F_SECOND)
			hash = ~hash;
		slot = (hash & htab_hash_mask) * HPTES_PER_GROUP;
		slot += (old_pte & H_PAGE_F_GIX) >> H_PAGE_F_GIX_SHIFT;

		if (mmu_hash_ops.hpte_updatepp(slot, rflags, vpn, mmu_psize,
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

		new_pte |= (slot << H_PAGE_F_GIX_SHIFT) &
			(H_PAGE_F_SECOND | H_PAGE_F_GIX);
	}

	/*
	 * No need to use ldarx/stdcx here
	 */
	*ptep = __pte(new_pte & ~H_PAGE_BUSY);
	return 0;
}

#if defined(CONFIG_PPC_64K_PAGES) && defined(CONFIG_DEBUG_VM)
/*
 * This enables us to catch the wrong page directory format
 * Moved here so that we can use WARN() in the call.
 */
int hugepd_ok(hugepd_t hpd)
{
	bool is_hugepd;
	unsigned long hpdval;

	hpdval = hpd_val(hpd);

	/*
	 * We should not find this format in page directory, warn otherwise.
	 */
	is_hugepd = (((hpdval & 0x3) == 0x0) && ((hpdval & HUGEPD_SHIFT_MASK) != 0));
	WARN(is_hugepd, "Found wrong page directory format\n");
	return 0;
}
#endif
