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
#include <uapi/linux/sched/types.h>
#include <drm/drmP.h>
#include "gpu_scheduler.h"

#include "spsc_queue.h"

#define CREATE_TRACE_POINTS
#include "gpu_sched_trace.h"

#define to_amd_sched_job(sched_job)		\
		container_of((sched_job), struct amd_sched_job, queue_node)

static bool amd_sched_entity_is_ready(struct amd_sched_entity *entity);
static void amd_sched_wakeup(struct amd_gpu_scheduler *sched);
static void amd_sched_process_job(struct dma_fence *f, struct dma_fence_cb *cb);

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
	if (!list_empty(&entity->list))
		return;
	spin_lock(&rq->lock);
	list_add_tail(&entity->list, &rq->entities);
	spin_unlock(&rq->lock);
}

static void amd_sched_rq_remove_entity(struct amd_sched_rq *rq,
				       struct amd_sched_entity *entity)
{
	if (list_empty(&entity->list))
		return;
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
			  uint32_t jobs, atomic_t *guilty)
{
	if (!(sched && entity && rq))
		return -EINVAL;

	memset(entity, 0, sizeof(struct amd_sched_entity));
	INIT_LIST_HEAD(&entity->list);
	entity->rq = rq;
	entity->sched = sched;
	entity->guilty = guilty;

	spin_lock_init(&entity->rq_lock);
	spin_lock_init(&entity->queue_lock);
	spsc_queue_init(&entity->job_queue);

	atomic_set(&entity->fence_seq, 0);
	entity->fence_context = dma_fence_context_alloc(2);

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
	if (spsc_queue_peek(&entity->job_queue) == NULL)
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
	if (spsc_queue_peek(&entity->job_queue) == NULL)
		return false;

	if (READ_ONCE(entity->dependency))
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
	int r;

	if (!amd_sched_entity_is_initialized(sched, entity))
		return;
	/**
	 * The client will not queue more IBs during this fini, consume existing
	 * queued IBs or discard them on SIGKILL
	*/
	if ((current->flags & PF_SIGNALED) && current->exit_code == SIGKILL)
		r = -ERESTARTSYS;
	else
		r = wait_event_killable(sched->job_scheduled,
					amd_sched_entity_is_idle(entity));
	amd_sched_entity_set_rq(entity, NULL);
	if (r) {
		struct amd_sched_job *job;

		/* Park the kernel for a moment to make sure it isn't processing
		 * our enity.
		 */
		kthread_park(sched->thread);
		kthread_unpark(sched->thread);
		while ((job = to_amd_sched_job(spsc_queue_pop(&entity->job_queue)))) {
			struct amd_sched_fence *s_fence = job->s_fence;
			amd_sched_fence_scheduled(s_fence);
			dma_fence_set_error(&s_fence->finished, -ESRCH);
			amd_sched_fence_finished(s_fence);
			dma_fence_put(&s_fence->finished);
			sched->ops->free_job(job);
		}
	}
}

static void amd_sched_entity_wakeup(struct dma_fence *f, struct dma_fence_cb *cb)
{
	struct amd_sched_entity *entity =
		container_of(cb, struct amd_sched_entity, cb);
	entity->dependency = NULL;
	dma_fence_put(f);
	amd_sched_wakeup(entity->sched);
}

static void amd_sched_entity_clear_dep(struct dma_fence *f, struct dma_fence_cb *cb)
{
	struct amd_sched_entity *entity =
		container_of(cb, struct amd_sched_entity, cb);
	entity->dependency = NULL;
	dma_fence_put(f);
}

void amd_sched_entity_set_rq(struct amd_sched_entity *entity,
			     struct amd_sched_rq *rq)
{
	if (entity->rq == rq)
		return;

	spin_lock(&entity->rq_lock);

	if (entity->rq)
		amd_sched_rq_remove_entity(entity->rq, entity);

	entity->rq = rq;
	if (rq)
		amd_sched_rq_add_entity(rq, entity);

	spin_unlock(&entity->rq_lock);
}

bool amd_sched_dependency_optimized(struct dma_fence* fence,
				    struct amd_sched_entity *entity)
{
	struct amd_gpu_scheduler *sched = entity->sched;
	struct amd_sched_fence *s_fence;

	if (!fence || dma_fence_is_signaled(fence))
		return false;
	if (fence->context == entity->fence_context)
		return true;
	s_fence = to_amd_sched_fence(fence);
	if (s_fence && s_fence->sched == sched)
		return true;

