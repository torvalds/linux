// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * KUnit tests for amdgpu_dm_psr.c
 *
 * Copyright 2026 Advanced Micro Devices, Inc.
 */

#include <kunit/test.h>

#include "dc.h"
#include "core_types.h"
#include "amdgpu_mode.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_psr.h"
#include "amdgpu_dm_kunit_test_helpers.h"
#include "power_helpers.h"

static struct dc_stream_state *alloc_test_psr_stream(struct kunit *test)
{
	struct dc_link *link;

	link = dm_kunit_alloc_link(test);
	link->psr_settings.psr_feature_enabled = true;

	return dm_kunit_alloc_stream(test, link);
}

static struct core_power *create_test_power_module(struct kunit *test,
		struct dc_stream_state *stream, struct psr_caps *caps)
{
	struct core_power *core_power;

	core_power = kunit_kzalloc(test, sizeof(*core_power), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, core_power);

	core_power->map = kunit_kzalloc(test, sizeof(*core_power->map), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, core_power->map);

	core_power->map[0].stream = stream;
	core_power->map[0].caps = caps;
	core_power->map[0].psr_events = psr_event_vsync;
	core_power->num_entities = 1;

	return core_power;
}

static struct dc_link *alloc_test_psrsu_link(struct kunit *test)
{
	struct dc_link *link = dm_kunit_alloc_link(test);
	struct dc_context *ctx;
	struct dc *dc;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dc);

	link->ctx = ctx;
	ctx->dc = dc;
	dc->ctx = ctx;
	dc->caps.dmcub_support = true;
	ctx->dce_version = DCN_VERSION_3_1;
	link->dpcd_caps.edp_rev = DP_EDP_14;
	link->dpcd_caps.psr_info.psr_version = DP_PSR2_WITH_Y_COORD_ET_SUPPORTED;
	link->dpcd_caps.alpm_caps.bits.AUX_WAKE_ALPM_CAP = 1;
	link->dpcd_caps.psr_info.psr_dpcd_caps.bits.Y_COORDINATE_REQUIRED = 1;

	return link;
}

static struct dc_link *alloc_test_psr_caps_link(struct kunit *test)
{
	struct dc_link *link = alloc_test_psrsu_link(test);

	link->ctx->dc->caps.dmub_caps.psr = true;
	link->connector_signal = SIGNAL_TYPE_EDP;
	link->type = dc_connection_single;

	return link;
}

static struct amdgpu_dm_connector *alloc_test_aconnector(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);

	return aconnector;
}

/* Tests for link_supports_psrsu() */

/**
 * dm_test_link_supports_psrsu_no_dmcub() - DMCUB support is required.
 * @test: KUnit test context.
 */
static void dm_test_link_supports_psrsu_no_dmcub(struct kunit *test)
{
	struct dc_link *link = alloc_test_psrsu_link(test);

	link->ctx->dc->caps.dmcub_support = false;

	KUNIT_EXPECT_FALSE(test, link_supports_psrsu(link));
}

/**
 * dm_test_link_supports_psrsu_old_dcn() - DCN version 3.1 or newer is required.
 * @test: KUnit test context.
 */
static void dm_test_link_supports_psrsu_old_dcn(struct kunit *test)
{
	struct dc_link *link = alloc_test_psrsu_link(test);

	link->ctx->dce_version = DCN_VERSION_3_0;

	KUNIT_EXPECT_FALSE(test, link_supports_psrsu(link));
}

/**
 * dm_test_link_supports_psrsu_panel_unsupported() - Panel PSR-SU caps are required.
 * @test: KUnit test context.
 */
static void dm_test_link_supports_psrsu_panel_unsupported(struct kunit *test)
{
	struct dc_link *link = alloc_test_psrsu_link(test);

	link->dpcd_caps.psr_info.psr_version = 0;

	KUNIT_EXPECT_FALSE(test, link_supports_psrsu(link));
}

