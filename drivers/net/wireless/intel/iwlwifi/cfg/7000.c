// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2012-2014, 2018-2020, 2023, 2025 Intel Corporation
 * Copyright (C) 2013-2014 Intel Mobile Communications GmbH
 * Copyright (C) 2015 Intel Deutschland GmbH
 */
#include <linux/module.h>
#include <linux/stringify.h>
#include "iwl-config.h"

/* Highest firmware API version supported */
#define IWL7260_UCODE_API_MAX	17
#define IWL7265_UCODE_API_MAX	17
#define IWL7265D_UCODE_API_MAX	29
#define IWL3168_UCODE_API_MAX	29

/* Lowest firmware API version supported */
#define IWL7260_UCODE_API_MIN	17
#define IWL7265_UCODE_API_MIN	17
#define IWL7265D_UCODE_API_MIN	22
#define IWL3168_UCODE_API_MIN	22

/* NVM versions */
#define IWL7260_NVM_VERSION		0x0a1d
#define IWL3160_NVM_VERSION		0x709
#define IWL3165_NVM_VERSION		0x709
#define IWL3168_NVM_VERSION		0xd01
#define IWL7265_NVM_VERSION		0x0a1d
#define IWL7265D_NVM_VERSION		0x0c11

/* DCCM offsets and lengths */
#define IWL7000_DCCM_OFFSET		0x800000
#define IWL7260_DCCM_LEN		0x14000
#define IWL3160_DCCM_LEN		0x10000
#define IWL7265_DCCM_LEN		0x17A00

#define IWL7260_FW_PRE "iwlwifi-7260"
#define IWL7260_MODULE_FIRMWARE(api) IWL7260_FW_PRE "-" __stringify(api) ".ucode"

#define IWL3160_FW_PRE "iwlwifi-3160"
#define IWL3160_MODULE_FIRMWARE(api) IWL3160_FW_PRE "-" __stringify(api) ".ucode"

#define IWL3168_FW_PRE "iwlwifi-3168"
#define IWL3168_MODULE_FIRMWARE(api) IWL3168_FW_PRE "-" __stringify(api) ".ucode"

#define IWL7265_FW_PRE "iwlwifi-7265"
#define IWL7265_MODULE_FIRMWARE(api) IWL7265_FW_PRE "-" __stringify(api) ".ucode"

#define IWL7265D_FW_PRE "iwlwifi-7265D"
#define IWL7265D_MODULE_FIRMWARE(api) IWL7265D_FW_PRE "-" __stringify(api) ".ucode"

static const struct iwl_base_params iwl7000_base_params = {
	.eeprom_size = OTP_LOW_IMAGE_SIZE_16K,
	.num_of_queues = 31,
	.max_tfd_queue_size = 256,
	.shadow_ram_support = true,
	.led_compensation = 57,
	.wd_timeout = IWL_LONG_WD_TIMEOUT,
	.max_event_log_size = 512,
	.shadow_reg_enable = true,
	.pcie_l1_allowed = true,
	.apmg_wake_up_wa = true,
};

static const struct iwl_tt_params iwl7000_high_temp_tt_params = {
	.ct_kill_entry = 118,
	.ct_kill_exit = 96,
	.ct_kill_duration = 5,
	.dynamic_smps_entry = 114,
	.dynamic_smps_exit = 110,
	.tx_protection_entry = 114,
	.tx_protection_exit = 108,
	.tx_backoff = {
		{.temperature = 112, .backoff = 300},
		{.temperature = 113, .backoff = 800},
		{.temperature = 114, .backoff = 1500},
		{.temperature = 115, .backoff = 3000},
		{.temperature = 116, .backoff = 5000},
		{.temperature = 117, .backoff = 10000},
	},
	.support_ct_kill = true,
	.support_dynamic_smps = true,
	.support_tx_protection = true,
	.support_tx_backoff = true,
};

