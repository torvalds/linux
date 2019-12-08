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

	if (bo->sgts) {
		int i;
		int n_sgt = bo->base.base.size / SZ_2M;

		for (i = 0; i < n_sgt; i++) {
			if (bo->sgts[i].sgl) {
				dma_unmap_sg(pfdev->dev, bo->sgts[i].sgl,
					     bo->sgts[i].nents, DMA_BIDIRECTIONAL);
				sg_free_table(&bo->sgts[i]);
			}
		}
		kfree(bo->sgts);
	}

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
	u64 align;
	struct panfrost_gem_object *bo = to_panfrost_bo(obj);
	unsigned long color = bo->noexec ? PANFROST_BO_NOEXEC : 0;
	struct panfrost_file_priv *priv = file_priv->driver_priv;

	/*
	 * Executable buffers cannot cross a 16MB boundary as the program
	 * counter is 24-bits. We assume executable buffers will be less than
	 * 16MB and aligning executable buffers to their size will avoid
	 * crossing a 16MB boundary.
	 */
	if (!bo->noexec)
		align = size >> PAGE_SHIFT;
	else
		align = size >= SZ_2M ? SZ_2M >> PAGE_SHIFT : 0;

	bo->mmu = &priv->mmu;
	spin_lock(&priv->mm_lock);
	ret = drm_mm_insert_node_generic(&priv->mm, &bo->node,
					 size >> PAGE_SHIFT, align, color, 0);
	spin_unlock(&priv->mm_lock);
	if (ret)
		return ret;

	if (!bo->is_heap) {
		ret = panfrost_mmu_map(bo);
		if (ret) {
			spin_lock(&priv->mm_lock);
			drm_mm_remove_node(&bo->node);
			spin_unlock(&priv->mm_lock);
		}
	}
	return ret;
}

static void panfrost_gem_close(struct drm_gem_object *obj, struct drm_file *file_priv)
{
	struct panfrost_gem_object *bo = to_panfrost_bo(obj);
	struct panfrost_file_priv *priv = file_priv->driver_priv;

	if (bo->is_mapped)
		panfrost_mmu_unmap(bo);

	spin_lock(&priv->mm_lock);
	if (drm_mm_node_allocated(&bo->node))
		drm_mm_remove_node(&bo->node);
	spin_unlock(&priv->mm_lock);
}

static int panfrost_gem_pin(struct drm_gem_object *obj)
{
	if (to_panfrost_bo(obj)->is_heap)
		return -EINVAL;

	return drm_gem_shmem_pin(obj);
}

static const struct drm_gem_object_funcs panfrost_gem_funcs = {
	.free = panfrost_gem_free_object,
	.open = panfrost_gem_open,
	.close = panfrost_gem_close,
	.print_info = drm_gem_shmem_print_info,
	.pin = panfrost_gem_pin,
	.unpin = drm_gem_shmem_unpin,
	.get_sg_table = drm_gem_shmem_get_sg_table,
	.vmap = drm_gem_shmem_vmap,
	.vunmap = drm_gem_shmem_vunmap,
	.mmap = drm_gem_shmem_mmap,
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

struct panfrost_gem_object *
panfrost_gem_create_with_handle(struct drm_file *file_priv,
				struct drm_device *dev, size_t size,
				u32 flags,
				uint32_t *handle)
{
	int ret;
	struct drm_gem_shmem_object *shmem;
	struct panfrost_gem_object *bo;

	/* Round up heap allocations to 2MB to keep fault handling simple */
	if (flags & PANFROST_BO_HEAP)
		size = roundup(size, SZ_2M);

	shmem = drm_gem_shmem_create(dev, size);
	if (IS_ERR(shmem))
		return ERR_CAST(shmem);

	bo = to_panfrost_bo(&shmem->base);
	bo->noexec = !!(flags & PANFROST_BO_NOEXEC);
	bo->is_heap = !!(flags & PANFROST_BO_HEAP);

	/*
	 * Allocate an id of idr table where the obj is registered
	 * and handle has the id what user can see.
	 */
	ret = drm_gem_handle_create(file_priv, &shmem->base, handle);
	/* drop reference from allocate - handle holds it now. */
	drm_gem_object_put_unlocked(&shmem->base);
	if (ret)
		return ERR_PTR(ret);

	return bo;
}

struct drm_gem_object *
panfrost_gem_prime_import_sg_table(struct drm_device *dev,
				   struct dma_buf_attachment *attach,
				   struct sg_table *sgt)
{
	struct drm_gem_object *obj;
	struct panfrost_gem_object *bo;

	obj = drm_gem_shmem_prime_import_sg_table(dev, attach, sgt);
	if (IS_ERR(obj))
		return ERR_CAST(obj);

	bo = to_panfrost_bo(obj);
	bo->noexec = true;

	return obj;
}
