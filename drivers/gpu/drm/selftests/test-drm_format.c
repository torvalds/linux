// SPDX-License-Identifier: GPL-2.0
/*
 * Test cases for the drm_format functions
 */

#define pr_fmt(fmt) "drm_format: " fmt

#include <linux/errno.h>
#include <linux/kernel.h>

#include <drm/drm_fourcc.h>

#include "test-drm_modeset_common.h"

int igt_check_drm_format_block_width(void *ignored)
{
	const struct drm_format_info *info = NULL;

	/* Test invalid arguments */
	FAIL_ON(drm_format_info_block_width(info, 0) != 0);
	FAIL_ON(drm_format_info_block_width(info, -1) != 0);
	FAIL_ON(drm_format_info_block_width(info, 1) != 0);

	/* Test 1 plane format */
	info = drm_format_info(DRM_FORMAT_XRGB4444);
	FAIL_ON(!info);
	FAIL_ON(drm_format_info_block_width(info, 0) != 1);
	FAIL_ON(drm_format_info_block_width(info, 1) != 0);
	FAIL_ON(drm_format_info_block_width(info, -1) != 0);

	/* Test 2 planes format */
	info = drm_format_info(DRM_FORMAT_NV12);
	FAIL_ON(!info);
	FAIL_ON(drm_format_info_block_width(info, 0) != 1);
	FAIL_ON(drm_format_info_block_width(info, 1) != 1);
	FAIL_ON(drm_format_info_block_width(info, 2) != 0);
	FAIL_ON(drm_format_info_block_width(info, -1) != 0);

	/* Test 3 planes format */
	info = drm_format_info(DRM_FORMAT_YUV422);
	FAIL_ON(!info);
	FAIL_ON(drm_format_info_block_width(info, 0) != 1);
	FAIL_ON(drm_format_info_block_width(info, 1) != 1);
	FAIL_ON(drm_format_info_block_width(info, 2) != 1);
	FAIL_ON(drm_format_info_block_width(info, 3) != 0);
	FAIL_ON(drm_format_info_block_width(info, -1) != 0);

	/* Test a tiled format */
	info = drm_format_info(DRM_FORMAT_X0L0);
	FAIL_ON(!info);
	FAIL_ON(drm_format_info_block_width(info, 0) != 2);
	FAIL_ON(drm_format_info_block_width(info, 1) != 0);
	FAIL_ON(drm_format_info_block_width(info, -1) != 0);

	return 0;
}

int igt_check_drm_format_block_height(void *ignored)
{
	const struct drm_format_info *info = NULL;

	/* Test invalid arguments */
	FAIL_ON(drm_format_info_block_height(info, 0) != 0);
	FAIL_ON(drm_format_info_block_height(info, -1) != 0);
	FAIL_ON(drm_format_info_block_height(info, 1) != 0);

	/* Test 1 plane format */
	info = drm_format_info(DRM_FORMAT_XRGB4444);
	FAIL_ON(!info);
	FAIL_ON(drm_format_info_block_height(info, 0) != 1);
	FAIL_ON(drm_format_info_block_height(info, 1) != 0);
	FAIL_ON(drm_format_info_block_height(info, -1) != 0);

	/* Test 2 planes format */
	info = drm_format_info(DRM_FORMAT_NV12);
	FAIL_ON(!info);
	FAIL_ON(drm_format_info_block_height(info, 0) != 1);
	FAIL_ON(drm_format_info_block_height(info, 1) != 1);
	FAIL_ON(drm_format_info_block_height(info, 2) != 0);
	FAIL_ON(drm_format_info_block_height(info, -1) != 0);

	/* Test 3 planes format */
	info = drm_format_info(DRM_FORMAT_YUV422);
	FAIL_ON(!info);
	FAIL_ON(drm_format_info_block_height(info, 0) != 1);
	FAIL_ON(drm_format_info_block_height(info, 1) != 1);
	FAIL_ON(drm_format_info_block_height(info, 2) != 1);
	FAIL_ON(drm_format_info_block_height(info, 3) != 0);
	FAIL_ON(drm_format_info_block_height(info, -1) != 0);

	/* Test a tiled format */
	info = drm_format_info(DRM_FORMAT_X0L0);
	FAIL_ON(!info);
	FAIL_ON(drm_format_info_block_height(info, 0) != 2);
	FAIL_ON(drm_format_info_block_height(info, 1) != 0);
	FAIL_ON(drm_format_info_block_height(info, -1) != 0);

	return 0;
}

