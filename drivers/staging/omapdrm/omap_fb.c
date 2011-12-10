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

#include "drm_crtc.h"
#include "drm_crtc_helper.h"


/*
 * framebuffer funcs
 */

#define to_omap_framebuffer(x) container_of(x, struct omap_framebuffer, base)

struct omap_framebuffer {
	struct drm_framebuffer base;
	struct drm_gem_object *bo;
	int size;
	dma_addr_t paddr;
};

static int omap_framebuffer_create_handle(struct drm_framebuffer *fb,
		struct drm_file *file_priv,
		unsigned int *handle)
{
	struct omap_framebuffer *omap_fb = to_omap_framebuffer(fb);
    return drm_gem_handle_create(file_priv, omap_fb->bo, handle);
}

static void omap_framebuffer_destroy(struct drm_framebuffer *fb)
{
	struct drm_device *dev = fb->dev;
	struct omap_framebuffer *omap_fb = to_omap_framebuffer(fb);

	DBG("destroy: FB ID: %d (%p)", fb->base.id, fb);

	drm_framebuffer_cleanup(fb);

	if (omap_fb->bo) {
		if (omap_fb->paddr && omap_gem_put_paddr(omap_fb->bo))
			dev_err(dev->dev, "could not unmap!\n");
		drm_gem_object_unreference_unlocked(omap_fb->bo);
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

/* returns the buffer size */
int omap_framebuffer_get_buffer(struct drm_framebuffer *fb, int x, int y,
		void **vaddr, dma_addr_t *paddr, unsigned int *screen_width)
{
	struct omap_framebuffer *omap_fb = to_omap_framebuffer(fb);
	int bpp = fb->bits_per_pixel / 8;
	unsigned long offset;

	offset = (x * bpp) + (y * fb->pitch);

	if (vaddr) {
		void *bo_vaddr = omap_gem_vaddr(omap_fb->bo);
		/* note: we can only count on having a vaddr for buffers that
		 * are allocated physically contiguously to begin with (ie.
		 * dma_alloc_coherent()).  But this should be ok because it
		 * is only used by legacy fbdev
		 */
		BUG_ON(IS_ERR_OR_NULL(bo_vaddr));
		*vaddr = bo_vaddr + offset;
	}

	*paddr = omap_fb->paddr + offset;
	*screen_width = fb->pitch / bpp;

	return omap_fb->size - offset;
}

struct drm_gem_object *omap_framebuffer_bo(struct drm_framebuffer *fb)
{
	struct omap_framebuffer *omap_fb = to_omap_framebuffer(fb);
	return omap_fb->bo;
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

struct drm_framebuffer *omap_framebuffer_create(struct drm_device *dev,
		struct drm_file *file, struct drm_mode_fb_cmd *mode_cmd)
{
	struct drm_gem_object *bo;
	struct drm_framebuffer *fb;
	bo = drm_gem_object_lookup(dev, file, mode_cmd->handle);
	if (!bo) {
		return ERR_PTR(-ENOENT);
	}
	fb = omap_framebuffer_init(dev, mode_cmd, bo);
	if (!fb) {
		return ERR_PTR(-ENOMEM);
	}
	return fb;
}

struct drm_framebuffer *omap_framebuffer_init(struct drm_device *dev,
		struct drm_mode_fb_cmd *mode_cmd, struct drm_gem_object *bo)
{
	struct omap_framebuffer *omap_fb;
	struct drm_framebuffer *fb = NULL;
	int size, ret;

	DBG("create framebuffer: dev=%p, mode_cmd=%p (%dx%d@%d)",
			dev, mode_cmd, mode_cmd->width, mode_cmd->height,
			mode_cmd->bpp);

	/* in case someone tries to feed us a completely bogus stride: */
	mode_cmd->pitch = align_pitch(mode_cmd->pitch,
			mode_cmd->width, mode_cmd->bpp);

	omap_fb = kzalloc(sizeof(*omap_fb), GFP_KERNEL);
	if (!omap_fb) {
		dev_err(dev->dev, "could not allocate fb\n");
		goto fail;
	}

	fb = &omap_fb->base;
	ret = drm_framebuffer_init(dev, fb, &omap_framebuffer_funcs);
	if (ret) {
		dev_err(dev->dev, "framebuffer init failed: %d\n", ret);
		goto fail;
	}

	DBG("create: FB ID: %d (%p)", fb->base.id, fb);

	size = PAGE_ALIGN(mode_cmd->pitch * mode_cmd->height);

	if (size > bo->size) {
		dev_err(dev->dev, "provided buffer object is too small!\n");
		goto fail;
	}

	omap_fb->bo = bo;
	omap_fb->size = size;

	if (omap_gem_get_paddr(bo, &omap_fb->paddr, true)) {
		dev_err(dev->dev, "could not map (paddr)!\n");
		goto fail;
	}

	drm_helper_mode_fill_fb_struct(fb, mode_cmd);

	return fb;

fail:
	if (fb) {
		omap_framebuffer_destroy(fb);
	}
	return NULL;
}
