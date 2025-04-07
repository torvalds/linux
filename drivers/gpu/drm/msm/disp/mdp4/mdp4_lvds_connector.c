// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 * Author: Vinay Simha <vinaysimha@inforcecomputing.com>
 */

#include "mdp4_kms.h"

struct mdp4_lvds_connector {
	struct drm_connector base;
	struct drm_encoder *encoder;
	struct device_node *panel_node;
	struct drm_panel *panel;
};
#define to_mdp4_lvds_connector(x) container_of(x, struct mdp4_lvds_connector, base)

static enum drm_connector_status mdp4_lvds_connector_detect(
		struct drm_connector *connector, bool force)
{
	struct mdp4_lvds_connector *mdp4_lvds_connector =
			to_mdp4_lvds_connector(connector);

	if (!mdp4_lvds_connector->panel) {
		mdp4_lvds_connector->panel =
			of_drm_find_panel(mdp4_lvds_connector->panel_node);
		if (IS_ERR(mdp4_lvds_connector->panel))
			mdp4_lvds_connector->panel = NULL;
	}

	return mdp4_lvds_connector->panel ?
			connector_status_connected :
			connector_status_disconnected;
}

static void mdp4_lvds_connector_destroy(struct drm_connector *connector)
{
	struct mdp4_lvds_connector *mdp4_lvds_connector =
			to_mdp4_lvds_connector(connector);

	drm_connector_cleanup(connector);

	kfree(mdp4_lvds_connector);
}

static int mdp4_lvds_connector_get_modes(struct drm_connector *connector)
{
	struct mdp4_lvds_connector *mdp4_lvds_connector =
			to_mdp4_lvds_connector(connector);
	struct drm_panel *panel = mdp4_lvds_connector->panel;
	int ret = 0;

	if (panel)
		ret = drm_panel_get_modes(panel, connector);

	return ret;
}

static enum drm_mode_status
mdp4_lvds_connector_mode_valid(struct drm_connector *connector,
			       const struct drm_display_mode *mode)
{
	struct mdp4_lvds_connector *mdp4_lvds_connector =
			to_mdp4_lvds_connector(connector);
	struct drm_encoder *encoder = mdp4_lvds_connector->encoder;
	long actual, requested;

	requested = 1000 * mode->clock;
	actual = mdp4_lcdc_round_pixclk(encoder, requested);

	DBG("requested=%ld, actual=%ld", requested, actual);

	if (actual != requested)
		return MODE_CLOCK_RANGE;

	return MODE_OK;
}

static const struct drm_connector_funcs mdp4_lvds_connector_funcs = {
	.detect = mdp4_lvds_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = mdp4_lvds_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_connector_helper_funcs mdp4_lvds_connector_helper_funcs = {
	.get_modes = mdp4_lvds_connector_get_modes,
	.mode_valid = mdp4_lvds_connector_mode_valid,
};

/* initialize connector */
struct drm_connector *mdp4_lvds_connector_init(struct drm_device *dev,
		struct device_node *panel_node, struct drm_encoder *encoder)
{
	struct drm_connector *connector = NULL;
	struct mdp4_lvds_connector *mdp4_lvds_connector;

	mdp4_lvds_connector = kzalloc(sizeof(*mdp4_lvds_connector), GFP_KERNEL);
	if (!mdp4_lvds_connector)
		return ERR_PTR(-ENOMEM);

	mdp4_lvds_connector->encoder = encoder;
	mdp4_lvds_connector->panel_node = panel_node;

	connector = &mdp4_lvds_connector->base;

	drm_connector_init(dev, connector, &mdp4_lvds_connector_funcs,
			DRM_MODE_CONNECTOR_LVDS);
	drm_connector_helper_add(connector, &mdp4_lvds_connector_helper_funcs);

	connector->polled = 0;

	connector->interlace_allowed = 0;
	connector->doublescan_allowed = 0;

	drm_connector_attach_encoder(connector, encoder);

	return connector;
}
