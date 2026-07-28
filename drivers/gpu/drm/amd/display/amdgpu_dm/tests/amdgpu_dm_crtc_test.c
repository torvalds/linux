// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * KUnit tests for amdgpu_dm_crtc.c
 *
 * Copyright 2026 Advanced Micro Devices, Inc.
 */

#include <kunit/test.h>
#include <drm/drm_atomic.h>
#include <drm/drm_connector.h>
#include <drm/drm_kunit_helpers.h>

#include "dc.h"
#include "amdgpu.h"
#include "amdgpu_mode.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_crtc.h"
#include "amdgpu_dm_kunit_test_helpers.h"
#include "amdgpu_dm_irq_params.h"

/* Tests for amdgpu_dm_crtc_modeset_required() */

/**
 * dm_test_crtc_modeset_required_active_mode_changed - Test Crtc modeset required active mode changed
 * @test: The KUnit test context
 */
static void dm_test_crtc_modeset_required_active_mode_changed(struct kunit *test)
{
	struct drm_crtc_state state = {};

	state.active = true;
	state.mode_changed = true;

	KUNIT_EXPECT_TRUE(test,
			  amdgpu_dm_crtc_modeset_required(&state, NULL, NULL));
}

/**
 * dm_test_crtc_modeset_required_active_active_changed - Test Crtc modeset required active active changed
 * @test: The KUnit test context
 */
static void dm_test_crtc_modeset_required_active_active_changed(struct kunit *test)
{
	struct drm_crtc_state state = {};

	state.active = true;
	state.active_changed = true;

	KUNIT_EXPECT_TRUE(test,
			  amdgpu_dm_crtc_modeset_required(&state, NULL, NULL));
}

/**
 * dm_test_crtc_modeset_required_active_connectors_changed - Test Crtc modeset required active connectors changed
 * @test: The KUnit test context
 */
static void dm_test_crtc_modeset_required_active_connectors_changed(struct kunit *test)
{
	struct drm_crtc_state state = {};

	state.active = true;
	state.connectors_changed = true;

	KUNIT_EXPECT_TRUE(test,
			  amdgpu_dm_crtc_modeset_required(&state, NULL, NULL));
}

/**
 * dm_test_crtc_modeset_required_inactive - Test Crtc modeset required inactive
 * @test: The KUnit test context
 */
static void dm_test_crtc_modeset_required_inactive(struct kunit *test)
{
	struct drm_crtc_state state = {};

	state.active = false;
	state.mode_changed = true;

	KUNIT_EXPECT_FALSE(test,
			   amdgpu_dm_crtc_modeset_required(&state, NULL, NULL));
}

/**
 * dm_test_crtc_modeset_required_no_changes - Test Crtc modeset required no changes
 * @test: The KUnit test context
 */
static void dm_test_crtc_modeset_required_no_changes(struct kunit *test)
{
	struct drm_crtc_state state = {};

	state.active = true;
	state.mode_changed = false;
	state.active_changed = false;
	state.connectors_changed = false;

	KUNIT_EXPECT_FALSE(test,
			   amdgpu_dm_crtc_modeset_required(&state, NULL, NULL));
}

/* Tests for amdgpu_dm_crtc_vrr_active_irq() */

/**
 * dm_test_crtc_vrr_active_irq_variable - Test Crtc vrr active irq variable
 * @test: The KUnit test context
 */
static void dm_test_crtc_vrr_active_irq_variable(struct kunit *test)
{
	struct amdgpu_crtc *acrtc = kunit_kzalloc(test, sizeof(*acrtc),
						  GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, acrtc);

	acrtc->dm_irq_params.freesync_config.state = VRR_STATE_ACTIVE_VARIABLE;

	KUNIT_EXPECT_TRUE(test, amdgpu_dm_crtc_vrr_active_irq(acrtc));
}

/**
 * dm_test_crtc_vrr_active_irq_fixed - Test Crtc vrr active irq fixed
 * @test: The KUnit test context
 */
static void dm_test_crtc_vrr_active_irq_fixed(struct kunit *test)
{
	struct amdgpu_crtc *acrtc = kunit_kzalloc(test, sizeof(*acrtc),
						  GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, acrtc);

	acrtc->dm_irq_params.freesync_config.state = VRR_STATE_ACTIVE_FIXED;

	KUNIT_EXPECT_TRUE(test, amdgpu_dm_crtc_vrr_active_irq(acrtc));
}

/**
 * dm_test_crtc_vrr_active_irq_inactive - Test Crtc vrr active irq inactive
 * @test: The KUnit test context
 */
