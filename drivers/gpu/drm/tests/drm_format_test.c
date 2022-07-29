// SPDX-License-Identifier: GPL-2.0
/*
 * Test cases for the drm_format functions
 *
 * Copyright (c) 2022 Maíra Canal <mairacanal@riseup.net>
 */

#include <kunit/test.h>

#include <drm/drm_fourcc.h>

static void igt_check_drm_format_block_width(struct kunit *test)
{
	const struct drm_format_info *info = NULL;

	/* Test invalid arguments */
	KUNIT_EXPECT_FALSE(test, drm_format_info_block_width(info, 0));
	KUNIT_EXPECT_FALSE(test, drm_format_info_block_width(info, -1));
	KUNIT_EXPECT_FALSE(test, drm_format_info_block_width(info, 1));

	/* Test 1 plane format */
	info = drm_format_info(DRM_FORMAT_XRGB4444);
	KUNIT_EXPECT_TRUE(test, info);
	KUNIT_EXPECT_TRUE(test, drm_format_info_block_width(info, 0));
	KUNIT_EXPECT_FALSE(test, drm_format_info_block_width(info, 1));
	KUNIT_EXPECT_FALSE(test, drm_format_info_block_width(info, -1));

	/* Test 2 planes format */
	info = drm_format_info(DRM_FORMAT_NV12);
	KUNIT_EXPECT_TRUE(test, info);
	KUNIT_EXPECT_TRUE(test, drm_format_info_block_width(info, 0));
	KUNIT_EXPECT_TRUE(test, drm_format_info_block_width(info, 1));
	KUNIT_EXPECT_FALSE(test, drm_format_info_block_width(info, 2));
	KUNIT_EXPECT_FALSE(test, drm_format_info_block_width(info, -1));

	/* Test 3 planes format */
	info = drm_format_info(DRM_FORMAT_YUV422);
	KUNIT_EXPECT_TRUE(test, info);
	KUNIT_EXPECT_TRUE(test, drm_format_info_block_width(info, 0));
	KUNIT_EXPECT_TRUE(test, drm_format_info_block_width(info, 1));
	KUNIT_EXPECT_TRUE(test, drm_format_info_block_width(info, 2));
	KUNIT_EXPECT_FALSE(test, drm_format_info_block_width(info, 3));
	KUNIT_EXPECT_FALSE(test, drm_format_info_block_width(info, -1));

	/* Test a tiled format */
	info = drm_format_info(DRM_FORMAT_X0L0);
	KUNIT_EXPECT_TRUE(test, info);
	KUNIT_EXPECT_EQ(test, drm_format_info_block_width(info, 0), 2);
	KUNIT_EXPECT_FALSE(test, drm_format_info_block_width(info, 1));
	KUNIT_EXPECT_FALSE(test, drm_format_info_block_width(info, -1));
}

static void igt_check_drm_format_block_height(struct kunit *test)
{
	const struct drm_format_info *info = NULL;

	/* Test invalid arguments */
	KUNIT_EXPECT_FALSE(test, drm_format_info_block_height(info, 0));
	KUNIT_EXPECT_FALSE(test, drm_format_info_block_height(info, -1));
	KUNIT_EXPECT_FALSE(test, drm_format_info_block_height(info, 1));

	/* Test 1 plane format */
	info = drm_format_info(DRM_FORMAT_XRGB4444);
	KUNIT_EXPECT_TRUE(test, info);
	KUNIT_EXPECT_TRUE(test, drm_format_info_block_height(info, 0));
	KUNIT_EXPECT_FALSE(test, drm_format_info_block_height(info, -1));
	KUNIT_EXPECT_FALSE(test, drm_format_info_block_height(info, 1));

	/* Test 2 planes format */
	info = drm_format_info(DRM_FORMAT_NV12);
	KUNIT_EXPECT_TRUE(test, info);
	KUNIT_EXPECT_TRUE(test, drm_format_info_block_height(info, 0));
	KUNIT_EXPECT_TRUE(test, drm_format_info_block_height(info, 1));
	KUNIT_EXPECT_FALSE(test, drm_format_info_block_height(info, 2));
	KUNIT_EXPECT_FALSE(test, drm_format_info_block_height(info, -1));

	/* Test 3 planes format */
	info = drm_format_info(DRM_FORMAT_YUV422);
	KUNIT_EXPECT_TRUE(test, info);
	KUNIT_EXPECT_TRUE(test, drm_format_info_block_height(info, 0));
	KUNIT_EXPECT_TRUE(test, drm_format_info_block_height(info, 1));
	KUNIT_EXPECT_TRUE(test, drm_format_info_block_height(info, 2));
	KUNIT_EXPECT_FALSE(test, drm_format_info_block_height(info, 3));
	KUNIT_EXPECT_FALSE(test, drm_format_info_block_height(info, -1));

	/* Test a tiled format */
	info = drm_format_info(DRM_FORMAT_X0L0);
	KUNIT_EXPECT_TRUE(test, info);
	KUNIT_EXPECT_EQ(test, drm_format_info_block_height(info, 0), 2);
	KUNIT_EXPECT_FALSE(test, drm_format_info_block_height(info, 1));
	KUNIT_EXPECT_FALSE(test, drm_format_info_block_height(info, -1));
}

