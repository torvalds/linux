// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * KUnit tests for amdgpu_dm_replay.c
 *
 * Copyright 2026 Advanced Micro Devices, Inc.
 */

#include <kunit/test.h>

#include "dc.h"
#include "dc_dmub_srv.h"
#include "amdgpu_mode.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_replay.h"
#include "amdgpu_dm_kunit_test_helpers.h"
#include "modules/power/power_helpers.h"
#include "dmub/dmub_srv.h"

/*
 * Helper: allocate a dc_link, amdgpu_dm_connector, and dm_connector_state
 * wired up so that to_dm_connector_state(aconnector->base.state) works.
 */
struct replay_test_ctx {
	struct dc_link *link;
	struct amdgpu_dm_connector *aconnector;
	struct dm_connector_state *dm_state;
	struct dc *dc;
	struct dc_context *dc_ctx;
	struct dc_stream_state *stream;
};

static struct replay_test_ctx *alloc_replay_ctx(struct kunit *test)
{
	struct replay_test_ctx *ctx;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	ctx->link = dm_kunit_alloc_link_with_ctx(test);
	ctx->dc_ctx = ctx->link->ctx;
	ctx->dc = ctx->dc_ctx->dc;

	ctx->aconnector = kunit_kzalloc(test, sizeof(*ctx->aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->aconnector);

	ctx->dm_state = kunit_kzalloc(test, sizeof(*ctx->dm_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->dm_state);

	ctx->stream = dm_kunit_alloc_stream(test, ctx->link);

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
	ctx->link->connector_signal = SIGNAL_TYPE_EDP;
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

/* Tests for amdgpu_dm_set_replay_caps() */

/**
 * dm_test_replay_set_caps_already_supported - Verify cached Replay support
 * @test: KUnit test context
 *
 * When replay_supported is already set, amdgpu_dm_set_replay_caps() should
 * return true without revalidating the link capabilities.
 */
static void dm_test_replay_set_caps_already_supported(struct kunit *test)
{
	struct replay_test_ctx *ctx = alloc_replay_ctx(test);

	ctx->link->replay_settings.config.replay_supported = true;

	KUNIT_EXPECT_TRUE(test, amdgpu_dm_set_replay_caps(ctx->link, ctx->aconnector));
}

/**
 * dm_test_replay_set_caps_non_embedded_signal - Verify non-eDP rejection
 * @test: KUnit test context
 *
 * When the link signal is not embedded, amdgpu_dm_set_replay_caps() should
 * reject Replay even if the sink capability fields are otherwise valid.
 */
static void dm_test_replay_set_caps_non_embedded_signal(struct kunit *test)
{
	struct replay_test_ctx *ctx = alloc_replay_ctx(test);

	set_all_replay_caps(ctx);
	ctx->link->connector_signal = SIGNAL_TYPE_DISPLAY_PORT;

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_set_replay_caps(ctx->link, ctx->aconnector));
}

/**
 * dm_test_replay_set_caps_disallowed_by_panel - Verify panel policy rejection
 * @test: KUnit test context
 *
 * When the panel configuration disallows Replay, amdgpu_dm_set_replay_caps()
 * should return false before accepting the capability set.
 */
static void dm_test_replay_set_caps_disallowed_by_panel(struct kunit *test)
{
	struct replay_test_ctx *ctx = alloc_replay_ctx(test);

	set_all_replay_caps(ctx);
	ctx->link->panel_config.psr.disallow_replay = true;

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_set_replay_caps(ctx->link, ctx->aconnector));
}

/**
 * dm_test_replay_set_caps_link_not_supported - Verify capability rejection
 * @test: KUnit test context
 *
 * When amdgpu_dm_link_supports_replay() rejects the link, the higher-level
 * Replay setup helper should also return false.
 */
static void dm_test_replay_set_caps_link_not_supported(struct kunit *test)
{
	struct replay_test_ctx *ctx = alloc_replay_ctx(test);

	set_all_replay_caps(ctx);
	ctx->dm_state->freesync_capable = false;

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_set_replay_caps(ctx->link, ctx->aconnector));
}

/**
 * dm_test_replay_set_caps_missing_dmub_srv - Verify missing DMUB rejection
 * @test: KUnit test context
 *
 * When the link and connector support Replay but no DMUB service is available,
 * amdgpu_dm_set_replay_caps() should return false.
 */
