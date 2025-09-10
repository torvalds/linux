// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 ARM Ltd.
 */

#include <linux/mm.h>
#include <linux/efi.h>
#include <linux/export.h>
#include <asm/tlbflush.h>

static inline bool mm_is_user(struct mm_struct *mm)
{
	/*
	 * Don't attempt to apply the contig bit to kernel mappings, because
	 * dynamically adding/removing the contig bit can cause page faults.
	 * These racing faults are ok for user space, since they get serialized
	 * on the PTL. But kernel mappings can't tolerate faults.
	 */
	if (unlikely(mm_is_efi(mm)))
		return false;
	return mm != &init_mm;
}

static inline pte_t *contpte_align_down(pte_t *ptep)
{
	return PTR_ALIGN_DOWN(ptep, sizeof(*ptep) * CONT_PTES);
}

static void contpte_try_unfold_partial(struct mm_struct *mm, unsigned long addr,
					pte_t *ptep, unsigned int nr)
{
	/*
	 * Unfold any partially covered contpte block at the beginning and end
	 * of the range.
	 */

	if (ptep != contpte_align_down(ptep) || nr < CONT_PTES)
		contpte_try_unfold(mm, addr, ptep, __ptep_get(ptep));

	if (ptep + nr != contpte_align_down(ptep + nr)) {
		unsigned long last_addr = addr + PAGE_SIZE * (nr - 1);
		pte_t *last_ptep = ptep + nr - 1;

		contpte_try_unfold(mm, last_addr, last_ptep,
				   __ptep_get(last_ptep));
	}
}

