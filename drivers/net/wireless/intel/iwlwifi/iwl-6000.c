/******************************************************************************
 *
 * Copyright(c) 2008 - 2014 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#include <linux/module.h>
#include <linux/stringify.h>
#include "iwl-config.h"
#include "iwl-agn-hw.h"
#include "dvm/commands.h" /* needed for BT for now */

/* Highest firmware API version supported */
#define IWL6000_UCODE_API_MAX 6
#define IWL6050_UCODE_API_MAX 5
#define IWL6000G2_UCODE_API_MAX 6
#define IWL6035_UCODE_API_MAX 6

/* Oldest version we won't warn about */
#define IWL6000_UCODE_API_OK 4
#define IWL6000G2_UCODE_API_OK 5
#define IWL6050_UCODE_API_OK 5
#define IWL6000G2B_UCODE_API_OK 6
#define IWL6035_UCODE_API_OK 6

/* Lowest firmware API version supported */
#define IWL6000_UCODE_API_MIN 4
#define IWL6050_UCODE_API_MIN 4
#define IWL6000G2_UCODE_API_MIN 5
#define IWL6035_UCODE_API_MIN 6

/* EEPROM versions */
#define EEPROM_6000_TX_POWER_VERSION	(4)
#define EEPROM_6000_EEPROM_VERSION	(0x423)
#define EEPROM_6050_TX_POWER_VERSION	(4)
#define EEPROM_6050_EEPROM_VERSION	(0x532)
#define EEPROM_6150_TX_POWER_VERSION	(6)
#define EEPROM_6150_EEPROM_VERSION	(0x553)
#define EEPROM_6005_TX_POWER_VERSION	(6)
#define EEPROM_6005_EEPROM_VERSION	(0x709)
#define EEPROM_6030_TX_POWER_VERSION	(6)
#define EEPROM_6030_EEPROM_VERSION	(0x709)
#define EEPROM_6035_TX_POWER_VERSION	(6)
#define EEPROM_6035_EEPROM_VERSION	(0x753)

#define IWL6000_FW_PRE "iwlwifi-6000-"
#define IWL6000_MODULE_FIRMWARE(api) IWL6000_FW_PRE __stringify(api) ".ucode"

#define IWL6050_FW_PRE "iwlwifi-6050-"
#define IWL6050_MODULE_FIRMWARE(api) IWL6050_FW_PRE __stringify(api) ".ucode"

#define IWL6005_FW_PRE "iwlwifi-6000g2a-"
#define IWL6005_MODULE_FIRMWARE(api) IWL6005_FW_PRE __stringify(api) ".ucode"

#define IWL6030_FW_PRE "iwlwifi-6000g2b-"
#define IWL6030_MODULE_FIRMWARE(api) IWL6030_FW_PRE __stringify(api) ".ucode"

static const struct iwl_base_params iwl6000_base_params = {
	.eeprom_size = OTP_LOW_IMAGE_SIZE,
	.num_of_queues = IWLAGN_NUM_QUEUES,
	.pll_cfg_val = 0,
	.max_ll_items = OTP_MAX_LL_ITEMS_6x00,
	.shadow_ram_support = true,
	.led_compensation = 51,
	.wd_timeout = IWL_DEF_WD_TIMEOUT,
	.max_event_log_size = 512,
	.shadow_reg_enable = false, /* TODO: fix bugs using this feature */
	.scd_chain_ext_wa = true,
};

static const struct iwl_base_params iwl6050_base_params = {
	.eeprom_size = OTP_LOW_IMAGE_SIZE,
	.num_of_queues = IWLAGN_NUM_QUEUES,
	.pll_cfg_val = 0,
	.max_ll_items = OTP_MAX_LL_ITEMS_6x50,
	.shadow_ram_support = true,
	.led_compensation = 51,
	.wd_timeout = IWL_DEF_WD_TIMEOUT,
	.max_event_log_size = 1024,
	.shadow_reg_enable = false, /* TODO: fix bugs using this feature */
	.scd_chain_ext_wa = true,
};

static const struct iwl_base_params iwl6000_g2_base_params = {
	.eeprom_size = OTP_LOW_IMAGE_SIZE,
	.num_of_queues = IWLAGN_NUM_QUEUES,
	.pll_cfg_val = 0,
	.max_ll_items = OTP_MAX_LL_ITEMS_6x00,
	.shadow_ram_support = true,
	.led_compensation = 57,
	.wd_timeout = IWL_LONG_WD_TIMEOUT,
	.max_event_log_size = 512,
	.shadow_reg_enable = false, /* TODO: fix bugs using this feature */
	.scd_chain_ext_wa = true,
};

