/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_BW_H__
#define __INTEL_BW_H__

#include <drm/drm_atomic.h>

#include "i915_drv.h"
#include "intel_display.h"

struct drm_i915_private;
struct intel_atomic_state;
struct intel_crtc_state;

struct intel_bw_state {
	struct drm_private_state base;

	unsigned int data_rate[I915_MAX_PIPES];
	u8 num_active_planes[I915_MAX_PIPES];
};

#define to_intel_bw_state(x) container_of((x), struct intel_bw_state, base)

static inline struct intel_bw_state *
intel_atomic_get_bw_state(struct intel_atomic_state *state)
{
	struct drm_i915_private *dev_priv = to_i915(state->base.dev);
	struct drm_private_state *bw_state;

	bw_state = drm_atomic_get_private_obj_state(&state->base,
						    &dev_priv->bw_obj);
	if (IS_ERR(bw_state))
		return ERR_CAST(bw_state);

	return to_intel_bw_state(bw_state);
}

void intel_bw_init_hw(struct drm_i915_private *dev_priv);
int intel_bw_init(struct drm_i915_private *dev_priv);
int intel_bw_atomic_check(struct intel_atomic_state *state);
void intel_bw_crtc_update(struct intel_bw_state *bw_state,
			  const struct intel_crtc_state *crtc_state);

#endif /* __INTEL_BW_H__ */
