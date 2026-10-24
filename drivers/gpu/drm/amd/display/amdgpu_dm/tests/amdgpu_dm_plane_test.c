// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * KUnit tests for amdgpu_dm_plane.c
 *
 * Copyright 2026 Advanced Micro Devices, Inc.
 */

#include <kunit/test.h>
#include <drm/drm_atomic.h>
#include <drm/drm_blend.h>
#include "link_enc_cfg.h"
#include "amdgpu_dm_plane.h"
#include "amdgpu_dm_kunit_test_helpers.h"
#include "amdgpu_rlc.h"
#include "gc/gc_11_0_0_offset.h"
#include "gc/gc_11_0_0_sh_mask.h"
#include <drm/amdgpu_drm.h>
#include <drm/drm_mode_config.h>
#include <drm/drm_plane.h>
#include <drm/drm_property.h>

struct dm_test_dcc_cap_ctx {
	bool callback_ret;
	bool capable;
	bool output_independent_64b_blks;
	bool called;
	struct dc_dcc_surface_param captured_input;
};

static struct dm_test_dcc_cap_ctx *dm_test_dcc_ctx;

struct dm_test_gfx11_reg_ctx {
	u32 gb_addr_config;
	u32 gc_reg_offsets[1];
	u32 expected_reg;
	u32 captured_reg;
	u32 captured_acc_flags;
	u32 captured_hwip;
	u32 captured_xcc_id;
	bool called;
};

static struct dm_test_gfx11_reg_ctx *dm_test_gfx11_reg_ctx;

static u32 dm_test_gfx11_rreg32(struct amdgpu_device *adev,
				u32 reg, u32 acc_flags, u32 hwip,
				u32 xcc_id)
{
	if (!dm_test_gfx11_reg_ctx)
		return 0;

	dm_test_gfx11_reg_ctx->called = true;
	dm_test_gfx11_reg_ctx->captured_reg = reg;
	dm_test_gfx11_reg_ctx->captured_acc_flags = acc_flags;
	dm_test_gfx11_reg_ctx->captured_hwip = hwip;
	dm_test_gfx11_reg_ctx->captured_xcc_id = xcc_id;

	return dm_test_gfx11_reg_ctx->gb_addr_config;
}

static const struct amdgpu_rlc_reg_funcs dm_test_gfx11_reg_funcs = {
	.rreg32 = dm_test_gfx11_rreg32,
};

static bool dm_test_get_dcc_compression_cap(const struct dc *dc,
					    const struct dc_dcc_surface_param *input,
					    struct dc_surface_dcc_cap *output)
{
	if (!dm_test_dcc_ctx)
		return false;

	dm_test_dcc_ctx->called = true;
	dm_test_dcc_ctx->captured_input = *input;
	output->capable = dm_test_dcc_ctx->capable;
	output->grph.rgb.independent_64b_blks = dm_test_dcc_ctx->output_independent_64b_blks;

	return dm_test_dcc_ctx->callback_ret;
}

static void dm_test_init_validate_dcc_inputs(struct amdgpu_device **adev,
					     struct dc **dc,
					     struct dc_tiling_info *tiling_info,
					     struct dc_plane_dcc_param *dcc,
					     struct dc_plane_address *address,
					     struct plane_size *plane_size,
					     struct kunit *test)
{
	*adev = kunit_kzalloc(test, sizeof(**adev), GFP_KERNEL);
	*dc = kunit_kzalloc(test, sizeof(**dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, *adev);
	KUNIT_ASSERT_NOT_NULL(test, *dc);

	(*adev)->dm.dc = *dc;
	(*adev)->family = AMDGPU_FAMILY_NV;

	tiling_info->gfx9.swizzle = 9;
	dcc->enable = 1;
	dcc->independent_64b_blks = 1;
	plane_size->surface_size.width = 1920;
	plane_size->surface_size.height = 1080;

	(void)address;
}


/**
 * dm_test_plane_is_video_format_known_video() - Verify known video formats.
 * @test: KUnit test context.
 *
 * Verify if NV12, NV21, and P010 are treated as video formats.
 */
static void dm_test_plane_is_video_format_known_video(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test, amdgpu_dm_plane_is_video_format(DRM_FORMAT_NV12));
	KUNIT_EXPECT_TRUE(test, amdgpu_dm_plane_is_video_format(DRM_FORMAT_NV21));
	KUNIT_EXPECT_TRUE(test, amdgpu_dm_plane_is_video_format(DRM_FORMAT_P010));
}

/**
 * dm_test_fill_blending_pixel_blend_modes() - Verify per-pixel blending modes.
 * @test: KUnit test context.
 *
 * Verify if opaque alpha keeps the default outputs, and if the premultiplied
 * and coverage modes enable per-pixel alpha for an alpha-capable format.
 */
static void dm_test_fill_blending_pixel_blend_modes(struct kunit *test)
{
	static const struct {
		const char *name;
		unsigned int blend_mode;
		bool with_fb;
		bool per_pixel_alpha;
		bool pre_multiplied_alpha;
	} cases[] = {
		{ "defaults", DRM_MODE_BLEND_PIXEL_NONE, false, false, true },
		{ "premultiplied", DRM_MODE_BLEND_PREMULTI, true, true, true },
		{ "coverage", DRM_MODE_BLEND_COVERAGE, true, true, false },
	};
	struct drm_framebuffer fb = { 0 };
	unsigned int i;

	fb.format = drm_format_info(DRM_FORMAT_ARGB8888);
	KUNIT_ASSERT_NOT_NULL(test, fb.format);

	for (i = 0; i < ARRAY_SIZE(cases); i++) {
		struct drm_plane_state state = { 0 };
		bool per_pixel_alpha;
		bool pre_multiplied_alpha;
		bool global_alpha;
		int global_alpha_value;

		if (cases[i].with_fb)
			state.fb = &fb;
		state.pixel_blend_mode = cases[i].blend_mode;
		state.alpha = 0xffff;

		amdgpu_dm_plane_fill_blending_from_plane_state(&state,
							       &per_pixel_alpha,
							       &pre_multiplied_alpha,
							       &global_alpha,
							       &global_alpha_value);

		KUNIT_EXPECT_EQ_MSG(test, per_pixel_alpha, cases[i].per_pixel_alpha,
				    "%s", cases[i].name);
		KUNIT_EXPECT_EQ_MSG(test, pre_multiplied_alpha, cases[i].pre_multiplied_alpha,
				    "%s", cases[i].name);
		KUNIT_EXPECT_FALSE_MSG(test, global_alpha, "%s", cases[i].name);
		KUNIT_EXPECT_EQ_MSG(test, global_alpha_value, 0xff, "%s", cases[i].name);
	}
}

/**
 * dm_test_fill_blending_global_alpha() - Verify global alpha down-scaling.
 * @test: KUnit test context.
 *
 * Verify if global alpha is enabled and the 16-bit DRM alpha is scaled down to
 * the 8-bit hardware field, or to the 12-bit field used by DCN 4.2.
 */
static void dm_test_fill_blending_global_alpha(struct kunit *test)
{
	static const struct {
		const char *name;
		u32 dce_version;
		int expected;
	} cases[] = {
		{ "8-bit field", IP_VERSION(4, 0, 0), 0x80 },
		{ "DCN 4.2 12-bit field", IP_VERSION(4, 2, 0), 0x800 },
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(cases); i++) {
		struct amdgpu_device *adev;
		struct drm_plane *plane;
		struct drm_plane_state *state;
		bool per_pixel_alpha;
		bool pre_multiplied_alpha;
		bool global_alpha;
		int global_alpha_value;

		adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
		plane = kunit_kzalloc(test, sizeof(*plane), GFP_KERNEL);
		state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
		KUNIT_ASSERT_NOT_NULL(test, adev);
		KUNIT_ASSERT_NOT_NULL(test, plane);
		KUNIT_ASSERT_NOT_NULL(test, state);

		adev->ip_versions[DCE_HWIP][0] = cases[i].dce_version;
		plane->dev = &adev->ddev;
		state->plane = plane;
		state->pixel_blend_mode = DRM_MODE_BLEND_PIXEL_NONE;
		state->alpha = 0x8000;

		amdgpu_dm_plane_fill_blending_from_plane_state(state,
							       &per_pixel_alpha,
							       &pre_multiplied_alpha,
							       &global_alpha,
							       &global_alpha_value);

		KUNIT_EXPECT_FALSE_MSG(test, per_pixel_alpha, "%s", cases[i].name);
		KUNIT_EXPECT_TRUE_MSG(test, pre_multiplied_alpha, "%s", cases[i].name);
		KUNIT_EXPECT_TRUE_MSG(test, global_alpha, "%s", cases[i].name);
		KUNIT_EXPECT_EQ_MSG(test, global_alpha_value, cases[i].expected,
				    "%s", cases[i].name);
	}
}

/**
 * dm_test_modifier_has_dcc() - Verify helper detects AMD DCC modifiers.
 * @test: KUnit test context.
 *
 * Verify if DCC detection works for linear and AMD DCC modifiers.
 */
static void dm_test_modifier_has_dcc(struct kunit *test)
{
	uint64_t dcc_mod = AMD_FMT_MOD | AMD_FMT_MOD_SET(DCC, 1);

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_plane_modifier_has_dcc(DRM_FORMAT_MOD_LINEAR));
	KUNIT_EXPECT_TRUE(test, amdgpu_dm_plane_modifier_has_dcc(dcc_mod));
}

/**
 * dm_test_modifier_gfx9_swizzle_mode() - Verify swizzle helper for linear and AMD modifiers.
 * @test: KUnit test context.
 *
 * Verify if swizzle mode decoding works for linear and AMD tiled modifiers.
 */
static void dm_test_modifier_gfx9_swizzle_mode(struct kunit *test)
{
	uint64_t mod = AMD_FMT_MOD | AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX9_64K_S_X);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_plane_modifier_gfx9_swizzle_mode(DRM_FORMAT_MOD_LINEAR), 0U);
	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_plane_modifier_gfx9_swizzle_mode(mod),
			(unsigned int)AMD_FMT_MOD_TILE_GFX9_64K_S_X);
}

/**
 * dm_test_get_plane_formats() - Verify plane format counts for key plane types.
 * @test: KUnit test context.
 *
 * Verify if returned format counts match primary, overlay, and cursor planes,
 * and if an overlay with a DCN universal plane cap reports the RGB list
 * instead of the overlay-only list.
 */
static void dm_test_get_plane_formats(struct kunit *test)
{
	struct drm_plane *plane;
	struct dc_plane_cap *cap;
	struct dc_plane_cap *universal_cap;
	uint32_t formats[32] = {0};

	plane = kunit_kzalloc(test, sizeof(*plane), GFP_KERNEL);
	cap = kunit_kzalloc(test, sizeof(*cap), GFP_KERNEL);
	universal_cap = kunit_kzalloc(test, sizeof(*universal_cap), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, plane);
	KUNIT_ASSERT_NOT_NULL(test, cap);
	KUNIT_ASSERT_NOT_NULL(test, universal_cap);

	plane->type = DRM_PLANE_TYPE_PRIMARY;
	KUNIT_EXPECT_EQ(test, amdgpu_dm_plane_get_plane_formats(plane, NULL, formats, 32), 14);

	cap->pixel_format_support.nv12 = true;
	cap->pixel_format_support.p010 = true;
	cap->pixel_format_support.fp16 = true;
	KUNIT_EXPECT_EQ(test, amdgpu_dm_plane_get_plane_formats(plane, cap, formats, 32), 20);

	plane->type = DRM_PLANE_TYPE_OVERLAY;
	KUNIT_EXPECT_EQ(test, amdgpu_dm_plane_get_plane_formats(plane, NULL, formats, 32), 9);

	universal_cap->type = DC_PLANE_TYPE_DCN_UNIVERSAL;
	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_plane_get_plane_formats(plane, universal_cap, formats, 32),
			14);

	plane->type = DRM_PLANE_TYPE_CURSOR;
	KUNIT_EXPECT_EQ(test, amdgpu_dm_plane_get_plane_formats(plane, NULL, formats, 32), 1);
}

/**
 * dm_test_get_plane_modifiers() - Verify early-return and cursor modifier list.
 * @test: KUnit test context.
 *
 * Verify if modifier list handling works for unsupported families and cursor planes.
 */
static void dm_test_get_plane_modifiers(struct kunit *test)
{
	struct amdgpu_device *adev;
	uint64_t *mods = NULL;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, adev);

	adev->family = AMDGPU_FAMILY_NV;
	KUNIT_ASSERT_EQ(test,
			amdgpu_dm_plane_get_plane_modifiers(adev, DRM_PLANE_TYPE_CURSOR, &mods),
			0);
	KUNIT_ASSERT_NOT_NULL(test, mods);
	KUNIT_EXPECT_EQ(test, mods[0], DRM_FORMAT_MOD_LINEAR);
	KUNIT_EXPECT_EQ(test, mods[1], DRM_FORMAT_MOD_INVALID);
	kfree(mods);
}

/**
 * dm_test_fill_dc_scaling_info() - Verify basic error and success paths.
 * @test: KUnit test context.
 *
 * Verify if scaling info rejects invalid sizes and accepts valid sizes.
 */
static void dm_test_fill_dc_scaling_info(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct drm_plane_state state = {0};
	struct dc_scaling_info info = {0};

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, adev);

	state.src_w = 0;
	state.src_h = 100 << 16;
	state.crtc_w = 100;
	state.crtc_h = 100;
	KUNIT_EXPECT_EQ(test, amdgpu_dm_plane_fill_dc_scaling_info(adev, &state, &info), -EINVAL);

	state.src_w = 100 << 16;
	state.src_h = 100 << 16;
	state.crtc_w = 100;
	state.crtc_h = 100;
	KUNIT_EXPECT_EQ(test, amdgpu_dm_plane_fill_dc_scaling_info(adev, &state, &info), 0);
}

/**
 * dm_test_get_min_max_dc_plane_scaling() - Verify format-specific cap selection and 1->1000 conversion.
 * @test: KUnit test context.
 *
 * Verify if min/max scaling values are correct for the NV12, XRGB8888 and fp16
 * caps.
 */
static void dm_test_get_min_max_dc_plane_scaling(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dc *dc;
	struct drm_framebuffer *fb;
	int min_downscale = 0;
	int max_upscale = 0;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	fb = kunit_kzalloc(test, sizeof(*fb), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, dc);
	KUNIT_ASSERT_NOT_NULL(test, fb);

	adev->dm.dc = dc;
	dc->caps.planes[0].max_upscale_factor.nv12 = 1;
	dc->caps.planes[0].max_downscale_factor.nv12 = 1;
	dc->caps.planes[0].max_upscale_factor.argb8888 = 1600;
	dc->caps.planes[0].max_downscale_factor.argb8888 = 250;
	dc->caps.planes[0].max_upscale_factor.fp16 = 2000;
	dc->caps.planes[0].max_downscale_factor.fp16 = 500;

	fb->format = drm_format_info(DRM_FORMAT_NV12);
	KUNIT_ASSERT_NOT_NULL(test, fb->format);
	amdgpu_dm_plane_get_min_max_dc_plane_scaling(&adev->ddev, fb, &min_downscale, &max_upscale);
	KUNIT_EXPECT_EQ(test, min_downscale, 1000);
	KUNIT_EXPECT_EQ(test, max_upscale, 1000);

	fb->format = drm_format_info(DRM_FORMAT_XRGB8888);
	KUNIT_ASSERT_NOT_NULL(test, fb->format);
	amdgpu_dm_plane_get_min_max_dc_plane_scaling(&adev->ddev, fb, &min_downscale, &max_upscale);
	KUNIT_EXPECT_EQ(test, min_downscale, 250);
	KUNIT_EXPECT_EQ(test, max_upscale, 1600);

	fb->format = drm_format_info(DRM_FORMAT_ARGB16161616F);
	KUNIT_ASSERT_NOT_NULL(test, fb->format);
	amdgpu_dm_plane_get_min_max_dc_plane_scaling(&adev->ddev, fb, &min_downscale, &max_upscale);
	KUNIT_EXPECT_EQ(test, min_downscale, 500);
	KUNIT_EXPECT_EQ(test, max_upscale, 2000);
}

/**
 * dm_test_get_cursor_position() - Verify cursor clipping and off-screen handling.
 * @test: KUnit test context.
 *
 * Verify if cursor clipping, hotspot adjustment, off-screen disable behavior
 * and the oversized-cursor rejection work.
 */
static void dm_test_get_cursor_position(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct amdgpu_crtc *amdgpu_crtc;
	struct drm_plane *plane;
	struct drm_plane_state *state;
	struct drm_framebuffer *fb;
	struct dc_cursor_position position = {0};

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	amdgpu_crtc = kunit_kzalloc(test, sizeof(*amdgpu_crtc), GFP_KERNEL);
	plane = kunit_kzalloc(test, sizeof(*plane), GFP_KERNEL);
	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	fb = kunit_kzalloc(test, sizeof(*fb), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, amdgpu_crtc);
	KUNIT_ASSERT_NOT_NULL(test, plane);
	KUNIT_ASSERT_NOT_NULL(test, state);
	KUNIT_ASSERT_NOT_NULL(test, fb);

	adev->ip_versions[DCE_HWIP][0] = IP_VERSION(4, 0, 0);
	amdgpu_crtc->max_cursor_width = 64;
	amdgpu_crtc->max_cursor_height = 64;

	plane->dev = &adev->ddev;
	plane->state = state;
	state->fb = fb;
	state->crtc_x = -5;
	state->crtc_y = -7;
	state->crtc_w = 32;
	state->crtc_h = 32;

	KUNIT_ASSERT_EQ(test,
			amdgpu_dm_plane_get_cursor_position(plane, &amdgpu_crtc->base, &position),
			0);
	KUNIT_EXPECT_TRUE(test, position.enable);
	KUNIT_EXPECT_EQ(test, position.x, 0);
	KUNIT_EXPECT_EQ(test, position.y, 0);
	KUNIT_EXPECT_EQ(test, position.x_hotspot, 5);
	KUNIT_EXPECT_EQ(test, position.y_hotspot, 7);
	KUNIT_EXPECT_TRUE(test, position.translate_by_source);

	memset(&position, 0, sizeof(position));
	state->crtc_x = -64;
	state->crtc_y = 0;
	KUNIT_ASSERT_EQ(test,
			amdgpu_dm_plane_get_cursor_position(plane, &amdgpu_crtc->base, &position),
			0);
	KUNIT_EXPECT_FALSE(test, position.enable);

	state->crtc_w = 128;
	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_plane_get_cursor_position(plane, &amdgpu_crtc->base, &position),
			-EINVAL);
}

