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
#include <drm/drm_vblank.h>

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
	struct drm_device *dev = dm_kunit_alloc_drm_with_connector_list(test);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);
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
	struct drm_device *dev = dm_kunit_alloc_drm_with_connector_list(test);
	struct drm_connector *wb = kunit_kzalloc(test, sizeof(*wb), GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, wb);
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
	struct drm_device *dev = dm_kunit_alloc_drm_with_connector_list(test);
	struct drm_connector *display = kunit_kzalloc(test, sizeof(*display), GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, display);
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
	struct drm_device *dev = dm_kunit_alloc_drm_with_connector_list(test);
	struct drm_connector *display = kunit_kzalloc(test, sizeof(*display), GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, display);
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
	struct drm_device *dev = dm_kunit_alloc_drm_with_connector_list(test);
	struct drm_connector *wb = kunit_kzalloc(test, sizeof(*wb), GFP_KERNEL);
	struct drm_connector *display = kunit_kzalloc(test, sizeof(*display), GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, wb);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, display);
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

/* Tests for amdgpu_dm_crtc_set_static_screen_optimze() */

/**
 * dm_test_crtc_set_static_screen_optimze_no_sr_entry - Test early return when SR entry disallowed
 * @test: The KUnit test context
 *
 * When self-refresh entry is not allowed the function must return immediately
 * without touching replay or PSR events, regardless of the requested SSO state.
 */
static void dm_test_crtc_set_static_screen_optimze_no_sr_entry(struct kunit *test)
{
	struct amdgpu_display_manager *dm;
	struct dc_link *link;
	struct dc_stream_state *stream;

	dm = kunit_kzalloc(test, sizeof(*dm), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dm);

	link = dm_kunit_alloc_link(test);
	stream = dm_kunit_alloc_stream(test, link);

	/* allow_sr_entry == false -> returns before any event is set. */
	amdgpu_dm_crtc_set_static_screen_optimze(dm, stream, true, false);
	amdgpu_dm_crtc_set_static_screen_optimze(dm, stream, false, false);
}

/* Tests for amdgpu_dm_crtc_enable_vblank() */

/**
 * dm_test_crtc_enable_vblank_rejects_unconfigured - Test vblank enable on disabled CRTC
 * @test: The KUnit test context
 *
 * Enabling vblank on a CRTC that is not enabled must be rejected with -EINVAL
 * before any interrupt state is touched.
 */
