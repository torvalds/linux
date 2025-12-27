/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */
#ifndef _XE_I915_DRV_H_
#define _XE_I915_DRV_H_

/*
 * "Adaptation header" to allow i915 display to also build for xe driver.
 * TODO: refactor i915 and xe so this can cease to exist
 */

#include <drm/drm_drv.h>

#include "xe_device_types.h"

static inline struct drm_i915_private *to_i915(const struct drm_device *dev)
{
	return container_of(dev, struct drm_i915_private, drm);
}

#endif
