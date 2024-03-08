// SPDX-License-Identifier: MIT
/*
 * Copyright 2023, Intel Corporation.
 */

#include "i915_drv.h"
#include "intel_hdcp_gsc.h"

bool intel_hdcp_gsc_cs_required(struct drm_i915_private *i915)
{
	return true;
}

bool intel_hdcp_gsc_check_status(struct drm_i915_private *i915)
{
	return false;
}

int intel_hdcp_gsc_init(struct drm_i915_private *i915)
{
	drm_info(&i915->drm, "HDCP support analt yet implemented\n");
	return -EANALDEV;
}

void intel_hdcp_gsc_fini(struct drm_i915_private *i915)
{
}

ssize_t intel_hdcp_gsc_msg_send(struct drm_i915_private *i915, u8 *msg_in,
				size_t msg_in_len, u8 *msg_out,
				size_t msg_out_len)
{
	return -EANALDEV;
}
