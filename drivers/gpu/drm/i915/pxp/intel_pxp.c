// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2020 Intel Corporation.
 */
#include "intel_pxp.h"
#include "intel_pxp_session.h"
#include "intel_pxp_tee.h"
#include "gt/intel_context.h"
#include "i915_drv.h"

struct intel_gt *pxp_to_gt(const struct intel_pxp *pxp)
{
	return container_of(pxp, struct intel_gt, pxp);
}

bool intel_pxp_is_active(const struct intel_pxp *pxp)
{
	return pxp->arb_is_valid;
}

/* KCR register definitions */
#define KCR_INIT _MMIO(0x320f0)
/* Setting KCR Init bit is required after system boot */
#define KCR_INIT_ALLOW_DISPLAY_ME_WRITES REG_BIT(14)

static void kcr_pxp_enable(struct intel_gt *gt)
{
	intel_uncore_write(gt->uncore, KCR_INIT,
			   _MASKED_BIT_ENABLE(KCR_INIT_ALLOW_DISPLAY_ME_WRITES));
}

static void kcr_pxp_disable(struct intel_gt *gt)
{
	intel_uncore_write(gt->uncore, KCR_INIT,
			   _MASKED_BIT_DISABLE(KCR_INIT_ALLOW_DISPLAY_ME_WRITES));
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

	mutex_init(&pxp->tee_mutex);

	ret = create_vcs_context(pxp);
	if (ret)
		return;

	ret = intel_pxp_tee_component_init(pxp);
	if (ret)
		goto out_context;

	drm_info(&gt->i915->drm, "Protected Xe Path (PXP) protected content support initialized\n");

	return;

out_context:
	destroy_vcs_context(pxp);
}

void intel_pxp_fini(struct intel_pxp *pxp)
{
	if (!intel_pxp_is_enabled(pxp))
		return;

	pxp->arb_is_valid = false;

	intel_pxp_tee_component_fini(pxp);

	destroy_vcs_context(pxp);
}

void intel_pxp_init_hw(struct intel_pxp *pxp)
{
	kcr_pxp_enable(pxp_to_gt(pxp));

	intel_pxp_create_arb_session(pxp);
}

void intel_pxp_fini_hw(struct intel_pxp *pxp)
{
	kcr_pxp_disable(pxp_to_gt(pxp));
}
