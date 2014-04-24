/*
 * Copyright (C) ROCKCHIP, Inc.
 * Author:yzq<yzq@rock-chips.com>
 *
 * based on exynos_drm_fb.c
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
#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <uapi/drm/rockchip_drm.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_fb.h"
#include "rockchip_drm_gem.h"
#include "rockchip_drm_iommu.h"
#include "rockchip_drm_encoder.h"

#define to_rockchip_fb(x)	container_of(x, struct rockchip_drm_fb, fb)

/*
 * rockchip specific framebuffer structure.
 *
 * @fb: drm framebuffer obejct.
 * @buf_cnt: a buffer count to drm framebuffer.
 * @rockchip_gem_obj: array of rockchip specific gem object containing a gem object.
 */
struct rockchip_drm_fb {
	struct drm_framebuffer		fb;
	unsigned int			buf_cnt;
	struct rockchip_drm_gem_obj	*rockchip_gem_obj[MAX_FB_BUFFER];
};

static int check_fb_gem_memory_type(struct drm_device *drm_dev,
				struct rockchip_drm_gem_obj *rockchip_gem_obj)
{
	unsigned int flags;

	/*
	 * if rockchip drm driver supports iommu then framebuffer can use
	 * all the buffer types.
	 */
	if (is_drm_iommu_supported(drm_dev))
		return 0;

	flags = rockchip_gem_obj->flags;

	/*
	 * without iommu support, not support physically non-continuous memory
	 * for framebuffer.
	 */
	if (IS_NONCONTIG_BUFFER(flags)) {
		DRM_ERROR("cannot use this gem memory type for fb.\n");
		return -EINVAL;
	}

	return 0;
}

static void rockchip_drm_fb_destroy(struct drm_framebuffer *fb)
{
	struct rockchip_drm_fb *rockchip_fb = to_rockchip_fb(fb);
	unsigned int i;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	/* make sure that overlay data are updated before relesing fb. */
	rockchip_drm_encoder_complete_scanout(fb);

	drm_framebuffer_cleanup(fb);

	for (i = 0; i < ARRAY_SIZE(rockchip_fb->rockchip_gem_obj); i++) {
		struct drm_gem_object *obj;

		if (rockchip_fb->rockchip_gem_obj[i] == NULL)
			continue;

		obj = &rockchip_fb->rockchip_gem_obj[i]->base;
		drm_gem_object_unreference_unlocked(obj);
	}

	kfree(rockchip_fb);
	rockchip_fb = NULL;
}

static int rockchip_drm_fb_create_handle(struct drm_framebuffer *fb,
					struct drm_file *file_priv,
					unsigned int *handle)
{
	struct rockchip_drm_fb *rockchip_fb = to_rockchip_fb(fb);

	DRM_DEBUG_KMS("%s\n", __FILE__);

	/* This fb should have only one gem object. */
	if (WARN_ON(rockchip_fb->buf_cnt != 1))
		return -EINVAL;

	return drm_gem_handle_create(file_priv,
			&rockchip_fb->rockchip_gem_obj[0]->base, handle);
}

static int rockchip_drm_fb_dirty(struct drm_framebuffer *fb,
				struct drm_file *file_priv, unsigned flags,
				unsigned color, struct drm_clip_rect *clips,
				unsigned num_clips)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	/* TODO */

	return 0;
}

static struct drm_framebuffer_funcs rockchip_drm_fb_funcs = {
	.destroy	= rockchip_drm_fb_destroy,
	.create_handle	= rockchip_drm_fb_create_handle,
	.dirty		= rockchip_drm_fb_dirty,
};

void rockchip_drm_fb_set_buf_cnt(struct drm_framebuffer *fb,
						unsigned int cnt)
{
	struct rockchip_drm_fb *rockchip_fb;

	rockchip_fb = to_rockchip_fb(fb);

	rockchip_fb->buf_cnt = cnt;
}

unsigned int rockchip_drm_fb_get_buf_cnt(struct drm_framebuffer *fb)
{
	struct rockchip_drm_fb *rockchip_fb;

	rockchip_fb = to_rockchip_fb(fb);

	return rockchip_fb->buf_cnt;
}

struct drm_framebuffer *
rockchip_drm_framebuffer_init(struct drm_device *dev,
			    struct drm_mode_fb_cmd2 *mode_cmd,
			    struct drm_gem_object *obj)
{
	struct rockchip_drm_fb *rockchip_fb;
	struct rockchip_drm_gem_obj *rockchip_gem_obj;
	int ret;

	rockchip_gem_obj = to_rockchip_gem_obj(obj);

	ret = check_fb_gem_memory_type(dev, rockchip_gem_obj);
	if (ret < 0) {
		DRM_ERROR("cannot use this gem memory type for fb.\n");
		return ERR_PTR(-EINVAL);
	}

	rockchip_fb = kzalloc(sizeof(*rockchip_fb), GFP_KERNEL);
	if (!rockchip_fb) {
		DRM_ERROR("failed to allocate rockchip drm framebuffer\n");
		return ERR_PTR(-ENOMEM);
	}

	drm_helper_mode_fill_fb_struct(&rockchip_fb->fb, mode_cmd);
	rockchip_fb->rockchip_gem_obj[0] = rockchip_gem_obj;

