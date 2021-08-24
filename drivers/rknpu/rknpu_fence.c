// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author: Felix Zeng <felix.zeng@rock-chips.com>
 */

#include <linux/slab.h>
#include <linux/file.h>
#include <linux/dma-fence.h>
#include <linux/sync_file.h>

#include "rknpu_drv.h"
#include "rknpu_job.h"

#include "rknpu_fence.h"

static const char *rknpu_fence_get_name(struct dma_fence *fence)
{
	return DRIVER_NAME;
}

static const struct dma_fence_ops rknpu_fence_ops = {
	.get_driver_name = rknpu_fence_get_name,
	.get_timeline_name = rknpu_fence_get_name,
};

struct rknpu_fence_context *rknpu_fence_context_alloc(void)
{
	struct rknpu_fence_context *fence_ctx = NULL;

	fence_ctx = kzalloc(sizeof(*fence_ctx), GFP_KERNEL);
	if (!fence_ctx)
		return ERR_PTR(-ENOMEM);

	fence_ctx->context = dma_fence_context_alloc(1);
	spin_lock_init(&fence_ctx->spinlock);

	return fence_ctx;
}

void rknpu_fence_context_free(struct rknpu_fence_context *fence_ctx)
{
	if (!IS_ERR(fence_ctx))
		kfree(fence_ctx);
}

int rknpu_fence_alloc(struct rknpu_job *job)
{
	struct rknpu_fence_context *fence_ctx = job->rknpu_dev->fence_ctx;
	struct dma_fence *fence = NULL;

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (!fence)
		return -ENOMEM;

	dma_fence_init(fence, &rknpu_fence_ops, &job->fence_lock,
		       fence_ctx->context, ++fence_ctx->seqno);

	job->fence = fence;

	return 0;
}

int rknpu_fence_get_fd(struct rknpu_job *job)
{
	struct sync_file *sync_file = NULL;
	int fence_fd = -1;

	if (!job->fence)
		return -EINVAL;

	fence_fd = get_unused_fd_flags(O_CLOEXEC);
	if (fence_fd < 0)
		return fence_fd;

	sync_file = sync_file_create(job->fence);
	if (!sync_file)
		return -ENOMEM;

	fd_install(fence_fd, sync_file->file);

	return fence_fd;
}
