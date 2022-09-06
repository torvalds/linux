// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 Bootlin
 * Copyright (c) 2022 Maíra Canal <mairacanal@riseup.net>
 */

#include <kunit/test.h>

#include <drm/drm_connector.h>
#include <drm/drm_modes.h>

static const struct drm_connector no_connector = {};

static void drm_cmdline_test_force_e_only(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "e";

	KUNIT_EXPECT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &no_connector, &mode));
	KUNIT_EXPECT_FALSE(test, mode.specified);
	KUNIT_EXPECT_FALSE(test, mode.refresh_specified);
	KUNIT_EXPECT_FALSE(test, mode.bpp_specified);

	KUNIT_EXPECT_FALSE(test, mode.rb);
	KUNIT_EXPECT_FALSE(test, mode.cvt);
	KUNIT_EXPECT_FALSE(test, mode.interlace);
	KUNIT_EXPECT_FALSE(test, mode.margins);
	KUNIT_EXPECT_EQ(test, mode.force, DRM_FORCE_ON);
}

static void drm_cmdline_test_force_D_only_not_digital(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "D";

	KUNIT_EXPECT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &no_connector, &mode));
	KUNIT_EXPECT_FALSE(test, mode.specified);
	KUNIT_EXPECT_FALSE(test, mode.refresh_specified);
	KUNIT_EXPECT_FALSE(test, mode.bpp_specified);

	KUNIT_EXPECT_FALSE(test, mode.rb);
	KUNIT_EXPECT_FALSE(test, mode.cvt);
	KUNIT_EXPECT_FALSE(test, mode.interlace);
	KUNIT_EXPECT_FALSE(test, mode.margins);
	KUNIT_EXPECT_EQ(test, mode.force, DRM_FORCE_ON);
}

static const struct drm_connector connector_hdmi = {
	.connector_type	= DRM_MODE_CONNECTOR_HDMIB,
};

static void drm_cmdline_test_force_D_only_hdmi(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "D";

	KUNIT_EXPECT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &connector_hdmi, &mode));
	KUNIT_EXPECT_FALSE(test, mode.specified);
	KUNIT_EXPECT_FALSE(test, mode.refresh_specified);
	KUNIT_EXPECT_FALSE(test, mode.bpp_specified);

	KUNIT_EXPECT_FALSE(test, mode.rb);
	KUNIT_EXPECT_FALSE(test, mode.cvt);
	KUNIT_EXPECT_FALSE(test, mode.interlace);
	KUNIT_EXPECT_FALSE(test, mode.margins);
	KUNIT_EXPECT_EQ(test, mode.force, DRM_FORCE_ON_DIGITAL);
}

static const struct drm_connector connector_dvi = {
	.connector_type	= DRM_MODE_CONNECTOR_DVII,
};

static void drm_cmdline_test_force_D_only_dvi(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "D";

	KUNIT_EXPECT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &connector_dvi, &mode));
	KUNIT_EXPECT_FALSE(test, mode.specified);
	KUNIT_EXPECT_FALSE(test, mode.refresh_specified);
	KUNIT_EXPECT_FALSE(test, mode.bpp_specified);

	KUNIT_EXPECT_FALSE(test, mode.rb);
	KUNIT_EXPECT_FALSE(test, mode.cvt);
	KUNIT_EXPECT_FALSE(test, mode.interlace);
	KUNIT_EXPECT_FALSE(test, mode.margins);
	KUNIT_EXPECT_EQ(test, mode.force, DRM_FORCE_ON_DIGITAL);
}

static void drm_cmdline_test_force_d_only(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "d";

	KUNIT_EXPECT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &no_connector, &mode));
	KUNIT_EXPECT_FALSE(test, mode.specified);
	KUNIT_EXPECT_FALSE(test, mode.refresh_specified);
	KUNIT_EXPECT_FALSE(test, mode.bpp_specified);

	KUNIT_EXPECT_FALSE(test, mode.rb);
	KUNIT_EXPECT_FALSE(test, mode.cvt);
	KUNIT_EXPECT_FALSE(test, mode.interlace);
	KUNIT_EXPECT_FALSE(test, mode.margins);
	KUNIT_EXPECT_EQ(test, mode.force, DRM_FORCE_OFF);
}

static void drm_cmdline_test_margin_only(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "m";

	KUNIT_EXPECT_FALSE(test, drm_mode_parse_command_line_for_connector(cmdline,
									   &no_connector, &mode));
}

static void drm_cmdline_test_interlace_only(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "i";

	KUNIT_EXPECT_FALSE(test, drm_mode_parse_command_line_for_connector(cmdline,
									   &no_connector, &mode));
}

static void drm_cmdline_test_res(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480";

	KUNIT_EXPECT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &no_connector, &mode));
	KUNIT_EXPECT_TRUE(test, mode.specified);
	KUNIT_EXPECT_EQ(test, mode.xres, 720);
	KUNIT_EXPECT_EQ(test, mode.yres, 480);

	KUNIT_EXPECT_FALSE(test, mode.refresh_specified);

	KUNIT_EXPECT_FALSE(test, mode.bpp_specified);

	KUNIT_EXPECT_FALSE(test, mode.rb);
	KUNIT_EXPECT_FALSE(test, mode.cvt);
	KUNIT_EXPECT_FALSE(test, mode.interlace);
	KUNIT_EXPECT_FALSE(test, mode.margins);
	KUNIT_EXPECT_EQ(test, mode.force, DRM_FORCE_UNSPECIFIED);
}

