/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024, Google LLC.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
 */

#ifndef __IOMMU_PAGES_H
#define __IOMMU_PAGES_H

#include <linux/vmstat.h>
#include <linux/gfp.h>
#include <linux/mm.h>

/*
 * All page allocations that should be reported to as "iommu-pagetables" to
 * userspace must use one of the functions below.  This includes allocations of
 * page-tables and other per-iommu_domain configuration structures.
 *
 * This is necessary for the proper accounting as IOMMU state can be rather
 * large, i.e. multiple gigabytes in size.
 */

/**
 * __iommu_alloc_account - account for newly allocated page.
 * @page: head struct page of the page.
 * @order: order of the page
 */
static inline void __iommu_alloc_account(struct page *page, int order)
{
	const long pgcnt = 1l << order;

	mod_node_page_state(page_pgdat(page), NR_IOMMU_PAGES, pgcnt);
	mod_lruvec_page_state(page, NR_SECONDARY_PAGETABLE, pgcnt);
}

/**
 * __iommu_free_account - account a page that is about to be freed.
 * @page: head struct page of the page.
 * @order: order of the page
 */
static inline void __iommu_free_account(struct page *page, int order)
{
	const long pgcnt = 1l << order;

	mod_node_page_state(page_pgdat(page), NR_IOMMU_PAGES, -pgcnt);
	mod_lruvec_page_state(page, NR_SECONDARY_PAGETABLE, -pgcnt);
}

/**
 * __iommu_alloc_pages - allocate a zeroed page of a given order.
 * @gfp: buddy allocator flags
 * @order: page order
 *
 * returns the head struct page of the allocated page.
 */
static inline struct page *__iommu_alloc_pages(gfp_t gfp, int order)
{
	struct page *page;

	page = alloc_pages(gfp | __GFP_ZERO, order);
	if (unlikely(!page))
		return NULL;

	__iommu_alloc_account(page, order);

	return page;
}

/**
 * __iommu_free_pages - free page of a given order
 * @page: head struct page of the page
 * @order: page order
 */
static inline void __iommu_free_pages(struct page *page, int order)
{
	if (!page)
		return;

	__iommu_free_account(page, order);
	__free_pages(page, order);
}

/**
 * iommu_alloc_pages_node - allocate a zeroed page of a given order from
 * specific NUMA node.
 * @nid: memory NUMA node id
 * @gfp: buddy allocator flags
 * @order: page order
 *
 * returns the virtual address of the allocated page
 */
static inline void *iommu_alloc_pages_node(int nid, gfp_t gfp, int order)
{
	struct page *page = alloc_pages_node(nid, gfp | __GFP_ZERO, order);

	if (unlikely(!page))
		return NULL;

	__iommu_alloc_account(page, order);

	return page_address(page);
}

/**
 * iommu_alloc_pages - allocate a zeroed page of a given order
 * @gfp: buddy allocator flags
 * @order: page order
 *
 * returns the virtual address of the allocated page
 */
static inline void *iommu_alloc_pages(gfp_t gfp, int order)
{
	struct page *page = __iommu_alloc_pages(gfp, order);

	if (unlikely(!page))
		return NULL;

	return page_address(page);
}

/**
 * iommu_alloc_page_node - allocate a zeroed page at specific NUMA node.
 * @nid: memory NUMA node id
 * @gfp: buddy allocator flags
 *
 * returns the virtual address of the allocated page
 */
static inline void *iommu_alloc_page_node(int nid, gfp_t gfp)
{
	return iommu_alloc_pages_node(nid, gfp, 0);
}

/**
 * iommu_alloc_page - allocate a zeroed page
 * @gfp: buddy allocator flags
 *
 * returns the virtual address of the allocated page
 */
static inline void *iommu_alloc_page(gfp_t gfp)
{
	return iommu_alloc_pages(gfp, 0);
}

/**
 * iommu_free_pages - free page of a given order
 * @virt: virtual address of the page to be freed.
 * @order: page order
 */
static inline void iommu_free_pages(void *virt, int order)
{
	if (!virt)
		return;

	__iommu_free_pages(virt_to_page(virt), order);
}

/**
 * iommu_free_page - free page
 * @virt: virtual address of the page to be freed.
 */
static inline void iommu_free_page(void *virt)
{
	iommu_free_pages(virt, 0);
}

/**
 * iommu_put_pages_list - free a list of pages.
 * @page: the head of the lru list to be freed.
 *
 * There are no locking requirement for these pages, as they are going to be
 * put on a free list as soon as refcount reaches 0. Pages are put on this LRU
 * list once they are removed from the IOMMU page tables. However, they can
 * still be access through debugfs.
 */
static inline void iommu_put_pages_list(struct list_head *page)
{
	while (!list_empty(page)) {
		struct page *p = list_entry(page->prev, struct page, lru);

		list_del(&p->lru);
		__iommu_free_account(p, 0);
		put_page(p);
	}
}

#endif	/* __IOMMU_PAGES_H */
