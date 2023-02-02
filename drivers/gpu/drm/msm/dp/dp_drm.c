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

	drm_dbg_dp(dp->drm_dev, "is_connected = %s\n",
		(dp->is_connected) ? "true" : "false");

	return (dp->is_connected) ? connector_status_connected :
					connector_status_disconnected;
}

static int dp_bridge_atomic_check(struct drm_bridge *bridge,
			    struct drm_bridge_state *bridge_state,
			    struct drm_crtc_state *crtc_state,
			    struct drm_connector_state *conn_state)
{
	struct msm_dp *dp;

	dp = to_dp_bridge(bridge)->dp_display;

	drm_dbg_dp(dp->drm_dev, "is_connected = %s\n",
		(dp->is_connected) ? "true" : "false");

	/*
	 * There is no protection in the DRM framework to check if the display
	 * pipeline has been already disabled before trying to disable it again.
	 * Hence if the sink is unplugged, the pipeline gets disabled, but the
	 * crtc->active is still true. Any attempt to set the mode or manually
	 * disable this encoder will result in the crash.
	 *
	 * TODO: add support for telling the DRM subsystem that the pipeline is
	 * disabled by the hardware and thus all access to it should be forbidden.
	 * After that this piece of code can be removed.
	 */
	if (bridge->ops & DRM_BRIDGE_OP_HPD)
		return (dp->is_connected) ? 0 : -ENOTCONN;

	return 0;
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

	if (!connector)
		return 0;

	dp = to_dp_bridge(bridge)->dp_display;

	/* pluggable case assumes EDID is read when HPD */
	if (dp->is_connected) {
		rc = dp_display_get_modes(dp);
		if (rc <= 0) {
			DRM_ERROR("failed to get DP sink modes, rc=%d\n", rc);
			return rc;
		}
	} else {
		drm_dbg_dp(connector->dev, "No sink connected\n");
	}
	return rc;
}

static const struct drm_bridge_funcs dp_bridge_ops = {
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state   = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset           = drm_atomic_helper_bridge_reset,
	.enable       = dp_bridge_enable,
	.disable      = dp_bridge_disable,
	.post_disable = dp_bridge_post_disable,
	.mode_set     = dp_bridge_mode_set,
	.mode_valid   = dp_bridge_mode_valid,
	.get_modes    = dp_bridge_get_modes,
	.detect       = dp_bridge_detect,
	.atomic_check = dp_bridge_atomic_check,
	.hpd_enable   = dp_bridge_hpd_enable,
	.hpd_disable  = dp_bridge_hpd_disable,
	.hpd_notify   = dp_bridge_hpd_notify,
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

	/*
	 * Many ops only make sense for DP. Why?
	 * - Detect/HPD are used by DRM to know if a display is _physically_
	 *   there, not whether the display is powered on / finished initting.
	 *   On eDP we assume the display is always there because you can't
	 *   know until power is applied. If we don't implement the ops DRM will
	 *   assume our display is always there.
	 * - Currently eDP mode reading is driven by the panel driver. This
	 *   allows the panel driver to properly power itself on to read the
	 *   modes.
	 */
	if (!dp_display->is_edp) {
		bridge->ops =
			DRM_BRIDGE_OP_DETECT |
			DRM_BRIDGE_OP_HPD |
			DRM_BRIDGE_OP_MODES;
	}

	drm_bridge_add(bridge);

	rc = drm_bridge_attach(encoder, bridge, NULL, DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (rc) {
		DRM_ERROR("failed to attach bridge, rc=%d\n", rc);
		drm_bridge_remove(bridge);

		return ERR_PTR(rc);
	}

	if (dp_display->next_bridge) {
		rc = drm_bridge_attach(encoder,
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
struct drm_connector *dp_drm_connector_init(struct msm_dp *dp_display, struct drm_encoder *encoder)
{
	struct drm_connector *connector = NULL;

	connector = drm_bridge_connector_init(dp_display->drm_dev, encoder);
	if (IS_ERR(connector))
		return connector;

	drm_connector_attach_encoder(connector, encoder);

	return connector;
}
