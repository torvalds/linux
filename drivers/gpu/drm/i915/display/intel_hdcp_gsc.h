/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_HDCP_GSC_H__
#define __INTEL_HDCP_GSC_H__

#include <linux/err.h>
#include <linux/types.h>

struct drm_i915_private;

struct intel_hdcp_gsc_message {
	struct i915_vma *vma;
	void *hdcp_cmd_in;
	void *hdcp_cmd_out;
};

bool intel_hdcp_gsc_cs_required(struct drm_i915_private *i915);
ssize_t intel_hdcp_gsc_msg_send(struct drm_i915_private *i915, u8 *msg_in,
				size_t msg_in_len, u8 *msg_out,
				size_t msg_out_len);
int intel_hdcp_gsc_init(struct drm_i915_private *i915);
void intel_hdcp_gsc_fini(struct drm_i915_private *i915);

#endif /* __INTEL_HDCP_GCS_H__ */
