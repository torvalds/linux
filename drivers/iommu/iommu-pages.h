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

void *iommu_alloc_pages_node_sz(int nid, gfp_t gfp, size_t size);
void iommu_free_pages(void *virt);
void iommu_put_pages_list(struct iommu_pages_list *list);

/**
 * iommu_pages_list_add - add the page to a iommu_pages_list
 * @list: List to add the page to
 * @virt: Address returned from iommu_alloc_pages_node_sz()
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
 * iommu_alloc_pages_sz - Allocate a zeroed page of a given size from
 *                          specific NUMA node
 * @nid: memory NUMA node id
 * @gfp: buddy allocator flags
 * @size: Memory size to allocate, this is rounded up to a power of 2
 *
 * Returns the virtual address of the allocated page.
 */
static inline void *iommu_alloc_pages_sz(gfp_t gfp, size_t size)
{
	return iommu_alloc_pages_node_sz(NUMA_NO_NODE, gfp, size);
}

#endif	/* __IOMMU_PAGES_H */
