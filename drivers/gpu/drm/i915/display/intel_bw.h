/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_BW_H__
#define __INTEL_BW_H__

#include <drm/drm_atomic.h>

#include "intel_display.h"
#include "intel_display_power.h"
#include "intel_global_state.h"

struct drm_i915_private;
struct intel_atomic_state;
struct intel_crtc_state;

struct intel_dbuf_bw {
	int used_bw[I915_MAX_DBUF_SLICES];
};

struct intel_bw_state {
	struct intel_global_state base;
	struct intel_dbuf_bw dbuf_bw[I915_MAX_PIPES];

	/*
	 * Contains a bit mask, used to determine, whether correspondent
	 * pipe allows SAGV or not.
	 */
	u8 pipe_sagv_reject;

	/*
	 * Current QGV points mask, which restricts
	 * some particular SAGV states, not to confuse
	 * with pipe_sagv_mask.
	 */
	u8 qgv_points_mask;

	unsigned int data_rate[I915_MAX_PIPES];
	u8 num_active_planes[I915_MAX_PIPES];

	/* bitmask of active pipes */
	u8 active_pipes;

	int min_cdclk;
};

#define to_intel_bw_state(x) container_of((x), struct intel_bw_state, base)

struct intel_bw_state *
intel_atomic_get_old_bw_state(struct intel_atomic_state *state);

struct intel_bw_state *
intel_atomic_get_new_bw_state(struct intel_atomic_state *state);

struct intel_bw_state *
intel_atomic_get_bw_state(struct intel_atomic_state *state);

void intel_bw_init_hw(struct drm_i915_private *dev_priv);
int intel_bw_init(struct drm_i915_private *dev_priv);
int intel_bw_atomic_check(struct intel_atomic_state *state);
void intel_bw_crtc_update(struct intel_bw_state *bw_state,
			  const struct intel_crtc_state *crtc_state);
int icl_pcode_restrict_qgv_points(struct drm_i915_private *dev_priv,
				  u32 points_mask);
int intel_bw_calc_min_cdclk(struct intel_atomic_state *state);
int skl_bw_calc_min_cdclk(struct intel_atomic_state *state);

#endif /* __INTEL_BW_H__ */
