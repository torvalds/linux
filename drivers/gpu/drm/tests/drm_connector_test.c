// SPDX-License-Identifier: GPL-2.0
/*
 * Kunit test for drm_modes functions
 */

#include <linux/i2c.h>

#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_drv.h>
#include <drm/drm_edid.h>
#include <drm/drm_kunit_helpers.h>
#include <drm/drm_modes.h>

#include <drm/display/drm_hdmi_helper.h>

#include <kunit/test.h>

#include "../drm_crtc_internal.h"

struct drm_connector_init_priv {
	struct drm_device drm;
	struct drm_connector connector;
	struct i2c_adapter ddc;
};

static const struct drm_connector_hdmi_funcs dummy_hdmi_funcs = {
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

/*
 * Test that the registration of a bog standard connector works as
 * expected and doesn't report any error.
 */
static void drm_test_connector_hdmi_init_valid(struct kunit *test)
{
	struct drm_connector_init_priv *priv = test->priv;
	int ret;

	ret = drmm_connector_hdmi_init(&priv->drm, &priv->connector,
				       "Vendor", "Product",
				       &dummy_funcs,
				       &dummy_hdmi_funcs,
				       DRM_MODE_CONNECTOR_HDMIA,
				       &priv->ddc,
				       BIT(HDMI_COLORSPACE_RGB),
				       8);
	KUNIT_EXPECT_EQ(test, ret, 0);
}

/*
 * Test that the registration of a connector without a DDC adapter
 * doesn't report any error.
 */
static void drm_test_connector_hdmi_init_null_ddc(struct kunit *test)
{
	struct drm_connector_init_priv *priv = test->priv;
	int ret;

	ret = drmm_connector_hdmi_init(&priv->drm, &priv->connector,
				       "Vendor", "Product",
				       &dummy_funcs,
				       &dummy_hdmi_funcs,
				       DRM_MODE_CONNECTOR_HDMIA,
				       NULL,
				       BIT(HDMI_COLORSPACE_RGB),
				       8);
	KUNIT_EXPECT_EQ(test, ret, 0);
}

/*
 * Test that the registration of an HDMI connector with a NULL vendor
 * fails.
 */
static void drm_test_connector_hdmi_init_null_vendor(struct kunit *test)
{
	struct drm_connector_init_priv *priv = test->priv;
	int ret;

	ret = drmm_connector_hdmi_init(&priv->drm, &priv->connector,
				       NULL, "Product",
				       &dummy_funcs,
				       &dummy_hdmi_funcs,
				       DRM_MODE_CONNECTOR_HDMIA,
				       &priv->ddc,
				       BIT(HDMI_COLORSPACE_RGB),
				       8);
	KUNIT_EXPECT_LT(test, ret, 0);
}

/*
 * Test that the registration of an HDMI connector with a NULL product
 * fails.
 */
static void drm_test_connector_hdmi_init_null_product(struct kunit *test)
{
	struct drm_connector_init_priv *priv = test->priv;
	int ret;

	ret = drmm_connector_hdmi_init(&priv->drm, &priv->connector,
				       "Vendor", NULL,
				       &dummy_funcs,
				       &dummy_hdmi_funcs,
				       DRM_MODE_CONNECTOR_HDMIA,
				       &priv->ddc,
				       BIT(HDMI_COLORSPACE_RGB),
				       8);
	KUNIT_EXPECT_LT(test, ret, 0);
}

/*
 * Test that the registration of a connector with a valid, shorter than
 * the max length, product name succeeds, and is stored padded with 0.
 */
static void drm_test_connector_hdmi_init_product_valid(struct kunit *test)
{
	struct drm_connector_init_priv *priv = test->priv;
	const unsigned char expected_product[DRM_CONNECTOR_HDMI_PRODUCT_LEN] = {
		'P', 'r', 'o', 'd',
	};
	const char *product_name = "Prod";
	int ret;

	KUNIT_ASSERT_LT(test, strlen(product_name), DRM_CONNECTOR_HDMI_PRODUCT_LEN);

	ret = drmm_connector_hdmi_init(&priv->drm, &priv->connector,
				       "Vendor", product_name,
				       &dummy_funcs,
				       &dummy_hdmi_funcs,
				       DRM_MODE_CONNECTOR_HDMIA,
				       &priv->ddc,
				       BIT(HDMI_COLORSPACE_RGB),
				       8);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_MEMEQ(test,
			   priv->connector.hdmi.product,
			   expected_product,
			   sizeof(priv->connector.hdmi.product));
}

/*
 * Test that the registration of a connector with a valid, at max
 * length, product name succeeds, and is stored padded without any
 * trailing \0.
 */
static void drm_test_connector_hdmi_init_product_length_exact(struct kunit *test)
{
	struct drm_connector_init_priv *priv = test->priv;
	const unsigned char expected_product[DRM_CONNECTOR_HDMI_PRODUCT_LEN] = {
		'P', 'r', 'o', 'd', 'u', 'c', 't',
		'P', 'r', 'o', 'd', 'u', 'c', 't',
		'P', 'r',
	};
	const char *product_name = "ProductProductPr";
	int ret;

	KUNIT_ASSERT_EQ(test, strlen(product_name), DRM_CONNECTOR_HDMI_PRODUCT_LEN);

	ret = drmm_connector_hdmi_init(&priv->drm, &priv->connector,
				       "Vendor", product_name,
				       &dummy_funcs,
				       &dummy_hdmi_funcs,
				       DRM_MODE_CONNECTOR_HDMIA,
				       &priv->ddc,
				       BIT(HDMI_COLORSPACE_RGB),
				       8);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_MEMEQ(test,
			   priv->connector.hdmi.product,
			   expected_product,
			   sizeof(priv->connector.hdmi.product));
}

/*
 * Test that the registration of a connector with a product name larger
 * than the maximum length fails.
 */
static void drm_test_connector_hdmi_init_product_length_too_long(struct kunit *test)
{
	struct drm_connector_init_priv *priv = test->priv;
	const char *product_name = "ProductProductProduct";
	int ret;

	KUNIT_ASSERT_GT(test, strlen(product_name), DRM_CONNECTOR_HDMI_PRODUCT_LEN);

	ret = drmm_connector_hdmi_init(&priv->drm, &priv->connector,
				       "Vendor", product_name,
				       &dummy_funcs,
				       &dummy_hdmi_funcs,
				       DRM_MODE_CONNECTOR_HDMIA,
				       &priv->ddc,
				       BIT(HDMI_COLORSPACE_RGB),
				       8);
	KUNIT_EXPECT_LT(test, ret, 0);
}

/*
 * Test that the registration of a connector with a vendor name smaller
 * than the maximum length succeeds, and is stored padded with zeros.
 */
static void drm_test_connector_hdmi_init_vendor_valid(struct kunit *test)
{
	struct drm_connector_init_priv *priv = test->priv;
	const char expected_vendor[DRM_CONNECTOR_HDMI_VENDOR_LEN] = {
		'V', 'e', 'n', 'd',
	};
	const char *vendor_name = "Vend";
	int ret;

	KUNIT_ASSERT_LT(test, strlen(vendor_name), DRM_CONNECTOR_HDMI_VENDOR_LEN);

	ret = drmm_connector_hdmi_init(&priv->drm, &priv->connector,
				       vendor_name, "Product",
				       &dummy_funcs,
				       &dummy_hdmi_funcs,
				       DRM_MODE_CONNECTOR_HDMIA,
				       &priv->ddc,
				       BIT(HDMI_COLORSPACE_RGB),
				       8);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_MEMEQ(test,
			   priv->connector.hdmi.vendor,
			   expected_vendor,
			   sizeof(priv->connector.hdmi.vendor));
}

/*
 * Test that the registration of a connector with a vendor name at the
 * maximum length succeeds, and is stored padded without the trailing
 * zero.
 */
static void drm_test_connector_hdmi_init_vendor_length_exact(struct kunit *test)
{
	struct drm_connector_init_priv *priv = test->priv;
	const char expected_vendor[DRM_CONNECTOR_HDMI_VENDOR_LEN] = {
		'V', 'e', 'n', 'd', 'o', 'r',
		'V', 'e',
	};
	const char *vendor_name = "VendorVe";
	int ret;

	KUNIT_ASSERT_EQ(test, strlen(vendor_name), DRM_CONNECTOR_HDMI_VENDOR_LEN);

	ret = drmm_connector_hdmi_init(&priv->drm, &priv->connector,
				       vendor_name, "Product",
				       &dummy_funcs,
				       &dummy_hdmi_funcs,
				       DRM_MODE_CONNECTOR_HDMIA,
				       &priv->ddc,
				       BIT(HDMI_COLORSPACE_RGB),
				       8);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_MEMEQ(test,
			   priv->connector.hdmi.vendor,
			   expected_vendor,
			   sizeof(priv->connector.hdmi.vendor));
}

/*
 * Test that the registration of a connector with a vendor name larger
 * than the maximum length fails.
 */
static void drm_test_connector_hdmi_init_vendor_length_too_long(struct kunit *test)
{
	struct drm_connector_init_priv *priv = test->priv;
	const char *vendor_name = "VendorVendor";
	int ret;

	KUNIT_ASSERT_GT(test, strlen(vendor_name), DRM_CONNECTOR_HDMI_VENDOR_LEN);

	ret = drmm_connector_hdmi_init(&priv->drm, &priv->connector,
				       vendor_name, "Product",
				       &dummy_funcs,
				       &dummy_hdmi_funcs,
				       DRM_MODE_CONNECTOR_HDMIA,
				       &priv->ddc,
				       BIT(HDMI_COLORSPACE_RGB),
				       8);
	KUNIT_EXPECT_LT(test, ret, 0);
}

/*
 * Test that the registration of a connector with an invalid maximum bpc
 * count fails.
 */
static void drm_test_connector_hdmi_init_bpc_invalid(struct kunit *test)
{
	struct drm_connector_init_priv *priv = test->priv;
	int ret;

	ret = drmm_connector_hdmi_init(&priv->drm, &priv->connector,
				       "Vendor", "Product",
				       &dummy_funcs,
				       &dummy_hdmi_funcs,
				       DRM_MODE_CONNECTOR_HDMIA,
				       &priv->ddc,
				       BIT(HDMI_COLORSPACE_RGB),
				       9);
	KUNIT_EXPECT_LT(test, ret, 0);
}

/*
 * Test that the registration of a connector with a null maximum bpc
 * count fails.
 */
static void drm_test_connector_hdmi_init_bpc_null(struct kunit *test)
{
	struct drm_connector_init_priv *priv = test->priv;
	int ret;

	ret = drmm_connector_hdmi_init(&priv->drm, &priv->connector,
				       "Vendor", "Product",
				       &dummy_funcs,
				       &dummy_hdmi_funcs,
				       DRM_MODE_CONNECTOR_HDMIA,
				       &priv->ddc,
				       BIT(HDMI_COLORSPACE_RGB),
				       0);
	KUNIT_EXPECT_LT(test, ret, 0);
}

/*
 * Test that the registration of a connector with a maximum bpc count of
 * 8 succeeds, registers the max bpc property, but doesn't register the
 * HDR output metadata one.
 */
static void drm_test_connector_hdmi_init_bpc_8(struct kunit *test)
{
	struct drm_connector_init_priv *priv = test->priv;
	struct drm_connector_state *state;
	struct drm_connector *connector = &priv->connector;
	struct drm_property *prop;
	uint64_t val;
	int ret;

	ret = drmm_connector_hdmi_init(&priv->drm, connector,
				       "Vendor", "Product",
				       &dummy_funcs,
				       &dummy_hdmi_funcs,
				       DRM_MODE_CONNECTOR_HDMIA,
				       &priv->ddc,
				       BIT(HDMI_COLORSPACE_RGB),
				       8);
	KUNIT_EXPECT_EQ(test, ret, 0);

	prop = connector->max_bpc_property;
	KUNIT_ASSERT_NOT_NULL(test, prop);
	KUNIT_EXPECT_NOT_NULL(test, drm_mode_obj_find_prop_id(&connector->base, prop->base.id));

	ret = drm_object_property_get_default_value(&connector->base, prop, &val);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, val, 8);

	state = connector->state;
	KUNIT_EXPECT_EQ(test, state->max_bpc, 8);
	KUNIT_EXPECT_EQ(test, state->max_requested_bpc, 8);

	prop = priv->drm.mode_config.hdr_output_metadata_property;
	KUNIT_ASSERT_NOT_NULL(test, prop);
	KUNIT_EXPECT_NULL(test, drm_mode_obj_find_prop_id(&connector->base, prop->base.id));
}

