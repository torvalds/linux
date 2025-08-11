// SPDX-License-Identifier: GPL-2.0+

#include <kunit/test.h>

#include "../vkms_config.h"

MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");

static size_t vkms_config_get_num_planes(struct vkms_config *config)
{
	struct vkms_config_plane *plane_cfg;
	size_t count = 0;

	vkms_config_for_each_plane(config, plane_cfg)
		count++;

	return count;
}

static size_t vkms_config_get_num_encoders(struct vkms_config *config)
{
	struct vkms_config_encoder *encoder_cfg;
	size_t count = 0;

	vkms_config_for_each_encoder(config, encoder_cfg)
		count++;

	return count;
}

static size_t vkms_config_get_num_connectors(struct vkms_config *config)
{
	struct vkms_config_connector *connector_cfg;
	size_t count = 0;

	vkms_config_for_each_connector(config, connector_cfg)
		count++;

	return count;
}

static struct vkms_config_plane *get_first_plane(struct vkms_config *config)
{
	struct vkms_config_plane *plane_cfg;

	vkms_config_for_each_plane(config, plane_cfg)
		return plane_cfg;

	return NULL;
}

static struct vkms_config_crtc *get_first_crtc(struct vkms_config *config)
{
	struct vkms_config_crtc *crtc_cfg;

	vkms_config_for_each_crtc(config, crtc_cfg)
		return crtc_cfg;

	return NULL;
}

static struct vkms_config_encoder *get_first_encoder(struct vkms_config *config)
{
	struct vkms_config_encoder *encoder_cfg;

	vkms_config_for_each_encoder(config, encoder_cfg)
		return encoder_cfg;

	return NULL;
}

static struct vkms_config_connector *get_first_connector(struct vkms_config *config)
{
	struct vkms_config_connector *connector_cfg;

	vkms_config_for_each_connector(config, connector_cfg)
		return connector_cfg;

	return NULL;
}

struct default_config_case {
	bool enable_cursor;
	bool enable_writeback;
	bool enable_overlay;
};

static void vkms_config_test_empty_config(struct kunit *test)
{
	struct vkms_config *config;
	const char *dev_name = "test";

	config = vkms_config_create(dev_name);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, config);

	/* The dev_name string and the config have different lifetimes */
	dev_name = NULL;
	KUNIT_EXPECT_STREQ(test, vkms_config_get_device_name(config), "test");

	KUNIT_EXPECT_EQ(test, vkms_config_get_num_planes(config), 0);
	KUNIT_EXPECT_EQ(test, vkms_config_get_num_crtcs(config), 0);
	KUNIT_EXPECT_EQ(test, vkms_config_get_num_encoders(config), 0);
	KUNIT_EXPECT_EQ(test, vkms_config_get_num_connectors(config), 0);

	KUNIT_EXPECT_FALSE(test, vkms_config_is_valid(config));

	vkms_config_destroy(config);
}

static struct default_config_case default_config_cases[] = {
	{ false, false, false },
	{ true, false, false },
	{ true, true, false },
	{ true, false, true },
	{ false, true, false },
	{ false, true, true },
	{ false, false, true },
	{ true, true, true },
};

KUNIT_ARRAY_PARAM(default_config, default_config_cases, NULL);

static void vkms_config_test_default_config(struct kunit *test)
{
	const struct default_config_case *params = test->param_value;
	struct vkms_config *config;
	struct vkms_config_plane *plane_cfg;
	struct vkms_config_crtc *crtc_cfg;
	int n_primaries = 0;
	int n_cursors = 0;
	int n_overlays = 0;

	config = vkms_config_default_create(params->enable_cursor,
					    params->enable_writeback,
					    params->enable_overlay);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, config);

	/* Planes */
	vkms_config_for_each_plane(config, plane_cfg) {
		switch (vkms_config_plane_get_type(plane_cfg)) {
		case DRM_PLANE_TYPE_PRIMARY:
			n_primaries++;
			break;
		case DRM_PLANE_TYPE_CURSOR:
			n_cursors++;
			break;
		case DRM_PLANE_TYPE_OVERLAY:
			n_overlays++;
			break;
		default:
			KUNIT_FAIL_AND_ABORT(test, "Unknown plane type");
		}
	}
	KUNIT_EXPECT_EQ(test, n_primaries, 1);
	KUNIT_EXPECT_EQ(test, n_cursors, params->enable_cursor ? 1 : 0);
	KUNIT_EXPECT_EQ(test, n_overlays, params->enable_overlay ? 8 : 0);

	/* CRTCs */
	KUNIT_EXPECT_EQ(test, vkms_config_get_num_crtcs(config), 1);

	crtc_cfg = get_first_crtc(config);
	KUNIT_EXPECT_EQ(test, vkms_config_crtc_get_writeback(crtc_cfg),
			params->enable_writeback);

	vkms_config_for_each_plane(config, plane_cfg) {
		struct vkms_config_crtc *possible_crtc;
		int n_possible_crtcs = 0;
		unsigned long idx = 0;

		vkms_config_plane_for_each_possible_crtc(plane_cfg, idx, possible_crtc) {
			KUNIT_EXPECT_PTR_EQ(test, crtc_cfg, possible_crtc);
			n_possible_crtcs++;
		}
		KUNIT_EXPECT_EQ(test, n_possible_crtcs, 1);
	}

	/* Encoders */
	KUNIT_EXPECT_EQ(test, vkms_config_get_num_encoders(config), 1);

	/* Connectors */
	KUNIT_EXPECT_EQ(test, vkms_config_get_num_connectors(config), 1);

	KUNIT_EXPECT_TRUE(test, vkms_config_is_valid(config));

	vkms_config_destroy(config);
}

