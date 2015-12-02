/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 *
 */
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <drm/drmP.h>
#include "gpu_scheduler.h"

#define CREATE_TRACE_POINTS
#include "gpu_sched_trace.h"

static bool amd_sched_entity_is_ready(struct amd_sched_entity *entity);
static void amd_sched_wakeup(struct amd_gpu_scheduler *sched);

struct kmem_cache *sched_fence_slab;
atomic_t sched_fence_slab_ref = ATOMIC_INIT(0);

/* Initialize a given run queue struct */
static void amd_sched_rq_init(struct amd_sched_rq *rq)
{
	spin_lock_init(&rq->lock);
	INIT_LIST_HEAD(&rq->entities);
	rq->current_entity = NULL;
}

static void amd_sched_rq_add_entity(struct amd_sched_rq *rq,
				    struct amd_sched_entity *entity)
{
	spin_lock(&rq->lock);
	list_add_tail(&entity->list, &rq->entities);
	spin_unlock(&rq->lock);
}

static void amd_sched_rq_remove_entity(struct amd_sched_rq *rq,
				       struct amd_sched_entity *entity)
{
	spin_lock(&rq->lock);
	list_del_init(&entity->list);
	if (rq->current_entity == entity)
		rq->current_entity = NULL;
	spin_unlock(&rq->lock);
}

/**
 * Select an entity which could provide a job to run
 *
 * @rq		The run queue to check.
 *
 * Try to find a ready entity, returns NULL if none found.
 */
static struct amd_sched_entity *
amd_sched_rq_select_entity(struct amd_sched_rq *rq)
{
	struct amd_sched_entity *entity;

	spin_lock(&rq->lock);

	entity = rq->current_entity;
	if (entity) {
		list_for_each_entry_continue(entity, &rq->entities, list) {
			if (amd_sched_entity_is_ready(entity)) {
				rq->current_entity = entity;
				spin_unlock(&rq->lock);
				return entity;
			}
		}
	}

	list_for_each_entry(entity, &rq->entities, list) {

		if (amd_sched_entity_is_ready(entity)) {
			rq->current_entity = entity;
			spin_unlock(&rq->lock);
			return entity;
		}

		if (entity == rq->current_entity)
			break;
	}

	spin_unlock(&rq->lock);

	return NULL;
}

/**
 * Init a context entity used by scheduler when submit to HW ring.
 *
 * @sched	The pointer to the scheduler
 * @entity	The pointer to a valid amd_sched_entity
 * @rq		The run queue this entity belongs
 * @kernel	If this is an entity for the kernel
 * @jobs	The max number of jobs in the job queue
 *
 * return 0 if succeed. negative error code on failure
*/
int amd_sched_entity_init(struct amd_gpu_scheduler *sched,
			  struct amd_sched_entity *entity,
			  struct amd_sched_rq *rq,
			  uint32_t jobs)
{
	int r;

	if (!(sched && entity && rq))
		return -EINVAL;

	memset(entity, 0, sizeof(struct amd_sched_entity));
	INIT_LIST_HEAD(&entity->list);
	entity->rq = rq;
	entity->sched = sched;

	spin_lock_init(&entity->queue_lock);
	r = kfifo_alloc(&entity->job_queue, jobs * sizeof(void *), GFP_KERNEL);
	if (r)
		return r;

	atomic_set(&entity->fence_seq, 0);
	entity->fence_context = fence_context_alloc(1);

	/* Add the entity to the run queue */
	amd_sched_rq_add_entity(rq, entity);

	return 0;
}

/**
 * Query if entity is initialized
 *
 * @sched       Pointer to scheduler instance
 * @entity	The pointer to a valid scheduler entity
 *
 * return true if entity is initialized, false otherwise
*/
static bool amd_sched_entity_is_initialized(struct amd_gpu_scheduler *sched,
					    struct amd_sched_entity *entity)
{
	return entity->sched == sched &&
		entity->rq != NULL;
}

