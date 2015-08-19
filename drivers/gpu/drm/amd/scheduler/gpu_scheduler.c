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
select_context(struct amd_gpu_scheduler *sched)
{
	struct amd_sched_entity *wake_entity = NULL;
	struct amd_sched_entity *tmp;

	if (!amd_sched_ready(sched))
		return NULL;

	/* Kernel run queue has higher priority than normal run queue*/
	tmp = amd_sched_rq_select_entity(&sched->kernel_rq);
	if (tmp == NULL)
		tmp = amd_sched_rq_select_entity(&sched->sched_rq);

	if (sched->current_entity && (sched->current_entity != tmp))
		wake_entity = sched->current_entity;
	sched->current_entity = tmp;
	if (wake_entity && wake_entity->need_wakeup)
		wake_up(&wake_entity->wait_queue);
	return tmp;
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
	char name[20];

	if (!(sched && entity && rq))
		return -EINVAL;

	memset(entity, 0, sizeof(struct amd_sched_entity));
	entity->belongto_rq = rq;
	entity->scheduler = sched;
	init_waitqueue_head(&entity->wait_queue);
	entity->fence_context = fence_context_alloc(1);
	snprintf(name, sizeof(name), "c_entity[%llu]", entity->fence_context);
	memcpy(entity->name, name, 20);
	entity->need_wakeup = false;
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
static bool is_context_entity_initialized(struct amd_gpu_scheduler *sched,
					  struct amd_sched_entity *entity)
{
	return entity->scheduler == sched &&
		entity->belongto_rq != NULL;
}

static bool is_context_entity_idle(struct amd_gpu_scheduler *sched,
				   struct amd_sched_entity *entity)
{
	/**
	 * Idle means no pending IBs, and the entity is not
	 * currently being used.
	*/
	barrier();
	if ((sched->current_entity != entity) &&
	    kfifo_is_empty(&entity->job_queue))
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
	int r = 0;
	struct amd_sched_rq *rq = entity->belongto_rq;

	if (!is_context_entity_initialized(sched, entity))
		return 0;
	entity->need_wakeup = true;
	/**
	 * The client will not queue more IBs during this fini, consume existing
	 * queued IBs
	*/
	r = wait_event_timeout(
		entity->wait_queue,
		is_context_entity_idle(sched, entity),
		msecs_to_jiffies(AMD_GPU_WAIT_IDLE_TIMEOUT_IN_MS)
		) ?  0 : -1;

	if (r)
		DRM_INFO("Entity %p is in waiting state during fini\n",
			 entity);

	amd_sched_rq_remove_entity(rq, entity);
	kfifo_free(&entity->job_queue);
	return r;
}

/**
 * Submit a normal job to the job queue
 *
 * @sched	The pointer to the scheduler
 * @c_entity    The pointer to amd_sched_entity
 * @job		The pointer to job required to submit
 * return 0 if succeed. -1 if failed.
 *        -2 indicate queue is full for this client, client should wait untill
 *	     scheduler consum some queued command.
 *	  -1 other fail.
*/
int amd_sched_push_job(struct amd_sched_job *sched_job)
{
	struct amd_sched_fence 	*fence =
		amd_sched_fence_create(sched_job->s_entity);
	if (!fence)
		return -EINVAL;
	fence_get(&fence->base);
	sched_job->s_fence = fence;
	while (kfifo_in_spinlocked(&sched_job->s_entity->job_queue,
				   &sched_job, sizeof(void *),
				   &sched_job->s_entity->queue_lock) !=
	       sizeof(void *)) {
		/**
		 * Current context used up all its IB slots
		 * wait here, or need to check whether GPU is hung
		*/
		schedule();
	}
	/* first job wake up scheduler */
	if ((kfifo_len(&sched_job->s_entity->job_queue) / sizeof(void *)) == 1)
		wake_up_interruptible(&sched_job->sched->wait_queue);
	return 0;
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
	int r;
	struct amd_sched_job *job;
	struct sched_param sparam = {.sched_priority = 1};
	struct amd_sched_entity *c_entity = NULL;
	struct amd_gpu_scheduler *sched = (struct amd_gpu_scheduler *)param;

	sched_setscheduler(current, SCHED_FIFO, &sparam);

	while (!kthread_should_stop()) {
		struct fence *fence;

		wait_event_interruptible(sched->wait_queue,
					 amd_sched_ready(sched) &&
					 (c_entity = select_context(sched)));
		r = kfifo_out(&c_entity->job_queue, &job, sizeof(void *));
		if (r != sizeof(void *))
			continue;
		r = 0;
		if (sched->ops->prepare_job)
			r = sched->ops->prepare_job(sched, c_entity, job);
		if (!r) {
			atomic_inc(&sched->hw_rq_count);
		}
		mutex_lock(&sched->sched_lock);
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
		mutex_unlock(&sched->sched_lock);
	}
	return 0;
}

/**
 * Create a gpu scheduler
 *
 * @device	The device context for this scheduler
 * @ops		The backend operations for this scheduler.
 * @id	        The scheduler is per ring, here is ring id.
 * @granularity	The minumum ms unit the scheduler will scheduled.
 * @preemption  Indicate whether this ring support preemption, 0 is no.
 *
 * return the pointer to scheduler for success, otherwise return NULL
*/
struct amd_gpu_scheduler *amd_sched_create(void *device,
					   struct amd_sched_backend_ops *ops,
					   unsigned ring,
					   unsigned granularity,
					   unsigned preemption,
					   unsigned hw_submission)
{
	struct amd_gpu_scheduler *sched;
	char name[20];

	sched = kzalloc(sizeof(struct amd_gpu_scheduler), GFP_KERNEL);
	if (!sched)
		return NULL;

	sched->device = device;
	sched->ops = ops;
	sched->granularity = granularity;
	sched->ring_id = ring;
	sched->preemption = preemption;
	sched->hw_submission_limit = hw_submission;
	snprintf(name, sizeof(name), "gpu_sched[%d]", ring);
	mutex_init(&sched->sched_lock);
	amd_sched_rq_init(&sched->sched_rq);
	amd_sched_rq_init(&sched->kernel_rq);

	init_waitqueue_head(&sched->wait_queue);
	atomic_set(&sched->hw_rq_count, 0);
	/* Each scheduler will run on a seperate kernel thread */
	sched->thread = kthread_create(amd_sched_main, sched, name);
	if (sched->thread) {
		wake_up_process(sched->thread);
		return sched;
	}

	DRM_ERROR("Failed to create scheduler for id %d.\n", ring);
	kfree(sched);
	return NULL;
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
