// SPDX-License-Identifier: GPL-2.0
/*
 * Test cases for the drm_rect functions
 *
 * Copyright (c) 2022 Maíra Canal <mairacanal@riseup.net>
 */

#include <kunit/test.h>

#include <drm/drm_rect.h>
#include <drm/drm_mode.h>

#include <linux/string_helpers.h>
#include <linux/errno.h>

static void drm_rect_compare(struct kunit *test, const struct drm_rect *r,
			     const struct drm_rect *expected)
{
	KUNIT_EXPECT_EQ(test, r->x1, expected->x1);
	KUNIT_EXPECT_EQ(test, r->y1, expected->y1);
	KUNIT_EXPECT_EQ(test, drm_rect_width(r), drm_rect_width(expected));
	KUNIT_EXPECT_EQ(test, drm_rect_height(r), drm_rect_height(expected));
}

static void drm_test_rect_clip_scaled_div_by_zero(struct kunit *test)
{
	struct drm_rect src, dst, clip;
	bool visible;

	/*
	 * Make sure we don't divide by zero when dst
	 * width/height is zero and dst and clip do not intersect.
	 */
	drm_rect_init(&src, 0, 0, 0, 0);
	drm_rect_init(&dst, 0, 0, 0, 0);
	drm_rect_init(&clip, 1, 1, 1, 1);
	visible = drm_rect_clip_scaled(&src, &dst, &clip);

	KUNIT_EXPECT_FALSE_MSG(test, visible, "Destination not be visible\n");
	KUNIT_EXPECT_FALSE_MSG(test, drm_rect_visible(&src), "Source should not be visible\n");

	drm_rect_init(&src, 0, 0, 0, 0);
	drm_rect_init(&dst, 3, 3, 0, 0);
	drm_rect_init(&clip, 1, 1, 1, 1);
	visible = drm_rect_clip_scaled(&src, &dst, &clip);

	KUNIT_EXPECT_FALSE_MSG(test, visible, "Destination not be visible\n");
	KUNIT_EXPECT_FALSE_MSG(test, drm_rect_visible(&src), "Source should not be visible\n");
}

static void drm_test_rect_clip_scaled_not_clipped(struct kunit *test)
{
	struct drm_rect src, dst, clip;
	bool visible;

	/* 1:1 scaling */
	drm_rect_init(&src, 0, 0, 1 << 16, 1 << 16);
	drm_rect_init(&dst, 0, 0, 1, 1);
	drm_rect_init(&clip, 0, 0, 1, 1);

	visible = drm_rect_clip_scaled(&src, &dst, &clip);

	KUNIT_EXPECT_FALSE_MSG(test, src.x1 != 0 || src.x2 != 1 << 16 ||
			       src.y1 != 0 || src.y2 != 1 << 16, "Source badly clipped\n");
	KUNIT_EXPECT_FALSE_MSG(test, dst.x1 != 0 || dst.x2 != 1 ||
			       dst.y1 != 0 || dst.y2 != 1, "Destination badly clipped\n");
	KUNIT_EXPECT_TRUE_MSG(test, visible, "Destination should be visible\n");
	KUNIT_EXPECT_TRUE_MSG(test, drm_rect_visible(&src), "Source should be visible\n");

	/* 2:1 scaling */
	drm_rect_init(&src, 0, 0, 2 << 16, 2 << 16);
	drm_rect_init(&dst, 0, 0, 1, 1);
	drm_rect_init(&clip, 0, 0, 1, 1);

	visible = drm_rect_clip_scaled(&src, &dst, &clip);

	KUNIT_EXPECT_FALSE_MSG(test, src.x1 != 0 || src.x2 != 2 << 16 ||
			       src.y1 != 0 || src.y2 != 2 << 16, "Source badly clipped\n");
	KUNIT_EXPECT_FALSE_MSG(test, dst.x1 != 0 || dst.x2 != 1 ||
			       dst.y1 != 0 || dst.y2 != 1, "Destination badly clipped\n");
	KUNIT_EXPECT_TRUE_MSG(test, visible, "Destination should be visible\n");
	KUNIT_EXPECT_TRUE_MSG(test, drm_rect_visible(&src), "Source should be visible\n");

	/* 1:2 scaling */
	drm_rect_init(&src, 0, 0, 1 << 16, 1 << 16);
	drm_rect_init(&dst, 0, 0, 2, 2);
	drm_rect_init(&clip, 0, 0, 2, 2);

	visible = drm_rect_clip_scaled(&src, &dst, &clip);

	KUNIT_EXPECT_FALSE_MSG(test, src.x1 != 0 || src.x2 != 1 << 16 ||
			       src.y1 != 0 || src.y2 != 1 << 16, "Source badly clipped\n");
	KUNIT_EXPECT_FALSE_MSG(test, dst.x1 != 0 || dst.x2 != 2 ||
			       dst.y1 != 0 || dst.y2 != 2, "Destination badly clipped\n");
	KUNIT_EXPECT_TRUE_MSG(test, visible, "Destination should be visible\n");
	KUNIT_EXPECT_TRUE_MSG(test, drm_rect_visible(&src), "Source should be visible\n");
}

