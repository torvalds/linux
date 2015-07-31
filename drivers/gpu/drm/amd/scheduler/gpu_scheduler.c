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
static void init_rq(struct amd_run_queue *rq)
{
	INIT_LIST_HEAD(&rq->head.list);
	rq->head.belongto_rq = rq;
	mutex_init(&rq->lock);
	atomic_set(&rq->nr_entity, 0);
	rq->current_entity = &rq->head;
}

/* Note: caller must hold the lock or in a atomic context */
static void rq_remove_entity(struct amd_run_queue *rq,
			     struct amd_sched_entity *entity)
{
	if (rq->current_entity == entity)
		rq->current_entity = list_entry(entity->list.prev,
						typeof(*entity), list);
	list_del_init(&entity->list);
	atomic_dec(&rq->nr_entity);
}

static void rq_add_entity(struct amd_run_queue *rq,
			  struct amd_sched_entity *entity)
{
	list_add_tail(&entity->list, &rq->head.list);
	atomic_inc(&rq->nr_entity);
}

/**
 * Select next entity from a specified run queue with round robin policy.
 * It could return the same entity as current one if current is the only
 * available one in the queue. Return NULL if nothing available.
 */
static struct amd_sched_entity *rq_select_entity(struct amd_run_queue *rq)
{
	struct amd_sched_entity *p = rq->current_entity;
	int i = atomic_read(&rq->nr_entity) + 1; /*real count + dummy head*/
	while (i) {
		p = list_entry(p->list.next, typeof(*p), list);
		if (!rq->check_entity_status(p)) {
			rq->current_entity = p;
			break;
		}
		i--;
	}
	return i ? p : NULL;
}

static bool context_entity_is_waiting(struct amd_context_entity *entity)
{
	/* TODO: sync obj for multi-ring synchronization */
	return false;
}

static int gpu_entity_check_status(struct amd_sched_entity *entity)
{
	struct amd_context_entity *tmp = NULL;

	if (entity == &entity->belongto_rq->head)
		return -1;

	tmp = container_of(entity, typeof(*tmp), generic_entity);
	if (kfifo_is_empty(&tmp->job_queue) ||
	    context_entity_is_waiting(tmp))
		return -1;

	return 0;
}

/**
 * Note: This function should only been called inside scheduler main
 * function for thread safety, there is no other protection here.
 * return ture if scheduler has something ready to run.
 *
 * For active_hw_rq, there is only one producer(scheduler thread) and
 * one consumer(ISR). It should be safe to use this function in scheduler
 * main thread to decide whether to continue emit more IBs.
*/
static bool is_scheduler_ready(struct amd_gpu_scheduler *sched)
{
	return !kfifo_is_full(&sched->active_hw_rq);
}

/**
 * Select next entity from the kernel run queue, if not available,
 * return null.
*/
static struct amd_context_entity *kernel_rq_select_context(
	struct amd_gpu_scheduler *sched)
{
	struct amd_sched_entity *sched_entity = NULL;
	struct amd_context_entity *tmp = NULL;
	struct amd_run_queue *rq = &sched->kernel_rq;

	mutex_lock(&rq->lock);
	sched_entity = rq_select_entity(rq);
	if (sched_entity)
		tmp = container_of(sched_entity,
				   typeof(*tmp),
				   generic_entity);
	mutex_unlock(&rq->lock);
	return tmp;
}

/**
 * Select next entity containing real IB submissions
*/
static struct amd_context_entity *select_context(
	struct amd_gpu_scheduler *sched)
{
	struct amd_context_entity *wake_entity = NULL;
	struct amd_context_entity *tmp;
	struct amd_run_queue *rq;

	if (!is_scheduler_ready(sched))
		return NULL;

	/* Kernel run queue has higher priority than normal run queue*/
	tmp = kernel_rq_select_context(sched);
	if (tmp != NULL)
		goto exit;

	WARN_ON(offsetof(struct amd_context_entity, generic_entity) != 0);

	rq = &sched->sched_rq;
	mutex_lock(&rq->lock);
	tmp = container_of(rq_select_entity(rq),
			   typeof(*tmp), generic_entity);
	mutex_unlock(&rq->lock);
exit:
	if (sched->current_entity && (sched->current_entity != tmp))
		wake_entity = sched->current_entity;
	sched->current_entity = tmp;
	if (wake_entity)
		wake_up(&wake_entity->wait_queue);
	return tmp;
}

