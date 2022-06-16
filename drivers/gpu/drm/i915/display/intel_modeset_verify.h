/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_MODESET_VERIFY_H__
#define __INTEL_MODESET_VERIFY_H__

struct drm_i915_private;
struct intel_atomic_state;
struct intel_crtc;
struct intel_crtc_state;

void intel_modeset_verify_crtc(struct intel_crtc *crtc,
			       struct intel_atomic_state *state,
			       struct intel_crtc_state *old_crtc_state,
			       struct intel_crtc_state *new_crtc_state);
void intel_modeset_verify_disabled(struct drm_i915_private *dev_priv,
				   struct intel_atomic_state *state);

#endif /* __INTEL_MODESET_VERIFY_H__ */
