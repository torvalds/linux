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
 */

/**
 * DOC: Overview
 *
 * The GPU scheduler provides entities which allow userspace to push jobs
 * into software queues which are then scheduled on a hardware run queue.
 * The software queues have a priority among them. The scheduler selects the entities
 * from the run queue using a FIFO. The scheduler provides dependency handling
 * features among jobs. The driver is supposed to provide callback functions for
 * backend operations to the scheduler like submitting a job to hardware run queue,
 * returning the dependencies of a job etc.
 *
 * The organisation of the scheduler is the following:
 *
 * 1. Each hw run queue has one scheduler
 * 2. Each scheduler has multiple run queues with different priorities
 *    (e.g., HIGH_HW,HIGH_SW, KERNEL, NORMAL)
 * 3. Each scheduler run queue has a queue of entities to schedule
 * 4. Entities themselves maintain a queue of jobs that will be scheduled on
 *    the hardware.
 *
 * The jobs in a entity are always scheduled in the order that they were pushed.
 */

#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <uapi/linux/sched/types.h>
#include <drm/drmP.h>
#include <drm/gpu_scheduler.h>
#include <drm/spsc_queue.h>

#define CREATE_TRACE_POINTS
#include "gpu_scheduler_trace.h"

#define to_drm_sched_job(sched_job)		\
		container_of((sched_job), struct drm_sched_job, queue_node)

static bool drm_sched_entity_is_ready(struct drm_sched_entity *entity);
static void drm_sched_wakeup(struct drm_gpu_scheduler *sched);
static void drm_sched_process_job(struct dma_fence *f, struct dma_fence_cb *cb);

/**
 * drm_sched_rq_init - initialize a given run queue struct
 *
 * @rq: scheduler run queue
 *
 * Initializes a scheduler runqueue.
 */
static void drm_sched_rq_init(struct drm_gpu_scheduler *sched,
			      struct drm_sched_rq *rq)
{
	spin_lock_init(&rq->lock);
	INIT_LIST_HEAD(&rq->entities);
	rq->current_entity = NULL;
	rq->sched = sched;
}

/**
 * drm_sched_rq_add_entity - add an entity
 *
 * @rq: scheduler run queue
 * @entity: scheduler entity
 *
 * Adds a scheduler entity to the run queue.
 */
static void drm_sched_rq_add_entity(struct drm_sched_rq *rq,
				    struct drm_sched_entity *entity)
{
	if (!list_empty(&entity->list))
		return;
	spin_lock(&rq->lock);
	list_add_tail(&entity->list, &rq->entities);
	spin_unlock(&rq->lock);
}

/**
 * drm_sched_rq_remove_entity - remove an entity
 *
 * @rq: scheduler run queue
 * @entity: scheduler entity
 *
 * Removes a scheduler entity from the run queue.
 */
static void drm_sched_rq_remove_entity(struct drm_sched_rq *rq,
				       struct drm_sched_entity *entity)
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
 * drm_sched_rq_select_entity - Select an entity which could provide a job to run
 *
 * @rq: scheduler run queue to check.
 *
 * Try to find a ready entity, returns NULL if none found.
 */
