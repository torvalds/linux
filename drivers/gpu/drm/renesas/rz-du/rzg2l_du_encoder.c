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

#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_panel.h>

#include "rzg2l_du_drv.h"
#include "rzg2l_du_encoder.h"

/* -----------------------------------------------------------------------------
 * Encoder
 */

static const struct drm_encoder_funcs rzg2l_du_encoder_funcs = {
};

int rzg2l_du_encoder_init(struct rzg2l_du_device  *rcdu,
			  enum rzg2l_du_output output,
			  struct device_node *enc_node)
{
	struct rzg2l_du_encoder *renc;
	struct drm_connector *connector;
	struct drm_bridge *bridge;
	int ret;

	/* Locate the DRM bridge from the DT node. */
	bridge = of_drm_find_bridge(enc_node);
	if (!bridge)
		return -EPROBE_DEFER;

	dev_dbg(rcdu->dev, "initializing encoder %pOF for output %s\n",
		enc_node, rzg2l_du_output_name(output));

	renc = drmm_encoder_alloc(&rcdu->ddev, struct rzg2l_du_encoder, base,
				  &rzg2l_du_encoder_funcs, DRM_MODE_ENCODER_NONE,
				  NULL);
	if (IS_ERR(renc))
		return PTR_ERR(renc);

	renc->output = output;

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
