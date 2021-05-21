/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 */
/*
 * Authors:
 *    Christian König <christian.koenig@amd.com>
 */

/**
 * DOC: MMU Notifier
 *
 * For coherent userptr handling registers an MMU notifier to inform the driver
 * about updates on the page tables of a process.
 *
 * When somebody tries to invalidate the page tables we block the update until
 * all operations on the pages in question are completed, then those pages are
 * marked as accessed and also dirty if it wasn't a read only access.
 *
 * New command submissions using the userptrs in question are delayed until all
 * page table invalidation are completed and we once more see a coherent process
 * address space.
 */

#include <linux/firmware.h>
#include <linux/module.h>
#include <drm/drm.h>

#include "amdgpu.h"
#include "amdgpu_amdkfd.h"

/**
 * amdgpu_mn_invalidate_gfx - callback to notify about mm change
 *
 * @mni: the range (mm) is about to update
 * @range: details on the invalidation
 * @cur_seq: Value to pass to mmu_interval_set_seq()
 *
 * Block for operations on BOs to finish and mark pages as accessed and
 * potentially dirty.
 */
static bool amdgpu_mn_invalidate_gfx(struct mmu_interval_notifier *mni,
				     const struct mmu_notifier_range *range,
				     unsigned long cur_seq)
{
	struct amdgpu_bo *bo = container_of(mni, struct amdgpu_bo, notifier);
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);
	long r;

	if (!mmu_notifier_range_blockable(range))
		return false;

	mutex_lock(&adev->notifier_lock);

	mmu_interval_set_seq(mni, cur_seq);

	r = dma_resv_wait_timeout_rcu(bo->tbo.base.resv, true, false,
				      MAX_SCHEDULE_TIMEOUT);
	mutex_unlock(&adev->notifier_lock);
	if (r <= 0)
		DRM_ERROR("(%ld) failed to wait for user bo\n", r);
	return true;
}

static const struct mmu_interval_notifier_ops amdgpu_mn_gfx_ops = {
	.invalidate = amdgpu_mn_invalidate_gfx,
};

/**
 * amdgpu_mn_invalidate_hsa - callback to notify about mm change
 *
 * @mni: the range (mm) is about to update
 * @range: details on the invalidation
 * @cur_seq: Value to pass to mmu_interval_set_seq()
 *
 * We temporarily evict the BO attached to this range. This necessitates
 * evicting all user-mode queues of the process.
 */
static bool amdgpu_mn_invalidate_hsa(struct mmu_interval_notifier *mni,
				     const struct mmu_notifier_range *range,
				     unsigned long cur_seq)
{
	struct amdgpu_bo *bo = container_of(mni, struct amdgpu_bo, notifier);
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);

	if (!mmu_notifier_range_blockable(range))
		return false;

	mutex_lock(&adev->notifier_lock);

	mmu_interval_set_seq(mni, cur_seq);

	amdgpu_amdkfd_evict_userptr(bo->kfd_bo, bo->notifier.mm);
	mutex_unlock(&adev->notifier_lock);

	return true;
}

static const struct mmu_interval_notifier_ops amdgpu_mn_hsa_ops = {
	.invalidate = amdgpu_mn_invalidate_hsa,
};

/**
 * amdgpu_mn_register - register a BO for notifier updates
 *
 * @bo: amdgpu buffer object
 * @addr: userptr addr we should monitor
 *
 * Registers a mmu_notifier for the given BO at the specified address.
 * Returns 0 on success, -ERRNO if anything goes wrong.
 */
int amdgpu_mn_register(struct amdgpu_bo *bo, unsigned long addr)
{
	if (bo->kfd_bo)
		return mmu_interval_notifier_insert(&bo->notifier, current->mm,
						    addr, amdgpu_bo_size(bo),
						    &amdgpu_mn_hsa_ops);
	return mmu_interval_notifier_insert(&bo->notifier, current->mm, addr,
					    amdgpu_bo_size(bo),
					    &amdgpu_mn_gfx_ops);
}

/**
 * amdgpu_mn_unregister - unregister a BO for notifier updates
 *
 * @bo: amdgpu buffer object
 *
 * Remove any registration of mmu notifier updates from the buffer object.
 */
void amdgpu_mn_unregister(struct amdgpu_bo *bo)
{
	if (!bo->notifier.mm)
		return;
	mmu_interval_notifier_remove(&bo->notifier);
	bo->notifier.mm = NULL;
}

int amdgpu_hmm_range_get_pages(struct mmu_interval_notifier *notifier,
			       struct mm_struct *mm, struct page **pages,
			       uint64_t start, uint64_t npages,
			       struct hmm_range **phmm_range, bool readonly,
			       bool mmap_locked)
{
	struct hmm_range *hmm_range;
	unsigned long timeout;
	unsigned long i;
	unsigned long *pfns;
	int r = 0;

	hmm_range = kzalloc(sizeof(*hmm_range), GFP_KERNEL);
	if (unlikely(!hmm_range))
		return -ENOMEM;

	pfns = kvmalloc_array(npages, sizeof(*pfns), GFP_KERNEL);
	if (unlikely(!pfns)) {
		r = -ENOMEM;
		goto out_free_range;
	}

	hmm_range->notifier = notifier;
	hmm_range->default_flags = HMM_PFN_REQ_FAULT;
	if (!readonly)
		hmm_range->default_flags |= HMM_PFN_REQ_WRITE;
	hmm_range->hmm_pfns = pfns;
	hmm_range->start = start;
	hmm_range->end = start + npages * PAGE_SIZE;

	/* Assuming 512MB takes maxmium 1 second to fault page address */
	timeout = max(npages >> 17, 1ULL) * HMM_RANGE_DEFAULT_TIMEOUT;
	timeout = jiffies + msecs_to_jiffies(timeout);

retry:
	hmm_range->notifier_seq = mmu_interval_read_begin(notifier);

	if (likely(!mmap_locked))
		mmap_read_lock(mm);

	r = hmm_range_fault(hmm_range);

	if (likely(!mmap_locked))
		mmap_read_unlock(mm);
	if (unlikely(r)) {
		/*
		 * FIXME: This timeout should encompass the retry from
		 * mmu_interval_read_retry() as well.
		 */
		if (r == -EBUSY && !time_after(jiffies, timeout))
			goto retry;
		goto out_free_pfns;
	}

	/*
	 * Due to default_flags, all pages are HMM_PFN_VALID or
	 * hmm_range_fault() fails. FIXME: The pages cannot be touched outside
	 * the notifier_lock, and mmu_interval_read_retry() must be done first.
	 */
	for (i = 0; pages && i < npages; i++)
		pages[i] = hmm_pfn_to_page(pfns[i]);

	*phmm_range = hmm_range;

	return 0;

out_free_pfns:
	kvfree(pfns);
out_free_range:
	kfree(hmm_range);

	return r;
}

int amdgpu_hmm_range_get_pages_done(struct hmm_range *hmm_range)
{
	int r;

	r = mmu_interval_read_retry(hmm_range->notifier,
				    hmm_range->notifier_seq);
	kvfree(hmm_range->hmm_pfns);
	kfree(hmm_range);

	return r;
}
