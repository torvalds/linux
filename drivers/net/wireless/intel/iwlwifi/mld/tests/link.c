// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * KUnit tests for channel helper functions
 *
 * Copyright (C) 2024-2025 Intel Corporation
 */
#include <kunit/static_stub.h>

#include "utils.h"
#include "mld.h"
#include "link.h"
#include "iface.h"
#include "fw/api/mac-cfg.h"

static const struct missed_beacon_test_case {
	const char *desc;
	struct {
		struct iwl_missed_beacons_notif notif;
		bool emlsr;
	} input;
	struct {
		bool disconnected;
		bool emlsr;
	} output;
} missed_beacon_cases[] = {
	{
		.desc = "no EMLSR, no disconnect",
		.input.notif = {
			.consec_missed_beacons = cpu_to_le32(4),
		},
	},
	{
		.desc = "no EMLSR, no beacon loss since Rx, no disconnect",
		.input.notif = {
			.consec_missed_beacons = cpu_to_le32(20),
		},
	},
	{
		.desc = "no EMLSR, beacon loss since Rx, disconnect",
		.input.notif = {
			.consec_missed_beacons = cpu_to_le32(20),
			.consec_missed_beacons_since_last_rx =
				cpu_to_le32(10),
		},
		.output.disconnected = true,
	},
};

KUNIT_ARRAY_PARAM_DESC(test_missed_beacon, missed_beacon_cases, desc);

static void fake_ieee80211_connection_loss(struct ieee80211_vif *vif)
{
	vif->cfg.assoc = false;
}

static void test_missed_beacon(struct kunit *test)
{
	struct iwl_mld *mld = test->priv;
	struct iwl_missed_beacons_notif *notif;
	const struct missed_beacon_test_case *test_param =
		(const void *)(test->param_value);
	struct ieee80211_vif *vif;
	struct iwl_rx_packet *pkt;
	struct iwl_mld_kunit_link link1 = {
		.id = 0,
		.band = NL80211_BAND_6GHZ,
	};
	struct iwl_mld_kunit_link link2 = {
		.id = 1,
		.band = NL80211_BAND_5GHZ,
	};

	kunit_activate_static_stub(test, ieee80211_connection_loss,
				   fake_ieee80211_connection_loss);
	pkt = iwl_mld_kunit_create_pkt(test_param->input.notif);
	notif = (void *)pkt->data;

	if (test_param->input.emlsr) {
		vif = iwlmld_kunit_assoc_emlsr(&link1, &link2);
	} else {
		struct iwl_mld_vif *mld_vif;

		vif = iwlmld_kunit_setup_non_mlo_assoc(&link1);
		mld_vif = iwl_mld_vif_from_mac80211(vif);
		notif->link_id = cpu_to_le32(mld_vif->deflink.fw_id);
	}

	wiphy_lock(mld->wiphy);

	iwl_mld_handle_missed_beacon_notif(mld, pkt);

	wiphy_unlock(mld->wiphy);

	KUNIT_ASSERT_NE(test, vif->cfg.assoc, test_param->output.disconnected);

	/* TODO: add test cases for esr and check */
}

static struct kunit_case link_cases[] = {
	KUNIT_CASE_PARAM(test_missed_beacon, test_missed_beacon_gen_params),
	{},
};

static struct kunit_suite link = {
	.name = "iwlmld-link",
	.test_cases = link_cases,
	.init = iwlmld_kunit_test_init,
};

kunit_test_suite(link);
