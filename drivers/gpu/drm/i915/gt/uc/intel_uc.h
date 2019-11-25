/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2014-2019 Intel Corporation
 */

#ifndef _INTEL_UC_H_
#define _INTEL_UC_H_

#include "intel_guc.h"
#include "intel_huc.h"
#include "i915_params.h"

struct intel_uc {
	struct intel_guc guc;
	struct intel_huc huc;

	/* Snapshot of GuC log from last failed load */
	struct drm_i915_gem_object *load_err_log;
};

void intel_uc_init_early(struct intel_uc *uc);
void intel_uc_driver_late_release(struct intel_uc *uc);
void intel_uc_init_mmio(struct intel_uc *uc);
void intel_uc_fetch_firmwares(struct intel_uc *uc);
void intel_uc_cleanup_firmwares(struct intel_uc *uc);
void intel_uc_sanitize(struct intel_uc *uc);
void intel_uc_init(struct intel_uc *uc);
int intel_uc_init_hw(struct intel_uc *uc);
void intel_uc_fini_hw(struct intel_uc *uc);
void intel_uc_fini(struct intel_uc *uc);
void intel_uc_reset_prepare(struct intel_uc *uc);
void intel_uc_suspend(struct intel_uc *uc);
void intel_uc_runtime_suspend(struct intel_uc *uc);
int intel_uc_resume(struct intel_uc *uc);
int intel_uc_runtime_resume(struct intel_uc *uc);

static inline bool intel_uc_supports_guc(struct intel_uc *uc)
{
	return intel_guc_is_supported(&uc->guc);
}

static inline bool intel_uc_uses_guc(struct intel_uc *uc)
{
	return intel_guc_is_enabled(&uc->guc);
}

static inline bool intel_uc_supports_guc_submission(struct intel_uc *uc)
{
	return intel_guc_is_submission_supported(&uc->guc);
}

static inline bool intel_uc_uses_guc_submission(struct intel_uc *uc)
{
	return intel_guc_is_submission_supported(&uc->guc);
}

static inline bool intel_uc_supports_huc(struct intel_uc *uc)
{
	return intel_uc_supports_guc(uc);
}

static inline bool intel_uc_uses_huc(struct intel_uc *uc)
{
	return intel_huc_is_enabled(&uc->huc);
}

#endif
