/*
 * PPC64 (POWER4) Huge TLB Page Support for Kernel.
 *
 * Copyright (C) 2003 David Gibson, IBM Corporation.
 *
 * Based on the IA-32 version:
 * Copyright (C) 2002, Rohit Seth <rohit.seth@intel.com>
 */

#include <linux/mm.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/hugetlb.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>

#define PAGE_SHIFT_64K	16
#define PAGE_SHIFT_16M	24
#define PAGE_SHIFT_16G	34

#define MAX_NUMBER_GPAGES	1024

/* Tracks the 16G pages after the device tree is scanned and before the
 * huge_boot_pages list is ready.  */
static unsigned long gpage_freearray[MAX_NUMBER_GPAGES];
static unsigned nr_gpages;

/* Flag to mark huge PD pointers.  This means pmd_bad() and pud_bad()
 * will choke on pointers to hugepte tables, which is handy for
 * catching screwups early. */

static inline int shift_to_mmu_psize(unsigned int shift)
{
	int psize;

	for (psize = 0; psize < MMU_PAGE_COUNT; ++psize)
		if (mmu_psize_defs[psize].shift == shift)
			return psize;
	return -1;
}

static inline unsigned int mmu_psize_to_shift(unsigned int mmu_psize)
{
	if (mmu_psize_defs[mmu_psize].shift)
		return mmu_psize_defs[mmu_psize].shift;
	BUG();
}

#define hugepd_none(hpd)	((hpd).pd == 0)

static inline pte_t *hugepd_page(hugepd_t hpd)
{
	BUG_ON(!hugepd_ok(hpd));
	return (pte_t *)((hpd.pd & ~HUGEPD_SHIFT_MASK) | 0xc000000000000000);
}

static inline unsigned int hugepd_shift(hugepd_t hpd)
{
	return hpd.pd & HUGEPD_SHIFT_MASK;
}

static inline pte_t *hugepte_offset(hugepd_t *hpdp, unsigned long addr, unsigned pdshift)
{
	unsigned long idx = (addr & ((1UL << pdshift) - 1)) >> hugepd_shift(*hpdp);
	pte_t *dir = hugepd_page(*hpdp);

	return dir + idx;
}

pte_t *find_linux_pte_or_hugepte(pgd_t *pgdir, unsigned long ea, unsigned *shift)
{
	pgd_t *pg;
	pud_t *pu;
	pmd_t *pm;
	hugepd_t *hpdp = NULL;
	unsigned pdshift = PGDIR_SHIFT;

	if (shift)
		*shift = 0;

	pg = pgdir + pgd_index(ea);
	if (is_hugepd(pg)) {
		hpdp = (hugepd_t *)pg;
	} else if (!pgd_none(*pg)) {
		pdshift = PUD_SHIFT;
		pu = pud_offset(pg, ea);
		if (is_hugepd(pu))
			hpdp = (hugepd_t *)pu;
		else if (!pud_none(*pu)) {
			pdshift = PMD_SHIFT;
			pm = pmd_offset(pu, ea);
			if (is_hugepd(pm))
				hpdp = (hugepd_t *)pm;
			else if (!pmd_none(*pm)) {
				return pte_offset_map(pm, ea);
			}
		}
	}

	if (!hpdp)
		return NULL;

	if (shift)
		*shift = hugepd_shift(*hpdp);
	return hugepte_offset(hpdp, ea, pdshift);
}

pte_t *huge_pte_offset(struct mm_struct *mm, unsigned long addr)
{
	return find_linux_pte_or_hugepte(mm->pgd, addr, NULL);
}

static int __hugepte_alloc(struct mm_struct *mm, hugepd_t *hpdp,
			   unsigned long address, unsigned pdshift, unsigned pshift)
{
	pte_t *new = kmem_cache_zalloc(PGT_CACHE(pdshift - pshift),
				       GFP_KERNEL|__GFP_REPEAT);

	BUG_ON(pshift > HUGEPD_SHIFT_MASK);
	BUG_ON((unsigned long)new & HUGEPD_SHIFT_MASK);

	if (! new)
		return -ENOMEM;

	spin_lock(&mm->page_table_lock);
	if (!hugepd_none(*hpdp))
		kmem_cache_free(PGT_CACHE(pdshift - pshift), new);
	else
		hpdp->pd = ((unsigned long)new & ~0x8000000000000000) | pshift;
	spin_unlock(&mm->page_table_lock);
	return 0;
}

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
	if (pshift >= PUD_SHIFT) {
		hpdp = (hugepd_t *)pg;
	} else {
		pdshift = PUD_SHIFT;
		pu = pud_alloc(mm, pg, addr);
		if (pshift >= PMD_SHIFT) {
			hpdp = (hugepd_t *)pu;
		} else {
			pdshift = PMD_SHIFT;
			pm = pmd_alloc(mm, pu, addr);
			hpdp = (hugepd_t *)pm;
		}
	}

	if (!hpdp)
		return NULL;

	BUG_ON(!hugepd_none(*hpdp) && !hugepd_ok(*hpdp));

	if (hugepd_none(*hpdp) && __hugepte_alloc(mm, hpdp, addr, pdshift, pshift))
		return NULL;

	return hugepte_offset(hpdp, addr, pdshift);
}

