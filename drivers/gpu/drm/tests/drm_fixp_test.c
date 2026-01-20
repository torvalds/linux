// SPDX-License-Identifier: MIT
/*
 * Copyright 2022 Advanced Micro Devices, Inc.
 */

#include <kunit/test.h>
#include <drm/drm_fixed.h>

static void drm_test_sm2fixp(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, 0x7fffffffffffffffll, ((1ull << 63) - 1));

	/* 1 */
	KUNIT_EXPECT_EQ(test, drm_int2fixp(1), drm_sm2fixp(1ull << DRM_FIXED_POINT));

	/* -1 */
	KUNIT_EXPECT_EQ(test, drm_int2fixp(-1),
			drm_sm2fixp((1ull << 63) | (1ull << DRM_FIXED_POINT)));

	/* 0.5 */
	KUNIT_EXPECT_EQ(test, drm_fixp_from_fraction(1, 2),
			drm_sm2fixp(1ull << (DRM_FIXED_POINT - 1)));

	/* -0.5 */
	KUNIT_EXPECT_EQ(test, drm_fixp_from_fraction(-1, 2),
			drm_sm2fixp((1ull << 63) | (1ull << (DRM_FIXED_POINT - 1))));
}

static void drm_test_int2fixp(struct kunit *test)
{
	/* 1 */
	KUNIT_EXPECT_EQ(test, 1ll << 32, drm_int2fixp(1));

	/* -1 */
	KUNIT_EXPECT_EQ(test, -(1ll << 32), drm_int2fixp(-1));

	/* 1 + (-1) = 0 */
	KUNIT_EXPECT_EQ(test, 0, drm_int2fixp(1) + drm_int2fixp(-1));

	/* 1 / 2 */
	KUNIT_EXPECT_EQ(test, 1ll << 31, drm_fixp_from_fraction(1, 2));

	/* -0.5 */
	KUNIT_EXPECT_EQ(test, -(1ll << 31), drm_fixp_from_fraction(-1, 2));

	/* (1 / 2) + (-1) = 0.5 */
	KUNIT_EXPECT_EQ(test, 1ll << 31, drm_fixp_from_fraction(-1, 2) + drm_int2fixp(1));

	/* (1 / 2) - 1) = 0.5 */
	KUNIT_EXPECT_EQ(test, -(1ll << 31), drm_fixp_from_fraction(1, 2) + drm_int2fixp(-1));

	/* (1 / 2) - 1) = 0.5 */
	KUNIT_EXPECT_EQ(test, -(1ll << 31), drm_fixp_from_fraction(1, 2) - drm_int2fixp(1));
}

static struct kunit_case drm_fixp_tests[] = {
	KUNIT_CASE(drm_test_int2fixp),
	KUNIT_CASE(drm_test_sm2fixp),
	{ }
};

static struct kunit_suite drm_fixp_test_suite = {
	.name = "drm_fixp",
	.test_cases = drm_fixp_tests,
};

kunit_test_suite(drm_fixp_test_suite);

MODULE_AUTHOR("AMD");
MODULE_LICENSE("Dual MIT/GPL");
MODULE_DESCRIPTION("Unit tests for drm_fixed.h");
