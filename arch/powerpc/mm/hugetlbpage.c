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
#include <linux/bootmem.h>
#include <linux/moduleparam.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>
#include <asm/setup.h>
#include <asm/hugetlb.h>
#include <asm/pte-walk.h>


#ifdef CONFIG_HUGETLB_PAGE

#define PAGE_SHIFT_64K	16
#define PAGE_SHIFT_512K	19
#define PAGE_SHIFT_8M	23
#define PAGE_SHIFT_16M	24
#define PAGE_SHIFT_16G	34

unsigned int HPAGE_SHIFT;
EXPORT_SYMBOL(HPAGE_SHIFT);

#define hugepd_none(hpd)	(hpd_val(hpd) == 0)

pte_t *huge_pte_offset(struct mm_struct *mm, unsigned long addr, unsigned long sz)
{
	/*
	 * Only called for hugetlbfs pages, hence can ignore THP and the
	 * irq disabled walk.
	 */
	return __find_linux_pte(mm->pgd, addr, NULL, NULL);
}

static int __hugepte_alloc(struct mm_struct *mm, hugepd_t *hpdp,
			   unsigned long address, unsigned pdshift, unsigned pshift)
{
	struct kmem_cache *cachep;
	pte_t *new;
	int i;
	int num_hugepd;

	if (pshift >= pdshift) {
		cachep = hugepte_cache;
		num_hugepd = 1 << (pshift - pdshift);
	} else {
		cachep = PGT_CACHE(pdshift - pshift);
		num_hugepd = 1;
	}

	new = kmem_cache_zalloc(cachep, pgtable_gfp_flags(mm, GFP_KERNEL));

	BUG_ON(pshift > HUGEPD_SHIFT_MASK);
	BUG_ON((unsigned long)new & HUGEPD_SHIFT_MASK);

	if (! new)
		return -ENOMEM;

	/*
	 * Make sure other cpus find the hugepd set only after a
	 * properly initialized page table is visible to them.
	 * For more details look for comment in __pte_alloc().
	 */
	smp_wmb();

	spin_lock(&mm->page_table_lock);

	/*
	 * We have multiple higher-level entries that point to the same
	 * actual pte location.  Fill in each as we go and backtrack on error.
	 * We need all of these so the DTLB pgtable walk code can find the
	 * right higher-level entry without knowing if it's a hugepage or not.
	 */
	for (i = 0; i < num_hugepd; i++, hpdp++) {
		if (unlikely(!hugepd_none(*hpdp)))
			break;
		else {
#ifdef CONFIG_PPC_BOOK3S_64
			*hpdp = __hugepd(__pa(new) |
					 (shift_to_mmu_psize(pshift) << 2));
#elif defined(CONFIG_PPC_8xx)
			*hpdp = __hugepd(__pa(new) |
					 (pshift == PAGE_SHIFT_8M ? _PMD_PAGE_8M :
					  _PMD_PAGE_512K) | _PMD_PRESENT);
#else
			/* We use the old format for PPC_FSL_BOOK3E */
			*hpdp = __hugepd(((unsigned long)new & ~PD_HUGE) | pshift);
#endif
		}
	}
	/* If we bailed from the for loop early, an error occurred, clean up */
	if (i < num_hugepd) {
		for (i = i - 1 ; i >= 0; i--, hpdp--)
			*hpdp = __hugepd(0);
		kmem_cache_free(cachep, new);
	}
	spin_unlock(&mm->page_table_lock);
	return 0;
}

/*
 * These macros define how to determine which level of the page table holds
 * the hpdp.
 */
#if defined(CONFIG_PPC_FSL_BOOK3E) || defined(CONFIG_PPC_8xx)
#define HUGEPD_PGD_SHIFT PGDIR_SHIFT
#define HUGEPD_PUD_SHIFT PUD_SHIFT
#else
#define HUGEPD_PGD_SHIFT PUD_SHIFT
#define HUGEPD_PUD_SHIFT PMD_SHIFT
#endif

/*
 * At this point we do the placement change only for BOOK3S 64. This would
 * possibly work on other subarchs.
 */
