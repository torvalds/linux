// SPDX-License-Identifier: GPL-2.0-only
/******************************************************************************
 *
 * Copyright(c) 2007 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2018 - 2020, 2023, 2025 Intel Corporation
 *****************************************************************************/

#include <linux/module.h>
#include <linux/stringify.h>
#include "iwl-config.h"
#include "iwl-agn-hw.h"

/* Highest firmware API version supported */
#define IWL5000_UCODE_API_MAX 5
#define IWL5150_UCODE_API_MAX 2

/* Lowest firmware API version supported */
#define IWL5000_UCODE_API_MIN 1
#define IWL5150_UCODE_API_MIN 1

/* EEPROM versions */
#define EEPROM_5000_TX_POWER_VERSION	(4)
#define EEPROM_5000_EEPROM_VERSION	(0x11A)
#define EEPROM_5050_TX_POWER_VERSION	(4)
#define EEPROM_5050_EEPROM_VERSION	(0x21E)

#define IWL5000_FW_PRE "iwlwifi-5000"
#define IWL5000_MODULE_FIRMWARE(api) IWL5000_FW_PRE "-" __stringify(api) ".ucode"

#define IWL5150_FW_PRE "iwlwifi-5150"
#define IWL5150_MODULE_FIRMWARE(api) IWL5150_FW_PRE "-" __stringify(api) ".ucode"

static const struct iwl_base_params iwl5000_base_params = {
	.eeprom_size = IWLAGN_EEPROM_IMG_SIZE,
	.num_of_queues = IWLAGN_NUM_QUEUES,
	.max_tfd_queue_size = 256,
	.pll_cfg = true,
	.led_compensation = 51,
	.wd_timeout = IWL_WATCHDOG_DISABLED,
	.max_event_log_size = 512,
	.scd_chain_ext_wa = true,
};

static const struct iwl_eeprom_params iwl5000_eeprom_params = {
	.regulatory_bands = {
		EEPROM_REG_BAND_1_CHANNELS,
		EEPROM_REG_BAND_2_CHANNELS,
		EEPROM_REG_BAND_3_CHANNELS,
		EEPROM_REG_BAND_4_CHANNELS,
		EEPROM_REG_BAND_5_CHANNELS,
		EEPROM_REG_BAND_24_HT40_CHANNELS,
		EEPROM_REG_BAND_52_HT40_CHANNELS
	},
};

const struct iwl_cfg_trans_params iwl5000_trans_cfg = {
	.device_family = IWL_DEVICE_FAMILY_5000,
	.base_params = &iwl5000_base_params,
};

#define IWL_DEVICE_5000						\
	.fw_name_pre = IWL5000_FW_PRE,				\
	.ucode_api_max = IWL5000_UCODE_API_MAX,			\
	.ucode_api_min = IWL5000_UCODE_API_MIN,			\
	.max_inst_size = IWLAGN_RTC_INST_SIZE,			\
	.max_data_size = IWLAGN_RTC_DATA_SIZE,			\
	.nvm_ver = EEPROM_5000_EEPROM_VERSION,		\
	.nvm_calib_ver = EEPROM_5000_TX_POWER_VERSION,	\
	.eeprom_params = &iwl5000_eeprom_params,		\
	.led_mode = IWL_LED_BLINK

const struct iwl_cfg iwl5300_agn_cfg = {
	IWL_DEVICE_5000,
	/* at least EEPROM 0x11A has wrong info */
	.valid_tx_ant = ANT_ABC,	/* .cfg overwrite */
	.valid_rx_ant = ANT_ABC,	/* .cfg overwrite */
	.ht_params = {
		.ht_greenfield_support = true,
		.ht40_bands = BIT(NL80211_BAND_2GHZ) | BIT(NL80211_BAND_5GHZ),
	},
};

const char iwl5300_agn_name[] = "Intel(R) Ultimate N WiFi Link 5300 AGN";

const struct iwl_cfg iwl5100_n_cfg = {
	IWL_DEVICE_5000,
	.valid_tx_ant = ANT_B,		/* .cfg overwrite */
	.valid_rx_ant = ANT_AB,		/* .cfg overwrite */
	.ht_params = {
		.ht_greenfield_support = true,
		.ht40_bands = BIT(NL80211_BAND_2GHZ) | BIT(NL80211_BAND_5GHZ),
	},
};

const char iwl5100_bgn_name[] = "Intel(R) WiFi Link 5100 BGN";

const struct iwl_cfg iwl5100_abg_cfg = {
	IWL_DEVICE_5000,
	.valid_tx_ant = ANT_B,		/* .cfg overwrite */
	.valid_rx_ant = ANT_AB,		/* .cfg overwrite */
};

const char iwl5100_abg_name[] = "Intel(R) WiFi Link 5100 ABG";
const char iwl5100_agn_name[] = "Intel(R) WiFi Link 5100 AGN";

const struct iwl_cfg iwl5350_agn_cfg = {
	.fw_name_pre = IWL5000_FW_PRE,
	.ucode_api_max = IWL5000_UCODE_API_MAX,
	.ucode_api_min = IWL5000_UCODE_API_MIN,
	.max_inst_size = IWLAGN_RTC_INST_SIZE,
	.max_data_size = IWLAGN_RTC_DATA_SIZE,
	.nvm_ver = EEPROM_5050_EEPROM_VERSION,
	.nvm_calib_ver = EEPROM_5050_TX_POWER_VERSION,
	.eeprom_params = &iwl5000_eeprom_params,
	.ht_params = {
		.ht_greenfield_support = true,
		.ht40_bands = BIT(NL80211_BAND_2GHZ) | BIT(NL80211_BAND_5GHZ),
	},
	.led_mode = IWL_LED_BLINK,
	.internal_wimax_coex = true,
};

const char iwl5350_agn_name[] = "Intel(R) WiMAX/WiFi Link 5350 AGN";

const struct iwl_cfg_trans_params iwl5150_trans_cfg = {
	.device_family = IWL_DEVICE_FAMILY_5150,
	.base_params = &iwl5000_base_params,
};

#define IWL_DEVICE_5150						\
	.fw_name_pre = IWL5150_FW_PRE,				\
	.ucode_api_max = IWL5150_UCODE_API_MAX,			\
	.ucode_api_min = IWL5150_UCODE_API_MIN,			\
	.max_inst_size = IWLAGN_RTC_INST_SIZE,			\
	.max_data_size = IWLAGN_RTC_DATA_SIZE,			\
	.nvm_ver = EEPROM_5050_EEPROM_VERSION,		\
	.nvm_calib_ver = EEPROM_5050_TX_POWER_VERSION,	\
	.eeprom_params = &iwl5000_eeprom_params,		\
	.led_mode = IWL_LED_BLINK,				\
	.internal_wimax_coex = true

const struct iwl_cfg iwl5150_agn_cfg = {
	IWL_DEVICE_5150,
	.ht_params = {
		.ht_greenfield_support = true,
		.ht40_bands = BIT(NL80211_BAND_2GHZ) | BIT(NL80211_BAND_5GHZ),
	},
};

const char iwl5150_agn_name[] = "Intel(R) WiMAX/WiFi Link 5150 AGN";

const struct iwl_cfg iwl5150_abg_cfg = {
	IWL_DEVICE_5150,
};

const char iwl5150_abg_name[] = "Intel(R) WiMAX/WiFi Link 5150 ABG";

MODULE_FIRMWARE(IWL5000_MODULE_FIRMWARE(IWL5000_UCODE_API_MAX));
MODULE_FIRMWARE(IWL5150_MODULE_FIRMWARE(IWL5150_UCODE_API_MAX));
