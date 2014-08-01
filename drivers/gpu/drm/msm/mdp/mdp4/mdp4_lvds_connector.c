/*
 * Copyright (C) 2014 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 * Author: Vinay Simha <vinaysimha@inforcecomputing.com>
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

#include <linux/gpio.h>

#include "mdp4_kms.h"

struct mdp4_lvds_connector {
	struct drm_connector base;
	struct drm_encoder *encoder;
	struct drm_panel *panel;
};
#define to_mdp4_lvds_connector(x) container_of(x, struct mdp4_lvds_connector, base)

static enum drm_connector_status mdp4_lvds_connector_detect(
		struct drm_connector *connector, bool force)
{
	struct mdp4_lvds_connector *mdp4_lvds_connector =
			to_mdp4_lvds_connector(connector);

	return mdp4_lvds_connector->panel ?
			connector_status_connected :
			connector_status_disconnected;
}

static void mdp4_lvds_connector_destroy(struct drm_connector *connector)
{
	struct mdp4_lvds_connector *mdp4_lvds_connector =
			to_mdp4_lvds_connector(connector);
	struct drm_panel *panel = mdp4_lvds_connector->panel;

	if (panel)
		drm_panel_detach(panel);

	drm_connector_unregister(connector);
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
		ret = panel->funcs->get_modes(panel);

	return ret;
}

static int mdp4_lvds_connector_mode_valid(struct drm_connector *connector,
				 struct drm_display_mode *mode)
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

static struct drm_encoder *
mdp4_lvds_connector_best_encoder(struct drm_connector *connector)
{
	struct mdp4_lvds_connector *mdp4_lvds_connector =
			to_mdp4_lvds_connector(connector);
	return mdp4_lvds_connector->encoder;
}

static const struct drm_connector_funcs mdp4_lvds_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.detect = mdp4_lvds_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = mdp4_lvds_connector_destroy,
};

static const struct drm_connector_helper_funcs mdp4_lvds_connector_helper_funcs = {
	.get_modes = mdp4_lvds_connector_get_modes,
	.mode_valid = mdp4_lvds_connector_mode_valid,
	.best_encoder = mdp4_lvds_connector_best_encoder,
};

/* initialize connector */
struct drm_connector *mdp4_lvds_connector_init(struct drm_device *dev,
		struct drm_panel *panel, struct drm_encoder *encoder)
{
	struct drm_connector *connector = NULL;
	struct mdp4_lvds_connector *mdp4_lvds_connector;
	int ret;

	mdp4_lvds_connector = kzalloc(sizeof(*mdp4_lvds_connector), GFP_KERNEL);
	if (!mdp4_lvds_connector) {
		ret = -ENOMEM;
		goto fail;
	}

	mdp4_lvds_connector->encoder = encoder;
	mdp4_lvds_connector->panel = panel;

	connector = &mdp4_lvds_connector->base;

	drm_connector_init(dev, connector, &mdp4_lvds_connector_funcs,
			DRM_MODE_CONNECTOR_LVDS);
	drm_connector_helper_add(connector, &mdp4_lvds_connector_helper_funcs);

	connector->polled = 0;

	connector->interlace_allowed = 0;
	connector->doublescan_allowed = 0;

	drm_connector_register(connector);

	drm_mode_connector_attach_encoder(connector, encoder);

	if (panel)
		drm_panel_attach(panel, connector);

	return connector;

fail:
	if (connector)
		mdp4_lvds_connector_destroy(connector);

	return ERR_PTR(ret);
}
