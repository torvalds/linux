// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2015-2017 Intel Deutschland GmbH
 * Copyright (C) 2018-2026 Intel Corporation
 */
#include "iwl-config.h"

/* Highest firmware API version supported */
#define IWL_HR_UCODE_API_MAX	100

/* Lowest firmware API version supported */
#define IWL_HR_UCODE_API_MIN	100

#define IWL_BZ_A_HR_B_FW_PRE		"iwlwifi-bz-a0-hr-b0"
#define IWL_SC_A_HR_A_FW_PRE		"iwlwifi-sc-a0-hr-b0"
#define IWL_SC_A_HR_B_FW_PRE		"iwlwifi-sc-a0-hr-b0"

#define IWL_BZ_A_HR_B_MODULE_FIRMWARE(api)	\
	IWL_BZ_A_HR_B_FW_PRE "-" __stringify(api) ".ucode"
#define IWL_SC_A_HR_A_FW_MODULE_FIRMWARE(api)	\
	IWL_SC_A_HR_A_FW_PRE "-" __stringify(api) ".ucode"
#define IWL_SC_A_HR_B_FW_MODULE_FIRMWARE(api)	\
	IWL_SC_A_HR_B_FW_PRE "-" __stringify(api) ".ucode"

#define IWL_DEVICE_HR							\
	.led_mode = IWL_LED_RF_STATE,					\
	.non_shared_ant = ANT_B,					\
	.vht_mu_mimo_supported = true,					\
	.ht_params = {							\
		.stbc = true,						\
		.ldpc = true,						\
		.ht40_bands = BIT(NL80211_BAND_2GHZ) |			\
			      BIT(NL80211_BAND_5GHZ),			\
	},								\
	.num_rbds = IWL_NUM_RBDS_HE,					\
	.nvm_type = IWL_NVM_EXT,					\
	.ucode_api_min = IWL_HR_UCODE_API_MIN,				\
	.ucode_api_max = IWL_HR_UCODE_API_MAX

const struct iwl_rf_cfg iwl_rf_hr1 = {
	IWL_DEVICE_HR,
	.tx_with_siso_diversity = true,
};

const struct iwl_rf_cfg iwl_rf_hr = {
	IWL_DEVICE_HR,
};

const struct iwl_rf_cfg iwl_rf_hr_80mhz = {
	IWL_DEVICE_HR,
	.bw_limit = 80,
};

const char iwl_ax101_name[] = "Intel(R) Wi-Fi 6 AX101";
const char iwl_ax200_name[] = "Intel(R) Wi-Fi 6 AX200 160MHz";
const char iwl_ax201_name[] = "Intel(R) Wi-Fi 6 AX201 160MHz";
const char iwl_ax203_name[] = "Intel(R) Wi-Fi 6 AX203";

MODULE_FIRMWARE(IWL_BZ_A_HR_B_MODULE_FIRMWARE(IWL_HR_UCODE_API_MAX));
MODULE_FIRMWARE(IWL_SC_A_HR_A_FW_MODULE_FIRMWARE(IWL_HR_UCODE_API_MAX));
MODULE_FIRMWARE(IWL_SC_A_HR_B_FW_MODULE_FIRMWARE(IWL_HR_UCODE_API_MAX));