static void drm_cmdline_test_res_missing_x(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "x480";

	KUNIT_EXPECT_FALSE(test, drm_mode_parse_command_line_for_connector(cmdline,
									   &no_connector, &mode));
}

static void drm_cmdline_test_res_missing_y(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "1024x";

	KUNIT_EXPECT_FALSE(test, drm_mode_parse_command_line_for_connector(cmdline,
									   &no_connector, &mode));
}

static void drm_cmdline_test_res_bad_y(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "1024xtest";

	KUNIT_EXPECT_FALSE(test, drm_mode_parse_command_line_for_connector(cmdline,
									   &no_connector, &mode));
}

static void drm_cmdline_test_res_missing_y_bpp(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "1024x-24";

	KUNIT_EXPECT_FALSE(test, drm_mode_parse_command_line_for_connector(cmdline,
									   &no_connector, &mode));
}

static void drm_cmdline_test_res_vesa(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480M";

	KUNIT_EXPECT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &no_connector, &mode));
	KUNIT_EXPECT_TRUE(test, mode.specified);
	KUNIT_EXPECT_EQ(test, mode.xres, 720);
	KUNIT_EXPECT_EQ(test, mode.yres, 480);

	KUNIT_EXPECT_FALSE(test, mode.refresh_specified);

	KUNIT_EXPECT_FALSE(test, mode.bpp_specified);

	KUNIT_EXPECT_FALSE(test, mode.rb);
	KUNIT_EXPECT_TRUE(test, mode.cvt);
	KUNIT_EXPECT_FALSE(test, mode.interlace);
	KUNIT_EXPECT_FALSE(test, mode.margins);
	KUNIT_EXPECT_EQ(test, mode.force, DRM_FORCE_UNSPECIFIED);
}

static void drm_cmdline_test_res_vesa_rblank(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480MR";

	KUNIT_EXPECT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &no_connector, &mode));
	KUNIT_EXPECT_TRUE(test, mode.specified);
	KUNIT_EXPECT_EQ(test, mode.xres, 720);
	KUNIT_EXPECT_EQ(test, mode.yres, 480);

	KUNIT_EXPECT_FALSE(test, mode.refresh_specified);

	KUNIT_EXPECT_FALSE(test, mode.bpp_specified);

	KUNIT_EXPECT_TRUE(test, mode.rb);
	KUNIT_EXPECT_TRUE(test, mode.cvt);
	KUNIT_EXPECT_FALSE(test, mode.interlace);
	KUNIT_EXPECT_FALSE(test, mode.margins);
	KUNIT_EXPECT_EQ(test, mode.force, DRM_FORCE_UNSPECIFIED);
}

static void drm_cmdline_test_res_rblank(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480R";

	KUNIT_EXPECT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &no_connector, &mode));
	KUNIT_EXPECT_TRUE(test, mode.specified);
	KUNIT_EXPECT_EQ(test, mode.xres, 720);
	KUNIT_EXPECT_EQ(test, mode.yres, 480);

	KUNIT_EXPECT_FALSE(test, mode.refresh_specified);

	KUNIT_EXPECT_FALSE(test, mode.bpp_specified);

	KUNIT_EXPECT_TRUE(test, mode.rb);
	KUNIT_EXPECT_FALSE(test, mode.cvt);
	KUNIT_EXPECT_FALSE(test, mode.interlace);
	KUNIT_EXPECT_FALSE(test, mode.margins);
	KUNIT_EXPECT_EQ(test, mode.force, DRM_FORCE_UNSPECIFIED);
}

static void drm_cmdline_test_res_bpp(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480-24";

	KUNIT_EXPECT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &no_connector, &mode));
	KUNIT_EXPECT_TRUE(test, mode.specified);
	KUNIT_EXPECT_EQ(test, mode.xres, 720);
	KUNIT_EXPECT_EQ(test, mode.yres, 480);

	KUNIT_EXPECT_FALSE(test, mode.refresh_specified);

	KUNIT_EXPECT_TRUE(test, mode.bpp_specified);
	KUNIT_EXPECT_EQ(test, mode.bpp, 24);

	KUNIT_EXPECT_FALSE(test, mode.rb);
	KUNIT_EXPECT_FALSE(test, mode.cvt);
	KUNIT_EXPECT_FALSE(test, mode.interlace);
	KUNIT_EXPECT_FALSE(test, mode.margins);
	KUNIT_EXPECT_EQ(test, mode.force, DRM_FORCE_UNSPECIFIED);
}

static void drm_cmdline_test_res_bad_bpp(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480-test";

	KUNIT_EXPECT_FALSE(test, drm_mode_parse_command_line_for_connector(cmdline,
									   &no_connector, &mode));
}

static void drm_cmdline_test_res_refresh(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480@60";

	KUNIT_EXPECT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &no_connector, &mode));
	KUNIT_EXPECT_TRUE(test, mode.specified);
	KUNIT_EXPECT_EQ(test, mode.xres, 720);
	KUNIT_EXPECT_EQ(test, mode.yres, 480);

	KUNIT_EXPECT_TRUE(test, mode.refresh_specified);
	KUNIT_EXPECT_EQ(test, mode.refresh, 60);

	KUNIT_EXPECT_FALSE(test, mode.bpp_specified);

	KUNIT_EXPECT_FALSE(test, mode.rb);
	KUNIT_EXPECT_FALSE(test, mode.cvt);
	KUNIT_EXPECT_FALSE(test, mode.interlace);
	KUNIT_EXPECT_FALSE(test, mode.margins);
	KUNIT_EXPECT_EQ(test, mode.force, DRM_FORCE_UNSPECIFIED);
}