static void dm_test_replay_set_caps_missing_dmub_srv(struct kunit *test)
{
	struct replay_test_ctx *ctx = alloc_replay_ctx(test);

	set_all_replay_caps(ctx);

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_set_replay_caps(ctx->link, ctx->aconnector));
}

/**
 * dm_test_replay_set_caps_success - Verify successful Replay configuration
 * @test: KUnit test context
 *
 * When all prerequisites are met (embedded signal, panel allows replay, link
 * supports replay, DMUB present with replay support), amdgpu_dm_set_replay_caps()
 * should configure the link replay settings and return true.
 */
static void dm_test_replay_set_caps_success(struct kunit *test)
{
	struct replay_test_ctx *ctx = alloc_replay_ctx(test);
	struct dc_dmub_srv *dmub_srv;
	struct dmub_srv *dmub;

	set_all_replay_caps(ctx);

	dmub_srv = kunit_kzalloc(test, sizeof(*dmub_srv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dmub_srv);

	dmub = kunit_kzalloc(test, sizeof(*dmub), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dmub);

	dmub->feature_caps.replay_supported = 1;
	dmub_srv->dmub = dmub;
	ctx->dc_ctx->dmub_srv = dmub_srv;

	KUNIT_EXPECT_TRUE(test, amdgpu_dm_set_replay_caps(ctx->link, ctx->aconnector));
	KUNIT_EXPECT_TRUE(test, ctx->link->replay_settings.config.replay_supported);
}

/* Tests for amdgpu_dm_link_setup_replay() */

/**
 * dm_test_replay_link_setup_null_stream - Verify NULL stream rejection
 * @test: KUnit test context
 *
 * amdgpu_dm_link_setup_replay() should return false when no stream is provided.
 */
static void dm_test_replay_link_setup_null_stream(struct kunit *test)
{
	struct mod_vrr_params vrr_params = { 0 };

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_link_setup_replay(NULL, &vrr_params));
}

/**
 * dm_test_replay_link_setup_null_link - Verify NULL stream link rejection
 * @test: KUnit test context
 *
 * amdgpu_dm_link_setup_replay() should return false when the stream has no
 * associated link.
 */
static void dm_test_replay_link_setup_null_link(struct kunit *test)
{
	struct replay_test_ctx *ctx = alloc_replay_ctx(test);
	struct mod_vrr_params vrr_params = { 0 };

	ctx->stream->link = NULL;

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_link_setup_replay(ctx->stream, &vrr_params));
}

/**
 * dm_test_replay_link_setup_null_vrr_params - Verify NULL VRR params rejection
 * @test: KUnit test context
 *
 * amdgpu_dm_link_setup_replay() should return false when VRR parameters are
 * not supplied.
 */
static void dm_test_replay_link_setup_null_vrr_params(struct kunit *test)
{
	struct replay_test_ctx *ctx = alloc_replay_ctx(test);

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_link_setup_replay(ctx->stream, NULL));
}

/**
 * dm_test_replay_link_setup_not_supported - Verify unsupported Replay rejection
 * @test: KUnit test context
 *
 * amdgpu_dm_link_setup_replay() should return false when Replay is not marked
 * supported on the link configuration.
 */
static void dm_test_replay_link_setup_not_supported(struct kunit *test)
{
	struct replay_test_ctx *ctx = alloc_replay_ctx(test);
	struct mod_vrr_params vrr_params = { 0 };

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_link_setup_replay(ctx->stream, &vrr_params));
}

/**
 * dm_test_replay_link_setup_already_enabled - Verify enabled Replay success
 * @test: KUnit test context
 *
 * When Replay is already enabled, amdgpu_dm_link_setup_replay() should return
 * true without recalculating coasting vtotal state.
 */
static void dm_test_replay_link_setup_already_enabled(struct kunit *test)
{
	struct replay_test_ctx *ctx = alloc_replay_ctx(test);
	struct mod_vrr_params vrr_params = { 0 };

	ctx->link->replay_settings.config.replay_supported = true;
	ctx->link->replay_settings.replay_feature_enabled = true;

	KUNIT_EXPECT_TRUE(test, amdgpu_dm_link_setup_replay(ctx->stream, &vrr_params));
}

/**
 * dm_test_replay_link_setup_success - Verify coasting vtotal configuration
 * @test: KUnit test context
 *
 * When Replay is supported but not yet enabled, amdgpu_dm_link_setup_replay()
 * should calculate the link-off frame count and set the coasting vtotal values,
 * then return true.
 */
