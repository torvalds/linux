/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author: Felix Zeng <felix.zeng@rock-chips.com>
 */

#ifndef __LINUX_RKNPU_DRV_H_
#define __LINUX_RKNPU_DRV_H_

#include <linux/completion.h>
#include <linux/device.h>
#include <linux/kref.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/regulator/consumer.h>
#include <linux/version.h>

#include "rknpu_job.h"
#include "rknpu_fence.h"

#define DRIVER_NAME "rknpu"
#define DRIVER_DESC "RKNPU driver"
#define DRIVER_DATE "20210824"
#define DRIVER_MAJOR 0
#define DRIVER_MINOR 5
#define DRIVER_PATCHLEVEL 0

#define LOG_TAG "RKNPU"

#define LOG_INFO(fmt, args...) pr_info(LOG_TAG ": " fmt, ##args)
#if KERNEL_VERSION(5, 5, 0) <= LINUX_VERSION_CODE
#define LOG_WARN(fmt, args...) pr_warn(LOG_TAG ": " fmt, ##args)
#else
#define LOG_WARN(fmt, args...) pr_warning(LOG_TAG ": " fmt, ##args)
#endif
#define LOG_DEBUG(fmt, args...) DRM_DEBUG_DRIVER(LOG_TAG ": " fmt, ##args)
#define LOG_ERROR(fmt, args...) pr_err(LOG_TAG ": " fmt, ##args)

#define LOG_DEV_INFO(dev, fmt, args...) dev_info(dev, LOG_TAG ": " fmt, ##args)
#define LOG_DEV_WARN(dev, fmt, args...) dev_warn(dev, LOG_TAG ": " fmt, ##args)
#define LOG_DEV_DEBUG(dev, fmt, args...)                                       \
	DRM_DEV_DEBUG_DRIVER(dev, LOG_TAG ": " fmt, ##args)
#define LOG_DEV_ERROR(dev, fmt, args...) dev_err(dev, LOG_TAG ": " fmt, ##args)

struct rknpu_config {
	__u32 bw_priority_addr;
	__u32 bw_priority_length;
	__u64 dma_mask;
};

/**
 * RKNPU device
 *
 * @base: IO mapped base address for device
 * @dev: Device instance
 * @drm_dev: DRM device instance
 */
struct rknpu_device {
	void __iomem *base;
	struct device *dev;
	struct device *fake_dev;
	struct drm_device *drm_dev;
	atomic_t sequence;
	spinlock_t lock;
	spinlock_t irq_lock;
	struct list_head todo_list;
	wait_queue_head_t job_done_wq;
	const struct rknpu_config *config;
	void __iomem *bw_priority_base;
	struct rknpu_job *job;
	struct rknpu_fence_context *fence_ctx;
	bool iommu_en;
	struct reset_control *srst_a;
	struct reset_control *srst_h;
	struct clk_bulk_data *clks;
	int num_clks;
	struct regulator *vdd;
	struct monitor_dev_info *mdev_info;
	struct ipa_power_model_data *model_data;
	struct thermal_cooling_device *devfreq_cooling;
	struct devfreq *devfreq;
	unsigned long current_freq;
	unsigned long current_volt;
	int bypass_irq_handler;
	int bypass_soft_reset;
};

#endif /* __LINUX_RKNPU_DRV_H_ */
