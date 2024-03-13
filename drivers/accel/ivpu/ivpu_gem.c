// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2023 Intel Corporation
 */

#include <linux/dma-buf.h>
#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/set_memory.h>
#include <linux/xarray.h>

#include <drm/drm_cache.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_file.h>
#include <drm/drm_utils.h>

#include "ivpu_drv.h"
#include "ivpu_gem.h"
#include "ivpu_hw.h"
#include "ivpu_mmu.h"
#include "ivpu_mmu_context.h"

static const struct drm_gem_object_funcs ivpu_gem_funcs;

static inline void ivpu_dbg_bo(struct ivpu_device *vdev, struct ivpu_bo *bo, const char *action)
{
	ivpu_dbg(vdev, BO,
		 "%6s: bo %8p vpu_addr %9llx size %8zu ctx %d has_pages %d dma_mapped %d mmu_mapped %d wc %d imported %d\n",
		 action, bo, bo->vpu_addr, ivpu_bo_size(bo), bo->ctx ? bo->ctx->id : 0,
		 (bool)bo->base.pages, (bool)bo->base.sgt, bo->mmu_mapped, bo->base.map_wc,
		 (bool)bo->base.base.import_attach);
}

/*
 * ivpu_bo_pin() - pin the backing physical pages and map them to VPU.
 *
 * This function pins physical memory pages, then maps the physical pages
 * to IOMMU address space and finally updates the VPU MMU page tables
 * to allow the VPU to translate VPU address to IOMMU address.
 */
int __must_check ivpu_bo_pin(struct ivpu_bo *bo)
{
	struct ivpu_device *vdev = ivpu_bo_to_vdev(bo);
	int ret = 0;

	mutex_lock(&bo->lock);

	ivpu_dbg_bo(vdev, bo, "pin");
	drm_WARN_ON(&vdev->drm, !bo->ctx);

	if (!bo->mmu_mapped) {
		struct sg_table *sgt = drm_gem_shmem_get_pages_sgt(&bo->base);

		if (IS_ERR(sgt)) {
			ret = PTR_ERR(sgt);
			ivpu_err(vdev, "Failed to map BO in IOMMU: %d\n", ret);
			goto unlock;
		}

		ret = ivpu_mmu_context_map_sgt(vdev, bo->ctx, bo->vpu_addr, sgt,
					       ivpu_bo_is_snooped(bo));
		if (ret) {
			ivpu_err(vdev, "Failed to map BO in MMU: %d\n", ret);
			goto unlock;
		}
		bo->mmu_mapped = true;
	}

unlock:
	mutex_unlock(&bo->lock);

	return ret;
}

static int
ivpu_bo_alloc_vpu_addr(struct ivpu_bo *bo, struct ivpu_mmu_context *ctx,
		       const struct ivpu_addr_range *range)
{
	struct ivpu_device *vdev = ivpu_bo_to_vdev(bo);
	int idx, ret;

	if (!drm_dev_enter(&vdev->drm, &idx))
		return -ENODEV;

	mutex_lock(&bo->lock);

	ret = ivpu_mmu_context_insert_node(ctx, range, ivpu_bo_size(bo), &bo->mm_node);
	if (!ret) {
		bo->ctx = ctx;
		bo->vpu_addr = bo->mm_node.start;
	} else {
		ivpu_err(vdev, "Failed to add BO to context %u: %d\n", ctx->id, ret);
	}

	ivpu_dbg_bo(vdev, bo, "alloc");

	mutex_unlock(&bo->lock);

	drm_dev_exit(idx);

	return ret;
}

