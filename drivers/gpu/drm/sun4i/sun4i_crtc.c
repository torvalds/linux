/*
 * Copyright (C) 2015 Free Electrons
 * Copyright (C) 2015 NextThing Co
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_modes.h>

#include <linux/clk-provider.h>
#include <linux/ioport.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/regmap.h>

#include <video/videomode.h>

#include "sun4i_backend.h"
#include "sun4i_crtc.h"
#include "sun4i_drv.h"
#include "sun4i_tcon.h"

static void sun4i_crtc_atomic_begin(struct drm_crtc *crtc,
				    struct drm_crtc_state *old_state)
{
	struct sun4i_crtc *scrtc = drm_crtc_to_sun4i_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	unsigned long flags;

	if (crtc->state->event) {
		WARN_ON(drm_crtc_vblank_get(crtc) != 0);

		spin_lock_irqsave(&dev->event_lock, flags);
		scrtc->event = crtc->state->event;
		spin_unlock_irqrestore(&dev->event_lock, flags);
		crtc->state->event = NULL;
	 }
}

static void sun4i_crtc_atomic_flush(struct drm_crtc *crtc,
				    struct drm_crtc_state *old_state)
{
	struct sun4i_crtc *scrtc = drm_crtc_to_sun4i_crtc(crtc);
	struct sun4i_drv *drv = scrtc->drv;
	struct drm_pending_vblank_event *event = crtc->state->event;

	DRM_DEBUG_DRIVER("Committing plane changes\n");

	sun4i_backend_commit(drv->backend);

	if (event) {
		crtc->state->event = NULL;

		spin_lock_irq(&crtc->dev->event_lock);
		if (drm_crtc_vblank_get(crtc) == 0)
			drm_crtc_arm_vblank_event(crtc, event);
		else
			drm_crtc_send_vblank_event(crtc, event);
		spin_unlock_irq(&crtc->dev->event_lock);
	}
}

static void sun4i_crtc_disable(struct drm_crtc *crtc)
{
	struct sun4i_crtc *scrtc = drm_crtc_to_sun4i_crtc(crtc);
	struct sun4i_drv *drv = scrtc->drv;

	DRM_DEBUG_DRIVER("Disabling the CRTC\n");

	sun4i_tcon_disable(drv->tcon);

	if (crtc->state->event && !crtc->state->active) {
		spin_lock_irq(&crtc->dev->event_lock);
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		spin_unlock_irq(&crtc->dev->event_lock);

		crtc->state->event = NULL;
	}
}

static void sun4i_crtc_enable(struct drm_crtc *crtc)
{
	struct sun4i_crtc *scrtc = drm_crtc_to_sun4i_crtc(crtc);
	struct sun4i_drv *drv = scrtc->drv;

	DRM_DEBUG_DRIVER("Enabling the CRTC\n");

	sun4i_tcon_enable(drv->tcon);
}

static const struct drm_crtc_helper_funcs sun4i_crtc_helper_funcs = {
	.atomic_begin	= sun4i_crtc_atomic_begin,
	.atomic_flush	= sun4i_crtc_atomic_flush,
	.disable	= sun4i_crtc_disable,
	.enable		= sun4i_crtc_enable,
};

static int sun4i_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct sun4i_crtc *scrtc = drm_crtc_to_sun4i_crtc(crtc);
	struct sun4i_drv *drv = scrtc->drv;

	DRM_DEBUG_DRIVER("Enabling VBLANK on crtc %p\n", crtc);

	sun4i_tcon_enable_vblank(drv->tcon, true);

	return 0;
}

static void sun4i_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct sun4i_crtc *scrtc = drm_crtc_to_sun4i_crtc(crtc);
	struct sun4i_drv *drv = scrtc->drv;

	DRM_DEBUG_DRIVER("Disabling VBLANK on crtc %p\n", crtc);

	sun4i_tcon_enable_vblank(drv->tcon, false);
}

static const struct drm_crtc_funcs sun4i_crtc_funcs = {
	.atomic_destroy_state	= drm_atomic_helper_crtc_destroy_state,
	.atomic_duplicate_state	= drm_atomic_helper_crtc_duplicate_state,
	.destroy		= drm_crtc_cleanup,
	.page_flip		= drm_atomic_helper_page_flip,
	.reset			= drm_atomic_helper_crtc_reset,
	.set_config		= drm_atomic_helper_set_config,
	.enable_vblank		= sun4i_crtc_enable_vblank,
	.disable_vblank		= sun4i_crtc_disable_vblank,
};

struct sun4i_crtc *sun4i_crtc_init(struct drm_device *drm)
{
	struct sun4i_drv *drv = drm->dev_private;
	struct sun4i_crtc *scrtc;
	int ret;

	scrtc = devm_kzalloc(drm->dev, sizeof(*scrtc), GFP_KERNEL);
	if (!scrtc)
		return NULL;
	scrtc->drv = drv;

	ret = drm_crtc_init_with_planes(drm, &scrtc->crtc,
					drv->primary,
					NULL,
					&sun4i_crtc_funcs,
					NULL);
	if (ret) {
		dev_err(drm->dev, "Couldn't init DRM CRTC\n");
		return NULL;
	}

	drm_crtc_helper_add(&scrtc->crtc, &sun4i_crtc_helper_funcs);

	return scrtc;
}
