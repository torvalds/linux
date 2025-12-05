// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include "xe_assert.h"
#include "xe_device.h"
#include "xe_gt_sriov_pf_config.h"
#include "xe_gt_sriov_pf_policy.h"
#include "xe_sriov.h"
#include "xe_sriov_pf_helpers.h"
#include "xe_sriov_pf_provision.h"
#include "xe_sriov_pf_provision_types.h"
#include "xe_sriov_printk.h"

static const char *mode_to_string(enum xe_sriov_provisioning_mode mode)
{
	switch (mode) {
	case XE_SRIOV_PROVISIONING_MODE_AUTO:
		return "auto";
	case XE_SRIOV_PROVISIONING_MODE_CUSTOM:
		return "custom";
	default:
		return "<invalid>";
	}
}

static bool pf_auto_provisioning_mode(struct xe_device *xe)
{
	xe_assert(xe, IS_SRIOV_PF(xe));

	return xe->sriov.pf.provision.mode == XE_SRIOV_PROVISIONING_MODE_AUTO;
}

static bool pf_needs_provisioning(struct xe_gt *gt, unsigned int num_vfs)
{
	unsigned int n;

	for (n = 1; n <= num_vfs; n++)
		if (!xe_gt_sriov_pf_config_is_empty(gt, n))
			return false;

	return true;
}

static int pf_provision_vfs(struct xe_device *xe, unsigned int num_vfs)
{
	struct xe_gt *gt;
	unsigned int id;
	int result = 0;
	int err;

	for_each_gt(gt, xe, id) {
		if (!pf_needs_provisioning(gt, num_vfs))
			return -EUCLEAN;
		err = xe_gt_sriov_pf_config_set_fair(gt, VFID(1), num_vfs);
		result = result ?: err;
	}

	return result;
}

static void pf_unprovision_vfs(struct xe_device *xe, unsigned int num_vfs)
{
	struct xe_gt *gt;
	unsigned int id;
	unsigned int n;

	for_each_gt(gt, xe, id)
		for (n = 1; n <= num_vfs; n++)
			xe_gt_sriov_pf_config_release(gt, n, true);
}

static void pf_unprovision_all_vfs(struct xe_device *xe)
{
	pf_unprovision_vfs(xe, xe_sriov_pf_get_totalvfs(xe));
}

