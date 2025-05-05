// SPDX-License-Identifier: GPL-2.0
/*
 * Kunit test for drm_probe_helper functions
 */

#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_kunit_helpers.h>
#include <drm/drm_mode.h>
#include <drm/drm_modes.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_probe_helper.h>

#include <kunit/test.h>

struct drm_probe_helper_test_priv {
	struct drm_device *drm;
	struct device *dev;
	struct drm_connector connector;
};

static const struct drm_connector_helper_funcs drm_probe_helper_connector_helper_funcs = {
};

static const struct drm_connector_funcs drm_probe_helper_connector_funcs = {
	.atomic_destroy_state	= drm_atomic_helper_connector_destroy_state,
	.atomic_duplicate_state	= drm_atomic_helper_connector_duplicate_state,
	.reset			= drm_atomic_helper_connector_reset,
};

static int drm_probe_helper_test_init(struct kunit *test)
{
	struct drm_probe_helper_test_priv *priv;
	struct drm_connector *connector;
	int ret;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, priv);
	test->priv = priv;

	priv->dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv->dev);

	priv->drm = __drm_kunit_helper_alloc_drm_device(test, priv->dev,
							sizeof(*priv->drm), 0,
							DRIVER_MODESET | DRIVER_ATOMIC);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv->drm);

	connector = &priv->connector;
	ret = drmm_connector_init(priv->drm, connector,
				  &drm_probe_helper_connector_funcs,
				  DRM_MODE_CONNECTOR_Unknown,
				  NULL);
	KUNIT_ASSERT_EQ(test, ret, 0);

	drm_connector_helper_add(connector, &drm_probe_helper_connector_helper_funcs);

	return 0;
}

typedef struct drm_display_mode *(*expected_mode_func_t)(struct drm_device *);

struct drm_connector_helper_tv_get_modes_test {
	const char *name;
	unsigned int supported_tv_modes;
	enum drm_connector_tv_mode default_mode;
	bool cmdline;
	enum drm_connector_tv_mode cmdline_mode;
	expected_mode_func_t *expected_modes;
	unsigned int num_expected_modes;
};

#define _TV_MODE_TEST(_name, _supported, _default, _cmdline, _cmdline_mode, ...)		\
	{											\
		.name = _name,									\
		.supported_tv_modes = _supported,						\
		.default_mode = _default,							\
		.cmdline = _cmdline,								\
		.cmdline_mode = _cmdline_mode,							\
		.expected_modes = (expected_mode_func_t[]) { __VA_ARGS__ },			\
		.num_expected_modes = sizeof((expected_mode_func_t[]) { __VA_ARGS__ }) /	\
				      (sizeof(expected_mode_func_t)),				\
	}

#define TV_MODE_TEST(_name, _supported, _default, ...)			\
	_TV_MODE_TEST(_name, _supported, _default, false, 0, __VA_ARGS__)

#define TV_MODE_TEST_CMDLINE(_name, _supported, _default, _cmdline, ...) \
	_TV_MODE_TEST(_name, _supported, _default, true, _cmdline, __VA_ARGS__)

static void
drm_test_connector_helper_tv_get_modes_check(struct kunit *test)
{
	const struct drm_connector_helper_tv_get_modes_test *params = test->param_value;
	struct drm_probe_helper_test_priv *priv = test->priv;
	struct drm_connector *connector = &priv->connector;
	struct drm_cmdline_mode *cmdline = &connector->cmdline_mode;
	struct drm_display_mode *mode;
	struct drm_display_mode *expected;
	size_t len;
	int ret;

	if (params->cmdline) {
		cmdline->tv_mode_specified = true;
		cmdline->tv_mode = params->cmdline_mode;
	}

	ret = drm_mode_create_tv_properties(priv->drm, params->supported_tv_modes);
	KUNIT_ASSERT_EQ(test, ret, 0);

	drm_object_attach_property(&connector->base,
				   priv->drm->mode_config.tv_mode_property,
				   params->default_mode);

	mutex_lock(&priv->drm->mode_config.mutex);

	ret = drm_connector_helper_tv_get_modes(connector);
	KUNIT_EXPECT_EQ(test, ret, params->num_expected_modes);

	len = 0;
	list_for_each_entry(mode, &connector->probed_modes, head)
		len++;
	KUNIT_EXPECT_EQ(test, len, params->num_expected_modes);

	if (params->num_expected_modes >= 1) {
		mode = list_first_entry_or_null(&connector->probed_modes,
						struct drm_display_mode, head);
		KUNIT_ASSERT_NOT_NULL(test, mode);

		expected = params->expected_modes[0](priv->drm);
		KUNIT_ASSERT_NOT_NULL(test, expected);

		KUNIT_EXPECT_TRUE(test, drm_mode_equal(mode, expected));
		KUNIT_EXPECT_TRUE(test, mode->type & DRM_MODE_TYPE_PREFERRED);

		ret = drm_kunit_add_mode_destroy_action(test, expected);
		KUNIT_ASSERT_EQ(test, ret, 0);
	}

	if (params->num_expected_modes >= 2) {
		mode = list_next_entry(mode, head);
		KUNIT_ASSERT_NOT_NULL(test, mode);

		expected = params->expected_modes[1](priv->drm);
		KUNIT_ASSERT_NOT_NULL(test, expected);

		KUNIT_EXPECT_TRUE(test, drm_mode_equal(mode, expected));
		KUNIT_EXPECT_FALSE(test, mode->type & DRM_MODE_TYPE_PREFERRED);

		ret = drm_kunit_add_mode_destroy_action(test, expected);
		KUNIT_ASSERT_EQ(test, ret, 0);
	}

	mutex_unlock(&priv->drm->mode_config.mutex);
}

