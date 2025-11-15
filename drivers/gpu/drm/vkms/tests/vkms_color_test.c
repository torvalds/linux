// SPDX-License-Identifier: GPL-2.0+

#include <kunit/test.h>

#include <drm/drm_fixed.h>
#include <drm/drm_mode.h>
#include "../vkms_composer.h"
#include "../vkms_drv.h"
#include "../vkms_luts.h"

#define TEST_LUT_SIZE 16

MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");

static struct drm_color_lut test_linear_array[TEST_LUT_SIZE] = {
	{ 0x0, 0x0, 0x0, 0 },
	{ 0x1111, 0x1111, 0x1111, 0 },
	{ 0x2222, 0x2222, 0x2222, 0 },
	{ 0x3333, 0x3333, 0x3333, 0 },
	{ 0x4444, 0x4444, 0x4444, 0 },
	{ 0x5555, 0x5555, 0x5555, 0 },
	{ 0x6666, 0x6666, 0x6666, 0 },
	{ 0x7777, 0x7777, 0x7777, 0 },
	{ 0x8888, 0x8888, 0x8888, 0 },
	{ 0x9999, 0x9999, 0x9999, 0 },
	{ 0xaaaa, 0xaaaa, 0xaaaa, 0 },
	{ 0xbbbb, 0xbbbb, 0xbbbb, 0 },
	{ 0xcccc, 0xcccc, 0xcccc, 0 },
	{ 0xdddd, 0xdddd, 0xdddd, 0 },
	{ 0xeeee, 0xeeee, 0xeeee, 0 },
	{ 0xffff, 0xffff, 0xffff, 0 },
};

/* lerp test parameters */
struct vkms_color_test_lerp_params {
	s64 t;
	__u16 a;
	__u16 b;
	__u16 expected;
};

/* lerp test cases */
static const struct vkms_color_test_lerp_params color_test_lerp_cases[] = {
	/* Half-way round down */
	{ 0x80000000 - 1, 0x0, 0x10, 0x8 },
	{ 0x80000000 - 1, 0x1, 0x10, 0x8 },	/* Odd a */
	{ 0x80000000 - 1, 0x1, 0xf, 0x8 },	/* Odd b */
	{ 0x80000000 - 1, 0x10, 0x10, 0x10 },	/* b = a */
	{ 0x80000000 - 1, 0x10, 0x11, 0x10 },	/* b = a + 1*/
	/* Half-way round up */
	{ 0x80000000, 0x0, 0x10, 0x8 },
	{ 0x80000000, 0x1, 0x10, 0x9 },		/* Odd a */
	{ 0x80000000, 0x1, 0xf, 0x8 },		/* Odd b */
	{ 0x80000000, 0x10, 0x10, 0x10 },	/* b = a */
	{ 0x80000000, 0x10, 0x11, 0x11 },	/* b = a + 1*/
	/*  t = 0.0 */
	{ 0x0, 0x0, 0x10, 0x0 },
	{ 0x0, 0x1, 0x10, 0x1 },		/* Odd a */
	{ 0x0, 0x1, 0xf, 0x1 },			/* Odd b */
	{ 0x0, 0x10, 0x10, 0x10 },		/* b = a */
	{ 0x0, 0x10, 0x11, 0x10 },		/* b = a + 1*/
	/*  t = 1.0 */
	{ 0x100000000, 0x0, 0x10, 0x10 },
	{ 0x100000000, 0x1, 0x10, 0x10 },	/* Odd a */
	{ 0x100000000, 0x1, 0xf, 0xf },		/* Odd b */
	{ 0x100000000, 0x10, 0x10, 0x10 },	/* b = a */
	{ 0x100000000, 0x10, 0x11, 0x11 },	/* b = a + 1*/
	/*  t = 0.0 + 1 */
	{ 0x0 + 1, 0x0, 0x10, 0x0 },
	{ 0x0 + 1, 0x1, 0x10, 0x1 },		/* Odd a */
	{ 0x0 + 1, 0x1, 0xf, 0x1 },		/* Odd b */
	{ 0x0 + 1, 0x10, 0x10, 0x10 },		/* b = a */
	{ 0x0 + 1, 0x10, 0x11, 0x10 },		/* b = a + 1*/
	/*  t = 1.0 - 1 */
	{ 0x100000000 - 1, 0x0, 0x10, 0x10 },
	{ 0x100000000 - 1, 0x1, 0x10, 0x10 },	/* Odd a */
	{ 0x100000000 - 1, 0x1, 0xf, 0xf },	/* Odd b */
	{ 0x100000000 - 1, 0x10, 0x10, 0x10 },	/* b = a */
	{ 0x100000000 - 1, 0x10, 0x11, 0x11 },	/* b = a + 1*/
	/*  t chosen to verify the flipping point of result a (or b) to a+1 (or b-1) */
	{ 0x80000000 - 1, 0x0, 0x1, 0x0 },
	{ 0x80000000, 0x0, 0x1, 0x1 },
};

