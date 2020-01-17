// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 Bootlin
 */

#define pr_fmt(fmt) "drm_cmdline: " fmt

#include <linux/kernel.h>
#include <linux/module.h>

#include <drm/drm_connector.h>
#include <drm/drm_modes.h>

#define TESTS "drm_cmdline_selftests.h"
#include "drm_selftest.h"
#include "test-drm_modeset_common.h"

static const struct drm_connector yes_connector = {};

static int drm_cmdline_test_force_e_only(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(!drm_mode_parse_command_line_for_connector("e",
							   &yes_connector,
							   &mode));
	FAIL_ON(mode.specified);
	FAIL_ON(mode.refresh_specified);
	FAIL_ON(mode.bpp_specified);

	FAIL_ON(mode.rb);
	FAIL_ON(mode.cvt);
	FAIL_ON(mode.interlace);
	FAIL_ON(mode.margins);
	FAIL_ON(mode.force != DRM_FORCE_ON);

	return 0;
}

static int drm_cmdline_test_force_D_only_yest_digital(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(!drm_mode_parse_command_line_for_connector("D",
							   &yes_connector,
							   &mode));
	FAIL_ON(mode.specified);
	FAIL_ON(mode.refresh_specified);
	FAIL_ON(mode.bpp_specified);

	FAIL_ON(mode.rb);
	FAIL_ON(mode.cvt);
	FAIL_ON(mode.interlace);
	FAIL_ON(mode.margins);
	FAIL_ON(mode.force != DRM_FORCE_ON);

	return 0;
}

static const struct drm_connector connector_hdmi = {
	.connector_type	= DRM_MODE_CONNECTOR_HDMIB,
};

static int drm_cmdline_test_force_D_only_hdmi(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(!drm_mode_parse_command_line_for_connector("D",
							   &connector_hdmi,
							   &mode));
	FAIL_ON(mode.specified);
	FAIL_ON(mode.refresh_specified);
	FAIL_ON(mode.bpp_specified);

	FAIL_ON(mode.rb);
	FAIL_ON(mode.cvt);
	FAIL_ON(mode.interlace);
	FAIL_ON(mode.margins);
	FAIL_ON(mode.force != DRM_FORCE_ON_DIGITAL);

	return 0;
}

static const struct drm_connector connector_dvi = {
	.connector_type	= DRM_MODE_CONNECTOR_DVII,
};

static int drm_cmdline_test_force_D_only_dvi(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(!drm_mode_parse_command_line_for_connector("D",
							   &connector_dvi,
							   &mode));
	FAIL_ON(mode.specified);
	FAIL_ON(mode.refresh_specified);
	FAIL_ON(mode.bpp_specified);

	FAIL_ON(mode.rb);
	FAIL_ON(mode.cvt);
	FAIL_ON(mode.interlace);
	FAIL_ON(mode.margins);
	FAIL_ON(mode.force != DRM_FORCE_ON_DIGITAL);

	return 0;
}

static int drm_cmdline_test_force_d_only(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(!drm_mode_parse_command_line_for_connector("d",
							   &yes_connector,
							   &mode));
	FAIL_ON(mode.specified);
	FAIL_ON(mode.refresh_specified);
	FAIL_ON(mode.bpp_specified);

	FAIL_ON(mode.rb);
	FAIL_ON(mode.cvt);
	FAIL_ON(mode.interlace);
	FAIL_ON(mode.margins);
	FAIL_ON(mode.force != DRM_FORCE_OFF);

	return 0;
}

static int drm_cmdline_test_margin_only(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(drm_mode_parse_command_line_for_connector("m",
							  &yes_connector,
							  &mode));

	return 0;
}

static int drm_cmdline_test_interlace_only(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(drm_mode_parse_command_line_for_connector("i",
							  &yes_connector,
							  &mode));

	return 0;
}

