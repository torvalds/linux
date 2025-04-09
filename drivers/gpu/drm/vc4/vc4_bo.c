// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright © 2015 Broadcom
 */

/**
 * DOC: VC4 GEM BO management support
 *
 * The VC4 GPU architecture (both scanout and rendering) has direct
 * access to system memory with no MMU in between.  To support it, we
 * use the GEM DMA helper functions to allocate contiguous ranges of
 * physical memory for our BOs.
 *
 * Since the DMA allocator is very slow, we keep a cache of recently
 * freed BOs around so that the kernel's allocation of objects for 3D
 * rendering can return quickly.
 */

#include <linux/dma-buf.h>

#include <drm/drm_fourcc.h>

#include "vc4_drv.h"
#include "uapi/drm/vc4_drm.h"

static const struct drm_gem_object_funcs vc4_gem_object_funcs;

static const char * const bo_type_names[] = {
	"kernel",
	"V3D",
	"V3D shader",
	"dumb",
	"binner",
	"RCL",
	"BCL",
	"kernel BO cache",
};

static bool is_user_label(int label)
{
	return label >= VC4_BO_TYPE_COUNT;
}

static void vc4_bo_stats_print(struct drm_printer *p, struct vc4_dev *vc4)
{
	int i;

	for (i = 0; i < vc4->num_labels; i++) {
		if (!vc4->bo_labels[i].num_allocated)
			continue;

		drm_printf(p, "%30s: %6dkb BOs (%d)\n",
			   vc4->bo_labels[i].name,
			   vc4->bo_labels[i].size_allocated / 1024,
			   vc4->bo_labels[i].num_allocated);
	}

	mutex_lock(&vc4->purgeable.lock);
	if (vc4->purgeable.num)
		drm_printf(p, "%30s: %6zdkb BOs (%d)\n", "userspace BO cache",
			   vc4->purgeable.size / 1024, vc4->purgeable.num);

	if (vc4->purgeable.purged_num)
		drm_printf(p, "%30s: %6zdkb BOs (%d)\n", "total purged BO",
			   vc4->purgeable.purged_size / 1024,
			   vc4->purgeable.purged_num);
	mutex_unlock(&vc4->purgeable.lock);
}

static int vc4_bo_stats_debugfs(struct seq_file *m, void *unused)
{
	struct drm_debugfs_entry *entry = m->private;
	struct drm_device *dev = entry->dev;
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct drm_printer p = drm_seq_file_printer(m);

	vc4_bo_stats_print(&p, vc4);

	return 0;
}

/* Takes ownership of *name and returns the appropriate slot for it in
 * the bo_labels[] array, extending it as necessary.
 *
 * This is inefficient and could use a hash table instead of walking
 * an array and strcmp()ing.  However, the assumption is that user
 * labeling will be infrequent (scanout buffers and other long-lived
 * objects, or debug driver builds), so we can live with it for now.
 */
static int vc4_get_user_label(struct vc4_dev *vc4, const char *name)
{
	int i;
	int free_slot = -1;

	for (i = 0; i < vc4->num_labels; i++) {
		if (!vc4->bo_labels[i].name) {
			free_slot = i;
		} else if (strcmp(vc4->bo_labels[i].name, name) == 0) {
			kfree(name);
			return i;
		}
	}

	if (free_slot != -1) {
		WARN_ON(vc4->bo_labels[free_slot].num_allocated != 0);
		vc4->bo_labels[free_slot].name = name;
		return free_slot;
	} else {
		u32 new_label_count = vc4->num_labels + 1;
		struct vc4_label *new_labels =
			krealloc(vc4->bo_labels,
				 new_label_count * sizeof(*new_labels),
				 GFP_KERNEL);

		if (!new_labels) {
			kfree(name);
			return -1;
		}

		free_slot = vc4->num_labels;
		vc4->bo_labels = new_labels;
		vc4->num_labels = new_label_count;

		vc4->bo_labels[free_slot].name = name;
		vc4->bo_labels[free_slot].num_allocated = 0;
		vc4->bo_labels[free_slot].size_allocated = 0;

		return free_slot;
	}
}

