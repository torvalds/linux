// SPDX-License-Identifier: GPL-2.0
/*
 * (C) COPYRIGHT 2018 ARM Limited. All rights reserved.
 * Author: James.Qian.Wang <james.qian.wang@arm.com>
 *
 */
#include <drm/drm_device.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>

#include "komeda_framebuffer.h"
#include "komeda_dev.h"

static void komeda_fb_destroy(struct drm_framebuffer *fb)
{
	struct komeda_fb *kfb = to_kfb(fb);
	u32 i;

	for (i = 0; i < fb->format->num_planes; i++)
		drm_gem_object_put(fb->obj[i]);

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
komeda_fb_afbc_size_check(struct komeda_fb *kfb, struct drm_file *file,
			  const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_framebuffer *fb = &kfb->base;
	const struct drm_format_info *info = fb->format;
	struct drm_gem_object *obj;
	u32 alignment_w = 0, alignment_h = 0, alignment_header, n_blocks, bpp;
	u64 min_size;

	obj = drm_gem_object_lookup(file, mode_cmd->handles[0]);
	if (!obj) {
		DRM_DEBUG_KMS("Failed to lookup GEM object\n");
		return -ENOENT;
	}

	switch (fb->modifier & AFBC_FORMAT_MOD_BLOCK_SIZE_MASK) {
	case AFBC_FORMAT_MOD_BLOCK_SIZE_32x8:
		alignment_w = 32;
		alignment_h = 8;
		break;
	case AFBC_FORMAT_MOD_BLOCK_SIZE_16x16:
		alignment_w = 16;
		alignment_h = 16;
		break;
	default:
		WARN(1, "Invalid AFBC_FORMAT_MOD_BLOCK_SIZE: %lld.\n",
		     fb->modifier & AFBC_FORMAT_MOD_BLOCK_SIZE_MASK);
		break;
	}

	/* tiled header afbc */
	if (fb->modifier & AFBC_FORMAT_MOD_TILED) {
		alignment_w *= AFBC_TH_LAYOUT_ALIGNMENT;
		alignment_h *= AFBC_TH_LAYOUT_ALIGNMENT;
		alignment_header = AFBC_TH_BODY_START_ALIGNMENT;
	} else {
		alignment_header = AFBC_BODY_START_ALIGNMENT;
	}

	kfb->aligned_w = ALIGN(fb->width, alignment_w);
	kfb->aligned_h = ALIGN(fb->height, alignment_h);

	if (fb->offsets[0] % alignment_header) {
		DRM_DEBUG_KMS("afbc offset alignment check failed.\n");
		goto check_failed;
	}

	n_blocks = (kfb->aligned_w * kfb->aligned_h) / AFBC_SUPERBLK_PIXELS;
	kfb->offset_payload = ALIGN(n_blocks * AFBC_HEADER_SIZE,
				    alignment_header);

	bpp = komeda_get_afbc_format_bpp(info, fb->modifier);
	kfb->afbc_size = kfb->offset_payload + n_blocks *
			 ALIGN(bpp * AFBC_SUPERBLK_PIXELS / 8,
			       AFBC_SUPERBLK_ALIGNMENT);
	min_size = kfb->afbc_size + fb->offsets[0];
	if (min_size > obj->size) {
		DRM_DEBUG_KMS("afbc size check failed, obj_size: 0x%zx. min_size 0x%llx.\n",
			      obj->size, min_size);
		goto check_failed;
	}

	fb->obj[0] = obj;
	return 0;

check_failed:
	drm_gem_object_put(obj);
	return -EINVAL;
}

static int
komeda_fb_none_afbc_size_check(struct komeda_dev *mdev, struct komeda_fb *kfb,
			       struct drm_file *file,
			       const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_framebuffer *fb = &kfb->base;
	const struct drm_format_info *info = fb->format;
	struct drm_gem_object *obj;
	u32 i, block_h;
	u64 min_size;

	if (komeda_fb_check_src_coords(kfb, 0, 0, fb->width, fb->height))
		return -EINVAL;

	for (i = 0; i < info->num_planes; i++) {
		obj = drm_gem_object_lookup(file, mode_cmd->handles[i]);
		if (!obj) {
			DRM_DEBUG_KMS("Failed to lookup GEM object\n");
			return -ENOENT;
		}
		fb->obj[i] = obj;

		block_h = drm_format_info_block_height(info, i);
		if ((fb->pitches[i] * block_h) % mdev->chip.bus_width) {
			DRM_DEBUG_KMS("Pitch[%d]: 0x%x doesn't align to 0x%x\n",
				      i, fb->pitches[i], mdev->chip.bus_width);
			return -EINVAL;
		}

		min_size = komeda_fb_get_pixel_addr(kfb, 0, fb->height, i)
			 - to_drm_gem_dma_obj(obj)->dma_addr;
		if (obj->size < min_size) {
			DRM_DEBUG_KMS("The fb->obj[%d] size: 0x%zx lower than the minimum requirement: 0x%llx.\n",
				      i, obj->size, min_size);
			return -EINVAL;
		}
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
		 const struct drm_format_info *info,
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

	drm_helper_mode_fill_fb_struct(dev, &kfb->base, info, mode_cmd);

	if (kfb->base.modifier)
		ret = komeda_fb_afbc_size_check(kfb, file, mode_cmd);
	else
		ret = komeda_fb_none_afbc_size_check(mdev, kfb, file, mode_cmd);
	if (ret < 0)
		goto err_cleanup;

	ret = drm_framebuffer_init(dev, &kfb->base, &komeda_fb_funcs);
	if (ret < 0) {
		DRM_DEBUG_KMS("failed to initialize fb\n");

		goto err_cleanup;
	}

	kfb->is_va = mdev->iommu ? true : false;

	return &kfb->base;

err_cleanup:
	for (i = 0; i < kfb->base.format->num_planes; i++)
		drm_gem_object_put(kfb->base.obj[i]);

	kfree(kfb);
	return ERR_PTR(ret);
}

int komeda_fb_check_src_coords(const struct komeda_fb *kfb,
			       u32 src_x, u32 src_y, u32 src_w, u32 src_h)
{
	const struct drm_framebuffer *fb = &kfb->base;
	const struct drm_format_info *info = fb->format;
	u32 block_w = drm_format_info_block_width(fb->format, 0);
	u32 block_h = drm_format_info_block_height(fb->format, 0);

	if ((src_x + src_w > fb->width) || (src_y + src_h > fb->height)) {
		DRM_DEBUG_ATOMIC("Invalid source coordinate.\n");
		return -EINVAL;
	}

	if ((src_x % info->hsub) || (src_w % info->hsub) ||
	    (src_y % info->vsub) || (src_h % info->vsub)) {
		DRM_DEBUG_ATOMIC("Wrong subsampling dimension x:%d, y:%d, w:%d, h:%d for format: %x.\n",
				 src_x, src_y, src_w, src_h, info->format);
		return -EINVAL;
	}

	if ((src_x % block_w) || (src_w % block_w) ||
	    (src_y % block_h) || (src_h % block_h)) {
		DRM_DEBUG_ATOMIC("x:%d, y:%d, w:%d, h:%d should be multiple of block_w/h for format: %x.\n",
				 src_x, src_y, src_w, src_h, info->format);
		return -EINVAL;
	}

	return 0;
}

dma_addr_t
komeda_fb_get_pixel_addr(struct komeda_fb *kfb, int x, int y, int plane)
{
	struct drm_framebuffer *fb = &kfb->base;
	const struct drm_gem_dma_object *obj;
	u32 offset, plane_x, plane_y, block_w, block_sz;

	if (plane >= fb->format->num_planes) {
		DRM_DEBUG_KMS("Out of max plane num.\n");
		return -EINVAL;
	}

	obj = drm_fb_dma_get_gem_obj(fb, plane);

	offset = fb->offsets[plane];
	if (!fb->modifier) {
		block_w = drm_format_info_block_width(fb->format, plane);
		block_sz = fb->format->char_per_block[plane];
		plane_x = x / (plane ? fb->format->hsub : 1);
		plane_y = y / (plane ? fb->format->vsub : 1);

		offset += (plane_x / block_w) * block_sz
			+ plane_y * fb->pitches[plane];
	}

	return obj->dma_addr + offset;
}

/* if the fb can be supported by a specific layer */
bool komeda_fb_is_layer_supported(struct komeda_fb *kfb, u32 layer_type,
				  u32 rot)
{
	struct drm_framebuffer *fb = &kfb->base;
	struct komeda_dev *mdev = fb->dev->dev_private;
	u32 fourcc = fb->format->format;
	u64 modifier = fb->modifier;
	bool supported;

	supported = komeda_format_mod_supported(&mdev->fmt_tbl, layer_type,
						fourcc, modifier, rot);
	if (!supported)
		DRM_DEBUG_ATOMIC("Layer TYPE: %d doesn't support fb FMT: %p4cc with modifier: 0x%llx.\n",
				 layer_type, &fourcc, modifier);

	return supported;
}
