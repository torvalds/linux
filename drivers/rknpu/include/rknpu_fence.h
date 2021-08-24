/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author: Felix Zeng <felix.zeng@rock-chips.com>
 */

#ifndef __LINUX_RKNPU_FENCE_H_
#define __LINUX_RKNPU_FENCE_H_

#include "rknpu_job.h"

struct rknpu_fence_context {
	unsigned int context;
	unsigned int seqno;
	spinlock_t spinlock;
};

struct rknpu_fence_context *rknpu_fence_context_alloc(void);

void rknpu_fence_context_free(struct rknpu_fence_context *fence_ctx);

int rknpu_fence_alloc(struct rknpu_job *job);

int rknpu_fence_get_fd(struct rknpu_job *job);

#endif /* __LINUX_RKNPU_FENCE_H_ */
