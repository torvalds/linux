/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2025 Valve Corporation */

#ifndef _SCHED_TESTS_H_
#define _SCHED_TESTS_H_

#include <kunit/test.h>
#include <linux/atomic.h>
#include <linux/completion.h>
#include <linux/dma-fence.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/types.h>

#include <drm/gpu_scheduler.h>

/*
 * DOC: Mock DRM scheduler data structures
 *
 * drm_mock_* data structures are used to implement a mock "GPU".
 *
 * They subclass the core DRM scheduler objects and add their data on top, which
 * enables tracking the submitted jobs and simulating their execution with the
 * attributes as specified by the test case.
 */

/**
 * struct drm_mock_scheduler - implements a trivial mock GPU execution engine
 *
 * @base: DRM scheduler base class
 * @test: Backpointer to owning the kunit test case
 * @lock: Lock to protect the simulated @hw_timeline, @job_list and @done_list
 * @job_list: List of jobs submitted to the mock GPU
 * @done_list: List of jobs completed by the mock GPU
 * @hw_timeline: Simulated hardware timeline has a @context, @next_seqno and
 *		 @cur_seqno for implementing a struct dma_fence signaling the
 *		 simulated job completion.
 *
 * Trivial mock GPU execution engine tracks submitted jobs and enables
 * completing them strictly in submission order.
 */
struct drm_mock_scheduler {
	struct drm_gpu_scheduler base;

	struct kunit		*test;

	spinlock_t		lock;
	struct list_head	job_list;

	struct {
		u64		context;
		atomic_t	next_seqno;
		unsigned int	cur_seqno;
	} hw_timeline;
};

/**
 * struct drm_mock_sched_entity - implements a mock GPU sched entity
 *
 * @base: DRM scheduler entity base class
 * @test: Backpointer to owning the kunit test case
 *
 * Mock GPU sched entity is used by the test cases to submit jobs to the mock
 * scheduler.
 */
struct drm_mock_sched_entity {
	struct drm_sched_entity base;

	struct kunit		*test;
};

/**
 * struct drm_mock_sched_job - implements a mock GPU job
 *
 * @base: DRM sched job base class
 * @done: Completion signaling job completion.
 * @flags: Flags designating job state.
 * @link: List head element used by job tracking by the drm_mock_scheduler
 * @timer: Timer used for simulating job execution duration
 * @duration_us: Simulated job duration in micro seconds, or zero if in manual
 *		 timeline advance mode
 * @finish_at: Absolute time when the jobs with set duration will complete
 * @lock: Lock used for @hw_fence
 * @hw_fence: Fence returned to DRM scheduler as the hardware fence
 * @test: Backpointer to owning the kunit test case
 *
 * Mock GPU sched job is used by the test cases to submit jobs to the mock
 * scheduler.
 */
struct drm_mock_sched_job {
	struct drm_sched_job	base;

	struct completion	done;

#define DRM_MOCK_SCHED_JOB_DONE			0x1
#define DRM_MOCK_SCHED_JOB_TIMEDOUT		0x2
#define DRM_MOCK_SCHED_JOB_DONT_RESET		0x4
#define DRM_MOCK_SCHED_JOB_RESET_SKIPPED	0x8
	unsigned long		flags;

	struct list_head	link;
	struct hrtimer		timer;

	unsigned int		duration_us;
	ktime_t			finish_at;

	struct dma_fence	hw_fence;

	struct kunit		*test;
};

static inline struct drm_mock_scheduler *
drm_sched_to_mock_sched(struct drm_gpu_scheduler *sched)
{
	return container_of(sched, struct drm_mock_scheduler, base);
};

static inline struct drm_mock_sched_entity *
drm_sched_entity_to_mock_entity(struct drm_sched_entity *sched_entity)
{
	return container_of(sched_entity, struct drm_mock_sched_entity, base);
};

static inline struct drm_mock_sched_job *
drm_sched_job_to_mock_job(struct drm_sched_job *sched_job)
{
	return container_of(sched_job, struct drm_mock_sched_job, base);
};

struct drm_mock_scheduler *drm_mock_sched_new(struct kunit *test,
					      long timeout);
void drm_mock_sched_fini(struct drm_mock_scheduler *sched);
unsigned int drm_mock_sched_advance(struct drm_mock_scheduler *sched,
				    unsigned int num);

struct drm_mock_sched_entity *
drm_mock_sched_entity_new(struct kunit *test,
			  enum drm_sched_priority priority,
			  struct drm_mock_scheduler *sched);
void drm_mock_sched_entity_free(struct drm_mock_sched_entity *entity);

struct drm_mock_sched_job *
drm_mock_sched_job_new(struct kunit *test,
		       struct drm_mock_sched_entity *entity);

/**
 * drm_mock_sched_job_submit - Arm and submit a job in one go
 *
 * @job: Job to arm and submit
 */
static inline void drm_mock_sched_job_submit(struct drm_mock_sched_job *job)
{
	drm_sched_job_arm(&job->base);
	drm_sched_entity_push_job(&job->base);
}

/**
 * drm_mock_sched_job_set_duration_us - Set a job duration
 *
 * @job: Job to set the duration for
 * @duration_us: Duration in micro seconds
 *
 * Jobs with duration set will be automatically completed by the mock scheduler
 * as the timeline progresses, unless a job without a set duration is
 * encountered in the timelime in which case calling drm_mock_sched_advance()
 * will be required to bump the timeline.
 */
static inline void
drm_mock_sched_job_set_duration_us(struct drm_mock_sched_job *job,
				   unsigned int duration_us)
{
	job->duration_us = duration_us;
}

/**
 * drm_mock_sched_job_is_finished - Check if a job is finished
 *
 * @job: Job to check
 *
 * Returns: true if finished
 */
static inline bool
drm_mock_sched_job_is_finished(struct drm_mock_sched_job *job)
{
	return job->flags & DRM_MOCK_SCHED_JOB_DONE;
}

/**
 * drm_mock_sched_job_wait_finished - Wait until a job is finished
 *
 * @job: Job to wait for
 * @timeout: Wait time in jiffies
 *
 * Returns: true if finished within the timeout provided, otherwise false
 */
static inline bool
drm_mock_sched_job_wait_finished(struct drm_mock_sched_job *job, long timeout)
{
	if (job->flags & DRM_MOCK_SCHED_JOB_DONE)
		return true;

	return wait_for_completion_timeout(&job->done, timeout) != 0;
}

/**
 * drm_mock_sched_job_wait_scheduled - Wait until a job is scheduled
 *
 * @job: Job to wait for
 * @timeout: Wait time in jiffies
 *
 * Returns: true if scheduled within the timeout provided, otherwise false
 */
static inline bool
drm_mock_sched_job_wait_scheduled(struct drm_mock_sched_job *job, long timeout)
{
	KUNIT_ASSERT_EQ(job->test, job->flags & DRM_MOCK_SCHED_JOB_DONE, 0);

	return dma_fence_wait_timeout(&job->base.s_fence->scheduled,
				      false,
				      timeout) != 0;
}

#endif
