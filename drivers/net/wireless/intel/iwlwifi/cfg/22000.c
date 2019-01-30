/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2015-2017 Intel Deutschland GmbH
 * Copyright (C) 2018 Intel Corporation
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
 * Copyright (C) 2018 Intel Corporation
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

/* Highest firmware API version supported */
#define IWL_22000_UCODE_API_MAX	43

/* Lowest firmware API version supported */
#define IWL_22000_UCODE_API_MIN	39

/* NVM versions */
#define IWL_22000_NVM_VERSION		0x0a1d

/* Memory offsets and lengths */
#define IWL_22000_DCCM_OFFSET		0x800000 /* LMAC1 */
#define IWL_22000_DCCM_LEN		0x10000 /* LMAC1 */
#define IWL_22000_DCCM2_OFFSET		0x880000
#define IWL_22000_DCCM2_LEN		0x8000
#define IWL_22000_SMEM_OFFSET		0x400000
#define IWL_22000_SMEM_LEN		0xD0000

#define IWL_22000_JF_FW_PRE		"iwlwifi-Qu-a0-jf-b0-"
#define IWL_22000_HR_FW_PRE		"iwlwifi-Qu-a0-hr-a0-"
#define IWL_22000_HR_CDB_FW_PRE		"iwlwifi-QuIcp-z0-hrcdb-a0-"
#define IWL_22000_HR_A_F0_FW_PRE	"iwlwifi-QuQnj-f0-hr-a0-"
#define IWL_22000_HR_B_F0_FW_PRE	"iwlwifi-Qu-b0-hr-b0-"
#define IWL_22000_QU_B_HR_B_FW_PRE	"iwlwifi-Qu-b0-hr-b0-"
#define IWL_22000_HR_B_FW_PRE		"iwlwifi-QuQnj-b0-hr-b0-"
#define IWL_22000_JF_B0_FW_PRE		"iwlwifi-QuQnj-a0-jf-b0-"
#define IWL_22000_HR_A0_FW_PRE		"iwlwifi-QuQnj-a0-hr-a0-"
#define IWL_22000_SU_Z0_FW_PRE		"iwlwifi-su-z0-"
#define IWL_QU_B_JF_B_FW_PRE		"iwlwifi-Qu-b0-jf-b0-"
#define IWL_CC_A_FW_PRE			"iwlwifi-cc-a0-"

#define IWL_22000_HR_MODULE_FIRMWARE(api) \
	IWL_22000_HR_FW_PRE __stringify(api) ".ucode"
#define IWL_22000_JF_MODULE_FIRMWARE(api) \
	IWL_22000_JF_FW_PRE __stringify(api) ".ucode"
#define IWL_22000_HR_A_F0_QNJ_MODULE_FIRMWARE(api) \
	IWL_22000_HR_A_F0_FW_PRE __stringify(api) ".ucode"
#define IWL_22000_HR_B_F0_QNJ_MODULE_FIRMWARE(api) \
	IWL_22000_HR_B_F0_FW_PRE __stringify(api) ".ucode"
#define IWL_22000_QU_B_HR_B_MODULE_FIRMWARE(api) \
	IWL_22000_QU_B_HR_B_FW_PRE __stringify(api) ".ucode"
#define IWL_22000_HR_B_QNJ_MODULE_FIRMWARE(api)	\
	IWL_22000_HR_B_FW_PRE __stringify(api) ".ucode"
#define IWL_22000_JF_B0_QNJ_MODULE_FIRMWARE(api) \
	IWL_22000_JF_B0_FW_PRE __stringify(api) ".ucode"
#define IWL_22000_HR_A0_QNJ_MODULE_FIRMWARE(api) \
	IWL_22000_HR_A0_FW_PRE __stringify(api) ".ucode"
#define IWL_22000_SU_Z0_MODULE_FIRMWARE(api) \
	IWL_22000_SU_Z0_FW_PRE __stringify(api) ".ucode"
#define IWL_QU_B_JF_B_MODULE_FIRMWARE(api) \
	IWL_QU_B_JF_B_FW_PRE __stringify(api) ".ucode"
#define IWL_CC_A_MODULE_FIRMWARE(api) \
	IWL_CC_A_FW_PRE __stringify(api) ".ucode"

static const struct iwl_base_params iwl_22000_base_params = {
	.eeprom_size = OTP_LOW_IMAGE_SIZE_32K,
	.num_of_queues = 512,
	.max_tfd_queue_size = 256,
	.shadow_ram_support = true,
	.led_compensation = 57,
	.wd_timeout = IWL_LONG_WD_TIMEOUT,
	.max_event_log_size = 512,
	.shadow_reg_enable = true,
	.pcie_l1_allowed = true,
};

