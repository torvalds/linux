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
 * The jobs in an entity are always scheduled in the order in which they were pushed.
 *
 * Note that once a job was taken from the entities queue and pushed to the
 * hardware, i.e. the pending queue, the entity must not be referenced anymore
 * through the jobs entity pointer.
 */

/**
 * DOC: Flow Control
 *
 * The DRM GPU scheduler provides a flow control mechanism to regulate the rate
 * in which the jobs fetched from scheduler entities are executed.
 *
 * In this context the &drm_gpu_scheduler keeps track of a driver specified
 * credit limit representing the capacity of this scheduler and a credit count;
 * every &drm_sched_job carries a driver specified number of credits.
 *
 * Once a job is executed (but not yet finished), the job's credits contribute
 * to the scheduler's credit count until the job is finished. If by executing
 * one more job the scheduler's credit count would exceed the scheduler's
 * credit limit, the job won't be executed. Instead, the scheduler will wait
 * until the credit count has decreased enough to not overflow its credit limit.
 * This implies waiting for previously executed jobs.
 */

#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/completion.h>
#include <linux/dma-resv.h>
#include <uapi/linux/sched/types.h>

#include <drm/drm_print.h>
#include <drm/drm_gem.h>
#include <drm/drm_syncobj.h>
#include <drm/gpu_scheduler.h>
#include <drm/spsc_queue.h>

#include "sched_internal.h"

#define CREATE_TRACE_POINTS
#include "gpu_scheduler_trace.h"

#ifdef CONFIG_LOCKDEP
static struct lockdep_map drm_sched_lockdep_map = {
	.name = "drm_sched_lockdep_map"
};
#endif

int drm_sched_policy = DRM_SCHED_POLICY_FIFO;

/**
 * DOC: sched_policy (int)
 * Used to override default entities scheduling policy in a run queue.
 */
MODULE_PARM_DESC(sched_policy, "Specify the scheduling policy for entities on a run-queue, " __stringify(DRM_SCHED_POLICY_RR) " = Round Robin, " __stringify(DRM_SCHED_POLICY_FIFO) " = FIFO (default).");
module_param_named(sched_policy, drm_sched_policy, int, 0444);

static u32 drm_sched_available_credits(struct drm_gpu_scheduler *sched)
{
	u32 credits;

	WARN_ON(check_sub_overflow(sched->credit_limit,
				   atomic_read(&sched->credit_count),
				   &credits));

	return credits;
}

/**
 * drm_sched_can_queue -- Can we queue more to the hardware?
 * @sched: scheduler instance
 * @entity: the scheduler entity
 *
 * Return true if we can push at least one more job from @entity, false
 * otherwise.
 */
static bool drm_sched_can_queue(struct drm_gpu_scheduler *sched,
				struct drm_sched_entity *entity)
{
	struct drm_sched_job *s_job;

	s_job = drm_sched_entity_queue_peek(entity);
	if (!s_job)
		return false;

	/* If a job exceeds the credit limit, truncate it to the credit limit
	 * itself to guarantee forward progress.
	 */
	if (s_job->credits > sched->credit_limit) {
		dev_WARN(sched->dev,
			 "Jobs may not exceed the credit limit, truncate.\n");
		s_job->credits = sched->credit_limit;
	}

	return drm_sched_available_credits(sched) >= s_job->credits;
}

static __always_inline bool drm_sched_entity_compare_before(struct rb_node *a,
							    const struct rb_node *b)
{
	struct drm_sched_entity *ent_a =  rb_entry((a), struct drm_sched_entity, rb_tree_node);
	struct drm_sched_entity *ent_b =  rb_entry((b), struct drm_sched_entity, rb_tree_node);

	return ktime_before(ent_a->oldest_job_waiting, ent_b->oldest_job_waiting);
}

static void drm_sched_rq_remove_fifo_locked(struct drm_sched_entity *entity,
					    struct drm_sched_rq *rq)
{
	if (!RB_EMPTY_NODE(&entity->rb_tree_node)) {
		rb_erase_cached(&entity->rb_tree_node, &rq->rb_tree_root);
		RB_CLEAR_NODE(&entity->rb_tree_node);
	}
}

