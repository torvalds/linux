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
/* Notes:
 *
 * We store bo pointer in drm_mm_node struct so we know which bo own a
 * specific node. There is no protection on the pointer, thus to make
 * sure things don't go berserk you have to access this pointer while
 * holding the global lru lock and make sure anytime you free a node you
 * reset the pointer to NULL.
 */

#include "ttm/ttm_module.h"
#include "ttm/ttm_bo_driver.h"
#include "ttm/ttm_placement.h"
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/file.h>
#include <linux/module.h>

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

	printk(KERN_ERR TTM_PFX "    has_type: %d\n", man->has_type);
	printk(KERN_ERR TTM_PFX "    use_type: %d\n", man->use_type);
	printk(KERN_ERR TTM_PFX "    flags: 0x%08X\n", man->flags);
	printk(KERN_ERR TTM_PFX "    gpu_offset: 0x%08lX\n", man->gpu_offset);
	printk(KERN_ERR TTM_PFX "    io_offset: 0x%08lX\n", man->io_offset);
	printk(KERN_ERR TTM_PFX "    io_size: %ld\n", man->io_size);
	printk(KERN_ERR TTM_PFX "    size: %llu\n", man->size);
	printk(KERN_ERR TTM_PFX "    available_caching: 0x%08X\n",
		man->available_caching);
	printk(KERN_ERR TTM_PFX "    default_caching: 0x%08X\n",
		man->default_caching);
	if (mem_type != TTM_PL_SYSTEM) {
		spin_lock(&bdev->glob->lru_lock);
		drm_mm_debug_table(&man->manager, TTM_PFX);
		spin_unlock(&bdev->glob->lru_lock);
	}
}

static void ttm_bo_mem_space_debug(struct ttm_buffer_object *bo,
					struct ttm_placement *placement)
{
	int i, ret, mem_type;

