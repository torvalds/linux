/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_FBC_H__
#define __INTEL_FBC_H__

#include <linux/types.h>

#include "intel_frontbuffer.h"

struct drm_i915_private;
struct intel_atomic_state;
struct intel_crtc;
struct intel_crtc_state;
struct intel_plane_state;

void intel_fbc_choose_crtc(struct drm_i915_private *dev_priv,
			   struct intel_atomic_state *state);
bool intel_fbc_is_active(struct drm_i915_private *dev_priv);
bool intel_fbc_pre_update(struct intel_atomic_state *state,
			  struct intel_crtc *crtc);
void intel_fbc_post_update(struct intel_atomic_state *state,
			   struct intel_crtc *crtc);
void intel_fbc_init(struct drm_i915_private *dev_priv);
void intel_fbc_update(struct intel_atomic_state *state,
		      struct intel_crtc *crtc);
void intel_fbc_disable(struct intel_crtc *crtc);
void intel_fbc_global_disable(struct drm_i915_private *dev_priv);
void intel_fbc_invalidate(struct drm_i915_private *dev_priv,
			  unsigned int frontbuffer_bits,
			  enum fb_op_origin origin);
void intel_fbc_flush(struct drm_i915_private *dev_priv,
		     unsigned int frontbuffer_bits, enum fb_op_origin origin);
void intel_fbc_cleanup_cfb(struct drm_i915_private *dev_priv);
void intel_fbc_handle_fifo_underrun_irq(struct drm_i915_private *dev_priv);
int intel_fbc_reset_underrun(struct drm_i915_private *dev_priv);

#endif /* __INTEL_FBC_H__ */
