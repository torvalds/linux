// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012-2013 Avionic Design GmbH
 * Copyright (C) 2012 NVIDIA CORPORATION.  All rights reserved.
 *
 * Based on the KMS/FB DMA helpers
 *   Copyright (C) 2012 Analog Devices Inc.
 */

#include <linux/console.h>

#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_modeset_helper.h>

#include "drm.h"
#include "gem.h"

struct tegra_bo *tegra_fb_get_plane(struct drm_framebuffer *framebuffer,
				    unsigned int index)
{
	return to_tegra_bo(drm_gem_fb_get_obj(framebuffer, index));
}

bool tegra_fb_is_bottom_up(struct drm_framebuffer *framebuffer)
{
	struct tegra_bo *bo = tegra_fb_get_plane(framebuffer, 0);

	if (bo->flags & TEGRA_BO_BOTTOM_UP)
		return true;

	return false;
}

int tegra_fb_get_tiling(struct drm_framebuffer *framebuffer,
			struct tegra_bo_tiling *tiling)
{
	uint64_t modifier = framebuffer->modifier;

	if (fourcc_mod_is_vendor(modifier, NVIDIA)) {
		if ((modifier & DRM_FORMAT_MOD_NVIDIA_SECTOR_LAYOUT) == 0)
			tiling->sector_layout = TEGRA_BO_SECTOR_LAYOUT_TEGRA;
		else
			tiling->sector_layout = TEGRA_BO_SECTOR_LAYOUT_GPU;

		modifier &= ~DRM_FORMAT_MOD_NVIDIA_SECTOR_LAYOUT;
	}

	switch (modifier) {
	case DRM_FORMAT_MOD_LINEAR:
		tiling->mode = TEGRA_BO_TILING_MODE_PITCH;
		tiling->value = 0;
		break;

	case DRM_FORMAT_MOD_NVIDIA_TEGRA_TILED:
		tiling->mode = TEGRA_BO_TILING_MODE_TILED;
		tiling->value = 0;
		break;

	case DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(0):
		tiling->mode = TEGRA_BO_TILING_MODE_BLOCK;
		tiling->value = 0;
		break;

	case DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(1):
		tiling->mode = TEGRA_BO_TILING_MODE_BLOCK;
		tiling->value = 1;
		break;

	case DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(2):
		tiling->mode = TEGRA_BO_TILING_MODE_BLOCK;
		tiling->value = 2;
		break;

	case DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(3):
		tiling->mode = TEGRA_BO_TILING_MODE_BLOCK;
		tiling->value = 3;
		break;

	case DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(4):
		tiling->mode = TEGRA_BO_TILING_MODE_BLOCK;
		tiling->value = 4;
		break;

	case DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(5):
		tiling->mode = TEGRA_BO_TILING_MODE_BLOCK;
		tiling->value = 5;
		break;

	default:
		DRM_DEBUG_KMS("unknown format modifier: %llx\n", modifier);
		return -EINVAL;
	}

	return 0;
}

static const struct drm_framebuffer_funcs tegra_fb_funcs = {
	.destroy = drm_gem_fb_destroy,
	.create_handle = drm_gem_fb_create_handle,
};

struct drm_framebuffer *tegra_fb_alloc(struct drm_device *drm,
				       const struct drm_format_info *info,
				       const struct drm_mode_fb_cmd2 *mode_cmd,
				       struct tegra_bo **planes,
				       unsigned int num_planes)
{
	struct drm_framebuffer *fb;
	unsigned int i;
	int err;

	fb = kzalloc(sizeof(*fb), GFP_KERNEL);
	if (!fb)
		return ERR_PTR(-ENOMEM);

	drm_helper_mode_fill_fb_struct(drm, fb, info, mode_cmd);

	for (i = 0; i < fb->format->num_planes; i++)
		fb->obj[i] = &planes[i]->gem;

	err = drm_framebuffer_init(drm, fb, &tegra_fb_funcs);
	if (err < 0) {
		dev_err(drm->dev, "failed to initialize framebuffer: %d\n",
			err);
		kfree(fb);
		return ERR_PTR(err);
	}

	return fb;
}

struct drm_framebuffer *tegra_fb_create(struct drm_device *drm,
					struct drm_file *file,
					const struct drm_format_info *info,
					const struct drm_mode_fb_cmd2 *cmd)
{
	struct tegra_bo *planes[4];
	struct drm_gem_object *gem;
	struct drm_framebuffer *fb;
	unsigned int i;
	int err;

	for (i = 0; i < info->num_planes; i++) {
		unsigned int width = cmd->width / (i ? info->hsub : 1);
		unsigned int height = cmd->height / (i ? info->vsub : 1);
		unsigned int size, bpp;

		gem = drm_gem_object_lookup(file, cmd->handles[i]);
		if (!gem) {
			err = -ENXIO;
			goto unreference;
		}

		bpp = info->cpp[i];

		size = (height - 1) * cmd->pitches[i] +
		       width * bpp + cmd->offsets[i];

		if (gem->size < size) {
			err = -EINVAL;
			drm_gem_object_put(gem);
			goto unreference;
		}

		planes[i] = to_tegra_bo(gem);
	}

	fb = tegra_fb_alloc(drm, info, cmd, planes, i);
	if (IS_ERR(fb)) {
		err = PTR_ERR(fb);
		goto unreference;
	}

	return fb;

unreference:
	while (i--)
		drm_gem_object_put(&planes[i]->gem);

	return ERR_PTR(err);
}