/**
 * dm_test_format_mod_supported() - Verify key format/modifier acceptance and rejection paths.
 * @test: KUnit test context.
 *
 * Verify if format-modifier support checks match accepted and rejected cases.
 */
static void dm_test_format_mod_supported(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct drm_plane *plane;
	uint64_t listed_mod;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	plane = kunit_kzalloc(test, sizeof(*plane), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, plane);

	adev->family = AMDGPU_FAMILY_NV;
	plane->dev = &adev->ddev;

	KUNIT_EXPECT_TRUE(test,
			  amdgpu_dm_plane_format_mod_supported(plane, DRM_FORMAT_XRGB8888,
							       DRM_FORMAT_MOD_LINEAR));
	KUNIT_EXPECT_TRUE(test,
			  amdgpu_dm_plane_format_mod_supported(plane, DRM_FORMAT_XRGB8888,
							       DRM_FORMAT_MOD_INVALID));

	KUNIT_EXPECT_FALSE(test,
			   amdgpu_dm_plane_format_mod_supported(plane, DRM_FORMAT_XRGB8888,
								DRM_FORMAT_MOD_VENDOR_AMD));

	listed_mod = AMD_FMT_MOD |
		     AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX9_64K_S_X) |
		     AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX9) |
		     AMD_FMT_MOD_SET(DCC, 1);
	plane->modifiers = &listed_mod;
	plane->modifier_count = 1;

	KUNIT_EXPECT_FALSE(test,
			   amdgpu_dm_plane_format_mod_supported(plane, DRM_FORMAT_NV12, listed_mod));

	/* 4 bytes per pixel, but its extra plane collides with the DCC metadata plane. */
	KUNIT_EXPECT_FALSE(test,
			   amdgpu_dm_plane_format_mod_supported(plane, DRM_FORMAT_XRGB8888_A8,
								listed_mod));
}

/**
 * dm_test_fill_gfx9_plane_attributes_from_modifiers() - Verify basic GFX9 linear modifier path.
 * @test: KUnit test context.
 *
 * Verify if GFX9 linear modifier handling keeps DCC disabled.
 */
static void dm_test_fill_gfx9_plane_attributes_from_modifiers(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct amdgpu_framebuffer *afb;
	struct plane_size plane_size = {0};
	struct dc_tiling_info tiling_info = {0};
	struct dc_plane_dcc_param dcc = {0};
	struct dc_plane_address address = {0};

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	afb = kunit_kzalloc(test, sizeof(*afb), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, afb);

	adev->family = AMDGPU_FAMILY_NV;
	adev->gfx.config.gb_addr_config_fields.num_pipes = 2;
	adev->gfx.config.gb_addr_config_fields.num_banks = 4;
	adev->gfx.config.gb_addr_config_fields.pipe_interleave_size = 256;
	adev->gfx.config.gb_addr_config_fields.num_se = 1;
	adev->gfx.config.gb_addr_config_fields.max_compress_frags = 2;
	adev->gfx.config.gb_addr_config_fields.num_rb_per_se = 1;
	adev->gfx.config.gb_addr_config_fields.num_pkrs = 2;
	adev->ip_versions[GC_HWIP][0] = IP_VERSION(10, 3, 0);

	afb->base.modifier = DRM_FORMAT_MOD_LINEAR;

	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_plane_fill_gfx9_attrs_from_modifiers(
			adev, afb, SURFACE_PIXEL_FORMAT_GRPH_ARGB8888,
			ROTATION_ANGLE_0, &plane_size, &tiling_info, &dcc, &address),
			0);
	KUNIT_EXPECT_EQ(test, (int)tiling_info.gfxversion, (int)DcGfxVersion9);
	KUNIT_EXPECT_EQ(test, tiling_info.gfx9.swizzle, 0U);
	KUNIT_EXPECT_FALSE(test, dcc.enable);
}

/**
 * dm_test_helper_check_state_viewport_rejects() - Verify viewport rejections.
 * @test: KUnit test context.
 *
 * Verify if a viewport outside the display bounds, or one clipped by an edge
 * to below the minimum pipe-split width or height, is rejected.
 */
static void dm_test_helper_check_state_viewport_rejects(struct kunit *test)
{
	static const struct {
		const char *name;
		int crtc_x;
		int crtc_y;
		int crtc_w;
		int crtc_h;
		int hdisplay;
		int vdisplay;
	} cases[] = {
		{ "outside the display", 200, 0, 100, 100, 100, 100 },
		{ "width below minimum", 0, 0, 10, 100, 1920, 1080 },
		{ "top-clipped height", -2, -95, 100, 100, 1920, 1080 },
		{ "bottom-clipped height", 0, 95, 100, 100, 1920, 100 },
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(cases); i++) {
		struct drm_plane *plane;
		struct drm_plane_state *state;
		struct drm_crtc *crtc;
		struct drm_crtc_state *new_crtc_state;
		struct drm_framebuffer *fb;
		int ret;

		plane = kunit_kzalloc(test, sizeof(*plane), GFP_KERNEL);
		state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
		crtc = kunit_kzalloc(test, sizeof(*crtc), GFP_KERNEL);
		new_crtc_state = kunit_kzalloc(test, sizeof(*new_crtc_state), GFP_KERNEL);
		fb = kunit_kzalloc(test, sizeof(*fb), GFP_KERNEL);
		KUNIT_ASSERT_NOT_NULL(test, plane);
		KUNIT_ASSERT_NOT_NULL(test, state);
		KUNIT_ASSERT_NOT_NULL(test, crtc);
		KUNIT_ASSERT_NOT_NULL(test, new_crtc_state);
		KUNIT_ASSERT_NOT_NULL(test, fb);

		plane->type = DRM_PLANE_TYPE_OVERLAY;
		state->plane = plane;
		state->fb = fb;
		state->crtc = crtc;
		state->crtc_x = cases[i].crtc_x;
		state->crtc_y = cases[i].crtc_y;
		state->crtc_w = cases[i].crtc_w;
		state->crtc_h = cases[i].crtc_h;
		new_crtc_state->mode.crtc_hdisplay = cases[i].hdisplay;
		new_crtc_state->mode.crtc_vdisplay = cases[i].vdisplay;

		ret = amdgpu_dm_plane_helper_check_state(state, new_crtc_state);
		KUNIT_EXPECT_EQ_MSG(test, ret, -EINVAL, "%s", cases[i].name);
	}
}

/**
 * dm_test_validate_dcc_early_exits() - Verify the checks before the DC query.
 * @test: KUnit test context.
 *
 * Verify if disabled DCC is accepted without consulting DC, and if a video
 * format on a pre-GFX12 device and a missing capability callback are rejected.
 */
static void dm_test_validate_dcc_early_exits(struct kunit *test)
{
	static const struct {
		const char *name;
		bool dcc_enable;
		enum surface_pixel_format format;
		int expected;
	} cases[] = {
		{ "dcc disabled", false, SURFACE_PIXEL_FORMAT_GRPH_ARGB8888, 0 },
		{ "video before gfx12", true, SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr, -EINVAL },
		{ "no capability callback", true, SURFACE_PIXEL_FORMAT_GRPH_ARGB8888, -EINVAL },
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(cases); i++) {
		struct amdgpu_device *adev;
		struct dc *dc;
		struct dc_tiling_info tiling_info = {0};
		struct dc_plane_dcc_param dcc = {0};
		struct dc_plane_address address = {0};
		struct plane_size plane_size = {0};
		int ret;

		dm_test_init_validate_dcc_inputs(&adev, &dc, &tiling_info, &dcc, &address,
						 &plane_size, test);
		dcc.enable = cases[i].dcc_enable;

		ret = amdgpu_dm_plane_validate_dcc(adev, cases[i].format, ROTATION_ANGLE_0,
						   &tiling_info, &dcc, &address, &plane_size);
		KUNIT_EXPECT_EQ_MSG(test, ret, cases[i].expected, "%s", cases[i].name);
	}
}

/**
 * dm_test_validate_dcc_rejects_from_cap() - Verify rejections from the DC query.
 * @test: KUnit test context.
 *
 * Verify if a failing capability callback, a surface reported as not DCC
 * capable, and an independent_64b_blks mismatch are all rejected.
 */
static void dm_test_validate_dcc_rejects_from_cap(struct kunit *test)
{
	static const struct {
		const char *name;
		bool callback_ret;
		bool capable;
		bool output_independent_64b_blks;
		u32 independent_64b_blks;
	} cases[] = {
		{ "callback fails", false, true, false, 1 },
		{ "not capable", true, false, false, 1 },
		{ "independent 64B mismatch", true, true, true, 0 },
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(cases); i++) {
		struct amdgpu_device *adev;
		struct dc *dc;
		struct dc_tiling_info tiling_info = {0};
		struct dc_plane_dcc_param dcc = {0};
		struct dc_plane_address address = {0};
		struct plane_size plane_size = {0};
		struct dm_test_dcc_cap_ctx ctx = {
			.callback_ret = cases[i].callback_ret,
			.capable = cases[i].capable,
			.output_independent_64b_blks = cases[i].output_independent_64b_blks,
		};
		int ret;

		dm_test_init_validate_dcc_inputs(&adev, &dc, &tiling_info, &dcc, &address,
						 &plane_size, test);
		dcc.independent_64b_blks = cases[i].independent_64b_blks;
		dc->cap_funcs.get_dcc_compression_cap = dm_test_get_dcc_compression_cap;
		dm_test_dcc_ctx = &ctx;

		ret = amdgpu_dm_plane_validate_dcc(adev, SURFACE_PIXEL_FORMAT_GRPH_ARGB8888,
						   ROTATION_ANGLE_0, &tiling_info, &dcc,
						   &address, &plane_size);
		dm_test_dcc_ctx = NULL;

		KUNIT_EXPECT_EQ_MSG(test, ret, -EINVAL, "%s", cases[i].name);
		KUNIT_EXPECT_TRUE_MSG(test, ctx.called, "%s", cases[i].name);
	}
}

/**
 * dm_test_validate_dcc_success_and_scan_mapping() - Verify success path and rotation-to-scan mapping.
 * @test: KUnit test context.
 *
 * Verify if DCC validation succeeds and rotation-to-scan mapping is correct.
 */
static void dm_test_validate_dcc_success_and_scan_mapping(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dc *dc;
	struct dc_tiling_info tiling_info = {0};
	struct dc_plane_dcc_param dcc = {0};
	struct dc_plane_address address = {0};
	struct plane_size plane_size = {0};
	struct dm_test_dcc_cap_ctx ctx = {
		.callback_ret = true,
		.capable = true,
		.output_independent_64b_blks = true,
	};

	dm_test_init_validate_dcc_inputs(&adev, &dc, &tiling_info, &dcc, &address,
					 &plane_size, test);
	dc->cap_funcs.get_dcc_compression_cap = dm_test_get_dcc_compression_cap;
	dm_test_dcc_ctx = &ctx;

	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_plane_validate_dcc(adev, SURFACE_PIXEL_FORMAT_GRPH_ARGB8888,
			ROTATION_ANGLE_90, &tiling_info, &dcc,
			&address, &plane_size),
			0);
	KUNIT_EXPECT_TRUE(test, ctx.called);
	KUNIT_EXPECT_EQ(test, (int)ctx.captured_input.scan, (int)SCAN_DIRECTION_VERTICAL);
	KUNIT_EXPECT_EQ(test, (int)ctx.captured_input.format,
			(int)SURFACE_PIXEL_FORMAT_GRPH_ARGB8888);

	dm_test_dcc_ctx = NULL;
}

/**
 * dm_test_add_modifier_appends_and_grows() - Verify append and capacity growth.
 * @test: KUnit test context.
 *
 * Verify if a modifier is appended within the existing capacity, and if a full
 * list is grown while preserving the modifiers already stored.
 */
static void dm_test_add_modifier_appends_and_grows(struct kunit *test)
{
	uint64_t size = 0;
	uint64_t cap = 2;
	uint64_t *mods = kmalloc_array(cap, sizeof(*mods), GFP_KERNEL);

	KUNIT_ASSERT_NOT_NULL(test, mods);

	amdgpu_dm_plane_add_modifier(&mods, &size, &cap, 0x1234ULL);

	KUNIT_ASSERT_NOT_NULL(test, mods);
	KUNIT_EXPECT_EQ(test, size, 1ULL);
	KUNIT_EXPECT_EQ(test, cap, 2ULL);
	KUNIT_EXPECT_EQ(test, mods[0], 0x1234ULL);

	kfree(mods);

	size = 1;
	cap = 1;
	mods = kmalloc_array(cap, sizeof(*mods), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, mods);
	mods[0] = 0xAAULL;

	amdgpu_dm_plane_add_modifier(&mods, &size, &cap, 0xBBULL);

	KUNIT_ASSERT_NOT_NULL(test, mods);
	KUNIT_EXPECT_EQ(test, cap, 2ULL);
	KUNIT_EXPECT_EQ(test, size, 2ULL);
	KUNIT_EXPECT_EQ(test, mods[0], 0xAAULL);
	KUNIT_EXPECT_EQ(test, mods[1], 0xBBULL);

	kfree(mods);
}

/**
 * dm_test_add_modifier_failure_paths() - Verify the two failure paths.
 * @test: KUnit test context.
 *
 * Verify if the append is a no-op once the modifier list is NULL, and if a
 * failed capacity growth releases and clears the list so the caller can detect
 * the allocation failure.
 */
static void dm_test_add_modifier_failure_paths(struct kunit *test)
{
	uint64_t size = 3;
	uint64_t cap = 7;
	uint64_t *mods = NULL;

	amdgpu_dm_plane_add_modifier(&mods, &size, &cap, 0x55ULL);

	KUNIT_EXPECT_PTR_EQ(test, mods, NULL);
	KUNIT_EXPECT_EQ(test, size, 3ULL);
	KUNIT_EXPECT_EQ(test, cap, 7ULL);

	/* Doubling this capacity overflows the kmalloc_array() size product. */
	cap = 1ULL << 62;
	size = cap;
	mods = kmalloc_array(1, sizeof(*mods), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, mods);

	amdgpu_dm_plane_add_modifier(&mods, &size, &cap, 0x1234ULL);

	KUNIT_EXPECT_PTR_EQ(test, mods, NULL);
	KUNIT_EXPECT_EQ(test, size, 1ULL << 62);
}

/**
 * dm_test_fill_gfx9_tiling_info_from_device() - Verify GFX9 device field copy.
 * @test: KUnit test context.
 *
 * Verify if the device tiling fields are copied, and if num_pkrs is left
 * untouched before GFX 10.3 but taken from the device from GFX 10.3 onwards.
 */
static void dm_test_fill_gfx9_tiling_info_from_device(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dc_tiling_info tiling_info = {0};

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, adev);

	adev->gfx.config.gb_addr_config_fields.num_pipes = 4;
	adev->gfx.config.gb_addr_config_fields.num_banks = 8;
	adev->gfx.config.gb_addr_config_fields.pipe_interleave_size = 256;
	adev->gfx.config.gb_addr_config_fields.num_se = 2;
	adev->gfx.config.gb_addr_config_fields.max_compress_frags = 1;
	adev->gfx.config.gb_addr_config_fields.num_rb_per_se = 2;
	adev->gfx.config.gb_addr_config_fields.num_pkrs = 3;
	adev->ip_versions[GC_HWIP][0] = IP_VERSION(10, 2, 9);

	tiling_info.gfx9.num_pkrs = 0x5a;

	amdgpu_dm_plane_fill_gfx9_tiling_info_from_device(adev, &tiling_info);

	KUNIT_EXPECT_EQ(test, tiling_info.gfx9.num_pipes, 4U);
	KUNIT_EXPECT_EQ(test, tiling_info.gfx9.num_banks, 8U);
	KUNIT_EXPECT_EQ(test, tiling_info.gfx9.pipe_interleave, 256U);
	KUNIT_EXPECT_EQ(test, tiling_info.gfx9.num_shader_engines, 2U);
	KUNIT_EXPECT_EQ(test, tiling_info.gfx9.max_compressed_frags, 1U);
	KUNIT_EXPECT_EQ(test, tiling_info.gfx9.num_rb_per_se, 2U);
	KUNIT_EXPECT_EQ(test, tiling_info.gfx9.shaderEnable, 1U);
	KUNIT_EXPECT_EQ(test, tiling_info.gfx9.num_pkrs, 0x5aU);

	adev->gfx.config.gb_addr_config_fields.num_pipes = 2;
	adev->gfx.config.gb_addr_config_fields.num_banks = 4;
	adev->gfx.config.gb_addr_config_fields.pipe_interleave_size = 128;
	adev->gfx.config.gb_addr_config_fields.num_se = 1;
	adev->gfx.config.gb_addr_config_fields.max_compress_frags = 2;
	adev->gfx.config.gb_addr_config_fields.num_rb_per_se = 1;
	adev->gfx.config.gb_addr_config_fields.num_pkrs = 6;
	adev->ip_versions[GC_HWIP][0] = IP_VERSION(10, 3, 0);

	memset(&tiling_info, 0, sizeof(tiling_info));

	amdgpu_dm_plane_fill_gfx9_tiling_info_from_device(adev, &tiling_info);

	KUNIT_EXPECT_EQ(test, tiling_info.gfx9.num_pipes, 2U);
	KUNIT_EXPECT_EQ(test, tiling_info.gfx9.num_banks, 4U);
	KUNIT_EXPECT_EQ(test, tiling_info.gfx9.pipe_interleave, 128U);
	KUNIT_EXPECT_EQ(test, tiling_info.gfx9.num_shader_engines, 1U);
	KUNIT_EXPECT_EQ(test, tiling_info.gfx9.max_compressed_frags, 2U);
	KUNIT_EXPECT_EQ(test, tiling_info.gfx9.num_rb_per_se, 1U);
	KUNIT_EXPECT_EQ(test, tiling_info.gfx9.shaderEnable, 1U);
	KUNIT_EXPECT_EQ(test, tiling_info.gfx9.num_pkrs, 6U);
}

/**
 * dm_test_fill_gfx9_tiling_info_from_modifier() - Verify GFX9 modifier decoding.
 * @test: KUnit test context.
 *
 * Verify if a non-AMD modifier keeps the device values, if an AMD modifier
 * updates the bank count on pre-NV families, and if it updates the packer
 * count on NV and later.
 */
