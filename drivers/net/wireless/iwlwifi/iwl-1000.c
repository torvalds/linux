/******************************************************************************
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/wireless.h>
#include <net/mac80211.h>
#include <linux/etherdevice.h>
#include <asm/unaligned.h>
#include <linux/stringify.h>

#include "iwl-eeprom.h"
#include "iwl-dev.h"
#include "iwl-core.h"
#include "iwl-io.h"
#include "iwl-sta.h"
#include "iwl-agn.h"
#include "iwl-helpers.h"
#include "iwl-agn-hw.h"

/* Highest firmware API version supported */
#define IWL1000_UCODE_API_MAX 5
#define IWL100_UCODE_API_MAX 5

/* Lowest firmware API version supported */
#define IWL1000_UCODE_API_MIN 1
#define IWL100_UCODE_API_MIN 5

#define IWL1000_FW_PRE "iwlwifi-1000-"
#define IWL1000_MODULE_FIRMWARE(api) IWL1000_FW_PRE __stringify(api) ".ucode"

#define IWL100_FW_PRE "iwlwifi-100-"
#define IWL100_MODULE_FIRMWARE(api) IWL100_FW_PRE __stringify(api) ".ucode"


/*
 * For 1000, use advance thermal throttling critical temperature threshold,
 * but legacy thermal management implementation for now.
 * This is for the reason of 1000 uCode using advance thermal throttling API
 * but not implement ct_kill_exit based on ct_kill exit temperature
 * so the thermal throttling will still based on legacy thermal throttling
 * management.
 * The code here need to be modified once 1000 uCode has the advanced thermal
 * throttling algorithm in place
 */
static void iwl1000_set_ct_threshold(struct iwl_priv *priv)
{
	/* want Celsius */
	priv->hw_params.ct_kill_threshold = CT_KILL_THRESHOLD_LEGACY;
	priv->hw_params.ct_kill_exit_threshold = CT_KILL_EXIT_THRESHOLD;
}

/* NIC configuration for 1000 series */
static void iwl1000_nic_config(struct iwl_priv *priv)
{
	/* set CSR_HW_CONFIG_REG for uCode use */
	iwl_set_bit(priv, CSR_HW_IF_CONFIG_REG,
		    CSR_HW_IF_CONFIG_REG_BIT_RADIO_SI |
		    CSR_HW_IF_CONFIG_REG_BIT_MAC_SI);

	/* Setting digital SVR for 1000 card to 1.32V */
	/* locking is acquired in iwl_set_bits_mask_prph() function */
	iwl_set_bits_mask_prph(priv, APMG_DIGITAL_SVR_REG,
				APMG_SVR_DIGITAL_VOLTAGE_1_32,
				~APMG_SVR_VOLTAGE_CONFIG_BIT_MSK);
}

static struct iwl_sensitivity_ranges iwl1000_sensitivity = {
	.min_nrg_cck = 95,
	.max_nrg_cck = 0, /* not used, set to 0 */
	.auto_corr_min_ofdm = 90,
	.auto_corr_min_ofdm_mrc = 170,
	.auto_corr_min_ofdm_x1 = 120,
	.auto_corr_min_ofdm_mrc_x1 = 240,

	.auto_corr_max_ofdm = 120,
	.auto_corr_max_ofdm_mrc = 210,
	.auto_corr_max_ofdm_x1 = 155,
	.auto_corr_max_ofdm_mrc_x1 = 290,

	.auto_corr_min_cck = 125,
	.auto_corr_max_cck = 200,
	.auto_corr_min_cck_mrc = 170,
	.auto_corr_max_cck_mrc = 400,
	.nrg_th_cck = 95,
	.nrg_th_ofdm = 95,

	.barker_corr_th_min = 190,
	.barker_corr_th_min_mrc = 390,
	.nrg_th_cca = 62,
};

static int iwl1000_hw_set_hw_params(struct iwl_priv *priv)
{
	if (iwlagn_mod_params.num_of_queues >= IWL_MIN_NUM_QUEUES &&
	    iwlagn_mod_params.num_of_queues <= IWLAGN_NUM_QUEUES)
		priv->cfg->base_params->num_of_queues =
			iwlagn_mod_params.num_of_queues;

	priv->hw_params.max_txq_num = priv->cfg->base_params->num_of_queues;
	priv->hw_params.scd_bc_tbls_size =
			priv->cfg->base_params->num_of_queues *
			sizeof(struct iwlagn_scd_bc_tbl);
	priv->hw_params.tfd_size = sizeof(struct iwl_tfd);
	priv->hw_params.max_stations = IWLAGN_STATION_COUNT;
	priv->contexts[IWL_RXON_CTX_BSS].bcast_sta_id = IWLAGN_BROADCAST_ID;

	priv->hw_params.max_data_size = IWLAGN_RTC_DATA_SIZE;
	priv->hw_params.max_inst_size = IWLAGN_RTC_INST_SIZE;

	priv->hw_params.ht40_channel =  BIT(IEEE80211_BAND_2GHZ) |
					BIT(IEEE80211_BAND_5GHZ);

	priv->hw_params.tx_chains_num = num_of_ant(priv->cfg->valid_tx_ant);
	if (priv->cfg->rx_with_siso_diversity)
		priv->hw_params.rx_chains_num = 1;
	else
		priv->hw_params.rx_chains_num =
			num_of_ant(priv->cfg->valid_rx_ant);
	priv->hw_params.valid_tx_ant = priv->cfg->valid_tx_ant;
	priv->hw_params.valid_rx_ant = priv->cfg->valid_rx_ant;

	iwl1000_set_ct_threshold(priv);

	/* Set initial sensitivity parameters */
	/* Set initial calibration set */
	priv->hw_params.sens = &iwl1000_sensitivity;
	priv->hw_params.calib_init_cfg =
			BIT(IWL_CALIB_XTAL)		|
			BIT(IWL_CALIB_LO)		|
			BIT(IWL_CALIB_TX_IQ) 		|
			BIT(IWL_CALIB_TX_IQ_PERD)	|
			BIT(IWL_CALIB_BASE_BAND);
	if (priv->cfg->need_dc_calib)
		priv->hw_params.calib_init_cfg |= BIT(IWL_CALIB_DC);

	priv->hw_params.beacon_time_tsf_bits = IWLAGN_EXT_BEACON_TIME_POS;

	return 0;
}

