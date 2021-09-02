// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2017 The Linux Foundation. All rights reserved.
 */

#include <linux/kref.h>
#include <linux/uaccess.h>

#include "msm_gpu.h"

void msm_submitqueue_destroy(struct kref *kref)
{
	struct msm_gpu_submitqueue *queue = container_of(kref,
		struct msm_gpu_submitqueue, ref);

	idr_destroy(&queue->fence_idr);

	drm_sched_entity_destroy(&queue->entity);

	msm_file_private_put(queue->ctx);

	kfree(queue);
}

struct msm_gpu_submitqueue *msm_submitqueue_get(struct msm_file_private *ctx,
		u32 id)
{
	struct msm_gpu_submitqueue *entry;

	if (!ctx)
		return NULL;

	read_lock(&ctx->queuelock);

	list_for_each_entry(entry, &ctx->submitqueues, node) {
		if (entry->id == id) {
			kref_get(&entry->ref);
			read_unlock(&ctx->queuelock);

			return entry;
		}
	}

	read_unlock(&ctx->queuelock);
	return NULL;
}

void msm_submitqueue_close(struct msm_file_private *ctx)
{
	struct msm_gpu_submitqueue *entry, *tmp;

	if (!ctx)
		return;

	/*
	 * No lock needed in close and there won't
	 * be any more user ioctls coming our way
	 */
	list_for_each_entry_safe(entry, tmp, &ctx->submitqueues, node) {
		list_del(&entry->node);
		msm_submitqueue_put(entry);
	}
}

int msm_submitqueue_create(struct drm_device *drm, struct msm_file_private *ctx,
		u32 prio, u32 flags, u32 *id)
{
	struct msm_drm_private *priv = drm->dev_private;
	struct msm_gpu_submitqueue *queue;
	struct msm_ringbuffer *ring;
	struct drm_gpu_scheduler *sched;
	enum drm_sched_priority sched_prio;
	unsigned ring_nr;
	int ret;

	if (!ctx)
		return -ENODEV;

	if (!priv->gpu)
		return -ENODEV;

	ret = msm_gpu_convert_priority(priv->gpu, prio, &ring_nr, &sched_prio);
	if (ret)
		return ret;

	queue = kzalloc(sizeof(*queue), GFP_KERNEL);

	if (!queue)
		return -ENOMEM;

	kref_init(&queue->ref);
	queue->flags = flags;
	queue->ring_nr = ring_nr;

	ring = priv->gpu->rb[ring_nr];
	sched = &ring->sched;

	ret = drm_sched_entity_init(&queue->entity,
			sched_prio, &sched, 1, NULL);
	if (ret) {
		kfree(queue);
		return ret;
	}

	write_lock(&ctx->queuelock);

	queue->ctx = msm_file_private_get(ctx);
	queue->id = ctx->queueid++;

	if (id)
		*id = queue->id;

	idr_init(&queue->fence_idr);
	mutex_init(&queue->lock);

	list_add_tail(&queue->node, &ctx->submitqueues);

	write_unlock(&ctx->queuelock);

	return 0;
}

/*
 * Create the default submit-queue (id==0), used for backwards compatibility
 * for userspace that pre-dates the introduction of submitqueues.
 */
int msm_submitqueue_init(struct drm_device *drm, struct msm_file_private *ctx)
{
	struct msm_drm_private *priv = drm->dev_private;
	int default_prio, max_priority;

	if (!priv->gpu)
		return -ENODEV;

	max_priority = (priv->gpu->nr_rings * NR_SCHED_PRIORITIES) - 1;

	/*
	 * Pick a medium priority level as default.  Lower numeric value is
	 * higher priority, so round-up to pick a priority that is not higher
	 * than the middle priority level.
	 */
	default_prio = DIV_ROUND_UP(max_priority, 2);

	INIT_LIST_HEAD(&ctx->submitqueues);

	rwlock_init(&ctx->queuelock);

	return msm_submitqueue_create(drm, ctx, default_prio, 0, NULL);
}

static int msm_submitqueue_query_faults(struct msm_gpu_submitqueue *queue,
		struct drm_msm_submitqueue_query *args)
{
	size_t size = min_t(size_t, args->len, sizeof(queue->faults));
	int ret;

	/* If a zero length was passed in, return the data size we expect */
	if (!args->len) {
		args->len = sizeof(queue->faults);
		return 0;
	}

	/* Set the length to the actual size of the data */
	args->len = size;

	ret = copy_to_user(u64_to_user_ptr(args->data), &queue->faults, size);

	return ret ? -EFAULT : 0;
}

int msm_submitqueue_query(struct drm_device *drm, struct msm_file_private *ctx,
		struct drm_msm_submitqueue_query *args)
{
	struct msm_gpu_submitqueue *queue;
	int ret = -EINVAL;

	if (args->pad)
		return -EINVAL;

	queue = msm_submitqueue_get(ctx, args->id);
	if (!queue)
		return -ENOENT;

	if (args->param == MSM_SUBMITQUEUE_PARAM_FAULTS)
		ret = msm_submitqueue_query_faults(queue, args);

	msm_submitqueue_put(queue);

	return ret;
}

int msm_submitqueue_remove(struct msm_file_private *ctx, u32 id)
{
	struct msm_gpu_submitqueue *entry;

	if (!ctx)
		return 0;

	/*
	 * id 0 is the "default" queue and can't be destroyed
	 * by the user
	 */
	if (!id)
		return -ENOENT;

	write_lock(&ctx->queuelock);

	list_for_each_entry(entry, &ctx->submitqueues, node) {
		if (entry->id == id) {
			list_del(&entry->node);
			write_unlock(&ctx->queuelock);

			msm_submitqueue_put(entry);
			return 0;
		}
	}

	write_unlock(&ctx->queuelock);
	return -ENOENT;
}

