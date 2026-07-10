// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * KUnit tests for amdgpu_dm.c
 *
 * Copyright 2026 Advanced Micro Devices, Inc.
 */

#include <kunit/test.h>
#include <linux/pci.h>
#include <drm/drm_atomic.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_modes.h>
#include <drm/drm_writeback.h>

#include "dc.h"
#include "inc/core_types.h"
#include "amd_shared.h"
#include "amdgpu.h"
#include "amdgpu_mode.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_kunit_test_helpers.h"

/* Tests for simple DM callbacks */

/**
 * dm_test_is_idle - Test placeholder idle callback returns true
 * @test: The KUnit test context
 */
static void dm_test_is_idle(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test, dm_is_idle(NULL));
}

/**
 * dm_test_wait_for_idle - Test placeholder wait-for-idle callback returns success
 * @test: The KUnit test context
 */
static void dm_test_wait_for_idle(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, dm_wait_for_idle(NULL), 0);
}

/**
 * dm_test_soft_reset - Test placeholder soft-reset callback returns success
 * @test: The KUnit test context
 */
static void dm_test_soft_reset(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, dm_soft_reset(NULL), 0);
}

/**
 * dm_test_set_clockgating_state - Test placeholder clockgating callback returns success
 * @test: The KUnit test context
 */
static void dm_test_set_clockgating_state(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, dm_set_clockgating_state(NULL, AMD_CG_STATE_GATE), 0);
}

/**
 * dm_test_set_powergating_state - Test placeholder powergating callback returns success
 * @test: The KUnit test context
 */
static void dm_test_set_powergating_state(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, dm_set_powergating_state(NULL, AMD_PG_STATE_GATE), 0);
}

/**
 * dm_test_bandwidth_update - Test placeholder bandwidth update is callable
 * @test: The KUnit test context
 */
static void dm_test_bandwidth_update(struct kunit *test)
{
	dm_bandwidth_update(NULL);
}

/**
 * dm_test_crtc_complete_writeback_no_connector - Test no writeback connector returns false
 * @test: The KUnit test context
 */
static void dm_test_crtc_complete_writeback_no_connector(struct kunit *test)
{
	struct amdgpu_crtc *acrtc;

	acrtc = kunit_kzalloc(test, sizeof(*acrtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, acrtc);

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_crtc_complete_writeback(acrtc));
}

/**
 * dm_test_crtc_complete_writeback_not_pending - Test non-pending writeback returns false
 * @test: The KUnit test context
 */
static void dm_test_crtc_complete_writeback_not_pending(struct kunit *test)
{
	struct amdgpu_crtc *acrtc;
	struct drm_writeback_connector *wb_conn;

	acrtc = kunit_kzalloc(test, sizeof(*acrtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, acrtc);
	wb_conn = kunit_kzalloc(test, sizeof(*wb_conn), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, wb_conn);

	spin_lock_init(&wb_conn->job_lock);
	acrtc->wb_conn = wb_conn;
	acrtc->wb_pending = false;

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_crtc_complete_writeback(acrtc));
}

/**
 * dm_test_vblank_get_counter_out_of_range - Test out-of-range CRTC returns zero
 * @test: The KUnit test context
 */
static void dm_test_vblank_get_counter_out_of_range(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);

	adev->mode_info.num_crtc = 1;

	KUNIT_EXPECT_EQ(test, dm_vblank_get_counter(adev, 1), 0U);
}

/**
 * dm_test_vblank_get_counter_no_stream - Test missing stream returns zero
 * @test: The KUnit test context
 */
static void dm_test_vblank_get_counter_no_stream(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct amdgpu_crtc *acrtc;

	acrtc = kunit_kzalloc(test, sizeof(*acrtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, acrtc);

	adev->mode_info.num_crtc = 1;
	adev->mode_info.crtcs[0] = acrtc;

	KUNIT_EXPECT_EQ(test, dm_vblank_get_counter(adev, 0), 0U);
}

/**
 * dm_test_crtc_get_scanoutpos_invalid_crtc - Test invalid CRTC returns -EINVAL
 * @test: The KUnit test context
 */
static void dm_test_crtc_get_scanoutpos_invalid_crtc(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	u32 vbl = 0;
	u32 position = 0;

	adev->mode_info.num_crtc = 1;

	KUNIT_EXPECT_EQ(test, dm_crtc_get_scanoutpos(adev, -1, &vbl, &position),
			-EINVAL);
	KUNIT_EXPECT_EQ(test, dm_crtc_get_scanoutpos(adev, 1, &vbl, &position),
			-EINVAL);
}

/**
 * dm_test_crtc_get_scanoutpos_no_stream - Test missing stream returns zero
 * @test: The KUnit test context
 */
static void dm_test_crtc_get_scanoutpos_no_stream(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct amdgpu_crtc *acrtc;
	u32 vbl = 0;
	u32 position = 0;

	acrtc = kunit_kzalloc(test, sizeof(*acrtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, acrtc);

	adev->mode_info.num_crtc = 1;
	adev->mode_info.crtcs[0] = acrtc;

	KUNIT_EXPECT_EQ(test, dm_crtc_get_scanoutpos(adev, 0, &vbl, &position), 0);
	KUNIT_EXPECT_EQ(test, vbl, 0U);
	KUNIT_EXPECT_EQ(test, position, 0U);
}

/**
 * dm_test_atomic_get_new_state_empty - Test empty atomic state has no DM state
 * @test: The KUnit test context
 */
static void dm_test_atomic_get_new_state_empty(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct drm_atomic_commit *state;

	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state);
	state->dev = &adev->ddev;

	KUNIT_EXPECT_NULL(test, dm_atomic_get_new_state(state));
}

/**
 * dm_test_atomic_get_new_state_match - Test atomic state returns matching DM private state
 * @test: The KUnit test context
 */
static void dm_test_atomic_get_new_state_match(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct dm_atomic_state *dm_state;
	struct drm_atomic_commit *state;

	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state);

	dm_state = kunit_kzalloc(test, sizeof(*dm_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dm_state);

	state->private_objs = kunit_kzalloc(test, sizeof(*state->private_objs),
					    GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state->private_objs);

	state->dev = &adev->ddev;
	state->num_private_objs = 1;
	state->private_objs[0].ptr = &adev->dm.atomic_obj;
	state->private_objs[0].new_state = &dm_state->base;

	KUNIT_EXPECT_PTR_EQ(test, dm_atomic_get_new_state(state), dm_state);
}

/**
 * dm_test_should_update_native_cursor_without_crtc - Test NULL crtc cases update native cursor
 * @test: The KUnit test context
 */
static void dm_test_should_update_native_cursor_without_crtc(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test, dm_should_update_native_cursor(NULL, NULL, NULL, false));
	KUNIT_EXPECT_TRUE(test, dm_should_update_native_cursor(NULL, NULL, NULL, true));
}

/**
 * dm_test_should_update_native_cursor_disable_native - Test disable path reads old crtc cursor mode
 * @test: The KUnit test context
 */
