/* exynos_drm_crtc.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Authors:
 *	Inki Dae <inki.dae@samsung.com>
 *	Joonyoung Shim <jy0922.shim@samsung.com>
 *	Seung-Woo Kim <sw0312.kim@samsung.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "drmP.h"
#include "drm_crtc_helper.h"

#include "exynos_drm_drv.h"
#include "exynos_drm_fb.h"
#include "exynos_drm_encoder.h"

#define to_exynos_crtc(x)	container_of(x, struct exynos_drm_crtc,\
				drm_crtc)

/*
 * @fb_x: horizontal position from framebuffer base
 * @fb_y: vertical position from framebuffer base
 * @base_x: horizontal position from screen base
 * @base_y: vertical position from screen base
 * @crtc_w: width of crtc
 * @crtc_h: height of crtc
 */
struct exynos_drm_crtc_pos {
	unsigned int fb_x;
	unsigned int fb_y;
	unsigned int base_x;
	unsigned int base_y;
	unsigned int crtc_w;
	unsigned int crtc_h;
};

/*
 * Exynos specific crtc structure.
 *
 * @drm_crtc: crtc object.
 * @overlay: contain information common to display controller and hdmi and
 *	contents of this overlay object would be copied to sub driver size.
 * @pipe: a crtc index created at load() with a new crtc object creation
 *	and the crtc object would be set to private->crtc array
 *	to get a crtc object corresponding to this pipe from private->crtc
 *	array when irq interrupt occured. the reason of using this pipe is that
 *	drm framework doesn't support multiple irq yet.
 *	we can refer to the crtc to current hardware interrupt occured through
 *	this pipe value.
 */
struct exynos_drm_crtc {
	struct drm_crtc			drm_crtc;
	struct exynos_drm_overlay	overlay;
	unsigned int			pipe;
};

void exynos_drm_crtc_apply(struct drm_crtc *crtc)
{
	struct exynos_drm_crtc *exynos_crtc = to_exynos_crtc(crtc);
	struct exynos_drm_overlay *overlay = &exynos_crtc->overlay;

	exynos_drm_fn_encoder(crtc, overlay,
			exynos_drm_encoder_crtc_mode_set);
	exynos_drm_fn_encoder(crtc, NULL, exynos_drm_encoder_crtc_commit);
}

static void exynos_drm_overlay_update(struct exynos_drm_overlay *overlay,
				       struct drm_framebuffer *fb,
				       struct drm_display_mode *mode,
				       struct exynos_drm_crtc_pos *pos)
{
	struct exynos_drm_buffer_info buffer_info;
	unsigned int actual_w = pos->crtc_w;
	unsigned int actual_h = pos->crtc_h;
	unsigned int hw_w;
	unsigned int hw_h;

	/* update buffer address of framebuffer. */
	exynos_drm_fb_update_buf_off(fb, pos->fb_x, pos->fb_y, &buffer_info);
	overlay->paddr = buffer_info.paddr;
	overlay->vaddr = buffer_info.vaddr;

	hw_w = mode->hdisplay - pos->base_x;
	hw_h = mode->vdisplay - pos->base_y;

	if (actual_w > hw_w)
		actual_w = hw_w;
	if (actual_h > hw_h)
		actual_h = hw_h;

	overlay->offset_x = pos->base_x;
	overlay->offset_y = pos->base_y;
	overlay->width = actual_w;
	overlay->height = actual_h;
	overlay->bpp = fb->bits_per_pixel;

	DRM_DEBUG_KMS("overlay : offset_x/y(%d,%d), width/height(%d,%d)",
			overlay->offset_x, overlay->offset_y,
			overlay->width, overlay->height);

	overlay->buf_offsize = fb->width - actual_w;
	overlay->line_size = actual_w;
}

static int exynos_drm_crtc_update(struct drm_crtc *crtc)
{
	struct exynos_drm_crtc *exynos_crtc;
	struct exynos_drm_overlay *overlay;
	struct exynos_drm_crtc_pos pos;
	struct drm_display_mode *mode = &crtc->mode;
	struct drm_framebuffer *fb = crtc->fb;

	if (!mode || !fb)
		return -EINVAL;

	exynos_crtc = to_exynos_crtc(crtc);
	overlay = &exynos_crtc->overlay;

	memset(&pos, 0, sizeof(struct exynos_drm_crtc_pos));
	pos.fb_x = crtc->x;
	pos.fb_y = crtc->y;
	pos.crtc_w = fb->width - crtc->x;
	pos.crtc_h = fb->height - crtc->y;

	exynos_drm_overlay_update(overlay, crtc->fb, mode, &pos);

	return 0;
}

static void exynos_drm_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	/* TODO */
}

static void exynos_drm_crtc_prepare(struct drm_crtc *crtc)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	/* drm framework doesn't check NULL. */
}

static void exynos_drm_crtc_commit(struct drm_crtc *crtc)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	/* drm framework doesn't check NULL. */
}

static bool
exynos_drm_crtc_mode_fixup(struct drm_crtc *crtc,
			    struct drm_display_mode *mode,
			    struct drm_display_mode *adjusted_mode)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	/* drm framework doesn't check NULL */
	return true;
}

