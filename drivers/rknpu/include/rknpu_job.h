/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author: Felix Zeng <felix.zeng@rock-chips.com>
 */

#ifndef __LINUX_RKNPU_JOB_H_
#define __LINUX_RKNPU_JOB_H_

#include <linux/spinlock.h>
#include <linux/dma-fence.h>

#include <drm/drm_device.h>

#include "rknpu_ioctl.h"

#define RKNPU_JOB_DONE (1 << 0)
#define RKNPU_JOB_ASYNC (1 << 1)

struct rknpu_job {
	struct rknpu_device *rknpu_dev;
	struct list_head head;
	struct work_struct cleanup_work;
	unsigned int flags;
	int ret;
	struct rknpu_submit *args;
	bool args_owner;
	struct rknpu_task *first_task;
	struct rknpu_task *last_task;
	uint32_t int_mask;
	uint32_t int_status;
	struct dma_fence *fence;
	spinlock_t fence_lock;
	ktime_t timestamp;
};

irqreturn_t rknpu_irq_handler(int irq, void *data);

int rknpu_submit_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv);

int rknpu_get_hw_version(struct rknpu_device *rknpu_dev, uint32_t *version);

int rknpu_get_bw_priority(struct rknpu_device *rknpu_dev, uint32_t *priority,
			  uint32_t *expect, uint32_t *tw);

int rknpu_set_bw_priority(struct rknpu_device *rknpu_dev, uint32_t priority,
			  uint32_t expect, uint32_t tw);

int rknpu_clear_rw_amount(struct rknpu_device *rknpu_dev);

int rknpu_get_rw_amount(struct rknpu_device *rknpu_dev, uint32_t *dt_wr,
			uint32_t *dt_rd, uint32_t *wd_rd);

int rknpu_get_total_rw_amount(struct rknpu_device *rknpu_dev, uint32_t *amount);

#endif /* __LINUX_RKNPU_JOB_H_ */
