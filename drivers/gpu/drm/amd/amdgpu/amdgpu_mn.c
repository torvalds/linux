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

#include "amdgpu.h"

struct amdgpu_mn {
	/* constant after initialisation */
	struct amdgpu_device	*adev;
	struct mm_struct	*mm;
	struct mmu_notifier	mn;

	/* only used on destruction */
	struct work_struct	work;

	/* protected by adev->mn_lock */
	struct hlist_node	node;

	/* objects protected by lock */
	struct mutex		lock;
	struct rb_root		objects;
};

struct amdgpu_mn_node {
	struct interval_tree_node	it;
	struct list_head		bos;
};

/**
 * amdgpu_mn_destroy - destroy the rmn
 *
 * @work: previously sheduled work item
 *
 * Lazy destroys the notifier from a work item
 */
static void amdgpu_mn_destroy(struct work_struct *work)
{
	struct amdgpu_mn *rmn = container_of(work, struct amdgpu_mn, work);
	struct amdgpu_device *adev = rmn->adev;
	struct amdgpu_mn_node *node, *next_node;
	struct amdgpu_bo *bo, *next_bo;

	mutex_lock(&adev->mn_lock);
	mutex_lock(&rmn->lock);
	hash_del(&rmn->node);
	rbtree_postorder_for_each_entry_safe(node, next_node, &rmn->objects,
					     it.rb) {
		list_for_each_entry_safe(bo, next_bo, &node->bos, mn_list) {
			bo->mn = NULL;
			list_del_init(&bo->mn_list);
		}
		kfree(node);
	}
	mutex_unlock(&rmn->lock);
	mutex_unlock(&adev->mn_lock);
	mmu_notifier_unregister_no_release(&rmn->mn, rmn->mm);
	kfree(rmn);
}

/**
 * amdgpu_mn_release - callback to notify about mm destruction
 *
 * @mn: our notifier
 * @mn: the mm this callback is about
 *
 * Shedule a work item to lazy destroy our notifier.
 */
static void amdgpu_mn_release(struct mmu_notifier *mn,
			      struct mm_struct *mm)
{
	struct amdgpu_mn *rmn = container_of(mn, struct amdgpu_mn, mn);
	INIT_WORK(&rmn->work, amdgpu_mn_destroy);
	schedule_work(&rmn->work);
}

/**
 * amdgpu_mn_invalidate_node - unmap all BOs of a node
 *
 * @node: the node with the BOs to unmap
 *
 * We block for all BOs and unmap them by move them
 * into system domain again.
 */
static void amdgpu_mn_invalidate_node(struct amdgpu_mn_node *node,
				      unsigned long start,
				      unsigned long end)
{
	struct amdgpu_bo *bo;
	long r;

	list_for_each_entry(bo, &node->bos, mn_list) {

		if (!amdgpu_ttm_tt_affect_userptr(bo->tbo.ttm, start, end))
			continue;

		r = amdgpu_bo_reserve(bo, true);
		if (r) {
			DRM_ERROR("(%ld) failed to reserve user bo\n", r);
			continue;
		}

		r = reservation_object_wait_timeout_rcu(bo->tbo.resv,
			true, false, MAX_SCHEDULE_TIMEOUT);
		if (r <= 0)
			DRM_ERROR("(%ld) failed to wait for user bo\n", r);

		amdgpu_ttm_placement_from_domain(bo, AMDGPU_GEM_DOMAIN_CPU);
		r = ttm_bo_validate(&bo->tbo, &bo->placement, false, false);
		if (r)
			DRM_ERROR("(%ld) failed to validate user bo\n", r);

		amdgpu_bo_unreserve(bo);
	}
}

/**
 * amdgpu_mn_invalidate_page - callback to notify about mm change
 *
 * @mn: our notifier
 * @mn: the mm this callback is about
 * @address: address of invalidate page
 *
 * Invalidation of a single page. Blocks for all BOs mapping it
 * and unmap them by move them into system domain again.
 */
static void amdgpu_mn_invalidate_page(struct mmu_notifier *mn,
				      struct mm_struct *mm,
				      unsigned long address)
{
	struct amdgpu_mn *rmn = container_of(mn, struct amdgpu_mn, mn);
	struct interval_tree_node *it;

	mutex_lock(&rmn->lock);

	it = interval_tree_iter_first(&rmn->objects, address, address);
	if (it) {
		struct amdgpu_mn_node *node;

		node = container_of(it, struct amdgpu_mn_node, it);
		amdgpu_mn_invalidate_node(node, address, address);
	}

	mutex_unlock(&rmn->lock);
}

/**
 * amdgpu_mn_invalidate_range_start - callback to notify about mm change
 *
 * @mn: our notifier
 * @mn: the mm this callback is about
 * @start: start of updated range
 * @end: end of updated range
 *
 * We block for all BOs between start and end to be idle and
 * unmap them by move them into system domain again.
 */
