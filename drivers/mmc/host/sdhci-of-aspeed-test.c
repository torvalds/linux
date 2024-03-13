// SPDX-License-Identifier: GPL-2.0-or-later
/* Copyright (C) 2020 IBM Corp. */

#include <kunit/test.h>

static void aspeed_sdhci_phase_ddr52(struct kunit *test)
{
	int rate = 52000000;

	KUNIT_EXPECT_EQ(test, 0,
			aspeed_sdhci_phase_to_tap(NULL, rate, 0));
	KUNIT_EXPECT_EQ(test, 0,
			aspeed_sdhci_phase_to_tap(NULL, rate, 1));
	KUNIT_EXPECT_EQ(test, 1,
			aspeed_sdhci_phase_to_tap(NULL, rate, 2));
	KUNIT_EXPECT_EQ(test, 1,
			aspeed_sdhci_phase_to_tap(NULL, rate, 3));
	KUNIT_EXPECT_EQ(test, 2,
			aspeed_sdhci_phase_to_tap(NULL, rate, 4));
	KUNIT_EXPECT_EQ(test, 3,
			aspeed_sdhci_phase_to_tap(NULL, rate, 5));
	KUNIT_EXPECT_EQ(test, 14,
			aspeed_sdhci_phase_to_tap(NULL, rate, 23));
	KUNIT_EXPECT_EQ(test, 15,
			aspeed_sdhci_phase_to_tap(NULL, rate, 24));
	KUNIT_EXPECT_EQ(test, 15,
			aspeed_sdhci_phase_to_tap(NULL, rate, 25));

	KUNIT_EXPECT_EQ(test, ASPEED_SDHCI_TAP_PARAM_INVERT_CLK | 0,
			aspeed_sdhci_phase_to_tap(NULL, rate, 180));
	KUNIT_EXPECT_EQ(test, ASPEED_SDHCI_TAP_PARAM_INVERT_CLK | 0,
			aspeed_sdhci_phase_to_tap(NULL, rate, 181));
	KUNIT_EXPECT_EQ(test, ASPEED_SDHCI_TAP_PARAM_INVERT_CLK | 1,
			aspeed_sdhci_phase_to_tap(NULL, rate, 182));
	KUNIT_EXPECT_EQ(test, ASPEED_SDHCI_TAP_PARAM_INVERT_CLK | 1,
			aspeed_sdhci_phase_to_tap(NULL, rate, 183));
	KUNIT_EXPECT_EQ(test, ASPEED_SDHCI_TAP_PARAM_INVERT_CLK | 2,
			aspeed_sdhci_phase_to_tap(NULL, rate, 184));
	KUNIT_EXPECT_EQ(test, ASPEED_SDHCI_TAP_PARAM_INVERT_CLK | 3,
			aspeed_sdhci_phase_to_tap(NULL, rate, 185));
	KUNIT_EXPECT_EQ(test, ASPEED_SDHCI_TAP_PARAM_INVERT_CLK | 14,
			aspeed_sdhci_phase_to_tap(NULL, rate, 203));
	KUNIT_EXPECT_EQ(test, ASPEED_SDHCI_TAP_PARAM_INVERT_CLK | 15,
			aspeed_sdhci_phase_to_tap(NULL, rate, 204));
	KUNIT_EXPECT_EQ(test, ASPEED_SDHCI_TAP_PARAM_INVERT_CLK | 15,
			aspeed_sdhci_phase_to_tap(NULL, rate, 205));
}

static void aspeed_sdhci_phase_hs200(struct kunit *test)
{
	int rate = 200000000;

	KUNIT_EXPECT_EQ(test, 0,
			aspeed_sdhci_phase_to_tap(NULL, rate, 0));
	KUNIT_EXPECT_EQ(test, 0,
			aspeed_sdhci_phase_to_tap(NULL, rate, 5));
	KUNIT_EXPECT_EQ(test, 1,
			aspeed_sdhci_phase_to_tap(NULL, rate, 6));
	KUNIT_EXPECT_EQ(test, 1,
			aspeed_sdhci_phase_to_tap(NULL, rate, 7));
	KUNIT_EXPECT_EQ(test, 14,
			aspeed_sdhci_phase_to_tap(NULL, rate, 89));
	KUNIT_EXPECT_EQ(test, 15,
			aspeed_sdhci_phase_to_tap(NULL, rate, 90));
	KUNIT_EXPECT_EQ(test, 15,
			aspeed_sdhci_phase_to_tap(NULL, rate, 91));
	KUNIT_EXPECT_EQ(test, 15,
			aspeed_sdhci_phase_to_tap(NULL, rate, 96));

	KUNIT_EXPECT_EQ(test, ASPEED_SDHCI_TAP_PARAM_INVERT_CLK,
			aspeed_sdhci_phase_to_tap(NULL, rate, 180));
	KUNIT_EXPECT_EQ(test, ASPEED_SDHCI_TAP_PARAM_INVERT_CLK,
			aspeed_sdhci_phase_to_tap(NULL, rate, 185));
	KUNIT_EXPECT_EQ(test, ASPEED_SDHCI_TAP_PARAM_INVERT_CLK | 1,
			aspeed_sdhci_phase_to_tap(NULL, rate, 186));
	KUNIT_EXPECT_EQ(test, ASPEED_SDHCI_TAP_PARAM_INVERT_CLK | 1,
			aspeed_sdhci_phase_to_tap(NULL, rate, 187));
	KUNIT_EXPECT_EQ(test, ASPEED_SDHCI_TAP_PARAM_INVERT_CLK | 14,
			aspeed_sdhci_phase_to_tap(NULL, rate, 269));
	KUNIT_EXPECT_EQ(test, ASPEED_SDHCI_TAP_PARAM_INVERT_CLK | 15,
			aspeed_sdhci_phase_to_tap(NULL, rate, 270));
	KUNIT_EXPECT_EQ(test, ASPEED_SDHCI_TAP_PARAM_INVERT_CLK | 15,
			aspeed_sdhci_phase_to_tap(NULL, rate, 271));
	KUNIT_EXPECT_EQ(test, ASPEED_SDHCI_TAP_PARAM_INVERT_CLK | 15,
			aspeed_sdhci_phase_to_tap(NULL, rate, 276));
}

static struct kunit_case aspeed_sdhci_test_cases[] = {
	KUNIT_CASE(aspeed_sdhci_phase_ddr52),
	KUNIT_CASE(aspeed_sdhci_phase_hs200),
	{}
};

static struct kunit_suite aspeed_sdhci_test_suite = {
	.name = "sdhci-of-aspeed",
	.test_cases = aspeed_sdhci_test_cases,
};

kunit_test_suite(aspeed_sdhci_test_suite);