static void dm_test_fill_gfx9_tiling_info_from_modifier(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dc_tiling_info tiling_info = {0};
	uint64_t modifier;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, adev);

	adev->family = AMDGPU_FAMILY_NV;
	adev->gfx.config.gb_addr_config_fields.num_pipes = 4;
	adev->gfx.config.gb_addr_config_fields.num_banks = 8;
	adev->gfx.config.gb_addr_config_fields.pipe_interleave_size = 256;
	adev->gfx.config.gb_addr_config_fields.num_se = 2;
	adev->gfx.config.gb_addr_config_fields.max_compress_frags = 1;
	adev->gfx.config.gb_addr_config_fields.num_rb_per_se = 2;
	adev->gfx.config.gb_addr_config_fields.num_pkrs = 3;
	adev->ip_versions[GC_HWIP][0] = IP_VERSION(10, 3, 0);

	amdgpu_dm_plane_fill_gfx9_tiling_info_from_modifier(adev, &tiling_info,
							    DRM_FORMAT_MOD_LINEAR);

	KUNIT_EXPECT_EQ(test, tiling_info.gfx9.num_pipes, 4U);
	KUNIT_EXPECT_EQ(test, tiling_info.gfx9.num_banks, 8U);
	KUNIT_EXPECT_EQ(test, tiling_info.gfx9.pipe_interleave, 256U);
	KUNIT_EXPECT_EQ(test, tiling_info.gfx9.num_shader_engines, 2U);
	KUNIT_EXPECT_EQ(test, tiling_info.gfx9.max_compressed_frags, 1U);
	KUNIT_EXPECT_EQ(test, tiling_info.gfx9.num_rb_per_se, 2U);
	KUNIT_EXPECT_EQ(test, tiling_info.gfx9.shaderEnable, 1U);
	KUNIT_EXPECT_EQ(test, tiling_info.gfx9.num_pkrs, 3U);

	adev->family = AMDGPU_FAMILY_RV;
	adev->gfx.config.gb_addr_config_fields.num_banks = 16;
	adev->gfx.config.gb_addr_config_fields.num_pkrs = 7;
	adev->ip_versions[GC_HWIP][0] = IP_VERSION(10, 2, 9);

	memset(&tiling_info, 0, sizeof(tiling_info));
	tiling_info.gfx9.num_pkrs = 0x5a;

	modifier = AMD_FMT_MOD |
		    AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX9_64K_S_X) |
		    AMD_FMT_MOD_SET(PIPE_XOR_BITS, 7) |
		    AMD_FMT_MOD_SET(BANK_XOR_BITS, 3) |
		    AMD_FMT_MOD_SET(PACKERS, 2);

	amdgpu_dm_plane_fill_gfx9_tiling_info_from_modifier(adev, &tiling_info, modifier);

	KUNIT_EXPECT_EQ(test, tiling_info.gfx9.num_pipes, 32U);
	KUNIT_EXPECT_EQ(test, tiling_info.gfx9.num_shader_engines, 4U);
	KUNIT_EXPECT_EQ(test, tiling_info.gfx9.num_banks, 8U);
	KUNIT_EXPECT_EQ(test, tiling_info.gfx9.num_pkrs, 0x5aU);
	KUNIT_EXPECT_EQ(test, tiling_info.gfx9.shaderEnable, 1U);

	adev->family = AMDGPU_FAMILY_NV;
	adev->gfx.config.gb_addr_config_fields.num_pipes = 2;
	adev->gfx.config.gb_addr_config_fields.num_banks = 9;
	adev->gfx.config.gb_addr_config_fields.pipe_interleave_size = 128;
	adev->gfx.config.gb_addr_config_fields.num_se = 1;
	adev->gfx.config.gb_addr_config_fields.max_compress_frags = 2;
	adev->gfx.config.gb_addr_config_fields.num_rb_per_se = 1;
	adev->gfx.config.gb_addr_config_fields.num_pkrs = 2;
	adev->ip_versions[GC_HWIP][0] = IP_VERSION(10, 3, 0);

	memset(&tiling_info, 0, sizeof(tiling_info));

	modifier = AMD_FMT_MOD |
		    AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX9_64K_S_X) |
		    AMD_FMT_MOD_SET(PIPE_XOR_BITS, 6) |
		    AMD_FMT_MOD_SET(BANK_XOR_BITS, 2) |
		    AMD_FMT_MOD_SET(PACKERS, 3);

	amdgpu_dm_plane_fill_gfx9_tiling_info_from_modifier(adev, &tiling_info, modifier);

	KUNIT_EXPECT_EQ(test, tiling_info.gfx9.num_pipes, 32U);
	KUNIT_EXPECT_EQ(test, tiling_info.gfx9.num_shader_engines, 2U);
	KUNIT_EXPECT_EQ(test, tiling_info.gfx9.num_banks, 9U);
	KUNIT_EXPECT_EQ(test, tiling_info.gfx9.num_pkrs, 8U);
	KUNIT_EXPECT_EQ(test, tiling_info.gfx9.shaderEnable, 1U);
}

/**
 * dm_test_get_format_info() - Verify modifier-based format info lookup.
 * @test: KUnit test context.
 *
 * Verify if non-AMD modifiers return NULL and AMD DCC modifiers resolve a
 * dedicated format info structure.
 */
static void dm_test_get_format_info(struct kunit *test)
{
	u64 dcc_mod = AMD_FMT_MOD |
		      AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX9) |
		      AMD_FMT_MOD_SET(DCC, 1);
	const struct drm_format_info *format_info;

	KUNIT_EXPECT_PTR_EQ(test,
			    (void *)amdgpu_dm_plane_get_format_info(DRM_FORMAT_XRGB8888,
								    DRM_FORMAT_MOD_LINEAR),
			    NULL);
	format_info = amdgpu_dm_plane_get_format_info(DRM_FORMAT_XRGB8888, dcc_mod);
	KUNIT_EXPECT_NOT_NULL(test, format_info);
}

/* Modifier lists are terminated by DRM_FORMAT_MOD_INVALID, which is not counted. */
static int dm_test_mods_count(const u64 *mods)
{
	int i = 0;

	while (mods[i] != DRM_FORMAT_MOD_INVALID)
		i++;

	return i;
}

static void dm_test_expect_mods_terminated(struct kunit *test, struct amdgpu_device *adev)
{
	u64 *mods = NULL;
	int ret;
	int count;

	ret = amdgpu_dm_plane_get_plane_modifiers(adev, DRM_PLANE_TYPE_PRIMARY, &mods);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_ASSERT_NOT_NULL(test, mods);

	count = dm_test_mods_count(mods);
	KUNIT_EXPECT_GT(test, count, 0);
	KUNIT_EXPECT_EQ(test, mods[count - 1], DRM_FORMAT_MOD_LINEAR);
	kfree(mods);
}

static bool dm_test_mods_contain(const u64 *mods, u64 expected)
{
	int count = dm_test_mods_count(mods);
	int i;

	for (i = 0; i < count; i++) {
		if (mods[i] == expected)
			return true;
	}

	return false;
}

static u64 *dm_test_get_primary_mods(struct kunit *test, struct amdgpu_device *adev)
{
	u64 *mods = NULL;
	int ret;

	ret = amdgpu_dm_plane_get_plane_modifiers(adev, DRM_PLANE_TYPE_PRIMARY, &mods);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_ASSERT_NOT_NULL(test, mods);

	return mods;
}

static int dm_test_gfx9_attrs(struct amdgpu_device *adev,
			      const struct amdgpu_framebuffer *afb,
				      const struct plane_size *plane_size,
				      struct dc_tiling_info *tiling_info,
				      struct dc_plane_dcc_param *dcc,
				      struct dc_plane_address *address)
{
	return amdgpu_dm_plane_fill_gfx9_attrs_from_modifiers(adev,
		afb, SURFACE_PIXEL_FORMAT_GRPH_ARGB8888, ROTATION_ANGLE_0,
		plane_size, tiling_info, dcc, address);
}

static int dm_test_gfx12_attrs(struct amdgpu_device *adev,
			       const struct amdgpu_framebuffer *afb,
				       const struct plane_size *plane_size,
				       struct dc_tiling_info *tiling_info,
				       struct dc_plane_dcc_param *dcc,
				       struct dc_plane_address *address)
{
	return amdgpu_dm_plane_fill_gfx12_attrs_from_modifiers(adev,
		afb, SURFACE_PIXEL_FORMAT_GRPH_ARGB8888, ROTATION_ANGLE_0,
		plane_size, tiling_info, dcc, address);
}

static int dm_test_plane_attrs(struct amdgpu_device *adev,
			       const struct amdgpu_framebuffer *afb,
			       enum surface_pixel_format format,
			       struct dc_tiling_info *tiling_info,
			       struct plane_size *plane_size,
			       struct dc_plane_dcc_param *dcc,
			       struct dc_plane_address *address)
{
	return amdgpu_dm_plane_fill_plane_buffer_attributes(adev, afb, format,
		ROTATION_ANGLE_0, tiling_info, plane_size, dcc, address,
		false);
}

static int dm_test_video_attrs(struct amdgpu_device *adev,
			       const struct amdgpu_framebuffer *afb,
			       struct dc_tiling_info *tiling_info,
			       struct plane_size *plane_size,
			       struct dc_plane_dcc_param *dcc,
			       struct dc_plane_address *address)
{
	return dm_test_plane_attrs(adev, afb, SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr,
		tiling_info, plane_size, dcc, address);
}

static int dm_test_graphics_attrs(struct amdgpu_device *adev,
				  const struct amdgpu_framebuffer *afb,
				  struct dc_tiling_info *tiling_info,
				  struct plane_size *plane_size,
				  struct dc_plane_dcc_param *dcc,
				  struct dc_plane_address *address)
{
	return dm_test_plane_attrs(adev, afb, SURFACE_PIXEL_FORMAT_GRPH_ARGB8888,
		tiling_info, plane_size, dcc, address);
}

static void dm_test_setup_gfx9_dcc_device(struct amdgpu_device *adev,
					  struct dc *dc,
					  struct dm_test_dcc_cap_ctx *ctx,
					  bool output_independent_64b_blks)
{
	adev->family = AMDGPU_FAMILY_NV;
	adev->dm.dc = dc;
	adev->gfx.config.gb_addr_config_fields.num_pipes = 2;
	adev->gfx.config.gb_addr_config_fields.num_pkrs = 2;
	adev->ip_versions[GC_HWIP][0] = IP_VERSION(10, 3, 0);
	dc->cap_funcs.get_dcc_compression_cap = dm_test_get_dcc_compression_cap;
	ctx->callback_ret = true;
	ctx->capable = true;
	ctx->output_independent_64b_blks = output_independent_64b_blks;
	dm_test_dcc_ctx = ctx;
}

static u64 dm_test_gfx9_dcc_modifier(u64 tile_version, bool independent_64b_blks,
				     bool independent_128b_blks)
{
	return AMD_FMT_MOD |
	       AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX9_64K_S_X) |
	       AMD_FMT_MOD_SET(TILE_VERSION, tile_version) |
	       AMD_FMT_MOD_SET(DCC, 1) |
	       AMD_FMT_MOD_SET(DCC_INDEPENDENT_64B, independent_64b_blks) |
	       AMD_FMT_MOD_SET(DCC_INDEPENDENT_128B, independent_128b_blks);
}

static void dm_test_setup_gfx11_device(struct amdgpu_device *adev,
				       struct dm_test_gfx11_reg_ctx *ctx,
				       u32 num_pkrs_log2, u32 num_pipes_log2)
{
	ctx->gb_addr_config =
		REG_SET_FIELD(0, GB_ADDR_CONFIG, NUM_PKRS, num_pkrs_log2) |
		REG_SET_FIELD(0, GB_ADDR_CONFIG, NUM_PIPES, num_pipes_log2);
	ctx->gc_reg_offsets[regGB_ADDR_CONFIG_BASE_IDX] = 0;
	ctx->expected_reg = ctx->gc_reg_offsets[regGB_ADDR_CONFIG_BASE_IDX] +
			    regGB_ADDR_CONFIG;
	dm_test_gfx11_reg_ctx = ctx;

	adev->family = AMDGPU_FAMILY_GC_11_0_0;
	adev->reg_offset[GC_HWIP][0] = ctx->gc_reg_offsets;
	adev->gfx.rlc.reg_funcs = &dm_test_gfx11_reg_funcs;
}

static u64 dm_test_gfx11_dcc_best_modifier(u32 pipe_xor_bits, u32 pkrs, u32 tile)
{
	return AMD_FMT_MOD |
	       AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX11) |
	       AMD_FMT_MOD_SET(PIPE_XOR_BITS, pipe_xor_bits) |
	       AMD_FMT_MOD_SET(TILE, tile) |
	       AMD_FMT_MOD_SET(PACKERS, pkrs) |
	       AMD_FMT_MOD_SET(DCC, 1) |
	       AMD_FMT_MOD_SET(DCC_INDEPENDENT_128B, 1) |
	       AMD_FMT_MOD_SET(DCC_MAX_COMPRESSED_BLOCK, AMD_FMT_MOD_DCC_BLOCK_128B);
}

static u64 dm_test_gfx11_dcc_4k_modifier(u32 pipe_xor_bits, u32 pkrs, u32 tile)
{
	return AMD_FMT_MOD |
	       AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX11) |
	       AMD_FMT_MOD_SET(PIPE_XOR_BITS, pipe_xor_bits) |
	       AMD_FMT_MOD_SET(TILE, tile) |
	       AMD_FMT_MOD_SET(PACKERS, pkrs) |
	       AMD_FMT_MOD_SET(DCC, 1) |
	       AMD_FMT_MOD_SET(DCC_INDEPENDENT_64B, 1) |
	       AMD_FMT_MOD_SET(DCC_INDEPENDENT_128B, 1) |
	       AMD_FMT_MOD_SET(DCC_MAX_COMPRESSED_BLOCK, AMD_FMT_MOD_DCC_BLOCK_64B);
}

/**
 * dm_test_get_plane_modifiers_gfx9() - Verify GFX9 modifier list generation.
 * @test: KUnit test context.
 *
 * Verify if the GFX9 family produces a non-empty modifier list terminated by
 * LINEAR and INVALID entries.
 */
static void dm_test_get_plane_modifiers_gfx9(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, adev);

	adev->family = AMDGPU_FAMILY_AI;
	adev->gfx.config.gb_addr_config_fields.num_pipes = 4;
	adev->gfx.config.gb_addr_config_fields.num_banks = 8;
	adev->gfx.config.gb_addr_config_fields.num_se = 2;
	adev->gfx.config.gb_addr_config_fields.num_rb_per_se = 2;

	dm_test_expect_mods_terminated(test, adev);
}

/**
 * dm_test_get_plane_modifiers_gfx10_and_gfx12() - Verify newer family dispatch.
 * @test: KUnit test context.
 *
 * Verify if the GFX10.1, GFX10.3 and GFX12 configurations each dispatch to
 * their modifier builder and produce a terminated list.
 */
static void dm_test_get_plane_modifiers_gfx10_and_gfx12(struct kunit *test)
{
	static const struct {
		u32 family;
		u32 gc_ip_version;
		u32 num_pipes;
		u32 num_pkrs;
	} cases[] = {
		{ AMDGPU_FAMILY_NV, IP_VERSION(10, 1, 0), 4, 0 },
		{ AMDGPU_FAMILY_NV, IP_VERSION(10, 3, 0), 4, 2 },
		{ AMDGPU_FAMILY_GC_12_0_0, 0, 0, 0 },
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(cases); i++) {
		struct amdgpu_device *adev;

		adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
		KUNIT_ASSERT_NOT_NULL(test, adev);

		adev->family = cases[i].family;
		adev->ip_versions[GC_HWIP][0] = cases[i].gc_ip_version;
		adev->gfx.config.gb_addr_config_fields.num_pipes = cases[i].num_pipes;
		adev->gfx.config.gb_addr_config_fields.num_pkrs = cases[i].num_pkrs;

		dm_test_expect_mods_terminated(test, adev);
	}
}

/**
 * dm_test_get_plane_modifiers_rv() - Verify RV modifier list generation.
 * @test: KUnit test context.
 *
 * Verify if RV devices add the RV-specific S-swizzle modifiers, and if the
 * constant-encode DCC variants only appear from Raven2 onwards.
 */
static void dm_test_get_plane_modifiers_rv(struct kunit *test)
{
	static const struct {
		const char *name;
		u32 external_rev_id;
		u32 constant_encode;
	} cases[] = {
		{ "raven", 0x80, 0 },
		{ "raven2", 0x81, 1 },
	};
	int pipes = 2;
	int pipe_xor_bits = 3;
	int bank_xor_bits = 2;
	u64 s_x_mod;
	u64 s_mod;
	unsigned int i;

	s_x_mod = AMD_FMT_MOD |
		  AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX9_64K_S_X) |
		  AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX9) |
		  AMD_FMT_MOD_SET(PIPE_XOR_BITS, pipe_xor_bits) |
		  AMD_FMT_MOD_SET(BANK_XOR_BITS, bank_xor_bits);
	s_mod = AMD_FMT_MOD |
		AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX9_64K_S) |
		AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX9);

	for (i = 0; i < ARRAY_SIZE(cases); i++) {
		struct amdgpu_device *adev;
		u64 *mods;
		u64 dcc_mod;
		u64 retile_mod;

		adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
		KUNIT_ASSERT_NOT_NULL(test, adev);

		adev->family = AMDGPU_FAMILY_RV;
		adev->asic_type = CHIP_RAVEN;
		adev->external_rev_id = cases[i].external_rev_id;
		adev->gfx.config.gb_addr_config_fields.num_pipes = 4;
		adev->gfx.config.gb_addr_config_fields.num_banks = 4;
		adev->gfx.config.gb_addr_config_fields.num_se = 2;
		adev->gfx.config.gb_addr_config_fields.num_rb_per_se = 2;

		mods = dm_test_get_primary_mods(test, adev);
		dcc_mod = AMD_FMT_MOD |
			  AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX9_64K_S_X) |
			  AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX9) |
			  AMD_FMT_MOD_SET(PIPE_XOR_BITS, pipe_xor_bits) |
			  AMD_FMT_MOD_SET(BANK_XOR_BITS, bank_xor_bits) |
			  AMD_FMT_MOD_SET(DCC, 1) |
			  AMD_FMT_MOD_SET(DCC_INDEPENDENT_64B, 1) |
			  AMD_FMT_MOD_SET(DCC_MAX_COMPRESSED_BLOCK, AMD_FMT_MOD_DCC_BLOCK_64B) |
			  AMD_FMT_MOD_SET(DCC_CONSTANT_ENCODE, cases[i].constant_encode);
		retile_mod = dcc_mod |
			     AMD_FMT_MOD_SET(DCC_RETILE, 1) |
			     AMD_FMT_MOD_SET(RB, 2) |
			     AMD_FMT_MOD_SET(PIPE, pipes);

		KUNIT_EXPECT_TRUE_MSG(test, dm_test_mods_contain(mods, dcc_mod),
				      "%s", cases[i].name);
		KUNIT_EXPECT_TRUE_MSG(test, dm_test_mods_contain(mods, retile_mod),
				      "%s", cases[i].name);
		KUNIT_EXPECT_TRUE_MSG(test, dm_test_mods_contain(mods, s_x_mod),
				      "%s", cases[i].name);
		KUNIT_EXPECT_TRUE_MSG(test, dm_test_mods_contain(mods, s_mod),
				      "%s", cases[i].name);

		kfree(mods);
	}
}

