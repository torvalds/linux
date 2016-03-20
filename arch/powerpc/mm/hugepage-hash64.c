/*
 * Copyright IBM Corporation, 2013
 * Author Aneesh Kumar K.V <aneesh.kumar@linux.vnet.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2.1 of the GNU Lesser General Public License
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 */

/*
 * PPC64 THP Support for hash based MMUs
 */
#include <linux/mm.h>
#include <asm/machdep.h>

int __hash_page_thp(unsigned long ea, unsigned long access, unsigned long vsid,
		    pmd_t *pmdp, unsigned long trap, unsigned long flags,
		    int ssize, unsigned int psize)
{
	unsigned int index, valid;
	unsigned char *hpte_slot_array;
	unsigned long rflags, pa, hidx;
	unsigned long old_pmd, new_pmd;
	int ret, lpsize = MMU_PAGE_16M;
	unsigned long vpn, hash, shift, slot;

	/*
	 * atomically mark the linux large page PMD busy and dirty
	 */
	do {
		pmd_t pmd = READ_ONCE(*pmdp);

		old_pmd = pmd_val(pmd);
		/* If PMD busy, retry the access */
		if (unlikely(old_pmd & _PAGE_BUSY))
			return 0;
		/* If PMD permissions don't match, take page fault */
		if (unlikely(access & ~old_pmd))
			return 1;
		/*
		 * Try to lock the PTE, add ACCESSED and DIRTY if it was
		 * a write access
		 */
		new_pmd = old_pmd | _PAGE_BUSY | _PAGE_ACCESSED;
		if (access & _PAGE_RW)
			new_pmd |= _PAGE_DIRTY;
	} while (old_pmd != __cmpxchg_u64((unsigned long *)pmdp,
					  old_pmd, new_pmd));
	rflags = htab_convert_pte_flags(new_pmd);

#if 0
	if (!cpu_has_feature(CPU_FTR_COHERENT_ICACHE)) {

		/*
		 * No CPU has hugepages but lacks no execute, so we
		 * don't need to worry about that case
		 */
		rflags = hash_page_do_lazy_icache(rflags, __pte(old_pte), trap);
	}
#endif
	/*
	 * Find the slot index details for this ea, using base page size.
	 */
	shift = mmu_psize_defs[psize].shift;
	index = (ea & ~HPAGE_PMD_MASK) >> shift;
	BUG_ON(index >= PTE_FRAG_SIZE);

	vpn = hpt_vpn(ea, vsid, ssize);
	hpte_slot_array = get_hpte_slot_array(pmdp);
	if (psize == MMU_PAGE_4K) {
		/*
		 * invalidate the old hpte entry if we have that mapped via 64K
		 * base page size. This is because demote_segment won't flush
		 * hash page table entries.
		 */
		if ((old_pmd & _PAGE_HASHPTE) && !(old_pmd & _PAGE_COMBO)) {
			flush_hash_hugepage(vsid, ea, pmdp, MMU_PAGE_64K,
					    ssize, flags);
			/*
			 * With THP, we also clear the slot information with
			 * respect to all the 64K hash pte mapping the 16MB
			 * page. They are all invalid now. This make sure we
			 * don't find the slot valid when we fault with 4k
			 * base page size.
			 *
			 */
			memset(hpte_slot_array, 0, PTE_FRAG_SIZE);
		}
	}

	valid = hpte_valid(hpte_slot_array, index);
	if (valid) {
		/* update the hpte bits */
		hash = hpt_hash(vpn, shift, ssize);
		hidx =  hpte_hash_index(hpte_slot_array, index);
		if (hidx & _PTEIDX_SECONDARY)
			hash = ~hash;
		slot = (hash & htab_hash_mask) * HPTES_PER_GROUP;
		slot += hidx & _PTEIDX_GROUP_IX;

		ret = ppc_md.hpte_updatepp(slot, rflags, vpn,
					   psize, lpsize, ssize, flags);
		/*
		 * We failed to update, try to insert a new entry.
		 */
		if (ret == -1) {
			/*
			 * large pte is marked busy, so we can be sure
			 * nobody is looking at hpte_slot_array. hence we can
			 * safely update this here.
			 */
			valid = 0;
			hpte_slot_array[index] = 0;
		}
	}

	if (!valid) {
		unsigned long hpte_group;

		hash = hpt_hash(vpn, shift, ssize);
		/* insert new entry */
		pa = pmd_pfn(__pmd(old_pmd)) << PAGE_SHIFT;
		new_pmd |= _PAGE_HASHPTE;

repeat:
		hpte_group = ((hash & htab_hash_mask) * HPTES_PER_GROUP) & ~0x7UL;

		/* Insert into the hash table, primary slot */
		slot = ppc_md.hpte_insert(hpte_group, vpn, pa, rflags, 0,
					  psize, lpsize, ssize);
		/*
		 * Primary is full, try the secondary
		 */
		if (unlikely(slot == -1)) {
			hpte_group = ((~hash & htab_hash_mask) *
				      HPTES_PER_GROUP) & ~0x7UL;
			slot = ppc_md.hpte_insert(hpte_group, vpn, pa,
						  rflags, HPTE_V_SECONDARY,
						  psize, lpsize, ssize);
			if (slot == -1) {
				if (mftb() & 0x1)
					hpte_group = ((hash & htab_hash_mask) *
						      HPTES_PER_GROUP) & ~0x7UL;

				ppc_md.hpte_remove(hpte_group);
				goto repeat;
			}
		}
		/*
		 * Hypervisor failure. Restore old pmd and return -1
		 * similar to __hash_page_*
		 */
		if (unlikely(slot == -2)) {
			*pmdp = __pmd(old_pmd);
			hash_failure_debug(ea, access, vsid, trap, ssize,
					   psize, lpsize, old_pmd);
			return -1;
		}
		/*
		 * large pte is marked busy, so we can be sure
		 * nobody is looking at hpte_slot_array. hence we can
		 * safely update this here.
		 */
		mark_hpte_slot_valid(hpte_slot_array, index, slot);
	}
	/*
	 * Mark the pte with _PAGE_COMBO, if we are trying to hash it with
	 * base page size 4k.
	 */
	if (psize == MMU_PAGE_4K)
		new_pmd |= _PAGE_COMBO;
	/*
	 * The hpte valid is stored in the pgtable whose address is in the
	 * second half of the PMD. Order this against clearing of the busy bit in
	 * huge pmd.
	 */
	smp_wmb();
	*pmdp = __pmd(new_pmd & ~_PAGE_BUSY);
	return 0;
}
