/*
 * PPC Huge TLB Page Support for Kernel.
 *
 * Copyright (C) 2003 David Gibson, IBM Corporation.
 * Copyright (C) 2011 Becky Bruce, Freescale Semiconductor
 *
 * Based on the IA-32 version:
 * Copyright (C) 2002, Rohit Seth <rohit.seth@intel.com>
 */

#include <linux/mm.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/hugetlb.h>
#include <linux/export.h>
#include <linux/of_fdt.h>
#include <linux/memblock.h>
#include <linux/moduleparam.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/kmemleak.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>
#include <asm/setup.h>
#include <asm/hugetlb.h>
#include <asm/pte-walk.h>

bool hugetlb_disabled = false;

#define hugepd_none(hpd)	(hpd_val(hpd) == 0)

#define PTE_T_ORDER	(__builtin_ffs(sizeof(pte_basic_t)) - \
			 __builtin_ffs(sizeof(void *)))

pte_t *huge_pte_offset(struct mm_struct *mm, unsigned long addr, unsigned long sz)
{
	/*
	 * Only called for hugetlbfs pages, hence can ignore THP and the
	 * irq disabled walk.
	 */
	return __find_linux_pte(mm->pgd, addr, NULL, NULL);
}

static int __hugepte_alloc(struct mm_struct *mm, hugepd_t *hpdp,
			   unsigned long address, unsigned int pdshift,
			   unsigned int pshift, spinlock_t *ptl)
{
	struct kmem_cache *cachep;
	pte_t *new;
	int i;
	int num_hugepd;

	if (pshift >= pdshift) {
		cachep = PGT_CACHE(PTE_T_ORDER);
		num_hugepd = 1 << (pshift - pdshift);
	} else {
		cachep = PGT_CACHE(pdshift - pshift);
		num_hugepd = 1;
	}

	if (!cachep) {
		WARN_ONCE(1, "No page table cache created for hugetlb tables");
		return -ENOMEM;
	}

	new = kmem_cache_alloc(cachep, pgtable_gfp_flags(mm, GFP_KERNEL));

	BUG_ON(pshift > HUGEPD_SHIFT_MASK);
	BUG_ON((unsigned long)new & HUGEPD_SHIFT_MASK);

	if (!new)
		return -ENOMEM;

	/*
	 * Make sure other cpus find the hugepd set only after a
	 * properly initialized page table is visible to them.
	 * For more details look for comment in __pte_alloc().
	 */
	smp_wmb();

	spin_lock(ptl);
	/*
	 * We have multiple higher-level entries that point to the same
	 * actual pte location.  Fill in each as we go and backtrack on error.
	 * We need all of these so the DTLB pgtable walk code can find the
	 * right higher-level entry without knowing if it's a hugepage or not.
	 */
	for (i = 0; i < num_hugepd; i++, hpdp++) {
		if (unlikely(!hugepd_none(*hpdp)))
			break;
		hugepd_populate(hpdp, new, pshift);
	}
	/* If we bailed from the for loop early, an error occurred, clean up */
	if (i < num_hugepd) {
		for (i = i - 1 ; i >= 0; i--, hpdp--)
			*hpdp = __hugepd(0);
		kmem_cache_free(cachep, new);
	} else {
		kmemleak_ignore(new);
	}
	spin_unlock(ptl);
	return 0;
}

/*
 * At this point we do the placement change only for BOOK3S 64. This would
 * possibly work on other subarchs.
 */