/**
 * dm_test_get_plane_modifiers_gfx11_64k_first() - Verify GFX11 small-pipe order.
 * @test: KUnit test context.
 *
 * Verify if GFX11 modifier generation reads GB_ADDR_CONFIG through the RLC
 * register callback and prefers 64K_R_X when the pipe count is 16 or lower.
 */
static void dm_test_get_plane_modifiers_gfx11_64k_first(struct kunit *test)
{
	struct dm_test_gfx11_reg_ctx ctx = {0};
	struct amdgpu_device *adev;
	u64 *mods;
	u32 pipe_xor_bits = 4;
	u32 pkrs = 1;
	u32 tile = AMD_FMT_MOD_TILE_GFX9_64K_R_X;
	u64 dcc_best;
	u64 dcc_4k;
	u64 d_mod;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, adev);
	dm_test_setup_gfx11_device(adev, &ctx, pkrs, pipe_xor_bits);

	mods = dm_test_get_primary_mods(test, adev);
	dcc_best = dm_test_gfx11_dcc_best_modifier(pipe_xor_bits, pkrs, tile);
	dcc_4k = dm_test_gfx11_dcc_4k_modifier(pipe_xor_bits, pkrs, tile);
	d_mod = AMD_FMT_MOD |
		AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX11) |
		AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX9_64K_D);

	KUNIT_EXPECT_TRUE(test, ctx.called);
	KUNIT_EXPECT_EQ(test, ctx.captured_reg, ctx.expected_reg);
	KUNIT_EXPECT_EQ(test, ctx.captured_acc_flags, 0U);
	KUNIT_EXPECT_EQ(test, ctx.captured_hwip, (u32)GC_HWIP);
	KUNIT_EXPECT_EQ(test, ctx.captured_xcc_id, 0U);
	KUNIT_EXPECT_EQ(test, mods[0], dcc_best);
	KUNIT_EXPECT_TRUE(test, dm_test_mods_contain(mods, dcc_4k));
	KUNIT_EXPECT_TRUE(test,
			  dm_test_mods_contain(mods,
					       dcc_best | AMD_FMT_MOD_SET(DCC_RETILE, 1)));
	KUNIT_EXPECT_TRUE(test, dm_test_mods_contain(mods, d_mod));

	dm_test_gfx11_reg_ctx = NULL;
	kfree(mods);
}

/**
 * dm_test_get_plane_modifiers_gfx11_256k_first() - Verify GFX11 large-pipe order.
 * @test: KUnit test context.
 *
 * Verify if GFX11 modifier generation prefers 256K_R_X when more than 16 pipes
 * are reported by GB_ADDR_CONFIG.
 */
static void dm_test_get_plane_modifiers_gfx11_256k_first(struct kunit *test)
{
	struct dm_test_gfx11_reg_ctx ctx = {0};
	struct amdgpu_device *adev;
	u64 *mods;
	u32 pipe_xor_bits = 5;
	u32 pkrs = 2;
	u32 tile = AMD_FMT_MOD_TILE_GFX11_256K_R_X;
	u32 fallback_tile = AMD_FMT_MOD_TILE_GFX9_64K_R_X;
	u64 dcc_best;
	u64 fallback_dcc_best = dm_test_gfx11_dcc_best_modifier(pipe_xor_bits, pkrs,
								  fallback_tile);

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, adev);
	dm_test_setup_gfx11_device(adev, &ctx, pkrs, pipe_xor_bits);

	mods = dm_test_get_primary_mods(test, adev);
	dcc_best = dm_test_gfx11_dcc_best_modifier(pipe_xor_bits, pkrs, tile);

	KUNIT_EXPECT_TRUE(test, ctx.called);
	KUNIT_EXPECT_EQ(test, ctx.captured_reg, ctx.expected_reg);
	KUNIT_EXPECT_EQ(test, mods[0], dcc_best);
	KUNIT_EXPECT_TRUE(test, dm_test_mods_contain(mods, fallback_dcc_best));

	dm_test_gfx11_reg_ctx = NULL;
	kfree(mods);
}

/**
 * dm_test_fill_gfx9_plane_attributes_dcc() - Verify GFX9 DCC modifier path.
 * @test: KUnit test context.
 *
 * Verify if a GFX10-RBPLUS DCC modifier enables DCC and selects the 64B
 * independent block mode.
 */
static void dm_test_fill_gfx9_plane_attributes_dcc(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dc *dc;
	struct amdgpu_framebuffer *afb;
	struct plane_size plane_size = {0};
	struct dc_tiling_info tiling_info = {0};
	struct dc_plane_dcc_param dcc = {0};
	struct dc_plane_address address = {0};
	struct dm_test_dcc_cap_ctx ctx = {
		.callback_ret = true,
		.capable = true,
		.output_independent_64b_blks = true,
	};
	int ret;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	afb = kunit_kzalloc(test, sizeof(*afb), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, dc);
	KUNIT_ASSERT_NOT_NULL(test, afb);

	adev->family = AMDGPU_FAMILY_NV;
	adev->dm.dc = dc;
	adev->gfx.config.gb_addr_config_fields.num_pipes = 2;
	adev->gfx.config.gb_addr_config_fields.num_pkrs = 2;
	adev->ip_versions[GC_HWIP][0] = IP_VERSION(10, 3, 0);
	dc->cap_funcs.get_dcc_compression_cap = dm_test_get_dcc_compression_cap;
	dm_test_dcc_ctx = &ctx;

	afb->base.pitches[1] = 256;
	afb->base.modifier = AMD_FMT_MOD |
			     AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX9_64K_S_X) |
			     AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX10_RBPLUS) |
			     AMD_FMT_MOD_SET(DCC, 1) |
			     AMD_FMT_MOD_SET(DCC_INDEPENDENT_64B, 1);
	plane_size.surface_size.width = 1920;
	plane_size.surface_size.height = 1080;

	ret = dm_test_gfx9_attrs(adev, afb, &plane_size, &tiling_info, &dcc,
				 &address);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, (int)tiling_info.gfxversion, (int)DcGfxVersion9);
	KUNIT_EXPECT_TRUE(test, dcc.enable);
	KUNIT_EXPECT_EQ(test, (int)dcc.dcc_ind_blk, (int)hubp_ind_block_64b);
	KUNIT_EXPECT_EQ(test, dcc.meta_pitch, 256U);

	dm_test_dcc_ctx = NULL;
}

/**
 * dm_test_fill_gfx9_plane_attributes_validate_fails() - Verify GFX9 error path.
 * @test: KUnit test context.
 *
 * Verify if GFX9 modifier parsing returns validation errors from the shared DCC
 * validation helper.
 */
static void dm_test_fill_gfx9_plane_attributes_validate_fails(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dc *dc;
	struct amdgpu_framebuffer *afb;
	struct plane_size plane_size = {0};
	struct dc_tiling_info tiling_info = {0};
	struct dc_plane_dcc_param dcc = {0};
	struct dc_plane_address address = {0};
	u64 tile_version = AMD_FMT_MOD_TILE_VER_GFX10_RBPLUS;
	int ret;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	afb = kunit_kzalloc(test, sizeof(*afb), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, dc);
	KUNIT_ASSERT_NOT_NULL(test, afb);

	adev->family = AMDGPU_FAMILY_NV;
	adev->dm.dc = dc;
	adev->ip_versions[GC_HWIP][0] = IP_VERSION(10, 3, 0);
	afb->base.modifier = dm_test_gfx9_dcc_modifier(tile_version, true, false);
	plane_size.surface_size.width = 1920;
	plane_size.surface_size.height = 1080;

	ret = dm_test_gfx9_attrs(adev, afb, &plane_size, &tiling_info, &dcc,
				 &address);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

/**
 * dm_test_fill_gfx9_plane_attributes_dcc_ind_blk() - Verify DCC block modes.
 * @test: KUnit test context.
 *
 * Verify if the independent 64B and 128B block bits of a GFX9 or GFX10-RBPLUS
 * DCC modifier select the matching HUBP independent block mode.
 */
static void dm_test_fill_gfx9_plane_attributes_dcc_ind_blk(struct kunit *test)
{
	static const struct {
		const char *name;
		u64 tile_version;
		bool independent_64b_blks;
		bool independent_128b_blks;
		int expected_ind_blk;
	} cases[] = {
		{ "rbplus 64B no 128BCL", AMD_FMT_MOD_TILE_VER_GFX10_RBPLUS, true, true,
		  hubp_ind_block_64b_no_128bcl },
		{ "rbplus 128B", AMD_FMT_MOD_TILE_VER_GFX10_RBPLUS, false, true,
		  hubp_ind_block_128b },
		{ "rbplus unconstrained", AMD_FMT_MOD_TILE_VER_GFX10_RBPLUS, false, false,
		  hubp_ind_block_unconstrained },
		{ "gfx9 64B", AMD_FMT_MOD_TILE_VER_GFX9, true, false, hubp_ind_block_64b },
		{ "gfx9 unconstrained", AMD_FMT_MOD_TILE_VER_GFX9, false, false,
		  hubp_ind_block_unconstrained },
	};
	struct plane_size plane_size = {0};
	struct amdgpu_device *adev;
	struct amdgpu_framebuffer *afb;
	struct dc *dc;
	unsigned int i;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	afb = kunit_kzalloc(test, sizeof(*afb), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, dc);
	KUNIT_ASSERT_NOT_NULL(test, afb);

	plane_size.surface_size.width = 1920;
	plane_size.surface_size.height = 1080;

	for (i = 0; i < ARRAY_SIZE(cases); i++) {
		struct dc_tiling_info tiling_info = {0};
		struct dc_plane_dcc_param dcc = {0};
		struct dc_plane_address address = {0};
		struct dm_test_dcc_cap_ctx ctx = {0};
		int ret;

		dm_test_setup_gfx9_dcc_device(adev, dc, &ctx, cases[i].independent_64b_blks);
		afb->base.modifier = dm_test_gfx9_dcc_modifier(cases[i].tile_version,
							       cases[i].independent_64b_blks,
							       cases[i].independent_128b_blks);

		ret = dm_test_gfx9_attrs(adev, afb, &plane_size, &tiling_info, &dcc,
					 &address);
		dm_test_dcc_ctx = NULL;

		KUNIT_EXPECT_EQ_MSG(test, ret, 0, "%s", cases[i].name);
		KUNIT_EXPECT_TRUE_MSG(test, dcc.enable, "%s", cases[i].name);
		KUNIT_EXPECT_EQ_MSG(test, (int)dcc.dcc_ind_blk, cases[i].expected_ind_blk,
				    "%s", cases[i].name);
	}
}

/**
 * dm_test_fill_gfx12_plane_attributes_dcc_blocks() - Verify GFX12 DCC blocks.
 * @test: KUnit test context.
 *
 * Verify if the max-compressed-block field of a GFX12 DCC modifier selects the
 * matching HUBP independent block mode and independent 64B flag, and if the
 * GFX addr3 tiling version is reported.
 */
static void dm_test_fill_gfx12_plane_attributes_dcc_blocks(struct kunit *test)
{
	static const struct {
		const char *name;
		u64 max_compressed_block;
		bool independent_64b_blks;
		int expected_ind_blk;
	} cases[] = {
		{ "64B", 0, true, hubp_ind_block_64b },
		{ "128B", 1, false, hubp_ind_block_128b },
		{ "unconstrained", 2, false, hubp_ind_block_unconstrained },
	};
	struct dm_test_dcc_cap_ctx ctx = {
		.callback_ret = true,
		.capable = true,
		.output_independent_64b_blks = false,
	};
	struct plane_size plane_size = {0};
	struct amdgpu_device *adev;
	struct amdgpu_framebuffer *afb;
	struct dc *dc;
	unsigned int i;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	afb = kunit_kzalloc(test, sizeof(*afb), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, dc);
	KUNIT_ASSERT_NOT_NULL(test, afb);

	adev->family = AMDGPU_FAMILY_GC_12_0_0;
	adev->dm.dc = dc;
	dc->cap_funcs.get_dcc_compression_cap = dm_test_get_dcc_compression_cap;
	plane_size.surface_size.width = 1920;
	plane_size.surface_size.height = 1080;

	for (i = 0; i < ARRAY_SIZE(cases); i++) {
		struct dc_tiling_info tiling_info = {0};
		struct dc_plane_dcc_param dcc = {0};
		struct dc_plane_address address = {0};
		int ret;

		afb->base.modifier = AMD_FMT_MOD |
				     AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX12_64K_2D) |
				     AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX12) |
				     AMD_FMT_MOD_SET(DCC, 1) |
				     AMD_FMT_MOD_SET(DCC_MAX_COMPRESSED_BLOCK,
						     cases[i].max_compressed_block);
		dm_test_dcc_ctx = &ctx;

		ret = dm_test_gfx12_attrs(adev, afb, &plane_size, &tiling_info, &dcc,
					  &address);
		dm_test_dcc_ctx = NULL;

		KUNIT_EXPECT_EQ_MSG(test, ret, 0, "%s", cases[i].name);
		KUNIT_EXPECT_EQ_MSG(test, (int)tiling_info.gfxversion, (int)DcGfxAddr3,
				    "%s", cases[i].name);
		KUNIT_EXPECT_TRUE_MSG(test, dcc.enable, "%s", cases[i].name);
		KUNIT_EXPECT_EQ_MSG(test, (bool)dcc.independent_64b_blks,
				    cases[i].independent_64b_blks, "%s", cases[i].name);
		KUNIT_EXPECT_EQ_MSG(test, (int)dcc.dcc_ind_blk, cases[i].expected_ind_blk,
				    "%s", cases[i].name);
	}
}

/**
 * dm_test_fill_gfx12_plane_attributes_validate_fails() - Verify GFX12 error path.
 * @test: KUnit test context.
 *
 * Verify if GFX12 modifier parsing returns validation errors from the shared
 * DCC validation helper.
 */
static void dm_test_fill_gfx12_plane_attributes_validate_fails(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dc *dc;
	struct amdgpu_framebuffer *afb;
	struct plane_size plane_size = {0};
	struct dc_tiling_info tiling_info = {0};
	struct dc_plane_dcc_param dcc = {0};
	struct dc_plane_address address = {0};
	int ret;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	afb = kunit_kzalloc(test, sizeof(*afb), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, dc);
	KUNIT_ASSERT_NOT_NULL(test, afb);

	adev->family = AMDGPU_FAMILY_GC_12_0_0;
	adev->dm.dc = dc;
	afb->base.modifier = AMD_FMT_MOD |
			     AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX12_64K_2D) |
			     AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX12) |
			     AMD_FMT_MOD_SET(DCC, 1) |
			     AMD_FMT_MOD_SET(DCC_MAX_COMPRESSED_BLOCK, 1);
	plane_size.surface_size.width = 1920;
	plane_size.surface_size.height = 1080;

	ret = dm_test_gfx12_attrs(adev, afb, &plane_size, &tiling_info, &dcc,
				  &address);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

/**
 * dm_test_fill_plane_buffer_attributes_video() - Verify NV12 attributes.
 * @test: KUnit test context.
 *
 * Verify if a video pixel format fills chroma plane size and the progressive
 * video address type on a GFX9 family device.
 */
static void dm_test_fill_plane_buffer_attributes_video(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct amdgpu_framebuffer *afb;
	struct dc_tiling_info tiling_info;
	struct plane_size plane_size;
	struct dc_plane_dcc_param dcc;
	struct dc_plane_address address;
	int ret;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	afb = kunit_kzalloc(test, sizeof(*afb), GFP_KERNEL);
	tiling_info = (struct dc_tiling_info){0};
	plane_size = (struct plane_size){0};
	dcc = (struct dc_plane_dcc_param){0};
	address = (struct dc_plane_address){0};
	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, afb);

	adev->family = AMDGPU_FAMILY_NV;
	adev->ip_versions[GC_HWIP][0] = IP_VERSION(10, 3, 0);
	afb->address = 0x80000000ULL;
	afb->base.width = 1920;
	afb->base.height = 1080;
	afb->base.offsets[0] = 0;
	afb->base.offsets[1] = 0x200000;
	afb->base.pitches[0] = 1920;
	afb->base.pitches[1] = 1920;
	afb->base.format = drm_format_info(DRM_FORMAT_NV12);
	afb->base.modifier = DRM_FORMAT_MOD_LINEAR;
	KUNIT_ASSERT_NOT_NULL(test, afb->base.format);

	ret = dm_test_video_attrs(adev, afb, &tiling_info, &plane_size, &dcc,
				  &address);

	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, plane_size.surface_size.width, 1920);
	KUNIT_EXPECT_EQ(test, plane_size.chroma_size.width, 960U);
	KUNIT_EXPECT_EQ(test, plane_size.chroma_size.height, 540U);
	KUNIT_EXPECT_EQ(test, address.type,
			(int)PLN_ADDR_TYPE_VIDEO_PROGRESSIVE);
	KUNIT_EXPECT_EQ(test, (int)tiling_info.gfxversion, (int)DcGfxVersion9);
}

/**
 * dm_test_fill_plane_buffer_attributes_gfx12() - Verify GFX12 dispatch path.
 * @test: KUnit test context.
 *
 * Verify if a GFX12 family device fills graphics attributes via the GFX12
 * modifier path and reports the GFX addr3 version.
 */
