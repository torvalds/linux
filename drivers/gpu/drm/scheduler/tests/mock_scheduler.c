// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Valve Corporation */

#include "sched_tests.h"

/*
 * Here we implement the mock "GPU" (or the scheduler backend) which is used by
 * the DRM scheduler unit tests in order to exercise the core functionality.
 *
 * Test cases are implemented in a separate file.
 */

/**
 * drm_mock_sched_entity_new - Create a new mock scheduler entity
 *
 * @test: KUnit test owning the entity
 * @priority: Scheduling priority
 * @sched: Mock scheduler on which the entity can be scheduled
 *
 * Returns: New mock scheduler entity with allocation managed by the test
 */
struct drm_mock_sched_entity *
drm_mock_sched_entity_new(struct kunit *test,
			  enum drm_sched_priority priority,
			  struct drm_mock_scheduler *sched)
{
	struct drm_mock_sched_entity *entity;
	struct drm_gpu_scheduler *drm_sched;
	int ret;

	entity = kunit_kzalloc(test, sizeof(*entity), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, entity);

	drm_sched = &sched->base;
	ret = drm_sched_entity_init(&entity->base,
				    priority,
				    &drm_sched, 1,
				    NULL);
	KUNIT_ASSERT_EQ(test, ret, 0);

	entity->test = test;

	return entity;
}

/**
 * drm_mock_sched_entity_free - Destroys a mock scheduler entity
 *
 * @entity: Entity to destroy
 *
 * To be used from the test cases once done with the entity.
 */
void drm_mock_sched_entity_free(struct drm_mock_sched_entity *entity)
{
	drm_sched_entity_destroy(&entity->base);
}

static void drm_mock_sched_job_complete(struct drm_mock_sched_job *job)
{
	struct drm_mock_scheduler *sched =
		drm_sched_to_mock_sched(job->base.sched);

	lockdep_assert_held(&sched->lock);

	job->flags |= DRM_MOCK_SCHED_JOB_DONE;
	list_del(&job->link);
	dma_fence_signal_locked(&job->hw_fence);
	complete(&job->done);
}

static enum hrtimer_restart
drm_mock_sched_job_signal_timer(struct hrtimer *hrtimer)
{
	struct drm_mock_sched_job *job =
		container_of(hrtimer, typeof(*job), timer);
	struct drm_mock_scheduler *sched =
		drm_sched_to_mock_sched(job->base.sched);
	struct drm_mock_sched_job *next;
	ktime_t now = ktime_get();
	unsigned long flags;
	LIST_HEAD(signal);

	spin_lock_irqsave(&sched->lock, flags);
	list_for_each_entry_safe(job, next, &sched->job_list, link) {
		if (!job->duration_us)
			break;

		if (ktime_before(now, job->finish_at))
			break;

		sched->hw_timeline.cur_seqno = job->hw_fence.seqno;
		drm_mock_sched_job_complete(job);
	}
	spin_unlock_irqrestore(&sched->lock, flags);

	return HRTIMER_NORESTART;
}

/**
 * drm_mock_sched_job_new - Create a new mock scheduler job
 *
 * @test: KUnit test owning the job
 * @entity: Scheduler entity of the job
 *
 * Returns: New mock scheduler job with allocation managed by the test
 */
struct drm_mock_sched_job *
drm_mock_sched_job_new(struct kunit *test,
		       struct drm_mock_sched_entity *entity)
{
	struct drm_mock_sched_job *job;
	int ret;

	job = kunit_kzalloc(test, sizeof(*job), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, job);

	ret = drm_sched_job_init(&job->base,
				 &entity->base,
				 1,
				 NULL,
				 1);
	KUNIT_ASSERT_EQ(test, ret, 0);

	job->test = test;

	init_completion(&job->done);
	INIT_LIST_HEAD(&job->link);
	hrtimer_setup(&job->timer, drm_mock_sched_job_signal_timer,
		      CLOCK_MONOTONIC, HRTIMER_MODE_ABS);

	return job;
}

static const char *drm_mock_sched_hw_fence_driver_name(struct dma_fence *fence)
{
	return "drm_mock_sched";
}

