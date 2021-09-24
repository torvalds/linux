// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2020 Intel Corporation.
 */
#include "intel_pxp.h"
#include "gt/intel_context.h"
#include "i915_drv.h"

struct intel_gt *pxp_to_gt(const struct intel_pxp *pxp)
{
	return container_of(pxp, struct intel_gt, pxp);
}

static int create_vcs_context(struct intel_pxp *pxp)
{
	static struct lock_class_key pxp_lock;
	struct intel_gt *gt = pxp_to_gt(pxp);
	struct intel_engine_cs *engine;
	struct intel_context *ce;
	int i;

	/*
	 * Find the first VCS engine present. We're guaranteed there is one
	 * if we're in this function due to the check in has_pxp
	 */
	for (i = 0, engine = NULL; !engine; i++)
		engine = gt->engine_class[VIDEO_DECODE_CLASS][i];

	GEM_BUG_ON(!engine || engine->class != VIDEO_DECODE_CLASS);

	ce = intel_engine_create_pinned_context(engine, engine->gt->vm, SZ_4K,
						I915_GEM_HWS_PXP_ADDR,
						&pxp_lock, "pxp_context");
	if (IS_ERR(ce)) {
		drm_err(&gt->i915->drm, "failed to create VCS ctx for PXP\n");
		return PTR_ERR(ce);
	}

	pxp->ce = ce;

	return 0;
}

static void destroy_vcs_context(struct intel_pxp *pxp)
{
	intel_engine_destroy_pinned_context(fetch_and_zero(&pxp->ce));
}

void intel_pxp_init(struct intel_pxp *pxp)
{
	struct intel_gt *gt = pxp_to_gt(pxp);
	int ret;

	if (!HAS_PXP(gt->i915))
		return;

	ret = create_vcs_context(pxp);
	if (ret)
		return;

	drm_info(&gt->i915->drm, "Protected Xe Path (PXP) protected content support initialized\n");
}

void intel_pxp_fini(struct intel_pxp *pxp)
{
	if (!intel_pxp_is_enabled(pxp))
		return;

	destroy_vcs_context(pxp);
}
