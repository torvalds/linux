// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2017 The Linux Foundation. All rights reserved.
 */

#include <linux/kref.h>
#include <linux/uaccess.h>

#include "msm_gpu.h"

int msm_context_set_sysprof(struct msm_context *ctx, struct msm_gpu *gpu, int sysprof)
{
	/*
	 * Since pm_runtime and sysprof_active are both refcounts, we
	 * call apply the new value first, and then unwind the previous
	 * value
	 */

	switch (sysprof) {
	default:
		return UERR(EINVAL, gpu->dev, "Invalid sysprof: %d", sysprof);
	case 2:
		pm_runtime_get_sync(&gpu->pdev->dev);
		fallthrough;
	case 1:
		refcount_inc(&gpu->sysprof_active);
		fallthrough;
	case 0:
		break;
	}

	/* unwind old value: */
	switch (ctx->sysprof) {
	case 2:
		pm_runtime_put_autosuspend(&gpu->pdev->dev);
		fallthrough;
	case 1:
		refcount_dec(&gpu->sysprof_active);
		fallthrough;
	case 0:
		break;
	}

	/* Some gpu families require additional setup for sysprof */
	if (gpu->funcs->sysprof_setup)
		gpu->funcs->sysprof_setup(gpu);

	ctx->sysprof = sysprof;

	return 0;
}

void __msm_context_destroy(struct kref *kref)
{
	struct msm_context *ctx = container_of(kref,
		struct msm_context, ref);
	int i;

	for (i = 0; i < ARRAY_SIZE(ctx->entities); i++) {
		if (!ctx->entities[i])
			continue;

		drm_sched_entity_destroy(ctx->entities[i]);
		kfree(ctx->entities[i]);
	}

	drm_gpuvm_put(ctx->vm);
	kfree(ctx->comm);
	kfree(ctx->cmdline);
	kfree(ctx);
}

void msm_submitqueue_destroy(struct kref *kref)
{
	struct msm_gpu_submitqueue *queue = container_of(kref,
		struct msm_gpu_submitqueue, ref);

	idr_destroy(&queue->fence_idr);

	if (queue->entity == &queue->_vm_bind_entity[0])
		drm_sched_entity_destroy(queue->entity);

	msm_context_put(queue->ctx);

	kfree(queue);
}

struct msm_gpu_submitqueue *msm_submitqueue_get(struct msm_context *ctx,
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

void msm_submitqueue_close(struct msm_context *ctx)
{
	struct msm_gpu_submitqueue *queue, *tmp;

	if (!ctx)
		return;

	/*
	 * No lock needed in close and there won't
	 * be any more user ioctls coming our way
	 */
	list_for_each_entry_safe(queue, tmp, &ctx->submitqueues, node) {
		if (queue->entity == &queue->_vm_bind_entity[0])
			drm_sched_entity_flush(queue->entity, MAX_WAIT_SCHED_ENTITY_Q_EMPTY);
		list_del(&queue->node);
		msm_submitqueue_put(queue);
	}

	if (!ctx->vm)
		return;

	msm_gem_vm_close(ctx->vm);
}

static struct drm_sched_entity *
get_sched_entity(struct msm_context *ctx, struct msm_ringbuffer *ring,
		 unsigned ring_nr, enum drm_sched_priority sched_prio)
{
	static DEFINE_MUTEX(entity_lock);
	unsigned idx = (ring_nr * NR_SCHED_PRIORITIES) + sched_prio;

	/* We should have already validated that the requested priority is
	 * valid by the time we get here.
	 */
	if (WARN_ON(idx >= ARRAY_SIZE(ctx->entities)))
		return ERR_PTR(-EINVAL);

	mutex_lock(&entity_lock);

	if (!ctx->entities[idx]) {
		struct drm_sched_entity *entity;
		struct drm_gpu_scheduler *sched = &ring->sched;
		int ret;

		entity = kzalloc(sizeof(*ctx->entities[idx]), GFP_KERNEL);

		ret = drm_sched_entity_init(entity, sched_prio, &sched, 1, NULL);
		if (ret) {
			mutex_unlock(&entity_lock);
			kfree(entity);
			return ERR_PTR(ret);
		}

		ctx->entities[idx] = entity;
	}

	mutex_unlock(&entity_lock);

	return ctx->entities[idx];
}

int msm_submitqueue_create(struct drm_device *drm, struct msm_context *ctx,
		u32 prio, u32 flags, u32 *id)
{
	struct msm_drm_private *priv = drm->dev_private;
	struct msm_gpu_submitqueue *queue;
	enum drm_sched_priority sched_prio;
	unsigned ring_nr;
	int ret;

	if (!ctx)
		return -ENODEV;

	if (!priv->gpu)
		return -ENODEV;

	if (flags & MSM_SUBMITQUEUE_VM_BIND) {
		unsigned sz;

		/* Not allowed for kernel managed VMs (ie. kernel allocs VA) */
		if (!msm_context_is_vmbind(ctx))
			return -EINVAL;

		if (prio)
			return -EINVAL;

		sz = struct_size(queue, _vm_bind_entity, 1);
		queue = kzalloc(sz, GFP_KERNEL);
	} else {
		extern int enable_preemption;
		bool preemption_supported =
			priv->gpu->nr_rings == 1 && enable_preemption != 0;

		if (flags & MSM_SUBMITQUEUE_ALLOW_PREEMPT && preemption_supported)
			return -EINVAL;

		ret = msm_gpu_convert_priority(priv->gpu, prio, &ring_nr, &sched_prio);
		if (ret)
			return ret;

		queue = kzalloc(sizeof(*queue), GFP_KERNEL);
	}

	if (!queue)
		return -ENOMEM;

	kref_init(&queue->ref);
	queue->flags = flags;

	if (flags & MSM_SUBMITQUEUE_VM_BIND) {
		struct drm_gpu_scheduler *sched = &to_msm_vm(msm_context_vm(drm, ctx))->sched;

		queue->entity = &queue->_vm_bind_entity[0];

		drm_sched_entity_init(queue->entity, DRM_SCHED_PRIORITY_KERNEL,
				      &sched, 1, NULL);
	} else {
		queue->ring_nr = ring_nr;

		queue->entity = get_sched_entity(ctx, priv->gpu->rb[ring_nr],
						 ring_nr, sched_prio);
	}

	if (IS_ERR(queue->entity)) {
		ret = PTR_ERR(queue->entity);
		kfree(queue);
		return ret;
	}

	write_lock(&ctx->queuelock);

	queue->ctx = msm_context_get(ctx);
	queue->id = ctx->queueid++;

	if (id)
		*id = queue->id;

	idr_init(&queue->fence_idr);
	spin_lock_init(&queue->idr_lock);
	mutex_init(&queue->lock);

	list_add_tail(&queue->node, &ctx->submitqueues);

	write_unlock(&ctx->queuelock);

	return 0;
}

/*
 * Create the default submit-queue (id==0), used for backwards compatibility
 * for userspace that pre-dates the introduction of submitqueues.
 */
int msm_submitqueue_init(struct drm_device *drm, struct msm_context *ctx)
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

int msm_submitqueue_query(struct drm_device *drm, struct msm_context *ctx,
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

int msm_submitqueue_remove(struct msm_context *ctx, u32 id)
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

