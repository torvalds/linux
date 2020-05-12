// SPDX-License-Identifier: GPL-2.0 OR MIT
/* Copyright 2017-2019 Qiang Yu <yuq825@gmail.com> */

#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/xarray.h>

#include "lima_drv.h"
#include "lima_sched.h"
#include "lima_vm.h"
#include "lima_mmu.h"
#include "lima_l2_cache.h"
#include "lima_gem.h"

struct lima_fence {
	struct dma_fence base;
	struct lima_sched_pipe *pipe;
};

static struct kmem_cache *lima_fence_slab;
static int lima_fence_slab_refcnt;

int lima_sched_slab_init(void)
{
	if (!lima_fence_slab) {
		lima_fence_slab = kmem_cache_create(
			"lima_fence", sizeof(struct lima_fence), 0,
			SLAB_HWCACHE_ALIGN, NULL);
		if (!lima_fence_slab)
			return -ENOMEM;
	}

	lima_fence_slab_refcnt++;
	return 0;
}

void lima_sched_slab_fini(void)
{
	if (!--lima_fence_slab_refcnt) {
		kmem_cache_destroy(lima_fence_slab);
		lima_fence_slab = NULL;
	}
}

static inline struct lima_fence *to_lima_fence(struct dma_fence *fence)
{
	return container_of(fence, struct lima_fence, base);
}

static const char *lima_fence_get_driver_name(struct dma_fence *fence)
{
	return "lima";
}

static const char *lima_fence_get_timeline_name(struct dma_fence *fence)
{
	struct lima_fence *f = to_lima_fence(fence);

	return f->pipe->base.name;
}

static void lima_fence_release_rcu(struct rcu_head *rcu)
{
	struct dma_fence *f = container_of(rcu, struct dma_fence, rcu);
	struct lima_fence *fence = to_lima_fence(f);

	kmem_cache_free(lima_fence_slab, fence);
}

static void lima_fence_release(struct dma_fence *fence)
{
	struct lima_fence *f = to_lima_fence(fence);

	call_rcu(&f->base.rcu, lima_fence_release_rcu);
}

static const struct dma_fence_ops lima_fence_ops = {
	.get_driver_name = lima_fence_get_driver_name,
	.get_timeline_name = lima_fence_get_timeline_name,
	.release = lima_fence_release,
};

static struct lima_fence *lima_fence_create(struct lima_sched_pipe *pipe)
{
	struct lima_fence *fence;

	fence = kmem_cache_zalloc(lima_fence_slab, GFP_KERNEL);
	if (!fence)
		return NULL;

	fence->pipe = pipe;
	dma_fence_init(&fence->base, &lima_fence_ops, &pipe->fence_lock,
		       pipe->fence_context, ++pipe->fence_seqno);

	return fence;
}

static inline struct lima_sched_task *to_lima_task(struct drm_sched_job *job)
{
	return container_of(job, struct lima_sched_task, base);
}

static inline struct lima_sched_pipe *to_lima_pipe(struct drm_gpu_scheduler *sched)
{
	return container_of(sched, struct lima_sched_pipe, base);
}

int lima_sched_task_init(struct lima_sched_task *task,
			 struct lima_sched_context *context,
			 struct lima_bo **bos, int num_bos,
			 struct lima_vm *vm)
{
	int err, i;

	task->bos = kmemdup(bos, sizeof(*bos) * num_bos, GFP_KERNEL);
	if (!task->bos)
		return -ENOMEM;

	for (i = 0; i < num_bos; i++)
		drm_gem_object_get(&bos[i]->base.base);

	err = drm_sched_job_init(&task->base, &context->base, vm);
	if (err) {
		kfree(task->bos);
		return err;
	}

	task->num_bos = num_bos;
	task->vm = lima_vm_get(vm);

	xa_init_flags(&task->deps, XA_FLAGS_ALLOC);

	return 0;
}

void lima_sched_task_fini(struct lima_sched_task *task)
{
	struct dma_fence *fence;
	unsigned long index;
	int i;

	drm_sched_job_cleanup(&task->base);

	xa_for_each(&task->deps, index, fence) {
		dma_fence_put(fence);
	}
	xa_destroy(&task->deps);

	if (task->bos) {
		for (i = 0; i < task->num_bos; i++)
			drm_gem_object_put_unlocked(&task->bos[i]->base.base);
		kfree(task->bos);
	}

	lima_vm_put(task->vm);
}

int lima_sched_context_init(struct lima_sched_pipe *pipe,
			    struct lima_sched_context *context,
			    atomic_t *guilty)
{
	struct drm_gpu_scheduler *sched = &pipe->base;

	return drm_sched_entity_init(&context->base, DRM_SCHED_PRIORITY_NORMAL,
				     &sched, 1, guilty);
}

void lima_sched_context_fini(struct lima_sched_pipe *pipe,
			     struct lima_sched_context *context)
{
	drm_sched_entity_fini(&context->base);
}

struct dma_fence *lima_sched_context_queue_task(struct lima_sched_context *context,
						struct lima_sched_task *task)
{
	struct dma_fence *fence = dma_fence_get(&task->base.s_fence->finished);

