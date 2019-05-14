/*
 * Copyright 2015-2016, Aneesh Kumar K.V, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/sched.h>
#include <linux/mm_types.h>
#include <linux/memblock.h>
#include <misc/cxl-base.h>

#include <asm/pgalloc.h>
#include <asm/tlb.h>
#include <asm/trace.h>
#include <asm/powernv.h>

#include <mm/mmu_decl.h>
#include <trace/events/thp.h>

unsigned long __pmd_frag_nr;
EXPORT_SYMBOL(__pmd_frag_nr);
unsigned long __pmd_frag_size_shift;
EXPORT_SYMBOL(__pmd_frag_size_shift);

int (*register_process_table)(unsigned long base, unsigned long page_size,
			      unsigned long tbl_size);

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
/*
 * This is called when relaxing access to a hugepage. It's also called in the page
 * fault path when we don't hit any of the major fault cases, ie, a minor
 * update of _PAGE_ACCESSED, _PAGE_DIRTY, etc... The generic code will have
 * handled those two for us, we additionally deal with missing execute
 * permission here on some processors
 */
int pmdp_set_access_flags(struct vm_area_struct *vma, unsigned long address,
			  pmd_t *pmdp, pmd_t entry, int dirty)
{
	int changed;
#ifdef CONFIG_DEBUG_VM
	WARN_ON(!pmd_trans_huge(*pmdp) && !pmd_devmap(*pmdp));
	assert_spin_locked(pmd_lockptr(vma->vm_mm, pmdp));
#endif
	changed = !pmd_same(*(pmdp), entry);
	if (changed) {
		/*
		 * We can use MMU_PAGE_2M here, because only radix
		 * path look at the psize.
		 */
		__ptep_set_access_flags(vma, pmdp_ptep(pmdp),
					pmd_pte(entry), address, MMU_PAGE_2M);
	}
	return changed;
}

int pmdp_test_and_clear_young(struct vm_area_struct *vma,
			      unsigned long address, pmd_t *pmdp)
{
	return __pmdp_test_and_clear_young(vma->vm_mm, address, pmdp);
}
/*
 * set a new huge pmd. We should not be called for updating
 * an existing pmd entry. That should go via pmd_hugepage_update.
 */
void set_pmd_at(struct mm_struct *mm, unsigned long addr,
		pmd_t *pmdp, pmd_t pmd)
{
#ifdef CONFIG_DEBUG_VM
	/*
	 * Make sure hardware valid bit is not set. We don't do
	 * tlb flush for this update.
	 */

	WARN_ON(pte_hw_valid(pmd_pte(*pmdp)) && !pte_protnone(pmd_pte(*pmdp)));
	assert_spin_locked(pmd_lockptr(mm, pmdp));
	WARN_ON(!(pmd_large(pmd)));
#endif
	trace_hugepage_set_pmd(addr, pmd_val(pmd));
	return set_pte_at(mm, addr, pmdp_ptep(pmdp), pmd_pte(pmd));
}

static void do_nothing(void *unused)
{

}
/*
 * Serialize against find_current_mm_pte which does lock-less
 * lookup in page tables with local interrupts disabled. For huge pages
 * it casts pmd_t to pte_t. Since format of pte_t is different from
 * pmd_t we want to prevent transit from pmd pointing to page table
 * to pmd pointing to huge page (and back) while interrupts are disabled.
 * We clear pmd to possibly replace it with page table pointer in
 * different code paths. So make sure we wait for the parallel
 * find_current_mm_pte to finish.
 */
void serialize_against_pte_lookup(struct mm_struct *mm)
{
	smp_mb();
	smp_call_function_many(mm_cpumask(mm), do_nothing, NULL, 1);
}

/*
 * We use this to invalidate a pmdp entry before switching from a
 * hugepte to regular pmd entry.
 */
pmd_t pmdp_invalidate(struct vm_area_struct *vma, unsigned long address,
		     pmd_t *pmdp)
{
	unsigned long old_pmd;

	old_pmd = pmd_hugepage_update(vma->vm_mm, address, pmdp, _PAGE_PRESENT, _PAGE_INVALID);
	flush_pmd_tlb_range(vma, address, address + HPAGE_PMD_SIZE);
	/*
	 * This ensures that generic code that rely on IRQ disabling
	 * to prevent a parallel THP split work as expected.
	 *
	 * Marking the entry with _PAGE_INVALID && ~_PAGE_PRESENT requires
	 * a special case check in pmd_access_permitted.
	 */
	serialize_against_pte_lookup(vma->vm_mm);
	return __pmd(old_pmd);
}

