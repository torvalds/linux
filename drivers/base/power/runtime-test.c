// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2025 Google, Inc.
 */

#include <linux/cleanup.h>
#include <linux/pm_runtime.h>
#include <kunit/device.h>
#include <kunit/test.h>

#define DEVICE_NAME "pm_runtime_test_device"

static void pm_runtime_depth_test(struct kunit *test)
{
	struct device *dev = kunit_device_register(test, DEVICE_NAME);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	pm_runtime_enable(dev);

	KUNIT_EXPECT_TRUE(test, pm_runtime_suspended(dev));
	KUNIT_EXPECT_EQ(test, 0, pm_runtime_get_sync(dev));
	KUNIT_EXPECT_TRUE(test, pm_runtime_active(dev));
	KUNIT_EXPECT_EQ(test, 1, pm_runtime_get_sync(dev)); /* "already active" */
	KUNIT_EXPECT_EQ(test, 0, pm_runtime_put_sync(dev));
	KUNIT_EXPECT_EQ(test, 0, pm_runtime_put_sync(dev));
	KUNIT_EXPECT_TRUE(test, pm_runtime_suspended(dev));
}

/* Test pm_runtime_put() and friends when already suspended. */
static void pm_runtime_already_suspended_test(struct kunit *test)
{
	struct device *dev = kunit_device_register(test, DEVICE_NAME);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	pm_runtime_enable(dev);
	KUNIT_EXPECT_TRUE(test, pm_runtime_suspended(dev));

	pm_runtime_get_noresume(dev);
	KUNIT_EXPECT_EQ(test, 0, pm_runtime_barrier(dev)); /* no wakeup needed */
	pm_runtime_put(dev);

	pm_runtime_get_noresume(dev);
	KUNIT_EXPECT_EQ(test, 1, pm_runtime_put_sync(dev));

	KUNIT_EXPECT_EQ(test, 1, pm_runtime_suspend(dev));
	KUNIT_EXPECT_EQ(test, 1, pm_runtime_autosuspend(dev));
	KUNIT_EXPECT_EQ(test, 1, pm_request_autosuspend(dev));

	pm_runtime_get_noresume(dev);
	KUNIT_EXPECT_EQ(test, 1, pm_runtime_put_sync_autosuspend(dev));

	pm_runtime_get_noresume(dev);
	pm_runtime_put_autosuspend(dev);

	/* Grab 2 refcounts */
	pm_runtime_get_noresume(dev);
	pm_runtime_get_noresume(dev);
	/* The first put() sees usage_count 1 */
	KUNIT_EXPECT_EQ(test, 0, pm_runtime_put_sync_autosuspend(dev));
	/* The second put() sees usage_count 0 but tells us "already suspended". */
	KUNIT_EXPECT_EQ(test, 1, pm_runtime_put_sync_autosuspend(dev));

	/* Should have remained suspended the whole time. */
	KUNIT_EXPECT_TRUE(test, pm_runtime_suspended(dev));
}

static void pm_runtime_idle_test(struct kunit *test)
{
	struct device *dev = kunit_device_register(test, DEVICE_NAME);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	pm_runtime_enable(dev);

	KUNIT_EXPECT_TRUE(test, pm_runtime_suspended(dev));
	KUNIT_EXPECT_EQ(test, 0, pm_runtime_get_sync(dev));
	KUNIT_EXPECT_TRUE(test, pm_runtime_active(dev));
	KUNIT_EXPECT_EQ(test, -EAGAIN, pm_runtime_idle(dev));
	KUNIT_EXPECT_TRUE(test, pm_runtime_active(dev));
	pm_runtime_put_noidle(dev);
	KUNIT_EXPECT_TRUE(test, pm_runtime_active(dev));
	KUNIT_EXPECT_EQ(test, 0, pm_runtime_idle(dev));
	KUNIT_EXPECT_TRUE(test, pm_runtime_suspended(dev));
	KUNIT_EXPECT_EQ(test, -EAGAIN, pm_runtime_idle(dev));
	KUNIT_EXPECT_EQ(test, -EAGAIN, pm_request_idle(dev));
}

