// SPDX-License-Identifier: GPL-2.0

/*
 *  Handling Page Tables through page fragments
 *
 */

#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/percpu.h>
#include <linux/hardirq.h>
#include <linux/hugetlb.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>
#include <asm/tlb.h>

void pte_frag_destroy(void *pte_frag)
{
	int count;
	struct page *page;

	page = virt_to_page(pte_frag);
	/* drop all the pending references */
	count = ((unsigned long)pte_frag & ~PAGE_MASK) >> PTE_FRAG_SIZE_SHIFT;
	/* We allow PTE_FRAG_NR fragments from a PTE page */
	if (atomic_sub_and_test(PTE_FRAG_NR - count, &page->pt_frag_refcount)) {
		pgtable_page_dtor(page);
		__free_page(page);
	}
}

static pte_t *get_pte_from_cache(struct mm_struct *mm)
{
	void *pte_frag, *ret;

	if (PTE_FRAG_NR == 1)
		return NULL;

	spin_lock(&mm->page_table_lock);
	ret = mm->context.pte_frag;
	if (ret) {
		pte_frag = ret + PTE_FRAG_SIZE;
		/*
		 * If we have taken up all the fragments mark PTE page NULL
		 */
		if (((unsigned long)pte_frag & ~PAGE_MASK) == 0)
			pte_frag = NULL;
		mm->context.pte_frag = pte_frag;
	}
	spin_unlock(&mm->page_table_lock);
	return (pte_t *)ret;
}

static pte_t *__alloc_for_ptecache(struct mm_struct *mm, int kernel)
{
	void *ret = NULL;
	struct page *page;

	if (!kernel) {
		page = alloc_page(PGALLOC_GFP | __GFP_ACCOUNT);
		if (!page)
			return NULL;
		if (!pgtable_page_ctor(page)) {
			__free_page(page);
			return NULL;
		}
	} else {
		page = alloc_page(PGALLOC_GFP);
		if (!page)
			return NULL;
	}

	atomic_set(&page->pt_frag_refcount, 1);

	ret = page_address(page);
	/*
	 * if we support only one fragment just return the
	 * allocated page.
	 */
	if (PTE_FRAG_NR == 1)
		return ret;
	spin_lock(&mm->page_table_lock);
	/*
	 * If we find pgtable_page set, we return
	 * the allocated page with single fragement
	 * count.
	 */
	if (likely(!mm->context.pte_frag)) {
		atomic_set(&page->pt_frag_refcount, PTE_FRAG_NR);
		mm->context.pte_frag = ret + PTE_FRAG_SIZE;
	}
	spin_unlock(&mm->page_table_lock);

	return (pte_t *)ret;
}

pte_t *pte_fragment_alloc(struct mm_struct *mm, unsigned long vmaddr, int kernel)
{
	pte_t *pte;

	pte = get_pte_from_cache(mm);
	if (pte)
		return pte;

	return __alloc_for_ptecache(mm, kernel);
}

void pte_fragment_free(unsigned long *table, int kernel)
{
	struct page *page = virt_to_page(table);

	BUG_ON(atomic_read(&page->pt_frag_refcount) <= 0);
	if (atomic_dec_and_test(&page->pt_frag_refcount)) {
		if (!kernel)
			pgtable_page_dtor(page);
		__free_page(page);
	}
}
