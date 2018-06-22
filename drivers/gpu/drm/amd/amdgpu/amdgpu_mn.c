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
#include <linux/interval_tree.h>
#include <drm/drmP.h>
#include <drm/drm.h>

#include "amdgpu.h"
#include "amdgpu_amdkfd.h"

struct amdgpu_mn {
	/* constant after initialisation */
	struct amdgpu_device	*adev;
	struct mm_struct	*mm;
	struct mmu_notifier	mn;
	enum amdgpu_mn_type	type;

	/* only used on destruction */
	struct work_struct	work;

	/* protected by adev->mn_lock */
	struct hlist_node	node;

	/* objects protected by lock */
	struct rw_semaphore	lock;
	struct rb_root_cached	objects;
	struct mutex		read_lock;
	atomic_t		recursion;
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
	down_write(&rmn->lock);
	hash_del(&rmn->node);
	rbtree_postorder_for_each_entry_safe(node, next_node,
					     &rmn->objects.rb_root, it.rb) {
		list_for_each_entry_safe(bo, next_bo, &node->bos, mn_list) {
			bo->mn = NULL;
			list_del_init(&bo->mn_list);
		}
		kfree(node);
	}
	up_write(&rmn->lock);
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
 * amdgpu_mn_lock - take the write side lock for this mn
 */
void amdgpu_mn_lock(struct amdgpu_mn *mn)
{
	if (mn)
		down_write(&mn->lock);
}

/**
 * amdgpu_mn_unlock - drop the write side lock for this mn
 */
void amdgpu_mn_unlock(struct amdgpu_mn *mn)
{
	if (mn)
		up_write(&mn->lock);
}

/**
 * amdgpu_mn_read_lock - take the rmn read lock
 *
 * @rmn: our notifier
 *
 * Take the rmn read side lock.
 */
static void amdgpu_mn_read_lock(struct amdgpu_mn *rmn)
{
	mutex_lock(&rmn->read_lock);
	if (atomic_inc_return(&rmn->recursion) == 1)
		down_read_non_owner(&rmn->lock);
	mutex_unlock(&rmn->read_lock);
}

/**
 * amdgpu_mn_read_unlock - drop the rmn read lock
 *
 * @rmn: our notifier
 *
 * Drop the rmn read side lock.
 */
static void amdgpu_mn_read_unlock(struct amdgpu_mn *rmn)
{
	if (atomic_dec_return(&rmn->recursion) == 0)
		up_read_non_owner(&rmn->lock);
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

		r = reservation_object_wait_timeout_rcu(bo->tbo.resv,
			true, false, MAX_SCHEDULE_TIMEOUT);
		if (r <= 0)
			DRM_ERROR("(%ld) failed to wait for user bo\n", r);

		amdgpu_ttm_tt_mark_user_pages(bo->tbo.ttm);
	}
}

/**
 * amdgpu_mn_invalidate_range_start_gfx - callback to notify about mm change
 *
 * @mn: our notifier
 * @mn: the mm this callback is about
 * @start: start of updated range
 * @end: end of updated range
 *
 * We block for all BOs between start and end to be idle and
 * unmap them by move them into system domain again.
 */
static void amdgpu_mn_invalidate_range_start_gfx(struct mmu_notifier *mn,
						 struct mm_struct *mm,
						 unsigned long start,
						 unsigned long end)
{
	struct amdgpu_mn *rmn = container_of(mn, struct amdgpu_mn, mn);
	struct interval_tree_node *it;

	/* notification is exclusive, but interval is inclusive */
	end -= 1;

	amdgpu_mn_read_lock(rmn);

	it = interval_tree_iter_first(&rmn->objects, start, end);
	while (it) {
		struct amdgpu_mn_node *node;

		node = container_of(it, struct amdgpu_mn_node, it);
		it = interval_tree_iter_next(it, start, end);

		amdgpu_mn_invalidate_node(node, start, end);
	}
}

/**
 * amdgpu_mn_invalidate_range_start_hsa - callback to notify about mm change
 *
 * @mn: our notifier
 * @mn: the mm this callback is about
 * @start: start of updated range
 * @end: end of updated range
 *
 * We temporarily evict all BOs between start and end. This
 * necessitates evicting all user-mode queues of the process. The BOs
 * are restorted in amdgpu_mn_invalidate_range_end_hsa.
 */
static void amdgpu_mn_invalidate_range_start_hsa(struct mmu_notifier *mn,
						 struct mm_struct *mm,
						 unsigned long start,
						 unsigned long end)
{
	struct amdgpu_mn *rmn = container_of(mn, struct amdgpu_mn, mn);
	struct interval_tree_node *it;

	/* notification is exclusive, but interval is inclusive */
	end -= 1;

	amdgpu_mn_read_lock(rmn);

	it = interval_tree_iter_first(&rmn->objects, start, end);
	while (it) {
		struct amdgpu_mn_node *node;
		struct amdgpu_bo *bo;

		node = container_of(it, struct amdgpu_mn_node, it);
		it = interval_tree_iter_next(it, start, end);

		list_for_each_entry(bo, &node->bos, mn_list) {
			struct kgd_mem *mem = bo->kfd_bo;

			if (amdgpu_ttm_tt_affect_userptr(bo->tbo.ttm,
							 start, end))
				amdgpu_amdkfd_evict_userptr(mem, mm);
		}
	}
}

