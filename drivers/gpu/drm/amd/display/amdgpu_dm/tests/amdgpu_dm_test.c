// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * KUnit tests for amdgpu_dm.c
 *
 * Copyright 2026 Advanced Micro Devices, Inc.
 */

#include <kunit/test.h>
#include <linux/pci.h>
#include <drm/drm_atomic.h>
#include <drm/drm_blend.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_kunit_helpers.h>
#include <drm/drm_modes.h>
#include <drm/drm_modeset_lock.h>
#include <drm/drm_plane.h>
#include <drm/drm_property.h>
#include <drm/drm_vblank.h>
#include <drm/drm_writeback.h>

#include "dc.h"
#include "dc/dc_dmub_srv.h"
#include "dal_asic_id.h"
#include "dm_services_types.h"
#include "dmub/dmub_srv.h"
#include "inc/core_types.h"
#include "logger_types.h"
#include "amd_shared.h"
#include "amdgpu.h"
#include "amdgpu_mode.h"
#include "atom.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_audio.h"
#include "amdgpu_dm_hdcp.h"
#include "amdgpu_dm_mst_types.h"
#include "amdgpu_dm_kunit_test_helpers.h"

/* Tests for simple DM callbacks */

/**
 * dm_test_wait_for_idle - Test placeholder wait-for-idle callback returns success
 * @test: The KUnit test context
 */
static void dm_test_wait_for_idle(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, dm_wait_for_idle(NULL), 0);
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
	dm_state = kzalloc_obj(*dm_state);
	KUNIT_ASSERT_NOT_NULL(test, dm_state);

	/* context == NULL: dc_state_release() is skipped and the state is freed. */
	dm_atomic_destroy_state(NULL, &dm_state->base);
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

struct dm_test_plane_update_ops_ctx {
	struct dc *dc;
	struct dc_surface_update *surface_updates;
	struct dc_stream_state *stream;
	struct dc_stream_update *stream_update;
	int surface_count;
	unsigned int call_seq;
	unsigned int post_update_seq;
	unsigned int update_seq;
	bool update_ret;
};

static struct dm_test_plane_update_ops_ctx dm_test_plane_update_ctx;

static void dm_test_post_update_surfaces_to_stream(struct dc *dc)
{
	dm_test_plane_update_ctx.dc = dc;
	dm_test_plane_update_ctx.post_update_seq = ++dm_test_plane_update_ctx.call_seq;
}

static bool dm_test_update_planes_and_stream(struct dc *dc,
					     struct dc_surface_update *surface_updates,
					     int surface_count,
					     struct dc_stream_state *dc_stream,
					     struct dc_stream_update *stream_update)
{
	dm_test_plane_update_ctx.dc = dc;
	dm_test_plane_update_ctx.surface_updates = surface_updates;
	dm_test_plane_update_ctx.surface_count = surface_count;
	dm_test_plane_update_ctx.stream = dc_stream;
	dm_test_plane_update_ctx.stream_update = stream_update;
	dm_test_plane_update_ctx.update_seq = ++dm_test_plane_update_ctx.call_seq;

	return dm_test_plane_update_ctx.update_ret;
}

static const struct amdgpu_dm_kunit_ops dm_test_plane_update_ops = {
	.post_update_surfaces_to_stream = dm_test_post_update_surfaces_to_stream,
	.update_planes_and_stream = dm_test_update_planes_and_stream,
};

static void dm_test_restore_dm_ops(void *ctx)
{
	amdgpu_dm_kunit_set_ops(NULL);
}

static void dm_test_install_dm_ops(struct kunit *test,
				   const struct amdgpu_dm_kunit_ops *ops)
{
	amdgpu_dm_kunit_set_ops(ops);
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, dm_test_restore_dm_ops, NULL), 0);
}

/**
 * dm_test_update_planes_adapter_sorts_and_forwards - Test sorting and call order
 * @test: The KUnit test context
 */
static void dm_test_update_planes_adapter_sorts_and_forwards(struct kunit *test)
{
	struct dc_surface_update *updates;
	struct dc_plane_state *planes;
	struct dc_stream_update *stream_update;
	struct dc_stream_state *stream;
	struct dc *dc;

	updates = kunit_kcalloc(test, 3, sizeof(*updates), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, updates);
	planes = kunit_kcalloc(test, 3, sizeof(*planes), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, planes);
	stream_update = kunit_kzalloc(test, sizeof(*stream_update), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, stream_update);
	stream = dm_kunit_alloc_stream(test, NULL);
	dc = dm_kunit_alloc_dc_with_ctx(test);

	planes[0].layer_index = 1;
	planes[1].layer_index = 5;
	planes[2].layer_index = 3;
	updates[0].surface = &planes[0];
	updates[1].surface = &planes[1];
	updates[2].surface = &planes[2];
	dm_test_plane_update_ctx = (struct dm_test_plane_update_ops_ctx) {
		.update_ret = true,
	};
	dm_test_install_dm_ops(test, &dm_test_plane_update_ops);

	KUNIT_EXPECT_TRUE(test, update_planes_and_stream_adapter(dc, 3,
								 stream, stream_update, updates));
	KUNIT_EXPECT_EQ(test, updates[0].surface->layer_index, 5);
	KUNIT_EXPECT_EQ(test, updates[1].surface->layer_index, 3);
	KUNIT_EXPECT_EQ(test, updates[2].surface->layer_index, 1);
	KUNIT_EXPECT_PTR_EQ(test, dm_test_plane_update_ctx.dc, dc);
	KUNIT_EXPECT_PTR_EQ(test, dm_test_plane_update_ctx.surface_updates, &updates[0]);
	KUNIT_EXPECT_EQ(test, dm_test_plane_update_ctx.surface_count, 3);
	KUNIT_EXPECT_PTR_EQ(test, dm_test_plane_update_ctx.stream, stream);
	KUNIT_EXPECT_PTR_EQ(test, dm_test_plane_update_ctx.stream_update, stream_update);
	KUNIT_EXPECT_LT(test, dm_test_plane_update_ctx.post_update_seq,
			dm_test_plane_update_ctx.update_seq);
}

/**
 * dm_test_update_planes_adapter_propagates_failure - Test DC failure is returned
 * @test: The KUnit test context
 */
