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
#include <drm/drm_panel.h>

#include "atmel_hlcdc_dc.h"

/**
 * Atmel HLCDC RGB output mode
 */
enum atmel_hlcdc_connector_rgb_mode {
	ATMEL_HLCDC_CONNECTOR_RGB444,
	ATMEL_HLCDC_CONNECTOR_RGB565,
	ATMEL_HLCDC_CONNECTOR_RGB666,
	ATMEL_HLCDC_CONNECTOR_RGB888,
};

/**
 * Atmel HLCDC RGB connector structure
 *
 * This structure stores RGB slave device information.
 *
 * @connector: DRM connector
 * @encoder: DRM encoder
 * @dc: pointer to the atmel_hlcdc_dc structure
 * @dpms: current DPMS mode
 */
struct atmel_hlcdc_rgb_output {
	struct drm_connector connector;
	struct drm_encoder encoder;
	struct atmel_hlcdc_dc *dc;
	int dpms;
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

/**
 * Atmel HLCDC Panel device structure
 *
 * This structure is specialization of the slave device structure to
 * interface with drm panels.
 *
 * @base: base slave device fields
 * @panel: drm panel attached to this slave device
 */
struct atmel_hlcdc_panel {
	struct atmel_hlcdc_rgb_output base;
	struct drm_panel *panel;
};

static inline struct atmel_hlcdc_panel *
atmel_hlcdc_rgb_output_to_panel(struct atmel_hlcdc_rgb_output *output)
{
	return container_of(output, struct atmel_hlcdc_panel, base);
}

static void atmel_hlcdc_panel_encoder_enable(struct drm_encoder *encoder)
{
	struct atmel_hlcdc_rgb_output *rgb =
			drm_encoder_to_atmel_hlcdc_rgb_output(encoder);
	struct atmel_hlcdc_panel *panel = atmel_hlcdc_rgb_output_to_panel(rgb);

	drm_panel_enable(panel->panel);
}

static void atmel_hlcdc_panel_encoder_disable(struct drm_encoder *encoder)
{
	struct atmel_hlcdc_rgb_output *rgb =
			drm_encoder_to_atmel_hlcdc_rgb_output(encoder);
	struct atmel_hlcdc_panel *panel = atmel_hlcdc_rgb_output_to_panel(rgb);

	drm_panel_disable(panel->panel);
}

static bool
atmel_hlcdc_panel_encoder_mode_fixup(struct drm_encoder *encoder,
				     const struct drm_display_mode *mode,
				     struct drm_display_mode *adjusted)
{
	return true;
}

static void
atmel_hlcdc_rgb_encoder_mode_set(struct drm_encoder *encoder,
				 struct drm_display_mode *mode,
				 struct drm_display_mode *adjusted)
{
	struct atmel_hlcdc_rgb_output *rgb =
			drm_encoder_to_atmel_hlcdc_rgb_output(encoder);
	struct drm_display_info *info = &rgb->connector.display_info;
	unsigned int cfg;

	cfg = 0;

	if (info->num_bus_formats) {
		switch (info->bus_formats[0]) {
		case MEDIA_BUS_FMT_RGB565_1X16:
			cfg |= ATMEL_HLCDC_CONNECTOR_RGB565 << 8;
			break;
		case MEDIA_BUS_FMT_RGB666_1X18:
			cfg |= ATMEL_HLCDC_CONNECTOR_RGB666 << 8;
			break;
		case MEDIA_BUS_FMT_RGB888_1X24:
			cfg |= ATMEL_HLCDC_CONNECTOR_RGB888 << 8;
			break;
		case MEDIA_BUS_FMT_RGB444_1X12:
		default:
			break;
		}
	}

	regmap_update_bits(rgb->dc->hlcdc->regmap, ATMEL_HLCDC_CFG(5),
			   ATMEL_HLCDC_MODE_MASK,
			   cfg);
}

static struct drm_encoder_helper_funcs atmel_hlcdc_panel_encoder_helper_funcs = {
	.mode_fixup = atmel_hlcdc_panel_encoder_mode_fixup,
	.mode_set = atmel_hlcdc_rgb_encoder_mode_set,
	.disable = atmel_hlcdc_panel_encoder_disable,
	.enable = atmel_hlcdc_panel_encoder_enable,
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
	struct atmel_hlcdc_panel *panel = atmel_hlcdc_rgb_output_to_panel(rgb);

