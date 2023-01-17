// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Maxime Ripard <mripard@kernel.org>
 */

#include <kunit/test.h>

#include <drm/drm_connector.h>
#include <drm/drm_edid.h>
#include <drm/drm_drv.h>
#include <drm/drm_modes.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_probe_helper.h>

#include "drm_kunit_helpers.h"

struct drm_client_modeset_test_priv {
	struct drm_device *drm;
	struct drm_connector connector;
};

static int drm_client_modeset_connector_get_modes(struct drm_connector *connector)
{
	return drm_add_modes_noedid(connector, 1920, 1200);
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

	priv->drm = drm_kunit_device_init(test, DRIVER_MODESET, "drm-client-modeset-test");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv->drm);

	ret = drmm_connector_init(priv->drm, &priv->connector,
				  &drm_client_modeset_connector_funcs,
				  DRM_MODE_CONNECTOR_Unknown,
				  NULL);
	KUNIT_ASSERT_EQ(test, ret, 0);

	drm_connector_helper_add(&priv->connector, &drm_client_modeset_connector_helper_funcs);

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

static struct kunit_case drm_test_pick_cmdline_tests[] = {
	KUNIT_CASE(drm_test_pick_cmdline_res_1920_1080_60),
	{}
};

static struct kunit_suite drm_test_pick_cmdline_test_suite = {
	.name = "drm_test_pick_cmdline",
	.init = drm_client_modeset_test_init,
	.test_cases = drm_test_pick_cmdline_tests
};

kunit_test_suite(drm_test_pick_cmdline_test_suite);
