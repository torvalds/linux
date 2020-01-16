// SPDX-License-Identifier: GPL-2.0+
/*
 * rcar_du_encoder.c  --  R-Car Display Unit Encoder
 *
 * Copyright (C) 2013-2014 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#include <linux/export.h>

#include <drm/drm_bridge.h>
#include <drm/drm_crtc.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_panel.h>

#include "rcar_du_drv.h"
#include "rcar_du_encoder.h"
#include "rcar_du_kms.h"
#include "rcar_lvds.h"

/* -----------------------------------------------------------------------------
 * Encoder
 */

static const struct drm_encoder_helper_funcs encoder_helper_funcs = {
};

static const struct drm_encoder_funcs encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static unsigned int rcar_du_encoder_count_ports(struct device_yesde *yesde)
{
	struct device_yesde *ports;
	struct device_yesde *port;
	unsigned int num_ports = 0;

	ports = of_get_child_by_name(yesde, "ports");
	if (!ports)
		ports = of_yesde_get(yesde);

	for_each_child_of_yesde(ports, port) {
		if (of_yesde_name_eq(port, "port"))
			num_ports++;
	}

	of_yesde_put(ports);

	return num_ports;
}

int rcar_du_encoder_init(struct rcar_du_device *rcdu,
			 enum rcar_du_output output,
			 struct device_yesde *enc_yesde)
{
	struct rcar_du_encoder *renc;
	struct drm_encoder *encoder;
	struct drm_bridge *bridge;
	int ret;

	renc = devm_kzalloc(rcdu->dev, sizeof(*renc), GFP_KERNEL);
	if (renc == NULL)
		return -ENOMEM;

	rcdu->encoders[output] = renc;
	renc->output = output;
	encoder = rcar_encoder_to_drm_encoder(renc);

	dev_dbg(rcdu->dev, "initializing encoder %pOF for output %u\n",
		enc_yesde, output);

	/*
	 * Locate the DRM bridge from the DT yesde. For the DPAD outputs, if the
	 * DT yesde has a single port, assume that it describes a panel and
	 * create a panel bridge.
	 */
	if ((output == RCAR_DU_OUTPUT_DPAD0 ||
	     output == RCAR_DU_OUTPUT_DPAD1) &&
	    rcar_du_encoder_count_ports(enc_yesde) == 1) {
		struct drm_panel *panel = of_drm_find_panel(enc_yesde);

		if (IS_ERR(panel)) {
			ret = PTR_ERR(panel);
			goto done;
		}

		bridge = devm_drm_panel_bridge_add_typed(rcdu->dev, panel,
							 DRM_MODE_CONNECTOR_DPI);
		if (IS_ERR(bridge)) {
			ret = PTR_ERR(bridge);
			goto done;
		}
	} else {
		bridge = of_drm_find_bridge(enc_yesde);
		if (!bridge) {
			ret = -EPROBE_DEFER;
			goto done;
		}
	}

	/*
	 * On Gen3 skip the LVDS1 output if the LVDS1 encoder is used as a
	 * companion for LVDS0 in dual-link mode.
	 */
	if (rcdu->info->gen >= 3 && output == RCAR_DU_OUTPUT_LVDS1) {
		if (rcar_lvds_dual_link(bridge)) {
			ret = -ENOLINK;
			goto done;
		}
	}

	ret = drm_encoder_init(rcdu->ddev, encoder, &encoder_funcs,
			       DRM_MODE_ENCODER_NONE, NULL);
	if (ret < 0)
		goto done;

	drm_encoder_helper_add(encoder, &encoder_helper_funcs);

	/*
	 * Attach the bridge to the encoder. The bridge will create the
	 * connector.
	 */
	ret = drm_bridge_attach(encoder, bridge, NULL);
	if (ret) {
		drm_encoder_cleanup(encoder);
		return ret;
	}

done:
	if (ret < 0) {
		if (encoder->name)
			encoder->funcs->destroy(encoder);
		devm_kfree(rcdu->dev, renc);
	}

	return ret;
}