/**
 * dm_test_link_supports_psrsu_missing_alpm() - AUX wake ALPM is required.
 * @test: KUnit test context.
 */
static void dm_test_link_supports_psrsu_missing_alpm(struct kunit *test)
{
	struct dc_link *link = alloc_test_psrsu_link(test);

	link->dpcd_caps.alpm_caps.bits.AUX_WAKE_ALPM_CAP = 0;

	KUNIT_EXPECT_FALSE(test, link_supports_psrsu(link));
}

/**
 * dm_test_link_supports_psrsu_missing_y_coordinate() - Y coordinate support is required.
 * @test: KUnit test context.
 */
static void dm_test_link_supports_psrsu_missing_y_coordinate(struct kunit *test)
{
	struct dc_link *link = alloc_test_psrsu_link(test);

	link->dpcd_caps.psr_info.psr_dpcd_caps.bits.Y_COORDINATE_REQUIRED = 0;

	KUNIT_EXPECT_FALSE(test, link_supports_psrsu(link));
}

/**
 * dm_test_link_supports_psrsu_missing_granularity() - Required granularity must
 * be reported by the panel.
 * @test: KUnit test context.
 */
static void dm_test_link_supports_psrsu_missing_granularity(struct kunit *test)
{
	struct dc_link *link = alloc_test_psrsu_link(test);

	link->dpcd_caps.psr_info.psr_dpcd_caps.bits.SU_GRANULARITY_REQUIRED = 1;
	link->dpcd_caps.psr_info.psr2_su_y_granularity_cap = 0;

	KUNIT_EXPECT_FALSE(test, link_supports_psrsu(link));
}

/**
 * dm_test_link_supports_psrsu_debug_mask_disabled() - Debug mask disables PSR-SU.
 * @test: KUnit test context.
 */
static void dm_test_link_supports_psrsu_debug_mask_disabled(struct kunit *test)
{
	struct dc_link *link = alloc_test_psrsu_link(test);
	unsigned int old_debug_mask;

	old_debug_mask = amdgpu_dm_psr_get_dc_debug_mask();
	amdgpu_dm_psr_set_dc_debug_mask(old_debug_mask | DC_DISABLE_PSR_SU);

	KUNIT_EXPECT_FALSE(test, link_supports_psrsu(link));
	amdgpu_dm_psr_set_dc_debug_mask(old_debug_mask);
}

/**
 * dm_test_link_supports_psrsu_temporarily_disabled() - Supported panels still
 * return false while PSR-SU is temporarily disabled.
 * @test: KUnit test context.
 */
static void dm_test_link_supports_psrsu_temporarily_disabled(struct kunit *test)
{
	struct dc_link *link = alloc_test_psrsu_link(test);
	unsigned int old_debug_mask;

	old_debug_mask = amdgpu_dm_psr_get_dc_debug_mask();
	amdgpu_dm_psr_set_dc_debug_mask(old_debug_mask & ~DC_DISABLE_PSR_SU);

	KUNIT_EXPECT_FALSE(test, link_supports_psrsu(link));
	amdgpu_dm_psr_set_dc_debug_mask(old_debug_mask);
}

/* End of tests for link_supports_psrsu() */

/* Tests for amdgpu_dm_set_psr_caps() */

/**
 * dm_test_set_psr_caps_null_link() - NULL link is rejected.
 * @test: KUnit test context.
 */
static void dm_test_set_psr_caps_null_link(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector = alloc_test_aconnector(test);

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_set_psr_caps(NULL, aconnector));
}

/**
 * dm_test_set_psr_caps_null_connector() - NULL connector is rejected.
 * @test: KUnit test context.
 */
static void dm_test_set_psr_caps_null_connector(struct kunit *test)
{
	struct dc_link *link = alloc_test_psr_caps_link(test);

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_set_psr_caps(link, NULL));
}

/**
 * dm_test_set_psr_caps_no_dmub_psr() - DMUB PSR capability is required.
 * @test: KUnit test context.
 */
