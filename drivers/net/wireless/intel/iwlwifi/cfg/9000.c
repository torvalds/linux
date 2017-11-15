/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2015-2017 Intel Deutschland GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2015-2017 Intel Deutschland GmbH
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/

#include <linux/module.h>
#include <linux/stringify.h>
#include "iwl-config.h"
#include "iwl-agn-hw.h"

/* Highest firmware API version supported */
#define IWL9000_UCODE_API_MAX	34

/* Lowest firmware API version supported */
#define IWL9000_UCODE_API_MIN	30

/* NVM versions */
#define IWL9000_NVM_VERSION		0x0a1d
#define IWL9000_TX_POWER_VERSION	0xffff /* meaningless */

/* Memory offsets and lengths */
#define IWL9000_DCCM_OFFSET		0x800000
#define IWL9000_DCCM_LEN		0x18000
#define IWL9000_DCCM2_OFFSET		0x880000
#define IWL9000_DCCM2_LEN		0x8000
#define IWL9000_SMEM_OFFSET		0x400000
#define IWL9000_SMEM_LEN		0x68000

#define  IWL9000A_FW_PRE "iwlwifi-9000-pu-a0-jf-a0-"
#define  IWL9000B_FW_PRE "iwlwifi-9000-pu-b0-jf-b0-"
#define  IWL9000RFB_FW_PRE "iwlwifi-9000-pu-a0-jf-b0-"
#define  IWL9260A_FW_PRE "iwlwifi-9260-th-a0-jf-a0-"
#define  IWL9260B_FW_PRE "iwlwifi-9260-th-b0-jf-b0-"
#define IWL9000A_MODULE_FIRMWARE(api) \
	IWL9000A_FW_PRE __stringify(api) ".ucode"
#define IWL9000B_MODULE_FIRMWARE(api) \
	IWL9000B_FW_PRE __stringify(api) ".ucode"
#define IWL9000RFB_MODULE_FIRMWARE(api) \
	IWL9000RFB_FW_PRE "-" __stringify(api) ".ucode"
#define IWL9260A_MODULE_FIRMWARE(api) \
	IWL9260A_FW_PRE "-" __stringify(api) ".ucode"
#define IWL9260B_MODULE_FIRMWARE(api) \
	IWL9260B_FW_PRE "-" __stringify(api) ".ucode"

#define NVM_HW_SECTION_NUM_FAMILY_9000		10

static const struct iwl_base_params iwl9000_base_params = {
	.eeprom_size = OTP_LOW_IMAGE_SIZE_FAMILY_9000,
	.num_of_queues = 31,
	.shadow_ram_support = true,
	.led_compensation = 57,
	.wd_timeout = IWL_LONG_WD_TIMEOUT,
	.max_event_log_size = 512,
	.shadow_reg_enable = true,
	.pcie_l1_allowed = true,
};

static const struct iwl_ht_params iwl9000_ht_params = {
	.stbc = true,
	.ldpc = true,
	.ht40_bands = BIT(NL80211_BAND_2GHZ) | BIT(NL80211_BAND_5GHZ),
};