static void vkms_config_test_get_planes(struct kunit *test)
{
	struct vkms_config *config;
	struct vkms_config_plane *plane_cfg;
	struct vkms_config_plane *plane_cfg1, *plane_cfg2;
	int n_planes = 0;

	config = vkms_config_create("test");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, config);

	vkms_config_for_each_plane(config, plane_cfg)
		n_planes++;
	KUNIT_ASSERT_EQ(test, n_planes, 0);

	plane_cfg1 = vkms_config_create_plane(config);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, plane_cfg1);
	vkms_config_for_each_plane(config, plane_cfg) {
		n_planes++;
		if (plane_cfg != plane_cfg1)
			KUNIT_FAIL(test, "Unexpected plane");
	}
	KUNIT_ASSERT_EQ(test, n_planes, 1);
	n_planes = 0;

	plane_cfg2 = vkms_config_create_plane(config);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, plane_cfg2);
	vkms_config_for_each_plane(config, plane_cfg) {
		n_planes++;
		if (plane_cfg != plane_cfg1 && plane_cfg != plane_cfg2)
			KUNIT_FAIL(test, "Unexpected plane");
	}
	KUNIT_ASSERT_EQ(test, n_planes, 2);
	n_planes = 0;

	vkms_config_destroy_plane(plane_cfg1);
	vkms_config_for_each_plane(config, plane_cfg) {
		n_planes++;
		if (plane_cfg != plane_cfg2)
			KUNIT_FAIL(test, "Unexpected plane");
	}
	KUNIT_ASSERT_EQ(test, n_planes, 1);

	vkms_config_destroy(config);
}

static void vkms_config_test_get_crtcs(struct kunit *test)
{
	struct vkms_config *config;
	struct vkms_config_crtc *crtc_cfg;
	struct vkms_config_crtc *crtc_cfg1, *crtc_cfg2;

	config = vkms_config_create("test");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, config);

	KUNIT_ASSERT_EQ(test, vkms_config_get_num_crtcs(config), 0);
	vkms_config_for_each_crtc(config, crtc_cfg)
		KUNIT_FAIL(test, "Unexpected CRTC");

	crtc_cfg1 = vkms_config_create_crtc(config);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, crtc_cfg1);
	KUNIT_ASSERT_EQ(test, vkms_config_get_num_crtcs(config), 1);
	vkms_config_for_each_crtc(config, crtc_cfg) {
		if (crtc_cfg != crtc_cfg1)
			KUNIT_FAIL(test, "Unexpected CRTC");
	}

	crtc_cfg2 = vkms_config_create_crtc(config);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, crtc_cfg2);
	KUNIT_ASSERT_EQ(test, vkms_config_get_num_crtcs(config), 2);
	vkms_config_for_each_crtc(config, crtc_cfg) {
		if (crtc_cfg != crtc_cfg1 && crtc_cfg != crtc_cfg2)
			KUNIT_FAIL(test, "Unexpected CRTC");
	}

	vkms_config_destroy_crtc(config, crtc_cfg2);
	KUNIT_ASSERT_EQ(test, vkms_config_get_num_crtcs(config), 1);
	vkms_config_for_each_crtc(config, crtc_cfg) {
		if (crtc_cfg != crtc_cfg1)
			KUNIT_FAIL(test, "Unexpected CRTC");
	}

	vkms_config_destroy(config);
}