	return false;
}

static bool amd_sched_entity_add_dependency_cb(struct amd_sched_entity *entity)
{
	struct amd_gpu_scheduler *sched = entity->sched;
	struct dma_fence * fence = entity->dependency;
	struct amd_sched_fence *s_fence;

	if (fence->context == entity->fence_context) {
		/* We can ignore fences from ourself */
		dma_fence_put(entity->dependency);
		return false;
	}

	s_fence = to_amd_sched_fence(fence);
	if (s_fence && s_fence->sched == sched) {

		/*
		 * Fence is from the same scheduler, only need to wait for
		 * it to be scheduled
		 */
		fence = dma_fence_get(&s_fence->scheduled);
		dma_fence_put(entity->dependency);
		entity->dependency = fence;
		if (!dma_fence_add_callback(fence, &entity->cb,
					    amd_sched_entity_clear_dep))
			return true;

		/* Ignore it when it is already scheduled */
		dma_fence_put(fence);
		return false;
	}

	if (!dma_fence_add_callback(entity->dependency, &entity->cb,
				    amd_sched_entity_wakeup))
		return true;

	dma_fence_put(entity->dependency);
	return false;
}

static struct amd_sched_job *
amd_sched_entity_pop_job(struct amd_sched_entity *entity)
{
	struct amd_gpu_scheduler *sched = entity->sched;
	struct amd_sched_job *sched_job = to_amd_sched_job(
						spsc_queue_peek(&entity->job_queue));

	if (!sched_job)
		return NULL;

	while ((entity->dependency = sched->ops->dependency(sched_job, entity)))
		if (amd_sched_entity_add_dependency_cb(entity))
			return NULL;

	/* skip jobs from entity that marked guilty */
	if (entity->guilty && atomic_read(entity->guilty))
		dma_fence_set_error(&sched_job->s_fence->finished, -ECANCELED);

	spsc_queue_pop(&entity->job_queue);
	return sched_job;
}

/**
 * Submit a job to the job queue
 *
 * @sched_job		The pointer to job required to submit
 *
 * Returns 0 for success, negative error code otherwise.
 */
void amd_sched_entity_push_job(struct amd_sched_job *sched_job,
			       struct amd_sched_entity *entity)
{
	struct amd_gpu_scheduler *sched = sched_job->sched;
	bool first = false;

	trace_amd_sched_job(sched_job, entity);

	spin_lock(&entity->queue_lock);
	first = spsc_queue_push(&entity->job_queue, &sched_job->queue_node);

	spin_unlock(&entity->queue_lock);

	/* first job wakes up scheduler */
	if (first) {
		/* Add the entity to the run queue */
		spin_lock(&entity->rq_lock);
		amd_sched_rq_add_entity(entity->rq, entity);
		spin_unlock(&entity->rq_lock);
		amd_sched_wakeup(sched);
	}
}

/* job_finish is called after hw fence signaled
 */
static void amd_sched_job_finish(struct work_struct *work)
{
	struct amd_sched_job *s_job = container_of(work, struct amd_sched_job,
						   finish_work);
	struct amd_gpu_scheduler *sched = s_job->sched;

	/* remove job from ring_mirror_list */
	spin_lock(&sched->job_list_lock);
	list_del_init(&s_job->node);
	if (sched->timeout != MAX_SCHEDULE_TIMEOUT) {
		struct amd_sched_job *next;

		spin_unlock(&sched->job_list_lock);
		cancel_delayed_work_sync(&s_job->work_tdr);
		spin_lock(&sched->job_list_lock);

		/* queue TDR for next job */
		next = list_first_entry_or_null(&sched->ring_mirror_list,
						struct amd_sched_job, node);

		if (next)
			schedule_delayed_work(&next->work_tdr, sched->timeout);
	}
	spin_unlock(&sched->job_list_lock);
	dma_fence_put(&s_job->s_fence->finished);
	sched->ops->free_job(s_job);
}

static void amd_sched_job_finish_cb(struct dma_fence *f,
				    struct dma_fence_cb *cb)
{
	struct amd_sched_job *job = container_of(cb, struct amd_sched_job,
						 finish_cb);
	schedule_work(&job->finish_work);
}

