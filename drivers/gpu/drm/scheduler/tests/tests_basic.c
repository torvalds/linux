// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Valve Corporation */

#include <linux/delay.h>

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
	KUNIT_CASE(drm_sched_change_priority),
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
	KUNIT_CASE(drm_sched_test_credits),
	{}
};

static struct kunit_suite drm_sched_credits = {
	.name = "drm_sched_basic_credits_tests",
	.test_cases = drm_sched_credits_tests,
};

kunit_test_suites(&drm_sched_basic,
		  &drm_sched_timeout,
		  &drm_sched_cancel,
		  &drm_sched_priority,
		  &drm_sched_modify_sched,
		  &drm_sched_credits);
