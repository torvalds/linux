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

/* dpt */
struct intel_dpt *intel_parent_dpt_create(struct intel_display *display,
					  struct drm_gem_object *obj, size_t size)
{
	if (display->parent->dpt)
		return display->parent->dpt->create(obj, size);

	return NULL;
}

void intel_parent_dpt_destroy(struct intel_display *display, struct intel_dpt *dpt)
{
	if (display->parent->dpt)
		display->parent->dpt->destroy(dpt);
}

void intel_parent_dpt_suspend(struct intel_display *display, struct intel_dpt *dpt)
{
	if (display->parent->dpt)
		display->parent->dpt->suspend(dpt);
}

void intel_parent_dpt_resume(struct intel_display *display, struct intel_dpt *dpt)
{
	if (display->parent->dpt)
		display->parent->dpt->resume(dpt);
}

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

/* overlay */
bool intel_parent_overlay_is_active(struct intel_display *display)
{
	return display->parent->overlay->is_active(display->drm);
}

int intel_parent_overlay_on(struct intel_display *display,
			    u32 frontbuffer_bits)
{
	return display->parent->overlay->overlay_on(display->drm,
						    frontbuffer_bits);
}

int intel_parent_overlay_continue(struct intel_display *display,
				  struct i915_vma *vma,
				  bool load_polyphase_filter)
{
	return display->parent->overlay->overlay_continue(display->drm, vma,
							  load_polyphase_filter);
}

int intel_parent_overlay_off(struct intel_display *display)
{
	return display->parent->overlay->overlay_off(display->drm);
}

int intel_parent_overlay_recover_from_interrupt(struct intel_display *display)
{
	return display->parent->overlay->recover_from_interrupt(display->drm);
}

int intel_parent_overlay_release_old_vid(struct intel_display *display)
{
	return display->parent->overlay->release_old_vid(display->drm);
}

void intel_parent_overlay_reset(struct intel_display *display)
{
	display->parent->overlay->reset(display->drm);
}

struct i915_vma *intel_parent_overlay_pin_fb(struct intel_display *display,
					     struct drm_gem_object *obj,
					     u32 *offset)
{
	return display->parent->overlay->pin_fb(display->drm, obj, offset);
}

void intel_parent_overlay_unpin_fb(struct intel_display *display,
				   struct i915_vma *vma)
{
	return display->parent->overlay->unpin_fb(display->drm, vma);
}

struct drm_gem_object *intel_parent_overlay_obj_lookup(struct intel_display *display,
						       struct drm_file *filp,
						       u32 handle)
{
	return display->parent->overlay->obj_lookup(display->drm,
						    filp, handle);
}

void __iomem *intel_parent_overlay_setup(struct intel_display *display,
					 bool needs_physical)
{
	if (drm_WARN_ON_ONCE(display->drm, !display->parent->overlay))
		return ERR_PTR(-ENODEV);

	return display->parent->overlay->setup(display->drm, needs_physical);
}

void intel_parent_overlay_cleanup(struct intel_display *display)
{
	display->parent->overlay->cleanup(display->drm);
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

/* pcode */
int intel_parent_pcode_read(struct intel_display *display, u32 mbox, u32 *val, u32 *val1)
{
	return display->parent->pcode->read(display->drm, mbox, val, val1);
}

int intel_parent_pcode_write_timeout(struct intel_display *display, u32 mbox, u32 val, int timeout_ms)
{
	return display->parent->pcode->write(display->drm, mbox, val, timeout_ms);
}

int intel_parent_pcode_write(struct intel_display *display, u32 mbox, u32 val)
{
	return intel_parent_pcode_write_timeout(display, mbox, val, 1);
}

int intel_parent_pcode_request(struct intel_display *display, u32 mbox, u32 request,
			       u32 reply_mask, u32 reply, int timeout_base_ms)
{
	return display->parent->pcode->request(display->drm, mbox, request, reply_mask, reply, timeout_base_ms);
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

/* vma */
int intel_parent_vma_fence_id(struct intel_display *display, const struct i915_vma *vma)
{
	if (!display->parent->vma)
		return -1;

	return display->parent->vma->fence_id(vma);
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
