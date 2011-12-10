/*
 * drivers/staging/omapdrm/omap_crtc.c
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

#include "drm_mode.h"
#include "drm_crtc.h"
#include "drm_crtc_helper.h"

#define to_omap_crtc(x) container_of(x, struct omap_crtc, base)

struct omap_crtc {
	struct drm_crtc base;
	struct omap_overlay *ovl;
	struct omap_overlay_info info;
	int id;

	/* if there is a pending flip, this will be non-null: */
	struct drm_pending_vblank_event *event;
};

/* push changes down to dss2 */
static int commit(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	struct omap_overlay *ovl = omap_crtc->ovl;
	struct omap_overlay_info *info = &omap_crtc->info;
	int ret;

	DBG("%s", omap_crtc->ovl->name);
	DBG("%dx%d -> %dx%d (%d)", info->width, info->height, info->out_width,
			info->out_height, info->screen_width);
	DBG("%d,%d %08x", info->pos_x, info->pos_y, info->paddr);

	/* NOTE: do we want to do this at all here, or just wait
	 * for dpms(ON) since other CRTC's may not have their mode
	 * set yet, so fb dimensions may still change..
	 */
	ret = ovl->set_overlay_info(ovl, info);
	if (ret) {
		dev_err(dev->dev, "could not set overlay info\n");
		return ret;
	}

	/* our encoder doesn't necessarily get a commit() after this, in
	 * particular in the dpms() and mode_set_base() cases, so force the
	 * manager to update:
	 *
	 * could this be in the encoder somehow?
	 */
	if (ovl->manager) {
		ret = ovl->manager->apply(ovl->manager);
		if (ret) {
			dev_err(dev->dev, "could not apply settings\n");
			return ret;
		}
	}

	if (info->enabled) {
		omap_framebuffer_flush(crtc->fb, crtc->x, crtc->y,
				crtc->fb->width, crtc->fb->height);
	}

	return 0;
}

/* update parameters that are dependent on the framebuffer dimensions and
 * position within the fb that this crtc scans out from. This is called
 * when framebuffer dimensions or x,y base may have changed, either due
 * to our mode, or a change in another crtc that is scanning out of the
 * same fb.
 */
static void update_scanout(struct drm_crtc *crtc)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	dma_addr_t paddr;
	unsigned int screen_width;

	omap_framebuffer_get_buffer(crtc->fb, crtc->x, crtc->y,
			NULL, &paddr, &screen_width);

	DBG("%s: %d,%d: %08x (%d)", omap_crtc->ovl->name,
			crtc->x, crtc->y, (u32)paddr, screen_width);

	omap_crtc->info.paddr = paddr;
	omap_crtc->info.screen_width = screen_width;
}

static void omap_crtc_gamma_set(struct drm_crtc *crtc,
		u16 *red, u16 *green, u16 *blue, uint32_t start, uint32_t size)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	DBG("%s", omap_crtc->ovl->name);
}

static void omap_crtc_destroy(struct drm_crtc *crtc)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	DBG("%s", omap_crtc->ovl->name);
	drm_crtc_cleanup(crtc);
	kfree(omap_crtc);
}

static void omap_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);

	DBG("%s: %d", omap_crtc->ovl->name, mode);

	if (mode == DRM_MODE_DPMS_ON) {
		update_scanout(crtc);
		omap_crtc->info.enabled = true;
	} else {
		omap_crtc->info.enabled = false;
	}

	WARN_ON(commit(crtc));
}

static bool omap_crtc_mode_fixup(struct drm_crtc *crtc,
				  struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	DBG("%s", omap_crtc->ovl->name);
	return true;
}

static int omap_crtc_mode_set(struct drm_crtc *crtc,
			       struct drm_display_mode *mode,
			       struct drm_display_mode *adjusted_mode,
			       int x, int y,
			       struct drm_framebuffer *old_fb)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);

	DBG("%s: %d,%d: %dx%d", omap_crtc->ovl->name, x, y,
			mode->hdisplay, mode->vdisplay);

	/* just use adjusted mode */
	mode = adjusted_mode;

	omap_crtc->info.width = mode->hdisplay;
	omap_crtc->info.height = mode->vdisplay;
	omap_crtc->info.out_width = mode->hdisplay;
	omap_crtc->info.out_height = mode->vdisplay;
	omap_crtc->info.color_mode = OMAP_DSS_COLOR_RGB24U;
	omap_crtc->info.rotation_type = OMAP_DSS_ROT_DMA;
	omap_crtc->info.rotation = OMAP_DSS_ROT_0;
	omap_crtc->info.global_alpha = 0xff;
	omap_crtc->info.mirror = 0;
	omap_crtc->info.mirror = 0;
	omap_crtc->info.pos_x = 0;
	omap_crtc->info.pos_y = 0;
