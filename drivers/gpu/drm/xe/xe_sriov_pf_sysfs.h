/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_SRIOV_PF_SYSFS_H_
#define _XE_SRIOV_PF_SYSFS_H_

struct xe_device;

int xe_sriov_pf_sysfs_init(struct xe_device *xe);

void xe_sriov_pf_sysfs_link_vfs(struct xe_device *xe, unsigned int num_vfs);
void xe_sriov_pf_sysfs_unlink_vfs(struct xe_device *xe, unsigned int num_vfs);

#endif
