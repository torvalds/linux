// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * KUnit tests for amdgpu_dm_connector.c
 *
 * Copyright 2026 Advanced Micro Devices, Inc.
 */

#include <kunit/test.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_edid.h>
#include <drm/drm_encoder.h>
#include <drm/drm_kunit_helpers.h>
#include <drm/drm_managed.h>
#include <drm/drm_mode_object.h>
#include <drm/drm_modes.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_property.h>
#include <linux/hdmi.h>
#include <linux/i2c.h>

#include "dc.h"
#include "amdgpu.h"
#include "amdgpu_mode.h"
#include "amdgpu_display.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_connector.h"
#include "amdgpu_dm_backlight.h"
#include "amdgpu_dm_hdcp.h"
#include "amdgpu_dm_psr.h"
#include "include/grph_object_id.h"
#include "amdgpu_dm_kunit_test_helpers.h"
#include "inc/link_service.h"
#include "inc/resource.h"
#include "inc/hw/timing_generator.h"

/* Tests for get_subconnector_type() */

/**
 * dm_test_subconnector_type_none - Test Subconnector type none
 * @test: The KUnit test context
 */
static void dm_test_subconnector_type_none(struct kunit *test)
{
	struct dc_link *link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link);

	link->dpcd_caps.dongle_type = DISPLAY_DONGLE_NONE;
	KUNIT_EXPECT_EQ(test, (int)get_subconnector_type(link), (int)DRM_MODE_SUBCONNECTOR_Native);
}

/**
 * dm_test_subconnector_type_vga - Test Subconnector type vga
 * @test: The KUnit test context
 */
static void dm_test_subconnector_type_vga(struct kunit *test)
{
	struct dc_link *link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link);

	link->dpcd_caps.dongle_type = DISPLAY_DONGLE_DP_VGA_CONVERTER;
	KUNIT_EXPECT_EQ(test, (int)get_subconnector_type(link), (int)DRM_MODE_SUBCONNECTOR_VGA);
}

/**
 * dm_test_subconnector_type_dvi_converter - Test Subconnector type dvi converter
 * @test: The KUnit test context
 */
static void dm_test_subconnector_type_dvi_converter(struct kunit *test)
{
	struct dc_link *link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link);

	link->dpcd_caps.dongle_type = DISPLAY_DONGLE_DP_DVI_CONVERTER;
	KUNIT_EXPECT_EQ(test, (int)get_subconnector_type(link), (int)DRM_MODE_SUBCONNECTOR_DVID);
}

/**
 * dm_test_subconnector_type_dvi_dongle - Test Subconnector type dvi dongle
 * @test: The KUnit test context
 */
static void dm_test_subconnector_type_dvi_dongle(struct kunit *test)
{
	struct dc_link *link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link);

	link->dpcd_caps.dongle_type = DISPLAY_DONGLE_DP_DVI_DONGLE;
	KUNIT_EXPECT_EQ(test, (int)get_subconnector_type(link), (int)DRM_MODE_SUBCONNECTOR_DVID);
}

/**
 * dm_test_subconnector_type_hdmi_converter - Test Subconnector type hdmi converter
 * @test: The KUnit test context
 */
static void dm_test_subconnector_type_hdmi_converter(struct kunit *test)
{
	struct dc_link *link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link);

	link->dpcd_caps.dongle_type = DISPLAY_DONGLE_DP_HDMI_CONVERTER;
	KUNIT_EXPECT_EQ(test, (int)get_subconnector_type(link), (int)DRM_MODE_SUBCONNECTOR_HDMIA);
}

/**
 * dm_test_subconnector_type_hdmi_dongle - Test Subconnector type hdmi dongle
 * @test: The KUnit test context
 */
static void dm_test_subconnector_type_hdmi_dongle(struct kunit *test)
{
	struct dc_link *link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link);

	link->dpcd_caps.dongle_type = DISPLAY_DONGLE_DP_HDMI_DONGLE;
	KUNIT_EXPECT_EQ(test, (int)get_subconnector_type(link), (int)DRM_MODE_SUBCONNECTOR_HDMIA);
}

/**
 * dm_test_subconnector_type_mismatched - Test Subconnector type mismatched
 * @test: The KUnit test context
 */
static void dm_test_subconnector_type_mismatched(struct kunit *test)
{
	struct dc_link *link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link);

	link->dpcd_caps.dongle_type = DISPLAY_DONGLE_DP_HDMI_MISMATCHED_DONGLE;
	KUNIT_EXPECT_EQ(test, (int)get_subconnector_type(link), (int)DRM_MODE_SUBCONNECTOR_Unknown);
}

/**
 * dm_test_subconnector_type_default_unknown - Test Subconnector type default unknown
 * @test: The KUnit test context
 */
static void dm_test_subconnector_type_default_unknown(struct kunit *test)
{
	struct dc_link *link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link);

	link->dpcd_caps.dongle_type = (typeof(link->dpcd_caps.dongle_type))0x7f;
	KUNIT_EXPECT_EQ(test, (int)get_subconnector_type(link), (int)DRM_MODE_SUBCONNECTOR_Unknown);
}

/* Tests for get_output_content_type() */

/**
 * dm_test_content_type_no_data - Test Content type no data
 * @test: The KUnit test context
 */
static void dm_test_content_type_no_data(struct kunit *test)
{
	struct drm_connector_state state = {};

	state.content_type = DRM_MODE_CONTENT_TYPE_NO_DATA;
	KUNIT_EXPECT_EQ(test, (int)get_output_content_type(&state), (int)DISPLAY_CONTENT_TYPE_NO_DATA);
}

/**
 * dm_test_content_type_graphics - Test Content type graphics
 * @test: The KUnit test context
 */
static void dm_test_content_type_graphics(struct kunit *test)
{
	struct drm_connector_state state = {};

	state.content_type = DRM_MODE_CONTENT_TYPE_GRAPHICS;
	KUNIT_EXPECT_EQ(test, (int)get_output_content_type(&state), (int)DISPLAY_CONTENT_TYPE_GRAPHICS);
}

/**
 * dm_test_content_type_photo - Test Content type photo
 * @test: The KUnit test context
 */
static void dm_test_content_type_photo(struct kunit *test)
{
	struct drm_connector_state state = {};

	state.content_type = DRM_MODE_CONTENT_TYPE_PHOTO;
	KUNIT_EXPECT_EQ(test, (int)get_output_content_type(&state), (int)DISPLAY_CONTENT_TYPE_PHOTO);
}

/**
 * dm_test_content_type_cinema - Test Content type cinema
 * @test: The KUnit test context
 */
static void dm_test_content_type_cinema(struct kunit *test)
{
	struct drm_connector_state state = {};

	state.content_type = DRM_MODE_CONTENT_TYPE_CINEMA;
	KUNIT_EXPECT_EQ(test, (int)get_output_content_type(&state), (int)DISPLAY_CONTENT_TYPE_CINEMA);
}

/**
 * dm_test_content_type_game - Test Content type game
 * @test: The KUnit test context
 */
static void dm_test_content_type_game(struct kunit *test)
{
	struct drm_connector_state state = {};

	state.content_type = DRM_MODE_CONTENT_TYPE_GAME;
	KUNIT_EXPECT_EQ(test, (int)get_output_content_type(&state), (int)DISPLAY_CONTENT_TYPE_GAME);
}

/**
 * dm_test_content_type_unknown_defaults_no_data - Test unknown content type defaults to no data
 * @test: The KUnit test context
 */
static void dm_test_content_type_unknown_defaults_no_data(struct kunit *test)
{
	struct drm_connector_state state = {};

	state.content_type = 0x7f;
	KUNIT_EXPECT_EQ(test, (int)get_output_content_type(&state),
			(int)DISPLAY_CONTENT_TYPE_NO_DATA);
}

/* Tests for adjust_colour_depth_from_display_info() */

/**
 * dm_test_adjust_colour_depth_fits_at_888 - Test Adjust colour depth fits at 888
 * @test: The KUnit test context
 */
static void dm_test_adjust_colour_depth_fits_at_888(struct kunit *test)
{
	struct dc_crtc_timing timing = {};
	struct drm_display_info info = {};

	/* 1080p @ 148500 KHz = 1485000 in 100Hz units */
	timing.pix_clk_100hz = 1485000;
	timing.display_color_depth = COLOR_DEPTH_888;
	timing.pixel_encoding = PIXEL_ENCODING_RGB;
	info.max_tmds_clock = 150000; /* 150 MHz */

	KUNIT_EXPECT_TRUE(test, adjust_colour_depth_from_display_info(&timing, &info));
	KUNIT_EXPECT_EQ(test, (int)timing.display_color_depth, (int)COLOR_DEPTH_888);
}

/**
 * dm_test_adjust_colour_depth_reduces_to_888 - Test Adjust colour depth reduces to 888
 * @test: The KUnit test context
 */
static void dm_test_adjust_colour_depth_reduces_to_888(struct kunit *test)
{
	struct dc_crtc_timing timing = {};
	struct drm_display_info info = {};

	/* Request 10bpc but TMDS limit only allows 8bpc */
	timing.pix_clk_100hz = 1485000;
	timing.display_color_depth = COLOR_DEPTH_101010;
	timing.pixel_encoding = PIXEL_ENCODING_RGB;
	/* 10bpc would need 148500*30/24 = 185625 KHz, exceeds limit */
	info.max_tmds_clock = 160000;

	KUNIT_EXPECT_TRUE(test, adjust_colour_depth_from_display_info(&timing, &info));
	KUNIT_EXPECT_EQ(test, (int)timing.display_color_depth, (int)COLOR_DEPTH_888);
}

/**
 * dm_test_adjust_colour_depth_10bpc_passes - Test Adjust colour depth 10bpc passes
 * @test: The KUnit test context
 */
static void dm_test_adjust_colour_depth_10bpc_passes(struct kunit *test)
{
	struct dc_crtc_timing timing = {};
	struct drm_display_info info = {};

	timing.pix_clk_100hz = 1485000;
	timing.display_color_depth = COLOR_DEPTH_101010;
	timing.pixel_encoding = PIXEL_ENCODING_RGB;
	/* 10bpc needs 185625 KHz, allow it */
	info.max_tmds_clock = 200000;

	KUNIT_EXPECT_TRUE(test, adjust_colour_depth_from_display_info(&timing, &info));
	KUNIT_EXPECT_EQ(test, (int)timing.display_color_depth, (int)COLOR_DEPTH_101010);
}

/**
 * dm_test_adjust_colour_depth_420_halves_clk - Test Adjust colour depth 420 halves clk
 * @test: The KUnit test context
 */
static void dm_test_adjust_colour_depth_420_halves_clk(struct kunit *test)
{
	struct dc_crtc_timing timing = {};
	struct drm_display_info info = {};

	/* 4K @ 594000 KHz = 5940000 in 100Hz units */
	timing.pix_clk_100hz = 5940000;
	timing.display_color_depth = COLOR_DEPTH_101010;
	timing.pixel_encoding = PIXEL_ENCODING_YCBCR420;
	/* With 420: effective = 594000/2 = 297000, 10bpc = 297000*30/24 = 371250 */
	info.max_tmds_clock = 400000;

	KUNIT_EXPECT_TRUE(test, adjust_colour_depth_from_display_info(&timing, &info));
	KUNIT_EXPECT_EQ(test, (int)timing.display_color_depth, (int)COLOR_DEPTH_101010);
}

/**
 * dm_test_adjust_colour_depth_420_reduces - Test Adjust colour depth 420 reduces
 * @test: The KUnit test context
 */
static void dm_test_adjust_colour_depth_420_reduces(struct kunit *test)
{
	struct dc_crtc_timing timing = {};
	struct drm_display_info info = {};

	/* 4K @ 594000 KHz = 5940000 in 100Hz units */
	timing.pix_clk_100hz = 5940000;
	timing.display_color_depth = COLOR_DEPTH_121212;
	timing.pixel_encoding = PIXEL_ENCODING_YCBCR420;
	/*
	 * With 420: effective = 594000/2 = 297000.
	 * 12bpc = 297000*36/24 = 445500 (exceeds limit),
	 * 10bpc = 297000*30/24 = 371250 (fits).
	 */
	info.max_tmds_clock = 400000;

	KUNIT_EXPECT_TRUE(test, adjust_colour_depth_from_display_info(&timing, &info));
	KUNIT_EXPECT_EQ(test, (int)timing.display_color_depth, (int)COLOR_DEPTH_101010);
}

/**
 * dm_test_adjust_colour_depth_reduces_12bpc_to_10bpc - Test Adjust colour
 * depth reduces 12bpc to 10bpc
 * @test: The KUnit test context
 */
static void dm_test_adjust_colour_depth_reduces_12bpc_to_10bpc(struct kunit *test)
{
	struct dc_crtc_timing timing = {};
	struct drm_display_info info = {};

	timing.pix_clk_100hz = 1485000;
	timing.display_color_depth = COLOR_DEPTH_121212;
	timing.pixel_encoding = PIXEL_ENCODING_RGB;
	info.max_tmds_clock = 190000;

	KUNIT_EXPECT_TRUE(test, adjust_colour_depth_from_display_info(&timing, &info));
	KUNIT_EXPECT_EQ(test, (int)timing.display_color_depth, (int)COLOR_DEPTH_101010);
}

/**
 * dm_test_adjust_colour_depth_16bpc_no_fallback - Test Adjust colour depth
 * 16bpc cannot fall back
 * @test: The KUnit test context
 */
static void dm_test_adjust_colour_depth_16bpc_no_fallback(struct kunit *test)
{
	struct dc_crtc_timing timing = {};
	struct drm_display_info info = {};

	/* 16bpc that exceeds limit cannot reduce because the next enum
	 * value (COLOR_DEPTH_141414) is not a valid HDMI depth.
	 */
	timing.pix_clk_100hz = 1485000;
	timing.display_color_depth = COLOR_DEPTH_161616;
	timing.pixel_encoding = PIXEL_ENCODING_RGB;
	info.max_tmds_clock = 230000;

	KUNIT_EXPECT_FALSE(test, adjust_colour_depth_from_display_info(&timing, &info));
}

/**
 * dm_test_adjust_colour_depth_none_fits - Test Adjust colour depth none fits
 * @test: The KUnit test context
 */
static void dm_test_adjust_colour_depth_none_fits(struct kunit *test)
{
	struct dc_crtc_timing timing = {};
	struct drm_display_info info = {};

	/* Even 8bpc doesn't fit */
	timing.pix_clk_100hz = 1485000;
	timing.display_color_depth = COLOR_DEPTH_888;
	timing.pixel_encoding = PIXEL_ENCODING_RGB;
	info.max_tmds_clock = 100000; /* Too low */

	KUNIT_EXPECT_FALSE(test, adjust_colour_depth_from_display_info(&timing, &info));
}

/**
 * dm_test_adjust_colour_depth_invalid_depth - Test Adjust colour depth invalid depth
 * @test: The KUnit test context
 */
static void dm_test_adjust_colour_depth_invalid_depth(struct kunit *test)
{
	struct dc_crtc_timing timing = {};
	struct drm_display_info info = {};

	timing.pix_clk_100hz = 1485000;
	timing.display_color_depth = COLOR_DEPTH_141414;
	timing.pixel_encoding = PIXEL_ENCODING_RGB;
	info.max_tmds_clock = 400000;

	KUNIT_EXPECT_FALSE(test, adjust_colour_depth_from_display_info(&timing, &info));
	KUNIT_EXPECT_EQ(test, (int)timing.display_color_depth, (int)COLOR_DEPTH_141414);
}

/* Tests for amdgpu_dm_get_output_color_space() */

/**
 * dm_test_output_color_space_default_rgb_full - Test Output color space default rgb full
 * @test: The KUnit test context
 */
static void dm_test_output_color_space_default_rgb_full(struct kunit *test)
{
	struct dc_crtc_timing timing = {};
	struct drm_connector_state state = {};
	struct drm_connector *connector = kunit_kzalloc(test, sizeof(*connector), GFP_KERNEL);

	state.connector = connector;
	timing.pixel_encoding = PIXEL_ENCODING_RGB;
	state.colorspace = DRM_MODE_COLORIMETRY_DEFAULT;
	state.hdmi.broadcast_rgb = DRM_HDMI_BROADCAST_RGB_AUTO;

	KUNIT_EXPECT_EQ(test, (int)amdgpu_dm_get_output_color_space(&timing, &state),
			(int)COLOR_SPACE_SRGB);
}

/**
 * dm_test_output_color_space_default_rgb_limited - Test Output color space default rgb limited
 * @test: The KUnit test context
 */
static void dm_test_output_color_space_default_rgb_limited(struct kunit *test)
{
	struct dc_crtc_timing timing = {};
	struct drm_connector_state state = {};

	timing.pixel_encoding = PIXEL_ENCODING_RGB;
	state.colorspace = DRM_MODE_COLORIMETRY_DEFAULT;
	state.hdmi.broadcast_rgb = DRM_HDMI_BROADCAST_RGB_LIMITED;

	KUNIT_EXPECT_EQ(test, (int)amdgpu_dm_get_output_color_space(&timing, &state),
			(int)COLOR_SPACE_SRGB_LIMITED);
}

/**
 * dm_test_output_color_space_default_ycbcr709 - Test Output color space default ycbcr709
 * @test: The KUnit test context
 */
static void dm_test_output_color_space_default_ycbcr709(struct kunit *test)
{
	struct dc_crtc_timing timing = {};
	struct drm_connector_state state = {};

	timing.pixel_encoding = PIXEL_ENCODING_YCBCR444;
	timing.pix_clk_100hz = 300000;
	timing.flags.Y_ONLY = 0;
	state.colorspace = DRM_MODE_COLORIMETRY_DEFAULT;

	KUNIT_EXPECT_EQ(test, (int)amdgpu_dm_get_output_color_space(&timing, &state),
			(int)COLOR_SPACE_YCBCR709);
}

/**
 * dm_test_output_color_space_default_ycbcr601_limited - Test Output color space
 * default ycbcr601 limited
 * @test: The KUnit test context
 */
static void dm_test_output_color_space_default_ycbcr601_limited(struct kunit *test)
{
	struct dc_crtc_timing timing = {};
	struct drm_connector_state state = {};

	timing.pixel_encoding = PIXEL_ENCODING_YCBCR444;
	timing.pix_clk_100hz = 270300;
	timing.flags.Y_ONLY = 1;
	state.colorspace = DRM_MODE_COLORIMETRY_DEFAULT;

	KUNIT_EXPECT_EQ(test, (int)amdgpu_dm_get_output_color_space(&timing, &state),
			(int)COLOR_SPACE_YCBCR601_LIMITED);
}

/**
 * dm_test_output_color_space_bt601_y_only - Test Output color space bt601 y only
 * @test: The KUnit test context
 */
static void dm_test_output_color_space_bt601_y_only(struct kunit *test)
{
	struct dc_crtc_timing timing = {};
	struct drm_connector_state state = {};

	timing.flags.Y_ONLY = 1;
	state.colorspace = DRM_MODE_COLORIMETRY_BT601_YCC;

	KUNIT_EXPECT_EQ(test, (int)amdgpu_dm_get_output_color_space(&timing, &state),
			(int)COLOR_SPACE_YCBCR601_LIMITED);
}

/**
 * dm_test_output_color_space_bt601 - Test Output color space bt601
 * @test: The KUnit test context
 */
static void dm_test_output_color_space_bt601(struct kunit *test)
{
	struct dc_crtc_timing timing = {};
	struct drm_connector_state state = {};

	timing.flags.Y_ONLY = 0;
	state.colorspace = DRM_MODE_COLORIMETRY_BT601_YCC;

	KUNIT_EXPECT_EQ(test, (int)amdgpu_dm_get_output_color_space(&timing, &state),
			(int)COLOR_SPACE_YCBCR601);
}

/**
 * dm_test_output_color_space_bt709 - Test Output color space bt709
 * @test: The KUnit test context
 */
static void dm_test_output_color_space_bt709(struct kunit *test)
{
	struct dc_crtc_timing timing = {};
	struct drm_connector_state state = {};

	timing.flags.Y_ONLY = 0;
	state.colorspace = DRM_MODE_COLORIMETRY_BT709_YCC;

	KUNIT_EXPECT_EQ(test, (int)amdgpu_dm_get_output_color_space(&timing, &state),
			(int)COLOR_SPACE_YCBCR709);
}

/**
 * dm_test_output_color_space_bt709_y_only - Test Output color space bt709 y only
 * @test: The KUnit test context
 */
static void dm_test_output_color_space_bt709_y_only(struct kunit *test)
{
	struct dc_crtc_timing timing = {};
	struct drm_connector_state state = {};

	timing.flags.Y_ONLY = 1;
	state.colorspace = DRM_MODE_COLORIMETRY_BT709_YCC;

	KUNIT_EXPECT_EQ(test, (int)amdgpu_dm_get_output_color_space(&timing, &state),
			(int)COLOR_SPACE_YCBCR709_LIMITED);
}

/**
 * dm_test_output_color_space_oprgb - Test Output color space oprgb
 * @test: The KUnit test context
 */
static void dm_test_output_color_space_oprgb(struct kunit *test)
{
	struct dc_crtc_timing timing = {};
	struct drm_connector_state state = {};

	state.colorspace = DRM_MODE_COLORIMETRY_OPRGB;

	KUNIT_EXPECT_EQ(test, (int)amdgpu_dm_get_output_color_space(&timing, &state),
			(int)COLOR_SPACE_ADOBERGB);
}

/**
 * dm_test_output_color_space_bt2020_rgb - Test Output color space bt2020 rgb
 * @test: The KUnit test context
 */
static void dm_test_output_color_space_bt2020_rgb(struct kunit *test)
{
	struct dc_crtc_timing timing = {};
	struct drm_connector_state state = {};
	struct drm_connector *connector = kunit_kzalloc(test, sizeof(*connector), GFP_KERNEL);

	state.connector = connector;
	timing.pixel_encoding = PIXEL_ENCODING_RGB;
	state.colorspace = DRM_MODE_COLORIMETRY_BT2020_RGB;

	KUNIT_EXPECT_EQ(test, (int)amdgpu_dm_get_output_color_space(&timing, &state),
			(int)COLOR_SPACE_2020_RGB_FULLRANGE);
}

/**
 * dm_test_output_color_space_bt2020_rgb_limited - Test limited BT.2020 RGB
 * @test: The KUnit test context
 */
static void dm_test_output_color_space_bt2020_rgb_limited(struct kunit *test)
{
	struct dc_crtc_timing timing = {};
	struct drm_connector_state state = {};

	timing.pixel_encoding = PIXEL_ENCODING_RGB;
	state.colorspace = DRM_MODE_COLORIMETRY_BT2020_RGB;
	state.hdmi.broadcast_rgb = DRM_HDMI_BROADCAST_RGB_LIMITED;

	KUNIT_EXPECT_EQ(test, (int)amdgpu_dm_get_output_color_space(&timing, &state),
			(int)COLOR_SPACE_2020_RGB_LIMITEDRANGE);
}

/**
 * dm_test_output_color_space_default_rgb_auto_hdmi_cta - Automatic follows the CTA-861 default
 * @test: The KUnit test context
 */
static void dm_test_output_color_space_default_rgb_auto_hdmi_cta(struct kunit *test)
{
	struct dc_crtc_timing timing = {};
	struct drm_connector_state state = {};
	struct drm_connector *connector = kunit_kzalloc(test, sizeof(*connector), GFP_KERNEL);

	connector->display_info.is_hdmi = true;
	timing.pixel_encoding = PIXEL_ENCODING_RGB;
	timing.vic = 32;
	state.connector = connector;
	state.colorspace = DRM_MODE_COLORIMETRY_DEFAULT;
	state.hdmi.broadcast_rgb = DRM_HDMI_BROADCAST_RGB_AUTO;

	KUNIT_EXPECT_EQ(test, (int)amdgpu_dm_get_output_color_space(&timing, &state),
			(int)COLOR_SPACE_SRGB_LIMITED);
}

/**
 * dm_test_output_color_space_default_rgb_auto_hdmi_vga - 640x480 (VIC 1) stays full range
 * @test: The KUnit test context
 */
static void dm_test_output_color_space_default_rgb_auto_hdmi_vga(struct kunit *test)
{
	struct dc_crtc_timing timing = {};
	struct drm_connector_state state = {};
	struct drm_connector *connector = kunit_kzalloc(test, sizeof(*connector), GFP_KERNEL);

	connector->display_info.is_hdmi = true;
	timing.pixel_encoding = PIXEL_ENCODING_RGB;
	timing.vic = 1;
	state.connector = connector;
	state.colorspace = DRM_MODE_COLORIMETRY_DEFAULT;
	state.hdmi.broadcast_rgb = DRM_HDMI_BROADCAST_RGB_AUTO;

	KUNIT_EXPECT_EQ(test, (int)amdgpu_dm_get_output_color_space(&timing, &state),
			(int)COLOR_SPACE_SRGB);
}

/**
 * dm_test_output_color_space_default_rgb_auto_dp_cta - Automatic on a non-HDMI sink is full range
 * @test: The KUnit test context
 */
static void dm_test_output_color_space_default_rgb_auto_dp_cta(struct kunit *test)
{
	struct dc_crtc_timing timing = {};
	struct drm_connector_state state = {};
	struct drm_connector *connector = kunit_kzalloc(test, sizeof(*connector), GFP_KERNEL);

	timing.pixel_encoding = PIXEL_ENCODING_RGB;
	timing.vic = 32;
	state.connector = connector;
	state.colorspace = DRM_MODE_COLORIMETRY_DEFAULT;
	state.hdmi.broadcast_rgb = DRM_HDMI_BROADCAST_RGB_AUTO;

	KUNIT_EXPECT_EQ(test, (int)amdgpu_dm_get_output_color_space(&timing, &state),
			(int)COLOR_SPACE_SRGB);
}

/**
 * dm_test_output_color_space_default_rgb_full_hdmi_cta - Full overrides the CTA-861 default
 * @test: The KUnit test context
 */
static void dm_test_output_color_space_default_rgb_full_hdmi_cta(struct kunit *test)
{
	struct dc_crtc_timing timing = {};
	struct drm_connector_state state = {};
	struct drm_connector *connector = kunit_kzalloc(test, sizeof(*connector), GFP_KERNEL);

	connector->display_info.is_hdmi = true;
	timing.pixel_encoding = PIXEL_ENCODING_RGB;
	timing.vic = 32;
	state.connector = connector;
	state.colorspace = DRM_MODE_COLORIMETRY_DEFAULT;
	state.hdmi.broadcast_rgb = DRM_HDMI_BROADCAST_RGB_FULL;

	KUNIT_EXPECT_EQ(test, (int)amdgpu_dm_get_output_color_space(&timing, &state),
			(int)COLOR_SPACE_SRGB);
}

/**
 * dm_test_output_color_space_bt2020_rgb_auto_hdmi_cta - BT.2020 RGB follows the same default
 * @test: The KUnit test context
 */
static void dm_test_output_color_space_bt2020_rgb_auto_hdmi_cta(struct kunit *test)
{
	struct dc_crtc_timing timing = {};
	struct drm_connector_state state = {};
	struct drm_connector *connector = kunit_kzalloc(test, sizeof(*connector), GFP_KERNEL);

	connector->display_info.is_hdmi = true;
	timing.pixel_encoding = PIXEL_ENCODING_RGB;
	timing.vic = 32;
	state.connector = connector;
	state.colorspace = DRM_MODE_COLORIMETRY_BT2020_RGB;
	state.hdmi.broadcast_rgb = DRM_HDMI_BROADCAST_RGB_AUTO;

	KUNIT_EXPECT_EQ(test, (int)amdgpu_dm_get_output_color_space(&timing, &state),
			(int)COLOR_SPACE_2020_RGB_LIMITEDRANGE);
}

/**
 * dm_test_output_color_space_bt2020_ycc - Test Output color space bt2020 ycc
 * @test: The KUnit test context
 */
static void dm_test_output_color_space_bt2020_ycc(struct kunit *test)
{
	struct dc_crtc_timing timing = {};
	struct drm_connector_state state = {};

	timing.pixel_encoding = PIXEL_ENCODING_YCBCR422;
	state.colorspace = DRM_MODE_COLORIMETRY_BT2020_YCC;

	KUNIT_EXPECT_EQ(test, (int)amdgpu_dm_get_output_color_space(&timing, &state),
			(int)COLOR_SPACE_2020_YCBCR_LIMITED);
}

/**
 * dm_test_output_color_space_default_ycbcr709_y_only - Test Output color space
 * default ycbcr709 limited via Y_ONLY at high pixel clock
 * @test: The KUnit test context
 */
static void dm_test_output_color_space_default_ycbcr709_y_only(struct kunit *test)
{
	struct dc_crtc_timing timing = {};
	struct drm_connector_state state = {};

	timing.pixel_encoding = PIXEL_ENCODING_YCBCR444;
	timing.pix_clk_100hz = 300000;
	timing.flags.Y_ONLY = 1;
	state.colorspace = DRM_MODE_COLORIMETRY_DEFAULT;

	KUNIT_EXPECT_EQ(test, (int)amdgpu_dm_get_output_color_space(&timing, &state),
			(int)COLOR_SPACE_YCBCR709_LIMITED);
}

/**
 * dm_test_output_color_space_default_ycbcr601 - Test Output color space default
 * ycbcr601 full range at low pixel clock
 * @test: The KUnit test context
 */
static void dm_test_output_color_space_default_ycbcr601(struct kunit *test)
{
	struct dc_crtc_timing timing = {};
	struct drm_connector_state state = {};

	timing.pixel_encoding = PIXEL_ENCODING_YCBCR444;
	timing.pix_clk_100hz = 270300;
	timing.flags.Y_ONLY = 0;
	state.colorspace = DRM_MODE_COLORIMETRY_DEFAULT;

	KUNIT_EXPECT_EQ(test, (int)amdgpu_dm_get_output_color_space(&timing, &state),
			(int)COLOR_SPACE_YCBCR601);
}

/**
 * dm_test_output_color_space_bt2020_ycc_rgb_encoding - Test Output color space
 * bt2020 ycc with rgb pixel encoding falls back to full range rgb
 * @test: The KUnit test context
 */
static void dm_test_output_color_space_bt2020_ycc_rgb_encoding(struct kunit *test)
{
	struct dc_crtc_timing timing = {};
	struct drm_connector_state state = {};
	struct drm_connector *connector = kunit_kzalloc(test, sizeof(*connector), GFP_KERNEL);

	state.connector = connector;
	timing.pixel_encoding = PIXEL_ENCODING_RGB;
	state.colorspace = DRM_MODE_COLORIMETRY_BT2020_YCC;

	KUNIT_EXPECT_EQ(test, (int)amdgpu_dm_get_output_color_space(&timing, &state),
			(int)COLOR_SPACE_2020_RGB_FULLRANGE);
}

/**
 * dm_test_output_color_space_bt2020_ycc_rgb_encoding_limited - Test limited
 * BT.2020 RGB output selected through the BT.2020 YCC connector colorspace
 * @test: The KUnit test context
 */
static void dm_test_output_color_space_bt2020_ycc_rgb_encoding_limited(struct kunit *test)
{
	struct dc_crtc_timing timing = {};
	struct drm_connector_state state = {};

	timing.pixel_encoding = PIXEL_ENCODING_RGB;
	state.colorspace = DRM_MODE_COLORIMETRY_BT2020_YCC;
	state.hdmi.broadcast_rgb = DRM_HDMI_BROADCAST_RGB_LIMITED;

	KUNIT_EXPECT_EQ(test, (int)amdgpu_dm_get_output_color_space(&timing, &state),
			(int)COLOR_SPACE_2020_RGB_LIMITEDRANGE);
}

/**
 * dm_test_output_color_space_bt2020_rgb_ycc_encoding - Test Output color space
 * bt2020 rgb with non-rgb pixel encoding falls back to limited ycbcr
 * @test: The KUnit test context
 */
static void dm_test_output_color_space_bt2020_rgb_ycc_encoding(struct kunit *test)
{
	struct dc_crtc_timing timing = {};
	struct drm_connector_state state = {};

	timing.pixel_encoding = PIXEL_ENCODING_YCBCR444;
	state.colorspace = DRM_MODE_COLORIMETRY_BT2020_RGB;

	KUNIT_EXPECT_EQ(test, (int)amdgpu_dm_get_output_color_space(&timing, &state),
			(int)COLOR_SPACE_2020_YCBCR_LIMITED);
}

/* Tests for amdgpu_dm_convert_dc_color_depth_into_bpc() */

/**
 * dm_test_convert_color_depth_bpc_mappings - Test Convert color depth bpc mappings
 * @test: The KUnit test context
 */
static void dm_test_convert_color_depth_bpc_mappings(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, amdgpu_dm_convert_dc_color_depth_into_bpc(COLOR_DEPTH_666), 6);
	KUNIT_EXPECT_EQ(test, amdgpu_dm_convert_dc_color_depth_into_bpc(COLOR_DEPTH_888), 8);
	KUNIT_EXPECT_EQ(test, amdgpu_dm_convert_dc_color_depth_into_bpc(COLOR_DEPTH_101010), 10);
	KUNIT_EXPECT_EQ(test, amdgpu_dm_convert_dc_color_depth_into_bpc(COLOR_DEPTH_121212), 12);
	KUNIT_EXPECT_EQ(test, amdgpu_dm_convert_dc_color_depth_into_bpc(COLOR_DEPTH_141414), 14);
	KUNIT_EXPECT_EQ(test, amdgpu_dm_convert_dc_color_depth_into_bpc(COLOR_DEPTH_161616), 16);
}

/**
 * dm_test_convert_color_depth_bpc_unknown - Test Convert color depth bpc unknown
 * @test: The KUnit test context
 */
static void dm_test_convert_color_depth_bpc_unknown(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, amdgpu_dm_convert_dc_color_depth_into_bpc(COLOR_DEPTH_UNDEFINED), 0);
}

/* Tests for amdgpu_dm_convert_color_depth_from_display_info() */

/**
 * dm_test_color_depth_from_info_bpc8 - Test Color depth from info bpc8
 * @test: The KUnit test context
 */
static void dm_test_color_depth_from_info_bpc8(struct kunit *test)
{
	struct drm_connector *connector;

	connector = kunit_kzalloc(test, sizeof(*connector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, connector);

	connector->display_info.bpc = 8;
	KUNIT_EXPECT_EQ(test, (int)amdgpu_dm_convert_color_depth_from_display_info(connector, false, 0),
			(int)COLOR_DEPTH_888);
}

/**
 * dm_test_color_depth_from_info_bpc10 - Test Color depth from info bpc10
 * @test: The KUnit test context
 */
static void dm_test_color_depth_from_info_bpc10(struct kunit *test)
{
	struct drm_connector *connector;

	connector = kunit_kzalloc(test, sizeof(*connector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, connector);

	connector->display_info.bpc = 10;
	KUNIT_EXPECT_EQ(test, (int)amdgpu_dm_convert_color_depth_from_display_info(connector, false, 0),
			(int)COLOR_DEPTH_101010);
}

/**
 * dm_test_color_depth_from_info_zero_bpc_defaults_888 - Test Color depth from
 * info zero bpc defaults 888
 * @test: The KUnit test context
 */
static void dm_test_color_depth_from_info_zero_bpc_defaults_888(struct kunit *test)
{
	struct drm_connector *connector;

	connector = kunit_kzalloc(test, sizeof(*connector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, connector);

	connector->display_info.bpc = 0;
	KUNIT_EXPECT_EQ(test, (int)amdgpu_dm_convert_color_depth_from_display_info(connector, false, 0),
			(int)COLOR_DEPTH_888);
}

/**
 * dm_test_color_depth_from_info_requested_bpc_caps - Test Color depth from info requested bpc caps
 * @test: The KUnit test context
 */
static void dm_test_color_depth_from_info_requested_bpc_caps(struct kunit *test)
{
	struct drm_connector *connector;

	connector = kunit_kzalloc(test, sizeof(*connector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, connector);

	/* Display supports 12bpc but user requests max 10 */
	connector->display_info.bpc = 12;
	KUNIT_EXPECT_EQ(test, (int)amdgpu_dm_convert_color_depth_from_display_info(connector, false, 10),
			(int)COLOR_DEPTH_101010);
}

/**
 * dm_test_color_depth_from_info_y420_default - Test Color depth from info y420 default
 * @test: The KUnit test context
 */
static void dm_test_color_depth_from_info_y420_default(struct kunit *test)
{
	struct drm_connector *connector;

	connector = kunit_kzalloc(test, sizeof(*connector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, connector);

	/* No Y420 DC modes set → 8bpc */
	KUNIT_EXPECT_EQ(test, (int)amdgpu_dm_convert_color_depth_from_display_info(connector, true, 0),
			(int)COLOR_DEPTH_888);
}

/**
 * dm_test_color_depth_from_info_y420_10bpc - Test Color depth from info y420 10bpc
 * @test: The KUnit test context
 */
static void dm_test_color_depth_from_info_y420_10bpc(struct kunit *test)
{
	struct drm_connector *connector;

	connector = kunit_kzalloc(test, sizeof(*connector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, connector);

	connector->display_info.hdmi.y420_dc_modes = DRM_EDID_YCBCR420_DC_30;
	KUNIT_EXPECT_EQ(test, (int)amdgpu_dm_convert_color_depth_from_display_info(connector, true, 0),
			(int)COLOR_DEPTH_101010);
}

/**
 * dm_test_color_depth_from_info_y420_12bpc - Test Color depth from info y420 12bpc
 * @test: The KUnit test context
 */
static void dm_test_color_depth_from_info_y420_12bpc(struct kunit *test)
{
	struct drm_connector *connector;

	connector = kunit_kzalloc(test, sizeof(*connector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, connector);

	connector->display_info.hdmi.y420_dc_modes = DRM_EDID_YCBCR420_DC_36;
	KUNIT_EXPECT_EQ(test, (int)amdgpu_dm_convert_color_depth_from_display_info(connector, true, 0),
			(int)COLOR_DEPTH_121212);
}

/**
 * dm_test_color_depth_from_info_y420_16bpc - Test Color depth from info y420 16bpc
 * @test: The KUnit test context
 */
static void dm_test_color_depth_from_info_y420_16bpc(struct kunit *test)
{
	struct drm_connector *connector;

	connector = kunit_kzalloc(test, sizeof(*connector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, connector);

	connector->display_info.hdmi.y420_dc_modes = DRM_EDID_YCBCR420_DC_48;
	KUNIT_EXPECT_EQ(test, (int)amdgpu_dm_convert_color_depth_from_display_info(connector, true, 0),
			(int)COLOR_DEPTH_161616);
}

/**
 * dm_test_color_depth_from_info_requested_odd_bpc - Test Color depth from info requested odd bpc
 * @test: The KUnit test context
 */
static void dm_test_color_depth_from_info_requested_odd_bpc(struct kunit *test)
{
	struct drm_connector *connector;

	connector = kunit_kzalloc(test, sizeof(*connector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, connector);

	connector->display_info.bpc = 12;
	KUNIT_EXPECT_EQ(test, (int)amdgpu_dm_convert_color_depth_from_display_info(connector, false, 11),
			(int)COLOR_DEPTH_101010);
}

/**
 * dm_test_color_depth_from_info_unsupported_bpc - Test Color depth from info unsupported bpc
 * @test: The KUnit test context
 */
static void dm_test_color_depth_from_info_unsupported_bpc(struct kunit *test)
{
	struct drm_connector *connector;

	connector = kunit_kzalloc(test, sizeof(*connector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, connector);

	connector->display_info.bpc = 9;
	KUNIT_EXPECT_EQ(test, (int)amdgpu_dm_convert_color_depth_from_display_info(connector, false, 0),
			(int)COLOR_DEPTH_UNDEFINED);
}

/* Tests for to_drm_connector_type() */

/**
 * dm_test_to_connector_type_hdmi - Test To connector type hdmi
 * @test: The KUnit test context
 */
static void dm_test_to_connector_type_hdmi(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, to_drm_connector_type(SIGNAL_TYPE_HDMI_TYPE_A, 0),
			DRM_MODE_CONNECTOR_HDMIA);
}

/**
 * dm_test_to_connector_type_edp - Test To connector type edp
 * @test: The KUnit test context
 */
static void dm_test_to_connector_type_edp(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, to_drm_connector_type(SIGNAL_TYPE_EDP, 0),
			DRM_MODE_CONNECTOR_eDP);
}

/**
 * dm_test_to_connector_type_lvds - Test To connector type lvds
 * @test: The KUnit test context
 */
static void dm_test_to_connector_type_lvds(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, to_drm_connector_type(SIGNAL_TYPE_LVDS, 0),
			DRM_MODE_CONNECTOR_LVDS);
}

/**
 * dm_test_to_connector_type_rgb - Test To connector type rgb
 * @test: The KUnit test context
 */
static void dm_test_to_connector_type_rgb(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, to_drm_connector_type(SIGNAL_TYPE_RGB, 0),
			DRM_MODE_CONNECTOR_VGA);
}

/**
 * dm_test_to_connector_type_dp - Test To connector type dp
 * @test: The KUnit test context
 */
static void dm_test_to_connector_type_dp(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, to_drm_connector_type(SIGNAL_TYPE_DISPLAY_PORT, 0),
			DRM_MODE_CONNECTOR_DisplayPort);
}

/**
 * dm_test_to_connector_type_dp_mst - Test To connector type dp mst
 * @test: The KUnit test context
 */
static void dm_test_to_connector_type_dp_mst(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, to_drm_connector_type(SIGNAL_TYPE_DISPLAY_PORT_MST, 0),
			DRM_MODE_CONNECTOR_DisplayPort);
}

/**
 * dm_test_to_connector_type_dvi_dvii - Test To connector type dvi dvii
 * @test: The KUnit test context
 */
static void dm_test_to_connector_type_dvi_dvii(struct kunit *test)
{
	int type = to_drm_connector_type(SIGNAL_TYPE_DVI_SINGLE_LINK, CONNECTOR_ID_SINGLE_LINK_DVII);

	KUNIT_EXPECT_EQ(test, type, DRM_MODE_CONNECTOR_DVII);
}

/**
 * dm_test_to_connector_type_dual_link_dvii - Test To connector type dual link dvii
 * @test: The KUnit test context
 */
static void dm_test_to_connector_type_dual_link_dvii(struct kunit *test)
{
	int type = to_drm_connector_type(SIGNAL_TYPE_DVI_DUAL_LINK, CONNECTOR_ID_DUAL_LINK_DVII);

	KUNIT_EXPECT_EQ(test, type, DRM_MODE_CONNECTOR_DVII);
}

/**
 * dm_test_to_connector_type_dvi_dvid - Test To connector type dvi dvid
 * @test: The KUnit test context
 */
static void dm_test_to_connector_type_dvi_dvid(struct kunit *test)
{
	int type = to_drm_connector_type(SIGNAL_TYPE_DVI_SINGLE_LINK, CONNECTOR_ID_SINGLE_LINK_DVID);

	KUNIT_EXPECT_EQ(test, type, DRM_MODE_CONNECTOR_DVID);
}

/**
 * dm_test_to_connector_type_dual_link_dvid - Test To connector type dual link dvid
 * @test: The KUnit test context
 */
static void dm_test_to_connector_type_dual_link_dvid(struct kunit *test)
{
	int type = to_drm_connector_type(SIGNAL_TYPE_DVI_DUAL_LINK, CONNECTOR_ID_DUAL_LINK_DVID);

	KUNIT_EXPECT_EQ(test, type, DRM_MODE_CONNECTOR_DVID);
}

/**
 * dm_test_to_connector_type_virtual - Test To connector type virtual
 * @test: The KUnit test context
 */
static void dm_test_to_connector_type_virtual(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, to_drm_connector_type(SIGNAL_TYPE_VIRTUAL, 0),
			DRM_MODE_CONNECTOR_VIRTUAL);
}

/**
 * dm_test_to_connector_type_unknown - Test To connector type unknown
 * @test: The KUnit test context
 */
static void dm_test_to_connector_type_unknown(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, to_drm_connector_type(SIGNAL_TYPE_NONE, 0),
			DRM_MODE_CONNECTOR_Unknown);
}

/* Tests for is_duplicate_mode() */

/**
 * dm_test_is_duplicate_mode_empty_list - Test Is duplicate mode empty list
 * @test: The KUnit test context
 */
static void dm_test_is_duplicate_mode_empty_list(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;
	struct drm_display_mode mode = {};

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, aconnector);

	INIT_LIST_HEAD(&aconnector->base.probed_modes);
	mode.hdisplay = 1920;
	mode.vdisplay = 1080;

	KUNIT_EXPECT_FALSE(test, is_duplicate_mode(aconnector, &mode));
}

/**
 * dm_test_is_duplicate_mode_match - Test Is duplicate mode match
 * @test: The KUnit test context
 */
static void dm_test_is_duplicate_mode_match(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;
	struct drm_display_mode existing = {};
	struct drm_display_mode candidate = {};

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, aconnector);

	INIT_LIST_HEAD(&aconnector->base.probed_modes);
	existing.hdisplay = 1920;
	existing.vdisplay = 1080;
	existing.clock = 148500;
	list_add_tail(&existing.head, &aconnector->base.probed_modes);

	candidate.hdisplay = 1920;
	candidate.vdisplay = 1080;
	candidate.clock = 148500;

	KUNIT_EXPECT_TRUE(test, is_duplicate_mode(aconnector, &candidate));
}

/**
 * dm_test_is_duplicate_mode_no_match - Test Is duplicate mode no match
 * @test: The KUnit test context
 */
static void dm_test_is_duplicate_mode_no_match(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;
	struct drm_display_mode existing = {};
	struct drm_display_mode candidate = {};

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, aconnector);

	INIT_LIST_HEAD(&aconnector->base.probed_modes);
	existing.hdisplay = 1920;
	existing.vdisplay = 1080;
	existing.clock = 148500;
	list_add_tail(&existing.head, &aconnector->base.probed_modes);

	candidate.hdisplay = 2560;
	candidate.vdisplay = 1440;
	candidate.clock = 241500;

	KUNIT_EXPECT_FALSE(test, is_duplicate_mode(aconnector, &candidate));
}

/**
 * dm_test_is_duplicate_mode_same_size_different_clock - Test Is duplicate mode
 * same size different clock
 * @test: The KUnit test context
 */
static void dm_test_is_duplicate_mode_same_size_different_clock(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;
	struct drm_display_mode existing = {};
	struct drm_display_mode candidate = {};

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, aconnector);

	INIT_LIST_HEAD(&aconnector->base.probed_modes);
	existing.hdisplay = 1920;
	existing.vdisplay = 1080;
	existing.clock = 148500;
	list_add_tail(&existing.head, &aconnector->base.probed_modes);

	candidate.hdisplay = 1920;
	candidate.vdisplay = 1080;
	candidate.clock = 74250;

	KUNIT_EXPECT_FALSE(test, is_duplicate_mode(aconnector, &candidate));
}

/* Tests for amdgpu_dm_get_encoder_crtc_mask() */

/**
 * dm_test_encoder_crtc_mask_1 - Test Encoder crtc mask 1
 * @test: The KUnit test context
 */
static void dm_test_encoder_crtc_mask_1(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	adev->mode_info.num_crtc = 1;
	KUNIT_EXPECT_EQ(test, amdgpu_dm_get_encoder_crtc_mask(adev), 0x1);
}

/**
 * dm_test_encoder_crtc_mask_2 - Test Encoder crtc mask 2
 * @test: The KUnit test context
 */
static void dm_test_encoder_crtc_mask_2(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	adev->mode_info.num_crtc = 2;
	KUNIT_EXPECT_EQ(test, amdgpu_dm_get_encoder_crtc_mask(adev), 0x3);
}

/**
 * dm_test_encoder_crtc_mask_3 - Test Encoder crtc mask 3
 * @test: The KUnit test context
 */
static void dm_test_encoder_crtc_mask_3(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	adev->mode_info.num_crtc = 3;
	KUNIT_EXPECT_EQ(test, amdgpu_dm_get_encoder_crtc_mask(adev), 0x7);
}

/**
 * dm_test_encoder_crtc_mask_4 - Test Encoder crtc mask 4
 * @test: The KUnit test context
 */
static void dm_test_encoder_crtc_mask_4(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	adev->mode_info.num_crtc = 4;
	KUNIT_EXPECT_EQ(test, amdgpu_dm_get_encoder_crtc_mask(adev), 0xf);
}

/**
 * dm_test_encoder_crtc_mask_5 - Test Encoder crtc mask 5
 * @test: The KUnit test context
 */
static void dm_test_encoder_crtc_mask_5(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	adev->mode_info.num_crtc = 5;
	KUNIT_EXPECT_EQ(test, amdgpu_dm_get_encoder_crtc_mask(adev), 0x1f);
}

/**
 * dm_test_encoder_crtc_mask_6 - Test Encoder crtc mask 6
 * @test: The KUnit test context
 */
static void dm_test_encoder_crtc_mask_6(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	adev->mode_info.num_crtc = 6;
	KUNIT_EXPECT_EQ(test, amdgpu_dm_get_encoder_crtc_mask(adev), 0x3f);
}

/**
 * dm_test_encoder_crtc_mask_default - Test Encoder crtc mask default
 * @test: The KUnit test context
 */
static void dm_test_encoder_crtc_mask_default(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	/* Values > 6 use the default case */
	adev->mode_info.num_crtc = 8;
	KUNIT_EXPECT_EQ(test, amdgpu_dm_get_encoder_crtc_mask(adev), 0x3f);
}

/* Tests for get_aspect_ratio() */

/**
 * dm_test_aspect_ratio_no_data - Test Aspect ratio no data
 * @test: The KUnit test context
 */
static void dm_test_aspect_ratio_no_data(struct kunit *test)
{
	struct drm_display_mode mode = {};

	mode.picture_aspect_ratio = HDMI_PICTURE_ASPECT_NONE;
	KUNIT_EXPECT_EQ(test, (int)get_aspect_ratio(&mode), (int)ASPECT_RATIO_NO_DATA);
}

/**
 * dm_test_aspect_ratio_4_3 - Test Aspect ratio 4 3
 * @test: The KUnit test context
 */
static void dm_test_aspect_ratio_4_3(struct kunit *test)
{
	struct drm_display_mode mode = {};

	mode.picture_aspect_ratio = HDMI_PICTURE_ASPECT_4_3;
	KUNIT_EXPECT_EQ(test, (int)get_aspect_ratio(&mode), (int)ASPECT_RATIO_4_3);
}

/**
 * dm_test_aspect_ratio_16_9 - Test Aspect ratio 16 9
 * @test: The KUnit test context
 */
static void dm_test_aspect_ratio_16_9(struct kunit *test)
{
	struct drm_display_mode mode = {};

	mode.picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9;
	KUNIT_EXPECT_EQ(test, (int)get_aspect_ratio(&mode), (int)ASPECT_RATIO_16_9);
}

/**
 * dm_test_aspect_ratio_64_27 - Test Aspect ratio 64 27
 * @test: The KUnit test context
 */
static void dm_test_aspect_ratio_64_27(struct kunit *test)
{
	struct drm_display_mode mode = {};

	mode.picture_aspect_ratio = HDMI_PICTURE_ASPECT_64_27;
	KUNIT_EXPECT_EQ(test, (int)get_aspect_ratio(&mode), (int)ASPECT_RATIO_64_27);
}

/**
 * dm_test_aspect_ratio_256_135 - Test Aspect ratio 256 135
 * @test: The KUnit test context
 */
static void dm_test_aspect_ratio_256_135(struct kunit *test)
{
	struct drm_display_mode mode = {};

	mode.picture_aspect_ratio = HDMI_PICTURE_ASPECT_256_135;
	KUNIT_EXPECT_EQ(test, (int)get_aspect_ratio(&mode), (int)ASPECT_RATIO_256_135);
}

/* Tests for copy_crtc_timing_for_drm_display_mode() */

/**
 * dm_test_copy_crtc_timing_copies_all_fields - Test all crtc timing fields copied
 * @test: The KUnit test context
 */
static void dm_test_copy_crtc_timing_copies_all_fields(struct kunit *test)
{
	struct drm_display_mode src = {};
	struct drm_display_mode dst = {};

	src.crtc_hdisplay = 1920;
	src.crtc_vdisplay = 1080;
	src.crtc_clock = 148500;
	src.crtc_hblank_start = 1920;
	src.crtc_hblank_end = 2200;
	src.crtc_hsync_start = 2008;
	src.crtc_hsync_end = 2052;
	src.crtc_htotal = 2200;
	src.crtc_hskew = 1;
	src.crtc_vblank_start = 1080;
	src.crtc_vblank_end = 1125;
	src.crtc_vsync_start = 1084;
	src.crtc_vsync_end = 1089;
	src.crtc_vtotal = 1125;

	copy_crtc_timing_for_drm_display_mode(&src, &dst);

	KUNIT_EXPECT_EQ(test, dst.crtc_hdisplay, 1920);
	KUNIT_EXPECT_EQ(test, dst.crtc_vdisplay, 1080);
	KUNIT_EXPECT_EQ(test, dst.crtc_clock, 148500);
	KUNIT_EXPECT_EQ(test, dst.crtc_hblank_start, 1920);
	KUNIT_EXPECT_EQ(test, dst.crtc_hblank_end, 2200);
	KUNIT_EXPECT_EQ(test, dst.crtc_hsync_start, 2008);
	KUNIT_EXPECT_EQ(test, dst.crtc_hsync_end, 2052);
	KUNIT_EXPECT_EQ(test, dst.crtc_htotal, 2200);
	KUNIT_EXPECT_EQ(test, dst.crtc_hskew, 1);
	KUNIT_EXPECT_EQ(test, dst.crtc_vblank_start, 1080);
	KUNIT_EXPECT_EQ(test, dst.crtc_vblank_end, 1125);
	KUNIT_EXPECT_EQ(test, dst.crtc_vsync_start, 1084);
	KUNIT_EXPECT_EQ(test, dst.crtc_vsync_end, 1089);
	KUNIT_EXPECT_EQ(test, dst.crtc_vtotal, 1125);
}

/**
 * dm_test_copy_crtc_timing_leaves_non_crtc_fields - Test non-crtc fields untouched
 * @test: The KUnit test context
 */
static void dm_test_copy_crtc_timing_leaves_non_crtc_fields(struct kunit *test)
{
	struct drm_display_mode src = {};
	struct drm_display_mode dst = {};

	src.crtc_hdisplay = 1280;
	src.crtc_vdisplay = 720;

	/* Non-crtc geometry on dst must be preserved by the copy */
	dst.hdisplay = 1920;
	dst.vdisplay = 1080;
	dst.clock = 148500;

	copy_crtc_timing_for_drm_display_mode(&src, &dst);

	KUNIT_EXPECT_EQ(test, dst.crtc_hdisplay, 1280);
	KUNIT_EXPECT_EQ(test, dst.crtc_vdisplay, 720);
	KUNIT_EXPECT_EQ(test, dst.hdisplay, 1920);
	KUNIT_EXPECT_EQ(test, dst.vdisplay, 1080);
	KUNIT_EXPECT_EQ(test, dst.clock, 148500);
}

/* Tests for decide_crtc_timing_for_drm_display_mode() */

/**
 * dm_test_decide_crtc_timing_scale_enabled - Test Decide crtc timing scale enabled
 * @test: The KUnit test context
 */
static void dm_test_decide_crtc_timing_scale_enabled(struct kunit *test)
{
	struct drm_display_mode drm_mode = {};
	struct drm_display_mode native_mode = {};

	native_mode.crtc_clock = 148500;
	native_mode.crtc_hdisplay = 1920;
	native_mode.crtc_vdisplay = 1080;
	native_mode.crtc_htotal = 2200;
	native_mode.crtc_vtotal = 1125;
	native_mode.crtc_hsync_start = 2008;
	native_mode.crtc_hsync_end = 2052;
	native_mode.crtc_vsync_start = 1084;
	native_mode.crtc_vsync_end = 1089;

	/* Different clock/htotal/vtotal, but scale_enabled forces copy */
	drm_mode.clock = 74250;
	drm_mode.htotal = 1650;
	drm_mode.vtotal = 750;

	decide_crtc_timing_for_drm_display_mode(&drm_mode, &native_mode, true);

	KUNIT_EXPECT_EQ(test, drm_mode.crtc_clock, 148500);
	KUNIT_EXPECT_EQ(test, drm_mode.crtc_hdisplay, 1920);
	KUNIT_EXPECT_EQ(test, drm_mode.crtc_vdisplay, 1080);
	KUNIT_EXPECT_EQ(test, drm_mode.crtc_htotal, 2200);
	KUNIT_EXPECT_EQ(test, drm_mode.crtc_vtotal, 1125);
}

/**
 * dm_test_decide_crtc_timing_matching_mode - Test Decide crtc timing matching mode
 * @test: The KUnit test context
 */
static void dm_test_decide_crtc_timing_matching_mode(struct kunit *test)
{
	struct drm_display_mode drm_mode = {};
	struct drm_display_mode native_mode = {};

	native_mode.clock = 148500;
	native_mode.htotal = 2200;
	native_mode.vtotal = 1125;
	native_mode.crtc_clock = 148500;
	native_mode.crtc_hdisplay = 1920;
	native_mode.crtc_vdisplay = 1080;
	native_mode.crtc_htotal = 2200;
	native_mode.crtc_vtotal = 1125;

	/* Matching clock/htotal/vtotal triggers copy */
	drm_mode.clock = 148500;
	drm_mode.htotal = 2200;
	drm_mode.vtotal = 1125;

	decide_crtc_timing_for_drm_display_mode(&drm_mode, &native_mode, false);

	KUNIT_EXPECT_EQ(test, drm_mode.crtc_clock, 148500);
	KUNIT_EXPECT_EQ(test, drm_mode.crtc_hdisplay, 1920);
	KUNIT_EXPECT_EQ(test, drm_mode.crtc_vtotal, 1125);
}

/**
 * dm_test_decide_crtc_timing_no_copy - Test Decide crtc timing no copy
 * @test: The KUnit test context
 */
static void dm_test_decide_crtc_timing_no_copy(struct kunit *test)
{
	struct drm_display_mode drm_mode = {};
	struct drm_display_mode native_mode = {};

	native_mode.clock = 148500;
	native_mode.htotal = 2200;
	native_mode.vtotal = 1125;
	native_mode.crtc_clock = 148500;
	native_mode.crtc_hdisplay = 1920;

	/* Different timings, no scaling → no copy */
	drm_mode.clock = 74250;
	drm_mode.htotal = 1650;
	drm_mode.vtotal = 750;

	decide_crtc_timing_for_drm_display_mode(&drm_mode, &native_mode, false);

	KUNIT_EXPECT_EQ(test, drm_mode.crtc_clock, 0);
	KUNIT_EXPECT_EQ(test, drm_mode.crtc_hdisplay, 0);
}

/**
 * dm_test_decide_crtc_timing_no_crtc_clock - Test Decide crtc timing no crtc clock
 * @test: The KUnit test context
 */
static void dm_test_decide_crtc_timing_no_crtc_clock(struct kunit *test)
{
	struct drm_display_mode drm_mode = {};
	struct drm_display_mode native_mode = {};

	/* Matching timings but native crtc_clock is 0 → no copy */
	native_mode.clock = 148500;
	native_mode.htotal = 2200;
	native_mode.vtotal = 1125;
	native_mode.crtc_clock = 0;
	native_mode.crtc_hdisplay = 1920;

	drm_mode.clock = 148500;
	drm_mode.htotal = 2200;
	drm_mode.vtotal = 1125;

	decide_crtc_timing_for_drm_display_mode(&drm_mode, &native_mode, false);

	KUNIT_EXPECT_EQ(test, drm_mode.crtc_clock, 0);
	KUNIT_EXPECT_EQ(test, drm_mode.crtc_hdisplay, 0);
}

static void dm_test_decide_crtc_timing_sink_mode_same_totals(struct kunit *test)
{
	struct drm_display_mode native_mode = {
		DRM_MODE("3840x2160", DRM_MODE_TYPE_DRIVER, 594000, 3840, 4016, 4104,
			 4400, 0, 2160, 2168, 2178, 2250, 0,
			 DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) };
	struct drm_display_mode drm_mode = {
		DRM_MODE("4096x2160", DRM_MODE_TYPE_DRIVER, 594000, 4096, 4184, 4272,
			 4400, 0, 2160, 2168, 2178, 2250, 0,
			 DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) };

	drm_mode_set_crtcinfo(&native_mode, 0);
	drm_mode_set_crtcinfo(&drm_mode, 0);

	decide_crtc_timing_for_drm_display_mode(&drm_mode, &native_mode, false);

	KUNIT_EXPECT_EQ(test, drm_mode.crtc_hdisplay, 4096);
	KUNIT_EXPECT_EQ(test, drm_mode.crtc_hsync_start, 4184);
}

static void dm_test_decide_crtc_timing_inserted_mode(struct kunit *test)
{
	struct drm_display_mode native_mode = {
		DRM_MODE("3840x2160", DRM_MODE_TYPE_DRIVER, 594000, 3840, 4016, 4104,
			 4400, 0, 2160, 2168, 2178, 2250, 0,
			 DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) };
	struct drm_display_mode drm_mode;

	drm_mode_set_crtcinfo(&native_mode, 0);
	drm_mode = native_mode;
	drm_mode.hdisplay = 1920;
	drm_mode.vdisplay = 1080;
	drm_mode_set_crtcinfo(&drm_mode, 0);

	decide_crtc_timing_for_drm_display_mode(&drm_mode, &native_mode, false);

	KUNIT_EXPECT_EQ(test, drm_mode.crtc_hdisplay, 3840);
	KUNIT_EXPECT_EQ(test, drm_mode.crtc_vdisplay, 2160);
}

/* Tests for amdgpu_dm_connector_funcs_reset() */

static const struct drm_connector_funcs dm_test_connector_funcs = {
	.reset = amdgpu_dm_connector_funcs_reset,
	.atomic_duplicate_state = amdgpu_dm_connector_atomic_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

/**
 * dm_test_funcs_reset_sets_defaults - Test funcs_reset sets defaults
 * @test: The KUnit test context
 */
static void dm_test_funcs_reset_sets_defaults(struct kunit *test)
{
	struct device *dev;
	struct drm_device *drm;
	struct drm_connector *connector;
	struct dm_connector_state *dm_state;

	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	drm = __drm_kunit_helper_alloc_drm_device(test, dev,
						   sizeof(*drm), 0,
						   DRIVER_MODESET);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, drm);

	connector = drmm_kzalloc(drm, sizeof(*connector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, connector);

	drmm_connector_init(drm, connector, &dm_test_connector_funcs,
			    DRM_MODE_CONNECTOR_DisplayPort, NULL);

	amdgpu_dm_connector_funcs_reset(connector);

	KUNIT_ASSERT_NOT_NULL(test, connector->state);
	dm_state = to_dm_connector_state(connector->state);
	KUNIT_EXPECT_EQ(test, (int)dm_state->scaling, (int)RMX_OFF);
	KUNIT_EXPECT_FALSE(test, dm_state->underscan_enable);
	KUNIT_EXPECT_EQ(test, (int)dm_state->underscan_hborder, 0);
	KUNIT_EXPECT_EQ(test, (int)dm_state->underscan_vborder, 0);
	KUNIT_EXPECT_EQ(test, (int)dm_state->base.max_requested_bpc, 8);
	KUNIT_EXPECT_EQ(test, dm_state->vcpi_slots, 0);
	KUNIT_EXPECT_EQ(test, (int)dm_state->pbn, 0);
}

/**
 * dm_test_funcs_reset_edp_abm_level - Test funcs_reset eDP sets ABM
 * @test: The KUnit test context
 */
static void dm_test_funcs_reset_edp_abm_level(struct kunit *test)
{
	struct device *dev;
	struct drm_device *drm;
	struct drm_connector *connector;
	struct dm_connector_state *dm_state;
	int saved_abm_level = amdgpu_dm_get_abm_level_param();

	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	drm = __drm_kunit_helper_alloc_drm_device(test, dev,
						   sizeof(*drm), 0,
						   DRIVER_MODESET);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, drm);

	connector = drmm_kzalloc(drm, sizeof(*connector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, connector);

	drmm_connector_init(drm, connector, &dm_test_connector_funcs,
			    DRM_MODE_CONNECTOR_eDP, NULL);

	/* Test with abm_level > 0 */
	amdgpu_dm_set_abm_level_param(3);
	amdgpu_dm_connector_funcs_reset(connector);

	KUNIT_ASSERT_NOT_NULL(test, connector->state);
	dm_state = to_dm_connector_state(connector->state);
	KUNIT_EXPECT_EQ(test, (int)dm_state->abm_level, 3);

	amdgpu_dm_set_abm_level_param(saved_abm_level);
}

/**
 * dm_test_funcs_reset_edp_abm_disabled - Test funcs_reset eDP ABM
 * disabled
 * @test: The KUnit test context
 */
static void dm_test_funcs_reset_edp_abm_disabled(struct kunit *test)
{
	struct device *dev;
	struct drm_device *drm;
	struct drm_connector *connector;
	struct dm_connector_state *dm_state;
	int saved_abm_level = amdgpu_dm_get_abm_level_param();

	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	drm = __drm_kunit_helper_alloc_drm_device(test, dev,
						   sizeof(*drm), 0,
						   DRIVER_MODESET);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, drm);

	connector = drmm_kzalloc(drm, sizeof(*connector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, connector);

	drmm_connector_init(drm, connector, &dm_test_connector_funcs,
			    DRM_MODE_CONNECTOR_eDP, NULL);

	/* Test with abm_level <= 0 → immediate disable */
	amdgpu_dm_set_abm_level_param(-1);
	amdgpu_dm_connector_funcs_reset(connector);

	KUNIT_ASSERT_NOT_NULL(test, connector->state);
	dm_state = to_dm_connector_state(connector->state);
	KUNIT_EXPECT_EQ(test, (int)dm_state->abm_level,
			(int)ABM_LEVEL_IMMEDIATE_DISABLE);

	amdgpu_dm_set_abm_level_param(saved_abm_level);
}

/* Tests for amdgpu_dm_connector_atomic_duplicate_state() */

/**
 * dm_test_atomic_dup_state_copies_fields - Test atomic_duplicate copies
 * fields
 * @test: The KUnit test context
 */
static void dm_test_atomic_dup_state_copies_fields(struct kunit *test)
{
	struct device *dev;
	struct drm_device *drm;
	struct drm_connector *connector;
	struct dm_connector_state *dm_state;
	struct dm_connector_state *new_dm_state;
	struct drm_connector_state *new_state;

	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	drm = __drm_kunit_helper_alloc_drm_device(test, dev,
						   sizeof(*drm), 0,
						   DRIVER_MODESET);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, drm);

	connector = drmm_kzalloc(drm, sizeof(*connector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, connector);

	drmm_connector_init(drm, connector, &dm_test_connector_funcs,
			    DRM_MODE_CONNECTOR_HDMIA, NULL);

	amdgpu_dm_connector_funcs_reset(connector);
	KUNIT_ASSERT_NOT_NULL(test, connector->state);

	/* Modify original state fields */
	dm_state = to_dm_connector_state(connector->state);
	dm_state->scaling = RMX_CENTER;
	dm_state->underscan_enable = true;
	dm_state->underscan_hborder = 10;
	dm_state->underscan_vborder = 20;
	dm_state->freesync_capable = true;
	dm_state->abm_level = 2;
	dm_state->vcpi_slots = 4;
	dm_state->pbn = 1234;

	/* Duplicate */
	new_state = amdgpu_dm_connector_atomic_duplicate_state(connector);
	KUNIT_ASSERT_NOT_NULL(test, new_state);
	new_dm_state = to_dm_connector_state(new_state);

	/* Verify all fields copied */
	KUNIT_EXPECT_EQ(test, (int)new_dm_state->scaling, (int)RMX_CENTER);
	KUNIT_EXPECT_TRUE(test, new_dm_state->underscan_enable);
	KUNIT_EXPECT_EQ(test, (int)new_dm_state->underscan_hborder, 10);
	KUNIT_EXPECT_EQ(test, (int)new_dm_state->underscan_vborder, 20);
	KUNIT_EXPECT_TRUE(test, new_dm_state->freesync_capable);
	KUNIT_EXPECT_EQ(test, (int)new_dm_state->abm_level, 2);
	KUNIT_EXPECT_EQ(test, new_dm_state->vcpi_slots, 4);
	KUNIT_EXPECT_EQ(test, (int)new_dm_state->pbn, 1234);

	kfree(new_dm_state);
}

/* Tests for amdgpu_dm_fill_hdr_info_packet() */

/**
 * dm_test_fill_hdr_null_metadata - Test fill_hdr returns 0 with no
 * metadata
 * @test: The KUnit test context
 */
static void dm_test_fill_hdr_null_metadata(struct kunit *test)
{
	struct drm_connector_state state = {};
	struct dc_info_packet out = {};

	/* No hdr_output_metadata → early return 0, out stays zeroed */
	state.hdr_output_metadata = NULL;
	KUNIT_EXPECT_EQ(test, amdgpu_dm_fill_hdr_info_packet(&state, &out), 0);
	KUNIT_EXPECT_FALSE(test, out.valid);
}

/**
 * dm_test_fill_hdr_zeroes_output - Test fill_hdr zeroes output with no
 * metadata
 * @test: The KUnit test context
 */
static void dm_test_fill_hdr_zeroes_output(struct kunit *test)
{
	struct drm_connector_state state = {};
	struct dc_info_packet out;

	/* Pre-fill out with nonzero to verify memset(0) */
	memset(&out, 0xAA, sizeof(out));

	state.hdr_output_metadata = NULL;
	KUNIT_EXPECT_EQ(test, amdgpu_dm_fill_hdr_info_packet(&state, &out), 0);
	KUNIT_EXPECT_FALSE(test, out.valid);
	KUNIT_EXPECT_EQ(test, (int)out.hb0, 0);
	KUNIT_EXPECT_EQ(test, (int)out.hb1, 0);
	KUNIT_EXPECT_EQ(test, (int)out.hb2, 0);
	KUNIT_EXPECT_EQ(test, (int)out.hb3, 0);
}

/*
 * Build a minimal connector state carrying a valid static HDR metadata blob so
 * the packing path can be exercised without a full atomic state. connector_type
 * selects the sink-specific header layout.
 */
struct dm_test_hdr_ctx {
	struct drm_connector conn;
	struct drm_connector_state state;
	struct drm_property_blob blob;
	struct hdr_output_metadata meta;
};

static struct dm_test_hdr_ctx *
dm_test_hdr_ctx_alloc(struct kunit *test, int connector_type)
{
	struct dm_test_hdr_ctx *ctx;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	ctx->conn.connector_type = connector_type;
	ctx->state.connector = &ctx->conn;
	ctx->blob.data = &ctx->meta;
	ctx->blob.length = sizeof(ctx->meta);
	ctx->state.hdr_output_metadata = &ctx->blob;

	return ctx;
}

/**
 * dm_test_fill_hdr_hdmi - Test the HDMI infopacket header layout
 * @test: The KUnit test context
 *
 * A valid metadata blob on an HDMI connector packs a type 0x87 infoframe and
 * marks the packet valid.
 */
static void dm_test_fill_hdr_hdmi(struct kunit *test)
{
	struct dm_test_hdr_ctx *ctx =
		dm_test_hdr_ctx_alloc(test, DRM_MODE_CONNECTOR_HDMIA);
	struct dc_info_packet out;

	memset(&out, 0xAA, sizeof(out));

	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_fill_hdr_info_packet(&ctx->state, &out), 0);
	KUNIT_EXPECT_TRUE(test, out.valid);
	KUNIT_EXPECT_EQ(test, (int)out.hb0, 0x87);
	KUNIT_EXPECT_EQ(test, (int)out.hb1, 0x01);
	KUNIT_EXPECT_EQ(test, (int)out.hb2, 0x1A);
}

/**
 * dm_test_fill_hdr_dp - Test the DisplayPort SDP header layout
 * @test: The KUnit test context
 *
 * A valid metadata blob on a DisplayPort connector packs the SDP header with
 * the version/length subpacket bytes and marks the packet valid.
 */
static void dm_test_fill_hdr_dp(struct kunit *test)
{
	struct dm_test_hdr_ctx *ctx =
		dm_test_hdr_ctx_alloc(test, DRM_MODE_CONNECTOR_DisplayPort);
	struct dc_info_packet out;

	memset(&out, 0xAA, sizeof(out));

	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_fill_hdr_info_packet(&ctx->state, &out), 0);
	KUNIT_EXPECT_TRUE(test, out.valid);
	KUNIT_EXPECT_EQ(test, (int)out.hb0, 0x00);
	KUNIT_EXPECT_EQ(test, (int)out.hb1, 0x87);
	KUNIT_EXPECT_EQ(test, (int)out.hb2, 0x1D);
	KUNIT_EXPECT_EQ(test, (int)out.hb3, 0x13 << 2);
	KUNIT_EXPECT_EQ(test, (int)out.sb[0], 0x01);
	KUNIT_EXPECT_EQ(test, (int)out.sb[1], 0x1A);
}

/**
 * dm_test_fill_hdr_unsupported_connector - Test unsupported sinks are rejected
 * @test: The KUnit test context
 *
 * A connector type with no defined HDR infopacket layout returns -EINVAL and
 * leaves the packet invalid.
 */
static void dm_test_fill_hdr_unsupported_connector(struct kunit *test)
{
	struct dm_test_hdr_ctx *ctx =
		dm_test_hdr_ctx_alloc(test, DRM_MODE_CONNECTOR_VGA);
	struct dc_info_packet out = {};

	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_fill_hdr_info_packet(&ctx->state, &out),
			-EINVAL);
	KUNIT_EXPECT_FALSE(test, out.valid);
}

/**
 * dm_test_fill_hdr_bad_metadata - Test malformed metadata is propagated
 * @test: The KUnit test context
 *
 * A metadata blob with no payload makes drm_hdmi_infoframe_set_hdr_metadata()
 * fail, and that error is returned unchanged.
 */
static void dm_test_fill_hdr_bad_metadata(struct kunit *test)
{
	struct dm_test_hdr_ctx *ctx =
		dm_test_hdr_ctx_alloc(test, DRM_MODE_CONNECTOR_HDMIA);
	struct dc_info_packet out = {};

	ctx->blob.data = NULL;

	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_fill_hdr_info_packet(&ctx->state, &out),
			-EINVAL);
	KUNIT_EXPECT_FALSE(test, out.valid);
}

/* Tests for amdgpu_dm_connector_atomic_set_property() */

/*
 * Build a connector wired to a kunit-allocated amdgpu_device so that
 * drm_to_adev() resolves correctly, together with old/new dm states and
 * the set of properties used by the get/set property handlers.
 */
struct dm_test_prop_ctx {
	struct amdgpu_device *adev;
	struct drm_connector *connector;
	struct dm_connector_state *old_state;
	struct dm_connector_state *new_state;
	struct drm_property *scaling_prop;
	struct drm_property *hborder_prop;
	struct drm_property *vborder_prop;
	struct drm_property *underscan_prop;
	struct drm_property *abm_prop;
};

static struct dm_test_prop_ctx *dm_test_prop_ctx_alloc(struct kunit *test)
{
	struct dm_test_prop_ctx *ctx;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	ctx->adev = kunit_kzalloc(test, sizeof(*ctx->adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->adev);
	ctx->connector = kunit_kzalloc(test, sizeof(*ctx->connector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->connector);
	ctx->old_state = kunit_kzalloc(test, sizeof(*ctx->old_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->old_state);
	ctx->new_state = kunit_kzalloc(test, sizeof(*ctx->new_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->new_state);
	ctx->scaling_prop = kunit_kzalloc(test, sizeof(*ctx->scaling_prop), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->scaling_prop);
	ctx->hborder_prop = kunit_kzalloc(test, sizeof(*ctx->hborder_prop), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->hborder_prop);
	ctx->vborder_prop = kunit_kzalloc(test, sizeof(*ctx->vborder_prop), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->vborder_prop);
	ctx->underscan_prop = kunit_kzalloc(test, sizeof(*ctx->underscan_prop), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->underscan_prop);
	ctx->abm_prop = kunit_kzalloc(test, sizeof(*ctx->abm_prop), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->abm_prop);

	ctx->connector->dev = &ctx->adev->ddev;
	ctx->connector->state = &ctx->old_state->base;

	ctx->adev->ddev.mode_config.scaling_mode_property = ctx->scaling_prop;
	ctx->adev->mode_info.underscan_hborder_property = ctx->hborder_prop;
	ctx->adev->mode_info.underscan_vborder_property = ctx->vborder_prop;
	ctx->adev->mode_info.underscan_property = ctx->underscan_prop;
	ctx->adev->mode_info.abm_level_property = ctx->abm_prop;

	return ctx;
}

/**
 * dm_test_set_property_scaling_center - Test set scaling property to center
 * @test: The KUnit test context
 */
static void dm_test_set_property_scaling_center(struct kunit *test)
{
	struct dm_test_prop_ctx *ctx = dm_test_prop_ctx_alloc(test);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_connector_atomic_set_property(
				ctx->connector, &ctx->new_state->base,
				ctx->scaling_prop, DRM_MODE_SCALE_CENTER), 0);
	KUNIT_EXPECT_EQ(test, (int)ctx->new_state->scaling, (int)RMX_CENTER);
}

/**
 * dm_test_set_property_scaling_aspect - Test set scaling property to aspect
 * @test: The KUnit test context
 */
static void dm_test_set_property_scaling_aspect(struct kunit *test)
{
	struct dm_test_prop_ctx *ctx = dm_test_prop_ctx_alloc(test);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_connector_atomic_set_property(
				ctx->connector, &ctx->new_state->base,
				ctx->scaling_prop, DRM_MODE_SCALE_ASPECT), 0);
	KUNIT_EXPECT_EQ(test, (int)ctx->new_state->scaling, (int)RMX_ASPECT);
}

/**
 * dm_test_set_property_scaling_fullscreen - Test set scaling property to full
 * @test: The KUnit test context
 */
static void dm_test_set_property_scaling_fullscreen(struct kunit *test)
{
	struct dm_test_prop_ctx *ctx = dm_test_prop_ctx_alloc(test);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_connector_atomic_set_property(
				ctx->connector, &ctx->new_state->base,
				ctx->scaling_prop, DRM_MODE_SCALE_FULLSCREEN), 0);
	KUNIT_EXPECT_EQ(test, (int)ctx->new_state->scaling, (int)RMX_FULL);
}

/**
 * dm_test_set_property_scaling_none - Test set scaling property to none
 * @test: The KUnit test context
 */
static void dm_test_set_property_scaling_none(struct kunit *test)
{
	struct dm_test_prop_ctx *ctx = dm_test_prop_ctx_alloc(test);

	/* old scaling is RMX_CENTER so RMX_OFF is a real change */
	ctx->old_state->scaling = RMX_CENTER;
	KUNIT_EXPECT_EQ(test, amdgpu_dm_connector_atomic_set_property(
				ctx->connector, &ctx->new_state->base,
				ctx->scaling_prop, DRM_MODE_SCALE_NONE), 0);
	KUNIT_EXPECT_EQ(test, (int)ctx->new_state->scaling, (int)RMX_OFF);
}

/**
 * dm_test_set_property_scaling_unchanged - Test set scaling property unchanged
 * @test: The KUnit test context
 */
static void dm_test_set_property_scaling_unchanged(struct kunit *test)
{
	struct dm_test_prop_ctx *ctx = dm_test_prop_ctx_alloc(test);

	/* old already RMX_OFF, requesting NONE/OFF returns 0 without write */
	ctx->old_state->scaling = RMX_OFF;
	ctx->new_state->scaling = RMX_CENTER;
	KUNIT_EXPECT_EQ(test, amdgpu_dm_connector_atomic_set_property(
				ctx->connector, &ctx->new_state->base,
				ctx->scaling_prop, DRM_MODE_SCALE_NONE), 0);
	/* new_state untouched because of early return */
	KUNIT_EXPECT_EQ(test, (int)ctx->new_state->scaling, (int)RMX_CENTER);
}

/**
 * dm_test_set_property_underscan_hborder - Test set underscan hborder
 * @test: The KUnit test context
 */
static void dm_test_set_property_underscan_hborder(struct kunit *test)
{
	struct dm_test_prop_ctx *ctx = dm_test_prop_ctx_alloc(test);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_connector_atomic_set_property(
				ctx->connector, &ctx->new_state->base,
				ctx->hborder_prop, 42), 0);
	KUNIT_EXPECT_EQ(test, (int)ctx->new_state->underscan_hborder, 42);
}

/**
 * dm_test_set_property_underscan_vborder - Test set underscan vborder
 * @test: The KUnit test context
 */
static void dm_test_set_property_underscan_vborder(struct kunit *test)
{
	struct dm_test_prop_ctx *ctx = dm_test_prop_ctx_alloc(test);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_connector_atomic_set_property(
				ctx->connector, &ctx->new_state->base,
				ctx->vborder_prop, 24), 0);
	KUNIT_EXPECT_EQ(test, (int)ctx->new_state->underscan_vborder, 24);
}

/**
 * dm_test_set_property_underscan_enable - Test set underscan enable
 * @test: The KUnit test context
 */
static void dm_test_set_property_underscan_enable(struct kunit *test)
{
	struct dm_test_prop_ctx *ctx = dm_test_prop_ctx_alloc(test);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_connector_atomic_set_property(
				ctx->connector, &ctx->new_state->base,
				ctx->underscan_prop, 1), 0);
	KUNIT_EXPECT_TRUE(test, ctx->new_state->underscan_enable);
}

/**
 * dm_test_set_property_abm_sysfs_control - Test set abm sysfs control
 * @test: The KUnit test context
 */
static void dm_test_set_property_abm_sysfs_control(struct kunit *test)
{
	struct dm_test_prop_ctx *ctx = dm_test_prop_ctx_alloc(test);

	ctx->new_state->abm_sysfs_forbidden = true;
	KUNIT_EXPECT_EQ(test, amdgpu_dm_connector_atomic_set_property(
				ctx->connector, &ctx->new_state->base,
				ctx->abm_prop, ABM_SYSFS_CONTROL), 0);
	KUNIT_EXPECT_FALSE(test, ctx->new_state->abm_sysfs_forbidden);
}

/**
 * dm_test_set_property_abm_level_off - Test set abm level off
 * @test: The KUnit test context
 */
static void dm_test_set_property_abm_level_off(struct kunit *test)
{
	struct dm_test_prop_ctx *ctx = dm_test_prop_ctx_alloc(test);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_connector_atomic_set_property(
				ctx->connector, &ctx->new_state->base,
				ctx->abm_prop, ABM_LEVEL_OFF), 0);
	KUNIT_EXPECT_TRUE(test, ctx->new_state->abm_sysfs_forbidden);
	KUNIT_EXPECT_EQ(test, (int)ctx->new_state->abm_level,
			(int)ABM_LEVEL_IMMEDIATE_DISABLE);
}

/**
 * dm_test_set_property_abm_level_value - Test set abm level to a value
 * @test: The KUnit test context
 */
static void dm_test_set_property_abm_level_value(struct kunit *test)
{
	struct dm_test_prop_ctx *ctx = dm_test_prop_ctx_alloc(test);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_connector_atomic_set_property(
				ctx->connector, &ctx->new_state->base,
				ctx->abm_prop, 3), 0);
	KUNIT_EXPECT_TRUE(test, ctx->new_state->abm_sysfs_forbidden);
	KUNIT_EXPECT_EQ(test, (int)ctx->new_state->abm_level, 3);
}

/**
 * dm_test_set_property_unknown - Test set unknown property returns -EINVAL
 * @test: The KUnit test context
 */
static void dm_test_set_property_unknown(struct kunit *test)
{
	struct dm_test_prop_ctx *ctx = dm_test_prop_ctx_alloc(test);
	struct drm_property *other;

	other = kunit_kzalloc(test, sizeof(*other), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, other);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_connector_atomic_set_property(
				ctx->connector, &ctx->new_state->base,
				other, 0), -EINVAL);
}

/* Tests for amdgpu_dm_connector_atomic_get_property() */

/**
 * dm_test_get_property_scaling_center - Test get scaling property center
 * @test: The KUnit test context
 */
static void dm_test_get_property_scaling_center(struct kunit *test)
{
	struct dm_test_prop_ctx *ctx = dm_test_prop_ctx_alloc(test);
	uint64_t val = 0;

	ctx->new_state->scaling = RMX_CENTER;
	KUNIT_EXPECT_EQ(test, amdgpu_dm_connector_atomic_get_property(
				ctx->connector, &ctx->new_state->base,
				ctx->scaling_prop, &val), 0);
	KUNIT_EXPECT_EQ(test, (int)val, (int)DRM_MODE_SCALE_CENTER);
}

/**
 * dm_test_get_property_scaling_aspect - Test get scaling property aspect
 * @test: The KUnit test context
 */
static void dm_test_get_property_scaling_aspect(struct kunit *test)
{
	struct dm_test_prop_ctx *ctx = dm_test_prop_ctx_alloc(test);
	uint64_t val = 0;

	ctx->new_state->scaling = RMX_ASPECT;
	KUNIT_EXPECT_EQ(test, amdgpu_dm_connector_atomic_get_property(
				ctx->connector, &ctx->new_state->base,
				ctx->scaling_prop, &val), 0);
	KUNIT_EXPECT_EQ(test, (int)val, (int)DRM_MODE_SCALE_ASPECT);
}

/**
 * dm_test_get_property_scaling_full - Test get scaling property fullscreen
 * @test: The KUnit test context
 */
static void dm_test_get_property_scaling_full(struct kunit *test)
{
	struct dm_test_prop_ctx *ctx = dm_test_prop_ctx_alloc(test);
	uint64_t val = 0;

	ctx->new_state->scaling = RMX_FULL;
	KUNIT_EXPECT_EQ(test, amdgpu_dm_connector_atomic_get_property(
				ctx->connector, &ctx->new_state->base,
				ctx->scaling_prop, &val), 0);
	KUNIT_EXPECT_EQ(test, (int)val, (int)DRM_MODE_SCALE_FULLSCREEN);
}

/**
 * dm_test_get_property_scaling_off - Test get scaling property off/none
 * @test: The KUnit test context
 */
static void dm_test_get_property_scaling_off(struct kunit *test)
{
	struct dm_test_prop_ctx *ctx = dm_test_prop_ctx_alloc(test);
	uint64_t val = 0;

	ctx->new_state->scaling = RMX_OFF;
	KUNIT_EXPECT_EQ(test, amdgpu_dm_connector_atomic_get_property(
				ctx->connector, &ctx->new_state->base,
				ctx->scaling_prop, &val), 0);
	KUNIT_EXPECT_EQ(test, (int)val, (int)DRM_MODE_SCALE_NONE);
}

/**
 * dm_test_get_property_underscan_borders - Test get underscan borders/enable
 * @test: The KUnit test context
 */
static void dm_test_get_property_underscan_borders(struct kunit *test)
{
	struct dm_test_prop_ctx *ctx = dm_test_prop_ctx_alloc(test);
	uint64_t val = 0;

	ctx->new_state->underscan_hborder = 12;
	ctx->new_state->underscan_vborder = 34;
	ctx->new_state->underscan_enable = true;

	KUNIT_EXPECT_EQ(test, amdgpu_dm_connector_atomic_get_property(
				ctx->connector, &ctx->new_state->base,
				ctx->hborder_prop, &val), 0);
	KUNIT_EXPECT_EQ(test, (int)val, 12);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_connector_atomic_get_property(
				ctx->connector, &ctx->new_state->base,
				ctx->vborder_prop, &val), 0);
	KUNIT_EXPECT_EQ(test, (int)val, 34);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_connector_atomic_get_property(
				ctx->connector, &ctx->new_state->base,
				ctx->underscan_prop, &val), 0);
	KUNIT_EXPECT_EQ(test, (int)val, 1);
}

/**
 * dm_test_get_property_abm_sysfs_allowed - Test get abm returns sysfs control
 * @test: The KUnit test context
 */
static void dm_test_get_property_abm_sysfs_allowed(struct kunit *test)
{
	struct dm_test_prop_ctx *ctx = dm_test_prop_ctx_alloc(test);
	uint64_t val = 0;

	ctx->new_state->abm_sysfs_forbidden = false;
	KUNIT_EXPECT_EQ(test, amdgpu_dm_connector_atomic_get_property(
				ctx->connector, &ctx->new_state->base,
				ctx->abm_prop, &val), 0);
	KUNIT_EXPECT_EQ(test, (int)val, (int)ABM_SYSFS_CONTROL);
}

/**
 * dm_test_get_property_abm_level - Test get abm returns level when forbidden
 * @test: The KUnit test context
 */
static void dm_test_get_property_abm_level(struct kunit *test)
{
	struct dm_test_prop_ctx *ctx = dm_test_prop_ctx_alloc(test);
	uint64_t val = 0;

	ctx->new_state->abm_sysfs_forbidden = true;
	ctx->new_state->abm_level = 2;
	KUNIT_EXPECT_EQ(test, amdgpu_dm_connector_atomic_get_property(
				ctx->connector, &ctx->new_state->base,
				ctx->abm_prop, &val), 0);
	KUNIT_EXPECT_EQ(test, (int)val, 2);
}

/**
 * dm_test_get_property_abm_disabled_zero - Test get abm returns 0 when disabled
 * @test: The KUnit test context
 */
static void dm_test_get_property_abm_disabled_zero(struct kunit *test)
{
	struct dm_test_prop_ctx *ctx = dm_test_prop_ctx_alloc(test);
	uint64_t val = 0xdead;

	ctx->new_state->abm_sysfs_forbidden = true;
	ctx->new_state->abm_level = ABM_LEVEL_IMMEDIATE_DISABLE;
	KUNIT_EXPECT_EQ(test, amdgpu_dm_connector_atomic_get_property(
				ctx->connector, &ctx->new_state->base,
				ctx->abm_prop, &val), 0);
	KUNIT_EXPECT_EQ(test, (int)val, 0);
}

/**
 * dm_test_get_property_unknown - Test get unknown property returns -EINVAL
 * @test: The KUnit test context
 */
static void dm_test_get_property_unknown(struct kunit *test)
{
	struct dm_test_prop_ctx *ctx = dm_test_prop_ctx_alloc(test);
	struct drm_property *other;
	uint64_t val = 0;

	other = kunit_kzalloc(test, sizeof(*other), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, other);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_connector_atomic_get_property(
				ctx->connector, &ctx->new_state->base,
				other, &val), -EINVAL);
}

/* Tests for amdgpu_dm_get_highest_refresh_rate_mode() */

/**
 * dm_test_highest_refresh_writeback_null - Test writeback connector returns NULL
 * @test: The KUnit test context
 */
static void dm_test_highest_refresh_writeback_null(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);

	aconnector->base.connector_type = DRM_MODE_CONNECTOR_WRITEBACK;
	KUNIT_EXPECT_NULL(test, amdgpu_dm_get_highest_refresh_rate_mode(aconnector, false));
}

/**
 * dm_test_highest_refresh_cached_base - Test cached freesync_vid_base is returned
 * @test: The KUnit test context
 */
static void dm_test_highest_refresh_cached_base(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);

	aconnector->base.connector_type = DRM_MODE_CONNECTOR_HDMIA;
	aconnector->freesync_vid_base.clock = 148500;

	KUNIT_EXPECT_PTR_EQ(test, amdgpu_dm_get_highest_refresh_rate_mode(aconnector, false),
			    &aconnector->freesync_vid_base);
}

/**
 * dm_test_highest_refresh_preferred_mode - Test preferred mode is selected
 * @test: The KUnit test context
 */
static void dm_test_highest_refresh_preferred_mode(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;
	struct drm_display_mode *mode;

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);
	mode = kunit_kzalloc(test, sizeof(*mode), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, mode);

	aconnector->base.connector_type = DRM_MODE_CONNECTOR_HDMIA;
	INIT_LIST_HEAD(&aconnector->base.modes);

	mode->type = DRM_MODE_TYPE_PREFERRED;
	mode->clock = 148500;
	mode->hdisplay = 1920;
	mode->vdisplay = 1080;
	mode->htotal = 2200;
	mode->vtotal = 1125;
	list_add_tail(&mode->head, &aconnector->base.modes);

	KUNIT_EXPECT_PTR_EQ(test, amdgpu_dm_get_highest_refresh_rate_mode(aconnector, false),
			    mode);
}

/* Tests for amdgpu_dm_is_freesync_video_mode() */

/**
 * dm_test_is_freesync_video_mode_null_mode - Test NULL mode returns false
 * @test: The KUnit test context
 */
static void dm_test_is_freesync_video_mode_null_mode(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);

	aconnector->base.connector_type = DRM_MODE_CONNECTOR_HDMIA;
	aconnector->freesync_vid_base.clock = 148500;

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_is_freesync_video_mode(NULL, aconnector));
}

/**
 * dm_test_is_freesync_video_mode_match - Test matching mode returns true
 * @test: The KUnit test context
 */
static void dm_test_is_freesync_video_mode_match(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;
	struct drm_display_mode candidate = {};

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);

	/* Cached high mode acts as reference */
	aconnector->base.connector_type = DRM_MODE_CONNECTOR_HDMIA;
	aconnector->freesync_vid_base.clock = 148500;
	aconnector->freesync_vid_base.hdisplay = 1920;
	aconnector->freesync_vid_base.vdisplay = 1080;
	aconnector->freesync_vid_base.hsync_start = 2008;
	aconnector->freesync_vid_base.hsync_end = 2052;
	aconnector->freesync_vid_base.htotal = 2200;
	aconnector->freesync_vid_base.vsync_start = 1084;
	aconnector->freesync_vid_base.vsync_end = 1089;
	aconnector->freesync_vid_base.vtotal = 1125;

	candidate.clock = 148500;
	candidate.hdisplay = 1920;
	candidate.vdisplay = 1080;
	candidate.hsync_start = 2008;
	candidate.hsync_end = 2052;
	candidate.htotal = 2200;
	candidate.vsync_start = 1084;
	candidate.vsync_end = 1089;
	candidate.vtotal = 1125;

	KUNIT_EXPECT_TRUE(test, amdgpu_dm_is_freesync_video_mode(&candidate, aconnector));
}

/**
 * dm_test_is_freesync_video_mode_no_match - Test mismatched mode returns false
 * @test: The KUnit test context
 */
static void dm_test_is_freesync_video_mode_no_match(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;
	struct drm_display_mode candidate = {};

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);

	aconnector->base.connector_type = DRM_MODE_CONNECTOR_HDMIA;
	aconnector->freesync_vid_base.clock = 148500;
	aconnector->freesync_vid_base.hdisplay = 1920;
	aconnector->freesync_vid_base.vdisplay = 1080;
	aconnector->freesync_vid_base.htotal = 2200;
	aconnector->freesync_vid_base.vtotal = 1125;

	/* Different resolution → not a freesync video mode */
	candidate.clock = 148500;
	candidate.hdisplay = 1280;
	candidate.vdisplay = 720;
	candidate.htotal = 1650;
	candidate.vtotal = 750;

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_is_freesync_video_mode(&candidate, aconnector));
}

/* Tests for amdgpu_dm_update_cacp_caps() */

struct dm_cacp_fixture {
	struct amdgpu_device *adev;
	struct amdgpu_dm_connector *aconnector;
	struct dc_link *link;
};

static void setup_cacp_fixture(struct kunit *test,
			       struct dm_cacp_fixture *fixture,
			       enum signal_type signal,
			       enum dc_panel_type panel_type)
{
	fixture->adev = kunit_kzalloc(test, sizeof(*fixture->adev), GFP_KERNEL);
	fixture->aconnector = kunit_kzalloc(test, sizeof(*fixture->aconnector),
					    GFP_KERNEL);
	fixture->link = kunit_kzalloc(test, sizeof(*fixture->link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, fixture->adev);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, fixture->aconnector);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, fixture->link);

	fixture->aconnector->dc_link = fixture->link;
	fixture->aconnector->base.dev = &fixture->adev->ddev;
	fixture->link->connector_signal = signal;
	fixture->link->panel_type = panel_type;
}

/**
 * dm_test_cacp_caps_unsupported_ip - Test CACP disabled on old DCE IP
 * @test: The KUnit test context
 *
 * A DCE IP version below 3.1.4 does not support CACP, so cacp_supported
 * must remain false regardless of signal or panel type.
 */
static void dm_test_cacp_caps_unsupported_ip(struct kunit *test)
{
	struct dm_cacp_fixture fixture = {};

	setup_cacp_fixture(test, &fixture, SIGNAL_TYPE_EDP, PANEL_TYPE_OLED);
	fixture.adev->ip_versions[DCE_HWIP][0] = IP_VERSION(3, 1, 2);

	amdgpu_dm_update_cacp_caps(fixture.aconnector);

	KUNIT_EXPECT_FALSE(test, fixture.link->panel_config.cacp.cacp_supported);
}

/**
 * dm_test_cacp_caps_excluded_ip_316 - Test CACP disabled on DCE IP 3.1.6
 * @test: The KUnit test context
 *
 * DCE IP version 3.1.6 is explicitly excluded from CACP support.
 */
static void dm_test_cacp_caps_excluded_ip_316(struct kunit *test)
{
	struct dm_cacp_fixture fixture = {};

	setup_cacp_fixture(test, &fixture, SIGNAL_TYPE_EDP, PANEL_TYPE_OLED);
	fixture.adev->ip_versions[DCE_HWIP][0] = IP_VERSION(3, 1, 6);

	amdgpu_dm_update_cacp_caps(fixture.aconnector);

	KUNIT_EXPECT_FALSE(test, fixture.link->panel_config.cacp.cacp_supported);
}

/**
 * dm_test_cacp_caps_edp_oled_supported - Test CACP enabled on eDP OLED
 * @test: The KUnit test context
 *
 * A supported DCE IP version on an eDP OLED panel must enable CACP.
 */
static void dm_test_cacp_caps_edp_oled_supported(struct kunit *test)
{
	struct dm_cacp_fixture fixture = {};

	setup_cacp_fixture(test, &fixture, SIGNAL_TYPE_EDP, PANEL_TYPE_OLED);
	fixture.adev->ip_versions[DCE_HWIP][0] = IP_VERSION(3, 1, 4);

	amdgpu_dm_update_cacp_caps(fixture.aconnector);

	KUNIT_EXPECT_TRUE(test, fixture.link->panel_config.cacp.cacp_supported);
}

/**
 * dm_test_cacp_caps_lvds_oled_supported - Test CACP enabled on LVDS OLED
 * @test: The KUnit test context
 *
 * LVDS is an accepted connector signal for CACP support.
 */
static void dm_test_cacp_caps_lvds_oled_supported(struct kunit *test)
{
	struct dm_cacp_fixture fixture = {};

	setup_cacp_fixture(test, &fixture, SIGNAL_TYPE_LVDS, PANEL_TYPE_OLED);
	fixture.adev->ip_versions[DCE_HWIP][0] = IP_VERSION(3, 5, 0);

	amdgpu_dm_update_cacp_caps(fixture.aconnector);

	KUNIT_EXPECT_TRUE(test, fixture.link->panel_config.cacp.cacp_supported);
}

/**
 * dm_test_cacp_caps_non_edp_signal - Test CACP disabled on non-eDP/LVDS signal
 * @test: The KUnit test context
 *
 * External DisplayPort is neither eDP nor LVDS, so CACP must be disabled.
 */
static void dm_test_cacp_caps_non_edp_signal(struct kunit *test)
{
	struct dm_cacp_fixture fixture = {};

	setup_cacp_fixture(test, &fixture, SIGNAL_TYPE_DISPLAY_PORT,
			   PANEL_TYPE_OLED);
	fixture.adev->ip_versions[DCE_HWIP][0] = IP_VERSION(3, 1, 4);

	amdgpu_dm_update_cacp_caps(fixture.aconnector);

	KUNIT_EXPECT_FALSE(test, fixture.link->panel_config.cacp.cacp_supported);
}

/**
 * dm_test_cacp_caps_lcd_panel - Test CACP disabled on LCD panel
 * @test: The KUnit test context
 *
 * Plain LCD panels do not benefit from CACP, so it must be disabled even
 * on a supported IP version and eDP signal.
 */
static void dm_test_cacp_caps_lcd_panel(struct kunit *test)
{
	struct dm_cacp_fixture fixture = {};

	setup_cacp_fixture(test, &fixture, SIGNAL_TYPE_EDP, PANEL_TYPE_LCD);
	fixture.adev->ip_versions[DCE_HWIP][0] = IP_VERSION(3, 1, 4);

	amdgpu_dm_update_cacp_caps(fixture.aconnector);

	KUNIT_EXPECT_FALSE(test, fixture.link->panel_config.cacp.cacp_supported);
}

/* Tests for amdgpu_dm_set_panel_type() */

struct dm_panel_type_fixture {
	struct amdgpu_device *adev;
	struct drm_device *drm;
	struct amdgpu_dm_connector *aconnector;
	struct dc_link *link;
	struct dc_sink *sink;
};

static void setup_panel_type_fixture(struct kunit *test,
				     struct dm_panel_type_fixture *fixture)
{
	struct device *dev;

	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	fixture->drm = __drm_kunit_helper_alloc_drm_device(test, dev,
							   sizeof(*fixture->adev),
							   offsetof(struct amdgpu_device, ddev),
							   DRIVER_MODESET);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, fixture->drm);
	fixture->adev = drm_to_adev(fixture->drm);

	fixture->aconnector = drmm_kzalloc(fixture->drm, sizeof(*fixture->aconnector),
					   GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, fixture->aconnector);
	fixture->link = kunit_kzalloc(test, sizeof(*fixture->link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, fixture->link);
	fixture->sink = kunit_kzalloc(test, sizeof(*fixture->sink), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, fixture->sink);

	fixture->aconnector->dc_link = fixture->link;
	drmm_connector_init(fixture->drm, &fixture->aconnector->base,
			    &dm_test_connector_funcs, DRM_MODE_CONNECTOR_eDP,
			    NULL);
}

/**
 * dm_test_set_panel_type_vsdb_oled - Test VSDB OLED maps to PANEL_TYPE_OLED
 * @test: The KUnit test context
 */
static void dm_test_set_panel_type_vsdb_oled(struct kunit *test)
{
	struct dm_panel_type_fixture fixture = {};

	setup_panel_type_fixture(test, &fixture);
	fixture.aconnector->base.display_info.amd_vsdb.panel_type =
		AMD_VSDB_PANEL_TYPE_OLED;

	amdgpu_dm_set_panel_type(fixture.aconnector);

	KUNIT_EXPECT_EQ(test, (int)fixture.link->panel_type,
			(int)PANEL_TYPE_OLED);
}

/**
 * dm_test_set_panel_type_vsdb_miniled - Test VSDB MINILED maps to PANEL_TYPE_MINILED
 * @test: The KUnit test context
 */
static void dm_test_set_panel_type_vsdb_miniled(struct kunit *test)
{
	struct dm_panel_type_fixture fixture = {};

	setup_panel_type_fixture(test, &fixture);
	fixture.aconnector->base.display_info.amd_vsdb.panel_type =
		AMD_VSDB_PANEL_TYPE_MINILED;

	amdgpu_dm_set_panel_type(fixture.aconnector);

	KUNIT_EXPECT_EQ(test, (int)fixture.link->panel_type,
			(int)PANEL_TYPE_MINILED);
}

/**
 * dm_test_set_panel_type_dpcd_oled - Test DPCD oled bit maps to PANEL_TYPE_OLED
 * @test: The KUnit test context
 */
static void dm_test_set_panel_type_dpcd_oled(struct kunit *test)
{
	struct dm_panel_type_fixture fixture = {};

	setup_panel_type_fixture(test, &fixture);
	fixture.link->dpcd_sink_ext_caps.bits.oled = 1;

	amdgpu_dm_set_panel_type(fixture.aconnector);

	KUNIT_EXPECT_EQ(test, (int)fixture.link->panel_type,
			(int)PANEL_TYPE_OLED);
}

/**
 * dm_test_set_panel_type_dpcd_miniled - Test DPCD miniled bit maps to PANEL_TYPE_MINILED
 * @test: The KUnit test context
 */
static void dm_test_set_panel_type_dpcd_miniled(struct kunit *test)
{
	struct dm_panel_type_fixture fixture = {};

	setup_panel_type_fixture(test, &fixture);
	fixture.link->dpcd_sink_ext_caps.bits.miniled = 1;

	amdgpu_dm_set_panel_type(fixture.aconnector);

	KUNIT_EXPECT_EQ(test, (int)fixture.link->panel_type,
			(int)PANEL_TYPE_MINILED);
}

/**
 * dm_test_set_panel_type_did_oled - Test DID OLED maps to PANEL_TYPE_OLED
 * @test: The KUnit test context
 *
 * When VSDB and DPCD do not identify the panel, a DID panel type of
 * DRM_MODE_PANEL_TYPE_OLED must map to PANEL_TYPE_OLED.
 */
static void dm_test_set_panel_type_did_oled(struct kunit *test)
{
	struct dm_panel_type_fixture fixture = {};

	setup_panel_type_fixture(test, &fixture);
	fixture.aconnector->base.display_info.panel_type =
		DRM_MODE_PANEL_TYPE_OLED;

	amdgpu_dm_set_panel_type(fixture.aconnector);

	KUNIT_EXPECT_EQ(test, (int)fixture.link->panel_type,
			(int)PANEL_TYPE_OLED);
}

/**
 * dm_test_set_panel_type_did_lcd - Test DID LCD maps to PANEL_TYPE_LCD
 * @test: The KUnit test context
 *
 * When VSDB and DPCD do not identify the panel, a DID panel type of
 * DRM_MODE_PANEL_TYPE_LCD must map to PANEL_TYPE_LCD.
 */
static void dm_test_set_panel_type_did_lcd(struct kunit *test)
{
	struct dm_panel_type_fixture fixture = {};

	setup_panel_type_fixture(test, &fixture);
	fixture.aconnector->base.display_info.panel_type =
		DRM_MODE_PANEL_TYPE_LCD;

	amdgpu_dm_set_panel_type(fixture.aconnector);

	KUNIT_EXPECT_EQ(test, (int)fixture.link->panel_type,
			(int)PANEL_TYPE_LCD);
}

/**
 * dm_test_set_panel_type_vendor_lum_heuristic - Test vendor luminance heuristic maps to MINILED
 * @test: The KUnit test context
 *
 * A panel from the specific vendor whose first luminance range is at least
 * 1.5x the second is treated as a mini-LED panel.
 */
static void dm_test_set_panel_type_vendor_lum_heuristic(struct kunit *test)
{
	struct dm_panel_type_fixture fixture = {};
	struct drm_amd_vsdb_info *vsdb;

	setup_panel_type_fixture(test, &fixture);
	fixture.link->local_sink = fixture.sink;
	fixture.sink->edid_caps.manufacturer_id = DDC_MANUFACTURERNAME_SAMSUNG;

	vsdb = &fixture.aconnector->base.display_info.amd_vsdb;
	vsdb->version = 1;
	vsdb->luminance_range1.max_luminance = 3000;
	vsdb->luminance_range2.max_luminance = 1000;

	amdgpu_dm_set_panel_type(fixture.aconnector);

	KUNIT_EXPECT_EQ(test, (int)fixture.link->panel_type,
			(int)PANEL_TYPE_MINILED);
}

/**
 * dm_test_set_panel_type_defaults_to_lcd - Test undetermined panel defaults to LCD
 * @test: The KUnit test context
 *
 * When no source identifies the panel, the type now defaults to
 * PANEL_TYPE_LCD instead of remaining PANEL_TYPE_NONE.
 */
static void dm_test_set_panel_type_defaults_to_lcd(struct kunit *test)
{
	struct dm_panel_type_fixture fixture = {};

	setup_panel_type_fixture(test, &fixture);

	amdgpu_dm_set_panel_type(fixture.aconnector);

	KUNIT_EXPECT_EQ(test, (int)fixture.link->panel_type,
			(int)PANEL_TYPE_LCD);
}

/* Tests for update_subconnector_property() */

/**
 * dm_test_update_subconnector_dp_with_sink - Test subconnector property is set
 * from the dongle type for a DisplayPort connector with a sink
 * @test: The KUnit test context
 */
static void dm_test_update_subconnector_dp_with_sink(struct kunit *test)
{
	struct device *dev;
	struct drm_device *drm;
	struct amdgpu_dm_connector *aconnector;
	struct dc_link *link;
	uint64_t val = 0;

	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	drm = __drm_kunit_helper_alloc_drm_device(test, dev,
						   sizeof(*drm), 0,
						   DRIVER_MODESET);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, drm);

	aconnector = drmm_kzalloc(drm, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);
	link = drmm_kzalloc(drm, sizeof(*link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, link);

	drmm_connector_init(drm, &aconnector->base, &dm_test_connector_funcs,
			    DRM_MODE_CONNECTOR_DisplayPort, NULL);
	drm_connector_attach_dp_subconnector_property(&aconnector->base);

	link->dpcd_caps.dongle_type = DISPLAY_DONGLE_DP_VGA_CONVERTER;
	aconnector->dc_link = link;
	/* Any non-NULL sink enables dongle-type resolution */
	aconnector->dc_sink = kunit_kzalloc(test, sizeof(*aconnector->dc_sink), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector->dc_sink);

	update_subconnector_property(aconnector);

	KUNIT_EXPECT_EQ(test, drm_object_property_get_default_value(&aconnector->base.base,
				aconnector->base.dev->mode_config.dp_subconnector_property,
				&val), 0);
	KUNIT_EXPECT_EQ(test, (int)val, (int)DRM_MODE_SUBCONNECTOR_VGA);
}

/**
 * dm_test_update_subconnector_dp_no_sink - Test subconnector property stays
 * unknown for a DisplayPort connector without a sink
 * @test: The KUnit test context
 */
static void dm_test_update_subconnector_dp_no_sink(struct kunit *test)
{
	struct device *dev;
	struct drm_device *drm;
	struct amdgpu_dm_connector *aconnector;
	struct dc_link *link;
	uint64_t val = 0;

	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	drm = __drm_kunit_helper_alloc_drm_device(test, dev,
						   sizeof(*drm), 0,
						   DRIVER_MODESET);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, drm);

	aconnector = drmm_kzalloc(drm, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);
	link = drmm_kzalloc(drm, sizeof(*link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, link);

	drmm_connector_init(drm, &aconnector->base, &dm_test_connector_funcs,
			    DRM_MODE_CONNECTOR_DisplayPort, NULL);
	drm_connector_attach_dp_subconnector_property(&aconnector->base);

	/* Dongle type is set, but no sink means it must not be consulted */
	link->dpcd_caps.dongle_type = DISPLAY_DONGLE_DP_HDMI_CONVERTER;
	aconnector->dc_link = link;
	aconnector->dc_sink = NULL;

	update_subconnector_property(aconnector);

	KUNIT_EXPECT_EQ(test, drm_object_property_get_default_value(&aconnector->base.base,
				aconnector->base.dev->mode_config.dp_subconnector_property,
				&val), 0);
	KUNIT_EXPECT_EQ(test, (int)val, (int)DRM_MODE_SUBCONNECTOR_Unknown);
}

/**
 * dm_test_update_subconnector_non_dp_noop - Test non-DisplayPort connector is
 * left untouched (early return)
 * @test: The KUnit test context
 */
static void dm_test_update_subconnector_non_dp_noop(struct kunit *test)
{
	struct device *dev;
	struct drm_device *drm;
	struct amdgpu_dm_connector *aconnector;
	struct dc_link *link;
	uint64_t val = 0;

	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	drm = __drm_kunit_helper_alloc_drm_device(test, dev,
						   sizeof(*drm), 0,
						   DRIVER_MODESET);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, drm);

	aconnector = drmm_kzalloc(drm, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);
	link = drmm_kzalloc(drm, sizeof(*link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, link);

	drmm_connector_init(drm, &aconnector->base, &dm_test_connector_funcs,
			    DRM_MODE_CONNECTOR_HDMIA, NULL);
	drm_connector_attach_dp_subconnector_property(&aconnector->base);

	/* Pre-seed the property to a non-default value */
	drm_object_property_set_value(&aconnector->base.base,
			aconnector->base.dev->mode_config.dp_subconnector_property,
			DRM_MODE_SUBCONNECTOR_VGA);

	link->dpcd_caps.dongle_type = DISPLAY_DONGLE_DP_HDMI_CONVERTER;
	aconnector->dc_link = link;
	aconnector->dc_sink = drmm_kzalloc(drm, sizeof(*aconnector->dc_sink), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector->dc_sink);

	update_subconnector_property(aconnector);

	/* Non-DP connector: value must remain what we seeded */
	KUNIT_EXPECT_EQ(test, drm_object_property_get_default_value(&aconnector->base.base,
				aconnector->base.dev->mode_config.dp_subconnector_property,
				&val), 0);
	KUNIT_EXPECT_EQ(test, (int)val, (int)DRM_MODE_SUBCONNECTOR_VGA);
}

/* Tests for amdgpu_dm_fbc_init() */

/*
 * Build an amdgpu_dm_connector wired to a kunit-allocated amdgpu_device so
 * that drm_to_adev() and to_amdgpu_dm_connector() resolve correctly, with a
 * dc, dc_link and an empty modes list ready for amdgpu_dm_fbc_init().
 */
struct dm_test_fbc_ctx {
	struct amdgpu_device *adev;
	struct amdgpu_dm_connector *aconnector;
	struct dc *dc;
	struct dc_link *link;
};

static struct dm_test_fbc_ctx *dm_test_fbc_ctx_alloc(struct kunit *test)
{
	struct dm_test_fbc_ctx *ctx;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	ctx->adev = kunit_kzalloc(test, sizeof(*ctx->adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->adev);
	ctx->aconnector = kunit_kzalloc(test, sizeof(*ctx->aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->aconnector);
	ctx->dc = kunit_kzalloc(test, sizeof(*ctx->dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->dc);
	ctx->link = kunit_kzalloc(test, sizeof(*ctx->link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->link);

	ctx->aconnector->base.dev = &ctx->adev->ddev;
	INIT_LIST_HEAD(&ctx->aconnector->base.modes);
	ctx->adev->dm.dc = ctx->dc;
	ctx->aconnector->dc_link = ctx->link;

	/* Default to the fully-enabled path so each test only flips one knob */
	ctx->link->connector_signal = SIGNAL_TYPE_EDP;
	ctx->dc->fbc_compressor =
		(struct compressor *)kunit_kzalloc(test, sizeof(void *), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->dc->fbc_compressor);

	return ctx;
}

/**
 * dm_test_fbc_init_no_compressor - Test fbc_init is a no-op without a compressor
 * @test: The KUnit test context
 */
static void dm_test_fbc_init_no_compressor(struct kunit *test)
{
	struct dm_test_fbc_ctx *ctx = dm_test_fbc_ctx_alloc(test);

	ctx->dc->fbc_compressor = NULL;

	amdgpu_dm_fbc_init(&ctx->aconnector->base);

	KUNIT_EXPECT_NULL(test, ctx->adev->dm.compressor.bo_ptr);
}

/**
 * dm_test_fbc_init_non_edp - Test fbc_init is a no-op for non-eDP links
 * @test: The KUnit test context
 */
static void dm_test_fbc_init_non_edp(struct kunit *test)
{
	struct dm_test_fbc_ctx *ctx = dm_test_fbc_ctx_alloc(test);

	ctx->link->connector_signal = SIGNAL_TYPE_DISPLAY_PORT;

	amdgpu_dm_fbc_init(&ctx->aconnector->base);

	KUNIT_EXPECT_NULL(test, ctx->adev->dm.compressor.bo_ptr);
}

/**
 * dm_test_fbc_init_already_allocated - Test fbc_init keeps an existing buffer
 * @test: The KUnit test context
 */
static void dm_test_fbc_init_already_allocated(struct kunit *test)
{
	struct dm_test_fbc_ctx *ctx = dm_test_fbc_ctx_alloc(test);
	struct amdgpu_bo *existing;

	existing = kunit_kzalloc(test, sizeof(void *), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, existing);
	ctx->adev->dm.compressor.bo_ptr = existing;

	amdgpu_dm_fbc_init(&ctx->aconnector->base);

	/* Buffer already present → left untouched, no reallocation */
	KUNIT_EXPECT_PTR_EQ(test, ctx->adev->dm.compressor.bo_ptr, existing);
}

/**
 * dm_test_fbc_init_no_modes - Test fbc_init skips allocation with no modes
 * @test: The KUnit test context
 */
static void dm_test_fbc_init_no_modes(struct kunit *test)
{
	struct dm_test_fbc_ctx *ctx = dm_test_fbc_ctx_alloc(test);

	/* All prerequisites met but the modes list is empty → max_size 0 */
	amdgpu_dm_fbc_init(&ctx->aconnector->base);

	KUNIT_EXPECT_NULL(test, ctx->adev->dm.compressor.bo_ptr);
}

/* Tests for amdgpu_dm_detect_mst_link_for_all_connectors() */

/* Allocate a bare drm_device suitable for registering connectors against. */
static struct drm_device *dm_test_alloc_drm(struct kunit *test)
{
	struct device *dev;
	struct drm_device *drm;

	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	drm = __drm_kunit_helper_alloc_drm_device(test, dev, sizeof(*drm), 0,
						  DRIVER_MODESET);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, drm);

	return drm;
}

/*
 * Allocate an amdgpu_dm_connector and register its embedded drm_connector with
 * @drm so that drm_for_each_connector_iter() and to_amdgpu_dm_connector() both
 * resolve to it.
 */
static struct amdgpu_dm_connector *dm_test_add_connector(struct kunit *test,
		struct drm_device *drm, int connector_type)
{
	struct amdgpu_dm_connector *aconnector;

	aconnector = drmm_kzalloc(drm, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);

	KUNIT_ASSERT_EQ(test,
		drmm_connector_init(drm, &aconnector->base,
				    &dm_test_connector_funcs, connector_type,
				    NULL), 0);

	return aconnector;
}

/**
 * dm_test_detect_mst_no_connectors - Test the no-op path on an empty device
 * @test: The KUnit test context
 */
static void dm_test_detect_mst_no_connectors(struct kunit *test)
{
	struct drm_device *drm = dm_test_alloc_drm(test);

	/* No connectors registered → iteration body never runs */
	KUNIT_EXPECT_EQ(test,
		amdgpu_dm_detect_mst_link_for_all_connectors(drm), 0);
}

/**
 * dm_test_detect_mst_skips_writeback - Test writeback connectors are skipped
 * @test: The KUnit test context
 *
 * A writeback connector is hit by the early ``continue`` before its dc_link is
 * ever dereferenced, so leaving dc_link NULL must not crash.
 */
static void dm_test_detect_mst_skips_writeback(struct kunit *test)
{
	struct drm_device *drm = dm_test_alloc_drm(test);
	struct amdgpu_dm_connector *aconnector;

	aconnector = dm_test_add_connector(test, drm,
					   DRM_MODE_CONNECTOR_WRITEBACK);
	/* dc_link intentionally left NULL: it must not be touched */
	aconnector->dc_link = NULL;

	KUNIT_EXPECT_EQ(test,
		amdgpu_dm_detect_mst_link_for_all_connectors(drm), 0);
}

/**
 * dm_test_detect_mst_non_mst_link - Test a non-MST link starts no topology
 * @test: The KUnit test context
 */
static void dm_test_detect_mst_non_mst_link(struct kunit *test)
{
	struct drm_device *drm = dm_test_alloc_drm(test);
	struct amdgpu_dm_connector *aconnector;
	struct dc_link *link;

	aconnector = dm_test_add_connector(test, drm,
					   DRM_MODE_CONNECTOR_DisplayPort);
	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, link);

	/* Not an MST branch → the topology manager is never started */
	link->type = dc_connection_single;
	aconnector->dc_link = link;

	KUNIT_EXPECT_EQ(test,
		amdgpu_dm_detect_mst_link_for_all_connectors(drm), 0);
}

/**
 * dm_test_detect_mst_branch_without_aux - Test an MST branch with no aux is
 * skipped
 * @test: The KUnit test context
 *
 * The condition short-circuits on a NULL mst_mgr.aux, so the real
 * drm_dp_mst_topology_mgr_set_mst() path is never reached.
 */
static void dm_test_detect_mst_branch_without_aux(struct kunit *test)
{
	struct drm_device *drm = dm_test_alloc_drm(test);
	struct amdgpu_dm_connector *aconnector;
	struct dc_link *link;

	aconnector = dm_test_add_connector(test, drm,
					   DRM_MODE_CONNECTOR_DisplayPort);
	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, link);

	link->type = dc_connection_mst_branch;
	aconnector->dc_link = link;
	/* mst_mgr.aux is NULL (kzalloc) → second half of the && is false */
	KUNIT_ASSERT_NULL(test, aconnector->mst_mgr.aux);

	KUNIT_EXPECT_EQ(test,
		amdgpu_dm_detect_mst_link_for_all_connectors(drm), 0);
}

/**
 * dm_test_detect_mst_start_fail - Test the MST-start failure path
 * @test: The KUnit test context
 *
 * An MST branch whose topology manager fails to start must downgrade the link
 * to a single connection and attempt to stop the topology manager. The failure
 * is forced by giving the manager a powered-down aux, so its initial DPCD read
 * returns an error before any real transfer is attempted.
 */
static void dm_test_detect_mst_start_fail(struct kunit *test)
{
	struct drm_device *drm = dm_test_alloc_drm(test);
	struct amdgpu_dm_connector *aconnector;
	struct dc_link *link;
	struct drm_dp_aux *aux;

	aconnector = dm_test_add_connector(test, drm,
					   DRM_MODE_CONNECTOR_DisplayPort);
	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, link);
	aux = kunit_kzalloc(test, sizeof(*aux), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aux);

	link->type = dc_connection_mst_branch;
	aconnector->dc_link = link;

	/*
	 * A powered-down aux makes the initial DPCD read return -EBUSY before
	 * any transfer, so drm_dp_mst_topology_mgr_set_mst() fails fast.
	 */
	mutex_init(&aux->hw_mutex);
	aux->name = "kunit-mst-aux";
	aux->powered_down = true;
	mutex_init(&aconnector->mst_mgr.lock);
	aconnector->mst_mgr.dev = drm;
	aconnector->mst_mgr.aux = aux;

	/*
	 * link->priv is left NULL so dm_helpers_dp_mst_stop_top_mgr() returns
	 * early without recursing back into the topology manager.
	 */
	KUNIT_EXPECT_EQ(test,
		amdgpu_dm_detect_mst_link_for_all_connectors(drm), 0);

	/* The failure path downgrades the link to a single connection. */
	KUNIT_EXPECT_EQ(test, link->type, dc_connection_single);
}

/* Tests for amdgpu_dm_find_first_crtc_matching_connector() */

/*
 * Build a minimal drm_atomic_commit holding @count connector slots. The
 * function under test only reads num_connector, connectors[i].ptr and
 * connectors[i].new_state, so a hand-rolled state is sufficient.
 */
static struct drm_atomic_commit *
dm_test_alloc_atomic_state(struct kunit *test, int count)
{
	struct drm_atomic_commit *state;

	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state);

	state->num_connector = count;
	if (count) {
		state->connectors = kunit_kcalloc(test, count,
						  sizeof(*state->connectors),
						  GFP_KERNEL);
		KUNIT_ASSERT_NOT_NULL(test, state->connectors);
	}

	return state;
}

/**
 * dm_test_find_first_crtc_match - Test find_first_crtc returns matching connector
 * @test: The KUnit test context
 */
static void dm_test_find_first_crtc_match(struct kunit *test)
{
	struct drm_atomic_commit *state = dm_test_alloc_atomic_state(test, 1);
	struct drm_connector *connector;
	struct drm_connector_state *con_state;
	struct drm_crtc *crtc;

	connector = kunit_kzalloc(test, sizeof(*connector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, connector);
	con_state = kunit_kzalloc(test, sizeof(*con_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, con_state);
	crtc = kunit_kzalloc(test, sizeof(*crtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, crtc);

	con_state->crtc = crtc;
	state->connectors[0].ptr = connector;
	state->connectors[0].new_state = con_state;

	KUNIT_EXPECT_PTR_EQ(test,
		amdgpu_dm_find_first_crtc_matching_connector(state, crtc),
		connector);
}

/**
 * dm_test_find_first_crtc_no_match - Test find_first_crtc returns NULL when no crtc matches
 * @test: The KUnit test context
 */
static void dm_test_find_first_crtc_no_match(struct kunit *test)
{
	struct drm_atomic_commit *state = dm_test_alloc_atomic_state(test, 1);
	struct drm_connector *connector;
	struct drm_connector_state *con_state;
	struct drm_crtc *crtc;
	struct drm_crtc *other_crtc;

	connector = kunit_kzalloc(test, sizeof(*connector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, connector);
	con_state = kunit_kzalloc(test, sizeof(*con_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, con_state);
	crtc = kunit_kzalloc(test, sizeof(*crtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, crtc);
	other_crtc = kunit_kzalloc(test, sizeof(*other_crtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, other_crtc);

	con_state->crtc = other_crtc;
	state->connectors[0].ptr = connector;
	state->connectors[0].new_state = con_state;

	KUNIT_EXPECT_NULL(test,
		amdgpu_dm_find_first_crtc_matching_connector(state, crtc));
}

/**
 * dm_test_find_first_crtc_empty_state - Test find_first_crtc returns NULL with no connectors
 * @test: The KUnit test context
 */
static void dm_test_find_first_crtc_empty_state(struct kunit *test)
{
	struct drm_atomic_commit *state = dm_test_alloc_atomic_state(test, 0);
	struct drm_crtc *crtc;

	crtc = kunit_kzalloc(test, sizeof(*crtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, crtc);

	KUNIT_EXPECT_NULL(test,
		amdgpu_dm_find_first_crtc_matching_connector(state, crtc));
}

/**
 * dm_test_find_first_crtc_skips_null_ptr - Test find_first_crtc skips empty connector slots
 * @test: The KUnit test context
 */
static void dm_test_find_first_crtc_skips_null_ptr(struct kunit *test)
{
	struct drm_atomic_commit *state = dm_test_alloc_atomic_state(test, 2);
	struct drm_connector *connector;
	struct drm_connector_state *con_state;
	struct drm_crtc *crtc;

	connector = kunit_kzalloc(test, sizeof(*connector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, connector);
	con_state = kunit_kzalloc(test, sizeof(*con_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, con_state);
	crtc = kunit_kzalloc(test, sizeof(*crtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, crtc);

	/* Slot 0 has no connector (ptr == NULL) and must be skipped. */
	con_state->crtc = crtc;
	state->connectors[1].ptr = connector;
	state->connectors[1].new_state = con_state;

	KUNIT_EXPECT_PTR_EQ(test,
		amdgpu_dm_find_first_crtc_matching_connector(state, crtc),
		connector);
}

/**
 * dm_test_find_first_crtc_returns_first - Test find_first_crtc returns the first match
 * @test: The KUnit test context
 */
static void dm_test_find_first_crtc_returns_first(struct kunit *test)
{
	struct drm_atomic_commit *state = dm_test_alloc_atomic_state(test, 2);
	struct drm_connector *first;
	struct drm_connector *second;
	struct drm_connector_state *first_state;
	struct drm_connector_state *second_state;
	struct drm_crtc *crtc;

	first = kunit_kzalloc(test, sizeof(*first), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, first);
	second = kunit_kzalloc(test, sizeof(*second), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, second);
	first_state = kunit_kzalloc(test, sizeof(*first_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, first_state);
	second_state = kunit_kzalloc(test, sizeof(*second_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, second_state);
	crtc = kunit_kzalloc(test, sizeof(*crtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, crtc);

	/* Both connectors target the same crtc; the first one must win. */
	first_state->crtc = crtc;
	second_state->crtc = crtc;
	state->connectors[0].ptr = first;
	state->connectors[0].new_state = first_state;
	state->connectors[1].ptr = second;
	state->connectors[1].new_state = second_state;

	KUNIT_EXPECT_PTR_EQ(test,
		amdgpu_dm_find_first_crtc_matching_connector(state, crtc),
		first);
}

/* Tests for amdgpu_dm_set_panel_type() */

/*
 * Build an amdgpu_dm_connector registered against a real kunit drm_device that
 * is embedded in an amdgpu_device, so drm_to_adev()/adev_to_drm() resolve and
 * the panel_type property can be created, attached and updated for real.
 */
struct dm_test_panel_ctx {
	struct amdgpu_device *adev;
	struct drm_device *drm;
	struct amdgpu_dm_connector *aconnector;
	struct dc_link *link;
	struct drm_display_info *display_info;
};

static struct dm_test_panel_ctx *dm_test_panel_ctx_alloc(struct kunit *test)
{
	struct dm_test_panel_ctx *ctx;
	struct drm_property *prop;
	struct device *dev;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	ctx->drm = __drm_kunit_helper_alloc_drm_device(test, dev,
						       sizeof(*ctx->adev),
						       offsetof(struct amdgpu_device, ddev),
						       DRIVER_MODESET);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx->drm);
	ctx->adev = drm_to_adev(ctx->drm);

	/* The function under test writes through this property. */
	prop = drm_property_create_range(ctx->drm, DRM_MODE_PROP_IMMUTABLE,
					 "panel_type", 0, 0xff);
	KUNIT_ASSERT_NOT_NULL(test, prop);
	ctx->drm->mode_config.panel_type_property = prop;

	ctx->aconnector = drmm_kzalloc(ctx->drm, sizeof(*ctx->aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->aconnector);
	KUNIT_ASSERT_EQ(test,
		drmm_connector_init(ctx->drm, &ctx->aconnector->base,
				    &dm_test_connector_funcs,
				    DRM_MODE_CONNECTOR_eDP, NULL), 0);
	drm_object_attach_property(&ctx->aconnector->base.base, prop,
				   DRM_MODE_PANEL_TYPE_UNKNOWN);

	ctx->link = drmm_kzalloc(ctx->drm, sizeof(*ctx->link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->link);
	ctx->aconnector->dc_link = ctx->link;

	ctx->display_info = &ctx->aconnector->base.display_info;

	return ctx;
}

static uint64_t dm_test_panel_prop_value(struct kunit *test,
					 struct dm_test_panel_ctx *ctx)
{
	uint64_t val = ~0ULL;

	KUNIT_EXPECT_EQ(test,
		drm_object_property_get_default_value(&ctx->aconnector->base.base,
			ctx->drm->mode_config.panel_type_property, &val), 0);
	return val;
}

/**
 * dm_test_set_panel_type_samsung_miniled - Test Samsung luminance heuristic
 * @test: The KUnit test context
 *
 * When no VSDB or DPCD hint is present, a Samsung sink whose first luminance
 * range is at least 1.5x the second is treated as mini-LED.
 */
static void dm_test_set_panel_type_samsung_miniled(struct kunit *test)
{
	struct dm_test_panel_ctx *ctx = dm_test_panel_ctx_alloc(test);
	struct dc_sink *sink;

	sink = kunit_kzalloc(test, sizeof(*sink), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, sink);
	sink->edid_caps.manufacturer_id = DDC_MANUFACTURERNAME_SAMSUNG;
	ctx->link->local_sink = sink;

	ctx->display_info->amd_vsdb.version = 1;
	ctx->display_info->amd_vsdb.luminance_range1.max_luminance = 1500;
	ctx->display_info->amd_vsdb.luminance_range2.max_luminance = 1000;

	amdgpu_dm_set_panel_type(ctx->aconnector);

	KUNIT_EXPECT_EQ(test, (int)ctx->link->panel_type, (int)PANEL_TYPE_MINILED);
}

/**
 * dm_test_set_panel_type_samsung_below_threshold - Test Samsung sink below the
 * mini-LED luminance threshold falls back to LCD
 * @test: The KUnit test context
 */
static void dm_test_set_panel_type_samsung_below_threshold(struct kunit *test)
{
	struct dm_test_panel_ctx *ctx = dm_test_panel_ctx_alloc(test);
	struct dc_sink *sink;

	sink = kunit_kzalloc(test, sizeof(*sink), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, sink);
	sink->edid_caps.manufacturer_id = DDC_MANUFACTURERNAME_SAMSUNG;
	ctx->link->local_sink = sink;

	ctx->display_info->amd_vsdb.version = 1;
	ctx->display_info->amd_vsdb.luminance_range1.max_luminance = 1000;
	ctx->display_info->amd_vsdb.luminance_range2.max_luminance = 1000;

	amdgpu_dm_set_panel_type(ctx->aconnector);

	KUNIT_EXPECT_EQ(test, (int)ctx->link->panel_type, (int)PANEL_TYPE_LCD);
}

/**
 * dm_test_set_panel_type_default_lcd - Test default fallback is LCD
 * @test: The KUnit test context
 *
 * With no VSDB, DPCD or DID hints the panel type defaults to LCD.
 */
static void dm_test_set_panel_type_default_lcd(struct kunit *test)
{
	struct dm_test_panel_ctx *ctx = dm_test_panel_ctx_alloc(test);

	amdgpu_dm_set_panel_type(ctx->aconnector);

	KUNIT_EXPECT_EQ(test, (int)ctx->link->panel_type, (int)PANEL_TYPE_LCD);
	KUNIT_EXPECT_EQ(test, dm_test_panel_prop_value(test, ctx),
			(uint64_t)DRM_MODE_PANEL_TYPE_LCD);
}

/* Tests for amdgpu_dm_update_cacp_caps() */

/*
 * Build an amdgpu_dm_connector wired to a real kunit drm_device embedded in an
 * amdgpu_device, so drm_to_adev() resolves and drm_dbg_kms() has a valid
 * device. Defaults are seeded to the fully-supported configuration so each
 * test only flips a single knob.
 */
struct dm_test_cacp_ctx {
	struct amdgpu_device *adev;
	struct amdgpu_dm_connector *aconnector;
	struct dc_link *link;
};

static struct dm_test_cacp_ctx *dm_test_cacp_ctx_alloc(struct kunit *test)
{
	struct dm_test_cacp_ctx *ctx;
	struct drm_device *drm;
	struct device *dev;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	drm = __drm_kunit_helper_alloc_drm_device(test, dev,
						  sizeof(*ctx->adev),
						  offsetof(struct amdgpu_device, ddev),
						  DRIVER_MODESET);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, drm);
	ctx->adev = drm_to_adev(drm);

	ctx->aconnector = kunit_kzalloc(test, sizeof(*ctx->aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->aconnector);
	ctx->aconnector->base.dev = drm;

	ctx->link = kunit_kzalloc(test, sizeof(*ctx->link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->link);
	ctx->aconnector->dc_link = ctx->link;

	/* Fully-supported defaults: new enough DCE, eDP, non-LCD panel. */
	ctx->adev->ip_versions[DCE_HWIP][0] = IP_VERSION(3, 1, 4);
	ctx->link->connector_signal = SIGNAL_TYPE_EDP;
	ctx->link->panel_type = PANEL_TYPE_OLED;

	return ctx;
}

/**
 * dm_test_cacp_caps_edp_supported - Test CACP supported on a new eDP panel
 * @test: The KUnit test context
 */
static void dm_test_cacp_caps_edp_supported(struct kunit *test)
{
	struct dm_test_cacp_ctx *ctx = dm_test_cacp_ctx_alloc(test);

	amdgpu_dm_update_cacp_caps(ctx->aconnector);

	KUNIT_EXPECT_TRUE(test, ctx->link->panel_config.cacp.cacp_supported);
}

/**
 * dm_test_cacp_caps_lvds_supported - Test CACP supported on an LVDS panel
 * @test: The KUnit test context
 */
static void dm_test_cacp_caps_lvds_supported(struct kunit *test)
{
	struct dm_test_cacp_ctx *ctx = dm_test_cacp_ctx_alloc(test);

	ctx->link->connector_signal = SIGNAL_TYPE_LVDS;

	amdgpu_dm_update_cacp_caps(ctx->aconnector);

	KUNIT_EXPECT_TRUE(test, ctx->link->panel_config.cacp.cacp_supported);
}

/**
 * dm_test_cacp_caps_old_ip_unsupported - Test CACP unsupported on old DCE
 * @test: The KUnit test context
 *
 * DCE versions older than 3.1.4 do not support CACP.
 */
static void dm_test_cacp_caps_old_ip_unsupported(struct kunit *test)
{
	struct dm_test_cacp_ctx *ctx = dm_test_cacp_ctx_alloc(test);

	ctx->adev->ip_versions[DCE_HWIP][0] = IP_VERSION(3, 1, 3);

	amdgpu_dm_update_cacp_caps(ctx->aconnector);

	KUNIT_EXPECT_FALSE(test, ctx->link->panel_config.cacp.cacp_supported);
}

/**
 * dm_test_cacp_caps_ip_3_1_6_unsupported - Test CACP unsupported on DCE 3.1.6
 * @test: The KUnit test context
 *
 * DCE 3.1.6 is explicitly excluded even though it is newer than 3.1.4.
 */
static void dm_test_cacp_caps_ip_3_1_6_unsupported(struct kunit *test)
{
	struct dm_test_cacp_ctx *ctx = dm_test_cacp_ctx_alloc(test);

	ctx->adev->ip_versions[DCE_HWIP][0] = IP_VERSION(3, 1, 6);

	amdgpu_dm_update_cacp_caps(ctx->aconnector);

	KUNIT_EXPECT_FALSE(test, ctx->link->panel_config.cacp.cacp_supported);
}

/**
 * dm_test_cacp_caps_non_edp_lvds_unsupported - Test CACP unsupported on a
 * non-eDP/LVDS signal
 * @test: The KUnit test context
 */
static void dm_test_cacp_caps_non_edp_lvds_unsupported(struct kunit *test)
{
	struct dm_test_cacp_ctx *ctx = dm_test_cacp_ctx_alloc(test);

	ctx->link->connector_signal = SIGNAL_TYPE_DISPLAY_PORT;

	amdgpu_dm_update_cacp_caps(ctx->aconnector);

	KUNIT_EXPECT_FALSE(test, ctx->link->panel_config.cacp.cacp_supported);
}

/**
 * dm_test_cacp_caps_lcd_unsupported - Test CACP unsupported on an LCD panel
 * @test: The KUnit test context
 */
static void dm_test_cacp_caps_lcd_unsupported(struct kunit *test)
{
	struct dm_test_cacp_ctx *ctx = dm_test_cacp_ctx_alloc(test);

	ctx->link->panel_type = PANEL_TYPE_LCD;

	amdgpu_dm_update_cacp_caps(ctx->aconnector);

	KUNIT_EXPECT_FALSE(test, ctx->link->panel_config.cacp.cacp_supported);
}

/* Tests for fill_stream_properties_from_drm_display_mode() */

/*
 * Build the inputs for fill_stream_properties_from_drm_display_mode(). The
 * connector is registered against a real kunit drm_device so that
 * to_amdgpu_dm_connector(), connector->display_info and the drm debug helpers
 * all resolve. Large structs are heap-allocated to keep the stack small.
 *
 * The stream signal defaults to DisplayPort so the HDMI infoframe paths are
 * skipped, keeping the exercised behaviour deterministic.
 */
struct dm_test_fill_ctx {
	struct drm_device *drm;
	struct amdgpu_dm_connector *aconnector;
	struct drm_connector_state *conn_state;
	struct dc_stream_state *stream;
	struct drm_display_mode *mode;
};

static struct dm_test_fill_ctx *dm_test_fill_ctx_alloc(struct kunit *test)
{
	struct dm_test_fill_ctx *ctx;
	struct device *dev;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	ctx->drm = __drm_kunit_helper_alloc_drm_device(test, dev,
						       sizeof(*ctx->drm), 0,
						       DRIVER_MODESET);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx->drm);

	ctx->aconnector = drmm_kzalloc(ctx->drm, sizeof(*ctx->aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->aconnector);
	KUNIT_ASSERT_EQ(test,
		drmm_connector_init(ctx->drm, &ctx->aconnector->base,
				    &dm_test_connector_funcs,
				    DRM_MODE_CONNECTOR_DisplayPort, NULL), 0);

	ctx->conn_state = drmm_kzalloc(ctx->drm, sizeof(*ctx->conn_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->conn_state);
	ctx->conn_state->connector = &ctx->aconnector->base;
	ctx->stream = kunit_kzalloc(test, sizeof(*ctx->stream), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->stream);
	ctx->mode = kunit_kzalloc(test, sizeof(*ctx->mode), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->mode);

	ctx->stream->signal = SIGNAL_TYPE_DISPLAY_PORT;

	return ctx;
}

/**
 * dm_test_fill_stream_borders_zeroed - Test the timing borders are cleared
 * @test: The KUnit test context
 */
static void dm_test_fill_stream_borders_zeroed(struct kunit *test)
{
	struct dm_test_fill_ctx *ctx = dm_test_fill_ctx_alloc(test);
	struct dc_crtc_timing *timing = &ctx->stream->timing;

	/* Pre-seed nonzero borders to prove they get reset. */
	timing->h_border_left = 5;
	timing->h_border_right = 6;
	timing->v_border_top = 7;
	timing->v_border_bottom = 8;

	fill_stream_properties_from_drm_display_mode(ctx->stream, ctx->mode,
		&ctx->aconnector->base, ctx->conn_state, NULL, 8,
		PIXEL_ENCODING_RGB, false);

	KUNIT_EXPECT_EQ(test, (int)timing->h_border_left, 0);
	KUNIT_EXPECT_EQ(test, (int)timing->h_border_right, 0);
	KUNIT_EXPECT_EQ(test, (int)timing->v_border_top, 0);
	KUNIT_EXPECT_EQ(test, (int)timing->v_border_bottom, 0);
}

/**
 * dm_test_fill_stream_rgb_defaults - Test the default RGB/sRGB output
 * @test: The KUnit test context
 *
 * A plain DisplayPort sink with no YCbCr color formats produces RGB encoding
 * and the sRGB color space, with a predefined sRGB transfer function.
 */
static void dm_test_fill_stream_rgb_defaults(struct kunit *test)
{
	struct dm_test_fill_ctx *ctx = dm_test_fill_ctx_alloc(test);
	struct dc_crtc_timing *timing = &ctx->stream->timing;

	fill_stream_properties_from_drm_display_mode(ctx->stream, ctx->mode,
		&ctx->aconnector->base, ctx->conn_state, NULL, 8,
		PIXEL_ENCODING_RGB, false);

	KUNIT_EXPECT_EQ(test, (int)timing->pixel_encoding, (int)PIXEL_ENCODING_RGB);
	KUNIT_EXPECT_EQ(test, (int)timing->timing_3d_format,
			(int)TIMING_3D_FORMAT_NONE);
	KUNIT_EXPECT_EQ(test, (int)timing->scan_type, (int)SCANNING_TYPE_NODATA);
	KUNIT_EXPECT_EQ(test, (int)ctx->stream->output_color_space,
			(int)COLOR_SPACE_SRGB);
	KUNIT_EXPECT_EQ(test, (int)ctx->stream->out_transfer_func.type,
			(int)TF_TYPE_PREDEFINED);
	KUNIT_EXPECT_EQ(test, (int)ctx->stream->out_transfer_func.tf,
			(int)TRANSFER_FUNCTION_SRGB);
}

/**
 * dm_test_fill_stream_sync_polarity_positive - Test sync polarity from mode flags
 * @test: The KUnit test context
 */
static void dm_test_fill_stream_sync_polarity_positive(struct kunit *test)
{
	struct dm_test_fill_ctx *ctx = dm_test_fill_ctx_alloc(test);
	struct dc_crtc_timing *timing = &ctx->stream->timing;

	ctx->mode->flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC;

	fill_stream_properties_from_drm_display_mode(ctx->stream, ctx->mode,
		&ctx->aconnector->base, ctx->conn_state, NULL, 8,
		PIXEL_ENCODING_RGB, false);

	KUNIT_EXPECT_EQ(test, (int)timing->flags.HSYNC_POSITIVE_POLARITY, 1);
	KUNIT_EXPECT_EQ(test, (int)timing->flags.VSYNC_POSITIVE_POLARITY, 1);
}

/**
 * dm_test_fill_stream_sync_polarity_negative - Test negative sync polarity default
 * @test: The KUnit test context
 */
static void dm_test_fill_stream_sync_polarity_negative(struct kunit *test)
{
	struct dm_test_fill_ctx *ctx = dm_test_fill_ctx_alloc(test);
	struct dc_crtc_timing *timing = &ctx->stream->timing;

	/* No sync flags set on the mode → polarity stays negative (0). */
	ctx->mode->flags = 0;

	fill_stream_properties_from_drm_display_mode(ctx->stream, ctx->mode,
		&ctx->aconnector->base, ctx->conn_state, NULL, 8,
		PIXEL_ENCODING_RGB, false);

	KUNIT_EXPECT_EQ(test, (int)timing->flags.HSYNC_POSITIVE_POLARITY, 0);
	KUNIT_EXPECT_EQ(test, (int)timing->flags.VSYNC_POSITIVE_POLARITY, 0);
}

/**
 * dm_test_fill_stream_inherits_old_stream - Test vic/polarity copied from old stream
 * @test: The KUnit test context
 *
 * When an old stream is supplied its vic and sync polarities are reused instead
 * of being derived from the mode.
 */
static void dm_test_fill_stream_inherits_old_stream(struct kunit *test)
{
	struct dm_test_fill_ctx *ctx = dm_test_fill_ctx_alloc(test);
	struct dc_crtc_timing *timing = &ctx->stream->timing;
	struct dc_stream_state *old_stream;

	old_stream = kunit_kzalloc(test, sizeof(*old_stream), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, old_stream);
	old_stream->timing.vic = 16;
	old_stream->timing.flags.HSYNC_POSITIVE_POLARITY = 1;
	old_stream->timing.flags.VSYNC_POSITIVE_POLARITY = 0;

	/* Mode flags would force positive polarity if the old stream were ignored. */
	ctx->mode->flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC;

	fill_stream_properties_from_drm_display_mode(ctx->stream, ctx->mode,
		&ctx->aconnector->base, ctx->conn_state, old_stream, 8,
		PIXEL_ENCODING_RGB, false);

	KUNIT_EXPECT_EQ(test, (int)timing->vic, 16);
	KUNIT_EXPECT_EQ(test, (int)timing->flags.HSYNC_POSITIVE_POLARITY, 1);
	KUNIT_EXPECT_EQ(test, (int)timing->flags.VSYNC_POSITIVE_POLARITY, 0);
}

/**
 * dm_test_fill_stream_timing_from_crtc - Test timing taken from crtc_* fields
 * @test: The KUnit test context
 *
 * Without a freesync video match the function uses the mode's crtc_* timing
 * fields and scales the pixel clock to 100Hz units.
 */
static void dm_test_fill_stream_timing_from_crtc(struct kunit *test)
{
	struct dm_test_fill_ctx *ctx = dm_test_fill_ctx_alloc(test);
	struct dc_crtc_timing *timing = &ctx->stream->timing;

	ctx->mode->crtc_hdisplay = 1920;
	ctx->mode->crtc_htotal = 2200;
	ctx->mode->crtc_hsync_start = 2008;
	ctx->mode->crtc_hsync_end = 2052;
	ctx->mode->crtc_vdisplay = 1080;
	ctx->mode->crtc_vtotal = 1125;
	ctx->mode->crtc_vsync_start = 1084;
	ctx->mode->crtc_vsync_end = 1089;
	ctx->mode->crtc_clock = 148500;

	fill_stream_properties_from_drm_display_mode(ctx->stream, ctx->mode,
		&ctx->aconnector->base, ctx->conn_state, NULL, 8,
		PIXEL_ENCODING_RGB, false);

	KUNIT_EXPECT_EQ(test, (int)timing->h_addressable, 1920);
	KUNIT_EXPECT_EQ(test, (int)timing->h_total, 2200);
	KUNIT_EXPECT_EQ(test, (int)timing->h_sync_width, 44);
	KUNIT_EXPECT_EQ(test, (int)timing->h_front_porch, 88);
	KUNIT_EXPECT_EQ(test, (int)timing->v_addressable, 1080);
	KUNIT_EXPECT_EQ(test, (int)timing->v_total, 1125);
	KUNIT_EXPECT_EQ(test, (int)timing->v_sync_width, 5);
	KUNIT_EXPECT_EQ(test, (int)timing->v_front_porch, 4);
	KUNIT_EXPECT_EQ(test, (int)timing->pix_clk_100hz, 1485000);
}

/**
 * dm_test_fill_stream_color_depth_requested_bpc - Test bpc capping
 * @test: The KUnit test context
 *
 * The requested bpc caps the display bpc and is rounded down to an even value.
 */
static void dm_test_fill_stream_color_depth_requested_bpc(struct kunit *test)
{
	struct dm_test_fill_ctx *ctx = dm_test_fill_ctx_alloc(test);
	struct dc_crtc_timing *timing = &ctx->stream->timing;

	ctx->aconnector->base.display_info.bpc = 12;

	fill_stream_properties_from_drm_display_mode(ctx->stream, ctx->mode,
		&ctx->aconnector->base, ctx->conn_state, NULL, 10,
		PIXEL_ENCODING_RGB, false);

	KUNIT_EXPECT_EQ(test, (int)timing->display_color_depth,
			(int)COLOR_DEPTH_101010);
}

/**
 * dm_test_fill_stream_content_type - Test content type forwarded from state
 * @test: The KUnit test context
 */
static void dm_test_fill_stream_content_type(struct kunit *test)
{
	struct dm_test_fill_ctx *ctx = dm_test_fill_ctx_alloc(test);

	ctx->conn_state->content_type = DRM_MODE_CONTENT_TYPE_GRAPHICS;

	fill_stream_properties_from_drm_display_mode(ctx->stream, ctx->mode,
		&ctx->aconnector->base, ctx->conn_state, NULL, 8,
		PIXEL_ENCODING_RGB, false);

	KUNIT_EXPECT_EQ(test, (int)ctx->stream->content_type,
			(int)DISPLAY_CONTENT_TYPE_GRAPHICS);
}

/**
 * dm_test_fill_stream_aspect_ratio - Test aspect ratio mapped from the mode
 * @test: The KUnit test context
 */
static void dm_test_fill_stream_aspect_ratio(struct kunit *test)
{
	struct dm_test_fill_ctx *ctx = dm_test_fill_ctx_alloc(test);
	struct dc_crtc_timing *timing = &ctx->stream->timing;

	ctx->mode->picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9;

	fill_stream_properties_from_drm_display_mode(ctx->stream, ctx->mode,
		&ctx->aconnector->base, ctx->conn_state, NULL, 8,
		PIXEL_ENCODING_RGB, false);

	KUNIT_EXPECT_EQ(test, (int)timing->aspect_ratio,
			(int)ASPECT_RATIO_16_9);
}

/**
 * dm_test_fill_stream_encoding_from_caller_ycbcr420 - Test caller-selected 420
 * @test: The KUnit test context
 *
 * The helper no longer derives the pixel encoding from the display info; it
 * applies whatever the caller selected. Passing YCbCr420 must be honoured even
 * though the DisplayPort sink advertises no YCbCr color formats.
 */
static void dm_test_fill_stream_encoding_from_caller_ycbcr420(struct kunit *test)
{
	struct dm_test_fill_ctx *ctx = dm_test_fill_ctx_alloc(test);
	struct dc_crtc_timing *timing = &ctx->stream->timing;

	fill_stream_properties_from_drm_display_mode(ctx->stream, ctx->mode,
		&ctx->aconnector->base, ctx->conn_state, NULL, 8,
		PIXEL_ENCODING_YCBCR420, false);

	KUNIT_EXPECT_EQ(test, (int)timing->pixel_encoding,
			(int)PIXEL_ENCODING_YCBCR420);
}

/**
 * dm_test_fill_stream_encoding_from_caller_ycbcr422 - Test caller-selected 422
 * @test: The KUnit test context
 *
 * A caller-selected YCbCr422 encoding is applied verbatim.
 */
static void dm_test_fill_stream_encoding_from_caller_ycbcr422(struct kunit *test)
{
	struct dm_test_fill_ctx *ctx = dm_test_fill_ctx_alloc(test);
	struct dc_crtc_timing *timing = &ctx->stream->timing;

	fill_stream_properties_from_drm_display_mode(ctx->stream, ctx->mode,
		&ctx->aconnector->base, ctx->conn_state, NULL, 8,
		PIXEL_ENCODING_YCBCR422, false);

	KUNIT_EXPECT_EQ(test, (int)timing->pixel_encoding,
			(int)PIXEL_ENCODING_YCBCR422);
}

/**
 * dm_test_fill_stream_encoding_from_caller_ycbcr444 - Test caller-selected 444
 * @test: The KUnit test context
 *
 * A caller-selected YCbCr444 encoding is applied verbatim.
 */
static void dm_test_fill_stream_encoding_from_caller_ycbcr444(struct kunit *test)
{
	struct dm_test_fill_ctx *ctx = dm_test_fill_ctx_alloc(test);
	struct dc_crtc_timing *timing = &ctx->stream->timing;

	fill_stream_properties_from_drm_display_mode(ctx->stream, ctx->mode,
		&ctx->aconnector->base, ctx->conn_state, NULL, 8,
		PIXEL_ENCODING_YCBCR444, false);

	KUNIT_EXPECT_EQ(test, (int)timing->pixel_encoding,
			(int)PIXEL_ENCODING_YCBCR444);
}

/**
 * dm_test_fill_stream_hdmi_ep_clamps_depth - Test HDMI TMDS depth clamp applied
 * @test: The KUnit test context
 *
 * With is_hdmi_ep set the colour depth is clamped to what the sink's max TMDS
 * clock allows: a 10bpc request that exceeds the limit is reduced to 8bpc.
 */
static void dm_test_fill_stream_hdmi_ep_clamps_depth(struct kunit *test)
{
	struct dm_test_fill_ctx *ctx = dm_test_fill_ctx_alloc(test);
	struct dc_crtc_timing *timing = &ctx->stream->timing;

	ctx->aconnector->base.display_info.bpc = 10;
	/* 10bpc RGB needs 185625 KHz, over the sink's 160 MHz TMDS limit. */
	ctx->aconnector->base.display_info.max_tmds_clock = 160000;
	ctx->mode->crtc_clock = 148500;

	fill_stream_properties_from_drm_display_mode(ctx->stream, ctx->mode,
		&ctx->aconnector->base, ctx->conn_state, NULL, 10,
		PIXEL_ENCODING_RGB, true);

	KUNIT_EXPECT_EQ(test, (int)timing->display_color_depth,
			(int)COLOR_DEPTH_888);
}

/**
 * dm_test_fill_stream_non_hdmi_ep_keeps_depth - Test no TMDS clamp off HDMI
 * @test: The KUnit test context
 *
 * With is_hdmi_ep clear the TMDS clamp is skipped, so the same over-limit
 * 10bpc request is left untouched. The clamp is HDMI-specific.
 */
static void dm_test_fill_stream_non_hdmi_ep_keeps_depth(struct kunit *test)
{
	struct dm_test_fill_ctx *ctx = dm_test_fill_ctx_alloc(test);
	struct dc_crtc_timing *timing = &ctx->stream->timing;

	ctx->aconnector->base.display_info.bpc = 10;
	ctx->aconnector->base.display_info.max_tmds_clock = 160000;
	ctx->mode->crtc_clock = 148500;

	fill_stream_properties_from_drm_display_mode(ctx->stream, ctx->mode,
		&ctx->aconnector->base, ctx->conn_state, NULL, 10,
		PIXEL_ENCODING_RGB, false);

	KUNIT_EXPECT_EQ(test, (int)timing->display_color_depth,
			(int)COLOR_DEPTH_101010);
}

/**
 * dm_test_fill_stream_hdmi_infoframe - Test HDMI signal fills VIC from infoframe
 * @test: The KUnit test context
 *
 * With an HDMI stream signal the AVI/vendor infoframe path runs and overwrites
 * the VIC (seeded via an old stream) with the value derived from the mode.
 */
static void dm_test_fill_stream_hdmi_infoframe(struct kunit *test)
{
	struct dm_test_fill_ctx *ctx = dm_test_fill_ctx_alloc(test);
	struct dc_crtc_timing *timing = &ctx->stream->timing;
	struct dc_stream_state *old_stream;
	struct drm_display_mode *cea;

	old_stream = kunit_kzalloc(test, sizeof(*old_stream), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, old_stream);
	old_stream->timing.vic = 99;

	/* A real CEA-16 (1080p60) mode so the AVI infoframe resolves VIC 16. */
	cea = drm_display_mode_from_cea_vic(ctx->drm, 16);
	KUNIT_ASSERT_NOT_NULL(test, cea);
	*ctx->mode = *cea;
	drm_mode_destroy(ctx->drm, cea);

	ctx->stream->signal = SIGNAL_TYPE_HDMI_TYPE_A;

	fill_stream_properties_from_drm_display_mode(ctx->stream, ctx->mode,
		&ctx->aconnector->base, ctx->conn_state, old_stream, 8,
		PIXEL_ENCODING_RGB, false);

	/* The HDMI block overwrote the old stream's VIC of 99 with the AVI code. */
	KUNIT_EXPECT_EQ(test, (int)timing->vic, 16);
}

/**
 * dm_test_fill_stream_freesync_video - Test timing taken from the mode for FS video
 * @test: The KUnit test context
 *
 * When the mode matches the connector's cached freesync base mode the timing is
 * taken from the drm mode fields (not crtc_*), including a 10x pixel clock.
 */
static void dm_test_fill_stream_freesync_video(struct kunit *test)
{
	struct dm_test_fill_ctx *ctx = dm_test_fill_ctx_alloc(test);
	struct dc_crtc_timing *timing = &ctx->stream->timing;

	/* Cached high mode == the mode passed in => freesync video mode. */
	ctx->aconnector->freesync_vid_base.clock = 148500;
	ctx->aconnector->freesync_vid_base.hdisplay = 1920;
	ctx->aconnector->freesync_vid_base.vdisplay = 1080;
	ctx->aconnector->freesync_vid_base.hsync_start = 2008;
	ctx->aconnector->freesync_vid_base.hsync_end = 2052;
	ctx->aconnector->freesync_vid_base.htotal = 2200;
	ctx->aconnector->freesync_vid_base.vsync_start = 1084;
	ctx->aconnector->freesync_vid_base.vsync_end = 1089;
	ctx->aconnector->freesync_vid_base.vtotal = 1125;

	ctx->mode->clock = 148500;
	ctx->mode->hdisplay = 1920;
	ctx->mode->vdisplay = 1080;
	ctx->mode->hsync_start = 2008;
	ctx->mode->hsync_end = 2052;
	ctx->mode->htotal = 2200;
	ctx->mode->vsync_start = 1084;
	ctx->mode->vsync_end = 1089;
	ctx->mode->vtotal = 1125;

	fill_stream_properties_from_drm_display_mode(ctx->stream, ctx->mode,
		&ctx->aconnector->base, ctx->conn_state, NULL, 8,
		PIXEL_ENCODING_RGB, false);

	KUNIT_EXPECT_EQ(test, (int)timing->h_addressable, 1920);
	KUNIT_EXPECT_EQ(test, (int)timing->v_addressable, 1080);
	KUNIT_EXPECT_EQ(test, (int)timing->pix_clk_100hz, 1485000);
}

/* Tests for create_stream_for_sink() */

/*
 * Build the inputs for create_stream_for_sink(). The connector is registered
 * against a real kunit drm_device so that to_amdgpu_dm_connector() and the drm
 * debug helpers resolve. The DC link carries a zeroed dc_context so that
 * dc_create_stream_for_sink() can allocate and construct a stream.
 *
 * By default no dc_sink is attached, so create_stream_for_sink() builds a fake
 * VIRTUAL sink. The VIRTUAL signal keeps the DSC, audio and DP/HDMI infoframe
 * paths as no-ops, making the exercised behaviour deterministic.
 */
struct dm_test_stream_ctx {
	struct drm_device *drm;
	struct amdgpu_dm_connector *aconnector;
	struct dc_context *dc_ctx;
	struct dc_link *link;
	struct dm_connector_state *dm_state;
	struct drm_display_mode *mode;
};

static struct dm_test_stream_ctx *dm_test_stream_ctx_alloc(struct kunit *test)
{
	struct dm_test_stream_ctx *ctx;
	struct device *dev;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	ctx->drm = __drm_kunit_helper_alloc_drm_device(test, dev,
						       sizeof(*ctx->drm), 0,
						       DRIVER_MODESET);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx->drm);

	ctx->aconnector = drmm_kzalloc(ctx->drm, sizeof(*ctx->aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->aconnector);
	KUNIT_ASSERT_EQ(test,
		drmm_connector_init(ctx->drm, &ctx->aconnector->base,
				    &dm_test_connector_funcs,
				    DRM_MODE_CONNECTOR_DisplayPort, NULL), 0);

	ctx->dc_ctx = kunit_kzalloc(test, sizeof(*ctx->dc_ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->dc_ctx);

	ctx->link = kunit_kzalloc(test, sizeof(*ctx->link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->link);
	ctx->link->ctx = ctx->dc_ctx;
	ctx->link->connector_signal = SIGNAL_TYPE_DISPLAY_PORT;

	ctx->aconnector->dc_link = ctx->link;
	ctx->aconnector->dc_sink = NULL;

	ctx->dm_state = kunit_kzalloc(test, sizeof(*ctx->dm_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->dm_state);
	ctx->dm_state->base.connector = &ctx->aconnector->base;
	ctx->dm_state->scaling = RMX_OFF;

	ctx->mode = kunit_kzalloc(test, sizeof(*ctx->mode), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->mode);
	ctx->mode->hdisplay = 1920;
	ctx->mode->vdisplay = 1080;
	ctx->mode->clock = 148500;

	return ctx;
}

/**
 * dm_test_create_stream_fake_sink_success - Test a stream is built from a fake sink
 * @test: The KUnit test context
 */
static void dm_test_create_stream_fake_sink_success(struct kunit *test)
{
	struct dm_test_stream_ctx *ctx = dm_test_stream_ctx_alloc(test);
	struct dc_stream_state *stream;

	stream = create_stream_for_sink(&ctx->aconnector->base, ctx->mode,
					ctx->dm_state, NULL, 8,
					PIXEL_ENCODING_RGB, false);

	KUNIT_ASSERT_NOT_NULL(test, stream);
	dc_stream_release(stream);
}

/**
 * dm_test_create_stream_sets_dm_context - Test dm_stream_context points to aconnector
 * @test: The KUnit test context
 */
static void dm_test_create_stream_sets_dm_context(struct kunit *test)
{
	struct dm_test_stream_ctx *ctx = dm_test_stream_ctx_alloc(test);
	struct dc_stream_state *stream;

	stream = create_stream_for_sink(&ctx->aconnector->base, ctx->mode,
					ctx->dm_state, NULL, 8,
					PIXEL_ENCODING_RGB, false);

	KUNIT_ASSERT_NOT_NULL(test, stream);
	KUNIT_EXPECT_PTR_EQ(test, stream->dm_stream_context, ctx->aconnector);
	dc_stream_release(stream);
}

/**
 * dm_test_create_stream_virtual_signal - Test the fake sink yields a VIRTUAL signal
 * @test: The KUnit test context
 */
static void dm_test_create_stream_virtual_signal(struct kunit *test)
{
	struct dm_test_stream_ctx *ctx = dm_test_stream_ctx_alloc(test);
	struct dc_stream_state *stream;

	stream = create_stream_for_sink(&ctx->aconnector->base, ctx->mode,
					ctx->dm_state, NULL, 8,
					PIXEL_ENCODING_RGB, false);

	KUNIT_ASSERT_NOT_NULL(test, stream);
	KUNIT_EXPECT_EQ(test, (int)stream->signal, (int)SIGNAL_TYPE_VIRTUAL);
	dc_stream_release(stream);
}

/**
 * dm_test_create_stream_scaling_src - Test the source rect follows the mode
 * @test: The KUnit test context
 *
 * With scaling off the full-screen source viewport matches the requested mode.
 */
static void dm_test_create_stream_scaling_src(struct kunit *test)
{
	struct dm_test_stream_ctx *ctx = dm_test_stream_ctx_alloc(test);
	struct dc_stream_state *stream;

	stream = create_stream_for_sink(&ctx->aconnector->base, ctx->mode,
					ctx->dm_state, NULL, 8,
					PIXEL_ENCODING_RGB, false);

	KUNIT_ASSERT_NOT_NULL(test, stream);
	KUNIT_EXPECT_EQ(test, (int)stream->src.width, 1920);
	KUNIT_EXPECT_EQ(test, (int)stream->src.height, 1080);
	dc_stream_release(stream);
}

/**
 * dm_test_create_stream_existing_sink - Test the existing-sink retain path
 * @test: The KUnit test context
 *
 * When the connector already has a dc_sink, create_stream_for_sink() reuses it
 * instead of building a fake sink.
 */
static void dm_test_create_stream_existing_sink(struct kunit *test)
{
	struct dm_test_stream_ctx *ctx = dm_test_stream_ctx_alloc(test);
	struct dc_sink_init_data sink_init = { 0 };
	struct dc_stream_state *stream;
	struct dc_sink *sink;

	sink_init.link = ctx->link;
	sink_init.sink_signal = SIGNAL_TYPE_VIRTUAL;
	sink = dc_sink_create(&sink_init);
	KUNIT_ASSERT_NOT_NULL(test, sink);
	sink->sink_signal = SIGNAL_TYPE_VIRTUAL;

	ctx->aconnector->dc_sink = sink;

	stream = create_stream_for_sink(&ctx->aconnector->base, ctx->mode,
					ctx->dm_state, NULL, 8,
					PIXEL_ENCODING_RGB, false);

	KUNIT_ASSERT_NOT_NULL(test, stream);
	KUNIT_EXPECT_PTR_EQ(test, stream->sink, sink);

	dc_stream_release(stream);
	dc_sink_release(sink);
}

/* Append a copy of the context mode to the connector's mode list. */
static void dm_test_stream_add_mode(struct kunit *test, struct dm_test_stream_ctx *ctx,
				    u32 type)
{
	struct drm_display_mode *mode = drm_mode_create(ctx->drm);

	KUNIT_ASSERT_NOT_NULL(test, mode);
	drm_mode_copy(mode, ctx->mode);
	mode->type = type;
	list_add_tail(&mode->head, &ctx->aconnector->base.modes);
}

/* Give the context mode a full set of CRTC timings. */
static void dm_test_stream_set_timings(struct dm_test_stream_ctx *ctx)
{
	ctx->mode->hsync_start = 2008;
	ctx->mode->hsync_end = 2052;
	ctx->mode->htotal = 2200;
	ctx->mode->vsync_start = 1084;
	ctx->mode->vsync_end = 1089;
	ctx->mode->vtotal = 1125;
}

/* Create a real dc_sink of @signal owned by the caller. */
static struct dc_sink *dm_test_stream_add_sink(struct kunit *test,
					       struct dm_test_stream_ctx *ctx,
					       enum signal_type signal)
{
	struct dc_sink_init_data sink_init = { 0 };
	struct dc_sink *sink;

	sink_init.link = ctx->link;
	sink_init.sink_signal = signal;
	sink = dc_sink_create(&sink_init);
	KUNIT_ASSERT_NOT_NULL(test, sink);
	sink->sink_signal = signal;
	ctx->aconnector->dc_sink = sink;

	return sink;
}

/**
 * dm_test_create_stream_preferred_mode - Test the preferred mode drives the timing
 * @test: The KUnit test context
 *
 * A preferred mode in the connector's mode list is used to recompute the CRTC
 * timings of the requested mode before the stream properties are filled in.
 */
static void dm_test_create_stream_preferred_mode(struct kunit *test)
{
	struct dm_test_stream_ctx *ctx = dm_test_stream_ctx_alloc(test);
	struct dc_stream_state *stream;

	dm_test_stream_set_timings(ctx);
	dm_test_stream_add_mode(test, ctx, DRM_MODE_TYPE_PREFERRED);

	stream = create_stream_for_sink(&ctx->aconnector->base, ctx->mode,
					ctx->dm_state, NULL, 8,
					PIXEL_ENCODING_RGB, false);

	KUNIT_ASSERT_NOT_NULL(test, stream);
	KUNIT_EXPECT_EQ(test, (int)stream->timing.h_addressable, 1920);
	KUNIT_EXPECT_EQ(test, (int)stream->timing.v_addressable, 1080);
	KUNIT_EXPECT_EQ(test, (int)stream->timing.h_total, 2200);
	KUNIT_EXPECT_EQ(test, (int)stream->timing.v_total, 1125);
	dc_stream_release(stream);
}

/**
 * dm_test_create_stream_scaled_keeps_refresh - Test scaling reuses the old timing
 * @test: The KUnit test context
 *
 * With scaling enabled and an unchanged refresh rate the stream properties are
 * filled from the old stream so the VIC and sync polarities are preserved.
 */
static void dm_test_create_stream_scaled_keeps_refresh(struct kunit *test)
{
	struct dm_test_stream_ctx *ctx = dm_test_stream_ctx_alloc(test);
	struct dc_stream_state *stream;

	dm_test_stream_set_timings(ctx);
	dm_test_stream_add_mode(test, ctx, DRM_MODE_TYPE_PREFERRED);
	ctx->dm_state->scaling = RMX_FULL;

	stream = create_stream_for_sink(&ctx->aconnector->base, ctx->mode,
					ctx->dm_state, NULL, 8,
					PIXEL_ENCODING_RGB, false);

	KUNIT_ASSERT_NOT_NULL(test, stream);
	KUNIT_EXPECT_EQ(test, (int)stream->timing.h_addressable, 1920);
	dc_stream_release(stream);
}

/**
 * dm_test_create_stream_timing_override - Test the automated test timing override
 * @test: The KUnit test context
 *
 * A connector carrying a requested timing from an automated test replaces the
 * timing derived from the mode.
 */
static void dm_test_create_stream_timing_override(struct kunit *test)
{
	struct dm_test_stream_ctx *ctx = dm_test_stream_ctx_alloc(test);
	struct dc_crtc_timing *timing;
	struct dc_stream_state *stream;

	timing = kunit_kzalloc(test, sizeof(*timing), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, timing);
	timing->h_addressable = 1280;
	timing->v_addressable = 720;
	timing->display_color_depth = COLOR_DEPTH_101010;

	ctx->aconnector->timing_changed = true;
	ctx->aconnector->timing_requested = timing;

	stream = create_stream_for_sink(&ctx->aconnector->base, ctx->mode,
					ctx->dm_state, NULL, 8,
					PIXEL_ENCODING_RGB, false);

	KUNIT_ASSERT_NOT_NULL(test, stream);
	KUNIT_EXPECT_EQ(test, (int)stream->timing.h_addressable, 1280);
	KUNIT_EXPECT_EQ(test, (int)stream->timing.display_color_depth,
			(int)COLOR_DEPTH_101010);
	dc_stream_release(stream);
}

/**
 * dm_test_create_stream_dp_vsc_colorimetry - Test the DP VSC SDP colorimetry decision
 * @test: The KUnit test context
 *
 * A DisplayPort sink on a DPCD 1.4 link advertising VSC SDP colorimetry, and
 * without the disable-colorimetry quirk, enables VSC SDP colorimetry and
 * arms the self refresh entry delay.
 */
static void dm_test_create_stream_dp_vsc_colorimetry(struct kunit *test)
{
	struct dm_test_stream_ctx *ctx = dm_test_stream_ctx_alloc(test);
	struct dc_stream_state *stream;
	struct dc_sink *sink;

	sink = dm_test_stream_add_sink(test, ctx, SIGNAL_TYPE_DISPLAY_PORT);
	ctx->link->dpcd_caps.dpcd_rev.raw = 0x14;
	ctx->link->dpcd_caps.dprx_feature.bits.VSC_SDP_COLORIMETRY_SUPPORTED = 1;

	stream = create_stream_for_sink(&ctx->aconnector->base, ctx->mode,
					ctx->dm_state, NULL, 8,
					PIXEL_ENCODING_RGB, false);

	KUNIT_ASSERT_NOT_NULL(test, stream);
	KUNIT_EXPECT_EQ(test, (int)stream->signal, (int)SIGNAL_TYPE_DISPLAY_PORT);
	KUNIT_EXPECT_TRUE(test, stream->use_vsc_sdp_for_colorimetry);
	KUNIT_EXPECT_EQ(test, ctx->aconnector->sr_skip_count, AMDGPU_DM_PSR_ENTRY_DELAY);

	dc_stream_release(stream);
	dc_sink_release(sink);
}

/**
 * dm_test_create_stream_dp_colorimetry_quirk - Test the disable colorimetry quirk
 * @test: The KUnit test context
 */
static void dm_test_create_stream_dp_colorimetry_quirk(struct kunit *test)
{
	struct dm_test_stream_ctx *ctx = dm_test_stream_ctx_alloc(test);
	struct dc_stream_state *stream;
	struct dc_sink *sink;

	sink = dm_test_stream_add_sink(test, ctx, SIGNAL_TYPE_DISPLAY_PORT);
	sink->edid_caps.panel_patch.disable_colorimetry = true;
	ctx->link->dpcd_caps.dpcd_rev.raw = 0x14;
	ctx->link->dpcd_caps.dprx_feature.bits.VSC_SDP_COLORIMETRY_SUPPORTED = 1;

	stream = create_stream_for_sink(&ctx->aconnector->base, ctx->mode,
					ctx->dm_state, NULL, 8,
					PIXEL_ENCODING_RGB, false);

	KUNIT_ASSERT_NOT_NULL(test, stream);
	KUNIT_EXPECT_FALSE(test, stream->use_vsc_sdp_for_colorimetry);

	dc_stream_release(stream);
	dc_sink_release(sink);
}

/**
 * dm_test_create_stream_hdmi_vsif - Test the HDMI vendor specific infoframe
 * @test: The KUnit test context
 *
 * An HDMI sink takes the HDMI Forum vendor specific infoframe path. A plain
 * 2D, non-HDMI-VIC timing carries no payload, so the packet stays invalid.
 */
static void dm_test_create_stream_hdmi_vsif(struct kunit *test)
{
	struct dm_test_stream_ctx *ctx = dm_test_stream_ctx_alloc(test);
	struct dc_stream_state *stream;
	struct dc_sink *sink;

	ctx->link->connector_signal = SIGNAL_TYPE_HDMI_TYPE_A;
	sink = dm_test_stream_add_sink(test, ctx, SIGNAL_TYPE_HDMI_TYPE_A);

	stream = create_stream_for_sink(&ctx->aconnector->base, ctx->mode,
					ctx->dm_state, NULL, 8,
					PIXEL_ENCODING_RGB, false);

	KUNIT_ASSERT_NOT_NULL(test, stream);
	KUNIT_EXPECT_EQ(test, (int)stream->signal, (int)SIGNAL_TYPE_HDMI_TYPE_A);
	KUNIT_EXPECT_FALSE(test, stream->vsp_infopacket.valid);

	dc_stream_release(stream);
	dc_sink_release(sink);
}

/* Tests for amdgpu_dm_connector_detect() */

/*
 * A non-DisplayPort connector keeps update_subconnector_property() a no-op and,
 * because the kunit thread is not the poll worker, the analog poll branch is
 * skipped. That leaves the forced-state and dc_sink presence branches as the
 * deterministic behaviour to exercise.
 */
static struct amdgpu_dm_connector *dm_test_detect_connector(struct kunit *test)
{
	struct drm_device *drm = dm_test_alloc_drm(test);

	return dm_test_add_connector(test, drm, DRM_MODE_CONNECTOR_HDMIA);
}

/**
 * dm_test_detect_force_on - Test DRM_FORCE_ON reports connected
 * @test: The KUnit test context
 */
static void dm_test_detect_force_on(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector = dm_test_detect_connector(test);

	aconnector->base.force = DRM_FORCE_ON;

	KUNIT_EXPECT_EQ(test,
		(int)amdgpu_dm_connector_detect(&aconnector->base, false),
		(int)connector_status_connected);
}

/**
 * dm_test_detect_force_on_digital - Test DRM_FORCE_ON_DIGITAL reports connected
 * @test: The KUnit test context
 */
static void dm_test_detect_force_on_digital(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector = dm_test_detect_connector(test);

	aconnector->base.force = DRM_FORCE_ON_DIGITAL;

	KUNIT_EXPECT_EQ(test,
		(int)amdgpu_dm_connector_detect(&aconnector->base, false),
		(int)connector_status_connected);
}

/**
 * dm_test_detect_force_off - Test DRM_FORCE_OFF reports disconnected
 * @test: The KUnit test context
 */
static void dm_test_detect_force_off(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector = dm_test_detect_connector(test);

	aconnector->base.force = DRM_FORCE_OFF;

	KUNIT_EXPECT_EQ(test,
		(int)amdgpu_dm_connector_detect(&aconnector->base, false),
		(int)connector_status_disconnected);
}

/**
 * dm_test_detect_sink_present - Test a present dc_sink reports connected
 * @test: The KUnit test context
 */
static void dm_test_detect_sink_present(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector = dm_test_detect_connector(test);
	struct dc_sink *sink;

	sink = kunit_kzalloc(test, sizeof(*sink), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, sink);

	aconnector->base.force = DRM_FORCE_UNSPECIFIED;
	aconnector->dc_sink = sink;

	KUNIT_EXPECT_EQ(test,
		(int)amdgpu_dm_connector_detect(&aconnector->base, false),
		(int)connector_status_connected);
}

/**
 * dm_test_detect_no_sink - Test a missing dc_sink reports disconnected
 * @test: The KUnit test context
 */
static void dm_test_detect_no_sink(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector = dm_test_detect_connector(test);

	aconnector->base.force = DRM_FORCE_UNSPECIFIED;
	aconnector->dc_sink = NULL;

	KUNIT_EXPECT_EQ(test,
		(int)amdgpu_dm_connector_detect(&aconnector->base, false),
		(int)connector_status_disconnected);
}

/*
 * A 256-byte EDID whose DisplayID extension carries a tiled display
 * topology block placing this connector at tile location (h=1, v=0), i.e.
 * a secondary tile. drm_edid_connector_update() parses it to set has_tile
 * and the non-origin tile location.
 */
static const u8 dm_test_detect_tile_edid[256] = {
	0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x10, 0xac, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x04, 0x80, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xc4, 0x70, 0x12, 0x18, 0x00,
	0x00, 0x12, 0x00, 0x15, 0x00, 0x11, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x8d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x90,
};

/**
 * dm_test_detect_hides_secondary_tile - Test the secondary tile is hidden
 * @test: The KUnit test context
 *
 * A sink whose panel patch requests disable_second_tile combined with a tiled
 * EDID at a non-origin tile location makes the detect path hide the secondary
 * display tile, reporting the connector as disconnected.
 */
static void dm_test_detect_hides_secondary_tile(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector = dm_test_detect_connector(test);
	struct dc_sink *sink;

	sink = kunit_kzalloc(test, sizeof(*sink), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, sink);
	sink->edid_caps.panel_patch.disable_second_tile = true;

	aconnector->base.force = DRM_FORCE_UNSPECIFIED;
	aconnector->dc_sink = sink;
	aconnector->drm_edid = drm_edid_alloc(dm_test_detect_tile_edid,
					     sizeof(dm_test_detect_tile_edid));
	KUNIT_ASSERT_NOT_NULL(test, aconnector->drm_edid);

	KUNIT_EXPECT_EQ(test,
		(int)amdgpu_dm_connector_detect(&aconnector->base, false),
		(int)connector_status_disconnected);

	drm_edid_free(aconnector->drm_edid);
}

/* Tests for amdgpu_dm_connector_poll() */

/**
 * dm_test_poll_dac_load_returns_cached - Test the DAC load detection shortcut
 * @test: The KUnit test context
 *
 * When the previous connection was established by analog DAC load detection and
 * polling is not forced, the connector is not re-detected and its cached status
 * is returned unchanged. The connector is embedded in an amdgpu_device so that
 * drm_to_adev() resolves.
 */
static void dm_test_poll_dac_load_returns_cached(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct amdgpu_dm_connector *aconnector;
	struct dc_link *link;
	struct dc_sink *local_sink;
	struct drm_device *drm;
	struct device *dev;

	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	drm = __drm_kunit_helper_alloc_drm_device(test, dev, sizeof(*adev),
						  offsetof(struct amdgpu_device, ddev),
						  DRIVER_MODESET);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, drm);
	adev = drm_to_adev(drm);

	aconnector = drmm_kzalloc(drm, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);
	KUNIT_ASSERT_EQ(test,
		drmm_connector_init(drm, &aconnector->base,
				    &dm_test_connector_funcs,
				    DRM_MODE_CONNECTOR_VGA, NULL), 0);

	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, link);
	local_sink = kunit_kzalloc(test, sizeof(*local_sink), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, local_sink);

	link->local_sink = local_sink;
	link->type = dc_connection_analog_load;
	aconnector->dc_link = link;

	/* The cached status that the shortcut must return unchanged. */
	aconnector->base.status = connector_status_connected;

	KUNIT_EXPECT_EQ(test,
		(int)amdgpu_dm_connector_poll(aconnector, false),
		(int)connector_status_connected);
}

/*
 * Fake link_service detection callbacks used by the connector poll tests.
 * dc_link_detect_connection_type() and dc_link_detect() proxy through
 * link->dc->link_srv, so faking these pointers exercises the poll paths
 * without touching real hardware.
 */
static bool dm_test_poll_detect_type_connected(struct dc_link *link,
						enum dc_connection_type *type)
{
	*type = dc_connection_single;
	return true;
}

static bool dm_test_poll_detect_type_none(struct dc_link *link,
					   enum dc_connection_type *type)
{
	*type = dc_connection_none;
	return false;
}

static bool dm_test_poll_detect_link_connected(struct dc_link *link,
					       enum dc_detect_reason reason)
{
	return true;
}

struct dm_test_poll_ctx {
	struct amdgpu_device *adev;
	struct amdgpu_dm_connector *aconnector;
	struct dc_link *link;
	struct dc *dc;
	struct dc_context *dc_ctx;
	struct link_service *link_srv;
	struct drm_device *drm;
};

/*
 * Build an amdgpu_dm_connector embedded in an amdgpu_device (so drm_to_adev()
 * resolves) with a faked dc_link->dc->link_srv. The hpd_lock and dc_lock
 * mutexes are initialised because the non-shortcut poll path takes them.
 */
static struct dm_test_poll_ctx *dm_test_poll_ctx_alloc(struct kunit *test)
{
	struct dm_test_poll_ctx *ctx;
	struct device *dev;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	ctx->drm = __drm_kunit_helper_alloc_drm_device(test, dev,
						       sizeof(*ctx->adev),
						       offsetof(struct amdgpu_device, ddev),
						       DRIVER_MODESET);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx->drm);
	ctx->adev = drm_to_adev(ctx->drm);
	mutex_init(&ctx->adev->dm.dc_lock);

	ctx->aconnector = drmm_kzalloc(ctx->drm, sizeof(*ctx->aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->aconnector);
	KUNIT_ASSERT_EQ(test,
		drmm_connector_init(ctx->drm, &ctx->aconnector->base,
				    &dm_test_connector_funcs,
				    DRM_MODE_CONNECTOR_VGA, NULL), 0);
	mutex_init(&ctx->aconnector->hpd_lock);

	ctx->dc = kunit_kzalloc(test, sizeof(*ctx->dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->dc);
	ctx->link_srv = kunit_kzalloc(test, sizeof(*ctx->link_srv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->link_srv);
	ctx->dc->link_srv = ctx->link_srv;

	ctx->dc_ctx = kunit_kzalloc(test, sizeof(*ctx->dc_ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->dc_ctx);

	ctx->link = kunit_kzalloc(test, sizeof(*ctx->link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->link);
	ctx->link->dc = ctx->dc;
	ctx->link->ctx = ctx->dc_ctx;
	ctx->aconnector->dc_link = ctx->link;

	return ctx;
}

/**
 * dm_test_poll_connected_cached_sink - Test the connected path reusing a sink
 * @test: The KUnit test context
 *
 * detect_connection_type reports a connection and a local_sink already exists,
 * so the short-circuit skips full detection and the status stays connected.
 */
static void dm_test_poll_connected_cached_sink(struct kunit *test)
{
	struct dm_test_poll_ctx *ctx = dm_test_poll_ctx_alloc(test);
	struct dc_sink *local_sink;

	ctx->link_srv->detect_connection_type = dm_test_poll_detect_type_connected;
	local_sink = kunit_kzalloc(test, sizeof(*local_sink), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, local_sink);
	ctx->link->local_sink = local_sink;
	ctx->link->type = dc_connection_single;
	ctx->aconnector->base.status = connector_status_connected;

	KUNIT_EXPECT_EQ(test,
		(int)amdgpu_dm_connector_poll(ctx->aconnector, false),
		(int)connector_status_connected);
}

/**
 * dm_test_poll_connected_new_sink - Test the connected path via full detection
 * @test: The KUnit test context
 *
 * With no cached local_sink, dc_link_detect() is consulted and reports a
 * connection, so the status becomes connected.
 */
static void dm_test_poll_connected_new_sink(struct kunit *test)
{
	struct dm_test_poll_ctx *ctx = dm_test_poll_ctx_alloc(test);

	ctx->link_srv->detect_connection_type = dm_test_poll_detect_type_connected;
	ctx->link_srv->detect_link = dm_test_poll_detect_link_connected;
	ctx->link->local_sink = NULL;
	ctx->aconnector->base.status = connector_status_connected;

	KUNIT_EXPECT_EQ(test,
		(int)amdgpu_dm_connector_poll(ctx->aconnector, false),
		(int)connector_status_connected);
}

/**
 * dm_test_poll_disconnect_releases_sink - Test the disconnect teardown path
 * @test: The KUnit test context
 *
 * detect_connection_type reports no connection while the cached status was
 * connected, so the stale local_sink is released and cleared before the
 * connector is re-evaluated.
 */
static void dm_test_poll_disconnect_releases_sink(struct kunit *test)
{
	struct dm_test_poll_ctx *ctx = dm_test_poll_ctx_alloc(test);
	struct dc_sink_init_data sink_init = { 0 };
	struct dc_sink *local_sink;

	ctx->link_srv->detect_connection_type = dm_test_poll_detect_type_none;

	sink_init.link = ctx->link;
	sink_init.sink_signal = SIGNAL_TYPE_VIRTUAL;
	local_sink = dc_sink_create(&sink_init);
	KUNIT_ASSERT_NOT_NULL(test, local_sink);
	ctx->link->local_sink = local_sink;

	ctx->aconnector->base.status = connector_status_connected;
	ctx->aconnector->dc_sink = NULL;

	KUNIT_EXPECT_EQ(test,
		(int)amdgpu_dm_connector_poll(ctx->aconnector, false),
		(int)connector_status_disconnected);
	KUNIT_EXPECT_NULL(test, ctx->link->local_sink);
}

/* Tests for amdgpu_dm_connector_late_register() and _unregister() */

/*
 * Build an amdgpu_dm_connector embedded in an amdgpu_device so drm_to_adev()
 * resolves. A VGA connector keeps amdgpu_dm_should_create_sysfs() false (sysfs
 * and DP AUX branches skipped) and bl_idx == -1 turns backlight registration
 * into a no-op, leaving the register/unregister bookkeeping safe to exercise.
 */
static struct amdgpu_dm_connector *dm_test_reg_connector(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct amdgpu_dm_connector *aconnector;
	struct drm_device *drm;
	struct device *dev;

	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	drm = __drm_kunit_helper_alloc_drm_device(test, dev, sizeof(*adev),
						  offsetof(struct amdgpu_device, ddev),
						  DRIVER_MODESET);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, drm);

	aconnector = drmm_kzalloc(drm, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);
	KUNIT_ASSERT_EQ(test,
		drmm_connector_init(drm, &aconnector->base,
				    &dm_test_connector_funcs,
				    DRM_MODE_CONNECTOR_VGA, NULL), 0);

	aconnector->bl_idx = -1;

	return aconnector;
}

/**
 * dm_test_late_register_non_dp_succeeds - Test late_register on a plain connector
 * @test: The KUnit test context
 *
 * With sysfs, backlight and DP AUX registration all skipped, late_register
 * completes successfully.
 */
static void dm_test_late_register_non_dp_succeeds(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector = dm_test_reg_connector(test);

	KUNIT_EXPECT_EQ(test,
		amdgpu_dm_connector_late_register(&aconnector->base), 0);
}

/**
 * dm_test_unregister_non_dp_noop - Test unregister tolerates an unregistered connector
 * @test: The KUnit test context
 *
 * No sysfs group was created, the CEC notifier is NULL and the DP AUX channel
 * was never registered, so unregister must be a safe no-op.
 */
static void dm_test_unregister_non_dp_noop(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector = dm_test_reg_connector(test);

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_should_create_sysfs(aconnector));

	amdgpu_dm_connector_unregister(&aconnector->base);
}

/* Tests for amdgpu_dm_connector_destroy() */

/*
 * amdgpu_dm_connector_destroy() ends with drm_connector_cleanup() followed by
 * kfree(connector), so the connector must be initialised with the unmanaged
 * drm_connector_init() and allocated with kzalloc() (the function frees it, so
 * kunit_kzalloc() would double free at teardown). It is embedded in an
 * amdgpu_device so drm_to_adev() resolves and a dc_link carries a dc_context so
 * dc_sink_create() works for the sink-release branches.
 */
struct dm_test_destroy_ctx {
	struct drm_device *drm;
	struct dc_context *dc_ctx;
	struct dc_link *link;
};

static struct dm_test_destroy_ctx *dm_test_destroy_ctx_alloc(struct kunit *test)
{
	struct dm_test_destroy_ctx *ctx;
	struct amdgpu_device *adev;
	struct device *dev;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	ctx->drm = __drm_kunit_helper_alloc_drm_device(test, dev, sizeof(*adev),
						       offsetof(struct amdgpu_device, ddev),
						       DRIVER_MODESET);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx->drm);

	ctx->dc_ctx = kunit_kzalloc(test, sizeof(*ctx->dc_ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->dc_ctx);

	ctx->link = kunit_kzalloc(test, sizeof(*ctx->link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->link);
	ctx->link->ctx = ctx->dc_ctx;

	return ctx;
}

/*
 * Allocate a connector the destroy path can free. Uses kzalloc() (not
 * kunit_kzalloc) and the unmanaged drm_connector_init() because the function
 * under test calls drm_connector_cleanup() + kfree(connector).
 *
 * drm_connector_init() requires funcs->destroy to be set, so a dedicated funcs
 * table wires it to amdgpu_dm_connector_destroy() (the test invokes it
 * directly; the connector is removed from the device before teardown).
 */
static const struct drm_connector_funcs dm_test_destroy_funcs = {
	.reset = amdgpu_dm_connector_funcs_reset,
	.atomic_duplicate_state = amdgpu_dm_connector_atomic_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.destroy = amdgpu_dm_connector_destroy,
};

static struct amdgpu_dm_connector *
dm_test_destroy_connector(struct kunit *test, struct drm_device *drm)
{
	struct amdgpu_dm_connector *aconnector;

	aconnector = kzalloc_obj(*aconnector);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);

	KUNIT_ASSERT_EQ(test,
		drm_connector_init(drm, &aconnector->base,
				   &dm_test_destroy_funcs,
				   DRM_MODE_CONNECTOR_VGA), 0);
	aconnector->bl_idx = -1;

	return aconnector;
}

/**
 * dm_test_destroy_minimal - Test destroy tears down a bare connector
 * @test: The KUnit test context
 *
 * With no MST, backlight, sinks or registered AUX/CEC, destroy must clean up
 * and free the connector without crashing.
 */
static void dm_test_destroy_minimal(struct kunit *test)
{
	struct dm_test_destroy_ctx *ctx = dm_test_destroy_ctx_alloc(test);
	struct amdgpu_dm_connector *aconnector =
		dm_test_destroy_connector(test, ctx->drm);

	amdgpu_dm_connector_destroy(&aconnector->base);
}

/**
 * dm_test_destroy_releases_dc_sink - Test destroy releases the dc_sink
 * @test: The KUnit test context
 */
static void dm_test_destroy_releases_dc_sink(struct kunit *test)
{
	struct dm_test_destroy_ctx *ctx = dm_test_destroy_ctx_alloc(test);
	struct amdgpu_dm_connector *aconnector =
		dm_test_destroy_connector(test, ctx->drm);
	struct dc_sink_init_data sink_init = { 0 };
	struct dc_sink *sink;

	sink_init.link = ctx->link;
	sink_init.sink_signal = SIGNAL_TYPE_VIRTUAL;
	sink = dc_sink_create(&sink_init);
	KUNIT_ASSERT_NOT_NULL(test, sink);

	/* Extra reference so the sink survives destroy for inspection. */
	dc_sink_retain(sink);
	aconnector->dc_sink = sink;

	amdgpu_dm_connector_destroy(&aconnector->base);

	KUNIT_EXPECT_EQ(test, (int)kref_read(&sink->refcount), 1);
	dc_sink_release(sink);
}

/**
 * dm_test_destroy_releases_dc_em_sink - Test destroy releases the emulated sink
 * @test: The KUnit test context
 */
static void dm_test_destroy_releases_dc_em_sink(struct kunit *test)
{
	struct dm_test_destroy_ctx *ctx = dm_test_destroy_ctx_alloc(test);
	struct amdgpu_dm_connector *aconnector =
		dm_test_destroy_connector(test, ctx->drm);
	struct dc_sink_init_data sink_init = { 0 };
	struct dc_sink *sink;

	sink_init.link = ctx->link;
	sink_init.sink_signal = SIGNAL_TYPE_VIRTUAL;
	sink = dc_sink_create(&sink_init);
	KUNIT_ASSERT_NOT_NULL(test, sink);

	dc_sink_retain(sink);
	aconnector->dc_em_sink = sink;

	amdgpu_dm_connector_destroy(&aconnector->base);

	KUNIT_EXPECT_EQ(test, (int)kref_read(&sink->refcount), 1);
	dc_sink_release(sink);
}

/* Tests for dm_encoder_helper_disable() */

/**
 * dm_test_encoder_disable_noop - Test the disable hook is a no-op
 * @test: The KUnit test context
 *
 * dm_encoder_helper_disable() has an empty body; calling it must neither touch
 * the encoder nor crash.
 */
static void dm_test_encoder_disable_noop(struct kunit *test)
{
	struct drm_encoder *encoder;

	encoder = kunit_kzalloc(test, sizeof(*encoder), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, encoder);

	dm_encoder_helper_disable(encoder);
}

/* Tests for dm_encoder_helper_atomic_check() */

/*
 * dm_encoder_helper_atomic_check() reads back through to_amdgpu_encoder(),
 * to_amdgpu_dm_connector() and to_dm_connector_state(), so the encoder,
 * connector and connector-state are stacked in their containers and wired
 * together through conn_state->connector.
 */
/* Tests for amdgpu_dm_encoder_init() */

/**
 * dm_test_encoder_init_success - Test encoder init wires id, crtc mask and helpers
 * @test: The KUnit test context
 *
 * On a DRM device embedded in an amdgpu_device, amdgpu_dm_encoder_init()
 * registers a TMDS encoder, derives possible_crtcs from mode_info.num_crtc,
 * records the link index as the encoder id and attaches the helper funcs.
 */
static void dm_test_encoder_init_success(struct kunit *test)
{
	struct device *dev;
	struct drm_device *drm;
	struct amdgpu_device *adev;
	struct amdgpu_encoder *aencoder;

	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);
	drm = __drm_kunit_helper_alloc_drm_device(test, dev, sizeof(*adev),
						  offsetof(struct amdgpu_device, ddev),
						  DRIVER_MODESET);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, drm);
	adev = drm_to_adev(drm);
	adev->mode_info.num_crtc = 4;

	/* Plain kzalloc: amdgpu_dm_encoder_destroy() kfree()s it on teardown. */
	aencoder = kzalloc_obj(*aencoder);
	KUNIT_ASSERT_NOT_NULL(test, aencoder);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_encoder_init(drm, aencoder, 2), 0);
	KUNIT_EXPECT_EQ(test, aencoder->encoder_id, 2);
	KUNIT_EXPECT_EQ(test, (int)aencoder->base.possible_crtcs, 0xf);
	KUNIT_EXPECT_PTR_EQ(test, (const void *)aencoder->base.helper_private,
			    (const void *)&amdgpu_dm_encoder_helper_funcs);
}

struct dm_test_atomic_check_ctx {
	struct drm_device *drm;
	struct amdgpu_encoder *aenc;
	struct amdgpu_dm_connector *aconnector;
	struct dm_connector_state *dm_state;
	struct drm_crtc_state *crtc_state;
};

static struct dm_test_atomic_check_ctx *
dm_test_atomic_check_ctx_alloc(struct kunit *test, int connector_type)
{
	struct dm_test_atomic_check_ctx *ctx;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	ctx->drm = dm_test_alloc_drm(test);

	ctx->aenc = kunit_kzalloc(test, sizeof(*ctx->aenc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->aenc);
	ctx->aenc->base.dev = ctx->drm;

	ctx->aconnector = kunit_kzalloc(test, sizeof(*ctx->aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->aconnector);
	ctx->aconnector->base.connector_type = connector_type;

	ctx->dm_state = kunit_kzalloc(test, sizeof(*ctx->dm_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->dm_state);
	ctx->dm_state->base.connector = &ctx->aconnector->base;

	ctx->crtc_state = kunit_kzalloc(test, sizeof(*ctx->crtc_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->crtc_state);

	return ctx;
}

/**
 * dm_test_atomic_check_edp_native_keeps_scaling - Test native eDP mode is left alone
 * @test: The KUnit test context
 *
 * On an eDP connector whose adjusted mode matches the panel's native mode,
 * drm_crtc_helper_mode_valid_fixed() returns MODE_OK so scaling is untouched.
 */
static void dm_test_atomic_check_edp_native_keeps_scaling(struct kunit *test)
{
	struct dm_test_atomic_check_ctx *ctx =
		dm_test_atomic_check_ctx_alloc(test, DRM_MODE_CONNECTOR_eDP);

	ctx->aenc->native_mode.hdisplay = 1920;
	ctx->aenc->native_mode.vdisplay = 1080;
	ctx->crtc_state->adjusted_mode.hdisplay = 1920;
	ctx->crtc_state->adjusted_mode.vdisplay = 1080;
	ctx->dm_state->scaling = RMX_OFF;

	KUNIT_EXPECT_EQ(test,
		dm_encoder_helper_atomic_check(&ctx->aenc->base,
					       ctx->crtc_state,
					       &ctx->dm_state->base), 0);
	KUNIT_EXPECT_EQ(test, (int)ctx->dm_state->scaling, (int)RMX_OFF);
}

/**
 * dm_test_atomic_check_lvds_non_native_enables_scaling - Test non-native LVDS turns on scaling
 * @test: The KUnit test context
 *
 * On an LVDS connector whose adjusted mode differs from the native mode and is
 * currently RMX_OFF, the check enables RMX_ASPECT scaling and still returns 0.
 */
static void dm_test_atomic_check_lvds_non_native_enables_scaling(struct kunit *test)
{
	struct dm_test_atomic_check_ctx *ctx =
		dm_test_atomic_check_ctx_alloc(test, DRM_MODE_CONNECTOR_LVDS);

	ctx->aenc->native_mode.hdisplay = 1920;
	ctx->aenc->native_mode.vdisplay = 1080;
	ctx->crtc_state->adjusted_mode.hdisplay = 1280;
	ctx->crtc_state->adjusted_mode.vdisplay = 720;
	ctx->dm_state->scaling = RMX_OFF;

	KUNIT_EXPECT_EQ(test,
		dm_encoder_helper_atomic_check(&ctx->aenc->base,
					       ctx->crtc_state,
					       &ctx->dm_state->base), 0);
	KUNIT_EXPECT_EQ(test, (int)ctx->dm_state->scaling, (int)RMX_ASPECT);
}

/**
 * dm_test_atomic_check_non_mst_returns_zero - Test non-MST connectors short-circuit
 * @test: The KUnit test context
 *
 * A non-eDP/LVDS connector with no MST output port hits the early ``return 0``
 * before any topology state is touched.
 */
static void dm_test_atomic_check_non_mst_returns_zero(struct kunit *test)
{
	struct dm_test_atomic_check_ctx *ctx =
		dm_test_atomic_check_ctx_alloc(test, DRM_MODE_CONNECTOR_HDMIA);

	ctx->aconnector->mst_output_port = NULL;

	KUNIT_EXPECT_EQ(test,
		dm_encoder_helper_atomic_check(&ctx->aenc->base,
					       ctx->crtc_state,
					       &ctx->dm_state->base), 0);
}

/**
 * dm_test_atomic_check_mst_no_change_returns_zero - Test MST no-change short-circuit
 * @test: The KUnit test context
 *
 * An MST connector (mst_output_port set) whose CRTC state reports neither a
 * connectors nor a mode change hits the ``return 0`` before any topology state
 * is fetched.
 */
static void dm_test_atomic_check_mst_no_change_returns_zero(struct kunit *test)
{
	struct dm_test_atomic_check_ctx *ctx =
		dm_test_atomic_check_ctx_alloc(test, DRM_MODE_CONNECTOR_DisplayPort);
	struct drm_dp_mst_port *mst_port;
	struct amdgpu_dm_connector *mst_root;

	mst_port = kunit_kzalloc(test, sizeof(*mst_port), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, mst_port);
	mst_root = kunit_kzalloc(test, sizeof(*mst_root), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, mst_root);

	ctx->aconnector->mst_output_port = mst_port;
	ctx->aconnector->mst_root = mst_root;
	ctx->crtc_state->connectors_changed = false;
	ctx->crtc_state->mode_changed = false;

	KUNIT_EXPECT_EQ(test,
		dm_encoder_helper_atomic_check(&ctx->aenc->base,
					       ctx->crtc_state,
					       &ctx->dm_state->base), 0);
}


static uint32_t dm_test_atomic_check_dp_link_bandwidth_kbps(
	const struct dc_link *link,
	const struct dc_link_settings *link_settings)
{
	return 4320000;
}

static const struct dc_link_settings *dm_test_atomic_check_dp_get_verified_link_cap(
	const struct dc_link *link)
{
	return &link->verified_link_cap;
}

/* Fake fully-wired MST atomic state for the deep dm_encoder_helper_atomic_check() path. */
struct dm_test_mst_scaffold {
	struct drm_atomic_commit *state;
	struct drm_dp_mst_topology_state *mst_state;
	struct drm_dp_mst_atomic_payload *payload;
	struct drm_dp_mst_port *mst_port;
	struct amdgpu_dm_connector *mst_root;
	struct __drm_private_objs_state *priv_objs;
};

/*
 * dm_test_atomic_check_mst_scaffold - build a fake MST atomic state.
 * @map_success: when true the topology manager maps to a valid topology state
 *               and a fake DC link/link-service is wired so the PBN divider is
 *               non-zero; when false the manager maps to an error pointer so
 *               drm_atomic_get_mst_topology_state() reports failure.
 */
static void dm_test_atomic_check_mst_scaffold(struct kunit *test,
		struct dm_test_atomic_check_ctx *ctx,
		struct dm_test_mst_scaffold *s,
		bool map_success)
{
	struct drm_modeset_acquire_ctx *acquire_ctx;
	struct link_service *link_srv;
	struct dc *dc;
	struct dc_link *link;
	struct drm_connector *port_conn;
	struct drm_crtc *crtc;
	struct drm_connector_state *port_conn_state;
	struct __drm_connnectors_state *conns;

	memset(s, 0, sizeof(*s));

	s->mst_root = kunit_kzalloc(test, sizeof(*s->mst_root), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, s->mst_root);
	s->mst_root->mst_mgr.dev = ctx->drm;

	s->mst_port = kunit_kzalloc(test, sizeof(*s->mst_port), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, s->mst_port);

	ctx->aconnector->mst_root = s->mst_root;
	ctx->aconnector->mst_output_port = s->mst_port;

	acquire_ctx = kunit_kzalloc(test, sizeof(*acquire_ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, acquire_ctx);

	s->state = kunit_kzalloc(test, sizeof(*s->state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, s->state);
	s->state->dev = ctx->drm;
	s->state->acquire_ctx = acquire_ctx;

	s->priv_objs = kunit_kzalloc(test, sizeof(*s->priv_objs), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, s->priv_objs);
	s->priv_objs[0].ptr = &s->mst_root->mst_mgr.base;
	s->state->num_private_objs = 1;
	s->state->private_objs = s->priv_objs;

	ctx->crtc_state->state = s->state;
	ctx->crtc_state->connectors_changed = true;

	if (!map_success) {
		s->priv_objs[0].new_state = ERR_PTR(-ENOMEM);
		return;
	}

	/* Fake DC link so dm_mst_get_pbn_divider() yields a non-zero divider. */
	link_srv = kunit_kzalloc(test, sizeof(*link_srv), GFP_KERNEL);
	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, link_srv);
	KUNIT_ASSERT_NOT_NULL(test, dc);
	KUNIT_ASSERT_NOT_NULL(test, link);
	link_srv->dp_get_verified_link_cap =
		dm_test_atomic_check_dp_get_verified_link_cap;
	link_srv->dp_link_bandwidth_kbps =
		dm_test_atomic_check_dp_link_bandwidth_kbps;
	dc->link_srv = link_srv;
	link->dc = dc;
	s->mst_root->dc_link = link;

	s->mst_state = kunit_kzalloc(test, sizeof(*s->mst_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, s->mst_state);
	INIT_LIST_HEAD(&s->mst_state->payloads);
	s->priv_objs[0].new_state = &s->mst_state->base;

	/* Pre-seed a payload so find_time_slots() skips the port kref alloc path. */
	s->payload = kunit_kzalloc(test, sizeof(*s->payload), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, s->payload);
	s->payload->port = s->mst_port;
	list_add(&s->payload->next, &s->mst_state->payloads);

	/* Wire a connector+crtc so find_time_slots() can resolve the crtc mask. */
	port_conn = kunit_kzalloc(test, sizeof(*port_conn), GFP_KERNEL);
	crtc = kunit_kzalloc(test, sizeof(*crtc), GFP_KERNEL);
	port_conn_state = kunit_kzalloc(test, sizeof(*port_conn_state), GFP_KERNEL);
	conns = kunit_kzalloc(test, sizeof(*conns), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, port_conn);
	KUNIT_ASSERT_NOT_NULL(test, crtc);
	KUNIT_ASSERT_NOT_NULL(test, port_conn_state);
	KUNIT_ASSERT_NOT_NULL(test, conns);
	port_conn->index = 0;
	crtc->index = 0;
	port_conn_state->crtc = crtc;
	s->mst_port->connector = port_conn;
	conns[0].new_state = port_conn_state;
	s->state->num_connector = 1;
	s->state->connectors = conns;
}

/**
 * dm_test_atomic_check_mst_finds_vcpi_slots - Test the MST time-slot allocation path
 * @test: The KUnit test context
 *
 * With a fully wired MST atomic state and connectors_changed set, the check
 * computes the color depth/PBN, allocates VCPI time slots and returns 0.
 */
static void dm_test_atomic_check_mst_finds_vcpi_slots(struct kunit *test)
{
	struct dm_test_atomic_check_ctx *ctx =
		dm_test_atomic_check_ctx_alloc(test, DRM_MODE_CONNECTOR_DisplayPort);
	struct dm_test_mst_scaffold s;

	dm_test_atomic_check_mst_scaffold(test, ctx, &s, true);
	s.state->duplicated = false;
	ctx->dm_state->base.max_requested_bpc = 8;

	KUNIT_EXPECT_EQ(test,
		dm_encoder_helper_atomic_check(&ctx->aenc->base,
					       ctx->crtc_state,
					       &ctx->dm_state->base), 0);
	KUNIT_EXPECT_GE(test, ctx->dm_state->vcpi_slots, 0);
}

/**
 * dm_test_atomic_check_mst_duplicated_skips_pbn - Test the duplicated-state fast path
 * @test: The KUnit test context
 *
 * When the atomic state is duplicated the color depth/PBN recompute block is
 * skipped, but time slots are still allocated and the check returns 0.
 */
static void dm_test_atomic_check_mst_duplicated_skips_pbn(struct kunit *test)
{
	struct dm_test_atomic_check_ctx *ctx =
		dm_test_atomic_check_ctx_alloc(test, DRM_MODE_CONNECTOR_DisplayPort);
	struct dm_test_mst_scaffold s;

	dm_test_atomic_check_mst_scaffold(test, ctx, &s, true);
	s.state->duplicated = true;
	ctx->dm_state->pbn = 0;

	KUNIT_EXPECT_EQ(test,
		dm_encoder_helper_atomic_check(&ctx->aenc->base,
					       ctx->crtc_state,
					       &ctx->dm_state->base), 0);
}

/**
 * dm_test_atomic_check_mst_topology_err_propagates - Test topology-state error propagation
 * @test: The KUnit test context
 *
 * When drm_atomic_get_mst_topology_state() returns an error pointer, the check
 * propagates the error code back to the caller.
 */
static void dm_test_atomic_check_mst_topology_err_propagates(struct kunit *test)
{
	struct dm_test_atomic_check_ctx *ctx =
		dm_test_atomic_check_ctx_alloc(test, DRM_MODE_CONNECTOR_DisplayPort);
	struct dm_test_mst_scaffold s;

	dm_test_atomic_check_mst_scaffold(test, ctx, &s, false);

	KUNIT_EXPECT_EQ(test,
		dm_encoder_helper_atomic_check(&ctx->aenc->base,
					       ctx->crtc_state,
					       &ctx->dm_state->base), -ENOMEM);
}

/**
 * dm_test_atomic_check_mst_vcpi_error_propagates - Test VCPI allocation error propagation
 * @test: The KUnit test context
 *
 * A pre-existing payload marked for deletion makes drm_dp_atomic_find_time_slots()
 * fail; the negative vcpi_slots result is propagated back to the caller.
 */
static void dm_test_atomic_check_mst_vcpi_error_propagates(struct kunit *test)
{
	struct dm_test_atomic_check_ctx *ctx =
		dm_test_atomic_check_ctx_alloc(test, DRM_MODE_CONNECTOR_DisplayPort);
	struct dm_test_mst_scaffold s;

	dm_test_atomic_check_mst_scaffold(test, ctx, &s, true);
	s.state->duplicated = true;
	ctx->dm_state->pbn = 100;
	s.payload->delete = true;

	KUNIT_EXPECT_EQ(test,
		dm_encoder_helper_atomic_check(&ctx->aenc->base,
					       ctx->crtc_state,
					       &ctx->dm_state->base), -EINVAL);
}

/* Tests for hdmi_cec_unset_edid() */

/**
 * dm_test_hdmi_cec_unset_edid_no_notifier - Test the no-notifier no-op path
 * @test: The KUnit test context
 *
 * With aconnector->notifier NULL the function returns early and must not crash.
 */
static void dm_test_hdmi_cec_unset_edid_no_notifier(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);

	hdmi_cec_unset_edid(aconnector);
}

/* Tests for create_eml_sink() and handle_edid_mgmt() */

/*
 * create_eml_sink() reads EDID off the connector's DDC. Forcing the connector
 * DRM_FORCE_OFF makes drm_edid_read_ddc() return NULL before touching any i2c
 * adapter, exercising the "no EDID" branch without real hardware. aux_mode is
 * set so the embedded DP AUX ddc is selected (no i2c adapter pointer needed).
 */
struct dm_test_edid_ctx {
	struct drm_device *drm;
	struct amdgpu_dm_connector *aconnector;
	struct dc_link *link;
};

static struct dm_test_edid_ctx *
dm_test_edid_ctx_alloc(struct kunit *test, int connector_type)
{
	struct dm_test_edid_ctx *ctx;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	ctx->drm = dm_test_alloc_drm(test);
	ctx->aconnector = dm_test_add_connector(test, ctx->drm, connector_type);

	ctx->link = kunit_kzalloc(test, sizeof(*ctx->link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->link);
	ctx->link->aux_mode = true;
	ctx->aconnector->dc_link = ctx->link;

	ctx->aconnector->base.force = DRM_FORCE_OFF;

	return ctx;
}

/**
 * dm_test_create_eml_sink_no_edid - Test the no-EDID branch creates no sink
 * @test: The KUnit test context
 *
 * When no EDID can be read the function logs an error and returns without
 * allocating an emulated sink.
 */
static void dm_test_create_eml_sink_no_edid(struct kunit *test)
{
	struct dm_test_edid_ctx *ctx =
		dm_test_edid_ctx_alloc(test, DRM_MODE_CONNECTOR_DisplayPort);

	create_eml_sink(ctx->aconnector);

	KUNIT_EXPECT_NULL(test, ctx->aconnector->dc_em_sink);
}

/**
 * dm_test_handle_edid_mgmt_dp_sets_link_caps - Test DP seeds verified link caps
 * @test: The KUnit test context
 *
 * For a DisplayPort link the function primes verified_link_cap before reading
 * EDID so a headless force-on connector can still modeset.
 */
static void dm_test_handle_edid_mgmt_dp_sets_link_caps(struct kunit *test)
{
	struct dm_test_edid_ctx *ctx =
		dm_test_edid_ctx_alloc(test, DRM_MODE_CONNECTOR_DisplayPort);

	ctx->link->connector_signal = SIGNAL_TYPE_DISPLAY_PORT;

	handle_edid_mgmt(ctx->aconnector);

	KUNIT_EXPECT_EQ(test, (int)ctx->link->verified_link_cap.lane_count,
			(int)LANE_COUNT_FOUR);
	KUNIT_EXPECT_EQ(test, (int)ctx->link->verified_link_cap.link_rate,
			(int)LINK_RATE_HIGH2);
	KUNIT_EXPECT_NULL(test, ctx->aconnector->dc_em_sink);
}

/**
 * dm_test_handle_edid_mgmt_non_dp_leaves_caps - Test non-DP links keep zeroed caps
 * @test: The KUnit test context
 *
 * A non-DisplayPort link skips the verified_link_cap seeding entirely.
 */
static void dm_test_handle_edid_mgmt_non_dp_leaves_caps(struct kunit *test)
{
	struct dm_test_edid_ctx *ctx =
		dm_test_edid_ctx_alloc(test, DRM_MODE_CONNECTOR_HDMIA);

	ctx->link->connector_signal = SIGNAL_TYPE_HDMI_TYPE_A;

	handle_edid_mgmt(ctx->aconnector);

	KUNIT_EXPECT_EQ(test, (int)ctx->link->verified_link_cap.lane_count, 0);
	KUNIT_EXPECT_EQ(test, (int)ctx->link->verified_link_cap.link_rate, 0);
}

/*
 * Context for the connector funcs / modes tests: a managed DRM device with a
 * registered connector and a managed encoder attached to it, so helpers that
 * walk connector->encoder relationships resolve correctly.
 */
struct dm_test_modes_ctx {
	struct drm_device *drm;
	struct amdgpu_dm_connector *aconnector;
	struct amdgpu_encoder *aenc;
};

static struct dm_test_modes_ctx *
dm_test_modes_ctx_alloc(struct kunit *test, int connector_type)
{
	struct dm_test_modes_ctx *ctx;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	ctx->drm = dm_test_alloc_drm(test);
	ctx->aconnector = dm_test_add_connector(test, ctx->drm, connector_type);

	ctx->aenc = drmm_kzalloc(ctx->drm, sizeof(*ctx->aenc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->aenc);
	KUNIT_ASSERT_EQ(test,
			drmm_encoder_init(ctx->drm, &ctx->aenc->base, NULL,
					  DRM_MODE_ENCODER_TMDS, NULL), 0);
	KUNIT_ASSERT_EQ(test,
			drm_connector_attach_encoder(&ctx->aconnector->base,
						     &ctx->aenc->base), 0);

	return ctx;
}

/**
 * dm_test_funcs_force_no_edid - Test force() leaves drm_edid NULL when no EDID
 * @test: The KUnit test context
 *
 * A headless force-on DisplayPort connector reads no EDID, so the cached
 * drm_edid pointer must stay NULL after the force callback runs.
 */
static void dm_test_funcs_force_no_edid(struct kunit *test)
{
	struct dm_test_edid_ctx *ctx =
		dm_test_edid_ctx_alloc(test, DRM_MODE_CONNECTOR_DisplayPort);

	amdgpu_dm_connector_funcs_force(&ctx->aconnector->base);

	KUNIT_EXPECT_NULL(test, ctx->aconnector->drm_edid);
}

/**
 * dm_test_validate_stream_null_stream - Test NULL stream returns unexpected
 * @test: The KUnit test context
 *
 * With a NULL stream the validation jumps straight to cleanup without ever
 * dereferencing the dc handle and reports DC_ERROR_UNEXPECTED.
 */
static void dm_test_validate_stream_null_stream(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test,
			(int)dm_validate_stream_and_context(NULL, NULL),
			(int)DC_ERROR_UNEXPECTED);
}

static bool dm_test_vs_validate_timing_true(struct timing_generator *tg,
					     const struct dc_crtc_timing *timing)
{
	return true;
}

static enum dc_status dm_test_vs_validate_mode_timing_ok(
		const struct dc_stream_state *stream,
		struct dc_link *link,
		const struct dc_crtc_timing *timing)
{
	return DC_OK;
}

/**
 * dm_test_validate_stream_dc_ok_no_pipe - Exercise the full validation body
 * @test: The KUnit test context
 *
 * Build a minimal fake dc so stream and plane validation both pass, then
 * let dc_state_add_stream() bail out early (timing_generator_count == 0) so
 * the deep pipe-allocation paths are avoided. This drives plane population,
 * dc_validate_stream(), dc_validate_plane() and cleanup.
 */
static void dm_test_validate_stream_dc_ok_no_pipe(struct kunit *test)
{
	struct drm_device *drm = dm_test_alloc_drm(test);
	struct dc *dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	struct dc_context *ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	struct dal_logger *logger = kunit_kzalloc(test, sizeof(*logger), GFP_KERNEL);
	struct resource_pool *pool = kunit_kzalloc(test, sizeof(*pool), GFP_KERNEL);
	struct resource_funcs *rfuncs = kunit_kzalloc(test, sizeof(*rfuncs), GFP_KERNEL);
	struct resource_caps *rcaps = kunit_kzalloc(test, sizeof(*rcaps), GFP_KERNEL);
	struct timing_generator *tg = kunit_kzalloc(test, sizeof(*tg), GFP_KERNEL);
	struct timing_generator_funcs *tgfuncs =
		kunit_kzalloc(test, sizeof(*tgfuncs), GFP_KERNEL);
	struct link_service *link_srv =
		kunit_kzalloc(test, sizeof(*link_srv), GFP_KERNEL);
	struct dc_stream_state *stream =
		kunit_kzalloc(test, sizeof(*stream), GFP_KERNEL);
	struct dc_link *link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, drm);
	KUNIT_ASSERT_NOT_NULL(test, dc);
	KUNIT_ASSERT_NOT_NULL(test, ctx);
	KUNIT_ASSERT_NOT_NULL(test, logger);
	KUNIT_ASSERT_NOT_NULL(test, pool);
	KUNIT_ASSERT_NOT_NULL(test, rfuncs);
	KUNIT_ASSERT_NOT_NULL(test, rcaps);
	KUNIT_ASSERT_NOT_NULL(test, tg);
	KUNIT_ASSERT_NOT_NULL(test, tgfuncs);
	KUNIT_ASSERT_NOT_NULL(test, link_srv);
	KUNIT_ASSERT_NOT_NULL(test, stream);
	KUNIT_ASSERT_NOT_NULL(test, link);

	logger->dev = drm;
	ctx->logger = logger;

	tgfuncs->validate_timing = dm_test_vs_validate_timing_true;
	tg->funcs = tgfuncs;

	pool->funcs = rfuncs;
	pool->res_cap = rcaps;
	pool->timing_generators[0] = tg;
	pool->timing_generator_count = 0;

	link_srv->validate_mode_timing = dm_test_vs_validate_mode_timing_ok;

	dc->ctx = ctx;
	dc->res_pool = pool;
	dc->link_srv = link_srv;

	link->ep_type = DISPLAY_ENDPOINT_UNKNOWN;
	stream->link = link;
	stream->src.width = 1920;
	stream->src.height = 1080;

	KUNIT_EXPECT_EQ(test,
			(int)dm_validate_stream_and_context(dc, stream),
			(int)DC_ERROR_UNEXPECTED);
}

/**
 * dm_test_to_encoder_no_encoder - Test connector with no encoder returns NULL
 * @test: The KUnit test context
 */
static void dm_test_to_encoder_no_encoder(struct kunit *test)
{
	struct drm_device *drm = dm_test_alloc_drm(test);
	struct amdgpu_dm_connector *aconnector =
		dm_test_add_connector(test, drm, DRM_MODE_CONNECTOR_HDMIA);

	KUNIT_EXPECT_NULL(test,
			  amdgpu_dm_connector_to_encoder(&aconnector->base));
}

/**
 * dm_test_to_encoder_returns_attached - Test the attached encoder is returned
 * @test: The KUnit test context
 */
static void dm_test_to_encoder_returns_attached(struct kunit *test)
{
	struct dm_test_modes_ctx *ctx =
		dm_test_modes_ctx_alloc(test, DRM_MODE_CONNECTOR_HDMIA);

	KUNIT_EXPECT_PTR_EQ(test,
			    amdgpu_dm_connector_to_encoder(&ctx->aconnector->base),
			    &ctx->aenc->base);
}

/**
 * dm_test_native_mode_no_encoder - Test native mode resolution is a no-op
 * @test: The KUnit test context
 *
 * Without an encoder there is nothing to copy into, so the call must return
 * cleanly without dereferencing a NULL encoder.
 */
static void dm_test_native_mode_no_encoder(struct kunit *test)
{
	struct drm_device *drm = dm_test_alloc_drm(test);
	struct amdgpu_dm_connector *aconnector =
		dm_test_add_connector(test, drm, DRM_MODE_CONNECTOR_HDMIA);

	amdgpu_dm_get_native_mode(&aconnector->base);
}

/**
 * dm_test_native_mode_empty_probed_zeroes_clock - Test empty probed list clears mode
 * @test: The KUnit test context
 *
 * With no probed modes there is no preferred mode to copy, so the encoder's
 * native mode is memset to zero (clock becomes 0).
 */
static void dm_test_native_mode_empty_probed_zeroes_clock(struct kunit *test)
{
	struct dm_test_modes_ctx *ctx =
		dm_test_modes_ctx_alloc(test, DRM_MODE_CONNECTOR_eDP);

	ctx->aenc->native_mode.clock = 148500;

	amdgpu_dm_get_native_mode(&ctx->aconnector->base);

	KUNIT_EXPECT_EQ(test, ctx->aenc->native_mode.clock, 0);
}

/**
 * dm_test_native_mode_copies_preferred - Test the preferred mode is copied
 * @test: The KUnit test context
 *
 * The preferred probed mode is duplicated into the encoder's native mode.
 */
static void dm_test_native_mode_copies_preferred(struct kunit *test)
{
	struct dm_test_modes_ctx *ctx =
		dm_test_modes_ctx_alloc(test, DRM_MODE_CONNECTOR_eDP);
	struct drm_display_mode *mode;

	mode = drm_mode_create(ctx->drm);
	KUNIT_ASSERT_NOT_NULL(test, mode);
	mode->type = DRM_MODE_TYPE_PREFERRED;
	mode->clock = 148500;
	mode->hdisplay = 1920;
	mode->vdisplay = 1080;
	mutex_lock(&ctx->drm->mode_config.mutex);
	drm_mode_probed_add(&ctx->aconnector->base, mode);

	amdgpu_dm_get_native_mode(&ctx->aconnector->base);
	mutex_unlock(&ctx->drm->mode_config.mutex);

	KUNIT_EXPECT_EQ(test, ctx->aenc->native_mode.hdisplay, 1920);
	KUNIT_EXPECT_EQ(test, ctx->aenc->native_mode.vdisplay, 1080);
	KUNIT_EXPECT_EQ(test, ctx->aenc->native_mode.clock, 148500);
}

/**
 * dm_test_create_common_mode_overrides - Test common mode inherits native timing
 * @test: The KUnit test context
 *
 * A new common mode takes its pixel clock and porches from the encoder's
 * native mode but overrides the visible resolution and clears PREFERRED.
 */
static void dm_test_create_common_mode_overrides(struct kunit *test)
{
	struct dm_test_modes_ctx *ctx =
		dm_test_modes_ctx_alloc(test, DRM_MODE_CONNECTOR_eDP);
	struct drm_display_mode *mode;

	ctx->aenc->native_mode.clock = 148500;
	ctx->aenc->native_mode.htotal = 2200;
	ctx->aenc->native_mode.type = DRM_MODE_TYPE_PREFERRED;

	mode = amdgpu_dm_create_common_mode(&ctx->aenc->base, "800x600",
					    800, 600);
	KUNIT_ASSERT_NOT_NULL(test, mode);

	KUNIT_EXPECT_EQ(test, mode->hdisplay, 800);
	KUNIT_EXPECT_EQ(test, mode->vdisplay, 600);
	KUNIT_EXPECT_EQ(test, mode->clock, 148500);
	KUNIT_EXPECT_EQ(test, mode->htotal, 2200);
	KUNIT_EXPECT_FALSE(test, mode->type & DRM_MODE_TYPE_PREFERRED);
	KUNIT_EXPECT_STREQ(test, mode->name, "800x600");

	drm_mode_destroy(ctx->drm, mode);
}

/**
 * dm_test_add_common_modes_non_edp_noop - Test non-eDP/LVDS adds no modes
 * @test: The KUnit test context
 *
 * Common scaled modes are only added for eDP/LVDS panels; an HDMI connector
 * is left untouched.
 */
static void dm_test_add_common_modes_non_edp_noop(struct kunit *test)
{
	struct dm_test_modes_ctx *ctx =
		dm_test_modes_ctx_alloc(test, DRM_MODE_CONNECTOR_HDMIA);

	ctx->aenc->native_mode.hdisplay = 1920;
	ctx->aenc->native_mode.vdisplay = 1200;

	mutex_lock(&ctx->drm->mode_config.mutex);
	amdgpu_dm_connector_add_common_modes(&ctx->aenc->base,
					     &ctx->aconnector->base);
	mutex_unlock(&ctx->drm->mode_config.mutex);

	KUNIT_EXPECT_EQ(test, ctx->aconnector->num_modes, 0);
}

/**
 * dm_test_add_common_modes_edp_adds - Test eDP adds the smaller common modes
 * @test: The KUnit test context
 *
 * For an eDP panel with a 1920x1200 native mode every common mode strictly
 * smaller than the native one is added (10 of the 11 entries).
 */
static void dm_test_add_common_modes_edp_adds(struct kunit *test)
{
	struct dm_test_modes_ctx *ctx =
		dm_test_modes_ctx_alloc(test, DRM_MODE_CONNECTOR_eDP);

	ctx->aenc->native_mode.hdisplay = 1920;
	ctx->aenc->native_mode.vdisplay = 1200;

	mutex_lock(&ctx->drm->mode_config.mutex);
	amdgpu_dm_connector_add_common_modes(&ctx->aenc->base,
					     &ctx->aconnector->base);
	mutex_unlock(&ctx->drm->mode_config.mutex);

	KUNIT_EXPECT_EQ(test, ctx->aconnector->num_modes, 10);
}

/**
 * dm_test_ddc_get_modes_null_edid - Test a NULL EDID resets the mode count
 * @test: The KUnit test context
 */
static void dm_test_ddc_get_modes_null_edid(struct kunit *test)
{
	struct drm_device *drm = dm_test_alloc_drm(test);
	struct amdgpu_dm_connector *aconnector =
		dm_test_add_connector(test, drm, DRM_MODE_CONNECTOR_HDMIA);

	aconnector->num_modes = 5;

	amdgpu_dm_connector_ddc_get_modes(&aconnector->base, NULL);

	KUNIT_EXPECT_EQ(test, aconnector->num_modes, 0);
}

/**
 * dm_test_add_fs_modes_no_preferred_mode - Test no preferred mode yields no modes
 * @test: The KUnit test context
 *
 * A writeback connector has no highest-refresh-rate mode, so add_fs_modes()
 * cannot build any FreeSync video modes and returns 0.
 */
static void dm_test_add_fs_modes_no_preferred_mode(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);

	aconnector->base.connector_type = DRM_MODE_CONNECTOR_WRITEBACK;

	KUNIT_EXPECT_EQ(test, (int)add_fs_modes(aconnector), 0);
}

/*
 * Build a DisplayPort connector whose highest-refresh mode is a fixed
 * 1920x1080@60 timing. A non-zero freesync_vid_base.clock makes
 * amdgpu_dm_get_highest_refresh_rate_mode() return that timing directly,
 * giving the test full control of the reference mode.
 */
static struct amdgpu_dm_connector *dm_test_fs_setup(struct kunit *test)
{
	struct drm_device *drm = dm_test_alloc_drm(test);
	struct amdgpu_dm_connector *aconnector =
		dm_test_add_connector(test, drm, DRM_MODE_CONNECTOR_DisplayPort);
	struct drm_display_mode *m = &aconnector->freesync_vid_base;

	m->clock = 148500;
	m->hdisplay = 1920;
	m->htotal = 2200;
	m->vdisplay = 1080;
	m->vsync_start = 1084;
	m->vsync_end = 1089;
	m->vtotal = 1125;

	return aconnector;
}

/**
 * dm_test_add_fs_modes_generates - Test FreeSync video modes are added
 * @test: The KUnit test context
 *
 * With min/max vfreq spanning the standard rates, add_fs_modes() derives one
 * mode per legal rate at or below the reference refresh. A second call finds
 * every generated mode already present, so is_duplicate_mode() rejects them
 * all and no new modes are added.
 */
static void dm_test_add_fs_modes_generates(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector = dm_test_fs_setup(test);
	unsigned int count, duplicate_count;

	aconnector->min_vfreq = 20;
	aconnector->max_vfreq = 60;

	mutex_lock(&aconnector->base.dev->mode_config.mutex);
	count = add_fs_modes(aconnector);
	duplicate_count = add_fs_modes(aconnector);
	mutex_unlock(&aconnector->base.dev->mode_config.mutex);

	KUNIT_EXPECT_EQ(test, count, 8);
	KUNIT_EXPECT_EQ(test, duplicate_count, 0);
}

/**
 * dm_test_add_fs_modes_out_of_range - Test no modes when rates fall outside range
 * @test: The KUnit test context
 *
 * A vfreq window above the reference refresh leaves every standard rate either
 * higher than the mode or outside [min_vfreq, max_vfreq], so add_fs_modes()
 * skips them all and returns 0.
 */
static void dm_test_add_fs_modes_out_of_range(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector = dm_test_fs_setup(test);

	aconnector->min_vfreq = 100;
	aconnector->max_vfreq = 120;

	KUNIT_EXPECT_EQ(test, (int)add_fs_modes(aconnector), 0);
}

/**
 * dm_test_add_fs_modes_skips_illegal - Test illegal derived timings are dropped
 * @test: The KUnit test context
 *
 * A reference vtotal of 1133 gives a true refresh of ~59.58 that rounds up to
 * 60, so the 60000 rate clears the refresh check yet yields a negative vtotal
 * delta. The resulting timing is illegal, so add_fs_modes() skips it and
 * returns 0.
 */
static void dm_test_add_fs_modes_skips_illegal(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector = dm_test_fs_setup(test);

	aconnector->freesync_vid_base.vtotal = 1133;
	aconnector->min_vfreq = 59;
	aconnector->max_vfreq = 60;

	KUNIT_EXPECT_EQ(test, (int)add_fs_modes(aconnector), 0);
}

/**
 * dm_test_add_freesync_modes_null_edid_noop - Test NULL EDID adds no modes
 * @test: The KUnit test context
 *
 * Without an EDID the FreeSync video modes cannot be derived, so the mode
 * count is left unchanged.
 */
static void dm_test_add_freesync_modes_null_edid_noop(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);

	aconnector->num_modes = 7;

	amdgpu_dm_connector_add_freesync_modes(&aconnector->base, NULL);

	KUNIT_EXPECT_EQ(test, aconnector->num_modes, 7);
}

/* EDID extension block tag values (avoids pulling in private drm headers). */
#define DM_TEST_CEA_EXT		0x02
#define DM_TEST_DISPLAYID_EXT	0x70

/**
 * dm_test_i2c_func_returns_flags - Test the i2c functionality flags
 * @test: The KUnit test context
 *
 * The algorithm advertises plain I2C plus emulated SMBUS regardless of the
 * adapter argument, which it never dereferences.
 */
static void dm_test_i2c_func_returns_flags(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, amdgpu_dm_i2c_func(NULL),
			I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL);
}

/**
 * dm_test_i2c_xfer_no_ddc_pin - Test transfers without a DDC pin are rejected
 * @test: The KUnit test context
 *
 * When the backing ddc_service has no ddc_pin the transfer bails out early
 * with -EIO before touching the message buffers or the dc handle.
 */
static void dm_test_i2c_xfer_no_ddc_pin(struct kunit *test)
{
	struct amdgpu_i2c_adapter *i2c;
	struct ddc_service *ddc;

	i2c = kunit_kzalloc(test, sizeof(*i2c), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, i2c);
	ddc = kunit_kzalloc(test, sizeof(*ddc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ddc);

	i2c->ddc_service = ddc;
	i2c_set_adapdata(&i2c->base, i2c);

	/* ddc->ddc_pin is NULL -> transfer is rejected with -EIO. */
	KUNIT_EXPECT_EQ(test, amdgpu_dm_i2c_xfer(&i2c->base, NULL, 0), -EIO);
}

/* Fake i2c adapter chain used by the amdgpu_dm_i2c_xfer() tests. */
struct dm_test_i2c_ctx {
	struct amdgpu_i2c_adapter *i2c;
	struct ddc_service *ddc;
	struct dc_context *dc_ctx;
	struct dc *dc;
	struct dc_link *link;
};

/*
 * Build an i2c adapter whose ddc_service has a ddc_pin (so the transfer is not
 * rejected up front) and whose dc back-pointer resolves to a link at index 0.
 * The link ddc has no ddc_pin, so the non-OEM dc_submit_i2c() path bails out
 * and reports failure without touching real hardware.
 */
static struct dm_test_i2c_ctx *dm_test_i2c_setup(struct kunit *test)
{
	struct dm_test_i2c_ctx *ctx;
	struct ddc_service *link_ddc;
	struct ddc *pin;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);
	ctx->i2c = kunit_kzalloc(test, sizeof(*ctx->i2c), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->i2c);
	ctx->ddc = kunit_kzalloc(test, sizeof(*ctx->ddc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->ddc);
	ctx->dc_ctx = kunit_kzalloc(test, sizeof(*ctx->dc_ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->dc_ctx);
	ctx->dc = kunit_kzalloc(test, sizeof(*ctx->dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->dc);
	ctx->link = kunit_kzalloc(test, sizeof(*ctx->link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->link);
	pin = kunit_kzalloc(test, sizeof(*pin), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, pin);
	link_ddc = kunit_kzalloc(test, sizeof(*link_ddc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, link_ddc);

	ctx->ddc->ddc_pin = pin;
	ctx->ddc->ctx = ctx->dc_ctx;
	ctx->ddc->link = ctx->link;
	ctx->dc_ctx->dc = ctx->dc;
	ctx->link->link_index = 0;
	ctx->link->ddc = link_ddc;
	ctx->dc->links[0] = ctx->link;
	ctx->i2c->ddc_service = ctx->ddc;
	i2c_set_adapdata(&ctx->i2c->base, ctx->i2c);

	return ctx;
}

/**
 * dm_test_i2c_xfer_hw_submit_fails - Test the non-OEM path reports a failed submit
 * @test: The KUnit test context
 *
 * With a ddc_pin present the transfer builds the payload list and dispatches
 * through dc_submit_i2c(); the target link has no ddc_pin, so the submit fails
 * and the transfer returns -EIO instead of the message count.
 */
static void dm_test_i2c_xfer_hw_submit_fails(struct kunit *test)
{
	struct dm_test_i2c_ctx *ctx = dm_test_i2c_setup(test);
	u8 buf[2];
	struct i2c_msg msgs[] = {
		{ .addr = 0x50, .flags = I2C_M_RD, .len = sizeof(buf), .buf = buf },
	};

	KUNIT_EXPECT_EQ(test, amdgpu_dm_i2c_xfer(&ctx->i2c->base, msgs, 1), -EIO);
}

/**
 * dm_test_i2c_xfer_oem_no_device - Test the OEM path with no OEM device fails
 * @test: The KUnit test context
 *
 * When the adapter is flagged as OEM the transfer routes through
 * dc_submit_i2c_oem(); with no oem_device on the resource pool the submit
 * fails and the transfer returns -EIO.
 */
static void dm_test_i2c_xfer_oem_no_device(struct kunit *test)
{
	struct dm_test_i2c_ctx *ctx = dm_test_i2c_setup(test);
	struct resource_pool *pool;
	u8 buf[2] = { 0x12, 0x34 };
	struct i2c_msg msgs[] = {
		{ .addr = 0x50, .flags = 0, .len = sizeof(buf), .buf = buf },
	};

	pool = kunit_kzalloc(test, sizeof(*pool), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, pool);
	ctx->dc->res_pool = pool;
	ctx->i2c->oem = true;

	KUNIT_EXPECT_EQ(test, amdgpu_dm_i2c_xfer(&ctx->i2c->base, msgs, 1), -EIO);
}

/* Fake ddc_service chain for amdgpu_dm_create_i2c() tests. */
struct dm_test_create_i2c_ctx {
	struct ddc_service *ddc;
	struct amdgpu_device *adev;
	struct pci_dev *pdev;
	struct dc_link *link;
};

/*
 * Build a ddc_service whose dc_context driver_context is an amdgpu_device with
 * a pci_dev (for the parent device), plus a link used by the hardware-bus name.
 */
static struct dm_test_create_i2c_ctx *dm_test_create_i2c_setup(struct kunit *test)
{
	struct dm_test_create_i2c_ctx *ctx;
	struct dc_context *dc_ctx;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);
	ctx->ddc = kunit_kzalloc(test, sizeof(*ctx->ddc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->ddc);
	ctx->adev = kunit_kzalloc(test, sizeof(*ctx->adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->adev);
	ctx->pdev = kunit_kzalloc(test, sizeof(*ctx->pdev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->pdev);
	ctx->link = kunit_kzalloc(test, sizeof(*ctx->link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->link);
	dc_ctx = kunit_kzalloc(test, sizeof(*dc_ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dc_ctx);

	ctx->adev->pdev = ctx->pdev;
	dc_ctx->driver_context = ctx->adev;
	ctx->ddc->ctx = dc_ctx;
	ctx->ddc->link = ctx->link;

	return ctx;
}

/**
 * dm_test_create_i2c_oem - Test the OEM adapter name and wiring
 * @test: The KUnit test context
 *
 * The OEM adapter is named for the OEM bus and is wired to the dm i2c
 * algorithm, the backing ddc_service, and the pci parent device.
 */
static void dm_test_create_i2c_oem(struct kunit *test)
{
	struct dm_test_create_i2c_ctx *ctx = dm_test_create_i2c_setup(test);
	struct amdgpu_i2c_adapter *i2c;

	i2c = amdgpu_dm_create_i2c(ctx->ddc, true);
	KUNIT_ASSERT_NOT_NULL(test, i2c);

	KUNIT_EXPECT_TRUE(test, i2c->oem);
	KUNIT_EXPECT_PTR_EQ(test, i2c->ddc_service, ctx->ddc);
	KUNIT_EXPECT_PTR_EQ(test, i2c_get_adapdata(&i2c->base), i2c);
	KUNIT_EXPECT_PTR_EQ(test, i2c->base.dev.parent, &ctx->adev->pdev->dev);
	KUNIT_EXPECT_TRUE(test, i2c->base.algo->master_xfer == amdgpu_dm_i2c_xfer);
	KUNIT_EXPECT_TRUE(test, i2c->base.algo->functionality == amdgpu_dm_i2c_func);
	KUNIT_EXPECT_STREQ(test, i2c->base.name, "AMDGPU DM i2c OEM bus");

	kfree(i2c);
}

/**
 * dm_test_create_i2c_hw_bus - Test the hardware bus adapter name carries the index
 * @test: The KUnit test context
 *
 * The non-OEM adapter is named for the hardware bus and embeds the link index,
 * and the OEM flag is left clear.
 */
static void dm_test_create_i2c_hw_bus(struct kunit *test)
{
	struct dm_test_create_i2c_ctx *ctx = dm_test_create_i2c_setup(test);
	struct amdgpu_i2c_adapter *i2c;

	ctx->link->link_index = 7;

	i2c = amdgpu_dm_create_i2c(ctx->ddc, false);
	KUNIT_ASSERT_NOT_NULL(test, i2c);

	KUNIT_EXPECT_FALSE(test, i2c->oem);
	KUNIT_EXPECT_PTR_EQ(test, i2c->ddc_service, ctx->ddc);
	KUNIT_EXPECT_STREQ(test, i2c->base.name, "AMDGPU DM i2c hw bus 7");

	kfree(i2c);
}

/**
 * dm_test_restore_state_writeback - Test writeback connectors are skipped
 * @test: The KUnit test context
 *
 * A writeback connector short-circuits before dc_sink is ever read, so leaving
 * it NULL must not crash and no connector state is created.
 */
static void dm_test_restore_state_writeback(struct kunit *test)
{
	struct drm_device *drm = dm_test_alloc_drm(test);
	struct amdgpu_dm_connector *aconnector;

	aconnector = dm_test_add_connector(test, drm,
					   DRM_MODE_CONNECTOR_WRITEBACK);

	dm_restore_drm_connector_state(drm, &aconnector->base);

	KUNIT_EXPECT_NULL(test, aconnector->base.state);
}

/**
 * dm_test_restore_state_no_dc_sink - Test a connector without a dc_sink is a no-op
 * @test: The KUnit test context
 *
 * With no dc_sink there is nothing to restore, so the function returns before
 * touching the connector state or encoder.
 */
static void dm_test_restore_state_no_dc_sink(struct kunit *test)
{
	struct drm_device *drm = dm_test_alloc_drm(test);
	struct amdgpu_dm_connector *aconnector;

	aconnector = dm_test_add_connector(test, drm,
					   DRM_MODE_CONNECTOR_HDMIA);
	/* dc_sink left NULL by kzalloc. */

	dm_restore_drm_connector_state(drm, &aconnector->base);

	KUNIT_EXPECT_NULL(test, aconnector->base.state);
}

/**
 * dm_test_restore_state_no_connector_state - Test a NULL connector state bails out
 * @test: The KUnit test context
 *
 * A dc_sink is present but the connector has no atomic state, so the function
 * returns before dereferencing the encoder.
 */
static void dm_test_restore_state_no_connector_state(struct kunit *test)
{
	struct drm_device *drm = dm_test_alloc_drm(test);
	struct amdgpu_dm_connector *aconnector;

	aconnector = dm_test_add_connector(test, drm,
					   DRM_MODE_CONNECTOR_HDMIA);
	aconnector->dc_sink = kunit_kzalloc(test, 1, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector->dc_sink);
	/* connector->state left NULL: the guard must catch it. */

	dm_restore_drm_connector_state(drm, &aconnector->base);

	KUNIT_EXPECT_NULL(test, aconnector->base.encoder);
}

/**
 * dm_test_restore_state_no_encoder - Test a NULL encoder bails out
 * @test: The KUnit test context
 *
 * A dc_sink and connector state are present but the connector is not routed to
 * any encoder, so the function returns before reading the encoder's crtc.
 */
static void dm_test_restore_state_no_encoder(struct kunit *test)
{
	struct drm_device *drm = dm_test_alloc_drm(test);
	struct amdgpu_dm_connector *aconnector;

	aconnector = dm_test_add_connector(test, drm,
					   DRM_MODE_CONNECTOR_HDMIA);
	aconnector->dc_sink = kunit_kzalloc(test, 1, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector->dc_sink);
	aconnector->base.funcs->reset(&aconnector->base);
	KUNIT_ASSERT_NOT_NULL(test, aconnector->base.state);
	/* connector->encoder left NULL. */

	dm_restore_drm_connector_state(drm, &aconnector->base);

	KUNIT_EXPECT_NULL(test, aconnector->base.encoder);
}

/**
 * dm_test_restore_state_no_stream - Test a crtc without a stream bails out
 * @test: The KUnit test context
 *
 * The connector is routed to an encoder and crtc, but the crtc state carries no
 * dc stream, so the function returns before comparing sinks.
 */
static void dm_test_restore_state_no_stream(struct kunit *test)
{
	struct drm_device *drm = dm_test_alloc_drm(test);
	struct amdgpu_dm_connector *aconnector;
	struct dm_crtc_state *acrtc_state;
	struct drm_encoder *enc;
	struct drm_crtc *crtc;

	aconnector = dm_test_add_connector(test, drm,
					   DRM_MODE_CONNECTOR_HDMIA);
	aconnector->dc_sink = kunit_kzalloc(test, 1, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector->dc_sink);
	aconnector->base.funcs->reset(&aconnector->base);

	enc = kunit_kzalloc(test, sizeof(*enc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, enc);
	crtc = kunit_kzalloc(test, sizeof(*crtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, crtc);
	acrtc_state = kunit_kzalloc(test, sizeof(*acrtc_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, acrtc_state);

	crtc->state = &acrtc_state->base;
	enc->crtc = crtc;
	aconnector->base.encoder = enc;
	/* acrtc_state->stream left NULL. */

	dm_restore_drm_connector_state(drm, &aconnector->base);

	KUNIT_EXPECT_NULL(test, acrtc_state->stream);
}

/**
 * dm_test_restore_state_same_sink - Test an unchanged sink skips the commit
 * @test: The KUnit test context
 *
 * When the streamed sink already matches the connector's dc_sink there is
 * nothing to restore, so the forced atomic commit is not issued.
 */
static void dm_test_restore_state_same_sink(struct kunit *test)
{
	struct drm_device *drm = dm_test_alloc_drm(test);
	struct amdgpu_dm_connector *aconnector;
	struct dm_crtc_state *acrtc_state;
	struct dc_stream_state *stream;
	struct drm_encoder *enc;
	struct drm_crtc *crtc;

	aconnector = dm_test_add_connector(test, drm,
					   DRM_MODE_CONNECTOR_HDMIA);
	aconnector->dc_sink = kunit_kzalloc(test, 1, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector->dc_sink);
	aconnector->base.funcs->reset(&aconnector->base);

	enc = kunit_kzalloc(test, sizeof(*enc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, enc);
	crtc = kunit_kzalloc(test, sizeof(*crtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, crtc);
	acrtc_state = kunit_kzalloc(test, sizeof(*acrtc_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, acrtc_state);
	stream = dm_kunit_alloc_stream(test, NULL);
	KUNIT_ASSERT_NOT_NULL(test, stream);

	/* Same sink as the connector: the final branch is not taken. */
	stream->sink = aconnector->dc_sink;
	acrtc_state->stream = stream;
	crtc->state = &acrtc_state->base;
	enc->crtc = crtc;
	aconnector->base.encoder = enc;

	dm_restore_drm_connector_state(drm, &aconnector->base);

	KUNIT_EXPECT_PTR_EQ(test, stream->sink, aconnector->dc_sink);
}

/**
 * dm_test_get_amd_vsdb_unsupported - Test a zero VSDB version reports no support
 * @test: The KUnit test context
 */
static void dm_test_get_amd_vsdb_unsupported(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;
	struct amdgpu_hdmi_vsdb_info vsdb_info = {0};

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);

	aconnector->base.display_info.amd_vsdb.version = 0;
	aconnector->base.display_info.amd_vsdb.replay_mode = false;

	KUNIT_EXPECT_EQ(test, get_amd_vsdb(aconnector, &vsdb_info), 0);
	KUNIT_EXPECT_EQ(test, vsdb_info.amd_vsdb_version, 0);
}

/**
 * dm_test_get_amd_vsdb_supported - Test a non-zero VSDB version is reported
 * @test: The KUnit test context
 *
 * The display info's VSDB version and replay mode are copied out and a
 * non-zero version reports support.
 */
static void dm_test_get_amd_vsdb_supported(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;
	struct amdgpu_hdmi_vsdb_info vsdb_info = {0};

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);

	aconnector->base.display_info.amd_vsdb.version = 2;
	aconnector->base.display_info.amd_vsdb.replay_mode = true;

	KUNIT_EXPECT_EQ(test, get_amd_vsdb(aconnector, &vsdb_info), 1);
	KUNIT_EXPECT_EQ(test, vsdb_info.amd_vsdb_version, 2);
	KUNIT_EXPECT_TRUE(test, vsdb_info.replay_mode);
}

/**
 * dm_test_parse_displayid_vrr_null_edid - Test NULL EDID leaves range untouched
 * @test: The KUnit test context
 */
static void dm_test_parse_displayid_vrr_null_edid(struct kunit *test)
{
	struct drm_connector *connector;

	connector = kunit_kzalloc(test, sizeof(*connector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, connector);

	parse_edid_displayid_vrr(connector, NULL);

	KUNIT_EXPECT_EQ(test, connector->display_info.monitor_range.max_vfreq, 0);
	KUNIT_EXPECT_EQ(test, connector->display_info.monitor_range.min_vfreq, 0);
}

/**
 * dm_test_parse_displayid_vrr_no_displayid - Test EDID without a DisplayID ext
 * @test: The KUnit test context
 *
 * Without a DisplayID extension block there is no dynamic range to extract.
 */
static void dm_test_parse_displayid_vrr_no_displayid(struct kunit *test)
{
	struct drm_connector *connector;
	struct edid *edid;
	u8 *raw;

	connector = kunit_kzalloc(test, sizeof(*connector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, connector);
	raw = kunit_kzalloc(test, 2 * EDID_LENGTH, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, raw);
	edid = (struct edid *)raw;
	edid->extensions = 1;
	raw[EDID_LENGTH] = DM_TEST_CEA_EXT;

	parse_edid_displayid_vrr(connector, edid);

	KUNIT_EXPECT_EQ(test, connector->display_info.monitor_range.max_vfreq, 0);
}

/**
 * dm_test_parse_displayid_vrr_sets_range - Test a DisplayID VRR block is parsed
 * @test: The KUnit test context
 *
 * A DisplayID dynamic video timing range descriptor populates the connector's
 * monitor refresh range.
 */
static void dm_test_parse_displayid_vrr_sets_range(struct kunit *test)
{
	struct drm_connector *connector;
	struct edid *edid;
	u8 *raw, *ext;

	connector = kunit_kzalloc(test, sizeof(*connector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, connector);
	raw = kunit_kzalloc(test, 2 * EDID_LENGTH, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, raw);
	edid = (struct edid *)raw;
	edid->extensions = 1;

	ext = raw + EDID_LENGTH;
	ext[0] = DM_TEST_DISPLAYID_EXT;
	/*
	 * DisplayID dynamic video timing range descriptor, parsed from offset
	 * 1: tag 0x25, flags 0 (single-byte max), payload length 9, then the
	 * min/max vfreq bytes.
	 */
	ext[1] = 0x25;
	ext[2] = 0x00;
	ext[3] = 9;
	ext[10] = 40;
	ext[11] = 144;

	parse_edid_displayid_vrr(connector, edid);

	KUNIT_EXPECT_EQ(test, connector->display_info.monitor_range.min_vfreq, 40);
	KUNIT_EXPECT_EQ(test, connector->display_info.monitor_range.max_vfreq, 144);
}

/**
 * dm_test_mode_valid_interlace_rejected - Test interlaced modes are rejected
 * @test: The KUnit test context
 *
 * Interlaced modes are rejected up front with MODE_ERROR before any sink or
 * stream validation is attempted.
 */
static void dm_test_mode_valid_interlace_rejected(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;
	struct drm_display_mode *mode;

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);
	mode = kunit_kzalloc(test, sizeof(*mode), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, mode);

	mode->flags = DRM_MODE_FLAG_INTERLACE;

	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_connector_mode_valid(&aconnector->base, mode),
			MODE_ERROR);
}

/**
 * dm_test_mode_valid_dblscan_rejected - Test doublescan modes are rejected
 * @test: The KUnit test context
 *
 * Doublescan modes are rejected up front with MODE_ERROR.
 */
static void dm_test_mode_valid_dblscan_rejected(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;
	struct drm_display_mode *mode;

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);
	mode = kunit_kzalloc(test, sizeof(*mode), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, mode);

	mode->flags = DRM_MODE_FLAG_DBLSCAN;

	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_connector_mode_valid(&aconnector->base, mode),
			MODE_ERROR);
}

/**
 * dm_test_hdmi_cec_set_edid_no_notifier - Test the no-notifier no-op path
 * @test: The KUnit test context
 *
 * With aconnector->notifier NULL the function returns early and must not crash.
 */
static void dm_test_hdmi_cec_set_edid_no_notifier(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);

	amdgpu_dm_hdmi_cec_set_edid(aconnector);
}

/**
 * dm_test_s3_handle_hdmi_cec_suspend - Test the suspend pass over connectors
 * @test: The KUnit test context
 *
 * Suspend iterates all connectors, skipping writeback ones and calling the
 * unset path on the rest; with NULL notifiers this is a crash-free no-op.
 */
static void dm_test_s3_handle_hdmi_cec_suspend(struct kunit *test)
{
	struct drm_device *drm = dm_test_alloc_drm(test);

	dm_test_add_connector(test, drm, DRM_MODE_CONNECTOR_WRITEBACK);
	dm_test_add_connector(test, drm, DRM_MODE_CONNECTOR_HDMIA);

	amdgpu_dm_s3_handle_hdmi_cec(drm, true);
}

/**
 * dm_test_s3_handle_hdmi_cec_resume - Test the resume pass over connectors
 * @test: The KUnit test context
 *
 * Resume iterates all connectors, skipping writeback ones and calling the set
 * path on the rest; with NULL notifiers this is a crash-free no-op.
 */
static void dm_test_s3_handle_hdmi_cec_resume(struct kunit *test)
{
	struct drm_device *drm = dm_test_alloc_drm(test);

	dm_test_add_connector(test, drm, DRM_MODE_CONNECTOR_WRITEBACK);
	dm_test_add_connector(test, drm, DRM_MODE_CONNECTOR_HDMIA);

	amdgpu_dm_s3_handle_hdmi_cec(drm, false);
}

/**
 * dm_test_create_validate_stream_null_dm_state - Test NULL state returns NULL
 * @test: The KUnit test context
 *
 * Without a connector state there is nothing to validate against, so the
 * helper bails out with NULL before touching the dc handle.
 */
static void dm_test_create_validate_stream_null_dm_state(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);

	KUNIT_EXPECT_NULL(test,
			  amdgpu_dm_create_validate_stream_for_sink(&aconnector->base,
								    NULL, NULL, NULL));
}

/* Tests for amdgpu_dm_create_validate_stream_for_sink() */

/*
 * Build a connector embedded in an amdgpu_device (so drm_to_adev() resolves)
 * carrying a dc_link but a deliberately low atomic-requested bpc. Every
 * candidate colour depth then exceeds that cap, so bpc_mask ends up empty and
 * the enumeration returns NULL before create_stream_for_sink() and
 * dc_validate_stream() (and thus the unpopulated dc handle) are ever reached.
 * This lets the encoding/bpc mask-building branches be exercised on their own.
 */
struct dm_test_cvs_ctx {
	struct amdgpu_device *adev;
	struct drm_device *drm;
	struct amdgpu_dm_connector *aconnector;
	struct dc_link *link;
	struct dm_connector_state *dm_state;
	struct drm_display_mode *mode;
};

static struct dm_test_cvs_ctx *
dm_test_cvs_ctx_alloc(struct kunit *test, int connector_type)
{
	struct dm_test_cvs_ctx *ctx;
	struct device *dev;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);
	ctx->drm = __drm_kunit_helper_alloc_drm_device(test, dev,
			sizeof(*ctx->adev),
			offsetof(struct amdgpu_device, ddev),
			DRIVER_MODESET);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx->drm);
	ctx->adev = drm_to_adev(ctx->drm);

	ctx->aconnector = drmm_kzalloc(ctx->drm, sizeof(*ctx->aconnector),
			GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->aconnector);
	KUNIT_ASSERT_EQ(test,
			drmm_connector_init(ctx->drm, &ctx->aconnector->base,
			&dm_test_connector_funcs, connector_type,
			NULL), 0);

	ctx->link = kunit_kzalloc(test, sizeof(*ctx->link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->link);
	ctx->aconnector->dc_link = ctx->link;

	ctx->dm_state = kunit_kzalloc(test, sizeof(*ctx->dm_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->dm_state);
	ctx->dm_state->base.connector = &ctx->aconnector->base;
	ctx->dm_state->base.max_requested_bpc = 4;

	ctx->mode = kunit_kzalloc(test, sizeof(*ctx->mode), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->mode);
	ctx->mode->hdisplay = 1920;
	ctx->mode->vdisplay = 1080;

	return ctx;
}

/**
 * dm_test_create_validate_stream_writeback - Test the writeback stream path
 * @test: The KUnit test context
 *
 * A writeback connector has no sink EDID to enumerate, so the helper builds and
 * returns a single RGB stream directly instead of running the validation loop.
 */
static void dm_test_create_validate_stream_writeback(struct kunit *test)
{
	struct amdgpu_dm_wb_connector *wbcon;
	struct dm_connector_state *dm_state;
	struct drm_display_mode *mode;
	struct dc_stream_state *stream;
	struct amdgpu_device *adev;
	struct dc_context *dc_ctx;
	struct drm_device *drm;
	struct dc_link *link;
	struct device *dev;

	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);
	drm = __drm_kunit_helper_alloc_drm_device(test, dev, sizeof(*adev),
			offsetof(struct amdgpu_device, ddev),
			DRIVER_MODESET);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, drm);
	adev = drm_to_adev(drm);

	wbcon = drmm_kzalloc(drm, sizeof(*wbcon), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, wbcon);
	KUNIT_ASSERT_EQ(test,
			drmm_connector_init(drm, &wbcon->base.base,
			&dm_test_connector_funcs,
			DRM_MODE_CONNECTOR_WRITEBACK, NULL), 0);

	dc_ctx = kunit_kzalloc(test, sizeof(*dc_ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dc_ctx);
	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, link);
	link->ctx = dc_ctx;
	link->connector_signal = SIGNAL_TYPE_VIRTUAL;
	wbcon->link = link;

	dm_state = kunit_kzalloc(test, sizeof(*dm_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dm_state);
	dm_state->base.connector = &wbcon->base.base;
	dm_state->scaling = RMX_OFF;

	mode = kunit_kzalloc(test, sizeof(*mode), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, mode);
	mode->hdisplay = 1920;
	mode->vdisplay = 1080;
	mode->clock = 148500;

	stream = amdgpu_dm_create_validate_stream_for_sink(&wbcon->base.base,
							   mode, dm_state, NULL);
	KUNIT_ASSERT_NOT_NULL(test, stream);
	dc_stream_release(stream);
}

/**
 * dm_test_create_validate_stream_no_valid_bpc - Test the exhausted-mask path
 * @test: The KUnit test context
 *
 * On a plain DisplayPort sink using the default RGB encoding but a bpc cap below
 * every candidate depth, no (encoding, bpc) pair is attempted and the helper
 * returns NULL without touching the dc handle.
 */
static void dm_test_create_validate_stream_no_valid_bpc(struct kunit *test)
{
	struct dm_test_cvs_ctx *ctx =
		dm_test_cvs_ctx_alloc(test, DRM_MODE_CONNECTOR_DisplayPort);
	struct dc_stream_state *stream;

	ctx->link->connector_signal = SIGNAL_TYPE_DISPLAY_PORT;

	stream = amdgpu_dm_create_validate_stream_for_sink(&ctx->aconnector->base,
							   ctx->mode,
							   ctx->dm_state, NULL);
	KUNIT_EXPECT_NULL(test, stream);
}

/**
 * dm_test_create_validate_stream_hdmi_ycbcr - Test the HDMI encoding mask
 * @test: The KUnit test context
 *
 * A native HDMI sink advertising YCbCr 4:4:4 and 4:2:2 exercises HDMI endpoint
 * detection and the YCbCr444/YCbCr422 mask branches; the low bpc cap still
 * prunes every depth, so the helper returns NULL.
 */
static void dm_test_create_validate_stream_hdmi_ycbcr(struct kunit *test)
{
	struct dm_test_cvs_ctx *ctx =
		dm_test_cvs_ctx_alloc(test, DRM_MODE_CONNECTOR_HDMIA);
	struct dc_stream_state *stream;

	ctx->link->connector_signal = SIGNAL_TYPE_HDMI_TYPE_A;
	ctx->aconnector->base.display_info.color_formats =
		BIT(DRM_OUTPUT_COLOR_FORMAT_YCBCR444) |
		BIT(DRM_OUTPUT_COLOR_FORMAT_YCBCR422);

	stream = amdgpu_dm_create_validate_stream_for_sink(&ctx->aconnector->base,
							   ctx->mode,
							   ctx->dm_state, NULL);
	KUNIT_EXPECT_NULL(test, stream);
}

/**
 * dm_test_create_validate_stream_force_ycbcr420 - Test the forced YCbCr420 mask
 * @test: The KUnit test context
 *
 * force_yuv_pixel_format pins the encoding mask to YCbCr420 even on an RGB sink;
 * the low bpc cap prunes every depth, so the helper returns NULL.
 */
static void dm_test_create_validate_stream_force_ycbcr420(struct kunit *test)
{
	struct dm_test_cvs_ctx *ctx =
		dm_test_cvs_ctx_alloc(test, DRM_MODE_CONNECTOR_DisplayPort);
	struct dc_stream_state *stream;

	ctx->link->connector_signal = SIGNAL_TYPE_DISPLAY_PORT;
	ctx->aconnector->force_yuv_pixel_format = PIXEL_ENCODING_YCBCR420;

	stream = amdgpu_dm_create_validate_stream_for_sink(&ctx->aconnector->base,
							   ctx->mode,
							   ctx->dm_state, NULL);
	KUNIT_EXPECT_NULL(test, stream);
}

/**
 * dm_test_create_validate_stream_force_ycbcr422 - Test the forced YCbCr422 mask
 * @test: The KUnit test context
 *
 * With the sink advertising YCbCr 4:2:2, a force_yuv override pins the encoding
 * mask to YCbCr422; the low bpc cap prunes every depth, so the helper returns
 * NULL.
 */
static void dm_test_create_validate_stream_force_ycbcr422(struct kunit *test)
{
	struct dm_test_cvs_ctx *ctx =
		dm_test_cvs_ctx_alloc(test, DRM_MODE_CONNECTOR_DisplayPort);
	struct dc_stream_state *stream;

	ctx->link->connector_signal = SIGNAL_TYPE_DISPLAY_PORT;
	ctx->aconnector->force_yuv_pixel_format = PIXEL_ENCODING_YCBCR422;
	ctx->aconnector->base.display_info.color_formats =
		BIT(DRM_OUTPUT_COLOR_FORMAT_YCBCR422);

	stream = amdgpu_dm_create_validate_stream_for_sink(&ctx->aconnector->base,
							   ctx->mode,
							   ctx->dm_state, NULL);
	KUNIT_EXPECT_NULL(test, stream);
}

/**
 * dm_test_create_validate_stream_force_ycbcr444 - Test the forced YCbCr444 mask
 * @test: The KUnit test context
 *
 * On a native HDMI sink advertising YCbCr 4:4:4, a force_yuv override pins the
 * encoding mask to YCbCr444; the low bpc cap prunes every depth, so the helper
 * returns NULL.
 */
static void dm_test_create_validate_stream_force_ycbcr444(struct kunit *test)
{
	struct dm_test_cvs_ctx *ctx =
		dm_test_cvs_ctx_alloc(test, DRM_MODE_CONNECTOR_HDMIA);
	struct dc_stream_state *stream;

	ctx->link->connector_signal = SIGNAL_TYPE_HDMI_TYPE_A;
	ctx->aconnector->force_yuv_pixel_format = PIXEL_ENCODING_YCBCR444;
	ctx->aconnector->base.display_info.color_formats =
		BIT(DRM_OUTPUT_COLOR_FORMAT_YCBCR444);

	stream = amdgpu_dm_create_validate_stream_for_sink(&ctx->aconnector->base,
							   ctx->mode,
							   ctx->dm_state, NULL);
	KUNIT_EXPECT_NULL(test, stream);
}

/*
 * Drive the enumeration loop far enough to reach create_stream_for_sink() and
 * dc_validate_stream() by providing a valid bpc plus a minimal fake dc. The
 * connector carries a dc_link with a dc_context so a fake VIRTUAL sink and its
 * stream can be built; adev->dm.dc is wired with just enough resource_pool /
 * timing_generator / link_service state for dc_validate_stream() and
 * dm_validate_stream_and_context() to run without a real pipe allocator.
 */
struct dm_test_cvs_dc {
	struct amdgpu_device *adev;
	struct drm_device *drm;
	struct amdgpu_dm_connector *aconnector;
	struct dc_link *link;
	struct dm_connector_state *dm_state;
	struct drm_display_mode *mode;
	struct dc *dc;
	struct timing_generator_funcs *tgfuncs;
	struct link_service *link_srv;
};

static bool dm_test_cvs_validate_timing_fail(struct timing_generator *tg,
					     const struct dc_crtc_timing *timing)
{
	return false;
}

static bool dm_test_cvs_validate_timing_ok(struct timing_generator *tg,
					   const struct dc_crtc_timing *timing)
{
	return true;
}

static enum dc_status dm_test_cvs_validate_mode_timing_ok(
		const struct dc_stream_state *stream,
		struct dc_link *link,
		const struct dc_crtc_timing *timing)
{
	return DC_OK;
}

static struct dm_test_cvs_dc *dm_test_cvs_dc_alloc(struct kunit *test)
{
	struct timing_generator_funcs *tgfuncs;
	struct resource_funcs *rfuncs;
	struct resource_caps *rcaps;
	struct resource_pool *pool;
	struct timing_generator *tg;
	struct dc_context *dcc;
	struct dal_logger *logger;
	struct dm_test_cvs_dc *c;
	struct device *dev;

	c = kunit_kzalloc(test, sizeof(*c), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, c);

	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);
	c->drm = __drm_kunit_helper_alloc_drm_device(test, dev,
			sizeof(*c->adev),
			offsetof(struct amdgpu_device, ddev),
			DRIVER_MODESET);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, c->drm);
	c->adev = drm_to_adev(c->drm);

	c->aconnector = drmm_kzalloc(c->drm, sizeof(*c->aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, c->aconnector);
	KUNIT_ASSERT_EQ(test,
			drmm_connector_init(c->drm, &c->aconnector->base,
			&dm_test_connector_funcs,
			DRM_MODE_CONNECTOR_DisplayPort, NULL), 0);
	c->aconnector->base.display_info.bpc = 8;

	dcc = kunit_kzalloc(test, sizeof(*dcc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dcc);
	logger = kunit_kzalloc(test, sizeof(*logger), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, logger);
	logger->dev = c->drm;
	dcc->logger = logger;

	c->link = kunit_kzalloc(test, sizeof(*c->link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, c->link);
	c->link->ctx = dcc;
	c->link->connector_signal = SIGNAL_TYPE_DISPLAY_PORT;
	c->link->ep_type = DISPLAY_ENDPOINT_UNKNOWN;
	c->aconnector->dc_link = c->link;

	pool = kunit_kzalloc(test, sizeof(*pool), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, pool);
	rfuncs = kunit_kzalloc(test, sizeof(*rfuncs), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, rfuncs);
	rcaps = kunit_kzalloc(test, sizeof(*rcaps), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, rcaps);
	tg = kunit_kzalloc(test, sizeof(*tg), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, tg);
	tgfuncs = kunit_kzalloc(test, sizeof(*tgfuncs), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, tgfuncs);
	c->tgfuncs = tgfuncs;
	c->link_srv = kunit_kzalloc(test, sizeof(*c->link_srv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, c->link_srv);

	tg->funcs = tgfuncs;
	pool->funcs = rfuncs;
	pool->res_cap = rcaps;
	pool->timing_generators[0] = tg;
	pool->timing_generator_count = 0;

	c->dc = kunit_kzalloc(test, sizeof(*c->dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, c->dc);
	c->dc->ctx = dcc;
	c->dc->res_pool = pool;
	c->dc->link_srv = c->link_srv;
	c->adev->dm.dc = c->dc;

	c->dm_state = kunit_kzalloc(test, sizeof(*c->dm_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, c->dm_state);
	c->dm_state->base.connector = &c->aconnector->base;
	c->dm_state->base.max_requested_bpc = 8;
	c->dm_state->scaling = RMX_OFF;

	c->mode = kunit_kzalloc(test, sizeof(*c->mode), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, c->mode);
	c->mode->hdisplay = 1920;
	c->mode->vdisplay = 1080;
	c->mode->clock = 148500;

	return c;
}

/**
 * dm_test_create_validate_stream_prune_timing - Test the dc_validate_stream prune
 * @test: The KUnit test context
 *
 * A built stream fails dc_validate_stream() (timing_generator rejects the
 * timing), so every candidate is pruned and released and the helper returns
 * NULL once the enumeration is exhausted.
 */
static void dm_test_create_validate_stream_prune_timing(struct kunit *test)
{
	struct dm_test_cvs_dc *c = dm_test_cvs_dc_alloc(test);

	c->tgfuncs->validate_timing = dm_test_cvs_validate_timing_fail;

	KUNIT_EXPECT_NULL(test,
			  amdgpu_dm_create_validate_stream_for_sink(&c->aconnector->base,
								    c->mode,
								    c->dm_state,
								    NULL));
}

/**
 * dm_test_create_validate_stream_prune_context - Test the context-validation prune
 * @test: The KUnit test context
 *
 * dc_validate_stream() succeeds but dm_validate_stream_and_context() fails (no
 * pipe allocator), exercising the DC_OK sub-branches and MST check before the
 * candidate is pruned; the exhausted enumeration returns NULL.
 */
static void dm_test_create_validate_stream_prune_context(struct kunit *test)
{
	struct dm_test_cvs_dc *c = dm_test_cvs_dc_alloc(test);

	c->tgfuncs->validate_timing = dm_test_cvs_validate_timing_ok;
	c->link_srv->validate_mode_timing = dm_test_cvs_validate_mode_timing_ok;

	KUNIT_EXPECT_NULL(test,
			  amdgpu_dm_create_validate_stream_for_sink(&c->aconnector->base,
								    c->mode,
								    c->dm_state,
								    NULL));
}

static enum dc_pixel_encoding dm_test_cvs_first_encoding;

static bool dm_test_cvs_record_first_encoding(struct timing_generator *tg,
					      const struct dc_crtc_timing *timing)
{
	if (dm_test_cvs_first_encoding == PIXEL_ENCODING_UNDEFINED)
		dm_test_cvs_first_encoding = timing->pixel_encoding;

	return false;
}

/**
 * dm_test_create_validate_stream_hdmi_rgb_first - HDMI tries RGB before YCbCr 4:4:4
 * @test: The KUnit test context
 */
static void dm_test_create_validate_stream_hdmi_rgb_first(struct kunit *test)
{
	struct dm_test_cvs_dc *c = dm_test_cvs_dc_alloc(test);

	c->link->connector_signal = SIGNAL_TYPE_HDMI_TYPE_A;
	c->aconnector->base.display_info.color_formats =
		BIT(DRM_OUTPUT_COLOR_FORMAT_YCBCR444);
	c->tgfuncs->validate_timing = dm_test_cvs_record_first_encoding;
	dm_test_cvs_first_encoding = PIXEL_ENCODING_UNDEFINED;

	KUNIT_EXPECT_NULL(test,
			  amdgpu_dm_create_validate_stream_for_sink(&c->aconnector->base,
								    c->mode,
								    c->dm_state,
								    NULL));
	KUNIT_EXPECT_EQ(test, (int)dm_test_cvs_first_encoding, (int)PIXEL_ENCODING_RGB);
}

/* Further tests for amdgpu_dm_connector_mode_valid() */

/**
 * dm_test_mode_valid_no_dc_sink - Test the missing-sink rejection
 * @test: The KUnit test context
 *
 * With no dc_sink and an unforced connector there is nothing to validate
 * against, so mode_valid() logs and returns MODE_ERROR without reaching stream
 * creation.
 */
static void dm_test_mode_valid_no_dc_sink(struct kunit *test)
{
	struct drm_device *drm = dm_test_alloc_drm(test);
	struct amdgpu_dm_connector *aconnector =
		dm_test_add_connector(test, drm, DRM_MODE_CONNECTOR_HDMIA);
	struct drm_display_mode *mode;

	mode = kunit_kzalloc(test, sizeof(*mode), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, mode);

	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_connector_mode_valid(&aconnector->base, mode),
			MODE_ERROR);
}

/**
 * dm_test_mode_valid_force_on_no_stream - Test the forced no-stream rejection
 * @test: The KUnit test context
 *
 * A forced-on connector with dc_em_sink already set skips the EDID refresh and
 * the missing-sink bail, reaching stream creation; with no validatable depth no
 * stream is produced, so mode_valid() returns MODE_ERROR.
 */
static void dm_test_mode_valid_force_on_no_stream(struct kunit *test)
{
	struct dm_test_cvs_ctx *ctx =
		dm_test_cvs_ctx_alloc(test, DRM_MODE_CONNECTOR_DisplayPort);
	struct dc_sink *em_sink;

	em_sink = kunit_kzalloc(test, sizeof(*em_sink), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, em_sink);

	ctx->link->connector_signal = SIGNAL_TYPE_DISPLAY_PORT;
	ctx->aconnector->dc_em_sink = em_sink;
	ctx->aconnector->base.force = DRM_FORCE_ON;

	/* drm_connector_cleanup() kfree()s connector->state, so it must not
	 * point at KUnit-managed memory.
	 */
	amdgpu_dm_connector_funcs_reset(&ctx->aconnector->base);
	KUNIT_ASSERT_NOT_NULL(test, ctx->aconnector->base.state);
	ctx->aconnector->base.state->max_requested_bpc = 4;

	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_connector_mode_valid(&ctx->aconnector->base,
						       ctx->mode),
			MODE_ERROR);
}

/**
 * dm_test_mode_valid_edid_mgmt_forced - Test the forced-connector EDID refresh
 * @test: The KUnit test context
 *
 * A forced connector with no emulated sink runs the one-time EDID mgmt refresh
 * before validating; with no readable EDID no dc_sink appears and a non-on
 * force still bails to MODE_ERROR.
 */
static void dm_test_mode_valid_edid_mgmt_forced(struct kunit *test)
{
	struct dm_test_edid_ctx *ctx =
		dm_test_edid_ctx_alloc(test, DRM_MODE_CONNECTOR_DisplayPort);
	struct drm_display_mode *mode;

	ctx->link->connector_signal = SIGNAL_TYPE_DISPLAY_PORT;

	mode = kunit_kzalloc(test, sizeof(*mode), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, mode);

	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_connector_mode_valid(&ctx->aconnector->base, mode),
			MODE_ERROR);
}

/* Tests for amdgpu_dm_connector_atomic_check() */

/*
 * Build a single-connector atomic commit for amdgpu_dm_connector_atomic_check().
 * The connector lives at index 0 so the hand-rolled connectors[] slot resolves
 * both the old and new connector state; leaving crtc NULL keeps the check off
 * the crtc-state machinery that a real atomic path would require.
 */
struct dm_test_conn_ac_ctx {
	struct amdgpu_dm_connector *aconn;
	struct drm_atomic_commit *state;
	struct drm_connector_state *old_state;
	struct drm_connector_state *new_state;
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
};

static struct dm_test_conn_ac_ctx *
dm_test_conn_ac_ctx_alloc(struct kunit *test, int connector_type)
{
	struct dm_test_conn_ac_ctx *ctx;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	ctx->aconn = kunit_kzalloc(test, sizeof(*ctx->aconn), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->aconn);
	ctx->old_state = kunit_kzalloc(test, sizeof(*ctx->old_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->old_state);
	ctx->new_state = kunit_kzalloc(test, sizeof(*ctx->new_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->new_state);

	ctx->state = dm_test_alloc_atomic_state(test, 1);

	ctx->aconn->base.connector_type = connector_type;
	ctx->new_state->connector = &ctx->aconn->base;
	ctx->new_state->state = ctx->state;
	ctx->old_state->connector = &ctx->aconn->base;

	ctx->state->connectors[0].ptr = &ctx->aconn->base;
	ctx->state->connectors[0].old_state = ctx->old_state;
	ctx->state->connectors[0].new_state = ctx->new_state;

	return ctx;
}

/**
 * dm_test_conn_atomic_check_no_crtc - Test the unbound-connector short-circuit
 * @test: The KUnit test context
 *
 * A non-DisplayPort connector with no crtc has nothing to validate and returns
 * 0 immediately.
 */
static void dm_test_conn_atomic_check_no_crtc(struct kunit *test)
{
	struct dm_test_conn_ac_ctx *ctx =
		dm_test_conn_ac_ctx_alloc(test, DRM_MODE_CONNECTOR_HDMIA);
	int ret;

	ret = amdgpu_dm_connector_atomic_check(&ctx->aconn->base, ctx->state);

	KUNIT_EXPECT_EQ(test, ret, 0);
}

/**
 * dm_test_conn_atomic_check_dp_mst - Test the DisplayPort MST root check
 * @test: The KUnit test context
 *
 * A DisplayPort connector runs the MST root atomic check; with no crtc bound on
 * either state it finds nothing to reserve and returns 0.
 */
static void dm_test_conn_atomic_check_dp_mst(struct kunit *test)
{
	struct dm_test_conn_ac_ctx *ctx =
		dm_test_conn_ac_ctx_alloc(test, DRM_MODE_CONNECTOR_DisplayPort);
	int ret;

	ret = amdgpu_dm_connector_atomic_check(&ctx->aconn->base, ctx->state);

	KUNIT_EXPECT_EQ(test, ret, 0);
}

/**
 * dm_test_conn_atomic_check_no_change - Test the bound-but-unchanged path
 * @test: The KUnit test context
 *
 * With a crtc bound but no privacy/colorspace/content-type/HDR differences the
 * check skips every modeset trigger and returns 0 without touching the crtc
 * state.
 */
static void dm_test_conn_atomic_check_no_change(struct kunit *test)
{
	struct dm_test_conn_ac_ctx *ctx =
		dm_test_conn_ac_ctx_alloc(test, DRM_MODE_CONNECTOR_HDMIA);
	struct drm_crtc *crtc;
	int ret;

	crtc = kunit_kzalloc(test, sizeof(*crtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, crtc);

	ctx->new_state->crtc = crtc;

	ret = amdgpu_dm_connector_atomic_check(&ctx->aconn->base, ctx->state);

	KUNIT_EXPECT_EQ(test, ret, 0);
}

/*
 * Bind a crtc to the commit with a pre-populated crtc slot so
 * drm_atomic_get_crtc_state() returns @crtc_state via its new-state fast path
 * rather than locking and duplicating through absent crtc funcs.
 */
static void dm_test_conn_ac_bind_crtc(struct kunit *test,
				      struct dm_test_conn_ac_ctx *ctx)
{
	ctx->crtc = kunit_kzalloc(test, sizeof(*ctx->crtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->crtc);
	ctx->crtc_state = kunit_kzalloc(test, sizeof(*ctx->crtc_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->crtc_state);

	ctx->state->crtcs = kunit_kcalloc(test, 1, sizeof(*ctx->state->crtcs),
					  GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->state->crtcs);
	ctx->state->crtcs[0].ptr = ctx->crtc;
	ctx->state->crtcs[0].new_state = ctx->crtc_state;

	/* Only needs to be non-NULL to satisfy the get_crtc_state() WARN. */
	ctx->state->acquire_ctx = (void *)ctx;

	ctx->new_state->crtc = ctx->crtc;
}

/**
 * dm_test_conn_atomic_check_privacy_change - Test privacy toggle forces modeset
 * @test: The KUnit test context
 *
 * A changed privacy-screen software state pulls in the crtc state and flags a
 * modeset.
 */
static void dm_test_conn_atomic_check_privacy_change(struct kunit *test)
{
	struct dm_test_conn_ac_ctx *ctx =
		dm_test_conn_ac_ctx_alloc(test, DRM_MODE_CONNECTOR_HDMIA);
	int ret;

	dm_test_conn_ac_bind_crtc(test, ctx);
	ctx->new_state->privacy_screen_sw_state = PRIVACY_SCREEN_ENABLED;

	ret = amdgpu_dm_connector_atomic_check(&ctx->aconn->base, ctx->state);

	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_TRUE(test, ctx->crtc_state->mode_changed);
}

/**
 * dm_test_conn_atomic_check_colorspace_change - Test colorspace change forces modeset
 * @test: The KUnit test context
 *
 * A changed output colorspace pulls in the crtc state and flags a modeset.
 */
static void dm_test_conn_atomic_check_colorspace_change(struct kunit *test)
{
	struct dm_test_conn_ac_ctx *ctx =
		dm_test_conn_ac_ctx_alloc(test, DRM_MODE_CONNECTOR_HDMIA);
	int ret;

	dm_test_conn_ac_bind_crtc(test, ctx);
	ctx->new_state->colorspace = DRM_MODE_COLORIMETRY_BT2020_RGB;

	ret = amdgpu_dm_connector_atomic_check(&ctx->aconn->base, ctx->state);

	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_TRUE(test, ctx->crtc_state->mode_changed);
}

/**
 * dm_test_conn_atomic_check_content_type_change - Test content-type change forces modeset
 * @test: The KUnit test context
 *
 * A changed content type pulls in the crtc state and flags a modeset.
 */
static void dm_test_conn_atomic_check_content_type_change(struct kunit *test)
{
	struct dm_test_conn_ac_ctx *ctx =
		dm_test_conn_ac_ctx_alloc(test, DRM_MODE_CONNECTOR_HDMIA);
	int ret;

	dm_test_conn_ac_bind_crtc(test, ctx);
	ctx->new_state->content_type = DRM_MODE_CONTENT_TYPE_GRAPHICS;

	ret = amdgpu_dm_connector_atomic_check(&ctx->aconn->base, ctx->state);

	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_TRUE(test, ctx->crtc_state->mode_changed);
}

/**
 * dm_test_conn_atomic_check_hdr_exit - Test exiting HDR forces a modeset
 * @test: The KUnit test context
 *
 * Clearing previously set HDR metadata makes the metadata unequal; the fill
 * succeeds for the empty new state and the enter/exit rule flags a modeset.
 */
static void dm_test_conn_atomic_check_hdr_exit(struct kunit *test)
{
	struct dm_test_conn_ac_ctx *ctx =
		dm_test_conn_ac_ctx_alloc(test, DRM_MODE_CONNECTOR_HDMIA);
	struct drm_property_blob *blob;
	int ret;

	blob = kunit_kzalloc(test, sizeof(*blob), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, blob);

	dm_test_conn_ac_bind_crtc(test, ctx);
	ctx->old_state->hdr_output_metadata = blob;

	ret = amdgpu_dm_connector_atomic_check(&ctx->aconn->base, ctx->state);

	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_TRUE(test, ctx->crtc_state->mode_changed);
}

/**
 * dm_test_conn_atomic_check_hdr_fill_error - Test infopacket fill errors propagate
 * @test: The KUnit test context
 *
 * A new HDR metadata blob with no payload makes the infopacket fill fail, and
 * that error is returned before the crtc state is touched.
 */
static void dm_test_conn_atomic_check_hdr_fill_error(struct kunit *test)
{
	struct dm_test_conn_ac_ctx *ctx =
		dm_test_conn_ac_ctx_alloc(test, DRM_MODE_CONNECTOR_HDMIA);
	struct drm_property_blob *blob;
	int ret;

	blob = kunit_kzalloc(test, sizeof(*blob), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, blob);

	dm_test_conn_ac_bind_crtc(test, ctx);
	ctx->new_state->hdr_output_metadata = blob;

	ret = amdgpu_dm_connector_atomic_check(&ctx->aconn->base, ctx->state);

	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

/**
 * dm_test_update_after_detect_mst_noop - Test MST connectors are left to drm_mst
 * @test: The KUnit test context
 *
 * An MST connector is handled by the drm_mst framework, so the function
 * returns immediately and never dereferences the (NULL) dc_link.
 */
static void dm_test_update_after_detect_mst_noop(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);

	aconnector->mst_mgr.mst_state = true;

	amdgpu_dm_update_connector_after_detect(aconnector);
}

/**
 * dm_test_update_after_detect_sink_unchanged - Test the short-pulse no-op path
 * @test: The KUnit test context
 *
 * When the link reports no local sink and the connector already has no
 * dc_sink, the "sink didn't change" path returns without touching DC.
 */
static void dm_test_update_after_detect_sink_unchanged(struct kunit *test)
{
	struct drm_device *drm = dm_test_alloc_drm(test);
	struct amdgpu_dm_connector *aconnector;
	struct dc_link *link;

	aconnector = dm_test_add_connector(test, drm, DRM_MODE_CONNECTOR_HDMIA);
	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, link);

	aconnector->dc_link = link;

	/* link->local_sink and aconnector->dc_sink are both NULL. */
	amdgpu_dm_update_connector_after_detect(aconnector);

	KUNIT_EXPECT_NULL(test, aconnector->dc_sink);
}

/* Tests for amdgpu_dm_update_connector_after_detect() */

/*
 * A minimal but structurally valid 128-byte EDID base block (correct header
 * and checksum) so drm_edid_alloc()/drm_edid_connector_update() accept it when
 * exercising the "sink carries EDID" branch.
 */
static const u8 dm_test_uad_edid[128] = {
	0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x04, 0x21, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x04, 0x80, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5c,
};

/*
 * Build an amdgpu_dm_connector registered against a real kunit drm_device that
 * is embedded in an amdgpu_device, so drm_to_adev()/adev_to_drm() resolve for
 * amdgpu_dm_update_connector_after_detect() and all of its helpers.
 *
 * bl_idx is forced to -1 and adev->dm.freesync_module is left NULL so the
 * backlight, CEC and freesync helpers take their early-return paths and the
 * test stays focused on the sink-adoption logic.
 */
struct dm_test_uad_ctx {
	struct amdgpu_device *adev;
	struct drm_device *drm;
	struct amdgpu_dm_connector *aconnector;
	struct dc_link *link;
};

static struct dm_test_uad_ctx *
dm_test_uad_ctx_alloc(struct kunit *test, int connector_type)
{
	struct dm_test_uad_ctx *ctx;
	struct device *dev;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	ctx->drm = __drm_kunit_helper_alloc_drm_device(test, dev,
			sizeof(*ctx->adev),
			offsetof(struct amdgpu_device, ddev),
			DRIVER_MODESET);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx->drm);
	ctx->adev = drm_to_adev(ctx->drm);

	ctx->aconnector = drmm_kzalloc(ctx->drm, sizeof(*ctx->aconnector),
			GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->aconnector);
	KUNIT_ASSERT_EQ(test,
			drmm_connector_init(ctx->drm, &ctx->aconnector->base,
			&dm_test_connector_funcs, connector_type,
			NULL), 0);

	ctx->link = kunit_kzalloc(test, sizeof(*ctx->link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->link);
	ctx->aconnector->dc_link = ctx->link;

	/* Keep the backlight/CEC/freesync helpers on their early-return paths. */
	ctx->aconnector->bl_idx = -1;

	return ctx;
}

/* A real (non-kunit) sink the function is expected to free via dc_sink_release. */
static struct dc_sink *dm_test_uad_owned_sink(void)
{
	struct dc_sink *sink = kzalloc_obj(*sink, GFP_KERNEL);

	if (sink)
		kref_init(&sink->refcount);
	return sink;
}

/* A kunit-managed sink that stays referenced (never released to zero). */
static struct dc_sink *dm_test_uad_kept_sink(struct kunit *test)
{
	struct dc_sink *sink = kunit_kzalloc(test, sizeof(*sink), GFP_KERNEL);

	if (sink)
		kref_init(&sink->refcount);
	return sink;
}

/**
 * dm_test_update_after_detect_mst_sink - Test an MST sink is left to drm_mst
 * @test: The KUnit test context
 *
 * A local sink reporting SIGNAL_TYPE_DISPLAY_PORT_MST is handled by the
 * drm_mst framework, so the function returns before adopting it.
 */
static void dm_test_update_after_detect_mst_sink(struct kunit *test)
{
	struct dm_test_uad_ctx *ctx =
		dm_test_uad_ctx_alloc(test, DRM_MODE_CONNECTOR_HDMIA);
	struct dc_sink *sink = dm_test_uad_kept_sink(test);

	KUNIT_ASSERT_NOT_NULL(test, sink);
	sink->sink_signal = SIGNAL_TYPE_DISPLAY_PORT_MST;
	ctx->link->local_sink = sink;

	amdgpu_dm_update_connector_after_detect(ctx->aconnector);

	KUNIT_EXPECT_NULL(test, ctx->aconnector->dc_sink);
}

/**
 * dm_test_update_after_detect_connect_edid - Test adopting a new sink with EDID
 * @test: The KUnit test context
 *
 * A freshly detected DisplayPort sink carrying EDID is adopted: the connector
 * takes the sink, allocates a drm_edid and a requested-timing structure.
 */
static void dm_test_update_after_detect_connect_edid(struct kunit *test)
{
	struct dm_test_uad_ctx *ctx =
		dm_test_uad_ctx_alloc(test, DRM_MODE_CONNECTOR_HDMIA);
	struct dc_sink *sink = dm_test_uad_kept_sink(test);

	KUNIT_ASSERT_NOT_NULL(test, sink);
	sink->sink_signal = SIGNAL_TYPE_DISPLAY_PORT;
	memcpy(sink->dc_edid.raw_edid, dm_test_uad_edid, sizeof(dm_test_uad_edid));
	sink->dc_edid.length = sizeof(dm_test_uad_edid);
	ctx->link->local_sink = sink;
	ctx->link->aux_mode = true;

	amdgpu_dm_update_connector_after_detect(ctx->aconnector);

	KUNIT_EXPECT_PTR_EQ(test, ctx->aconnector->dc_sink, sink);
	KUNIT_EXPECT_NOT_NULL(test, ctx->aconnector->timing_requested);

	kfree(ctx->aconnector->timing_requested);
	ctx->aconnector->timing_requested = NULL;
}

/**
 * dm_test_update_after_detect_replace_no_edid - Test replacing a sink with no
 * EDID and HDMI compression auto
 * @test: The KUnit test context
 *
 * When a new EDID-less sink replaces an existing one, the old sink is released
 * and, with hdmi_comp_auto set, an HDMI sink signal is promoted to FRL.
 */
static void dm_test_update_after_detect_replace_no_edid(struct kunit *test)
{
	struct dm_test_uad_ctx *ctx =
		dm_test_uad_ctx_alloc(test, DRM_MODE_CONNECTOR_HDMIA);
	struct dc_sink *old_sink = dm_test_uad_owned_sink();
	struct dc_sink *new_sink = dm_test_uad_kept_sink(test);

	KUNIT_ASSERT_NOT_NULL(test, old_sink);
	KUNIT_ASSERT_NOT_NULL(test, new_sink);

	ctx->aconnector->dc_sink = old_sink;
	new_sink->sink_signal = SIGNAL_TYPE_HDMI_TYPE_A;
	new_sink->dc_edid.length = 0;
	ctx->link->local_sink = new_sink;
	ctx->link->aux_mode = true;
	ctx->aconnector->hdmi_comp_auto = true;

	amdgpu_dm_update_connector_after_detect(ctx->aconnector);

	KUNIT_EXPECT_PTR_EQ(test, ctx->aconnector->dc_sink, new_sink);
	KUNIT_EXPECT_EQ(test, (int)new_sink->sink_signal,
			(int)SIGNAL_TYPE_HDMI_FRL);
}

/**
 * dm_test_update_after_detect_disconnect - Test tearing down on unplug
 * @test: The KUnit test context
 *
 * With no local sink but an existing dc_sink, the disconnect path releases the
 * sink, clears modes/timing, downgrades content protection and notifies audio.
 */
static void dm_test_update_after_detect_disconnect(struct kunit *test)
{
	struct dm_test_uad_ctx *ctx =
		dm_test_uad_ctx_alloc(test, DRM_MODE_CONNECTOR_HDMIA);
	struct dc_sink *old_sink = dm_test_uad_owned_sink();
	struct dc_crtc_timing *timing = kzalloc_obj(*timing, GFP_KERNEL);

	KUNIT_ASSERT_NOT_NULL(test, old_sink);
	KUNIT_ASSERT_NOT_NULL(test, timing);

	mutex_init(&ctx->adev->dm.audio_lock);
	ctx->aconnector->dc_sink = old_sink;
	ctx->aconnector->timing_requested = timing;
	ctx->aconnector->num_modes = 3;
	ctx->aconnector->audio_inst = 5;
	ctx->link->local_sink = NULL;

	amdgpu_dm_connector_funcs_reset(&ctx->aconnector->base);
	KUNIT_ASSERT_NOT_NULL(test, ctx->aconnector->base.state);
	ctx->aconnector->base.state->content_protection =
		DRM_MODE_CONTENT_PROTECTION_ENABLED;

	amdgpu_dm_update_connector_after_detect(ctx->aconnector);

	KUNIT_EXPECT_NULL(test, ctx->aconnector->dc_sink);
	KUNIT_EXPECT_NULL(test, ctx->aconnector->timing_requested);
	KUNIT_EXPECT_EQ(test, ctx->aconnector->num_modes, 0);
	KUNIT_EXPECT_EQ(test, ctx->aconnector->audio_inst, -1);
	KUNIT_EXPECT_EQ(test,
			(int)ctx->aconnector->base.state->content_protection,
			(int)DRM_MODE_CONTENT_PROTECTION_DESIRED);
}

/**
 * dm_test_update_after_detect_force_em_adopt - Test forced eml_sink adoption
 * @test: The KUnit test context
 *
 * A forced connector with an emulated sink adopts a newly reported local sink
 * under the mode_config lock.
 */
static void dm_test_update_after_detect_force_em_adopt(struct kunit *test)
{
	struct dm_test_uad_ctx *ctx =
		dm_test_uad_ctx_alloc(test, DRM_MODE_CONNECTOR_HDMIA);
	struct dc_sink *em_sink = dm_test_uad_kept_sink(test);
	struct dc_sink *sink = dm_test_uad_kept_sink(test);

	KUNIT_ASSERT_NOT_NULL(test, em_sink);
	KUNIT_ASSERT_NOT_NULL(test, sink);

	ctx->aconnector->base.force = DRM_FORCE_ON;
	ctx->aconnector->dc_em_sink = em_sink;
	ctx->link->local_sink = sink;

	amdgpu_dm_update_connector_after_detect(ctx->aconnector);

	KUNIT_EXPECT_PTR_EQ(test, ctx->aconnector->dc_sink, sink);
}

/**
 * dm_test_update_after_detect_force_em_replace - Test forced eml_sink replace
 * @test: The KUnit test context
 *
 * A forced connector that already has a dc_sink releases it before adopting the
 * newly reported local sink.
 */
static void dm_test_update_after_detect_force_em_replace(struct kunit *test)
{
	struct dm_test_uad_ctx *ctx =
		dm_test_uad_ctx_alloc(test, DRM_MODE_CONNECTOR_HDMIA);
	struct dc_sink *em_sink = dm_test_uad_kept_sink(test);
	struct dc_sink *old_sink = dm_test_uad_owned_sink();
	struct dc_sink *new_sink = dm_test_uad_kept_sink(test);

	KUNIT_ASSERT_NOT_NULL(test, em_sink);
	KUNIT_ASSERT_NOT_NULL(test, old_sink);
	KUNIT_ASSERT_NOT_NULL(test, new_sink);

	ctx->aconnector->base.force = DRM_FORCE_ON;
	ctx->aconnector->dc_em_sink = em_sink;
	ctx->aconnector->dc_sink = old_sink;
	ctx->link->local_sink = new_sink;

	amdgpu_dm_update_connector_after_detect(ctx->aconnector);

	KUNIT_EXPECT_PTR_EQ(test, ctx->aconnector->dc_sink, new_sink);
}

/**
 * dm_test_update_after_detect_force_em_fake - Test forced fallback to eml_sink
 * @test: The KUnit test context
 *
 * A forced connector with no local sink and no dc_sink falls back to using the
 * emulated sink so a headless stream can still be faked.
 */
static void dm_test_update_after_detect_force_em_fake(struct kunit *test)
{
	struct dm_test_uad_ctx *ctx =
		dm_test_uad_ctx_alloc(test, DRM_MODE_CONNECTOR_HDMIA);
	struct dc_sink *em_sink = dm_test_uad_kept_sink(test);

	KUNIT_ASSERT_NOT_NULL(test, em_sink);

	ctx->aconnector->base.force = DRM_FORCE_ON;
	ctx->aconnector->dc_em_sink = em_sink;
	ctx->link->local_sink = NULL;

	amdgpu_dm_update_connector_after_detect(ctx->aconnector);

	KUNIT_EXPECT_PTR_EQ(test, ctx->aconnector->dc_sink, em_sink);
}

/**
 * dm_test_update_after_detect_force_em_keep - Test forced no-sink keeps dc_sink
 * @test: The KUnit test context
 *
 * A forced connector with no local sink but an existing dc_sink keeps that sink
 * (the emulated-sink fallback is skipped).
 */
static void dm_test_update_after_detect_force_em_keep(struct kunit *test)
{
	struct dm_test_uad_ctx *ctx =
		dm_test_uad_ctx_alloc(test, DRM_MODE_CONNECTOR_HDMIA);
	struct dc_sink *em_sink = dm_test_uad_kept_sink(test);
	struct dc_sink *dc_sink = dm_test_uad_kept_sink(test);

	KUNIT_ASSERT_NOT_NULL(test, em_sink);
	KUNIT_ASSERT_NOT_NULL(test, dc_sink);

	ctx->aconnector->base.force = DRM_FORCE_ON;
	ctx->aconnector->dc_em_sink = em_sink;
	ctx->aconnector->dc_sink = dc_sink;
	ctx->link->local_sink = NULL;

	amdgpu_dm_update_connector_after_detect(ctx->aconnector);

	KUNIT_EXPECT_PTR_EQ(test, ctx->aconnector->dc_sink, dc_sink);
}

/* Tests for amdgpu_dm_connector_funcs_force() with a valid EDID */

/*
 * Fake i2c adapter that serves dm_test_uad_edid over the DDC read protocol
 * drm_do_probe_ddc_edid() uses: a one-byte write to DDC_ADDR (0x50) sets the
 * block offset and the following read returns bytes from that offset. This
 * lets amdgpu_dm_connector_funcs_force() obtain a valid EDID without hardware.
 */
static int dm_test_force_edid_i2c_xfer(struct i2c_adapter *adap,
				       struct i2c_msg *msgs, int num)
{
	u8 offset = 0;
	int i;

	for (i = 0; i < num; i++) {
		if (msgs[i].flags & I2C_M_RD) {
			size_t len = msgs[i].len;

			if (offset >= sizeof(dm_test_uad_edid))
				len = 0;
			else if (len > sizeof(dm_test_uad_edid) - offset)
				len = sizeof(dm_test_uad_edid) - offset;
			memcpy(msgs[i].buf, dm_test_uad_edid + offset, len);
		} else if (msgs[i].addr == 0x50 && msgs[i].len >= 1) {
			offset = msgs[i].buf[0];
		}
	}

	return num;
}

static u32 dm_test_force_edid_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C;
}

static const struct i2c_algorithm dm_test_force_edid_i2c_algo = {
	.master_xfer = dm_test_force_edid_i2c_xfer,
	.functionality = dm_test_force_edid_i2c_func,
};

static void dm_test_force_edid_lock_bus(struct i2c_adapter *adap, unsigned int flags) {}
static int dm_test_force_edid_trylock_bus(struct i2c_adapter *adap, unsigned int flags)
{
	return 1;
}
static void dm_test_force_edid_unlock_bus(struct i2c_adapter *adap, unsigned int flags) {}

static const struct i2c_lock_operations dm_test_force_edid_lock_ops = {
	.lock_bus = dm_test_force_edid_lock_bus,
	.trylock_bus = dm_test_force_edid_trylock_bus,
	.unlock_bus = dm_test_force_edid_unlock_bus,
};

/**
 * dm_test_funcs_force_reads_edid - Test force() caches EDID and updates em_sink
 * @test: The KUnit test context
 *
 * A non-AUX link selects the connector's i2c adapter, which serves a valid
 * EDID. force() must cache it in drm_edid and, because an emulated sink and a
 * dc_link are present, copy the raw EDID into the sink and parse its caps.
 */
static void dm_test_funcs_force_reads_edid(struct kunit *test)
{
	struct dm_test_edid_ctx *ctx =
		dm_test_edid_ctx_alloc(test, DRM_MODE_CONNECTOR_DisplayPort);
	struct amdgpu_i2c_adapter *i2c;
	struct dc_sink *em_sink;

	i2c = kunit_kzalloc(test, sizeof(*i2c), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, i2c);
	i2c->base.algo = &dm_test_force_edid_i2c_algo;
	i2c->base.lock_ops = &dm_test_force_edid_lock_ops;
	ctx->aconnector->i2c = i2c;

	em_sink = kunit_kzalloc(test, sizeof(*em_sink), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, em_sink);
	ctx->aconnector->dc_em_sink = em_sink;

	/* Non-AUX link picks the i2c adapter; force-on skips the DDC probe. */
	ctx->link->aux_mode = false;
	ctx->link->priv = ctx->aconnector;
	ctx->aconnector->base.force = DRM_FORCE_ON;

	amdgpu_dm_connector_funcs_force(&ctx->aconnector->base);

	KUNIT_EXPECT_NOT_NULL(test, ctx->aconnector->drm_edid);
	KUNIT_EXPECT_MEMEQ(test, em_sink->dc_edid.raw_edid,
			   dm_test_uad_edid, sizeof(dm_test_uad_edid));

	drm_edid_free(ctx->aconnector->drm_edid);
	ctx->aconnector->drm_edid = NULL;
}

/* Tests for create_eml_sink() with a valid EDID */

/*
 * create_eml_sink() reads the EDID off the connector DDC and, on success,
 * installs an emulated sink from link_srv->add_remote_sink(). A non-AUX link
 * selects the connector i2c adapter (serving dm_test_uad_edid) and a fake
 * add_remote_sink() returns dm_test_ces_remote_sink, so the full EDID path
 * runs without real hardware or DC.
 */
static struct dc_sink *dm_test_ces_remote_sink;

static struct dc_sink *
dm_test_ces_add_remote_sink(struct dc_link *link, const uint8_t *edid,
			    unsigned int len, struct dc_sink_init_data *init_data)
{
	return dm_test_ces_remote_sink;
}

static void dm_test_ces_setup(struct kunit *test, struct dm_test_edid_ctx *ctx,
			      struct dc_sink *em_sink)
{
	struct amdgpu_i2c_adapter *i2c;
	struct link_service *link_srv;
	struct dc *dc;

	i2c = kunit_kzalloc(test, sizeof(*i2c), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, i2c);
	i2c->base.algo = &dm_test_force_edid_i2c_algo;
	i2c->base.lock_ops = &dm_test_force_edid_lock_ops;
	ctx->aconnector->i2c = i2c;

	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dc);
	link_srv = kunit_kzalloc(test, sizeof(*link_srv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, link_srv);
	link_srv->add_remote_sink = dm_test_ces_add_remote_sink;
	dc->link_srv = link_srv;

	ctx->link->dc = dc;
	ctx->link->aux_mode = false;
	dm_test_ces_remote_sink = em_sink;
}

/**
 * dm_test_create_eml_sink_reads_edid - Test the valid-EDID path builds a sink
 * @test: The KUnit test context
 *
 * With a readable EDID and force unspecified, create_eml_sink() caches the EDID
 * in drm_edid and installs the emulated sink while leaving dc_sink untouched.
 */
static void dm_test_create_eml_sink_reads_edid(struct kunit *test)
{
	struct dm_test_edid_ctx *ctx =
	dm_test_edid_ctx_alloc(test, DRM_MODE_CONNECTOR_DisplayPort);
	struct dc_sink *em_sink;

	em_sink = kunit_kzalloc(test, sizeof(*em_sink), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, em_sink);
	dm_test_ces_setup(test, ctx, em_sink);
	ctx->aconnector->base.force = DRM_FORCE_UNSPECIFIED;

	create_eml_sink(ctx->aconnector);

	KUNIT_EXPECT_NOT_NULL(test, ctx->aconnector->drm_edid);
	KUNIT_EXPECT_PTR_EQ(test, ctx->aconnector->dc_em_sink, em_sink);
	KUNIT_EXPECT_NULL(test, ctx->aconnector->dc_sink);

	drm_edid_free(ctx->aconnector->drm_edid);
	ctx->aconnector->drm_edid = NULL;
}

/**
 * dm_test_create_eml_sink_force_on_em - Test force-on adopts the emulated sink
 * @test: The KUnit test context
 *
 * With DRM_FORCE_ON and no local sink, create_eml_sink() adopts the emulated
 * sink as dc_sink and retains it.
 */
static void dm_test_create_eml_sink_force_on_em(struct kunit *test)
{
	struct dm_test_edid_ctx *ctx =
	dm_test_edid_ctx_alloc(test, DRM_MODE_CONNECTOR_DisplayPort);
	struct dc_sink *em_sink;

	em_sink = kunit_kzalloc(test, sizeof(*em_sink), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, em_sink);
	kref_init(&em_sink->refcount);
	dm_test_ces_setup(test, ctx, em_sink);
	ctx->link->local_sink = NULL;
	ctx->aconnector->base.force = DRM_FORCE_ON;

	create_eml_sink(ctx->aconnector);

	KUNIT_EXPECT_PTR_EQ(test, ctx->aconnector->dc_sink, em_sink);
	KUNIT_EXPECT_EQ(test, kref_read(&em_sink->refcount), 2);

	drm_edid_free(ctx->aconnector->drm_edid);
	ctx->aconnector->drm_edid = NULL;
}

/**
 * dm_test_create_eml_sink_force_on_local - Test force-on prefers the local sink
 * @test: The KUnit test context
 *
 * With DRM_FORCE_ON and a local sink present, create_eml_sink() adopts the
 * local sink as dc_sink instead of the emulated one and retains it.
 */
static void dm_test_create_eml_sink_force_on_local(struct kunit *test)
{
	struct dm_test_edid_ctx *ctx =
	dm_test_edid_ctx_alloc(test, DRM_MODE_CONNECTOR_DisplayPort);
	struct dc_sink *em_sink;
	struct dc_sink *local_sink;

	em_sink = kunit_kzalloc(test, sizeof(*em_sink), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, em_sink);
	local_sink = kunit_kzalloc(test, sizeof(*local_sink), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, local_sink);
	kref_init(&local_sink->refcount);
	dm_test_ces_setup(test, ctx, em_sink);
	ctx->link->local_sink = local_sink;
	ctx->aconnector->base.force = DRM_FORCE_ON;

	create_eml_sink(ctx->aconnector);

	KUNIT_EXPECT_PTR_EQ(test, ctx->aconnector->dc_sink, local_sink);
	KUNIT_EXPECT_EQ(test, kref_read(&local_sink->refcount), 2);

	drm_edid_free(ctx->aconnector->drm_edid);
	ctx->aconnector->drm_edid = NULL;
}

/* Tests for amdgpu_dm_connector_get_modes() */

static enum dp_link_encoding dm_test_gm_enc_8b10b(const struct dc_link_settings *s)
{
	return DP_8b_10b_ENCODING;
}

static enum dp_link_encoding dm_test_gm_enc_128b(const struct dc_link_settings *s)
{
	return DP_128b_132b_ENCODING;
}

/*
 * Build an amdgpu_dm_connector on an amdgpu_device-backed drm device (so
 * drm_to_adev() resolves for amdgpu_dm_fbc_init()) with an attached encoder
 * and a dc/dc_link whose link_srv reports a non-128b encoding by default. The
 * fbc compressor is left NULL so amdgpu_dm_fbc_init() early-returns.
 */
struct dm_test_gm_ctx {
	struct amdgpu_device *adev;
	struct drm_device *drm;
	struct amdgpu_dm_connector *aconnector;
	struct amdgpu_encoder *aenc;
	struct dc *dc;
	struct link_service *link_srv;
	struct dc_link *link;
};

static struct dm_test_gm_ctx *
dm_test_gm_ctx_alloc(struct kunit *test, int connector_type)
{
	struct dm_test_gm_ctx *ctx;
	struct device *dev;
	int ret;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	ctx->drm = __drm_kunit_helper_alloc_drm_device(test, dev,
						       sizeof(*ctx->adev),
						       offsetof(struct amdgpu_device, ddev),
						       DRIVER_MODESET);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx->drm);
	ctx->adev = drm_to_adev(ctx->drm);

	ctx->aconnector = drmm_kzalloc(ctx->drm, sizeof(*ctx->aconnector),
				       GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->aconnector);
	ret = drmm_connector_init(ctx->drm, &ctx->aconnector->base,
				  &dm_test_connector_funcs, connector_type,
				  NULL);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ctx->aenc = drmm_kzalloc(ctx->drm, sizeof(*ctx->aenc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->aenc);
	ret = drmm_encoder_init(ctx->drm, &ctx->aenc->base, NULL,
				DRM_MODE_ENCODER_TMDS, NULL);
	KUNIT_ASSERT_EQ(test, ret, 0);
	ret = drm_connector_attach_encoder(&ctx->aconnector->base,
					   &ctx->aenc->base);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ctx->dc = kunit_kzalloc(test, sizeof(*ctx->dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->dc);
	ctx->link_srv = kunit_kzalloc(test, sizeof(*ctx->link_srv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->link_srv);
	ctx->link = kunit_kzalloc(test, sizeof(*ctx->link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->link);

	ctx->link_srv->dp_get_encoding_format = dm_test_gm_enc_8b10b;
	ctx->dc->link_srv = ctx->link_srv;
	ctx->adev->dm.dc = ctx->dc;
	ctx->link->dc = ctx->dc;
	ctx->aconnector->dc_link = ctx->link;

	return ctx;
}

/**
 * dm_test_get_modes_noedid_default - Test synthesized modes without an EDID
 * @test: The KUnit test context
 *
 * With no cached EDID and a non-128b link, get_modes() synthesizes the default
 * 640x480 fallback mode(s) and reports a non-zero count.
 */
static void dm_test_get_modes_noedid_default(struct kunit *test)
{
	struct dm_test_gm_ctx *ctx =
		dm_test_gm_ctx_alloc(test, DRM_MODE_CONNECTOR_DisplayPort);
	int count;

	mutex_lock(&ctx->drm->mode_config.mutex);
	count = amdgpu_dm_connector_get_modes(&ctx->aconnector->base);
	mutex_unlock(&ctx->drm->mode_config.mutex);

	KUNIT_EXPECT_GT(test, count, 0);
}

/**
 * dm_test_get_modes_noedid_128b_adds_more - Test 128b links add 1080p modes
 * @test: The KUnit test context
 *
 * A 128b/132b link synthesizes the extra 1920x1080 fallback modes, so the mode
 * count is strictly greater than for an 8b/10b link.
 */
static void dm_test_get_modes_noedid_128b_adds_more(struct kunit *test)
{
	struct dm_test_gm_ctx *ctx =
		dm_test_gm_ctx_alloc(test, DRM_MODE_CONNECTOR_DisplayPort);
	int n_8b, n_128b;

	mutex_lock(&ctx->drm->mode_config.mutex);
	n_8b = amdgpu_dm_connector_get_modes(&ctx->aconnector->base);

	ctx->link_srv->dp_get_encoding_format = dm_test_gm_enc_128b;
	n_128b = amdgpu_dm_connector_get_modes(&ctx->aconnector->base);
	mutex_unlock(&ctx->drm->mode_config.mutex);

	KUNIT_EXPECT_GT(test, n_128b, n_8b);
}

/**
 * dm_test_get_modes_noedid_analog_adds_common - Test analog sinks add common modes
 * @test: The KUnit test context
 *
 * An analog VGA sink detected by load detection adds the common fallback modes
 * on top of the default 640x480 mode(s).
 */
static void dm_test_get_modes_noedid_analog_adds_common(struct kunit *test)
{
	struct dm_test_gm_ctx *ctx =
		dm_test_gm_ctx_alloc(test, DRM_MODE_CONNECTOR_VGA);
	struct dc_sink *sink;
	int n_base, n_analog;

	mutex_lock(&ctx->drm->mode_config.mutex);
	n_base = amdgpu_dm_connector_get_modes(&ctx->aconnector->base);
	mutex_unlock(&ctx->drm->mode_config.mutex);

	sink = kunit_kzalloc(test, sizeof(*sink), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, sink);
	sink->edid_caps.analog = true;
	ctx->aconnector->dc_sink = sink;
	ctx->link->link_id.id = CONNECTOR_ID_VGA;

	mutex_lock(&ctx->drm->mode_config.mutex);
	n_analog = amdgpu_dm_connector_get_modes(&ctx->aconnector->base);
	mutex_unlock(&ctx->drm->mode_config.mutex);

	KUNIT_EXPECT_GT(test, n_analog, n_base);
}

/**
 * dm_test_get_modes_with_edid - Test the cached-EDID path adds common modes
 * @test: The KUnit test context
 *
 * With a cached EDID on an eDP connector, get_modes() takes the DDC path and
 * adds the common downscaled modes derived from the encoder native mode.
 */
static void dm_test_get_modes_with_edid(struct kunit *test)
{
	struct dm_test_gm_ctx *ctx =
		dm_test_gm_ctx_alloc(test, DRM_MODE_CONNECTOR_eDP);
	const struct drm_edid *drm_edid;
	int count;

	drm_edid = drm_edid_alloc(dm_test_uad_edid, sizeof(dm_test_uad_edid));
	KUNIT_ASSERT_NOT_NULL(test, drm_edid);
	drm_edid_connector_update(&ctx->aconnector->base, drm_edid);
	ctx->aconnector->drm_edid = drm_edid;

	ctx->aenc->native_mode.hdisplay = 1920;
	ctx->aenc->native_mode.vdisplay = 1200;

	mutex_lock(&ctx->drm->mode_config.mutex);
	count = amdgpu_dm_connector_get_modes(&ctx->aconnector->base);
	mutex_unlock(&ctx->drm->mode_config.mutex);

	KUNIT_EXPECT_GT(test, count, 0);

	drm_edid_free(drm_edid);
	ctx->aconnector->drm_edid = NULL;
}

/* Tests for amdgpu_set_panel_orientation() */

/**
 * dm_test_panel_orientation_non_edp - Test non-eDP/LVDS connectors are skipped
 * @test: The KUnit test context
 *
 * Only eDP and LVDS panels carry a fixed orientation, so a DisplayPort
 * connector returns immediately with its panel_orientation left unknown.
 */
static void dm_test_panel_orientation_non_edp(struct kunit *test)
{
	struct dm_test_gm_ctx *ctx =
	dm_test_gm_ctx_alloc(test, DRM_MODE_CONNECTOR_DisplayPort);

	amdgpu_set_panel_orientation(&ctx->aconnector->base);

	KUNIT_EXPECT_EQ(test,
			ctx->aconnector->base.display_info.panel_orientation,
			DRM_MODE_PANEL_ORIENTATION_UNKNOWN);
}

/**
 * dm_test_panel_orientation_no_native_mode - Test a missing native mode is skipped
 * @test: The KUnit test context
 *
 * On an eDP connector with no cached EDID the encoder native mode stays 0x0, so
 * the function returns before applying an orientation.
 */
static void dm_test_panel_orientation_no_native_mode(struct kunit *test)
{
	struct dm_test_gm_ctx *ctx =
	dm_test_gm_ctx_alloc(test, DRM_MODE_CONNECTOR_eDP);

	amdgpu_set_panel_orientation(&ctx->aconnector->base);

	KUNIT_EXPECT_EQ(test, ctx->aenc->native_mode.hdisplay, 0);
	KUNIT_EXPECT_EQ(test,
			ctx->aconnector->base.display_info.panel_orientation,
			DRM_MODE_PANEL_ORIENTATION_UNKNOWN);
}

/**
 * dm_test_panel_orientation_applies_quirk - Test a native mode reaches the quirk
 * @test: The KUnit test context
 *
 * With a valid encoder native mode on an eDP connector the panel dimensions are
 * forwarded to the orientation quirk lookup; absent a matching quirk the
 * orientation stays unknown.
 */
static void dm_test_panel_orientation_applies_quirk(struct kunit *test)
{
	struct dm_test_gm_ctx *ctx =
	dm_test_gm_ctx_alloc(test, DRM_MODE_CONNECTOR_eDP);

	ctx->aenc->native_mode.hdisplay = 1920;
	ctx->aenc->native_mode.vdisplay = 1200;

	amdgpu_set_panel_orientation(&ctx->aconnector->base);

	KUNIT_EXPECT_EQ(test,
			ctx->aconnector->base.display_info.panel_orientation,
			DRM_MODE_PANEL_ORIENTATION_UNKNOWN);
}

/* Tests for amdgpu_dm_prune_primary_tile_modes() */

struct dm_test_prune_ctx {
	struct drm_device *drm;
	struct amdgpu_dm_connector *aconnector;
	struct dc_sink *sink;
};

/*
 * Build a connector configured as the primary tile of an Apple Studio Display:
 * a dc_sink requesting the second-tile patch, has_tile set, tile location (0,0)
 * and a per-tile timing of tile_h_size x tile_v_size. Individual tests relax a
 * single precondition to exercise the early-return guards.
 */
static struct dm_test_prune_ctx *
dm_test_prune_ctx_alloc(struct kunit *test)
{
	struct dm_test_prune_ctx *ctx;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	ctx->drm = dm_test_alloc_drm(test);
	ctx->aconnector = dm_test_add_connector(test, ctx->drm,
						DRM_MODE_CONNECTOR_DisplayPort);

	ctx->sink = kunit_kzalloc(test, sizeof(*ctx->sink), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->sink);
	ctx->sink->edid_caps.panel_patch.disable_second_tile = true;
	ctx->aconnector->dc_sink = ctx->sink;

	ctx->aconnector->base.has_tile = true;
	ctx->aconnector->base.tile_h_size = 2560;
	ctx->aconnector->base.tile_v_size = 2880;

	return ctx;
}

static struct drm_display_mode *
dm_test_prune_add_mode(struct kunit *test, struct drm_connector *connector,
		       int hdisplay, int vdisplay)
{
	struct drm_display_mode *mode = drm_mode_create(connector->dev);

	KUNIT_ASSERT_NOT_NULL(test, mode);
	mode->hdisplay = hdisplay;
	mode->vdisplay = vdisplay;
	list_add_tail(&mode->head, &connector->probed_modes);

	return mode;
}

static int dm_test_prune_count(struct drm_connector *connector)
{
	struct drm_display_mode *mode;
	int n = 0;

	list_for_each_entry(mode, &connector->probed_modes, head)
		n++;

	return n;
}

/**
 * dm_test_prune_no_sink - Test a connector without a sink is left untouched
 * @test: The KUnit test context
 */
static void dm_test_prune_no_sink(struct kunit *test)
{
	struct dm_test_prune_ctx *ctx = dm_test_prune_ctx_alloc(test);

	ctx->aconnector->dc_sink = NULL;
	dm_test_prune_add_mode(test, &ctx->aconnector->base, 2560, 2880);
	ctx->aconnector->num_modes = 1;

	amdgpu_dm_prune_primary_tile_modes(&ctx->aconnector->base);

	KUNIT_EXPECT_EQ(test, dm_test_prune_count(&ctx->aconnector->base), 1);
	KUNIT_EXPECT_EQ(test, ctx->aconnector->num_modes, 1);
}

/**
 * dm_test_prune_no_patch - Test the patch flag being unset skips pruning
 * @test: The KUnit test context
 */
static void dm_test_prune_no_patch(struct kunit *test)
{
	struct dm_test_prune_ctx *ctx = dm_test_prune_ctx_alloc(test);

	ctx->sink->edid_caps.panel_patch.disable_second_tile = false;
	dm_test_prune_add_mode(test, &ctx->aconnector->base, 2560, 2880);
	ctx->aconnector->num_modes = 1;

	amdgpu_dm_prune_primary_tile_modes(&ctx->aconnector->base);

	KUNIT_EXPECT_EQ(test, dm_test_prune_count(&ctx->aconnector->base), 1);
	KUNIT_EXPECT_EQ(test, ctx->aconnector->num_modes, 1);
}

/**
 * dm_test_prune_no_tile - Test a non-tiled connector skips pruning
 * @test: The KUnit test context
 */
static void dm_test_prune_no_tile(struct kunit *test)
{
	struct dm_test_prune_ctx *ctx = dm_test_prune_ctx_alloc(test);

	ctx->aconnector->base.has_tile = false;
	dm_test_prune_add_mode(test, &ctx->aconnector->base, 2560, 2880);
	ctx->aconnector->num_modes = 1;

	amdgpu_dm_prune_primary_tile_modes(&ctx->aconnector->base);

	KUNIT_EXPECT_EQ(test, dm_test_prune_count(&ctx->aconnector->base), 1);
	KUNIT_EXPECT_EQ(test, ctx->aconnector->num_modes, 1);
}

/**
 * dm_test_prune_secondary_tile - Test a secondary tile is left untouched
 * @test: The KUnit test context
 *
 * Only the primary tile (location 0,0) is pruned; a secondary tile keeps its
 * per-tile timing.
 */
static void dm_test_prune_secondary_tile(struct kunit *test)
{
	struct dm_test_prune_ctx *ctx = dm_test_prune_ctx_alloc(test);

	ctx->aconnector->base.tile_h_loc = 1;
	dm_test_prune_add_mode(test, &ctx->aconnector->base, 2560, 2880);
	ctx->aconnector->num_modes = 1;

	amdgpu_dm_prune_primary_tile_modes(&ctx->aconnector->base);

	KUNIT_EXPECT_EQ(test, dm_test_prune_count(&ctx->aconnector->base), 1);
	KUNIT_EXPECT_EQ(test, ctx->aconnector->num_modes, 1);
}

/**
 * dm_test_prune_removes_per_tile - Test the per-tile timing is pruned
 * @test: The KUnit test context
 *
 * On the primary tile the per-tile (tile_h_size x tile_v_size) timing is
 * dropped while the full-resolution mode is kept and num_modes is decremented.
 */
static void dm_test_prune_removes_per_tile(struct kunit *test)
{
	struct dm_test_prune_ctx *ctx = dm_test_prune_ctx_alloc(test);
	struct drm_display_mode *mode;
	bool has_tile_mode = false;

	dm_test_prune_add_mode(test, &ctx->aconnector->base, 2560, 2880);
	dm_test_prune_add_mode(test, &ctx->aconnector->base, 5120, 2880);
	ctx->aconnector->num_modes = 2;

	amdgpu_dm_prune_primary_tile_modes(&ctx->aconnector->base);

	KUNIT_EXPECT_EQ(test, dm_test_prune_count(&ctx->aconnector->base), 1);
	KUNIT_EXPECT_EQ(test, ctx->aconnector->num_modes, 1);

	list_for_each_entry(mode, &ctx->aconnector->base.probed_modes, head)
		if (mode->hdisplay == 2560 && mode->vdisplay == 2880)
			has_tile_mode = true;
	KUNIT_EXPECT_FALSE(test, has_tile_mode);
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

/* Tests for hdmi_frl_status_polling_work() */

static int dm_test_frl_poll_calls;
static int dm_test_frl_detect_calls;

static bool dm_test_frl_poll_true(struct dc_link *link)
{
	dm_test_frl_poll_calls++;
	return true;
}

static bool dm_test_frl_poll_false(struct dc_link *link)
{
	dm_test_frl_poll_calls++;
	return false;
}

static bool dm_test_frl_detect(struct dc_link *link, enum dc_detect_reason reason)
{
	dm_test_frl_detect_calls++;
	return true;
}

struct dm_test_frl_ctx {
	struct amdgpu_display_manager *dm;
	struct dc *dc;
	struct link_service *link_srv;
};

/*
 * Build a display manager whose polling work re-arms on a real workqueue. The
 * dc starts with an empty link array and link_srv hooks that count poll and
 * detect calls, so a test can assert exactly how far a link progresses. A large
 * re-arm delay keeps the requeued work dormant until the test cancels it.
 */
static struct dm_test_frl_ctx *dm_test_frl_ctx_alloc(struct kunit *test)
{
	struct dm_test_frl_ctx *ctx;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	ctx->dm = kunit_kzalloc(test, sizeof(*ctx->dm), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->dm);
	ctx->dc = kunit_kzalloc(test, sizeof(*ctx->dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->dc);
	ctx->link_srv = kunit_kzalloc(test, sizeof(*ctx->link_srv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->link_srv);

	ctx->link_srv->hdmi_frl_poll_status_flag = dm_test_frl_poll_true;
	ctx->link_srv->detect_link = dm_test_frl_detect;
	ctx->dc->link_srv = ctx->link_srv;
	ctx->dm->dc = ctx->dc;
	mutex_init(&ctx->dm->dc_lock);
	INIT_DELAYED_WORK(&ctx->dm->hdmi_frl_status_polling_work, hdmi_frl_status_polling_work);
	ctx->dm->hdmi_frl_status_polling_delay_ms = 100000;
	ctx->dm->hdmi_frl_status_polling_wq = system_wq;

	dm_test_frl_poll_calls = 0;
	dm_test_frl_detect_calls = 0;

	return ctx;
}

/* Allocate a dc_link, wire its dc back-pointer and register it at index 0. */
static struct dc_link *dm_test_frl_add_link(struct kunit *test, struct dm_test_frl_ctx *ctx)
{
	struct dc_link *link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);

	KUNIT_ASSERT_NOT_NULL(test, link);
	link->dc = ctx->dc;
	ctx->dc->links[0] = link;

	return link;
}

/* Register a link that satisfies every guard up to the poll status check. */
static struct dc_link *dm_test_frl_add_hdmi_link(struct kunit *test, struct dm_test_frl_ctx *ctx)
{
	struct dc_link *link = dm_test_frl_add_link(test, ctx);

	link->local_sink = kunit_kzalloc(test, sizeof(*link->local_sink), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, link->local_sink);
	link->connector_signal = SIGNAL_TYPE_HDMI_TYPE_A;
	link->frl_link_settings.frl_link_rate = HDMI_FRL_LINK_RATE_3GBPS;
	link->link_status.link_active = true;

	return link;
}

/* Run the work once directly, then cancel and tear down the re-armed work. */
static void dm_test_frl_run(struct dm_test_frl_ctx *ctx)
{
	hdmi_frl_status_polling_work(&ctx->dm->hdmi_frl_status_polling_work.work);
	cancel_delayed_work_sync(&ctx->dm->hdmi_frl_status_polling_work);
}

/**
 * dm_test_frl_no_links - Test an empty link array is a no-op
 * @test: The KUnit test context
 *
 * With no links registered the loop body never runs, so neither the poll nor
 * the detect hook is invoked.
 */
static void dm_test_frl_no_links(struct kunit *test)
{
	struct dm_test_frl_ctx *ctx = dm_test_frl_ctx_alloc(test);

	dm_test_frl_run(ctx);

	KUNIT_EXPECT_EQ(test, dm_test_frl_poll_calls, 0);
	KUNIT_EXPECT_EQ(test, dm_test_frl_detect_calls, 0);
}

/**
 * dm_test_frl_skips_no_local_sink - Test a link without a local sink is skipped
 * @test: The KUnit test context
 */
static void dm_test_frl_skips_no_local_sink(struct kunit *test)
{
	struct dm_test_frl_ctx *ctx = dm_test_frl_ctx_alloc(test);

	dm_test_frl_add_link(test, ctx);

	dm_test_frl_run(ctx);

	KUNIT_EXPECT_EQ(test, dm_test_frl_poll_calls, 0);
}

/**
 * dm_test_frl_skips_non_hdmi - Test a non-HDMI link is skipped
 * @test: The KUnit test context
 */
static void dm_test_frl_skips_non_hdmi(struct kunit *test)
{
	struct dm_test_frl_ctx *ctx = dm_test_frl_ctx_alloc(test);
	struct dc_link *link = dm_test_frl_add_link(test, ctx);

	link->local_sink = kunit_kzalloc(test, sizeof(*link->local_sink), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, link->local_sink);
	link->connector_signal = SIGNAL_TYPE_DISPLAY_PORT;

	dm_test_frl_run(ctx);

	KUNIT_EXPECT_EQ(test, dm_test_frl_poll_calls, 0);
}

/**
 * dm_test_frl_skips_zero_rate - Test a link with no FRL rate is skipped
 * @test: The KUnit test context
 */
static void dm_test_frl_skips_zero_rate(struct kunit *test)
{
	struct dm_test_frl_ctx *ctx = dm_test_frl_ctx_alloc(test);
	struct dc_link *link = dm_test_frl_add_hdmi_link(test, ctx);

	link->frl_link_settings.frl_link_rate = 0;

	dm_test_frl_run(ctx);

	KUNIT_EXPECT_EQ(test, dm_test_frl_poll_calls, 0);
}

/**
 * dm_test_frl_poll_no_update - Test a clear poll status skips retraining
 * @test: The KUnit test context
 *
 * The poll hook is reached but reports no change, so dc_link_detect() is not
 * called.
 */
static void dm_test_frl_poll_no_update(struct kunit *test)
{
	struct dm_test_frl_ctx *ctx = dm_test_frl_ctx_alloc(test);

	ctx->link_srv->hdmi_frl_poll_status_flag = dm_test_frl_poll_false;
	dm_test_frl_add_hdmi_link(test, ctx);

	dm_test_frl_run(ctx);

	KUNIT_EXPECT_EQ(test, dm_test_frl_poll_calls, 1);
	KUNIT_EXPECT_EQ(test, dm_test_frl_detect_calls, 0);
}

/**
 * dm_test_frl_poll_retrains - Test a set poll status triggers a retrain
 * @test: The KUnit test context
 *
 * A poll status change drives dc_link_detect() with DETECT_REASON_RETRAIN.
 */
static void dm_test_frl_poll_retrains(struct kunit *test)
{
	struct dm_test_frl_ctx *ctx = dm_test_frl_ctx_alloc(test);

	dm_test_frl_add_hdmi_link(test, ctx);

	dm_test_frl_run(ctx);

	KUNIT_EXPECT_EQ(test, dm_test_frl_poll_calls, 1);
	KUNIT_EXPECT_EQ(test, dm_test_frl_detect_calls, 1);
}

/* Tests for amdgpu_dm_connector_init_helper() */

/*
 * Build an amdgpu_dm_connector on a kunit drm_device embedded in an
 * amdgpu_device, with the amdgpu mode properties created exactly as
 * amdgpu_dm_mode_config_init() does so the helper has real properties to
 * attach. The dc_link carries a link encoder and leaves DIG mapping
 * inflexible, so link_enc_cfg_get_link_enc() resolves without a dc instance.
 */
struct dm_test_init_helper_ctx {
	struct amdgpu_device *adev;
	struct drm_device *drm;
	struct amdgpu_display_manager *dm;
	struct amdgpu_dm_connector *aconnector;
	struct dc_link *link;
};

/*
 * Stand in for amdgpu_display_modeset_create_props(), which the test module
 * cannot link against. Only the properties the init helper attaches are
 * created; underscan is a plain range because only attachment is under test.
 */
static void dm_test_create_mode_props(struct kunit *test, struct amdgpu_device *adev)
{
	struct amdgpu_mode_info *mode_info = &adev->mode_info;
	struct drm_device *drm = adev_to_drm(adev);

	KUNIT_ASSERT_EQ(test, drm_mode_create_scaling_mode_property(drm), 0);

	mode_info->underscan_property =
		drm_property_create_range(drm, 0, "underscan", 0, 1);
	KUNIT_ASSERT_NOT_NULL(test, mode_info->underscan_property);
	mode_info->underscan_hborder_property =
		drm_property_create_range(drm, 0, "underscan hborder", 0, 128);
	KUNIT_ASSERT_NOT_NULL(test, mode_info->underscan_hborder_property);
	mode_info->underscan_vborder_property =
		drm_property_create_range(drm, 0, "underscan vborder", 0, 128);
	KUNIT_ASSERT_NOT_NULL(test, mode_info->underscan_vborder_property);
}

static struct dm_test_init_helper_ctx *
dm_test_init_helper_ctx_alloc(struct kunit *test, int connector_type)
{
	struct dm_test_init_helper_ctx *ctx;
	struct device *dev;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);
	ctx->drm = __drm_kunit_helper_alloc_drm_device(test, dev,
						       sizeof(struct amdgpu_device),
						       offsetof(struct amdgpu_device, ddev),
						       DRIVER_MODESET | DRIVER_ATOMIC);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx->drm);

	ctx->adev = drm_to_adev(ctx->drm);
	ctx->adev->dev = dev;
	dm_test_create_mode_props(test, ctx->adev);

	ctx->dm = &ctx->adev->dm;
	ctx->dm->adev = ctx->adev;
	ctx->dm->ddev = ctx->drm;

	ctx->link = kunit_kzalloc(test, sizeof(*ctx->link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->link);
	ctx->link->link_enc = kunit_kzalloc(test, sizeof(*ctx->link->link_enc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->link->link_enc);
	ctx->aconnector = drmm_kzalloc(ctx->drm, sizeof(*ctx->aconnector), GFP_KERNEL);

	KUNIT_ASSERT_NOT_NULL(test, ctx->aconnector);
	KUNIT_ASSERT_EQ(test,
			drmm_connector_init(ctx->drm, &ctx->aconnector->base,
					    &dm_test_connector_funcs, connector_type,
					    NULL), 0);

	return ctx;
}

/* True when @prop is attached to the connector's mode object. */
static bool dm_test_has_prop(struct drm_connector *connector, struct drm_property *prop)
{
	struct drm_object_properties *props = connector->base.properties;
	int i;

	if (!prop)
		return false;

	for (i = 0; i < props->count; i++)
		if (props->properties[i] == prop)
			return true;

	return false;
}

/**
 * dm_test_init_helper_common_defaults - Test the connector defaults and properties
 * @test: The KUnit test context
 *
 * The helper resets the connector state, records the link and its index, and
 * attaches the scaling mode and the three underscan properties that every
 * connector type receives.
 */
static void dm_test_init_helper_common_defaults(struct kunit *test)
{
	struct dm_test_init_helper_ctx *ctx =
		dm_test_init_helper_ctx_alloc(test, DRM_MODE_CONNECTOR_HDMIA);
	struct drm_connector *connector = &ctx->aconnector->base;

	amdgpu_dm_connector_init_helper(ctx->dm, ctx->aconnector,
					DRM_MODE_CONNECTOR_HDMIA, ctx->link, 3);

	KUNIT_ASSERT_NOT_NULL(test, connector->state);
	KUNIT_EXPECT_EQ(test, ctx->aconnector->connector_id, 3);
	KUNIT_EXPECT_EQ(test, ctx->aconnector->bl_idx, -1);
	KUNIT_EXPECT_PTR_EQ(test, ctx->aconnector->dc_link, ctx->link);
	KUNIT_EXPECT_EQ(test, ctx->aconnector->audio_inst, -1);
	KUNIT_EXPECT_FALSE(test, ctx->aconnector->pack_sdp_v1_3);
	KUNIT_EXPECT_EQ(test, (int)ctx->aconnector->as_type, (int)ADAPTIVE_SYNC_TYPE_NONE);
	KUNIT_EXPECT_FALSE(test, connector->interlace_allowed);
	KUNIT_EXPECT_FALSE(test, connector->doublescan_allowed);
	KUNIT_EXPECT_FALSE(test, connector->stereo_allowed);
	KUNIT_EXPECT_EQ(test, connector->dpms, DRM_MODE_DPMS_OFF);
	KUNIT_EXPECT_EQ(test, (int)ctx->aconnector->hpd.hpd, (int)AMDGPU_HPD_NONE);
	KUNIT_EXPECT_EQ(test, ctx->aconnector->hdmi_hpd_debounce_delay_ms, 0);

	/* The link encoder advertises no YCbCr 4:2:0 support. */
	KUNIT_EXPECT_FALSE(test, connector->ycbcr_420_allowed);

	KUNIT_EXPECT_TRUE(test,
			  dm_test_has_prop(connector, ctx->drm->mode_config.scaling_mode_property));
	KUNIT_EXPECT_TRUE(test,
			  dm_test_has_prop(connector, ctx->adev->mode_info.underscan_property));
	KUNIT_EXPECT_TRUE(test,
			  dm_test_has_prop(connector,
					   ctx->adev->mode_info.underscan_hborder_property));
	KUNIT_EXPECT_TRUE(test,
			  dm_test_has_prop(connector,
					   ctx->adev->mode_info.underscan_vborder_property));
}

/**
 * dm_test_init_helper_hdmi - Test the HDMI connector wiring
 * @test: The KUnit test context
 *
 * HDMI polls on hotplug, takes YCbCr 4:2:0 support straight from the link
 * encoder features and gets the max bpc, content type, colorspace, HDR
 * metadata and VRR capable properties.
 */
static void dm_test_init_helper_hdmi(struct kunit *test)
{
	struct dm_test_init_helper_ctx *ctx =
		dm_test_init_helper_ctx_alloc(test, DRM_MODE_CONNECTOR_HDMIA);
	struct drm_connector *connector = &ctx->aconnector->base;

	ctx->link->link_enc->features.hdmi_ycbcr420_supported = true;

	amdgpu_dm_connector_init_helper(ctx->dm, ctx->aconnector,
					DRM_MODE_CONNECTOR_HDMIA, ctx->link, 0);

	KUNIT_EXPECT_EQ(test, (int)connector->polled, (int)DRM_CONNECTOR_POLL_HPD);
	KUNIT_EXPECT_TRUE(test, connector->ycbcr_420_allowed);
	KUNIT_ASSERT_NOT_NULL(test, connector->state);
	KUNIT_EXPECT_EQ(test, connector->state->max_bpc, 16);
	KUNIT_EXPECT_EQ(test, connector->state->max_requested_bpc, 16);
	KUNIT_EXPECT_NOT_NULL(test, connector->max_bpc_property);
	KUNIT_EXPECT_NOT_NULL(test, connector->broadcast_rgb_property);
	KUNIT_EXPECT_NOT_NULL(test, connector->colorspace_property);
	KUNIT_EXPECT_NOT_NULL(test, connector->vrr_capable_property);
	KUNIT_EXPECT_TRUE(test,
			  dm_test_has_prop(connector, ctx->drm->mode_config.content_type_property));
	KUNIT_EXPECT_TRUE(test,
			  dm_test_has_prop(connector,
					   ctx->drm->mode_config.hdr_output_metadata_property));
}

/**
 * dm_test_init_helper_dp - Test the DisplayPort connector wiring
 * @test: The KUnit test context
 *
 * DisplayPort re-resolves the link encoder through the encoder configuration
 * and takes YCbCr 4:2:0 support from its DP feature bit.
 */
static void dm_test_init_helper_dp(struct kunit *test)
{
	struct dm_test_init_helper_ctx *ctx =
		dm_test_init_helper_ctx_alloc(test, DRM_MODE_CONNECTOR_DisplayPort);
	struct drm_connector *connector = &ctx->aconnector->base;

	ctx->link->link_enc->features.dp_ycbcr420_supported = true;

	amdgpu_dm_connector_init_helper(ctx->dm, ctx->aconnector,
					DRM_MODE_CONNECTOR_DisplayPort, ctx->link, 1);

	KUNIT_EXPECT_EQ(test, (int)connector->polled, (int)DRM_CONNECTOR_POLL_HPD);
	KUNIT_EXPECT_TRUE(test, connector->ycbcr_420_allowed);
	KUNIT_EXPECT_NOT_NULL(test, connector->broadcast_rgb_property);
	KUNIT_EXPECT_NOT_NULL(test, connector->colorspace_property);
	KUNIT_EXPECT_NOT_NULL(test, connector->vrr_capable_property);
}

/**
 * dm_test_init_helper_dp_mst_root - Test an MST branch connector skips properties
 * @test: The KUnit test context
 *
 * A connector below an MST root gets neither the broadcast RGB, max bpc,
 * colorspace nor VRR capable property, because those live on the root.
 */
static void dm_test_init_helper_dp_mst_root(struct kunit *test)
{
	struct dm_test_init_helper_ctx *ctx =
		dm_test_init_helper_ctx_alloc(test, DRM_MODE_CONNECTOR_DisplayPort);
	struct drm_connector *connector = &ctx->aconnector->base;

	ctx->aconnector->mst_root = ctx->aconnector;

	amdgpu_dm_connector_init_helper(ctx->dm, ctx->aconnector,
					DRM_MODE_CONNECTOR_DisplayPort, ctx->link, 0);

	KUNIT_EXPECT_NULL(test, connector->broadcast_rgb_property);
	KUNIT_EXPECT_NULL(test, connector->max_bpc_property);
	KUNIT_EXPECT_NULL(test, connector->colorspace_property);
	KUNIT_EXPECT_NULL(test, connector->vrr_capable_property);
}

/**
 * dm_test_init_helper_dvid - Test DVI-D polls on hotplug
 * @test: The KUnit test context
 */
static void dm_test_init_helper_dvid(struct kunit *test)
{
	struct dm_test_init_helper_ctx *ctx =
		dm_test_init_helper_ctx_alloc(test, DRM_MODE_CONNECTOR_DVID);

	amdgpu_dm_connector_init_helper(ctx->dm, ctx->aconnector,
					DRM_MODE_CONNECTOR_DVID, ctx->link, 0);

	KUNIT_EXPECT_EQ(test, (int)ctx->aconnector->base.polled, (int)DRM_CONNECTOR_POLL_HPD);
	KUNIT_EXPECT_FALSE(test, ctx->aconnector->base.ycbcr_420_allowed);
}

/**
 * dm_test_init_helper_vga - Test VGA polls on connect and disconnect
 * @test: The KUnit test context
 */
static void dm_test_init_helper_vga(struct kunit *test)
{
	struct dm_test_init_helper_ctx *ctx =
		dm_test_init_helper_ctx_alloc(test, DRM_MODE_CONNECTOR_VGA);

	amdgpu_dm_connector_init_helper(ctx->dm, ctx->aconnector,
					DRM_MODE_CONNECTOR_VGA, ctx->link, 0);

	KUNIT_EXPECT_EQ(test, (int)ctx->aconnector->base.polled,
			(int)(DRM_CONNECTOR_POLL_CONNECT | DRM_CONNECTOR_POLL_DISCONNECT));
}

/**
 * dm_test_init_helper_unpolled_default - Test an unhandled type is left unpolled
 * @test: The KUnit test context
 */
static void dm_test_init_helper_unpolled_default(struct kunit *test)
{
	struct dm_test_init_helper_ctx *ctx =
		dm_test_init_helper_ctx_alloc(test, DRM_MODE_CONNECTOR_Composite);

	amdgpu_dm_connector_init_helper(ctx->dm, ctx->aconnector,
					DRM_MODE_CONNECTOR_Composite, ctx->link, 0);

	KUNIT_EXPECT_EQ(test, (int)ctx->aconnector->base.polled, 0);
	KUNIT_EXPECT_NULL(test, ctx->aconnector->base.colorspace_property);
	KUNIT_EXPECT_NULL(test, ctx->aconnector->base.vrr_capable_property);
}

/**
 * dm_test_init_helper_edp - Test eDP gets the panel type property
 * @test: The KUnit test context
 *
 * eDP shares the DisplayPort colorspace and HDR property set and additionally
 * receives the panel type property. No privacy screen provider is registered
 * in the test environment, so the lookup fails with -ENODEV and is ignored.
 */
static void dm_test_init_helper_edp(struct kunit *test)
{
	struct dm_test_init_helper_ctx *ctx =
		dm_test_init_helper_ctx_alloc(test, DRM_MODE_CONNECTOR_eDP);
	struct drm_connector *connector = &ctx->aconnector->base;

	amdgpu_dm_connector_init_helper(ctx->dm, ctx->aconnector,
					DRM_MODE_CONNECTOR_eDP, ctx->link, 0);

	KUNIT_EXPECT_EQ(test, (int)connector->polled, 0);
	KUNIT_EXPECT_NOT_NULL(test, connector->colorspace_property);
	KUNIT_EXPECT_NOT_NULL(test, connector->vrr_capable_property);
	KUNIT_EXPECT_TRUE(test,
			  dm_test_has_prop(connector, ctx->drm->mode_config.panel_type_property));
	KUNIT_EXPECT_NULL(test, connector->privacy_screen);
}

/**
 * dm_test_init_helper_hdcp_property - Test the content protection property
 * @test: The KUnit test context
 *
 * The content protection property is only attached when a HDCP workqueue was
 * created for the device.
 */
static void dm_test_init_helper_hdcp_property(struct kunit *test)
{
	struct dm_test_init_helper_ctx *ctx =
		dm_test_init_helper_ctx_alloc(test, DRM_MODE_CONNECTOR_HDMIA);
	struct drm_connector *connector = &ctx->aconnector->base;

	ctx->adev->dm.hdcp_workqueue = kunit_kzalloc(test, sizeof(struct hdcp_workqueue),
						     GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->adev->dm.hdcp_workqueue);

	amdgpu_dm_connector_init_helper(ctx->dm, ctx->aconnector,
					DRM_MODE_CONNECTOR_HDMIA, ctx->link, 0);

	KUNIT_EXPECT_TRUE(test,
			  dm_test_has_prop(connector,
					   ctx->drm->mode_config.content_protection_property));
}

/**
 * dm_test_init_helper_hpd_debounce_disabled - Test the default HPD debounce delay
 * @test: The KUnit test context
 *
 * With the hdmi_hpd_debounce_delay_ms module parameter left at its default the
 * debounce work is not armed and the delay stays zero.
 */
static void dm_test_init_helper_hpd_debounce_disabled(struct kunit *test)
{
	struct dm_test_init_helper_ctx *ctx =
		dm_test_init_helper_ctx_alloc(test, DRM_MODE_CONNECTOR_HDMIA);

	amdgpu_dm_connector_init_helper(ctx->dm, ctx->aconnector,
					DRM_MODE_CONNECTOR_HDMIA, ctx->link, 0);

	KUNIT_EXPECT_EQ(test, ctx->aconnector->hdmi_hpd_debounce_delay_ms, 0);
}

/* Tests for amdgpu_dm_initialize_hdmi_connector() */

/**
 * dm_test_initialize_hdmi_cec_registers - Test the CEC notifier is registered
 * @test: The KUnit test context
 *
 * Without DC_DISABLE_HDMI_CEC in the debug mask a CEC notifier is created for
 * the connector.
 */
static void dm_test_initialize_hdmi_cec_registers(struct kunit *test)
{
	struct dm_test_init_helper_ctx *ctx =
		dm_test_init_helper_ctx_alloc(test, DRM_MODE_CONNECTOR_HDMIA);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_initialize_hdmi_connector(ctx->aconnector), 0);
	KUNIT_EXPECT_NOT_NULL(test, ctx->aconnector->notifier);
}

/* Tests for amdgpu_dm_update_freesync_caps() */

static void dm_test_fs_free_edid(void *data)
{
	drm_edid_free(data);
}

/*
 * Build a two block EDID: the minimal base block with its extension count set
 * to one, followed by @ext, with both checksums recomputed so drm_edid_raw()
 * and the DRM EDID parsers accept it.
 */
static const struct drm_edid *dm_test_fs_edid_alloc(struct kunit *test, const u8 *ext)
{
	const struct drm_edid *drm_edid;
	u8 *raw;
	int i;

	raw = kunit_kzalloc(test, 2 * EDID_LENGTH, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, raw);
	memcpy(raw, dm_test_uad_edid, EDID_LENGTH);
	memcpy(raw + EDID_LENGTH, ext, EDID_LENGTH);
	raw[EDID_LENGTH - 2] = 1;

	for (i = 0; i < 2; i++) {
		u8 *block = raw + i * EDID_LENGTH;
		u8 sum = 0;
		int j;

		for (j = 0; j < EDID_LENGTH - 1; j++)
			sum += block[j];
		block[EDID_LENGTH - 1] = -sum;
	}

	drm_edid = drm_edid_alloc(raw, 2 * EDID_LENGTH);
	KUNIT_ASSERT_NOT_NULL(test, drm_edid);
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, dm_test_fs_free_edid,
						  (void *)drm_edid), 0);

	return drm_edid;
}

/*
 * A DisplayID extension holding a dynamic video timing range descriptor, which
 * is what parse_edid_displayid_vrr() scans for.
 */
static const struct drm_edid *
dm_test_fs_edid_displayid(struct kunit *test, u8 min_vfreq, u8 max_vfreq)
{
	u8 ext[EDID_LENGTH] = {0};

	ext[0] = DM_TEST_DISPLAYID_EXT;
	ext[1] = 0x25;
	ext[2] = 0x00;
	ext[3] = 9;
	ext[10] = min_vfreq;
	ext[11] = max_vfreq;

	return dm_test_fs_edid_alloc(test, ext);
}

/*
 * A CTA-861 extension with no data blocks, enough for parse_hdmi_amd_vsdb() to
 * accept the block and hand it to the (mocked) DMCU parser.
 */
static const struct drm_edid *dm_test_fs_edid_cea(struct kunit *test)
{
	u8 ext[EDID_LENGTH] = {0};

	ext[0] = DM_TEST_CEA_EXT;
	ext[1] = 3;
	ext[2] = 4;

	return dm_test_fs_edid_alloc(test, ext);
}

/*
 * A CTA-861 extension carrying a 15-byte AMD VSDB v3 payload, which
 * drm_parse_amd_vsdb() turns into connector->display_info.amd_vsdb and
 * get_amd_vsdb() then reads back.
 */
static const struct drm_edid *
dm_test_fs_edid_amd_vsdb(struct kunit *test, u8 feature_caps)
{
	u8 ext[EDID_LENGTH] = {0};

	ext[0] = DM_TEST_CEA_EXT;
	ext[1] = 3;
	ext[2] = 4 + 1 + 15;
	ext[4] = (3 << 5) | 15;		/* vendor data block, 15-byte payload */
	ext[5] = 0x1a;			/* AMD IEEE OUI, LSB first */
	ext[8] = 0x03;			/* AMD VSDB payload version */
	ext[9] = feature_caps;

	return dm_test_fs_edid_alloc(test, ext);
}

struct dm_test_fs_caps_ctx {
	struct amdgpu_device *adev;
	struct drm_device *drm;
	struct amdgpu_dm_connector *aconnector;
	struct dc_link *link;
	struct dc_sink *sink;
	struct resource_pool *pool;
};

/*
 * Build a connector that clears every guard at the top of
 * amdgpu_dm_update_freesync_caps(): a reset connector state, a dc_sink whose
 * dc_context reports a VRR capable DCE version, and a non-NULL freesync
 * module. The resource pool carries no DMCU, so tests that need the AMD VSDB
 * parser to succeed install one.
 */
static struct dm_test_fs_caps_ctx *
dm_test_fs_caps_ctx_alloc(struct kunit *test, enum signal_type sink_signal)
{
	struct dm_test_fs_caps_ctx *ctx;
	struct dc_context *dc_ctx;
	struct device *dev;
	struct dc *dc;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);
	ctx->drm = __drm_kunit_helper_alloc_drm_device(test, dev,
						       sizeof(struct amdgpu_device),
						       offsetof(struct amdgpu_device, ddev),
						       DRIVER_MODESET);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx->drm);
	ctx->adev = drm_to_adev(ctx->drm);
	ctx->adev->dm.adev = ctx->adev;
	mutex_init(&ctx->adev->dm.dc_lock);

	ctx->aconnector = drmm_kzalloc(ctx->drm, sizeof(*ctx->aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->aconnector);
	KUNIT_ASSERT_EQ(test,
			drmm_connector_init(ctx->drm, &ctx->aconnector->base,
					    &dm_test_connector_funcs,
					    DRM_MODE_CONNECTOR_DisplayPort, NULL), 0);
	amdgpu_dm_connector_funcs_reset(&ctx->aconnector->base);
	KUNIT_ASSERT_NOT_NULL(test, ctx->aconnector->base.state);
	KUNIT_ASSERT_EQ(test,
			drm_connector_attach_vrr_capable_property(&ctx->aconnector->base), 0);

	dc_ctx = kunit_kzalloc(test, sizeof(*dc_ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dc_ctx);
	dc_ctx->driver_context = ctx->adev;
	dc_ctx->dce_version = DCE_VERSION_8_0;

	ctx->pool = kunit_kzalloc(test, sizeof(*ctx->pool), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->pool);

	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dc);
	dc->ctx = dc_ctx;
	dc->res_pool = ctx->pool;
	ctx->adev->dm.dc = dc;

	ctx->adev->dm.freesync_module =
		kunit_kzalloc(test, sizeof(struct mod_freesync), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->adev->dm.freesync_module);

	ctx->link = kunit_kzalloc(test, sizeof(*ctx->link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->link);
	ctx->aconnector->dc_link = ctx->link;

	ctx->sink = kunit_kzalloc(test, sizeof(*ctx->sink), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->sink);
	ctx->sink->ctx = dc_ctx;
	ctx->sink->sink_signal = sink_signal;
	ctx->aconnector->dc_sink = ctx->sink;

	return ctx;
}

static bool dm_test_fs_caps_capable(struct dm_test_fs_caps_ctx *ctx)
{
	return to_dm_connector_state(ctx->aconnector->base.state)->freesync_capable;
}

/**
 * dm_test_fs_caps_dp_msa_range - Test a DP sink with an invalid-MSA range
 * @test: The KUnit test context
 *
 * A DisplayPort sink allowing invalid MSA timing takes its refresh range from
 * the monitor range that parse_edid_displayid_vrr() recovered, and a range
 * wider than 10Hz marks the connector FreeSync capable.
 */
static void dm_test_fs_caps_dp_msa_range(struct kunit *test)
{
	struct dm_test_fs_caps_ctx *ctx =
		dm_test_fs_caps_ctx_alloc(test, SIGNAL_TYPE_DISPLAY_PORT);
	const struct drm_edid *drm_edid = dm_test_fs_edid_displayid(test, 40, 144);

	ctx->link->dpcd_caps.allow_invalid_MSA_timing_param = true;

	amdgpu_dm_update_freesync_caps(&ctx->aconnector->base, drm_edid, false);

	KUNIT_EXPECT_EQ(test, ctx->aconnector->min_vfreq, 40);
	KUNIT_EXPECT_EQ(test, ctx->aconnector->max_vfreq, 144);
	KUNIT_EXPECT_TRUE(test, dm_test_fs_caps_capable(ctx));
}

/**
 * dm_test_fs_caps_dp_narrow_range - Test a range of 10Hz or less is rejected
 * @test: The KUnit test context
 */
static void dm_test_fs_caps_dp_narrow_range(struct kunit *test)
{
	struct dm_test_fs_caps_ctx *ctx =
		dm_test_fs_caps_ctx_alloc(test, SIGNAL_TYPE_DISPLAY_PORT);
	const struct drm_edid *drm_edid = dm_test_fs_edid_displayid(test, 60, 65);

	ctx->link->dpcd_caps.allow_invalid_MSA_timing_param = true;

	amdgpu_dm_update_freesync_caps(&ctx->aconnector->base, drm_edid, false);

	KUNIT_EXPECT_EQ(test, ctx->aconnector->min_vfreq, 60);
	KUNIT_EXPECT_EQ(test, ctx->aconnector->max_vfreq, 65);
	KUNIT_EXPECT_FALSE(test, dm_test_fs_caps_capable(ctx));
}

/**
 * dm_test_fs_caps_dp_msa_not_allowed - Test a DP sink requiring valid MSA
 * @test: The KUnit test context
 *
 * Without allow_invalid_MSA_timing_param the reported range is ignored and the
 * connector keeps a zero refresh range.
 */
static void dm_test_fs_caps_dp_msa_not_allowed(struct kunit *test)
{
	struct dm_test_fs_caps_ctx *ctx =
		dm_test_fs_caps_ctx_alloc(test, SIGNAL_TYPE_DISPLAY_PORT);
	const struct drm_edid *drm_edid = dm_test_fs_edid_displayid(test, 40, 144);

	amdgpu_dm_update_freesync_caps(&ctx->aconnector->base, drm_edid, false);

	KUNIT_EXPECT_EQ(test, ctx->aconnector->min_vfreq, 0);
	KUNIT_EXPECT_EQ(test, ctx->aconnector->max_vfreq, 0);
	KUNIT_EXPECT_FALSE(test, dm_test_fs_caps_capable(ctx));
}

/**
 * dm_test_fs_caps_edp_replay_mode - Test an eDP panel advertising replay
 * @test: The KUnit test context
 *
 * An AMD VSDB with the replay feature bit set records the replay mode and its
 * VSDB version on the connector and selects the eDP adaptive sync type.
 */
static void dm_test_fs_caps_edp_replay_mode(struct kunit *test)
{
	struct dm_test_fs_caps_ctx *ctx =
		dm_test_fs_caps_ctx_alloc(test, SIGNAL_TYPE_EDP);
	const struct drm_edid *drm_edid = dm_test_fs_edid_amd_vsdb(test, 0x40);

	amdgpu_dm_update_freesync_caps(&ctx->aconnector->base, drm_edid, false);

	KUNIT_EXPECT_TRUE(test, ctx->aconnector->vsdb_info.replay_mode);
	KUNIT_EXPECT_EQ(test, ctx->aconnector->vsdb_info.amd_vsdb_version, 3);
	KUNIT_EXPECT_EQ(test, (int)ctx->aconnector->as_type, (int)ADAPTIVE_SYNC_TYPE_EDP);
}

/**
 * dm_test_fs_caps_hdmi_no_vsdb - Test an HDMI sink with no AMD VSDB
 * @test: The KUnit test context
 *
 * Without a DMCU the CEA parse fails, so no refresh range is recorded.
 */
static void dm_test_fs_caps_hdmi_no_vsdb(struct kunit *test)
{
	struct dm_test_fs_caps_ctx *ctx =
		dm_test_fs_caps_ctx_alloc(test, SIGNAL_TYPE_HDMI_TYPE_A);
	const struct drm_edid *drm_edid = dm_test_fs_edid_cea(test);

	amdgpu_dm_update_freesync_caps(&ctx->aconnector->base, drm_edid, false);

	KUNIT_EXPECT_FALSE(test, ctx->aconnector->vsdb_info.freesync_supported);
	KUNIT_EXPECT_EQ(test, ctx->aconnector->max_vfreq, 0);
	KUNIT_EXPECT_FALSE(test, dm_test_fs_caps_capable(ctx));
}

/**
 * dm_test_fs_caps_force_min_hz_quirk - Test the forced FreeSync minimum quirk
 * @test: The KUnit test context
 *
 * A quirked panel overrides the minimum refresh rate on both the connector and
 * the published monitor range once FreeSync is otherwise supported.
 */
static void dm_test_fs_caps_force_min_hz_quirk(struct kunit *test)
{
	struct dm_test_fs_caps_ctx *ctx =
		dm_test_fs_caps_ctx_alloc(test, SIGNAL_TYPE_DISPLAY_PORT);
	const struct drm_edid *drm_edid = dm_test_fs_edid_displayid(test, 40, 144);
	struct drm_connector *connector = &ctx->aconnector->base;

	ctx->link->dpcd_caps.allow_invalid_MSA_timing_param = true;
	ctx->sink->edid_caps.panel_patch.force_freesync_min_hz = 50;

	amdgpu_dm_update_freesync_caps(connector, drm_edid, false);

	KUNIT_EXPECT_EQ(test, ctx->aconnector->min_vfreq, 50);
	KUNIT_EXPECT_EQ(test, connector->display_info.monitor_range.min_vfreq, 50);
}

/**
 * dm_test_fs_caps_mccs_clears_capability - Test MCCS withdraws FreeSync support
 * @test: The KUnit test context
 *
 * When the sink advertises a FreeSync VCP code but MCCS reports no support,
 * the connector loses its FreeSync capability.
 */
static void dm_test_fs_caps_mccs_clears_capability(struct kunit *test)
{
	struct dm_test_fs_caps_ctx *ctx =
		dm_test_fs_caps_ctx_alloc(test, SIGNAL_TYPE_DISPLAY_PORT);
	const struct drm_edid *drm_edid = dm_test_fs_edid_displayid(test, 40, 144);

	ctx->link->dpcd_caps.allow_invalid_MSA_timing_param = true;
	/* A non-DP, non-HDMI link keeps dm_helpers_read_mccs_caps() off the wire. */
	ctx->link->connector_signal = SIGNAL_TYPE_VIRTUAL;
	ctx->sink->edid_caps.freesync_vcp_code = 0x60;

	amdgpu_dm_update_freesync_caps(&ctx->aconnector->base, drm_edid, true);

	KUNIT_EXPECT_FALSE(test, ctx->sink->mccs_caps.freesync_supported);
	KUNIT_EXPECT_FALSE(test, dm_test_fs_caps_capable(ctx));
}

/**
 * dm_test_fs_caps_disables_replay - Test replay is dropped without FreeSync
 * @test: The KUnit test context
 *
 * A link advertising replay support has it withdrawn when the connector ends
 * up without FreeSync capability.
 */
static void dm_test_fs_caps_disables_replay(struct kunit *test)
{
	struct dm_test_fs_caps_ctx *ctx =
		dm_test_fs_caps_ctx_alloc(test, SIGNAL_TYPE_DISPLAY_PORT);
	const struct drm_edid *drm_edid = dm_test_fs_edid_displayid(test, 40, 144);

	ctx->link->replay_settings.config.replay_supported = true;
	ctx->link->replay_settings.replay_feature_enabled = true;

	amdgpu_dm_update_freesync_caps(&ctx->aconnector->base, drm_edid, false);

	KUNIT_EXPECT_FALSE(test, ctx->link->replay_settings.config.replay_supported);
	KUNIT_EXPECT_FALSE(test, ctx->link->replay_settings.replay_feature_enabled);
}

/* Tests for amdgpu_dm_connector_init() and the get_modes() helper hook */

/*
 * amdgpu_dm_create_i2c() parents the adapter on adev->pdev->dev, so
 * devm_i2c_add_adapter() needs a registered device there. Provide one by
 * registering the device embedded in a KUnit allocated pci_dev.
 */
static void dm_test_conn_init_release_dev(struct device *dev)
{
	/* Backing storage is KUnit managed, so there is nothing to free. */
}

static void dm_test_conn_init_unregister_dev(void *data)
{
	device_unregister(data);
}

struct dm_test_conn_init_ctx {
	struct amdgpu_device *adev;
	struct drm_device *drm;
	struct amdgpu_display_manager *dm;
	struct amdgpu_dm_connector *aconnector;
	struct amdgpu_encoder *aencoder;
	struct dc_link *link;
};

static struct dm_test_conn_init_ctx *
dm_test_conn_init_ctx_alloc(struct kunit *test, enum signal_type signal)
{
	struct dm_test_conn_init_ctx *ctx;
	struct link_service *link_srv;
	struct dc_context *dc_ctx;
	struct ddc_service *ddc;
	struct pci_dev *pdev;
	struct device *dev;
	struct dc *dc;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);
	ctx->drm = __drm_kunit_helper_alloc_drm_device(test, dev,
						       sizeof(struct amdgpu_device),
						       offsetof(struct amdgpu_device, ddev),
						       DRIVER_MODESET);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx->drm);
	ctx->adev = drm_to_adev(ctx->drm);
	ctx->adev->dev = dev;
	ctx->adev->mode_info.num_crtc = 1;
	dm_test_create_mode_props(test, ctx->adev);

	pdev = kunit_kzalloc(test, sizeof(*pdev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, pdev);
	device_initialize(&pdev->dev);
	pdev->dev.parent = dev;
	pdev->dev.release = dm_test_conn_init_release_dev;
	KUNIT_ASSERT_EQ(test, dev_set_name(&pdev->dev, "dm-test-i2c-parent"), 0);
	KUNIT_ASSERT_EQ(test, device_add(&pdev->dev), 0);
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, dm_test_conn_init_unregister_dev,
						  &pdev->dev), 0);
	ctx->adev->pdev = pdev;

	ctx->dm = &ctx->adev->dm;
	ctx->dm->adev = ctx->adev;
	ctx->dm->ddev = ctx->drm;

	dc_ctx = kunit_kzalloc(test, sizeof(*dc_ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dc_ctx);
	dc_ctx->driver_context = ctx->adev;

	link_srv = kunit_kzalloc(test, sizeof(*link_srv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, link_srv);
	link_srv->dp_get_encoding_format = dm_test_gm_enc_8b10b;

	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dc);
	dc->ctx = dc_ctx;
	dc->link_srv = link_srv;

	ctx->link = kunit_kzalloc(test, sizeof(*ctx->link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->link);
	ctx->link->connector_signal = signal;
	ctx->link->dc = dc;
	ctx->link->link_enc = kunit_kzalloc(test, sizeof(*ctx->link->link_enc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->link->link_enc);

	ddc = kunit_kzalloc(test, sizeof(*ddc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ddc);
	ddc->ctx = dc_ctx;
	ddc->link = ctx->link;
	ctx->link->ddc = ddc;

	dc->links[0] = ctx->link;
	dc->link_count = 1;
	ctx->dm->dc = dc;

	/* amdgpu_dm_connector_destroy() and amdgpu_dm_encoder_destroy() kfree() these. */
	ctx->aconnector = kzalloc_obj(*ctx->aconnector);
	KUNIT_ASSERT_NOT_NULL(test, ctx->aconnector);
	ctx->aencoder = kzalloc_obj(*ctx->aencoder);
	KUNIT_ASSERT_NOT_NULL(test, ctx->aencoder);
	KUNIT_ASSERT_EQ(test, amdgpu_dm_encoder_init(ctx->drm, ctx->aencoder, 0), 0);

	return ctx;
}

/**
 * dm_test_conn_init_hdmi - Test a HDMI connector is fully brought up
 * @test: The KUnit test context
 *
 * The DC link is bound to the connector, an i2c adapter is created and
 * registered as the DDC bus, the DRM connector is initialized with the DM
 * funcs and helpers, the encoder is attached and the CEC notifier registered.
 */
static void dm_test_conn_init_hdmi(struct kunit *test)
{
	struct dm_test_conn_init_ctx *ctx =
		dm_test_conn_init_ctx_alloc(test, SIGNAL_TYPE_HDMI_TYPE_A);
	struct drm_connector *connector = &ctx->aconnector->base;

	KUNIT_ASSERT_EQ(test,
			amdgpu_dm_connector_init(ctx->dm, ctx->aconnector, 0,
						 ctx->aencoder), 0);

	KUNIT_EXPECT_PTR_EQ(test, ctx->link->priv, ctx->aconnector);
	KUNIT_ASSERT_NOT_NULL(test, ctx->aconnector->i2c);
	KUNIT_EXPECT_PTR_EQ(test, connector->ddc, &ctx->aconnector->i2c->base);
	KUNIT_EXPECT_EQ(test, connector->connector_type, DRM_MODE_CONNECTOR_HDMIA);
	KUNIT_EXPECT_NOT_NULL(test, connector->helper_private);
	KUNIT_EXPECT_PTR_EQ(test, ctx->aconnector->dc_link, ctx->link);
	KUNIT_EXPECT_EQ(test, ctx->aconnector->connector_id, 0);
	KUNIT_EXPECT_EQ(test, connector->possible_encoders,
			drm_encoder_mask(&ctx->aencoder->base));
	KUNIT_EXPECT_NOT_NULL(test, ctx->aconnector->notifier);
}

/**
 * dm_test_conn_init_dvi - Test a DVI link maps to a DVI-D connector
 * @test: The KUnit test context
 *
 * A single link DVI signal is not HDMI, so no CEC notifier is registered.
 */
static void dm_test_conn_init_dvi(struct kunit *test)
{
	struct dm_test_conn_init_ctx *ctx =
		dm_test_conn_init_ctx_alloc(test, SIGNAL_TYPE_DVI_SINGLE_LINK);

	KUNIT_ASSERT_EQ(test,
			amdgpu_dm_connector_init(ctx->dm, ctx->aconnector, 0,
						 ctx->aencoder), 0);

	KUNIT_EXPECT_EQ(test, ctx->aconnector->base.connector_type,
			DRM_MODE_CONNECTOR_DVID);
	KUNIT_EXPECT_NULL(test, ctx->aconnector->notifier);
}

/**
 * dm_test_conn_init_get_modes_hook - Test the installed get_modes helper
 * @test: The KUnit test context
 *
 * The connector helper funcs that amdgpu_dm_connector_init() installs forward
 * mode enumeration to amdgpu_dm_connector_get_modes(), which synthesizes the
 * no-EDID fallback modes.
 */
static void dm_test_conn_init_get_modes_hook(struct kunit *test)
{
	struct dm_test_conn_init_ctx *ctx =
		dm_test_conn_init_ctx_alloc(test, SIGNAL_TYPE_HDMI_TYPE_A);
	const struct drm_connector_helper_funcs *helper;
	struct drm_connector *connector = &ctx->aconnector->base;
	int count;

	KUNIT_ASSERT_EQ(test,
			amdgpu_dm_connector_init(ctx->dm, ctx->aconnector, 0,
						 ctx->aencoder), 0);

	helper = connector->helper_private;
	KUNIT_ASSERT_NOT_NULL(test, helper);
	KUNIT_ASSERT_NOT_NULL(test, helper->get_modes);

	mutex_lock(&ctx->drm->mode_config.mutex);
	count = helper->get_modes(connector);
	mutex_unlock(&ctx->drm->mode_config.mutex);

	KUNIT_EXPECT_GT(test, count, 0);
	KUNIT_EXPECT_GT(test, ctx->aconnector->num_modes, 0);
}

/* Tests for dm_force_atomic_commit() */

/* Fails the very first state duplication so the error path is taken. */
static struct drm_connector_state *
dm_test_force_dup_state_fail(struct drm_connector *connector)
{
	return NULL;
}

static const struct drm_connector_funcs dm_test_force_fail_funcs = {
	.reset = amdgpu_dm_connector_funcs_reset,
	.atomic_duplicate_state = dm_test_force_dup_state_fail,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

/* The atomic modeset check dereferences the helper funcs unconditionally. */
static int dm_test_force_get_modes(struct drm_connector *connector)
{
	return 0;
}

static const struct drm_connector_helper_funcs dm_test_force_conn_helper_funcs = {
	.get_modes = dm_test_force_get_modes,
};

struct dm_test_force_ctx {
	struct drm_device *drm;
	struct amdgpu_dm_connector *aconnector;
	struct drm_modeset_acquire_ctx acquire_ctx;
};

static void dm_test_force_drop_locks(void *data)
{
	struct dm_test_force_ctx *ctx = data;

	ctx->drm->mode_config.acquire_ctx = NULL;
	drm_modeset_drop_locks(&ctx->acquire_ctx);
	drm_modeset_acquire_fini(&ctx->acquire_ctx);
}

/*
 * Build a disabled but fully initialized pipe: a primary plane, a CRTC (whose
 * drm_crtc is the first member of amdgpu_crtc, so to_amdgpu_crtc() resolves)
 * and a connector wired to an encoder that still points at the CRTC, which is
 * the state dm_force_atomic_commit() is asked to restore.
 */
static struct dm_test_force_ctx *
dm_test_force_ctx_alloc(struct kunit *test, const struct drm_connector_funcs *funcs)
{
	struct dm_test_force_ctx *ctx;
	struct drm_encoder *encoder;
	struct drm_plane *primary;
	struct drm_crtc *crtc;
	struct device *dev;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);
	ctx->drm = __drm_kunit_helper_alloc_drm_device(test, dev,
						       sizeof(struct amdgpu_device),
						       offsetof(struct amdgpu_device, ddev),
						       DRIVER_MODESET | DRIVER_ATOMIC);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx->drm);

	primary = drm_kunit_helper_create_primary_plane(test, ctx->drm, NULL, NULL,
							NULL, 0, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, primary);
	crtc = drm_kunit_helper_create_crtc(test, ctx->drm, primary, NULL, NULL, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, crtc);

	ctx->aconnector = drmm_kzalloc(ctx->drm, sizeof(*ctx->aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->aconnector);
	KUNIT_ASSERT_EQ(test,
			drmm_connector_init(ctx->drm, &ctx->aconnector->base, funcs,
					    DRM_MODE_CONNECTOR_HDMIA, NULL), 0);
	drm_connector_helper_add(&ctx->aconnector->base, &dm_test_force_conn_helper_funcs);

	encoder = drmm_kzalloc(ctx->drm, sizeof(*encoder), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, encoder);
	KUNIT_ASSERT_EQ(test,
			drmm_encoder_init(ctx->drm, encoder, NULL,
					  DRM_MODE_ENCODER_TMDS, NULL), 0);
	encoder->crtc = crtc;
	ctx->aconnector->base.encoder = encoder;

	drm_mode_config_reset(ctx->drm);

	drm_modeset_acquire_init(&ctx->acquire_ctx, 0);
	ctx->drm->mode_config.acquire_ctx = &ctx->acquire_ctx;
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, dm_test_force_drop_locks, ctx), 0);

	return ctx;
}

/**
 * dm_test_force_commit_succeeds - Test the forced restore commits the pipe
 * @test: The KUnit test context
 *
 * The connector, its CRTC and the CRTC primary plane are pulled into a fresh
 * atomic state, the CRTC is marked as needing a modeset and the commit
 * succeeds.
 */
static void dm_test_force_commit_succeeds(struct kunit *test)
{
	struct dm_test_force_ctx *ctx =
		dm_test_force_ctx_alloc(test, &dm_test_connector_funcs);

	KUNIT_EXPECT_EQ(test, dm_force_atomic_commit(&ctx->aconnector->base), 0);
}

/**
 * dm_test_force_commit_conn_state_error - Test a failed connector state duplication
 * @test: The KUnit test context
 *
 * When the connector state cannot be duplicated the error is propagated and
 * the partially built atomic state is released without committing.
 */
static void dm_test_force_commit_conn_state_error(struct kunit *test)
{
	struct dm_test_force_ctx *ctx =
		dm_test_force_ctx_alloc(test, &dm_test_force_fail_funcs);

	KUNIT_EXPECT_EQ(test, dm_force_atomic_commit(&ctx->aconnector->base), -ENOMEM);
}

static struct kunit_case amdgpu_dm_connector_tests[] = {
	/* get_subconnector_type */
	KUNIT_CASE(dm_test_subconnector_type_none),
	KUNIT_CASE(dm_test_subconnector_type_vga),
	KUNIT_CASE(dm_test_subconnector_type_dvi_converter),
	KUNIT_CASE(dm_test_subconnector_type_dvi_dongle),
	KUNIT_CASE(dm_test_subconnector_type_hdmi_converter),
	KUNIT_CASE(dm_test_subconnector_type_hdmi_dongle),
	KUNIT_CASE(dm_test_subconnector_type_mismatched),
	KUNIT_CASE(dm_test_subconnector_type_default_unknown),
	/* get_output_content_type */
	KUNIT_CASE(dm_test_content_type_no_data),
	KUNIT_CASE(dm_test_content_type_graphics),
	KUNIT_CASE(dm_test_content_type_photo),
	KUNIT_CASE(dm_test_content_type_cinema),
	KUNIT_CASE(dm_test_content_type_game),
	KUNIT_CASE(dm_test_content_type_unknown_defaults_no_data),
	/* adjust_colour_depth_from_display_info */
	KUNIT_CASE(dm_test_adjust_colour_depth_fits_at_888),
	KUNIT_CASE(dm_test_adjust_colour_depth_reduces_to_888),
	KUNIT_CASE(dm_test_adjust_colour_depth_10bpc_passes),
	KUNIT_CASE(dm_test_adjust_colour_depth_420_halves_clk),
	KUNIT_CASE(dm_test_adjust_colour_depth_420_reduces),
	KUNIT_CASE(dm_test_adjust_colour_depth_reduces_12bpc_to_10bpc),
	KUNIT_CASE(dm_test_adjust_colour_depth_16bpc_no_fallback),
	KUNIT_CASE(dm_test_adjust_colour_depth_none_fits),
	KUNIT_CASE(dm_test_adjust_colour_depth_invalid_depth),
	/* amdgpu_dm_get_output_color_space */
	KUNIT_CASE(dm_test_output_color_space_default_rgb_full),
	KUNIT_CASE(dm_test_output_color_space_default_rgb_limited),
	KUNIT_CASE(dm_test_output_color_space_default_ycbcr709),
	KUNIT_CASE(dm_test_output_color_space_default_ycbcr601_limited),
	KUNIT_CASE(dm_test_output_color_space_bt601_y_only),
	KUNIT_CASE(dm_test_output_color_space_bt601),
	KUNIT_CASE(dm_test_output_color_space_bt709),
	KUNIT_CASE(dm_test_output_color_space_bt709_y_only),
	KUNIT_CASE(dm_test_output_color_space_oprgb),
	KUNIT_CASE(dm_test_output_color_space_bt2020_rgb),
	KUNIT_CASE(dm_test_output_color_space_bt2020_rgb_limited),
	KUNIT_CASE(dm_test_output_color_space_default_rgb_auto_hdmi_cta),
	KUNIT_CASE(dm_test_output_color_space_default_rgb_auto_hdmi_vga),
	KUNIT_CASE(dm_test_output_color_space_default_rgb_auto_dp_cta),
	KUNIT_CASE(dm_test_output_color_space_default_rgb_full_hdmi_cta),
	KUNIT_CASE(dm_test_output_color_space_bt2020_rgb_auto_hdmi_cta),
	KUNIT_CASE(dm_test_output_color_space_bt2020_ycc),
	KUNIT_CASE(dm_test_output_color_space_default_ycbcr709_y_only),
	KUNIT_CASE(dm_test_output_color_space_default_ycbcr601),
	KUNIT_CASE(dm_test_output_color_space_bt2020_ycc_rgb_encoding),
	KUNIT_CASE(dm_test_output_color_space_bt2020_ycc_rgb_encoding_limited),
	KUNIT_CASE(dm_test_output_color_space_bt2020_rgb_ycc_encoding),
	/* Tests for amdgpu_dm_convert_dc_color_depth_into_bpc */
	KUNIT_CASE(dm_test_convert_color_depth_bpc_mappings),
	KUNIT_CASE(dm_test_convert_color_depth_bpc_unknown),
	/* amdgpu_dm_convert_color_depth_from_display_info */
	KUNIT_CASE(dm_test_color_depth_from_info_bpc8),
	KUNIT_CASE(dm_test_color_depth_from_info_bpc10),
	KUNIT_CASE(dm_test_color_depth_from_info_zero_bpc_defaults_888),
	KUNIT_CASE(dm_test_color_depth_from_info_requested_bpc_caps),
	KUNIT_CASE(dm_test_color_depth_from_info_y420_default),
	KUNIT_CASE(dm_test_color_depth_from_info_y420_10bpc),
	KUNIT_CASE(dm_test_color_depth_from_info_y420_12bpc),
	KUNIT_CASE(dm_test_color_depth_from_info_y420_16bpc),
	KUNIT_CASE(dm_test_color_depth_from_info_requested_odd_bpc),
	KUNIT_CASE(dm_test_color_depth_from_info_unsupported_bpc),
	/* to_drm_connector_type */
	KUNIT_CASE(dm_test_to_connector_type_hdmi),
	KUNIT_CASE(dm_test_to_connector_type_edp),
	KUNIT_CASE(dm_test_to_connector_type_lvds),
	KUNIT_CASE(dm_test_to_connector_type_rgb),
	KUNIT_CASE(dm_test_to_connector_type_dp),
	KUNIT_CASE(dm_test_to_connector_type_dp_mst),
	KUNIT_CASE(dm_test_to_connector_type_dvi_dvii),
	KUNIT_CASE(dm_test_to_connector_type_dual_link_dvii),
	KUNIT_CASE(dm_test_to_connector_type_dvi_dvid),
	KUNIT_CASE(dm_test_to_connector_type_dual_link_dvid),
	KUNIT_CASE(dm_test_to_connector_type_virtual),
	KUNIT_CASE(dm_test_to_connector_type_unknown),
	/* is_duplicate_mode */
	KUNIT_CASE(dm_test_is_duplicate_mode_empty_list),
	KUNIT_CASE(dm_test_is_duplicate_mode_match),
	KUNIT_CASE(dm_test_is_duplicate_mode_no_match),
	KUNIT_CASE(dm_test_is_duplicate_mode_same_size_different_clock),
	/* amdgpu_dm_get_encoder_crtc_mask */
	KUNIT_CASE(dm_test_encoder_crtc_mask_1),
	KUNIT_CASE(dm_test_encoder_crtc_mask_2),
	KUNIT_CASE(dm_test_encoder_crtc_mask_3),
	KUNIT_CASE(dm_test_encoder_crtc_mask_4),
	KUNIT_CASE(dm_test_encoder_crtc_mask_5),
	KUNIT_CASE(dm_test_encoder_crtc_mask_6),
	KUNIT_CASE(dm_test_encoder_crtc_mask_default),
	/* get_aspect_ratio */
	KUNIT_CASE(dm_test_aspect_ratio_no_data),
	KUNIT_CASE(dm_test_aspect_ratio_4_3),
	KUNIT_CASE(dm_test_aspect_ratio_16_9),
	KUNIT_CASE(dm_test_aspect_ratio_64_27),
	KUNIT_CASE(dm_test_aspect_ratio_256_135),
	/* copy_crtc_timing_for_drm_display_mode */
	KUNIT_CASE(dm_test_copy_crtc_timing_copies_all_fields),
	KUNIT_CASE(dm_test_copy_crtc_timing_leaves_non_crtc_fields),
	/* decide_crtc_timing_for_drm_display_mode */
	KUNIT_CASE(dm_test_decide_crtc_timing_scale_enabled),
	KUNIT_CASE(dm_test_decide_crtc_timing_matching_mode),
	KUNIT_CASE(dm_test_decide_crtc_timing_no_copy),
	KUNIT_CASE(dm_test_decide_crtc_timing_no_crtc_clock),
	KUNIT_CASE(dm_test_decide_crtc_timing_sink_mode_same_totals),
	KUNIT_CASE(dm_test_decide_crtc_timing_inserted_mode),
	/* amdgpu_dm_connector_funcs_reset */
	KUNIT_CASE(dm_test_funcs_reset_sets_defaults),
	KUNIT_CASE(dm_test_funcs_reset_edp_abm_level),
	KUNIT_CASE(dm_test_funcs_reset_edp_abm_disabled),
	/* amdgpu_dm_connector_atomic_duplicate_state */
	KUNIT_CASE(dm_test_atomic_dup_state_copies_fields),
	/* amdgpu_dm_fill_hdr_info_packet */
	KUNIT_CASE(dm_test_fill_hdr_null_metadata),
	KUNIT_CASE(dm_test_fill_hdr_zeroes_output),
	KUNIT_CASE(dm_test_fill_hdr_hdmi),
	KUNIT_CASE(dm_test_fill_hdr_dp),
	KUNIT_CASE(dm_test_fill_hdr_unsupported_connector),
	KUNIT_CASE(dm_test_fill_hdr_bad_metadata),
	/* amdgpu_dm_connector_atomic_check */
	KUNIT_CASE(dm_test_conn_atomic_check_no_crtc),
	KUNIT_CASE(dm_test_conn_atomic_check_dp_mst),
	KUNIT_CASE(dm_test_conn_atomic_check_no_change),
	KUNIT_CASE(dm_test_conn_atomic_check_privacy_change),
	KUNIT_CASE(dm_test_conn_atomic_check_colorspace_change),
	KUNIT_CASE(dm_test_conn_atomic_check_content_type_change),
	KUNIT_CASE(dm_test_conn_atomic_check_hdr_exit),
	KUNIT_CASE(dm_test_conn_atomic_check_hdr_fill_error),
	/* amdgpu_dm_connector_atomic_set_property */
	KUNIT_CASE(dm_test_set_property_scaling_center),
	KUNIT_CASE(dm_test_set_property_scaling_aspect),
	KUNIT_CASE(dm_test_set_property_scaling_fullscreen),
	KUNIT_CASE(dm_test_set_property_scaling_none),
	KUNIT_CASE(dm_test_set_property_scaling_unchanged),
	KUNIT_CASE(dm_test_set_property_underscan_hborder),
	KUNIT_CASE(dm_test_set_property_underscan_vborder),
	KUNIT_CASE(dm_test_set_property_underscan_enable),
	KUNIT_CASE(dm_test_set_property_abm_sysfs_control),
	KUNIT_CASE(dm_test_set_property_abm_level_off),
	KUNIT_CASE(dm_test_set_property_abm_level_value),
	KUNIT_CASE(dm_test_set_property_unknown),
	/* amdgpu_dm_connector_atomic_get_property */
	KUNIT_CASE(dm_test_get_property_scaling_center),
	KUNIT_CASE(dm_test_get_property_scaling_aspect),
	KUNIT_CASE(dm_test_get_property_scaling_full),
	KUNIT_CASE(dm_test_get_property_scaling_off),
	KUNIT_CASE(dm_test_get_property_underscan_borders),
	KUNIT_CASE(dm_test_get_property_abm_sysfs_allowed),
	KUNIT_CASE(dm_test_get_property_abm_level),
	KUNIT_CASE(dm_test_get_property_abm_disabled_zero),
	KUNIT_CASE(dm_test_get_property_unknown),
	/* amdgpu_dm_get_highest_refresh_rate_mode */
	KUNIT_CASE(dm_test_highest_refresh_writeback_null),
	KUNIT_CASE(dm_test_highest_refresh_cached_base),
	KUNIT_CASE(dm_test_highest_refresh_preferred_mode),
	/* amdgpu_dm_is_freesync_video_mode */
	KUNIT_CASE(dm_test_is_freesync_video_mode_null_mode),
	KUNIT_CASE(dm_test_is_freesync_video_mode_match),
	KUNIT_CASE(dm_test_is_freesync_video_mode_no_match),
	/* update_subconnector_property */
	KUNIT_CASE(dm_test_update_subconnector_dp_with_sink),
	KUNIT_CASE(dm_test_update_subconnector_dp_no_sink),
	KUNIT_CASE(dm_test_update_subconnector_non_dp_noop),
	/* amdgpu_dm_update_cacp_caps */
	KUNIT_CASE(dm_test_cacp_caps_unsupported_ip),
	KUNIT_CASE(dm_test_cacp_caps_excluded_ip_316),
	KUNIT_CASE(dm_test_cacp_caps_edp_oled_supported),
	KUNIT_CASE(dm_test_cacp_caps_lvds_oled_supported),
	KUNIT_CASE(dm_test_cacp_caps_non_edp_signal),
	KUNIT_CASE(dm_test_cacp_caps_lcd_panel),
	/* amdgpu_dm_set_panel_type */
	KUNIT_CASE(dm_test_set_panel_type_vsdb_oled),
	KUNIT_CASE(dm_test_set_panel_type_vsdb_miniled),
	KUNIT_CASE(dm_test_set_panel_type_dpcd_oled),
	KUNIT_CASE(dm_test_set_panel_type_dpcd_miniled),
	KUNIT_CASE(dm_test_set_panel_type_did_oled),
	KUNIT_CASE(dm_test_set_panel_type_did_lcd),
	KUNIT_CASE(dm_test_set_panel_type_vendor_lum_heuristic),
	KUNIT_CASE(dm_test_set_panel_type_defaults_to_lcd),
	/* amdgpu_dm_fbc_init */
	KUNIT_CASE(dm_test_fbc_init_no_compressor),
	KUNIT_CASE(dm_test_fbc_init_non_edp),
	KUNIT_CASE(dm_test_fbc_init_already_allocated),
	KUNIT_CASE(dm_test_fbc_init_no_modes),
	/* amdgpu_dm_detect_mst_link_for_all_connectors */
	KUNIT_CASE(dm_test_detect_mst_no_connectors),
	KUNIT_CASE(dm_test_detect_mst_skips_writeback),
	KUNIT_CASE(dm_test_detect_mst_non_mst_link),
	KUNIT_CASE(dm_test_detect_mst_branch_without_aux),
	KUNIT_CASE(dm_test_detect_mst_start_fail),
	/* amdgpu_dm_find_first_crtc_matching_connector */
	KUNIT_CASE(dm_test_find_first_crtc_match),
	KUNIT_CASE(dm_test_find_first_crtc_no_match),
	KUNIT_CASE(dm_test_find_first_crtc_empty_state),
	KUNIT_CASE(dm_test_find_first_crtc_skips_null_ptr),
	KUNIT_CASE(dm_test_find_first_crtc_returns_first),
	/* amdgpu_dm_set_panel_type */
	KUNIT_CASE(dm_test_set_panel_type_samsung_miniled),
	KUNIT_CASE(dm_test_set_panel_type_samsung_below_threshold),
	KUNIT_CASE(dm_test_set_panel_type_default_lcd),
	/* amdgpu_dm_update_cacp_caps */
	KUNIT_CASE(dm_test_cacp_caps_edp_supported),
	KUNIT_CASE(dm_test_cacp_caps_lvds_supported),
	KUNIT_CASE(dm_test_cacp_caps_old_ip_unsupported),
	KUNIT_CASE(dm_test_cacp_caps_ip_3_1_6_unsupported),
	KUNIT_CASE(dm_test_cacp_caps_non_edp_lvds_unsupported),
	KUNIT_CASE(dm_test_cacp_caps_lcd_unsupported),
	/* fill_stream_properties_from_drm_display_mode */
	KUNIT_CASE(dm_test_fill_stream_borders_zeroed),
	KUNIT_CASE(dm_test_fill_stream_rgb_defaults),
	KUNIT_CASE(dm_test_fill_stream_sync_polarity_positive),
	KUNIT_CASE(dm_test_fill_stream_sync_polarity_negative),
	KUNIT_CASE(dm_test_fill_stream_inherits_old_stream),
	KUNIT_CASE(dm_test_fill_stream_timing_from_crtc),
	KUNIT_CASE(dm_test_fill_stream_color_depth_requested_bpc),
	KUNIT_CASE(dm_test_fill_stream_content_type),
	KUNIT_CASE(dm_test_fill_stream_aspect_ratio),
	KUNIT_CASE(dm_test_fill_stream_encoding_from_caller_ycbcr420),
	KUNIT_CASE(dm_test_fill_stream_encoding_from_caller_ycbcr422),
	KUNIT_CASE(dm_test_fill_stream_encoding_from_caller_ycbcr444),
	KUNIT_CASE(dm_test_fill_stream_hdmi_ep_clamps_depth),
	KUNIT_CASE(dm_test_fill_stream_non_hdmi_ep_keeps_depth),
	KUNIT_CASE(dm_test_fill_stream_hdmi_infoframe),
	KUNIT_CASE(dm_test_fill_stream_freesync_video),
	/* create_stream_for_sink */
	KUNIT_CASE(dm_test_create_stream_fake_sink_success),
	KUNIT_CASE(dm_test_create_stream_sets_dm_context),
	KUNIT_CASE(dm_test_create_stream_virtual_signal),
	KUNIT_CASE(dm_test_create_stream_scaling_src),
	KUNIT_CASE(dm_test_create_stream_existing_sink),
	KUNIT_CASE(dm_test_create_stream_preferred_mode),
	KUNIT_CASE(dm_test_create_stream_scaled_keeps_refresh),
	KUNIT_CASE(dm_test_create_stream_timing_override),
	KUNIT_CASE(dm_test_create_stream_dp_vsc_colorimetry),
	KUNIT_CASE(dm_test_create_stream_dp_colorimetry_quirk),
	KUNIT_CASE(dm_test_create_stream_hdmi_vsif),
	/* amdgpu_dm_connector_detect */
	KUNIT_CASE(dm_test_detect_force_on),
	KUNIT_CASE(dm_test_detect_force_on_digital),
	KUNIT_CASE(dm_test_detect_force_off),
	KUNIT_CASE(dm_test_detect_sink_present),
	KUNIT_CASE(dm_test_detect_no_sink),
	KUNIT_CASE(dm_test_detect_hides_secondary_tile),
	/* amdgpu_dm_connector_poll */
	KUNIT_CASE(dm_test_poll_dac_load_returns_cached),
	KUNIT_CASE(dm_test_poll_connected_cached_sink),
	KUNIT_CASE(dm_test_poll_connected_new_sink),
	KUNIT_CASE(dm_test_poll_disconnect_releases_sink),
	/* amdgpu_dm_connector_late_register */
	KUNIT_CASE(dm_test_late_register_non_dp_succeeds),
	/* amdgpu_dm_connector_unregister */
	KUNIT_CASE(dm_test_unregister_non_dp_noop),
	/* amdgpu_dm_connector_destroy */
	KUNIT_CASE(dm_test_destroy_minimal),
	KUNIT_CASE(dm_test_destroy_releases_dc_sink),
	KUNIT_CASE(dm_test_destroy_releases_dc_em_sink),
	/* dm_encoder_helper_disable */
	KUNIT_CASE(dm_test_encoder_disable_noop),
	/* dm_encoder_helper_atomic_check */
	KUNIT_CASE(dm_test_atomic_check_edp_native_keeps_scaling),
	KUNIT_CASE(dm_test_atomic_check_lvds_non_native_enables_scaling),
	KUNIT_CASE(dm_test_atomic_check_non_mst_returns_zero),
	KUNIT_CASE(dm_test_atomic_check_mst_no_change_returns_zero),
	KUNIT_CASE(dm_test_atomic_check_mst_finds_vcpi_slots),
	KUNIT_CASE(dm_test_atomic_check_mst_duplicated_skips_pbn),
	KUNIT_CASE(dm_test_atomic_check_mst_topology_err_propagates),
	KUNIT_CASE(dm_test_atomic_check_mst_vcpi_error_propagates),
	KUNIT_CASE(dm_test_encoder_init_success),
	/* hdmi_cec_unset_edid */
	KUNIT_CASE(dm_test_hdmi_cec_unset_edid_no_notifier),
	/* create_eml_sink */
	KUNIT_CASE(dm_test_create_eml_sink_no_edid),
	KUNIT_CASE(dm_test_create_eml_sink_reads_edid),
	KUNIT_CASE(dm_test_create_eml_sink_force_on_em),
	KUNIT_CASE(dm_test_create_eml_sink_force_on_local),
	/* handle_edid_mgmt */
	KUNIT_CASE(dm_test_handle_edid_mgmt_dp_sets_link_caps),
	KUNIT_CASE(dm_test_handle_edid_mgmt_non_dp_leaves_caps),
	/* amdgpu_dm_connector_funcs_force */
	KUNIT_CASE(dm_test_funcs_force_no_edid),
	KUNIT_CASE(dm_test_funcs_force_reads_edid),
	/* amdgpu_dm_connector_get_modes */
	KUNIT_CASE(dm_test_get_modes_noedid_default),
	KUNIT_CASE(dm_test_get_modes_noedid_128b_adds_more),
	KUNIT_CASE(dm_test_get_modes_noedid_analog_adds_common),
	KUNIT_CASE(dm_test_get_modes_with_edid),
	/* amdgpu_set_panel_orientation */
	KUNIT_CASE(dm_test_panel_orientation_non_edp),
	KUNIT_CASE(dm_test_panel_orientation_no_native_mode),
	KUNIT_CASE(dm_test_panel_orientation_applies_quirk),
	/* amdgpu_dm_prune_primary_tile_modes */
	KUNIT_CASE(dm_test_prune_no_sink),
	KUNIT_CASE(dm_test_prune_no_patch),
	KUNIT_CASE(dm_test_prune_no_tile),
	KUNIT_CASE(dm_test_prune_secondary_tile),
	KUNIT_CASE(dm_test_prune_removes_per_tile),
	/* dm_validate_stream_and_context */
	KUNIT_CASE(dm_test_validate_stream_null_stream),
	KUNIT_CASE(dm_test_validate_stream_dc_ok_no_pipe),
	/* amdgpu_dm_connector_to_encoder */
	KUNIT_CASE(dm_test_to_encoder_no_encoder),
	KUNIT_CASE(dm_test_to_encoder_returns_attached),
	/* amdgpu_dm_get_native_mode */
	KUNIT_CASE(dm_test_native_mode_no_encoder),
	KUNIT_CASE(dm_test_native_mode_empty_probed_zeroes_clock),
	KUNIT_CASE(dm_test_native_mode_copies_preferred),
	/* amdgpu_dm_create_common_mode */
	KUNIT_CASE(dm_test_create_common_mode_overrides),
	/* amdgpu_dm_connector_add_common_modes */
	KUNIT_CASE(dm_test_add_common_modes_non_edp_noop),
	KUNIT_CASE(dm_test_add_common_modes_edp_adds),
	/* amdgpu_dm_connector_ddc_get_modes */
	KUNIT_CASE(dm_test_ddc_get_modes_null_edid),
	/* add_fs_modes */
	KUNIT_CASE(dm_test_add_fs_modes_no_preferred_mode),
	KUNIT_CASE(dm_test_add_fs_modes_generates),
	KUNIT_CASE(dm_test_add_fs_modes_out_of_range),
	KUNIT_CASE(dm_test_add_fs_modes_skips_illegal),
	/* amdgpu_dm_connector_add_freesync_modes */
	KUNIT_CASE(dm_test_add_freesync_modes_null_edid_noop),
	/* amdgpu_dm_i2c_func */
	KUNIT_CASE(dm_test_i2c_func_returns_flags),
	/* amdgpu_dm_i2c_xfer */
	KUNIT_CASE(dm_test_i2c_xfer_no_ddc_pin),
	KUNIT_CASE(dm_test_i2c_xfer_hw_submit_fails),
	KUNIT_CASE(dm_test_i2c_xfer_oem_no_device),
	/* amdgpu_dm_create_i2c */
	KUNIT_CASE(dm_test_create_i2c_oem),
	KUNIT_CASE(dm_test_create_i2c_hw_bus),
	/* dm_restore_drm_connector_state */
	KUNIT_CASE(dm_test_restore_state_writeback),
	KUNIT_CASE(dm_test_restore_state_no_dc_sink),
	KUNIT_CASE(dm_test_restore_state_no_connector_state),
	KUNIT_CASE(dm_test_restore_state_no_encoder),
	KUNIT_CASE(dm_test_restore_state_no_stream),
	KUNIT_CASE(dm_test_restore_state_same_sink),
	/* get_amd_vsdb */
	KUNIT_CASE(dm_test_get_amd_vsdb_unsupported),
	KUNIT_CASE(dm_test_get_amd_vsdb_supported),
	/* parse_edid_displayid_vrr */
	KUNIT_CASE(dm_test_parse_displayid_vrr_null_edid),
	KUNIT_CASE(dm_test_parse_displayid_vrr_no_displayid),
	KUNIT_CASE(dm_test_parse_displayid_vrr_sets_range),
	/* amdgpu_dm_connector_mode_valid */
	KUNIT_CASE(dm_test_mode_valid_interlace_rejected),
	KUNIT_CASE(dm_test_mode_valid_dblscan_rejected),
	KUNIT_CASE(dm_test_mode_valid_no_dc_sink),
	KUNIT_CASE(dm_test_mode_valid_force_on_no_stream),
	KUNIT_CASE(dm_test_mode_valid_edid_mgmt_forced),
	/* amdgpu_dm_hdmi_cec_set_edid */
	KUNIT_CASE(dm_test_hdmi_cec_set_edid_no_notifier),
	/* amdgpu_dm_s3_handle_hdmi_cec */
	KUNIT_CASE(dm_test_s3_handle_hdmi_cec_suspend),
	KUNIT_CASE(dm_test_s3_handle_hdmi_cec_resume),
	/* amdgpu_dm_create_validate_stream_for_sink */
	KUNIT_CASE(dm_test_create_validate_stream_null_dm_state),
	KUNIT_CASE(dm_test_create_validate_stream_writeback),
	KUNIT_CASE(dm_test_create_validate_stream_no_valid_bpc),
	KUNIT_CASE(dm_test_create_validate_stream_hdmi_ycbcr),
	KUNIT_CASE(dm_test_create_validate_stream_force_ycbcr420),
	KUNIT_CASE(dm_test_create_validate_stream_force_ycbcr422),
	KUNIT_CASE(dm_test_create_validate_stream_force_ycbcr444),
	KUNIT_CASE(dm_test_create_validate_stream_prune_timing),
	KUNIT_CASE(dm_test_create_validate_stream_prune_context),
	KUNIT_CASE(dm_test_create_validate_stream_hdmi_rgb_first),
	/* amdgpu_dm_update_connector_after_detect */
	KUNIT_CASE(dm_test_update_after_detect_mst_noop),
	KUNIT_CASE(dm_test_update_after_detect_sink_unchanged),
	KUNIT_CASE(dm_test_update_after_detect_mst_sink),
	KUNIT_CASE(dm_test_update_after_detect_connect_edid),
	KUNIT_CASE(dm_test_update_after_detect_replace_no_edid),
	KUNIT_CASE(dm_test_update_after_detect_disconnect),
	KUNIT_CASE(dm_test_update_after_detect_force_em_adopt),
	KUNIT_CASE(dm_test_update_after_detect_force_em_replace),
	KUNIT_CASE(dm_test_update_after_detect_force_em_fake),
	KUNIT_CASE(dm_test_update_after_detect_force_em_keep),
	/* amdgpu_dm_update_stream_scaling_settings */
	KUNIT_CASE(dm_test_update_scaling_null_mode),
	KUNIT_CASE(dm_test_update_scaling_fullscreen_default),
	KUNIT_CASE(dm_test_update_scaling_rmx_full),
	KUNIT_CASE(dm_test_update_scaling_rmx_aspect_pillarbox),
	KUNIT_CASE(dm_test_update_scaling_rmx_aspect_letterbox),
	KUNIT_CASE(dm_test_update_scaling_rmx_center),
	KUNIT_CASE(dm_test_update_scaling_underscan),
	/* hdmi_frl_status_polling_work */
	KUNIT_CASE(dm_test_frl_no_links),
	KUNIT_CASE(dm_test_frl_skips_no_local_sink),
	KUNIT_CASE(dm_test_frl_skips_non_hdmi),
	KUNIT_CASE(dm_test_frl_skips_zero_rate),
	KUNIT_CASE(dm_test_frl_poll_no_update),
	KUNIT_CASE(dm_test_frl_poll_retrains),
	/* amdgpu_dm_connector_init_helper */
	KUNIT_CASE(dm_test_init_helper_common_defaults),
	KUNIT_CASE(dm_test_init_helper_hdmi),
	KUNIT_CASE(dm_test_init_helper_dp),
	KUNIT_CASE(dm_test_init_helper_dp_mst_root),
	KUNIT_CASE(dm_test_init_helper_dvid),
	KUNIT_CASE(dm_test_init_helper_vga),
	KUNIT_CASE(dm_test_init_helper_unpolled_default),
	KUNIT_CASE(dm_test_init_helper_edp),
	KUNIT_CASE(dm_test_init_helper_hdcp_property),
	KUNIT_CASE(dm_test_init_helper_hpd_debounce_disabled),
	/* amdgpu_dm_initialize_hdmi_connector */
	KUNIT_CASE(dm_test_initialize_hdmi_cec_registers),
	/* amdgpu_dm_update_freesync_caps */
	KUNIT_CASE(dm_test_fs_caps_dp_msa_range),
	KUNIT_CASE(dm_test_fs_caps_dp_narrow_range),
	KUNIT_CASE(dm_test_fs_caps_dp_msa_not_allowed),
	KUNIT_CASE(dm_test_fs_caps_edp_replay_mode),
	KUNIT_CASE(dm_test_fs_caps_hdmi_no_vsdb),
	KUNIT_CASE(dm_test_fs_caps_force_min_hz_quirk),
	KUNIT_CASE(dm_test_fs_caps_mccs_clears_capability),
	KUNIT_CASE(dm_test_fs_caps_disables_replay),
	/* amdgpu_dm_connector_init */
	KUNIT_CASE(dm_test_conn_init_hdmi),
	KUNIT_CASE(dm_test_conn_init_dvi),
	KUNIT_CASE(dm_test_conn_init_get_modes_hook),
	/* dm_force_atomic_commit */
	KUNIT_CASE(dm_test_force_commit_succeeds),
	KUNIT_CASE(dm_test_force_commit_conn_state_error),
	{}
};

static struct kunit_suite amdgpu_dm_connector_test_suite = {
	.name = "amdgpu_dm_connector",
	.test_cases = amdgpu_dm_connector_tests,
};

kunit_test_suite(amdgpu_dm_connector_test_suite);

MODULE_AUTHOR("AMD");
MODULE_DESCRIPTION("KUnit tests for amdgpu_dm_connector");
MODULE_LICENSE("Dual MIT/GPL");