static void dm_test_set_psr_caps_no_dmub_psr(struct kunit *test)
{
	struct dc_link *link = alloc_test_psr_caps_link(test);
	struct amdgpu_dm_connector *aconnector = alloc_test_aconnector(test);

	link->psr_settings.psr_version = DC_PSR_VERSION_1;
	link->ctx->dc->caps.dmub_caps.psr = false;

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_set_psr_caps(link, aconnector));
	KUNIT_EXPECT_EQ(test, link->psr_settings.psr_version,
			DC_PSR_VERSION_UNSUPPORTED);
}

/**
 * dm_test_set_psr_caps_non_edp() - Only eDP links can enable PSR.
 * @test: KUnit test context.
 */
static void dm_test_set_psr_caps_non_edp(struct kunit *test)
{
	struct dc_link *link = alloc_test_psr_caps_link(test);
	struct amdgpu_dm_connector *aconnector = alloc_test_aconnector(test);

	link->connector_signal = SIGNAL_TYPE_DISPLAY_PORT;

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_set_psr_caps(link, aconnector));
}

/**
 * dm_test_set_psr_caps_disconnected() - Disconnected links cannot enable PSR.
 * @test: KUnit test context.
 */
static void dm_test_set_psr_caps_disconnected(struct kunit *test)
{
	struct dc_link *link = alloc_test_psr_caps_link(test);
	struct amdgpu_dm_connector *aconnector = alloc_test_aconnector(test);

	link->type = dc_connection_none;

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_set_psr_caps(link, aconnector));
}

/**
 * dm_test_set_psr_caps_no_dpcd_psr() - DPCD PSR version is required.
 * @test: KUnit test context.
 */
static void dm_test_set_psr_caps_no_dpcd_psr(struct kunit *test)
{
	struct dc_link *link = alloc_test_psr_caps_link(test);
	struct amdgpu_dm_connector *aconnector = alloc_test_aconnector(test);

	link->dpcd_caps.psr_info.psr_version = 0;

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_set_psr_caps(link, aconnector));
}

/**
 * dm_test_set_psr_caps_edp1_disabled() - eDP panel instance 1 is blocked.
 * @test: KUnit test context.
 */
static void dm_test_set_psr_caps_edp1_disabled(struct kunit *test)
{
	struct dc_link *link = alloc_test_psr_caps_link(test);
	struct dc_link *edp0 = dm_kunit_alloc_link(test);
	struct amdgpu_dm_connector *aconnector = alloc_test_aconnector(test);
	struct dc *dc = link->ctx->dc;

	edp0->connector_signal = SIGNAL_TYPE_EDP;
	dc->links[0] = edp0;
	dc->links[1] = link;
	dc->link_count = 2;

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_set_psr_caps(link, aconnector));
}

/**
 * dm_test_set_psr_caps_success_psr1() - Valid eDP link enables PSR1 caps.
 * @test: KUnit test context.
 */
static void dm_test_set_psr_caps_success_psr1(struct kunit *test)
{
	struct dc_link *link = alloc_test_psr_caps_link(test);
	struct amdgpu_dm_connector *aconnector = alloc_test_aconnector(test);

	KUNIT_EXPECT_TRUE(test, amdgpu_dm_set_psr_caps(link, aconnector));
	KUNIT_EXPECT_EQ(test, link->psr_settings.psr_version, DC_PSR_VERSION_1);
	KUNIT_EXPECT_EQ(test, (int)aconnector->psr_caps.psr_version, 1);
	KUNIT_EXPECT_EQ(test, (int)aconnector->psr_caps.support_ver,
			DP_PSR2_WITH_Y_COORD_ET_SUPPORTED);
}

/* End of tests for amdgpu_dm_set_psr_caps() */

/* Tests for amdgpu_dm_psr_fill_caps() — PSR version mapping */

