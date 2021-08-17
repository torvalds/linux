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

#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/completion.h>

#include <drm/drm_print.h>
#include <drm/gpu_scheduler.h>

#include "gpu_scheduler_trace.h"

#define to_drm_sched_job(sched_job)		\
		container_of((sched_job), struct drm_sched_job, queue_node)

/**
 * drm_sched_entity_init - Init a context entity used by scheduler when
 * submit to HW ring.
 *
 * @entity: scheduler entity to init
 * @priority: priority of the entity
 * @sched_list: the list of drm scheds on which jobs from this
 *           entity can be submitted
 * @num_sched_list: number of drm sched in sched_list
 * @guilty: atomic_t set to 1 when a job on this queue
 *          is found to be guilty causing a timeout
 *
 * Note: the sched_list should have at least one element to schedule
 *       the entity
 *
 * Returns 0 on success or a negative error code on failure.
 */
int drm_sched_entity_init(struct drm_sched_entity *entity,
			  enum drm_sched_priority priority,
			  struct drm_gpu_scheduler **sched_list,
			  unsigned int num_sched_list,
			  atomic_t *guilty)
{
	if (!(entity && sched_list && (num_sched_list == 0 || sched_list[0])))
		return -EINVAL;

	memset(entity, 0, sizeof(struct drm_sched_entity));
	INIT_LIST_HEAD(&entity->list);
	entity->rq = NULL;
	entity->guilty = guilty;
	entity->num_sched_list = num_sched_list;
	entity->priority = priority;
	entity->sched_list = num_sched_list > 1 ? sched_list : NULL;
	entity->last_scheduled = NULL;

	if(num_sched_list)
		entity->rq = &sched_list[0]->sched_rq[entity->priority];

	init_completion(&entity->entity_idle);

	/* We start in an idle state. */
	complete(&entity->entity_idle);

	spin_lock_init(&entity->rq_lock);
	spsc_queue_init(&entity->job_queue);

	atomic_set(&entity->fence_seq, 0);
	entity->fence_context = dma_fence_context_alloc(2);

	return 0;
}
EXPORT_SYMBOL(drm_sched_entity_init);

/**
 * drm_sched_entity_modify_sched - Modify sched of an entity
 * @entity: scheduler entity to init
 * @sched_list: the list of new drm scheds which will replace
 *		 existing entity->sched_list
 * @num_sched_list: number of drm sched in sched_list
 */
void drm_sched_entity_modify_sched(struct drm_sched_entity *entity,
				    struct drm_gpu_scheduler **sched_list,
				    unsigned int num_sched_list)
{
	WARN_ON(!num_sched_list || !sched_list);

	entity->sched_list = sched_list;
	entity->num_sched_list = num_sched_list;
}
EXPORT_SYMBOL(drm_sched_entity_modify_sched);

/**
 * drm_sched_entity_is_idle - Check if entity is idle
 *
 * @entity: scheduler entity
 *
 * Returns true if the entity does not have any unscheduled jobs.
 */
static bool drm_sched_entity_is_idle(struct drm_sched_entity *entity)
{
	rmb(); /* for list_empty to work without lock */

	if (list_empty(&entity->list) ||
	    spsc_queue_count(&entity->job_queue) == 0 ||
	    entity->stopped)
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
bool drm_sched_entity_is_ready(struct drm_sched_entity *entity)
{
	if (spsc_queue_peek(&entity->job_queue) == NULL)
		return false;

	if (READ_ONCE(entity->dependency))
		return false;

	return true;
}

/**
 * drm_sched_entity_flush - Flush a context entity
 *
 * @entity: scheduler entity
 * @timeout: time to wait in for Q to become empty in jiffies.
 *
 * Splitting drm_sched_entity_fini() into two functions, The first one does the
 * waiting, removes the entity from the runqueue and returns an error when the
 * process was killed.
 *
 * Returns the remaining time in jiffies left from the input timeout
 */
long drm_sched_entity_flush(struct drm_sched_entity *entity, long timeout)
{
	struct drm_gpu_scheduler *sched;
	struct task_struct *last_user;
	long ret = timeout;

	if (!entity->rq)
		return 0;

	sched = entity->rq->sched;
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
	} else {
		wait_event_killable(sched->job_scheduled,
				    drm_sched_entity_is_idle(entity));
	}

	/* For killed process disable any more IBs enqueue right now */
	last_user = cmpxchg(&entity->last_user, current->group_leader, NULL);
	if ((!last_user || last_user == current->group_leader) &&
	    (current->flags & PF_EXITING) && (current->exit_code == SIGKILL)) {
		spin_lock(&entity->rq_lock);
		entity->stopped = true;
		drm_sched_rq_remove_entity(entity->rq, entity);
		spin_unlock(&entity->rq_lock);
	}

	return ret;
}
EXPORT_SYMBOL(drm_sched_entity_flush);

