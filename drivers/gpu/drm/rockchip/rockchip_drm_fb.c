/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:Mark Yao <mark.yao@rock-chips.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <drm/drm.h>
#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_crtc_helper.h>
#include <linux/memblock.h>
#include <linux/iommu.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_gem.h"

#define to_rockchip_fb(x) container_of(x, struct rockchip_drm_fb, fb)

struct rockchip_drm_fb {
	struct drm_framebuffer fb;
	dma_addr_t dma_addr[ROCKCHIP_MAX_FB_BUFFER];
	struct drm_gem_object *obj[ROCKCHIP_MAX_FB_BUFFER];
	struct rockchip_logo *logo;
};

dma_addr_t rockchip_fb_get_dma_addr(struct drm_framebuffer *fb,
				    unsigned int plane)
{
	struct rockchip_drm_fb *rk_fb = to_rockchip_fb(fb);

	if (WARN_ON(plane >= ROCKCHIP_MAX_FB_BUFFER))
		return 0;

	return rk_fb->dma_addr[plane];
}

static void rockchip_drm_fb_destroy(struct drm_framebuffer *fb)
{
	struct rockchip_drm_fb *rockchip_fb = to_rockchip_fb(fb);
	struct drm_gem_object *obj;
	int i;

	for (i = 0; i < ROCKCHIP_MAX_FB_BUFFER; i++) {
		obj = rockchip_fb->obj[i];
		if (obj)
			drm_gem_object_unreference_unlocked(obj);
	}

#ifndef MODULE
	if (rockchip_fb->logo)
		rockchip_free_loader_memory(fb->dev);
#else
	WARN_ON(rockchip_fb->logo);
#endif

	drm_framebuffer_cleanup(fb);
	kfree(rockchip_fb);
}

static int rockchip_drm_fb_create_handle(struct drm_framebuffer *fb,
					 struct drm_file *file_priv,
					 unsigned int *handle)
{
	struct rockchip_drm_fb *rockchip_fb = to_rockchip_fb(fb);

	return drm_gem_handle_create(file_priv,
				     rockchip_fb->obj[0], handle);
}

static const struct drm_framebuffer_funcs rockchip_drm_fb_funcs = {
	.destroy	= rockchip_drm_fb_destroy,
	.create_handle	= rockchip_drm_fb_create_handle,
};

struct drm_framebuffer *
rockchip_fb_alloc(struct drm_device *dev, struct drm_mode_fb_cmd2 *mode_cmd,
		  struct drm_gem_object **obj, struct rockchip_logo *logo,
		  unsigned int num_planes)
{
	struct rockchip_drm_fb *rockchip_fb;
	struct rockchip_gem_object *rk_obj;
	int ret = 0;
	int i;

	rockchip_fb = kzalloc(sizeof(*rockchip_fb), GFP_KERNEL);
	if (!rockchip_fb)
		return ERR_PTR(-ENOMEM);

	drm_helper_mode_fill_fb_struct(&rockchip_fb->fb, mode_cmd);

	ret = drm_framebuffer_init(dev, &rockchip_fb->fb,
				   &rockchip_drm_fb_funcs);
	if (ret) {
		dev_err(dev->dev, "Failed to initialize framebuffer: %d\n",
			ret);
		goto err_free_fb;
	}

	if (obj) {
		for (i = 0; i < num_planes; i++)
			rockchip_fb->obj[i] = obj[i];

		for (i = 0; i < num_planes; i++) {
			rk_obj = to_rockchip_obj(obj[i]);
			rockchip_fb->dma_addr[i] = rk_obj->dma_addr;
		}
#ifndef MODULE
	} else if (logo) {
		rockchip_fb->dma_addr[0] = logo->dma_addr;
		rockchip_fb->logo = logo;
		logo->count++;
#endif
	} else {
		ret = -EINVAL;
		dev_err(dev->dev, "Failed to find available buffer\n");
		goto err_deinit_drm_fb;
	}

	return &rockchip_fb->fb;

err_deinit_drm_fb:
	drm_framebuffer_cleanup(&rockchip_fb->fb);
err_free_fb:
	kfree(rockchip_fb);
	return ERR_PTR(ret);
}