static void vc4_bo_set_label(struct drm_gem_object *gem_obj, int label)
{
	struct vc4_bo *bo = to_vc4_bo(gem_obj);
	struct vc4_dev *vc4 = to_vc4_dev(gem_obj->dev);

	lockdep_assert_held(&vc4->bo_lock);

	if (label != -1) {
		vc4->bo_labels[label].num_allocated++;
		vc4->bo_labels[label].size_allocated += gem_obj->size;
	}

	vc4->bo_labels[bo->label].num_allocated--;
	vc4->bo_labels[bo->label].size_allocated -= gem_obj->size;

	if (vc4->bo_labels[bo->label].num_allocated == 0 &&
	    is_user_label(bo->label)) {
		/* Free user BO label slots on last unreference.
		 * Slots are just where we track the stats for a given
		 * name, and once a name is unused we can reuse that
		 * slot.
		 */
		kfree(vc4->bo_labels[bo->label].name);
		vc4->bo_labels[bo->label].name = NULL;
	}

	bo->label = label;
}

static uint32_t bo_page_index(size_t size)
{
	return (size / PAGE_SIZE) - 1;
}

static void vc4_bo_destroy(struct vc4_bo *bo)
{
	struct drm_gem_object *obj = &bo->base.base;
	struct vc4_dev *vc4 = to_vc4_dev(obj->dev);

	lockdep_assert_held(&vc4->bo_lock);

	vc4_bo_set_label(obj, -1);

	if (bo->validated_shader) {
		kfree(bo->validated_shader->uniform_addr_offsets);
		kfree(bo->validated_shader->texture_samples);
		kfree(bo->validated_shader);
		bo->validated_shader = NULL;
	}

	mutex_destroy(&bo->madv_lock);
	drm_gem_dma_free(&bo->base);
}

static void vc4_bo_remove_from_cache(struct vc4_bo *bo)
{
	struct vc4_dev *vc4 = to_vc4_dev(bo->base.base.dev);

	lockdep_assert_held(&vc4->bo_lock);
	list_del(&bo->unref_head);
	list_del(&bo->size_head);
}

static struct list_head *vc4_get_cache_list_for_size(struct drm_device *dev,
						     size_t size)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	uint32_t page_index = bo_page_index(size);

	if (vc4->bo_cache.size_list_size <= page_index) {
		uint32_t new_size = max(vc4->bo_cache.size_list_size * 2,
					page_index + 1);
		struct list_head *new_list;
		uint32_t i;

		new_list = kmalloc_array(new_size, sizeof(struct list_head),
					 GFP_KERNEL);
		if (!new_list)
			return NULL;

		/* Rebase the old cached BO lists to their new list
		 * head locations.
		 */
		for (i = 0; i < vc4->bo_cache.size_list_size; i++) {
			struct list_head *old_list =
				&vc4->bo_cache.size_list[i];

			if (list_empty(old_list))
				INIT_LIST_HEAD(&new_list[i]);
			else
				list_replace(old_list, &new_list[i]);
		}
		/* And initialize the brand new BO list heads. */
		for (i = vc4->bo_cache.size_list_size; i < new_size; i++)
			INIT_LIST_HEAD(&new_list[i]);

		kfree(vc4->bo_cache.size_list);
		vc4->bo_cache.size_list = new_list;
		vc4->bo_cache.size_list_size = new_size;
	}

	return &vc4->bo_cache.size_list[page_index];
}

static void vc4_bo_cache_purge(struct drm_device *dev)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);

	mutex_lock(&vc4->bo_lock);
	while (!list_empty(&vc4->bo_cache.time_list)) {
		struct vc4_bo *bo = list_last_entry(&vc4->bo_cache.time_list,
						    struct vc4_bo, unref_head);
		vc4_bo_remove_from_cache(bo);
		vc4_bo_destroy(bo);
	}
	mutex_unlock(&vc4->bo_lock);
}

void vc4_bo_add_to_purgeable_pool(struct vc4_bo *bo)
{
	struct vc4_dev *vc4 = to_vc4_dev(bo->base.base.dev);

	if (WARN_ON_ONCE(vc4->gen > VC4_GEN_4))
		return;

	mutex_lock(&vc4->purgeable.lock);
	list_add_tail(&bo->size_head, &vc4->purgeable.list);
	vc4->purgeable.num++;
	vc4->purgeable.size += bo->base.base.size;
	mutex_unlock(&vc4->purgeable.lock);
}

