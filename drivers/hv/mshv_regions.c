// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025, Microsoft Corporation.
 *
 * Memory region management for mshv_root module.
 *
 * Authors: Microsoft Linux virtualization team
 */

#include <linux/hmm.h>
#include <linux/hyperv.h>
#include <linux/kref.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>

#include <asm/mshyperv.h>

#include "mshv_root.h"

#define MSHV_MAP_FAULT_IN_PAGES				PTRS_PER_PMD

/**
 * mshv_chunk_stride - Compute stride for mapping guest memory
 * @page      : The page to check for huge page backing
 * @gfn       : Guest frame number for the mapping
 * @page_count: Total number of pages in the mapping
 *
 * Determines the appropriate stride (in pages) for mapping guest memory.
 * Uses huge page stride if the backing page is huge and the guest mapping
 * is properly aligned; otherwise falls back to single page stride.
 *
 * Return: Stride in pages, or -EINVAL if page order is unsupported.
 */
static int mshv_chunk_stride(struct page *page,
			     u64 gfn, u64 page_count)
{
	unsigned int page_order;

	/*
	 * Use single page stride by default. For huge page stride, the
	 * page must be compound and point to the head of the compound
	 * page, and both gfn and page_count must be huge-page aligned.
	 */
	if (!PageCompound(page) || !PageHead(page) ||
	    !IS_ALIGNED(gfn, PTRS_PER_PMD) ||
	    !IS_ALIGNED(page_count, PTRS_PER_PMD))
		return 1;

	page_order = folio_order(page_folio(page));
	/* The hypervisor only supports 2M huge page */
	if (page_order != PMD_ORDER)
		return -EINVAL;

	return 1 << page_order;
}

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
						     u64 page_count,
						     bool huge_page))
{
	u64 gfn = region->start_gfn + page_offset;
	u64 count;
	struct page *page;
	int stride, ret;

	page = region->pages[page_offset];
	if (!page)
		return -EINVAL;

	stride = mshv_chunk_stride(page, gfn, page_count);
	if (stride < 0)
		return stride;

	/* Start at stride since the first stride is validated */
	for (count = stride; count < page_count; count += stride) {
		page = region->pages[page_offset + count];

		/* Break if current page is not present */
		if (!page)
			break;

		/* Break if stride size changes */
		if (stride != mshv_chunk_stride(page, gfn + count,
						page_count - count))
			break;
	}

	ret = handler(region, flags, page_offset, count, stride > 1);
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
						    u64 page_count,
						    bool huge_page))
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
					   u64 uaddr, u32 flags)
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

	kref_init(&region->refcount);

	return region;
}

