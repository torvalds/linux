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
void intel_gpu_ips_init(struct drm_i915_private *dev_priv);
void intel_gpu_ips_teardown(void);
void intel_init_gt_powersave(struct drm_i915_private *dev_priv);
void intel_cleanup_gt_powersave(struct drm_i915_private *dev_priv);
void intel_sanitize_gt_powersave(struct drm_i915_private *dev_priv);
void intel_enable_gt_powersave(struct drm_i915_private *dev_priv);
void intel_disable_gt_powersave(struct drm_i915_private *dev_priv);
void gen6_rps_busy(struct drm_i915_private *dev_priv);
void gen6_rps_idle(struct drm_i915_private *dev_priv);
void gen6_rps_boost(struct i915_request *rq);
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
bool ilk_disable_lp_wm(struct drm_device *dev);
int skl_check_pipe_max_pixel_rate(struct intel_crtc *intel_crtc,
				  struct intel_crtc_state *cstate);
void intel_init_ipc(struct drm_i915_private *dev_priv);
void intel_enable_ipc(struct drm_i915_private *dev_priv);

int intel_gpu_freq(struct drm_i915_private *dev_priv, int val);
int intel_freq_opcode(struct drm_i915_private *dev_priv, int val);
u64 intel_rc6_residency_ns(struct drm_i915_private *dev_priv, i915_reg_t reg);
u64 intel_rc6_residency_us(struct drm_i915_private *dev_priv, i915_reg_t reg);

u32 intel_get_cagf(struct drm_i915_private *dev_priv, u32 rpstat1);

unsigned long i915_chipset_val(struct drm_i915_private *dev_priv);
unsigned long i915_mch_val(struct drm_i915_private *dev_priv);
unsigned long i915_gfx_val(struct drm_i915_private *dev_priv);
void i915_update_gfx_val(struct drm_i915_private *dev_priv);

bool ironlake_set_drps(struct drm_i915_private *dev_priv, u8 val);
int intel_set_rps(struct drm_i915_private *dev_priv, u8 val);
void intel_rps_mark_interactive(struct drm_i915_private *i915, bool interactive);
bool intel_set_memory_cxsr(struct drm_i915_private *dev_priv, bool enable);

#endif /* __INTEL_PM_H__ */
