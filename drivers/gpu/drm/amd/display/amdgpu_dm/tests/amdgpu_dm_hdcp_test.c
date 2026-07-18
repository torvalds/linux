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
	/* hdcp_handle_cpirq() */
	KUNIT_CASE(dm_test_hdcp_handle_cpirq_schedules_work),
	KUNIT_CASE(dm_test_hdcp_handle_cpirq_selects_link_index),
	/* hdcp_update_display() helper logic */
	KUNIT_CASE(dm_test_hdcp_update_display_enable_schedules_property_validate),
	KUNIT_CASE(dm_test_hdcp_update_display_disable_resets_status_and_cancels_validate),
	/* hdcp_create_workqueue() */
	KUNIT_CASE(dm_test_hdcp_create_workqueue_zero_max_links_returns_null),
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
