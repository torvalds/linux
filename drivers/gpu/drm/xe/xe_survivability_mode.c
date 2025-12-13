// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include "xe_survivability_mode.h"
#include "xe_survivability_mode_types.h"

#include <linux/kobject.h>
#include <linux/pci.h>
#include <linux/sysfs.h>

#include "xe_configfs.h"
#include "xe_device.h"
#include "xe_gt.h"
#include "xe_heci_gsc.h"
#include "xe_i2c.h"
#include "xe_mmio.h"
#include "xe_pcode_api.h"
#include "xe_vsec.h"

#define MAX_SCRATCH_MMIO 8

/**
 * DOC: Survivability Mode
 *
 * Survivability Mode is a software based workflow for recovering a system in a failed boot state
 * Here system recoverability is concerned with recovering the firmware responsible for boot.
 *
 * Boot Survivability
 * ===================
 *
 * Boot Survivability is implemented by loading the driver with bare minimum (no drm card) to allow
 * the firmware to be flashed through mei driver and collect telemetry. The driver's probe flow is
 * modified such that it enters survivability mode when pcode initialization is incomplete and boot
 * status denotes a failure.
 *
 * Survivability mode can also be entered manually using the survivability mode attribute available
 * through configfs which is beneficial in several usecases. It can be used to address scenarios
 * where pcode does not detect failure or for validation purposes. It can also be used in
 * In-Field-Repair (IFR) to repair a single card without impacting the other cards in a node.
 *
 * Use below command enable survivability mode manually::
 *
 *	# echo 1 > /sys/kernel/config/xe/0000:03:00.0/survivability_mode
 *
 * It is the responsibility of the user to clear the mode once firmware flash is complete.
 *
 * Refer :ref:`xe_configfs` for more details on how to use configfs
 *
 * Survivability mode is indicated by the below admin-only readable sysfs which provides additional
 * debug information::
 *
 *	/sys/bus/pci/devices/<device>/survivability_mode
 *
 * Capability Information:
 *	Provides boot status
 * Postcode Information:
 *	Provides information about the failure
 * Overflow Information
 *	Provides history of previous failures
 * Auxiliary Information
 *	Certain failures may have information in addition to postcode information
 *
 * Runtime Survivability
 * =====================
 *
 * Certain runtime firmware errors can cause the device to enter a wedged state
 * (:ref:`xe-device-wedging`) requiring a firmware flash to restore normal operation.
 * Runtime Survivability Mode indicates that a firmware flash is necessary to recover the device and
 * is indicated by the presence of survivability mode sysfs::
 *
 *	/sys/bus/pci/devices/<device>/survivability_mode
 *
 * Survivability mode sysfs provides information about the type of survivability mode.
 *
 * When such errors occur, userspace is notified with the drm device wedged uevent and runtime
 * survivability mode. User can then initiate a firmware flash using userspace tools like fwupd
 * to restore device to normal operation.
 */

static u32 aux_history_offset(u32 reg_value)
{
	return REG_FIELD_GET(AUXINFO_HISTORY_OFFSET, reg_value);
}

static void set_survivability_info(struct xe_mmio *mmio, struct xe_survivability_info *info,
				   int id, char *name)
{
	strscpy(info[id].name, name, sizeof(info[id].name));
	info[id].reg = PCODE_SCRATCH(id).raw;
	info[id].value = xe_mmio_read32(mmio, PCODE_SCRATCH(id));
}

static void populate_survivability_info(struct xe_device *xe)
{
	struct xe_survivability *survivability = &xe->survivability;
	struct xe_survivability_info *info = survivability->info;
	struct xe_mmio *mmio;
	u32 id = 0, reg_value;
	char name[NAME_MAX];
	int index;

	mmio = xe_root_tile_mmio(xe);
	set_survivability_info(mmio, info, id, "Capability Info");
	reg_value = info[id].value;

	if (reg_value & HISTORY_TRACKING) {
		id++;
		set_survivability_info(mmio, info, id, "Postcode Info");

		if (reg_value & OVERFLOW_SUPPORT) {
			id = REG_FIELD_GET(OVERFLOW_REG_OFFSET, reg_value);
			set_survivability_info(mmio, info, id, "Overflow Info");
		}
	}

	if (reg_value & AUXINFO_SUPPORT) {
		id = REG_FIELD_GET(AUXINFO_REG_OFFSET, reg_value);

		for (index = 0; id && reg_value; index++, reg_value = info[id].value,
		     id = aux_history_offset(reg_value)) {
			snprintf(name, NAME_MAX, "Auxiliary Info %d", index);
			set_survivability_info(mmio, info, id, name);
		}
	}
}