static void dm_test_fill_plane_buffer_attributes_gfx12(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dc *dc;
	struct amdgpu_framebuffer *afb;
	struct dc_tiling_info tiling_info;
	struct plane_size plane_size;
	struct dc_plane_dcc_param dcc;
	struct dc_plane_address address;
	int ret;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	afb = kunit_kzalloc(test, sizeof(*afb), GFP_KERNEL);
	tiling_info = (struct dc_tiling_info){0};
	plane_size = (struct plane_size){0};
	dcc = (struct dc_plane_dcc_param){0};
	address = (struct dc_plane_address){0};
	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, dc);
	KUNIT_ASSERT_NOT_NULL(test, afb);

	adev->family = AMDGPU_FAMILY_GC_12_0_0;
	adev->dm.dc = dc;
	afb->address = 0x80000000ULL;
	afb->base.width = 1920;
	afb->base.height = 1080;
	afb->base.pitches[0] = 7680;
	afb->base.format = drm_format_info(DRM_FORMAT_XRGB8888);
	afb->base.modifier = DRM_FORMAT_MOD_LINEAR;
	KUNIT_ASSERT_NOT_NULL(test, afb->base.format);

	ret = dm_test_graphics_attrs(adev, afb, &tiling_info, &plane_size, &dcc,
				     &address);

	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, address.type, (int)PLN_ADDR_TYPE_GRAPHICS);
	KUNIT_EXPECT_EQ(test, (int)tiling_info.gfxversion, (int)DcGfxAddr3);
}

/**
 * dm_test_helper_check_state_scaling_caps() - Verify DC scaling caps are applied.
 * @test: KUnit test context.
 *
 * Verify if helper_check_state converts DC plane scaling caps to DRM scale
 * limits and rejects scaling outside those limits.
 */
static void dm_test_helper_check_state_scaling_caps(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dc *dc;
	struct drm_plane *plane;
	struct drm_plane_state *state;
	struct drm_crtc *crtc;
	struct drm_crtc_state *new_crtc_state;
	struct drm_framebuffer *fb;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	plane = kunit_kzalloc(test, sizeof(*plane), GFP_KERNEL);
	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	crtc = kunit_kzalloc(test, sizeof(*crtc), GFP_KERNEL);
	new_crtc_state = kunit_kzalloc(test, sizeof(*new_crtc_state), GFP_KERNEL);
	fb = kunit_kzalloc(test, sizeof(*fb), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, dc);
	KUNIT_ASSERT_NOT_NULL(test, plane);
	KUNIT_ASSERT_NOT_NULL(test, state);
	KUNIT_ASSERT_NOT_NULL(test, crtc);
	KUNIT_ASSERT_NOT_NULL(test, new_crtc_state);
	KUNIT_ASSERT_NOT_NULL(test, fb);

	adev->dm.dc = dc;
	dc->caps.planes[0].max_upscale_factor.argb8888 = 1;
	dc->caps.planes[0].max_downscale_factor.argb8888 = 1;

	plane->type = DRM_PLANE_TYPE_OVERLAY;
	plane->dev = &adev->ddev;
	crtc->dev = &adev->ddev;
	fb->width = 100;
	fb->height = 100;
	fb->format = drm_format_info(DRM_FORMAT_XRGB8888);
	KUNIT_ASSERT_NOT_NULL(test, fb->format);

	state->plane = plane;
	state->fb = fb;
	state->crtc = crtc;
	state->src_w = 100 << 16;
	state->src_h = 100 << 16;
	state->crtc_w = 200;
	state->crtc_h = 200;
	new_crtc_state->crtc = crtc;
	new_crtc_state->mode.crtc_hdisplay = 1920;
	new_crtc_state->mode.crtc_vdisplay = 1080;

	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_plane_helper_check_state(state, new_crtc_state),
			-ERANGE);
}

/**
 * dm_test_fill_dc_scaling_info_nv12_dcn1x() - Verify NV12 DCN1x rejection.
 * @test: KUnit test context.
 *
 * Verify if a non-zero NV12 source origin is rejected on DCN 1.0 to avoid the
 * known DCN1x hang.
 */
static void dm_test_fill_dc_scaling_info_nv12_dcn1x(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct drm_plane_state state = {0};
	struct drm_framebuffer fb = {0};
	struct dc_scaling_info info = {0};

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, adev);

	adev->ip_versions[DCE_HWIP][0] = IP_VERSION(1, 0, 0);
	fb.format = drm_format_info(DRM_FORMAT_NV12);
	KUNIT_ASSERT_NOT_NULL(test, fb.format);

	state.fb = &fb;
	state.src_x = 10 << 16;
	state.src_y = 0;
	state.src_w = 100 << 16;
	state.src_h = 100 << 16;
	state.crtc_w = 100;
	state.crtc_h = 100;

	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_plane_fill_dc_scaling_info(adev, &state, &info),
			-EINVAL);

	state.src_x = 0;
	state.src_y = 10 << 16;
	memset(&info, 0, sizeof(info));

	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_plane_fill_dc_scaling_info(adev, &state, &info),
			-EINVAL);
}

/**
 * dm_test_fill_dc_scaling_info_plane_caps() - Verify scaling caps path.
 * @test: KUnit test context.
 *
 * Verify if scaling info uses plane caps when the state references a plane,
 * device, and framebuffer.
 */
static void dm_test_fill_dc_scaling_info_plane_caps(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dc *dc;
	struct drm_plane *plane;
	struct drm_plane_state *state;
	struct drm_framebuffer *fb;
	struct dc_scaling_info info = {0};

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	plane = kunit_kzalloc(test, sizeof(*plane), GFP_KERNEL);
	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	fb = kunit_kzalloc(test, sizeof(*fb), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, dc);
	KUNIT_ASSERT_NOT_NULL(test, plane);
	KUNIT_ASSERT_NOT_NULL(test, state);
	KUNIT_ASSERT_NOT_NULL(test, fb);

	adev->dm.dc = dc;
	dc->caps.planes[0].max_upscale_factor.argb8888 = 16000;
	dc->caps.planes[0].max_downscale_factor.argb8888 = 250;

	plane->dev = &adev->ddev;
	fb->format = drm_format_info(DRM_FORMAT_XRGB8888);
	KUNIT_ASSERT_NOT_NULL(test, fb->format);

	state->plane = plane;
	state->fb = fb;
	state->src_w = 100 << 16;
	state->src_h = 100 << 16;
	state->crtc_w = 100;
	state->crtc_h = 100;

	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_plane_fill_dc_scaling_info(adev, state, &info),
			0);
}

/**
 * dm_test_format_mod_supported_d_swizzle_reject() - Verify D swizzle rejection.
 * @test: KUnit test context.
 *
 * Verify if a D micro-swizzle modifier is rejected for formats narrower than
 * 8 bytes per pixel.
 */
static void dm_test_format_mod_supported_d_swizzle_reject(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct drm_plane *plane;
	u64 listed_mod;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	plane = kunit_kzalloc(test, sizeof(*plane), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, plane);

	adev->family = AMDGPU_FAMILY_RV;
	plane->dev = &adev->ddev;

	listed_mod = AMD_FMT_MOD |
		     AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX9_64K_D) |
		     AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX9);
	plane->modifiers = &listed_mod;
	plane->modifier_count = 1;

	KUNIT_EXPECT_FALSE(test,
			   amdgpu_dm_plane_format_mod_supported(plane, DRM_FORMAT_XRGB8888,
								listed_mod));
}

/**
 * dm_test_atomic_async_check_rejects() - Verify async check rejections.
 * @test: KUnit test context.
 *
 * Verify if async flip on non-overlay planes and async cursor update on
 * non-cursor planes are rejected.
 */
static void dm_test_atomic_async_check_rejects(struct kunit *test)
{
	struct drm_plane *plane;

	plane = kunit_kzalloc(test, sizeof(*plane), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, plane);

	plane->type = DRM_PLANE_TYPE_PRIMARY;
	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_plane_atomic_async_check(plane, NULL, true),
			-EINVAL);
	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_plane_atomic_async_check(plane, NULL, false),
			-EINVAL);
}

/**
 * dm_test_atomic_async_check_overlay_cursor() - Verify overlay cursor rejection.
 * @test: KUnit test context.
 *
 * Verify if async cursor updates are rejected while the CRTC is using an
 * overlay cursor mode.
 */
static void dm_test_atomic_async_check_overlay_cursor(struct kunit *test)
{
	struct drm_atomic_commit *state;
	struct __drm_planes_state *planes;
	struct __drm_crtcs_state *crtcs;
	struct drm_plane *plane;
	struct drm_plane_state *plane_state;
	struct drm_crtc *crtc;
	struct dm_crtc_state *dm_crtc_state;

	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	planes = kunit_kzalloc(test, sizeof(*planes), GFP_KERNEL);
	crtcs = kunit_kzalloc(test, sizeof(*crtcs), GFP_KERNEL);
	plane = kunit_kzalloc(test, sizeof(*plane), GFP_KERNEL);
	plane_state = kunit_kzalloc(test, sizeof(*plane_state), GFP_KERNEL);
	crtc = kunit_kzalloc(test, sizeof(*crtc), GFP_KERNEL);
	dm_crtc_state = kunit_kzalloc(test, sizeof(*dm_crtc_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state);
	KUNIT_ASSERT_NOT_NULL(test, planes);
	KUNIT_ASSERT_NOT_NULL(test, crtcs);
	KUNIT_ASSERT_NOT_NULL(test, plane);
	KUNIT_ASSERT_NOT_NULL(test, plane_state);
	KUNIT_ASSERT_NOT_NULL(test, crtc);
	KUNIT_ASSERT_NOT_NULL(test, dm_crtc_state);

	plane->type = DRM_PLANE_TYPE_CURSOR;
	plane->index = 0;
	crtc->index = 0;
	plane_state->crtc = crtc;
	dm_crtc_state->cursor_mode = DM_CURSOR_OVERLAY_MODE;
	state->planes = planes;
	state->crtcs = crtcs;
	state->planes[0].new_state = plane_state;
	state->crtcs[0].new_state = &dm_crtc_state->base;

	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_plane_atomic_async_check(plane, state, false),
			-EINVAL);

	dm_crtc_state->cursor_mode = DM_CURSOR_NATIVE_MODE;
	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_plane_atomic_async_check(plane, state, false),
			0);
}

static struct amdgpu_device *dm_test_init_atomic_check_state(struct kunit *test,
							     struct drm_atomic_commit **state,
							     struct drm_plane **plane,
							     struct dm_plane_state **dm_plane_state,
							     struct drm_crtc_state **new_crtc_state,
							     struct drm_framebuffer **fb)
{
	struct amdgpu_device *adev;
	struct dc *dc;
	struct __drm_planes_state *planes;
	struct __drm_crtcs_state *crtcs;
	struct dc_plane_state *dc_plane_state;
	struct drm_crtc *crtc;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	*state = kunit_kzalloc(test, sizeof(**state), GFP_KERNEL);
	planes = kunit_kzalloc(test, sizeof(*planes), GFP_KERNEL);
	crtcs = kunit_kzalloc(test, sizeof(*crtcs), GFP_KERNEL);
	*plane = kunit_kzalloc(test, sizeof(**plane), GFP_KERNEL);
	*dm_plane_state = kunit_kzalloc(test, sizeof(**dm_plane_state), GFP_KERNEL);
	dc_plane_state = kunit_kzalloc(test, sizeof(*dc_plane_state), GFP_KERNEL);
	crtc = kunit_kzalloc(test, sizeof(*crtc), GFP_KERNEL);
	*new_crtc_state = kunit_kzalloc(test, sizeof(**new_crtc_state), GFP_KERNEL);
	*fb = kunit_kzalloc(test, sizeof(**fb), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, dc);
	KUNIT_ASSERT_NOT_NULL(test, *state);
	KUNIT_ASSERT_NOT_NULL(test, planes);
	KUNIT_ASSERT_NOT_NULL(test, crtcs);
	KUNIT_ASSERT_NOT_NULL(test, *plane);
	KUNIT_ASSERT_NOT_NULL(test, *dm_plane_state);
	KUNIT_ASSERT_NOT_NULL(test, dc_plane_state);
	KUNIT_ASSERT_NOT_NULL(test, crtc);
	KUNIT_ASSERT_NOT_NULL(test, *new_crtc_state);
	KUNIT_ASSERT_NOT_NULL(test, *fb);

	adev->dm.dc = dc;
	dc->caps.planes[0].max_upscale_factor.argb8888 = 1000;
	dc->caps.planes[0].max_downscale_factor.argb8888 = 1000;
	dc->caps.planes[0].max_upscale_factor.nv12 = 1000;
	dc->caps.planes[0].max_downscale_factor.nv12 = 1000;

	(*plane)->dev = &adev->ddev;
	(*plane)->index = 0;
	(*plane)->type = DRM_PLANE_TYPE_OVERLAY;
	(*plane)->name = "kunit-plane";
	crtc->dev = &adev->ddev;
	crtc->index = 0;
	(*fb)->width = 100;
	(*fb)->height = 100;
	(*fb)->format = drm_format_info(DRM_FORMAT_XRGB8888);
	KUNIT_ASSERT_NOT_NULL(test, (*fb)->format);

	(*dm_plane_state)->base.plane = *plane;
	(*dm_plane_state)->base.state = *state;
	(*dm_plane_state)->base.crtc = crtc;
	(*dm_plane_state)->base.fb = *fb;
	(*dm_plane_state)->base.src_w = 100 << 16;
	(*dm_plane_state)->base.src_h = 100 << 16;
	(*dm_plane_state)->base.crtc_w = 100;
	(*dm_plane_state)->base.crtc_h = 100;
	(*dm_plane_state)->dc_state = dc_plane_state;

	(*new_crtc_state)->crtc = crtc;
	(*new_crtc_state)->enable = true;
	(*new_crtc_state)->mode.crtc_hdisplay = 1920;
	(*new_crtc_state)->mode.crtc_vdisplay = 1080;

	(*state)->planes = planes;
	(*state)->crtcs = crtcs;
	(*state)->planes[0].new_state = &(*dm_plane_state)->base;
	(*state)->crtcs[0].new_state = *new_crtc_state;

	return adev;
}

/**
 * dm_test_atomic_check_no_dc_state() - Verify missing DC plane state succeeds.
 * @test: KUnit test context.
 *
 * Verify if atomic_check exits before deeper validation when the DM plane state
 * has no DC plane state attached.
 */
