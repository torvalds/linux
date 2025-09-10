// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2015-2017 Intel Deutschland GmbH
 * Copyright (C) 2018-2025 Intel Corporation
 */
#include "iwl-config.h"

/* Highest firmware API version supported */
#define IWL_GF_UCODE_API_MAX	100

/* Lowest firmware API version supported */
#define IWL_GF_UCODE_API_MIN	98

#define IWL_SO_A_GF_A_FW_PRE		"iwlwifi-so-a0-gf-a0"
#define IWL_TY_A_GF_A_FW_PRE		"iwlwifi-ty-a0-gf-a0"
#define IWL_MA_A_GF_A_FW_PRE		"iwlwifi-ma-a0-gf-a0"
#define IWL_MA_B_GF_A_FW_PRE		"iwlwifi-ma-b0-gf-a0"
#define IWL_SO_A_GF4_A_FW_PRE		"iwlwifi-so-a0-gf4-a0"
#define IWL_MA_A_GF4_A_FW_PRE		"iwlwifi-ma-a0-gf4-a0"
#define IWL_MA_B_GF4_A_FW_PRE		"iwlwifi-ma-b0-gf4-a0"
#define IWL_BZ_A_GF_A_FW_PRE		"iwlwifi-bz-a0-gf-a0"
#define IWL_BZ_A_GF4_A_FW_PRE		"iwlwifi-bz-a0-gf4-a0"
#define IWL_SC_A_GF_A_FW_PRE		"iwlwifi-sc-a0-gf-a0"
#define IWL_SC_A_GF4_A_FW_PRE		"iwlwifi-sc-a0-gf4-a0"

/* NVM versions */
#define IWL_GF_NVM_VERSION		0x0a1d

const struct iwl_rf_cfg iwl_rf_gf = {
	.uhb_supported = true,
	.led_mode = IWL_LED_RF_STATE,
	.non_shared_ant = ANT_B,
	.vht_mu_mimo_supported = true,
	.ht_params = {
		.stbc = true,
		.ldpc = true,
		.ht40_bands = BIT(NL80211_BAND_2GHZ) |
			      BIT(NL80211_BAND_5GHZ),
	},
	.nvm_ver = IWL_GF_NVM_VERSION,
	.nvm_type = IWL_NVM_EXT,
	.num_rbds = IWL_NUM_RBDS_HE,
	.ucode_api_min = IWL_GF_UCODE_API_MIN,
	.ucode_api_max = IWL_GF_UCODE_API_MAX,
};

const char iwl_ax210_killer_1675w_name[] =
	"Killer(R) Wi-Fi 6E AX1675w 160MHz Wireless Network Adapter (210D2W)";
const char iwl_ax210_killer_1675x_name[] =
	"Killer(R) Wi-Fi 6E AX1675x 160MHz Wireless Network Adapter (210NGW)";
const char iwl_ax211_killer_1675s_name[] =
	"Killer(R) Wi-Fi 6E AX1675s 160MHz Wireless Network Adapter (211D2W)";
const char iwl_ax211_killer_1675i_name[] =
	"Killer(R) Wi-Fi 6E AX1675i 160MHz Wireless Network Adapter (211NGW)";
const char iwl_ax411_killer_1690s_name[] =
	"Killer(R) Wi-Fi 6E AX1690s 160MHz Wireless Network Adapter (411D2W)";
const char iwl_ax411_killer_1690i_name[] =
	"Killer(R) Wi-Fi 6E AX1690i 160MHz Wireless Network Adapter (411NGW)";

const char iwl_ax210_name[] = "Intel(R) Wi-Fi 6E AX210 160MHz";
const char iwl_ax211_name[] = "Intel(R) Wi-Fi 6E AX211 160MHz";
const char iwl_ax411_name[] = "Intel(R) Wi-Fi 6E AX411 160MHz";

IWL_FW_AND_PNVM(IWL_SO_A_GF_A_FW_PRE, IWL_GF_UCODE_API_MAX);
IWL_FW_AND_PNVM(IWL_TY_A_GF_A_FW_PRE, IWL_GF_UCODE_API_MAX);
IWL_FW_AND_PNVM(IWL_MA_A_GF_A_FW_PRE, IWL_GF_UCODE_API_MAX);
IWL_FW_AND_PNVM(IWL_MA_B_GF_A_FW_PRE, IWL_GF_UCODE_API_MAX);
IWL_FW_AND_PNVM(IWL_MA_A_GF4_A_FW_PRE, IWL_GF_UCODE_API_MAX);
IWL_FW_AND_PNVM(IWL_MA_B_GF4_A_FW_PRE, IWL_GF_UCODE_API_MAX);
IWL_FW_AND_PNVM(IWL_BZ_A_GF_A_FW_PRE, IWL_GF_UCODE_API_MAX);
IWL_FW_AND_PNVM(IWL_BZ_A_GF4_A_FW_PRE, IWL_GF_UCODE_API_MAX);
IWL_FW_AND_PNVM(IWL_SC_A_GF_A_FW_PRE, IWL_GF_UCODE_API_MAX);
IWL_FW_AND_PNVM(IWL_SC_A_GF4_A_FW_PRE, IWL_GF_UCODE_API_MAX);