static void drm_cmdline_test_res_bad_refresh(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480@refresh";

	KUNIT_EXPECT_FALSE(test, drm_mode_parse_command_line_for_connector(cmdline,
									   &no_connector, &mode));
}

static void drm_cmdline_test_res_bpp_refresh(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480-24@60";

	KUNIT_EXPECT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &no_connector, &mode));
	KUNIT_EXPECT_TRUE(test, mode.specified);
	KUNIT_EXPECT_EQ(test, mode.xres, 720);
	KUNIT_EXPECT_EQ(test, mode.yres, 480);

	KUNIT_EXPECT_TRUE(test, mode.refresh_specified);
	KUNIT_EXPECT_EQ(test, mode.refresh, 60);

	KUNIT_EXPECT_TRUE(test, mode.bpp_specified);
	KUNIT_EXPECT_EQ(test, mode.bpp, 24);

	KUNIT_EXPECT_FALSE(test, mode.rb);
	KUNIT_EXPECT_FALSE(test, mode.cvt);
	KUNIT_EXPECT_FALSE(test, mode.interlace);
	KUNIT_EXPECT_FALSE(test, mode.margins);
	KUNIT_EXPECT_EQ(test, mode.force, DRM_FORCE_UNSPECIFIED);
}

static void drm_cmdline_test_res_bpp_refresh_interlaced(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480-24@60i";

	KUNIT_EXPECT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &no_connector, &mode));
	KUNIT_EXPECT_TRUE(test, mode.specified);
	KUNIT_EXPECT_EQ(test, mode.xres, 720);
	KUNIT_EXPECT_EQ(test, mode.yres, 480);

	KUNIT_EXPECT_TRUE(test, mode.refresh_specified);
	KUNIT_EXPECT_EQ(test, mode.refresh, 60);

	KUNIT_EXPECT_TRUE(test, mode.bpp_specified);
	KUNIT_EXPECT_EQ(test, mode.bpp, 24);

	KUNIT_EXPECT_FALSE(test, mode.rb);
	KUNIT_EXPECT_FALSE(test, mode.cvt);
	KUNIT_EXPECT_TRUE(test, mode.interlace);
	KUNIT_EXPECT_FALSE(test, mode.margins);
	KUNIT_EXPECT_EQ(test, mode.force, DRM_FORCE_UNSPECIFIED);
}

static void drm_cmdline_test_res_bpp_refresh_margins(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline =  "720x480-24@60m";

	KUNIT_EXPECT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &no_connector, &mode));
	KUNIT_EXPECT_TRUE(test, mode.specified);
	KUNIT_EXPECT_EQ(test, mode.xres, 720);
	KUNIT_EXPECT_EQ(test, mode.yres, 480);

	KUNIT_EXPECT_TRUE(test, mode.refresh_specified);
	KUNIT_EXPECT_EQ(test, mode.refresh, 60);

	KUNIT_EXPECT_TRUE(test, mode.bpp_specified);
	KUNIT_EXPECT_EQ(test, mode.bpp, 24);

	KUNIT_EXPECT_FALSE(test, mode.rb);
	KUNIT_EXPECT_FALSE(test, mode.cvt);
	KUNIT_EXPECT_FALSE(test, mode.interlace);
	KUNIT_EXPECT_TRUE(test, mode.margins);
	KUNIT_EXPECT_EQ(test, mode.force, DRM_FORCE_UNSPECIFIED);
}

static void drm_cmdline_test_res_bpp_refresh_force_off(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline =  "720x480-24@60d";

	KUNIT_EXPECT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &no_connector, &mode));
	KUNIT_EXPECT_TRUE(test, mode.specified);
	KUNIT_EXPECT_EQ(test, mode.xres, 720);
	KUNIT_EXPECT_EQ(test, mode.yres, 480);

	KUNIT_EXPECT_TRUE(test, mode.refresh_specified);
	KUNIT_EXPECT_EQ(test, mode.refresh, 60);

	KUNIT_EXPECT_TRUE(test, mode.bpp_specified);
	KUNIT_EXPECT_EQ(test, mode.bpp, 24);

	KUNIT_EXPECT_FALSE(test, mode.rb);
	KUNIT_EXPECT_FALSE(test, mode.cvt);
	KUNIT_EXPECT_FALSE(test, mode.interlace);
	KUNIT_EXPECT_FALSE(test, mode.margins);
	KUNIT_EXPECT_EQ(test, mode.force, DRM_FORCE_OFF);
}

static void drm_cmdline_test_res_bpp_refresh_force_on_off(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline =  "720x480-24@60de";

	KUNIT_EXPECT_FALSE(test, drm_mode_parse_command_line_for_connector(cmdline,
									   &no_connector, &mode));
}

static void drm_cmdline_test_res_bpp_refresh_force_on(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline =  "720x480-24@60e";

	KUNIT_EXPECT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &no_connector, &mode));
	KUNIT_EXPECT_TRUE(test, mode.specified);
	KUNIT_EXPECT_EQ(test, mode.xres, 720);
	KUNIT_EXPECT_EQ(test, mode.yres, 480);

	KUNIT_EXPECT_TRUE(test, mode.refresh_specified);
	KUNIT_EXPECT_EQ(test, mode.refresh, 60);

	KUNIT_EXPECT_TRUE(test, mode.bpp_specified);
	KUNIT_EXPECT_EQ(test, mode.bpp, 24);

	KUNIT_EXPECT_FALSE(test, mode.rb);
	KUNIT_EXPECT_FALSE(test, mode.cvt);
	KUNIT_EXPECT_FALSE(test, mode.interlace);
	KUNIT_EXPECT_FALSE(test, mode.margins);
	KUNIT_EXPECT_EQ(test, mode.force, DRM_FORCE_ON);
}

