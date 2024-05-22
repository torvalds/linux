/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#ifndef _XE_GT_SRIOV_PF_POLICY_TYPES_H_
#define _XE_GT_SRIOV_PF_POLICY_TYPES_H_

#include <linux/types.h>

/**
 * struct xe_gt_sriov_guc_policies - GuC SR-IOV policies.
 * @sched_if_idle: controls strict scheduling policy.
 * @reset_engine: controls engines reset on VF switch policy.
 * @sample_period: adverse events sampling period (in milliseconds).
 */
struct xe_gt_sriov_guc_policies {
	bool sched_if_idle;
	bool reset_engine;
	u32 sample_period;
};

/**
 * struct xe_gt_sriov_pf_policy - PF policy data.
 * @guc: GuC scheduling policies.
 */
struct xe_gt_sriov_pf_policy {
	struct xe_gt_sriov_guc_policies guc;
};

#endif
