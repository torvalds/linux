// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2025 Intel Corporation
 */
#include "iwl-config.h"

/* NVM versions */
#define IWL_WH_NVM_VERSION		0x0a1d

#define IWL_DEVICE_WH							\
	.ht_params = {							\
		.stbc = true,						\
		.ldpc = true,						\
		.ht40_bands = BIT(NL80211_BAND_2GHZ) |			\
			      BIT(NL80211_BAND_5GHZ),			\
	},								\
	.led_mode = IWL_LED_RF_STATE,					\
	.non_shared_ant = ANT_B,					\
	.vht_mu_mimo_supported = true,					\
	.uhb_supported = true,						\
	.num_rbds = IWL_NUM_RBDS_EHT,					\
	.nvm_ver = IWL_WH_NVM_VERSION,					\
	.nvm_type = IWL_NVM_EXT

/* currently iwl_rf_wh/iwl_rf_wh_160mhz are just defines for the FM ones */

const struct iwl_rf_cfg iwl_rf_wh_non_eht = {
	IWL_DEVICE_WH,
	.eht_supported = false,
};

const char iwl_killer_be1775s_name[] =
	"Killer(R) Wi-Fi 7 BE1775s 320MHz Wireless Network Adapter (BE211D2W)";
const char iwl_killer_be1775i_name[] =
	"Killer(R) Wi-Fi 7 BE1775i 320MHz Wireless Network Adapter (BE211NGW)";

const char iwl_be211_name[] = "Intel(R) Wi-Fi 7 BE211 320MHz";
const char iwl_be213_name[] = "Intel(R) Wi-Fi 7 BE213 160MHz";
const char iwl_ax221_name[] = "Intel(R) Wi-Fi 6E AX221 160MHz";
