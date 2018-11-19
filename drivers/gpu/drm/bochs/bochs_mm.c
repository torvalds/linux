/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "bochs.h"

static void bochs_ttm_placement(struct bochs_bo *bo, int domain);

/* ---------------------------------------------------------------------- */

static inline struct bochs_device *bochs_bdev(struct ttm_bo_device *bd)
{
	return container_of(bd, struct bochs_device, ttm.bdev);
}

static void bochs_bo_ttm_destroy(struct ttm_buffer_object *tbo)
{
	struct bochs_bo *bo;

	bo = container_of(tbo, struct bochs_bo, bo);
	drm_gem_object_release(&bo->gem);
	kfree(bo);
}

static bool bochs_ttm_bo_is_bochs_bo(struct ttm_buffer_object *bo)
{
	if (bo->destroy == &bochs_bo_ttm_destroy)
		return true;
	return false;
}

static int bochs_bo_init_mem_type(struct ttm_bo_device *bdev, uint32_t type,
				  struct ttm_mem_type_manager *man)
{
	switch (type) {
	case TTM_PL_SYSTEM:
		man->flags = TTM_MEMTYPE_FLAG_MAPPABLE;
		man->available_caching = TTM_PL_MASK_CACHING;
		man->default_caching = TTM_PL_FLAG_CACHED;
		break;
	case TTM_PL_VRAM:
		man->func = &ttm_bo_manager_func;
		man->flags = TTM_MEMTYPE_FLAG_FIXED |
			TTM_MEMTYPE_FLAG_MAPPABLE;
		man->available_caching = TTM_PL_FLAG_UNCACHED |
			TTM_PL_FLAG_WC;
		man->default_caching = TTM_PL_FLAG_WC;
		break;
	default:
		DRM_ERROR("Unsupported memory type %u\n", (unsigned)type);
		return -EINVAL;
	}
	return 0;
}

static void
bochs_bo_evict_flags(struct ttm_buffer_object *bo, struct ttm_placement *pl)
{
	struct bochs_bo *bochsbo = bochs_bo(bo);

	if (!bochs_ttm_bo_is_bochs_bo(bo))
		return;

	bochs_ttm_placement(bochsbo, TTM_PL_FLAG_SYSTEM);
	*pl = bochsbo->placement;
}

static int bochs_bo_verify_access(struct ttm_buffer_object *bo,
				  struct file *filp)
{
	struct bochs_bo *bochsbo = bochs_bo(bo);

	return drm_vma_node_verify_access(&bochsbo->gem.vma_node,
					  filp->private_data);
}

static int bochs_ttm_io_mem_reserve(struct ttm_bo_device *bdev,
				    struct ttm_mem_reg *mem)
{
	struct ttm_mem_type_manager *man = &bdev->man[mem->mem_type];
	struct bochs_device *bochs = bochs_bdev(bdev);

	mem->bus.addr = NULL;
	mem->bus.offset = 0;
	mem->bus.size = mem->num_pages << PAGE_SHIFT;
	mem->bus.base = 0;
	mem->bus.is_iomem = false;
	if (!(man->flags & TTM_MEMTYPE_FLAG_MAPPABLE))
		return -EINVAL;
	switch (mem->mem_type) {
	case TTM_PL_SYSTEM:
		/* system memory */
		return 0;
	case TTM_PL_VRAM:
		mem->bus.offset = mem->start << PAGE_SHIFT;
		mem->bus.base = bochs->fb_base;
		mem->bus.is_iomem = true;
		break;
	default:
		return -EINVAL;
		break;
	}
	return 0;
}

static void bochs_ttm_io_mem_free(struct ttm_bo_device *bdev,
				  struct ttm_mem_reg *mem)
{
}

static void bochs_ttm_backend_destroy(struct ttm_tt *tt)
{
	ttm_tt_fini(tt);
	kfree(tt);
}

static struct ttm_backend_func bochs_tt_backend_func = {
	.destroy = &bochs_ttm_backend_destroy,
};

static struct ttm_tt *bochs_ttm_tt_create(struct ttm_buffer_object *bo,
					  uint32_t page_flags)
{
	struct ttm_tt *tt;

	tt = kzalloc(sizeof(struct ttm_tt), GFP_KERNEL);
	if (tt == NULL)
		return NULL;
	tt->func = &bochs_tt_backend_func;
	if (ttm_tt_init(tt, bo, page_flags)) {
		kfree(tt);
		return NULL;
	}
	return tt;
}

