/*
 * drivers/video/tegra/host/nvhost_intr.h
 *
 * Tegra Graphics Host Interrupt Management
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __NVHOST_INTR_H
#define __NVHOST_INTR_H

#include <linux/kthread.h>
#include <linux/semaphore.h>

#include "nvhost_hardware.h"

struct nvhost_channel;

enum nvhost_intr_action {
	/**
	 * Perform cleanup after a submit has completed.
	 * 'data' points to a channel
	 */
	NVHOST_INTR_ACTION_SUBMIT_COMPLETE = 0,

	/**
	 * Save a HW context.
	 * 'data' points to a context
	 */
	NVHOST_INTR_ACTION_CTXSAVE,

	/**
	 * Wake up a  task.
	 * 'data' points to a wait_queue_head_t
	 */
	NVHOST_INTR_ACTION_WAKEUP,

	/**
	 * Wake up a interruptible task.
	 * 'data' points to a wait_queue_head_t
	 */
	NVHOST_INTR_ACTION_WAKEUP_INTERRUPTIBLE,

	NVHOST_INTR_ACTION_COUNT
};

struct nvhost_intr_syncpt {
	u8 id;
	u8 irq_requested;
	u16 irq;
	spinlock_t lock;
	struct list_head wait_head;
	char thresh_irq_name[12];
};

struct nvhost_intr {
	struct nvhost_intr_syncpt syncpt[NV_HOST1X_SYNCPT_NB_PTS];
	int host1x_irq;
	bool host1x_isr_started;
};

/**
 * Schedule an action to be taken when a sync point reaches the given threshold.
 *
 * @id the sync point
 * @thresh the threshold
 * @action the action to take
 * @data a pointer to extra data depending on action, see above
 * @ref must be passed if cancellation is possible, else NULL
 *
 * This is a non-blocking api.
 */
int nvhost_intr_add_action(struct nvhost_intr *intr, u32 id, u32 thresh,
			enum nvhost_intr_action action, void *data,
			void **ref);

/**
 * Unreference an action submitted to nvhost_intr_add_action().
 * You must call this if you passed non-NULL as ref.
 * @ref the ref returned from nvhost_intr_add_action()
 */
void nvhost_intr_put_ref(struct nvhost_intr *intr, void *ref);

int nvhost_intr_init(struct nvhost_intr *intr, u32 irq_gen, u32 irq_sync);
void nvhost_intr_deinit(struct nvhost_intr *intr);
void nvhost_intr_configure(struct nvhost_intr *intr, u32 hz);

#endif