pte_t *huge_pte_alloc(struct mm_struct *mm, struct vm_area_struct *vma,
		      unsigned long addr, unsigned long sz)
{
	pgd_t *pg;
	p4d_t *p4;
	pud_t *pu;
	pmd_t *pm;
	hugepd_t *hpdp = NULL;
	unsigned pshift = __ffs(sz);
	unsigned pdshift = PGDIR_SHIFT;
	spinlock_t *ptl;

	addr &= ~(sz-1);
	pg = pgd_offset(mm, addr);
	p4 = p4d_offset(pg, addr);

#ifdef CONFIG_PPC_BOOK3S_64
	if (pshift == PGDIR_SHIFT)
		/* 16GB huge page */
		return (pte_t *) p4;
	else if (pshift > PUD_SHIFT) {
		/*
		 * We need to use hugepd table
		 */
		ptl = &mm->page_table_lock;
		hpdp = (hugepd_t *)p4;
	} else {
		pdshift = PUD_SHIFT;
		pu = pud_alloc(mm, p4, addr);
		if (!pu)
			return NULL;
		if (pshift == PUD_SHIFT)
			return (pte_t *)pu;
		else if (pshift > PMD_SHIFT) {
			ptl = pud_lockptr(mm, pu);
			hpdp = (hugepd_t *)pu;
		} else {
			pdshift = PMD_SHIFT;
			pm = pmd_alloc(mm, pu, addr);
			if (!pm)
				return NULL;
			if (pshift == PMD_SHIFT)
				/* 16MB hugepage */
				return (pte_t *)pm;
			else {
				ptl = pmd_lockptr(mm, pm);
				hpdp = (hugepd_t *)pm;
			}
		}
	}
#else
	if (pshift >= PGDIR_SHIFT) {
		ptl = &mm->page_table_lock;
		hpdp = (hugepd_t *)p4;
	} else {
		pdshift = PUD_SHIFT;
		pu = pud_alloc(mm, p4, addr);
		if (!pu)
			return NULL;
		if (pshift >= PUD_SHIFT) {
			ptl = pud_lockptr(mm, pu);
			hpdp = (hugepd_t *)pu;
		} else {
			pdshift = PMD_SHIFT;
			pm = pmd_alloc(mm, pu, addr);
			if (!pm)
				return NULL;
			ptl = pmd_lockptr(mm, pm);
			hpdp = (hugepd_t *)pm;
		}
	}
#endif
	if (!hpdp)
		return NULL;

	if (IS_ENABLED(CONFIG_PPC_8xx) && pshift < PMD_SHIFT)
		return pte_alloc_map(mm, (pmd_t *)hpdp, addr);

	BUG_ON(!hugepd_none(*hpdp) && !hugepd_ok(*hpdp));

	if (hugepd_none(*hpdp) && __hugepte_alloc(mm, hpdp, addr,
						  pdshift, pshift, ptl))
		return NULL;

	return hugepte_offset(*hpdp, addr, pdshift);
}

#ifdef CONFIG_PPC_BOOK3S_64
/*
 * Tracks gpages after the device tree is scanned and before the
 * huge_boot_pages list is ready on pseries.
 */
#define MAX_NUMBER_GPAGES	1024
__initdata static u64 gpage_freearray[MAX_NUMBER_GPAGES];
__initdata static unsigned nr_gpages;

/*
 * Build list of addresses of gigantic pages.  This function is used in early
 * boot before the buddy allocator is setup.
 */
void __init pseries_add_gpage(u64 addr, u64 page_size, unsigned long number_of_pages)
{
	if (!addr)
		return;
	while (number_of_pages > 0) {
		gpage_freearray[nr_gpages] = addr;
		nr_gpages++;
		number_of_pages--;
		addr += page_size;
	}
}

static int __init pseries_alloc_bootmem_huge_page(struct hstate *hstate)
{
	struct huge_bootmem_page *m;
	if (nr_gpages == 0)
		return 0;
	m = phys_to_virt(gpage_freearray[--nr_gpages]);
	gpage_freearray[nr_gpages] = 0;
	list_add(&m->list, &huge_boot_pages);
	m->hstate = hstate;
	return 1;
}

bool __init hugetlb_node_alloc_supported(void)
{
	return false;
}
#endif


int __init alloc_bootmem_huge_page(struct hstate *h, int nid)
{

#ifdef CONFIG_PPC_BOOK3S_64
	if (firmware_has_feature(FW_FEATURE_LPAR) && !radix_enabled())
		return pseries_alloc_bootmem_huge_page(h);
#endif
	return __alloc_bootmem_huge_page(h, nid);
}

#ifndef CONFIG_PPC_BOOK3S_64
#define HUGEPD_FREELIST_SIZE \
	((PAGE_SIZE - sizeof(struct hugepd_freelist)) / sizeof(pte_t))

struct hugepd_freelist {
	struct rcu_head	rcu;
	unsigned int index;
	void *ptes[];
};

static DEFINE_PER_CPU(struct hugepd_freelist *, hugepd_freelist_cur);