static const struct iwl_tt_params iwl9000_tt_params = {
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

#define IWL_DEVICE_9000							\
	.ucode_api_max = IWL9000_UCODE_API_MAX,				\
	.ucode_api_min = IWL9000_UCODE_API_MIN,				\
	.device_family = IWL_DEVICE_FAMILY_9000,			\
	.max_inst_size = IWL60_RTC_INST_SIZE,				\
	.max_data_size = IWL60_RTC_DATA_SIZE,				\
	.base_params = &iwl9000_base_params,				\
	.led_mode = IWL_LED_RF_STATE,					\
	.nvm_hw_section_num = NVM_HW_SECTION_NUM_FAMILY_9000,		\
	.non_shared_ant = ANT_A,					\
	.dccm_offset = IWL9000_DCCM_OFFSET,				\
	.dccm_len = IWL9000_DCCM_LEN,					\
	.dccm2_offset = IWL9000_DCCM2_OFFSET,				\
	.dccm2_len = IWL9000_DCCM2_LEN,					\
	.smem_offset = IWL9000_SMEM_OFFSET,				\
	.smem_len = IWL9000_SMEM_LEN,					\
	.features = IWL_TX_CSUM_NETIF_FLAGS | NETIF_F_RXCSUM,		\
	.thermal_params = &iwl9000_tt_params,				\
	.apmg_not_supported = true,					\
	.mq_rx_supported = true,					\
	.vht_mu_mimo_supported = true,					\
	.mac_addr_from_csr = true,					\
	.rf_id = true,							\
	.nvm_type = IWL_NVM_EXT,					\
	.dbgc_supported = true,						\
	.min_umac_error_event_table = 0x800000

const struct iwl_cfg iwl9160_2ac_cfg = {
	.name = "Intel(R) Dual Band Wireless AC 9160",
	.fw_name_pre = IWL9260A_FW_PRE,
	.fw_name_pre_b_or_c_step = IWL9260B_FW_PRE,
	IWL_DEVICE_9000,
	.ht_params = &iwl9000_ht_params,
	.nvm_ver = IWL9000_NVM_VERSION,
	.nvm_calib_ver = IWL9000_TX_POWER_VERSION,
	.max_ht_ampdu_exponent = IEEE80211_HT_MAX_AMPDU_64K,
};

const struct iwl_cfg iwl9260_2ac_cfg = {
	.name = "Intel(R) Dual Band Wireless AC 9260",
	.fw_name_pre = IWL9260A_FW_PRE,
	.fw_name_pre_b_or_c_step = IWL9260B_FW_PRE,
	IWL_DEVICE_9000,
	.ht_params = &iwl9000_ht_params,
	.nvm_ver = IWL9000_NVM_VERSION,
	.nvm_calib_ver = IWL9000_TX_POWER_VERSION,
	.max_ht_ampdu_exponent = IEEE80211_HT_MAX_AMPDU_64K,
};

const struct iwl_cfg iwl9270_2ac_cfg = {
	.name = "Intel(R) Dual Band Wireless AC 9270",
	.fw_name_pre = IWL9260A_FW_PRE,
	.fw_name_pre_b_or_c_step = IWL9260B_FW_PRE,
	IWL_DEVICE_9000,
	.ht_params = &iwl9000_ht_params,
	.nvm_ver = IWL9000_NVM_VERSION,
	.nvm_calib_ver = IWL9000_TX_POWER_VERSION,
	.max_ht_ampdu_exponent = IEEE80211_HT_MAX_AMPDU_64K,
};

const struct iwl_cfg iwl9460_2ac_cfg = {
	.name = "Intel(R) Dual Band Wireless AC 9460",
	.fw_name_pre = IWL9260A_FW_PRE,
	.fw_name_pre_b_or_c_step = IWL9260B_FW_PRE,
	IWL_DEVICE_9000,
	.ht_params = &iwl9000_ht_params,
	.nvm_ver = IWL9000_NVM_VERSION,
	.nvm_calib_ver = IWL9000_TX_POWER_VERSION,
	.max_ht_ampdu_exponent = IEEE80211_HT_MAX_AMPDU_64K,
};

const struct iwl_cfg iwl9460_2ac_cfg_soc = {
	.name = "Intel(R) Dual Band Wireless AC 9460",
	.fw_name_pre = IWL9000A_FW_PRE,
	.fw_name_pre_b_or_c_step = IWL9000B_FW_PRE,
	.fw_name_pre_rf_next_step = IWL9000RFB_FW_PRE,
	IWL_DEVICE_9000,
	.ht_params = &iwl9000_ht_params,
	.nvm_ver = IWL9000_NVM_VERSION,
	.nvm_calib_ver = IWL9000_TX_POWER_VERSION,
	.max_ht_ampdu_exponent = IEEE80211_HT_MAX_AMPDU_64K,
	.integrated = true,
	.soc_latency = 5000,
};

const struct iwl_cfg iwl9461_2ac_cfg_soc = {
		.name = "Intel(R) Dual Band Wireless AC 9461",
		.fw_name_pre = IWL9000A_FW_PRE,
		.fw_name_pre_b_or_c_step = IWL9000B_FW_PRE,
		.fw_name_pre_rf_next_step = IWL9000RFB_FW_PRE,
		IWL_DEVICE_9000,
		.ht_params = &iwl9000_ht_params,
		.nvm_ver = IWL9000_NVM_VERSION,
		.nvm_calib_ver = IWL9000_TX_POWER_VERSION,
		.max_ht_ampdu_exponent = IEEE80211_HT_MAX_AMPDU_64K,
		.integrated = true,
		.soc_latency = 5000,
};

const struct iwl_cfg iwl9462_2ac_cfg_soc = {
		.name = "Intel(R) Dual Band Wireless AC 9462",
		.fw_name_pre = IWL9000A_FW_PRE,
		.fw_name_pre_b_or_c_step = IWL9000B_FW_PRE,
		.fw_name_pre_rf_next_step = IWL9000RFB_FW_PRE,
		IWL_DEVICE_9000,
		.ht_params = &iwl9000_ht_params,
		.nvm_ver = IWL9000_NVM_VERSION,
		.nvm_calib_ver = IWL9000_TX_POWER_VERSION,
		.max_ht_ampdu_exponent = IEEE80211_HT_MAX_AMPDU_64K,
		.integrated = true,
		.soc_latency = 5000,
};

const struct iwl_cfg iwl9560_2ac_cfg = {
	.name = "Intel(R) Dual Band Wireless AC 9560",
	.fw_name_pre = IWL9260A_FW_PRE,
	.fw_name_pre_b_or_c_step = IWL9260B_FW_PRE,
	IWL_DEVICE_9000,
	.ht_params = &iwl9000_ht_params,
	.nvm_ver = IWL9000_NVM_VERSION,
	.nvm_calib_ver = IWL9000_TX_POWER_VERSION,
	.max_ht_ampdu_exponent = IEEE80211_HT_MAX_AMPDU_64K,
};

const struct iwl_cfg iwl9560_2ac_cfg_soc = {
	.name = "Intel(R) Dual Band Wireless AC 9560",
	.fw_name_pre = IWL9000A_FW_PRE,
	.fw_name_pre_b_or_c_step = IWL9000B_FW_PRE,
	.fw_name_pre_rf_next_step = IWL9000RFB_FW_PRE,
	IWL_DEVICE_9000,
	.ht_params = &iwl9000_ht_params,
	.nvm_ver = IWL9000_NVM_VERSION,
	.nvm_calib_ver = IWL9000_TX_POWER_VERSION,
	.max_ht_ampdu_exponent = IEEE80211_HT_MAX_AMPDU_64K,
	.integrated = true,
	.soc_latency = 5000,
};
MODULE_FIRMWARE(IWL9000A_MODULE_FIRMWARE(IWL9000_UCODE_API_MAX));
MODULE_FIRMWARE(IWL9000B_MODULE_FIRMWARE(IWL9000_UCODE_API_MAX));
MODULE_FIRMWARE(IWL9000RFB_MODULE_FIRMWARE(IWL9000_UCODE_API_MAX));
MODULE_FIRMWARE(IWL9260A_MODULE_FIRMWARE(IWL9000_UCODE_API_MAX));
MODULE_FIRMWARE(IWL9260B_MODULE_FIRMWARE(IWL9000_UCODE_API_MAX));