/* Build list of addresses of gigantic pages.  This function is used in early
 * boot before the buddy or bootmem allocator is setup.
 */
void add_gpage(unsigned long addr, unsigned long page_size,
	unsigned long number_of_pages)
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

/* Moves the gigantic page addresses from the temporary list to the
 * huge_boot_pages list.
 */
int alloc_bootmem_huge_page(struct hstate *hstate)
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

int huge_pmd_unshare(struct mm_struct *mm, unsigned long *addr, pte_t *ptep)
{
	return 0;
}

static void free_hugepd_range(struct mmu_gather *tlb, hugepd_t *hpdp, int pdshift,
			      unsigned long start, unsigned long end,
			      unsigned long floor, unsigned long ceiling)
{
	pte_t *hugepte = hugepd_page(*hpdp);
	unsigned shift = hugepd_shift(*hpdp);
	unsigned long pdmask = ~((1UL << pdshift) - 1);

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

	hpdp->pd = 0;
	tlb->need_flush = 1;
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
	pmd = pmd_offset(pud, addr);
	do {
		next = pmd_addr_end(addr, end);
		if (pmd_none(*pmd))
			continue;
		free_hugepd_range(tlb, (hugepd_t *)pmd, PMD_SHIFT,
				  addr, next, floor, ceiling);
	} while (pmd++, addr = next, addr != end);

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
}

static void hugetlb_free_pud_range(struct mmu_gather *tlb, pgd_t *pgd,
				   unsigned long addr, unsigned long end,
				   unsigned long floor, unsigned long ceiling)
{
	pud_t *pud;
	unsigned long next;
	unsigned long start;

	start = addr;
	pud = pud_offset(pgd, addr);
	do {
		next = pud_addr_end(addr, end);
		if (!is_hugepd(pud)) {
			if (pud_none_or_clear_bad(pud))
				continue;
			hugetlb_free_pmd_range(tlb, pud, addr, next, floor,
					       ceiling);
		} else {
			free_hugepd_range(tlb, (hugepd_t *)pud, PUD_SHIFT,
					  addr, next, floor, ceiling);
		}
	} while (pud++, addr = next, addr != end);

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
}

/*
 * This function frees user-level page tables of a process.
 *
 * Must be called with pagetable lock held.
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

	pgd = pgd_offset(tlb->mm, addr);
	do {
		next = pgd_addr_end(addr, end);
		if (!is_hugepd(pgd)) {
			if (pgd_none_or_clear_bad(pgd))
				continue;
			hugetlb_free_pud_range(tlb, pgd, addr, next, floor, ceiling);
		} else {
			free_hugepd_range(tlb, (hugepd_t *)pgd, PGDIR_SHIFT,
					  addr, next, floor, ceiling);
		}
	} while (pgd++, addr = next, addr != end);
}

struct page *
follow_huge_addr(struct mm_struct *mm, unsigned long address, int write)
{
	pte_t *ptep;
	struct page *page;
	unsigned shift;
	unsigned long mask;

	ptep = find_linux_pte_or_hugepte(mm->pgd, address, &shift);

	/* Verify it is a huge page else bail. */
	if (!ptep || !shift)
		return ERR_PTR(-EINVAL);

	mask = (1UL << shift) - 1;
	page = pte_page(*ptep);
	if (page)
		page += (address & mask) / PAGE_SIZE;

	return page;
}

int pmd_huge(pmd_t pmd)
{
	return 0;
}

int pud_huge(pud_t pud)
{
	return 0;
}

struct page *
follow_huge_pmd(struct mm_struct *mm, unsigned long address,
		pmd_t *pmd, int write)
{
	BUG();
	return NULL;
}

