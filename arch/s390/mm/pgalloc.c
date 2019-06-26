// SPDX-License-Identifier: GPL-2.0
/*
 *  Page table allocation functions
 *
 *    Copyright IBM Corp. 2016
 *    Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#include <linux/sysctl.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <asm/mmu_context.h>
#include <asm/pgalloc.h>
#include <asm/gmap.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>

#ifdef CONFIG_PGSTE

static int page_table_allocate_pgste_min = 0;
static int page_table_allocate_pgste_max = 1;
int page_table_allocate_pgste = 0;
EXPORT_SYMBOL(page_table_allocate_pgste);

static struct ctl_table page_table_sysctl[] = {
	{
		.procname	= "allocate_pgste",
		.data		= &page_table_allocate_pgste,
		.maxlen		= sizeof(int),
		.mode		= S_IRUGO | S_IWUSR,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &page_table_allocate_pgste_min,
		.extra2		= &page_table_allocate_pgste_max,
	},
	{ }
};

static struct ctl_table page_table_sysctl_dir[] = {
	{
		.procname	= "vm",
		.maxlen		= 0,
		.mode		= 0555,
		.child		= page_table_sysctl,
	},
	{ }
};

static int __init page_table_register_sysctl(void)
{
	return register_sysctl_table(page_table_sysctl_dir) ? 0 : -ENOMEM;
}
__initcall(page_table_register_sysctl);

#endif /* CONFIG_PGSTE */

unsigned long *crst_table_alloc(struct mm_struct *mm)
{
	struct page *page = alloc_pages(GFP_KERNEL, 2);

	if (!page)
		return NULL;
	arch_set_page_dat(page, 2);
	return (unsigned long *) page_to_phys(page);
}

void crst_table_free(struct mm_struct *mm, unsigned long *table)
{
	free_pages((unsigned long) table, 2);
}

static void __crst_table_upgrade(void *arg)
{
	struct mm_struct *mm = arg;

	if (current->active_mm == mm)
		set_user_asce(mm);
	__tlb_flush_local();
}

int crst_table_upgrade(struct mm_struct *mm, unsigned long end)
{
	unsigned long *table, *pgd;
	int rc, notify;

	/* upgrade should only happen from 3 to 4, 3 to 5, or 4 to 5 levels */
	VM_BUG_ON(mm->context.asce_limit < _REGION2_SIZE);
	rc = 0;
	notify = 0;
	while (mm->context.asce_limit < end) {
		table = crst_table_alloc(mm);
		if (!table) {
			rc = -ENOMEM;
			break;
		}
		spin_lock_bh(&mm->page_table_lock);
		pgd = (unsigned long *) mm->pgd;
		if (mm->context.asce_limit == _REGION2_SIZE) {
			crst_table_init(table, _REGION2_ENTRY_EMPTY);
			p4d_populate(mm, (p4d_t *) table, (pud_t *) pgd);
			mm->pgd = (pgd_t *) table;
			mm->context.asce_limit = _REGION1_SIZE;
			mm->context.asce = __pa(mm->pgd) | _ASCE_TABLE_LENGTH |
				_ASCE_USER_BITS | _ASCE_TYPE_REGION2;
			mm_inc_nr_puds(mm);
		} else {
			crst_table_init(table, _REGION1_ENTRY_EMPTY);
			pgd_populate(mm, (pgd_t *) table, (p4d_t *) pgd);
			mm->pgd = (pgd_t *) table;
			mm->context.asce_limit = -PAGE_SIZE;
			mm->context.asce = __pa(mm->pgd) | _ASCE_TABLE_LENGTH |
				_ASCE_USER_BITS | _ASCE_TYPE_REGION1;
		}
		notify = 1;
		spin_unlock_bh(&mm->page_table_lock);
	}
	if (notify)
		on_each_cpu(__crst_table_upgrade, mm, 0);
	return rc;
}