static void drm_cmdline_test_res_bpp_refresh_force_on_analog(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480-24@60D";

	KUNIT_EXPECT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &no_connector, &mode));
	KUNIT_EXPECT_TRUE(test, mode.specified);
	KUNIT_EXPECT_EQ(test, mode.xres, 720);
	KUNIT_EXPECT_EQ(test, mode.yres, 480);

	KUNIT_EXPECT_TRUE(test, mode.refresh_specified);
	KUNIT_EXPECT_EQ(test, mode.refresh, 60);

	KUNIT_EXPECT_TRUE(test, mode.bpp_specified);
	KUNIT_EXPECT_EQ(test, mode.bpp, 24);

	KUNIT_EXPECT_FALSE(test, mode.rb);
	KUNIT_EXPECT_FALSE(test, mode.cvt);
	KUNIT_EXPECT_FALSE(test, mode.interlace);
	KUNIT_EXPECT_FALSE(test, mode.margins);
	KUNIT_EXPECT_EQ(test, mode.force, DRM_FORCE_ON);
}

static void drm_cmdline_test_res_bpp_refresh_force_on_digital(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	static const struct drm_connector connector = {
		.connector_type = DRM_MODE_CONNECTOR_DVII,
	};
	const char *cmdline = "720x480-24@60D";

	KUNIT_EXPECT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &connector, &mode));
	KUNIT_EXPECT_TRUE(test, mode.specified);
	KUNIT_EXPECT_EQ(test, mode.xres, 720);
	KUNIT_EXPECT_EQ(test, mode.yres, 480);

	KUNIT_EXPECT_TRUE(test, mode.refresh_specified);
	KUNIT_EXPECT_EQ(test, mode.refresh, 60);

	KUNIT_EXPECT_TRUE(test, mode.bpp_specified);
	KUNIT_EXPECT_EQ(test, mode.bpp, 24);

	KUNIT_EXPECT_FALSE(test, mode.rb);
	KUNIT_EXPECT_FALSE(test, mode.cvt);
	KUNIT_EXPECT_FALSE(test, mode.interlace);
	KUNIT_EXPECT_FALSE(test, mode.margins);
	KUNIT_EXPECT_EQ(test, mode.force, DRM_FORCE_ON_DIGITAL);
}

static void drm_cmdline_test_res_bpp_refresh_interlaced_margins_force_on(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480-24@60ime";

	KUNIT_EXPECT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &no_connector, &mode));
	KUNIT_EXPECT_TRUE(test, mode.specified);
	KUNIT_EXPECT_EQ(test, mode.xres, 720);
	KUNIT_EXPECT_EQ(test, mode.yres, 480);

	KUNIT_EXPECT_TRUE(test, mode.refresh_specified);
	KUNIT_EXPECT_EQ(test, mode.refresh, 60);

	KUNIT_EXPECT_TRUE(test, mode.bpp_specified);
	KUNIT_EXPECT_EQ(test, mode.bpp, 24);

	KUNIT_EXPECT_FALSE(test, mode.rb);
	KUNIT_EXPECT_FALSE(test, mode.cvt);
	KUNIT_EXPECT_TRUE(test, mode.interlace);
	KUNIT_EXPECT_TRUE(test, mode.margins);
	KUNIT_EXPECT_EQ(test, mode.force, DRM_FORCE_ON);
}

static void drm_cmdline_test_res_margins_force_on(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480me";

	KUNIT_EXPECT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &no_connector, &mode));
	KUNIT_EXPECT_TRUE(test, mode.specified);
	KUNIT_EXPECT_EQ(test, mode.xres, 720);
	KUNIT_EXPECT_EQ(test, mode.yres, 480);

	KUNIT_EXPECT_FALSE(test, mode.refresh_specified);

	KUNIT_EXPECT_FALSE(test, mode.bpp_specified);

	KUNIT_EXPECT_FALSE(test, mode.rb);
	KUNIT_EXPECT_FALSE(test, mode.cvt);
	KUNIT_EXPECT_FALSE(test, mode.interlace);
	KUNIT_EXPECT_TRUE(test, mode.margins);
	KUNIT_EXPECT_EQ(test, mode.force, DRM_FORCE_ON);
}

static void drm_cmdline_test_res_vesa_margins(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480Mm";

	KUNIT_EXPECT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &no_connector, &mode));
	KUNIT_EXPECT_TRUE(test, mode.specified);
	KUNIT_EXPECT_EQ(test, mode.xres, 720);
	KUNIT_EXPECT_EQ(test, mode.yres, 480);

	KUNIT_EXPECT_FALSE(test, mode.refresh_specified);

	KUNIT_EXPECT_FALSE(test, mode.bpp_specified);

	KUNIT_EXPECT_FALSE(test, mode.rb);
	KUNIT_EXPECT_TRUE(test, mode.cvt);
	KUNIT_EXPECT_FALSE(test, mode.interlace);
	KUNIT_EXPECT_TRUE(test, mode.margins);
	KUNIT_EXPECT_EQ(test, mode.force, DRM_FORCE_UNSPECIFIED);
}

static void drm_cmdline_test_res_invalid_mode(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480f";

	KUNIT_EXPECT_FALSE(test, drm_mode_parse_command_line_for_connector(cmdline,
									   &no_connector, &mode));
}

static void drm_cmdline_test_res_bpp_wrong_place_mode(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480e-24";

	KUNIT_EXPECT_FALSE(test, drm_mode_parse_command_line_for_connector(cmdline,
									   &no_connector, &mode));
}