static int drm_cmdline_test_res(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(!drm_mode_parse_command_line_for_connector("720x480",
							   &yes_connector,
							   &mode));
	FAIL_ON(!mode.specified);
	FAIL_ON(mode.xres != 720);
	FAIL_ON(mode.yres != 480);

	FAIL_ON(mode.refresh_specified);

	FAIL_ON(mode.bpp_specified);

	FAIL_ON(mode.rb);
	FAIL_ON(mode.cvt);
	FAIL_ON(mode.interlace);
	FAIL_ON(mode.margins);
	FAIL_ON(mode.force != DRM_FORCE_UNSPECIFIED);

	return 0;
}

static int drm_cmdline_test_res_missing_x(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(drm_mode_parse_command_line_for_connector("x480",
							  &yes_connector,
							  &mode));

	return 0;
}

static int drm_cmdline_test_res_missing_y(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(drm_mode_parse_command_line_for_connector("1024x",
							  &yes_connector,
							  &mode));

	return 0;
}

static int drm_cmdline_test_res_bad_y(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(drm_mode_parse_command_line_for_connector("1024xtest",
							  &yes_connector,
							  &mode));

	return 0;
}

static int drm_cmdline_test_res_missing_y_bpp(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(drm_mode_parse_command_line_for_connector("1024x-24",
							  &yes_connector,
							  &mode));

	return 0;
}

static int drm_cmdline_test_res_vesa(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(!drm_mode_parse_command_line_for_connector("720x480M",
							   &yes_connector,
							   &mode));
	FAIL_ON(!mode.specified);
	FAIL_ON(mode.xres != 720);
	FAIL_ON(mode.yres != 480);

	FAIL_ON(mode.refresh_specified);

	FAIL_ON(mode.bpp_specified);

	FAIL_ON(mode.rb);
	FAIL_ON(!mode.cvt);
	FAIL_ON(mode.interlace);
	FAIL_ON(mode.margins);
	FAIL_ON(mode.force != DRM_FORCE_UNSPECIFIED);

	return 0;
}

static int drm_cmdline_test_res_vesa_rblank(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(!drm_mode_parse_command_line_for_connector("720x480MR",
							   &yes_connector,
							   &mode));
	FAIL_ON(!mode.specified);
	FAIL_ON(mode.xres != 720);
	FAIL_ON(mode.yres != 480);

	FAIL_ON(mode.refresh_specified);

	FAIL_ON(mode.bpp_specified);

	FAIL_ON(!mode.rb);
	FAIL_ON(!mode.cvt);
	FAIL_ON(mode.interlace);
	FAIL_ON(mode.margins);
	FAIL_ON(mode.force != DRM_FORCE_UNSPECIFIED);

	return 0;
}

static int drm_cmdline_test_res_rblank(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(!drm_mode_parse_command_line_for_connector("720x480R",
							   &yes_connector,
							   &mode));
	FAIL_ON(!mode.specified);
	FAIL_ON(mode.xres != 720);
	FAIL_ON(mode.yres != 480);

	FAIL_ON(mode.refresh_specified);

	FAIL_ON(mode.bpp_specified);

	FAIL_ON(!mode.rb);
	FAIL_ON(mode.cvt);
	FAIL_ON(mode.interlace);
	FAIL_ON(mode.margins);
	FAIL_ON(mode.force != DRM_FORCE_UNSPECIFIED);

	return 0;
}

static int drm_cmdline_test_res_bpp(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(!drm_mode_parse_command_line_for_connector("720x480-24",
							   &yes_connector,
							   &mode));
	FAIL_ON(!mode.specified);
	FAIL_ON(mode.xres != 720);
	FAIL_ON(mode.yres != 480);

	FAIL_ON(mode.refresh_specified);

	FAIL_ON(!mode.bpp_specified);
	FAIL_ON(mode.bpp != 24);

	FAIL_ON(mode.rb);
	FAIL_ON(mode.cvt);
	FAIL_ON(mode.interlace);
	FAIL_ON(mode.margins);
	FAIL_ON(mode.force != DRM_FORCE_UNSPECIFIED);

	return 0;
}