static struct ttm_bo_driver bochs_bo_driver = {
	.ttm_tt_create = bochs_ttm_tt_create,
	.init_mem_type = bochs_bo_init_mem_type,
	.eviction_valuable = ttm_bo_eviction_valuable,
	.evict_flags = bochs_bo_evict_flags,
	.move = NULL,
	.verify_access = bochs_bo_verify_access,
	.io_mem_reserve = &bochs_ttm_io_mem_reserve,
	.io_mem_free = &bochs_ttm_io_mem_free,
};

int bochs_mm_init(struct bochs_device *bochs)
{
	struct ttm_bo_device *bdev = &bochs->ttm.bdev;
	int ret;

	ret = ttm_bo_device_init(&bochs->ttm.bdev,
				 &bochs_bo_driver,
				 bochs->dev->anon_inode->i_mapping,
				 DRM_FILE_PAGE_OFFSET,
				 true);
	if (ret) {
		DRM_ERROR("Error initialising bo driver; %d\n", ret);
		return ret;
	}

	ret = ttm_bo_init_mm(bdev, TTM_PL_VRAM,
			     bochs->fb_size >> PAGE_SHIFT);
	if (ret) {
		DRM_ERROR("Failed ttm VRAM init: %d\n", ret);
		return ret;
	}

	bochs->ttm.initialized = true;
	return 0;
}

void bochs_mm_fini(struct bochs_device *bochs)
{
	if (!bochs->ttm.initialized)
		return;

	ttm_bo_device_release(&bochs->ttm.bdev);
	bochs->ttm.initialized = false;
}

static void bochs_ttm_placement(struct bochs_bo *bo, int domain)
{
	unsigned i;
	u32 c = 0;
	bo->placement.placement = bo->placements;
	bo->placement.busy_placement = bo->placements;
	if (domain & TTM_PL_FLAG_VRAM) {
		bo->placements[c++].flags = TTM_PL_FLAG_WC
			| TTM_PL_FLAG_UNCACHED
			| TTM_PL_FLAG_VRAM;
	}
	if (domain & TTM_PL_FLAG_SYSTEM) {
		bo->placements[c++].flags = TTM_PL_MASK_CACHING
			| TTM_PL_FLAG_SYSTEM;
	}
	if (!c) {
		bo->placements[c++].flags = TTM_PL_MASK_CACHING
			| TTM_PL_FLAG_SYSTEM;
	}
	for (i = 0; i < c; ++i) {
		bo->placements[i].fpfn = 0;
		bo->placements[i].lpfn = 0;
	}
	bo->placement.num_placement = c;
	bo->placement.num_busy_placement = c;
}

static inline u64 bochs_bo_gpu_offset(struct bochs_bo *bo)
{
	return bo->bo.offset;
}

int bochs_bo_pin(struct bochs_bo *bo, u32 pl_flag, u64 *gpu_addr)
{
	struct ttm_operation_ctx ctx = { false, false };
	int i, ret;

	if (bo->pin_count) {
		bo->pin_count++;
		if (gpu_addr)
			*gpu_addr = bochs_bo_gpu_offset(bo);
		return 0;
	}

	bochs_ttm_placement(bo, pl_flag);
	for (i = 0; i < bo->placement.num_placement; i++)
		bo->placements[i].flags |= TTM_PL_FLAG_NO_EVICT;
	ret = ttm_bo_validate(&bo->bo, &bo->placement, &ctx);
	if (ret)
		return ret;

	bo->pin_count = 1;
	if (gpu_addr)
		*gpu_addr = bochs_bo_gpu_offset(bo);
	return 0;
}

int bochs_bo_unpin(struct bochs_bo *bo)
{
	struct ttm_operation_ctx ctx = { false, false };
	int i, ret;

	if (!bo->pin_count) {
		DRM_ERROR("unpin bad %p\n", bo);
		return 0;
	}
	bo->pin_count--;

	if (bo->pin_count)
		return 0;

	for (i = 0; i < bo->placement.num_placement; i++)
		bo->placements[i].flags &= ~TTM_PL_FLAG_NO_EVICT;
	ret = ttm_bo_validate(&bo->bo, &bo->placement, &ctx);
	if (ret)
		return ret;

	return 0;
}