static const char *
drm_mock_sched_hw_fence_timeline_name(struct dma_fence *fence)
{
	struct drm_mock_sched_job *job =
		container_of(fence, typeof(*job), hw_fence);

	return (const char *)job->base.sched->name;
}

static void drm_mock_sched_hw_fence_release(struct dma_fence *fence)
{
	struct drm_mock_sched_job *job =
		container_of(fence, typeof(*job), hw_fence);

	hrtimer_cancel(&job->timer);

	/* Containing job is freed by the kunit framework */
}

static const struct dma_fence_ops drm_mock_sched_hw_fence_ops = {
	.get_driver_name = drm_mock_sched_hw_fence_driver_name,
	.get_timeline_name = drm_mock_sched_hw_fence_timeline_name,
	.release = drm_mock_sched_hw_fence_release,
};

static struct dma_fence *mock_sched_run_job(struct drm_sched_job *sched_job)
{
	struct drm_mock_scheduler *sched =
		drm_sched_to_mock_sched(sched_job->sched);
	struct drm_mock_sched_job *job = drm_sched_job_to_mock_job(sched_job);

	dma_fence_init(&job->hw_fence,
		       &drm_mock_sched_hw_fence_ops,
		       &sched->lock,
		       sched->hw_timeline.context,
		       atomic_inc_return(&sched->hw_timeline.next_seqno));

	dma_fence_get(&job->hw_fence); /* Reference for the job_list */

	spin_lock_irq(&sched->lock);
	if (job->duration_us) {
		ktime_t prev_finish_at = 0;

		if (!list_empty(&sched->job_list)) {
			struct drm_mock_sched_job *prev =
				list_last_entry(&sched->job_list, typeof(*prev),
						link);

			prev_finish_at = prev->finish_at;
		}

		if (!prev_finish_at)
			prev_finish_at = ktime_get();

		job->finish_at = ktime_add_us(prev_finish_at, job->duration_us);
	}
	list_add_tail(&job->link, &sched->job_list);
	if (job->finish_at)
		hrtimer_start(&job->timer, job->finish_at, HRTIMER_MODE_ABS);
	spin_unlock_irq(&sched->lock);

	return &job->hw_fence;
}

/*
 * Normally, drivers would take appropriate measures in this callback, such as
 * killing the entity the faulty job is associated with, resetting the hardware
 * and / or resubmitting non-faulty jobs.
 *
 * For the mock scheduler, there are no hardware rings to be resetted nor jobs
 * to be resubmitted. Thus, this function merely ensures that
 *   a) timedout fences get signaled properly and removed from the pending list
 *   b) the mock scheduler framework gets informed about the timeout via a flag
 *   c) The drm_sched_job, not longer needed, gets freed
 */
static enum drm_gpu_sched_stat
mock_sched_timedout_job(struct drm_sched_job *sched_job)
{
	struct drm_mock_scheduler *sched = drm_sched_to_mock_sched(sched_job->sched);
	struct drm_mock_sched_job *job = drm_sched_job_to_mock_job(sched_job);
	unsigned long flags;

	if (job->flags & DRM_MOCK_SCHED_JOB_DONT_RESET) {
		job->flags |= DRM_MOCK_SCHED_JOB_RESET_SKIPPED;
		return DRM_GPU_SCHED_STAT_NO_HANG;
	}

	spin_lock_irqsave(&sched->lock, flags);
	if (!dma_fence_is_signaled_locked(&job->hw_fence)) {
		list_del(&job->link);
		job->flags |= DRM_MOCK_SCHED_JOB_TIMEDOUT;
		dma_fence_set_error(&job->hw_fence, -ETIMEDOUT);
		dma_fence_signal_locked(&job->hw_fence);
	}
	spin_unlock_irqrestore(&sched->lock, flags);

	dma_fence_put(&job->hw_fence);
	drm_sched_job_cleanup(sched_job);
	/* Mock job itself is freed by the kunit framework. */

	return DRM_GPU_SCHED_STAT_RESET;
}

static void mock_sched_free_job(struct drm_sched_job *sched_job)
{
	struct drm_mock_sched_job *job = drm_sched_job_to_mock_job(sched_job);

	dma_fence_put(&job->hw_fence);
	drm_sched_job_cleanup(sched_job);

	/* Mock job itself is freed by the kunit framework. */
}

