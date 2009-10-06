/*
 * PPC64 (POWER4) Huge TLB Page Support for Kernel.
 *
 * Copyright (C) 2003 David Gibson, IBM Corporation.
 *
 * Based on the IA-32 version:
 * Copyright (C) 2002, Rohit Seth <rohit.seth@intel.com>
 */

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/sysctl.h>
#include <asm/mman.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/mmu_context.h>
#include <asm/machdep.h>
#include <asm/cputable.h>
#include <asm/spu.h>

#define PAGE_SHIFT_64K	16
#define PAGE_SHIFT_16M	24
#define PAGE_SHIFT_16G	34

#define NUM_LOW_AREAS	(0x100000000UL >> SID_SHIFT)
#define NUM_HIGH_AREAS	(PGTABLE_RANGE >> HTLB_AREA_SHIFT)
#define MAX_NUMBER_GPAGES	1024

/* Tracks the 16G pages after the device tree is scanned and before the
 * huge_boot_pages list is ready.  */
static unsigned long gpage_freearray[MAX_NUMBER_GPAGES];
static unsigned nr_gpages;

/* Array of valid huge page sizes - non-zero value(hugepte_shift) is
 * stored for the huge page sizes that are valid.
 */
unsigned int mmu_huge_psizes[MMU_PAGE_COUNT] = { }; /* initialize all to 0 */

#define hugepte_shift			mmu_huge_psizes
#define PTRS_PER_HUGEPTE(psize)		(1 << hugepte_shift[psize])
#define HUGEPTE_TABLE_SIZE(psize)	(sizeof(pte_t) << hugepte_shift[psize])

#define HUGEPD_SHIFT(psize)		(mmu_psize_to_shift(psize) \
						+ hugepte_shift[psize])
#define HUGEPD_SIZE(psize)		(1UL << HUGEPD_SHIFT(psize))
#define HUGEPD_MASK(psize)		(~(HUGEPD_SIZE(psize)-1))

/* Subtract one from array size because we don't need a cache for 4K since
 * is not a huge page size */
#define HUGE_PGTABLE_INDEX(psize)	(HUGEPTE_CACHE_NUM + psize - 1)
#define HUGEPTE_CACHE_NAME(psize)	(huge_pgtable_cache_name[psize])

static const char *huge_pgtable_cache_name[MMU_PAGE_COUNT] = {
	[MMU_PAGE_64K]	= "hugepte_cache_64K",
	[MMU_PAGE_1M]	= "hugepte_cache_1M",
	[MMU_PAGE_16M]	= "hugepte_cache_16M",
	[MMU_PAGE_16G]	= "hugepte_cache_16G",
};

/* Flag to mark huge PD pointers.  This means pmd_bad() and pud_bad()
 * will choke on pointers to hugepte tables, which is handy for
 * catching screwups early. */
#define HUGEPD_OK	0x1

typedef struct { unsigned long pd; } hugepd_t;

#define hugepd_none(hpd)	((hpd).pd == 0)

static inline int shift_to_mmu_psize(unsigned int shift)
{
	switch (shift) {
#ifndef CONFIG_PPC_64K_PAGES
	case PAGE_SHIFT_64K:
	    return MMU_PAGE_64K;
#endif
	case PAGE_SHIFT_16M:
	    return MMU_PAGE_16M;
	case PAGE_SHIFT_16G:
	    return MMU_PAGE_16G;
	}
	return -1;
}

static inline unsigned int mmu_psize_to_shift(unsigned int mmu_psize)
{
	if (mmu_psize_defs[mmu_psize].shift)
		return mmu_psize_defs[mmu_psize].shift;
	BUG();
}

static inline pte_t *hugepd_page(hugepd_t hpd)
{
	BUG_ON(!(hpd.pd & HUGEPD_OK));
	return (pte_t *)(hpd.pd & ~HUGEPD_OK);
}

static inline pte_t *hugepte_offset(hugepd_t *hpdp, unsigned long addr,
				    struct hstate *hstate)
{
	unsigned int shift = huge_page_shift(hstate);
	int psize = shift_to_mmu_psize(shift);
	unsigned long idx = ((addr >> shift) & (PTRS_PER_HUGEPTE(psize)-1));
	pte_t *dir = hugepd_page(*hpdp);