int bochs_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_file *file_priv;
	struct bochs_device *bochs;

	if (unlikely(vma->vm_pgoff < DRM_FILE_PAGE_OFFSET))
		return -EINVAL;

	file_priv = filp->private_data;
	bochs = file_priv->minor->dev->dev_private;
	return ttm_bo_mmap(filp, vma, &bochs->ttm.bdev);
}

/* ---------------------------------------------------------------------- */

static int bochs_bo_create(struct drm_device *dev, int size, int align,
			   uint32_t flags, struct bochs_bo **pbochsbo)
{
	struct bochs_device *bochs = dev->dev_private;
	struct bochs_bo *bochsbo;
	size_t acc_size;
	int ret;

	bochsbo = kzalloc(sizeof(struct bochs_bo), GFP_KERNEL);
	if (!bochsbo)
		return -ENOMEM;

	ret = drm_gem_object_init(dev, &bochsbo->gem, size);
	if (ret) {
		kfree(bochsbo);
		return ret;
	}

	bochsbo->bo.bdev = &bochs->ttm.bdev;
	bochsbo->bo.bdev->dev_mapping = dev->anon_inode->i_mapping;

	bochs_ttm_placement(bochsbo, TTM_PL_FLAG_VRAM | TTM_PL_FLAG_SYSTEM);

	acc_size = ttm_bo_dma_acc_size(&bochs->ttm.bdev, size,
				       sizeof(struct bochs_bo));

	ret = ttm_bo_init(&bochs->ttm.bdev, &bochsbo->bo, size,
			  ttm_bo_type_device, &bochsbo->placement,
			  align >> PAGE_SHIFT, false, acc_size,
			  NULL, NULL, bochs_bo_ttm_destroy);
	if (ret)
		return ret;

	*pbochsbo = bochsbo;
	return 0;
}

int bochs_gem_create(struct drm_device *dev, u32 size, bool iskernel,
		     struct drm_gem_object **obj)
{
	struct bochs_bo *bochsbo;
	int ret;

	*obj = NULL;

	size = PAGE_ALIGN(size);
	if (size == 0)
		return -EINVAL;

	ret = bochs_bo_create(dev, size, 0, 0, &bochsbo);
	if (ret) {
		if (ret != -ERESTARTSYS)
			DRM_ERROR("failed to allocate GEM object\n");
		return ret;
	}
	*obj = &bochsbo->gem;
	return 0;
}

int bochs_dumb_create(struct drm_file *file, struct drm_device *dev,
		      struct drm_mode_create_dumb *args)
{
	struct drm_gem_object *gobj;
	u32 handle;
	int ret;

	args->pitch = args->width * ((args->bpp + 7) / 8);
	args->size = args->pitch * args->height;

	ret = bochs_gem_create(dev, args->size, false,
			       &gobj);
	if (ret)
		return ret;

	ret = drm_gem_handle_create(file, gobj, &handle);
	drm_gem_object_put_unlocked(gobj);
	if (ret)
		return ret;

	args->handle = handle;
	return 0;
}

static void bochs_bo_unref(struct bochs_bo **bo)
{
	struct ttm_buffer_object *tbo;

	if ((*bo) == NULL)
		return;

	tbo = &((*bo)->bo);
	ttm_bo_put(tbo);
	*bo = NULL;
}

void bochs_gem_free_object(struct drm_gem_object *obj)
{
	struct bochs_bo *bochs_bo = gem_to_bochs_bo(obj);

	bochs_bo_unref(&bochs_bo);
}

int bochs_dumb_mmap_offset(struct drm_file *file, struct drm_device *dev,
			   uint32_t handle, uint64_t *offset)
{
	struct drm_gem_object *obj;
	struct bochs_bo *bo;

	obj = drm_gem_object_lookup(file, handle);
	if (obj == NULL)
		return -ENOENT;

	bo = gem_to_bochs_bo(obj);
	*offset = bochs_bo_mmap_offset(bo);

	drm_gem_object_put_unlocked(obj);
	return 0;
}
