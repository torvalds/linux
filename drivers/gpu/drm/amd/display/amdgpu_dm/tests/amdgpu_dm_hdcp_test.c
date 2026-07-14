// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * KUnit tests for amdgpu_dm_hdcp.c
 *
 * Copyright 2026 Advanced Micro Devices, Inc.
 */

#include <kunit/test.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

#include "amdgpu.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_hdcp.h"
#include "amdgpu_dm_kunit_test_helpers.h"
#include "hdcp_psp.h"

static void dummy_work_fn(struct work_struct *work) {}

/* Tests for hdcp_get_content_protection_from_status() */

/**
 * dm_test_hdcp_get_cp_disabled_returns_desired - HDCP off maps to DESIRED
 * @test: KUnit test context
 *
 * When encryption status is HDCP_OFF, content_protection should be set
 * to DESIRED and the function should return true to indicate an update.
 */
static void dm_test_hdcp_get_cp_disabled_returns_desired(struct kunit *test)
{
	unsigned int content_protection = DRM_MODE_CONTENT_PROTECTION_UNDESIRED;
	bool update;

	update = hdcp_get_content_protection_from_status(
		DRM_MODE_HDCP_CONTENT_TYPE0,
		MOD_HDCP_ENCRYPTION_STATUS_HDCP_OFF,
		&content_protection);

	KUNIT_EXPECT_TRUE(test, update);
	KUNIT_EXPECT_EQ(test, content_protection,
			DRM_MODE_CONTENT_PROTECTION_DESIRED);
}

/**
 * dm_test_hdcp_get_cp_type0_returns_enabled - TYPE0 with TYPE0_ON maps to ENABLED
 * @test: KUnit test context
 *
 * When content type is TYPE0 and encryption status is at or below
 * HDCP2_TYPE0_ON, content_protection should be set to ENABLED.
 */
static void dm_test_hdcp_get_cp_type0_returns_enabled(struct kunit *test)
{
	unsigned int content_protection = DRM_MODE_CONTENT_PROTECTION_UNDESIRED;
	bool update;

	update = hdcp_get_content_protection_from_status(
		DRM_MODE_HDCP_CONTENT_TYPE0,
		MOD_HDCP_ENCRYPTION_STATUS_HDCP2_TYPE0_ON,
		&content_protection);

	KUNIT_EXPECT_TRUE(test, update);
	KUNIT_EXPECT_EQ(test, content_protection,
			DRM_MODE_CONTENT_PROTECTION_ENABLED);
}

/**
 * dm_test_hdcp_get_cp_type1_returns_enabled - TYPE1 with TYPE1_ON maps to ENABLED
 * @test: KUnit test context
 *
 * When content type is TYPE1 and encryption status is exactly
 * HDCP2_TYPE1_ON, content_protection should be set to ENABLED.
 */
static void dm_test_hdcp_get_cp_type1_returns_enabled(struct kunit *test)
{
	unsigned int content_protection = DRM_MODE_CONTENT_PROTECTION_UNDESIRED;
	bool update;

	update = hdcp_get_content_protection_from_status(
		DRM_MODE_HDCP_CONTENT_TYPE1,
		MOD_HDCP_ENCRYPTION_STATUS_HDCP2_TYPE1_ON,
		&content_protection);

	KUNIT_EXPECT_TRUE(test, update);
	KUNIT_EXPECT_EQ(test, content_protection,
			DRM_MODE_CONTENT_PROTECTION_ENABLED);
}

/**
 * dm_test_hdcp_get_cp_type1_rejects_type0_status - TYPE1 rejects TYPE0_ON
 * @test: KUnit test context
 *
 * When content type is TYPE1 but encryption status is only TYPE0_ON,
 * the function should return false and leave content_protection unchanged.
 */
static void dm_test_hdcp_get_cp_type1_rejects_type0_status(struct kunit *test)
{
	unsigned int content_protection = DRM_MODE_CONTENT_PROTECTION_UNDESIRED;
	bool update;

	update = hdcp_get_content_protection_from_status(
		DRM_MODE_HDCP_CONTENT_TYPE1,
		MOD_HDCP_ENCRYPTION_STATUS_HDCP2_TYPE0_ON,
		&content_protection);

	KUNIT_EXPECT_FALSE(test, update);
	KUNIT_EXPECT_EQ(test, content_protection,
			DRM_MODE_CONTENT_PROTECTION_UNDESIRED);
}

/**
 * dm_test_hdcp_get_cp_type0_rejects_type1_status - TYPE0 rejects TYPE1_ON
 * @test: KUnit test context
 *
 * When content type is TYPE0 but encryption status exceeds the TYPE0_ON
 * boundary (TYPE1_ON), the function should return false.
 */
static void dm_test_hdcp_get_cp_type0_rejects_type1_status(struct kunit *test)
{
	unsigned int content_protection = DRM_MODE_CONTENT_PROTECTION_UNDESIRED;
	bool update;

	update = hdcp_get_content_protection_from_status(
		DRM_MODE_HDCP_CONTENT_TYPE0,
		MOD_HDCP_ENCRYPTION_STATUS_HDCP2_TYPE1_ON,
		&content_protection);

	KUNIT_EXPECT_FALSE(test, update);
	KUNIT_EXPECT_EQ(test, content_protection,
			DRM_MODE_CONTENT_PROTECTION_UNDESIRED);
}

/* Tests for hdcp_get_link_display_adjustments() */

/**
 * dm_test_hdcp_get_adjustments_disable_authentication - disable path zeroes adjustments
 * @test: KUnit test context
 *
 * When enable_encryption is false, display_adjust should disable
 * authentication and all link_adjust fields should remain zeroed.
 */
static void dm_test_hdcp_get_adjustments_disable_authentication(struct kunit *test)
{
	struct mod_hdcp_link_adjustment link_adjust;
	struct mod_hdcp_display_adjustment display_adjust;
	unsigned int disable;
	unsigned int hdcp1_disable;
	unsigned int force_type;

	hdcp_get_link_display_adjustments(false, DRM_MODE_HDCP_CONTENT_TYPE0,
		false, false, false, &link_adjust, &display_adjust);
	disable = display_adjust.disable;
	hdcp1_disable = link_adjust.hdcp1.disable;
	force_type = link_adjust.hdcp2.force_type;

	KUNIT_EXPECT_EQ(test, disable,
			MOD_HDCP_DISPLAY_DISABLE_AUTHENTICATION);
	KUNIT_EXPECT_EQ(test, link_adjust.auth_delay, 0);
	KUNIT_EXPECT_EQ(test, link_adjust.retry_limit, 0);
	KUNIT_EXPECT_EQ(test, hdcp1_disable, 0);
	KUNIT_EXPECT_EQ(test, force_type, 0);
}

/**
 * dm_test_hdcp_get_adjustments_type0_policy - TYPE0 enables HDCP1 and forces TYPE0
 * @test: KUnit test context
 *
 * When encryption is enabled with content TYPE0, hdcp1 should remain
 * enabled, force_type should be TYPE_0, and sw_locality_fallback should
 * be propagated from the input parameter.
 */
static void dm_test_hdcp_get_adjustments_type0_policy(struct kunit *test)
{
	struct mod_hdcp_link_adjustment link_adjust;
	struct mod_hdcp_display_adjustment display_adjust;
	unsigned int disable;
	unsigned int hdcp1_disable;
	unsigned int force_type;

	hdcp_get_link_display_adjustments(true, DRM_MODE_HDCP_CONTENT_TYPE0,
		false, false, true, &link_adjust, &display_adjust);
	disable = display_adjust.disable;
	hdcp1_disable = link_adjust.hdcp1.disable;
	force_type = link_adjust.hdcp2.force_type;

	KUNIT_EXPECT_EQ(test, disable,
			MOD_HDCP_DISPLAY_NOT_DISABLE);
	KUNIT_EXPECT_EQ(test, link_adjust.auth_delay, 2);
	KUNIT_EXPECT_EQ(test, link_adjust.retry_limit, MAX_NUM_OF_ATTEMPTS);
	KUNIT_EXPECT_EQ(test, hdcp1_disable, 0);
	KUNIT_EXPECT_EQ(test, force_type,
			MOD_HDCP_FORCE_TYPE_0);
	KUNIT_EXPECT_FALSE(test, link_adjust.hdcp2.use_fw_locality_check);
	KUNIT_EXPECT_TRUE(test, link_adjust.hdcp2.use_sw_locality_fallback);
}

