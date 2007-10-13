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

#define NUM_LOW_AREAS	(0x100000000UL >> SID_SHIFT)
#define NUM_HIGH_AREAS	(PGTABLE_RANGE >> HTLB_AREA_SHIFT)

#ifdef CONFIG_PPC_64K_PAGES
#define HUGEPTE_INDEX_SIZE	(PMD_SHIFT-HPAGE_SHIFT)
#else
#define HUGEPTE_INDEX_SIZE	(PUD_SHIFT-HPAGE_SHIFT)
#endif
#define PTRS_PER_HUGEPTE	(1 << HUGEPTE_INDEX_SIZE)
#define HUGEPTE_TABLE_SIZE	(sizeof(pte_t) << HUGEPTE_INDEX_SIZE)

#define HUGEPD_SHIFT		(HPAGE_SHIFT + HUGEPTE_INDEX_SIZE)
#define HUGEPD_SIZE		(1UL << HUGEPD_SHIFT)
#define HUGEPD_MASK		(~(HUGEPD_SIZE-1))

#define huge_pgtable_cache	(pgtable_cache[HUGEPTE_CACHE_NUM])

/* Flag to mark huge PD pointers.  This means pmd_bad() and pud_bad()
 * will choke on pointers to hugepte tables, which is handy for
 * catching screwups early. */
#define HUGEPD_OK	0x1

typedef struct { unsigned long pd; } hugepd_t;

#define hugepd_none(hpd)	((hpd).pd == 0)

static inline pte_t *hugepd_page(hugepd_t hpd)
{
	BUG_ON(!(hpd.pd & HUGEPD_OK));
	return (pte_t *)(hpd.pd & ~HUGEPD_OK);
}

static inline pte_t *hugepte_offset(hugepd_t *hpdp, unsigned long addr)
{
	unsigned long idx = ((addr >> HPAGE_SHIFT) & (PTRS_PER_HUGEPTE-1));
	pte_t *dir = hugepd_page(*hpdp);

	return dir + idx;
}

static int __hugepte_alloc(struct mm_struct *mm, hugepd_t *hpdp,
			   unsigned long address)
{
	pte_t *new = kmem_cache_alloc(huge_pgtable_cache,
				      GFP_KERNEL|__GFP_REPEAT);

	if (! new)
		return -ENOMEM;

	spin_lock(&mm->page_table_lock);
	if (!hugepd_none(*hpdp))
		kmem_cache_free(huge_pgtable_cache, new);
	else
		hpdp->pd = (unsigned long)new | HUGEPD_OK;
	spin_unlock(&mm->page_table_lock);
	return 0;
}

/* Modelled after find_linux_pte() */
pte_t *huge_pte_offset(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pg;
	pud_t *pu;

	BUG_ON(get_slice_psize(mm, addr) != mmu_huge_psize);

	addr &= HPAGE_MASK;

	pg = pgd_offset(mm, addr);
	if (!pgd_none(*pg)) {
		pu = pud_offset(pg, addr);
		if (!pud_none(*pu)) {
#ifdef CONFIG_PPC_64K_PAGES
			pmd_t *pm;
			pm = pmd_offset(pu, addr);
			if (!pmd_none(*pm))
				return hugepte_offset((hugepd_t *)pm, addr);
#else
			return hugepte_offset((hugepd_t *)pu, addr);
#endif
		}
	}

	return NULL;
}

pte_t *huge_pte_alloc(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pg;
	pud_t *pu;
	hugepd_t *hpdp = NULL;

	BUG_ON(get_slice_psize(mm, addr) != mmu_huge_psize);

	addr &= HPAGE_MASK;

	pg = pgd_offset(mm, addr);
	pu = pud_alloc(mm, pg, addr);

	if (pu) {
#ifdef CONFIG_PPC_64K_PAGES
		pmd_t *pm;
		pm = pmd_alloc(mm, pu, addr);
		if (pm)
			hpdp = (hugepd_t *)pm;
#else
		hpdp = (hugepd_t *)pu;
#endif
	}

	if (! hpdp)
		return NULL;

	if (hugepd_none(*hpdp) && __hugepte_alloc(mm, hpdp, addr))
		return NULL;

	return hugepte_offset(hpdp, addr);
}

int huge_pmd_unshare(struct mm_struct *mm, unsigned long *addr, pte_t *ptep)
{
	return 0;
}

static void free_hugepte_range(struct mmu_gather *tlb, hugepd_t *hpdp)
{
	pte_t *hugepte = hugepd_page(*hpdp);

	hpdp->pd = 0;
	tlb->need_flush = 1;
	pgtable_free_tlb(tlb, pgtable_free_cache(hugepte, HUGEPTE_CACHE_NUM,
						 PGF_CACHENUM_MASK));
}

#ifdef CONFIG_PPC_64K_PAGES
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
		free_hugepte_range(tlb, (hugepd_t *)pmd);
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
	pmd_free_tlb(tlb, pmd);
}
#endif

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
#ifdef CONFIG_PPC_64K_PAGES
		if (pud_none_or_clear_bad(pud))
			continue;
		hugetlb_free_pmd_range(tlb, pud, addr, next, floor, ceiling);
#else
		if (pud_none(*pud))
			continue;
		free_hugepte_range(tlb, (hugepd_t *)pud);
#endif
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
	pud_free_tlb(tlb, pud);
}

/*
 * This function frees user-level page tables of a process.
 *
 * Must be called with pagetable lock held.
 */
