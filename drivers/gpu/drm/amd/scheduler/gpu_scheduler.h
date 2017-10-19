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
#include <linux/dma-fence.h>

struct amd_gpu_scheduler;
struct amd_sched_rq;

enum amd_sched_priority {
	AMD_SCHED_PRIORITY_MIN,
	AMD_SCHED_PRIORITY_LOW = AMD_SCHED_PRIORITY_MIN,
	AMD_SCHED_PRIORITY_NORMAL,
	AMD_SCHED_PRIORITY_HIGH_SW,
	AMD_SCHED_PRIORITY_HIGH_HW,
	AMD_SCHED_PRIORITY_KERNEL,
	AMD_SCHED_PRIORITY_MAX,
	AMD_SCHED_PRIORITY_INVALID = -1,
	AMD_SCHED_PRIORITY_UNSET = -2
};


/**
 * A scheduler entity is a wrapper around a job queue or a group
 * of other entities. Entities take turns emitting jobs from their
 * job queues to corresponding hardware ring based on scheduling
 * policy.
*/
struct amd_sched_entity {
	struct list_head		list;
	struct amd_sched_rq		*rq;
	spinlock_t			rq_lock;
	struct amd_gpu_scheduler	*sched;

	spinlock_t			queue_lock;
	struct kfifo                    job_queue;

	atomic_t			fence_seq;
	uint64_t                        fence_context;

	struct dma_fence		*dependency;
	struct dma_fence_cb		cb;
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
	struct dma_fence                scheduled;
	struct dma_fence                finished;
	struct dma_fence_cb             cb;
	struct dma_fence                *parent;
	struct amd_gpu_scheduler	*sched;
	spinlock_t			lock;
	void                            *owner;
};

struct amd_sched_job {
	struct amd_gpu_scheduler        *sched;
	struct amd_sched_entity         *s_entity;
	struct amd_sched_fence          *s_fence;
	struct dma_fence_cb		finish_cb;
	struct work_struct		finish_work;
	struct list_head		node;
	struct delayed_work		work_tdr;
	uint64_t			id;
	atomic_t karma;
	enum amd_sched_priority s_priority;
};

extern const struct dma_fence_ops amd_sched_fence_ops_scheduled;
extern const struct dma_fence_ops amd_sched_fence_ops_finished;
static inline struct amd_sched_fence *to_amd_sched_fence(struct dma_fence *f)
{
	if (f->ops == &amd_sched_fence_ops_scheduled)
		return container_of(f, struct amd_sched_fence, scheduled);

	if (f->ops == &amd_sched_fence_ops_finished)
		return container_of(f, struct amd_sched_fence, finished);

	return NULL;
}

static inline bool amd_sched_invalidate_job(struct amd_sched_job *s_job, int threshold)
{
	return (s_job && atomic_inc_return(&s_job->karma) > threshold);
}

/**
 * Define the backend operations called by the scheduler,
 * these functions should be implemented in driver side
*/
struct amd_sched_backend_ops {
	struct dma_fence *(*dependency)(struct amd_sched_job *sched_job);
	struct dma_fence *(*run_job)(struct amd_sched_job *sched_job);
	void (*timedout_job)(struct amd_sched_job *sched_job);
	void (*free_job)(struct amd_sched_job *sched_job);
};

/**
 * One scheduler is implemented for each hardware ring
*/
struct amd_gpu_scheduler {
	const struct amd_sched_backend_ops	*ops;
	uint32_t			hw_submission_limit;
	long				timeout;
	const char			*name;
	struct amd_sched_rq		sched_rq[AMD_SCHED_PRIORITY_MAX];
	wait_queue_head_t		wake_up_worker;
	wait_queue_head_t		job_scheduled;
	atomic_t			hw_rq_count;
	atomic64_t			job_id_count;
	struct task_struct		*thread;
	struct list_head	ring_mirror_list;
	spinlock_t			job_list_lock;
};

int amd_sched_init(struct amd_gpu_scheduler *sched,
		   const struct amd_sched_backend_ops *ops,
		   uint32_t hw_submission, long timeout, const char *name);
void amd_sched_fini(struct amd_gpu_scheduler *sched);

int amd_sched_entity_init(struct amd_gpu_scheduler *sched,
			  struct amd_sched_entity *entity,
			  struct amd_sched_rq *rq,
			  uint32_t jobs);
void amd_sched_entity_fini(struct amd_gpu_scheduler *sched,
			   struct amd_sched_entity *entity);
void amd_sched_entity_push_job(struct amd_sched_job *sched_job);
void amd_sched_entity_set_rq(struct amd_sched_entity *entity,
			     struct amd_sched_rq *rq);

int amd_sched_fence_slab_init(void);
void amd_sched_fence_slab_fini(void);

struct amd_sched_fence *amd_sched_fence_create(
	struct amd_sched_entity *s_entity, void *owner);
void amd_sched_fence_scheduled(struct amd_sched_fence *fence);
void amd_sched_fence_finished(struct amd_sched_fence *fence);
int amd_sched_job_init(struct amd_sched_job *job,
		       struct amd_gpu_scheduler *sched,
		       struct amd_sched_entity *entity,
		       void *owner);
void amd_sched_hw_job_reset(struct amd_gpu_scheduler *sched);
void amd_sched_job_recovery(struct amd_gpu_scheduler *sched);
bool amd_sched_dependency_optimized(struct dma_fence* fence,
				    struct amd_sched_entity *entity);
void amd_sched_job_kickout(struct amd_sched_job *s_job);

#endif
