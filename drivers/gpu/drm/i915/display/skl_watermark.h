/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __SKL_WATERMARK_H__
#define __SKL_WATERMARK_H__

#include <linux/types.h>

#include "intel_display.h"
#include "intel_global_state.h"
#include "intel_pm_types.h"

struct drm_i915_private;
struct intel_atomic_state;
struct intel_bw_state;
struct intel_crtc;
struct intel_crtc_state;
struct intel_plane;

u8 intel_enabled_dbuf_slices_mask(struct drm_i915_private *i915);

void intel_sagv_pre_plane_update(struct intel_atomic_state *state);
void intel_sagv_post_plane_update(struct intel_atomic_state *state);
bool intel_can_enable_sagv(struct drm_i915_private *i915,
			   const struct intel_bw_state *bw_state);

u32 skl_ddb_dbuf_slice_mask(struct drm_i915_private *i915,
			    const struct skl_ddb_entry *entry);

void skl_write_plane_wm(struct intel_plane *plane,
			const struct intel_crtc_state *crtc_state);
void skl_write_cursor_wm(struct intel_plane *plane,
			 const struct intel_crtc_state *crtc_state);

bool skl_ddb_allocation_overlaps(const struct skl_ddb_entry *ddb,
				 const struct skl_ddb_entry *entries,
				 int num_entries, int ignore_idx);

void skl_wm_get_hw_state(struct drm_i915_private *i915);
void skl_wm_sanitize(struct drm_i915_private *i915);

void intel_wm_state_verify(struct intel_crtc *crtc,
			   struct intel_crtc_state *new_crtc_state);

void skl_watermark_ipc_init(struct drm_i915_private *i915);
void skl_watermark_ipc_update(struct drm_i915_private *i915);
bool skl_watermark_ipc_enabled(struct drm_i915_private *i915);
void skl_watermark_ipc_debugfs_register(struct drm_i915_private *i915);

void skl_wm_init(struct drm_i915_private *i915);

struct intel_dbuf_state {
	struct intel_global_state base;

	struct skl_ddb_entry ddb[I915_MAX_PIPES];
	unsigned int weight[I915_MAX_PIPES];
	u8 slices[I915_MAX_PIPES];
	u8 enabled_slices;
	u8 active_pipes;
	bool joined_mbus;
};

struct intel_dbuf_state *
intel_atomic_get_dbuf_state(struct intel_atomic_state *state);

#define to_intel_dbuf_state(x) container_of((x), struct intel_dbuf_state, base)
#define intel_atomic_get_old_dbuf_state(state) \
	to_intel_dbuf_state(intel_atomic_get_old_global_obj_state(state, &to_i915(state->base.dev)->display.dbuf.obj))
#define intel_atomic_get_new_dbuf_state(state) \
	to_intel_dbuf_state(intel_atomic_get_new_global_obj_state(state, &to_i915(state->base.dev)->display.dbuf.obj))

int intel_dbuf_init(struct drm_i915_private *i915);
void intel_dbuf_pre_plane_update(struct intel_atomic_state *state);
void intel_dbuf_post_plane_update(struct intel_atomic_state *state);
void intel_mbus_dbox_update(struct intel_atomic_state *state);

#endif /* __SKL_WATERMARK_H__ */