/*
 * Test that the registration of a connector with a maximum bpc count of
 * 10 succeeds and registers the max bpc and HDR output metadata
 * properties.
 */
static void drm_test_connector_hdmi_init_bpc_10(struct kunit *test)
{
	struct drm_connector_init_priv *priv = test->priv;
	struct drm_connector_state *state;
	struct drm_connector *connector = &priv->connector;
	struct drm_property *prop;
	uint64_t val;
	int ret;

	ret = drmm_connector_hdmi_init(&priv->drm, connector,
				       "Vendor", "Product",
				       &dummy_funcs,
				       &dummy_hdmi_funcs,
				       DRM_MODE_CONNECTOR_HDMIA,
				       &priv->ddc,
				       BIT(HDMI_COLORSPACE_RGB),
				       10);
	KUNIT_EXPECT_EQ(test, ret, 0);

	prop = connector->max_bpc_property;
	KUNIT_ASSERT_NOT_NULL(test, prop);
	KUNIT_EXPECT_NOT_NULL(test, drm_mode_obj_find_prop_id(&connector->base, prop->base.id));

	ret = drm_object_property_get_default_value(&connector->base, prop, &val);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, val, 10);

	state = connector->state;
	KUNIT_EXPECT_EQ(test, state->max_bpc, 10);
	KUNIT_EXPECT_EQ(test, state->max_requested_bpc, 10);

	prop = priv->drm.mode_config.hdr_output_metadata_property;
	KUNIT_ASSERT_NOT_NULL(test, prop);
	KUNIT_EXPECT_NOT_NULL(test, drm_mode_obj_find_prop_id(&connector->base, prop->base.id));
}

