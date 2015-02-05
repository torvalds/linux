/*
 * NVIDIA Tegra DRM GEM helper functions
 *
 * Copyright (C) 2012 Sascha Hauer, Pengutronix
 * Copyright (C) 2013 NVIDIA CORPORATION, All rights reserved.
 *
 * Based on the GEM/CMA helpers
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/dma-buf.h>
#include <drm/tegra_drm.h>

#include "drm.h"
#include "gem.h"

static inline struct tegra_bo *host1x_to_tegra_bo(struct host1x_bo *bo)
{
	return container_of(bo, struct tegra_bo, base);
}

static void tegra_bo_put(struct host1x_bo *bo)
{
	struct tegra_bo *obj = host1x_to_tegra_bo(bo);
	struct drm_device *drm = obj->gem.dev;

	mutex_lock(&drm->struct_mutex);
	drm_gem_object_unreference(&obj->gem);
	mutex_unlock(&drm->struct_mutex);
}

static dma_addr_t tegra_bo_pin(struct host1x_bo *bo, struct sg_table **sgt)
{
	struct tegra_bo *obj = host1x_to_tegra_bo(bo);

	return obj->paddr;
}

static void tegra_bo_unpin(struct host1x_bo *bo, struct sg_table *sgt)
{
}

static void *tegra_bo_mmap(struct host1x_bo *bo)
{
	struct tegra_bo *obj = host1x_to_tegra_bo(bo);

	return obj->vaddr;
}

static void tegra_bo_munmap(struct host1x_bo *bo, void *addr)
{
}

static void *tegra_bo_kmap(struct host1x_bo *bo, unsigned int page)
{
	struct tegra_bo *obj = host1x_to_tegra_bo(bo);

	return obj->vaddr + page * PAGE_SIZE;
}

static void tegra_bo_kunmap(struct host1x_bo *bo, unsigned int page,
			    void *addr)
{
}

static struct host1x_bo *tegra_bo_get(struct host1x_bo *bo)
{
	struct tegra_bo *obj = host1x_to_tegra_bo(bo);
	struct drm_device *drm = obj->gem.dev;

	mutex_lock(&drm->struct_mutex);
	drm_gem_object_reference(&obj->gem);
	mutex_unlock(&drm->struct_mutex);

	return bo;
}

static const struct host1x_bo_ops tegra_bo_ops = {
	.get = tegra_bo_get,
	.put = tegra_bo_put,
	.pin = tegra_bo_pin,
	.unpin = tegra_bo_unpin,
	.mmap = tegra_bo_mmap,
	.munmap = tegra_bo_munmap,
	.kmap = tegra_bo_kmap,
	.kunmap = tegra_bo_kunmap,
};

static void tegra_bo_destroy(struct drm_device *drm, struct tegra_bo *bo)
{
	dma_free_writecombine(drm->dev, bo->gem.size, bo->vaddr, bo->paddr);
}

struct tegra_bo *tegra_bo_create(struct drm_device *drm, unsigned int size,
				 unsigned long flags)
{
	struct tegra_bo *bo;
	int err;

	bo = kzalloc(sizeof(*bo), GFP_KERNEL);
	if (!bo)
		return ERR_PTR(-ENOMEM);

	host1x_bo_init(&bo->base, &tegra_bo_ops);
	size = round_up(size, PAGE_SIZE);

	bo->vaddr = dma_alloc_writecombine(drm->dev, size, &bo->paddr,
					   GFP_KERNEL | __GFP_NOWARN);
	if (!bo->vaddr) {
		dev_err(drm->dev, "failed to allocate buffer with size %u\n",
			size);
		err = -ENOMEM;
		goto err_dma;
	}

	err = drm_gem_object_init(drm, &bo->gem, size);
	if (err)
		goto err_init;

	err = drm_gem_create_mmap_offset(&bo->gem);
	if (err)
		goto err_mmap;

	if (flags & DRM_TEGRA_GEM_CREATE_TILED)
		bo->tiling.mode = TEGRA_BO_TILING_MODE_TILED;

	if (flags & DRM_TEGRA_GEM_CREATE_BOTTOM_UP)
		bo->flags |= TEGRA_BO_BOTTOM_UP;

	return bo;

err_mmap:
	drm_gem_object_release(&bo->gem);
err_init:
	tegra_bo_destroy(drm, bo);
err_dma:
	kfree(bo);

	return ERR_PTR(err);
}

struct tegra_bo *tegra_bo_create_with_handle(struct drm_file *file,
					     struct drm_device *drm,
					     unsigned int size,
					     unsigned long flags,
					     unsigned int *handle)
{
	struct tegra_bo *bo;
	int ret;

	bo = tegra_bo_create(drm, size, flags);
	if (IS_ERR(bo))
		return bo;

	ret = drm_gem_handle_create(file, &bo->gem, handle);
	if (ret)
		goto err;

	drm_gem_object_unreference_unlocked(&bo->gem);

	return bo;

err:
	tegra_bo_free_object(&bo->gem);
	return ERR_PTR(ret);
}

static struct tegra_bo *tegra_bo_import(struct drm_device *drm,
					struct dma_buf *buf)
{
	struct dma_buf_attachment *attach;
	struct tegra_bo *bo;
	ssize_t size;
	int err;

	bo = kzalloc(sizeof(*bo), GFP_KERNEL);
	if (!bo)
		return ERR_PTR(-ENOMEM);

	host1x_bo_init(&bo->base, &tegra_bo_ops);
	size = round_up(buf->size, PAGE_SIZE);

	err = drm_gem_object_init(drm, &bo->gem, size);
	if (err < 0)
		goto free;

	err = drm_gem_create_mmap_offset(&bo->gem);
	if (err < 0)
		goto release;

	attach = dma_buf_attach(buf, drm->dev);
	if (IS_ERR(attach)) {
		err = PTR_ERR(attach);
		goto free_mmap;
	}

	get_dma_buf(buf);

	bo->sgt = dma_buf_map_attachment(attach, DMA_TO_DEVICE);
	if (!bo->sgt) {
		err = -ENOMEM;
		goto detach;
	}

	if (IS_ERR(bo->sgt)) {
		err = PTR_ERR(bo->sgt);
		goto detach;
	}

	if (bo->sgt->nents > 1) {
		err = -EINVAL;
		goto detach;
	}

	bo->paddr = sg_dma_address(bo->sgt->sgl);
	bo->gem.import_attach = attach;

	return bo;

detach:
	if (!IS_ERR_OR_NULL(bo->sgt))
		dma_buf_unmap_attachment(attach, bo->sgt, DMA_TO_DEVICE);

	dma_buf_detach(buf, attach);
	dma_buf_put(buf);
free_mmap:
	drm_gem_free_mmap_offset(&bo->gem);
release:
	drm_gem_object_release(&bo->gem);
free:
	kfree(bo);

	return ERR_PTR(err);
}

void tegra_bo_free_object(struct drm_gem_object *gem)
{
	struct tegra_bo *bo = to_tegra_bo(gem);

	if (gem->import_attach) {
		dma_buf_unmap_attachment(gem->import_attach, bo->sgt,
					 DMA_TO_DEVICE);
		drm_prime_gem_destroy(gem, NULL);
	} else {
		tegra_bo_destroy(gem->dev, bo);
	}

	drm_gem_free_mmap_offset(gem);
	drm_gem_object_release(gem);

	kfree(bo);
}

int tegra_bo_dumb_create(struct drm_file *file, struct drm_device *drm,
			 struct drm_mode_create_dumb *args)
{
	unsigned int min_pitch = DIV_ROUND_UP(args->width * args->bpp, 8);
	struct tegra_drm *tegra = drm->dev_private;
	struct tegra_bo *bo;

	args->pitch = round_up(min_pitch, tegra->pitch_align);
	args->size = args->pitch * args->height;

	bo = tegra_bo_create_with_handle(file, drm, args->size, 0,
					 &args->handle);
	if (IS_ERR(bo))
		return PTR_ERR(bo);

	return 0;
}

int tegra_bo_dumb_map_offset(struct drm_file *file, struct drm_device *drm,
			     uint32_t handle, uint64_t *offset)
{
	struct drm_gem_object *gem;
	struct tegra_bo *bo;

	mutex_lock(&drm->struct_mutex);

	gem = drm_gem_object_lookup(drm, file, handle);
	if (!gem) {
		dev_err(drm->dev, "failed to lookup GEM object\n");
		mutex_unlock(&drm->struct_mutex);
		return -EINVAL;
	}

	bo = to_tegra_bo(gem);

	*offset = drm_vma_node_offset_addr(&bo->gem.vma_node);

	drm_gem_object_unreference(gem);

	mutex_unlock(&drm->struct_mutex);

	return 0;
}

const struct vm_operations_struct tegra_bo_vm_ops = {
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

int tegra_drm_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct drm_gem_object *gem;
	struct tegra_bo *bo;
	int ret;

	ret = drm_gem_mmap(file, vma);
	if (ret)
		return ret;

	gem = vma->vm_private_data;
	bo = to_tegra_bo(gem);

	ret = remap_pfn_range(vma, vma->vm_start, bo->paddr >> PAGE_SHIFT,
			      vma->vm_end - vma->vm_start, vma->vm_page_prot);
	if (ret)
		drm_gem_vm_close(vma);

	return ret;
}

static struct sg_table *
tegra_gem_prime_map_dma_buf(struct dma_buf_attachment *attach,
			    enum dma_data_direction dir)
{
	struct drm_gem_object *gem = attach->dmabuf->priv;
	struct tegra_bo *bo = to_tegra_bo(gem);
	struct sg_table *sgt;

	sgt = kmalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt)
		return NULL;

	if (sg_alloc_table(sgt, 1, GFP_KERNEL)) {
		kfree(sgt);
		return NULL;
	}

	sg_dma_address(sgt->sgl) = bo->paddr;
	sg_dma_len(sgt->sgl) = gem->size;

	return sgt;
}

static void tegra_gem_prime_unmap_dma_buf(struct dma_buf_attachment *attach,
					  struct sg_table *sgt,
					  enum dma_data_direction dir)
{
	sg_free_table(sgt);
	kfree(sgt);
}

static void tegra_gem_prime_release(struct dma_buf *buf)
{
	drm_gem_dmabuf_release(buf);
}

static void *tegra_gem_prime_kmap_atomic(struct dma_buf *buf,
					 unsigned long page)
{
	return NULL;
}

static void tegra_gem_prime_kunmap_atomic(struct dma_buf *buf,
					  unsigned long page,
					  void *addr)
{
}

static void *tegra_gem_prime_kmap(struct dma_buf *buf, unsigned long page)
{
	return NULL;
}

static void tegra_gem_prime_kunmap(struct dma_buf *buf, unsigned long page,
				   void *addr)
{
}

static int tegra_gem_prime_mmap(struct dma_buf *buf, struct vm_area_struct *vma)
{
	return -EINVAL;
}

static void *tegra_gem_prime_vmap(struct dma_buf *buf)
{
	struct drm_gem_object *gem = buf->priv;
	struct tegra_bo *bo = to_tegra_bo(gem);

	return bo->vaddr;
}

static void tegra_gem_prime_vunmap(struct dma_buf *buf, void *vaddr)
{
}

static const struct dma_buf_ops tegra_gem_prime_dmabuf_ops = {
	.map_dma_buf = tegra_gem_prime_map_dma_buf,
	.unmap_dma_buf = tegra_gem_prime_unmap_dma_buf,
	.release = tegra_gem_prime_release,
	.kmap_atomic = tegra_gem_prime_kmap_atomic,
	.kunmap_atomic = tegra_gem_prime_kunmap_atomic,
	.kmap = tegra_gem_prime_kmap,
	.kunmap = tegra_gem_prime_kunmap,
	.mmap = tegra_gem_prime_mmap,
	.vmap = tegra_gem_prime_vmap,
	.vunmap = tegra_gem_prime_vunmap,
};

struct dma_buf *tegra_gem_prime_export(struct drm_device *drm,
				       struct drm_gem_object *gem,
				       int flags)
{
	return dma_buf_export(gem, &tegra_gem_prime_dmabuf_ops, gem->size,
			      flags, NULL);
}

struct drm_gem_object *tegra_gem_prime_import(struct drm_device *drm,
					      struct dma_buf *buf)
{
	struct tegra_bo *bo;

	if (buf->ops == &tegra_gem_prime_dmabuf_ops) {
		struct drm_gem_object *gem = buf->priv;

		if (gem->dev == drm) {
			drm_gem_object_reference(gem);
			return gem;
		}
	}

	bo = tegra_bo_import(drm, buf);
	if (IS_ERR(bo))
		return ERR_CAST(bo);

	return &bo->gem;
}
