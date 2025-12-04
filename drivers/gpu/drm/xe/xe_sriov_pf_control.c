// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include "xe_device.h"
#include "xe_gt_sriov_pf_control.h"
#include "xe_gt_sriov_pf_migration.h"
#include "xe_sriov_packet.h"
#include "xe_sriov_pf_control.h"
#include "xe_sriov_printk.h"

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

	if (result)
		return result;

	xe_sriov_info(xe, "VF%u paused!\n", vfid);
	return 0;
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

	if (result)
		return result;

	xe_sriov_info(xe, "VF%u resumed!\n", vfid);
	return 0;
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

	if (result)
		return result;

	xe_sriov_info(xe, "VF%u stopped!\n", vfid);
	return 0;
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

	for_each_gt(gt, xe, id) {
		err = xe_gt_sriov_pf_control_wait_flr(gt, vfid);
		result = result ? -EUCLEAN : err;
	}

	return result;
}

/**
 * xe_sriov_pf_control_wait_flr() - Wait for a VF reset (FLR) to complete.
 * @xe: the &xe_device
 * @vfid: the VF identifier
 *
 * This function is for PF only.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_pf_control_wait_flr(struct xe_device *xe, unsigned int vfid)
{
	struct xe_gt *gt;
	unsigned int id;
	int result = 0;
	int err;

	for_each_gt(gt, xe, id) {
		err = xe_gt_sriov_pf_control_wait_flr(gt, vfid);
		result = result ? -EUCLEAN : err;
	}

	return result;
}

/**
 * xe_sriov_pf_control_sync_flr() - Synchronize a VF FLR between all GTs.
 * @xe: the &xe_device
 * @vfid: the VF identifier
 *
 * This function is for PF only.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_pf_control_sync_flr(struct xe_device *xe, unsigned int vfid)
{
	struct xe_gt *gt;
	unsigned int id;
	int ret;

	for_each_gt(gt, xe, id) {
		ret = xe_gt_sriov_pf_control_sync_flr(gt, vfid, false);
		if (ret < 0)
			return ret;
	}
	for_each_gt(gt, xe, id) {
		ret = xe_gt_sriov_pf_control_sync_flr(gt, vfid, true);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/**
 * xe_sriov_pf_control_trigger_save_vf() - Start VF migration data SAVE sequence on all GTs.
 * @xe: the &xe_device
 * @vfid: the VF identifier
 *
 * This function is for PF only.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_pf_control_trigger_save_vf(struct xe_device *xe, unsigned int vfid)
{
	struct xe_gt *gt;
	unsigned int id;
	int ret;

	ret = xe_sriov_packet_save_init(xe, vfid);
	if (ret)
		return ret;

	for_each_gt(gt, xe, id) {
		xe_gt_sriov_pf_migration_save_init(gt, vfid);

		ret = xe_gt_sriov_pf_control_trigger_save_vf(gt, vfid);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * xe_sriov_pf_control_finish_save_vf() - Complete VF migration data SAVE sequence on all GTs.
 * @xe: the &xe_device
 * @vfid: the VF identifier
 *
 * This function is for PF only.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_pf_control_finish_save_vf(struct xe_device *xe, unsigned int vfid)
{
	struct xe_gt *gt;
	unsigned int id;
	int ret;

	for_each_gt(gt, xe, id) {
		ret = xe_gt_sriov_pf_control_finish_save_vf(gt, vfid);
		if (ret)
			break;
	}

	return ret;
}

/**
 * xe_sriov_pf_control_trigger_restore_vf() - Start VF migration data RESTORE sequence on all GTs.
 * @xe: the &xe_device
 * @vfid: the VF identifier
 *
 * This function is for PF only.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_pf_control_trigger_restore_vf(struct xe_device *xe, unsigned int vfid)
{
	struct xe_gt *gt;
	unsigned int id;
	int ret;

	for_each_gt(gt, xe, id) {
		ret = xe_gt_sriov_pf_control_trigger_restore_vf(gt, vfid);
		if (ret)
			return ret;
	}

	return ret;
}

/**
 * xe_sriov_pf_control_finish_restore_vf() - Complete VF migration data RESTORE sequence on all GTs.
 * @xe: the &xe_device
 * @vfid: the VF identifier
 *
 * This function is for PF only.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_pf_control_finish_restore_vf(struct xe_device *xe, unsigned int vfid)
{
	struct xe_gt *gt;
	unsigned int id;
	int ret;

	for_each_gt(gt, xe, id) {
		ret = xe_gt_sriov_pf_control_finish_restore_vf(gt, vfid);
		if (ret)
			break;
	}

	return ret;
}
