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
#include <linux/reservation.h>

#define TTM_ASSERT_LOCKED(param)
#define TTM_DEBUG(fmt, arg...)
#define TTM_BO_HASH_ORDER 13

static int ttm_bo_swapout(struct ttm_mem_shrink *shrink);
static void ttm_bo_global_kobj_release(struct kobject *kobj);

static struct attribute ttm_bo_count = {
	.name = "bo_count",
	.mode = S_IRUGO
};

static inline int ttm_mem_type_from_place(const struct ttm_place *place,
					  uint32_t *mem_type)
{
	int pos;

	pos = ffs(place->flags & TTM_PL_MASK_MEM);
	if (unlikely(!pos))
		return -EINVAL;

	*mem_type = pos - 1;
	return 0;
}

static void ttm_mem_type_debug(struct ttm_bo_device *bdev, int mem_type)
{
	struct ttm_mem_type_manager *man = &bdev->man[mem_type];
	struct drm_printer p = drm_debug_printer(TTM_PFX);

	pr_err("    has_type: %d\n", man->has_type);
	pr_err("    use_type: %d\n", man->use_type);
	pr_err("    flags: 0x%08X\n", man->flags);
	pr_err("    gpu_offset: 0x%08llX\n", man->gpu_offset);
	pr_err("    size: %llu\n", man->size);
	pr_err("    available_caching: 0x%08X\n", man->available_caching);
	pr_err("    default_caching: 0x%08X\n", man->default_caching);
	if (mem_type != TTM_PL_SYSTEM)
		(*man->func->debug)(man, &p);
}

static void ttm_bo_mem_space_debug(struct ttm_buffer_object *bo,
					struct ttm_placement *placement)
{
	int i, ret, mem_type;

	pr_err("No space for %p (%lu pages, %luK, %luM)\n",
	       bo, bo->mem.num_pages, bo->mem.size >> 10,
	       bo->mem.size >> 20);
	for (i = 0; i < placement->num_placement; i++) {
		ret = ttm_mem_type_from_place(&placement->placement[i],
						&mem_type);
		if (ret)
			return;
		pr_err("  placement[%d]=0x%08X (%d)\n",
		       i, placement->placement[i].flags, mem_type);
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

	BUG_ON(kref_read(&bo->list_kref));
	BUG_ON(kref_read(&bo->kref));
	BUG_ON(atomic_read(&bo->cpu_writers));
	BUG_ON(bo->mem.mm_node != NULL);
	BUG_ON(!list_empty(&bo->lru));
	BUG_ON(!list_empty(&bo->ddestroy));
	ttm_tt_destroy(bo->ttm);
	atomic_dec(&bo->glob->bo_count);
	dma_fence_put(bo->moving);
	if (bo->resv == &bo->ttm_resv)
		reservation_object_fini(&bo->ttm_resv);
	mutex_destroy(&bo->wu_mutex);
	if (bo->destroy)
		bo->destroy(bo);
	else {
		kfree(bo);
	}
	ttm_mem_global_free(bdev->glob->mem_glob, acc_size);
}

void ttm_bo_add_to_lru(struct ttm_buffer_object *bo)
{
	struct ttm_bo_device *bdev = bo->bdev;
	struct ttm_mem_type_manager *man;

	lockdep_assert_held(&bo->resv->lock.base);

	if (!(bo->mem.placement & TTM_PL_FLAG_NO_EVICT)) {

		BUG_ON(!list_empty(&bo->lru));

		man = &bdev->man[bo->mem.mem_type];
		list_add_tail(&bo->lru, &man->lru[bo->priority]);
		kref_get(&bo->list_kref);

		if (bo->ttm && !(bo->ttm->page_flags & TTM_PAGE_FLAG_SG)) {
			list_add_tail(&bo->swap,
				      &bo->glob->swap_lru[bo->priority]);
			kref_get(&bo->list_kref);
		}
	}
}
EXPORT_SYMBOL(ttm_bo_add_to_lru);

static void ttm_bo_ref_bug(struct kref *list_kref)
{
	BUG();
}

void ttm_bo_del_from_lru(struct ttm_buffer_object *bo)
{
	if (!list_empty(&bo->swap)) {
		list_del_init(&bo->swap);
		kref_put(&bo->list_kref, ttm_bo_ref_bug);
	}
	if (!list_empty(&bo->lru)) {
		list_del_init(&bo->lru);
		kref_put(&bo->list_kref, ttm_bo_ref_bug);
	}

	/*
	 * TODO: Add a driver hook to delete from
	 * driver-specific LRU's here.
	 */
}

void ttm_bo_del_sub_from_lru(struct ttm_buffer_object *bo)
{
	spin_lock(&bo->glob->lru_lock);
	ttm_bo_del_from_lru(bo);
	spin_unlock(&bo->glob->lru_lock);
}
EXPORT_SYMBOL(ttm_bo_del_sub_from_lru);

void ttm_bo_move_to_lru_tail(struct ttm_buffer_object *bo)
{
	lockdep_assert_held(&bo->resv->lock.base);

	ttm_bo_del_from_lru(bo);
	ttm_bo_add_to_lru(bo);
}
EXPORT_SYMBOL(ttm_bo_move_to_lru_tail);

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
				bdev->driver->move_notify(bo, evict, mem);
			bo->mem = *mem;
			mem->mm_node = NULL;
			goto moved;
		}
	}

	if (bdev->driver->move_notify)
		bdev->driver->move_notify(bo, evict, mem);

	if (!(old_man->flags & TTM_MEMTYPE_FLAG_FIXED) &&
	    !(new_man->flags & TTM_MEMTYPE_FLAG_FIXED))
		ret = ttm_bo_move_ttm(bo, interruptible, no_wait_gpu, mem);
	else if (bdev->driver->move)
		ret = bdev->driver->move(bo, evict, interruptible,
					 no_wait_gpu, mem);
	else
		ret = ttm_bo_move_memcpy(bo, interruptible, no_wait_gpu, mem);

	if (ret) {
		if (bdev->driver->move_notify) {
			struct ttm_mem_reg tmp_mem = *mem;
			*mem = bo->mem;
			bo->mem = tmp_mem;
			bdev->driver->move_notify(bo, false, mem);
			bo->mem = *mem;
			*mem = tmp_mem;
		}

		goto out_err;
	}

