// SPDX-License-Identifier: GPL-2.0-or-later
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
 */

#include <linux/pci.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_vram_helper.h>
#include <drm/drm_print.h>

#include "hibmc_drm_drv.h"

int hibmc_mm_init(struct hibmc_drm_private *hibmc)
{
	struct drm_vram_mm *vmm;
	int ret;
	struct drm_device *dev = hibmc->dev;

	vmm = drm_vram_helper_alloc_mm(dev,
				       pci_resource_start(dev->pdev, 0),
				       hibmc->fb_size);
	if (IS_ERR(vmm)) {
		ret = PTR_ERR(vmm);
		DRM_ERROR("Error initializing VRAM MM; %d\n", ret);
		return ret;
	}

	return 0;
}

void hibmc_mm_fini(struct hibmc_drm_private *hibmc)
{
	if (!hibmc->dev->vram_mm)
		return;

	drm_vram_helper_release_mm(hibmc->dev);
}

int hibmc_gem_create(struct drm_device *dev, u32 size, bool iskernel,
		     struct drm_gem_object **obj)
{
	struct drm_gem_vram_object *gbo;
	int ret;

	*obj = NULL;

	size = roundup(size, PAGE_SIZE);
	if (size == 0)
		return -EINVAL;

	gbo = drm_gem_vram_create(dev, &dev->vram_mm->bdev, size, 0, false);
	if (IS_ERR(gbo)) {
		ret = PTR_ERR(gbo);
		if (ret != -ERESTARTSYS)
			DRM_ERROR("failed to allocate GEM object: %d\n", ret);
		return ret;
	}
	*obj = &gbo->bo.base;
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

const struct drm_mode_config_funcs hibmc_mode_funcs = {
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
	.fb_create = drm_gem_fb_create,
};