/**
 * dm_test_hdcp_get_adjustments_type1_policy - TYPE1 disables HDCP1 and forces TYPE1
 * @test: KUnit test context
 *
 * When encryption is enabled with content TYPE1, hdcp1 should be
 * disabled, force_type should be TYPE_1, and fw_locality_check should
 * be enabled when hdcp_lc_force_fw_enable is set.
 */
static void dm_test_hdcp_get_adjustments_type1_policy(struct kunit *test)
{
	struct mod_hdcp_link_adjustment link_adjust;
	struct mod_hdcp_display_adjustment display_adjust;
	unsigned int disable;
	unsigned int hdcp1_disable;
	unsigned int force_type;

	hdcp_get_link_display_adjustments(true, DRM_MODE_HDCP_CONTENT_TYPE1,
		false, true, false, &link_adjust, &display_adjust);
	disable = display_adjust.disable;
	hdcp1_disable = link_adjust.hdcp1.disable;
	force_type = link_adjust.hdcp2.force_type;

	KUNIT_EXPECT_EQ(test, disable,
			MOD_HDCP_DISPLAY_NOT_DISABLE);
	KUNIT_EXPECT_EQ(test, link_adjust.auth_delay, 2);
	KUNIT_EXPECT_EQ(test, link_adjust.retry_limit, MAX_NUM_OF_ATTEMPTS);
	KUNIT_EXPECT_EQ(test, hdcp1_disable, 1);
	KUNIT_EXPECT_EQ(test, force_type,
			MOD_HDCP_FORCE_TYPE_1);
	KUNIT_EXPECT_TRUE(test, link_adjust.hdcp2.use_fw_locality_check);
	KUNIT_EXPECT_FALSE(test, link_adjust.hdcp2.use_sw_locality_fallback);
}

/**
 * dm_test_hdcp_get_adjustments_fused_io_enables_fw_check - fused_io enables FW locality check
 * @test: KUnit test context
 *
 * When fused_io_supported is true, use_fw_locality_check should be
 * enabled regardless of hdcp_lc_force_fw_enable.
 */
static void dm_test_hdcp_get_adjustments_fused_io_enables_fw_check(struct kunit *test)
{
	struct mod_hdcp_link_adjustment link_adjust;
	struct mod_hdcp_display_adjustment display_adjust;

	hdcp_get_link_display_adjustments(true, DRM_MODE_HDCP_CONTENT_TYPE0,
		true, false, false, &link_adjust, &display_adjust);

	KUNIT_EXPECT_TRUE(test, link_adjust.hdcp2.use_fw_locality_check);
}

/* Tests for process_output() */

/**
 * alloc_test_workqueue - allocate a minimal hdcp_workqueue for testing
 * @test: KUnit test context for managed allocation
 *
 * Allocates and initialises a minimal hdcp_workqueue sufficient for
 * process_output() testing. Only the three delayed works accessed by
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

/**
 * dm_test_process_output_property_validate_always_scheduled - validate_dwork always queued
 * @test: KUnit test context
 *
 * process_output() always schedules property_validate_dwork with delay=0,
 * which queues the work item directly (bypassing the timer). Uses
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

/**
 * dm_test_process_output_callback_needed - callback_needed schedules callback_dwork
 * @test: KUnit test context
 *
 * When output.callback_needed is true, process_output() must schedule
 * callback_dwork with the specified delay.
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

/**
 * dm_test_process_output_callback_stop - callback_stop cancels callback_dwork
 * @test: KUnit test context
 *
 * When output.callback_stop is true, process_output() must cancel a
 * previously scheduled callback_dwork.
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

/**
 * dm_test_process_output_watchdog_needed - watchdog_needed schedules watchdog_dwork
 * @test: KUnit test context
 *
 * When output.watchdog_timer_needed is true, process_output() must
 * schedule watchdog_timer_dwork with the specified delay.
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

/**
 * dm_test_process_output_watchdog_stop - watchdog_stop cancels watchdog_dwork
 * @test: KUnit test context
 *
 * When output.watchdog_timer_stop is true, process_output() must cancel
 * a previously scheduled watchdog_timer_dwork.
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

/**
 * dm_test_process_output_callback_and_watchdog_needed - both dworks scheduled independently
 * @test: KUnit test context
 *
 * When both callback_needed and watchdog_timer_needed are set,
 * process_output() must schedule both dworks independently.
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

/**
 * dm_test_process_output_callback_stop_and_needed_requeues - stop+needed requeues callback work
 * @test: KUnit test context
 *
 * When callback_stop and callback_needed are both set, process_output()
 * should cancel the previous callback_dwork and queue it again.
 */
static void dm_test_process_output_callback_stop_and_needed_requeues(struct kunit *test)
{
	struct hdcp_workqueue *work = alloc_test_workqueue(test);

	schedule_delayed_work(&work->callback_dwork, msecs_to_jiffies(10000));
	KUNIT_ASSERT_TRUE(test, delayed_work_pending(&work->callback_dwork));

	work->output.callback_stop = true;
	work->output.callback_needed = true;
	work->output.callback_delay = 300;

	process_output(work);

	KUNIT_EXPECT_TRUE(test, delayed_work_pending(&work->callback_dwork));

	cancel_delayed_work_sync(&work->callback_dwork);
	cancel_delayed_work_sync(&work->property_validate_dwork);
}

/**
 * dm_test_process_output_watchdog_stop_and_needed_requeues - stop+needed requeues watchdog work
 * @test: KUnit test context
 *
 * When watchdog_timer_stop and watchdog_timer_needed are both set,
 * process_output() should cancel previous watchdog work and queue it again.
 */
static void dm_test_process_output_watchdog_stop_and_needed_requeues(struct kunit *test)
{
	struct hdcp_workqueue *work = alloc_test_workqueue(test);

	schedule_delayed_work(&work->watchdog_timer_dwork, msecs_to_jiffies(10000));
	KUNIT_ASSERT_TRUE(test, delayed_work_pending(&work->watchdog_timer_dwork));

	work->output.watchdog_timer_stop = true;
	work->output.watchdog_timer_needed = true;
	work->output.watchdog_timer_delay = 700;

	process_output(work);

	KUNIT_EXPECT_TRUE(test, delayed_work_pending(&work->watchdog_timer_dwork));

	cancel_delayed_work_sync(&work->watchdog_timer_dwork);
	cancel_delayed_work_sync(&work->property_validate_dwork);
}
/* End of tests for process_output() */

/* Tests for event_property_update() */

/**
 * alloc_test_workqueue_for_property_update - allocate minimal workqueue for callback tests
 * @test: KUnit test context for managed allocation
 *
 * Allocates a minimal hdcp_workqueue with property_update_work initialised
 * so event_property_update() can resolve container_of() safely.
 */
static struct hdcp_workqueue *alloc_test_workqueue_for_property_update(struct kunit *test)
{
	struct hdcp_workqueue *work;

	work = kunit_kzalloc(test, sizeof(*work), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, work);

	INIT_WORK(&work->property_update_work, dummy_work_fn);

	return work;
}

