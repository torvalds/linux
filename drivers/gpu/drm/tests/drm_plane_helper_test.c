// SPDX-License-Identifier: GPL-2.0
/*
 * Test cases for the drm_plane_helper functions
 *
 * Copyright (c) 2022 Maíra Canal <mairacanal@riseup.net>
 */

#include <kunit/test.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_modes.h>
#include <drm/drm_rect.h>

static const struct drm_crtc_state crtc_state = {
	.crtc = ZERO_SIZE_PTR,
	.enable = true,
	.active = true,
	.mode = {
		DRM_MODE("1024x768", 0, 65000, 1024, 1048,
			 1184, 1344, 0, 768, 771, 777, 806, 0,
			 DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC)
	},
};

struct drm_check_plane_state_test {
	const char *name;
	const char *msg;
	struct {
		unsigned int x;
		unsigned int y;
		unsigned int w;
		unsigned int h;
	} src, src_expected;
	struct {
		int x;
		int y;
		unsigned int w;
		unsigned int h;
	} crtc, crtc_expected;
	unsigned int rotation;
	int min_scale;
	int max_scale;
	bool can_position;
};

static int drm_plane_helper_init(struct kunit *test)
{
	const struct drm_check_plane_state_test *params = test->param_value;
	struct drm_plane *plane;
	struct drm_framebuffer *fb;
	struct drm_plane_state *mock;

	plane = kunit_kzalloc(test, sizeof(*plane), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, plane);

	fb = kunit_kzalloc(test, sizeof(*fb), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, fb);
	fb->width = 2048;
	fb->height = 2048;

	mock = kunit_kzalloc(test, sizeof(*mock), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, mock);
	mock->plane = plane;
	mock->crtc = ZERO_SIZE_PTR;
	mock->fb = fb;
	mock->rotation = params->rotation;
	mock->src_x = params->src.x;
	mock->src_y = params->src.y;
	mock->src_w = params->src.w;
	mock->src_h = params->src.h;
	mock->crtc_x = params->crtc.x;
	mock->crtc_y = params->crtc.y;
	mock->crtc_w = params->crtc.w;
	mock->crtc_h = params->crtc.h;

	test->priv = mock;

	return 0;
}

static void check_src_eq(struct kunit *test, struct drm_plane_state *plane_state,
			 unsigned int src_x, unsigned int src_y,
			 unsigned int src_w, unsigned int src_h)
{
	struct drm_rect expected = DRM_RECT_INIT(src_x, src_y, src_w, src_h);

	KUNIT_ASSERT_GE_MSG(test, plane_state->src.x1, 0,
			    "src x coordinate %x should never be below 0, src: " DRM_RECT_FP_FMT,
			    plane_state->src.x1, DRM_RECT_FP_ARG(&plane_state->src));

	KUNIT_ASSERT_GE_MSG(test, plane_state->src.y1, 0,
			    "src y coordinate %x should never be below 0, src: " DRM_RECT_FP_FMT,
			    plane_state->src.y1, DRM_RECT_FP_ARG(&plane_state->src));

	KUNIT_EXPECT_TRUE_MSG(test, drm_rect_equals(&plane_state->src, &expected),
			      "dst: " DRM_RECT_FP_FMT ", expected: " DRM_RECT_FP_FMT,
			      DRM_RECT_FP_ARG(&plane_state->src), DRM_RECT_FP_ARG(&expected));
}

static void check_crtc_eq(struct kunit *test, struct drm_plane_state *plane_state,
			  int crtc_x, int crtc_y,
			  unsigned int crtc_w, unsigned int crtc_h)
{
	struct drm_rect expected = DRM_RECT_INIT(crtc_x, crtc_y, crtc_w, crtc_h);

	KUNIT_EXPECT_TRUE_MSG(test, drm_rect_equals(&plane_state->dst, &expected),
			      "dst: " DRM_RECT_FMT ", expected: " DRM_RECT_FMT,
			      DRM_RECT_ARG(&plane_state->dst), DRM_RECT_ARG(&expected));
}