/*
 * Test that the registration of a connector with a maximum bpc count of
 * 12 succeeds and registers the max bpc and HDR output metadata
 * properties.
 */
static void drm_test_connector_hdmi_init_bpc_12(struct kunit *test)
{
	struct drm_connector_init_priv *priv = test->priv;
	struct drm_connector_state *state;
	struct drm_connector *connector = &priv->connector;
	struct drm_property *prop;
	uint64_t val;
	int ret;

	ret = drmm_connector_hdmi_init(&priv->drm, connector,
				       "Vendor", "Product",
				       &dummy_funcs,
				       &dummy_hdmi_funcs,
				       DRM_MODE_CONNECTOR_HDMIA,
				       &priv->ddc,
				       BIT(HDMI_COLORSPACE_RGB),
				       12);
	KUNIT_EXPECT_EQ(test, ret, 0);

	prop = connector->max_bpc_property;
	KUNIT_ASSERT_NOT_NULL(test, prop);
	KUNIT_EXPECT_NOT_NULL(test, drm_mode_obj_find_prop_id(&connector->base, prop->base.id));

	ret = drm_object_property_get_default_value(&connector->base, prop, &val);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, val, 12);

	state = connector->state;
	KUNIT_EXPECT_EQ(test, state->max_bpc, 12);
	KUNIT_EXPECT_EQ(test, state->max_requested_bpc, 12);

	prop = priv->drm.mode_config.hdr_output_metadata_property;
	KUNIT_ASSERT_NOT_NULL(test, prop);
	KUNIT_EXPECT_NOT_NULL(test, drm_mode_obj_find_prop_id(&connector->base, prop->base.id));
}

/*
 * Test that the registration of an HDMI connector with no supported
 * format fails.
 */
