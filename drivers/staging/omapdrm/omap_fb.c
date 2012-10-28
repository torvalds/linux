/*
 * drivers/staging/omapdrm/omap_fb.c
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

#include "omap_drv.h"
#include "omap_dmm_tiler.h"

#include "drm_crtc.h"
#include "drm_crtc_helper.h"

/*
 * framebuffer funcs
 */

/* per-format info: */
struct format {
	enum omap_color_mode dss_format;
	uint32_t pixel_format;
	struct {
		int stride_bpp;           /* this times width is stride */
		int sub_y;                /* sub-sample in y dimension */
	} planes[4];
	bool yuv;
};

static const struct format formats[] = {
	/* 16bpp [A]RGB: */
	{ OMAP_DSS_COLOR_RGB16,       DRM_FORMAT_RGB565,   {{2, 1}}, false }, /* RGB16-565 */
	{ OMAP_DSS_COLOR_RGB12U,      DRM_FORMAT_RGBX4444, {{2, 1}}, false }, /* RGB12x-4444 */
	{ OMAP_DSS_COLOR_RGBX16,      DRM_FORMAT_XRGB4444, {{2, 1}}, false }, /* xRGB12-4444 */
	{ OMAP_DSS_COLOR_RGBA16,      DRM_FORMAT_RGBA4444, {{2, 1}}, false }, /* RGBA12-4444 */
	{ OMAP_DSS_COLOR_ARGB16,      DRM_FORMAT_ARGB4444, {{2, 1}}, false }, /* ARGB16-4444 */
	{ OMAP_DSS_COLOR_XRGB16_1555, DRM_FORMAT_XRGB1555, {{2, 1}}, false }, /* xRGB15-1555 */
	{ OMAP_DSS_COLOR_ARGB16_1555, DRM_FORMAT_ARGB1555, {{2, 1}}, false }, /* ARGB16-1555 */
	/* 24bpp RGB: */
	{ OMAP_DSS_COLOR_RGB24P,      DRM_FORMAT_RGB888,   {{3, 1}}, false }, /* RGB24-888 */
	/* 32bpp [A]RGB: */
	{ OMAP_DSS_COLOR_RGBX32,      DRM_FORMAT_RGBX8888, {{4, 1}}, false }, /* RGBx24-8888 */
	{ OMAP_DSS_COLOR_RGB24U,      DRM_FORMAT_XRGB8888, {{4, 1}}, false }, /* xRGB24-8888 */
	{ OMAP_DSS_COLOR_RGBA32,      DRM_FORMAT_RGBA8888, {{4, 1}}, false }, /* RGBA32-8888 */
	{ OMAP_DSS_COLOR_ARGB32,      DRM_FORMAT_ARGB8888, {{4, 1}}, false }, /* ARGB32-8888 */
	/* YUV: */
	{ OMAP_DSS_COLOR_NV12,        DRM_FORMAT_NV12,     {{1, 1}, {1, 2}}, true },
	{ OMAP_DSS_COLOR_YUV2,        DRM_FORMAT_YUYV,     {{2, 1}}, true },
	{ OMAP_DSS_COLOR_UYVY,        DRM_FORMAT_UYVY,     {{2, 1}}, true },
};

/* convert from overlay's pixel formats bitmask to an array of fourcc's */
uint32_t omap_framebuffer_get_formats(uint32_t *pixel_formats,
		uint32_t max_formats, enum omap_color_mode supported_modes)
{
	uint32_t nformats = 0;
	int i = 0;

	for (i = 0; i < ARRAY_SIZE(formats) && nformats < max_formats; i++)
		if (formats[i].dss_format & supported_modes)
			pixel_formats[nformats++] = formats[i].pixel_format;

	return nformats;
}

/* per-plane info for the fb: */
struct plane {
	struct drm_gem_object *bo;
	uint32_t pitch;
	uint32_t offset;
	dma_addr_t paddr;
};

#define to_omap_framebuffer(x) container_of(x, struct omap_framebuffer, base)

