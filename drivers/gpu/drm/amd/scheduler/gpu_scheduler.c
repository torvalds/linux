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
 * Select next entity from a specified run queue with round robin policy.
 * It could return the same entity as current one if current is the only
 * available one in the queue. Return NULL if nothing available.
 */
static struct amd_sched_entity *
amd_sched_rq_select_entity(struct amd_sched_rq *rq)
{
	struct amd_sched_entity *entity;

	spin_lock(&rq->lock);

	entity = rq->current_entity;
	if (entity) {
		list_for_each_entry_continue(entity, &rq->entities, list) {
			if (!kfifo_is_empty(&entity->job_queue)) {
				rq->current_entity = entity;
				spin_unlock(&rq->lock);
				return rq->current_entity;
			}
		}
	}

	list_for_each_entry(entity, &rq->entities, list) {

		if (!kfifo_is_empty(&entity->job_queue)) {
			rq->current_entity = entity;
			spin_unlock(&rq->lock);
			return rq->current_entity;
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
	if (!(sched && entity && rq))
		return -EINVAL;

	memset(entity, 0, sizeof(struct amd_sched_entity));
	entity->belongto_rq = rq;
	entity->scheduler = sched;
	init_waitqueue_head(&entity->wait_queue);
	entity->fence_context = fence_context_alloc(1);
	if(kfifo_alloc(&entity->job_queue,
		       jobs * sizeof(void *),
		       GFP_KERNEL))
		return -EINVAL;

	spin_lock_init(&entity->queue_lock);
	atomic_set(&entity->fence_seq, 0);

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
	return entity->scheduler == sched &&
		entity->belongto_rq != NULL;
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
 * Destroy a context entity
 *
 * @sched       Pointer to scheduler instance
 * @entity	The pointer to a valid scheduler entity
 *
 * return 0 if succeed. negative error code on failure
 */
int amd_sched_entity_fini(struct amd_gpu_scheduler *sched,
			    struct amd_sched_entity *entity)
{
	struct amd_sched_rq *rq = entity->belongto_rq;
	long r;

	if (!amd_sched_entity_is_initialized(sched, entity))
		return 0;

	/**
	 * The client will not queue more IBs during this fini, consume existing
	 * queued IBs
	*/
	r = wait_event_timeout(entity->wait_queue,
		amd_sched_entity_is_idle(entity),
		msecs_to_jiffies(AMD_GPU_WAIT_IDLE_TIMEOUT_IN_MS));

	if (r <= 0)
		DRM_INFO("Entity %p is in waiting state during fini\n",
			 entity);

	amd_sched_rq_remove_entity(rq, entity);
	kfifo_free(&entity->job_queue);
	return r;
}

/**
 * Helper to submit a job to the job queue
 *
 * @job		The pointer to job required to submit
 *
 * Returns true if we could submit the job.
 */
static bool amd_sched_entity_in(struct amd_sched_job *job)
{
	struct amd_sched_entity *entity = job->s_entity;
	bool added, first = false;

	spin_lock(&entity->queue_lock);
	added = kfifo_in(&entity->job_queue, &job, sizeof(job)) == sizeof(job);

	if (added && kfifo_len(&entity->job_queue) == sizeof(job))
		first = true;

	spin_unlock(&entity->queue_lock);

	/* first job wakes up scheduler */
	if (first)
		wake_up_interruptible(&job->sched->wait_queue);

	return added;
}

/**
 * Submit a job to the job queue
 *
 * @job		The pointer to job required to submit
 *
 * Returns 0 for success, negative error code otherwise.
 */
int amd_sched_entity_push_job(struct amd_sched_job *sched_job)
{
	struct amd_sched_entity *entity = sched_job->s_entity;
	struct amd_sched_fence *fence = amd_sched_fence_create(
		entity, sched_job->owner);
	int r;

	if (!fence)
		return -ENOMEM;

	fence_get(&fence->base);
	sched_job->s_fence = fence;

	r = wait_event_interruptible(entity->wait_queue,
				     amd_sched_entity_in(sched_job));

	return r;
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
 * Select next entity containing real IB submissions
*/
static struct amd_sched_entity *
amd_sched_select_context(struct amd_gpu_scheduler *sched)
{
	struct amd_sched_entity *tmp;

	if (!amd_sched_ready(sched))
		return NULL;

	/* Kernel run queue has higher priority than normal run queue*/
	tmp = amd_sched_rq_select_entity(&sched->kernel_rq);
	if (tmp == NULL)
		tmp = amd_sched_rq_select_entity(&sched->sched_rq);

	return tmp;
}

static void amd_sched_process_job(struct fence *f, struct fence_cb *cb)
{
	struct amd_sched_job *sched_job =
		container_of(cb, struct amd_sched_job, cb);
	struct amd_gpu_scheduler *sched;

	sched = sched_job->sched;
	amd_sched_fence_signal(sched_job->s_fence);
	atomic_dec(&sched->hw_rq_count);
	fence_put(&sched_job->s_fence->base);
	sched->ops->process_job(sched, sched_job);
	wake_up_interruptible(&sched->wait_queue);
}

static int amd_sched_main(void *param)
{
	struct sched_param sparam = {.sched_priority = 1};
	struct amd_gpu_scheduler *sched = (struct amd_gpu_scheduler *)param;
	int r;

	sched_setscheduler(current, SCHED_FIFO, &sparam);

	while (!kthread_should_stop()) {
		struct amd_sched_entity *c_entity = NULL;
		struct amd_sched_job *job;
		struct fence *fence;

		wait_event_interruptible(sched->wait_queue,
			kthread_should_stop() ||
			(c_entity = amd_sched_select_context(sched)));

		if (!c_entity)
			continue;

		r = kfifo_out(&c_entity->job_queue, &job, sizeof(void *));
		if (r != sizeof(void *))
			continue;
		atomic_inc(&sched->hw_rq_count);

		fence = sched->ops->run_job(sched, c_entity, job);
		if (fence) {
			r = fence_add_callback(fence, &job->cb,
					       amd_sched_process_job);
			if (r == -ENOENT)
				amd_sched_process_job(fence, &job->cb);
			else if (r)
				DRM_ERROR("fence add callback failed (%d)\n", r);
			fence_put(fence);
		}

		wake_up(&c_entity->wait_queue);
	}
	return 0;
}

/**
 * Create a gpu scheduler
 *
 * @ops			The backend operations for this scheduler.
 * @ring		The the ring id for the scheduler.
 * @hw_submissions	Number of hw submissions to do.
 *
 * Return the pointer to scheduler for success, otherwise return NULL
*/
struct amd_gpu_scheduler *amd_sched_create(struct amd_sched_backend_ops *ops,
					   unsigned ring, unsigned hw_submission,
					   void *priv)
{
	struct amd_gpu_scheduler *sched;

	sched = kzalloc(sizeof(struct amd_gpu_scheduler), GFP_KERNEL);
	if (!sched)
		return NULL;

	sched->ops = ops;
	sched->ring_id = ring;
	sched->hw_submission_limit = hw_submission;
	sched->priv = priv;
	snprintf(sched->name, sizeof(sched->name), "amdgpu[%d]", ring);
	amd_sched_rq_init(&sched->sched_rq);
	amd_sched_rq_init(&sched->kernel_rq);

	init_waitqueue_head(&sched->wait_queue);
	atomic_set(&sched->hw_rq_count, 0);
	/* Each scheduler will run on a seperate kernel thread */
	sched->thread = kthread_run(amd_sched_main, sched, sched->name);
	if (IS_ERR(sched->thread)) {
		DRM_ERROR("Failed to create scheduler for id %d.\n", ring);
		kfree(sched);
		return NULL;
	}

	return sched;
}

/**
 * Destroy a gpu scheduler
 *
 * @sched	The pointer to the scheduler
 *
 * return 0 if succeed. -1 if failed.
 */
int amd_sched_destroy(struct amd_gpu_scheduler *sched)
{
	kthread_stop(sched->thread);
	kfree(sched);
	return  0;
}
