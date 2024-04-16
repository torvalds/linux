// SPDX-License-Identifier: GPL-2.0-only
/*
 * KUnit tests for channel helper functions
 *
 * Copyright (C) 2024 Intel Corporation
 */
#include <net/mac80211.h>
#include "../mvm.h"
#include <kunit/test.h>

MODULE_IMPORT_NS(EXPORTED_FOR_KUNIT_TESTING);

static struct ieee80211_channel chan_5ghz = {
	.band = NL80211_BAND_5GHZ,
};

static struct ieee80211_channel chan_6ghz = {
	.band = NL80211_BAND_6GHZ,
};

static struct ieee80211_channel chan_2ghz = {
	.band = NL80211_BAND_2GHZ,
};

static struct iwl_mvm_phy_ctxt ctx = {};

static struct iwl_mvm_vif_link_info mvm_link = {
	.phy_ctxt = &ctx,
	.active = true
};

static struct cfg80211_bss bss = {};

static struct ieee80211_bss_conf link_conf = {.bss = &bss};

static const struct link_grading_case {
	const char *desc;
	const struct cfg80211_chan_def chandef;
	s32 signal;
	s16 channel_util;
	int chan_load_by_us;
	unsigned int grade;
} link_grading_cases[] = {
	{
		.desc = "UHB, RSSI below range, no factors",
		.chandef = {
			.chan = &chan_6ghz,
			.width = NL80211_CHAN_WIDTH_20,
		},
		.signal = -100,
		.grade = 177,
	},
	{
		.desc = "LB, RSSI in range, no factors",
		.chandef = {
			.chan = &chan_2ghz,
			.width = NL80211_CHAN_WIDTH_20,
		},
		.signal = -84,
		.grade = 344,
	},
	{
		.desc = "HB, RSSI above range, no factors",
		.chandef = {
			.chan = &chan_5ghz,
			.width = NL80211_CHAN_WIDTH_20,
		},
		.signal = -50,
		.grade = 3442,
	},
	{
		.desc = "HB, BSS Load IE (20 percent), inactive link, no puncturing factor",
		.chandef = {
			.chan = &chan_5ghz,
			.width = NL80211_CHAN_WIDTH_20,
		},
		.signal = -66,
		.channel_util = 51,
		.grade = 1836,
	},
	{
		.desc = "LB, BSS Load IE (20 percent), active link, chan_load_by_us=10 percent. No puncturing factor",
		.chandef = {
			.chan = &chan_2ghz,
			.width = NL80211_CHAN_WIDTH_20,
		},
		.signal = -61,
		.channel_util = 51,
		.chan_load_by_us = 10,
		.grade = 2061,
	},
	{
		.desc = "UHB, BSS Load IE (40 percent), active link, chan_load_by_us=50 (invalid) percent. No puncturing factor",
		.chandef = {
			.chan = &chan_6ghz,
			.width = NL80211_CHAN_WIDTH_20,
		},
		.signal = -66,
		.channel_util = 102,
		.chan_load_by_us = 50,
		.grade = 1552,
	},
	{	.desc = "HB, 80 MHz, no channel load factor, punctured percentage 0",
		.chandef = {
			.chan = &chan_5ghz,
			.width = NL80211_CHAN_WIDTH_80,
			.punctured = 0x0000
		},
		.signal = -72,
		.grade = 1750,
	},
	{	.desc = "HB, 160 MHz, no channel load factor, punctured percentage 25",
		.chandef = {
			.chan = &chan_5ghz,
			.width = NL80211_CHAN_WIDTH_160,
			.punctured = 0x3
		},
		.signal = -72,
		.grade = 1312,
	},
	{	.desc = "UHB, 320 MHz, no channel load factor, punctured percentage 12.5 (2/16)",
		.chandef = {
			.chan = &chan_6ghz,
			.width = NL80211_CHAN_WIDTH_320,
			.punctured = 0x3
		},
		.signal = -72,
		.grade = 1806,
	},
	{	.desc = "HB, 160 MHz, channel load 20, channel load by us 10, punctured percentage 25",
		.chandef = {
			.chan = &chan_5ghz,
			.width = NL80211_CHAN_WIDTH_160,
			.punctured = 0x3
		},
		.channel_util = 51,
		.chan_load_by_us = 10,
		.signal = -72,
		.grade = 1179,
	},
};

KUNIT_ARRAY_PARAM_DESC(link_grading, link_grading_cases, desc)

static void setup_link_conf(struct kunit *test)
{
	const struct link_grading_case *params = test->param_value;
	size_t vif_size = sizeof(struct ieee80211_vif) +
		sizeof(struct iwl_mvm_vif);
	struct ieee80211_vif *vif = kunit_kzalloc(test, vif_size, GFP_KERNEL);
	struct ieee80211_bss_load_elem *bss_load;
	struct element *element;
	size_t ies_size = sizeof(struct cfg80211_bss_ies) + sizeof(*bss_load) + sizeof(element);
	struct cfg80211_bss_ies *ies;
	struct iwl_mvm_vif *mvmvif;

	KUNIT_ASSERT_NOT_NULL(test, vif);

	mvmvif = iwl_mvm_vif_from_mac80211(vif);
	if (params->chan_load_by_us > 0) {
		ctx.channel_load_by_us = params->chan_load_by_us;
		mvmvif->link[0] = &mvm_link;
	}

	link_conf.vif = vif;
	link_conf.chanreq.oper = params->chandef;
	bss.signal = DBM_TO_MBM(params->signal);

	ies = kunit_kzalloc(test, ies_size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ies);
	ies->len = sizeof(*bss_load) + sizeof(struct element);

	element = (void *)ies->data;
	element->datalen = sizeof(*bss_load);
	element->id = 11;

	bss_load = (void *)element->data;
	bss_load->channel_util = params->channel_util;

	rcu_assign_pointer(bss.ies, ies);
}

static void test_link_grading(struct kunit *test)
{
	const struct link_grading_case *params = test->param_value;
	unsigned int ret;

	setup_link_conf(test);

	rcu_read_lock();
	ret = iwl_mvm_get_link_grade(&link_conf);
	rcu_read_unlock();

	KUNIT_EXPECT_EQ(test, ret, params->grade);

	kunit_kfree(test, link_conf.vif);
	RCU_INIT_POINTER(bss.ies, NULL);
}

static struct kunit_case link_grading_test_cases[] = {
	KUNIT_CASE_PARAM(test_link_grading, link_grading_gen_params),
	{}
};

static struct kunit_suite link_grading = {
	.name = "iwlmvm-link-grading",
	.test_cases = link_grading_test_cases,
};

kunit_test_suite(link_grading);