static void contpte_convert(struct mm_struct *mm, unsigned long addr,
			    pte_t *ptep, pte_t pte)
{
	struct vm_area_struct vma = TLB_FLUSH_VMA(mm, 0);
	unsigned long start_addr;
	pte_t *start_ptep;
	int i;

	start_ptep = ptep = contpte_align_down(ptep);
	start_addr = addr = ALIGN_DOWN(addr, CONT_PTE_SIZE);
	pte = pfn_pte(ALIGN_DOWN(pte_pfn(pte), CONT_PTES), pte_pgprot(pte));

	for (i = 0; i < CONT_PTES; i++, ptep++, addr += PAGE_SIZE) {
		pte_t ptent = __ptep_get_and_clear(mm, addr, ptep);

		if (pte_dirty(ptent))
			pte = pte_mkdirty(pte);

		if (pte_young(ptent))
			pte = pte_mkyoung(pte);
	}

	/*
	 * On eliding the __tlb_flush_range() under BBML2+noabort:
	 *
	 * NOTE: Instead of using N=16 as the contiguous block length, we use
	 *       N=4 for clarity.
	 *
	 * NOTE: 'n' and 'c' are used to denote the "contiguous bit" being
	 *       unset and set, respectively.
	 *
	 * We worry about two cases where contiguous bit is used:
	 *  - When folding N smaller non-contiguous ptes as 1 contiguous block.
	 *  - When unfolding a contiguous block into N smaller non-contiguous ptes.
	 *
	 * Currently, the BBML0 folding case looks as follows:
	 *
	 *  0) Initial page-table layout:
	 *
	 *   +----+----+----+----+
	 *   |RO,n|RO,n|RO,n|RW,n| <--- last page being set as RO
	 *   +----+----+----+----+
	 *
	 *  1) Aggregate AF + dirty flags using __ptep_get_and_clear():
	 *
	 *   +----+----+----+----+
	 *   |  0 |  0 |  0 |  0 |
	 *   +----+----+----+----+
	 *
	 *  2) __flush_tlb_range():
	 *
	 *   |____ tlbi + dsb ____|
	 *
	 *  3) __set_ptes() to repaint contiguous block:
	 *
	 *   +----+----+----+----+
	 *   |RO,c|RO,c|RO,c|RO,c|
	 *   +----+----+----+----+
	 *
	 *  4) The kernel will eventually __flush_tlb() for changed page:
	 *
	 *                  |____| <--- tlbi + dsb
	 *
	 * As expected, the intermediate tlbi+dsb ensures that other PEs
	 * only ever see an invalid (0) entry, or the new contiguous TLB entry.
	 * The final tlbi+dsb will always throw away the newly installed
	 * contiguous TLB entry, which is a micro-optimisation opportunity,
	 * but does not affect correctness.
	 *
	 * In the BBML2 case, the change is avoiding the intermediate tlbi+dsb.
	 * This means a few things, but notably other PEs will still "see" any
	 * stale cached TLB entries. This could lead to a "contiguous bit
	 * misprogramming" issue until the final tlbi+dsb of the changed page,
	 * which would clear out both the stale (RW,n) entry and the new (RO,c)
	 * contiguous entry installed in its place.
	 *
	 * What this is saying, is the following:
	 *
	 *  +----+----+----+----+
	 *  |RO,n|RO,n|RO,n|RW,n| <--- old page tables, all non-contiguous
	 *  +----+----+----+----+
	 *
	 *  +----+----+----+----+
	 *  |RO,c|RO,c|RO,c|RO,c| <--- new page tables, all contiguous
	 *  +----+----+----+----+
	 *   /\
	 *   ||
	 *
	 *  If both the old single (RW,n) and new contiguous (RO,c) TLB entries
	 *  are present, and a write is made to this address, do we fault or
	 *  is the write permitted (via amalgamation)?
	 *
	 * The relevant Arm ARM DDI 0487L.a requirements are RNGLXZ and RJQQTC,
	 * and together state that when BBML1 or BBML2 are implemented, either
	 * a TLB conflict abort is raised (which we expressly forbid), or will
	 * "produce an OA, access permissions, and memory attributes that are
	 * consistent with any of the programmed translation table values".
	 *
	 * That is to say, will either raise a TLB conflict, or produce one of
	 * the cached TLB entries, but never amalgamate.
	 *
	 * Thus, as the page tables are only considered "consistent" after
	 * the final tlbi+dsb (which evicts both the single stale (RW,n) TLB
	 * entry as well as the new contiguous (RO,c) TLB entry), omitting the
	 * initial tlbi+dsb is correct.
	 *
	 * It is also important to note that at the end of the BBML2 folding
	 * case, we are still left with potentially all N TLB entries still
	 * cached (the N-1 non-contiguous ptes, and the single contiguous
	 * block). However, over time, natural TLB pressure will cause the
	 * non-contiguous pte TLB entries to be flushed, leaving only the
	 * contiguous block TLB entry. This means that omitting the tlbi+dsb is
	 * not only correct, but also keeps our eventual performance benefits.
	 *
	 * For the unfolding case, BBML0 looks as follows:
	 *
	 *  0) Initial page-table layout:
	 *
	 *   +----+----+----+----+
	 *   |RW,c|RW,c|RW,c|RW,c| <--- last page being set as RO
	 *   +----+----+----+----+
	 *
	 *  1) Aggregate AF + dirty flags using __ptep_get_and_clear():
	 *
	 *   +----+----+----+----+
	 *   |  0 |  0 |  0 |  0 |
	 *   +----+----+----+----+
	 *
	 *  2) __flush_tlb_range():
	 *
	 *   |____ tlbi + dsb ____|
	 *
	 *  3) __set_ptes() to repaint as non-contiguous:
	 *
	 *   +----+----+----+----+
	 *   |RW,n|RW,n|RW,n|RW,n|
	 *   +----+----+----+----+
	 *
	 *  4) Update changed page permissions:
	 *
	 *   +----+----+----+----+
	 *   |RW,n|RW,n|RW,n|RO,n| <--- last page permissions set
	 *   +----+----+----+----+
	 *
	 *  5) The kernel will eventually __flush_tlb() for changed page:
	 *
	 *                  |____| <--- tlbi + dsb
	 *
	 * For BBML2, we again remove the intermediate tlbi+dsb. Here, there
	 * are no issues, as the final tlbi+dsb covering the changed page is
	 * guaranteed to remove the original large contiguous (RW,c) TLB entry,
	 * as well as the intermediate (RW,n) TLB entry; the next access will
	 * install the new (RO,n) TLB entry and the page tables are only
	 * considered "consistent" after the final tlbi+dsb, so software must
	 * be prepared for this inconsistency prior to finishing the mm dance
	 * regardless.
	 */

	if (!system_supports_bbml2_noabort())
		__flush_tlb_range(&vma, start_addr, addr, PAGE_SIZE, true, 3);

	__set_ptes(mm, start_addr, start_ptep, pte, CONT_PTES);
}