static struct drm_framebuffer *
rockchip_user_fb_create(struct drm_device *dev, struct drm_file *file_priv,
			struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_framebuffer *fb;
	struct drm_gem_object *objs[ROCKCHIP_MAX_FB_BUFFER];
	struct drm_gem_object *obj;
	unsigned int hsub;
	unsigned int vsub;
	int num_planes;
	int ret;
	int i;

	hsub = drm_format_horz_chroma_subsampling(mode_cmd->pixel_format);
	vsub = drm_format_vert_chroma_subsampling(mode_cmd->pixel_format);
	num_planes = min(drm_format_num_planes(mode_cmd->pixel_format),
			 ROCKCHIP_MAX_FB_BUFFER);

	for (i = 0; i < num_planes; i++) {
		unsigned int width = mode_cmd->width / (i ? hsub : 1);
		unsigned int height = mode_cmd->height / (i ? vsub : 1);
		unsigned int min_size;
		unsigned int bpp =
			drm_format_plane_bpp(mode_cmd->pixel_format, i);

		obj = drm_gem_object_lookup(dev, file_priv,
					    mode_cmd->handles[i]);
		if (!obj) {
			dev_err(dev->dev, "Failed to lookup GEM object\n");
			ret = -ENXIO;
			goto err_gem_object_unreference;
		}

		min_size = (height - 1) * mode_cmd->pitches[i] +
			mode_cmd->offsets[i] + roundup(width * bpp, 8) / 8;
		if (obj->size < min_size) {
			drm_gem_object_unreference_unlocked(obj);
			ret = -EINVAL;
			goto err_gem_object_unreference;
		}
		objs[i] = obj;
	}

	fb = rockchip_fb_alloc(dev, mode_cmd, objs, NULL, i);
	if (IS_ERR(fb)) {
		ret = PTR_ERR(fb);
		goto err_gem_object_unreference;
	}

	return fb;

err_gem_object_unreference:
	for (i--; i >= 0; i--)
		drm_gem_object_unreference_unlocked(objs[i]);
	return ERR_PTR(ret);
}

static void rockchip_drm_output_poll_changed(struct drm_device *dev)
{
	struct rockchip_drm_private *private = dev->dev_private;
	struct drm_fb_helper *fb_helper = private->fbdev_helper;

	if (fb_helper)
		drm_fb_helper_hotplug_event(fb_helper);
}

static void rockchip_crtc_wait_for_update(struct drm_crtc *crtc)
{
	struct rockchip_drm_private *priv = crtc->dev->dev_private;
	int pipe = drm_crtc_index(crtc);
	const struct rockchip_crtc_funcs *crtc_funcs = priv->crtc_funcs[pipe];

	if (crtc_funcs && crtc_funcs->wait_for_update)
		crtc_funcs->wait_for_update(crtc);
}

/*
 * We can't use drm_atomic_helper_wait_for_vblanks() because rk3288 and rk3066
 * have hardware counters for neither vblanks nor scanlines, which results in
 * a race where:
 *				| <-- HW vsync irq and reg take effect
 *	       plane_commit --> |
 *	get_vblank and wait --> |
 *				| <-- handle_vblank, vblank->count + 1
 *		 cleanup_fb --> |
 *		iommu crash --> |
 *				| <-- HW vsync irq and reg take effect
 *
 * This function is equivalent but uses rockchip_crtc_wait_for_update() instead
 * of waiting for vblank_count to change.
 */