static void dm_test_update_planes_adapter_propagates_failure(struct kunit *test)
{
	dm_test_plane_update_ctx = (struct dm_test_plane_update_ops_ctx) { 0 };
	dm_test_install_dm_ops(test, &dm_test_plane_update_ops);

	KUNIT_EXPECT_FALSE(test, update_planes_and_stream_adapter(NULL, 0,
								  NULL, NULL, NULL));
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

	ret = fill_plane_color_attributes(NULL, &plane_state,
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

	ret = fill_plane_color_attributes(NULL, &plane_state,
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

	ret = fill_plane_color_attributes(NULL, &plane_state,
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

	ret = fill_plane_color_attributes(NULL, &plane_state,
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

	ret = fill_plane_color_attributes(NULL, &plane_state,
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

	ret = fill_plane_color_attributes(NULL, &plane_state,
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

	ret = fill_plane_color_attributes(NULL, &plane_state,
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

	ret = fill_plane_color_attributes(NULL, &plane_state,
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

/*
 * Attach a DC stream to CRTC 0 of @adev so the scanout helpers walk the DC
 * resource context instead of bailing out early.
 */
static void dm_test_crtc_with_stream(struct kunit *test,
				     struct amdgpu_device *adev, struct dc *dc)
{
	struct dc_stream_state *stream;
	struct amdgpu_crtc *acrtc;
	struct dal_logger *logger;

	acrtc = kunit_kzalloc(test, sizeof(*acrtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, acrtc);

	logger = kunit_kzalloc(test, sizeof(*logger), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, logger);
	logger->dev = &adev->ddev;
	dc->ctx->logger = logger;

	dc->current_state = dm_kunit_alloc_dc_state(test);
	KUNIT_ASSERT_NOT_NULL(test, dc->current_state);

	stream = dm_kunit_alloc_stream(test, NULL);
	stream->ctx = dc->ctx;

	adev->dm.dc = dc;
	adev->mode_info.num_crtc = 1;
	adev->mode_info.crtcs[0] = acrtc;
	acrtc->dm_irq_params.stream = stream;
}

/**
 * dm_test_vblank_get_counter_unmapped_stream - Test a stream without a pipe returns zero
 * @test: The KUnit test context
 */
static void dm_test_vblank_get_counter_unmapped_stream(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct dc *dc = dm_kunit_alloc_dc_with_ctx(test);

	dm_test_crtc_with_stream(test, adev, dc);

	KUNIT_EXPECT_EQ(test, dm_vblank_get_counter(adev, 0), 0U);
}

/**
 * dm_test_crtc_get_scanoutpos_unmapped_stream - Test scanout position for an unmapped stream
 * @test: The KUnit test context
 */
static void dm_test_crtc_get_scanoutpos_unmapped_stream(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct dc *dc = dm_kunit_alloc_dc_with_ctx(test);
	u32 vbl = 0xdeadbeef;
	u32 position = 0xdeadbeef;

	dm_test_crtc_with_stream(test, adev, dc);

	KUNIT_EXPECT_EQ(test, dm_crtc_get_scanoutpos(adev, 0, &vbl, &position), 0);
	KUNIT_EXPECT_EQ(test, vbl, 0U);
	KUNIT_EXPECT_EQ(test, position, 0U);
}

/**
 * dm_test_crtc_get_scanoutpos_exits_idle - Test idle optimizations are disabled first
 * @test: The KUnit test context
 */
static void dm_test_crtc_get_scanoutpos_exits_idle(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct dc *dc = dm_kunit_alloc_dc_with_ctx(test);
	u32 vbl = 0;
	u32 position = 0;

	dm_test_crtc_with_stream(test, adev, dc);
	dc->caps.ips_support = true;
	dc->idle_optimizations_allowed = true;

	KUNIT_EXPECT_EQ(test, dm_crtc_get_scanoutpos(adev, 0, &vbl, &position), 0);
}

static struct drm_atomic_commit *dm_test_alloc_commit(struct kunit *test,
						     struct amdgpu_device *adev)
{
	struct drm_atomic_commit *state;

	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state);
	state->dev = &adev->ddev;

	return state;
}

/**
 * dm_test_atomic_get_state_already_acquired - Test an acquired DM state is returned as is
 * @test: The KUnit test context
 */
static void dm_test_atomic_get_state_already_acquired(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct drm_atomic_commit *state = dm_test_alloc_commit(test, adev);
	struct dm_atomic_state *dm_state;
	struct dm_atomic_state *acquired;

	dm_state = kunit_kzalloc(test, sizeof(*dm_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dm_state);
	acquired = dm_state;

	KUNIT_EXPECT_EQ(test, dm_atomic_get_state(state, &acquired), 0);
	KUNIT_EXPECT_PTR_EQ(test, acquired, dm_state);
}

/**
 * dm_test_atomic_duplicate_state_no_context - Test duplication fails without a DC context
 * @test: The KUnit test context
 */
static void dm_test_atomic_duplicate_state_no_context(struct kunit *test)
{
	struct dm_atomic_state *old_state;
	struct drm_private_obj *obj;

	obj = kunit_kzalloc(test, sizeof(*obj), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, obj);
	old_state = kunit_kzalloc(test, sizeof(*old_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, old_state);

	obj->state = &old_state->base;

	KUNIT_EXPECT_NULL(test, dm_atomic_duplicate_state(obj));
}

/* Tests for add_affected_mst_dsc_crtcs() */

/**
 * dm_test_add_affected_mst_dsc_crtcs_no_connector - Test an empty state adds nothing
 * @test: The KUnit test context
 */
static void dm_test_add_affected_mst_dsc_crtcs_no_connector(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct drm_atomic_commit *state = dm_test_alloc_commit(test, adev);
	struct drm_crtc *crtc;

	crtc = kunit_kzalloc(test, sizeof(*crtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, crtc);

	KUNIT_EXPECT_EQ(test, add_affected_mst_dsc_crtcs(state, crtc), 0);
}

/*
 * Build an atomic state holding a single connector bound to @crtc.
 */
static struct drm_atomic_commit *
dm_test_state_with_connector(struct kunit *test, struct drm_connector *connector,
			     struct drm_crtc *crtc)
{
	struct drm_connector_state *conn_state;
	struct drm_atomic_commit *state;

	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state);
	conn_state = kunit_kzalloc(test, sizeof(*conn_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, conn_state);

	state->connectors = kunit_kzalloc(test, sizeof(*state->connectors),
					  GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state->connectors);

	conn_state->crtc = crtc;
	state->num_connector = 1;
	state->connectors[0].ptr = connector;
	state->connectors[0].old_state = conn_state;
	state->connectors[0].new_state = conn_state;

	return state;
}

/**
 * dm_test_add_affected_mst_dsc_crtcs_writeback - Test writeback connectors are ignored
 * @test: The KUnit test context
 */
static void dm_test_add_affected_mst_dsc_crtcs_writeback(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;
	struct drm_atomic_commit *state;
	struct drm_crtc *crtc;

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);
	crtc = kunit_kzalloc(test, sizeof(*crtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, crtc);

	aconnector->base.connector_type = DRM_MODE_CONNECTOR_WRITEBACK;
	state = dm_test_state_with_connector(test, &aconnector->base, crtc);

	KUNIT_EXPECT_EQ(test, add_affected_mst_dsc_crtcs(state, crtc), 0);
}

/**
 * dm_test_add_affected_mst_dsc_crtcs_not_mst - Test a non-MST connector adds nothing
 * @test: The KUnit test context
 */
static void dm_test_add_affected_mst_dsc_crtcs_not_mst(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;
	struct drm_atomic_commit *state;
	struct drm_crtc *crtc;

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);
	crtc = kunit_kzalloc(test, sizeof(*crtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, crtc);

	aconnector->base.connector_type = DRM_MODE_CONNECTOR_DisplayPort;
	state = dm_test_state_with_connector(test, &aconnector->base, crtc);

	KUNIT_EXPECT_EQ(test, add_affected_mst_dsc_crtcs(state, crtc), 0);
}

/**
 * dm_test_add_affected_mst_dsc_crtcs_other_crtc - Test connectors on other CRTCs are skipped
 * @test: The KUnit test context
 */
static void dm_test_add_affected_mst_dsc_crtcs_other_crtc(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;
	struct drm_atomic_commit *state;
	struct drm_crtc *crtc, *other_crtc;

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);
	crtc = kunit_kzalloc(test, sizeof(*crtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, crtc);
	other_crtc = kunit_kzalloc(test, sizeof(*other_crtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, other_crtc);

	aconnector->base.connector_type = DRM_MODE_CONNECTOR_DisplayPort;
	state = dm_test_state_with_connector(test, &aconnector->base, other_crtc);

	KUNIT_EXPECT_EQ(test, add_affected_mst_dsc_crtcs(state, crtc), 0);
}

/**
 * dm_test_add_affected_mst_dsc_crtcs_disabled - Test a disabled connector uses its old state
 * @test: The KUnit test context
 */
static void dm_test_add_affected_mst_dsc_crtcs_disabled(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;
	struct drm_connector_state *new_conn_state;
	struct drm_atomic_commit *state;
	struct drm_crtc *crtc;

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);
	crtc = kunit_kzalloc(test, sizeof(*crtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, crtc);
	new_conn_state = kunit_kzalloc(test, sizeof(*new_conn_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, new_conn_state);

	aconnector->base.connector_type = DRM_MODE_CONNECTOR_DisplayPort;
	state = dm_test_state_with_connector(test, &aconnector->base, crtc);
	state->connectors[0].new_state = new_conn_state;

	KUNIT_EXPECT_EQ(test, add_affected_mst_dsc_crtcs(state, crtc), 0);
}

/* Tests for amdgpu_dm_commit_cursors() */

/**
 * dm_test_commit_cursors_no_planes - Test an empty plane set is a no-op
 * @test: The KUnit test context
 */
static void dm_test_commit_cursors_no_planes(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);

	amdgpu_dm_commit_cursors(dm_test_alloc_commit(test, adev));
}

/*
 * Build a commit holding a single plane of @type with empty old and new plane
 * states.
 */
static struct drm_atomic_commit *
dm_test_commit_with_plane(struct kunit *test, struct amdgpu_device *adev,
			  enum drm_plane_type type)
{
	struct drm_atomic_commit *state = dm_test_alloc_commit(test, adev);
	struct drm_plane_state *old_state, *new_state;
	struct drm_plane *plane;

	plane = kunit_kzalloc(test, sizeof(*plane), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, plane);
	old_state = kunit_kzalloc(test, sizeof(*old_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, old_state);
	new_state = kunit_kzalloc(test, sizeof(*new_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, new_state);
	state->planes = kunit_kzalloc(test, sizeof(*state->planes), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state->planes);

	plane->type = type;
	plane->dev = &adev->ddev;
	plane->state = new_state;
	adev->ddev.mode_config.num_total_plane = 1;
	state->planes[0].ptr = plane;
	state->planes[0].old_state = old_state;

	return state;
}

/**
 * dm_test_commit_cursors_skips_non_cursor - Test non-cursor planes are skipped
 * @test: The KUnit test context
 */
static void dm_test_commit_cursors_skips_non_cursor(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);

	amdgpu_dm_commit_cursors(dm_test_commit_with_plane(test, adev,
							   DRM_PLANE_TYPE_PRIMARY));
}

/**
 * dm_test_commit_cursors_updates_cursor - Test cursor planes reach the cursor update
 * @test: The KUnit test context
 */
static void dm_test_commit_cursors_updates_cursor(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);

	amdgpu_dm_commit_cursors(dm_test_commit_with_plane(test, adev,
							   DRM_PLANE_TYPE_CURSOR));
}

/**
 * dm_test_update_cursor_no_framebuffer - Test missing framebuffers leave the update untouched
 * @test: The KUnit test context
 */
static void dm_test_update_cursor_no_framebuffer(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct drm_atomic_commit *state;
	struct dc_stream_update *update;
	struct drm_plane *plane;

	state = dm_test_commit_with_plane(test, adev, DRM_PLANE_TYPE_CURSOR);
	plane = state->planes[0].ptr;
	update = kunit_kzalloc(test, sizeof(*update), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, update);

	update->cursor_attributes = (void *)1;
	update->cursor_position = (void *)1;

	amdgpu_dm_update_cursor(plane, state->planes[0].old_state, update);

	KUNIT_EXPECT_PTR_EQ(test, update->cursor_attributes, (void *)1);
	KUNIT_EXPECT_PTR_EQ(test, update->cursor_position, (void *)1);
}

/**
 * dm_test_update_cursor_disables_stream - Test removing the framebuffer disables the cursor
 * @test: The KUnit test context
 */
static void dm_test_update_cursor_disables_stream(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct drm_atomic_commit *state;
	struct dc_stream_update *update;
	struct drm_plane_state *old_plane_state;
	struct dm_crtc_state *crtc_state;
	struct amdgpu_framebuffer *afb;
	struct amdgpu_crtc *acrtc;
	struct drm_plane *plane;

	state = dm_test_commit_with_plane(test, adev, DRM_PLANE_TYPE_CURSOR);
	plane = state->planes[0].ptr;
	old_plane_state = state->planes[0].old_state;
	crtc_state = kunit_kzalloc(test, sizeof(*crtc_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, crtc_state);
	afb = kunit_kzalloc(test, sizeof(*afb), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, afb);
	acrtc = kunit_kzalloc(test, sizeof(*acrtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, acrtc);
	update = kunit_kzalloc(test, sizeof(*update), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, update);

	old_plane_state->fb = &afb->base;
	old_plane_state->crtc = &acrtc->base;
	acrtc->base.state = &crtc_state->base;
	crtc_state->stream = dm_kunit_alloc_stream(test, NULL);
	crtc_state->stream->cursor_position.enable = true;

	amdgpu_dm_update_cursor(plane, old_plane_state, update);

	KUNIT_EXPECT_FALSE(test, crtc_state->stream->cursor_position.enable);
	KUNIT_EXPECT_PTR_EQ(test, update->cursor_position,
			    &crtc_state->stream->cursor_position);
	KUNIT_EXPECT_NULL(test, update->cursor_attributes);
}

/* Tests for dm_arm_vblank_event() */

struct dm_test_vblank_ctx {
	struct amdgpu_device *adev;
	struct amdgpu_crtc *acrtc;
	struct dm_crtc_state *acrtc_state;
	struct drm_pending_vblank_event *event;
};

static int dm_test_vblank_enable(struct drm_crtc *crtc)
{
	return 0;
}

static void dm_test_vblank_disable(struct drm_crtc *crtc)
{
}

static u32 dm_test_vblank_counter(struct drm_crtc *crtc)
{
	return 0;
}

static const struct drm_crtc_funcs dm_test_vblank_crtc_funcs = {
	.enable_vblank = dm_test_vblank_enable,
	.disable_vblank = dm_test_vblank_disable,
	.get_vblank_counter = dm_test_vblank_counter,
};

static void dm_test_vblank_ctx_cleanup(void *data)
{
	struct amdgpu_crtc *acrtc = data;

	if (drm_dev_has_vblank(acrtc->base.dev))
		drm_crtc_vblank_off(&acrtc->base);
	list_del_init(&acrtc->base.head);
}

/*
 * A CRTC with one active plane and a pending vblank event. There is no
 * initialised vblank, so drm_crtc_vblank_get() fails, which the function under
 * test ignores.
 */
static struct dm_test_vblank_ctx *dm_test_vblank_ctx_alloc(struct kunit *test)
{
	struct dm_test_vblank_ctx *ctx;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	ctx->adev = dm_kunit_alloc_adev(test);
	ctx->acrtc = kunit_kzalloc(test, sizeof(*ctx->acrtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->acrtc);
	ctx->acrtc_state = kunit_kzalloc(test, sizeof(*ctx->acrtc_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->acrtc_state);
	ctx->event = kunit_kzalloc(test, sizeof(*ctx->event), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->event);

	ctx->acrtc->base.dev = &ctx->adev->ddev;
	ctx->acrtc->base.funcs = &dm_test_vblank_crtc_funcs;
	INIT_LIST_HEAD(&ctx->acrtc->base.head);
	list_add_tail(&ctx->acrtc->base.head,
		      &ctx->adev->ddev.mode_config.crtc_list);
	ctx->acrtc->base.state = &ctx->acrtc_state->base;
	ctx->acrtc_state->base.event = ctx->event;
	ctx->acrtc_state->active_planes = 1;
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, dm_test_vblank_ctx_cleanup, ctx->acrtc), 0);

	return ctx;
}

static void dm_test_arm_vblank(struct dm_test_vblank_ctx *ctx, bool pflip_update,
			       bool cursor_update)
{
	unsigned long flags;

	spin_lock_irqsave(&ctx->adev->ddev.event_lock, flags);
	dm_arm_vblank_event(ctx->acrtc, ctx->acrtc_state, pflip_update,
			    cursor_update);
	spin_unlock_irqrestore(&ctx->adev->ddev.event_lock, flags);
}

static void dm_test_arm_vblank_pre_programming(struct dm_test_vblank_ctx *ctx,
					       bool pflip_update, bool cursor_update)
{
	struct dm_crtc_state *state = ctx->acrtc_state;
	struct amdgpu_crtc *acrtc = ctx->acrtc;
	unsigned long flags;

	spin_lock_irqsave(&ctx->adev->ddev.event_lock, flags);
	dm_arm_vblank_event_pre_programming(acrtc, state, pflip_update, cursor_update);
	spin_unlock_irqrestore(&ctx->adev->ddev.event_lock, flags);
}

/**
 * dm_test_arm_vblank_pre_programming_no_event - Test missing event takes no reference
 * @test: The KUnit test context
 */
static void dm_test_arm_vblank_pre_programming_no_event(struct kunit *test)
{
	struct dm_test_vblank_ctx *ctx = dm_test_vblank_ctx_alloc(test);
	struct drm_vblank_crtc *vblank;

	KUNIT_ASSERT_EQ(test, drm_vblank_init(&ctx->adev->ddev, 1), 0);
	vblank = drm_crtc_vblank_crtc(&ctx->acrtc->base);
	ctx->acrtc_state->base.event = NULL;

	dm_test_arm_vblank_pre_programming(ctx, true, false);

	KUNIT_EXPECT_EQ(test, atomic_read(&vblank->refcount), 0);
}

/**
 * dm_test_arm_vblank_pre_programming_no_planes - Test inactive CRTC takes no reference
 * @test: The KUnit test context
 */
static void dm_test_arm_vblank_pre_programming_no_planes(struct kunit *test)
{
	struct dm_test_vblank_ctx *ctx = dm_test_vblank_ctx_alloc(test);
	struct drm_vblank_crtc *vblank;

	KUNIT_ASSERT_EQ(test, drm_vblank_init(&ctx->adev->ddev, 1), 0);
	vblank = drm_crtc_vblank_crtc(&ctx->acrtc->base);
	ctx->acrtc_state->active_planes = 0;

	dm_test_arm_vblank_pre_programming(ctx, false, true);

	KUNIT_EXPECT_EQ(test, atomic_read(&vblank->refcount), 0);
}

/**
 * dm_test_arm_vblank_pre_programming_update - Test an update takes a vblank reference
 * @test: The KUnit test context
 */
static void dm_test_arm_vblank_pre_programming_update(struct kunit *test)
{
	struct dm_test_vblank_ctx *ctx = dm_test_vblank_ctx_alloc(test);
	struct drm_vblank_crtc *vblank;

	KUNIT_ASSERT_EQ(test, drm_vblank_init(&ctx->adev->ddev, 1), 0);
	vblank = drm_crtc_vblank_crtc(&ctx->acrtc->base);

	dm_test_arm_vblank_pre_programming(ctx, true, false);

	KUNIT_EXPECT_EQ(test, atomic_read(&vblank->refcount), 1);
	drm_crtc_vblank_put(&ctx->acrtc->base);
}

/**
 * dm_test_arm_vblank_event_no_event - Test a commit without an event is a no-op
 * @test: The KUnit test context
 */
static void dm_test_arm_vblank_event_no_event(struct kunit *test)
{
	struct dm_test_vblank_ctx *ctx = dm_test_vblank_ctx_alloc(test);

	ctx->acrtc_state->base.event = NULL;

	dm_test_arm_vblank(ctx, true, false);

	KUNIT_EXPECT_NULL(test, ctx->acrtc->event);
}

/**
 * dm_test_arm_vblank_event_no_active_planes - Test an event is left armed without planes
 * @test: The KUnit test context
 */
static void dm_test_arm_vblank_event_no_active_planes(struct kunit *test)
{
	struct dm_test_vblank_ctx *ctx = dm_test_vblank_ctx_alloc(test);

	ctx->acrtc_state->active_planes = 0;

	dm_test_arm_vblank(ctx, false, true);

	KUNIT_EXPECT_NULL(test, ctx->acrtc->event);
	KUNIT_EXPECT_PTR_EQ(test, ctx->acrtc_state->base.event, ctx->event);
}

/**
 * dm_test_arm_vblank_event_pflip - Test a page flip arms the flip ISR
 * @test: The KUnit test context
 */
static void dm_test_arm_vblank_event_pflip(struct kunit *test)
{
	struct dm_test_vblank_ctx *ctx = dm_test_vblank_ctx_alloc(test);

	dm_test_arm_vblank(ctx, true, false);

	KUNIT_EXPECT_PTR_EQ(test, ctx->acrtc->event, ctx->event);
	KUNIT_EXPECT_NULL(test, ctx->acrtc_state->base.event);
	KUNIT_EXPECT_EQ(test, (int)ctx->acrtc->pflip_status,
			(int)AMDGPU_FLIP_SUBMITTED);
}

/**
 * dm_test_arm_vblank_event_cursor - Test a cursor update consumes the event
 * @test: The KUnit test context
 */
static void dm_test_arm_vblank_event_cursor(struct kunit *test)
{
	struct dm_test_vblank_ctx *ctx = dm_test_vblank_ctx_alloc(test);

	dm_test_arm_vblank(ctx, false, true);

	KUNIT_EXPECT_PTR_EQ(test, ctx->acrtc->event, ctx->event);
	KUNIT_EXPECT_NULL(test, ctx->acrtc_state->base.event);
	KUNIT_EXPECT_EQ(test, (int)ctx->acrtc->pflip_status,
			(int)AMDGPU_FLIP_NONE);
}

/**
 * dm_test_update_pflip_irq_state_dcn - Test DCN skips the GRPH_PFLIP reapply
 * @test: The KUnit test context
 */
static void dm_test_update_pflip_irq_state_dcn(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct amdgpu_crtc *acrtc;

	acrtc = kunit_kzalloc(test, sizeof(*acrtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, acrtc);

	adev->mode_info.num_crtc = 1;
	adev->ip_versions[DCE_HWIP][0] = IP_VERSION(3, 2, 0);

	dm_update_pflip_irq_state(adev, acrtc);
}

/*
 * Spy for the pageflip IRQ source: amdgpu_irq_update() always dispatches
 * through src->funcs->set(), which needs a registered IH ring on real
 * hardware. Recording the requested state instead keeps the DCE reapply path
 * reachable and observable.
 */
struct dm_test_irq_spy {
	unsigned int set_count;
	unsigned int last_type;
	enum amdgpu_interrupt_state last_state;
};

static struct dm_test_irq_spy dm_test_irq_spy_data;

static int dm_test_irq_set(struct amdgpu_device *adev,
			   struct amdgpu_irq_src *src, unsigned int type,
			   enum amdgpu_interrupt_state state)
{
	dm_test_irq_spy_data.set_count++;
	dm_test_irq_spy_data.last_type = type;
	dm_test_irq_spy_data.last_state = state;

	return 0;
}

static const struct amdgpu_irq_src_funcs dm_test_irq_funcs = {
	.set = dm_test_irq_set,
};

/**
 * dm_test_update_pflip_irq_state_dce - Test DCE reapplies the GRPH_PFLIP state
 * @test: The KUnit test context
 */
static void dm_test_update_pflip_irq_state_dce(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct amdgpu_crtc *acrtc;

	acrtc = kunit_kzalloc(test, sizeof(*acrtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, acrtc);

	memset(&dm_test_irq_spy_data, 0, sizeof(dm_test_irq_spy_data));
	adev->mode_info.num_crtc = 2;
	adev->ip_versions[DCE_HWIP][0] = 0;
	adev->pageflip_irq.funcs = &dm_test_irq_funcs;
	acrtc->crtc_id = 1;
	spin_lock_init(&adev->irq.lock);

	dm_update_pflip_irq_state(adev, acrtc);

	KUNIT_EXPECT_EQ(test, dm_test_irq_spy_data.set_count, 1U);
	KUNIT_EXPECT_EQ(test, dm_test_irq_spy_data.last_type,
			(unsigned int)AMDGPU_CRTC_IRQ_VBLANK2);
	KUNIT_EXPECT_EQ(test, (int)dm_test_irq_spy_data.last_state,
			(int)AMDGPU_IRQ_STATE_DISABLE);
}

/* Tests for amdgpu_dm_crtc_mem_type_changed() */

struct dm_test_mem_type_ctx {
	struct amdgpu_device *adev;
	struct drm_atomic_commit *state;
	struct drm_crtc_state *crtc_state;
	struct drm_plane *plane;
	struct drm_plane_state *old_plane_state;
	struct drm_plane_state *new_plane_state;
};

/*
 * Register a single plane on the CRTC's plane mask. get_mem_type() walks
 * fb->obj[0] back to an amdgpu_bo, so the framebuffers are backed by fake
 * buffer objects with a TTM resource instead of a live TTM device.
 */
static struct drm_framebuffer *dm_test_alloc_fb(struct kunit *test,
						u32 mem_type)
{
	struct drm_framebuffer *fb;
	struct ttm_resource *res;
	struct amdgpu_bo *abo;

	fb = kunit_kzalloc(test, sizeof(*fb), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, fb);
	abo = kunit_kzalloc(test, sizeof(*abo), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, abo);
	res = kunit_kzalloc(test, sizeof(*res), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, res);

	res->mem_type = mem_type;
	abo->tbo.resource = res;
	fb->obj[0] = &abo->tbo.base;

	return fb;
}

static struct dm_test_mem_type_ctx *dm_test_mem_type_ctx_alloc(struct kunit *test)
{
	struct dm_test_mem_type_ctx *ctx;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	ctx->adev = dm_kunit_alloc_adev(test);
	ctx->state = dm_test_alloc_commit(test, ctx->adev);
	ctx->crtc_state = kunit_kzalloc(test, sizeof(*ctx->crtc_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->crtc_state);
	ctx->plane = drm_kunit_helper_create_primary_plane(test, &ctx->adev->ddev,
							   NULL, NULL, NULL, 0, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx->plane);
	ctx->old_plane_state = kunit_kzalloc(test, sizeof(*ctx->old_plane_state),
					     GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->old_plane_state);
	ctx->new_plane_state = kunit_kzalloc(test, sizeof(*ctx->new_plane_state),
					     GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->new_plane_state);
	ctx->state->planes = kunit_kcalloc(test, ctx->plane->index + 1,
					   sizeof(*ctx->state->planes), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->state->planes);

	ctx->crtc_state->plane_mask = drm_plane_mask(ctx->plane);
	ctx->state->planes[ctx->plane->index].ptr = ctx->plane;

	return ctx;
}

/**
 * dm_test_mem_type_changed_no_planes - Test an empty plane mask reports no change
 * @test: The KUnit test context
 */
static void dm_test_mem_type_changed_no_planes(struct kunit *test)
{
	struct dm_test_mem_type_ctx *ctx = dm_test_mem_type_ctx_alloc(test);

	ctx->crtc_state->plane_mask = 0;

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_crtc_mem_type_changed(&ctx->adev->ddev,
								ctx->state,
								ctx->crtc_state));
}

/**
 * dm_test_mem_type_changed_missing_state - Test a plane without both states is skipped
 * @test: The KUnit test context
 */
static void dm_test_mem_type_changed_missing_state(struct kunit *test)
{
	struct dm_test_mem_type_ctx *ctx = dm_test_mem_type_ctx_alloc(test);

	ctx->state->planes[ctx->plane->index].new_state = ctx->new_plane_state;

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_crtc_mem_type_changed(&ctx->adev->ddev,
								ctx->state,
								ctx->crtc_state));
}

/**
 * dm_test_mem_type_changed_same_domain - Test identical memory domains report no change
 * @test: The KUnit test context
 */
static void dm_test_mem_type_changed_same_domain(struct kunit *test)
{
	struct dm_test_mem_type_ctx *ctx = dm_test_mem_type_ctx_alloc(test);

	ctx->old_plane_state->fb = dm_test_alloc_fb(test, TTM_PL_VRAM);
	ctx->new_plane_state->fb = dm_test_alloc_fb(test, TTM_PL_VRAM);
	ctx->state->planes[ctx->plane->index].old_state = ctx->old_plane_state;
	ctx->state->planes[ctx->plane->index].new_state = ctx->new_plane_state;

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_crtc_mem_type_changed(&ctx->adev->ddev,
								ctx->state,
								ctx->crtc_state));
}

/**
 * dm_test_mem_type_changed_different_domain - Test a domain migration is detected
 * @test: The KUnit test context
 */
static void dm_test_mem_type_changed_different_domain(struct kunit *test)
{
	struct dm_test_mem_type_ctx *ctx = dm_test_mem_type_ctx_alloc(test);

	ctx->old_plane_state->fb = dm_test_alloc_fb(test, TTM_PL_TT);
	ctx->new_plane_state->fb = dm_test_alloc_fb(test, TTM_PL_VRAM);
	ctx->state->planes[ctx->plane->index].old_state = ctx->old_plane_state;
	ctx->state->planes[ctx->plane->index].new_state = ctx->new_plane_state;

	KUNIT_EXPECT_TRUE(test, amdgpu_dm_crtc_mem_type_changed(&ctx->adev->ddev,
							       ctx->state,
							       ctx->crtc_state));
}

/* Tests for fill_dc_dirty_rects() */

struct dm_test_dirty_ctx {
	struct amdgpu_device *adev;
	struct drm_plane *plane;
	struct drm_plane_state *old_plane_state;
	struct drm_plane_state *new_plane_state;
	struct dm_crtc_state *crtc_state;
	struct dc_flip_addrs *flip_addrs;
	bool dirty_regions_changed;
};

static struct dm_test_dirty_ctx *dm_test_dirty_ctx_alloc(struct kunit *test)
{
	struct dm_test_dirty_ctx *ctx;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	ctx->adev = dm_kunit_alloc_adev(test);
	ctx->plane = drm_kunit_helper_create_primary_plane(test, &ctx->adev->ddev,
							   NULL, NULL, NULL, 0, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx->plane);
	drm_plane_enable_fb_damage_clips(ctx->plane);
	ctx->old_plane_state = kunit_kzalloc(test, sizeof(*ctx->old_plane_state),
					     GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->old_plane_state);
	ctx->new_plane_state = kunit_kzalloc(test, sizeof(*ctx->new_plane_state),
					     GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->new_plane_state);
	ctx->crtc_state = kunit_kzalloc(test, sizeof(*ctx->crtc_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->crtc_state);
	ctx->flip_addrs = kunit_kzalloc(test, sizeof(*ctx->flip_addrs), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->flip_addrs);

	ctx->plane->type = DRM_PLANE_TYPE_PRIMARY;
	ctx->new_plane_state->plane = ctx->plane;
	ctx->new_plane_state->rotation = DRM_MODE_ROTATE_0;
	ctx->old_plane_state->plane = ctx->plane;
	ctx->crtc_state->base.mode.crtc_hdisplay = 1920;
	ctx->crtc_state->base.mode.crtc_vdisplay = 1080;

	return ctx;
}

static void dm_test_fill_dirty_rects(struct dm_test_dirty_ctx *ctx, bool is_psr_su)
{
	fill_dc_dirty_rects(ctx->plane, ctx->old_plane_state,
			    ctx->new_plane_state, &ctx->crtc_state->base,
			    ctx->flip_addrs, is_psr_su,
			    &ctx->dirty_regions_changed);
}

/*
 * Attach @count damage clips to the new plane state. The blob is only ever
 * read through drm_plane_get_damage_clips(), so a bare blob is enough.
 */
static struct drm_mode_rect *dm_test_add_damage_clips(struct kunit *test,
						      struct drm_plane_state *state,
						      unsigned int count)
{
	struct drm_property_blob *blob;
	struct drm_mode_rect *clips;

	blob = kunit_kzalloc(test, sizeof(*blob), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, blob);
	clips = kunit_kcalloc(test, count, sizeof(*clips), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, clips);

	blob->length = count * sizeof(*clips);
	blob->data = clips;
	state->fb_damage_clips = blob;

	return clips;
}

/**
 * dm_test_dirty_rects_cursor_plane - Test cursor planes are left to their own path
 * @test: The KUnit test context
 */
static void dm_test_dirty_rects_cursor_plane(struct kunit *test)
{
	struct dm_test_dirty_ctx *ctx = dm_test_dirty_ctx_alloc(test);

	ctx->plane->type = DRM_PLANE_TYPE_CURSOR;

	dm_test_fill_dirty_rects(ctx, true);

	KUNIT_EXPECT_EQ(test, ctx->flip_addrs->dirty_rect_count, 0U);
}

/**
 * dm_test_dirty_rects_rotation_ffu - Test a rotated plane falls back to full frame update
 * @test: The KUnit test context
 */
static void dm_test_dirty_rects_rotation_ffu(struct kunit *test)
{
	struct dm_test_dirty_ctx *ctx = dm_test_dirty_ctx_alloc(test);

	ctx->new_plane_state->rotation = DRM_MODE_ROTATE_90;

	dm_test_fill_dirty_rects(ctx, true);

	KUNIT_EXPECT_EQ(test, ctx->flip_addrs->dirty_rect_count, 1U);
	KUNIT_EXPECT_EQ(test, ctx->flip_addrs->dirty_rects[0].x, 0);
	KUNIT_EXPECT_EQ(test, ctx->flip_addrs->dirty_rects[0].y, 0);
	KUNIT_EXPECT_EQ(test, ctx->flip_addrs->dirty_rects[0].width, 1920);
	KUNIT_EXPECT_EQ(test, ctx->flip_addrs->dirty_rects[0].height, 1080);
}

/**
 * dm_test_dirty_rects_no_clips_ffu - Test a damage-unaware client gets a full frame update
 * @test: The KUnit test context
 */
static void dm_test_dirty_rects_no_clips_ffu(struct kunit *test)
{
	struct dm_test_dirty_ctx *ctx = dm_test_dirty_ctx_alloc(test);

	dm_test_fill_dirty_rects(ctx, true);

	KUNIT_EXPECT_EQ(test, ctx->flip_addrs->dirty_rect_count, 1U);
	KUNIT_EXPECT_EQ(test, ctx->flip_addrs->dirty_rects[0].width, 1920);
	KUNIT_EXPECT_EQ(test, ctx->flip_addrs->dirty_rects[0].height, 1080);
}

/**
 * dm_test_dirty_rects_ignored_damage_clips - Test ignored damage clips force a full update
 * @test: The KUnit test context
 */
static void dm_test_dirty_rects_ignored_damage_clips(struct kunit *test)
{
	struct dm_test_dirty_ctx *ctx = dm_test_dirty_ctx_alloc(test);

	dm_test_add_damage_clips(test, ctx->new_plane_state, 1);
	ctx->new_plane_state->ignore_damage_clips = true;

	dm_test_fill_dirty_rects(ctx, true);

	KUNIT_EXPECT_EQ(test, ctx->flip_addrs->dirty_rect_count, 1U);
	KUNIT_EXPECT_EQ(test, ctx->flip_addrs->dirty_rects[0].width, 1920);
}

/**
 * dm_test_dirty_rects_too_many_clips_ffu - Test exceeding DC_MAX_DIRTY_RECTS falls back
 * @test: The KUnit test context
 */
static void dm_test_dirty_rects_too_many_clips_ffu(struct kunit *test)
{
	struct dm_test_dirty_ctx *ctx = dm_test_dirty_ctx_alloc(test);

	dm_test_add_damage_clips(test, ctx->new_plane_state,
				 DC_MAX_DIRTY_RECTS + 1);

	dm_test_fill_dirty_rects(ctx, false);

	KUNIT_EXPECT_EQ(test, ctx->flip_addrs->dirty_rect_count, 1U);
	KUNIT_EXPECT_EQ(test, ctx->flip_addrs->dirty_rects[0].width, 1920);
}

/**
 * dm_test_dirty_rects_damage_clips - Test damage clips are copied verbatim
 * @test: The KUnit test context
 */
static void dm_test_dirty_rects_damage_clips(struct kunit *test)
{
	struct dm_test_dirty_ctx *ctx = dm_test_dirty_ctx_alloc(test);
	struct drm_mode_rect *clips;

	clips = dm_test_add_damage_clips(test, ctx->new_plane_state, 2);
	clips[0].x1 = 10;
	clips[0].y1 = 20;
	clips[0].x2 = 40;
	clips[0].y2 = 60;
	clips[1].x1 = 100;
	clips[1].y1 = 200;
	clips[1].x2 = 150;
	clips[1].y2 = 260;

	dm_test_fill_dirty_rects(ctx, false);

	KUNIT_EXPECT_EQ(test, ctx->flip_addrs->dirty_rect_count, 2U);
	KUNIT_EXPECT_EQ(test, ctx->flip_addrs->dirty_rects[0].x, 10);
	KUNIT_EXPECT_EQ(test, ctx->flip_addrs->dirty_rects[0].y, 20);
	KUNIT_EXPECT_EQ(test, ctx->flip_addrs->dirty_rects[0].width, 30);
	KUNIT_EXPECT_EQ(test, ctx->flip_addrs->dirty_rects[0].height, 40);
	KUNIT_EXPECT_EQ(test, ctx->flip_addrs->dirty_rects[1].x, 100);
	KUNIT_EXPECT_EQ(test, ctx->flip_addrs->dirty_rects[1].width, 50);
	KUNIT_EXPECT_FALSE(test, ctx->dirty_regions_changed);
}

/**
 * dm_test_dirty_rects_mpo_bb_changed - Test MPO adds both plane bounding boxes
 * @test: The KUnit test context
 */
static void dm_test_dirty_rects_mpo_bb_changed(struct kunit *test)
{
	struct dm_test_dirty_ctx *ctx = dm_test_dirty_ctx_alloc(test);
	struct drm_framebuffer *fb;

	fb = kunit_kzalloc(test, sizeof(*fb), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, fb);

	ctx->crtc_state->mpo_requested = true;
	ctx->old_plane_state->fb = fb;
	ctx->new_plane_state->fb = fb;
	ctx->old_plane_state->crtc_x = 0;
	ctx->old_plane_state->crtc_y = 0;
	ctx->old_plane_state->crtc_w = 640;
	ctx->old_plane_state->crtc_h = 480;
	ctx->new_plane_state->crtc_x = 100;
	ctx->new_plane_state->crtc_y = 50;
	ctx->new_plane_state->crtc_w = 800;
	ctx->new_plane_state->crtc_h = 600;

	dm_test_fill_dirty_rects(ctx, true);

	KUNIT_EXPECT_TRUE(test, ctx->dirty_regions_changed);
	KUNIT_EXPECT_EQ(test, ctx->flip_addrs->dirty_rect_count, 2U);
	KUNIT_EXPECT_EQ(test, ctx->flip_addrs->dirty_rects[0].x, 100);
	KUNIT_EXPECT_EQ(test, ctx->flip_addrs->dirty_rects[0].width, 800);
	KUNIT_EXPECT_EQ(test, ctx->flip_addrs->dirty_rects[1].x, 0);
	KUNIT_EXPECT_EQ(test, ctx->flip_addrs->dirty_rects[1].width, 640);
}

/**
 * dm_test_dirty_rects_mpo_fb_changed - Test MPO flips add the new plane bounding box
 * @test: The KUnit test context
 */
static void dm_test_dirty_rects_mpo_fb_changed(struct kunit *test)
{
	struct dm_test_dirty_ctx *ctx = dm_test_dirty_ctx_alloc(test);
	struct drm_framebuffer *old_fb;
	struct drm_framebuffer *new_fb;

	old_fb = kunit_kzalloc(test, sizeof(*old_fb), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, old_fb);
	new_fb = kunit_kzalloc(test, sizeof(*new_fb), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, new_fb);

	old_fb->base.id = 1;
	new_fb->base.id = 2;
	ctx->crtc_state->mpo_requested = true;
	ctx->old_plane_state->fb = old_fb;
	ctx->new_plane_state->fb = new_fb;
	ctx->new_plane_state->crtc_w = 800;
	ctx->new_plane_state->crtc_h = 600;
	ctx->old_plane_state->crtc_w = 800;
	ctx->old_plane_state->crtc_h = 600;

	dm_test_fill_dirty_rects(ctx, true);

	KUNIT_EXPECT_FALSE(test, ctx->dirty_regions_changed);
	KUNIT_EXPECT_EQ(test, ctx->flip_addrs->dirty_rect_count, 1U);
	KUNIT_EXPECT_EQ(test, ctx->flip_addrs->dirty_rects[0].width, 800);
	KUNIT_EXPECT_EQ(test, ctx->flip_addrs->dirty_rects[0].height, 600);
}

/**
 * dm_test_dirty_rects_psr_su_ffu - Test PSR SU ignores clips in auto damage mode
 * @test: The KUnit test context
 */
static void dm_test_dirty_rects_psr_su_ffu(struct kunit *test)
{
	struct dm_test_dirty_ctx *ctx = dm_test_dirty_ctx_alloc(test);

	dm_test_add_damage_clips(test, ctx->new_plane_state, 1);

	dm_test_fill_dirty_rects(ctx, true);

	KUNIT_EXPECT_EQ(test, ctx->flip_addrs->dirty_rect_count, 1U);
	KUNIT_EXPECT_EQ(test, ctx->flip_addrs->dirty_rects[0].width, 1920);
	KUNIT_EXPECT_EQ(test, ctx->flip_addrs->dirty_rects[0].height, 1080);
}

/**
 * dm_test_dirty_rects_mpo_clips - Test MPO copies damage clips when the box is stable
 * @test: The KUnit test context
 */
static void dm_test_dirty_rects_mpo_clips(struct kunit *test)
{
	struct dm_test_dirty_ctx *ctx = dm_test_dirty_ctx_alloc(test);
	struct drm_mode_rect *clips;
	struct drm_framebuffer *fb;

	fb = kunit_kzalloc(test, sizeof(*fb), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, fb);

	clips = dm_test_add_damage_clips(test, ctx->new_plane_state, 2);
	clips[0].x1 = 5;
	clips[0].y1 = 6;
	clips[0].x2 = 25;
	clips[0].y2 = 36;
	clips[1].x1 = 50;
	clips[1].y1 = 60;
	clips[1].x2 = 90;
	clips[1].y2 = 110;

	ctx->crtc_state->mpo_requested = true;
	ctx->old_plane_state->fb = fb;
	ctx->new_plane_state->fb = fb;

	dm_test_fill_dirty_rects(ctx, false);

	KUNIT_EXPECT_FALSE(test, ctx->dirty_regions_changed);
	KUNIT_EXPECT_EQ(test, ctx->flip_addrs->dirty_rect_count, 2U);
	KUNIT_EXPECT_EQ(test, ctx->flip_addrs->dirty_rects[0].x, 5);
	KUNIT_EXPECT_EQ(test, ctx->flip_addrs->dirty_rects[0].width, 20);
	KUNIT_EXPECT_EQ(test, ctx->flip_addrs->dirty_rects[1].x, 50);
	KUNIT_EXPECT_EQ(test, ctx->flip_addrs->dirty_rects[1].height, 50);
}

/**
 * dm_test_dirty_rects_mpo_overflow_ffu - Test MPO falls back when clips plus boxes overflow
 * @test: The KUnit test context
 */
static void dm_test_dirty_rects_mpo_overflow_ffu(struct kunit *test)
{
	struct dm_test_dirty_ctx *ctx = dm_test_dirty_ctx_alloc(test);
	struct drm_framebuffer *fb;

	fb = kunit_kzalloc(test, sizeof(*fb), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, fb);

	dm_test_add_damage_clips(test, ctx->new_plane_state, 2);

	ctx->crtc_state->mpo_requested = true;
	ctx->old_plane_state->fb = fb;
	ctx->new_plane_state->fb = fb;
	ctx->new_plane_state->crtc_x = 100;

	dm_test_fill_dirty_rects(ctx, false);

	KUNIT_EXPECT_TRUE(test, ctx->dirty_regions_changed);
	KUNIT_EXPECT_EQ(test, ctx->flip_addrs->dirty_rect_count, 1U);
	KUNIT_EXPECT_EQ(test, ctx->flip_addrs->dirty_rects[0].width, 1920);
}

/* Tests for should_reset_plane() */

struct dm_test_reset_plane_ctx {
	struct amdgpu_device *adev;
	struct drm_atomic_commit *state;
	struct drm_crtc *crtc;
	struct drm_plane *plane;
	struct drm_plane_state *old_plane_state;
	struct drm_plane_state *new_plane_state;
	struct dm_crtc_state *old_crtc_state;
	struct dm_crtc_state *new_crtc_state;
	struct drm_plane *other;
	struct dm_plane_state *other_old;
	struct dm_plane_state *other_new;
};

/*
 * Build a fast-update baseline: a DCN 3.2 device with one plane bound to an
 * unchanged CRTC, so should_reset_plane() runs to the end and returns false.
 */
static struct dm_test_reset_plane_ctx *
dm_test_reset_plane_ctx_alloc(struct kunit *test)
{
	struct dm_test_reset_plane_ctx *ctx;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	ctx->adev = dm_kunit_alloc_adev(test);
	ctx->adev->ip_versions[DCE_HWIP][0] = IP_VERSION(3, 2, 0);
	ctx->adev->reset_domain = kunit_kzalloc(test,
						sizeof(*ctx->adev->reset_domain),
						GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->adev->reset_domain);

	ctx->state = dm_test_alloc_commit(test, ctx->adev);
	ctx->crtc = kunit_kzalloc(test, sizeof(*ctx->crtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->crtc);
	ctx->plane = kunit_kzalloc(test, sizeof(*ctx->plane), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->plane);
	ctx->old_plane_state = kunit_kzalloc(test, sizeof(*ctx->old_plane_state),
					     GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->old_plane_state);
	ctx->new_plane_state = kunit_kzalloc(test, sizeof(*ctx->new_plane_state),
					     GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->new_plane_state);
	ctx->old_crtc_state = kunit_kzalloc(test, sizeof(*ctx->old_crtc_state),
					    GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->old_crtc_state);
	ctx->new_crtc_state = kunit_kzalloc(test, sizeof(*ctx->new_crtc_state),
					    GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->new_crtc_state);
	ctx->state->crtcs = kunit_kzalloc(test, sizeof(*ctx->state->crtcs),
					  GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->state->crtcs);

	ctx->plane->dev = &ctx->adev->ddev;
	ctx->plane->type = DRM_PLANE_TYPE_PRIMARY;
	ctx->old_plane_state->crtc = ctx->crtc;
	ctx->new_plane_state->crtc = ctx->crtc;
	ctx->state->crtcs[0].ptr = ctx->crtc;
	ctx->state->crtcs[0].old_state = &ctx->old_crtc_state->base;
	ctx->state->crtcs[0].new_state = &ctx->new_crtc_state->base;

	return ctx;
}

static bool dm_test_should_reset_plane(struct dm_test_reset_plane_ctx *ctx)
{
	return should_reset_plane(ctx->state, ctx->plane, ctx->old_plane_state,
				  ctx->new_plane_state);
}

/**
 * dm_test_reset_plane_pre_dcn32_modeset - Test pre-DCN3.2 modesets always reset
 * @test: The KUnit test context
 */
static void dm_test_reset_plane_pre_dcn32_modeset(struct kunit *test)
{
	struct dm_test_reset_plane_ctx *ctx = dm_test_reset_plane_ctx_alloc(test);

	ctx->adev->ip_versions[DCE_HWIP][0] = IP_VERSION(3, 1, 2);
	ctx->state->allow_modeset = true;

	KUNIT_EXPECT_TRUE(test, dm_test_should_reset_plane(ctx));
}

/*
 * Add a connector of @type to the commit so should_reset_plane() walks its
 * writeback check.
 */
static struct drm_connector_state *
dm_test_reset_plane_add_connector(struct kunit *test,
				  struct dm_test_reset_plane_ctx *ctx, int type)
{
	struct drm_connector_state *conn_state;
	struct drm_connector *connector;

	connector = kunit_kzalloc(test, sizeof(*connector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, connector);
	conn_state = kunit_kzalloc(test, sizeof(*conn_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, conn_state);
	ctx->state->connectors = kunit_kzalloc(test,
					       sizeof(*ctx->state->connectors),
					       GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->state->connectors);

	connector->connector_type = type;
	ctx->state->num_connector = 1;
	ctx->state->connectors[0].ptr = connector;
	ctx->state->connectors[0].new_state = conn_state;

	return conn_state;
}

/**
 * dm_test_reset_plane_writeback_job - Test a writeback commit resets planes
 * @test: The KUnit test context
 */
static void dm_test_reset_plane_writeback_job(struct kunit *test)
{
	struct dm_test_reset_plane_ctx *ctx = dm_test_reset_plane_ctx_alloc(test);
	struct drm_connector_state *conn_state;

	conn_state = dm_test_reset_plane_add_connector(test, ctx,
						       DRM_MODE_CONNECTOR_WRITEBACK);
	conn_state->writeback_job = kunit_kzalloc(test,
						  sizeof(*conn_state->writeback_job),
						  GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, conn_state->writeback_job);

	KUNIT_EXPECT_TRUE(test, dm_test_should_reset_plane(ctx));
}

/**
 * dm_test_reset_plane_crtc_changed - Test moving a plane between CRTCs resets it
 * @test: The KUnit test context
 */
static void dm_test_reset_plane_crtc_changed(struct kunit *test)
{
	struct dm_test_reset_plane_ctx *ctx = dm_test_reset_plane_ctx_alloc(test);

	ctx->old_plane_state->crtc = NULL;

	KUNIT_EXPECT_TRUE(test, dm_test_should_reset_plane(ctx));
}

/**
 * dm_test_reset_plane_not_in_context - Test a plane outside the context is not reset
 * @test: The KUnit test context
 */
static void dm_test_reset_plane_not_in_context(struct kunit *test)
{
	struct dm_test_reset_plane_ctx *ctx = dm_test_reset_plane_ctx_alloc(test);

	ctx->old_plane_state->crtc = NULL;
	ctx->new_plane_state->crtc = NULL;

	KUNIT_EXPECT_FALSE(test, dm_test_should_reset_plane(ctx));
}

/**
 * dm_test_reset_plane_no_new_crtc_state - Test a missing new CRTC state resets the plane
 * @test: The KUnit test context
 */
static void dm_test_reset_plane_no_new_crtc_state(struct kunit *test)
{
	struct dm_test_reset_plane_ctx *ctx = dm_test_reset_plane_ctx_alloc(test);

	ctx->state->crtcs[0].new_state = NULL;

	KUNIT_EXPECT_TRUE(test, dm_test_should_reset_plane(ctx));
}

/**
 * dm_test_reset_plane_cursor_mode_changed - Test a cursor mode switch resets the plane
 * @test: The KUnit test context
 */
static void dm_test_reset_plane_cursor_mode_changed(struct kunit *test)
{
	struct dm_test_reset_plane_ctx *ctx = dm_test_reset_plane_ctx_alloc(test);

	ctx->plane->type = DRM_PLANE_TYPE_CURSOR;
	ctx->old_crtc_state->cursor_mode = DM_CURSOR_NATIVE_MODE;
	ctx->new_crtc_state->cursor_mode = DM_CURSOR_OVERLAY_MODE;

	KUNIT_EXPECT_TRUE(test, dm_test_should_reset_plane(ctx));
}

/**
 * dm_test_reset_plane_color_mgmt_changed - Test a degamma change resets the plane
 * @test: The KUnit test context
 */
static void dm_test_reset_plane_color_mgmt_changed(struct kunit *test)
{
	struct dm_test_reset_plane_ctx *ctx = dm_test_reset_plane_ctx_alloc(test);

	ctx->new_crtc_state->base.color_mgmt_changed = true;

	KUNIT_EXPECT_TRUE(test, dm_test_should_reset_plane(ctx));
}

/**
 * dm_test_reset_plane_zpos_changed - Test a z-order change resets the plane
 * @test: The KUnit test context
 */
static void dm_test_reset_plane_zpos_changed(struct kunit *test)
{
	struct dm_test_reset_plane_ctx *ctx = dm_test_reset_plane_ctx_alloc(test);

	ctx->old_plane_state->normalized_zpos = 0;
	ctx->new_plane_state->normalized_zpos = 1;

	KUNIT_EXPECT_TRUE(test, dm_test_should_reset_plane(ctx));
}

/**
 * dm_test_reset_plane_modeset - Test a CRTC modeset resets the plane
 * @test: The KUnit test context
 */
static void dm_test_reset_plane_modeset(struct kunit *test)
{
	struct dm_test_reset_plane_ctx *ctx = dm_test_reset_plane_ctx_alloc(test);

	ctx->new_crtc_state->base.mode_changed = true;

	KUNIT_EXPECT_TRUE(test, dm_test_should_reset_plane(ctx));
}

/**
 * dm_test_reset_plane_fast_update - Test an unchanged plane stays on the fast path
 * @test: The KUnit test context
 */
static void dm_test_reset_plane_fast_update(struct kunit *test)
{
	struct dm_test_reset_plane_ctx *ctx = dm_test_reset_plane_ctx_alloc(test);

	KUNIT_EXPECT_FALSE(test, dm_test_should_reset_plane(ctx));
}

/**
 * dm_test_reset_plane_skips_non_writeback - Test non-writeback connectors are skipped
 * @test: The KUnit test context
 */
static void dm_test_reset_plane_skips_non_writeback(struct kunit *test)
{
	struct dm_test_reset_plane_ctx *ctx = dm_test_reset_plane_ctx_alloc(test);

	dm_test_reset_plane_add_connector(test, ctx,
					  DRM_MODE_CONNECTOR_DisplayPort);

	KUNIT_EXPECT_FALSE(test, dm_test_should_reset_plane(ctx));
}

/*
 * Add a second plane to the commit so should_reset_plane() walks its
 * cross-plane loop. Both states start identical and bound to the same CRTC, so
 * each test only has to perturb the one field it cares about.
 */
static void dm_test_reset_plane_add_other(struct kunit *test,
					  struct dm_test_reset_plane_ctx *ctx)
{
	ctx->other = kunit_kzalloc(test, sizeof(*ctx->other), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->other);
	ctx->other_old = kunit_kzalloc(test, sizeof(*ctx->other_old), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->other_old);
	ctx->other_new = kunit_kzalloc(test, sizeof(*ctx->other_new), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->other_new);
	ctx->state->planes = kunit_kzalloc(test, sizeof(*ctx->state->planes),
					   GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->state->planes);

	ctx->other->type = DRM_PLANE_TYPE_PRIMARY;
	ctx->other_old->base.crtc = ctx->crtc;
	ctx->other_new->base.crtc = ctx->crtc;

	ctx->adev->ddev.mode_config.num_total_plane = 1;
	ctx->state->planes[0].ptr = ctx->other;
	ctx->state->planes[0].old_state = &ctx->other_old->base;
	ctx->state->planes[0].new_state = &ctx->other_new->base;
}

/**
 * dm_test_reset_plane_other_cursor_skipped - Test other cursor planes are ignored
 * @test: The KUnit test context
 */
static void dm_test_reset_plane_other_cursor_skipped(struct kunit *test)
{
	struct dm_test_reset_plane_ctx *ctx = dm_test_reset_plane_ctx_alloc(test);

	dm_test_reset_plane_add_other(test, ctx);
	ctx->other->type = DRM_PLANE_TYPE_CURSOR;
	ctx->other_new->base.rotation = DRM_MODE_ROTATE_90;

	KUNIT_EXPECT_FALSE(test, dm_test_should_reset_plane(ctx));
}

/**
 * dm_test_reset_plane_other_crtc_skipped - Test planes on other CRTCs are ignored
 * @test: The KUnit test context
 */
static void dm_test_reset_plane_other_crtc_skipped(struct kunit *test)
{
	struct dm_test_reset_plane_ctx *ctx = dm_test_reset_plane_ctx_alloc(test);

	dm_test_reset_plane_add_other(test, ctx);
	ctx->other_old->base.crtc = NULL;
	ctx->other_new->base.crtc = NULL;
	ctx->other_new->base.rotation = DRM_MODE_ROTATE_90;

	KUNIT_EXPECT_FALSE(test, dm_test_should_reset_plane(ctx));
}

/**
 * dm_test_reset_plane_other_crtc_moved - Test moving another plane forces a reset
 * @test: The KUnit test context
 */
static void dm_test_reset_plane_other_crtc_moved(struct kunit *test)
{
	struct dm_test_reset_plane_ctx *ctx = dm_test_reset_plane_ctx_alloc(test);

	dm_test_reset_plane_add_other(test, ctx);
	ctx->other_old->base.crtc = NULL;

	KUNIT_EXPECT_TRUE(test, dm_test_should_reset_plane(ctx));
}

/**
 * dm_test_reset_plane_other_scaling - Test src/dst size changes force a reset
 * @test: The KUnit test context
 */
static void dm_test_reset_plane_other_scaling(struct kunit *test)
{
	struct dm_test_reset_plane_ctx *ctx = dm_test_reset_plane_ctx_alloc(test);

	dm_test_reset_plane_add_other(test, ctx);
	ctx->other_new->base.src_w = 1 << 16;

	KUNIT_EXPECT_TRUE(test, dm_test_should_reset_plane(ctx));
}

/**
 * dm_test_reset_plane_other_rotation - Test rotation changes force a reset
 * @test: The KUnit test context
 */
static void dm_test_reset_plane_other_rotation(struct kunit *test)
{
	struct dm_test_reset_plane_ctx *ctx = dm_test_reset_plane_ctx_alloc(test);

	dm_test_reset_plane_add_other(test, ctx);
	ctx->other_new->base.rotation = DRM_MODE_ROTATE_180;

	KUNIT_EXPECT_TRUE(test, dm_test_should_reset_plane(ctx));
}

/**
 * dm_test_reset_plane_other_blending - Test blend mode changes force a reset
 * @test: The KUnit test context
 */
static void dm_test_reset_plane_other_blending(struct kunit *test)
{
	struct dm_test_reset_plane_ctx *ctx = dm_test_reset_plane_ctx_alloc(test);

	dm_test_reset_plane_add_other(test, ctx);
	ctx->other_new->base.pixel_blend_mode = DRM_MODE_BLEND_COVERAGE;

	KUNIT_EXPECT_TRUE(test, dm_test_should_reset_plane(ctx));
}

/**
 * dm_test_reset_plane_other_alpha - Test alpha changes force a reset
 * @test: The KUnit test context
 */
static void dm_test_reset_plane_other_alpha(struct kunit *test)
{
	struct dm_test_reset_plane_ctx *ctx = dm_test_reset_plane_ctx_alloc(test);

	dm_test_reset_plane_add_other(test, ctx);
	ctx->other_new->base.alpha = DRM_BLEND_ALPHA_OPAQUE;
	ctx->other_old->base.alpha = DRM_BLEND_ALPHA_OPAQUE / 2;

	KUNIT_EXPECT_TRUE(test, dm_test_should_reset_plane(ctx));
}

/**
 * dm_test_reset_plane_other_colorspace - Test colorspace changes force a reset
 * @test: The KUnit test context
 */
static void dm_test_reset_plane_other_colorspace(struct kunit *test)
{
	struct dm_test_reset_plane_ctx *ctx = dm_test_reset_plane_ctx_alloc(test);

	dm_test_reset_plane_add_other(test, ctx);
	ctx->other_new->base.color_encoding = DRM_COLOR_YCBCR_BT2020;

	KUNIT_EXPECT_TRUE(test, dm_test_should_reset_plane(ctx));
}

/**
 * dm_test_reset_plane_other_hdr_mult - Test transfer function changes force a reset
 * @test: The KUnit test context
 */
static void dm_test_reset_plane_other_hdr_mult(struct kunit *test)
{
	struct dm_test_reset_plane_ctx *ctx = dm_test_reset_plane_ctx_alloc(test);

	dm_test_reset_plane_add_other(test, ctx);
	ctx->other_new->hdr_mult = 1;

	KUNIT_EXPECT_TRUE(test, dm_test_should_reset_plane(ctx));
}

/**
 * dm_test_reset_plane_other_no_fb - Test planes without framebuffers are skipped
 * @test: The KUnit test context
 */
static void dm_test_reset_plane_other_no_fb(struct kunit *test)
{
	struct dm_test_reset_plane_ctx *ctx = dm_test_reset_plane_ctx_alloc(test);

	dm_test_reset_plane_add_other(test, ctx);
	ctx->other_new->base.fb = kunit_kzalloc(test,
						sizeof(*ctx->other_new->base.fb),
						GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->other_new->base.fb);

	KUNIT_EXPECT_FALSE(test, dm_test_should_reset_plane(ctx));
}

/*
 * Give the second plane a matching pair of framebuffers. should_reset_plane()
 * casts them to amdgpu_framebuffer, so both are allocated as such.
 */
static void dm_test_reset_plane_add_other_fbs(struct kunit *test,
					      struct dm_test_reset_plane_ctx *ctx)
{
	struct amdgpu_framebuffer *old_afb, *new_afb;
	struct drm_format_info *format;

	format = kunit_kzalloc(test, sizeof(*format), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, format);
	old_afb = kunit_kzalloc(test, sizeof(*old_afb), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, old_afb);
	new_afb = kunit_kzalloc(test, sizeof(*new_afb), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, new_afb);

	old_afb->base.format = format;
	new_afb->base.format = format;
	ctx->other_old->base.fb = &old_afb->base;
	ctx->other_new->base.fb = &new_afb->base;
}

/**
 * dm_test_reset_plane_other_format - Test pixel format changes force a reset
 * @test: The KUnit test context
 */
static void dm_test_reset_plane_other_format(struct kunit *test)
{
	struct dm_test_reset_plane_ctx *ctx = dm_test_reset_plane_ctx_alloc(test);
	struct drm_format_info *new_format;

	dm_test_reset_plane_add_other(test, ctx);
	dm_test_reset_plane_add_other_fbs(test, ctx);
	new_format = kunit_kzalloc(test, sizeof(*new_format), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, new_format);
	ctx->other_new->base.fb->format = new_format;

	KUNIT_EXPECT_TRUE(test, dm_test_should_reset_plane(ctx));
}

/**
 * dm_test_reset_plane_other_modifier - Test tiling/DCC changes force a reset
 * @test: The KUnit test context
 */
static void dm_test_reset_plane_other_modifier(struct kunit *test)
{
	struct dm_test_reset_plane_ctx *ctx = dm_test_reset_plane_ctx_alloc(test);

	dm_test_reset_plane_add_other(test, ctx);
	dm_test_reset_plane_add_other_fbs(test, ctx);
	ctx->other_new->base.fb->modifier = 1;

	KUNIT_EXPECT_TRUE(test, dm_test_should_reset_plane(ctx));
}

/* Tests for amdgpu_dm_dump_links_and_sinks() */

/**
 * dm_test_dump_links_no_dc - Test a device without DC dumps nothing
 * @test: The KUnit test context
 */
static void dm_test_dump_links_no_dc(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);

	amdgpu_dm_dump_links_and_sinks(adev);
}

/**
 * dm_test_dump_links_no_links - Test a DC without links dumps nothing
 * @test: The KUnit test context
 */
static void dm_test_dump_links_no_links(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);

	adev->dm.dc = dm_kunit_alloc_dc_with_ctx(test);

	amdgpu_dm_dump_links_and_sinks(adev);
}

/*
 * Attach a single DC link to @adev so amdgpu_dm_dump_links_and_sinks() walks it.
 */
static struct dc_link *dm_test_dc_with_link(struct kunit *test,
					   struct amdgpu_device *adev)
{
	struct dc *dc = dm_kunit_alloc_dc_with_ctx(test);
	struct dc_link *link = dm_kunit_alloc_link(test);

	dc->links[0] = link;
	dc->link_count = 1;
	adev->dm.dc = dc;

	return link;
}

/**
 * dm_test_dump_links_with_sinks - Test local and remote sinks are walked
 * @test: The KUnit test context
 */
static void dm_test_dump_links_with_sinks(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct dc_link *link = dm_test_dc_with_link(test, adev);
	struct dc_sink *local_sink, *remote_sink;

	local_sink = kunit_kzalloc(test, sizeof(*local_sink), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, local_sink);
	remote_sink = kunit_kzalloc(test, sizeof(*remote_sink), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, remote_sink);

	strscpy(local_sink->edid_caps.display_name, "local");
	strscpy(remote_sink->edid_caps.display_name, "remote");
	link->local_sink = local_sink;
	link->sink_count = 1;
	link->remote_sinks[0] = remote_sink;
	adev->dm.dc->link_count = 2;

	amdgpu_dm_dump_links_and_sinks(adev);
}

/**
 * dm_test_dump_links_unnamed_sinks - Test links without a sink or EDID name
 * @test: The KUnit test context
 */
static void dm_test_dump_links_unnamed_sinks(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct dc_link *link = dm_test_dc_with_link(test, adev);
	struct dc_sink *remote_sink;

	remote_sink = kunit_kzalloc(test, sizeof(*remote_sink), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, remote_sink);

	link->sink_count = 2;
	link->remote_sinks[0] = NULL;
	link->remote_sinks[1] = remote_sink;

	amdgpu_dm_dump_links_and_sinks(adev);
}

/**
 * dm_test_update_hdcp_no_workqueue - Test HDCP update is skipped without a workqueue
 * @test: The KUnit test context
 */
static void dm_test_update_hdcp_no_workqueue(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);

	amdgpu_dm_update_hdcp(dm_test_alloc_commit(test, adev));
}

/**
 * dm_test_atomic_setup_commit_empty - Test an empty commit needs no color setup
 * @test: The KUnit test context
 */
static void dm_test_atomic_setup_commit_empty(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);

	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_atomic_setup_commit(dm_test_alloc_commit(test, adev)),
			0);
}

/**
 * dm_test_atomic_check_empty - Test an empty atomic commit needs no DC validation
 * @test: The KUnit test context
 */
static void dm_test_atomic_check_empty(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct drm_atomic_commit *state = dm_test_alloc_commit(test, adev);

	adev->dm.dc = dm_kunit_alloc_dc_with_ctx(test);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_atomic_check(&adev->ddev, state), 0);
	KUNIT_EXPECT_EQ(test, state->num_private_objs, 0U);
}

/*
 * A commit with one connector of @type bound to a CRTC that keeps its stream.
 * The content protection state is unchanged, so amdgpu_dm_update_hdcp() walks
 * the connector without reaching hdcp_update_display() or hdcp_reset_display(),
 * which are the only users of the HDCP workqueue contents.
 */
static struct drm_atomic_commit *dm_test_hdcp_commit(struct kunit *test,
						     struct amdgpu_device *adev,
						     int type)
{
	struct drm_atomic_commit *state = dm_test_alloc_commit(test, adev);
	struct dm_crtc_state *old_crtc_state, *new_crtc_state;
	struct dm_connector_state *old_dm, *new_dm;
	struct amdgpu_dm_connector *aconnector;
	struct amdgpu_crtc *acrtc;
	struct dc_sink *sink;

	adev->dm.hdcp_workqueue = kunit_kzalloc(test,
						sizeof(*adev->dm.hdcp_workqueue),
						GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, adev->dm.hdcp_workqueue);
	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);
	acrtc = kunit_kzalloc(test, sizeof(*acrtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, acrtc);
	old_dm = kunit_kzalloc(test, sizeof(*old_dm), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, old_dm);
	new_dm = kunit_kzalloc(test, sizeof(*new_dm), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, new_dm);
	old_crtc_state = kunit_kzalloc(test, sizeof(*old_crtc_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, old_crtc_state);
	new_crtc_state = kunit_kzalloc(test, sizeof(*new_crtc_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, new_crtc_state);
	sink = kunit_kzalloc(test, sizeof(*sink), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, sink);
	state->connectors = kunit_kzalloc(test, sizeof(*state->connectors), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state->connectors);
	state->crtcs = kunit_kzalloc(test, sizeof(*state->crtcs), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state->crtcs);

	strscpy(sink->edid_caps.display_name, "panel");
	sink->sink_signal = SIGNAL_TYPE_DISPLAY_PORT;
	aconnector->dc_sink = sink;
	aconnector->base.connector_type = type;
	aconnector->base.dpms = DRM_MODE_DPMS_ON;
	aconnector->base.state = &new_dm->base;
	new_dm->base.crtc = &acrtc->base;
	new_crtc_state->stream = dm_kunit_alloc_stream(test, NULL);

	state->num_connector = 1;
	state->connectors[0].ptr = &aconnector->base;
	state->connectors[0].old_state = &old_dm->base;
	state->connectors[0].new_state = &new_dm->base;
	state->crtcs[0].ptr = &acrtc->base;
	state->crtcs[0].old_state = &old_crtc_state->base;
	state->crtcs[0].new_state = &new_crtc_state->base;

	return state;
}

/**
 * dm_test_update_hdcp_writeback_skipped - Test writeback connectors are skipped
 * @test: The KUnit test context
 */
static void dm_test_update_hdcp_writeback_skipped(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);

	amdgpu_dm_update_hdcp(dm_test_hdcp_commit(test, adev,
						  DRM_MODE_CONNECTOR_WRITEBACK));
}

/**
 * dm_test_update_hdcp_unchanged - Test an unchanged connector needs no HDCP update
 * @test: The KUnit test context
 */
static void dm_test_update_hdcp_unchanged(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);

	amdgpu_dm_update_hdcp(dm_test_hdcp_commit(test, adev,
						  DRM_MODE_CONNECTOR_DisplayPort));
}

/*
 * Run do_aquire_global_lock() with a fresh acquire context, releasing the locks
 * it leaves held on @state->acquire_ctx.
 */
static int dm_test_run_global_lock(struct kunit *test,
				   struct amdgpu_device *adev)
{
	struct drm_atomic_commit *state = dm_test_alloc_commit(test, adev);
	struct drm_modeset_acquire_ctx acquire_ctx;
	int ret;

	drm_modeset_acquire_init(&acquire_ctx, 0);
	state->acquire_ctx = &acquire_ctx;

	ret = do_aquire_global_lock(&adev->ddev, state);

	drm_modeset_drop_locks(&acquire_ctx);
	drm_modeset_acquire_fini(&acquire_ctx);

	return ret;
}

/**
 * dm_test_aquire_global_lock_no_crtc - Test the global lock is taken without CRTCs
 * @test: The KUnit test context
 */
static void dm_test_aquire_global_lock_no_crtc(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);

	KUNIT_EXPECT_EQ(test, dm_test_run_global_lock(test, adev), 0);
}

/*
 * A CRTC registered on @adev, needed to walk the per-CRTC commit loop.
 */
static struct drm_crtc *dm_test_alloc_crtc(struct kunit *test,
					   struct amdgpu_device *adev)
{
	struct drm_plane *primary;
	struct drm_crtc *crtc;

	primary = drm_kunit_helper_create_primary_plane(test, &adev->ddev, NULL,
							NULL, NULL, 0, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, primary);
	crtc = drm_kunit_helper_create_crtc(test, &adev->ddev, primary, NULL,
					    NULL, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, crtc);

	return crtc;
}

/**
 * dm_test_aquire_global_lock_no_commit - Test a CRTC without a pending commit
 * @test: The KUnit test context
 */
static void dm_test_aquire_global_lock_no_commit(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);

	dm_test_alloc_crtc(test, adev);

	KUNIT_EXPECT_EQ(test, dm_test_run_global_lock(test, adev), 0);
}

/**
 * dm_test_aquire_global_lock_waits_commit - Test a completed commit is waited on
 * @test: The KUnit test context
 */
static void dm_test_aquire_global_lock_waits_commit(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct drm_crtc_commit *commit;
	struct drm_crtc *crtc;
	int ret;

	crtc = dm_test_alloc_crtc(test, adev);
	commit = kunit_kzalloc(test, sizeof(*commit), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, commit);

	/*
	 * The extra reference taken by the loop is dropped again, so the commit
	 * never reaches zero and stays KUnit-managed.
	 */
	kref_init(&commit->ref);
	init_completion(&commit->hw_done);
	init_completion(&commit->flip_done);
	complete_all(&commit->hw_done);
	complete_all(&commit->flip_done);
	list_add_tail(&commit->commit_entry, &crtc->commit_list);

	ret = dm_test_run_global_lock(test, adev);

	list_del(&commit->commit_entry);

	KUNIT_EXPECT_EQ(test, ret, 0);
}

/**
 * dm_test_update_crtc_state_unchanged - Test unchanged state needs no validation
 * @test: The KUnit test context
 */
static void dm_test_update_crtc_state_unchanged(struct kunit *test)
{
	struct dm_test_reset_plane_ctx *ctx = dm_test_reset_plane_ctx_alloc(test);
	bool lock_and_validation_needed = false;

	ctx->adev->dm.adev = ctx->adev;
	KUNIT_EXPECT_EQ(test,
			dm_update_crtc_state(&ctx->adev->dm, ctx->state, ctx->crtc,
					     &ctx->old_crtc_state->base,
					     &ctx->new_crtc_state->base, true,
					     &lock_and_validation_needed),
			0);
	KUNIT_EXPECT_FALSE(test, lock_and_validation_needed);
	KUNIT_EXPECT_NULL(test, ctx->new_crtc_state->stream);
}

/**
 * dm_test_update_plane_state_detached - Test a detached plane needs no DC update
 * @test: The KUnit test context
 */
static void dm_test_update_plane_state_detached(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct dm_plane_state *old_plane_state;
	struct dm_plane_state *new_plane_state;
	struct drm_plane *plane;
	bool lock_and_validation_needed = false;
	bool is_top_most_overlay = true;

	old_plane_state = kunit_kzalloc(test, sizeof(*old_plane_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, old_plane_state);
	new_plane_state = kunit_kzalloc(test, sizeof(*new_plane_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, new_plane_state);
	plane = kunit_kzalloc(test, sizeof(*plane), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, plane);
	adev->reset_domain = kunit_kzalloc(test, sizeof(*adev->reset_domain), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, adev->reset_domain);

	adev->ip_versions[DCE_HWIP][0] = IP_VERSION(3, 2, 0);
	plane->dev = &adev->ddev;
	plane->type = DRM_PLANE_TYPE_PRIMARY;

	KUNIT_EXPECT_EQ(test,
			dm_update_plane_state(NULL, dm_test_alloc_commit(test, adev), plane,
					      &old_plane_state->base,
					      &new_plane_state->base, false,
					      &lock_and_validation_needed,
					      &is_top_most_overlay),
			0);
	KUNIT_EXPECT_FALSE(test, lock_and_validation_needed);
	KUNIT_EXPECT_TRUE(test, is_top_most_overlay);
	KUNIT_EXPECT_NULL(test, new_plane_state->dc_state);
}

/**
 * dm_test_mod_power_update_streams_empty - Test an empty commit updates no streams
 * @test: The KUnit test context
 */
static void dm_test_mod_power_update_streams_empty(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);

	amdgpu_dm_mod_power_update_streams(dm_test_alloc_commit(test, adev),
					   &adev->dm);
}

/**
 * dm_test_mod_power_setup_streams_empty - Test an empty commit sets up no streams
 * @test: The KUnit test context
 */
static void dm_test_mod_power_setup_streams_empty(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);

	amdgpu_dm_mod_power_setup_streams(dm_test_alloc_commit(test, adev),
					  &adev->dm);
}

/* Tests for amdgpu_dm_trigger_timing_sync() */

/**
 * dm_test_trigger_timing_sync_no_state - Test no DC state leaves the sync untouched
 * @test: The KUnit test context
 */
static void dm_test_trigger_timing_sync_no_state(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);

	adev->dm.dc = dm_kunit_alloc_dc_with_ctx(test);
	mutex_init(&adev->dm.dc_lock);

	amdgpu_dm_trigger_timing_sync(&adev->ddev);
}

/**
 * dm_test_trigger_timing_sync_streams - Test the force flag reaches every stream
 * @test: The KUnit test context
 */
static void dm_test_trigger_timing_sync_streams(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct dc *dc = dm_kunit_alloc_dc_with_ctx(test);
	struct dc_stream_state *stream;
	struct dc_state *context;

	context = dm_kunit_alloc_dc_state(test);
	KUNIT_ASSERT_NOT_NULL(test, context);
	stream = dm_kunit_alloc_stream(test, NULL);

	context->streams[0] = stream;
	context->stream_count = 1;
	dc->current_state = context;
	adev->dm.dc = dc;
	adev->dm.force_timing_sync = true;
	mutex_init(&adev->dm.dc_lock);

	amdgpu_dm_trigger_timing_sync(&adev->ddev);

	KUNIT_EXPECT_TRUE(test, stream->triggered_crtc_reset.enabled);
}

/**
 * dm_test_acpi_phy_transition_interlock - Test the PHY transition interlock stub
 * @test: The KUnit test context
 */
static void dm_test_acpi_phy_transition_interlock(struct kunit *test)
{
	struct dm_process_phy_transition_init_params params = { 0 };

	dm_acpi_process_phy_transition_interlock(NULL, params);
}

/**
 * dm_test_early_fini_audio_disabled - Test early fini with audio never enabled
 * @test: The KUnit test context
 */
static void dm_test_early_fini_audio_disabled(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct amdgpu_ip_block ip_block = { .adev = adev };

	KUNIT_EXPECT_EQ(test, amdgpu_dm_early_fini(&ip_block), 0);
}

/**
 * dm_test_sw_fini_releases_state - Test sw fini drops the DMUB software state
 * @test: The KUnit test context
 */
static void dm_test_sw_fini_releases_state(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct amdgpu_ip_block ip_block = { .adev = adev };

	INIT_LIST_HEAD(&adev->dm.da_list);
	adev->dm.dmub_fb_info = kzalloc_obj(*adev->dm.dmub_fb_info);
	KUNIT_ASSERT_NOT_NULL(test, adev->dm.dmub_fb_info);

	KUNIT_EXPECT_EQ(test, dm_sw_fini(&ip_block), 0);
	KUNIT_EXPECT_NULL(test, adev->dm.dmub_fb_info);
}

/**
 * dm_test_oem_i2c_hw_init_no_device - Test no OEM I2C device leaves the bus unset
 * @test: The KUnit test context
 */
static void dm_test_oem_i2c_hw_init_no_device(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct dc *dc = dm_kunit_alloc_dc_with_ctx(test);

	dc->res_pool = kunit_kzalloc(test, sizeof(*dc->res_pool), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dc->res_pool);
	adev->dm.dc = dc;

	KUNIT_EXPECT_EQ(test, dm_oem_i2c_hw_init(adev), 0);
	KUNIT_EXPECT_NULL(test, adev->dm.oem_i2c);
}

/**
 * dm_test_resume_mst_no_primary - Test a missing primary branch releases the topology lock
 * @test: The KUnit test context
 */
static void dm_test_resume_mst_no_primary(struct kunit *test)
{
	struct drm_dp_mst_topology_mgr *mgr;

	mgr = kunit_kzalloc(test, sizeof(*mgr), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, mgr);
	mutex_init(&mgr->lock);

	resume_mst_branch_status(mgr);

	KUNIT_ASSERT_TRUE(test, mutex_trylock(&mgr->lock));
	mutex_unlock(&mgr->lock);
}

/**
 * dm_test_s3_handle_mst_empty - Test empty connector lists need no MST action
 * @test: The KUnit test context
 */
static void dm_test_s3_handle_mst_empty(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);

	s3_handle_mst(&adev->ddev, true);

	KUNIT_EXPECT_TRUE(test, list_empty(&adev->ddev.mode_config.connector_list));
}

/**
 * dm_test_gpureset_commit_state_no_streams - Test an empty DC state programs nothing
 * @test: The KUnit test context
 */
static void dm_test_gpureset_commit_state_no_streams(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct dc_state *context = dm_kunit_alloc_dc_state(test);

	KUNIT_ASSERT_NOT_NULL(test, context);
	adev->dm.ddev = &adev->ddev;

	dm_gpureset_commit_state(context, &adev->dm);
}

/**
 * dm_test_emulated_link_detect_bad_signal - Test an unknown connector signal is rejected
 * @test: The KUnit test context
 */
static void dm_test_emulated_link_detect_bad_signal(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct dc_link *link = dm_kunit_alloc_link_with_ctx(test);

	link->ctx->driver_context = adev;
	link->type = dc_connection_single;
	link->connector_signal = SIGNAL_TYPE_VIRTUAL;

	amdgpu_dm_emulated_link_detect(link);

	KUNIT_EXPECT_EQ(test, (int)link->type, (int)dc_connection_none);
	KUNIT_EXPECT_NULL(test, link->local_sink);
}

/* Tests for the mod_power modeset helpers */

struct dm_test_modeset_ctx {
	struct amdgpu_device *adev;
	struct drm_atomic_commit *state;
	struct amdgpu_crtc *acrtc;
	struct dm_crtc_state *old_crtc_state;
	struct dm_crtc_state *new_crtc_state;
};

/*
 * A modeset commit on one CRTC with an old and a new stream. dm->power_module
 * stays NULL, which every mod_power entry point treats as a no-op, so the DM
 * side of the modeset can be walked without a live power module.
 */
static struct dm_test_modeset_ctx *dm_test_modeset_ctx_alloc(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;
	struct dm_test_modeset_ctx *ctx;
	struct dc_link *link;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	ctx->adev = dm_kunit_alloc_adev(test);
	ctx->state = dm_test_alloc_commit(test, ctx->adev);
	ctx->acrtc = kunit_kzalloc(test, sizeof(*ctx->acrtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->acrtc);
	ctx->old_crtc_state = kunit_kzalloc(test, sizeof(*ctx->old_crtc_state),
					    GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->old_crtc_state);
	ctx->new_crtc_state = kunit_kzalloc(test, sizeof(*ctx->new_crtc_state),
					    GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->new_crtc_state);
	ctx->state->crtcs = kunit_kzalloc(test, sizeof(*ctx->state->crtcs),
					  GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->state->crtcs);
	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);

	link = dm_kunit_alloc_link(test);
	ctx->old_crtc_state->stream = dm_kunit_alloc_stream(test, link);
	ctx->new_crtc_state->stream = dm_kunit_alloc_stream(test, link);
	ctx->new_crtc_state->stream->dm_stream_context = aconnector;

	ctx->adev->ddev.mode_config.num_crtc = 1;
	ctx->adev->dm.dc = dm_kunit_alloc_dc_with_ctx(test);
	mutex_init(&ctx->adev->dm.dc_lock);
	ctx->new_crtc_state->base.mode_changed = true;
	ctx->new_crtc_state->base.state = ctx->state;
	ctx->state->crtcs[0].ptr = &ctx->acrtc->base;
	ctx->state->crtcs[0].old_state = &ctx->old_crtc_state->base;
	ctx->state->crtcs[0].new_state = &ctx->new_crtc_state->base;

	return ctx;
}

/**
 * dm_test_mod_power_update_streams_no_modeset - Test fast updates are skipped
 * @test: The KUnit test context
 */
static void dm_test_mod_power_update_streams_no_modeset(struct kunit *test)
{
	struct dm_test_modeset_ctx *ctx = dm_test_modeset_ctx_alloc(test);

	ctx->new_crtc_state->base.mode_changed = false;
	ctx->new_crtc_state->base.active = true;

	amdgpu_dm_mod_power_update_streams(ctx->state, &ctx->adev->dm);
}

/**
 * dm_test_mod_power_update_streams_enable - Test enabling a CRTC adds the stream
 * @test: The KUnit test context
 */
static void dm_test_mod_power_update_streams_enable(struct kunit *test)
{
	struct dm_test_modeset_ctx *ctx = dm_test_modeset_ctx_alloc(test);

	ctx->new_crtc_state->base.active = true;

	amdgpu_dm_mod_power_update_streams(ctx->state, &ctx->adev->dm);
}

/**
 * dm_test_mod_power_update_streams_replace - Test a re-modeset replaces the stream
 * @test: The KUnit test context
 */
static void dm_test_mod_power_update_streams_replace(struct kunit *test)
{
	struct dm_test_modeset_ctx *ctx = dm_test_modeset_ctx_alloc(test);

	ctx->old_crtc_state->base.active = true;
	ctx->new_crtc_state->base.active = true;

	amdgpu_dm_mod_power_update_streams(ctx->state, &ctx->adev->dm);
}

/**
 * dm_test_mod_power_update_streams_disable - Test disabling a CRTC removes the stream
 * @test: The KUnit test context
 */
static void dm_test_mod_power_update_streams_disable(struct kunit *test)
{
	struct dm_test_modeset_ctx *ctx = dm_test_modeset_ctx_alloc(test);

	ctx->old_crtc_state->base.active = true;

	amdgpu_dm_mod_power_update_streams(ctx->state, &ctx->adev->dm);
}

/**
 * dm_test_mod_power_setup_streams_modeset - Test a modeset sets up the new stream
 * @test: The KUnit test context
 */
static void dm_test_mod_power_setup_streams_modeset(struct kunit *test)
{
	struct dm_test_modeset_ctx *ctx = dm_test_modeset_ctx_alloc(test);

	ctx->new_crtc_state->base.active = true;

	amdgpu_dm_mod_power_setup_streams(ctx->state, &ctx->adev->dm);
}

/**
 * dm_test_mod_power_setup_streams_no_modeset - Test fast updates set up no streams
 * @test: The KUnit test context
 */
static void dm_test_mod_power_setup_streams_no_modeset(struct kunit *test)
{
	struct dm_test_modeset_ctx *ctx = dm_test_modeset_ctx_alloc(test);

	ctx->new_crtc_state->base.mode_changed = false;
	ctx->new_crtc_state->base.active = true;

	amdgpu_dm_mod_power_setup_streams(ctx->state, &ctx->adev->dm);
}

/**
 * dm_test_atomic_setup_commit_color_mgmt - Test color management is reprogrammed
 * @test: The KUnit test context
 */
static void dm_test_atomic_setup_commit_color_mgmt(struct kunit *test)
{
	struct dm_test_modeset_ctx *ctx = dm_test_modeset_ctx_alloc(test);

	ctx->new_crtc_state->base.active = true;
	ctx->new_crtc_state->base.color_mgmt_changed = true;

	KUNIT_EXPECT_EQ(test, amdgpu_dm_atomic_setup_commit(ctx->state), 0);
}

/**
 * dm_test_atomic_setup_commit_modeset - Test a modeset alone reprograms color state
 * @test: The KUnit test context
 */
static void dm_test_atomic_setup_commit_modeset(struct kunit *test)
{
	struct dm_test_modeset_ctx *ctx = dm_test_modeset_ctx_alloc(test);

	ctx->new_crtc_state->base.active = true;

	KUNIT_EXPECT_EQ(test, amdgpu_dm_atomic_setup_commit(ctx->state), 0);
}

/**
 * dm_test_atomic_setup_commit_bad_lut - Test an invalid degamma LUT is rejected
 * @test: The KUnit test context
 */
static void dm_test_atomic_setup_commit_bad_lut(struct kunit *test)
{
	struct dm_test_modeset_ctx *ctx = dm_test_modeset_ctx_alloc(test);
	struct drm_property_blob *blob;
	struct drm_color_lut *lut;

	blob = kunit_kzalloc(test, sizeof(*blob), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, blob);
	lut = kunit_kzalloc(test, sizeof(*lut), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, lut);

	blob->length = sizeof(*lut);
	blob->data = lut;
	ctx->new_crtc_state->base.active = true;
	ctx->new_crtc_state->base.degamma_lut = blob;

	KUNIT_EXPECT_EQ(test, amdgpu_dm_atomic_setup_commit(ctx->state), -EINVAL);
}

/* Tests for mmhub_read_system_context() */

#define DM_TEST_PD_ADDR		0x1234000ULL

/*
 * Stub for the page directory address read: amdgpu_gmc_pd_addr() walks a live
 * TTM buffer object back to its device, so return a fixed address instead.
 */
static uint64_t dm_test_gmc_pd_addr(struct amdgpu_bo *bo)
{
	return DM_TEST_PD_ADDR;
}

static const struct amdgpu_dm_kunit_ops dm_test_dm_ops = {
	.gmc_pd_addr = dm_test_gmc_pd_addr,
};

/*
 * A device whose AGP aperture is disabled (bot above top), so the frame buffer
 * alone decides the logical address range.
 */
static struct amdgpu_device *dm_test_mmhub_adev(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);

	dm_test_install_dm_ops(test, &dm_test_dm_ops);

	adev->gmc.agp_start = 0x2000000;
	adev->gmc.agp_end = 0x1000000;
	adev->gmc.fb_start = 0x40000000;
	adev->gmc.fb_end = 0x7fffffff;
	adev->gmc.gart_start = 0x100000000ULL;
	adev->gmc.gart_end = 0x1ffffffffULL;
	adev->vm_manager.vram_base_offset = 0x800000;

	return adev;
}

/**
 * dm_test_mmhub_agp_disabled - Test a disabled AGP aperture uses the frame buffer
 * @test: The KUnit test context
 */
static void dm_test_mmhub_agp_disabled(struct kunit *test)
{
	struct amdgpu_device *adev = dm_test_mmhub_adev(test);
	struct dc_phy_addr_space_config pa_config;

	mmhub_read_system_context(adev, &pa_config);

	KUNIT_EXPECT_EQ(test, pa_config.system_aperture.start_addr, 0x40000000ULL);
	KUNIT_EXPECT_EQ(test, pa_config.system_aperture.end_addr, 0x7ffc0000ULL);
	KUNIT_EXPECT_EQ(test, pa_config.system_aperture.agp_base, 0ULL);
	KUNIT_EXPECT_EQ(test, pa_config.system_aperture.agp_bot, 0x2000000ULL);
	KUNIT_EXPECT_EQ(test, pa_config.system_aperture.agp_top, 0x1000000ULL);
	KUNIT_EXPECT_EQ(test, pa_config.system_aperture.fb_base, 0x40000000ULL);
	KUNIT_EXPECT_EQ(test, pa_config.system_aperture.fb_offset, 0x800000ULL);
	KUNIT_EXPECT_EQ(test, pa_config.system_aperture.fb_top, 0x7fffffffULL);
	KUNIT_EXPECT_EQ(test, pa_config.gart_config.page_table_start_addr, 0x100000000ULL);
	KUNIT_EXPECT_EQ(test, pa_config.gart_config.page_table_end_addr, 0x1fffff000ULL);
	KUNIT_EXPECT_EQ(test, pa_config.gart_config.page_table_base_addr, DM_TEST_PD_ADDR);
	KUNIT_EXPECT_FALSE(test, pa_config.is_hvm_enabled);
}

/**
 * dm_test_mmhub_agp_disabled_raven2 - Test the Raven2 aperture workaround
 * @test: The KUnit test context
 */
static void dm_test_mmhub_agp_disabled_raven2(struct kunit *test)
{
	struct amdgpu_device *adev = dm_test_mmhub_adev(test);
	struct dc_phy_addr_space_config pa_config;

	adev->apu_flags = AMD_APU_IS_RAVEN2;

	mmhub_read_system_context(adev, &pa_config);

	KUNIT_EXPECT_EQ(test, pa_config.system_aperture.end_addr, 0x80000000ULL);
}

/**
 * dm_test_mmhub_agp_enabled - Test an enabled AGP aperture widens the range
 * @test: The KUnit test context
 */
static void dm_test_mmhub_agp_enabled(struct kunit *test)
{
	struct amdgpu_device *adev = dm_test_mmhub_adev(test);
	struct dc_phy_addr_space_config pa_config;

	adev->gmc.agp_start = 0x1000000;
	adev->gmc.agp_end = 0x2000000;
	adev->mode_info.gpu_vm_support = true;

	mmhub_read_system_context(adev, &pa_config);

	KUNIT_EXPECT_EQ(test, pa_config.system_aperture.start_addr, 0x1000000ULL);
	KUNIT_EXPECT_EQ(test, pa_config.system_aperture.end_addr, 0x7ffc0000ULL);
	KUNIT_EXPECT_TRUE(test, pa_config.is_hvm_enabled);
}

/**
 * dm_test_mmhub_agp_enabled_renoir - Test the Renoir aperture workaround
 * @test: The KUnit test context
 */
static void dm_test_mmhub_agp_enabled_renoir(struct kunit *test)
{
	struct amdgpu_device *adev = dm_test_mmhub_adev(test);
	struct dc_phy_addr_space_config pa_config;

	adev->gmc.agp_start = 0x1000000;
	adev->gmc.agp_end = 0x2000000;
	adev->apu_flags = AMD_APU_IS_RENOIR;

	mmhub_read_system_context(adev, &pa_config);

	KUNIT_EXPECT_EQ(test, pa_config.system_aperture.end_addr, 0x80000000ULL);
}

/* Tests for amdgpu_dm_init_power_module() */

/**
 * dm_test_init_power_module_no_edp - Test no eDP skips the power module
 * @test: The KUnit test context
 */
static void dm_test_init_power_module_no_edp(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);

	adev->dm.ddev = &adev->ddev;
	adev->dm.num_of_edps = 0;

	KUNIT_EXPECT_EQ(test, amdgpu_dm_init_power_module(&adev->dm), 0);
	KUNIT_EXPECT_NULL(test, adev->dm.power_module);
}

/**
 * dm_test_init_power_module_alloc_failure - Test a failed power module create
 * @test: The KUnit test context
 *
 * mod_power_create() rejects a NULL DC, which walks the full parameter setup
 * loop and then reports the allocation failure.
 */
static void dm_test_init_power_module_alloc_failure(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);

	adev->dm.ddev = &adev->ddev;
	adev->dm.num_of_edps = 1;
	adev->dm.backlight_caps[0].min_input_signal = 0x10;
	adev->dm.backlight_caps[0].max_input_signal = 0xff;

	KUNIT_EXPECT_EQ(test, amdgpu_dm_init_power_module(&adev->dm), -ENOMEM);
	KUNIT_EXPECT_NULL(test, adev->dm.power_module);
}

/* Tests for fill_dc_plane_info_and_addr() */

struct dm_test_plane_info_ctx {
	struct amdgpu_device *adev;
	struct amdgpu_framebuffer *afb;
	struct drm_plane *plane;
	struct dm_plane_state *dm_plane_state;
	struct drm_plane_state *plane_state;
	struct dc_plane_info plane_info;
	struct dc_plane_address address;
};

/*
 * A linear GFX9 framebuffer of @drm_format bound to an unrotated plane, which
 * is the simplest input that lets the buffer attribute helper succeed. @adev is
 * passed in because a test may only allocate one mock DRM device.
 */
static struct dm_test_plane_info_ctx *
dm_test_plane_info_ctx_alloc(struct kunit *test, struct amdgpu_device *adev,
			     u32 drm_format)
{
	struct dm_test_plane_info_ctx *ctx;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	ctx->adev = adev;
	ctx->afb = kunit_kzalloc(test, sizeof(*ctx->afb), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->afb);
	ctx->plane = kunit_kzalloc(test, sizeof(*ctx->plane), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->plane);
	ctx->dm_plane_state = kunit_kzalloc(test, sizeof(*ctx->dm_plane_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->dm_plane_state);
	ctx->plane_state = &ctx->dm_plane_state->base;

	ctx->adev->family = AMDGPU_FAMILY_NV;
	ctx->adev->ip_versions[GC_HWIP][0] = IP_VERSION(10, 3, 0);

	ctx->afb->address = 0x80000000ULL;
	ctx->afb->base.width = 1920;
	ctx->afb->base.height = 1080;
	ctx->afb->base.offsets[1] = 0x200000;
	ctx->afb->base.pitches[0] = 1920 * 4;
	ctx->afb->base.pitches[1] = 1920 * 4;
	ctx->afb->base.modifier = DRM_FORMAT_MOD_LINEAR;
	ctx->afb->base.format = drm_format_info(drm_format);
	KUNIT_ASSERT_NOT_NULL(test, ctx->afb->base.format);

	ctx->plane->dev = &ctx->adev->ddev;
	ctx->plane_state->plane = ctx->plane;
	ctx->plane_state->fb = &ctx->afb->base;
	ctx->plane_state->rotation = DRM_MODE_ROTATE_0;
	ctx->plane_state->alpha = DRM_BLEND_ALPHA_OPAQUE;
	ctx->plane_state->src_w = 1920 << 16;
	ctx->plane_state->src_h = 1080 << 16;
	ctx->plane_state->crtc_w = 1920;
	ctx->plane_state->crtc_h = 1080;

	return ctx;
}

static int dm_test_fill_plane_info(struct dm_test_plane_info_ctx *ctx)
{
	return fill_dc_plane_info_and_addr(ctx->adev, ctx->plane_state->state,
					   ctx->plane_state,
					   &ctx->plane_info, &ctx->address, false);
}

static const struct {
	u32 drm_format;
	enum surface_pixel_format dc_format;
} dm_test_plane_graphics_formats[] = {
	{ DRM_FORMAT_C8, SURFACE_PIXEL_FORMAT_GRPH_PALETA_256_COLORS },
	{ DRM_FORMAT_RGB565, SURFACE_PIXEL_FORMAT_GRPH_RGB565 },
	{ DRM_FORMAT_XRGB8888, SURFACE_PIXEL_FORMAT_GRPH_ARGB8888 },
	{ DRM_FORMAT_ARGB8888, SURFACE_PIXEL_FORMAT_GRPH_ARGB8888 },
	{ DRM_FORMAT_XRGB2101010, SURFACE_PIXEL_FORMAT_GRPH_ARGB2101010 },
	{ DRM_FORMAT_ARGB2101010, SURFACE_PIXEL_FORMAT_GRPH_ARGB2101010 },
	{ DRM_FORMAT_XBGR2101010, SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010 },
	{ DRM_FORMAT_ABGR2101010, SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010 },
	{ DRM_FORMAT_XBGR8888, SURFACE_PIXEL_FORMAT_GRPH_ABGR8888 },
	{ DRM_FORMAT_ABGR8888, SURFACE_PIXEL_FORMAT_GRPH_ABGR8888 },
	{ DRM_FORMAT_XRGB16161616F, SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616F },
	{ DRM_FORMAT_ARGB16161616F, SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616F },
	{ DRM_FORMAT_XBGR16161616F, SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F },
	{ DRM_FORMAT_ABGR16161616F, SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F },
	{ DRM_FORMAT_XRGB16161616, SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616 },
	{ DRM_FORMAT_ARGB16161616, SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616 },
	{ DRM_FORMAT_XBGR16161616, SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616 },
	{ DRM_FORMAT_ABGR16161616, SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616 },
};

/**
 * dm_test_plane_info_graphics_formats - Test every graphics format mapping
 * @test: The KUnit test context
 */
static void dm_test_plane_info_graphics_formats(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(dm_test_plane_graphics_formats); i++) {
		u32 fmt = dm_test_plane_graphics_formats[i].drm_format;
		struct dm_test_plane_info_ctx *ctx;

		ctx = dm_test_plane_info_ctx_alloc(test, adev, fmt);

		KUNIT_EXPECT_EQ_MSG(test, dm_test_fill_plane_info(ctx), 0,
				    "format %p4cc", &fmt);
		KUNIT_EXPECT_EQ_MSG(test, (int)ctx->plane_info.format,
				    (int)dm_test_plane_graphics_formats[i].dc_format,
				    "format %p4cc", &fmt);
		/* Graphics formats ignore the DRM colour properties. */
		KUNIT_EXPECT_EQ(test, (int)ctx->plane_info.color_space,
				(int)COLOR_SPACE_SRGB);
	}
}

static const struct {
	u32 drm_format;
	enum surface_pixel_format dc_format;
} dm_test_plane_video_formats[] = {
	{ DRM_FORMAT_NV21, SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr },
	{ DRM_FORMAT_NV12, SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb },
	{ DRM_FORMAT_P010, SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCrCb },
};

/**
 * dm_test_plane_info_video_formats - Test every video format mapping
 * @test: The KUnit test context
 */
static void dm_test_plane_info_video_formats(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(dm_test_plane_video_formats); i++) {
		u32 fmt = dm_test_plane_video_formats[i].drm_format;
		struct dm_test_plane_info_ctx *ctx;

		ctx = dm_test_plane_info_ctx_alloc(test, adev, fmt);
		ctx->plane_state->color_encoding = DRM_COLOR_YCBCR_BT709;
		ctx->plane_state->color_range = DRM_COLOR_YCBCR_LIMITED_RANGE;

		KUNIT_EXPECT_EQ_MSG(test, dm_test_fill_plane_info(ctx), 0,
				    "format %p4cc", &fmt);
		KUNIT_EXPECT_EQ_MSG(test, (int)ctx->plane_info.format,
				    (int)dm_test_plane_video_formats[i].dc_format,
				    "format %p4cc", &fmt);
		KUNIT_EXPECT_EQ(test, (int)ctx->plane_info.color_space,
				(int)COLOR_SPACE_YCBCR709_LIMITED);
	}
}

/**
 * dm_test_plane_info_unsupported_format - Test an unsupported format is rejected
 * @test: The KUnit test context
 */
static void dm_test_plane_info_unsupported_format(struct kunit *test)
{
	struct dm_test_plane_info_ctx *ctx;

	ctx = dm_test_plane_info_ctx_alloc(test, dm_kunit_alloc_adev(test),
					   DRM_FORMAT_YUYV);

	KUNIT_EXPECT_EQ(test, dm_test_fill_plane_info(ctx), -EINVAL);
}

/**
 * dm_test_plane_info_bad_color_encoding - Test an invalid colour encoding is rejected
 * @test: The KUnit test context
 */
static void dm_test_plane_info_bad_color_encoding(struct kunit *test)
{
	struct dm_test_plane_info_ctx *ctx;

	ctx = dm_test_plane_info_ctx_alloc(test, dm_kunit_alloc_adev(test),
					   DRM_FORMAT_NV12);
	ctx->plane_state->color_encoding = DRM_COLOR_ENCODING_MAX;

	KUNIT_EXPECT_EQ(test, dm_test_fill_plane_info(ctx), -EINVAL);
}

/**
 * dm_test_plane_info_rotations - Test every DRM rotation maps to a DC angle
 * @test: The KUnit test context
 */
static void dm_test_plane_info_rotations(struct kunit *test)
{
	static const struct {
		unsigned int drm_rotation;
		enum dc_rotation_angle dc_rotation;
	} cases[] = {
		{ DRM_MODE_ROTATE_0, ROTATION_ANGLE_0 },
		{ DRM_MODE_ROTATE_90, ROTATION_ANGLE_90 },
		{ DRM_MODE_ROTATE_180, ROTATION_ANGLE_180 },
		{ DRM_MODE_ROTATE_270, ROTATION_ANGLE_270 },
		/* No rotation bit set falls back to the unrotated angle. */
		{ 0, ROTATION_ANGLE_0 },
	};
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(cases); i++) {
		struct dm_test_plane_info_ctx *ctx;

		ctx = dm_test_plane_info_ctx_alloc(test, adev, DRM_FORMAT_ARGB8888);
		ctx->plane_state->rotation = cases[i].drm_rotation;

		KUNIT_EXPECT_EQ(test, dm_test_fill_plane_info(ctx), 0);
		KUNIT_EXPECT_EQ(test, (int)ctx->plane_info.rotation,
				(int)cases[i].dc_rotation);
	}
}

/**
 * dm_test_plane_info_layer_and_blending - Test z-order and blending are forwarded
 * @test: The KUnit test context
 */
static void dm_test_plane_info_layer_and_blending(struct kunit *test)
{
	struct dm_test_plane_info_ctx *ctx;

	ctx = dm_test_plane_info_ctx_alloc(test, dm_kunit_alloc_adev(test),
					   DRM_FORMAT_ARGB8888);
	ctx->plane_state->normalized_zpos = 3;
	ctx->plane_state->pixel_blend_mode = DRM_MODE_BLEND_PREMULTI;
	ctx->plane_state->alpha = DRM_BLEND_ALPHA_OPAQUE / 2;

	KUNIT_EXPECT_EQ(test, dm_test_fill_plane_info(ctx), 0);
	KUNIT_EXPECT_TRUE(test, ctx->plane_info.visible);
	KUNIT_EXPECT_EQ(test, ctx->plane_info.layer_index, 3);
	KUNIT_EXPECT_EQ(test, (int)ctx->plane_info.stereo_format,
			(int)PLANE_STEREO_FORMAT_NONE);
	KUNIT_EXPECT_TRUE(test, ctx->plane_info.per_pixel_alpha);
	KUNIT_EXPECT_TRUE(test, ctx->plane_info.global_alpha);
	KUNIT_EXPECT_EQ(test, ctx->plane_info.global_alpha_value, 0x7f);
}

/* Tests for amdgpu_dm_enable_self_refresh() */

struct dm_test_sr_ctx {
	struct amdgpu_display_manager *dm;
	struct amdgpu_crtc *acrtc;
	struct dm_crtc_state *acrtc_state;
	struct amdgpu_dm_connector *aconn;
	struct dc_link *link;
};

/*
 * A fast-update CRTC whose stream has a self-refresh capable link. The power
 * module stays NULL, which every mod_power entry point treats as a no-op.
 */
static struct dm_test_sr_ctx *dm_test_sr_ctx_alloc(struct kunit *test)
{
	struct dm_test_sr_ctx *ctx;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	ctx->dm = dm_kunit_alloc_dm(test);
	ctx->acrtc = kunit_kzalloc(test, sizeof(*ctx->acrtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->acrtc);
	ctx->acrtc_state = kunit_kzalloc(test, sizeof(*ctx->acrtc_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->acrtc_state);
	ctx->aconn = kunit_kzalloc(test, sizeof(*ctx->aconn), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->aconn);

	ctx->link = dm_kunit_alloc_link(test);
	ctx->acrtc_state->stream = dm_kunit_alloc_stream(test, ctx->link);
	ctx->acrtc_state->stream->dm_stream_context = ctx->aconn;
	ctx->acrtc_state->update_type = UPDATE_TYPE_FAST;

	return ctx;
}

static void dm_test_enable_sr(struct dm_test_sr_ctx *ctx, u64 current_ts)
{
	amdgpu_dm_enable_self_refresh(ctx->dm, ctx->acrtc, ctx->acrtc_state,
				      current_ts);
}

/**
 * dm_test_self_refresh_full_update - Test a full update blocks self refresh
 * @test: The KUnit test context
 */
static void dm_test_self_refresh_full_update(struct kunit *test)
{
	struct dm_test_sr_ctx *ctx = dm_test_sr_ctx_alloc(test);

	ctx->acrtc_state->update_type = UPDATE_TYPE_FULL;
	ctx->link->psr_settings.psr_feature_enabled = true;
	ctx->acrtc->dm_irq_params.allow_sr_entry = true;

	dm_test_enable_sr(ctx, 0);

	KUNIT_EXPECT_FALSE(test, ctx->acrtc->dm_irq_params.allow_sr_entry);
}

/**
 * dm_test_self_refresh_unsupported_link - Test a link without PSR or Replay
 * @test: The KUnit test context
 */
static void dm_test_self_refresh_unsupported_link(struct kunit *test)
{
	struct dm_test_sr_ctx *ctx = dm_test_sr_ctx_alloc(test);

	ctx->acrtc->dm_irq_params.allow_sr_entry = true;

	dm_test_enable_sr(ctx, 0);

	KUNIT_EXPECT_FALSE(test, ctx->acrtc->dm_irq_params.allow_sr_entry);
}

/**
 * dm_test_self_refresh_decrements_skip_count - Test the skip count gates entry
 * @test: The KUnit test context
 */
static void dm_test_self_refresh_decrements_skip_count(struct kunit *test)
{
	struct dm_test_sr_ctx *ctx = dm_test_sr_ctx_alloc(test);

	ctx->link->psr_settings.psr_feature_enabled = true;
	ctx->aconn->sr_skip_count = 2;

	dm_test_enable_sr(ctx, 0);

	KUNIT_EXPECT_EQ(test, ctx->aconn->sr_skip_count, 1);
	KUNIT_EXPECT_FALSE(test, ctx->acrtc->dm_irq_params.allow_sr_entry);
}

/**
 * dm_test_self_refresh_allows_entry - Test a drained skip count allows entry
 * @test: The KUnit test context
 */
static void dm_test_self_refresh_allows_entry(struct kunit *test)
{
	struct dm_test_sr_ctx *ctx = dm_test_sr_ctx_alloc(test);

	ctx->link->psr_settings.psr_feature_enabled = true;
	ctx->aconn->sr_skip_count = 1;

	/* Well past the 500ms settle window, so the events are cleared. */
	dm_test_enable_sr(ctx, 2ULL * NSEC_PER_SEC);

	KUNIT_EXPECT_EQ(test, ctx->aconn->sr_skip_count, 0);
	KUNIT_EXPECT_TRUE(test, ctx->acrtc->dm_irq_params.allow_sr_entry);
}

/**
 * dm_test_self_refresh_within_settle_window - Test a recent damage change holds off
 * @test: The KUnit test context
 */
static void dm_test_self_refresh_within_settle_window(struct kunit *test)
{
	struct dm_test_sr_ctx *ctx = dm_test_sr_ctx_alloc(test);

	ctx->link->psr_settings.psr_feature_enabled = true;
	ctx->link->psr_settings.psr_dirty_rects_change_timestamp_ns = 1;

	dm_test_enable_sr(ctx, 2);

	KUNIT_EXPECT_TRUE(test, ctx->acrtc->dm_irq_params.allow_sr_entry);
}

/**
 * dm_test_self_refresh_replay_link - Test a Replay capable link takes the same path
 * @test: The KUnit test context
 */
static void dm_test_self_refresh_replay_link(struct kunit *test)
{
	struct dm_test_sr_ctx *ctx = dm_test_sr_ctx_alloc(test);

	ctx->link->replay_settings.replay_feature_enabled = true;

	dm_test_enable_sr(ctx, 2ULL * NSEC_PER_SEC);

	KUNIT_EXPECT_TRUE(test, ctx->acrtc->dm_irq_params.allow_sr_entry);
}

/* Tests for manage_dm_interrupts() */

struct dm_test_irq_mgmt_ctx {
	struct amdgpu_device *adev;
	struct amdgpu_crtc *acrtc;
	struct dm_crtc_state *acrtc_state;
};

/*
 * A CRTC with a single initialised vblank and a 1080p60 stream timing, which
 * is what the vblank off-delay estimate is derived from.
 */
static struct dm_test_irq_mgmt_ctx *dm_test_irq_mgmt_ctx_alloc(struct kunit *test)
{
	struct dm_test_irq_mgmt_ctx *ctx;
	struct dc_crtc_timing *timing;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	ctx->adev = dm_kunit_alloc_adev(test);
	KUNIT_ASSERT_EQ(test, drm_vblank_init(&ctx->adev->ddev, 1), 0);

	ctx->acrtc = kunit_kzalloc(test, sizeof(*ctx->acrtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->acrtc);
	ctx->acrtc_state = kunit_kzalloc(test, sizeof(*ctx->acrtc_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->acrtc_state);

	ctx->adev->mode_info.num_crtc = 1;
	ctx->acrtc->base.dev = &ctx->adev->ddev;
	ctx->acrtc->base.funcs = &dm_test_vblank_crtc_funcs;
	INIT_LIST_HEAD(&ctx->acrtc->base.head);
	list_add_tail(&ctx->acrtc->base.head,
		      &ctx->adev->ddev.mode_config.crtc_list);
	ctx->acrtc->crtc_id = 0;
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, dm_test_vblank_ctx_cleanup, ctx->acrtc), 0);

	ctx->acrtc_state->stream = dm_kunit_alloc_stream(test, NULL);
	timing = &ctx->acrtc_state->stream->timing;
	timing->h_total = 2200;
	timing->v_total = 1125;
	timing->pix_clk_100hz = 1485000;

	return ctx;
}

/**
 * dm_test_manage_interrupts_offdelay - Test the off delay is derived from timing
 * @test: The KUnit test context
 *
 * DCN3.0 also takes the extra vupdate reference. The IRQ subsystem is not
 * installed, so amdgpu_irq_get() only reports the missing source.
 */
static void dm_test_manage_interrupts_offdelay(struct kunit *test)
{
	struct dm_test_irq_mgmt_ctx *ctx = dm_test_irq_mgmt_ctx_alloc(test);

	/* Pre-DCN3.5 keeps the two frame off delay. */
	ctx->adev->ip_versions[DCE_HWIP][0] = IP_VERSION(3, 0, 0);

	manage_dm_interrupts(ctx->adev, ctx->acrtc, ctx->acrtc_state);

	KUNIT_EXPECT_EQ(test, ctx->adev->ddev.vblank[0].config.offdelay_ms, 34);
	KUNIT_EXPECT_FALSE(test, ctx->adev->ddev.vblank[0].config.disable_immediate);
}

/**
 * dm_test_manage_interrupts_offdelay_fallback - Test a zero delay falls back to 30ms
 * @test: The KUnit test context
 */
static void dm_test_manage_interrupts_offdelay_fallback(struct kunit *test)
{
	struct dm_test_irq_mgmt_ctx *ctx = dm_test_irq_mgmt_ctx_alloc(test);

	ctx->adev->ip_versions[DCE_HWIP][0] = IP_VERSION(3, 1, 2);
	/* A zero line count makes the computed delay round down to zero. */
	ctx->acrtc_state->stream->timing.v_total = 0;

	manage_dm_interrupts(ctx->adev, ctx->acrtc, ctx->acrtc_state);

	KUNIT_EXPECT_EQ(test, ctx->adev->ddev.vblank[0].config.offdelay_ms, 30);
}

/**
 * dm_test_manage_interrupts_apu_instant_off - Test DCN3.5 APUs use instant off
 * @test: The KUnit test context
 */
static void dm_test_manage_interrupts_apu_instant_off(struct kunit *test)
{
	struct dm_test_irq_mgmt_ctx *ctx = dm_test_irq_mgmt_ctx_alloc(test);

	ctx->adev->ip_versions[DCE_HWIP][0] = IP_VERSION(3, 5, 0);
	ctx->adev->flags |= AMD_IS_APU;

	manage_dm_interrupts(ctx->adev, ctx->acrtc, ctx->acrtc_state);

	KUNIT_EXPECT_EQ(test, ctx->adev->ddev.vblank[0].config.offdelay_ms, 1);
	KUNIT_EXPECT_TRUE(test, ctx->adev->ddev.vblank[0].config.disable_immediate);
}

/**
 * dm_test_manage_interrupts_disable - Test a NULL CRTC state turns vblank off
 * @test: The KUnit test context
 */
static void dm_test_manage_interrupts_disable(struct kunit *test)
{
	struct dm_test_irq_mgmt_ctx *ctx = dm_test_irq_mgmt_ctx_alloc(test);

	ctx->adev->ip_versions[DCE_HWIP][0] = IP_VERSION(3, 0, 0);
	manage_dm_interrupts(ctx->adev, ctx->acrtc, ctx->acrtc_state);

	manage_dm_interrupts(ctx->adev, ctx->acrtc, NULL);

	KUNIT_EXPECT_FALSE(test, ctx->adev->ddev.vblank[0].enabled);
}

/* Tests for dm_early_init() */

#define DM_TEST_ATOM_BIOS_SIZE	512

/*
 * A fake ATOM BIOS image. Every byte is non-zero, so the master data table
 * reports a present entry for whichever index the object header lives at,
 * which is all amdgpu_atom_parse_data_header() checks here.
 */
static struct atom_context *dm_test_alloc_atom_context(struct kunit *test,
						       bool object_header)
{
	struct atom_context *ctx;
	void *bios;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);
	bios = kunit_kzalloc(test, DM_TEST_ATOM_BIOS_SIZE, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, bios);

	if (object_header)
		memset(bios, 0x01, DM_TEST_ATOM_BIOS_SIZE);

	ctx->bios = bios;
	ctx->bios_size = DM_TEST_ATOM_BIOS_SIZE;
	ctx->data_table = 0;

	return ctx;
}

static struct amdgpu_device *dm_test_early_init_adev(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);

	adev->mode_info.atom_context = dm_test_alloc_atom_context(test, true);

	return adev;
}

static int dm_test_run_early_init(struct amdgpu_device *adev)
{
	struct amdgpu_ip_block ip_block = { .adev = adev };

	return dm_early_init(&ip_block);
}

/**
 * dm_test_early_init_no_object_header - Test a BIOS without an object header
 * @test: The KUnit test context
 */
static void dm_test_early_init_no_object_header(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);

	adev->mode_info.atom_context = dm_test_alloc_atom_context(test, false);

	KUNIT_EXPECT_EQ(test, dm_test_run_early_init(adev), -ENOENT);
	KUNIT_EXPECT_TRUE(test, adev->harvest_ip_mask & AMD_HARVEST_IP_DMU_MASK);
	KUNIT_EXPECT_FALSE(test, adev->dc_enabled);
}

/**
 * dm_test_early_init_legacy_asics - Test the display counts of legacy ASICs
 * @test: The KUnit test context
 */
static void dm_test_early_init_legacy_asics(struct kunit *test)
{
	static const struct {
		enum amd_asic_type asic_type;
		u32 num_crtc;
		u32 num_hpd;
		u32 num_dig;
	} cases[] = {
		{ CHIP_BONAIRE, 6, 6, 6 },
		{ CHIP_HAWAII, 6, 6, 6 },
		{ CHIP_KAVERI, 4, 6, 7 },
		{ CHIP_KABINI, 2, 6, 6 },
		{ CHIP_MULLINS, 2, 6, 6 },
		{ CHIP_FIJI, 6, 6, 7 },
		{ CHIP_TONGA, 6, 6, 7 },
		{ CHIP_CARRIZO, 3, 6, 9 },
		{ CHIP_STONEY, 2, 6, 9 },
		{ CHIP_POLARIS11, 5, 5, 5 },
		{ CHIP_POLARIS12, 5, 5, 5 },
		{ CHIP_POLARIS10, 6, 6, 6 },
		{ CHIP_VEGAM, 6, 6, 6 },
		{ CHIP_VEGA10, 6, 6, 6 },
		{ CHIP_VEGA12, 6, 6, 6 },
		{ CHIP_VEGA20, 6, 6, 6 },
	};
	struct amdgpu_device *adev = dm_test_early_init_adev(test);
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(cases); i++) {
		adev->asic_type = cases[i].asic_type;

		KUNIT_EXPECT_EQ_MSG(test, dm_test_run_early_init(adev), 0,
				    "asic_type %d", cases[i].asic_type);
		KUNIT_EXPECT_EQ_MSG(test, adev->mode_info.num_crtc, cases[i].num_crtc,
				    "asic_type %d", cases[i].asic_type);
		KUNIT_EXPECT_EQ_MSG(test, adev->mode_info.num_hpd, cases[i].num_hpd,
				    "asic_type %d", cases[i].asic_type);
		KUNIT_EXPECT_EQ_MSG(test, adev->mode_info.num_dig, cases[i].num_dig,
				    "asic_type %d", cases[i].asic_type);
	}

	KUNIT_EXPECT_NOT_NULL(test, adev->mode_info.funcs);
	KUNIT_EXPECT_TRUE(test, adev->dc_enabled);
}

/**
 * dm_test_early_init_dcn_versions - Test the display counts of DCN IP versions
 * @test: The KUnit test context
 *
 * Only IP versions without DMUB firmware are used, so dm_init_microcode() does
 * not reach a firmware request.
 */
static void dm_test_early_init_dcn_versions(struct kunit *test)
{
	static const struct {
		u32 ip_version;
		u32 num_crtc;
	} cases[] = {
		{ IP_VERSION(2, 0, 2), 6 },
		{ IP_VERSION(2, 0, 0), 5 },
		{ IP_VERSION(2, 0, 3), 2 },
		{ IP_VERSION(1, 0, 0), 4 },
		{ IP_VERSION(1, 0, 1), 4 },
	};
	struct amdgpu_device *adev = dm_test_early_init_adev(test);
	unsigned int i;

	adev->asic_type = CHIP_IP_DISCOVERY;

	for (i = 0; i < ARRAY_SIZE(cases); i++) {
		adev->ip_versions[DCE_HWIP][0] = cases[i].ip_version;

		KUNIT_EXPECT_EQ_MSG(test, dm_test_run_early_init(adev), 0,
				    "ip_version 0x%x", cases[i].ip_version);
		KUNIT_EXPECT_EQ_MSG(test, adev->mode_info.num_crtc, cases[i].num_crtc,
				    "ip_version 0x%x", cases[i].ip_version);
		/* Every DCN entry keeps hpd and dig in step with the CRTC count. */
		KUNIT_EXPECT_EQ(test, adev->mode_info.num_hpd, cases[i].num_crtc);
		KUNIT_EXPECT_EQ(test, adev->mode_info.num_dig, cases[i].num_crtc);
	}
}

/**
 * dm_test_early_init_unsupported_version - Test an unknown IP version is rejected
 * @test: The KUnit test context
 */
static void dm_test_early_init_unsupported_version(struct kunit *test)
{
	struct amdgpu_device *adev = dm_test_early_init_adev(test);

	adev->asic_type = CHIP_IP_DISCOVERY;
	adev->ip_versions[DCE_HWIP][0] = IP_VERSION(9, 9, 9);

	KUNIT_EXPECT_EQ(test, dm_test_run_early_init(adev), -EINVAL);
	KUNIT_EXPECT_FALSE(test, adev->dc_enabled);
}

/* Tests for the remaining suspend and resume helpers */

/**
 * dm_test_commit_zero_streams_empty - Test committing an already empty DC state
 * @test: The KUnit test context
 */
static void dm_test_commit_zero_streams_empty(struct kunit *test)
{
	struct dc *dc = dm_kunit_alloc_dc_with_ctx(test);

	dc->current_state = dm_kunit_alloc_dc_state(test);
	KUNIT_ASSERT_NOT_NULL(test, dc->current_state);

	KUNIT_EXPECT_EQ(test, (int)amdgpu_dm_commit_zero_streams(dc), (int)DC_OK);
}

/**
 * dm_test_cache_state_empty_device - Test caching an empty DRM atomic state
 * @test: The KUnit test context
 */
static void dm_test_cache_state_empty_device(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);

	KUNIT_ASSERT_EQ(test, dm_cache_state(adev), 0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev->dm.cached_state);

	dm_destroy_cached_state(adev);
	KUNIT_EXPECT_NULL(test, adev->dm.cached_state);
}

static struct drm_atomic_commit *dm_test_atomic_helper_suspend_error(struct drm_device *dev)
{
	return ERR_PTR(-EIO);
}

static const struct amdgpu_dm_kunit_ops dm_test_cache_state_ops = {
	.atomic_helper_suspend = dm_test_atomic_helper_suspend_error,
};

/**
 * dm_test_cache_state_error - Test atomic suspend errors are returned and cleared
 * @test: The KUnit test context
 */
static void dm_test_cache_state_error(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);

	dm_test_install_dm_ops(test, &dm_test_cache_state_ops);

	KUNIT_EXPECT_EQ(test, dm_cache_state(adev), -EIO);
	KUNIT_EXPECT_NULL(test, adev->dm.cached_state);
}

