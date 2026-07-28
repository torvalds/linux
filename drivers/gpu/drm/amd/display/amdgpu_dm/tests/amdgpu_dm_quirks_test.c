// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * KUnit tests for amdgpu_dm_quirks.c
 *
 * Copyright 2026 Advanced Micro Devices, Inc.
 */

#include <kunit/test.h>

#include "dc.h"
#include "amdgpu_mode.h"
#include "amdgpu_dm.h"

/* Tests for retrieve_dmi_info() */

/*
 * Verify that retrieve_dmi_info() always initialises aux_hpd_discon_quirk to
 * false, even when the caller had previously set it to true.
 */
/**
 * dm_test_quirks_aux_hpd_discon_reset - Test Quirks aux hpd discon reset
 * @test: The KUnit test context
 */
static void dm_test_quirks_aux_hpd_discon_reset(struct kunit *test)
{
	struct amdgpu_display_manager *dm;

	dm = kunit_kzalloc(test, sizeof(*dm), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dm);

	dm->aux_hpd_discon_quirk = true;

	retrieve_dmi_info(dm);

	/*
	 * In a KUnit / UML environment no real DMI table is present, so
	 * dmi_check_system() returns 0 and retrieve_dmi_info() leaves the
	 * quirk at its initialised-to-false value.
	 */
	KUNIT_EXPECT_FALSE(test, dm->aux_hpd_discon_quirk);
}

/*
 * Verify that retrieve_dmi_info() always initialises edp0_on_dp1_quirk to
 * false, even when the caller had previously set it to true.
 */
/**
 * dm_test_quirks_edp0_on_dp1_reset - Test Quirks edp0 on dp1 reset
 * @test: The KUnit test context
 */
static void dm_test_quirks_edp0_on_dp1_reset(struct kunit *test)
{
	struct amdgpu_display_manager *dm;

	dm = kunit_kzalloc(test, sizeof(*dm), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dm);

	dm->edp0_on_dp1_quirk = true;

	retrieve_dmi_info(dm);

	KUNIT_EXPECT_FALSE(test, dm->edp0_on_dp1_quirk);
}

/*
 * Verify that when no DMI match is found both quirks remain false after a
 * fresh (zero-initialised) dm is passed to retrieve_dmi_info().
 */
/**
 * dm_test_quirks_no_dmi_match_both_false - Test Quirks no dmi match both false
 * @test: The KUnit test context
 */
static void dm_test_quirks_no_dmi_match_both_false(struct kunit *test)
{
	struct amdgpu_display_manager *dm;

	dm = kunit_kzalloc(test, sizeof(*dm), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dm);

	retrieve_dmi_info(dm);

	KUNIT_EXPECT_FALSE(test, dm->aux_hpd_discon_quirk);
	KUNIT_EXPECT_FALSE(test, dm->edp0_on_dp1_quirk);
}

static struct kunit_case amdgpu_dm_quirks_tests[] = {
	/* retrieve_dmi_info */
	KUNIT_CASE(dm_test_quirks_aux_hpd_discon_reset),
	KUNIT_CASE(dm_test_quirks_edp0_on_dp1_reset),
	KUNIT_CASE(dm_test_quirks_no_dmi_match_both_false),
	{}
};

static struct kunit_suite amdgpu_dm_quirks_test_suite = {
	.name = "amdgpu_dm_quirks",
	.test_cases = amdgpu_dm_quirks_tests,
};

kunit_test_suite(amdgpu_dm_quirks_test_suite);

MODULE_AUTHOR("AMD");
MODULE_DESCRIPTION("KUnit tests for amdgpu_dm_quirks");
MODULE_LICENSE("Dual MIT/GPL");
