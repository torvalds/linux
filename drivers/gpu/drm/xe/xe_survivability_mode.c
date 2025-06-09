// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include "xe_survivability_mode.h"
#include "xe_survivability_mode_types.h"

#include <linux/kobject.h>
#include <linux/pci.h>
#include <linux/sysfs.h>

#include "xe_device.h"
#include "xe_gt.h"
#include "xe_heci_gsc.h"
#include "xe_mmio.h"
#include "xe_pcode_api.h"
#include "xe_vsec.h"

#define MAX_SCRATCH_MMIO 8

/**
 * DOC: Xe Boot Survivability
 *
 * Boot Survivability is a software based workflow for recovering a system in a failed boot state
 * Here system recoverability is concerned with recovering the firmware responsible for boot.
 *
 * This is implemented by loading the driver with bare minimum (no drm card) to allow the firmware
 * to be flashed through mei and collect telemetry. The driver's probe flow is modified
 * such that it enters survivability mode when pcode initialization is incomplete and boot status
 * denotes a failure. The driver then  populates the survivability_mode PCI sysfs indicating
 * survivability mode and provides additional information required for debug
 *
 * KMD exposes below admin-only readable sysfs in survivability mode
 *
 * device/survivability_mode: The presence of this file indicates that the card is in survivability
 *			      mode. Also, provides additional information on why the driver entered
 *			      survivability mode.
 *
 *			      Capability Information - Provides boot status
 *			      Postcode Information   - Provides information about the failure
 *			      Overflow Information   - Provides history of previous failures
 *			      Auxiliary Information  - Certain failures may have information in
 *						       addition to postcode information
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

static ssize_t survivability_mode_show(struct device *dev,
				       struct device_attribute *attr, char *buff)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct xe_device *xe = pdev_to_xe_device(pdev);
	struct xe_survivability *survivability = &xe->survivability;
	struct xe_survivability_info *info = survivability->info;
	int index = 0, count = 0;

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

static int enable_survivability_mode(struct pci_dev *pdev)
{
	struct device *dev = &pdev->dev;
	struct xe_device *xe = pdev_to_xe_device(pdev);
	struct xe_survivability *survivability = &xe->survivability;
	int ret = 0;

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

	ret = xe_heci_gsc_init(xe);
	if (ret)
		return ret;

	xe_vsec_init(xe);

	survivability->mode = true;
	dev_err(dev, "In Survivability Mode\n");

	return 0;
}

/**
 * xe_survivability_mode_is_enabled - check if survivability mode is enabled
 * @xe: xe device instance
 *
 * Returns true if in survivability mode, false otherwise
 */
bool xe_survivability_mode_is_enabled(struct xe_device *xe)
{
	return xe->survivability.mode;
}

/**
 * xe_survivability_mode_required - checks if survivability mode is required
 * @xe: xe device instance
 *
 * This function reads the boot status from Pcode
 *
 * Return: true if boot status indicates failure, false otherwise
 */
bool xe_survivability_mode_required(struct xe_device *xe)
{
	struct xe_survivability *survivability = &xe->survivability;
	struct xe_mmio *mmio = xe_root_tile_mmio(xe);
	u32 data;

	if (!IS_DGFX(xe) || xe->info.platform < XE_BATTLEMAGE || IS_SRIOV_VF(xe))
		return false;

	data = xe_mmio_read32(mmio, PCODE_SCRATCH(0));
	survivability->boot_status = REG_FIELD_GET(BOOT_STATUS, data);

	return survivability->boot_status == NON_CRITICAL_FAILURE ||
		survivability->boot_status == CRITICAL_FAILURE;
}

/**
 * xe_survivability_mode_enable - Initialize and enable the survivability mode
 * @xe: xe device instance
 *
 * Initialize survivability information and enable survivability mode
 *
 * Return: 0 for success, negative error code otherwise.
 */
int xe_survivability_mode_enable(struct xe_device *xe)
{
	struct xe_survivability *survivability = &xe->survivability;
	struct xe_survivability_info *info;
	struct pci_dev *pdev = to_pci_dev(xe->drm.dev);

	survivability->size = MAX_SCRATCH_MMIO;

	info = devm_kcalloc(xe->drm.dev, survivability->size, sizeof(*info),
			    GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	survivability->info = info;

	populate_survivability_info(xe);

	/* Only log debug information and exit if it is a critical failure */
	if (survivability->boot_status == CRITICAL_FAILURE) {
		log_survivability_info(pdev);
		return -ENXIO;
	}

	return enable_survivability_mode(pdev);
}