static pmd_t pmd_set_protbits(pmd_t pmd, pgprot_t pgprot)
{
	return __pmd(pmd_val(pmd) | pgprot_val(pgprot));
}

pmd_t pfn_pmd(unsigned long pfn, pgprot_t pgprot)
{
	unsigned long pmdv;

	pmdv = (pfn << PAGE_SHIFT) & PTE_RPN_MASK;
	return pmd_set_protbits(__pmd(pmdv), pgprot);
}

pmd_t mk_pmd(struct page *page, pgprot_t pgprot)
{
	return pfn_pmd(page_to_pfn(page), pgprot);
}

pmd_t pmd_modify(pmd_t pmd, pgprot_t newprot)
{
	unsigned long pmdv;

	pmdv = pmd_val(pmd);
	pmdv &= _HPAGE_CHG_MASK;
	return pmd_set_protbits(__pmd(pmdv), newprot);
}

/*
 * This is called at the end of handling a user page fault, when the
 * fault has been handled by updating a HUGE PMD entry in the linux page tables.
 * We use it to preload an HPTE into the hash table corresponding to
 * the updated linux HUGE PMD entry.
 */
void update_mmu_cache_pmd(struct vm_area_struct *vma, unsigned long addr,
			  pmd_t *pmd)
{
	if (radix_enabled())
		prefetch((void *)addr);
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

/* For use by kexec */
void mmu_cleanup_all(void)
{
	if (radix_enabled())
		radix__mmu_cleanup_all();
	else if (mmu_hash_ops.hpte_clear_all)
		mmu_hash_ops.hpte_clear_all();
}

#ifdef CONFIG_MEMORY_HOTPLUG
int __meminit create_section_mapping(unsigned long start, unsigned long end, int nid)
{
	if (radix_enabled())
		return radix__create_section_mapping(start, end, nid);

	return hash__create_section_mapping(start, end, nid);
}

int __meminit remove_section_mapping(unsigned long start, unsigned long end)
{
	if (radix_enabled())
		return radix__remove_section_mapping(start, end);

	return hash__remove_section_mapping(start, end);
}
#endif /* CONFIG_MEMORY_HOTPLUG */

void __init mmu_partition_table_init(void)
{
	unsigned long patb_size = 1UL << PATB_SIZE_SHIFT;
	unsigned long ptcr;

	BUILD_BUG_ON_MSG((PATB_SIZE_SHIFT > 36), "Partition table size too large.");
	/* Initialize the Partition Table with no entries */
	partition_tb = memblock_alloc(patb_size, patb_size);
	if (!partition_tb)
		panic("%s: Failed to allocate %lu bytes align=0x%lx\n",
		      __func__, patb_size, patb_size);

	/*
	 * update partition table control register,
	 * 64 K size.
	 */
	ptcr = __pa(partition_tb) | (PATB_SIZE_SHIFT - 12);
	mtspr(SPRN_PTCR, ptcr);
	powernv_set_nmmu_ptcr(ptcr);
}

void mmu_partition_table_set_entry(unsigned int lpid, unsigned long dw0,
				   unsigned long dw1)
{
	unsigned long old = be64_to_cpu(partition_tb[lpid].patb0);

	partition_tb[lpid].patb0 = cpu_to_be64(dw0);
	partition_tb[lpid].patb1 = cpu_to_be64(dw1);

	/*
	 * Global flush of TLBs and partition table caches for this lpid.
	 * The type of flush (hash or radix) depends on what the previous
	 * use of this partition ID was, not the new use.
	 */
	asm volatile("ptesync" : : : "memory");
	if (old & PATB_HR) {
		asm volatile(PPC_TLBIE_5(%0,%1,2,0,1) : :
			     "r" (TLBIEL_INVAL_SET_LPID), "r" (lpid));
		asm volatile(PPC_TLBIE_5(%0,%1,2,1,1) : :
			     "r" (TLBIEL_INVAL_SET_LPID), "r" (lpid));
		trace_tlbie(lpid, 0, TLBIEL_INVAL_SET_LPID, lpid, 2, 0, 1);
	} else {
		asm volatile(PPC_TLBIE_5(%0,%1,2,0,0) : :
			     "r" (TLBIEL_INVAL_SET_LPID), "r" (lpid));
		trace_tlbie(lpid, 0, TLBIEL_INVAL_SET_LPID, lpid, 2, 0, 0);
	}
	/* do we need fixup here ?*/
	asm volatile("eieio; tlbsync; ptesync" : : : "memory");
}
EXPORT_SYMBOL_GPL(mmu_partition_table_set_entry);

static pmd_t *get_pmd_from_cache(struct mm_struct *mm)
{
	void *pmd_frag, *ret;

	if (PMD_FRAG_NR == 1)
		return NULL;

	spin_lock(&mm->page_table_lock);
	ret = mm->context.pmd_frag;
	if (ret) {
		pmd_frag = ret + PMD_FRAG_SIZE;
		/*
		 * If we have taken up all the fragments mark PTE page NULL
		 */
		if (((unsigned long)pmd_frag & ~PAGE_MASK) == 0)
			pmd_frag = NULL;
		mm->context.pmd_frag = pmd_frag;
	}
	spin_unlock(&mm->page_table_lock);
	return (pmd_t *)ret;
}

static pmd_t *__alloc_for_pmdcache(struct mm_struct *mm)
{
	void *ret = NULL;
	struct page *page;
	gfp_t gfp = GFP_KERNEL_ACCOUNT | __GFP_ZERO;

	if (mm == &init_mm)
		gfp &= ~__GFP_ACCOUNT;
	page = alloc_page(gfp);
	if (!page)
		return NULL;
	if (!pgtable_pmd_page_ctor(page)) {
		__free_pages(page, 0);
		return NULL;
	}

	atomic_set(&page->pt_frag_refcount, 1);

	ret = page_address(page);
	/*
	 * if we support only one fragment just return the
	 * allocated page.
	 */
	if (PMD_FRAG_NR == 1)
		return ret;

	spin_lock(&mm->page_table_lock);
	/*
	 * If we find pgtable_page set, we return
	 * the allocated page with single fragement
	 * count.
	 */
	if (likely(!mm->context.pmd_frag)) {
		atomic_set(&page->pt_frag_refcount, PMD_FRAG_NR);
		mm->context.pmd_frag = ret + PMD_FRAG_SIZE;
	}
	spin_unlock(&mm->page_table_lock);

	return (pmd_t *)ret;
}

pmd_t *pmd_fragment_alloc(struct mm_struct *mm, unsigned long vmaddr)
{
	pmd_t *pmd;

	pmd = get_pmd_from_cache(mm);
	if (pmd)
		return pmd;

	return __alloc_for_pmdcache(mm);
}

void pmd_fragment_free(unsigned long *pmd)
{
	struct page *page = virt_to_page(pmd);

	BUG_ON(atomic_read(&page->pt_frag_refcount) <= 0);
	if (atomic_dec_and_test(&page->pt_frag_refcount)) {
		pgtable_pmd_page_dtor(page);
		__free_page(page);
	}
}

static inline void pgtable_free(void *table, int index)
{
	switch (index) {
	case PTE_INDEX:
		pte_fragment_free(table, 0);
		break;
	case PMD_INDEX:
		pmd_fragment_free(table);
		break;
	case PUD_INDEX:
		kmem_cache_free(PGT_CACHE(PUD_CACHE_INDEX), table);
		break;
#if defined(CONFIG_PPC_4K_PAGES) && defined(CONFIG_HUGETLB_PAGE)
		/* 16M hugepd directory at pud level */
	case HTLB_16M_INDEX:
		BUILD_BUG_ON(H_16M_CACHE_INDEX <= 0);
		kmem_cache_free(PGT_CACHE(H_16M_CACHE_INDEX), table);
		break;
		/* 16G hugepd directory at the pgd level */
	case HTLB_16G_INDEX:
		BUILD_BUG_ON(H_16G_CACHE_INDEX <= 0);
		kmem_cache_free(PGT_CACHE(H_16G_CACHE_INDEX), table);
		break;
#endif
		/* We don't free pgd table via RCU callback */
	default:
		BUG();
	}
}

#ifdef CONFIG_SMP
void pgtable_free_tlb(struct mmu_gather *tlb, void *table, int index)
{
	unsigned long pgf = (unsigned long)table;

	BUG_ON(index > MAX_PGTABLE_INDEX_SIZE);
	pgf |= index;
	tlb_remove_table(tlb, (void *)pgf);
}

void __tlb_remove_table(void *_table)
{
	void *table = (void *)((unsigned long)_table & ~MAX_PGTABLE_INDEX_SIZE);
	unsigned int index = (unsigned long)_table & MAX_PGTABLE_INDEX_SIZE;

	return pgtable_free(table, index);
}
#else
void pgtable_free_tlb(struct mmu_gather *tlb, void *table, int index)
{
	return pgtable_free(table, index);
}
#endif

#ifdef CONFIG_PROC_FS
atomic_long_t direct_pages_count[MMU_PAGE_COUNT];

void arch_report_meminfo(struct seq_file *m)
{
	/*
	 * Hash maps the memory with one size mmu_linear_psize.
	 * So don't bother to print these on hash
	 */
	if (!radix_enabled())
		return;
	seq_printf(m, "DirectMap4k:    %8lu kB\n",
		   atomic_long_read(&direct_pages_count[MMU_PAGE_4K]) << 2);
	seq_printf(m, "DirectMap64k:    %8lu kB\n",
		   atomic_long_read(&direct_pages_count[MMU_PAGE_64K]) << 6);
	seq_printf(m, "DirectMap2M:    %8lu kB\n",
		   atomic_long_read(&direct_pages_count[MMU_PAGE_2M]) << 11);
	seq_printf(m, "DirectMap1G:    %8lu kB\n",
		   atomic_long_read(&direct_pages_count[MMU_PAGE_1G]) << 20);
}
#endif /* CONFIG_PROC_FS */

pte_t ptep_modify_prot_start(struct vm_area_struct *vma, unsigned long addr,
			     pte_t *ptep)
{
	unsigned long pte_val;

	/*
	 * Clear the _PAGE_PRESENT so that no hardware parallel update is
	 * possible. Also keep the pte_present true so that we don't take
	 * wrong fault.
	 */
	pte_val = pte_update(vma->vm_mm, addr, ptep, _PAGE_PRESENT, _PAGE_INVALID, 0);

	return __pte(pte_val);

}

void ptep_modify_prot_commit(struct vm_area_struct *vma, unsigned long addr,
			     pte_t *ptep, pte_t old_pte, pte_t pte)
{
	if (radix_enabled())
		return radix__ptep_modify_prot_commit(vma, addr,
						      ptep, old_pte, pte);
	set_pte_at(vma->vm_mm, addr, ptep, pte);
}

/*
 * For hash translation mode, we use the deposited table to store hash slot
 * information and they are stored at PTRS_PER_PMD offset from related pmd
 * location. Hence a pmd move requires deposit and withdraw.
 *
 * For radix translation with split pmd ptl, we store the deposited table in the
 * pmd page. Hence if we have different pmd page we need to withdraw during pmd
 * move.
 *
 * With hash we use deposited table always irrespective of anon or not.
 * With radix we use deposited table only for anonymous mapping.
 */
int pmd_move_must_withdraw(struct spinlock *new_pmd_ptl,
			   struct spinlock *old_pmd_ptl,
			   struct vm_area_struct *vma)
{
	if (radix_enabled())
		return (new_pmd_ptl != old_pmd_ptl) && vma_is_anonymous(vma);

	return true;
}

int ioremap_range(unsigned long ea, phys_addr_t pa, unsigned long size, pgprot_t prot, int nid)
{
	unsigned long i;

	if (radix_enabled())
		return radix__ioremap_range(ea, pa, size, prot, nid);

	for (i = 0; i < size; i += PAGE_SIZE) {
		int err = map_kernel_page(ea + i, pa + i, prot);
		if (err) {
			if (slab_is_available())
				unmap_kernel_range(ea, size);
			else
				WARN_ON_ONCE(1); /* Should clean up */
			return err;
		}
	}

	return 0;
}
