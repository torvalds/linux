// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#include <linux/debugfs.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_managed.h>

#include "xe_assert.h"
#include "xe_configfs.h"
#include "xe_device.h"
#include "xe_gt_sriov_pf.h"
#include "xe_module.h"
#include "xe_sriov.h"
#include "xe_sriov_pf.h"
#include "xe_sriov_pf_helpers.h"
#include "xe_sriov_pf_migration.h"
#include "xe_sriov_pf_service.h"
#include "xe_sriov_pf_sysfs.h"
#include "xe_sriov_printk.h"

static unsigned int wanted_max_vfs(struct xe_device *xe)
{
	if (IS_ENABLED(CONFIG_CONFIGFS_FS))
		return xe_configfs_get_max_vfs(to_pci_dev(xe->drm.dev));
	return xe_modparam.max_vfs;
}

static int pf_reduce_totalvfs(struct xe_device *xe, int limit)
{
	struct device *dev = xe->drm.dev;
	struct pci_dev *pdev = to_pci_dev(dev);
	int err;

	err = pci_sriov_set_totalvfs(pdev, limit);
	if (err)
		xe_sriov_notice(xe, "Failed to set number of VFs to %d (%pe)\n",
				limit, ERR_PTR(err));
	return err;
}

static bool pf_continue_as_native(struct xe_device *xe, const char *why)
{
	xe_sriov_dbg(xe, "%s, continuing as native\n", why);
	pf_reduce_totalvfs(xe, 0);
	return false;
}

/**
 * xe_sriov_pf_readiness - Check if PF functionality can be enabled.
 * @xe: the &xe_device to check
 *
 * This function is called as part of the SR-IOV probe to validate if all
 * PF prerequisites are satisfied and we can continue with enabling PF mode.
 *
 * Return: true if the PF mode can be turned on.
 */
bool xe_sriov_pf_readiness(struct xe_device *xe)
{
	struct device *dev = xe->drm.dev;
	struct pci_dev *pdev = to_pci_dev(dev);
	int totalvfs = pci_sriov_get_totalvfs(pdev);
	int newlimit = min_t(u16, wanted_max_vfs(xe), totalvfs);

	xe_assert(xe, totalvfs <= U16_MAX);

	if (!dev_is_pf(dev))
		return false;

	if (!xe_device_uc_enabled(xe))
		return pf_continue_as_native(xe, "Guc submission disabled");

	if (!newlimit)
		return pf_continue_as_native(xe, "all VFs disabled");

	pf_reduce_totalvfs(xe, newlimit);

	xe->sriov.pf.device_total_vfs = totalvfs;
	xe->sriov.pf.driver_max_vfs = newlimit;

	return true;
}

