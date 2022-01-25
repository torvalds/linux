// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2020, Intel Corporation. All rights reserved.
 */

#include <drm/i915_drm.h>

#include "i915_drv.h"

#include "intel_pxp.h"
#include "intel_pxp_cmd.h"
#include "intel_pxp_session.h"
#include "intel_pxp_tee.h"
#include "intel_pxp_types.h"

#define ARB_SESSION I915_PROTECTED_CONTENT_DEFAULT_SESSION /* shorter define */

#define GEN12_KCR_SIP _MMIO(0x32260) /* KCR hwdrm session in play 0-31 */

/* PXP global terminate register for session termination */
#define PXP_GLOBAL_TERMINATE _MMIO(0x320f8)

static bool intel_pxp_session_is_in_play(struct intel_pxp *pxp, u32 id)
{
	struct intel_uncore *uncore = pxp_to_gt(pxp)->uncore;
	intel_wakeref_t wakeref;
	u32 sip = 0;

	/* if we're suspended the session is considered off */
	with_intel_runtime_pm_if_in_use(uncore->rpm, wakeref)
		sip = intel_uncore_read(uncore, GEN12_KCR_SIP);

	return sip & BIT(id);
}

static int pxp_wait_for_session_state(struct intel_pxp *pxp, u32 id, bool in_play)
{
	struct intel_uncore *uncore = pxp_to_gt(pxp)->uncore;
	intel_wakeref_t wakeref;
	u32 mask = BIT(id);
	int ret;

	/* if we're suspended the session is considered off */
	wakeref = intel_runtime_pm_get_if_in_use(uncore->rpm);
	if (!wakeref)
		return in_play ? -ENODEV : 0;

	ret = intel_wait_for_register(uncore,
				      GEN12_KCR_SIP,
				      mask,
				      in_play ? mask : 0,
				      100);

	intel_runtime_pm_put(uncore->rpm, wakeref);

	return ret;
}

static int pxp_create_arb_session(struct intel_pxp *pxp)
{
	struct intel_gt *gt = pxp_to_gt(pxp);
	int ret;

	pxp->arb_is_valid = false;

	if (intel_pxp_session_is_in_play(pxp, ARB_SESSION)) {
		drm_err(&gt->i915->drm, "arb session already in play at creation time\n");
		return -EEXIST;
	}

	ret = intel_pxp_tee_cmd_create_arb_session(pxp, ARB_SESSION);
	if (ret) {
		drm_err(&gt->i915->drm, "tee cmd for arb session creation failed\n");
		return ret;
	}

	ret = pxp_wait_for_session_state(pxp, ARB_SESSION, true);
	if (ret) {
		drm_err(&gt->i915->drm, "arb session failed to go in play\n");
		return ret;
	}

	if (!++pxp->key_instance)
		++pxp->key_instance;

	pxp->arb_is_valid = true;

	return 0;
}

static int pxp_terminate_arb_session_and_global(struct intel_pxp *pxp)
{
	int ret;
	struct intel_gt *gt = pxp_to_gt(pxp);

	/* must mark termination in progress calling this function */
	GEM_WARN_ON(pxp->arb_is_valid);

	/* terminate the hw sessions */
	ret = intel_pxp_terminate_session(pxp, ARB_SESSION);
	if (ret) {
		drm_err(&gt->i915->drm, "Failed to submit session termination\n");
		return ret;
	}

	ret = pxp_wait_for_session_state(pxp, ARB_SESSION, false);
	if (ret) {
		drm_err(&gt->i915->drm, "Session state did not clear\n");
		return ret;
	}

	intel_uncore_write(gt->uncore, PXP_GLOBAL_TERMINATE, 1);

	return ret;
}

static void pxp_terminate(struct intel_pxp *pxp)
{
	int ret;

	pxp->hw_state_invalidated = true;

	/*
	 * if we fail to submit the termination there is no point in waiting for
	 * it to complete. PXP will be marked as non-active until the next
	 * termination is issued.
	 */
	ret = pxp_terminate_arb_session_and_global(pxp);
	if (ret)
		complete_all(&pxp->termination);
}

static void pxp_terminate_complete(struct intel_pxp *pxp)
{
	/* Re-create the arb session after teardown handle complete */
	if (fetch_and_zero(&pxp->hw_state_invalidated))
		pxp_create_arb_session(pxp);

	complete_all(&pxp->termination);
}

void intel_pxp_session_work(struct work_struct *work)
{
	struct intel_pxp *pxp = container_of(work, typeof(*pxp), session_work);
	struct intel_gt *gt = pxp_to_gt(pxp);
	intel_wakeref_t wakeref;
	u32 events = 0;

	spin_lock_irq(&gt->irq_lock);
	events = fetch_and_zero(&pxp->session_events);
	spin_unlock_irq(&gt->irq_lock);

	if (!events)
		return;

	if (events & PXP_INVAL_REQUIRED)
		intel_pxp_invalidate(pxp);

	/*
	 * If we're processing an event while suspending then don't bother,
	 * we're going to re-init everything on resume anyway.
	 */
	wakeref = intel_runtime_pm_get_if_in_use(gt->uncore->rpm);
	if (!wakeref)
		return;

	if (events & PXP_TERMINATION_REQUEST) {
		events &= ~PXP_TERMINATION_COMPLETE;
		pxp_terminate(pxp);
	}

	if (events & PXP_TERMINATION_COMPLETE)
		pxp_terminate_complete(pxp);

	intel_runtime_pm_put(gt->uncore->rpm, wakeref);
}