static void vkms_config_test_get_encoders(struct kunit *test)
{
	struct vkms_config *config;
	struct vkms_config_encoder *encoder_cfg;
	struct vkms_config_encoder *encoder_cfg1, *encoder_cfg2;
	int n_encoders = 0;

	config = vkms_config_create("test");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, config);

	vkms_config_for_each_encoder(config, encoder_cfg)
		n_encoders++;
	KUNIT_ASSERT_EQ(test, n_encoders, 0);

	encoder_cfg1 = vkms_config_create_encoder(config);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, encoder_cfg1);
	vkms_config_for_each_encoder(config, encoder_cfg) {
		n_encoders++;
		if (encoder_cfg != encoder_cfg1)
			KUNIT_FAIL(test, "Unexpected encoder");
	}
	KUNIT_ASSERT_EQ(test, n_encoders, 1);
	n_encoders = 0;

	encoder_cfg2 = vkms_config_create_encoder(config);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, encoder_cfg2);
	vkms_config_for_each_encoder(config, encoder_cfg) {
		n_encoders++;
		if (encoder_cfg != encoder_cfg1 && encoder_cfg != encoder_cfg2)
			KUNIT_FAIL(test, "Unexpected encoder");
	}
	KUNIT_ASSERT_EQ(test, n_encoders, 2);
	n_encoders = 0;

	vkms_config_destroy_encoder(config, encoder_cfg2);
	vkms_config_for_each_encoder(config, encoder_cfg) {
		n_encoders++;
		if (encoder_cfg != encoder_cfg1)
			KUNIT_FAIL(test, "Unexpected encoder");
	}
	KUNIT_ASSERT_EQ(test, n_encoders, 1);
	n_encoders = 0;

	vkms_config_destroy(config);
}

static void vkms_config_test_get_connectors(struct kunit *test)
{
	struct vkms_config *config;
	struct vkms_config_connector *connector_cfg;
	struct vkms_config_connector *connector_cfg1, *connector_cfg2;
	int n_connectors = 0;

	config = vkms_config_create("test");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, config);

	vkms_config_for_each_connector(config, connector_cfg)
		n_connectors++;
	KUNIT_ASSERT_EQ(test, n_connectors, 0);

	connector_cfg1 = vkms_config_create_connector(config);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, connector_cfg1);
	vkms_config_for_each_connector(config, connector_cfg) {
		n_connectors++;
		if (connector_cfg != connector_cfg1)
			KUNIT_FAIL(test, "Unexpected connector");
	}
	KUNIT_ASSERT_EQ(test, n_connectors, 1);
	n_connectors = 0;

	connector_cfg2 = vkms_config_create_connector(config);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, connector_cfg2);
	vkms_config_for_each_connector(config, connector_cfg) {
		n_connectors++;
		if (connector_cfg != connector_cfg1 &&
		    connector_cfg != connector_cfg2)
			KUNIT_FAIL(test, "Unexpected connector");
	}
	KUNIT_ASSERT_EQ(test, n_connectors, 2);
	n_connectors = 0;

	vkms_config_destroy_connector(connector_cfg2);
	vkms_config_for_each_connector(config, connector_cfg) {
		n_connectors++;
		if (connector_cfg != connector_cfg1)
			KUNIT_FAIL(test, "Unexpected connector");
	}
	KUNIT_ASSERT_EQ(test, n_connectors, 1);
	n_connectors = 0;

	vkms_config_destroy(config);
}

static void vkms_config_test_invalid_plane_number(struct kunit *test)
{
	struct vkms_config *config;
	struct vkms_config_plane *plane_cfg;
	int n;

	config = vkms_config_default_create(false, false, false);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, config);

	/* Invalid: No planes */
	plane_cfg = get_first_plane(config);
	vkms_config_destroy_plane(plane_cfg);
	KUNIT_EXPECT_FALSE(test, vkms_config_is_valid(config));

	/* Invalid: Too many planes */
	for (n = 0; n <= 32; n++)
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, vkms_config_create_plane(config));

	KUNIT_EXPECT_FALSE(test, vkms_config_is_valid(config));

	vkms_config_destroy(config);
}