static void ivpu_bo_unbind_locked(struct ivpu_bo *bo)
{
	struct ivpu_device *vdev = ivpu_bo_to_vdev(bo);

	lockdep_assert(lockdep_is_held(&bo->lock) || !kref_read(&bo->base.base.refcount));

	if (bo->mmu_mapped) {
		drm_WARN_ON(&vdev->drm, !bo->ctx);
		drm_WARN_ON(&vdev->drm, !bo->vpu_addr);
		drm_WARN_ON(&vdev->drm, !bo->base.sgt);
		ivpu_mmu_context_unmap_sgt(vdev, bo->ctx, bo->vpu_addr, bo->base.sgt);
		bo->mmu_mapped = false;
	}

	if (bo->ctx) {
		ivpu_mmu_context_remove_node(bo->ctx, &bo->mm_node);
		bo->ctx = NULL;
	}

	if (bo->base.base.import_attach)
		return;

	dma_resv_lock(bo->base.base.resv, NULL);
	if (bo->base.sgt) {
		dma_unmap_sgtable(vdev->drm.dev, bo->base.sgt, DMA_BIDIRECTIONAL, 0);
		sg_free_table(bo->base.sgt);
		kfree(bo->base.sgt);
		bo->base.sgt = NULL;
	}
	dma_resv_unlock(bo->base.base.resv);
}

void ivpu_bo_unbind_all_bos_from_context(struct ivpu_device *vdev, struct ivpu_mmu_context *ctx)
{
	struct ivpu_bo *bo;

	if (drm_WARN_ON(&vdev->drm, !ctx))
		return;

	mutex_lock(&vdev->bo_list_lock);
	list_for_each_entry(bo, &vdev->bo_list, bo_list_node) {
		mutex_lock(&bo->lock);
		if (bo->ctx == ctx) {
			ivpu_dbg_bo(vdev, bo, "unbind");
			ivpu_bo_unbind_locked(bo);
		}
		mutex_unlock(&bo->lock);
	}
	mutex_unlock(&vdev->bo_list_lock);
}

struct drm_gem_object *ivpu_gem_create_object(struct drm_device *dev, size_t size)
{
	struct ivpu_bo *bo;

	if (size == 0 || !PAGE_ALIGNED(size))
		return ERR_PTR(-EINVAL);

	bo = kzalloc(sizeof(*bo), GFP_KERNEL);
	if (!bo)
		return ERR_PTR(-ENOMEM);

	bo->base.base.funcs = &ivpu_gem_funcs;
	bo->base.pages_mark_dirty_on_put = true; /* VPU can dirty a BO anytime */

	INIT_LIST_HEAD(&bo->bo_list_node);
	mutex_init(&bo->lock);

	return &bo->base.base;
}

