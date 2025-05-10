// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2014, 2018-2020, 2023, 2025 Intel Corporation
 * Copyright (C) 2014-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016 Intel Deutschland GmbH
 */
#include <linux/module.h>
#include <linux/stringify.h>
#include "iwl-config.h"

/* Highest firmware API version supported */
#define IWL8000_UCODE_API_MAX	36
#define IWL8265_UCODE_API_MAX	36

/* Lowest firmware API version supported */
#define IWL8000_UCODE_API_MIN	22
#define IWL8265_UCODE_API_MIN	22

/* NVM versions */
#define IWL8000_NVM_VERSION		0x0a1d

/* Memory offsets and lengths */
#define IWL8260_DCCM_OFFSET		0x800000
#define IWL8260_DCCM_LEN		0x18000
#define IWL8260_DCCM2_OFFSET		0x880000
#define IWL8260_DCCM2_LEN		0x8000
#define IWL8260_SMEM_OFFSET		0x400000
#define IWL8260_SMEM_LEN		0x68000

#define IWL8000_FW_PRE "iwlwifi-8000C"
#define IWL8000_MODULE_FIRMWARE(api) \
	IWL8000_FW_PRE "-" __stringify(api) ".ucode"

#define IWL8265_FW_PRE "iwlwifi-8265"
#define IWL8265_MODULE_FIRMWARE(api) \
	IWL8265_FW_PRE "-" __stringify(api) ".ucode"

static const struct iwl_family_base_params iwl8000_base = {
	.eeprom_size = OTP_LOW_IMAGE_SIZE_32K,
	.num_of_queues = 31,
	.max_tfd_queue_size = 256,
	.shadow_ram_support = true,
	.led_compensation = 57,
	.wd_timeout = IWL_LONG_WD_TIMEOUT,
	.max_event_log_size = 512,
	.shadow_reg_enable = true,
	.pcie_l1_allowed = true,
	.nvm_hw_section_num = 10,
	.features = NETIF_F_RXCSUM,
	.smem_offset = IWL8260_SMEM_OFFSET,
	.smem_len = IWL8260_SMEM_LEN,
	.apmg_not_supported = true,
	.min_umac_error_event_table = 0x800000,
};

static const struct iwl_tt_params iwl8000_tt_params = {
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

const struct iwl_mac_cfg iwl8000_mac_cfg = {
	.device_family = IWL_DEVICE_FAMILY_8000,
	.base = &iwl8000_base,
};

#define IWL_DEVICE_8000_COMMON						\
	.led_mode = IWL_LED_RF_STATE,					\
	.non_shared_ant = ANT_A,					\
	.dccm_offset = IWL8260_DCCM_OFFSET,				\
	.dccm_len = IWL8260_DCCM_LEN,					\
	.dccm2_offset = IWL8260_DCCM2_OFFSET,				\
	.dccm2_len = IWL8260_DCCM2_LEN,					\
	.thermal_params = &iwl8000_tt_params,				\
	.nvm_type = IWL_NVM_EXT

#define IWL_DEVICE_8260							\
	IWL_DEVICE_8000_COMMON,						\
	.ucode_api_max = IWL8000_UCODE_API_MAX,				\
	.ucode_api_min = IWL8000_UCODE_API_MIN				\

#define IWL_DEVICE_8265							\
	IWL_DEVICE_8000_COMMON,						\
	.ucode_api_max = IWL8265_UCODE_API_MAX,				\
	.ucode_api_min = IWL8265_UCODE_API_MIN				\

const char iwl8260_2n_name[] = "Intel(R) Dual Band Wireless-N 8260";
const char iwl8260_2ac_name[] = "Intel(R) Dual Band Wireless-AC 8260";
const char iwl8265_2ac_name[] = "Intel(R) Dual Band Wireless-AC 8265";
const char iwl8275_2ac_name[] = "Intel(R) Dual Band Wireless-AC 8275";
const char iwl4165_2ac_name[] = "Intel(R) Dual Band Wireless-AC 4165";

const char iwl_killer_1435i_name[] =
	"Killer(R) Wireless-AC 1435i Wireless Network Adapter (8265D2W)";
const char iwl_killer_1434_kix_name[] =
	"Killer(R) Wireless-AC 1435-KIX Wireless Network Adapter (8265NGW)";

const struct iwl_rf_cfg iwl8260_cfg = {
	.fw_name_pre = IWL8000_FW_PRE,
	IWL_DEVICE_8260,
	.ht_params = {
		.stbc = true,
		.ldpc = true,
		.ht40_bands = BIT(NL80211_BAND_2GHZ) | BIT(NL80211_BAND_5GHZ),
	},
	.nvm_ver = IWL8000_NVM_VERSION,
};

const struct iwl_rf_cfg iwl8265_cfg = {
	.fw_name_pre = IWL8265_FW_PRE,
	IWL_DEVICE_8265,
	.ht_params = {
		.stbc = true,
		.ldpc = true,
		.ht40_bands = BIT(NL80211_BAND_2GHZ) | BIT(NL80211_BAND_5GHZ),
	},
	.nvm_ver = IWL8000_NVM_VERSION,
	.vht_mu_mimo_supported = true,
};

MODULE_FIRMWARE(IWL8000_MODULE_FIRMWARE(IWL8000_UCODE_API_MAX));
MODULE_FIRMWARE(IWL8265_MODULE_FIRMWARE(IWL8265_UCODE_API_MAX));
