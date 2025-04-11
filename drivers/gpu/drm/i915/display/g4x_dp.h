/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef _G4X_DP_H_
#define _G4X_DP_H_

#include <linux/types.h>

#include "i915_reg_defs.h"

enum pipe;
enum port;
struct intel_crtc_state;
struct intel_display;
struct intel_dp;
struct intel_encoder;

#ifdef I915
const struct dpll *vlv_get_dpll(struct intel_display *display);
bool g4x_dp_port_enabled(struct intel_display *display,
			 i915_reg_t dp_reg, enum port port,
			 enum pipe *pipe);
bool g4x_dp_init(struct intel_display *display,
		 i915_reg_t output_reg, enum port port);
#else
static inline const struct dpll *vlv_get_dpll(struct intel_display *display)
{
	return NULL;
}
static inline bool g4x_dp_port_enabled(struct intel_display *display,
				       i915_reg_t dp_reg, int port,
				       enum pipe *pipe)
{
	return false;
}
static inline bool g4x_dp_init(struct intel_display *display,
			       i915_reg_t output_reg, int port)
{
	return false;
}
#endif

#endif
