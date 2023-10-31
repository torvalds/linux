// SPDX-License-Identifier: GPL-2.0-or-later
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
 */

#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/percpu.h>
#include <linux/hardirq.h>
#include <linux/hugetlb.h>
#include <asm/tlbflush.h>
#include <asm/tlb.h>
#include <asm/hugetlb.h>
#include <asm/pte-walk.h>

#ifdef CONFIG_PPC64
#define PGD_ALIGN (sizeof(pgd_t) * MAX_PTRS_PER_PGD)
#else
#define PGD_ALIGN PAGE_SIZE
#endif

pgd_t swapper_pg_dir[MAX_PTRS_PER_PGD] __section(".bss..page_aligned") __aligned(PGD_ALIGN);

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

	if (pte_present(pte) && !pte_special(pte)) {
		if (pte_ci(pte))
			return 0;
		if (pte_user(pte))
			return 1;
	}
	return 0;
}

static struct folio *maybe_pte_to_folio(pte_t pte)
{
	unsigned long pfn = pte_pfn(pte);
	struct page *page;

	if (unlikely(!pfn_valid(pfn)))
		return NULL;
	page = pfn_to_page(pfn);
	if (PageReserved(page))
		return NULL;
	return page_folio(page);
}

#ifdef CONFIG_PPC_BOOK3S

/* Server-style MMU handles coherency when hashing if HW exec permission
 * is supposed per page (currently 64-bit only). If not, then, we always
 * flush the cache for valid PTEs in set_pte. Embedded CPU without HW exec
 * support falls into the same category.
 */

static pte_t set_pte_filter_hash(pte_t pte)
{
	pte = __pte(pte_val(pte) & ~_PAGE_HPTEFLAGS);
	if (pte_looks_normal(pte) && !(cpu_has_feature(CPU_FTR_COHERENT_ICACHE) ||
				       cpu_has_feature(CPU_FTR_NOEXECUTE))) {
		struct folio *folio = maybe_pte_to_folio(pte);
		if (!folio)
			return pte;
		if (!test_bit(PG_dcache_clean, &folio->flags)) {
			flush_dcache_icache_folio(folio);
			set_bit(PG_dcache_clean, &folio->flags);
		}
	}
	return pte;
}

#else /* CONFIG_PPC_BOOK3S */

static pte_t set_pte_filter_hash(pte_t pte) { return pte; }

#endif /* CONFIG_PPC_BOOK3S */

/* Embedded type MMU with HW exec support. This is a bit more complicated
 * as we don't have two bits to spare for _PAGE_EXEC and _PAGE_HWEXEC so
 * instead we "filter out" the exec permission for non clean pages.
 */
static inline pte_t set_pte_filter(pte_t pte)
{
	struct folio *folio;

	if (radix_enabled())
		return pte;

	if (mmu_has_feature(MMU_FTR_HPTE_TABLE))
		return set_pte_filter_hash(pte);

	/* No exec permission in the first place, move on */
	if (!pte_exec(pte) || !pte_looks_normal(pte))
		return pte;

	/* If you set _PAGE_EXEC on weird pages you're on your own */
	folio = maybe_pte_to_folio(pte);
	if (unlikely(!folio))
		return pte;

	/* If the page clean, we move on */
	if (test_bit(PG_dcache_clean, &folio->flags))
		return pte;

	/* If it's an exec fault, we flush the cache and make it clean */
	if (is_exec_fault()) {
		flush_dcache_icache_folio(folio);
		set_bit(PG_dcache_clean, &folio->flags);
		return pte;
	}

	/* Else, we filter out _PAGE_EXEC */
	return pte_exprotect(pte);
}

static pte_t set_access_flags_filter(pte_t pte, struct vm_area_struct *vma,
				     int dirty)
{
	struct folio *folio;

	if (IS_ENABLED(CONFIG_PPC_BOOK3S_64))
		return pte;

	if (mmu_has_feature(MMU_FTR_HPTE_TABLE))
		return pte;

	/* So here, we only care about exec faults, as we use them
	 * to recover lost _PAGE_EXEC and perform I$/D$ coherency
	 * if necessary. Also if _PAGE_EXEC is already set, same deal,
	 * we just bail out
	 */
	if (dirty || pte_exec(pte) || !is_exec_fault())
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
	folio = maybe_pte_to_folio(pte);
	if (unlikely(!folio))
		goto bail;

	/* If the page is already clean, we move on */
	if (test_bit(PG_dcache_clean, &folio->flags))
		goto bail;

	/* Clean the page and set PG_dcache_clean */
	flush_dcache_icache_folio(folio);
	set_bit(PG_dcache_clean, &folio->flags);

 bail:
	return pte_mkexec(pte);
}

/*
 * set_pte stores a linux PTE into the linux page table.
 */