/**
 * dm_test_event_property_update_skips_null_connector - null connector is ignored
 * @test: KUnit test context
 *
 * If aconnector entry is NULL, event_property_update() should skip it
 * without modifying encryption_status.
 */
static void dm_test_event_property_update_skips_null_connector(struct kunit *test)
{
	struct hdcp_workqueue *work = alloc_test_workqueue_for_property_update(test);
	enum mod_hdcp_encryption_status before;

	work->encryption_status[0] = MOD_HDCP_ENCRYPTION_STATUS_HDCP2_TYPE0_ON;
	before = work->encryption_status[0];

	event_property_update(&work->property_update_work);

	KUNIT_EXPECT_EQ(test, work->encryption_status[0], before);
}

/* End of tests for event_property_update() */

/* Tests for event_callback() */

/**
 * alloc_test_workqueue_for_callback - workqueue ready for event_callback()
 * @test: KUnit test context for managed allocation
 *
 * Allocates a minimal hdcp_workqueue with its mutex and the three delayed
 * works initialised, as required by the guard(mutex), cancel_delayed_work()
 * and process_output() usage inside event_callback().
 */
static struct hdcp_workqueue *alloc_test_workqueue_for_callback(struct kunit *test)
{
	struct hdcp_workqueue *work;

	work = kunit_kzalloc(test, sizeof(*work), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, work);

	mutex_init(&work->mutex);
	INIT_DELAYED_WORK(&work->callback_dwork, dummy_work_fn);
	INIT_DELAYED_WORK(&work->watchdog_timer_dwork, dummy_work_fn);
	INIT_DELAYED_WORK(&work->property_validate_dwork, dummy_work_fn);

	return work;
}

/**
 * dm_test_event_callback_cancels_callback_dwork - callback work is cancelled
 * @test: KUnit test context
 *
 * event_callback() must cancel a previously scheduled callback_dwork. With
 * no active hdcp display, mod_hdcp_process_event() leaves output cleared so
 * the callback is not requeued and callback_dwork ends up not pending.
 */
static void dm_test_event_callback_cancels_callback_dwork(struct kunit *test)
{
	struct hdcp_workqueue *work = alloc_test_workqueue_for_callback(test);

	/* Pre-schedule callback_dwork with a long delay so it won't fire. */
	schedule_delayed_work(&work->callback_dwork, msecs_to_jiffies(10000));
	KUNIT_ASSERT_TRUE(test, delayed_work_pending(&work->callback_dwork));

	event_callback(&work->callback_dwork.work);

	KUNIT_EXPECT_FALSE(test, delayed_work_pending(&work->callback_dwork));

	cancel_delayed_work_sync(&work->callback_dwork);
	cancel_delayed_work_sync(&work->watchdog_timer_dwork);
	cancel_delayed_work_sync(&work->property_validate_dwork);
}

/**
 * dm_test_event_callback_schedules_property_validate - process_output() runs
 * @test: KUnit test context
 *
 * event_callback() finishes by calling process_output(), which always
 * enqueues property_validate_dwork with delay=0. Verifying it is pending
 * proves event_callback() reached process_output() and released the mutex.
 */
static void dm_test_event_callback_schedules_property_validate(struct kunit *test)
{
	struct hdcp_workqueue *work = alloc_test_workqueue_for_callback(test);

	event_callback(&work->callback_dwork.work);

	KUNIT_EXPECT_TRUE(test, work_pending(&work->property_validate_dwork.work));
	/* Mutex must be released after the guard scope exits. */
	KUNIT_EXPECT_FALSE(test, mutex_is_locked(&work->mutex));

	cancel_delayed_work_sync(&work->callback_dwork);
	cancel_delayed_work_sync(&work->watchdog_timer_dwork);
	cancel_delayed_work_sync(&work->property_validate_dwork);
}

/* End of tests for event_callback() */

/* Tests for event_property_validate() */

/**
 * alloc_test_workqueue_for_property_validate - workqueue for validate tests
 * @test: KUnit test context for managed allocation
 *
 * Allocates a minimal hdcp_workqueue with its mutex and property_update_work
 * initialised, as required by the guard(mutex) and schedule_work() usage
 * inside event_property_validate().
 */
static struct hdcp_workqueue *alloc_test_workqueue_for_property_validate(struct kunit *test)
{
	struct hdcp_workqueue *work;

	work = kunit_kzalloc(test, sizeof(*work), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, work);

	mutex_init(&work->mutex);
	INIT_WORK(&work->property_update_work, dummy_work_fn);

	return work;
}

/**
 * alloc_validate_connector - connector for event_property_validate() tests
 * @test: KUnit test context for managed allocation
 * @index: drm connector index to assign
 * @status: drm connector detection status to assign
 * @with_state: whether to attach a zeroed drm_connector_state
 *
 * Allocates an amdgpu_dm_connector sufficient for the traversal in
 * event_property_validate(). mod_hdcp_query_display() has no active display
 * so it returns early, leaving the pre-set HDCP_OFF query untouched.
 */
static struct amdgpu_dm_connector *alloc_validate_connector(struct kunit *test,
							    unsigned int index,
							    enum drm_connector_status status,
							    bool with_state)
{
	struct amdgpu_dm_connector *aconnector;

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);

	aconnector->base.index = index;
	aconnector->base.status = status;

	if (with_state) {
		struct drm_connector_state *conn_state;

		conn_state = kunit_kzalloc(test, sizeof(*conn_state), GFP_KERNEL);
		KUNIT_ASSERT_NOT_NULL(test, conn_state);
		aconnector->base.state = conn_state;
	}

	return aconnector;
}

/**
 * dm_test_event_property_validate_skips_null_connector - null entries ignored
 * @test: KUnit test context
 *
 * With every aconnector entry NULL, event_property_validate() must not
 * schedule property_update_work and must leave encryption_status untouched.
 */
static void dm_test_event_property_validate_skips_null_connector(struct kunit *test)
{
	struct hdcp_workqueue *work = alloc_test_workqueue_for_property_validate(test);

	work->encryption_status[0] = MOD_HDCP_ENCRYPTION_STATUS_HDCP2_TYPE1_ON;

	event_property_validate(&work->property_validate_dwork.work);

	KUNIT_EXPECT_FALSE(test, work_pending(&work->property_update_work));
	KUNIT_EXPECT_EQ(test, work->encryption_status[0],
			MOD_HDCP_ENCRYPTION_STATUS_HDCP2_TYPE1_ON);
}

/**
 * dm_test_event_property_validate_skips_disconnected - disconnected is skipped
 * @test: KUnit test context
 *
 * A connector whose status is not connector_status_connected must be
 * skipped, leaving encryption_status unchanged and no work scheduled.
 */
static void dm_test_event_property_validate_skips_disconnected(struct kunit *test)
{
	struct hdcp_workqueue *work = alloc_test_workqueue_for_property_validate(test);

	work->aconnector[1] = alloc_validate_connector(test, 1,
						       connector_status_disconnected, true);
	work->encryption_status[1] = MOD_HDCP_ENCRYPTION_STATUS_HDCP2_TYPE1_ON;

	event_property_validate(&work->property_validate_dwork.work);

	KUNIT_EXPECT_FALSE(test, work_pending(&work->property_update_work));
	KUNIT_EXPECT_EQ(test, work->encryption_status[1],
			MOD_HDCP_ENCRYPTION_STATUS_HDCP2_TYPE1_ON);
}

/**
 * dm_test_event_property_validate_skips_null_state - missing state is skipped
 * @test: KUnit test context
 *
 * A connected connector without a drm_connector_state must be skipped
 * before the query, leaving encryption_status unchanged.
 */
