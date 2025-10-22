// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "i915_utils.h"

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG)

/* i915 specific, just put here for shutting it up */
int __i915_inject_probe_error(struct drm_i915_private *i915, int err,
			      const char *func, int line)
{
	return 0;
}

#endif