static void drm_cmdline_test_name(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "NTSC";

	KUNIT_EXPECT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &no_connector, &mode));
	KUNIT_EXPECT_STREQ(test, mode.name, "NTSC");
	KUNIT_EXPECT_FALSE(test, mode.refresh_specified);
	KUNIT_EXPECT_FALSE(test, mode.bpp_specified);
}

static void drm_cmdline_test_name_bpp(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "NTSC-24";

	KUNIT_EXPECT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &no_connector, &mode));
	KUNIT_EXPECT_STREQ(test, mode.name, "NTSC");

	KUNIT_EXPECT_FALSE(test, mode.refresh_specified);

	KUNIT_EXPECT_TRUE(test, mode.bpp_specified);
	KUNIT_EXPECT_EQ(test, mode.bpp, 24);
}

static void drm_cmdline_test_name_bpp_refresh(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "NTSC-24@60";

	KUNIT_EXPECT_FALSE(test, drm_mode_parse_command_line_for_connector(cmdline,
									   &no_connector, &mode));
}

static void drm_cmdline_test_name_refresh(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "NTSC@60";

	KUNIT_EXPECT_FALSE(test, drm_mode_parse_command_line_for_connector(cmdline,
									   &no_connector, &mode));
}

static void drm_cmdline_test_name_refresh_wrong_mode(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "NTSC@60m";

	KUNIT_EXPECT_FALSE(test, drm_mode_parse_command_line_for_connector(cmdline,
									   &no_connector, &mode));
}

static void drm_cmdline_test_name_refresh_invalid_mode(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "NTSC@60f";

	KUNIT_EXPECT_FALSE(test, drm_mode_parse_command_line_for_connector(cmdline,
									   &no_connector, &mode));
}

static void drm_cmdline_test_name_option(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "NTSC,rotate=180";

	KUNIT_EXPECT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &no_connector, &mode));
	KUNIT_EXPECT_TRUE(test, mode.specified);
	KUNIT_EXPECT_STREQ(test, mode.name, "NTSC");
	KUNIT_EXPECT_EQ(test, mode.rotation_reflection, DRM_MODE_ROTATE_180);
}

static void drm_cmdline_test_name_bpp_option(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "NTSC-24,rotate=180";

	KUNIT_EXPECT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &no_connector, &mode));
	KUNIT_EXPECT_TRUE(test, mode.specified);
	KUNIT_EXPECT_STREQ(test, mode.name, "NTSC");
	KUNIT_EXPECT_EQ(test, mode.rotation_reflection, DRM_MODE_ROTATE_180);
	KUNIT_EXPECT_TRUE(test, mode.bpp_specified);
	KUNIT_EXPECT_EQ(test, mode.bpp, 24);
}

static void drm_cmdline_test_rotate_0(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480,rotate=0";

	KUNIT_EXPECT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &no_connector, &mode));
	KUNIT_EXPECT_TRUE(test, mode.specified);
	KUNIT_EXPECT_EQ(test, mode.xres, 720);
	KUNIT_EXPECT_EQ(test, mode.yres, 480);
	KUNIT_EXPECT_EQ(test, mode.rotation_reflection, DRM_MODE_ROTATE_0);

	KUNIT_EXPECT_FALSE(test, mode.refresh_specified);

	KUNIT_EXPECT_FALSE(test, mode.bpp_specified);

	KUNIT_EXPECT_FALSE(test, mode.rb);
	KUNIT_EXPECT_FALSE(test, mode.cvt);
	KUNIT_EXPECT_FALSE(test, mode.interlace);
	KUNIT_EXPECT_FALSE(test, mode.margins);
	KUNIT_EXPECT_EQ(test, mode.force, DRM_FORCE_UNSPECIFIED);
}

static void drm_cmdline_test_rotate_90(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480,rotate=90";

	KUNIT_EXPECT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &no_connector, &mode));
	KUNIT_EXPECT_TRUE(test, mode.specified);
	KUNIT_EXPECT_EQ(test, mode.xres, 720);
	KUNIT_EXPECT_EQ(test, mode.yres, 480);
	KUNIT_EXPECT_EQ(test, mode.rotation_reflection, DRM_MODE_ROTATE_90);

	KUNIT_EXPECT_FALSE(test, mode.refresh_specified);

	KUNIT_EXPECT_FALSE(test, mode.bpp_specified);

	KUNIT_EXPECT_FALSE(test, mode.rb);
	KUNIT_EXPECT_FALSE(test, mode.cvt);
	KUNIT_EXPECT_FALSE(test, mode.interlace);
	KUNIT_EXPECT_FALSE(test, mode.margins);
	KUNIT_EXPECT_EQ(test, mode.force, DRM_FORCE_UNSPECIFIED);
}

static void drm_cmdline_test_rotate_180(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480,rotate=180";

	KUNIT_EXPECT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &no_connector, &mode));
	KUNIT_EXPECT_TRUE(test, mode.specified);
	KUNIT_EXPECT_EQ(test, mode.xres, 720);
	KUNIT_EXPECT_EQ(test, mode.yres, 480);
	KUNIT_EXPECT_EQ(test, mode.rotation_reflection, DRM_MODE_ROTATE_180);

	KUNIT_EXPECT_FALSE(test, mode.refresh_specified);

	KUNIT_EXPECT_FALSE(test, mode.bpp_specified);

	KUNIT_EXPECT_FALSE(test, mode.rb);
	KUNIT_EXPECT_FALSE(test, mode.cvt);
	KUNIT_EXPECT_FALSE(test, mode.interlace);
	KUNIT_EXPECT_FALSE(test, mode.margins);
	KUNIT_EXPECT_EQ(test, mode.force, DRM_FORCE_UNSPECIFIED);
}