void crst_table_downgrade(struct mm_struct *mm)
{
	pgd_t *pgd;

	/* downgrade should only happen from 3 to 2 levels (compat only) */
	VM_BUG_ON(mm->context.asce_limit != _REGION2_SIZE);

	if (current->active_mm == mm) {
		clear_user_asce();
		__tlb_flush_mm(mm);
	}

	pgd = mm->pgd;
	mm_dec_nr_pmds(mm);
	mm->pgd = (pgd_t *) (pgd_val(*pgd) & _REGION_ENTRY_ORIGIN);
	mm->context.asce_limit = _REGION3_SIZE;
	mm->context.asce = __pa(mm->pgd) | _ASCE_TABLE_LENGTH |
			   _ASCE_USER_BITS | _ASCE_TYPE_SEGMENT;
	crst_table_free(mm, (unsigned long *) pgd);

	if (current->active_mm == mm)
		set_user_asce(mm);
}

static inline unsigned int atomic_xor_bits(atomic_t *v, unsigned int bits)
{
	unsigned int old, new;

	do {
		old = atomic_read(v);
		new = old ^ bits;
	} while (atomic_cmpxchg(v, old, new) != old);
	return new;
}

#ifdef CONFIG_PGSTE

struct page *page_table_alloc_pgste(struct mm_struct *mm)
{
	struct page *page;
	u64 *table;

	page = alloc_page(GFP_KERNEL);
	if (page) {
		table = (u64 *)page_to_phys(page);
		memset64(table, _PAGE_INVALID, PTRS_PER_PTE);
		memset64(table + PTRS_PER_PTE, 0, PTRS_PER_PTE);
	}
	return page;
}

void page_table_free_pgste(struct page *page)
{
	__free_page(page);
}

#endif /* CONFIG_PGSTE */

/*
 * page table entry allocation/free routines.
 */
unsigned long *page_table_alloc(struct mm_struct *mm)
{
	unsigned long *table;
	struct page *page;
	unsigned int mask, bit;

	/* Try to get a fragment of a 4K page as a 2K page table */
	if (!mm_alloc_pgste(mm)) {
		table = NULL;
		spin_lock_bh(&mm->context.lock);
		if (!list_empty(&mm->context.pgtable_list)) {
			page = list_first_entry(&mm->context.pgtable_list,
						struct page, lru);
			mask = atomic_read(&page->_refcount) >> 24;
			mask = (mask | (mask >> 4)) & 3;
			if (mask != 3) {
				table = (unsigned long *) page_to_phys(page);
				bit = mask & 1;		/* =1 -> second 2K */
				if (bit)
					table += PTRS_PER_PTE;
				atomic_xor_bits(&page->_refcount,
							1U << (bit + 24));
				list_del(&page->lru);
			}
		}
		spin_unlock_bh(&mm->context.lock);
		if (table)
			return table;
	}
	/* Allocate a fresh page */
	page = alloc_page(GFP_KERNEL);
	if (!page)
		return NULL;
	if (!pgtable_page_ctor(page)) {
		__free_page(page);
		return NULL;
	}
	arch_set_page_dat(page, 0);
	/* Initialize page table */
	table = (unsigned long *) page_to_phys(page);
	if (mm_alloc_pgste(mm)) {
		/* Return 4K page table with PGSTEs */
		atomic_xor_bits(&page->_refcount, 3 << 24);
		memset64((u64 *)table, _PAGE_INVALID, PTRS_PER_PTE);
		memset64((u64 *)table + PTRS_PER_PTE, 0, PTRS_PER_PTE);
	} else {
		/* Return the first 2K fragment of the page */
		atomic_xor_bits(&page->_refcount, 1 << 24);
		memset64((u64 *)table, _PAGE_INVALID, 2 * PTRS_PER_PTE);
		spin_lock_bh(&mm->context.lock);
		list_add(&page->lru, &mm->context.pgtable_list);
		spin_unlock_bh(&mm->context.lock);
	}
	return table;
}

