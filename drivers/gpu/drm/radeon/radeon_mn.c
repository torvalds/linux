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
#include <drm/drmP.h>
#include <drm/drm.h>

#include "radeon.h"

struct radeon_mn {
	/* constant after initialisation */
	struct radeon_device	*rdev;
	struct mm_struct	*mm;
	struct mmu_notifier	mn;

	/* only used on destruction */
	struct work_struct	work;

	/* protected by rdev->mn_lock */
	struct hlist_node	node;

	/* objects protected by lock */
	struct mutex		lock;
	struct rb_root_cached	objects;
};

struct radeon_mn_node {
	struct interval_tree_node	it;
	struct list_head		bos;
};

/**
 * radeon_mn_destroy - destroy the rmn
 *
 * @work: previously sheduled work item
 *
 * Lazy destroys the notifier from a work item
 */
static void radeon_mn_destroy(struct work_struct *work)
{
	struct radeon_mn *rmn = container_of(work, struct radeon_mn, work);
	struct radeon_device *rdev = rmn->rdev;
	struct radeon_mn_node *node, *next_node;
	struct radeon_bo *bo, *next_bo;

	mutex_lock(&rdev->mn_lock);
	mutex_lock(&rmn->lock);
	hash_del(&rmn->node);
	rbtree_postorder_for_each_entry_safe(node, next_node,
					     &rmn->objects.rb_root, it.rb) {

		interval_tree_remove(&node->it, &rmn->objects);
		list_for_each_entry_safe(bo, next_bo, &node->bos, mn_list) {
			bo->mn = NULL;
			list_del_init(&bo->mn_list);
		}
		kfree(node);
	}
	mutex_unlock(&rmn->lock);
	mutex_unlock(&rdev->mn_lock);
	mmu_notifier_unregister(&rmn->mn, rmn->mm);
	kfree(rmn);
}

/**
 * radeon_mn_release - callback to notify about mm destruction
 *
 * @mn: our notifier
 * @mn: the mm this callback is about
 *
 * Shedule a work item to lazy destroy our notifier.
 */
static void radeon_mn_release(struct mmu_notifier *mn,
			      struct mm_struct *mm)
{
	struct radeon_mn *rmn = container_of(mn, struct radeon_mn, mn);
	INIT_WORK(&rmn->work, radeon_mn_destroy);
	schedule_work(&rmn->work);
}

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
static void radeon_mn_invalidate_range_start(struct mmu_notifier *mn,
					     struct mm_struct *mm,
					     unsigned long start,
					     unsigned long end)
{
	struct radeon_mn *rmn = container_of(mn, struct radeon_mn, mn);
	struct interval_tree_node *it;

	/* notification is exclusive, but interval is inclusive */
	end -= 1;

	mutex_lock(&rmn->lock);

	it = interval_tree_iter_first(&rmn->objects, start, end);
	while (it) {
		struct radeon_mn_node *node;
		struct radeon_bo *bo;
		long r;

		node = container_of(it, struct radeon_mn_node, it);
		it = interval_tree_iter_next(it, start, end);

		list_for_each_entry(bo, &node->bos, mn_list) {

			if (!bo->tbo.ttm || bo->tbo.ttm->state != tt_bound)
				continue;

			r = radeon_bo_reserve(bo, true);
			if (r) {
				DRM_ERROR("(%ld) failed to reserve user bo\n", r);
				continue;
			}

			r = reservation_object_wait_timeout_rcu(bo->tbo.resv,
				true, false, MAX_SCHEDULE_TIMEOUT);
			if (r <= 0)
				DRM_ERROR("(%ld) failed to wait for user bo\n", r);

			radeon_ttm_placement_from_domain(bo, RADEON_GEM_DOMAIN_CPU);
			r = ttm_bo_validate(&bo->tbo, &bo->placement, false, false);
			if (r)
				DRM_ERROR("(%ld) failed to validate user bo\n", r);

			radeon_bo_unreserve(bo);
		}
	}
	
	mutex_unlock(&rmn->lock);
}

static const struct mmu_notifier_ops radeon_mn_ops = {
	.release = radeon_mn_release,
	.invalidate_range_start = radeon_mn_invalidate_range_start,
};

/**
 * radeon_mn_get - create notifier context
 *
 * @rdev: radeon device pointer
 *
 * Creates a notifier context for current->mm.
 */
static struct radeon_mn *radeon_mn_get(struct radeon_device *rdev)
{
	struct mm_struct *mm = current->mm;
	struct radeon_mn *rmn;
	int r;

	if (down_write_killable(&mm->mmap_sem))
		return ERR_PTR(-EINTR);

	mutex_lock(&rdev->mn_lock);

	hash_for_each_possible(rdev->mn_hash, rmn, node, (unsigned long)mm)
		if (rmn->mm == mm)
			goto release_locks;

	rmn = kzalloc(sizeof(*rmn), GFP_KERNEL);
	if (!rmn) {
		rmn = ERR_PTR(-ENOMEM);
		goto release_locks;
	}

	rmn->rdev = rdev;
	rmn->mm = mm;
	rmn->mn.ops = &radeon_mn_ops;
	mutex_init(&rmn->lock);
	rmn->objects = RB_ROOT_CACHED;
	
	r = __mmu_notifier_register(&rmn->mn, mm);
	if (r)
		goto free_rmn;

	hash_add(rdev->mn_hash, &rmn->node, (unsigned long)mm);

release_locks:
	mutex_unlock(&rdev->mn_lock);
	up_write(&mm->mmap_sem);

	return rmn;

free_rmn:
	mutex_unlock(&rdev->mn_lock);
	up_write(&mm->mmap_sem);
	kfree(rmn);

	return ERR_PTR(r);
}

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
	struct radeon_device *rdev = bo->rdev;
	struct radeon_mn *rmn;
	struct radeon_mn_node *node = NULL;
	struct list_head bos;
	struct interval_tree_node *it;

	rmn = radeon_mn_get(rdev);
	if (IS_ERR(rmn))
		return PTR_ERR(rmn);

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
	struct radeon_device *rdev = bo->rdev;
	struct radeon_mn *rmn;
	struct list_head *head;

	mutex_lock(&rdev->mn_lock);
	rmn = bo->mn;
	if (rmn == NULL) {
		mutex_unlock(&rdev->mn_lock);
		return;
	}

	mutex_lock(&rmn->lock);
	/* save the next list entry for later */
	head = bo->mn_list.next;

	bo->mn = NULL;
	list_del(&bo->mn_list);

	if (list_empty(head)) {
		struct radeon_mn_node *node;
		node = container_of(head, struct radeon_mn_node, bos);
		interval_tree_remove(&node->it, &rmn->objects);
		kfree(node);
	}

	mutex_unlock(&rmn->lock);
	mutex_unlock(&rdev->mn_lock);
}
