/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024, Google LLC.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
 */

#ifndef __IOMMU_PAGES_H
#define __IOMMU_PAGES_H

#include <linux/iommu.h>

void *iommu_alloc_pages_node(int nid, gfp_t gfp, unsigned int order);
void iommu_free_pages(void *virt);
void iommu_put_pages_list_new(struct iommu_pages_list *list);
void iommu_put_pages_list_old(struct list_head *head);

#define iommu_put_pages_list(head)                                   \
	_Generic(head,                                               \
		struct iommu_pages_list *: iommu_put_pages_list_new, \
		struct list_head *: iommu_put_pages_list_old)(head)

/**
 * iommu_pages_list_add - add the page to a iommu_pages_list
 * @list: List to add the page to
 * @virt: Address returned from iommu_alloc_pages_node()
 */
static inline void iommu_pages_list_add(struct iommu_pages_list *list,
					void *virt)
{
	list_add_tail(&virt_to_page(virt)->lru, &list->pages);
}

/**
 * iommu_pages_list_splice - Put all the pages in list from into list to
 * @from: Source list of pages
 * @to: Destination list of pages
 *
 * from must be re-initialized after calling this function if it is to be
 * used again.
 */
static inline void iommu_pages_list_splice(struct iommu_pages_list *from,
					   struct iommu_pages_list *to)
{
	list_splice(&from->pages, &to->pages);
}

/**
 * iommu_pages_list_empty - True if the list is empty
 * @list: List to check
 */
static inline bool iommu_pages_list_empty(struct iommu_pages_list *list)
{
	return list_empty(&list->pages);
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
	return iommu_alloc_pages_node(numa_node_id(), gfp, order);
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
	return iommu_alloc_pages_node(numa_node_id(), gfp, 0);
}

#endif	/* __IOMMU_PAGES_H */
