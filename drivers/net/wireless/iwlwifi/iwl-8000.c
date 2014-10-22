/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2014 Intel Mobile Communications GmbH
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
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2014 Intel Mobile Communications GmbH
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
#define IWL8000_UCODE_API_MAX	10

/* Oldest version we won't warn about */
#define IWL8000_UCODE_API_OK	8

/* Lowest firmware API version supported */
#define IWL8000_UCODE_API_MIN	8

/* NVM versions */
#define IWL8000_NVM_VERSION		0x0a1d
#define IWL8000_TX_POWER_VERSION	0xffff /* meaningless */

#define IWL8000_FW_PRE "iwlwifi-8000"
#define IWL8000_MODULE_FIRMWARE(api) IWL8000_FW_PRE __stringify(api) ".ucode"

#define NVM_HW_SECTION_NUM_FAMILY_8000		10
#define DEFAULT_NVM_FILE_FAMILY_8000		"iwl_nvm_8000.bin"

/* Max SDIO RX aggregation size of the ADDBA request/response */
#define MAX_RX_AGG_SIZE_8260_SDIO	28

static const struct iwl_base_params iwl8000_base_params = {
	.eeprom_size = OTP_LOW_IMAGE_SIZE_FAMILY_8000,
	.num_of_queues = IWLAGN_NUM_QUEUES,
	.pll_cfg_val = 0,
	.shadow_ram_support = true,
	.led_compensation = 57,
	.wd_timeout = IWL_LONG_WD_TIMEOUT,
	.max_event_log_size = 512,
	.shadow_reg_enable = true,
	.pcie_l1_allowed = true,
};

static const struct iwl_ht_params iwl8000_ht_params = {
	.ldpc = true,
	.ht40_bands = BIT(IEEE80211_BAND_2GHZ) | BIT(IEEE80211_BAND_5GHZ),
};

#define IWL_DEVICE_8000						\
	.ucode_api_max = IWL8000_UCODE_API_MAX,			\
	.ucode_api_ok = IWL8000_UCODE_API_OK,			\
	.ucode_api_min = IWL8000_UCODE_API_MIN,			\
	.device_family = IWL_DEVICE_FAMILY_8000,		\
	.max_inst_size = IWL60_RTC_INST_SIZE,			\
	.max_data_size = IWL60_RTC_DATA_SIZE,			\
	.base_params = &iwl8000_base_params,			\
	.led_mode = IWL_LED_RF_STATE,				\
	.nvm_hw_section_num = NVM_HW_SECTION_NUM_FAMILY_8000,	\
	.non_shared_ant = ANT_A

const struct iwl_cfg iwl8260_2n_cfg = {
	.name = "Intel(R) Dual Band Wireless N 8260",
	.fw_name_pre = IWL8000_FW_PRE,
	IWL_DEVICE_8000,
	.ht_params = &iwl8000_ht_params,
	.nvm_ver = IWL8000_NVM_VERSION,
	.nvm_calib_ver = IWL8000_TX_POWER_VERSION,
};

const struct iwl_cfg iwl8260_2ac_cfg = {
	.name = "Intel(R) Dual Band Wireless AC 8260",
	.fw_name_pre = IWL8000_FW_PRE,
	IWL_DEVICE_8000,
	.ht_params = &iwl8000_ht_params,
	.nvm_ver = IWL8000_NVM_VERSION,
	.nvm_calib_ver = IWL8000_TX_POWER_VERSION,
};

const struct iwl_cfg iwl8260_2ac_sdio_cfg = {
	.name = "Intel(R) Dual Band Wireless-AC 8260",
	.fw_name_pre = IWL8000_FW_PRE,
	IWL_DEVICE_8000,
	.ht_params = &iwl8000_ht_params,
	.nvm_ver = IWL8000_NVM_VERSION,
	.nvm_calib_ver = IWL8000_TX_POWER_VERSION,
	.default_nvm_file = DEFAULT_NVM_FILE_FAMILY_8000,
	.max_rx_agg_size = MAX_RX_AGG_SIZE_8260_SDIO,
	.disable_dummy_notification = true,
};

MODULE_FIRMWARE(IWL8000_MODULE_FIRMWARE(IWL8000_UCODE_API_OK));