void __contpte_try_fold(struct mm_struct *mm, unsigned long addr,
			pte_t *ptep, pte_t pte)
{
	/*
	 * We have already checked that the virtual and pysical addresses are
	 * correctly aligned for a contpte mapping in contpte_try_fold() so the
	 * remaining checks are to ensure that the contpte range is fully
	 * covered by a single folio, and ensure that all the ptes are valid
	 * with contiguous PFNs and matching prots. We ignore the state of the
	 * access and dirty bits for the purpose of deciding if its a contiguous
	 * range; the folding process will generate a single contpte entry which
	 * has a single access and dirty bit. Those 2 bits are the logical OR of
	 * their respective bits in the constituent pte entries. In order to
	 * ensure the contpte range is covered by a single folio, we must
	 * recover the folio from the pfn, but special mappings don't have a
	 * folio backing them. Fortunately contpte_try_fold() already checked
	 * that the pte is not special - we never try to fold special mappings.
	 * Note we can't use vm_normal_page() for this since we don't have the
	 * vma.
	 */

	unsigned long folio_start, folio_end;
	unsigned long cont_start, cont_end;
	pte_t expected_pte, subpte;
	struct folio *folio;
	struct page *page;
	unsigned long pfn;
	pte_t *orig_ptep;
	pgprot_t prot;

	int i;

	if (!mm_is_user(mm))
		return;

	page = pte_page(pte);
	folio = page_folio(page);
	folio_start = addr - (page - &folio->page) * PAGE_SIZE;
	folio_end = folio_start + folio_nr_pages(folio) * PAGE_SIZE;
	cont_start = ALIGN_DOWN(addr, CONT_PTE_SIZE);
	cont_end = cont_start + CONT_PTE_SIZE;

	if (folio_start > cont_start || folio_end < cont_end)
		return;

	pfn = ALIGN_DOWN(pte_pfn(pte), CONT_PTES);
	prot = pte_pgprot(pte_mkold(pte_mkclean(pte)));
	expected_pte = pfn_pte(pfn, prot);
	orig_ptep = ptep;
	ptep = contpte_align_down(ptep);

	for (i = 0; i < CONT_PTES; i++) {
		subpte = pte_mkold(pte_mkclean(__ptep_get(ptep)));
		if (!pte_same(subpte, expected_pte))
			return;
		expected_pte = pte_advance_pfn(expected_pte, 1);
		ptep++;
	}

	pte = pte_mkcont(pte);
	contpte_convert(mm, addr, orig_ptep, pte);
}
EXPORT_SYMBOL_GPL(__contpte_try_fold);

void __contpte_try_unfold(struct mm_struct *mm, unsigned long addr,
			pte_t *ptep, pte_t pte)
{
	/*
	 * We have already checked that the ptes are contiguous in
	 * contpte_try_unfold(), so just check that the mm is user space.
	 */
	if (!mm_is_user(mm))
		return;

	pte = pte_mknoncont(pte);
	contpte_convert(mm, addr, ptep, pte);
}
EXPORT_SYMBOL_GPL(__contpte_try_unfold);

