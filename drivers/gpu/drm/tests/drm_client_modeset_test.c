// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Maxime Ripard <mripard@kernel.org>
 */

#include <kunit/test.h>

#include <drm/drm_connector.h>
#include <drm/drm_edid.h>
#include <drm/drm_drv.h>
#include <drm/drm_kunit_helpers.h>
#include <drm/drm_modes.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_probe_helper.h>

struct drm_client_modeset_test_priv {
	struct drm_device *drm;
	struct device *dev;
	struct drm_connector connector;
};

static int drm_client_modeset_connector_get_modes(struct drm_connector *connector)
{
	struct drm_display_mode *mode;
	int count;

	count = drm_add_modes_noedid(connector, 1920, 1200);

	mode = drm_mode_analog_ntsc_480i(connector->dev);
	if (!mode)
		return count;

	drm_mode_probed_add(connector, mode);
	count += 1;

	mode = drm_mode_analog_pal_576i(connector->dev);
	if (!mode)
		return count;

	drm_mode_probed_add(connector, mode);
	count += 1;

	return count;
}

static const struct drm_connector_helper_funcs drm_client_modeset_connector_helper_funcs = {
	.get_modes = drm_client_modeset_connector_get_modes,
};

static const struct drm_connector_funcs drm_client_modeset_connector_funcs = {
};

static int drm_client_modeset_test_init(struct kunit *test)
{
	struct drm_client_modeset_test_priv *priv;
	int ret;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, priv);

	test->priv = priv;

	priv->dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv->dev);

	priv->drm = __drm_kunit_helper_alloc_drm_device(test, priv->dev,
							sizeof(*priv->drm), 0,
							DRIVER_MODESET);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv->drm);

	ret = drmm_connector_init(priv->drm, &priv->connector,
				  &drm_client_modeset_connector_funcs,
				  DRM_MODE_CONNECTOR_Unknown,
				  NULL);
	KUNIT_ASSERT_EQ(test, ret, 0);

	drm_connector_helper_add(&priv->connector, &drm_client_modeset_connector_helper_funcs);

	priv->connector.interlace_allowed = true;
	priv->connector.doublescan_allowed = true;

	return 0;
}

static void drm_test_pick_cmdline_res_1920_1080_60(struct kunit *test)
{
	struct drm_client_modeset_test_priv *priv = test->priv;
	struct drm_device *drm = priv->drm;
	struct drm_connector *connector = &priv->connector;
	struct drm_cmdline_mode *cmdline_mode = &connector->cmdline_mode;
	struct drm_display_mode *expected_mode, *mode;
	const char *cmdline = "1920x1080@60";
	int ret;

	expected_mode = drm_mode_find_dmt(priv->drm, 1920, 1080, 60, false);
	KUNIT_ASSERT_NOT_NULL(test, expected_mode);

	KUNIT_ASSERT_TRUE(test,
			  drm_mode_parse_command_line_for_connector(cmdline,
								    connector,
								    cmdline_mode));

	mutex_lock(&drm->mode_config.mutex);
	ret = drm_helper_probe_single_connector_modes(connector, 1920, 1080);
	mutex_unlock(&drm->mode_config.mutex);
	KUNIT_ASSERT_GT(test, ret, 0);

	mode = drm_connector_pick_cmdline_mode(connector);
	KUNIT_ASSERT_NOT_NULL(test, mode);

	KUNIT_EXPECT_TRUE(test, drm_mode_equal(expected_mode, mode));
}

struct drm_connector_pick_cmdline_mode_test {
	const char *cmdline;
	struct drm_display_mode *(*func)(struct drm_device *drm);
};

#define TEST_CMDLINE(_cmdline, _fn)		\
	{					\
		.cmdline = _cmdline,		\
		.func = _fn,			\
	}

static void drm_test_pick_cmdline_named(struct kunit *test)
{
	const struct drm_connector_pick_cmdline_mode_test *params = test->param_value;
	struct drm_client_modeset_test_priv *priv = test->priv;
	struct drm_device *drm = priv->drm;
	struct drm_connector *connector = &priv->connector;
	struct drm_cmdline_mode *cmdline_mode = &connector->cmdline_mode;
	const struct drm_display_mode *expected_mode, *mode;
	const char *cmdline = params->cmdline;
	int ret;

	KUNIT_ASSERT_TRUE(test,
			  drm_mode_parse_command_line_for_connector(cmdline,
								    connector,
								    cmdline_mode));

	mutex_lock(&drm->mode_config.mutex);
	ret = drm_helper_probe_single_connector_modes(connector, 1920, 1080);
	mutex_unlock(&drm->mode_config.mutex);
	KUNIT_ASSERT_GT(test, ret, 0);

	mode = drm_connector_pick_cmdline_mode(connector);
	KUNIT_ASSERT_NOT_NULL(test, mode);

	expected_mode = params->func(drm);
	KUNIT_ASSERT_NOT_NULL(test, expected_mode);

	KUNIT_EXPECT_TRUE(test, drm_mode_equal(expected_mode, mode));
}

static const
struct drm_connector_pick_cmdline_mode_test drm_connector_pick_cmdline_mode_tests[] = {
	TEST_CMDLINE("NTSC", drm_mode_analog_ntsc_480i),
	TEST_CMDLINE("NTSC-J", drm_mode_analog_ntsc_480i),
	TEST_CMDLINE("PAL", drm_mode_analog_pal_576i),
	TEST_CMDLINE("PAL-M", drm_mode_analog_ntsc_480i),
};

static void
drm_connector_pick_cmdline_mode_desc(const struct drm_connector_pick_cmdline_mode_test *t,
				     char *desc)
{
	sprintf(desc, "%s", t->cmdline);
}

KUNIT_ARRAY_PARAM(drm_connector_pick_cmdline_mode,
		  drm_connector_pick_cmdline_mode_tests,
		  drm_connector_pick_cmdline_mode_desc);

static struct kunit_case drm_test_pick_cmdline_tests[] = {
	KUNIT_CASE(drm_test_pick_cmdline_res_1920_1080_60),
	KUNIT_CASE_PARAM(drm_test_pick_cmdline_named,
			 drm_connector_pick_cmdline_mode_gen_params),
	{}
};

static struct kunit_suite drm_test_pick_cmdline_test_suite = {
	.name = "drm_test_pick_cmdline",
	.init = drm_client_modeset_test_init,
	.test_cases = drm_test_pick_cmdline_tests
};

kunit_test_suite(drm_test_pick_cmdline_test_suite);

/*
 * This file is included directly by drm_client_modeset.c so we can't
 * use any MODULE_* macro here.
 */
