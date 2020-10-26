// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_crtc.h>

#include "msm_drv.h"
#include "msm_kms.h"
#include "dp_drm.h"

struct dp_connector {
	struct drm_connector base;
	struct msm_dp *dp_display;
};
#define to_dp_connector(x) container_of(x, struct dp_connector, base)

/**
 * dp_connector_detect - callback to determine if connector is connected
 * @conn: Pointer to drm connector structure
 * @force: Force detect setting from drm framework
 * Returns: Connector 'is connected' status
 */
static enum drm_connector_status dp_connector_detect(struct drm_connector *conn,
		bool force)
{
	struct msm_dp *dp;

	dp = to_dp_connector(conn)->dp_display;

	DRM_DEBUG_DP("is_connected = %s\n",
		(dp->is_connected) ? "true" : "false");

	return (dp->is_connected) ? connector_status_connected :
					connector_status_disconnected;
}

/**
 * dp_connector_get_modes - callback to add drm modes via drm_mode_probed_add()
 * @connector: Pointer to drm connector structure
 * Returns: Number of modes added
 */
static int dp_connector_get_modes(struct drm_connector *connector)
{
	int rc = 0;
	struct msm_dp *dp;
	struct dp_display_mode *dp_mode = NULL;
	struct drm_display_mode *m, drm_mode;

	if (!connector)
		return 0;

	dp = to_dp_connector(connector)->dp_display;

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

/**
 * dp_connector_mode_valid - callback to determine if specified mode is valid
 * @connector: Pointer to drm connector structure
 * @mode: Pointer to drm mode structure
 * Returns: Validity status for specified mode
 */
static enum drm_mode_status dp_connector_mode_valid(
		struct drm_connector *connector,
		struct drm_display_mode *mode)
{
	struct msm_dp *dp_disp;

	dp_disp = to_dp_connector(connector)->dp_display;

	if ((dp_disp->max_pclk_khz <= 0) ||
			(dp_disp->max_pclk_khz > DP_MAX_PIXEL_CLK_KHZ) ||
			(mode->clock > dp_disp->max_pclk_khz))
		return MODE_BAD;

	return dp_display_validate_mode(dp_disp, mode->clock);
}

static const struct drm_connector_funcs dp_connector_funcs = {
	.detect = dp_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_connector_helper_funcs dp_connector_helper_funcs = {
	.get_modes = dp_connector_get_modes,
	.mode_valid = dp_connector_mode_valid,
};

/* connector initialization */
struct drm_connector *dp_drm_connector_init(struct msm_dp *dp_display)
{
	struct drm_connector *connector = NULL;
	struct dp_connector *dp_connector;
	int ret;

	dp_connector = devm_kzalloc(dp_display->drm_dev->dev,
					sizeof(*dp_connector),
					GFP_KERNEL);
	if (!dp_connector)
		return ERR_PTR(-ENOMEM);

	dp_connector->dp_display = dp_display;

	connector = &dp_connector->base;

	ret = drm_connector_init(dp_display->drm_dev, connector,
			&dp_connector_funcs,
			DRM_MODE_CONNECTOR_DisplayPort);
	if (ret)
		return ERR_PTR(ret);

	drm_connector_helper_add(connector, &dp_connector_helper_funcs);

	/*
	 * Enable HPD to let hpd event is handled when cable is connected.
	 */
	connector->polled = DRM_CONNECTOR_POLL_HPD;

	drm_connector_attach_encoder(connector, dp_display->encoder);

	return connector;
}
