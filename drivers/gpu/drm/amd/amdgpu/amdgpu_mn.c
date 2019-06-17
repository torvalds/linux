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
#include <linux/hmm.h>
#include <linux/interval_tree.h>

#include <drm/drm.h>

#include "amdgpu.h"
#include "amdgpu_amdkfd.h"

/**
 * struct amdgpu_mn
 *
 * @adev: amdgpu device pointer
 * @mm: process address space
 * @type: type of MMU notifier
 * @work: destruction work item
 * @node: hash table node to find structure by adev and mn
 * @lock: rw semaphore protecting the notifier nodes
 * @objects: interval tree containing amdgpu_mn_nodes
 * @mirror: HMM mirror function support
 *
 * Data for each amdgpu device and process address space.
 */
struct amdgpu_mn {
	/* constant after initialisation */
	struct amdgpu_device	*adev;
	struct mm_struct	*mm;
	enum amdgpu_mn_type	type;

	/* only used on destruction */
	struct work_struct	work;

	/* protected by adev->mn_lock */
	struct hlist_node	node;

	/* objects protected by lock */
	struct rw_semaphore	lock;
	struct rb_root_cached	objects;

	/* HMM mirror */
	struct hmm_mirror	mirror;
};

/**
 * struct amdgpu_mn_node
 *
 * @it: interval node defining start-last of the affected address range
 * @bos: list of all BOs in the affected address range
 *
 * Manages all BOs which are affected of a certain range of address space.
 */
struct amdgpu_mn_node {
	struct interval_tree_node	it;
	struct list_head		bos;
};

/**
 * amdgpu_mn_destroy - destroy the HMM mirror
 *
 * @work: previously sheduled work item
 *
 * Lazy destroys the notifier from a work item
 */
static void amdgpu_mn_destroy(struct work_struct *work)
{
	struct amdgpu_mn *amn = container_of(work, struct amdgpu_mn, work);
	struct amdgpu_device *adev = amn->adev;
	struct amdgpu_mn_node *node, *next_node;
	struct amdgpu_bo *bo, *next_bo;

	mutex_lock(&adev->mn_lock);
	down_write(&amn->lock);
	hash_del(&amn->node);
	rbtree_postorder_for_each_entry_safe(node, next_node,
					     &amn->objects.rb_root, it.rb) {
		list_for_each_entry_safe(bo, next_bo, &node->bos, mn_list) {
			bo->mn = NULL;
			list_del_init(&bo->mn_list);
		}
		kfree(node);
	}
	up_write(&amn->lock);
	mutex_unlock(&adev->mn_lock);

	hmm_mirror_unregister(&amn->mirror);
	kfree(amn);
}

/**
 * amdgpu_hmm_mirror_release - callback to notify about mm destruction
 *
 * @mirror: the HMM mirror (mm) this callback is about
 *
 * Shedule a work item to lazy destroy HMM mirror.
 */
static void amdgpu_hmm_mirror_release(struct hmm_mirror *mirror)
{
	struct amdgpu_mn *amn = container_of(mirror, struct amdgpu_mn, mirror);

	INIT_WORK(&amn->work, amdgpu_mn_destroy);
	schedule_work(&amn->work);
}

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
 * amdgpu_mn_read_lock - take the read side lock for this notifier
 *
 * @amn: our notifier
 */
static int amdgpu_mn_read_lock(struct amdgpu_mn *amn, bool blockable)
{
	if (blockable)
		down_read(&amn->lock);
	else if (!down_read_trylock(&amn->lock))
		return -EAGAIN;

	return 0;
}

/**
 * amdgpu_mn_read_unlock - drop the read side lock for this notifier
 *
 * @amn: our notifier
 */
static void amdgpu_mn_read_unlock(struct amdgpu_mn *amn)
{
	up_read(&amn->lock);
}

/**
 * amdgpu_mn_invalidate_node - unmap all BOs of a node
 *
 * @node: the node with the BOs to unmap
 * @start: start of address range affected
 * @end: end of address range affected
 *
 * Block for operations on BOs to finish and mark pages as accessed and
 * potentially dirty.
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
	}
}

/**
 * amdgpu_mn_sync_pagetables_gfx - callback to notify about mm change
 *
 * @mirror: the hmm_mirror (mm) is about to update
 * @update: the update start, end address
 *
 * Block for operations on BOs to finish and mark pages as accessed and
 * potentially dirty.
 */
static int amdgpu_mn_sync_pagetables_gfx(struct hmm_mirror *mirror,
			const struct hmm_update *update)
{
	struct amdgpu_mn *amn = container_of(mirror, struct amdgpu_mn, mirror);
	unsigned long start = update->start;
	unsigned long end = update->end;
	bool blockable = update->blockable;
	struct interval_tree_node *it;

	/* notification is exclusive, but interval is inclusive */
	end -= 1;

	/* TODO we should be able to split locking for interval tree and
	 * amdgpu_mn_invalidate_node
	 */
	if (amdgpu_mn_read_lock(amn, blockable))
		return -EAGAIN;

	it = interval_tree_iter_first(&amn->objects, start, end);
	while (it) {
		struct amdgpu_mn_node *node;

		if (!blockable) {
			amdgpu_mn_read_unlock(amn);
			return -EAGAIN;
		}

		node = container_of(it, struct amdgpu_mn_node, it);
		it = interval_tree_iter_next(it, start, end);

		amdgpu_mn_invalidate_node(node, start, end);
	}

	amdgpu_mn_read_unlock(amn);

	return 0;
}

