/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_CDCLK_H__
#define __INTEL_CDCLK_H__

#include <linux/types.h>

#include "intel_display.h"

struct drm_i915_private;
struct intel_atomic_state;
struct intel_cdclk_state;
struct intel_crtc_state;

int intel_crtc_compute_min_cdclk(const struct intel_crtc_state *crtc_state);
void intel_cdclk_init(struct drm_i915_private *i915);
void intel_cdclk_uninit(struct drm_i915_private *i915);
void intel_init_cdclk_hooks(struct drm_i915_private *dev_priv);
void intel_update_max_cdclk(struct drm_i915_private *dev_priv);
void intel_update_cdclk(struct drm_i915_private *dev_priv);
void intel_update_rawclk(struct drm_i915_private *dev_priv);
bool intel_cdclk_needs_cd2x_update(struct drm_i915_private *dev_priv,
				   const struct intel_cdclk_state *a,
				   const struct intel_cdclk_state *b);
bool intel_cdclk_needs_modeset(const struct intel_cdclk_state *a,
			       const struct intel_cdclk_state *b);
bool intel_cdclk_changed(const struct intel_cdclk_state *a,
			 const struct intel_cdclk_state *b);
void intel_cdclk_swap_state(struct intel_atomic_state *state);
void
intel_set_cdclk_pre_plane_update(struct drm_i915_private *dev_priv,
				 const struct intel_cdclk_state *old_state,
				 const struct intel_cdclk_state *new_state,
				 enum pipe pipe);
void
intel_set_cdclk_post_plane_update(struct drm_i915_private *dev_priv,
				  const struct intel_cdclk_state *old_state,
				  const struct intel_cdclk_state *new_state,
				  enum pipe pipe);
void intel_dump_cdclk_state(const struct intel_cdclk_state *cdclk_state,
			    const char *context);

#endif /* __INTEL_CDCLK_H__ */