static struct ivpu_bo *
ivpu_bo_create(struct ivpu_device *vdev, u64 size, u32 flags)
{
	struct drm_gem_shmem_object *shmem;
	struct ivpu_bo *bo;

	switch (flags & DRM_IVPU_BO_CACHE_MASK) {
	case DRM_IVPU_BO_CACHED:
	case DRM_IVPU_BO_WC:
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	shmem = drm_gem_shmem_create(&vdev->drm, size);
	if (IS_ERR(shmem))
		return ERR_CAST(shmem);

	bo = to_ivpu_bo(&shmem->base);
	bo->base.map_wc = flags & DRM_IVPU_BO_WC;
	bo->flags = flags;

	mutex_lock(&vdev->bo_list_lock);
	list_add_tail(&bo->bo_list_node, &vdev->bo_list);
	mutex_unlock(&vdev->bo_list_lock);

	return bo;
}

static int ivpu_bo_open(struct drm_gem_object *obj, struct drm_file *file)
{
	struct ivpu_file_priv *file_priv = file->driver_priv;
	struct ivpu_device *vdev = file_priv->vdev;
	struct ivpu_bo *bo = to_ivpu_bo(obj);
	struct ivpu_addr_range *range;

	if (bo->ctx) {
		ivpu_warn(vdev, "Can't add BO to ctx %u: already in ctx %u\n",
			  file_priv->ctx.id, bo->ctx->id);
		return -EALREADY;
	}

	if (bo->flags & DRM_IVPU_BO_SHAVE_MEM)
		range = &vdev->hw->ranges.shave;
	else if (bo->flags & DRM_IVPU_BO_DMA_MEM)
		range = &vdev->hw->ranges.dma;
	else
		range = &vdev->hw->ranges.user;

	return ivpu_bo_alloc_vpu_addr(bo, &file_priv->ctx, range);
}

static void ivpu_bo_free(struct drm_gem_object *obj)
{
	struct ivpu_device *vdev = to_ivpu_device(obj->dev);
	struct ivpu_bo *bo = to_ivpu_bo(obj);

	ivpu_dbg_bo(vdev, bo, "free");

	mutex_lock(&vdev->bo_list_lock);
	list_del(&bo->bo_list_node);
	mutex_unlock(&vdev->bo_list_lock);

	drm_WARN_ON(&vdev->drm, !dma_resv_test_signaled(obj->resv, DMA_RESV_USAGE_READ));

	ivpu_bo_unbind_locked(bo);
	mutex_destroy(&bo->lock);

	drm_WARN_ON(obj->dev, bo->base.pages_use_count > 1);
	drm_gem_shmem_free(&bo->base);
}

static const struct drm_gem_object_funcs ivpu_gem_funcs = {
	.free = ivpu_bo_free,
	.open = ivpu_bo_open,
	.print_info = drm_gem_shmem_object_print_info,
	.pin = drm_gem_shmem_object_pin,
	.unpin = drm_gem_shmem_object_unpin,
	.get_sg_table = drm_gem_shmem_object_get_sg_table,
	.vmap = drm_gem_shmem_object_vmap,
	.vunmap = drm_gem_shmem_object_vunmap,
	.mmap = drm_gem_shmem_object_mmap,
	.vm_ops = &drm_gem_shmem_vm_ops,
};

int ivpu_bo_create_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct ivpu_file_priv *file_priv = file->driver_priv;
	struct ivpu_device *vdev = file_priv->vdev;
	struct drm_ivpu_bo_create *args = data;
	u64 size = PAGE_ALIGN(args->size);
	struct ivpu_bo *bo;
	int ret;

	if (args->flags & ~DRM_IVPU_BO_FLAGS)
		return -EINVAL;

	if (size == 0)
		return -EINVAL;

	bo = ivpu_bo_create(vdev, size, args->flags);
	if (IS_ERR(bo)) {
		ivpu_err(vdev, "Failed to create BO: %pe (ctx %u size %llu flags 0x%x)",
			 bo, file_priv->ctx.id, args->size, args->flags);
		return PTR_ERR(bo);
	}

	ret = drm_gem_handle_create(file, &bo->base.base, &args->handle);
	if (!ret)
		args->vpu_addr = bo->vpu_addr;

	drm_gem_object_put(&bo->base.base);

	return ret;
}

struct ivpu_bo *
ivpu_bo_alloc_internal(struct ivpu_device *vdev, u64 vpu_addr, u64 size, u32 flags)
{
	const struct ivpu_addr_range *range;
	struct ivpu_addr_range fixed_range;
	struct iosys_map map;
	struct ivpu_bo *bo;
	int ret;

	drm_WARN_ON(&vdev->drm, !PAGE_ALIGNED(vpu_addr));
	drm_WARN_ON(&vdev->drm, !PAGE_ALIGNED(size));

	if (vpu_addr) {
		fixed_range.start = vpu_addr;
		fixed_range.end = vpu_addr + size;
		range = &fixed_range;
	} else {
		range = &vdev->hw->ranges.global;
	}

	bo = ivpu_bo_create(vdev, size, flags);
	if (IS_ERR(bo)) {
		ivpu_err(vdev, "Failed to create BO: %pe (vpu_addr 0x%llx size %llu flags 0x%x)",
			 bo, vpu_addr, size, flags);
		return NULL;
	}

	ret = ivpu_bo_alloc_vpu_addr(bo, &vdev->gctx, range);
	if (ret)
		goto err_put;

	ret = ivpu_bo_pin(bo);
	if (ret)
		goto err_put;

	dma_resv_lock(bo->base.base.resv, NULL);
	ret = drm_gem_shmem_vmap(&bo->base, &map);
	dma_resv_unlock(bo->base.base.resv);
	if (ret)
		goto err_put;

	return bo;

err_put:
	drm_gem_object_put(&bo->base.base);
	return NULL;
}

