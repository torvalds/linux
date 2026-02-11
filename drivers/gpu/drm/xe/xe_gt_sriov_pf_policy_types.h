/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#ifndef _XE_GT_SRIOV_PF_POLICY_TYPES_H_
#define _XE_GT_SRIOV_PF_POLICY_TYPES_H_

#include <linux/types.h>

#include "abi/guc_scheduler_abi.h"

/**
 * enum xe_sriov_sched_group_modes - list of possible scheduler group modes
 * @XE_SRIOV_SCHED_GROUPS_DISABLED: no separate groups (i.e., all engines in group 0)
 * @XE_SRIOV_SCHED_GROUPS_MEDIA_SLICES: separate groups for each media slice
 * @XE_SRIOV_SCHED_GROUPS_MODES_COUNT: number of valid modes
 */
enum xe_sriov_sched_group_modes {
	XE_SRIOV_SCHED_GROUPS_DISABLED = 0,
	XE_SRIOV_SCHED_GROUPS_MEDIA_SLICES,
	XE_SRIOV_SCHED_GROUPS_MODES_COUNT /* must be last */
};

/**
 * struct xe_gt_sriov_scheduler_groups - Scheduler groups policy info
 * @max_groups: max number of groups supported by the GuC for the platform
 * @supported_modes: mask of supported modes
 * @current_mode: active scheduler groups mode
 * @modes: array of masks and their number for each mode
 * @modes.groups: array of engine instance groups in given mode, with each group
 *                consisting of GUC_MAX_ENGINE_CLASSES engine instances masks. A
 *                A NULL value indicates that all the engines are in the same
 *                group for this mode on this GT.
 * @modes.num_groups: number of groups in given mode, zero if all the engines
 *                    are in the same group.
 */
struct xe_gt_sriov_scheduler_groups {
	u8 max_groups;
	u32 supported_modes;
	enum xe_sriov_sched_group_modes current_mode;
	struct {
		struct guc_sched_group *groups;
		u32 num_groups;
	} modes[XE_SRIOV_SCHED_GROUPS_MODES_COUNT];
};

/**
 * struct xe_gt_sriov_guc_policies - GuC SR-IOV policies.
 * @sched_if_idle: controls strict scheduling policy.
 * @reset_engine: controls engines reset on VF switch policy.
 * @sample_period: adverse events sampling period (in milliseconds).
 * @sched_groups: available scheduling group configurations.
 */
struct xe_gt_sriov_guc_policies {
	bool sched_if_idle;
	bool reset_engine;
	u32 sample_period;
	struct xe_gt_sriov_scheduler_groups sched_groups;
};

/**
 * struct xe_gt_sriov_pf_policy - PF policy data.
 * @guc: GuC scheduling policies.
 */
struct xe_gt_sriov_pf_policy {
	struct xe_gt_sriov_guc_policies guc;
};

#endif