static void dm_test_event_property_validate_skips_null_state(struct kunit *test)
{
	struct hdcp_workqueue *work = alloc_test_workqueue_for_property_validate(test);

	work->aconnector[2] = alloc_validate_connector(test, 2,
						       connector_status_connected, false);
	work->encryption_status[2] = MOD_HDCP_ENCRYPTION_STATUS_HDCP2_TYPE1_ON;

	event_property_validate(&work->property_validate_dwork.work);

	KUNIT_EXPECT_FALSE(test, work_pending(&work->property_update_work));
	KUNIT_EXPECT_EQ(test, work->encryption_status[2],
			MOD_HDCP_ENCRYPTION_STATUS_HDCP2_TYPE1_ON);
}

/**
 * dm_test_event_property_validate_updates_on_status_change - change triggers update
 * @test: KUnit test context
 *
 * For a connected connector with state, the query returns HDCP_OFF (no
 * active hdcp display). When the stored encryption_status differs, it must
 * be updated to HDCP_OFF and property_update_work must be scheduled.
 */
static void dm_test_event_property_validate_updates_on_status_change(struct kunit *test)
{
	struct hdcp_workqueue *work = alloc_test_workqueue_for_property_validate(test);

	work->aconnector[3] = alloc_validate_connector(test, 3,
						       connector_status_connected, true);
	work->encryption_status[3] = MOD_HDCP_ENCRYPTION_STATUS_HDCP2_TYPE1_ON;

	event_property_validate(&work->property_validate_dwork.work);

	KUNIT_EXPECT_EQ(test, work->encryption_status[3],
			MOD_HDCP_ENCRYPTION_STATUS_HDCP_OFF);
	KUNIT_EXPECT_TRUE(test, work_pending(&work->property_update_work));

	cancel_work_sync(&work->property_update_work);
}

/**
 * dm_test_event_property_validate_no_update_when_unchanged - no change, no work
 * @test: KUnit test context
 *
 * For a connected connector with state whose stored encryption_status is
 * already HDCP_OFF (matching the query result), event_property_validate()
 * must not reschedule property_update_work.
 */
static void dm_test_event_property_validate_no_update_when_unchanged(struct kunit *test)
{
	struct hdcp_workqueue *work = alloc_test_workqueue_for_property_validate(test);

	work->aconnector[4] = alloc_validate_connector(test, 4,
						       connector_status_connected, true);
	work->encryption_status[4] = MOD_HDCP_ENCRYPTION_STATUS_HDCP_OFF;

	event_property_validate(&work->property_validate_dwork.work);

	KUNIT_EXPECT_EQ(test, work->encryption_status[4],
			MOD_HDCP_ENCRYPTION_STATUS_HDCP_OFF);
	KUNIT_EXPECT_FALSE(test, work_pending(&work->property_update_work));
}

/* End of tests for event_property_validate() */

/* Tests for event_watchdog_timer() */

/**
 * dm_test_event_watchdog_timer_cancels_watchdog_dwork - watchdog work is cancelled
 * @test: KUnit test context
 *
 * event_watchdog_timer() must cancel a previously scheduled
 * watchdog_timer_dwork. With no active hdcp display,
 * mod_hdcp_process_event() leaves output cleared so the watchdog is not
 * requeued and watchdog_timer_dwork ends up not pending.
 */
static void dm_test_event_watchdog_timer_cancels_watchdog_dwork(struct kunit *test)
{
	struct hdcp_workqueue *work = alloc_test_workqueue_for_callback(test);

	/* Pre-schedule watchdog_timer_dwork with a long delay so it won't fire. */
	schedule_delayed_work(&work->watchdog_timer_dwork, msecs_to_jiffies(10000));
	KUNIT_ASSERT_TRUE(test, delayed_work_pending(&work->watchdog_timer_dwork));

	event_watchdog_timer(&work->watchdog_timer_dwork.work);

	KUNIT_EXPECT_FALSE(test, delayed_work_pending(&work->watchdog_timer_dwork));

	cancel_delayed_work_sync(&work->callback_dwork);
	cancel_delayed_work_sync(&work->watchdog_timer_dwork);
	cancel_delayed_work_sync(&work->property_validate_dwork);
}

/**
 * dm_test_event_watchdog_timer_schedules_property_validate - process_output() runs
 * @test: KUnit test context
 *
 * event_watchdog_timer() finishes by calling process_output(), which always
 * enqueues property_validate_dwork with delay=0. Verifying it is pending
 * proves the handler reached process_output() and released the mutex.
 */
static void dm_test_event_watchdog_timer_schedules_property_validate(struct kunit *test)
{
	struct hdcp_workqueue *work = alloc_test_workqueue_for_callback(test);

	event_watchdog_timer(&work->watchdog_timer_dwork.work);

	KUNIT_EXPECT_TRUE(test, work_pending(&work->property_validate_dwork.work));
	/* Mutex must be released after the guard scope exits. */
	KUNIT_EXPECT_FALSE(test, mutex_is_locked(&work->mutex));

	cancel_delayed_work_sync(&work->callback_dwork);
	cancel_delayed_work_sync(&work->watchdog_timer_dwork);
	cancel_delayed_work_sync(&work->property_validate_dwork);
}

/* End of tests for event_watchdog_timer() */

/* Tests for event_cpirq() */

/**
 * alloc_test_workqueue_for_cpirq - workqueue ready for event_cpirq()
 * @test: KUnit test context for managed allocation
 *
 * Allocates a minimal hdcp_workqueue with its mutex, cpirq_work and the
 * three delayed works initialised, as required by the guard(mutex) and
 * process_output() usage inside event_cpirq().
 */
static struct hdcp_workqueue *alloc_test_workqueue_for_cpirq(struct kunit *test)
{
	struct hdcp_workqueue *work;

	work = kunit_kzalloc(test, sizeof(*work), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, work);

	mutex_init(&work->mutex);
	INIT_WORK(&work->cpirq_work, dummy_work_fn);
	INIT_DELAYED_WORK(&work->callback_dwork, dummy_work_fn);
	INIT_DELAYED_WORK(&work->watchdog_timer_dwork, dummy_work_fn);
	INIT_DELAYED_WORK(&work->property_validate_dwork, dummy_work_fn);

	return work;
}

/**
 * dm_test_event_cpirq_schedules_property_validate - process_output() runs
 * @test: KUnit test context
 *
 * event_cpirq() finishes by calling process_output(), which always
 * enqueues property_validate_dwork with delay=0. Verifying it is pending
 * proves the handler reached process_output() and released the mutex.
 */
static void dm_test_event_cpirq_schedules_property_validate(struct kunit *test)
{
	struct hdcp_workqueue *work = alloc_test_workqueue_for_cpirq(test);

	event_cpirq(&work->cpirq_work);

	KUNIT_EXPECT_TRUE(test, work_pending(&work->property_validate_dwork.work));
	/* Mutex must be released after the guard scope exits. */
	KUNIT_EXPECT_FALSE(test, mutex_is_locked(&work->mutex));

	cancel_delayed_work_sync(&work->callback_dwork);
	cancel_delayed_work_sync(&work->watchdog_timer_dwork);
	cancel_delayed_work_sync(&work->property_validate_dwork);
}

/**
 * dm_test_event_cpirq_leaves_callback_and_watchdog_idle - only validate is queued
 * @test: KUnit test context
 *
 * With no active hdcp display, mod_hdcp_process_event() leaves output
 * cleared, so event_cpirq() must not schedule callback_dwork or
 * watchdog_timer_dwork; only property_validate_dwork is enqueued.
 */
static void dm_test_event_cpirq_leaves_callback_and_watchdog_idle(struct kunit *test)
{
	struct hdcp_workqueue *work = alloc_test_workqueue_for_cpirq(test);

	event_cpirq(&work->cpirq_work);

	KUNIT_EXPECT_FALSE(test, delayed_work_pending(&work->callback_dwork));
	KUNIT_EXPECT_FALSE(test, delayed_work_pending(&work->watchdog_timer_dwork));

	cancel_delayed_work_sync(&work->callback_dwork);
	cancel_delayed_work_sync(&work->watchdog_timer_dwork);
	cancel_delayed_work_sync(&work->property_validate_dwork);
}