pte_t contpte_ptep_get(pte_t *ptep, pte_t orig_pte)
{
	/*
	 * Gather access/dirty bits, which may be populated in any of the ptes
	 * of the contig range. We are guaranteed to be holding the PTL, so any
	 * contiguous range cannot be unfolded or otherwise modified under our
	 * feet.
	 */

	pte_t pte;
	int i;

	ptep = contpte_align_down(ptep);

	for (i = 0; i < CONT_PTES; i++, ptep++) {
		pte = __ptep_get(ptep);

		if (pte_dirty(pte)) {
			orig_pte = pte_mkdirty(orig_pte);
			for (; i < CONT_PTES; i++, ptep++) {
				pte = __ptep_get(ptep);
				if (pte_young(pte)) {
					orig_pte = pte_mkyoung(orig_pte);
					break;
				}
			}
			break;
		}

		if (pte_young(pte)) {
			orig_pte = pte_mkyoung(orig_pte);
			i++;
			ptep++;
			for (; i < CONT_PTES; i++, ptep++) {
				pte = __ptep_get(ptep);
				if (pte_dirty(pte)) {
					orig_pte = pte_mkdirty(orig_pte);
					break;
				}
			}
			break;
		}
	}

	return orig_pte;
}
EXPORT_SYMBOL_GPL(contpte_ptep_get);

static inline bool contpte_is_consistent(pte_t pte, unsigned long pfn,
					pgprot_t orig_prot)
{
	pgprot_t prot = pte_pgprot(pte_mkold(pte_mkclean(pte)));

	return pte_valid_cont(pte) && pte_pfn(pte) == pfn &&
			pgprot_val(prot) == pgprot_val(orig_prot);
}

pte_t contpte_ptep_get_lockless(pte_t *orig_ptep)
{
	/*
	 * The ptep_get_lockless() API requires us to read and return *orig_ptep
	 * so that it is self-consistent, without the PTL held, so we may be
	 * racing with other threads modifying the pte. Usually a READ_ONCE()
	 * would suffice, but for the contpte case, we also need to gather the
	 * access and dirty bits from across all ptes in the contiguous block,
	 * and we can't read all of those neighbouring ptes atomically, so any
	 * contiguous range may be unfolded/modified/refolded under our feet.
	 * Therefore we ensure we read a _consistent_ contpte range by checking
	 * that all ptes in the range are valid and have CONT_PTE set, that all
	 * pfns are contiguous and that all pgprots are the same (ignoring
	 * access/dirty). If we find a pte that is not consistent, then we must
	 * be racing with an update so start again. If the target pte does not
	 * have CONT_PTE set then that is considered consistent on its own
	 * because it is not part of a contpte range.
	 */

	pgprot_t orig_prot;
	unsigned long pfn;
	pte_t orig_pte;
	pte_t *ptep;
	pte_t pte;
	int i;

retry:
	orig_pte = __ptep_get(orig_ptep);

	if (!pte_valid_cont(orig_pte))
		return orig_pte;

	orig_prot = pte_pgprot(pte_mkold(pte_mkclean(orig_pte)));
	ptep = contpte_align_down(orig_ptep);
	pfn = pte_pfn(orig_pte) - (orig_ptep - ptep);

	for (i = 0; i < CONT_PTES; i++, ptep++, pfn++) {
		pte = __ptep_get(ptep);

		if (!contpte_is_consistent(pte, pfn, orig_prot))
			goto retry;

		if (pte_dirty(pte)) {
			orig_pte = pte_mkdirty(orig_pte);
			for (; i < CONT_PTES; i++, ptep++, pfn++) {
				pte = __ptep_get(ptep);

				if (!contpte_is_consistent(pte, pfn, orig_prot))
					goto retry;

				if (pte_young(pte)) {
					orig_pte = pte_mkyoung(orig_pte);
					break;
				}
			}
			break;
		}

		if (pte_young(pte)) {
			orig_pte = pte_mkyoung(orig_pte);
			i++;
			ptep++;
			pfn++;
			for (; i < CONT_PTES; i++, ptep++, pfn++) {
				pte = __ptep_get(ptep);

				if (!contpte_is_consistent(pte, pfn, orig_prot))
					goto retry;

				if (pte_dirty(pte)) {
					orig_pte = pte_mkdirty(orig_pte);
					break;
				}
			}
			break;
		}
	}

	return orig_pte;
}
EXPORT_SYMBOL_GPL(contpte_ptep_get_lockless);

