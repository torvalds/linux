/**************************************************************************
 *
 * Copyright (c) 2006-2009 VMware, Inc., Palo Alto, CA., USA
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
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/
/*
 * Authors: Thomas Hellstrom <thellstrom-at-vmware-dot-com>
 */

#define pr_fmt(fmt) "[TTM] " fmt

#include <drm/ttm/ttm_module.h>
#include <drm/ttm/ttm_bo_driver.h>
#include <drm/ttm/ttm_placement.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/file.h>
#include <linux/module.h>
#include <linux/atomic.h>

#define TTM_ASSERT_LOCKED(param)
#define TTM_DEBUG(fmt, arg...)
#define TTM_BO_HASH_ORDER 13

static int ttm_bo_setup_vm(struct ttm_buffer_object *bo);
static int ttm_bo_swapout(struct ttm_mem_shrink *shrink);
static void ttm_bo_global_kobj_release(struct kobject *kobj);

static struct attribute ttm_bo_count = {
	.name = "bo_count",
	.mode = S_IRUGO
};

static inline int ttm_mem_type_from_flags(uint32_t flags, uint32_t *mem_type)
{
	int i;

	for (i = 0; i <= TTM_PL_PRIV5; i++)
		if (flags & (1 << i)) {
			*mem_type = i;
			return 0;
		}
	return -EINVAL;
}

static void ttm_mem_type_debug(struct ttm_bo_device *bdev, int mem_type)
{
	struct ttm_mem_type_manager *man = &bdev->man[mem_type];

	pr_err("    has_type: %d\n", man->has_type);
	pr_err("    use_type: %d\n", man->use_type);
	pr_err("    flags: 0x%08X\n", man->flags);
	pr_err("    gpu_offset: 0x%08lX\n", man->gpu_offset);
	pr_err("    size: %llu\n", man->size);
	pr_err("    available_caching: 0x%08X\n", man->available_caching);
	pr_err("    default_caching: 0x%08X\n", man->default_caching);
	if (mem_type != TTM_PL_SYSTEM)
		(*man->func->debug)(man, TTM_PFX);
}

static void ttm_bo_mem_space_debug(struct ttm_buffer_object *bo,
					struct ttm_placement *placement)
{
	int i, ret, mem_type;

	pr_err("No space for %p (%lu pages, %luK, %luM)\n",
	       bo, bo->mem.num_pages, bo->mem.size >> 10,
	       bo->mem.size >> 20);
	for (i = 0; i < placement->num_placement; i++) {
		ret = ttm_mem_type_from_flags(placement->placement[i],
						&mem_type);
		if (ret)
			return;
		pr_err("  placement[%d]=0x%08X (%d)\n",
		       i, placement->placement[i], mem_type);
		ttm_mem_type_debug(bo->bdev, mem_type);
	}
}

static ssize_t ttm_bo_global_show(struct kobject *kobj,
				  struct attribute *attr,
				  char *buffer)
{
	struct ttm_bo_global *glob =
		container_of(kobj, struct ttm_bo_global, kobj);

	return snprintf(buffer, PAGE_SIZE, "%lu\n",
			(unsigned long) atomic_read(&glob->bo_count));
}

static struct attribute *ttm_bo_global_attrs[] = {
	&ttm_bo_count,
	NULL
};

static const struct sysfs_ops ttm_bo_global_ops = {
	.show = &ttm_bo_global_show
};

static struct kobj_type ttm_bo_glob_kobj_type  = {
	.release = &ttm_bo_global_kobj_release,
	.sysfs_ops = &ttm_bo_global_ops,
	.default_attrs = ttm_bo_global_attrs
};


static inline uint32_t ttm_bo_type_flags(unsigned type)
{
	return 1 << (type);
}

static void ttm_bo_release_list(struct kref *list_kref)
{
	struct ttm_buffer_object *bo =
	    container_of(list_kref, struct ttm_buffer_object, list_kref);
	struct ttm_bo_device *bdev = bo->bdev;
	size_t acc_size = bo->acc_size;

	BUG_ON(atomic_read(&bo->list_kref.refcount));
	BUG_ON(atomic_read(&bo->kref.refcount));
	BUG_ON(atomic_read(&bo->cpu_writers));
	BUG_ON(bo->sync_obj != NULL);
	BUG_ON(bo->mem.mm_node != NULL);
	BUG_ON(!list_empty(&bo->lru));
	BUG_ON(!list_empty(&bo->ddestroy));

	if (bo->ttm)
		ttm_tt_destroy(bo->ttm);
	atomic_dec(&bo->glob->bo_count);
	if (bo->destroy)
		bo->destroy(bo);
	else {
		kfree(bo);
	}
	ttm_mem_global_free(bdev->glob->mem_glob, acc_size);
}

static int ttm_bo_wait_unreserved(struct ttm_buffer_object *bo,
				  bool interruptible)
{
	if (interruptible) {
		return wait_event_interruptible(bo->event_queue,
					       !ttm_bo_is_reserved(bo));
	} else {
		wait_event(bo->event_queue, !ttm_bo_is_reserved(bo));
		return 0;
	}
}

void ttm_bo_add_to_lru(struct ttm_buffer_object *bo)
{
	struct ttm_bo_device *bdev = bo->bdev;
	struct ttm_mem_type_manager *man;

	BUG_ON(!ttm_bo_is_reserved(bo));

	if (!(bo->mem.placement & TTM_PL_FLAG_NO_EVICT)) {

		BUG_ON(!list_empty(&bo->lru));

		man = &bdev->man[bo->mem.mem_type];
		list_add_tail(&bo->lru, &man->lru);
		kref_get(&bo->list_kref);

		if (bo->ttm != NULL) {
			list_add_tail(&bo->swap, &bo->glob->swap_lru);
			kref_get(&bo->list_kref);
		}
	}
}

int ttm_bo_del_from_lru(struct ttm_buffer_object *bo)
{
	int put_count = 0;

	if (!list_empty(&bo->swap)) {
		list_del_init(&bo->swap);
		++put_count;
	}
	if (!list_empty(&bo->lru)) {
		list_del_init(&bo->lru);
		++put_count;
	}

	/*
	 * TODO: Add a driver hook to delete from
	 * driver-specific LRU's here.
	 */

	return put_count;
}

int ttm_bo_reserve_nolru(struct ttm_buffer_object *bo,
			  bool interruptible,
			  bool no_wait, bool use_sequence, uint32_t sequence)
{
	int ret;

	while (unlikely(atomic_xchg(&bo->reserved, 1) != 0)) {
		/**
		 * Deadlock avoidance for multi-bo reserving.
		 */
		if (use_sequence && bo->seq_valid) {
			/**
			 * We've already reserved this one.
			 */
			if (unlikely(sequence == bo->val_seq))
				return -EDEADLK;
			/**
			 * Already reserved by a thread that will not back
			 * off for us. We need to back off.
			 */
			if (unlikely(sequence - bo->val_seq < (1 << 31)))
				return -EAGAIN;
		}

		if (no_wait)
			return -EBUSY;

		ret = ttm_bo_wait_unreserved(bo, interruptible);

		if (unlikely(ret))
			return ret;
	}

	if (use_sequence) {
		bool wake_up = false;
		/**
		 * Wake up waiters that may need to recheck for deadlock,
		 * if we decreased the sequence number.
		 */
		if (unlikely((bo->val_seq - sequence < (1 << 31))
			     || !bo->seq_valid))
			wake_up = true;

		/*
		 * In the worst case with memory ordering these values can be
		 * seen in the wrong order. However since we call wake_up_all
		 * in that case, this will hopefully not pose a problem,
		 * and the worst case would only cause someone to accidentally
		 * hit -EAGAIN in ttm_bo_reserve when they see old value of
		 * val_seq. However this would only happen if seq_valid was
		 * written before val_seq was, and just means some slightly
		 * increased cpu usage
		 */
		bo->val_seq = sequence;
		bo->seq_valid = true;
		if (wake_up)
			wake_up_all(&bo->event_queue);
	} else {
		bo->seq_valid = false;
	}

	return 0;
}
EXPORT_SYMBOL(ttm_bo_reserve);

