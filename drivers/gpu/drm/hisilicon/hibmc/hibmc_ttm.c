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