/* End of tests for event_cpirq() */

/* Tests for hdcp_handle_cpirq() */

/**
 * dm_test_hdcp_handle_cpirq_schedules_work - cpirq handler queues cpirq_work
 * @test: KUnit test context
 *
 * hdcp_handle_cpirq() should schedule cpirq_work for the selected link.
 */
static void dm_test_hdcp_handle_cpirq_schedules_work(struct kunit *test)
{
	struct hdcp_workqueue *work;

	work = kunit_kzalloc(test, sizeof(*work), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, work);

	INIT_WORK(&work->cpirq_work, dummy_work_fn);

	hdcp_handle_cpirq(work, 0);

	KUNIT_EXPECT_TRUE(test, work_pending(&work->cpirq_work));

	cancel_work_sync(&work->cpirq_work);
}

/**
 * dm_test_hdcp_handle_cpirq_selects_link_index - only selected link work is queued
 * @test: KUnit test context
 *
 * hdcp_handle_cpirq() should schedule cpirq_work for the selected index
 * and not queue unrelated links.
 */
static void dm_test_hdcp_handle_cpirq_selects_link_index(struct kunit *test)
{
	struct hdcp_workqueue *work;

	work = kunit_kcalloc(test, 2, sizeof(*work), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, work);

	INIT_WORK(&work[0].cpirq_work, dummy_work_fn);
	INIT_WORK(&work[1].cpirq_work, dummy_work_fn);

	hdcp_handle_cpirq(work, 1);

	KUNIT_EXPECT_FALSE(test, work_pending(&work[0].cpirq_work));
	KUNIT_EXPECT_TRUE(test, work_pending(&work[1].cpirq_work));

	cancel_work_sync(&work[1].cpirq_work);
}

/* End of tests for hdcp_handle_cpirq() */

/* Tests for hdcp_update_display() helper logic */

/**
 * dm_test_hdcp_update_display_enable_schedules_property_validate - enable path queues validate work
 * @test: KUnit test context
 *
 * hdcp_update_display() should schedule property_validate_dwork when
 * encryption is enabled.
 */
static void dm_test_hdcp_update_display_enable_schedules_property_validate(struct kunit *test)
{
	struct hdcp_workqueue *work;

	work = kunit_kzalloc(test, sizeof(*work), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, work);

	INIT_DELAYED_WORK(&work->property_validate_dwork, dummy_work_fn);
	work->srm_size = 0;

	hdcp_update_display_encryption_control(work, work, 0, true);

	KUNIT_EXPECT_TRUE(test, delayed_work_pending(&work->property_validate_dwork));

	cancel_delayed_work_sync(&work->property_validate_dwork);
}

/**
 * dm_test_hdcp_update_display_disable_resets_status_and_cancels_validate - disable path state update
 * @test: KUnit test context
 *
 * hdcp_update_display() should set encryption_status to HDCP_OFF and
 * cancel property_validate_dwork when encryption is disabled.
 */
static void dm_test_hdcp_update_display_disable_resets_status_and_cancels_validate(struct kunit *test)
{
	struct hdcp_workqueue *work;

	work = kunit_kzalloc(test, sizeof(*work), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, work);

	INIT_DELAYED_WORK(&work->property_validate_dwork, dummy_work_fn);
	schedule_delayed_work(&work->property_validate_dwork, msecs_to_jiffies(10000));
	KUNIT_ASSERT_TRUE(test, delayed_work_pending(&work->property_validate_dwork));

	work->encryption_status[3] = MOD_HDCP_ENCRYPTION_STATUS_HDCP2_TYPE1_ON;

	hdcp_update_display_encryption_control(work, work, 3, false);

	KUNIT_EXPECT_EQ(test, work->encryption_status[3],
			MOD_HDCP_ENCRYPTION_STATUS_HDCP_OFF);
	KUNIT_EXPECT_FALSE(test, delayed_work_pending(&work->property_validate_dwork));
}

/* End of tests for hdcp_update_display() helper logic */

/* Tests for hdcp_create_workqueue() */

/**
 * dm_test_hdcp_create_workqueue_zero_max_links_returns_null - zero-link creation fails early
 * @test: KUnit test context
 *
 * When dc->caps.max_links is zero, hdcp_create_workqueue() should fail
 * the initial allocation path and return NULL.
 */
static void dm_test_hdcp_create_workqueue_zero_max_links_returns_null(struct kunit *test)
{
	struct cp_psp cp_psp = {0};
	struct dc *dc;
	struct hdcp_workqueue *work;

	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dc);

	dc->caps.max_links = 0;

	work = hdcp_create_workqueue(NULL, &cp_psp, dc);

	KUNIT_EXPECT_PTR_EQ(test, work, NULL);
}

/* End of tests for hdcp_create_workqueue() */

/* Tests for hdcp_destroy() */

static ssize_t test_srm_bin_read(struct file *filp, struct kobject *kobj,
				 const struct bin_attribute *bin_attr, char *buffer,
				 loff_t pos, size_t count)
{
	return 0;
}

static ssize_t test_srm_bin_write(struct file *filp, struct kobject *kobj,
				  const struct bin_attribute *bin_attr, char *buffer,
				  loff_t pos, size_t count)
{
	return count;
}

/**
 * setup_destroy_sysfs - create a kobject with the SRM bin file attached
 * @test: KUnit test context
 * @work: workqueue whose attr will be registered
 *
 * hdcp_destroy() calls sysfs_remove_bin_file() on the first entry's attr, so
 * a real kobject with the bin file created is required. Returns the kobject,
 * which the caller must kobject_put() after hdcp_destroy() has run.
 */
static struct kobject *setup_destroy_sysfs(struct kunit *test,
					   struct hdcp_workqueue *work)
{
	struct kobject *kobj;
	int ret;

	kobj = kobject_create_and_add("amdgpu_dm_hdcp_test", NULL);
	KUNIT_ASSERT_NOT_NULL(test, kobj);

	sysfs_bin_attr_init(&work->attr);
	work->attr.attr.name = "hdcp_srm";
	work->attr.attr.mode = 0664;
	work->attr.size = 16;
	work->attr.read = test_srm_bin_read;
	work->attr.write = test_srm_bin_write;

	ret = sysfs_create_bin_file(kobj, &work->attr);
	KUNIT_ASSERT_EQ(test, ret, 0);

	return kobj;
}

/**
 * dm_test_hdcp_destroy_frees_and_removes_sysfs - full teardown path
 * @test: KUnit test context
 *
 * hdcp_destroy() must cancel every link's delayed works, remove the SRM
 * sysfs bin file and free srm, srm_temp and the workqueue itself. The
 * workqueue and SRM buffers use kzalloc() (not kunit-managed) because
 * hdcp_destroy() frees them; KASAN/kmemleak validate there is no leak or
 * use-after-free.
 */
static void dm_test_hdcp_destroy_frees_and_removes_sysfs(struct kunit *test)
{
	struct hdcp_workqueue *work;
	struct kobject *kobj;

	work = kzalloc_obj(*work);
	KUNIT_ASSERT_NOT_NULL(test, work);

	work->max_link = 1;
	INIT_DELAYED_WORK(&work->callback_dwork, dummy_work_fn);
	INIT_DELAYED_WORK(&work->watchdog_timer_dwork, dummy_work_fn);
	INIT_DELAYED_WORK(&work->property_validate_dwork, dummy_work_fn);

	work->srm = kzalloc(16, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, work->srm);
	work->srm_temp = kzalloc(16, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, work->srm_temp);

	kobj = setup_destroy_sysfs(test, work);

	/* Pre-schedule a delayed work to exercise the cancel path. */
	schedule_delayed_work(&work->callback_dwork, msecs_to_jiffies(10000));
	KUNIT_ASSERT_TRUE(test, delayed_work_pending(&work->callback_dwork));

	hdcp_destroy(kobj, work);

	/* work is freed by hdcp_destroy(); only the kobject remains. */
	kobject_put(kobj);
}

