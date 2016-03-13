/*
 *  Copyright © 2015 Broadcom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* DOC: VC4 GEM BO management support.
 *
 * The VC4 GPU architecture (both scanout and rendering) has direct
 * access to system memory with no MMU in between.  To support it, we
 * use the GEM CMA helper functions to allocate contiguous ranges of
 * physical memory for our BOs.
 *
 * Since the CMA allocator is very slow, we keep a cache of recently
 * freed BOs around so that the kernel's allocation of objects for 3D
 * rendering can return quickly.
 */

#include "vc4_drv.h"
#include "uapi/drm/vc4_drm.h"

static void vc4_bo_stats_dump(struct vc4_dev *vc4)
{
	DRM_INFO("num bos allocated: %d\n",
		 vc4->bo_stats.num_allocated);
	DRM_INFO("size bos allocated: %dkb\n",
		 vc4->bo_stats.size_allocated / 1024);
	DRM_INFO("num bos used: %d\n",
		 vc4->bo_stats.num_allocated - vc4->bo_stats.num_cached);
	DRM_INFO("size bos used: %dkb\n",
		 (vc4->bo_stats.size_allocated -
		  vc4->bo_stats.size_cached) / 1024);
	DRM_INFO("num bos cached: %d\n",
		 vc4->bo_stats.num_cached);
	DRM_INFO("size bos cached: %dkb\n",
		 vc4->bo_stats.size_cached / 1024);
}

#ifdef CONFIG_DEBUG_FS
int vc4_bo_stats_debugfs(struct seq_file *m, void *unused)
{
	struct drm_info_node *node = (struct drm_info_node *)m->private;
	struct drm_device *dev = node->minor->dev;
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct vc4_bo_stats stats;

	/* Take a snapshot of the current stats with the lock held. */
	mutex_lock(&vc4->bo_lock);
	stats = vc4->bo_stats;
	mutex_unlock(&vc4->bo_lock);

	seq_printf(m, "num bos allocated: %d\n",
		   stats.num_allocated);
	seq_printf(m, "size bos allocated: %dkb\n",
		   stats.size_allocated / 1024);
	seq_printf(m, "num bos used: %d\n",
		   stats.num_allocated - stats.num_cached);
	seq_printf(m, "size bos used: %dkb\n",
		   (stats.size_allocated - stats.size_cached) / 1024);
	seq_printf(m, "num bos cached: %d\n",
		   stats.num_cached);
	seq_printf(m, "size bos cached: %dkb\n",
		   stats.size_cached / 1024);

	return 0;
}
#endif

static uint32_t bo_page_index(size_t size)
{
	return (size / PAGE_SIZE) - 1;
}

/* Must be called with bo_lock held. */
static void vc4_bo_destroy(struct vc4_bo *bo)
{
	struct drm_gem_object *obj = &bo->base.base;
	struct vc4_dev *vc4 = to_vc4_dev(obj->dev);

	if (bo->validated_shader) {
		kfree(bo->validated_shader->texture_samples);
		kfree(bo->validated_shader);
		bo->validated_shader = NULL;
	}

	vc4->bo_stats.num_allocated--;
	vc4->bo_stats.size_allocated -= obj->size;
	drm_gem_cma_free_object(obj);
}

/* Must be called with bo_lock held. */
static void vc4_bo_remove_from_cache(struct vc4_bo *bo)
{
	struct drm_gem_object *obj = &bo->base.base;
	struct vc4_dev *vc4 = to_vc4_dev(obj->dev);

	vc4->bo_stats.num_cached--;
	vc4->bo_stats.size_cached -= obj->size;

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

void vc4_bo_cache_purge(struct drm_device *dev)
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

static struct vc4_bo *vc4_bo_get_from_cache(struct drm_device *dev,
					    uint32_t size)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	uint32_t page_index = bo_page_index(size);
	struct vc4_bo *bo = NULL;

	size = roundup(size, PAGE_SIZE);

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
	mutex_unlock(&vc4->bo_lock);
	return bo;
}

/**
 * vc4_gem_create_object - Implementation of driver->gem_create_object.
 *
 * This lets the CMA helpers allocate object structs for us, and keep
 * our BO stats correct.
 */
struct drm_gem_object *vc4_create_object(struct drm_device *dev, size_t size)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct vc4_bo *bo;

	bo = kzalloc(sizeof(*bo), GFP_KERNEL);
	if (!bo)
		return ERR_PTR(-ENOMEM);

	mutex_lock(&vc4->bo_lock);
	vc4->bo_stats.num_allocated++;
	vc4->bo_stats.size_allocated += size;
	mutex_unlock(&vc4->bo_lock);

	return &bo->base.base;
}

