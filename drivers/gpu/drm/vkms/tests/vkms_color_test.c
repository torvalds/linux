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

#define FIXPT_HALF        (DRM_FIXED_ONE >> 1)
#define FIXPT_QUARTER     (DRM_FIXED_ONE >> 2)

static const struct drm_color_ctm_3x4 test_matrix_3x4_50_desat = { {
	FIXPT_HALF, FIXPT_QUARTER, FIXPT_QUARTER, 0,
	FIXPT_QUARTER, FIXPT_HALF, FIXPT_QUARTER, 0,
	FIXPT_QUARTER, FIXPT_QUARTER, FIXPT_HALF, 0
} };

static void vkms_color_ctm_3x4_50_desat(struct kunit *test)
{
	struct pixel_argb_s32 ref, out;

	/* full white */
	ref.a = 0xffff;
	ref.r = 0xffff;
	ref.g = 0xffff;
	ref.b = 0xffff;

	memcpy(&out, &ref, sizeof(out));
	apply_3x4_matrix(&out, &test_matrix_3x4_50_desat);

	KUNIT_EXPECT_MEMEQ(test, &ref, &out, sizeof(out));

	/* full black */
	ref.a = 0xffff;
	ref.r = 0x0;
	ref.g = 0x0;
	ref.b = 0x0;

	memcpy(&out, &ref, sizeof(out));
	apply_3x4_matrix(&out, &test_matrix_3x4_50_desat);

	KUNIT_EXPECT_MEMEQ(test, &ref, &out, sizeof(out));

	/* 50% grey */
	ref.a = 0xffff;
	ref.r = 0x8000;
	ref.g = 0x8000;
	ref.b = 0x8000;

	memcpy(&out, &ref, sizeof(out));
	apply_3x4_matrix(&out, &test_matrix_3x4_50_desat);

	KUNIT_EXPECT_MEMEQ(test, &ref, &out, sizeof(out));

	/* full red to 50% desat */
	ref.a = 0xffff;
	ref.r = 0x8000;
	ref.g = 0x4000;
	ref.b = 0x4000;

	out.a = 0xffff;
	out.r = 0xffff;
	out.g = 0x0;
	out.b = 0x0;

	apply_3x4_matrix(&out, &test_matrix_3x4_50_desat);

	KUNIT_EXPECT_MEMEQ(test, &ref, &out, sizeof(out));
}

/*
 * BT.709 encoding matrix
 *
 * Values printed from within IGT when converting
 * igt_matrix_3x4_bt709_enc to the fixed-point format expected
 * by DRM/KMS.
 */
static const struct drm_color_ctm_3x4 test_matrix_3x4_bt709_enc = { {
	0x00000000366cf400ull, 0x00000000b7175900ull, 0x0000000127bb300ull, 0,
	0x800000001993b3a0ull, 0x800000005609fe80ull, 0x000000006f9db200ull, 0,
	0x000000009d70a400ull, 0x800000008f011100ull, 0x800000000e6f9330ull, 0
} };

