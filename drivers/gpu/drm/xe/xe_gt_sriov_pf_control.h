/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#ifndef _XE_GT_SRIOV_PF_CONTROL_H_
#define _XE_GT_SRIOV_PF_CONTROL_H_

#include <linux/errno.h>
#include <linux/types.h>

struct xe_gt;

int xe_gt_sriov_pf_control_init(struct xe_gt *gt);
void xe_gt_sriov_pf_control_restart(struct xe_gt *gt);

int xe_gt_sriov_pf_control_pause_vf(struct xe_gt *gt, unsigned int vfid);
int xe_gt_sriov_pf_control_resume_vf(struct xe_gt *gt, unsigned int vfid);
int xe_gt_sriov_pf_control_stop_vf(struct xe_gt *gt, unsigned int vfid);
int xe_gt_sriov_pf_control_trigger_flr(struct xe_gt *gt, unsigned int vfid);

#ifdef CONFIG_PCI_IOV
int xe_gt_sriov_pf_control_process_guc2pf(struct xe_gt *gt, const u32 *msg, u32 len);
#else
static inline int xe_gt_sriov_pf_control_process_guc2pf(struct xe_gt *gt, const u32 *msg, u32 len)
{
	return -EPROTO;
}
#endif

#endif