const struct iwl_mac_cfg iwl7000_mac_cfg = {
	.device_family = IWL_DEVICE_FAMILY_7000,
	.base_params = &iwl7000_base_params,
};

#define IWL_DEVICE_7000_COMMON					\
	.led_mode = IWL_LED_RF_STATE,				\
	.nvm_hw_section_num = 0,				\
	.non_shared_ant = ANT_A,				\
	.dccm_offset = IWL7000_DCCM_OFFSET

#define IWL_DEVICE_7000						\
	IWL_DEVICE_7000_COMMON,					\
	.ucode_api_max = IWL7260_UCODE_API_MAX,			\
	.ucode_api_min = IWL7260_UCODE_API_MIN

#define IWL_DEVICE_7005						\
	IWL_DEVICE_7000_COMMON,					\
	.ucode_api_max = IWL7265_UCODE_API_MAX,			\
	.ucode_api_min = IWL7265_UCODE_API_MIN

#define IWL_DEVICE_3008						\
	IWL_DEVICE_7000_COMMON,					\
	.ucode_api_max = IWL3168_UCODE_API_MAX,			\
	.ucode_api_min = IWL3168_UCODE_API_MIN

#define IWL_DEVICE_7005D					\
	IWL_DEVICE_7000_COMMON,					\
	.ucode_api_max = IWL7265D_UCODE_API_MAX,		\
	.ucode_api_min = IWL7265D_UCODE_API_MIN

const char iwl7260_2ac_name[] = "Intel(R) Dual Band Wireless AC 7260";
const char iwl7260_2n_name[] = "Intel(R) Dual Band Wireless N 7260";
const char iwl7260_n_name[] = "Intel(R) Wireless N 7260";
const char iwl3160_2ac_name[] = "Intel(R) Dual Band Wireless AC 3160";
const char iwl3160_2n_name[] = "Intel(R) Dual Band Wireless N 3160";
const char iwl3160_n_name[] = "Intel(R) Wireless N 3160";
const char iwl3165_2ac_name[] = "Intel(R) Dual Band Wireless AC 3165";
const char iwl3168_2ac_name[] = "Intel(R) Dual Band Wireless AC 3168";
const char iwl7265_2ac_name[] = "Intel(R) Dual Band Wireless AC 7265";
const char iwl7265_2n_name[] = "Intel(R) Dual Band Wireless N 7265";
const char iwl7265_n_name[] = "Intel(R) Wireless N 7265";
const char iwl7265d_2ac_name[] = "Intel(R) Dual Band Wireless AC 7265";
const char iwl7265d_2n_name[] = "Intel(R) Dual Band Wireless N 7265";
const char iwl7265d_n_name[] = "Intel(R) Wireless N 7265";

const struct iwl_cfg iwl7260_cfg = {
	.fw_name_pre = IWL7260_FW_PRE,
	IWL_DEVICE_7000,
	.ht_params = {
		.stbc = true,
		.ht40_bands = BIT(NL80211_BAND_2GHZ) | BIT(NL80211_BAND_5GHZ),
	},
	.nvm_ver = IWL7260_NVM_VERSION,
	.host_interrupt_operation_mode = true,
	.lp_xtal_workaround = true,
	.dccm_len = IWL7260_DCCM_LEN,
};

const struct iwl_cfg iwl7260_high_temp_cfg = {
	.fw_name_pre = IWL7260_FW_PRE,
	IWL_DEVICE_7000,
	.ht_params = {
		.stbc = true,
		.ht40_bands = BIT(NL80211_BAND_2GHZ) | BIT(NL80211_BAND_5GHZ),
	},
	.nvm_ver = IWL7260_NVM_VERSION,
	.high_temp = true,
	.host_interrupt_operation_mode = true,
	.lp_xtal_workaround = true,
	.dccm_len = IWL7260_DCCM_LEN,
	.thermal_params = &iwl7000_high_temp_tt_params,
};