static void log_survivability_info(struct pci_dev *pdev)
{
	struct xe_device *xe = pdev_to_xe_device(pdev);
	struct xe_survivability *survivability = &xe->survivability;
	struct xe_survivability_info *info = survivability->info;
	int id;

	dev_info(&pdev->dev, "Survivability Boot Status : Critical Failure (%d)\n",
		 survivability->boot_status);
	for (id = 0; id < MAX_SCRATCH_MMIO; id++) {
		if (info[id].reg)
			dev_info(&pdev->dev, "%s: 0x%x - 0x%x\n", info[id].name,
				 info[id].reg, info[id].value);
	}
}

static int check_boot_failure(struct xe_device *xe)
{
	struct xe_survivability *survivability = &xe->survivability;

	return survivability->boot_status == NON_CRITICAL_FAILURE ||
		survivability->boot_status == CRITICAL_FAILURE;
}

static ssize_t survivability_mode_show(struct device *dev,
				       struct device_attribute *attr, char *buff)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct xe_device *xe = pdev_to_xe_device(pdev);
	struct xe_survivability *survivability = &xe->survivability;
	struct xe_survivability_info *info = survivability->info;
	int index = 0, count = 0;

	count += sysfs_emit_at(buff, count, "Survivability mode type: %s\n",
			       survivability->type ? "Runtime" : "Boot");

	if (!check_boot_failure(xe))
		return count;

	for (index = 0; index < MAX_SCRATCH_MMIO; index++) {
		if (info[index].reg)
			count += sysfs_emit_at(buff, count, "%s: 0x%x - 0x%x\n", info[index].name,
					       info[index].reg, info[index].value);
	}

	return count;
}

static DEVICE_ATTR_ADMIN_RO(survivability_mode);

static void xe_survivability_mode_fini(void *arg)
{
	struct xe_device *xe = arg;
	struct pci_dev *pdev = to_pci_dev(xe->drm.dev);
	struct device *dev = &pdev->dev;

	sysfs_remove_file(&dev->kobj, &dev_attr_survivability_mode.attr);
}

static int create_survivability_sysfs(struct pci_dev *pdev)
{
	struct device *dev = &pdev->dev;
	struct xe_device *xe = pdev_to_xe_device(pdev);
	int ret;

	/* create survivability mode sysfs */
	ret = sysfs_create_file(&dev->kobj, &dev_attr_survivability_mode.attr);
	if (ret) {
		dev_warn(dev, "Failed to create survivability sysfs files\n");
		return ret;
	}

	ret = devm_add_action_or_reset(xe->drm.dev,
				       xe_survivability_mode_fini, xe);
	if (ret)
		return ret;

	return 0;
}

static int enable_boot_survivability_mode(struct pci_dev *pdev)
{
	struct device *dev = &pdev->dev;
	struct xe_device *xe = pdev_to_xe_device(pdev);
	struct xe_survivability *survivability = &xe->survivability;
	int ret = 0;

	ret = create_survivability_sysfs(pdev);
	if (ret)
		return ret;

	/* Make sure xe_heci_gsc_init() knows about survivability mode */
	survivability->mode = true;

	ret = xe_heci_gsc_init(xe);
	if (ret)
		goto err;

	xe_vsec_init(xe);

	ret = xe_i2c_probe(xe);
	if (ret)
		goto err;

	dev_err(dev, "In Survivability Mode\n");

	return 0;

err:
	survivability->mode = false;
	return ret;
}

