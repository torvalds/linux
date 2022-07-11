/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Rockchip Electronics Co.Ltd
 * Author: Felix Zeng <felix.zeng@rock-chips.com>
 */

#ifndef __LINUX_RKNPU_JOB_H_
#define __LINUX_RKNPU_JOB_H_

#include <linux/spinlock.h>
#include <linux/dma-fence.h>
#include <linux/irq.h>

#include <drm/drm_device.h>

#include "rknpu_ioctl.h"

#define RKNPU_MAX_CORES 3

#define RKNPU_JOB_DONE (1 << 0)
#define RKNPU_JOB_ASYNC (1 << 1)
#define RKNPU_JOB_DETACHED (1 << 2)

#define RKNPU_CORE_AUTO_MASK 0x00
#define RKNPU_CORE0_MASK 0x01
#define RKNPU_CORE1_MASK 0x02
#define RKNPU_CORE2_MASK 0x04

struct rknpu_job {
	struct rknpu_device *rknpu_dev;
	struct list_head head[RKNPU_MAX_CORES];
	struct work_struct cleanup_work;
	bool in_queue[RKNPU_MAX_CORES];
	bool irq_entry[RKNPU_MAX_CORES];
	unsigned int flags;
	int ret;
	struct rknpu_submit *args;
	bool args_owner;
	struct rknpu_task *first_task;
	struct rknpu_task *last_task;
	uint32_t int_mask[RKNPU_MAX_CORES];
	uint32_t int_status[RKNPU_MAX_CORES];
	struct dma_fence *fence;
	ktime_t timestamp;
	uint32_t use_core_num;
	uint32_t run_count;
	uint32_t interrupt_count;
	ktime_t hw_recoder_time;
};

irqreturn_t rknpu_core0_irq_handler(int irq, void *data);
irqreturn_t rknpu_core1_irq_handler(int irq, void *data);
irqreturn_t rknpu_core2_irq_handler(int irq, void *data);

#ifdef CONFIG_ROCKCHIP_RKNPU_DRM_GEM
int rknpu_submit_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv);
#endif
#ifdef CONFIG_ROCKCHIP_RKNPU_DMA_HEAP
int rknpu_submit_ioctl(struct rknpu_device *rknpu_dev, unsigned long data);
#endif

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
