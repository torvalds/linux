// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2023 Intel Corporation.
 */

#include "gem/i915_gem_internal.h"

#include "gt/intel_context.h"

#include "i915_drv.h"
#include "intel_pxp_cmd_interface_43.h"
#include "intel_pxp_gsccs.h"
#include "intel_pxp_types.h"

static void
gsccs_destroy_execution_resource(struct intel_pxp *pxp)
{
	struct gsccs_session_resources *exec_res = &pxp->gsccs_res;

	if (exec_res->ce)
		intel_context_put(exec_res->ce);

	memset(exec_res, 0, sizeof(*exec_res));
}

static int
gsccs_allocate_execution_resource(struct intel_pxp *pxp)
{
	struct intel_gt *gt = pxp->ctrl_gt;
	struct gsccs_session_resources *exec_res = &pxp->gsccs_res;
	struct intel_engine_cs *engine = gt->engine[GSC0];
	struct intel_context *ce;

	/*
	 * First, ensure the GSC engine is present.
	 * NOTE: Backend would only be called with the correct gt.
	 */
	if (!engine)
		return -ENODEV;

	/* Finally, create an intel_context to be used during the submission */
	ce = intel_context_create(engine);
	if (IS_ERR(ce)) {
		drm_err(&gt->i915->drm, "Failed creating gsccs backend ctx\n");
		return PTR_ERR(ce);
	}

	i915_vm_put(ce->vm);
	ce->vm = i915_vm_get(pxp->ctrl_gt->vm);
	exec_res->ce = ce;

	return 0;
}

void intel_pxp_gsccs_fini(struct intel_pxp *pxp)
{
	gsccs_destroy_execution_resource(pxp);
}

int intel_pxp_gsccs_init(struct intel_pxp *pxp)
{
	return gsccs_allocate_execution_resource(pxp);
}
