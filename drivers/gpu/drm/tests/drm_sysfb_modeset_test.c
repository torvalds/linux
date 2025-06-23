// SPDX-License-Identifier: GPL-2.0+

#include <kunit/test.h>

#include <drm/drm_fourcc.h>
#include <drm/drm_kunit_helpers.h>

#include "../sysfb/drm_sysfb_helper.h"

#define TEST_BUF_SIZE 50

struct sysfb_build_fourcc_list_case {
	const char *name;
	u32 native_fourccs[TEST_BUF_SIZE];
	size_t native_fourccs_size;
	u32 expected[TEST_BUF_SIZE];
	size_t expected_fourccs_size;
};

static struct sysfb_build_fourcc_list_case sysfb_build_fourcc_list_cases[] = {
	{
		.name = "no native formats",
		.native_fourccs = { },
		.native_fourccs_size = 0,
		.expected = { DRM_FORMAT_XRGB8888 },
		.expected_fourccs_size = 1,
	},
	{
		.name = "XRGB8888 as native format",
		.native_fourccs = { DRM_FORMAT_XRGB8888 },
		.native_fourccs_size = 1,
		.expected = { DRM_FORMAT_XRGB8888 },
		.expected_fourccs_size = 1,
	},
	{
		.name = "remove duplicates",
		.native_fourccs = {
			DRM_FORMAT_XRGB8888,
			DRM_FORMAT_XRGB8888,
			DRM_FORMAT_RGB888,
			DRM_FORMAT_RGB888,
			DRM_FORMAT_RGB888,
			DRM_FORMAT_XRGB8888,
			DRM_FORMAT_RGB888,
			DRM_FORMAT_RGB565,
			DRM_FORMAT_RGB888,
			DRM_FORMAT_XRGB8888,
			DRM_FORMAT_RGB565,
			DRM_FORMAT_RGB565,
			DRM_FORMAT_XRGB8888,
		},
		.native_fourccs_size = 11,
		.expected = {
			DRM_FORMAT_XRGB8888,
			DRM_FORMAT_RGB888,
			DRM_FORMAT_RGB565,
		},
		.expected_fourccs_size = 3,
	},
	{
		.name = "convert alpha formats",
		.native_fourccs = {
			DRM_FORMAT_ARGB1555,
			DRM_FORMAT_ABGR1555,
			DRM_FORMAT_RGBA5551,
			DRM_FORMAT_BGRA5551,
			DRM_FORMAT_ARGB8888,
			DRM_FORMAT_ABGR8888,
			DRM_FORMAT_RGBA8888,
			DRM_FORMAT_BGRA8888,
			DRM_FORMAT_ARGB2101010,
			DRM_FORMAT_ABGR2101010,
			DRM_FORMAT_RGBA1010102,
			DRM_FORMAT_BGRA1010102,
		},
		.native_fourccs_size = 12,
		.expected = {
			DRM_FORMAT_XRGB1555,
			DRM_FORMAT_XBGR1555,
			DRM_FORMAT_RGBX5551,
			DRM_FORMAT_BGRX5551,
			DRM_FORMAT_XRGB8888,
			DRM_FORMAT_XBGR8888,
			DRM_FORMAT_RGBX8888,
			DRM_FORMAT_BGRX8888,
			DRM_FORMAT_XRGB2101010,
			DRM_FORMAT_XBGR2101010,
			DRM_FORMAT_RGBX1010102,
			DRM_FORMAT_BGRX1010102,
		},
		.expected_fourccs_size = 12,
	},
	{
		.name = "random formats",
		.native_fourccs = {
			DRM_FORMAT_Y212,
			DRM_FORMAT_ARGB1555,
			DRM_FORMAT_ABGR16161616F,
			DRM_FORMAT_C8,
			DRM_FORMAT_BGR888,
			DRM_FORMAT_XRGB1555,
			DRM_FORMAT_RGBA5551,
			DRM_FORMAT_BGR565_A8,
			DRM_FORMAT_R10,
			DRM_FORMAT_XYUV8888,
		},
		.native_fourccs_size = 10,
		.expected = {
			DRM_FORMAT_Y212,
			DRM_FORMAT_XRGB1555,
			DRM_FORMAT_ABGR16161616F,
			DRM_FORMAT_C8,
			DRM_FORMAT_BGR888,
			DRM_FORMAT_RGBX5551,
			DRM_FORMAT_BGR565_A8,
			DRM_FORMAT_R10,
			DRM_FORMAT_XYUV8888,
			DRM_FORMAT_XRGB8888,
		},
		.expected_fourccs_size = 10,
	},
};

static void sysfb_build_fourcc_list_case_desc(struct sysfb_build_fourcc_list_case *t, char *desc)
{
	strscpy(desc, t->name, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(sysfb_build_fourcc_list, sysfb_build_fourcc_list_cases,
		  sysfb_build_fourcc_list_case_desc);

static void drm_test_sysfb_build_fourcc_list(struct kunit *test)
{
	const struct sysfb_build_fourcc_list_case *params = test->param_value;
	u32 fourccs_out[TEST_BUF_SIZE] = {0};
	size_t nfourccs_out;
	struct drm_device *drm;
	struct device *dev;

	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	drm = __drm_kunit_helper_alloc_drm_device(test, dev, sizeof(*drm), 0, DRIVER_MODESET);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, drm);

	nfourccs_out = drm_sysfb_build_fourcc_list(drm, params->native_fourccs,
						   params->native_fourccs_size,
						   fourccs_out, TEST_BUF_SIZE);

	KUNIT_EXPECT_EQ(test, nfourccs_out, params->expected_fourccs_size);
	KUNIT_EXPECT_MEMEQ(test, fourccs_out, params->expected, TEST_BUF_SIZE);
}

static struct kunit_case drm_sysfb_modeset_test_cases[] = {
	KUNIT_CASE_PARAM(drm_test_sysfb_build_fourcc_list, sysfb_build_fourcc_list_gen_params),
	{}
};

static struct kunit_suite drm_sysfb_modeset_test_suite = {
	.name = "drm_sysfb_modeset_test",
	.test_cases = drm_sysfb_modeset_test_cases,
};

kunit_test_suite(drm_sysfb_modeset_test_suite);

MODULE_DESCRIPTION("KUnit tests for the drm_sysfb_modeset APIs");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("José Expósito <jose.exposito89@gmail.com>");
