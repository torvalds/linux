// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2025-2026 Intel Corporation
 */
#include "iwl-config.h"

#define IWL_DEVICE_PE							\
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
	.eht_supported = true,						\
	.uhr_supported = true,						\
	.num_rbds = IWL_NUM_RBDS_EHT,					\
	.nvm_type = IWL_NVM_EXT

const struct iwl_rf_cfg iwl_rf_pe = {
	IWL_DEVICE_PE,
};

const char iwl_killer_bn1850w2_name[] =
	"Killer(R) Wi-Fi 8 BN1850w2 320MHz Wireless Network Adapter (BN201.D2W)";
const char iwl_killer_bn1850i_name[] =
	"Killer(R) Wi-Fi 8 BN1850i 320MHz Wireless Network Adapter (BN201.NGW)";

const char iwl_bn201_name[] = "Intel(R) Wi-Fi 8 BN201";
const char iwl_bn203_name[] = "Intel(R) Wi-Fi 8 BN203";
const char iwl_be223_name[] = "Intel(R) Wi-Fi 7 BE223";