static void hugepd_free_rcu_callback(struct rcu_head *head)
{
	struct hugepd_freelist *batch =
		container_of(head, struct hugepd_freelist, rcu);
	unsigned int i;

	for (i = 0; i < batch->index; i++)
		kmem_cache_free(PGT_CACHE(PTE_T_ORDER), batch->ptes[i]);

	free_page((unsigned long)batch);
}

static void hugepd_free(struct mmu_gather *tlb, void *hugepte)
{
	struct hugepd_freelist **batchp;

	batchp = &get_cpu_var(hugepd_freelist_cur);

	if (atomic_read(&tlb->mm->mm_users) < 2 ||
	    mm_is_thread_local(tlb->mm)) {
		kmem_cache_free(PGT_CACHE(PTE_T_ORDER), hugepte);
		put_cpu_var(hugepd_freelist_cur);
		return;
	}

	if (*batchp == NULL) {
		*batchp = (struct hugepd_freelist *)__get_free_page(GFP_ATOMIC);
		(*batchp)->index = 0;
	}

	(*batchp)->ptes[(*batchp)->index++] = hugepte;
	if ((*batchp)->index == HUGEPD_FREELIST_SIZE) {
		call_rcu(&(*batchp)->rcu, hugepd_free_rcu_callback);
		*batchp = NULL;
	}
	put_cpu_var(hugepd_freelist_cur);
}
#else
static inline void hugepd_free(struct mmu_gather *tlb, void *hugepte) {}
#endif

/* Return true when the entry to be freed maps more than the area being freed */
static bool range_is_outside_limits(unsigned long start, unsigned long end,
				    unsigned long floor, unsigned long ceiling,
				    unsigned long mask)
{
	if ((start & mask) < floor)
		return true;
	if (ceiling) {
		ceiling &= mask;
		if (!ceiling)
			return true;
	}
	return end - 1 > ceiling - 1;
}

static void free_hugepd_range(struct mmu_gather *tlb, hugepd_t *hpdp, int pdshift,
			      unsigned long start, unsigned long end,
			      unsigned long floor, unsigned long ceiling)
{
	pte_t *hugepte = hugepd_page(*hpdp);
	int i;

	unsigned long pdmask = ~((1UL << pdshift) - 1);
	unsigned int num_hugepd = 1;
	unsigned int shift = hugepd_shift(*hpdp);

	/* Note: On fsl the hpdp may be the first of several */
	if (shift > pdshift)
		num_hugepd = 1 << (shift - pdshift);

	if (range_is_outside_limits(start, end, floor, ceiling, pdmask))
		return;

	for (i = 0; i < num_hugepd; i++, hpdp++)
		*hpdp = __hugepd(0);

	if (shift >= pdshift)
		hugepd_free(tlb, hugepte);
	else
		pgtable_free_tlb(tlb, hugepte,
				 get_hugepd_cache_index(pdshift - shift));
}

static void hugetlb_free_pte_range(struct mmu_gather *tlb, pmd_t *pmd,
				   unsigned long addr, unsigned long end,
				   unsigned long floor, unsigned long ceiling)
{
	pgtable_t token = pmd_pgtable(*pmd);

	if (range_is_outside_limits(addr, end, floor, ceiling, PMD_MASK))
		return;

	pmd_clear(pmd);
	pte_free_tlb(tlb, token, addr);
	mm_dec_nr_ptes(tlb->mm);
}

static void hugetlb_free_pmd_range(struct mmu_gather *tlb, pud_t *pud,
				   unsigned long addr, unsigned long end,
				   unsigned long floor, unsigned long ceiling)
{
	pmd_t *pmd;
	unsigned long next;
	unsigned long start;

	start = addr;
	do {
		unsigned long more;

		pmd = pmd_offset(pud, addr);
		next = pmd_addr_end(addr, end);
		if (!is_hugepd(__hugepd(pmd_val(*pmd)))) {
			if (pmd_none_or_clear_bad(pmd))
				continue;

			/*
			 * if it is not hugepd pointer, we should already find
			 * it cleared.
			 */
			WARN_ON(!IS_ENABLED(CONFIG_PPC_8xx));

			hugetlb_free_pte_range(tlb, pmd, addr, end, floor, ceiling);

			continue;
		}
		/*
		 * Increment next by the size of the huge mapping since
		 * there may be more than one entry at this level for a
		 * single hugepage, but all of them point to
		 * the same kmem cache that holds the hugepte.
		 */
		more = addr + (1 << hugepd_shift(*(hugepd_t *)pmd));
		if (more > next)
			next = more;

		free_hugepd_range(tlb, (hugepd_t *)pmd, PMD_SHIFT,
				  addr, next, floor, ceiling);
	} while (addr = next, addr != end);

	if (range_is_outside_limits(start, end, floor, ceiling, PUD_MASK))
		return;

	pmd = pmd_offset(pud, start & PUD_MASK);
	pud_clear(pud);
	pmd_free_tlb(tlb, pmd, start & PUD_MASK);
	mm_dec_nr_pmds(tlb->mm);
}

