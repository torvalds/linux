/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2014-2018 Intel Corporation
 */

#ifndef _I915_WORKAROUNDS_H_
#define _I915_WORKAROUNDS_H_

int intel_ctx_workarounds_init(struct drm_i915_private *dev_priv);
int intel_ctx_workarounds_emit(struct i915_request *rq);

void intel_gt_workarounds_apply(struct drm_i915_private *dev_priv);

int intel_whitelist_workarounds_apply(struct intel_engine_cs *engine);

#endif
