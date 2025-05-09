// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2015-2017 Intel Deutschland GmbH
 * Copyright (C) 2018-2025 Intel Corporation
 */
#include "iwl-config.h"

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
};
