/*
 *  arch/s390/mm/pgtable.c
 *
 *    Copyright IBM Corp. 2007
 *    Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/smp.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/quicklist.h>

#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/mmu_context.h>

#ifndef CONFIG_64BIT
#define ALLOC_ORDER	1
#define TABLES_PER_PAGE	4
#define FRAG_MASK	15UL
#define SECOND_HALVES	10UL
#else
#define ALLOC_ORDER	2
#define TABLES_PER_PAGE	2
#define FRAG_MASK	3UL
#define SECOND_HALVES	2UL
#endif

unsigned long *crst_table_alloc(struct mm_struct *mm, int noexec)
{
	struct page *page = alloc_pages(GFP_KERNEL, ALLOC_ORDER);

	if (!page)
		return NULL;
	page->index = 0;
	if (noexec) {
		struct page *shadow = alloc_pages(GFP_KERNEL, ALLOC_ORDER);
		if (!shadow) {
			__free_pages(page, ALLOC_ORDER);
			return NULL;
		}
		page->index = page_to_phys(shadow);
	}
	spin_lock(&mm->page_table_lock);
	list_add(&page->lru, &mm->context.crst_list);
	spin_unlock(&mm->page_table_lock);
	return (unsigned long *) page_to_phys(page);
}

void crst_table_free(struct mm_struct *mm, unsigned long *table)
{
	unsigned long *shadow = get_shadow_table(table);
	struct page *page = virt_to_page(table);

	spin_lock(&mm->page_table_lock);
	list_del(&page->lru);
	spin_unlock(&mm->page_table_lock);
	if (shadow)
		free_pages((unsigned long) shadow, ALLOC_ORDER);
	free_pages((unsigned long) table, ALLOC_ORDER);
}

#ifdef CONFIG_64BIT
int crst_table_upgrade(struct mm_struct *mm, unsigned long limit)
{
	unsigned long *table, *pgd;
	unsigned long entry;

	BUG_ON(limit > (1UL << 53));
repeat:
	table = crst_table_alloc(mm, mm->context.noexec);
	if (!table)
		return -ENOMEM;
	spin_lock(&mm->page_table_lock);
	if (mm->context.asce_limit < limit) {
		pgd = (unsigned long *) mm->pgd;
		if (mm->context.asce_limit <= (1UL << 31)) {
			entry = _REGION3_ENTRY_EMPTY;
			mm->context.asce_limit = 1UL << 42;
			mm->context.asce_bits = _ASCE_TABLE_LENGTH |
						_ASCE_USER_BITS |
						_ASCE_TYPE_REGION3;
		} else {
			entry = _REGION2_ENTRY_EMPTY;
			mm->context.asce_limit = 1UL << 53;
			mm->context.asce_bits = _ASCE_TABLE_LENGTH |
						_ASCE_USER_BITS |
						_ASCE_TYPE_REGION2;
		}
		crst_table_init(table, entry);
		pgd_populate(mm, (pgd_t *) table, (pud_t *) pgd);
		mm->pgd = (pgd_t *) table;
		table = NULL;
	}
	spin_unlock(&mm->page_table_lock);
	if (table)
		crst_table_free(mm, table);
	if (mm->context.asce_limit < limit)
		goto repeat;
	update_mm(mm, current);
	return 0;
}

void crst_table_downgrade(struct mm_struct *mm, unsigned long limit)
{
	pgd_t *pgd;

	if (mm->context.asce_limit <= limit)
		return;
	__tlb_flush_mm(mm);
	while (mm->context.asce_limit > limit) {
		pgd = mm->pgd;
		switch (pgd_val(*pgd) & _REGION_ENTRY_TYPE_MASK) {
		case _REGION_ENTRY_TYPE_R2:
			mm->context.asce_limit = 1UL << 42;
			mm->context.asce_bits = _ASCE_TABLE_LENGTH |
						_ASCE_USER_BITS |
						_ASCE_TYPE_REGION3;
			break;
		case _REGION_ENTRY_TYPE_R3:
			mm->context.asce_limit = 1UL << 31;
			mm->context.asce_bits = _ASCE_TABLE_LENGTH |
						_ASCE_USER_BITS |
						_ASCE_TYPE_SEGMENT;
			break;
		default:
			BUG();
		}
		mm->pgd = (pgd_t *) (pgd_val(*pgd) & _REGION_ENTRY_ORIGIN);
		crst_table_free(mm, (unsigned long *) pgd);
	}
	update_mm(mm, current);
}
#endif

/*
 * page table entry allocation/free routines.
 */
unsigned long *page_table_alloc(struct mm_struct *mm)
{
	struct page *page;
	unsigned long *table;
	unsigned long bits;

	bits = mm->context.noexec ? 3UL : 1UL;
	spin_lock(&mm->page_table_lock);
	page = NULL;
	if (!list_empty(&mm->context.pgtable_list)) {
		page = list_first_entry(&mm->context.pgtable_list,
					struct page, lru);
		if ((page->flags & FRAG_MASK) == ((1UL << TABLES_PER_PAGE) - 1))
			page = NULL;
	}
	if (!page) {
		spin_unlock(&mm->page_table_lock);
		page = alloc_page(GFP_KERNEL|__GFP_REPEAT);
		if (!page)
			return NULL;
		pgtable_page_ctor(page);
		page->flags &= ~FRAG_MASK;
		table = (unsigned long *) page_to_phys(page);
		clear_table(table, _PAGE_TYPE_EMPTY, PAGE_SIZE);
		spin_lock(&mm->page_table_lock);
		list_add(&page->lru, &mm->context.pgtable_list);
	}
	table = (unsigned long *) page_to_phys(page);
	while (page->flags & bits) {
		table += 256;
		bits <<= 1;
	}
	page->flags |= bits;
	if ((page->flags & FRAG_MASK) == ((1UL << TABLES_PER_PAGE) - 1))
		list_move_tail(&page->lru, &mm->context.pgtable_list);
	spin_unlock(&mm->page_table_lock);
	return table;
}

void page_table_free(struct mm_struct *mm, unsigned long *table)
{
	struct page *page;
	unsigned long bits;

	bits = mm->context.noexec ? 3UL : 1UL;
	bits <<= (__pa(table) & (PAGE_SIZE - 1)) / 256 / sizeof(unsigned long);
	page = pfn_to_page(__pa(table) >> PAGE_SHIFT);
	spin_lock(&mm->page_table_lock);
	page->flags ^= bits;
	if (page->flags & FRAG_MASK) {
		/* Page now has some free pgtable fragments. */
		list_move(&page->lru, &mm->context.pgtable_list);
		page = NULL;
	} else
		/* All fragments of the 4K page have been freed. */
		list_del(&page->lru);
	spin_unlock(&mm->page_table_lock);
	if (page) {
		pgtable_page_dtor(page);
		__free_page(page);
	}
}

void disable_noexec(struct mm_struct *mm, struct task_struct *tsk)
{
	struct page *page;

	spin_lock(&mm->page_table_lock);
	/* Free shadow region and segment tables. */
	list_for_each_entry(page, &mm->context.crst_list, lru)
		if (page->index) {
			free_pages((unsigned long) page->index, ALLOC_ORDER);
			page->index = 0;
		}
	/* "Free" second halves of page tables. */
	list_for_each_entry(page, &mm->context.pgtable_list, lru)
		page->flags &= ~SECOND_HALVES;
	spin_unlock(&mm->page_table_lock);
	mm->context.noexec = 0;
	update_mm(mm, tsk);
}
