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

#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/mmu_notifier.h>

#include <drm/drm.h>

#include "radeon.h"

struct radeon_mn {
	struct mmu_notifier	mn;

	/* objects protected by lock */
	struct mutex		lock;
	struct rb_root_cached	objects;
};

struct radeon_mn_node {
	struct interval_tree_node	it;
	struct list_head		bos;
};

/**
 * radeon_mn_invalidate_range_start - callback to notify about mm change
 *
 * @mn: our notifier
 * @mn: the mm this callback is about
 * @start: start of updated range
 * @end: end of updated range
 *
 * We block for all BOs between start and end to be idle and
 * unmap them by move them into system domain again.
 */
static int radeon_mn_invalidate_range_start(struct mmu_notifier *mn,
				const struct mmu_notifier_range *range)
{
	struct radeon_mn *rmn = container_of(mn, struct radeon_mn, mn);
	struct ttm_operation_ctx ctx = { false, false };
	struct interval_tree_node *it;
	unsigned long end;
	int ret = 0;

	/* notification is exclusive, but interval is inclusive */
	end = range->end - 1;

	/* TODO we should be able to split locking for interval tree and
	 * the tear down.
	 */
	if (mmu_notifier_range_blockable(range))
		mutex_lock(&rmn->lock);
	else if (!mutex_trylock(&rmn->lock))
		return -EAGAIN;

	it = interval_tree_iter_first(&rmn->objects, range->start, end);
	while (it) {
		struct radeon_mn_node *node;
		struct radeon_bo *bo;
		long r;

		if (!mmu_notifier_range_blockable(range)) {
			ret = -EAGAIN;
			goto out_unlock;
		}

		node = container_of(it, struct radeon_mn_node, it);
		it = interval_tree_iter_next(it, range->start, end);

		list_for_each_entry(bo, &node->bos, mn_list) {

			if (!bo->tbo.ttm || bo->tbo.ttm->state != tt_bound)
				continue;

			r = radeon_bo_reserve(bo, true);
			if (r) {
				DRM_ERROR("(%ld) failed to reserve user bo\n", r);
				continue;
			}

			r = dma_resv_wait_timeout_rcu(bo->tbo.base.resv,
				true, false, MAX_SCHEDULE_TIMEOUT);
			if (r <= 0)
				DRM_ERROR("(%ld) failed to wait for user bo\n", r);

			radeon_ttm_placement_from_domain(bo, RADEON_GEM_DOMAIN_CPU);
			r = ttm_bo_validate(&bo->tbo, &bo->placement, &ctx);
			if (r)
				DRM_ERROR("(%ld) failed to validate user bo\n", r);

			radeon_bo_unreserve(bo);
		}
	}
	
out_unlock:
	mutex_unlock(&rmn->lock);

	return ret;
}

static void radeon_mn_release(struct mmu_notifier *mn, struct mm_struct *mm)
{
	struct mmu_notifier_range range = {
		.mm = mm,
		.start = 0,
		.end = ULONG_MAX,
		.flags = 0,
		.event = MMU_NOTIFY_UNMAP,
	};

	radeon_mn_invalidate_range_start(mn, &range);
}

static struct mmu_notifier *radeon_mn_alloc_notifier(struct mm_struct *mm)
{
	struct radeon_mn *rmn;

	rmn = kzalloc(sizeof(*rmn), GFP_KERNEL);
	if (!rmn)
		return ERR_PTR(-ENOMEM);

	mutex_init(&rmn->lock);
	rmn->objects = RB_ROOT_CACHED;
	return &rmn->mn;
}

static void radeon_mn_free_notifier(struct mmu_notifier *mn)
{
	kfree(container_of(mn, struct radeon_mn, mn));
}

static const struct mmu_notifier_ops radeon_mn_ops = {
	.release = radeon_mn_release,
	.invalidate_range_start = radeon_mn_invalidate_range_start,
	.alloc_notifier = radeon_mn_alloc_notifier,
	.free_notifier = radeon_mn_free_notifier,
};

/**
 * radeon_mn_register - register a BO for notifier updates
 *
 * @bo: radeon buffer object
 * @addr: userptr addr we should monitor
 *
 * Registers an MMU notifier for the given BO at the specified address.
 * Returns 0 on success, -ERRNO if anything goes wrong.
 */
int radeon_mn_register(struct radeon_bo *bo, unsigned long addr)
{
	unsigned long end = addr + radeon_bo_size(bo) - 1;
	struct mmu_notifier *mn;
	struct radeon_mn *rmn;
	struct radeon_mn_node *node = NULL;
	struct list_head bos;
	struct interval_tree_node *it;

	mn = mmu_notifier_get(&radeon_mn_ops, current->mm);
	if (IS_ERR(mn))
		return PTR_ERR(mn);
	rmn = container_of(mn, struct radeon_mn, mn);

	INIT_LIST_HEAD(&bos);

	mutex_lock(&rmn->lock);

	while ((it = interval_tree_iter_first(&rmn->objects, addr, end))) {
		kfree(node);
		node = container_of(it, struct radeon_mn_node, it);
		interval_tree_remove(&node->it, &rmn->objects);
		addr = min(it->start, addr);
		end = max(it->last, end);
		list_splice(&node->bos, &bos);
	}

	if (!node) {
		node = kmalloc(sizeof(struct radeon_mn_node), GFP_KERNEL);
		if (!node) {
			mutex_unlock(&rmn->lock);
			return -ENOMEM;
		}
	}

	bo->mn = rmn;

	node->it.start = addr;
	node->it.last = end;
	INIT_LIST_HEAD(&node->bos);
	list_splice(&bos, &node->bos);
	list_add(&bo->mn_list, &node->bos);

	interval_tree_insert(&node->it, &rmn->objects);

	mutex_unlock(&rmn->lock);

	return 0;
}

/**
 * radeon_mn_unregister - unregister a BO for notifier updates
 *
 * @bo: radeon buffer object
 *
 * Remove any registration of MMU notifier updates from the buffer object.
 */
void radeon_mn_unregister(struct radeon_bo *bo)
{
	struct radeon_mn *rmn = bo->mn;
	struct list_head *head;

	if (!rmn)
		return;

	mutex_lock(&rmn->lock);
	/* save the next list entry for later */
	head = bo->mn_list.next;

	list_del(&bo->mn_list);

	if (list_empty(head)) {
		struct radeon_mn_node *node;
		node = container_of(head, struct radeon_mn_node, bos);
		interval_tree_remove(&node->it, &rmn->objects);
		kfree(node);
	}

	mutex_unlock(&rmn->lock);

	mmu_notifier_put(&rmn->mn);
	bo->mn = NULL;
}
