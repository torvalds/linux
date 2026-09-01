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
#include "inc/core_types.h"
#include "irq/irq_service.h"
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

/**
 * dm_test_crtc_set_vupdate_irq_dc_busy - Test vupdate irq when DC rejects request
 * @test: The KUnit test context
 *
 * With an OTG instance assigned but no DC attached, dc_interrupt_set() returns
 * false and the function must report the request as busy (-EBUSY).
 */
static void dm_test_crtc_set_vupdate_irq_dc_busy(struct kunit *test)
{
	struct amdgpu_crtc *acrtc;
	struct amdgpu_device *adev;

	adev = dm_kunit_alloc_adev(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	acrtc = kunit_kzalloc(test, sizeof(*acrtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, acrtc);

	acrtc->base.dev = &adev->ddev;
	acrtc->otg_inst = 0;

	/* adev->dm.dc is NULL, so dc_interrupt_set() returns false. */
	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_crtc_set_vupdate_irq(&acrtc->base, true), -EBUSY);
}

/* Per-source funcs let dc_interrupt_set() succeed without register access. */
static bool dm_test_vupdate_irq_src_set(struct irq_service *irq_service,
					const struct irq_source_info *info,
					bool enable)
{
	return true;
}

static bool dm_test_vupdate_irq_src_ack(struct irq_service *irq_service,
					const struct irq_source_info *info)
{
	return true;
}

static struct irq_source_info_funcs dm_test_vupdate_irq_src_funcs = {
	.set = dm_test_vupdate_irq_src_set,
	.ack = dm_test_vupdate_irq_src_ack,
};

/* A .set that fails so dc_interrupt_set() reports the source as busy. */
static bool dm_test_vupdate_irq_src_set_busy(struct irq_service *irq_service,
					     const struct irq_source_info *info,
					     bool enable)
{
	return false;
}

static struct irq_source_info_funcs dm_test_vupdate_irq_src_busy_funcs = {
	.set = dm_test_vupdate_irq_src_set_busy,
	.ack = dm_test_vupdate_irq_src_ack,
};

/**
 * dm_test_crtc_set_vupdate_irq_enable - Test vupdate irq enable/disable success
 * @test: The KUnit test context
 *
 * With an OTG instance assigned and a DC whose IRQ service accepts the request,
 * enabling and disabling the vupdate IRQ must both succeed (return 0).
 */
static void dm_test_crtc_set_vupdate_irq_enable(struct kunit *test)
{
	struct irq_source_info *info;
	struct resource_pool *res_pool;
	struct irq_service *irqs;
	struct amdgpu_crtc *acrtc;
	struct amdgpu_device *adev;
	struct dc *dc;
	int i;

	adev = dm_kunit_alloc_adev(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	acrtc = kunit_kzalloc(test, sizeof(*acrtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, acrtc);

	dc = dm_kunit_alloc_dc_with_ctx(test);
	res_pool = kunit_kzalloc(test, sizeof(*res_pool), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, res_pool);
	irqs = kunit_kzalloc(test, sizeof(*irqs), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, irqs);

	/* Populate the per-source info table so dc_interrupt_set() succeeds. */
	info = kunit_kzalloc(test, sizeof(*info) * DAL_IRQ_SOURCES_NUMBER,
			     GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, info);
	for (i = 0; i < DAL_IRQ_SOURCES_NUMBER; i++)
		info[i].funcs = &dm_test_vupdate_irq_src_funcs;

	irqs->info = info;
	res_pool->irqs = irqs;
	dc->res_pool = res_pool;
	adev->dm.dc = dc;

	acrtc->base.dev = &adev->ddev;
	acrtc->otg_inst = 0;

	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_crtc_set_vupdate_irq(&acrtc->base, true), 0);
	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_crtc_set_vupdate_irq(&acrtc->base, false), 0);
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

/**
 * dm_test_idle_worker_disabled_clears_running - Test worker exits when disabled
 * @test: The KUnit test context
 *
 * With the idle workqueue disabled, amdgpu_dm_idle_worker() must skip the idle
 * optimization loop entirely and leave the shared running flag cleared.
 */
static void dm_test_idle_worker_disabled_clears_running(struct kunit *test)
{
	struct idle_workqueue *idle_work;
	struct amdgpu_device *adev;

	adev = dm_kunit_alloc_adev(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	idle_work = kunit_kzalloc(test, sizeof(*idle_work), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, idle_work);

	idle_work->dm = &adev->dm;
	idle_work->enable = false;
	idle_work->running = true;
	/* The worker toggles running through dm->idle_workqueue. */
	adev->dm.idle_workqueue = idle_work;

	amdgpu_dm_idle_worker(&idle_work->work);

	KUNIT_EXPECT_FALSE(test, idle_work->running);
}

/**
 * dm_test_idle_worker_enabled_breaks_when_idle_disallowed - Test loop entry/exit
 * @test: The KUnit test context
 *
 * With the workqueue enabled but idle optimizations disallowed, the worker enters
 * the detection loop once, takes the early break, and clears the running flag.
 */
static void dm_test_idle_worker_enabled_breaks_when_idle_disallowed(struct kunit *test)
{
	struct idle_workqueue *idle_work;
	struct amdgpu_device *adev;

	adev = dm_kunit_alloc_adev(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	mutex_init(&adev->dm.dc_lock);
	adev->dm.dc = dm_kunit_alloc_dc_with_ctx(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev->dm.dc);
	/* First loop iteration breaks before any dc_allow_idle_optimizations(). */
	adev->dm.dc->idle_optimizations_allowed = false;

	idle_work = kunit_kzalloc(test, sizeof(*idle_work), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, idle_work);

	idle_work->dm = &adev->dm;
	idle_work->enable = true;
	adev->dm.idle_workqueue = idle_work;

	amdgpu_dm_idle_worker(&idle_work->work);

	KUNIT_EXPECT_FALSE(test, idle_work->running);
}

/**
 * dm_test_idle_worker_enabled_breaks_when_not_headless - Test second break path
 * @test: The KUnit test context
 *
 * With idle optimizations allowed, the worker passes the first branch and runs
 * dc_allow_idle_optimizations(). A connected display makes the device non-headless
 * while no PSR is active, so the worker takes the second break and stops running.
 */
static void dm_test_idle_worker_enabled_breaks_when_not_headless(struct kunit *test)
{
	struct idle_workqueue *idle_work;
	struct drm_connector *display;
	struct amdgpu_device *adev;

	adev = dm_kunit_alloc_adev(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	mutex_init(&adev->dm.dc_lock);
	adev->dm.adev = adev;
	adev->dm.ddev = dm_kunit_alloc_drm_with_connector_list(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev->dm.ddev);

	adev->dm.dc = dm_kunit_alloc_dc_with_ctx(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev->dm.dc);
	/* Allow idle so the first branch is skipped and dc_allow() is exercised. */
	adev->dm.dc->idle_optimizations_allowed = true;
	/* is_apu path avoids DC_LOG_DC()'s NULL-logger dereference. */
	adev->dm.dc->caps.is_apu = true;
	/* Empty stream list -> amdgpu_dm_psr_is_active_allowed() returns false. */
	adev->dm.dc->current_state = dm_kunit_alloc_dc_state(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev->dm.dc->current_state);

	/* A connected display makes amdgpu_dm_is_headless() false. */
	display = kunit_kzalloc(test, sizeof(*display), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, display);
	dm_test_add_connector(adev->dm.ddev, display, DRM_MODE_CONNECTOR_HDMIA,
			      connector_status_connected);

	idle_work = kunit_kzalloc(test, sizeof(*idle_work), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, idle_work);
	idle_work->dm = &adev->dm;
	idle_work->enable = true;
	adev->dm.idle_workqueue = idle_work;

	amdgpu_dm_idle_worker(&idle_work->work);

	KUNIT_EXPECT_FALSE(test, idle_work->running);
}

/*
 * Report success only when disabling idle. dc_allow_idle_optimizations() then
 * clears dc->idle_optimizations_allowed on the disable call but leaves it clear
 * on the re-enable call inside the worker's enable-body, so the next loop
 * iteration breaks at the first branch instead of looping forever.
 */
static bool dm_test_idle_apply_flip(struct dc *dc, bool enable)
{
	return !enable;
}

/**
 * dm_test_idle_worker_enabled_runs_body - Test the enable-body path
 * @test: The KUnit test context
 *
 * A headless device makes the second branch false so the worker runs the
 * enable-body (dc_post_update_surfaces_to_stream() + re-enable). An injected
 * hwss.apply_idle_power_optimizations() callback lets dc_allow_idle_optimizations()
 * clear idle_optimizations_allowed on the disable half, so the following loop
 * iteration breaks at the first branch and the worker stops.
 */
static void dm_test_idle_worker_enabled_runs_body(struct kunit *test)
{
	struct idle_workqueue *idle_work;
	struct amdgpu_device *adev;
	struct dal_logger *logger;

	adev = dm_kunit_alloc_adev(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	mutex_init(&adev->dm.dc_lock);
	adev->dm.adev = adev;
	/* Empty connector list keeps the device headless -> second branch false. */
	adev->dm.ddev = dm_kunit_alloc_drm_with_connector_list(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev->dm.ddev);

	adev->dm.dc = dm_kunit_alloc_dc_with_ctx(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev->dm.dc);
	/* Allow idle so the first branch is skipped and dc_allow() is exercised. */
	adev->dm.dc->idle_optimizations_allowed = true;
	/* is_apu path avoids DC_LOG_DC()'s NULL-logger dereference. */
	adev->dm.dc->caps.is_apu = true;
	/* dc_allow() logs via DC_LOG_DEBUG() when it flips the flag. */
	logger = kunit_kzalloc(test, sizeof(*logger), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, logger);
	logger->dev = &adev->ddev;
	adev->dm.dc->ctx->logger = logger;
	/* dc_allow() only flips the flag when clk_mgr and apply() are present. */
	adev->dm.dc->clk_mgr = dm_kunit_alloc_clk_mgr(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev->dm.dc->clk_mgr);
	adev->dm.dc->hwss.apply_idle_power_optimizations = dm_test_idle_apply_flip;

	idle_work = kunit_kzalloc(test, sizeof(*idle_work), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, idle_work);
	idle_work->dm = &adev->dm;
	idle_work->enable = true;
	adev->dm.idle_workqueue = idle_work;

	amdgpu_dm_idle_worker(&idle_work->work);

	/* Enable-body ran, then the next iteration disabled idle and stopped. */
	KUNIT_EXPECT_FALSE(test, adev->dm.dc->idle_optimizations_allowed);
	KUNIT_EXPECT_FALSE(test, idle_work->running);
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

/**
 * dm_test_crtc_set_static_screen_optimze_sr_entry_psr - Test SSO toggle drives PSR
 * @test: The KUnit test context
 *
 * With self-refresh entry allowed and a PSR version below DC_PSR_VERSION_SU_1,
 * the function must run both the replay and PSR event updates. The link has no
 * replay/PSR feature enabled, so both event helpers short-circuit safely. Both
 * SSO states are exercised to cover the set_vsync_event computation.
 */
static void dm_test_crtc_set_static_screen_optimze_sr_entry_psr(struct kunit *test)
{
	struct amdgpu_display_manager *dm;
	struct dc_link *link;
	struct dc_stream_state *stream;

	dm = kunit_kzalloc(test, sizeof(*dm), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dm);

	link = dm_kunit_alloc_link(test);
	stream = dm_kunit_alloc_stream(test, link);

	/* psr_version < DC_PSR_VERSION_SU_1 -> PSR event branch is taken. */
	link->psr_settings.psr_version = DC_PSR_VERSION_1;

	amdgpu_dm_crtc_set_static_screen_optimze(dm, stream, true, true);
	amdgpu_dm_crtc_set_static_screen_optimze(dm, stream, false, true);
}

/**
 * dm_test_crtc_set_static_screen_optimze_psr_su_skips - Test PSR SU skips PSR event
 * @test: The KUnit test context
 *
 * With self-refresh entry allowed and a PSR version of DC_PSR_VERSION_SU_1, the
 * function must update the replay event but skip the PSR event update.
 */
static void dm_test_crtc_set_static_screen_optimze_psr_su_skips(struct kunit *test)
{
	struct amdgpu_display_manager *dm;
	struct dc_link *link;
	struct dc_stream_state *stream;

	dm = kunit_kzalloc(test, sizeof(*dm), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dm);

	link = dm_kunit_alloc_link(test);
	stream = dm_kunit_alloc_stream(test, link);

	/* psr_version >= DC_PSR_VERSION_SU_1 -> PSR event branch is skipped. */
	link->psr_settings.psr_version = DC_PSR_VERSION_SU_1;

	amdgpu_dm_crtc_set_static_screen_optimze(dm, stream, true, true);
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

/* Stub IRQ source .set so amdgpu_irq_get()/put() pass their funcs->set check. */
static int dm_test_crtc_irq_src_set(struct amdgpu_device *adev,
				    struct amdgpu_irq_src *source,
				    unsigned int type,
				    enum amdgpu_interrupt_state state)
{
	return 0;
}

static const struct amdgpu_irq_src_funcs dm_test_crtc_irq_src_funcs = {
	.set = dm_test_crtc_irq_src_set,
};

/*
 * Stub get_vblank_timestamp so drm_crtc_vblank_restore() finds a non-NULL hook.
 * The CRTC is not registered on the device, so drm_crtc_from_index() returns
 * NULL and this callback is never actually invoked; it only satisfies the
 * WARN_ON_ONCE(!crtc->funcs->get_vblank_timestamp) sanity check.
 */
static bool dm_test_crtc_get_vblank_timestamp(struct drm_crtc *crtc,
					      int *max_error,
					      ktime_t *vblank_time,
					      bool in_vblank_irq)
{
	return false;
}

static const struct drm_crtc_funcs dm_test_crtc_funcs = {
	.get_vblank_timestamp = dm_test_crtc_get_vblank_timestamp,
};

/*
 * dm_test_crtc_arm_irq_src - Prime an IRQ source so get()/put() short-circuit.
 * @test: The KUnit test context
 * @src: The amdgpu IRQ source to arm
 * @count: Initial per-type reference count
 *
 * Seeds enabled_types[AMDGPU_CRTC_IRQ_VBLANK1] with @count and installs a
 * non-NULL funcs->set so amdgpu_irq_get()/amdgpu_irq_put() adjust the refcount
 * without ever reaching amdgpu_irq_update() (which would touch hardware).
 */
static void dm_test_crtc_arm_irq_src(struct kunit *test,
				     struct amdgpu_irq_src *src, int count)
{
	atomic_t *enabled;

	enabled = kunit_kzalloc(test, sizeof(*enabled), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, enabled);

	atomic_set(enabled, count);
	src->num_types = 1;
	src->enabled_types = enabled;
	src->funcs = &dm_test_crtc_irq_src_funcs;
}

/*
 * dm_test_crtc_setup_enable - Build an adev/CRTC primed for the vblank enable path.
 * @test: The KUnit test context
 * @adev_out: Receives the allocated device
 * @dce_version: DCE version stamped on the DC (controls dc_supports_vrr())
 *
 * Returns a CRTC whose enable path can run to completion: a configured
 * (enabled) CRTC with crtc_id 0, an initialized single-pipe vblank array, a DC
 * with @dce_version, a non-reset reset_domain and a dm_crtc_state carrying a
 * stream+link. IPS support stays disabled so drm_crtc_vblank_restore() is
 * skipped, and the IRQ subsystem is left uninstalled for callers to arm.
 */
static struct amdgpu_crtc *
dm_test_crtc_setup_enable(struct kunit *test, struct amdgpu_device **adev_out,
			  enum dce_version dce_version)
{
	struct amdgpu_reset_domain *reset_domain;
	struct dc_stream_state *stream;
	struct dm_crtc_state *dm_state;
	struct amdgpu_device *adev;
	struct amdgpu_crtc *acrtc;
	struct dc_link *link;

	adev = dm_kunit_alloc_adev(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	KUNIT_ASSERT_EQ(test, drm_vblank_init(&adev->ddev, 1), 0);

	adev->dm.dc = dm_kunit_alloc_dc_with_ctx(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev->dm.dc);
	adev->dm.dc->ctx->dce_version = dce_version;

	/* crtc_id 0 maps to a valid IRQ type only when a CRTC is registered. */
	adev->mode_info.num_crtc = 1;

	reset_domain = kunit_kzalloc(test, sizeof(*reset_domain), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reset_domain);
	adev->reset_domain = reset_domain;

	acrtc = kunit_kzalloc(test, sizeof(*acrtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, acrtc);
	acrtc->base.dev = &adev->ddev;
	acrtc->base.enabled = true;
	acrtc->crtc_id = 0;

	link = dm_kunit_alloc_link(test);
	stream = dm_kunit_alloc_stream(test, link);

	dm_state = kunit_kzalloc(test, sizeof(*dm_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dm_state);
	dm_state->stream = stream;
	acrtc->base.state = &dm_state->base;

	*adev_out = adev;
	return acrtc;
}

/**
 * dm_test_crtc_enable_vblank_full_path - Test the enable path runs to completion
 * @test: The KUnit test context
 *
 * With a configured CRTC on a VRR-capable DC and both crtc/pageflip IRQ sources
 * armed, the enable path walks the vupdate-irq branch (VRR active, OTG
 * unassigned so it returns early), acquires both IRQ references and completes
 * with no vblank workqueue queued.
 */
static void dm_test_crtc_enable_vblank_full_path(struct kunit *test)
{
	struct dm_crtc_state *acrtc_state;
	struct amdgpu_device *adev;
	struct amdgpu_crtc *acrtc;

	/* DCE_VERSION_8_0 supports VRR -> the vupdate-irq branch is walked. */
	acrtc = dm_test_crtc_setup_enable(test, &adev, DCE_VERSION_8_0);

	/* OTG unassigned -> amdgpu_dm_crtc_set_vupdate_irq() returns 0 early. */
	acrtc->otg_inst = -1;
	/* VRR active so the enable path takes the vupdate-irq branch. */
	acrtc_state = to_dm_crtc_state(acrtc->base.state);
	acrtc_state->freesync_config.state = VRR_STATE_ACTIVE_VARIABLE;

	adev->irq.installed = true;
	dm_test_crtc_arm_irq_src(test, &adev->crtc_irq, 1);
	dm_test_crtc_arm_irq_src(test, &adev->pageflip_irq, 1);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_crtc_enable_vblank(&acrtc->base), 0);
}

/**
 * dm_test_crtc_enable_vblank_vupdate_busy - Test vupdate failure aborts enable
 * @test: The KUnit test context
 *
 * When VRR is active and the DC rejects the vupdate IRQ request, the enable
 * path must propagate the error (-EBUSY) before touching the crtc/pageflip
 * IRQs.
 */
static void dm_test_crtc_enable_vblank_vupdate_busy(struct kunit *test)
{
	struct irq_source_info *info;
	struct resource_pool *res_pool;
	struct dm_crtc_state *acrtc_state;
	struct irq_service *irqs;
	struct amdgpu_device *adev;
	struct amdgpu_crtc *acrtc;
	int i;

	acrtc = dm_test_crtc_setup_enable(test, &adev, DCE_VERSION_8_0);

	/* OTG assigned and VRR active so set_vupdate_irq() calls into DC. */
	acrtc->otg_inst = 0;
	acrtc_state = to_dm_crtc_state(acrtc->base.state);
	acrtc_state->freesync_config.state = VRR_STATE_ACTIVE_VARIABLE;

	res_pool = kunit_kzalloc(test, sizeof(*res_pool), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, res_pool);
	irqs = kunit_kzalloc(test, sizeof(*irqs), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, irqs);

	/* Per-source .set fails so dc_interrupt_set() reports the source busy. */
	info = kunit_kzalloc(test, sizeof(*info) * DAL_IRQ_SOURCES_NUMBER,
			     GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, info);
	for (i = 0; i < DAL_IRQ_SOURCES_NUMBER; i++)
		info[i].funcs = &dm_test_vupdate_irq_src_busy_funcs;

	irqs->info = info;
	res_pool->irqs = irqs;
	adev->dm.dc->res_pool = res_pool;

	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_crtc_enable_vblank(&acrtc->base), -EBUSY);
}

/**
 * dm_test_crtc_enable_vblank_crtc_irq_error - Test crtc IRQ failure aborts enable
 * @test: The KUnit test context
 *
 * On a non-VRR DC the vupdate-irq branch is skipped. With the IRQ subsystem
 * uninstalled, amdgpu_irq_get() on the crtc IRQ returns -ENOENT and the enable
 * path must propagate it before touching the pageflip IRQ.
 */
static void dm_test_crtc_enable_vblank_crtc_irq_error(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct amdgpu_crtc *acrtc;

	/* DCE_VERSION_6_0 has no VRR, so the vupdate-irq branch is skipped. */
	acrtc = dm_test_crtc_setup_enable(test, &adev, DCE_VERSION_6_0);

	/* IRQ subsystem not installed -> amdgpu_irq_get() returns -ENOENT. */
	adev->irq.installed = false;

	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_crtc_enable_vblank(&acrtc->base), -ENOENT);
}

/**
 * dm_test_crtc_enable_vblank_in_reset - Test enable returns early during GPU reset
 * @test: The KUnit test context
 *
 * After acquiring the IRQ references, an in-progress GPU reset must short the
 * enable path so it returns 0 without queuing any vblank control work.
 */
static void dm_test_crtc_enable_vblank_in_reset(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct amdgpu_crtc *acrtc;

	/* DCE_VERSION_6_0 has no VRR, so the vupdate-irq branch is skipped. */
	acrtc = dm_test_crtc_setup_enable(test, &adev, DCE_VERSION_6_0);

	adev->irq.installed = true;
	dm_test_crtc_arm_irq_src(test, &adev->crtc_irq, 1);
	dm_test_crtc_arm_irq_src(test, &adev->pageflip_irq, 1);

	/* Mid-reset: return 0 before the vblank workqueue branch is reached. */
	atomic_set(&adev->reset_domain->in_gpu_reset, 1);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_crtc_enable_vblank(&acrtc->base), 0);
}

/**
 * dm_test_crtc_enable_vblank_queues_work - Test enable queues vblank control work
 * @test: The KUnit test context
 *
 * With a vblank control workqueue installed, the enable path allocates a work
 * item, retains the stream and queues the control worker. Draining the queue
 * runs the worker, which bumps the active vblank IRQ count. The initial ISM
 * state has no EXIT_IDLE_REQUESTED transition, so the worker only exercises the
 * vblank accounting (the ISM state machine is covered by its own tests).
 */
static void dm_test_crtc_enable_vblank_queues_work(struct kunit *test)
{
	struct dm_crtc_state *acrtc_state;
	struct amdgpu_device *adev;
	struct amdgpu_crtc *acrtc;

	/* DCE_VERSION_8_0 supports VRR -> the vupdate-irq branch is walked. */
	acrtc = dm_test_crtc_setup_enable(test, &adev, DCE_VERSION_8_0);

	/* OTG unassigned -> amdgpu_dm_crtc_set_vupdate_irq() returns 0 early. */
	acrtc->otg_inst = -1;
	acrtc_state = to_dm_crtc_state(acrtc->base.state);
	acrtc_state->freesync_config.state = VRR_STATE_ACTIVE_VARIABLE;

	adev->irq.installed = true;
	dm_test_crtc_arm_irq_src(test, &adev->crtc_irq, 1);
	dm_test_crtc_arm_irq_src(test, &adev->pageflip_irq, 1);

	/* Real workqueue so the queue_work() branch runs the control worker. */
	mutex_init(&adev->dm.dc_lock);
	adev->dm.vblank_control_workqueue =
		create_singlethread_workqueue("dm_test_vblank");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev->dm.vblank_control_workqueue);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_crtc_enable_vblank(&acrtc->base), 0);

	/* Drain the queued worker before the fixture is torn down, then tidy up. */
	destroy_workqueue(adev->dm.vblank_control_workqueue);
	adev->dm.vblank_control_workqueue = NULL;

	/* The queued worker ran and accounted the active vblank IRQ. */
	KUNIT_EXPECT_EQ(test, adev->dm.active_vblank_irq_count, 1);
}

/**
 * dm_test_crtc_enable_vblank_ips_restore - Test IPS/self-refresh vblank restore
 * @test: The KUnit test context
 *
 * When the DC advertises IPS support with IPS not fully disabled, self-refresh
 * is supported and immediate vblank disable is configured, the enable path must
 * call drm_crtc_vblank_restore() to estimate missed vblanks before arming the
 * IRQs. The enable path then runs to completion.
 */
static void dm_test_crtc_enable_vblank_ips_restore(struct kunit *test)
{
	struct dm_crtc_state *acrtc_state;
	struct drm_vblank_crtc *vblank;
	struct amdgpu_device *adev;
	struct amdgpu_crtc *acrtc;
	struct dc_link *link;

	/* DCE_VERSION_8_0 supports VRR -> the vupdate-irq branch is walked. */
	acrtc = dm_test_crtc_setup_enable(test, &adev, DCE_VERSION_8_0);

	/* OTG unassigned -> amdgpu_dm_crtc_set_vupdate_irq() returns 0 early. */
	acrtc->otg_inst = -1;
	acrtc_state = to_dm_crtc_state(acrtc->base.state);
	acrtc_state->freesync_config.state = VRR_STATE_ACTIVE_VARIABLE;

	/* Non-NULL get_vblank_timestamp keeps drm_crtc_vblank_restore() quiet. */
	acrtc->base.funcs = &dm_test_crtc_funcs;

	/* IPS enabled and not fully disabled -> first restore condition holds. */
	adev->dm.dc->caps.ips_support = true;
	adev->dm.dc->config.disable_ips = DMUB_IPS_ENABLE;

	/* Supported PSR version makes self-refresh supported. */
	link = acrtc_state->stream->link;
	link->psr_settings.psr_version = DC_PSR_VERSION_1;

	/* Immediate vblank disable is the last condition gating the restore. */
	vblank = drm_crtc_vblank_crtc(&acrtc->base);
	vblank->config.disable_immediate = true;

	adev->irq.installed = true;
	dm_test_crtc_arm_irq_src(test, &adev->crtc_irq, 1);
	dm_test_crtc_arm_irq_src(test, &adev->pageflip_irq, 1);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_crtc_enable_vblank(&acrtc->base), 0);
}

/**
 * dm_test_crtc_enable_vblank_ips_restore_replay - Test IPS restore via replay support
 * @test: The KUnit test context
 *
 * Same as dm_test_crtc_enable_vblank_ips_restore() but self-refresh support is
 * established through replay rather than PSR: the PSR version is unsupported, so
 * the sr_supported computation must fall through to pr->config.replay_supported.
 * The enable path still calls drm_crtc_vblank_restore() and completes.
 */
static void dm_test_crtc_enable_vblank_ips_restore_replay(struct kunit *test)
{
	struct dm_crtc_state *acrtc_state;
	struct drm_vblank_crtc *vblank;
	struct amdgpu_device *adev;
	struct amdgpu_crtc *acrtc;
	struct dc_link *link;

	/* DCE_VERSION_8_0 supports VRR -> the vupdate-irq branch is walked. */
	acrtc = dm_test_crtc_setup_enable(test, &adev, DCE_VERSION_8_0);

	/* OTG unassigned -> amdgpu_dm_crtc_set_vupdate_irq() returns 0 early. */
	acrtc->otg_inst = -1;
	acrtc_state = to_dm_crtc_state(acrtc->base.state);
	acrtc_state->freesync_config.state = VRR_STATE_ACTIVE_VARIABLE;

	/* Non-NULL get_vblank_timestamp keeps drm_crtc_vblank_restore() quiet. */
	acrtc->base.funcs = &dm_test_crtc_funcs;

	/* IPS enabled and not fully disabled -> first restore condition holds. */
	adev->dm.dc->caps.ips_support = true;
	adev->dm.dc->config.disable_ips = DMUB_IPS_ENABLE;

	/*
	 * PSR unsupported but replay supported -> sr_supported is driven by the
	 * pr->config.replay_supported side of the OR.
	 */
	link = acrtc_state->stream->link;
	link->psr_settings.psr_version = DC_PSR_VERSION_UNSUPPORTED;
	link->replay_settings.config.replay_supported = true;

	/* Immediate vblank disable is the last condition gating the restore. */
	vblank = drm_crtc_vblank_crtc(&acrtc->base);
	vblank->config.disable_immediate = true;

	adev->irq.installed = true;
	dm_test_crtc_arm_irq_src(test, &adev->crtc_irq, 1);
	dm_test_crtc_arm_irq_src(test, &adev->pageflip_irq, 1);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_crtc_enable_vblank(&acrtc->base), 0);
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

/* Tests for amdgpu_dm_crtc_count_crtc_active_planes() */

static void dm_test_add_plane(struct drm_device *dev, struct drm_plane *plane,
			      unsigned int index, enum drm_plane_type type)
{
	INIT_LIST_HEAD(&plane->head);
	plane->index = index;
	plane->type = type;
	list_add_tail(&plane->head, &dev->mode_config.plane_list);
}

/**
 * dm_test_count_crtc_active_planes_none - Test empty plane list counts zero
 * @test: The KUnit test context
 *
 * With no planes attached to the CRTC the active plane count must be zero.
 */
static void dm_test_count_crtc_active_planes_none(struct kunit *test)
{
	struct drm_crtc_state *crtc_state;
	struct drm_atomic_commit *state;
	struct drm_device *dev;

	dev = dm_kunit_alloc_drm_with_connector_list(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);
	INIT_LIST_HEAD(&dev->mode_config.plane_list);

	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, state);
	state->dev = dev;

	crtc_state = kunit_kzalloc(test, sizeof(*crtc_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, crtc_state);
	crtc_state->state = state;
	crtc_state->plane_mask = 0;

	KUNIT_EXPECT_EQ(test, amdgpu_dm_crtc_count_crtc_active_planes(crtc_state), 0);
}

/**
 * dm_test_count_crtc_active_planes_mixed - Test counting across all plane cases
 * @test: The KUnit test context
 *
 * Exercises every branch of the counting loop: a plane excluded by the mask,
 * a cursor plane (skipped), a masked plane with no new state (counted), a
 * masked plane with a framebuffer (counted) and a masked plane without a
 * framebuffer (not counted). Only two planes should be reported active.
 */
static void dm_test_count_crtc_active_planes_mixed(struct kunit *test)
{
	struct drm_plane *plane_no_state;
	struct drm_plane *plane_cursor;
	struct drm_plane *plane_with_fb;
	struct drm_plane *plane_no_fb;
	struct drm_plane *plane_excluded;
	struct drm_plane_state *ps_fb;
	struct drm_plane_state *ps_no_fb;
	struct drm_crtc_state *crtc_state;
	struct drm_atomic_commit *state;
	struct drm_framebuffer *fb;
	struct drm_device *dev;

	dev = dm_kunit_alloc_drm_with_connector_list(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);
	INIT_LIST_HEAD(&dev->mode_config.plane_list);

	plane_no_state = kunit_kzalloc(test, sizeof(*plane_no_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, plane_no_state);
	plane_cursor = kunit_kzalloc(test, sizeof(*plane_cursor), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, plane_cursor);
	plane_with_fb = kunit_kzalloc(test, sizeof(*plane_with_fb), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, plane_with_fb);
	plane_no_fb = kunit_kzalloc(test, sizeof(*plane_no_fb), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, plane_no_fb);
	plane_excluded = kunit_kzalloc(test, sizeof(*plane_excluded), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, plane_excluded);

	dm_test_add_plane(dev, plane_no_state, 0, DRM_PLANE_TYPE_PRIMARY);
	dm_test_add_plane(dev, plane_cursor, 1, DRM_PLANE_TYPE_CURSOR);
	dm_test_add_plane(dev, plane_with_fb, 2, DRM_PLANE_TYPE_PRIMARY);
	dm_test_add_plane(dev, plane_no_fb, 3, DRM_PLANE_TYPE_PRIMARY);
	dm_test_add_plane(dev, plane_excluded, 4, DRM_PLANE_TYPE_PRIMARY);

	fb = kunit_kzalloc(test, sizeof(*fb), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, fb);
	ps_fb = kunit_kzalloc(test, sizeof(*ps_fb), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ps_fb);
	ps_fb->fb = fb;
	ps_no_fb = kunit_kzalloc(test, sizeof(*ps_no_fb), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ps_no_fb);
	ps_no_fb->fb = NULL;

	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, state);
	state->dev = dev;
	state->planes = kunit_kzalloc(test, sizeof(*state->planes) * 5, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, state->planes);
	state->planes[0].new_state = NULL;
	state->planes[1].new_state = ps_fb;
	state->planes[2].new_state = ps_fb;
	state->planes[3].new_state = ps_no_fb;
	state->planes[4].new_state = ps_fb;

	crtc_state = kunit_kzalloc(test, sizeof(*crtc_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, crtc_state);
	crtc_state->state = state;
	/* Exclude plane index 4 from the CRTC. */
	crtc_state->plane_mask = BIT(0) | BIT(1) | BIT(2) | BIT(3);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_crtc_count_crtc_active_planes(crtc_state), 2);
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
 * dm_test_crtc_destroy_state_releases_stream - Test destroy releases the stream
 * @test: The KUnit test context
 *
 * When the CRTC state carries a DC stream, destroying the state must release a
 * stream reference. An extra reference is taken up front so the release leaves
 * the KUnit-managed reference intact rather than freeing the stream here.
 */
static void dm_test_crtc_destroy_state_releases_stream(struct kunit *test)
{
	struct dc_stream_state *stream;
	struct dm_crtc_state *dm_state;
	struct dc_link *link;

	link = dm_kunit_alloc_link(test);
	stream = dm_kunit_alloc_stream(test, link);

	/*
	 * Take an extra reference so amdgpu_dm_crtc_destroy_state() drops back to
	 * the KUnit-managed reference instead of freeing the stream.
	 */
	kref_get(&stream->refcount);

	/* destroy_state kfree()s the state, so use a plain (unmanaged) alloc. */
	dm_state = kzalloc_obj(*dm_state, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dm_state);
	dm_state->stream = stream;

	amdgpu_dm_crtc_destroy_state(NULL, &dm_state->base);

	/* One reference was dropped, leaving the KUnit-managed one. */
	KUNIT_EXPECT_EQ(test, kref_read(&stream->refcount), 1);
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
 * dm_test_crtc_handle_vblank_completes_cursor_only - Test event sent on vblank
 * @test: The KUnit test context
 *
 * A pending event whose flip is not AMDGPU_FLIP_SUBMITTED (a cursor-only commit)
 * must be signalled on vblank: the vblank event is sent, the vblank reference is
 * dropped and acrtc->event is cleared.
 */
static void dm_test_crtc_handle_vblank_completes_cursor_only(struct kunit *test)
{
	struct drm_pending_vblank_event *event;
	struct amdgpu_device *adev;
	struct amdgpu_crtc *acrtc;

	adev = dm_kunit_alloc_adev(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	KUNIT_ASSERT_EQ(test, drm_vblank_init(&adev->ddev, 1), 0);

	acrtc = kunit_kzalloc(test, sizeof(*acrtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, acrtc);

	/* drm_crtc_send_vblank_event() consumes (kfree()s) the event. */
	event = kzalloc_obj(*event, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, event);

	acrtc->base.dev = &adev->ddev;
	acrtc->event = event;
	acrtc->pflip_status = AMDGPU_FLIP_NONE;

	/*
	 * Take a vblank reference so the handler's drm_crtc_vblank_put() does not
	 * underflow. Mark vblank enabled so the get succeeds without a hardware
	 * enable hook.
	 */
	adev->ddev.vblank[0].enabled = true;
	KUNIT_ASSERT_EQ(test, drm_crtc_vblank_get(&acrtc->base), 0);

	amdgpu_dm_crtc_handle_vblank(acrtc);

	/* Cursor-only commit: event was signalled and cleared. */
	KUNIT_EXPECT_NULL(test, acrtc->event);
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

/**
 * dm_test_crtc_disable_vblank_vrr - Test disable path releases IRQs on a VRR DC
 * @test: The KUnit test context
 *
 * On a VRR-capable DC the disable path turns the vupdate IRQ off (OTG
 * unassigned so it returns early), releases the armed crtc and pageflip IRQ
 * references and completes without queuing vblank control work.
 */
static void dm_test_crtc_disable_vblank_vrr(struct kunit *test)
{
	struct amdgpu_reset_domain *reset_domain;
	struct amdgpu_device *adev;
	struct amdgpu_crtc *acrtc;

	adev = dm_kunit_alloc_adev(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	adev->dm.dc = dm_kunit_alloc_dc_with_ctx(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev->dm.dc);
	/* DCE_VERSION_8_0 supports VRR -> the vupdate-irq branch is walked. */
	adev->dm.dc->ctx->dce_version = DCE_VERSION_8_0;

	adev->mode_info.num_crtc = 1;
	adev->irq.installed = true;

	reset_domain = kunit_kzalloc(test, sizeof(*reset_domain), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reset_domain);
	adev->reset_domain = reset_domain;

	/* Seed with 2 so amdgpu_irq_put() drops to a non-zero refcount. */
	dm_test_crtc_arm_irq_src(test, &adev->crtc_irq, 2);
	dm_test_crtc_arm_irq_src(test, &adev->pageflip_irq, 2);

	acrtc = kunit_kzalloc(test, sizeof(*acrtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, acrtc);
	acrtc->base.dev = &adev->ddev;
	acrtc->crtc_id = 0;
	/* OTG unassigned -> amdgpu_dm_crtc_set_vupdate_irq() returns 0 early. */
	acrtc->otg_inst = -1;

	amdgpu_dm_crtc_disable_vblank(&acrtc->base);

	/* Both IRQ references were released without underflow. */
	KUNIT_EXPECT_EQ(test, atomic_read(&adev->crtc_irq.enabled_types[0]), 1);
	KUNIT_EXPECT_EQ(test, atomic_read(&adev->pageflip_irq.enabled_types[0]), 1);
}

/**
 * dm_test_crtc_disable_vblank_queues_work - Test disable queues work without a stream
 * @test: The KUnit test context
 *
 * With a vblank control workqueue installed and a CRTC state carrying no
 * stream, the disable path queues the control worker without retaining a
 * stream. Draining the queue runs the worker, which drops the active vblank IRQ
 * count. The ISM is seeded in a state with no ENTER_IDLE_REQUESTED transition
 * so the worker only exercises the vblank accounting.
 */
static void dm_test_crtc_disable_vblank_queues_work(struct kunit *test)
{
	struct amdgpu_reset_domain *reset_domain;
	struct dm_crtc_state *dm_state;
	struct amdgpu_device *adev;
	struct amdgpu_crtc *acrtc;

	adev = dm_kunit_alloc_adev(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	adev->dm.dc = dm_kunit_alloc_dc_with_ctx(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev->dm.dc);
	/* DCE_VERSION_6_0 has no VRR, so the vupdate-irq branch is skipped. */
	adev->dm.dc->ctx->dce_version = DCE_VERSION_6_0;
	adev->dm.active_vblank_irq_count = 2;

	adev->mode_info.num_crtc = 1;
	adev->irq.installed = true;

	reset_domain = kunit_kzalloc(test, sizeof(*reset_domain), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, reset_domain);
	adev->reset_domain = reset_domain;

	/* Seed with 2 so amdgpu_irq_put() drops to a non-zero refcount. */
	dm_test_crtc_arm_irq_src(test, &adev->crtc_irq, 2);
	dm_test_crtc_arm_irq_src(test, &adev->pageflip_irq, 2);

	acrtc = kunit_kzalloc(test, sizeof(*acrtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, acrtc);
	acrtc->base.dev = &adev->ddev;
	acrtc->crtc_id = 0;
	acrtc->otg_inst = -1;
	/*
	 * Seed the ISM in a state where ENTER_IDLE_REQUESTED does not transition
	 * so the worker skips the ISM power-state dispatch and its timers.
	 */
	acrtc->ism.current_state = DM_ISM_STATE_HYSTERESIS_WAITING;

	/* CRTC state with no stream -> the stream-retain branch is skipped. */
	dm_state = kunit_kzalloc(test, sizeof(*dm_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dm_state);
	acrtc->base.state = &dm_state->base;

	/* Real workqueue so the queue_work() branch runs the control worker. */
	mutex_init(&adev->dm.dc_lock);
	adev->dm.vblank_control_workqueue =
		create_singlethread_workqueue("dm_test_vblank");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev->dm.vblank_control_workqueue);

	amdgpu_dm_crtc_disable_vblank(&acrtc->base);

	/* Drain the queued worker before the fixture is torn down, then tidy up. */
	destroy_workqueue(adev->dm.vblank_control_workqueue);
	adev->dm.vblank_control_workqueue = NULL;

	/* The queued worker ran and decremented the active vblank IRQ count. */
	KUNIT_EXPECT_EQ(test, adev->dm.active_vblank_irq_count, 1);
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
	KUNIT_CASE(dm_test_crtc_set_vupdate_irq_dc_busy),
	KUNIT_CASE(dm_test_crtc_set_vupdate_irq_enable),
	/* idle_create_workqueue */
	KUNIT_CASE(dm_test_idle_create_workqueue),
	/* amdgpu_dm_idle_worker */
	KUNIT_CASE(dm_test_idle_worker_disabled_clears_running),
	KUNIT_CASE(dm_test_idle_worker_enabled_breaks_when_idle_disallowed),
	KUNIT_CASE(dm_test_idle_worker_enabled_breaks_when_not_headless),
	KUNIT_CASE(dm_test_idle_worker_enabled_runs_body),
	/* amdgpu_dm_crtc_set_static_screen_optimze */
	KUNIT_CASE(dm_test_crtc_set_static_screen_optimze_no_sr_entry),
	KUNIT_CASE(dm_test_crtc_set_static_screen_optimze_sr_entry_psr),
	KUNIT_CASE(dm_test_crtc_set_static_screen_optimze_psr_su_skips),
	/* amdgpu_dm_crtc_enable_vblank */
	KUNIT_CASE(dm_test_crtc_enable_vblank_rejects_unconfigured),
	KUNIT_CASE(dm_test_crtc_enable_vblank_full_path),
	KUNIT_CASE(dm_test_crtc_enable_vblank_vupdate_busy),
	KUNIT_CASE(dm_test_crtc_enable_vblank_crtc_irq_error),
	KUNIT_CASE(dm_test_crtc_enable_vblank_in_reset),
	KUNIT_CASE(dm_test_crtc_enable_vblank_queues_work),
	KUNIT_CASE(dm_test_crtc_enable_vblank_ips_restore),
	KUNIT_CASE(dm_test_crtc_enable_vblank_ips_restore_replay),
	/* amdgpu_dm_crtc_update_crtc_active_planes */
	KUNIT_CASE(dm_test_crtc_update_active_planes_no_stream),
	/* amdgpu_dm_crtc_count_crtc_active_planes */
	KUNIT_CASE(dm_test_count_crtc_active_planes_none),
	KUNIT_CASE(dm_test_count_crtc_active_planes_mixed),
	/* amdgpu_dm_crtc_duplicate_state */
	KUNIT_CASE(dm_test_crtc_duplicate_state_copies_fields),
	/* amdgpu_dm_crtc_reset_state */
	KUNIT_CASE(dm_test_crtc_reset_state_allocates_state),
	/* amdgpu_dm_crtc_destroy_state */
	KUNIT_CASE(dm_test_crtc_destroy_state_no_stream),
	KUNIT_CASE(dm_test_crtc_destroy_state_releases_stream),
	/* amdgpu_dm_crtc_handle_vblank */
	KUNIT_CASE(dm_test_crtc_handle_vblank_no_event),
	KUNIT_CASE(dm_test_crtc_handle_vblank_skips_when_flip_submitted),
	KUNIT_CASE(dm_test_crtc_handle_vblank_completes_cursor_only),
	/* amdgpu_dm_crtc_vblank_control_worker */
	KUNIT_CASE(dm_test_vblank_control_worker_enable_increments),
	KUNIT_CASE(dm_test_vblank_control_worker_disable_decrements),
	KUNIT_CASE(dm_test_vblank_control_worker_disable_clamps_zero),
	/* amdgpu_dm_crtc_disable_vblank */
	KUNIT_CASE(dm_test_crtc_disable_vblank_no_irq_installed),
	KUNIT_CASE(dm_test_crtc_disable_vblank_vrr),
	KUNIT_CASE(dm_test_crtc_disable_vblank_queues_work),
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
