/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 */

#ifndef MSM_DISP_SNAPSHOT_H_
#define MSM_DISP_SNAPSHOT_H_

#include <drm/drm_atomic_helper.h>
#include <drm/drm_device.h>
#include "../../../drm_crtc_internal.h"
#include <drm/drm_print.h>
#include <drm/drm_atomic.h>
#include <linux/debugfs.h>
#include <linux/list.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/ktime.h>
#include <linux/uaccess.h>
#include <linux/dma-buf.h>
#include <linux/slab.h>
#include <linux/list_sort.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/kthread.h>
#include <linux/devcoredump.h>
#include "msm_kms.h"

#define MSM_DISP_SNAPSHOT_MAX_BLKS		10

/* debug option to print the registers in logs */
#define MSM_DISP_SNAPSHOT_DUMP_IN_CONSOLE 0

/* print debug ranges in groups of 4 u32s */
#define REG_DUMP_ALIGN		16

/**
 * struct msm_disp_state - structure to store current dpu state
 * @dev: device pointer
 * @drm_dev: drm device pointer
 * @atomic_state: atomic state duplicated at the time of the error
 * @time: timestamp at which the coredump was captured
 */
struct msm_disp_state {
	struct device *dev;
	struct drm_device *drm_dev;

	struct list_head blocks;

	struct drm_atomic_state *atomic_state;

	struct timespec64 time;
};

/**
 * struct msm_disp_state_block - structure to store each hardware block state
 * @name: name of the block
 * @drm_dev: handle to the linked list head
 * @size: size of the register space of this hardware block
 * @state: array holding the register dump of this hardware block
 * @base_addr: starting address of this hardware block's register space
 */
struct msm_disp_state_block {
	char name[SZ_128];
	struct list_head node;
	unsigned int size;
	u32 *state;
	void __iomem *base_addr;
};

/**
 * msm_disp_snapshot_init - initialize display snapshot
 * @drm_dev:	drm device handle
 *
 * Returns:		0 or -ERROR
 */
int msm_disp_snapshot_init(struct drm_device *drm_dev);

/**
 * msm_disp_snapshot_destroy - destroy the display snapshot
 * @drm_dev:    drm device handle
 *
 * Returns:	none
 */
void msm_disp_snapshot_destroy(struct drm_device *drm_dev);

/**
 * msm_disp_snapshot_state_sync - synchronously snapshot display state
 * @kms:  the kms object
 *
 * Returns state or error
 *
 * Must be called with &kms->dump_mutex held
 */
struct msm_disp_state *msm_disp_snapshot_state_sync(struct msm_kms *kms);

/**
 * msm_disp_snapshot_state - trigger to dump the display snapshot
 * @drm_dev:	handle to drm device

 * Returns:	none
 */
void msm_disp_snapshot_state(struct drm_device *drm_dev);

/**
 * msm_disp_state_print - print out the current dpu state
 * @disp_state:	    handle to drm device
 * @p:	    handle to drm printer
 *
 * Returns:	none
 */
void msm_disp_state_print(struct msm_disp_state *disp_state, struct drm_printer *p);

/**
 * msm_disp_snapshot_capture_state - utility to capture atomic state and hw registers
 * @disp_state:	    handle to msm_disp_state struct

 * Returns:	none
 */
void msm_disp_snapshot_capture_state(struct msm_disp_state *disp_state);

/**
 * msm_disp_state_free - free the memory after the coredump has been read
 * @data:	    handle to struct msm_disp_state

 * Returns: none
 */
void msm_disp_state_free(void *data);

/**
 * msm_disp_snapshot_add_block - add a hardware block with its register dump
 * @disp_state:	    handle to struct msm_disp_state
 * @name:           name of the hardware block
 * @len:            size of the register space of the hardware block
 * @base_addr:      starting address of the register space of the hardware block
 * @fmt:            format in which the block names need to be printed
 *
 * Returns: none
 */
__printf(4, 5)
void msm_disp_snapshot_add_block(struct msm_disp_state *disp_state, u32 len,
		void __iomem *base_addr, const char *fmt, ...);

#endif /* MSM_DISP_SNAPSHOT_H_ */
