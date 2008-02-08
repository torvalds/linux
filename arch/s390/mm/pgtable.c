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

#ifndef CONFIG_64BIT
#define ALLOC_ORDER	1
#else
#define ALLOC_ORDER	2
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
	return (unsigned long *) page_to_phys(page);
}

void crst_table_free(unsigned long *table)
{
	unsigned long *shadow = get_shadow_table(table);

	if (shadow)
		free_pages((unsigned long) shadow, ALLOC_ORDER);
	free_pages((unsigned long) table, ALLOC_ORDER);
}

/*
 * page table entry allocation/free routines.
 */
unsigned long *page_table_alloc(int noexec)
{
	struct page *page = alloc_page(GFP_KERNEL);
	unsigned long *table;

	if (!page)
		return NULL;
	page->index = 0;
	if (noexec) {
		struct page *shadow = alloc_page(GFP_KERNEL);
		if (!shadow) {
			__free_page(page);
			return NULL;
		}
		table = (unsigned long *) page_to_phys(shadow);
		clear_table(table, _PAGE_TYPE_EMPTY, PAGE_SIZE);
		page->index = (addr_t) table;
	}
	pgtable_page_ctor(page);
	table = (unsigned long *) page_to_phys(page);
	clear_table(table, _PAGE_TYPE_EMPTY, PAGE_SIZE);
	return table;
}

void page_table_free(unsigned long *table)
{
	unsigned long *shadow = get_shadow_pte(table);

	pgtable_page_dtor(virt_to_page(table));
	if (shadow)
		free_page((unsigned long) shadow);
	free_page((unsigned long) table);

}
