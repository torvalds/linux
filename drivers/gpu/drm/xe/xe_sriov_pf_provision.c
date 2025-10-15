// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include "xe_assert.h"
#include "xe_device.h"
#include "xe_gt_sriov_pf_config.h"
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