void drm_sched_rq_update_fifo_locked(struct drm_sched_entity *entity,
				     struct drm_sched_rq *rq,
				     ktime_t ts)
{
	/*
	 * Both locks need to be grabbed, one to protect from entity->rq change
	 * for entity from within concurrent drm_sched_entity_select_rq and the
	 * other to update the rb tree structure.
	 */
	lockdep_assert_held(&entity->lock);
	lockdep_assert_held(&rq->lock);

	drm_sched_rq_remove_fifo_locked(entity, rq);

	entity->oldest_job_waiting = ts;

	rb_add_cached(&entity->rb_tree_node, &rq->rb_tree_root,
		      drm_sched_entity_compare_before);
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
	lockdep_assert_held(&entity->lock);
	lockdep_assert_held(&rq->lock);

	if (!list_empty(&entity->list))
		return;

	atomic_inc(rq->sched->score);
	list_add_tail(&entity->list, &rq->entities);
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
	lockdep_assert_held(&entity->lock);

	if (list_empty(&entity->list))
		return;

	spin_lock(&rq->lock);

	atomic_dec(rq->sched->score);
	list_del_init(&entity->list);

	if (rq->current_entity == entity)
		rq->current_entity = NULL;

	if (drm_sched_policy == DRM_SCHED_POLICY_FIFO)
		drm_sched_rq_remove_fifo_locked(entity, rq);

	spin_unlock(&rq->lock);
}

/**
 * drm_sched_rq_select_entity_rr - Select an entity which could provide a job to run
 *
 * @sched: the gpu scheduler
 * @rq: scheduler run queue to check.
 *
 * Try to find the next ready entity.
 *
 * Return an entity if one is found; return an error-pointer (!NULL) if an
 * entity was ready, but the scheduler had insufficient credits to accommodate
 * its job; return NULL, if no ready entity was found.
 */
static struct drm_sched_entity *
drm_sched_rq_select_entity_rr(struct drm_gpu_scheduler *sched,
			      struct drm_sched_rq *rq)
{
	struct drm_sched_entity *entity;

	spin_lock(&rq->lock);

	entity = rq->current_entity;
	if (entity) {
		list_for_each_entry_continue(entity, &rq->entities, list) {
			if (drm_sched_entity_is_ready(entity)) {
				/* If we can't queue yet, preserve the current
				 * entity in terms of fairness.
				 */
				if (!drm_sched_can_queue(sched, entity)) {
					spin_unlock(&rq->lock);
					return ERR_PTR(-ENOSPC);
				}

				rq->current_entity = entity;
				reinit_completion(&entity->entity_idle);
				spin_unlock(&rq->lock);
				return entity;
			}
		}
	}

