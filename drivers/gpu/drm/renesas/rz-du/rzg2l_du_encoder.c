// SPDX-License-Identifier: GPL-2.0+
/*
 * RZ/G2L Display Unit Encoder
 *
 * Copyright (C) 2023 Renesas Electronics Corporation
 *
 * Based on rcar_du_encoder.c
 */

#include <linux/export.h>
#include <linux/of.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_panel.h>

#include "rzg2l_du_drv.h"
#include "rzg2l_du_encoder.h"

/* -----------------------------------------------------------------------------
 * Encoder
 */

static unsigned int rzg2l_du_encoder_count_ports(struct device_node *node)
{
	struct device_node *ports;
	struct device_node *port;
	unsigned int num_ports = 0;

	ports = of_get_child_by_name(node, "ports");
	if (!ports)
		ports = of_node_get(node);

	for_each_child_of_node(ports, port) {
		if (of_node_name_eq(port, "port"))
			num_ports++;
	}

	of_node_put(ports);

	return num_ports;
}

static const struct drm_encoder_funcs rzg2l_du_encoder_funcs = {
};

static enum drm_mode_status
rzg2l_du_encoder_mode_valid(struct drm_encoder *encoder,
			    const struct drm_display_mode *mode)
{
	struct rzg2l_du_encoder *renc = to_rzg2l_encoder(encoder);

	if (renc->output == RZG2L_DU_OUTPUT_DPAD0 && mode->clock > 83500)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static const struct drm_encoder_helper_funcs rzg2l_du_encoder_helper_funcs = {
	.mode_valid = rzg2l_du_encoder_mode_valid,
};

int rzg2l_du_encoder_init(struct rzg2l_du_device  *rcdu,
			  enum rzg2l_du_output output,
			  struct device_node *enc_node)
{
	struct rzg2l_du_encoder *renc;
	struct drm_connector *connector;
	struct drm_bridge *bridge;
	int ret;

	/*
	 * Locate the DRM bridge from the DT node. For the DPAD outputs, if the
	 * DT node has a single port, assume that it describes a panel and
	 * create a panel bridge.
	 */
	if (output == RZG2L_DU_OUTPUT_DPAD0 && rzg2l_du_encoder_count_ports(enc_node) == 1) {
		struct drm_panel *panel = of_drm_find_panel(enc_node);

		if (IS_ERR(panel))
			return PTR_ERR(panel);

		bridge = devm_drm_panel_bridge_add_typed(rcdu->dev, panel,
							 DRM_MODE_CONNECTOR_DPI);
		if (IS_ERR(bridge))
			return PTR_ERR(bridge);
	} else {
		bridge = of_drm_find_bridge(enc_node);
		if (!bridge)
			return -EPROBE_DEFER;
	}

	dev_dbg(rcdu->dev, "initializing encoder %pOF for output %s\n",
		enc_node, rzg2l_du_output_name(output));

	renc = drmm_encoder_alloc(&rcdu->ddev, struct rzg2l_du_encoder, base,
				  &rzg2l_du_encoder_funcs, DRM_MODE_ENCODER_NONE,
				  NULL);
	if (IS_ERR(renc))
		return PTR_ERR(renc);

	renc->output = output;
	drm_encoder_helper_add(&renc->base, &rzg2l_du_encoder_helper_funcs);

	/* Attach the bridge to the encoder. */
	ret = drm_bridge_attach(&renc->base, bridge, NULL,
				DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (ret) {
		dev_err(rcdu->dev,
			"failed to attach bridge %pOF for output %s (%d)\n",
			bridge->of_node, rzg2l_du_output_name(output), ret);
		return ret;
	}

	/* Create the connector for the chain of bridges. */
	connector = drm_bridge_connector_init(&rcdu->ddev, &renc->base);
	if (IS_ERR(connector)) {
		dev_err(rcdu->dev,
			"failed to created connector for output %s (%ld)\n",
			rzg2l_du_output_name(output), PTR_ERR(connector));
		return PTR_ERR(connector);
	}

	return drm_connector_attach_encoder(connector, &renc->base);
}
