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

/**
 * dm_test_need_dp_aux() - Test dm_need_dp_aux().
 * @test: KUnit test context.
 *
 * Verifies that dm_need_dp_aux() returns true when the transition starts or
 * stops a DPRX CRC source (requiring the DP AUX handle), and false for
 * non-DPRX transitions such as CRTC or NONE→NONE.
 */
static void dm_test_need_dp_aux(struct kunit *test)
{
	/* Starting a DPRX source always needs AUX, regardless of current source */
	KUNIT_EXPECT_TRUE(test, dm_need_dp_aux(AMDGPU_DM_PIPE_CRC_SOURCE_DPRX,
					       AMDGPU_DM_PIPE_CRC_SOURCE_NONE));
	KUNIT_EXPECT_TRUE(test, dm_need_dp_aux(AMDGPU_DM_PIPE_CRC_SOURCE_DPRX,
					       AMDGPU_DM_PIPE_CRC_SOURCE_CRTC));
	KUNIT_EXPECT_TRUE(test, dm_need_dp_aux(AMDGPU_DM_PIPE_CRC_SOURCE_DPRX_DITHER,
					       AMDGPU_DM_PIPE_CRC_SOURCE_NONE));

	/* Stopping a DPRX source (NONE requested, DPRX was active) needs AUX */
	KUNIT_EXPECT_TRUE(test, dm_need_dp_aux(AMDGPU_DM_PIPE_CRC_SOURCE_NONE,
					       AMDGPU_DM_PIPE_CRC_SOURCE_DPRX));
	KUNIT_EXPECT_TRUE(test, dm_need_dp_aux(AMDGPU_DM_PIPE_CRC_SOURCE_NONE,
					       AMDGPU_DM_PIPE_CRC_SOURCE_DPRX_DITHER));

	/* CRTC transitions do not need AUX */
	KUNIT_EXPECT_FALSE(test, dm_need_dp_aux(AMDGPU_DM_PIPE_CRC_SOURCE_CRTC,
						AMDGPU_DM_PIPE_CRC_SOURCE_NONE));
	KUNIT_EXPECT_FALSE(test, dm_need_dp_aux(AMDGPU_DM_PIPE_CRC_SOURCE_NONE,
						AMDGPU_DM_PIPE_CRC_SOURCE_CRTC));
	KUNIT_EXPECT_FALSE(test, dm_need_dp_aux(AMDGPU_DM_PIPE_CRC_SOURCE_NONE,
						AMDGPU_DM_PIPE_CRC_SOURCE_NONE));
}

/**
 * dm_test_crc_source_should_start_dprx() - Test dm_crc_source_should_start_dprx().
 * @test: KUnit test context.
 *
 * Verifies that dm_crc_source_should_start_dprx() returns true only when CRC
 * is transitioning from off (!enabled) to a DPRX source (enable &&
 * is_dprx(source)), and false for all other combinations including
 * already-enabled or non-DPRX targets.
 */
static void dm_test_crc_source_should_start_dprx(struct kunit *test)
{
	/* CRC off → DPRX: should start */
	KUNIT_EXPECT_TRUE(test,
		dm_crc_source_should_start_dprx(AMDGPU_DM_PIPE_CRC_SOURCE_DPRX,
						AMDGPU_DM_PIPE_CRC_SOURCE_NONE));
	KUNIT_EXPECT_TRUE(test,
		dm_crc_source_should_start_dprx(AMDGPU_DM_PIPE_CRC_SOURCE_DPRX_DITHER,
						AMDGPU_DM_PIPE_CRC_SOURCE_NONE));

	/* CRC already on (any source) → DPRX: should NOT start (already enabled) */
	KUNIT_EXPECT_FALSE(test,
		dm_crc_source_should_start_dprx(AMDGPU_DM_PIPE_CRC_SOURCE_DPRX,
						AMDGPU_DM_PIPE_CRC_SOURCE_CRTC));
	KUNIT_EXPECT_FALSE(test,
		dm_crc_source_should_start_dprx(AMDGPU_DM_PIPE_CRC_SOURCE_DPRX,
						AMDGPU_DM_PIPE_CRC_SOURCE_DPRX));

	/* CRC off → CRTC: not a DPRX start */
	KUNIT_EXPECT_FALSE(test,
		dm_crc_source_should_start_dprx(AMDGPU_DM_PIPE_CRC_SOURCE_CRTC,
						AMDGPU_DM_PIPE_CRC_SOURCE_NONE));

	/* Disabling: should not start */
	KUNIT_EXPECT_FALSE(test,
		dm_crc_source_should_start_dprx(AMDGPU_DM_PIPE_CRC_SOURCE_NONE,
						AMDGPU_DM_PIPE_CRC_SOURCE_DPRX));
}

