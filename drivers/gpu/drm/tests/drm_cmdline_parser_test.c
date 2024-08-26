// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 Bootlin
 * Copyright (c) 2022 Maíra Canal <mairacanal@riseup.net>
 */

#include <kunit/test.h>

#include <drm/drm_connector.h>
#include <drm/drm_modes.h>

static const struct drm_connector no_connector = {};

static void drm_test_cmdline_force_e_only(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "e";

	KUNIT_ASSERT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
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

static void drm_test_cmdline_force_D_only_not_digital(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "D";

	KUNIT_ASSERT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
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

static void drm_test_cmdline_force_D_only_hdmi(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "D";

	KUNIT_ASSERT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
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

static void drm_test_cmdline_force_D_only_dvi(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "D";

	KUNIT_ASSERT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
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

static void drm_test_cmdline_force_d_only(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "d";

	KUNIT_ASSERT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
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

static void drm_test_cmdline_res(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480";

	KUNIT_ASSERT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
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

static void drm_test_cmdline_res_vesa(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480M";

	KUNIT_ASSERT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
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

static void drm_test_cmdline_res_vesa_rblank(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480MR";

	KUNIT_ASSERT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
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

static void drm_test_cmdline_res_rblank(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480R";

	KUNIT_ASSERT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
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

static void drm_test_cmdline_res_bpp(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480-24";

	KUNIT_ASSERT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
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

static void drm_test_cmdline_res_refresh(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480@60";

	KUNIT_ASSERT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
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

static void drm_test_cmdline_res_bpp_refresh(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480-24@60";

	KUNIT_ASSERT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
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

static void drm_test_cmdline_res_bpp_refresh_interlaced(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480-24@60i";

	KUNIT_ASSERT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
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

static void drm_test_cmdline_res_bpp_refresh_margins(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480-24@60m";

	KUNIT_ASSERT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
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

static void drm_test_cmdline_res_bpp_refresh_force_off(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480-24@60d";

	KUNIT_ASSERT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
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

static void drm_test_cmdline_res_bpp_refresh_force_on(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480-24@60e";

	KUNIT_ASSERT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
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

static void drm_test_cmdline_res_bpp_refresh_force_on_analog(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480-24@60D";

	KUNIT_ASSERT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
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

static void drm_test_cmdline_res_bpp_refresh_force_on_digital(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	static const struct drm_connector connector = {
		.connector_type = DRM_MODE_CONNECTOR_DVII,
	};
	const char *cmdline = "720x480-24@60D";

	KUNIT_ASSERT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
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

static void drm_test_cmdline_res_bpp_refresh_interlaced_margins_force_on(struct kunit *test)
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

static void drm_test_cmdline_res_margins_force_on(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480me";

	KUNIT_ASSERT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
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

static void drm_test_cmdline_res_vesa_margins(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480Mm";

	KUNIT_ASSERT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
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

static void drm_test_cmdline_name(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "NTSC";

	KUNIT_ASSERT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &no_connector, &mode));
	KUNIT_EXPECT_STREQ(test, mode.name, "NTSC");
	KUNIT_EXPECT_FALSE(test, mode.refresh_specified);
	KUNIT_EXPECT_FALSE(test, mode.bpp_specified);
}

static void drm_test_cmdline_name_bpp(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "NTSC-24";

	KUNIT_ASSERT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &no_connector, &mode));
	KUNIT_EXPECT_STREQ(test, mode.name, "NTSC");

	KUNIT_EXPECT_FALSE(test, mode.refresh_specified);

	KUNIT_EXPECT_TRUE(test, mode.bpp_specified);
	KUNIT_EXPECT_EQ(test, mode.bpp, 24);
}

static void drm_test_cmdline_name_option(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "NTSC,rotate=180";

	KUNIT_ASSERT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &no_connector, &mode));
	KUNIT_EXPECT_TRUE(test, mode.specified);
	KUNIT_EXPECT_STREQ(test, mode.name, "NTSC");
	KUNIT_EXPECT_EQ(test, mode.rotation_reflection, DRM_MODE_ROTATE_180);
}

static void drm_test_cmdline_name_bpp_option(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "NTSC-24,rotate=180";

	KUNIT_ASSERT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
									  &no_connector, &mode));
	KUNIT_EXPECT_TRUE(test, mode.specified);
	KUNIT_EXPECT_STREQ(test, mode.name, "NTSC");
	KUNIT_EXPECT_EQ(test, mode.rotation_reflection, DRM_MODE_ROTATE_180);
	KUNIT_EXPECT_TRUE(test, mode.bpp_specified);
	KUNIT_EXPECT_EQ(test, mode.bpp, 24);
}

static void drm_test_cmdline_rotate_0(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480,rotate=0";

	KUNIT_ASSERT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
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

static void drm_test_cmdline_rotate_90(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480,rotate=90";

	KUNIT_ASSERT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
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

static void drm_test_cmdline_rotate_180(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480,rotate=180";

	KUNIT_ASSERT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
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

static void drm_test_cmdline_rotate_270(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480,rotate=270";

	KUNIT_ASSERT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
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

static void drm_test_cmdline_hmirror(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480,reflect_x";

	KUNIT_ASSERT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
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

static void drm_test_cmdline_vmirror(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480,reflect_y";

	KUNIT_ASSERT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
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

static void drm_test_cmdline_margin_options(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline =
		"720x480,margin_right=14,margin_left=24,margin_bottom=36,margin_top=42";

	KUNIT_ASSERT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
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

static void drm_test_cmdline_multiple_options(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480,rotate=270,reflect_x";

	KUNIT_ASSERT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
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

static void drm_test_cmdline_bpp_extra_and_option(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480-24e,rotate=180";

	KUNIT_ASSERT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
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

static void drm_test_cmdline_extra_and_option(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "720x480e,rotate=180";

	KUNIT_ASSERT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
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

static void drm_test_cmdline_freestanding_options(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "margin_right=14,margin_left=24,margin_bottom=36,margin_top=42";

	KUNIT_ASSERT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
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

static void drm_test_cmdline_freestanding_force_e_and_options(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "e,margin_right=14,margin_left=24,margin_bottom=36,margin_top=42";

	KUNIT_ASSERT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
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

static void drm_test_cmdline_panel_orientation(struct kunit *test)
{
	struct drm_cmdline_mode mode = { };
	const char *cmdline = "panel_orientation=upside_down";

	KUNIT_ASSERT_TRUE(test, drm_mode_parse_command_line_for_connector(cmdline,
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

struct drm_cmdline_invalid_test {
	const char *name;
	const char *cmdline;
};

static void drm_test_cmdline_invalid(struct kunit *test)
{
	const struct drm_cmdline_invalid_test *params = test->param_value;
	struct drm_cmdline_mode mode = { };

	KUNIT_EXPECT_FALSE(test, drm_mode_parse_command_line_for_connector(params->cmdline,
									   &no_connector,
									   &mode));
}

static const struct drm_cmdline_invalid_test drm_cmdline_invalid_tests[] = {
	{
		.name = "margin_only",
		.cmdline = "m",
	},
	{
		.name = "interlace_only",
		.cmdline = "i",
	},
	{
		.name = "res_missing_x",
		.cmdline = "x480",
	},
	{
		.name = "res_missing_y",
		.cmdline = "1024x",
	},
	{
		.name = "res_bad_y",
		.cmdline = "1024xtest",
	},
	{
		.name = "res_missing_y_bpp",
		.cmdline = "1024x-24",
	},
	{
		.name = "res_bad_bpp",
		.cmdline = "720x480-test",
	},
	{
		.name = "res_bad_refresh",
		.cmdline = "720x480@refresh",
	},
	{
		.name = "res_bpp_refresh_force_on_off",
		.cmdline = "720x480-24@60de",
	},
	{
		.name = "res_invalid_mode",
		.cmdline = "720x480f",
	},
	{
		.name = "res_bpp_wrong_place_mode",
		.cmdline = "720x480e-24",
	},
	{
		.name = "name_bpp_refresh",
		.cmdline = "NTSC-24@60",
	},
	{
		.name = "name_refresh",
		.cmdline = "NTSC@60",
	},
	{
		.name = "name_refresh_wrong_mode",
		.cmdline = "NTSC@60m",
	},
	{
		.name = "name_refresh_invalid_mode",
		.cmdline = "NTSC@60f",
	},
	{
		.name = "rotate_multiple",
		.cmdline = "720x480,rotate=0,rotate=90",
	},
	{
		.name = "rotate_invalid_val",
		.cmdline = "720x480,rotate=42",
	},
	{
		.name = "rotate_truncated",
		.cmdline = "720x480,rotate=",
	},
	{
		.name = "invalid_option",
		.cmdline = "720x480,test=42",
	},
	{
		.name = "invalid_tv_option",
		.cmdline = "720x480i,tv_mode=invalid",
	},
	{
		.name = "truncated_tv_option",
		.cmdline = "720x480i,tv_mode=NTS",
	},
};

static void drm_cmdline_invalid_desc(const struct drm_cmdline_invalid_test *t,
				     char *desc)
{
	sprintf(desc, "%s", t->name);
}

KUNIT_ARRAY_PARAM(drm_cmdline_invalid, drm_cmdline_invalid_tests, drm_cmdline_invalid_desc);

struct drm_cmdline_tv_option_test {
	const char *name;
	const char *cmdline;
	struct drm_display_mode *(*mode_fn)(struct drm_device *dev);
	enum drm_connector_tv_mode tv_mode;
};

static void drm_test_cmdline_tv_options(struct kunit *test)
{
	const struct drm_cmdline_tv_option_test *params = test->param_value;
	const struct drm_display_mode *expected_mode = params->mode_fn(NULL);
	struct drm_cmdline_mode mode = { };

	KUNIT_EXPECT_TRUE(test, drm_mode_parse_command_line_for_connector(params->cmdline,
									  &no_connector, &mode));
	KUNIT_EXPECT_TRUE(test, mode.specified);
	KUNIT_EXPECT_EQ(test, mode.xres, expected_mode->hdisplay);
	KUNIT_EXPECT_EQ(test, mode.yres, expected_mode->vdisplay);
	KUNIT_EXPECT_EQ(test, mode.tv_mode, params->tv_mode);

	KUNIT_EXPECT_FALSE(test, mode.refresh_specified);

	KUNIT_EXPECT_FALSE(test, mode.bpp_specified);

	KUNIT_EXPECT_FALSE(test, mode.rb);
	KUNIT_EXPECT_FALSE(test, mode.cvt);
	KUNIT_EXPECT_EQ(test, mode.interlace, !!(expected_mode->flags & DRM_MODE_FLAG_INTERLACE));
	KUNIT_EXPECT_FALSE(test, mode.margins);
	KUNIT_EXPECT_EQ(test, mode.force, DRM_FORCE_UNSPECIFIED);
}

#define TV_OPT_TEST(_opt, _cmdline, _mode_fn)		\
	{						\
		.name = #_opt,				\
		.cmdline = _cmdline,			\
		.mode_fn = _mode_fn,			\
		.tv_mode = DRM_MODE_TV_MODE_ ## _opt,	\
	}

static const struct drm_cmdline_tv_option_test drm_cmdline_tv_option_tests[] = {
	TV_OPT_TEST(NTSC, "720x480i,tv_mode=NTSC", drm_mode_analog_ntsc_480i),
	TV_OPT_TEST(NTSC_443, "720x480i,tv_mode=NTSC-443", drm_mode_analog_ntsc_480i),
	TV_OPT_TEST(NTSC_J, "720x480i,tv_mode=NTSC-J", drm_mode_analog_ntsc_480i),
	TV_OPT_TEST(PAL, "720x576i,tv_mode=PAL", drm_mode_analog_pal_576i),
	TV_OPT_TEST(PAL_M, "720x480i,tv_mode=PAL-M", drm_mode_analog_ntsc_480i),
	TV_OPT_TEST(PAL_N, "720x576i,tv_mode=PAL-N", drm_mode_analog_pal_576i),
	TV_OPT_TEST(SECAM, "720x576i,tv_mode=SECAM", drm_mode_analog_pal_576i),
	{
		.name = "MONO_525",
		.cmdline = "720x480i,tv_mode=Mono",
		.mode_fn = drm_mode_analog_ntsc_480i,
		.tv_mode = DRM_MODE_TV_MODE_MONOCHROME,
	}, {
		.name = "MONO_625",
		.cmdline = "720x576i,tv_mode=Mono",
		.mode_fn = drm_mode_analog_pal_576i,
		.tv_mode = DRM_MODE_TV_MODE_MONOCHROME,
	},
};

static void drm_cmdline_tv_option_desc(const struct drm_cmdline_tv_option_test *t,
				       char *desc)
{
	sprintf(desc, "%s", t->name);
}

KUNIT_ARRAY_PARAM(drm_cmdline_tv_option,
		  drm_cmdline_tv_option_tests,
		  drm_cmdline_tv_option_desc);

static struct kunit_case drm_cmdline_parser_tests[] = {
	KUNIT_CASE(drm_test_cmdline_force_d_only),
	KUNIT_CASE(drm_test_cmdline_force_D_only_dvi),
	KUNIT_CASE(drm_test_cmdline_force_D_only_hdmi),
	KUNIT_CASE(drm_test_cmdline_force_D_only_not_digital),
	KUNIT_CASE(drm_test_cmdline_force_e_only),
	KUNIT_CASE(drm_test_cmdline_res),
	KUNIT_CASE(drm_test_cmdline_res_vesa),
	KUNIT_CASE(drm_test_cmdline_res_vesa_rblank),
	KUNIT_CASE(drm_test_cmdline_res_rblank),
	KUNIT_CASE(drm_test_cmdline_res_bpp),
	KUNIT_CASE(drm_test_cmdline_res_refresh),
	KUNIT_CASE(drm_test_cmdline_res_bpp_refresh),
	KUNIT_CASE(drm_test_cmdline_res_bpp_refresh_interlaced),
	KUNIT_CASE(drm_test_cmdline_res_bpp_refresh_margins),
	KUNIT_CASE(drm_test_cmdline_res_bpp_refresh_force_off),
	KUNIT_CASE(drm_test_cmdline_res_bpp_refresh_force_on),
	KUNIT_CASE(drm_test_cmdline_res_bpp_refresh_force_on_analog),
	KUNIT_CASE(drm_test_cmdline_res_bpp_refresh_force_on_digital),
	KUNIT_CASE(drm_test_cmdline_res_bpp_refresh_interlaced_margins_force_on),
	KUNIT_CASE(drm_test_cmdline_res_margins_force_on),
	KUNIT_CASE(drm_test_cmdline_res_vesa_margins),
	KUNIT_CASE(drm_test_cmdline_name),
	KUNIT_CASE(drm_test_cmdline_name_bpp),
	KUNIT_CASE(drm_test_cmdline_name_option),
	KUNIT_CASE(drm_test_cmdline_name_bpp_option),
	KUNIT_CASE(drm_test_cmdline_rotate_0),
	KUNIT_CASE(drm_test_cmdline_rotate_90),
	KUNIT_CASE(drm_test_cmdline_rotate_180),
	KUNIT_CASE(drm_test_cmdline_rotate_270),
	KUNIT_CASE(drm_test_cmdline_hmirror),
	KUNIT_CASE(drm_test_cmdline_vmirror),
	KUNIT_CASE(drm_test_cmdline_margin_options),
	KUNIT_CASE(drm_test_cmdline_multiple_options),
	KUNIT_CASE(drm_test_cmdline_bpp_extra_and_option),
	KUNIT_CASE(drm_test_cmdline_extra_and_option),
	KUNIT_CASE(drm_test_cmdline_freestanding_options),
	KUNIT_CASE(drm_test_cmdline_freestanding_force_e_and_options),
	KUNIT_CASE(drm_test_cmdline_panel_orientation),
	KUNIT_CASE_PARAM(drm_test_cmdline_invalid, drm_cmdline_invalid_gen_params),
	KUNIT_CASE_PARAM(drm_test_cmdline_tv_options, drm_cmdline_tv_option_gen_params),
	{}
};

static struct kunit_suite drm_cmdline_parser_test_suite = {
	.name = "drm_cmdline_parser",
	.test_cases = drm_cmdline_parser_tests
};

kunit_test_suite(drm_cmdline_parser_test_suite);

MODULE_AUTHOR("Maxime Ripard <maxime.ripard@bootlin.com>");
MODULE_DESCRIPTION("Kunit test for drm_cmdline_parser functions");
MODULE_LICENSE("GPL");