static void dm_test_atomic_check_no_dc_state(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct drm_atomic_commit *state;
	struct __drm_planes_state *planes;
	struct drm_plane *plane;
	struct dm_plane_state *dm_plane_state;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	planes = kunit_kzalloc(test, sizeof(*planes), GFP_KERNEL);
	plane = kunit_kzalloc(test, sizeof(*plane), GFP_KERNEL);
	dm_plane_state = kunit_kzalloc(test, sizeof(*dm_plane_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, state);
	KUNIT_ASSERT_NOT_NULL(test, planes);
	KUNIT_ASSERT_NOT_NULL(test, plane);
	KUNIT_ASSERT_NOT_NULL(test, dm_plane_state);

	plane->dev = &adev->ddev;
	plane->index = 0;
	dm_plane_state->base.plane = plane;
	state->planes = planes;
	state->planes[0].new_state = &dm_plane_state->base;

	KUNIT_EXPECT_EQ(test, amdgpu_dm_plane_atomic_check(plane, state), 0);
}

/**
 * dm_test_atomic_check_missing_crtc_state() - Verify missing CRTC state fails.
 * @test: KUnit test context.
 *
 * Verify if atomic_check rejects a plane with DC state when the atomic CRTC
 * state is absent.
 */
static void dm_test_atomic_check_missing_crtc_state(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct drm_atomic_commit *state;
	struct __drm_planes_state *planes;
	struct __drm_crtcs_state *crtcs;
	struct drm_plane *plane;
	struct dm_plane_state *dm_plane_state;
	struct dc_plane_state *dc_plane_state;
	struct drm_crtc *crtc;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	planes = kunit_kzalloc(test, sizeof(*planes), GFP_KERNEL);
	crtcs = kunit_kzalloc(test, sizeof(*crtcs), GFP_KERNEL);
	plane = kunit_kzalloc(test, sizeof(*plane), GFP_KERNEL);
	dm_plane_state = kunit_kzalloc(test, sizeof(*dm_plane_state), GFP_KERNEL);
	dc_plane_state = kunit_kzalloc(test, sizeof(*dc_plane_state), GFP_KERNEL);
	crtc = kunit_kzalloc(test, sizeof(*crtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, state);
	KUNIT_ASSERT_NOT_NULL(test, planes);
	KUNIT_ASSERT_NOT_NULL(test, crtcs);
	KUNIT_ASSERT_NOT_NULL(test, plane);
	KUNIT_ASSERT_NOT_NULL(test, dm_plane_state);
	KUNIT_ASSERT_NOT_NULL(test, dc_plane_state);
	KUNIT_ASSERT_NOT_NULL(test, crtc);

	plane->dev = &adev->ddev;
	plane->index = 0;
	crtc->index = 0;
	dm_plane_state->base.plane = plane;
	dm_plane_state->base.crtc = crtc;
	dm_plane_state->dc_state = dc_plane_state;
	state->planes = planes;
	state->crtcs = crtcs;
	state->planes[0].new_state = &dm_plane_state->base;

	KUNIT_EXPECT_EQ(test, amdgpu_dm_plane_atomic_check(plane, state), -EINVAL);
}

/**
 * dm_test_atomic_check_rejects() - Verify the checks before DC validation.
 * @test: KUnit test context.
 *
 * Verify if atomic_check returns before DC validation when the DRM helper state
 * validation rejects the plane, when both the plane COLOR_PIPELINE and the CRTC
 * DEGAMMA_LUT are in use, and when filling the scaling info fails.
 */
static void dm_test_atomic_check_rejects(struct kunit *test)
{
	struct drm_atomic_commit *state;
	struct drm_plane *plane;
	struct dm_plane_state *dm_plane_state;
	struct drm_crtc_state *new_crtc_state;
	struct drm_framebuffer *fb;
	struct amdgpu_device *adev;
	void *color_pipeline;
	void *degamma_lut;

	dm_test_init_atomic_check_state(test, &state, &plane, &dm_plane_state,
					&new_crtc_state, &fb);
	dm_plane_state->base.crtc_w = 10;

	KUNIT_EXPECT_EQ(test, amdgpu_dm_plane_atomic_check(plane, state), -EINVAL);

	dm_test_init_atomic_check_state(test, &state, &plane, &dm_plane_state,
					&new_crtc_state, &fb);
	color_pipeline = kunit_kzalloc(test, 1, GFP_KERNEL);
	degamma_lut = kunit_kzalloc(test, 1, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, color_pipeline);
	KUNIT_ASSERT_NOT_NULL(test, degamma_lut);

	dm_plane_state->base.color_pipeline = color_pipeline;
	new_crtc_state->degamma_lut = degamma_lut;

	KUNIT_EXPECT_EQ(test, amdgpu_dm_plane_atomic_check(plane, state), -EINVAL);

	adev = dm_test_init_atomic_check_state(test, &state, &plane, &dm_plane_state,
					       &new_crtc_state, &fb);
	adev->ip_versions[DCE_HWIP][0] = IP_VERSION(1, 0, 0);
	fb->width = 200;
	fb->format = drm_format_info(DRM_FORMAT_NV12);
	KUNIT_ASSERT_NOT_NULL(test, fb->format);
	dm_plane_state->base.src_x = 1 << 16;

	KUNIT_EXPECT_EQ(test, amdgpu_dm_plane_atomic_check(plane, state), -EINVAL);
}

/**
 * dm_test_panic_flush_no_dc_state() - Verify panic flush exits without DC state.
 * @test: KUnit test context.
 *
 * Verify if panic_flush returns without dereferencing DC state when the current
 * plane state has no DC plane state attached.
 */
static void dm_test_panic_flush_no_dc_state(struct kunit *test)
{
	struct drm_plane *plane;
	struct dm_plane_state *dm_plane_state;

	plane = kunit_kzalloc(test, sizeof(*plane), GFP_KERNEL);
	dm_plane_state = kunit_kzalloc(test, sizeof(*dm_plane_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, plane);
	KUNIT_ASSERT_NOT_NULL(test, dm_plane_state);

	plane->state = &dm_plane_state->base;

	amdgpu_dm_plane_panic_flush(plane);
}

static const struct drm_plane_funcs dm_test_plane_reset_funcs = {
	.atomic_destroy_state = amdgpu_dm_plane_drm_plane_destroy_state,
};

/**
 * dm_test_plane_create_state_initializes_state() - Verify create_state allocates default state.
 * @test: KUnit test context.
 *
 * Verify amdgpu_dm_plane_drm_plane_create_state() allocates a fresh
 * dm_plane_state, and initializes the AMD-specific transfer function and HDR
 * multiplier defaults.
 */
static void dm_test_plane_create_state_initializes_state(struct kunit *test)
{
	struct drm_plane_state *plane_state;
	struct dm_plane_state *new_state;
	struct drm_plane *plane;

	plane = kunit_kzalloc(test, sizeof(*plane), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, plane);

	plane->funcs = &dm_test_plane_reset_funcs;

	plane_state = amdgpu_dm_plane_drm_plane_create_state(plane);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, plane_state);

	new_state = to_dm_plane_state(plane_state);
	KUNIT_EXPECT_EQ(test, new_state->degamma_tf, AMDGPU_TRANSFER_FUNCTION_DEFAULT);
	KUNIT_EXPECT_EQ(test, new_state->hdr_mult, AMDGPU_HDR_MULT_DEFAULT);
	KUNIT_EXPECT_EQ(test, new_state->shaper_tf, AMDGPU_TRANSFER_FUNCTION_DEFAULT);
	KUNIT_EXPECT_EQ(test, new_state->blend_tf, AMDGPU_TRANSFER_FUNCTION_DEFAULT);

	amdgpu_dm_plane_drm_plane_destroy_state(plane, &new_state->base);
}

/**
 * dm_test_plane_duplicate_state_copies_fields() - Verify state duplication.
 * @test: KUnit test context.
 *
 * Verify amdgpu_dm_plane_drm_plane_duplicate_state() allocates a new state and
 * copies the transfer-function and HDR-multiplier fields from the current
 * plane state when no DC state or color blob is attached.
 */
static void dm_test_plane_duplicate_state_copies_fields(struct kunit *test)
{
	struct dm_plane_state *old_state;
	struct drm_plane_state *dup_base;
	struct dm_plane_state *dup_state;
	struct drm_plane *plane;

	plane = kunit_kzalloc(test, sizeof(*plane), GFP_KERNEL);
	old_state = kunit_kzalloc(test, sizeof(*old_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, plane);
	KUNIT_ASSERT_NOT_NULL(test, old_state);

	/* duplicate_state kmemdup()s these, so the source state must own them. */
	old_state->flip_addr = kunit_kzalloc(test, sizeof(*old_state->flip_addr), GFP_KERNEL);
	old_state->scaling_info = kunit_kzalloc(test, sizeof(*old_state->scaling_info), GFP_KERNEL);
	old_state->plane_info = kunit_kzalloc(test, sizeof(*old_state->plane_info), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, old_state->flip_addr);
	KUNIT_ASSERT_NOT_NULL(test, old_state->scaling_info);
	KUNIT_ASSERT_NOT_NULL(test, old_state->plane_info);

	old_state->degamma_tf = AMDGPU_TRANSFER_FUNCTION_PQ_EOTF;
	old_state->hdr_mult = 0x123456789ULL;
	old_state->shaper_tf = AMDGPU_TRANSFER_FUNCTION_IDENTITY;
	old_state->blend_tf = AMDGPU_TRANSFER_FUNCTION_SRGB_EOTF;
	plane->state = &old_state->base;

	dup_base = amdgpu_dm_plane_drm_plane_duplicate_state(plane);
	KUNIT_ASSERT_NOT_NULL(test, dup_base);

	dup_state = to_dm_plane_state(dup_base);
	KUNIT_EXPECT_EQ(test, dup_state->degamma_tf, AMDGPU_TRANSFER_FUNCTION_PQ_EOTF);
	KUNIT_EXPECT_EQ(test, dup_state->hdr_mult, 0x123456789ULL);
	KUNIT_EXPECT_EQ(test, dup_state->shaper_tf, AMDGPU_TRANSFER_FUNCTION_IDENTITY);
	KUNIT_EXPECT_EQ(test, dup_state->blend_tf, AMDGPU_TRANSFER_FUNCTION_SRGB_EOTF);
	KUNIT_EXPECT_NULL(test, dup_state->dc_state);

	amdgpu_dm_plane_drm_plane_destroy_state(plane, dup_base);
}

/*
 * Attach a blob to every color property of @state. Each blob starts with a
 * single reference that the tested function is expected to drop or share.
 */
static void dm_test_attach_color_blobs(struct kunit *test, struct drm_device *dev,
				       struct dm_plane_state *state)
{
	struct drm_property_blob **blobs[] = {
		&state->degamma_lut, &state->ctm, &state->lut3d,
		&state->shaper_lut, &state->blend_lut,
	};
	u32 blob_data = 0;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(blobs); i++) {
		*blobs[i] = drm_property_create_blob(dev, sizeof(blob_data), &blob_data);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, *blobs[i]);
	}
}

/**
 * dm_test_plane_duplicate_state_copies_resources() - Verify blob and DC state sharing.
 * @test: KUnit test context.
 *
 * Verify amdgpu_dm_plane_drm_plane_duplicate_state() shares the DC plane state
 * and every color blob with the duplicate, taking an extra reference on each.
 */
static void dm_test_plane_duplicate_state_copies_resources(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct dm_plane_state *old_state;
	struct drm_plane_state *dup_base;
	struct dm_plane_state *dup_state;
	struct dc_plane_state *dc_plane_state;
	struct drm_plane *plane;

	KUNIT_ASSERT_EQ(test, drmm_mode_config_init(&adev->ddev), 0);

	plane = kunit_kzalloc(test, sizeof(*plane), GFP_KERNEL);
	old_state = kunit_kzalloc(test, sizeof(*old_state), GFP_KERNEL);
	dc_plane_state = kunit_kzalloc(test, sizeof(*dc_plane_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, plane);
	KUNIT_ASSERT_NOT_NULL(test, old_state);
	KUNIT_ASSERT_NOT_NULL(test, dc_plane_state);

	kref_init(&dc_plane_state->refcount);
	old_state->dc_state = dc_plane_state;
	dm_test_attach_color_blobs(test, &adev->ddev, old_state);

	/* duplicate_state kmemdup()s these, so the source state must own them. */
	old_state->flip_addr = kunit_kzalloc(test, sizeof(*old_state->flip_addr), GFP_KERNEL);
	old_state->scaling_info = kunit_kzalloc(test, sizeof(*old_state->scaling_info), GFP_KERNEL);
	old_state->plane_info = kunit_kzalloc(test, sizeof(*old_state->plane_info), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, old_state->flip_addr);
	KUNIT_ASSERT_NOT_NULL(test, old_state->scaling_info);
	KUNIT_ASSERT_NOT_NULL(test, old_state->plane_info);

	plane->state = &old_state->base;

	dup_base = amdgpu_dm_plane_drm_plane_duplicate_state(plane);
	KUNIT_ASSERT_NOT_NULL(test, dup_base);

	dup_state = to_dm_plane_state(dup_base);
	KUNIT_EXPECT_PTR_EQ(test, dup_state->dc_state, dc_plane_state);
	KUNIT_EXPECT_EQ(test, kref_read(&dc_plane_state->refcount), 2U);
	KUNIT_EXPECT_PTR_EQ(test, dup_state->degamma_lut, old_state->degamma_lut);
	KUNIT_EXPECT_PTR_EQ(test, dup_state->ctm, old_state->ctm);
	KUNIT_EXPECT_PTR_EQ(test, dup_state->shaper_lut, old_state->shaper_lut);
	KUNIT_EXPECT_PTR_EQ(test, dup_state->lut3d, old_state->lut3d);
	KUNIT_EXPECT_PTR_EQ(test, dup_state->blend_lut, old_state->blend_lut);

	/* Drops the duplicate's references; the originals are released below. */
	amdgpu_dm_plane_drm_plane_destroy_state(plane, dup_base);

	drm_property_blob_put(old_state->degamma_lut);
	drm_property_blob_put(old_state->ctm);
	drm_property_blob_put(old_state->lut3d);
	drm_property_blob_put(old_state->shaper_lut);
	drm_property_blob_put(old_state->blend_lut);
}

/**
 * dm_test_plane_destroy_state_minimal() - Verify destroy of a minimal state.
 * @test: KUnit test context.
 *
 * Verify amdgpu_dm_plane_drm_plane_destroy_state() tears down a plane state
 * that has no color blobs or DC plane state attached without dereferencing
 * NULL resources.
 */
static void dm_test_plane_destroy_state_minimal(struct kunit *test)
{
	struct dm_plane_state *dm_plane_state;
	struct drm_plane *plane;

	plane = kunit_kzalloc(test, sizeof(*plane), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, plane);

	/* destroy_state frees the state itself, so use a plain allocation. */
	dm_plane_state = kzalloc_obj(*dm_plane_state);
	KUNIT_ASSERT_NOT_NULL(test, dm_plane_state);

	amdgpu_dm_plane_drm_plane_destroy_state(plane, &dm_plane_state->base);
}

/**
 * dm_test_plane_destroy_state_releases_resources() - Verify blob and DC state release.
 * @test: KUnit test context.
 *
 * Verify amdgpu_dm_plane_drm_plane_destroy_state() drops a reference on every
 * attached color blob and on the DC plane state.
 */
static void dm_test_plane_destroy_state_releases_resources(struct kunit *test)
{
	struct amdgpu_device *adev = dm_kunit_alloc_adev(test);
	struct dm_plane_state *dm_plane_state;
	struct dc_plane_state *dc_plane_state;
	struct drm_plane *plane;

	KUNIT_ASSERT_EQ(test, drmm_mode_config_init(&adev->ddev), 0);

	plane = kunit_kzalloc(test, sizeof(*plane), GFP_KERNEL);
	dc_plane_state = kunit_kzalloc(test, sizeof(*dc_plane_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, plane);
	KUNIT_ASSERT_NOT_NULL(test, dc_plane_state);

	/* destroy_state frees the state itself, so use a plain allocation. */
	dm_plane_state = kzalloc_obj(*dm_plane_state, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dm_plane_state);

	/* Hold an extra reference so the release does not free KUnit memory. */
	kref_init(&dc_plane_state->refcount);
	kref_get(&dc_plane_state->refcount);
	dm_plane_state->dc_state = dc_plane_state;
	dm_test_attach_color_blobs(test, &adev->ddev, dm_plane_state);

	amdgpu_dm_plane_drm_plane_destroy_state(plane, &dm_plane_state->base);

	KUNIT_EXPECT_EQ(test, kref_read(&dc_plane_state->refcount), 1U);
}

/**
 * dm_test_add_modifier_dedup() - Verify the de-duplicating append.
 * @test: KUnit test context.
 *
 * Verify if a modifier already present in the list is not appended a second
 * time, and if the append is a no-op once the list is NULL after an earlier
 * allocation failure.
 */
static void dm_test_add_modifier_dedup(struct kunit *test)
{
	u64 size = 2;
	u64 cap = 4;
	u64 *mods = kmalloc_array(cap, sizeof(*mods), GFP_KERNEL);

	KUNIT_ASSERT_NOT_NULL(test, mods);
	mods[0] = 0xAAULL;
	mods[1] = 0xBBULL;

	amdgpu_dm_plane_add_modifier_dedup(&mods, &size, &cap, 0xAAULL);
	KUNIT_EXPECT_EQ(test, size, 2ULL);

	amdgpu_dm_plane_add_modifier_dedup(&mods, &size, &cap, 0xBBULL);
	KUNIT_EXPECT_EQ(test, size, 2ULL);

	amdgpu_dm_plane_add_modifier_dedup(&mods, &size, &cap, 0xCCULL);
	KUNIT_ASSERT_EQ(test, size, 3ULL);
	KUNIT_EXPECT_EQ(test, mods[2], 0xCCULL);

	kfree(mods);

	size = 3;
	cap = 7;
	mods = NULL;

	amdgpu_dm_plane_add_modifier_dedup(&mods, &size, &cap, 0x55ULL);

	KUNIT_EXPECT_PTR_EQ(test, mods, NULL);
	KUNIT_EXPECT_EQ(test, size, 3ULL);
	KUNIT_EXPECT_EQ(test, cap, 7ULL);
}

static int dm_test_gfx6_tiling(struct dc_tiling_info *tiling_info, u64 modifier)
{
	return amdgpu_dm_plane_fill_gfx6_tiling_info_from_modifier(tiling_info, modifier);
}

/**
 * dm_test_fill_gfx6_tiling_info_from_modifier() - Verify GFX6-8 modifier decoding.
 * @test: KUnit test context.
 *
 * Verify if a linear modifier maps to the generic linear array mode, if non-AMD
 * modifiers and newer tile versions are rejected, if a 1D tiled modifier stops
 * before the macro tiling fields, and if a 2D tiled modifier decodes them all.
 */
static void dm_test_fill_gfx6_tiling_info_from_modifier(struct kunit *test)
{
	struct dc_tiling_info tiling_info = {0};
	u64 not_amd_mod = DRM_FORMAT_MOD_VENDOR_AMD;
	u64 gfx9_mod = AMD_FMT_MOD | AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX9);
	u64 mod_1d = AMD_FMT_MOD |
		     AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX6) |
		     AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX6_1D_TILED_THIN1) |
		     AMD_FMT_MOD_SET(MICROTILE, AMD_FMT_MOD_MICROTILE_DISPLAY) |
		     AMD_FMT_MOD_SET(NUM_BANKS, 3);
	u64 mod_2d = AMD_FMT_MOD |
		     AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX6) |
		     AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX6_2D_TILED_THIN1) |
		     AMD_FMT_MOD_SET(MICROTILE, AMD_FMT_MOD_MICROTILE_DISPLAY) |
		     AMD_FMT_MOD_SET(PIPE_CONFIG, 5) |
		     AMD_FMT_MOD_SET(TILE_SPLIT, 4) |
		     AMD_FMT_MOD_SET(BANK_WIDTH, 1) |
		     AMD_FMT_MOD_SET(BANK_HEIGHT, 2) |
		     AMD_FMT_MOD_SET(MACRO_TILE_ASPECT, 3) |
		     AMD_FMT_MOD_SET(NUM_BANKS, 2);

	KUNIT_EXPECT_EQ(test, dm_test_gfx6_tiling(&tiling_info, DRM_FORMAT_MOD_LINEAR), 0);
	KUNIT_EXPECT_EQ(test, (int)tiling_info.gfx8.array_mode, (int)DC_ARRAY_LINEAR_GENERAL);
	KUNIT_EXPECT_EQ(test, (int)tiling_info.gfxversion, (int)DcGfxVersion8);

	KUNIT_EXPECT_EQ(test, dm_test_gfx6_tiling(&tiling_info, not_amd_mod), -EINVAL);
	KUNIT_EXPECT_EQ(test, dm_test_gfx6_tiling(&tiling_info, gfx9_mod), -EINVAL);

	memset(&tiling_info, 0, sizeof(tiling_info));
	KUNIT_EXPECT_EQ(test, dm_test_gfx6_tiling(&tiling_info, mod_1d), 0);
	KUNIT_EXPECT_EQ(test, (int)tiling_info.gfx8.array_mode,
			(int)AMD_FMT_MOD_TILE_GFX6_1D_TILED_THIN1);
	KUNIT_EXPECT_EQ(test, (int)tiling_info.gfxversion, (int)DcGfxVersion8);
	KUNIT_EXPECT_EQ(test, tiling_info.gfx8.num_banks, 0U);

	memset(&tiling_info, 0, sizeof(tiling_info));
	KUNIT_EXPECT_EQ(test, dm_test_gfx6_tiling(&tiling_info, mod_2d), 0);
	KUNIT_EXPECT_EQ(test, (int)tiling_info.gfx8.array_mode,
			(int)AMD_FMT_MOD_TILE_GFX6_2D_TILED_THIN1);
	KUNIT_EXPECT_EQ(test, (int)tiling_info.gfx8.tile_mode,
			(int)AMD_FMT_MOD_MICROTILE_DISPLAY);
	KUNIT_EXPECT_EQ(test, tiling_info.gfx8.pipe_config, 5U);
	KUNIT_EXPECT_EQ(test, tiling_info.gfx8.tile_split, 4U);
	KUNIT_EXPECT_EQ(test, tiling_info.gfx8.bank_width, 1U);
	KUNIT_EXPECT_EQ(test, tiling_info.gfx8.bank_height, 2U);
	KUNIT_EXPECT_EQ(test, tiling_info.gfx8.tile_aspect, 3U);
	KUNIT_EXPECT_EQ(test, tiling_info.gfx8.num_banks, 2U);
}

static u64 dm_test_gfx6_mod(u32 tile, u32 pipe_config, u32 tile_split, u32 bank_width,
			    u32 bank_height, u32 aspect, u32 num_banks)
{
	return AMD_FMT_MOD |
	       AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX6) |
	       AMD_FMT_MOD_SET(TILE, tile) |
	       AMD_FMT_MOD_SET(MICROTILE, AMD_FMT_MOD_MICROTILE_DISPLAY) |
	       AMD_FMT_MOD_SET(PIPE_CONFIG, pipe_config) |
	       AMD_FMT_MOD_SET(TILE_SPLIT, tile_split) |
	       AMD_FMT_MOD_SET(BANK_WIDTH, bank_width) |
	       AMD_FMT_MOD_SET(BANK_HEIGHT, bank_height) |
	       AMD_FMT_MOD_SET(MACRO_TILE_ASPECT, aspect) |
	       AMD_FMT_MOD_SET(NUM_BANKS, num_banks);
}