/**
 * dm_test_destroy_cached_state_none - Test no cached state is a no-op
 * @test: The KUnit test context
 */
static void dm_test_destroy_cached_state_none(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);

	dm_destroy_cached_state(adev);

	KUNIT_EXPECT_NULL(test, adev->dm.cached_state);
}

static bool dm_test_update_bandwidth(struct dc *dc, struct dc_state *context)
{
	return true;
}

/**
 * dm_test_clear_writeback_removes_stream - Test teardown removes the stream writeback
 * @test: The KUnit test context
 */
static void dm_test_clear_writeback_removes_stream(struct kunit *test)
{
	struct amdgpu_display_manager *dm;
	struct dm_crtc_state *crtc_state;
	struct amdgpu_crtc *acrtc;
	struct dc_stream_state *stream;

	dm = dm_kunit_alloc_dm(test);
	crtc_state = kunit_kzalloc(test, sizeof(*crtc_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, crtc_state);
	acrtc = kunit_kzalloc(test, sizeof(*acrtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, acrtc);
	stream = dm_kunit_alloc_stream(test, NULL);
	dm->dc->hwss.update_bandwidth = dm_test_update_bandwidth;
	crtc_state->stream = stream;
	stream->num_wb_info = 1;
	stream->writeback_info[0].dwb_pipe_inst = 0;

	dm_clear_writeback(dm, acrtc, crtc_state);

	KUNIT_EXPECT_EQ(test, stream->num_wb_info, 0U);
}

/**
 * dm_test_set_writeback_no_pipe - Test a stream without a DC pipe is not armed
 * @test: The KUnit test context
 */
static void dm_test_set_writeback_no_pipe(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct drm_writeback_connector *wb_conn;
	struct drm_connector_state *conn_state;
	struct drm_writeback_job *job;
	struct amdgpu_framebuffer *afb;
	struct dm_crtc_state *crtc_state;
	struct amdgpu_crtc *acrtc;
	struct dc *dc;

	wb_conn = kunit_kzalloc(test, sizeof(*wb_conn), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, wb_conn);
	conn_state = kunit_kzalloc(test, sizeof(*conn_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, conn_state);
	job = kunit_kzalloc(test, sizeof(*job), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, job);
	afb = kunit_kzalloc(test, sizeof(*afb), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, afb);
	crtc_state = kunit_kzalloc(test, sizeof(*crtc_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, crtc_state);
	acrtc = kunit_kzalloc(test, sizeof(*acrtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, acrtc);
	dc = dm_kunit_alloc_dc_with_ctx(test);
	dc->current_state = dm_kunit_alloc_dc_state(test);

	adev->dm.adev = adev;
	adev->dm.dc = dc;
	wb_conn->encoder.crtc = &acrtc->base;
	job->fb = &afb->base;
	conn_state->writeback_job = job;
	crtc_state->stream = dm_kunit_alloc_stream(test, NULL);

	dm_set_writeback(&adev->dm, crtc_state, &wb_conn->base, conn_state);

	KUNIT_EXPECT_NULL(test, acrtc->wb_conn);
	KUNIT_EXPECT_FALSE(test, acrtc->wb_pending);
}

/* Tests for dm_update_mst_vcpi_slots_for_dsc() */

/**
 * dm_test_mst_vcpi_slots_no_connector - Test an empty commit allocates no slots
 * @test: The KUnit test context
 */
static void dm_test_mst_vcpi_slots_no_connector(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct dsc_mst_fairness_vars vars[MAX_PIPES] = {0};
	struct dc_state *dc_state = dm_kunit_alloc_dc_state(test);

	KUNIT_ASSERT_NOT_NULL(test, dc_state);

	KUNIT_EXPECT_EQ(test,
			dm_update_mst_vcpi_slots_for_dsc(dm_test_alloc_commit(test, adev),
							 dc_state, vars),
			0);
}

/**
 * dm_test_mst_vcpi_slots_skips_writeback - Test writeback connectors are skipped
 * @test: The KUnit test context
 */
static void dm_test_mst_vcpi_slots_skips_writeback(struct kunit *test)
{
	struct dsc_mst_fairness_vars vars[MAX_PIPES] = {0};
	struct amdgpu_dm_connector *aconnector;
	struct dc_state *dc_state;
	struct drm_atomic_commit *state;
	struct drm_crtc *crtc;

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);
	crtc = kunit_kzalloc(test, sizeof(*crtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, crtc);
	dc_state = dm_kunit_alloc_dc_state(test);
	KUNIT_ASSERT_NOT_NULL(test, dc_state);

	aconnector->base.connector_type = DRM_MODE_CONNECTOR_WRITEBACK;
	state = dm_test_state_with_connector(test, &aconnector->base, crtc);

	KUNIT_EXPECT_EQ(test,
			dm_update_mst_vcpi_slots_for_dsc(state, dc_state, vars), 0);
}

/**
 * dm_test_mst_vcpi_slots_skips_non_mst - Test a connector without an MST port
 * @test: The KUnit test context
 */
static void dm_test_mst_vcpi_slots_skips_non_mst(struct kunit *test)
{
	struct dsc_mst_fairness_vars vars[MAX_PIPES] = {0};
	struct amdgpu_dm_connector *aconnector;
	struct dc_state *dc_state;
	struct drm_atomic_commit *state;
	struct drm_crtc *crtc;

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);
	crtc = kunit_kzalloc(test, sizeof(*crtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, crtc);
	dc_state = dm_kunit_alloc_dc_state(test);
	KUNIT_ASSERT_NOT_NULL(test, dc_state);

	aconnector->base.connector_type = DRM_MODE_CONNECTOR_DisplayPort;
	state = dm_test_state_with_connector(test, &aconnector->base, crtc);

	KUNIT_EXPECT_EQ(test,
			dm_update_mst_vcpi_slots_for_dsc(state, dc_state, vars), 0);
}

/**
 * dm_test_mst_vcpi_slots_no_matching_stream - Test a connector with no DC stream
 * @test: The KUnit test context
 */
static void dm_test_mst_vcpi_slots_no_matching_stream(struct kunit *test)
{
	struct dsc_mst_fairness_vars vars[MAX_PIPES] = {0};
	struct amdgpu_dm_connector *aconnector;
	struct drm_dp_mst_port *port;
	struct dc_state *dc_state;
	struct drm_atomic_commit *state;
	struct drm_crtc *crtc;

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);
	port = kunit_kzalloc(test, sizeof(*port), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, port);
	crtc = kunit_kzalloc(test, sizeof(*crtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, crtc);
	dc_state = dm_kunit_alloc_dc_state(test);
	KUNIT_ASSERT_NOT_NULL(test, dc_state);

	aconnector->base.connector_type = DRM_MODE_CONNECTOR_DisplayPort;
	aconnector->mst_output_port = port;
	state = dm_test_state_with_connector(test, &aconnector->base, crtc);

	/* No DC stream references the connector, so the slot update is skipped. */
	KUNIT_EXPECT_EQ(test,
			dm_update_mst_vcpi_slots_for_dsc(state, dc_state, vars), 0);
}

/* Tests for fill_dc_plane_attributes() */

struct dm_test_plane_attr_ctx {
	struct amdgpu_device *adev;
	struct dm_crtc_state *crtc_state;
	struct dm_test_plane_info_ctx *plane;
	struct dc_plane_state *dc_plane;
};

/*
 * A DC plane, plus a CRTC state complete enough for the colour management
 * update at the end of fill_dc_plane_attributes(). A scaling factor of 1 in the
 * plane caps means "no scaling", which is what the 1:1 geometry of the plane
 * info context needs.
 */
static struct dm_test_plane_attr_ctx *
dm_test_plane_attr_ctx_alloc(struct kunit *test, u32 drm_format)
{
	struct dm_test_plane_attr_ctx *ctx;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);
	ctx->crtc_state = kunit_kzalloc(test, sizeof(*ctx->crtc_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->crtc_state);
	ctx->dc_plane = kunit_kzalloc(test, sizeof(*ctx->dc_plane), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->dc_plane);

	ctx->adev = dm_kunit_alloc_adev(test);
	ctx->adev->dm.dc = dm_kunit_alloc_dc_with_ctx(test);
	ctx->adev->dm.dc->caps.planes[0].max_upscale_factor.argb8888 = 1;
	ctx->adev->dm.dc->caps.planes[0].max_downscale_factor.argb8888 = 1;

	ctx->crtc_state->stream = dm_kunit_alloc_stream(test, NULL);
	/* Colour management resolves the device through the commit backpointer. */
	ctx->crtc_state->base.state = dm_test_alloc_commit(test, ctx->adev);
	ctx->plane = dm_test_plane_info_ctx_alloc(test, ctx->adev, drm_format);

	return ctx;
}

static int dm_test_fill_plane_attr(struct dm_test_plane_attr_ctx *ctx)
{
	return fill_dc_plane_attributes(ctx->adev, ctx->dc_plane,
					ctx->plane->plane_state, &ctx->crtc_state->base);
}

/**
 * dm_test_plane_attributes_success - Test plane info is copied into the DC plane
 * @test: The KUnit test context
 */
static void dm_test_plane_attributes_success(struct kunit *test)
{
	struct dm_test_plane_attr_ctx *ctx;

	ctx = dm_test_plane_attr_ctx_alloc(test, DRM_FORMAT_ARGB8888);
	ctx->plane->plane_state->normalized_zpos = 2;

	KUNIT_EXPECT_EQ(test, dm_test_fill_plane_attr(ctx), 0);
	KUNIT_EXPECT_EQ(test, (int)ctx->dc_plane->format,
			(int)SURFACE_PIXEL_FORMAT_GRPH_ARGB8888);
	KUNIT_EXPECT_EQ(test, (int)ctx->dc_plane->color_space, (int)COLOR_SPACE_SRGB);
	KUNIT_EXPECT_EQ(test, (int)ctx->dc_plane->rotation, (int)ROTATION_ANGLE_0);
	KUNIT_EXPECT_EQ(test, ctx->dc_plane->src_rect.width, 1920U);
	KUNIT_EXPECT_EQ(test, ctx->dc_plane->dst_rect.width, 1920U);
	KUNIT_EXPECT_EQ(test, ctx->dc_plane->layer_index, 2);
	KUNIT_EXPECT_TRUE(test, ctx->dc_plane->visible);
	KUNIT_EXPECT_TRUE(test, ctx->dc_plane->flip_int_enabled);
}

/**
 * dm_test_plane_attributes_bad_scaling - Test an empty source rectangle is rejected
 * @test: The KUnit test context
 */
static void dm_test_plane_attributes_bad_scaling(struct kunit *test)
{
	struct dm_test_plane_attr_ctx *ctx;

	ctx = dm_test_plane_attr_ctx_alloc(test, DRM_FORMAT_ARGB8888);
	ctx->plane->plane_state->src_w = 0;

	KUNIT_EXPECT_EQ(test, dm_test_fill_plane_attr(ctx), -EINVAL);
}

/**
 * dm_test_plane_attributes_bad_format - Test an unsupported format is rejected
 * @test: The KUnit test context
 */
static void dm_test_plane_attributes_bad_format(struct kunit *test)
{
	struct dm_test_plane_attr_ctx *ctx;

	ctx = dm_test_plane_attr_ctx_alloc(test, DRM_FORMAT_YUYV);

	KUNIT_EXPECT_EQ(test, dm_test_fill_plane_attr(ctx), -EINVAL);
}

/**
 * dm_test_plane_attributes_bad_color_mgmt - Test a bad 3D LUT is rejected
 * @test: The KUnit test context
 */
static void dm_test_plane_attributes_bad_color_mgmt(struct kunit *test)
{
	struct dm_test_plane_attr_ctx *ctx;
	struct drm_property_blob *blob;
	struct drm_color_lut *lut;

	ctx = dm_test_plane_attr_ctx_alloc(test, DRM_FORMAT_ARGB8888);
	blob = kunit_kzalloc(test, sizeof(*blob), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, blob);
	lut = kunit_kcalloc(test, 16, sizeof(*lut), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, lut);

	/* A 3D LUT that is not a perfect cube fails validation. */
	blob->data = lut;
	blob->length = 16 * sizeof(*lut);
	ctx->adev->dm.dc->caps.color.dpp.hw_3d_lut = true;
	ctx->plane->dm_plane_state->lut3d = blob;

	KUNIT_EXPECT_EQ(test, dm_test_fill_plane_attr(ctx), -EINVAL);
}

/* Tests for load_dmcu_fw() */

/**
 * dm_test_load_dmcu_fw_no_dmcu - Test ASICs and IP versions without a DMCU
 * @test: The KUnit test context
 */
static void dm_test_load_dmcu_fw_no_dmcu(struct kunit *test)
{
	static const enum amd_asic_type cases[] = {
		CHIP_BONAIRE, CHIP_HAWAII, CHIP_KAVERI, CHIP_KABINI, CHIP_MULLINS,
		CHIP_TONGA, CHIP_FIJI, CHIP_CARRIZO, CHIP_STONEY, CHIP_POLARIS11,
		CHIP_POLARIS10, CHIP_POLARIS12, CHIP_VEGAM, CHIP_VEGA10,
		CHIP_VEGA12, CHIP_VEGA20,
	};
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(cases); i++) {
		adev->asic_type = cases[i];

		KUNIT_EXPECT_EQ_MSG(test, load_dmcu_fw(adev), 0, "asic_type %d",
				    cases[i]);
		KUNIT_EXPECT_NULL(test, adev->dm.fw_dmcu);
	}
}

/**
 * dm_test_load_dmcu_fw_dcn - Test DCN IP versions report no DMCU firmware
 * @test: The KUnit test context
 */
static void dm_test_load_dmcu_fw_dcn(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);

	adev->asic_type = CHIP_IP_DISCOVERY;
	adev->ip_versions[DCE_HWIP][0] = IP_VERSION(3, 5, 0);

	KUNIT_EXPECT_EQ(test, load_dmcu_fw(adev), 0);
	KUNIT_EXPECT_NULL(test, adev->dm.fw_dmcu);
}

/**
 * dm_test_load_dmcu_fw_unsupported - Test an unknown IP version is rejected
 * @test: The KUnit test context
 */
static void dm_test_load_dmcu_fw_unsupported(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);

	adev->asic_type = CHIP_IP_DISCOVERY;
	adev->ip_versions[DCE_HWIP][0] = IP_VERSION(9, 9, 9);

	KUNIT_EXPECT_EQ(test, load_dmcu_fw(adev), -EINVAL);
}

/**
 * dm_test_load_dmcu_fw_raven - Test the Raven revision selects a DMCU
 * @test: The KUnit test context
 */
static void dm_test_load_dmcu_fw_raven(struct kunit *test)
{
	static const u32 cases[] = { PICASSO_A0, RAVEN2_A0, 0 };
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	unsigned int i;

	adev->asic_type = CHIP_RAVEN;
	adev->firmware.load_type = AMDGPU_FW_LOAD_DIRECT;

	/* Picasso and Raven2 name a DMCU; a bare Raven has none. */
	for (i = 0; i < ARRAY_SIZE(cases); i++) {
		adev->external_rev_id = cases[i];

		KUNIT_EXPECT_EQ_MSG(test, load_dmcu_fw(adev), 0, "rev 0x%x",
				    cases[i]);
	}
}

/**
 * dm_test_load_dmcu_fw_missing_firmware - Test a missing DMCU image is not fatal
 * @test: The KUnit test context
 */
static void dm_test_load_dmcu_fw_missing_firmware(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);

	adev->dev = adev->ddev.dev;
	adev->asic_type = CHIP_NAVI12;
	adev->firmware.load_type = AMDGPU_FW_LOAD_PSP;

	/* No firmware is installed in the test environment. */
	KUNIT_EXPECT_EQ(test, load_dmcu_fw(adev), 0);
	KUNIT_EXPECT_NULL(test, adev->dm.fw_dmcu);
}

/* Tests for dm_sw_init() and dm_sw_fini() */

/**
 * dm_test_sw_init_no_dmub - Test software init on an ASIC without a DMUB
 * @test: The KUnit test context
 */
static void dm_test_sw_init_no_dmub(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct amdgpu_ip_block ip_block = { .adev = adev };

	adev->asic_type = CHIP_BONAIRE;

	KUNIT_EXPECT_EQ(test, dm_sw_init(&ip_block), 0);
	KUNIT_ASSERT_NOT_NULL(test, adev->dm.cgs_device);
	KUNIT_EXPECT_TRUE(test, list_empty(&adev->dm.da_list));

	/* amdgpu_cgs_destroy_device() is not exported and is a plain kfree(). */
	kfree(adev->dm.cgs_device);
}

/**
 * dm_test_sw_fini_releases_bounding_box - Test the DMUB bounding box is released
 * @test: The KUnit test context
 */
static void dm_test_sw_fini_releases_bounding_box(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct amdgpu_ip_block ip_block = { .adev = adev };
	struct dal_allocation *da;
	void *bb;

	bb = kunit_kzalloc(test, sizeof(*da), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, bb);
	/* Freed by dm_sw_fini(), so it must not be KUnit-managed. */
	da = kzalloc_obj(*da);
	KUNIT_ASSERT_NOT_NULL(test, da);

	INIT_LIST_HEAD(&adev->dm.da_list);
	da->cpu_ptr = bb;
	list_add(&da->list, &adev->dm.da_list);
	adev->dm.bb_from_dmub = bb;

	KUNIT_EXPECT_EQ(test, dm_sw_fini(&ip_block), 0);
	KUNIT_EXPECT_NULL(test, adev->dm.bb_from_dmub);
	KUNIT_EXPECT_TRUE(test, list_empty(&adev->dm.da_list));
}

/* Tests for dm_late_init() */

/*
 * A DC without a DMCU and with an empty link list, so the ABM configuration is
 * skipped and only the MST detection sweep runs.
 */
static struct amdgpu_device *dm_test_late_init_adev(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct dc *dc = dm_kunit_alloc_dc_with_ctx(test);

	dc->res_pool = kunit_kzalloc(test, sizeof(*dc->res_pool), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dc->res_pool);
	adev->dm.dc = dc;

	return adev;
}

static int dm_test_run_late_init(struct amdgpu_device *adev)
{
	struct amdgpu_ip_block ip_block = { .adev = adev };

	return dm_late_init(&ip_block);
}

/**
 * dm_test_late_init_no_dmcu - Test a DC without a DMCU or DMUB
 * @test: The KUnit test context
 */
static void dm_test_late_init_no_dmcu(struct kunit *test)
{
	struct amdgpu_device *adev = dm_test_late_init_adev(test);

	KUNIT_EXPECT_EQ(test, dm_test_run_late_init(adev), 0);
}

/**
 * dm_test_late_init_boot_crc_no_dmub - Test boot time CRC needs a DMUB service
 * @test: The KUnit test context
 */
static void dm_test_late_init_boot_crc_no_dmub(struct kunit *test)
{
	struct amdgpu_device *adev = dm_test_late_init_adev(test);

	adev->ip_versions[DCE_HWIP][0] = IP_VERSION(3, 6, 0);

	KUNIT_EXPECT_EQ(test, dm_test_run_late_init(adev), 0);
	KUNIT_EXPECT_NULL(test, adev->dm.boot_time_crc_info.bo_ptr);
}

static union dmub_fw_boot_options dm_test_fw_boot_options;

static union dmub_fw_boot_options dm_test_get_fw_boot_option(struct dmub_srv *dmub)
{
	return dm_test_fw_boot_options;
}

/**
 * dm_test_late_init_boot_crc_disabled - Test a disabled boot time CRC allocates nothing
 * @test: The KUnit test context
 */
static void dm_test_late_init_boot_crc_disabled(struct kunit *test)
{
	struct amdgpu_device *adev = dm_test_late_init_adev(test);
	struct dc_dmub_srv *dmub_srv;
	struct dmub_srv *dmub;

	dmub_srv = kunit_kzalloc(test, sizeof(*dmub_srv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dmub_srv);
	dmub = kunit_kzalloc(test, sizeof(*dmub), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dmub);

	dm_test_fw_boot_options.bits.bootcrc_en_at_S0i3 = 0;
	dmub->hw_funcs.get_fw_boot_option = dm_test_get_fw_boot_option;
	dmub_srv->dmub = dmub;
	adev->dm.dc->ctx->dmub_srv = dmub_srv;
	adev->ip_versions[DCE_HWIP][0] = IP_VERSION(3, 6, 0);

	KUNIT_EXPECT_EQ(test, dm_test_run_late_init(adev), 0);
	KUNIT_EXPECT_NULL(test, adev->dm.boot_time_crc_info.bo_ptr);
}

/* Tests for amdgpu_dm_mode_config_init() */

static void dm_test_fini_atomic_obj(void *ctx)
{
	drm_atomic_private_obj_fini(ctx);
}

static void dm_test_restore_audio_param(void *ctx)
{
	amdgpu_dm_audio_set_param((long)ctx);
}

/*
 * A device ready for mode config init: DM creates its private object state
 * from the current DC state, and audio is disabled so no audio component is
 * left registered on the mock device.
 */
static struct amdgpu_device *dm_test_mode_config_adev(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	long saved_audio = amdgpu_dm_audio_get_param();

	adev->dm.dc = dm_kunit_alloc_dc_with_ctx(test);
	adev->dm.dc->current_state = dm_kunit_alloc_dc_state(test);
	KUNIT_ASSERT_NOT_NULL(test, adev->dm.dc->current_state);

	amdgpu_dm_audio_set_param(0);
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test, dm_test_restore_audio_param,
							(void *)saved_audio), 0);

	return adev;
}