static void vkms_config_test_valid_plane_type(struct kunit *test)
{
	struct vkms_config *config;
	struct vkms_config_plane *plane_cfg;
	struct vkms_config_crtc *crtc_cfg;
	struct vkms_config_encoder *encoder_cfg;
	int err;

	config = vkms_config_default_create(false, false, false);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, config);

	plane_cfg = get_first_plane(config);
	vkms_config_destroy_plane(plane_cfg);

	crtc_cfg = get_first_crtc(config);

	/* Invalid: No primary plane */
	plane_cfg = vkms_config_create_plane(config);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, plane_cfg);
	vkms_config_plane_set_type(plane_cfg, DRM_PLANE_TYPE_OVERLAY);
	err = vkms_config_plane_attach_crtc(plane_cfg, crtc_cfg);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_FALSE(test, vkms_config_is_valid(config));

	/* Invalid: Multiple primary planes */
	plane_cfg = vkms_config_create_plane(config);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, plane_cfg);
	vkms_config_plane_set_type(plane_cfg, DRM_PLANE_TYPE_PRIMARY);
	err = vkms_config_plane_attach_crtc(plane_cfg, crtc_cfg);
	KUNIT_EXPECT_EQ(test, err, 0);

	plane_cfg = vkms_config_create_plane(config);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, plane_cfg);
	vkms_config_plane_set_type(plane_cfg, DRM_PLANE_TYPE_PRIMARY);
	err = vkms_config_plane_attach_crtc(plane_cfg, crtc_cfg);
	KUNIT_EXPECT_EQ(test, err, 0);

	KUNIT_EXPECT_FALSE(test, vkms_config_is_valid(config));

	/* Valid: One primary plane */
	vkms_config_destroy_plane(plane_cfg);
	KUNIT_EXPECT_TRUE(test, vkms_config_is_valid(config));

	/* Invalid: Multiple cursor planes */
	plane_cfg = vkms_config_create_plane(config);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, plane_cfg);
	vkms_config_plane_set_type(plane_cfg, DRM_PLANE_TYPE_CURSOR);
	err = vkms_config_plane_attach_crtc(plane_cfg, crtc_cfg);
	KUNIT_EXPECT_EQ(test, err, 0);

	plane_cfg = vkms_config_create_plane(config);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, plane_cfg);
	vkms_config_plane_set_type(plane_cfg, DRM_PLANE_TYPE_CURSOR);
	err = vkms_config_plane_attach_crtc(plane_cfg, crtc_cfg);
	KUNIT_EXPECT_EQ(test, err, 0);

	KUNIT_EXPECT_FALSE(test, vkms_config_is_valid(config));

	/* Valid: One primary and one cursor plane */
	vkms_config_destroy_plane(plane_cfg);
	KUNIT_EXPECT_TRUE(test, vkms_config_is_valid(config));

	/* Invalid: Second CRTC without primary plane */
	crtc_cfg = vkms_config_create_crtc(config);
	encoder_cfg = vkms_config_create_encoder(config);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, crtc_cfg);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, encoder_cfg);

	err = vkms_config_encoder_attach_crtc(encoder_cfg, crtc_cfg);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_FALSE(test, vkms_config_is_valid(config));

	/* Valid: Second CRTC with a primary plane */
	plane_cfg = vkms_config_create_plane(config);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, plane_cfg);
	vkms_config_plane_set_type(plane_cfg, DRM_PLANE_TYPE_PRIMARY);
	err = vkms_config_plane_attach_crtc(plane_cfg, crtc_cfg);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_TRUE(test, vkms_config_is_valid(config));

	vkms_config_destroy(config);
}

static void vkms_config_test_valid_plane_possible_crtcs(struct kunit *test)
{
	struct vkms_config *config;
	struct vkms_config_plane *plane_cfg;
	struct vkms_config_crtc *crtc_cfg;

	config = vkms_config_default_create(false, false, false);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, config);

	plane_cfg = get_first_plane(config);
	crtc_cfg = get_first_crtc(config);

	/* Invalid: Primary plane without a possible CRTC */
	vkms_config_plane_detach_crtc(plane_cfg, crtc_cfg);
	KUNIT_EXPECT_FALSE(test, vkms_config_is_valid(config));

	vkms_config_destroy(config);
}

static void vkms_config_test_invalid_crtc_number(struct kunit *test)
{
	struct vkms_config *config;
	struct vkms_config_crtc *crtc_cfg;
	int n;

	config = vkms_config_default_create(false, false, false);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, config);

	/* Invalid: No CRTCs */
	crtc_cfg = get_first_crtc(config);
	vkms_config_destroy_crtc(config, crtc_cfg);
	KUNIT_EXPECT_FALSE(test, vkms_config_is_valid(config));

	/* Invalid: Too many CRTCs */
	for (n = 0; n <= 32; n++)
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, vkms_config_create_crtc(config));

	KUNIT_EXPECT_FALSE(test, vkms_config_is_valid(config));

	vkms_config_destroy(config);
}

static void vkms_config_test_invalid_encoder_number(struct kunit *test)
{
	struct vkms_config *config;
	struct vkms_config_encoder *encoder_cfg;
	int n;

	config = vkms_config_default_create(false, false, false);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, config);

	/* Invalid: No encoders */
	encoder_cfg = get_first_encoder(config);
	vkms_config_destroy_encoder(config, encoder_cfg);
	KUNIT_EXPECT_FALSE(test, vkms_config_is_valid(config));

	/* Invalid: Too many encoders */
	for (n = 0; n <= 32; n++)
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, vkms_config_create_encoder(config));

	KUNIT_EXPECT_FALSE(test, vkms_config_is_valid(config));

	vkms_config_destroy(config);
}

