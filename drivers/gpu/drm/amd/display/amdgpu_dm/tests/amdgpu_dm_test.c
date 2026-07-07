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

/* Tests for amdgpu_dm_update_stream_scaling_settings() */

/**
 * dm_test_update_scaling_null_mode - Test NULL mode leaves the stream rects untouched
 * @test: The KUnit test context
 */
static void dm_test_update_scaling_null_mode(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct dc_stream_state *stream = dm_kunit_alloc_stream(test, NULL);

	stream->timing.h_addressable = 1920;
	stream->timing.v_addressable = 1080;

	amdgpu_dm_update_stream_scaling_settings(&adev->ddev, NULL, NULL, stream);

	/* NULL mode: early return before touching src/dst */
	KUNIT_EXPECT_EQ(test, stream->src.width, 0);
	KUNIT_EXPECT_EQ(test, stream->dst.width, 0);
}

/**
 * dm_test_update_scaling_fullscreen_default - Test full-screen default with no dm_state
 * @test: The KUnit test context
 */
static void dm_test_update_scaling_fullscreen_default(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct dc_stream_state *stream = dm_kunit_alloc_stream(test, NULL);
	struct drm_display_mode mode = { 0 };

	mode.hdisplay = 1920;
	mode.vdisplay = 1080;
	stream->timing.h_addressable = 2560;
	stream->timing.v_addressable = 1440;

	amdgpu_dm_update_stream_scaling_settings(&adev->ddev, &mode, NULL, stream);

	/* src = mode, dst = timing addressable, no centering without dm_state */
	KUNIT_EXPECT_EQ(test, stream->src.width, 1920);
	KUNIT_EXPECT_EQ(test, stream->src.height, 1080);
	KUNIT_EXPECT_EQ(test, stream->dst.width, 2560);
	KUNIT_EXPECT_EQ(test, stream->dst.height, 1440);
	KUNIT_EXPECT_EQ(test, stream->dst.x, 0);
	KUNIT_EXPECT_EQ(test, stream->dst.y, 0);
}

/**
 * dm_test_update_scaling_rmx_full - Test RMX_FULL keeps a full-size, centered dst
 * @test: The KUnit test context
 */