void hugetlb_free_pgd_range(struct mmu_gather **tlb,
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

	addr &= HUGEPD_MASK;
	if (addr < floor) {
		addr += HUGEPD_SIZE;
		if (!addr)
			return;
	}
	if (ceiling) {
		ceiling &= HUGEPD_MASK;
		if (!ceiling)
			return;
	}
	if (end - 1 > ceiling - 1)
		end -= HUGEPD_SIZE;
	if (addr > end - 1)
		return;

	start = addr;
	pgd = pgd_offset((*tlb)->mm, addr);
	do {
		BUG_ON(get_slice_psize((*tlb)->mm, addr) != mmu_huge_psize);
		next = pgd_addr_end(addr, end);
		if (pgd_none_or_clear_bad(pgd))
			continue;
		hugetlb_free_pud_range(*tlb, pgd, addr, next, floor, ceiling);
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
		pte_update(mm, addr & HPAGE_MASK, ptep, ~0UL, 1);
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

	if (get_slice_psize(mm, address) != mmu_huge_psize)
		return ERR_PTR(-EINVAL);

	ptep = huge_pte_offset(mm, address);
	page = pte_page(*ptep);
	if (page)
		page += (address % HPAGE_SIZE) / PAGE_SIZE;

	return page;
}

int pmd_huge(pmd_t pmd)
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
	return slice_get_unmapped_area(addr, len, flags,
				       mmu_huge_psize, 1, 0);
}

/*
 * Called by asm hashtable.S for doing lazy icache flush
 */
static unsigned int hash_huge_page_do_lazy_icache(unsigned long rflags,
						  pte_t pte, int trap)
{
	struct page *page;
	int i;

	if (!pfn_valid(pte_pfn(pte)))
		return rflags;

	page = pte_page(pte);

	/* page is dirty */
	if (!test_bit(PG_arch_1, &page->flags) && !PageReserved(page)) {
		if (trap == 0x400) {
			for (i = 0; i < (HPAGE_SIZE / PAGE_SIZE); i++)
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
	unsigned long va, rflags, pa;
	long slot;
	int err = 1;
	int ssize = user_segment_size(ea);

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
		new_pte = old_pte | _PAGE_BUSY |
			_PAGE_ACCESSED | _PAGE_HASHPTE;
	} while(old_pte != __cmpxchg_u64((unsigned long *)ptep,
					 old_pte, new_pte));

	rflags = 0x2 | (!(new_pte & _PAGE_RW));
 	/* _PAGE_EXEC -> HW_NO_EXEC since it's inverted */
	rflags |= ((new_pte & _PAGE_EXEC) ? 0 : HPTE_R_N);
	if (!cpu_has_feature(CPU_FTR_COHERENT_ICACHE))
		/* No CPU has hugepages but lacks no execute, so we
		 * don't need to worry about that case */
		rflags = hash_huge_page_do_lazy_icache(rflags, __pte(old_pte),
						       trap);

	/* Check if pte already has an hpte (case 2) */
	if (unlikely(old_pte & _PAGE_HASHPTE)) {
		/* There MIGHT be an HPTE for this pte */
		unsigned long hash, slot;

		hash = hpt_hash(va, HPAGE_SHIFT, ssize);
		if (old_pte & _PAGE_F_SECOND)
			hash = ~hash;
		slot = (hash & htab_hash_mask) * HPTES_PER_GROUP;
		slot += (old_pte & _PAGE_F_GIX) >> 12;

		if (ppc_md.hpte_updatepp(slot, rflags, va, mmu_huge_psize,
					 ssize, local) == -1)
			old_pte &= ~_PAGE_HPTEFLAGS;
	}

	if (likely(!(old_pte & _PAGE_HASHPTE))) {
		unsigned long hash = hpt_hash(va, HPAGE_SHIFT, ssize);
		unsigned long hpte_group;

		pa = pte_pfn(__pte(old_pte)) << PAGE_SHIFT;

repeat:
		hpte_group = ((hash & htab_hash_mask) *
			      HPTES_PER_GROUP) & ~0x7UL;

		/* clear HPTE slot informations in new PTE */
		new_pte = (new_pte & ~_PAGE_HPTEFLAGS) | _PAGE_HASHPTE;

		/* Add in WIMG bits */
		/* XXX We should store these in the pte */
		/* --BenH: I think they are ... */
		rflags |= _PAGE_COHERENT;

		/* Insert into the hash table, primary slot */
		slot = ppc_md.hpte_insert(hpte_group, va, pa, rflags, 0,
					  mmu_huge_psize, ssize);

		/* Primary is full, try the secondary */
		if (unlikely(slot == -1)) {
			hpte_group = ((~hash & htab_hash_mask) *
				      HPTES_PER_GROUP) & ~0x7UL; 
			slot = ppc_md.hpte_insert(hpte_group, va, pa, rflags,
						  HPTE_V_SECONDARY,
						  mmu_huge_psize, ssize);
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

static void zero_ctor(void *addr, struct kmem_cache *cache, unsigned long flags)
{
	memset(addr, 0, kmem_cache_size(cache));
}

static int __init hugetlbpage_init(void)
{
	if (!cpu_has_feature(CPU_FTR_16M_PAGE))
		return -ENODEV;

	huge_pgtable_cache = kmem_cache_create("hugepte_cache",
					       HUGEPTE_TABLE_SIZE,
					       HUGEPTE_TABLE_SIZE,
					       0,
					       zero_ctor);
	if (! huge_pgtable_cache)
		panic("hugetlbpage_init(): could not create hugepte cache\n");

	return 0;
}

module_init(hugetlbpage_init);
