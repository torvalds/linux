// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Intel Corporation
 */

#include <linux/device.h>
#include <linux/err.h>

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
		total_npu_memory += bo->base.base.size;
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
static ssize_t
sched_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct ivpu_device *vdev = to_ivpu_device(drm);

	return sysfs_emit(buf, "%s\n", vdev->fw->sched_mode ? "HW" : "OS");
}

static DEVICE_ATTR_RO(sched_mode);

static struct attribute *ivpu_dev_attrs[] = {
	&dev_attr_npu_busy_time_us.attr,
	&dev_attr_npu_memory_utilization.attr,
	&dev_attr_sched_mode.attr,
	NULL,
};

static struct attribute_group ivpu_dev_attr_group = {
	.attrs = ivpu_dev_attrs,
};

void ivpu_sysfs_init(struct ivpu_device *vdev)
{
	int ret;

	ret = devm_device_add_group(vdev->drm.dev, &ivpu_dev_attr_group);
	if (ret)
		ivpu_warn(vdev, "Failed to add group to device, ret %d", ret);
}