static void dm_test_psr_fill_caps_version_1(struct kunit *test)
{
	struct dc_link *link = dm_kunit_alloc_link(test);
	struct psr_caps caps;

	memset(&caps, 0, sizeof(caps));
	link->psr_settings.psr_version = DC_PSR_VERSION_1;

	amdgpu_dm_psr_fill_caps(link, &caps);

	KUNIT_EXPECT_EQ(test, (int)caps.psr_version, 1);
}

static void dm_test_psr_fill_caps_version_su1(struct kunit *test)
{
	struct dc_link *link = dm_kunit_alloc_link(test);
	struct psr_caps caps;

	memset(&caps, 0, sizeof(caps));
	link->psr_settings.psr_version = DC_PSR_VERSION_SU_1;

	amdgpu_dm_psr_fill_caps(link, &caps);

	KUNIT_EXPECT_EQ(test, (int)caps.psr_version, 2);
}

static void dm_test_psr_fill_caps_version_unsupported(struct kunit *test)
{
	struct dc_link *link = dm_kunit_alloc_link(test);
	struct psr_caps caps;

	memset(&caps, 0, sizeof(caps));
	link->psr_settings.psr_version = DC_PSR_VERSION_UNSUPPORTED;

	amdgpu_dm_psr_fill_caps(link, &caps);

	/*
	 * Neither DC_PSR_VERSION_1 nor DC_PSR_VERSION_SU_1,
	 * so psr_version stays at its zero-initialised value.
	 */
	KUNIT_EXPECT_EQ(test, (int)caps.psr_version, 0);
}

/* Tests for amdgpu_dm_psr_fill_caps() — RFB setup time */

static void dm_test_psr_fill_caps_setup_time_zero(struct kunit *test)
{
	struct dc_link *link = dm_kunit_alloc_link(test);
	struct psr_caps caps;

	memset(&caps, 0, sizeof(caps));
	/* PSR_SETUP_TIME = 0 → (6 - 0) * 55 = 330 */
	link->dpcd_caps.psr_info.psr_dpcd_caps.bits.PSR_SETUP_TIME = 0;

	amdgpu_dm_psr_fill_caps(link, &caps);

	KUNIT_EXPECT_EQ(test, caps.psr_rfb_setup_time, 330U);
}

static void dm_test_psr_fill_caps_setup_time_mid(struct kunit *test)
{
	struct dc_link *link = dm_kunit_alloc_link(test);
	struct psr_caps caps;

	memset(&caps, 0, sizeof(caps));
	/* PSR_SETUP_TIME = 3 → (6 - 3) * 55 = 165 */
	link->dpcd_caps.psr_info.psr_dpcd_caps.bits.PSR_SETUP_TIME = 3;

	amdgpu_dm_psr_fill_caps(link, &caps);

	KUNIT_EXPECT_EQ(test, caps.psr_rfb_setup_time, 165U);
}

static void dm_test_psr_fill_caps_setup_time_max(struct kunit *test)
{
	struct dc_link *link = dm_kunit_alloc_link(test);
	struct psr_caps caps;

	memset(&caps, 0, sizeof(caps));
	/* PSR_SETUP_TIME = 6 → (6 - 6) * 55 = 0 */
	link->dpcd_caps.psr_info.psr_dpcd_caps.bits.PSR_SETUP_TIME = 6;

	amdgpu_dm_psr_fill_caps(link, &caps);

	KUNIT_EXPECT_EQ(test, caps.psr_rfb_setup_time, 0U);
}

/* Tests for amdgpu_dm_psr_fill_caps() — link training flag */

static void dm_test_psr_fill_caps_link_training_required(struct kunit *test)
{
	struct dc_link *link = dm_kunit_alloc_link(test);
	struct psr_caps caps;

	memset(&caps, 0, sizeof(caps));
	link->dpcd_caps.psr_info.psr_dpcd_caps.bits.LINK_TRAINING_ON_EXIT_NOT_REQUIRED = 0;

	amdgpu_dm_psr_fill_caps(link, &caps);

	KUNIT_EXPECT_TRUE(test, caps.psr_exit_link_training_required);
}

