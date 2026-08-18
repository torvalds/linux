// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * KUnit tests for amdgpu_dm_crc.c
 *
 * Copyright 2026 Advanced Micro Devices, Inc.
 */

#include <kunit/test.h>

#include "amdgpu_dm_crc.h"

static void dm_test_parse_crc_source_none(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, AMDGPU_DM_PIPE_CRC_SOURCE_NONE, dm_parse_crc_source("none"));
	KUNIT_EXPECT_EQ(test, AMDGPU_DM_PIPE_CRC_SOURCE_NONE, dm_parse_crc_source(NULL));
}

static void dm_test_parse_crc_source_crtc(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, AMDGPU_DM_PIPE_CRC_SOURCE_CRTC, dm_parse_crc_source("crtc"));
	KUNIT_EXPECT_EQ(test, AMDGPU_DM_PIPE_CRC_SOURCE_CRTC, dm_parse_crc_source("auto"));
}

static void dm_test_parse_crc_source_dprx(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, AMDGPU_DM_PIPE_CRC_SOURCE_DPRX, dm_parse_crc_source("dprx"));
}

static void dm_test_parse_crc_source_crtc_dither(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, AMDGPU_DM_PIPE_CRC_SOURCE_CRTC_DITHER,
			dm_parse_crc_source("crtc dither"));
}

static void dm_test_parse_crc_source_dprx_dither(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, AMDGPU_DM_PIPE_CRC_SOURCE_DPRX_DITHER,
			dm_parse_crc_source("dprx dither"));
}

static void dm_test_parse_crc_source_invalid(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, AMDGPU_DM_PIPE_CRC_SOURCE_INVALID,
			dm_parse_crc_source("invalid"));
	KUNIT_EXPECT_EQ(test, AMDGPU_DM_PIPE_CRC_SOURCE_INVALID,
			dm_parse_crc_source("unknown"));
	KUNIT_EXPECT_EQ(test, AMDGPU_DM_PIPE_CRC_SOURCE_INVALID,
			dm_parse_crc_source(""));
}

static void dm_test_is_crc_source_crtc(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test, dm_is_crc_source_crtc(AMDGPU_DM_PIPE_CRC_SOURCE_CRTC));
	KUNIT_EXPECT_TRUE(test, dm_is_crc_source_crtc(AMDGPU_DM_PIPE_CRC_SOURCE_CRTC_DITHER));

	KUNIT_EXPECT_FALSE(test, dm_is_crc_source_crtc(AMDGPU_DM_PIPE_CRC_SOURCE_NONE));
	KUNIT_EXPECT_FALSE(test, dm_is_crc_source_crtc(AMDGPU_DM_PIPE_CRC_SOURCE_DPRX));
	KUNIT_EXPECT_FALSE(test, dm_is_crc_source_crtc(AMDGPU_DM_PIPE_CRC_SOURCE_DPRX_DITHER));
	KUNIT_EXPECT_FALSE(test, dm_is_crc_source_crtc(AMDGPU_DM_PIPE_CRC_SOURCE_INVALID));
}

static void dm_test_is_crc_source_dprx(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test, dm_is_crc_source_dprx(AMDGPU_DM_PIPE_CRC_SOURCE_DPRX));
	KUNIT_EXPECT_TRUE(test, dm_is_crc_source_dprx(AMDGPU_DM_PIPE_CRC_SOURCE_DPRX_DITHER));

	KUNIT_EXPECT_FALSE(test, dm_is_crc_source_dprx(AMDGPU_DM_PIPE_CRC_SOURCE_NONE));
	KUNIT_EXPECT_FALSE(test, dm_is_crc_source_dprx(AMDGPU_DM_PIPE_CRC_SOURCE_CRTC));
	KUNIT_EXPECT_FALSE(test, dm_is_crc_source_dprx(AMDGPU_DM_PIPE_CRC_SOURCE_CRTC_DITHER));
	KUNIT_EXPECT_FALSE(test, dm_is_crc_source_dprx(AMDGPU_DM_PIPE_CRC_SOURCE_INVALID));
}

static void dm_test_need_crc_dither(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test, dm_need_crc_dither(AMDGPU_DM_PIPE_CRC_SOURCE_NONE));
	KUNIT_EXPECT_TRUE(test, dm_need_crc_dither(AMDGPU_DM_PIPE_CRC_SOURCE_CRTC_DITHER));
	KUNIT_EXPECT_TRUE(test, dm_need_crc_dither(AMDGPU_DM_PIPE_CRC_SOURCE_DPRX_DITHER));

	KUNIT_EXPECT_FALSE(test, dm_need_crc_dither(AMDGPU_DM_PIPE_CRC_SOURCE_CRTC));
	KUNIT_EXPECT_FALSE(test, dm_need_crc_dither(AMDGPU_DM_PIPE_CRC_SOURCE_DPRX));
	KUNIT_EXPECT_FALSE(test, dm_need_crc_dither(AMDGPU_DM_PIPE_CRC_SOURCE_INVALID));
}

static void dm_test_is_valid_crc_source(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test, amdgpu_dm_is_valid_crc_source(AMDGPU_DM_PIPE_CRC_SOURCE_CRTC));
	KUNIT_EXPECT_TRUE(test, amdgpu_dm_is_valid_crc_source(AMDGPU_DM_PIPE_CRC_SOURCE_DPRX));
	KUNIT_EXPECT_TRUE(test,
			  amdgpu_dm_is_valid_crc_source(AMDGPU_DM_PIPE_CRC_SOURCE_CRTC_DITHER));
	KUNIT_EXPECT_TRUE(test,
			  amdgpu_dm_is_valid_crc_source(AMDGPU_DM_PIPE_CRC_SOURCE_DPRX_DITHER));

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_is_valid_crc_source(AMDGPU_DM_PIPE_CRC_SOURCE_NONE));
	KUNIT_EXPECT_FALSE(test, amdgpu_dm_is_valid_crc_source(AMDGPU_DM_PIPE_CRC_SOURCE_MAX));
	KUNIT_EXPECT_FALSE(test, amdgpu_dm_is_valid_crc_source(AMDGPU_DM_PIPE_CRC_SOURCE_INVALID));
}

static struct kunit_case dm_crc_test_cases[] = {
	KUNIT_CASE(dm_test_parse_crc_source_none),
	KUNIT_CASE(dm_test_parse_crc_source_crtc),
	KUNIT_CASE(dm_test_parse_crc_source_dprx),
	KUNIT_CASE(dm_test_parse_crc_source_crtc_dither),
	KUNIT_CASE(dm_test_parse_crc_source_dprx_dither),
	KUNIT_CASE(dm_test_parse_crc_source_invalid),
	KUNIT_CASE(dm_test_is_crc_source_crtc),
	KUNIT_CASE(dm_test_is_crc_source_dprx),
	KUNIT_CASE(dm_test_need_crc_dither),
	KUNIT_CASE(dm_test_is_valid_crc_source),
	{}
};

static struct kunit_suite dm_crc_test_suite = {
	.name = "amdgpu_dm_crc",
	.test_cases = dm_crc_test_cases,
};

kunit_test_suite(dm_crc_test_suite);

MODULE_LICENSE("Dual MIT/GPL");
MODULE_DESCRIPTION("KUnit tests for amdgpu_dm_crc");
MODULE_AUTHOR("AMD");