static int drm_cmdline_test_res_bad_bpp(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(drm_mode_parse_command_line_for_connector("720x480-test",
							  &yes_connector,
							  &mode));

	return 0;
}

static int drm_cmdline_test_res_refresh(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(!drm_mode_parse_command_line_for_connector("720x480@60",
							   &yes_connector,
							   &mode));
	FAIL_ON(!mode.specified);
	FAIL_ON(mode.xres != 720);
	FAIL_ON(mode.yres != 480);

	FAIL_ON(!mode.refresh_specified);
	FAIL_ON(mode.refresh != 60);

	FAIL_ON(mode.bpp_specified);

	FAIL_ON(mode.rb);
	FAIL_ON(mode.cvt);
	FAIL_ON(mode.interlace);
	FAIL_ON(mode.margins);
	FAIL_ON(mode.force != DRM_FORCE_UNSPECIFIED);

	return 0;
}

static int drm_cmdline_test_res_bad_refresh(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(drm_mode_parse_command_line_for_connector("720x480@refresh",
							  &yes_connector,
							  &mode));

	return 0;
}

static int drm_cmdline_test_res_bpp_refresh(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(!drm_mode_parse_command_line_for_connector("720x480-24@60",
							   &yes_connector,
							   &mode));
	FAIL_ON(!mode.specified);
	FAIL_ON(mode.xres != 720);
	FAIL_ON(mode.yres != 480);

	FAIL_ON(!mode.refresh_specified);
	FAIL_ON(mode.refresh != 60);

	FAIL_ON(!mode.bpp_specified);
	FAIL_ON(mode.bpp != 24);

	FAIL_ON(mode.rb);
	FAIL_ON(mode.cvt);
	FAIL_ON(mode.interlace);
	FAIL_ON(mode.margins);
	FAIL_ON(mode.force != DRM_FORCE_UNSPECIFIED);

	return 0;
}

static int drm_cmdline_test_res_bpp_refresh_interlaced(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(!drm_mode_parse_command_line_for_connector("720x480-24@60i",
							   &yes_connector,
							   &mode));
	FAIL_ON(!mode.specified);
	FAIL_ON(mode.xres != 720);
	FAIL_ON(mode.yres != 480);

	FAIL_ON(!mode.refresh_specified);
	FAIL_ON(mode.refresh != 60);

	FAIL_ON(!mode.bpp_specified);
	FAIL_ON(mode.bpp != 24);

	FAIL_ON(mode.rb);
	FAIL_ON(mode.cvt);
	FAIL_ON(!mode.interlace);
	FAIL_ON(mode.margins);
	FAIL_ON(mode.force != DRM_FORCE_UNSPECIFIED);

	return 0;
}

static int drm_cmdline_test_res_bpp_refresh_margins(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(!drm_mode_parse_command_line_for_connector("720x480-24@60m",
							   &yes_connector,
							   &mode));
	FAIL_ON(!mode.specified);
	FAIL_ON(mode.xres != 720);
	FAIL_ON(mode.yres != 480);

	FAIL_ON(!mode.refresh_specified);
	FAIL_ON(mode.refresh != 60);

	FAIL_ON(!mode.bpp_specified);
	FAIL_ON(mode.bpp != 24);

	FAIL_ON(mode.rb);
	FAIL_ON(mode.cvt);
	FAIL_ON(mode.interlace);
	FAIL_ON(!mode.margins);
	FAIL_ON(mode.force != DRM_FORCE_UNSPECIFIED);

	return 0;
}