static void drm_test_check_plane_state(struct kunit *test)
{
	const struct drm_check_plane_state_test *params = test->param_value;
	struct drm_plane_state *plane_state = test->priv;

	KUNIT_ASSERT_EQ_MSG(test,
			    drm_atomic_helper_check_plane_state(plane_state, &crtc_state,
								params->min_scale,
								params->max_scale,
								params->can_position, false),
			    0, params->msg);
	KUNIT_EXPECT_TRUE(test, plane_state->visible);
	check_src_eq(test, plane_state, params->src_expected.x, params->src_expected.y,
		     params->src_expected.w, params->src_expected.h);
	check_crtc_eq(test, plane_state, params->crtc_expected.x, params->crtc_expected.y,
		      params->crtc_expected.w, params->crtc_expected.h);
}

static void drm_check_plane_state_desc(const struct drm_check_plane_state_test *t,
				       char *desc)
{
	sprintf(desc, "%s", t->name);
}

static const struct drm_check_plane_state_test drm_check_plane_state_tests[] = {
	{
		.name = "clipping_simple",
		.msg = "Simple clipping check should pass",
		.src = { 0, 0,
			 2048 << 16,
			 2048 << 16 },
		.crtc = { 0, 0, 2048, 2048 },
		.rotation = DRM_MODE_ROTATE_0,
		.min_scale = DRM_PLANE_NO_SCALING,
		.max_scale = DRM_PLANE_NO_SCALING,
		.can_position = false,
		.src_expected = { 0, 0, 1024 << 16, 768 << 16 },
		.crtc_expected = { 0, 0, 1024, 768 },
	},
	{
		.name = "clipping_rotate_reflect",
		.msg = "Rotated clipping check should pass",
		.src = { 0, 0,
			 2048 << 16,
			 2048 << 16 },
		.crtc = { 0, 0, 2048, 2048 },
		.rotation = DRM_MODE_ROTATE_90 | DRM_MODE_REFLECT_X,
		.min_scale = DRM_PLANE_NO_SCALING,
		.max_scale = DRM_PLANE_NO_SCALING,
		.can_position = false,
		.src_expected = { 0, 0, 768 << 16, 1024 << 16 },
		.crtc_expected = { 0, 0, 1024, 768 },
	},
	{
		.name = "positioning_simple",
		.msg = "Simple positioning should work",
		.src = { 0, 0, 1023 << 16, 767 << 16 },
		.crtc = { 0, 0, 1023, 767 },
		.rotation = DRM_MODE_ROTATE_0,
		.min_scale = DRM_PLANE_NO_SCALING,
		.max_scale = DRM_PLANE_NO_SCALING,
		.can_position = true,
		.src_expected = { 0, 0, 1023 << 16, 767 << 16 },
		.crtc_expected = { 0, 0, 1023, 767 },
	},
	{
		.name = "upscaling",
		.msg = "Upscaling exactly 2x should work",
		.src = { 0, 0, 512 << 16, 384 << 16 },
		.crtc = { 0, 0, 1024, 768 },
		.rotation = DRM_MODE_ROTATE_0,
		.min_scale = 0x8000,
		.max_scale = DRM_PLANE_NO_SCALING,
		.can_position = false,
		.src_expected = { 0, 0, 512 << 16, 384 << 16 },
		.crtc_expected = { 0, 0, 1024, 768 },
	},
	{
		.name = "downscaling",
		.msg = "Should succeed with exact scaling limit",
		.src = { 0, 0, 2048 << 16, 1536 << 16 },
		.crtc = { 0, 0, 1024, 768 },
		.rotation = DRM_MODE_ROTATE_0,
		.min_scale = DRM_PLANE_NO_SCALING,
		.max_scale = 0x20000,
		.can_position = false,
		.src_expected = { 0, 0, 2048 << 16, 1536 << 16 },
		.crtc_expected = { 0, 0, 1024, 768 },
	},
	{
		.name = "rounding1",
		.msg = "Should succeed by clipping to exact multiple",
		.src = { 0, 0, 0x40001, 0x40001 },
		.crtc = { 1022, 766, 4, 4 },
		.rotation = DRM_MODE_ROTATE_0,
		.min_scale = DRM_PLANE_NO_SCALING,
		.max_scale = 0x10001,
		.can_position = true,
		.src_expected = { 0, 0, 2 << 16, 2 << 16 },
		.crtc_expected = { 1022, 766, 2, 2 },
	},
	{
		.name = "rounding2",
		.msg = "Should succeed by clipping to exact multiple",
		.src = { 0x20001, 0x20001, 0x4040001, 0x3040001 },
		.crtc = { -2, -2, 1028, 772 },
		.rotation = DRM_MODE_ROTATE_0,
		.min_scale = DRM_PLANE_NO_SCALING,
		.max_scale = 0x10001,
		.can_position = false,
		.src_expected = { 0x40002, 0x40002, 1024 << 16, 768 << 16 },
		.crtc_expected = { 0, 0, 1024, 768 },
	},
	{
		.name = "rounding3",
		.msg = "Should succeed by clipping to exact multiple",
		.src = { 0, 0, 0x3ffff, 0x3ffff },
		.crtc = { 1022, 766, 4, 4 },
		.rotation = DRM_MODE_ROTATE_0,
		.min_scale = 0xffff,
		.max_scale = DRM_PLANE_NO_SCALING,
		.can_position = true,
		/* Should not be rounded to 0x20001, which would be upscaling. */
		.src_expected = { 0, 0, 2 << 16, 2 << 16 },
		.crtc_expected = { 1022, 766, 2, 2 },
	},
	{
		.name = "rounding4",
		.msg = "Should succeed by clipping to exact multiple",
		.src = { 0x1ffff, 0x1ffff, 0x403ffff, 0x303ffff },
		.crtc = { -2, -2, 1028, 772 },
		.rotation = DRM_MODE_ROTATE_0,
		.min_scale = 0xffff,
		.max_scale = DRM_PLANE_NO_SCALING,
		.can_position = false,
		.src_expected = { 0x3fffe, 0x3fffe, 1024 << 16, 768 << 16 },
		.crtc_expected = { 0, 0, 1024, 768 },
	},
};

