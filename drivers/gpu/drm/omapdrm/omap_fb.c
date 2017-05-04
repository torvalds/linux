/*
 * drivers/gpu/drm/omapdrm/omap_fb.c
 *
 * Copyright (C) 2011 Texas Instruments
 * Author: Rob Clark <rob@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/seq_file.h>

#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>

#include "omap_dmm_tiler.h"
#include "omap_drv.h"

/*
 * framebuffer funcs
 */

/* DSS to DRM formats mapping */
static const struct {
	enum omap_color_mode dss_format;
	uint32_t pixel_format;
} formats[] = {
	/* 16bpp [A]RGB: */
	{ OMAP_DSS_COLOR_RGB16,       DRM_FORMAT_RGB565 },   /* RGB16-565 */
	{ OMAP_DSS_COLOR_RGB12U,      DRM_FORMAT_RGBX4444 }, /* RGB12x-4444 */
	{ OMAP_DSS_COLOR_RGBX16,      DRM_FORMAT_XRGB4444 }, /* xRGB12-4444 */
	{ OMAP_DSS_COLOR_RGBA16,      DRM_FORMAT_RGBA4444 }, /* RGBA12-4444 */
	{ OMAP_DSS_COLOR_ARGB16,      DRM_FORMAT_ARGB4444 }, /* ARGB16-4444 */
	{ OMAP_DSS_COLOR_XRGB16_1555, DRM_FORMAT_XRGB1555 }, /* xRGB15-1555 */
	{ OMAP_DSS_COLOR_ARGB16_1555, DRM_FORMAT_ARGB1555 }, /* ARGB16-1555 */
	/* 24bpp RGB: */
	{ OMAP_DSS_COLOR_RGB24P,      DRM_FORMAT_RGB888 },   /* RGB24-888 */
	/* 32bpp [A]RGB: */
	{ OMAP_DSS_COLOR_RGBX32,      DRM_FORMAT_RGBX8888 }, /* RGBx24-8888 */
	{ OMAP_DSS_COLOR_RGB24U,      DRM_FORMAT_XRGB8888 }, /* xRGB24-8888 */
	{ OMAP_DSS_COLOR_RGBA32,      DRM_FORMAT_RGBA8888 }, /* RGBA32-8888 */
	{ OMAP_DSS_COLOR_ARGB32,      DRM_FORMAT_ARGB8888 }, /* ARGB32-8888 */
	/* YUV: */
	{ OMAP_DSS_COLOR_NV12,        DRM_FORMAT_NV12 },
	{ OMAP_DSS_COLOR_YUV2,        DRM_FORMAT_YUYV },
	{ OMAP_DSS_COLOR_UYVY,        DRM_FORMAT_UYVY },
};

/* convert from overlay's pixel formats bitmask to an array of fourcc's */
uint32_t omap_framebuffer_get_formats(uint32_t *pixel_formats,
		uint32_t max_formats, const enum omap_color_mode *supported_modes)
{
	uint32_t nformats = 0;
	int i = 0;

	for (i = 0; i < ARRAY_SIZE(formats) && nformats < max_formats; i++) {
		unsigned int t;

		for (t = 0; supported_modes[t]; ++t) {
			if (supported_modes[t] != formats[i].dss_format)
				continue;

			pixel_formats[nformats++] = formats[i].pixel_format;
			break;
		}
	}

	return nformats;
}

/* per-plane info for the fb: */
struct plane {
	struct drm_gem_object *bo;
	uint32_t pitch;
	uint32_t offset;
	dma_addr_t dma_addr;
};

#define to_omap_framebuffer(x) container_of(x, struct omap_framebuffer, base)

struct omap_framebuffer {
	struct drm_framebuffer base;
	int pin_count;
	const struct drm_format_info *format;
	enum omap_color_mode dss_format;
	struct plane planes[2];
	/* lock for pinning (pin_count and planes.dma_addr) */
	struct mutex lock;
};

static int omap_framebuffer_create_handle(struct drm_framebuffer *fb,
		struct drm_file *file_priv,
		unsigned int *handle)
{
	struct omap_framebuffer *omap_fb = to_omap_framebuffer(fb);
	return drm_gem_handle_create(file_priv,
			omap_fb->planes[0].bo, handle);
}

