/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2014-2019 Intel Corporation
 */

#ifndef _INTEL_GUC_SUBMISSION_H_
#define _INTEL_GUC_SUBMISSION_H_

#include <linux/types.h>

#include "intel_guc.h"

struct intel_engine_cs;

void intel_guc_submission_init_early(struct intel_guc *guc);
int intel_guc_submission_init(struct intel_guc *guc);
void intel_guc_submission_enable(struct intel_guc *guc);
void intel_guc_submission_disable(struct intel_guc *guc);
void intel_guc_submission_fini(struct intel_guc *guc);
int intel_guc_preempt_work_create(struct intel_guc *guc);
void intel_guc_preempt_work_destroy(struct intel_guc *guc);
int intel_guc_submission_setup(struct intel_engine_cs *engine);

static inline bool intel_guc_submission_is_supported(struct intel_guc *guc)
{
	/* XXX: GuC submission is unavailable for now */
	return false;
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