static void vc4_bo_remove_from_purgeable_pool_locked(struct vc4_bo *bo)
{
	struct vc4_dev *vc4 = to_vc4_dev(bo->base.base.dev);

	if (WARN_ON_ONCE(vc4->gen > VC4_GEN_4))
		return;

	/* list_del_init() is used here because the caller might release
	 * the purgeable lock in order to acquire the madv one and update the
	 * madv status.
	 * During this short period of time a user might decide to mark
	 * the BO as unpurgeable, and if bo->madv is set to
	 * VC4_MADV_DONTNEED it will try to remove the BO from the
	 * purgeable list which will fail if the ->next/prev fields
	 * are set to LIST_POISON1/LIST_POISON2 (which is what
	 * list_del() does).
	 * Re-initializing the list element guarantees that list_del()
	 * will work correctly even if it's a NOP.
	 */
	list_del_init(&bo->size_head);
	vc4->purgeable.num--;
	vc4->purgeable.size -= bo->base.base.size;
}

void vc4_bo_remove_from_purgeable_pool(struct vc4_bo *bo)
{
	struct vc4_dev *vc4 = to_vc4_dev(bo->base.base.dev);

	mutex_lock(&vc4->purgeable.lock);
	vc4_bo_remove_from_purgeable_pool_locked(bo);
	mutex_unlock(&vc4->purgeable.lock);
}

static void vc4_bo_purge(struct drm_gem_object *obj)
{
	struct vc4_bo *bo = to_vc4_bo(obj);
	struct drm_device *dev = obj->dev;

	WARN_ON(!mutex_is_locked(&bo->madv_lock));
	WARN_ON(bo->madv != VC4_MADV_DONTNEED);

	drm_vma_node_unmap(&obj->vma_node, dev->anon_inode->i_mapping);

	dma_free_wc(dev->dev, obj->size, bo->base.vaddr, bo->base.dma_addr);
	bo->base.vaddr = NULL;
	bo->madv = __VC4_MADV_PURGED;
}

static void vc4_bo_userspace_cache_purge(struct drm_device *dev)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);

	mutex_lock(&vc4->purgeable.lock);
	while (!list_empty(&vc4->purgeable.list)) {
		struct vc4_bo *bo = list_first_entry(&vc4->purgeable.list,
						     struct vc4_bo, size_head);
		struct drm_gem_object *obj = &bo->base.base;
		size_t purged_size = 0;

		vc4_bo_remove_from_purgeable_pool_locked(bo);

		/* Release the purgeable lock while we're purging the BO so
		 * that other people can continue inserting things in the
		 * purgeable pool without having to wait for all BOs to be
		 * purged.
		 */
		mutex_unlock(&vc4->purgeable.lock);
		mutex_lock(&bo->madv_lock);

		/* Since we released the purgeable pool lock before acquiring
		 * the BO madv one, the user may have marked the BO as WILLNEED
		 * and re-used it in the meantime.
		 * Before purging the BO we need to make sure
		 * - it is still marked as DONTNEED
		 * - it has not been re-inserted in the purgeable list
		 * - it is not used by HW blocks
		 * If one of these conditions is not met, just skip the entry.
		 */
		if (bo->madv == VC4_MADV_DONTNEED &&
		    list_empty(&bo->size_head) &&
		    !refcount_read(&bo->usecnt)) {
			purged_size = bo->base.base.size;
			vc4_bo_purge(obj);
		}
		mutex_unlock(&bo->madv_lock);
		mutex_lock(&vc4->purgeable.lock);

		if (purged_size) {
			vc4->purgeable.purged_size += purged_size;
			vc4->purgeable.purged_num++;
		}
	}
	mutex_unlock(&vc4->purgeable.lock);
}

static struct vc4_bo *vc4_bo_get_from_cache(struct drm_device *dev,
					    uint32_t size,
					    enum vc4_kernel_bo_type type)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	uint32_t page_index = bo_page_index(size);
	struct vc4_bo *bo = NULL;

	mutex_lock(&vc4->bo_lock);
	if (page_index >= vc4->bo_cache.size_list_size)
		goto out;

	if (list_empty(&vc4->bo_cache.size_list[page_index]))
		goto out;

	bo = list_first_entry(&vc4->bo_cache.size_list[page_index],
			      struct vc4_bo, size_head);
	vc4_bo_remove_from_cache(bo);
	kref_init(&bo->base.base.refcount);

out:
	if (bo)
		vc4_bo_set_label(&bo->base.base, type);
	mutex_unlock(&vc4->bo_lock);
	return bo;
}

/**
 * vc4_create_object - Implementation of driver->gem_create_object.
 * @dev: DRM device
 * @size: Size in bytes of the memory the object will reference
 *
 * This lets the DMA helpers allocate object structs for us, and keep
 * our BO stats correct.
 */
