/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef INTEL_GT_REQUESTS_H
#define INTEL_GT_REQUESTS_H

#include <stddef.h>

struct intel_engine_cs;
struct intel_gt;
struct intel_timeline;

long intel_gt_retire_requests_timeout(struct intel_gt *gt, long timeout,
				      long *remaining_timeout);
static inline void intel_gt_retire_requests(struct intel_gt *gt)
{
	intel_gt_retire_requests_timeout(gt, 0, NULL);
}

void intel_engine_init_retire(struct intel_engine_cs *engine);
void intel_engine_add_retire(struct intel_engine_cs *engine,
			     struct intel_timeline *tl);
void intel_engine_fini_retire(struct intel_engine_cs *engine);

void intel_gt_init_requests(struct intel_gt *gt);
void intel_gt_park_requests(struct intel_gt *gt);
void intel_gt_unpark_requests(struct intel_gt *gt);
void intel_gt_fini_requests(struct intel_gt *gt);

#endif /* INTEL_GT_REQUESTS_H */