pte_t *huge_pte_alloc(struct mm_struct *mm, unsigned long addr, unsigned long sz)
{
	pgd_t *pg;
	pud_t *pu;
	pmd_t *pm;
	hugepd_t *hpdp = NULL;
	unsigned pshift = __ffs(sz);
	unsigned pdshift = PGDIR_SHIFT;

	addr &= ~(sz-1);
	pg = pgd_offset(mm, addr);

#ifdef CONFIG_PPC_BOOK3S_64
	if (pshift == PGDIR_SHIFT)
		/* 16GB huge page */
		return (pte_t *) pg;
	else if (pshift > PUD_SHIFT)
		/*
		 * We need to use hugepd table
		 */
		hpdp = (hugepd_t *)pg;
	else {
		pdshift = PUD_SHIFT;
		pu = pud_alloc(mm, pg, addr);
		if (pshift == PUD_SHIFT)
			return (pte_t *)pu;
		else if (pshift > PMD_SHIFT)
			hpdp = (hugepd_t *)pu;
		else {
			pdshift = PMD_SHIFT;
			pm = pmd_alloc(mm, pu, addr);
			if (pshift == PMD_SHIFT)
				/* 16MB hugepage */
				return (pte_t *)pm;
			else
				hpdp = (hugepd_t *)pm;
		}
	}
#else
	if (pshift >= HUGEPD_PGD_SHIFT) {
		hpdp = (hugepd_t *)pg;
	} else {
		pdshift = PUD_SHIFT;
		pu = pud_alloc(mm, pg, addr);
		if (pshift >= HUGEPD_PUD_SHIFT) {
			hpdp = (hugepd_t *)pu;
		} else {
			pdshift = PMD_SHIFT;
			pm = pmd_alloc(mm, pu, addr);
			hpdp = (hugepd_t *)pm;
		}
	}
#endif
	if (!hpdp)
		return NULL;

	BUG_ON(!hugepd_none(*hpdp) && !hugepd_ok(*hpdp));

	if (hugepd_none(*hpdp) && __hugepte_alloc(mm, hpdp, addr, pdshift, pshift))
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

int __init pseries_alloc_bootmem_huge_page(struct hstate *hstate)
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
#endif


int __init alloc_bootmem_huge_page(struct hstate *h)
{

#ifdef CONFIG_PPC_BOOK3S_64
	if (firmware_has_feature(FW_FEATURE_LPAR) && !radix_enabled())
		return pseries_alloc_bootmem_huge_page(h);
#endif
	return __alloc_bootmem_huge_page(h);
}

#if defined(CONFIG_PPC_FSL_BOOK3E) || defined(CONFIG_PPC_8xx)
#define HUGEPD_FREELIST_SIZE \
	((PAGE_SIZE - sizeof(struct hugepd_freelist)) / sizeof(pte_t))

struct hugepd_freelist {
	struct rcu_head	rcu;
	unsigned int index;
	void *ptes[0];
};

static DEFINE_PER_CPU(struct hugepd_freelist *, hugepd_freelist_cur);

static void hugepd_free_rcu_callback(struct rcu_head *head)
{
	struct hugepd_freelist *batch =
		container_of(head, struct hugepd_freelist, rcu);
	unsigned int i;

	for (i = 0; i < batch->index; i++)
		kmem_cache_free(hugepte_cache, batch->ptes[i]);

	free_page((unsigned long)batch);
}

static void hugepd_free(struct mmu_gather *tlb, void *hugepte)
{
	struct hugepd_freelist **batchp;

	batchp = &get_cpu_var(hugepd_freelist_cur);

	if (atomic_read(&tlb->mm->mm_users) < 2 ||
	    mm_is_thread_local(tlb->mm)) {
		kmem_cache_free(hugepte_cache, hugepte);
		put_cpu_var(hugepd_freelist_cur);
		return;
	}

	if (*batchp == NULL) {
		*batchp = (struct hugepd_freelist *)__get_free_page(GFP_ATOMIC);
		(*batchp)->index = 0;
	}

	(*batchp)->ptes[(*batchp)->index++] = hugepte;
	if ((*batchp)->index == HUGEPD_FREELIST_SIZE) {
		call_rcu_sched(&(*batchp)->rcu, hugepd_free_rcu_callback);
		*batchp = NULL;
	}
	put_cpu_var(hugepd_freelist_cur);
}
#else
static inline void hugepd_free(struct mmu_gather *tlb, void *hugepte) {}
#endif

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

	start &= pdmask;
	if (start < floor)
		return;
	if (ceiling) {
		ceiling &= pdmask;
		if (! ceiling)
			return;
	}
	if (end - 1 > ceiling - 1)
		return;

	for (i = 0; i < num_hugepd; i++, hpdp++)
		*hpdp = __hugepd(0);

	if (shift >= pdshift)
		hugepd_free(tlb, hugepte);
	else
		pgtable_free_tlb(tlb, hugepte, pdshift - shift);
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
			/*
			 * if it is not hugepd pointer, we should already find
			 * it cleared.
			 */
			WARN_ON(!pmd_none_or_clear_bad(pmd));
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

	start &= PUD_MASK;
	if (start < floor)
		return;
	if (ceiling) {
		ceiling &= PUD_MASK;
		if (!ceiling)
			return;
	}
	if (end - 1 > ceiling - 1)
		return;

	pmd = pmd_offset(pud, start);
	pud_clear(pud);
	pmd_free_tlb(tlb, pmd, start);
	mm_dec_nr_pmds(tlb->mm);
}

