// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2020 Intel Corporation.
 */

#include "intel_pxp.h"
#include "intel_pxp_irq.h"
#include "intel_pxp_pm.h"
#include "intel_pxp_session.h"
#include "i915_drv.h"

void intel_pxp_suspend_prepare(struct intel_pxp *pxp)
{
	if (!intel_pxp_is_enabled(pxp))
		return;

	pxp->arb_is_valid = false;

	intel_pxp_invalidate(pxp);
}

void intel_pxp_suspend(struct intel_pxp *pxp)
{
	intel_wakeref_t wakeref;

	if (!intel_pxp_is_enabled(pxp))
		return;

	with_intel_runtime_pm(&pxp_to_gt(pxp)->i915->runtime_pm, wakeref) {
		intel_pxp_fini_hw(pxp);
		pxp->hw_state_invalidated = false;
	}
}

void intel_pxp_resume(struct intel_pxp *pxp)
{
	if (!intel_pxp_is_enabled(pxp))
		return;

	/*
	 * The PXP component gets automatically unbound when we go into S3 and
	 * re-bound after we come out, so in that scenario we can defer the
	 * hw init to the bind call.
	 */
	if (!pxp->pxp_component)
		return;

	intel_pxp_init_hw(pxp);
}

void intel_pxp_runtime_suspend(struct intel_pxp *pxp)
{
	if (!intel_pxp_is_enabled(pxp))
		return;

	pxp->arb_is_valid = false;

	intel_pxp_fini_hw(pxp);

	pxp->hw_state_invalidated = false;
}
