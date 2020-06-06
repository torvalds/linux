/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2014-2019 Intel Corporation
 */

#ifndef _INTEL_GUC_SUBMISSION_H_
#define _INTEL_GUC_SUBMISSION_H_

#include <linux/types.h>

struct intel_guc;
struct intel_engine_cs;

void intel_guc_submission_init_early(struct intel_guc *guc);
int intel_guc_submission_init(struct intel_guc *guc);
void intel_guc_submission_enable(struct intel_guc *guc);
void intel_guc_submission_disable(struct intel_guc *guc);
void intel_guc_submission_fini(struct intel_guc *guc);
int intel_guc_preempt_work_create(struct intel_guc *guc);
void intel_guc_preempt_work_destroy(struct intel_guc *guc);
bool intel_engine_in_guc_submission_mode(const struct intel_engine_cs *engine);

#endif
