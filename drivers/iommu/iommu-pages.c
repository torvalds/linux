// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, Google LLC.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
 */
#include "iommu-pages.h"
#include <linux/gfp.h>
#include <linux/mm.h>

#define IOPTDESC_MATCH(pg_elm, elm)                    \
	static_assert(offsetof(struct page, pg_elm) == \
		      offsetof(struct ioptdesc, elm))
IOPTDESC_MATCH(flags, __page_flags);
IOPTDESC_MATCH(lru, iopt_freelist_elm); /* Ensure bit 0 is clear */
IOPTDESC_MATCH(mapping, __page_mapping);
IOPTDESC_MATCH(private, _private);
IOPTDESC_MATCH(page_type, __page_type);
IOPTDESC_MATCH(_refcount, __page_refcount);
#ifdef CONFIG_MEMCG
IOPTDESC_MATCH(memcg_data, memcg_data);
#endif
#undef IOPTDESC_MATCH
static_assert(sizeof(struct ioptdesc) <= sizeof(struct page));

/**
 * iommu_alloc_pages_node_sz - Allocate a zeroed page of a given size from
 *                             specific NUMA node
 * @nid: memory NUMA node id
 * @gfp: buddy allocator flags
 * @size: Memory size to allocate, rounded up to a power of 2
 *
 * Returns the virtual address of the allocated page. The page must be freed
 * either by calling iommu_free_pages() or via iommu_put_pages_list(). The
 * returned allocation is round_up_pow_two(size) big, and is physically aligned
 * to its size.
 */
void *iommu_alloc_pages_node_sz(int nid, gfp_t gfp, size_t size)
{
	unsigned long pgcnt;
	struct folio *folio;
	unsigned int order;

	/* This uses page_address() on the memory. */
	if (WARN_ON(gfp & __GFP_HIGHMEM))
		return NULL;

	/*
	 * Currently sub page allocations result in a full page being returned.
	 */
	order = get_order(size);

	/*
	 * __folio_alloc_node() does not handle NUMA_NO_NODE like
	 * alloc_pages_node() did.
	 */
	if (nid == NUMA_NO_NODE)
		nid = numa_mem_id();

	folio = __folio_alloc_node(gfp | __GFP_ZERO, order, nid);
	if (unlikely(!folio))
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
	pgcnt = 1UL << order;
	mod_node_page_state(folio_pgdat(folio), NR_IOMMU_PAGES, pgcnt);
	lruvec_stat_mod_folio(folio, NR_SECONDARY_PAGETABLE, pgcnt);

	return folio_address(folio);
}
EXPORT_SYMBOL_GPL(iommu_alloc_pages_node_sz);

static void __iommu_free_desc(struct ioptdesc *iopt)
{
	struct folio *folio = ioptdesc_folio(iopt);
	const unsigned long pgcnt = 1UL << folio_order(folio);

	mod_node_page_state(folio_pgdat(folio), NR_IOMMU_PAGES, -pgcnt);
	lruvec_stat_mod_folio(folio, NR_SECONDARY_PAGETABLE, -pgcnt);
	folio_put(folio);
}

/**
 * iommu_free_pages - free pages
 * @virt: virtual address of the page to be freed.
 *
 * The page must have have been allocated by iommu_alloc_pages_node_sz()
 */
void iommu_free_pages(void *virt)
{
	if (!virt)
		return;
	__iommu_free_desc(virt_to_ioptdesc(virt));
}
EXPORT_SYMBOL_GPL(iommu_free_pages);

/**
 * iommu_put_pages_list - free a list of pages.
 * @list: The list of pages to be freed
 *
 * Frees a list of pages allocated by iommu_alloc_pages_node_sz(). On return the
 * passed list is invalid, the caller must use IOMMU_PAGES_LIST_INIT to reinit
 * the list if it expects to use it again.
 */
void iommu_put_pages_list(struct iommu_pages_list *list)
{
	struct ioptdesc *iopt, *tmp;

	list_for_each_entry_safe(iopt, tmp, &list->pages, iopt_freelist_elm)
		__iommu_free_desc(iopt);
}
EXPORT_SYMBOL_GPL(iommu_put_pages_list);
