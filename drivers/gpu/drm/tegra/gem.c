// SPDX-License-Identifier: GPL-2.0-only
/*
 * NVIDIA Tegra DRM GEM helper functions
 *
 * Copyright (C) 2012 Sascha Hauer, Pengutronix
 * Copyright (C) 2013-2015 NVIDIA CORPORATION, All rights reserved.
 *
 * Based on the GEM/CMA helpers
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 */

#include <linux/dma-buf.h>
#include <linux/iommu.h>
#include <linux/module.h>

#include <drm/drm_drv.h>
#include <drm/drm_prime.h>
#include <drm/tegra_drm.h>

#include "drm.h"
#include "gem.h"

MODULE_IMPORT_NS(DMA_BUF);

static unsigned int sg_dma_count_chunks(struct scatterlist *sgl, unsigned int nents)
{
	dma_addr_t next = ~(dma_addr_t)0;
	unsigned int count = 0, i;
	struct scatterlist *s;

	for_each_sg(sgl, s, nents, i) {
		/* sg_dma_address(s) is only valid for entries that have sg_dma_len(s) != 0. */
		if (!sg_dma_len(s))
			continue;

		if (sg_dma_address(s) != next) {
			next = sg_dma_address(s) + sg_dma_len(s);
			count++;
		}
	}

	return count;
}

static inline unsigned int sgt_dma_count_chunks(struct sg_table *sgt)
{
	return sg_dma_count_chunks(sgt->sgl, sgt->nents);
}

static void tegra_bo_put(struct host1x_bo *bo)
{
	struct tegra_bo *obj = host1x_to_tegra_bo(bo);

	drm_gem_object_put(&obj->gem);
}

static struct host1x_bo_mapping *tegra_bo_pin(struct device *dev, struct host1x_bo *bo,
					      enum dma_data_direction direction)
{
	struct tegra_bo *obj = host1x_to_tegra_bo(bo);
	struct drm_gem_object *gem = &obj->gem;
	struct host1x_bo_mapping *map;
	int err;

	map = kzalloc(sizeof(*map), GFP_KERNEL);
	if (!map)
		return ERR_PTR(-ENOMEM);

	kref_init(&map->ref);
	map->bo = host1x_bo_get(bo);
	map->direction = direction;
	map->dev = dev;

	/*
	 * Imported buffers need special treatment to satisfy the semantics of DMA-BUF.
	 */
	if (gem->import_attach) {
		struct dma_buf *buf = gem->import_attach->dmabuf;

		map->attach = dma_buf_attach(buf, dev);
		if (IS_ERR(map->attach)) {
			err = PTR_ERR(map->attach);
			goto free;
		}

		map->sgt = dma_buf_map_attachment(map->attach, direction);
		if (IS_ERR(map->sgt)) {
			dma_buf_detach(buf, map->attach);
			err = PTR_ERR(map->sgt);
			map->sgt = NULL;
			goto free;
		}

		err = sgt_dma_count_chunks(map->sgt);
		map->size = gem->size;

		goto out;
	}

	/*
	 * If we don't have a mapping for this buffer yet, return an SG table
	 * so that host1x can do the mapping for us via the DMA API.
	 */
	map->sgt = kzalloc(sizeof(*map->sgt), GFP_KERNEL);
	if (!map->sgt) {
		err = -ENOMEM;
		goto free;
	}

	if (obj->pages) {
		/*
		 * If the buffer object was allocated from the explicit IOMMU
		 * API code paths, construct an SG table from the pages.
		 */
		err = sg_alloc_table_from_pages(map->sgt, obj->pages, obj->num_pages, 0, gem->size,
						GFP_KERNEL);
		if (err < 0)
			goto free;
	} else {
		/*
		 * If the buffer object had no pages allocated and if it was
		 * not imported, it had to be allocated with the DMA API, so
		 * the DMA API helper can be used.
		 */
		err = dma_get_sgtable(dev, map->sgt, obj->vaddr, obj->iova, gem->size);
		if (err < 0)
			goto free;
	}

	err = dma_map_sgtable(dev, map->sgt, direction, 0);
	if (err)
		goto free_sgt;

out:
	/*
	 * If we've manually mapped the buffer object through the IOMMU, make sure to return the
	 * existing IOVA address of our mapping.
	 */
	if (!obj->mm) {
		map->phys = sg_dma_address(map->sgt->sgl);
		map->chunks = err;
	} else {
		map->phys = obj->iova;
		map->chunks = 1;
	}

