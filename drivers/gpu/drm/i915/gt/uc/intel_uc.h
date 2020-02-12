/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2014-2019 Intel Corporation
 */

#ifndef _INTEL_UC_H_
#define _INTEL_UC_H_

#include "intel_guc.h"
#include "intel_huc.h"
#include "i915_params.h"

struct intel_uc;

struct intel_uc_ops {
	int (*sanitize)(struct intel_uc *uc);
	void (*init_fw)(struct intel_uc *uc);
	void (*fini_fw)(struct intel_uc *uc);
	void (*init)(struct intel_uc *uc);
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
};

void intel_uc_init_early(struct intel_uc *uc);
void intel_uc_driver_late_release(struct intel_uc *uc);
void intel_uc_init_mmio(struct intel_uc *uc);
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
intel_uc_ops_function(init, init, void, );
intel_uc_ops_function(fini, fini, void, );
intel_uc_ops_function(init_hw, init_hw, int, 0);
intel_uc_ops_function(fini_hw, fini_hw, void, );
#undef intel_uc_ops_function

#endif
