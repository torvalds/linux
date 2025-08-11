// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * KUnit tests for link selection functions
 *
 * Copyright (C) 2025 Intel Corporation
 */
#include <kunit/static_stub.h>

#include "utils.h"
#include "mld.h"
#include "link.h"
#include "iface.h"
#include "phy.h"
#include "mlo.h"

static const struct link_grading_test_case {
	const char *desc;
	struct {
		struct {
			u8 link_id;
			const struct cfg80211_chan_def *chandef;
			bool active;
			s32 signal;
			bool has_chan_util_elem;
			u8 chan_util; /* 0-255 , used only if has_chan_util_elem is true */
			u8 chan_load_by_us; /* 0-100, used only if active is true */;
		} link;
	} input;
	unsigned int expected_grade;
} link_grading_cases[] = {
	{
		.desc = "channel util of 128 (50%)",
		.input.link = {
			.link_id = 0,
			.chandef = &chandef_2ghz_20mhz,
			.active = false,
			.has_chan_util_elem = true,
			.chan_util = 128,
		},
		.expected_grade = 86,
	},
	{
		.desc = "channel util of 180 (70%)",
		.input.link = {
			.link_id = 0,
			.chandef = &chandef_2ghz_20mhz,
			.active = false,
			.has_chan_util_elem = true,
			.chan_util = 180,
		},
		.expected_grade = 51,
	},
	{
		.desc = "channel util of 180 (70%), channel load by us of 10%",
		.input.link = {
			.link_id = 0,
			.chandef = &chandef_2ghz_20mhz,
			.has_chan_util_elem = true,
			.chan_util = 180,
			.active = true,
			.chan_load_by_us = 10,
		},
		.expected_grade = 67,
	},
		{
		.desc = "no channel util element",
		.input.link = {
			.link_id = 0,
			.chandef = &chandef_2ghz_20mhz,
			.active = true,
		},
		.expected_grade = 120,
	},
};

KUNIT_ARRAY_PARAM_DESC(link_grading, link_grading_cases, desc);

static void setup_link(struct ieee80211_bss_conf *link)
{
	struct kunit *test = kunit_get_current_test();
	struct iwl_mld *mld = test->priv;
	const struct link_grading_test_case *test_param =
		(const void *)(test->param_value);

	KUNIT_ALLOC_AND_ASSERT(test, link->bss);

	link->bss->signal = DBM_TO_MBM(test_param->input.link.signal);

	link->chanreq.oper = *test_param->input.link.chandef;

	if (test_param->input.link.has_chan_util_elem) {
		struct cfg80211_bss_ies *ies;
		struct ieee80211_bss_load_elem bss_load = {
			.channel_util = test_param->input.link.chan_util,
		};
		struct element *elem =
			iwlmld_kunit_gen_element(WLAN_EID_QBSS_LOAD,
						 &bss_load,
						 sizeof(bss_load));
		unsigned int elem_len = sizeof(*elem) + sizeof(bss_load);

		KUNIT_ALLOC_AND_ASSERT_SIZE(test, ies, sizeof(*ies) + elem_len);
		memcpy(ies->data, elem, elem_len);
		ies->len = elem_len;
		rcu_assign_pointer(link->bss->beacon_ies, ies);
		rcu_assign_pointer(link->bss->ies, ies);
	}

	if (test_param->input.link.active) {
		struct ieee80211_chanctx_conf *chan_ctx =
			wiphy_dereference(mld->wiphy, link->chanctx_conf);
		struct iwl_mld_phy *phy;

		KUNIT_ASSERT_NOT_NULL(test, chan_ctx);

		phy = iwl_mld_phy_from_mac80211(chan_ctx);

		phy->channel_load_by_us = test_param->input.link.chan_load_by_us;
	}
}