	printk(KERN_ERR TTM_PFX "No space for %p (%lu pages, %luK, %luM)\n",
		bo, bo->mem.num_pages, bo->mem.size >> 10,
		bo->mem.size >> 20);
	for (i = 0; i < placement->num_placement; i++) {
		ret = ttm_mem_type_from_flags(placement->placement[i],
						&mem_type);
		if (ret)
			return;
		printk(KERN_ERR TTM_PFX "  placement[%d]=0x%08X (%d)\n",
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

static struct sysfs_ops ttm_bo_global_ops = {
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
		ttm_mem_global_free(bdev->glob->mem_glob, bo->acc_size);
		kfree(bo);
	}
}

int ttm_bo_wait_unreserved(struct ttm_buffer_object *bo, bool interruptible)
{

	if (interruptible) {
		int ret = 0;

		ret = wait_event_interruptible(bo->event_queue,
					       atomic_read(&bo->reserved) == 0);
		if (unlikely(ret != 0))
			return ret;
	} else {
		wait_event(bo->event_queue, atomic_read(&bo->reserved) == 0);
	}
	return 0;
}
EXPORT_SYMBOL(ttm_bo_wait_unreserved);

static void ttm_bo_add_to_lru(struct ttm_buffer_object *bo)
{
	struct ttm_bo_device *bdev = bo->bdev;
	struct ttm_mem_type_manager *man;

	BUG_ON(!atomic_read(&bo->reserved));

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

/**
 * Call with the lru_lock held.
 */

static int ttm_bo_del_from_lru(struct ttm_buffer_object *bo)
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

int ttm_bo_reserve_locked(struct ttm_buffer_object *bo,
			  bool interruptible,
			  bool no_wait, bool use_sequence, uint32_t sequence)
{
	struct ttm_bo_global *glob = bo->glob;
	int ret;

	while (unlikely(atomic_cmpxchg(&bo->reserved, 0, 1) != 0)) {
		if (use_sequence && bo->seq_valid &&
			(sequence - bo->val_seq < (1 << 31))) {
			return -EAGAIN;
		}

		if (no_wait)
			return -EBUSY;

		spin_unlock(&glob->lru_lock);
		ret = ttm_bo_wait_unreserved(bo, interruptible);
		spin_lock(&glob->lru_lock);

		if (unlikely(ret))
			return ret;
	}

	if (use_sequence) {
		bo->val_seq = sequence;
		bo->seq_valid = true;
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

int ttm_bo_reserve(struct ttm_buffer_object *bo,
		   bool interruptible,
		   bool no_wait, bool use_sequence, uint32_t sequence)
{
	struct ttm_bo_global *glob = bo->glob;
	int put_count = 0;
	int ret;

	spin_lock(&glob->lru_lock);
	ret = ttm_bo_reserve_locked(bo, interruptible, no_wait, use_sequence,
				    sequence);
	if (likely(ret == 0))
		put_count = ttm_bo_del_from_lru(bo);
	spin_unlock(&glob->lru_lock);

	while (put_count--)
		kref_put(&bo->list_kref, ttm_bo_ref_bug);

	return ret;
}

void ttm_bo_unreserve(struct ttm_buffer_object *bo)
{
	struct ttm_bo_global *glob = bo->glob;

	spin_lock(&glob->lru_lock);
	ttm_bo_add_to_lru(bo);
	atomic_set(&bo->reserved, 0);
	wake_up_all(&bo->event_queue);
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
		bo->ttm = ttm_tt_create(bdev, bo->num_pages << PAGE_SHIFT,
					page_flags, glob->dummy_read_page);
		if (unlikely(bo->ttm == NULL))
			ret = -ENOMEM;
		break;
	case ttm_bo_type_user:
		bo->ttm = ttm_tt_create(bdev, bo->num_pages << PAGE_SHIFT,
					page_flags | TTM_PAGE_FLAG_USER,
					glob->dummy_read_page);
		if (unlikely(bo->ttm == NULL)) {
			ret = -ENOMEM;
			break;
		}

		ret = ttm_tt_set_user(bo->ttm, current,
				      bo->buffer_start, bo->num_pages);
		if (unlikely(ret != 0))
			ttm_tt_destroy(bo->ttm);
		break;
	default:
		printk(KERN_ERR TTM_PFX "Illegal buffer object type\n");
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int ttm_bo_handle_move_mem(struct ttm_buffer_object *bo,
				  struct ttm_mem_reg *mem,
				  bool evict, bool interruptible, bool no_wait)
{
	struct ttm_bo_device *bdev = bo->bdev;
	bool old_is_pci = ttm_mem_reg_is_pci(bdev, &bo->mem);
	bool new_is_pci = ttm_mem_reg_is_pci(bdev, mem);
	struct ttm_mem_type_manager *old_man = &bdev->man[bo->mem.mem_type];
	struct ttm_mem_type_manager *new_man = &bdev->man[mem->mem_type];
	int ret = 0;

	if (old_is_pci || new_is_pci ||
	    ((mem->placement & bo->mem.placement & TTM_PL_MASK_CACHING) == 0))
		ttm_bo_unmap_virtual(bo);

	/*
	 * Create and bind a ttm if required.
	 */

	if (!(new_man->flags & TTM_MEMTYPE_FLAG_FIXED) && (bo->ttm == NULL)) {
		ret = ttm_bo_add_ttm(bo, false);
		if (ret)
			goto out_err;

		ret = ttm_tt_set_placement_caching(bo->ttm, mem->placement);
		if (ret)
			goto out_err;

		if (mem->mem_type != TTM_PL_SYSTEM) {
			ret = ttm_tt_bind(bo->ttm, mem);
			if (ret)
				goto out_err;
		}

		if (bo->mem.mem_type == TTM_PL_SYSTEM) {
			bo->mem = *mem;
			mem->mm_node = NULL;
			goto moved;
		}

	}

	if (bdev->driver->move_notify)
		bdev->driver->move_notify(bo, mem);

	if (!(old_man->flags & TTM_MEMTYPE_FLAG_FIXED) &&
	    !(new_man->flags & TTM_MEMTYPE_FLAG_FIXED))
		ret = ttm_bo_move_ttm(bo, evict, no_wait, mem);
	else if (bdev->driver->move)
		ret = bdev->driver->move(bo, evict, interruptible,
					 no_wait, mem);
	else
		ret = ttm_bo_move_memcpy(bo, evict, no_wait, mem);

	if (ret)
		goto out_err;

moved:
	if (bo->evicted) {
		ret = bdev->driver->invalidate_caches(bdev, bo->mem.placement);
		if (ret)
			printk(KERN_ERR TTM_PFX "Can not flush read caches\n");
		bo->evicted = false;
	}

	if (bo->mem.mm_node) {
		spin_lock(&bo->lock);
		bo->offset = (bo->mem.mm_node->start << PAGE_SHIFT) +
		    bdev->man[bo->mem.mem_type].gpu_offset;
		bo->cur_placement = bo->mem.placement;
		spin_unlock(&bo->lock);
	}

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
 * If bo idle, remove from delayed- and lru lists, and unref.
 * If not idle, and already on delayed list, do nothing.
 * If not idle, and not on delayed list, put on delayed list,
 *   up the list_kref and schedule a delayed list check.
 */

static int ttm_bo_cleanup_refs(struct ttm_buffer_object *bo, bool remove_all)
{
	struct ttm_bo_device *bdev = bo->bdev;
	struct ttm_bo_global *glob = bo->glob;
	struct ttm_bo_driver *driver = bdev->driver;
	int ret;

	spin_lock(&bo->lock);
	(void) ttm_bo_wait(bo, false, false, !remove_all);

	if (!bo->sync_obj) {
		int put_count;

		spin_unlock(&bo->lock);

		spin_lock(&glob->lru_lock);
		put_count = ttm_bo_del_from_lru(bo);

		ret = ttm_bo_reserve_locked(bo, false, false, false, 0);
		BUG_ON(ret);
		if (bo->ttm)
			ttm_tt_unbind(bo->ttm);

		if (!list_empty(&bo->ddestroy)) {
			list_del_init(&bo->ddestroy);
			++put_count;
		}
		if (bo->mem.mm_node) {
			bo->mem.mm_node->private = NULL;
			drm_mm_put_block(bo->mem.mm_node);
			bo->mem.mm_node = NULL;
		}
		spin_unlock(&glob->lru_lock);

		atomic_set(&bo->reserved, 0);

		while (put_count--)
			kref_put(&bo->list_kref, ttm_bo_ref_bug);

		return 0;
	}

	spin_lock(&glob->lru_lock);
	if (list_empty(&bo->ddestroy)) {
		void *sync_obj = bo->sync_obj;
		void *sync_obj_arg = bo->sync_obj_arg;

		kref_get(&bo->list_kref);
		list_add_tail(&bo->ddestroy, &bdev->ddestroy);
		spin_unlock(&glob->lru_lock);
		spin_unlock(&bo->lock);

		if (sync_obj)
			driver->sync_obj_flush(sync_obj, sync_obj_arg);
		schedule_delayed_work(&bdev->wq,
				      ((HZ / 100) < 1) ? 1 : HZ / 100);
		ret = 0;

	} else {
		spin_unlock(&glob->lru_lock);
		spin_unlock(&bo->lock);
		ret = -EBUSY;
	}

	return ret;
}

/**
 * Traverse the delayed list, and call ttm_bo_cleanup_refs on all
 * encountered buffers.
 */

static int ttm_bo_delayed_delete(struct ttm_bo_device *bdev, bool remove_all)
{
	struct ttm_bo_global *glob = bdev->glob;
	struct ttm_buffer_object *entry, *nentry;
	struct list_head *list, *next;
	int ret;

	spin_lock(&glob->lru_lock);
	list_for_each_safe(list, next, &bdev->ddestroy) {
		entry = list_entry(list, struct ttm_buffer_object, ddestroy);
		nentry = NULL;

		/*
		 * Protect the next list entry from destruction while we
		 * unlock the lru_lock.
		 */

		if (next != &bdev->ddestroy) {
			nentry = list_entry(next, struct ttm_buffer_object,
					    ddestroy);
			kref_get(&nentry->list_kref);
		}
		kref_get(&entry->list_kref);

		spin_unlock(&glob->lru_lock);
		ret = ttm_bo_cleanup_refs(entry, remove_all);
		kref_put(&entry->list_kref, ttm_bo_release_list);

		spin_lock(&glob->lru_lock);
		if (nentry) {
			bool next_onlist = !list_empty(next);
			spin_unlock(&glob->lru_lock);
			kref_put(&nentry->list_kref, ttm_bo_release_list);
			spin_lock(&glob->lru_lock);
			/*
			 * Someone might have raced us and removed the
			 * next entry from the list. We don't bother restarting
			 * list traversal.
			 */

			if (!next_onlist)
				break;
		}
		if (ret)
			break;
	}
	ret = !list_empty(&bdev->ddestroy);
	spin_unlock(&glob->lru_lock);

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

	if (likely(bo->vm_node != NULL)) {
		rb_erase(&bo->vm_rb, &bdev->addr_space_rb);
		drm_mm_put_block(bo->vm_node);
		bo->vm_node = NULL;
	}
	write_unlock(&bdev->vm_lock);
	ttm_bo_cleanup_refs(bo, false);
	kref_put(&bo->list_kref, ttm_bo_release_list);
	write_lock(&bdev->vm_lock);
}

void ttm_bo_unref(struct ttm_buffer_object **p_bo)
{
	struct ttm_buffer_object *bo = *p_bo;
	struct ttm_bo_device *bdev = bo->bdev;

	*p_bo = NULL;
	write_lock(&bdev->vm_lock);
	kref_put(&bo->kref, ttm_bo_release);
	write_unlock(&bdev->vm_lock);
}
EXPORT_SYMBOL(ttm_bo_unref);

static int ttm_bo_evict(struct ttm_buffer_object *bo, bool interruptible,
			bool no_wait)
{
	struct ttm_bo_device *bdev = bo->bdev;
	struct ttm_bo_global *glob = bo->glob;
	struct ttm_mem_reg evict_mem;
	struct ttm_placement placement;
	int ret = 0;

	spin_lock(&bo->lock);
	ret = ttm_bo_wait(bo, false, interruptible, no_wait);
	spin_unlock(&bo->lock);

	if (unlikely(ret != 0)) {
		if (ret != -ERESTARTSYS) {
			printk(KERN_ERR TTM_PFX
			       "Failed to expire sync object before "
			       "buffer eviction.\n");
		}
		goto out;
	}

	BUG_ON(!atomic_read(&bo->reserved));

	evict_mem = bo->mem;
	evict_mem.mm_node = NULL;

	placement.fpfn = 0;
	placement.lpfn = 0;
	placement.num_placement = 0;
	placement.num_busy_placement = 0;
	bdev->driver->evict_flags(bo, &placement);
	ret = ttm_bo_mem_space(bo, &placement, &evict_mem, interruptible,
				no_wait);
	if (ret) {
		if (ret != -ERESTARTSYS) {
			printk(KERN_ERR TTM_PFX
			       "Failed to find memory space for "
			       "buffer 0x%p eviction.\n", bo);
			ttm_bo_mem_space_debug(bo, &placement);
		}
		goto out;
	}

	ret = ttm_bo_handle_move_mem(bo, &evict_mem, true, interruptible,
				     no_wait);
	if (ret) {
		if (ret != -ERESTARTSYS)
			printk(KERN_ERR TTM_PFX "Buffer eviction failed\n");
		spin_lock(&glob->lru_lock);
		if (evict_mem.mm_node) {
			evict_mem.mm_node->private = NULL;
			drm_mm_put_block(evict_mem.mm_node);
			evict_mem.mm_node = NULL;
		}
		spin_unlock(&glob->lru_lock);
		goto out;
	}
	bo->evicted = true;
out:
	return ret;
}

static int ttm_mem_evict_first(struct ttm_bo_device *bdev,
				uint32_t mem_type,
				bool interruptible, bool no_wait)
{
	struct ttm_bo_global *glob = bdev->glob;
	struct ttm_mem_type_manager *man = &bdev->man[mem_type];
	struct ttm_buffer_object *bo;
	int ret, put_count = 0;

retry:
	spin_lock(&glob->lru_lock);
	if (list_empty(&man->lru)) {
		spin_unlock(&glob->lru_lock);
		return -EBUSY;
	}

	bo = list_first_entry(&man->lru, struct ttm_buffer_object, lru);
	kref_get(&bo->list_kref);

	ret = ttm_bo_reserve_locked(bo, false, true, false, 0);

	if (unlikely(ret == -EBUSY)) {
		spin_unlock(&glob->lru_lock);
		if (likely(!no_wait))
			ret = ttm_bo_wait_unreserved(bo, interruptible);

		kref_put(&bo->list_kref, ttm_bo_release_list);

		/**
		 * We *need* to retry after releasing the lru lock.
		 */

		if (unlikely(ret != 0))
			return ret;
		goto retry;
	}

	put_count = ttm_bo_del_from_lru(bo);
	spin_unlock(&glob->lru_lock);

	BUG_ON(ret != 0);

	while (put_count--)
		kref_put(&bo->list_kref, ttm_bo_ref_bug);

	ret = ttm_bo_evict(bo, interruptible, no_wait);
	ttm_bo_unreserve(bo);

	kref_put(&bo->list_kref, ttm_bo_release_list);
	return ret;
}

static int ttm_bo_man_get_node(struct ttm_buffer_object *bo,
				struct ttm_mem_type_manager *man,
				struct ttm_placement *placement,
				struct ttm_mem_reg *mem,
				struct drm_mm_node **node)
{
	struct ttm_bo_global *glob = bo->glob;
	unsigned long lpfn;
	int ret;

	lpfn = placement->lpfn;
	if (!lpfn)
		lpfn = man->size;
	*node = NULL;
	do {
		ret = drm_mm_pre_get(&man->manager);
		if (unlikely(ret))
			return ret;

		spin_lock(&glob->lru_lock);
		*node = drm_mm_search_free_in_range(&man->manager,
					mem->num_pages, mem->page_alignment,
					placement->fpfn, lpfn, 1);
		if (unlikely(*node == NULL)) {
			spin_unlock(&glob->lru_lock);
			return 0;
		}
		*node = drm_mm_get_block_atomic_range(*node, mem->num_pages,
							mem->page_alignment,
							placement->fpfn,
							lpfn);
		spin_unlock(&glob->lru_lock);
	} while (*node == NULL);
	return 0;
}

/**
 * Repeatedly evict memory from the LRU for @mem_type until we create enough
 * space, or we've evicted everything and there isn't enough space.
 */
static int ttm_bo_mem_force_space(struct ttm_buffer_object *bo,
					uint32_t mem_type,
					struct ttm_placement *placement,
					struct ttm_mem_reg *mem,
					bool interruptible, bool no_wait)
{
	struct ttm_bo_device *bdev = bo->bdev;
	struct ttm_bo_global *glob = bdev->glob;
	struct ttm_mem_type_manager *man = &bdev->man[mem_type];
	struct drm_mm_node *node;
	int ret;

	do {
		ret = ttm_bo_man_get_node(bo, man, placement, mem, &node);
		if (unlikely(ret != 0))
			return ret;
		if (node)
			break;
		spin_lock(&glob->lru_lock);
		if (list_empty(&man->lru)) {
			spin_unlock(&glob->lru_lock);
			break;
		}
		spin_unlock(&glob->lru_lock);
		ret = ttm_mem_evict_first(bdev, mem_type, interruptible,
						no_wait);
		if (unlikely(ret != 0))
			return ret;
	} while (1);
	if (node == NULL)
		return -ENOMEM;
	mem->mm_node = node;
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
				 bool disallow_fixed,
				 uint32_t mem_type,
				 uint32_t proposed_placement,
				 uint32_t *masked_placement)
{
	uint32_t cur_flags = ttm_bo_type_flags(mem_type);

	if ((man->flags & TTM_MEMTYPE_FLAG_FIXED) && disallow_fixed)
		return false;

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
			bool interruptible, bool no_wait)
{
	struct ttm_bo_device *bdev = bo->bdev;
	struct ttm_mem_type_manager *man;
	uint32_t mem_type = TTM_PL_SYSTEM;
	uint32_t cur_flags = 0;
	bool type_found = false;
	bool type_ok = false;
	bool has_erestartsys = false;
	struct drm_mm_node *node = NULL;
	int i, ret;

	mem->mm_node = NULL;
	for (i = 0; i < placement->num_placement; ++i) {
		ret = ttm_mem_type_from_flags(placement->placement[i],
						&mem_type);
		if (ret)
			return ret;
		man = &bdev->man[mem_type];

		type_ok = ttm_bo_mt_compatible(man,
						bo->type == ttm_bo_type_user,
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
			ret = ttm_bo_man_get_node(bo, man, placement, mem,
							&node);
			if (unlikely(ret))
				return ret;
		}
		if (node)
			break;
	}

	if ((type_ok && (mem_type == TTM_PL_SYSTEM)) || node) {
		mem->mm_node = node;
		mem->mem_type = mem_type;
		mem->placement = cur_flags;
		if (node)
			node->private = bo;
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
						bo->type == ttm_bo_type_user,
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

		ret = ttm_bo_mem_force_space(bo, mem_type, placement, mem,
						interruptible, no_wait);
		if (ret == 0 && mem->mm_node) {
			mem->placement = cur_flags;
			mem->mm_node->private = bo;
			return 0;
		}
		if (ret == -ERESTARTSYS)
			has_erestartsys = true;
	}
	ret = (has_erestartsys) ? -ERESTARTSYS : -ENOMEM;
	return ret;
}
EXPORT_SYMBOL(ttm_bo_mem_space);

int ttm_bo_wait_cpu(struct ttm_buffer_object *bo, bool no_wait)
{
	if ((atomic_read(&bo->cpu_writers) > 0) && no_wait)
		return -EBUSY;

	return wait_event_interruptible(bo->event_queue,
					atomic_read(&bo->cpu_writers) == 0);
}
EXPORT_SYMBOL(ttm_bo_wait_cpu);

int ttm_bo_move_buffer(struct ttm_buffer_object *bo,
			struct ttm_placement *placement,
			bool interruptible, bool no_wait)
{
	struct ttm_bo_global *glob = bo->glob;
	int ret = 0;
	struct ttm_mem_reg mem;

	BUG_ON(!atomic_read(&bo->reserved));

	/*
	 * FIXME: It's possible to pipeline buffer moves.
	 * Have the driver move function wait for idle when necessary,
	 * instead of doing it here.
	 */
	spin_lock(&bo->lock);
	ret = ttm_bo_wait(bo, false, interruptible, no_wait);
	spin_unlock(&bo->lock);
	if (ret)
		return ret;
	mem.num_pages = bo->num_pages;
	mem.size = mem.num_pages << PAGE_SHIFT;
	mem.page_alignment = bo->mem.page_alignment;
	/*
	 * Determine where to move the buffer.
	 */
	ret = ttm_bo_mem_space(bo, placement, &mem, interruptible, no_wait);
	if (ret)
		goto out_unlock;
	ret = ttm_bo_handle_move_mem(bo, &mem, false, interruptible, no_wait);
out_unlock:
	if (ret && mem.mm_node) {
		spin_lock(&glob->lru_lock);
		mem.mm_node->private = NULL;
		drm_mm_put_block(mem.mm_node);
		spin_unlock(&glob->lru_lock);
	}
	return ret;
}

static int ttm_bo_mem_compat(struct ttm_placement *placement,
			     struct ttm_mem_reg *mem)
{
	int i;

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
			bool interruptible, bool no_wait)
{
	int ret;

	BUG_ON(!atomic_read(&bo->reserved));
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
		ret = ttm_bo_move_buffer(bo, placement, interruptible, no_wait);
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
	int i;

	if (placement->fpfn || placement->lpfn) {
		if (bo->mem.num_pages > (placement->lpfn - placement->fpfn)) {
			printk(KERN_ERR TTM_PFX "Page number range to small "
				"Need %lu pages, range is [%u, %u]\n",
				bo->mem.num_pages, placement->fpfn,
				placement->lpfn);
			return -EINVAL;
		}
	}
	for (i = 0; i < placement->num_placement; i++) {
		if (!capable(CAP_SYS_ADMIN)) {
			if (placement->placement[i] & TTM_PL_FLAG_NO_EVICT) {
				printk(KERN_ERR TTM_PFX "Need to be root to "
					"modify NO_EVICT status.\n");
				return -EINVAL;
			}
		}
	}
	for (i = 0; i < placement->num_busy_placement; i++) {
		if (!capable(CAP_SYS_ADMIN)) {
			if (placement->busy_placement[i] & TTM_PL_FLAG_NO_EVICT) {
				printk(KERN_ERR TTM_PFX "Need to be root to "
					"modify NO_EVICT status.\n");
				return -EINVAL;
			}
		}
	}
	return 0;
}

int ttm_bo_init(struct ttm_bo_device *bdev,
		struct ttm_buffer_object *bo,
		unsigned long size,
		enum ttm_bo_type type,
		struct ttm_placement *placement,
		uint32_t page_alignment,
		unsigned long buffer_start,
		bool interruptible,
		struct file *persistant_swap_storage,
		size_t acc_size,
		void (*destroy) (struct ttm_buffer_object *))
{
	int ret = 0;
	unsigned long num_pages;

	size += buffer_start & ~PAGE_MASK;
	num_pages = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	if (num_pages == 0) {
		printk(KERN_ERR TTM_PFX "Illegal buffer object size.\n");
		return -EINVAL;
	}
	bo->destroy = destroy;

	spin_lock_init(&bo->lock);
	kref_init(&bo->kref);
	kref_init(&bo->list_kref);
	atomic_set(&bo->cpu_writers, 0);
	atomic_set(&bo->reserved, 1);
	init_waitqueue_head(&bo->event_queue);
	INIT_LIST_HEAD(&bo->lru);
	INIT_LIST_HEAD(&bo->ddestroy);
	INIT_LIST_HEAD(&bo->swap);
	bo->bdev = bdev;
	bo->glob = bdev->glob;
	bo->type = type;
	bo->num_pages = num_pages;
	bo->mem.size = num_pages << PAGE_SHIFT;
	bo->mem.mem_type = TTM_PL_SYSTEM;
	bo->mem.num_pages = bo->num_pages;
	bo->mem.mm_node = NULL;
	bo->mem.page_alignment = page_alignment;
	bo->buffer_start = buffer_start & PAGE_MASK;
	bo->priv_flags = 0;
	bo->mem.placement = (TTM_PL_FLAG_SYSTEM | TTM_PL_FLAG_CACHED);
	bo->seq_valid = false;
	bo->persistant_swap_storage = persistant_swap_storage;
	bo->acc_size = acc_size;
	atomic_inc(&bo->glob->bo_count);

	ret = ttm_bo_check_placement(bo, placement);
	if (unlikely(ret != 0))
		goto out_err;

	/*
	 * For ttm_bo_type_device buffers, allocate
	 * address space from the device.
	 */
	if (bo->type == ttm_bo_type_device) {
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

static inline size_t ttm_bo_size(struct ttm_bo_global *glob,
				 unsigned long num_pages)
{
	size_t page_array_size = (num_pages * sizeof(void *) + PAGE_SIZE - 1) &
	    PAGE_MASK;

	return glob->ttm_bo_size + 2 * page_array_size;
}

int ttm_bo_create(struct ttm_bo_device *bdev,
			unsigned long size,
			enum ttm_bo_type type,
			struct ttm_placement *placement,
			uint32_t page_alignment,
			unsigned long buffer_start,
			bool interruptible,
			struct file *persistant_swap_storage,
			struct ttm_buffer_object **p_bo)
{
	struct ttm_buffer_object *bo;
	struct ttm_mem_global *mem_glob = bdev->glob->mem_glob;
	int ret;

	size_t acc_size =
	    ttm_bo_size(bdev->glob, (size + PAGE_SIZE - 1) >> PAGE_SHIFT);
	ret = ttm_mem_global_alloc(mem_glob, acc_size, false, false);
	if (unlikely(ret != 0))
		return ret;

	bo = kzalloc(sizeof(*bo), GFP_KERNEL);

	if (unlikely(bo == NULL)) {
		ttm_mem_global_free(mem_glob, acc_size);
		return -ENOMEM;
	}

	ret = ttm_bo_init(bdev, bo, size, type, placement, page_alignment,
				buffer_start, interruptible,
				persistant_swap_storage, acc_size, NULL);
	if (likely(ret == 0))
		*p_bo = bo;

	return ret;
}

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
				printk(KERN_ERR TTM_PFX
					"Cleanup eviction failed\n");
			}
		}
		spin_lock(&glob->lru_lock);
	}
	spin_unlock(&glob->lru_lock);
	return 0;
}

int ttm_bo_clean_mm(struct ttm_bo_device *bdev, unsigned mem_type)
{
	struct ttm_bo_global *glob = bdev->glob;
	struct ttm_mem_type_manager *man;
	int ret = -EINVAL;

	if (mem_type >= TTM_NUM_MEM_TYPES) {
		printk(KERN_ERR TTM_PFX "Illegal memory type %d\n", mem_type);
		return ret;
	}
	man = &bdev->man[mem_type];

	if (!man->has_type) {
		printk(KERN_ERR TTM_PFX "Trying to take down uninitialized "
		       "memory manager type %u\n", mem_type);
		return ret;
	}

	man->use_type = false;
	man->has_type = false;

	ret = 0;
	if (mem_type > 0) {
		ttm_bo_force_list_clean(bdev, mem_type, false);

		spin_lock(&glob->lru_lock);
		if (drm_mm_clean(&man->manager))
			drm_mm_takedown(&man->manager);
		else
			ret = -EBUSY;

		spin_unlock(&glob->lru_lock);
	}

	return ret;
}
EXPORT_SYMBOL(ttm_bo_clean_mm);

int ttm_bo_evict_mm(struct ttm_bo_device *bdev, unsigned mem_type)
{
	struct ttm_mem_type_manager *man = &bdev->man[mem_type];

	if (mem_type == 0 || mem_type >= TTM_NUM_MEM_TYPES) {
		printk(KERN_ERR TTM_PFX
		       "Illegal memory manager memory type %u.\n",
		       mem_type);
		return -EINVAL;
	}

	if (!man->has_type) {
		printk(KERN_ERR TTM_PFX
		       "Memory type %u has not been initialized.\n",
		       mem_type);
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

	if (type >= TTM_NUM_MEM_TYPES) {
		printk(KERN_ERR TTM_PFX "Illegal memory type %d\n", type);
		return ret;
	}

	man = &bdev->man[type];
	if (man->has_type) {
		printk(KERN_ERR TTM_PFX
		       "Memory manager already initialized for type %d\n",
		       type);
		return ret;
	}

	ret = bdev->driver->init_mem_type(bdev, type, man);
	if (ret)
		return ret;

	ret = 0;
	if (type != TTM_PL_SYSTEM) {
		if (!p_size) {
			printk(KERN_ERR TTM_PFX
			       "Zero size memory manager type %d\n",
			       type);
			return ret;
		}
		ret = drm_mm_init(&man->manager, 0, p_size);
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

void ttm_bo_global_release(struct ttm_global_reference *ref)
{
	struct ttm_bo_global *glob = ref->object;

	kobject_del(&glob->kobj);
	kobject_put(&glob->kobj);
}
EXPORT_SYMBOL(ttm_bo_global_release);

int ttm_bo_global_init(struct ttm_global_reference *ref)
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
		printk(KERN_ERR TTM_PFX
		       "Could not register buffer object swapout.\n");
		goto out_no_shrink;
	}

	glob->ttm_bo_extra_size =
		ttm_round_pot(sizeof(struct ttm_tt)) +
		ttm_round_pot(sizeof(struct ttm_backend));

	glob->ttm_bo_size = glob->ttm_bo_extra_size +
		ttm_round_pot(sizeof(struct ttm_buffer_object));

	atomic_set(&glob->bo_count, 0);

	kobject_init(&glob->kobj, &ttm_bo_glob_kobj_type);
	ret = kobject_add(&glob->kobj, ttm_get_kobj(), "buffer_objects");
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
				printk(KERN_ERR TTM_PFX
				       "DRM memory manager type %d "
				       "is not clean.\n", i);
			}
			man->has_type = false;
		}
	}

	mutex_lock(&glob->device_list_mutex);
	list_del(&bdev->device_list);
	mutex_unlock(&glob->device_list_mutex);

	if (!cancel_delayed_work(&bdev->wq))
		flush_scheduled_work();

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
	bdev->nice_mode = true;
	INIT_LIST_HEAD(&bdev->ddestroy);
	bdev->dev_mapping = NULL;
	bdev->glob = glob;
	bdev->need_dma32 = need_dma32;

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

int ttm_bo_pci_offset(struct ttm_bo_device *bdev,
		      struct ttm_mem_reg *mem,
		      unsigned long *bus_base,
		      unsigned long *bus_offset, unsigned long *bus_size)
{
	struct ttm_mem_type_manager *man = &bdev->man[mem->mem_type];

	*bus_size = 0;
	if (!(man->flags & TTM_MEMTYPE_FLAG_MAPPABLE))
		return -EINVAL;

	if (ttm_mem_reg_is_pci(bdev, mem)) {
		*bus_offset = mem->mm_node->start << PAGE_SHIFT;
		*bus_size = mem->num_pages << PAGE_SHIFT;
		*bus_base = man->io_offset;
	}

	return 0;
}

void ttm_bo_unmap_virtual(struct ttm_buffer_object *bo)
{
	struct ttm_bo_device *bdev = bo->bdev;
	loff_t offset = (loff_t) bo->addr_space_offset;
	loff_t holelen = ((loff_t) bo->mem.num_pages) << PAGE_SHIFT;

	if (!bdev->dev_mapping)
		return;

	unmap_mapping_range(bdev->dev_mapping, offset, holelen, 1);
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
	void *sync_obj;
	void *sync_obj_arg;
	int ret = 0;

	if (likely(bo->sync_obj == NULL))
		return 0;

	while (bo->sync_obj) {

		if (driver->sync_obj_signaled(bo->sync_obj, bo->sync_obj_arg)) {
			void *tmp_obj = bo->sync_obj;
			bo->sync_obj = NULL;
			clear_bit(TTM_BO_PRIV_FLAG_MOVING, &bo->priv_flags);
			spin_unlock(&bo->lock);
			driver->sync_obj_unref(&tmp_obj);
			spin_lock(&bo->lock);
			continue;
		}

		if (no_wait)
			return -EBUSY;

		sync_obj = driver->sync_obj_ref(bo->sync_obj);
		sync_obj_arg = bo->sync_obj_arg;
		spin_unlock(&bo->lock);
		ret = driver->sync_obj_wait(sync_obj, sync_obj_arg,
					    lazy, interruptible);
		if (unlikely(ret != 0)) {
			driver->sync_obj_unref(&sync_obj);
			spin_lock(&bo->lock);
			return ret;
		}
		spin_lock(&bo->lock);
		if (likely(bo->sync_obj == sync_obj &&
			   bo->sync_obj_arg == sync_obj_arg)) {
			void *tmp_obj = bo->sync_obj;
			bo->sync_obj = NULL;
			clear_bit(TTM_BO_PRIV_FLAG_MOVING,
				  &bo->priv_flags);
			spin_unlock(&bo->lock);
			driver->sync_obj_unref(&sync_obj);
			driver->sync_obj_unref(&tmp_obj);
			spin_lock(&bo->lock);
		} else {
			spin_unlock(&bo->lock);
			driver->sync_obj_unref(&sync_obj);
			spin_lock(&bo->lock);
		}
	}
	return 0;
}
EXPORT_SYMBOL(ttm_bo_wait);

void ttm_bo_unblock_reservation(struct ttm_buffer_object *bo)
{
	atomic_set(&bo->reserved, 0);
	wake_up_all(&bo->event_queue);
}

int ttm_bo_block_reservation(struct ttm_buffer_object *bo, bool interruptible,
			     bool no_wait)
{
	int ret;

	while (unlikely(atomic_cmpxchg(&bo->reserved, 0, 1) != 0)) {
		if (no_wait)
			return -EBUSY;
		else if (interruptible) {
			ret = wait_event_interruptible
			    (bo->event_queue, atomic_read(&bo->reserved) == 0);
			if (unlikely(ret != 0))
				return ret;
		} else {
			wait_event(bo->event_queue,
				   atomic_read(&bo->reserved) == 0);
		}
	}
	return 0;
}

int ttm_bo_synccpu_write_grab(struct ttm_buffer_object *bo, bool no_wait)
{
	int ret = 0;

	/*
	 * Using ttm_bo_reserve instead of ttm_bo_block_reservation
	 * makes sure the lru lists are updated.
	 */

	ret = ttm_bo_reserve(bo, true, no_wait, false, 0);
	if (unlikely(ret != 0))
		return ret;
	spin_lock(&bo->lock);
	ret = ttm_bo_wait(bo, false, true, no_wait);
	spin_unlock(&bo->lock);
	if (likely(ret == 0))
		atomic_inc(&bo->cpu_writers);
	ttm_bo_unreserve(bo);
	return ret;
}
EXPORT_SYMBOL(ttm_bo_synccpu_write_grab);

void ttm_bo_synccpu_write_release(struct ttm_buffer_object *bo)
{
	if (atomic_dec_and_test(&bo->cpu_writers))
		wake_up_all(&bo->event_queue);
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
	while (ret == -EBUSY) {
		if (unlikely(list_empty(&glob->swap_lru))) {
			spin_unlock(&glob->lru_lock);
			return -EBUSY;
		}

		bo = list_first_entry(&glob->swap_lru,
				      struct ttm_buffer_object, swap);
		kref_get(&bo->list_kref);

		/**
		 * Reserve buffer. Since we unlock while sleeping, we need
		 * to re-check that nobody removed us from the swap-list while
		 * we slept.
		 */

		ret = ttm_bo_reserve_locked(bo, false, true, false, 0);
		if (unlikely(ret == -EBUSY)) {
			spin_unlock(&glob->lru_lock);
			ttm_bo_wait_unreserved(bo, false);
			kref_put(&bo->list_kref, ttm_bo_release_list);
			spin_lock(&glob->lru_lock);
		}
	}

	BUG_ON(ret != 0);
	put_count = ttm_bo_del_from_lru(bo);
	spin_unlock(&glob->lru_lock);

	while (put_count--)
		kref_put(&bo->list_kref, ttm_bo_ref_bug);

	/**
	 * Wait for GPU, then move to system cached.
	 */

	spin_lock(&bo->lock);
	ret = ttm_bo_wait(bo, false, false, false);
	spin_unlock(&bo->lock);

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

	ret = ttm_tt_swapout(bo->ttm, bo->persistant_swap_storage);
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