static struct amdgpu_device *dm_test_alloc_gfx7_device(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, adev);

	adev->family = AMDGPU_FAMILY_VI;
	adev->gfx.config.mem_row_size_in_kb = 4;
	/* 2D_TILED_THIN1 array mode (bits 5:2) and pipe config 2 (bits 10:6). */
	adev->gfx.config.tile_mode_array[10] = (4u << 2) | (2u << 6);
	/*
	 * Bank width (bits 1:0), bank height (bits 3:2), macro tile aspect
	 * (bits 5:4) and num banks (bits 7:6). Entries 1 and 2 are identical so
	 * the 16 and 32 bpp modifiers collide and the dedup path is taken.
	 */
	adev->gfx.config.macrotile_mode_array[1] = 0xb9u;
	adev->gfx.config.macrotile_mode_array[2] = 0xb9u;
	adev->gfx.config.macrotile_mode_array[3] = 0x2au;

	return adev;
}

/**
 * dm_test_get_gfx6_tile_idx() - Verify GFX6-8 tile mode index selection.
 * @test: KUnit test context.
 *
 * Verify if the micro tiled mode maps to a fixed index, GFX7+ uses a single
 * macro tiled index, and GFX6 selects the index from the bits per pixel.
 */
static void dm_test_get_gfx6_tile_idx(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, adev);

	adev->family = AMDGPU_FAMILY_VI;
	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_plane_get_gfx6_tile_idx(adev, 32, DC_ARRAY_1D_TILED_THIN1),
			9U);
	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_plane_get_gfx6_tile_idx(adev, 32, DC_ARRAY_2D_TILED_THIN1),
			10U);

	adev->family = AMDGPU_FAMILY_SI;
	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_plane_get_gfx6_tile_idx(adev, 8, DC_ARRAY_2D_TILED_THIN1),
			10U);
	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_plane_get_gfx6_tile_idx(adev, 16, DC_ARRAY_2D_TILED_THIN1),
			11U);
	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_plane_get_gfx6_tile_idx(adev, 32, DC_ARRAY_2D_TILED_THIN1),
			12U);
}

/**
 * dm_test_calc_gfx7_tile_split() - Verify GFX7-8 tile split calculation.
 * @test: KUnit test context.
 *
 * Verify if the tile split is derived from the sample split factor and clamped
 * to the 256 byte minimum and the memory row size maximum.
 */
static void dm_test_calc_gfx7_tile_split(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, adev);

	adev->gfx.config.mem_row_size_in_kb = 4;

	/* 8 bpp gives 64 bytes, clamped up to the 256 byte minimum. */
	KUNIT_EXPECT_EQ(test, amdgpu_dm_plane_calc_gfx7_tile_split(adev, 8, 0u << 25), 256U);
	/* 32 bpp with a sample split factor of 2. */
	KUNIT_EXPECT_EQ(test, amdgpu_dm_plane_calc_gfx7_tile_split(adev, 32, 1u << 25), 512U);

	/* 64 bpp with a sample split factor of 8, clamped to the memory row size. */
	adev->gfx.config.mem_row_size_in_kb = 1;
	KUNIT_EXPECT_EQ(test, amdgpu_dm_plane_calc_gfx7_tile_split(adev, 64, 3u << 25), 1024U);
}

/**
 * dm_test_get_gfx7_macro_tile_idx() - Verify GFX7-8 macro tile index selection.
 * @test: KUnit test context.
 *
 * Verify if the macro tile index is the log2 of the tile size in 64 byte units,
 * clamped to the tile split.
 */
static void dm_test_get_gfx7_macro_tile_idx(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, amdgpu_dm_plane_get_gfx7_macro_tile_idx(8, 1024), 0U);
	KUNIT_EXPECT_EQ(test, amdgpu_dm_plane_get_gfx7_macro_tile_idx(32, 1024), 2U);
	/* 64 bpp needs 512 bytes but the tile split caps it at 256. */
	KUNIT_EXPECT_EQ(test, amdgpu_dm_plane_get_gfx7_macro_tile_idx(64, 256), 2U);
}

/**
 * dm_test_calc_gfx6_mod() - Verify the GFX6-8 modifier calculation.
 * @test: KUnit test context.
 *
 * Verify if a micro tiled array mode returns early with only the base modifier
 * fields set, if GFX7-8 combines the tile mode pipe config and computed tile
 * split with the bank fields from the macro tile mode table, and if GFX6 takes
 * the tile split and bank fields directly from the tile mode register.
 */
static void dm_test_calc_gfx6_mod(struct kunit *test)
{
	struct amdgpu_device *adev = dm_test_alloc_gfx7_device(test);
	struct amdgpu_device *si_adev;
	u64 expected_1d = AMD_FMT_MOD |
			  AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX6) |
			  AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX6_1D_TILED_THIN1) |
			  AMD_FMT_MOD_SET(MICROTILE, AMD_FMT_MOD_MICROTILE_DISPLAY);

	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_plane_calc_gfx6_mod(adev, 0, DC_ARRAY_1D_TILED_THIN1),
			expected_1d);

	/* 32 bpp: 256 byte tile split (log2 of 4 units), macro tile mode 2. */
	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_plane_calc_gfx6_mod(adev, 32, DC_ARRAY_2D_TILED_THIN1),
			dm_test_gfx6_mod(AMD_FMT_MOD_TILE_GFX6_2D_TILED_THIN1, 2, 2, 1, 2, 3, 2));

	/* 64 bpp: 512 byte tile split (log2 of 8 units), macro tile mode 3. */
	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_plane_calc_gfx6_mod(adev, 64, DC_ARRAY_2D_TILED_THIN1),
			dm_test_gfx6_mod(AMD_FMT_MOD_TILE_GFX6_2D_TILED_THIN1, 2, 3, 2, 2, 2, 0));

	si_adev = kunit_kzalloc(test, sizeof(*si_adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, si_adev);

	si_adev->family = AMDGPU_FAMILY_SI;
	/* 2D_TILED_THIN1, pipe config 3, 256 byte tile split, banks 1/2/3/1. */
	si_adev->gfx.config.tile_mode_array[12] = (4u << 2) | (3u << 6) | (2u << 11) |
						  (1u << 14) | (2u << 16) | (3u << 18) |
						  (1u << 20);

	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_plane_calc_gfx6_mod(si_adev, 32, DC_ARRAY_2D_TILED_THIN1),
			dm_test_gfx6_mod(AMD_FMT_MOD_TILE_GFX6_2D_TILED_THIN1, 3, 2, 1, 2, 3, 1));
}

/**
 * dm_test_gfx6_format_mod_supported() - Verify GFX6-8 modifier support checks.
 * @test: KUnit test context.
 *
 * Verify if only the canonical modifier for a given bits per pixel is accepted,
 * and that non-AMD modifiers, newer tile versions, GFX9+ devices, DCC, and
 * non-displayable tile modes are rejected.
 */
static void dm_test_gfx6_format_mod_supported(struct kunit *test)
{
	struct amdgpu_device *adev = dm_test_alloc_gfx7_device(test);
	u64 modifier = amdgpu_dm_plane_calc_gfx6_mod(adev, 32, DC_ARRAY_2D_TILED_THIN1);
	u64 gfx9_mod = AMD_FMT_MOD | AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX9);
	/* Neither 1D nor 2D THIN array mode. */
	u64 bad_tile_mod = AMD_FMT_MOD | AMD_FMT_MOD_SET(TILE, 3);
	/* Non-displayable micro tile mode. */
	u64 bad_micro_mod = AMD_FMT_MOD |
			    AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX6_2D_TILED_THIN1) |
			    AMD_FMT_MOD_SET(MICROTILE, AMD_FMT_MOD_MICROTILE_THIN);
	u64 not_amd_mod = DRM_FORMAT_MOD_VENDOR_AMD;
	u64 dcc_mod = modifier | AMD_FMT_MOD_SET(DCC, 1);

	KUNIT_EXPECT_TRUE(test, amdgpu_dm_plane_gfx6_format_mod_supported(adev, 32, modifier));

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_plane_gfx6_format_mod_supported(adev, 64, modifier));
	KUNIT_EXPECT_FALSE(test, amdgpu_dm_plane_gfx6_format_mod_supported(adev, 32, not_amd_mod));
	KUNIT_EXPECT_FALSE(test, amdgpu_dm_plane_gfx6_format_mod_supported(adev, 32, gfx9_mod));
	KUNIT_EXPECT_FALSE(test, amdgpu_dm_plane_gfx6_format_mod_supported(adev, 32, dcc_mod));
	KUNIT_EXPECT_FALSE(test, amdgpu_dm_plane_gfx6_format_mod_supported(adev, 32, bad_tile_mod));
	KUNIT_EXPECT_FALSE(test, amdgpu_dm_plane_gfx6_format_mod_supported(adev, 32, bad_micro_mod));

	adev->family = AMDGPU_FAMILY_AI;
	KUNIT_EXPECT_FALSE(test, amdgpu_dm_plane_gfx6_format_mod_supported(adev, 32, modifier));
}

/**
 * dm_test_get_plane_modifiers_gfx6() - Verify GFX6-8 modifier list generation.
 * @test: KUnit test context.
 *
 * Verify if a GFX6-8 family device exposes one macro tiled modifier per
 * distinct bits per pixel plus a micro tiled one, and that the duplicate 32 bpp
 * modifier is de-duplicated.
 */
static void dm_test_get_plane_modifiers_gfx6(struct kunit *test)
{
	struct amdgpu_device *adev = dm_test_alloc_gfx7_device(test);
	u64 *mods = dm_test_get_primary_mods(test, adev);
	u64 mod_1d = AMD_FMT_MOD |
		     AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX6) |
		     AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX6_1D_TILED_THIN1) |
		     AMD_FMT_MOD_SET(MICROTILE, AMD_FMT_MOD_MICROTILE_DISPLAY);

	/* 16 bpp, 64 bpp, micro tiled and linear; the 32 bpp duplicate is dropped. */
	KUNIT_EXPECT_EQ(test, dm_test_mods_count(mods), 4);
	KUNIT_EXPECT_EQ(test, mods[0],
			dm_test_gfx6_mod(AMD_FMT_MOD_TILE_GFX6_2D_TILED_THIN1, 2, 2, 1, 2, 3, 2));
	KUNIT_EXPECT_EQ(test, mods[1],
			dm_test_gfx6_mod(AMD_FMT_MOD_TILE_GFX6_2D_TILED_THIN1, 2, 3, 2, 2, 2, 0));
	KUNIT_EXPECT_EQ(test, mods[2], mod_1d);
	KUNIT_EXPECT_EQ(test, mods[3], DRM_FORMAT_MOD_LINEAR);

	kfree(mods);
}

/**
 * dm_test_format_mod_supported_gfx6() - Verify dispatch to the GFX6-8 check.
 * @test: KUnit test context.
 *
 * Verify if a listed GFX6 tile version modifier is validated through the
 * GFX6-8 specific helper.
 */
static void dm_test_format_mod_supported_gfx6(struct kunit *test)
{
	struct amdgpu_device *adev = dm_test_alloc_gfx7_device(test);
	struct drm_plane *plane;
	u64 listed_mod = amdgpu_dm_plane_calc_gfx6_mod(adev, 32, DC_ARRAY_2D_TILED_THIN1);

	plane = kunit_kzalloc(test, sizeof(*plane), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, plane);

	plane->dev = &adev->ddev;
	plane->modifiers = &listed_mod;
	plane->modifier_count = 1;

	KUNIT_EXPECT_TRUE(test,
			  amdgpu_dm_plane_format_mod_supported(plane, DRM_FORMAT_XRGB8888,
							       listed_mod));
	KUNIT_EXPECT_FALSE(test,
			   amdgpu_dm_plane_format_mod_supported(plane, DRM_FORMAT_XRGB16161616,
								listed_mod));
}

/**
 * dm_test_helper_prepare_fb_no_fb() - Verify prepare_fb without a framebuffer.
 * @test: KUnit test context.
 *
 * Verify if prepare_fb succeeds without touching any buffer object when the new
 * plane state has no framebuffer bound.
 */
static void dm_test_helper_prepare_fb_no_fb(struct kunit *test)
{
	struct drm_plane_state state = {0};
	struct drm_plane *plane;

	plane = kunit_kzalloc(test, sizeof(*plane), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, plane);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_plane_helper_prepare_fb(plane, &state), 0);
}

/**
 * dm_test_helper_cleanup_fb_no_fb() - Verify cleanup_fb without a framebuffer.
 * @test: KUnit test context.
 *
 * Verify if cleanup_fb returns without unpinning anything when the old plane
 * state has no framebuffer bound.
 */
static void dm_test_helper_cleanup_fb_no_fb(struct kunit *test)
{
	struct drm_plane_state state = {0};
	struct drm_plane *plane;

	plane = kunit_kzalloc(test, sizeof(*plane), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, plane);

	amdgpu_dm_plane_helper_cleanup_fb(plane, &state);
}

/**
 * dm_test_handle_cursor_update_no_fb() - Verify cursor update without framebuffers.
 * @test: KUnit test context.
 *
 * Verify if the cursor update returns early, without touching the CRTC, when
 * neither the new nor the old plane state has a framebuffer bound.
 */
static void dm_test_handle_cursor_update_no_fb(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct drm_plane_state old_state = {0};
	struct drm_plane_state state = {0};
	struct drm_plane *plane;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	plane = kunit_kzalloc(test, sizeof(*plane), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, plane);

	plane->dev = &adev->ddev;
	plane->state = &state;

	amdgpu_dm_plane_handle_cursor_update(plane, &old_state);
}

/**
 * dm_test_atomic_async_update_copies_state() - Verify async cursor state copy.
 * @test: KUnit test context.
 *
 * Verify if the async update swaps the framebuffer and copies the source and
 * CRTC rectangles from the new state into the current plane state.
 */
static void dm_test_atomic_async_update_copies_state(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct drm_atomic_commit *state;
	struct __drm_planes_state *planes;
	struct drm_plane_state *cur_state;
	struct drm_plane_state *new_state;
	struct drm_plane_state *old_state;
	struct amdgpu_framebuffer *afb;
	struct drm_plane *plane;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	planes = kunit_kzalloc(test, sizeof(*planes), GFP_KERNEL);
	plane = kunit_kzalloc(test, sizeof(*plane), GFP_KERNEL);
	cur_state = kunit_kzalloc(test, sizeof(*cur_state), GFP_KERNEL);
	new_state = kunit_kzalloc(test, sizeof(*new_state), GFP_KERNEL);
	old_state = kunit_kzalloc(test, sizeof(*old_state), GFP_KERNEL);
	afb = kunit_kzalloc(test, sizeof(*afb), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, state);
	KUNIT_ASSERT_NOT_NULL(test, planes);
	KUNIT_ASSERT_NOT_NULL(test, plane);
	KUNIT_ASSERT_NOT_NULL(test, cur_state);
	KUNIT_ASSERT_NOT_NULL(test, new_state);
	KUNIT_ASSERT_NOT_NULL(test, old_state);
	KUNIT_ASSERT_NOT_NULL(test, afb);

	plane->dev = &adev->ddev;
	plane->index = 0;
	plane->state = cur_state;
	cur_state->fb = &afb->base;
	new_state->src_x = 1 << 16;
	new_state->src_y = 2 << 16;
	new_state->src_w = 64 << 16;
	new_state->src_h = 64 << 16;
	new_state->crtc_x = 10;
	new_state->crtc_y = 20;
	new_state->crtc_w = 64;
	new_state->crtc_h = 64;
	state->planes = planes;
	state->planes[0].new_state = new_state;
	state->planes[0].old_state = old_state;

	amdgpu_dm_plane_atomic_async_update(plane, state);

	KUNIT_EXPECT_PTR_EQ(test, cur_state->fb, NULL);
	KUNIT_EXPECT_PTR_EQ(test, new_state->fb, &afb->base);
	KUNIT_EXPECT_EQ(test, cur_state->src_x, 1U << 16);
	KUNIT_EXPECT_EQ(test, cur_state->src_h, 64U << 16);
	KUNIT_EXPECT_EQ(test, cur_state->crtc_x, 10);
	KUNIT_EXPECT_EQ(test, cur_state->crtc_h, 64U);
}

/**
 * dm_test_fill_plane_buffer_attributes_gfx6() - Verify the pre-GFX9 dispatch.
 * @test: KUnit test context.
 *
 * Verify if a pre-Vega family decodes tiling from the GFX6 modifier instead of
 * taking the GFX9 or GFX12 modifier paths, and propagates the decode error for
 * a modifier the GFX6 decoder does not understand.
 */
static void dm_test_fill_plane_buffer_attributes_gfx6(struct kunit *test)
{
	struct dc_tiling_info tiling_info = {0};
	struct dc_plane_dcc_param dcc = {0};
	struct dc_plane_address address = {0};
	struct plane_size plane_size = {0};
	struct amdgpu_framebuffer *afb;
	struct amdgpu_device *adev;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	afb = kunit_kzalloc(test, sizeof(*afb), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, afb);

	adev->family = AMDGPU_FAMILY_CZ;
	afb->address = 0x80000000ULL;
	afb->base.width = 1920;
	afb->base.height = 1080;
	afb->base.pitches[0] = 7680;
	afb->base.format = drm_format_info(DRM_FORMAT_XRGB8888);
	KUNIT_ASSERT_NOT_NULL(test, afb->base.format);
	afb->base.modifier = AMD_FMT_MOD |
			     AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX6) |
			     AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX6_1D_TILED_THIN1) |
			     AMD_FMT_MOD_SET(MICROTILE, AMD_FMT_MOD_MICROTILE_DISPLAY);

	KUNIT_EXPECT_EQ(test, dm_test_graphics_attrs(adev, afb, &tiling_info,
						     &plane_size, &dcc, &address), 0);
	KUNIT_EXPECT_EQ(test, address.type, (int)PLN_ADDR_TYPE_GRAPHICS);
	KUNIT_EXPECT_EQ(test, (int)tiling_info.gfxversion, (int)DcGfxVersion8);

	afb->base.modifier = AMD_FMT_MOD |
			     AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX9);

	KUNIT_EXPECT_EQ(test, dm_test_graphics_attrs(adev, afb, &tiling_info,
						     &plane_size, &dcc, &address), -EINVAL);
}