static int
exynos_drm_crtc_mode_set(struct drm_crtc *crtc, struct drm_display_mode *mode,
			  struct drm_display_mode *adjusted_mode, int x, int y,
			  struct drm_framebuffer *old_fb)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	mode = adjusted_mode;

	return exynos_drm_crtc_update(crtc);
}

static int exynos_drm_crtc_mode_set_base(struct drm_crtc *crtc, int x, int y,
					  struct drm_framebuffer *old_fb)
{
	int ret;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	ret = exynos_drm_crtc_update(crtc);
	if (ret)
		return ret;

	exynos_drm_crtc_apply(crtc);

	return ret;
}

static void exynos_drm_crtc_load_lut(struct drm_crtc *crtc)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);
	/* drm framework doesn't check NULL */
}

static struct drm_crtc_helper_funcs exynos_crtc_helper_funcs = {
	.dpms		= exynos_drm_crtc_dpms,
	.prepare	= exynos_drm_crtc_prepare,
	.commit		= exynos_drm_crtc_commit,
	.mode_fixup	= exynos_drm_crtc_mode_fixup,
	.mode_set	= exynos_drm_crtc_mode_set,
	.mode_set_base	= exynos_drm_crtc_mode_set_base,
	.load_lut	= exynos_drm_crtc_load_lut,
};

static int exynos_drm_crtc_page_flip(struct drm_crtc *crtc,
				      struct drm_framebuffer *fb,
				      struct drm_pending_vblank_event *event)
{
	struct drm_device *dev = crtc->dev;
	struct exynos_drm_private *dev_priv = dev->dev_private;
	struct exynos_drm_crtc *exynos_crtc = to_exynos_crtc(crtc);
	struct drm_framebuffer *old_fb = crtc->fb;
	int ret = -EINVAL;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	mutex_lock(&dev->struct_mutex);

	if (event && !dev_priv->pageflip_event) {
		list_add_tail(&event->base.link,
				&dev_priv->pageflip_event_list);

		ret = drm_vblank_get(dev, exynos_crtc->pipe);
		if (ret) {
			DRM_DEBUG("failed to acquire vblank counter\n");
			goto out;
		}

		crtc->fb = fb;
		ret = exynos_drm_crtc_update(crtc);
		if (ret) {
			crtc->fb = old_fb;
			drm_vblank_put(dev, exynos_crtc->pipe);
			dev_priv->pageflip_event = false;

			goto out;
		}

		dev_priv->pageflip_event = true;
	}
out:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

static void exynos_drm_crtc_destroy(struct drm_crtc *crtc)
{
	struct exynos_drm_crtc *exynos_crtc = to_exynos_crtc(crtc);
	struct exynos_drm_private *private = crtc->dev->dev_private;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	private->crtc[exynos_crtc->pipe] = NULL;

	drm_crtc_cleanup(crtc);
	kfree(exynos_crtc);
}

static struct drm_crtc_funcs exynos_crtc_funcs = {
	.set_config	= drm_crtc_helper_set_config,
	.page_flip	= exynos_drm_crtc_page_flip,
	.destroy	= exynos_drm_crtc_destroy,
};

struct exynos_drm_overlay *get_exynos_drm_overlay(struct drm_device *dev,
		struct drm_crtc *crtc)
{
	struct exynos_drm_crtc *exynos_crtc = to_exynos_crtc(crtc);

	return &exynos_crtc->overlay;
}

int exynos_drm_crtc_create(struct drm_device *dev, unsigned int nr)
{
	struct exynos_drm_crtc *exynos_crtc;
	struct exynos_drm_private *private = dev->dev_private;
	struct drm_crtc *crtc;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	exynos_crtc = kzalloc(sizeof(*exynos_crtc), GFP_KERNEL);
	if (!exynos_crtc) {
		DRM_ERROR("failed to allocate exynos crtc\n");
		return -ENOMEM;
	}

	exynos_crtc->pipe = nr;
	crtc = &exynos_crtc->drm_crtc;

	private->crtc[nr] = crtc;

	drm_crtc_init(dev, crtc, &exynos_crtc_funcs);
	drm_crtc_helper_add(crtc, &exynos_crtc_helper_funcs);

	return 0;
}

int exynos_drm_crtc_enable_vblank(struct drm_device *dev, int crtc)
{
	struct exynos_drm_private *private = dev->dev_private;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	exynos_drm_fn_encoder(private->crtc[crtc], &crtc,
			exynos_drm_enable_vblank);

	return 0;
}

void exynos_drm_crtc_disable_vblank(struct drm_device *dev, int crtc)
{
	struct exynos_drm_private *private = dev->dev_private;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	exynos_drm_fn_encoder(private->crtc[crtc], &crtc,
			exynos_drm_disable_vblank);
}

MODULE_AUTHOR("Inki Dae <inki.dae@samsung.com>");
MODULE_AUTHOR("Joonyoung Shim <jy0922.shim@samsung.com>");
MODULE_AUTHOR("Seung-Woo Kim <sw0312.kim@samsung.com>");
MODULE_DESCRIPTION("Samsung SoC DRM CRTC Driver");
MODULE_LICENSE("GPL");