static int init_survivability_mode(struct xe_device *xe)
{
	struct xe_survivability *survivability = &xe->survivability;
	struct xe_survivability_info *info;

	survivability->size = MAX_SCRATCH_MMIO;

	info = devm_kcalloc(xe->drm.dev, survivability->size, sizeof(*info),
			    GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	survivability->info = info;

	populate_survivability_info(xe);

	return 0;
}

/**
 * xe_survivability_mode_is_boot_enabled- check if boot survivability mode is enabled
 * @xe: xe device instance
 *
 * Returns true if in boot survivability mode of type, else false
 */
bool xe_survivability_mode_is_boot_enabled(struct xe_device *xe)
{
	struct xe_survivability *survivability = &xe->survivability;

	return survivability->mode && survivability->type == XE_SURVIVABILITY_TYPE_BOOT;
}

/**
 * xe_survivability_mode_is_requested - check if it's possible to enable survivability
 *					mode that was requested by firmware or userspace
 * @xe: xe device instance
 *
 * This function reads configfs and  boot status from Pcode.
 *
 * Return: true if platform support is available and boot status indicates
 * failure or if survivability mode is requested, false otherwise.
 */
bool xe_survivability_mode_is_requested(struct xe_device *xe)
{
	struct xe_survivability *survivability = &xe->survivability;
	struct xe_mmio *mmio = xe_root_tile_mmio(xe);
	struct pci_dev *pdev = to_pci_dev(xe->drm.dev);
	u32 data;
	bool survivability_mode;

	if (!IS_DGFX(xe) || IS_SRIOV_VF(xe) || xe->info.platform < XE_BATTLEMAGE)
		return false;

	survivability_mode = xe_configfs_get_survivability_mode(pdev);
	/* Enable survivability mode if set via configfs */
	if (survivability_mode)
		return true;

	data = xe_mmio_read32(mmio, PCODE_SCRATCH(0));
	survivability->boot_status = REG_FIELD_GET(BOOT_STATUS, data);

	return check_boot_failure(xe);
}

/**
 * xe_survivability_mode_runtime_enable - Initialize and enable runtime survivability mode
 * @xe: xe device instance
 *
 * Initialize survivability information and enable runtime survivability mode.
 * Runtime survivability mode is enabled when certain errors cause the device to be
 * in non-recoverable state. The device is declared wedged with the appropriate
 * recovery method and survivability mode sysfs exposed to userspace
 *
 * Return: 0 if runtime survivability mode is enabled, negative error code otherwise.
 */
int xe_survivability_mode_runtime_enable(struct xe_device *xe)
{
	struct xe_survivability *survivability = &xe->survivability;
	struct pci_dev *pdev = to_pci_dev(xe->drm.dev);
	int ret;

	if (!IS_DGFX(xe) || IS_SRIOV_VF(xe) || xe->info.platform < XE_BATTLEMAGE) {
		dev_err(&pdev->dev, "Runtime Survivability Mode not supported\n");
		return -EINVAL;
	}

	ret = init_survivability_mode(xe);
	if (ret)
		return ret;

	ret = create_survivability_sysfs(pdev);
	if (ret)
		dev_err(&pdev->dev, "Failed to create survivability mode sysfs\n");

	survivability->type = XE_SURVIVABILITY_TYPE_RUNTIME;
	dev_err(&pdev->dev, "Runtime Survivability mode enabled\n");

	xe_device_set_wedged_method(xe, DRM_WEDGE_RECOVERY_VENDOR);
	xe_device_declare_wedged(xe);
	dev_err(&pdev->dev, "Firmware flash required, Please refer to the userspace documentation for more details!\n");

	return 0;
}

/**
 * xe_survivability_mode_boot_enable - Initialize and enable boot survivability mode
 * @xe: xe device instance
 *
 * Initialize survivability information and enable boot survivability mode
 *
 * Return: 0 if boot survivability mode is enabled or not requested, negative error
 * code otherwise.
 */
int xe_survivability_mode_boot_enable(struct xe_device *xe)
{
	struct xe_survivability *survivability = &xe->survivability;
	struct pci_dev *pdev = to_pci_dev(xe->drm.dev);
	int ret;

	if (!xe_survivability_mode_is_requested(xe))
		return 0;

	ret = init_survivability_mode(xe);
	if (ret)
		return ret;

	/* Log breadcrumbs but do not enter survivability mode for Critical boot errors */
	if (survivability->boot_status == CRITICAL_FAILURE) {
		log_survivability_info(pdev);
		return -ENXIO;
	}

	survivability->type = XE_SURVIVABILITY_TYPE_BOOT;

	return enable_boot_survivability_mode(pdev);
}