static void dm_test_crtc_vrr_active_irq_inactive(struct kunit *test)
{
	struct amdgpu_crtc *acrtc = kunit_kzalloc(test, sizeof(*acrtc),
						  GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, acrtc);

	acrtc->dm_irq_params.freesync_config.state = VRR_STATE_INACTIVE;

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_crtc_vrr_active_irq(acrtc));
}

/**
 * dm_test_crtc_vrr_active_irq_disabled - Test Crtc vrr active irq disabled
 * @test: The KUnit test context
 */
static void dm_test_crtc_vrr_active_irq_disabled(struct kunit *test)
{
	struct amdgpu_crtc *acrtc = kunit_kzalloc(test, sizeof(*acrtc),
						  GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, acrtc);

	acrtc->dm_irq_params.freesync_config.state = VRR_STATE_DISABLED;

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_crtc_vrr_active_irq(acrtc));
}

/**
 * dm_test_crtc_vrr_active_irq_unsupported - Test Crtc vrr active irq unsupported
 * @test: The KUnit test context
 */
static void dm_test_crtc_vrr_active_irq_unsupported(struct kunit *test)
{
	struct amdgpu_crtc *acrtc = kunit_kzalloc(test, sizeof(*acrtc),
						  GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, acrtc);

	acrtc->dm_irq_params.freesync_config.state = VRR_STATE_UNSUPPORTED;

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_crtc_vrr_active_irq(acrtc));
}

/* Tests for amdgpu_dm_crtc_vrr_active() */

/**
 * dm_test_crtc_vrr_active_variable - Test Crtc vrr active variable
 * @test: The KUnit test context
 */
static void dm_test_crtc_vrr_active_variable(struct kunit *test)
{
	struct dm_crtc_state *dm_state = kunit_kzalloc(test,
						       sizeof(*dm_state),
						       GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dm_state);

	dm_state->freesync_config.state = VRR_STATE_ACTIVE_VARIABLE;

	KUNIT_EXPECT_TRUE(test, amdgpu_dm_crtc_vrr_active(dm_state));
}

/**
 * dm_test_crtc_vrr_active_fixed - Test Crtc vrr active fixed
 * @test: The KUnit test context
 */
static void dm_test_crtc_vrr_active_fixed(struct kunit *test)
{
	struct dm_crtc_state *dm_state = kunit_kzalloc(test,
						       sizeof(*dm_state),
						       GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dm_state);

	dm_state->freesync_config.state = VRR_STATE_ACTIVE_FIXED;

	KUNIT_EXPECT_TRUE(test, amdgpu_dm_crtc_vrr_active(dm_state));
}

/**
 * dm_test_crtc_vrr_active_inactive - Test Crtc vrr active inactive
 * @test: The KUnit test context
 */
static void dm_test_crtc_vrr_active_inactive(struct kunit *test)
{
	struct dm_crtc_state *dm_state = kunit_kzalloc(test,
						       sizeof(*dm_state),
						       GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dm_state);

	dm_state->freesync_config.state = VRR_STATE_INACTIVE;

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_crtc_vrr_active(dm_state));
}

/**
 * dm_test_crtc_vrr_active_disabled - Test Crtc vrr active disabled
 * @test: The KUnit test context
 */
static void dm_test_crtc_vrr_active_disabled(struct kunit *test)
{
	struct dm_crtc_state *dm_state = kunit_kzalloc(test,
						       sizeof(*dm_state),
						       GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dm_state);

	dm_state->freesync_config.state = VRR_STATE_DISABLED;

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_crtc_vrr_active(dm_state));
}

/**
 * dm_test_crtc_vrr_active_unsupported - Test Crtc vrr active unsupported
 * @test: The KUnit test context
 */
static void dm_test_crtc_vrr_active_unsupported(struct kunit *test)
{
	struct dm_crtc_state *dm_state = kunit_kzalloc(test,
						       sizeof(*dm_state),
						       GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dm_state);

	dm_state->freesync_config.state = VRR_STATE_UNSUPPORTED;

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_crtc_vrr_active(dm_state));
}

/* Tests for amdgpu_dm_is_headless() */

static void dm_test_add_connector(struct drm_device *dev,
				  struct drm_connector *connector,
				  int connector_type,
				  enum drm_connector_status status)
{
	INIT_LIST_HEAD(&connector->head);
	kref_init(&connector->base.refcount);
	connector->connector_type = connector_type;
	connector->status = status;
	list_add_tail(&connector->head, &dev->mode_config.connector_list);
}

/**
 * dm_test_crtc_is_headless_null_adev - Test Crtc is headless null adev
 * @test: The KUnit test context
 */
static void dm_test_crtc_is_headless_null_adev(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test, amdgpu_dm_is_headless(NULL));
}

