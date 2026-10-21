// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Valve Corporation */

#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/minmax.h>
#include <linux/time.h>
#include <linux/workqueue.h>

#include "sched_tests.h"

#define MOCK_TIMEOUT (HZ / 5)

/*
 * DRM scheduler basic tests should check the basic functional correctness of
 * the scheduler, including some very light smoke testing. More targeted tests,
 * for example focusing on testing specific bugs and other more complicated test
 * scenarios, should be implemented in separate source units.
 */

static int drm_sched_basic_init(struct kunit *test)
{
	test->priv = drm_mock_sched_new(test, MAX_SCHEDULE_TIMEOUT);

	return 0;
}

static void drm_sched_basic_exit(struct kunit *test)
{
	struct drm_mock_scheduler *sched = test->priv;

	drm_mock_sched_fini(sched);
}

static int drm_sched_timeout_init(struct kunit *test)
{
	test->priv = drm_mock_sched_new(test, MOCK_TIMEOUT);

	return 0;
}

static void drm_sched_basic_submit(struct kunit *test)
{
	struct drm_mock_scheduler *sched = test->priv;
	struct drm_mock_sched_entity *entity;
	struct drm_mock_sched_job *job;
	unsigned int i;
	bool done;

	/*
	 * Submit one job to the scheduler and verify that it gets scheduled
	 * and completed only when the mock hw backend processes it.
	 */

	entity = drm_mock_sched_entity_new(test,
					   DRM_SCHED_PRIORITY_NORMAL,
					   sched);
	job = drm_mock_sched_job_new(test, entity);

	drm_mock_sched_job_submit(job);

	done = drm_mock_sched_job_wait_scheduled(job, HZ);
	KUNIT_ASSERT_TRUE(test, done);

	done = drm_mock_sched_job_wait_finished(job, HZ / 2);
	KUNIT_ASSERT_FALSE(test, done);

	i = drm_mock_sched_advance(sched, 1);
	KUNIT_ASSERT_EQ(test, i, 1);

	done = drm_mock_sched_job_wait_finished(job, HZ);
	KUNIT_ASSERT_TRUE(test, done);

	drm_mock_sched_entity_free(entity);
}

struct drm_sched_basic_params {
	const char *description;
	unsigned int queue_depth;
	unsigned int num_entities;
	unsigned int job_us;
	bool dep_chain;
};

static const struct drm_sched_basic_params drm_sched_basic_cases[] = {
	{
		.description = "A queue of jobs in a single entity",
		.queue_depth = 100,
		.job_us = 1000,
		.num_entities = 1,
	},
	{
		.description = "A chain of dependent jobs across multiple entities",
		.queue_depth = 100,
		.job_us = 1000,
		.num_entities = 1,
		.dep_chain = true,
	},
	{
		.description = "Multiple independent job queues",
		.queue_depth = 100,
		.job_us = 1000,
		.num_entities = 4,
	},
	{
		.description = "Multiple inter-dependent job queues",
		.queue_depth = 100,
		.job_us = 1000,
		.num_entities = 4,
		.dep_chain = true,
	},
};