static void dm_test_psr_fill_caps_link_training_not_required(struct kunit *test)
{
	struct dc_link *link = dm_kunit_alloc_link(test);
	struct psr_caps caps;

	memset(&caps, 0, sizeof(caps));
	link->dpcd_caps.psr_info.psr_dpcd_caps.bits.LINK_TRAINING_ON_EXIT_NOT_REQUIRED = 1;

	amdgpu_dm_psr_fill_caps(link, &caps);

	KUNIT_EXPECT_FALSE(test, caps.psr_exit_link_training_required);
}

/* Tests for amdgpu_dm_psr_fill_caps() — DPCD field passthrough */

static void dm_test_psr_fill_caps_dpcd_fields(struct kunit *test)
{
	struct dc_link *link = dm_kunit_alloc_link(test);
	struct psr_caps caps;

	memset(&caps, 0, sizeof(caps));

	link->dpcd_caps.edp_rev = 0x14;
	link->dpcd_caps.psr_info.psr_version = 2;
	link->dpcd_caps.psr_info.psr_dpcd_caps.bits.SU_GRANULARITY_REQUIRED = 1;
	link->dpcd_caps.psr_info.psr_dpcd_caps.bits.Y_COORDINATE_REQUIRED = 1;
	link->dpcd_caps.psr_info.psr2_su_y_granularity_cap = 4;
	link->dpcd_caps.alpm_caps.bits.AUX_WAKE_ALPM_CAP = 1;
	link->dpcd_caps.alpm_caps.bits.PM_STATE_2A_SUPPORT = 1;

	amdgpu_dm_psr_fill_caps(link, &caps);

	KUNIT_EXPECT_EQ(test, (int)caps.edp_revision, 0x14);
	KUNIT_EXPECT_EQ(test, (int)caps.support_ver, 2);
	KUNIT_EXPECT_TRUE(test, caps.su_granularity_required);
	KUNIT_EXPECT_TRUE(test, caps.y_coordinate_required);
	KUNIT_EXPECT_EQ(test, (int)caps.su_y_granularity, 4);
	KUNIT_EXPECT_TRUE(test, caps.alpm_cap);
	KUNIT_EXPECT_TRUE(test, caps.standby_support);
}

static void dm_test_psr_fill_caps_dpcd_fields_unset(struct kunit *test)
{
	struct dc_link *link = dm_kunit_alloc_link(test);
	struct psr_caps caps;

	memset(&caps, 0xFF, sizeof(caps));

	/* All dpcd_caps fields are zero from kzalloc */
	amdgpu_dm_psr_fill_caps(link, &caps);

	KUNIT_EXPECT_EQ(test, (int)caps.edp_revision, 0);
	KUNIT_EXPECT_EQ(test, (int)caps.support_ver, 0);
	KUNIT_EXPECT_FALSE(test, caps.su_granularity_required);
	KUNIT_EXPECT_FALSE(test, caps.y_coordinate_required);
	KUNIT_EXPECT_EQ(test, (int)caps.su_y_granularity, 0);
	KUNIT_EXPECT_FALSE(test, caps.alpm_cap);
	KUNIT_EXPECT_FALSE(test, caps.standby_support);
}

/* Tests for amdgpu_dm_psr_fill_caps() — rate control and power opts */

static void dm_test_psr_fill_caps_rate_control_always_zero(struct kunit *test)
{
	struct dc_link *link = dm_kunit_alloc_link(test);
	struct psr_caps caps;

	/* Pre-fill caps with non-zero to verify overwrite */
	memset(&caps, 0xFF, sizeof(caps));

	amdgpu_dm_psr_fill_caps(link, &caps);

	KUNIT_EXPECT_EQ(test, (int)caps.rate_control_caps, 0);
}

