/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "bochs.h"

/* ---------------------------------------------------------------------- */

static inline struct bochs_device *bochs_bdev(struct ttm_bo_device *bd)
{
	return container_of(bd, struct bochs_device, ttm.bdev);
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
	.evict_flags = drm_gem_vram_bo_driver_evict_flags,
	.move = NULL,
	.verify_access = drm_gem_vram_bo_driver_verify_access,
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

int bochs_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_file *file_priv = filp->private_data;
	struct bochs_device *bochs = file_priv->minor->dev->dev_private;

	return ttm_bo_mmap(filp, vma, &bochs->ttm.bdev);
}

/* ---------------------------------------------------------------------- */

int bochs_dumb_create(struct drm_file *file, struct drm_device *dev,
		      struct drm_mode_create_dumb *args)
{
	struct bochs_device *bochs = dev->dev_private;

	return drm_gem_vram_fill_create_dumb(file, dev, &bochs->ttm.bdev, 0,
					     false, args);
}
