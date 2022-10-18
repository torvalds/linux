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
#include <linux/completion.h>
#include <linux/dma-resv.h>
#include <uapi/linux/sched/types.h>

#include <drm/drm_print.h>
#include <drm/drm_gem.h>
#include <drm/gpu_scheduler.h>
#include <drm/spsc_queue.h>

#define CREATE_TRACE_POINTS
#include "gpu_scheduler_trace.h"

#define to_drm_sched_job(sched_job)		\
		container_of((sched_job), struct drm_sched_job, queue_node)

int drm_sched_policy = DRM_SCHED_POLICY_RR;

/**
 * DOC: sched_policy (int)
 * Used to override default entities scheduling policy in a run queue.
 */
MODULE_PARM_DESC(sched_policy, "Specify schedule policy for entities on a runqueue, " __stringify(DRM_SCHED_POLICY_RR) " = Round Robin (default), " __stringify(DRM_SCHED_POLICY_FIFO) " = use FIFO.");
module_param_named(sched_policy, drm_sched_policy, int, 0444);

static __always_inline bool drm_sched_entity_compare_before(struct rb_node *a,
							    const struct rb_node *b)
{
	struct drm_sched_entity *ent_a =  rb_entry((a), struct drm_sched_entity, rb_tree_node);
	struct drm_sched_entity *ent_b =  rb_entry((b), struct drm_sched_entity, rb_tree_node);

	return ktime_before(ent_a->oldest_job_waiting, ent_b->oldest_job_waiting);
}

static inline void drm_sched_rq_remove_fifo_locked(struct drm_sched_entity *entity)
{
	struct drm_sched_rq *rq = entity->rq;

	if (!RB_EMPTY_NODE(&entity->rb_tree_node)) {
		rb_erase_cached(&entity->rb_tree_node, &rq->rb_tree_root);
		RB_CLEAR_NODE(&entity->rb_tree_node);
	}
}

void drm_sched_rq_update_fifo(struct drm_sched_entity *entity, ktime_t ts)
{
	/*
	 * Both locks need to be grabbed, one to protect from entity->rq change
	 * for entity from within concurrent drm_sched_entity_select_rq and the
	 * other to update the rb tree structure.
	 */
	spin_lock(&entity->rq_lock);
	spin_lock(&entity->rq->lock);

	drm_sched_rq_remove_fifo_locked(entity);

	entity->oldest_job_waiting = ts;

	rb_add_cached(&entity->rb_tree_node, &entity->rq->rb_tree_root,
		      drm_sched_entity_compare_before);

	spin_unlock(&entity->rq->lock);
	spin_unlock(&entity->rq_lock);
}

/**
 * drm_sched_rq_init - initialize a given run queue struct
 *
 * @sched: scheduler instance to associate with this run queue
 * @rq: scheduler run queue
 *
 * Initializes a scheduler runqueue.
 */
static void drm_sched_rq_init(struct drm_gpu_scheduler *sched,
			      struct drm_sched_rq *rq)
{
	spin_lock_init(&rq->lock);
	INIT_LIST_HEAD(&rq->entities);
	rq->rb_tree_root = RB_ROOT_CACHED;
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

	atomic_inc(rq->sched->score);
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

	atomic_dec(rq->sched->score);
	list_del_init(&entity->list);

	if (rq->current_entity == entity)
		rq->current_entity = NULL;

	if (drm_sched_policy == DRM_SCHED_POLICY_FIFO)
		drm_sched_rq_remove_fifo_locked(entity);

	spin_unlock(&rq->lock);
}

/**
 * drm_sched_rq_select_entity_rr - Select an entity which could provide a job to run
 *
 * @rq: scheduler run queue to check.
 *
 * Try to find a ready entity, returns NULL if none found.
 */