static int drm_cmdline_test_res_bpp_refresh_force_off(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(!drm_mode_parse_command_line_for_connector("720x480-24@60d",
							   &yes_connector,
							   &mode));
	FAIL_ON(!mode.specified);
	FAIL_ON(mode.xres != 720);
	FAIL_ON(mode.yres != 480);

	FAIL_ON(!mode.refresh_specified);
	FAIL_ON(mode.refresh != 60);

	FAIL_ON(!mode.bpp_specified);
	FAIL_ON(mode.bpp != 24);

	FAIL_ON(mode.rb);
	FAIL_ON(mode.cvt);
	FAIL_ON(mode.interlace);
	FAIL_ON(mode.margins);
	FAIL_ON(mode.force != DRM_FORCE_OFF);

	return 0;
}

static int drm_cmdline_test_res_bpp_refresh_force_on_off(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(drm_mode_parse_command_line_for_connector("720x480-24@60de",
							  &yes_connector,
							  &mode));

	return 0;
}

static int drm_cmdline_test_res_bpp_refresh_force_on(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(!drm_mode_parse_command_line_for_connector("720x480-24@60e",
							   &yes_connector,
							   &mode));
	FAIL_ON(!mode.specified);
	FAIL_ON(mode.xres != 720);
	FAIL_ON(mode.yres != 480);

	FAIL_ON(!mode.refresh_specified);
	FAIL_ON(mode.refresh != 60);

	FAIL_ON(!mode.bpp_specified);
	FAIL_ON(mode.bpp != 24);

	FAIL_ON(mode.rb);
	FAIL_ON(mode.cvt);
	FAIL_ON(mode.interlace);
	FAIL_ON(mode.margins);
	FAIL_ON(mode.force != DRM_FORCE_ON);

	return 0;
}

static int drm_cmdline_test_res_bpp_refresh_force_on_analog(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(!drm_mode_parse_command_line_for_connector("720x480-24@60D",
							   &yes_connector,
							   &mode));
	FAIL_ON(!mode.specified);
	FAIL_ON(mode.xres != 720);
	FAIL_ON(mode.yres != 480);

	FAIL_ON(!mode.refresh_specified);
	FAIL_ON(mode.refresh != 60);

	FAIL_ON(!mode.bpp_specified);
	FAIL_ON(mode.bpp != 24);

	FAIL_ON(mode.rb);
	FAIL_ON(mode.cvt);
	FAIL_ON(mode.interlace);
	FAIL_ON(mode.margins);
	FAIL_ON(mode.force != DRM_FORCE_ON);

	return 0;
}

static int drm_cmdline_test_res_bpp_refresh_force_on_digital(void *igyesred)
{
	struct drm_cmdline_mode mode = { };
	static const struct drm_connector connector = {
		.connector_type = DRM_MODE_CONNECTOR_DVII,
	};

	FAIL_ON(!drm_mode_parse_command_line_for_connector("720x480-24@60D",
							   &connector,
							   &mode));
	FAIL_ON(!mode.specified);
	FAIL_ON(mode.xres != 720);
	FAIL_ON(mode.yres != 480);

	FAIL_ON(!mode.refresh_specified);
	FAIL_ON(mode.refresh != 60);

	FAIL_ON(!mode.bpp_specified);
	FAIL_ON(mode.bpp != 24);

	FAIL_ON(mode.rb);
	FAIL_ON(mode.cvt);
	FAIL_ON(mode.interlace);
	FAIL_ON(mode.margins);
	FAIL_ON(mode.force != DRM_FORCE_ON_DIGITAL);

	return 0;
}

static int drm_cmdline_test_res_bpp_refresh_interlaced_margins_force_on(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(!drm_mode_parse_command_line_for_connector("720x480-24@60ime",
							   &yes_connector,
							   &mode));
	FAIL_ON(!mode.specified);
	FAIL_ON(mode.xres != 720);
	FAIL_ON(mode.yres != 480);

	FAIL_ON(!mode.refresh_specified);
	FAIL_ON(mode.refresh != 60);

	FAIL_ON(!mode.bpp_specified);
	FAIL_ON(mode.bpp != 24);

	FAIL_ON(mode.rb);
	FAIL_ON(mode.cvt);
	FAIL_ON(!mode.interlace);
	FAIL_ON(!mode.margins);
	FAIL_ON(mode.force != DRM_FORCE_ON);

	return 0;
}