struct drm_gem_object *vc4_create_object(struct drm_device *dev, size_t size)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct vc4_bo *bo;

	if (WARN_ON_ONCE(vc4->gen > VC4_GEN_4))
		return ERR_PTR(-ENODEV);

	bo = kzalloc(sizeof(*bo), GFP_KERNEL);
	if (!bo)
		return ERR_PTR(-ENOMEM);

	bo->madv = VC4_MADV_WILLNEED;
	refcount_set(&bo->usecnt, 0);

	mutex_init(&bo->madv_lock);

	mutex_lock(&vc4->bo_lock);
	bo->label = VC4_BO_TYPE_KERNEL;
	vc4->bo_labels[VC4_BO_TYPE_KERNEL].num_allocated++;
	vc4->bo_labels[VC4_BO_TYPE_KERNEL].size_allocated += size;
	mutex_unlock(&vc4->bo_lock);

	bo->base.base.funcs = &vc4_gem_object_funcs;

	return &bo->base.base;
}

struct vc4_bo *vc4_bo_create(struct drm_device *dev, size_t unaligned_size,
			     bool allow_unzeroed, enum vc4_kernel_bo_type type)
{
	size_t size = roundup(unaligned_size, PAGE_SIZE);
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct drm_gem_dma_object *dma_obj;
	struct vc4_bo *bo;

	if (WARN_ON_ONCE(vc4->gen > VC4_GEN_4))
		return ERR_PTR(-ENODEV);

	if (size == 0)
		return ERR_PTR(-EINVAL);

	/* First, try to get a vc4_bo from the kernel BO cache. */
	bo = vc4_bo_get_from_cache(dev, size, type);
	if (bo) {
		if (!allow_unzeroed)
			memset(bo->base.vaddr, 0, bo->base.base.size);
		return bo;
	}

	dma_obj = drm_gem_dma_create(dev, size);
	if (IS_ERR(dma_obj)) {
		/*
		 * If we've run out of DMA memory, kill the cache of
		 * DMA allocations we've got laying around and try again.
		 */
		vc4_bo_cache_purge(dev);
		dma_obj = drm_gem_dma_create(dev, size);
	}

	if (IS_ERR(dma_obj)) {
		/*
		 * Still not enough DMA memory, purge the userspace BO
		 * cache and retry.
		 * This is sub-optimal since we purge the whole userspace
		 * BO cache which forces user that want to re-use the BO to
		 * restore its initial content.
		 * Ideally, we should purge entries one by one and retry
		 * after each to see if DMA allocation succeeds. Or even
		 * better, try to find an entry with at least the same
		 * size.
		 */
		vc4_bo_userspace_cache_purge(dev);
		dma_obj = drm_gem_dma_create(dev, size);
	}

	if (IS_ERR(dma_obj)) {
		struct drm_printer p = drm_info_printer(vc4->base.dev);
		drm_err(dev, "Failed to allocate from GEM DMA helper:\n");
		vc4_bo_stats_print(&p, vc4);
		return ERR_PTR(-ENOMEM);
	}
	bo = to_vc4_bo(&dma_obj->base);

	/* By default, BOs do not support the MADV ioctl. This will be enabled
	 * only on BOs that are exposed to userspace (V3D, V3D_SHADER and DUMB
	 * BOs).
	 */
	bo->madv = __VC4_MADV_NOTSUPP;

	mutex_lock(&vc4->bo_lock);
	vc4_bo_set_label(&dma_obj->base, type);
	mutex_unlock(&vc4->bo_lock);

	return bo;
}

int vc4_bo_dumb_create(struct drm_file *file_priv,
		       struct drm_device *dev,
		       struct drm_mode_create_dumb *args)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct vc4_bo *bo = NULL;
	int ret;

	if (WARN_ON_ONCE(vc4->gen > VC4_GEN_4))
		return -ENODEV;

	ret = vc4_dumb_fixup_args(args);
	if (ret)
		return ret;

	bo = vc4_bo_create(dev, args->size, false, VC4_BO_TYPE_DUMB);
	if (IS_ERR(bo))
		return PTR_ERR(bo);

	bo->madv = VC4_MADV_WILLNEED;

	ret = drm_gem_handle_create(file_priv, &bo->base.base, &args->handle);
	drm_gem_object_put(&bo->base.base);

	return ret;
}

