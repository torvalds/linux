/*
 * Copyright IBM Corporation, 2015
 * Author Aneesh Kumar K.V <aneesh.kumar@linux.vnet.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU Lesser General Public License
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 */

#include <linux/mm.h>
#include <asm/machdep.h>
#include <asm/mmu.h>

int __hash_page_4K(unsigned long ea, unsigned long access, unsigned long vsid,
		   pte_t *ptep, unsigned long trap, unsigned long flags,
		   int ssize, int subpg_prot)
{
	real_pte_t rpte;
	unsigned long *hidxp;
	unsigned long hpte_group;
	unsigned int subpg_index;
	unsigned long rflags, pa, hidx;
	unsigned long old_pte, new_pte, subpg_pte;
	unsigned long vpn, hash, slot;
	unsigned long shift = mmu_psize_defs[MMU_PAGE_4K].shift;

	/*
	 * atomically mark the linux large page PTE busy and dirty
	 */
	do {
		pte_t pte = READ_ONCE(*ptep);

		old_pte = pte_val(pte);
		/* If PTE busy, retry the access */
		if (unlikely(old_pte & _PAGE_BUSY))
			return 0;
		/* If PTE permissions don't match, take page fault */
		if (unlikely(access & ~old_pte))
			return 1;
		/*
		 * Try to lock the PTE, add ACCESSED and DIRTY if it was
		 * a write access. Since this is 4K insert of 64K page size
		 * also add _PAGE_COMBO
		 */
		new_pte = old_pte | _PAGE_BUSY | _PAGE_ACCESSED | _PAGE_COMBO;
		if (access & _PAGE_RW)
			new_pte |= _PAGE_DIRTY;
	} while (old_pte != __cmpxchg_u64((unsigned long *)ptep,
					  old_pte, new_pte));
	/*
	 * Handle the subpage protection bits
	 */
	subpg_pte = new_pte & ~subpg_prot;
	/*
	 * PP bits. _PAGE_USER is already PP bit 0x2, so we only
	 * need to add in 0x1 if it's a read-only user page
	 */
	rflags = subpg_pte & _PAGE_USER;
	if ((subpg_pte & _PAGE_USER) && !((subpg_pte & _PAGE_RW) &&
					(subpg_pte & _PAGE_DIRTY)))
		rflags |= 0x1;
	/*
	 * _PAGE_EXEC -> HW_NO_EXEC since it's inverted
	 */
	rflags |= ((subpg_pte & _PAGE_EXEC) ? 0 : HPTE_R_N);
	/*
	 * Always add C and Memory coherence bit
	 */
	rflags |= HPTE_R_C | HPTE_R_M;
	/*
	 * Add in WIMG bits
	 */
	rflags |= (subpg_pte & (_PAGE_WRITETHRU | _PAGE_NO_CACHE |
				_PAGE_COHERENT | _PAGE_GUARDED));

	if (!cpu_has_feature(CPU_FTR_NOEXECUTE) &&
	    !cpu_has_feature(CPU_FTR_COHERENT_ICACHE)) {

		/*
		 * No CPU has hugepages but lacks no execute, so we
		 * don't need to worry about that case
		 */
		rflags = hash_page_do_lazy_icache(rflags, __pte(old_pte), trap);
	}

	subpg_index = (ea & (PAGE_SIZE - 1)) >> shift;
	vpn  = hpt_vpn(ea, vsid, ssize);
	rpte = __real_pte(__pte(old_pte), ptep);
	/*
	 *None of the sub 4k page is hashed
	 */
	if (!(old_pte & _PAGE_HASHPTE))
		goto htab_insert_hpte;
	/*
	 * Check if the pte was already inserted into the hash table
	 * as a 64k HW page, and invalidate the 64k HPTE if so.
	 */
	if (!(old_pte & _PAGE_COMBO)) {
		flush_hash_page(vpn, rpte, MMU_PAGE_64K, ssize, flags);
		old_pte &= ~_PAGE_HPTE_SUB;
		goto htab_insert_hpte;
	}
	/*
	 * Check for sub page valid and update
	 */
	if (__rpte_sub_valid(rpte, subpg_index)) {
		int ret;

		hash = hpt_hash(vpn, shift, ssize);
		hidx = __rpte_to_hidx(rpte, subpg_index);
		if (hidx & _PTEIDX_SECONDARY)
			hash = ~hash;
		slot = (hash & htab_hash_mask) * HPTES_PER_GROUP;
		slot += hidx & _PTEIDX_GROUP_IX;

		ret = ppc_md.hpte_updatepp(slot, rflags, vpn,
					   MMU_PAGE_4K, MMU_PAGE_4K,
					   ssize, flags);
		/*
		 *if we failed because typically the HPTE wasn't really here
		 * we try an insertion.
		 */
		if (ret == -1)
			goto htab_insert_hpte;

		*ptep = __pte(new_pte & ~_PAGE_BUSY);
		return 0;
	}

htab_insert_hpte:
	/*
	 * handle _PAGE_4K_PFN case
	 */
	if (old_pte & _PAGE_4K_PFN) {
		/*
		 * All the sub 4k page have the same
		 * physical address.
		 */
		pa = pte_pfn(__pte(old_pte)) << HW_PAGE_SHIFT;
	} else {
		pa = pte_pfn(__pte(old_pte)) << PAGE_SHIFT;
		pa += (subpg_index << shift);
	}
	hash = hpt_hash(vpn, shift, ssize);
repeat:
	hpte_group = ((hash & htab_hash_mask) * HPTES_PER_GROUP) & ~0x7UL;

	/* Insert into the hash table, primary slot */
	slot = ppc_md.hpte_insert(hpte_group, vpn, pa, rflags, 0,
				  MMU_PAGE_4K, MMU_PAGE_4K, ssize);
	/*
	 * Primary is full, try the secondary
	 */
	if (unlikely(slot == -1)) {
		hpte_group = ((~hash & htab_hash_mask) * HPTES_PER_GROUP) & ~0x7UL;
		slot = ppc_md.hpte_insert(hpte_group, vpn, pa,
					  rflags, HPTE_V_SECONDARY,
					  MMU_PAGE_4K, MMU_PAGE_4K, ssize);
		if (slot == -1) {
			if (mftb() & 0x1)
				hpte_group = ((hash & htab_hash_mask) *
					      HPTES_PER_GROUP) & ~0x7UL;
			ppc_md.hpte_remove(hpte_group);
			/*
			 * FIXME!! Should be try the group from which we removed ?
			 */
			goto repeat;
		}
	}
	/*
	 * Hypervisor failure. Restore old pmd and return -1
	 * similar to __hash_page_*
	 */
	if (unlikely(slot == -2)) {
		*ptep = __pte(old_pte);
		hash_failure_debug(ea, access, vsid, trap, ssize,
				   MMU_PAGE_4K, MMU_PAGE_4K, old_pte);
		return -1;
	}
	/*
	 * Insert slot number & secondary bit in PTE second half,
	 * clear _PAGE_BUSY and set appropriate HPTE slot bit
	 * Since we have _PAGE_BUSY set on ptep, we can be sure
	 * nobody is undating hidx.
	 */
	hidxp = (unsigned long *)(ptep + PTRS_PER_PTE);
	/* __real_pte use pte_val() any idea why ? FIXME!! */
	rpte.hidx &= ~(0xfUL << (subpg_index << 2));
	*hidxp = rpte.hidx  | (slot << (subpg_index << 2));
	new_pte |= (_PAGE_HPTE_SUB0 >> subpg_index);
	/*
	 * check __real_pte for details on matching smp_rmb()
	 */
	smp_wmb();
	*ptep = __pte(new_pte & ~_PAGE_BUSY);
	return 0;
}
