/*
 * This file contains common routines for dealing with free of page tables
 * Along with common page table handling code
 *
 *  Derived from arch/powerpc/mm/tlb_64.c:
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Modifications by Paul Mackerras (PowerMac) (paulus@cs.anu.edu.au)
 *  and Cort Dougan (PReP) (cort@cs.nmt.edu)
 *    Copyright (C) 1996 Paul Mackerras
 *
 *  Derived from "arch/i386/mm/init.c"
 *    Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  Dave Engebretsen <engebret@us.ibm.com>
 *      Rework for PPC64 port.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/percpu.h>
#include <linux/hardirq.h>
#include <linux/hugetlb.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>
#include <asm/tlb.h>

static inline int is_exec_fault(void)
{
	return current->thread.regs && TRAP(current->thread.regs) == 0x400;
}

/* We only try to do i/d cache coherency on stuff that looks like
 * reasonably "normal" PTEs. We currently require a PTE to be present
 * and we avoid _PAGE_SPECIAL and cache inhibited pte. We also only do that
 * on userspace PTEs
 */
static inline int pte_looks_normal(pte_t pte)
{

#if defined(CONFIG_PPC_BOOK3S_64)
	if ((pte_val(pte) & (_PAGE_PRESENT | _PAGE_SPECIAL)) == _PAGE_PRESENT) {
		if (pte_ci(pte))
			return 0;
		if (pte_user(pte))
			return 1;
	}
	return 0;
#else
	return (pte_val(pte) &
		(_PAGE_PRESENT | _PAGE_SPECIAL | _PAGE_NO_CACHE | _PAGE_USER)) ==
		(_PAGE_PRESENT | _PAGE_USER);
#endif
}

static struct page *maybe_pte_to_page(pte_t pte)
{
	unsigned long pfn = pte_pfn(pte);
	struct page *page;

	if (unlikely(!pfn_valid(pfn)))
		return NULL;
	page = pfn_to_page(pfn);
	if (PageReserved(page))
		return NULL;
	return page;
}

#if defined(CONFIG_PPC_STD_MMU) || _PAGE_EXEC == 0

/* Server-style MMU handles coherency when hashing if HW exec permission
 * is supposed per page (currently 64-bit only). If not, then, we always
 * flush the cache for valid PTEs in set_pte. Embedded CPU without HW exec
 * support falls into the same category.
 */

static pte_t set_pte_filter(pte_t pte)
{
	if (radix_enabled())
		return pte;

	pte = __pte(pte_val(pte) & ~_PAGE_HPTEFLAGS);
	if (pte_looks_normal(pte) && !(cpu_has_feature(CPU_FTR_COHERENT_ICACHE) ||
				       cpu_has_feature(CPU_FTR_NOEXECUTE))) {
		struct page *pg = maybe_pte_to_page(pte);
		if (!pg)
			return pte;
		if (!test_bit(PG_arch_1, &pg->flags)) {
			flush_dcache_icache_page(pg);
			set_bit(PG_arch_1, &pg->flags);
		}
	}
	return pte;
}

static pte_t set_access_flags_filter(pte_t pte, struct vm_area_struct *vma,
				     int dirty)
{
	return pte;
}

#else /* defined(CONFIG_PPC_STD_MMU) || _PAGE_EXEC == 0 */

/* Embedded type MMU with HW exec support. This is a bit more complicated
 * as we don't have two bits to spare for _PAGE_EXEC and _PAGE_HWEXEC so
 * instead we "filter out" the exec permission for non clean pages.
 */
static pte_t set_pte_filter(pte_t pte)
{
	struct page *pg;

	/* No exec permission in the first place, move on */
	if (!(pte_val(pte) & _PAGE_EXEC) || !pte_looks_normal(pte))
		return pte;

	/* If you set _PAGE_EXEC on weird pages you're on your own */
	pg = maybe_pte_to_page(pte);
	if (unlikely(!pg))
		return pte;

	/* If the page clean, we move on */
	if (test_bit(PG_arch_1, &pg->flags))
		return pte;

	/* If it's an exec fault, we flush the cache and make it clean */
	if (is_exec_fault()) {
		flush_dcache_icache_page(pg);
		set_bit(PG_arch_1, &pg->flags);
		return pte;
	}

	/* Else, we filter out _PAGE_EXEC */
	return __pte(pte_val(pte) & ~_PAGE_EXEC);
}

