// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * KUnit tests for link selection functions
 *
 * Copyright (C) 2025 Intel Corporation
 */
#include <kunit/static_stub.h>

#include "utils.h"
#include "mld.h"
#include "mlo.h"

static const struct emlsr_with_bt_test_case {
	const char *desc;
	struct {
		struct iwl_bt_coex_profile_notif notif;
		s32 signal;
		bool check_entry;
	} input;
	bool emlsr_allowed;
} emlsr_with_bt_cases[] = {
	{
		.desc = "BT penalty(exit) with low rssi 4.5: emlsr allowed",
		.input = {
			.notif.wifi_loss_low_rssi[1] = {4, 5},
			.notif.wifi_loss_mid_high_rssi[1] = {7, 9},
			.signal = -69,
			.check_entry = false,
		},
		.emlsr_allowed = true,
	},
	{
		.desc = "BT penalty(exit) from high rssi 5: emlsr allowed",
		.input = {
			.notif.wifi_loss_low_rssi[1] = {7, 9},
			.notif.wifi_loss_mid_high_rssi[1] = {5, 5},
			.signal = -68,
			.check_entry = false,
		},
		.emlsr_allowed = true,
	},
	{
		.desc = "BT penalty(exit) with low rssi 8: emlsr not allowed",
		.input = {
			.notif.wifi_loss_low_rssi[1] = {7, 9},
			.notif.wifi_loss_mid_high_rssi[1] = {4, 5},
			.signal = -69,
			.check_entry = false,
		},
		.emlsr_allowed = false,
	},
	{
		.desc = "BT penalty(exit) from high rssi 9: emlsr not allowed",
		.input = {
			.notif.wifi_loss_low_rssi[1] = {4, 5},
			.notif.wifi_loss_mid_high_rssi[1] = {9, 9},
			.signal = -68,
			.check_entry = false,
		},
		.emlsr_allowed = false,
	},
	{
		.desc = "BT penalty(entry) with low rssi 4.5: emlsr allowed",
		.input = {
			.notif.wifi_loss_low_rssi[1] = {4, 5},
			.notif.wifi_loss_mid_high_rssi[1] = {7, 9},
			.signal = -63,
			.check_entry = true,
		},
		.emlsr_allowed = true,
	},
	{
		.desc = "BT penalty(entry) from high rssi 5: emlsr allowed",
		.input = {
			.notif.wifi_loss_low_rssi[1] = {7, 9},
			.notif.wifi_loss_mid_high_rssi[1] = {5, 5},
			.signal = -62,
			.check_entry = false,
		},
		.emlsr_allowed = true,
	},
	{
		.desc = "BT penalty(entry) with low rssi 8: emlsr not allowed",
		.input = {
			.notif.wifi_loss_low_rssi[1] = {7, 9},
			.notif.wifi_loss_mid_high_rssi[1] = {4, 5},
			.signal = -63,
			.check_entry = false,
		},
		.emlsr_allowed = true,
	},
	{
		.desc = "BT penalty(entry) from high rssi 9: emlsr not allowed",
		.input = {
			.notif.wifi_loss_low_rssi[1] = {4, 5},
			.notif.wifi_loss_mid_high_rssi[1] = {9, 9},
			.signal = -62,
			.check_entry = true,
		},
		.emlsr_allowed = false,
	},
};

KUNIT_ARRAY_PARAM_DESC(emlsr_with_bt, emlsr_with_bt_cases, desc);

static void test_emlsr_with_bt(struct kunit *test)
{
	struct iwl_mld *mld = test->priv;
	const struct emlsr_with_bt_test_case *test_param =
		(const void *)(test->param_value);
	struct ieee80211_vif *vif =
		iwlmld_kunit_add_vif(true, NL80211_IFTYPE_STATION);
	struct ieee80211_bss_conf *link = iwlmld_kunit_add_link(vif, 1);
	bool actual_value = false;

	KUNIT_ALLOC_AND_ASSERT(test, link->bss);

	/* Extract test case parameters */
	link->bss->signal = DBM_TO_MBM(test_param->input.signal);
	memcpy(&mld->last_bt_notif, &test_param->input.notif,
	       sizeof(struct iwl_bt_coex_profile_notif));

	actual_value = iwl_mld_bt_allows_emlsr(mld, link,
					       test_param->input.check_entry);
	/* Assert that the returned value matches the expected emlsr_allowed */
	KUNIT_EXPECT_EQ(test, actual_value, test_param->emlsr_allowed);
}

static struct kunit_case emlsr_with_bt_test_cases[] = {
	KUNIT_CASE_PARAM(test_emlsr_with_bt, emlsr_with_bt_gen_params),
	{},
};

static struct kunit_suite emlsr_with_bt = {
	.name = "iwlmld-emlsr-with-bt-tests",
	.test_cases = emlsr_with_bt_test_cases,
	.init = iwlmld_kunit_test_init,
};

kunit_test_suite(emlsr_with_bt);