static void vkms_config_test_valid_encoder_possible_crtcs(struct kunit *test)
{
	struct vkms_config *config;
	struct vkms_config_plane *plane_cfg;
	struct vkms_config_crtc *crtc_cfg1, *crtc_cfg2;
	struct vkms_config_encoder *encoder_cfg;
	int err;

	config = vkms_config_default_create(false, false, false);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, config);

	crtc_cfg1 = get_first_crtc(config);

	/* Invalid: Encoder without a possible CRTC */
	encoder_cfg = vkms_config_create_encoder(config);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, encoder_cfg);
	KUNIT_EXPECT_FALSE(test, vkms_config_is_valid(config));

	/* Valid: Second CRTC with shared encoder */
	crtc_cfg2 = vkms_config_create_crtc(config);
	plane_cfg = vkms_config_create_plane(config);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, crtc_cfg2);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, plane_cfg);

	vkms_config_plane_set_type(plane_cfg, DRM_PLANE_TYPE_PRIMARY);
	err = vkms_config_plane_attach_crtc(plane_cfg, crtc_cfg2);
	KUNIT_EXPECT_EQ(test, err, 0);

	err = vkms_config_encoder_attach_crtc(encoder_cfg, crtc_cfg1);
	KUNIT_EXPECT_EQ(test, err, 0);

	err = vkms_config_encoder_attach_crtc(encoder_cfg, crtc_cfg2);
	KUNIT_EXPECT_EQ(test, err, 0);

	KUNIT_EXPECT_TRUE(test, vkms_config_is_valid(config));

	/* Invalid: Second CRTC without encoders */
	vkms_config_encoder_detach_crtc(encoder_cfg, crtc_cfg2);
	KUNIT_EXPECT_FALSE(test, vkms_config_is_valid(config));

	/* Valid: First CRTC with 2 possible encoder */
	vkms_config_destroy_plane(plane_cfg);
	vkms_config_destroy_crtc(config, crtc_cfg2);
	KUNIT_EXPECT_TRUE(test, vkms_config_is_valid(config));

	vkms_config_destroy(config);
}

static void vkms_config_test_invalid_connector_number(struct kunit *test)
{
	struct vkms_config *config;
	struct vkms_config_connector *connector_cfg;
	int n;

	config = vkms_config_default_create(false, false, false);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, config);

	/* Invalid: No connectors */
	connector_cfg = get_first_connector(config);
	vkms_config_destroy_connector(connector_cfg);
	KUNIT_EXPECT_FALSE(test, vkms_config_is_valid(config));

	/* Invalid: Too many connectors */
	for (n = 0; n <= 32; n++)
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, vkms_config_create_connector(config));

	KUNIT_EXPECT_FALSE(test, vkms_config_is_valid(config));

	vkms_config_destroy(config);
}

static void vkms_config_test_valid_connector_possible_encoders(struct kunit *test)
{
	struct vkms_config *config;
	struct vkms_config_encoder *encoder_cfg;
	struct vkms_config_connector *connector_cfg;

	config = vkms_config_default_create(false, false, false);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, config);

	encoder_cfg = get_first_encoder(config);
	connector_cfg = get_first_connector(config);

	/* Invalid: Connector without a possible encoder */
	vkms_config_connector_detach_encoder(connector_cfg, encoder_cfg);
	KUNIT_EXPECT_FALSE(test, vkms_config_is_valid(config));

	vkms_config_destroy(config);
}

static void vkms_config_test_attach_different_configs(struct kunit *test)
{
	struct vkms_config *config1, *config2;
	struct vkms_config_plane *plane_cfg1, *plane_cfg2;
	struct vkms_config_crtc *crtc_cfg1, *crtc_cfg2;
	struct vkms_config_encoder *encoder_cfg1, *encoder_cfg2;
	struct vkms_config_connector *connector_cfg1, *connector_cfg2;
	int err;

	config1 = vkms_config_create("test1");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, config1);

	config2 = vkms_config_create("test2");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, config2);

	plane_cfg1 = vkms_config_create_plane(config1);
	crtc_cfg1 = vkms_config_create_crtc(config1);
	encoder_cfg1 = vkms_config_create_encoder(config1);
	connector_cfg1 = vkms_config_create_connector(config1);

	plane_cfg2 = vkms_config_create_plane(config2);
	crtc_cfg2 = vkms_config_create_crtc(config2);
	encoder_cfg2 = vkms_config_create_encoder(config2);
	connector_cfg2 = vkms_config_create_connector(config2);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, plane_cfg1);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, plane_cfg2);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, crtc_cfg1);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, crtc_cfg2);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, encoder_cfg1);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, encoder_cfg2);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, connector_cfg1);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, connector_cfg2);

	err = vkms_config_plane_attach_crtc(plane_cfg1, crtc_cfg2);
	KUNIT_EXPECT_NE(test, err, 0);
	err = vkms_config_plane_attach_crtc(plane_cfg2, crtc_cfg1);
	KUNIT_EXPECT_NE(test, err, 0);

	err = vkms_config_encoder_attach_crtc(encoder_cfg1, crtc_cfg2);
	KUNIT_EXPECT_NE(test, err, 0);
	err = vkms_config_encoder_attach_crtc(encoder_cfg2, crtc_cfg1);
	KUNIT_EXPECT_NE(test, err, 0);

	err = vkms_config_connector_attach_encoder(connector_cfg1, encoder_cfg2);
	KUNIT_EXPECT_NE(test, err, 0);
	err = vkms_config_connector_attach_encoder(connector_cfg2, encoder_cfg1);
	KUNIT_EXPECT_NE(test, err, 0);

	vkms_config_destroy(config1);
	vkms_config_destroy(config2);
}

