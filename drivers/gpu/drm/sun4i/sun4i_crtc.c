// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2015 Free Electrons
 * Copyright (C) 2015 NextThing Co
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 */

#include <linux/clk-provider.h>
#include <linux/ioport.h>
#include <linux/of_address.h>
#include <linux/of_graph.h>
#include <linux/of_irq.h>
#include <linux/regmap.h>

#include <video/videomode.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_modes.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include "sun4i_backend.h"
#include "sun4i_crtc.h"
#include "sun4i_drv.h"
#include "sunxi_engine.h"
#include "sun4i_tcon.h"

/*
 * While this isn't really working in the DRM theory, in practice we
 * can only ever have one encoder per TCON since we have a mux in our
 * TCON.
 */
static struct drm_encoder *sun4i_crtc_get_encoder(struct drm_crtc *crtc)
{
	struct drm_encoder *encoder;

	drm_for_each_encoder(encoder, crtc->dev)
		if (encoder->crtc == crtc)
			return encoder;

	return NULL;
}

static int sun4i_crtc_atomic_check(struct drm_crtc *crtc,
				    struct drm_atomic_state *state)
{
	struct drm_crtc_state *crtc_state = drm_atomic_get_new_crtc_state(state,
									  crtc);
	struct sun4i_crtc *scrtc = drm_crtc_to_sun4i_crtc(crtc);
	struct sunxi_engine *engine = scrtc->engine;
	int ret = 0;

	if (engine && engine->ops && engine->ops->atomic_check)
		ret = engine->ops->atomic_check(engine, crtc_state);

	return ret;
}

static void sun4i_crtc_atomic_begin(struct drm_crtc *crtc,
				    struct drm_crtc_state *old_state)
{
	struct sun4i_crtc *scrtc = drm_crtc_to_sun4i_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct sunxi_engine *engine = scrtc->engine;
	unsigned long flags;

	if (crtc->state->event) {
		WARN_ON(drm_crtc_vblank_get(crtc) != 0);

		spin_lock_irqsave(&dev->event_lock, flags);
		scrtc->event = crtc->state->event;
		spin_unlock_irqrestore(&dev->event_lock, flags);
		crtc->state->event = NULL;
	}

	if (engine->ops->atomic_begin)
		engine->ops->atomic_begin(engine, old_state);
}

static void sun4i_crtc_atomic_flush(struct drm_crtc *crtc,
				    struct drm_crtc_state *old_state)
{
	struct sun4i_crtc *scrtc = drm_crtc_to_sun4i_crtc(crtc);
	struct drm_pending_vblank_event *event = crtc->state->event;

	DRM_DEBUG_DRIVER("Committing plane changes\n");

	sunxi_engine_commit(scrtc->engine);

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

static void sun4i_crtc_atomic_disable(struct drm_crtc *crtc,
				      struct drm_atomic_state *state)
{
	struct drm_encoder *encoder = sun4i_crtc_get_encoder(crtc);
	struct sun4i_crtc *scrtc = drm_crtc_to_sun4i_crtc(crtc);

	DRM_DEBUG_DRIVER("Disabling the CRTC\n");

	drm_crtc_vblank_off(crtc);

	sun4i_tcon_set_status(scrtc->tcon, encoder, false);

	if (crtc->state->event && !crtc->state->active) {
		spin_lock_irq(&crtc->dev->event_lock);
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		spin_unlock_irq(&crtc->dev->event_lock);

		crtc->state->event = NULL;
	}
}

static void sun4i_crtc_atomic_enable(struct drm_crtc *crtc,
				     struct drm_atomic_state *state)
{
	struct drm_encoder *encoder = sun4i_crtc_get_encoder(crtc);
	struct sun4i_crtc *scrtc = drm_crtc_to_sun4i_crtc(crtc);

	DRM_DEBUG_DRIVER("Enabling the CRTC\n");

	sun4i_tcon_set_status(scrtc->tcon, encoder, true);

	drm_crtc_vblank_on(crtc);
}

static void sun4i_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
	struct drm_display_mode *mode = &crtc->state->adjusted_mode;
	struct drm_encoder *encoder = sun4i_crtc_get_encoder(crtc);
	struct sun4i_crtc *scrtc = drm_crtc_to_sun4i_crtc(crtc);

	sun4i_tcon_mode_set(scrtc->tcon, encoder, mode);
}

static const struct drm_crtc_helper_funcs sun4i_crtc_helper_funcs = {
	.atomic_check	= sun4i_crtc_atomic_check,
	.atomic_begin	= sun4i_crtc_atomic_begin,
	.atomic_flush	= sun4i_crtc_atomic_flush,
	.atomic_enable	= sun4i_crtc_atomic_enable,
	.atomic_disable	= sun4i_crtc_atomic_disable,
	.mode_set_nofb	= sun4i_crtc_mode_set_nofb,
};

static int sun4i_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct sun4i_crtc *scrtc = drm_crtc_to_sun4i_crtc(crtc);

	DRM_DEBUG_DRIVER("Enabling VBLANK on crtc %p\n", crtc);

	sun4i_tcon_enable_vblank(scrtc->tcon, true);

	return 0;
}

static void sun4i_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct sun4i_crtc *scrtc = drm_crtc_to_sun4i_crtc(crtc);

	DRM_DEBUG_DRIVER("Disabling VBLANK on crtc %p\n", crtc);

	sun4i_tcon_enable_vblank(scrtc->tcon, false);
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

struct sun4i_crtc *sun4i_crtc_init(struct drm_device *drm,
				   struct sunxi_engine *engine,
				   struct sun4i_tcon *tcon)
{
	struct sun4i_crtc *scrtc;
	struct drm_plane **planes;
	struct drm_plane *primary = NULL, *cursor = NULL;
	int ret, i;

	scrtc = devm_kzalloc(drm->dev, sizeof(*scrtc), GFP_KERNEL);
	if (!scrtc)
		return ERR_PTR(-ENOMEM);
	scrtc->engine = engine;
	scrtc->tcon = tcon;

	/* Create our layers */
	planes = sunxi_engine_layers_init(drm, engine);
	if (IS_ERR(planes)) {
		dev_err(drm->dev, "Couldn't create the planes\n");
		return NULL;
	}

	/* find primary and cursor planes for drm_crtc_init_with_planes */
	for (i = 0; planes[i]; i++) {
		struct drm_plane *plane = planes[i];

		switch (plane->type) {
		case DRM_PLANE_TYPE_PRIMARY:
			primary = plane;
			break;
		case DRM_PLANE_TYPE_CURSOR:
			cursor = plane;
			break;
		default:
			break;
		}
	}

	ret = drm_crtc_init_with_planes(drm, &scrtc->crtc,
					primary,
					cursor,
					&sun4i_crtc_funcs,
					NULL);
	if (ret) {
		dev_err(drm->dev, "Couldn't init DRM CRTC\n");
		return ERR_PTR(ret);
	}

	drm_crtc_helper_add(&scrtc->crtc, &sun4i_crtc_helper_funcs);

	/* Set crtc.port to output port node of the tcon */
	scrtc->crtc.port = of_graph_get_port_by_id(scrtc->tcon->dev->of_node,
						   1);

	/* Set possible_crtcs to this crtc for overlay planes */
	for (i = 0; planes[i]; i++) {
		uint32_t possible_crtcs = drm_crtc_mask(&scrtc->crtc);
		struct drm_plane *plane = planes[i];

		if (plane->type == DRM_PLANE_TYPE_OVERLAY)
			plane->possible_crtcs = possible_crtcs;
	}

	return scrtc;
}