	return dir + idx;
}

static int __hugepte_alloc(struct mm_struct *mm, hugepd_t *hpdp,
			   unsigned long address, unsigned int psize)
{
	pte_t *new = kmem_cache_zalloc(pgtable_cache[HUGE_PGTABLE_INDEX(psize)],
				      GFP_KERNEL|__GFP_REPEAT);

	if (! new)
		return -ENOMEM;

	spin_lock(&mm->page_table_lock);
	if (!hugepd_none(*hpdp))
		kmem_cache_free(pgtable_cache[HUGE_PGTABLE_INDEX(psize)], new);
	else
		hpdp->pd = (unsigned long)new | HUGEPD_OK;
	spin_unlock(&mm->page_table_lock);
	return 0;
}


static pud_t *hpud_offset(pgd_t *pgd, unsigned long addr, struct hstate *hstate)
{
	if (huge_page_shift(hstate) < PUD_SHIFT)
		return pud_offset(pgd, addr);
	else
		return (pud_t *) pgd;
}
static pud_t *hpud_alloc(struct mm_struct *mm, pgd_t *pgd, unsigned long addr,
			 struct hstate *hstate)
{
	if (huge_page_shift(hstate) < PUD_SHIFT)
		return pud_alloc(mm, pgd, addr);
	else
		return (pud_t *) pgd;
}
static pmd_t *hpmd_offset(pud_t *pud, unsigned long addr, struct hstate *hstate)
{
	if (huge_page_shift(hstate) < PMD_SHIFT)
		return pmd_offset(pud, addr);
	else
		return (pmd_t *) pud;
}
static pmd_t *hpmd_alloc(struct mm_struct *mm, pud_t *pud, unsigned long addr,
			 struct hstate *hstate)
{
	if (huge_page_shift(hstate) < PMD_SHIFT)
		return pmd_alloc(mm, pud, addr);
	else
		return (pmd_t *) pud;
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


/* Modelled after find_linux_pte() */
pte_t *huge_pte_offset(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pg;
	pud_t *pu;
	pmd_t *pm;

	unsigned int psize;
	unsigned int shift;
	unsigned long sz;
	struct hstate *hstate;
	psize = get_slice_psize(mm, addr);
	shift = mmu_psize_to_shift(psize);
	sz = ((1UL) << shift);
	hstate = size_to_hstate(sz);

	addr &= hstate->mask;

	pg = pgd_offset(mm, addr);
	if (!pgd_none(*pg)) {
		pu = hpud_offset(pg, addr, hstate);
		if (!pud_none(*pu)) {
			pm = hpmd_offset(pu, addr, hstate);
			if (!pmd_none(*pm))
				return hugepte_offset((hugepd_t *)pm, addr,
						      hstate);
		}
	}

	return NULL;
}

pte_t *huge_pte_alloc(struct mm_struct *mm,
			unsigned long addr, unsigned long sz)
{
	pgd_t *pg;
	pud_t *pu;
	pmd_t *pm;
	hugepd_t *hpdp = NULL;
	struct hstate *hstate;
	unsigned int psize;
	hstate = size_to_hstate(sz);

	psize = get_slice_psize(mm, addr);
	BUG_ON(!mmu_huge_psizes[psize]);

	addr &= hstate->mask;

	pg = pgd_offset(mm, addr);
	pu = hpud_alloc(mm, pg, addr, hstate);

	if (pu) {
		pm = hpmd_alloc(mm, pu, addr, hstate);
		if (pm)
			hpdp = (hugepd_t *)pm;
	}

	if (! hpdp)
		return NULL;

	if (hugepd_none(*hpdp) && __hugepte_alloc(mm, hpdp, addr, psize))
		return NULL;

	return hugepte_offset(hpdp, addr, hstate);
}

int huge_pmd_unshare(struct mm_struct *mm, unsigned long *addr, pte_t *ptep)
{
	return 0;
}

static void free_hugepte_range(struct mmu_gather *tlb, hugepd_t *hpdp,
			       unsigned int psize)
{
	pte_t *hugepte = hugepd_page(*hpdp);

	hpdp->pd = 0;
	tlb->need_flush = 1;
	pgtable_free_tlb(tlb, pgtable_free_cache(hugepte,
						 HUGEPTE_CACHE_NUM+psize-1,
						 PGF_CACHENUM_MASK));
}

static void hugetlb_free_pmd_range(struct mmu_gather *tlb, pud_t *pud,
				   unsigned long addr, unsigned long end,
				   unsigned long floor, unsigned long ceiling,
				   unsigned int psize)
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
		free_hugepte_range(tlb, (hugepd_t *)pmd, psize);
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
	unsigned int shift;
	unsigned int psize = get_slice_psize(tlb->mm, addr);
	shift = mmu_psize_to_shift(psize);