/**
 * Check if entity is idle
 *
 * @entity	The pointer to a valid scheduler entity
 *
 * Return true if entity don't has any unscheduled jobs.
 */
static bool amd_sched_entity_is_idle(struct amd_sched_entity *entity)
{
	rmb();
	if (kfifo_is_empty(&entity->job_queue))
		return true;

	return false;
}

/**
 * Check if entity is ready
 *
 * @entity	The pointer to a valid scheduler entity
 *
 * Return true if entity could provide a job.
 */
static bool amd_sched_entity_is_ready(struct amd_sched_entity *entity)
{
	if (kfifo_is_empty(&entity->job_queue))
		return false;

	if (ACCESS_ONCE(entity->dependency))
		return false;

	return true;
}

/**
 * Destroy a context entity
 *
 * @sched       Pointer to scheduler instance
 * @entity	The pointer to a valid scheduler entity
 *
 * Cleanup and free the allocated resources.
 */
void amd_sched_entity_fini(struct amd_gpu_scheduler *sched,
			   struct amd_sched_entity *entity)
{
	struct amd_sched_rq *rq = entity->rq;

	if (!amd_sched_entity_is_initialized(sched, entity))
		return;

	/**
	 * The client will not queue more IBs during this fini, consume existing
	 * queued IBs
	*/
	wait_event(sched->job_scheduled, amd_sched_entity_is_idle(entity));

	amd_sched_rq_remove_entity(rq, entity);
	kfifo_free(&entity->job_queue);
}

static void amd_sched_entity_wakeup(struct fence *f, struct fence_cb *cb)
{
	struct amd_sched_entity *entity =
		container_of(cb, struct amd_sched_entity, cb);
	entity->dependency = NULL;
	fence_put(f);
	amd_sched_wakeup(entity->sched);
}

static bool amd_sched_entity_add_dependency_cb(struct amd_sched_entity *entity)
{
	struct amd_gpu_scheduler *sched = entity->sched;
	struct fence * fence = entity->dependency;
	struct amd_sched_fence *s_fence;

	if (fence->context == entity->fence_context) {
		/* We can ignore fences from ourself */
		fence_put(entity->dependency);
		return false;
	}

	s_fence = to_amd_sched_fence(fence);
	if (s_fence && s_fence->sched == sched) {
		/* Fence is from the same scheduler */
		if (test_bit(AMD_SCHED_FENCE_SCHEDULED_BIT, &fence->flags)) {
			/* Ignore it when it is already scheduled */
			fence_put(entity->dependency);
			return false;
		}

		/* Wait for fence to be scheduled */
		entity->cb.func = amd_sched_entity_wakeup;
		list_add_tail(&entity->cb.node, &s_fence->scheduled_cb);
		return true;
	}

	if (!fence_add_callback(entity->dependency, &entity->cb,
				amd_sched_entity_wakeup))
		return true;

	fence_put(entity->dependency);
	return false;
}

static struct amd_sched_job *
amd_sched_entity_pop_job(struct amd_sched_entity *entity)
{
	struct amd_gpu_scheduler *sched = entity->sched;
	struct amd_sched_job *sched_job;

	if (!kfifo_out_peek(&entity->job_queue, &sched_job, sizeof(sched_job)))
		return NULL;

	while ((entity->dependency = sched->ops->dependency(sched_job)))
		if (amd_sched_entity_add_dependency_cb(entity))
			return NULL;

	return sched_job;
}

/**
 * Helper to submit a job to the job queue
 *
 * @sched_job		The pointer to job required to submit
 *
 * Returns true if we could submit the job.
 */