/**
 * dm_test_hdcp_destroy_zero_links_null_srm - teardown with no links or SRM
 * @test: KUnit test context
 *
 * With max_link == 0 the cancel loop is skipped, and NULL srm/srm_temp make
 * the kfree() calls no-ops. hdcp_destroy() must still remove the sysfs bin
 * file and free the workqueue without crashing.
 */
static void dm_test_hdcp_destroy_zero_links_null_srm(struct kunit *test)
{
	struct hdcp_workqueue *work;
	struct kobject *kobj;

	work = kzalloc_obj(*work);
	KUNIT_ASSERT_NOT_NULL(test, work);

	work->max_link = 0;
	work->srm = NULL;
	work->srm_temp = NULL;

	kobj = setup_destroy_sysfs(test, work);

	hdcp_destroy(kobj, work);

	kobject_put(kobj);
}

/* End of tests for hdcp_destroy() */

/* Tests for link_lock() */

/**
 * dm_test_link_lock_locks_and_unlocks_all_links - lock/unlock spans every link
 * @test: KUnit test context
 *
 * link_lock() should acquire the mutex of every entry from 0 to max_link
 * when locking, and release all of them when unlocking. A subsequent
 * lock/unlock cycle must succeed, proving the mutexes were left released.
 */
static void dm_test_link_lock_locks_and_unlocks_all_links(struct kunit *test)
{
	const int num_links = 3;
	struct hdcp_workqueue *work;
	int i;

	work = kunit_kcalloc(test, num_links, sizeof(*work), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, work);

	/* max_link is read from the first element. */
	work[0].max_link = num_links;
	for (i = 0; i < num_links; i++)
		mutex_init(&work[i].mutex);

	link_lock(work, true);
	for (i = 0; i < num_links; i++)
		KUNIT_EXPECT_TRUE(test, mutex_is_locked(&work[i].mutex));

	link_lock(work, false);
	for (i = 0; i < num_links; i++)
		KUNIT_EXPECT_FALSE(test, mutex_is_locked(&work[i].mutex));

	/* Mutexes must be re-acquirable after being released. */
	link_lock(work, true);
	for (i = 0; i < num_links; i++)
		KUNIT_EXPECT_TRUE(test, mutex_is_locked(&work[i].mutex));
	link_lock(work, false);
}

/**
 * dm_test_link_lock_zero_links_is_noop - zero max_link touches no mutexes
 * @test: KUnit test context
 *
 * When max_link is zero, link_lock() must not touch any mutex and simply
 * return, leaving the (single) entry's mutex unlocked.
 */
static void dm_test_link_lock_zero_links_is_noop(struct kunit *test)
{
	struct hdcp_workqueue *work;

	work = kunit_kzalloc(test, sizeof(*work), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, work);

	mutex_init(&work->mutex);
	work->max_link = 0;

	link_lock(work, true);

	KUNIT_EXPECT_FALSE(test, mutex_is_locked(&work->mutex));
}

/* End of tests for link_lock() */

/* Tests for psp_get_srm() and psp_set_srm() */

/**
 * dm_test_psp_get_srm_uninitialized_returns_null - GET fails when TA not initialized
 * @test: KUnit test context
 *
 * When the HDCP TA context is not initialized, psp_get_srm() must take the
 * guard path and return NULL without touching the output parameters or
 * invoking the (real) firmware path.
 */
static void dm_test_psp_get_srm_uninitialized_returns_null(struct kunit *test)
{
	struct psp_context *psp;
	uint32_t srm_version = 0xdead;
	uint32_t srm_size = 0xbeef;
	uint8_t *srm;

	psp = kunit_kzalloc(test, sizeof(*psp), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, psp);

	/* kzalloc leaves hdcp_context.context.initialized == false */
	srm = psp_get_srm(psp, &srm_version, &srm_size);

	KUNIT_EXPECT_PTR_EQ(test, srm, NULL);
	/* Output parameters must be left untouched on the guard path. */
	KUNIT_EXPECT_EQ(test, srm_version, 0xdead);
	KUNIT_EXPECT_EQ(test, srm_size, 0xbeef);
}

/**
 * dm_test_psp_set_srm_uninitialized_returns_einval - SET fails when TA not initialized
 * @test: KUnit test context
 *
 * When the HDCP TA context is not initialized, psp_set_srm() must take the
 * guard path and return -EINVAL without updating srm_version or invoking
 * the (real) firmware path.
 */
static void dm_test_psp_set_srm_uninitialized_returns_einval(struct kunit *test)
{
	struct psp_context *psp;
	uint32_t srm_version = 0xdead;
	u8 srm_buf[4] = {0};
	int ret;

	psp = kunit_kzalloc(test, sizeof(*psp), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, psp);

	/* kzalloc leaves hdcp_context.context.initialized == false */
	ret = psp_set_srm(psp, srm_buf, sizeof(srm_buf), &srm_version);

	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	/* srm_version must be left untouched on the guard path. */
	KUNIT_EXPECT_EQ(test, srm_version, 0xdead);
}

/* End of tests for psp_get_srm() and psp_set_srm() */

/*
 * Tests for hdcp_update_display() / hdcp_remove_display() /
 * hdcp_reset_display().
 */

/**
 * alloc_test_workqueue_locked - workqueue with dworks and mutex initialised
 * @test: KUnit test context for managed allocation
 *
 * Allocates a minimal hdcp_workqueue with its mutex and the three delayed
 * works initialised, as required by the guard(mutex) and process_output()
 * usage in the display update/remove/reset helpers.
 */
static struct hdcp_workqueue *alloc_test_workqueue_locked(struct kunit *test)
{
	struct hdcp_workqueue *work;

	work = kunit_kzalloc(test, sizeof(*work), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, work);

	mutex_init(&work->mutex);
	INIT_DELAYED_WORK(&work->callback_dwork, dummy_work_fn);
	INIT_DELAYED_WORK(&work->watchdog_timer_dwork, dummy_work_fn);
	INIT_DELAYED_WORK(&work->property_validate_dwork, dummy_work_fn);

	return work;
}

/**
 * alloc_test_connector - minimal amdgpu_dm_connector for display tests
 * @test: KUnit test context for managed allocation
 * @index: drm connector index to assign
 *
 * Allocates an amdgpu_dm_connector with an initialised connector refcount
 * (so drm_connector_get() is valid) and a dc_link/dc pair. The connector's
 * free_cb is left NULL so drm_connector_put() is a safe no-op that never
 * releases the object.
 */
static struct amdgpu_dm_connector *alloc_test_connector(struct kunit *test,
							unsigned int index)
{
	struct amdgpu_dm_connector *aconnector;
	struct dc_link *dc_link;
	struct dc *dc;

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);
	dc_link = kunit_kzalloc(test, sizeof(*dc_link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dc_link);
	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dc);

	kref_init(&aconnector->base.base.refcount);
	aconnector->base.index = index;
	dc_link->dc = dc;
	aconnector->dc_link = dc_link;

	return aconnector;
}

/**
 * dm_test_hdcp_update_display_enable_registers_connector - enable stores the connector
 * @test: KUnit test context
 *
 * hdcp_update_display() with enable_encryption=true should register the
 * connector in the per-link aconnector array at its connector index. The
 * hdcp has no active display, so mod_hdcp_update_display() is effectively a
 * no-op and the call must not touch encryption_status.
 */
