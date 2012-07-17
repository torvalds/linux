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
	struct drm_plane *plane;
	const char *name;
	int id;

	/* if there is a pending flip, these will be non-null: */
	struct drm_pending_vblank_event *event;
	struct drm_framebuffer *old_fb;
};

static void omap_crtc_destroy(struct drm_crtc *crtc)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	omap_crtc->plane->funcs->destroy(omap_crtc->plane);
	drm_crtc_cleanup(crtc);
	kfree(omap_crtc);
}

static void omap_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct omap_drm_private *priv = crtc->dev->dev_private;
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	int i;

	WARN_ON(omap_plane_dpms(omap_crtc->plane, mode));

	for (i = 0; i < priv->num_planes; i++) {
		struct drm_plane *plane = priv->planes[i];
		if (plane->crtc == crtc)
			WARN_ON(omap_plane_dpms(plane, mode));
	}
}

static bool omap_crtc_mode_fixup(struct drm_crtc *crtc,
		const struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode)
{
	return true;
}

static int omap_crtc_mode_set(struct drm_crtc *crtc,
		struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode,
		int x, int y,
		struct drm_framebuffer *old_fb)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	struct drm_plane *plane = omap_crtc->plane;

	return omap_plane_mode_set(plane, crtc, crtc->fb,
			0, 0, mode->hdisplay, mode->vdisplay,
			x << 16, y << 16,
			mode->hdisplay << 16, mode->vdisplay << 16);
}

static void omap_crtc_prepare(struct drm_crtc *crtc)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	DBG("%s", omap_crtc->name);
	omap_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);
}

static void omap_crtc_commit(struct drm_crtc *crtc)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	DBG("%s", omap_crtc->name);
	omap_crtc_dpms(crtc, DRM_MODE_DPMS_ON);
}

static int omap_crtc_mode_set_base(struct drm_crtc *crtc, int x, int y,
		struct drm_framebuffer *old_fb)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	struct drm_plane *plane = omap_crtc->plane;
	struct drm_display_mode *mode = &crtc->mode;

	return plane->funcs->update_plane(plane, crtc, crtc->fb,
			0, 0, mode->hdisplay, mode->vdisplay,
			x << 16, y << 16,
			mode->hdisplay << 16, mode->vdisplay << 16);
}

static void omap_crtc_load_lut(struct drm_crtc *crtc)
{
}

static void vblank_cb(void *arg)
{
	static uint32_t sequence = 0;
	struct drm_crtc *crtc = arg;
	struct drm_device *dev = crtc->dev;
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	struct drm_pending_vblank_event *event = omap_crtc->event;
	unsigned long flags;
	struct timeval now;

	WARN_ON(!event);

	omap_crtc->event = NULL;

	/* wakeup userspace */
	if (event) {
		do_gettimeofday(&now);

		spin_lock_irqsave(&dev->event_lock, flags);
		/* TODO: we can't yet use the vblank time accounting,
		 * because omapdss lower layer is the one that knows
		 * the irq # and registers the handler, which more or
		 * less defeats how drm_irq works.. for now just fake
		 * the sequence number and use gettimeofday..
		 *
		event->event.sequence = drm_vblank_count_and_time(
				dev, omap_crtc->id, &now);
		 */
		event->event.sequence = sequence++;
		event->event.tv_sec = now.tv_sec;
		event->event.tv_usec = now.tv_usec;
		list_add_tail(&event->base.link,
				&event->base.file_priv->event_list);
		wake_up_interruptible(&event->base.file_priv->event_wait);
		spin_unlock_irqrestore(&dev->event_lock, flags);
	}
}

static void page_flip_cb(void *arg)
{
	struct drm_crtc *crtc = arg;
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	struct drm_framebuffer *old_fb = omap_crtc->old_fb;

	omap_crtc->old_fb = NULL;

	omap_crtc_mode_set_base(crtc, crtc->x, crtc->y, old_fb);

	/* really we'd like to setup the callback atomically w/ setting the
	 * new scanout buffer to avoid getting stuck waiting an extra vblank
	 * cycle.. for now go for correctness and later figure out speed..
	 */
	omap_plane_on_endwin(omap_crtc->plane, vblank_cb, crtc);
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

	omap_crtc->old_fb = crtc->fb;
	omap_crtc->event = event;
	crtc->fb = fb;

	omap_gem_op_async(omap_framebuffer_bo(fb, 0), OMAP_GEM_READ,
			page_flip_cb, crtc);

	return 0;
}

static const struct drm_crtc_funcs omap_crtc_funcs = {
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

	crtc = &omap_crtc->base;

	omap_crtc->plane = omap_plane_init(dev, ovl, (1 << id), true);
	omap_crtc->plane->crtc = crtc;
	omap_crtc->name = ovl->name;
	omap_crtc->id = id;

	drm_crtc_init(dev, crtc, &omap_crtc_funcs);
	drm_crtc_helper_add(crtc, &omap_crtc_helper_funcs);

	return crtc;

fail:
	if (crtc) {
		omap_crtc_destroy(crtc);
	}
	return NULL;
}