	return panel->panel->funcs->get_modes(panel->panel);
}

static int atmel_hlcdc_rgb_mode_valid(struct drm_connector *connector,
				      struct drm_display_mode *mode)
{
	struct atmel_hlcdc_rgb_output *rgb =
			drm_connector_to_atmel_hlcdc_rgb_output(connector);

	return atmel_hlcdc_dc_mode_valid(rgb->dc, mode);
}



static struct drm_encoder *
atmel_hlcdc_rgb_best_encoder(struct drm_connector *connector)
{
	struct atmel_hlcdc_rgb_output *rgb =
			drm_connector_to_atmel_hlcdc_rgb_output(connector);

	return &rgb->encoder;
}

static struct drm_connector_helper_funcs atmel_hlcdc_panel_connector_helper_funcs = {
	.get_modes = atmel_hlcdc_panel_get_modes,
	.mode_valid = atmel_hlcdc_rgb_mode_valid,
	.best_encoder = atmel_hlcdc_rgb_best_encoder,
};

static enum drm_connector_status
atmel_hlcdc_panel_connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static void
atmel_hlcdc_panel_connector_destroy(struct drm_connector *connector)
{
	struct atmel_hlcdc_rgb_output *rgb =
			drm_connector_to_atmel_hlcdc_rgb_output(connector);
	struct atmel_hlcdc_panel *panel = atmel_hlcdc_rgb_output_to_panel(rgb);

	drm_panel_detach(panel->panel);
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

static int atmel_hlcdc_create_panel_output(struct drm_device *dev,
					   struct of_endpoint *ep)
{
	struct atmel_hlcdc_dc *dc = dev->dev_private;
	struct device_node *np;
	struct drm_panel *p = NULL;
	struct atmel_hlcdc_panel *panel;
	int ret;

	np = of_graph_get_remote_port_parent(ep->local_node);
	if (!np)
		return -EINVAL;

	p = of_drm_find_panel(np);
	of_node_put(np);

	if (!p)
		return -EPROBE_DEFER;

	panel = devm_kzalloc(dev->dev, sizeof(*panel), GFP_KERNEL);
	if (!panel)
		return -EINVAL;

	panel->base.dpms = DRM_MODE_DPMS_OFF;

	panel->base.dc = dc;

	drm_encoder_helper_add(&panel->base.encoder,
			       &atmel_hlcdc_panel_encoder_helper_funcs);
	ret = drm_encoder_init(dev, &panel->base.encoder,
			       &atmel_hlcdc_panel_encoder_funcs,
			       DRM_MODE_ENCODER_LVDS);
	if (ret)
		return ret;

	panel->base.connector.dpms = DRM_MODE_DPMS_OFF;
	panel->base.connector.polled = DRM_CONNECTOR_POLL_CONNECT;
	drm_connector_helper_add(&panel->base.connector,
				 &atmel_hlcdc_panel_connector_helper_funcs);
	ret = drm_connector_init(dev, &panel->base.connector,
				 &atmel_hlcdc_panel_connector_funcs,
				 DRM_MODE_CONNECTOR_LVDS);
	if (ret)
		goto err_encoder_cleanup;

	drm_mode_connector_attach_encoder(&panel->base.connector,
					  &panel->base.encoder);
	panel->base.encoder.possible_crtcs = 0x1;

	drm_panel_attach(p, &panel->base.connector);
	panel->panel = p;

	return 0;

err_encoder_cleanup:
	drm_encoder_cleanup(&panel->base.encoder);

	return ret;
}

int atmel_hlcdc_create_outputs(struct drm_device *dev)
{
	struct device_node *port_np, *np;
	struct of_endpoint ep;
	int ret;

	port_np = of_get_child_by_name(dev->dev->of_node, "port");
	if (!port_np)
		return -EINVAL;

	np = of_get_child_by_name(port_np, "endpoint");
	of_node_put(port_np);

	if (!np)
		return -EINVAL;

	ret = of_graph_parse_endpoint(np, &ep);
	of_node_put(port_np);

	if (ret)
		return ret;

	/* We currently only support panel output */
	return atmel_hlcdc_create_panel_output(dev, &ep);
}
