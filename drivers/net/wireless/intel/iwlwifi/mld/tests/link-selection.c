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
			.chandef = &chandef_2ghz,
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
			.chandef = &chandef_2ghz,
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
			.chandef = &chandef_2ghz,
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
			.chandef = &chandef_2ghz,
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
	u8 assoc_link_id;
	/* Extract test case parameters */
	u8 link_id = test_param->input.link.link_id;
	enum nl80211_band band = test_param->input.link.chandef->chan->band;
	bool active = test_param->input.link.active;
	u16 valid_links;

	/* If the link is not active, use a different link as the assoc link */
	if (active) {
		assoc_link_id = link_id;
		valid_links = BIT(link_id);
	} else {
		assoc_link_id = BIT(ffz(BIT(link_id)));
		valid_links = BIT(assoc_link_id) | BIT(link_id);
	}

	vif = iwlmld_kunit_setup_mlo_assoc(valid_links, assoc_link_id, band);

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
