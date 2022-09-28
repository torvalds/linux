// SPDX-License-Identifier: GPL-2.0
/*
 * ZynqMP DisplayPort Subsystem - KMS API
 *
 * Copyright (C) 2017 - 2021 Xilinx, Inc.
 *
 * Authors:
 * - Hyun Woo Kwon <hyun.kwon@xilinx.com>
 * - Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_connector.h>
#include <drm/drm_encoder.h>
#include <drm/drm_simple_kms_helper.h>

#include "zynqmp_disp.h"
#include "zynqmp_dp.h"
#include "zynqmp_dpsub.h"
#include "zynqmp_kms.h"

/* -----------------------------------------------------------------------------
 * Initialization
 */

int zynqmp_dpsub_kms_init(struct zynqmp_dpsub *dpsub)
{
	struct drm_encoder *encoder = &dpsub->encoder;
	struct drm_connector *connector;
	int ret;

	/*
	 * Initialize the DISP and DP components. This will creates planes,
	 * CRTC, and a bridge for the DP encoder.
	 */
	ret = zynqmp_disp_drm_init(dpsub);
	if (ret)
		return ret;

	ret = zynqmp_dp_drm_init(dpsub);
	if (ret)
		return ret;

	/* Create the encoder and attach the bridge. */
	encoder->possible_crtcs |= zynqmp_disp_get_crtc_mask(dpsub->disp);
	drm_simple_encoder_init(&dpsub->drm, encoder, DRM_MODE_ENCODER_NONE);

	ret = drm_bridge_attach(encoder, dpsub->bridge, NULL,
				DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (ret) {
		dev_err(dpsub->dev, "failed to attach bridge to encoder\n");
		return ret;
	}

	/* Create the connector for the chain of bridges. */
	connector = drm_bridge_connector_init(&dpsub->drm, encoder);
	if (IS_ERR(connector)) {
		dev_err(dpsub->dev, "failed to created connector\n");
		return PTR_ERR(connector);
	}

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret < 0) {
		dev_err(dpsub->dev, "failed to attach connector to encoder\n");
		return ret;
	}

	return 0;
}