/**
 * dm_test_mode_config_init - Test the mode config and DM private object are set up
 * @test: The KUnit test context
 */
static void dm_test_mode_config_init(struct kunit *test)
{
	struct amdgpu_device *adev = dm_test_mode_config_adev(test);
	struct dm_atomic_state *dm_state;

	KUNIT_ASSERT_EQ(test, amdgpu_dm_mode_config_init(adev), 0);
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test, dm_test_fini_atomic_obj,
							&adev->dm.atomic_obj), 0);

	KUNIT_EXPECT_TRUE(test, adev->mode_info.mode_config_initialized);
	KUNIT_EXPECT_EQ(test, adev->ddev.mode_config.max_width, 16384);
	KUNIT_EXPECT_EQ(test, adev->ddev.mode_config.max_height, 16384);
	KUNIT_EXPECT_EQ(test, adev->ddev.mode_config.preferred_depth, 24);
	KUNIT_EXPECT_EQ(test, adev->ddev.mode_config.prefer_shadow, 1);
	KUNIT_EXPECT_TRUE(test, adev->ddev.mode_config.async_page_flip);

	/* drm_atomic_private_obj_init() creates the state through DM. */
	dm_state = to_dm_atomic_state(adev->dm.atomic_obj.state);
	KUNIT_ASSERT_NOT_NULL(test, dm_state);
	KUNIT_EXPECT_NOT_NULL(test, dm_state->context);
}