static void dm_test_crtc_enable_vblank_rejects_unconfigured(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct amdgpu_crtc *acrtc;

	acrtc = kunit_kzalloc(test, sizeof(*acrtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, acrtc);

	acrtc->base.dev = &adev->ddev;
	acrtc->base.enabled = false;

	KUNIT_EXPECT_EQ(test, amdgpu_dm_crtc_enable_vblank(&acrtc->base), -EINVAL);
}

/* Tests for amdgpu_dm_crtc_update_crtc_active_planes() */

/**
 * dm_test_crtc_update_active_planes_no_stream - Test active plane reset without a stream
 * @test: The KUnit test context
 *
 * Without a DC stream attached the active plane count must be reset to zero
 * and the plane-counting path must be skipped.
 */
static void dm_test_crtc_update_active_planes_no_stream(struct kunit *test)
{
	struct dm_crtc_state *dm_state;

	dm_state = kunit_kzalloc(test, sizeof(*dm_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dm_state);

	dm_state->stream = NULL;
	dm_state->active_planes = 5;

	amdgpu_dm_crtc_update_crtc_active_planes(NULL, &dm_state->base);

	KUNIT_EXPECT_EQ(test, dm_state->active_planes, 0);
}

/* Tests for amdgpu_dm_crtc_duplicate_state() */

/**
 * dm_test_crtc_duplicate_state_copies_fields - Test duplicated state carries DM fields
 * @test: The KUnit test context
 *
 * Duplicating a CRTC state without a stream must produce a new state that
 * carries over the DM-specific fields from the current state.
 */
static void dm_test_crtc_duplicate_state_copies_fields(struct kunit *test)
{
	struct drm_crtc *crtc;
	struct dm_crtc_state *cur;
	struct drm_crtc_state *dup;
	struct dm_crtc_state *dm_dup;

	crtc = kunit_kzalloc(test, sizeof(*crtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, crtc);
	cur = kunit_kzalloc(test, sizeof(*cur), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, cur);

	cur->abm_level = 3;
	cur->active_planes = 2;
	cur->vrr_supported = true;
	cur->cm_has_degamma = true;
	cur->cm_is_degamma_srgb = true;
	cur->crc_skip_count = 7;
	cur->mpo_requested = true;
	crtc->state = &cur->base;

	dup = amdgpu_dm_crtc_duplicate_state(crtc);
	KUNIT_ASSERT_NOT_NULL(test, dup);

	dm_dup = to_dm_crtc_state(dup);
	KUNIT_EXPECT_EQ(test, dm_dup->abm_level, 3);
	KUNIT_EXPECT_EQ(test, dm_dup->active_planes, 2);
	KUNIT_EXPECT_TRUE(test, dm_dup->vrr_supported);
	KUNIT_EXPECT_TRUE(test, dm_dup->cm_has_degamma);
	KUNIT_EXPECT_TRUE(test, dm_dup->cm_is_degamma_srgb);
	KUNIT_EXPECT_EQ(test, dm_dup->crc_skip_count, 7);
	KUNIT_EXPECT_TRUE(test, dm_dup->mpo_requested);

	amdgpu_dm_crtc_destroy_state(crtc, dup);
}

/* Tests for amdgpu_dm_crtc_reset_state() */

/**
 * dm_test_crtc_reset_state_allocates_state - Test reset installs a fresh state
 * @test: The KUnit test context
 *
 * Resetting a CRTC with no existing state must allocate and install a new
 * drm_crtc_state.
 */
static void dm_test_crtc_reset_state_allocates_state(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct drm_crtc *crtc;

	crtc = kunit_kzalloc(test, sizeof(*crtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, crtc);
	crtc->dev = &adev->ddev;
	crtc->state = NULL;

	amdgpu_dm_crtc_reset_state(crtc);

	KUNIT_EXPECT_NOT_NULL(test, crtc->state);

	if (crtc->state)
		amdgpu_dm_crtc_destroy_state(crtc, crtc->state);
}

/* Tests for amdgpu_dm_crtc_destroy_state() */

/**
 * dm_test_crtc_destroy_state_no_stream - Test destroy frees a stream-less state
 * @test: The KUnit test context
 *
 * Destroying a CRTC state with no stream attached must free the state without
 * attempting to release a DC stream.
 */
static void dm_test_crtc_destroy_state_no_stream(struct kunit *test)
{
	struct dm_crtc_state *dm_state;

	/* destroy_state kfree()s the state, so use a plain (unmanaged) alloc. */
	dm_state = kzalloc_obj(*dm_state, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dm_state);

	amdgpu_dm_crtc_destroy_state(NULL, &dm_state->base);
}

/**
 * dm_test_crtc_handle_vblank_no_event - Test vblank handling with no pending event
 * @test: The KUnit test context
 *
 * With no flip event pending, handling a vblank must complete without sending a
 * vblank event and must leave acrtc->event untouched (NULL).
 */
static void dm_test_crtc_handle_vblank_no_event(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct amdgpu_crtc *acrtc;

	adev = dm_kunit_alloc_adev(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	/* Initialise vblank so drm_crtc_handle_vblank() runs cleanly. */
	KUNIT_ASSERT_EQ(test, drm_vblank_init(&adev->ddev, 1), 0);

	acrtc = kunit_kzalloc(test, sizeof(*acrtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, acrtc);
	acrtc->base.dev = &adev->ddev;
	acrtc->event = NULL;

	amdgpu_dm_crtc_handle_vblank(acrtc);

	KUNIT_EXPECT_NULL(test, acrtc->event);
}

/**
 * dm_test_crtc_handle_vblank_skips_when_flip_submitted - Test event kept on submit
 * @test: The KUnit test context
 *
 * A pending event whose flip is still AMDGPU_FLIP_SUBMITTED must not be signalled
 * on vblank; acrtc->event must remain set for later completion.
 */
static void dm_test_crtc_handle_vblank_skips_when_flip_submitted(struct kunit *test)
{
	struct drm_pending_vblank_event *event;
	struct amdgpu_device *adev;
	struct amdgpu_crtc *acrtc;

	adev = dm_kunit_alloc_adev(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	KUNIT_ASSERT_EQ(test, drm_vblank_init(&adev->ddev, 1), 0);

	acrtc = kunit_kzalloc(test, sizeof(*acrtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, acrtc);
	event = kunit_kzalloc(test, sizeof(*event), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, event);

	acrtc->base.dev = &adev->ddev;
	acrtc->event = event;
	acrtc->pflip_status = AMDGPU_FLIP_SUBMITTED;

	amdgpu_dm_crtc_handle_vblank(acrtc);

	/* Flip still in-flight: event must be preserved, not signalled. */
	KUNIT_EXPECT_PTR_EQ(test, acrtc->event, event);
}

/**
 * dm_test_vblank_control_worker_setup - Build a vblank_control_work for the worker
 * @test: The KUnit test context
 * @enable: Value for vblank_work->enable
 * @count: Initial dm->active_vblank_irq_count
 *
 * Returns a work item wired to a freshly allocated adev/crtc/stream. The CRTC is
 * left without an atomic state so amdgpu_dm_ism_commit_event() short-circuits and
 * only the vblank IRQ accounting in the worker runs.
 */
static struct vblank_control_work *
dm_test_vblank_control_worker_setup(struct kunit *test, bool enable,
				    uint32_t count)
{
	struct dc_stream_state *stream;
	struct vblank_control_work *work;
	struct amdgpu_device *adev;
	struct amdgpu_crtc *acrtc;

	adev = dm_kunit_alloc_adev(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	mutex_init(&adev->dm.dc_lock);
	adev->dm.dc = dm_kunit_alloc_dc_with_ctx(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev->dm.dc);
	adev->dm.active_vblank_irq_count = count;

	acrtc = kunit_kzalloc(test, sizeof(*acrtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, acrtc);
	acrtc->base.dev = &adev->ddev;
	acrtc->base.state = NULL;

	stream = dm_kunit_alloc_stream(test, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, stream);
	/* Worker releases the stream; keep an extra ref so kunit owns the free. */
	kref_get(&stream->refcount);

	/* Worker kfree()s the work item, so it must be a plain allocation. */
	work = kzalloc_obj(*work, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, work);
	work->dm = &adev->dm;
	work->acrtc = acrtc;
	work->stream = stream;
	work->enable = enable;

	return work;
}

/**
 * dm_test_vblank_control_worker_enable_increments - Test enable bumps IRQ count
 * @test: The KUnit test context
 *
 * Running the worker with enable set must increment the active vblank IRQ count.
 */
static void dm_test_vblank_control_worker_enable_increments(struct kunit *test)
{
	struct vblank_control_work *work;
	struct amdgpu_display_manager *dm;

	work = dm_test_vblank_control_worker_setup(test, true, 0);
	dm = work->dm;

	amdgpu_dm_crtc_vblank_control_worker(&work->work);

	KUNIT_EXPECT_EQ(test, dm->active_vblank_irq_count, 1);
}

/**
 * dm_test_vblank_control_worker_disable_decrements - Test disable drops IRQ count
 * @test: The KUnit test context
 *
 * Running the worker with enable clear must decrement a non-zero active vblank
 * IRQ count.
 */
static void dm_test_vblank_control_worker_disable_decrements(struct kunit *test)
{
	struct vblank_control_work *work;
	struct amdgpu_display_manager *dm;

	work = dm_test_vblank_control_worker_setup(test, false, 2);
	dm = work->dm;

	amdgpu_dm_crtc_vblank_control_worker(&work->work);

	KUNIT_EXPECT_EQ(test, dm->active_vblank_irq_count, 1);
}

/**
 * dm_test_vblank_control_worker_disable_clamps_zero - Test disable clamps at zero
 * @test: The KUnit test context
 *
 * Disabling when the active vblank IRQ count is already zero must not underflow.
 */
static void dm_test_vblank_control_worker_disable_clamps_zero(struct kunit *test)
{
	struct vblank_control_work *work;
	struct amdgpu_display_manager *dm;

	work = dm_test_vblank_control_worker_setup(test, false, 0);
	dm = work->dm;

	amdgpu_dm_crtc_vblank_control_worker(&work->work);

	KUNIT_EXPECT_EQ(test, dm->active_vblank_irq_count, 0);
}

/**
 * dm_test_crtc_disable_vblank_no_irq_installed - Test disable with IRQ uninstalled
 * @test: The KUnit test context
 *
 * Disabling vblank walks amdgpu_dm_crtc_set_vblank()'s disable path. With the
 * IRQ subsystem not installed, amdgpu_irq_put() returns early so the routine
 * completes without touching the vblank workqueue or the active IRQ count.
 */
static void dm_test_crtc_disable_vblank_no_irq_installed(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct amdgpu_crtc *acrtc;

	adev = dm_kunit_alloc_adev(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	adev->dm.dc = dm_kunit_alloc_dc_with_ctx(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev->dm.dc);
	/* DCE_VERSION_6_0 has no VRR, so the vupdate-irq branch is skipped. */
	adev->dm.dc->ctx->dce_version = DCE_VERSION_6_0;
	adev->dm.active_vblank_irq_count = 0;

	/* No CRTCs registered and IRQs not installed -> irq_put returns early. */
	adev->mode_info.num_crtc = 0;
	adev->irq.installed = false;

	acrtc = kunit_kzalloc(test, sizeof(*acrtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, acrtc);
	acrtc->base.dev = &adev->ddev;
	acrtc->crtc_id = 0;

	amdgpu_dm_crtc_disable_vblank(&acrtc->base);

	KUNIT_EXPECT_EQ(test, adev->dm.active_vblank_irq_count, 0);
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
	/* amdgpu_dm_crtc_set_static_screen_optimze */
	KUNIT_CASE(dm_test_crtc_set_static_screen_optimze_no_sr_entry),
	/* amdgpu_dm_crtc_enable_vblank */
	KUNIT_CASE(dm_test_crtc_enable_vblank_rejects_unconfigured),
	/* amdgpu_dm_crtc_update_crtc_active_planes */
	KUNIT_CASE(dm_test_crtc_update_active_planes_no_stream),
	/* amdgpu_dm_crtc_duplicate_state */
	KUNIT_CASE(dm_test_crtc_duplicate_state_copies_fields),
	/* amdgpu_dm_crtc_reset_state */
	KUNIT_CASE(dm_test_crtc_reset_state_allocates_state),
	/* amdgpu_dm_crtc_destroy_state */
	KUNIT_CASE(dm_test_crtc_destroy_state_no_stream),
	/* amdgpu_dm_crtc_handle_vblank */
	KUNIT_CASE(dm_test_crtc_handle_vblank_no_event),
	KUNIT_CASE(dm_test_crtc_handle_vblank_skips_when_flip_submitted),
	/* amdgpu_dm_crtc_vblank_control_worker */
	KUNIT_CASE(dm_test_vblank_control_worker_enable_increments),
	KUNIT_CASE(dm_test_vblank_control_worker_disable_decrements),
	KUNIT_CASE(dm_test_vblank_control_worker_disable_clamps_zero),
	/* amdgpu_dm_crtc_disable_vblank */
	KUNIT_CASE(dm_test_crtc_disable_vblank_no_irq_installed),
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