/**
 * xe_sriov_pf_provision_vfs() - Provision VFs in auto-mode.
 * @xe: the PF &xe_device
 * @num_vfs: the number of VFs to auto-provision
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_pf_provision_vfs(struct xe_device *xe, unsigned int num_vfs)
{
	xe_assert(xe, IS_SRIOV_PF(xe));

	if (!pf_auto_provisioning_mode(xe))
		return 0;

	return pf_provision_vfs(xe, num_vfs);
}

/**
 * xe_sriov_pf_unprovision_vfs() - Unprovision VFs in auto-mode.
 * @xe: the PF &xe_device
 * @num_vfs: the number of VFs to unprovision
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_pf_unprovision_vfs(struct xe_device *xe, unsigned int num_vfs)
{
	xe_assert(xe, IS_SRIOV_PF(xe));

	if (!pf_auto_provisioning_mode(xe))
		return 0;

	pf_unprovision_vfs(xe, num_vfs);
	return 0;
}

/**
 * xe_sriov_pf_provision_set_mode() - Change VFs provision mode.
 * @xe: the PF &xe_device
 * @mode: the new VFs provisioning mode
 *
 * When changing from AUTO to CUSTOM mode, any already allocated VFs resources
 * will remain allocated and will not be released upon VFs disabling.
 *
 * When changing back to AUTO mode, if VFs are not enabled, already allocated
 * VFs resources will be immediately released. If VFs are still enabled, such
 * mode change is rejected.
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_pf_provision_set_mode(struct xe_device *xe, enum xe_sriov_provisioning_mode mode)
{
	xe_assert(xe, IS_SRIOV_PF(xe));

	if (mode == xe->sriov.pf.provision.mode)
		return 0;

	if (mode == XE_SRIOV_PROVISIONING_MODE_AUTO) {
		if (xe_sriov_pf_num_vfs(xe)) {
			xe_sriov_dbg(xe, "can't restore %s: VFs must be disabled!\n",
				     mode_to_string(mode));
			return -EBUSY;
		}
		pf_unprovision_all_vfs(xe);
	}

	xe_sriov_dbg(xe, "mode %s changed to %s by %ps\n",
		     mode_to_string(xe->sriov.pf.provision.mode),
		     mode_to_string(mode), __builtin_return_address(0));
	xe->sriov.pf.provision.mode = mode;
	return 0;
}

/**
 * xe_sriov_pf_provision_bulk_apply_eq() - Change execution quantum for all VFs and PF.
 * @xe: the PF &xe_device
 * @eq: execution quantum in [ms] to set
 *
 * Change execution quantum (EQ) provisioning on all tiles/GTs.
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_pf_provision_bulk_apply_eq(struct xe_device *xe, u32 eq)
{
	struct xe_gt *gt;
	unsigned int id;
	int result = 0;
	int err;

	guard(mutex)(xe_sriov_pf_master_mutex(xe));

	for_each_gt(gt, xe, id) {
		err = xe_gt_sriov_pf_config_bulk_set_exec_quantum_locked(gt, eq);
		result = result ?: err;
	}

	return result;
}

/**
 * xe_sriov_pf_provision_apply_vf_eq() - Change VF's execution quantum.
 * @xe: the PF &xe_device
 * @vfid: the VF identifier
 * @eq: execution quantum in [ms] to set
 *
 * Change VF's execution quantum (EQ) provisioning on all tiles/GTs.
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_pf_provision_apply_vf_eq(struct xe_device *xe, unsigned int vfid, u32 eq)
{
	struct xe_gt *gt;
	unsigned int id;
	int result = 0;
	int err;

	guard(mutex)(xe_sriov_pf_master_mutex(xe));

	for_each_gt(gt, xe, id) {
		err = xe_gt_sriov_pf_config_set_exec_quantum_locked(gt, vfid, eq);
		result = result ?: err;
	}

	return result;
}

static int pf_report_unclean(struct xe_gt *gt, unsigned int vfid,
			     const char *what, u32 found, u32 expected)
{
	char name[8];

	xe_sriov_dbg(gt_to_xe(gt), "%s on GT%u has %s=%u (expected %u)\n",
		     xe_sriov_function_name(vfid, name, sizeof(name)),
		     gt->info.id, what, found, expected);
	return -EUCLEAN;
}

/**
 * xe_sriov_pf_provision_query_vf_eq() - Query VF's execution quantum.
 * @xe: the PF &xe_device
 * @vfid: the VF identifier
 * @eq: placeholder for the returned execution quantum in [ms]
 *
 * Query VF's execution quantum (EQ) provisioning from all tiles/GTs.
 * If values across tiles/GTs are inconsistent then -EUCLEAN error will be returned.
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_pf_provision_query_vf_eq(struct xe_device *xe, unsigned int vfid, u32 *eq)
{
	struct xe_gt *gt;
	unsigned int id;
	int count = 0;
	u32 value;

	guard(mutex)(xe_sriov_pf_master_mutex(xe));

	for_each_gt(gt, xe, id) {
		value = xe_gt_sriov_pf_config_get_exec_quantum_locked(gt, vfid);
		if (!count++)
			*eq = value;
		else if (value != *eq)
			return pf_report_unclean(gt, vfid, "EQ", value, *eq);
	}

	return !count ? -ENODATA : 0;
}

/**
 * xe_sriov_pf_provision_bulk_apply_pt() - Change preemption timeout for all VFs and PF.
 * @xe: the PF &xe_device
 * @pt: preemption timeout in [us] to set
 *
 * Change preemption timeout (PT) provisioning on all tiles/GTs.
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_pf_provision_bulk_apply_pt(struct xe_device *xe, u32 pt)
{
	struct xe_gt *gt;
	unsigned int id;
	int result = 0;
	int err;

	guard(mutex)(xe_sriov_pf_master_mutex(xe));

	for_each_gt(gt, xe, id) {
		err = xe_gt_sriov_pf_config_bulk_set_preempt_timeout_locked(gt, pt);
		result = result ?: err;
	}

	return result;
}

/**
 * xe_sriov_pf_provision_apply_vf_pt() - Change VF's preemption timeout.
 * @xe: the PF &xe_device
 * @vfid: the VF identifier
 * @pt: preemption timeout in [us] to set
 *
 * Change VF's preemption timeout (PT) provisioning on all tiles/GTs.
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_pf_provision_apply_vf_pt(struct xe_device *xe, unsigned int vfid, u32 pt)
{
	struct xe_gt *gt;
	unsigned int id;
	int result = 0;
	int err;

	guard(mutex)(xe_sriov_pf_master_mutex(xe));

	for_each_gt(gt, xe, id) {
		err = xe_gt_sriov_pf_config_set_preempt_timeout_locked(gt, vfid, pt);
		result = result ?: err;
	}

	return result;
}

/**
 * xe_sriov_pf_provision_query_vf_pt() - Query VF's preemption timeout.
 * @xe: the PF &xe_device
 * @vfid: the VF identifier
 * @pt: placeholder for the returned preemption timeout in [us]
 *
 * Query VF's preemption timeout (PT) provisioning from all tiles/GTs.
 * If values across tiles/GTs are inconsistent then -EUCLEAN error will be returned.
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_pf_provision_query_vf_pt(struct xe_device *xe, unsigned int vfid, u32 *pt)
{
	struct xe_gt *gt;
	unsigned int id;
	int count = 0;
	u32 value;

	guard(mutex)(xe_sriov_pf_master_mutex(xe));

	for_each_gt(gt, xe, id) {
		value = xe_gt_sriov_pf_config_get_preempt_timeout_locked(gt, vfid);
		if (!count++)
			*pt = value;
		else if (value != *pt)
			return pf_report_unclean(gt, vfid, "PT", value, *pt);
	}

	return !count ? -ENODATA : 0;
}

/**
 * xe_sriov_pf_provision_bulk_apply_priority() - Change scheduling priority of all VFs and PF.
 * @xe: the PF &xe_device
 * @prio: scheduling priority to set
 *
 * Change the scheduling priority provisioning on all tiles/GTs.
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_pf_provision_bulk_apply_priority(struct xe_device *xe, u32 prio)
{
	bool sched_if_idle;
	struct xe_gt *gt;
	unsigned int id;
	int result = 0;
	int err;

	/*
	 * Currently, priority changes that involves VFs are only allowed using
	 * the 'sched_if_idle' policy KLV, so only LOW and NORMAL are supported.
	 */
	xe_assert(xe, prio < GUC_SCHED_PRIORITY_HIGH);
	sched_if_idle = prio == GUC_SCHED_PRIORITY_NORMAL;

	for_each_gt(gt, xe, id) {
		err = xe_gt_sriov_pf_policy_set_sched_if_idle(gt, sched_if_idle);
		result = result ?: err;
	}

	return result;
}

