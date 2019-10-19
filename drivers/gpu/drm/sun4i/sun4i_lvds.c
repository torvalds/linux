// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2017 Free Electrons
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 */

#include <linux/clk.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#include "sun4i_crtc.h"
#include "sun4i_tcon.h"
#include "sun4i_lvds.h"

struct sun4i_lvds {
	struct drm_connector	connector;
	struct drm_encoder	encoder;

	struct drm_panel	*panel;
};

static inline struct sun4i_lvds *
drm_connector_to_sun4i_lvds(struct drm_connector *connector)
{
	return container_of(connector, struct sun4i_lvds,
			    connector);
}

static inline struct sun4i_lvds *
drm_encoder_to_sun4i_lvds(struct drm_encoder *encoder)
{
	return container_of(encoder, struct sun4i_lvds,
			    encoder);
}

static int sun4i_lvds_get_modes(struct drm_connector *connector)
{
	struct sun4i_lvds *lvds =
		drm_connector_to_sun4i_lvds(connector);

	return drm_panel_get_modes(lvds->panel);
}

static struct drm_connector_helper_funcs sun4i_lvds_con_helper_funcs = {
	.get_modes	= sun4i_lvds_get_modes,
};

static void
sun4i_lvds_connector_destroy(struct drm_connector *connector)
{
	struct sun4i_lvds *lvds = drm_connector_to_sun4i_lvds(connector);

	drm_panel_detach(lvds->panel);
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs sun4i_lvds_con_funcs = {
	.fill_modes		= drm_helper_probe_single_connector_modes,
	.destroy		= sun4i_lvds_connector_destroy,
	.reset			= drm_atomic_helper_connector_reset,
	.atomic_duplicate_state	= drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_connector_destroy_state,
};

static void sun4i_lvds_encoder_enable(struct drm_encoder *encoder)
{
	struct sun4i_lvds *lvds = drm_encoder_to_sun4i_lvds(encoder);

	DRM_DEBUG_DRIVER("Enabling LVDS output\n");

	if (lvds->panel) {
		drm_panel_prepare(lvds->panel);
		drm_panel_enable(lvds->panel);
	}
}

static void sun4i_lvds_encoder_disable(struct drm_encoder *encoder)
{
	struct sun4i_lvds *lvds = drm_encoder_to_sun4i_lvds(encoder);

	DRM_DEBUG_DRIVER("Disabling LVDS output\n");

	if (lvds->panel) {
		drm_panel_disable(lvds->panel);
		drm_panel_unprepare(lvds->panel);
	}
}

static const struct drm_encoder_helper_funcs sun4i_lvds_enc_helper_funcs = {
	.disable	= sun4i_lvds_encoder_disable,
	.enable		= sun4i_lvds_encoder_enable,
};

static const struct drm_encoder_funcs sun4i_lvds_enc_funcs = {
	.destroy	= drm_encoder_cleanup,
};

int sun4i_lvds_init(struct drm_device *drm, struct sun4i_tcon *tcon)
{
	struct drm_encoder *encoder;
	struct drm_bridge *bridge;
	struct sun4i_lvds *lvds;
	int ret;

	lvds = devm_kzalloc(drm->dev, sizeof(*lvds), GFP_KERNEL);
	if (!lvds)
		return -ENOMEM;
	encoder = &lvds->encoder;

	ret = drm_of_find_panel_or_bridge(tcon->dev->of_node, 1, 0,
					  &lvds->panel, &bridge);
	if (ret) {
		dev_info(drm->dev, "No panel or bridge found... LVDS output disabled\n");
		return 0;
	}

	drm_encoder_helper_add(&lvds->encoder,
			       &sun4i_lvds_enc_helper_funcs);
	ret = drm_encoder_init(drm,
			       &lvds->encoder,
			       &sun4i_lvds_enc_funcs,
			       DRM_MODE_ENCODER_LVDS,
			       NULL);
	if (ret) {
		dev_err(drm->dev, "Couldn't initialise the lvds encoder\n");
		goto err_out;
	}

	/* The LVDS encoder can only work with the TCON channel 0 */
	lvds->encoder.possible_crtcs = drm_crtc_mask(&tcon->crtc->crtc);

	if (lvds->panel) {
		drm_connector_helper_add(&lvds->connector,
					 &sun4i_lvds_con_helper_funcs);
		ret = drm_connector_init(drm, &lvds->connector,
					 &sun4i_lvds_con_funcs,
					 DRM_MODE_CONNECTOR_LVDS);
		if (ret) {
			dev_err(drm->dev, "Couldn't initialise the lvds connector\n");
			goto err_cleanup_connector;
		}

		drm_connector_attach_encoder(&lvds->connector,
						  &lvds->encoder);

		ret = drm_panel_attach(lvds->panel, &lvds->connector);
		if (ret) {
			dev_err(drm->dev, "Couldn't attach our panel\n");
			goto err_cleanup_connector;
		}
	}

	if (bridge) {
		ret = drm_bridge_attach(encoder, bridge, NULL);
		if (ret) {
			dev_err(drm->dev, "Couldn't attach our bridge\n");
			goto err_cleanup_connector;
		}
	}

	return 0;

err_cleanup_connector:
	drm_encoder_cleanup(&lvds->encoder);
err_out:
	return ret;
}
EXPORT_SYMBOL(sun4i_lvds_init);
