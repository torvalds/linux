// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <linux/device.h>
#include <linux/kobject.h>
#include <linux/pci.h>
#include <linux/sysfs.h>

#include "xe_device.h"
#include "xe_device_sysfs.h"
#include "xe_mmio.h"
#include "xe_pcode_api.h"
#include "xe_pcode.h"
#include "xe_pm.h"

/**
 * DOC: Xe device sysfs
 * Xe driver requires exposing certain tunable knobs controlled by user space for
 * each graphics device. Considering this, we need to add sysfs attributes at device
 * level granularity.
 * These sysfs attributes will be available under pci device kobj directory.
 *
 * vram_d3cold_threshold - Report/change vram used threshold(in MB) below
 * which vram save/restore is permissible during runtime D3cold entry/exit.
 *
 * lb_fan_control_version - Fan control version provisioned by late binding.
 * Exposed only if supported by the device.
 *
 * lb_voltage_regulator_version - Voltage regulator version provisioned by late
 * binding. Exposed only if supported by the device.
 */

static ssize_t
vram_d3cold_threshold_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct xe_device *xe = pdev_to_xe_device(pdev);
	int ret;

	xe_pm_runtime_get(xe);
	ret = sysfs_emit(buf, "%d\n", xe->d3cold.vram_threshold);
	xe_pm_runtime_put(xe);

	return ret;
}

static ssize_t
vram_d3cold_threshold_store(struct device *dev, struct device_attribute *attr,
			    const char *buff, size_t count)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct xe_device *xe = pdev_to_xe_device(pdev);
	u32 vram_d3cold_threshold;
	int ret;

	ret = kstrtou32(buff, 0, &vram_d3cold_threshold);
	if (ret)
		return ret;

	drm_dbg(&xe->drm, "vram_d3cold_threshold: %u\n", vram_d3cold_threshold);

	xe_pm_runtime_get(xe);
	ret = xe_pm_set_vram_threshold(xe, vram_d3cold_threshold);
	xe_pm_runtime_put(xe);

	return ret ?: count;
}

static DEVICE_ATTR_RW(vram_d3cold_threshold);

static ssize_t
lb_fan_control_version_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct xe_device *xe = pdev_to_xe_device(to_pci_dev(dev));
	struct xe_tile *root = xe_device_get_root_tile(xe);
	u32 cap, ver_low = FAN_TABLE, ver_high = FAN_TABLE;
	u16 major = 0, minor = 0, hotfix = 0, build = 0;
	int ret;

	xe_pm_runtime_get(xe);

	ret = xe_pcode_read(root, PCODE_MBOX(PCODE_LATE_BINDING, GET_CAPABILITY_STATUS, 0),
			    &cap, NULL);
	if (ret)
		goto out;

	if (REG_FIELD_GET(V1_FAN_PROVISIONED, cap)) {
		ret = xe_pcode_read(root, PCODE_MBOX(PCODE_LATE_BINDING, GET_VERSION_LOW, 0),
				    &ver_low, NULL);
		if (ret)
			goto out;

		ret = xe_pcode_read(root, PCODE_MBOX(PCODE_LATE_BINDING, GET_VERSION_HIGH, 0),
				    &ver_high, NULL);
		if (ret)
			goto out;

		major = REG_FIELD_GET(MAJOR_VERSION_MASK, ver_low);
		minor = REG_FIELD_GET(MINOR_VERSION_MASK, ver_low);
		hotfix = REG_FIELD_GET(HOTFIX_VERSION_MASK, ver_high);
		build = REG_FIELD_GET(BUILD_VERSION_MASK, ver_high);
	}
out:
	xe_pm_runtime_put(xe);

	return ret ?: sysfs_emit(buf, "%u.%u.%u.%u\n", major, minor, hotfix, build);
}
static DEVICE_ATTR_ADMIN_RO(lb_fan_control_version);

