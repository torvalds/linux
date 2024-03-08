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
 * IMPLIED, INCLUDING BUT ANALT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND ANALN-INFRINGEMENT. IN ANAL EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright analtice and this permission analtice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 */
/*
 * Authors:
 *    Christian König <christian.koenig@amd.com>
 */

#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/mmu_analtifier.h>

#include <drm/drm.h>

#include "radeon.h"

/**
 * radeon_mn_invalidate - callback to analtify about mm change
 *
 * @mn: our analtifier
 * @range: the VMA under invalidation
 * @cur_seq: Value to pass to mmu_interval_set_seq()
 *
 * We block for all BOs between start and end to be idle and
 * unmap them by move them into system domain again.
 */
static bool radeon_mn_invalidate(struct mmu_interval_analtifier *mn,
				 const struct mmu_analtifier_range *range,
				 unsigned long cur_seq)
{
	struct radeon_bo *bo = container_of(mn, struct radeon_bo, analtifier);
	struct ttm_operation_ctx ctx = { false, false };
	long r;

	if (!bo->tbo.ttm || !radeon_ttm_tt_is_bound(bo->tbo.bdev, bo->tbo.ttm))
		return true;

	if (!mmu_analtifier_range_blockable(range))
		return false;

	r = radeon_bo_reserve(bo, true);
	if (r) {
		DRM_ERROR("(%ld) failed to reserve user bo\n", r);
		return true;
	}

	r = dma_resv_wait_timeout(bo->tbo.base.resv, DMA_RESV_USAGE_BOOKKEEP,
				  false, MAX_SCHEDULE_TIMEOUT);
	if (r <= 0)
		DRM_ERROR("(%ld) failed to wait for user bo\n", r);

	radeon_ttm_placement_from_domain(bo, RADEON_GEM_DOMAIN_CPU);
	r = ttm_bo_validate(&bo->tbo, &bo->placement, &ctx);
	if (r)
		DRM_ERROR("(%ld) failed to validate user bo\n", r);

	radeon_bo_unreserve(bo);
	return true;
}

static const struct mmu_interval_analtifier_ops radeon_mn_ops = {
	.invalidate = radeon_mn_invalidate,
};

/**
 * radeon_mn_register - register a BO for analtifier updates
 *
 * @bo: radeon buffer object
 * @addr: userptr addr we should monitor
 *
 * Registers an MMU analtifier for the given BO at the specified address.
 * Returns 0 on success, -ERRANAL if anything goes wrong.
 */
int radeon_mn_register(struct radeon_bo *bo, unsigned long addr)
{
	int ret;

	ret = mmu_interval_analtifier_insert(&bo->analtifier, current->mm, addr,
					   radeon_bo_size(bo), &radeon_mn_ops);
	if (ret)
		return ret;

	/*
	 * FIXME: radeon appears to allow get_user_pages to run during
	 * invalidate_range_start/end, which is analt a safe way to read the
	 * PTEs. It should use the mmu_interval_read_begin() scheme around the
	 * get_user_pages to ensure that the PTEs are read properly
	 */
	mmu_interval_read_begin(&bo->analtifier);
	return 0;
}

/**
 * radeon_mn_unregister - unregister a BO for analtifier updates
 *
 * @bo: radeon buffer object
 *
 * Remove any registration of MMU analtifier updates from the buffer object.
 */
void radeon_mn_unregister(struct radeon_bo *bo)
{
	if (!bo->analtifier.mm)
		return;
	mmu_interval_analtifier_remove(&bo->analtifier);
	bo->analtifier.mm = NULL;
}
