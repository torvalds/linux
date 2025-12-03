// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025, Microsoft Corporation.
 *
 * Memory region management for mshv_root module.
 *
 * Authors: Microsoft Linux virtualization team
 */

#include <linux/kref.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>

#include <asm/mshyperv.h>

#include "mshv_root.h"

/**
 * mshv_region_process_chunk - Processes a contiguous chunk of memory pages
 *                             in a region.
 * @region     : Pointer to the memory region structure.
 * @flags      : Flags to pass to the handler.
 * @page_offset: Offset into the region's pages array to start processing.
 * @page_count : Number of pages to process.
 * @handler    : Callback function to handle the chunk.
 *
 * This function scans the region's pages starting from @page_offset,
 * checking for contiguous present pages of the same size (normal or huge).
 * It invokes @handler for the chunk of contiguous pages found. Returns the
 * number of pages handled, or a negative error code if the first page is
 * not present or the handler fails.
 *
 * Note: The @handler callback must be able to handle both normal and huge
 * pages.
 *
 * Return: Number of pages handled, or negative error code.
 */
static long mshv_region_process_chunk(struct mshv_mem_region *region,
				      u32 flags,
				      u64 page_offset, u64 page_count,
				      int (*handler)(struct mshv_mem_region *region,
						     u32 flags,
						     u64 page_offset,
						     u64 page_count))
{
	u64 count, stride;
	unsigned int page_order;
	struct page *page;
	int ret;

	page = region->pages[page_offset];
	if (!page)
		return -EINVAL;

	page_order = folio_order(page_folio(page));
	/* The hypervisor only supports 4K and 2M page sizes */
	if (page_order && page_order != HPAGE_PMD_ORDER)
		return -EINVAL;

	stride = 1 << page_order;

	/* Start at stride since the first page is validated */
	for (count = stride; count < page_count; count += stride) {
		page = region->pages[page_offset + count];

		/* Break if current page is not present */
		if (!page)
			break;

		/* Break if page size changes */
		if (page_order != folio_order(page_folio(page)))
			break;
	}

	ret = handler(region, flags, page_offset, count);
	if (ret)
		return ret;

	return count;
}

/**
 * mshv_region_process_range - Processes a range of memory pages in a
 *                             region.
 * @region     : Pointer to the memory region structure.
 * @flags      : Flags to pass to the handler.
 * @page_offset: Offset into the region's pages array to start processing.
 * @page_count : Number of pages to process.
 * @handler    : Callback function to handle each chunk of contiguous
 *               pages.
 *
 * Iterates over the specified range of pages in @region, skipping
 * non-present pages. For each contiguous chunk of present pages, invokes
 * @handler via mshv_region_process_chunk.
 *
 * Note: The @handler callback must be able to handle both normal and huge
 * pages.
 *
 * Returns 0 on success, or a negative error code on failure.
 */
static int mshv_region_process_range(struct mshv_mem_region *region,
				     u32 flags,
				     u64 page_offset, u64 page_count,
				     int (*handler)(struct mshv_mem_region *region,
						    u32 flags,
						    u64 page_offset,
						    u64 page_count))
{
	long ret;

	if (page_offset + page_count > region->nr_pages)
		return -EINVAL;

	while (page_count) {
		/* Skip non-present pages */
		if (!region->pages[page_offset]) {
			page_offset++;
			page_count--;
			continue;
		}

		ret = mshv_region_process_chunk(region, flags,
						page_offset,
						page_count,
						handler);
		if (ret < 0)
			return ret;

		page_offset += ret;
		page_count -= ret;
	}

	return 0;
}

struct mshv_mem_region *mshv_region_create(u64 guest_pfn, u64 nr_pages,
					   u64 uaddr, u32 flags,
					   bool is_mmio)
{
	struct mshv_mem_region *region;

	region = vzalloc(sizeof(*region) + sizeof(struct page *) * nr_pages);
	if (!region)
		return ERR_PTR(-ENOMEM);

	region->nr_pages = nr_pages;
	region->start_gfn = guest_pfn;
	region->start_uaddr = uaddr;
	region->hv_map_flags = HV_MAP_GPA_READABLE | HV_MAP_GPA_ADJUSTABLE;
	if (flags & BIT(MSHV_SET_MEM_BIT_WRITABLE))
		region->hv_map_flags |= HV_MAP_GPA_WRITABLE;
	if (flags & BIT(MSHV_SET_MEM_BIT_EXECUTABLE))
		region->hv_map_flags |= HV_MAP_GPA_EXECUTABLE;

	if (!is_mmio)
		region->flags.range_pinned = true;

	kref_init(&region->refcount);

	return region;
}

static int mshv_region_chunk_share(struct mshv_mem_region *region,
				   u32 flags,
				   u64 page_offset, u64 page_count)
{
	struct page *page = region->pages[page_offset];

	if (PageHuge(page) || PageTransCompound(page))
		flags |= HV_MODIFY_SPA_PAGE_HOST_ACCESS_LARGE_PAGE;

	return hv_call_modify_spa_host_access(region->partition->pt_id,
					      region->pages + page_offset,
					      page_count,
					      HV_MAP_GPA_READABLE |
					      HV_MAP_GPA_WRITABLE,
					      flags, true);
}

