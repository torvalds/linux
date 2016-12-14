/*
 * NVIDIA Tegra DRM GEM helper functions
 *
 * Copyright (C) 2012 Sascha Hauer, Pengutronix
 * Copyright (C) 2013-2015 NVIDIA CORPORATION, All rights reserved.
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
#include <linux/iommu.h>
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

	drm_gem_object_unreference_unlocked(&obj->gem);
}

static dma_addr_t tegra_bo_pin(struct host1x_bo *bo, struct sg_table **sgt)
{
	struct tegra_bo *obj = host1x_to_tegra_bo(bo);

	*sgt = obj->sgt;

	return obj->paddr;
}

static void tegra_bo_unpin(struct host1x_bo *bo, struct sg_table *sgt)
{
}

static void *tegra_bo_mmap(struct host1x_bo *bo)
{
	struct tegra_bo *obj = host1x_to_tegra_bo(bo);

	if (obj->vaddr)
		return obj->vaddr;
	else if (obj->gem.import_attach)
		return dma_buf_vmap(obj->gem.import_attach->dmabuf);
	else
		return vmap(obj->pages, obj->num_pages, VM_MAP,
			    pgprot_writecombine(PAGE_KERNEL));
}

static void tegra_bo_munmap(struct host1x_bo *bo, void *addr)
{
	struct tegra_bo *obj = host1x_to_tegra_bo(bo);

	if (obj->vaddr)
		return;
	else if (obj->gem.import_attach)
		dma_buf_vunmap(obj->gem.import_attach->dmabuf, addr);
	else
		vunmap(addr);
}

static void *tegra_bo_kmap(struct host1x_bo *bo, unsigned int page)
{
	struct tegra_bo *obj = host1x_to_tegra_bo(bo);

	if (obj->vaddr)
		return obj->vaddr + page * PAGE_SIZE;
	else if (obj->gem.import_attach)
		return dma_buf_kmap(obj->gem.import_attach->dmabuf, page);
	else
		return vmap(obj->pages + page, 1, VM_MAP,
			    pgprot_writecombine(PAGE_KERNEL));
}

static void tegra_bo_kunmap(struct host1x_bo *bo, unsigned int page,
			    void *addr)
{
	struct tegra_bo *obj = host1x_to_tegra_bo(bo);

	if (obj->vaddr)
		return;
	else if (obj->gem.import_attach)
		dma_buf_kunmap(obj->gem.import_attach->dmabuf, page, addr);
	else
		vunmap(addr);
}

static struct host1x_bo *tegra_bo_get(struct host1x_bo *bo)
{
	struct tegra_bo *obj = host1x_to_tegra_bo(bo);

	drm_gem_object_reference(&obj->gem);

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

static int tegra_bo_iommu_map(struct tegra_drm *tegra, struct tegra_bo *bo)
{
	int prot = IOMMU_READ | IOMMU_WRITE;
	ssize_t err;

	if (bo->mm)
		return -EBUSY;

	bo->mm = kzalloc(sizeof(*bo->mm), GFP_KERNEL);
	if (!bo->mm)
		return -ENOMEM;

	err = drm_mm_insert_node_generic(&tegra->mm, bo->mm, bo->gem.size,
					 PAGE_SIZE, 0, 0, 0);
	if (err < 0) {
		dev_err(tegra->drm->dev, "out of I/O virtual memory: %zd\n",
			err);
		goto free;
	}

	bo->paddr = bo->mm->start;

	err = iommu_map_sg(tegra->domain, bo->paddr, bo->sgt->sgl,
			   bo->sgt->nents, prot);
	if (err < 0) {
		dev_err(tegra->drm->dev, "failed to map buffer: %zd\n", err);
		goto remove;
	}

	bo->size = err;

	return 0;

remove:
	drm_mm_remove_node(bo->mm);
free:
	kfree(bo->mm);
	return err;
}

static int tegra_bo_iommu_unmap(struct tegra_drm *tegra, struct tegra_bo *bo)
{
	if (!bo->mm)
		return 0;

	iommu_unmap(tegra->domain, bo->paddr, bo->size);
	drm_mm_remove_node(bo->mm);
	kfree(bo->mm);

	return 0;
}

static struct tegra_bo *tegra_bo_alloc_object(struct drm_device *drm,
					      size_t size)
{
	struct tegra_bo *bo;
	int err;

	bo = kzalloc(sizeof(*bo), GFP_KERNEL);
	if (!bo)
		return ERR_PTR(-ENOMEM);

	host1x_bo_init(&bo->base, &tegra_bo_ops);
	size = round_up(size, PAGE_SIZE);

	err = drm_gem_object_init(drm, &bo->gem, size);
	if (err < 0)
		goto free;

	err = drm_gem_create_mmap_offset(&bo->gem);
	if (err < 0)
		goto release;

	return bo;

release:
	drm_gem_object_release(&bo->gem);
free:
	kfree(bo);
	return ERR_PTR(err);
}

static void tegra_bo_free(struct drm_device *drm, struct tegra_bo *bo)
{
	if (bo->pages) {
		drm_gem_put_pages(&bo->gem, bo->pages, true, true);
		sg_free_table(bo->sgt);
		kfree(bo->sgt);
	} else if (bo->vaddr) {
		dma_free_wc(drm->dev, bo->gem.size, bo->vaddr, bo->paddr);
	}
}

static int tegra_bo_get_pages(struct drm_device *drm, struct tegra_bo *bo)
{
	struct scatterlist *s;
	unsigned int i;

	bo->pages = drm_gem_get_pages(&bo->gem);
	if (IS_ERR(bo->pages))
		return PTR_ERR(bo->pages);

	bo->num_pages = bo->gem.size >> PAGE_SHIFT;

	bo->sgt = drm_prime_pages_to_sg(bo->pages, bo->num_pages);
	if (IS_ERR(bo->sgt))
		goto put_pages;

	/*
	 * Fake up the SG table so that dma_sync_sg_for_device() can be used
	 * to flush the pages associated with it.
	 *
	 * TODO: Replace this by drm_clflash_sg() once it can be implemented
	 * without relying on symbols that are not exported.
	 */
	for_each_sg(bo->sgt->sgl, s, bo->sgt->nents, i)
		sg_dma_address(s) = sg_phys(s);

	dma_sync_sg_for_device(drm->dev, bo->sgt->sgl, bo->sgt->nents,
			       DMA_TO_DEVICE);

	return 0;