void ivpu_bo_free_internal(struct ivpu_bo *bo)
{
	struct iosys_map map = IOSYS_MAP_INIT_VADDR(bo->base.vaddr);

	dma_resv_lock(bo->base.base.resv, NULL);
	drm_gem_shmem_vunmap(&bo->base, &map);
	dma_resv_unlock(bo->base.base.resv);

	drm_gem_object_put(&bo->base.base);
}

int ivpu_bo_info_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_ivpu_bo_info *args = data;
	struct drm_gem_object *obj;
	struct ivpu_bo *bo;
	int ret = 0;

	obj = drm_gem_object_lookup(file, args->handle);
	if (!obj)
		return -ENOENT;

	bo = to_ivpu_bo(obj);

	mutex_lock(&bo->lock);
	args->flags = bo->flags;
	args->mmap_offset = drm_vma_node_offset_addr(&obj->vma_node);
	args->vpu_addr = bo->vpu_addr;
	args->size = obj->size;
	mutex_unlock(&bo->lock);

	drm_gem_object_put(obj);
	return ret;
}

int ivpu_bo_wait_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_ivpu_bo_wait *args = data;
	struct drm_gem_object *obj;
	unsigned long timeout;
	long ret;

	timeout = drm_timeout_abs_to_jiffies(args->timeout_ns);

	obj = drm_gem_object_lookup(file, args->handle);
	if (!obj)
		return -EINVAL;

	ret = dma_resv_wait_timeout(obj->resv, DMA_RESV_USAGE_READ, true, timeout);
	if (ret == 0) {
		ret = -ETIMEDOUT;
	} else if (ret > 0) {
		ret = 0;
		args->job_status = to_ivpu_bo(obj)->job_status;
	}

	drm_gem_object_put(obj);

	return ret;
}

static void ivpu_bo_print_info(struct ivpu_bo *bo, struct drm_printer *p)
{
	mutex_lock(&bo->lock);

	drm_printf(p, "%-9p %-3u 0x%-12llx %-10lu 0x%-8x %-4u",
		   bo, bo->ctx->id, bo->vpu_addr, bo->base.base.size,
		   bo->flags, kref_read(&bo->base.base.refcount));

	if (bo->base.pages)
		drm_printf(p, " has_pages");

	if (bo->mmu_mapped)
		drm_printf(p, " mmu_mapped");

	if (bo->base.base.import_attach)
		drm_printf(p, " imported");

	drm_printf(p, "\n");

	mutex_unlock(&bo->lock);
}

void ivpu_bo_list(struct drm_device *dev, struct drm_printer *p)
{
	struct ivpu_device *vdev = to_ivpu_device(dev);
	struct ivpu_bo *bo;

	drm_printf(p, "%-9s %-3s %-14s %-10s %-10s %-4s %s\n",
		   "bo", "ctx", "vpu_addr", "size", "flags", "refs", "attribs");

	mutex_lock(&vdev->bo_list_lock);
	list_for_each_entry(bo, &vdev->bo_list, bo_list_node)
		ivpu_bo_print_info(bo, p);
	mutex_unlock(&vdev->bo_list_lock);
}

void ivpu_bo_list_print(struct drm_device *dev)
{
	struct drm_printer p = drm_info_printer(dev->dev);

	ivpu_bo_list(dev, &p);
}