void set_ptes(struct mm_struct *mm, unsigned long addr, pte_t *ptep,
		pte_t pte, unsigned int nr)
{
	/*
	 * Make sure hardware valid bit is not set. We don't do
	 * tlb flush for this update.
	 */
	VM_WARN_ON(pte_hw_valid(*ptep) && !pte_protnone(*ptep));

	/* Note: mm->context.id might not yet have been assigned as
	 * this context might not have been activated yet when this
	 * is called.
	 */
	pte = set_pte_filter(pte);

	/* Perform the setting of the PTE */
	arch_enter_lazy_mmu_mode();
	for (;;) {
		__set_pte_at(mm, addr, ptep, pte, 0);
		if (--nr == 0)
			break;
		ptep++;
		pte = __pte(pte_val(pte) + (1UL << PTE_RPN_SHIFT));
		addr += PAGE_SIZE;
	}
	arch_leave_lazy_mmu_mode();
}

void unmap_kernel_page(unsigned long va)
{
	pmd_t *pmdp = pmd_off_k(va);
	pte_t *ptep = pte_offset_kernel(pmdp, va);

	pte_clear(&init_mm, va, ptep);
	flush_tlb_kernel_range(va, va + PAGE_SIZE);
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
		assert_pte_locked(vma->vm_mm, address);
		__ptep_set_access_flags(vma, ptep, entry,
					address, mmu_virtual_psize);
	}
	return changed;
}

#ifdef CONFIG_HUGETLB_PAGE
int huge_ptep_set_access_flags(struct vm_area_struct *vma,
			       unsigned long addr, pte_t *ptep,
			       pte_t pte, int dirty)
{
#ifdef HUGETLB_NEED_PRELOAD
	/*
	 * The "return 1" forces a call of update_mmu_cache, which will write a
	 * TLB entry.  Without this, platforms that don't do a write of the TLB
	 * entry in the TLB miss handler asm will fault ad infinitum.
	 */
	ptep_set_access_flags(vma, addr, ptep, pte, dirty);
	return 1;
#else
	int changed, psize;

	pte = set_access_flags_filter(pte, vma, dirty);
	changed = !pte_same(*(ptep), pte);
	if (changed) {

#ifdef CONFIG_PPC_BOOK3S_64
		struct hstate *h = hstate_vma(vma);

		psize = hstate_get_psize(h);
#ifdef CONFIG_DEBUG_VM
		assert_spin_locked(huge_pte_lockptr(h, vma->vm_mm, ptep));
#endif

#else
		/*
		 * Not used on non book3s64 platforms.
		 * 8xx compares it with mmu_virtual_psize to
		 * know if it is a huge page or not.
		 */
		psize = MMU_PAGE_COUNT;
#endif
		__ptep_set_access_flags(vma, ptep, pte, addr, psize);
	}
	return changed;
#endif
}

#if defined(CONFIG_PPC_8xx)
void set_huge_pte_at(struct mm_struct *mm, unsigned long addr, pte_t *ptep,
		     pte_t pte, unsigned long sz)
{
	pmd_t *pmd = pmd_off(mm, addr);
	pte_basic_t val;
	pte_basic_t *entry = (pte_basic_t *)ptep;
	int num, i;

	/*
	 * Make sure hardware valid bit is not set. We don't do
	 * tlb flush for this update.
	 */
	VM_WARN_ON(pte_hw_valid(*ptep) && !pte_protnone(*ptep));

	pte = set_pte_filter(pte);

	val = pte_val(pte);

	num = number_of_cells_per_pte(pmd, val, 1);

	for (i = 0; i < num; i++, entry++, val += SZ_4K)
		*entry = val;
}
#endif
#endif /* CONFIG_HUGETLB_PAGE */

#ifdef CONFIG_DEBUG_VM
void assert_pte_locked(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	spinlock_t *ptl;

	if (mm == &init_mm)
		return;
	pgd = mm->pgd + pgd_index(addr);
	BUG_ON(pgd_none(*pgd));
	p4d = p4d_offset(pgd, addr);
	BUG_ON(p4d_none(*p4d));
	pud = pud_offset(p4d, addr);
	BUG_ON(pud_none(*pud));
	pmd = pmd_offset(pud, addr);
	/*
	 * khugepaged to collapse normal pages to hugepage, first set
	 * pmd to none to force page fault/gup to take mmap_lock. After
	 * pmd is set to none, we do a pte_clear which does this assertion
	 * so if we find pmd none, return.
	 */
	if (pmd_none(*pmd))
		return;
	pte = pte_offset_map_nolock(mm, pmd, addr, &ptl);
	BUG_ON(!pte);
	assert_spin_locked(ptl);
	pte_unmap(pte);
}
#endif /* CONFIG_DEBUG_VM */

unsigned long vmalloc_to_phys(void *va)
{
	unsigned long pfn = vmalloc_to_pfn(va);

	BUG_ON(!pfn);
	return __pa(pfn_to_kaddr(pfn)) + offset_in_page(va);
}
EXPORT_SYMBOL_GPL(vmalloc_to_phys);

/*
 * We have 4 cases for pgds and pmds:
 * (1) invalid (all zeroes)
 * (2) pointer to next table, as normal; bottom 6 bits == 0
 * (3) leaf pte for huge page _PAGE_PTE set
 * (4) hugepd pointer, _PAGE_PTE = 0 and bits [2..6] indicate size of table
 *
 * So long as we atomically load page table pointers we are safe against teardown,
 * we can follow the address down to the page and take a ref on it.
 * This function need to be called with interrupts disabled. We use this variant
 * when we have MSR[EE] = 0 but the paca->irq_soft_mask = IRQS_ENABLED
 */