/**
 * dm_test_crc_source_should_stop_dprx() - Test dm_crc_source_should_stop_dprx().
 * @test: KUnit test context.
 *
 * Verifies that dm_crc_source_should_stop_dprx() returns true only when CRC
 * is transitioning from a DPRX source (enabled && is_dprx(cur_crc_src)) to
 * off (!enable), and false for non-DPRX disables, DPRX starts, and no-op
 * transitions.
 */
static void dm_test_crc_source_should_stop_dprx(struct kunit *test)
{
	/* DPRX → off: should stop */
	KUNIT_EXPECT_TRUE(test,
		dm_crc_source_should_stop_dprx(AMDGPU_DM_PIPE_CRC_SOURCE_NONE,
					       AMDGPU_DM_PIPE_CRC_SOURCE_DPRX));
	KUNIT_EXPECT_TRUE(test,
		dm_crc_source_should_stop_dprx(AMDGPU_DM_PIPE_CRC_SOURCE_NONE,
					       AMDGPU_DM_PIPE_CRC_SOURCE_DPRX_DITHER));

	/* CRTC → off: not a DPRX stop */
	KUNIT_EXPECT_FALSE(test,
		dm_crc_source_should_stop_dprx(AMDGPU_DM_PIPE_CRC_SOURCE_NONE,
					       AMDGPU_DM_PIPE_CRC_SOURCE_CRTC));

	/* off → DPRX: not a stop */
	KUNIT_EXPECT_FALSE(test,
		dm_crc_source_should_stop_dprx(AMDGPU_DM_PIPE_CRC_SOURCE_DPRX,
					       AMDGPU_DM_PIPE_CRC_SOURCE_NONE));

	/* DPRX → DPRX: no transition, not a stop */
	KUNIT_EXPECT_FALSE(test,
		dm_crc_source_should_stop_dprx(AMDGPU_DM_PIPE_CRC_SOURCE_DPRX,
					       AMDGPU_DM_PIPE_CRC_SOURCE_DPRX));

	/* off → off: not a stop */
	KUNIT_EXPECT_FALSE(test,
		dm_crc_source_should_stop_dprx(AMDGPU_DM_PIPE_CRC_SOURCE_NONE,
					       AMDGPU_DM_PIPE_CRC_SOURCE_NONE));
}

static struct kunit_case dm_crc_test_cases[] = {
	/* dm_parse_crc_source() */
	KUNIT_CASE(dm_test_parse_crc_source_none),
	KUNIT_CASE(dm_test_parse_crc_source_crtc),
	KUNIT_CASE(dm_test_parse_crc_source_dprx),
	KUNIT_CASE(dm_test_parse_crc_source_crtc_dither),
	KUNIT_CASE(dm_test_parse_crc_source_dprx_dither),
	KUNIT_CASE(dm_test_parse_crc_source_invalid),
	/* dm_is_crc_source_crtc() */
	KUNIT_CASE(dm_test_is_crc_source_crtc),
	/* dm_is_crc_source_dprx() */
	KUNIT_CASE(dm_test_is_crc_source_dprx),
	/* dm_need_crc_dither() */
	KUNIT_CASE(dm_test_need_crc_dither),
	/* amdgpu_dm_is_valid_crc_source() */
	KUNIT_CASE(dm_test_is_valid_crc_source),
	/* dm_need_dp_aux() */
	KUNIT_CASE(dm_test_need_dp_aux),
	/* dm_crc_source_should_start_dprx() */
	KUNIT_CASE(dm_test_crc_source_should_start_dprx),
	/* dm_crc_source_should_stop_dprx() */
	KUNIT_CASE(dm_test_crc_source_should_stop_dprx),
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
