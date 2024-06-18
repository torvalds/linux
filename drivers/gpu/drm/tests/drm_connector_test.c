// SPDX-License-Identifier: GPL-2.0
/*
 * Kunit test for drm_modes functions
 */

#include <linux/i2c.h>

#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_drv.h>
#include <drm/drm_kunit_helpers.h>

#include <kunit/test.h>

struct drm_connector_init_priv {
	struct drm_device drm;
	struct drm_connector connector;
	struct i2c_adapter ddc;
};

static const struct drm_connector_funcs dummy_funcs = {
	.atomic_destroy_state	= drm_atomic_helper_connector_destroy_state,
	.atomic_duplicate_state	= drm_atomic_helper_connector_duplicate_state,
	.reset			= drm_atomic_helper_connector_reset,
};

static int dummy_ddc_xfer(struct i2c_adapter *adapter,
			  struct i2c_msg *msgs, int num)
{
	return num;
}

static u32 dummy_ddc_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm dummy_ddc_algorithm = {
	.master_xfer = dummy_ddc_xfer,
	.functionality = dummy_ddc_func,
};

static void i2c_del_adapter_wrapper(void *ptr)
{
	struct i2c_adapter *adap = ptr;

	i2c_del_adapter(adap);
}

static int drm_test_connector_init(struct kunit *test)
{
	struct drm_connector_init_priv *priv;
	struct device *dev;
	int ret;

	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	priv = drm_kunit_helper_alloc_drm_device(test, dev,
						 struct drm_connector_init_priv, drm,
						 DRIVER_MODESET | DRIVER_ATOMIC);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv);

	strscpy(priv->ddc.name, "dummy-connector-ddc", sizeof(priv->ddc.name));
	priv->ddc.owner = THIS_MODULE;
	priv->ddc.algo = &dummy_ddc_algorithm;
	priv->ddc.dev.parent = dev;

	ret = i2c_add_adapter(&priv->ddc);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = kunit_add_action_or_reset(test, i2c_del_adapter_wrapper, &priv->ddc);
	KUNIT_ASSERT_EQ(test, ret, 0);

	test->priv = priv;
	return 0;
}

/*
 * Test that the registration of a bog standard connector works as
 * expected and doesn't report any error.
 */
static void drm_test_drmm_connector_init(struct kunit *test)
{
	struct drm_connector_init_priv *priv = test->priv;
	int ret;

	ret = drmm_connector_init(&priv->drm, &priv->connector,
				  &dummy_funcs,
				  DRM_MODE_CONNECTOR_HDMIA,
				  &priv->ddc);
	KUNIT_EXPECT_EQ(test, ret, 0);
}

/*
 * Test that the registration of a connector without a DDC adapter
 * doesn't report any error.
 */
static void drm_test_drmm_connector_init_null_ddc(struct kunit *test)
{
	struct drm_connector_init_priv *priv = test->priv;
	int ret;

	ret = drmm_connector_init(&priv->drm, &priv->connector,
				  &dummy_funcs,
				  DRM_MODE_CONNECTOR_HDMIA,
				  NULL);
	KUNIT_EXPECT_EQ(test, ret, 0);
}

/*
 * Test that the registration of a connector succeeds for all possible
 * connector types.
 */
static void drm_test_drmm_connector_init_type_valid(struct kunit *test)
{
	struct drm_connector_init_priv *priv = test->priv;
	unsigned int connector_type = *(unsigned int *)test->param_value;
	int ret;

	ret = drmm_connector_init(&priv->drm, &priv->connector,
				  &dummy_funcs,
				  connector_type,
				  &priv->ddc);
	KUNIT_EXPECT_EQ(test, ret, 0);
}

static const unsigned int drm_connector_init_type_valid_tests[] = {
	DRM_MODE_CONNECTOR_Unknown,
	DRM_MODE_CONNECTOR_VGA,
	DRM_MODE_CONNECTOR_DVII,
	DRM_MODE_CONNECTOR_DVID,
	DRM_MODE_CONNECTOR_DVIA,
	DRM_MODE_CONNECTOR_Composite,
	DRM_MODE_CONNECTOR_SVIDEO,
	DRM_MODE_CONNECTOR_LVDS,
	DRM_MODE_CONNECTOR_Component,
	DRM_MODE_CONNECTOR_9PinDIN,
	DRM_MODE_CONNECTOR_DisplayPort,
	DRM_MODE_CONNECTOR_HDMIA,
	DRM_MODE_CONNECTOR_HDMIB,
	DRM_MODE_CONNECTOR_TV,
	DRM_MODE_CONNECTOR_eDP,
	DRM_MODE_CONNECTOR_VIRTUAL,
	DRM_MODE_CONNECTOR_DSI,
	DRM_MODE_CONNECTOR_DPI,
	DRM_MODE_CONNECTOR_WRITEBACK,
	DRM_MODE_CONNECTOR_SPI,
	DRM_MODE_CONNECTOR_USB,
};