pte_t *__find_linux_pte(pgd_t *pgdir, unsigned long ea,
			bool *is_thp, unsigned *hpage_shift)
{
	pgd_t *pgdp;
	p4d_t p4d, *p4dp;
	pud_t pud, *pudp;
	pmd_t pmd, *pmdp;
	pte_t *ret_pte;
	hugepd_t *hpdp = NULL;
	unsigned pdshift;

	if (hpage_shift)
		*hpage_shift = 0;

	if (is_thp)
		*is_thp = false;

	/*
	 * Always operate on the local stack value. This make sure the
	 * value don't get updated by a parallel THP split/collapse,
	 * page fault or a page unmap. The return pte_t * is still not
	 * stable. So should be checked there for above conditions.
	 * Top level is an exception because it is folded into p4d.
	 */
	pgdp = pgdir + pgd_index(ea);
	p4dp = p4d_offset(pgdp, ea);
	p4d  = READ_ONCE(*p4dp);
	pdshift = P4D_SHIFT;

	if (p4d_none(p4d))
		return NULL;

	if (p4d_is_leaf(p4d)) {
		ret_pte = (pte_t *)p4dp;
		goto out;
	}

	if (is_hugepd(__hugepd(p4d_val(p4d)))) {
		hpdp = (hugepd_t *)&p4d;
		goto out_huge;
	}

	/*
	 * Even if we end up with an unmap, the pgtable will not
	 * be freed, because we do an rcu free and here we are
	 * irq disabled
	 */
	pdshift = PUD_SHIFT;
	pudp = pud_offset(&p4d, ea);
	pud  = READ_ONCE(*pudp);

	if (pud_none(pud))
		return NULL;

	if (pud_is_leaf(pud)) {
		ret_pte = (pte_t *)pudp;
		goto out;
	}

	if (is_hugepd(__hugepd(pud_val(pud)))) {
		hpdp = (hugepd_t *)&pud;
		goto out_huge;
	}

	pdshift = PMD_SHIFT;
	pmdp = pmd_offset(&pud, ea);
	pmd  = READ_ONCE(*pmdp);

	/*
	 * A hugepage collapse is captured by this condition, see
	 * pmdp_collapse_flush.
	 */
	if (pmd_none(pmd))
		return NULL;

#ifdef CONFIG_PPC_BOOK3S_64
	/*
	 * A hugepage split is captured by this condition, see
	 * pmdp_invalidate.
	 *
	 * Huge page modification can be caught here too.
	 */
	if (pmd_is_serializing(pmd))
		return NULL;
#endif

	if (pmd_trans_huge(pmd) || pmd_devmap(pmd)) {
		if (is_thp)
			*is_thp = true;
		ret_pte = (pte_t *)pmdp;
		goto out;
	}

	if (pmd_is_leaf(pmd)) {
		ret_pte = (pte_t *)pmdp;
		goto out;
	}

	if (is_hugepd(__hugepd(pmd_val(pmd)))) {
		hpdp = (hugepd_t *)&pmd;
		goto out_huge;
	}

	return pte_offset_kernel(&pmd, ea);

out_huge:
	if (!hpdp)
		return NULL;

	ret_pte = hugepte_offset(*hpdp, ea, pdshift);
	pdshift = hugepd_shift(*hpdp);
out:
	if (hpage_shift)
		*hpage_shift = pdshift;
	return ret_pte;
}
EXPORT_SYMBOL_GPL(__find_linux_pte);

/* Note due to the way vm flags are laid out, the bits are XWR */
const pgprot_t protection_map[16] = {
	[VM_NONE]					= PAGE_NONE,
	[VM_READ]					= PAGE_READONLY,
	[VM_WRITE]					= PAGE_COPY,
	[VM_WRITE | VM_READ]				= PAGE_COPY,
	[VM_EXEC]					= PAGE_READONLY_X,
	[VM_EXEC | VM_READ]				= PAGE_READONLY_X,
	[VM_EXEC | VM_WRITE]				= PAGE_COPY_X,
	[VM_EXEC | VM_WRITE | VM_READ]			= PAGE_COPY_X,
	[VM_SHARED]					= PAGE_NONE,
	[VM_SHARED | VM_READ]				= PAGE_READONLY,
	[VM_SHARED | VM_WRITE]				= PAGE_SHARED,
	[VM_SHARED | VM_WRITE | VM_READ]		= PAGE_SHARED,
	[VM_SHARED | VM_EXEC]				= PAGE_READONLY_X,
	[VM_SHARED | VM_EXEC | VM_READ]			= PAGE_READONLY_X,
	[VM_SHARED | VM_EXEC | VM_WRITE]		= PAGE_SHARED_X,
	[VM_SHARED | VM_EXEC | VM_WRITE | VM_READ]	= PAGE_SHARED_X
};

#ifndef CONFIG_PPC_BOOK3S_64
DECLARE_VM_GET_PAGE_PROT
#endif
