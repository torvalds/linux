/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2014 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 Intel Deutschland GmbH
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2014 - 2015 Intel Mobile Communications GmbH
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
#define IWL8000_UCODE_API_MAX	36
#define IWL8265_UCODE_API_MAX	36

/* Lowest firmware API version supported */
#define IWL8000_UCODE_API_MIN	22
#define IWL8265_UCODE_API_MIN	22

/* NVM versions */
#define IWL8000_NVM_VERSION		0x0a1d
#define IWL8000_TX_POWER_VERSION	0xffff /* meaningless */

/* Memory offsets and lengths */
#define IWL8260_DCCM_OFFSET		0x800000
#define IWL8260_DCCM_LEN		0x18000
#define IWL8260_DCCM2_OFFSET		0x880000
#define IWL8260_DCCM2_LEN		0x8000
#define IWL8260_SMEM_OFFSET		0x400000
#define IWL8260_SMEM_LEN		0x68000

#define IWL8000_FW_PRE "iwlwifi-8000C-"
#define IWL8000_MODULE_FIRMWARE(api) \
	IWL8000_FW_PRE __stringify(api) ".ucode"

#define IWL8265_FW_PRE "iwlwifi-8265-"
#define IWL8265_MODULE_FIRMWARE(api) \
	IWL8265_FW_PRE __stringify(api) ".ucode"

#define NVM_HW_SECTION_NUM_FAMILY_8000		10
#define DEFAULT_NVM_FILE_FAMILY_8000C		"nvmData-8000C"

static const struct iwl_base_params iwl8000_base_params = {
	.eeprom_size = OTP_LOW_IMAGE_SIZE_FAMILY_8000,
	.num_of_queues = 31,
	.shadow_ram_support = true,
	.led_compensation = 57,
	.wd_timeout = IWL_LONG_WD_TIMEOUT,
	.max_event_log_size = 512,
	.shadow_reg_enable = true,
	.pcie_l1_allowed = true,
};

static const struct iwl_ht_params iwl8000_ht_params = {
	.stbc = true,
	.ldpc = true,
	.ht40_bands = BIT(NL80211_BAND_2GHZ) | BIT(NL80211_BAND_5GHZ),
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

#define IWL_DEVICE_8000_COMMON						\
	.device_family = IWL_DEVICE_FAMILY_8000,			\
	.max_inst_size = IWL60_RTC_INST_SIZE,				\
	.max_data_size = IWL60_RTC_DATA_SIZE,				\
	.base_params = &iwl8000_base_params,				\
	.led_mode = IWL_LED_RF_STATE,					\
	.nvm_hw_section_num = NVM_HW_SECTION_NUM_FAMILY_8000,		\
	.features = NETIF_F_RXCSUM,					\
	.non_shared_ant = ANT_A,					\
	.dccm_offset = IWL8260_DCCM_OFFSET,				\
	.dccm_len = IWL8260_DCCM_LEN,					\
	.dccm2_offset = IWL8260_DCCM2_OFFSET,				\
	.dccm2_len = IWL8260_DCCM2_LEN,					\
	.smem_offset = IWL8260_SMEM_OFFSET,				\
	.smem_len = IWL8260_SMEM_LEN,					\
	.default_nvm_file_C_step = DEFAULT_NVM_FILE_FAMILY_8000C,	\
	.thermal_params = &iwl8000_tt_params,				\
	.apmg_not_supported = true,					\
	.nvm_type = IWL_NVM_EXT,					\
	.dbgc_supported = true,						\
	.min_umac_error_event_table = 0x800000

#define IWL_DEVICE_8000							\
	IWL_DEVICE_8000_COMMON,						\
	.ucode_api_max = IWL8000_UCODE_API_MAX,				\
	.ucode_api_min = IWL8000_UCODE_API_MIN				\

#define IWL_DEVICE_8260							\
	IWL_DEVICE_8000_COMMON,						\
	.ucode_api_max = IWL8000_UCODE_API_MAX,				\
	.ucode_api_min = IWL8000_UCODE_API_MIN				\

#define IWL_DEVICE_8265							\
	IWL_DEVICE_8000_COMMON,						\
	.ucode_api_max = IWL8265_UCODE_API_MAX,				\
	.ucode_api_min = IWL8265_UCODE_API_MIN				\

const struct iwl_cfg iwl8260_2n_cfg = {
	.name = "Intel(R) Dual Band Wireless N 8260",
	.fw_name_pre = IWL8000_FW_PRE,
	IWL_DEVICE_8260,
	.ht_params = &iwl8000_ht_params,
	.nvm_ver = IWL8000_NVM_VERSION,
	.nvm_calib_ver = IWL8000_TX_POWER_VERSION,
};

const struct iwl_cfg iwl8260_2ac_cfg = {
	.name = "Intel(R) Dual Band Wireless AC 8260",
	.fw_name_pre = IWL8000_FW_PRE,
	IWL_DEVICE_8260,
	.ht_params = &iwl8000_ht_params,
	.nvm_ver = IWL8000_NVM_VERSION,
	.nvm_calib_ver = IWL8000_TX_POWER_VERSION,
	.max_ht_ampdu_exponent = IEEE80211_HT_MAX_AMPDU_64K,
};

const struct iwl_cfg iwl8265_2ac_cfg = {
	.name = "Intel(R) Dual Band Wireless AC 8265",
	.fw_name_pre = IWL8265_FW_PRE,
	IWL_DEVICE_8265,
	.ht_params = &iwl8000_ht_params,
	.nvm_ver = IWL8000_NVM_VERSION,
	.nvm_calib_ver = IWL8000_TX_POWER_VERSION,
	.max_ht_ampdu_exponent = IEEE80211_HT_MAX_AMPDU_64K,
	.vht_mu_mimo_supported = true,
};

const struct iwl_cfg iwl8275_2ac_cfg = {
	.name = "Intel(R) Dual Band Wireless AC 8275",
	.fw_name_pre = IWL8265_FW_PRE,
	IWL_DEVICE_8265,
	.ht_params = &iwl8000_ht_params,
	.nvm_ver = IWL8000_NVM_VERSION,
	.nvm_calib_ver = IWL8000_TX_POWER_VERSION,
	.max_ht_ampdu_exponent = IEEE80211_HT_MAX_AMPDU_64K,
	.vht_mu_mimo_supported = true,
};

const struct iwl_cfg iwl4165_2ac_cfg = {
	.name = "Intel(R) Dual Band Wireless AC 4165",
	.fw_name_pre = IWL8000_FW_PRE,
	IWL_DEVICE_8000,
	.ht_params = &iwl8000_ht_params,
	.nvm_ver = IWL8000_NVM_VERSION,
	.nvm_calib_ver = IWL8000_TX_POWER_VERSION,
	.max_ht_ampdu_exponent = IEEE80211_HT_MAX_AMPDU_64K,
};

MODULE_FIRMWARE(IWL8000_MODULE_FIRMWARE(IWL8000_UCODE_API_MAX));
MODULE_FIRMWARE(IWL8265_MODULE_FIRMWARE(IWL8265_UCODE_API_MAX));
