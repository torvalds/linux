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
 * amdgpu_mn_lock - take the write side lock for this notifier
 *
 * @mn: our notifier
 */
void amdgpu_mn_lock(struct amdgpu_mn *mn)
{
	if (mn)
		down_write(&mn->lock);
}

/**
 * amdgpu_mn_unlock - drop the write side lock for this notifier
 *
 * @mn: our notifier
 */
void amdgpu_mn_unlock(struct amdgpu_mn *mn)
{
	if (mn)
		up_write(&mn->lock);
}

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
	amdgpu_amdkfd_evict_userptr(bo->kfd_bo, bo->notifier.mm);
	mutex_unlock(&adev->notifier_lock);

	return true;
}

static const struct mmu_interval_notifier_ops amdgpu_mn_hsa_ops = {
	.invalidate = amdgpu_mn_invalidate_hsa,
};

static int amdgpu_mn_sync_pagetables(struct hmm_mirror *mirror,
				     const struct mmu_notifier_range *update)
{
	struct amdgpu_mn *amn = container_of(mirror, struct amdgpu_mn, mirror);

	if (!mmu_notifier_range_blockable(update))
		return -EAGAIN;

	down_read(&amn->lock);
	up_read(&amn->lock);
	return 0;
}

/* Low bits of any reasonable mm pointer will be unused due to struct
 * alignment. Use these bits to make a unique key from the mm pointer
 * and notifier type.
 */
#define AMDGPU_MN_KEY(mm, type) ((unsigned long)(mm) + (type))

static struct hmm_mirror_ops amdgpu_hmm_mirror_ops[] = {
	[AMDGPU_MN_TYPE_GFX] = {
		.sync_cpu_device_pagetables = amdgpu_mn_sync_pagetables,
	},
	[AMDGPU_MN_TYPE_HSA] = {
		.sync_cpu_device_pagetables = amdgpu_mn_sync_pagetables,
	},
};

/**
 * amdgpu_mn_get - create HMM mirror context
 *
 * @adev: amdgpu device pointer
 * @type: type of MMU notifier context
 *
 * Creates a HMM mirror context for current->mm.
 */
struct amdgpu_mn *amdgpu_mn_get(struct amdgpu_device *adev,
				enum amdgpu_mn_type type)
{
	struct mm_struct *mm = current->mm;
	struct amdgpu_mn *amn;
	unsigned long key = AMDGPU_MN_KEY(mm, type);
	int r;

	mutex_lock(&adev->mn_lock);
	if (down_write_killable(&mm->mmap_sem)) {
		mutex_unlock(&adev->mn_lock);
		return ERR_PTR(-EINTR);
	}

	hash_for_each_possible(adev->mn_hash, amn, node, key)
		if (AMDGPU_MN_KEY(amn->mirror.hmm->mmu_notifier.mm,
				  amn->type) == key)
			goto release_locks;

	amn = kzalloc(sizeof(*amn), GFP_KERNEL);
	if (!amn) {
		amn = ERR_PTR(-ENOMEM);
		goto release_locks;
	}

	amn->adev = adev;
	init_rwsem(&amn->lock);
	amn->type = type;

	amn->mirror.ops = &amdgpu_hmm_mirror_ops[type];
	r = hmm_mirror_register(&amn->mirror, mm);
	if (r)
		goto free_amn;

	hash_add(adev->mn_hash, &amn->node, AMDGPU_MN_KEY(mm, type));

release_locks:
	up_write(&mm->mmap_sem);
	mutex_unlock(&adev->mn_lock);

	return amn;

free_amn:
	up_write(&mm->mmap_sem);
	mutex_unlock(&adev->mn_lock);
	kfree(amn);

	return ERR_PTR(r);
}

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
		bo->notifier.ops = &amdgpu_mn_hsa_ops;
	else
		bo->notifier.ops = &amdgpu_mn_gfx_ops;

	return mmu_interval_notifier_insert(&bo->notifier, addr,
					    amdgpu_bo_size(bo), current->mm);
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

/* flags used by HMM internal, not related to CPU/GPU PTE flags */
static const uint64_t hmm_range_flags[HMM_PFN_FLAG_MAX] = {
		(1 << 0), /* HMM_PFN_VALID */
		(1 << 1), /* HMM_PFN_WRITE */
		0 /* HMM_PFN_DEVICE_PRIVATE */
};

static const uint64_t hmm_range_values[HMM_PFN_VALUE_MAX] = {
		0xfffffffffffffffeUL, /* HMM_PFN_ERROR */
		0, /* HMM_PFN_NONE */
		0xfffffffffffffffcUL /* HMM_PFN_SPECIAL */
};

void amdgpu_hmm_init_range(struct hmm_range *range)
{
	if (range) {
		range->flags = hmm_range_flags;
		range->values = hmm_range_values;
		range->pfn_shift = PAGE_SHIFT;
	}
}
