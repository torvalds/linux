// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2011 Texas Instruments Incorporated - http://www.ti.com/
 * Author: Rob Clark <rob@ti.com>
 */

#include <linux/seq_file.h>

#include <drm/drm_crtc.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>

#include "omap_dmm_tiler.h"
#include "omap_drv.h"

/*
 * framebuffer funcs
 */

static const u32 formats[] = {
	/* 16bpp [A]RGB: */
	DRM_FORMAT_RGB565, /* RGB16-565 */
	DRM_FORMAT_RGBX4444, /* RGB12x-4444 */
	DRM_FORMAT_XRGB4444, /* xRGB12-4444 */
	DRM_FORMAT_RGBA4444, /* RGBA12-4444 */
	DRM_FORMAT_ARGB4444, /* ARGB16-4444 */
	DRM_FORMAT_XRGB1555, /* xRGB15-1555 */
	DRM_FORMAT_ARGB1555, /* ARGB16-1555 */
	/* 24bpp RGB: */
	DRM_FORMAT_RGB888,   /* RGB24-888 */
	/* 32bpp [A]RGB: */
	DRM_FORMAT_RGBX8888, /* RGBx24-8888 */
	DRM_FORMAT_XRGB8888, /* xRGB24-8888 */
	DRM_FORMAT_RGBA8888, /* RGBA32-8888 */
	DRM_FORMAT_ARGB8888, /* ARGB32-8888 */
	/* YUV: */
	DRM_FORMAT_NV12,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_UYVY,
};

/* per-plane info for the fb: */
struct plane {
	dma_addr_t dma_addr;
};

#define to_omap_framebuffer(x) container_of(x, struct omap_framebuffer, base)

struct omap_framebuffer {
	struct drm_framebuffer base;
	int pin_count;
	const struct drm_format_info *format;
	struct plane planes[2];
	/* lock for pinning (pin_count and planes.dma_addr) */
	struct mutex lock;
};

static int omap_framebuffer_dirty(struct drm_framebuffer *fb,
				  struct drm_file *file_priv,
				  unsigned flags, unsigned color,
				  struct drm_clip_rect *clips,
				  unsigned num_clips)
{
	struct drm_crtc *crtc;

	drm_modeset_lock_all(fb->dev);

	drm_for_each_crtc(crtc, fb->dev)
		omap_crtc_flush(crtc);

	drm_modeset_unlock_all(fb->dev);

	return 0;
}

static const struct drm_framebuffer_funcs omap_framebuffer_funcs = {
	.create_handle = drm_gem_fb_create_handle,
	.dirty = omap_framebuffer_dirty,
	.destroy = drm_gem_fb_destroy,
};

static u32 get_linear_addr(struct drm_framebuffer *fb,
		const struct drm_format_info *format, int n, int x, int y)
{
	struct omap_framebuffer *omap_fb = to_omap_framebuffer(fb);
	struct plane *plane = &omap_fb->planes[n];
	u32 offset;

	offset = fb->offsets[n]
	       + (x * format->cpp[n] / (n == 0 ? 1 : format->hsub))
	       + (y * fb->pitches[n] / (n == 0 ? 1 : format->vsub));

	return plane->dma_addr + offset;
}

bool omap_framebuffer_supports_rotation(struct drm_framebuffer *fb)
{
	return omap_gem_flags(fb->obj[0]) & OMAP_BO_TILED;
}

/* Note: DRM rotates counter-clockwise, TILER & DSS rotates clockwise */
static u32 drm_rotation_to_tiler(unsigned int drm_rot)
{
	u32 orient;

	switch (drm_rot & DRM_MODE_ROTATE_MASK) {
	default:
	case DRM_MODE_ROTATE_0:
		orient = 0;
		break;
	case DRM_MODE_ROTATE_90:
		orient = MASK_XY_FLIP | MASK_X_INVERT;
		break;
	case DRM_MODE_ROTATE_180:
		orient = MASK_X_INVERT | MASK_Y_INVERT;
		break;
	case DRM_MODE_ROTATE_270:
		orient = MASK_XY_FLIP | MASK_Y_INVERT;
		break;
	}

	if (drm_rot & DRM_MODE_REFLECT_X)
		orient ^= MASK_X_INVERT;

	if (drm_rot & DRM_MODE_REFLECT_Y)
		orient ^= MASK_Y_INVERT;

	return orient;
}

/* update ovl info for scanout, handles cases of multi-planar fb's, etc.
 */
void omap_framebuffer_update_scanout(struct drm_framebuffer *fb,
		struct drm_plane_state *state, struct omap_overlay_info *info)
{
	struct omap_framebuffer *omap_fb = to_omap_framebuffer(fb);
	const struct drm_format_info *format = omap_fb->format;
	struct plane *plane = &omap_fb->planes[0];
	u32 x, y, orient = 0;

