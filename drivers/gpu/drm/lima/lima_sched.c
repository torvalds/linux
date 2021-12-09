// SPDX-License-Identifier: GPL-2.0 OR MIT
/* Copyright 2017-2019 Qiang Yu <yuq825@gmail.com> */

#include <linux/dma-buf-map.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/pm_runtime.h>

#include "lima_devfreq.h"
#include "lima_drv.h"
#include "lima_sched.h"
#include "lima_vm.h"
#include "lima_mmu.h"
#include "lima_l2_cache.h"
#include "lima_gem.h"
#include "lima_trace.h"

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
			drm_gem_object_put(&task->bos[i]->base.base);
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

	trace_lima_task_submit(task);
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

static int lima_pm_busy(struct lima_device *ldev)
{
	int ret;

	/* resume GPU if it has been suspended by runtime PM */
	ret = pm_runtime_resume_and_get(ldev->dev);
	if (ret < 0)
		return ret;

	lima_devfreq_record_busy(&ldev->devfreq);
	return 0;
}

static void lima_pm_idle(struct lima_device *ldev)
{
	lima_devfreq_record_idle(&ldev->devfreq);

	/* GPU can do auto runtime suspend */
	pm_runtime_mark_last_busy(ldev->dev);
	pm_runtime_put_autosuspend(ldev->dev);
}

static struct dma_fence *lima_sched_run_job(struct drm_sched_job *job)
{
	struct lima_sched_task *task = to_lima_task(job);
	struct lima_sched_pipe *pipe = to_lima_pipe(job->sched);
	struct lima_device *ldev = pipe->ldev;
	struct lima_fence *fence;
	int i, err;

	/* after GPU reset */
	if (job->s_fence->finished.error < 0)
		return NULL;

	fence = lima_fence_create(pipe);
	if (!fence)
		return NULL;

	err = lima_pm_busy(ldev);
	if (err < 0) {
		dma_fence_put(&fence->base);
		return NULL;
	}

	task->fence = &fence->base;

	/* for caller usage of the fence, otherwise irq handler
	 * may consume the fence before caller use it
	 */
	dma_fence_get(task->fence);

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

	lima_vm_put(pipe->current_vm);
	pipe->current_vm = lima_vm_get(task->vm);

	if (pipe->bcast_mmu)
		lima_mmu_switch_vm(pipe->bcast_mmu, pipe->current_vm);
	else {
		for (i = 0; i < pipe->num_mmu; i++)
			lima_mmu_switch_vm(pipe->mmu[i], pipe->current_vm);
	}

	trace_lima_task_run(task);

	pipe->error = false;
	pipe->task_run(pipe, task);

	return task->fence;
}

static void lima_sched_build_error_task_list(struct lima_sched_task *task)
{
	struct lima_sched_error_task *et;
	struct lima_sched_pipe *pipe = to_lima_pipe(task->base.sched);
	struct lima_ip *ip = pipe->processor[0];
	int pipe_id = ip->id == lima_ip_gp ? lima_pipe_gp : lima_pipe_pp;
	struct lima_device *dev = ip->dev;
	struct lima_sched_context *sched_ctx =
		container_of(task->base.entity,
			     struct lima_sched_context, base);
	struct lima_ctx *ctx =
		container_of(sched_ctx, struct lima_ctx, context[pipe_id]);
	struct lima_dump_task *dt;
	struct lima_dump_chunk *chunk;
	struct lima_dump_chunk_pid *pid_chunk;
	struct lima_dump_chunk_buffer *buffer_chunk;
	u32 size, task_size, mem_size;
	int i;
	struct dma_buf_map map;
	int ret;

	mutex_lock(&dev->error_task_list_lock);

	if (dev->dump.num_tasks >= lima_max_error_tasks) {
		dev_info(dev->dev, "fail to save task state from %s pid %d: "
			 "error task list is full\n", ctx->pname, ctx->pid);
		goto out;
	}

	/* frame chunk */
	size = sizeof(struct lima_dump_chunk) + pipe->frame_size;
	/* process name chunk */
	size += sizeof(struct lima_dump_chunk) + sizeof(ctx->pname);
	/* pid chunk */
	size += sizeof(struct lima_dump_chunk);
	/* buffer chunks */
	for (i = 0; i < task->num_bos; i++) {
		struct lima_bo *bo = task->bos[i];

		size += sizeof(struct lima_dump_chunk);
		size += bo->heap_size ? bo->heap_size : lima_bo_size(bo);
	}

	task_size = size + sizeof(struct lima_dump_task);
	mem_size = task_size + sizeof(*et);
	et = kvmalloc(mem_size, GFP_KERNEL);
	if (!et) {
		dev_err(dev->dev, "fail to alloc task dump buffer of size %x\n",
			mem_size);
		goto out;
	}

	et->data = et + 1;
	et->size = task_size;

	dt = et->data;
	memset(dt, 0, sizeof(*dt));
	dt->id = pipe_id;
	dt->size = size;

	chunk = (struct lima_dump_chunk *)(dt + 1);
	memset(chunk, 0, sizeof(*chunk));
	chunk->id = LIMA_DUMP_CHUNK_FRAME;
	chunk->size = pipe->frame_size;
	memcpy(chunk + 1, task->frame, pipe->frame_size);
	dt->num_chunks++;

	chunk = (void *)(chunk + 1) + chunk->size;
	memset(chunk, 0, sizeof(*chunk));
	chunk->id = LIMA_DUMP_CHUNK_PROCESS_NAME;
	chunk->size = sizeof(ctx->pname);
	memcpy(chunk + 1, ctx->pname, sizeof(ctx->pname));
	dt->num_chunks++;

	pid_chunk = (void *)(chunk + 1) + chunk->size;
	memset(pid_chunk, 0, sizeof(*pid_chunk));
	pid_chunk->id = LIMA_DUMP_CHUNK_PROCESS_ID;
	pid_chunk->pid = ctx->pid;
	dt->num_chunks++;

	buffer_chunk = (void *)(pid_chunk + 1) + pid_chunk->size;
	for (i = 0; i < task->num_bos; i++) {
		struct lima_bo *bo = task->bos[i];
		void *data;

		memset(buffer_chunk, 0, sizeof(*buffer_chunk));
		buffer_chunk->id = LIMA_DUMP_CHUNK_BUFFER;
		buffer_chunk->va = lima_vm_get_va(task->vm, bo);

		if (bo->heap_size) {
			buffer_chunk->size = bo->heap_size;

			data = vmap(bo->base.pages, bo->heap_size >> PAGE_SHIFT,
				    VM_MAP, pgprot_writecombine(PAGE_KERNEL));
			if (!data) {
				kvfree(et);
				goto out;
			}

			memcpy(buffer_chunk + 1, data, buffer_chunk->size);

			vunmap(data);
		} else {
			buffer_chunk->size = lima_bo_size(bo);

			ret = drm_gem_shmem_vmap(&bo->base.base, &map);
			if (ret) {
				kvfree(et);
				goto out;
			}

			memcpy(buffer_chunk + 1, map.vaddr, buffer_chunk->size);

			drm_gem_shmem_vunmap(&bo->base.base, &map);
		}

		buffer_chunk = (void *)(buffer_chunk + 1) + buffer_chunk->size;
		dt->num_chunks++;
	}

	list_add(&et->list, &dev->error_task_list);
	dev->dump.size += et->size;
	dev->dump.num_tasks++;

	dev_info(dev->dev, "save error task state success\n");

out:
	mutex_unlock(&dev->error_task_list_lock);
}