static void dm_test_hdcp_update_display_enable_registers_connector(struct kunit *test)
{
	struct hdcp_workqueue *work = alloc_test_workqueue_locked(test);
	struct amdgpu_dm_connector *aconnector = alloc_test_connector(test, 0);

	work->srm_size = 0;

	hdcp_update_display(work, 0, aconnector, DRM_MODE_HDCP_CONTENT_TYPE0, true);

	KUNIT_EXPECT_PTR_EQ(test, work->aconnector[0], aconnector);
	KUNIT_EXPECT_EQ(test, work->encryption_status[0],
			MOD_HDCP_ENCRYPTION_STATUS_HDCP_OFF);

	cancel_delayed_work_sync(&work->property_validate_dwork);
	cancel_delayed_work_sync(&work->callback_dwork);
	cancel_delayed_work_sync(&work->watchdog_timer_dwork);
}

/**
 * dm_test_hdcp_update_display_disable_sets_status_off - disable clears status
 * @test: KUnit test context
 *
 * hdcp_update_display() with enable_encryption=false should register the
 * connector and reset the connector's encryption_status entry to HDCP_OFF
 * via hdcp_update_display_encryption_control().
 */
static void dm_test_hdcp_update_display_disable_sets_status_off(struct kunit *test)
{
	struct hdcp_workqueue *work = alloc_test_workqueue_locked(test);
	struct amdgpu_dm_connector *aconnector = alloc_test_connector(test, 2);

	work->encryption_status[2] = MOD_HDCP_ENCRYPTION_STATUS_HDCP2_TYPE1_ON;

	hdcp_update_display(work, 0, aconnector, DRM_MODE_HDCP_CONTENT_TYPE0, false);

	KUNIT_EXPECT_PTR_EQ(test, work->aconnector[2], aconnector);
	KUNIT_EXPECT_EQ(test, work->encryption_status[2],
			MOD_HDCP_ENCRYPTION_STATUS_HDCP_OFF);

	cancel_delayed_work_sync(&work->property_validate_dwork);
	cancel_delayed_work_sync(&work->callback_dwork);
	cancel_delayed_work_sync(&work->watchdog_timer_dwork);
}

/**
 * dm_test_hdcp_remove_display_enabled_resets_cp - ENABLED CP reverts to DESIRED
 * @test: KUnit test context
 *
 * hdcp_remove_display() must revert a connector whose content_protection is
 * ENABLED back to DESIRED and clear the per-link aconnector entry.
 */
static void dm_test_hdcp_remove_display_enabled_resets_cp(struct kunit *test)
{
	struct hdcp_workqueue *work = alloc_test_workqueue_locked(test);
	struct amdgpu_dm_connector *aconnector = alloc_test_connector(test, 1);
	struct drm_connector_state *conn_state;

	conn_state = kunit_kzalloc(test, sizeof(*conn_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, conn_state);

	conn_state->content_protection = DRM_MODE_CONTENT_PROTECTION_ENABLED;
	aconnector->base.state = conn_state;
	work->aconnector[1] = aconnector;

	hdcp_remove_display(work, 0, aconnector);

	KUNIT_EXPECT_EQ(test, conn_state->content_protection,
			DRM_MODE_CONTENT_PROTECTION_DESIRED);
	KUNIT_EXPECT_PTR_EQ(test, work->aconnector[1], NULL);

	cancel_delayed_work_sync(&work->property_validate_dwork);
	cancel_delayed_work_sync(&work->callback_dwork);
	cancel_delayed_work_sync(&work->watchdog_timer_dwork);
}

/**
 * dm_test_hdcp_remove_display_null_state_clears_connector - NULL state skips CP change
 * @test: KUnit test context
 *
 * When the connector has no drm_connector_state, hdcp_remove_display() must
 * skip the content_protection update path and still clear the per-link
 * aconnector entry.
 */
static void dm_test_hdcp_remove_display_null_state_clears_connector(struct kunit *test)
{
	struct hdcp_workqueue *work = alloc_test_workqueue_locked(test);
	struct amdgpu_dm_connector *aconnector = alloc_test_connector(test, 0);

	aconnector->base.state = NULL;
	work->aconnector[0] = aconnector;

	hdcp_remove_display(work, 0, aconnector);

	KUNIT_EXPECT_PTR_EQ(test, work->aconnector[0], NULL);

	cancel_delayed_work_sync(&work->property_validate_dwork);
	cancel_delayed_work_sync(&work->callback_dwork);
	cancel_delayed_work_sync(&work->watchdog_timer_dwork);
}

/**
 * dm_test_hdcp_reset_display_clears_all_state - reset clears status and connectors
 * @test: KUnit test context
 *
 * hdcp_reset_display() must reset every connector's encryption_status to
 * HDCP_OFF and clear all per-link aconnector entries.
 */
static void dm_test_hdcp_reset_display_clears_all_state(struct kunit *test)
{
	struct hdcp_workqueue *work = alloc_test_workqueue_locked(test);
	struct amdgpu_dm_connector *aconnector = alloc_test_connector(test, 4);

	work->encryption_status[4] = MOD_HDCP_ENCRYPTION_STATUS_HDCP2_TYPE1_ON;
	work->aconnector[4] = aconnector;

	hdcp_reset_display(work, 0);

	KUNIT_EXPECT_EQ(test, work->encryption_status[4],
			MOD_HDCP_ENCRYPTION_STATUS_HDCP_OFF);
	KUNIT_EXPECT_PTR_EQ(test, work->aconnector[4], NULL);

	cancel_delayed_work_sync(&work->property_validate_dwork);
	cancel_delayed_work_sync(&work->callback_dwork);
	cancel_delayed_work_sync(&work->watchdog_timer_dwork);
}

/*
 * End of tests for hdcp_update_display() / hdcp_remove_display() /
 * hdcp_reset_display().
 */

/* Tests for enable_assr() */

/**
 * alloc_test_workqueue_for_assr - workqueue wired to a psp for enable_assr()
 * @test: KUnit test context for managed allocation
 * @adev: amdgpu device whose drm_device backs psp->adev (for drm_info())
 *
 * Allocates a minimal hdcp_workqueue and a psp_context connected through
 * hdcp.config.psp.handle, matching the dereference chain enable_assr()
 * performs. The psp is left with dtm_context.context.initialized == false
 * (from kzalloc) so enable_assr() takes the "DTM TA not initialized" path.
 */
static struct hdcp_workqueue *alloc_test_workqueue_for_assr(struct kunit *test,
							    struct amdgpu_device *adev)
{
	struct hdcp_workqueue *work;
	struct psp_context *psp;

	work = kunit_kzalloc(test, sizeof(*work), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, work);

	psp = kunit_kzalloc(test, sizeof(*psp), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, psp);

	psp->adev = adev;
	work->hdcp.config.psp.handle = psp;

	return work;
}

/**
 * dm_test_enable_assr_uninitialized_dtm_returns_false - DTM TA not initialized
 * @test: KUnit test context
 *
 * When the DTM TA context is not initialized, enable_assr() must take the
 * early-return path, emit the informational message and return false
 * without invoking the (real) firmware path.
 */
static void dm_test_enable_assr_uninitialized_dtm_returns_false(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct hdcp_workqueue *work = alloc_test_workqueue_for_assr(test, adev);
	struct dc_link *link;
	bool ret;

	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, link);

	/* kzalloc leaves dtm_context.context.initialized == false */
	ret = enable_assr(work, link);

	KUNIT_EXPECT_FALSE(test, ret);
}

/**
 * dm_test_enable_assr_uninitialized_dtm_ignores_link - link untouched on failure
 * @test: KUnit test context
 *
 * On the "DTM TA not initialized" path enable_assr() returns before reading
 * any field of @link, so a NULL link must be tolerated and the call must
 * still return false.
 */
