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

	panfrost_mmu_unmap(bo);

	spin_lock(&pfdev->mm_lock);
	drm_mm_remove_node(&bo->node);
	spin_unlock(&pfdev->mm_lock);

	drm_gem_shmem_free_object(obj);
}

static const struct drm_gem_object_funcs panfrost_gem_funcs = {
	.free = panfrost_gem_free_object,
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
	int ret;
	struct panfrost_device *pfdev = dev->dev_private;
	struct panfrost_gem_object *obj;
	u64 align;

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj)
		return NULL;

	obj->base.base.funcs = &panfrost_gem_funcs;

	size = roundup(size, PAGE_SIZE);
	align = size >= SZ_2M ? SZ_2M >> PAGE_SHIFT : 0;

	spin_lock(&pfdev->mm_lock);
	ret = drm_mm_insert_node_generic(&pfdev->mm, &obj->node,
					 size >> PAGE_SHIFT, align, 0, 0);
	spin_unlock(&pfdev->mm_lock);
	if (ret)
		goto free_obj;

	return &obj->base.base;

free_obj:
	kfree(obj);
	return ERR_PTR(ret);
}

struct drm_gem_object *
panfrost_gem_prime_import_sg_table(struct drm_device *dev,
				   struct dma_buf_attachment *attach,
				   struct sg_table *sgt)
{
	struct drm_gem_object *obj;
	struct panfrost_gem_object *pobj;

	obj = drm_gem_shmem_prime_import_sg_table(dev, attach, sgt);
	if (IS_ERR(obj))
		return ERR_CAST(obj);

	pobj = to_panfrost_bo(obj);

	panfrost_mmu_map(pobj);

	return obj;
}