static void pm_runtime_disabled_test(struct kunit *test)
{
	struct device *dev = kunit_device_register(test, DEVICE_NAME);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	/* Never called pm_runtime_enable() */
	KUNIT_EXPECT_FALSE(test, pm_runtime_enabled(dev));

	/* "disabled" is treated as "active" */
	KUNIT_EXPECT_TRUE(test, pm_runtime_active(dev));
	KUNIT_EXPECT_FALSE(test, pm_runtime_suspended(dev));

	/*
	 * Note: these "fail", but they still acquire/release refcounts, so
	 * keep them balanced.
	 */
	KUNIT_EXPECT_EQ(test, -EACCES, pm_runtime_get(dev));
	pm_runtime_put(dev);

	KUNIT_EXPECT_EQ(test, -EACCES, pm_runtime_get_sync(dev));
	KUNIT_EXPECT_EQ(test, -EACCES, pm_runtime_put_sync(dev));

	KUNIT_EXPECT_EQ(test, -EACCES, pm_runtime_get(dev));
	pm_runtime_put_autosuspend(dev);

	KUNIT_EXPECT_EQ(test, -EACCES, pm_runtime_resume_and_get(dev));
	KUNIT_EXPECT_EQ(test, -EACCES, pm_runtime_idle(dev));
	KUNIT_EXPECT_EQ(test, -EACCES, pm_request_idle(dev));
	KUNIT_EXPECT_EQ(test, -EACCES, pm_request_resume(dev));
	KUNIT_EXPECT_EQ(test, -EACCES, pm_request_autosuspend(dev));
	KUNIT_EXPECT_EQ(test, -EACCES, pm_runtime_suspend(dev));
	KUNIT_EXPECT_EQ(test, -EACCES, pm_runtime_resume(dev));
	KUNIT_EXPECT_EQ(test, -EACCES, pm_runtime_autosuspend(dev));

	/* Still disabled */
	KUNIT_EXPECT_TRUE(test, pm_runtime_active(dev));
	KUNIT_EXPECT_FALSE(test, pm_runtime_enabled(dev));
}