	ret = drm_framebuffer_init(dev, &rockchip_fb->fb, &rockchip_drm_fb_funcs);
	if (ret) {
		DRM_ERROR("failed to initialize framebuffer\n");
		return ERR_PTR(ret);
	}

	return &rockchip_fb->fb;
}

static u32 rockchip_drm_format_num_buffers(struct drm_mode_fb_cmd2 *mode_cmd)
{
	unsigned int cnt = 0;

	if (mode_cmd->pixel_format != DRM_FORMAT_NV12)
		return drm_format_num_planes(mode_cmd->pixel_format);

	while (cnt != MAX_FB_BUFFER) {
		if (!mode_cmd->handles[cnt])
			break;
		cnt++;
	}

	/*
	 * check if NV12 or NV12M.
	 *
	 * NV12
	 * handles[0] = base1, offsets[0] = 0
	 * handles[1] = base1, offsets[1] = Y_size
	 *
	 * NV12M
	 * handles[0] = base1, offsets[0] = 0
	 * handles[1] = base2, offsets[1] = 0
	 */
	if (cnt == 2) {
		/*
		 * in case of NV12 format, offsets[1] is not 0 and
		 * handles[0] is same as handles[1].
		 */
		if (mode_cmd->offsets[1] &&
			mode_cmd->handles[0] == mode_cmd->handles[1])
			cnt = 1;
	}

	return cnt;
}

static struct drm_framebuffer *
rockchip_user_fb_create(struct drm_device *dev, struct drm_file *file_priv,
		      struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_gem_object *obj;
	struct rockchip_drm_gem_obj *rockchip_gem_obj;
	struct rockchip_drm_fb *rockchip_fb;
	int i, ret;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	rockchip_fb = kzalloc(sizeof(*rockchip_fb), GFP_KERNEL);
	if (!rockchip_fb) {
		DRM_ERROR("failed to allocate rockchip drm framebuffer\n");
		return ERR_PTR(-ENOMEM);
	}

	obj = drm_gem_object_lookup(dev, file_priv, mode_cmd->handles[0]);
	if (!obj) {
		DRM_ERROR("failed to lookup gem object\n");
		ret = -ENOENT;
		goto err_free;
	}

	drm_helper_mode_fill_fb_struct(&rockchip_fb->fb, mode_cmd);
	rockchip_fb->rockchip_gem_obj[0] = to_rockchip_gem_obj(obj);
	rockchip_fb->buf_cnt = rockchip_drm_format_num_buffers(mode_cmd);

	DRM_DEBUG_KMS("buf_cnt = %d\n", rockchip_fb->buf_cnt);

	for (i = 1; i < rockchip_fb->buf_cnt; i++) {
		obj = drm_gem_object_lookup(dev, file_priv,
				mode_cmd->handles[i]);
		if (!obj) {
			DRM_ERROR("failed to lookup gem object\n");
			ret = -ENOENT;
			rockchip_fb->buf_cnt = i;
			goto err_unreference;
		}

		rockchip_gem_obj = to_rockchip_gem_obj(obj);
		rockchip_fb->rockchip_gem_obj[i] = rockchip_gem_obj;

		ret = check_fb_gem_memory_type(dev, rockchip_gem_obj);
		if (ret < 0) {
			DRM_ERROR("cannot use this gem memory type for fb.\n");
			goto err_unreference;
		}
	}

	ret = drm_framebuffer_init(dev, &rockchip_fb->fb, &rockchip_drm_fb_funcs);
	if (ret) {
		DRM_ERROR("failed to init framebuffer.\n");
		goto err_unreference;
	}

	return &rockchip_fb->fb;

err_unreference:
	for (i = 0; i < rockchip_fb->buf_cnt; i++) {
		struct drm_gem_object *obj;

		obj = &rockchip_fb->rockchip_gem_obj[i]->base;
		if (obj)
			drm_gem_object_unreference_unlocked(obj);
	}
err_free:
	kfree(rockchip_fb);
	return ERR_PTR(ret);
}

struct rockchip_drm_gem_buf *rockchip_drm_fb_buffer(struct drm_framebuffer *fb,
						int index)
{
	struct rockchip_drm_fb *rockchip_fb = to_rockchip_fb(fb);
	struct rockchip_drm_gem_buf *buffer;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (index >= MAX_FB_BUFFER)
		return NULL;

	buffer = rockchip_fb->rockchip_gem_obj[index]->buffer;
	if (!buffer)
		return NULL;

	DRM_DEBUG_KMS("dma_addr = 0x%lx\n", (unsigned long)buffer->dma_addr);

	return buffer;
}

static void rockchip_drm_output_poll_changed(struct drm_device *dev)
{
	struct rockchip_drm_private *private = dev->dev_private;
	struct drm_fb_helper *fb_helper = private->fb_helper;

	if (fb_helper)
		drm_fb_helper_hotplug_event(fb_helper);
}

static const struct drm_mode_config_funcs rockchip_drm_mode_config_funcs = {
	.fb_create = rockchip_user_fb_create,
	.output_poll_changed = rockchip_drm_output_poll_changed,
};

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
}
