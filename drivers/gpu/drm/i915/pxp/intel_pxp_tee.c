// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2020 Intel Corporation.
 */

#include <linux/component.h>
#include "drm/i915_pxp_tee_interface.h"
#include "drm/i915_component.h"
#include "i915_drv.h"
#include "intel_pxp.h"
#include "intel_pxp_tee.h"

static inline struct intel_pxp *i915_dev_to_pxp(struct device *i915_kdev)
{
	return &kdev_to_i915(i915_kdev)->gt.pxp;
}

/**
 * i915_pxp_tee_component_bind - bind function to pass the function pointers to pxp_tee
 * @i915_kdev: pointer to i915 kernel device
 * @tee_kdev: pointer to tee kernel device
 * @data: pointer to pxp_tee_master containing the function pointers
 *
 * This bind function is called during the system boot or resume from system sleep.
 *
 * Return: return 0 if successful.
 */
static int i915_pxp_tee_component_bind(struct device *i915_kdev,
				       struct device *tee_kdev, void *data)
{
	struct intel_pxp *pxp = i915_dev_to_pxp(i915_kdev);

	pxp->pxp_component = data;
	pxp->pxp_component->tee_dev = tee_kdev;

	return 0;
}

static void i915_pxp_tee_component_unbind(struct device *i915_kdev,
					  struct device *tee_kdev, void *data)
{
	struct intel_pxp *pxp = i915_dev_to_pxp(i915_kdev);

	pxp->pxp_component = NULL;
}

static const struct component_ops i915_pxp_tee_component_ops = {
	.bind   = i915_pxp_tee_component_bind,
	.unbind = i915_pxp_tee_component_unbind,
};

int intel_pxp_tee_component_init(struct intel_pxp *pxp)
{
	int ret;
	struct intel_gt *gt = pxp_to_gt(pxp);
	struct drm_i915_private *i915 = gt->i915;

	ret = component_add_typed(i915->drm.dev, &i915_pxp_tee_component_ops,
				  I915_COMPONENT_PXP);
	if (ret < 0) {
		drm_err(&i915->drm, "Failed to add PXP component (%d)\n", ret);
		return ret;
	}

	pxp->pxp_component_added = true;

	return 0;
}

void intel_pxp_tee_component_fini(struct intel_pxp *pxp)
{
	struct drm_i915_private *i915 = pxp_to_gt(pxp)->i915;

	if (!pxp->pxp_component_added)
		return;

	component_del(i915->drm.dev, &i915_pxp_tee_component_ops);
	pxp->pxp_component_added = false;
}
