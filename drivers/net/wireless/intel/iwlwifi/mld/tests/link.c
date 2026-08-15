// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * KUnit tests for link helper functions
 *
 * Copyright (C) 2024-2026 Intel Corporation
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
		.chandef = &chandef_6ghz_160mhz,
	};
	struct iwl_mld_kunit_link link2 = {
		.id = 1,
		.chandef = &chandef_5ghz_80mhz,
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

struct dup_beacon_test_case {
	const char *desc;
	enum nl80211_chan_width bandwidth;
	bool has_he_oper;
	bool dup_beacon_bit;
	s8 expected_adj;
};

static const struct dup_beacon_test_case dup_beacon_cases[] = {
	{
		.desc = "20 MHz - no duplication",
		.bandwidth = NL80211_CHAN_WIDTH_20,
		.has_he_oper = true,
		.dup_beacon_bit = true,
		.expected_adj = 0,
	},
	{
		.desc = "40 MHz with duplication - 3 dB",
		.bandwidth = NL80211_CHAN_WIDTH_40,
		.has_he_oper = true,
		.dup_beacon_bit = true,
		.expected_adj = 3,
	},
	{
		.desc = "80 MHz with duplication - 6 dB",
		.bandwidth = NL80211_CHAN_WIDTH_80,
		.has_he_oper = true,
		.dup_beacon_bit = true,
		.expected_adj = 6,
	},
	{
		.desc = "160 MHz with duplication - 9 dB",
		.bandwidth = NL80211_CHAN_WIDTH_160,
		.has_he_oper = true,
		.dup_beacon_bit = true,
		.expected_adj = 9,
	},
	{
		.desc = "320 MHz with duplication - 12 dB",
		.bandwidth = NL80211_CHAN_WIDTH_320,
		.has_he_oper = true,
		.dup_beacon_bit = true,
		.expected_adj = 12,
	},
	{
		.desc = "80 MHz without dup bit - no adjustment",
		.bandwidth = NL80211_CHAN_WIDTH_80,
		.has_he_oper = true,
		.dup_beacon_bit = false,
		.expected_adj = 0,
	},
	{
		.desc = "80 MHz without HE oper - no adjustment",
		.bandwidth = NL80211_CHAN_WIDTH_80,
		.has_he_oper = false,
		.dup_beacon_bit = true,
		.expected_adj = 0,
	},
};

KUNIT_ARRAY_PARAM_DESC(test_dup_beacon_rssi_adjust, dup_beacon_cases, desc);

struct psd_eirp_test_case {
	const char *desc;
	const struct cfg80211_chan_def *chandef;
	enum ieee80211_ap_reg_power power_type;
	struct {
		s8 psd_20;
		s8 psd_oper;
		s8 eirp_20;
		s8 eirp_oper;
	} local, reg;
	s8 expected_adj;
	struct {
		bool no_psd_data;
		bool no_eirp_data;
		bool no_reg_psd_data;
		bool has_partial_psd;
		u8 psd_partial_count;
		bool non_uniform_psd;
		bool has_unusable_channels;
	} flags;
};

static const struct psd_eirp_test_case psd_eirp_cases[] = {
	{
		.desc = "20 MHz VLP baseline - no boost expected",
		.chandef = &chandef_6ghz_20mhz,
		.power_type = IEEE80211_REG_VLP_AP,
		.local = {
			.psd_20 = 20, .psd_oper = 20,
			.eirp_20 = 40, .eirp_oper = 40,
		},
		.reg = {
			.psd_20 = 20, .psd_oper = 20,
			.eirp_20 = 40, .eirp_oper = 40,
		},
		.expected_adj = 0,
	},
	{
		.desc = "40 MHz VLP - power limit prevents boost",
		.chandef = &chandef_6ghz_40mhz,
		.power_type = IEEE80211_REG_VLP_AP,
		.local = {
			.psd_20 = 20, .psd_oper = 20,
			.eirp_20 = 40, .eirp_oper = 46,
		},
		.reg = {
			.psd_20 = 20, .psd_oper = 20,
			.eirp_20 = 40, .eirp_oper = 46,
		},
		.expected_adj = 0,
	},
	{
		.desc = "80 MHz LPI - power limit caps the boost",
		.chandef = &chandef_6ghz_80mhz,
		.power_type = IEEE80211_REG_LPI_AP,
		.local = {
			.psd_20 = 20, .psd_oper = 20,
			.eirp_20 = 40, .eirp_oper = 52,
		},
		.reg = {
			.psd_20 = 20, .psd_oper = 20,
			.eirp_20 = 40, .eirp_oper = 52,
		},
		.expected_adj = 3,
	},
	{
		.desc = "160 MHz LPI - power limit caps the boost",
		.chandef = &chandef_6ghz_160mhz,
		.power_type = IEEE80211_REG_LPI_AP,
		.local = {
			.psd_20 = 20, .psd_oper = 20,
			.eirp_20 = 40, .eirp_oper = 58,
		},
		.reg = {
			.psd_20 = 20, .psd_oper = 20,
			.eirp_20 = 40, .eirp_oper = 58,
		},
		.expected_adj = 3,
	},
	{
		.desc = "320 MHz SP - power limit caps the boost",
		.chandef = &chandef_6ghz_320mhz_pri0,
		.power_type = IEEE80211_REG_SP_AP,
		.local = {
			.psd_20 = 20, .psd_oper = 20,
			.eirp_20 = 40, .eirp_oper = 63,
		},
		.reg = {
			.psd_20 = 20, .psd_oper = 20,
			.eirp_20 = 40, .eirp_oper = 63,
		},
		.expected_adj = 3,
	},
	{
		.desc = "80 MHz - EIRP prevents boost",
		.chandef = &chandef_6ghz_80mhz,
		.power_type = IEEE80211_REG_LPI_AP,
		.local = {
			.psd_20 = 20, .psd_oper = 20,
			.eirp_20 = 40, .eirp_oper = 20,
		},
		.reg = {
			.psd_20 = 20, .psd_oper = 20,
			.eirp_20 = 40, .eirp_oper = 20,
		},
		.expected_adj = 0,
	},
	{
		.desc = "40 MHz - regulatory TPE sets lower limits",
		.chandef = &chandef_6ghz_40mhz,
		.power_type = IEEE80211_REG_LPI_AP,
		.local = {
			.psd_20 = 30, .psd_oper = 30,
			.eirp_20 = 50, .eirp_oper = 56,
		},
		.reg = {
			.psd_20 = 20, .psd_oper = 20,
			.eirp_20 = 40, .eirp_oper = 46,
		},
		.expected_adj = 3,
	},
	{
		.desc = "80 MHz - PSD missing, use EIRP only",
		.chandef = &chandef_6ghz_80mhz,
		.power_type = IEEE80211_REG_LPI_AP,
		.local = {
			.psd_20 = S8_MAX, .psd_oper = S8_MAX,
			.eirp_20 = 40, .eirp_oper = 52,
		},
		.reg = {
			.psd_20 = S8_MAX, .psd_oper = S8_MAX,
			.eirp_20 = 40, .eirp_oper = 52,
		},
		.expected_adj = 0,
		.flags.no_psd_data = true,
	},
	{
		.desc = "80 MHz - single PSD source available",
		.chandef = &chandef_6ghz_80mhz,
		.power_type = IEEE80211_REG_LPI_AP,
		.local = {
			.psd_20 = 20, .psd_oper = 20,
			.eirp_20 = 40, .eirp_oper = 52,
		},
		.reg = {
			.psd_20 = 20, .psd_oper = 20,
			.eirp_20 = 40, .eirp_oper = 52,
		},
		.expected_adj = 3,
		.flags.no_reg_psd_data = true,
	},
	{
		.desc = "80 MHz - partial PSD data present",
		.chandef = &chandef_6ghz_80mhz,
		.power_type = IEEE80211_REG_LPI_AP,
		.local = {
			.psd_20 = 24, .psd_oper = 24,
			.eirp_20 = 40, .eirp_oper = 56,
		},
		.reg = {
			.psd_20 = 24, .psd_oper = 24,
			.eirp_20 = 40, .eirp_oper = 56,
		},
		.expected_adj = 0,
		.flags.has_partial_psd = true,
		.flags.psd_partial_count = 2,
	},
	{
		.desc = "160 MHz - different PSD per sub-channel",
		.chandef = &chandef_6ghz_160mhz,
		.power_type = IEEE80211_REG_LPI_AP,
		.local = {
			.psd_20 = 8, .psd_oper = 20,
			.eirp_20 = 40, .eirp_oper = 58,
		},
		.reg = {
			.psd_20 = 8, .psd_oper = 20,
			.eirp_20 = 40, .eirp_oper = 58,
		},
		.expected_adj = 11,
		.flags.non_uniform_psd = true,
	},
	{
		.desc = "80 MHz - EIRP missing, use PSD only",
		.chandef = &chandef_6ghz_80mhz,
		.power_type = IEEE80211_REG_LPI_AP,
		.local = {
			.psd_20 = 20, .psd_oper = 20,
			.eirp_20 = S8_MAX, .eirp_oper = S8_MAX,
		},
		.reg = {
			.psd_20 = 20, .psd_oper = 20,
			.eirp_20 = S8_MAX, .eirp_oper = S8_MAX,
		},
		.expected_adj = 3,
		.flags.no_eirp_data = true,
	},
	{
		.desc = "80 MHz - skip unusable channels in average",
		.chandef = &chandef_6ghz_80mhz,
		.power_type = IEEE80211_REG_LPI_AP,
		.local = {
			.psd_20 = 20, .psd_oper = 20,
			.eirp_20 = 40, .eirp_oper = 52,
		},
		.reg = {
			.psd_20 = 20, .psd_oper = 20,
			.eirp_20 = 40, .eirp_oper = 52,
		},
		.expected_adj = 3,
		.flags.has_unusable_channels = true,
	},
	{
		.desc = "40 MHz - no negative adjustment",
		.chandef = &chandef_6ghz_40mhz,
		.power_type = IEEE80211_REG_LPI_AP,
		.local = {
			.psd_20 = 20, .psd_oper = 20,
			.eirp_20 = 40, .eirp_oper = 18,
		},
		.reg = {
			.psd_20 = 20, .psd_oper = 20,
			.eirp_20 = 40, .eirp_oper = 18,
		},
		.expected_adj = 0,
	},
};

KUNIT_ARRAY_PARAM_DESC(test_psd_eirp_rssi_adjust, psd_eirp_cases, desc);

static void setup_psd(struct ieee80211_bss_conf *link_conf,
		      const struct psd_eirp_test_case *params,
		      int num_subchannels)
{
	int i;

	if (params->flags.no_psd_data) {
		link_conf->tpe.psd_local[0].valid = false;
		link_conf->tpe.psd_reg_client[0].valid = false;
		link_conf->tpe.psd_local[0].count = 0;
		link_conf->tpe.psd_reg_client[0].count = 0;
	} else if (params->flags.no_reg_psd_data) {
		link_conf->tpe.psd_local[0].valid = true;
		link_conf->tpe.psd_local[0].count = num_subchannels;
		link_conf->tpe.psd_reg_client[0].valid = false;
		link_conf->tpe.psd_reg_client[0].count = 0;
	} else if (params->flags.has_partial_psd) {
		link_conf->tpe.psd_local[0].valid = true;
		link_conf->tpe.psd_local[0].count =
			params->flags.psd_partial_count;
		link_conf->tpe.psd_reg_client[0].valid = true;
		link_conf->tpe.psd_reg_client[0].count =
			params->flags.psd_partial_count;
	} else {
		link_conf->tpe.psd_local[0].valid = true;
		link_conf->tpe.psd_local[0].count = num_subchannels;
		link_conf->tpe.psd_reg_client[0].valid = true;
		link_conf->tpe.psd_reg_client[0].count = num_subchannels;
	}

	/* TPE element stores PSD limit as value * 2 */
	if (params->flags.non_uniform_psd) {
		/* PSD varies per sub-channel: 10/12/10/8 dBm pattern */
		static const s8 psd_values[] = {20, 24, 20, 16, 20, 24, 20, 16,
						20, 24, 20, 16, 20, 24, 20};
		/* Set primary channel (index 0) explicitly */
		link_conf->tpe.psd_local[0].power[0] =
			params->local.psd_20 * 2;
		link_conf->tpe.psd_reg_client[0].power[0] =
			params->reg.psd_20 * 2;
		/* Set remaining subchannels with pattern */
		for (i = 1; i < num_subchannels; i++) {
			link_conf->tpe.psd_local[0].power[i] =
				psd_values[i - 1];
			link_conf->tpe.psd_reg_client[0].power[i] =
				psd_values[i - 1];
		}
	} else if (params->flags.no_psd_data) {
		for (i = 0; i < num_subchannels; i++) {
			link_conf->tpe.psd_local[0].power[i] = S8_MAX;
			link_conf->tpe.psd_reg_client[0].power[i] = S8_MAX;
		}
	} else if (params->flags.has_unusable_channels) {
		/* Alternate usable/unusable channels for S8_MIN test */
		/* Set primary channel (index 0) explicitly */
		link_conf->tpe.psd_local[0].power[0] =
			params->local.psd_20 * 2;
		link_conf->tpe.psd_reg_client[0].power[0] =
			params->reg.psd_20 * 2;
		/* Alternate usable/unusable for remaining subchannels */
		for (i = 1; i < num_subchannels; i++) {
			if (i % 2 == 0) {
				link_conf->tpe.psd_local[0].power[i] =
					params->local.psd_oper * 2;
				link_conf->tpe.psd_reg_client[0].power[i] =
					params->reg.psd_oper * 2;
			} else {
				link_conf->tpe.psd_local[0].power[i] = S8_MIN;
				link_conf->tpe.psd_reg_client[0].power[i] =
					S8_MIN;
			}
		}
	} else {
		/* Set primary channel (index 0) separately */
		link_conf->tpe.psd_local[0].power[0] =
			params->local.psd_20 * 2;
		link_conf->tpe.psd_reg_client[0].power[0] =
			params->reg.psd_20 * 2;
		/* Set remaining subchannels */
		for (i = 1; i < num_subchannels; i++) {
			link_conf->tpe.psd_local[0].power[i] =
				params->local.psd_oper * 2;
			link_conf->tpe.psd_reg_client[0].power[i] =
				params->reg.psd_oper * 2;
		}
	}
}

static void setup_eirp(struct ieee80211_bss_conf *link_conf,
		       const struct psd_eirp_test_case *params,
		       int num_subchannels)
{
	int i;
	int count = ilog2(num_subchannels) + 1;

	link_conf->tpe.max_local[0].valid = !params->flags.no_eirp_data;
	link_conf->tpe.max_reg_client[0].valid = !params->flags.no_eirp_data;

	if (params->flags.no_eirp_data) {
		link_conf->tpe.max_local[0].count = 0;
		link_conf->tpe.max_reg_client[0].count = 0;
		return;
	}

	link_conf->tpe.max_local[0].count = count;
	link_conf->tpe.max_reg_client[0].count = count;

	/* TPE element stores EIRP limit as value * 2 */
	link_conf->tpe.max_local[0].power[0] = params->local.eirp_20 * 2;
	link_conf->tpe.max_reg_client[0].power[0] = params->reg.eirp_20 * 2;
	for (i = 1; i < count; i++) {
		link_conf->tpe.max_local[0].power[i] =
			params->local.eirp_oper * 2;
		link_conf->tpe.max_reg_client[0].power[i] =
			params->reg.eirp_oper * 2;
	}
}

static void test_dup_beacon_rssi_adjust(struct kunit *test)
{
	const struct dup_beacon_test_case *params = test->param_value;
	struct iwl_mld *mld = test->priv;
	struct ieee80211_bss_conf *link_conf;
	struct cfg80211_bss *bss;
	struct element *he_elem = NULL;
	s8 result;

	KUNIT_ALLOC_AND_ASSERT(test, link_conf);
	KUNIT_ALLOC_AND_ASSERT(test, bss);
	link_conf->bss = bss;

	link_conf->chanreq.oper.chan = &chan_6ghz;
	link_conf->chanreq.oper.width = params->bandwidth;

	if (params->has_he_oper) {
		struct ieee80211_he_6ghz_oper he_6ghz = {};

		if (params->dup_beacon_bit)
			he_6ghz.control =
				IEEE80211_HE_6GHZ_OPER_CTRL_DUP_BEACON;
		he_elem = iwlmld_kunit_create_he_6ghz_oper(he_6ghz);
	}

	rcu_assign_pointer(bss->beacon_ies,
			   iwlmld_kunit_create_bss_ies(he_elem));

	guard(wiphy)(mld->wiphy);
	result = iwl_mld_get_dup_beacon_rssi_adjust(mld, link_conf);

	KUNIT_EXPECT_EQ(test, result, params->expected_adj);
}

static void test_psd_eirp_rssi_adjust(struct kunit *test)
{
	const struct psd_eirp_test_case *params = test->param_value;
	struct ieee80211_bss_conf *link_conf;
	int num_subchannels;
	s8 result;

	KUNIT_ALLOC_AND_ASSERT(test, link_conf);

	link_conf->power_type = params->power_type;
	link_conf->chanreq.oper = *params->chandef;
	num_subchannels =
		nl80211_chan_width_to_mhz(params->chandef->width) / 20;

	setup_psd(link_conf, params, num_subchannels);
	setup_eirp(link_conf, params, num_subchannels);

	result = iwl_mld_get_psd_eirp_rssi_adjust(link_conf);

	KUNIT_EXPECT_EQ(test, result, params->expected_adj);
}

static struct kunit_case link_cases[] = {
	KUNIT_CASE_PARAM(test_missed_beacon, test_missed_beacon_gen_params),
	KUNIT_CASE_PARAM(test_dup_beacon_rssi_adjust,
			 test_dup_beacon_rssi_adjust_gen_params),
	KUNIT_CASE_PARAM(test_psd_eirp_rssi_adjust,
			 test_psd_eirp_rssi_adjust_gen_params),
	{},
};

static struct kunit_suite link = {
	.name = "iwlmld-link",
	.test_cases = link_cases,
	.init = iwlmld_kunit_test_init,
};

kunit_test_suite(link);