static void vkms_config_test_plane_attach_crtc(struct kunit *test)
{
	struct vkms_config *config;
	struct vkms_config_plane *overlay_cfg;
	struct vkms_config_plane *primary_cfg;
	struct vkms_config_plane *cursor_cfg;
	struct vkms_config_crtc *crtc_cfg;
	int err;

	config = vkms_config_create("test");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, config);

	overlay_cfg = vkms_config_create_plane(config);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, overlay_cfg);
	vkms_config_plane_set_type(overlay_cfg, DRM_PLANE_TYPE_OVERLAY);

	primary_cfg = vkms_config_create_plane(config);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, primary_cfg);
	vkms_config_plane_set_type(primary_cfg, DRM_PLANE_TYPE_PRIMARY);

	cursor_cfg = vkms_config_create_plane(config);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, cursor_cfg);
	vkms_config_plane_set_type(cursor_cfg, DRM_PLANE_TYPE_CURSOR);

	crtc_cfg = vkms_config_create_crtc(config);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, crtc_cfg);

	/* No primary or cursor planes */
	KUNIT_EXPECT_NULL(test, vkms_config_crtc_primary_plane(config, crtc_cfg));
	KUNIT_EXPECT_NULL(test, vkms_config_crtc_cursor_plane(config, crtc_cfg));

	/* Overlay plane, but no primary or cursor planes */
	err = vkms_config_plane_attach_crtc(overlay_cfg, crtc_cfg);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_NULL(test, vkms_config_crtc_primary_plane(config, crtc_cfg));
	KUNIT_EXPECT_NULL(test, vkms_config_crtc_cursor_plane(config, crtc_cfg));

	/* Primary plane, attaching it twice must fail */
	err = vkms_config_plane_attach_crtc(primary_cfg, crtc_cfg);
	KUNIT_EXPECT_EQ(test, err, 0);
	err = vkms_config_plane_attach_crtc(primary_cfg, crtc_cfg);
	KUNIT_EXPECT_NE(test, err, 0);
	KUNIT_EXPECT_PTR_EQ(test,
			    vkms_config_crtc_primary_plane(config, crtc_cfg),
			    primary_cfg);
	KUNIT_EXPECT_NULL(test, vkms_config_crtc_cursor_plane(config, crtc_cfg));

	/* Primary and cursor planes */
	err = vkms_config_plane_attach_crtc(cursor_cfg, crtc_cfg);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_PTR_EQ(test,
			    vkms_config_crtc_primary_plane(config, crtc_cfg),
			    primary_cfg);
	KUNIT_EXPECT_PTR_EQ(test,
			    vkms_config_crtc_cursor_plane(config, crtc_cfg),
			    cursor_cfg);

	/* Detach primary and destroy cursor plane */
	vkms_config_plane_detach_crtc(overlay_cfg, crtc_cfg);
	vkms_config_plane_detach_crtc(primary_cfg, crtc_cfg);
	vkms_config_destroy_plane(cursor_cfg);
	KUNIT_EXPECT_NULL(test, vkms_config_crtc_primary_plane(config, crtc_cfg));
	KUNIT_EXPECT_NULL(test, vkms_config_crtc_cursor_plane(config, crtc_cfg));

	vkms_config_destroy(config);
}