void contpte_set_ptes(struct mm_struct *mm, unsigned long addr,
					pte_t *ptep, pte_t pte, unsigned int nr)
{
	unsigned long next;
	unsigned long end;
	unsigned long pfn;
	pgprot_t prot;

	/*
	 * The set_ptes() spec guarantees that when nr > 1, the initial state of
	 * all ptes is not-present. Therefore we never need to unfold or
	 * otherwise invalidate a range before we set the new ptes.
	 * contpte_set_ptes() should never be called for nr < 2.
	 */
	VM_WARN_ON(nr == 1);

	if (!mm_is_user(mm))
		return __set_ptes(mm, addr, ptep, pte, nr);

	end = addr + (nr << PAGE_SHIFT);
	pfn = pte_pfn(pte);
	prot = pte_pgprot(pte);

	do {
		next = pte_cont_addr_end(addr, end);
		nr = (next - addr) >> PAGE_SHIFT;
		pte = pfn_pte(pfn, prot);

		if (((addr | next | (pfn << PAGE_SHIFT)) & ~CONT_PTE_MASK) == 0)
			pte = pte_mkcont(pte);
		else
			pte = pte_mknoncont(pte);

		__set_ptes(mm, addr, ptep, pte, nr);

		addr = next;
		ptep += nr;
		pfn += nr;

	} while (addr != end);
}
EXPORT_SYMBOL_GPL(contpte_set_ptes);

void contpte_clear_full_ptes(struct mm_struct *mm, unsigned long addr,
				pte_t *ptep, unsigned int nr, int full)
{
	contpte_try_unfold_partial(mm, addr, ptep, nr);
	__clear_full_ptes(mm, addr, ptep, nr, full);
}
EXPORT_SYMBOL_GPL(contpte_clear_full_ptes);

pte_t contpte_get_and_clear_full_ptes(struct mm_struct *mm,
				unsigned long addr, pte_t *ptep,
				unsigned int nr, int full)
{
	contpte_try_unfold_partial(mm, addr, ptep, nr);
	return __get_and_clear_full_ptes(mm, addr, ptep, nr, full);
}
EXPORT_SYMBOL_GPL(contpte_get_and_clear_full_ptes);

int contpte_ptep_test_and_clear_young(struct vm_area_struct *vma,
					unsigned long addr, pte_t *ptep)
{
	/*
	 * ptep_clear_flush_young() technically requires us to clear the access
	 * flag for a _single_ pte. However, the core-mm code actually tracks
	 * access/dirty per folio, not per page. And since we only create a
	 * contig range when the range is covered by a single folio, we can get
	 * away with clearing young for the whole contig range here, so we avoid
	 * having to unfold.
	 */

	int young = 0;
	int i;

	ptep = contpte_align_down(ptep);
	addr = ALIGN_DOWN(addr, CONT_PTE_SIZE);

	for (i = 0; i < CONT_PTES; i++, ptep++, addr += PAGE_SIZE)
		young |= __ptep_test_and_clear_young(vma, addr, ptep);

	return young;
}
EXPORT_SYMBOL_GPL(contpte_ptep_test_and_clear_young);

int contpte_ptep_clear_flush_young(struct vm_area_struct *vma,
					unsigned long addr, pte_t *ptep)
{
	int young;

	young = contpte_ptep_test_and_clear_young(vma, addr, ptep);

	if (young) {
		/*
		 * See comment in __ptep_clear_flush_young(); same rationale for
		 * eliding the trailing DSB applies here.
		 */
		addr = ALIGN_DOWN(addr, CONT_PTE_SIZE);
		__flush_tlb_range_nosync(vma->vm_mm, addr, addr + CONT_PTE_SIZE,
					 PAGE_SIZE, true, 3);
	}

	return young;
}
EXPORT_SYMBOL_GPL(contpte_ptep_clear_flush_young);