static const struct iwl_base_params iwl_22560_base_params = {
	.eeprom_size = OTP_LOW_IMAGE_SIZE_32K,
	.num_of_queues = 512,
	.max_tfd_queue_size = 65536,
	.shadow_ram_support = true,
	.led_compensation = 57,
	.wd_timeout = IWL_LONG_WD_TIMEOUT,
	.max_event_log_size = 512,
	.shadow_reg_enable = true,
	.pcie_l1_allowed = true,
};

static const struct iwl_ht_params iwl_22000_ht_params = {
	.stbc = true,
	.ldpc = true,
	.ht40_bands = BIT(NL80211_BAND_2GHZ) | BIT(NL80211_BAND_5GHZ),
};

#define IWL_DEVICE_22000_COMMON						\
	.ucode_api_max = IWL_22000_UCODE_API_MAX,			\
	.ucode_api_min = IWL_22000_UCODE_API_MIN,			\
	.led_mode = IWL_LED_RF_STATE,					\
	.nvm_hw_section_num = 10,					\
	.non_shared_ant = ANT_B,					\
	.dccm_offset = IWL_22000_DCCM_OFFSET,				\
	.dccm_len = IWL_22000_DCCM_LEN,					\
	.dccm2_offset = IWL_22000_DCCM2_OFFSET,				\
	.dccm2_len = IWL_22000_DCCM2_LEN,				\
	.smem_offset = IWL_22000_SMEM_OFFSET,				\
	.smem_len = IWL_22000_SMEM_LEN,					\
	.features = IWL_TX_CSUM_NETIF_FLAGS | NETIF_F_RXCSUM,		\
	.apmg_not_supported = true,					\
	.mq_rx_supported = true,					\
	.vht_mu_mimo_supported = true,					\
	.mac_addr_from_csr = true,					\
	.ht_params = &iwl_22000_ht_params,				\
	.nvm_ver = IWL_22000_NVM_VERSION,				\
	.max_ht_ampdu_exponent = IEEE80211_HT_MAX_AMPDU_64K,		\
	.use_tfh = true,						\
	.rf_id = true,							\
	.gen2 = true,							\
	.nvm_type = IWL_NVM_EXT,					\
	.dbgc_supported = true,						\
	.min_umac_error_event_table = 0x400000,				\
	.d3_debug_data_base_addr = 0x401000,				\
	.d3_debug_data_length = 60 * 1024

#define IWL_DEVICE_22500						\
	IWL_DEVICE_22000_COMMON,					\
	.device_family = IWL_DEVICE_FAMILY_22000,			\
	.base_params = &iwl_22000_base_params,				\
	.csr = &iwl_csr_v1

#define IWL_DEVICE_22560						\
	IWL_DEVICE_22000_COMMON,					\
	.device_family = IWL_DEVICE_FAMILY_22560,			\
	.base_params = &iwl_22560_base_params,				\
	.csr = &iwl_csr_v2

const struct iwl_cfg iwl22000_2ac_cfg_hr = {
	.name = "Intel(R) Dual Band Wireless AC 22000",
	.fw_name_pre = IWL_22000_HR_FW_PRE,
	IWL_DEVICE_22500,
};

const struct iwl_cfg iwl22000_2ac_cfg_hr_cdb = {
	.name = "Intel(R) Dual Band Wireless AC 22000",
	.fw_name_pre = IWL_22000_HR_CDB_FW_PRE,
	IWL_DEVICE_22500,
	.cdb = true,
};

const struct iwl_cfg iwl22000_2ac_cfg_jf = {
	.name = "Intel(R) Dual Band Wireless AC 22000",
	.fw_name_pre = IWL_22000_JF_FW_PRE,
	IWL_DEVICE_22500,
};

const struct iwl_cfg iwl22560_2ax_cfg_hr = {
	.name = "Intel(R) Wireless-AX 22560",
	.fw_name_pre = IWL_22000_QU_B_HR_B_FW_PRE,
	IWL_DEVICE_22500,
	/*
	 * This device doesn't support receiving BlockAck with a large bitmap
	 * so we need to restrict the size of transmitted aggregation to the
	 * HT size; mac80211 would otherwise pick the HE max (256) by default.
	 */
	.max_tx_agg_size = IEEE80211_MAX_AMPDU_BUF_HT,
};

const struct iwl_cfg iwl22260_2ax_cfg = {
	.name = "Intel(R) Wireless-AX 22260",
	.fw_name_pre = IWL_CC_A_FW_PRE,
	IWL_DEVICE_22500,
	/*
	 * This device doesn't support receiving BlockAck with a large bitmap
	 * so we need to restrict the size of transmitted aggregation to the
	 * HT size; mac80211 would otherwise pick the HE max (256) by default.
	 */
	.max_tx_agg_size = IEEE80211_MAX_AMPDU_BUF_HT,
};

