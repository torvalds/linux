// SPDX-License-Identifier: MIT
/* Copyright © 2025 Intel Corporation */

/*
 * Convenience wrapper functions to call the parent interface functions:
 *
 * - display->parent->SUBSTRUCT->FUNCTION()
 * - display->parent->FUNCTION()
 *
 * All functions here should be named accordingly:
 *
 * - intel_parent_SUBSTRUCT_FUNCTION()
 * - intel_parent_FUNCTION()
 *
 * These functions may use display driver specific types for parameters and
 * return values, translating them to and from the generic types used in the
 * function pointer interface.
 */

#include <drm/intel/display_parent_interface.h>

#include "intel_display_core.h"
#include "intel_parent.h"

ssize_t intel_parent_hdcp_gsc_msg_send(struct intel_display *display,
				       struct intel_hdcp_gsc_context *gsc_context,
				       void *msg_in, size_t msg_in_len,
				       void *msg_out, size_t msg_out_len)
{
	return display->parent->hdcp->gsc_msg_send(gsc_context, msg_in, msg_in_len, msg_out, msg_out_len);
}

bool intel_parent_hdcp_gsc_check_status(struct intel_display *display)
{
	return display->parent->hdcp->gsc_check_status(display->drm);
}

struct intel_hdcp_gsc_context *intel_parent_hdcp_gsc_context_alloc(struct intel_display *display)
{
	return display->parent->hdcp->gsc_context_alloc(display->drm);
}

void intel_parent_hdcp_gsc_context_free(struct intel_display *display,
					struct intel_hdcp_gsc_context *gsc_context)
{
	display->parent->hdcp->gsc_context_free(gsc_context);
}

bool intel_parent_irq_enabled(struct intel_display *display)
{
	return display->parent->irq->enabled(display->drm);
}

void intel_parent_irq_synchronize(struct intel_display *display)
{
	display->parent->irq->synchronize(display->drm);
}

bool intel_parent_rps_available(struct intel_display *display)
{
	return display->parent->rps;
}

void intel_parent_rps_boost_if_not_started(struct intel_display *display, struct dma_fence *fence)
{
	if (display->parent->rps)
		display->parent->rps->boost_if_not_started(fence);
}

void intel_parent_rps_mark_interactive(struct intel_display *display, bool interactive)
{
	if (display->parent->rps)
		display->parent->rps->mark_interactive(display->drm, interactive);
}

void intel_parent_rps_ilk_irq_handler(struct intel_display *display)
{
	if (display->parent->rps)
		display->parent->rps->ilk_irq_handler(display->drm);
}

bool intel_parent_vgpu_active(struct intel_display *display)
{
	return display->parent->vgpu_active && display->parent->vgpu_active(display->drm);
}

bool intel_parent_has_fenced_regions(struct intel_display *display)
{
	return display->parent->has_fenced_regions && display->parent->has_fenced_regions(display->drm);
}

void intel_parent_fence_priority_display(struct intel_display *display, struct dma_fence *fence)
{
	if (display->parent->fence_priority_display)
		display->parent->fence_priority_display(fence);
}
