// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2020, Intel Corporation. All rights reserved.
 */

#include "drm/i915_drm.h"
#include "i915_drv.h"

#include "intel_pxp.h"
#include "intel_pxp_session.h"
#include "intel_pxp_tee.h"
#include "intel_pxp_types.h"

#define ARB_SESSION I915_PROTECTED_CONTENT_DEFAULT_SESSION /* shorter define */

#define GEN12_KCR_SIP _MMIO(0x32260) /* KCR hwdrm session in play 0-31 */

static bool intel_pxp_session_is_in_play(struct intel_pxp *pxp, u32 id)
{
	struct intel_gt *gt = pxp_to_gt(pxp);
	intel_wakeref_t wakeref;
	u32 sip = 0;

	with_intel_runtime_pm(gt->uncore->rpm, wakeref)
		sip = intel_uncore_read(gt->uncore, GEN12_KCR_SIP);

	return sip & BIT(id);
}

static int pxp_wait_for_session_state(struct intel_pxp *pxp, u32 id, bool in_play)
{
	struct intel_gt *gt = pxp_to_gt(pxp);
	intel_wakeref_t wakeref;
	u32 mask = BIT(id);
	int ret;

	with_intel_runtime_pm(gt->uncore->rpm, wakeref)
		ret = intel_wait_for_register(gt->uncore,
					      GEN12_KCR_SIP,
					      mask,
					      in_play ? mask : 0,
					      100);

	return ret;
}

int intel_pxp_create_arb_session(struct intel_pxp *pxp)
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

	pxp->arb_is_valid = true;

	return 0;
}