static void vc4_bo_cache_free_old(struct drm_device *dev)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	unsigned long expire_time = jiffies - msecs_to_jiffies(1000);

	lockdep_assert_held(&vc4->bo_lock);

	while (!list_empty(&vc4->bo_cache.time_list)) {
		struct vc4_bo *bo = list_last_entry(&vc4->bo_cache.time_list,
						    struct vc4_bo, unref_head);
		if (time_before(expire_time, bo->free_time)) {
			mod_timer(&vc4->bo_cache.time_timer,
				  round_jiffies_up(jiffies +
						   msecs_to_jiffies(1000)));
			return;
		}

		vc4_bo_remove_from_cache(bo);
		vc4_bo_destroy(bo);
	}
}

/* Called on the last userspace/kernel unreference of the BO.  Returns
 * it to the BO cache if possible, otherwise frees it.
 */
static void vc4_free_object(struct drm_gem_object *gem_bo)
{
	struct drm_device *dev = gem_bo->dev;
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct vc4_bo *bo = to_vc4_bo(gem_bo);
	struct list_head *cache_list;

	/* Remove the BO from the purgeable list. */
	mutex_lock(&bo->madv_lock);
	if (bo->madv == VC4_MADV_DONTNEED && !refcount_read(&bo->usecnt))
		vc4_bo_remove_from_purgeable_pool(bo);
	mutex_unlock(&bo->madv_lock);

	mutex_lock(&vc4->bo_lock);
	/* If the object references someone else's memory, we can't cache it.
	 */
	if (gem_bo->import_attach) {
		vc4_bo_destroy(bo);
		goto out;
	}

	/* Don't cache if it was publicly named. */
	if (gem_bo->name) {
		vc4_bo_destroy(bo);
		goto out;
	}

	/* If this object was partially constructed but DMA allocation
	 * had failed, just free it. Can also happen when the BO has been
	 * purged.
	 */
	if (!bo->base.vaddr) {
		vc4_bo_destroy(bo);
		goto out;
	}

	cache_list = vc4_get_cache_list_for_size(dev, gem_bo->size);
	if (!cache_list) {
		vc4_bo_destroy(bo);
		goto out;
	}

	if (bo->validated_shader) {
		kfree(bo->validated_shader->uniform_addr_offsets);
		kfree(bo->validated_shader->texture_samples);
		kfree(bo->validated_shader);
		bo->validated_shader = NULL;
	}

	/* Reset madv and usecnt before adding the BO to the cache. */
	bo->madv = __VC4_MADV_NOTSUPP;
	refcount_set(&bo->usecnt, 0);

	bo->t_format = false;
	bo->free_time = jiffies;
	list_add(&bo->size_head, cache_list);
	list_add(&bo->unref_head, &vc4->bo_cache.time_list);

	vc4_bo_set_label(&bo->base.base, VC4_BO_TYPE_KERNEL_CACHE);

	vc4_bo_cache_free_old(dev);

out:
	mutex_unlock(&vc4->bo_lock);
}

static void vc4_bo_cache_time_work(struct work_struct *work)
{
	struct vc4_dev *vc4 =
		container_of(work, struct vc4_dev, bo_cache.time_work);
	struct drm_device *dev = &vc4->base;

	mutex_lock(&vc4->bo_lock);
	vc4_bo_cache_free_old(dev);
	mutex_unlock(&vc4->bo_lock);
}

int vc4_bo_inc_usecnt(struct vc4_bo *bo)
{
	struct vc4_dev *vc4 = to_vc4_dev(bo->base.base.dev);
	int ret;

	if (WARN_ON_ONCE(vc4->gen > VC4_GEN_4))
		return -ENODEV;

	/* Fast path: if the BO is already retained by someone, no need to
	 * check the madv status.
	 */
	if (refcount_inc_not_zero(&bo->usecnt))
		return 0;

	mutex_lock(&bo->madv_lock);
	switch (bo->madv) {
	case VC4_MADV_WILLNEED:
		if (!refcount_inc_not_zero(&bo->usecnt))
			refcount_set(&bo->usecnt, 1);
		ret = 0;
		break;
	case VC4_MADV_DONTNEED:
		/* We shouldn't use a BO marked as purgeable if at least
		 * someone else retained its content by incrementing usecnt.
		 * Luckily the BO hasn't been purged yet, but something wrong
		 * is happening here. Just throw an error instead of
		 * authorizing this use case.
		 */
	case __VC4_MADV_PURGED:
		/* We can't use a purged BO. */
	default:
		/* Invalid madv value. */
		ret = -EINVAL;
		break;
	}
	mutex_unlock(&bo->madv_lock);

	return ret;
}