static void dm_test_psr_fill_caps_power_opts_z10_always_set(struct kunit *test)
{
	struct dc_link *link = dm_kunit_alloc_link(test);
	struct psr_caps caps;

	memset(&caps, 0, sizeof(caps));

	amdgpu_dm_psr_fill_caps(link, &caps);

	/*
	 * psr_power_opt_z10_static_screen is always added to power_opts
	 * regardless of amdgpu_dc_feature_mask.
	 */
	KUNIT_EXPECT_TRUE(test,
			  (caps.psr_power_opt_flag &
			   psr_power_opt_z10_static_screen) != 0);
}

static void dm_test_psr_fill_caps_power_opts_smu_opt_set(struct kunit *test)
{
	struct dc_link *link = dm_kunit_alloc_link(test);
	struct psr_caps caps;
	unsigned int old_feature_mask;

	memset(&caps, 0, sizeof(caps));
	old_feature_mask = amdgpu_dm_psr_get_dc_feature_mask();
	amdgpu_dm_psr_set_dc_feature_mask(old_feature_mask | DC_PSR_ALLOW_SMU_OPT);

	amdgpu_dm_psr_fill_caps(link, &caps);
	amdgpu_dm_psr_set_dc_feature_mask(old_feature_mask);

	KUNIT_EXPECT_TRUE(test,
			  (caps.psr_power_opt_flag &
			   psr_power_opt_smu_opt_static_screen) != 0);
}
/* End of tests for amdgpu_dm_psr_fill_caps() */

/* Tests for amdgpu_dm_psr_set_event() — early-exit validation guards */

static void dm_test_psr_set_event_null_stream(struct kunit *test)
{
	/* NULL stream → immediate false, dm is not accessed */
	KUNIT_EXPECT_FALSE(test, amdgpu_dm_psr_set_event(NULL, NULL, true, psr_event_vsync, false));
}

static void dm_test_psr_set_event_null_link(struct kunit *test)
{
	struct dc_stream_state *stream;

	stream = kunit_kzalloc(test, sizeof(*stream), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, stream);
	/* stream->link remains NULL from kzalloc */

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_psr_set_event(NULL, stream, true, psr_event_vsync, false));
}

static void dm_test_psr_set_event_psr_not_enabled(struct kunit *test)
{
	struct dc_stream_state *stream;
	struct dc_link *link;

	stream = kunit_kzalloc(test, sizeof(*stream), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, stream);

	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, link);

	stream->link = link;
	/* link->psr_settings.psr_feature_enabled remains false from kzalloc */

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_psr_set_event(NULL, stream, true, psr_event_vsync, false));
}

/**
 * dm_test_psr_set_event_get_event_fails() - Failed power event read returns false.
 * @test: KUnit test context.
 */
static void dm_test_psr_set_event_get_event_fails(struct kunit *test)
{
	struct amdgpu_display_manager *dm = dm_kunit_alloc_dm(test);
	struct dc_stream_state *stream = alloc_test_psr_stream(test);

	dm->power_module = NULL;

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_psr_set_event(dm, stream, true, psr_event_vsync, false));
}

/**
 * dm_test_psr_set_event_already_set() - Already set event returns true.
 * @test: KUnit test context.
 */
static void dm_test_psr_set_event_already_set(struct kunit *test)
{
	struct amdgpu_display_manager *dm = dm_kunit_alloc_dm(test);
	struct dc_stream_state *stream = alloc_test_psr_stream(test);
	struct psr_caps caps = {0};
	struct core_power *core_power;

	caps.psr_version = 1;
	core_power = create_test_power_module(test, stream, &caps);
	dm->power_module = &core_power->mod_public;

	KUNIT_EXPECT_TRUE(test,
			  amdgpu_dm_psr_set_event(dm, stream, true, psr_event_vsync, false));
	KUNIT_EXPECT_EQ(test, core_power->map[0].psr_events,
			(unsigned int)psr_event_vsync);
}

