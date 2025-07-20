/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#ifndef _XE_SRIOV_VF_H_
#define _XE_SRIOV_VF_H_

struct xe_device;

void xe_sriov_vf_init_early(struct xe_device *xe);
void xe_sriov_vf_start_migration_recovery(struct xe_device *xe);

#endif
