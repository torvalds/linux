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
	vkms_config_for_each_plane(config, plane_cfg) {
		n_planes++;
		if (plane_cfg != plane_cfg1)
			KUNIT_FAIL(test, "Unexpected plane");
	}
	KUNIT_ASSERT_EQ(test, n_planes, 1);
	n_planes = 0;

	plane_cfg2 = vkms_config_create_plane(config);
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
	KUNIT_ASSERT_EQ(test, vkms_config_get_num_crtcs(config), 1);
	vkms_config_for_each_crtc(config, crtc_cfg) {
		if (crtc_cfg != crtc_cfg1)
			KUNIT_FAIL(test, "Unexpected CRTC");
	}

	crtc_cfg2 = vkms_config_create_crtc(config);
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
		vkms_config_create_plane(config);

	KUNIT_EXPECT_FALSE(test, vkms_config_is_valid(config));

	vkms_config_destroy(config);
}

static void vkms_config_test_valid_plane_type(struct kunit *test)
{
	struct vkms_config *config;
	struct vkms_config_plane *plane_cfg;

	config = vkms_config_default_create(false, false, false);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, config);

	plane_cfg = get_first_plane(config);
	vkms_config_destroy_plane(plane_cfg);

	/* Invalid: No primary plane */
	plane_cfg = vkms_config_create_plane(config);
	vkms_config_plane_set_type(plane_cfg, DRM_PLANE_TYPE_OVERLAY);
	KUNIT_EXPECT_FALSE(test, vkms_config_is_valid(config));

	/* Invalid: Multiple primary planes */
	plane_cfg = vkms_config_create_plane(config);
	vkms_config_plane_set_type(plane_cfg, DRM_PLANE_TYPE_PRIMARY);
	plane_cfg = vkms_config_create_plane(config);
	vkms_config_plane_set_type(plane_cfg, DRM_PLANE_TYPE_PRIMARY);
	KUNIT_EXPECT_FALSE(test, vkms_config_is_valid(config));

	/* Valid: One primary plane */
	vkms_config_destroy_plane(plane_cfg);
	KUNIT_EXPECT_TRUE(test, vkms_config_is_valid(config));

	/* Invalid: Multiple cursor planes */
	plane_cfg = vkms_config_create_plane(config);
	vkms_config_plane_set_type(plane_cfg, DRM_PLANE_TYPE_CURSOR);
	plane_cfg = vkms_config_create_plane(config);
	vkms_config_plane_set_type(plane_cfg, DRM_PLANE_TYPE_CURSOR);
	KUNIT_EXPECT_FALSE(test, vkms_config_is_valid(config));

	/* Valid: One primary and one cursor plane */
	vkms_config_destroy_plane(plane_cfg);
	KUNIT_EXPECT_TRUE(test, vkms_config_is_valid(config));

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
		vkms_config_create_crtc(config);

	KUNIT_EXPECT_FALSE(test, vkms_config_is_valid(config));

	vkms_config_destroy(config);
}

static struct kunit_case vkms_config_test_cases[] = {
	KUNIT_CASE(vkms_config_test_empty_config),
	KUNIT_CASE_PARAM(vkms_config_test_default_config,
			 default_config_gen_params),
	KUNIT_CASE(vkms_config_test_get_planes),
	KUNIT_CASE(vkms_config_test_get_crtcs),
	KUNIT_CASE(vkms_config_test_invalid_plane_number),
	KUNIT_CASE(vkms_config_test_valid_plane_type),
	KUNIT_CASE(vkms_config_test_invalid_crtc_number),
	{}
};

static struct kunit_suite vkms_config_test_suite = {
	.name = "vkms-config",
	.test_cases = vkms_config_test_cases,
};

kunit_test_suite(vkms_config_test_suite);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Kunit test for vkms config utility");
