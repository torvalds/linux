/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef _G4X_DP_H_
#define _G4X_DP_H_

#include <linux/types.h>

#include "i915_reg.h"

enum pipe;
enum port;
struct drm_i915_private;
struct intel_crtc_state;
struct intel_dp;
struct intel_encoder;

const struct dpll *vlv_get_dpll(struct drm_i915_private *i915);
enum pipe vlv_active_pipe(struct intel_dp *intel_dp);
void g4x_dp_set_clock(struct intel_encoder *encoder,
		      struct intel_crtc_state *pipe_config);
bool g4x_dp_port_enabled(struct drm_i915_private *dev_priv,
			 i915_reg_t dp_reg, enum port port,
			 enum pipe *pipe);
bool g4x_dp_init(struct drm_i915_private *dev_priv,
		 i915_reg_t output_reg, enum port port);

#endif
