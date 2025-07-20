/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_HDCP_GSC_H__
#define __INTEL_HDCP_GSC_H__

#include <linux/types.h>

struct drm_device;
struct intel_hdcp_gsc_context;

ssize_t intel_hdcp_gsc_msg_send(struct intel_hdcp_gsc_context *gsc_context,
				void *msg_in, size_t msg_in_len,
				void *msg_out, size_t msg_out_len);
bool intel_hdcp_gsc_check_status(struct drm_device *drm);

struct intel_hdcp_gsc_context *intel_hdcp_gsc_context_alloc(struct drm_device *drm);
void intel_hdcp_gsc_context_free(struct intel_hdcp_gsc_context *gsc_context);

#endif /* __INTEL_HDCP_GCS_H__ */