	map->size = gem->size;

	return map;

free_sgt:
	sg_free_table(map->sgt);
free:
	kfree(map->sgt);
	kfree(map);
	return ERR_PTR(err);
}

static void tegra_bo_unpin(struct host1x_bo_mapping *map)
{
	if (map->attach) {
		dma_buf_unmap_attachment(map->attach, map->sgt, map->direction);
		dma_buf_detach(map->attach->dmabuf, map->attach);
	} else {
		dma_unmap_sgtable(map->dev, map->sgt, map->direction, 0);
		sg_free_table(map->sgt);
		kfree(map->sgt);
	}

	host1x_bo_put(map->bo);
	kfree(map);
}

static void *tegra_bo_mmap(struct host1x_bo *bo)
{
	struct tegra_bo *obj = host1x_to_tegra_bo(bo);
	struct iosys_map map;
	int ret;

	if (obj->vaddr) {
		return obj->vaddr;
	} else if (obj->gem.import_attach) {
		ret = dma_buf_vmap(obj->gem.import_attach->dmabuf, &map);
		return ret ? NULL : map.vaddr;
	} else {
		return vmap(obj->pages, obj->num_pages, VM_MAP,
			    pgprot_writecombine(PAGE_KERNEL));
	}
}

static void tegra_bo_munmap(struct host1x_bo *bo, void *addr)
{
	struct tegra_bo *obj = host1x_to_tegra_bo(bo);
	struct iosys_map map = IOSYS_MAP_INIT_VADDR(addr);

	if (obj->vaddr)
		return;
	else if (obj->gem.import_attach)
		dma_buf_vunmap(obj->gem.import_attach->dmabuf, &map);
	else
		vunmap(addr);
}

static struct host1x_bo *tegra_bo_get(struct host1x_bo *bo)
{
	struct tegra_bo *obj = host1x_to_tegra_bo(bo);

	drm_gem_object_get(&obj->gem);

	return bo;
}

static const struct host1x_bo_ops tegra_bo_ops = {
	.get = tegra_bo_get,
	.put = tegra_bo_put,
	.pin = tegra_bo_pin,
	.unpin = tegra_bo_unpin,
	.mmap = tegra_bo_mmap,
	.munmap = tegra_bo_munmap,
};

static int tegra_bo_iommu_map(struct tegra_drm *tegra, struct tegra_bo *bo)
{
	int prot = IOMMU_READ | IOMMU_WRITE;
	int err;

	if (bo->mm)
		return -EBUSY;

	bo->mm = kzalloc(sizeof(*bo->mm), GFP_KERNEL);
	if (!bo->mm)
		return -ENOMEM;

	mutex_lock(&tegra->mm_lock);

	err = drm_mm_insert_node_generic(&tegra->mm,
					 bo->mm, bo->gem.size, PAGE_SIZE, 0, 0);
	if (err < 0) {
		dev_err(tegra->drm->dev, "out of I/O virtual memory: %d\n",
			err);
		goto unlock;
	}

	bo->iova = bo->mm->start;

	bo->size = iommu_map_sgtable(tegra->domain, bo->iova, bo->sgt, prot);
	if (!bo->size) {
		dev_err(tegra->drm->dev, "failed to map buffer\n");
		err = -ENOMEM;
		goto remove;
	}

	mutex_unlock(&tegra->mm_lock);

	return 0;

remove:
	drm_mm_remove_node(bo->mm);
unlock:
	mutex_unlock(&tegra->mm_lock);
	kfree(bo->mm);
	return err;
}

static int tegra_bo_iommu_unmap(struct tegra_drm *tegra, struct tegra_bo *bo)
{
	if (!bo->mm)
		return 0;

	mutex_lock(&tegra->mm_lock);
	iommu_unmap(tegra->domain, bo->iova, bo->size);
	drm_mm_remove_node(bo->mm);
	mutex_unlock(&tegra->mm_lock);

	kfree(bo->mm);

	return 0;
}

static const struct drm_gem_object_funcs tegra_gem_object_funcs = {
	.free = tegra_bo_free_object,
	.export = tegra_gem_prime_export,
	.vm_ops = &tegra_bo_vm_ops,
};