static struct drm_sched_entity *
drm_sched_rq_select_entity_rr(struct drm_sched_rq *rq)
{
	struct drm_sched_entity *entity;

	spin_lock(&rq->lock);

	entity = rq->current_entity;
	if (entity) {
		list_for_each_entry_continue(entity, &rq->entities, list) {
			if (drm_sched_entity_is_ready(entity)) {
				rq->current_entity = entity;
				reinit_completion(&entity->entity_idle);
				spin_unlock(&rq->lock);
				return entity;
			}
		}
	}

	list_for_each_entry(entity, &rq->entities, list) {

		if (drm_sched_entity_is_ready(entity)) {
			rq->current_entity = entity;
			reinit_completion(&entity->entity_idle);
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
 * drm_sched_rq_select_entity_fifo - Select an entity which provides a job to run
 *
 * @rq: scheduler run queue to check.
 *
 * Find oldest waiting ready entity, returns NULL if none found.
 */
static struct drm_sched_entity *
drm_sched_rq_select_entity_fifo(struct drm_sched_rq *rq)
{
	struct rb_node *rb;

	spin_lock(&rq->lock);
	for (rb = rb_first_cached(&rq->rb_tree_root); rb; rb = rb_next(rb)) {
		struct drm_sched_entity *entity;

		entity = rb_entry(rb, struct drm_sched_entity, rb_tree_node);
		if (drm_sched_entity_is_ready(entity)) {
			rq->current_entity = entity;
			reinit_completion(&entity->entity_idle);
			break;
		}
	}
	spin_unlock(&rq->lock);

	return rb ? rb_entry(rb, struct drm_sched_entity, rb_tree_node) : NULL;
}

/**
 * drm_sched_job_done - complete a job
 * @s_job: pointer to the job which is done
 *
 * Finish the job's fence and wake up the worker thread.
 */
static void drm_sched_job_done(struct drm_sched_job *s_job)
{
	struct drm_sched_fence *s_fence = s_job->s_fence;
	struct drm_gpu_scheduler *sched = s_fence->sched;

	atomic_dec(&sched->hw_rq_count);
	atomic_dec(sched->score);

	trace_drm_sched_process_job(s_fence);

	dma_fence_get(&s_fence->finished);
	drm_sched_fence_finished(s_fence);
	dma_fence_put(&s_fence->finished);
	wake_up_interruptible(&sched->wake_up_worker);
}

/**
 * drm_sched_job_done_cb - the callback for a done job
 * @f: fence
 * @cb: fence callbacks
 */
static void drm_sched_job_done_cb(struct dma_fence *f, struct dma_fence_cb *cb)
{
	struct drm_sched_job *s_job = container_of(cb, struct drm_sched_job, cb);

	drm_sched_job_done(s_job);
}

/**
 * drm_sched_dependency_optimized - test if the dependency can be optimized
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
	    !list_empty(&sched->pending_list))
		queue_delayed_work(sched->timeout_wq, &sched->work_tdr, sched->timeout);
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
	mod_delayed_work(sched->timeout_wq, &sched->work_tdr, 0);
}
EXPORT_SYMBOL(drm_sched_fault);

/**
 * drm_sched_suspend_timeout - Suspend scheduler job timeout
 *
 * @sched: scheduler instance for which to suspend the timeout
 *
 * Suspend the delayed work timeout for the scheduler. This is done by
 * modifying the delayed work timeout to an arbitrary large value,
 * MAX_SCHEDULE_TIMEOUT in this case.
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
	if (mod_delayed_work(sched->timeout_wq, &sched->work_tdr, MAX_SCHEDULE_TIMEOUT)
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
 * Resume the delayed work timeout for the scheduler.
 */
void drm_sched_resume_timeout(struct drm_gpu_scheduler *sched,
		unsigned long remaining)
{
	spin_lock(&sched->job_list_lock);

	if (list_empty(&sched->pending_list))
		cancel_delayed_work(&sched->work_tdr);
	else
		mod_delayed_work(sched->timeout_wq, &sched->work_tdr, remaining);

	spin_unlock(&sched->job_list_lock);
}
EXPORT_SYMBOL(drm_sched_resume_timeout);

static void drm_sched_job_begin(struct drm_sched_job *s_job)
{
	struct drm_gpu_scheduler *sched = s_job->sched;

	spin_lock(&sched->job_list_lock);
	list_add_tail(&s_job->list, &sched->pending_list);
	drm_sched_start_timeout(sched);
	spin_unlock(&sched->job_list_lock);
}

static void drm_sched_job_timedout(struct work_struct *work)
{
	struct drm_gpu_scheduler *sched;
	struct drm_sched_job *job;
	enum drm_gpu_sched_stat status = DRM_GPU_SCHED_STAT_NOMINAL;

	sched = container_of(work, struct drm_gpu_scheduler, work_tdr.work);

	/* Protects against concurrent deletion in drm_sched_get_cleanup_job */
	spin_lock(&sched->job_list_lock);
	job = list_first_entry_or_null(&sched->pending_list,
				       struct drm_sched_job, list);

	if (job) {
		/*
		 * Remove the bad job so it cannot be freed by concurrent
		 * drm_sched_cleanup_jobs. It will be reinserted back after sched->thread
		 * is parked at which point it's safe.
		 */
		list_del_init(&job->list);
		spin_unlock(&sched->job_list_lock);

		status = job->sched->ops->timedout_job(job);

		/*
		 * Guilty job did complete and hence needs to be manually removed
		 * See drm_sched_stop doc.
		 */
		if (sched->free_guilty) {
			job->sched->ops->free_job(job);
			sched->free_guilty = false;
		}
	} else {
		spin_unlock(&sched->job_list_lock);
	}

	if (status != DRM_GPU_SCHED_STAT_ENODEV) {
		spin_lock(&sched->job_list_lock);
		drm_sched_start_timeout(sched);
		spin_unlock(&sched->job_list_lock);
	}
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
	drm_sched_increase_karma_ext(bad, 1);
}
EXPORT_SYMBOL(drm_sched_increase_karma);

void drm_sched_reset_karma(struct drm_sched_job *bad)
{
	drm_sched_increase_karma_ext(bad, 0);
}
EXPORT_SYMBOL(drm_sched_reset_karma);

/**
 * drm_sched_stop - stop the scheduler
 *
 * @sched: scheduler instance
 * @bad: job which caused the time out
 *
 * Stop the scheduler and also removes and frees all completed jobs.
 * Note: bad job will not be freed as it might be used later and so it's
 * callers responsibility to release it manually if it's not part of the
 * pending list any more.
 *
 */
void drm_sched_stop(struct drm_gpu_scheduler *sched, struct drm_sched_job *bad)
{
	struct drm_sched_job *s_job, *tmp;

	kthread_park(sched->thread);

	/*
	 * Reinsert back the bad job here - now it's safe as
	 * drm_sched_get_cleanup_job cannot race against us and release the
	 * bad job at this point - we parked (waited for) any in progress
	 * (earlier) cleanups and drm_sched_get_cleanup_job will not be called
	 * now until the scheduler thread is unparked.
	 */
	if (bad && bad->sched == sched)
		/*
		 * Add at the head of the queue to reflect it was the earliest
		 * job extracted.
		 */
		list_add(&bad->list, &sched->pending_list);

	/*
	 * Iterate the job list from later to  earlier one and either deactive
	 * their HW callbacks or remove them from pending list if they already
	 * signaled.
	 * This iteration is thread safe as sched thread is stopped.
	 */
	list_for_each_entry_safe_reverse(s_job, tmp, &sched->pending_list,
					 list) {
		if (s_job->s_fence->parent &&
		    dma_fence_remove_callback(s_job->s_fence->parent,
					      &s_job->cb)) {
			dma_fence_put(s_job->s_fence->parent);
			s_job->s_fence->parent = NULL;
			atomic_dec(&sched->hw_rq_count);
		} else {
			/*
			 * remove job from pending_list.
			 * Locking here is for concurrent resume timeout
			 */
			spin_lock(&sched->job_list_lock);
			list_del_init(&s_job->list);
			spin_unlock(&sched->job_list_lock);

			/*
			 * Wait for job's HW fence callback to finish using s_job
			 * before releasing it.
			 *
			 * Job is still alive so fence refcount at least 1
			 */
			dma_fence_wait(&s_job->s_fence->finished, false);

			/*
			 * We must keep bad job alive for later use during
			 * recovery by some of the drivers but leave a hint
			 * that the guilty job must be released.
			 */
			if (bad != s_job)
				sched->ops->free_job(s_job);
			else
				sched->free_guilty = true;
		}
	}

	/*
	 * Stop pending timer in flight as we rearm it in  drm_sched_start. This
	 * avoids the pending timeout work in progress to fire right away after
	 * this TDR finished and before the newly restarted jobs had a
	 * chance to complete.
	 */
	cancel_delayed_work(&sched->work_tdr);
}

EXPORT_SYMBOL(drm_sched_stop);

/**
 * drm_sched_start - recover jobs after a reset
 *
 * @sched: scheduler instance
 * @full_recovery: proceed with complete sched restart
 *
 */
void drm_sched_start(struct drm_gpu_scheduler *sched, bool full_recovery)
{
	struct drm_sched_job *s_job, *tmp;
	int r;

	/*
	 * Locking the list is not required here as the sched thread is parked
	 * so no new jobs are being inserted or removed. Also concurrent
	 * GPU recovers can't run in parallel.
	 */
	list_for_each_entry_safe(s_job, tmp, &sched->pending_list, list) {
		struct dma_fence *fence = s_job->s_fence->parent;

		atomic_inc(&sched->hw_rq_count);

		if (!full_recovery)
			continue;

		if (fence) {
			r = dma_fence_add_callback(fence, &s_job->cb,
						   drm_sched_job_done_cb);
			if (r == -ENOENT)
				drm_sched_job_done(s_job);
			else if (r)
				DRM_DEV_ERROR(sched->dev, "fence add callback failed (%d)\n",
					  r);
		} else
			drm_sched_job_done(s_job);
	}

	if (full_recovery) {
		spin_lock(&sched->job_list_lock);
		drm_sched_start_timeout(sched);
		spin_unlock(&sched->job_list_lock);
	}

	kthread_unpark(sched->thread);
}
EXPORT_SYMBOL(drm_sched_start);

/**
 * drm_sched_resubmit_jobs - helper to relaunch jobs from the pending list
 *
 * @sched: scheduler instance
 *
 */
void drm_sched_resubmit_jobs(struct drm_gpu_scheduler *sched)
{
	drm_sched_resubmit_jobs_ext(sched, INT_MAX);
}
EXPORT_SYMBOL(drm_sched_resubmit_jobs);

/**
 * drm_sched_resubmit_jobs_ext - helper to relunch certain number of jobs from mirror ring list
 *
 * @sched: scheduler instance
 * @max: job numbers to relaunch
 *
 */
void drm_sched_resubmit_jobs_ext(struct drm_gpu_scheduler *sched, int max)
{
	struct drm_sched_job *s_job, *tmp;
	uint64_t guilty_context;
	bool found_guilty = false;
	struct dma_fence *fence;
	int i = 0;

	list_for_each_entry_safe(s_job, tmp, &sched->pending_list, list) {
		struct drm_sched_fence *s_fence = s_job->s_fence;

		if (i >= max)
			break;

		if (!found_guilty && atomic_read(&s_job->karma) > sched->hang_limit) {
			found_guilty = true;
			guilty_context = s_job->s_fence->scheduled.context;
		}

		if (found_guilty && s_job->s_fence->scheduled.context == guilty_context)
			dma_fence_set_error(&s_fence->finished, -ECANCELED);

		fence = sched->ops->run_job(s_job);
		i++;

		if (IS_ERR_OR_NULL(fence)) {
			if (IS_ERR(fence))
				dma_fence_set_error(&s_fence->finished, PTR_ERR(fence));

			s_job->s_fence->parent = NULL;
		} else {

			s_job->s_fence->parent = dma_fence_get(fence);

			/* Drop for orignal kref_init */
			dma_fence_put(fence);
		}
	}
}
EXPORT_SYMBOL(drm_sched_resubmit_jobs_ext);

/**
 * drm_sched_job_init - init a scheduler job
 * @job: scheduler job to init
 * @entity: scheduler entity to use
 * @owner: job owner for debugging
 *
 * Refer to drm_sched_entity_push_job() documentation
 * for locking considerations.
 *
 * Drivers must make sure drm_sched_job_cleanup() if this function returns
 * successfully, even when @job is aborted before drm_sched_job_arm() is called.
 *
 * WARNING: amdgpu abuses &drm_sched.ready to signal when the hardware
 * has died, which can mean that there's no valid runqueue for a @entity.
 * This function returns -ENOENT in this case (which probably should be -EIO as
 * a more meanigful return value).
 *
 * Returns 0 for success, negative error code otherwise.
 */
int drm_sched_job_init(struct drm_sched_job *job,
		       struct drm_sched_entity *entity,
		       void *owner)
{
	if (!entity->rq)
		return -ENOENT;

	job->entity = entity;
	job->s_fence = drm_sched_fence_alloc(entity, owner);
	if (!job->s_fence)
		return -ENOMEM;

	INIT_LIST_HEAD(&job->list);

	xa_init_flags(&job->dependencies, XA_FLAGS_ALLOC);

	return 0;
}
EXPORT_SYMBOL(drm_sched_job_init);

/**
 * drm_sched_job_arm - arm a scheduler job for execution
 * @job: scheduler job to arm
 *
 * This arms a scheduler job for execution. Specifically it initializes the
 * &drm_sched_job.s_fence of @job, so that it can be attached to struct dma_resv
 * or other places that need to track the completion of this job.
 *
 * Refer to drm_sched_entity_push_job() documentation for locking
 * considerations.
 *
 * This can only be called if drm_sched_job_init() succeeded.
 */
void drm_sched_job_arm(struct drm_sched_job *job)
{
	struct drm_gpu_scheduler *sched;
	struct drm_sched_entity *entity = job->entity;

	BUG_ON(!entity);
	drm_sched_entity_select_rq(entity);
	sched = entity->rq->sched;

	job->sched = sched;
	job->s_priority = entity->rq - sched->sched_rq;
	job->id = atomic64_inc_return(&sched->job_id_count);

	drm_sched_fence_init(job->s_fence, job->entity);
}
EXPORT_SYMBOL(drm_sched_job_arm);

/**
 * drm_sched_job_add_dependency - adds the fence as a job dependency
 * @job: scheduler job to add the dependencies to
 * @fence: the dma_fence to add to the list of dependencies.
 *
 * Note that @fence is consumed in both the success and error cases.
 *
 * Returns:
 * 0 on success, or an error on failing to expand the array.
 */
int drm_sched_job_add_dependency(struct drm_sched_job *job,
				 struct dma_fence *fence)
{
	struct dma_fence *entry;
	unsigned long index;
	u32 id = 0;
	int ret;

	if (!fence)
		return 0;

	/* Deduplicate if we already depend on a fence from the same context.
	 * This lets the size of the array of deps scale with the number of
	 * engines involved, rather than the number of BOs.
	 */
	xa_for_each(&job->dependencies, index, entry) {
		if (entry->context != fence->context)
			continue;

		if (dma_fence_is_later(fence, entry)) {
			dma_fence_put(entry);
			xa_store(&job->dependencies, index, fence, GFP_KERNEL);
		} else {
			dma_fence_put(fence);
		}
		return 0;
	}

	ret = xa_alloc(&job->dependencies, &id, fence, xa_limit_32b, GFP_KERNEL);
	if (ret != 0)
		dma_fence_put(fence);

	return ret;
}
EXPORT_SYMBOL(drm_sched_job_add_dependency);

/**
 * drm_sched_job_add_implicit_dependencies - adds implicit dependencies as job
 *   dependencies
 * @job: scheduler job to add the dependencies to
 * @obj: the gem object to add new dependencies from.
 * @write: whether the job might write the object (so we need to depend on
 * shared fences in the reservation object).
 *
 * This should be called after drm_gem_lock_reservations() on your array of
 * GEM objects used in the job but before updating the reservations with your
 * own fences.
 *
 * Returns:
 * 0 on success, or an error on failing to expand the array.
 */
int drm_sched_job_add_implicit_dependencies(struct drm_sched_job *job,
					    struct drm_gem_object *obj,
					    bool write)
{
	struct dma_resv_iter cursor;
	struct dma_fence *fence;
	int ret;

	dma_resv_assert_held(obj->resv);

	dma_resv_for_each_fence(&cursor, obj->resv, dma_resv_usage_rw(write),
				fence) {
		/* Make sure to grab an additional ref on the added fence */
		dma_fence_get(fence);
		ret = drm_sched_job_add_dependency(job, fence);
		if (ret) {
			dma_fence_put(fence);
			return ret;
		}
	}
	return 0;
}
EXPORT_SYMBOL(drm_sched_job_add_implicit_dependencies);


/**
 * drm_sched_job_cleanup - clean up scheduler job resources
 * @job: scheduler job to clean up
 *
 * Cleans up the resources allocated with drm_sched_job_init().
 *
 * Drivers should call this from their error unwind code if @job is aborted
 * before drm_sched_job_arm() is called.
 *
 * After that point of no return @job is committed to be executed by the
 * scheduler, and this function should be called from the
 * &drm_sched_backend_ops.free_job callback.
 */
void drm_sched_job_cleanup(struct drm_sched_job *job)
{
	struct dma_fence *fence;
	unsigned long index;

	if (kref_read(&job->s_fence->finished.refcount)) {
		/* drm_sched_job_arm() has been called */
		dma_fence_put(&job->s_fence->finished);
	} else {
		/* aborted job before committing to run it */
		drm_sched_fence_free(job->s_fence);
	}

	job->s_fence = NULL;

	xa_for_each(&job->dependencies, index, fence) {
		dma_fence_put(fence);
	}
	xa_destroy(&job->dependencies);

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
	for (i = DRM_SCHED_PRIORITY_COUNT - 1; i >= DRM_SCHED_PRIORITY_MIN; i--) {
		entity = drm_sched_policy == DRM_SCHED_POLICY_FIFO ?
			drm_sched_rq_select_entity_fifo(&sched->sched_rq[i]) :
			drm_sched_rq_select_entity_rr(&sched->sched_rq[i]);
		if (entity)
			break;
	}

	return entity;
}

/**
 * drm_sched_get_cleanup_job - fetch the next finished job to be destroyed
 *
 * @sched: scheduler instance
 *
 * Returns the next finished job from the pending list (if there is one)
 * ready for it to be destroyed.
 */
static struct drm_sched_job *
drm_sched_get_cleanup_job(struct drm_gpu_scheduler *sched)
{
	struct drm_sched_job *job, *next;

	spin_lock(&sched->job_list_lock);

	job = list_first_entry_or_null(&sched->pending_list,
				       struct drm_sched_job, list);

	if (job && dma_fence_is_signaled(&job->s_fence->finished)) {
		/* remove job from pending_list */
		list_del_init(&job->list);

		/* cancel this job's TO timer */
		cancel_delayed_work(&sched->work_tdr);
		/* make the scheduled timestamp more accurate */
		next = list_first_entry_or_null(&sched->pending_list,
						typeof(*next), list);

		if (next) {
			next->s_fence->scheduled.timestamp =
				job->s_fence->finished.timestamp;
			/* start TO timer for next job */
			drm_sched_start_timeout(sched);
		}
	} else {
		job = NULL;
	}

	spin_unlock(&sched->job_list_lock);

	return job;
}

/**
 * drm_sched_pick_best - Get a drm sched from a sched_list with the least load
 * @sched_list: list of drm_gpu_schedulers
 * @num_sched_list: number of drm_gpu_schedulers in the sched_list
 *
 * Returns pointer of the sched with the least load or NULL if none of the
 * drm_gpu_schedulers are ready
 */
struct drm_gpu_scheduler *
drm_sched_pick_best(struct drm_gpu_scheduler **sched_list,
		     unsigned int num_sched_list)
{
	struct drm_gpu_scheduler *sched, *picked_sched = NULL;
	int i;
	unsigned int min_score = UINT_MAX, num_score;

	for (i = 0; i < num_sched_list; ++i) {
		sched = sched_list[i];

		if (!sched->ready) {
			DRM_WARN("scheduler %s is not ready, skipping",
				 sched->name);
			continue;
		}

		num_score = atomic_read(sched->score);
		if (num_score < min_score) {
			min_score = num_score;
			picked_sched = sched;
		}
	}

	return picked_sched;
}
EXPORT_SYMBOL(drm_sched_pick_best);

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
	struct drm_gpu_scheduler *sched = (struct drm_gpu_scheduler *)param;
	int r;

	sched_set_fifo_low(current);

	while (!kthread_should_stop()) {
		struct drm_sched_entity *entity = NULL;
		struct drm_sched_fence *s_fence;
		struct drm_sched_job *sched_job;
		struct dma_fence *fence;
		struct drm_sched_job *cleanup_job = NULL;

		wait_event_interruptible(sched->wake_up_worker,
					 (cleanup_job = drm_sched_get_cleanup_job(sched)) ||
					 (!drm_sched_blocked(sched) &&
					  (entity = drm_sched_select_entity(sched))) ||
					 kthread_should_stop());

		if (cleanup_job)
			sched->ops->free_job(cleanup_job);

		if (!entity)
			continue;

		sched_job = drm_sched_entity_pop_job(entity);

		if (!sched_job) {
			complete(&entity->entity_idle);
			continue;
		}

		s_fence = sched_job->s_fence;

		atomic_inc(&sched->hw_rq_count);
		drm_sched_job_begin(sched_job);

		trace_drm_run_job(sched_job, entity);
		fence = sched->ops->run_job(sched_job);
		complete(&entity->entity_idle);
		drm_sched_fence_scheduled(s_fence);

		if (!IS_ERR_OR_NULL(fence)) {
			s_fence->parent = dma_fence_get(fence);
			/* Drop for original kref_init of the fence */
			dma_fence_put(fence);

			r = dma_fence_add_callback(fence, &sched_job->cb,
						   drm_sched_job_done_cb);
			if (r == -ENOENT)
				drm_sched_job_done(sched_job);
			else if (r)
				DRM_DEV_ERROR(sched->dev, "fence add callback failed (%d)\n",
					  r);
		} else {
			if (IS_ERR(fence))
				dma_fence_set_error(&s_fence->finished, PTR_ERR(fence));

			drm_sched_job_done(sched_job);
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
 * @timeout_wq: workqueue to use for timeout work. If NULL, the system_wq is
 *		used
 * @score: optional score atomic shared with other schedulers
 * @name: name used for debugging
 * @dev: target &struct device
 *
 * Return 0 on success, otherwise error code.
 */
int drm_sched_init(struct drm_gpu_scheduler *sched,
		   const struct drm_sched_backend_ops *ops,
		   unsigned hw_submission, unsigned hang_limit,
		   long timeout, struct workqueue_struct *timeout_wq,
		   atomic_t *score, const char *name, struct device *dev)
{
	int i, ret;
	sched->ops = ops;
	sched->hw_submission_limit = hw_submission;
	sched->name = name;
	sched->timeout = timeout;
	sched->timeout_wq = timeout_wq ? : system_wq;
	sched->hang_limit = hang_limit;
	sched->score = score ? score : &sched->_score;
	sched->dev = dev;
	for (i = DRM_SCHED_PRIORITY_MIN; i < DRM_SCHED_PRIORITY_COUNT; i++)
		drm_sched_rq_init(sched, &sched->sched_rq[i]);

	init_waitqueue_head(&sched->wake_up_worker);
	init_waitqueue_head(&sched->job_scheduled);
	INIT_LIST_HEAD(&sched->pending_list);
	spin_lock_init(&sched->job_list_lock);
	atomic_set(&sched->hw_rq_count, 0);
	INIT_DELAYED_WORK(&sched->work_tdr, drm_sched_job_timedout);
	atomic_set(&sched->_score, 0);
	atomic64_set(&sched->job_id_count, 0);

	/* Each scheduler will run on a seperate kernel thread */
	sched->thread = kthread_run(drm_sched_main, sched, sched->name);
	if (IS_ERR(sched->thread)) {
		ret = PTR_ERR(sched->thread);
		sched->thread = NULL;
		DRM_DEV_ERROR(sched->dev, "Failed to create scheduler for %s.\n", name);
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
	struct drm_sched_entity *s_entity;
	int i;

	if (sched->thread)
		kthread_stop(sched->thread);

	for (i = DRM_SCHED_PRIORITY_COUNT - 1; i >= DRM_SCHED_PRIORITY_MIN; i--) {
		struct drm_sched_rq *rq = &sched->sched_rq[i];

		if (!rq)
			continue;

		spin_lock(&rq->lock);
		list_for_each_entry(s_entity, &rq->entities, list)
			/*
			 * Prevents reinsertion and marks job_queue as idle,
			 * it will removed from rq in drm_sched_entity_fini
			 * eventually
			 */
			s_entity->stopped = true;
		spin_unlock(&rq->lock);

	}

	/* Wakeup everyone stuck in drm_sched_entity_flush for this scheduler */
	wake_up_all(&sched->job_scheduled);

	/* Confirm no work left behind accessing device structures */
	cancel_delayed_work_sync(&sched->work_tdr);

	sched->ready = false;
}
EXPORT_SYMBOL(drm_sched_fini);

/**
 * drm_sched_increase_karma_ext - Update sched_entity guilty flag
 *
 * @bad: The job guilty of time out
 * @type: type for increase/reset karma
 *
 */
void drm_sched_increase_karma_ext(struct drm_sched_job *bad, int type)
{
	int i;
	struct drm_sched_entity *tmp;
	struct drm_sched_entity *entity;
	struct drm_gpu_scheduler *sched = bad->sched;

	/* don't change @bad's karma if it's from KERNEL RQ,
	 * because sometimes GPU hang would cause kernel jobs (like VM updating jobs)
	 * corrupt but keep in mind that kernel jobs always considered good.
	 */
	if (bad->s_priority != DRM_SCHED_PRIORITY_KERNEL) {
		if (type == 0)
			atomic_set(&bad->karma, 0);
		else if (type == 1)
			atomic_inc(&bad->karma);

		for (i = DRM_SCHED_PRIORITY_MIN; i < DRM_SCHED_PRIORITY_KERNEL;
		     i++) {
			struct drm_sched_rq *rq = &sched->sched_rq[i];

			spin_lock(&rq->lock);
			list_for_each_entry_safe(entity, tmp, &rq->entities, list) {
				if (bad->s_fence->scheduled.context ==
				    entity->fence_context) {
					if (entity->guilty)
						atomic_set(entity->guilty, type);
					break;
				}
			}
			spin_unlock(&rq->lock);
			if (&entity->list != &rq->entities)
				break;
		}
	}
}
EXPORT_SYMBOL(drm_sched_increase_karma_ext);
