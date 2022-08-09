/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_CDCLK_H__
#define __INTEL_CDCLK_H__

#include <linux/types.h>

#include "i915_drv.h"
#include "intel_display.h"
#include "intel_global_state.h"

struct drm_i915_private;
struct intel_atomic_state;
struct intel_crtc_state;

struct intel_cdclk_vals {
	u32 cdclk;
	u16 refclk;
	u8 divider;	/* CD2X divider * 2 */
	u8 ratio;
};

struct intel_cdclk_state {
	struct intel_global_state base;

	/*
	 * Logical configuration of cdclk (used for all scaling,
	 * watermark, etc. calculations and checks). This is
	 * computed as if all enabled crtcs were active.
	 */
	struct intel_cdclk_config logical;

	/*
	 * Actual configuration of cdclk, can be different from the
	 * logical configuration only when all crtc's are DPMS off.
	 */
	struct intel_cdclk_config actual;

	/* minimum acceptable cdclk for each pipe */
	int min_cdclk[I915_MAX_PIPES];
	/* minimum acceptable voltage level for each pipe */
	u8 min_voltage_level[I915_MAX_PIPES];

	/* pipe to which cd2x update is synchronized */
	enum pipe pipe;

	/* forced minimum cdclk for glk+ audio w/a */
	int force_min_cdclk;

	/* bitmask of active pipes */
	u8 active_pipes;
};

int intel_crtc_compute_min_cdclk(const struct intel_crtc_state *crtc_state);
void intel_cdclk_init_hw(struct drm_i915_private *i915);
void intel_cdclk_uninit_hw(struct drm_i915_private *i915);
void intel_init_cdclk_hooks(struct drm_i915_private *dev_priv);
void intel_update_max_cdclk(struct drm_i915_private *dev_priv);
void intel_update_cdclk(struct drm_i915_private *dev_priv);
u32 intel_read_rawclk(struct drm_i915_private *dev_priv);
bool intel_cdclk_needs_modeset(const struct intel_cdclk_config *a,
			       const struct intel_cdclk_config *b);
void intel_set_cdclk_pre_plane_update(struct intel_atomic_state *state);
void intel_set_cdclk_post_plane_update(struct intel_atomic_state *state);
void intel_dump_cdclk_config(const struct intel_cdclk_config *cdclk_config,
			     const char *context);
int intel_modeset_calc_cdclk(struct intel_atomic_state *state);
void intel_cdclk_get_cdclk(struct drm_i915_private *dev_priv,
			   struct intel_cdclk_config *cdclk_config);
int intel_cdclk_bw_calc_min_cdclk(struct intel_atomic_state *state);
struct intel_cdclk_state *
intel_atomic_get_cdclk_state(struct intel_atomic_state *state);

#define to_intel_cdclk_state(x) container_of((x), struct intel_cdclk_state, base)
#define intel_atomic_get_old_cdclk_state(state) \
	to_intel_cdclk_state(intel_atomic_get_old_global_obj_state(state, &to_i915(state->base.dev)->cdclk.obj))
#define intel_atomic_get_new_cdclk_state(state) \
	to_intel_cdclk_state(intel_atomic_get_new_global_obj_state(state, &to_i915(state->base.dev)->cdclk.obj))

int intel_cdclk_init(struct drm_i915_private *dev_priv);

#endif /* __INTEL_CDCLK_H__ */