static const struct iwl_ht_params iwl6000_ht_params = {
	.ht_greenfield_support = true,
	.use_rts_for_aggregation = true, /* use rts/cts protection */
	.ht40_bands = BIT(IEEE80211_BAND_2GHZ) | BIT(IEEE80211_BAND_5GHZ),
};

static const struct iwl_eeprom_params iwl6000_eeprom_params = {
	.regulatory_bands = {
		EEPROM_REG_BAND_1_CHANNELS,
		EEPROM_REG_BAND_2_CHANNELS,
		EEPROM_REG_BAND_3_CHANNELS,
		EEPROM_REG_BAND_4_CHANNELS,
		EEPROM_REG_BAND_5_CHANNELS,
		EEPROM_6000_REG_BAND_24_HT40_CHANNELS,
		EEPROM_REG_BAND_52_HT40_CHANNELS
	},
	.enhanced_txpower = true,
};

#define IWL_DEVICE_6005						\
	.fw_name_pre = IWL6005_FW_PRE,				\
	.ucode_api_max = IWL6000G2_UCODE_API_MAX,		\
	.ucode_api_ok = IWL6000G2_UCODE_API_OK,			\
	.ucode_api_min = IWL6000G2_UCODE_API_MIN,		\
	.device_family = IWL_DEVICE_FAMILY_6005,		\
	.max_inst_size = IWL60_RTC_INST_SIZE,			\
	.max_data_size = IWL60_RTC_DATA_SIZE,			\
	.nvm_ver = EEPROM_6005_EEPROM_VERSION,		\
	.nvm_calib_ver = EEPROM_6005_TX_POWER_VERSION,	\
	.base_params = &iwl6000_g2_base_params,			\
	.eeprom_params = &iwl6000_eeprom_params,		\
	.led_mode = IWL_LED_RF_STATE,				\
	.max_ht_ampdu_exponent = IEEE80211_HT_MAX_AMPDU_64K

const struct iwl_cfg iwl6005_2agn_cfg = {
	.name = "Intel(R) Centrino(R) Advanced-N 6205 AGN",
	IWL_DEVICE_6005,
	.ht_params = &iwl6000_ht_params,
};

const struct iwl_cfg iwl6005_2abg_cfg = {
	.name = "Intel(R) Centrino(R) Advanced-N 6205 ABG",
	IWL_DEVICE_6005,
};

const struct iwl_cfg iwl6005_2bg_cfg = {
	.name = "Intel(R) Centrino(R) Advanced-N 6205 BG",
	IWL_DEVICE_6005,
};

const struct iwl_cfg iwl6005_2agn_sff_cfg = {
	.name = "Intel(R) Centrino(R) Advanced-N 6205S AGN",
	IWL_DEVICE_6005,
	.ht_params = &iwl6000_ht_params,
};

const struct iwl_cfg iwl6005_2agn_d_cfg = {
	.name = "Intel(R) Centrino(R) Advanced-N 6205D AGN",
	IWL_DEVICE_6005,
	.ht_params = &iwl6000_ht_params,
};

const struct iwl_cfg iwl6005_2agn_mow1_cfg = {
	.name = "Intel(R) Centrino(R) Advanced-N 6206 AGN",
	IWL_DEVICE_6005,
	.ht_params = &iwl6000_ht_params,
};

const struct iwl_cfg iwl6005_2agn_mow2_cfg = {
	.name = "Intel(R) Centrino(R) Advanced-N 6207 AGN",
	IWL_DEVICE_6005,
	.ht_params = &iwl6000_ht_params,
};

#define IWL_DEVICE_6030						\
	.fw_name_pre = IWL6030_FW_PRE,				\
	.ucode_api_max = IWL6000G2_UCODE_API_MAX,		\
	.ucode_api_ok = IWL6000G2B_UCODE_API_OK,		\
	.ucode_api_min = IWL6000G2_UCODE_API_MIN,		\
	.device_family = IWL_DEVICE_FAMILY_6030,		\
	.max_inst_size = IWL60_RTC_INST_SIZE,			\
	.max_data_size = IWL60_RTC_DATA_SIZE,			\
	.nvm_ver = EEPROM_6030_EEPROM_VERSION,		\
	.nvm_calib_ver = EEPROM_6030_TX_POWER_VERSION,	\
	.base_params = &iwl6000_g2_base_params,			\
	.eeprom_params = &iwl6000_eeprom_params,		\
	.led_mode = IWL_LED_RF_STATE,				\
	.max_ht_ampdu_exponent = IEEE80211_HT_MAX_AMPDU_64K

const struct iwl_cfg iwl6030_2agn_cfg = {
	.name = "Intel(R) Centrino(R) Advanced-N 6230 AGN",
	IWL_DEVICE_6030,
	.ht_params = &iwl6000_ht_params,
};