static void drm_test_rect_clip_scaled_clipped(struct kunit *test)
{
	struct drm_rect src, dst, clip;
	bool visible;

	/* 1:1 scaling top/left clip */
	drm_rect_init(&src, 0, 0, 2 << 16, 2 << 16);
	drm_rect_init(&dst, 0, 0, 2, 2);
	drm_rect_init(&clip, 0, 0, 1, 1);

	visible = drm_rect_clip_scaled(&src, &dst, &clip);

	KUNIT_EXPECT_FALSE_MSG(test, src.x1 != 0 || src.x2 != 1 << 16 ||
			       src.y1 != 0 || src.y2 != 1 << 16, "Source badly clipped\n");
	KUNIT_EXPECT_FALSE_MSG(test, dst.x1 != 0 || dst.x2 != 1 ||
			       dst.y1 != 0 || dst.y2 != 1, "Destination badly clipped\n");
	KUNIT_EXPECT_TRUE_MSG(test, visible, "Destination should be visible\n");
	KUNIT_EXPECT_TRUE_MSG(test, drm_rect_visible(&src), "Source should be visible\n");

	/* 1:1 scaling bottom/right clip */
	drm_rect_init(&src, 0, 0, 2 << 16, 2 << 16);
	drm_rect_init(&dst, 0, 0, 2, 2);
	drm_rect_init(&clip, 1, 1, 1, 1);

	visible = drm_rect_clip_scaled(&src, &dst, &clip);

	KUNIT_EXPECT_FALSE_MSG(test, src.x1 != 1 << 16 || src.x2 != 2 << 16 ||
			       src.y1 != 1 << 16 || src.y2 != 2 << 16, "Source badly clipped\n");
	KUNIT_EXPECT_FALSE_MSG(test, dst.x1 != 1 || dst.x2 != 2 || dst.y1 != 1 ||
			       dst.y2 != 2, "Destination badly clipped\n");
	KUNIT_EXPECT_TRUE_MSG(test, visible, "Destination should be visible\n");
	KUNIT_EXPECT_TRUE_MSG(test, drm_rect_visible(&src), "Source should be visible\n");

	/* 2:1 scaling top/left clip */
	drm_rect_init(&src, 0, 0, 4 << 16, 4 << 16);
	drm_rect_init(&dst, 0, 0, 2, 2);
	drm_rect_init(&clip, 0, 0, 1, 1);

	visible = drm_rect_clip_scaled(&src, &dst, &clip);

	KUNIT_EXPECT_FALSE_MSG(test, src.x1 != 0 || src.x2 != 2 << 16 ||
			       src.y1 != 0 || src.y2 != 2 << 16, "Source badly clipped\n");
	KUNIT_EXPECT_FALSE_MSG(test, dst.x1 != 0 || dst.x2 != 1 || dst.y1 != 0 ||
			       dst.y2 != 1, "Destination badly clipped\n");
	KUNIT_EXPECT_TRUE_MSG(test, visible, "Destination should be visible\n");
	KUNIT_EXPECT_TRUE_MSG(test, drm_rect_visible(&src), "Source should be visible\n");

	/* 2:1 scaling bottom/right clip */
	drm_rect_init(&src, 0, 0, 4 << 16, 4 << 16);
	drm_rect_init(&dst, 0, 0, 2, 2);
	drm_rect_init(&clip, 1, 1, 1, 1);

	visible = drm_rect_clip_scaled(&src, &dst, &clip);

	KUNIT_EXPECT_FALSE_MSG(test, src.x1 != 2 << 16 || src.x2 != 4 << 16 ||
			       src.y1 != 2 << 16 || src.y2 != 4 << 16, "Source badly clipped\n");
	KUNIT_EXPECT_FALSE_MSG(test, dst.x1 != 1 || dst.x2 != 2 || dst.y1 != 1 ||
			       dst.y2 != 2, "Destination badly clipped\n");
	KUNIT_EXPECT_TRUE_MSG(test, visible, "Destination should be visible\n");
	KUNIT_EXPECT_TRUE_MSG(test, drm_rect_visible(&src), "Source should be visible\n");

	/* 1:2 scaling top/left clip */
	drm_rect_init(&src, 0, 0, 2 << 16, 2 << 16);
	drm_rect_init(&dst, 0, 0, 4, 4);
	drm_rect_init(&clip, 0, 0, 2, 2);

	visible = drm_rect_clip_scaled(&src, &dst, &clip);

	KUNIT_EXPECT_FALSE_MSG(test, src.x1 != 0 || src.x2 != 1 << 16 ||
			       src.y1 != 0 || src.y2 != 1 << 16, "Source badly clipped\n");
	KUNIT_EXPECT_FALSE_MSG(test, dst.x1 != 0 || dst.x2 != 2 || dst.y1 != 0 ||
			       dst.y2 != 2, "Destination badly clipped\n");
	KUNIT_EXPECT_TRUE_MSG(test, visible, "Destination should be visible\n");
	KUNIT_EXPECT_TRUE_MSG(test, drm_rect_visible(&src), "Source should be visible\n");

	/* 1:2 scaling bottom/right clip */
	drm_rect_init(&src, 0, 0, 2 << 16, 2 << 16);
	drm_rect_init(&dst, 0, 0, 4, 4);
	drm_rect_init(&clip, 2, 2, 2, 2);

	visible = drm_rect_clip_scaled(&src, &dst, &clip);

	KUNIT_EXPECT_FALSE_MSG(test, src.x1 != 1 << 16 || src.x2 != 2 << 16 ||
			       src.y1 != 1 << 16 || src.y2 != 2 << 16, "Source badly clipped\n");
	KUNIT_EXPECT_FALSE_MSG(test, dst.x1 != 2 || dst.x2 != 4 || dst.y1 != 2 ||
			       dst.y2 != 4, "Destination badly clipped\n");
	KUNIT_EXPECT_TRUE_MSG(test, visible, "Destination should be visible\n");
	KUNIT_EXPECT_TRUE_MSG(test, drm_rect_visible(&src), "Source should be visible\n");
}

