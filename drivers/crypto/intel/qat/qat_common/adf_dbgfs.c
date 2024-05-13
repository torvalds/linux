// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2023 Intel Corporation */

#include <linux/debugfs.h>
#include "adf_accel_devices.h"
#include "adf_cfg.h"
#include "adf_common_drv.h"
#include "adf_dbgfs.h"
#include "adf_fw_counters.h"
#include "adf_heartbeat_dbgfs.h"

/**
 * adf_dbgfs_init() - add persistent debugfs entries
 * @accel_dev:  Pointer to acceleration device.
 *
 * This function creates debugfs entries that are persistent through a device
 * state change (from up to down or vice versa).
 */
void adf_dbgfs_init(struct adf_accel_dev *accel_dev)
{
	char name[ADF_DEVICE_NAME_LENGTH];
	void *ret;

	/* Create dev top level debugfs entry */
	snprintf(name, sizeof(name), "%s%s_%s", ADF_DEVICE_NAME_PREFIX,
		 accel_dev->hw_device->dev_class->name,
		 pci_name(accel_dev->accel_pci_dev.pci_dev));

	ret = debugfs_create_dir(name, NULL);
	if (IS_ERR_OR_NULL(ret))
		return;

	accel_dev->debugfs_dir = ret;

	adf_cfg_dev_dbgfs_add(accel_dev);
}
EXPORT_SYMBOL_GPL(adf_dbgfs_init);

/**
 * adf_dbgfs_exit() - remove persistent debugfs entries
 * @accel_dev:  Pointer to acceleration device.
 */
void adf_dbgfs_exit(struct adf_accel_dev *accel_dev)
{
	adf_cfg_dev_dbgfs_rm(accel_dev);
	debugfs_remove(accel_dev->debugfs_dir);
}
EXPORT_SYMBOL_GPL(adf_dbgfs_exit);

/**
 * adf_dbgfs_add() - add non-persistent debugfs entries
 * @accel_dev:  Pointer to acceleration device.
 *
 * This function creates debugfs entries that are not persistent through
 * a device state change (from up to down or vice versa).
 */
void adf_dbgfs_add(struct adf_accel_dev *accel_dev)
{
	if (!accel_dev->debugfs_dir)
		return;

	if (!accel_dev->is_vf) {
		adf_fw_counters_dbgfs_add(accel_dev);
		adf_heartbeat_dbgfs_add(accel_dev);
	}
}

/**
 * adf_dbgfs_rm() - remove non-persistent debugfs entries
 * @accel_dev:  Pointer to acceleration device.
 */
void adf_dbgfs_rm(struct adf_accel_dev *accel_dev)
{
	if (!accel_dev->debugfs_dir)
		return;

	if (!accel_dev->is_vf) {
		adf_heartbeat_dbgfs_rm(accel_dev);
		adf_fw_counters_dbgfs_rm(accel_dev);
	}
}
