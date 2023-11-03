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
#include <asm/page-states.h>
#include <asm/pgalloc.h>
#include <asm/gmap.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>

#ifdef CONFIG_PGSTE

int page_table_allocate_pgste = 0;
EXPORT_SYMBOL(page_table_allocate_pgste);

static struct ctl_table page_table_sysctl[] = {
	{
		.procname	= "allocate_pgste",
		.data		= &page_table_allocate_pgste,
		.maxlen		= sizeof(int),
		.mode		= S_IRUGO | S_IWUSR,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
};

static int __init page_table_register_sysctl(void)
{
	return register_sysctl("vm", page_table_sysctl) ? 0 : -ENOMEM;
}
__initcall(page_table_register_sysctl);

#endif /* CONFIG_PGSTE */

unsigned long *crst_table_alloc(struct mm_struct *mm)
{
	struct ptdesc *ptdesc = pagetable_alloc(GFP_KERNEL, CRST_ALLOC_ORDER);
	unsigned long *table;

	if (!ptdesc)
		return NULL;
	table = ptdesc_to_virt(ptdesc);
	__arch_set_page_dat(table, 1UL << CRST_ALLOC_ORDER);
	return table;
}

void crst_table_free(struct mm_struct *mm, unsigned long *table)
{
	pagetable_free(virt_to_ptdesc(table));
}

static void __crst_table_upgrade(void *arg)
{
	struct mm_struct *mm = arg;

	/* change all active ASCEs to avoid the creation of new TLBs */
	if (current->active_mm == mm) {
		S390_lowcore.user_asce.val = mm->context.asce;
		local_ctl_load(7, &S390_lowcore.user_asce);
	}
	__tlb_flush_local();
}

int crst_table_upgrade(struct mm_struct *mm, unsigned long end)
{
	unsigned long *pgd = NULL, *p4d = NULL, *__pgd;
	unsigned long asce_limit = mm->context.asce_limit;

	/* upgrade should only happen from 3 to 4, 3 to 5, or 4 to 5 levels */
	VM_BUG_ON(asce_limit < _REGION2_SIZE);

	if (end <= asce_limit)
		return 0;

	if (asce_limit == _REGION2_SIZE) {
		p4d = crst_table_alloc(mm);
		if (unlikely(!p4d))
			goto err_p4d;
		crst_table_init(p4d, _REGION2_ENTRY_EMPTY);
	}
	if (end > _REGION1_SIZE) {
		pgd = crst_table_alloc(mm);
		if (unlikely(!pgd))
			goto err_pgd;
		crst_table_init(pgd, _REGION1_ENTRY_EMPTY);
	}

	spin_lock_bh(&mm->page_table_lock);

	/*
	 * This routine gets called with mmap_lock lock held and there is
	 * no reason to optimize for the case of otherwise. However, if
	 * that would ever change, the below check will let us know.
	 */
	VM_BUG_ON(asce_limit != mm->context.asce_limit);

	if (p4d) {
		__pgd = (unsigned long *) mm->pgd;
		p4d_populate(mm, (p4d_t *) p4d, (pud_t *) __pgd);
		mm->pgd = (pgd_t *) p4d;
		mm->context.asce_limit = _REGION1_SIZE;
		mm->context.asce = __pa(mm->pgd) | _ASCE_TABLE_LENGTH |
			_ASCE_USER_BITS | _ASCE_TYPE_REGION2;
		mm_inc_nr_puds(mm);
	}
	if (pgd) {
		__pgd = (unsigned long *) mm->pgd;
		pgd_populate(mm, (pgd_t *) pgd, (p4d_t *) __pgd);
		mm->pgd = (pgd_t *) pgd;
		mm->context.asce_limit = TASK_SIZE_MAX;
		mm->context.asce = __pa(mm->pgd) | _ASCE_TABLE_LENGTH |
			_ASCE_USER_BITS | _ASCE_TYPE_REGION1;
	}

	spin_unlock_bh(&mm->page_table_lock);

	on_each_cpu(__crst_table_upgrade, mm, 0);

	return 0;

err_pgd:
	crst_table_free(mm, p4d);
err_p4d:
	return -ENOMEM;
}

#ifdef CONFIG_PGSTE

struct page *page_table_alloc_pgste(struct mm_struct *mm)
{
	struct ptdesc *ptdesc;
	u64 *table;

