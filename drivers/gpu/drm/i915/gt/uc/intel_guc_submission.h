/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2014-2019 Intel Corporation
 */

#ifndef _INTEL_GUC_SUBMISSION_H_
#define _INTEL_GUC_SUBMISSION_H_

#include <linux/types.h>

#include "intel_guc.h"

struct drm_printer;
struct intel_engine_cs;

void intel_guc_submission_init_early(struct intel_guc *guc);
int intel_guc_submission_init(struct intel_guc *guc);
void intel_guc_submission_enable(struct intel_guc *guc);
void intel_guc_submission_disable(struct intel_guc *guc);
void intel_guc_submission_fini(struct intel_guc *guc);
int intel_guc_preempt_work_create(struct intel_guc *guc);
void intel_guc_preempt_work_destroy(struct intel_guc *guc);
int intel_guc_submission_setup(struct intel_engine_cs *engine);
void intel_guc_submission_print_info(struct intel_guc *guc,
				     struct drm_printer *p);
void intel_guc_submission_print_context_info(struct intel_guc *guc,
					     struct drm_printer *p);
void intel_guc_dump_active_requests(struct intel_engine_cs *engine,
				    struct i915_request *hung_rq,
				    struct drm_printer *m);

bool intel_guc_virtual_engine_has_heartbeat(const struct intel_engine_cs *ve);

int intel_guc_wait_for_pending_msg(struct intel_guc *guc,
				   atomic_t *wait_var,
				   bool interruptible,
				   long timeout);

static inline bool intel_guc_submission_is_supported(struct intel_guc *guc)
{
	return guc->submission_supported;
}

static inline bool intel_guc_submission_is_wanted(struct intel_guc *guc)
{
	return guc->submission_selected;
}

static inline bool intel_guc_submission_is_used(struct intel_guc *guc)
{
	return intel_guc_is_used(guc) && intel_guc_submission_is_wanted(guc);
}

#endif
