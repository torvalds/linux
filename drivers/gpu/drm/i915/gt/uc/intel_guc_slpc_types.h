/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef _INTEL_GUC_SLPC_TYPES_H_
#define _INTEL_GUC_SLPC_TYPES_H_

#include <linux/types.h>

struct intel_guc_slpc {
	struct i915_vma *vma;
	struct slpc_shared_data *vaddr;
	bool supported;
	bool selected;
};

#endif