void page_table_free(struct mm_struct *mm, unsigned long *table)
{
	struct page *page;
	unsigned int bit, mask;

	page = pfn_to_page(__pa(table) >> PAGE_SHIFT);
	if (!mm_alloc_pgste(mm)) {
		/* Free 2K page table fragment of a 4K page */
		bit = (__pa(table) & ~PAGE_MASK)/(PTRS_PER_PTE*sizeof(pte_t));
		spin_lock_bh(&mm->context.lock);
		mask = atomic_xor_bits(&page->_refcount, 1U << (bit + 24));
		mask >>= 24;
		if (mask & 3)
			list_add(&page->lru, &mm->context.pgtable_list);
		else
			list_del(&page->lru);
		spin_unlock_bh(&mm->context.lock);
		if (mask != 0)
			return;
	} else {
		atomic_xor_bits(&page->_refcount, 3U << 24);
	}

	pgtable_page_dtor(page);
	__free_page(page);
}

void page_table_free_rcu(struct mmu_gather *tlb, unsigned long *table,
			 unsigned long vmaddr)
{
	struct mm_struct *mm;
	struct page *page;
	unsigned int bit, mask;

	mm = tlb->mm;
	page = pfn_to_page(__pa(table) >> PAGE_SHIFT);
	if (mm_alloc_pgste(mm)) {
		gmap_unlink(mm, table, vmaddr);
		table = (unsigned long *) (__pa(table) | 3);
		tlb_remove_table(tlb, table);
		return;
	}
	bit = (__pa(table) & ~PAGE_MASK) / (PTRS_PER_PTE*sizeof(pte_t));
	spin_lock_bh(&mm->context.lock);
	mask = atomic_xor_bits(&page->_refcount, 0x11U << (bit + 24));
	mask >>= 24;
	if (mask & 3)
		list_add_tail(&page->lru, &mm->context.pgtable_list);
	else
		list_del(&page->lru);
	spin_unlock_bh(&mm->context.lock);
	table = (unsigned long *) (__pa(table) | (1U << bit));
	tlb_remove_table(tlb, table);
}

void __tlb_remove_table(void *_table)
{
	unsigned int mask = (unsigned long) _table & 3;
	void *table = (void *)((unsigned long) _table ^ mask);
	struct page *page = pfn_to_page(__pa(table) >> PAGE_SHIFT);

	switch (mask) {
	case 0:		/* pmd, pud, or p4d */
		free_pages((unsigned long) table, 2);
		break;
	case 1:		/* lower 2K of a 4K page table */
	case 2:		/* higher 2K of a 4K page table */
		mask = atomic_xor_bits(&page->_refcount, mask << (4 + 24));
		mask >>= 24;
		if (mask != 0)
			break;
		/* fallthrough */
	case 3:		/* 4K page table with pgstes */
		if (mask & 3)
			atomic_xor_bits(&page->_refcount, 3 << 24);
		pgtable_page_dtor(page);
		__free_page(page);
		break;
	}
}

/*
 * Base infrastructure required to generate basic asces, region, segment,
 * and page tables that do not make use of enhanced features like EDAT1.
 */

static struct kmem_cache *base_pgt_cache;

static unsigned long base_pgt_alloc(void)
{
	u64 *table;

	table = kmem_cache_alloc(base_pgt_cache, GFP_KERNEL);
	if (table)
		memset64(table, _PAGE_INVALID, PTRS_PER_PTE);
	return (unsigned long) table;
}

static void base_pgt_free(unsigned long table)
{
	kmem_cache_free(base_pgt_cache, (void *) table);
}

static unsigned long base_crst_alloc(unsigned long val)
{
	unsigned long table;

	table =	 __get_free_pages(GFP_KERNEL, CRST_ALLOC_ORDER);
	if (table)
		crst_table_init((unsigned long *)table, val);
	return table;
}

static void base_crst_free(unsigned long table)
{
	free_pages(table, CRST_ALLOC_ORDER);
}

#define BASE_ADDR_END_FUNC(NAME, SIZE)					\
static inline unsigned long base_##NAME##_addr_end(unsigned long addr,	\
						   unsigned long end)	\
{									\
	unsigned long next = (addr + (SIZE)) & ~((SIZE) - 1);		\
									\
	return (next - 1) < (end - 1) ? next : end;			\
}

BASE_ADDR_END_FUNC(page,    _PAGE_SIZE)
BASE_ADDR_END_FUNC(segment, _SEGMENT_SIZE)
BASE_ADDR_END_FUNC(region3, _REGION3_SIZE)
BASE_ADDR_END_FUNC(region2, _REGION2_SIZE)
BASE_ADDR_END_FUNC(region1, _REGION1_SIZE)

