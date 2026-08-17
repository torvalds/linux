// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * KUnit tests for amdgpu_dm_replay.c
 *
 * Copyright 2026 Advanced Micro Devices, Inc.
 */

#include <kunit/test.h>

#include "dc.h"
#include "amdgpu_mode.h"
#include "amdgpu_dm.h"

/* Extern declaration for the function under test */
extern bool amdgpu_dm_link_supports_replay(struct dc_link *link,
					   struct amdgpu_dm_connector *aconnector);

/*
 * Helper: allocate a dc_link, amdgpu_dm_connector, and dm_connector_state
 * wired up so that to_dm_connector_state(aconnector->base.state) works.
 */
struct replay_test_ctx {
	struct dc_link *link;
	struct amdgpu_dm_connector *aconnector;
	struct dm_connector_state *dm_state;
};

static struct replay_test_ctx *alloc_replay_ctx(struct kunit *test)
{
	struct replay_test_ctx *ctx;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	ctx->link = kunit_kzalloc(test, sizeof(*ctx->link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->link);

	ctx->aconnector = kunit_kzalloc(test, sizeof(*ctx->aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->aconnector);

	ctx->dm_state = kunit_kzalloc(test, sizeof(*ctx->dm_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->dm_state);

	/* Wire connector state so to_dm_connector_state() works */
	ctx->aconnector->base.state = &ctx->dm_state->base;

	return ctx;
}

/*
 * Helper: set all conditions for replay support to pass so individual
 * tests can disable one condition at a time.
 */
static void set_all_replay_caps(struct replay_test_ctx *ctx)
{
	ctx->dm_state->freesync_capable = true;
	ctx->aconnector->vsdb_info.replay_mode = true;
	ctx->link->dpcd_caps.edp_rev = EDP_REVISION_13;
	ctx->link->dpcd_caps.alpm_caps.bits.AUX_WAKE_ALPM_CAP = 1;
	ctx->link->dpcd_caps.adaptive_sync_caps.dp_adap_sync_caps.bits.ADAPTIVE_SYNC_SDP_SUPPORT = 1;
	ctx->link->dpcd_caps.pr_info.pixel_deviation_per_line = 1;
	ctx->link->dpcd_caps.pr_info.max_deviation_line = 1;
}

/* Tests for amdgpu_dm_link_supports_replay() — all caps met */

static void dm_test_replay_supports_all_caps(struct kunit *test)
{
	struct replay_test_ctx *ctx = alloc_replay_ctx(test);

	set_all_replay_caps(ctx);

	KUNIT_EXPECT_TRUE(test,
			  amdgpu_dm_link_supports_replay(ctx->link, ctx->aconnector));
}

/* Tests for amdgpu_dm_link_supports_replay() — freesync not capable */

static void dm_test_replay_no_freesync(struct kunit *test)
{
	struct replay_test_ctx *ctx = alloc_replay_ctx(test);

	set_all_replay_caps(ctx);
	ctx->dm_state->freesync_capable = false;

	KUNIT_EXPECT_FALSE(test,
			   amdgpu_dm_link_supports_replay(ctx->link, ctx->aconnector));
}

/* Tests for amdgpu_dm_link_supports_replay() — no replay mode in VSDB */

static void dm_test_replay_no_vsdb_replay_mode(struct kunit *test)
{
	struct replay_test_ctx *ctx = alloc_replay_ctx(test);

	set_all_replay_caps(ctx);
	ctx->aconnector->vsdb_info.replay_mode = false;

	KUNIT_EXPECT_FALSE(test,
			   amdgpu_dm_link_supports_replay(ctx->link, ctx->aconnector));
}

/* Tests for amdgpu_dm_link_supports_replay() — eDP revision too low */

static void dm_test_replay_edp_rev_too_low(struct kunit *test)
{
	struct replay_test_ctx *ctx = alloc_replay_ctx(test);

	set_all_replay_caps(ctx);
	ctx->link->dpcd_caps.edp_rev = EDP_REVISION_12;

	KUNIT_EXPECT_FALSE(test,
			   amdgpu_dm_link_supports_replay(ctx->link, ctx->aconnector));
}

/* Tests for amdgpu_dm_link_supports_replay() — no ALPM AUX wake cap */

static void dm_test_replay_no_alpm_aux_wake(struct kunit *test)
{
	struct replay_test_ctx *ctx = alloc_replay_ctx(test);

	set_all_replay_caps(ctx);
	ctx->link->dpcd_caps.alpm_caps.bits.AUX_WAKE_ALPM_CAP = 0;

	KUNIT_EXPECT_FALSE(test,
			   amdgpu_dm_link_supports_replay(ctx->link, ctx->aconnector));
}

/* Tests for amdgpu_dm_link_supports_replay() — no adaptive sync SDP */

static void dm_test_replay_no_adaptive_sync_sdp(struct kunit *test)
{
	struct replay_test_ctx *ctx = alloc_replay_ctx(test);

	set_all_replay_caps(ctx);
	ctx->link->dpcd_caps.adaptive_sync_caps.dp_adap_sync_caps.bits.ADAPTIVE_SYNC_SDP_SUPPORT = 0;

	KUNIT_EXPECT_FALSE(test,
			   amdgpu_dm_link_supports_replay(ctx->link, ctx->aconnector));
}

/* Tests for amdgpu_dm_link_supports_replay() — zero pixel deviation */

static void dm_test_replay_zero_pixel_deviation(struct kunit *test)
{
	struct replay_test_ctx *ctx = alloc_replay_ctx(test);

	set_all_replay_caps(ctx);
	ctx->link->dpcd_caps.pr_info.pixel_deviation_per_line = 0;

	KUNIT_EXPECT_FALSE(test,
			   amdgpu_dm_link_supports_replay(ctx->link, ctx->aconnector));
}

/* Tests for amdgpu_dm_link_supports_replay() — zero max deviation line */

static void dm_test_replay_zero_max_deviation_line(struct kunit *test)
{
	struct replay_test_ctx *ctx = alloc_replay_ctx(test);

	set_all_replay_caps(ctx);
	ctx->link->dpcd_caps.pr_info.max_deviation_line = 0;

	KUNIT_EXPECT_FALSE(test,
			   amdgpu_dm_link_supports_replay(ctx->link, ctx->aconnector));
}

/* Tests for amdgpu_dm_link_supports_replay() — both deviation fields zero */

static void dm_test_replay_both_deviations_zero(struct kunit *test)
{
	struct replay_test_ctx *ctx = alloc_replay_ctx(test);

	set_all_replay_caps(ctx);
	ctx->link->dpcd_caps.pr_info.pixel_deviation_per_line = 0;
	ctx->link->dpcd_caps.pr_info.max_deviation_line = 0;

	KUNIT_EXPECT_FALSE(test,
			   amdgpu_dm_link_supports_replay(ctx->link, ctx->aconnector));
}

/* End of tests for amdgpu_dm_link_supports_replay() */

static struct kunit_case dm_replay_test_cases[] = {
	KUNIT_CASE(dm_test_replay_supports_all_caps),
	KUNIT_CASE(dm_test_replay_no_freesync),
	KUNIT_CASE(dm_test_replay_no_vsdb_replay_mode),
	KUNIT_CASE(dm_test_replay_edp_rev_too_low),
	KUNIT_CASE(dm_test_replay_no_alpm_aux_wake),
	KUNIT_CASE(dm_test_replay_no_adaptive_sync_sdp),
	KUNIT_CASE(dm_test_replay_zero_pixel_deviation),
	KUNIT_CASE(dm_test_replay_zero_max_deviation_line),
	KUNIT_CASE(dm_test_replay_both_deviations_zero),
	{}
};

static struct kunit_suite dm_replay_test_suite = {
	.name = "amdgpu_dm_replay",
	.test_cases = dm_replay_test_cases,
};

kunit_test_suite(dm_replay_test_suite);

MODULE_LICENSE("Dual MIT/GPL");
MODULE_DESCRIPTION("KUnit tests for amdgpu_dm_replay");
MODULE_AUTHOR("AMD");