struct vc4_bo *vc4_bo_create(struct drm_device *dev, size_t unaligned_size,
			     bool from_cache)
{
	size_t size = roundup(unaligned_size, PAGE_SIZE);
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct drm_gem_cma_object *cma_obj;

	if (size == 0)
		return ERR_PTR(-EINVAL);

	/* First, try to get a vc4_bo from the kernel BO cache. */
	if (from_cache) {
		struct vc4_bo *bo = vc4_bo_get_from_cache(dev, size);

		if (bo)
			return bo;
	}

	cma_obj = drm_gem_cma_create(dev, size);
	if (IS_ERR(cma_obj)) {
		/*
		 * If we've run out of CMA memory, kill the cache of
		 * CMA allocations we've got laying around and try again.
		 */
		vc4_bo_cache_purge(dev);

		cma_obj = drm_gem_cma_create(dev, size);
		if (IS_ERR(cma_obj)) {
			DRM_ERROR("Failed to allocate from CMA:\n");
			vc4_bo_stats_dump(vc4);
			return ERR_PTR(-ENOMEM);
		}
	}

	return to_vc4_bo(&cma_obj->base);
}

int vc4_dumb_create(struct drm_file *file_priv,
		    struct drm_device *dev,
		    struct drm_mode_create_dumb *args)
{
	int min_pitch = DIV_ROUND_UP(args->width * args->bpp, 8);
	struct vc4_bo *bo = NULL;
	int ret;

	if (args->pitch < min_pitch)
		args->pitch = min_pitch;

	if (args->size < args->pitch * args->height)
		args->size = args->pitch * args->height;

	bo = vc4_bo_create(dev, args->size, false);
	if (IS_ERR(bo))
		return PTR_ERR(bo);

	ret = drm_gem_handle_create(file_priv, &bo->base.base, &args->handle);
	drm_gem_object_unreference_unlocked(&bo->base.base);

	return ret;
}

/* Must be called with bo_lock held. */
static void vc4_bo_cache_free_old(struct drm_device *dev)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	unsigned long expire_time = jiffies - msecs_to_jiffies(1000);

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
 *
 * Note that this is called with the struct_mutex held.
 */
void vc4_free_object(struct drm_gem_object *gem_bo)
{
	struct drm_device *dev = gem_bo->dev;
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct vc4_bo *bo = to_vc4_bo(gem_bo);
	struct list_head *cache_list;

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

	cache_list = vc4_get_cache_list_for_size(dev, gem_bo->size);
	if (!cache_list) {
		vc4_bo_destroy(bo);
		goto out;
	}

	if (bo->validated_shader) {
		kfree(bo->validated_shader->texture_samples);
		kfree(bo->validated_shader);
		bo->validated_shader = NULL;
	}

	bo->free_time = jiffies;
	list_add(&bo->size_head, cache_list);
	list_add(&bo->unref_head, &vc4->bo_cache.time_list);

	vc4->bo_stats.num_cached++;
	vc4->bo_stats.size_cached += gem_bo->size;

	vc4_bo_cache_free_old(dev);

out:
	mutex_unlock(&vc4->bo_lock);
}

static void vc4_bo_cache_time_work(struct work_struct *work)
{
	struct vc4_dev *vc4 =
		container_of(work, struct vc4_dev, bo_cache.time_work);
	struct drm_device *dev = vc4->dev;

	mutex_lock(&vc4->bo_lock);
	vc4_bo_cache_free_old(dev);
	mutex_unlock(&vc4->bo_lock);
}

static void vc4_bo_cache_time_timer(unsigned long data)
{
	struct drm_device *dev = (struct drm_device *)data;
	struct vc4_dev *vc4 = to_vc4_dev(dev);

	schedule_work(&vc4->bo_cache.time_work);
}

struct dma_buf *
vc4_prime_export(struct drm_device *dev, struct drm_gem_object *obj, int flags)
{
	struct vc4_bo *bo = to_vc4_bo(obj);

	if (bo->validated_shader) {
		DRM_ERROR("Attempting to export shader BO\n");
		return ERR_PTR(-EINVAL);
	}

	return drm_gem_prime_export(dev, obj, flags);
}