static void dm_test_enable_assr_uninitialized_dtm_ignores_link(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct hdcp_workqueue *work = alloc_test_workqueue_for_assr(test, adev);
	bool ret;

	/* link is not dereferenced before the initialized check. */
	ret = enable_assr(work, NULL);

	KUNIT_EXPECT_FALSE(test, ret);
}

/**
 * dm_test_enable_assr_initialized_builds_command_and_fails - full body, invoke bypassed
 * @test: KUnit test context
 *
 * With the DTM TA marked initialized and a valid shared buffer, enable_assr()
 * runs its full body: it acquires the DTM mutex, clears the shared command,
 * fills in the ASSR-enable command from @link and pre-sets the status to
 * GENERIC_FAILURE before invoking the TA.
 *
 * psp_dtm_invoke() is prevented from touching real firmware by marking the
 * device as an SR-IOV virtual function, which makes it return early without
 * modifying the shared status. The status therefore stays GENERIC_FAILURE, so
 * enable_assr() must return false. Inspecting the shared command afterwards
 * proves the body executed and consumed @link.
 */
static void dm_test_enable_assr_initialized_builds_command_and_fails(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct hdcp_workqueue *work = alloc_test_workqueue_for_assr(test, adev);
	struct psp_context *psp = work->hdcp.config.psp.handle;
	struct ta_dtm_shared_memory *dtm_cmd;
	struct dc_link *link;
	bool ret;

	dtm_cmd = kunit_kzalloc(test, sizeof(*dtm_cmd), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dtm_cmd);

	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, link);
	link->link_enc_hw_inst = 3;

	/* Wire up an "initialized" DTM TA with a real shared buffer. */
	psp->dtm_context.context.initialized = true;
	psp->dtm_context.context.mem_context.shared_buf = (uint8_t *)dtm_cmd;
	mutex_init(&psp->dtm_context.mutex);

	/*
	 * Force the SR-IOV VF early-return in psp_dtm_invoke() so no GPU
	 * command is submitted; the shared status is left untouched.
	 */
	adev->virt.caps |= AMDGPU_SRIOV_CAPS_IS_VF;

	ret = enable_assr(work, link);

	/* Status was never advanced to SUCCESS, so the call must fail. */
	KUNIT_EXPECT_FALSE(test, ret);
	/* The command body must have populated the shared buffer. */
	KUNIT_EXPECT_EQ(test, dtm_cmd->cmd_id, TA_DTM_COMMAND__TOPOLOGY_ASSR_ENABLE);
	KUNIT_EXPECT_EQ(test,
			dtm_cmd->dtm_in_message.topology_assr_enable.display_topology_dig_be_index,
			link->link_enc_hw_inst);
	KUNIT_EXPECT_EQ(test, dtm_cmd->dtm_status, TA_DTM_STATUS__GENERIC_FAILURE);
	/* The DTM mutex must be released after the guard scope exits. */
	KUNIT_EXPECT_FALSE(test, mutex_is_locked(&psp->dtm_context.mutex));
}

/* End of tests for enable_assr() */

static struct kunit_case dm_hdcp_test_cases[] = {
	/* hdcp_get_content_protection_from_status() */
	KUNIT_CASE(dm_test_hdcp_get_cp_disabled_returns_desired),
	KUNIT_CASE(dm_test_hdcp_get_cp_type0_returns_enabled),
	KUNIT_CASE(dm_test_hdcp_get_cp_type1_returns_enabled),
	KUNIT_CASE(dm_test_hdcp_get_cp_type1_rejects_type0_status),
	KUNIT_CASE(dm_test_hdcp_get_cp_type0_rejects_type1_status),
	/* hdcp_get_link_display_adjustments() */
	KUNIT_CASE(dm_test_hdcp_get_adjustments_disable_authentication),
	KUNIT_CASE(dm_test_hdcp_get_adjustments_type0_policy),
	KUNIT_CASE(dm_test_hdcp_get_adjustments_type1_policy),
	KUNIT_CASE(dm_test_hdcp_get_adjustments_fused_io_enables_fw_check),
	/* process_output() */
	KUNIT_CASE(dm_test_process_output_property_validate_always_scheduled),
	KUNIT_CASE(dm_test_process_output_callback_needed),
	KUNIT_CASE(dm_test_process_output_callback_stop),
	KUNIT_CASE(dm_test_process_output_watchdog_needed),
	KUNIT_CASE(dm_test_process_output_watchdog_stop),
	KUNIT_CASE(dm_test_process_output_callback_and_watchdog_needed),
	KUNIT_CASE(dm_test_process_output_callback_stop_and_needed_requeues),
	KUNIT_CASE(dm_test_process_output_watchdog_stop_and_needed_requeues),
	/* event_property_update() */
	KUNIT_CASE(dm_test_event_property_update_skips_null_connector),
	/* event_callback() */
	KUNIT_CASE(dm_test_event_callback_cancels_callback_dwork),
	KUNIT_CASE(dm_test_event_callback_schedules_property_validate),
	/* event_property_validate() */
	KUNIT_CASE(dm_test_event_property_validate_skips_null_connector),
	KUNIT_CASE(dm_test_event_property_validate_skips_disconnected),
	KUNIT_CASE(dm_test_event_property_validate_skips_null_state),
	KUNIT_CASE(dm_test_event_property_validate_updates_on_status_change),
	KUNIT_CASE(dm_test_event_property_validate_no_update_when_unchanged),
	/* event_watchdog_timer() */
	KUNIT_CASE(dm_test_event_watchdog_timer_cancels_watchdog_dwork),
	KUNIT_CASE(dm_test_event_watchdog_timer_schedules_property_validate),
	/* event_cpirq() */
	KUNIT_CASE(dm_test_event_cpirq_schedules_property_validate),
	KUNIT_CASE(dm_test_event_cpirq_leaves_callback_and_watchdog_idle),
	/* hdcp_handle_cpirq() */
	KUNIT_CASE(dm_test_hdcp_handle_cpirq_schedules_work),
	KUNIT_CASE(dm_test_hdcp_handle_cpirq_selects_link_index),
	/* hdcp_update_display() helper logic */
	KUNIT_CASE(dm_test_hdcp_update_display_enable_schedules_property_validate),
	KUNIT_CASE(dm_test_hdcp_update_display_disable_resets_status_and_cancels_validate),
	/* hdcp_create_workqueue() */
	KUNIT_CASE(dm_test_hdcp_create_workqueue_zero_max_links_returns_null),
	/* hdcp_destroy() */
	KUNIT_CASE(dm_test_hdcp_destroy_frees_and_removes_sysfs),
	KUNIT_CASE(dm_test_hdcp_destroy_zero_links_null_srm),
	/* link_lock() */
	KUNIT_CASE(dm_test_link_lock_locks_and_unlocks_all_links),
	KUNIT_CASE(dm_test_link_lock_zero_links_is_noop),
	/* psp_get_srm() / psp_set_srm() */
	KUNIT_CASE(dm_test_psp_get_srm_uninitialized_returns_null),
	KUNIT_CASE(dm_test_psp_set_srm_uninitialized_returns_einval),
	/* hdcp_update_display() / hdcp_remove_display() / hdcp_reset_display() */
	KUNIT_CASE(dm_test_hdcp_update_display_enable_registers_connector),
	KUNIT_CASE(dm_test_hdcp_update_display_disable_sets_status_off),
	KUNIT_CASE(dm_test_hdcp_remove_display_enabled_resets_cp),
	KUNIT_CASE(dm_test_hdcp_remove_display_null_state_clears_connector),
	KUNIT_CASE(dm_test_hdcp_reset_display_clears_all_state),
	/* enable_assr() */
	KUNIT_CASE(dm_test_enable_assr_uninitialized_dtm_returns_false),
	KUNIT_CASE(dm_test_enable_assr_uninitialized_dtm_ignores_link),
	KUNIT_CASE(dm_test_enable_assr_initialized_builds_command_and_fails),
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
