/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_SRIOV_PF_CONTROL_H_
#define _XE_SRIOV_PF_CONTROL_H_

struct xe_device;

int xe_sriov_pf_control_pause_vf(struct xe_device *xe, unsigned int vfid);
int xe_sriov_pf_control_resume_vf(struct xe_device *xe, unsigned int vfid);
int xe_sriov_pf_control_stop_vf(struct xe_device *xe, unsigned int vfid);
int xe_sriov_pf_control_reset_vf(struct xe_device *xe, unsigned int vfid);
int xe_sriov_pf_control_wait_flr(struct xe_device *xe, unsigned int vfid);
int xe_sriov_pf_control_sync_flr(struct xe_device *xe, unsigned int vfid);
int xe_sriov_pf_control_trigger_save_vf(struct xe_device *xe, unsigned int vfid);
int xe_sriov_pf_control_finish_save_vf(struct xe_device *xe, unsigned int vfid);
int xe_sriov_pf_control_trigger_restore_vf(struct xe_device *xe, unsigned int vfid);
int xe_sriov_pf_control_finish_restore_vf(struct xe_device *xe, unsigned int vfid);

#endif