static const
struct drm_connector_helper_tv_get_modes_test drm_connector_helper_tv_get_modes_tests[] = {
	{ .name = "None" },
	TV_MODE_TEST("PAL",
		     BIT(DRM_MODE_TV_MODE_PAL),
		     DRM_MODE_TV_MODE_PAL,
		     drm_mode_analog_pal_576i),
	TV_MODE_TEST("NTSC",
		     BIT(DRM_MODE_TV_MODE_NTSC),
		     DRM_MODE_TV_MODE_NTSC,
		     drm_mode_analog_ntsc_480i),
	TV_MODE_TEST("Both, NTSC Default",
		     BIT(DRM_MODE_TV_MODE_NTSC) | BIT(DRM_MODE_TV_MODE_PAL),
		     DRM_MODE_TV_MODE_NTSC,
		     drm_mode_analog_ntsc_480i, drm_mode_analog_pal_576i),
	TV_MODE_TEST("Both, PAL Default",
		     BIT(DRM_MODE_TV_MODE_NTSC) | BIT(DRM_MODE_TV_MODE_PAL),
		     DRM_MODE_TV_MODE_PAL,
		     drm_mode_analog_pal_576i, drm_mode_analog_ntsc_480i),
	TV_MODE_TEST_CMDLINE("Both, NTSC Default, with PAL on command-line",
			     BIT(DRM_MODE_TV_MODE_NTSC) | BIT(DRM_MODE_TV_MODE_PAL),
			     DRM_MODE_TV_MODE_NTSC,
			     DRM_MODE_TV_MODE_PAL,
			     drm_mode_analog_pal_576i, drm_mode_analog_ntsc_480i),
	TV_MODE_TEST_CMDLINE("Both, PAL Default, with NTSC on command-line",
			     BIT(DRM_MODE_TV_MODE_NTSC) | BIT(DRM_MODE_TV_MODE_PAL),
			     DRM_MODE_TV_MODE_PAL,
			     DRM_MODE_TV_MODE_NTSC,
			     drm_mode_analog_ntsc_480i, drm_mode_analog_pal_576i),
};

static void
drm_connector_helper_tv_get_modes_desc(const struct drm_connector_helper_tv_get_modes_test *t,
				       char *desc)
{
	sprintf(desc, "%s", t->name);
}

KUNIT_ARRAY_PARAM(drm_connector_helper_tv_get_modes,
		  drm_connector_helper_tv_get_modes_tests,
		  drm_connector_helper_tv_get_modes_desc);

static struct kunit_case drm_test_connector_helper_tv_get_modes_tests[] = {
	KUNIT_CASE_PARAM(drm_test_connector_helper_tv_get_modes_check,
			 drm_connector_helper_tv_get_modes_gen_params),
	{ }
};

static struct kunit_suite drm_test_connector_helper_tv_get_modes_suite = {
	.name = "drm_connector_helper_tv_get_modes",
	.init = drm_probe_helper_test_init,
	.test_cases = drm_test_connector_helper_tv_get_modes_tests,
};

kunit_test_suite(drm_test_connector_helper_tv_get_modes_suite);

MODULE_AUTHOR("Maxime Ripard <maxime@cerno.tech>");
MODULE_DESCRIPTION("Kunit test for drm_probe_helper functions");
MODULE_LICENSE("GPL");