put_pages:
	drm_gem_put_pages(&bo->gem, bo->pages, false, false);
	return PTR_ERR(bo->sgt);
}

static int tegra_bo_alloc(struct drm_device *drm, struct tegra_bo *bo)
{
	struct tegra_drm *tegra = drm->dev_private;
	int err;

	if (tegra->domain) {
		err = tegra_bo_get_pages(drm, bo);
		if (err < 0)
			return err;

		err = tegra_bo_iommu_map(tegra, bo);
		if (err < 0) {
			tegra_bo_free(drm, bo);
			return err;
		}
	} else {
		size_t size = bo->gem.size;

		bo->vaddr = dma_alloc_wc(drm->dev, size, &bo->paddr,
					 GFP_KERNEL | __GFP_NOWARN);
		if (!bo->vaddr) {
			dev_err(drm->dev,
				"failed to allocate buffer of size %zu\n",
				size);
			return -ENOMEM;
		}
	}

	return 0;
}

struct tegra_bo *tegra_bo_create(struct drm_device *drm, size_t size,
				 unsigned long flags)
{
	struct tegra_bo *bo;
	int err;

	bo = tegra_bo_alloc_object(drm, size);
	if (IS_ERR(bo))
		return bo;

	err = tegra_bo_alloc(drm, bo);
	if (err < 0)
		goto release;

	if (flags & DRM_TEGRA_GEM_CREATE_TILED)
		bo->tiling.mode = TEGRA_BO_TILING_MODE_TILED;

	if (flags & DRM_TEGRA_GEM_CREATE_BOTTOM_UP)
		bo->flags |= TEGRA_BO_BOTTOM_UP;

	return bo;

release:
	drm_gem_object_release(&bo->gem);
	kfree(bo);
	return ERR_PTR(err);
}

struct tegra_bo *tegra_bo_create_with_handle(struct drm_file *file,
					     struct drm_device *drm,
					     size_t size,
					     unsigned long flags,
					     u32 *handle)
{
	struct tegra_bo *bo;
	int err;

	bo = tegra_bo_create(drm, size, flags);
	if (IS_ERR(bo))
		return bo;

	err = drm_gem_handle_create(file, &bo->gem, handle);
	if (err) {
		tegra_bo_free_object(&bo->gem);
		return ERR_PTR(err);
	}

	drm_gem_object_unreference_unlocked(&bo->gem);

	return bo;
}

static struct tegra_bo *tegra_bo_import(struct drm_device *drm,
					struct dma_buf *buf)
{
	struct tegra_drm *tegra = drm->dev_private;
	struct dma_buf_attachment *attach;
	struct tegra_bo *bo;
	int err;

	bo = tegra_bo_alloc_object(drm, buf->size);
	if (IS_ERR(bo))
		return bo;

	attach = dma_buf_attach(buf, drm->dev);
	if (IS_ERR(attach)) {
		err = PTR_ERR(attach);
		goto free;
	}

	get_dma_buf(buf);

	bo->sgt = dma_buf_map_attachment(attach, DMA_TO_DEVICE);
	if (IS_ERR(bo->sgt)) {
		err = PTR_ERR(bo->sgt);
		goto detach;
	}