/**
 * dm_test_psr_set_event_updates_event() - Changed event delegates to mod_power.
 * @test: KUnit test context.
 */
static void dm_test_psr_set_event_updates_event(struct kunit *test)
{
	struct amdgpu_display_manager *dm = dm_kunit_alloc_dm(test);
	struct dc_stream_state *stream = alloc_test_psr_stream(test);
	struct psr_caps caps = {0};
	struct core_power *core_power;

	caps.psr_version = 1;
	core_power = create_test_power_module(test, stream, &caps);
	dm->power_module = &core_power->mod_public;

	KUNIT_EXPECT_TRUE(test,
			  amdgpu_dm_psr_set_event(dm, stream, true, psr_event_full_screen, false));
	KUNIT_EXPECT_EQ(test, core_power->map[0].psr_events,
			(unsigned int)(psr_event_vsync | psr_event_full_screen));
}
/* End of tests for amdgpu_dm_psr_set_event() */

/* Tests for amdgpu_dm_psr_is_active_allowed() */

/**
 * dm_test_psr_is_active_allowed_no_streams() - Empty DC state disallows PSR.
 * @test: KUnit test context.
 */
static void dm_test_psr_is_active_allowed_no_streams(struct kunit *test)
{
	struct amdgpu_display_manager *dm = dm_kunit_alloc_dm(test);

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_psr_is_active_allowed(dm));
}

/**
 * dm_test_psr_is_active_allowed_null_link() - Streams without links are skipped.
 * @test: KUnit test context.
 */
static void dm_test_psr_is_active_allowed_null_link(struct kunit *test)
{
	struct amdgpu_display_manager *dm = dm_kunit_alloc_dm(test);
	struct dc_state *state = dm->dc->current_state;

	dm_kunit_add_stream_to_state(test, state, 0, NULL);

	KUNIT_EXPECT_FALSE(test, amdgpu_dm_psr_is_active_allowed(dm));
}

/**
 * dm_test_psr_is_active_allowed_requires_enabled_and_allowed() - Both link flags
 * must be set before PSR active is allowed.
 * @test: KUnit test context.
 */
static void dm_test_psr_is_active_allowed_requires_enabled_and_allowed(struct kunit *test)
{
	struct amdgpu_display_manager *dm = dm_kunit_alloc_dm(test);
	struct dc_state *state = dm->dc->current_state;
	struct dc_link *link = dm_kunit_alloc_link(test);

	dm_kunit_add_stream_to_state(test, state, 0, link);
	link->psr_settings.psr_allow_active = true;
	KUNIT_EXPECT_FALSE(test, amdgpu_dm_psr_is_active_allowed(dm));

	link->psr_settings.psr_allow_active = false;
	link->psr_settings.psr_feature_enabled = true;
	KUNIT_EXPECT_FALSE(test, amdgpu_dm_psr_is_active_allowed(dm));
}

/**
 * dm_test_psr_is_active_allowed_any_stream() - Any enabled and allowed stream
 * permits active PSR.
 * @test: KUnit test context.
 */
static void dm_test_psr_is_active_allowed_any_stream(struct kunit *test)
{
	struct amdgpu_display_manager *dm = dm_kunit_alloc_dm(test);
	struct dc_state *state = dm->dc->current_state;
	struct dc_link *disabled_link = dm_kunit_alloc_link(test);
	struct dc_link *allowed_link = dm_kunit_alloc_link(test);

	disabled_link->psr_settings.psr_allow_active = true;
	allowed_link->psr_settings.psr_feature_enabled = true;
	allowed_link->psr_settings.psr_allow_active = true;

	dm_kunit_add_stream_to_state(test, state, 0, disabled_link);
	dm_kunit_add_stream_to_state(test, state, 1, allowed_link);

	KUNIT_EXPECT_TRUE(test, amdgpu_dm_psr_is_active_allowed(dm));
}

/* End of tests for amdgpu_dm_psr_is_active_allowed() */