	drm_sched_entity_push_job(&task->base, &context->base);
	return fence;
}

static struct dma_fence *lima_sched_dependency(struct drm_sched_job *job,
					       struct drm_sched_entity *entity)
{
	struct lima_sched_task *task = to_lima_task(job);

	if (!xa_empty(&task->deps))
		return xa_erase(&task->deps, task->last_dep++);

	return NULL;
}

static struct dma_fence *lima_sched_run_job(struct drm_sched_job *job)
{
	struct lima_sched_task *task = to_lima_task(job);
	struct lima_sched_pipe *pipe = to_lima_pipe(job->sched);
	struct lima_fence *fence;
	struct dma_fence *ret;
	struct lima_vm *vm = NULL, *last_vm = NULL;
	int i;

	/* after GPU reset */
	if (job->s_fence->finished.error < 0)
		return NULL;

	fence = lima_fence_create(pipe);
	if (!fence)
		return NULL;
	task->fence = &fence->base;

	/* for caller usage of the fence, otherwise irq handler
	 * may consume the fence before caller use it
	 */
	ret = dma_fence_get(task->fence);

	pipe->current_task = task;

	/* this is needed for MMU to work correctly, otherwise GP/PP
	 * will hang or page fault for unknown reason after running for
	 * a while.
	 *
	 * Need to investigate:
	 * 1. is it related to TLB
	 * 2. how much performance will be affected by L2 cache flush
	 * 3. can we reduce the calling of this function because all
	 *    GP/PP use the same L2 cache on mali400
	 *
	 * TODO:
	 * 1. move this to task fini to save some wait time?
	 * 2. when GP/PP use different l2 cache, need PP wait GP l2
	 *    cache flush?
	 */
	for (i = 0; i < pipe->num_l2_cache; i++)
		lima_l2_cache_flush(pipe->l2_cache[i]);

	if (task->vm != pipe->current_vm) {
		vm = lima_vm_get(task->vm);
		last_vm = pipe->current_vm;
		pipe->current_vm = task->vm;
	}

	if (pipe->bcast_mmu)
		lima_mmu_switch_vm(pipe->bcast_mmu, vm);
	else {
		for (i = 0; i < pipe->num_mmu; i++)
			lima_mmu_switch_vm(pipe->mmu[i], vm);
	}

	if (last_vm)
		lima_vm_put(last_vm);

	pipe->error = false;
	pipe->task_run(pipe, task);

	return task->fence;
}

static void lima_sched_timedout_job(struct drm_sched_job *job)
{
	struct lima_sched_pipe *pipe = to_lima_pipe(job->sched);
	struct lima_sched_task *task = to_lima_task(job);

	if (!pipe->error)
		DRM_ERROR("lima job timeout\n");

	drm_sched_stop(&pipe->base, &task->base);

	drm_sched_increase_karma(&task->base);

	pipe->task_error(pipe);

	if (pipe->bcast_mmu)
		lima_mmu_page_fault_resume(pipe->bcast_mmu);
	else {
		int i;

		for (i = 0; i < pipe->num_mmu; i++)
			lima_mmu_page_fault_resume(pipe->mmu[i]);
	}

	if (pipe->current_vm)
		lima_vm_put(pipe->current_vm);

	pipe->current_vm = NULL;
	pipe->current_task = NULL;

	drm_sched_resubmit_jobs(&pipe->base);
	drm_sched_start(&pipe->base, true);
}

static void lima_sched_free_job(struct drm_sched_job *job)
{
	struct lima_sched_task *task = to_lima_task(job);
	struct lima_sched_pipe *pipe = to_lima_pipe(job->sched);
	struct lima_vm *vm = task->vm;
	struct lima_bo **bos = task->bos;
	int i;

	dma_fence_put(task->fence);

	for (i = 0; i < task->num_bos; i++)
		lima_vm_bo_del(vm, bos[i]);

	lima_sched_task_fini(task);
	kmem_cache_free(pipe->task_slab, task);
}

static const struct drm_sched_backend_ops lima_sched_ops = {
	.dependency = lima_sched_dependency,
	.run_job = lima_sched_run_job,
	.timedout_job = lima_sched_timedout_job,
	.free_job = lima_sched_free_job,
};

int lima_sched_pipe_init(struct lima_sched_pipe *pipe, const char *name)
{
	unsigned int timeout = lima_sched_timeout_ms > 0 ?
			       lima_sched_timeout_ms : 500;

	pipe->fence_context = dma_fence_context_alloc(1);
	spin_lock_init(&pipe->fence_lock);

	return drm_sched_init(&pipe->base, &lima_sched_ops, 1, 0,
			      msecs_to_jiffies(timeout), name);
}

void lima_sched_pipe_fini(struct lima_sched_pipe *pipe)
{
	drm_sched_fini(&pipe->base);
}

void lima_sched_pipe_task_done(struct lima_sched_pipe *pipe)
{
	if (pipe->error)
		drm_sched_fault(&pipe->base);
	else {
		struct lima_sched_task *task = pipe->current_task;

		pipe->task_fini(pipe);
		dma_fence_signal(task->fence);
	}
}