static ssize_t
lb_voltage_regulator_version_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct xe_device *xe = pdev_to_xe_device(to_pci_dev(dev));
	struct xe_tile *root = xe_device_get_root_tile(xe);
	u32 cap, ver_low = VR_CONFIG, ver_high = VR_CONFIG;
	u16 major = 0, minor = 0, hotfix = 0, build = 0;
	int ret;

	xe_pm_runtime_get(xe);

	ret = xe_pcode_read(root, PCODE_MBOX(PCODE_LATE_BINDING, GET_CAPABILITY_STATUS, 0),
			    &cap, NULL);
	if (ret)
		goto out;

	if (REG_FIELD_GET(VR_PARAMS_PROVISIONED, cap)) {
		ret = xe_pcode_read(root, PCODE_MBOX(PCODE_LATE_BINDING, GET_VERSION_LOW, 0),
				    &ver_low, NULL);
		if (ret)
			goto out;

		ret = xe_pcode_read(root, PCODE_MBOX(PCODE_LATE_BINDING, GET_VERSION_HIGH, 0),
				    &ver_high, NULL);
		if (ret)
			goto out;

		major = REG_FIELD_GET(MAJOR_VERSION_MASK, ver_low);
		minor = REG_FIELD_GET(MINOR_VERSION_MASK, ver_low);
		hotfix = REG_FIELD_GET(HOTFIX_VERSION_MASK, ver_high);
		build = REG_FIELD_GET(BUILD_VERSION_MASK, ver_high);
	}
out:
	xe_pm_runtime_put(xe);

	return ret ?: sysfs_emit(buf, "%u.%u.%u.%u\n", major, minor, hotfix, build);
}
static DEVICE_ATTR_ADMIN_RO(lb_voltage_regulator_version);

static int late_bind_create_files(struct device *dev)
{
	struct xe_device *xe = pdev_to_xe_device(to_pci_dev(dev));
	struct xe_tile *root = xe_device_get_root_tile(xe);
	u32 cap;
	int ret;

	xe_pm_runtime_get(xe);

	ret = xe_pcode_read(root, PCODE_MBOX(PCODE_LATE_BINDING, GET_CAPABILITY_STATUS, 0),
			    &cap, NULL);
	if (ret) {
		if (ret == -ENXIO) {
			drm_dbg(&xe->drm, "Late binding not supported by firmware\n");
			ret = 0;
		}
		goto out;
	}

	if (REG_FIELD_GET(V1_FAN_SUPPORTED, cap)) {
		ret = sysfs_create_file(&dev->kobj, &dev_attr_lb_fan_control_version.attr);
		if (ret)
			goto out;
	}

	if (REG_FIELD_GET(VR_PARAMS_SUPPORTED, cap))
		ret = sysfs_create_file(&dev->kobj, &dev_attr_lb_voltage_regulator_version.attr);
out:
	xe_pm_runtime_put(xe);

	return ret;
}

static void late_bind_remove_files(struct device *dev)
{
	struct xe_device *xe = pdev_to_xe_device(to_pci_dev(dev));
	struct xe_tile *root = xe_device_get_root_tile(xe);
	u32 cap;
	int ret;

	xe_pm_runtime_get(xe);

	ret = xe_pcode_read(root, PCODE_MBOX(PCODE_LATE_BINDING, GET_CAPABILITY_STATUS, 0),
			    &cap, NULL);
	if (ret)
		goto out;

	if (REG_FIELD_GET(V1_FAN_SUPPORTED, cap))
		sysfs_remove_file(&dev->kobj, &dev_attr_lb_fan_control_version.attr);

	if (REG_FIELD_GET(VR_PARAMS_SUPPORTED, cap))
		sysfs_remove_file(&dev->kobj, &dev_attr_lb_voltage_regulator_version.attr);
out:
	xe_pm_runtime_put(xe);
}

