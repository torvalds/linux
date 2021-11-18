/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 *
 * Author: Huang Lee <Putin.li@rock-chips.com>
 */

#ifndef __LINUX_RGA_FENCE_H_
#define __LINUX_RGA_FENCE_H_

#include "rga_drv.h"

struct rga_fence_context *rga_fence_context_alloc(void);

void rga_fence_context_free(struct rga_fence_context *fence_ctx);

int rga_out_fence_alloc(struct rga_job *job);

int rga_out_fence_get_fd(struct rga_job *job);

struct dma_fence *rga_get_input_fence(int in_fence_fd);

int rga_wait_input_fence(struct dma_fence *in_fence);

int rga_add_dma_fence_callback(struct rga_job *job,
	struct dma_fence *in_fence, dma_fence_func_t func);

#endif /* __LINUX_RGA_FENCE_H_ */