static struct iwl_lib_ops iwl1000_lib = {
	.set_hw_params = iwl1000_hw_set_hw_params,
	.nic_config = iwl1000_nic_config,
	.eeprom_ops = {
		.regulatory_bands = {
			EEPROM_REG_BAND_1_CHANNELS,
			EEPROM_REG_BAND_2_CHANNELS,
			EEPROM_REG_BAND_3_CHANNELS,
			EEPROM_REG_BAND_4_CHANNELS,
			EEPROM_REG_BAND_5_CHANNELS,
			EEPROM_REG_BAND_24_HT40_CHANNELS,
			EEPROM_REGULATORY_BAND_NO_HT40,
		},
	},
	.temperature = iwlagn_temperature,
};

static struct iwl_base_params iwl1000_base_params = {
	.num_of_queues = IWLAGN_NUM_QUEUES,
	.num_of_ampdu_queues = IWLAGN_NUM_AMPDU_QUEUES,
	.eeprom_size = OTP_LOW_IMAGE_SIZE,
	.pll_cfg_val = CSR50_ANA_PLL_CFG_VAL,
	.max_ll_items = OTP_MAX_LL_ITEMS_1000,
	.shadow_ram_support = false,
	.led_compensation = 51,
	.chain_noise_num_beacons = IWL_CAL_NUM_BEACONS,
	.support_ct_kill_exit = true,
	.plcp_delta_threshold = IWL_MAX_PLCP_ERR_EXT_LONG_THRESHOLD_DEF,
	.chain_noise_scale = 1000,
	.wd_timeout = IWL_DEF_WD_TIMEOUT,
	.max_event_log_size = 128,
};
static struct iwl_ht_params iwl1000_ht_params = {
	.ht_greenfield_support = true,
	.use_rts_for_aggregation = true, /* use rts/cts protection */
	.smps_mode = IEEE80211_SMPS_STATIC,
};

#define IWL_DEVICE_1000						\
	.fw_name_pre = IWL1000_FW_PRE,				\
	.ucode_api_max = IWL1000_UCODE_API_MAX,			\
	.ucode_api_min = IWL1000_UCODE_API_MIN,			\
	.eeprom_ver = EEPROM_1000_EEPROM_VERSION,		\
	.eeprom_calib_ver = EEPROM_1000_TX_POWER_VERSION,	\
	.lib = &iwl1000_lib,					\
	.base_params = &iwl1000_base_params,			\
	.led_mode = IWL_LED_BLINK

struct iwl_cfg iwl1000_bgn_cfg = {
	.name = "Intel(R) Centrino(R) Wireless-N 1000 BGN",
	IWL_DEVICE_1000,
	.ht_params = &iwl1000_ht_params,
};

struct iwl_cfg iwl1000_bg_cfg = {
	.name = "Intel(R) Centrino(R) Wireless-N 1000 BG",
	IWL_DEVICE_1000,
};

#define IWL_DEVICE_100						\
	.fw_name_pre = IWL100_FW_PRE,				\
	.ucode_api_max = IWL100_UCODE_API_MAX,			\
	.ucode_api_min = IWL100_UCODE_API_MIN,			\
	.eeprom_ver = EEPROM_1000_EEPROM_VERSION,		\
	.eeprom_calib_ver = EEPROM_1000_TX_POWER_VERSION,	\
	.lib = &iwl1000_lib,					\
	.base_params = &iwl1000_base_params,			\
	.led_mode = IWL_LED_RF_STATE,				\
	.rx_with_siso_diversity = true

struct iwl_cfg iwl100_bgn_cfg = {
	.name = "Intel(R) Centrino(R) Wireless-N 100 BGN",
	IWL_DEVICE_100,
	.ht_params = &iwl1000_ht_params,
};

struct iwl_cfg iwl100_bg_cfg = {
	.name = "Intel(R) Centrino(R) Wireless-N 100 BG",
	IWL_DEVICE_100,
};

MODULE_FIRMWARE(IWL1000_MODULE_FIRMWARE(IWL1000_UCODE_API_MAX));
MODULE_FIRMWARE(IWL100_MODULE_FIRMWARE(IWL100_UCODE_API_MAX));