static noinline int gup_hugepte(pte_t *ptep, unsigned long sz, unsigned long addr,
		       unsigned long end, int write, struct page **pages, int *nr)
{
	unsigned long mask;
	unsigned long pte_end;
	struct page *head, *page;
	pte_t pte;
	int refs;

	pte_end = (addr + sz) & ~(sz-1);
	if (pte_end < end)
		end = pte_end;

	pte = *ptep;
	mask = _PAGE_PRESENT | _PAGE_USER;
	if (write)
		mask |= _PAGE_RW;

	if ((pte_val(pte) & mask) != mask)
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
		while (*nr) {
			put_page(page);
			(*nr)--;
		}
	}

	return 1;
}

static unsigned long hugepte_addr_end(unsigned long addr, unsigned long end,
				      unsigned long sz)
{
	unsigned long __boundary = (addr + sz) & ~(sz-1);
	return (__boundary - 1 < end - 1) ? __boundary : end;
}

int gup_hugepd(hugepd_t *hugepd, unsigned pdshift,
	       unsigned long addr, unsigned long end,
	       int write, struct page **pages, int *nr)
{
	pte_t *ptep;
	unsigned long sz = 1UL << hugepd_shift(*hugepd);
	unsigned long next;

	ptep = hugepte_offset(hugepd, addr, pdshift);
	do {
		next = hugepte_addr_end(addr, end, sz);
		if (!gup_hugepte(ptep, sz, addr, end, write, pages, nr))
			return 0;
	} while (ptep++, addr = next, addr != end);

	return 1;
}

unsigned long hugetlb_get_unmapped_area(struct file *file, unsigned long addr,
					unsigned long len, unsigned long pgoff,
					unsigned long flags)
{
	struct hstate *hstate = hstate_file(file);
	int mmu_psize = shift_to_mmu_psize(huge_page_shift(hstate));

	return slice_get_unmapped_area(addr, len, flags, mmu_psize, 1, 0);
}

unsigned long vma_mmu_pagesize(struct vm_area_struct *vma)
{
	unsigned int psize = get_slice_psize(vma->vm_mm, vma->vm_start);

	return 1UL << mmu_psize_to_shift(psize);
}

static int __init add_huge_page_size(unsigned long long size)
{
	int shift = __ffs(size);
	int mmu_psize;

	/* Check that it is a page size supported by the hardware and
	 * that it fits within pagetable and slice limits. */
	if (!is_power_of_2(size)
	    || (shift > SLICE_HIGH_SHIFT) || (shift <= PAGE_SHIFT))
		return -EINVAL;

	if ((mmu_psize = shift_to_mmu_psize(shift)) < 0)
		return -EINVAL;

#ifdef CONFIG_SPU_FS_64K_LS
	/* Disable support for 64K huge pages when 64K SPU local store
	 * support is enabled as the current implementation conflicts.
	 */
	if (shift == PAGE_SHIFT_64K)
		return -EINVAL;
#endif /* CONFIG_SPU_FS_64K_LS */

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

	if (add_huge_page_size(size) != 0)
		printk(KERN_WARNING "Invalid huge page size specified(%llu)\n", size);

	return 1;
}
__setup("hugepagesz=", hugepage_setup_sz);

static int __init hugetlbpage_init(void)
{
	int psize;

	if (!cpu_has_feature(CPU_FTR_16M_PAGE))
		return -ENODEV;

	for (psize = 0; psize < MMU_PAGE_COUNT; ++psize) {
		unsigned shift;
		unsigned pdshift;

		if (!mmu_psize_defs[psize].shift)
			continue;

		shift = mmu_psize_to_shift(psize);

		if (add_huge_page_size(1ULL << shift) < 0)
			continue;

		if (shift < PMD_SHIFT)
			pdshift = PMD_SHIFT;
		else if (shift < PUD_SHIFT)
			pdshift = PUD_SHIFT;
		else
			pdshift = PGDIR_SHIFT;

		pgtable_cache_add(pdshift - shift, NULL);
		if (!PGT_CACHE(pdshift - shift))
			panic("hugetlbpage_init(): could not create "
			      "pgtable cache for %d bit pagesize\n", shift);
	}

	/* Set default large page size. Currently, we pick 16M or 1M
	 * depending on what is available
	 */
	if (mmu_psize_defs[MMU_PAGE_16M].shift)
		HPAGE_SHIFT = mmu_psize_defs[MMU_PAGE_16M].shift;
	else if (mmu_psize_defs[MMU_PAGE_1M].shift)
		HPAGE_SHIFT = mmu_psize_defs[MMU_PAGE_1M].shift;

	return 0;
}

module_init(hugetlbpage_init);

void flush_dcache_icache_hugepage(struct page *page)
{
	int i;

	BUG_ON(!PageCompound(page));

	for (i = 0; i < (1UL << compound_order(page)); i++)
		__flush_dcache_icache(page_address(page+i));
}
