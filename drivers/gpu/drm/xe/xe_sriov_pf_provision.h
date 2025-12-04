/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_SRIOV_PF_PROVISION_H_
#define _XE_SRIOV_PF_PROVISION_H_

#include <linux/types.h>

#include "xe_sriov_pf_provision_types.h"

struct xe_device;

int xe_sriov_pf_provision_bulk_apply_eq(struct xe_device *xe, u32 eq);
int xe_sriov_pf_provision_apply_vf_eq(struct xe_device *xe, unsigned int vfid, u32 eq);
int xe_sriov_pf_provision_query_vf_eq(struct xe_device *xe, unsigned int vfid, u32 *eq);

int xe_sriov_pf_provision_bulk_apply_pt(struct xe_device *xe, u32 pt);
int xe_sriov_pf_provision_apply_vf_pt(struct xe_device *xe, unsigned int vfid, u32 pt);
int xe_sriov_pf_provision_query_vf_pt(struct xe_device *xe, unsigned int vfid, u32 *pt);

int xe_sriov_pf_provision_bulk_apply_priority(struct xe_device *xe, u32 prio);
int xe_sriov_pf_provision_apply_vf_priority(struct xe_device *xe, unsigned int vfid, u32 prio);
int xe_sriov_pf_provision_query_vf_priority(struct xe_device *xe, unsigned int vfid, u32 *prio);

int xe_sriov_pf_provision_vfs(struct xe_device *xe, unsigned int num_vfs);
int xe_sriov_pf_unprovision_vfs(struct xe_device *xe, unsigned int num_vfs);

int xe_sriov_pf_provision_set_mode(struct xe_device *xe, enum xe_sriov_provisioning_mode mode);

/**
 * xe_sriov_pf_provision_set_custom_mode() - Change VFs provision mode to custom.
 * @xe: the PF &xe_device
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
static inline int xe_sriov_pf_provision_set_custom_mode(struct xe_device *xe)
{
	return xe_sriov_pf_provision_set_mode(xe, XE_SRIOV_PROVISIONING_MODE_CUSTOM);
}

#endif
