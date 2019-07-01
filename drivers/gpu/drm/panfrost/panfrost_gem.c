// SPDX-License-Identifier: GPL-2.0
/* Copyright 2019 Linaro, Ltd, Rob Herring <robh@kernel.org> */

#include <linux/err.h>
#include <linux/slab.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>

#include <drm/panfrost_drm.h>
#include "panfrost_device.h"
#include "panfrost_gem.h"
#include "panfrost_mmu.h"

/* Called DRM core on the last userspace/kernel unreference of the
 * BO.
 */
static void panfrost_gem_free_object(struct drm_gem_object *obj)
{
	struct panfrost_gem_object *bo = to_panfrost_bo(obj);
	struct panfrost_device *pfdev = obj->dev->dev_private;

	mutex_lock(&pfdev->shrinker_lock);
	if (!list_empty(&bo->base.madv_list))
		list_del(&bo->base.madv_list);
	mutex_unlock(&pfdev->shrinker_lock);

	drm_gem_shmem_free_object(obj);
}

static int panfrost_gem_open(struct drm_gem_object *obj, struct drm_file *file_priv)
{
	int ret;
	size_t size = obj->size;
	u64 align = size >= SZ_2M ? SZ_2M >> PAGE_SHIFT : 0;
	struct panfrost_gem_object *bo = to_panfrost_bo(obj);
	struct panfrost_device *pfdev = obj->dev->dev_private;

	spin_lock(&pfdev->mm_lock);
	ret = drm_mm_insert_node_generic(&pfdev->mm, &bo->node,
					 size >> PAGE_SHIFT, align, 0, 0);
	if (ret)
		goto out;

	ret = panfrost_mmu_map(bo);
	if (ret)
		drm_mm_remove_node(&bo->node);

out:
	spin_unlock(&pfdev->mm_lock);
	return ret;
}

static void panfrost_gem_close(struct drm_gem_object *obj, struct drm_file *file_priv)
{
	struct panfrost_gem_object *bo = to_panfrost_bo(obj);
	struct panfrost_device *pfdev = obj->dev->dev_private;

	if (bo->is_mapped)
		panfrost_mmu_unmap(bo);

	spin_lock(&pfdev->mm_lock);
	if (drm_mm_node_allocated(&bo->node))
		drm_mm_remove_node(&bo->node);
	spin_unlock(&pfdev->mm_lock);
}

static const struct drm_gem_object_funcs panfrost_gem_funcs = {
	.free = panfrost_gem_free_object,
	.open = panfrost_gem_open,
	.close = panfrost_gem_close,
	.print_info = drm_gem_shmem_print_info,
	.pin = drm_gem_shmem_pin,
	.unpin = drm_gem_shmem_unpin,
	.get_sg_table = drm_gem_shmem_get_sg_table,
	.vmap = drm_gem_shmem_vmap,
	.vunmap = drm_gem_shmem_vunmap,
	.vm_ops = &drm_gem_shmem_vm_ops,
};

/**
 * panfrost_gem_create_object - Implementation of driver->gem_create_object.
 * @dev: DRM device
 * @size: Size in bytes of the memory the object will reference
 *
 * This lets the GEM helpers allocate object structs for us, and keep
 * our BO stats correct.
 */
struct drm_gem_object *panfrost_gem_create_object(struct drm_device *dev, size_t size)
{
	struct panfrost_gem_object *obj;

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj)
		return NULL;

	obj->base.base.funcs = &panfrost_gem_funcs;

	return &obj->base.base;
}

struct drm_gem_object *
panfrost_gem_prime_import_sg_table(struct drm_device *dev,
				   struct dma_buf_attachment *attach,
				   struct sg_table *sgt)
{
	struct drm_gem_object *obj;

	obj = drm_gem_shmem_prime_import_sg_table(dev, attach, sgt);
	if (IS_ERR(obj))
		return ERR_CAST(obj);

	return obj;
}