static void vkms_color_ctm_3x4_bt709(struct kunit *test)
{
	struct pixel_argb_s32 out;

	/* full white to bt709 */
	out.a = 0xffff;
	out.r = 0xffff;
	out.g = 0xffff;
	out.b = 0xffff;

	apply_3x4_matrix(&out, &test_matrix_3x4_bt709_enc);

	/* Y 255 */
	KUNIT_EXPECT_GT(test, out.r, 0xfe00);
	KUNIT_EXPECT_LT(test, out.r, 0x10000);

	/* U 0 */
	KUNIT_EXPECT_LT(test, out.g, 0x0100);

	/* V 0 */
	KUNIT_EXPECT_LT(test, out.b, 0x0100);

	/* full black to bt709 */
	out.a = 0xffff;
	out.r = 0x0;
	out.g = 0x0;
	out.b = 0x0;

	apply_3x4_matrix(&out, &test_matrix_3x4_bt709_enc);

	/* Y 0 */
	KUNIT_EXPECT_LT(test, out.r, 0x100);

	/* U 0 */
	KUNIT_EXPECT_LT(test, out.g, 0x0100);

	/* V 0 */
	KUNIT_EXPECT_LT(test, out.b, 0x0100);

	/* gray to bt709 */
	out.a = 0xffff;
	out.r = 0x7fff;
	out.g = 0x7fff;
	out.b = 0x7fff;

	apply_3x4_matrix(&out, &test_matrix_3x4_bt709_enc);

	/* Y 127 */
	KUNIT_EXPECT_GT(test, out.r, 0x7e00);
	KUNIT_EXPECT_LT(test, out.r, 0x8000);

	/* U 0 */
	KUNIT_EXPECT_LT(test, out.g, 0x0100);

	/* V 0 */
	KUNIT_EXPECT_LT(test, out.b, 0x0100);

	/* == red 255 - bt709 enc == */
	out.a = 0xffff;
	out.r = 0xffff;
	out.g = 0x0;
	out.b = 0x0;

	apply_3x4_matrix(&out, &test_matrix_3x4_bt709_enc);

	/* Y 54 */
	KUNIT_EXPECT_GT(test, out.r, 0x3500);
	KUNIT_EXPECT_LT(test, out.r, 0x3700);

	/* U 0 */
	KUNIT_EXPECT_LT(test, out.g, 0x0100);

	/* V 157 */
	KUNIT_EXPECT_GT(test, out.b, 0x9C00);
	KUNIT_EXPECT_LT(test, out.b, 0x9E00);

	/* == green 255 - bt709 enc == */
	out.a = 0xffff;
	out.r = 0x0;
	out.g = 0xffff;
	out.b = 0x0;

	apply_3x4_matrix(&out, &test_matrix_3x4_bt709_enc);

	/* Y 182 */
	KUNIT_EXPECT_GT(test, out.r, 0xB500);
	KUNIT_EXPECT_LT(test, out.r, 0xB780); /* laxed by half*/

	/* U 0 */
	KUNIT_EXPECT_LT(test, out.g, 0x0100);

	/* V 0 */
	KUNIT_EXPECT_LT(test, out.b, 0x0100);

	/* == blue 255 - bt709 enc == */
	out.a = 0xffff;
	out.r = 0x0;
	out.g = 0x0;
	out.b = 0xffff;

	apply_3x4_matrix(&out, &test_matrix_3x4_bt709_enc);

	/* Y 18 */
	KUNIT_EXPECT_GT(test, out.r, 0x1100);
	KUNIT_EXPECT_LT(test, out.r, 0x1300);

	/* U 111 */
	KUNIT_EXPECT_GT(test, out.g, 0x6E00);
	KUNIT_EXPECT_LT(test, out.g, 0x7000);

	/* V 0 */
	KUNIT_EXPECT_LT(test, out.b, 0x0100);

	/* == red 140 - bt709 enc == */
	out.a = 0xffff;
	out.r = 0x8c8c;
	out.g = 0x0;
	out.b = 0x0;

	apply_3x4_matrix(&out, &test_matrix_3x4_bt709_enc);

	/* Y 30 */
	KUNIT_EXPECT_GT(test, out.r, 0x1D00);
	KUNIT_EXPECT_LT(test, out.r, 0x1F00);

	/* U 0 */
	KUNIT_EXPECT_LT(test, out.g, 0x100);

	/* V 87 */
	KUNIT_EXPECT_GT(test, out.b, 0x5600);
	KUNIT_EXPECT_LT(test, out.b, 0x5800);

	/* == green 140 - bt709 enc == */
	out.a = 0xffff;
	out.r = 0x0;
	out.g = 0x8c8c;
	out.b = 0x0;

	apply_3x4_matrix(&out, &test_matrix_3x4_bt709_enc);

	/* Y 30 */
	KUNIT_EXPECT_GT(test, out.r, 0x6400);
	KUNIT_EXPECT_LT(test, out.r, 0x6600);

	/* U 0 */
	KUNIT_EXPECT_LT(test, out.g, 0x100);

	/* V 0 */
	KUNIT_EXPECT_LT(test, out.b, 0x100);

	/* == blue 140 - bt709 enc == */
	out.a = 0xffff;
	out.r = 0x0;
	out.g = 0x0;
	out.b = 0x8c8c;

	apply_3x4_matrix(&out, &test_matrix_3x4_bt709_enc);

	/* Y 30 */
	KUNIT_EXPECT_GT(test, out.r, 0x900);
	KUNIT_EXPECT_LT(test, out.r, 0xB00);

	/* U 61 */
	KUNIT_EXPECT_GT(test, out.g, 0x3C00);
	KUNIT_EXPECT_LT(test, out.g, 0x3E00);

	/* V 0 */
	KUNIT_EXPECT_LT(test, out.b, 0x100);
}

static struct kunit_case vkms_color_test_cases[] = {
	KUNIT_CASE(vkms_color_test_get_lut_index),
	KUNIT_CASE(vkms_color_test_lerp),
	KUNIT_CASE(vkms_color_test_linear),
	KUNIT_CASE(vkms_color_srgb_inv_srgb),
	KUNIT_CASE(vkms_color_ctm_3x4_50_desat),
	KUNIT_CASE(vkms_color_ctm_3x4_bt709),
	{}
};

static struct kunit_suite vkms_color_test_suite = {
	.name = "vkms-color",
	.test_cases = vkms_color_test_cases,
};

kunit_test_suite(vkms_color_test_suite);

MODULE_DESCRIPTION("Kunit test for VKMS LUT handling");
MODULE_LICENSE("GPL");
