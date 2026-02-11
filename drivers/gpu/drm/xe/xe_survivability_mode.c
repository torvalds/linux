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
#include "xe_heci_gsc.h"
#include "xe_i2c.h"
#include "xe_mmio.h"
#include "xe_nvm.h"
#include "xe_pcode_api.h"
#include "xe_vsec.h"

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
 * Survivability mode is indicated by the below admin-only readable sysfs entry. It
 * provides information about the type of survivability mode (Boot/Runtime).
 *
 * .. code-block:: shell
 *
 *	# cat /sys/bus/pci/devices/<device>/survivability_mode
 *	  Boot
 *
 *
 * Any additional debug information if present will be visible under the directory
 * ``survivability_info``::
 *
 *	/sys/bus/pci/devices/<device>/survivability_info/
 *	├── aux_info0
 *	├── aux_info1
 *	├── aux_info2
 *	├── aux_info3
 *	├── aux_info4
 *	├── capability_info
 *	├── fdo_mode
 *	├── postcode_trace
 *	└── postcode_trace_overflow
 *
 * This directory has the following attributes
 *
 * - ``capability_info`` : Indicates Boot status and support for additional information
 *
 * - ``postcode_trace``, ``postcode_trace_overflow`` : Each postcode is a 8bit value and
 *   represents a boot failure event. When a new failure event is logged by PCODE the
 *   existing postcodes are shifted left. These entries provide a history of 8 postcodes.
 *
 * - ``aux_info<n>`` : Some failures have additional debug information
 *
 * - ``fdo_mode`` : To allow recovery in scenarios where MEI itself fails, a new SPI Flash
 *   Descriptor Override (FDO) mode is added in v2 survivability breadcrumbs. This mode is enabled
 *   by PCODE and provides the ability to directly update the firmware via SPI Driver without
 *   any dependency on MEI. Xe KMD initializes the nvm aux driver if FDO mode is enabled.
 *
 * Runtime Survivability
 * =====================
 *
 * Certain runtime firmware errors can cause the device to enter a wedged state
 * (:ref:`xe-device-wedging`) requiring a firmware flash to restore normal operation.
 * Runtime Survivability Mode indicates that a firmware flash is necessary to recover the device and
 * is indicated by the presence of survivability mode sysfs.
 * Survivability mode sysfs provides information about the type of survivability mode.
 *
 * .. code-block:: shell
 *
 *	# cat /sys/bus/pci/devices/<device>/survivability_mode
 *	  Runtime
 *
 * When such errors occur, userspace is notified with the drm device wedged uevent and runtime
 * survivability mode. User can then initiate a firmware flash using userspace tools like fwupd
 * to restore device to normal operation.
 */

static const char * const reg_map[] = {
	[CAPABILITY_INFO]         = "Capability Info",
	[POSTCODE_TRACE]          = "Postcode trace",
	[POSTCODE_TRACE_OVERFLOW] = "Postcode trace overflow",
	[AUX_INFO0]               = "Auxiliary Info 0",
	[AUX_INFO1]               = "Auxiliary Info 1",
	[AUX_INFO2]               = "Auxiliary Info 2",
	[AUX_INFO3]               = "Auxiliary Info 3",
	[AUX_INFO4]               = "Auxiliary Info 4",
};

#define FDO_INFO	(MAX_SCRATCH_REG + 1)

struct xe_survivability_attribute {
	struct device_attribute attr;
	u8 index;
};

static struct
xe_survivability_attribute *dev_attr_to_survivability_attr(struct device_attribute *attr)
{
	return container_of(attr, struct xe_survivability_attribute, attr);
}

static void set_survivability_info(struct xe_mmio *mmio, u32  *info, int id)
{
	info[id] = xe_mmio_read32(mmio, PCODE_SCRATCH(id));
}

static void populate_survivability_info(struct xe_device *xe)
{
	struct xe_survivability *survivability = &xe->survivability;
	u32 *info = survivability->info;
	struct xe_mmio *mmio;
	u32 id = 0, reg_value;

	mmio = xe_root_tile_mmio(xe);
	set_survivability_info(mmio, info, CAPABILITY_INFO);
	reg_value = info[CAPABILITY_INFO];

	survivability->version = REG_FIELD_GET(BREADCRUMB_VERSION, reg_value);
	/* FDO mode is exposed only from version 2 */
	if (survivability->version >= 2)
		survivability->fdo_mode = REG_FIELD_GET(FDO_MODE, reg_value);

	if (reg_value & HISTORY_TRACKING) {
		set_survivability_info(mmio, info, POSTCODE_TRACE);

		if (reg_value & OVERFLOW_SUPPORT)
			set_survivability_info(mmio, info, POSTCODE_TRACE_OVERFLOW);
	}

	/* Traverse the linked list of aux info registers */
	if (reg_value & AUXINFO_SUPPORT) {
		for (id = REG_FIELD_GET(AUXINFO_REG_OFFSET, reg_value);
		     id >= AUX_INFO0 && id < MAX_SCRATCH_REG;
		     id =  REG_FIELD_GET(AUXINFO_HISTORY_OFFSET, info[id]))
			set_survivability_info(mmio, info, id);
	}
}

