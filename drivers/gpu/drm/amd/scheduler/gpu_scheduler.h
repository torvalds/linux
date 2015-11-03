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
#include <linux/fence.h>

struct amd_gpu_scheduler;
struct amd_sched_rq;

/**
 * A scheduler entity is a wrapper around a job queue or a group
 * of other entities. Entities take turns emitting jobs from their 
 * job queues to corresponding hardware ring based on scheduling
 * policy.
*/
struct amd_sched_entity {
	struct list_head		list;
	struct amd_sched_rq		*rq;
	struct amd_gpu_scheduler	*sched;

	spinlock_t			queue_lock;
	struct kfifo                    job_queue;

	atomic_t			fence_seq;
	uint64_t                        fence_context;

	struct fence			*dependency;
	struct fence_cb			cb;
};

/**
 * Run queue is a set of entities scheduling command submissions for
 * one specific ring. It implements the scheduling policy that selects
 * the next entity to emit commands from.
*/
struct amd_sched_rq {
	spinlock_t		lock;
	struct list_head	entities;
	struct amd_sched_entity	*current_entity;
};

struct amd_sched_fence {
	struct fence                    base;
	struct fence_cb                 cb;
	struct amd_gpu_scheduler	*sched;
	spinlock_t			lock;
	void                            *owner;
};

struct amd_sched_job {
	struct amd_gpu_scheduler        *sched;
	struct amd_sched_entity         *s_entity;
	struct amd_sched_fence          *s_fence;
	void		                *owner;
};

extern const struct fence_ops amd_sched_fence_ops;
static inline struct amd_sched_fence *to_amd_sched_fence(struct fence *f)
{
	struct amd_sched_fence *__f = container_of(f, struct amd_sched_fence, base);

	if (__f->base.ops == &amd_sched_fence_ops)
		return __f;

	return NULL;
}

/**
 * Define the backend operations called by the scheduler,
 * these functions should be implemented in driver side
*/
struct amd_sched_backend_ops {
	struct fence *(*dependency)(struct amd_sched_job *sched_job);
	struct fence *(*run_job)(struct amd_sched_job *sched_job);
};

/**
 * One scheduler is implemented for each hardware ring
*/
struct amd_gpu_scheduler {
	struct amd_sched_backend_ops	*ops;
	uint32_t			hw_submission_limit;
	const char			*name;
	struct amd_sched_rq		sched_rq;
	struct amd_sched_rq		kernel_rq;
	wait_queue_head_t		wake_up_worker;
	wait_queue_head_t		job_scheduled;
	atomic_t			hw_rq_count;
	struct task_struct		*thread;
};

int amd_sched_init(struct amd_gpu_scheduler *sched,
		   struct amd_sched_backend_ops *ops,
		   uint32_t hw_submission, const char *name);
void amd_sched_fini(struct amd_gpu_scheduler *sched);

int amd_sched_entity_init(struct amd_gpu_scheduler *sched,
			  struct amd_sched_entity *entity,
			  struct amd_sched_rq *rq,
			  uint32_t jobs);
void amd_sched_entity_fini(struct amd_gpu_scheduler *sched,
			   struct amd_sched_entity *entity);
int amd_sched_entity_push_job(struct amd_sched_job *sched_job);

struct amd_sched_fence *amd_sched_fence_create(
	struct amd_sched_entity *s_entity, void *owner);
void amd_sched_fence_signal(struct amd_sched_fence *fence);


#endif
