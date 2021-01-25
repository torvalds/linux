// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 *
 */

#include "i915_drv.h"
#include "intel_display_types.h"
#include "intel_vrr.h"

bool intel_vrr_is_capable(struct drm_connector *connector)
{
	struct intel_dp *intel_dp;
	const struct drm_display_info *info = &connector->display_info;
	struct drm_i915_private *i915 = to_i915(connector->dev);

	if (connector->connector_type != DRM_MODE_CONNECTOR_eDP &&
	    connector->connector_type != DRM_MODE_CONNECTOR_DisplayPort)
		return false;

	intel_dp = intel_attached_dp(to_intel_connector(connector));
	/*
	 * DP Sink is capable of Variable refresh video timings if
	 * Ignore MSA bit is set in DPCD.
	 * EDID monitor range also should be atleast 10 for reasonable
	 * Adaptive sync/ VRR end user experience.
	 */
	return HAS_VRR(i915) &&
		drm_dp_sink_can_do_video_without_timing_msa(intel_dp->dpcd) &&
		info->monitor_range.max_vfreq - info->monitor_range.min_vfreq > 10;
}
