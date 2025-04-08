// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 */

#include <linux/string_choices.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_crtc.h>

#include "msm_drv.h"
#include "msm_kms.h"
#include "dp_drm.h"

/**
 * msm_dp_bridge_detect - callback to determine if connector is connected
 * @bridge: Pointer to drm bridge structure
 * Returns: Bridge's 'is connected' status
 */
static enum drm_connector_status msm_dp_bridge_detect(struct drm_bridge *bridge)
{
	struct msm_dp *dp;

	dp = to_dp_bridge(bridge)->msm_dp_display;

	drm_dbg_dp(dp->drm_dev, "link_ready = %s\n",
		str_true_false(dp->link_ready));

	return (dp->link_ready) ? connector_status_connected :
					connector_status_disconnected;
}

static int msm_dp_bridge_atomic_check(struct drm_bridge *bridge,
			    struct drm_bridge_state *bridge_state,
			    struct drm_crtc_state *crtc_state,
			    struct drm_connector_state *conn_state)
{
	struct msm_dp *dp;

	dp = to_dp_bridge(bridge)->msm_dp_display;

	drm_dbg_dp(dp->drm_dev, "link_ready = %s\n",
		str_true_false(dp->link_ready));

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
		return (dp->link_ready) ? 0 : -ENOTCONN;

	return 0;
}


/**
 * msm_dp_bridge_get_modes - callback to add drm modes via drm_mode_probed_add()
 * @bridge: Poiner to drm bridge
 * @connector: Pointer to drm connector structure
 * Returns: Number of modes added
 */
static int msm_dp_bridge_get_modes(struct drm_bridge *bridge, struct drm_connector *connector)
{
	int rc = 0;
	struct msm_dp *dp;

	if (!connector)
		return 0;

	dp = to_dp_bridge(bridge)->msm_dp_display;

	/* pluggable case assumes EDID is read when HPD */
	if (dp->link_ready) {
		rc = msm_dp_display_get_modes(dp);
		if (rc <= 0) {
			DRM_ERROR("failed to get DP sink modes, rc=%d\n", rc);
			return rc;
		}
	} else {
		drm_dbg_dp(connector->dev, "No sink connected\n");
	}
	return rc;
}

static void msm_dp_bridge_debugfs_init(struct drm_bridge *bridge, struct dentry *root)
{
	struct msm_dp *dp = to_dp_bridge(bridge)->msm_dp_display;

	msm_dp_display_debugfs_init(dp, root, false);
}

static const struct drm_bridge_funcs msm_dp_bridge_ops = {
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state   = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset           = drm_atomic_helper_bridge_reset,
	.atomic_enable          = msm_dp_bridge_atomic_enable,
	.atomic_disable         = msm_dp_bridge_atomic_disable,
	.atomic_post_disable    = msm_dp_bridge_atomic_post_disable,
	.mode_set     = msm_dp_bridge_mode_set,
	.mode_valid   = msm_dp_bridge_mode_valid,
	.get_modes    = msm_dp_bridge_get_modes,
	.detect       = msm_dp_bridge_detect,
	.atomic_check = msm_dp_bridge_atomic_check,
	.hpd_enable   = msm_dp_bridge_hpd_enable,
	.hpd_disable  = msm_dp_bridge_hpd_disable,
	.hpd_notify   = msm_dp_bridge_hpd_notify,
	.debugfs_init = msm_dp_bridge_debugfs_init,
};

static int msm_edp_bridge_atomic_check(struct drm_bridge *drm_bridge,
				   struct drm_bridge_state *bridge_state,
				   struct drm_crtc_state *crtc_state,
				   struct drm_connector_state *conn_state)
{
	struct msm_dp *dp = to_dp_bridge(drm_bridge)->msm_dp_display;

	if (WARN_ON(!conn_state))
		return -ENODEV;

	conn_state->self_refresh_aware = dp->psr_supported;

	if (!conn_state->crtc || !crtc_state)
		return 0;