static void drm_cmdline_test_rotate_270(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480,rotate=270";

	KUNIT_EXPECT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &no_connector, &mode));
	KUNIT_EXPECT_TRUE(test, mode.specified);
	KUNIT_EXPECT_EQ(test, mode.xres, 720);
	KUNIT_EXPECT_EQ(test, mode.yres, 480);
	KUNIT_EXPECT_EQ(test, mode.rotation_reflection, DRM_MODE_ROTATE_270);

	KUNIT_EXPECT_FALSE(test, mode.refresh_specified);

	KUNIT_EXPECT_FALSE(test, mode.bpp_specified);

	KUNIT_EXPECT_FALSE(test, mode.rb);
	KUNIT_EXPECT_FALSE(test, mode.cvt);
	KUNIT_EXPECT_FALSE(test, mode.interlace);
	KUNIT_EXPECT_FALSE(test, mode.margins);
	KUNIT_EXPECT_EQ(test, mode.force, DRM_FORCE_UNSPECIFIED);
}

static void drm_cmdline_test_rotate_multiple(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480,rotate=0,rotate=90";

	KUNIT_EXPECT_FALSE(test, drm_mode_parse_command_line_for_connector(cmdline,
									   &no_connector, &mode));
}

static void drm_cmdline_test_rotate_invalid_val(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480,rotate=42";

	KUNIT_EXPECT_FALSE(test, drm_mode_parse_command_line_for_connector(cmdline,
									   &no_connector, &mode));
}

static void drm_cmdline_test_rotate_truncated(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480,rotate=";

	KUNIT_EXPECT_FALSE(test, drm_mode_parse_command_line_for_connector(cmdline,
									   &no_connector, &mode));
}

static void drm_cmdline_test_hmirror(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480,reflect_x";

	KUNIT_EXPECT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &no_connector, &mode));
	KUNIT_EXPECT_TRUE(test, mode.specified);
	KUNIT_EXPECT_EQ(test, mode.xres, 720);
	KUNIT_EXPECT_EQ(test, mode.yres, 480);
	KUNIT_EXPECT_EQ(test, mode.rotation_reflection, (DRM_MODE_ROTATE_0 | DRM_MODE_REFLECT_X));

	KUNIT_EXPECT_FALSE(test, mode.refresh_specified);

	KUNIT_EXPECT_FALSE(test, mode.bpp_specified);

	KUNIT_EXPECT_FALSE(test, mode.rb);
	KUNIT_EXPECT_FALSE(test, mode.cvt);
	KUNIT_EXPECT_FALSE(test, mode.interlace);
	KUNIT_EXPECT_FALSE(test, mode.margins);
	KUNIT_EXPECT_EQ(test, mode.force, DRM_FORCE_UNSPECIFIED);
}

static void drm_cmdline_test_vmirror(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480,reflect_y";

	KUNIT_EXPECT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &no_connector, &mode));
	KUNIT_EXPECT_TRUE(test, mode.specified);
	KUNIT_EXPECT_EQ(test, mode.xres, 720);
	KUNIT_EXPECT_EQ(test, mode.yres, 480);
	KUNIT_EXPECT_EQ(test, mode.rotation_reflection, (DRM_MODE_ROTATE_0 | DRM_MODE_REFLECT_Y));

	KUNIT_EXPECT_FALSE(test, mode.refresh_specified);

	KUNIT_EXPECT_FALSE(test, mode.bpp_specified);

	KUNIT_EXPECT_FALSE(test, mode.rb);
	KUNIT_EXPECT_FALSE(test, mode.cvt);
	KUNIT_EXPECT_FALSE(test, mode.interlace);
	KUNIT_EXPECT_FALSE(test, mode.margins);
	KUNIT_EXPECT_EQ(test, mode.force, DRM_FORCE_UNSPECIFIED);
}

static void drm_cmdline_test_margin_options(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline =
		"720x480,margin_right=14,margin_left=24,margin_bottom=36,margin_top=42";

	KUNIT_EXPECT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &no_connector, &mode));
	KUNIT_EXPECT_TRUE(test, mode.specified);
	KUNIT_EXPECT_EQ(test, mode.xres, 720);
	KUNIT_EXPECT_EQ(test, mode.yres, 480);
	KUNIT_EXPECT_EQ(test, mode.tv_margins.right, 14);
	KUNIT_EXPECT_EQ(test, mode.tv_margins.left, 24);
	KUNIT_EXPECT_EQ(test, mode.tv_margins.bottom, 36);
	KUNIT_EXPECT_EQ(test, mode.tv_margins.top, 42);

	KUNIT_EXPECT_FALSE(test, mode.refresh_specified);

	KUNIT_EXPECT_FALSE(test, mode.bpp_specified);

	KUNIT_EXPECT_FALSE(test, mode.rb);
	KUNIT_EXPECT_FALSE(test, mode.cvt);
	KUNIT_EXPECT_FALSE(test, mode.interlace);
	KUNIT_EXPECT_FALSE(test, mode.margins);
	KUNIT_EXPECT_EQ(test, mode.force, DRM_FORCE_UNSPECIFIED);
}

