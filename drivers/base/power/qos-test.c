// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 NXP
 */
#include <kunit/test.h>
#include <linux/pm_qos.h>

/* Basic test for aggregating two "min" requests */
static void freq_qos_test_min(struct kunit *test)
{
	struct freq_constraints	qos;
	struct freq_qos_request	req1, req2;
	int ret;

	freq_constraints_init(&qos);
	memset(&req1, 0, sizeof(req1));
	memset(&req2, 0, sizeof(req2));

	ret = freq_qos_add_request(&qos, &req1, FREQ_QOS_MIN, 1000);
	KUNIT_EXPECT_EQ(test, ret, 1);
	ret = freq_qos_add_request(&qos, &req2, FREQ_QOS_MIN, 2000);
	KUNIT_EXPECT_EQ(test, ret, 1);

	KUNIT_EXPECT_EQ(test, freq_qos_read_value(&qos, FREQ_QOS_MIN), 2000);

	ret = freq_qos_remove_request(&req2);
	KUNIT_EXPECT_EQ(test, ret, 1);
	KUNIT_EXPECT_EQ(test, freq_qos_read_value(&qos, FREQ_QOS_MIN), 1000);

	ret = freq_qos_remove_request(&req1);
	KUNIT_EXPECT_EQ(test, ret, 1);
	KUNIT_EXPECT_EQ(test, freq_qos_read_value(&qos, FREQ_QOS_MIN),
			FREQ_QOS_MIN_DEFAULT_VALUE);
}

/* Test that requests for MAX_DEFAULT_VALUE have no effect */
static void freq_qos_test_maxdef(struct kunit *test)
{
	struct freq_constraints	qos;
	struct freq_qos_request	req1, req2;
	int ret;

	freq_constraints_init(&qos);
	memset(&req1, 0, sizeof(req1));
	memset(&req2, 0, sizeof(req2));
	KUNIT_EXPECT_EQ(test, freq_qos_read_value(&qos, FREQ_QOS_MAX),
			FREQ_QOS_MAX_DEFAULT_VALUE);

	ret = freq_qos_add_request(&qos, &req1, FREQ_QOS_MAX,
			FREQ_QOS_MAX_DEFAULT_VALUE);
	KUNIT_EXPECT_EQ(test, ret, 0);
	ret = freq_qos_add_request(&qos, &req2, FREQ_QOS_MAX,
			FREQ_QOS_MAX_DEFAULT_VALUE);
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* Add max 1000 */
	ret = freq_qos_update_request(&req1, 1000);
	KUNIT_EXPECT_EQ(test, ret, 1);
	KUNIT_EXPECT_EQ(test, freq_qos_read_value(&qos, FREQ_QOS_MAX), 1000);

	/* Add max 2000, no impact */
	ret = freq_qos_update_request(&req2, 2000);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, freq_qos_read_value(&qos, FREQ_QOS_MAX), 1000);

	/* Remove max 1000, new max 2000 */
	ret = freq_qos_remove_request(&req1);
	KUNIT_EXPECT_EQ(test, ret, 1);
	KUNIT_EXPECT_EQ(test, freq_qos_read_value(&qos, FREQ_QOS_MAX), 2000);
}

/*
 * Test that a freq_qos_request can be added again after removal
 *
 * This issue was solved by commit 05ff1ba412fd ("PM: QoS: Invalidate frequency
 * QoS requests after removal")
 */
static void freq_qos_test_readd(struct kunit *test)
{
	struct freq_constraints	qos;
	struct freq_qos_request	req;
	int ret;

	freq_constraints_init(&qos);
	memset(&req, 0, sizeof(req));
	KUNIT_EXPECT_EQ(test, freq_qos_read_value(&qos, FREQ_QOS_MIN),
			FREQ_QOS_MIN_DEFAULT_VALUE);

	/* Add */
	ret = freq_qos_add_request(&qos, &req, FREQ_QOS_MIN, 1000);
	KUNIT_EXPECT_EQ(test, ret, 1);
	KUNIT_EXPECT_EQ(test, freq_qos_read_value(&qos, FREQ_QOS_MIN), 1000);

	/* Remove */
	ret = freq_qos_remove_request(&req);
	KUNIT_EXPECT_EQ(test, ret, 1);
	KUNIT_EXPECT_EQ(test, freq_qos_read_value(&qos, FREQ_QOS_MIN),
			FREQ_QOS_MIN_DEFAULT_VALUE);

	/* Add again */
	ret = freq_qos_add_request(&qos, &req, FREQ_QOS_MIN, 2000);
	KUNIT_EXPECT_EQ(test, ret, 1);
	KUNIT_EXPECT_EQ(test, freq_qos_read_value(&qos, FREQ_QOS_MIN), 2000);
}

static struct kunit_case pm_qos_test_cases[] = {
	KUNIT_CASE(freq_qos_test_min),
	KUNIT_CASE(freq_qos_test_maxdef),
	KUNIT_CASE(freq_qos_test_readd),
	{},
};

static struct kunit_suite pm_qos_test_module = {
	.name = "qos-kunit-test",
	.test_cases = pm_qos_test_cases,
};
kunit_test_suites(&pm_qos_test_module);