static void dm_test_replay_link_setup_success(struct kunit *test)
{
	struct replay_test_ctx *ctx = alloc_replay_ctx(test);
	struct mod_vrr_params vrr_params = { 0 };

	ctx->link->replay_settings.config.replay_supported = true;
	ctx->link->replay_settings.config.replay_version = DC_FREESYNC_REPLAY;

	/* Set timing so calculate_replay_link_off_frame_count computes */
	ctx->stream->timing.v_total = 1125;
	ctx->stream->timing.h_total = 2200;
	ctx->stream->timing.pix_clk_100hz = 1485000;
	ctx->link->dpcd_caps.pr_info.pixel_deviation_per_line = 4;
	ctx->link->dpcd_caps.pr_info.max_deviation_line = 10;

	/* min_refresh_in_uhz = 0 makes calc return v_total directly */
	vrr_params.min_refresh_in_uhz = 0;

	KUNIT_EXPECT_TRUE(test, amdgpu_dm_link_setup_replay(ctx->stream, &vrr_params));

	/* Verify coasting vtotal was set */
	KUNIT_EXPECT_EQ(test,
			ctx->link->replay_settings.coasting_vtotal_table[PR_COASTING_TYPE_NOM],
			(uint32_t)1125);
	KUNIT_EXPECT_EQ(test,
			ctx->link->replay_settings.coasting_vtotal_table[PR_COASTING_TYPE_STATIC],
			(uint32_t)1125);

	/* Verify link_off_frame_count was calculated: 2200*10/(4*1125) = 4 */
	KUNIT_EXPECT_EQ(test,
			ctx->link->replay_settings.link_off_frame_count,
			(uint32_t)4);
}

/* Tests for amdgpu_dm_replay_set_event() */

/**
 * dm_test_replay_set_event_null_stream - Verify NULL stream rejection
 * @test: KUnit test context
 *
 * amdgpu_dm_replay_set_event() should return false when no stream is provided.
 */
static void dm_test_replay_set_event_null_stream(struct kunit *test)
{
	struct amdgpu_display_manager *dm;

	dm = kunit_kzalloc(test, sizeof(*dm), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dm);

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_replay_set_event(dm, NULL, true,
							    replay_event_vsync, false));
}

/**
 * dm_test_replay_set_event_null_link - Verify NULL stream link rejection
 * @test: KUnit test context
 *
 * amdgpu_dm_replay_set_event() should return false when the stream has no
 * associated link.
 */
static void dm_test_replay_set_event_null_link(struct kunit *test)
{
	struct amdgpu_display_manager *dm;
	struct replay_test_ctx *ctx = alloc_replay_ctx(test);

	dm = kunit_kzalloc(test, sizeof(*dm), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dm);

	ctx->stream->link = NULL;

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_replay_set_event(dm, ctx->stream, true,
							    replay_event_vsync, false));
}

/**
 * dm_test_replay_set_event_feature_disabled - Verify disabled Replay rejection
 * @test: KUnit test context
 *
 * amdgpu_dm_replay_set_event() should return false when Replay is not enabled
 * on the stream link.
 */
static void dm_test_replay_set_event_feature_disabled(struct kunit *test)
{
	struct amdgpu_display_manager *dm;
	struct replay_test_ctx *ctx = alloc_replay_ctx(test);

	dm = kunit_kzalloc(test, sizeof(*dm), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dm);

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_replay_set_event(dm, ctx->stream, true,
							    replay_event_vsync, false));
}

/**
 * dm_test_replay_set_event_missing_power_module - Verify missing power rejection
 * @test: KUnit test context
 *
 * When Replay is enabled but no power module is available, the event helper
 * should return false after failing to read the current Replay events.
 */
static void dm_test_replay_set_event_missing_power_module(struct kunit *test)
{
	struct amdgpu_display_manager *dm;
	struct replay_test_ctx *ctx = alloc_replay_ctx(test);

	dm = kunit_kzalloc(test, sizeof(*dm), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dm);

	ctx->link->replay_settings.replay_feature_enabled = true;

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_replay_set_event(dm, ctx->stream, true,
							    replay_event_vsync, false));
}

/**
 * dm_test_replay_set_event_already_set - Verify no-op when event already active
 * @test: KUnit test context
 *
 * When the requested event is already in the desired state, the function should
 * return true without calling mod_power_set_replay_event().
 */
