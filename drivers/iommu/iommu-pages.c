// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, Google LLC.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
 */
#include "iommu-pages.h"
#include <linux/gfp.h>
#include <linux/mm.h>

/**
 * iommu_alloc_pages_node - Allocate a zeroed page of a given order from
 *                          specific NUMA node
 * @nid: memory NUMA node id
 * @gfp: buddy allocator flags
 * @order: page order
 *
 * Returns the virtual address of the allocated page. The page must be
 * freed either by calling iommu_free_pages() or via iommu_put_pages_list().
 */
void *iommu_alloc_pages_node(int nid, gfp_t gfp, unsigned int order)
{
	const unsigned long pgcnt = 1UL << order;
	struct page *page;

	page = alloc_pages_node(nid, gfp | __GFP_ZERO | __GFP_COMP, order);
	if (unlikely(!page))
		return NULL;

	/*
	 * All page allocations that should be reported to as "iommu-pagetables"
	 * to userspace must use one of the functions below. This includes
	 * allocations of page-tables and other per-iommu_domain configuration
	 * structures.
	 *
	 * This is necessary for the proper accounting as IOMMU state can be
	 * rather large, i.e. multiple gigabytes in size.
	 */
	mod_node_page_state(page_pgdat(page), NR_IOMMU_PAGES, pgcnt);
	mod_lruvec_page_state(page, NR_SECONDARY_PAGETABLE, pgcnt);

	return page_address(page);
}
EXPORT_SYMBOL_GPL(iommu_alloc_pages_node);

static void __iommu_free_page(struct page *page)
{
	unsigned int order = folio_order(page_folio(page));
	const unsigned long pgcnt = 1UL << order;

	mod_node_page_state(page_pgdat(page), NR_IOMMU_PAGES, -pgcnt);
	mod_lruvec_page_state(page, NR_SECONDARY_PAGETABLE, -pgcnt);
	put_page(page);
}

/**
 * iommu_free_pages - free pages
 * @virt: virtual address of the page to be freed.
 *
 * The page must have have been allocated by iommu_alloc_pages_node()
 */
void iommu_free_pages(void *virt)
{
	if (!virt)
		return;
	__iommu_free_page(virt_to_page(virt));
}
EXPORT_SYMBOL_GPL(iommu_free_pages);

/**
 * iommu_put_pages_list - free a list of pages.
 * @head: the head of the lru list to be freed.
 *
 * Frees a list of pages allocated by iommu_alloc_pages_node().
 */
void iommu_put_pages_list(struct list_head *head)
{
	while (!list_empty(head)) {
		struct page *p = list_entry(head->prev, struct page, lru);

		list_del(&p->lru);
		__iommu_free_page(p);
	}
}
EXPORT_SYMBOL_GPL(iommu_put_pages_list);