void contpte_wrprotect_ptes(struct mm_struct *mm, unsigned long addr,
					pte_t *ptep, unsigned int nr)
{
	/*
	 * If wrprotecting an entire contig range, we can avoid unfolding. Just
	 * set wrprotect and wait for the later mmu_gather flush to invalidate
	 * the tlb. Until the flush, the page may or may not be wrprotected.
	 * After the flush, it is guaranteed wrprotected. If it's a partial
	 * range though, we must unfold, because we can't have a case where
	 * CONT_PTE is set but wrprotect applies to a subset of the PTEs; this
	 * would cause it to continue to be unpredictable after the flush.
	 */

	contpte_try_unfold_partial(mm, addr, ptep, nr);
	__wrprotect_ptes(mm, addr, ptep, nr);
}
EXPORT_SYMBOL_GPL(contpte_wrprotect_ptes);

void contpte_clear_young_dirty_ptes(struct vm_area_struct *vma,
				    unsigned long addr, pte_t *ptep,
				    unsigned int nr, cydp_t flags)
{
	/*
	 * We can safely clear access/dirty without needing to unfold from
	 * the architectures perspective, even when contpte is set. If the
	 * range starts or ends midway through a contpte block, we can just
	 * expand to include the full contpte block. While this is not
	 * exactly what the core-mm asked for, it tracks access/dirty per
	 * folio, not per page. And since we only create a contpte block
	 * when it is covered by a single folio, we can get away with
	 * clearing access/dirty for the whole block.
	 */
	unsigned long start = addr;
	unsigned long end = start + nr * PAGE_SIZE;

	if (pte_cont(__ptep_get(ptep + nr - 1)))
		end = ALIGN(end, CONT_PTE_SIZE);

	if (pte_cont(__ptep_get(ptep))) {
		start = ALIGN_DOWN(start, CONT_PTE_SIZE);
		ptep = contpte_align_down(ptep);
	}

	__clear_young_dirty_ptes(vma, start, ptep, (end - start) / PAGE_SIZE, flags);
}
EXPORT_SYMBOL_GPL(contpte_clear_young_dirty_ptes);

int contpte_ptep_set_access_flags(struct vm_area_struct *vma,
					unsigned long addr, pte_t *ptep,
					pte_t entry, int dirty)
{
	unsigned long start_addr;
	pte_t orig_pte;
	int i;

	/*
	 * Gather the access/dirty bits for the contiguous range. If nothing has
	 * changed, its a noop.
	 */
	orig_pte = pte_mknoncont(ptep_get(ptep));
	if (pte_val(orig_pte) == pte_val(entry))
		return 0;

	/*
	 * We can fix up access/dirty bits without having to unfold the contig
	 * range. But if the write bit is changing, we must unfold.
	 */
	if (pte_write(orig_pte) == pte_write(entry)) {
		/*
		 * For HW access management, we technically only need to update
		 * the flag on a single pte in the range. But for SW access
		 * management, we need to update all the ptes to prevent extra
		 * faults. Avoid per-page tlb flush in __ptep_set_access_flags()
		 * and instead flush the whole range at the end.
		 */
		ptep = contpte_align_down(ptep);
		start_addr = addr = ALIGN_DOWN(addr, CONT_PTE_SIZE);

		/*
		 * We are not advancing entry because __ptep_set_access_flags()
		 * only consumes access flags from entry. And since we have checked
		 * for the whole contpte block and returned early, pte_same()
		 * within __ptep_set_access_flags() is likely false.
		 */
		for (i = 0; i < CONT_PTES; i++, ptep++, addr += PAGE_SIZE)
			__ptep_set_access_flags(vma, addr, ptep, entry, 0);

		if (dirty)
			__flush_tlb_range(vma, start_addr, addr,
							PAGE_SIZE, true, 3);
	} else {
		__contpte_try_unfold(vma->vm_mm, addr, ptep, orig_pte);
		__ptep_set_access_flags(vma, addr, ptep, entry, dirty);
	}

	return 1;
}
EXPORT_SYMBOL_GPL(contpte_ptep_set_access_flags);