static void mock_sched_cancel_job(struct drm_sched_job *sched_job)
{
	struct drm_mock_scheduler *sched = drm_sched_to_mock_sched(sched_job->sched);
	struct drm_mock_sched_job *job = drm_sched_job_to_mock_job(sched_job);
	unsigned long flags;

	hrtimer_cancel(&job->timer);

	spin_lock_irqsave(&sched->lock, flags);
	if (!dma_fence_is_signaled_locked(&job->hw_fence)) {
		list_del(&job->link);
		dma_fence_set_error(&job->hw_fence, -ECANCELED);
		dma_fence_signal_locked(&job->hw_fence);
	}
	spin_unlock_irqrestore(&sched->lock, flags);

	/*
	 * The GPU Scheduler will call drm_sched_backend_ops.free_job(), still.
	 * Mock job itself is freed by the kunit framework.
	 */
}

static const struct drm_sched_backend_ops drm_mock_scheduler_ops = {
	.run_job = mock_sched_run_job,
	.timedout_job = mock_sched_timedout_job,
	.free_job = mock_sched_free_job,
	.cancel_job = mock_sched_cancel_job,
};

/**
 * drm_mock_sched_new - Create a new mock scheduler
 *
 * @test: KUnit test owning the job
 * @timeout: Job timeout to set
 *
 * Returns: New mock scheduler with allocation managed by the test
 */
struct drm_mock_scheduler *drm_mock_sched_new(struct kunit *test, long timeout)
{
	struct drm_sched_init_args args = {
		.ops		= &drm_mock_scheduler_ops,
		.num_rqs	= DRM_SCHED_PRIORITY_COUNT,
		.credit_limit	= U32_MAX,
		.hang_limit	= 1,
		.timeout	= timeout,
		.name		= "drm-mock-scheduler",
	};
	struct drm_mock_scheduler *sched;
	int ret;

	sched = kunit_kzalloc(test, sizeof(*sched), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, sched);

	ret = drm_sched_init(&sched->base, &args);
	KUNIT_ASSERT_EQ(test, ret, 0);

	sched->test = test;
	sched->hw_timeline.context = dma_fence_context_alloc(1);
	atomic_set(&sched->hw_timeline.next_seqno, 0);
	INIT_LIST_HEAD(&sched->job_list);
	spin_lock_init(&sched->lock);

	return sched;
}

/**
 * drm_mock_sched_fini - Destroys a mock scheduler
 *
 * @sched: Scheduler to destroy
 *
 * To be used from the test cases once done with the scheduler.
 */
void drm_mock_sched_fini(struct drm_mock_scheduler *sched)
{
	drm_sched_fini(&sched->base);
}

/**
 * drm_mock_sched_advance - Advances the mock scheduler timeline
 *
 * @sched: Scheduler timeline to advance
 * @num: By how many jobs to advance
 *
 * Advancing the scheduler timeline by a number of seqnos will trigger
 * signalling of the hardware fences and unlinking the jobs from the internal
 * scheduler tracking.
 *
 * This can be used from test cases which want complete control of the simulated
 * job execution timing. For example submitting one job with no set duration
 * would never complete it before test cases advances the timeline by one.
 */
unsigned int drm_mock_sched_advance(struct drm_mock_scheduler *sched,
				    unsigned int num)
{
	struct drm_mock_sched_job *job, *next;
	unsigned int found = 0;
	unsigned long flags;
	LIST_HEAD(signal);

	spin_lock_irqsave(&sched->lock, flags);
	if (WARN_ON_ONCE(sched->hw_timeline.cur_seqno + num <
			 sched->hw_timeline.cur_seqno))
		goto unlock;
	sched->hw_timeline.cur_seqno += num;
	list_for_each_entry_safe(job, next, &sched->job_list, link) {
		if (sched->hw_timeline.cur_seqno < job->hw_fence.seqno)
			break;

		drm_mock_sched_job_complete(job);
		found++;
	}
unlock:
	spin_unlock_irqrestore(&sched->lock, flags);

	return found;
}

MODULE_DESCRIPTION("DRM mock scheduler and tests");
MODULE_LICENSE("GPL");