static void omap_framebuffer_destroy(struct drm_framebuffer *fb)
{
	struct omap_framebuffer *omap_fb = to_omap_framebuffer(fb);
	int i, n = fb->format->num_planes;

	DBG("destroy: FB ID: %d (%p)", fb->base.id, fb);

	drm_framebuffer_cleanup(fb);

	for (i = 0; i < n; i++) {
		struct plane *plane = &omap_fb->planes[i];

		drm_gem_object_unreference_unlocked(plane->bo);
	}

	kfree(omap_fb);
}

static const struct drm_framebuffer_funcs omap_framebuffer_funcs = {
	.create_handle = omap_framebuffer_create_handle,
	.destroy = omap_framebuffer_destroy,
};

static uint32_t get_linear_addr(struct plane *plane,
		const struct drm_format_info *format, int n, int x, int y)
{
	uint32_t offset;

	offset = plane->offset
	       + (x * format->cpp[n] / (n == 0 ? 1 : format->hsub))
	       + (y * plane->pitch / (n == 0 ? 1 : format->vsub));

	return plane->dma_addr + offset;
}

bool omap_framebuffer_supports_rotation(struct drm_framebuffer *fb)
{
	struct omap_framebuffer *omap_fb = to_omap_framebuffer(fb);
	struct plane *plane = &omap_fb->planes[0];

	return omap_gem_flags(plane->bo) & OMAP_BO_TILED;
}

/* update ovl info for scanout, handles cases of multi-planar fb's, etc.
 */
void omap_framebuffer_update_scanout(struct drm_framebuffer *fb,
		struct omap_drm_window *win, struct omap_overlay_info *info)
{
	struct omap_framebuffer *omap_fb = to_omap_framebuffer(fb);
	const struct drm_format_info *format = omap_fb->format;
	struct plane *plane = &omap_fb->planes[0];
	uint32_t x, y, orient = 0;

	info->color_mode = omap_fb->dss_format;

	info->pos_x      = win->crtc_x;
	info->pos_y      = win->crtc_y;
	info->out_width  = win->crtc_w;
	info->out_height = win->crtc_h;
	info->width      = win->src_w;
	info->height     = win->src_h;

	x = win->src_x;
	y = win->src_y;

