/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_PM_H__
#define __INTEL_PM_H__

#include <linux/types.h>

#include "i915_reg.h"

struct drm_device;
struct drm_i915_private;
struct i915_request;
struct intel_atomic_state;
struct intel_crtc;
struct intel_crtc_state;
struct intel_plane;
struct skl_ddb_allocation;
struct skl_ddb_entry;
struct skl_pipe_wm;
struct skl_wm_level;

void intel_init_clock_gating(struct drm_i915_private *dev_priv);
void intel_suspend_hw(struct drm_i915_private *dev_priv);
int ilk_wm_max_level(const struct drm_i915_private *dev_priv);
void intel_update_watermarks(struct intel_crtc *crtc);
void intel_init_pm(struct drm_i915_private *dev_priv);
void intel_init_clock_gating_hooks(struct drm_i915_private *dev_priv);
void intel_pm_setup(struct drm_i915_private *dev_priv);
void g4x_wm_get_hw_state(struct drm_i915_private *dev_priv);
void vlv_wm_get_hw_state(struct drm_i915_private *dev_priv);
void ilk_wm_get_hw_state(struct drm_i915_private *dev_priv);
void skl_wm_get_hw_state(struct drm_i915_private *dev_priv);
void skl_pipe_ddb_get_hw_state(struct intel_crtc *crtc,
			       struct skl_ddb_entry *ddb_y,
			       struct skl_ddb_entry *ddb_uv);
void skl_ddb_get_hw_state(struct drm_i915_private *dev_priv,
			  struct skl_ddb_allocation *ddb /* out */);
void skl_pipe_wm_get_hw_state(struct intel_crtc *crtc,
			      struct skl_pipe_wm *out);
void g4x_wm_sanitize(struct drm_i915_private *dev_priv);
void vlv_wm_sanitize(struct drm_i915_private *dev_priv);
bool intel_can_enable_sagv(struct intel_atomic_state *state);
int intel_enable_sagv(struct drm_i915_private *dev_priv);
int intel_disable_sagv(struct drm_i915_private *dev_priv);
bool skl_wm_level_equals(const struct skl_wm_level *l1,
			 const struct skl_wm_level *l2);
bool skl_ddb_allocation_overlaps(const struct skl_ddb_entry *ddb,
				 const struct skl_ddb_entry *entries,
				 int num_entries, int ignore_idx);
void skl_write_plane_wm(struct intel_plane *plane,
			const struct intel_crtc_state *crtc_state);
void skl_write_cursor_wm(struct intel_plane *plane,
			 const struct intel_crtc_state *crtc_state);
bool ilk_disable_lp_wm(struct drm_i915_private *dev_priv);
void intel_init_ipc(struct drm_i915_private *dev_priv);
void intel_enable_ipc(struct drm_i915_private *dev_priv);

bool intel_set_memory_cxsr(struct drm_i915_private *dev_priv, bool enable);

#endif /* __INTEL_PM_H__ */
