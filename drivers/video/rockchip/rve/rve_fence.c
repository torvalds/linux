// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 *
 * Author: Huang Lee <Putin.li@rock-chips.com>
 */

#define pr_fmt(fmt) "rve_fence: " fmt

#include <linux/dma-fence.h>
#include <linux/sync_file.h>
#include <linux/slab.h>

#include "rve_fence.h"

static const char *rve_fence_get_name(struct dma_fence *fence)
{
	return DRIVER_NAME;
}

static const struct dma_fence_ops rve_fence_ops = {
	.get_driver_name = rve_fence_get_name,
	.get_timeline_name = rve_fence_get_name,
};

struct rve_fence_context *rve_fence_context_alloc(void)
{
	struct rve_fence_context *fence_ctx = NULL;

	fence_ctx = kzalloc(sizeof(*fence_ctx), GFP_KERNEL);
	if (!fence_ctx)
		return ERR_PTR(-ENOMEM);

	fence_ctx->context = dma_fence_context_alloc(1);
	spin_lock_init(&fence_ctx->spinlock);

	return fence_ctx;
}

void rve_fence_context_free(struct rve_fence_context *fence_ctx)
{
	kfree(fence_ctx);
}

int rve_out_fence_alloc(struct rve_job *job)
{
	struct rve_fence_context *fence_ctx = rve_drvdata->fence_ctx;
	struct dma_fence *fence = NULL;

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (!fence)
		return -ENOMEM;

	dma_fence_init(fence, &rve_fence_ops, &job->fence_lock,
			 fence_ctx->context, ++fence_ctx->seqno);

	job->out_fence = fence;

	return 0;
}

int rve_out_fence_get_fd(struct rve_job *job)
{
	struct sync_file *sync_file = NULL;
	int fence_fd = -1;

	if (!job->out_fence)
		return -EINVAL;

	fence_fd = get_unused_fd_flags(O_CLOEXEC);
	if (fence_fd < 0)
		return fence_fd;

	sync_file = sync_file_create(job->out_fence);
	if (!sync_file)
		return -ENOMEM;

	fd_install(fence_fd, sync_file->file);

	return fence_fd;
}

struct dma_fence *rve_get_input_fence(int in_fence_fd)
{
	struct dma_fence *in_fence;

	in_fence = sync_file_get_fence(in_fence_fd);

	if (!in_fence)
		pr_err("can not get in-fence from fd\n");

	return in_fence;
}

int rve_wait_input_fence(struct dma_fence *in_fence)
{
	int ret = 0;

	ret = dma_fence_wait(in_fence, true);

	dma_fence_put(in_fence);

	return ret;
}

int rve_add_dma_fence_callback(struct rve_job *job, struct dma_fence *in_fence,
				 dma_fence_func_t func)
{
	struct rve_fence_waiter *waiter;
	int ret;

	waiter = kmalloc(sizeof(*waiter), GFP_KERNEL);
	if (!waiter) {
		pr_err("%s: Failed to allocate waiter\n", __func__);
		return -ENOMEM;
	}

	waiter->job = job;

	ret = dma_fence_add_callback(in_fence, &waiter->waiter, func);
	if (ret == -ENOENT) {
		pr_err("'input fence' has been already signaled.");
		goto err_free_waiter;
	} else if (ret == -EINVAL) {
		pr_err
			("%s: failed to add callback to dma_fence, err: %d\n",
			 __func__, ret);
		goto err_free_waiter;
	}

	return ret;

err_free_waiter:
	kfree(waiter);
	return ret;
}