static void igt_check_drm_format_min_pitch_for_single_plane(struct kunit *test)
{
	const struct drm_format_info *info = NULL;

	/* Test invalid arguments */
	KUNIT_EXPECT_FALSE(test, drm_format_info_min_pitch(info, 0, 0));
	KUNIT_EXPECT_FALSE(test, drm_format_info_min_pitch(info, -1, 0));
	KUNIT_EXPECT_FALSE(test, drm_format_info_min_pitch(info, 1, 0));

	/* Test 1 plane 8 bits per pixel format */
	info = drm_format_info(DRM_FORMAT_RGB332);
	KUNIT_EXPECT_TRUE(test, info);
	KUNIT_EXPECT_FALSE(test, drm_format_info_min_pitch(info, 0, 0));
	KUNIT_EXPECT_FALSE(test, drm_format_info_min_pitch(info, -1, 0));
	KUNIT_EXPECT_FALSE(test, drm_format_info_min_pitch(info, 1, 0));

	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 1), 1);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 2), 2);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 640), 640);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 1024), 1024);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 1920), 1920);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 4096), 4096);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 671), 671);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, UINT_MAX),
			(uint64_t)UINT_MAX);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, (UINT_MAX - 1)),
			(uint64_t)(UINT_MAX - 1));

	/* Test 1 plane 16 bits per pixel format */
	info = drm_format_info(DRM_FORMAT_XRGB4444);
	KUNIT_EXPECT_TRUE(test, info);
	KUNIT_EXPECT_FALSE(test, drm_format_info_min_pitch(info, 0, 0));
	KUNIT_EXPECT_FALSE(test, drm_format_info_min_pitch(info, -1, 0));
	KUNIT_EXPECT_FALSE(test, drm_format_info_min_pitch(info, 1, 0));

	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 1), 2);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 2), 4);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 640), 1280);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 1024), 2048);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 1920), 3840);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 4096), 8192);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 671), 1342);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, UINT_MAX),
			(uint64_t)UINT_MAX * 2);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, (UINT_MAX - 1)),
			(uint64_t)(UINT_MAX - 1) * 2);

	/* Test 1 plane 24 bits per pixel format */
	info = drm_format_info(DRM_FORMAT_RGB888);
	KUNIT_EXPECT_TRUE(test, info);
	KUNIT_EXPECT_FALSE(test, drm_format_info_min_pitch(info, 0, 0));
	KUNIT_EXPECT_FALSE(test, drm_format_info_min_pitch(info, -1, 0));
	KUNIT_EXPECT_FALSE(test, drm_format_info_min_pitch(info, 1, 0));

	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 1), 3);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 2), 6);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 640), 1920);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 1024), 3072);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 1920), 5760);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 4096), 12288);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 671), 2013);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, UINT_MAX),
			(uint64_t)UINT_MAX * 3);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, (UINT_MAX - 1)),
			(uint64_t)(UINT_MAX - 1) * 3);

	/* Test 1 plane 32 bits per pixel format */
	info = drm_format_info(DRM_FORMAT_ABGR8888);
	KUNIT_EXPECT_TRUE(test, info);
	KUNIT_EXPECT_FALSE(test, drm_format_info_min_pitch(info, 0, 0));
	KUNIT_EXPECT_FALSE(test, drm_format_info_min_pitch(info, -1, 0));
	KUNIT_EXPECT_FALSE(test, drm_format_info_min_pitch(info, 1, 0));

	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 1), 4);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 2), 8);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 640), 2560);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 1024), 4096);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 1920), 7680);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 4096), 16384);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 671), 2684);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, UINT_MAX),
			(uint64_t)UINT_MAX * 4);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, (UINT_MAX - 1)),
			(uint64_t)(UINT_MAX - 1) * 4);
}

