// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "i915_drv.h"
#include "intel_guc_slpc.h"
#include "gt/intel_gt.h"

static inline struct intel_guc *slpc_to_guc(struct intel_guc_slpc *slpc)
{
	return container_of(slpc, struct intel_guc, slpc);
}

static bool __detect_slpc_supported(struct intel_guc *guc)
{
	/* GuC SLPC is unavailable for pre-Gen12 */
	return guc->submission_supported &&
		GRAPHICS_VER(guc_to_gt(guc)->i915) >= 12;
}

static bool __guc_slpc_selected(struct intel_guc *guc)
{
	if (!intel_guc_slpc_is_supported(guc))
		return false;

	return guc->submission_selected;
}

void intel_guc_slpc_init_early(struct intel_guc_slpc *slpc)
{
	struct intel_guc *guc = slpc_to_guc(slpc);

	slpc->supported = __detect_slpc_supported(guc);
	slpc->selected = __guc_slpc_selected(guc);
}

int intel_guc_slpc_init(struct intel_guc_slpc *slpc)
{
	return 0;
}

void intel_guc_slpc_fini(struct intel_guc_slpc *slpc)
{
}