static void drm_test_connector_hdmi_init_formats_empty(struct kunit *test)
{
	struct drm_connector_init_priv *priv = test->priv;
	int ret;

	ret = drmm_connector_hdmi_init(&priv->drm, &priv->connector,
				       "Vendor", "Product",
				       &dummy_funcs,
				       &dummy_hdmi_funcs,
				       DRM_MODE_CONNECTOR_HDMIA,
				       &priv->ddc,
				       0,
				       8);
	KUNIT_EXPECT_LT(test, ret, 0);
}

/*
 * Test that the registration of an HDMI connector not listing RGB as a
 * supported format fails.
 */
static void drm_test_connector_hdmi_init_formats_no_rgb(struct kunit *test)
{
	struct drm_connector_init_priv *priv = test->priv;
	int ret;

	ret = drmm_connector_hdmi_init(&priv->drm, &priv->connector,
				       "Vendor", "Product",
				       &dummy_funcs,
				       &dummy_hdmi_funcs,
				       DRM_MODE_CONNECTOR_HDMIA,
				       &priv->ddc,
				       BIT(HDMI_COLORSPACE_YUV422),
				       8);
	KUNIT_EXPECT_LT(test, ret, 0);
}

/*
 * Test that the registration of an HDMI connector with an HDMI
 * connector type succeeds.
 */
static void drm_test_connector_hdmi_init_type_valid(struct kunit *test)
{
	struct drm_connector_init_priv *priv = test->priv;
	unsigned int connector_type = *(unsigned int *)test->param_value;
	int ret;

	ret = drmm_connector_hdmi_init(&priv->drm, &priv->connector,
				       "Vendor", "Product",
				       &dummy_funcs,
				       &dummy_hdmi_funcs,
				       connector_type,
				       &priv->ddc,
				       BIT(HDMI_COLORSPACE_RGB),
				       8);
	KUNIT_EXPECT_EQ(test, ret, 0);
}

static const unsigned int drm_connector_hdmi_init_type_valid_tests[] = {
	DRM_MODE_CONNECTOR_HDMIA,
	DRM_MODE_CONNECTOR_HDMIB,
};

static void drm_connector_hdmi_init_type_desc(const unsigned int *type, char *desc)
{
	sprintf(desc, "%s", drm_get_connector_type_name(*type));
}

KUNIT_ARRAY_PARAM(drm_connector_hdmi_init_type_valid,
		  drm_connector_hdmi_init_type_valid_tests,
		  drm_connector_hdmi_init_type_desc);

/*
 * Test that the registration of an HDMI connector with an !HDMI
 * connector type fails.
 */
static void drm_test_connector_hdmi_init_type_invalid(struct kunit *test)
{
	struct drm_connector_init_priv *priv = test->priv;
	unsigned int connector_type = *(unsigned int *)test->param_value;
	int ret;

	ret = drmm_connector_hdmi_init(&priv->drm, &priv->connector,
				       "Vendor", "Product",
				       &dummy_funcs,
				       &dummy_hdmi_funcs,
				       connector_type,
				       &priv->ddc,
				       BIT(HDMI_COLORSPACE_RGB),
				       8);
	KUNIT_EXPECT_LT(test, ret, 0);
}

static const unsigned int drm_connector_hdmi_init_type_invalid_tests[] = {
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
	DRM_MODE_CONNECTOR_TV,
	DRM_MODE_CONNECTOR_eDP,
	DRM_MODE_CONNECTOR_VIRTUAL,
	DRM_MODE_CONNECTOR_DSI,
	DRM_MODE_CONNECTOR_DPI,
	DRM_MODE_CONNECTOR_WRITEBACK,
	DRM_MODE_CONNECTOR_SPI,
	DRM_MODE_CONNECTOR_USB,
};

KUNIT_ARRAY_PARAM(drm_connector_hdmi_init_type_invalid,
		  drm_connector_hdmi_init_type_invalid_tests,
		  drm_connector_hdmi_init_type_desc);

static struct kunit_case drmm_connector_hdmi_init_tests[] = {
	KUNIT_CASE(drm_test_connector_hdmi_init_valid),
	KUNIT_CASE(drm_test_connector_hdmi_init_bpc_8),
	KUNIT_CASE(drm_test_connector_hdmi_init_bpc_10),
	KUNIT_CASE(drm_test_connector_hdmi_init_bpc_12),
	KUNIT_CASE(drm_test_connector_hdmi_init_bpc_invalid),
	KUNIT_CASE(drm_test_connector_hdmi_init_bpc_null),
	KUNIT_CASE(drm_test_connector_hdmi_init_formats_empty),
	KUNIT_CASE(drm_test_connector_hdmi_init_formats_no_rgb),
	KUNIT_CASE(drm_test_connector_hdmi_init_null_ddc),
	KUNIT_CASE(drm_test_connector_hdmi_init_null_product),
	KUNIT_CASE(drm_test_connector_hdmi_init_null_vendor),
	KUNIT_CASE(drm_test_connector_hdmi_init_product_length_exact),
	KUNIT_CASE(drm_test_connector_hdmi_init_product_length_too_long),
	KUNIT_CASE(drm_test_connector_hdmi_init_product_valid),
	KUNIT_CASE(drm_test_connector_hdmi_init_vendor_length_exact),
	KUNIT_CASE(drm_test_connector_hdmi_init_vendor_length_too_long),
	KUNIT_CASE(drm_test_connector_hdmi_init_vendor_valid),
	KUNIT_CASE_PARAM(drm_test_connector_hdmi_init_type_valid,
			 drm_connector_hdmi_init_type_valid_gen_params),
	KUNIT_CASE_PARAM(drm_test_connector_hdmi_init_type_invalid,
			 drm_connector_hdmi_init_type_invalid_gen_params),
	{ }
};