static void drm_cmdline_test_multiple_options(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480,rotate=270,reflect_x";

	KUNIT_EXPECT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &no_connector, &mode));
	KUNIT_EXPECT_TRUE(test, mode.specified);
	KUNIT_EXPECT_EQ(test, mode.xres, 720);
	KUNIT_EXPECT_EQ(test, mode.yres, 480);
	KUNIT_EXPECT_EQ(test, mode.rotation_reflection, (DRM_MODE_ROTATE_270 | DRM_MODE_REFLECT_X));

	KUNIT_EXPECT_FALSE(test, mode.refresh_specified);

	KUNIT_EXPECT_FALSE(test, mode.bpp_specified);

	KUNIT_EXPECT_FALSE(test, mode.rb);
	KUNIT_EXPECT_FALSE(test, mode.cvt);
	KUNIT_EXPECT_FALSE(test, mode.interlace);
	KUNIT_EXPECT_FALSE(test, mode.margins);
	KUNIT_EXPECT_EQ(test, mode.force, DRM_FORCE_UNSPECIFIED);
}

static void drm_cmdline_test_invalid_option(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480,test=42";

	KUNIT_EXPECT_FALSE(test, drm_mode_parse_command_line_for_connector(cmdline,
									   &no_connector, &mode));
}

static void drm_cmdline_test_bpp_extra_and_option(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480-24e,rotate=180";

	KUNIT_EXPECT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &no_connector, &mode));
	KUNIT_EXPECT_TRUE(test, mode.specified);
	KUNIT_EXPECT_EQ(test, mode.xres, 720);
	KUNIT_EXPECT_EQ(test, mode.yres, 480);
	KUNIT_EXPECT_EQ(test, mode.rotation_reflection, DRM_MODE_ROTATE_180);

	KUNIT_EXPECT_FALSE(test, mode.refresh_specified);

	KUNIT_EXPECT_TRUE(test, mode.bpp_specified);
	KUNIT_EXPECT_EQ(test, mode.bpp, 24);

	KUNIT_EXPECT_FALSE(test, mode.rb);
	KUNIT_EXPECT_FALSE(test, mode.cvt);
	KUNIT_EXPECT_FALSE(test, mode.interlace);
	KUNIT_EXPECT_FALSE(test, mode.margins);
	KUNIT_EXPECT_EQ(test, mode.force, DRM_FORCE_ON);
}

static void drm_cmdline_test_extra_and_option(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480e,rotate=180";

	KUNIT_EXPECT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &no_connector, &mode));
	KUNIT_EXPECT_TRUE(test, mode.specified);
	KUNIT_EXPECT_EQ(test, mode.xres, 720);
	KUNIT_EXPECT_EQ(test, mode.yres, 480);
	KUNIT_EXPECT_EQ(test, mode.rotation_reflection, DRM_MODE_ROTATE_180);

	KUNIT_EXPECT_FALSE(test, mode.refresh_specified);
	KUNIT_EXPECT_FALSE(test, mode.bpp_specified);

	KUNIT_EXPECT_FALSE(test, mode.rb);
	KUNIT_EXPECT_FALSE(test, mode.cvt);
	KUNIT_EXPECT_FALSE(test, mode.interlace);
	KUNIT_EXPECT_FALSE(test, mode.margins);
	KUNIT_EXPECT_EQ(test, mode.force, DRM_FORCE_ON);
}

static void drm_cmdline_test_freestanding_options(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "margin_right=14,margin_left=24,margin_bottom=36,margin_top=42";

	KUNIT_EXPECT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &no_connector, &mode));
	KUNIT_EXPECT_FALSE(test, mode.specified);
	KUNIT_EXPECT_FALSE(test, mode.refresh_specified);
	KUNIT_EXPECT_FALSE(test, mode.bpp_specified);

	KUNIT_EXPECT_EQ(test, mode.tv_margins.right, 14);
	KUNIT_EXPECT_EQ(test, mode.tv_margins.left, 24);
	KUNIT_EXPECT_EQ(test, mode.tv_margins.bottom, 36);
	KUNIT_EXPECT_EQ(test, mode.tv_margins.top, 42);

	KUNIT_EXPECT_FALSE(test, mode.rb);
	KUNIT_EXPECT_FALSE(test, mode.cvt);
	KUNIT_EXPECT_FALSE(test, mode.interlace);
	KUNIT_EXPECT_FALSE(test, mode.margins);
	KUNIT_EXPECT_EQ(test, mode.force, DRM_FORCE_UNSPECIFIED);
}

static void drm_cmdline_test_freestanding_force_e_and_options(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "e,margin_right=14,margin_left=24,margin_bottom=36,margin_top=42";

	KUNIT_EXPECT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &no_connector, &mode));
	KUNIT_EXPECT_FALSE(test, mode.specified);
	KUNIT_EXPECT_FALSE(test, mode.refresh_specified);
	KUNIT_EXPECT_FALSE(test, mode.bpp_specified);

	KUNIT_EXPECT_EQ(test, mode.tv_margins.right, 14);
	KUNIT_EXPECT_EQ(test, mode.tv_margins.left, 24);
	KUNIT_EXPECT_EQ(test, mode.tv_margins.bottom, 36);
	KUNIT_EXPECT_EQ(test, mode.tv_margins.top, 42);

	KUNIT_EXPECT_FALSE(test, mode.rb);
	KUNIT_EXPECT_FALSE(test, mode.cvt);
	KUNIT_EXPECT_FALSE(test, mode.interlace);
	KUNIT_EXPECT_FALSE(test, mode.margins);
	KUNIT_EXPECT_EQ(test, mode.force, DRM_FORCE_ON);
}