static int mshv_region_chunk_share(struct mshv_mem_region *region,
				   u32 flags,
				   u64 page_offset, u64 page_count,
				   bool huge_page)
{
	if (huge_page)
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
				     u64 page_offset, u64 page_count,
				     bool huge_page)
{
	if (huge_page)
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
				   u64 page_offset, u64 page_count,
				   bool huge_page)
{
	if (huge_page)
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
	if (region->type == MSHV_REGION_TYPE_MEM_PINNED)
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
				   u64 page_offset, u64 page_count,
				   bool huge_page)
{
	if (huge_page)
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

	if (region->type == MSHV_REGION_TYPE_MEM_MOVABLE)
		mshv_region_movable_fini(region);

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

/**
 * mshv_region_hmm_fault_and_lock - Handle HMM faults and lock the memory region
 * @region: Pointer to the memory region structure
 * @range: Pointer to the HMM range structure
 *
 * This function performs the following steps:
 * 1. Reads the notifier sequence for the HMM range.
 * 2. Acquires a read lock on the memory map.
 * 3. Handles HMM faults for the specified range.
 * 4. Releases the read lock on the memory map.
 * 5. If successful, locks the memory region mutex.
 * 6. Verifies if the notifier sequence has changed during the operation.
 *    If it has, releases the mutex and returns -EBUSY to match with
 *    hmm_range_fault() return code for repeating.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
static int mshv_region_hmm_fault_and_lock(struct mshv_mem_region *region,
					  struct hmm_range *range)
{
	int ret;

	range->notifier_seq = mmu_interval_read_begin(range->notifier);
	mmap_read_lock(region->mni.mm);
	ret = hmm_range_fault(range);
	mmap_read_unlock(region->mni.mm);
	if (ret)
		return ret;

	mutex_lock(&region->mutex);

	if (mmu_interval_read_retry(range->notifier, range->notifier_seq)) {
		mutex_unlock(&region->mutex);
		cond_resched();
		return -EBUSY;
	}

	return 0;
}

/**
 * mshv_region_range_fault - Handle memory range faults for a given region.
 * @region: Pointer to the memory region structure.
 * @page_offset: Offset of the page within the region.
 * @page_count: Number of pages to handle.
 *
 * This function resolves memory faults for a specified range of pages
 * within a memory region. It uses HMM (Heterogeneous Memory Management)
 * to fault in the required pages and updates the region's page array.
 *
 * Return: 0 on success, negative error code on failure.
 */
static int mshv_region_range_fault(struct mshv_mem_region *region,
				   u64 page_offset, u64 page_count)
{
	struct hmm_range range = {
		.notifier = &region->mni,
		.default_flags = HMM_PFN_REQ_FAULT | HMM_PFN_REQ_WRITE,
	};
	unsigned long *pfns;
	int ret;
	u64 i;

	pfns = kmalloc_array(page_count, sizeof(*pfns), GFP_KERNEL);
	if (!pfns)
		return -ENOMEM;

	range.hmm_pfns = pfns;
	range.start = region->start_uaddr + page_offset * HV_HYP_PAGE_SIZE;
	range.end = range.start + page_count * HV_HYP_PAGE_SIZE;

	do {
		ret = mshv_region_hmm_fault_and_lock(region, &range);
	} while (ret == -EBUSY);

	if (ret)
		goto out;

	for (i = 0; i < page_count; i++)
		region->pages[page_offset + i] = hmm_pfn_to_page(pfns[i]);

	ret = mshv_region_remap_pages(region, region->hv_map_flags,
				      page_offset, page_count);

	mutex_unlock(&region->mutex);
out:
	kfree(pfns);
	return ret;
}

bool mshv_region_handle_gfn_fault(struct mshv_mem_region *region, u64 gfn)
{
	u64 page_offset, page_count;
	int ret;

	/* Align the page offset to the nearest MSHV_MAP_FAULT_IN_PAGES. */
	page_offset = ALIGN_DOWN(gfn - region->start_gfn,
				 MSHV_MAP_FAULT_IN_PAGES);

	/* Map more pages than requested to reduce the number of faults. */
	page_count = min(region->nr_pages - page_offset,
			 MSHV_MAP_FAULT_IN_PAGES);

	ret = mshv_region_range_fault(region, page_offset, page_count);

	WARN_ONCE(ret,
		  "p%llu: GPA intercept failed: region %#llx-%#llx, gfn %#llx, page_offset %llu, page_count %llu\n",
		  region->partition->pt_id, region->start_uaddr,
		  region->start_uaddr + (region->nr_pages << HV_HYP_PAGE_SHIFT),
		  gfn, page_offset, page_count);

	return !ret;
}

/**
 * mshv_region_interval_invalidate - Invalidate a range of memory region
 * @mni: Pointer to the mmu_interval_notifier structure
 * @range: Pointer to the mmu_notifier_range structure
 * @cur_seq: Current sequence number for the interval notifier
 *
 * This function invalidates a memory region by remapping its pages with
 * no access permissions. It locks the region's mutex to ensure thread safety
 * and updates the sequence number for the interval notifier. If the range
 * is blockable, it uses a blocking lock; otherwise, it attempts a non-blocking
 * lock and returns false if unsuccessful.
 *
 * NOTE: Failure to invalidate a region is a serious error, as the pages will
 * be considered freed while they are still mapped by the hypervisor.
 * Any attempt to access such pages will likely crash the system.
 *
 * Return: true if the region was successfully invalidated, false otherwise.
 */
static bool mshv_region_interval_invalidate(struct mmu_interval_notifier *mni,
					    const struct mmu_notifier_range *range,
					    unsigned long cur_seq)
{
	struct mshv_mem_region *region = container_of(mni,
						      struct mshv_mem_region,
						      mni);
	u64 page_offset, page_count;
	unsigned long mstart, mend;
	int ret = -EPERM;

	mstart = max(range->start, region->start_uaddr);
	mend = min(range->end, region->start_uaddr +
		   (region->nr_pages << HV_HYP_PAGE_SHIFT));

	page_offset = HVPFN_DOWN(mstart - region->start_uaddr);
	page_count = HVPFN_DOWN(mend - mstart);

	if (mmu_notifier_range_blockable(range))
		mutex_lock(&region->mutex);
	else if (!mutex_trylock(&region->mutex))
		goto out_fail;

	mmu_interval_set_seq(mni, cur_seq);

	ret = mshv_region_remap_pages(region, HV_MAP_GPA_NO_ACCESS,
				      page_offset, page_count);
	if (ret)
		goto out_unlock;

	mshv_region_invalidate_pages(region, page_offset, page_count);

	mutex_unlock(&region->mutex);

	return true;

out_unlock:
	mutex_unlock(&region->mutex);
out_fail:
	WARN_ONCE(ret,
		  "Failed to invalidate region %#llx-%#llx (range %#lx-%#lx, event: %u, pages %#llx-%#llx, mm: %#llx): %d\n",
		  region->start_uaddr,
		  region->start_uaddr + (region->nr_pages << HV_HYP_PAGE_SHIFT),
		  range->start, range->end, range->event,
		  page_offset, page_offset + page_count - 1, (u64)range->mm, ret);
	return false;
}

static const struct mmu_interval_notifier_ops mshv_region_mni_ops = {
	.invalidate = mshv_region_interval_invalidate,
};

void mshv_region_movable_fini(struct mshv_mem_region *region)
{
	mmu_interval_notifier_remove(&region->mni);
}

bool mshv_region_movable_init(struct mshv_mem_region *region)
{
	int ret;

	ret = mmu_interval_notifier_insert(&region->mni, current->mm,
					   region->start_uaddr,
					   region->nr_pages << HV_HYP_PAGE_SHIFT,
					   &mshv_region_mni_ops);
	if (ret)
		return false;

	mutex_init(&region->mutex);

	return true;
}
