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
bool xe_gt_sriov_pf_control_check_save_data_done(struct xe_gt *gt, unsigned int vfid);
bool xe_gt_sriov_pf_control_check_save_failed(struct xe_gt *gt, unsigned int vfid);
int xe_gt_sriov_pf_control_process_save_data(struct xe_gt *gt, unsigned int vfid);
int xe_gt_sriov_pf_control_trigger_save_vf(struct xe_gt *gt, unsigned int vfid);
int xe_gt_sriov_pf_control_finish_save_vf(struct xe_gt *gt, unsigned int vfid);
int xe_gt_sriov_pf_control_restore_data_done(struct xe_gt *gt, unsigned int vfid);
bool xe_gt_sriov_pf_control_check_restore_failed(struct xe_gt *gt, unsigned int vfid);
int xe_gt_sriov_pf_control_process_restore_data(struct xe_gt *gt, unsigned int vfid);
int xe_gt_sriov_pf_control_trigger_restore_vf(struct xe_gt *gt, unsigned int vfid);
int xe_gt_sriov_pf_control_finish_restore_vf(struct xe_gt *gt, unsigned int vfid);
int xe_gt_sriov_pf_control_stop_vf(struct xe_gt *gt, unsigned int vfid);
int xe_gt_sriov_pf_control_trigger_flr(struct xe_gt *gt, unsigned int vfid);
int xe_gt_sriov_pf_control_sync_flr(struct xe_gt *gt, unsigned int vfid, bool sync);
int xe_gt_sriov_pf_control_wait_flr(struct xe_gt *gt, unsigned int vfid);

#ifdef CONFIG_PCI_IOV
int xe_gt_sriov_pf_control_process_guc2pf(struct xe_gt *gt, const u32 *msg, u32 len);
#else
static inline int xe_gt_sriov_pf_control_process_guc2pf(struct xe_gt *gt, const u32 *msg, u32 len)
{
	return -EPROTO;
}
#endif

#endif