static struct kunit_suite drmm_connector_hdmi_init_test_suite = {
	.name = "drmm_connector_hdmi_init",
	.init = drm_test_connector_init,
	.test_cases = drmm_connector_hdmi_init_tests,
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
	TV_MODE_NAME("Mono", DRM_MODE_TV_MODE_MONOCHROME),
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

struct drm_hdmi_connector_get_broadcast_rgb_name_test {
	unsigned int kind;
	const char *expected_name;
};

#define BROADCAST_RGB_TEST(_kind, _name)	\
	{					\
		.kind = _kind,			\
		.expected_name = _name,		\
	}

static void drm_test_drm_hdmi_connector_get_broadcast_rgb_name(struct kunit *test)
{
	const struct drm_hdmi_connector_get_broadcast_rgb_name_test *params =
		test->param_value;

	KUNIT_EXPECT_STREQ(test,
			   drm_hdmi_connector_get_broadcast_rgb_name(params->kind),
			   params->expected_name);
}

static const
struct drm_hdmi_connector_get_broadcast_rgb_name_test
drm_hdmi_connector_get_broadcast_rgb_name_valid_tests[] = {
	BROADCAST_RGB_TEST(DRM_HDMI_BROADCAST_RGB_AUTO, "Automatic"),
	BROADCAST_RGB_TEST(DRM_HDMI_BROADCAST_RGB_FULL, "Full"),
	BROADCAST_RGB_TEST(DRM_HDMI_BROADCAST_RGB_LIMITED, "Limited 16:235"),
};

static void
drm_hdmi_connector_get_broadcast_rgb_name_valid_desc(const struct drm_hdmi_connector_get_broadcast_rgb_name_test *t,
						     char *desc)
{
	sprintf(desc, "%s", t->expected_name);
}

KUNIT_ARRAY_PARAM(drm_hdmi_connector_get_broadcast_rgb_name_valid,
		  drm_hdmi_connector_get_broadcast_rgb_name_valid_tests,
		  drm_hdmi_connector_get_broadcast_rgb_name_valid_desc);

static void drm_test_drm_hdmi_connector_get_broadcast_rgb_name_invalid(struct kunit *test)
{
	KUNIT_EXPECT_NULL(test, drm_hdmi_connector_get_broadcast_rgb_name(3));
};

static struct kunit_case drm_hdmi_connector_get_broadcast_rgb_name_tests[] = {
	KUNIT_CASE_PARAM(drm_test_drm_hdmi_connector_get_broadcast_rgb_name,
			 drm_hdmi_connector_get_broadcast_rgb_name_valid_gen_params),
	KUNIT_CASE(drm_test_drm_hdmi_connector_get_broadcast_rgb_name_invalid),
	{ }
};

static struct kunit_suite drm_hdmi_connector_get_broadcast_rgb_name_test_suite = {
	.name = "drm_hdmi_connector_get_broadcast_rgb_name",
	.test_cases = drm_hdmi_connector_get_broadcast_rgb_name_tests,
};

struct drm_hdmi_connector_get_output_format_name_test {
	unsigned int kind;
	const char *expected_name;
};

#define OUTPUT_FORMAT_TEST(_kind, _name)	\
	{					\
		.kind = _kind,			\
		.expected_name = _name,		\
	}

static void drm_test_drm_hdmi_connector_get_output_format_name(struct kunit *test)
{
	const struct drm_hdmi_connector_get_output_format_name_test *params =
		test->param_value;

	KUNIT_EXPECT_STREQ(test,
			   drm_hdmi_connector_get_output_format_name(params->kind),
			   params->expected_name);
}

static const
struct drm_hdmi_connector_get_output_format_name_test
drm_hdmi_connector_get_output_format_name_valid_tests[] = {
	OUTPUT_FORMAT_TEST(HDMI_COLORSPACE_RGB, "RGB"),
	OUTPUT_FORMAT_TEST(HDMI_COLORSPACE_YUV420, "YUV 4:2:0"),
	OUTPUT_FORMAT_TEST(HDMI_COLORSPACE_YUV422, "YUV 4:2:2"),
	OUTPUT_FORMAT_TEST(HDMI_COLORSPACE_YUV444, "YUV 4:4:4"),
};

static void
drm_hdmi_connector_get_output_format_name_valid_desc(const struct drm_hdmi_connector_get_output_format_name_test *t,
						     char *desc)
{
	sprintf(desc, "%s", t->expected_name);
}

KUNIT_ARRAY_PARAM(drm_hdmi_connector_get_output_format_name_valid,
		  drm_hdmi_connector_get_output_format_name_valid_tests,
		  drm_hdmi_connector_get_output_format_name_valid_desc);

static void drm_test_drm_hdmi_connector_get_output_format_name_invalid(struct kunit *test)
{
	KUNIT_EXPECT_NULL(test, drm_hdmi_connector_get_output_format_name(4));
};

static struct kunit_case drm_hdmi_connector_get_output_format_name_tests[] = {
	KUNIT_CASE_PARAM(drm_test_drm_hdmi_connector_get_output_format_name,
			 drm_hdmi_connector_get_output_format_name_valid_gen_params),
	KUNIT_CASE(drm_test_drm_hdmi_connector_get_output_format_name_invalid),
	{ }
};

static struct kunit_suite drm_hdmi_connector_get_output_format_name_test_suite = {
	.name = "drm_hdmi_connector_get_output_format_name",
	.test_cases = drm_hdmi_connector_get_output_format_name_tests,
};

static void drm_test_drm_connector_attach_broadcast_rgb_property(struct kunit *test)
{
	struct drm_connector_init_priv *priv = test->priv;
	struct drm_connector *connector = &priv->connector;
	struct drm_property *prop;
	int ret;

	ret = drmm_connector_init(&priv->drm, connector,
				  &dummy_funcs,
				  DRM_MODE_CONNECTOR_HDMIA,
				  &priv->ddc);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = drm_connector_attach_broadcast_rgb_property(connector);
	KUNIT_ASSERT_EQ(test, ret, 0);

	prop = connector->broadcast_rgb_property;
	KUNIT_ASSERT_NOT_NULL(test, prop);
	KUNIT_EXPECT_NOT_NULL(test, drm_mode_obj_find_prop_id(&connector->base, prop->base.id));
}

static void drm_test_drm_connector_attach_broadcast_rgb_property_hdmi_connector(struct kunit *test)
{
	struct drm_connector_init_priv *priv = test->priv;
	struct drm_connector *connector = &priv->connector;
	struct drm_property *prop;
	int ret;

	ret = drmm_connector_hdmi_init(&priv->drm, connector,
				       "Vendor", "Product",
				       &dummy_funcs,
				       &dummy_hdmi_funcs,
				       DRM_MODE_CONNECTOR_HDMIA,
				       &priv->ddc,
				       BIT(HDMI_COLORSPACE_RGB),
				       8);
	KUNIT_EXPECT_EQ(test, ret, 0);

	ret = drm_connector_attach_broadcast_rgb_property(connector);
	KUNIT_ASSERT_EQ(test, ret, 0);

	prop = connector->broadcast_rgb_property;
	KUNIT_ASSERT_NOT_NULL(test, prop);
	KUNIT_EXPECT_NOT_NULL(test, drm_mode_obj_find_prop_id(&connector->base, prop->base.id));
}

static struct kunit_case drm_connector_attach_broadcast_rgb_property_tests[] = {
	KUNIT_CASE(drm_test_drm_connector_attach_broadcast_rgb_property),
	KUNIT_CASE(drm_test_drm_connector_attach_broadcast_rgb_property_hdmi_connector),
	{ }
};

static struct kunit_suite drm_connector_attach_broadcast_rgb_property_test_suite = {
	.name = "drm_connector_attach_broadcast_rgb_property",
	.init = drm_test_connector_init,
	.test_cases = drm_connector_attach_broadcast_rgb_property_tests,
};

/*
 * Test that for a given mode, with 8bpc and an RGB output the TMDS
 * character rate is equal to the mode pixel clock.
 */
static void drm_test_drm_hdmi_compute_mode_clock_rgb(struct kunit *test)
{
	struct drm_connector_init_priv *priv = test->priv;
	const struct drm_display_mode *mode;
	unsigned long long rate;
	struct drm_device *drm = &priv->drm;

	mode = drm_kunit_display_mode_from_cea_vic(test, drm, 16);
	KUNIT_ASSERT_NOT_NULL(test, mode);

	KUNIT_ASSERT_FALSE(test, mode->flags & DRM_MODE_FLAG_DBLCLK);

	rate = drm_hdmi_compute_mode_clock(mode, 8, HDMI_COLORSPACE_RGB);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_EQ(test, mode->clock * 1000ULL, rate);
}

/*
 * Test that for a given mode, with 10bpc and an RGB output the TMDS
 * character rate is equal to 1.25 times the mode pixel clock.
 */
static void drm_test_drm_hdmi_compute_mode_clock_rgb_10bpc(struct kunit *test)
{
	struct drm_connector_init_priv *priv = test->priv;
	const struct drm_display_mode *mode;
	unsigned long long rate;
	struct drm_device *drm = &priv->drm;

	mode = drm_kunit_display_mode_from_cea_vic(test, drm, 16);
	KUNIT_ASSERT_NOT_NULL(test, mode);

	KUNIT_ASSERT_FALSE(test, mode->flags & DRM_MODE_FLAG_DBLCLK);

	rate = drm_hdmi_compute_mode_clock(mode, 10, HDMI_COLORSPACE_RGB);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_EQ(test, mode->clock * 1250, rate);
}

/*
 * Test that for the VIC-1 mode, with 10bpc and an RGB output the TMDS
 * character rate computation fails.
 */
static void drm_test_drm_hdmi_compute_mode_clock_rgb_10bpc_vic_1(struct kunit *test)
{
	struct drm_connector_init_priv *priv = test->priv;
	const struct drm_display_mode *mode;
	unsigned long long rate;
	struct drm_device *drm = &priv->drm;

	mode = drm_kunit_display_mode_from_cea_vic(test, drm, 1);
	KUNIT_ASSERT_NOT_NULL(test, mode);

	rate = drm_hdmi_compute_mode_clock(mode, 10, HDMI_COLORSPACE_RGB);
	KUNIT_EXPECT_EQ(test, rate, 0);
}

/*
 * Test that for a given mode, with 12bpc and an RGB output the TMDS
 * character rate is equal to 1.5 times the mode pixel clock.
 */
static void drm_test_drm_hdmi_compute_mode_clock_rgb_12bpc(struct kunit *test)
{
	struct drm_connector_init_priv *priv = test->priv;
	const struct drm_display_mode *mode;
	unsigned long long rate;
	struct drm_device *drm = &priv->drm;

	mode = drm_kunit_display_mode_from_cea_vic(test, drm, 16);
	KUNIT_ASSERT_NOT_NULL(test, mode);

	KUNIT_ASSERT_FALSE(test, mode->flags & DRM_MODE_FLAG_DBLCLK);

	rate = drm_hdmi_compute_mode_clock(mode, 12, HDMI_COLORSPACE_RGB);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_EQ(test, mode->clock * 1500, rate);
}

/*
 * Test that for the VIC-1 mode, with 12bpc and an RGB output the TMDS
 * character rate computation fails.
 */
static void drm_test_drm_hdmi_compute_mode_clock_rgb_12bpc_vic_1(struct kunit *test)
{
	struct drm_connector_init_priv *priv = test->priv;
	const struct drm_display_mode *mode;
	unsigned long long rate;
	struct drm_device *drm = &priv->drm;

	mode = drm_kunit_display_mode_from_cea_vic(test, drm, 1);
	KUNIT_ASSERT_NOT_NULL(test, mode);

	rate = drm_hdmi_compute_mode_clock(mode, 12, HDMI_COLORSPACE_RGB);
	KUNIT_EXPECT_EQ(test, rate, 0);
}

/*
 * Test that for a mode with the pixel repetition flag, the TMDS
 * character rate is indeed double the mode pixel clock.
 */
static void drm_test_drm_hdmi_compute_mode_clock_rgb_double(struct kunit *test)
{
	struct drm_connector_init_priv *priv = test->priv;
	const struct drm_display_mode *mode;
	unsigned long long rate;
	struct drm_device *drm = &priv->drm;

	mode = drm_kunit_display_mode_from_cea_vic(test, drm, 6);
	KUNIT_ASSERT_NOT_NULL(test, mode);

	KUNIT_ASSERT_TRUE(test, mode->flags & DRM_MODE_FLAG_DBLCLK);

	rate = drm_hdmi_compute_mode_clock(mode, 8, HDMI_COLORSPACE_RGB);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_EQ(test, (mode->clock * 1000ULL) * 2, rate);
}

/*
 * Test that the TMDS character rate computation for the VIC modes
 * explicitly listed in the spec as supporting YUV420 succeed and return
 * half the mode pixel clock.
 */
static void drm_test_connector_hdmi_compute_mode_clock_yuv420_valid(struct kunit *test)
{
	struct drm_connector_init_priv *priv = test->priv;
	const struct drm_display_mode *mode;
	struct drm_device *drm = &priv->drm;
	unsigned long long rate;
	unsigned int vic = *(unsigned int *)test->param_value;

	mode = drm_kunit_display_mode_from_cea_vic(test, drm, vic);
	KUNIT_ASSERT_NOT_NULL(test, mode);

	KUNIT_ASSERT_FALSE(test, mode->flags & DRM_MODE_FLAG_DBLCLK);

	rate = drm_hdmi_compute_mode_clock(mode, 8, HDMI_COLORSPACE_YUV420);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_EQ(test, (mode->clock * 1000ULL) / 2, rate);
}

static const unsigned int drm_hdmi_compute_mode_clock_yuv420_vic_valid_tests[] = {
	96, 97, 101, 102, 106, 107,
};

static void drm_hdmi_compute_mode_clock_yuv420_vic_desc(const unsigned int *vic, char *desc)
{
	sprintf(desc, "VIC %u", *vic);
}

KUNIT_ARRAY_PARAM(drm_hdmi_compute_mode_clock_yuv420_valid,
		  drm_hdmi_compute_mode_clock_yuv420_vic_valid_tests,
		  drm_hdmi_compute_mode_clock_yuv420_vic_desc);

/*
 * Test that for a given mode listed supporting it and an YUV420 output
 * with 10bpc, the TMDS character rate is equal to 0.625 times the mode
 * pixel clock.
 */
static void drm_test_connector_hdmi_compute_mode_clock_yuv420_10_bpc(struct kunit *test)
{
	struct drm_connector_init_priv *priv = test->priv;
	const struct drm_display_mode *mode;
	struct drm_device *drm = &priv->drm;
	unsigned int vic =
		drm_hdmi_compute_mode_clock_yuv420_vic_valid_tests[0];
	unsigned long long rate;

	mode = drm_kunit_display_mode_from_cea_vic(test, drm, vic);
	KUNIT_ASSERT_NOT_NULL(test, mode);

	KUNIT_ASSERT_FALSE(test, mode->flags & DRM_MODE_FLAG_DBLCLK);

	rate = drm_hdmi_compute_mode_clock(mode, 10, HDMI_COLORSPACE_YUV420);
	KUNIT_ASSERT_GT(test, rate, 0);

	KUNIT_EXPECT_EQ(test, mode->clock * 625, rate);
}

/*
 * Test that for a given mode listed supporting it and an YUV420 output
 * with 12bpc, the TMDS character rate is equal to 0.75 times the mode
 * pixel clock.
 */
static void drm_test_connector_hdmi_compute_mode_clock_yuv420_12_bpc(struct kunit *test)
{
	struct drm_connector_init_priv *priv = test->priv;
	const struct drm_display_mode *mode;
	struct drm_device *drm = &priv->drm;
	unsigned int vic =
		drm_hdmi_compute_mode_clock_yuv420_vic_valid_tests[0];
	unsigned long long rate;

	mode = drm_kunit_display_mode_from_cea_vic(test, drm, vic);
	KUNIT_ASSERT_NOT_NULL(test, mode);

	KUNIT_ASSERT_FALSE(test, mode->flags & DRM_MODE_FLAG_DBLCLK);

	rate = drm_hdmi_compute_mode_clock(mode, 12, HDMI_COLORSPACE_YUV420);
	KUNIT_ASSERT_GT(test, rate, 0);

	KUNIT_EXPECT_EQ(test, mode->clock * 750, rate);
}

/*
 * Test that for a given mode, the computation of the TMDS character
 * rate with 8bpc and a YUV422 output succeeds and returns a rate equal
 * to the mode pixel clock.
 */
static void drm_test_connector_hdmi_compute_mode_clock_yuv422_8_bpc(struct kunit *test)
{
	struct drm_connector_init_priv *priv = test->priv;
	const struct drm_display_mode *mode;
	struct drm_device *drm = &priv->drm;
	unsigned long long rate;

	mode = drm_kunit_display_mode_from_cea_vic(test, drm, 16);
	KUNIT_ASSERT_NOT_NULL(test, mode);

	KUNIT_ASSERT_FALSE(test, mode->flags & DRM_MODE_FLAG_DBLCLK);

	rate = drm_hdmi_compute_mode_clock(mode, 8, HDMI_COLORSPACE_YUV422);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_EQ(test, mode->clock * 1000, rate);
}

/*
 * Test that for a given mode, the computation of the TMDS character
 * rate with 10bpc and a YUV422 output succeeds and returns a rate equal
 * to the mode pixel clock.
 */
static void drm_test_connector_hdmi_compute_mode_clock_yuv422_10_bpc(struct kunit *test)
{
	struct drm_connector_init_priv *priv = test->priv;
	const struct drm_display_mode *mode;
	struct drm_device *drm = &priv->drm;
	unsigned long long rate;

	mode = drm_kunit_display_mode_from_cea_vic(test, drm, 16);
	KUNIT_ASSERT_NOT_NULL(test, mode);

	KUNIT_ASSERT_FALSE(test, mode->flags & DRM_MODE_FLAG_DBLCLK);

	rate = drm_hdmi_compute_mode_clock(mode, 10, HDMI_COLORSPACE_YUV422);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_EQ(test, mode->clock * 1000, rate);
}

/*
 * Test that for a given mode, the computation of the TMDS character
 * rate with 12bpc and a YUV422 output succeeds and returns a rate equal
 * to the mode pixel clock.
 */
static void drm_test_connector_hdmi_compute_mode_clock_yuv422_12_bpc(struct kunit *test)
{
	struct drm_connector_init_priv *priv = test->priv;
	const struct drm_display_mode *mode;
	struct drm_device *drm = &priv->drm;
	unsigned long long rate;

	mode = drm_kunit_display_mode_from_cea_vic(test, drm, 16);
	KUNIT_ASSERT_NOT_NULL(test, mode);

	KUNIT_ASSERT_FALSE(test, mode->flags & DRM_MODE_FLAG_DBLCLK);

	rate = drm_hdmi_compute_mode_clock(mode, 12, HDMI_COLORSPACE_YUV422);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_EQ(test, mode->clock * 1000, rate);
}

static struct kunit_case drm_hdmi_compute_mode_clock_tests[] = {
	KUNIT_CASE(drm_test_drm_hdmi_compute_mode_clock_rgb),
	KUNIT_CASE(drm_test_drm_hdmi_compute_mode_clock_rgb_10bpc),
	KUNIT_CASE(drm_test_drm_hdmi_compute_mode_clock_rgb_10bpc_vic_1),
	KUNIT_CASE(drm_test_drm_hdmi_compute_mode_clock_rgb_12bpc),
	KUNIT_CASE(drm_test_drm_hdmi_compute_mode_clock_rgb_12bpc_vic_1),
	KUNIT_CASE(drm_test_drm_hdmi_compute_mode_clock_rgb_double),
	KUNIT_CASE_PARAM(drm_test_connector_hdmi_compute_mode_clock_yuv420_valid,
			 drm_hdmi_compute_mode_clock_yuv420_valid_gen_params),
	KUNIT_CASE(drm_test_connector_hdmi_compute_mode_clock_yuv420_10_bpc),
	KUNIT_CASE(drm_test_connector_hdmi_compute_mode_clock_yuv420_12_bpc),
	KUNIT_CASE(drm_test_connector_hdmi_compute_mode_clock_yuv422_8_bpc),
	KUNIT_CASE(drm_test_connector_hdmi_compute_mode_clock_yuv422_10_bpc),
	KUNIT_CASE(drm_test_connector_hdmi_compute_mode_clock_yuv422_12_bpc),
	{ }
};

static struct kunit_suite drm_hdmi_compute_mode_clock_test_suite = {
	.name = "drm_test_connector_hdmi_compute_mode_clock",
	.init = drm_test_connector_init,
	.test_cases = drm_hdmi_compute_mode_clock_tests,
};

kunit_test_suites(
	&drmm_connector_hdmi_init_test_suite,
	&drmm_connector_init_test_suite,
	&drm_connector_attach_broadcast_rgb_property_test_suite,
	&drm_get_tv_mode_from_name_test_suite,
	&drm_hdmi_compute_mode_clock_test_suite,
	&drm_hdmi_connector_get_broadcast_rgb_name_test_suite,
	&drm_hdmi_connector_get_output_format_name_test_suite
);

MODULE_AUTHOR("Maxime Ripard <maxime@cerno.tech>");
MODULE_DESCRIPTION("Kunit test for drm_modes functions");
MODULE_LICENSE("GPL");