static void drm_test_rect_clip_scaled_signed_vs_unsigned(struct kunit *test)
{
	struct drm_rect src, dst, clip;
	bool visible;

	/*
	 * 'clip.x2 - dst.x1 >= dst width' could result a negative
	 * src rectangle width which is no longer expected by the
	 * code as it's using unsigned types. This could lead to
	 * the clipped source rectangle appering visible when it
	 * should have been fully clipped. Make sure both rectangles
	 * end up invisible.
	 */
	drm_rect_init(&src, 0, 0, INT_MAX, INT_MAX);
	drm_rect_init(&dst, 0, 0, 2, 2);
	drm_rect_init(&clip, 3, 3, 1, 1);

	visible = drm_rect_clip_scaled(&src, &dst, &clip);

	KUNIT_EXPECT_FALSE_MSG(test, visible, "Destination should not be visible\n");
	KUNIT_EXPECT_FALSE_MSG(test, drm_rect_visible(&src), "Source should not be visible\n");
}

struct drm_rect_intersect_case {
	const char *description;
	struct drm_rect r1, r2;
	bool should_be_visible;
	struct drm_rect expected_intersection;
};

static const struct drm_rect_intersect_case drm_rect_intersect_cases[] = {
	{
		.description = "top-left x bottom-right",
		.r1 = DRM_RECT_INIT(1, 1, 2, 2),
		.r2 = DRM_RECT_INIT(0, 0, 2, 2),
		.should_be_visible = true,
		.expected_intersection = DRM_RECT_INIT(1, 1, 1, 1),
	},
	{
		.description = "top-right x bottom-left",
		.r1 = DRM_RECT_INIT(0, 0, 2, 2),
		.r2 = DRM_RECT_INIT(1, -1, 2, 2),
		.should_be_visible = true,
		.expected_intersection = DRM_RECT_INIT(1, 0, 1, 1),
	},
	{
		.description = "bottom-left x top-right",
		.r1 = DRM_RECT_INIT(1, -1, 2, 2),
		.r2 = DRM_RECT_INIT(0, 0, 2, 2),
		.should_be_visible = true,
		.expected_intersection = DRM_RECT_INIT(1, 0, 1, 1),
	},
	{
		.description = "bottom-right x top-left",
		.r1 = DRM_RECT_INIT(0, 0, 2, 2),
		.r2 = DRM_RECT_INIT(1, 1, 2, 2),
		.should_be_visible = true,
		.expected_intersection = DRM_RECT_INIT(1, 1, 1, 1),
	},
	{
		.description = "right x left",
		.r1 = DRM_RECT_INIT(0, 0, 2, 1),
		.r2 = DRM_RECT_INIT(1, 0, 3, 1),
		.should_be_visible = true,
		.expected_intersection = DRM_RECT_INIT(1, 0, 1, 1),
	},
	{
		.description = "left x right",
		.r1 = DRM_RECT_INIT(1, 0, 3, 1),
		.r2 = DRM_RECT_INIT(0, 0, 2, 1),
		.should_be_visible = true,
		.expected_intersection = DRM_RECT_INIT(1, 0, 1, 1),
	},
	{
		.description = "up x bottom",
		.r1 = DRM_RECT_INIT(0, 0, 1, 2),
		.r2 = DRM_RECT_INIT(0, -1, 1, 3),
		.should_be_visible = true,
		.expected_intersection = DRM_RECT_INIT(0, 0, 1, 2),
	},
	{
		.description = "bottom x up",
		.r1 = DRM_RECT_INIT(0, -1, 1, 3),
		.r2 = DRM_RECT_INIT(0, 0, 1, 2),
		.should_be_visible = true,
		.expected_intersection = DRM_RECT_INIT(0, 0, 1, 2),
	},
	{
		.description = "touching corner",
		.r1 = DRM_RECT_INIT(0, 0, 1, 1),
		.r2 = DRM_RECT_INIT(1, 1, 2, 2),
		.should_be_visible = false,
		.expected_intersection = DRM_RECT_INIT(1, 1, 0, 0),
	},
	{
		.description = "touching side",
		.r1 = DRM_RECT_INIT(0, 0, 1, 1),
		.r2 = DRM_RECT_INIT(1, 0, 1, 1),
		.should_be_visible = false,
		.expected_intersection = DRM_RECT_INIT(1, 0, 0, 1),
	},
	{
		.description = "equal rects",
		.r1 = DRM_RECT_INIT(0, 0, 2, 2),
		.r2 = DRM_RECT_INIT(0, 0, 2, 2),
		.should_be_visible = true,
		.expected_intersection = DRM_RECT_INIT(0, 0, 2, 2),
	},
	{
		.description = "inside another",
		.r1 = DRM_RECT_INIT(0, 0, 2, 2),
		.r2 = DRM_RECT_INIT(1, 1, 1, 1),
		.should_be_visible = true,
		.expected_intersection = DRM_RECT_INIT(1, 1, 1, 1),
	},
	{
		.description = "far away",
		.r1 = DRM_RECT_INIT(0, 0, 1, 1),
		.r2 = DRM_RECT_INIT(3, 6, 1, 1),
		.should_be_visible = false,
		.expected_intersection = DRM_RECT_INIT(3, 6, -2, -5),
	},
	{
		.description = "points intersecting",
		.r1 = DRM_RECT_INIT(5, 10, 0, 0),
		.r2 = DRM_RECT_INIT(5, 10, 0, 0),
		.should_be_visible = false,
		.expected_intersection = DRM_RECT_INIT(5, 10, 0, 0),
	},
	{
		.description = "points not intersecting",
		.r1 = DRM_RECT_INIT(0, 0, 0, 0),
		.r2 = DRM_RECT_INIT(5, 10, 0, 0),
		.should_be_visible = false,
		.expected_intersection = DRM_RECT_INIT(5, 10, -5, -10),
	},
};