static void
drm_sched_basic_desc(const struct drm_sched_basic_params *params, char *desc)
{
	strscpy(desc, params->description, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(drm_sched_basic, drm_sched_basic_cases, drm_sched_basic_desc);

static void drm_sched_basic_test(struct kunit *test)
{
	const struct drm_sched_basic_params *params = test->param_value;
	struct drm_mock_scheduler *sched = test->priv;
	struct drm_mock_sched_job *job, *prev = NULL;
	struct drm_mock_sched_entity **entity;
	unsigned int i, cur_ent = 0;
	bool done;

	entity = kunit_kcalloc(test, params->num_entities, sizeof(*entity),
			       GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, entity);

	for (i = 0; i < params->num_entities; i++)
		entity[i] = drm_mock_sched_entity_new(test,
						      DRM_SCHED_PRIORITY_NORMAL,
						      sched);

	for (i = 0; i < params->queue_depth; i++) {
		job = drm_mock_sched_job_new(test, entity[cur_ent++]);
		cur_ent %= params->num_entities;
		drm_mock_sched_job_set_duration_us(job, params->job_us);
		if (params->dep_chain && prev)
			drm_sched_job_add_dependency(&job->base,
						     dma_fence_get(&prev->base.s_fence->finished));
		drm_mock_sched_job_submit(job);
		prev = job;
	}

	done = drm_mock_sched_job_wait_finished(job, HZ);
	KUNIT_ASSERT_TRUE(test, done);

	for (i = 0; i < params->num_entities; i++)
		drm_mock_sched_entity_free(entity[i]);
}

static void drm_sched_basic_entity_cleanup(struct kunit *test)
{
	struct drm_mock_sched_job *job, *mid, *prev = NULL;
	struct drm_mock_scheduler *sched = test->priv;
	struct drm_mock_sched_entity *entity[4];
	const unsigned int qd = 100;
	unsigned int i, cur_ent = 0;
	bool done;

	/*
	 * Submit a queue of jobs across different entities with an explicit
	 * chain of dependencies between them and trigger entity cleanup while
	 * the queue is still being processed.
	 */

	for (i = 0; i < ARRAY_SIZE(entity); i++)
		entity[i] = drm_mock_sched_entity_new(test,
						      DRM_SCHED_PRIORITY_NORMAL,
						      sched);

	for (i = 0; i < qd; i++) {
		job = drm_mock_sched_job_new(test, entity[cur_ent++]);
		cur_ent %= ARRAY_SIZE(entity);
		drm_mock_sched_job_set_duration_us(job, 1000);
		if (prev)
			drm_sched_job_add_dependency(&job->base,
						     dma_fence_get(&prev->base.s_fence->finished));
		drm_mock_sched_job_submit(job);
		if (i == qd / 2)
			mid = job;
		prev = job;
	}

	done = drm_mock_sched_job_wait_finished(mid, HZ);
	KUNIT_ASSERT_TRUE(test, done);

	/* Exit with half of the queue still pending to be executed. */
	for (i = 0; i < ARRAY_SIZE(entity); i++)
		drm_mock_sched_entity_free(entity[i]);
}

static struct kunit_case drm_sched_basic_tests[] = {
	KUNIT_CASE(drm_sched_basic_submit),
	KUNIT_CASE_PARAM(drm_sched_basic_test, drm_sched_basic_gen_params),
	KUNIT_CASE(drm_sched_basic_entity_cleanup),
	{}
};

static struct kunit_suite drm_sched_basic = {
	.name = "drm_sched_basic_tests",
	.init = drm_sched_basic_init,
	.exit = drm_sched_basic_exit,
	.test_cases = drm_sched_basic_tests,
};

static void drm_sched_basic_cancel(struct kunit *test)
{
	struct drm_mock_sched_entity *entity;
	struct drm_mock_scheduler *sched;
	struct drm_mock_sched_job *job;
	bool done;

	/*
	 * Check that drm_sched_fini() uses the cancel_job() callback to cancel
	 * jobs that are still pending.
	 */

	sched = drm_mock_sched_new(test, MAX_SCHEDULE_TIMEOUT);
	entity = drm_mock_sched_entity_new(test, DRM_SCHED_PRIORITY_NORMAL,
					   sched);

	job = drm_mock_sched_job_new(test, entity);

	drm_mock_sched_job_submit(job);

	done = drm_mock_sched_job_wait_scheduled(job, HZ);
	KUNIT_ASSERT_TRUE(test, done);

	drm_mock_sched_entity_free(entity);
	drm_mock_sched_fini(sched);

	KUNIT_ASSERT_EQ(test, job->hw_fence.error, -ECANCELED);
}

struct sched_concurrent_context {
	struct drm_mock_scheduler *sched;
	struct workqueue_struct *sub_wq;
	struct kunit *test;
	struct completion wait_go;
};

KUNIT_DEFINE_ACTION_WRAPPER(drm_mock_sched_fini_wrap, drm_mock_sched_fini,
			    struct drm_mock_scheduler *);

KUNIT_DEFINE_ACTION_WRAPPER(drm_mock_sched_entity_free_wrap, drm_mock_sched_entity_free,
			    struct drm_mock_sched_entity *);

static void complete_destroy_workqueue(void *context)
{
	struct sched_concurrent_context *ctx = context;

	complete_all(&ctx->wait_go);

	destroy_workqueue(ctx->sub_wq);
}

static int drm_sched_concurrent_init(struct kunit *test)
{
	struct sched_concurrent_context *ctx;
	int ret;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	init_completion(&ctx->wait_go);

	ctx->sched = drm_mock_sched_new(test, MAX_SCHEDULE_TIMEOUT);

	ret = kunit_add_action_or_reset(test, drm_mock_sched_fini_wrap, ctx->sched);
	KUNIT_ASSERT_EQ(test, ret, 0);

	/* Use an unbounded workqueue to maximize job submission concurrency */
	ctx->sub_wq = alloc_workqueue("drm-sched-submitters-wq", WQ_UNBOUND,
				      WQ_UNBOUND_MAX_ACTIVE);
	KUNIT_ASSERT_NOT_NULL(test, ctx->sub_wq);

	ret = kunit_add_action_or_reset(test, complete_destroy_workqueue, ctx);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ctx->test = test;
	test->priv = ctx;

	return 0;
}

struct drm_sched_parallel_params {
	const char *description;
	unsigned int num_jobs;
	unsigned int num_workers;
};

static const struct drm_sched_parallel_params drm_sched_parallel_cases[] = {
	{
		.description = "Parallel submission of multiple jobs per worker",
		.num_jobs = 8,
		.num_workers = 16,
	},
};

static void
drm_sched_parallel_desc(const struct drm_sched_parallel_params *params, char *desc)
{
	strscpy(desc, params->description, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(drm_sched_parallel, drm_sched_parallel_cases, drm_sched_parallel_desc);

struct parallel_worker {
	struct work_struct work;
	struct sched_concurrent_context *ctx;
	struct drm_mock_sched_entity *entity;
	struct drm_mock_sched_job **jobs;
	unsigned int id;
};

static void drm_sched_parallel_worker(struct work_struct *work)
{
	const struct drm_sched_parallel_params *params;
	struct sched_concurrent_context *test_ctx;
	struct parallel_worker *worker;
	unsigned int i;

	worker = container_of(work, struct parallel_worker, work);
	test_ctx = worker->ctx;
	params = test_ctx->test->param_value;

	wait_for_completion(&test_ctx->wait_go);

	kunit_info(test_ctx->test, "Parallel worker %u submitting %u jobs started\n",
		   worker->id, params->num_jobs);

	for (i = 0; i < params->num_jobs; i++)
		drm_mock_sched_job_submit(worker->jobs[i]);
}

/*
 * Spawns workers that submit a sequence of jobs to the mock scheduler.
 * Once all jobs are submitted, the timeline is manually advanced.
 */
static void drm_sched_parallel_submit_test(struct kunit *test)
{
	struct sched_concurrent_context *ctx = test->priv;
	const struct drm_sched_parallel_params *params = test->param_value;
	struct parallel_worker *workers, *worker;
	struct drm_mock_sched_job *job;
	unsigned int i, j, completed_jobs, total_jobs;
	bool done;
	int ret;

	KUNIT_ASSERT_GT(test, params->num_workers, 0);
	KUNIT_ASSERT_GT(test, params->num_jobs, 0);

	workers = kunit_kcalloc(test, params->num_workers, sizeof(*workers),
				GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, workers);

	/*
	 * Init workers only after all jobs and entities have been successfully
	 * allocated. In this way, the cleanup logic for when an assertion fail
	 * can be simplified.
	 */
	for (i = 0; i < params->num_workers; i++) {
		worker = &workers[i];
		worker->id = i;
		worker->ctx = ctx;
		worker->entity = drm_mock_sched_entity_new(test,
							   DRM_SCHED_PRIORITY_NORMAL,
							   ctx->sched);

		worker->jobs = kunit_kcalloc(test, params->num_jobs,
					     sizeof(*worker->jobs), GFP_KERNEL);
		KUNIT_ASSERT_NOT_NULL(test, worker->jobs);

		for (j = 0; j < params->num_jobs; j++) {
			job = drm_mock_sched_job_new(test, worker->entity);
			worker->jobs[j] = job;
		}

		ret = kunit_add_action_or_reset(test, drm_mock_sched_entity_free_wrap,
						worker->entity);
		KUNIT_ASSERT_EQ(test, ret, 0);
	}

	for (i = 0; i < params->num_workers; i++) {
		worker = &workers[i];
		INIT_WORK(&worker->work, drm_sched_parallel_worker);
		queue_work(ctx->sub_wq, &worker->work);
	}

	complete_all(&ctx->wait_go);
	flush_workqueue(ctx->sub_wq);

	for (i = 0; i < params->num_workers; i++) {
		worker = &workers[i];
		for (j = 0; j < params->num_jobs; j++) {
			job = worker->jobs[j];
			done = drm_mock_sched_job_wait_scheduled(job, HZ);
			KUNIT_EXPECT_TRUE(test, done);
		}
	}

	total_jobs = params->num_workers * params->num_jobs;
	completed_jobs = drm_mock_sched_advance(ctx->sched, total_jobs);
	KUNIT_EXPECT_EQ(test, completed_jobs, total_jobs);

	for (i = 0; i < params->num_workers; i++) {
		worker = &workers[i];
		for (j = 0; j < params->num_jobs; j++) {
			job = worker->jobs[j];
			done = drm_mock_sched_job_wait_finished(job, HZ);
			KUNIT_EXPECT_TRUE(test, done);
		}
	}
}

struct drm_sched_interleaved_params {
	const char *description;
	unsigned int test_duration_ms;
	unsigned int job_base_duration_us;
	unsigned int num_workers;
	unsigned int num_in_flight_jobs;
};

static const struct drm_sched_interleaved_params drm_sched_interleaved_cases[] = {
	{
		.description = "Interleaved submission of multiple jobs per worker",
		.test_duration_ms = 1000,
		.job_base_duration_us = 100,
		.num_workers = 16,
		.num_in_flight_jobs = 8,
	},
};

static void
drm_sched_interleaved_desc(const struct drm_sched_interleaved_params *params, char *desc)
{
	strscpy(desc, params->description, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(drm_sched_interleaved, drm_sched_interleaved_cases,
		  drm_sched_interleaved_desc);

struct interleaved_worker {
	struct work_struct work;
	struct sched_concurrent_context *ctx;
	struct drm_mock_sched_entity *entity;
	struct drm_mock_sched_job **jobs;
	unsigned int id;
	unsigned int job_count;
	unsigned int job_duration_us;
};

static void drm_sched_interleaved_worker(struct work_struct *work)
{
	struct sched_concurrent_context *test_ctx;
	const struct drm_sched_interleaved_params *params;
	struct interleaved_worker *worker;
	unsigned int i, j, max_in_flight_job;
	unsigned long timeout;
	bool done;

	worker = container_of(work, struct interleaved_worker, work);
	test_ctx = worker->ctx;
	params = test_ctx->test->param_value;

	wait_for_completion(&test_ctx->wait_go);

	kunit_info(test_ctx->test, "Worker %u submitting %u jobs of %u us started\n",
		   worker->id, worker->job_count, worker->job_duration_us);

	timeout = msecs_to_jiffies(params->test_duration_ms * 2);

	/* Fill the submission window */
	max_in_flight_job = min(worker->job_count, params->num_in_flight_jobs);
	for (i = 0; i < max_in_flight_job; i++)
		drm_mock_sched_job_submit(worker->jobs[i]);

	/* Keep the window full by submitting a new job at once until done */
	for (i = 0; i < worker->job_count; i++) {
		done = drm_mock_sched_job_wait_finished(worker->jobs[i], timeout);
		if (!done)
			kunit_info(test_ctx->test, "Job %u of worker %u timed out\n",
				   i, worker->id);

		j = i + max_in_flight_job;
		if (j < worker->job_count)
			drm_mock_sched_job_submit(worker->jobs[j]);
	}
}

/*
 * Spawns workers that submit a sequence of jobs to the mock scheduler. Job
 * durations are chosen as multiples of a base duration value specified as
 * a test parameter. Since the scheduler serializes jobs from all workers,
 * the total test duration budget is divided into equal shares among workers.
 * These shares are then used to compute the number of jobs that each worker
 * can submit.
 */
static void drm_sched_interleaved_submit_test(struct kunit *test)
{
	const struct drm_sched_interleaved_params *params = test->param_value;
	struct sched_concurrent_context *ctx = test->priv;
	struct interleaved_worker *workers, *worker;
	struct drm_mock_sched_job *job;
	unsigned int worker_share_us;
	unsigned int i, j;
	bool done;
	int ret;

	KUNIT_ASSERT_GT(test, params->num_workers, 0);
	KUNIT_ASSERT_GT(test, params->job_base_duration_us, 0);

	workers = kunit_kcalloc(test, params->num_workers, sizeof(*workers),
				GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, workers);

	/* Divide the available test time into equal shares among the workers */
	worker_share_us = (params->test_duration_ms * USEC_PER_MSEC) /
			  params->num_workers;

	/*
	 * Init workers only after all jobs and entities have been successfully
	 * allocated. In this way, the cleanup logic for when an assertion fails
	 * can be simplified.
	 */
	for (i = 0; i < params->num_workers; i++) {
		worker = &workers[i];
		worker->id = i;
		worker->ctx = ctx;

		worker->job_duration_us = params->job_base_duration_us * (i + 1);
		worker->job_count = worker_share_us / worker->job_duration_us;
		worker->job_count = max(1U, worker->job_count);

		worker->entity = drm_mock_sched_entity_new(test,
							   DRM_SCHED_PRIORITY_NORMAL,
							   ctx->sched);

		worker->jobs = kunit_kcalloc(test, worker->job_count,
					     sizeof(*worker->jobs), GFP_KERNEL);
		KUNIT_ASSERT_NOT_NULL(test, worker->jobs);

		for (j = 0; j < worker->job_count; j++) {
			job = drm_mock_sched_job_new(test, worker->entity);
			drm_mock_sched_job_set_duration_us(job, worker->job_duration_us);

			worker->jobs[j] = job;
		}

		ret = kunit_add_action_or_reset(test, drm_mock_sched_entity_free_wrap,
						worker->entity);
		KUNIT_ASSERT_EQ(test, ret, 0);
	}

	for (i = 0; i < params->num_workers; i++) {
		worker = &workers[i];
		INIT_WORK(&worker->work, drm_sched_interleaved_worker);
		queue_work(ctx->sub_wq, &worker->work);
	}

	complete_all(&ctx->wait_go);
	flush_workqueue(ctx->sub_wq);

	for (i = 0; i < params->num_workers; i++) {
		worker = &workers[i];
		for (j = 0; j < worker->job_count; j++) {
			job = worker->jobs[j];
			done = drm_mock_sched_job_is_finished(job);
			KUNIT_EXPECT_TRUE(test, done);
		}
	}
}

static struct kunit_case drm_sched_concurrent_tests[] = {
	KUNIT_CASE_PARAM(drm_sched_parallel_submit_test, drm_sched_parallel_gen_params),
	KUNIT_CASE_PARAM(drm_sched_interleaved_submit_test, drm_sched_interleaved_gen_params),
	{}
};

static struct kunit_suite drm_sched_concurrent = {
	.name = "drm_sched_concurrent_tests",
	.init = drm_sched_concurrent_init,
	.test_cases = drm_sched_concurrent_tests,
	.attr = {
		.speed = KUNIT_SPEED_SLOW,
	},
};

static struct kunit_case drm_sched_cancel_tests[] = {
	KUNIT_CASE(drm_sched_basic_cancel),
	{}
};

static struct kunit_suite drm_sched_cancel = {
	.name = "drm_sched_basic_cancel_tests",
	.init = drm_sched_basic_init,
	.exit = drm_sched_basic_exit,
	.test_cases = drm_sched_cancel_tests,
};

static void drm_sched_basic_timeout(struct kunit *test)
{
	struct drm_mock_scheduler *sched = test->priv;
	struct drm_mock_sched_entity *entity;
	struct drm_mock_sched_job *job;
	bool done;

	/*
	 * Submit a single job against a scheduler with the timeout configured
	 * and verify that the timeout handling will run if the backend fails
	 * to complete it in time.
	 */

	entity = drm_mock_sched_entity_new(test,
					   DRM_SCHED_PRIORITY_NORMAL,
					   sched);
	job = drm_mock_sched_job_new(test, entity);

	drm_mock_sched_job_submit(job);

	done = drm_mock_sched_job_wait_scheduled(job, HZ);
	KUNIT_ASSERT_TRUE(test, done);

	done = drm_mock_sched_job_wait_finished(job, MOCK_TIMEOUT / 2);
	KUNIT_ASSERT_FALSE(test, done);

	KUNIT_ASSERT_EQ(test,
			job->flags & DRM_MOCK_SCHED_JOB_TIMEDOUT,
			0);

	done = drm_mock_sched_job_wait_finished(job, MOCK_TIMEOUT);
	KUNIT_ASSERT_FALSE(test, done);

	KUNIT_ASSERT_EQ(test,
			job->flags & DRM_MOCK_SCHED_JOB_TIMEDOUT,
			DRM_MOCK_SCHED_JOB_TIMEDOUT);

	drm_mock_sched_entity_free(entity);
}

static void drm_sched_skip_reset(struct kunit *test)
{
	struct drm_mock_scheduler *sched = test->priv;
	struct drm_mock_sched_entity *entity;
	struct drm_mock_sched_job *job;
	unsigned int i;
	bool done;

	/*
	 * Submit a single job against a scheduler with the timeout configured
	 * and verify that if the job is still running, the timeout handler
	 * will skip the reset and allow the job to complete.
	 */

	entity = drm_mock_sched_entity_new(test,
					   DRM_SCHED_PRIORITY_NORMAL,
					   sched);
	job = drm_mock_sched_job_new(test, entity);

	job->flags = DRM_MOCK_SCHED_JOB_DONT_RESET;

	drm_mock_sched_job_submit(job);

	done = drm_mock_sched_job_wait_scheduled(job, HZ);
	KUNIT_ASSERT_TRUE(test, done);

	done = drm_mock_sched_job_wait_finished(job, 2 * MOCK_TIMEOUT);
	KUNIT_ASSERT_FALSE(test, done);

	KUNIT_ASSERT_EQ(test,
			job->flags & DRM_MOCK_SCHED_JOB_RESET_SKIPPED,
			DRM_MOCK_SCHED_JOB_RESET_SKIPPED);

	i = drm_mock_sched_advance(sched, 1);
	KUNIT_ASSERT_EQ(test, i, 1);

	done = drm_mock_sched_job_wait_finished(job, HZ);
	KUNIT_ASSERT_TRUE(test, done);

	drm_mock_sched_entity_free(entity);
}

static struct kunit_case drm_sched_timeout_tests[] = {
	KUNIT_CASE(drm_sched_basic_timeout),
	KUNIT_CASE(drm_sched_skip_reset),
	{}
};

static struct kunit_suite drm_sched_timeout = {
	.name = "drm_sched_basic_timeout_tests",
	.init = drm_sched_timeout_init,
	.exit = drm_sched_basic_exit,
	.test_cases = drm_sched_timeout_tests,
};

static void drm_sched_priorities(struct kunit *test)
{
	struct drm_mock_sched_entity *entity[DRM_SCHED_PRIORITY_COUNT];
	struct drm_mock_scheduler *sched = test->priv;
	struct drm_mock_sched_job *job;
	const unsigned int qd = 100;
	unsigned int i, cur_ent = 0;
	enum drm_sched_priority p;
	bool done;

	/*
	 * Submit a bunch of jobs against entities configured with different
	 * priorities.
	 */

	BUILD_BUG_ON(DRM_SCHED_PRIORITY_KERNEL > DRM_SCHED_PRIORITY_LOW);
	BUILD_BUG_ON(ARRAY_SIZE(entity) != DRM_SCHED_PRIORITY_COUNT);

	for (p = DRM_SCHED_PRIORITY_KERNEL; p <= DRM_SCHED_PRIORITY_LOW; p++)
		entity[p] = drm_mock_sched_entity_new(test, p, sched);

	for (i = 0; i < qd; i++) {
		job = drm_mock_sched_job_new(test, entity[cur_ent++]);
		cur_ent %= ARRAY_SIZE(entity);
		drm_mock_sched_job_set_duration_us(job, 1000);
		drm_mock_sched_job_submit(job);
	}

	done = drm_mock_sched_job_wait_finished(job, HZ);
	KUNIT_ASSERT_TRUE(test, done);

	for (i = 0; i < ARRAY_SIZE(entity); i++)
		drm_mock_sched_entity_free(entity[i]);
}

static void drm_sched_change_priority(struct kunit *test)
{
	struct drm_mock_sched_entity *entity[DRM_SCHED_PRIORITY_COUNT];
	struct drm_mock_scheduler *sched = test->priv;
	struct drm_mock_sched_job *job;
	const unsigned int qd = 1000;
	unsigned int i, cur_ent = 0;
	enum drm_sched_priority p;

	/*
	 * Submit a bunch of jobs against entities configured with different
	 * priorities and while waiting for them to complete, periodically keep
	 * changing their priorities.
	 *
	 * We set up the queue-depth (qd) and job duration so the priority
	 * changing loop has some time to interact with submissions to the
	 * backend and job completions as they progress.
	 */

	for (p = DRM_SCHED_PRIORITY_KERNEL; p <= DRM_SCHED_PRIORITY_LOW; p++)
		entity[p] = drm_mock_sched_entity_new(test, p, sched);

	for (i = 0; i < qd; i++) {
		job = drm_mock_sched_job_new(test, entity[cur_ent++]);
		cur_ent %= ARRAY_SIZE(entity);
		drm_mock_sched_job_set_duration_us(job, 1000);
		drm_mock_sched_job_submit(job);
	}

	do {
		drm_sched_entity_set_priority(&entity[cur_ent]->base,
					      (entity[cur_ent]->base.priority + 1) %
					      DRM_SCHED_PRIORITY_COUNT);
		cur_ent++;
		cur_ent %= ARRAY_SIZE(entity);
		usleep_range(200, 500);
	} while (!drm_mock_sched_job_is_finished(job));

	for (i = 0; i < ARRAY_SIZE(entity); i++)
		drm_mock_sched_entity_free(entity[i]);
}

static struct kunit_case drm_sched_priority_tests[] = {
	KUNIT_CASE(drm_sched_priorities),
	KUNIT_CASE_SLOW(drm_sched_change_priority),
	{}
};

static struct kunit_suite drm_sched_priority = {
	.name = "drm_sched_basic_priority_tests",
	.init = drm_sched_basic_init,
	.exit = drm_sched_basic_exit,
	.test_cases = drm_sched_priority_tests,
};

static void drm_sched_test_modify_sched(struct kunit *test)
{
	unsigned int i, cur_ent = 0, cur_sched = 0;
	struct drm_mock_sched_entity *entity[13];
	struct drm_mock_scheduler *sched[3];
	struct drm_mock_sched_job *job;
	const unsigned int qd = 1000;

	/*
	 * Submit a bunch of jobs against entities configured with different
	 * schedulers and while waiting for them to complete, periodically keep
	 * changing schedulers associated with each entity.
	 *
	 * We set up the queue-depth (qd) and job duration so the sched modify
	 * loop has some time to interact with submissions to the backend and
	 * job completions as they progress.
	 *
	 * For the number of schedulers and entities we use primes in order to
	 * perturb the entity->sched assignments with less of a regular pattern.
	 */

	for (i = 0; i < ARRAY_SIZE(sched); i++)
		sched[i] = drm_mock_sched_new(test, MAX_SCHEDULE_TIMEOUT);

	for (i = 0; i < ARRAY_SIZE(entity); i++)
		entity[i] = drm_mock_sched_entity_new(test,
						      DRM_SCHED_PRIORITY_NORMAL,
						      sched[i % ARRAY_SIZE(sched)]);

	for (i = 0; i < qd; i++) {
		job = drm_mock_sched_job_new(test, entity[cur_ent++]);
		cur_ent %= ARRAY_SIZE(entity);
		drm_mock_sched_job_set_duration_us(job, 1000);
		drm_mock_sched_job_submit(job);
	}

	do {
		struct drm_gpu_scheduler *modify;

		usleep_range(200, 500);
		cur_ent++;
		cur_ent %= ARRAY_SIZE(entity);
		cur_sched++;
		cur_sched %= ARRAY_SIZE(sched);
		modify = &sched[cur_sched]->base;
		drm_sched_entity_modify_sched(&entity[cur_ent]->base, &modify,
					      1);
	} while (!drm_mock_sched_job_is_finished(job));

	for (i = 0; i < ARRAY_SIZE(entity); i++)
		drm_mock_sched_entity_free(entity[i]);

	for (i = 0; i < ARRAY_SIZE(sched); i++)
		drm_mock_sched_fini(sched[i]);
}

static struct kunit_case drm_sched_modify_sched_tests[] = {
	KUNIT_CASE(drm_sched_test_modify_sched),
	{}
};

static struct kunit_suite drm_sched_modify_sched = {
	.name = "drm_sched_basic_modify_sched_tests",
	.test_cases = drm_sched_modify_sched_tests,
};

static void drm_sched_test_credits(struct kunit *test)
{
	struct drm_mock_sched_entity *entity;
	struct drm_mock_scheduler *sched;
	struct drm_mock_sched_job *job[2];
	bool done;
	int i;

	/*
	 * Check that the configured credit limit is respected.
	 */

	sched = drm_mock_sched_new(test, MAX_SCHEDULE_TIMEOUT);
	sched->base.credit_limit = 1;

	entity = drm_mock_sched_entity_new(test,
					   DRM_SCHED_PRIORITY_NORMAL,
					   sched);

	job[0] = drm_mock_sched_job_new(test, entity);
	job[1] = drm_mock_sched_job_new(test, entity);

	drm_mock_sched_job_submit(job[0]);
	drm_mock_sched_job_submit(job[1]);

	done = drm_mock_sched_job_wait_scheduled(job[0], HZ);
	KUNIT_ASSERT_TRUE(test, done);

	done = drm_mock_sched_job_wait_scheduled(job[1], HZ);
	KUNIT_ASSERT_FALSE(test, done);

	i = drm_mock_sched_advance(sched, 1);
	KUNIT_ASSERT_EQ(test, i, 1);

	done = drm_mock_sched_job_wait_scheduled(job[1], HZ);
	KUNIT_ASSERT_TRUE(test, done);

	i = drm_mock_sched_advance(sched, 1);
	KUNIT_ASSERT_EQ(test, i, 1);

	done = drm_mock_sched_job_wait_finished(job[1], HZ);
	KUNIT_ASSERT_TRUE(test, done);

	drm_mock_sched_entity_free(entity);
	drm_mock_sched_fini(sched);
}

static struct kunit_case drm_sched_credits_tests[] = {
	KUNIT_CASE_SLOW(drm_sched_test_credits),
	{}
};

static struct kunit_suite drm_sched_credits = {
	.name = "drm_sched_basic_credits_tests",
	.test_cases = drm_sched_credits_tests,
};

kunit_test_suites(&drm_sched_basic,
		  &drm_sched_concurrent,
		  &drm_sched_timeout,
		  &drm_sched_cancel,
		  &drm_sched_priority,
		  &drm_sched_modify_sched,
		  &drm_sched_credits);