static void drm_connector_init_type_desc(const unsigned int *type, char *desc)
{
	sprintf(desc, "%s", drm_get_connector_type_name(*type));
}

KUNIT_ARRAY_PARAM(drm_connector_init_type_valid,
		  drm_connector_init_type_valid_tests,
		  drm_connector_init_type_desc);

static struct kunit_case drmm_connector_init_tests[] = {
	KUNIT_CASE(drm_test_drmm_connector_init),
	KUNIT_CASE(drm_test_drmm_connector_init_null_ddc),
	KUNIT_CASE_PARAM(drm_test_drmm_connector_init_type_valid,
			 drm_connector_init_type_valid_gen_params),
	{ }
};

static struct kunit_suite drmm_connector_init_test_suite = {
	.name = "drmm_connector_init",
	.init = drm_test_connector_init,
	.test_cases = drmm_connector_init_tests,
};

struct drm_get_tv_mode_from_name_test {
	const char *name;
	enum drm_connector_tv_mode expected_mode;
};

#define TV_MODE_NAME(_name, _mode)		\
	{					\
		.name = _name,			\
		.expected_mode = _mode,		\
	}

static void drm_test_get_tv_mode_from_name_valid(struct kunit *test)
{
	const struct drm_get_tv_mode_from_name_test *params = test->param_value;

	KUNIT_EXPECT_EQ(test,
			drm_get_tv_mode_from_name(params->name, strlen(params->name)),
			params->expected_mode);
}

static const
struct drm_get_tv_mode_from_name_test drm_get_tv_mode_from_name_valid_tests[] = {
	TV_MODE_NAME("NTSC", DRM_MODE_TV_MODE_NTSC),
	TV_MODE_NAME("NTSC-443", DRM_MODE_TV_MODE_NTSC_443),
	TV_MODE_NAME("NTSC-J", DRM_MODE_TV_MODE_NTSC_J),
	TV_MODE_NAME("PAL", DRM_MODE_TV_MODE_PAL),
	TV_MODE_NAME("PAL-M", DRM_MODE_TV_MODE_PAL_M),
	TV_MODE_NAME("PAL-N", DRM_MODE_TV_MODE_PAL_N),
	TV_MODE_NAME("SECAM", DRM_MODE_TV_MODE_SECAM),
};

static void
drm_get_tv_mode_from_name_valid_desc(const struct drm_get_tv_mode_from_name_test *t,
				     char *desc)
{
	sprintf(desc, "%s", t->name);
}

KUNIT_ARRAY_PARAM(drm_get_tv_mode_from_name_valid,
		  drm_get_tv_mode_from_name_valid_tests,
		  drm_get_tv_mode_from_name_valid_desc);

static void drm_test_get_tv_mode_from_name_truncated(struct kunit *test)
{
	const char *name = "NTS";
	int ret;

	ret = drm_get_tv_mode_from_name(name, strlen(name));
	KUNIT_EXPECT_LT(test, ret, 0);
};

static struct kunit_case drm_get_tv_mode_from_name_tests[] = {
	KUNIT_CASE_PARAM(drm_test_get_tv_mode_from_name_valid,
			 drm_get_tv_mode_from_name_valid_gen_params),
	KUNIT_CASE(drm_test_get_tv_mode_from_name_truncated),
	{ }
};

static struct kunit_suite drm_get_tv_mode_from_name_test_suite = {
	.name = "drm_get_tv_mode_from_name",
	.test_cases = drm_get_tv_mode_from_name_tests,
};

kunit_test_suites(
	&drmm_connector_init_test_suite,
	&drm_get_tv_mode_from_name_test_suite
);

MODULE_AUTHOR("Maxime Ripard <maxime@cerno.tech>");
MODULE_LICENSE("GPL");