/**
 * dm_test_crtc_is_headless_no_connectors - Test Crtc is headless no connectors
 * @test: The KUnit test context
 */
static void dm_test_crtc_is_headless_no_connectors(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct drm_device *dev = kunit_kzalloc(test, sizeof(*dev), GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	INIT_LIST_HEAD(&dev->mode_config.connector_list);
	spin_lock_init(&dev->mode_config.connector_list_lock);
	adev->dm.ddev = dev;

	KUNIT_EXPECT_TRUE(test, amdgpu_dm_is_headless(adev));
}

/**
 * dm_test_crtc_is_headless_writeback_only - Test Crtc is headless writeback only
 * @test: The KUnit test context
 */
static void dm_test_crtc_is_headless_writeback_only(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct drm_device *dev = kunit_kzalloc(test, sizeof(*dev), GFP_KERNEL);
	struct drm_connector *wb = kunit_kzalloc(test, sizeof(*wb), GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, wb);

	INIT_LIST_HEAD(&dev->mode_config.connector_list);
	spin_lock_init(&dev->mode_config.connector_list_lock);
	adev->dm.ddev = dev;

	dm_test_add_connector(dev, wb, DRM_MODE_CONNECTOR_WRITEBACK,
			      connector_status_connected);

	KUNIT_EXPECT_TRUE(test, amdgpu_dm_is_headless(adev));
}

/**
 * dm_test_crtc_is_headless_disconnected_display - Test Crtc is headless disconnected display
 * @test: The KUnit test context
 */
static void dm_test_crtc_is_headless_disconnected_display(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct drm_device *dev = kunit_kzalloc(test, sizeof(*dev), GFP_KERNEL);
	struct drm_connector *display = kunit_kzalloc(test, sizeof(*display), GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, display);

	INIT_LIST_HEAD(&dev->mode_config.connector_list);
	spin_lock_init(&dev->mode_config.connector_list_lock);
	adev->dm.ddev = dev;

	dm_test_add_connector(dev, display, DRM_MODE_CONNECTOR_HDMIA,
			      connector_status_disconnected);

	KUNIT_EXPECT_TRUE(test, amdgpu_dm_is_headless(adev));
}

/**
 * dm_test_crtc_is_headless_connected_display - Test Crtc is headless connected display
 * @test: The KUnit test context
 */
static void dm_test_crtc_is_headless_connected_display(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct drm_device *dev = kunit_kzalloc(test, sizeof(*dev), GFP_KERNEL);
	struct drm_connector *display = kunit_kzalloc(test, sizeof(*display), GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, display);

	INIT_LIST_HEAD(&dev->mode_config.connector_list);
	spin_lock_init(&dev->mode_config.connector_list_lock);
	adev->dm.ddev = dev;

	dm_test_add_connector(dev, display, DRM_MODE_CONNECTOR_HDMIA,
			      connector_status_connected);

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_is_headless(adev));
}

/**
 * dm_test_crtc_is_headless_mixed_connectors - Test headless skips WB and finds display
 * @test: The KUnit test context
 */
static void dm_test_crtc_is_headless_mixed_connectors(struct kunit *test)
{
	struct amdgpu_device *adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	struct drm_device *dev = kunit_kzalloc(test, sizeof(*dev), GFP_KERNEL);
	struct drm_connector *wb = kunit_kzalloc(test, sizeof(*wb), GFP_KERNEL);
	struct drm_connector *display = kunit_kzalloc(test, sizeof(*display), GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, wb);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, display);

	INIT_LIST_HEAD(&dev->mode_config.connector_list);
	spin_lock_init(&dev->mode_config.connector_list_lock);
	adev->dm.ddev = dev;

	dm_test_add_connector(dev, wb, DRM_MODE_CONNECTOR_WRITEBACK,
			      connector_status_connected);
	dm_test_add_connector(dev, display, DRM_MODE_CONNECTOR_DisplayPort,
			      connector_status_connected);

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_is_headless(adev));
}

/* Tests for amdgpu_dm_crtc_helper_mode_fixup() */

/**
 * dm_test_crtc_helper_mode_fixup_returns_true - Test mode_fixup accepts mode
 * @test: The KUnit test context
 */
static void dm_test_crtc_helper_mode_fixup_returns_true(struct kunit *test)
{
	struct drm_display_mode mode = { 0 };
	struct drm_display_mode adjusted_mode = { 0 };

	KUNIT_EXPECT_TRUE(test,
			  amdgpu_dm_crtc_helper_mode_fixup(NULL, &mode, &adjusted_mode));
}

/* Tests for amdgpu_dm_crtc_set_vupdate_irq() */

