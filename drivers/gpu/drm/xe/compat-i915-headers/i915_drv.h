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

#include "i915_utils.h"
#include "intel_runtime_pm.h"
#include "xe_device_types.h"

static inline struct drm_i915_private *to_i915(const struct drm_device *dev)
{
	return container_of(dev, struct drm_i915_private, drm);
}

static inline struct drm_i915_private *kdev_to_i915(struct device *kdev)
{
	return dev_get_drvdata(kdev);
}

#define IS_PLATFORM(xe, x) ((xe)->info.platform == x)
#define INTEL_INFO(dev_priv)	(&((dev_priv)->info))
#define IS_I830(dev_priv)	(dev_priv && 0)
#define IS_I845G(dev_priv)	(dev_priv && 0)
#define IS_I85X(dev_priv)	(dev_priv && 0)
#define IS_I865G(dev_priv)	(dev_priv && 0)
#define IS_I915G(dev_priv)	(dev_priv && 0)
#define IS_I915GM(dev_priv)	(dev_priv && 0)
#define IS_I945G(dev_priv)	(dev_priv && 0)
#define IS_I945GM(dev_priv)	(dev_priv && 0)
#define IS_I965G(dev_priv)	(dev_priv && 0)
#define IS_I965GM(dev_priv)	(dev_priv && 0)
#define IS_G45(dev_priv)	(dev_priv && 0)
#define IS_GM45(dev_priv)	(dev_priv && 0)
#define IS_G4X(dev_priv)	(dev_priv && 0)
#define IS_PINEVIEW(dev_priv)	(dev_priv && 0)
#define IS_G33(dev_priv)	(dev_priv && 0)
#define IS_IRONLAKE(dev_priv)	(dev_priv && 0)
#define IS_IRONLAKE_M(dev_priv) (dev_priv && 0)
#define IS_SANDYBRIDGE(dev_priv)	(dev_priv && 0)
#define IS_IVYBRIDGE(dev_priv)	(dev_priv && 0)
#define IS_IVB_GT1(dev_priv)	(dev_priv && 0)
#define IS_VALLEYVIEW(dev_priv)	(dev_priv && 0)
#define IS_CHERRYVIEW(dev_priv)	(dev_priv && 0)
#define IS_HASWELL(dev_priv)	(dev_priv && 0)
#define IS_BROADWELL(dev_priv)	(dev_priv && 0)
#define IS_SKYLAKE(dev_priv)	(dev_priv && 0)
#define IS_BROXTON(dev_priv)	(dev_priv && 0)
#define IS_KABYLAKE(dev_priv)	(dev_priv && 0)
#define IS_GEMINILAKE(dev_priv)	(dev_priv && 0)
#define IS_COFFEELAKE(dev_priv)	(dev_priv && 0)
#define IS_COMETLAKE(dev_priv)	(dev_priv && 0)
#define IS_ICELAKE(dev_priv)	(dev_priv && 0)
#define IS_JASPERLAKE(dev_priv)	(dev_priv && 0)
#define IS_ELKHARTLAKE(dev_priv)	(dev_priv && 0)
#define IS_TIGERLAKE(dev_priv)	IS_PLATFORM(dev_priv, XE_TIGERLAKE)
#define IS_ROCKETLAKE(dev_priv)	IS_PLATFORM(dev_priv, XE_ROCKETLAKE)
#define IS_DG1(dev_priv)        IS_PLATFORM(dev_priv, XE_DG1)
#define IS_ALDERLAKE_S(dev_priv) IS_PLATFORM(dev_priv, XE_ALDERLAKE_S)
#define IS_ALDERLAKE_P(dev_priv) (IS_PLATFORM(dev_priv, XE_ALDERLAKE_P) || \
				  IS_PLATFORM(dev_priv, XE_ALDERLAKE_N))
#define IS_DG2(dev_priv)	IS_PLATFORM(dev_priv, XE_DG2)
#define IS_METEORLAKE(dev_priv) IS_PLATFORM(dev_priv, XE_METEORLAKE)
#define IS_LUNARLAKE(dev_priv) IS_PLATFORM(dev_priv, XE_LUNARLAKE)
#define IS_BATTLEMAGE(dev_priv)  IS_PLATFORM(dev_priv, XE_BATTLEMAGE)

#define IS_HASWELL_ULT(dev_priv) (dev_priv && 0)
#define IS_BROADWELL_ULT(dev_priv) (dev_priv && 0)
#define IS_BROADWELL_ULX(dev_priv) (dev_priv && 0)

#define IP_VER(ver, rel)                ((ver) << 8 | (rel))

#define IS_MOBILE(xe) (xe && 0)

#define IS_LP(xe) (0)
#define IS_GEN9_LP(xe) (0)
#define IS_GEN9_BC(xe) (0)

#define IS_TIGERLAKE_UY(xe) (xe && 0)
#define IS_COMETLAKE_ULX(xe) (xe && 0)
#define IS_COFFEELAKE_ULX(xe) (xe && 0)
#define IS_KABYLAKE_ULX(xe) (xe && 0)
#define IS_SKYLAKE_ULX(xe) (xe && 0)
#define IS_HASWELL_ULX(xe) (xe && 0)
#define IS_COMETLAKE_ULT(xe) (xe && 0)
#define IS_COFFEELAKE_ULT(xe) (xe && 0)
#define IS_KABYLAKE_ULT(xe) (xe && 0)
#define IS_SKYLAKE_ULT(xe) (xe && 0)

#define IS_DG2_G10(xe) ((xe)->info.subplatform == XE_SUBPLATFORM_DG2_G10)
#define IS_DG2_G11(xe) ((xe)->info.subplatform == XE_SUBPLATFORM_DG2_G11)
#define IS_DG2_G12(xe) ((xe)->info.subplatform == XE_SUBPLATFORM_DG2_G12)
#define IS_RAPTORLAKE_U(xe) ((xe)->info.subplatform == XE_SUBPLATFORM_ALDERLAKE_P_RPLU)
#define IS_ICL_WITH_PORT_F(xe) (xe && 0)
#define HAS_FLAT_CCS(xe) (xe_device_has_flat_ccs(xe))

#define HAS_128_BYTE_Y_TILING(xe) (xe || 1)

#define I915_PRIORITY_DISPLAY 0
struct i915_sched_attr {
	int priority;
};
#define i915_gem_fence_wait_priority(fence, attr) do { (void) attr; } while (0)

#define pdev_to_i915 pdev_to_xe_device

#define FORCEWAKE_ALL XE_FORCEWAKE_ALL

#ifdef CONFIG_ARM64
/*
 * arm64 indirectly includes linux/rtc.h,
 * which defines a irq_lock, so include it
 * here before #define-ing it
 */
#include <linux/rtc.h>
#endif

#define irq_lock irq.lock

#endif