static enum drm_gpu_sched_stat lima_sched_timedout_job(struct drm_sched_job *job)
{
	struct lima_sched_pipe *pipe = to_lima_pipe(job->sched);
	struct lima_sched_task *task = to_lima_task(job);
	struct lima_device *ldev = pipe->ldev;

	if (!pipe->error)
		DRM_ERROR("lima job timeout\n");

	drm_sched_stop(&pipe->base, &task->base);

	drm_sched_increase_karma(&task->base);

	lima_sched_build_error_task_list(task);

	pipe->task_error(pipe);

	if (pipe->bcast_mmu)
		lima_mmu_page_fault_resume(pipe->bcast_mmu);
	else {
		int i;

		for (i = 0; i < pipe->num_mmu; i++)
			lima_mmu_page_fault_resume(pipe->mmu[i]);
	}

	lima_vm_put(pipe->current_vm);
	pipe->current_vm = NULL;
	pipe->current_task = NULL;

	lima_pm_idle(ldev);

	drm_sched_resubmit_jobs(&pipe->base);
	drm_sched_start(&pipe->base, true);

	return DRM_GPU_SCHED_STAT_NOMINAL;
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

static void lima_sched_recover_work(struct work_struct *work)
{
	struct lima_sched_pipe *pipe =
		container_of(work, struct lima_sched_pipe, recover_work);
	int i;

	for (i = 0; i < pipe->num_l2_cache; i++)
		lima_l2_cache_flush(pipe->l2_cache[i]);

	if (pipe->bcast_mmu) {
		lima_mmu_flush_tlb(pipe->bcast_mmu);
	} else {
		for (i = 0; i < pipe->num_mmu; i++)
			lima_mmu_flush_tlb(pipe->mmu[i]);
	}

	if (pipe->task_recover(pipe))
		drm_sched_fault(&pipe->base);
}

int lima_sched_pipe_init(struct lima_sched_pipe *pipe, const char *name)
{
	unsigned int timeout = lima_sched_timeout_ms > 0 ?
			       lima_sched_timeout_ms : 500;

	pipe->fence_context = dma_fence_context_alloc(1);
	spin_lock_init(&pipe->fence_lock);

	INIT_WORK(&pipe->recover_work, lima_sched_recover_work);

	return drm_sched_init(&pipe->base, &lima_sched_ops, 1,
			      lima_job_hang_limit,
			      msecs_to_jiffies(timeout), NULL,
			      NULL, name);
}

void lima_sched_pipe_fini(struct lima_sched_pipe *pipe)
{
	drm_sched_fini(&pipe->base);
}

void lima_sched_pipe_task_done(struct lima_sched_pipe *pipe)
{
	struct lima_sched_task *task = pipe->current_task;
	struct lima_device *ldev = pipe->ldev;

	if (pipe->error) {
		if (task && task->recoverable)
			schedule_work(&pipe->recover_work);
		else
			drm_sched_fault(&pipe->base);
	} else {
		pipe->task_fini(pipe);
		dma_fence_signal(task->fence);

		lima_pm_idle(ldev);
	}
}
