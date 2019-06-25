/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#ifndef INTEL_GT_PM_H
#define INTEL_GT_PM_H

#include <linux/types.h>

struct intel_gt;

enum {
	INTEL_GT_UNPARK,
	INTEL_GT_PARK,
};

void intel_gt_pm_get(struct intel_gt *gt);
void intel_gt_pm_put(struct intel_gt *gt);

void intel_gt_pm_init_early(struct intel_gt *gt);

void intel_gt_sanitize(struct intel_gt *gt, bool force);
void intel_gt_resume(struct intel_gt *gt);

#endif /* INTEL_GT_PM_H */
