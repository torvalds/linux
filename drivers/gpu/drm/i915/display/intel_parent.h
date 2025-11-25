/* SPDX-License-Identifier: MIT */
/* Copyright © 2025 Intel Corporation */

#ifndef __INTEL_PARENT_H__
#define __INTEL_PARENT_H__

#include <linux/types.h>

struct dma_fence;
struct intel_display;
struct intel_hdcp_gsc_context;

ssize_t intel_parent_hdcp_gsc_msg_send(struct intel_display *display,
				       struct intel_hdcp_gsc_context *gsc_context,
				       void *msg_in, size_t msg_in_len,
				       void *msg_out, size_t msg_out_len);
bool intel_parent_hdcp_gsc_check_status(struct intel_display *display);
struct intel_hdcp_gsc_context *intel_parent_hdcp_gsc_context_alloc(struct intel_display *display);
void intel_parent_hdcp_gsc_context_free(struct intel_display *display,
					struct intel_hdcp_gsc_context *gsc_context);

bool intel_parent_irq_enabled(struct intel_display *display);
void intel_parent_irq_synchronize(struct intel_display *display);

bool intel_parent_rps_available(struct intel_display *display);
void intel_parent_rps_boost_if_not_started(struct intel_display *display, struct dma_fence *fence);
void intel_parent_rps_mark_interactive(struct intel_display *display, bool interactive);
void intel_parent_rps_ilk_irq_handler(struct intel_display *display);

bool intel_parent_vgpu_active(struct intel_display *display);

bool intel_parent_has_fenced_regions(struct intel_display *display);

void intel_parent_fence_priority_display(struct intel_display *display, struct dma_fence *fence);

#endif /* __INTEL_PARENT_H__ */