static void test_link_grading(struct kunit *test)
{
	struct iwl_mld *mld = test->priv;
	const struct link_grading_test_case *test_param =
		(const void *)(test->param_value);
	struct ieee80211_vif *vif;
	struct ieee80211_bss_conf *link;
	unsigned int actual_grade;
	/* Extract test case parameters */
	u8 link_id = test_param->input.link.link_id;
	bool active = test_param->input.link.active;
	u16 valid_links;
	struct iwl_mld_kunit_link assoc_link = {
		.chandef = test_param->input.link.chandef,
	};

	/* If the link is not active, use a different link as the assoc link */
	if (active) {
		assoc_link.id = link_id;
		valid_links = BIT(link_id);
	} else {
		assoc_link.id = BIT(ffz(BIT(link_id)));
		valid_links = BIT(assoc_link.id) | BIT(link_id);
	}

	vif = iwlmld_kunit_setup_mlo_assoc(valid_links, &assoc_link);

	wiphy_lock(mld->wiphy);
	link = wiphy_dereference(mld->wiphy, vif->link_conf[link_id]);
	KUNIT_ASSERT_NOT_NULL(test, link);

	setup_link(link);

	actual_grade = iwl_mld_get_link_grade(mld, link);
	wiphy_unlock(mld->wiphy);

	/* Assert that the returned grade matches the expected grade */
	KUNIT_EXPECT_EQ(test, actual_grade, test_param->expected_grade);
}

static struct kunit_case link_selection_cases[] = {
	KUNIT_CASE_PARAM(test_link_grading, link_grading_gen_params),
	{},
};

static struct kunit_suite link_selection = {
	.name = "iwlmld-link-selection-tests",
	.test_cases = link_selection_cases,
	.init = iwlmld_kunit_test_init,
};

kunit_test_suite(link_selection);

static const struct link_pair_case {
	const char *desc;
	const struct cfg80211_chan_def *chandef_a, *chandef_b;
	bool low_latency_vif;
	u32 chan_load_not_by_us;
	bool primary_link_active;
	u32 expected_result;
} link_pair_cases[] = {
	{
		.desc = "Unequal bandwidth, primary link inactive, EMLSR not allowed",
		.low_latency_vif = false,
		.primary_link_active = false,
		.chandef_a = &chandef_5ghz_40mhz,
		.chandef_b = &chandef_6ghz_20mhz,
		.expected_result = IWL_MLD_EMLSR_EXIT_CHAN_LOAD,
	},
	{
		.desc = "Equal bandwidths, sufficient channel load, EMLSR allowed",
		.low_latency_vif = false,
		.primary_link_active = true,
		.chan_load_not_by_us = 11,
		.chandef_a = &chandef_5ghz_40mhz,
		.chandef_b = &chandef_6ghz_40mhz,
		.expected_result = 0,
	},
	{
		.desc = "Equal bandwidths, insufficient channel load, EMLSR not allowed",
		.low_latency_vif = false,
		.primary_link_active = true,
		.chan_load_not_by_us = 6,
		.chandef_a = &chandef_5ghz_80mhz,
		.chandef_b = &chandef_6ghz_80mhz,
		.expected_result = IWL_MLD_EMLSR_EXIT_CHAN_LOAD,
	},
	{
		.desc = "Low latency VIF, sufficient channel load, EMLSR allowed",
		.low_latency_vif = true,
		.primary_link_active = true,
		.chan_load_not_by_us = 6,
		.chandef_a = &chandef_5ghz_160mhz,
		.chandef_b = &chandef_6ghz_160mhz,
		.expected_result = 0,
	},
	{
		.desc = "Different bandwidths (2x ratio), primary link load permits EMLSR",
		.low_latency_vif = false,
		.primary_link_active = true,
		.chan_load_not_by_us = 30,
		.chandef_a = &chandef_5ghz_40mhz,
		.chandef_b = &chandef_6ghz_20mhz,
		.expected_result = 0,
	},
	{
		.desc = "Different bandwidths (4x ratio), primary link load permits EMLSR",
		.low_latency_vif = false,
		.primary_link_active = true,
		.chan_load_not_by_us = 45,
		.chandef_a = &chandef_5ghz_80mhz,
		.chandef_b = &chandef_6ghz_20mhz,
		.expected_result = 0,
	},
	{
		.desc = "Different bandwidths (16x ratio), primary link load insufficient",
		.low_latency_vif = false,
		.primary_link_active = true,
		.chan_load_not_by_us = 45,
		.chandef_a = &chandef_6ghz_320mhz,
		.chandef_b = &chandef_5ghz_20mhz,
		.expected_result = IWL_MLD_EMLSR_EXIT_CHAN_LOAD,
	},
	{
		.desc = "Same band not allowed (2.4 GHz)",
		.low_latency_vif = false,
		.primary_link_active = true,
		.chan_load_not_by_us = 30,
		.chandef_a = &chandef_2ghz_20mhz,
		.chandef_b = &chandef_2ghz_11_20mhz,
		.expected_result = IWL_MLD_EMLSR_EXIT_EQUAL_BAND,
	},
	{
		.desc = "Same band not allowed (5 GHz)",
		.low_latency_vif = false,
		.primary_link_active = true,
		.chan_load_not_by_us = 30,
		.chandef_a = &chandef_5ghz_40mhz,
		.chandef_b = &chandef_5ghz_40mhz,
		.expected_result = IWL_MLD_EMLSR_EXIT_EQUAL_BAND,
	},
	{
		.desc = "Same band allowed (5 GHz separated)",
		.low_latency_vif = false,
		.primary_link_active = true,
		.chan_load_not_by_us = 30,
		.chandef_a = &chandef_5ghz_40mhz,
		.chandef_b = &chandef_5ghz_120_40mhz,
		.expected_result = 0,
	},
	{
		.desc = "Same band not allowed (6 GHz)",
		.low_latency_vif = false,
		.primary_link_active = true,
		.chan_load_not_by_us = 30,
		.chandef_a = &chandef_6ghz_160mhz,
		.chandef_b = &chandef_6ghz_221_160mhz,
		.expected_result = IWL_MLD_EMLSR_EXIT_EQUAL_BAND,
	},
};

