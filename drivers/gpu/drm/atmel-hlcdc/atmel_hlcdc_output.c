/*
 * Copyright (C) 2014 Traphandler
 * Copyright (C) 2014 Free Electrons
 * Copyright (C) 2014 Atmel
 *
 * Author: Jean-Jacques Hiblot <jjhiblot@traphandler.com>
 * Author: Boris BREZILLON <boris.brezillon@free-electrons.com>
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

#include <linux/of_graph.h>

#include <drm/drmP.h>
#include <drm/drm_of.h>

#include "atmel_hlcdc_dc.h"

/**
 * Atmel HLCDC RGB connector structure
 *
 * This structure stores RGB slave device information.
 *
 * @connector: DRM connector
 * @encoder: DRM encoder
 * @dc: pointer to the atmel_hlcdc_dc structure
 * @panel: panel connected on the RGB output
 */
struct atmel_hlcdc_rgb_output {
	struct drm_connector connector;
	struct drm_encoder encoder;
	struct atmel_hlcdc_dc *dc;
	struct drm_panel *panel;
};

static inline struct atmel_hlcdc_rgb_output *
drm_connector_to_atmel_hlcdc_rgb_output(struct drm_connector *connector)
{
	return container_of(connector, struct atmel_hlcdc_rgb_output,
			    connector);
}

static inline struct atmel_hlcdc_rgb_output *
drm_encoder_to_atmel_hlcdc_rgb_output(struct drm_encoder *encoder)
{
	return container_of(encoder, struct atmel_hlcdc_rgb_output, encoder);
}

static void atmel_hlcdc_rgb_encoder_enable(struct drm_encoder *encoder)
{
	struct atmel_hlcdc_rgb_output *rgb =
			drm_encoder_to_atmel_hlcdc_rgb_output(encoder);

	if (rgb->panel) {
		drm_panel_prepare(rgb->panel);
		drm_panel_enable(rgb->panel);
	}
}

static void atmel_hlcdc_rgb_encoder_disable(struct drm_encoder *encoder)
{
	struct atmel_hlcdc_rgb_output *rgb =
			drm_encoder_to_atmel_hlcdc_rgb_output(encoder);

	if (rgb->panel) {
		drm_panel_disable(rgb->panel);
		drm_panel_unprepare(rgb->panel);
	}
}

static const struct drm_encoder_helper_funcs atmel_hlcdc_panel_encoder_helper_funcs = {
	.disable = atmel_hlcdc_rgb_encoder_disable,
	.enable = atmel_hlcdc_rgb_encoder_enable,
};

static void atmel_hlcdc_rgb_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
	memset(encoder, 0, sizeof(*encoder));
}

static const struct drm_encoder_funcs atmel_hlcdc_panel_encoder_funcs = {
	.destroy = atmel_hlcdc_rgb_encoder_destroy,
};

static int atmel_hlcdc_panel_get_modes(struct drm_connector *connector)
{
	struct atmel_hlcdc_rgb_output *rgb =
			drm_connector_to_atmel_hlcdc_rgb_output(connector);

	if (rgb->panel)
		return rgb->panel->funcs->get_modes(rgb->panel);

	return 0;
}

static int atmel_hlcdc_rgb_mode_valid(struct drm_connector *connector,
				      struct drm_display_mode *mode)
{
	struct atmel_hlcdc_rgb_output *rgb =
			drm_connector_to_atmel_hlcdc_rgb_output(connector);

	return atmel_hlcdc_dc_mode_valid(rgb->dc, mode);
}

static const struct drm_connector_helper_funcs atmel_hlcdc_panel_connector_helper_funcs = {
	.get_modes = atmel_hlcdc_panel_get_modes,
	.mode_valid = atmel_hlcdc_rgb_mode_valid,
};

static enum drm_connector_status
atmel_hlcdc_panel_connector_detect(struct drm_connector *connector, bool force)
{
	struct atmel_hlcdc_rgb_output *rgb =
			drm_connector_to_atmel_hlcdc_rgb_output(connector);

	if (rgb->panel)
		return connector_status_connected;

	return connector_status_disconnected;
}

static void
atmel_hlcdc_panel_connector_destroy(struct drm_connector *connector)
{
	struct atmel_hlcdc_rgb_output *rgb =
			drm_connector_to_atmel_hlcdc_rgb_output(connector);

	if (rgb->panel)
		drm_panel_detach(rgb->panel);

	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs atmel_hlcdc_panel_connector_funcs = {
	.dpms = drm_atomic_helper_connector_dpms,
	.detect = atmel_hlcdc_panel_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = atmel_hlcdc_panel_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int atmel_hlcdc_attach_endpoint(struct drm_device *dev,
				       const struct device_node *np)
{
	struct atmel_hlcdc_dc *dc = dev->dev_private;
	struct atmel_hlcdc_rgb_output *output;
	struct drm_panel *panel;
	struct drm_bridge *bridge;
	int ret;

	output = devm_kzalloc(dev->dev, sizeof(*output), GFP_KERNEL);
	if (!output)
		return -EINVAL;

	output->dc = dc;

	drm_encoder_helper_add(&output->encoder,
			       &atmel_hlcdc_panel_encoder_helper_funcs);
	ret = drm_encoder_init(dev, &output->encoder,
			       &atmel_hlcdc_panel_encoder_funcs,
			       DRM_MODE_ENCODER_NONE, NULL);
	if (ret)
		return ret;

	output->encoder.possible_crtcs = 0x1;

	ret = drm_of_find_panel_or_bridge(np, 0, 0, &panel, &bridge);
	if (ret)
		return ret;

	if (panel) {
		output->connector.dpms = DRM_MODE_DPMS_OFF;
		output->connector.polled = DRM_CONNECTOR_POLL_CONNECT;
		drm_connector_helper_add(&output->connector,
				&atmel_hlcdc_panel_connector_helper_funcs);
		ret = drm_connector_init(dev, &output->connector,
					 &atmel_hlcdc_panel_connector_funcs,
					 DRM_MODE_CONNECTOR_Unknown);
		if (ret)
			goto err_encoder_cleanup;

		drm_mode_connector_attach_encoder(&output->connector,
						  &output->encoder);

		ret = drm_panel_attach(panel, &output->connector);
		if (ret) {
			drm_connector_cleanup(&output->connector);
			goto err_encoder_cleanup;
		}

		output->panel = panel;

		return 0;
	}

	if (bridge) {
		ret = drm_bridge_attach(&output->encoder, bridge, NULL);
		if (!ret)
			return 0;
	}

err_encoder_cleanup:
	drm_encoder_cleanup(&output->encoder);

	return ret;
}

int atmel_hlcdc_create_outputs(struct drm_device *dev)
{
	struct device_node *remote;
	int ret, endpoint = 0;

	while (true) {
		/* Loop thru possible multiple connections to the output */
		remote = of_graph_get_remote_node(dev->dev->of_node, 0,
						  endpoint++);
		if (!remote)
			break;

		ret = atmel_hlcdc_attach_endpoint(dev, remote);
		of_node_put(remote);
		if (ret)
			return ret;
	}

	if (!endpoint)
		return -ENODEV;
	return ret;
}