static int drm_cmdline_test_res_margins_force_on(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(!drm_mode_parse_command_line_for_connector("720x480me",
							   &yes_connector,
							   &mode));
	FAIL_ON(!mode.specified);
	FAIL_ON(mode.xres != 720);
	FAIL_ON(mode.yres != 480);

	FAIL_ON(mode.refresh_specified);

	FAIL_ON(mode.bpp_specified);

	FAIL_ON(mode.rb);
	FAIL_ON(mode.cvt);
	FAIL_ON(mode.interlace);
	FAIL_ON(!mode.margins);
	FAIL_ON(mode.force != DRM_FORCE_ON);

	return 0;
}

static int drm_cmdline_test_res_vesa_margins(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(!drm_mode_parse_command_line_for_connector("720x480Mm",
							   &yes_connector,
							   &mode));
	FAIL_ON(!mode.specified);
	FAIL_ON(mode.xres != 720);
	FAIL_ON(mode.yres != 480);

	FAIL_ON(mode.refresh_specified);

	FAIL_ON(mode.bpp_specified);

	FAIL_ON(mode.rb);
	FAIL_ON(!mode.cvt);
	FAIL_ON(mode.interlace);
	FAIL_ON(!mode.margins);
	FAIL_ON(mode.force != DRM_FORCE_UNSPECIFIED);

	return 0;
}

static int drm_cmdline_test_res_invalid_mode(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(drm_mode_parse_command_line_for_connector("720x480f",
							  &yes_connector,
							  &mode));

	return 0;
}

static int drm_cmdline_test_res_bpp_wrong_place_mode(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(drm_mode_parse_command_line_for_connector("720x480e-24",
							  &yes_connector,
							  &mode));

	return 0;
}

static int drm_cmdline_test_name(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(!drm_mode_parse_command_line_for_connector("NTSC",
							   &yes_connector,
							   &mode));
	FAIL_ON(strcmp(mode.name, "NTSC"));
	FAIL_ON(mode.refresh_specified);
	FAIL_ON(mode.bpp_specified);

	return 0;
}

static int drm_cmdline_test_name_bpp(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(!drm_mode_parse_command_line_for_connector("NTSC-24",
							   &yes_connector,
							   &mode));
	FAIL_ON(strcmp(mode.name, "NTSC"));

	FAIL_ON(mode.refresh_specified);

	FAIL_ON(!mode.bpp_specified);
	FAIL_ON(mode.bpp != 24);

	return 0;
}

static int drm_cmdline_test_name_bpp_refresh(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(drm_mode_parse_command_line_for_connector("NTSC-24@60",
							  &yes_connector,
							  &mode));

	return 0;
}

static int drm_cmdline_test_name_refresh(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(drm_mode_parse_command_line_for_connector("NTSC@60",
							  &yes_connector,
							  &mode));

	return 0;
}

static int drm_cmdline_test_name_refresh_wrong_mode(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(drm_mode_parse_command_line_for_connector("NTSC@60m",
							  &yes_connector,
							  &mode));

	return 0;
}

static int drm_cmdline_test_name_refresh_invalid_mode(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(drm_mode_parse_command_line_for_connector("NTSC@60f",
							  &yes_connector,
							  &mode));

	return 0;
}

static int drm_cmdline_test_name_option(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(!drm_mode_parse_command_line_for_connector("NTSC,rotate=180",
							   &yes_connector,
							   &mode));
	FAIL_ON(!mode.specified);
	FAIL_ON(strcmp(mode.name, "NTSC"));
	FAIL_ON(mode.rotation_reflection != DRM_MODE_ROTATE_180);

	return 0;
}