static struct kunit_case dm_psr_test_cases[] = {
	/* link_supports_psrsu */
	KUNIT_CASE(dm_test_link_supports_psrsu_no_dmcub),
	KUNIT_CASE(dm_test_link_supports_psrsu_old_dcn),
	KUNIT_CASE(dm_test_link_supports_psrsu_panel_unsupported),
	KUNIT_CASE(dm_test_link_supports_psrsu_missing_alpm),
	KUNIT_CASE(dm_test_link_supports_psrsu_missing_y_coordinate),
	KUNIT_CASE(dm_test_link_supports_psrsu_missing_granularity),
	KUNIT_CASE(dm_test_link_supports_psrsu_debug_mask_disabled),
	KUNIT_CASE(dm_test_link_supports_psrsu_temporarily_disabled),
	/* amdgpu_dm_set_psr_caps */
	KUNIT_CASE(dm_test_set_psr_caps_null_link),
	KUNIT_CASE(dm_test_set_psr_caps_null_connector),
	KUNIT_CASE(dm_test_set_psr_caps_no_dmub_psr),
	KUNIT_CASE(dm_test_set_psr_caps_non_edp),
	KUNIT_CASE(dm_test_set_psr_caps_disconnected),
	KUNIT_CASE(dm_test_set_psr_caps_no_dpcd_psr),
	KUNIT_CASE(dm_test_set_psr_caps_edp1_disabled),
	KUNIT_CASE(dm_test_set_psr_caps_success_psr1),
	/* amdgpu_dm_psr_fill_caps */
	KUNIT_CASE(dm_test_psr_fill_caps_version_1),
	KUNIT_CASE(dm_test_psr_fill_caps_version_su1),
	KUNIT_CASE(dm_test_psr_fill_caps_version_unsupported),
	KUNIT_CASE(dm_test_psr_fill_caps_setup_time_zero),
	KUNIT_CASE(dm_test_psr_fill_caps_setup_time_mid),
	KUNIT_CASE(dm_test_psr_fill_caps_setup_time_max),
	KUNIT_CASE(dm_test_psr_fill_caps_link_training_required),
	KUNIT_CASE(dm_test_psr_fill_caps_link_training_not_required),
	KUNIT_CASE(dm_test_psr_fill_caps_dpcd_fields),
	KUNIT_CASE(dm_test_psr_fill_caps_dpcd_fields_unset),
	KUNIT_CASE(dm_test_psr_fill_caps_rate_control_always_zero),
	KUNIT_CASE(dm_test_psr_fill_caps_power_opts_z10_always_set),
	KUNIT_CASE(dm_test_psr_fill_caps_power_opts_smu_opt_set),
	/* amdgpu_dm_psr_set_event */
	KUNIT_CASE(dm_test_psr_set_event_null_stream),
	KUNIT_CASE(dm_test_psr_set_event_null_link),
	KUNIT_CASE(dm_test_psr_set_event_psr_not_enabled),
	KUNIT_CASE(dm_test_psr_set_event_get_event_fails),
	KUNIT_CASE(dm_test_psr_set_event_already_set),
	KUNIT_CASE(dm_test_psr_set_event_updates_event),
	/* amdgpu_dm_psr_is_active_allowed */
	KUNIT_CASE(dm_test_psr_is_active_allowed_no_streams),
	KUNIT_CASE(dm_test_psr_is_active_allowed_null_link),
	KUNIT_CASE(dm_test_psr_is_active_allowed_requires_enabled_and_allowed),
	KUNIT_CASE(dm_test_psr_is_active_allowed_any_stream),
	{}
};

static struct kunit_suite dm_psr_test_suite = {
	.name = "amdgpu_dm_psr",
	.test_cases = dm_psr_test_cases,
};

kunit_test_suite(dm_psr_test_suite);

MODULE_LICENSE("Dual MIT/GPL");
MODULE_DESCRIPTION("KUnit tests for amdgpu_dm_psr");
MODULE_AUTHOR("AMD");