	info->fourcc = fb->format->format;

	info->pos_x      = state->crtc_x;
	info->pos_y      = state->crtc_y;
	info->out_width  = state->crtc_w;
	info->out_height = state->crtc_h;
	info->width      = state->src_w >> 16;
	info->height     = state->src_h >> 16;

	/* DSS driver wants the w & h in rotated orientation */
	if (drm_rotation_90_or_270(state->rotation))
		swap(info->width, info->height);

	x = state->src_x >> 16;
	y = state->src_y >> 16;

	if (omap_gem_flags(fb->obj[0]) & OMAP_BO_TILED) {
		u32 w = state->src_w >> 16;
		u32 h = state->src_h >> 16;

		orient = drm_rotation_to_tiler(state->rotation);

		/*
		 * omap_gem_rotated_paddr() wants the x & y in tiler units.
		 * Usually tiler unit size is the same as the pixel size, except
		 * for YUV422 formats, for which the tiler unit size is 32 bits
		 * and pixel size is 16 bits.
		 */
		if (fb->format->format == DRM_FORMAT_UYVY ||
				fb->format->format == DRM_FORMAT_YUYV) {
			x /= 2;
			w /= 2;
		}

		/* adjust x,y offset for invert: */
		if (orient & MASK_Y_INVERT)
			y += h - 1;
		if (orient & MASK_X_INVERT)
			x += w - 1;

		/* Note: x and y are in TILER units, not pixels */
		omap_gem_rotated_dma_addr(fb->obj[0], orient, x, y,
					  &info->paddr);
		info->rotation_type = OMAP_DSS_ROT_TILER;
		info->rotation = state->rotation ?: DRM_MODE_ROTATE_0;
		/* Note: stride in TILER units, not pixels */
		info->screen_width  = omap_gem_tiled_stride(fb->obj[0], orient);
	} else {
		switch (state->rotation & DRM_MODE_ROTATE_MASK) {
		case 0:
		case DRM_MODE_ROTATE_0:
			/* OK */
			break;

		default:
			dev_warn(fb->dev->dev,
				"rotation '%d' ignored for non-tiled fb\n",
				state->rotation);
			break;
		}

		info->paddr         = get_linear_addr(fb, format, 0, x, y);
		info->rotation_type = OMAP_DSS_ROT_NONE;
		info->rotation      = DRM_MODE_ROTATE_0;
		info->screen_width  = fb->pitches[0];
	}

	/* convert to pixels: */
	info->screen_width /= format->cpp[0];

	if (fb->format->format == DRM_FORMAT_NV12) {
		plane = &omap_fb->planes[1];

		if (info->rotation_type == OMAP_DSS_ROT_TILER) {
			WARN_ON(!(omap_gem_flags(fb->obj[1]) & OMAP_BO_TILED));
			omap_gem_rotated_dma_addr(fb->obj[1], orient, x/2, y/2,
						  &info->p_uv_addr);
		} else {
			info->p_uv_addr = get_linear_addr(fb, format, 1, x, y);
		}
	} else {
		info->p_uv_addr = 0;
	}
}

/* pin, prepare for scanout: */
int omap_framebuffer_pin(struct drm_framebuffer *fb)
{
	struct omap_framebuffer *omap_fb = to_omap_framebuffer(fb);
	int ret, i, n = fb->format->num_planes;

	mutex_lock(&omap_fb->lock);

	if (omap_fb->pin_count > 0) {
		omap_fb->pin_count++;
		mutex_unlock(&omap_fb->lock);
		return 0;
	}

	for (i = 0; i < n; i++) {
		struct plane *plane = &omap_fb->planes[i];
		ret = omap_gem_pin(fb->obj[i], &plane->dma_addr);
		if (ret)
			goto fail;
		omap_gem_dma_sync_buffer(fb->obj[i], DMA_TO_DEVICE);
	}

	omap_fb->pin_count++;

	mutex_unlock(&omap_fb->lock);

	return 0;

fail:
	for (i--; i >= 0; i--) {
		struct plane *plane = &omap_fb->planes[i];
		omap_gem_unpin(fb->obj[i]);
		plane->dma_addr = 0;
	}

	mutex_unlock(&omap_fb->lock);

	return ret;
}

/* unpin, no longer being scanned out: */
void omap_framebuffer_unpin(struct drm_framebuffer *fb)
{
	struct omap_framebuffer *omap_fb = to_omap_framebuffer(fb);
	int i, n = fb->format->num_planes;

	mutex_lock(&omap_fb->lock);

	omap_fb->pin_count--;

	if (omap_fb->pin_count > 0) {
		mutex_unlock(&omap_fb->lock);
		return;
	}

	for (i = 0; i < n; i++) {
		struct plane *plane = &omap_fb->planes[i];
		omap_gem_unpin(fb->obj[i]);
		plane->dma_addr = 0;
	}

	mutex_unlock(&omap_fb->lock);
}