static void igt_check_drm_format_min_pitch_for_multi_planar(struct kunit *test)
{
	const struct drm_format_info *info = NULL;

	/* Test 2 planes format */
	info = drm_format_info(DRM_FORMAT_NV12);
	KUNIT_EXPECT_TRUE(test, info);
	KUNIT_EXPECT_FALSE(test, drm_format_info_min_pitch(info, 0, 0));
	KUNIT_EXPECT_FALSE(test, drm_format_info_min_pitch(info, 1, 0));
	KUNIT_EXPECT_FALSE(test, drm_format_info_min_pitch(info, -1, 0));
	KUNIT_EXPECT_FALSE(test, drm_format_info_min_pitch(info, 2, 0));

	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 1), 1);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 1, 1), 2);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 2), 2);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 1, 1), 2);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 640), 640);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 1, 320), 640);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 1024), 1024);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 1, 512), 1024);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 1920), 1920);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 1, 960), 1920);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 4096), 4096);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 1, 2048), 4096);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 671), 671);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 1, 336), 672);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, UINT_MAX),
			(uint64_t)UINT_MAX);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 1, UINT_MAX / 2 + 1),
			(uint64_t)UINT_MAX + 1);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, (UINT_MAX - 1)),
			(uint64_t)(UINT_MAX - 1));
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 1, (UINT_MAX - 1) /  2),
			(uint64_t)(UINT_MAX - 1));

	/* Test 3 planes 8 bits per pixel format */
	info = drm_format_info(DRM_FORMAT_YUV422);
	KUNIT_EXPECT_TRUE(test, info);
	KUNIT_EXPECT_FALSE(test, drm_format_info_min_pitch(info, 0, 0));
	KUNIT_EXPECT_FALSE(test, drm_format_info_min_pitch(info, 1, 0));
	KUNIT_EXPECT_FALSE(test, drm_format_info_min_pitch(info, 2, 0));
	KUNIT_EXPECT_FALSE(test, drm_format_info_min_pitch(info, -1, 0));
	KUNIT_EXPECT_FALSE(test, drm_format_info_min_pitch(info, 3, 0));

	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 1), 1);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 1, 1), 1);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 2, 1), 1);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 2), 2);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 1, 2), 2);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 2, 2), 2);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 640), 640);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 1, 320), 320);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 2, 320), 320);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 1024), 1024);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 1, 512), 512);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 2, 512), 512);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 1920), 1920);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 1, 960), 960);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 2, 960), 960);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 4096), 4096);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 1, 2048), 2048);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 2, 2048), 2048);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 671), 671);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 1, 336), 336);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 2, 336), 336);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, UINT_MAX),
			(uint64_t)UINT_MAX);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 1, UINT_MAX / 2 + 1),
			(uint64_t)UINT_MAX / 2 + 1);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 2, UINT_MAX / 2 + 1),
			(uint64_t)UINT_MAX / 2 + 1);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, (UINT_MAX - 1) / 2),
			(uint64_t)(UINT_MAX - 1) / 2);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 1, (UINT_MAX - 1) / 2),
			(uint64_t)(UINT_MAX - 1) / 2);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 2, (UINT_MAX - 1) / 2),
			(uint64_t)(UINT_MAX - 1) / 2);
}

static void igt_check_drm_format_min_pitch_for_tiled_format(struct kunit *test)
{
	const struct drm_format_info *info = NULL;

	/* Test tiled format */
	info = drm_format_info(DRM_FORMAT_X0L2);
	KUNIT_EXPECT_TRUE(test, info);
	KUNIT_EXPECT_FALSE(test, drm_format_info_min_pitch(info, 0, 0));
	KUNIT_EXPECT_FALSE(test, drm_format_info_min_pitch(info, -1, 0));
	KUNIT_EXPECT_FALSE(test, drm_format_info_min_pitch(info, 1, 0));

	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 1), 2);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 2), 4);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 640), 1280);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 1024), 2048);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 1920), 3840);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 4096), 8192);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, 671), 1342);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, UINT_MAX),
			(uint64_t)UINT_MAX * 2);
	KUNIT_EXPECT_EQ(test, drm_format_info_min_pitch(info, 0, UINT_MAX - 1),
			(uint64_t)(UINT_MAX - 1) * 2);
}

static struct kunit_case drm_format_tests[] = {
	KUNIT_CASE(igt_check_drm_format_block_width),
	KUNIT_CASE(igt_check_drm_format_block_height),
	KUNIT_CASE(igt_check_drm_format_min_pitch_for_single_plane),
	KUNIT_CASE(igt_check_drm_format_min_pitch_for_multi_planar),
	KUNIT_CASE(igt_check_drm_format_min_pitch_for_tiled_format),
	{ }
};

static struct kunit_suite drm_format_test_suite = {
	.name = "drm_format",
	.test_cases = drm_format_tests,
};

kunit_test_suite(drm_format_test_suite);

MODULE_LICENSE("GPL");