KUNIT_ARRAY_PARAM(drm_check_plane_state, drm_check_plane_state_tests, drm_check_plane_state_desc);

static void drm_test_check_invalid_plane_state(struct kunit *test)
{
	const struct drm_check_plane_state_test *params = test->param_value;
	struct drm_plane_state *plane_state = test->priv;

	KUNIT_ASSERT_LT_MSG(test,
			    drm_atomic_helper_check_plane_state(plane_state, &crtc_state,
								params->min_scale,
								params->max_scale,
								params->can_position, false),
			    0, params->msg);
}

static const struct drm_check_plane_state_test drm_check_invalid_plane_state_tests[] = {
	{
		.name = "positioning_invalid",
		.msg = "Should not be able to position on the crtc with can_position=false",
		.src = { 0, 0, 1023 << 16, 767 << 16 },
		.crtc = { 0, 0, 1023, 767 },
		.rotation = DRM_MODE_ROTATE_0,
		.min_scale = DRM_PLANE_NO_SCALING,
		.max_scale = DRM_PLANE_NO_SCALING,
		.can_position = false,
	},
	{
		.name = "upscaling_invalid",
		.msg = "Upscaling out of range should fail",
		.src = { 0, 0, 512 << 16, 384 << 16 },
		.crtc = { 0, 0, 1024, 768 },
		.rotation = DRM_MODE_ROTATE_0,
		.min_scale = 0x8001,
		.max_scale = DRM_PLANE_NO_SCALING,
		.can_position = false,
	},
	{
		.name = "downscaling_invalid",
		.msg = "Downscaling out of range should fail",
		.src = { 0, 0, 2048 << 16, 1536 << 16 },
		.crtc = { 0, 0, 1024, 768 },
		.rotation = DRM_MODE_ROTATE_0,
		.min_scale = DRM_PLANE_NO_SCALING,
		.max_scale = 0x1ffff,
		.can_position = false,
	},
};

KUNIT_ARRAY_PARAM(drm_check_invalid_plane_state, drm_check_invalid_plane_state_tests,
		  drm_check_plane_state_desc);

static struct kunit_case drm_plane_helper_test[] = {
	KUNIT_CASE_PARAM(drm_test_check_plane_state, drm_check_plane_state_gen_params),
	KUNIT_CASE_PARAM(drm_test_check_invalid_plane_state,
			 drm_check_invalid_plane_state_gen_params),
	{}
};

static struct kunit_suite drm_plane_helper_test_suite = {
	.name = "drm_plane_helper",
	.init = drm_plane_helper_init,
	.test_cases = drm_plane_helper_test,
};

kunit_test_suite(drm_plane_helper_test_suite);

MODULE_LICENSE("GPL");