	if (crtc_state->self_refresh_active && !dp->psr_supported)
		return -EINVAL;

	return 0;
}

static void msm_edp_bridge_atomic_enable(struct drm_bridge *drm_bridge,
					 struct drm_atomic_state *state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state;
	struct msm_dp_bridge *msm_dp_bridge = to_dp_bridge(drm_bridge);
	struct msm_dp *dp = msm_dp_bridge->msm_dp_display;

	/*
	 * Check the old state of the crtc to determine if the panel
	 * was put into psr state previously by the msm_edp_bridge_atomic_disable.
	 * If the panel is in psr, just exit psr state and skip the full
	 * bridge enable sequence.
	 */
	crtc = drm_atomic_get_new_crtc_for_encoder(state,
						   drm_bridge->encoder);
	if (!crtc)
		return;

	old_crtc_state = drm_atomic_get_old_crtc_state(state, crtc);

	if (old_crtc_state && old_crtc_state->self_refresh_active) {
		msm_dp_display_set_psr(dp, false);
		return;
	}

	msm_dp_bridge_atomic_enable(drm_bridge, state);
}

static void msm_edp_bridge_atomic_disable(struct drm_bridge *drm_bridge,
					  struct drm_atomic_state *atomic_state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *new_crtc_state = NULL, *old_crtc_state = NULL;
	struct msm_dp_bridge *msm_dp_bridge = to_dp_bridge(drm_bridge);
	struct msm_dp *dp = msm_dp_bridge->msm_dp_display;

	crtc = drm_atomic_get_old_crtc_for_encoder(atomic_state,
						   drm_bridge->encoder);
	if (!crtc)
		goto out;

	new_crtc_state = drm_atomic_get_new_crtc_state(atomic_state, crtc);
	if (!new_crtc_state)
		goto out;

	old_crtc_state = drm_atomic_get_old_crtc_state(atomic_state, crtc);
	if (!old_crtc_state)
		goto out;

	/*
	 * Set self refresh mode if current crtc state is active.
	 *
	 * If old crtc state is active, then this is a display disable
	 * call while the sink is in psr state. So, exit psr here.
	 * The eDP controller will be disabled in the
	 * msm_edp_bridge_atomic_post_disable function.
	 *
	 * We observed sink is stuck in self refresh if psr exit is skipped
	 * when display disable occurs while the sink is in psr state.
	 */
	if (new_crtc_state->self_refresh_active) {
		msm_dp_display_set_psr(dp, true);
		return;
	} else if (old_crtc_state->self_refresh_active) {
		msm_dp_display_set_psr(dp, false);
		return;
	}

out:
	msm_dp_bridge_atomic_disable(drm_bridge, atomic_state);
}

static void msm_edp_bridge_atomic_post_disable(struct drm_bridge *drm_bridge,
					       struct drm_atomic_state *atomic_state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *new_crtc_state = NULL;

	crtc = drm_atomic_get_old_crtc_for_encoder(atomic_state,
						   drm_bridge->encoder);
	if (!crtc)
		return;

	new_crtc_state = drm_atomic_get_new_crtc_state(atomic_state, crtc);
	if (!new_crtc_state)
		return;

	/*
	 * Self refresh mode is already set in msm_edp_bridge_atomic_disable.
	 */
	if (new_crtc_state->self_refresh_active)
		return;

	msm_dp_bridge_atomic_post_disable(drm_bridge, atomic_state);
}

/**
 * msm_edp_bridge_mode_valid - callback to determine if specified mode is valid
 * @bridge: Pointer to drm bridge structure
 * @info: display info
 * @mode: Pointer to drm mode structure
 * Returns: Validity status for specified mode
 */
