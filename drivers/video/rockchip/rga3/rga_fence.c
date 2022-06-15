// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 *
 * Author: Huang Lee <Putin.li@rock-chips.com>
 */

#define pr_fmt(fmt) "rga_fence: " fmt

#include <linux/dma-fence.h>
#include <linux/sync_file.h>
#include <linux/slab.h>

#include "rga_drv.h"
#include "rga_fence.h"

static const char *rga_fence_get_name(struct dma_fence *fence)
{
	return DRIVER_NAME;
}

static const struct dma_fence_ops rga_fence_ops = {
	.get_driver_name = rga_fence_get_name,
	.get_timeline_name = rga_fence_get_name,
};

int rga_fence_context_init(struct rga_fence_context **ctx)
{
	struct rga_fence_context *fence_ctx = NULL;

	fence_ctx = kzalloc(sizeof(struct rga_fence_context), GFP_KERNEL);
	if (!fence_ctx) {
		pr_err("can not kzalloc for rga_fence_context!\n");
		return -ENOMEM;
	}

	fence_ctx->context = dma_fence_context_alloc(1);
	spin_lock_init(&fence_ctx->spinlock);

	*ctx = fence_ctx;

	return 0;
}

void rga_fence_context_remove(struct rga_fence_context **ctx)
{
	if (*ctx == NULL)
		return;

	kfree(*ctx);
	*ctx = NULL;
}

struct dma_fence *rga_dma_fence_alloc(void)
{
	struct rga_fence_context *fence_ctx = rga_drvdata->fence_ctx;
	struct dma_fence *fence = NULL;

	if (fence_ctx == NULL) {
		pr_err("fence_context is NULL!\n");
		return ERR_PTR(-EINVAL);
	}

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (!fence)
		return ERR_PTR(-ENOMEM);

	dma_fence_init(fence, &rga_fence_ops, &fence_ctx->spinlock,
		       fence_ctx->context, ++fence_ctx->seqno);

	return fence;
}

int rga_dma_fence_get_fd(struct dma_fence *fence)
{
	struct sync_file *sync_file = NULL;
	int fence_fd = -1;

	if (!fence)
		return -EINVAL;

	fence_fd = get_unused_fd_flags(O_CLOEXEC);
	if (fence_fd < 0)
		return fence_fd;

	sync_file = sync_file_create(fence);
	if (!sync_file)
		return -ENOMEM;

	fd_install(fence_fd, sync_file->file);

	return fence_fd;
}

struct dma_fence *rga_get_dma_fence_from_fd(int fence_fd)
{
	struct dma_fence *fence;

	fence = sync_file_get_fence(fence_fd);
	if (!fence)
		pr_err("can not get fence from fd\n");

	return fence;
}

int rga_dma_fence_wait(struct dma_fence *fence)
{
	int ret = 0;

	ret = dma_fence_wait(fence, true);

	dma_fence_put(fence);

	return ret;
}

int rga_dma_fence_add_callback(struct dma_fence *fence, dma_fence_func_t func, void *private)
{
	int ret;
	struct rga_fence_waiter *waiter = NULL;

	waiter = kmalloc(sizeof(*waiter), GFP_KERNEL);
	if (!waiter) {
		pr_err("%s: Failed to allocate waiter\n", __func__);
		return -ENOMEM;
	}

	waiter->private = private;

	ret = dma_fence_add_callback(fence, &waiter->waiter, func);
	if (ret == -ENOENT) {
		pr_err("'input fence' has been already signaled.");
		goto err_free_waiter;
	} else if (ret == -EINVAL) {
		pr_err("%s: failed to add callback to dma_fence, err: %d\n", __func__, ret);
		goto err_free_waiter;
	}

	return ret;

err_free_waiter:
	kfree(waiter);
	return ret;
}