int mshv_region_share(struct mshv_mem_region *region)
{
	u32 flags = HV_MODIFY_SPA_PAGE_HOST_ACCESS_MAKE_SHARED;

	return mshv_region_process_range(region, flags,
					 0, region->nr_pages,
					 mshv_region_chunk_share);
}

static int mshv_region_chunk_unshare(struct mshv_mem_region *region,
				     u32 flags,
				     u64 page_offset, u64 page_count)
{
	struct page *page = region->pages[page_offset];

	if (PageHuge(page) || PageTransCompound(page))
		flags |= HV_MODIFY_SPA_PAGE_HOST_ACCESS_LARGE_PAGE;

	return hv_call_modify_spa_host_access(region->partition->pt_id,
					      region->pages + page_offset,
					      page_count, 0,
					      flags, false);
}

int mshv_region_unshare(struct mshv_mem_region *region)
{
	u32 flags = HV_MODIFY_SPA_PAGE_HOST_ACCESS_MAKE_EXCLUSIVE;

	return mshv_region_process_range(region, flags,
					 0, region->nr_pages,
					 mshv_region_chunk_unshare);
}

static int mshv_region_chunk_remap(struct mshv_mem_region *region,
				   u32 flags,
				   u64 page_offset, u64 page_count)
{
	struct page *page = region->pages[page_offset];

	if (PageHuge(page) || PageTransCompound(page))
		flags |= HV_MAP_GPA_LARGE_PAGE;

	return hv_call_map_gpa_pages(region->partition->pt_id,
				     region->start_gfn + page_offset,
				     page_count, flags,
				     region->pages + page_offset);
}

static int mshv_region_remap_pages(struct mshv_mem_region *region,
				   u32 map_flags,
				   u64 page_offset, u64 page_count)
{
	return mshv_region_process_range(region, map_flags,
					 page_offset, page_count,
					 mshv_region_chunk_remap);
}

int mshv_region_map(struct mshv_mem_region *region)
{
	u32 map_flags = region->hv_map_flags;

	return mshv_region_remap_pages(region, map_flags,
				       0, region->nr_pages);
}

static void mshv_region_invalidate_pages(struct mshv_mem_region *region,
					 u64 page_offset, u64 page_count)
{
	if (region->flags.range_pinned)
		unpin_user_pages(region->pages + page_offset, page_count);

	memset(region->pages + page_offset, 0,
	       page_count * sizeof(struct page *));
}

void mshv_region_invalidate(struct mshv_mem_region *region)
{
	mshv_region_invalidate_pages(region, 0, region->nr_pages);
}

int mshv_region_pin(struct mshv_mem_region *region)
{
	u64 done_count, nr_pages;
	struct page **pages;
	__u64 userspace_addr;
	int ret;

	for (done_count = 0; done_count < region->nr_pages; done_count += ret) {
		pages = region->pages + done_count;
		userspace_addr = region->start_uaddr +
				 done_count * HV_HYP_PAGE_SIZE;
		nr_pages = min(region->nr_pages - done_count,
			       MSHV_PIN_PAGES_BATCH_SIZE);

		/*
		 * Pinning assuming 4k pages works for large pages too.
		 * All page structs within the large page are returned.
		 *
		 * Pin requests are batched because pin_user_pages_fast
		 * with the FOLL_LONGTERM flag does a large temporary
		 * allocation of contiguous memory.
		 */
		ret = pin_user_pages_fast(userspace_addr, nr_pages,
					  FOLL_WRITE | FOLL_LONGTERM,
					  pages);
		if (ret < 0)
			goto release_pages;
	}

	return 0;

release_pages:
	mshv_region_invalidate_pages(region, 0, done_count);
	return ret;
}

static int mshv_region_chunk_unmap(struct mshv_mem_region *region,
				   u32 flags,
				   u64 page_offset, u64 page_count)
{
	struct page *page = region->pages[page_offset];

	if (PageHuge(page) || PageTransCompound(page))
		flags |= HV_UNMAP_GPA_LARGE_PAGE;

	return hv_call_unmap_gpa_pages(region->partition->pt_id,
				       region->start_gfn + page_offset,
				       page_count, flags);
}

static int mshv_region_unmap(struct mshv_mem_region *region)
{
	return mshv_region_process_range(region, 0,
					 0, region->nr_pages,
					 mshv_region_chunk_unmap);
}

static void mshv_region_destroy(struct kref *ref)
{
	struct mshv_mem_region *region =
		container_of(ref, struct mshv_mem_region, refcount);
	struct mshv_partition *partition = region->partition;
	int ret;

	if (mshv_partition_encrypted(partition)) {
		ret = mshv_region_share(region);
		if (ret) {
			pt_err(partition,
			       "Failed to regain access to memory, unpinning user pages will fail and crash the host error: %d\n",
			       ret);
			return;
		}
	}

	mshv_region_unmap(region);

	mshv_region_invalidate(region);

	vfree(region);
}

void mshv_region_put(struct mshv_mem_region *region)
{
	kref_put(&region->refcount, mshv_region_destroy);
}

int mshv_region_get(struct mshv_mem_region *region)
{
	return kref_get_unless_zero(&region->refcount);
}