static void amd_sched_job_begin(struct amd_sched_job *s_job)
{
	struct amd_gpu_scheduler *sched = s_job->sched;

	dma_fence_add_callback(&s_job->s_fence->finished, &s_job->finish_cb,
			       amd_sched_job_finish_cb);

	spin_lock(&sched->job_list_lock);
	list_add_tail(&s_job->node, &sched->ring_mirror_list);
	if (sched->timeout != MAX_SCHEDULE_TIMEOUT &&
	    list_first_entry_or_null(&sched->ring_mirror_list,
				     struct amd_sched_job, node) == s_job)
		schedule_delayed_work(&s_job->work_tdr, sched->timeout);
	spin_unlock(&sched->job_list_lock);
}

static void amd_sched_job_timedout(struct work_struct *work)
{
	struct amd_sched_job *job = container_of(work, struct amd_sched_job,
						 work_tdr.work);

	job->sched->ops->timedout_job(job);
}

void amd_sched_hw_job_reset(struct amd_gpu_scheduler *sched, struct amd_sched_job *bad)
{
	struct amd_sched_job *s_job;
	struct amd_sched_entity *entity, *tmp;
	int i;;

	spin_lock(&sched->job_list_lock);
	list_for_each_entry_reverse(s_job, &sched->ring_mirror_list, node) {
		if (s_job->s_fence->parent &&
		    dma_fence_remove_callback(s_job->s_fence->parent,
					      &s_job->s_fence->cb)) {
			dma_fence_put(s_job->s_fence->parent);
			s_job->s_fence->parent = NULL;
			atomic_dec(&sched->hw_rq_count);
		}
	}
	spin_unlock(&sched->job_list_lock);

	if (bad && bad->s_priority != AMD_SCHED_PRIORITY_KERNEL) {
		atomic_inc(&bad->karma);
		/* don't increase @bad's karma if it's from KERNEL RQ,
		 * becuase sometimes GPU hang would cause kernel jobs (like VM updating jobs)
		 * corrupt but keep in mind that kernel jobs always considered good.
		 */
		for (i = AMD_SCHED_PRIORITY_MIN; i < AMD_SCHED_PRIORITY_KERNEL; i++ ) {
			struct amd_sched_rq *rq = &sched->sched_rq[i];

			spin_lock(&rq->lock);
			list_for_each_entry_safe(entity, tmp, &rq->entities, list) {
				if (bad->s_fence->scheduled.context == entity->fence_context) {
				    if (atomic_read(&bad->karma) > bad->sched->hang_limit)
						if (entity->guilty)
							atomic_set(entity->guilty, 1);
					break;
				}
			}
			spin_unlock(&rq->lock);
			if (&entity->list != &rq->entities)
				break;
		}
	}
}

void amd_sched_job_kickout(struct amd_sched_job *s_job)
{
	struct amd_gpu_scheduler *sched = s_job->sched;

	spin_lock(&sched->job_list_lock);
	list_del_init(&s_job->node);
	spin_unlock(&sched->job_list_lock);
}

void amd_sched_job_recovery(struct amd_gpu_scheduler *sched)
{
	struct amd_sched_job *s_job, *tmp;
	bool found_guilty = false;
	int r;

	spin_lock(&sched->job_list_lock);
	s_job = list_first_entry_or_null(&sched->ring_mirror_list,
					 struct amd_sched_job, node);
	if (s_job && sched->timeout != MAX_SCHEDULE_TIMEOUT)
		schedule_delayed_work(&s_job->work_tdr, sched->timeout);

	list_for_each_entry_safe(s_job, tmp, &sched->ring_mirror_list, node) {
		struct amd_sched_fence *s_fence = s_job->s_fence;
		struct dma_fence *fence;
		uint64_t guilty_context;

		if (!found_guilty && atomic_read(&s_job->karma) > sched->hang_limit) {
			found_guilty = true;
			guilty_context = s_job->s_fence->scheduled.context;
		}

		if (found_guilty && s_job->s_fence->scheduled.context == guilty_context)
			dma_fence_set_error(&s_fence->finished, -ECANCELED);

		spin_unlock(&sched->job_list_lock);
		fence = sched->ops->run_job(s_job);
		atomic_inc(&sched->hw_rq_count);
		if (fence) {
			s_fence->parent = dma_fence_get(fence);
			r = dma_fence_add_callback(fence, &s_fence->cb,
						   amd_sched_process_job);
			if (r == -ENOENT)
				amd_sched_process_job(fence, &s_fence->cb);
			else if (r)
				DRM_ERROR("fence add callback failed (%d)\n",
					  r);
			dma_fence_put(fence);
		} else {
			amd_sched_process_job(NULL, &s_fence->cb);
		}
		spin_lock(&sched->job_list_lock);
	}
	spin_unlock(&sched->job_list_lock);
}

