// SPDX-License-Identifier: GPL-2.0
/*
 *  Page table allocation functions
 *
 *    Copyright IBM Corp. 2016
 *    Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#include <linux/mm.h>
#include <linux/sysctl.h>
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
		.proc_handler	= proc_dointvec,
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
			mask = atomic_read(&page->_mapcount);
			mask = (mask | (mask >> 4)) & 3;
			if (mask != 3) {
				table = (unsigned long *) page_to_phys(page);
				bit = mask & 1;		/* =1 -> second 2K */
				if (bit)
					table += PTRS_PER_PTE;
				atomic_xor_bits(&page->_mapcount, 1U << bit);
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
		atomic_set(&page->_mapcount, 3);
		memset64((u64 *)table, _PAGE_INVALID, PTRS_PER_PTE);
		memset64((u64 *)table + PTRS_PER_PTE, 0, PTRS_PER_PTE);
	} else {
		/* Return the first 2K fragment of the page */
		atomic_set(&page->_mapcount, 1);
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
		mask = atomic_xor_bits(&page->_mapcount, 1U << bit);
		if (mask & 3)
			list_add(&page->lru, &mm->context.pgtable_list);
		else
			list_del(&page->lru);
		spin_unlock_bh(&mm->context.lock);
		if (mask != 0)
			return;
	}

	pgtable_page_dtor(page);
	atomic_set(&page->_mapcount, -1);
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
	mask = atomic_xor_bits(&page->_mapcount, 0x11U << bit);
	if (mask & 3)
		list_add_tail(&page->lru, &mm->context.pgtable_list);
	else
		list_del(&page->lru);
	spin_unlock_bh(&mm->context.lock);
	table = (unsigned long *) (__pa(table) | (1U << bit));
	tlb_remove_table(tlb, table);
}

static void __tlb_remove_table(void *_table)
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
		if (atomic_xor_bits(&page->_mapcount, mask << 4) != 0)
			break;
		/* fallthrough */
	case 3:		/* 4K page table with pgstes */
		pgtable_page_dtor(page);
		atomic_set(&page->_mapcount, -1);
		__free_page(page);
		break;
	}
}

static void tlb_remove_table_smp_sync(void *arg)
{
	/* Simply deliver the interrupt */
}

static void tlb_remove_table_one(void *table)
{
	/*
	 * This isn't an RCU grace period and hence the page-tables cannot be
	 * assumed to be actually RCU-freed.
	 *
	 * It is however sufficient for software page-table walkers that rely
	 * on IRQ disabling. See the comment near struct mmu_table_batch.
	 */
	smp_call_function(tlb_remove_table_smp_sync, NULL, 1);
	__tlb_remove_table(table);
}

static void tlb_remove_table_rcu(struct rcu_head *head)
{
	struct mmu_table_batch *batch;
	int i;

	batch = container_of(head, struct mmu_table_batch, rcu);

	for (i = 0; i < batch->nr; i++)
		__tlb_remove_table(batch->tables[i]);

	free_page((unsigned long)batch);
}

void tlb_table_flush(struct mmu_gather *tlb)
{
	struct mmu_table_batch **batch = &tlb->batch;

	if (*batch) {
		call_rcu_sched(&(*batch)->rcu, tlb_remove_table_rcu);
		*batch = NULL;
	}
}

void tlb_remove_table(struct mmu_gather *tlb, void *table)
{
	struct mmu_table_batch **batch = &tlb->batch;

	tlb->mm->context.flush_mm = 1;
	if (*batch == NULL) {
		*batch = (struct mmu_table_batch *)
			__get_free_page(GFP_NOWAIT | __GFP_NOWARN);
		if (*batch == NULL) {
			__tlb_flush_mm_lazy(tlb->mm);
			tlb_remove_table_one(table);
			return;
		}
		(*batch)->nr = 0;
	}
	(*batch)->tables[(*batch)->nr++] = table;
	if ((*batch)->nr == MAX_TABLE_BATCH)
		tlb_flush_mmu(tlb);
}