/**
 * Init a context entity used by scheduler when submit to HW ring.
 *
 * @sched	The pointer to the scheduler
 * @entity	The pointer to a valid amd_context_entity
 * @parent	The parent entity of this amd_context_entity
 * @rq		The run queue this entity belongs
 * @context_id	The context id for this entity
 * @jobs	The max number of jobs in the job queue
 *
 * return 0 if succeed. negative error code on failure
*/
int amd_context_entity_init(struct amd_gpu_scheduler *sched,
			    struct amd_context_entity *entity,
			    struct amd_sched_entity *parent,
			    struct amd_run_queue *rq,
			    uint32_t context_id,
			    uint32_t jobs)
{
	uint64_t seq_ring = 0;

	if (!(sched && entity && rq))
		return -EINVAL;

	memset(entity, 0, sizeof(struct amd_context_entity));
	seq_ring = ((uint64_t)sched->ring_id) << 60;
	spin_lock_init(&entity->lock);
	entity->generic_entity.belongto_rq = rq;
	entity->generic_entity.parent = parent;
	entity->scheduler = sched;
	init_waitqueue_head(&entity->wait_queue);
	init_waitqueue_head(&entity->wait_emit);
	if(kfifo_alloc(&entity->job_queue,
		       jobs * sizeof(void *),
		       GFP_KERNEL))
		return -EINVAL;

	spin_lock_init(&entity->queue_lock);
	entity->tgid = (context_id == AMD_KERNEL_CONTEXT_ID) ?
		AMD_KERNEL_PROCESS_ID : current->tgid;
	entity->context_id = context_id;
	atomic64_set(&entity->last_emitted_v_seq, seq_ring);
	atomic64_set(&entity->last_queued_v_seq, seq_ring);

	/* Add the entity to the run queue */
	mutex_lock(&rq->lock);
	rq_add_entity(rq, &entity->generic_entity);
	mutex_unlock(&rq->lock);
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
					  struct amd_context_entity *entity)
{
	return entity->scheduler == sched &&
		entity->generic_entity.belongto_rq != NULL;
}

static bool is_context_entity_idle(struct amd_gpu_scheduler *sched,
				   struct amd_context_entity *entity)
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
int amd_context_entity_fini(struct amd_gpu_scheduler *sched,
			    struct amd_context_entity *entity)
{
	int r = 0;
	struct amd_run_queue *rq = entity->generic_entity.belongto_rq;

	if (!is_context_entity_initialized(sched, entity))
		return 0;

	/**
	 * The client will not queue more IBs during this fini, consume existing
	 * queued IBs
	*/
	r = wait_event_timeout(
		entity->wait_queue,
		is_context_entity_idle(sched, entity),
		msecs_to_jiffies(AMD_GPU_WAIT_IDLE_TIMEOUT_IN_MS)
		) ?  0 : -1;

	if (r) {
		if (entity->is_pending)
			DRM_INFO("Entity %u is in waiting state during fini,\
				all pending ibs will be canceled.\n",
				 entity->context_id);
	}

	mutex_lock(&rq->lock);
	rq_remove_entity(rq, &entity->generic_entity);
	mutex_unlock(&rq->lock);
	kfifo_free(&entity->job_queue);
	return r;
}

/**
 * Submit a normal job to the job queue
 *
 * @sched	The pointer to the scheduler
 * @c_entity    The pointer to amd_context_entity
 * @job		The pointer to job required to submit
 * return 0 if succeed. -1 if failed.
 *        -2 indicate queue is full for this client, client should wait untill
 *	     scheduler consum some queued command.
 *	  -1 other fail.
*/
int amd_sched_push_job(struct amd_gpu_scheduler *sched,
		       struct amd_context_entity *c_entity,
		       void *job)
{
	while (kfifo_in_spinlocked(&c_entity->job_queue, &job, sizeof(void *),
				   &c_entity->queue_lock) != sizeof(void *)) {
		/**
		 * Current context used up all its IB slots
		 * wait here, or need to check whether GPU is hung
		*/
		schedule();
	}

	wake_up_interruptible(&sched->wait_queue);
	return 0;
}

