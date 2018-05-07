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
#include <soc/rockchip/rockchip_dmc.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_fb.h"
#include "rockchip_drm_gem.h"
#include "rockchip_drm_backlight.h"

bool rockchip_fb_is_logo(struct drm_framebuffer *fb)
{
	struct rockchip_drm_fb *rk_fb = to_rockchip_fb(fb);

	return rk_fb && rk_fb->logo;
}

dma_addr_t rockchip_fb_get_dma_addr(struct drm_framebuffer *fb,
				    unsigned int plane)
{
	struct rockchip_drm_fb *rk_fb = to_rockchip_fb(fb);

	if (WARN_ON(plane >= ROCKCHIP_MAX_FB_BUFFER))
		return 0;

	return rk_fb->dma_addr[plane];
}

void *rockchip_fb_get_kvaddr(struct drm_framebuffer *fb, unsigned int plane)
{
	struct rockchip_drm_fb *rk_fb = to_rockchip_fb(fb);

	if (WARN_ON(plane >= ROCKCHIP_MAX_FB_BUFFER))
		return 0;

	return rk_fb->kvaddr[plane];
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
			rockchip_fb->kvaddr[i] = rk_obj->kvaddr;
		}
#ifndef MODULE
	} else if (logo) {
		rockchip_fb->dma_addr[0] = logo->dma_addr;
		rockchip_fb->kvaddr[0] = logo->kvaddr;
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
			DRM_ERROR("Invalid Gem size on plane[%d]: %zd < %d\n",
				  i, obj->size, min_size);
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

static int rockchip_drm_bandwidth_atomic_check(struct drm_device *dev,
					       struct drm_atomic_state *state,
					       size_t *bandwidth)
{
	struct rockchip_drm_private *priv = dev->dev_private;
	struct drm_crtc_state *crtc_state;
	const struct rockchip_crtc_funcs *funcs;
	struct drm_crtc *crtc;
	int i, ret = 0;

	*bandwidth = 0;
	for_each_crtc_in_state(state, crtc, crtc_state, i) {
		funcs = priv->crtc_funcs[drm_crtc_index(crtc)];

		if (funcs && funcs->bandwidth)
			*bandwidth += funcs->bandwidth(crtc, crtc_state);
	}

	/*
	 * Check ddr frequency support here here.
	 */
	if (priv->dmc_support && !priv->devfreq) {
		priv->devfreq = devfreq_get_devfreq_by_phandle(dev->dev, 0);
		if (IS_ERR(priv->devfreq))
			priv->devfreq = NULL;
	}

	if (priv->devfreq)
		ret = rockchip_dmcfreq_vop_bandwidth_request(priv->devfreq,
							     *bandwidth);

	return ret;
}

static void
rockchip_atomic_commit_complete(struct rockchip_atomic_commit *commit)
{
	struct drm_atomic_state *state = commit->state;
	struct drm_device *dev = commit->dev;
	struct rockchip_drm_private *prv = dev->dev_private;
	size_t bandwidth = commit->bandwidth;

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
	drm_atomic_helper_wait_for_dependencies(state);

	drm_atomic_helper_commit_modeset_disables(dev, state);

	drm_atomic_helper_commit_modeset_enables(dev, state);

	rockchip_drm_backlight_update(dev);

	if (prv->dmc_support && !prv->devfreq) {
		prv->devfreq = devfreq_get_devfreq_by_phandle(dev->dev, 0);
		if (IS_ERR(prv->devfreq))
			prv->devfreq = NULL;
	}
	if (prv->devfreq)
		rockchip_dmcfreq_vop_bandwidth_update(prv->devfreq, bandwidth);

	drm_atomic_helper_commit_planes(dev, state, true);

	drm_atomic_helper_commit_hw_done(state);

	drm_atomic_helper_wait_for_vblanks(dev, state);

	drm_atomic_helper_cleanup_planes(dev, state);

	drm_atomic_helper_commit_cleanup_done(state);

	drm_atomic_state_free(state);

	kfree(commit);
}

void rockchip_drm_atomic_work(struct work_struct *work)
{
	struct rockchip_drm_private *private = container_of(work,
				struct rockchip_drm_private, commit_work);

	rockchip_atomic_commit_complete(private->commit);
	private->commit = NULL;
}

static int rockchip_drm_atomic_commit(struct drm_device *dev,
				      struct drm_atomic_state *state,
				      bool async)
{
	struct rockchip_drm_private *private = dev->dev_private;
	struct rockchip_atomic_commit *commit;
	size_t bandwidth;
	int ret;

	ret = drm_atomic_helper_setup_commit(state, false);
	if (ret)
		return ret;

	ret = drm_atomic_helper_prepare_planes(dev, state);
	if (ret)
		return ret;

	ret = rockchip_drm_bandwidth_atomic_check(dev, state, &bandwidth);
	if (ret) {
		/*
		 * TODO:
		 * Just report bandwidth can't support now.
		 */
		DRM_ERROR("vop bandwidth too large %zd\n", bandwidth);
	}

	drm_atomic_helper_swap_state(dev, state);

	commit = kmalloc(sizeof(*commit), GFP_KERNEL);
	if (!commit)
		return -ENOMEM;

	commit->dev = dev;
	commit->state = state;
	commit->bandwidth = bandwidth;

	if (async) {
		mutex_lock(&private->commit_lock);

		flush_work(&private->commit_work);
		WARN_ON(private->commit);
		private->commit = commit;
		schedule_work(&private->commit_work);

		mutex_unlock(&private->commit_lock);
	} else {
		rockchip_atomic_commit_complete(commit);
	}

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
