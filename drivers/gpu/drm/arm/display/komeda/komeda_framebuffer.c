// SPDX-License-Identifier: GPL-2.0
/*
 * (C) COPYRIGHT 2018 ARM Limited. All rights reserved.
 * Author: James.Qian.Wang <james.qian.wang@arm.com>
 *
 */
#include <drm/drm_device.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>

#include "komeda_framebuffer.h"
#include "komeda_dev.h"

static void komeda_fb_destroy(struct drm_framebuffer *fb)
{
	struct komeda_fb *kfb = to_kfb(fb);
	u32 i;

	for (i = 0; i < fb->format->num_planes; i++)
		drm_gem_object_put_unlocked(fb->obj[i]);

	drm_framebuffer_cleanup(fb);
	kfree(kfb);
}

static int komeda_fb_create_handle(struct drm_framebuffer *fb,
				   struct drm_file *file, u32 *handle)
{
	return drm_gem_handle_create(file, fb->obj[0], handle);
}

static const struct drm_framebuffer_funcs komeda_fb_funcs = {
	.destroy	= komeda_fb_destroy,
	.create_handle	= komeda_fb_create_handle,
};

static int
komeda_fb_none_afbc_size_check(struct komeda_dev *mdev, struct komeda_fb *kfb,
			       struct drm_file *file,
			       const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_framebuffer *fb = &kfb->base;
	struct drm_gem_object *obj;
	u32 min_size = 0;
	u32 i;

	for (i = 0; i < fb->format->num_planes; i++) {
		obj = drm_gem_object_lookup(file, mode_cmd->handles[i]);
		if (!obj) {
			DRM_DEBUG_KMS("Failed to lookup GEM object\n");
			fb->obj[i] = NULL;

			return -ENOENT;
		}

		kfb->aligned_w = fb->width / (i ? fb->format->hsub : 1);
		kfb->aligned_h = fb->height / (i ? fb->format->vsub : 1);

		if (fb->pitches[i] % mdev->chip.bus_width) {
			DRM_DEBUG_KMS("Pitch[%d]: 0x%x doesn't align to 0x%x\n",
				      i, fb->pitches[i], mdev->chip.bus_width);
			drm_gem_object_put_unlocked(obj);
			fb->obj[i] = NULL;

			return -EINVAL;
		}

		min_size = ((kfb->aligned_h / kfb->format_caps->tile_size - 1)
			    * fb->pitches[i])
			    + (kfb->aligned_w * fb->format->cpp[i]
			       * kfb->format_caps->tile_size)
			    + fb->offsets[i];

		if (obj->size < min_size) {
			DRM_DEBUG_KMS("Fail to check none afbc fb size.\n");
			drm_gem_object_put_unlocked(obj);
			fb->obj[i] = NULL;

			return -EINVAL;
		}

		fb->obj[i] = obj;
	}

	if (fb->format->num_planes == 3) {
		if (fb->pitches[1] != fb->pitches[2]) {
			DRM_DEBUG_KMS("The pitch[1] and [2] are not same\n");
			return -EINVAL;
		}
	}

	return 0;
}

struct drm_framebuffer *
komeda_fb_create(struct drm_device *dev, struct drm_file *file,
		 const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct komeda_dev *mdev = dev->dev_private;
	struct komeda_fb *kfb;
	int ret = 0, i;

	kfb = kzalloc(sizeof(*kfb), GFP_KERNEL);
	if (!kfb)
		return ERR_PTR(-ENOMEM);

	kfb->format_caps = komeda_get_format_caps(&mdev->fmt_tbl,
						  mode_cmd->pixel_format,
						  mode_cmd->modifier[0]);
	if (!kfb->format_caps) {
		DRM_DEBUG_KMS("FMT %x is not supported.\n",
			      mode_cmd->pixel_format);
		kfree(kfb);
		return ERR_PTR(-EINVAL);
	}

	drm_helper_mode_fill_fb_struct(dev, &kfb->base, mode_cmd);

	ret = komeda_fb_none_afbc_size_check(mdev, kfb, file, mode_cmd);
	if (ret < 0)
		goto err_cleanup;

	ret = drm_framebuffer_init(dev, &kfb->base, &komeda_fb_funcs);
	if (ret < 0) {
		DRM_DEBUG_KMS("failed to initialize fb\n");

		goto err_cleanup;
	}

	return &kfb->base;

err_cleanup:
	for (i = 0; i < kfb->base.format->num_planes; i++)
		drm_gem_object_put_unlocked(kfb->base.obj[i]);

	kfree(kfb);
	return ERR_PTR(ret);
}

dma_addr_t
komeda_fb_get_pixel_addr(struct komeda_fb *kfb, int x, int y, int plane)
{
	struct drm_framebuffer *fb = &kfb->base;
	const struct drm_gem_cma_object *obj;
	u32 plane_x, plane_y, cpp, pitch, offset;

	if (plane >= fb->format->num_planes) {
		DRM_DEBUG_KMS("Out of max plane num.\n");
		return -EINVAL;
	}

	obj = drm_fb_cma_get_gem_obj(fb, plane);

	offset = fb->offsets[plane];
	if (!fb->modifier) {
		plane_x = x / (plane ? fb->format->hsub : 1);
		plane_y = y / (plane ? fb->format->vsub : 1);
		cpp = fb->format->cpp[plane];
		pitch = fb->pitches[plane];
		offset += plane_x * cpp *  kfb->format_caps->tile_size +
				(plane_y * pitch) / kfb->format_caps->tile_size;
	}

	return obj->paddr + offset;
}
