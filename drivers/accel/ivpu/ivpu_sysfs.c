// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024-2026 Intel Corporation
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/pm_runtime.h>
#include <linux/units.h>

#include "ivpu_drv.h"
#include "ivpu_gem.h"
#include "ivpu_fw.h"
#include "ivpu_hw.h"
#include "ivpu_sysfs.h"

/**
 * DOC: npu_busy_time_us
 *
 * npu_busy_time_us is the time that the device spent executing jobs.
 * The time is counted when and only when there are jobs submitted to firmware.
 *
 * This time can be used to measure the utilization of NPU, either by calculating
 * npu_busy_time_us difference between two timepoints (i.e. measuring the time
 * that the NPU was active during some workload) or monitoring utilization percentage
 * by reading npu_busy_time_us periodically.
 *
 * When reading the value periodically, it shouldn't be read too often as it may have
 * an impact on job submission performance. Recommended period is 1 second.
 */
static ssize_t
npu_busy_time_us_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct ivpu_device *vdev = to_ivpu_device(drm);
	ktime_t total, now = 0;

	mutex_lock(&vdev->submitted_jobs_lock);

	total = vdev->busy_time;
	if (!xa_empty(&vdev->submitted_jobs_xa))
		now = ktime_sub(ktime_get(), vdev->busy_start_ts);
	mutex_unlock(&vdev->submitted_jobs_lock);

	return sysfs_emit(buf, "%lld\n", ktime_to_us(ktime_add(total, now)));
}

static DEVICE_ATTR_RO(npu_busy_time_us);

/**
 * DOC: npu_memory_utilization
 *
 * The npu_memory_utilization is used to report in bytes a current NPU memory utilization.
 *
 */
static ssize_t
npu_memory_utilization_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct ivpu_device *vdev = to_ivpu_device(drm);
	struct ivpu_bo *bo;
	u64 total_npu_memory = 0;

	mutex_lock(&vdev->bo_list_lock);
	list_for_each_entry(bo, &vdev->bo_list, bo_list_node)
		if (ivpu_bo_is_resident(bo))
			total_npu_memory += ivpu_bo_size(bo);
	mutex_unlock(&vdev->bo_list_lock);

	return sysfs_emit(buf, "%lld\n", total_npu_memory);
}

static DEVICE_ATTR_RO(npu_memory_utilization);

/**
 * DOC: sched_mode
 *
 * The sched_mode is used to report current NPU scheduling mode.
 *
 * It returns following strings:
 * - "HW"		- Hardware Scheduler mode
 * - "OS"		- Operating System Scheduler mode
 *
 */
static ssize_t sched_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct ivpu_device *vdev = to_ivpu_device(drm);

	return sysfs_emit(buf, "%s\n", vdev->fw->sched_mode ? "HW" : "OS");
}

static DEVICE_ATTR_RO(sched_mode);

/**
 * DOC: NPU frequency control and information
 *
 * Hardware frequency capabilities:
 * freq/hw_min_freq - Minimum frequency supported by the NPU hardware.
 * freq/hw_max_freq - Maximum frequency supported by the NPU hardware.
 * freq/hw_efficient_freq - Most efficient operating frequency for the NPU.
 *
 * Configurable frequency limits (50XX devices and newer):
 * freq/set_min_freq - Configured minimum operating frequency.
 * freq/set_max_freq - Configured maximum operating frequency.
 *
 * Clamping behavior: Values written to set_min_freq and set_max_freq are
 * clamped to hardware limits. If set_min_freq exceeds set_max_freq, the driver
 * clamps set_min_freq to set_max_freq when selecting the operating frequency.
 *
 * Current operating frequency:
 * freq/current_freq - Current frequency in MHz. Valid only when the device is
 * active; returns 0 when idle. May be lower than the requested range due to
 * power or thermal constraints.
 *
 * Legacy attributes (backward compatibility):
 * npu_max_frequency_mhz - Alias for freq/hw_max_freq.
 * npu_current_frequency_mhz - Alias for freq/current_freq.
 */

static ssize_t hw_min_freq_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct ivpu_device *vdev = to_ivpu_device(drm);
	u32 freq_mhz = ivpu_hw_btrs_pll_ratio_to_mhz(vdev, vdev->hw->pll.min_ratio);

	return sysfs_emit(buf, "%u\n", freq_mhz);
}

static DEVICE_ATTR_RO(hw_min_freq);

static ssize_t hw_efficient_freq_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct ivpu_device *vdev = to_ivpu_device(drm);
	u32 freq_mhz = ivpu_hw_btrs_pll_ratio_to_mhz(vdev, vdev->hw->pll.pn_ratio);

	return sysfs_emit(buf, "%u\n", freq_mhz);
}

static DEVICE_ATTR_RO(hw_efficient_freq);

