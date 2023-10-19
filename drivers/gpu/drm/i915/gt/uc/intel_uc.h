/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2014-2019 Intel Corporation
 */

#ifndef _INTEL_UC_H_
#define _INTEL_UC_H_

#include "intel_guc.h"
#include "intel_guc_rc.h"
#include "intel_guc_submission.h"
#include "intel_guc_slpc.h"
#include "intel_huc.h"
#include "i915_params.h"

struct intel_uc;

struct intel_uc_ops {
	int (*sanitize)(struct intel_uc *uc);
	void (*init_fw)(struct intel_uc *uc);
	void (*fini_fw)(struct intel_uc *uc);
	int (*init)(struct intel_uc *uc);
	void (*fini)(struct intel_uc *uc);
	int (*init_hw)(struct intel_uc *uc);
	void (*fini_hw)(struct intel_uc *uc);
};

struct intel_uc {
	struct intel_uc_ops const *ops;
	struct intel_guc guc;
	struct intel_huc huc;

	/* Snapshot of GuC log from last failed load */
	struct drm_i915_gem_object *load_err_log;

	bool reset_in_progress;
};

void intel_uc_init_early(struct intel_uc *uc);
void intel_uc_init_late(struct intel_uc *uc);
void intel_uc_driver_late_release(struct intel_uc *uc);
void intel_uc_driver_remove(struct intel_uc *uc);
void intel_uc_init_mmio(struct intel_uc *uc);
void intel_uc_reset_prepare(struct intel_uc *uc);
void intel_uc_reset(struct intel_uc *uc, intel_engine_mask_t stalled);
void intel_uc_reset_finish(struct intel_uc *uc);
void intel_uc_cancel_requests(struct intel_uc *uc);
void intel_uc_suspend(struct intel_uc *uc);
void intel_uc_runtime_suspend(struct intel_uc *uc);
int intel_uc_resume(struct intel_uc *uc);
int intel_uc_runtime_resume(struct intel_uc *uc);

/*
 * We need to know as early as possible if we're going to use GuC or not to
 * take the correct setup paths. Additionally, once we've started loading the
 * GuC, it is unsafe to keep executing without it because some parts of the HW,
 * a subset of which is not cleaned on GT reset, will start expecting the GuC FW
 * to be running.
 * To solve both these requirements, we commit to using the microcontrollers if
 * the relevant modparam is set and the blobs are found on the system. At this
 * stage, the only thing that can stop us from attempting to load the blobs on
 * the HW and use them is a fundamental issue (e.g. no memory for our
 * structures); if we hit such a problem during driver load we're broken even
 * without GuC, so there is no point in trying to fall back.
 *
 * Given the above, we can be in one of 4 states, with the last one implying
 * we're committed to using the microcontroller:
 * - Not supported: not available in HW and/or firmware not defined.
 * - Supported: available in HW and firmware defined.
 * - Wanted: supported + enabled in modparam.
 * - In use: wanted + firmware found on the system and successfully fetched.
 */

#define __uc_state_checker(x, func, state, required) \
static inline bool intel_uc_##state##_##func(struct intel_uc *uc) \
{ \
	return intel_##func##_is_##required(&uc->x); \
}

#define uc_state_checkers(x, func) \
__uc_state_checker(x, func, supports, supported) \
__uc_state_checker(x, func, wants, wanted) \
__uc_state_checker(x, func, uses, used)

uc_state_checkers(guc, guc);
uc_state_checkers(huc, huc);
uc_state_checkers(guc, guc_submission);
uc_state_checkers(guc, guc_slpc);
uc_state_checkers(guc, guc_rc);

#undef uc_state_checkers
#undef __uc_state_checker

static inline int intel_uc_wait_for_idle(struct intel_uc *uc, long timeout)
{
	return intel_guc_wait_for_idle(&uc->guc, timeout);
}

#define intel_uc_ops_function(_NAME, _OPS, _TYPE, _RET) \
static inline _TYPE intel_uc_##_NAME(struct intel_uc *uc) \
{ \
	if (uc->ops->_OPS) \
		return uc->ops->_OPS(uc); \
	return _RET; \
}
intel_uc_ops_function(sanitize, sanitize, int, 0);
intel_uc_ops_function(fetch_firmwares, init_fw, void, );
intel_uc_ops_function(cleanup_firmwares, fini_fw, void, );
intel_uc_ops_function(init, init, int, 0);
intel_uc_ops_function(fini, fini, void, );
intel_uc_ops_function(init_hw, init_hw, int, 0);
intel_uc_ops_function(fini_hw, fini_hw, void, );
#undef intel_uc_ops_function

#endif
