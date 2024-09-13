/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef _XE_GT_SRIOV_PF_MIGRATION_H_
#define _XE_GT_SRIOV_PF_MIGRATION_H_

#include <linux/types.h>

struct xe_gt;

int xe_gt_sriov_pf_migration_init(struct xe_gt *gt);
int xe_gt_sriov_pf_migration_save_guc_state(struct xe_gt *gt, unsigned int vfid);
int xe_gt_sriov_pf_migration_restore_guc_state(struct xe_gt *gt, unsigned int vfid);

#endif