static void dm_test_replay_set_event_already_set(struct kunit *test)
{
	struct amdgpu_display_manager *dm;
	struct replay_test_ctx *ctx = alloc_replay_ctx(test);
	struct core_power *core_power;
	struct power_entity *map;

	dm = kunit_kzalloc(test, sizeof(*dm), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dm);

	core_power = kunit_kzalloc(test, sizeof(*core_power), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, core_power);

	map = kunit_kzalloc(test, sizeof(*map), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, map);

	/* Wire the power module so mod_power_get_replay_event() succeeds */
	map->stream = ctx->stream;
	map->replay_events = replay_event_vsync;
	core_power->map = map;
	core_power->num_entities = 1;
	dm->power_module = &core_power->mod_public;

	ctx->link->replay_settings.replay_feature_enabled = true;

	/* Event already set — should return true without calling set */
	KUNIT_EXPECT_TRUE(test, amdgpu_dm_replay_set_event(dm, ctx->stream, true,
							   replay_event_vsync, false));
}

/**
 * dm_test_replay_set_event_already_clear - Verify no-op when event already cleared
 * @test: KUnit test context
 *
 * When clearing an event that is not currently active, the function should
 * return true without calling mod_power_set_replay_event().
 */
static void dm_test_replay_set_event_already_clear(struct kunit *test)
{
	struct amdgpu_display_manager *dm;
	struct replay_test_ctx *ctx = alloc_replay_ctx(test);
	struct core_power *core_power;
	struct power_entity *map;

	dm = kunit_kzalloc(test, sizeof(*dm), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dm);

	core_power = kunit_kzalloc(test, sizeof(*core_power), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, core_power);

	map = kunit_kzalloc(test, sizeof(*map), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, map);

	/* Wire the power module — replay_events has NO vsync bit */
	map->stream = ctx->stream;
	map->replay_events = 0;
	core_power->map = map;
	core_power->num_entities = 1;
	dm->power_module = &core_power->mod_public;

	ctx->link->replay_settings.replay_feature_enabled = true;

	/* Clearing an event that's already clear — should return true */
	KUNIT_EXPECT_TRUE(test, amdgpu_dm_replay_set_event(dm, ctx->stream, false,
							   replay_event_vsync, false));
}

static struct kunit_case dm_replay_test_cases[] = {
	/* amdgpu_dm_link_supports_replay */
	KUNIT_CASE(dm_test_replay_supports_all_caps),
	KUNIT_CASE(dm_test_replay_no_freesync),
	KUNIT_CASE(dm_test_replay_no_vsdb_replay_mode),
	KUNIT_CASE(dm_test_replay_edp_rev_too_low),
	KUNIT_CASE(dm_test_replay_no_alpm_aux_wake),
	KUNIT_CASE(dm_test_replay_no_adaptive_sync_sdp),
	KUNIT_CASE(dm_test_replay_zero_pixel_deviation),
	KUNIT_CASE(dm_test_replay_zero_max_deviation_line),
	KUNIT_CASE(dm_test_replay_both_deviations_zero),
	/* amdgpu_dm_set_replay_caps */
	KUNIT_CASE(dm_test_replay_set_caps_already_supported),
	KUNIT_CASE(dm_test_replay_set_caps_non_embedded_signal),
	KUNIT_CASE(dm_test_replay_set_caps_disallowed_by_panel),
	KUNIT_CASE(dm_test_replay_set_caps_link_not_supported),
	KUNIT_CASE(dm_test_replay_set_caps_missing_dmub_srv),
	KUNIT_CASE(dm_test_replay_set_caps_success),
	/* amdgpu_dm_link_setup_replay */
	KUNIT_CASE(dm_test_replay_link_setup_null_stream),
	KUNIT_CASE(dm_test_replay_link_setup_null_link),
	KUNIT_CASE(dm_test_replay_link_setup_null_vrr_params),
	KUNIT_CASE(dm_test_replay_link_setup_not_supported),
	KUNIT_CASE(dm_test_replay_link_setup_already_enabled),
	KUNIT_CASE(dm_test_replay_link_setup_success),
	/* amdgpu_dm_replay_set_event */
	KUNIT_CASE(dm_test_replay_set_event_null_stream),
	KUNIT_CASE(dm_test_replay_set_event_null_link),
	KUNIT_CASE(dm_test_replay_set_event_feature_disabled),
	KUNIT_CASE(dm_test_replay_set_event_missing_power_module),
	KUNIT_CASE(dm_test_replay_set_event_already_set),
	KUNIT_CASE(dm_test_replay_set_event_already_clear),
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
