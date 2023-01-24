// SPDX-License-Identifier: GPL-2.0
/*
 * Kunit test for drm_modes functions
 */

#include <drm/drm_connector.h>

#include <kunit/test.h>

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

kunit_test_suite(drm_get_tv_mode_from_name_test_suite);

MODULE_AUTHOR("Maxime Ripard <maxime@cerno.tech>");
MODULE_LICENSE("GPL");