static int drm_cmdline_test_name_bpp_option(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(!drm_mode_parse_command_line_for_connector("NTSC-24,rotate=180",
							   &yes_connector,
							   &mode));
	FAIL_ON(!mode.specified);
	FAIL_ON(strcmp(mode.name, "NTSC"));
	FAIL_ON(mode.rotation_reflection != DRM_MODE_ROTATE_180);
	FAIL_ON(!mode.bpp_specified);
	FAIL_ON(mode.bpp != 24);

	return 0;
}

static int drm_cmdline_test_rotate_0(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(!drm_mode_parse_command_line_for_connector("720x480,rotate=0",
							   &yes_connector,
							   &mode));
	FAIL_ON(!mode.specified);
	FAIL_ON(mode.xres != 720);
	FAIL_ON(mode.yres != 480);
	FAIL_ON(mode.rotation_reflection != DRM_MODE_ROTATE_0);

	FAIL_ON(mode.refresh_specified);

	FAIL_ON(mode.bpp_specified);

	FAIL_ON(mode.rb);
	FAIL_ON(mode.cvt);
	FAIL_ON(mode.interlace);
	FAIL_ON(mode.margins);
	FAIL_ON(mode.force != DRM_FORCE_UNSPECIFIED);

	return 0;
}

static int drm_cmdline_test_rotate_90(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(!drm_mode_parse_command_line_for_connector("720x480,rotate=90",
							   &yes_connector,
							   &mode));
	FAIL_ON(!mode.specified);
	FAIL_ON(mode.xres != 720);
	FAIL_ON(mode.yres != 480);
	FAIL_ON(mode.rotation_reflection != DRM_MODE_ROTATE_90);

	FAIL_ON(mode.refresh_specified);

	FAIL_ON(mode.bpp_specified);

	FAIL_ON(mode.rb);
	FAIL_ON(mode.cvt);
	FAIL_ON(mode.interlace);
	FAIL_ON(mode.margins);
	FAIL_ON(mode.force != DRM_FORCE_UNSPECIFIED);

	return 0;
}

static int drm_cmdline_test_rotate_180(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(!drm_mode_parse_command_line_for_connector("720x480,rotate=180",
							   &yes_connector,
							   &mode));
	FAIL_ON(!mode.specified);
	FAIL_ON(mode.xres != 720);
	FAIL_ON(mode.yres != 480);
	FAIL_ON(mode.rotation_reflection != DRM_MODE_ROTATE_180);

	FAIL_ON(mode.refresh_specified);

	FAIL_ON(mode.bpp_specified);

	FAIL_ON(mode.rb);
	FAIL_ON(mode.cvt);
	FAIL_ON(mode.interlace);
	FAIL_ON(mode.margins);
	FAIL_ON(mode.force != DRM_FORCE_UNSPECIFIED);

	return 0;
}

static int drm_cmdline_test_rotate_270(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(!drm_mode_parse_command_line_for_connector("720x480,rotate=270",
							   &yes_connector,
							   &mode));
	FAIL_ON(!mode.specified);
	FAIL_ON(mode.xres != 720);
	FAIL_ON(mode.yres != 480);
	FAIL_ON(mode.rotation_reflection != DRM_MODE_ROTATE_270);

	FAIL_ON(mode.refresh_specified);

	FAIL_ON(mode.bpp_specified);

	FAIL_ON(mode.rb);
	FAIL_ON(mode.cvt);
	FAIL_ON(mode.interlace);
	FAIL_ON(mode.margins);
	FAIL_ON(mode.force != DRM_FORCE_UNSPECIFIED);

	return 0;
}

static int drm_cmdline_test_rotate_invalid_val(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(drm_mode_parse_command_line_for_connector("720x480,rotate=42",
							  &yes_connector,
							  &mode));

	return 0;
}

static int drm_cmdline_test_rotate_truncated(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(drm_mode_parse_command_line_for_connector("720x480,rotate=",
							  &yes_connector,
							  &mode));

	return 0;
}