static const struct vkms_color_lut test_linear_lut = {
	.base = test_linear_array,
	.lut_length = TEST_LUT_SIZE,
	.channel_value2index_ratio = 0xf000fll
};

static void vkms_color_test_get_lut_index(struct kunit *test)
{
	s64 lut_index;
	int i;

	lut_index = get_lut_index(&test_linear_lut, test_linear_array[0].red);
	KUNIT_EXPECT_EQ(test, drm_fixp2int(lut_index), 0);

	for (i = 0; i < TEST_LUT_SIZE; i++) {
		lut_index = get_lut_index(&test_linear_lut, test_linear_array[i].red);
		KUNIT_EXPECT_EQ(test, drm_fixp2int_ceil(lut_index), i);
	}

	KUNIT_EXPECT_EQ(test, drm_fixp2int(get_lut_index(&srgb_eotf, 0x0)), 0x0);
	KUNIT_EXPECT_EQ(test, drm_fixp2int_ceil(get_lut_index(&srgb_eotf, 0x0)), 0x0);
	KUNIT_EXPECT_EQ(test, drm_fixp2int_ceil(get_lut_index(&srgb_eotf, 0x101)), 0x1);
	KUNIT_EXPECT_EQ(test, drm_fixp2int_ceil(get_lut_index(&srgb_eotf, 0x202)), 0x2);

	KUNIT_EXPECT_EQ(test, drm_fixp2int(get_lut_index(&srgb_inv_eotf, 0x0)), 0x0);
	KUNIT_EXPECT_EQ(test, drm_fixp2int_ceil(get_lut_index(&srgb_inv_eotf, 0x0)), 0x0);
	KUNIT_EXPECT_EQ(test, drm_fixp2int_ceil(get_lut_index(&srgb_inv_eotf, 0x101)), 0x1);
	KUNIT_EXPECT_EQ(test, drm_fixp2int_ceil(get_lut_index(&srgb_inv_eotf, 0x202)), 0x2);

	KUNIT_EXPECT_EQ(test, drm_fixp2int_ceil(get_lut_index(&srgb_eotf, 0xfefe)), 0xfe);
	KUNIT_EXPECT_EQ(test, drm_fixp2int_ceil(get_lut_index(&srgb_eotf, 0xffff)), 0xff);
}

static void vkms_color_test_lerp(struct kunit *test)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(color_test_lerp_cases); i++) {
		const struct vkms_color_test_lerp_params *params = &color_test_lerp_cases[i];

		KUNIT_EXPECT_EQ(test, lerp_u16(params->a, params->b, params->t), params->expected);
	}
}

static void vkms_color_test_linear(struct kunit *test)
{
	for (int i = 0; i < LUT_SIZE; i++) {
		int linear = apply_lut_to_channel_value(&linear_eotf, i * 0x101, LUT_RED);

		KUNIT_EXPECT_EQ(test, DIV_ROUND_CLOSEST(linear, 0x101), i);
	}
}

static void vkms_color_srgb_inv_srgb(struct kunit *test)
{
	u16 srgb, final;

	for (int i = 0; i < LUT_SIZE; i++) {
		srgb = apply_lut_to_channel_value(&srgb_eotf, i * 0x101, LUT_RED);
		final = apply_lut_to_channel_value(&srgb_inv_eotf, srgb, LUT_RED);

		KUNIT_EXPECT_GE(test, final / 0x101, i - 1);
		KUNIT_EXPECT_LE(test, final / 0x101, i + 1);
	}
}

static struct kunit_case vkms_color_test_cases[] = {
	KUNIT_CASE(vkms_color_test_get_lut_index),
	KUNIT_CASE(vkms_color_test_lerp),
	KUNIT_CASE(vkms_color_test_linear),
	KUNIT_CASE(vkms_color_srgb_inv_srgb),
	{}
};

static struct kunit_suite vkms_color_test_suite = {
	.name = "vkms-color",
	.test_cases = vkms_color_test_cases,
};

kunit_test_suite(vkms_color_test_suite);

MODULE_DESCRIPTION("Kunit test for VKMS LUT handling");
MODULE_LICENSE("GPL");
