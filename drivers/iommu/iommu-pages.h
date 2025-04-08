/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024, Google LLC.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
 */

#ifndef __IOMMU_PAGES_H
#define __IOMMU_PAGES_H

#include <linux/iommu.h>

/**
 * struct ioptdesc - Memory descriptor for IOMMU page tables
 * @iopt_freelist_elm: List element for a struct iommu_pages_list
 *
 * This struct overlays struct page for now. Do not modify without a good
 * understanding of the issues.
 */
struct ioptdesc {
	unsigned long __page_flags;

	struct list_head iopt_freelist_elm;
	unsigned long __page_mapping;
	pgoff_t __index;
	void *_private;

	unsigned int __page_type;
	atomic_t __page_refcount;
#ifdef CONFIG_MEMCG
	unsigned long memcg_data;
#endif
};

static inline struct ioptdesc *folio_ioptdesc(struct folio *folio)
{
	return (struct ioptdesc *)folio;
}

static inline struct folio *ioptdesc_folio(struct ioptdesc *iopt)
{
	return (struct folio *)iopt;
}

static inline struct ioptdesc *virt_to_ioptdesc(void *virt)
{
	return folio_ioptdesc(virt_to_folio(virt));
}

void *iommu_alloc_pages_node(int nid, gfp_t gfp, unsigned int order);
void iommu_free_pages(void *virt);
void iommu_put_pages_list(struct iommu_pages_list *list);

/**
 * iommu_pages_list_add - add the page to a iommu_pages_list
 * @list: List to add the page to
 * @virt: Address returned from iommu_alloc_pages_node()
 */
static inline void iommu_pages_list_add(struct iommu_pages_list *list,
					void *virt)
{
	list_add_tail(&virt_to_ioptdesc(virt)->iopt_freelist_elm, &list->pages);
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
	return iommu_alloc_pages_node(NUMA_NO_NODE, gfp, order);
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
	return iommu_alloc_pages_node(NUMA_NO_NODE, gfp, 0);
}

#endif	/* __IOMMU_PAGES_H */