/**
 * dm_test_crtc_set_vupdate_irq_no_otg - Test vupdate irq with unassigned OTG
 * @test: The KUnit test context
 *
 * When the CRTC has no OTG instance assigned (otg_inst == -1) the function
 * must return 0 immediately without touching the DC interrupt state.
 */
static void dm_test_crtc_set_vupdate_irq_no_otg(struct kunit *test)
{
	struct amdgpu_crtc *acrtc;
	struct amdgpu_device *adev;

	adev = dm_kunit_alloc_adev(test);

	acrtc = kunit_kzalloc(test, sizeof(*acrtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, acrtc);

	acrtc->base.dev = &adev->ddev;
	acrtc->otg_inst = -1;

	KUNIT_EXPECT_EQ(test, amdgpu_dm_crtc_set_vupdate_irq(&acrtc->base, true), 0);
	KUNIT_EXPECT_EQ(test, amdgpu_dm_crtc_set_vupdate_irq(&acrtc->base, false), 0);
}

/* Tests for idle_create_workqueue() */

/**
 * dm_test_idle_create_workqueue - Test idle workqueue creation
 * @test: The KUnit test context
 *
 * Verify that idle_create_workqueue() allocates an idle workqueue tied to the
 * device's display manager and initializes it in a disabled, non-running state.
 */
static void dm_test_idle_create_workqueue(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct idle_workqueue *idle_work;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	idle_work = idle_create_workqueue(adev);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, idle_work);

	KUNIT_EXPECT_PTR_EQ(test, idle_work->dm, &adev->dm);
	KUNIT_EXPECT_FALSE(test, idle_work->enable);
	KUNIT_EXPECT_FALSE(test, idle_work->running);

	kfree(idle_work);
}

static struct kunit_case amdgpu_dm_crtc_tests[] = {
	/* amdgpu_dm_crtc_modeset_required */
	KUNIT_CASE(dm_test_crtc_modeset_required_active_mode_changed),
	KUNIT_CASE(dm_test_crtc_modeset_required_active_active_changed),
	KUNIT_CASE(dm_test_crtc_modeset_required_active_connectors_changed),
	KUNIT_CASE(dm_test_crtc_modeset_required_inactive),
	KUNIT_CASE(dm_test_crtc_modeset_required_no_changes),
	/* amdgpu_dm_crtc_vrr_active_irq */
	KUNIT_CASE(dm_test_crtc_vrr_active_irq_variable),
	KUNIT_CASE(dm_test_crtc_vrr_active_irq_fixed),
	KUNIT_CASE(dm_test_crtc_vrr_active_irq_inactive),
	KUNIT_CASE(dm_test_crtc_vrr_active_irq_disabled),
	KUNIT_CASE(dm_test_crtc_vrr_active_irq_unsupported),
	/* amdgpu_dm_crtc_vrr_active */
	KUNIT_CASE(dm_test_crtc_vrr_active_variable),
	KUNIT_CASE(dm_test_crtc_vrr_active_fixed),
	KUNIT_CASE(dm_test_crtc_vrr_active_inactive),
	KUNIT_CASE(dm_test_crtc_vrr_active_disabled),
	KUNIT_CASE(dm_test_crtc_vrr_active_unsupported),
	/* amdgpu_dm_is_headless */
	KUNIT_CASE(dm_test_crtc_is_headless_null_adev),
	KUNIT_CASE(dm_test_crtc_is_headless_no_connectors),
	KUNIT_CASE(dm_test_crtc_is_headless_writeback_only),
	KUNIT_CASE(dm_test_crtc_is_headless_disconnected_display),
	KUNIT_CASE(dm_test_crtc_is_headless_connected_display),
	KUNIT_CASE(dm_test_crtc_is_headless_mixed_connectors),
	/* amdgpu_dm_crtc_helper_mode_fixup */
	KUNIT_CASE(dm_test_crtc_helper_mode_fixup_returns_true),
	/* amdgpu_dm_crtc_set_vupdate_irq */
	KUNIT_CASE(dm_test_crtc_set_vupdate_irq_no_otg),
	/* idle_create_workqueue */
	KUNIT_CASE(dm_test_idle_create_workqueue),
	{}
};

static struct kunit_suite amdgpu_dm_crtc_test_suite = {
	.name = "amdgpu_dm_crtc",
	.test_cases = amdgpu_dm_crtc_tests,
};

kunit_test_suite(amdgpu_dm_crtc_test_suite);

MODULE_AUTHOR("AMD");
MODULE_DESCRIPTION("KUnit tests for amdgpu_dm_crtc");
MODULE_LICENSE("Dual MIT/GPL");