static void log_survivability_info(struct pci_dev *pdev)
{
	struct xe_device *xe = pdev_to_xe_device(pdev);
	struct xe_survivability *survivability = &xe->survivability;
	u32 *info = survivability->info;
	int id;

	dev_info(&pdev->dev, "Survivability Boot Status : Critical Failure (%d)\n",
		 survivability->boot_status);
	for (id = 0; id < MAX_SCRATCH_REG; id++) {
		if (info[id])
			dev_info(&pdev->dev, "%s: 0x%x\n", reg_map[id], info[id]);
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

	return sysfs_emit(buff, "%s\n", survivability->type ? "Runtime" : "Boot");
}

static DEVICE_ATTR_ADMIN_RO(survivability_mode);

static ssize_t survivability_info_show(struct device *dev,
				       struct device_attribute *attr, char *buff)
{
	struct xe_survivability_attribute *sa = dev_attr_to_survivability_attr(attr);
	struct pci_dev *pdev = to_pci_dev(dev);
	struct xe_device *xe = pdev_to_xe_device(pdev);
	struct xe_survivability *survivability = &xe->survivability;
	u32 *info = survivability->info;

	if (sa->index == FDO_INFO)
		return sysfs_emit(buff, "%s\n", str_enabled_disabled(survivability->fdo_mode));

	return sysfs_emit(buff, "0x%x\n", info[sa->index]);
}

#define SURVIVABILITY_ATTR_RO(name, _index)					\
	struct xe_survivability_attribute attr_##name =	{			\
		.attr =  __ATTR(name, 0400, survivability_info_show, NULL),	\
		.index = _index,						\
	}

static SURVIVABILITY_ATTR_RO(capability_info, CAPABILITY_INFO);
static SURVIVABILITY_ATTR_RO(postcode_trace, POSTCODE_TRACE);
static SURVIVABILITY_ATTR_RO(postcode_trace_overflow, POSTCODE_TRACE_OVERFLOW);
static SURVIVABILITY_ATTR_RO(aux_info0, AUX_INFO0);
static SURVIVABILITY_ATTR_RO(aux_info1, AUX_INFO1);
static SURVIVABILITY_ATTR_RO(aux_info2, AUX_INFO2);
static SURVIVABILITY_ATTR_RO(aux_info3, AUX_INFO3);
static SURVIVABILITY_ATTR_RO(aux_info4, AUX_INFO4);
static SURVIVABILITY_ATTR_RO(fdo_mode, FDO_INFO);

static void xe_survivability_mode_fini(void *arg)
{
	struct xe_device *xe = arg;
	struct pci_dev *pdev = to_pci_dev(xe->drm.dev);
	struct device *dev = &pdev->dev;

	device_remove_file(dev, &dev_attr_survivability_mode);
}

static umode_t survivability_info_attrs_visible(struct kobject *kobj, struct attribute *attr,
						int idx)
{
	struct xe_device *xe = kdev_to_xe_device(kobj_to_dev(kobj));
	struct xe_survivability *survivability = &xe->survivability;
	u32 *info = survivability->info;

	/*
	 * Last index in survivability_info_attrs is fdo mode and is applicable only in
	 * version 2 of survivability mode
	 */
	if (idx == MAX_SCRATCH_REG && survivability->version >= 2)
		return 0400;

	if (idx < MAX_SCRATCH_REG && info[idx])
		return 0400;

	return 0;
}

/* Attributes are ordered according to enum scratch_reg */
static struct attribute *survivability_info_attrs[] = {
	&attr_capability_info.attr.attr,
	&attr_postcode_trace.attr.attr,
	&attr_postcode_trace_overflow.attr.attr,
	&attr_aux_info0.attr.attr,
	&attr_aux_info1.attr.attr,
	&attr_aux_info2.attr.attr,
	&attr_aux_info3.attr.attr,
	&attr_aux_info4.attr.attr,
	&attr_fdo_mode.attr.attr,
	NULL,
};

static const struct attribute_group survivability_info_group = {
	.name = "survivability_info",
	.attrs = survivability_info_attrs,
	.is_visible = survivability_info_attrs_visible,
};

static int create_survivability_sysfs(struct pci_dev *pdev)
{
	struct device *dev = &pdev->dev;
	struct xe_device *xe = pdev_to_xe_device(pdev);
	int ret;

	ret = device_create_file(dev, &dev_attr_survivability_mode);
	if (ret) {
		dev_warn(dev, "Failed to create survivability sysfs files\n");
		return ret;
	}

	ret = devm_add_action_or_reset(xe->drm.dev,
				       xe_survivability_mode_fini, xe);
	if (ret)
		return ret;

	if (check_boot_failure(xe)) {
		ret = devm_device_add_group(dev, &survivability_info_group);
		if (ret)
			return ret;
	}

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

	/* Make sure xe_heci_gsc_init() and xe_i2c_probe() are aware of survivability */
	survivability->mode = true;

	xe_heci_gsc_init(xe);

	xe_vsec_init(xe);

	if (survivability->fdo_mode) {
		ret = xe_nvm_init(xe);
		if (ret)
			goto err;
	}

	ret = xe_i2c_probe(xe);
	if (ret)
		goto err;

	dev_err(dev, "In Survivability Mode\n");

	return 0;

err:
	dev_err(dev, "Failed to enable Survivability Mode\n");
	survivability->mode = false;
	return ret;
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

	populate_survivability_info(xe);

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

	if (!xe_survivability_mode_is_requested(xe))
		return 0;

	populate_survivability_info(xe);

	/*
	 * v2 supports survivability mode for critical errors
	 */
	if (survivability->version < 2  && survivability->boot_status == CRITICAL_FAILURE) {
		log_survivability_info(pdev);
		return -ENXIO;
	}

	survivability->type = XE_SURVIVABILITY_TYPE_BOOT;

	return enable_boot_survivability_mode(pdev);
}