const struct iwl_cfg killer1650x_2ax_cfg = {
	.name = "Killer(R) Wireless-AX 1650x Wireless Network Adapter (22260NGW)",
	.fw_name_pre = IWL_CC_A_FW_PRE,
	IWL_DEVICE_22500,
	/*
	 * This device doesn't support receiving BlockAck with a large bitmap
	 * so we need to restrict the size of transmitted aggregation to the
	 * HT size; mac80211 would otherwise pick the HE max (256) by default.
	 */
	.max_tx_agg_size = IEEE80211_MAX_AMPDU_BUF_HT,
};

const struct iwl_cfg killer1650w_2ax_cfg = {
	.name = "Killer(R) Wireless-AX 1650w Wireless Network Adapter (22260D2W)",
	.fw_name_pre = IWL_CC_A_FW_PRE,
	IWL_DEVICE_22500,
	/*
	 * This device doesn't support receiving BlockAck with a large bitmap
	 * so we need to restrict the size of transmitted aggregation to the
	 * HT size; mac80211 would otherwise pick the HE max (256) by default.
	 */
	.max_tx_agg_size = IEEE80211_MAX_AMPDU_BUF_HT,
};

/*
 * All JF radio modules are part of the 9000 series, but the MAC part
 * looks more like 22000.  That's why this device is here, but called
 * 9560 nevertheless.
 */
const struct iwl_cfg iwl9461_2ac_cfg_qu_b0_jf_b0 = {
	.name = "Intel(R) Wireless-AC 9461",
	.fw_name_pre = IWL_QU_B_JF_B_FW_PRE,
	IWL_DEVICE_22500,
};

const struct iwl_cfg iwl9462_2ac_cfg_qu_b0_jf_b0 = {
	.name = "Intel(R) Wireless-AC 9462",
	.fw_name_pre = IWL_QU_B_JF_B_FW_PRE,
	IWL_DEVICE_22500,
};

const struct iwl_cfg iwl9560_2ac_cfg_qu_b0_jf_b0 = {
	.name = "Intel(R) Wireless-AC 9560",
	.fw_name_pre = IWL_QU_B_JF_B_FW_PRE,
	IWL_DEVICE_22500,
};

const struct iwl_cfg iwl9560_2ac_160_cfg_qu_b0_jf_b0 = {
	.name = "Intel(R) Wireless-AC 9560 160MHz",
	.fw_name_pre = IWL_QU_B_JF_B_FW_PRE,
	IWL_DEVICE_22500,
};

const struct iwl_cfg killer1550i_2ac_cfg_qu_b0_jf_b0 = {
	.name = "Killer (R) Wireless-AC 1550i Wireless Network Adapter (9560NGW)",
	.fw_name_pre = IWL_QU_B_JF_B_FW_PRE,
	IWL_DEVICE_22500,
};

const struct iwl_cfg killer1550s_2ac_cfg_qu_b0_jf_b0 = {
	.name = "Killer (R) Wireless-AC 1550s Wireless Network Adapter (9560NGW)",
	.fw_name_pre = IWL_QU_B_JF_B_FW_PRE,
	IWL_DEVICE_22500,
};

const struct iwl_cfg killer1650s_2ax_cfg_qu_b0_hr_b0 = {
	.name = "Killer(R) Wireless-AX 1650i Wireless Network Adapter (22560NGW)",
	.fw_name_pre = IWL_22000_QU_B_HR_B_FW_PRE,
	IWL_DEVICE_22500,
	/*
	 * This device doesn't support receiving BlockAck with a large bitmap
	 * so we need to restrict the size of transmitted aggregation to the
	 * HT size; mac80211 would otherwise pick the HE max (256) by default.
	 */
	.max_tx_agg_size = IEEE80211_MAX_AMPDU_BUF_HT,
};

const struct iwl_cfg killer1650i_2ax_cfg_qu_b0_hr_b0 = {
	.name = "Killer(R) Wireless-AX 1650s Wireless Network Adapter (22560D2W)",
	.fw_name_pre = IWL_22000_QU_B_HR_B_FW_PRE,
	IWL_DEVICE_22500,
	/*
	 * This device doesn't support receiving BlockAck with a large bitmap
	 * so we need to restrict the size of transmitted aggregation to the
	 * HT size; mac80211 would otherwise pick the HE max (256) by default.
	 */
	.max_tx_agg_size = IEEE80211_MAX_AMPDU_BUF_HT,
};

const struct iwl_cfg iwl22000_2ax_cfg_jf = {
	.name = "Intel(R) Dual Band Wireless AX 22000",
	.fw_name_pre = IWL_QU_B_JF_B_FW_PRE,
	IWL_DEVICE_22500,
	/*
	 * This device doesn't support receiving BlockAck with a large bitmap
	 * so we need to restrict the size of transmitted aggregation to the
	 * HT size; mac80211 would otherwise pick the HE max (256) by default.
	 */
	.max_tx_agg_size = IEEE80211_MAX_AMPDU_BUF_HT,
};

