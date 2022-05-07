// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */

#include <linux/module.h>
#include <linux/version.h>

#if KERNEL_VERSION(5, 5, 0) <= LINUX_VERSION_CODE
#include <drm/drm_fourcc.h>
#else
#include <drm/drmP.h>
#endif
#include <drm/drm_gem.h>
#include <drm/drm_crtc.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>

#include "vs_fb.h"
#include "vs_gem.h"

#define fourcc_mod_vs_get_type(val) \
	(((val) & DRM_FORMAT_MOD_VS_TYPE_MASK) >> 54)

static struct drm_framebuffer_funcs vs_fb_funcs = {
	.create_handle	= drm_gem_fb_create_handle,
	.destroy	= drm_gem_fb_destroy,
	.dirty		= drm_atomic_helper_dirtyfb,
};

static struct drm_framebuffer *
vs_fb_alloc(struct drm_device *dev, const struct drm_mode_fb_cmd2 *mode_cmd,
		 struct vs_gem_object **obj, unsigned int num_planes)
{
	struct drm_framebuffer *fb;
	int ret, i;

	fb = kzalloc(sizeof(*fb), GFP_KERNEL);
	if (!fb)
		return ERR_PTR(-ENOMEM);

	drm_helper_mode_fill_fb_struct(dev, fb, mode_cmd);

	for (i = 0; i < num_planes; i++)
		fb->obj[i] = &obj[i]->base;

	ret = drm_framebuffer_init(dev, fb, &vs_fb_funcs);
	if (ret) {
		dev_err(dev->dev, "Failed to initialize framebuffer: %d\n",
			ret);
		kfree(fb);
		return ERR_PTR(ret);
	}

	return fb;
}

static struct drm_framebuffer *vs_fb_create(struct drm_device *dev,
					  struct drm_file *file_priv,
					  const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_framebuffer *fb;
	const struct drm_format_info *info;
	struct vs_gem_object *objs[MAX_NUM_PLANES];
	struct drm_gem_object *obj;
	unsigned int height, size;
	unsigned char i, num_planes;
	int ret = 0;

	info = drm_get_format_info(dev, mode_cmd);
	if (!info)
		return ERR_PTR(-EINVAL);

	num_planes = info->num_planes;
	if (num_planes > MAX_NUM_PLANES)
		return ERR_PTR(-EINVAL);

	for (i = 0; i < num_planes; i++) {
		obj = drm_gem_object_lookup(file_priv, mode_cmd->handles[i]);
		if (!obj) {
			dev_err(dev->dev, "Failed to lookup GEM object.\n");
			ret = -ENXIO;
			goto err;
		}

		height = drm_format_info_plane_height(info,
							  mode_cmd->height, i);

		size = height * mode_cmd->pitches[i] + mode_cmd->offsets[i];

		if (obj->size < size) {
#if KERNEL_VERSION(5, 9, 0) <= LINUX_VERSION_CODE
			drm_gem_object_put(obj);
#else
			drm_gem_object_put_unlocked(obj);
#endif
			ret = -EINVAL;
			goto err;
		}

		objs[i] = to_vs_gem_object(obj);
	}

	fb = vs_fb_alloc(dev, mode_cmd, objs, i);
	if (IS_ERR(fb)) {
		ret = PTR_ERR(fb);
		goto err;
	}

	return fb;

err:
	for (; i > 0; i--)
#if KERNEL_VERSION(5, 9, 0) <= LINUX_VERSION_CODE
		drm_gem_object_put(&objs[i-1]->base);
#else
		drm_gem_object_put_unlocked(&objs[i-1]->base);
#endif

	return ERR_PTR(ret);
}

struct vs_gem_object *vs_fb_get_gem_obj(struct drm_framebuffer *fb,
					unsigned char plane)
{
	if (plane > MAX_NUM_PLANES)
		return NULL;

	return to_vs_gem_object(fb->obj[plane]);
}

static const struct drm_format_info vs_formats[] = {
	{.format = DRM_FORMAT_NV12, .depth = 0, .num_planes = 2, .char_per_block = { 20, 40, 0 },
	 .block_w = { 4, 4, 0 }, .block_h = { 4, 4, 0 }, .hsub = 2, .vsub = 2, .is_yuv = true},
	{.format = DRM_FORMAT_YUV444, .depth = 0, .num_planes = 3, .char_per_block = { 20, 20, 20 },
	 .block_w = { 4, 4, 4 }, .block_h = { 4, 4, 4 }, .hsub = 1, .vsub = 1, .is_yuv = true},
};

static const struct drm_format_info *
vs_lookup_format_info(const struct drm_format_info formats[],
				   int num_formats, u32 format)
{
	int i;

	for (i = 0; i < num_formats; i++) {
		if (formats[i].format == format)
			return &formats[i];
	}

	return NULL;
}

static const struct drm_format_info *
vs_get_format_info(const struct drm_mode_fb_cmd2 *cmd)
{
	if (fourcc_mod_vs_get_type(cmd->modifier[0]) ==
		DRM_FORMAT_MOD_VS_TYPE_CUSTOM_10BIT)
		return vs_lookup_format_info(vs_formats, ARRAY_SIZE(vs_formats),
									 cmd->pixel_format);
	else
		return NULL;
}

static const struct drm_mode_config_funcs vs_mode_config_funcs = {
	.fb_create			 = vs_fb_create,
	.get_format_info	 = vs_get_format_info,
	.output_poll_changed = drm_fb_helper_output_poll_changed,
	.atomic_check		 = drm_atomic_helper_check,
	.atomic_commit		 = drm_atomic_helper_commit,
};

static struct drm_mode_config_helper_funcs vs_mode_config_helpers = {
	.atomic_commit_tail = drm_atomic_helper_commit_tail_rpm,
};

void vs_mode_config_init(struct drm_device *dev)
{
	dev->mode_config.allow_fb_modifiers = true;

	if (dev->mode_config.max_width == 0 ||
		dev->mode_config.max_height == 0) {
		dev->mode_config.min_width	= 0;
		dev->mode_config.min_height = 0;
		dev->mode_config.max_width = 4096;
		dev->mode_config.max_height = 4096;
	}
	dev->mode_config.funcs = &vs_mode_config_funcs;
	dev->mode_config.helper_private = &vs_mode_config_helpers;
}
