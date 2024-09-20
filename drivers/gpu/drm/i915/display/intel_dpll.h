/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef _INTEL_DPLL_H_
#define _INTEL_DPLL_H_

#include <linux/types.h>

struct dpll;
struct drm_i915_private;
struct intel_atomic_state;
struct intel_crtc;
struct intel_crtc_state;
struct intel_dpll_hw_state;
enum pipe;

void intel_dpll_init_clock_hook(struct drm_i915_private *dev_priv);
int intel_dpll_crtc_compute_clock(struct intel_atomic_state *state,
				  struct intel_crtc *crtc);
int intel_dpll_crtc_get_shared_dpll(struct intel_atomic_state *state,
				    struct intel_crtc *crtc);
int i9xx_calc_dpll_params(int refclk, struct dpll *clock);
u32 i9xx_dpll_compute_fp(const struct dpll *dpll);
void i9xx_dpll_get_hw_state(struct intel_crtc *crtc,
			    struct intel_dpll_hw_state *dpll_hw_state);
void vlv_compute_dpll(struct intel_crtc_state *crtc_state);
void chv_compute_dpll(struct intel_crtc_state *crtc_state);

int vlv_force_pll_on(struct drm_i915_private *dev_priv, enum pipe pipe,
		     const struct dpll *dpll);
void vlv_force_pll_off(struct drm_i915_private *dev_priv, enum pipe pipe);

void chv_enable_pll(const struct intel_crtc_state *crtc_state);
void chv_disable_pll(struct drm_i915_private *dev_priv, enum pipe pipe);
void vlv_enable_pll(const struct intel_crtc_state *crtc_state);
void vlv_disable_pll(struct drm_i915_private *dev_priv, enum pipe pipe);
void i9xx_enable_pll(const struct intel_crtc_state *crtc_state);
void i9xx_disable_pll(const struct intel_crtc_state *crtc_state);
bool bxt_find_best_dpll(struct intel_crtc_state *crtc_state,
			struct dpll *best_clock);
int chv_calc_dpll_params(int refclk, struct dpll *pll_clock);

void i9xx_crtc_clock_get(struct intel_crtc_state *crtc_state);
void vlv_crtc_clock_get(struct intel_crtc_state *crtc_state);
void chv_crtc_clock_get(struct intel_crtc_state *crtc_state);

void assert_pll_enabled(struct drm_i915_private *i915, enum pipe pipe);
void assert_pll_disabled(struct drm_i915_private *i915, enum pipe pipe);

#endif
