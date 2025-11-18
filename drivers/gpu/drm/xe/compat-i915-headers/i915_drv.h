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

#include "xe_device.h" /* for xe_device_has_flat_ccs() */
#include "xe_device_types.h"

static inline struct drm_i915_private *to_i915(const struct drm_device *dev)
{
	return container_of(dev, struct drm_i915_private, drm);
}

/* compat platform checks only for soc/ usage */
#define IS_PLATFORM(xe, x) ((xe)->info.platform == x)
#define IS_I915G(dev_priv)	(dev_priv && 0)
#define IS_I915GM(dev_priv)	(dev_priv && 0)
#define IS_PINEVIEW(dev_priv)	(dev_priv && 0)
#define IS_VALLEYVIEW(dev_priv)	(dev_priv && 0)
#define IS_CHERRYVIEW(dev_priv)	(dev_priv && 0)
#define IS_HASWELL(dev_priv)	(dev_priv && 0)
#define IS_BROADWELL(dev_priv)	(dev_priv && 0)
#define IS_BROXTON(dev_priv)	(dev_priv && 0)
#define IS_GEMINILAKE(dev_priv)	(dev_priv && 0)
#define IS_DG2(dev_priv)	IS_PLATFORM(dev_priv, XE_DG2)

#define IS_MOBILE(xe) (xe && 0)

#define HAS_FLAT_CCS(xe) (xe_device_has_flat_ccs(xe))
#define HAS_128_BYTE_Y_TILING(xe) (xe || 1)

#endif
