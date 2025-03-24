// SPDX-License-Identifier: GPL-2.0-only
/*
 * KUnit tests for channel helper functions
 *
 * Copyright (C) 2024 Intel Corporation
 */
#include <net/mac80211.h>
#include "../mvm.h"
#include <kunit/test.h>

MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");

static const struct acs_average_db_case {
	const char *desc;
	u8 neg_dbm[22];
	s8 result;
} acs_average_db_cases[] = {
	{
		.desc = "Smallest possible value, all filled",
		.neg_dbm = {
			128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
			128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
			128, 128
		},
		.result = -128,
	},
	{
		.desc = "Biggest possible value, all filled",
		.neg_dbm = {
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			0, 0,
		},
		.result = 0,
	},
	{
		.desc = "Smallest possible value, partial filled",
		.neg_dbm = {
			128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
			0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff,
		},
		.result = -128,
	},
	{
		.desc = "Biggest possible value, partial filled",
		.neg_dbm = {
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff,
		},
		.result = 0,
	},
	{
		.desc = "Adding -80dBm to -75dBm until it is still rounded to -79dBm",
		.neg_dbm = {
			75, 80, 80, 80, 80, 80, 80, 80, 80, 80,
			80, 80, 80, 80, 80, 80, 80, 0xff, 0xff, 0xff,
			0xff, 0xff,
		},
		.result = -79,
	},
	{
		.desc = "Adding -80dBm to -75dBm until it is just rounded to -80dBm",
		.neg_dbm = {
			75, 80, 80, 80, 80, 80, 80, 80, 80, 80,
			80, 80, 80, 80, 80, 80, 80, 80, 0xff, 0xff,
			0xff, 0xff,
		},
		.result = -80,
	},
};

KUNIT_ARRAY_PARAM_DESC(acs_average_db, acs_average_db_cases, desc)

static void test_acs_average_db(struct kunit *test)
{
	const struct acs_average_db_case *params = test->param_value;
	struct iwl_umac_scan_channel_survey_notif notif;
	int i;

	/* Test the values in the given order */
	for (i = 0; i < ARRAY_SIZE(params->neg_dbm); i++)
		notif.noise[i] = params->neg_dbm[i];
	KUNIT_ASSERT_EQ(test,
			iwl_mvm_average_dbm_values(&notif),
			params->result);

	/* Test in reverse order */
	for (i = 0; i < ARRAY_SIZE(params->neg_dbm); i++)
		notif.noise[ARRAY_SIZE(params->neg_dbm) - i - 1] =
			params->neg_dbm[i];
	KUNIT_ASSERT_EQ(test,
			iwl_mvm_average_dbm_values(&notif),
			params->result);
}

static struct kunit_case acs_average_db_case[] = {
	KUNIT_CASE_PARAM(test_acs_average_db, acs_average_db_gen_params),
	{}
};

static struct kunit_suite acs_average_db = {
	.name = "iwlmvm-acs-average-db",
	.test_cases = acs_average_db_case,
};

kunit_test_suite(acs_average_db);