static void vkms_config_test_plane_get_possible_crtcs(struct kunit *test)
{
	struct vkms_config *config;
	struct vkms_config_plane *plane_cfg1, *plane_cfg2;
	struct vkms_config_crtc *crtc_cfg1, *crtc_cfg2;
	struct vkms_config_crtc *possible_crtc;
	unsigned long idx = 0;
	int n_crtcs = 0;
	int err;

	config = vkms_config_create("test");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, config);

	plane_cfg1 = vkms_config_create_plane(config);
	plane_cfg2 = vkms_config_create_plane(config);
	crtc_cfg1 = vkms_config_create_crtc(config);
	crtc_cfg2 = vkms_config_create_crtc(config);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, plane_cfg1);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, plane_cfg2);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, crtc_cfg1);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, crtc_cfg2);

	/* No possible CRTCs */
	vkms_config_plane_for_each_possible_crtc(plane_cfg1, idx, possible_crtc)
		KUNIT_FAIL(test, "Unexpected possible CRTC");

	vkms_config_plane_for_each_possible_crtc(plane_cfg2, idx, possible_crtc)
		KUNIT_FAIL(test, "Unexpected possible CRTC");

	/* Plane 1 attached to CRTC 1 and 2 */
	err = vkms_config_plane_attach_crtc(plane_cfg1, crtc_cfg1);
	KUNIT_EXPECT_EQ(test, err, 0);
	err = vkms_config_plane_attach_crtc(plane_cfg1, crtc_cfg2);
	KUNIT_EXPECT_EQ(test, err, 0);

	vkms_config_plane_for_each_possible_crtc(plane_cfg1, idx, possible_crtc) {
		n_crtcs++;
		if (possible_crtc != crtc_cfg1 && possible_crtc != crtc_cfg2)
			KUNIT_FAIL(test, "Unexpected possible CRTC");
	}
	KUNIT_ASSERT_EQ(test, n_crtcs, 2);
	n_crtcs = 0;

	vkms_config_plane_for_each_possible_crtc(plane_cfg2, idx, possible_crtc)
		KUNIT_FAIL(test, "Unexpected possible CRTC");

	/* Plane 1 attached to CRTC 1 and plane 2 to CRTC 2 */
	vkms_config_plane_detach_crtc(plane_cfg1, crtc_cfg2);
	vkms_config_plane_for_each_possible_crtc(plane_cfg1, idx, possible_crtc) {
		n_crtcs++;
		if (possible_crtc != crtc_cfg1)
			KUNIT_FAIL(test, "Unexpected possible CRTC");
	}
	KUNIT_ASSERT_EQ(test, n_crtcs, 1);
	n_crtcs = 0;

	err = vkms_config_plane_attach_crtc(plane_cfg2, crtc_cfg2);
	KUNIT_EXPECT_EQ(test, err, 0);
	vkms_config_plane_for_each_possible_crtc(plane_cfg2, idx, possible_crtc) {
		n_crtcs++;
		if (possible_crtc != crtc_cfg2)
			KUNIT_FAIL(test, "Unexpected possible CRTC");
	}
	KUNIT_ASSERT_EQ(test, n_crtcs, 1);

	vkms_config_destroy(config);
}

static void vkms_config_test_encoder_get_possible_crtcs(struct kunit *test)
{
	struct vkms_config *config;
	struct vkms_config_encoder *encoder_cfg1, *encoder_cfg2;
	struct vkms_config_crtc *crtc_cfg1, *crtc_cfg2;
	struct vkms_config_crtc *possible_crtc;
	unsigned long idx = 0;
	int n_crtcs = 0;
	int err;

	config = vkms_config_create("test");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, config);

	encoder_cfg1 = vkms_config_create_encoder(config);
	encoder_cfg2 = vkms_config_create_encoder(config);
	crtc_cfg1 = vkms_config_create_crtc(config);
	crtc_cfg2 = vkms_config_create_crtc(config);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, encoder_cfg1);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, encoder_cfg2);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, crtc_cfg1);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, crtc_cfg2);

	/* No possible CRTCs */
	vkms_config_encoder_for_each_possible_crtc(encoder_cfg1, idx, possible_crtc)
		KUNIT_FAIL(test, "Unexpected possible CRTC");

	vkms_config_encoder_for_each_possible_crtc(encoder_cfg2, idx, possible_crtc)
		KUNIT_FAIL(test, "Unexpected possible CRTC");

	/* Encoder 1 attached to CRTC 1 and 2 */
	err = vkms_config_encoder_attach_crtc(encoder_cfg1, crtc_cfg1);
	KUNIT_EXPECT_EQ(test, err, 0);
	err = vkms_config_encoder_attach_crtc(encoder_cfg1, crtc_cfg2);
	KUNIT_EXPECT_EQ(test, err, 0);

	vkms_config_encoder_for_each_possible_crtc(encoder_cfg1, idx, possible_crtc) {
		n_crtcs++;
		if (possible_crtc != crtc_cfg1 && possible_crtc != crtc_cfg2)
			KUNIT_FAIL(test, "Unexpected possible CRTC");
	}
	KUNIT_ASSERT_EQ(test, n_crtcs, 2);
	n_crtcs = 0;

	vkms_config_encoder_for_each_possible_crtc(encoder_cfg2, idx, possible_crtc)
		KUNIT_FAIL(test, "Unexpected possible CRTC");

	/* Encoder 1 attached to CRTC 1 and encoder 2 to CRTC 2 */
	vkms_config_encoder_detach_crtc(encoder_cfg1, crtc_cfg2);
	vkms_config_encoder_for_each_possible_crtc(encoder_cfg1, idx, possible_crtc) {
		n_crtcs++;
		if (possible_crtc != crtc_cfg1)
			KUNIT_FAIL(test, "Unexpected possible CRTC");
	}
	KUNIT_ASSERT_EQ(test, n_crtcs, 1);
	n_crtcs = 0;

	err = vkms_config_encoder_attach_crtc(encoder_cfg2, crtc_cfg2);
	KUNIT_EXPECT_EQ(test, err, 0);
	vkms_config_encoder_for_each_possible_crtc(encoder_cfg2, idx, possible_crtc) {
		n_crtcs++;
		if (possible_crtc != crtc_cfg2)
			KUNIT_FAIL(test, "Unexpected possible CRTC");
	}
	KUNIT_ASSERT_EQ(test, n_crtcs, 1);

	vkms_config_destroy(config);
}

