/* Hisilicon Hibmc SoC drm driver
 *
 * Based on the bochs drm driver.
 *
 * Copyright (c) 2016 Huawei Limited.
 *
 * Author:
 *	Rongrong Zou <zourongrong@huawei.com>
 *	Rongrong Zou <zourongrong@gmail.com>
 *	Jianhua Li <lijianhua@huawei.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <drm/drm_atomic_helper.h>
#include <drm/ttm/ttm_page_alloc.h>

#include "hibmc_drm_drv.h"

static inline struct hibmc_drm_private *
hibmc_bdev(struct ttm_bo_device *bd)
{
	return container_of(bd, struct hibmc_drm_private, bdev);
}

static int
hibmc_bo_init_mem_type(struct ttm_bo_device *bdev, u32 type,
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
		DRM_ERROR("unsupported memory type %u\n", type);
		return -EINVAL;
	}
	return 0;
}

static int hibmc_ttm_io_mem_reserve(struct ttm_bo_device *bdev,
				    struct ttm_mem_reg *mem)
{
	struct ttm_mem_type_manager *man = &bdev->man[mem->mem_type];
	struct hibmc_drm_private *hibmc = hibmc_bdev(bdev);

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
		mem->bus.base = pci_resource_start(hibmc->dev->pdev, 0);
		mem->bus.is_iomem = true;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static void hibmc_ttm_backend_destroy(struct ttm_tt *tt)
{
	ttm_tt_fini(tt);
	kfree(tt);
}

static struct ttm_backend_func hibmc_tt_backend_func = {
	.destroy = &hibmc_ttm_backend_destroy,
};

static struct ttm_tt *hibmc_ttm_tt_create(struct ttm_buffer_object *bo,
					  u32 page_flags)
{
	struct ttm_tt *tt;
	int ret;

	tt = kzalloc(sizeof(*tt), GFP_KERNEL);
	if (!tt) {
		DRM_ERROR("failed to allocate ttm_tt\n");
		return NULL;
	}
	tt->func = &hibmc_tt_backend_func;
	ret = ttm_tt_init(tt, bo, page_flags);
	if (ret) {
		DRM_ERROR("failed to initialize ttm_tt: %d\n", ret);
		kfree(tt);
		return NULL;
	}
	return tt;
}

struct ttm_bo_driver hibmc_bo_driver = {
	.ttm_tt_create		= hibmc_ttm_tt_create,
	.init_mem_type		= hibmc_bo_init_mem_type,
	.evict_flags		= drm_gem_vram_bo_driver_evict_flags,
	.move			= NULL,
	.verify_access		= drm_gem_vram_bo_driver_verify_access,
	.io_mem_reserve		= &hibmc_ttm_io_mem_reserve,
	.io_mem_free		= NULL,
};

int hibmc_mm_init(struct hibmc_drm_private *hibmc)
{
	int ret;
	struct drm_device *dev = hibmc->dev;
	struct ttm_bo_device *bdev = &hibmc->bdev;

	ret = ttm_bo_device_init(&hibmc->bdev,
				 &hibmc_bo_driver,
				 dev->anon_inode->i_mapping,
				 true);
	if (ret) {
		DRM_ERROR("error initializing bo driver: %d\n", ret);
		return ret;
	}

	ret = ttm_bo_init_mm(bdev, TTM_PL_VRAM,
			     hibmc->fb_size >> PAGE_SHIFT);
	if (ret) {
		DRM_ERROR("failed ttm VRAM init: %d\n", ret);
		return ret;
	}

	hibmc->mm_inited = true;
	return 0;
}

void hibmc_mm_fini(struct hibmc_drm_private *hibmc)
{
	if (!hibmc->mm_inited)
		return;

	ttm_bo_device_release(&hibmc->bdev);
	hibmc->mm_inited = false;
}

int hibmc_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_file *file_priv = filp->private_data;
	struct hibmc_drm_private *hibmc = file_priv->minor->dev->dev_private;

	return ttm_bo_mmap(filp, vma, &hibmc->bdev);
}

int hibmc_gem_create(struct drm_device *dev, u32 size, bool iskernel,
		     struct drm_gem_object **obj)
{
	struct hibmc_drm_private *hibmc = dev->dev_private;
	struct drm_gem_vram_object *gbo;
	int ret;

	*obj = NULL;

	size = roundup(size, PAGE_SIZE);
	if (size == 0)
		return -EINVAL;

	gbo = drm_gem_vram_create(dev, &hibmc->bdev, size, 0, false);
	if (IS_ERR(gbo)) {
		ret = PTR_ERR(gbo);
		if (ret != -ERESTARTSYS)
			DRM_ERROR("failed to allocate GEM object: %d\n", ret);
		return ret;
	}
	*obj = &gbo->gem;
	return 0;
}

int hibmc_dumb_create(struct drm_file *file, struct drm_device *dev,
		      struct drm_mode_create_dumb *args)
{
	struct drm_gem_object *gobj;
	u32 handle;
	int ret;

	args->pitch = ALIGN(args->width * DIV_ROUND_UP(args->bpp, 8), 16);
	args->size = args->pitch * args->height;

	ret = hibmc_gem_create(dev, args->size, false,
			       &gobj);
	if (ret) {
		DRM_ERROR("failed to create GEM object: %d\n", ret);
		return ret;
	}

	ret = drm_gem_handle_create(file, gobj, &handle);
	drm_gem_object_put_unlocked(gobj);
	if (ret) {
		DRM_ERROR("failed to unreference GEM object: %d\n", ret);
		return ret;
	}

	args->handle = handle;
	return 0;
}

static void hibmc_user_framebuffer_destroy(struct drm_framebuffer *fb)
{
	struct hibmc_framebuffer *hibmc_fb = to_hibmc_framebuffer(fb);

	drm_gem_object_put_unlocked(hibmc_fb->obj);
	drm_framebuffer_cleanup(fb);
	kfree(hibmc_fb);
}

static const struct drm_framebuffer_funcs hibmc_fb_funcs = {
	.destroy = hibmc_user_framebuffer_destroy,
};

struct hibmc_framebuffer *
hibmc_framebuffer_init(struct drm_device *dev,
		       const struct drm_mode_fb_cmd2 *mode_cmd,
		       struct drm_gem_object *obj)
{
	struct hibmc_framebuffer *hibmc_fb;
	int ret;

	hibmc_fb = kzalloc(sizeof(*hibmc_fb), GFP_KERNEL);
	if (!hibmc_fb) {
		DRM_ERROR("failed to allocate hibmc_fb\n");
		return ERR_PTR(-ENOMEM);
	}

	drm_helper_mode_fill_fb_struct(dev, &hibmc_fb->fb, mode_cmd);
	hibmc_fb->obj = obj;
	ret = drm_framebuffer_init(dev, &hibmc_fb->fb, &hibmc_fb_funcs);
	if (ret) {
		DRM_ERROR("drm_framebuffer_init failed: %d\n", ret);
		kfree(hibmc_fb);
		return ERR_PTR(ret);
	}

	return hibmc_fb;
}

static struct drm_framebuffer *
hibmc_user_framebuffer_create(struct drm_device *dev,
			      struct drm_file *filp,
			      const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_gem_object *obj;
	struct hibmc_framebuffer *hibmc_fb;

	DRM_DEBUG_DRIVER("%dx%d, format %c%c%c%c\n",
			 mode_cmd->width, mode_cmd->height,
			 (mode_cmd->pixel_format) & 0xff,
			 (mode_cmd->pixel_format >> 8)  & 0xff,
			 (mode_cmd->pixel_format >> 16) & 0xff,
			 (mode_cmd->pixel_format >> 24) & 0xff);

	obj = drm_gem_object_lookup(filp, mode_cmd->handles[0]);
	if (!obj)
		return ERR_PTR(-ENOENT);

	hibmc_fb = hibmc_framebuffer_init(dev, mode_cmd, obj);
	if (IS_ERR(hibmc_fb)) {
		drm_gem_object_put_unlocked(obj);
		return ERR_PTR((long)hibmc_fb);
	}
	return &hibmc_fb->fb;
}

const struct drm_mode_config_funcs hibmc_mode_funcs = {
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
	.fb_create = hibmc_user_framebuffer_create,
};
