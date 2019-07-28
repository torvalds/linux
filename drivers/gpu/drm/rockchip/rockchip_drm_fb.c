// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:Mark Yao <mark.yao@rock-chips.com>
 */

#include <linux/kernel.h>
#include <drm/drm.h>
#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_probe_helper.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_fb.h"
#include "rockchip_drm_gem.h"
#include "rockchip_drm_psr.h"

static const struct drm_framebuffer_funcs rockchip_drm_fb_funcs = {
	.destroy       = drm_gem_fb_destroy,
	.create_handle = drm_gem_fb_create_handle,
	.dirty	       = drm_atomic_helper_dirtyfb,
};

static struct drm_framebuffer *
rockchip_fb_alloc(struct drm_device *dev, const struct drm_mode_fb_cmd2 *mode_cmd,
		  struct drm_gem_object **obj, unsigned int num_planes)
{
	struct drm_framebuffer *fb;
	int ret;
	int i;

	fb = kzalloc(sizeof(*fb), GFP_KERNEL);
	if (!fb)
		return ERR_PTR(-ENOMEM);

	drm_helper_mode_fill_fb_struct(dev, fb, mode_cmd);

	for (i = 0; i < num_planes; i++)
		fb->obj[i] = obj[i];

	ret = drm_framebuffer_init(dev, fb, &rockchip_drm_fb_funcs);
	if (ret) {
		DRM_DEV_ERROR(dev->dev,
			      "Failed to initialize framebuffer: %d\n",
			      ret);
		kfree(fb);
		return ERR_PTR(ret);
	}

	return fb;
}

static struct drm_framebuffer *
rockchip_user_fb_create(struct drm_device *dev, struct drm_file *file_priv,
			const struct drm_mode_fb_cmd2 *mode_cmd)
{
	const struct drm_format_info *info = drm_get_format_info(dev,
								 mode_cmd);
	struct drm_framebuffer *fb;
	struct drm_gem_object *objs[ROCKCHIP_MAX_FB_BUFFER];
	struct drm_gem_object *obj;
	int num_planes = min_t(int, info->num_planes, ROCKCHIP_MAX_FB_BUFFER);
	int ret;
	int i;

	for (i = 0; i < num_planes; i++) {
		unsigned int width = mode_cmd->width / (i ? info->hsub : 1);
		unsigned int height = mode_cmd->height / (i ? info->vsub : 1);
		unsigned int min_size;

		obj = drm_gem_object_lookup(file_priv, mode_cmd->handles[i]);
		if (!obj) {
			DRM_DEV_ERROR(dev->dev,
				      "Failed to lookup GEM object\n");
			ret = -ENXIO;
			goto err_gem_object_unreference;
		}

		min_size = (height - 1) * mode_cmd->pitches[i] +
			mode_cmd->offsets[i] +
			width * info->cpp[i];

		if (obj->size < min_size) {
			drm_gem_object_put_unlocked(obj);
			ret = -EINVAL;
			goto err_gem_object_unreference;
		}
		objs[i] = obj;
	}

	fb = rockchip_fb_alloc(dev, mode_cmd, objs, i);
	if (IS_ERR(fb)) {
		ret = PTR_ERR(fb);
		goto err_gem_object_unreference;
	}

	return fb;

err_gem_object_unreference:
	for (i--; i >= 0; i--)
		drm_gem_object_put_unlocked(objs[i]);
	return ERR_PTR(ret);
}

static void
rockchip_atomic_helper_commit_tail_rpm(struct drm_atomic_state *old_state)
{
	struct drm_device *dev = old_state->dev;

	rockchip_drm_psr_inhibit_get_state(old_state);

	drm_atomic_helper_commit_modeset_disables(dev, old_state);

	drm_atomic_helper_commit_modeset_enables(dev, old_state);

	drm_atomic_helper_commit_planes(dev, old_state,
					DRM_PLANE_COMMIT_ACTIVE_ONLY);

	rockchip_drm_psr_inhibit_put_state(old_state);

	drm_atomic_helper_commit_hw_done(old_state);

	drm_atomic_helper_wait_for_vblanks(dev, old_state);

	drm_atomic_helper_cleanup_planes(dev, old_state);
}

static const struct drm_mode_config_helper_funcs rockchip_mode_config_helpers = {
	.atomic_commit_tail = rockchip_atomic_helper_commit_tail_rpm,
};

static const struct drm_mode_config_funcs rockchip_drm_mode_config_funcs = {
	.fb_create = rockchip_user_fb_create,
	.output_poll_changed = drm_fb_helper_output_poll_changed,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

struct drm_framebuffer *
rockchip_drm_framebuffer_init(struct drm_device *dev,
			      const struct drm_mode_fb_cmd2 *mode_cmd,
			      struct drm_gem_object *obj)
{
	struct drm_framebuffer *fb;

	fb = rockchip_fb_alloc(dev, mode_cmd, &obj, 1);
	if (IS_ERR(fb))
		return ERR_CAST(fb);

	return fb;
}

void rockchip_drm_mode_config_init(struct drm_device *dev)
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

	dev->mode_config.funcs = &rockchip_drm_mode_config_funcs;
	dev->mode_config.helper_private = &rockchip_mode_config_helpers;
}
