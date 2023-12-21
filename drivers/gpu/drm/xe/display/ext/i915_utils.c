// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "i915_drv.h"

bool i915_vtd_active(struct drm_i915_private *i915)
{
	if (device_iommu_mapped(i915->drm.dev))
		return true;

	/* Running as a guest, we assume the host is enforcing VT'd */
	return i915_run_as_guest();
}

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG)

/* i915 specific, just put here for shutting it up */
int __i915_inject_probe_error(struct drm_i915_private *i915, int err,
			      const char *func, int line)
{
	return 0;
}

#endif