static ssize_t hw_max_freq_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct ivpu_device *vdev = to_ivpu_device(drm);
	u32 freq_mhz = ivpu_hw_btrs_pll_ratio_to_mhz(vdev, vdev->hw->pll.max_ratio);

	return sysfs_emit(buf, "%u\n", freq_mhz);
}

static DEVICE_ATTR_RO(hw_max_freq);

static ssize_t set_min_freq_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct ivpu_device *vdev = to_ivpu_device(drm);
	u32 freq_mhz = ivpu_hw_btrs_pll_ratio_to_mhz(vdev, vdev->hw->pll.cfg_min_ratio);

	return sysfs_emit(buf, "%u\n", freq_mhz);
}

static ssize_t
set_min_freq_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct ivpu_device *vdev = to_ivpu_device(drm);
	u32 freq_mhz;
	int ret;

	ret = kstrtou32(buf, 10, &freq_mhz);
	if (ret)
		return ret;

	ret = ivpu_hw_btrs_cfg_min_freq_set(vdev, freq_mhz);
	if (ret)
		return ret;

	return count;
}

static DEVICE_ATTR_RW(set_min_freq);

static ssize_t set_max_freq_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct ivpu_device *vdev = to_ivpu_device(drm);
	u32 freq_mhz = ivpu_hw_btrs_pll_ratio_to_mhz(vdev, vdev->hw->pll.cfg_max_ratio);

	return sysfs_emit(buf, "%u\n", freq_mhz);
}

static ssize_t
set_max_freq_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct ivpu_device *vdev = to_ivpu_device(drm);
	u32 freq_mhz;
	int ret;

	ret = kstrtou32(buf, 10, &freq_mhz);
	if (ret)
		return ret;

	/* Convert MHz to Hz and set max frequency */
	ret = ivpu_hw_btrs_cfg_max_freq_set(vdev, freq_mhz);
	if (ret)
		return ret;

	return count;
}

static DEVICE_ATTR_RW(set_max_freq);

static ssize_t current_freq_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct ivpu_device *vdev = to_ivpu_device(drm);
	u32 freq_mhz = 0;

	/* Read frequency only if device is active, otherwise frequency is 0 */
	if (pm_runtime_get_if_active(vdev->drm.dev) > 0) {
		freq_mhz = ivpu_hw_btrs_current_freq_get(vdev);

		pm_runtime_put_autosuspend(vdev->drm.dev);
	}

	return sysfs_emit(buf, "%u\n", freq_mhz);
}

static DEVICE_ATTR_RO(current_freq);

/* Alias to current_freq for legacy compat */
static struct device_attribute dev_attr_npu_max_frequency_mhz =
	__ATTR(npu_max_frequency_mhz, 0444, hw_max_freq_show, NULL);

static struct device_attribute dev_attr_npu_current_frequency_mhz =
	__ATTR(npu_current_frequency_mhz, 0444, current_freq_show, NULL);

static struct attribute *ivpu_freq_attrs[] = {
	&dev_attr_hw_min_freq.attr,
	&dev_attr_hw_efficient_freq.attr,
	&dev_attr_hw_max_freq.attr,
	&dev_attr_current_freq.attr,
	NULL,
};

static struct attribute_group ivpu_freq_attr_group = {
	.name = "freq",
	.attrs = ivpu_freq_attrs,
};

static struct attribute *ivpu_dev_attrs[] = {
	&dev_attr_npu_busy_time_us.attr,
	&dev_attr_npu_memory_utilization.attr,
	&dev_attr_sched_mode.attr,
	&dev_attr_npu_max_frequency_mhz.attr,
	&dev_attr_npu_current_frequency_mhz.attr,
	NULL,
};

static struct attribute_group ivpu_dev_attr_group = {
	.attrs = ivpu_dev_attrs,
};

void ivpu_sysfs_init(struct ivpu_device *vdev)
{
	int ret;

	ret = devm_device_add_group(vdev->drm.dev, &ivpu_dev_attr_group);
	if (ret) {
		ivpu_warn(vdev, "Failed to add sysfs group to device, ret %d", ret);
		return;
	}

	ret = devm_device_add_group(vdev->drm.dev, &ivpu_freq_attr_group);
	if (ret) {
		ivpu_warn(vdev, "Failed to add sysfs freq group, ret %d", ret);
		return;
	}

	if (ivpu_hw_ip_gen(vdev) >= IVPU_HW_IP_50XX) {
		ret = sysfs_add_file_to_group(&vdev->drm.dev->kobj,
					      &dev_attr_set_min_freq.attr,
					      "freq");
		if (ret) {
			ivpu_warn(vdev, "Failed to add sysfs set_min_freq to device, ret %d", ret);
			return;
		}

		ret = sysfs_add_file_to_group(&vdev->drm.dev->kobj,
					      &dev_attr_set_max_freq.attr,
					      "freq");
		if (ret) {
			ivpu_warn(vdev, "Failed to add sysfs set_max_freq to device, ret %d", ret);
			return;
		}
	}
}