static void drm_rect_intersect_case_desc(const struct drm_rect_intersect_case *t, char *desc)
{
	snprintf(desc, KUNIT_PARAM_DESC_SIZE,
		 "%s: " DRM_RECT_FMT " x " DRM_RECT_FMT,
		 t->description, DRM_RECT_ARG(&t->r1), DRM_RECT_ARG(&t->r2));
}

KUNIT_ARRAY_PARAM(drm_rect_intersect, drm_rect_intersect_cases, drm_rect_intersect_case_desc);

static void drm_test_rect_intersect(struct kunit *test)
{
	const struct drm_rect_intersect_case *params = test->param_value;
	struct drm_rect r1_aux = params->r1;
	bool visible;

	visible = drm_rect_intersect(&r1_aux, &params->r2);

	KUNIT_EXPECT_EQ(test, visible, params->should_be_visible);
	drm_rect_compare(test, &r1_aux, &params->expected_intersection);
}

struct drm_rect_scale_case {
	const char *name;
	struct drm_rect src, dst;
	int min_range, max_range;
	int expected_scaling_factor;
};

static const struct drm_rect_scale_case drm_rect_scale_cases[] = {
	{
		.name = "normal use",
		.src = DRM_RECT_INIT(0, 0, 2 << 16, 2 << 16),
		.dst = DRM_RECT_INIT(0, 0, 1 << 16, 1 << 16),
		.min_range = 0, .max_range = INT_MAX,
		.expected_scaling_factor = 2,
	},
	{
		.name = "out of max range",
		.src = DRM_RECT_INIT(0, 0, 10 << 16, 10 << 16),
		.dst = DRM_RECT_INIT(0, 0, 1 << 16, 1 << 16),
		.min_range = 3, .max_range = 5,
		.expected_scaling_factor = -ERANGE,
	},
	{
		.name = "out of min range",
		.src = DRM_RECT_INIT(0, 0, 2 << 16, 2 << 16),
		.dst = DRM_RECT_INIT(0, 0, 1 << 16, 1 << 16),
		.min_range = 3, .max_range = 5,
		.expected_scaling_factor = -ERANGE,
	},
	{
		.name = "zero dst",
		.src = DRM_RECT_INIT(0, 0, 2 << 16, 2 << 16),
		.dst = DRM_RECT_INIT(0, 0, 0 << 16, 0 << 16),
		.min_range = 0, .max_range = INT_MAX,
		.expected_scaling_factor = 0,
	},
	{
		.name = "negative src",
		.src = DRM_RECT_INIT(0, 0, -(1 << 16), -(1 << 16)),
		.dst = DRM_RECT_INIT(0, 0, 1 << 16, 1 << 16),
		.min_range = 0, .max_range = INT_MAX,
		.expected_scaling_factor = -EINVAL,
	},
	{
		.name = "negative dst",
		.src = DRM_RECT_INIT(0, 0, 1 << 16, 1 << 16),
		.dst = DRM_RECT_INIT(0, 0, -(1 << 16), -(1 << 16)),
		.min_range = 0, .max_range = INT_MAX,
		.expected_scaling_factor = -EINVAL,
	},
};

