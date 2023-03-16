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
	void *hdcp_cmd;
};

ssize_t intel_hdcp_gsc_msg_send(struct drm_i915_private *i915, u8 *msg_in,
				size_t msg_in_len, u8 *msg_out,
				size_t msg_out_len);

#endif /* __INTEL_HDCP_GCS_H__ */