#if 0 /* re-enable when these are available in DSS2 driver */
	omap_crtc->info.zorder = 3;        /* GUI in the front, video behind */
	omap_crtc->info.min_x_decim = 1;
	omap_crtc->info.max_x_decim = 1;
	omap_crtc->info.min_y_decim = 1;
	omap_crtc->info.max_y_decim = 1;
#endif

	update_scanout(crtc);

	return 0;
}

static void omap_crtc_prepare(struct drm_crtc *crtc)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	struct omap_overlay *ovl = omap_crtc->ovl;

	DBG("%s", omap_crtc->ovl->name);

	ovl->get_overlay_info(ovl, &omap_crtc->info);

	omap_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);
}

static void omap_crtc_commit(struct drm_crtc *crtc)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	DBG("%s", omap_crtc->ovl->name);
	omap_crtc_dpms(crtc, DRM_MODE_DPMS_ON);
}

static int omap_crtc_mode_set_base(struct drm_crtc *crtc, int x, int y,
		    struct drm_framebuffer *old_fb)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);

	DBG("%s %d,%d: fb=%p", omap_crtc->ovl->name, x, y, old_fb);

	update_scanout(crtc);

	return commit(crtc);
}

static void omap_crtc_load_lut(struct drm_crtc *crtc)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	DBG("%s", omap_crtc->ovl->name);
}

static void page_flip_cb(void *arg)
{
	struct drm_crtc *crtc = arg;
	struct drm_device *dev = crtc->dev;
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	struct drm_pending_vblank_event *event = omap_crtc->event;
	struct timeval now;
	unsigned long flags;

	WARN_ON(!event);

	omap_crtc->event = NULL;

	update_scanout(crtc);
	WARN_ON(commit(crtc));

	/* wakeup userspace */
	/* TODO: this should happen *after* flip in vsync IRQ handler */
	if (event) {
		spin_lock_irqsave(&dev->event_lock, flags);
		event->event.sequence = drm_vblank_count_and_time(
				dev, omap_crtc->id, &now);
		event->event.tv_sec = now.tv_sec;
		event->event.tv_usec = now.tv_usec;
		list_add_tail(&event->base.link,
				&event->base.file_priv->event_list);
		wake_up_interruptible(&event->base.file_priv->event_wait);
		spin_unlock_irqrestore(&dev->event_lock, flags);
	}
}

static int omap_crtc_page_flip_locked(struct drm_crtc *crtc,
		 struct drm_framebuffer *fb,
		 struct drm_pending_vblank_event *event)
{
	struct drm_device *dev = crtc->dev;
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);

	DBG("%d -> %d", crtc->fb ? crtc->fb->base.id : -1, fb->base.id);

	if (omap_crtc->event) {
		dev_err(dev->dev, "already a pending flip\n");
		return -EINVAL;
	}

	crtc->fb = fb;
	omap_crtc->event = event;

	omap_gem_op_async(omap_framebuffer_bo(fb), OMAP_GEM_READ,
			page_flip_cb, crtc);

	return 0;
}

static const struct drm_crtc_funcs omap_crtc_funcs = {
	.gamma_set = omap_crtc_gamma_set,
	.set_config = drm_crtc_helper_set_config,
	.destroy = omap_crtc_destroy,
	.page_flip = omap_crtc_page_flip_locked,
};

static const struct drm_crtc_helper_funcs omap_crtc_helper_funcs = {
	.dpms = omap_crtc_dpms,
	.mode_fixup = omap_crtc_mode_fixup,
	.mode_set = omap_crtc_mode_set,
	.prepare = omap_crtc_prepare,
	.commit = omap_crtc_commit,
	.mode_set_base = omap_crtc_mode_set_base,
	.load_lut = omap_crtc_load_lut,
};

struct omap_overlay *omap_crtc_get_overlay(struct drm_crtc *crtc)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	return omap_crtc->ovl;
}

/* initialize crtc */
struct drm_crtc *omap_crtc_init(struct drm_device *dev,
		struct omap_overlay *ovl, int id)
{
	struct drm_crtc *crtc = NULL;
	struct omap_crtc *omap_crtc = kzalloc(sizeof(*omap_crtc), GFP_KERNEL);

	DBG("%s", ovl->name);

	if (!omap_crtc) {
		dev_err(dev->dev, "could not allocate CRTC\n");
		goto fail;
	}

	omap_crtc->ovl = ovl;
	omap_crtc->id = id;
	crtc = &omap_crtc->base;
	drm_crtc_init(dev, crtc, &omap_crtc_funcs);
	drm_crtc_helper_add(crtc, &omap_crtc_helper_funcs);

	return crtc;

fail:
	if (crtc) {
		omap_crtc_destroy(crtc);
	}
	return NULL;
}