/* init a sched_job with basic field */
int amd_sched_job_init(struct amd_sched_job *job,
		       struct amd_gpu_scheduler *sched,
		       struct amd_sched_entity *entity,
		       void *owner)
{
	job->sched = sched;
	job->s_priority = entity->rq - sched->sched_rq;
	job->s_fence = amd_sched_fence_create(entity, owner);
	if (!job->s_fence)
		return -ENOMEM;
	job->id = atomic64_inc_return(&sched->job_id_count);

	INIT_WORK(&job->finish_work, amd_sched_job_finish);
	INIT_LIST_HEAD(&job->node);
	INIT_DELAYED_WORK(&job->work_tdr, amd_sched_job_timedout);

	return 0;
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
	for (i = AMD_SCHED_PRIORITY_MAX - 1; i >= AMD_SCHED_PRIORITY_MIN; i--) {
		entity = amd_sched_rq_select_entity(&sched->sched_rq[i]);
		if (entity)
			break;
	}

	return entity;
}

static void amd_sched_process_job(struct dma_fence *f, struct dma_fence_cb *cb)
{
	struct amd_sched_fence *s_fence =
		container_of(cb, struct amd_sched_fence, cb);
	struct amd_gpu_scheduler *sched = s_fence->sched;

	dma_fence_get(&s_fence->finished);
	atomic_dec(&sched->hw_rq_count);
	amd_sched_fence_finished(s_fence);

	trace_amd_sched_process_job(s_fence);
	dma_fence_put(&s_fence->finished);
	wake_up_interruptible(&sched->wake_up_worker);
}

static bool amd_sched_blocked(struct amd_gpu_scheduler *sched)
{
	if (kthread_should_park()) {
		kthread_parkme();
		return true;
	}

	return false;
}

static int amd_sched_main(void *param)
{
	struct sched_param sparam = {.sched_priority = 1};
	struct amd_gpu_scheduler *sched = (struct amd_gpu_scheduler *)param;
	int r;

	sched_setscheduler(current, SCHED_FIFO, &sparam);

	while (!kthread_should_stop()) {
		struct amd_sched_entity *entity = NULL;
		struct amd_sched_fence *s_fence;
		struct amd_sched_job *sched_job;
		struct dma_fence *fence;

		wait_event_interruptible(sched->wake_up_worker,
					 (!amd_sched_blocked(sched) &&
					  (entity = amd_sched_select_entity(sched))) ||
					 kthread_should_stop());

		if (!entity)
			continue;

		sched_job = amd_sched_entity_pop_job(entity);
		if (!sched_job)
			continue;

		s_fence = sched_job->s_fence;

		atomic_inc(&sched->hw_rq_count);
		amd_sched_job_begin(sched_job);

		fence = sched->ops->run_job(sched_job);
		amd_sched_fence_scheduled(s_fence);

		if (fence) {
			s_fence->parent = dma_fence_get(fence);
			r = dma_fence_add_callback(fence, &s_fence->cb,
						   amd_sched_process_job);
			if (r == -ENOENT)
				amd_sched_process_job(fence, &s_fence->cb);
			else if (r)
				DRM_ERROR("fence add callback failed (%d)\n",
					  r);
			dma_fence_put(fence);
		} else {
			amd_sched_process_job(NULL, &s_fence->cb);
		}

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
		   const struct amd_sched_backend_ops *ops,
		   unsigned hw_submission,
		   unsigned hang_limit,
		   long timeout,
		   const char *name)
{
	int i;
	sched->ops = ops;
	sched->hw_submission_limit = hw_submission;
	sched->name = name;
	sched->timeout = timeout;
	sched->hang_limit = hang_limit;
	for (i = AMD_SCHED_PRIORITY_MIN; i < AMD_SCHED_PRIORITY_MAX; i++)
		amd_sched_rq_init(&sched->sched_rq[i]);

	init_waitqueue_head(&sched->wake_up_worker);
	init_waitqueue_head(&sched->job_scheduled);
	INIT_LIST_HEAD(&sched->ring_mirror_list);
	spin_lock_init(&sched->job_list_lock);
	atomic_set(&sched->hw_rq_count, 0);
	atomic64_set(&sched->job_id_count, 0);

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
}