moved:
	if (bo->evicted) {
		if (bdev->driver->invalidate_caches) {
			ret = bdev->driver->invalidate_caches(bdev, bo->mem.placement);
			if (ret)
				pr_err("Can not flush read caches\n");
		}
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
	if (new_man->flags & TTM_MEMTYPE_FLAG_FIXED) {
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
		bo->bdev->driver->move_notify(bo, false, NULL);

	ttm_tt_destroy(bo->ttm);
	bo->ttm = NULL;
	ttm_bo_mem_put(bo, &bo->mem);

	ww_mutex_unlock (&bo->resv->lock);
}

static int ttm_bo_individualize_resv(struct ttm_buffer_object *bo)
{
	int r;

	if (bo->resv == &bo->ttm_resv)
		return 0;

	reservation_object_init(&bo->ttm_resv);
	BUG_ON(!reservation_object_trylock(&bo->ttm_resv));

	r = reservation_object_copy_fences(&bo->ttm_resv, bo->resv);
	if (r) {
		reservation_object_unlock(&bo->ttm_resv);
		reservation_object_fini(&bo->ttm_resv);
	}

	return r;
}

static void ttm_bo_flush_all_fences(struct ttm_buffer_object *bo)
{
	struct reservation_object_list *fobj;
	struct dma_fence *fence;
	int i;

	fobj = reservation_object_get_list(&bo->ttm_resv);
	fence = reservation_object_get_excl(&bo->ttm_resv);
	if (fence && !fence->ops->signaled)
		dma_fence_enable_sw_signaling(fence);

	for (i = 0; fobj && i < fobj->shared_count; ++i) {
		fence = rcu_dereference_protected(fobj->shared[i],
					reservation_object_held(bo->resv));

		if (!fence->ops->signaled)
			dma_fence_enable_sw_signaling(fence);
	}
}

static void ttm_bo_cleanup_refs_or_queue(struct ttm_buffer_object *bo)
{
	struct ttm_bo_device *bdev = bo->bdev;
	struct ttm_bo_global *glob = bo->glob;
	int ret;

	spin_lock(&glob->lru_lock);
	ret = __ttm_bo_reserve(bo, false, true, NULL);

	if (!ret) {
		if (!ttm_bo_wait(bo, false, true)) {
			ttm_bo_del_from_lru(bo);
			spin_unlock(&glob->lru_lock);
			ttm_bo_cleanup_memtype_use(bo);

			return;
		}

		ret = ttm_bo_individualize_resv(bo);
		if (ret) {
			/* Last resort, if we fail to allocate memory for the
			 * fences block for the BO to become idle and free it.
			 */
			spin_unlock(&glob->lru_lock);
			ttm_bo_wait(bo, true, true);
			ttm_bo_cleanup_memtype_use(bo);
			return;
		}
		ttm_bo_flush_all_fences(bo);

		/*
		 * Make NO_EVICT bos immediately available to
		 * shrinkers, now that they are queued for
		 * destruction.
		 */
		if (bo->mem.placement & TTM_PL_FLAG_NO_EVICT) {
			bo->mem.placement &= ~TTM_PL_FLAG_NO_EVICT;
			ttm_bo_add_to_lru(bo);
		}

		if (bo->resv != &bo->ttm_resv)
			reservation_object_unlock(&bo->ttm_resv);
		__ttm_bo_unreserve(bo);
	}

	kref_get(&bo->list_kref);
	list_add_tail(&bo->ddestroy, &bdev->ddestroy);
	spin_unlock(&glob->lru_lock);

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
	struct ttm_bo_global *glob = bo->glob;
	struct reservation_object *resv;
	int ret;

	if (unlikely(list_empty(&bo->ddestroy)))
		resv = bo->resv;
	else
		resv = &bo->ttm_resv;

	if (reservation_object_test_signaled_rcu(resv, true))
		ret = 0;
	else
		ret = -EBUSY;

	if (ret && !no_wait_gpu) {
		long lret;
		ww_mutex_unlock(&bo->resv->lock);
		spin_unlock(&glob->lru_lock);

		lret = reservation_object_wait_timeout_rcu(resv, true,
							   interruptible,
							   30 * HZ);

		if (lret < 0)
			return lret;
		else if (lret == 0)
			return -EBUSY;

		spin_lock(&glob->lru_lock);
		ret = __ttm_bo_reserve(bo, false, true, NULL);

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
	}

	if (ret || unlikely(list_empty(&bo->ddestroy))) {
		__ttm_bo_unreserve(bo);
		spin_unlock(&glob->lru_lock);
		return ret;
	}

	ttm_bo_del_from_lru(bo);
	list_del_init(&bo->ddestroy);
	kref_put(&bo->list_kref, ttm_bo_ref_bug);

	spin_unlock(&glob->lru_lock);
	ttm_bo_cleanup_memtype_use(bo);

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

		ret = __ttm_bo_reserve(entry, false, true, NULL);
		if (remove_all && ret) {
			spin_unlock(&glob->lru_lock);
			ret = __ttm_bo_reserve(entry, false, false, NULL);
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

	drm_vma_offset_remove(&bdev->vma_manager, &bo->vma_node);
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

	lockdep_assert_held(&bo->resv->lock.base);

	evict_mem = bo->mem;
	evict_mem.mm_node = NULL;
	evict_mem.bus.io_reserved_vm = false;
	evict_mem.bus.io_reserved_count = 0;

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
	if (unlikely(ret)) {
		if (ret != -ERESTARTSYS)
			pr_err("Buffer eviction failed\n");
		ttm_bo_mem_put(bo, &evict_mem);
		goto out;
	}
	bo->evicted = true;
out:
	return ret;
}

bool ttm_bo_eviction_valuable(struct ttm_buffer_object *bo,
			      const struct ttm_place *place)
{
	/* Don't evict this BO if it's outside of the
	 * requested placement range
	 */
	if (place->fpfn >= (bo->mem.start + bo->mem.size) ||
	    (place->lpfn && place->lpfn <= bo->mem.start))
		return false;

	return true;
}
EXPORT_SYMBOL(ttm_bo_eviction_valuable);

static int ttm_mem_evict_first(struct ttm_bo_device *bdev,
				uint32_t mem_type,
				const struct ttm_place *place,
				bool interruptible,
				bool no_wait_gpu)
{
	struct ttm_bo_global *glob = bdev->glob;
	struct ttm_mem_type_manager *man = &bdev->man[mem_type];
	struct ttm_buffer_object *bo;
	int ret = -EBUSY;
	unsigned i;

	spin_lock(&glob->lru_lock);
	for (i = 0; i < TTM_MAX_BO_PRIORITY; ++i) {
		list_for_each_entry(bo, &man->lru[i], lru) {
			ret = __ttm_bo_reserve(bo, false, true, NULL);
			if (ret)
				continue;

			if (place && !bdev->driver->eviction_valuable(bo,
								      place)) {
				__ttm_bo_unreserve(bo);
				ret = -EBUSY;
				continue;
			}

			break;
		}

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

	ttm_bo_del_from_lru(bo);
	spin_unlock(&glob->lru_lock);

	BUG_ON(ret != 0);

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
 * Add the last move fence to the BO and reserve a new shared slot.
 */
static int ttm_bo_add_move_fence(struct ttm_buffer_object *bo,
				 struct ttm_mem_type_manager *man,
				 struct ttm_mem_reg *mem)
{
	struct dma_fence *fence;
	int ret;

	spin_lock(&man->move_lock);
	fence = dma_fence_get(man->move);
	spin_unlock(&man->move_lock);

	if (fence) {
		reservation_object_add_shared_fence(bo->resv, fence);

		ret = reservation_object_reserve_shared(bo->resv);
		if (unlikely(ret))
			return ret;

		dma_fence_put(bo->moving);
		bo->moving = fence;
	}

	return 0;
}

/**
 * Repeatedly evict memory from the LRU for @mem_type until we create enough
 * space, or we've evicted everything and there isn't enough space.
 */
static int ttm_bo_mem_force_space(struct ttm_buffer_object *bo,
					uint32_t mem_type,
					const struct ttm_place *place,
					struct ttm_mem_reg *mem,
					bool interruptible,
					bool no_wait_gpu)
{
	struct ttm_bo_device *bdev = bo->bdev;
	struct ttm_mem_type_manager *man = &bdev->man[mem_type];
	int ret;

	do {
		ret = (*man->func->get_node)(man, bo, place, mem);
		if (unlikely(ret != 0))
			return ret;
		if (mem->mm_node)
			break;
		ret = ttm_mem_evict_first(bdev, mem_type, place,
					  interruptible, no_wait_gpu);
		if (unlikely(ret != 0))
			return ret;
	} while (1);
	mem->mem_type = mem_type;
	return ttm_bo_add_move_fence(bo, man, mem);
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
				 const struct ttm_place *place,
				 uint32_t *masked_placement)
{
	uint32_t cur_flags = ttm_bo_type_flags(mem_type);

	if ((cur_flags & place->flags & TTM_PL_MASK_MEM) == 0)
		return false;

	if ((place->flags & man->available_caching) == 0)
		return false;

	cur_flags |= (place->flags & man->available_caching);

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

	ret = reservation_object_reserve_shared(bo->resv);
	if (unlikely(ret))
		return ret;

	mem->mm_node = NULL;
	for (i = 0; i < placement->num_placement; ++i) {
		const struct ttm_place *place = &placement->placement[i];

		ret = ttm_mem_type_from_place(place, &mem_type);
		if (ret)
			return ret;
		man = &bdev->man[mem_type];
		if (!man->has_type || !man->use_type)
			continue;

		type_ok = ttm_bo_mt_compatible(man, mem_type, place,
						&cur_flags);

		if (!type_ok)
			continue;

		type_found = true;
		cur_flags = ttm_bo_select_caching(man, bo->mem.placement,
						  cur_flags);
		/*
		 * Use the access and other non-mapping-related flag bits from
		 * the memory placement flags to the current flags
		 */
		ttm_flag_masked(&cur_flags, place->flags,
				~TTM_PL_MASK_MEMTYPE);

		if (mem_type == TTM_PL_SYSTEM)
			break;

		ret = (*man->func->get_node)(man, bo, place, mem);
		if (unlikely(ret))
			return ret;

		if (mem->mm_node) {
			ret = ttm_bo_add_move_fence(bo, man, mem);
			if (unlikely(ret)) {
				(*man->func->put_node)(man, mem);
				return ret;
			}
			break;
		}
	}

	if ((type_ok && (mem_type == TTM_PL_SYSTEM)) || mem->mm_node) {
		mem->mem_type = mem_type;
		mem->placement = cur_flags;
		return 0;
	}

	for (i = 0; i < placement->num_busy_placement; ++i) {
		const struct ttm_place *place = &placement->busy_placement[i];

		ret = ttm_mem_type_from_place(place, &mem_type);
		if (ret)
			return ret;
		man = &bdev->man[mem_type];
		if (!man->has_type || !man->use_type)
			continue;
		if (!ttm_bo_mt_compatible(man, mem_type, place, &cur_flags))
			continue;

		type_found = true;
		cur_flags = ttm_bo_select_caching(man, bo->mem.placement,
						  cur_flags);
		/*
		 * Use the access and other non-mapping-related flag bits from
		 * the memory placement flags to the current flags
		 */
		ttm_flag_masked(&cur_flags, place->flags,
				~TTM_PL_MASK_MEMTYPE);

		if (mem_type == TTM_PL_SYSTEM) {
			mem->mem_type = mem_type;
			mem->placement = cur_flags;
			mem->mm_node = NULL;
			return 0;
		}

		ret = ttm_bo_mem_force_space(bo, mem_type, place, mem,
						interruptible, no_wait_gpu);
		if (ret == 0 && mem->mm_node) {
			mem->placement = cur_flags;
			return 0;
		}
		if (ret == -ERESTARTSYS)
			has_erestartsys = true;
	}

	if (!type_found) {
		pr_err(TTM_PFX "No compatible memory type found\n");
		return -EINVAL;
	}

	return (has_erestartsys) ? -ERESTARTSYS : -ENOMEM;
}
EXPORT_SYMBOL(ttm_bo_mem_space);

static int ttm_bo_move_buffer(struct ttm_buffer_object *bo,
			struct ttm_placement *placement,
			bool interruptible,
			bool no_wait_gpu)
{
	int ret = 0;
	struct ttm_mem_reg mem;

	lockdep_assert_held(&bo->resv->lock.base);

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

static bool ttm_bo_places_compat(const struct ttm_place *places,
				 unsigned num_placement,
				 struct ttm_mem_reg *mem,
				 uint32_t *new_flags)
{
	unsigned i;

	for (i = 0; i < num_placement; i++) {
		const struct ttm_place *heap = &places[i];

		if (mem->mm_node && (mem->start < heap->fpfn ||
		     (heap->lpfn != 0 && (mem->start + mem->num_pages) > heap->lpfn)))
			continue;

		*new_flags = heap->flags;
		if ((*new_flags & mem->placement & TTM_PL_MASK_CACHING) &&
		    (*new_flags & mem->placement & TTM_PL_MASK_MEM) &&
		    (!(*new_flags & TTM_PL_FLAG_CONTIGUOUS) ||
		     (mem->placement & TTM_PL_FLAG_CONTIGUOUS)))
			return true;
	}
	return false;
}

bool ttm_bo_mem_compat(struct ttm_placement *placement,
		       struct ttm_mem_reg *mem,
		       uint32_t *new_flags)
{
	if (ttm_bo_places_compat(placement->placement, placement->num_placement,
				 mem, new_flags))
		return true;

	if ((placement->busy_placement != placement->placement ||
	     placement->num_busy_placement > placement->num_placement) &&
	    ttm_bo_places_compat(placement->busy_placement,
				 placement->num_busy_placement,
				 mem, new_flags))
		return true;

	return false;
}
EXPORT_SYMBOL(ttm_bo_mem_compat);

int ttm_bo_validate(struct ttm_buffer_object *bo,
			struct ttm_placement *placement,
			bool interruptible,
			bool no_wait_gpu)
{
	int ret;
	uint32_t new_flags;

	lockdep_assert_held(&bo->resv->lock.base);
	/*
	 * Check whether we need to move buffer.
	 */
	if (!ttm_bo_mem_compat(placement, &bo->mem, &new_flags)) {
		ret = ttm_bo_move_buffer(bo, placement, interruptible,
					 no_wait_gpu);
		if (ret)
			return ret;
	} else {
		/*
		 * Use the access and other non-mapping-related flag bits from
		 * the compatible memory placement flags to the active flags
		 */
		ttm_flag_masked(&bo->mem.placement, new_flags,
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

int ttm_bo_init_reserved(struct ttm_bo_device *bdev,
			 struct ttm_buffer_object *bo,
			 unsigned long size,
			 enum ttm_bo_type type,
			 struct ttm_placement *placement,
			 uint32_t page_alignment,
			 bool interruptible,
			 struct file *persistent_swap_storage,
			 size_t acc_size,
			 struct sg_table *sg,
			 struct reservation_object *resv,
			 void (*destroy) (struct ttm_buffer_object *))
{
	int ret = 0;
	unsigned long num_pages;
	struct ttm_mem_global *mem_glob = bdev->glob->mem_glob;
	bool locked;

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
	INIT_LIST_HEAD(&bo->lru);
	INIT_LIST_HEAD(&bo->ddestroy);
	INIT_LIST_HEAD(&bo->swap);
	INIT_LIST_HEAD(&bo->io_reserve_lru);
	mutex_init(&bo->wu_mutex);
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
	bo->moving = NULL;
	bo->mem.placement = (TTM_PL_FLAG_SYSTEM | TTM_PL_FLAG_CACHED);
	bo->persistent_swap_storage = persistent_swap_storage;
	bo->acc_size = acc_size;
	bo->sg = sg;
	if (resv) {
		bo->resv = resv;
		lockdep_assert_held(&bo->resv->lock.base);
	} else {
		bo->resv = &bo->ttm_resv;
		reservation_object_init(&bo->ttm_resv);
	}
	atomic_inc(&bo->glob->bo_count);
	drm_vma_node_reset(&bo->vma_node);
	bo->priority = 0;

	/*
	 * For ttm_bo_type_device buffers, allocate
	 * address space from the device.
	 */
	if (bo->type == ttm_bo_type_device ||
	    bo->type == ttm_bo_type_sg)
		ret = drm_vma_offset_add(&bdev->vma_manager, &bo->vma_node,
					 bo->mem.num_pages);

	/* passed reservation objects should already be locked,
	 * since otherwise lockdep will be angered in radeon.
	 */
	if (!resv) {
		locked = ww_mutex_trylock(&bo->resv->lock);
		WARN_ON(!locked);
	}

	if (likely(!ret))
		ret = ttm_bo_validate(bo, placement, interruptible, false);

	if (unlikely(ret)) {
		if (!resv)
			ttm_bo_unreserve(bo);

		ttm_bo_unref(&bo);
		return ret;
	}

	if (resv && !(bo->mem.placement & TTM_PL_FLAG_NO_EVICT)) {
		spin_lock(&bo->glob->lru_lock);
		ttm_bo_add_to_lru(bo);
		spin_unlock(&bo->glob->lru_lock);
	}

	return ret;
}
EXPORT_SYMBOL(ttm_bo_init_reserved);

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
		struct reservation_object *resv,
		void (*destroy) (struct ttm_buffer_object *))
{
	int ret;

	ret = ttm_bo_init_reserved(bdev, bo, size, type, placement,
				   page_alignment, interruptible,
				   persistent_swap_storage, acc_size,
				   sg, resv, destroy);
	if (ret)
		return ret;

	if (!resv)
		ttm_bo_unreserve(bo);

	return 0;
}
EXPORT_SYMBOL(ttm_bo_init);

size_t ttm_bo_acc_size(struct ttm_bo_device *bdev,
		       unsigned long bo_size,
		       unsigned struct_size)
{
	unsigned npages = (PAGE_ALIGN(bo_size)) >> PAGE_SHIFT;
	size_t size = 0;

	size += ttm_round_pot(struct_size);
	size += ttm_round_pot(npages * sizeof(void *));
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
	size += ttm_round_pot(npages * (2*sizeof(void *) + sizeof(dma_addr_t)));
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
			  NULL, NULL, NULL);
	if (likely(ret == 0))
		*p_bo = bo;

	return ret;
}
EXPORT_SYMBOL(ttm_bo_create);

static int ttm_bo_force_list_clean(struct ttm_bo_device *bdev,
				   unsigned mem_type)
{
	struct ttm_mem_type_manager *man = &bdev->man[mem_type];
	struct ttm_bo_global *glob = bdev->glob;
	struct dma_fence *fence;
	int ret;
	unsigned i;

	/*
	 * Can't use standard list traversal since we're unlocking.
	 */

	spin_lock(&glob->lru_lock);
	for (i = 0; i < TTM_MAX_BO_PRIORITY; ++i) {
		while (!list_empty(&man->lru[i])) {
			spin_unlock(&glob->lru_lock);
			ret = ttm_mem_evict_first(bdev, mem_type, NULL, false, false);
			if (ret)
				return ret;
			spin_lock(&glob->lru_lock);
		}
	}
	spin_unlock(&glob->lru_lock);

	spin_lock(&man->move_lock);
	fence = dma_fence_get(man->move);
	spin_unlock(&man->move_lock);

	if (fence) {
		ret = dma_fence_wait(fence, false);
		dma_fence_put(fence);
		if (ret)
			return ret;
	}

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
		ret = ttm_bo_force_list_clean(bdev, mem_type);
		if (ret) {
			pr_err("Cleanup eviction failed\n");
			return ret;
		}

		ret = (*man->func->takedown)(man);
	}

	dma_fence_put(man->move);
	man->move = NULL;

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

	return ttm_bo_force_list_clean(bdev, mem_type);
}
EXPORT_SYMBOL(ttm_bo_evict_mm);

int ttm_bo_init_mm(struct ttm_bo_device *bdev, unsigned type,
			unsigned long p_size)
{
	int ret;
	struct ttm_mem_type_manager *man;
	unsigned i;

	BUG_ON(type >= TTM_NUM_MEM_TYPES);
	man = &bdev->man[type];
	BUG_ON(man->has_type);
	man->io_reserve_fastpath = true;
	man->use_io_reserve_lru = false;
	mutex_init(&man->io_reserve_mutex);
	spin_lock_init(&man->move_lock);
	INIT_LIST_HEAD(&man->io_reserve_lru);

	ret = bdev->driver->init_mem_type(bdev, type, man);
	if (ret)
		return ret;
	man->bdev = bdev;

	if (type != TTM_PL_SYSTEM) {
		ret = (*man->func->init)(man, p_size);
		if (ret)
			return ret;
	}
	man->has_type = true;
	man->use_type = true;
	man->size = p_size;

	for (i = 0; i < TTM_MAX_BO_PRIORITY; ++i)
		INIT_LIST_HEAD(&man->lru[i]);
	man->move = NULL;

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
	unsigned i;

	mutex_init(&glob->device_list_mutex);
	spin_lock_init(&glob->lru_lock);
	glob->mem_glob = bo_ref->mem_glob;
	glob->dummy_read_page = alloc_page(__GFP_ZERO | GFP_DMA32);

	if (unlikely(glob->dummy_read_page == NULL)) {
		ret = -ENOMEM;
		goto out_no_drp;
	}

	for (i = 0; i < TTM_MAX_BO_PRIORITY; ++i)
		INIT_LIST_HEAD(&glob->swap_lru[i]);
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

	for (i = 0; i < TTM_MAX_BO_PRIORITY; ++i)
		if (list_empty(&bdev->man[0].lru[0]))
			TTM_DEBUG("Swap list %d was clean\n", i);
	spin_unlock(&glob->lru_lock);

	drm_vma_offset_manager_destroy(&bdev->vma_manager);

	return ret;
}
EXPORT_SYMBOL(ttm_bo_device_release);

int ttm_bo_device_init(struct ttm_bo_device *bdev,
		       struct ttm_bo_global *glob,
		       struct ttm_bo_driver *driver,
		       struct address_space *mapping,
		       uint64_t file_page_offset,
		       bool need_dma32)
{
	int ret = -EINVAL;

	bdev->driver = driver;

	memset(bdev->man, 0, sizeof(bdev->man));

	/*
	 * Initialize the system memory buffer type.
	 * Other types need to be driver / IOCTL initialized.
	 */
	ret = ttm_bo_init_mm(bdev, TTM_PL_SYSTEM, 0);
	if (unlikely(ret != 0))
		goto out_no_sys;

	drm_vma_offset_manager_init(&bdev->vma_manager, file_page_offset,
				    0x10000000);
	INIT_DELAYED_WORK(&bdev->wq, ttm_bo_delayed_workqueue);
	INIT_LIST_HEAD(&bdev->ddestroy);
	bdev->dev_mapping = mapping;
	bdev->glob = glob;
	bdev->need_dma32 = need_dma32;
	mutex_lock(&glob->device_list_mutex);
	list_add_tail(&bdev->device_list, &glob->device_list);
	mutex_unlock(&glob->device_list_mutex);

	return 0;
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

	drm_vma_node_unmap(&bo->vma_node, bdev->dev_mapping);
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

int ttm_bo_wait(struct ttm_buffer_object *bo,
		bool interruptible, bool no_wait)
{
	long timeout = 15 * HZ;

	if (no_wait) {
		if (reservation_object_test_signaled_rcu(bo->resv, true))
			return 0;
		else
			return -EBUSY;
	}

	timeout = reservation_object_wait_timeout_rcu(bo->resv, true,
						      interruptible, timeout);
	if (timeout < 0)
		return timeout;

	if (timeout == 0)
		return -EBUSY;

	reservation_object_add_excl_fence(bo->resv, NULL);
	return 0;
}
EXPORT_SYMBOL(ttm_bo_wait);

int ttm_bo_synccpu_write_grab(struct ttm_buffer_object *bo, bool no_wait)
{
	int ret = 0;

	/*
	 * Using ttm_bo_reserve makes sure the lru lists are updated.
	 */

	ret = ttm_bo_reserve(bo, true, no_wait, NULL);
	if (unlikely(ret != 0))
		return ret;
	ret = ttm_bo_wait(bo, true, no_wait);
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
	unsigned i;

	spin_lock(&glob->lru_lock);
	for (i = 0; i < TTM_MAX_BO_PRIORITY; ++i) {
		list_for_each_entry(bo, &glob->swap_lru[i], swap) {
			ret = __ttm_bo_reserve(bo, false, true, NULL);
			if (!ret)
				break;
		}
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

	ttm_bo_del_from_lru(bo);
	spin_unlock(&glob->lru_lock);

	/**
	 * Move to system cached
	 */

	if (bo->mem.mem_type != TTM_PL_SYSTEM ||
	    bo->ttm->caching_state != tt_cached) {
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

	/**
	 * Make sure BO is idle.
	 */

	ret = ttm_bo_wait(bo, false, false);
	if (unlikely(ret != 0))
		goto out;

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

	__ttm_bo_unreserve(bo);
	kref_put(&bo->list_kref, ttm_bo_release_list);
	return ret;
}

void ttm_bo_swapout_all(struct ttm_bo_device *bdev)
{
	while (ttm_bo_swapout(&bdev->glob->shrink) == 0)
		;
}
EXPORT_SYMBOL(ttm_bo_swapout_all);

/**
 * ttm_bo_wait_unreserved - interruptible wait for a buffer object to become
 * unreserved
 *
 * @bo: Pointer to buffer
 */
int ttm_bo_wait_unreserved(struct ttm_buffer_object *bo)
{
	int ret;

	/*
	 * In the absense of a wait_unlocked API,
	 * Use the bo::wu_mutex to avoid triggering livelocks due to
	 * concurrent use of this function. Note that this use of
	 * bo::wu_mutex can go away if we change locking order to
	 * mmap_sem -> bo::reserve.
	 */
	ret = mutex_lock_interruptible(&bo->wu_mutex);
	if (unlikely(ret != 0))
		return -ERESTARTSYS;
	if (!ww_mutex_is_locked(&bo->resv->lock))
		goto out_unlock;
	ret = __ttm_bo_reserve(bo, true, false, NULL);
	if (unlikely(ret != 0))
		goto out_unlock;
	__ttm_bo_unreserve(bo);

out_unlock:
	mutex_unlock(&bo->wu_mutex);
	return ret;
}