/**
 * dm_test_atomic_check_success() - Verify a fully valid plane state is accepted.
 * @test: KUnit test context.
 *
 * Verify if atomic_check reports success once DC validation accepts the plane.
 * The resource pool exposes no validate_plane callback, so DC falls back to
 * checking the source and destination rectangles only.
 */
static void dm_test_atomic_check_success(struct kunit *test)
{
	struct dm_plane_state *dm_plane_state;
	struct drm_crtc_state *new_crtc_state;
	struct drm_atomic_commit *state;
	struct resource_pool *res_pool;
	struct amdgpu_device *adev;
	struct drm_framebuffer *fb;
	struct drm_plane *plane;

	adev = dm_test_init_atomic_check_state(test, &state, &plane, &dm_plane_state,
					       &new_crtc_state, &fb);
	res_pool = kunit_kzalloc(test, sizeof(*res_pool), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, res_pool);
	res_pool->funcs = kunit_kzalloc(test, sizeof(*res_pool->funcs), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, res_pool->funcs);

	adev->dm.dc->res_pool = res_pool;

	dm_plane_state->dc_state->src_rect.width = 100;
	dm_plane_state->dc_state->src_rect.height = 100;
	dm_plane_state->dc_state->dst_rect.width = 100;
	dm_plane_state->dc_state->dst_rect.height = 100;

	KUNIT_EXPECT_EQ(test, amdgpu_dm_plane_atomic_check(plane, state), 0);
}

struct dm_test_cursor_ctx {
	struct amdgpu_device *adev;
	struct amdgpu_crtc *acrtc;
	struct dm_crtc_state *crtc_state;
	struct drm_plane_state *state;
	struct drm_plane_state old_state;
	struct drm_plane *plane;
};

/*
 * Build a 64x64 cursor plane bound to an amdgpu_crtc whose DM CRTC state
 * carries no DC stream, so the cursor update stops before programming DC.
 */
static struct dm_test_cursor_ctx *dm_test_alloc_cursor_ctx(struct kunit *test)
{
	struct amdgpu_framebuffer *afb;
	struct dm_test_cursor_ctx *ctx;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	afb = kunit_kzalloc(test, sizeof(*afb), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);
	KUNIT_ASSERT_NOT_NULL(test, afb);

	ctx->adev = dm_kunit_alloc_adev(test);
	ctx->acrtc = kunit_kzalloc(test, sizeof(*ctx->acrtc), GFP_KERNEL);
	ctx->crtc_state = kunit_kzalloc(test, sizeof(*ctx->crtc_state), GFP_KERNEL);
	ctx->state = kunit_kzalloc(test, sizeof(*ctx->state), GFP_KERNEL);
	ctx->plane = kunit_kzalloc(test, sizeof(*ctx->plane), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->acrtc);
	KUNIT_ASSERT_NOT_NULL(test, ctx->crtc_state);
	KUNIT_ASSERT_NOT_NULL(test, ctx->state);
	KUNIT_ASSERT_NOT_NULL(test, ctx->plane);

	ctx->adev->dm.dc = dm_kunit_alloc_dc_with_ctx(test);

	ctx->acrtc->base.dev = &ctx->adev->ddev;
	ctx->acrtc->base.state = &ctx->crtc_state->base;
	ctx->acrtc->max_cursor_width = 64;
	ctx->acrtc->max_cursor_height = 64;

	afb->address = 0x80000000ULL;
	afb->base.pitches[0] = 256;
	afb->base.format = drm_format_info(DRM_FORMAT_ARGB8888);
	KUNIT_ASSERT_NOT_NULL(test, afb->base.format);

	ctx->state->fb = &afb->base;
	ctx->state->crtc = &ctx->acrtc->base;
	ctx->state->crtc_w = 64;
	ctx->state->crtc_h = 64;

	ctx->plane->dev = &ctx->adev->ddev;
	ctx->plane->state = ctx->state;

	return ctx;
}

/**
 * dm_test_handle_cursor_update_disabled() - Verify the cursor-off path.
 * @test: KUnit test context.
 *
 * Verify if a cursor positioned fully off the left edge is treated as disabled
 * and returns before the cursor geometry is latched on the CRTC.
 */
static void dm_test_handle_cursor_update_disabled(struct kunit *test)
{
	struct dm_test_cursor_ctx *ctx = dm_test_alloc_cursor_ctx(test);

	ctx->state->crtc_x = -64;

	amdgpu_dm_plane_handle_cursor_update(ctx->plane, &ctx->old_state);

	KUNIT_EXPECT_EQ(test, ctx->acrtc->cursor_width, 0);
	KUNIT_EXPECT_EQ(test, ctx->acrtc->cursor_height, 0);
}

/**
 * dm_test_handle_cursor_update_no_stream() - Verify cursor attribute assembly.
 * @test: KUnit test context.
 *
 * Verify if an enabled cursor latches its geometry on the CRTC and builds the
 * DC cursor attributes, including the degamma ROM bit, before stopping at the
 * missing DC stream.
 */
static void dm_test_handle_cursor_update_no_stream(struct kunit *test)
{
	struct dm_test_cursor_ctx *ctx = dm_test_alloc_cursor_ctx(test);

	ctx->crtc_state->cm_is_degamma_srgb = true;
	ctx->adev->dm.dc->caps.color.dpp.gamma_corr = true;

	amdgpu_dm_plane_handle_cursor_update(ctx->plane, &ctx->old_state);

	KUNIT_EXPECT_EQ(test, ctx->acrtc->cursor_width, 64);
	KUNIT_EXPECT_EQ(test, ctx->acrtc->cursor_height, 64);
}

static int dm_test_clear_dcc_tiling_count;

static void dm_test_clear_surface_dcc_and_tiling(struct pipe_ctx *pipe_ctx,
						 struct dc_plane_state *plane_state,
						 bool clear_tiling)
{
	if (clear_tiling)
		dm_test_clear_dcc_tiling_count++;
}

/**
 * dm_test_panic_flush_disables_dcc() - Verify panic flush disables DCC.
 * @test: KUnit test context.
 *
 * Verify if panic_flush asks DC to clear DCC on the active pipe, and requests
 * the tiling teardown as well when the framebuffer is not linear.
 */
static void dm_test_panic_flush_disables_dcc(struct kunit *test)
{
	struct dc_plane_state *dc_plane_state;
	struct dm_plane_state *dm_plane_state;
	struct resource_pool *res_pool;
	struct drm_framebuffer *fb;
	struct drm_plane *plane;
	struct dc *dc;

	plane = kunit_kzalloc(test, sizeof(*plane), GFP_KERNEL);
	dm_plane_state = kunit_kzalloc(test, sizeof(*dm_plane_state), GFP_KERNEL);
	dc_plane_state = kunit_kzalloc(test, sizeof(*dc_plane_state), GFP_KERNEL);
	fb = kunit_kzalloc(test, sizeof(*fb), GFP_KERNEL);
	res_pool = kunit_kzalloc(test, sizeof(*res_pool), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, plane);
	KUNIT_ASSERT_NOT_NULL(test, dm_plane_state);
	KUNIT_ASSERT_NOT_NULL(test, dc_plane_state);
	KUNIT_ASSERT_NOT_NULL(test, fb);
	KUNIT_ASSERT_NOT_NULL(test, res_pool);

	dc = dm_kunit_alloc_dc_with_ctx(test);
	dc->current_state = dm_kunit_alloc_dc_state(test);
	dc->hwss.clear_surface_dcc_and_tiling = dm_test_clear_surface_dcc_and_tiling;
	res_pool->pipe_count = 1;
	dc->res_pool = res_pool;
	dc_plane_state->ctx = dc->ctx;

	fb->modifier = AMD_FMT_MOD;
	dm_plane_state->base.fb = fb;
	dm_plane_state->dc_state = dc_plane_state;
	plane->state = &dm_plane_state->base;

	dm_test_clear_dcc_tiling_count = 0;

	amdgpu_dm_plane_panic_flush(plane);

	KUNIT_EXPECT_EQ(test, dm_test_clear_dcc_tiling_count, 1);
}

/*
 * Build a DM ready to initialize planes on a real DRM device. The plane itself
 * is plain kzalloc'd because DRM frees it through drm_plane_helper_destroy()
 * when the mode config is torn down.
 */
static struct drm_plane *dm_test_alloc_init_plane(struct kunit *test,
						  struct amdgpu_device **adev_out)
{
	struct amdgpu_device *adev;
	struct drm_plane *plane;

	adev = dm_kunit_alloc_adev(test);
	adev->family = AMDGPU_FAMILY_NV;
	adev->dm.adev = adev;
	adev->dm.dc = dm_kunit_alloc_dc_with_ctx(test);

	plane = kzalloc_obj(*plane, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, plane);
	plane->type = DRM_PLANE_TYPE_OVERLAY;

	*adev_out = adev;

	return plane;
}

/**
 * dm_test_plane_init_creates_optional_properties() - Verify optional properties.
 * @test: KUnit test context.
 *
 * Verify if a Bonaire-or-newer non-cursor plane on a DCE version above 3.0.1
 * gains the rotation property and framebuffer damage clips.
 */
static void dm_test_plane_init_creates_optional_properties(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct drm_plane *plane;

	plane = dm_test_alloc_init_plane(test, &adev);
	adev->asic_type = CHIP_BONAIRE;
	adev->ip_versions[DCE_HWIP][0] = IP_VERSION(3, 1, 0);

	KUNIT_ASSERT_EQ(test, amdgpu_dm_plane_init(&adev->dm, plane, 1, NULL), 0);

	KUNIT_EXPECT_NOT_NULL(test, plane->rotation_property);
	KUNIT_EXPECT_NOT_NULL(test, adev->ddev.mode_config.prop_fb_damage_clips);
}

/**
 * dm_test_plane_init_creates_color_pipeline() - Verify color pipeline creation.
 * @test: KUnit test context.
 *
 * Verify if a non-cursor plane on DCN 3.0 or newer gets a default color
 * pipeline exposed through the COLOR_PIPELINE property.
 */
static void dm_test_plane_init_creates_color_pipeline(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct drm_plane *plane;

	plane = dm_test_alloc_init_plane(test, &adev);
	adev->dm.dc->ctx->dce_version = DCN_VERSION_3_0;

	KUNIT_ASSERT_EQ(test, amdgpu_dm_plane_init(&adev->dm, plane, 1, NULL), 0);

	KUNIT_EXPECT_NOT_NULL(test, plane->color_pipeline_property);
}

static struct kunit_case amdgpu_dm_plane_test_cases[] = {
	/* amdgpu_dm_plane_is_video_format() */
	KUNIT_CASE(dm_test_plane_is_video_format_known_video),
	/* amdgpu_dm_plane_get_format_info() */
	KUNIT_CASE(dm_test_get_format_info),
	/* amdgpu_dm_plane_fill_blending_from_plane_state() */
	KUNIT_CASE(dm_test_fill_blending_pixel_blend_modes),
	KUNIT_CASE(dm_test_fill_blending_global_alpha),
	/* amdgpu_dm_plane_modifier_* helpers() */
	KUNIT_CASE(dm_test_modifier_has_dcc),
	KUNIT_CASE(dm_test_modifier_gfx9_swizzle_mode),
	/* amdgpu_dm_plane_get_plane_formats() */
	KUNIT_CASE(dm_test_get_plane_formats),
	/* amdgpu_dm_plane_get_plane_modifiers() */
	KUNIT_CASE(dm_test_get_plane_modifiers),
	KUNIT_CASE(dm_test_get_plane_modifiers_gfx6),
	KUNIT_CASE(dm_test_get_plane_modifiers_gfx9),
	KUNIT_CASE(dm_test_get_plane_modifiers_rv),
	KUNIT_CASE(dm_test_get_plane_modifiers_gfx10_and_gfx12),
	KUNIT_CASE(dm_test_get_plane_modifiers_gfx11_64k_first),
	KUNIT_CASE(dm_test_get_plane_modifiers_gfx11_256k_first),
	/* amdgpu_dm_plane_fill_dc_scaling_info() */
	KUNIT_CASE(dm_test_fill_dc_scaling_info),
	KUNIT_CASE(dm_test_fill_dc_scaling_info_nv12_dcn1x),
	KUNIT_CASE(dm_test_fill_dc_scaling_info_plane_caps),
	/* amdgpu_dm_plane_get_min_max_dc_plane_scaling() */
	KUNIT_CASE(dm_test_get_min_max_dc_plane_scaling),
	/* amdgpu_dm_plane_fill_plane_buffer_attributes() */
	KUNIT_CASE(dm_test_fill_plane_buffer_attributes_video),
	KUNIT_CASE(dm_test_fill_plane_buffer_attributes_gfx12),
	KUNIT_CASE(dm_test_fill_plane_buffer_attributes_gfx6),
	/* amdgpu_dm_plane_get_cursor_position() */
	KUNIT_CASE(dm_test_get_cursor_position),
	/* amdgpu_dm_plane_format_mod_supported() */
	KUNIT_CASE(dm_test_format_mod_supported),
	KUNIT_CASE(dm_test_format_mod_supported_d_swizzle_reject),
	KUNIT_CASE(dm_test_format_mod_supported_gfx6),
	/* amdgpu_dm_plane_fill_gfx12_attrs_from_modifiers() */
	KUNIT_CASE(dm_test_fill_gfx12_plane_attributes_dcc_blocks),
	KUNIT_CASE(dm_test_fill_gfx12_plane_attributes_validate_fails),
	/* amdgpu_dm_plane_fill_gfx9_attrs_from_modifiers() */
	KUNIT_CASE(dm_test_fill_gfx9_plane_attributes_from_modifiers),
	KUNIT_CASE(dm_test_fill_gfx9_plane_attributes_dcc),
	KUNIT_CASE(dm_test_fill_gfx9_plane_attributes_validate_fails),
	KUNIT_CASE(dm_test_fill_gfx9_plane_attributes_dcc_ind_blk),
	/* amdgpu_dm_plane_helper_check_state() */
	KUNIT_CASE(dm_test_helper_check_state_viewport_rejects),
	KUNIT_CASE(dm_test_helper_check_state_scaling_caps),
	/* amdgpu_dm_plane_helper_prepare_fb() */
	KUNIT_CASE(dm_test_helper_prepare_fb_no_fb),
	/* amdgpu_dm_plane_helper_cleanup_fb() */
	KUNIT_CASE(dm_test_helper_cleanup_fb_no_fb),
	/* amdgpu_dm_plane_handle_cursor_update() */
	KUNIT_CASE(dm_test_handle_cursor_update_no_fb),
	KUNIT_CASE(dm_test_handle_cursor_update_disabled),
	KUNIT_CASE(dm_test_handle_cursor_update_no_stream),
	/* amdgpu_dm_plane_atomic_async_update() */
	KUNIT_CASE(dm_test_atomic_async_update_copies_state),
	/* amdgpu_dm_plane_atomic_async_check() */
	KUNIT_CASE(dm_test_atomic_async_check_rejects),
	KUNIT_CASE(dm_test_atomic_async_check_overlay_cursor),
	/* amdgpu_dm_plane_atomic_check() */
	KUNIT_CASE(dm_test_atomic_check_no_dc_state),
	KUNIT_CASE(dm_test_atomic_check_missing_crtc_state),
	KUNIT_CASE(dm_test_atomic_check_rejects),
	KUNIT_CASE(dm_test_atomic_check_success),
	/* amdgpu_dm_plane_panic_flush() */
	KUNIT_CASE(dm_test_panic_flush_no_dc_state),
	KUNIT_CASE(dm_test_panic_flush_disables_dcc),
	/* amdgpu_dm_plane_drm_plane_create_state() */
	KUNIT_CASE(dm_test_plane_create_state_initializes_state),
	/* amdgpu_dm_plane_drm_plane_duplicate_state() */
	KUNIT_CASE(dm_test_plane_duplicate_state_copies_fields),
	KUNIT_CASE(dm_test_plane_duplicate_state_copies_resources),
	/* amdgpu_dm_plane_drm_plane_destroy_state() */
	KUNIT_CASE(dm_test_plane_destroy_state_minimal),
	KUNIT_CASE(dm_test_plane_destroy_state_releases_resources),
	/* amdgpu_dm_plane_add_modifier() */
	KUNIT_CASE(dm_test_add_modifier_appends_and_grows),
	KUNIT_CASE(dm_test_add_modifier_failure_paths),
	/* amdgpu_dm_plane_add_modifier_dedup() */
	KUNIT_CASE(dm_test_add_modifier_dedup),
	/* amdgpu_dm_plane_fill_gfx6_tiling_info_from_modifier() */
	KUNIT_CASE(dm_test_fill_gfx6_tiling_info_from_modifier),
	/* amdgpu_dm_plane_get_gfx6_tile_idx() */
	KUNIT_CASE(dm_test_get_gfx6_tile_idx),
	/* amdgpu_dm_plane_calc_gfx7_tile_split() */
	KUNIT_CASE(dm_test_calc_gfx7_tile_split),
	/* amdgpu_dm_plane_get_gfx7_macro_tile_idx() */
	KUNIT_CASE(dm_test_get_gfx7_macro_tile_idx),
	/* amdgpu_dm_plane_calc_gfx6_mod() */
	KUNIT_CASE(dm_test_calc_gfx6_mod),
	/* amdgpu_dm_plane_gfx6_format_mod_supported() */
	KUNIT_CASE(dm_test_gfx6_format_mod_supported),
	/* amdgpu_dm_plane_fill_gfx9_tiling_info_from_device() */
	KUNIT_CASE(dm_test_fill_gfx9_tiling_info_from_device),
	/* amdgpu_dm_plane_fill_gfx9_tiling_info_from_modifier() */
	KUNIT_CASE(dm_test_fill_gfx9_tiling_info_from_modifier),
	/* amdgpu_dm_plane_validate_dcc() */
	KUNIT_CASE(dm_test_validate_dcc_early_exits),
	KUNIT_CASE(dm_test_validate_dcc_rejects_from_cap),
	KUNIT_CASE(dm_test_validate_dcc_success_and_scan_mapping),
	/* amdgpu_dm_plane_init() */
	KUNIT_CASE(dm_test_plane_init_creates_optional_properties),
	KUNIT_CASE(dm_test_plane_init_creates_color_pipeline),
	{}
};

static struct kunit_suite amdgpu_dm_plane_test_suite = {
	.name = "amdgpu_dm_plane",
	.test_cases = amdgpu_dm_plane_test_cases,
};

kunit_test_suite(amdgpu_dm_plane_test_suite);

MODULE_DESCRIPTION("KUnit tests for amdgpu_dm_plane");
MODULE_LICENSE("Dual MIT/GPL");