static void pm_runtime_error_test(struct kunit *test)
{
	struct device *dev = kunit_device_register(test, DEVICE_NAME);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	pm_runtime_enable(dev);
	KUNIT_EXPECT_TRUE(test, pm_runtime_suspended(dev));

	/* Fake a .runtime_resume() error */
	dev->power.runtime_error = -EIO;

	/*
	 * Note: these "fail", but they still acquire/release refcounts, so
	 * keep them balanced.
	 */
	KUNIT_EXPECT_EQ(test, -EINVAL, pm_runtime_get(dev));
	pm_runtime_put(dev);

	KUNIT_EXPECT_EQ(test, -EINVAL, pm_runtime_get_sync(dev));
	KUNIT_EXPECT_EQ(test, -EINVAL, pm_runtime_put_sync(dev));

	KUNIT_EXPECT_EQ(test, -EINVAL, pm_runtime_get(dev));
	pm_runtime_put_autosuspend(dev);

	KUNIT_EXPECT_EQ(test, -EINVAL, pm_runtime_get(dev));
	KUNIT_EXPECT_EQ(test, -EINVAL, pm_runtime_put_sync_autosuspend(dev));

	KUNIT_EXPECT_EQ(test, -EINVAL, pm_runtime_resume_and_get(dev));
	KUNIT_EXPECT_EQ(test, -EINVAL, pm_runtime_idle(dev));
	KUNIT_EXPECT_EQ(test, -EINVAL, pm_request_idle(dev));
	KUNIT_EXPECT_EQ(test, -EINVAL, pm_request_resume(dev));
	KUNIT_EXPECT_EQ(test, -EINVAL, pm_request_autosuspend(dev));
	KUNIT_EXPECT_EQ(test, -EINVAL, pm_runtime_suspend(dev));
	KUNIT_EXPECT_EQ(test, -EINVAL, pm_runtime_resume(dev));
	KUNIT_EXPECT_EQ(test, -EINVAL, pm_runtime_autosuspend(dev));

	/* Error is still pending */
	KUNIT_EXPECT_TRUE(test, pm_runtime_suspended(dev));
	KUNIT_EXPECT_EQ(test, -EIO, dev->power.runtime_error);
	/* Clear error */
	KUNIT_EXPECT_EQ(test, 0, pm_runtime_set_suspended(dev));
	KUNIT_EXPECT_EQ(test, 0, dev->power.runtime_error);
	/* Still suspended */
	KUNIT_EXPECT_TRUE(test, pm_runtime_suspended(dev));

	KUNIT_EXPECT_EQ(test, 0, pm_runtime_get(dev));
	KUNIT_EXPECT_EQ(test, 1, pm_runtime_barrier(dev)); /* resume was pending */
	pm_runtime_put(dev);
	pm_runtime_suspend(dev); /* flush the put(), to suspend */
	KUNIT_EXPECT_TRUE(test, pm_runtime_suspended(dev));

	KUNIT_EXPECT_EQ(test, 0, pm_runtime_get_sync(dev));
	KUNIT_EXPECT_EQ(test, 0, pm_runtime_put_sync(dev));

	KUNIT_EXPECT_EQ(test, 0, pm_runtime_get_sync(dev));
	pm_runtime_put_autosuspend(dev);

	KUNIT_EXPECT_EQ(test, 0, pm_runtime_resume_and_get(dev));

	/*
	 * The following should all return -EAGAIN (usage is non-zero) or 1
	 * (already resumed).
	 */
	KUNIT_EXPECT_EQ(test, -EAGAIN, pm_runtime_idle(dev));
	KUNIT_EXPECT_EQ(test, -EAGAIN, pm_request_idle(dev));
	KUNIT_EXPECT_EQ(test, 1, pm_request_resume(dev));
	KUNIT_EXPECT_EQ(test, -EAGAIN, pm_request_autosuspend(dev));
	KUNIT_EXPECT_EQ(test, -EAGAIN, pm_runtime_suspend(dev));
	KUNIT_EXPECT_EQ(test, 1, pm_runtime_resume(dev));
	KUNIT_EXPECT_EQ(test, -EAGAIN, pm_runtime_autosuspend(dev));

	KUNIT_EXPECT_EQ(test, 0, pm_runtime_put_sync(dev));

	/* Suspended again */
	KUNIT_EXPECT_TRUE(test, pm_runtime_suspended(dev));
}

/*
 * Explore a typical probe() sequence in which a device marks itself powered,
 * but doesn't hold any runtime PM reference, so it suspends as soon as it goes
 * idle.
 */
static void pm_runtime_probe_active_test(struct kunit *test)
{
	struct device *dev = kunit_device_register(test, DEVICE_NAME);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	KUNIT_EXPECT_TRUE(test, pm_runtime_status_suspended(dev));

	KUNIT_EXPECT_EQ(test, 0, pm_runtime_set_active(dev));
	KUNIT_EXPECT_TRUE(test, pm_runtime_active(dev));

	pm_runtime_enable(dev);
	KUNIT_EXPECT_TRUE(test, pm_runtime_active(dev));

	/* Nothing to flush. We stay active. */
	KUNIT_EXPECT_EQ(test, 0, pm_runtime_barrier(dev));
	KUNIT_EXPECT_TRUE(test, pm_runtime_active(dev));

	/* Ask for idle? Now we suspend. */
	KUNIT_EXPECT_EQ(test, 0, pm_runtime_idle(dev));
	KUNIT_EXPECT_TRUE(test, pm_runtime_suspended(dev));
}

static struct kunit_case pm_runtime_test_cases[] = {
	KUNIT_CASE(pm_runtime_depth_test),
	KUNIT_CASE(pm_runtime_already_suspended_test),
	KUNIT_CASE(pm_runtime_idle_test),
	KUNIT_CASE(pm_runtime_disabled_test),
	KUNIT_CASE(pm_runtime_error_test),
	KUNIT_CASE(pm_runtime_probe_active_test),
	{}
};

static struct kunit_suite pm_runtime_test_suite = {
	.name = "pm_runtime_test_cases",
	.test_cases = pm_runtime_test_cases,
};

kunit_test_suite(pm_runtime_test_suite);
MODULE_DESCRIPTION("Runtime power management unit test suite");
MODULE_LICENSE("GPL");
