/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2020, Intel Corporation. All rights reserved.
 */

#ifndef __INTEL_PXP_H__
#define __INTEL_PXP_H__

#include <linux/errno.h>
#include <linux/types.h>

struct intel_pxp;
struct drm_i915_gem_object;

#ifdef CONFIG_DRM_I915_PXP
struct intel_gt *pxp_to_gt(const struct intel_pxp *pxp);
bool intel_pxp_is_enabled(const struct intel_pxp *pxp);
bool intel_pxp_is_active(const struct intel_pxp *pxp);

void intel_pxp_init(struct intel_pxp *pxp);
void intel_pxp_fini(struct intel_pxp *pxp);

void intel_pxp_init_hw(struct intel_pxp *pxp);
void intel_pxp_fini_hw(struct intel_pxp *pxp);

void intel_pxp_mark_termination_in_progress(struct intel_pxp *pxp);

int intel_pxp_start(struct intel_pxp *pxp);

int intel_pxp_key_check(struct intel_pxp *pxp,
			struct drm_i915_gem_object *obj,
			bool assign);

void intel_pxp_invalidate(struct intel_pxp *pxp);
#else
static inline void intel_pxp_init(struct intel_pxp *pxp)
{
}

static inline void intel_pxp_fini(struct intel_pxp *pxp)
{
}

static inline int intel_pxp_start(struct intel_pxp *pxp)
{
	return -ENODEV;
}

static inline bool intel_pxp_is_enabled(const struct intel_pxp *pxp)
{
	return false;
}

static inline bool intel_pxp_is_active(const struct intel_pxp *pxp)
{
	return false;
}

static inline int intel_pxp_key_check(struct intel_pxp *pxp,
				      struct drm_i915_gem_object *obj,
				      bool assign)
{
	return -ENODEV;
}
#endif

#endif /* __INTEL_PXP_H__ */
