// SPDX-License-Identifier: GPL-2.0
/*
 * PARISC64 Huge TLB page support.
 *
 * This parisc implementation is heavily based on the SPARC and x86 code.
 *
 * Copyright (C) 2015 Helge Deller <deller@gmx.de>
 */

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/hugetlb.h>
#include <linux/pagemap.h>
#include <linux/sysctl.h>

#include <asm/mman.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/cacheflush.h>
#include <asm/mmu_context.h>


unsigned long
hugetlb_get_unmapped_area(struct file *file, unsigned long addr,
		unsigned long len, unsigned long pgoff, unsigned long flags)
{
	struct hstate *h = hstate_file(file);

	if (len & ~huge_page_mask(h))
		return -EINVAL;
	if (len > TASK_SIZE)
		return -ENOMEM;

	if (flags & MAP_FIXED)
		if (prepare_hugepage_range(file, addr, len))
			return -EINVAL;

	if (addr)
		addr = ALIGN(addr, huge_page_size(h));

	/* we need to make sure the colouring is OK */
	return arch_get_unmapped_area(file, addr, len, pgoff, flags);
}


pte_t *huge_pte_alloc(struct mm_struct *mm,
			unsigned long addr, unsigned long sz)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte = NULL;

	/* We must align the address, because our caller will run
	 * set_huge_pte_at() on whatever we return, which writes out
	 * all of the sub-ptes for the hugepage range.  So we have
	 * to give it the first such sub-pte.
	 */
	addr &= HPAGE_MASK;

	pgd = pgd_offset(mm, addr);
	p4d = p4d_offset(pgd, addr);
	pud = pud_alloc(mm, p4d, addr);
	if (pud) {
		pmd = pmd_alloc(mm, pud, addr);
		if (pmd)
			pte = pte_alloc_map(mm, pmd, addr);
	}
	return pte;
}

pte_t *huge_pte_offset(struct mm_struct *mm,
		       unsigned long addr, unsigned long sz)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte = NULL;

	addr &= HPAGE_MASK;

	pgd = pgd_offset(mm, addr);
	if (!pgd_none(*pgd)) {
		p4d = p4d_offset(pgd, addr);
		if (!p4d_none(*p4d)) {
			pud = pud_offset(p4d, addr);
			if (!pud_none(*pud)) {
				pmd = pmd_offset(pud, addr);
				if (!pmd_none(*pmd))
					pte = pte_offset_map(pmd, addr);
			}
		}
	}
	return pte;
}

/* Purge data and instruction TLB entries.  Must be called holding
 * the pa_tlb_lock.  The TLB purge instructions are slow on SMP
 * machines since the purge must be broadcast to all CPUs.
 */
static inline void purge_tlb_entries_huge(struct mm_struct *mm, unsigned long addr)
{
	int i;

	/* We may use multiple physical huge pages (e.g. 2x1 MB) to emulate
	 * Linux standard huge pages (e.g. 2 MB) */
	BUILD_BUG_ON(REAL_HPAGE_SHIFT > HPAGE_SHIFT);

	addr &= HPAGE_MASK;
	addr |= _HUGE_PAGE_SIZE_ENCODING_DEFAULT;

	for (i = 0; i < (1 << (HPAGE_SHIFT-REAL_HPAGE_SHIFT)); i++) {
		purge_tlb_entries(mm, addr);
		addr += (1UL << REAL_HPAGE_SHIFT);
	}
}

/* __set_huge_pte_at() must be called holding the pa_tlb_lock. */
static void __set_huge_pte_at(struct mm_struct *mm, unsigned long addr,
		     pte_t *ptep, pte_t entry)
{
	unsigned long addr_start;
	int i;

	addr &= HPAGE_MASK;
	addr_start = addr;

	for (i = 0; i < (1 << HUGETLB_PAGE_ORDER); i++) {
		set_pte(ptep, entry);
		ptep++;

		addr += PAGE_SIZE;
		pte_val(entry) += PAGE_SIZE;
	}

	purge_tlb_entries_huge(mm, addr_start);
}

void set_huge_pte_at(struct mm_struct *mm, unsigned long addr,
		     pte_t *ptep, pte_t entry)
{
	unsigned long flags;

	spin_lock_irqsave(pgd_spinlock((mm)->pgd), flags);
	__set_huge_pte_at(mm, addr, ptep, entry);
	spin_unlock_irqrestore(pgd_spinlock((mm)->pgd), flags);
}


pte_t huge_ptep_get_and_clear(struct mm_struct *mm, unsigned long addr,
			      pte_t *ptep)
{
	unsigned long flags;
	pte_t entry;

	spin_lock_irqsave(pgd_spinlock((mm)->pgd), flags);
	entry = *ptep;
	__set_huge_pte_at(mm, addr, ptep, __pte(0));
	spin_unlock_irqrestore(pgd_spinlock((mm)->pgd), flags);

	return entry;
}


void huge_ptep_set_wrprotect(struct mm_struct *mm,
				unsigned long addr, pte_t *ptep)
{
	unsigned long flags;
	pte_t old_pte;

	spin_lock_irqsave(pgd_spinlock((mm)->pgd), flags);
	old_pte = *ptep;
	__set_huge_pte_at(mm, addr, ptep, pte_wrprotect(old_pte));
	spin_unlock_irqrestore(pgd_spinlock((mm)->pgd), flags);
}

int huge_ptep_set_access_flags(struct vm_area_struct *vma,
				unsigned long addr, pte_t *ptep,
				pte_t pte, int dirty)
{
	unsigned long flags;
	int changed;
	struct mm_struct *mm = vma->vm_mm;

	spin_lock_irqsave(pgd_spinlock((mm)->pgd), flags);
	changed = !pte_same(*ptep, pte);
	if (changed) {
		__set_huge_pte_at(mm, addr, ptep, pte);
	}
	spin_unlock_irqrestore(pgd_spinlock((mm)->pgd), flags);
	return changed;
}


int pmd_huge(pmd_t pmd)
{
	return 0;
}

int pud_huge(pud_t pud)
{
	return 0;
}
