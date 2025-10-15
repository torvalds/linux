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
			continue;
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

	pf_unprovision_vfs(xe, num_vfs);
	return 0;
}