static void dm_test_should_update_native_cursor_disable_native(struct kunit *test)
{
	struct dm_crtc_state *dm_crtc_state;
	struct drm_atomic_commit *state;
	struct drm_crtc *crtc;

	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state);

	crtc = kunit_kzalloc(test, sizeof(*crtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, crtc);

	dm_crtc_state = kunit_kzalloc(test, sizeof(*dm_crtc_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dm_crtc_state);

	state->crtcs = kunit_kzalloc(test, sizeof(*state->crtcs), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state->crtcs);

	crtc->index = 0;
	dm_crtc_state->cursor_mode = DM_CURSOR_NATIVE_MODE;
	state->crtcs[0].old_state = &dm_crtc_state->base;

	KUNIT_EXPECT_TRUE(test,
			  dm_should_update_native_cursor(state, crtc, NULL, false));
}

/**
 * dm_test_should_update_native_cursor_enable_overlay - Test enable path reads new crtc cursor mode
 * @test: The KUnit test context
 */
static void dm_test_should_update_native_cursor_enable_overlay(struct kunit *test)
{
	struct dm_crtc_state *dm_crtc_state;
	struct drm_atomic_commit *state;
	struct drm_crtc *crtc;

	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state);

	crtc = kunit_kzalloc(test, sizeof(*crtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, crtc);

	dm_crtc_state = kunit_kzalloc(test, sizeof(*dm_crtc_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dm_crtc_state);

	state->crtcs = kunit_kzalloc(test, sizeof(*state->crtcs), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state->crtcs);

	crtc->index = 0;
	dm_crtc_state->cursor_mode = DM_CURSOR_OVERLAY_MODE;
	state->crtcs[0].new_state = &dm_crtc_state->base;

	KUNIT_EXPECT_FALSE(test,
			   dm_should_update_native_cursor(state, NULL, crtc, true));
}

/**
 * dm_test_atomic_destroy_state_no_context - Test destroying DM atomic state without a DC context
 * @test: The KUnit test context
 */
static void dm_test_atomic_destroy_state_no_context(struct kunit *test)
{
	struct dm_atomic_state *dm_state;

	/*
	 * Use kzalloc(), not kunit_kzalloc(): dm_atomic_destroy_state() frees
	 * the state itself, so KUnit-managed memory would be double-freed.
	 */
	dm_state = kzalloc(sizeof(*dm_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dm_state);

	/* context == NULL: dc_state_release() is skipped and the state is freed. */
	dm_atomic_destroy_state(NULL, &dm_state->base);
}

/**
 * dm_test_smu_write_watermarks_table_default - Test watermarks table skips non-Navi1x IP versions
 * @test: The KUnit test context
 */
static void dm_test_smu_write_watermarks_table_default(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);

	/*
	 * A zeroed adev reports DCE IP version 0, which is not one of the
	 * Navi1x versions handled by the switch, so the function returns early.
	 */
	KUNIT_EXPECT_EQ(test, amdgpu_dm_smu_write_watermarks_table(adev), 0);
}

/* Tests for dm_plane_layer_index_cmp() */

/**
 * dm_test_plane_layer_index_cmp_equal - Test Plane layer index cmp equal
 * @test: The KUnit test context
 */
static void dm_test_plane_layer_index_cmp_equal(struct kunit *test)
{
	struct dc_plane_state *plane_a;
	struct dc_plane_state *plane_b;
	struct dc_surface_update sa, sb;

	plane_a = kunit_kzalloc(test, sizeof(*plane_a), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, plane_a);
	plane_b = kunit_kzalloc(test, sizeof(*plane_b), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, plane_b);

	plane_a->layer_index = 5;
	plane_b->layer_index = 5;
	sa.surface = plane_a;
	sb.surface = plane_b;

	KUNIT_EXPECT_EQ(test, dm_plane_layer_index_cmp(&sa, &sb), 0);
}

/**
 * dm_test_plane_layer_index_cmp_descending - Test Plane layer index cmp descending
 * @test: The KUnit test context
 */
static void dm_test_plane_layer_index_cmp_descending(struct kunit *test)
{
	struct dc_plane_state *plane_a;
	struct dc_plane_state *plane_b;
	struct dc_surface_update sa, sb;

	plane_a = kunit_kzalloc(test, sizeof(*plane_a), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, plane_a);
	plane_b = kunit_kzalloc(test, sizeof(*plane_b), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, plane_b);

	plane_a->layer_index = 3;
	plane_b->layer_index = 7;
	sa.surface = plane_a;
	sb.surface = plane_b;

	/* b has higher index, so cmp(a,b) = b - a > 0 (b sorts first) */
	KUNIT_EXPECT_GT(test, dm_plane_layer_index_cmp(&sa, &sb), 0);
}

/**
 * dm_test_plane_layer_index_cmp_ascending - Test Plane layer index cmp ascending
 * @test: The KUnit test context
 */
static void dm_test_plane_layer_index_cmp_ascending(struct kunit *test)
{
	struct dc_plane_state *plane_a;
	struct dc_plane_state *plane_b;
	struct dc_surface_update sa, sb;

	plane_a = kunit_kzalloc(test, sizeof(*plane_a), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, plane_a);
	plane_b = kunit_kzalloc(test, sizeof(*plane_b), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, plane_b);

	plane_a->layer_index = 9;
	plane_b->layer_index = 2;
	sa.surface = plane_a;
	sb.surface = plane_b;

	/* a has higher index, so cmp(a,b) = b - a < 0 (a sorts first) */
	KUNIT_EXPECT_LT(test, dm_plane_layer_index_cmp(&sa, &sb), 0);
}

/* Tests for fill_plane_color_attributes() */

/**
 * dm_test_fill_color_attr_rgb_format - Test Fill color attr rgb format
 * @test: The KUnit test context
 */
static void dm_test_fill_color_attr_rgb_format(struct kunit *test)
{
	struct drm_plane_state plane_state = { 0 };
	enum dc_color_space color_space = COLOR_SPACE_UNKNOWN;
	int ret;

	/* RGB format: should return 0 and set SRGB regardless of encoding */
	plane_state.color_encoding = DRM_COLOR_YCBCR_BT709;
	plane_state.color_range = DRM_COLOR_YCBCR_FULL_RANGE;

	ret = fill_plane_color_attributes(&plane_state,
					  SURFACE_PIXEL_FORMAT_GRPH_ARGB8888,
					  &color_space);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, (int)color_space, (int)COLOR_SPACE_SRGB);
}

/**
 * dm_test_fill_color_attr_bt601_full - Test Fill color attr bt601 full
 * @test: The KUnit test context
 */
static void dm_test_fill_color_attr_bt601_full(struct kunit *test)
{
	struct drm_plane_state plane_state = { 0 };
	enum dc_color_space color_space = COLOR_SPACE_UNKNOWN;
	int ret;

	plane_state.color_encoding = DRM_COLOR_YCBCR_BT601;
	plane_state.color_range = DRM_COLOR_YCBCR_FULL_RANGE;

	ret = fill_plane_color_attributes(&plane_state,
					  SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr,
					  &color_space);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, (int)color_space, (int)COLOR_SPACE_YCBCR601);
}

/**
 * dm_test_fill_color_attr_bt601_limited - Test Fill color attr bt601 limited
 * @test: The KUnit test context
 */
static void dm_test_fill_color_attr_bt601_limited(struct kunit *test)
{
	struct drm_plane_state plane_state = { 0 };
	enum dc_color_space color_space = COLOR_SPACE_UNKNOWN;
	int ret;

	plane_state.color_encoding = DRM_COLOR_YCBCR_BT601;
	plane_state.color_range = DRM_COLOR_YCBCR_LIMITED_RANGE;

	ret = fill_plane_color_attributes(&plane_state,
					  SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr,
					  &color_space);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, (int)color_space,
			(int)COLOR_SPACE_YCBCR601_LIMITED);
}

/**
 * dm_test_fill_color_attr_bt709_full - Test Fill color attr bt709 full
 * @test: The KUnit test context
 */
static void dm_test_fill_color_attr_bt709_full(struct kunit *test)
{
	struct drm_plane_state plane_state = { 0 };
	enum dc_color_space color_space = COLOR_SPACE_UNKNOWN;
	int ret;

	plane_state.color_encoding = DRM_COLOR_YCBCR_BT709;
	plane_state.color_range = DRM_COLOR_YCBCR_FULL_RANGE;

	ret = fill_plane_color_attributes(&plane_state,
					  SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr,
					  &color_space);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, (int)color_space, (int)COLOR_SPACE_YCBCR709);
}

/**
 * dm_test_fill_color_attr_bt709_limited - Test Fill color attr bt709 limited
 * @test: The KUnit test context
 */
static void dm_test_fill_color_attr_bt709_limited(struct kunit *test)
{
	struct drm_plane_state plane_state = { 0 };
	enum dc_color_space color_space = COLOR_SPACE_UNKNOWN;
	int ret;

	plane_state.color_encoding = DRM_COLOR_YCBCR_BT709;
	plane_state.color_range = DRM_COLOR_YCBCR_LIMITED_RANGE;

	ret = fill_plane_color_attributes(&plane_state,
					  SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr,
					  &color_space);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, (int)color_space,
			(int)COLOR_SPACE_YCBCR709_LIMITED);
}

/**
 * dm_test_fill_color_attr_bt2020_full - Test Fill color attr bt2020 full
 * @test: The KUnit test context
 */
static void dm_test_fill_color_attr_bt2020_full(struct kunit *test)
{
	struct drm_plane_state plane_state = { 0 };
	enum dc_color_space color_space = COLOR_SPACE_UNKNOWN;
	int ret;

	plane_state.color_encoding = DRM_COLOR_YCBCR_BT2020;
	plane_state.color_range = DRM_COLOR_YCBCR_FULL_RANGE;

	ret = fill_plane_color_attributes(&plane_state,
					  SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr,
					  &color_space);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, (int)color_space,
			(int)COLOR_SPACE_2020_YCBCR_FULL);
}

/**
 * dm_test_fill_color_attr_bt2020_limited - Test Fill color attr bt2020 limited
 * @test: The KUnit test context
 */
static void dm_test_fill_color_attr_bt2020_limited(struct kunit *test)
{
	struct drm_plane_state plane_state = { 0 };
	enum dc_color_space color_space = COLOR_SPACE_UNKNOWN;
	int ret;

	plane_state.color_encoding = DRM_COLOR_YCBCR_BT2020;
	plane_state.color_range = DRM_COLOR_YCBCR_LIMITED_RANGE;

	ret = fill_plane_color_attributes(&plane_state,
					  SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr,
					  &color_space);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, (int)color_space,
			(int)COLOR_SPACE_2020_YCBCR_LIMITED);
}

/**
 * dm_test_fill_color_attr_invalid_encoding - Test Fill color attr invalid encoding
 * @test: The KUnit test context
 */
