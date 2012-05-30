/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Intel Corporation. All rights reserved.
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
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#include <linux/module.h>
#include <linux/stringify.h>
#include "iwl-config.h"
#include "iwl-agn-hw.h"
#include "iwl-csr.h"
#include "cfg.h"

/* Highest firmware API version supported */
#define IWL5000_UCODE_API_MAX 5
#define IWL5150_UCODE_API_MAX 2

/* Oldest version we won't warn about */
#define IWL5000_UCODE_API_OK 5
#define IWL5150_UCODE_API_OK 2

/* Lowest firmware API version supported */
#define IWL5000_UCODE_API_MIN 1
#define IWL5150_UCODE_API_MIN 1

/* EEPROM versions */
#define EEPROM_5000_TX_POWER_VERSION	(4)
#define EEPROM_5000_EEPROM_VERSION	(0x11A)
#define EEPROM_5050_TX_POWER_VERSION	(4)
#define EEPROM_5050_EEPROM_VERSION	(0x21E)

#define IWL5000_FW_PRE "iwlwifi-5000-"
#define IWL5000_MODULE_FIRMWARE(api) IWL5000_FW_PRE __stringify(api) ".ucode"

#define IWL5150_FW_PRE "iwlwifi-5150-"
#define IWL5150_MODULE_FIRMWARE(api) IWL5150_FW_PRE __stringify(api) ".ucode"

static const struct iwl_base_params iwl5000_base_params = {
	.eeprom_size = IWLAGN_EEPROM_IMG_SIZE,
	.num_of_queues = IWLAGN_NUM_QUEUES,
	.pll_cfg_val = CSR50_ANA_PLL_CFG_VAL,
	.led_compensation = 51,
	.plcp_delta_threshold = IWL_MAX_PLCP_ERR_LONG_THRESHOLD_DEF,
	.chain_noise_scale = 1000,
	.wd_timeout = IWL_WATCHDOG_DISABLED,
	.max_event_log_size = 512,
	.no_idle_support = true,
};

static const struct iwl_ht_params iwl5000_ht_params = {
	.ht_greenfield_support = true,
	.ht40_bands = BIT(IEEE80211_BAND_2GHZ) | BIT(IEEE80211_BAND_5GHZ),
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

#define IWL_DEVICE_5000						\
	.fw_name_pre = IWL5000_FW_PRE,				\
	.ucode_api_max = IWL5000_UCODE_API_MAX,			\
	.ucode_api_ok = IWL5000_UCODE_API_OK,			\
	.ucode_api_min = IWL5000_UCODE_API_MIN,			\
	.device_family = IWL_DEVICE_FAMILY_5000,		\
	.max_inst_size = IWLAGN_RTC_INST_SIZE,			\
	.max_data_size = IWLAGN_RTC_DATA_SIZE,			\
	.eeprom_ver = EEPROM_5000_EEPROM_VERSION,		\
	.eeprom_calib_ver = EEPROM_5000_TX_POWER_VERSION,	\
	.base_params = &iwl5000_base_params,			\
	.eeprom_params = &iwl5000_eeprom_params,		\
	.led_mode = IWL_LED_BLINK

const struct iwl_cfg iwl5300_agn_cfg = {
	.name = "Intel(R) Ultimate N WiFi Link 5300 AGN",
	IWL_DEVICE_5000,
	/* at least EEPROM 0x11A has wrong info */
	.valid_tx_ant = ANT_ABC,	/* .cfg overwrite */
	.valid_rx_ant = ANT_ABC,	/* .cfg overwrite */
	.ht_params = &iwl5000_ht_params,
};

const struct iwl_cfg iwl5100_bgn_cfg = {
	.name = "Intel(R) WiFi Link 5100 BGN",
	IWL_DEVICE_5000,
	.valid_tx_ant = ANT_B,		/* .cfg overwrite */
	.valid_rx_ant = ANT_AB,		/* .cfg overwrite */
	.ht_params = &iwl5000_ht_params,
};

const struct iwl_cfg iwl5100_abg_cfg = {
	.name = "Intel(R) WiFi Link 5100 ABG",
	IWL_DEVICE_5000,
	.valid_tx_ant = ANT_B,		/* .cfg overwrite */
	.valid_rx_ant = ANT_AB,		/* .cfg overwrite */
};

const struct iwl_cfg iwl5100_agn_cfg = {
	.name = "Intel(R) WiFi Link 5100 AGN",
	IWL_DEVICE_5000,
	.valid_tx_ant = ANT_B,		/* .cfg overwrite */
	.valid_rx_ant = ANT_AB,		/* .cfg overwrite */
	.ht_params = &iwl5000_ht_params,
};

const struct iwl_cfg iwl5350_agn_cfg = {
	.name = "Intel(R) WiMAX/WiFi Link 5350 AGN",
	.fw_name_pre = IWL5000_FW_PRE,
	.ucode_api_max = IWL5000_UCODE_API_MAX,
	.ucode_api_ok = IWL5000_UCODE_API_OK,
	.ucode_api_min = IWL5000_UCODE_API_MIN,
	.device_family = IWL_DEVICE_FAMILY_5000,
	.max_inst_size = IWLAGN_RTC_INST_SIZE,
	.max_data_size = IWLAGN_RTC_DATA_SIZE,
	.eeprom_ver = EEPROM_5050_EEPROM_VERSION,
	.eeprom_calib_ver = EEPROM_5050_TX_POWER_VERSION,
	.base_params = &iwl5000_base_params,
	.eeprom_params = &iwl5000_eeprom_params,
	.ht_params = &iwl5000_ht_params,
	.led_mode = IWL_LED_BLINK,
	.internal_wimax_coex = true,
};

#define IWL_DEVICE_5150						\
	.fw_name_pre = IWL5150_FW_PRE,				\
	.ucode_api_max = IWL5150_UCODE_API_MAX,			\
	.ucode_api_ok = IWL5150_UCODE_API_OK,			\
	.ucode_api_min = IWL5150_UCODE_API_MIN,			\
	.device_family = IWL_DEVICE_FAMILY_5150,		\
	.max_inst_size = IWLAGN_RTC_INST_SIZE,			\
	.max_data_size = IWLAGN_RTC_DATA_SIZE,			\
	.eeprom_ver = EEPROM_5050_EEPROM_VERSION,		\
	.eeprom_calib_ver = EEPROM_5050_TX_POWER_VERSION,	\
	.base_params = &iwl5000_base_params,			\
	.eeprom_params = &iwl5000_eeprom_params,		\
	.no_xtal_calib = true,					\
	.led_mode = IWL_LED_BLINK,				\
	.internal_wimax_coex = true

const struct iwl_cfg iwl5150_agn_cfg = {
	.name = "Intel(R) WiMAX/WiFi Link 5150 AGN",
	IWL_DEVICE_5150,
	.ht_params = &iwl5000_ht_params,

};

const struct iwl_cfg iwl5150_abg_cfg = {
	.name = "Intel(R) WiMAX/WiFi Link 5150 ABG",
	IWL_DEVICE_5150,
};

MODULE_FIRMWARE(IWL5000_MODULE_FIRMWARE(IWL5000_UCODE_API_OK));
MODULE_FIRMWARE(IWL5150_MODULE_FIRMWARE(IWL5150_UCODE_API_OK));