static struct drm_sched_entity *
drm_sched_rq_select_entity(struct drm_sched_rq *rq)
{
	struct drm_sched_entity *entity;

	spin_lock(&rq->lock);

	entity = rq->current_entity;
	if (entity) {
		list_for_each_entry_continue(entity, &rq->entities, list) {
			if (drm_sched_entity_is_ready(entity)) {
				rq->current_entity = entity;
				spin_unlock(&rq->lock);
				return entity;
			}
		}
	}

	list_for_each_entry(entity, &rq->entities, list) {

		if (drm_sched_entity_is_ready(entity)) {
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
 * drm_sched_entity_init - Init a context entity used by scheduler when
 * submit to HW ring.
 *
 * @entity: scheduler entity to init
 * @rq_list: the list of run queue on which jobs from this
 *           entity can be submitted
 * @num_rq_list: number of run queue in rq_list
 * @guilty: atomic_t set to 1 when a job on this queue
 *          is found to be guilty causing a timeout
 *
 * Note: the rq_list should have atleast one element to schedule
 *       the entity
 *
 * Returns 0 on success or a negative error code on failure.
*/
int drm_sched_entity_init(struct drm_sched_entity *entity,
			  struct drm_sched_rq **rq_list,
			  unsigned int num_rq_list,
			  atomic_t *guilty)
{
	if (!(entity && rq_list && num_rq_list > 0 && rq_list[0]))
		return -EINVAL;

	memset(entity, 0, sizeof(struct drm_sched_entity));
	INIT_LIST_HEAD(&entity->list);
	entity->rq = rq_list[0];
	entity->sched = rq_list[0]->sched;
	entity->guilty = guilty;
	entity->last_scheduled = NULL;

	spin_lock_init(&entity->rq_lock);
	spsc_queue_init(&entity->job_queue);

	atomic_set(&entity->fence_seq, 0);
	entity->fence_context = dma_fence_context_alloc(2);

	return 0;
}
EXPORT_SYMBOL(drm_sched_entity_init);

/**
 * drm_sched_entity_is_initialized - Query if entity is initialized
 *
 * @sched: Pointer to scheduler instance
 * @entity: The pointer to a valid scheduler entity
 *
 * return true if entity is initialized, false otherwise
*/
static bool drm_sched_entity_is_initialized(struct drm_gpu_scheduler *sched,
					    struct drm_sched_entity *entity)
{
	return entity->sched == sched &&
		entity->rq != NULL;
}

/**
 * drm_sched_entity_is_idle - Check if entity is idle
 *
 * @entity: scheduler entity
 *
 * Returns true if the entity does not have any unscheduled jobs.
 */
static bool drm_sched_entity_is_idle(struct drm_sched_entity *entity)
{
	rmb();

	if (!entity->rq || spsc_queue_peek(&entity->job_queue) == NULL)
		return true;

	return false;
}

/**
 * drm_sched_entity_is_ready - Check if entity is ready
 *
 * @entity: scheduler entity
 *
 * Return true if entity could provide a job.
 */
static bool drm_sched_entity_is_ready(struct drm_sched_entity *entity)
{
	if (spsc_queue_peek(&entity->job_queue) == NULL)
		return false;

	if (READ_ONCE(entity->dependency))
		return false;

	return true;
}

static void drm_sched_entity_kill_jobs_cb(struct dma_fence *f,
				    struct dma_fence_cb *cb)
{
	struct drm_sched_job *job = container_of(cb, struct drm_sched_job,
						 finish_cb);
	drm_sched_fence_finished(job->s_fence);
	WARN_ON(job->s_fence->parent);
	dma_fence_put(&job->s_fence->finished);
	job->sched->ops->free_job(job);
}


/**
 * drm_sched_entity_flush - Flush a context entity
 *
 * @sched: scheduler instance
 * @entity: scheduler entity
 * @timeout: time to wait in for Q to become empty in jiffies.
 *
 * Splitting drm_sched_entity_fini() into two functions, The first one does the waiting,
 * removes the entity from the runqueue and returns an error when the process was killed.
 *
 * Returns the remaining time in jiffies left from the input timeout
 */
long drm_sched_entity_flush(struct drm_gpu_scheduler *sched,
			   struct drm_sched_entity *entity, long timeout)
{
	long ret = timeout;

	if (!drm_sched_entity_is_initialized(sched, entity))
		return ret;
	/**
	 * The client will not queue more IBs during this fini, consume existing
	 * queued IBs or discard them on SIGKILL
	*/
	if (current->flags & PF_EXITING) {
		if (timeout)
			ret = wait_event_timeout(
					sched->job_scheduled,
					drm_sched_entity_is_idle(entity),
					timeout);
	} else
		wait_event_killable(sched->job_scheduled, drm_sched_entity_is_idle(entity));


	/* For killed process disable any more IBs enqueue right now */
	if ((current->flags & PF_EXITING) && (current->exit_code == SIGKILL))
		drm_sched_entity_set_rq(entity, NULL);

	return ret;
}
EXPORT_SYMBOL(drm_sched_entity_flush);

/**
 * drm_sched_entity_cleanup - Destroy a context entity
 *
 * @sched: scheduler instance
 * @entity: scheduler entity
 *
 * This should be called after @drm_sched_entity_do_release. It goes over the
 * entity and signals all jobs with an error code if the process was killed.
 *
 */
void drm_sched_entity_fini(struct drm_gpu_scheduler *sched,
			   struct drm_sched_entity *entity)
{

	drm_sched_entity_set_rq(entity, NULL);

	/* Consumption of existing IBs wasn't completed. Forcefully
	 * remove them here.
	 */
	if (spsc_queue_peek(&entity->job_queue)) {
		struct drm_sched_job *job;
		int r;

		/* Park the kernel for a moment to make sure it isn't processing
		 * our enity.
		 */
		kthread_park(sched->thread);
		kthread_unpark(sched->thread);
		if (entity->dependency) {
			dma_fence_remove_callback(entity->dependency,
						  &entity->cb);
			dma_fence_put(entity->dependency);
			entity->dependency = NULL;
		}

		while ((job = to_drm_sched_job(spsc_queue_pop(&entity->job_queue)))) {
			struct drm_sched_fence *s_fence = job->s_fence;
			drm_sched_fence_scheduled(s_fence);
			dma_fence_set_error(&s_fence->finished, -ESRCH);

			/*
			 * When pipe is hanged by older entity, new entity might
			 * not even have chance to submit it's first job to HW
			 * and so entity->last_scheduled will remain NULL
			 */
			if (!entity->last_scheduled) {
				drm_sched_entity_kill_jobs_cb(NULL, &job->finish_cb);
			} else {
				r = dma_fence_add_callback(entity->last_scheduled, &job->finish_cb,
								drm_sched_entity_kill_jobs_cb);
				if (r == -ENOENT)
					drm_sched_entity_kill_jobs_cb(NULL, &job->finish_cb);
				else if (r)
					DRM_ERROR("fence add callback failed (%d)\n", r);
			}
		}
	}

	dma_fence_put(entity->last_scheduled);
	entity->last_scheduled = NULL;
}
EXPORT_SYMBOL(drm_sched_entity_fini);

/**
 * drm_sched_entity_fini - Destroy a context entity
 *
 * @sched: scheduler instance
 * @entity: scheduler entity
 *
 * Calls drm_sched_entity_do_release() and drm_sched_entity_cleanup()
 */
void drm_sched_entity_destroy(struct drm_gpu_scheduler *sched,
				struct drm_sched_entity *entity)
{
	drm_sched_entity_flush(sched, entity, MAX_WAIT_SCHED_ENTITY_Q_EMPTY);
	drm_sched_entity_fini(sched, entity);
}
EXPORT_SYMBOL(drm_sched_entity_destroy);

static void drm_sched_entity_wakeup(struct dma_fence *f, struct dma_fence_cb *cb)
{
	struct drm_sched_entity *entity =
		container_of(cb, struct drm_sched_entity, cb);
	entity->dependency = NULL;
	dma_fence_put(f);
	drm_sched_wakeup(entity->sched);
}

static void drm_sched_entity_clear_dep(struct dma_fence *f, struct dma_fence_cb *cb)
{
	struct drm_sched_entity *entity =
		container_of(cb, struct drm_sched_entity, cb);
	entity->dependency = NULL;
	dma_fence_put(f);
}

/**
 * drm_sched_entity_set_rq - Sets the run queue for an entity
 *
 * @entity: scheduler entity
 * @rq: scheduler run queue
 *
 * Sets the run queue for an entity and removes the entity from the previous
 * run queue in which was present.
 */
void drm_sched_entity_set_rq(struct drm_sched_entity *entity,
			     struct drm_sched_rq *rq)
{
	if (entity->rq == rq)
		return;

	spin_lock(&entity->rq_lock);

	if (entity->rq)
		drm_sched_rq_remove_entity(entity->rq, entity);

	entity->rq = rq;
	if (rq)
		drm_sched_rq_add_entity(rq, entity);

	spin_unlock(&entity->rq_lock);
}
EXPORT_SYMBOL(drm_sched_entity_set_rq);

/**
 * drm_sched_dependency_optimized
 *
 * @fence: the dependency fence
 * @entity: the entity which depends on the above fence
 *
 * Returns true if the dependency can be optimized and false otherwise
 */
bool drm_sched_dependency_optimized(struct dma_fence* fence,
				    struct drm_sched_entity *entity)
{
	struct drm_gpu_scheduler *sched = entity->sched;
	struct drm_sched_fence *s_fence;

	if (!fence || dma_fence_is_signaled(fence))
		return false;
	if (fence->context == entity->fence_context)
		return true;
	s_fence = to_drm_sched_fence(fence);
	if (s_fence && s_fence->sched == sched)
		return true;

	return false;
}
EXPORT_SYMBOL(drm_sched_dependency_optimized);

static bool drm_sched_entity_add_dependency_cb(struct drm_sched_entity *entity)
{
	struct drm_gpu_scheduler *sched = entity->sched;
	struct dma_fence * fence = entity->dependency;
	struct drm_sched_fence *s_fence;

	if (fence->context == entity->fence_context ||
            fence->context == entity->fence_context + 1) {
                /*
                 * Fence is a scheduled/finished fence from a job
                 * which belongs to the same entity, we can ignore
                 * fences from ourself
                 */
		dma_fence_put(entity->dependency);
		return false;
	}

	s_fence = to_drm_sched_fence(fence);
	if (s_fence && s_fence->sched == sched) {

		/*
		 * Fence is from the same scheduler, only need to wait for
		 * it to be scheduled
		 */
		fence = dma_fence_get(&s_fence->scheduled);
		dma_fence_put(entity->dependency);
		entity->dependency = fence;
		if (!dma_fence_add_callback(fence, &entity->cb,
					    drm_sched_entity_clear_dep))
			return true;

		/* Ignore it when it is already scheduled */
		dma_fence_put(fence);
		return false;
	}

	if (!dma_fence_add_callback(entity->dependency, &entity->cb,
				    drm_sched_entity_wakeup))
		return true;

	dma_fence_put(entity->dependency);
	return false;
}

static struct drm_sched_job *
drm_sched_entity_pop_job(struct drm_sched_entity *entity)
{
	struct drm_gpu_scheduler *sched = entity->sched;
	struct drm_sched_job *sched_job = to_drm_sched_job(
						spsc_queue_peek(&entity->job_queue));

	if (!sched_job)
		return NULL;

	while ((entity->dependency = sched->ops->dependency(sched_job, entity)))
		if (drm_sched_entity_add_dependency_cb(entity))
			return NULL;

	/* skip jobs from entity that marked guilty */
	if (entity->guilty && atomic_read(entity->guilty))
		dma_fence_set_error(&sched_job->s_fence->finished, -ECANCELED);

	dma_fence_put(entity->last_scheduled);
	entity->last_scheduled = dma_fence_get(&sched_job->s_fence->finished);

	spsc_queue_pop(&entity->job_queue);
	return sched_job;
}

/**
 * drm_sched_entity_push_job - Submit a job to the entity's job queue
 *
 * @sched_job: job to submit
 * @entity: scheduler entity
 *
 * Note: To guarantee that the order of insertion to queue matches
 * the job's fence sequence number this function should be
 * called with drm_sched_job_init under common lock.
 *
 * Returns 0 for success, negative error code otherwise.
 */
void drm_sched_entity_push_job(struct drm_sched_job *sched_job,
			       struct drm_sched_entity *entity)
{
	struct drm_gpu_scheduler *sched = sched_job->sched;
	bool first = false;

	trace_drm_sched_job(sched_job, entity);

	first = spsc_queue_push(&entity->job_queue, &sched_job->queue_node);

	/* first job wakes up scheduler */
	if (first) {
		/* Add the entity to the run queue */
		spin_lock(&entity->rq_lock);
		if (!entity->rq) {
			DRM_ERROR("Trying to push to a killed entity\n");
			spin_unlock(&entity->rq_lock);
			return;
		}
		drm_sched_rq_add_entity(entity->rq, entity);
		spin_unlock(&entity->rq_lock);
		drm_sched_wakeup(sched);
	}
}
EXPORT_SYMBOL(drm_sched_entity_push_job);

/* job_finish is called after hw fence signaled
 */
static void drm_sched_job_finish(struct work_struct *work)
{
	struct drm_sched_job *s_job = container_of(work, struct drm_sched_job,
						   finish_work);
	struct drm_gpu_scheduler *sched = s_job->sched;

	/* remove job from ring_mirror_list */
	spin_lock(&sched->job_list_lock);
	list_del_init(&s_job->node);
	if (sched->timeout != MAX_SCHEDULE_TIMEOUT) {
		struct drm_sched_job *next;

		spin_unlock(&sched->job_list_lock);
		cancel_delayed_work_sync(&s_job->work_tdr);
		spin_lock(&sched->job_list_lock);

		/* queue TDR for next job */
		next = list_first_entry_or_null(&sched->ring_mirror_list,
						struct drm_sched_job, node);

		if (next)
			schedule_delayed_work(&next->work_tdr, sched->timeout);
	}
	spin_unlock(&sched->job_list_lock);
	dma_fence_put(&s_job->s_fence->finished);
	sched->ops->free_job(s_job);
}

static void drm_sched_job_finish_cb(struct dma_fence *f,
				    struct dma_fence_cb *cb)
{
	struct drm_sched_job *job = container_of(cb, struct drm_sched_job,
						 finish_cb);
	schedule_work(&job->finish_work);
}

static void drm_sched_job_begin(struct drm_sched_job *s_job)
{
	struct drm_gpu_scheduler *sched = s_job->sched;

	dma_fence_add_callback(&s_job->s_fence->finished, &s_job->finish_cb,
			       drm_sched_job_finish_cb);

	spin_lock(&sched->job_list_lock);
	list_add_tail(&s_job->node, &sched->ring_mirror_list);
	if (sched->timeout != MAX_SCHEDULE_TIMEOUT &&
	    list_first_entry_or_null(&sched->ring_mirror_list,
				     struct drm_sched_job, node) == s_job)
		schedule_delayed_work(&s_job->work_tdr, sched->timeout);
	spin_unlock(&sched->job_list_lock);
}

static void drm_sched_job_timedout(struct work_struct *work)
{
	struct drm_sched_job *job = container_of(work, struct drm_sched_job,
						 work_tdr.work);

	job->sched->ops->timedout_job(job);
}

/**
 * drm_sched_hw_job_reset - stop the scheduler if it contains the bad job
 *
 * @sched: scheduler instance
 * @bad: bad scheduler job
 *
 */
void drm_sched_hw_job_reset(struct drm_gpu_scheduler *sched, struct drm_sched_job *bad)
{
	struct drm_sched_job *s_job;
	struct drm_sched_entity *entity, *tmp;
	int i;

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

	if (bad && bad->s_priority != DRM_SCHED_PRIORITY_KERNEL) {
		atomic_inc(&bad->karma);
		/* don't increase @bad's karma if it's from KERNEL RQ,
		 * becuase sometimes GPU hang would cause kernel jobs (like VM updating jobs)
		 * corrupt but keep in mind that kernel jobs always considered good.
		 */
		for (i = DRM_SCHED_PRIORITY_MIN; i < DRM_SCHED_PRIORITY_KERNEL; i++ ) {
			struct drm_sched_rq *rq = &sched->sched_rq[i];

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
EXPORT_SYMBOL(drm_sched_hw_job_reset);

/**
 * drm_sched_job_recovery - recover jobs after a reset
 *
 * @sched: scheduler instance
 *
 */
void drm_sched_job_recovery(struct drm_gpu_scheduler *sched)
{
	struct drm_sched_job *s_job, *tmp;
	bool found_guilty = false;
	int r;

	spin_lock(&sched->job_list_lock);
	s_job = list_first_entry_or_null(&sched->ring_mirror_list,
					 struct drm_sched_job, node);
	if (s_job && sched->timeout != MAX_SCHEDULE_TIMEOUT)
		schedule_delayed_work(&s_job->work_tdr, sched->timeout);

	list_for_each_entry_safe(s_job, tmp, &sched->ring_mirror_list, node) {
		struct drm_sched_fence *s_fence = s_job->s_fence;
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
						   drm_sched_process_job);
			if (r == -ENOENT)
				drm_sched_process_job(fence, &s_fence->cb);
			else if (r)
				DRM_ERROR("fence add callback failed (%d)\n",
					  r);
			dma_fence_put(fence);
		} else {
			drm_sched_process_job(NULL, &s_fence->cb);
		}
		spin_lock(&sched->job_list_lock);
	}
	spin_unlock(&sched->job_list_lock);
}
EXPORT_SYMBOL(drm_sched_job_recovery);

/**
 * drm_sched_job_init - init a scheduler job
 *
 * @job: scheduler job to init
 * @sched: scheduler instance
 * @entity: scheduler entity to use
 * @owner: job owner for debugging
 *
 * Refer to drm_sched_entity_push_job() documentation
 * for locking considerations.
 *
 * Returns 0 for success, negative error code otherwise.
 */
int drm_sched_job_init(struct drm_sched_job *job,
		       struct drm_gpu_scheduler *sched,
		       struct drm_sched_entity *entity,
		       void *owner)
{
	job->sched = sched;
	job->entity = entity;
	job->s_priority = entity->rq - sched->sched_rq;
	job->s_fence = drm_sched_fence_create(entity, owner);
	if (!job->s_fence)
		return -ENOMEM;
	job->id = atomic64_inc_return(&sched->job_id_count);

	INIT_WORK(&job->finish_work, drm_sched_job_finish);
	INIT_LIST_HEAD(&job->node);
	INIT_DELAYED_WORK(&job->work_tdr, drm_sched_job_timedout);

	return 0;
}
EXPORT_SYMBOL(drm_sched_job_init);

/**
 * drm_sched_ready - is the scheduler ready
 *
 * @sched: scheduler instance
 *
 * Return true if we can push more jobs to the hw, otherwise false.
 */
static bool drm_sched_ready(struct drm_gpu_scheduler *sched)
{
	return atomic_read(&sched->hw_rq_count) <
		sched->hw_submission_limit;
}

/**
 * drm_sched_wakeup - Wake up the scheduler when it is ready
 *
 * @sched: scheduler instance
 *
 */
static void drm_sched_wakeup(struct drm_gpu_scheduler *sched)
{
	if (drm_sched_ready(sched))
		wake_up_interruptible(&sched->wake_up_worker);
}

/**
 * drm_sched_select_entity - Select next entity to process
 *
 * @sched: scheduler instance
 *
 * Returns the entity to process or NULL if none are found.
 */
static struct drm_sched_entity *
drm_sched_select_entity(struct drm_gpu_scheduler *sched)
{
	struct drm_sched_entity *entity;
	int i;

	if (!drm_sched_ready(sched))
		return NULL;

	/* Kernel run queue has higher priority than normal run queue*/
	for (i = DRM_SCHED_PRIORITY_MAX - 1; i >= DRM_SCHED_PRIORITY_MIN; i--) {
		entity = drm_sched_rq_select_entity(&sched->sched_rq[i]);
		if (entity)
			break;
	}

	return entity;
}

/**
 * drm_sched_process_job - process a job
 *
 * @f: fence
 * @cb: fence callbacks
 *
 * Called after job has finished execution.
 */
static void drm_sched_process_job(struct dma_fence *f, struct dma_fence_cb *cb)
{
	struct drm_sched_fence *s_fence =
		container_of(cb, struct drm_sched_fence, cb);
	struct drm_gpu_scheduler *sched = s_fence->sched;

	dma_fence_get(&s_fence->finished);
	atomic_dec(&sched->hw_rq_count);
	drm_sched_fence_finished(s_fence);

	trace_drm_sched_process_job(s_fence);
	dma_fence_put(&s_fence->finished);
	wake_up_interruptible(&sched->wake_up_worker);
}

/**
 * drm_sched_blocked - check if the scheduler is blocked
 *
 * @sched: scheduler instance
 *
 * Returns true if blocked, otherwise false.
 */
static bool drm_sched_blocked(struct drm_gpu_scheduler *sched)
{
	if (kthread_should_park()) {
		kthread_parkme();
		return true;
	}

	return false;
}

/**
 * drm_sched_main - main scheduler thread
 *
 * @param: scheduler instance
 *
 * Returns 0.
 */
static int drm_sched_main(void *param)
{
	struct sched_param sparam = {.sched_priority = 1};
	struct drm_gpu_scheduler *sched = (struct drm_gpu_scheduler *)param;
	int r;

	sched_setscheduler(current, SCHED_FIFO, &sparam);

	while (!kthread_should_stop()) {
		struct drm_sched_entity *entity = NULL;
		struct drm_sched_fence *s_fence;
		struct drm_sched_job *sched_job;
		struct dma_fence *fence;

		wait_event_interruptible(sched->wake_up_worker,
					 (!drm_sched_blocked(sched) &&
					  (entity = drm_sched_select_entity(sched))) ||
					 kthread_should_stop());

		if (!entity)
			continue;

		sched_job = drm_sched_entity_pop_job(entity);
		if (!sched_job)
			continue;

		s_fence = sched_job->s_fence;

		atomic_inc(&sched->hw_rq_count);
		drm_sched_job_begin(sched_job);

		fence = sched->ops->run_job(sched_job);
		drm_sched_fence_scheduled(s_fence);

		if (fence) {
			s_fence->parent = dma_fence_get(fence);
			r = dma_fence_add_callback(fence, &s_fence->cb,
						   drm_sched_process_job);
			if (r == -ENOENT)
				drm_sched_process_job(fence, &s_fence->cb);
			else if (r)
				DRM_ERROR("fence add callback failed (%d)\n",
					  r);
			dma_fence_put(fence);
		} else {
			drm_sched_process_job(NULL, &s_fence->cb);
		}

		wake_up(&sched->job_scheduled);
	}
	return 0;
}

/**
 * drm_sched_init - Init a gpu scheduler instance
 *
 * @sched: scheduler instance
 * @ops: backend operations for this scheduler
 * @hw_submission: number of hw submissions that can be in flight
 * @hang_limit: number of times to allow a job to hang before dropping it
 * @timeout: timeout value in jiffies for the scheduler
 * @name: name used for debugging
 *
 * Return 0 on success, otherwise error code.
 */
int drm_sched_init(struct drm_gpu_scheduler *sched,
		   const struct drm_sched_backend_ops *ops,
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
	for (i = DRM_SCHED_PRIORITY_MIN; i < DRM_SCHED_PRIORITY_MAX; i++)
		drm_sched_rq_init(sched, &sched->sched_rq[i]);

	init_waitqueue_head(&sched->wake_up_worker);
	init_waitqueue_head(&sched->job_scheduled);
	INIT_LIST_HEAD(&sched->ring_mirror_list);
	spin_lock_init(&sched->job_list_lock);
	atomic_set(&sched->hw_rq_count, 0);
	atomic64_set(&sched->job_id_count, 0);

	/* Each scheduler will run on a seperate kernel thread */
	sched->thread = kthread_run(drm_sched_main, sched, sched->name);
	if (IS_ERR(sched->thread)) {
		DRM_ERROR("Failed to create scheduler for %s.\n", name);
		return PTR_ERR(sched->thread);
	}

	return 0;
}
EXPORT_SYMBOL(drm_sched_init);

/**
 * drm_sched_fini - Destroy a gpu scheduler
 *
 * @sched: scheduler instance
 *
 * Tears down and cleans up the scheduler.
 */
void drm_sched_fini(struct drm_gpu_scheduler *sched)
{
	if (sched->thread)
		kthread_stop(sched->thread);
}
EXPORT_SYMBOL(drm_sched_fini);
