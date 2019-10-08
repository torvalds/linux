/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#ifndef INTEL_GT_REQUESTS_H
#define INTEL_GT_REQUESTS_H

struct intel_gt;

long intel_gt_retire_requests_timeout(struct intel_gt *gt, long timeout);
static inline void intel_gt_retire_requests(struct intel_gt *gt)
{
	intel_gt_retire_requests_timeout(gt, 0);
}

int intel_gt_wait_for_idle(struct intel_gt *gt, long timeout);

void intel_gt_init_requests(struct intel_gt *gt);
void intel_gt_park_requests(struct intel_gt *gt);
void intel_gt_unpark_requests(struct intel_gt *gt);

#endif /* INTEL_GT_REQUESTS_H */