static void dm_test_update_scaling_rmx_full(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct dc_stream_state *stream = dm_kunit_alloc_stream(test, NULL);
	struct dm_connector_state *dm_state;
	struct drm_display_mode mode = { 0 };

	dm_state = kunit_kzalloc(test, sizeof(*dm_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dm_state);

	mode.hdisplay = 1280;
	mode.vdisplay = 720;
	stream->timing.h_addressable = 1920;
	stream->timing.v_addressable = 1080;
	dm_state->scaling = RMX_FULL;

	amdgpu_dm_update_stream_scaling_settings(&adev->ddev, &mode, dm_state, stream);

	/* RMX_FULL: dst stays full addressable, offset 0 */
	KUNIT_EXPECT_EQ(test, stream->dst.width, 1920);
	KUNIT_EXPECT_EQ(test, stream->dst.height, 1080);
	KUNIT_EXPECT_EQ(test, stream->dst.x, 0);
	KUNIT_EXPECT_EQ(test, stream->dst.y, 0);
}

/**
 * dm_test_update_scaling_rmx_aspect_pillarbox - Test RMX_ASPECT preserves aspect ratio
 * @test: The KUnit test context
 */
static void dm_test_update_scaling_rmx_aspect_pillarbox(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct dc_stream_state *stream = dm_kunit_alloc_stream(test, NULL);
	struct dm_connector_state *dm_state;
	struct drm_display_mode mode = { 0 };

	dm_state = kunit_kzalloc(test, sizeof(*dm_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dm_state);

	/* 4:3 source on a 16:9 panel -> pillarboxed */
	mode.hdisplay = 1024;
	mode.vdisplay = 768;
	stream->timing.h_addressable = 1920;
	stream->timing.v_addressable = 1080;
	dm_state->scaling = RMX_ASPECT;

	amdgpu_dm_update_stream_scaling_settings(&adev->ddev, &mode, dm_state, stream);

	/*
	 * src.width*dst.height (1024*1080) < src.height*dst.width (768*1920):
	 * width scaled to src.width*dst.height/src.height = 1440, height stays
	 * 1080, centered horizontally at (1920-1440)/2 = 240.
	 */
	KUNIT_EXPECT_EQ(test, stream->dst.width, 1440);
	KUNIT_EXPECT_EQ(test, stream->dst.height, 1080);
	KUNIT_EXPECT_EQ(test, stream->dst.x, 240);
	KUNIT_EXPECT_EQ(test, stream->dst.y, 0);
}

/**
 * dm_test_update_scaling_rmx_aspect_letterbox - Test RMX_ASPECT letterboxes wide sources
 * @test: The KUnit test context
 */
static void dm_test_update_scaling_rmx_aspect_letterbox(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct dc_stream_state *stream = dm_kunit_alloc_stream(test, NULL);
	struct dm_connector_state *dm_state;
	struct drm_display_mode mode = { 0 };

	dm_state = kunit_kzalloc(test, sizeof(*dm_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dm_state);

	/* 16:9 source on a 4:3 panel -> letterboxed */
	mode.hdisplay = 1920;
	mode.vdisplay = 1080;
	stream->timing.h_addressable = 1024;
	stream->timing.v_addressable = 768;
	dm_state->scaling = RMX_ASPECT;

	amdgpu_dm_update_stream_scaling_settings(&adev->ddev, &mode, dm_state, stream);

	KUNIT_EXPECT_EQ(test, stream->dst.width, 1024);
	KUNIT_EXPECT_EQ(test, stream->dst.height, 576);
	KUNIT_EXPECT_EQ(test, stream->dst.x, 0);
	KUNIT_EXPECT_EQ(test, stream->dst.y, 96);
}

/**
 * dm_test_update_scaling_rmx_center - Test RMX_CENTER centers a 1:1 dst
 * @test: The KUnit test context
 */
static void dm_test_update_scaling_rmx_center(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct dc_stream_state *stream = dm_kunit_alloc_stream(test, NULL);
	struct dm_connector_state *dm_state;
	struct drm_display_mode mode = { 0 };

	dm_state = kunit_kzalloc(test, sizeof(*dm_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dm_state);

	mode.hdisplay = 1280;
	mode.vdisplay = 720;
	stream->timing.h_addressable = 1920;
	stream->timing.v_addressable = 1080;
	dm_state->scaling = RMX_CENTER;

	amdgpu_dm_update_stream_scaling_settings(&adev->ddev, &mode, dm_state, stream);

	/* RMX_CENTER: dst = src, centered on the addressable area */
	KUNIT_EXPECT_EQ(test, stream->dst.width, 1280);
	KUNIT_EXPECT_EQ(test, stream->dst.height, 720);
	KUNIT_EXPECT_EQ(test, stream->dst.x, 320);
	KUNIT_EXPECT_EQ(test, stream->dst.y, 180);
}

/**
 * dm_test_update_scaling_underscan - Test underscan borders shrink and offset dst
 * @test: The KUnit test context
 */
static void dm_test_update_scaling_underscan(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct dc_stream_state *stream = dm_kunit_alloc_stream(test, NULL);
	struct dm_connector_state *dm_state;
	struct drm_display_mode mode = { 0 };

	dm_state = kunit_kzalloc(test, sizeof(*dm_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dm_state);

	mode.hdisplay = 1920;
	mode.vdisplay = 1080;
	stream->timing.h_addressable = 1920;
	stream->timing.v_addressable = 1080;
	dm_state->scaling = RMX_FULL;
	dm_state->underscan_enable = true;
	dm_state->underscan_hborder = 64;
	dm_state->underscan_vborder = 32;

	amdgpu_dm_update_stream_scaling_settings(&adev->ddev, &mode, dm_state, stream);

	/* Full dst, then underscan: x/y += border/2, width/height -= border */
	KUNIT_EXPECT_EQ(test, stream->dst.x, 32);
	KUNIT_EXPECT_EQ(test, stream->dst.y, 16);
	KUNIT_EXPECT_EQ(test, stream->dst.width, 1856);
	KUNIT_EXPECT_EQ(test, stream->dst.height, 1048);
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
	/* amdgpu_dm_update_stream_scaling_settings */
	KUNIT_CASE(dm_test_update_scaling_null_mode),
	KUNIT_CASE(dm_test_update_scaling_fullscreen_default),
	KUNIT_CASE(dm_test_update_scaling_rmx_full),
	KUNIT_CASE(dm_test_update_scaling_rmx_aspect_pillarbox),
	KUNIT_CASE(dm_test_update_scaling_rmx_aspect_letterbox),
	KUNIT_CASE(dm_test_update_scaling_rmx_center),
	KUNIT_CASE(dm_test_update_scaling_underscan),
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
