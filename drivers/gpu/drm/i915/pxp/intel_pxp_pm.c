// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2020 Intel Corporation.
 */

#include "i915_drv.h"

#include "intel_pxp.h"
#include "intel_pxp_irq.h"
#include "intel_pxp_pm.h"
#include "intel_pxp_session.h"
#include "intel_pxp_types.h"

void intel_pxp_suspend_prepare(struct intel_pxp *pxp)
{
	if (!intel_pxp_is_enabled(pxp))
		return;

	intel_pxp_end(pxp);

	intel_pxp_invalidate(pxp);
}

void intel_pxp_suspend(struct intel_pxp *pxp)
{
	intel_wakeref_t wakeref;

	if (!intel_pxp_is_enabled(pxp))
		return;

	with_intel_runtime_pm(&pxp->ctrl_gt->i915->runtime_pm, wakeref) {
		intel_pxp_fini_hw(pxp);
		pxp->hw_state_invalidated = false;
	}
}

static void _pxp_resume(struct intel_pxp *pxp, bool take_wakeref)
{
	intel_wakeref_t wakeref;

	if (!intel_pxp_is_enabled(pxp))
		return;

	/*
	 * The PXP component gets automatically unbound when we go into S3 and
	 * re-bound after we come out, so in that scenario we can defer the
	 * hw init to the bind call.
	 * NOTE: GSC-CS backend doesn't rely on components.
	 */
	if (!HAS_ENGINE(pxp->ctrl_gt, GSC0) && !pxp->pxp_component)
		return;

	if (take_wakeref)
		wakeref = intel_runtime_pm_get(&pxp->ctrl_gt->i915->runtime_pm);
	intel_pxp_init_hw(pxp);
	if (take_wakeref)
		intel_runtime_pm_put(&pxp->ctrl_gt->i915->runtime_pm, wakeref);
}

void intel_pxp_resume_complete(struct intel_pxp *pxp)
{
	_pxp_resume(pxp, true);
}

void intel_pxp_runtime_resume(struct intel_pxp *pxp)
{
	_pxp_resume(pxp, false);
}

void intel_pxp_runtime_suspend(struct intel_pxp *pxp)
{
	if (!intel_pxp_is_enabled(pxp))
		return;

	pxp->arb_is_valid = false;

	intel_pxp_fini_hw(pxp);

	pxp->hw_state_invalidated = false;
}
