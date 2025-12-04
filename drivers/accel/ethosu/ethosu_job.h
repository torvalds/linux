/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Copyright 2024-2025 Tomeu Vizoso <tomeu@tomeuvizoso.net> */
/* Copyright 2025 Arm, Ltd. */

#ifndef __ETHOSU_JOB_H__
#define __ETHOSU_JOB_H__

#include <linux/kref.h>
#include <drm/gpu_scheduler.h>

struct ethosu_device;
struct ethosu_file_priv;

struct ethosu_job {
	struct drm_sched_job base;
	struct ethosu_device *dev;

	struct drm_gem_object *cmd_bo;
	struct drm_gem_object *region_bo[NPU_BASEP_REGION_MAX];
	u8 region_bo_num[NPU_BASEP_REGION_MAX];
	u8 region_cnt;
	u32 sram_size;

	/* Fence to be signaled by drm-sched once its done with the job */
	struct dma_fence *inference_done_fence;

	/* Fence to be signaled by IRQ handler when the job is complete. */
	struct dma_fence *done_fence;

	struct kref refcount;
};

int ethosu_ioctl_submit(struct drm_device *dev, void *data, struct drm_file *file);

int ethosu_job_init(struct ethosu_device *dev);
void ethosu_job_fini(struct ethosu_device *dev);
int ethosu_job_open(struct ethosu_file_priv *ethosu_priv);
void ethosu_job_close(struct ethosu_file_priv *ethosu_priv);

#endif
