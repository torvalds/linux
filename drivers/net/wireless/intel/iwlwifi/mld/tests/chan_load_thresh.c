// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * KUnit tests for channel helper functions
 *
 * Copyright (C) 2026 Intel Corporation
 */
#include <kunit/static_stub.h>
#include "mld.h"
#include "link.h"
#include "iface.h"
#include "utils.h"

MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");

struct test_chan_load_case {
	const char *desc;
	u32 load;
	enum iwl_mld_link_chan_load_level old_lvl;
	enum iwl_mld_link_chan_load_level expected_lvl;
	bool expected_scan_trig;
};

static const struct test_chan_load_case test_chan_load_thresh_cases[] = {
	/* Level-up transitions */
	{
		.desc = "Transition NONE->NONE",
		.load = 20,
		.old_lvl = LINK_CHAN_LOAD_LVL_NONE,
		.expected_lvl = LINK_CHAN_LOAD_LVL_NONE,
		.expected_scan_trig = false,
	},
	{
		.desc = "Transition NONE->LVL1",
		.load = 50,
		.old_lvl = LINK_CHAN_LOAD_LVL_NONE,
		.expected_lvl = LINK_CHAN_LOAD_LVL1,
		.expected_scan_trig = true,
	},
	{
		.desc = "Transition LVL1->LVL2",
		.load = 75,
		.old_lvl = LINK_CHAN_LOAD_LVL1,
		.expected_lvl = LINK_CHAN_LOAD_LVL2,
		.expected_scan_trig = true,
	},
	{
		.desc = "Transition LVL2->LVL3",
		.load = 90,
		.old_lvl = LINK_CHAN_LOAD_LVL2,
		.expected_lvl = LINK_CHAN_LOAD_LVL3,
		.expected_scan_trig = true,
	},

	/* Level-down transitions */
	{
		.desc = "Transition LVL1->NONE",
		.load = 30,
		.old_lvl = LINK_CHAN_LOAD_LVL1,
		.expected_lvl = LINK_CHAN_LOAD_LVL_NONE,
		.expected_scan_trig = false,
	},
	{
		.desc = "Transition LVL2->LVL1",
		.load = 60,
		.old_lvl = LINK_CHAN_LOAD_LVL2,
		.expected_lvl = LINK_CHAN_LOAD_LVL1,
		.expected_scan_trig = false,
	},
	{
		.desc = "Transition LVL3->LVL2",
		.load = 70,
		.old_lvl = LINK_CHAN_LOAD_LVL3,
		.expected_lvl = LINK_CHAN_LOAD_LVL2,
		.expected_scan_trig = false,
	},

	/* No change */
	{
		.desc = "Transition LVL2->LVL2",
		.load = 72,
		.old_lvl = LINK_CHAN_LOAD_LVL2,
		.expected_lvl = LINK_CHAN_LOAD_LVL2,
		.expected_scan_trig = false,
	},
};

KUNIT_ARRAY_PARAM_DESC(test_chan_load_thresh_cases,
		       test_chan_load_thresh_cases, desc);

static void test_chan_load_thresholds(struct kunit *test)
{
	const struct test_chan_load_case *tc = test->param_value;
	struct iwl_mld *mld = test->priv;
	struct ieee80211_vif *vif;
	struct iwl_mld_vif *mld_vif;
	struct ieee80211_bss_conf *link_conf;
	struct iwl_mld_link *mld_link;
	struct iwl_mld_kunit_link assoc_link = {
		.id = 0,
		.chandef = &chandef_6ghz_160mhz,
	};
	bool scan_trig;
	u32 chan_load;

	/* Setup associated non-MLO station */
	vif = iwlmld_kunit_setup_non_mlo_assoc(&assoc_link);
	mld_vif = iwl_mld_vif_from_mac80211(vif);

	link_conf = &vif->bss_conf;
	mld_link = &mld_vif->deflink;

	chan_load = tc->load;
	mld_link->chan_load_lvl = tc->old_lvl;

	/* Execute function under test */
	wiphy_lock(mld->wiphy);
	scan_trig = iwl_mld_chan_load_requires_scan(mld, link_conf, chan_load);
	wiphy_unlock(mld->wiphy);

	/* Check return value */
	KUNIT_EXPECT_EQ(test, tc->expected_scan_trig, scan_trig);

	/* Check updated channel-load level */
	KUNIT_EXPECT_EQ(test, tc->expected_lvl, mld_link->chan_load_lvl);
}

static struct kunit_case chan_load_thresh_test_cases[] = {
	KUNIT_CASE_PARAM(test_chan_load_thresholds,
			 test_chan_load_thresh_cases_gen_params),
	{}
};

static struct kunit_suite chan_load_thresh_test_suite = {
	.name = "iwl_mld_chan_load_threshold_tests",
	.init = iwlmld_kunit_test_init,
	.test_cases = chan_load_thresh_test_cases,
};

kunit_test_suite(chan_load_thresh_test_suite);