#ifdef CONFIG_DEBUG_FS
void omap_framebuffer_describe(struct drm_framebuffer *fb, struct seq_file *m)
{
	int i, n = fb->format->num_planes;

	seq_printf(m, "fb: %dx%d@%4.4s\n", fb->width, fb->height,
			(char *)&fb->format->format);

	for (i = 0; i < n; i++) {
		seq_printf(m, "   %d: offset=%d pitch=%d, obj: ",
				i, fb->offsets[n], fb->pitches[i]);
		omap_gem_describe(fb->obj[i], m);
	}
}
#endif

struct drm_framebuffer *omap_framebuffer_create(struct drm_device *dev,
		struct drm_file *file, const struct drm_mode_fb_cmd2 *mode_cmd)
{
	const struct drm_format_info *info = drm_get_format_info(dev,
								 mode_cmd);
	unsigned int num_planes = info->num_planes;
	struct drm_gem_object *bos[4];
	struct drm_framebuffer *fb;
	int i;

	for (i = 0; i < num_planes; i++) {
		bos[i] = drm_gem_object_lookup(file, mode_cmd->handles[i]);
		if (!bos[i]) {
			fb = ERR_PTR(-ENOENT);
			goto error;
		}
	}

	fb = omap_framebuffer_init(dev, mode_cmd, bos);
	if (IS_ERR(fb))
		goto error;

	return fb;

error:
	while (--i >= 0)
		drm_gem_object_put_unlocked(bos[i]);

	return fb;
}

struct drm_framebuffer *omap_framebuffer_init(struct drm_device *dev,
		const struct drm_mode_fb_cmd2 *mode_cmd, struct drm_gem_object **bos)
{
	const struct drm_format_info *format = NULL;
	struct omap_framebuffer *omap_fb = NULL;
	struct drm_framebuffer *fb = NULL;
	unsigned int pitch = mode_cmd->pitches[0];
	int ret, i;

	DBG("create framebuffer: dev=%p, mode_cmd=%p (%dx%d@%4.4s)",
			dev, mode_cmd, mode_cmd->width, mode_cmd->height,
			(char *)&mode_cmd->pixel_format);

	format = drm_get_format_info(dev, mode_cmd);

	for (i = 0; i < ARRAY_SIZE(formats); i++) {
		if (formats[i] == mode_cmd->pixel_format)
			break;
	}

	if (!format || i == ARRAY_SIZE(formats)) {
		dev_dbg(dev->dev, "unsupported pixel format: %4.4s\n",
			(char *)&mode_cmd->pixel_format);
		ret = -EINVAL;
		goto fail;
	}

	omap_fb = kzalloc(sizeof(*omap_fb), GFP_KERNEL);
	if (!omap_fb) {
		ret = -ENOMEM;
		goto fail;
	}

	fb = &omap_fb->base;
	omap_fb->format = format;
	mutex_init(&omap_fb->lock);

	/*
	 * The code below assumes that no format use more than two planes, and
	 * that the two planes of multiplane formats need the same number of
	 * bytes per pixel.
	 */
	if (format->num_planes == 2 && pitch != mode_cmd->pitches[1]) {
		dev_dbg(dev->dev, "pitches differ between planes 0 and 1\n");
		ret = -EINVAL;
		goto fail;
	}

	if (pitch % format->cpp[0]) {
		dev_dbg(dev->dev,
			"buffer pitch (%u bytes) is not a multiple of pixel size (%u bytes)\n",
			pitch, format->cpp[0]);
		ret = -EINVAL;
		goto fail;
	}

	for (i = 0; i < format->num_planes; i++) {
		struct plane *plane = &omap_fb->planes[i];
		unsigned int vsub = i == 0 ? 1 : format->vsub;
		unsigned int size;

		size = pitch * mode_cmd->height / vsub;

		if (size > omap_gem_mmap_size(bos[i]) - mode_cmd->offsets[i]) {
			dev_dbg(dev->dev,
				"provided buffer object is too small! %zu < %d\n",
				bos[i]->size - mode_cmd->offsets[i], size);
			ret = -EINVAL;
			goto fail;
		}

		fb->obj[i]    = bos[i];
		plane->dma_addr  = 0;
	}

	drm_helper_mode_fill_fb_struct(dev, fb, mode_cmd);

	ret = drm_framebuffer_init(dev, fb, &omap_framebuffer_funcs);
	if (ret) {
		dev_err(dev->dev, "framebuffer init failed: %d\n", ret);
		goto fail;
	}

	DBG("create: FB ID: %d (%p)", fb->base.id, fb);

	return fb;

fail:
	kfree(omap_fb);

	return ERR_PTR(ret);
}
