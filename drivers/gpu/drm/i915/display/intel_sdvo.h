/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_SDVO_H__
#define __INTEL_SDVO_H__

#include <linux/types.h>

#include "i915_reg.h"

struct drm_i915_private;
enum pipe;
enum port;

bool intel_sdvo_port_enabled(struct drm_i915_private *dev_priv,
			     i915_reg_t sdvo_reg, enum pipe *pipe);
bool intel_sdvo_init(struct drm_i915_private *dev_priv,
		     i915_reg_t reg, enum port port);

#endif /* __INTEL_SDVO_H__ */