static int drm_cmdline_test_hmirror(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(!drm_mode_parse_command_line_for_connector("720x480,reflect_x",
							   &yes_connector,
							   &mode));
	FAIL_ON(!mode.specified);
	FAIL_ON(mode.xres != 720);
	FAIL_ON(mode.yres != 480);
	FAIL_ON(mode.rotation_reflection != DRM_MODE_REFLECT_X);

	FAIL_ON(mode.refresh_specified);

	FAIL_ON(mode.bpp_specified);

	FAIL_ON(mode.rb);
	FAIL_ON(mode.cvt);
	FAIL_ON(mode.interlace);
	FAIL_ON(mode.margins);
	FAIL_ON(mode.force != DRM_FORCE_UNSPECIFIED);

	return 0;
}

static int drm_cmdline_test_vmirror(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(!drm_mode_parse_command_line_for_connector("720x480,reflect_y",
							   &yes_connector,
							   &mode));
	FAIL_ON(!mode.specified);
	FAIL_ON(mode.xres != 720);
	FAIL_ON(mode.yres != 480);
	FAIL_ON(mode.rotation_reflection != DRM_MODE_REFLECT_Y);

	FAIL_ON(mode.refresh_specified);

	FAIL_ON(mode.bpp_specified);

	FAIL_ON(mode.rb);
	FAIL_ON(mode.cvt);
	FAIL_ON(mode.interlace);
	FAIL_ON(mode.margins);
	FAIL_ON(mode.force != DRM_FORCE_UNSPECIFIED);

	return 0;
}

static int drm_cmdline_test_margin_options(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(!drm_mode_parse_command_line_for_connector("720x480,margin_right=14,margin_left=24,margin_bottom=36,margin_top=42",
							   &yes_connector,
							   &mode));
	FAIL_ON(!mode.specified);
	FAIL_ON(mode.xres != 720);
	FAIL_ON(mode.yres != 480);
	FAIL_ON(mode.tv_margins.right != 14);
	FAIL_ON(mode.tv_margins.left != 24);
	FAIL_ON(mode.tv_margins.bottom != 36);
	FAIL_ON(mode.tv_margins.top != 42);

	FAIL_ON(mode.refresh_specified);

	FAIL_ON(mode.bpp_specified);

	FAIL_ON(mode.rb);
	FAIL_ON(mode.cvt);
	FAIL_ON(mode.interlace);
	FAIL_ON(mode.margins);
	FAIL_ON(mode.force != DRM_FORCE_UNSPECIFIED);

	return 0;
}

static int drm_cmdline_test_multiple_options(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(!drm_mode_parse_command_line_for_connector("720x480,rotate=270,reflect_x",
							   &yes_connector,
							   &mode));
	FAIL_ON(!mode.specified);
	FAIL_ON(mode.xres != 720);
	FAIL_ON(mode.yres != 480);
	FAIL_ON(mode.rotation_reflection != (DRM_MODE_ROTATE_270 | DRM_MODE_REFLECT_X));

	FAIL_ON(mode.refresh_specified);

	FAIL_ON(mode.bpp_specified);

	FAIL_ON(mode.rb);
	FAIL_ON(mode.cvt);
	FAIL_ON(mode.interlace);
	FAIL_ON(mode.margins);
	FAIL_ON(mode.force != DRM_FORCE_UNSPECIFIED);

	return 0;
}

static int drm_cmdline_test_invalid_option(void *igyesred)
{
	struct drm_cmdline_mode mode = { };

	FAIL_ON(drm_mode_parse_command_line_for_connector("720x480,test=42",
							  &yes_connector,
							  &mode));

	return 0;
}

#include "drm_selftest.c"

static int __init test_drm_cmdline_init(void)
{
	int err;

	err = run_selftests(selftests, ARRAY_SIZE(selftests), NULL);

	return err > 0 ? 0 : err;
}
module_init(test_drm_cmdline_init);

MODULE_AUTHOR("Maxime Ripard <maxime.ripard@bootlin.com>");
MODULE_LICENSE("GPL");