struct omap_framebuffer {
	struct drm_framebuffer base;
	const struct format *format;
	struct plane planes[4];
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
	int i, n = drm_format_num_planes(fb->pixel_format);

	DBG("destroy: FB ID: %d (%p)", fb->base.id, fb);

	drm_framebuffer_cleanup(fb);

	for (i = 0; i < n; i++) {
		struct plane *plane = &omap_fb->planes[i];
		if (plane->bo)
			drm_gem_object_unreference_unlocked(plane->bo);
	}

	kfree(omap_fb);
}

static int omap_framebuffer_dirty(struct drm_framebuffer *fb,
		struct drm_file *file_priv, unsigned flags, unsigned color,
		struct drm_clip_rect *clips, unsigned num_clips)
{
	int i;

	for (i = 0; i < num_clips; i++) {
		omap_framebuffer_flush(fb, clips[i].x1, clips[i].y1,
					clips[i].x2 - clips[i].x1,
					clips[i].y2 - clips[i].y1);
	}

	return 0;
}

static const struct drm_framebuffer_funcs omap_framebuffer_funcs = {
	.create_handle = omap_framebuffer_create_handle,
	.destroy = omap_framebuffer_destroy,
	.dirty = omap_framebuffer_dirty,
};

static uint32_t get_linear_addr(struct plane *plane,
		const struct format *format, int n, int x, int y)
{
	uint32_t offset;

	offset = plane->offset +
			(x * format->planes[n].stride_bpp) +
			(y * plane->pitch / format->planes[n].sub_y);

	return plane->paddr + offset;
}

/* update ovl info for scanout, handles cases of multi-planar fb's, etc.
 */
void omap_framebuffer_update_scanout(struct drm_framebuffer *fb,
		struct omap_drm_window *win, struct omap_overlay_info *info)
{
	struct omap_framebuffer *omap_fb = to_omap_framebuffer(fb);
	const struct format *format = omap_fb->format;
	struct plane *plane = &omap_fb->planes[0];
	uint32_t x, y, orient = 0;

	info->color_mode = format->dss_format;

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

		switch (win->rotation & 0xf) {
		default:
			dev_err(fb->dev->dev, "invalid rotation: %02x",
					(uint32_t)win->rotation);
			/* fallthru to default to no rotation */
		case 0:
		case BIT(DRM_ROTATE_0):
			orient = 0;
			break;
		case BIT(DRM_ROTATE_90):
			orient = MASK_XY_FLIP | MASK_X_INVERT;
			break;
		case BIT(DRM_ROTATE_180):
			orient = MASK_X_INVERT | MASK_Y_INVERT;
			break;
		case BIT(DRM_ROTATE_270):
			orient = MASK_XY_FLIP | MASK_Y_INVERT;
			break;
		}

		if (win->rotation & BIT(DRM_REFLECT_X))
			orient ^= MASK_X_INVERT;

		if (win->rotation & BIT(DRM_REFLECT_Y))
			orient ^= MASK_Y_INVERT;

		/* adjust x,y offset for flip/invert: */
		if (orient & MASK_XY_FLIP)
			swap(w, h);
		if (orient & MASK_Y_INVERT)
			y += h - 1;
		if (orient & MASK_X_INVERT)
			x += w - 1;

		omap_gem_rotated_paddr(plane->bo, orient, x, y, &info->paddr);
		info->rotation_type = OMAP_DSS_ROT_TILER;
		info->screen_width  = omap_gem_tiled_stride(plane->bo, orient);
	} else {
		info->paddr         = get_linear_addr(plane, format, 0, x, y);
		info->rotation_type = OMAP_DSS_ROT_DMA;
		info->screen_width  = plane->pitch;
	}

	/* convert to pixels: */
	info->screen_width /= format->planes[0].stride_bpp;

	if (format->dss_format == OMAP_DSS_COLOR_NV12) {
		plane = &omap_fb->planes[1];

		if (info->rotation_type == OMAP_DSS_ROT_TILER) {
			WARN_ON(!(omap_gem_flags(plane->bo) & OMAP_BO_TILED));
			omap_gem_rotated_paddr(plane->bo, orient,
					x/2, y/2, &info->p_uv_addr);
		} else {
			info->p_uv_addr = get_linear_addr(plane, format, 1, x, y);
		}
	} else {
		info->p_uv_addr = 0;
	}
}

