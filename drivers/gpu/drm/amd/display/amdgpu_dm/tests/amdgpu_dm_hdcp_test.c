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
#include <linux/i2c.h>

#include <drm/display/drm_dp_helper.h>

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
 * so event_property_update() can resolve container_of() safely. The mutex is
 * initialised as well because the connected path takes guard(mutex).
 */
static struct hdcp_workqueue *alloc_test_workqueue_for_property_update(struct kunit *test)
{
	struct hdcp_workqueue *work;

	work = kunit_kzalloc(test, sizeof(*work), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, work);

	mutex_init(&work->mutex);
	INIT_WORK(&work->property_update_work, dummy_work_fn);

	return work;
}

/**
 * alloc_update_connector - connector for event_property_update() tests
 * @test: KUnit test context for managed allocation
 * @status: drm connector detection status to assign
 * @conn_state: drm_connector_state to attach (may be NULL)
 * @dev: drm_device to attach as connector->dev (may be NULL)
 *
 * Allocates an amdgpu_dm_connector wired for the traversal in
 * event_property_update(). The caller supplies the state and device so the
 * various skip branches (disconnected, no state, no device) and the fully
 * connected path can all be exercised.
 */
static struct amdgpu_dm_connector *alloc_update_connector(struct kunit *test,
							  enum drm_connector_status status,
							  struct drm_connector_state *conn_state,
							  struct drm_device *dev)
{
	struct amdgpu_dm_connector *aconnector;

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);

	aconnector->base.status = status;
	aconnector->base.state = conn_state;
	aconnector->base.dev = dev;

	return aconnector;
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

/**
 * dm_test_event_property_update_skips_disconnected - disconnected is skipped
 * @test: KUnit test context
 *
 * A connector whose status is not connector_status_connected must be skipped
 * before any modeset lock is taken, leaving encryption_status untouched.
 */