static void drm_cmdline_test_panel_orientation(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "panel_orientation=upside_down";

	KUNIT_EXPECT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &no_connector, &mode));
	KUNIT_EXPECT_FALSE(test, mode.specified);
	KUNIT_EXPECT_FALSE(test, mode.refresh_specified);
	KUNIT_EXPECT_FALSE(test, mode.bpp_specified);

	KUNIT_EXPECT_EQ(test, mode.panel_orientation, DRM_MODE_PANEL_ORIENTATION_BOTTOM_UP);

	KUNIT_EXPECT_FALSE(test, mode.rb);
	KUNIT_EXPECT_FALSE(test, mode.cvt);
	KUNIT_EXPECT_FALSE(test, mode.interlace);
	KUNIT_EXPECT_FALSE(test, mode.margins);
	KUNIT_EXPECT_EQ(test, mode.force, DRM_FORCE_UNSPECIFIED);
}

static struct kunit_case drm_cmdline_parser_tests[] = {
	KUNIT_CASE(drm_cmdline_test_force_d_only),
	KUNIT_CASE(drm_cmdline_test_force_D_only_dvi),
	KUNIT_CASE(drm_cmdline_test_force_D_only_hdmi),
	KUNIT_CASE(drm_cmdline_test_force_D_only_not_digital),
	KUNIT_CASE(drm_cmdline_test_force_e_only),
	KUNIT_CASE(drm_cmdline_test_margin_only),
	KUNIT_CASE(drm_cmdline_test_interlace_only),
	KUNIT_CASE(drm_cmdline_test_res),
	KUNIT_CASE(drm_cmdline_test_res_missing_x),
	KUNIT_CASE(drm_cmdline_test_res_missing_y),
	KUNIT_CASE(drm_cmdline_test_res_bad_y),
	KUNIT_CASE(drm_cmdline_test_res_missing_y_bpp),
	KUNIT_CASE(drm_cmdline_test_res_vesa),
	KUNIT_CASE(drm_cmdline_test_res_vesa_rblank),
	KUNIT_CASE(drm_cmdline_test_res_rblank),
	KUNIT_CASE(drm_cmdline_test_res_bpp),
	KUNIT_CASE(drm_cmdline_test_res_bad_bpp),
	KUNIT_CASE(drm_cmdline_test_res_refresh),
	KUNIT_CASE(drm_cmdline_test_res_bad_refresh),
	KUNIT_CASE(drm_cmdline_test_res_bpp_refresh),
	KUNIT_CASE(drm_cmdline_test_res_bpp_refresh_interlaced),
	KUNIT_CASE(drm_cmdline_test_res_bpp_refresh_margins),
	KUNIT_CASE(drm_cmdline_test_res_bpp_refresh_force_off),
	KUNIT_CASE(drm_cmdline_test_res_bpp_refresh_force_on_off),
	KUNIT_CASE(drm_cmdline_test_res_bpp_refresh_force_on),
	KUNIT_CASE(drm_cmdline_test_res_bpp_refresh_force_on_analog),
	KUNIT_CASE(drm_cmdline_test_res_bpp_refresh_force_on_digital),
	KUNIT_CASE(drm_cmdline_test_res_bpp_refresh_interlaced_margins_force_on),
	KUNIT_CASE(drm_cmdline_test_res_margins_force_on),
	KUNIT_CASE(drm_cmdline_test_res_vesa_margins),
	KUNIT_CASE(drm_cmdline_test_res_invalid_mode),
	KUNIT_CASE(drm_cmdline_test_res_bpp_wrong_place_mode),
	KUNIT_CASE(drm_cmdline_test_name),
	KUNIT_CASE(drm_cmdline_test_name_bpp),
	KUNIT_CASE(drm_cmdline_test_name_refresh),
	KUNIT_CASE(drm_cmdline_test_name_bpp_refresh),
	KUNIT_CASE(drm_cmdline_test_name_refresh_wrong_mode),
	KUNIT_CASE(drm_cmdline_test_name_refresh_invalid_mode),
	KUNIT_CASE(drm_cmdline_test_name_option),
	KUNIT_CASE(drm_cmdline_test_name_bpp_option),
	KUNIT_CASE(drm_cmdline_test_rotate_0),
	KUNIT_CASE(drm_cmdline_test_rotate_90),
	KUNIT_CASE(drm_cmdline_test_rotate_180),
	KUNIT_CASE(drm_cmdline_test_rotate_270),
	KUNIT_CASE(drm_cmdline_test_rotate_multiple),
	KUNIT_CASE(drm_cmdline_test_rotate_invalid_val),
	KUNIT_CASE(drm_cmdline_test_rotate_truncated),
	KUNIT_CASE(drm_cmdline_test_hmirror),
	KUNIT_CASE(drm_cmdline_test_vmirror),
	KUNIT_CASE(drm_cmdline_test_margin_options),
	KUNIT_CASE(drm_cmdline_test_multiple_options),
	KUNIT_CASE(drm_cmdline_test_invalid_option),
	KUNIT_CASE(drm_cmdline_test_bpp_extra_and_option),
	KUNIT_CASE(drm_cmdline_test_extra_and_option),
	KUNIT_CASE(drm_cmdline_test_freestanding_options),
	KUNIT_CASE(drm_cmdline_test_freestanding_force_e_and_options),
	KUNIT_CASE(drm_cmdline_test_panel_orientation),
	{}
};

static struct kunit_suite drm_cmdline_parser_test_suite = {
	.name = "drm_cmdline_parser",
	.test_cases = drm_cmdline_parser_tests
};

kunit_test_suite(drm_cmdline_parser_test_suite);

MODULE_AUTHOR("Maxime Ripard <maxime.ripard@bootlin.com>");
MODULE_LICENSE("GPL");
