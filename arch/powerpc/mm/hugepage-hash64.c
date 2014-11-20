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

static void invalidate_old_hpte(unsigned long vsid, unsigned long addr,
				pmd_t *pmdp, unsigned int psize, int ssize)
{
	int i, max_hpte_count, valid;
	unsigned long s_addr;
	unsigned char *hpte_slot_array;
	unsigned long hidx, shift, vpn, hash, slot;

	s_addr = addr & HPAGE_PMD_MASK;
	hpte_slot_array = get_hpte_slot_array(pmdp);
	/*
	 * IF we try to do a HUGE PTE update after a withdraw is done.
	 * we will find the below NULL. This happens when we do
	 * split_huge_page_pmd
	 */
	if (!hpte_slot_array)
		return;

	if (ppc_md.hugepage_invalidate)
		return ppc_md.hugepage_invalidate(vsid, s_addr, hpte_slot_array,
						  psize, ssize);
	/*
	 * No bluk hpte removal support, invalidate each entry
	 */
	shift = mmu_psize_defs[psize].shift;
	max_hpte_count = HPAGE_PMD_SIZE >> shift;
	for (i = 0; i < max_hpte_count; i++) {
		/*
		 * 8 bits per each hpte entries
		 * 000| [ secondary group (one bit) | hidx (3 bits) | valid bit]
		 */
		valid = hpte_valid(hpte_slot_array, i);
		if (!valid)
			continue;
		hidx =  hpte_hash_index(hpte_slot_array, i);

		/* get the vpn */
		addr = s_addr + (i * (1ul << shift));
		vpn = hpt_vpn(addr, vsid, ssize);
		hash = hpt_hash(vpn, shift, ssize);
		if (hidx & _PTEIDX_SECONDARY)
			hash = ~hash;

		slot = (hash & htab_hash_mask) * HPTES_PER_GROUP;
		slot += hidx & _PTEIDX_GROUP_IX;
		ppc_md.hpte_invalidate(slot, vpn, psize,
				       MMU_PAGE_16M, ssize, 0);
	}
}


int __hash_page_thp(unsigned long ea, unsigned long access, unsigned long vsid,
		    pmd_t *pmdp, unsigned long trap, int local, int ssize,
		    unsigned int psize)
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
		pmd_t pmd = ACCESS_ONCE(*pmdp);

		old_pmd = pmd_val(pmd);
		/* If PMD busy, retry the access */
		if (unlikely(old_pmd & _PAGE_BUSY))
			return 0;
		/* If PMD is trans splitting retry the access */
		if (unlikely(old_pmd & _PAGE_SPLITTING))
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
	/*
	 * PP bits. _PAGE_USER is already PP bit 0x2, so we only
	 * need to add in 0x1 if it's a read-only user page
	 */
	rflags = new_pmd & _PAGE_USER;
	if ((new_pmd & _PAGE_USER) && !((new_pmd & _PAGE_RW) &&
					   (new_pmd & _PAGE_DIRTY)))
		rflags |= 0x1;
	/*
	 * _PAGE_EXEC -> HW_NO_EXEC since it's inverted
	 */
	rflags |= ((new_pmd & _PAGE_EXEC) ? 0 : HPTE_R_N);

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
	BUG_ON(index >= 4096);

	vpn = hpt_vpn(ea, vsid, ssize);
	hash = hpt_hash(vpn, shift, ssize);
	hpte_slot_array = get_hpte_slot_array(pmdp);
	if (psize == MMU_PAGE_4K) {
		/*
		 * invalidate the old hpte entry if we have that mapped via 64K
		 * base page size. This is because demote_segment won't flush
		 * hash page table entries.
		 */
		if ((old_pmd & _PAGE_HASHPTE) && !(old_pmd & _PAGE_COMBO))
			invalidate_old_hpte(vsid, ea, pmdp, MMU_PAGE_64K, ssize);
	}

	valid = hpte_valid(hpte_slot_array, index);
	if (valid) {
		/* update the hpte bits */
		hidx =  hpte_hash_index(hpte_slot_array, index);
		if (hidx & _PTEIDX_SECONDARY)
			hash = ~hash;
		slot = (hash & htab_hash_mask) * HPTES_PER_GROUP;
		slot += hidx & _PTEIDX_GROUP_IX;

		ret = ppc_md.hpte_updatepp(slot, rflags, vpn,
					   psize, lpsize, ssize, local);
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

		/* insert new entry */
		pa = pmd_pfn(__pmd(old_pmd)) << PAGE_SHIFT;
		new_pmd |= _PAGE_HASHPTE;

		/* Add in WIMG bits */
		rflags |= (new_pmd & (_PAGE_WRITETHRU | _PAGE_NO_CACHE |
				      _PAGE_GUARDED));
		/*
		 * enable the memory coherence always
		 */
		rflags |= HPTE_R_M;
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
