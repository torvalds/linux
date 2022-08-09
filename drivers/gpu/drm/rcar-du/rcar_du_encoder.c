// SPDX-License-Identifier: GPL-2.0+
/*
 * rcar_du_encoder.c  --  R-Car Display Unit Encoder
 *
 * Copyright (C) 2013-2014 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#include <linux/export.h>
#include <linux/slab.h>

#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_managed.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_panel.h>

#include "rcar_du_drv.h"
#include "rcar_du_encoder.h"
#include "rcar_du_kms.h"
#include "rcar_lvds.h"

/* -----------------------------------------------------------------------------
 * Encoder
 */

static unsigned int rcar_du_encoder_count_ports(struct device_node *node)
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

static const struct drm_encoder_funcs rcar_du_encoder_funcs = {
};

int rcar_du_encoder_init(struct rcar_du_device *rcdu,
			 enum rcar_du_output output,
			 struct device_node *enc_node)
{
	struct rcar_du_encoder *renc;
	struct drm_connector *connector;
	struct drm_bridge *bridge;
	int ret;

	/*
	 * Locate the DRM bridge from the DT node. For the DPAD outputs, if the
	 * DT node has a single port, assume that it describes a panel and
	 * create a panel bridge.
	 */
	if ((output == RCAR_DU_OUTPUT_DPAD0 ||
	     output == RCAR_DU_OUTPUT_DPAD1) &&
	    rcar_du_encoder_count_ports(enc_node) == 1) {
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

		if (output == RCAR_DU_OUTPUT_LVDS0 ||
		    output == RCAR_DU_OUTPUT_LVDS1)
			rcdu->lvds[output - RCAR_DU_OUTPUT_LVDS0] = bridge;
	}

	/*
	 * Create and initialize the encoder. On Gen3, skip the LVDS1 output if
	 * the LVDS1 encoder is used as a companion for LVDS0 in dual-link
	 * mode, or any LVDS output if it isn't connected. The latter may happen
	 * on D3 or E3 as the LVDS encoders are needed to provide the pixel
	 * clock to the DU, even when the LVDS outputs are not used.
	 */
	if (rcdu->info->gen >= 3) {
		if (output == RCAR_DU_OUTPUT_LVDS1 &&
		    rcar_lvds_dual_link(bridge))
			return -ENOLINK;

		if ((output == RCAR_DU_OUTPUT_LVDS0 ||
		     output == RCAR_DU_OUTPUT_LVDS1) &&
		    !rcar_lvds_is_connected(bridge))
			return -ENOLINK;
	}

	dev_dbg(rcdu->dev, "initializing encoder %pOF for output %u\n",
		enc_node, output);

	renc = drmm_encoder_alloc(&rcdu->ddev, struct rcar_du_encoder, base,
				  &rcar_du_encoder_funcs, DRM_MODE_ENCODER_NONE,
				  NULL);
	if (!renc)
		return -ENOMEM;

	renc->output = output;

	/* Attach the bridge to the encoder. */
	ret = drm_bridge_attach(&renc->base, bridge, NULL,
				DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (ret) {
		dev_err(rcdu->dev, "failed to attach bridge for output %u\n",
			output);
		return ret;
	}

	/* Create the connector for the chain of bridges. */
	connector = drm_bridge_connector_init(&rcdu->ddev, &renc->base);
	if (IS_ERR(connector)) {
		dev_err(rcdu->dev,
			"failed to created connector for output %u\n", output);
		return PTR_ERR(connector);
	}

	return drm_connector_attach_encoder(connector, &renc->base);
}