static struct tegra_bo *tegra_bo_alloc_object(struct drm_device *drm,
					      size_t size)
{
	struct tegra_bo *bo;
	int err;

	bo = kzalloc(sizeof(*bo), GFP_KERNEL);
	if (!bo)
		return ERR_PTR(-ENOMEM);

	bo->gem.funcs = &tegra_gem_object_funcs;

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
		dma_unmap_sgtable(drm->dev, bo->sgt, DMA_FROM_DEVICE, 0);
		drm_gem_put_pages(&bo->gem, bo->pages, true, true);
		sg_free_table(bo->sgt);
		kfree(bo->sgt);
	} else if (bo->vaddr) {
		dma_free_wc(drm->dev, bo->gem.size, bo->vaddr, bo->iova);
	}
}

static int tegra_bo_get_pages(struct drm_device *drm, struct tegra_bo *bo)
{
	int err;

	bo->pages = drm_gem_get_pages(&bo->gem);
	if (IS_ERR(bo->pages))
		return PTR_ERR(bo->pages);

	bo->num_pages = bo->gem.size >> PAGE_SHIFT;

	bo->sgt = drm_prime_pages_to_sg(bo->gem.dev, bo->pages, bo->num_pages);
	if (IS_ERR(bo->sgt)) {
		err = PTR_ERR(bo->sgt);
		goto put_pages;
	}

	err = dma_map_sgtable(drm->dev, bo->sgt, DMA_FROM_DEVICE, 0);
	if (err)
		goto free_sgt;

	return 0;

free_sgt:
	sg_free_table(bo->sgt);
	kfree(bo->sgt);
put_pages:
	drm_gem_put_pages(&bo->gem, bo->pages, false, false);
	return err;
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

		bo->vaddr = dma_alloc_wc(drm->dev, size, &bo->iova,
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

	drm_gem_object_put(&bo->gem);

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
	struct host1x_bo_mapping *mapping, *tmp;
	struct tegra_bo *bo = to_tegra_bo(gem);

	/* remove all mappings of this buffer object from any caches */
	list_for_each_entry_safe(mapping, tmp, &bo->base.mappings, list) {
		if (mapping->cache)
			host1x_bo_unpin(mapping);
		else
			dev_err(gem->dev->dev, "mapping %p stale for device %s\n", mapping,
				dev_name(mapping->dev));
	}

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

static vm_fault_t tegra_bo_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct drm_gem_object *gem = vma->vm_private_data;
	struct tegra_bo *bo = to_tegra_bo(gem);
	struct page *page;
	pgoff_t offset;

	if (!bo->pages)
		return VM_FAULT_SIGBUS;

	offset = (vmf->address - vma->vm_start) >> PAGE_SHIFT;
	page = bo->pages[offset];

	return vmf_insert_page(vma, vmf->address, page);
}

const struct vm_operations_struct tegra_bo_vm_ops = {
	.fault = tegra_bo_fault,
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

int __tegra_gem_mmap(struct drm_gem_object *gem, struct vm_area_struct *vma)
{
	struct tegra_bo *bo = to_tegra_bo(gem);

	if (!bo->pages) {
		unsigned long vm_pgoff = vma->vm_pgoff;
		int err;

		/*
		 * Clear the VM_PFNMAP flag that was set by drm_gem_mmap(),
		 * and set the vm_pgoff (used as a fake buffer offset by DRM)
		 * to 0 as we want to map the whole buffer.
		 */
		vma->vm_flags &= ~VM_PFNMAP;
		vma->vm_pgoff = 0;

		err = dma_mmap_wc(gem->dev->dev, vma, bo->vaddr, bo->iova,
				  gem->size);
		if (err < 0) {
			drm_gem_vm_close(vma);
			return err;
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

int tegra_drm_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct drm_gem_object *gem;
	int err;

	err = drm_gem_mmap(file, vma);
	if (err < 0)
		return err;

	gem = vma->vm_private_data;

	return __tegra_gem_mmap(gem, vma);
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
		if (sg_alloc_table_from_pages(sgt, bo->pages, bo->num_pages,
					      0, gem->size, GFP_KERNEL) < 0)
			goto free;
	} else {
		if (dma_get_sgtable(attach->dev, sgt, bo->vaddr, bo->iova,
				    gem->size) < 0)
			goto free;
	}

	if (dma_map_sgtable(attach->dev, sgt, dir, 0))
		goto free;

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
		dma_unmap_sgtable(attach->dev, sgt, dir, 0);

	sg_free_table(sgt);
	kfree(sgt);
}

static void tegra_gem_prime_release(struct dma_buf *buf)
{
	drm_gem_dmabuf_release(buf);
}

static int tegra_gem_prime_begin_cpu_access(struct dma_buf *buf,
					    enum dma_data_direction direction)
{
	struct drm_gem_object *gem = buf->priv;
	struct tegra_bo *bo = to_tegra_bo(gem);
	struct drm_device *drm = gem->dev;

	if (bo->pages)
		dma_sync_sgtable_for_cpu(drm->dev, bo->sgt, DMA_FROM_DEVICE);

	return 0;
}

static int tegra_gem_prime_end_cpu_access(struct dma_buf *buf,
					  enum dma_data_direction direction)
{
	struct drm_gem_object *gem = buf->priv;
	struct tegra_bo *bo = to_tegra_bo(gem);
	struct drm_device *drm = gem->dev;

	if (bo->pages)
		dma_sync_sgtable_for_device(drm->dev, bo->sgt, DMA_TO_DEVICE);

	return 0;
}

static int tegra_gem_prime_mmap(struct dma_buf *buf, struct vm_area_struct *vma)
{
	struct drm_gem_object *gem = buf->priv;
	int err;

	err = drm_gem_mmap_obj(gem, gem->size, vma);
	if (err < 0)
		return err;

	return __tegra_gem_mmap(gem, vma);
}

static int tegra_gem_prime_vmap(struct dma_buf *buf, struct iosys_map *map)
{
	struct drm_gem_object *gem = buf->priv;
	struct tegra_bo *bo = to_tegra_bo(gem);
	void *vaddr;

	vaddr = tegra_bo_mmap(&bo->base);
	if (IS_ERR(vaddr))
		return PTR_ERR(vaddr);

	iosys_map_set_vaddr(map, vaddr);

	return 0;
}

static void tegra_gem_prime_vunmap(struct dma_buf *buf, struct iosys_map *map)
{
	struct drm_gem_object *gem = buf->priv;
	struct tegra_bo *bo = to_tegra_bo(gem);

	tegra_bo_munmap(&bo->base, map->vaddr);
}

static const struct dma_buf_ops tegra_gem_prime_dmabuf_ops = {
	.map_dma_buf = tegra_gem_prime_map_dma_buf,
	.unmap_dma_buf = tegra_gem_prime_unmap_dma_buf,
	.release = tegra_gem_prime_release,
	.begin_cpu_access = tegra_gem_prime_begin_cpu_access,
	.end_cpu_access = tegra_gem_prime_end_cpu_access,
	.mmap = tegra_gem_prime_mmap,
	.vmap = tegra_gem_prime_vmap,
	.vunmap = tegra_gem_prime_vunmap,
};

struct dma_buf *tegra_gem_prime_export(struct drm_gem_object *gem,
				       int flags)
{
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);

	exp_info.exp_name = KBUILD_MODNAME;
	exp_info.owner = gem->dev->driver->fops->owner;
	exp_info.ops = &tegra_gem_prime_dmabuf_ops;
	exp_info.size = gem->size;
	exp_info.flags = flags;
	exp_info.priv = gem;

	return drm_gem_dmabuf_export(gem->dev, &exp_info);
}

struct drm_gem_object *tegra_gem_prime_import(struct drm_device *drm,
					      struct dma_buf *buf)
{
	struct tegra_bo *bo;

	if (buf->ops == &tegra_gem_prime_dmabuf_ops) {
		struct drm_gem_object *gem = buf->priv;

		if (gem->dev == drm) {
			drm_gem_object_get(gem);
			return gem;
		}
	}

	bo = tegra_bo_import(drm, buf);
	if (IS_ERR(bo))
		return ERR_CAST(bo);

	return &bo->gem;
}

struct host1x_bo *tegra_gem_lookup(struct drm_file *file, u32 handle)
{
	struct drm_gem_object *gem;
	struct tegra_bo *bo;

	gem = drm_gem_object_lookup(file, handle);
	if (!gem)
		return NULL;

	bo = to_tegra_bo(gem);
	return &bo->base;
}
