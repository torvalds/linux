/* SPDX-License-Identifier: MIT */
/* Copyright © 2025 Intel Corporation */

#ifndef __INTEL_PARENT_H__
#define __INTEL_PARENT_H__

#include <linux/types.h>

struct dma_fence;
struct drm_file;
struct drm_gem_object;
struct drm_scanout_buffer;
struct i915_vma;
struct intel_display;
struct intel_dpt;
struct intel_hdcp_gsc_context;
struct intel_panic;
struct intel_stolen_node;

/* dpt */
struct intel_dpt *intel_parent_dpt_create(struct intel_display *display,
					  struct drm_gem_object *obj, size_t size);
void intel_parent_dpt_destroy(struct intel_display *display, struct intel_dpt *dpt);
void intel_parent_dpt_suspend(struct intel_display *display, struct intel_dpt *dpt);
void intel_parent_dpt_resume(struct intel_display *display, struct intel_dpt *dpt);

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

/* overlay */
bool intel_parent_overlay_is_active(struct intel_display *display);
int intel_parent_overlay_on(struct intel_display *display,
			    u32 frontbuffer_bits);
int intel_parent_overlay_continue(struct intel_display *display,
				  struct i915_vma *vma,
				  bool load_polyphase_filter);
int intel_parent_overlay_off(struct intel_display *display);
int intel_parent_overlay_recover_from_interrupt(struct intel_display *display);
int intel_parent_overlay_release_old_vid(struct intel_display *display);
void intel_parent_overlay_reset(struct intel_display *display);
struct i915_vma *intel_parent_overlay_pin_fb(struct intel_display *display,
					     struct drm_gem_object *obj,
					     u32 *offset);
void intel_parent_overlay_unpin_fb(struct intel_display *display,
				   struct i915_vma *vma);
struct drm_gem_object *intel_parent_overlay_obj_lookup(struct intel_display *display,
						       struct drm_file *filp,
						       u32 handle);
void __iomem *intel_parent_overlay_setup(struct intel_display *display,
					 bool needs_physical);
void intel_parent_overlay_cleanup(struct intel_display *display);

/* panic */
struct intel_panic *intel_parent_panic_alloc(struct intel_display *display);
int intel_parent_panic_setup(struct intel_display *display, struct intel_panic *panic, struct drm_scanout_buffer *sb);
void intel_parent_panic_finish(struct intel_display *display, struct intel_panic *panic);

/* pc8 */
void intel_parent_pc8_block(struct intel_display *display);
void intel_parent_pc8_unblock(struct intel_display *display);

/* pcode */
int intel_parent_pcode_read(struct intel_display *display, u32 mbox, u32 *val, u32 *val1);
int intel_parent_pcode_write_timeout(struct intel_display *display, u32 mbox, u32 val, int timeout_ms);
int intel_parent_pcode_write(struct intel_display *display, u32 mbox, u32 val);
int intel_parent_pcode_request(struct intel_display *display, u32 mbox, u32 request,
			       u32 reply_mask, u32 reply, int timeout_base_ms);

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

/* vma */
int intel_parent_vma_fence_id(struct intel_display *display, const struct i915_vma *vma);

/* generic */
bool intel_parent_has_auxccs(struct intel_display *display);
bool intel_parent_has_fenced_regions(struct intel_display *display);
bool intel_parent_vgpu_active(struct intel_display *display);
void intel_parent_fence_priority_display(struct intel_display *display, struct dma_fence *fence);

#endif /* __INTEL_PARENT_H__ */
