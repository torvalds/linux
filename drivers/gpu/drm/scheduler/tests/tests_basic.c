// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Valve Corporation */

#include "sched_tests.h"

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
	test->priv = drm_mock_sched_new(test, HZ);

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

	done = drm_mock_sched_job_wait_finished(job, HZ / 2);
	KUNIT_ASSERT_FALSE(test, done);

	KUNIT_ASSERT_EQ(test,
			job->flags & DRM_MOCK_SCHED_JOB_TIMEDOUT,
			0);

	done = drm_mock_sched_job_wait_finished(job, HZ);
	KUNIT_ASSERT_FALSE(test, done);

	KUNIT_ASSERT_EQ(test,
			job->flags & DRM_MOCK_SCHED_JOB_TIMEDOUT,
			DRM_MOCK_SCHED_JOB_TIMEDOUT);

	drm_mock_sched_entity_free(entity);
}

static struct kunit_case drm_sched_timeout_tests[] = {
	KUNIT_CASE(drm_sched_basic_timeout),
	{}
};

static struct kunit_suite drm_sched_timeout = {
	.name = "drm_sched_basic_timeout_tests",
	.init = drm_sched_timeout_init,
	.exit = drm_sched_basic_exit,
	.test_cases = drm_sched_timeout_tests,
};

kunit_test_suites(&drm_sched_basic,
		  &drm_sched_timeout);