static void vkms_config_test_connector_get_possible_encoders(struct kunit *test)
{
	struct vkms_config *config;
	struct vkms_config_connector *connector_cfg1, *connector_cfg2;
	struct vkms_config_encoder *encoder_cfg1, *encoder_cfg2;
	struct vkms_config_encoder *possible_encoder;
	unsigned long idx = 0;
	int n_encoders = 0;
	int err;

	config = vkms_config_create("test");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, config);

	connector_cfg1 = vkms_config_create_connector(config);
	connector_cfg2 = vkms_config_create_connector(config);
	encoder_cfg1 = vkms_config_create_encoder(config);
	encoder_cfg2 = vkms_config_create_encoder(config);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, connector_cfg1);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, connector_cfg2);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, encoder_cfg1);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, encoder_cfg2);

	/* No possible encoders */
	vkms_config_connector_for_each_possible_encoder(connector_cfg1, idx,
							possible_encoder)
		KUNIT_FAIL(test, "Unexpected possible encoder");

	vkms_config_connector_for_each_possible_encoder(connector_cfg2, idx,
							possible_encoder)
		KUNIT_FAIL(test, "Unexpected possible encoder");

	/* Connector 1 attached to encoders 1 and 2 */
	err = vkms_config_connector_attach_encoder(connector_cfg1, encoder_cfg1);
	KUNIT_EXPECT_EQ(test, err, 0);
	err = vkms_config_connector_attach_encoder(connector_cfg1, encoder_cfg2);
	KUNIT_EXPECT_EQ(test, err, 0);

	vkms_config_connector_for_each_possible_encoder(connector_cfg1, idx,
							possible_encoder) {
		n_encoders++;
		if (possible_encoder != encoder_cfg1 &&
		    possible_encoder != encoder_cfg2)
			KUNIT_FAIL(test, "Unexpected possible encoder");
	}
	KUNIT_ASSERT_EQ(test, n_encoders, 2);
	n_encoders = 0;

	vkms_config_connector_for_each_possible_encoder(connector_cfg2, idx,
							possible_encoder)
		KUNIT_FAIL(test, "Unexpected possible encoder");

	/* Connector 1 attached to encoder 1 and connector 2 to encoder 2 */
	vkms_config_connector_detach_encoder(connector_cfg1, encoder_cfg2);
	vkms_config_connector_for_each_possible_encoder(connector_cfg1, idx,
							possible_encoder) {
		n_encoders++;
		if (possible_encoder != encoder_cfg1)
			KUNIT_FAIL(test, "Unexpected possible encoder");
	}
	KUNIT_ASSERT_EQ(test, n_encoders, 1);
	n_encoders = 0;

	err = vkms_config_connector_attach_encoder(connector_cfg2, encoder_cfg2);
	KUNIT_EXPECT_EQ(test, err, 0);
	vkms_config_connector_for_each_possible_encoder(connector_cfg2, idx,
							possible_encoder) {
		n_encoders++;
		if (possible_encoder != encoder_cfg2)
			KUNIT_FAIL(test, "Unexpected possible encoder");
	}
	KUNIT_ASSERT_EQ(test, n_encoders, 1);

	vkms_config_destroy(config);
}

static struct kunit_case vkms_config_test_cases[] = {
	KUNIT_CASE(vkms_config_test_empty_config),
	KUNIT_CASE_PARAM(vkms_config_test_default_config,
			 default_config_gen_params),
	KUNIT_CASE(vkms_config_test_get_planes),
	KUNIT_CASE(vkms_config_test_get_crtcs),
	KUNIT_CASE(vkms_config_test_get_encoders),
	KUNIT_CASE(vkms_config_test_get_connectors),
	KUNIT_CASE(vkms_config_test_invalid_plane_number),
	KUNIT_CASE(vkms_config_test_valid_plane_type),
	KUNIT_CASE(vkms_config_test_valid_plane_possible_crtcs),
	KUNIT_CASE(vkms_config_test_invalid_crtc_number),
	KUNIT_CASE(vkms_config_test_invalid_encoder_number),
	KUNIT_CASE(vkms_config_test_valid_encoder_possible_crtcs),
	KUNIT_CASE(vkms_config_test_invalid_connector_number),
	KUNIT_CASE(vkms_config_test_valid_connector_possible_encoders),
	KUNIT_CASE(vkms_config_test_attach_different_configs),
	KUNIT_CASE(vkms_config_test_plane_attach_crtc),
	KUNIT_CASE(vkms_config_test_plane_get_possible_crtcs),
	KUNIT_CASE(vkms_config_test_encoder_get_possible_crtcs),
	KUNIT_CASE(vkms_config_test_connector_get_possible_encoders),
	{}
};

static struct kunit_suite vkms_config_test_suite = {
	.name = "vkms-config",
	.test_cases = vkms_config_test_cases,
};

kunit_test_suite(vkms_config_test_suite);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Kunit test for vkms config utility");