/**
 * dm_test_mode_config_init_hawaii - Test Hawaii disables the preferred shadow
 * @test: The KUnit test context
 */
static void dm_test_mode_config_init_hawaii(struct kunit *test)
{
	struct amdgpu_device *adev = dm_test_mode_config_adev(test);

	adev->asic_type = CHIP_HAWAII;

	KUNIT_ASSERT_EQ(test, amdgpu_dm_mode_config_init(adev), 0);
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test, dm_test_fini_atomic_obj,
							&adev->dm.atomic_obj), 0);

	KUNIT_EXPECT_EQ(test, adev->ddev.mode_config.prefer_shadow, 0);
}

/* Tests for initialize_plane() */

/**
 * dm_test_initialize_plane_primary - Test a primary plane is stored in mode info
 * @test: The KUnit test context
 */
static void dm_test_initialize_plane_primary(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct amdgpu_mode_info *mode_info = &adev->mode_info;

	adev->family = AMDGPU_FAMILY_NV;
	adev->dm.adev = adev;
	adev->dm.dc = dm_kunit_alloc_dc_with_ctx(test);
	adev->dm.dc->caps.max_streams = 1;

	KUNIT_ASSERT_EQ(test, initialize_plane(&adev->dm, mode_info, 0,
					       DRM_PLANE_TYPE_PRIMARY, NULL), 0);

	KUNIT_ASSERT_NOT_NULL(test, mode_info->planes[0]);
	KUNIT_EXPECT_EQ(test, (int)mode_info->planes[0]->type,
			(int)DRM_PLANE_TYPE_PRIMARY);
	KUNIT_EXPECT_EQ(test, mode_info->planes[0]->possible_crtcs, 1U);
}