static void
rockchip_atomic_wait_for_complete(struct drm_device *dev, struct drm_atomic_state *old_state)
{
	struct drm_crtc_state *old_crtc_state;
	struct drm_crtc *crtc;
	int i, ret;

	for_each_crtc_in_state(old_state, crtc, old_crtc_state, i) {
		/* No one cares about the old state, so abuse it for tracking
		 * and store whether we hold a vblank reference (and should do a
		 * vblank wait) in the ->enable boolean.
		 */
		old_crtc_state->enable = false;

		if (!crtc->state->active)
			continue;

		if (!drm_atomic_helper_framebuffer_changed(dev,
				old_state, crtc))
			continue;

		ret = drm_crtc_vblank_get(crtc);
		if (ret != 0)
			continue;

		old_crtc_state->enable = true;
	}

	for_each_crtc_in_state(old_state, crtc, old_crtc_state, i) {
		if (!old_crtc_state->enable)
			continue;

		rockchip_crtc_wait_for_update(crtc);
		drm_crtc_vblank_put(crtc);
	}
}

static void
rockchip_atomic_commit_complete(struct rockchip_atomic_commit *commit)
{
	struct drm_atomic_state *state = commit->state;
	struct drm_device *dev = commit->dev;

	/*
	 * TODO: do fence wait here.
	 */

	/*
	 * Rockchip crtc support runtime PM, can't update display planes
	 * when crtc is disabled.
	 *
	 * drm_atomic_helper_commit comments detail that:
	 *     For drivers supporting runtime PM the recommended sequence is
	 *
	 *     drm_atomic_helper_commit_modeset_disables(dev, state);
	 *
	 *     drm_atomic_helper_commit_modeset_enables(dev, state);
	 *
	 *     drm_atomic_helper_commit_planes(dev, state, true);
	 *
	 * See the kerneldoc entries for these three functions for more details.
	 */
	drm_atomic_helper_commit_modeset_disables(dev, state);

	drm_atomic_helper_commit_modeset_enables(dev, state);

	drm_atomic_helper_commit_planes(dev, state, true);

	rockchip_atomic_wait_for_complete(dev, state);

	drm_atomic_helper_cleanup_planes(dev, state);

	drm_atomic_state_free(state);
}

void rockchip_drm_atomic_work(struct work_struct *work)
{
	struct rockchip_atomic_commit *commit = container_of(work,
					struct rockchip_atomic_commit, work);

	rockchip_atomic_commit_complete(commit);
}

int rockchip_drm_atomic_commit(struct drm_device *dev,
			       struct drm_atomic_state *state,
			       bool async)
{
	struct rockchip_drm_private *private = dev->dev_private;
	struct rockchip_atomic_commit *commit = &private->commit;
	int ret;

	ret = drm_atomic_helper_prepare_planes(dev, state);
	if (ret)
		return ret;

	/* serialize outstanding asynchronous commits */
	mutex_lock(&commit->lock);
	flush_work(&commit->work);

	drm_atomic_helper_swap_state(dev, state);

	commit->dev = dev;
	commit->state = state;

	if (async)
		schedule_work(&commit->work);
	else
		rockchip_atomic_commit_complete(commit);

	mutex_unlock(&commit->lock);

	return 0;
}

static const struct drm_mode_config_funcs rockchip_drm_mode_config_funcs = {
	.fb_create = rockchip_user_fb_create,
	.output_poll_changed = rockchip_drm_output_poll_changed,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = rockchip_drm_atomic_commit,
};

struct drm_framebuffer *
rockchip_drm_framebuffer_init(struct drm_device *dev,
			      struct drm_mode_fb_cmd2 *mode_cmd,
			      struct drm_gem_object *obj)
{
	struct drm_framebuffer *fb;

	fb = rockchip_fb_alloc(dev, mode_cmd, &obj, NULL, 1);
	if (IS_ERR(fb))
		return NULL;

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
	dev->mode_config.max_width = 8192;
	dev->mode_config.max_height = 8192;

	dev->mode_config.funcs = &rockchip_drm_mode_config_funcs;
}
