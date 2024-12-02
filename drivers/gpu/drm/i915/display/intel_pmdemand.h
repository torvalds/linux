/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_PMDEMAND_H__
#define __INTEL_PMDEMAND_H__

#include "intel_display_limits.h"
#include "intel_global_state.h"

struct drm_i915_private;
struct intel_atomic_state;
struct intel_crtc_state;
struct intel_encoder;
struct intel_plane_state;

struct pmdemand_params {
	u16 qclk_gv_bw;
	u8 voltage_index;
	u8 qclk_gv_index;
	u8 active_pipes;
	u8 active_dbufs;	/* pre-Xe3 only */
	/* Total number of non type C active phys from active_phys_mask */
	u8 active_phys;
	u8 plls;
	u16 cdclk_freq_mhz;
	/* max from ddi_clocks[] */
	u16 ddiclk_max;
	u8 scalers;		/* pre-Xe3 only */
};

struct intel_pmdemand_state {
	struct intel_global_state base;

	/* Maintain a persistent list of port clocks across all crtcs */
	int ddi_clocks[I915_MAX_PIPES];

	/* Maintain a persistent list of non type C phys mask */
	u16 active_combo_phys_mask;

	/* Parameters to be configured in the pmdemand registers */
	struct pmdemand_params params;
};

#define to_intel_pmdemand_state(global_state) \
	container_of_const((global_state), struct intel_pmdemand_state, base)

void intel_pmdemand_init_early(struct drm_i915_private *i915);
int intel_pmdemand_init(struct drm_i915_private *i915);
void intel_pmdemand_init_pmdemand_params(struct drm_i915_private *i915,
					 struct intel_pmdemand_state *pmdemand_state);
void intel_pmdemand_update_port_clock(struct drm_i915_private *i915,
				      struct intel_pmdemand_state *pmdemand_state,
				      enum pipe pipe, int port_clock);
void intel_pmdemand_update_phys_mask(struct drm_i915_private *i915,
				     struct intel_encoder *encoder,
				     struct intel_pmdemand_state *pmdemand_state,
				     bool clear_bit);
void intel_pmdemand_program_dbuf(struct drm_i915_private *i915,
				 u8 dbuf_slices);
void intel_pmdemand_pre_plane_update(struct intel_atomic_state *state);
void intel_pmdemand_post_plane_update(struct intel_atomic_state *state);
int intel_pmdemand_atomic_check(struct intel_atomic_state *state);

#endif /* __INTEL_PMDEMAND_H__ */
