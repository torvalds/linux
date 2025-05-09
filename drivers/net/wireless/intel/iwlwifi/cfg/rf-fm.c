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
