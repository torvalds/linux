// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * KUnit tests for amdgpu_dm_hdcp.c
 *
 * Copyright 2026 Advanced Micro Devices, Inc.
 */

#include <kunit/test.h>
#include <linux/workqueue.h>

#include "amdgpu_dm_hdcp.h"

static void dummy_work_fn(struct work_struct *work) {}

/* Tests for process_output() */

/*
 * Helper: allocate and initialise a minimal hdcp_workqueue sufficient for
 * process_output() testing.  Only the three delayed works accessed by
 * process_output() are initialised; everything else is zeroed.
 */
static struct hdcp_workqueue *alloc_test_workqueue(struct kunit *test)
{
	struct hdcp_workqueue *work;

	work = kunit_kzalloc(test, sizeof(*work), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, work);

	INIT_DELAYED_WORK(&work->callback_dwork, dummy_work_fn);
	INIT_DELAYED_WORK(&work->watchdog_timer_dwork, dummy_work_fn);
	INIT_DELAYED_WORK(&work->property_validate_dwork, dummy_work_fn);

	return work;
}

/*
 * process_output() always schedules property_validate_dwork with delay=0,
 * which queues the work item directly (bypassing the timer).  Use
 * work_pending() rather than delayed_work_pending() to detect this.
 */
static void dm_test_process_output_property_validate_always_scheduled(struct kunit *test)
{
	struct hdcp_workqueue *work = alloc_test_workqueue(test);

	/* No flags set: only property_validate_dwork should be enqueued */
	process_output(work);

	KUNIT_EXPECT_TRUE(test, work_pending(&work->property_validate_dwork.work));
	KUNIT_EXPECT_FALSE(test, delayed_work_pending(&work->callback_dwork));
	KUNIT_EXPECT_FALSE(test, delayed_work_pending(&work->watchdog_timer_dwork));

	cancel_delayed_work_sync(&work->property_validate_dwork);
}

/*
 * output.callback_needed=true must schedule callback_dwork.
 */
static void dm_test_process_output_callback_needed(struct kunit *test)
{
	struct hdcp_workqueue *work = alloc_test_workqueue(test);

	work->output.callback_needed = true;
	work->output.callback_delay = 500;

	process_output(work);

	KUNIT_EXPECT_TRUE(test, delayed_work_pending(&work->callback_dwork));

	cancel_delayed_work_sync(&work->callback_dwork);
	cancel_delayed_work_sync(&work->property_validate_dwork);
}

/*
 * output.callback_stop=true must cancel a previously scheduled callback_dwork.
 */
static void dm_test_process_output_callback_stop(struct kunit *test)
{
	struct hdcp_workqueue *work = alloc_test_workqueue(test);

	/* Pre-schedule callback_dwork with a long delay so it won't fire. */
	schedule_delayed_work(&work->callback_dwork, msecs_to_jiffies(10000));
	KUNIT_ASSERT_TRUE(test, delayed_work_pending(&work->callback_dwork));

	work->output.callback_stop = true;

	process_output(work);

	KUNIT_EXPECT_FALSE(test, delayed_work_pending(&work->callback_dwork));

	cancel_delayed_work_sync(&work->property_validate_dwork);
}

/*
 * output.watchdog_timer_needed=true must schedule watchdog_timer_dwork.
 */
static void dm_test_process_output_watchdog_needed(struct kunit *test)
{
	struct hdcp_workqueue *work = alloc_test_workqueue(test);

	work->output.watchdog_timer_needed = true;
	work->output.watchdog_timer_delay = 1000;

	process_output(work);

	KUNIT_EXPECT_TRUE(test, delayed_work_pending(&work->watchdog_timer_dwork));

	cancel_delayed_work_sync(&work->watchdog_timer_dwork);
	cancel_delayed_work_sync(&work->property_validate_dwork);
}

/*
 * output.watchdog_timer_stop=true must cancel a previously scheduled
 * watchdog_timer_dwork.
 */
static void dm_test_process_output_watchdog_stop(struct kunit *test)
{
	struct hdcp_workqueue *work = alloc_test_workqueue(test);

	/* Pre-schedule watchdog_timer_dwork with a long delay. */
	schedule_delayed_work(&work->watchdog_timer_dwork, msecs_to_jiffies(10000));
	KUNIT_ASSERT_TRUE(test, delayed_work_pending(&work->watchdog_timer_dwork));

	work->output.watchdog_timer_stop = true;

	process_output(work);

	KUNIT_EXPECT_FALSE(test, delayed_work_pending(&work->watchdog_timer_dwork));

	cancel_delayed_work_sync(&work->property_validate_dwork);
}

/*
 * Both callback_needed and watchdog_timer_needed set: both dworks are
 * scheduled independently.
 */
static void dm_test_process_output_callback_and_watchdog_needed(struct kunit *test)
{
	struct hdcp_workqueue *work = alloc_test_workqueue(test);

	work->output.callback_needed = true;
	work->output.callback_delay = 200;
	work->output.watchdog_timer_needed = true;
	work->output.watchdog_timer_delay = 800;

	process_output(work);

	KUNIT_EXPECT_TRUE(test, delayed_work_pending(&work->callback_dwork));
	KUNIT_EXPECT_TRUE(test, delayed_work_pending(&work->watchdog_timer_dwork));

	cancel_delayed_work_sync(&work->callback_dwork);
	cancel_delayed_work_sync(&work->watchdog_timer_dwork);
	cancel_delayed_work_sync(&work->property_validate_dwork);
}
/* End of tests for process_output() */

static struct kunit_case dm_hdcp_test_cases[] = {
	KUNIT_CASE(dm_test_process_output_property_validate_always_scheduled),
	KUNIT_CASE(dm_test_process_output_callback_needed),
	KUNIT_CASE(dm_test_process_output_callback_stop),
	KUNIT_CASE(dm_test_process_output_watchdog_needed),
	KUNIT_CASE(dm_test_process_output_watchdog_stop),
	KUNIT_CASE(dm_test_process_output_callback_and_watchdog_needed),
	{}
};

static struct kunit_suite dm_hdcp_test_suite = {
	.name = "amdgpu_dm_hdcp",
	.test_cases = dm_hdcp_test_cases,
};

kunit_test_suite(dm_hdcp_test_suite);

MODULE_LICENSE("Dual MIT/GPL");
MODULE_DESCRIPTION("KUnit tests for amdgpu_dm_hdcp");
MODULE_AUTHOR("AMD");
