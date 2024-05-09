/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef _INTEL_PCH_REFCLK_H_
#define _INTEL_PCH_REFCLK_H_

#include <linux/types.h>

struct drm_i915_private;
struct intel_crtc_state;

#ifdef I915
void lpt_program_iclkip(const struct intel_crtc_state *crtc_state);
void lpt_disable_iclkip(struct drm_i915_private *dev_priv);
int lpt_get_iclkip(struct drm_i915_private *dev_priv);
int lpt_iclkip(const struct intel_crtc_state *crtc_state);

void intel_init_pch_refclk(struct drm_i915_private *dev_priv);
void lpt_disable_clkout_dp(struct drm_i915_private *dev_priv);
#else
static inline void lpt_program_iclkip(const struct intel_crtc_state *crtc_state)
{
}
static inline void lpt_disable_iclkip(struct drm_i915_private *dev_priv)
{
}
static inline int lpt_get_iclkip(struct drm_i915_private *dev_priv)
{
	return 0;
}
static inline int lpt_iclkip(const struct intel_crtc_state *crtc_state)
{
	return 0;
}
static inline void intel_init_pch_refclk(struct drm_i915_private *dev_priv)
{
}
static inline void lpt_disable_clkout_dp(struct drm_i915_private *dev_priv)
{
}
#endif

#endif