static void ttm_bo_ref_bug(struct kref *list_kref)
{
	BUG();
}

void ttm_bo_list_ref_sub(struct ttm_buffer_object *bo, int count,
			 bool never_free)
{
	kref_sub(&bo->list_kref, count,
		 (never_free) ? ttm_bo_ref_bug : ttm_bo_release_list);
}

int ttm_bo_reserve(struct ttm_buffer_object *bo,
		   bool interruptible,
		   bool no_wait, bool use_sequence, uint32_t sequence)
{
	struct ttm_bo_global *glob = bo->glob;
	int put_count = 0;
	int ret;

	ret = ttm_bo_reserve_nolru(bo, interruptible, no_wait, use_sequence,
				   sequence);
	if (likely(ret == 0)) {
		spin_lock(&glob->lru_lock);
		put_count = ttm_bo_del_from_lru(bo);
		spin_unlock(&glob->lru_lock);
		ttm_bo_list_ref_sub(bo, put_count, true);
	}

	return ret;
}

int ttm_bo_reserve_slowpath_nolru(struct ttm_buffer_object *bo,
				  bool interruptible, uint32_t sequence)
{
	bool wake_up = false;
	int ret;

	while (unlikely(atomic_xchg(&bo->reserved, 1) != 0)) {
		WARN_ON(bo->seq_valid && sequence == bo->val_seq);

		ret = ttm_bo_wait_unreserved(bo, interruptible);

		if (unlikely(ret))
			return ret;
	}

	if ((bo->val_seq - sequence < (1 << 31)) || !bo->seq_valid)
		wake_up = true;

	/**
	 * Wake up waiters that may need to recheck for deadlock,
	 * if we decreased the sequence number.
	 */
	bo->val_seq = sequence;
	bo->seq_valid = true;
	if (wake_up)
		wake_up_all(&bo->event_queue);

	return 0;
}

int ttm_bo_reserve_slowpath(struct ttm_buffer_object *bo,
			    bool interruptible, uint32_t sequence)
{
	struct ttm_bo_global *glob = bo->glob;
	int put_count, ret;

	ret = ttm_bo_reserve_slowpath_nolru(bo, interruptible, sequence);
	if (likely(!ret)) {
		spin_lock(&glob->lru_lock);
		put_count = ttm_bo_del_from_lru(bo);
		spin_unlock(&glob->lru_lock);
		ttm_bo_list_ref_sub(bo, put_count, true);
	}
	return ret;
}
EXPORT_SYMBOL(ttm_bo_reserve_slowpath);

void ttm_bo_unreserve_locked(struct ttm_buffer_object *bo)
{
	ttm_bo_add_to_lru(bo);
	atomic_set(&bo->reserved, 0);
	wake_up_all(&bo->event_queue);
}

void ttm_bo_unreserve(struct ttm_buffer_object *bo)
{
	struct ttm_bo_global *glob = bo->glob;

	spin_lock(&glob->lru_lock);
	ttm_bo_unreserve_locked(bo);
	spin_unlock(&glob->lru_lock);
}
EXPORT_SYMBOL(ttm_bo_unreserve);

/*
 * Call bo->mutex locked.
 */
