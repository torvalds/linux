/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_SRIOV_PF_PROVISION_H_
#define _XE_SRIOV_PF_PROVISION_H_

struct xe_device;

int xe_sriov_pf_provision_vfs(struct xe_device *xe, unsigned int num_vfs);
int xe_sriov_pf_unprovision_vfs(struct xe_device *xe, unsigned int num_vfs);

#endif