/* Call for unpin 'a' (if not NULL), and pin 'b' (if not NULL).  Although
 * buffers to unpin are just pushed to the unpin fifo so that the
 * caller can defer unpin until vblank.
 *
 * Note if this fails (ie. something went very wrong!), all buffers are
 * unpinned, and the caller disables the overlay.  We could have tried
 * to revert back to the previous set of pinned buffers but if things are
 * hosed there is no guarantee that would succeed.
 */
int omap_framebuffer_replace(struct drm_framebuffer *a,
		struct drm_framebuffer *b, void *arg,
		void (*unpin)(void *arg, struct drm_gem_object *bo))
{
	int ret = 0, i, na, nb;
	struct omap_framebuffer *ofba = to_omap_framebuffer(a);
	struct omap_framebuffer *ofbb = to_omap_framebuffer(b);

	na = a ? drm_format_num_planes(a->pixel_format) : 0;
	nb = b ? drm_format_num_planes(b->pixel_format) : 0;

	for (i = 0; i < max(na, nb); i++) {
		struct plane *pa, *pb;

		pa = (i < na) ? &ofba->planes[i] : NULL;
		pb = (i < nb) ? &ofbb->planes[i] : NULL;

		if (pa) {
			unpin(arg, pa->bo);
			pa->paddr = 0;
		}

		if (pb && !ret) {
			ret = omap_gem_get_paddr(pb->bo, &pb->paddr, true);
			if (!ret)
				omap_gem_dma_sync(pb->bo, DMA_TO_DEVICE);
		}
	}

	if (ret) {
		/* something went wrong.. unpin what has been pinned */
		for (i = 0; i < nb; i++) {
			struct plane *pb = &ofba->planes[i];
			if (pb->paddr) {
				unpin(arg, pb->bo);
				pb->paddr = 0;
			}
		}
	}

	return ret;
}

struct drm_gem_object *omap_framebuffer_bo(struct drm_framebuffer *fb, int p)
{
	struct omap_framebuffer *omap_fb = to_omap_framebuffer(fb);
	if (p >= drm_format_num_planes(fb->pixel_format))
		return NULL;
	return omap_fb->planes[p].bo;
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

	if (!from) {
		return list_first_entry(connector_list, typeof(*from), head);
	}

	list_for_each_entry_from(connector, connector_list, head) {
		if (connector != from) {
			struct drm_encoder *encoder = connector->encoder;
			struct drm_crtc *crtc = encoder ? encoder->crtc : NULL;
			if (crtc && crtc->fb == fb) {
				return connector;
			}
		}
	}

	return NULL;
}

/* flush an area of the framebuffer (in case of manual update display that
 * is not automatically flushed)
 */
void omap_framebuffer_flush(struct drm_framebuffer *fb,
		int x, int y, int w, int h)
{
	struct drm_connector *connector = NULL;

	VERB("flush: %d,%d %dx%d, fb=%p", x, y, w, h, fb);

	while ((connector = omap_framebuffer_get_next_connector(fb, connector))) {
		/* only consider connectors that are part of a chain */
		if (connector->encoder && connector->encoder->crtc) {
			/* TODO: maybe this should propagate thru the crtc who
			 * could do the coordinate translation..
			 */
			struct drm_crtc *crtc = connector->encoder->crtc;
			int cx = max(0, x - crtc->x);
			int cy = max(0, y - crtc->y);
			int cw = w + (x - crtc->x) - cx;
			int ch = h + (y - crtc->y) - cy;

			omap_connector_flush(connector, cx, cy, cw, ch);
		}
	}
}

