// SPDX-License-Identifier: MIT
/*
 * Copyright 2023, Intel Corporation.
 */

#include "i915_drv.h"
#include "intel_hdcp_gsc.h"

int intel_hdcp_gsc_init(struct drm_i915_private *i915)
{
	drm_info(&i915->drm, "HDCP support not yet implemented\n");
	return -ENODEV;
}

void intel_hdcp_gsc_fini(struct drm_i915_private *i915)
{
}

ssize_t intel_hdcp_gsc_msg_send(struct drm_i915_private *i915, u8 *msg_in,
				size_t msg_in_len, u8 *msg_out,
				size_t msg_out_len)
{
	return -ENODEV;
}