/**
 * xe_sriov_pf_init_early - Initialize SR-IOV PF specific data.
 * @xe: the &xe_device to initialize
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_pf_init_early(struct xe_device *xe)
{
	int err;

	xe_assert(xe, IS_SRIOV_PF(xe));

	xe->sriov.pf.vfs = drmm_kcalloc(&xe->drm, 1 + xe_sriov_pf_get_totalvfs(xe),
					sizeof(*xe->sriov.pf.vfs), GFP_KERNEL);
	if (!xe->sriov.pf.vfs)
		return -ENOMEM;

	err = drmm_mutex_init(&xe->drm, &xe->sriov.pf.master_lock);
	if (err)
		return err;

	err = xe_sriov_pf_migration_init(xe);
	if (err)
		return err;

	xe_guard_init(&xe->sriov.pf.guard_vfs_enabling, "vfs_enabling");

	xe_sriov_pf_service_init(xe);

	xe_mert_init_early(xe);

	return 0;
}

/**
 * xe_sriov_pf_init_late() - Late initialization of the SR-IOV PF.
 * @xe: the &xe_device to initialize
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_pf_init_late(struct xe_device *xe)
{
	struct xe_gt *gt;
	unsigned int id;
	int err;

	xe_assert(xe, IS_SRIOV_PF(xe));

	for_each_gt(gt, xe, id) {
		err = xe_gt_sriov_pf_init(gt);
		if (err)
			return err;
	}

	err = xe_sriov_pf_sysfs_init(xe);
	if (err)
		return err;

	return 0;
}

/**
 * xe_sriov_pf_wait_ready() - Wait until PF is ready to operate.
 * @xe: the &xe_device to test
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_pf_wait_ready(struct xe_device *xe)
{
	struct xe_gt *gt;
	unsigned int id;
	int err;

	if (xe_device_wedged(xe))
		return -ECANCELED;

	for_each_gt(gt, xe, id) {
		err = xe_gt_sriov_pf_wait_ready(gt);
		if (err)
			return err;
	}

	return 0;
}

/**
 * xe_sriov_pf_arm_guard() - Arm the guard for exclusive/lockdown mode.
 * @xe: the PF &xe_device
 * @guard: the &xe_guard to arm
 * @lockdown: arm for lockdown(true) or exclusive(false) mode
 * @who: the address of the new owner, or NULL if it's a caller
 *
 * This function can only be called on PF.
 *
 * It is a simple wrapper for xe_guard_arm() with additional debug
 * messages.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_pf_arm_guard(struct xe_device *xe, struct xe_guard *guard,
			  bool lockdown, void *who)
{
	void *new_owner = who ?: __builtin_return_address(0);
	int err;

	err = xe_guard_arm(guard, lockdown, new_owner);
	if (err) {
		xe_sriov_dbg(xe, "%s/%s mode denied (%pe) last owner %ps\n",
			     guard->name, xe_guard_mode_str(lockdown),
			     ERR_PTR(err), guard->owner);
		return err;
	}

	xe_sriov_dbg_verbose(xe, "%s/%s by %ps\n",
			     guard->name, xe_guard_mode_str(lockdown),
			     new_owner);
	return 0;
}

/**
 * xe_sriov_pf_disarm_guard() - Disarm the guard.
 * @xe: the PF &xe_device
 * @guard: the &xe_guard to disarm
 * @lockdown: disarm from lockdown(true) or exclusive(false) mode
 * @who: the address of the indirect owner, or NULL if it's a caller
 *
 * This function can only be called on PF.
 *
 * It is a simple wrapper for xe_guard_disarm() with additional debug
 * messages and xe_assert() to easily catch any illegal calls.
 */
void xe_sriov_pf_disarm_guard(struct xe_device *xe, struct xe_guard *guard,
			      bool lockdown, void *who)
{
	bool disarmed;

	xe_sriov_dbg_verbose(xe, "%s/%s by %ps\n",
			     guard->name, xe_guard_mode_str(lockdown),
			     who ?: __builtin_return_address(0));

	disarmed = xe_guard_disarm(guard, lockdown);
	xe_assert_msg(xe, disarmed, "%s/%s not armed? last owner %ps",
		      guard->name, xe_guard_mode_str(lockdown), guard->owner);
}

/**
 * xe_sriov_pf_lockdown() - Lockdown the PF to prevent VFs enabling.
 * @xe: the PF &xe_device
 *
 * This function can only be called on PF.
 *
 * Once the PF is locked down, it will not enable VFs.
 * If VFs are already enabled, the -EBUSY will be returned.
 * To allow the PF enable VFs again call xe_sriov_pf_end_lockdown().
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_pf_lockdown(struct xe_device *xe)
{
	xe_assert(xe, IS_SRIOV_PF(xe));

	return xe_sriov_pf_arm_guard(xe, &xe->sriov.pf.guard_vfs_enabling, true,
				     __builtin_return_address(0));
}

/**
 * xe_sriov_pf_end_lockdown() - Allow the PF to enable VFs again.
 * @xe: the PF &xe_device
 *
 * This function can only be called on PF.
 * See xe_sriov_pf_lockdown() for details.
 */
void xe_sriov_pf_end_lockdown(struct xe_device *xe)
{
	xe_assert(xe, IS_SRIOV_PF(xe));

	xe_sriov_pf_disarm_guard(xe, &xe->sriov.pf.guard_vfs_enabling, true,
				 __builtin_return_address(0));
}

/**
 * xe_sriov_pf_print_vfs_summary - Print SR-IOV PF information.
 * @xe: the &xe_device to print info from
 * @p: the &drm_printer
 *
 * Print SR-IOV PF related information into provided DRM printer.
 */
void xe_sriov_pf_print_vfs_summary(struct xe_device *xe, struct drm_printer *p)
{
	struct pci_dev *pdev = to_pci_dev(xe->drm.dev);

	xe_assert(xe, IS_SRIOV_PF(xe));

	drm_printf(p, "total: %u\n", xe->sriov.pf.device_total_vfs);
	drm_printf(p, "supported: %u\n", xe->sriov.pf.driver_max_vfs);
	drm_printf(p, "enabled: %u\n", pci_num_vf(pdev));
}