/**
 * drm_sched_entity_kill_jobs_cb - helper for drm_sched_entity_kill_jobs
 *
 * @f: signaled fence
 * @cb: our callback structure
 *
 * Signal the scheduler finished fence when the entity in question is killed.
 */
static void drm_sched_entity_kill_jobs_cb(struct dma_fence *f,
					  struct dma_fence_cb *cb)
{
	struct drm_sched_job *job = container_of(cb, struct drm_sched_job,
						 finish_cb);

	drm_sched_fence_finished(job->s_fence);
	WARN_ON(job->s_fence->parent);
	job->sched->ops->free_job(job);
}

/**
 * drm_sched_entity_kill_jobs - Make sure all remaining jobs are killed
 *
 * @entity: entity which is cleaned up
 *
 * Makes sure that all remaining jobs in an entity are killed before it is
 * destroyed.
 */
static void drm_sched_entity_kill_jobs(struct drm_sched_entity *entity)
{
	struct drm_sched_job *job;
	struct dma_fence *f;
	int r;

	while ((job = to_drm_sched_job(spsc_queue_pop(&entity->job_queue)))) {
		struct drm_sched_fence *s_fence = job->s_fence;

		/* Wait for all dependencies to avoid data corruptions */
		while ((f = job->sched->ops->dependency(job, entity)))
			dma_fence_wait(f, false);

		drm_sched_fence_scheduled(s_fence);
		dma_fence_set_error(&s_fence->finished, -ESRCH);

		/*
		 * When pipe is hanged by older entity, new entity might
		 * not even have chance to submit it's first job to HW
		 * and so entity->last_scheduled will remain NULL
		 */
		if (!entity->last_scheduled) {
			drm_sched_entity_kill_jobs_cb(NULL, &job->finish_cb);
			continue;
		}

		r = dma_fence_add_callback(entity->last_scheduled,
					   &job->finish_cb,
					   drm_sched_entity_kill_jobs_cb);
		if (r == -ENOENT)
			drm_sched_entity_kill_jobs_cb(NULL, &job->finish_cb);
		else if (r)
			DRM_ERROR("fence add callback failed (%d)\n", r);
	}
}

/**
 * drm_sched_entity_fini - Destroy a context entity
 *
 * @entity: scheduler entity
 *
 * This should be called after @drm_sched_entity_do_release. It goes over the
 * entity and signals all jobs with an error code if the process was killed.
 *
 */
void drm_sched_entity_fini(struct drm_sched_entity *entity)
{
	struct drm_gpu_scheduler *sched = NULL;

	if (entity->rq) {
		sched = entity->rq->sched;
		drm_sched_rq_remove_entity(entity->rq, entity);
	}

	/* Consumption of existing IBs wasn't completed. Forcefully
	 * remove them here.
	 */
	if (spsc_queue_count(&entity->job_queue)) {
		if (sched) {
			/*
			 * Wait for thread to idle to make sure it isn't processing
			 * this entity.
			 */
			wait_for_completion(&entity->entity_idle);

		}
		if (entity->dependency) {
			dma_fence_remove_callback(entity->dependency,
						  &entity->cb);
			dma_fence_put(entity->dependency);
			entity->dependency = NULL;
		}

		drm_sched_entity_kill_jobs(entity);
	}

	dma_fence_put(entity->last_scheduled);
	entity->last_scheduled = NULL;
}
EXPORT_SYMBOL(drm_sched_entity_fini);

/**
 * drm_sched_entity_destroy - Destroy a context entity
 *
 * @entity: scheduler entity
 *
 * Calls drm_sched_entity_do_release() and drm_sched_entity_cleanup()
 */
void drm_sched_entity_destroy(struct drm_sched_entity *entity)
{
	drm_sched_entity_flush(entity, MAX_WAIT_SCHED_ENTITY_Q_EMPTY);
	drm_sched_entity_fini(entity);
}
EXPORT_SYMBOL(drm_sched_entity_destroy);

/*
 * drm_sched_entity_clear_dep - callback to clear the entities dependency
 */
static void drm_sched_entity_clear_dep(struct dma_fence *f,
				       struct dma_fence_cb *cb)
{
	struct drm_sched_entity *entity =
		container_of(cb, struct drm_sched_entity, cb);

	entity->dependency = NULL;
	dma_fence_put(f);
}

/*
 * drm_sched_entity_clear_dep - callback to clear the entities dependency and
 * wake up scheduler
 */
static void drm_sched_entity_wakeup(struct dma_fence *f,
				    struct dma_fence_cb *cb)
{
	struct drm_sched_entity *entity =
		container_of(cb, struct drm_sched_entity, cb);