void vc4_bo_dec_usecnt(struct vc4_bo *bo)
{
	struct vc4_dev *vc4 = to_vc4_dev(bo->base.base.dev);

	if (WARN_ON_ONCE(vc4->gen > VC4_GEN_4))
		return;

	/* Fast path: if the BO is still retained by someone, no need to test
	 * the madv value.
	 */
	if (refcount_dec_not_one(&bo->usecnt))
		return;

	mutex_lock(&bo->madv_lock);
	if (refcount_dec_and_test(&bo->usecnt) &&
	    bo->madv == VC4_MADV_DONTNEED)
		vc4_bo_add_to_purgeable_pool(bo);
	mutex_unlock(&bo->madv_lock);
}

static void vc4_bo_cache_time_timer(struct timer_list *t)
{
	struct vc4_dev *vc4 = from_timer(vc4, t, bo_cache.time_timer);

	schedule_work(&vc4->bo_cache.time_work);
}

static struct dma_buf *vc4_prime_export(struct drm_gem_object *obj, int flags)
{
	struct vc4_bo *bo = to_vc4_bo(obj);
	struct dma_buf *dmabuf;
	int ret;

	if (bo->validated_shader) {
		DRM_DEBUG("Attempting to export shader BO\n");
		return ERR_PTR(-EINVAL);
	}

	/* Note: as soon as the BO is exported it becomes unpurgeable, because
	 * noone ever decrements the usecnt even if the reference held by the
	 * exported BO is released. This shouldn't be a problem since we don't
	 * expect exported BOs to be marked as purgeable.
	 */
	ret = vc4_bo_inc_usecnt(bo);
	if (ret) {
		drm_err(obj->dev, "Failed to increment BO usecnt\n");
		return ERR_PTR(ret);
	}

	dmabuf = drm_gem_prime_export(obj, flags);
	if (IS_ERR(dmabuf))
		vc4_bo_dec_usecnt(bo);

	return dmabuf;
}

static vm_fault_t vc4_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct drm_gem_object *obj = vma->vm_private_data;
	struct vc4_bo *bo = to_vc4_bo(obj);

	/* The only reason we would end up here is when user-space accesses
	 * BO's memory after it's been purged.
	 */
	mutex_lock(&bo->madv_lock);
	WARN_ON(bo->madv != __VC4_MADV_PURGED);
	mutex_unlock(&bo->madv_lock);

	return VM_FAULT_SIGBUS;
}

static int vc4_gem_object_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma)
{
	struct vc4_bo *bo = to_vc4_bo(obj);

	if (bo->validated_shader && (vma->vm_flags & VM_WRITE)) {
		DRM_DEBUG("mmapping of shader BOs for writing not allowed.\n");
		return -EINVAL;
	}

	if (bo->madv != VC4_MADV_WILLNEED) {
		DRM_DEBUG("mmapping of %s BO not allowed\n",
			  bo->madv == VC4_MADV_DONTNEED ?
			  "purgeable" : "purged");
		return -EINVAL;
	}

	return drm_gem_dma_mmap(&bo->base, vma);
}

static const struct vm_operations_struct vc4_vm_ops = {
	.fault = vc4_fault,
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

static const struct drm_gem_object_funcs vc4_gem_object_funcs = {
	.free = vc4_free_object,
	.export = vc4_prime_export,
	.get_sg_table = drm_gem_dma_object_get_sg_table,
	.vmap = drm_gem_dma_object_vmap,
	.mmap = vc4_gem_object_mmap,
	.vm_ops = &vc4_vm_ops,
};

static int vc4_grab_bin_bo(struct vc4_dev *vc4, struct vc4_file *vc4file)
{
	if (!vc4->v3d)
		return -ENODEV;

	if (vc4file->bin_bo_used)
		return 0;

	return vc4_v3d_bin_bo_get(vc4, &vc4file->bin_bo_used);
}

int vc4_create_bo_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	struct drm_vc4_create_bo *args = data;
	struct vc4_file *vc4file = file_priv->driver_priv;
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct vc4_bo *bo = NULL;
	int ret;

	if (WARN_ON_ONCE(vc4->gen > VC4_GEN_4))
		return -ENODEV;

	ret = vc4_grab_bin_bo(vc4, vc4file);
	if (ret)
		return ret;

	/*
	 * We can't allocate from the BO cache, because the BOs don't
	 * get zeroed, and that might leak data between users.
	 */
	bo = vc4_bo_create(dev, args->size, false, VC4_BO_TYPE_V3D);
	if (IS_ERR(bo))
		return PTR_ERR(bo);

	bo->madv = VC4_MADV_WILLNEED;

	ret = drm_gem_handle_create(file_priv, &bo->base.base, &args->handle);
	drm_gem_object_put(&bo->base.base);

	return ret;
}