static pte_t set_access_flags_filter(pte_t pte, struct vm_area_struct *vma,
				     int dirty)
{
	struct page *pg;

	/* So here, we only care about exec faults, as we use them
	 * to recover lost _PAGE_EXEC and perform I$/D$ coherency
	 * if necessary. Also if _PAGE_EXEC is already set, same deal,
	 * we just bail out
	 */
	if (dirty || (pte_val(pte) & _PAGE_EXEC) || !is_exec_fault())
		return pte;

#ifdef CONFIG_DEBUG_VM
	/* So this is an exec fault, _PAGE_EXEC is not set. If it was
	 * an error we would have bailed out earlier in do_page_fault()
	 * but let's make sure of it
	 */
	if (WARN_ON(!(vma->vm_flags & VM_EXEC)))
		return pte;
#endif /* CONFIG_DEBUG_VM */

	/* If you set _PAGE_EXEC on weird pages you're on your own */
	pg = maybe_pte_to_page(pte);
	if (unlikely(!pg))
		goto bail;

	/* If the page is already clean, we move on */
	if (test_bit(PG_arch_1, &pg->flags))
		goto bail;

	/* Clean the page and set PG_arch_1 */
	flush_dcache_icache_page(pg);
	set_bit(PG_arch_1, &pg->flags);

 bail:
	return __pte(pte_val(pte) | _PAGE_EXEC);
}

#endif /* !(defined(CONFIG_PPC_STD_MMU) || _PAGE_EXEC == 0) */

/*
 * set_pte stores a linux PTE into the linux page table.
 */
void set_pte_at(struct mm_struct *mm, unsigned long addr, pte_t *ptep,
		pte_t pte)
{
	/*
	 * When handling numa faults, we already have the pte marked
	 * _PAGE_PRESENT, but we can be sure that it is not in hpte.
	 * Hence we can use set_pte_at for them.
	 */
	VM_WARN_ON(pte_present(*ptep) && !pte_protnone(*ptep));

	/*
	 * Add the pte bit when tryint set a pte
	 */
	pte = __pte(pte_val(pte) | _PAGE_PTE);

	/* Note: mm->context.id might not yet have been assigned as
	 * this context might not have been activated yet when this
	 * is called.
	 */
	pte = set_pte_filter(pte);

	/* Perform the setting of the PTE */
	__set_pte_at(mm, addr, ptep, pte, 0);
}

/*
 * This is called when relaxing access to a PTE. It's also called in the page
 * fault path when we don't hit any of the major fault cases, ie, a minor
 * update of _PAGE_ACCESSED, _PAGE_DIRTY, etc... The generic code will have
 * handled those two for us, we additionally deal with missing execute
 * permission here on some processors
 */
int ptep_set_access_flags(struct vm_area_struct *vma, unsigned long address,
			  pte_t *ptep, pte_t entry, int dirty)
{
	int changed;
	entry = set_access_flags_filter(entry, vma, dirty);
	changed = !pte_same(*(ptep), entry);
	if (changed) {
		if (!is_vm_hugetlb_page(vma))
			assert_pte_locked(vma->vm_mm, address);
		__ptep_set_access_flags(vma->vm_mm, ptep, entry, address);
		flush_tlb_page(vma, address);
	}
	return changed;
}

#ifdef CONFIG_DEBUG_VM
void assert_pte_locked(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;

	if (mm == &init_mm)
		return;
	pgd = mm->pgd + pgd_index(addr);
	BUG_ON(pgd_none(*pgd));
	pud = pud_offset(pgd, addr);
	BUG_ON(pud_none(*pud));
	pmd = pmd_offset(pud, addr);
	/*
	 * khugepaged to collapse normal pages to hugepage, first set
	 * pmd to none to force page fault/gup to take mmap_sem. After
	 * pmd is set to none, we do a pte_clear which does this assertion
	 * so if we find pmd none, return.
	 */
	if (pmd_none(*pmd))
		return;
	BUG_ON(!pmd_present(*pmd));
	assert_spin_locked(pte_lockptr(mm, pmd));
}
#endif /* CONFIG_DEBUG_VM */

unsigned long vmalloc_to_phys(void *va)
{
	unsigned long pfn = vmalloc_to_pfn(va);

	BUG_ON(!pfn);
	return __pa(pfn_to_kaddr(pfn)) + offset_in_page(va);
}
EXPORT_SYMBOL_GPL(vmalloc_to_phys);