	if (tegra->domain) {
		err = tegra_bo_iommu_map(tegra, bo);
		if (err < 0)
			goto detach;
	} else {
		if (bo->sgt->nents > 1) {
			err = -EINVAL;
			goto detach;
		}

		bo->paddr = sg_dma_address(bo->sgt->sgl);
	}

	bo->gem.import_attach = attach;

	return bo;

detach:
	if (!IS_ERR_OR_NULL(bo->sgt))
		dma_buf_unmap_attachment(attach, bo->sgt, DMA_TO_DEVICE);

	dma_buf_detach(buf, attach);
	dma_buf_put(buf);
free:
	drm_gem_object_release(&bo->gem);
	kfree(bo);
	return ERR_PTR(err);
}

void tegra_bo_free_object(struct drm_gem_object *gem)
{
	struct tegra_drm *tegra = gem->dev->dev_private;
	struct tegra_bo *bo = to_tegra_bo(gem);

	if (tegra->domain)
		tegra_bo_iommu_unmap(tegra, bo);

	if (gem->import_attach) {
		dma_buf_unmap_attachment(gem->import_attach, bo->sgt,
					 DMA_TO_DEVICE);
		drm_prime_gem_destroy(gem, NULL);
	} else {
		tegra_bo_free(gem->dev, bo);
	}

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
			     u32 handle, u64 *offset)
{
	struct drm_gem_object *gem;
	struct tegra_bo *bo;

	gem = drm_gem_object_lookup(file, handle);
	if (!gem) {
		dev_err(drm->dev, "failed to lookup GEM object\n");
		return -EINVAL;
	}

	bo = to_tegra_bo(gem);

	*offset = drm_vma_node_offset_addr(&bo->gem.vma_node);

	drm_gem_object_unreference_unlocked(gem);

	return 0;
}

static int tegra_bo_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct drm_gem_object *gem = vma->vm_private_data;
	struct tegra_bo *bo = to_tegra_bo(gem);
	struct page *page;
	pgoff_t offset;
	int err;

	if (!bo->pages)
		return VM_FAULT_SIGBUS;

	offset = ((unsigned long)vmf->virtual_address - vma->vm_start) >> PAGE_SHIFT;
	page = bo->pages[offset];

	err = vm_insert_page(vma, (unsigned long)vmf->virtual_address, page);
	switch (err) {
	case -EAGAIN:
	case 0:
	case -ERESTARTSYS:
	case -EINTR:
	case -EBUSY:
		return VM_FAULT_NOPAGE;

	case -ENOMEM:
		return VM_FAULT_OOM;
	}

	return VM_FAULT_SIGBUS;
}

const struct vm_operations_struct tegra_bo_vm_ops = {
	.fault = tegra_bo_fault,
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

	if (!bo->pages) {
		unsigned long vm_pgoff = vma->vm_pgoff;

		vma->vm_flags &= ~VM_PFNMAP;
		vma->vm_pgoff = 0;

		ret = dma_mmap_wc(gem->dev->dev, vma, bo->vaddr, bo->paddr,
				  gem->size);
		if (ret) {
			drm_gem_vm_close(vma);
			return ret;
		}

		vma->vm_pgoff = vm_pgoff;
	} else {
		pgprot_t prot = vm_get_page_prot(vma->vm_flags);

		vma->vm_flags |= VM_MIXEDMAP;
		vma->vm_flags &= ~VM_PFNMAP;

		vma->vm_page_prot = pgprot_writecombine(prot);
	}

	return 0;
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

	if (bo->pages) {
		struct scatterlist *sg;
		unsigned int i;

		if (sg_alloc_table(sgt, bo->num_pages, GFP_KERNEL))
			goto free;

		for_each_sg(sgt->sgl, sg, bo->num_pages, i)
			sg_set_page(sg, bo->pages[i], PAGE_SIZE, 0);

		if (dma_map_sg(attach->dev, sgt->sgl, sgt->nents, dir) == 0)
			goto free;
	} else {
		if (sg_alloc_table(sgt, 1, GFP_KERNEL))
			goto free;

		sg_dma_address(sgt->sgl) = bo->paddr;
		sg_dma_len(sgt->sgl) = gem->size;
	}

	return sgt;

free:
	sg_free_table(sgt);
	kfree(sgt);
	return NULL;
}

static void tegra_gem_prime_unmap_dma_buf(struct dma_buf_attachment *attach,
					  struct sg_table *sgt,
					  enum dma_data_direction dir)
{
	struct drm_gem_object *gem = attach->dmabuf->priv;
	struct tegra_bo *bo = to_tegra_bo(gem);

	if (bo->pages)
		dma_unmap_sg(attach->dev, sgt->sgl, sgt->nents, dir);

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
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);

	exp_info.ops = &tegra_gem_prime_dmabuf_ops;
	exp_info.size = gem->size;
	exp_info.flags = flags;
	exp_info.priv = gem;

	return drm_gem_dmabuf_export(drm, &exp_info);
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
