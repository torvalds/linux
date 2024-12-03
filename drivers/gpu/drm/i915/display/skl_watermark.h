/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __SKL_WATERMARK_H__
#define __SKL_WATERMARK_H__

#include <linux/types.h>

#include "intel_display_limits.h"
#include "intel_global_state.h"
#include "intel_wm_types.h"

struct drm_i915_private;
struct intel_atomic_state;
struct intel_bw_state;
struct intel_crtc;
struct intel_crtc_state;
struct intel_plane;
struct skl_pipe_wm;
struct skl_wm_level;

u8 intel_enabled_dbuf_slices_mask(struct drm_i915_private *i915);

void intel_sagv_pre_plane_update(struct intel_atomic_state *state);
void intel_sagv_post_plane_update(struct intel_atomic_state *state);
bool intel_can_enable_sagv(struct drm_i915_private *i915,
			   const struct intel_bw_state *bw_state);
bool intel_has_sagv(struct drm_i915_private *i915);

u32 skl_ddb_dbuf_slice_mask(struct drm_i915_private *i915,
			    const struct skl_ddb_entry *entry);

bool skl_ddb_allocation_overlaps(const struct skl_ddb_entry *ddb,
				 const struct skl_ddb_entry *entries,
				 int num_entries, int ignore_idx);

void intel_wm_state_verify(struct intel_atomic_state *state,
			   struct intel_crtc *crtc);

void skl_watermark_ipc_init(struct drm_i915_private *i915);
void skl_watermark_ipc_update(struct drm_i915_private *i915);
bool skl_watermark_ipc_enabled(struct drm_i915_private *i915);
void skl_watermark_debugfs_register(struct drm_i915_private *i915);

unsigned int skl_watermark_max_latency(struct drm_i915_private *i915,
				       int initial_wm_level);
void skl_wm_init(struct drm_i915_private *i915);

const struct skl_wm_level *skl_plane_wm_level(const struct skl_pipe_wm *pipe_wm,
					      enum plane_id plane_id,
					      int level);
const struct skl_wm_level *skl_plane_trans_wm(const struct skl_pipe_wm *pipe_wm,
					      enum plane_id plane_id);

struct intel_dbuf_state {
	struct intel_global_state base;

	struct skl_ddb_entry ddb[I915_MAX_PIPES];
	unsigned int weight[I915_MAX_PIPES];
	u8 slices[I915_MAX_PIPES];
	u8 enabled_slices;
	u8 active_pipes;
	u8 mdclk_cdclk_ratio;
	bool joined_mbus;
};

struct intel_dbuf_state *
intel_atomic_get_dbuf_state(struct intel_atomic_state *state);

#define to_intel_dbuf_state(global_state) \
	container_of_const((global_state), struct intel_dbuf_state, base)

#define intel_atomic_get_old_dbuf_state(state) \
	to_intel_dbuf_state(intel_atomic_get_old_global_obj_state(state, &to_intel_display(state)->dbuf.obj))
#define intel_atomic_get_new_dbuf_state(state) \
	to_intel_dbuf_state(intel_atomic_get_new_global_obj_state(state, &to_intel_display(state)->dbuf.obj))

int intel_dbuf_init(struct drm_i915_private *i915);
int intel_dbuf_state_set_mdclk_cdclk_ratio(struct intel_atomic_state *state,
					   int ratio);

void intel_dbuf_pre_plane_update(struct intel_atomic_state *state);
void intel_dbuf_post_plane_update(struct intel_atomic_state *state);
void intel_dbuf_mdclk_cdclk_ratio_update(struct drm_i915_private *i915,
					 int ratio, bool joined_mbus);
void intel_dbuf_mbus_pre_ddb_update(struct intel_atomic_state *state);
void intel_dbuf_mbus_post_ddb_update(struct intel_atomic_state *state);
void intel_program_dpkgc_latency(struct intel_atomic_state *state);

#endif /* __SKL_WATERMARK_H__ */