static bool amd_sched_entity_in(struct amd_sched_job *sched_job)
{
	struct amd_gpu_scheduler *sched = sched_job->sched;
	struct amd_sched_entity *entity = sched_job->s_entity;
	bool added, first = false;

	spin_lock(&entity->queue_lock);
	added = kfifo_in(&entity->job_queue, &sched_job,
			sizeof(sched_job)) == sizeof(sched_job);

	if (added && kfifo_len(&entity->job_queue) == sizeof(sched_job))
		first = true;

	spin_unlock(&entity->queue_lock);

	/* first job wakes up scheduler */
	if (first)
		amd_sched_wakeup(sched);

	return added;
}

/**
 * Submit a job to the job queue
 *
 * @sched_job		The pointer to job required to submit
 *
 * Returns 0 for success, negative error code otherwise.
 */
void amd_sched_entity_push_job(struct amd_sched_job *sched_job)
{
	struct amd_sched_entity *entity = sched_job->s_entity;

	trace_amd_sched_job(sched_job);
	wait_event(entity->sched->job_scheduled,
		   amd_sched_entity_in(sched_job));
}

/**
 * Return ture if we can push more jobs to the hw.
 */
static bool amd_sched_ready(struct amd_gpu_scheduler *sched)
{
	return atomic_read(&sched->hw_rq_count) <
		sched->hw_submission_limit;
}

/**
 * Wake up the scheduler when it is ready
 */
static void amd_sched_wakeup(struct amd_gpu_scheduler *sched)
{
	if (amd_sched_ready(sched))
		wake_up_interruptible(&sched->wake_up_worker);
}

/**
 * Select next entity to process
*/
static struct amd_sched_entity *
amd_sched_select_entity(struct amd_gpu_scheduler *sched)
{
	struct amd_sched_entity *entity;
	int i;

	if (!amd_sched_ready(sched))
		return NULL;

	/* Kernel run queue has higher priority than normal run queue*/
	for (i = 0; i < AMD_SCHED_MAX_PRIORITY; i++) {
		entity = amd_sched_rq_select_entity(&sched->sched_rq[i]);
		if (entity)
			break;
	}

	return entity;
}

static void amd_sched_process_job(struct fence *f, struct fence_cb *cb)
{
	struct amd_sched_fence *s_fence =
		container_of(cb, struct amd_sched_fence, cb);
	struct amd_gpu_scheduler *sched = s_fence->sched;
	unsigned long flags;

	atomic_dec(&sched->hw_rq_count);
	amd_sched_fence_signal(s_fence);
	if (sched->timeout != MAX_SCHEDULE_TIMEOUT) {
		cancel_delayed_work(&s_fence->dwork);
		spin_lock_irqsave(&sched->fence_list_lock, flags);
		list_del_init(&s_fence->list);
		spin_unlock_irqrestore(&sched->fence_list_lock, flags);
	}
	trace_amd_sched_process_job(s_fence);
	fence_put(&s_fence->base);
	wake_up_interruptible(&sched->wake_up_worker);
}

static void amd_sched_fence_work_func(struct work_struct *work)
{
	struct amd_sched_fence *s_fence =
		container_of(work, struct amd_sched_fence, dwork.work);
	struct amd_gpu_scheduler *sched = s_fence->sched;
	struct amd_sched_fence *entity, *tmp;
	unsigned long flags;

	DRM_ERROR("[%s] scheduler is timeout!\n", sched->name);

	/* Clean all pending fences */
	spin_lock_irqsave(&sched->fence_list_lock, flags);
	list_for_each_entry_safe(entity, tmp, &sched->fence_list, list) {
		DRM_ERROR("  fence no %d\n", entity->base.seqno);
		cancel_delayed_work(&entity->dwork);
		list_del_init(&entity->list);
		fence_put(&entity->base);
	}
	spin_unlock_irqrestore(&sched->fence_list_lock, flags);
}

