// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include "xe_device.h"
#include "xe_gt_sriov_pf_control.h"
#include "xe_sriov_pf_control.h"

/**
 * xe_sriov_pf_control_pause_vf() - Pause a VF on all GTs.
 * @xe: the &xe_device
 * @vfid: the VF identifier (can't be 0 == PFID)
 *
 * This function is for PF only.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_pf_control_pause_vf(struct xe_device *xe, unsigned int vfid)
{
	struct xe_gt *gt;
	unsigned int id;
	int result = 0;
	int err;

	for_each_gt(gt, xe, id) {
		err = xe_gt_sriov_pf_control_pause_vf(gt, vfid);
		result = result ? -EUCLEAN : err;
	}

	return result;
}

/**
 * xe_sriov_pf_control_resume_vf() - Resume a VF on all GTs.
 * @xe: the &xe_device
 * @vfid: the VF identifier
 *
 * This function is for PF only.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_pf_control_resume_vf(struct xe_device *xe, unsigned int vfid)
{
	struct xe_gt *gt;
	unsigned int id;
	int result = 0;
	int err;

	for_each_gt(gt, xe, id) {
		err = xe_gt_sriov_pf_control_resume_vf(gt, vfid);
		result = result ? -EUCLEAN : err;
	}

	return result;
}

/**
 * xe_sriov_pf_control_stop_vf - Stop a VF on all GTs.
 * @xe: the &xe_device
 * @vfid: the VF identifier
 *
 * This function is for PF only.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_pf_control_stop_vf(struct xe_device *xe, unsigned int vfid)
{
	struct xe_gt *gt;
	unsigned int id;
	int result = 0;
	int err;

	for_each_gt(gt, xe, id) {
		err = xe_gt_sriov_pf_control_stop_vf(gt, vfid);
		result = result ? -EUCLEAN : err;
	}

	return result;
}

/**
 * xe_sriov_pf_control_reset_vf() - Perform a VF reset (FLR).
 * @xe: the &xe_device
 * @vfid: the VF identifier
 *
 * This function is for PF only.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_pf_control_reset_vf(struct xe_device *xe, unsigned int vfid)
{
	struct xe_gt *gt;
	unsigned int id;
	int result = 0;
	int err;

	for_each_gt(gt, xe, id) {
		err = xe_gt_sriov_pf_control_trigger_flr(gt, vfid);
		result = result ? -EUCLEAN : err;
	}

	return result;
}