	start = addr;
	pud = pud_offset(pgd, addr);
	do {
		next = pud_addr_end(addr, end);
		if (shift < PMD_SHIFT) {
			if (pud_none_or_clear_bad(pud))
				continue;
			hugetlb_free_pmd_range(tlb, pud, addr, next, floor,
					       ceiling, psize);
		} else {
			if (pud_none(*pud))
				continue;
			free_hugepte_range(tlb, (hugepd_t *)pud, psize);
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
	unsigned long start;

	/*
	 * Comments below take from the normal free_pgd_range().  They
	 * apply here too.  The tests against HUGEPD_MASK below are
	 * essential, because we *don't* test for this at the bottom
	 * level.  Without them we'll attempt to free a hugepte table
	 * when we unmap just part of it, even if there are other
	 * active mappings using it.
	 *
	 * The next few lines have given us lots of grief...
	 *
	 * Why are we testing HUGEPD* at this top level?  Because
	 * often there will be no work to do at all, and we'd prefer
	 * not to go all the way down to the bottom just to discover
	 * that.
	 *
	 * Why all these "- 1"s?  Because 0 represents both the bottom
	 * of the address space and the top of it (using -1 for the
	 * top wouldn't help much: the masks would do the wrong thing).
	 * The rule is that addr 0 and floor 0 refer to the bottom of
	 * the address space, but end 0 and ceiling 0 refer to the top
	 * Comparisons need to use "end - 1" and "ceiling - 1" (though
	 * that end 0 case should be mythical).
	 *
	 * Wherever addr is brought up or ceiling brought down, we
	 * must be careful to reject "the opposite 0" before it
	 * confuses the subsequent tests.  But what about where end is
	 * brought down by HUGEPD_SIZE below? no, end can't go down to
	 * 0 there.
	 *
	 * Whereas we round start (addr) and ceiling down, by different
	 * masks at different levels, in order to test whether a table
	 * now has no other vmas using it, so can be freed, we don't
	 * bother to round floor or end up - the tests don't need that.
	 */
	unsigned int psize = get_slice_psize(tlb->mm, addr);

	addr &= HUGEPD_MASK(psize);
	if (addr < floor) {
		addr += HUGEPD_SIZE(psize);
		if (!addr)
			return;
	}
	if (ceiling) {
		ceiling &= HUGEPD_MASK(psize);
		if (!ceiling)
			return;
	}
	if (end - 1 > ceiling - 1)
		end -= HUGEPD_SIZE(psize);
	if (addr > end - 1)
		return;

	start = addr;
	pgd = pgd_offset(tlb->mm, addr);
	do {
		psize = get_slice_psize(tlb->mm, addr);
		BUG_ON(!mmu_huge_psizes[psize]);
		next = pgd_addr_end(addr, end);
		if (mmu_psize_to_shift(psize) < PUD_SHIFT) {
			if (pgd_none_or_clear_bad(pgd))
				continue;
			hugetlb_free_pud_range(tlb, pgd, addr, next, floor, ceiling);
		} else {
			if (pgd_none(*pgd))
				continue;
			free_hugepte_range(tlb, (hugepd_t *)pgd, psize);
		}
	} while (pgd++, addr = next, addr != end);
}

void set_huge_pte_at(struct mm_struct *mm, unsigned long addr,
		     pte_t *ptep, pte_t pte)
{
	if (pte_present(*ptep)) {
		/* We open-code pte_clear because we need to pass the right
		 * argument to hpte_need_flush (huge / !huge). Might not be
		 * necessary anymore if we make hpte_need_flush() get the
		 * page size from the slices
		 */
		unsigned int psize = get_slice_psize(mm, addr);
		unsigned int shift = mmu_psize_to_shift(psize);
		unsigned long sz = ((1UL) << shift);
		struct hstate *hstate = size_to_hstate(sz);
		pte_update(mm, addr & hstate->mask, ptep, ~0UL, 1);
	}
	*ptep = __pte(pte_val(pte) & ~_PAGE_HPTEFLAGS);
}

pte_t huge_ptep_get_and_clear(struct mm_struct *mm, unsigned long addr,
			      pte_t *ptep)
{
	unsigned long old = pte_update(mm, addr, ptep, ~0UL, 1);
	return __pte(old);
}

struct page *
follow_huge_addr(struct mm_struct *mm, unsigned long address, int write)
{
	pte_t *ptep;
	struct page *page;
	unsigned int mmu_psize = get_slice_psize(mm, address);

	/* Verify it is a huge page else bail. */
	if (!mmu_huge_psizes[mmu_psize])
		return ERR_PTR(-EINVAL);

	ptep = huge_pte_offset(mm, address);
	page = pte_page(*ptep);
	if (page) {
		unsigned int shift = mmu_psize_to_shift(mmu_psize);
		unsigned long sz = ((1UL) << shift);
		page += (address % sz) / PAGE_SIZE;
	}

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


unsigned long hugetlb_get_unmapped_area(struct file *file, unsigned long addr,
					unsigned long len, unsigned long pgoff,
					unsigned long flags)
{
	struct hstate *hstate = hstate_file(file);
	int mmu_psize = shift_to_mmu_psize(huge_page_shift(hstate));

	if (!mmu_huge_psizes[mmu_psize])
		return -EINVAL;
	return slice_get_unmapped_area(addr, len, flags, mmu_psize, 1, 0);
}

unsigned long vma_mmu_pagesize(struct vm_area_struct *vma)
{
	unsigned int psize = get_slice_psize(vma->vm_mm, vma->vm_start);

	return 1UL << mmu_psize_to_shift(psize);
}

/*
 * Called by asm hashtable.S for doing lazy icache flush
 */
static unsigned int hash_huge_page_do_lazy_icache(unsigned long rflags,
					pte_t pte, int trap, unsigned long sz)
{
	struct page *page;
	int i;

	if (!pfn_valid(pte_pfn(pte)))
		return rflags;

	page = pte_page(pte);

	/* page is dirty */
	if (!test_bit(PG_arch_1, &page->flags) && !PageReserved(page)) {
		if (trap == 0x400) {
			for (i = 0; i < (sz / PAGE_SIZE); i++)
				__flush_dcache_icache(page_address(page+i));
			set_bit(PG_arch_1, &page->flags);
		} else {
			rflags |= HPTE_R_N;
		}
	}
	return rflags;
}

int hash_huge_page(struct mm_struct *mm, unsigned long access,
		   unsigned long ea, unsigned long vsid, int local,
		   unsigned long trap)
{
	pte_t *ptep;
	unsigned long old_pte, new_pte;
	unsigned long va, rflags, pa, sz;
	long slot;
	int err = 1;
	int ssize = user_segment_size(ea);
	unsigned int mmu_psize;
	int shift;
	mmu_psize = get_slice_psize(mm, ea);

	if (!mmu_huge_psizes[mmu_psize])
		goto out;
	ptep = huge_pte_offset(mm, ea);

	/* Search the Linux page table for a match with va */
	va = hpt_va(ea, vsid, ssize);

	/*
	 * If no pte found or not present, send the problem up to
	 * do_page_fault
	 */
	if (unlikely(!ptep || pte_none(*ptep)))
		goto out;

	/* 
	 * Check the user's access rights to the page.  If access should be
	 * prevented then send the problem up to do_page_fault.
	 */
	if (unlikely(access & ~pte_val(*ptep)))
		goto out;
	/*
	 * At this point, we have a pte (old_pte) which can be used to build
	 * or update an HPTE. There are 2 cases:
	 *
	 * 1. There is a valid (present) pte with no associated HPTE (this is 
	 *	the most common case)
	 * 2. There is a valid (present) pte with an associated HPTE. The
	 *	current values of the pp bits in the HPTE prevent access
	 *	because we are doing software DIRTY bit management and the
	 *	page is currently not DIRTY. 
	 */


	do {
		old_pte = pte_val(*ptep);
		if (old_pte & _PAGE_BUSY)
			goto out;
		new_pte = old_pte | _PAGE_BUSY | _PAGE_ACCESSED;
	} while(old_pte != __cmpxchg_u64((unsigned long *)ptep,
					 old_pte, new_pte));

	rflags = 0x2 | (!(new_pte & _PAGE_RW));
 	/* _PAGE_EXEC -> HW_NO_EXEC since it's inverted */
	rflags |= ((new_pte & _PAGE_EXEC) ? 0 : HPTE_R_N);
	shift = mmu_psize_to_shift(mmu_psize);
	sz = ((1UL) << shift);
	if (!cpu_has_feature(CPU_FTR_COHERENT_ICACHE))
		/* No CPU has hugepages but lacks no execute, so we
		 * don't need to worry about that case */
		rflags = hash_huge_page_do_lazy_icache(rflags, __pte(old_pte),
						       trap, sz);

	/* Check if pte already has an hpte (case 2) */
	if (unlikely(old_pte & _PAGE_HASHPTE)) {
		/* There MIGHT be an HPTE for this pte */
		unsigned long hash, slot;

		hash = hpt_hash(va, shift, ssize);
		if (old_pte & _PAGE_F_SECOND)
			hash = ~hash;
		slot = (hash & htab_hash_mask) * HPTES_PER_GROUP;
		slot += (old_pte & _PAGE_F_GIX) >> 12;

		if (ppc_md.hpte_updatepp(slot, rflags, va, mmu_psize,
					 ssize, local) == -1)
			old_pte &= ~_PAGE_HPTEFLAGS;
	}

	if (likely(!(old_pte & _PAGE_HASHPTE))) {
		unsigned long hash = hpt_hash(va, shift, ssize);
		unsigned long hpte_group;

		pa = pte_pfn(__pte(old_pte)) << PAGE_SHIFT;

repeat:
		hpte_group = ((hash & htab_hash_mask) *
			      HPTES_PER_GROUP) & ~0x7UL;

		/* clear HPTE slot informations in new PTE */
#ifdef CONFIG_PPC_64K_PAGES
		new_pte = (new_pte & ~_PAGE_HPTEFLAGS) | _PAGE_HPTE_SUB0;
#else
		new_pte = (new_pte & ~_PAGE_HPTEFLAGS) | _PAGE_HASHPTE;
#endif
		/* Add in WIMG bits */
		rflags |= (new_pte & (_PAGE_WRITETHRU | _PAGE_NO_CACHE |
				      _PAGE_COHERENT | _PAGE_GUARDED));

		/* Insert into the hash table, primary slot */
		slot = ppc_md.hpte_insert(hpte_group, va, pa, rflags, 0,
					  mmu_psize, ssize);

		/* Primary is full, try the secondary */
		if (unlikely(slot == -1)) {
			hpte_group = ((~hash & htab_hash_mask) *
				      HPTES_PER_GROUP) & ~0x7UL; 
			slot = ppc_md.hpte_insert(hpte_group, va, pa, rflags,
						  HPTE_V_SECONDARY,
						  mmu_psize, ssize);
			if (slot == -1) {
				if (mftb() & 0x1)
					hpte_group = ((hash & htab_hash_mask) *
						      HPTES_PER_GROUP)&~0x7UL;

				ppc_md.hpte_remove(hpte_group);
				goto repeat;
                        }
		}

		if (unlikely(slot == -2))
			panic("hash_huge_page: pte_insert failed\n");

		new_pte |= (slot << 12) & (_PAGE_F_SECOND | _PAGE_F_GIX);
	}

	/*
	 * No need to use ldarx/stdcx here
	 */
	*ptep = __pte(new_pte & ~_PAGE_BUSY);

	err = 0;

 out:
	return err;
}

static void __init set_huge_psize(int psize)
{
	/* Check that it is a page size supported by the hardware and
	 * that it fits within pagetable limits. */
	if (mmu_psize_defs[psize].shift &&
		mmu_psize_defs[psize].shift < SID_SHIFT_1T &&
		(mmu_psize_defs[psize].shift > MIN_HUGEPTE_SHIFT ||
		 mmu_psize_defs[psize].shift == PAGE_SHIFT_64K ||
		 mmu_psize_defs[psize].shift == PAGE_SHIFT_16G)) {
		/* Return if huge page size has already been setup or is the
		 * same as the base page size. */
		if (mmu_huge_psizes[psize] ||
		   mmu_psize_defs[psize].shift == PAGE_SHIFT)
			return;
		if (WARN_ON(HUGEPTE_CACHE_NAME(psize) == NULL))
			return;
		hugetlb_add_hstate(mmu_psize_defs[psize].shift - PAGE_SHIFT);

		switch (mmu_psize_defs[psize].shift) {
		case PAGE_SHIFT_64K:
		    /* We only allow 64k hpages with 4k base page,
		     * which was checked above, and always put them
		     * at the PMD */
		    hugepte_shift[psize] = PMD_SHIFT;
		    break;
		case PAGE_SHIFT_16M:
		    /* 16M pages can be at two different levels
		     * of pagestables based on base page size */
		    if (PAGE_SHIFT == PAGE_SHIFT_64K)
			    hugepte_shift[psize] = PMD_SHIFT;
		    else /* 4k base page */
			    hugepte_shift[psize] = PUD_SHIFT;
		    break;
		case PAGE_SHIFT_16G:
		    /* 16G pages are always at PGD level */
		    hugepte_shift[psize] = PGDIR_SHIFT;
		    break;
		}
		hugepte_shift[psize] -= mmu_psize_defs[psize].shift;
	} else
		hugepte_shift[psize] = 0;
}

static int __init hugepage_setup_sz(char *str)
{
	unsigned long long size;
	int mmu_psize;
	int shift;

	size = memparse(str, &str);

	shift = __ffs(size);
	mmu_psize = shift_to_mmu_psize(shift);
	if (mmu_psize >= 0 && mmu_psize_defs[mmu_psize].shift)
		set_huge_psize(mmu_psize);
	else
		printk(KERN_WARNING "Invalid huge page size specified(%llu)\n", size);

	return 1;
}
__setup("hugepagesz=", hugepage_setup_sz);

static int __init hugetlbpage_init(void)
{
	unsigned int psize;

	if (!cpu_has_feature(CPU_FTR_16M_PAGE))
		return -ENODEV;

	/* Add supported huge page sizes.  Need to change HUGE_MAX_HSTATE
	 * and adjust PTE_NONCACHE_NUM if the number of supported huge page
	 * sizes changes.
	 */
	set_huge_psize(MMU_PAGE_16M);
	set_huge_psize(MMU_PAGE_16G);

	/* Temporarily disable support for 64K huge pages when 64K SPU local
	 * store support is enabled as the current implementation conflicts.
	 */
#ifndef CONFIG_SPU_FS_64K_LS
	set_huge_psize(MMU_PAGE_64K);
#endif

	for (psize = 0; psize < MMU_PAGE_COUNT; ++psize) {
		if (mmu_huge_psizes[psize]) {
			pgtable_cache[HUGE_PGTABLE_INDEX(psize)] =
				kmem_cache_create(
					HUGEPTE_CACHE_NAME(psize),
					HUGEPTE_TABLE_SIZE(psize),
					HUGEPTE_TABLE_SIZE(psize),
					0,
					NULL);
			if (!pgtable_cache[HUGE_PGTABLE_INDEX(psize)])
				panic("hugetlbpage_init(): could not create %s"\
				      "\n", HUGEPTE_CACHE_NAME(psize));
		}
	}

	return 0;
}

module_init(hugetlbpage_init);