static void hugetlb_free_pud_range(struct mmu_gather *tlb, pgd_t *pgd,
				   unsigned long addr, unsigned long end,
				   unsigned long floor, unsigned long ceiling)
{
	pud_t *pud;
	unsigned long next;
	unsigned long start;

	start = addr;
	do {
		pud = pud_offset(pgd, addr);
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

	start &= PGDIR_MASK;
	if (start < floor)
		return;
	if (ceiling) {
		ceiling &= PGDIR_MASK;
		if (!ceiling)
			return;
	}
	if (end - 1 > ceiling - 1)
		return;

	pud = pud_offset(pgd, start);
	pgd_clear(pgd);
	pud_free_tlb(tlb, pud, start);
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
		if (!is_hugepd(__hugepd(pgd_val(*pgd)))) {
			if (pgd_none_or_clear_bad(pgd))
				continue;
			hugetlb_free_pud_range(tlb, pgd, addr, next, floor, ceiling);
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

			free_hugepd_range(tlb, (hugepd_t *)pgd, PGDIR_SHIFT,
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

static unsigned long hugepte_addr_end(unsigned long addr, unsigned long end,
				      unsigned long sz)
{
	unsigned long __boundary = (addr + sz) & ~(sz-1);
	return (__boundary - 1 < end - 1) ? __boundary : end;
}

int gup_huge_pd(hugepd_t hugepd, unsigned long addr, unsigned pdshift,
		unsigned long end, int write, struct page **pages, int *nr)
{
	pte_t *ptep;
	unsigned long sz = 1UL << hugepd_shift(hugepd);
	unsigned long next;

	ptep = hugepte_offset(hugepd, addr, pdshift);
	do {
		next = hugepte_addr_end(addr, end, sz);
		if (!gup_hugepte(ptep, sz, addr, end, write, pages, nr))
			return 0;
	} while (ptep++, addr = next, addr != end);

	return 1;
}

#ifdef CONFIG_PPC_MM_SLICES
unsigned long hugetlb_get_unmapped_area(struct file *file, unsigned long addr,
					unsigned long len, unsigned long pgoff,
					unsigned long flags)
{
	struct hstate *hstate = hstate_file(file);
	int mmu_psize = shift_to_mmu_psize(huge_page_shift(hstate));

	if (radix_enabled())
		return radix__hugetlb_get_unmapped_area(file, addr, len,
						       pgoff, flags);
	return slice_get_unmapped_area(addr, len, flags, mmu_psize, 1);
}
#endif

unsigned long vma_mmu_pagesize(struct vm_area_struct *vma)
{
#ifdef CONFIG_PPC_MM_SLICES
	unsigned int psize = get_slice_psize(vma->vm_mm, vma->vm_start);
	/* With radix we don't use slice, so derive it from vma*/
	if (!radix_enabled())
		return 1UL << mmu_psize_to_shift(psize);
#endif
	if (!is_vm_hugetlb_page(vma))
		return PAGE_SIZE;

	return huge_page_size(hstate_vma(vma));
}

static inline bool is_power_of_4(unsigned long x)
{
	if (is_power_of_2(x))
		return (__ilog2(x) % 2) ? false : true;
	return false;
}

static int __init add_huge_page_size(unsigned long long size)
{
	int shift = __ffs(size);
	int mmu_psize;

	/* Check that it is a page size supported by the hardware and
	 * that it fits within pagetable and slice limits. */
	if (size <= PAGE_SIZE)
		return -EINVAL;
#if defined(CONFIG_PPC_FSL_BOOK3E)
	if (!is_power_of_4(size))
		return -EINVAL;
#elif !defined(CONFIG_PPC_8xx)
	if (!is_power_of_2(size) || (shift > SLICE_HIGH_SHIFT))
		return -EINVAL;
#endif

	if ((mmu_psize = shift_to_mmu_psize(shift)) < 0)
		return -EINVAL;

#ifdef CONFIG_PPC_BOOK3S_64
	/*
	 * We need to make sure that for different page sizes reported by
	 * firmware we only add hugetlb support for page sizes that can be
	 * supported by linux page table layout.
	 * For now we have
	 * Radix: 2M
	 * Hash: 16M and 16G
	 */
	if (radix_enabled()) {
		if (mmu_psize != MMU_PAGE_2M) {
			if (cpu_has_feature(CPU_FTR_POWER9_DD1) ||
			    (mmu_psize != MMU_PAGE_1G))
				return -EINVAL;
		}
	} else {
		if (mmu_psize != MMU_PAGE_16M && mmu_psize != MMU_PAGE_16G)
			return -EINVAL;
	}
#endif

	BUG_ON(mmu_psize_defs[mmu_psize].shift != shift);

	/* Return if huge page size has already been setup */
	if (size_to_hstate(size))
		return 0;

	hugetlb_add_hstate(shift - PAGE_SHIFT);

	return 0;
}

static int __init hugepage_setup_sz(char *str)
{
	unsigned long long size;

	size = memparse(str, &str);

	if (add_huge_page_size(size) != 0) {
		hugetlb_bad_size();
		pr_err("Invalid huge page size specified(%llu)\n", size);
	}

	return 1;
}
__setup("hugepagesz=", hugepage_setup_sz);

struct kmem_cache *hugepte_cache;
static int __init hugetlbpage_init(void)
{
	int psize;

#if !defined(CONFIG_PPC_FSL_BOOK3E) && !defined(CONFIG_PPC_8xx)
	if (!radix_enabled() && !mmu_has_feature(MMU_FTR_16M_PAGE))
		return -ENODEV;
#endif
	for (psize = 0; psize < MMU_PAGE_COUNT; ++psize) {
		unsigned shift;
		unsigned pdshift;

		if (!mmu_psize_defs[psize].shift)
			continue;

		shift = mmu_psize_to_shift(psize);

		if (add_huge_page_size(1ULL << shift) < 0)
			continue;

		if (shift < HUGEPD_PUD_SHIFT)
			pdshift = PMD_SHIFT;
		else if (shift < HUGEPD_PGD_SHIFT)
			pdshift = PUD_SHIFT;
		else
			pdshift = PGDIR_SHIFT;
		/*
		 * if we have pdshift and shift value same, we don't
		 * use pgt cache for hugepd.
		 */
		if (pdshift > shift)
			pgtable_cache_add(pdshift - shift, NULL);
#if defined(CONFIG_PPC_FSL_BOOK3E) || defined(CONFIG_PPC_8xx)
		else if (!hugepte_cache) {
			/*
			 * Create a kmem cache for hugeptes.  The bottom bits in
			 * the pte have size information encoded in them, so
			 * align them to allow this
			 */
			hugepte_cache = kmem_cache_create("hugepte-cache",
							  sizeof(pte_t),
							  HUGEPD_SHIFT_MASK + 1,
							  0, NULL);
			if (hugepte_cache == NULL)
				panic("%s: Unable to create kmem cache "
				      "for hugeptes\n", __func__);

		}
#endif
	}

#if defined(CONFIG_PPC_FSL_BOOK3E) || defined(CONFIG_PPC_8xx)
	/* Default hpage size = 4M on FSL_BOOK3E and 512k on 8xx */
	if (mmu_psize_defs[MMU_PAGE_4M].shift)
		HPAGE_SHIFT = mmu_psize_defs[MMU_PAGE_4M].shift;
	else if (mmu_psize_defs[MMU_PAGE_512K].shift)
		HPAGE_SHIFT = mmu_psize_defs[MMU_PAGE_512K].shift;
#else
	/* Set default large page size. Currently, we pick 16M or 1M
	 * depending on what is available
	 */
	if (mmu_psize_defs[MMU_PAGE_16M].shift)
		HPAGE_SHIFT = mmu_psize_defs[MMU_PAGE_16M].shift;
	else if (mmu_psize_defs[MMU_PAGE_1M].shift)
		HPAGE_SHIFT = mmu_psize_defs[MMU_PAGE_1M].shift;
	else if (mmu_psize_defs[MMU_PAGE_2M].shift)
		HPAGE_SHIFT = mmu_psize_defs[MMU_PAGE_2M].shift;
#endif
	return 0;
}

arch_initcall(hugetlbpage_init);

void flush_dcache_icache_hugepage(struct page *page)
{
	int i;
	void *start;

	BUG_ON(!PageCompound(page));

	for (i = 0; i < (1UL << compound_order(page)); i++) {
		if (!PageHighMem(page)) {
			__flush_dcache_icache(page_address(page+i));
		} else {
			start = kmap_atomic(page+i);
			__flush_dcache_icache(start);
			kunmap_atomic(start);
		}
	}
}

#endif /* CONFIG_HUGETLB_PAGE */

/*
 * We have 4 cases for pgds and pmds:
 * (1) invalid (all zeroes)
 * (2) pointer to next table, as normal; bottom 6 bits == 0
 * (3) leaf pte for huge page _PAGE_PTE set
 * (4) hugepd pointer, _PAGE_PTE = 0 and bits [2..6] indicate size of table
 *
 * So long as we atomically load page table pointers we are safe against teardown,
 * we can follow the address down to the the page and take a ref on it.
 * This function need to be called with interrupts disabled. We use this variant
 * when we have MSR[EE] = 0 but the paca->soft_enabled = 1
 */
pte_t *__find_linux_pte(pgd_t *pgdir, unsigned long ea,
			bool *is_thp, unsigned *hpage_shift)
{
	pgd_t pgd, *pgdp;
	pud_t pud, *pudp;
	pmd_t pmd, *pmdp;
	pte_t *ret_pte;
	hugepd_t *hpdp = NULL;
	unsigned pdshift = PGDIR_SHIFT;

	if (hpage_shift)
		*hpage_shift = 0;

	if (is_thp)
		*is_thp = false;

	pgdp = pgdir + pgd_index(ea);
	pgd  = READ_ONCE(*pgdp);
	/*
	 * Always operate on the local stack value. This make sure the
	 * value don't get updated by a parallel THP split/collapse,
	 * page fault or a page unmap. The return pte_t * is still not
	 * stable. So should be checked there for above conditions.
	 */
	if (pgd_none(pgd))
		return NULL;
	else if (pgd_huge(pgd)) {
		ret_pte = (pte_t *) pgdp;
		goto out;
	} else if (is_hugepd(__hugepd(pgd_val(pgd))))
		hpdp = (hugepd_t *)&pgd;
	else {
		/*
		 * Even if we end up with an unmap, the pgtable will not
		 * be freed, because we do an rcu free and here we are
		 * irq disabled
		 */
		pdshift = PUD_SHIFT;
		pudp = pud_offset(&pgd, ea);
		pud  = READ_ONCE(*pudp);

		if (pud_none(pud))
			return NULL;
		else if (pud_huge(pud)) {
			ret_pte = (pte_t *) pudp;
			goto out;
		} else if (is_hugepd(__hugepd(pud_val(pud))))
			hpdp = (hugepd_t *)&pud;
		else {
			pdshift = PMD_SHIFT;
			pmdp = pmd_offset(&pud, ea);
			pmd  = READ_ONCE(*pmdp);
			/*
			 * A hugepage collapse is captured by pmd_none, because
			 * it mark the pmd none and do a hpte invalidate.
			 */
			if (pmd_none(pmd))
				return NULL;

			if (pmd_trans_huge(pmd) || pmd_devmap(pmd)) {
				if (is_thp)
					*is_thp = true;
				ret_pte = (pte_t *) pmdp;
				goto out;
			}

			if (pmd_huge(pmd)) {
				ret_pte = (pte_t *) pmdp;
				goto out;
			} else if (is_hugepd(__hugepd(pmd_val(pmd))))
				hpdp = (hugepd_t *)&pmd;
			else
				return pte_offset_kernel(&pmd, ea);
		}
	}
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

int gup_hugepte(pte_t *ptep, unsigned long sz, unsigned long addr,
		unsigned long end, int write, struct page **pages, int *nr)
{
	unsigned long pte_end;
	struct page *head, *page;
	pte_t pte;
	int refs;

	pte_end = (addr + sz) & ~(sz-1);
	if (pte_end < end)
		end = pte_end;

	pte = READ_ONCE(*ptep);

	if (!pte_present(pte) || !pte_read(pte))
		return 0;
	if (write && !pte_write(pte))
		return 0;

	/* hugepages are never "special" */
	VM_BUG_ON(!pfn_valid(pte_pfn(pte)));

	refs = 0;
	head = pte_page(pte);

	page = head + ((addr & (sz-1)) >> PAGE_SHIFT);
	do {
		VM_BUG_ON(compound_head(page) != head);
		pages[*nr] = page;
		(*nr)++;
		page++;
		refs++;
	} while (addr += PAGE_SIZE, addr != end);

	if (!page_cache_add_speculative(head, refs)) {
		*nr -= refs;
		return 0;
	}

	if (unlikely(pte_val(pte) != pte_val(*ptep))) {
		/* Could be optimized better */
		*nr -= refs;
		while (refs--)
			put_page(head);
		return 0;
	}

	return 1;
}