	drm_sched_entity_clear_dep(f, cb);
	drm_sched_wakeup(entity->rq->sched);
}

/**
 * drm_sched_entity_set_priority - Sets priority of the entity
 *
 * @entity: scheduler entity
 * @priority: scheduler priority
 *
 * Update the priority of runqueus used for the entity.
 */
void drm_sched_entity_set_priority(struct drm_sched_entity *entity,
				   enum drm_sched_priority priority)
{
	spin_lock(&entity->rq_lock);
	entity->priority = priority;
	spin_unlock(&entity->rq_lock);
}
EXPORT_SYMBOL(drm_sched_entity_set_priority);

/**
 * drm_sched_entity_add_dependency_cb - add callback for the entities dependency
 *
 * @entity: entity with dependency
 *
 * Add a callback to the current dependency of the entity to wake up the
 * scheduler when the entity becomes available.
 */
static bool drm_sched_entity_add_dependency_cb(struct drm_sched_entity *entity)
{
	struct drm_gpu_scheduler *sched = entity->rq->sched;
	struct dma_fence *fence = entity->dependency;
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

/**
 * drm_sched_entity_pop_job - get a ready to be scheduled job from the entity
 *
 * @entity: entity to get the job from
 *
 * Process all dependencies and try to get one job from the entities queue.
 */
struct drm_sched_job *drm_sched_entity_pop_job(struct drm_sched_entity *entity)
{
	struct drm_gpu_scheduler *sched = entity->rq->sched;
	struct drm_sched_job *sched_job;

	sched_job = to_drm_sched_job(spsc_queue_peek(&entity->job_queue));
	if (!sched_job)
		return NULL;

	while ((entity->dependency =
			sched->ops->dependency(sched_job, entity))) {
		trace_drm_sched_job_wait_dep(sched_job, entity->dependency);

		if (drm_sched_entity_add_dependency_cb(entity))
			return NULL;
	}

	/* skip jobs from entity that marked guilty */
	if (entity->guilty && atomic_read(entity->guilty))
		dma_fence_set_error(&sched_job->s_fence->finished, -ECANCELED);

	dma_fence_put(entity->last_scheduled);
	entity->last_scheduled = dma_fence_get(&sched_job->s_fence->finished);

	spsc_queue_pop(&entity->job_queue);
	return sched_job;
}

/**
 * drm_sched_entity_select_rq - select a new rq for the entity
 *
 * @entity: scheduler entity
 *
 * Check all prerequisites and select a new rq for the entity for load
 * balancing.
 */
void drm_sched_entity_select_rq(struct drm_sched_entity *entity)
{
	struct dma_fence *fence;
	struct drm_gpu_scheduler *sched;
	struct drm_sched_rq *rq;

	if (spsc_queue_count(&entity->job_queue) || !entity->sched_list)
		return;

	fence = READ_ONCE(entity->last_scheduled);
	if (fence && !dma_fence_is_signaled(fence))
		return;

	spin_lock(&entity->rq_lock);
	sched = drm_sched_pick_best(entity->sched_list, entity->num_sched_list);
	rq = sched ? &sched->sched_rq[entity->priority] : NULL;
	if (rq != entity->rq) {
		drm_sched_rq_remove_entity(entity->rq, entity);
		entity->rq = rq;
	}
	spin_unlock(&entity->rq_lock);

	if (entity->num_sched_list == 1)
		entity->sched_list = NULL;
}

/**
 * drm_sched_entity_push_job - Submit a job to the entity's job queue
 *
 * @sched_job: job to submit
 * @entity: scheduler entity
 *
 * Note: To guarantee that the order of insertion to queue matches the job's
 * fence sequence number this function should be called with drm_sched_job_arm()
 * under common lock.
 *
 * Returns 0 for success, negative error code otherwise.
 */
void drm_sched_entity_push_job(struct drm_sched_job *sched_job,
			       struct drm_sched_entity *entity)
{
	bool first;

	trace_drm_sched_job(sched_job, entity);
	atomic_inc(entity->rq->sched->score);
	WRITE_ONCE(entity->last_user, current->group_leader);
	first = spsc_queue_push(&entity->job_queue, &sched_job->queue_node);

	/* first job wakes up scheduler */
	if (first) {
		/* Add the entity to the run queue */
		spin_lock(&entity->rq_lock);
		if (entity->stopped) {
			spin_unlock(&entity->rq_lock);

			DRM_ERROR("Trying to push to a killed entity\n");
			return;
		}
		drm_sched_rq_add_entity(entity->rq, entity);
		spin_unlock(&entity->rq_lock);
		drm_sched_wakeup(entity->rq->sched);
	}
}
EXPORT_SYMBOL(drm_sched_entity_push_job);
