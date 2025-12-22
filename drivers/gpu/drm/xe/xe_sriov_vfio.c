// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <drm/intel/xe_sriov_vfio.h>
#include <linux/cleanup.h>

#include "xe_pci.h"
#include "xe_pm.h"
#include "xe_sriov_pf_control.h"
#include "xe_sriov_pf_helpers.h"
#include "xe_sriov_pf_migration.h"

struct xe_device *xe_sriov_vfio_get_pf(struct pci_dev *pdev)
{
	return xe_pci_to_pf_device(pdev);
}
EXPORT_SYMBOL_FOR_MODULES(xe_sriov_vfio_get_pf, "xe-vfio-pci");

bool xe_sriov_vfio_migration_supported(struct xe_device *xe)
{
	if (!IS_SRIOV_PF(xe))
		return false;

	return xe_sriov_pf_migration_supported(xe);
}
EXPORT_SYMBOL_FOR_MODULES(xe_sriov_vfio_migration_supported, "xe-vfio-pci");

#define DEFINE_XE_SRIOV_VFIO_FUNCTION(_type, _func, _impl)			\
_type xe_sriov_vfio_##_func(struct xe_device *xe, unsigned int vfid)		\
{										\
	if (!IS_SRIOV_PF(xe))							\
		return -EPERM;							\
	if (vfid == PFID || vfid > xe_sriov_pf_num_vfs(xe))			\
		return -EINVAL;							\
										\
	guard(xe_pm_runtime_noresume)(xe);					\
										\
	return xe_sriov_pf_##_impl(xe, vfid);					\
}										\
EXPORT_SYMBOL_FOR_MODULES(xe_sriov_vfio_##_func, "xe-vfio-pci")

DEFINE_XE_SRIOV_VFIO_FUNCTION(int, wait_flr_done, control_wait_flr);
DEFINE_XE_SRIOV_VFIO_FUNCTION(int, suspend_device, control_pause_vf);
DEFINE_XE_SRIOV_VFIO_FUNCTION(int, resume_device, control_resume_vf);
DEFINE_XE_SRIOV_VFIO_FUNCTION(int, stop_copy_enter, control_trigger_save_vf);
DEFINE_XE_SRIOV_VFIO_FUNCTION(int, stop_copy_exit, control_finish_save_vf);
DEFINE_XE_SRIOV_VFIO_FUNCTION(int, resume_data_enter, control_trigger_restore_vf);
DEFINE_XE_SRIOV_VFIO_FUNCTION(int, resume_data_exit, control_finish_restore_vf);
DEFINE_XE_SRIOV_VFIO_FUNCTION(int, error, control_stop_vf);
DEFINE_XE_SRIOV_VFIO_FUNCTION(ssize_t, stop_copy_size, migration_size);

ssize_t xe_sriov_vfio_data_read(struct xe_device *xe, unsigned int vfid,
				char __user *buf, size_t len)
{
	if (!IS_SRIOV_PF(xe))
		return -EPERM;
	if (vfid == PFID || vfid > xe_sriov_pf_num_vfs(xe))
		return -EINVAL;

	guard(xe_pm_runtime_noresume)(xe);

	return xe_sriov_pf_migration_read(xe, vfid, buf, len);
}
EXPORT_SYMBOL_FOR_MODULES(xe_sriov_vfio_data_read, "xe-vfio-pci");

ssize_t xe_sriov_vfio_data_write(struct xe_device *xe, unsigned int vfid,
				 const char __user *buf, size_t len)
{
	if (!IS_SRIOV_PF(xe))
		return -EPERM;
	if (vfid == PFID || vfid > xe_sriov_pf_num_vfs(xe))
		return -EINVAL;

	guard(xe_pm_runtime_noresume)(xe);

	return xe_sriov_pf_migration_write(xe, vfid, buf, len);
}
EXPORT_SYMBOL_FOR_MODULES(xe_sriov_vfio_data_write, "xe-vfio-pci");
