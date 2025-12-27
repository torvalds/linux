/* SPDX-License-Identifier: MIT */
/* Copyright © 2025 Intel Corporation */

#ifndef __INTEL_PARENT_H__
#define __INTEL_PARENT_H__

#include <linux/types.h>

struct dma_fence;
struct drm_scanout_buffer;
struct intel_display;
struct intel_hdcp_gsc_context;
struct intel_panic;
struct intel_stolen_node;

/* hdcp */
ssize_t intel_parent_hdcp_gsc_msg_send(struct intel_display *display,
				       struct intel_hdcp_gsc_context *gsc_context,
				       void *msg_in, size_t msg_in_len,
				       void *msg_out, size_t msg_out_len);
bool intel_parent_hdcp_gsc_check_status(struct intel_display *display);
struct intel_hdcp_gsc_context *intel_parent_hdcp_gsc_context_alloc(struct intel_display *display);
void intel_parent_hdcp_gsc_context_free(struct intel_display *display,
					struct intel_hdcp_gsc_context *gsc_context);

/* irq */
bool intel_parent_irq_enabled(struct intel_display *display);
void intel_parent_irq_synchronize(struct intel_display *display);

/* panic */
struct intel_panic *intel_parent_panic_alloc(struct intel_display *display);
int intel_parent_panic_setup(struct intel_display *display, struct intel_panic *panic, struct drm_scanout_buffer *sb);
void intel_parent_panic_finish(struct intel_display *display, struct intel_panic *panic);

/* pc8 */
void intel_parent_pc8_block(struct intel_display *display);
void intel_parent_pc8_unblock(struct intel_display *display);

/* rps */
bool intel_parent_rps_available(struct intel_display *display);
void intel_parent_rps_boost_if_not_started(struct intel_display *display, struct dma_fence *fence);
void intel_parent_rps_mark_interactive(struct intel_display *display, bool interactive);
void intel_parent_rps_ilk_irq_handler(struct intel_display *display);

/* stolen */
int intel_parent_stolen_insert_node_in_range(struct intel_display *display,
					     struct intel_stolen_node *node, u64 size,
					     unsigned int align, u64 start, u64 end);
int intel_parent_stolen_insert_node(struct intel_display *display, struct intel_stolen_node *node, u64 size,
				    unsigned int align);
void intel_parent_stolen_remove_node(struct intel_display *display,
				     struct intel_stolen_node *node);
bool intel_parent_stolen_initialized(struct intel_display *display);
bool intel_parent_stolen_node_allocated(struct intel_display *display,
					const struct intel_stolen_node *node);
u32 intel_parent_stolen_node_offset(struct intel_display *display, struct intel_stolen_node *node);
u64 intel_parent_stolen_area_address(struct intel_display *display);
u64 intel_parent_stolen_area_size(struct intel_display *display);
u64 intel_parent_stolen_node_address(struct intel_display *display, struct intel_stolen_node *node);
u64 intel_parent_stolen_node_size(struct intel_display *display, const struct intel_stolen_node *node);
struct intel_stolen_node *intel_parent_stolen_node_alloc(struct intel_display *display);
void intel_parent_stolen_node_free(struct intel_display *display, const struct intel_stolen_node *node);

/* generic */
bool intel_parent_has_auxccs(struct intel_display *display);
bool intel_parent_has_fenced_regions(struct intel_display *display);
bool intel_parent_vgpu_active(struct intel_display *display);
void intel_parent_fence_priority_display(struct intel_display *display, struct dma_fence *fence);

#endif /* __INTEL_PARENT_H__ */