	ptdesc = pagetable_alloc(GFP_KERNEL, 0);
	if (ptdesc) {
		table = (u64 *)ptdesc_to_virt(ptdesc);
		__arch_set_page_dat(table, 1);
		memset64(table, _PAGE_INVALID, PTRS_PER_PTE);
		memset64(table + PTRS_PER_PTE, 0, PTRS_PER_PTE);
	}
	return ptdesc_page(ptdesc);
}

void page_table_free_pgste(struct page *page)
{
	pagetable_free(page_ptdesc(page));
}

#endif /* CONFIG_PGSTE */

unsigned long *page_table_alloc(struct mm_struct *mm)
{
	struct ptdesc *ptdesc;
	unsigned long *table;

	ptdesc = pagetable_alloc(GFP_KERNEL, 0);
	if (!ptdesc)
		return NULL;
	if (!pagetable_pte_ctor(ptdesc)) {
		pagetable_free(ptdesc);
		return NULL;
	}
	table = ptdesc_to_virt(ptdesc);
	__arch_set_page_dat(table, 1);
	/* pt_list is used by gmap only */
	INIT_LIST_HEAD(&ptdesc->pt_list);
	memset64((u64 *)table, _PAGE_INVALID, PTRS_PER_PTE);
	memset64((u64 *)table + PTRS_PER_PTE, 0, PTRS_PER_PTE);
	return table;
}

static void pagetable_pte_dtor_free(struct ptdesc *ptdesc)
{
	pagetable_pte_dtor(ptdesc);
	pagetable_free(ptdesc);
}

void page_table_free(struct mm_struct *mm, unsigned long *table)
{
	struct ptdesc *ptdesc = virt_to_ptdesc(table);

	pagetable_pte_dtor_free(ptdesc);
}

void __tlb_remove_table(void *table)
{
	struct ptdesc *ptdesc = virt_to_ptdesc(table);
	struct page *page = ptdesc_page(ptdesc);

	if (compound_order(page) == CRST_ALLOC_ORDER) {
		/* pmd, pud, or p4d */
		pagetable_free(ptdesc);
		return;
	}
	pagetable_pte_dtor_free(ptdesc);
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
static void pte_free_now(struct rcu_head *head)
{
	struct ptdesc *ptdesc = container_of(head, struct ptdesc, pt_rcu_head);

	pagetable_pte_dtor_free(ptdesc);
}

void pte_free_defer(struct mm_struct *mm, pgtable_t pgtable)
{
	struct ptdesc *ptdesc = virt_to_ptdesc(pgtable);

	call_rcu(&ptdesc->pt_rcu_head, pte_free_now);
	/*
	 * THPs are not allowed for KVM guests. Warn if pgste ever reaches here.
	 * Turn to the generic pte_free_defer() version once gmap is removed.
	 */
	WARN_ON_ONCE(mm_has_pgste(mm));
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

/*
 * Base infrastructure required to generate basic asces, region, segment,
 * and page tables that do not make use of enhanced features like EDAT1.
 */

static struct kmem_cache *base_pgt_cache;

static unsigned long *base_pgt_alloc(void)
{
	unsigned long *table;

	table = kmem_cache_alloc(base_pgt_cache, GFP_KERNEL);
	if (table)
		memset64((u64 *)table, _PAGE_INVALID, PTRS_PER_PTE);
	return table;
}

static void base_pgt_free(unsigned long *table)
{
	kmem_cache_free(base_pgt_cache, table);
}

static unsigned long *base_crst_alloc(unsigned long val)
{
	unsigned long *table;
	struct ptdesc *ptdesc;

	ptdesc = pagetable_alloc(GFP_KERNEL, CRST_ALLOC_ORDER);
	if (!ptdesc)
		return NULL;
	table = ptdesc_address(ptdesc);
	crst_table_init(table, val);
	return table;
}

static void base_crst_free(unsigned long *table)
{
	pagetable_free(virt_to_ptdesc(table));
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

static int base_page_walk(unsigned long *origin, unsigned long addr,
			  unsigned long end, int alloc)
{
	unsigned long *pte, next;

	if (!alloc)
		return 0;
	pte = origin;
	pte += (addr & _PAGE_INDEX) >> _PAGE_SHIFT;
	do {
		next = base_page_addr_end(addr, end);
		*pte = base_lra(addr);
	} while (pte++, addr = next, addr < end);
	return 0;
}

static int base_segment_walk(unsigned long *origin, unsigned long addr,
			     unsigned long end, int alloc)
{
	unsigned long *ste, next, *table;
	int rc;

	ste = origin;
	ste += (addr & _SEGMENT_INDEX) >> _SEGMENT_SHIFT;
	do {
		next = base_segment_addr_end(addr, end);
		if (*ste & _SEGMENT_ENTRY_INVALID) {
			if (!alloc)
				continue;
			table = base_pgt_alloc();
			if (!table)
				return -ENOMEM;
			*ste = __pa(table) | _SEGMENT_ENTRY;
		}
		table = __va(*ste & _SEGMENT_ENTRY_ORIGIN);
		rc = base_page_walk(table, addr, next, alloc);
		if (rc)
			return rc;
		if (!alloc)
			base_pgt_free(table);
		cond_resched();
	} while (ste++, addr = next, addr < end);
	return 0;
}

static int base_region3_walk(unsigned long *origin, unsigned long addr,
			     unsigned long end, int alloc)
{
	unsigned long *rtte, next, *table;
	int rc;

	rtte = origin;
	rtte += (addr & _REGION3_INDEX) >> _REGION3_SHIFT;
	do {
		next = base_region3_addr_end(addr, end);
		if (*rtte & _REGION_ENTRY_INVALID) {
			if (!alloc)
				continue;
			table = base_crst_alloc(_SEGMENT_ENTRY_EMPTY);
			if (!table)
				return -ENOMEM;
			*rtte = __pa(table) | _REGION3_ENTRY;
		}
		table = __va(*rtte & _REGION_ENTRY_ORIGIN);
		rc = base_segment_walk(table, addr, next, alloc);
		if (rc)
			return rc;
		if (!alloc)
			base_crst_free(table);
	} while (rtte++, addr = next, addr < end);
	return 0;
}

static int base_region2_walk(unsigned long *origin, unsigned long addr,
			     unsigned long end, int alloc)
{
	unsigned long *rste, next, *table;
	int rc;

	rste = origin;
	rste += (addr & _REGION2_INDEX) >> _REGION2_SHIFT;
	do {
		next = base_region2_addr_end(addr, end);
		if (*rste & _REGION_ENTRY_INVALID) {
			if (!alloc)
				continue;
			table = base_crst_alloc(_REGION3_ENTRY_EMPTY);
			if (!table)
				return -ENOMEM;
			*rste = __pa(table) | _REGION2_ENTRY;
		}
		table = __va(*rste & _REGION_ENTRY_ORIGIN);
		rc = base_region3_walk(table, addr, next, alloc);
		if (rc)
			return rc;
		if (!alloc)
			base_crst_free(table);
	} while (rste++, addr = next, addr < end);
	return 0;
}

static int base_region1_walk(unsigned long *origin, unsigned long addr,
			     unsigned long end, int alloc)
{
	unsigned long *rfte, next, *table;
	int rc;

	rfte = origin;
	rfte += (addr & _REGION1_INDEX) >> _REGION1_SHIFT;
	do {
		next = base_region1_addr_end(addr, end);
		if (*rfte & _REGION_ENTRY_INVALID) {
			if (!alloc)
				continue;
			table = base_crst_alloc(_REGION2_ENTRY_EMPTY);
			if (!table)
				return -ENOMEM;
			*rfte = __pa(table) | _REGION1_ENTRY;
		}
		table = __va(*rfte & _REGION_ENTRY_ORIGIN);
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
	unsigned long *table = __va(asce & _ASCE_ORIGIN);

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
		base_region1_walk(table, 0, TASK_SIZE_MAX, 0);
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
	unsigned long asce, *table, end;
	int rc;

	if (base_pgt_cache_init())
		return 0;
	end = addr + num_pages * PAGE_SIZE;
	if (end <= _REGION3_SIZE) {
		table = base_crst_alloc(_SEGMENT_ENTRY_EMPTY);
		if (!table)
			return 0;
		rc = base_segment_walk(table, addr, end, 1);
		asce = __pa(table) | _ASCE_TYPE_SEGMENT | _ASCE_TABLE_LENGTH;
	} else if (end <= _REGION2_SIZE) {
		table = base_crst_alloc(_REGION3_ENTRY_EMPTY);
		if (!table)
			return 0;
		rc = base_region3_walk(table, addr, end, 1);
		asce = __pa(table) | _ASCE_TYPE_REGION3 | _ASCE_TABLE_LENGTH;
	} else if (end <= _REGION1_SIZE) {
		table = base_crst_alloc(_REGION2_ENTRY_EMPTY);
		if (!table)
			return 0;
		rc = base_region2_walk(table, addr, end, 1);
		asce = __pa(table) | _ASCE_TYPE_REGION2 | _ASCE_TABLE_LENGTH;
	} else {
		table = base_crst_alloc(_REGION1_ENTRY_EMPTY);
		if (!table)
			return 0;
		rc = base_region1_walk(table, addr, end, 1);
		asce = __pa(table) | _ASCE_TYPE_REGION1 | _ASCE_TABLE_LENGTH;
	}
	if (rc) {
		base_asce_free(asce);
		asce = 0;
	}
	return asce;
}