#ifdef CONFIG_DEBUG_FS
void omap_framebuffer_describe(struct drm_framebuffer *fb, struct seq_file *m)
{
	struct omap_framebuffer *omap_fb = to_omap_framebuffer(fb);
	int i, n = drm_format_num_planes(fb->pixel_format);

	seq_printf(m, "fb: %dx%d@%4.4s\n", fb->width, fb->height,
			(char *)&fb->pixel_format);

	for (i = 0; i < n; i++) {
		struct plane *plane = &omap_fb->planes[i];
		seq_printf(m, "   %d: offset=%d pitch=%d, obj: ",
				i, plane->offset, plane->pitch);
		omap_gem_describe(plane->bo, m);
	}
}
#endif

struct drm_framebuffer *omap_framebuffer_create(struct drm_device *dev,
		struct drm_file *file, struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_gem_object *bos[4];
	struct drm_framebuffer *fb;
	int ret;

	ret = objects_lookup(dev, file, mode_cmd->pixel_format,
			bos, mode_cmd->handles);
	if (ret)
		return ERR_PTR(ret);

	fb = omap_framebuffer_init(dev, mode_cmd, bos);
	if (IS_ERR(fb)) {
		int i, n = drm_format_num_planes(mode_cmd->pixel_format);
		for (i = 0; i < n; i++)
			drm_gem_object_unreference_unlocked(bos[i]);
		return fb;
	}
	return fb;
}

struct drm_framebuffer *omap_framebuffer_init(struct drm_device *dev,
		struct drm_mode_fb_cmd2 *mode_cmd, struct drm_gem_object **bos)
{
	struct omap_framebuffer *omap_fb;
	struct drm_framebuffer *fb = NULL;
	const struct format *format = NULL;
	int ret, i, n = drm_format_num_planes(mode_cmd->pixel_format);

	DBG("create framebuffer: dev=%p, mode_cmd=%p (%dx%d@%4.4s)",
			dev, mode_cmd, mode_cmd->width, mode_cmd->height,
			(char *)&mode_cmd->pixel_format);

	for (i = 0; i < ARRAY_SIZE(formats); i++) {
		if (formats[i].pixel_format == mode_cmd->pixel_format) {
			format = &formats[i];
			break;
		}
	}

	if (!format) {
		dev_err(dev->dev, "unsupported pixel format: %4.4s\n",
				(char *)&mode_cmd->pixel_format);
		ret = -EINVAL;
		goto fail;
	}

	omap_fb = kzalloc(sizeof(*omap_fb), GFP_KERNEL);
	if (!omap_fb) {
		dev_err(dev->dev, "could not allocate fb\n");
		ret = -ENOMEM;
		goto fail;
	}

	fb = &omap_fb->base;
	ret = drm_framebuffer_init(dev, fb, &omap_framebuffer_funcs);
	if (ret) {
		dev_err(dev->dev, "framebuffer init failed: %d\n", ret);
		goto fail;
	}

	DBG("create: FB ID: %d (%p)", fb->base.id, fb);

	omap_fb->format = format;

	for (i = 0; i < n; i++) {
		struct plane *plane = &omap_fb->planes[i];
		int size, pitch = mode_cmd->pitches[i];

		if (pitch < (mode_cmd->width * format->planes[i].stride_bpp)) {
			dev_err(dev->dev, "provided buffer pitch is too small! %d < %d\n",
					pitch, mode_cmd->width * format->planes[i].stride_bpp);
			ret = -EINVAL;
			goto fail;
		}

		size = pitch * mode_cmd->height / format->planes[i].sub_y;

		if (size > (omap_gem_mmap_size(bos[i]) - mode_cmd->offsets[i])) {
			dev_err(dev->dev, "provided buffer object is too small! %d < %d\n",
					bos[i]->size - mode_cmd->offsets[i], size);
			ret = -EINVAL;
			goto fail;
		}

		plane->bo     = bos[i];
		plane->offset = mode_cmd->offsets[i];
		plane->pitch  = pitch;
		plane->paddr  = 0;
	}

	drm_helper_mode_fill_fb_struct(fb, mode_cmd);

	return fb;

fail:
	if (fb) {
		omap_framebuffer_destroy(fb);
	}
	return ERR_PTR(ret);
}
