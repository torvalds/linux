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

#ifndef _GPU_SCHEDULER_H_
#define _GPU_SCHEDULER_H_

#include <linux/kfifo.h>

#define AMD_GPU_WAIT_IDLE_TIMEOUT_IN_MS		3000

struct amd_gpu_scheduler;
struct amd_run_queue;

/**
 * A scheduler entity is a wrapper around a job queue or a group
 * of other entities. Entities take turns emitting jobs from their 
 * job queues to corresponding hardware ring based on scheduling
 * policy.
*/
struct amd_sched_entity {
	struct list_head		list;
	struct amd_run_queue		*belongto_rq;
	struct amd_sched_entity	        *parent;
};

/**
 * Run queue is a set of entities scheduling command submissions for
 * one specific ring. It implements the scheduling policy that selects
 * the next entity to emit commands from.
*/
struct amd_run_queue {
	struct mutex			lock;
	atomic_t			nr_entity;
	struct amd_sched_entity	        head;
	struct amd_sched_entity	        *current_entity;
	/**
	 * Return 0 means this entity can be scheduled
	 * Return -1 means this entity cannot be scheduled for reasons,
	 * i.e, it is the head, or these is no job, etc
	*/
	int (*check_entity_status)(struct amd_sched_entity *entity);
};

/**
 * Context based scheduler entity, there can be multiple entities for
 * each context, and one entity per ring
*/
struct amd_context_entity {
	struct amd_sched_entity	        generic_entity;
	spinlock_t			lock;
	/* the virtual_seq is unique per context per ring */
	atomic64_t			last_queued_v_seq;
	atomic64_t			last_emitted_v_seq;
	/* the job_queue maintains the jobs submitted by clients */
	struct kfifo                    job_queue;
	spinlock_t			queue_lock;
	struct amd_gpu_scheduler	*scheduler;
	wait_queue_head_t		wait_queue;
	wait_queue_head_t		wait_emit;
	bool                            is_pending;
};

/**
 * Define the backend operations called by the scheduler,
 * these functions should be implemented in driver side
*/
struct amd_sched_backend_ops {
	int (*prepare_job)(struct amd_gpu_scheduler *sched,
			   struct amd_context_entity *c_entity,
			   void *job);
	void (*run_job)(struct amd_gpu_scheduler *sched,
			struct amd_context_entity *c_entity,
			void *job);
	void (*process_job)(struct amd_gpu_scheduler *sched, void *job);
};

/**
 * One scheduler is implemented for each hardware ring
*/
struct amd_gpu_scheduler {
	void			        *device;
	struct task_struct		*thread;
	struct amd_run_queue		sched_rq;
	struct amd_run_queue		kernel_rq;
	struct kfifo                    active_hw_rq;
	struct amd_sched_backend_ops	*ops;
	uint32_t			ring_id;
	uint32_t			granularity; /* in ms unit */
	uint32_t			preemption;
	atomic64_t			last_handled_seq;
	wait_queue_head_t		wait_queue;
	struct amd_context_entity	*current_entity;
	struct mutex			sched_lock;
	spinlock_t			queue_lock;
};


struct amd_gpu_scheduler *amd_sched_create(void *device,
				struct amd_sched_backend_ops *ops,
				uint32_t ring,
				uint32_t granularity,
				uint32_t preemption,
				uint32_t hw_submission);

int amd_sched_destroy(struct amd_gpu_scheduler *sched);

uint64_t amd_sched_push_job(struct amd_gpu_scheduler *sched,
		       struct amd_context_entity *c_entity,
		       void *job);

int amd_sched_wait_emit(struct amd_context_entity *c_entity,
			uint64_t seq,
			bool intr,
			long timeout);

void amd_sched_isr(struct amd_gpu_scheduler *sched);
uint64_t amd_sched_get_handled_seq(struct amd_gpu_scheduler *sched);

int amd_context_entity_fini(struct amd_gpu_scheduler *sched,
			    struct amd_context_entity *entity);

int amd_context_entity_init(struct amd_gpu_scheduler *sched,
			    struct amd_context_entity *entity,
			    struct amd_sched_entity *parent,
			    struct amd_run_queue *rq,
			    uint32_t jobs);

void amd_sched_emit(struct amd_context_entity *c_entity, uint64_t seq);

uint64_t amd_sched_next_queued_seq(struct amd_context_entity *c_entity);

#endif
