// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2015-2017 Intel Deutschland GmbH
 * Copyright (C) 2018-2025 Intel Corporation
 */
#include "iwl-config.h"

/* NVM versions */
#define IWL_FM_NVM_VERSION		0x0a1d

#define IWL_DEVICE_FM							\
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
	.nvm_ver = IWL_FM_NVM_VERSION,					\
	.nvm_type = IWL_NVM_EXT

const struct iwl_rf_cfg iwl_rf_fm = {
	IWL_DEVICE_FM,
};

const struct iwl_rf_cfg iwl_rf_fm_160mhz = {
	IWL_DEVICE_FM,
	.bw_limit = 160,
};

const char iwl_killer_be1750s_name[] =
	"Killer(R) Wi-Fi 7 BE1750s 320MHz Wireless Network Adapter (BE201D2W)";
const char iwl_killer_be1750i_name[] =
	"Killer(R) Wi-Fi 7 BE1750i 320MHz Wireless Network Adapter (BE201NGW)";
const char iwl_killer_be1750w_name[] =
	"Killer(TM) Wi-Fi 7 BE1750w 320MHz Wireless Network Adapter (BE200D2W)";
const char iwl_killer_be1750x_name[] =
	"Killer(TM) Wi-Fi 7 BE1750x 320MHz Wireless Network Adapter (BE200NGW)";
const char iwl_killer_be1790s_name[] =
	"Killer(R) Wi-Fi 7 BE1790s 320MHz Wireless Network Adapter (BE401D2W)";
const char iwl_killer_be1790i_name[] =
	"Killer(R) Wi-Fi 7 BE1790i 320MHz Wireless Network Adapter (BE401NGW)";

const char iwl_be201_name[] = "Intel(R) Wi-Fi 7 BE201 320MHz";
const char iwl_be200_name[] = "Intel(R) Wi-Fi 7 BE200 320MHz";
const char iwl_be202_name[] = "Intel(R) Wi-Fi 7 BE202 160MHz";
const char iwl_be401_name[] = "Intel(R) Wi-Fi 7 BE401 320MHz";