int igt_check_drm_format_min_pitch(void *ignored)
{
	const struct drm_format_info *info = NULL;

	/* Test invalid arguments */
	FAIL_ON(drm_format_info_min_pitch(info, 0, 0) != 0);
	FAIL_ON(drm_format_info_min_pitch(info, -1, 0) != 0);
	FAIL_ON(drm_format_info_min_pitch(info, 1, 0) != 0);

	/* Test 1 plane 8 bits per pixel format */
	info = drm_format_info(DRM_FORMAT_RGB332);
	FAIL_ON(!info);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 0) != 0);
	FAIL_ON(drm_format_info_min_pitch(info, -1, 0) != 0);
	FAIL_ON(drm_format_info_min_pitch(info, 1, 0) != 0);

	FAIL_ON(drm_format_info_min_pitch(info, 0, 1) != 1);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 2) != 2);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 640) != 640);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 1024) != 1024);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 1920) != 1920);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 4096) != 4096);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 671) != 671);
	FAIL_ON(drm_format_info_min_pitch(info, 0, UINT_MAX) !=
			(uint64_t)UINT_MAX);
	FAIL_ON(drm_format_info_min_pitch(info, 0, (UINT_MAX - 1)) !=
			(uint64_t)(UINT_MAX - 1));

	/* Test 1 plane 16 bits per pixel format */
	info = drm_format_info(DRM_FORMAT_XRGB4444);
	FAIL_ON(!info);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 0) != 0);
	FAIL_ON(drm_format_info_min_pitch(info, -1, 0) != 0);
	FAIL_ON(drm_format_info_min_pitch(info, 1, 0) != 0);

	FAIL_ON(drm_format_info_min_pitch(info, 0, 1) != 2);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 2) != 4);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 640) != 1280);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 1024) != 2048);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 1920) != 3840);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 4096) != 8192);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 671) != 1342);
	FAIL_ON(drm_format_info_min_pitch(info, 0, UINT_MAX) !=
			(uint64_t)UINT_MAX * 2);
	FAIL_ON(drm_format_info_min_pitch(info, 0, (UINT_MAX - 1)) !=
			(uint64_t)(UINT_MAX - 1) * 2);

	/* Test 1 plane 24 bits per pixel format */
	info = drm_format_info(DRM_FORMAT_RGB888);
	FAIL_ON(!info);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 0) != 0);
	FAIL_ON(drm_format_info_min_pitch(info, -1, 0) != 0);
	FAIL_ON(drm_format_info_min_pitch(info, 1, 0) != 0);

	FAIL_ON(drm_format_info_min_pitch(info, 0, 1) != 3);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 2) != 6);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 640) != 1920);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 1024) != 3072);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 1920) != 5760);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 4096) != 12288);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 671) != 2013);
	FAIL_ON(drm_format_info_min_pitch(info, 0, UINT_MAX) !=
			(uint64_t)UINT_MAX * 3);
	FAIL_ON(drm_format_info_min_pitch(info, 0, UINT_MAX - 1) !=
			(uint64_t)(UINT_MAX - 1) * 3);

	/* Test 1 plane 32 bits per pixel format */
	info = drm_format_info(DRM_FORMAT_ABGR8888);
	FAIL_ON(!info);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 0) != 0);
	FAIL_ON(drm_format_info_min_pitch(info, -1, 0) != 0);
	FAIL_ON(drm_format_info_min_pitch(info, 1, 0) != 0);

	FAIL_ON(drm_format_info_min_pitch(info, 0, 1) != 4);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 2) != 8);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 640) != 2560);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 1024) != 4096);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 1920) != 7680);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 4096) != 16384);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 671) != 2684);
	FAIL_ON(drm_format_info_min_pitch(info, 0, UINT_MAX) !=
			(uint64_t)UINT_MAX * 4);
	FAIL_ON(drm_format_info_min_pitch(info, 0, UINT_MAX - 1) !=
			(uint64_t)(UINT_MAX - 1) * 4);

	/* Test 2 planes format */
	info = drm_format_info(DRM_FORMAT_NV12);
	FAIL_ON(!info);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 0) != 0);
	FAIL_ON(drm_format_info_min_pitch(info, 1, 0) != 0);
	FAIL_ON(drm_format_info_min_pitch(info, -1, 0) != 0);
	FAIL_ON(drm_format_info_min_pitch(info, 2, 0) != 0);

	FAIL_ON(drm_format_info_min_pitch(info, 0, 1) != 1);
	FAIL_ON(drm_format_info_min_pitch(info, 1, 1) != 2);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 2) != 2);
	FAIL_ON(drm_format_info_min_pitch(info, 1, 1) != 2);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 640) != 640);
	FAIL_ON(drm_format_info_min_pitch(info, 1, 320) != 640);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 1024) != 1024);
	FAIL_ON(drm_format_info_min_pitch(info, 1, 512) != 1024);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 1920) != 1920);
	FAIL_ON(drm_format_info_min_pitch(info, 1, 960) != 1920);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 4096) != 4096);
	FAIL_ON(drm_format_info_min_pitch(info, 1, 2048) != 4096);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 671) != 671);
	FAIL_ON(drm_format_info_min_pitch(info, 1, 336) != 672);
	FAIL_ON(drm_format_info_min_pitch(info, 0, UINT_MAX) !=
			(uint64_t)UINT_MAX);
	FAIL_ON(drm_format_info_min_pitch(info, 1, UINT_MAX / 2 + 1) !=
			(uint64_t)UINT_MAX + 1);
	FAIL_ON(drm_format_info_min_pitch(info, 0, (UINT_MAX - 1)) !=
			(uint64_t)(UINT_MAX - 1));
	FAIL_ON(drm_format_info_min_pitch(info, 1, (UINT_MAX - 1) /  2) !=
			(uint64_t)(UINT_MAX - 1));

	/* Test 3 planes 8 bits per pixel format */
	info = drm_format_info(DRM_FORMAT_YUV422);
	FAIL_ON(!info);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 0) != 0);
	FAIL_ON(drm_format_info_min_pitch(info, 1, 0) != 0);
	FAIL_ON(drm_format_info_min_pitch(info, 2, 0) != 0);
	FAIL_ON(drm_format_info_min_pitch(info, -1, 0) != 0);
	FAIL_ON(drm_format_info_min_pitch(info, 3, 0) != 0);

	FAIL_ON(drm_format_info_min_pitch(info, 0, 1) != 1);
	FAIL_ON(drm_format_info_min_pitch(info, 1, 1) != 1);
	FAIL_ON(drm_format_info_min_pitch(info, 2, 1) != 1);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 2) != 2);
	FAIL_ON(drm_format_info_min_pitch(info, 1, 2) != 2);
	FAIL_ON(drm_format_info_min_pitch(info, 2, 2) != 2);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 640) != 640);
	FAIL_ON(drm_format_info_min_pitch(info, 1, 320) != 320);
	FAIL_ON(drm_format_info_min_pitch(info, 2, 320) != 320);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 1024) != 1024);
	FAIL_ON(drm_format_info_min_pitch(info, 1, 512) != 512);
	FAIL_ON(drm_format_info_min_pitch(info, 2, 512) != 512);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 1920) != 1920);
	FAIL_ON(drm_format_info_min_pitch(info, 1, 960) != 960);
	FAIL_ON(drm_format_info_min_pitch(info, 2, 960) != 960);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 4096) != 4096);
	FAIL_ON(drm_format_info_min_pitch(info, 1, 2048) != 2048);
	FAIL_ON(drm_format_info_min_pitch(info, 2, 2048) != 2048);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 671) != 671);
	FAIL_ON(drm_format_info_min_pitch(info, 1, 336) != 336);
	FAIL_ON(drm_format_info_min_pitch(info, 2, 336) != 336);
	FAIL_ON(drm_format_info_min_pitch(info, 0, UINT_MAX) !=
			(uint64_t)UINT_MAX);
	FAIL_ON(drm_format_info_min_pitch(info, 1, UINT_MAX / 2 + 1) !=
			(uint64_t)UINT_MAX / 2 + 1);
	FAIL_ON(drm_format_info_min_pitch(info, 2, UINT_MAX / 2 + 1) !=
			(uint64_t)UINT_MAX / 2 + 1);
	FAIL_ON(drm_format_info_min_pitch(info, 0, (UINT_MAX - 1) / 2) !=
			(uint64_t)(UINT_MAX - 1) / 2);
	FAIL_ON(drm_format_info_min_pitch(info, 1, (UINT_MAX - 1) / 2) !=
			(uint64_t)(UINT_MAX - 1) / 2);
	FAIL_ON(drm_format_info_min_pitch(info, 2, (UINT_MAX - 1) / 2) !=
			(uint64_t)(UINT_MAX - 1) / 2);

	/* Test tiled format */
	info = drm_format_info(DRM_FORMAT_X0L2);
	FAIL_ON(!info);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 0) != 0);
	FAIL_ON(drm_format_info_min_pitch(info, -1, 0) != 0);
	FAIL_ON(drm_format_info_min_pitch(info, 1, 0) != 0);

	FAIL_ON(drm_format_info_min_pitch(info, 0, 1) != 2);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 2) != 4);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 640) != 1280);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 1024) != 2048);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 1920) != 3840);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 4096) != 8192);
	FAIL_ON(drm_format_info_min_pitch(info, 0, 671) != 1342);
	FAIL_ON(drm_format_info_min_pitch(info, 0, UINT_MAX) !=
			(uint64_t)UINT_MAX * 2);
	FAIL_ON(drm_format_info_min_pitch(info, 0, UINT_MAX - 1) !=
			(uint64_t)(UINT_MAX - 1) * 2);

	return 0;
}
