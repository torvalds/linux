// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * KUnit tests for amdgpu_dm_psr.c
 *
 * Copyright 2026 Advanced Micro Devices, Inc.
 */

#include <kunit/test.h>

#include "amdgpu_dm_psr.h"

/*
 * Helper: allocate and zero-initialise a dc_link sufficient for
 * amdgpu_dm_psr_fill_caps() testing.  The function only accesses
 * embedded members (dpcd_caps, psr_settings) so no pointer fields
 * need to be wired up.
 */
static struct dc_link *alloc_test_link(struct kunit *test)
{
	struct dc_link *link;

	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, link);

	return link;
}

/* Tests for amdgpu_dm_psr_fill_caps() — PSR version mapping */

static void dm_test_psr_fill_caps_version_1(struct kunit *test)
{
	struct dc_link *link = alloc_test_link(test);
	struct psr_caps caps;

	memset(&caps, 0, sizeof(caps));
	link->psr_settings.psr_version = DC_PSR_VERSION_1;

	amdgpu_dm_psr_fill_caps(link, &caps);

	KUNIT_EXPECT_EQ(test, (int)caps.psr_version, 1);
}

static void dm_test_psr_fill_caps_version_su1(struct kunit *test)
{
	struct dc_link *link = alloc_test_link(test);
	struct psr_caps caps;

	memset(&caps, 0, sizeof(caps));
	link->psr_settings.psr_version = DC_PSR_VERSION_SU_1;

	amdgpu_dm_psr_fill_caps(link, &caps);

	KUNIT_EXPECT_EQ(test, (int)caps.psr_version, 2);
}

static void dm_test_psr_fill_caps_version_unsupported(struct kunit *test)
{
	struct dc_link *link = alloc_test_link(test);
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
	struct dc_link *link = alloc_test_link(test);
	struct psr_caps caps;

	memset(&caps, 0, sizeof(caps));
	/* PSR_SETUP_TIME = 0 → (6 - 0) * 55 = 330 */
	link->dpcd_caps.psr_info.psr_dpcd_caps.bits.PSR_SETUP_TIME = 0;

	amdgpu_dm_psr_fill_caps(link, &caps);

	KUNIT_EXPECT_EQ(test, caps.psr_rfb_setup_time, 330U);
}

static void dm_test_psr_fill_caps_setup_time_mid(struct kunit *test)
{
	struct dc_link *link = alloc_test_link(test);
	struct psr_caps caps;

	memset(&caps, 0, sizeof(caps));
	/* PSR_SETUP_TIME = 3 → (6 - 3) * 55 = 165 */
	link->dpcd_caps.psr_info.psr_dpcd_caps.bits.PSR_SETUP_TIME = 3;

	amdgpu_dm_psr_fill_caps(link, &caps);

	KUNIT_EXPECT_EQ(test, caps.psr_rfb_setup_time, 165U);
}

static void dm_test_psr_fill_caps_setup_time_max(struct kunit *test)
{
	struct dc_link *link = alloc_test_link(test);
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
	struct dc_link *link = alloc_test_link(test);
	struct psr_caps caps;

	memset(&caps, 0, sizeof(caps));
	link->dpcd_caps.psr_info.psr_dpcd_caps.bits.LINK_TRAINING_ON_EXIT_NOT_REQUIRED = 0;

	amdgpu_dm_psr_fill_caps(link, &caps);

	KUNIT_EXPECT_TRUE(test, caps.psr_exit_link_training_required);
}

static void dm_test_psr_fill_caps_link_training_not_required(struct kunit *test)
{
	struct dc_link *link = alloc_test_link(test);
	struct psr_caps caps;

	memset(&caps, 0, sizeof(caps));
	link->dpcd_caps.psr_info.psr_dpcd_caps.bits.LINK_TRAINING_ON_EXIT_NOT_REQUIRED = 1;

	amdgpu_dm_psr_fill_caps(link, &caps);

	KUNIT_EXPECT_FALSE(test, caps.psr_exit_link_training_required);
}

/* Tests for amdgpu_dm_psr_fill_caps() — DPCD field passthrough */

static void dm_test_psr_fill_caps_dpcd_fields(struct kunit *test)
{
	struct dc_link *link = alloc_test_link(test);
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
	struct dc_link *link = alloc_test_link(test);
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
	struct dc_link *link = alloc_test_link(test);
	struct psr_caps caps;

	/* Pre-fill caps with non-zero to verify overwrite */
	memset(&caps, 0xFF, sizeof(caps));

	amdgpu_dm_psr_fill_caps(link, &caps);

	KUNIT_EXPECT_EQ(test, (int)caps.rate_control_caps, 0);
}

static void dm_test_psr_fill_caps_power_opts_z10_always_set(struct kunit *test)
{
	struct dc_link *link = alloc_test_link(test);
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
/* End of tests for amdgpu_dm_psr_set_event() */

static struct kunit_case dm_psr_test_cases[] = {
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
	KUNIT_CASE(dm_test_psr_set_event_null_stream),
	KUNIT_CASE(dm_test_psr_set_event_null_link),
	KUNIT_CASE(dm_test_psr_set_event_psr_not_enabled),
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
