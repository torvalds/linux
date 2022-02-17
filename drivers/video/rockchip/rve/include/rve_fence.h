/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 *
 * Author: Huang Lee <Putin.li@rock-chips.com>
 */

#ifndef __LINUX_RVE_FENCE_H_
#define __LINUX_RVE_FENCE_H_

#ifdef CONFIG_SYNC_FILE

#include "rve_drv.h"

struct rve_fence_context *rve_fence_context_alloc(void);

void rve_fence_context_free(struct rve_fence_context *fence_ctx);

int rve_out_fence_alloc(struct rve_job *job);

int rve_out_fence_get_fd(struct rve_job *job);

struct dma_fence *rve_get_input_fence(int in_fence_fd);

int rve_wait_input_fence(struct dma_fence *in_fence);

int rve_add_dma_fence_callback(struct rve_job *job,
	struct dma_fence *in_fence, dma_fence_func_t func);

#endif

#endif /* __LINUX_RVE_FENCE_H_ */
