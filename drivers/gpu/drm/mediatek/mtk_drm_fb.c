// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015 MediaTek Inc.
 */

#include <linux/dma-buf.h>
#include <linux/reservation.h>

#include <drm/drm_modeset_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_framebuffer_helper.h>

#include "mtk_drm_drv.h"
#include "mtk_drm_fb.h"
#include "mtk_drm_gem.h"

static const struct drm_framebuffer_funcs mtk_drm_fb_funcs = {
	.create_handle = drm_gem_fb_create_handle,
	.destroy = drm_gem_fb_destroy,
};

static struct drm_framebuffer *mtk_drm_framebuffer_init(struct drm_device *dev,
					const struct drm_mode_fb_cmd2 *mode,
					struct drm_gem_object *obj)
{
	const struct drm_format_info *info = drm_get_format_info(dev, mode);
	struct drm_framebuffer *fb;
	int ret;

	if (info->num_planes != 1)
		return ERR_PTR(-EINVAL);

	fb = kzalloc(sizeof(*fb), GFP_KERNEL);
	if (!fb)
		return ERR_PTR(-ENOMEM);

	drm_helper_mode_fill_fb_struct(dev, fb, mode);

	fb->obj[0] = obj;

	ret = drm_framebuffer_init(dev, fb, &mtk_drm_fb_funcs);
	if (ret) {
		DRM_ERROR("failed to initialize framebuffer\n");
		kfree(fb);
		return ERR_PTR(ret);
	}

	return fb;
}

struct drm_framebuffer *mtk_drm_mode_fb_create(struct drm_device *dev,
					       struct drm_file *file,
					       const struct drm_mode_fb_cmd2 *cmd)
{
	const struct drm_format_info *info = drm_get_format_info(dev, cmd);
	struct drm_framebuffer *fb;
	struct drm_gem_object *gem;
	unsigned int width = cmd->width;
	unsigned int height = cmd->height;
	unsigned int size, bpp;
	int ret;

	if (info->num_planes != 1)
		return ERR_PTR(-EINVAL);

	gem = drm_gem_object_lookup(file, cmd->handles[0]);
	if (!gem)
		return ERR_PTR(-ENOENT);

	bpp = info->cpp[0];
	size = (height - 1) * cmd->pitches[0] + width * bpp;
	size += cmd->offsets[0];

	if (gem->size < size) {
		ret = -EINVAL;
		goto unreference;
	}

	fb = mtk_drm_framebuffer_init(dev, cmd, gem);
	if (IS_ERR(fb)) {
		ret = PTR_ERR(fb);
		goto unreference;
	}

	return fb;

unreference:
	drm_gem_object_put_unlocked(gem);
	return ERR_PTR(ret);
}
