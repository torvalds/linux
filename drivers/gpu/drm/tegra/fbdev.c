// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012-2013 Avionic Design GmbH
 * Copyright (C) 2012 NVIDIA CORPORATION.  All rights reserved.
 *
 * Based on the KMS/FB DMA helpers
 *   Copyright (C) 2012 Analog Devices Inc.
 */

#include <linux/console.h>
#include <linux/vmalloc.h>

#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_modeset_helper.h>

#include "drm.h"
#include "gem.h"

static int tegra_fb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct drm_fb_helper *helper = info->par;
	struct tegra_bo *bo;
	int err;

	bo = tegra_fb_get_plane(helper->fb, 0);

	err = drm_gem_mmap_obj(&bo->gem, bo->gem.size, vma);
	if (err < 0)
		return err;

	return __tegra_gem_mmap(&bo->gem, vma);
}

static const struct fb_ops tegra_fb_ops = {
	.owner = THIS_MODULE,
	DRM_FB_HELPER_DEFAULT_OPS,
	.fb_read = drm_fb_helper_sys_read,
	.fb_write = drm_fb_helper_sys_write,
	.fb_fillrect = drm_fb_helper_sys_fillrect,
	.fb_copyarea = drm_fb_helper_sys_copyarea,
	.fb_imageblit = drm_fb_helper_sys_imageblit,
	.fb_mmap = tegra_fb_mmap,
};

static int tegra_fbdev_probe(struct drm_fb_helper *helper,
			     struct drm_fb_helper_surface_size *sizes)
{
	struct tegra_drm *tegra = helper->dev->dev_private;
	struct drm_device *drm = helper->dev;
	struct drm_mode_fb_cmd2 cmd = { 0 };
	unsigned int bytes_per_pixel;
	struct drm_framebuffer *fb;
	unsigned long offset;
	struct fb_info *info;
	struct tegra_bo *bo;
	size_t size;
	int err;

	bytes_per_pixel = DIV_ROUND_UP(sizes->surface_bpp, 8);

	cmd.width = sizes->surface_width;
	cmd.height = sizes->surface_height;
	cmd.pitches[0] = round_up(sizes->surface_width * bytes_per_pixel,
				  tegra->pitch_align);

	cmd.pixel_format = drm_mode_legacy_fb_format(sizes->surface_bpp,
						     sizes->surface_depth);

	size = cmd.pitches[0] * cmd.height;

	bo = tegra_bo_create(drm, size, 0);
	if (IS_ERR(bo))
		return PTR_ERR(bo);

	info = drm_fb_helper_alloc_info(helper);
	if (IS_ERR(info)) {
		dev_err(drm->dev, "failed to allocate framebuffer info\n");
		drm_gem_object_put(&bo->gem);
		return PTR_ERR(info);
	}

	fb = tegra_fb_alloc(drm, &cmd, &bo, 1);
	if (IS_ERR(fb)) {
		err = PTR_ERR(fb);
		dev_err(drm->dev, "failed to allocate DRM framebuffer: %d\n",
			err);
		drm_gem_object_put(&bo->gem);
		return PTR_ERR(fb);
	}

	helper->fb = fb;
	helper->info = info;

	info->fbops = &tegra_fb_ops;

	drm_fb_helper_fill_info(info, helper, sizes);

	offset = info->var.xoffset * bytes_per_pixel +
		 info->var.yoffset * fb->pitches[0];

	if (bo->pages) {
		bo->vaddr = vmap(bo->pages, bo->num_pages, VM_MAP,
				 pgprot_writecombine(PAGE_KERNEL));
		if (!bo->vaddr) {
			dev_err(drm->dev, "failed to vmap() framebuffer\n");
			err = -ENOMEM;
			goto destroy;
		}
	}

	info->screen_base = (void __iomem *)bo->vaddr + offset;
	info->screen_size = size;
	info->fix.smem_start = (unsigned long)(bo->iova + offset);
	info->fix.smem_len = size;

	return 0;

destroy:
	drm_framebuffer_remove(fb);
	return err;
}

static const struct drm_fb_helper_funcs tegra_fb_helper_funcs = {
	.fb_probe = tegra_fbdev_probe,
};

static struct drm_fb_helper *tegra_fbdev_create(struct drm_device *drm)
{
	struct drm_fb_helper *helper;

	helper = kzalloc(sizeof(*helper), GFP_KERNEL);
	if (!helper)
		return ERR_PTR(-ENOMEM);

	drm_fb_helper_prepare(drm, helper, 32, &tegra_fb_helper_funcs);

	return helper;
}

static void tegra_fbdev_free(struct drm_fb_helper *helper)
{
	drm_fb_helper_unprepare(helper);
	kfree(helper);
}

static int tegra_fbdev_init(struct drm_fb_helper *helper,
			    unsigned int num_crtc,
			    unsigned int max_connectors)
{
	struct drm_device *drm = helper->dev;
	int err;

	err = drm_fb_helper_init(drm, helper);
	if (err < 0) {
		dev_err(drm->dev, "failed to initialize DRM FB helper: %d\n",
			err);
		return err;
	}

	err = drm_fb_helper_initial_config(helper);
	if (err < 0) {
		dev_err(drm->dev, "failed to set initial configuration: %d\n",
			err);
		goto fini;
	}

	return 0;

fini:
	drm_fb_helper_fini(helper);
	return err;
}

static void tegra_fbdev_exit(struct drm_fb_helper *helper)
{
	struct drm_framebuffer *fb = helper->fb;

	drm_fb_helper_unregister_info(helper);

	if (fb) {
		struct tegra_bo *bo = tegra_fb_get_plane(fb, 0);

		/* Undo the special mapping we made in fbdev probe. */
		if (bo && bo->pages) {
			vunmap(bo->vaddr);
			bo->vaddr = NULL;
		}

		drm_framebuffer_remove(fb);
	}

	drm_fb_helper_fini(helper);
	tegra_fbdev_free(helper);
}

int tegra_drm_fb_prepare(struct drm_device *drm)
{
	drm->fb_helper = tegra_fbdev_create(drm);
	if (IS_ERR(drm->fb_helper))
		return PTR_ERR(drm->fb_helper);

	return 0;
}

void tegra_drm_fb_free(struct drm_device *drm)
{
	tegra_fbdev_free(drm->fb_helper);
}

int tegra_drm_fb_init(struct drm_device *drm)
{
	int err;

	err = tegra_fbdev_init(drm->fb_helper, drm->mode_config.num_crtc,
			       drm->mode_config.num_connector);
	if (err < 0)
		return err;

	return 0;
}

void tegra_drm_fb_exit(struct drm_device *drm)
{
	tegra_fbdev_exit(drm->fb_helper);
}
