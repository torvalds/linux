/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 *
 * Author: Huang Lee <Putin.li@rock-chips.com>
 */

#ifndef __LINUX_RGA_FENCE_H_
#define __LINUX_RGA_FENCE_H_

struct rga_fence_context {
	unsigned int context;
	unsigned int seqno;
	spinlock_t spinlock;
};

struct rga_fence_waiter {
	/* Base sync driver waiter structure */
	struct dma_fence_cb waiter;

	void *private;
};

#ifdef CONFIG_ROCKCHIP_RGA_ASYNC
int rga_fence_context_init(struct rga_fence_context **ctx);
void rga_fence_context_remove(struct rga_fence_context **ctx);

struct dma_fence *rga_dma_fence_alloc(void);
int rga_dma_fence_get_fd(struct dma_fence *fence);
struct dma_fence *rga_get_dma_fence_from_fd(int fence_fd);
int rga_dma_fence_wait(struct dma_fence *fence);
int rga_dma_fence_add_callback(struct dma_fence *fence, dma_fence_func_t func, void *private);


static inline void rga_dma_fence_put(struct dma_fence *fence)
{
	if (fence)
		dma_fence_put(fence);
}

static inline void rga_dma_fence_signal(struct dma_fence *fence, int error)
{
	if (fence) {
		if (error != 0)
			dma_fence_set_error(fence, error);
		dma_fence_signal(fence);
	}
}

static inline int rga_dma_fence_get_status(struct dma_fence *fence)
{
	if (fence)
		return dma_fence_get_status(fence);
	else
		return 1;
}

#else
static inline struct dma_fence *rga_dma_fence_alloc(void)
{
	return NULL;
}

static inline int rga_dma_fence_get_fd(struct dma_fence *fence)
{
	return 0;
}

static inline struct dma_fence *rga_get_dma_fence_from_fd(int fence_fd)
{
	return NULL;
}

static inline int rga_dma_fence_wait(struct dma_fence *fence)
{
	return 0;
}

static inline int rga_dma_fence_add_callback(struct dma_fence *fence,
					     dma_fence_func_t func,
					     void *private)
{
	return 0;
}

static inline void rga_dma_fence_put(struct dma_fence *fence)
{
}

static inline void rga_dma_fence_signal(struct dma_fence *fence, int error)
{
}

static inline int rga_dma_fence_get_status(struct dma_fence *fence)
{
	return 0;
}

#endif /* #ifdef CONFIG_SYNC_FILE */

#endif /* __LINUX_RGA_FENCE_H_ */
