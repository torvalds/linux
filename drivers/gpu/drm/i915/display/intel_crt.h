/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_CRT_H__
#define __INTEL_CRT_H__

#include "i915_reg.h"

enum pipe;
struct drm_encoder;
struct drm_i915_private;
struct drm_i915_private;

bool intel_crt_port_enabled(struct drm_i915_private *dev_priv,
			    i915_reg_t adpa_reg, enum pipe *pipe);
void intel_crt_init(struct drm_i915_private *dev_priv);
void intel_crt_reset(struct drm_encoder *encoder);

#endif /* __INTEL_CRT_H__ */