static void amdgpu_mn_invalidate_range_start(struct mmu_notifier *mn,
					     struct mm_struct *mm,
					     unsigned long start,
					     unsigned long end)
{
	struct amdgpu_mn *rmn = container_of(mn, struct amdgpu_mn, mn);
	struct interval_tree_node *it;

	/* notification is exclusive, but interval is inclusive */
	end -= 1;

	mutex_lock(&rmn->lock);

	it = interval_tree_iter_first(&rmn->objects, start, end);
	while (it) {
		struct amdgpu_mn_node *node;

		node = container_of(it, struct amdgpu_mn_node, it);
		it = interval_tree_iter_next(it, start, end);

		amdgpu_mn_invalidate_node(node, start, end);
	}

	mutex_unlock(&rmn->lock);
}

static const struct mmu_notifier_ops amdgpu_mn_ops = {
	.release = amdgpu_mn_release,
	.invalidate_page = amdgpu_mn_invalidate_page,
	.invalidate_range_start = amdgpu_mn_invalidate_range_start,
};

/**
 * amdgpu_mn_get - create notifier context
 *
 * @adev: amdgpu device pointer
 *
 * Creates a notifier context for current->mm.
 */
static struct amdgpu_mn *amdgpu_mn_get(struct amdgpu_device *adev)
{
	struct mm_struct *mm = current->mm;
	struct amdgpu_mn *rmn;
	int r;

	mutex_lock(&adev->mn_lock);
	if (down_write_killable(&mm->mmap_sem)) {
		mutex_unlock(&adev->mn_lock);
		return ERR_PTR(-EINTR);
	}

	hash_for_each_possible(adev->mn_hash, rmn, node, (unsigned long)mm)
		if (rmn->mm == mm)
			goto release_locks;

	rmn = kzalloc(sizeof(*rmn), GFP_KERNEL);
	if (!rmn) {
		rmn = ERR_PTR(-ENOMEM);
		goto release_locks;
	}

	rmn->adev = adev;
	rmn->mm = mm;
	rmn->mn.ops = &amdgpu_mn_ops;
	mutex_init(&rmn->lock);
	rmn->objects = RB_ROOT;

	r = __mmu_notifier_register(&rmn->mn, mm);
	if (r)
		goto free_rmn;

	hash_add(adev->mn_hash, &rmn->node, (unsigned long)mm);

release_locks:
	up_write(&mm->mmap_sem);
	mutex_unlock(&adev->mn_lock);

	return rmn;

free_rmn:
	up_write(&mm->mmap_sem);
	mutex_unlock(&adev->mn_lock);
	kfree(rmn);

	return ERR_PTR(r);
}

/**
 * amdgpu_mn_register - register a BO for notifier updates
 *
 * @bo: amdgpu buffer object
 * @addr: userptr addr we should monitor
 *
 * Registers an MMU notifier for the given BO at the specified address.
 * Returns 0 on success, -ERRNO if anything goes wrong.
 */
int amdgpu_mn_register(struct amdgpu_bo *bo, unsigned long addr)
{
	unsigned long end = addr + amdgpu_bo_size(bo) - 1;
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);
	struct amdgpu_mn *rmn;
	struct amdgpu_mn_node *node = NULL;
	struct list_head bos;
	struct interval_tree_node *it;

	rmn = amdgpu_mn_get(adev);
	if (IS_ERR(rmn))
		return PTR_ERR(rmn);

	INIT_LIST_HEAD(&bos);

	mutex_lock(&rmn->lock);

	while ((it = interval_tree_iter_first(&rmn->objects, addr, end))) {
		kfree(node);
		node = container_of(it, struct amdgpu_mn_node, it);
		interval_tree_remove(&node->it, &rmn->objects);
		addr = min(it->start, addr);
		end = max(it->last, end);
		list_splice(&node->bos, &bos);
	}

	if (!node) {
		node = kmalloc(sizeof(struct amdgpu_mn_node), GFP_KERNEL);
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
 * amdgpu_mn_unregister - unregister a BO for notifier updates
 *
 * @bo: amdgpu buffer object
 *
 * Remove any registration of MMU notifier updates from the buffer object.
 */
void amdgpu_mn_unregister(struct amdgpu_bo *bo)
{
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);
	struct amdgpu_mn *rmn;
	struct list_head *head;

	mutex_lock(&adev->mn_lock);

	rmn = bo->mn;
	if (rmn == NULL) {
		mutex_unlock(&adev->mn_lock);
		return;
	}

	mutex_lock(&rmn->lock);

	/* save the next list entry for later */
	head = bo->mn_list.next;

	bo->mn = NULL;
	list_del(&bo->mn_list);

	if (list_empty(head)) {
		struct amdgpu_mn_node *node;
		node = container_of(head, struct amdgpu_mn_node, bos);
		interval_tree_remove(&node->it, &rmn->objects);
		kfree(node);
	}

	mutex_unlock(&rmn->lock);
	mutex_unlock(&adev->mn_lock);
}