const struct iwl_cfg iwl6030_2abg_cfg = {
	.name = "Intel(R) Centrino(R) Advanced-N 6230 ABG",
	IWL_DEVICE_6030,
};

const struct iwl_cfg iwl6030_2bgn_cfg = {
	.name = "Intel(R) Centrino(R) Advanced-N 6230 BGN",
	IWL_DEVICE_6030,
	.ht_params = &iwl6000_ht_params,
};

const struct iwl_cfg iwl6030_2bg_cfg = {
	.name = "Intel(R) Centrino(R) Advanced-N 6230 BG",
	IWL_DEVICE_6030,
};

#define IWL_DEVICE_6035						\
	.fw_name_pre = IWL6030_FW_PRE,				\
	.ucode_api_max = IWL6035_UCODE_API_MAX,			\
	.ucode_api_ok = IWL6035_UCODE_API_OK,			\
	.ucode_api_min = IWL6035_UCODE_API_MIN,			\
	.device_family = IWL_DEVICE_FAMILY_6030,		\
	.max_inst_size = IWL60_RTC_INST_SIZE,			\
	.max_data_size = IWL60_RTC_DATA_SIZE,			\
	.nvm_ver = EEPROM_6030_EEPROM_VERSION,		\
	.nvm_calib_ver = EEPROM_6030_TX_POWER_VERSION,	\
	.base_params = &iwl6000_g2_base_params,			\
	.eeprom_params = &iwl6000_eeprom_params,		\
	.led_mode = IWL_LED_RF_STATE,				\
	.max_ht_ampdu_exponent = IEEE80211_HT_MAX_AMPDU_64K

const struct iwl_cfg iwl6035_2agn_cfg = {
	.name = "Intel(R) Centrino(R) Advanced-N 6235 AGN",
	IWL_DEVICE_6035,
	.ht_params = &iwl6000_ht_params,
};

const struct iwl_cfg iwl6035_2agn_sff_cfg = {
	.name = "Intel(R) Centrino(R) Ultimate-N 6235 AGN",
	IWL_DEVICE_6035,
	.ht_params = &iwl6000_ht_params,
};

const struct iwl_cfg iwl1030_bgn_cfg = {
	.name = "Intel(R) Centrino(R) Wireless-N 1030 BGN",
	IWL_DEVICE_6030,
	.ht_params = &iwl6000_ht_params,
};

const struct iwl_cfg iwl1030_bg_cfg = {
	.name = "Intel(R) Centrino(R) Wireless-N 1030 BG",
	IWL_DEVICE_6030,
};

const struct iwl_cfg iwl130_bgn_cfg = {
	.name = "Intel(R) Centrino(R) Wireless-N 130 BGN",
	IWL_DEVICE_6030,
	.ht_params = &iwl6000_ht_params,
	.rx_with_siso_diversity = true,
};

const struct iwl_cfg iwl130_bg_cfg = {
	.name = "Intel(R) Centrino(R) Wireless-N 130 BG",
	IWL_DEVICE_6030,
	.rx_with_siso_diversity = true,
};

/*
 * "i": Internal configuration, use internal Power Amplifier
 */
#define IWL_DEVICE_6000i					\
	.fw_name_pre = IWL6000_FW_PRE,				\
	.ucode_api_max = IWL6000_UCODE_API_MAX,			\
	.ucode_api_ok = IWL6000_UCODE_API_OK,			\
	.ucode_api_min = IWL6000_UCODE_API_MIN,			\
	.device_family = IWL_DEVICE_FAMILY_6000i,		\
	.max_inst_size = IWL60_RTC_INST_SIZE,			\
	.max_data_size = IWL60_RTC_DATA_SIZE,			\
	.valid_tx_ant = ANT_BC,		/* .cfg overwrite */	\
	.valid_rx_ant = ANT_BC,		/* .cfg overwrite */	\
	.nvm_ver = EEPROM_6000_EEPROM_VERSION,		\
	.nvm_calib_ver = EEPROM_6000_TX_POWER_VERSION,	\
	.base_params = &iwl6000_base_params,			\
	.eeprom_params = &iwl6000_eeprom_params,		\
	.led_mode = IWL_LED_BLINK,				\
	.max_ht_ampdu_exponent = IEEE80211_HT_MAX_AMPDU_64K

const struct iwl_cfg iwl6000i_2agn_cfg = {
	.name = "Intel(R) Centrino(R) Advanced-N 6200 AGN",
	IWL_DEVICE_6000i,
	.ht_params = &iwl6000_ht_params,
};

const struct iwl_cfg iwl6000i_2abg_cfg = {
	.name = "Intel(R) Centrino(R) Advanced-N 6200 ABG",
	IWL_DEVICE_6000i,
};