const struct iwl_cfg iwl3160_cfg = {
	.fw_name_pre = IWL3160_FW_PRE,
	IWL_DEVICE_7000,
	.ht_params = {
		.stbc = true,
		.ht40_bands = BIT(NL80211_BAND_2GHZ) | BIT(NL80211_BAND_5GHZ),
	},
	.nvm_ver = IWL3160_NVM_VERSION,
	.host_interrupt_operation_mode = true,
	.dccm_len = IWL3160_DCCM_LEN,
};

static const struct iwl_pwr_tx_backoff iwl7265_pwr_tx_backoffs[] = {
	{.pwr = 1600, .backoff = 0},
	{.pwr = 1300, .backoff = 467},
	{.pwr = 900,  .backoff = 1900},
	{.pwr = 800, .backoff = 2630},
	{.pwr = 700, .backoff = 3720},
	{.pwr = 600, .backoff = 5550},
	{.pwr = 500, .backoff = 9350},
	{0},
};

const struct iwl_cfg iwl3165_2ac_cfg = {
	.fw_name_pre = IWL7265D_FW_PRE,
	IWL_DEVICE_7005D,
	.ht_params = {
		.stbc = true,
		.ht40_bands = BIT(NL80211_BAND_2GHZ) | BIT(NL80211_BAND_5GHZ),
	},
	.nvm_ver = IWL3165_NVM_VERSION,
	.pwr_tx_backoffs = iwl7265_pwr_tx_backoffs,
	.dccm_len = IWL7265_DCCM_LEN,
};

const struct iwl_cfg iwl3168_2ac_cfg = {
	.fw_name_pre = IWL3168_FW_PRE,
	IWL_DEVICE_3008,
	.ht_params = {
		.stbc = true,
		.ht40_bands = BIT(NL80211_BAND_2GHZ) | BIT(NL80211_BAND_5GHZ),
	},
	.nvm_ver = IWL3168_NVM_VERSION,
	.pwr_tx_backoffs = iwl7265_pwr_tx_backoffs,
	.dccm_len = IWL7265_DCCM_LEN,
	.nvm_type = IWL_NVM_SDP,
};

const struct iwl_cfg iwl7265_cfg = {
	.fw_name_pre = IWL7265_FW_PRE,
	IWL_DEVICE_7005,
	.ht_params = {
		.stbc = true,
		.ldpc = true,
		.ht40_bands = BIT(NL80211_BAND_2GHZ) | BIT(NL80211_BAND_5GHZ),
	},
	.nvm_ver = IWL7265_NVM_VERSION,
	.pwr_tx_backoffs = iwl7265_pwr_tx_backoffs,
	.dccm_len = IWL7265_DCCM_LEN,
};

const struct iwl_cfg iwl7265d_cfg = {
	.fw_name_pre = IWL7265D_FW_PRE,
	IWL_DEVICE_7005D,
	.ht_params = {
		.stbc = true,
		.ldpc = true,
		.ht40_bands = BIT(NL80211_BAND_2GHZ) | BIT(NL80211_BAND_5GHZ),
	},
	.nvm_ver = IWL7265D_NVM_VERSION,
	.pwr_tx_backoffs = iwl7265_pwr_tx_backoffs,
	.dccm_len = IWL7265_DCCM_LEN,
};

MODULE_FIRMWARE(IWL7260_MODULE_FIRMWARE(IWL7260_UCODE_API_MAX));
MODULE_FIRMWARE(IWL3160_MODULE_FIRMWARE(IWL7260_UCODE_API_MAX));
MODULE_FIRMWARE(IWL3168_MODULE_FIRMWARE(IWL3168_UCODE_API_MAX));
MODULE_FIRMWARE(IWL7265_MODULE_FIRMWARE(IWL7265_UCODE_API_MAX));
MODULE_FIRMWARE(IWL7265D_MODULE_FIRMWARE(IWL7265D_UCODE_API_MAX));