KUNIT_ARRAY_PARAM_DESC(link_pair, link_pair_cases, desc);

static void test_iwl_mld_link_pair_allows_emlsr(struct kunit *test)
{
	const struct link_pair_case *params = test->param_value;
	struct iwl_mld *mld = test->priv;
	struct ieee80211_vif *vif;
	/* link A is the primary and link B is the secondary */
	struct iwl_mld_link_sel_data a = {
		.chandef = params->chandef_a,
		.link_id = 4,
	};
	struct iwl_mld_link_sel_data b = {
		.chandef = params->chandef_b,
		.link_id = 5,
	};
	struct iwl_mld_kunit_link assoc_link = {
		.chandef = params->primary_link_active ? a.chandef : b.chandef,
		.id = params->primary_link_active ? a.link_id : b.link_id,
	};
	u32 result;

	vif = iwlmld_kunit_setup_mlo_assoc(BIT(a.link_id) | BIT(b.link_id),
					   &assoc_link);

	if (params->low_latency_vif)
		iwl_mld_vif_from_mac80211(vif)->low_latency_causes = 1;

	wiphy_lock(mld->wiphy);

	/* Simulate channel load */
	if (params->primary_link_active) {
		struct iwl_mld_phy *phy =
			iwlmld_kunit_get_phy_of_link(vif, a.link_id);

		phy->avg_channel_load_not_by_us = params->chan_load_not_by_us;
	}

	result = iwl_mld_emlsr_pair_state(vif, &a, &b);

	wiphy_unlock(mld->wiphy);

	KUNIT_EXPECT_EQ(test, result, params->expected_result);
}

static struct kunit_case link_pair_criteria_test_cases[] = {
	KUNIT_CASE_PARAM(test_iwl_mld_link_pair_allows_emlsr, link_pair_gen_params),
	{}
};

static struct kunit_suite link_pair_criteria_tests = {
	.name = "iwlmld_link_pair_allows_emlsr",
	.test_cases = link_pair_criteria_test_cases,
	.init = iwlmld_kunit_test_init,
};

kunit_test_suite(link_pair_criteria_tests);