static int ttm_bo_add_ttm(struct ttm_buffer_object *bo, bool zero_alloc)
{
	struct ttm_bo_device *bdev = bo->bdev;
	struct ttm_bo_global *glob = bo->glob;
	int ret = 0;
	uint32_t page_flags = 0;

	TTM_ASSERT_LOCKED(&bo->mutex);
	bo->ttm = NULL;

	if (bdev->need_dma32)
		page_flags |= TTM_PAGE_FLAG_DMA32;

	switch (bo->type) {
	case ttm_bo_type_device:
		if (zero_alloc)
			page_flags |= TTM_PAGE_FLAG_ZERO_ALLOC;
	case ttm_bo_type_kernel:
		bo->ttm = bdev->driver->ttm_tt_create(bdev, bo->num_pages << PAGE_SHIFT,
						      page_flags, glob->dummy_read_page);
		if (unlikely(bo->ttm == NULL))
			ret = -ENOMEM;
		break;
	case ttm_bo_type_sg:
		bo->ttm = bdev->driver->ttm_tt_create(bdev, bo->num_pages << PAGE_SHIFT,
						      page_flags | TTM_PAGE_FLAG_SG,
						      glob->dummy_read_page);
		if (unlikely(bo->ttm == NULL)) {
			ret = -ENOMEM;
			break;
		}
		bo->ttm->sg = bo->sg;
		break;
	default:
		pr_err("Illegal buffer object type\n");
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int ttm_bo_handle_move_mem(struct ttm_buffer_object *bo,
				  struct ttm_mem_reg *mem,
				  bool evict, bool interruptible,
				  bool no_wait_gpu)
{
	struct ttm_bo_device *bdev = bo->bdev;
	bool old_is_pci = ttm_mem_reg_is_pci(bdev, &bo->mem);
	bool new_is_pci = ttm_mem_reg_is_pci(bdev, mem);
	struct ttm_mem_type_manager *old_man = &bdev->man[bo->mem.mem_type];
	struct ttm_mem_type_manager *new_man = &bdev->man[mem->mem_type];
	int ret = 0;

	if (old_is_pci || new_is_pci ||
	    ((mem->placement & bo->mem.placement & TTM_PL_MASK_CACHING) == 0)) {
		ret = ttm_mem_io_lock(old_man, true);
		if (unlikely(ret != 0))
			goto out_err;
		ttm_bo_unmap_virtual_locked(bo);
		ttm_mem_io_unlock(old_man);
	}

	/*
	 * Create and bind a ttm if required.
	 */

	if (!(new_man->flags & TTM_MEMTYPE_FLAG_FIXED)) {
		if (bo->ttm == NULL) {
			bool zero = !(old_man->flags & TTM_MEMTYPE_FLAG_FIXED);
			ret = ttm_bo_add_ttm(bo, zero);
			if (ret)
				goto out_err;
		}

		ret = ttm_tt_set_placement_caching(bo->ttm, mem->placement);
		if (ret)
			goto out_err;

		if (mem->mem_type != TTM_PL_SYSTEM) {
			ret = ttm_tt_bind(bo->ttm, mem);
			if (ret)
				goto out_err;
		}

		if (bo->mem.mem_type == TTM_PL_SYSTEM) {
			if (bdev->driver->move_notify)
				bdev->driver->move_notify(bo, mem);
			bo->mem = *mem;
			mem->mm_node = NULL;
			goto moved;
		}
	}

	if (bdev->driver->move_notify)
		bdev->driver->move_notify(bo, mem);

	if (!(old_man->flags & TTM_MEMTYPE_FLAG_FIXED) &&
	    !(new_man->flags & TTM_MEMTYPE_FLAG_FIXED))
		ret = ttm_bo_move_ttm(bo, evict, no_wait_gpu, mem);
	else if (bdev->driver->move)
		ret = bdev->driver->move(bo, evict, interruptible,
					 no_wait_gpu, mem);
	else
		ret = ttm_bo_move_memcpy(bo, evict, no_wait_gpu, mem);

	if (ret) {
		if (bdev->driver->move_notify) {
			struct ttm_mem_reg tmp_mem = *mem;
			*mem = bo->mem;
			bo->mem = tmp_mem;
			bdev->driver->move_notify(bo, mem);
			bo->mem = *mem;
			*mem = tmp_mem;
		}

		goto out_err;
	}

moved:
	if (bo->evicted) {
		ret = bdev->driver->invalidate_caches(bdev, bo->mem.placement);
		if (ret)
			pr_err("Can not flush read caches\n");
		bo->evicted = false;
	}

	if (bo->mem.mm_node) {
		bo->offset = (bo->mem.start << PAGE_SHIFT) +
		    bdev->man[bo->mem.mem_type].gpu_offset;
		bo->cur_placement = bo->mem.placement;
	} else
		bo->offset = 0;

	return 0;

out_err:
	new_man = &bdev->man[bo->mem.mem_type];
	if ((new_man->flags & TTM_MEMTYPE_FLAG_FIXED) && bo->ttm) {
		ttm_tt_unbind(bo->ttm);
		ttm_tt_destroy(bo->ttm);
		bo->ttm = NULL;
	}

	return ret;
}

/**
 * Call bo::reserved.
 * Will release GPU memory type usage on destruction.
 * This is the place to put in driver specific hooks to release
 * driver private resources.
 * Will release the bo::reserved lock.
 */

static void ttm_bo_cleanup_memtype_use(struct ttm_buffer_object *bo)
{
	if (bo->bdev->driver->move_notify)
		bo->bdev->driver->move_notify(bo, NULL);

	if (bo->ttm) {
		ttm_tt_unbind(bo->ttm);
		ttm_tt_destroy(bo->ttm);
		bo->ttm = NULL;
	}
	ttm_bo_mem_put(bo, &bo->mem);

	atomic_set(&bo->reserved, 0);
	wake_up_all(&bo->event_queue);

	/*
	 * Since the final reference to this bo may not be dropped by
	 * the current task we have to put a memory barrier here to make
	 * sure the changes done in this function are always visible.
	 *
	 * This function only needs protection against the final kref_put.
	 */
	smp_mb__before_atomic_dec();
}

static void ttm_bo_cleanup_refs_or_queue(struct ttm_buffer_object *bo)
{
	struct ttm_bo_device *bdev = bo->bdev;
	struct ttm_bo_global *glob = bo->glob;
	struct ttm_bo_driver *driver = bdev->driver;
	void *sync_obj = NULL;
	int put_count;
	int ret;

	spin_lock(&glob->lru_lock);
	ret = ttm_bo_reserve_nolru(bo, false, true, false, 0);

	spin_lock(&bdev->fence_lock);
	(void) ttm_bo_wait(bo, false, false, true);
	if (!ret && !bo->sync_obj) {
		spin_unlock(&bdev->fence_lock);
		put_count = ttm_bo_del_from_lru(bo);

		spin_unlock(&glob->lru_lock);
		ttm_bo_cleanup_memtype_use(bo);

		ttm_bo_list_ref_sub(bo, put_count, true);

		return;
	}
	if (bo->sync_obj)
		sync_obj = driver->sync_obj_ref(bo->sync_obj);
	spin_unlock(&bdev->fence_lock);

	if (!ret) {
		atomic_set(&bo->reserved, 0);
		wake_up_all(&bo->event_queue);
	}

	kref_get(&bo->list_kref);
	list_add_tail(&bo->ddestroy, &bdev->ddestroy);
	spin_unlock(&glob->lru_lock);

	if (sync_obj) {
		driver->sync_obj_flush(sync_obj);
		driver->sync_obj_unref(&sync_obj);
	}
	schedule_delayed_work(&bdev->wq,
			      ((HZ / 100) < 1) ? 1 : HZ / 100);
}

/**
 * function ttm_bo_cleanup_refs_and_unlock
 * If bo idle, remove from delayed- and lru lists, and unref.
 * If not idle, do nothing.
 *
 * Must be called with lru_lock and reservation held, this function
 * will drop both before returning.
 *
 * @interruptible         Any sleeps should occur interruptibly.
 * @no_wait_gpu           Never wait for gpu. Return -EBUSY instead.
 */

static int ttm_bo_cleanup_refs_and_unlock(struct ttm_buffer_object *bo,
					  bool interruptible,
					  bool no_wait_gpu)
{
	struct ttm_bo_device *bdev = bo->bdev;
	struct ttm_bo_driver *driver = bdev->driver;
	struct ttm_bo_global *glob = bo->glob;
	int put_count;
	int ret;

	spin_lock(&bdev->fence_lock);
	ret = ttm_bo_wait(bo, false, false, true);

	if (ret && !no_wait_gpu) {
		void *sync_obj;

		/*
		 * Take a reference to the fence and unreserve,
		 * at this point the buffer should be dead, so
		 * no new sync objects can be attached.
		 */
		sync_obj = driver->sync_obj_ref(bo->sync_obj);
		spin_unlock(&bdev->fence_lock);

		atomic_set(&bo->reserved, 0);
		wake_up_all(&bo->event_queue);
		spin_unlock(&glob->lru_lock);

		ret = driver->sync_obj_wait(sync_obj, false, interruptible);
		driver->sync_obj_unref(&sync_obj);
		if (ret)
			return ret;

		/*
		 * remove sync_obj with ttm_bo_wait, the wait should be
		 * finished, and no new wait object should have been added.
		 */
		spin_lock(&bdev->fence_lock);
		ret = ttm_bo_wait(bo, false, false, true);
		WARN_ON(ret);
		spin_unlock(&bdev->fence_lock);
		if (ret)
			return ret;

		spin_lock(&glob->lru_lock);
		ret = ttm_bo_reserve_nolru(bo, false, true, false, 0);

		/*
		 * We raced, and lost, someone else holds the reservation now,
		 * and is probably busy in ttm_bo_cleanup_memtype_use.
		 *
		 * Even if it's not the case, because we finished waiting any
		 * delayed destruction would succeed, so just return success
		 * here.
		 */
		if (ret) {
			spin_unlock(&glob->lru_lock);
			return 0;
		}
	} else
		spin_unlock(&bdev->fence_lock);

	if (ret || unlikely(list_empty(&bo->ddestroy))) {
		atomic_set(&bo->reserved, 0);
		wake_up_all(&bo->event_queue);
		spin_unlock(&glob->lru_lock);
		return ret;
	}

	put_count = ttm_bo_del_from_lru(bo);
	list_del_init(&bo->ddestroy);
	++put_count;

	spin_unlock(&glob->lru_lock);
	ttm_bo_cleanup_memtype_use(bo);

	ttm_bo_list_ref_sub(bo, put_count, true);

	return 0;
}

/**
 * Traverse the delayed list, and call ttm_bo_cleanup_refs on all
 * encountered buffers.
 */

static int ttm_bo_delayed_delete(struct ttm_bo_device *bdev, bool remove_all)
{
	struct ttm_bo_global *glob = bdev->glob;
	struct ttm_buffer_object *entry = NULL;
	int ret = 0;

	spin_lock(&glob->lru_lock);
	if (list_empty(&bdev->ddestroy))
		goto out_unlock;

	entry = list_first_entry(&bdev->ddestroy,
		struct ttm_buffer_object, ddestroy);
	kref_get(&entry->list_kref);

	for (;;) {
		struct ttm_buffer_object *nentry = NULL;

		if (entry->ddestroy.next != &bdev->ddestroy) {
			nentry = list_first_entry(&entry->ddestroy,
				struct ttm_buffer_object, ddestroy);
			kref_get(&nentry->list_kref);
		}

		ret = ttm_bo_reserve_nolru(entry, false, true, false, 0);
		if (remove_all && ret) {
			spin_unlock(&glob->lru_lock);
			ret = ttm_bo_reserve_nolru(entry, false, false,
						   false, 0);
			spin_lock(&glob->lru_lock);
		}

		if (!ret)
			ret = ttm_bo_cleanup_refs_and_unlock(entry, false,
							     !remove_all);
		else
			spin_unlock(&glob->lru_lock);

		kref_put(&entry->list_kref, ttm_bo_release_list);
		entry = nentry;

		if (ret || !entry)
			goto out;

		spin_lock(&glob->lru_lock);
		if (list_empty(&entry->ddestroy))
			break;
	}

out_unlock:
	spin_unlock(&glob->lru_lock);
out:
	if (entry)
		kref_put(&entry->list_kref, ttm_bo_release_list);
	return ret;
}

static void ttm_bo_delayed_workqueue(struct work_struct *work)
{
	struct ttm_bo_device *bdev =
	    container_of(work, struct ttm_bo_device, wq.work);

	if (ttm_bo_delayed_delete(bdev, false)) {
		schedule_delayed_work(&bdev->wq,
				      ((HZ / 100) < 1) ? 1 : HZ / 100);
	}
}

static void ttm_bo_release(struct kref *kref)
{
	struct ttm_buffer_object *bo =
	    container_of(kref, struct ttm_buffer_object, kref);
	struct ttm_bo_device *bdev = bo->bdev;
	struct ttm_mem_type_manager *man = &bdev->man[bo->mem.mem_type];

	write_lock(&bdev->vm_lock);
	if (likely(bo->vm_node != NULL)) {
		rb_erase(&bo->vm_rb, &bdev->addr_space_rb);
		drm_mm_put_block(bo->vm_node);
		bo->vm_node = NULL;
	}
	write_unlock(&bdev->vm_lock);
	ttm_mem_io_lock(man, false);
	ttm_mem_io_free_vm(bo);
	ttm_mem_io_unlock(man);
	ttm_bo_cleanup_refs_or_queue(bo);
	kref_put(&bo->list_kref, ttm_bo_release_list);
}

void ttm_bo_unref(struct ttm_buffer_object **p_bo)
{
	struct ttm_buffer_object *bo = *p_bo;

	*p_bo = NULL;
	kref_put(&bo->kref, ttm_bo_release);
}
EXPORT_SYMBOL(ttm_bo_unref);

int ttm_bo_lock_delayed_workqueue(struct ttm_bo_device *bdev)
{
	return cancel_delayed_work_sync(&bdev->wq);
}
EXPORT_SYMBOL(ttm_bo_lock_delayed_workqueue);

void ttm_bo_unlock_delayed_workqueue(struct ttm_bo_device *bdev, int resched)
{
	if (resched)
		schedule_delayed_work(&bdev->wq,
				      ((HZ / 100) < 1) ? 1 : HZ / 100);
}
EXPORT_SYMBOL(ttm_bo_unlock_delayed_workqueue);

static int ttm_bo_evict(struct ttm_buffer_object *bo, bool interruptible,
			bool no_wait_gpu)
{
	struct ttm_bo_device *bdev = bo->bdev;
	struct ttm_mem_reg evict_mem;
	struct ttm_placement placement;
	int ret = 0;

	spin_lock(&bdev->fence_lock);
	ret = ttm_bo_wait(bo, false, interruptible, no_wait_gpu);
	spin_unlock(&bdev->fence_lock);

	if (unlikely(ret != 0)) {
		if (ret != -ERESTARTSYS) {
			pr_err("Failed to expire sync object before buffer eviction\n");
		}
		goto out;
	}

	BUG_ON(!ttm_bo_is_reserved(bo));

	evict_mem = bo->mem;
	evict_mem.mm_node = NULL;
	evict_mem.bus.io_reserved_vm = false;
	evict_mem.bus.io_reserved_count = 0;

	placement.fpfn = 0;
	placement.lpfn = 0;
	placement.num_placement = 0;
	placement.num_busy_placement = 0;
	bdev->driver->evict_flags(bo, &placement);
	ret = ttm_bo_mem_space(bo, &placement, &evict_mem, interruptible,
				no_wait_gpu);
	if (ret) {
		if (ret != -ERESTARTSYS) {
			pr_err("Failed to find memory space for buffer 0x%p eviction\n",
			       bo);
			ttm_bo_mem_space_debug(bo, &placement);
		}
		goto out;
	}

	ret = ttm_bo_handle_move_mem(bo, &evict_mem, true, interruptible,
				     no_wait_gpu);
	if (ret) {
		if (ret != -ERESTARTSYS)
			pr_err("Buffer eviction failed\n");
		ttm_bo_mem_put(bo, &evict_mem);
		goto out;
	}
	bo->evicted = true;
out:
	return ret;
}

static int ttm_mem_evict_first(struct ttm_bo_device *bdev,
				uint32_t mem_type,
				bool interruptible,
				bool no_wait_gpu)
{
	struct ttm_bo_global *glob = bdev->glob;
	struct ttm_mem_type_manager *man = &bdev->man[mem_type];
	struct ttm_buffer_object *bo;
	int ret = -EBUSY, put_count;

	spin_lock(&glob->lru_lock);
	list_for_each_entry(bo, &man->lru, lru) {
		ret = ttm_bo_reserve_nolru(bo, false, true, false, 0);
		if (!ret)
			break;
	}

	if (ret) {
		spin_unlock(&glob->lru_lock);
		return ret;
	}

	kref_get(&bo->list_kref);

	if (!list_empty(&bo->ddestroy)) {
		ret = ttm_bo_cleanup_refs_and_unlock(bo, interruptible,
						     no_wait_gpu);
		kref_put(&bo->list_kref, ttm_bo_release_list);
		return ret;
	}

	put_count = ttm_bo_del_from_lru(bo);
	spin_unlock(&glob->lru_lock);

	BUG_ON(ret != 0);

	ttm_bo_list_ref_sub(bo, put_count, true);

	ret = ttm_bo_evict(bo, interruptible, no_wait_gpu);
	ttm_bo_unreserve(bo);

	kref_put(&bo->list_kref, ttm_bo_release_list);
	return ret;
}

void ttm_bo_mem_put(struct ttm_buffer_object *bo, struct ttm_mem_reg *mem)
{
	struct ttm_mem_type_manager *man = &bo->bdev->man[mem->mem_type];

	if (mem->mm_node)
		(*man->func->put_node)(man, mem);
}
EXPORT_SYMBOL(ttm_bo_mem_put);

/**
 * Repeatedly evict memory from the LRU for @mem_type until we create enough
 * space, or we've evicted everything and there isn't enough space.
 */
static int ttm_bo_mem_force_space(struct ttm_buffer_object *bo,
					uint32_t mem_type,
					struct ttm_placement *placement,
					struct ttm_mem_reg *mem,
					bool interruptible,
					bool no_wait_gpu)
{
	struct ttm_bo_device *bdev = bo->bdev;
	struct ttm_mem_type_manager *man = &bdev->man[mem_type];
	int ret;

	do {
		ret = (*man->func->get_node)(man, bo, placement, mem);
		if (unlikely(ret != 0))
			return ret;
		if (mem->mm_node)
			break;
		ret = ttm_mem_evict_first(bdev, mem_type,
					  interruptible, no_wait_gpu);
		if (unlikely(ret != 0))
			return ret;
	} while (1);
	if (mem->mm_node == NULL)
		return -ENOMEM;
	mem->mem_type = mem_type;
	return 0;
}

static uint32_t ttm_bo_select_caching(struct ttm_mem_type_manager *man,
				      uint32_t cur_placement,
				      uint32_t proposed_placement)
{
	uint32_t caching = proposed_placement & TTM_PL_MASK_CACHING;
	uint32_t result = proposed_placement & ~TTM_PL_MASK_CACHING;

	/**
	 * Keep current caching if possible.
	 */

	if ((cur_placement & caching) != 0)
		result |= (cur_placement & caching);
	else if ((man->default_caching & caching) != 0)
		result |= man->default_caching;
	else if ((TTM_PL_FLAG_CACHED & caching) != 0)
		result |= TTM_PL_FLAG_CACHED;
	else if ((TTM_PL_FLAG_WC & caching) != 0)
		result |= TTM_PL_FLAG_WC;
	else if ((TTM_PL_FLAG_UNCACHED & caching) != 0)
		result |= TTM_PL_FLAG_UNCACHED;

	return result;
}

static bool ttm_bo_mt_compatible(struct ttm_mem_type_manager *man,
				 uint32_t mem_type,
				 uint32_t proposed_placement,
				 uint32_t *masked_placement)
{
	uint32_t cur_flags = ttm_bo_type_flags(mem_type);

	if ((cur_flags & proposed_placement & TTM_PL_MASK_MEM) == 0)
		return false;

	if ((proposed_placement & man->available_caching) == 0)
		return false;

	cur_flags |= (proposed_placement & man->available_caching);

	*masked_placement = cur_flags;
	return true;
}

/**
 * Creates space for memory region @mem according to its type.
 *
 * This function first searches for free space in compatible memory types in
 * the priority order defined by the driver.  If free space isn't found, then
 * ttm_bo_mem_force_space is attempted in priority order to evict and find
 * space.
 */
int ttm_bo_mem_space(struct ttm_buffer_object *bo,
			struct ttm_placement *placement,
			struct ttm_mem_reg *mem,
			bool interruptible,
			bool no_wait_gpu)
{
	struct ttm_bo_device *bdev = bo->bdev;
	struct ttm_mem_type_manager *man;
	uint32_t mem_type = TTM_PL_SYSTEM;
	uint32_t cur_flags = 0;
	bool type_found = false;
	bool type_ok = false;
	bool has_erestartsys = false;
	int i, ret;

	mem->mm_node = NULL;
	for (i = 0; i < placement->num_placement; ++i) {
		ret = ttm_mem_type_from_flags(placement->placement[i],
						&mem_type);
		if (ret)
			return ret;
		man = &bdev->man[mem_type];

		type_ok = ttm_bo_mt_compatible(man,
						mem_type,
						placement->placement[i],
						&cur_flags);

		if (!type_ok)
			continue;

		cur_flags = ttm_bo_select_caching(man, bo->mem.placement,
						  cur_flags);
		/*
		 * Use the access and other non-mapping-related flag bits from
		 * the memory placement flags to the current flags
		 */
		ttm_flag_masked(&cur_flags, placement->placement[i],
				~TTM_PL_MASK_MEMTYPE);

		if (mem_type == TTM_PL_SYSTEM)
			break;

		if (man->has_type && man->use_type) {
			type_found = true;
			ret = (*man->func->get_node)(man, bo, placement, mem);
			if (unlikely(ret))
				return ret;
		}
		if (mem->mm_node)
			break;
	}

	if ((type_ok && (mem_type == TTM_PL_SYSTEM)) || mem->mm_node) {
		mem->mem_type = mem_type;
		mem->placement = cur_flags;
		return 0;
	}

	if (!type_found)
		return -EINVAL;

	for (i = 0; i < placement->num_busy_placement; ++i) {
		ret = ttm_mem_type_from_flags(placement->busy_placement[i],
						&mem_type);
		if (ret)
			return ret;
		man = &bdev->man[mem_type];
		if (!man->has_type)
			continue;
		if (!ttm_bo_mt_compatible(man,
						mem_type,
						placement->busy_placement[i],
						&cur_flags))
			continue;

		cur_flags = ttm_bo_select_caching(man, bo->mem.placement,
						  cur_flags);
		/*
		 * Use the access and other non-mapping-related flag bits from
		 * the memory placement flags to the current flags
		 */
		ttm_flag_masked(&cur_flags, placement->busy_placement[i],
				~TTM_PL_MASK_MEMTYPE);


		if (mem_type == TTM_PL_SYSTEM) {
			mem->mem_type = mem_type;
			mem->placement = cur_flags;
			mem->mm_node = NULL;
			return 0;
		}

		ret = ttm_bo_mem_force_space(bo, mem_type, placement, mem,
						interruptible, no_wait_gpu);
		if (ret == 0 && mem->mm_node) {
			mem->placement = cur_flags;
			return 0;
		}
		if (ret == -ERESTARTSYS)
			has_erestartsys = true;
	}
	ret = (has_erestartsys) ? -ERESTARTSYS : -ENOMEM;
	return ret;
}
EXPORT_SYMBOL(ttm_bo_mem_space);

int ttm_bo_move_buffer(struct ttm_buffer_object *bo,
			struct ttm_placement *placement,
			bool interruptible,
			bool no_wait_gpu)
{
	int ret = 0;
	struct ttm_mem_reg mem;
	struct ttm_bo_device *bdev = bo->bdev;

	BUG_ON(!ttm_bo_is_reserved(bo));

	/*
	 * FIXME: It's possible to pipeline buffer moves.
	 * Have the driver move function wait for idle when necessary,
	 * instead of doing it here.
	 */
	spin_lock(&bdev->fence_lock);
	ret = ttm_bo_wait(bo, false, interruptible, no_wait_gpu);
	spin_unlock(&bdev->fence_lock);
	if (ret)
		return ret;
	mem.num_pages = bo->num_pages;
	mem.size = mem.num_pages << PAGE_SHIFT;
	mem.page_alignment = bo->mem.page_alignment;
	mem.bus.io_reserved_vm = false;
	mem.bus.io_reserved_count = 0;
	/*
	 * Determine where to move the buffer.
	 */
	ret = ttm_bo_mem_space(bo, placement, &mem,
			       interruptible, no_wait_gpu);
	if (ret)
		goto out_unlock;
	ret = ttm_bo_handle_move_mem(bo, &mem, false,
				     interruptible, no_wait_gpu);
out_unlock:
	if (ret && mem.mm_node)
		ttm_bo_mem_put(bo, &mem);
	return ret;
}

static int ttm_bo_mem_compat(struct ttm_placement *placement,
			     struct ttm_mem_reg *mem)
{
	int i;

	if (mem->mm_node && placement->lpfn != 0 &&
	    (mem->start < placement->fpfn ||
	     mem->start + mem->num_pages > placement->lpfn))
		return -1;

	for (i = 0; i < placement->num_placement; i++) {
		if ((placement->placement[i] & mem->placement &
			TTM_PL_MASK_CACHING) &&
			(placement->placement[i] & mem->placement &
			TTM_PL_MASK_MEM))
			return i;
	}
	return -1;
}

int ttm_bo_validate(struct ttm_buffer_object *bo,
			struct ttm_placement *placement,
			bool interruptible,
			bool no_wait_gpu)
{
	int ret;

	BUG_ON(!ttm_bo_is_reserved(bo));
	/* Check that range is valid */
	if (placement->lpfn || placement->fpfn)
		if (placement->fpfn > placement->lpfn ||
			(placement->lpfn - placement->fpfn) < bo->num_pages)
			return -EINVAL;
	/*
	 * Check whether we need to move buffer.
	 */
	ret = ttm_bo_mem_compat(placement, &bo->mem);
	if (ret < 0) {
		ret = ttm_bo_move_buffer(bo, placement, interruptible,
					 no_wait_gpu);
		if (ret)
			return ret;
	} else {
		/*
		 * Use the access and other non-mapping-related flag bits from
		 * the compatible memory placement flags to the active flags
		 */
		ttm_flag_masked(&bo->mem.placement, placement->placement[ret],
				~TTM_PL_MASK_MEMTYPE);
	}
	/*
	 * We might need to add a TTM.
	 */
	if (bo->mem.mem_type == TTM_PL_SYSTEM && bo->ttm == NULL) {
		ret = ttm_bo_add_ttm(bo, true);
		if (ret)
			return ret;
	}
	return 0;
}
EXPORT_SYMBOL(ttm_bo_validate);

int ttm_bo_check_placement(struct ttm_buffer_object *bo,
				struct ttm_placement *placement)
{
	BUG_ON((placement->fpfn || placement->lpfn) &&
	       (bo->mem.num_pages > (placement->lpfn - placement->fpfn)));

	return 0;
}

int ttm_bo_init(struct ttm_bo_device *bdev,
		struct ttm_buffer_object *bo,
		unsigned long size,
		enum ttm_bo_type type,
		struct ttm_placement *placement,
		uint32_t page_alignment,
		bool interruptible,
		struct file *persistent_swap_storage,
		size_t acc_size,
		struct sg_table *sg,
		void (*destroy) (struct ttm_buffer_object *))
{
	int ret = 0;
	unsigned long num_pages;
	struct ttm_mem_global *mem_glob = bdev->glob->mem_glob;

	ret = ttm_mem_global_alloc(mem_glob, acc_size, false, false);
	if (ret) {
		pr_err("Out of kernel memory\n");
		if (destroy)
			(*destroy)(bo);
		else
			kfree(bo);
		return -ENOMEM;
	}

	num_pages = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	if (num_pages == 0) {
		pr_err("Illegal buffer object size\n");
		if (destroy)
			(*destroy)(bo);
		else
			kfree(bo);
		ttm_mem_global_free(mem_glob, acc_size);
		return -EINVAL;
	}
	bo->destroy = destroy;

	kref_init(&bo->kref);
	kref_init(&bo->list_kref);
	atomic_set(&bo->cpu_writers, 0);
	atomic_set(&bo->reserved, 1);
	init_waitqueue_head(&bo->event_queue);
	INIT_LIST_HEAD(&bo->lru);
	INIT_LIST_HEAD(&bo->ddestroy);
	INIT_LIST_HEAD(&bo->swap);
	INIT_LIST_HEAD(&bo->io_reserve_lru);
	bo->bdev = bdev;
	bo->glob = bdev->glob;
	bo->type = type;
	bo->num_pages = num_pages;
	bo->mem.size = num_pages << PAGE_SHIFT;
	bo->mem.mem_type = TTM_PL_SYSTEM;
	bo->mem.num_pages = bo->num_pages;
	bo->mem.mm_node = NULL;
	bo->mem.page_alignment = page_alignment;
	bo->mem.bus.io_reserved_vm = false;
	bo->mem.bus.io_reserved_count = 0;
	bo->priv_flags = 0;
	bo->mem.placement = (TTM_PL_FLAG_SYSTEM | TTM_PL_FLAG_CACHED);
	bo->seq_valid = false;
	bo->persistent_swap_storage = persistent_swap_storage;
	bo->acc_size = acc_size;
	bo->sg = sg;
	atomic_inc(&bo->glob->bo_count);

	ret = ttm_bo_check_placement(bo, placement);
	if (unlikely(ret != 0))
		goto out_err;

	/*
	 * For ttm_bo_type_device buffers, allocate
	 * address space from the device.
	 */
	if (bo->type == ttm_bo_type_device ||
	    bo->type == ttm_bo_type_sg) {
		ret = ttm_bo_setup_vm(bo);
		if (ret)
			goto out_err;
	}

	ret = ttm_bo_validate(bo, placement, interruptible, false);
	if (ret)
		goto out_err;

	ttm_bo_unreserve(bo);
	return 0;

out_err:
	ttm_bo_unreserve(bo);
	ttm_bo_unref(&bo);

	return ret;
}
EXPORT_SYMBOL(ttm_bo_init);

size_t ttm_bo_acc_size(struct ttm_bo_device *bdev,
		       unsigned long bo_size,
		       unsigned struct_size)
{
	unsigned npages = (PAGE_ALIGN(bo_size)) >> PAGE_SHIFT;
	size_t size = 0;

	size += ttm_round_pot(struct_size);
	size += PAGE_ALIGN(npages * sizeof(void *));
	size += ttm_round_pot(sizeof(struct ttm_tt));
	return size;
}
EXPORT_SYMBOL(ttm_bo_acc_size);

size_t ttm_bo_dma_acc_size(struct ttm_bo_device *bdev,
			   unsigned long bo_size,
			   unsigned struct_size)
{
	unsigned npages = (PAGE_ALIGN(bo_size)) >> PAGE_SHIFT;
	size_t size = 0;

	size += ttm_round_pot(struct_size);
	size += PAGE_ALIGN(npages * sizeof(void *));
	size += PAGE_ALIGN(npages * sizeof(dma_addr_t));
	size += ttm_round_pot(sizeof(struct ttm_dma_tt));
	return size;
}
EXPORT_SYMBOL(ttm_bo_dma_acc_size);

int ttm_bo_create(struct ttm_bo_device *bdev,
			unsigned long size,
			enum ttm_bo_type type,
			struct ttm_placement *placement,
			uint32_t page_alignment,
			bool interruptible,
			struct file *persistent_swap_storage,
			struct ttm_buffer_object **p_bo)
{
	struct ttm_buffer_object *bo;
	size_t acc_size;
	int ret;

	bo = kzalloc(sizeof(*bo), GFP_KERNEL);
	if (unlikely(bo == NULL))
		return -ENOMEM;

	acc_size = ttm_bo_acc_size(bdev, size, sizeof(struct ttm_buffer_object));
	ret = ttm_bo_init(bdev, bo, size, type, placement, page_alignment,
			  interruptible, persistent_swap_storage, acc_size,
			  NULL, NULL);
	if (likely(ret == 0))
		*p_bo = bo;

	return ret;
}
EXPORT_SYMBOL(ttm_bo_create);

static int ttm_bo_force_list_clean(struct ttm_bo_device *bdev,
					unsigned mem_type, bool allow_errors)
{
	struct ttm_mem_type_manager *man = &bdev->man[mem_type];
	struct ttm_bo_global *glob = bdev->glob;
	int ret;

	/*
	 * Can't use standard list traversal since we're unlocking.
	 */

	spin_lock(&glob->lru_lock);
	while (!list_empty(&man->lru)) {
		spin_unlock(&glob->lru_lock);
		ret = ttm_mem_evict_first(bdev, mem_type, false, false);
		if (ret) {
			if (allow_errors) {
				return ret;
			} else {
				pr_err("Cleanup eviction failed\n");
			}
		}
		spin_lock(&glob->lru_lock);
	}
	spin_unlock(&glob->lru_lock);
	return 0;
}

int ttm_bo_clean_mm(struct ttm_bo_device *bdev, unsigned mem_type)
{
	struct ttm_mem_type_manager *man;
	int ret = -EINVAL;

	if (mem_type >= TTM_NUM_MEM_TYPES) {
		pr_err("Illegal memory type %d\n", mem_type);
		return ret;
	}
	man = &bdev->man[mem_type];

	if (!man->has_type) {
		pr_err("Trying to take down uninitialized memory manager type %u\n",
		       mem_type);
		return ret;
	}

	man->use_type = false;
	man->has_type = false;

	ret = 0;
	if (mem_type > 0) {
		ttm_bo_force_list_clean(bdev, mem_type, false);

		ret = (*man->func->takedown)(man);
	}

	return ret;
}
EXPORT_SYMBOL(ttm_bo_clean_mm);

int ttm_bo_evict_mm(struct ttm_bo_device *bdev, unsigned mem_type)
{
	struct ttm_mem_type_manager *man = &bdev->man[mem_type];

	if (mem_type == 0 || mem_type >= TTM_NUM_MEM_TYPES) {
		pr_err("Illegal memory manager memory type %u\n", mem_type);
		return -EINVAL;
	}

	if (!man->has_type) {
		pr_err("Memory type %u has not been initialized\n", mem_type);
		return 0;
	}

	return ttm_bo_force_list_clean(bdev, mem_type, true);
}
EXPORT_SYMBOL(ttm_bo_evict_mm);

int ttm_bo_init_mm(struct ttm_bo_device *bdev, unsigned type,
			unsigned long p_size)
{
	int ret = -EINVAL;
	struct ttm_mem_type_manager *man;

	BUG_ON(type >= TTM_NUM_MEM_TYPES);
	man = &bdev->man[type];
	BUG_ON(man->has_type);
	man->io_reserve_fastpath = true;
	man->use_io_reserve_lru = false;
	mutex_init(&man->io_reserve_mutex);
	INIT_LIST_HEAD(&man->io_reserve_lru);

	ret = bdev->driver->init_mem_type(bdev, type, man);
	if (ret)
		return ret;
	man->bdev = bdev;

	ret = 0;
	if (type != TTM_PL_SYSTEM) {
		ret = (*man->func->init)(man, p_size);
		if (ret)
			return ret;
	}
	man->has_type = true;
	man->use_type = true;
	man->size = p_size;

	INIT_LIST_HEAD(&man->lru);

	return 0;
}
EXPORT_SYMBOL(ttm_bo_init_mm);

static void ttm_bo_global_kobj_release(struct kobject *kobj)
{
	struct ttm_bo_global *glob =
		container_of(kobj, struct ttm_bo_global, kobj);

	ttm_mem_unregister_shrink(glob->mem_glob, &glob->shrink);
	__free_page(glob->dummy_read_page);
	kfree(glob);
}

void ttm_bo_global_release(struct drm_global_reference *ref)
{
	struct ttm_bo_global *glob = ref->object;

	kobject_del(&glob->kobj);
	kobject_put(&glob->kobj);
}
EXPORT_SYMBOL(ttm_bo_global_release);

int ttm_bo_global_init(struct drm_global_reference *ref)
{
	struct ttm_bo_global_ref *bo_ref =
		container_of(ref, struct ttm_bo_global_ref, ref);
	struct ttm_bo_global *glob = ref->object;
	int ret;

	mutex_init(&glob->device_list_mutex);
	spin_lock_init(&glob->lru_lock);
	glob->mem_glob = bo_ref->mem_glob;
	glob->dummy_read_page = alloc_page(__GFP_ZERO | GFP_DMA32);

	if (unlikely(glob->dummy_read_page == NULL)) {
		ret = -ENOMEM;
		goto out_no_drp;
	}

	INIT_LIST_HEAD(&glob->swap_lru);
	INIT_LIST_HEAD(&glob->device_list);

	ttm_mem_init_shrink(&glob->shrink, ttm_bo_swapout);
	ret = ttm_mem_register_shrink(glob->mem_glob, &glob->shrink);
	if (unlikely(ret != 0)) {
		pr_err("Could not register buffer object swapout\n");
		goto out_no_shrink;
	}

	atomic_set(&glob->bo_count, 0);

	ret = kobject_init_and_add(
		&glob->kobj, &ttm_bo_glob_kobj_type, ttm_get_kobj(), "buffer_objects");
	if (unlikely(ret != 0))
		kobject_put(&glob->kobj);
	return ret;
out_no_shrink:
	__free_page(glob->dummy_read_page);
out_no_drp:
	kfree(glob);
	return ret;
}
EXPORT_SYMBOL(ttm_bo_global_init);


int ttm_bo_device_release(struct ttm_bo_device *bdev)
{
	int ret = 0;
	unsigned i = TTM_NUM_MEM_TYPES;
	struct ttm_mem_type_manager *man;
	struct ttm_bo_global *glob = bdev->glob;

	while (i--) {
		man = &bdev->man[i];
		if (man->has_type) {
			man->use_type = false;
			if ((i != TTM_PL_SYSTEM) && ttm_bo_clean_mm(bdev, i)) {
				ret = -EBUSY;
				pr_err("DRM memory manager type %d is not clean\n",
				       i);
			}
			man->has_type = false;
		}
	}

	mutex_lock(&glob->device_list_mutex);
	list_del(&bdev->device_list);
	mutex_unlock(&glob->device_list_mutex);

	cancel_delayed_work_sync(&bdev->wq);

	while (ttm_bo_delayed_delete(bdev, true))
		;

	spin_lock(&glob->lru_lock);
	if (list_empty(&bdev->ddestroy))
		TTM_DEBUG("Delayed destroy list was clean\n");

	if (list_empty(&bdev->man[0].lru))
		TTM_DEBUG("Swap list was clean\n");
	spin_unlock(&glob->lru_lock);

	BUG_ON(!drm_mm_clean(&bdev->addr_space_mm));
	write_lock(&bdev->vm_lock);
	drm_mm_takedown(&bdev->addr_space_mm);
	write_unlock(&bdev->vm_lock);

	return ret;
}
EXPORT_SYMBOL(ttm_bo_device_release);

int ttm_bo_device_init(struct ttm_bo_device *bdev,
		       struct ttm_bo_global *glob,
		       struct ttm_bo_driver *driver,
		       uint64_t file_page_offset,
		       bool need_dma32)
{
	int ret = -EINVAL;

	rwlock_init(&bdev->vm_lock);
	bdev->driver = driver;

	memset(bdev->man, 0, sizeof(bdev->man));

	/*
	 * Initialize the system memory buffer type.
	 * Other types need to be driver / IOCTL initialized.
	 */
	ret = ttm_bo_init_mm(bdev, TTM_PL_SYSTEM, 0);
	if (unlikely(ret != 0))
		goto out_no_sys;

	bdev->addr_space_rb = RB_ROOT;
	ret = drm_mm_init(&bdev->addr_space_mm, file_page_offset, 0x10000000);
	if (unlikely(ret != 0))
		goto out_no_addr_mm;

	INIT_DELAYED_WORK(&bdev->wq, ttm_bo_delayed_workqueue);
	INIT_LIST_HEAD(&bdev->ddestroy);
	bdev->dev_mapping = NULL;
	bdev->glob = glob;
	bdev->need_dma32 = need_dma32;
	bdev->val_seq = 0;
	spin_lock_init(&bdev->fence_lock);
	mutex_lock(&glob->device_list_mutex);
	list_add_tail(&bdev->device_list, &glob->device_list);
	mutex_unlock(&glob->device_list_mutex);

	return 0;
out_no_addr_mm:
	ttm_bo_clean_mm(bdev, 0);
out_no_sys:
	return ret;
}
EXPORT_SYMBOL(ttm_bo_device_init);

/*
 * buffer object vm functions.
 */

bool ttm_mem_reg_is_pci(struct ttm_bo_device *bdev, struct ttm_mem_reg *mem)
{
	struct ttm_mem_type_manager *man = &bdev->man[mem->mem_type];

	if (!(man->flags & TTM_MEMTYPE_FLAG_FIXED)) {
		if (mem->mem_type == TTM_PL_SYSTEM)
			return false;

		if (man->flags & TTM_MEMTYPE_FLAG_CMA)
			return false;

		if (mem->placement & TTM_PL_FLAG_CACHED)
			return false;
	}
	return true;
}

void ttm_bo_unmap_virtual_locked(struct ttm_buffer_object *bo)
{
	struct ttm_bo_device *bdev = bo->bdev;
	loff_t offset = (loff_t) bo->addr_space_offset;
	loff_t holelen = ((loff_t) bo->mem.num_pages) << PAGE_SHIFT;

	if (!bdev->dev_mapping)
		return;
	unmap_mapping_range(bdev->dev_mapping, offset, holelen, 1);
	ttm_mem_io_free_vm(bo);
}

void ttm_bo_unmap_virtual(struct ttm_buffer_object *bo)
{
	struct ttm_bo_device *bdev = bo->bdev;
	struct ttm_mem_type_manager *man = &bdev->man[bo->mem.mem_type];

	ttm_mem_io_lock(man, false);
	ttm_bo_unmap_virtual_locked(bo);
	ttm_mem_io_unlock(man);
}


EXPORT_SYMBOL(ttm_bo_unmap_virtual);

static void ttm_bo_vm_insert_rb(struct ttm_buffer_object *bo)
{
	struct ttm_bo_device *bdev = bo->bdev;
	struct rb_node **cur = &bdev->addr_space_rb.rb_node;
	struct rb_node *parent = NULL;
	struct ttm_buffer_object *cur_bo;
	unsigned long offset = bo->vm_node->start;
	unsigned long cur_offset;

	while (*cur) {
		parent = *cur;
		cur_bo = rb_entry(parent, struct ttm_buffer_object, vm_rb);
		cur_offset = cur_bo->vm_node->start;
		if (offset < cur_offset)
			cur = &parent->rb_left;
		else if (offset > cur_offset)
			cur = &parent->rb_right;
		else
			BUG();
	}

	rb_link_node(&bo->vm_rb, parent, cur);
	rb_insert_color(&bo->vm_rb, &bdev->addr_space_rb);
}

/**
 * ttm_bo_setup_vm:
 *
 * @bo: the buffer to allocate address space for
 *
 * Allocate address space in the drm device so that applications
 * can mmap the buffer and access the contents. This only
 * applies to ttm_bo_type_device objects as others are not
 * placed in the drm device address space.
 */

static int ttm_bo_setup_vm(struct ttm_buffer_object *bo)
{
	struct ttm_bo_device *bdev = bo->bdev;
	int ret;

retry_pre_get:
	ret = drm_mm_pre_get(&bdev->addr_space_mm);
	if (unlikely(ret != 0))
		return ret;

	write_lock(&bdev->vm_lock);
	bo->vm_node = drm_mm_search_free(&bdev->addr_space_mm,
					 bo->mem.num_pages, 0, 0);

	if (unlikely(bo->vm_node == NULL)) {
		ret = -ENOMEM;
		goto out_unlock;
	}

	bo->vm_node = drm_mm_get_block_atomic(bo->vm_node,
					      bo->mem.num_pages, 0);

	if (unlikely(bo->vm_node == NULL)) {
		write_unlock(&bdev->vm_lock);
		goto retry_pre_get;
	}

	ttm_bo_vm_insert_rb(bo);
	write_unlock(&bdev->vm_lock);
	bo->addr_space_offset = ((uint64_t) bo->vm_node->start) << PAGE_SHIFT;

	return 0;
out_unlock:
	write_unlock(&bdev->vm_lock);
	return ret;
}

int ttm_bo_wait(struct ttm_buffer_object *bo,
		bool lazy, bool interruptible, bool no_wait)
{
	struct ttm_bo_driver *driver = bo->bdev->driver;
	struct ttm_bo_device *bdev = bo->bdev;
	void *sync_obj;
	int ret = 0;

	if (likely(bo->sync_obj == NULL))
		return 0;

	while (bo->sync_obj) {

		if (driver->sync_obj_signaled(bo->sync_obj)) {
			void *tmp_obj = bo->sync_obj;
			bo->sync_obj = NULL;
			clear_bit(TTM_BO_PRIV_FLAG_MOVING, &bo->priv_flags);
			spin_unlock(&bdev->fence_lock);
			driver->sync_obj_unref(&tmp_obj);
			spin_lock(&bdev->fence_lock);
			continue;
		}

		if (no_wait)
			return -EBUSY;

		sync_obj = driver->sync_obj_ref(bo->sync_obj);
		spin_unlock(&bdev->fence_lock);
		ret = driver->sync_obj_wait(sync_obj,
					    lazy, interruptible);
		if (unlikely(ret != 0)) {
			driver->sync_obj_unref(&sync_obj);
			spin_lock(&bdev->fence_lock);
			return ret;
		}
		spin_lock(&bdev->fence_lock);
		if (likely(bo->sync_obj == sync_obj)) {
			void *tmp_obj = bo->sync_obj;
			bo->sync_obj = NULL;
			clear_bit(TTM_BO_PRIV_FLAG_MOVING,
				  &bo->priv_flags);
			spin_unlock(&bdev->fence_lock);
			driver->sync_obj_unref(&sync_obj);
			driver->sync_obj_unref(&tmp_obj);
			spin_lock(&bdev->fence_lock);
		} else {
			spin_unlock(&bdev->fence_lock);
			driver->sync_obj_unref(&sync_obj);
			spin_lock(&bdev->fence_lock);
		}
	}
	return 0;
}
EXPORT_SYMBOL(ttm_bo_wait);

int ttm_bo_synccpu_write_grab(struct ttm_buffer_object *bo, bool no_wait)
{
	struct ttm_bo_device *bdev = bo->bdev;
	int ret = 0;

	/*
	 * Using ttm_bo_reserve makes sure the lru lists are updated.
	 */

	ret = ttm_bo_reserve(bo, true, no_wait, false, 0);
	if (unlikely(ret != 0))
		return ret;
	spin_lock(&bdev->fence_lock);
	ret = ttm_bo_wait(bo, false, true, no_wait);
	spin_unlock(&bdev->fence_lock);
	if (likely(ret == 0))
		atomic_inc(&bo->cpu_writers);
	ttm_bo_unreserve(bo);
	return ret;
}
EXPORT_SYMBOL(ttm_bo_synccpu_write_grab);

void ttm_bo_synccpu_write_release(struct ttm_buffer_object *bo)
{
	atomic_dec(&bo->cpu_writers);
}
EXPORT_SYMBOL(ttm_bo_synccpu_write_release);

/**
 * A buffer object shrink method that tries to swap out the first
 * buffer object on the bo_global::swap_lru list.
 */

static int ttm_bo_swapout(struct ttm_mem_shrink *shrink)
{
	struct ttm_bo_global *glob =
	    container_of(shrink, struct ttm_bo_global, shrink);
	struct ttm_buffer_object *bo;
	int ret = -EBUSY;
	int put_count;
	uint32_t swap_placement = (TTM_PL_FLAG_CACHED | TTM_PL_FLAG_SYSTEM);

	spin_lock(&glob->lru_lock);
	list_for_each_entry(bo, &glob->swap_lru, swap) {
		ret = ttm_bo_reserve_nolru(bo, false, true, false, 0);
		if (!ret)
			break;
	}

	if (ret) {
		spin_unlock(&glob->lru_lock);
		return ret;
	}

	kref_get(&bo->list_kref);

	if (!list_empty(&bo->ddestroy)) {
		ret = ttm_bo_cleanup_refs_and_unlock(bo, false, false);
		kref_put(&bo->list_kref, ttm_bo_release_list);
		return ret;
	}

	put_count = ttm_bo_del_from_lru(bo);
	spin_unlock(&glob->lru_lock);

	ttm_bo_list_ref_sub(bo, put_count, true);

	/**
	 * Wait for GPU, then move to system cached.
	 */

	spin_lock(&bo->bdev->fence_lock);
	ret = ttm_bo_wait(bo, false, false, false);
	spin_unlock(&bo->bdev->fence_lock);

	if (unlikely(ret != 0))
		goto out;

	if ((bo->mem.placement & swap_placement) != swap_placement) {
		struct ttm_mem_reg evict_mem;

		evict_mem = bo->mem;
		evict_mem.mm_node = NULL;
		evict_mem.placement = TTM_PL_FLAG_SYSTEM | TTM_PL_FLAG_CACHED;
		evict_mem.mem_type = TTM_PL_SYSTEM;

		ret = ttm_bo_handle_move_mem(bo, &evict_mem, true,
					     false, false);
		if (unlikely(ret != 0))
			goto out;
	}

	ttm_bo_unmap_virtual(bo);

	/**
	 * Swap out. Buffer will be swapped in again as soon as
	 * anyone tries to access a ttm page.
	 */

	if (bo->bdev->driver->swap_notify)
		bo->bdev->driver->swap_notify(bo);

	ret = ttm_tt_swapout(bo->ttm, bo->persistent_swap_storage);
out:

	/**
	 *
	 * Unreserve without putting on LRU to avoid swapping out an
	 * already swapped buffer.
	 */

	atomic_set(&bo->reserved, 0);
	wake_up_all(&bo->event_queue);
	kref_put(&bo->list_kref, ttm_bo_release_list);
	return ret;
}

void ttm_bo_swapout_all(struct ttm_bo_device *bdev)
{
	while (ttm_bo_swapout(&bdev->glob->shrink) == 0)
		;
}
EXPORT_SYMBOL(ttm_bo_swapout_all);