/**
 * amdgpu_mn_sync_pagetables_hsa - callback to notify about mm change
 *
 * @mirror: the hmm_mirror (mm) is about to update
 * @update: the update start, end address
 *
 * We temporarily evict all BOs between start and end. This
 * necessitates evicting all user-mode queues of the process. The BOs
 * are restorted in amdgpu_mn_invalidate_range_end_hsa.
 */
static int amdgpu_mn_sync_pagetables_hsa(struct hmm_mirror *mirror,
			const struct hmm_update *update)
{
	struct amdgpu_mn *amn = container_of(mirror, struct amdgpu_mn, mirror);
	unsigned long start = update->start;
	unsigned long end = update->end;
	bool blockable = update->blockable;
	struct interval_tree_node *it;

	/* notification is exclusive, but interval is inclusive */
	end -= 1;

	if (amdgpu_mn_read_lock(amn, blockable))
		return -EAGAIN;

	it = interval_tree_iter_first(&amn->objects, start, end);
	while (it) {
		struct amdgpu_mn_node *node;
		struct amdgpu_bo *bo;

		if (!blockable) {
			amdgpu_mn_read_unlock(amn);
			return -EAGAIN;
		}

		node = container_of(it, struct amdgpu_mn_node, it);
		it = interval_tree_iter_next(it, start, end);

		list_for_each_entry(bo, &node->bos, mn_list) {
			struct kgd_mem *mem = bo->kfd_bo;

			if (amdgpu_ttm_tt_affect_userptr(bo->tbo.ttm,
							 start, end))
				amdgpu_amdkfd_evict_userptr(mem, amn->mm);
		}
	}

	amdgpu_mn_read_unlock(amn);

	return 0;
}

/* Low bits of any reasonable mm pointer will be unused due to struct
 * alignment. Use these bits to make a unique key from the mm pointer
 * and notifier type.
 */
#define AMDGPU_MN_KEY(mm, type) ((unsigned long)(mm) + (type))

static struct hmm_mirror_ops amdgpu_hmm_mirror_ops[] = {
	[AMDGPU_MN_TYPE_GFX] = {
		.sync_cpu_device_pagetables = amdgpu_mn_sync_pagetables_gfx,
		.release = amdgpu_hmm_mirror_release
	},
	[AMDGPU_MN_TYPE_HSA] = {
		.sync_cpu_device_pagetables = amdgpu_mn_sync_pagetables_hsa,
		.release = amdgpu_hmm_mirror_release
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
		if (AMDGPU_MN_KEY(amn->mm, amn->type) == key)
			goto release_locks;

	amn = kzalloc(sizeof(*amn), GFP_KERNEL);
	if (!amn) {
		amn = ERR_PTR(-ENOMEM);
		goto release_locks;
	}

	amn->adev = adev;
	amn->mm = mm;
	init_rwsem(&amn->lock);
	amn->type = type;
	amn->objects = RB_ROOT_CACHED;

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
 * Registers an HMM mirror for the given BO at the specified address.
 * Returns 0 on success, -ERRNO if anything goes wrong.
 */
int amdgpu_mn_register(struct amdgpu_bo *bo, unsigned long addr)
{
	unsigned long end = addr + amdgpu_bo_size(bo) - 1;
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);
	enum amdgpu_mn_type type =
		bo->kfd_bo ? AMDGPU_MN_TYPE_HSA : AMDGPU_MN_TYPE_GFX;
	struct amdgpu_mn *amn;
	struct amdgpu_mn_node *node = NULL, *new_node;
	struct list_head bos;
	struct interval_tree_node *it;

	amn = amdgpu_mn_get(adev, type);
	if (IS_ERR(amn))
		return PTR_ERR(amn);

	new_node = kmalloc(sizeof(*new_node), GFP_KERNEL);
	if (!new_node)
		return -ENOMEM;

	INIT_LIST_HEAD(&bos);

	down_write(&amn->lock);

	while ((it = interval_tree_iter_first(&amn->objects, addr, end))) {
		kfree(node);
		node = container_of(it, struct amdgpu_mn_node, it);
		interval_tree_remove(&node->it, &amn->objects);
		addr = min(it->start, addr);
		end = max(it->last, end);
		list_splice(&node->bos, &bos);
	}

	if (!node)
		node = new_node;
	else
		kfree(new_node);

	bo->mn = amn;

	node->it.start = addr;
	node->it.last = end;
	INIT_LIST_HEAD(&node->bos);
	list_splice(&bos, &node->bos);
	list_add(&bo->mn_list, &node->bos);

	interval_tree_insert(&node->it, &amn->objects);

	up_write(&amn->lock);

	return 0;
}

/**
 * amdgpu_mn_unregister - unregister a BO for HMM mirror updates
 *
 * @bo: amdgpu buffer object
 *
 * Remove any registration of HMM mirror updates from the buffer object.
 */
void amdgpu_mn_unregister(struct amdgpu_bo *bo)
{
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);
	struct amdgpu_mn *amn;
	struct list_head *head;

	mutex_lock(&adev->mn_lock);

	amn = bo->mn;
	if (amn == NULL) {
		mutex_unlock(&adev->mn_lock);
		return;
	}

	down_write(&amn->lock);

	/* save the next list entry for later */
	head = bo->mn_list.next;

	bo->mn = NULL;
	list_del_init(&bo->mn_list);

	if (list_empty(head)) {
		struct amdgpu_mn_node *node;

		node = container_of(head, struct amdgpu_mn_node, bos);
		interval_tree_remove(&node->it, &amn->objects);
		kfree(node);
	}

	up_write(&amn->lock);
	mutex_unlock(&adev->mn_lock);
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
		range->pfns = NULL;
		INIT_LIST_HEAD(&range->list);
	}
}