	list_for_each_entry(entity, &rq->entities, list) {
		if (drm_sched_entity_is_ready(entity)) {
			/* If we can't queue yet, preserve the current entity in
			 * terms of fairness.
			 */
			if (!drm_sched_can_queue(sched, entity)) {
				spin_unlock(&rq->lock);
				return ERR_PTR(-ENOSPC);
			}

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
 * @sched: the gpu scheduler
 * @rq: scheduler run queue to check.
 *
 * Find oldest waiting ready entity.
 *
 * Return an entity if one is found; return an error-pointer (!NULL) if an
 * entity was ready, but the scheduler had insufficient credits to accommodate
 * its job; return NULL, if no ready entity was found.
 */
static struct drm_sched_entity *
drm_sched_rq_select_entity_fifo(struct drm_gpu_scheduler *sched,
				struct drm_sched_rq *rq)
{
	struct rb_node *rb;

	spin_lock(&rq->lock);
	for (rb = rb_first_cached(&rq->rb_tree_root); rb; rb = rb_next(rb)) {
		struct drm_sched_entity *entity;

		entity = rb_entry(rb, struct drm_sched_entity, rb_tree_node);
		if (drm_sched_entity_is_ready(entity)) {
			/* If we can't queue yet, preserve the current entity in
			 * terms of fairness.
			 */
			if (!drm_sched_can_queue(sched, entity)) {
				spin_unlock(&rq->lock);
				return ERR_PTR(-ENOSPC);
			}

			reinit_completion(&entity->entity_idle);
			break;
		}
	}
	spin_unlock(&rq->lock);

	return rb ? rb_entry(rb, struct drm_sched_entity, rb_tree_node) : NULL;
}

/**
 * drm_sched_run_job_queue - enqueue run-job work
 * @sched: scheduler instance
 */
static void drm_sched_run_job_queue(struct drm_gpu_scheduler *sched)
{
	if (!READ_ONCE(sched->pause_submit))
		queue_work(sched->submit_wq, &sched->work_run_job);
}

/**
 * __drm_sched_run_free_queue - enqueue free-job work
 * @sched: scheduler instance
 */
static void __drm_sched_run_free_queue(struct drm_gpu_scheduler *sched)
{
	if (!READ_ONCE(sched->pause_submit))
		queue_work(sched->submit_wq, &sched->work_free_job);
}

/**
 * drm_sched_run_free_queue - enqueue free-job work if ready
 * @sched: scheduler instance
 */
static void drm_sched_run_free_queue(struct drm_gpu_scheduler *sched)
{
	struct drm_sched_job *job;

	spin_lock(&sched->job_list_lock);
	job = list_first_entry_or_null(&sched->pending_list,
				       struct drm_sched_job, list);
	if (job && dma_fence_is_signaled(&job->s_fence->finished))
		__drm_sched_run_free_queue(sched);
	spin_unlock(&sched->job_list_lock);
}

/**
 * drm_sched_job_done - complete a job
 * @s_job: pointer to the job which is done
 *
 * Finish the job's fence and wake up the worker thread.
 */
static void drm_sched_job_done(struct drm_sched_job *s_job, int result)
{
	struct drm_sched_fence *s_fence = s_job->s_fence;
	struct drm_gpu_scheduler *sched = s_fence->sched;

	atomic_sub(s_job->credits, &sched->credit_count);
	atomic_dec(sched->score);

	trace_drm_sched_process_job(s_fence);

	dma_fence_get(&s_fence->finished);
	drm_sched_fence_finished(s_fence, result);
	dma_fence_put(&s_fence->finished);
	__drm_sched_run_free_queue(sched);
}

/**
 * drm_sched_job_done_cb - the callback for a done job
 * @f: fence
 * @cb: fence callbacks
 */
static void drm_sched_job_done_cb(struct dma_fence *f, struct dma_fence_cb *cb)
{
	struct drm_sched_job *s_job = container_of(cb, struct drm_sched_job, cb);

	drm_sched_job_done(s_job, f->error);
}

/**
 * drm_sched_start_timeout - start timeout for reset worker
 *
 * @sched: scheduler instance to start the worker for
 *
 * Start the timeout for the given scheduler.
 */
static void drm_sched_start_timeout(struct drm_gpu_scheduler *sched)
{
	lockdep_assert_held(&sched->job_list_lock);

	if (sched->timeout != MAX_SCHEDULE_TIMEOUT &&
	    !list_empty(&sched->pending_list))
		mod_delayed_work(sched->timeout_wq, &sched->work_tdr, sched->timeout);
}

static void drm_sched_start_timeout_unlocked(struct drm_gpu_scheduler *sched)
{
	spin_lock(&sched->job_list_lock);
	drm_sched_start_timeout(sched);
	spin_unlock(&sched->job_list_lock);
}

/**
 * drm_sched_tdr_queue_imm: - immediately start job timeout handler
 *
 * @sched: scheduler for which the timeout handling should be started.
 *
 * Start timeout handling immediately for the named scheduler.
 */
void drm_sched_tdr_queue_imm(struct drm_gpu_scheduler *sched)
{
	spin_lock(&sched->job_list_lock);
	sched->timeout = 0;
	drm_sched_start_timeout(sched);
	spin_unlock(&sched->job_list_lock);
}
EXPORT_SYMBOL(drm_sched_tdr_queue_imm);

/**
 * drm_sched_fault - immediately start timeout handler
 *
 * @sched: scheduler where the timeout handling should be started.
 *
 * Start timeout handling immediately when the driver detects a hardware fault.
 */
void drm_sched_fault(struct drm_gpu_scheduler *sched)
{
	if (sched->timeout_wq)
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

	/* Protects against concurrent deletion in drm_sched_get_finished_job */
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

	if (status != DRM_GPU_SCHED_STAT_ENODEV)
		drm_sched_start_timeout_unlocked(sched);
}

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
 * This function is typically used for reset recovery (see the docu of
 * drm_sched_backend_ops.timedout_job() for details). Do not call it for
 * scheduler teardown, i.e., before calling drm_sched_fini().
 */
void drm_sched_stop(struct drm_gpu_scheduler *sched, struct drm_sched_job *bad)
{
	struct drm_sched_job *s_job, *tmp;

	drm_sched_wqueue_stop(sched);

	/*
	 * Reinsert back the bad job here - now it's safe as
	 * drm_sched_get_finished_job cannot race against us and release the
	 * bad job at this point - we parked (waited for) any in progress
	 * (earlier) cleanups and drm_sched_get_finished_job will not be called
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
			atomic_sub(s_job->credits, &sched->credit_count);
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
 * @errno: error to set on the pending fences
 *
 * This function is typically used for reset recovery (see the docu of
 * drm_sched_backend_ops.timedout_job() for details). Do not call it for
 * scheduler startup. The scheduler itself is fully operational after
 * drm_sched_init() succeeded.
 */
void drm_sched_start(struct drm_gpu_scheduler *sched, int errno)
{
	struct drm_sched_job *s_job, *tmp;

	/*
	 * Locking the list is not required here as the sched thread is parked
	 * so no new jobs are being inserted or removed. Also concurrent
	 * GPU recovers can't run in parallel.
	 */
	list_for_each_entry_safe(s_job, tmp, &sched->pending_list, list) {
		struct dma_fence *fence = s_job->s_fence->parent;

		atomic_add(s_job->credits, &sched->credit_count);

		if (!fence) {
			drm_sched_job_done(s_job, errno ?: -ECANCELED);
			continue;
		}

		if (dma_fence_add_callback(fence, &s_job->cb,
					   drm_sched_job_done_cb))
			drm_sched_job_done(s_job, fence->error ?: errno);
	}

	drm_sched_start_timeout_unlocked(sched);
	drm_sched_wqueue_start(sched);
}
EXPORT_SYMBOL(drm_sched_start);

/**
 * drm_sched_resubmit_jobs - Deprecated, don't use in new code!
 *
 * @sched: scheduler instance
 *
 * Re-submitting jobs was a concept AMD came up as cheap way to implement
 * recovery after a job timeout.
 *
 * This turned out to be not working very well. First of all there are many
 * problem with the dma_fence implementation and requirements. Either the
 * implementation is risking deadlocks with core memory management or violating
 * documented implementation details of the dma_fence object.
 *
 * Drivers can still save and restore their state for recovery operations, but
 * we shouldn't make this a general scheduler feature around the dma_fence
 * interface.
 */
void drm_sched_resubmit_jobs(struct drm_gpu_scheduler *sched)
{
	struct drm_sched_job *s_job, *tmp;
	uint64_t guilty_context;
	bool found_guilty = false;
	struct dma_fence *fence;

	list_for_each_entry_safe(s_job, tmp, &sched->pending_list, list) {
		struct drm_sched_fence *s_fence = s_job->s_fence;

		if (!found_guilty && atomic_read(&s_job->karma) > sched->hang_limit) {
			found_guilty = true;
			guilty_context = s_job->s_fence->scheduled.context;
		}

		if (found_guilty && s_job->s_fence->scheduled.context == guilty_context)
			dma_fence_set_error(&s_fence->finished, -ECANCELED);

		fence = sched->ops->run_job(s_job);

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
EXPORT_SYMBOL(drm_sched_resubmit_jobs);

/**
 * drm_sched_job_init - init a scheduler job
 * @job: scheduler job to init
 * @entity: scheduler entity to use
 * @credits: the number of credits this job contributes to the schedulers
 * credit limit
 * @owner: job owner for debugging
 *
 * Refer to drm_sched_entity_push_job() documentation
 * for locking considerations.
 *
 * Drivers must make sure drm_sched_job_cleanup() if this function returns
 * successfully, even when @job is aborted before drm_sched_job_arm() is called.
 *
 * Note that this function does not assign a valid value to each struct member
 * of struct drm_sched_job. Take a look at that struct's documentation to see
 * who sets which struct member with what lifetime.
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
		       u32 credits, void *owner)
{
	if (!entity->rq) {
		/* This will most likely be followed by missing frames
		 * or worse--a blank screen--leave a trail in the
		 * logs, so this can be debugged easier.
		 */
		dev_err(job->sched->dev, "%s: entity has no rq!\n", __func__);
		return -ENOENT;
	}

	if (unlikely(!credits)) {
		pr_err("*ERROR* %s: credits cannot be 0!\n", __func__);
		return -EINVAL;
	}

	/*
	 * We don't know for sure how the user has allocated. Thus, zero the
	 * struct so that unallowed (i.e., too early) usage of pointers that
	 * this function does not set is guaranteed to lead to a NULL pointer
	 * exception instead of UB.
	 */
	memset(job, 0, sizeof(*job));

	job->entity = entity;
	job->credits = credits;
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
	job->s_priority = entity->priority;
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
 * drm_sched_job_add_syncobj_dependency - adds a syncobj's fence as a job dependency
 * @job: scheduler job to add the dependencies to
 * @file: drm file private pointer
 * @handle: syncobj handle to lookup
 * @point: timeline point
 *
 * This adds the fence matching the given syncobj to @job.
 *
 * Returns:
 * 0 on success, or an error on failing to expand the array.
 */
int drm_sched_job_add_syncobj_dependency(struct drm_sched_job *job,
					 struct drm_file *file,
					 u32 handle,
					 u32 point)
{
	struct dma_fence *fence;
	int ret;

	ret = drm_syncobj_find_fence(file, handle, point, 0, &fence);
	if (ret)
		return ret;

	return drm_sched_job_add_dependency(job, fence);
}
EXPORT_SYMBOL(drm_sched_job_add_syncobj_dependency);

/**
 * drm_sched_job_add_resv_dependencies - add all fences from the resv to the job
 * @job: scheduler job to add the dependencies to
 * @resv: the dma_resv object to get the fences from
 * @usage: the dma_resv_usage to use to filter the fences
 *
 * This adds all fences matching the given usage from @resv to @job.
 * Must be called with the @resv lock held.
 *
 * Returns:
 * 0 on success, or an error on failing to expand the array.
 */
int drm_sched_job_add_resv_dependencies(struct drm_sched_job *job,
					struct dma_resv *resv,
					enum dma_resv_usage usage)
{
	struct dma_resv_iter cursor;
	struct dma_fence *fence;
	int ret;

	dma_resv_assert_held(resv);

	dma_resv_for_each_fence(&cursor, resv, usage, fence) {
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
EXPORT_SYMBOL(drm_sched_job_add_resv_dependencies);

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
	return drm_sched_job_add_resv_dependencies(job, obj->resv,
						   dma_resv_usage_rw(write));
}
EXPORT_SYMBOL(drm_sched_job_add_implicit_dependencies);

/**
 * drm_sched_job_has_dependency - check whether fence is the job's dependency
 * @job: scheduler job to check
 * @fence: fence to look for
 *
 * Returns:
 * True if @fence is found within the job's dependencies, or otherwise false.
 */
bool drm_sched_job_has_dependency(struct drm_sched_job *job,
				  struct dma_fence *fence)
{
	struct dma_fence *f;
	unsigned long index;

	xa_for_each(&job->dependencies, index, f) {
		if (f == fence)
			return true;
	}

	return false;
}
EXPORT_SYMBOL(drm_sched_job_has_dependency);

/**
 * drm_sched_job_cleanup - clean up scheduler job resources
 * @job: scheduler job to clean up
 *
 * Cleans up the resources allocated with drm_sched_job_init().
 *
 * Drivers should call this from their error unwind code if @job is aborted
 * before it was submitted to an entity with drm_sched_entity_push_job().
 *
 * Since calling drm_sched_job_arm() causes the job's fences to be initialized,
 * it is up to the driver to ensure that fences that were exposed to external
 * parties get signaled. drm_sched_job_cleanup() does not ensure this.
 *
 * This function must also be called in &struct drm_sched_backend_ops.free_job
 */
void drm_sched_job_cleanup(struct drm_sched_job *job)
{
	struct dma_fence *fence;
	unsigned long index;

	if (kref_read(&job->s_fence->finished.refcount)) {
		/* drm_sched_job_arm() has been called */
		dma_fence_put(&job->s_fence->finished);
	} else {
		/* aborted job before arming */
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
 * drm_sched_wakeup - Wake up the scheduler if it is ready to queue
 * @sched: scheduler instance
 *
 * Wake up the scheduler if we can queue jobs.
 */
void drm_sched_wakeup(struct drm_gpu_scheduler *sched)
{
	drm_sched_run_job_queue(sched);
}

/**
 * drm_sched_select_entity - Select next entity to process
 *
 * @sched: scheduler instance
 *
 * Return an entity to process or NULL if none are found.
 *
 * Note, that we break out of the for-loop when "entity" is non-null, which can
 * also be an error-pointer--this assures we don't process lower priority
 * run-queues. See comments in the respectively called functions.
 */
static struct drm_sched_entity *
drm_sched_select_entity(struct drm_gpu_scheduler *sched)
{
	struct drm_sched_entity *entity;
	int i;

	/* Start with the highest priority.
	 */
	for (i = DRM_SCHED_PRIORITY_KERNEL; i < sched->num_rqs; i++) {
		entity = drm_sched_policy == DRM_SCHED_POLICY_FIFO ?
			drm_sched_rq_select_entity_fifo(sched, sched->sched_rq[i]) :
			drm_sched_rq_select_entity_rr(sched, sched->sched_rq[i]);
		if (entity)
			break;
	}

	return IS_ERR(entity) ? NULL : entity;
}

/**
 * drm_sched_get_finished_job - fetch the next finished job to be destroyed
 *
 * @sched: scheduler instance
 *
 * Returns the next finished job from the pending list (if there is one)
 * ready for it to be destroyed.
 */
static struct drm_sched_job *
drm_sched_get_finished_job(struct drm_gpu_scheduler *sched)
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
			if (test_bit(DMA_FENCE_FLAG_TIMESTAMP_BIT,
				     &next->s_fence->scheduled.flags))
				next->s_fence->scheduled.timestamp =
					dma_fence_timestamp(&job->s_fence->finished);
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
 * drm_sched_free_job_work - worker to call free_job
 *
 * @w: free job work
 */
static void drm_sched_free_job_work(struct work_struct *w)
{
	struct drm_gpu_scheduler *sched =
		container_of(w, struct drm_gpu_scheduler, work_free_job);
	struct drm_sched_job *job;

	job = drm_sched_get_finished_job(sched);
	if (job)
		sched->ops->free_job(job);

	drm_sched_run_free_queue(sched);
	drm_sched_run_job_queue(sched);
}

/**
 * drm_sched_run_job_work - worker to call run_job
 *
 * @w: run job work
 */
static void drm_sched_run_job_work(struct work_struct *w)
{
	struct drm_gpu_scheduler *sched =
		container_of(w, struct drm_gpu_scheduler, work_run_job);
	struct drm_sched_entity *entity;
	struct dma_fence *fence;
	struct drm_sched_fence *s_fence;
	struct drm_sched_job *sched_job;
	int r;

	/* Find entity with a ready job */
	entity = drm_sched_select_entity(sched);
	if (!entity)
		return;	/* No more work */

	sched_job = drm_sched_entity_pop_job(entity);
	if (!sched_job) {
		complete_all(&entity->entity_idle);
		drm_sched_run_job_queue(sched);
		return;
	}

	s_fence = sched_job->s_fence;

	atomic_add(sched_job->credits, &sched->credit_count);
	drm_sched_job_begin(sched_job);

	trace_drm_run_job(sched_job, entity);
	fence = sched->ops->run_job(sched_job);
	complete_all(&entity->entity_idle);
	drm_sched_fence_scheduled(s_fence, fence);

	if (!IS_ERR_OR_NULL(fence)) {
		/* Drop for original kref_init of the fence */
		dma_fence_put(fence);

		r = dma_fence_add_callback(fence, &sched_job->cb,
					   drm_sched_job_done_cb);
		if (r == -ENOENT)
			drm_sched_job_done(sched_job, fence->error);
		else if (r)
			DRM_DEV_ERROR(sched->dev, "fence add callback failed (%d)\n", r);
	} else {
		drm_sched_job_done(sched_job, IS_ERR(fence) ?
				   PTR_ERR(fence) : 0);
	}

	wake_up(&sched->job_scheduled);
	drm_sched_run_job_queue(sched);
}

/**
 * drm_sched_init - Init a gpu scheduler instance
 *
 * @sched: scheduler instance
 * @args: scheduler initialization arguments
 *
 * Return 0 on success, otherwise error code.
 */
int drm_sched_init(struct drm_gpu_scheduler *sched, const struct drm_sched_init_args *args)
{
	int i;

	sched->ops = args->ops;
	sched->credit_limit = args->credit_limit;
	sched->name = args->name;
	sched->timeout = args->timeout;
	sched->hang_limit = args->hang_limit;
	sched->timeout_wq = args->timeout_wq ? args->timeout_wq : system_wq;
	sched->score = args->score ? args->score : &sched->_score;
	sched->dev = args->dev;

	if (args->num_rqs > DRM_SCHED_PRIORITY_COUNT) {
		/* This is a gross violation--tell drivers what the  problem is.
		 */
		dev_err(sched->dev, "%s: num_rqs cannot be greater than DRM_SCHED_PRIORITY_COUNT\n",
			__func__);
		return -EINVAL;
	} else if (sched->sched_rq) {
		/* Not an error, but warn anyway so drivers can
		 * fine-tune their DRM calling order, and return all
		 * is good.
		 */
		dev_warn(sched->dev, "%s: scheduler already initialized!\n", __func__);
		return 0;
	}

	if (args->submit_wq) {
		sched->submit_wq = args->submit_wq;
		sched->own_submit_wq = false;
	} else {
#ifdef CONFIG_LOCKDEP
		sched->submit_wq = alloc_ordered_workqueue_lockdep_map(args->name,
								       WQ_MEM_RECLAIM,
								       &drm_sched_lockdep_map);
#else
		sched->submit_wq = alloc_ordered_workqueue(args->name, WQ_MEM_RECLAIM);
#endif
		if (!sched->submit_wq)
			return -ENOMEM;

		sched->own_submit_wq = true;
	}

	sched->sched_rq = kmalloc_array(args->num_rqs, sizeof(*sched->sched_rq),
					GFP_KERNEL | __GFP_ZERO);
	if (!sched->sched_rq)
		goto Out_check_own;
	sched->num_rqs = args->num_rqs;
	for (i = DRM_SCHED_PRIORITY_KERNEL; i < sched->num_rqs; i++) {
		sched->sched_rq[i] = kzalloc(sizeof(*sched->sched_rq[i]), GFP_KERNEL);
		if (!sched->sched_rq[i])
			goto Out_unroll;
		drm_sched_rq_init(sched, sched->sched_rq[i]);
	}

	init_waitqueue_head(&sched->job_scheduled);
	INIT_LIST_HEAD(&sched->pending_list);
	spin_lock_init(&sched->job_list_lock);
	atomic_set(&sched->credit_count, 0);
	INIT_DELAYED_WORK(&sched->work_tdr, drm_sched_job_timedout);
	INIT_WORK(&sched->work_run_job, drm_sched_run_job_work);
	INIT_WORK(&sched->work_free_job, drm_sched_free_job_work);
	atomic_set(&sched->_score, 0);
	atomic64_set(&sched->job_id_count, 0);
	sched->pause_submit = false;

	sched->ready = true;
	return 0;
Out_unroll:
	for (--i ; i >= DRM_SCHED_PRIORITY_KERNEL; i--)
		kfree(sched->sched_rq[i]);

	kfree(sched->sched_rq);
	sched->sched_rq = NULL;
Out_check_own:
	if (sched->own_submit_wq)
		destroy_workqueue(sched->submit_wq);
	dev_err(sched->dev, "%s: Failed to setup GPU scheduler--out of memory\n", __func__);
	return -ENOMEM;
}
EXPORT_SYMBOL(drm_sched_init);

/**
 * drm_sched_fini - Destroy a gpu scheduler
 *
 * @sched: scheduler instance
 *
 * Tears down and cleans up the scheduler.
 *
 * This stops submission of new jobs to the hardware through
 * drm_sched_backend_ops.run_job(). Consequently, drm_sched_backend_ops.free_job()
 * will not be called for all jobs still in drm_gpu_scheduler.pending_list.
 * There is no solution for this currently. Thus, it is up to the driver to make
 * sure that:
 *
 *  a) drm_sched_fini() is only called after for all submitted jobs
 *     drm_sched_backend_ops.free_job() has been called or that
 *  b) the jobs for which drm_sched_backend_ops.free_job() has not been called
 *     after drm_sched_fini() ran are freed manually.
 *
 * FIXME: Take care of the above problem and prevent this function from leaking
 * the jobs in drm_gpu_scheduler.pending_list under any circumstances.
 */
void drm_sched_fini(struct drm_gpu_scheduler *sched)
{
	struct drm_sched_entity *s_entity;
	int i;

	drm_sched_wqueue_stop(sched);

	for (i = DRM_SCHED_PRIORITY_KERNEL; i < sched->num_rqs; i++) {
		struct drm_sched_rq *rq = sched->sched_rq[i];

		spin_lock(&rq->lock);
		list_for_each_entry(s_entity, &rq->entities, list)
			/*
			 * Prevents reinsertion and marks job_queue as idle,
			 * it will be removed from the rq in drm_sched_entity_fini()
			 * eventually
			 */
			s_entity->stopped = true;
		spin_unlock(&rq->lock);
		kfree(sched->sched_rq[i]);
	}

	/* Wakeup everyone stuck in drm_sched_entity_flush for this scheduler */
	wake_up_all(&sched->job_scheduled);

	/* Confirm no work left behind accessing device structures */
	cancel_delayed_work_sync(&sched->work_tdr);

	if (sched->own_submit_wq)
		destroy_workqueue(sched->submit_wq);
	sched->ready = false;
	kfree(sched->sched_rq);
	sched->sched_rq = NULL;
}
EXPORT_SYMBOL(drm_sched_fini);

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

	/* don't change @bad's karma if it's from KERNEL RQ,
	 * because sometimes GPU hang would cause kernel jobs (like VM updating jobs)
	 * corrupt but keep in mind that kernel jobs always considered good.
	 */
	if (bad->s_priority != DRM_SCHED_PRIORITY_KERNEL) {
		atomic_inc(&bad->karma);

		for (i = DRM_SCHED_PRIORITY_HIGH; i < sched->num_rqs; i++) {
			struct drm_sched_rq *rq = sched->sched_rq[i];

			spin_lock(&rq->lock);
			list_for_each_entry_safe(entity, tmp, &rq->entities, list) {
				if (bad->s_fence->scheduled.context ==
				    entity->fence_context) {
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
 * drm_sched_wqueue_ready - Is the scheduler ready for submission
 *
 * @sched: scheduler instance
 *
 * Returns true if submission is ready
 */
bool drm_sched_wqueue_ready(struct drm_gpu_scheduler *sched)
{
	return sched->ready;
}
EXPORT_SYMBOL(drm_sched_wqueue_ready);

/**
 * drm_sched_wqueue_stop - stop scheduler submission
 * @sched: scheduler instance
 *
 * Stops the scheduler from pulling new jobs from entities. It also stops
 * freeing jobs automatically through drm_sched_backend_ops.free_job().
 */
void drm_sched_wqueue_stop(struct drm_gpu_scheduler *sched)
{
	WRITE_ONCE(sched->pause_submit, true);
	cancel_work_sync(&sched->work_run_job);
	cancel_work_sync(&sched->work_free_job);
}
EXPORT_SYMBOL(drm_sched_wqueue_stop);

/**
 * drm_sched_wqueue_start - start scheduler submission
 * @sched: scheduler instance
 *
 * Restarts the scheduler after drm_sched_wqueue_stop() has stopped it.
 *
 * This function is not necessary for 'conventional' startup. The scheduler is
 * fully operational after drm_sched_init() succeeded.
 */
void drm_sched_wqueue_start(struct drm_gpu_scheduler *sched)
{
	WRITE_ONCE(sched->pause_submit, false);
	queue_work(sched->submit_wq, &sched->work_run_job);
	queue_work(sched->submit_wq, &sched->work_free_job);
}
EXPORT_SYMBOL(drm_sched_wqueue_start);
