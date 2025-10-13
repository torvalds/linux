/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#ifndef _XE_SRIOV_VF_H_
#define _XE_SRIOV_VF_H_

#include <linux/types.h>

struct dentry;
struct xe_device;

void xe_sriov_vf_init_early(struct xe_device *xe);
int xe_sriov_vf_init_late(struct xe_device *xe);
void xe_sriov_vf_start_migration_recovery(struct xe_device *xe);
bool xe_sriov_vf_migration_supported(struct xe_device *xe);
void xe_sriov_vf_debugfs_register(struct xe_device *xe, struct dentry *root);

#endif