/**
 * DOC: PCIe Gen5 Limitations
 *
 * Default link speed of discrete GPUs is determined by configuration parameters
 * stored in their flash memory, which are subject to override through user
 * initiated firmware updates. It has been observed that devices configured with
 * PCIe Gen5 as their default link speed can come across link quality issues due
 * to host or motherboard limitations and may have to auto-downgrade their link
 * to PCIe Gen4 speed when faced with unstable link at Gen5, which makes
 * firmware updates rather risky on such setups. It is required to ensure that
 * the device is capable of auto-downgrading its link to PCIe Gen4 speed before
 * pushing the firmware image with PCIe Gen5 as default configuration. This can
 * be done by reading ``auto_link_downgrade_capable`` sysfs entry, which will
 * denote if the device is capable of auto-downgrading its link to PCIe Gen4
 * speed with boolean output value of ``0`` or ``1``, meaning `incapable` or
 * `capable` respectively.
 *
 * .. code-block:: shell
 *
 *    $ cat /sys/bus/pci/devices/<bdf>/auto_link_downgrade_capable
 *
 * Pushing the firmware image with PCIe Gen5 as default configuration on a auto
 * link downgrade incapable device and facing link instability due to host or
 * motherboard limitations can result in driver failing to bind to the device,
 * making further firmware updates impossible with RMA being the only last
 * resort.
 *
 * Link downgrade status of auto link downgrade capable devices is available
 * through ``auto_link_downgrade_status`` sysfs entry with boolean output value
 * of ``0`` or ``1``, where ``0`` means no auto-downgrading was required during
 * link training (which is the optimal scenario) and ``1`` means the device has
 * auto-downgraded its link to PCIe Gen4 speed due to unstable Gen5 link.
 *
 * .. code-block:: shell
 *
 *    $ cat /sys/bus/pci/devices/<bdf>/auto_link_downgrade_status
 */

static ssize_t
auto_link_downgrade_capable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct xe_device *xe = pdev_to_xe_device(pdev);
	u32 cap, val;

	xe_pm_runtime_get(xe);
	val = xe_mmio_read32(xe_root_tile_mmio(xe), BMG_PCIE_CAP);
	xe_pm_runtime_put(xe);

	cap = REG_FIELD_GET(LINK_DOWNGRADE, val);
	return sysfs_emit(buf, "%u\n", cap == DOWNGRADE_CAPABLE);
}
static DEVICE_ATTR_ADMIN_RO(auto_link_downgrade_capable);

static ssize_t
auto_link_downgrade_status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct xe_device *xe = pdev_to_xe_device(pdev);
	/* default the auto_link_downgrade status to 0 */
	u32 val = 0;
	int ret;

	xe_pm_runtime_get(xe);
	ret = xe_pcode_read(xe_device_get_root_tile(xe),
			    PCODE_MBOX(DGFX_PCODE_STATUS, DGFX_GET_INIT_STATUS, 0),
			    &val, NULL);
	xe_pm_runtime_put(xe);

	return ret ?: sysfs_emit(buf, "%u\n", REG_FIELD_GET(DGFX_LINK_DOWNGRADE_STATUS, val));
}
static DEVICE_ATTR_ADMIN_RO(auto_link_downgrade_status);

static const struct attribute *auto_link_downgrade_attrs[] = {
	&dev_attr_auto_link_downgrade_capable.attr,
	&dev_attr_auto_link_downgrade_status.attr,
	NULL
};

static void xe_device_sysfs_fini(void *arg)
{
	struct xe_device *xe = arg;

	if (xe->d3cold.capable)
		sysfs_remove_file(&xe->drm.dev->kobj, &dev_attr_vram_d3cold_threshold.attr);

	if (xe->info.platform == XE_BATTLEMAGE) {
		sysfs_remove_files(&xe->drm.dev->kobj, auto_link_downgrade_attrs);
		late_bind_remove_files(xe->drm.dev);
	}
}

int xe_device_sysfs_init(struct xe_device *xe)
{
	struct device *dev = xe->drm.dev;
	int ret;

	if (xe->d3cold.capable) {
		ret = sysfs_create_file(&dev->kobj, &dev_attr_vram_d3cold_threshold.attr);
		if (ret)
			return ret;
	}

	if (xe->info.platform == XE_BATTLEMAGE && !IS_SRIOV_VF(xe)) {
		ret = sysfs_create_files(&dev->kobj, auto_link_downgrade_attrs);
		if (ret)
			goto cleanup;

		ret = late_bind_create_files(dev);
		if (ret)
			goto cleanup;
	}

	return devm_add_action_or_reset(dev, xe_device_sysfs_fini, xe);

cleanup:
	xe_device_sysfs_fini(xe);
	return ret;
}
