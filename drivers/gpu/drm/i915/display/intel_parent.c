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

#include <drm/drm_print.h>
#include <drm/intel/display_parent_interface.h>

#include "intel_display_core.h"
#include "intel_parent.h"

/* hdcp */
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

/* irq */
bool intel_parent_irq_enabled(struct intel_display *display)
{
	return display->parent->irq->enabled(display->drm);
}

void intel_parent_irq_synchronize(struct intel_display *display)
{
	display->parent->irq->synchronize(display->drm);
}

/* panic */
struct intel_panic *intel_parent_panic_alloc(struct intel_display *display)
{
	return display->parent->panic->alloc();
}

int intel_parent_panic_setup(struct intel_display *display, struct intel_panic *panic, struct drm_scanout_buffer *sb)
{
	return display->parent->panic->setup(panic, sb);
}

void intel_parent_panic_finish(struct intel_display *display, struct intel_panic *panic)
{
	display->parent->panic->finish(panic);
}

/* pc8 */
void intel_parent_pc8_block(struct intel_display *display)
{
	if (drm_WARN_ON_ONCE(display->drm, !display->parent->pc8))
		return;

	display->parent->pc8->block(display->drm);
}

void intel_parent_pc8_unblock(struct intel_display *display)
{
	if (drm_WARN_ON_ONCE(display->drm, !display->parent->pc8))
		return;

	display->parent->pc8->unblock(display->drm);
}

/* rps */
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

/* stolen */
int intel_parent_stolen_insert_node_in_range(struct intel_display *display,
					     struct intel_stolen_node *node, u64 size,
					     unsigned int align, u64 start, u64 end)
{
	return display->parent->stolen->insert_node_in_range(node, size, align, start, end);
}

int intel_parent_stolen_insert_node(struct intel_display *display, struct intel_stolen_node *node, u64 size,
				    unsigned int align)
{
	if (drm_WARN_ON_ONCE(display->drm, !display->parent->stolen->insert_node))
		return -ENODEV;

	return display->parent->stolen->insert_node(node, size, align);
}

void intel_parent_stolen_remove_node(struct intel_display *display,
				     struct intel_stolen_node *node)
{
	display->parent->stolen->remove_node(node);
}

bool intel_parent_stolen_initialized(struct intel_display *display)
{
	return display->parent->stolen->initialized(display->drm);
}

bool intel_parent_stolen_node_allocated(struct intel_display *display,
					const struct intel_stolen_node *node)
{
	return display->parent->stolen->node_allocated(node);
}

u32 intel_parent_stolen_node_offset(struct intel_display *display, struct intel_stolen_node *node)
{
	return display->parent->stolen->node_offset(node);
}

u64 intel_parent_stolen_area_address(struct intel_display *display)
{
	if (drm_WARN_ON_ONCE(display->drm, !display->parent->stolen->area_address))
		return 0;

	return display->parent->stolen->area_address(display->drm);
}

u64 intel_parent_stolen_area_size(struct intel_display *display)
{
	if (drm_WARN_ON_ONCE(display->drm, !display->parent->stolen->area_size))
		return 0;

	return display->parent->stolen->area_size(display->drm);
}

u64 intel_parent_stolen_node_address(struct intel_display *display, struct intel_stolen_node *node)
{
	return display->parent->stolen->node_address(node);
}

u64 intel_parent_stolen_node_size(struct intel_display *display, const struct intel_stolen_node *node)
{
	return display->parent->stolen->node_size(node);
}

struct intel_stolen_node *intel_parent_stolen_node_alloc(struct intel_display *display)
{
	return display->parent->stolen->node_alloc(display->drm);
}

void intel_parent_stolen_node_free(struct intel_display *display, const struct intel_stolen_node *node)
{
	display->parent->stolen->node_free(node);
}

/* generic */
void intel_parent_fence_priority_display(struct intel_display *display, struct dma_fence *fence)
{
	if (display->parent->fence_priority_display)
		display->parent->fence_priority_display(fence);
}

bool intel_parent_has_auxccs(struct intel_display *display)
{
	return display->parent->has_auxccs && display->parent->has_auxccs(display->drm);
}

bool intel_parent_has_fenced_regions(struct intel_display *display)
{
	return display->parent->has_fenced_regions && display->parent->has_fenced_regions(display->drm);
}

bool intel_parent_vgpu_active(struct intel_display *display)
{
	return display->parent->vgpu_active && display->parent->vgpu_active(display->drm);
}
