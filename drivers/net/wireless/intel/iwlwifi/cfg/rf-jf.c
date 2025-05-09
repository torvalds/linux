// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2015-2017 Intel Deutschland GmbH
 * Copyright (C) 2018-2021, 2023, 2025 Intel Corporation
 */
#include "iwl-config.h"

/* NVM versions */
#define IWL_JF_NVM_VERSION		0x0a1d

/* Memory offsets and lengths */
#define IWL9000_DCCM_OFFSET		0x800000
#define IWL9000_DCCM_LEN		0x18000
#define IWL9000_DCCM2_OFFSET		0x880000
#define IWL9000_DCCM2_LEN		0x8000

static const struct iwl_tt_params iwl_jf_tt_params = {
	.ct_kill_entry = 115,
	.ct_kill_exit = 93,
	.ct_kill_duration = 5,
	.dynamic_smps_entry = 111,
	.dynamic_smps_exit = 107,
	.tx_protection_entry = 112,
	.tx_protection_exit = 105,
	.tx_backoff = {
		{.temperature = 110, .backoff = 200},
		{.temperature = 111, .backoff = 600},
		{.temperature = 112, .backoff = 1200},
		{.temperature = 113, .backoff = 2000},
		{.temperature = 114, .backoff = 4000},
	},
	.support_ct_kill = true,
	.support_dynamic_smps = true,
	.support_tx_protection = true,
	.support_tx_backoff = true,
};

/* these values are ignored if not with Pu/Th MAC firmware, due to offload */
#define IWL_DEVICE_JF_PU						\
	.dccm_offset = IWL9000_DCCM_OFFSET,				\
	.dccm_len = IWL9000_DCCM_LEN,					\
	.dccm2_offset = IWL9000_DCCM2_OFFSET,				\
	.dccm2_len = IWL9000_DCCM2_LEN,					\
	.thermal_params = &iwl_jf_tt_params

#define IWL_DEVICE_JF							\
	IWL_DEVICE_JF_PU,						\
	.led_mode = IWL_LED_RF_STATE,					\
	.non_shared_ant = ANT_B,					\
	.num_rbds = IWL_NUM_RBDS_NON_HE,				\
	.vht_mu_mimo_supported = true,					\
	.ht_params = {							\
		.stbc = true,						\
		.ldpc = true,						\
		.ht40_bands = BIT(NL80211_BAND_2GHZ) |			\
			      BIT(NL80211_BAND_5GHZ),			\
	},								\
	.nvm_ver = IWL_JF_NVM_VERSION,					\
	.nvm_type = IWL_NVM_EXT

const struct iwl_cfg iwl_rf_jf = {
	IWL_DEVICE_JF,
};

const struct iwl_cfg iwl_rf_jf_80mhz = {
	IWL_DEVICE_JF,
	.bw_limit = 80,
};