const struct iwl_cfg iwl22000_2ax_cfg_qnj_hr_a0_f0 = {
	.name = "Intel(R) Dual Band Wireless AX 22000",
	.fw_name_pre = IWL_22000_HR_A_F0_FW_PRE,
	IWL_DEVICE_22500,
	/*
	 * This device doesn't support receiving BlockAck with a large bitmap
	 * so we need to restrict the size of transmitted aggregation to the
	 * HT size; mac80211 would otherwise pick the HE max (256) by default.
	 */
	.max_tx_agg_size = IEEE80211_MAX_AMPDU_BUF_HT,
};

const struct iwl_cfg iwl22000_2ax_cfg_qnj_hr_b0 = {
	.name = "Intel(R) Dual Band Wireless AX 22000",
	.fw_name_pre = IWL_22000_HR_B_FW_PRE,
	IWL_DEVICE_22500,
	/*
	 * This device doesn't support receiving BlockAck with a large bitmap
	 * so we need to restrict the size of transmitted aggregation to the
	 * HT size; mac80211 would otherwise pick the HE max (256) by default.
	 */
	.max_tx_agg_size = IEEE80211_MAX_AMPDU_BUF_HT,
};

const struct iwl_cfg iwl22000_2ax_cfg_qnj_jf_b0 = {
	.name = "Intel(R) Dual Band Wireless AX 22000",
	.fw_name_pre = IWL_22000_JF_B0_FW_PRE,
	IWL_DEVICE_22500,
	/*
	 * This device doesn't support receiving BlockAck with a large bitmap
	 * so we need to restrict the size of transmitted aggregation to the
	 * HT size; mac80211 would otherwise pick the HE max (256) by default.
	 */
	.max_tx_agg_size = IEEE80211_MAX_AMPDU_BUF_HT,
};

const struct iwl_cfg iwl22000_2ax_cfg_qnj_hr_a0 = {
	.name = "Intel(R) Dual Band Wireless AX 22000",
	.fw_name_pre = IWL_22000_HR_A0_FW_PRE,
	IWL_DEVICE_22500,
	/*
	 * This device doesn't support receiving BlockAck with a large bitmap
	 * so we need to restrict the size of transmitted aggregation to the
	 * HT size; mac80211 would otherwise pick the HE max (256) by default.
	 */
	.max_tx_agg_size = IEEE80211_MAX_AMPDU_BUF_HT,
};

const struct iwl_cfg iwl22560_2ax_cfg_su_cdb = {
	.name = "Intel(R) Dual Band Wireless AX 22560",
	.fw_name_pre = IWL_22000_SU_Z0_FW_PRE,
	IWL_DEVICE_22560,
	.cdb = true,
	/*
	 * This device doesn't support receiving BlockAck with a large bitmap
	 * so we need to restrict the size of transmitted aggregation to the
	 * HT size; mac80211 would otherwise pick the HE max (256) by default.
	 */
	.max_tx_agg_size = IEEE80211_MAX_AMPDU_BUF_HT,
};

MODULE_FIRMWARE(IWL_22000_HR_MODULE_FIRMWARE(IWL_22000_UCODE_API_MAX));
MODULE_FIRMWARE(IWL_22000_JF_MODULE_FIRMWARE(IWL_22000_UCODE_API_MAX));
MODULE_FIRMWARE(IWL_22000_HR_A_F0_QNJ_MODULE_FIRMWARE(IWL_22000_UCODE_API_MAX));
MODULE_FIRMWARE(IWL_22000_HR_B_F0_QNJ_MODULE_FIRMWARE(IWL_22000_UCODE_API_MAX));
MODULE_FIRMWARE(IWL_22000_HR_B_QNJ_MODULE_FIRMWARE(IWL_22000_UCODE_API_MAX));
MODULE_FIRMWARE(IWL_22000_JF_B0_QNJ_MODULE_FIRMWARE(IWL_22000_UCODE_API_MAX));
MODULE_FIRMWARE(IWL_22000_HR_A0_QNJ_MODULE_FIRMWARE(IWL_22000_UCODE_API_MAX));
MODULE_FIRMWARE(IWL_22000_SU_Z0_MODULE_FIRMWARE(IWL_22000_UCODE_API_MAX));
MODULE_FIRMWARE(IWL_QU_B_JF_B_MODULE_FIRMWARE(IWL_22000_UCODE_API_MAX));
MODULE_FIRMWARE(IWL_CC_A_MODULE_FIRMWARE(IWL_22000_UCODE_API_MAX));
