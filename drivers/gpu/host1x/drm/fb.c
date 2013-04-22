/*
 * Copyright (C) 2012-2013 Avionic Design GmbH
 * Copyright (C) 2012 NVIDIA CORPORATION.  All rights reserved.
 *
 * Based on the KMS/FB CMA helpers
 *   Copyright (C) 2012 Analog Device Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>

#include "drm.h"
#include "gem.h"

static inline struct tegra_fb *to_tegra_fb(struct drm_framebuffer *fb)
{
	return container_of(fb, struct tegra_fb, base);
}

static inline struct tegra_fbdev *to_tegra_fbdev(struct drm_fb_helper *helper)
{
	return container_of(helper, struct tegra_fbdev, base);
}

struct tegra_bo *tegra_fb_get_plane(struct drm_framebuffer *framebuffer,
				    unsigned int index)
{
	struct tegra_fb *fb = to_tegra_fb(framebuffer);

	if (index >= drm_format_num_planes(framebuffer->pixel_format))
		return NULL;

	return fb->planes[index];
}

static void tegra_fb_destroy(struct drm_framebuffer *framebuffer)
{
	struct tegra_fb *fb = to_tegra_fb(framebuffer);
	unsigned int i;

	for (i = 0; i < fb->num_planes; i++) {
		struct tegra_bo *bo = fb->planes[i];

		if (bo)
			drm_gem_object_unreference_unlocked(&bo->gem);
	}

	drm_framebuffer_cleanup(framebuffer);
	kfree(fb->planes);
	kfree(fb);
}

static int tegra_fb_create_handle(struct drm_framebuffer *framebuffer,
				  struct drm_file *file, unsigned int *handle)
{
	struct tegra_fb *fb = to_tegra_fb(framebuffer);

	return drm_gem_handle_create(file, &fb->planes[0]->gem, handle);
}

static struct drm_framebuffer_funcs tegra_fb_funcs = {
	.destroy = tegra_fb_destroy,
	.create_handle = tegra_fb_create_handle,
};

static struct tegra_fb *tegra_fb_alloc(struct drm_device *drm,
				       struct drm_mode_fb_cmd2 *mode_cmd,
				       struct tegra_bo **planes,
				       unsigned int num_planes)
{
	struct tegra_fb *fb;
	unsigned int i;
	int err;

	fb = kzalloc(sizeof(*fb), GFP_KERNEL);
	if (!fb)
		return ERR_PTR(-ENOMEM);

	fb->planes = kzalloc(num_planes * sizeof(*planes), GFP_KERNEL);
	if (!fb->planes)
		return ERR_PTR(-ENOMEM);

	fb->num_planes = num_planes;

	drm_helper_mode_fill_fb_struct(&fb->base, mode_cmd);

	for (i = 0; i < fb->num_planes; i++)
		fb->planes[i] = planes[i];

	err = drm_framebuffer_init(drm, &fb->base, &tegra_fb_funcs);
	if (err < 0) {
		dev_err(drm->dev, "failed to initialize framebuffer: %d\n",
			err);
		kfree(fb->planes);
		kfree(fb);
		return ERR_PTR(err);
	}

	return fb;
}

static struct drm_framebuffer *tegra_fb_create(struct drm_device *drm,
					       struct drm_file *file,
					       struct drm_mode_fb_cmd2 *cmd)
{
	unsigned int hsub, vsub, i;
	struct tegra_bo *planes[4];
	struct drm_gem_object *gem;
	struct tegra_fb *fb;
	int err;

	hsub = drm_format_horz_chroma_subsampling(cmd->pixel_format);
	vsub = drm_format_vert_chroma_subsampling(cmd->pixel_format);

	for (i = 0; i < drm_format_num_planes(cmd->pixel_format); i++) {
		unsigned int width = cmd->width / (i ? hsub : 1);
		unsigned int height = cmd->height / (i ? vsub : 1);
		unsigned int size, bpp;

		gem = drm_gem_object_lookup(drm, file, cmd->handles[i]);
		if (!gem) {
			err = -ENXIO;
			goto unreference;
		}

		bpp = drm_format_plane_cpp(cmd->pixel_format, i);

		size = (height - 1) * cmd->pitches[i] +
		       width * bpp + cmd->offsets[i];

		if (gem->size < size) {
			err = -EINVAL;
			goto unreference;
		}

		planes[i] = to_tegra_bo(gem);
	}

	fb = tegra_fb_alloc(drm, cmd, planes, i);
	if (IS_ERR(fb)) {
		err = PTR_ERR(fb);
		goto unreference;
	}

	return &fb->base;

unreference:
	while (i--)
		drm_gem_object_unreference_unlocked(&planes[i]->gem);

	return ERR_PTR(err);
}

static struct fb_ops tegra_fb_ops = {
	.owner = THIS_MODULE,
	.fb_fillrect = sys_fillrect,
	.fb_copyarea = sys_copyarea,
	.fb_imageblit = sys_imageblit,
	.fb_check_var = drm_fb_helper_check_var,
	.fb_set_par = drm_fb_helper_set_par,
	.fb_blank = drm_fb_helper_blank,
	.fb_pan_display = drm_fb_helper_pan_display,
	.fb_setcmap = drm_fb_helper_setcmap,
};

static int tegra_fbdev_probe(struct drm_fb_helper *helper,
			     struct drm_fb_helper_surface_size *sizes)
{
	struct tegra_fbdev *fbdev = to_tegra_fbdev(helper);
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
	cmd.pitches[0] = sizes->surface_width * bytes_per_pixel;
	cmd.pixel_format = drm_mode_legacy_fb_format(sizes->surface_bpp,
						     sizes->surface_depth);

	size = cmd.pitches[0] * cmd.height;

	bo = tegra_bo_create(drm, size);
	if (IS_ERR(bo))
		return PTR_ERR(bo);

	info = framebuffer_alloc(0, drm->dev);
	if (!info) {
		dev_err(drm->dev, "failed to allocate framebuffer info\n");
		tegra_bo_free_object(&bo->gem);
		return -ENOMEM;
	}

	fbdev->fb = tegra_fb_alloc(drm, &cmd, &bo, 1);
	if (IS_ERR(fbdev->fb)) {
		dev_err(drm->dev, "failed to allocate DRM framebuffer\n");
		err = PTR_ERR(fbdev->fb);
		goto release;
	}

	fb = &fbdev->fb->base;
	helper->fb = fb;
	helper->fbdev = info;

	info->par = helper;
	info->flags = FBINFO_FLAG_DEFAULT;
	info->fbops = &tegra_fb_ops;

	err = fb_alloc_cmap(&info->cmap, 256, 0);
	if (err < 0) {
		dev_err(drm->dev, "failed to allocate color map: %d\n", err);
		goto destroy;
	}

	drm_fb_helper_fill_fix(info, fb->pitches[0], fb->depth);
	drm_fb_helper_fill_var(info, helper, fb->width, fb->height);

	offset = info->var.xoffset * bytes_per_pixel +
		 info->var.yoffset * fb->pitches[0];

	drm->mode_config.fb_base = (resource_size_t)bo->paddr;
	info->screen_base = bo->vaddr + offset;
	info->screen_size = size;
	info->fix.smem_start = (unsigned long)(bo->paddr + offset);
	info->fix.smem_len = size;

	return 0;

destroy:
	drm_framebuffer_unregister_private(fb);
	tegra_fb_destroy(fb);
release:
	framebuffer_release(info);
	return err;
}

static struct drm_fb_helper_funcs tegra_fb_helper_funcs = {
	.fb_probe = tegra_fbdev_probe,
};

static struct tegra_fbdev *tegra_fbdev_create(struct drm_device *drm,
					      unsigned int preferred_bpp,
					      unsigned int num_crtc,
					      unsigned int max_connectors)
{
	struct drm_fb_helper *helper;
	struct tegra_fbdev *fbdev;
	int err;

	fbdev = kzalloc(sizeof(*fbdev), GFP_KERNEL);
	if (!fbdev) {
		dev_err(drm->dev, "failed to allocate DRM fbdev\n");
		return ERR_PTR(-ENOMEM);
	}

	fbdev->base.funcs = &tegra_fb_helper_funcs;
	helper = &fbdev->base;

	err = drm_fb_helper_init(drm, &fbdev->base, num_crtc, max_connectors);
	if (err < 0) {
		dev_err(drm->dev, "failed to initialize DRM FB helper\n");
		goto free;
	}

	err = drm_fb_helper_single_add_all_connectors(&fbdev->base);
	if (err < 0) {
		dev_err(drm->dev, "failed to add connectors\n");
		goto fini;
	}

	drm_helper_disable_unused_functions(drm);

	err = drm_fb_helper_initial_config(&fbdev->base, preferred_bpp);
	if (err < 0) {
		dev_err(drm->dev, "failed to set initial configuration\n");
		goto fini;
	}

	return fbdev;

fini:
	drm_fb_helper_fini(&fbdev->base);
free:
	kfree(fbdev);
	return ERR_PTR(err);
}

static void tegra_fbdev_free(struct tegra_fbdev *fbdev)
{
	struct fb_info *info = fbdev->base.fbdev;

	if (info) {
		int err;

		err = unregister_framebuffer(info);
		if (err < 0)
			DRM_DEBUG_KMS("failed to unregister framebuffer\n");

		if (info->cmap.len)
			fb_dealloc_cmap(&info->cmap);

		framebuffer_release(info);
	}

	if (fbdev->fb) {
		drm_framebuffer_unregister_private(&fbdev->fb->base);
		tegra_fb_destroy(&fbdev->fb->base);
	}

	drm_fb_helper_fini(&fbdev->base);
	kfree(fbdev);
}

static void tegra_fb_output_poll_changed(struct drm_device *drm)
{
	struct host1x_drm *host1x = drm->dev_private;

	if (host1x->fbdev)
		drm_fb_helper_hotplug_event(&host1x->fbdev->base);
}

static const struct drm_mode_config_funcs tegra_drm_mode_funcs = {
	.fb_create = tegra_fb_create,
	.output_poll_changed = tegra_fb_output_poll_changed,
};

int tegra_drm_fb_init(struct drm_device *drm)
{
	struct host1x_drm *host1x = drm->dev_private;
	struct tegra_fbdev *fbdev;

	drm->mode_config.min_width = 0;
	drm->mode_config.min_height = 0;

	drm->mode_config.max_width = 4096;
	drm->mode_config.max_height = 4096;

	drm->mode_config.funcs = &tegra_drm_mode_funcs;

	fbdev = tegra_fbdev_create(drm, 32, drm->mode_config.num_crtc,
				   drm->mode_config.num_connector);
	if (IS_ERR(fbdev))
		return PTR_ERR(fbdev);

	host1x->fbdev = fbdev;

	return 0;
}

void tegra_drm_fb_exit(struct drm_device *drm)
{
	struct host1x_drm *host1x = drm->dev_private;

	tegra_fbdev_free(host1x->fbdev);
}

void tegra_fbdev_restore_mode(struct tegra_fbdev *fbdev)
{
	if (fbdev) {
		drm_modeset_lock_all(fbdev->base.dev);
		drm_fb_helper_restore_fbdev_mode(&fbdev->base);
		drm_modeset_unlock_all(fbdev->base.dev);
	}
}
