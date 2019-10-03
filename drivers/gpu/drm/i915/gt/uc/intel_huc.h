/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2014-2019 Intel Corporation
 */

#ifndef _INTEL_HUC_H_
#define _INTEL_HUC_H_

#include "i915_reg.h"
#include "intel_uc_fw.h"
#include "intel_huc_fw.h"

struct intel_huc {
	/* Generic uC firmware management */
	struct intel_uc_fw fw;

	/* HuC-specific additions */
	struct i915_vma *rsa_data;

	struct {
		i915_reg_t reg;
		u32 mask;
		u32 value;
	} status;
};

void intel_huc_init_early(struct intel_huc *huc);
int intel_huc_init(struct intel_huc *huc);
void intel_huc_fini(struct intel_huc *huc);
int intel_huc_auth(struct intel_huc *huc);
int intel_huc_check_status(struct intel_huc *huc);

static inline int intel_huc_sanitize(struct intel_huc *huc)
{
	intel_uc_fw_sanitize(&huc->fw);
	return 0;
}

static inline bool intel_huc_is_supported(struct intel_huc *huc)
{
	return intel_uc_fw_is_supported(&huc->fw);
}

static inline bool intel_huc_is_enabled(struct intel_huc *huc)
{
	return intel_uc_fw_is_enabled(&huc->fw);
}

static inline bool intel_huc_is_authenticated(struct intel_huc *huc)
{
	return intel_uc_fw_is_running(&huc->fw);
}

#endif