static inline unsigned long base_lra(unsigned long address)
{
	unsigned long real;

	asm volatile(
		"	lra	%0,0(%1)\n"
		: "=d" (real) : "a" (address) : "cc");
	return real;
}

static int base_page_walk(unsigned long origin, unsigned long addr,
			  unsigned long end, int alloc)
{
	unsigned long *pte, next;

	if (!alloc)
		return 0;
	pte = (unsigned long *) origin;
	pte += (addr & _PAGE_INDEX) >> _PAGE_SHIFT;
	do {
		next = base_page_addr_end(addr, end);
		*pte = base_lra(addr);
	} while (pte++, addr = next, addr < end);
	return 0;
}

static int base_segment_walk(unsigned long origin, unsigned long addr,
			     unsigned long end, int alloc)
{
	unsigned long *ste, next, table;
	int rc;

	ste = (unsigned long *) origin;
	ste += (addr & _SEGMENT_INDEX) >> _SEGMENT_SHIFT;
	do {
		next = base_segment_addr_end(addr, end);
		if (*ste & _SEGMENT_ENTRY_INVALID) {
			if (!alloc)
				continue;
			table = base_pgt_alloc();
			if (!table)
				return -ENOMEM;
			*ste = table | _SEGMENT_ENTRY;
		}
		table = *ste & _SEGMENT_ENTRY_ORIGIN;
		rc = base_page_walk(table, addr, next, alloc);
		if (rc)
			return rc;
		if (!alloc)
			base_pgt_free(table);
		cond_resched();
	} while (ste++, addr = next, addr < end);
	return 0;
}

static int base_region3_walk(unsigned long origin, unsigned long addr,
			     unsigned long end, int alloc)
{
	unsigned long *rtte, next, table;
	int rc;

	rtte = (unsigned long *) origin;
	rtte += (addr & _REGION3_INDEX) >> _REGION3_SHIFT;
	do {
		next = base_region3_addr_end(addr, end);
		if (*rtte & _REGION_ENTRY_INVALID) {
			if (!alloc)
				continue;
			table = base_crst_alloc(_SEGMENT_ENTRY_EMPTY);
			if (!table)
				return -ENOMEM;
			*rtte = table | _REGION3_ENTRY;
		}
		table = *rtte & _REGION_ENTRY_ORIGIN;
		rc = base_segment_walk(table, addr, next, alloc);
		if (rc)
			return rc;
		if (!alloc)
			base_crst_free(table);
	} while (rtte++, addr = next, addr < end);
	return 0;
}

static int base_region2_walk(unsigned long origin, unsigned long addr,
			     unsigned long end, int alloc)
{
	unsigned long *rste, next, table;
	int rc;

	rste = (unsigned long *) origin;
	rste += (addr & _REGION2_INDEX) >> _REGION2_SHIFT;
	do {
		next = base_region2_addr_end(addr, end);
		if (*rste & _REGION_ENTRY_INVALID) {
			if (!alloc)
				continue;
			table = base_crst_alloc(_REGION3_ENTRY_EMPTY);
			if (!table)
				return -ENOMEM;
			*rste = table | _REGION2_ENTRY;
		}
		table = *rste & _REGION_ENTRY_ORIGIN;
		rc = base_region3_walk(table, addr, next, alloc);
		if (rc)
			return rc;
		if (!alloc)
			base_crst_free(table);
	} while (rste++, addr = next, addr < end);
	return 0;
}

static int base_region1_walk(unsigned long origin, unsigned long addr,
			     unsigned long end, int alloc)
{
	unsigned long *rfte, next, table;
	int rc;

	rfte = (unsigned long *) origin;
	rfte += (addr & _REGION1_INDEX) >> _REGION1_SHIFT;
	do {
		next = base_region1_addr_end(addr, end);
		if (*rfte & _REGION_ENTRY_INVALID) {
			if (!alloc)
				continue;
			table = base_crst_alloc(_REGION2_ENTRY_EMPTY);
			if (!table)
				return -ENOMEM;
			*rfte = table | _REGION1_ENTRY;
		}
		table = *rfte & _REGION_ENTRY_ORIGIN;
		rc = base_region2_walk(table, addr, next, alloc);
		if (rc)
			return rc;
		if (!alloc)
			base_crst_free(table);
	} while (rfte++, addr = next, addr < end);
	return 0;
}