static void drm_rect_scale_case_desc(const struct drm_rect_scale_case *t, char *desc)
{
	strscpy(desc, t->name, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(drm_rect_scale, drm_rect_scale_cases, drm_rect_scale_case_desc);

static void drm_test_rect_calc_hscale(struct kunit *test)
{
	const struct drm_rect_scale_case *params = test->param_value;
	int scaling_factor;

	scaling_factor = drm_rect_calc_hscale(&params->src, &params->dst,
					      params->min_range, params->max_range);

	KUNIT_EXPECT_EQ(test, scaling_factor, params->expected_scaling_factor);
}

static void drm_test_rect_calc_vscale(struct kunit *test)
{
	const struct drm_rect_scale_case *params = test->param_value;
	int scaling_factor;

	scaling_factor = drm_rect_calc_vscale(&params->src, &params->dst,
					      params->min_range, params->max_range);

	KUNIT_EXPECT_EQ(test, scaling_factor, params->expected_scaling_factor);
}

struct drm_rect_rotate_case {
	const char *name;
	unsigned int rotation;
	struct drm_rect rect;
	int width, height;
	struct drm_rect expected;
};

static const struct drm_rect_rotate_case drm_rect_rotate_cases[] = {
	{
		.name = "reflect-x",
		.rotation = DRM_MODE_REFLECT_X,
		.rect = DRM_RECT_INIT(0, 0, 5, 5),
		.width = 5, .height = 10,
		.expected = DRM_RECT_INIT(0, 0, 5, 5),
	},
	{
		.name = "reflect-y",
		.rotation = DRM_MODE_REFLECT_Y,
		.rect = DRM_RECT_INIT(2, 0, 5, 5),
		.width = 5, .height = 10,
		.expected = DRM_RECT_INIT(2, 5, 5, 5),
	},
	{
		.name = "rotate-0",
		.rotation = DRM_MODE_ROTATE_0,
		.rect = DRM_RECT_INIT(0, 2, 5, 5),
		.width = 5, .height = 10,
		.expected = DRM_RECT_INIT(0, 2, 5, 5),
	},
	{
		.name = "rotate-90",
		.rotation = DRM_MODE_ROTATE_90,
		.rect = DRM_RECT_INIT(0, 0, 5, 10),
		.width = 5, .height = 10,
		.expected = DRM_RECT_INIT(0, 0, 10, 5),
	},
	{
		.name = "rotate-180",
		.rotation = DRM_MODE_ROTATE_180,
		.rect = DRM_RECT_INIT(11, 3, 5, 10),
		.width = 5, .height = 10,
		.expected = DRM_RECT_INIT(-11, -3, 5, 10),
	},
	{
		.name = "rotate-270",
		.rotation = DRM_MODE_ROTATE_270,
		.rect = DRM_RECT_INIT(6, 3, 5, 10),
		.width = 5, .height = 10,
		.expected = DRM_RECT_INIT(-3, 6, 10, 5),
	},
};

static void drm_rect_rotate_case_desc(const struct drm_rect_rotate_case *t, char *desc)
{
	strscpy(desc, t->name, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(drm_rect_rotate, drm_rect_rotate_cases, drm_rect_rotate_case_desc);

static void drm_test_rect_rotate(struct kunit *test)
{
	const struct drm_rect_rotate_case *params = test->param_value;
	struct drm_rect r = params->rect;

	drm_rect_rotate(&r, params->width, params->height, params->rotation);

	drm_rect_compare(test, &r, &params->expected);
}

static void drm_test_rect_rotate_inv(struct kunit *test)
{
	const struct drm_rect_rotate_case *params = test->param_value;
	struct drm_rect r = params->expected;

	drm_rect_rotate_inv(&r, params->width, params->height, params->rotation);

	drm_rect_compare(test, &r, &params->rect);
}

static struct kunit_case drm_rect_tests[] = {
	KUNIT_CASE(drm_test_rect_clip_scaled_div_by_zero),
	KUNIT_CASE(drm_test_rect_clip_scaled_not_clipped),
	KUNIT_CASE(drm_test_rect_clip_scaled_clipped),
	KUNIT_CASE(drm_test_rect_clip_scaled_signed_vs_unsigned),
	KUNIT_CASE_PARAM(drm_test_rect_intersect, drm_rect_intersect_gen_params),
	KUNIT_CASE_PARAM(drm_test_rect_calc_hscale, drm_rect_scale_gen_params),
	KUNIT_CASE_PARAM(drm_test_rect_calc_vscale, drm_rect_scale_gen_params),
	KUNIT_CASE_PARAM(drm_test_rect_rotate, drm_rect_rotate_gen_params),
	KUNIT_CASE_PARAM(drm_test_rect_rotate_inv, drm_rect_rotate_gen_params),
	{ }
};

static struct kunit_suite drm_rect_test_suite = {
	.name = "drm_rect",
	.test_cases = drm_rect_tests,
};

kunit_test_suite(drm_rect_test_suite);

MODULE_DESCRIPTION("Test cases for the drm_rect functions");
MODULE_LICENSE("GPL");
