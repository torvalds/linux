// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#include "xe_assert.h"
#include "xe_device.h"
#include "xe_gt_sriov_pf_config.h"
#include "xe_pci_sriov.h"
#include "xe_pm.h"
#include "xe_sriov.h"
#include "xe_sriov_pf_helpers.h"
#include "xe_sriov_printk.h"

static int pf_provision_vfs(struct xe_device *xe, unsigned int num_vfs)
{
	struct xe_gt *gt;
	unsigned int id;
	int result = 0, err;

	for_each_gt(gt, xe, id) {
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

static int pf_enable_vfs(struct xe_device *xe, int num_vfs)
{
	struct pci_dev *pdev = to_pci_dev(xe->drm.dev);
	int total_vfs = xe_sriov_pf_get_totalvfs(xe);
	int err;

	xe_assert(xe, IS_SRIOV_PF(xe));
	xe_assert(xe, num_vfs > 0);
	xe_assert(xe, num_vfs <= total_vfs);
	xe_sriov_dbg(xe, "enabling %u VF%s\n", num_vfs, str_plural(num_vfs));

	/*
	 * We must hold additional reference to the runtime PM to keep PF in D0
	 * during VFs lifetime, as our VFs do not implement the PM capability.
	 *
	 * With PF being in D0 state, all VFs will also behave as in D0 state.
	 * This will also keep GuC alive with all VFs' configurations.
	 *
	 * We will release this additional PM reference in pf_disable_vfs().
	 */
	xe_pm_runtime_get_noresume(xe);

	err = pf_provision_vfs(xe, num_vfs);
	if (err < 0)
		goto failed;

	err = pci_enable_sriov(pdev, num_vfs);
	if (err < 0)
		goto failed;

	xe_sriov_info(xe, "Enabled %u of %u VF%s\n",
		      num_vfs, total_vfs, str_plural(total_vfs));
	return num_vfs;

failed:
	pf_unprovision_vfs(xe, num_vfs);
	xe_pm_runtime_put(xe);

	xe_sriov_notice(xe, "Failed to enable %u VF%s (%pe)\n",
			num_vfs, str_plural(num_vfs), ERR_PTR(err));
	return err;
}

static int pf_disable_vfs(struct xe_device *xe)
{
	struct device *dev = xe->drm.dev;
	struct pci_dev *pdev = to_pci_dev(dev);
	u16 num_vfs = pci_num_vf(pdev);

	xe_assert(xe, IS_SRIOV_PF(xe));
	xe_sriov_dbg(xe, "disabling %u VF%s\n", num_vfs, str_plural(num_vfs));

	if (!num_vfs)
		return 0;

	pci_disable_sriov(pdev);

	pf_unprovision_vfs(xe, num_vfs);

	/* not needed anymore - see pf_enable_vfs() */
	xe_pm_runtime_put(xe);

	xe_sriov_info(xe, "Disabled %u VF%s\n", num_vfs, str_plural(num_vfs));
	return 0;
}

/**
 * xe_pci_sriov_configure - Configure SR-IOV (enable/disable VFs).
 * @pdev: the &pci_dev
 * @num_vfs: number of VFs to enable or zero to disable all VFs
 *
 * This is the Xe implementation of struct pci_driver.sriov_configure callback.
 *
 * This callback will be called by the PCI subsystem to enable or disable SR-IOV
 * Virtual Functions (VFs) as requested by the used via the PCI sysfs interface.
 *
 * Return: number of configured VFs or a negative error code on failure.
 */
int xe_pci_sriov_configure(struct pci_dev *pdev, int num_vfs)
{
	struct xe_device *xe = pdev_to_xe_device(pdev);
	int ret;

	if (!IS_SRIOV_PF(xe))
		return -ENODEV;

	if (num_vfs < 0)
		return -EINVAL;

	if (num_vfs > xe_sriov_pf_get_totalvfs(xe))
		return -ERANGE;

	if (num_vfs && pci_num_vf(pdev))
		return -EBUSY;

	xe_pm_runtime_get(xe);
	if (num_vfs > 0)
		ret = pf_enable_vfs(xe, num_vfs);
	else
		ret = pf_disable_vfs(xe);
	xe_pm_runtime_put(xe);

	return ret;
}