/**
 * xe_sriov_pf_provision_apply_vf_priority() - Change VF's scheduling priority.
 * @xe: the PF &xe_device
 * @vfid: the VF identifier
 * @prio: scheduling priority to set
 *
 * Change VF's scheduling priority provisioning on all tiles/GTs.
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_pf_provision_apply_vf_priority(struct xe_device *xe, unsigned int vfid, u32 prio)
{
	struct xe_gt *gt;
	unsigned int id;
	int result = 0;
	int err;

	for_each_gt(gt, xe, id) {
		err = xe_gt_sriov_pf_config_set_sched_priority(gt, vfid, prio);
		result = result ?: err;
	}

	return result;
}

/**
 * xe_sriov_pf_provision_query_vf_priority() - Query VF's scheduling priority.
 * @xe: the PF &xe_device
 * @vfid: the VF identifier
 * @prio: placeholder for the returned scheduling priority
 *
 * Query VF's scheduling priority provisioning from all tiles/GTs.
 * If values across tiles/GTs are inconsistent then -EUCLEAN error will be returned.
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_pf_provision_query_vf_priority(struct xe_device *xe, unsigned int vfid, u32 *prio)
{
	struct xe_gt *gt;
	unsigned int id;
	int count = 0;
	u32 value;

	for_each_gt(gt, xe, id) {
		value = xe_gt_sriov_pf_config_get_sched_priority(gt, vfid);
		if (!count++)
			*prio = value;
		else if (value != *prio)
			return pf_report_unclean(gt, vfid, "priority", value, *prio);
	}

	return !count ? -ENODATA : 0;
}
