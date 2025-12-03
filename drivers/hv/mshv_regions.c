// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025, Microsoft Corporation.
 *
 * Memory region management for mshv_root module.
 *
 * Authors: Microsoft Linux virtualization team
 */

#include <linux/mm.h>
#include <linux/vmalloc.h>

#include <asm/mshyperv.h>

#include "mshv_root.h"

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

	/* Note: large_pages flag populated when we pin the pages */
	if (!is_mmio)
		region->flags.range_pinned = true;

	return region;
}

int mshv_region_share(struct mshv_mem_region *region)
{
	u32 flags = HV_MODIFY_SPA_PAGE_HOST_ACCESS_MAKE_SHARED;

	if (region->flags.large_pages)
		flags |= HV_MODIFY_SPA_PAGE_HOST_ACCESS_LARGE_PAGE;

	return hv_call_modify_spa_host_access(region->partition->pt_id,
			region->pages, region->nr_pages,
			HV_MAP_GPA_READABLE | HV_MAP_GPA_WRITABLE,
			flags, true);
}

int mshv_region_unshare(struct mshv_mem_region *region)
{
	u32 flags = HV_MODIFY_SPA_PAGE_HOST_ACCESS_MAKE_EXCLUSIVE;

	if (region->flags.large_pages)
		flags |= HV_MODIFY_SPA_PAGE_HOST_ACCESS_LARGE_PAGE;

	return hv_call_modify_spa_host_access(region->partition->pt_id,
			region->pages, region->nr_pages,
			0,
			flags, false);
}

static int mshv_region_remap_pages(struct mshv_mem_region *region,
				   u32 map_flags,
				   u64 page_offset, u64 page_count)
{
	if (page_offset + page_count > region->nr_pages)
		return -EINVAL;

	if (region->flags.large_pages)
		map_flags |= HV_MAP_GPA_LARGE_PAGE;

	return hv_call_map_gpa_pages(region->partition->pt_id,
				     region->start_gfn + page_offset,
				     page_count, map_flags,
				     region->pages + page_offset);
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

	if (PageHuge(region->pages[0]))
		region->flags.large_pages = true;

	return 0;

release_pages:
	mshv_region_invalidate_pages(region, 0, done_count);
	return ret;
}

void mshv_region_destroy(struct mshv_mem_region *region)
{
	struct mshv_partition *partition = region->partition;
	u32 unmap_flags = 0;
	int ret;

	hlist_del(&region->hnode);

	if (mshv_partition_encrypted(partition)) {
		ret = mshv_region_share(region);
		if (ret) {
			pt_err(partition,
			       "Failed to regain access to memory, unpinning user pages will fail and crash the host error: %d\n",
			       ret);
			return;
		}
	}

	if (region->flags.large_pages)
		unmap_flags |= HV_UNMAP_GPA_LARGE_PAGE;

	/* ignore unmap failures and continue as process may be exiting */
	hv_call_unmap_gpa_pages(partition->pt_id, region->start_gfn,
				region->nr_pages, unmap_flags);

	mshv_region_invalidate(region);

	vfree(region);
}
