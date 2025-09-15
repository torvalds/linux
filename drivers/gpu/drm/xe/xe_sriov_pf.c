// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#include <linux/debugfs.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_managed.h>

#include "xe_assert.h"
#include "xe_device.h"
#include "xe_gt_sriov_pf.h"
#include "xe_module.h"
#include "xe_sriov.h"
#include "xe_sriov_pf.h"
#include "xe_sriov_pf_helpers.h"
#include "xe_sriov_pf_service.h"
#include "xe_sriov_printk.h"

static unsigned int wanted_max_vfs(struct xe_device *xe)
{
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

	xe_sriov_pf_service_init(xe);

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

static int simple_show(struct seq_file *m, void *data)
{
	struct drm_printer p = drm_seq_file_printer(m);
	struct drm_info_node *node = m->private;
	struct dentry *parent = node->dent->d_parent;
	struct xe_device *xe = parent->d_inode->i_private;
	void (*print)(struct xe_device *, struct drm_printer *) = node->info_ent->data;

	print(xe, &p);
	return 0;
}

static const struct drm_info_list debugfs_list[] = {
	{ .name = "vfs", .show = simple_show, .data = xe_sriov_pf_print_vfs_summary },
	{ .name = "versions", .show = simple_show, .data = xe_sriov_pf_service_print_versions },
};

/**
 * xe_sriov_pf_debugfs_register - Register PF debugfs attributes.
 * @xe: the &xe_device
 * @root: the root &dentry
 *
 * Prepare debugfs attributes exposed by the PF.
 */
void xe_sriov_pf_debugfs_register(struct xe_device *xe, struct dentry *root)
{
	struct drm_minor *minor = xe->drm.primary;
	struct dentry *parent;

	/*
	 *      /sys/kernel/debug/dri/0/
	 *      ├── pf
	 *      │   ├── ...
	 */
	parent = debugfs_create_dir("pf", root);
	if (IS_ERR(parent))
		return;
	parent->d_inode->i_private = xe;

	drm_debugfs_create_files(debugfs_list, ARRAY_SIZE(debugfs_list), parent, minor);
}
