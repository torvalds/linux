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
void drm_sched_rq_add_entity(struct drm_sched_rq *rq,
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
void drm_sched_rq_remove_entity(struct drm_sched_rq *rq,
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
	struct drm_gpu_scheduler *sched = entity->rq->sched;
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

/**
 * drm_sched_start_timeout - start timeout for reset worker
 *
 * @sched: scheduler instance to start the worker for
 *
 * Start the timeout for the given scheduler.
 */
static void drm_sched_start_timeout(struct drm_gpu_scheduler *sched)
{
	if (sched->timeout != MAX_SCHEDULE_TIMEOUT &&
	    !list_empty(&sched->ring_mirror_list))
		schedule_delayed_work(&sched->work_tdr, sched->timeout);
}

/**
 * drm_sched_fault - immediately start timeout handler
 *
 * @sched: scheduler where the timeout handling should be started.
 *
 * Start timeout handling immediately when the driver detects a hardware fault.
 */
void drm_sched_fault(struct drm_gpu_scheduler *sched)
{
	mod_delayed_work(system_wq, &sched->work_tdr, 0);
}
EXPORT_SYMBOL(drm_sched_fault);

/**
 * drm_sched_suspend_timeout - Suspend scheduler job timeout
 *
 * @sched: scheduler instance for which to suspend the timeout
 *
 * Suspend the delayed work timeout for the scheduler. This is done by
 * modifying the delayed work timeout to an arbitrary large value,
 * MAX_SCHEDULE_TIMEOUT in this case. Note that this function can be
 * called from an IRQ context.
 *
 * Returns the timeout remaining
 *
 */
unsigned long drm_sched_suspend_timeout(struct drm_gpu_scheduler *sched)
{
	unsigned long sched_timeout, now = jiffies;

	sched_timeout = sched->work_tdr.timer.expires;

	/*
	 * Modify the timeout to an arbitrarily large value. This also prevents
	 * the timeout to be restarted when new submissions arrive
	 */
	if (mod_delayed_work(system_wq, &sched->work_tdr, MAX_SCHEDULE_TIMEOUT)
			&& time_after(sched_timeout, now))
		return sched_timeout - now;
	else
		return sched->timeout;
}
EXPORT_SYMBOL(drm_sched_suspend_timeout);

/**
 * drm_sched_resume_timeout - Resume scheduler job timeout
 *
 * @sched: scheduler instance for which to resume the timeout
 * @remaining: remaining timeout
 *
 * Resume the delayed work timeout for the scheduler. Note that
 * this function can be called from an IRQ context.
 */
void drm_sched_resume_timeout(struct drm_gpu_scheduler *sched,
		unsigned long remaining)
{
	unsigned long flags;

	spin_lock_irqsave(&sched->job_list_lock, flags);

	if (list_empty(&sched->ring_mirror_list))
		cancel_delayed_work(&sched->work_tdr);
	else
		mod_delayed_work(system_wq, &sched->work_tdr, remaining);

	spin_unlock_irqrestore(&sched->job_list_lock, flags);
}
EXPORT_SYMBOL(drm_sched_resume_timeout);

/* job_finish is called after hw fence signaled
 */
static void drm_sched_job_finish(struct work_struct *work)
{
	struct drm_sched_job *s_job = container_of(work, struct drm_sched_job,
						   finish_work);
	struct drm_gpu_scheduler *sched = s_job->sched;
	unsigned long flags;

	/*
	 * Canceling the timeout without removing our job from the ring mirror
	 * list is safe, as we will only end up in this worker if our jobs
	 * finished fence has been signaled. So even if some another worker
	 * manages to find this job as the next job in the list, the fence
	 * signaled check below will prevent the timeout to be restarted.
	 */
	cancel_delayed_work_sync(&sched->work_tdr);

	spin_lock_irqsave(&sched->job_list_lock, flags);
	/* queue TDR for next job */
	drm_sched_start_timeout(sched);
	spin_unlock_irqrestore(&sched->job_list_lock, flags);

	sched->ops->free_job(s_job);
}

static void drm_sched_job_begin(struct drm_sched_job *s_job)
{
	struct drm_gpu_scheduler *sched = s_job->sched;
	unsigned long flags;

	spin_lock_irqsave(&sched->job_list_lock, flags);
	list_add_tail(&s_job->node, &sched->ring_mirror_list);
	drm_sched_start_timeout(sched);
	spin_unlock_irqrestore(&sched->job_list_lock, flags);
}

static void drm_sched_job_timedout(struct work_struct *work)
{
	struct drm_gpu_scheduler *sched;
	struct drm_sched_job *job;
	unsigned long flags;

	sched = container_of(work, struct drm_gpu_scheduler, work_tdr.work);
	job = list_first_entry_or_null(&sched->ring_mirror_list,
				       struct drm_sched_job, node);

	if (job)
		job->sched->ops->timedout_job(job);

	spin_lock_irqsave(&sched->job_list_lock, flags);
	drm_sched_start_timeout(sched);
	spin_unlock_irqrestore(&sched->job_list_lock, flags);
}

 /**
  * drm_sched_increase_karma - Update sched_entity guilty flag
  *
  * @bad: The job guilty of time out
  *
  * Increment on every hang caused by the 'bad' job. If this exceeds the hang
  * limit of the scheduler then the respective sched entity is marked guilty and
  * jobs from it will not be scheduled further
  */
void drm_sched_increase_karma(struct drm_sched_job *bad)
{
	int i;
	struct drm_sched_entity *tmp;
	struct drm_sched_entity *entity;
	struct drm_gpu_scheduler *sched = bad->sched;

	/* don't increase @bad's karma if it's from KERNEL RQ,
	 * because sometimes GPU hang would cause kernel jobs (like VM updating jobs)
	 * corrupt but keep in mind that kernel jobs always considered good.
	 */
	if (bad->s_priority != DRM_SCHED_PRIORITY_KERNEL) {
		atomic_inc(&bad->karma);
		for (i = DRM_SCHED_PRIORITY_MIN; i < DRM_SCHED_PRIORITY_KERNEL;
		     i++) {
			struct drm_sched_rq *rq = &sched->sched_rq[i];

			spin_lock(&rq->lock);
			list_for_each_entry_safe(entity, tmp, &rq->entities, list) {
				if (bad->s_fence->scheduled.context ==
				    entity->fence_context) {
					if (atomic_read(&bad->karma) >
					    bad->sched->hang_limit)
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
EXPORT_SYMBOL(drm_sched_increase_karma);

/**
 * drm_sched_stop - stop the scheduler
 *
 * @sched: scheduler instance
 *
 */
void drm_sched_stop(struct drm_gpu_scheduler *sched)
{
	struct drm_sched_job *s_job;
	unsigned long flags;
	struct dma_fence *last_fence =  NULL;

	kthread_park(sched->thread);

	/*
	 * Verify all the signaled jobs in mirror list are removed from the ring
	 * by waiting for the latest job to enter the list. This should insure that
	 * also all the previous jobs that were in flight also already singaled
	 * and removed from the list.
	 */
	spin_lock_irqsave(&sched->job_list_lock, flags);
	list_for_each_entry_reverse(s_job, &sched->ring_mirror_list, node) {
		if (s_job->s_fence->parent &&
		    dma_fence_remove_callback(s_job->s_fence->parent,
					      &s_job->cb)) {
			dma_fence_put(s_job->s_fence->parent);
			s_job->s_fence->parent = NULL;
			atomic_dec(&sched->hw_rq_count);
		} else {
			 last_fence = dma_fence_get(&s_job->s_fence->finished);
			 break;
		}
	}
	spin_unlock_irqrestore(&sched->job_list_lock, flags);

	if (last_fence) {
		dma_fence_wait(last_fence, false);
		dma_fence_put(last_fence);
	}
}

EXPORT_SYMBOL(drm_sched_stop);

/**
 * drm_sched_job_recovery - recover jobs after a reset
 *
 * @sched: scheduler instance
 *
 */
void drm_sched_start(struct drm_gpu_scheduler *sched, bool full_recovery)
{
	struct drm_sched_job *s_job, *tmp;
	int r;

	if (!full_recovery)
		goto unpark;

	/*
	 * Locking the list is not required here as the sched thread is parked
	 * so no new jobs are being pushed in to HW and in drm_sched_stop we
	 * flushed all the jobs who were still in mirror list but who already
	 * signaled and removed them self from the list. Also concurrent
	 * GPU recovers can't run in parallel.
	 */
	list_for_each_entry_safe(s_job, tmp, &sched->ring_mirror_list, node) {
		struct dma_fence *fence = s_job->s_fence->parent;

		if (fence) {
			r = dma_fence_add_callback(fence, &s_job->cb,
						   drm_sched_process_job);
			if (r == -ENOENT)
				drm_sched_process_job(fence, &s_job->cb);
			else if (r)
				DRM_ERROR("fence add callback failed (%d)\n",
					  r);
		} else
			drm_sched_process_job(NULL, &s_job->cb);
	}

	drm_sched_start_timeout(sched);

unpark:
	kthread_unpark(sched->thread);
}
EXPORT_SYMBOL(drm_sched_start);

/**
 * drm_sched_resubmit_jobs - helper to relunch job from mirror ring list
 *
 * @sched: scheduler instance
 *
 */
void drm_sched_resubmit_jobs(struct drm_gpu_scheduler *sched)
{
	struct drm_sched_job *s_job, *tmp;
	uint64_t guilty_context;
	bool found_guilty = false;

	/*TODO DO we need spinlock here ? */
	list_for_each_entry_safe(s_job, tmp, &sched->ring_mirror_list, node) {
		struct drm_sched_fence *s_fence = s_job->s_fence;

		if (!found_guilty && atomic_read(&s_job->karma) > sched->hang_limit) {
			found_guilty = true;
			guilty_context = s_job->s_fence->scheduled.context;
		}

		if (found_guilty && s_job->s_fence->scheduled.context == guilty_context)
			dma_fence_set_error(&s_fence->finished, -ECANCELED);

		s_job->s_fence->parent = sched->ops->run_job(s_job);
		atomic_inc(&sched->hw_rq_count);
	}
}
EXPORT_SYMBOL(drm_sched_resubmit_jobs);

/**
 * drm_sched_job_init - init a scheduler job
 *
 * @job: scheduler job to init
 * @entity: scheduler entity to use
 * @owner: job owner for debugging
 *
 * Refer to drm_sched_entity_push_job() documentation
 * for locking considerations.
 *
 * Returns 0 for success, negative error code otherwise.
 */
int drm_sched_job_init(struct drm_sched_job *job,
		       struct drm_sched_entity *entity,
		       void *owner)
{
	struct drm_gpu_scheduler *sched;

	drm_sched_entity_select_rq(entity);
	if (!entity->rq)
		return -ENOENT;

	sched = entity->rq->sched;

	job->sched = sched;
	job->entity = entity;
	job->s_priority = entity->rq - sched->sched_rq;
	job->s_fence = drm_sched_fence_create(entity, owner);
	if (!job->s_fence)
		return -ENOMEM;
	job->id = atomic64_inc_return(&sched->job_id_count);

	INIT_WORK(&job->finish_work, drm_sched_job_finish);
	INIT_LIST_HEAD(&job->node);

	return 0;
}
EXPORT_SYMBOL(drm_sched_job_init);

/**
 * drm_sched_job_cleanup - clean up scheduler job resources
 *
 * @job: scheduler job to clean up
 */
void drm_sched_job_cleanup(struct drm_sched_job *job)
{
	dma_fence_put(&job->s_fence->finished);
	job->s_fence = NULL;
}
EXPORT_SYMBOL(drm_sched_job_cleanup);

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
void drm_sched_wakeup(struct drm_gpu_scheduler *sched)
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
	struct drm_sched_job *s_job = container_of(cb, struct drm_sched_job, cb);
	struct drm_sched_fence *s_fence = s_job->s_fence;
	struct drm_gpu_scheduler *sched = s_fence->sched;
	unsigned long flags;

	cancel_delayed_work(&sched->work_tdr);

	atomic_dec(&sched->hw_rq_count);
	atomic_dec(&sched->num_jobs);

	spin_lock_irqsave(&sched->job_list_lock, flags);
	/* remove job from ring_mirror_list */
	list_del_init(&s_job->node);
	spin_unlock_irqrestore(&sched->job_list_lock, flags);

	drm_sched_fence_finished(s_fence);

	trace_drm_sched_process_job(s_fence);
	wake_up_interruptible(&sched->wake_up_worker);

	schedule_work(&s_job->finish_work);
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
			r = dma_fence_add_callback(fence, &sched_job->cb,
						   drm_sched_process_job);
			if (r == -ENOENT)
				drm_sched_process_job(fence, &sched_job->cb);
			else if (r)
				DRM_ERROR("fence add callback failed (%d)\n",
					  r);
			dma_fence_put(fence);
		} else
			drm_sched_process_job(NULL, &sched_job->cb);

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
	int i, ret;
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
	INIT_DELAYED_WORK(&sched->work_tdr, drm_sched_job_timedout);
	atomic_set(&sched->num_jobs, 0);
	atomic64_set(&sched->job_id_count, 0);

	/* Each scheduler will run on a seperate kernel thread */
	sched->thread = kthread_run(drm_sched_main, sched, sched->name);
	if (IS_ERR(sched->thread)) {
		ret = PTR_ERR(sched->thread);
		sched->thread = NULL;
		DRM_ERROR("Failed to create scheduler for %s.\n", name);
		return ret;
	}

	sched->ready = true;
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

	sched->ready = false;
}
EXPORT_SYMBOL(drm_sched_fini);
