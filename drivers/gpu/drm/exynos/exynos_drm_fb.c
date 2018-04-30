/* exynos_drm_fb.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Authors:
 *	Inki Dae <inki.dae@samsung.com>
 *	Joonyoung Shim <jy0922.shim@samsung.com>
 *	Seung-Woo Kim <sw0312.kim@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <uapi/drm/exynos_drm.h>

#include "exynos_drm_drv.h"
#include "exynos_drm_fb.h"
#include "exynos_drm_fbdev.h"
#include "exynos_drm_iommu.h"
#include "exynos_drm_crtc.h"

static int check_fb_gem_memory_type(struct drm_device *drm_dev,
				    struct exynos_drm_gem *exynos_gem)
{
	unsigned int flags;

	/*
	 * if exynos drm driver supports iommu then framebuffer can use
	 * all the buffer types.
	 */
	if (is_drm_iommu_supported(drm_dev))
		return 0;

	flags = exynos_gem->flags;

	/*
	 * Physically non-contiguous memory type for framebuffer is not
	 * supported without IOMMU.
	 */
	if (IS_NONCONTIG_BUFFER(flags)) {
		DRM_ERROR("Non-contiguous GEM memory is not supported.\n");
		return -EINVAL;
	}

	return 0;
}

static const struct drm_framebuffer_funcs exynos_drm_fb_funcs = {
	.destroy	= drm_gem_fb_destroy,
	.create_handle	= drm_gem_fb_create_handle,
};

struct drm_framebuffer *
exynos_drm_framebuffer_init(struct drm_device *dev,
			    const struct drm_mode_fb_cmd2 *mode_cmd,
			    struct exynos_drm_gem **exynos_gem,
			    int count)
{
	struct drm_framebuffer *fb;
	int i;
	int ret;

	fb = kzalloc(sizeof(*fb), GFP_KERNEL);
	if (!fb)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < count; i++) {
		ret = check_fb_gem_memory_type(dev, exynos_gem[i]);
		if (ret < 0)
			goto err;

		fb->obj[i] = &exynos_gem[i]->base;
	}

	drm_helper_mode_fill_fb_struct(dev, fb, mode_cmd);

	ret = drm_framebuffer_init(dev, fb, &exynos_drm_fb_funcs);
	if (ret < 0) {
		DRM_ERROR("failed to initialize framebuffer\n");
		goto err;
	}

	return fb;

err:
	kfree(fb);
	return ERR_PTR(ret);
}

static struct drm_framebuffer *
exynos_user_fb_create(struct drm_device *dev, struct drm_file *file_priv,
		      const struct drm_mode_fb_cmd2 *mode_cmd)
{
	const struct drm_format_info *info = drm_get_format_info(dev, mode_cmd);
	struct exynos_drm_gem *exynos_gem[MAX_FB_BUFFER];
	struct drm_gem_object *obj;
	struct drm_framebuffer *fb;
	int i;
	int ret;

	for (i = 0; i < info->num_planes; i++) {
		unsigned int height = (i == 0) ? mode_cmd->height :
				     DIV_ROUND_UP(mode_cmd->height, info->vsub);
		unsigned long size = height * mode_cmd->pitches[i] +
				     mode_cmd->offsets[i];

		obj = drm_gem_object_lookup(file_priv, mode_cmd->handles[i]);
		if (!obj) {
			DRM_ERROR("failed to lookup gem object\n");
			ret = -ENOENT;
			goto err;
		}

		exynos_gem[i] = to_exynos_gem(obj);

		if (size > exynos_gem[i]->size) {
			i++;
			ret = -EINVAL;
			goto err;
		}
	}

	fb = exynos_drm_framebuffer_init(dev, mode_cmd, exynos_gem, i);
	if (IS_ERR(fb)) {
		ret = PTR_ERR(fb);
		goto err;
	}

	return fb;

err:
	while (i--)
		drm_gem_object_unreference_unlocked(&exynos_gem[i]->base);

	return ERR_PTR(ret);
}

dma_addr_t exynos_drm_fb_dma_addr(struct drm_framebuffer *fb, int index)
{
	struct exynos_drm_gem *exynos_gem;

	if (WARN_ON_ONCE(index >= MAX_FB_BUFFER))
		return 0;

	exynos_gem = to_exynos_gem(fb->obj[index]);
	return exynos_gem->dma_addr + fb->offsets[index];
}

static struct drm_mode_config_helper_funcs exynos_drm_mode_config_helpers = {
	.atomic_commit_tail = drm_atomic_helper_commit_tail_rpm,
};

static const struct drm_mode_config_funcs exynos_drm_mode_config_funcs = {
	.fb_create = exynos_user_fb_create,
	.output_poll_changed = drm_fb_helper_output_poll_changed,
	.atomic_check = exynos_atomic_check,
	.atomic_commit = drm_atomic_helper_commit,
};

void exynos_drm_mode_config_init(struct drm_device *dev)
{
	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;

	/*
	 * set max width and height as default value(4096x4096).
	 * this value would be used to check framebuffer size limitation
	 * at drm_mode_addfb().
	 */
	dev->mode_config.max_width = 4096;
	dev->mode_config.max_height = 4096;

	dev->mode_config.funcs = &exynos_drm_mode_config_funcs;
	dev->mode_config.helper_private = &exynos_drm_mode_config_helpers;

	dev->mode_config.allow_fb_modifiers = true;
}