/**
 * dm_test_initialize_plane_overlay - Test an overlay plane can target any CRTC
 * @test: The KUnit test context
 */
static void dm_test_initialize_plane_overlay(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct dc_plane_cap *plane_cap;

	plane_cap = kunit_kzalloc(test, sizeof(*plane_cap), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, plane_cap);

	adev->family = AMDGPU_FAMILY_NV;
	adev->dm.adev = adev;
	adev->dm.dc = dm_kunit_alloc_dc_with_ctx(test);
	adev->dm.dc->caps.max_streams = 1;
	plane_cap->per_pixel_alpha = true;
	plane_cap->pixel_format_support.nv12 = true;

	/* Plane id at or above max_streams is never a primary, so any CRTC works. */
	KUNIT_ASSERT_EQ(test, initialize_plane(&adev->dm, NULL, 1,
					       DRM_PLANE_TYPE_OVERLAY, plane_cap), 0);
}

/* Tests for dm_gpureset_toggle_interrupts() */

/**
 * dm_test_gpureset_toggle_interrupts_dcn - Test DCN only toggles the vupdate IRQ
 * @test: The KUnit test context
 */
static void dm_test_gpureset_toggle_interrupts_dcn(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct dc_state *state = dm_kunit_alloc_dc_state(test);
	struct amdgpu_crtc *acrtc;

	KUNIT_ASSERT_NOT_NULL(test, state);
	acrtc = kunit_kzalloc(test, sizeof(*acrtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, acrtc);

	adev->ip_versions[DCE_HWIP][0] = IP_VERSION(3, 2, 0);
	adev->mode_info.num_crtc = 1;
	adev->mode_info.crtcs[0] = acrtc;
	acrtc->base.dev = &adev->ddev;
	acrtc->otg_inst = 0;
	state->stream_count = 1;
	state->stream_status[0].primary_otg_inst = 0;
	state->stream_status[0].plane_count = 1;

	/* GRPH_PFLIP is unused on DCN, so only the vupdate IRQ is touched. */
	dm_gpureset_toggle_interrupts(adev, state, true);
}

static struct kunit_case amdgpu_dm_tests[] = {
	/* Simple DM callbacks */
	KUNIT_CASE(dm_test_wait_for_idle),
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
	KUNIT_CASE(dm_test_atomic_destroy_state_no_context),
	/* dm_plane_layer_index_cmp */
	KUNIT_CASE(dm_test_plane_layer_index_cmp_equal),
	KUNIT_CASE(dm_test_plane_layer_index_cmp_descending),
	KUNIT_CASE(dm_test_plane_layer_index_cmp_ascending),
	KUNIT_CASE(dm_test_update_planes_adapter_sorts_and_forwards),
	KUNIT_CASE(dm_test_update_planes_adapter_propagates_failure),
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
	/* is_scaling_state_different */
	KUNIT_CASE(dm_test_scaling_state_same),
	KUNIT_CASE(dm_test_scaling_state_scaling_changed),
	KUNIT_CASE(dm_test_scaling_state_underscan_enabled),
	KUNIT_CASE(dm_test_scaling_state_underscan_disabled),
	KUNIT_CASE(dm_test_scaling_state_underscan_border_changed),
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
	/* dm_enable_per_frame_crtc_master_sync */
	KUNIT_CASE(dm_test_per_frame_master_sync_single_stream),
	KUNIT_CASE(dm_test_per_frame_master_sync_two_streams),
	KUNIT_CASE(dm_test_per_frame_master_sync_skips_null_stream),
	/* amdgpu_dm_apply_delay_after_dpcd_poweroff */
	KUNIT_CASE(dm_test_apply_delay_null_sink),
	KUNIT_CASE(dm_test_apply_delay_zero_wait),
	KUNIT_CASE(dm_test_apply_delay_nonzero_wait),
	/* dm_vblank_get_counter / dm_crtc_get_scanoutpos with a stream */
	KUNIT_CASE(dm_test_vblank_get_counter_unmapped_stream),
	KUNIT_CASE(dm_test_crtc_get_scanoutpos_unmapped_stream),
	KUNIT_CASE(dm_test_crtc_get_scanoutpos_exits_idle),
	/* dm_atomic_get_state / dm_atomic_duplicate_state */
	KUNIT_CASE(dm_test_atomic_get_state_already_acquired),
	KUNIT_CASE(dm_test_atomic_duplicate_state_no_context),
	/* add_affected_mst_dsc_crtcs */
	KUNIT_CASE(dm_test_add_affected_mst_dsc_crtcs_no_connector),
	KUNIT_CASE(dm_test_add_affected_mst_dsc_crtcs_writeback),
	KUNIT_CASE(dm_test_add_affected_mst_dsc_crtcs_not_mst),
	KUNIT_CASE(dm_test_add_affected_mst_dsc_crtcs_other_crtc),
	KUNIT_CASE(dm_test_add_affected_mst_dsc_crtcs_disabled),
	/* amdgpu_dm_commit_cursors */
	KUNIT_CASE(dm_test_commit_cursors_no_planes),
	KUNIT_CASE(dm_test_commit_cursors_skips_non_cursor),
	KUNIT_CASE(dm_test_commit_cursors_updates_cursor),
	KUNIT_CASE(dm_test_update_cursor_no_framebuffer),
	KUNIT_CASE(dm_test_update_cursor_disables_stream),
	/* dm_arm_vblank_event */
	KUNIT_CASE(dm_test_arm_vblank_event_no_event),
	KUNIT_CASE(dm_test_arm_vblank_event_no_active_planes),
	KUNIT_CASE(dm_test_arm_vblank_event_pflip),
	KUNIT_CASE(dm_test_arm_vblank_event_cursor),
	KUNIT_CASE(dm_test_arm_vblank_pre_programming_no_event),
	KUNIT_CASE(dm_test_arm_vblank_pre_programming_no_planes),
	KUNIT_CASE(dm_test_arm_vblank_pre_programming_update),
	/* dm_update_pflip_irq_state */
	KUNIT_CASE(dm_test_update_pflip_irq_state_dcn),
	KUNIT_CASE(dm_test_update_pflip_irq_state_dce),
	/* amdgpu_dm_crtc_mem_type_changed */
	KUNIT_CASE(dm_test_mem_type_changed_no_planes),
	KUNIT_CASE(dm_test_mem_type_changed_missing_state),
	KUNIT_CASE(dm_test_mem_type_changed_same_domain),
	KUNIT_CASE(dm_test_mem_type_changed_different_domain),
	/* fill_dc_dirty_rects */
	KUNIT_CASE(dm_test_dirty_rects_cursor_plane),
	KUNIT_CASE(dm_test_dirty_rects_rotation_ffu),
	KUNIT_CASE(dm_test_dirty_rects_no_clips_ffu),
	KUNIT_CASE(dm_test_dirty_rects_ignored_damage_clips),
	KUNIT_CASE(dm_test_dirty_rects_too_many_clips_ffu),
	KUNIT_CASE(dm_test_dirty_rects_damage_clips),
	KUNIT_CASE(dm_test_dirty_rects_mpo_bb_changed),
	KUNIT_CASE(dm_test_dirty_rects_mpo_fb_changed),
	KUNIT_CASE(dm_test_dirty_rects_psr_su_ffu),
	KUNIT_CASE(dm_test_dirty_rects_mpo_clips),
	KUNIT_CASE(dm_test_dirty_rects_mpo_overflow_ffu),
	/* should_reset_plane */
	KUNIT_CASE(dm_test_reset_plane_pre_dcn32_modeset),
	KUNIT_CASE(dm_test_reset_plane_writeback_job),
	KUNIT_CASE(dm_test_reset_plane_crtc_changed),
	KUNIT_CASE(dm_test_reset_plane_not_in_context),
	KUNIT_CASE(dm_test_reset_plane_no_new_crtc_state),
	KUNIT_CASE(dm_test_reset_plane_cursor_mode_changed),
	KUNIT_CASE(dm_test_reset_plane_color_mgmt_changed),
	KUNIT_CASE(dm_test_reset_plane_zpos_changed),
	KUNIT_CASE(dm_test_reset_plane_modeset),
	KUNIT_CASE(dm_test_reset_plane_fast_update),
	KUNIT_CASE(dm_test_reset_plane_skips_non_writeback),
	KUNIT_CASE(dm_test_reset_plane_other_cursor_skipped),
	KUNIT_CASE(dm_test_reset_plane_other_crtc_skipped),
	KUNIT_CASE(dm_test_reset_plane_other_crtc_moved),
	KUNIT_CASE(dm_test_reset_plane_other_scaling),
	KUNIT_CASE(dm_test_reset_plane_other_rotation),
	KUNIT_CASE(dm_test_reset_plane_other_blending),
	KUNIT_CASE(dm_test_reset_plane_other_alpha),
	KUNIT_CASE(dm_test_reset_plane_other_colorspace),
	KUNIT_CASE(dm_test_reset_plane_other_hdr_mult),
	KUNIT_CASE(dm_test_reset_plane_other_no_fb),
	KUNIT_CASE(dm_test_reset_plane_other_format),
	KUNIT_CASE(dm_test_reset_plane_other_modifier),
	/* amdgpu_dm_dump_links_and_sinks */
	KUNIT_CASE(dm_test_dump_links_no_dc),
	KUNIT_CASE(dm_test_dump_links_no_links),
	KUNIT_CASE(dm_test_dump_links_with_sinks),
	KUNIT_CASE(dm_test_dump_links_unnamed_sinks),
	/* commit-tail helpers */
	KUNIT_CASE(dm_test_update_hdcp_no_workqueue),
	KUNIT_CASE(dm_test_update_hdcp_writeback_skipped),
	KUNIT_CASE(dm_test_update_hdcp_unchanged),
	KUNIT_CASE(dm_test_atomic_setup_commit_empty),
	KUNIT_CASE(dm_test_atomic_setup_commit_color_mgmt),
	KUNIT_CASE(dm_test_atomic_setup_commit_modeset),
	KUNIT_CASE(dm_test_atomic_setup_commit_bad_lut),
	KUNIT_CASE(dm_test_atomic_check_empty),
	KUNIT_CASE(dm_test_aquire_global_lock_no_crtc),
	KUNIT_CASE(dm_test_aquire_global_lock_no_commit),
	KUNIT_CASE(dm_test_aquire_global_lock_waits_commit),
	KUNIT_CASE(dm_test_update_crtc_state_unchanged),
	KUNIT_CASE(dm_test_update_plane_state_detached),
	KUNIT_CASE(dm_test_mod_power_update_streams_empty),
	KUNIT_CASE(dm_test_mod_power_update_streams_no_modeset),
	KUNIT_CASE(dm_test_mod_power_update_streams_enable),
	KUNIT_CASE(dm_test_mod_power_update_streams_replace),
	KUNIT_CASE(dm_test_mod_power_update_streams_disable),
	KUNIT_CASE(dm_test_mod_power_setup_streams_empty),
	KUNIT_CASE(dm_test_mod_power_setup_streams_modeset),
	KUNIT_CASE(dm_test_mod_power_setup_streams_no_modeset),
	/* amdgpu_dm_trigger_timing_sync */
	KUNIT_CASE(dm_test_trigger_timing_sync_no_state),
	KUNIT_CASE(dm_test_trigger_timing_sync_streams),
	/* dm_acpi_process_phy_transition_interlock */
	KUNIT_CASE(dm_test_acpi_phy_transition_interlock),
	/* IP block lifecycle helpers */
	KUNIT_CASE(dm_test_early_fini_audio_disabled),
	KUNIT_CASE(dm_test_sw_fini_releases_state),
	KUNIT_CASE(dm_test_oem_i2c_hw_init_no_device),
	KUNIT_CASE(dm_test_resume_mst_no_primary),
	KUNIT_CASE(dm_test_s3_handle_mst_empty),
	KUNIT_CASE(dm_test_gpureset_commit_state_no_streams),
	KUNIT_CASE(dm_test_emulated_link_detect_bad_signal),
	/* mmhub_read_system_context */
	KUNIT_CASE(dm_test_mmhub_agp_disabled),
	KUNIT_CASE(dm_test_mmhub_agp_disabled_raven2),
	KUNIT_CASE(dm_test_mmhub_agp_enabled),
	KUNIT_CASE(dm_test_mmhub_agp_enabled_renoir),
	/* amdgpu_dm_init_power_module */
	KUNIT_CASE(dm_test_init_power_module_no_edp),
	KUNIT_CASE(dm_test_init_power_module_alloc_failure),
	/* fill_dc_plane_info_and_addr */
	KUNIT_CASE(dm_test_plane_info_graphics_formats),
	KUNIT_CASE(dm_test_plane_info_video_formats),
	KUNIT_CASE(dm_test_plane_info_unsupported_format),
	KUNIT_CASE(dm_test_plane_info_bad_color_encoding),
	KUNIT_CASE(dm_test_plane_info_rotations),
	KUNIT_CASE(dm_test_plane_info_layer_and_blending),
	/* amdgpu_dm_enable_self_refresh */
	KUNIT_CASE(dm_test_self_refresh_full_update),
	KUNIT_CASE(dm_test_self_refresh_unsupported_link),
	KUNIT_CASE(dm_test_self_refresh_decrements_skip_count),
	KUNIT_CASE(dm_test_self_refresh_allows_entry),
	KUNIT_CASE(dm_test_self_refresh_within_settle_window),
	KUNIT_CASE(dm_test_self_refresh_replay_link),
	/* manage_dm_interrupts */
	KUNIT_CASE(dm_test_manage_interrupts_offdelay),
	KUNIT_CASE(dm_test_manage_interrupts_offdelay_fallback),
	KUNIT_CASE(dm_test_manage_interrupts_apu_instant_off),
	KUNIT_CASE(dm_test_manage_interrupts_disable),
	/* dm_early_init */
	KUNIT_CASE(dm_test_early_init_no_object_header),
	KUNIT_CASE(dm_test_early_init_legacy_asics),
	KUNIT_CASE(dm_test_early_init_dcn_versions),
	KUNIT_CASE(dm_test_early_init_unsupported_version),
	/* suspend and resume helpers */
	KUNIT_CASE(dm_test_commit_zero_streams_empty),
	KUNIT_CASE(dm_test_cache_state_empty_device),
	KUNIT_CASE(dm_test_cache_state_error),
	KUNIT_CASE(dm_test_destroy_cached_state_none),
	KUNIT_CASE(dm_test_clear_writeback_removes_stream),
	KUNIT_CASE(dm_test_set_writeback_no_pipe),
	/* dm_update_mst_vcpi_slots_for_dsc */
	KUNIT_CASE(dm_test_mst_vcpi_slots_no_connector),
	KUNIT_CASE(dm_test_mst_vcpi_slots_skips_writeback),
	KUNIT_CASE(dm_test_mst_vcpi_slots_skips_non_mst),
	KUNIT_CASE(dm_test_mst_vcpi_slots_no_matching_stream),
	/* fill_dc_plane_attributes */
	KUNIT_CASE(dm_test_plane_attributes_success),
	KUNIT_CASE(dm_test_plane_attributes_bad_scaling),
	KUNIT_CASE(dm_test_plane_attributes_bad_format),
	KUNIT_CASE(dm_test_plane_attributes_bad_color_mgmt),
	/* load_dmcu_fw */
	KUNIT_CASE(dm_test_load_dmcu_fw_no_dmcu),
	KUNIT_CASE(dm_test_load_dmcu_fw_dcn),
	KUNIT_CASE(dm_test_load_dmcu_fw_unsupported),
	KUNIT_CASE(dm_test_load_dmcu_fw_raven),
	KUNIT_CASE(dm_test_load_dmcu_fw_missing_firmware),
	/* dm_sw_init / dm_sw_fini */
	KUNIT_CASE(dm_test_sw_init_no_dmub),
	KUNIT_CASE(dm_test_sw_fini_releases_bounding_box),
	/* dm_late_init */
	KUNIT_CASE(dm_test_late_init_no_dmcu),
	KUNIT_CASE(dm_test_late_init_boot_crc_no_dmub),
	KUNIT_CASE(dm_test_late_init_boot_crc_disabled),
	/* amdgpu_dm_mode_config_init */
	KUNIT_CASE(dm_test_mode_config_init),
	KUNIT_CASE(dm_test_mode_config_init_hawaii),
	/* initialize_plane */
	KUNIT_CASE(dm_test_initialize_plane_primary),
	KUNIT_CASE(dm_test_initialize_plane_overlay),
	/* dm_gpureset_toggle_interrupts */
	KUNIT_CASE(dm_test_gpureset_toggle_interrupts_dcn),
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