/**
 * base_asce_free - free asce and tables returned from base_asce_alloc()
 * @asce: asce to be freed
 *
 * Frees all region, segment, and page tables that were allocated with a
 * corresponding base_asce_alloc() call.
 */
void base_asce_free(unsigned long asce)
{
	unsigned long table = asce & _ASCE_ORIGIN;

	if (!asce)
		return;
	switch (asce & _ASCE_TYPE_MASK) {
	case _ASCE_TYPE_SEGMENT:
		base_segment_walk(table, 0, _REGION3_SIZE, 0);
		break;
	case _ASCE_TYPE_REGION3:
		base_region3_walk(table, 0, _REGION2_SIZE, 0);
		break;
	case _ASCE_TYPE_REGION2:
		base_region2_walk(table, 0, _REGION1_SIZE, 0);
		break;
	case _ASCE_TYPE_REGION1:
		base_region1_walk(table, 0, -_PAGE_SIZE, 0);
		break;
	}
	base_crst_free(table);
}

static int base_pgt_cache_init(void)
{
	static DEFINE_MUTEX(base_pgt_cache_mutex);
	unsigned long sz = _PAGE_TABLE_SIZE;

	if (base_pgt_cache)
		return 0;
	mutex_lock(&base_pgt_cache_mutex);
	if (!base_pgt_cache)
		base_pgt_cache = kmem_cache_create("base_pgt", sz, sz, 0, NULL);
	mutex_unlock(&base_pgt_cache_mutex);
	return base_pgt_cache ? 0 : -ENOMEM;
}

/**
 * base_asce_alloc - create kernel mapping without enhanced DAT features
 * @addr: virtual start address of kernel mapping
 * @num_pages: number of consecutive pages
 *
 * Generate an asce, including all required region, segment and page tables,
 * that can be used to access the virtual kernel mapping. The difference is
 * that the returned asce does not make use of any enhanced DAT features like
 * e.g. large pages. This is required for some I/O functions that pass an
 * asce, like e.g. some service call requests.
 *
 * Note: the returned asce may NEVER be attached to any cpu. It may only be
 *	 used for I/O requests. tlb entries that might result because the
 *	 asce was attached to a cpu won't be cleared.
 */
unsigned long base_asce_alloc(unsigned long addr, unsigned long num_pages)
{
	unsigned long asce, table, end;
	int rc;

	if (base_pgt_cache_init())
		return 0;
	end = addr + num_pages * PAGE_SIZE;
	if (end <= _REGION3_SIZE) {
		table = base_crst_alloc(_SEGMENT_ENTRY_EMPTY);
		if (!table)
			return 0;
		rc = base_segment_walk(table, addr, end, 1);
		asce = table | _ASCE_TYPE_SEGMENT | _ASCE_TABLE_LENGTH;
	} else if (end <= _REGION2_SIZE) {
		table = base_crst_alloc(_REGION3_ENTRY_EMPTY);
		if (!table)
			return 0;
		rc = base_region3_walk(table, addr, end, 1);
		asce = table | _ASCE_TYPE_REGION3 | _ASCE_TABLE_LENGTH;
	} else if (end <= _REGION1_SIZE) {
		table = base_crst_alloc(_REGION2_ENTRY_EMPTY);
		if (!table)
			return 0;
		rc = base_region2_walk(table, addr, end, 1);
		asce = table | _ASCE_TYPE_REGION2 | _ASCE_TABLE_LENGTH;
	} else {
		table = base_crst_alloc(_REGION1_ENTRY_EMPTY);
		if (!table)
			return 0;
		rc = base_region1_walk(table, addr, end, 1);
		asce = table | _ASCE_TYPE_REGION1 | _ASCE_TABLE_LENGTH;
	}
	if (rc) {
		base_asce_free(asce);
		asce = 0;
	}
	return asce;
}