int vc4_mmap_bo_ioctl(struct drm_device *dev, void *data,
		      struct drm_file *file_priv)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct drm_vc4_mmap_bo *args = data;
	struct drm_gem_object *gem_obj;

	if (WARN_ON_ONCE(vc4->gen > VC4_GEN_4))
		return -ENODEV;

	gem_obj = drm_gem_object_lookup(file_priv, args->handle);
	if (!gem_obj) {
		DRM_DEBUG("Failed to look up GEM BO %d\n", args->handle);
		return -EINVAL;
	}

	/* The mmap offset was set up at BO allocation time. */
	args->offset = drm_vma_node_offset_addr(&gem_obj->vma_node);

	drm_gem_object_put(gem_obj);
	return 0;
}

int
vc4_create_shader_bo_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file_priv)
{
	struct drm_vc4_create_shader_bo *args = data;
	struct vc4_file *vc4file = file_priv->driver_priv;
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct vc4_bo *bo = NULL;
	int ret;

	if (WARN_ON_ONCE(vc4->gen > VC4_GEN_4))
		return -ENODEV;

	if (args->size == 0)
		return -EINVAL;

	if (args->size % sizeof(u64) != 0)
		return -EINVAL;

	if (args->flags != 0) {
		DRM_INFO("Unknown flags set: 0x%08x\n", args->flags);
		return -EINVAL;
	}

	if (args->pad != 0) {
		DRM_INFO("Pad set: 0x%08x\n", args->pad);
		return -EINVAL;
	}

	ret = vc4_grab_bin_bo(vc4, vc4file);
	if (ret)
		return ret;

	bo = vc4_bo_create(dev, args->size, true, VC4_BO_TYPE_V3D_SHADER);
	if (IS_ERR(bo))
		return PTR_ERR(bo);

	bo->madv = VC4_MADV_WILLNEED;

	if (copy_from_user(bo->base.vaddr,
			     (void __user *)(uintptr_t)args->data,
			     args->size)) {
		ret = -EFAULT;
		goto fail;
	}
	/* Clear the rest of the memory from allocating from the BO
	 * cache.
	 */
	memset(bo->base.vaddr + args->size, 0,
	       bo->base.base.size - args->size);

	bo->validated_shader = vc4_validate_shader(&bo->base);
	if (!bo->validated_shader) {
		ret = -EINVAL;
		goto fail;
	}

	/* We have to create the handle after validation, to avoid
	 * races for users to do doing things like mmap the shader BO.
	 */
	ret = drm_gem_handle_create(file_priv, &bo->base.base, &args->handle);

fail:
	drm_gem_object_put(&bo->base.base);

	return ret;
}

/**
 * vc4_set_tiling_ioctl() - Sets the tiling modifier for a BO.
 * @dev: DRM device
 * @data: ioctl argument
 * @file_priv: DRM file for this fd
 *
 * The tiling state of the BO decides the default modifier of an fb if
 * no specific modifier was set by userspace, and the return value of
 * vc4_get_tiling_ioctl() (so that userspace can treat a BO it
 * received from dmabuf as the same tiling format as the producer
 * used).
 */
int vc4_set_tiling_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct drm_vc4_set_tiling *args = data;
	struct drm_gem_object *gem_obj;
	struct vc4_bo *bo;
	bool t_format;

	if (WARN_ON_ONCE(vc4->gen > VC4_GEN_4))
		return -ENODEV;

	if (args->flags != 0)
		return -EINVAL;

	switch (args->modifier) {
	case DRM_FORMAT_MOD_NONE:
		t_format = false;
		break;
	case DRM_FORMAT_MOD_BROADCOM_VC4_T_TILED:
		t_format = true;
		break;
	default:
		return -EINVAL;
	}

	gem_obj = drm_gem_object_lookup(file_priv, args->handle);
	if (!gem_obj) {
		DRM_DEBUG("Failed to look up GEM BO %d\n", args->handle);
		return -ENOENT;
	}
	bo = to_vc4_bo(gem_obj);
	bo->t_format = t_format;

	drm_gem_object_put(gem_obj);

	return 0;
}

/**
 * vc4_get_tiling_ioctl() - Gets the tiling modifier for a BO.
 * @dev: DRM device
 * @data: ioctl argument
 * @file_priv: DRM file for this fd
 *
 * Returns the tiling modifier for a BO as set by vc4_set_tiling_ioctl().
 */