static void dm_test_event_property_update_skips_disconnected(struct kunit *test)
{
	struct hdcp_workqueue *work = alloc_test_workqueue_for_property_update(test);
	struct drm_connector_state *conn_state;

	conn_state = kunit_kzalloc(test, sizeof(*conn_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, conn_state);

	work->aconnector[1] = alloc_update_connector(test, connector_status_disconnected,
						     conn_state, NULL);
	work->encryption_status[1] = MOD_HDCP_ENCRYPTION_STATUS_HDCP2_TYPE1_ON;

	event_property_update(&work->property_update_work);

	KUNIT_EXPECT_EQ(test, work->encryption_status[1],
			MOD_HDCP_ENCRYPTION_STATUS_HDCP2_TYPE1_ON);
}

/**
 * dm_test_event_property_update_skips_null_state - missing state is skipped
 * @test: KUnit test context
 *
 * A connected connector without a drm_connector_state must be skipped before
 * the modeset lock, leaving encryption_status unchanged.
 */
static void dm_test_event_property_update_skips_null_state(struct kunit *test)
{
	struct hdcp_workqueue *work = alloc_test_workqueue_for_property_update(test);

	work->aconnector[2] = alloc_update_connector(test, connector_status_connected,
						     NULL, NULL);
	work->encryption_status[2] = MOD_HDCP_ENCRYPTION_STATUS_HDCP2_TYPE1_ON;

	event_property_update(&work->property_update_work);

	KUNIT_EXPECT_EQ(test, work->encryption_status[2],
			MOD_HDCP_ENCRYPTION_STATUS_HDCP2_TYPE1_ON);
}

/**
 * dm_test_event_property_update_skips_null_dev - missing device is skipped
 * @test: KUnit test context
 *
 * A connected connector with state but no drm_device must be skipped before
 * the modeset lock, leaving encryption_status unchanged.
 */
static void dm_test_event_property_update_skips_null_dev(struct kunit *test)
{
	struct hdcp_workqueue *work = alloc_test_workqueue_for_property_update(test);
	struct drm_connector_state *conn_state;

	conn_state = kunit_kzalloc(test, sizeof(*conn_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, conn_state);

	work->aconnector[3] = alloc_update_connector(test, connector_status_connected,
						     conn_state, NULL);
	work->encryption_status[3] = MOD_HDCP_ENCRYPTION_STATUS_HDCP2_TYPE1_ON;

	event_property_update(&work->property_update_work);

	KUNIT_EXPECT_EQ(test, work->encryption_status[3],
			MOD_HDCP_ENCRYPTION_STATUS_HDCP2_TYPE1_ON);
}

/**
 * dm_test_event_property_update_desired_when_off - HDCP off maps to DESIRED
 * @test: KUnit test context
 *
 * A fully connected display with HDCP_OFF encryption drives the connected
 * path: the modeset lock is taken, hdcp_get_content_protection_from_status()
 * reports DRM_MODE_CONTENT_PROTECTION_DESIRED and
 * drm_hdcp_update_content_protection() is called. The connector state is
 * pre-set to DESIRED so the value is unchanged (no sysfs event) and the deep
 * path completes cleanly.
 */
static void dm_test_event_property_update_desired_when_off(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct hdcp_workqueue *work = alloc_test_workqueue_for_property_update(test);
	struct drm_connector_state *conn_state;

	conn_state = kunit_kzalloc(test, sizeof(*conn_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, conn_state);
	conn_state->content_protection = DRM_MODE_CONTENT_PROTECTION_DESIRED;

	work->aconnector[0] = alloc_update_connector(test, connector_status_connected,
						     conn_state, &adev->ddev);
	work->encryption_status[0] = MOD_HDCP_ENCRYPTION_STATUS_HDCP_OFF;

	event_property_update(&work->property_update_work);

	KUNIT_EXPECT_EQ(test, conn_state->content_protection,
			(unsigned int)DRM_MODE_CONTENT_PROTECTION_DESIRED);
	KUNIT_EXPECT_EQ(test, work->encryption_status[0],
			MOD_HDCP_ENCRYPTION_STATUS_HDCP_OFF);
}

/**
 * dm_test_event_property_update_enabled_when_encrypted - encrypted maps to ENABLED
 * @test: KUnit test context
 *
 * A fully connected display with TYPE0 content and HDCP1 encryption drives
 * the connected path where hdcp_get_content_protection_from_status() reports
 * DRM_MODE_CONTENT_PROTECTION_ENABLED. The connector state is pre-set to
 * ENABLED so drm_hdcp_update_content_protection() leaves it unchanged.
 */
static void dm_test_event_property_update_enabled_when_encrypted(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct hdcp_workqueue *work = alloc_test_workqueue_for_property_update(test);
	struct drm_connector_state *conn_state;

	conn_state = kunit_kzalloc(test, sizeof(*conn_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, conn_state);
	conn_state->hdcp_content_type = DRM_MODE_HDCP_CONTENT_TYPE0;
	conn_state->content_protection = DRM_MODE_CONTENT_PROTECTION_ENABLED;

	work->aconnector[0] = alloc_update_connector(test, connector_status_connected,
						     conn_state, &adev->ddev);
	work->encryption_status[0] = MOD_HDCP_ENCRYPTION_STATUS_HDCP1_ON;

	event_property_update(&work->property_update_work);

	KUNIT_EXPECT_EQ(test, conn_state->content_protection,
			(unsigned int)DRM_MODE_CONTENT_PROTECTION_ENABLED);
	KUNIT_EXPECT_EQ(test, work->encryption_status[0],
			MOD_HDCP_ENCRYPTION_STATUS_HDCP1_ON);
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

/**
 * dm_test_hdcp_create_workqueue_initializes_work - success path wires everything up
 * @test: KUnit test context
 *
 * With a single link, hdcp_create_workqueue() must allocate the workqueue and
 * both SRM buffers, record max_link, publish the cp_psp callbacks and handle,
 * and point every link's psp handle at the device psp. A non-matching
 * dce_version must leave dtm_v3_supported clear. hdcp_destroy() is used to
 * tear the workqueue down (it also removes the SRM sysfs file created on the
 * device kobject).
 */
static void dm_test_hdcp_create_workqueue_initializes_work(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct cp_psp cp_psp = {0};
	struct dc *dc = dm_kunit_alloc_dc_with_ctx(test);
	struct hdcp_workqueue *work;

	/* sysfs_create_bin_file() needs a real device kobject. */
	adev->dev = adev->ddev.dev;
	dc->caps.max_links = 1;
	/* DCE_VERSION_UNKNOWN (0) is not in the DTM v3 list. */
	dc->ctx->dce_version = DCE_VERSION_UNKNOWN;

	work = hdcp_create_workqueue(adev, &cp_psp, dc);

	KUNIT_ASSERT_NOT_NULL(test, work);
	KUNIT_EXPECT_EQ(test, work->max_link, 1);
	KUNIT_EXPECT_NOT_NULL(test, work->srm);
	KUNIT_EXPECT_NOT_NULL(test, work->srm_temp);
	KUNIT_EXPECT_PTR_EQ(test, (void *)cp_psp.funcs.update_stream_config,
			    (void *)update_config);
	KUNIT_EXPECT_PTR_EQ(test, (void *)cp_psp.funcs.enable_assr,
			    (void *)enable_assr);
	KUNIT_EXPECT_PTR_EQ(test, cp_psp.handle, work);
	KUNIT_EXPECT_PTR_EQ(test, work[0].hdcp.config.psp.handle, &adev->psp);
	KUNIT_EXPECT_EQ(test, work[0].hdcp.config.psp.caps.dtm_v3_supported, 0);

	hdcp_destroy(&adev->dev->kobj, work);
}

/**
 * dm_test_hdcp_create_workqueue_sets_dtm_v3_for_dcn31 - DCN 3.1 enables DTM v3
 * @test: KUnit test context
 *
 * On a DCN 3.1 device, hdcp_create_workqueue() must set the per-link
 * dtm_v3_supported cap; an unmatched dce_version must leave it clear.
 */
static void dm_test_hdcp_create_workqueue_sets_dtm_v3_for_dcn31(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct cp_psp cp_psp = {0};
	struct dc *dc = dm_kunit_alloc_dc_with_ctx(test);
	struct hdcp_workqueue *work;

	adev->dev = adev->ddev.dev;
	dc->caps.max_links = 1;
	dc->ctx->dce_version = DCN_VERSION_3_1;

	work = hdcp_create_workqueue(adev, &cp_psp, dc);

	KUNIT_ASSERT_NOT_NULL(test, work);
	KUNIT_EXPECT_EQ(test, work[0].hdcp.config.psp.caps.dtm_v3_supported, 1);

	hdcp_destroy(&adev->dev->kobj, work);
}

/**
 * dm_test_hdcp_create_workqueue_initializes_all_links - loop covers every link
 * @test: KUnit test context
 *
 * With more than one link, hdcp_create_workqueue() must run its init loop for
 * every entry: each link records max_link, points its psp handle at the device
 * psp and gets the DTM v3 cap set for a matching dce_version.
 */
static void dm_test_hdcp_create_workqueue_initializes_all_links(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct cp_psp cp_psp = {0};
	struct dc *dc = dm_kunit_alloc_dc_with_ctx(test);
	struct hdcp_workqueue *work;
	int i;

	adev->dev = adev->ddev.dev;
	dc->caps.max_links = 3;
	dc->ctx->dce_version = DCN_VERSION_3_1;

	work = hdcp_create_workqueue(adev, &cp_psp, dc);

	KUNIT_ASSERT_NOT_NULL(test, work);
	KUNIT_EXPECT_EQ(test, work->max_link, 3);
	for (i = 0; i < 3; i++) {
		KUNIT_EXPECT_PTR_EQ(test, work[i].hdcp.config.psp.handle,
				    &adev->psp);
		KUNIT_EXPECT_EQ(test, work[i].hdcp.config.psp.caps.dtm_v3_supported, 1);
	}

	hdcp_destroy(&adev->dev->kobj, work);
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

/**
 * dm_test_psp_set_srm_initialized_stages_command - initialized path builds the command
 * @test: KUnit test context
 *
 * With an initialized TA and the SR-IOV VF bypass, psp_hdcp_invoke() is a
 * no-op, so the shared command buffer keeps the values psp_set_srm() staged.
 * The function must copy the SRM into the SET_SRM in-message, record its size
 * and command id, and then fail the response validation (the zeroed reply has
 * valid_signature == 0), returning -EINVAL without updating srm_version.
 */
static void dm_test_psp_set_srm_initialized_stages_command(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct ta_hdcp_shared_memory *hdcp_cmd;
	struct psp_context *psp;
	uint32_t srm_version = 0xdead;
	u8 srm_buf[4] = {0x1, 0x2, 0x3, 0x4};
	int ret;

	KUNIT_ASSERT_NOT_NULL(test, adev);

	psp = kunit_kzalloc(test, sizeof(*psp), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, psp);
	hdcp_cmd = kunit_kzalloc(test, sizeof(*hdcp_cmd), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, hdcp_cmd);

	psp->adev = adev;
	psp->hdcp_context.context.initialized = true;
	psp->hdcp_context.context.mem_context.shared_buf = (uint8_t *)hdcp_cmd;

	/* SR-IOV VF makes psp_hdcp_invoke() return early without firmware. */
	adev->virt.caps |= AMDGPU_SRIOV_CAPS_IS_VF;

	ret = psp_set_srm(psp, srm_buf, sizeof(srm_buf), &srm_version);

	/* Response validation fails (valid_signature == 0 in the zeroed reply). */
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	KUNIT_EXPECT_EQ(test, srm_version, 0xdead);
	/* The initialized path must have staged the SET_SRM command. */
	KUNIT_EXPECT_EQ(test, hdcp_cmd->cmd_id, TA_HDCP_COMMAND__HDCP_SET_SRM);
	KUNIT_EXPECT_EQ(test, hdcp_cmd->in_msg.hdcp_set_srm.srm_buf_size,
			(uint32_t)sizeof(srm_buf));
	KUNIT_EXPECT_MEMEQ(test, hdcp_cmd->in_msg.hdcp_set_srm.srm_buf, srm_buf,
			   sizeof(srm_buf));
}

/* End of tests for psp_get_srm() and psp_set_srm() */

/* Tests for srm_data_write() and srm_data_read() */

/**
 * dm_test_srm_data_write_uninitialized_ta_keeps_srm - write with TA not initialized
 * @test: KUnit test context
 *
 * srm_data_write() always copies the incoming buffer into work->srm_temp and
 * returns the byte count. When the HDCP TA is not initialized, psp_set_srm()
 * fails, so the committed SRM (work->srm / work->srm_size) must stay untouched.
 */
static void dm_test_srm_data_write_uninitialized_ta_keeps_srm(struct kunit *test)
{
	struct hdcp_workqueue *work;
	struct psp_context *psp;
	u8 buf[4] = {0xAA, 0xBB, 0xCC, 0xDD};
	ssize_t ret;

	work = kunit_kzalloc(test, sizeof(*work), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, work);
	psp = kunit_kzalloc(test, sizeof(*psp), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, psp);

	work->max_link = 1;
	mutex_init(&work->mutex);
	/* kzalloc leaves hdcp_context.context.initialized == false */
	work->hdcp.config.psp.handle = psp;
	work->srm_temp = kunit_kzalloc(test, PSP_HDCP_SRM_FIRST_GEN_MAX_SIZE, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, work->srm_temp);
	work->srm = kunit_kzalloc(test, PSP_HDCP_SRM_FIRST_GEN_MAX_SIZE, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, work->srm);

	ret = srm_data_write(NULL, NULL, &work->attr, buf, 0, sizeof(buf));

	KUNIT_EXPECT_EQ(test, ret, (ssize_t)sizeof(buf));
	/* Incoming data is always staged into srm_temp. */
	KUNIT_EXPECT_MEMEQ(test, work->srm_temp, buf, sizeof(buf));
	/* psp_set_srm() failed, so the committed SRM must be unchanged. */
	KUNIT_EXPECT_EQ(test, work->srm_size, 0u);
}

/**
 * dm_test_srm_data_read_uninitialized_ta_returns_einval - read with TA not initialized
 * @test: KUnit test context
 *
 * When the HDCP TA is not initialized, psp_get_srm() returns NULL, so
 * srm_data_read() must take the error path and return -EINVAL.
 */
static void dm_test_srm_data_read_uninitialized_ta_returns_einval(struct kunit *test)
{
	struct hdcp_workqueue *work;
	struct psp_context *psp;
	u8 buf[4];
	ssize_t ret;

	work = kunit_kzalloc(test, sizeof(*work), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, work);
	psp = kunit_kzalloc(test, sizeof(*psp), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, psp);

	work->max_link = 1;
	mutex_init(&work->mutex);
	/* kzalloc leaves hdcp_context.context.initialized == false */
	work->hdcp.config.psp.handle = psp;

	ret = srm_data_read(NULL, NULL, &work->attr, buf, 0, sizeof(buf));

	KUNIT_EXPECT_EQ(test, ret, (ssize_t)-EINVAL);
}

/**
 * dm_test_srm_data_read_empty_srm_returns_zero - read of an empty SRM
 * @test: KUnit test context
 *
 * With an initialized TA and the SR-IOV VF bypass, psp_hdcp_invoke() is a
 * no-op and the zeroed shared buffer yields a SUCCESS status with srm_size 0.
 * psp_get_srm() then returns a non-NULL (empty) buffer, so srm_data_read()
 * takes the "nothing left to copy" path and returns 0.
 */
static void dm_test_srm_data_read_empty_srm_returns_zero(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct ta_hdcp_shared_memory *hdcp_cmd;
	struct hdcp_workqueue *work;
	struct psp_context *psp;
	u8 buf[4];
	ssize_t ret;

	KUNIT_ASSERT_NOT_NULL(test, adev);

	work = kunit_kzalloc(test, sizeof(*work), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, work);
	psp = kunit_kzalloc(test, sizeof(*psp), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, psp);
	hdcp_cmd = kunit_kzalloc(test, sizeof(*hdcp_cmd), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, hdcp_cmd);

	work->max_link = 1;
	mutex_init(&work->mutex);
	psp->adev = adev;
	psp->hdcp_context.context.initialized = true;
	psp->hdcp_context.context.mem_context.shared_buf = (uint8_t *)hdcp_cmd;
	work->hdcp.config.psp.handle = psp;

	/* SR-IOV VF makes psp_hdcp_invoke() return early without firmware. */
	adev->virt.caps |= AMDGPU_SRIOV_CAPS_IS_VF;

	ret = srm_data_read(NULL, NULL, &work->attr, buf, 0, sizeof(buf));

	KUNIT_EXPECT_EQ(test, ret, 0);
}

/* End of tests for srm_data_write() and srm_data_read() */

/* Tests for lp_write_i2c() / lp_read_i2c() / lp_write_dpcd() / lp_read_dpcd() */

/* Defined further below with the display helper tests. */
static struct amdgpu_dm_connector *alloc_test_connector(struct kunit *test,
						       unsigned int index);

/*
 * Recording fakes for the DDC layer. The lp_* wrappers build i2c/DPCD
 * transactions and forward them through dm_helpers_*, which end up calling
 * i2c_transfer() / drm_dp_dpcd_*(). These fakes capture the resulting
 * messages so the tests can assert what the wrappers built, without touching
 * real hardware. KUnit runs cases sequentially, so file-scope capture state
 * is reset at the start of each test.
 */
#define FAKE_DDC_MAX_MSGS 4

static struct fake_i2c_capture {
	int num;
	struct i2c_msg msgs[FAKE_DDC_MAX_MSGS];
} fake_i2c_cap;

static int fake_i2c_master_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs,
				int num)
{
	int i;

	fake_i2c_cap.num = num;
	for (i = 0; i < num && i < FAKE_DDC_MAX_MSGS; i++)
		fake_i2c_cap.msgs[i] = msgs[i];

	return num;
}

static u32 fake_i2c_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C;
}

static const struct i2c_algorithm fake_i2c_algo = {
	.master_xfer = fake_i2c_master_xfer,
	.functionality = fake_i2c_functionality,
};

static void fake_i2c_lock_bus(struct i2c_adapter *adap, unsigned int flags) {}
static int fake_i2c_trylock_bus(struct i2c_adapter *adap, unsigned int flags)
{
	return 1;
}
static void fake_i2c_unlock_bus(struct i2c_adapter *adap, unsigned int flags) {}

static const struct i2c_lock_operations fake_i2c_lock_ops = {
	.lock_bus = fake_i2c_lock_bus,
	.trylock_bus = fake_i2c_trylock_bus,
	.unlock_bus = fake_i2c_unlock_bus,
};

static struct fake_aux_capture {
	int calls;
	u8 request;
	unsigned int address;
	size_t size;
} fake_aux_cap;

static ssize_t fake_aux_transfer(struct drm_dp_aux *aux,
				 struct drm_dp_aux_msg *msg)
{
	fake_aux_cap.calls++;
	fake_aux_cap.request = msg->request;
	fake_aux_cap.address = msg->address;
	fake_aux_cap.size = msg->size;
	msg->reply = DP_AUX_NATIVE_REPLY_ACK;

	return msg->size;
}

/**
 * alloc_test_ddc_link - connector/link wired to the recording i2c + aux fakes
 * @test: KUnit test context for managed allocation
 *
 * Builds an amdgpu_dm_connector with a fake i2c adapter and a fake DP aux, and
 * points link->priv at the connector so dm_helpers_* find it. Returns the
 * dc_link that the lp_* wrappers take as their opaque handle.
 */
static struct dc_link *alloc_test_ddc_link(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector = alloc_test_connector(test, 0);
	struct amdgpu_i2c_adapter *i2c;
	struct dc_link *link;

	KUNIT_ASSERT_NOT_NULL(test, aconnector);

	i2c = kunit_kzalloc(test, sizeof(*i2c), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, i2c);
	i2c->base.algo = &fake_i2c_algo;
	i2c->base.lock_ops = &fake_i2c_lock_ops;
	aconnector->i2c = i2c;

	mutex_init(&aconnector->dm_dp_aux.aux.hw_mutex);
	aconnector->dm_dp_aux.aux.transfer = fake_aux_transfer;
	/* Skip the DPCD "throw away" probe read so we capture only our access. */
	aconnector->dm_dp_aux.aux.dpcd_probe_disabled = true;

	link = aconnector->dc_link;
	link->priv = aconnector;

	return link;
}

/**
 * dm_test_lp_write_i2c_builds_single_write_payload - write builds one i2c msg
 * @test: KUnit test context
 *
 * lp_write_i2c() must forward a single write payload carrying the address,
 * length and data buffer unchanged.
 */
static void dm_test_lp_write_i2c_builds_single_write_payload(struct kunit *test)
{
	struct dc_link *link = alloc_test_ddc_link(test);
	u8 data[3] = {0x11, 0x22, 0x33};
	bool ok;

	memset(&fake_i2c_cap, 0, sizeof(fake_i2c_cap));

	ok = lp_write_i2c(link, 0x3a, data, sizeof(data));

	KUNIT_EXPECT_TRUE(test, ok);
	KUNIT_ASSERT_EQ(test, fake_i2c_cap.num, 1);
	/* write => flags without I2C_M_RD */
	KUNIT_EXPECT_EQ(test, (int)fake_i2c_cap.msgs[0].flags, 0);
	KUNIT_EXPECT_EQ(test, (int)fake_i2c_cap.msgs[0].addr, 0x3a);
	KUNIT_EXPECT_EQ(test, (int)fake_i2c_cap.msgs[0].len, (int)sizeof(data));
	KUNIT_EXPECT_PTR_EQ(test, fake_i2c_cap.msgs[0].buf, (void *)data);
}

/**
 * dm_test_lp_read_i2c_builds_offset_then_read - read builds offset + read msgs
 * @test: KUnit test context
 *
 * lp_read_i2c() must build a 1-byte write of the offset followed by a
 * size-byte read into the caller buffer, both at the same address.
 */
static void dm_test_lp_read_i2c_builds_offset_then_read(struct kunit *test)
{
	struct dc_link *link = alloc_test_ddc_link(test);
	u8 data[4];
	bool ok;

	memset(&fake_i2c_cap, 0, sizeof(fake_i2c_cap));

	ok = lp_read_i2c(link, 0x50, 0x07, data, sizeof(data));

	KUNIT_EXPECT_TRUE(test, ok);
	KUNIT_ASSERT_EQ(test, fake_i2c_cap.num, 2);
	/* first: 1-byte write of the offset */
	KUNIT_EXPECT_EQ(test, (int)fake_i2c_cap.msgs[0].flags, 0);
	KUNIT_EXPECT_EQ(test, (int)fake_i2c_cap.msgs[0].addr, 0x50);
	KUNIT_EXPECT_EQ(test, (int)fake_i2c_cap.msgs[0].len, 1);
	/* second: size-byte read into the caller buffer */
	KUNIT_EXPECT_EQ(test, (int)fake_i2c_cap.msgs[1].flags, I2C_M_RD);
	KUNIT_EXPECT_EQ(test, (int)fake_i2c_cap.msgs[1].addr, 0x50);
	KUNIT_EXPECT_EQ(test, (int)fake_i2c_cap.msgs[1].len, (int)sizeof(data));
	KUNIT_EXPECT_PTR_EQ(test, fake_i2c_cap.msgs[1].buf, (void *)data);
}

/**
 * dm_test_lp_write_dpcd_forwards_native_write - write forwards a native write
 * @test: KUnit test context
 *
 * lp_write_dpcd() must issue a single DP_AUX_NATIVE_WRITE at the requested
 * address for the requested size.
 */
static void dm_test_lp_write_dpcd_forwards_native_write(struct kunit *test)
{
	struct dc_link *link = alloc_test_ddc_link(test);
	u8 data[2] = {0xDE, 0xAD};
	bool ok;

	memset(&fake_aux_cap, 0, sizeof(fake_aux_cap));

	ok = lp_write_dpcd(link, 0x68000, data, sizeof(data));

	KUNIT_EXPECT_TRUE(test, ok);
	KUNIT_EXPECT_EQ(test, fake_aux_cap.calls, 1);
	KUNIT_EXPECT_EQ(test, (int)fake_aux_cap.request, DP_AUX_NATIVE_WRITE);
	KUNIT_EXPECT_EQ(test, fake_aux_cap.address, 0x68000u);
	KUNIT_EXPECT_EQ(test, (int)fake_aux_cap.size, (int)sizeof(data));
}

/**
 * dm_test_lp_read_dpcd_forwards_native_read - read forwards a native read
 * @test: KUnit test context
 *
 * lp_read_dpcd() must issue a single DP_AUX_NATIVE_READ at the requested
 * address for the requested size.
 */
static void dm_test_lp_read_dpcd_forwards_native_read(struct kunit *test)
{
	struct dc_link *link = alloc_test_ddc_link(test);
	u8 data[4];
	bool ok;

	memset(&fake_aux_cap, 0, sizeof(fake_aux_cap));

	ok = lp_read_dpcd(link, 0x00220, data, sizeof(data));

	KUNIT_EXPECT_TRUE(test, ok);
	KUNIT_EXPECT_EQ(test, fake_aux_cap.calls, 1);
	KUNIT_EXPECT_EQ(test, (int)fake_aux_cap.request, DP_AUX_NATIVE_READ);
	KUNIT_EXPECT_EQ(test, fake_aux_cap.address, 0x00220u);
	KUNIT_EXPECT_EQ(test, (int)fake_aux_cap.size, (int)sizeof(data));
}

/**
 * dm_test_lp_write_i2c_no_connector_returns_false - missing connector fails
 * @test: KUnit test context
 *
 * When link->priv has no connector, dm_helpers_submit_i2c() cannot proceed,
 * so lp_write_i2c() must report failure without invoking the adapter.
 */
static void dm_test_lp_write_i2c_no_connector_returns_false(struct kunit *test)
{
	struct dc_link *link = alloc_test_ddc_link(test);
	u8 data[2] = {0x01, 0x02};
	bool ok;

	link->priv = NULL;
	memset(&fake_i2c_cap, 0, sizeof(fake_i2c_cap));

	ok = lp_write_i2c(link, 0x3a, data, sizeof(data));

	KUNIT_EXPECT_FALSE(test, ok);
	KUNIT_EXPECT_EQ(test, fake_i2c_cap.num, 0);
}

/**
 * dm_test_lp_read_dpcd_no_connector_returns_false - missing connector fails
 * @test: KUnit test context
 *
 * When link->priv has no connector, dm_helpers_dp_read_dpcd() cannot proceed,
 * so lp_read_dpcd() must report failure without invoking the aux transfer.
 */
static void dm_test_lp_read_dpcd_no_connector_returns_false(struct kunit *test)
{
	struct dc_link *link = alloc_test_ddc_link(test);
	u8 data[4];
	bool ok;

	link->priv = NULL;
	memset(&fake_aux_cap, 0, sizeof(fake_aux_cap));

	ok = lp_read_dpcd(link, 0x00220, data, sizeof(data));

	KUNIT_EXPECT_FALSE(test, ok);
	KUNIT_EXPECT_EQ(test, fake_aux_cap.calls, 0);
}

/* End of tests for lp_write_i2c() / lp_read_i2c() / lp_write_dpcd() / lp_read_dpcd() */

/*
 * Tests for lp_atomic_write_poll_read_i2c() / lp_atomic_write_poll_read_aux()
 *
 * These wrappers cast the opaque handle to a dc_link and forward to the
 * dc_fused_io helpers. The success path submits a fused-IO command sequence to
 * the DMCUB, which is out of reach for a unit test, so the coverage here is the
 * hardware-free early returns: a NULL link and a payload that fails conversion
 * (op size larger than the fused request buffer).
 */

/**
 * dm_test_lp_atomic_i2c_null_handle_returns_false - NULL link fails cleanly
 * @test: KUnit test context
 *
 * With a NULL handle the forwarded dc_link is NULL, so the helper must return
 * false without dereferencing anything.
 */
static void dm_test_lp_atomic_i2c_null_handle_returns_false(struct kunit *test)
{
	struct mod_hdcp_atomic_op_i2c op = { 0 };

	KUNIT_EXPECT_FALSE(test,
			   lp_atomic_write_poll_read_i2c(NULL, &op, &op, &op, 0, 0));
}

/**
 * dm_test_lp_atomic_i2c_oversized_op_returns_false - bad payload fails conversion
 * @test: KUnit test context
 *
 * An op whose size exceeds the fused request buffer must fail conversion, so
 * the helper returns false before any fused-IO submission. no_ddc_pin routes
 * the DDC line through aux_hw_inst, avoiding the GPIO pin dereference.
 */
static void dm_test_lp_atomic_i2c_oversized_op_returns_false(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector = alloc_test_connector(test, 0);
	struct mod_hdcp_atomic_op_i2c write = { .size = 0x100 };
	struct mod_hdcp_atomic_op_i2c op = { 0 };
	struct dc_link *link;

	KUNIT_ASSERT_NOT_NULL(test, aconnector);

	link = aconnector->dc_link;
	link->no_ddc_pin = true;

	KUNIT_EXPECT_FALSE(test,
			   lp_atomic_write_poll_read_i2c(link, &write, &op, &op, 0, 0));
}

/**
 * dm_test_lp_atomic_aux_null_handle_returns_false - NULL link fails cleanly
 * @test: KUnit test context
 *
 * With a NULL handle the forwarded dc_link is NULL, so the helper must return
 * false without dereferencing anything.
 */
static void dm_test_lp_atomic_aux_null_handle_returns_false(struct kunit *test)
{
	struct mod_hdcp_atomic_op_aux op = { 0 };

	KUNIT_EXPECT_FALSE(test,
			   lp_atomic_write_poll_read_aux(NULL, &op, &op, &op, 0, 0));
}

/**
 * dm_test_lp_atomic_aux_oversized_op_returns_false - bad payload fails conversion
 * @test: KUnit test context
 *
 * The aux helper reads the DDC line from link->ddc->ddc_pin->pin_data before
 * converting, so a minimal pin chain is wired up. An op larger than the fused
 * request buffer then fails conversion and the helper returns false without
 * any fused-IO submission.
 */
static void dm_test_lp_atomic_aux_oversized_op_returns_false(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector = alloc_test_connector(test, 0);
	struct mod_hdcp_atomic_op_aux write = { .size = 0x100 };
	struct mod_hdcp_atomic_op_aux op = { 0 };
	struct ddc_service *ddc;
	struct ddc *ddc_pin;
	struct dc_link *link;
	void *pin_data;

	KUNIT_ASSERT_NOT_NULL(test, aconnector);

	ddc = kunit_kzalloc(test, sizeof(*ddc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ddc);
	ddc_pin = kunit_kzalloc(test, sizeof(*ddc_pin), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ddc_pin);
	/* Only ->en is read; over-allocate so struct gpio stays opaque here. */
	pin_data = kunit_kzalloc(test, 128, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, pin_data);

	ddc_pin->pin_data = pin_data;
	ddc->ddc_pin = ddc_pin;

	link = aconnector->dc_link;
	link->ddc = ddc;

	KUNIT_EXPECT_FALSE(test,
			   lp_atomic_write_poll_read_aux(link, &write, &op, &op, 0, 0));
}

/* End of tests for lp_atomic_write_poll_read_i2c() / lp_atomic_write_poll_read_aux() */

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

/* Tests for update_config() */

/**
 * dm_test_update_config_null_connector_is_noop - NULL stream ctx returns early
 * @test: KUnit test context
 *
 * When config->dm_stream_ctx is NULL, update_config() must return before
 * touching the workqueue, leaving the per-link aconnector array untouched.
 */
static void dm_test_update_config_null_connector_is_noop(struct kunit *test)
{
	struct hdcp_workqueue *work = alloc_test_workqueue_locked(test);
	struct cp_psp_stream_config config = {0};

	config.dm_stream_ctx = NULL;

	update_config(work, &config);

	KUNIT_EXPECT_PTR_EQ(test, work->aconnector[0], NULL);
}

/**
 * dm_test_update_config_null_dc_link_is_noop - NULL dc_link returns early
 * @test: KUnit test context
 *
 * A connector without a dc_link must cause update_config() to return before
 * registering the connector, leaving the aconnector array untouched.
 */
static void dm_test_update_config_null_dc_link_is_noop(struct kunit *test)
{
	struct hdcp_workqueue *work = alloc_test_workqueue_locked(test);
	struct amdgpu_dm_connector *aconnector;
	struct cp_psp_stream_config config = {0};

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);
	aconnector->dc_link = NULL;

	config.dm_stream_ctx = aconnector;

	update_config(work, &config);

	KUNIT_EXPECT_PTR_EQ(test, work->aconnector[0], NULL);
}

/**
 * dm_test_update_config_dpms_off_removes_display - dpms_off path removes display
 * @test: KUnit test context
 *
 * With config->dpms_off set, update_config() must take the removal path:
 * hdcp_remove_display() reverts an ENABLED connector to DESIRED and clears
 * its per-link aconnector entry.
 */
static void dm_test_update_config_dpms_off_removes_display(struct kunit *test)
{
	struct hdcp_workqueue *work = alloc_test_workqueue_locked(test);
	struct amdgpu_dm_connector *aconnector = alloc_test_connector(test, 0);
	struct drm_connector_state *conn_state;
	struct cp_psp_stream_config config = {0};

	conn_state = kunit_kzalloc(test, sizeof(*conn_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, conn_state);
	conn_state->content_protection = DRM_MODE_CONTENT_PROTECTION_ENABLED;
	aconnector->base.state = conn_state;
	work->aconnector[0] = aconnector;

	config.dm_stream_ctx = aconnector;
	config.dpms_off = true;

	update_config(work, &config);

	KUNIT_EXPECT_EQ(test, conn_state->content_protection,
			DRM_MODE_CONTENT_PROTECTION_DESIRED);
	KUNIT_EXPECT_PTR_EQ(test, work->aconnector[0], NULL);

	cancel_delayed_work_sync(&work->property_validate_dwork);
	cancel_delayed_work_sync(&work->callback_dwork);
	cancel_delayed_work_sync(&work->watchdog_timer_dwork);
}

/**
 * dm_test_update_config_populates_display_and_link - active path fills state
 * @test: KUnit test context
 *
 * With dpms_off clear, update_config() must build the display and link from
 * @config, reset the connector's encryption_status to HDCP_OFF, register the
 * connector and reach process_output() (which enqueues property_validate).
 *
 * mod_hdcp_add_display() reaches add_display_to_topology(), which returns
 * early because the DTM TA is left uninitialized, so no firmware is touched.
 */
static void dm_test_update_config_populates_display_and_link(struct kunit *test)
{
	struct hdcp_workqueue *work = alloc_test_workqueue_locked(test);
	struct amdgpu_dm_connector *aconnector = alloc_test_connector(test, 0);
	struct psp_context *psp;
	struct cp_psp_stream_config config = {0};

	/* add_display_to_topology() dereferences the psp handle. */
	psp = kunit_kzalloc(test, sizeof(*psp), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, psp);
	work->hdcp.config.psp.handle = psp;

	config.dm_stream_ctx = aconnector;
	config.dpms_off = false;
	config.otg_inst = 1;
	config.dig_fe = 4;
	config.dig_be = 5;
	config.stream_enc_idx = 6;
	config.link_enc_idx = 7;
	config.dio_output_idx = 8;
	config.phy_idx = 2;

	update_config(work, &config);

	KUNIT_EXPECT_EQ(test, work->encryption_status[0],
			MOD_HDCP_ENCRYPTION_STATUS_HDCP_OFF);
	KUNIT_EXPECT_PTR_EQ(test, work->aconnector[0], aconnector);

	KUNIT_EXPECT_EQ(test, work->display.state, MOD_HDCP_DISPLAY_ACTIVE);
	KUNIT_EXPECT_EQ(test, work->display.controller,
			CONTROLLER_ID_D0 + config.otg_inst);
	KUNIT_EXPECT_EQ(test, work->display.dig_fe, config.dig_fe);
	KUNIT_EXPECT_EQ(test, work->display.stream_enc_idx, config.stream_enc_idx);

	KUNIT_EXPECT_EQ(test, work->link.dig_be, config.dig_be);
	KUNIT_EXPECT_EQ(test, work->link.link_enc_idx, config.link_enc_idx);
	KUNIT_EXPECT_EQ(test, work->link.dio_output_id, config.dio_output_idx);
	KUNIT_EXPECT_EQ(test, work->link.phy_idx, config.phy_idx);

	KUNIT_EXPECT_TRUE(test, work_pending(&work->property_validate_dwork.work));

	cancel_delayed_work_sync(&work->property_validate_dwork);
	cancel_delayed_work_sync(&work->callback_dwork);
	cancel_delayed_work_sync(&work->watchdog_timer_dwork);
}

/* End of tests for update_config() */

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
	KUNIT_CASE(dm_test_event_property_update_skips_disconnected),
	KUNIT_CASE(dm_test_event_property_update_skips_null_state),
	KUNIT_CASE(dm_test_event_property_update_skips_null_dev),
	KUNIT_CASE(dm_test_event_property_update_desired_when_off),
	KUNIT_CASE(dm_test_event_property_update_enabled_when_encrypted),
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
	KUNIT_CASE(dm_test_hdcp_create_workqueue_initializes_work),
	KUNIT_CASE(dm_test_hdcp_create_workqueue_sets_dtm_v3_for_dcn31),
	KUNIT_CASE(dm_test_hdcp_create_workqueue_initializes_all_links),
	/* hdcp_destroy() */
	KUNIT_CASE(dm_test_hdcp_destroy_frees_and_removes_sysfs),
	KUNIT_CASE(dm_test_hdcp_destroy_zero_links_null_srm),
	/* link_lock() */
	KUNIT_CASE(dm_test_link_lock_locks_and_unlocks_all_links),
	KUNIT_CASE(dm_test_link_lock_zero_links_is_noop),
	/* psp_get_srm() / psp_set_srm() */
	KUNIT_CASE(dm_test_psp_get_srm_uninitialized_returns_null),
	KUNIT_CASE(dm_test_psp_set_srm_uninitialized_returns_einval),
	KUNIT_CASE(dm_test_psp_set_srm_initialized_stages_command),
	/* srm_data_write() / srm_data_read() */
	KUNIT_CASE(dm_test_srm_data_write_uninitialized_ta_keeps_srm),
	KUNIT_CASE(dm_test_srm_data_read_uninitialized_ta_returns_einval),
	KUNIT_CASE(dm_test_srm_data_read_empty_srm_returns_zero),
	/* lp_write_i2c() / lp_read_i2c() / lp_write_dpcd() / lp_read_dpcd() */
	KUNIT_CASE(dm_test_lp_write_i2c_builds_single_write_payload),
	KUNIT_CASE(dm_test_lp_read_i2c_builds_offset_then_read),
	KUNIT_CASE(dm_test_lp_write_dpcd_forwards_native_write),
	KUNIT_CASE(dm_test_lp_read_dpcd_forwards_native_read),
	KUNIT_CASE(dm_test_lp_write_i2c_no_connector_returns_false),
	KUNIT_CASE(dm_test_lp_read_dpcd_no_connector_returns_false),
	/* lp_atomic_write_poll_read_i2c() / lp_atomic_write_poll_read_aux() */
	KUNIT_CASE(dm_test_lp_atomic_i2c_null_handle_returns_false),
	KUNIT_CASE(dm_test_lp_atomic_i2c_oversized_op_returns_false),
	KUNIT_CASE(dm_test_lp_atomic_aux_null_handle_returns_false),
	KUNIT_CASE(dm_test_lp_atomic_aux_oversized_op_returns_false),
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
	/* update_config() */
	KUNIT_CASE(dm_test_update_config_null_connector_is_noop),
	KUNIT_CASE(dm_test_update_config_null_dc_link_is_noop),
	KUNIT_CASE(dm_test_update_config_dpms_off_removes_display),
	KUNIT_CASE(dm_test_update_config_populates_display_and_link),
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