static int amd_sched_main(void *param)
{
	struct sched_param sparam = {.sched_priority = 1};
	struct amd_gpu_scheduler *sched = (struct amd_gpu_scheduler *)param;
	int r, count;

	spin_lock_init(&sched->fence_list_lock);
	INIT_LIST_HEAD(&sched->fence_list);
	sched_setscheduler(current, SCHED_FIFO, &sparam);

	while (!kthread_should_stop()) {
		struct amd_sched_entity *entity;
		struct amd_sched_fence *s_fence;
		struct amd_sched_job *sched_job;
		struct fence *fence;
		unsigned long flags;

		wait_event_interruptible(sched->wake_up_worker,
			(entity = amd_sched_select_entity(sched)) ||
			kthread_should_stop());

		if (!entity)
			continue;

		sched_job = amd_sched_entity_pop_job(entity);
		if (!sched_job)
			continue;

		s_fence = sched_job->s_fence;

		if (sched->timeout != MAX_SCHEDULE_TIMEOUT) {
			INIT_DELAYED_WORK(&s_fence->dwork, amd_sched_fence_work_func);
			schedule_delayed_work(&s_fence->dwork, sched->timeout);
			spin_lock_irqsave(&sched->fence_list_lock, flags);
			list_add_tail(&s_fence->list, &sched->fence_list);
			spin_unlock_irqrestore(&sched->fence_list_lock, flags);
		}

		atomic_inc(&sched->hw_rq_count);
		fence = sched->ops->run_job(sched_job);
		amd_sched_fence_scheduled(s_fence);
		if (fence) {
			r = fence_add_callback(fence, &s_fence->cb,
					       amd_sched_process_job);
			if (r == -ENOENT)
				amd_sched_process_job(fence, &s_fence->cb);
			else if (r)
				DRM_ERROR("fence add callback failed (%d)\n", r);
			fence_put(fence);
		} else {
			DRM_ERROR("Failed to run job!\n");
			amd_sched_process_job(NULL, &s_fence->cb);
		}

		count = kfifo_out(&entity->job_queue, &sched_job,
				sizeof(sched_job));
		WARN_ON(count != sizeof(sched_job));
		wake_up(&sched->job_scheduled);
	}
	return 0;
}

/**
 * Init a gpu scheduler instance
 *
 * @sched		The pointer to the scheduler
 * @ops			The backend operations for this scheduler.
 * @hw_submissions	Number of hw submissions to do.
 * @name		Name used for debugging
 *
 * Return 0 on success, otherwise error code.
*/
int amd_sched_init(struct amd_gpu_scheduler *sched,
		   struct amd_sched_backend_ops *ops,
		   unsigned hw_submission, long timeout, const char *name)
{
	int i;
	sched->ops = ops;
	sched->hw_submission_limit = hw_submission;
	sched->name = name;
	sched->timeout = timeout;
	for (i = 0; i < AMD_SCHED_MAX_PRIORITY; i++)
		amd_sched_rq_init(&sched->sched_rq[i]);

	init_waitqueue_head(&sched->wake_up_worker);
	init_waitqueue_head(&sched->job_scheduled);
	atomic_set(&sched->hw_rq_count, 0);
	if (atomic_inc_return(&sched_fence_slab_ref) == 1) {
		sched_fence_slab = kmem_cache_create(
			"amd_sched_fence", sizeof(struct amd_sched_fence), 0,
			SLAB_HWCACHE_ALIGN, NULL);
		if (!sched_fence_slab)
			return -ENOMEM;
	}

	/* Each scheduler will run on a seperate kernel thread */
	sched->thread = kthread_run(amd_sched_main, sched, sched->name);
	if (IS_ERR(sched->thread)) {
		DRM_ERROR("Failed to create scheduler for %s.\n", name);
		return PTR_ERR(sched->thread);
	}

	return 0;
}

/**
 * Destroy a gpu scheduler
 *
 * @sched	The pointer to the scheduler
 */
void amd_sched_fini(struct amd_gpu_scheduler *sched)
{
	if (sched->thread)
		kthread_stop(sched->thread);
	if (atomic_dec_and_test(&sched_fence_slab_ref))
		kmem_cache_destroy(sched_fence_slab);
}