static void hugetlb_free_pud_range(struct mmu_gather *tlb, p4d_t *p4d,
				   unsigned long addr, unsigned long end,
				   unsigned long floor, unsigned long ceiling)
{
	pud_t *pud;
	unsigned long next;
	unsigned long start;

	start = addr;
	do {
		pud = pud_offset(p4d, addr);
		next = pud_addr_end(addr, end);
		if (!is_hugepd(__hugepd(pud_val(*pud)))) {
			if (pud_none_or_clear_bad(pud))
				continue;
			hugetlb_free_pmd_range(tlb, pud, addr, next, floor,
					       ceiling);
		} else {
			unsigned long more;
			/*
			 * Increment next by the size of the huge mapping since
			 * there may be more than one entry at this level for a
			 * single hugepage, but all of them point to
			 * the same kmem cache that holds the hugepte.
			 */
			more = addr + (1 << hugepd_shift(*(hugepd_t *)pud));
			if (more > next)
				next = more;

			free_hugepd_range(tlb, (hugepd_t *)pud, PUD_SHIFT,
					  addr, next, floor, ceiling);
		}
	} while (addr = next, addr != end);

	if (range_is_outside_limits(start, end, floor, ceiling, PGDIR_MASK))
		return;

	pud = pud_offset(p4d, start & PGDIR_MASK);
	p4d_clear(p4d);
	pud_free_tlb(tlb, pud, start & PGDIR_MASK);
	mm_dec_nr_puds(tlb->mm);
}

/*
 * This function frees user-level page tables of a process.
 */
void hugetlb_free_pgd_range(struct mmu_gather *tlb,
			    unsigned long addr, unsigned long end,
			    unsigned long floor, unsigned long ceiling)
{
	pgd_t *pgd;
	p4d_t *p4d;
	unsigned long next;

	/*
	 * Because there are a number of different possible pagetable
	 * layouts for hugepage ranges, we limit knowledge of how
	 * things should be laid out to the allocation path
	 * (huge_pte_alloc(), above).  Everything else works out the
	 * structure as it goes from information in the hugepd
	 * pointers.  That means that we can't here use the
	 * optimization used in the normal page free_pgd_range(), of
	 * checking whether we're actually covering a large enough
	 * range to have to do anything at the top level of the walk
	 * instead of at the bottom.
	 *
	 * To make sense of this, you should probably go read the big
	 * block comment at the top of the normal free_pgd_range(),
	 * too.
	 */

	do {
		next = pgd_addr_end(addr, end);
		pgd = pgd_offset(tlb->mm, addr);
		p4d = p4d_offset(pgd, addr);
		if (!is_hugepd(__hugepd(pgd_val(*pgd)))) {
			if (p4d_none_or_clear_bad(p4d))
				continue;
			hugetlb_free_pud_range(tlb, p4d, addr, next, floor, ceiling);
		} else {
			unsigned long more;
			/*
			 * Increment next by the size of the huge mapping since
			 * there may be more than one entry at the pgd level
			 * for a single hugepage, but all of them point to the
			 * same kmem cache that holds the hugepte.
			 */
			more = addr + (1 << hugepd_shift(*(hugepd_t *)pgd));
			if (more > next)
				next = more;

			free_hugepd_range(tlb, (hugepd_t *)p4d, PGDIR_SHIFT,
					  addr, next, floor, ceiling);
		}
	} while (addr = next, addr != end);
}

