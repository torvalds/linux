/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#ifndef _XE_GT_SRIOV_PF_POLICY_H_
#define _XE_GT_SRIOV_PF_POLICY_H_

#include <linux/types.h>

#include "xe_gt_sriov_pf_policy_types.h"

struct drm_printer;
struct xe_gt;

int xe_gt_sriov_pf_policy_set_sched_if_idle(struct xe_gt *gt, bool enable);
bool xe_gt_sriov_pf_policy_get_sched_if_idle(struct xe_gt *gt);
int xe_gt_sriov_pf_policy_set_reset_engine(struct xe_gt *gt, bool enable);
bool xe_gt_sriov_pf_policy_get_reset_engine(struct xe_gt *gt);
int xe_gt_sriov_pf_policy_set_sample_period(struct xe_gt *gt, u32 value);
u32 xe_gt_sriov_pf_policy_get_sample_period(struct xe_gt *gt);
bool xe_sriov_gt_pf_policy_has_sched_groups_support(struct xe_gt *gt);
bool xe_sriov_gt_pf_policy_has_multi_group_modes(struct xe_gt *gt);
bool xe_sriov_gt_pf_policy_has_sched_group_mode(struct xe_gt *gt,
						enum xe_sriov_sched_group_modes mode);
int xe_gt_sriov_pf_policy_set_sched_groups_mode(struct xe_gt *gt,
						enum xe_sriov_sched_group_modes mode);
bool xe_gt_sriov_pf_policy_sched_groups_enabled(struct xe_gt *gt);

void xe_gt_sriov_pf_policy_init(struct xe_gt *gt);
void xe_gt_sriov_pf_policy_sanitize(struct xe_gt *gt);
int xe_gt_sriov_pf_policy_reprovision(struct xe_gt *gt, bool reset);
int xe_gt_sriov_pf_policy_print(struct xe_gt *gt, struct drm_printer *p);

#endif
