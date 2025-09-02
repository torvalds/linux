/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_SRIOV_VF_CCS_H_
#define _XE_SRIOV_VF_CCS_H_

struct xe_device;
struct xe_bo;

int xe_sriov_vf_ccs_init(struct xe_device *xe);
int xe_sriov_vf_ccs_attach_bo(struct xe_bo *bo);
int xe_sriov_vf_ccs_detach_bo(struct xe_bo *bo);
int xe_sriov_vf_ccs_register_context(struct xe_device *xe);

#endif