struct page *follow_huge_pd(struct vm_area_struct *vma,
			    unsigned long address, hugepd_t hpd,
			    int flags, int pdshift)
{
	pte_t *ptep;
	spinlock_t *ptl;
	struct page *page = NULL;
	unsigned long mask;
	int shift = hugepd_shift(hpd);
	struct mm_struct *mm = vma->vm_mm;

retry:
	/*
	 * hugepage directory entries are protected by mm->page_table_lock
	 * Use this instead of huge_pte_lockptr
	 */
	ptl = &mm->page_table_lock;
	spin_lock(ptl);

	ptep = hugepte_offset(hpd, address, pdshift);
	if (pte_present(*ptep)) {
		mask = (1UL << shift) - 1;
		page = pte_page(*ptep);
		page += ((address & mask) >> PAGE_SHIFT);
		if (flags & FOLL_GET)
			get_page(page);
	} else {
		if (is_hugetlb_entry_migration(*ptep)) {
			spin_unlock(ptl);
			__migration_entry_wait(mm, ptep, ptl);
			goto retry;
		}
	}
	spin_unlock(ptl);
	return page;
}

bool __init arch_hugetlb_valid_size(unsigned long size)
{
	int shift = __ffs(size);
	int mmu_psize;

	/* Check that it is a page size supported by the hardware and
	 * that it fits within pagetable and slice limits. */
	if (size <= PAGE_SIZE || !is_power_of_2(size))
		return false;

	mmu_psize = check_and_get_huge_psize(shift);
	if (mmu_psize < 0)
		return false;

	BUG_ON(mmu_psize_defs[mmu_psize].shift != shift);

	return true;
}

static int __init add_huge_page_size(unsigned long long size)
{
	int shift = __ffs(size);

	if (!arch_hugetlb_valid_size((unsigned long)size))
		return -EINVAL;

	hugetlb_add_hstate(shift - PAGE_SHIFT);
	return 0;
}

static int __init hugetlbpage_init(void)
{
	bool configured = false;
	int psize;

	if (hugetlb_disabled) {
		pr_info("HugeTLB support is disabled!\n");
		return 0;
	}

	if (IS_ENABLED(CONFIG_PPC_BOOK3S_64) && !radix_enabled() &&
	    !mmu_has_feature(MMU_FTR_16M_PAGE))
		return -ENODEV;

	for (psize = 0; psize < MMU_PAGE_COUNT; ++psize) {
		unsigned shift;
		unsigned pdshift;

		if (!mmu_psize_defs[psize].shift)
			continue;

		shift = mmu_psize_to_shift(psize);

#ifdef CONFIG_PPC_BOOK3S_64
		if (shift > PGDIR_SHIFT)
			continue;
		else if (shift > PUD_SHIFT)
			pdshift = PGDIR_SHIFT;
		else if (shift > PMD_SHIFT)
			pdshift = PUD_SHIFT;
		else
			pdshift = PMD_SHIFT;
#else
		if (shift < PUD_SHIFT)
			pdshift = PMD_SHIFT;
		else if (shift < PGDIR_SHIFT)
			pdshift = PUD_SHIFT;
		else
			pdshift = PGDIR_SHIFT;
#endif

		if (add_huge_page_size(1ULL << shift) < 0)
			continue;
		/*
		 * if we have pdshift and shift value same, we don't
		 * use pgt cache for hugepd.
		 */
		if (pdshift > shift) {
			if (!IS_ENABLED(CONFIG_PPC_8xx))
				pgtable_cache_add(pdshift - shift);
		} else if (IS_ENABLED(CONFIG_PPC_FSL_BOOK3E) ||
			   IS_ENABLED(CONFIG_PPC_8xx)) {
			pgtable_cache_add(PTE_T_ORDER);
		}

		configured = true;
	}

	if (!configured)
		pr_info("Failed to initialize. Disabling HugeTLB");

	return 0;
}

arch_initcall(hugetlbpage_init);

void __init gigantic_hugetlb_cma_reserve(void)
{
	unsigned long order = 0;

	if (radix_enabled())
		order = PUD_SHIFT - PAGE_SHIFT;
	else if (!firmware_has_feature(FW_FEATURE_LPAR) && mmu_psize_defs[MMU_PAGE_16G].shift)
		/*
		 * For pseries we do use ibm,expected#pages for reserving 16G pages.
		 */
		order = mmu_psize_to_shift(MMU_PAGE_16G) - PAGE_SHIFT;

	if (order) {
		VM_WARN_ON(order < MAX_ORDER);
		hugetlb_cma_reserve(order);
	}
}