static enum drm_mode_status msm_edp_bridge_mode_valid(struct drm_bridge *bridge,
					  const struct drm_display_info *info,
					  const struct drm_display_mode *mode)
{
	struct msm_dp *dp;
	int mode_pclk_khz = mode->clock;

	dp = to_dp_bridge(bridge)->msm_dp_display;

	if (!dp || !mode_pclk_khz || !dp->connector) {
		DRM_ERROR("invalid params\n");
		return -EINVAL;
	}

	if (msm_dp_wide_bus_available(dp))
		mode_pclk_khz /= 2;

	if (mode_pclk_khz > DP_MAX_PIXEL_CLK_KHZ)
		return MODE_CLOCK_HIGH;

	/*
	 * The eDP controller currently does not have a reliable way of
	 * enabling panel power to read sink capabilities. So, we rely
	 * on the panel driver to populate only supported modes for now.
	 */
	return MODE_OK;
}

static void msm_edp_bridge_debugfs_init(struct drm_bridge *bridge, struct dentry *root)
{
	struct msm_dp *dp = to_dp_bridge(bridge)->msm_dp_display;

	msm_dp_display_debugfs_init(dp, root, true);
}

static const struct drm_bridge_funcs msm_edp_bridge_ops = {
	.atomic_enable = msm_edp_bridge_atomic_enable,
	.atomic_disable = msm_edp_bridge_atomic_disable,
	.atomic_post_disable = msm_edp_bridge_atomic_post_disable,
	.mode_set = msm_dp_bridge_mode_set,
	.mode_valid = msm_edp_bridge_mode_valid,
	.atomic_reset = drm_atomic_helper_bridge_reset,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_check = msm_edp_bridge_atomic_check,
	.debugfs_init = msm_edp_bridge_debugfs_init,
};

int msm_dp_bridge_init(struct msm_dp *msm_dp_display, struct drm_device *dev,
		   struct drm_encoder *encoder, bool yuv_supported)
{
	int rc;
	struct msm_dp_bridge *msm_dp_bridge;
	struct drm_bridge *bridge;

	msm_dp_bridge = devm_kzalloc(dev->dev, sizeof(*msm_dp_bridge), GFP_KERNEL);
	if (!msm_dp_bridge)
		return -ENOMEM;

	msm_dp_bridge->msm_dp_display = msm_dp_display;

	bridge = &msm_dp_bridge->bridge;
	bridge->funcs = msm_dp_display->is_edp ? &msm_edp_bridge_ops : &msm_dp_bridge_ops;
	bridge->type = msm_dp_display->connector_type;
	bridge->ycbcr_420_allowed = yuv_supported;

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
	if (!msm_dp_display->is_edp) {
		bridge->ops =
			DRM_BRIDGE_OP_DETECT |
			DRM_BRIDGE_OP_HPD |
			DRM_BRIDGE_OP_MODES;
	}

	rc = devm_drm_bridge_add(dev->dev, bridge);
	if (rc) {
		DRM_ERROR("failed to add bridge, rc=%d\n", rc);

		return rc;
	}

	rc = drm_bridge_attach(encoder, bridge, NULL, DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (rc) {
		DRM_ERROR("failed to attach bridge, rc=%d\n", rc);

		return rc;
	}

	if (msm_dp_display->next_bridge) {
		rc = drm_bridge_attach(encoder,
					msm_dp_display->next_bridge, bridge,
					DRM_BRIDGE_ATTACH_NO_CONNECTOR);
		if (rc < 0) {
			DRM_ERROR("failed to attach panel bridge: %d\n", rc);
			return rc;
		}
	}

	return 0;
}

/* connector initialization */
struct drm_connector *msm_dp_drm_connector_init(struct msm_dp *msm_dp_display,
					    struct drm_encoder *encoder)
{
	struct drm_connector *connector = NULL;

	connector = drm_bridge_connector_init(msm_dp_display->drm_dev, encoder);
	if (IS_ERR(connector))
		return connector;

	if (!msm_dp_display->is_edp)
		drm_connector_attach_dp_subconnector_property(connector);

	drm_connector_attach_encoder(connector, encoder);

	return connector;
}