/**
 * amdgpu_mn_invalidate_range_end - callback to notify about mm change
 *
 * @mn: our notifier
 * @mn: the mm this callback is about
 * @start: start of updated range
 * @end: end of updated range
 *
 * Release the lock again to allow new command submissions.
 */
static void amdgpu_mn_invalidate_range_end(struct mmu_notifier *mn,
					   struct mm_struct *mm,
					   unsigned long start,
					   unsigned long end)
{
	struct amdgpu_mn *rmn = container_of(mn, struct amdgpu_mn, mn);

	amdgpu_mn_read_unlock(rmn);
}

static const struct mmu_notifier_ops amdgpu_mn_ops[] = {
	[AMDGPU_MN_TYPE_GFX] = {
		.release = amdgpu_mn_release,
		.invalidate_range_start = amdgpu_mn_invalidate_range_start_gfx,
		.invalidate_range_end = amdgpu_mn_invalidate_range_end,
	},
	[AMDGPU_MN_TYPE_HSA] = {
		.release = amdgpu_mn_release,
		.invalidate_range_start = amdgpu_mn_invalidate_range_start_hsa,
		.invalidate_range_end = amdgpu_mn_invalidate_range_end,
	},
};

/* Low bits of any reasonable mm pointer will be unused due to struct
 * alignment. Use these bits to make a unique key from the mm pointer
 * and notifier type.
 */
#define AMDGPU_MN_KEY(mm, type) ((unsigned long)(mm) + (type))

/**
 * amdgpu_mn_get - create notifier context
 *
 * @adev: amdgpu device pointer
 * @type: type of MMU notifier context
 *
 * Creates a notifier context for current->mm.
 */
struct amdgpu_mn *amdgpu_mn_get(struct amdgpu_device *adev,
				enum amdgpu_mn_type type)
{
	struct mm_struct *mm = current->mm;
	struct amdgpu_mn *rmn;
	unsigned long key = AMDGPU_MN_KEY(mm, type);
	int r;

	mutex_lock(&adev->mn_lock);
	if (down_write_killable(&mm->mmap_sem)) {
		mutex_unlock(&adev->mn_lock);
		return ERR_PTR(-EINTR);
	}

	hash_for_each_possible(adev->mn_hash, rmn, node, key)
		if (AMDGPU_MN_KEY(rmn->mm, rmn->type) == key)
			goto release_locks;

	rmn = kzalloc(sizeof(*rmn), GFP_KERNEL);
	if (!rmn) {
		rmn = ERR_PTR(-ENOMEM);
		goto release_locks;
	}

	rmn->adev = adev;
	rmn->mm = mm;
	init_rwsem(&rmn->lock);
	rmn->type = type;
	rmn->mn.ops = &amdgpu_mn_ops[type];
	rmn->objects = RB_ROOT_CACHED;
	mutex_init(&rmn->read_lock);
	atomic_set(&rmn->recursion, 0);

	r = __mmu_notifier_register(&rmn->mn, mm);
	if (r)
		goto free_rmn;

	hash_add(adev->mn_hash, &rmn->node, AMDGPU_MN_KEY(mm, type));

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
	enum amdgpu_mn_type type =
		bo->kfd_bo ? AMDGPU_MN_TYPE_HSA : AMDGPU_MN_TYPE_GFX;
	struct amdgpu_mn *rmn;
	struct amdgpu_mn_node *node = NULL, *new_node;
	struct list_head bos;
	struct interval_tree_node *it;

	rmn = amdgpu_mn_get(adev, type);
	if (IS_ERR(rmn))
		return PTR_ERR(rmn);

	new_node = kmalloc(sizeof(*new_node), GFP_KERNEL);
	if (!new_node)
		return -ENOMEM;

	INIT_LIST_HEAD(&bos);

	down_write(&rmn->lock);

	while ((it = interval_tree_iter_first(&rmn->objects, addr, end))) {
		kfree(node);
		node = container_of(it, struct amdgpu_mn_node, it);
		interval_tree_remove(&node->it, &rmn->objects);
		addr = min(it->start, addr);
		end = max(it->last, end);
		list_splice(&node->bos, &bos);
	}

	if (!node)
		node = new_node;
	else
		kfree(new_node);

	bo->mn = rmn;

	node->it.start = addr;
	node->it.last = end;
	INIT_LIST_HEAD(&node->bos);
	list_splice(&bos, &node->bos);
	list_add(&bo->mn_list, &node->bos);

	interval_tree_insert(&node->it, &rmn->objects);

	up_write(&rmn->lock);

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

	down_write(&rmn->lock);

	/* save the next list entry for later */
	head = bo->mn_list.next;

	bo->mn = NULL;
	list_del_init(&bo->mn_list);

	if (list_empty(head)) {
		struct amdgpu_mn_node *node;
		node = container_of(head, struct amdgpu_mn_node, bos);
		interval_tree_remove(&node->it, &rmn->objects);
		kfree(node);
	}

	up_write(&rmn->lock);
	mutex_unlock(&adev->mn_lock);
}