int vc4_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_gem_object *gem_obj;
	struct vc4_bo *bo;
	int ret;

	ret = drm_gem_mmap(filp, vma);
	if (ret)
		return ret;

	gem_obj = vma->vm_private_data;
	bo = to_vc4_bo(gem_obj);

	if (bo->validated_shader && (vma->vm_flags & VM_WRITE)) {
		DRM_ERROR("mmaping of shader BOs for writing not allowed.\n");
		return -EINVAL;
	}

	/*
	 * Clear the VM_PFNMAP flag that was set by drm_gem_mmap(), and set the
	 * vm_pgoff (used as a fake buffer offset by DRM) to 0 as we want to map
	 * the whole buffer.
	 */
	vma->vm_flags &= ~VM_PFNMAP;
	vma->vm_pgoff = 0;

	ret = dma_mmap_writecombine(bo->base.base.dev->dev, vma,
				    bo->base.vaddr, bo->base.paddr,
				    vma->vm_end - vma->vm_start);
	if (ret)
		drm_gem_vm_close(vma);

	return ret;
}

int vc4_prime_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma)
{
	struct vc4_bo *bo = to_vc4_bo(obj);

	if (bo->validated_shader && (vma->vm_flags & VM_WRITE)) {
		DRM_ERROR("mmaping of shader BOs for writing not allowed.\n");
		return -EINVAL;
	}

	return drm_gem_cma_prime_mmap(obj, vma);
}

void *vc4_prime_vmap(struct drm_gem_object *obj)
{
	struct vc4_bo *bo = to_vc4_bo(obj);

	if (bo->validated_shader) {
		DRM_ERROR("mmaping of shader BOs not allowed.\n");
		return ERR_PTR(-EINVAL);
	}

	return drm_gem_cma_prime_vmap(obj);
}

int vc4_create_bo_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	struct drm_vc4_create_bo *args = data;
	struct vc4_bo *bo = NULL;
	int ret;

	/*
	 * We can't allocate from the BO cache, because the BOs don't
	 * get zeroed, and that might leak data between users.
	 */
	bo = vc4_bo_create(dev, args->size, false);
	if (IS_ERR(bo))
		return PTR_ERR(bo);

	ret = drm_gem_handle_create(file_priv, &bo->base.base, &args->handle);
	drm_gem_object_unreference_unlocked(&bo->base.base);

	return ret;
}

int vc4_mmap_bo_ioctl(struct drm_device *dev, void *data,
		      struct drm_file *file_priv)
{
	struct drm_vc4_mmap_bo *args = data;
	struct drm_gem_object *gem_obj;

	gem_obj = drm_gem_object_lookup(dev, file_priv, args->handle);
	if (!gem_obj) {
		DRM_ERROR("Failed to look up GEM BO %d\n", args->handle);
		return -EINVAL;
	}

	/* The mmap offset was set up at BO allocation time. */
	args->offset = drm_vma_node_offset_addr(&gem_obj->vma_node);

	drm_gem_object_unreference_unlocked(gem_obj);
	return 0;
}

int
vc4_create_shader_bo_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file_priv)
{
	struct drm_vc4_create_shader_bo *args = data;
	struct vc4_bo *bo = NULL;
	int ret;

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

	bo = vc4_bo_create(dev, args->size, true);
	if (IS_ERR(bo))
		return PTR_ERR(bo);

	ret = copy_from_user(bo->base.vaddr,
			     (void __user *)(uintptr_t)args->data,
			     args->size);
	if (ret != 0)
		goto fail;
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
	drm_gem_object_unreference_unlocked(&bo->base.base);

	return ret;
}

void vc4_bo_cache_init(struct drm_device *dev)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);

	mutex_init(&vc4->bo_lock);

	INIT_LIST_HEAD(&vc4->bo_cache.time_list);

	INIT_WORK(&vc4->bo_cache.time_work, vc4_bo_cache_time_work);
	setup_timer(&vc4->bo_cache.time_timer,
		    vc4_bo_cache_time_timer,
		    (unsigned long)dev);
}

void vc4_bo_cache_destroy(struct drm_device *dev)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);

	del_timer(&vc4->bo_cache.time_timer);
	cancel_work_sync(&vc4->bo_cache.time_work);

	vc4_bo_cache_purge(dev);

	if (vc4->bo_stats.num_allocated) {
		DRM_ERROR("Destroying BO cache while BOs still allocated:\n");
		vc4_bo_stats_dump(vc4);
	}
}
