// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_crtc.h>

#include "msm_drv.h"
#include "msm_kms.h"
#include "dp_drm.h"

/**
 * dp_bridge_detect - callback to determine if connector is connected
 * @bridge: Pointer to drm bridge structure
 * Returns: Bridge's 'is connected' status
 */
static enum drm_connector_status dp_bridge_detect(struct drm_bridge *bridge)
{
	struct msm_dp *dp;

	dp = to_dp_bridge(bridge)->dp_display;

	DRM_DEBUG_DP("is_connected = %s\n",
		(dp->is_connected) ? "true" : "false");

	return (dp->is_connected) ? connector_status_connected :
					connector_status_disconnected;
}

/**
 * dp_bridge_get_modes - callback to add drm modes via drm_mode_probed_add()
 * @bridge: Poiner to drm bridge
 * @connector: Pointer to drm connector structure
 * Returns: Number of modes added
 */
static int dp_bridge_get_modes(struct drm_bridge *bridge, struct drm_connector *connector)
{
	int rc = 0;
	struct msm_dp *dp;
	struct dp_display_mode *dp_mode = NULL;
	struct drm_display_mode *m, drm_mode;

	if (!connector)
		return 0;

	dp = to_dp_bridge(bridge)->dp_display;

	dp_mode = kzalloc(sizeof(*dp_mode),  GFP_KERNEL);
	if (!dp_mode)
		return 0;

	/* pluggable case assumes EDID is read when HPD */
	if (dp->is_connected) {
		/*
		 *The get_modes() function might return one mode that is stored
		 * in dp_mode when compliance test is in progress. If not, the
		 * return value is equal to the total number of modes supported
		 * by the sink
		 */
		rc = dp_display_get_modes(dp, dp_mode);
		if (rc <= 0) {
			DRM_ERROR("failed to get DP sink modes, rc=%d\n", rc);
			kfree(dp_mode);
			return rc;
		}
		if (dp_mode->drm_mode.clock) { /* valid DP mode */
			memset(&drm_mode, 0x0, sizeof(drm_mode));
			drm_mode_copy(&drm_mode, &dp_mode->drm_mode);
			m = drm_mode_duplicate(connector->dev, &drm_mode);
			if (!m) {
				DRM_ERROR("failed to add mode %ux%u\n",
				       drm_mode.hdisplay,
				       drm_mode.vdisplay);
				kfree(dp_mode);
				return 0;
			}
			drm_mode_probed_add(connector, m);
		}
	} else {
		DRM_DEBUG_DP("No sink connected\n");
	}
	kfree(dp_mode);
	return rc;
}

static const struct drm_bridge_funcs dp_bridge_ops = {
	.enable       = dp_bridge_enable,
	.disable      = dp_bridge_disable,
	.post_disable = dp_bridge_post_disable,
	.mode_set     = dp_bridge_mode_set,
	.mode_valid   = dp_bridge_mode_valid,
	.get_modes    = dp_bridge_get_modes,
	.detect       = dp_bridge_detect,
};

struct drm_bridge *dp_bridge_init(struct msm_dp *dp_display, struct drm_device *dev,
			struct drm_encoder *encoder)
{
	int rc;
	struct msm_dp_bridge *dp_bridge;
	struct drm_bridge *bridge;

	dp_bridge = devm_kzalloc(dev->dev, sizeof(*dp_bridge), GFP_KERNEL);
	if (!dp_bridge)
		return ERR_PTR(-ENOMEM);

	dp_bridge->dp_display = dp_display;

	bridge = &dp_bridge->bridge;
	bridge->funcs = &dp_bridge_ops;
	bridge->type = dp_display->connector_type;

	bridge->ops =
		DRM_BRIDGE_OP_DETECT |
		DRM_BRIDGE_OP_HPD |
		DRM_BRIDGE_OP_MODES;

	drm_bridge_add(bridge);

	rc = drm_bridge_attach(encoder, bridge, NULL, DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (rc) {
		DRM_ERROR("failed to attach bridge, rc=%d\n", rc);
		drm_bridge_remove(bridge);

		return ERR_PTR(rc);
	}

	if (dp_display->next_bridge) {
		rc = drm_bridge_attach(dp_display->encoder,
					dp_display->next_bridge, bridge,
					DRM_BRIDGE_ATTACH_NO_CONNECTOR);
		if (rc < 0) {
			DRM_ERROR("failed to attach panel bridge: %d\n", rc);
			drm_bridge_remove(bridge);
			return ERR_PTR(rc);
		}
	}

	return bridge;
}

/* connector initialization */
struct drm_connector *dp_drm_connector_init(struct msm_dp *dp_display)
{
	struct drm_connector *connector = NULL;

	connector = drm_bridge_connector_init(dp_display->drm_dev, dp_display->encoder);
	if (IS_ERR(connector))
		return connector;

	drm_connector_attach_encoder(connector, dp_display->encoder);

	return connector;
}