const struct iwl_cfg iwl6000i_2bg_cfg = {
	.name = "Intel(R) Centrino(R) Advanced-N 6200 BG",
	IWL_DEVICE_6000i,
};

#define IWL_DEVICE_6050						\
	.fw_name_pre = IWL6050_FW_PRE,				\
	.ucode_api_max = IWL6050_UCODE_API_MAX,			\
	.ucode_api_min = IWL6050_UCODE_API_MIN,			\
	.device_family = IWL_DEVICE_FAMILY_6050,		\
	.max_inst_size = IWL60_RTC_INST_SIZE,			\
	.max_data_size = IWL60_RTC_DATA_SIZE,			\
	.valid_tx_ant = ANT_AB,		/* .cfg overwrite */	\
	.valid_rx_ant = ANT_AB,		/* .cfg overwrite */	\
	.nvm_ver = EEPROM_6050_EEPROM_VERSION,		\
	.nvm_calib_ver = EEPROM_6050_TX_POWER_VERSION,	\
	.base_params = &iwl6050_base_params,			\
	.eeprom_params = &iwl6000_eeprom_params,		\
	.led_mode = IWL_LED_BLINK,				\
	.internal_wimax_coex = true,				\
	.max_ht_ampdu_exponent = IEEE80211_HT_MAX_AMPDU_64K

const struct iwl_cfg iwl6050_2agn_cfg = {
	.name = "Intel(R) Centrino(R) Advanced-N + WiMAX 6250 AGN",
	IWL_DEVICE_6050,
	.ht_params = &iwl6000_ht_params,
};

const struct iwl_cfg iwl6050_2abg_cfg = {
	.name = "Intel(R) Centrino(R) Advanced-N + WiMAX 6250 ABG",
	IWL_DEVICE_6050,
};

#define IWL_DEVICE_6150						\
	.fw_name_pre = IWL6050_FW_PRE,				\
	.ucode_api_max = IWL6050_UCODE_API_MAX,			\
	.ucode_api_min = IWL6050_UCODE_API_MIN,			\
	.device_family = IWL_DEVICE_FAMILY_6150,		\
	.max_inst_size = IWL60_RTC_INST_SIZE,			\
	.max_data_size = IWL60_RTC_DATA_SIZE,			\
	.nvm_ver = EEPROM_6150_EEPROM_VERSION,		\
	.nvm_calib_ver = EEPROM_6150_TX_POWER_VERSION,	\
	.base_params = &iwl6050_base_params,			\
	.eeprom_params = &iwl6000_eeprom_params,		\
	.led_mode = IWL_LED_BLINK,				\
	.internal_wimax_coex = true,				\
	.max_ht_ampdu_exponent = IEEE80211_HT_MAX_AMPDU_64K

const struct iwl_cfg iwl6150_bgn_cfg = {
	.name = "Intel(R) Centrino(R) Wireless-N + WiMAX 6150 BGN",
	IWL_DEVICE_6150,
	.ht_params = &iwl6000_ht_params,
};

const struct iwl_cfg iwl6150_bg_cfg = {
	.name = "Intel(R) Centrino(R) Wireless-N + WiMAX 6150 BG",
	IWL_DEVICE_6150,
};

const struct iwl_cfg iwl6000_3agn_cfg = {
	.name = "Intel(R) Centrino(R) Ultimate-N 6300 AGN",
	.fw_name_pre = IWL6000_FW_PRE,
	.ucode_api_max = IWL6000_UCODE_API_MAX,
	.ucode_api_ok = IWL6000_UCODE_API_OK,
	.ucode_api_min = IWL6000_UCODE_API_MIN,
	.device_family = IWL_DEVICE_FAMILY_6000,
	.max_inst_size = IWL60_RTC_INST_SIZE,
	.max_data_size = IWL60_RTC_DATA_SIZE,
	.nvm_ver = EEPROM_6000_EEPROM_VERSION,
	.nvm_calib_ver = EEPROM_6000_TX_POWER_VERSION,
	.base_params = &iwl6000_base_params,
	.eeprom_params = &iwl6000_eeprom_params,
	.ht_params = &iwl6000_ht_params,
	.led_mode = IWL_LED_BLINK,
};

MODULE_FIRMWARE(IWL6000_MODULE_FIRMWARE(IWL6000_UCODE_API_OK));
MODULE_FIRMWARE(IWL6050_MODULE_FIRMWARE(IWL6050_UCODE_API_OK));
MODULE_FIRMWARE(IWL6005_MODULE_FIRMWARE(IWL6000G2_UCODE_API_OK));
MODULE_FIRMWARE(IWL6030_MODULE_FIRMWARE(IWL6000G2B_UCODE_API_OK));
