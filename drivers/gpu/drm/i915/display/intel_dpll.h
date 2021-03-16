/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef _INTEL_DPLL_H_
#define _INTEL_DPLL_H_

struct dpll;
struct drm_i915_private;
struct intel_crtc;
struct intel_crtc_state;

void intel_dpll_init_clock_hook(struct drm_i915_private *dev_priv);
int vlv_calc_dpll_params(int refclk, struct dpll *clock);
int pnv_calc_dpll_params(int refclk, struct dpll *clock);
int i9xx_calc_dpll_params(int refclk, struct dpll *clock);
void vlv_compute_dpll(struct intel_crtc *crtc,
		      struct intel_crtc_state *pipe_config);
void chv_compute_dpll(struct intel_crtc *crtc,
		      struct intel_crtc_state *pipe_config);

#endif
