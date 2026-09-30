// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * KUnit tests for TX gp2 timestamp handling
 *
 * Copyright (C) 2026 Intel Corporation
 */
#include <kunit/test.h>

#include "mld.h"
#include "tx.h"

MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");

static const struct tx_gp2_case {
	const char *desc;
	u32 est_gp2_us;
	u32 est_host_us;
	u32 tx_host_us;
	long valid_for;
	u32 expected_gp2_us;
	bool expected_refresh;
} tx_gp2_cases[] = {
	{
		.desc = "zero offset is valid",
		.est_gp2_us = 2000,
		.est_host_us = 2000,
		.tx_host_us = 3500,
		.valid_for = IWL_MLD_TX_GP2_VALID_PERIOD,
		.expected_gp2_us = 3500,
	},
	{
		.desc = "gp2 behind host",
		.est_gp2_us = 2000,
		.est_host_us = 10000,
		.tx_host_us = 11500,
		.valid_for = IWL_MLD_TX_GP2_VALID_PERIOD,
		.expected_gp2_us = 3500,
	},
	{
		.desc = "gp2 ahead of host",
		.est_gp2_us = 10000,
		.est_host_us = 2000,
		.tx_host_us = 3500,
		.valid_for = IWL_MLD_TX_GP2_VALID_PERIOD,
		.expected_gp2_us = 11500,
	},
	{
		.desc = "gp2 wraps after the estimate",
		.est_gp2_us = U32_MAX - 999,
		.est_host_us = 1000,
		.tx_host_us = 2500,
		.valid_for = IWL_MLD_TX_GP2_VALID_PERIOD,
		.expected_gp2_us = 500,
	},
	{
		.desc = "host time wraps after the estimate",
		.est_gp2_us = 5000,
		.est_host_us = U32_MAX - 999,
		.tx_host_us = 500,
		.valid_for = IWL_MLD_TX_GP2_VALID_PERIOD,
		.expected_gp2_us = 6500,
	},
	{
		.desc = "gp2 landing on 0 is reported as 1",
		.est_gp2_us = U32_MAX - 1499,
		.est_host_us = 1000,
		.tx_host_us = 2500,
		.valid_for = IWL_MLD_TX_GP2_VALID_PERIOD,
		.expected_gp2_us = 1,
	},
	{
		.desc = "within the refresh margin: still timestamped",
		.est_gp2_us = 2000,
		.est_host_us = 10000,
		.tx_host_us = 11500,
		.valid_for = IWL_MLD_TX_GP2_REFRESH_MARGIN - 1,
		.expected_gp2_us = 3500,
		.expected_refresh = true,
	},
	{
		.desc = "outside the refresh margin: no refresh",
		.est_gp2_us = 2000,
		.est_host_us = 10000,
		.tx_host_us = 11500,
		.valid_for = IWL_MLD_TX_GP2_REFRESH_MARGIN + 1,
		.expected_gp2_us = 3500,
	},
	{
		.desc = "at the refresh threshold",
		.est_gp2_us = 2000,
		.est_host_us = 10000,
		.tx_host_us = 11500,
		.valid_for = IWL_MLD_TX_GP2_REFRESH_MARGIN,
		.expected_gp2_us = 3500,
		.expected_refresh = true,
	},
	{
		.desc = "last valid tick",
		.est_gp2_us = 2000,
		.est_host_us = 10000,
		.tx_host_us = 11500,
		.valid_for = 1,
		.expected_gp2_us = 3500,
		.expected_refresh = true,
	},
	{
		.desc = "validity reached: leave the frame unstamped",
		.est_gp2_us = 2000,
		.est_host_us = 10000,
		.tx_host_us = 11500,
		.valid_for = 0,
		.expected_refresh = true,
	},
	{
		.desc = "validity passed: leave the frame unstamped",
		.est_gp2_us = 2000,
		.est_host_us = 10000,
		.tx_host_us = 11500,
		.valid_for = -1,
		.expected_refresh = true,
	},
};

KUNIT_ARRAY_PARAM_DESC(tx_gp2, tx_gp2_cases, desc);

static void test_tx_gp2(struct kunit *test)
{
	/* Pin time near wraparound to exercise wrapped validity deadlines. */
	const unsigned long now =
		ULONG_MAX - IWL_MLD_TX_GP2_REFRESH_MARGIN - 1;
	const struct tx_gp2_case *test_case = test->param_value;
	u32 delta_us = test_case->est_gp2_us - test_case->est_host_us;
	unsigned long valid_until = now + test_case->valid_for;
	bool refresh = !test_case->expected_refresh;
	u32 timestamp;

	timestamp = iwl_mld_tx_gp2_from_est(delta_us, valid_until,
					    test_case->tx_host_us, now,
					    &refresh);
	KUNIT_EXPECT_EQ(test, timestamp, test_case->expected_gp2_us);
	KUNIT_EXPECT_EQ(test, refresh, test_case->expected_refresh);
}

static struct kunit_case tx_gp2_test_cases[] = {
	KUNIT_CASE_PARAM(test_tx_gp2, tx_gp2_gen_params),
	{}
};

static struct kunit_suite tx_gp2_test_suite = {
	.name = "iwl_mld_tx_gp2",
	.test_cases = tx_gp2_test_cases,
};

kunit_test_suite(tx_gp2_test_suite);