/**
 * Wait for a virtual sequence number to be emitted.
 *
 * @c_entity	The pointer to a valid context entity
 * @seq         The virtual sequence number to wait
 * @intr	Interruptible or not
 * @timeout	Timeout in ms, wait infinitely if <0
 * @emit        wait for emit or signal
 *
 * return =0 signaled ,  <0 failed
*/
int amd_sched_wait_emit(struct amd_context_entity *c_entity,
			uint64_t seq,
			bool intr,
			long timeout)
{
	atomic64_t *v_seq = &c_entity->last_emitted_v_seq;
	wait_queue_head_t *wait_queue = &c_entity->wait_emit;

	if (intr && (timeout < 0)) {
		wait_event_interruptible(
			*wait_queue,
			seq <= atomic64_read(v_seq));
		return 0;
	} else if (intr && (timeout >= 0)) {
		wait_event_interruptible_timeout(
			*wait_queue,
			seq <= atomic64_read(v_seq),
			msecs_to_jiffies(timeout));
		return (seq <= atomic64_read(v_seq)) ?
			0 : -1;
	} else if (!intr && (timeout < 0)) {
		wait_event(
			*wait_queue,
			seq <= atomic64_read(v_seq));
		return 0;
	} else if (!intr && (timeout >= 0)) {
		wait_event_timeout(
			*wait_queue,
			seq <= atomic64_read(v_seq),
			msecs_to_jiffies(timeout));
		return (seq <= atomic64_read(v_seq)) ?
			0 : -1;
	}
	return 0;
}

static int amd_sched_main(void *param)
{
	int r;
	void *job;
	struct sched_param sparam = {.sched_priority = 1};
	struct amd_context_entity *c_entity = NULL;
	struct amd_gpu_scheduler *sched = (struct amd_gpu_scheduler *)param;

	sched_setscheduler(current, SCHED_FIFO, &sparam);

	while (!kthread_should_stop()) {
		wait_event_interruptible(sched->wait_queue,
					 is_scheduler_ready(sched) &&
					 (c_entity = select_context(sched)));
		r = kfifo_out(&c_entity->job_queue, &job, sizeof(void *));
		if (r != sizeof(void *))
			continue;
		r = sched->ops->prepare_job(sched, c_entity, job);
		if (!r)
			WARN_ON(kfifo_in_spinlocked(
					&sched->active_hw_rq,
					&job,
					sizeof(void *),
					&sched->queue_lock) != sizeof(void *));
		mutex_lock(&sched->sched_lock);
		sched->ops->run_job(sched, c_entity, job);
		mutex_unlock(&sched->sched_lock);
	}
	return 0;
}

uint64_t amd_sched_get_handled_seq(struct amd_gpu_scheduler *sched)
{
	return sched->last_handled_seq;
}

/**
 * ISR to handle EOP inetrrupts
 *
 * @sched: gpu scheduler
 *
*/
void amd_sched_isr(struct amd_gpu_scheduler *sched)
{
	int r;
	void *job;
	r = kfifo_out_spinlocked(&sched->active_hw_rq,
				 &job, sizeof(void *),
				 &sched->queue_lock);

	if (r != sizeof(void *))
		job = NULL;

	sched->ops->process_job(sched, job);
	sched->last_handled_seq++;
	wake_up_interruptible(&sched->wait_queue);
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
	char name[20] = "gpu_sched[0]";

	sched = kzalloc(sizeof(struct amd_gpu_scheduler), GFP_KERNEL);
	if (!sched)
		return NULL;

	sched->device = device;
	sched->ops = ops;
	sched->granularity = granularity;
	sched->ring_id = ring;
	sched->preemption = preemption;
	sched->last_handled_seq = 0;

	snprintf(name, sizeof(name), "gpu_sched[%d]", ring);
	mutex_init(&sched->sched_lock);
	spin_lock_init(&sched->queue_lock);
	init_rq(&sched->sched_rq);
	sched->sched_rq.check_entity_status = gpu_entity_check_status;

	init_rq(&sched->kernel_rq);
	sched->kernel_rq.check_entity_status = gpu_entity_check_status;

	init_waitqueue_head(&sched->wait_queue);
	if(kfifo_alloc(&sched->active_hw_rq,
		       hw_submission * sizeof(void *),
		       GFP_KERNEL)) {
		kfree(sched);
		return NULL;
	}

	/* Each scheduler will run on a seperate kernel thread */
	sched->thread = kthread_create(amd_sched_main, sched, name);
	if (sched->thread) {
		wake_up_process(sched->thread);
		return sched;
	}

	DRM_ERROR("Failed to create scheduler for id %d.\n", ring);
	kfifo_free(&sched->active_hw_rq);
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
	kfifo_free(&sched->active_hw_rq);
	kfree(sched);
	return  0;
}