	if (omap_gem_flags(plane->bo) & OMAP_BO_TILED) {
		uint32_t w = win->src_w;
		uint32_t h = win->src_h;

		switch (win->rotation & DRM_MODE_ROTATE_MASK) {
		default:
			dev_err(fb->dev->dev, "invalid rotation: %02x",
					(uint32_t)win->rotation);
			/* fallthru to default to no rotation */
		case 0:
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

		if (win->rotation & DRM_MODE_REFLECT_X)
			orient ^= MASK_X_INVERT;

		if (win->rotation & DRM_MODE_REFLECT_Y)
			orient ^= MASK_Y_INVERT;

		/* adjust x,y offset for flip/invert: */
		if (orient & MASK_XY_FLIP)
			swap(w, h);
		if (orient & MASK_Y_INVERT)
			y += h - 1;
		if (orient & MASK_X_INVERT)
			x += w - 1;

		omap_gem_rotated_dma_addr(plane->bo, orient, x, y,
					  &info->paddr);
		info->rotation_type = OMAP_DSS_ROT_TILER;
		info->screen_width  = omap_gem_tiled_stride(plane->bo, orient);
	} else {
		switch (win->rotation & DRM_MODE_ROTATE_MASK) {
		case 0:
		case DRM_MODE_ROTATE_0:
			/* OK */
			break;

		default:
			dev_warn(fb->dev->dev,
				"rotation '%d' ignored for non-tiled fb\n",
				win->rotation);
			win->rotation = 0;
			break;
		}

		info->paddr         = get_linear_addr(plane, format, 0, x, y);
		info->rotation_type = OMAP_DSS_ROT_NONE;
		info->screen_width  = plane->pitch;
	}

	/* convert to pixels: */
	info->screen_width /= format->cpp[0];

	if (omap_fb->dss_format == OMAP_DSS_COLOR_NV12) {
		plane = &omap_fb->planes[1];

		if (info->rotation_type == OMAP_DSS_ROT_TILER) {
			WARN_ON(!(omap_gem_flags(plane->bo) & OMAP_BO_TILED));
			omap_gem_rotated_dma_addr(plane->bo, orient, x/2, y/2,
						  &info->p_uv_addr);
		} else {
			info->p_uv_addr = get_linear_addr(plane, format, 1, x, y);
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
		ret = omap_gem_pin(plane->bo, &plane->dma_addr);
		if (ret)
			goto fail;
		omap_gem_dma_sync_buffer(plane->bo, DMA_TO_DEVICE);
	}

	omap_fb->pin_count++;

	mutex_unlock(&omap_fb->lock);

	return 0;

fail:
	for (i--; i >= 0; i--) {
		struct plane *plane = &omap_fb->planes[i];
		omap_gem_unpin(plane->bo);
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
		omap_gem_unpin(plane->bo);
		plane->dma_addr = 0;
	}

	mutex_unlock(&omap_fb->lock);
}

/* iterate thru all the connectors, returning ones that are attached
 * to the same fb..
 */
struct drm_connector *omap_framebuffer_get_next_connector(
		struct drm_framebuffer *fb, struct drm_connector *from)
{
	struct drm_device *dev = fb->dev;
	struct list_head *connector_list = &dev->mode_config.connector_list;
	struct drm_connector *connector = from;

	if (!from)
		return list_first_entry_or_null(connector_list, typeof(*from),
						head);

	list_for_each_entry_from(connector, connector_list, head) {
		if (connector != from) {
			struct drm_encoder *encoder = connector->encoder;
			struct drm_crtc *crtc = encoder ? encoder->crtc : NULL;
			if (crtc && crtc->primary->fb == fb)
				return connector;

		}
	}

	return NULL;
}

#ifdef CONFIG_DEBUG_FS
void omap_framebuffer_describe(struct drm_framebuffer *fb, struct seq_file *m)
{
	struct omap_framebuffer *omap_fb = to_omap_framebuffer(fb);
	int i, n = fb->format->num_planes;

	seq_printf(m, "fb: %dx%d@%4.4s\n", fb->width, fb->height,
			(char *)&fb->format->format);

	for (i = 0; i < n; i++) {
		struct plane *plane = &omap_fb->planes[i];
		seq_printf(m, "   %d: offset=%d pitch=%d, obj: ",
				i, plane->offset, plane->pitch);
		omap_gem_describe(plane->bo, m);
	}
}
#endif

struct drm_framebuffer *omap_framebuffer_create(struct drm_device *dev,
		struct drm_file *file, const struct drm_mode_fb_cmd2 *mode_cmd)
{
	unsigned int num_planes = drm_format_num_planes(mode_cmd->pixel_format);
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
	while (--i > 0)
		drm_gem_object_unreference_unlocked(bos[i]);

	return fb;
}

struct drm_framebuffer *omap_framebuffer_init(struct drm_device *dev,
		const struct drm_mode_fb_cmd2 *mode_cmd, struct drm_gem_object **bos)
{
	const struct drm_format_info *format = NULL;
	struct omap_framebuffer *omap_fb = NULL;
	struct drm_framebuffer *fb = NULL;
	enum omap_color_mode dss_format = 0;
	unsigned int pitch = mode_cmd->pitches[0];
	int ret, i;

	DBG("create framebuffer: dev=%p, mode_cmd=%p (%dx%d@%4.4s)",
			dev, mode_cmd, mode_cmd->width, mode_cmd->height,
			(char *)&mode_cmd->pixel_format);

	format = drm_format_info(mode_cmd->pixel_format);

	for (i = 0; i < ARRAY_SIZE(formats); i++) {
		if (formats[i].pixel_format == mode_cmd->pixel_format) {
			dss_format = formats[i].dss_format;
			break;
		}
	}

	if (!format || !dss_format) {
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
	omap_fb->dss_format = dss_format;
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

		plane->bo     = bos[i];
		plane->offset = mode_cmd->offsets[i];
		plane->pitch  = pitch;
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