int vc4_get_tiling_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct drm_vc4_get_tiling *args = data;
	struct drm_gem_object *gem_obj;
	struct vc4_bo *bo;

	if (WARN_ON_ONCE(vc4->gen > VC4_GEN_4))
		return -ENODEV;

	if (args->flags != 0 || args->modifier != 0)
		return -EINVAL;

	gem_obj = drm_gem_object_lookup(file_priv, args->handle);
	if (!gem_obj) {
		DRM_DEBUG("Failed to look up GEM BO %d\n", args->handle);
		return -ENOENT;
	}
	bo = to_vc4_bo(gem_obj);

	if (bo->t_format)
		args->modifier = DRM_FORMAT_MOD_BROADCOM_VC4_T_TILED;
	else
		args->modifier = DRM_FORMAT_MOD_NONE;

	drm_gem_object_put(gem_obj);

	return 0;
}

int vc4_bo_debugfs_init(struct drm_minor *minor)
{
	struct drm_device *drm = minor->dev;
	struct vc4_dev *vc4 = to_vc4_dev(drm);

	if (!vc4->v3d)
		return -ENODEV;

	drm_debugfs_add_file(drm, "bo_stats", vc4_bo_stats_debugfs, NULL);

	return 0;
}

static void vc4_bo_cache_destroy(struct drm_device *dev, void *unused);
int vc4_bo_cache_init(struct drm_device *dev)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	int ret;
	int i;

	if (WARN_ON_ONCE(vc4->gen > VC4_GEN_4))
		return -ENODEV;

	/* Create the initial set of BO labels that the kernel will
	 * use.  This lets us avoid a bunch of string reallocation in
	 * the kernel's draw and BO allocation paths.
	 */
	vc4->bo_labels = kcalloc(VC4_BO_TYPE_COUNT, sizeof(*vc4->bo_labels),
				 GFP_KERNEL);
	if (!vc4->bo_labels)
		return -ENOMEM;
	vc4->num_labels = VC4_BO_TYPE_COUNT;

	BUILD_BUG_ON(ARRAY_SIZE(bo_type_names) != VC4_BO_TYPE_COUNT);
	for (i = 0; i < VC4_BO_TYPE_COUNT; i++)
		vc4->bo_labels[i].name = bo_type_names[i];

	ret = drmm_mutex_init(dev, &vc4->bo_lock);
	if (ret) {
		kfree(vc4->bo_labels);
		return ret;
	}

	INIT_LIST_HEAD(&vc4->bo_cache.time_list);

	INIT_WORK(&vc4->bo_cache.time_work, vc4_bo_cache_time_work);
	timer_setup(&vc4->bo_cache.time_timer, vc4_bo_cache_time_timer, 0);

	return drmm_add_action_or_reset(dev, vc4_bo_cache_destroy, NULL);
}

static void vc4_bo_cache_destroy(struct drm_device *dev, void *unused)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	int i;

	timer_delete(&vc4->bo_cache.time_timer);
	cancel_work_sync(&vc4->bo_cache.time_work);

	vc4_bo_cache_purge(dev);

	for (i = 0; i < vc4->num_labels; i++) {
		if (vc4->bo_labels[i].num_allocated) {
			drm_err(dev, "Destroying BO cache with %d %s "
				"BOs still allocated\n",
				vc4->bo_labels[i].num_allocated,
				vc4->bo_labels[i].name);
		}

		if (is_user_label(i))
			kfree(vc4->bo_labels[i].name);
	}
	kfree(vc4->bo_labels);
}

int vc4_label_bo_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct drm_vc4_label_bo *args = data;
	char *name;
	struct drm_gem_object *gem_obj;
	int ret = 0, label;

	if (WARN_ON_ONCE(vc4->gen > VC4_GEN_4))
		return -ENODEV;

	if (!args->len)
		return -EINVAL;

	name = strndup_user(u64_to_user_ptr(args->name), args->len + 1);
	if (IS_ERR(name))
		return PTR_ERR(name);

	gem_obj = drm_gem_object_lookup(file_priv, args->handle);
	if (!gem_obj) {
		drm_err(dev, "Failed to look up GEM BO %d\n", args->handle);
		kfree(name);
		return -ENOENT;
	}

	mutex_lock(&vc4->bo_lock);
	label = vc4_get_user_label(vc4, name);
	if (label != -1)
		vc4_bo_set_label(gem_obj, label);
	else
		ret = -ENOMEM;
	mutex_unlock(&vc4->bo_lock);

	drm_gem_object_put(gem_obj);

	return ret;
}