static void dm_test_fill_color_attr_invalid_encoding(struct kunit *test)
{
	struct drm_plane_state plane_state = { 0 };
	enum dc_color_space color_space = COLOR_SPACE_UNKNOWN;
	int ret;

	plane_state.color_encoding = 99;
	plane_state.color_range = DRM_COLOR_YCBCR_FULL_RANGE;

	ret = fill_plane_color_attributes(&plane_state,
					  SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr,
					  &color_space);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

/* Tests for modereset_required() */

/**
 * dm_test_modereset_required_when_inactive_and_modeset - Test Modereset required when inactive and modeset
 * @test: The KUnit test context
 */
static void dm_test_modereset_required_when_inactive_and_modeset(struct kunit *test)
{
	struct drm_crtc_state crtc_state = { 0 };

	crtc_state.active = false;
	crtc_state.mode_changed = true;

	KUNIT_EXPECT_TRUE(test, modereset_required(&crtc_state));
}

/**
 * dm_test_modereset_not_required_when_active_and_modeset - Test Modereset not required when active and modeset
 * @test: The KUnit test context
 */
static void dm_test_modereset_not_required_when_active_and_modeset(struct kunit *test)
{
	struct drm_crtc_state crtc_state = { 0 };

	crtc_state.active = true;
	crtc_state.mode_changed = true;

	KUNIT_EXPECT_FALSE(test, modereset_required(&crtc_state));
}

/**
 * dm_test_modereset_not_required_when_inactive_without_modeset - Test Modereset not required when inactive without modeset
 * @test: The KUnit test context
 */
static void dm_test_modereset_not_required_when_inactive_without_modeset(struct kunit *test)
{
	struct drm_crtc_state crtc_state = { 0 };

	crtc_state.active = false;
	crtc_state.mode_changed = false;

	KUNIT_EXPECT_FALSE(test, modereset_required(&crtc_state));
}

/* Tests for dm_get_oriented_plane_size() */

/**
 * dm_test_oriented_plane_size_rotate_0 - Test Oriented plane size rotate 0
 * @test: The KUnit test context
 */
static void dm_test_oriented_plane_size_rotate_0(struct kunit *test)
{
	struct drm_plane_state plane_state = { 0 };
	int src_w = 0;
	int src_h = 0;

	plane_state.rotation = DRM_MODE_ROTATE_0;
	plane_state.src_w = 1920 << 16;
	plane_state.src_h = 1080 << 16;

	dm_get_oriented_plane_size(&plane_state, &src_w, &src_h);

	KUNIT_EXPECT_EQ(test, src_w, 1920);
	KUNIT_EXPECT_EQ(test, src_h, 1080);
}

/**
 * dm_test_oriented_plane_size_rotate_90 - Test Oriented plane size rotate 90
 * @test: The KUnit test context
 */
static void dm_test_oriented_plane_size_rotate_90(struct kunit *test)
{
	struct drm_plane_state plane_state = { 0 };
	int src_w = 0;
	int src_h = 0;

	plane_state.rotation = DRM_MODE_ROTATE_90;
	plane_state.src_w = 1920 << 16;
	plane_state.src_h = 1080 << 16;

	dm_get_oriented_plane_size(&plane_state, &src_w, &src_h);

	KUNIT_EXPECT_EQ(test, src_w, 1080);
	KUNIT_EXPECT_EQ(test, src_h, 1920);
}

/**
 * dm_test_oriented_plane_size_rotate_180 - Test Oriented plane size rotate 180
 * @test: The KUnit test context
 */
static void dm_test_oriented_plane_size_rotate_180(struct kunit *test)
{
	struct drm_plane_state plane_state = { 0 };
	int src_w = 0;
	int src_h = 0;

	plane_state.rotation = DRM_MODE_ROTATE_180;
	plane_state.src_w = 1920 << 16;
	plane_state.src_h = 1080 << 16;

	dm_get_oriented_plane_size(&plane_state, &src_w, &src_h);

	KUNIT_EXPECT_EQ(test, src_w, 1920);
	KUNIT_EXPECT_EQ(test, src_h, 1080);
}

/**
 * dm_test_oriented_plane_size_rotate_270 - Test Oriented plane size rotate 270
 * @test: The KUnit test context
 */
static void dm_test_oriented_plane_size_rotate_270(struct kunit *test)
{
	struct drm_plane_state plane_state = { 0 };
	int src_w = 0;
	int src_h = 0;

	plane_state.rotation = DRM_MODE_ROTATE_270;
	plane_state.src_w = 1920 << 16;
	plane_state.src_h = 1080 << 16;

	dm_get_oriented_plane_size(&plane_state, &src_w, &src_h);

	KUNIT_EXPECT_EQ(test, src_w, 1080);
	KUNIT_EXPECT_EQ(test, src_h, 1920);
}

/* Tests for dm_get_plane_scale() */

/**
 * dm_test_get_plane_scale_identity - Test Get plane scale identity
 * @test: The KUnit test context
 */
static void dm_test_get_plane_scale_identity(struct kunit *test)
{
	struct drm_plane_state plane_state = { 0 };
	int scale_w = 0;
	int scale_h = 0;

	plane_state.rotation = DRM_MODE_ROTATE_0;
	plane_state.src_w = 1920 << 16;
	plane_state.src_h = 1080 << 16;
	plane_state.crtc_w = 1920;
	plane_state.crtc_h = 1080;

	dm_get_plane_scale(&plane_state, &scale_w, &scale_h);

	KUNIT_EXPECT_EQ(test, scale_w, 1000);
	KUNIT_EXPECT_EQ(test, scale_h, 1000);
}

/**
 * dm_test_get_plane_scale_rotate_90_identity - Test Get plane scale rotate 90 identity
 * @test: The KUnit test context
 */
static void dm_test_get_plane_scale_rotate_90_identity(struct kunit *test)
{
	struct drm_plane_state plane_state = { 0 };
	int scale_w = 0;
	int scale_h = 0;

	plane_state.rotation = DRM_MODE_ROTATE_90;
	plane_state.src_w = 1920 << 16;
	plane_state.src_h = 1080 << 16;
	plane_state.crtc_w = 1080;
	plane_state.crtc_h = 1920;

	dm_get_plane_scale(&plane_state, &scale_w, &scale_h);

	KUNIT_EXPECT_EQ(test, scale_w, 1000);
	KUNIT_EXPECT_EQ(test, scale_h, 1000);
}

/**
 * dm_test_get_plane_scale_zero_src_width - Test Get plane scale zero src width
 * @test: The KUnit test context
 */
static void dm_test_get_plane_scale_zero_src_width(struct kunit *test)
{
	struct drm_plane_state plane_state = { 0 };
	int scale_w = 0;
	int scale_h = 0;

	plane_state.rotation = DRM_MODE_ROTATE_0;
	plane_state.src_w = 0;
	plane_state.src_h = 1080 << 16;
	plane_state.crtc_w = 100;
	plane_state.crtc_h = 200;

	dm_get_plane_scale(&plane_state, &scale_w, &scale_h);

	KUNIT_EXPECT_EQ(test, scale_w, 0);
	KUNIT_EXPECT_EQ(test, scale_h, 185);
}

/* Tests for is_scaling_state_different() */

/**
 * dm_test_scaling_state_same - Test identical scaling states compare equal
 * @test: The KUnit test context
 */
static void dm_test_scaling_state_same(struct kunit *test)
{
	struct dm_connector_state *a;
	struct dm_connector_state *b;

	a = kunit_kzalloc(test, sizeof(*a), GFP_KERNEL);
	b = kunit_kzalloc(test, sizeof(*b), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, a);
	KUNIT_ASSERT_NOT_NULL(test, b);

	a->scaling = RMX_FULL;
	a->underscan_enable = false;
	*b = *a;

	KUNIT_EXPECT_FALSE(test, is_scaling_state_different(a, b));
}

/**
 * dm_test_scaling_state_scaling_changed - Test differing scaling mode is detected
 * @test: The KUnit test context
 */
static void dm_test_scaling_state_scaling_changed(struct kunit *test)
{
	struct dm_connector_state *a;
	struct dm_connector_state *b;

	a = kunit_kzalloc(test, sizeof(*a), GFP_KERNEL);
	b = kunit_kzalloc(test, sizeof(*b), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, a);
	KUNIT_ASSERT_NOT_NULL(test, b);

	a->scaling = RMX_FULL;
	b->scaling = RMX_CENTER;

	KUNIT_EXPECT_TRUE(test, is_scaling_state_different(a, b));
}

/**
 * dm_test_scaling_state_underscan_enabled - Test enabling underscan with borders differs
 * @test: The KUnit test context
 */
static void dm_test_scaling_state_underscan_enabled(struct kunit *test)
{
	struct dm_connector_state *old_state;
	struct dm_connector_state *new_state;

	old_state = kunit_kzalloc(test, sizeof(*old_state), GFP_KERNEL);
	new_state = kunit_kzalloc(test, sizeof(*new_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, old_state);
	KUNIT_ASSERT_NOT_NULL(test, new_state);

	/* new enables underscan with non-zero borders, old has it disabled */
	new_state->underscan_enable = true;
	new_state->underscan_hborder = 16;
	new_state->underscan_vborder = 16;
	old_state->underscan_enable = false;

	KUNIT_EXPECT_TRUE(test, is_scaling_state_different(new_state, old_state));
}

/**
 * dm_test_scaling_state_underscan_disabled - Test disabling underscan with borders differs
 * @test: The KUnit test context
 */
static void dm_test_scaling_state_underscan_disabled(struct kunit *test)
{
	struct dm_connector_state *old_state;
	struct dm_connector_state *new_state;

	old_state = kunit_kzalloc(test, sizeof(*old_state), GFP_KERNEL);
	new_state = kunit_kzalloc(test, sizeof(*new_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, old_state);
	KUNIT_ASSERT_NOT_NULL(test, new_state);

	old_state->underscan_enable = true;
	old_state->underscan_hborder = 16;
	old_state->underscan_vborder = 16;
	new_state->underscan_enable = false;

	KUNIT_EXPECT_TRUE(test, is_scaling_state_different(new_state, old_state));
}

/**
 * dm_test_scaling_state_underscan_border_changed - Test changed underscan borders differ
 * @test: The KUnit test context
 */
static void dm_test_scaling_state_underscan_border_changed(struct kunit *test)
{
	struct dm_connector_state *a;
	struct dm_connector_state *b;

	a = kunit_kzalloc(test, sizeof(*a), GFP_KERNEL);
	b = kunit_kzalloc(test, sizeof(*b), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, a);
	KUNIT_ASSERT_NOT_NULL(test, b);

	a->underscan_enable = true;
	a->underscan_hborder = 16;
	a->underscan_vborder = 16;
	*b = *a;
	b->underscan_hborder = 32;

	KUNIT_EXPECT_TRUE(test, is_scaling_state_different(a, b));
}

/* Tests for is_timing_unchanged_for_freesync() */

/**
 * dm_test_timing_unchanged_null_args - Test NULL crtc states return false
 * @test: The KUnit test context
 */
static void dm_test_timing_unchanged_null_args(struct kunit *test)
{
	struct drm_crtc_state crtc_state = { 0 };

	KUNIT_EXPECT_FALSE(test,
			   is_timing_unchanged_for_freesync(NULL, &crtc_state));
	KUNIT_EXPECT_FALSE(test,
			   is_timing_unchanged_for_freesync(&crtc_state, NULL));
}

/**
 * dm_test_timing_unchanged_identical_modes - Test identical modes are not "unchanged"
 * @test: The KUnit test context
 *
 * The helper only returns true when vtotal/vsync shift (vrr) while the rest
 * of the timing stays fixed, so identical modes must return false.
 */
static void dm_test_timing_unchanged_identical_modes(struct kunit *test)
{
	struct drm_crtc_state old_state = { 0 };
	struct drm_crtc_state new_state = { 0 };

	old_state.mode.clock = 148500;
	old_state.mode.hdisplay = 1920;
	old_state.mode.vdisplay = 1080;
	old_state.mode.htotal = 2200;
	old_state.mode.vtotal = 1125;
	new_state.mode = old_state.mode;

	KUNIT_EXPECT_FALSE(test,
			   is_timing_unchanged_for_freesync(&old_state, &new_state));
}

/**
 * dm_test_timing_unchanged_vrr_shift - Test vrr-style vtotal/vsync shift is detected
 * @test: The KUnit test context
 */
static void dm_test_timing_unchanged_vrr_shift(struct kunit *test)
{
	struct drm_crtc_state old_state = { 0 };
	struct drm_crtc_state new_state = { 0 };

	old_state.mode.clock = 148500;
	old_state.mode.hdisplay = 1920;
	old_state.mode.vdisplay = 1080;
	old_state.mode.htotal = 2200;
	old_state.mode.vtotal = 1125;
	old_state.mode.hsync_start = 2008;
	old_state.mode.vsync_start = 1084;
	old_state.mode.hsync_end = 2052;
	old_state.mode.vsync_end = 1089;

	/* Same horizontal timing, vertical totals/sync shifted by 125 lines */
	new_state.mode = old_state.mode;
	new_state.mode.vtotal = 1250;
	new_state.mode.vsync_start = 1209;
	new_state.mode.vsync_end = 1214;

	KUNIT_EXPECT_TRUE(test,
			  is_timing_unchanged_for_freesync(&old_state, &new_state));
}

/**
 * dm_test_timing_unchanged_clock_changed - Test pixel clock change returns false
 * @test: The KUnit test context
 */
static void dm_test_timing_unchanged_clock_changed(struct kunit *test)
{
	struct drm_crtc_state old_state = { 0 };
	struct drm_crtc_state new_state = { 0 };

	old_state.mode.clock = 148500;
	old_state.mode.htotal = 2200;
	old_state.mode.vtotal = 1125;
	old_state.mode.vsync_start = 1084;
	old_state.mode.vsync_end = 1089;

	new_state.mode = old_state.mode;
	new_state.mode.clock = 297000;
	new_state.mode.vtotal = 1250;
	new_state.mode.vsync_start = 1209;
	new_state.mode.vsync_end = 1214;

	KUNIT_EXPECT_FALSE(test,
			   is_timing_unchanged_for_freesync(&old_state, &new_state));
}

/* Tests for set_freesync_fixed_config() */

/**
 * dm_test_set_freesync_fixed_config_60hz - Test fixed refresh computed for 1080p60
 * @test: The KUnit test context
 */
static void dm_test_set_freesync_fixed_config_60hz(struct kunit *test)
{
	struct dm_crtc_state dm_crtc_state = { 0 };

	dm_crtc_state.base.mode.clock = 148500;
	dm_crtc_state.base.mode.htotal = 2200;
	dm_crtc_state.base.mode.vtotal = 1125;

	set_freesync_fixed_config(&dm_crtc_state);

	KUNIT_EXPECT_EQ(test, (int)dm_crtc_state.freesync_config.state,
			(int)VRR_STATE_ACTIVE_FIXED);
	/* 148500 kHz / (2200 * 1125) = 60 Hz = 60000000 uHz */
	KUNIT_EXPECT_EQ(test, dm_crtc_state.freesync_config.fixed_refresh_in_uhz,
			60000000U);
}

/* Tests for is_dc_timing_adjust_needed() */

/**
 * dm_test_dc_timing_adjust_pending - Test a pending hw timing adjust forces true
 * @test: The KUnit test context
 */
static void dm_test_dc_timing_adjust_pending(struct kunit *test)
{
	struct dm_crtc_state *old_state, *new_state;
	struct dc_stream_state *stream;

	old_state = kunit_kzalloc(test, sizeof(*old_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, old_state);
	new_state = kunit_kzalloc(test, sizeof(*new_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, new_state);
	stream = kunit_kzalloc(test, sizeof(*stream), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, stream);

	new_state->stream = stream;
	stream->adjust.timing_adjust_pending = 1;

	KUNIT_EXPECT_TRUE(test, is_dc_timing_adjust_needed(old_state, new_state));
}

/**
 * dm_test_dc_timing_adjust_active_fixed - Test VRR active-fixed forces true
 * @test: The KUnit test context
 */
static void dm_test_dc_timing_adjust_active_fixed(struct kunit *test)
{
	struct dm_crtc_state *old_state, *new_state;
	struct dc_stream_state *stream;

	old_state = kunit_kzalloc(test, sizeof(*old_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, old_state);
	new_state = kunit_kzalloc(test, sizeof(*new_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, new_state);
	stream = kunit_kzalloc(test, sizeof(*stream), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, stream);

	new_state->stream = stream;
	new_state->freesync_config.state = VRR_STATE_ACTIVE_FIXED;

	KUNIT_EXPECT_TRUE(test, is_dc_timing_adjust_needed(old_state, new_state));
}

/**
 * dm_test_dc_timing_adjust_vrr_toggle - Test a change in vrr active state forces true
 * @test: The KUnit test context
 */
static void dm_test_dc_timing_adjust_vrr_toggle(struct kunit *test)
{
	struct dm_crtc_state *old_state, *new_state;
	struct dc_stream_state *stream;

	old_state = kunit_kzalloc(test, sizeof(*old_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, old_state);
	new_state = kunit_kzalloc(test, sizeof(*new_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, new_state);
	stream = kunit_kzalloc(test, sizeof(*stream), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, stream);

	new_state->stream = stream;
	old_state->freesync_config.state = VRR_STATE_ACTIVE_VARIABLE;
	new_state->freesync_config.state = VRR_STATE_INACTIVE;

	KUNIT_EXPECT_TRUE(test, is_dc_timing_adjust_needed(old_state, new_state));
}

/**
 * dm_test_dc_timing_adjust_not_needed - Test steady-state timing needs no adjust
 * @test: The KUnit test context
 */
static void dm_test_dc_timing_adjust_not_needed(struct kunit *test)
{
	struct dm_crtc_state *old_state, *new_state;
	struct dc_stream_state *stream;

	old_state = kunit_kzalloc(test, sizeof(*old_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, old_state);
	new_state = kunit_kzalloc(test, sizeof(*new_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, new_state);
	stream = kunit_kzalloc(test, sizeof(*stream), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, stream);

	new_state->stream = stream;
	old_state->freesync_config.state = VRR_STATE_INACTIVE;
	new_state->freesync_config.state = VRR_STATE_INACTIVE;

	KUNIT_EXPECT_FALSE(test, is_dc_timing_adjust_needed(old_state, new_state));
}

/* Tests for set_multisync_trigger_params() */

/**
 * dm_test_multisync_trigger_disabled - Test disabled reset leaves params untouched
 * @test: The KUnit test context
 */
static void dm_test_multisync_trigger_disabled(struct kunit *test)
{
	struct dc_stream_state *stream;

	stream = kunit_kzalloc(test, sizeof(*stream), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, stream);

	stream->triggered_crtc_reset.enabled = false;
	stream->triggered_crtc_reset.event = CRTC_EVENT_VSYNC_FALLING;
	stream->triggered_crtc_reset.delay = TRIGGER_DELAY_NEXT_LINE;

	set_multisync_trigger_params(stream);

	/* Nothing should change when the reset trigger is disabled */
	KUNIT_EXPECT_EQ(test, (int)stream->triggered_crtc_reset.event,
			(int)CRTC_EVENT_VSYNC_FALLING);
	KUNIT_EXPECT_EQ(test, (int)stream->triggered_crtc_reset.delay,
			(int)TRIGGER_DELAY_NEXT_LINE);
}

/**
 * dm_test_multisync_trigger_rising - Test positive vsync polarity selects rising edge
 * @test: The KUnit test context
 */
static void dm_test_multisync_trigger_rising(struct kunit *test)
{
	struct dc_stream_state *stream;
	struct dc_stream_state *master;

	stream = kunit_kzalloc(test, sizeof(*stream), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, stream);
	master = kunit_kzalloc(test, sizeof(*master), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, master);

	master->timing.flags.VSYNC_POSITIVE_POLARITY = 1;
	stream->triggered_crtc_reset.enabled = true;
	stream->triggered_crtc_reset.event_source = master;

	set_multisync_trigger_params(stream);

	KUNIT_EXPECT_EQ(test, (int)stream->triggered_crtc_reset.event,
			(int)CRTC_EVENT_VSYNC_RISING);
	KUNIT_EXPECT_EQ(test, (int)stream->triggered_crtc_reset.delay,
			(int)TRIGGER_DELAY_NEXT_PIXEL);
}

/**
 * dm_test_multisync_trigger_falling - Test negative vsync polarity selects falling edge
 * @test: The KUnit test context
 */
static void dm_test_multisync_trigger_falling(struct kunit *test)
{
	struct dc_stream_state *stream;
	struct dc_stream_state *master;

	stream = kunit_kzalloc(test, sizeof(*stream), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, stream);
	master = kunit_kzalloc(test, sizeof(*master), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, master);

	master->timing.flags.VSYNC_POSITIVE_POLARITY = 0;
	stream->triggered_crtc_reset.enabled = true;
	stream->triggered_crtc_reset.event_source = master;

	set_multisync_trigger_params(stream);

	KUNIT_EXPECT_EQ(test, (int)stream->triggered_crtc_reset.event,
			(int)CRTC_EVENT_VSYNC_FALLING);
	KUNIT_EXPECT_EQ(test, (int)stream->triggered_crtc_reset.delay,
			(int)TRIGGER_DELAY_NEXT_PIXEL);
}

/* Tests for set_master_stream() */

/**
 * dm_test_master_stream_highest_refresh - Test highest refresh-rate stream becomes master
 * @test: The KUnit test context
 */
static void dm_test_master_stream_highest_refresh(struct kunit *test)
{
	struct dc_stream_state *stream0, *stream1;
	struct dc_stream_state *stream_set[2];

	stream0 = kunit_kzalloc(test, sizeof(*stream0), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, stream0);
	stream1 = kunit_kzalloc(test, sizeof(*stream1), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, stream1);
	stream_set[0] = stream0;
	stream_set[1] = stream1;

	/* stream0: 60Hz, stream1: 120Hz -> stream1 is master */
	stream0->triggered_crtc_reset.enabled = true;
	stream0->timing.pix_clk_100hz = 1485000;
	stream0->timing.h_total = 2200;
	stream0->timing.v_total = 1125;

	stream1->triggered_crtc_reset.enabled = true;
	stream1->timing.pix_clk_100hz = 2970000;
	stream1->timing.h_total = 2200;
	stream1->timing.v_total = 1125;

	set_master_stream(stream_set, 2);

	KUNIT_EXPECT_PTR_EQ(test, stream0->triggered_crtc_reset.event_source,
			    stream1);
	KUNIT_EXPECT_PTR_EQ(test, stream1->triggered_crtc_reset.event_source,
			    stream1);
}

/**
 * dm_test_master_stream_defaults_to_first - Test default master when none triggered
 * @test: The KUnit test context
 *
 * When no stream has the reset trigger enabled, master_stream stays 0 and all
 * streams point at the first stream as their event source.
 */
static void dm_test_master_stream_defaults_to_first(struct kunit *test)
{
	struct dc_stream_state *stream0, *stream1;
	struct dc_stream_state *stream_set[2];

	stream0 = kunit_kzalloc(test, sizeof(*stream0), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, stream0);
	stream1 = kunit_kzalloc(test, sizeof(*stream1), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, stream1);
	stream_set[0] = stream0;
	stream_set[1] = stream1;

	set_master_stream(stream_set, 2);

	KUNIT_EXPECT_PTR_EQ(test, stream0->triggered_crtc_reset.event_source,
			    stream0);
	KUNIT_EXPECT_PTR_EQ(test, stream1->triggered_crtc_reset.event_source,
			    stream0);
}

/* Tests for is_content_protection_different() */

struct dm_test_cp_ctx {
	struct amdgpu_dm_connector *aconnector;
	struct dm_connector_state *new_dm;	/* also connector->state */
	struct dm_connector_state *old_dm;
	struct drm_crtc_state *new_crtc;
	struct drm_crtc_state *old_crtc;
};

static struct dm_test_cp_ctx *dm_test_cp_ctx_alloc(struct kunit *test)
{
	struct dm_test_cp_ctx *ctx;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	ctx->aconnector = kunit_kzalloc(test, sizeof(*ctx->aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->aconnector);
	ctx->new_dm = kunit_kzalloc(test, sizeof(*ctx->new_dm), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->new_dm);
	ctx->old_dm = kunit_kzalloc(test, sizeof(*ctx->old_dm), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->old_dm);
	ctx->new_crtc = kunit_kzalloc(test, sizeof(*ctx->new_crtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->new_crtc);
	ctx->old_crtc = kunit_kzalloc(test, sizeof(*ctx->old_crtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->old_crtc);

	/* connector->state must be the new dm connector state */
	ctx->aconnector->base.state = &ctx->new_dm->base;
	ctx->aconnector->base.dpms = DRM_MODE_DPMS_ON;

	return ctx;
}

static bool dm_test_cp_diff(struct dm_test_cp_ctx *ctx)
{
	return is_content_protection_different(ctx->new_crtc, ctx->old_crtc,
					       &ctx->new_dm->base,
					       &ctx->old_dm->base,
					       &ctx->aconnector->base, NULL);
}

/**
 * dm_test_cp_diff_hdcp_type_change - Test an HDCP content-type change forces true
 * @test: The KUnit test context
 */
static void dm_test_cp_diff_hdcp_type_change(struct kunit *test)
{
	struct dm_test_cp_ctx *ctx = dm_test_cp_ctx_alloc(test);

	ctx->old_dm->base.hdcp_content_type = 0;
	ctx->new_dm->base.hdcp_content_type = 1;
	ctx->new_dm->base.content_protection = DRM_MODE_CONTENT_PROTECTION_ENABLED;

	KUNIT_EXPECT_TRUE(test, dm_test_cp_diff(ctx));
	KUNIT_EXPECT_EQ(test, (int)ctx->new_dm->base.content_protection,
			(int)DRM_MODE_CONTENT_PROTECTION_DESIRED);
}

/**
 * dm_test_cp_diff_reenable_mode_changed - Test ENABLED->DESIRED with modeset forces true
 * @test: The KUnit test context
 */
static void dm_test_cp_diff_reenable_mode_changed(struct kunit *test)
{
	struct dm_test_cp_ctx *ctx = dm_test_cp_ctx_alloc(test);

	ctx->old_dm->base.content_protection = DRM_MODE_CONTENT_PROTECTION_ENABLED;
	ctx->new_dm->base.content_protection = DRM_MODE_CONTENT_PROTECTION_DESIRED;
	ctx->new_crtc->mode_changed = true;

	KUNIT_EXPECT_TRUE(test, dm_test_cp_diff(ctx));
	KUNIT_EXPECT_EQ(test, (int)ctx->new_dm->base.content_protection,
			(int)DRM_MODE_CONTENT_PROTECTION_DESIRED);
}

/**
 * dm_test_cp_diff_reenable_no_change - Test ENABLED->DESIRED without modeset restores ENABLED
 * @test: The KUnit test context
 */
static void dm_test_cp_diff_reenable_no_change(struct kunit *test)
{
	struct dm_test_cp_ctx *ctx = dm_test_cp_ctx_alloc(test);

	ctx->old_dm->base.content_protection = DRM_MODE_CONTENT_PROTECTION_ENABLED;
	ctx->new_dm->base.content_protection = DRM_MODE_CONTENT_PROTECTION_DESIRED;
	ctx->new_crtc->mode_changed = false;

	KUNIT_EXPECT_FALSE(test, dm_test_cp_diff(ctx));
	KUNIT_EXPECT_EQ(test, (int)ctx->new_dm->base.content_protection,
			(int)DRM_MODE_CONTENT_PROTECTION_ENABLED);
}

/**
 * dm_test_cp_diff_undesired - Test UNDESIRED->UNDESIRED needs no update
 * @test: The KUnit test context
 */
static void dm_test_cp_diff_undesired(struct kunit *test)
{
	struct dm_test_cp_ctx *ctx = dm_test_cp_ctx_alloc(test);

	ctx->old_dm->base.content_protection = DRM_MODE_CONTENT_PROTECTION_UNDESIRED;
	ctx->new_dm->base.content_protection = DRM_MODE_CONTENT_PROTECTION_UNDESIRED;

	KUNIT_EXPECT_FALSE(test, dm_test_cp_diff(ctx));
}

/**
 * dm_test_cp_diff_desired_mode_changed - Test DESIRED->DESIRED with modeset forces true
 * @test: The KUnit test context
 */
static void dm_test_cp_diff_desired_mode_changed(struct kunit *test)
{
	struct dm_test_cp_ctx *ctx = dm_test_cp_ctx_alloc(test);

	ctx->old_dm->base.content_protection = DRM_MODE_CONTENT_PROTECTION_DESIRED;
	ctx->new_dm->base.content_protection = DRM_MODE_CONTENT_PROTECTION_DESIRED;
	ctx->new_crtc->mode_changed = true;

	KUNIT_EXPECT_TRUE(test, dm_test_cp_diff(ctx));
}

/**
 * dm_test_cp_diff_desired_no_change - Test steady DESIRED->DESIRED needs no update
 * @test: The KUnit test context
 */
static void dm_test_cp_diff_desired_no_change(struct kunit *test)
{
	struct dm_test_cp_ctx *ctx = dm_test_cp_ctx_alloc(test);

	ctx->old_dm->base.content_protection = DRM_MODE_CONTENT_PROTECTION_DESIRED;
	ctx->new_dm->base.content_protection = DRM_MODE_CONTENT_PROTECTION_DESIRED;
	ctx->new_crtc->mode_changed = false;

	KUNIT_EXPECT_FALSE(test, dm_test_cp_diff(ctx));
}

/**
 * dm_test_cp_diff_update_hdcp_hotplug - Test the update_hdcp hot-plug path forces true
 * @test: The KUnit test context
 */
static void dm_test_cp_diff_update_hdcp_hotplug(struct kunit *test)
{
	struct dm_test_cp_ctx *ctx = dm_test_cp_ctx_alloc(test);
	struct dc_sink *sink = kunit_kzalloc(test, sizeof(*sink), GFP_KERNEL);

	KUNIT_ASSERT_NOT_NULL(test, sink);

	ctx->old_dm->base.content_protection = DRM_MODE_CONTENT_PROTECTION_DESIRED;
	ctx->new_dm->base.content_protection = DRM_MODE_CONTENT_PROTECTION_DESIRED;
	ctx->new_dm->update_hdcp = true;
	ctx->aconnector->base.dpms = DRM_MODE_DPMS_ON;
	ctx->aconnector->dc_sink = sink;

	KUNIT_EXPECT_TRUE(test, dm_test_cp_diff(ctx));
	KUNIT_EXPECT_FALSE(test, ctx->new_dm->update_hdcp);
}

/**
 * dm_test_cp_diff_stream_reenabled - Test the stream removed-and-re-enabled path forces true
 * @test: The KUnit test context
 */
static void dm_test_cp_diff_stream_reenabled(struct kunit *test)
{
	struct dm_test_cp_ctx *ctx = dm_test_cp_ctx_alloc(test);
	struct drm_crtc *crtc = kunit_kzalloc(test, sizeof(*crtc), GFP_KERNEL);

	KUNIT_ASSERT_NOT_NULL(test, crtc);
	crtc->enabled = true;

	ctx->old_dm->base.content_protection = DRM_MODE_CONTENT_PROTECTION_DESIRED;
	ctx->new_dm->base.content_protection = DRM_MODE_CONTENT_PROTECTION_DESIRED;
	ctx->new_dm->update_hdcp = true;
	ctx->old_dm->base.crtc = NULL;
	ctx->new_dm->base.crtc = crtc;

	KUNIT_EXPECT_TRUE(test, dm_test_cp_diff(ctx));
	KUNIT_EXPECT_FALSE(test, ctx->new_dm->update_hdcp);
}

/**
 * dm_test_cp_diff_s3_undesired_to_enabled - Test the S3 UNDESIRED->ENABLED path forces true
 * @test: The KUnit test context
 */
static void dm_test_cp_diff_s3_undesired_to_enabled(struct kunit *test)
{
	struct dm_test_cp_ctx *ctx = dm_test_cp_ctx_alloc(test);

	ctx->old_dm->base.content_protection = DRM_MODE_CONTENT_PROTECTION_UNDESIRED;
	ctx->new_dm->base.content_protection = DRM_MODE_CONTENT_PROTECTION_ENABLED;

	KUNIT_EXPECT_TRUE(test, dm_test_cp_diff(ctx));
	KUNIT_EXPECT_EQ(test, (int)ctx->new_dm->base.content_protection,
			(int)DRM_MODE_CONTENT_PROTECTION_DESIRED);
}

/**
 * dm_test_cp_diff_desired_to_enabled - Test DESIRED->ENABLED needs no update
 * @test: The KUnit test context
 */
static void dm_test_cp_diff_desired_to_enabled(struct kunit *test)
{
	struct dm_test_cp_ctx *ctx = dm_test_cp_ctx_alloc(test);

	ctx->old_dm->base.content_protection = DRM_MODE_CONTENT_PROTECTION_DESIRED;
	ctx->new_dm->base.content_protection = DRM_MODE_CONTENT_PROTECTION_ENABLED;

	KUNIT_EXPECT_FALSE(test, dm_test_cp_diff(ctx));
}

/**
 * dm_test_cp_diff_desired_to_undesired - Test DESIRED->UNDESIRED forces update
 * @test: The KUnit test context
 */
static void dm_test_cp_diff_desired_to_undesired(struct kunit *test)
{
	struct dm_test_cp_ctx *ctx = dm_test_cp_ctx_alloc(test);

	ctx->old_dm->base.content_protection = DRM_MODE_CONTENT_PROTECTION_DESIRED;
	ctx->new_dm->base.content_protection = DRM_MODE_CONTENT_PROTECTION_UNDESIRED;

	KUNIT_EXPECT_TRUE(test, dm_test_cp_diff(ctx));
}

/* Tests for get_freesync_config_for_crtc() */

struct dm_test_freesync_ctx {
	struct amdgpu_dm_connector *aconnector;
	struct dm_crtc_state *crtc_state;
	struct dm_connector_state *conn_state;
	struct dc_stream_state *stream;
};

static struct dm_test_freesync_ctx *dm_test_freesync_ctx_alloc(struct kunit *test)
{
	struct dm_test_freesync_ctx *ctx;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	ctx->aconnector = kunit_kzalloc(test, sizeof(*ctx->aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->aconnector);
	ctx->crtc_state = kunit_kzalloc(test, sizeof(*ctx->crtc_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->crtc_state);
	ctx->conn_state = kunit_kzalloc(test, sizeof(*ctx->conn_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->conn_state);
	ctx->stream = dm_kunit_alloc_stream(test, NULL);

	ctx->conn_state->base.connector = &ctx->aconnector->base;
	ctx->aconnector->base.connector_type = DRM_MODE_CONNECTOR_DisplayPort;
	ctx->crtc_state->stream = ctx->stream;

	/* 1080p60 timing so drm_mode_vrefresh() == 60 */
	ctx->crtc_state->base.mode.clock = 148500;
	ctx->crtc_state->base.mode.htotal = 2200;
	ctx->crtc_state->base.mode.vtotal = 1125;

	return ctx;
}

/**
 * dm_test_freesync_config_writeback - Test writeback connector is left untouched
 * @test: The KUnit test context
 */
static void dm_test_freesync_config_writeback(struct kunit *test)
{
	struct dm_test_freesync_ctx *ctx = dm_test_freesync_ctx_alloc(test);

	ctx->aconnector->base.connector_type = DRM_MODE_CONNECTOR_WRITEBACK;
	ctx->conn_state->freesync_capable = true;
	ctx->aconnector->min_vfreq = 48;
	ctx->aconnector->max_vfreq = 120;
	ctx->crtc_state->vrr_supported = true;	/* sentinel: must stay set */

	get_freesync_config_for_crtc(ctx->crtc_state, ctx->conn_state);

	/* Writeback: early return leaves vrr_supported sentinel untouched */
	KUNIT_EXPECT_TRUE(test, ctx->crtc_state->vrr_supported);
}

/**
 * dm_test_freesync_config_not_capable - Test a non-freesync sink reports UNSUPPORTED
 * @test: The KUnit test context
 */
static void dm_test_freesync_config_not_capable(struct kunit *test)
{
	struct dm_test_freesync_ctx *ctx = dm_test_freesync_ctx_alloc(test);

	ctx->conn_state->freesync_capable = false;
	ctx->aconnector->min_vfreq = 48;
	ctx->aconnector->max_vfreq = 120;

	get_freesync_config_for_crtc(ctx->crtc_state, ctx->conn_state);

	KUNIT_EXPECT_FALSE(test, ctx->crtc_state->vrr_supported);
	KUNIT_EXPECT_EQ(test, (int)ctx->crtc_state->freesync_config.state,
			(int)VRR_STATE_UNSUPPORTED);
}

/**
 * dm_test_freesync_config_out_of_range - Test a refresh outside the range is UNSUPPORTED
 * @test: The KUnit test context
 */
static void dm_test_freesync_config_out_of_range(struct kunit *test)
{
	struct dm_test_freesync_ctx *ctx = dm_test_freesync_ctx_alloc(test);

	ctx->conn_state->freesync_capable = true;
	ctx->aconnector->min_vfreq = 90;	/* 60 < 90 -> out of range */
	ctx->aconnector->max_vfreq = 120;

	get_freesync_config_for_crtc(ctx->crtc_state, ctx->conn_state);

	KUNIT_EXPECT_FALSE(test, ctx->crtc_state->vrr_supported);
	KUNIT_EXPECT_EQ(test, (int)ctx->crtc_state->freesync_config.state,
			(int)VRR_STATE_UNSUPPORTED);
}

/**
 * dm_test_freesync_config_active_variable - Test vrr_enabled yields ACTIVE_VARIABLE
 * @test: The KUnit test context
 */
static void dm_test_freesync_config_active_variable(struct kunit *test)
{
	struct dm_test_freesync_ctx *ctx = dm_test_freesync_ctx_alloc(test);

	ctx->conn_state->freesync_capable = true;
	ctx->aconnector->min_vfreq = 48;
	ctx->aconnector->max_vfreq = 120;
	ctx->crtc_state->base.vrr_enabled = true;

	get_freesync_config_for_crtc(ctx->crtc_state, ctx->conn_state);

	KUNIT_EXPECT_TRUE(test, ctx->crtc_state->vrr_supported);
	KUNIT_EXPECT_TRUE(test, ctx->stream->ignore_msa_timing_param);
	KUNIT_EXPECT_EQ(test, (int)ctx->crtc_state->freesync_config.state,
			(int)VRR_STATE_ACTIVE_VARIABLE);
	KUNIT_EXPECT_EQ(test, ctx->crtc_state->freesync_config.min_refresh_in_uhz,
			48000000U);
	KUNIT_EXPECT_EQ(test, ctx->crtc_state->freesync_config.max_refresh_in_uhz,
			120000000U);
	KUNIT_EXPECT_TRUE(test, ctx->crtc_state->freesync_config.vsif_supported);
	KUNIT_EXPECT_TRUE(test, ctx->crtc_state->freesync_config.btr);
}

/**
 * dm_test_freesync_config_inactive - Test supported-but-off yields INACTIVE
 * @test: The KUnit test context
 */
static void dm_test_freesync_config_inactive(struct kunit *test)
{
	struct dm_test_freesync_ctx *ctx = dm_test_freesync_ctx_alloc(test);

	ctx->conn_state->freesync_capable = true;
	ctx->aconnector->min_vfreq = 48;
	ctx->aconnector->max_vfreq = 120;
	ctx->crtc_state->base.vrr_enabled = false;

	get_freesync_config_for_crtc(ctx->crtc_state, ctx->conn_state);

	KUNIT_EXPECT_TRUE(test, ctx->crtc_state->vrr_supported);
	KUNIT_EXPECT_EQ(test, (int)ctx->crtc_state->freesync_config.state,
			(int)VRR_STATE_INACTIVE);
}

/**
 * dm_test_freesync_config_active_fixed - Test freesync-video mode yields ACTIVE_FIXED
 * @test: The KUnit test context
 */
static void dm_test_freesync_config_active_fixed(struct kunit *test)
{
	struct dm_test_freesync_ctx *ctx = dm_test_freesync_ctx_alloc(test);

	ctx->conn_state->freesync_capable = true;
	ctx->aconnector->min_vfreq = 48;
	ctx->aconnector->max_vfreq = 120;
	/* Pre-set fixed state selects the freesync-video (fixed) path */
	ctx->crtc_state->freesync_config.state = VRR_STATE_ACTIVE_FIXED;
	ctx->crtc_state->freesync_config.fixed_refresh_in_uhz = 60000000;
	ctx->crtc_state->base.vrr_enabled = true;	/* ignored on the fixed path */

	get_freesync_config_for_crtc(ctx->crtc_state, ctx->conn_state);

	KUNIT_EXPECT_TRUE(test, ctx->crtc_state->vrr_supported);
	KUNIT_EXPECT_EQ(test, (int)ctx->crtc_state->freesync_config.state,
			(int)VRR_STATE_ACTIVE_FIXED);
	KUNIT_EXPECT_EQ(test, ctx->crtc_state->freesync_config.fixed_refresh_in_uhz,
			60000000U);
}

/* Tests for reset_freesync_config_for_crtc() */

/**
 * dm_test_reset_freesync_config - Test reset clears vrr support and info packet
 * @test: The KUnit test context
 */
static void dm_test_reset_freesync_config(struct kunit *test)
{
	struct dm_crtc_state *crtc_state;

	crtc_state = kunit_kzalloc(test, sizeof(*crtc_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, crtc_state);

	crtc_state->vrr_supported = true;
	crtc_state->vrr_infopacket.valid = true;

	reset_freesync_config_for_crtc(crtc_state);

	KUNIT_EXPECT_FALSE(test, crtc_state->vrr_supported);
	KUNIT_EXPECT_FALSE(test, crtc_state->vrr_infopacket.valid);
}

/* Tests for dm_enable_per_frame_crtc_master_sync() */

/**
 * dm_test_per_frame_master_sync_single_stream - Test fewer than two streams is a no-op
 * @test: The KUnit test context
 */
static void dm_test_per_frame_master_sync_single_stream(struct kunit *test)
{
	struct dc_state *context;
	struct dc_stream_state *stream;

	context = kunit_kzalloc(test, sizeof(*context), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, context);
	stream = kunit_kzalloc(test, sizeof(*stream), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, stream);

	stream->triggered_crtc_reset.enabled = true;
	context->streams[0] = stream;
	context->stream_count = 1;

	dm_enable_per_frame_crtc_master_sync(context);

	/* < 2 streams: early return, event_source stays NULL */
	KUNIT_EXPECT_NULL(test, stream->triggered_crtc_reset.event_source);
}

/**
 * dm_test_per_frame_master_sync_two_streams - Test the master is picked and applied
 * @test: The KUnit test context
 */
static void dm_test_per_frame_master_sync_two_streams(struct kunit *test)
{
	struct dc_state *context;
	struct dc_stream_state *stream0, *stream1;

	context = kunit_kzalloc(test, sizeof(*context), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, context);
	stream0 = kunit_kzalloc(test, sizeof(*stream0), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, stream0);
	stream1 = kunit_kzalloc(test, sizeof(*stream1), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, stream1);

	/* stream0 60Hz, stream1 120Hz, both trigger-reset enabled */
	stream0->triggered_crtc_reset.enabled = true;
	stream0->timing.pix_clk_100hz = 1485000;
	stream0->timing.h_total = 2200;
	stream0->timing.v_total = 1125;
	stream1->triggered_crtc_reset.enabled = true;
	stream1->timing.pix_clk_100hz = 2970000;
	stream1->timing.h_total = 2200;
	stream1->timing.v_total = 1125;
	stream1->timing.flags.VSYNC_POSITIVE_POLARITY = 1;

	context->streams[0] = stream0;
	context->streams[1] = stream1;
	context->stream_count = 2;

	dm_enable_per_frame_crtc_master_sync(context);

	/* set_master_stream picks the highest refresh (stream1) as event source */
	KUNIT_EXPECT_PTR_EQ(test, stream0->triggered_crtc_reset.event_source,
			    stream1);
	KUNIT_EXPECT_PTR_EQ(test, stream1->triggered_crtc_reset.event_source,
			    stream1);
	/* set_multisync_trigger_params applied to enabled streams */
	KUNIT_EXPECT_EQ(test, (int)stream0->triggered_crtc_reset.event,
			(int)CRTC_EVENT_VSYNC_RISING);
	KUNIT_EXPECT_EQ(test, (int)stream0->triggered_crtc_reset.delay,
			(int)TRIGGER_DELAY_NEXT_PIXEL);
}

/**
 * dm_test_per_frame_master_sync_skips_null_stream - Test NULL stream entries are skipped
 * @test: The KUnit test context
 */
static void dm_test_per_frame_master_sync_skips_null_stream(struct kunit *test)
{
	struct dc_state *context;
	struct dc_stream_state *stream;

	context = kunit_kzalloc(test, sizeof(*context), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, context);
	stream = kunit_kzalloc(test, sizeof(*stream), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, stream);

	stream->triggered_crtc_reset.enabled = true;
	stream->timing.pix_clk_100hz = 1485000;
	stream->timing.h_total = 2200;
	stream->timing.v_total = 1125;
	context->streams[0] = stream;
	context->streams[1] = NULL;
	context->stream_count = 2;

	dm_enable_per_frame_crtc_master_sync(context);

	KUNIT_EXPECT_PTR_EQ(test, stream->triggered_crtc_reset.event_source,
			    stream);
}

/* Tests for dm_should_disable_stutter() */

/**
 * dm_test_should_disable_stutter_match - Test the quirk device matches
 * @test: The KUnit test context
 */
static void dm_test_should_disable_stutter_match(struct kunit *test)
{
	struct pci_dev *pdev;

	pdev = kunit_kzalloc(test, sizeof(*pdev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, pdev);

	pdev->vendor = 0x1002;
	pdev->device = 0x15dd;
	pdev->subsystem_vendor = 0x1002;
	pdev->subsystem_device = 0x15dd;
	pdev->revision = 0xc8;

	KUNIT_EXPECT_TRUE(test, dm_should_disable_stutter(pdev));
}

/**
 * dm_test_should_disable_stutter_no_match - Test a non-quirk device does not match
 * @test: The KUnit test context
 */
static void dm_test_should_disable_stutter_no_match(struct kunit *test)
{
	struct pci_dev *pdev;

	pdev = kunit_kzalloc(test, sizeof(*pdev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, pdev);

	pdev->vendor = 0x1002;
	pdev->device = 0x1234;

	KUNIT_EXPECT_FALSE(test, dm_should_disable_stutter(pdev));
}

/**
 * dm_test_should_disable_stutter_revision_differs - Test a partial match (revision) fails
 * @test: The KUnit test context
 */
static void dm_test_should_disable_stutter_revision_differs(struct kunit *test)
{
	struct pci_dev *pdev;

	pdev = kunit_kzalloc(test, sizeof(*pdev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, pdev);

	/* Everything matches the quirk except the revision */
	pdev->vendor = 0x1002;
	pdev->device = 0x15dd;
	pdev->subsystem_vendor = 0x1002;
	pdev->subsystem_device = 0x15dd;
	pdev->revision = 0x00;

	KUNIT_EXPECT_FALSE(test, dm_should_disable_stutter(pdev));
}

/* Tests for amdgpu_dm_apply_delay_after_dpcd_poweroff() */

/**
 * dm_test_apply_delay_null_sink - Test a NULL sink returns without delay
 * @test: The KUnit test context
 */
static void dm_test_apply_delay_null_sink(struct kunit *test)
{
	/* NULL sink: early return, no delay, no dereference */
	amdgpu_dm_apply_delay_after_dpcd_poweroff(NULL, NULL);
}

/**
 * dm_test_apply_delay_zero_wait - Test a zero wait interval skips the delay
 * @test: The KUnit test context
 */
static void dm_test_apply_delay_zero_wait(struct kunit *test)
{
	struct dc_sink *sink;

	sink = kunit_kzalloc(test, sizeof(*sink), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, sink);

	/* wait == 0: no msleep, adev is unused so NULL is safe */
	sink->edid_caps.panel_patch.wait_after_dpcd_poweroff_ms = 0;
	amdgpu_dm_apply_delay_after_dpcd_poweroff(NULL, sink);
}

/**
 * dm_test_apply_delay_nonzero_wait - Test a non-zero wait interval executes delay path
 * @test: The KUnit test context
 */
static void dm_test_apply_delay_nonzero_wait(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct dc_sink *sink;

	sink = kunit_kzalloc(test, sizeof(*sink), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, sink);

	sink->edid_caps.panel_patch.wait_after_dpcd_poweroff_ms = 1;
	amdgpu_dm_apply_delay_after_dpcd_poweroff(adev, sink);
}

static struct kunit_case amdgpu_dm_tests[] = {
	/* Simple DM callbacks */
	KUNIT_CASE(dm_test_is_idle),
	KUNIT_CASE(dm_test_wait_for_idle),
	KUNIT_CASE(dm_test_soft_reset),
	KUNIT_CASE(dm_test_set_clockgating_state),
	KUNIT_CASE(dm_test_set_powergating_state),
	KUNIT_CASE(dm_test_bandwidth_update),
	KUNIT_CASE(dm_test_crtc_complete_writeback_no_connector),
	KUNIT_CASE(dm_test_crtc_complete_writeback_not_pending),
	KUNIT_CASE(dm_test_vblank_get_counter_out_of_range),
	KUNIT_CASE(dm_test_vblank_get_counter_no_stream),
	KUNIT_CASE(dm_test_crtc_get_scanoutpos_invalid_crtc),
	KUNIT_CASE(dm_test_crtc_get_scanoutpos_no_stream),
	KUNIT_CASE(dm_test_atomic_get_new_state_empty),
	KUNIT_CASE(dm_test_atomic_get_new_state_match),
	KUNIT_CASE(dm_test_should_update_native_cursor_without_crtc),
	KUNIT_CASE(dm_test_should_update_native_cursor_disable_native),
	KUNIT_CASE(dm_test_should_update_native_cursor_enable_overlay),
	KUNIT_CASE(dm_test_atomic_destroy_state_no_context),
	KUNIT_CASE(dm_test_smu_write_watermarks_table_default),
	/* dm_plane_layer_index_cmp */
	KUNIT_CASE(dm_test_plane_layer_index_cmp_equal),
	KUNIT_CASE(dm_test_plane_layer_index_cmp_descending),
	KUNIT_CASE(dm_test_plane_layer_index_cmp_ascending),
	/* fill_plane_color_attributes */
	KUNIT_CASE(dm_test_fill_color_attr_rgb_format),
	KUNIT_CASE(dm_test_fill_color_attr_bt601_full),
	KUNIT_CASE(dm_test_fill_color_attr_bt601_limited),
	KUNIT_CASE(dm_test_fill_color_attr_bt709_full),
	KUNIT_CASE(dm_test_fill_color_attr_bt709_limited),
	KUNIT_CASE(dm_test_fill_color_attr_bt2020_full),
	KUNIT_CASE(dm_test_fill_color_attr_bt2020_limited),
	KUNIT_CASE(dm_test_fill_color_attr_invalid_encoding),
	/* modereset_required */
	KUNIT_CASE(dm_test_modereset_required_when_inactive_and_modeset),
	KUNIT_CASE(dm_test_modereset_not_required_when_active_and_modeset),
	KUNIT_CASE(dm_test_modereset_not_required_when_inactive_without_modeset),
	/* dm_get_oriented_plane_size */
	KUNIT_CASE(dm_test_oriented_plane_size_rotate_0),
	KUNIT_CASE(dm_test_oriented_plane_size_rotate_90),
	KUNIT_CASE(dm_test_oriented_plane_size_rotate_180),
	KUNIT_CASE(dm_test_oriented_plane_size_rotate_270),
	/* dm_get_plane_scale */
	KUNIT_CASE(dm_test_get_plane_scale_identity),
	KUNIT_CASE(dm_test_get_plane_scale_rotate_90_identity),
	KUNIT_CASE(dm_test_get_plane_scale_zero_src_width),
	/* is_scaling_state_different */
	KUNIT_CASE(dm_test_scaling_state_same),
	KUNIT_CASE(dm_test_scaling_state_scaling_changed),
	KUNIT_CASE(dm_test_scaling_state_underscan_enabled),
	KUNIT_CASE(dm_test_scaling_state_underscan_disabled),
	KUNIT_CASE(dm_test_scaling_state_underscan_border_changed),
	/* is_timing_unchanged_for_freesync */
	KUNIT_CASE(dm_test_timing_unchanged_null_args),
	KUNIT_CASE(dm_test_timing_unchanged_identical_modes),
	KUNIT_CASE(dm_test_timing_unchanged_vrr_shift),
	KUNIT_CASE(dm_test_timing_unchanged_clock_changed),
	/* set_freesync_fixed_config */
	KUNIT_CASE(dm_test_set_freesync_fixed_config_60hz),
	/* is_dc_timing_adjust_needed */
	KUNIT_CASE(dm_test_dc_timing_adjust_pending),
	KUNIT_CASE(dm_test_dc_timing_adjust_active_fixed),
	KUNIT_CASE(dm_test_dc_timing_adjust_vrr_toggle),
	KUNIT_CASE(dm_test_dc_timing_adjust_not_needed),
	/* set_multisync_trigger_params */
	KUNIT_CASE(dm_test_multisync_trigger_disabled),
	KUNIT_CASE(dm_test_multisync_trigger_rising),
	KUNIT_CASE(dm_test_multisync_trigger_falling),
	/* set_master_stream */
	KUNIT_CASE(dm_test_master_stream_highest_refresh),
	KUNIT_CASE(dm_test_master_stream_defaults_to_first),
	/* is_content_protection_different */
	KUNIT_CASE(dm_test_cp_diff_hdcp_type_change),
	KUNIT_CASE(dm_test_cp_diff_reenable_mode_changed),
	KUNIT_CASE(dm_test_cp_diff_reenable_no_change),
	KUNIT_CASE(dm_test_cp_diff_undesired),
	KUNIT_CASE(dm_test_cp_diff_desired_mode_changed),
	KUNIT_CASE(dm_test_cp_diff_desired_no_change),
	KUNIT_CASE(dm_test_cp_diff_update_hdcp_hotplug),
	KUNIT_CASE(dm_test_cp_diff_stream_reenabled),
	KUNIT_CASE(dm_test_cp_diff_s3_undesired_to_enabled),
	KUNIT_CASE(dm_test_cp_diff_desired_to_enabled),
	KUNIT_CASE(dm_test_cp_diff_desired_to_undesired),
	/* get_freesync_config_for_crtc */
	KUNIT_CASE(dm_test_freesync_config_writeback),
	KUNIT_CASE(dm_test_freesync_config_not_capable),
	KUNIT_CASE(dm_test_freesync_config_out_of_range),
	KUNIT_CASE(dm_test_freesync_config_active_variable),
	KUNIT_CASE(dm_test_freesync_config_inactive),
	KUNIT_CASE(dm_test_freesync_config_active_fixed),
	/* reset_freesync_config_for_crtc */
	KUNIT_CASE(dm_test_reset_freesync_config),
	/* dm_enable_per_frame_crtc_master_sync */
	KUNIT_CASE(dm_test_per_frame_master_sync_single_stream),
	KUNIT_CASE(dm_test_per_frame_master_sync_two_streams),
	KUNIT_CASE(dm_test_per_frame_master_sync_skips_null_stream),
	/* dm_should_disable_stutter */
	KUNIT_CASE(dm_test_should_disable_stutter_match),
	KUNIT_CASE(dm_test_should_disable_stutter_no_match),
	KUNIT_CASE(dm_test_should_disable_stutter_revision_differs),
	/* amdgpu_dm_apply_delay_after_dpcd_poweroff */
	KUNIT_CASE(dm_test_apply_delay_null_sink),
	KUNIT_CASE(dm_test_apply_delay_zero_wait),
	KUNIT_CASE(dm_test_apply_delay_nonzero_wait),
	{}
};

static struct kunit_suite amdgpu_dm_test_suite = {
	.name = "amdgpu_dm",
	.test_cases = amdgpu_dm_tests,
};

kunit_test_suite(amdgpu_dm_test_suite);

MODULE_AUTHOR("AMD");
MODULE_DESCRIPTION("KUnit tests for amdgpu_dm");
MODULE_LICENSE("Dual MIT/GPL");
