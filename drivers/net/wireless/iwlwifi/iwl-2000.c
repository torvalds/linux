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
#include <net/mac80211.h>
#include <linux/etherdevice.h>
#include <asm/unaligned.h>
#include <linux/stringify.h>

#include "iwl-eeprom.h"
#include "iwl-dev.h"
#include "iwl-core.h"
#include "iwl-io.h"
#include "iwl-agn.h"
#include "iwl-agn-hw.h"
#include "iwl-shared.h"
#include "iwl-cfg.h"

/* Highest firmware API version supported */
#define IWL2030_UCODE_API_MAX 6
#define IWL2000_UCODE_API_MAX 6
#define IWL105_UCODE_API_MAX 6
#define IWL135_UCODE_API_MAX 6

/* Oldest version we won't warn about */
#define IWL2030_UCODE_API_OK 5
#define IWL2000_UCODE_API_OK 5
#define IWL105_UCODE_API_OK 5
#define IWL135_UCODE_API_OK 5

/* Lowest firmware API version supported */
#define IWL2030_UCODE_API_MIN 5
#define IWL2000_UCODE_API_MIN 5
#define IWL105_UCODE_API_MIN 5
#define IWL135_UCODE_API_MIN 5

#define IWL2030_FW_PRE "iwlwifi-2030-"
#define IWL2030_MODULE_FIRMWARE(api) IWL2030_FW_PRE __stringify(api) ".ucode"

#define IWL2000_FW_PRE "iwlwifi-2000-"
#define IWL2000_MODULE_FIRMWARE(api) IWL2000_FW_PRE __stringify(api) ".ucode"

#define IWL105_FW_PRE "iwlwifi-105-"
#define IWL105_MODULE_FIRMWARE(api) IWL105_FW_PRE __stringify(api) ".ucode"

#define IWL135_FW_PRE "iwlwifi-135-"
#define IWL135_MODULE_FIRMWARE(api) IWL135_FW_PRE __stringify(api) ".ucode"

static void iwl2000_set_ct_threshold(struct iwl_priv *priv)
{
	/* want Celsius */
	hw_params(priv).ct_kill_threshold = CT_KILL_THRESHOLD;
	hw_params(priv).ct_kill_exit_threshold = CT_KILL_EXIT_THRESHOLD;
}

/* NIC configuration for 2000 series */
static void iwl2000_nic_config(struct iwl_priv *priv)
{
	iwl_rf_config(priv);

	if (priv->cfg->iq_invert)
		iwl_set_bit(bus(priv), CSR_GP_DRIVER_REG,
			    CSR_GP_DRIVER_REG_BIT_RADIO_IQ_INVER);
}

static struct iwl_sensitivity_ranges iwl2000_sensitivity = {
	.min_nrg_cck = 97,
	.max_nrg_cck = 0, /* not used, set to 0 */
	.auto_corr_min_ofdm = 80,
	.auto_corr_min_ofdm_mrc = 128,
	.auto_corr_min_ofdm_x1 = 105,
	.auto_corr_min_ofdm_mrc_x1 = 192,

	.auto_corr_max_ofdm = 145,
	.auto_corr_max_ofdm_mrc = 232,
	.auto_corr_max_ofdm_x1 = 110,
	.auto_corr_max_ofdm_mrc_x1 = 232,

	.auto_corr_min_cck = 125,
	.auto_corr_max_cck = 175,
	.auto_corr_min_cck_mrc = 160,
	.auto_corr_max_cck_mrc = 310,
	.nrg_th_cck = 97,
	.nrg_th_ofdm = 100,

	.barker_corr_th_min = 190,
	.barker_corr_th_min_mrc = 390,
	.nrg_th_cca = 62,
};

static int iwl2000_hw_set_hw_params(struct iwl_priv *priv)
{
	if (iwlagn_mod_params.num_of_queues >= IWL_MIN_NUM_QUEUES &&
	    iwlagn_mod_params.num_of_queues <= IWLAGN_NUM_QUEUES)
		priv->cfg->base_params->num_of_queues =
			iwlagn_mod_params.num_of_queues;

	hw_params(priv).max_txq_num = priv->cfg->base_params->num_of_queues;
	priv->contexts[IWL_RXON_CTX_BSS].bcast_sta_id = IWLAGN_BROADCAST_ID;

	hw_params(priv).max_data_size = IWL60_RTC_DATA_SIZE;
	hw_params(priv).max_inst_size = IWL60_RTC_INST_SIZE;

	hw_params(priv).ht40_channel =  BIT(IEEE80211_BAND_2GHZ);

	hw_params(priv).tx_chains_num = num_of_ant(priv->cfg->valid_tx_ant);
	if (priv->cfg->rx_with_siso_diversity)
		hw_params(priv).rx_chains_num = 1;
	else
		hw_params(priv).rx_chains_num =
			num_of_ant(priv->cfg->valid_rx_ant);
	hw_params(priv).valid_tx_ant = priv->cfg->valid_tx_ant;
	hw_params(priv).valid_rx_ant = priv->cfg->valid_rx_ant;

	iwl2000_set_ct_threshold(priv);

	/* Set initial sensitivity parameters */
	/* Set initial calibration set */
	hw_params(priv).sens = &iwl2000_sensitivity;
	hw_params(priv).calib_init_cfg =
		BIT(IWL_CALIB_XTAL)             |
		BIT(IWL_CALIB_LO)               |
		BIT(IWL_CALIB_TX_IQ)            |
		BIT(IWL_CALIB_BASE_BAND);
	if (priv->cfg->need_dc_calib)
		hw_params(priv).calib_rt_cfg |= IWL_CALIB_CFG_DC_IDX;
	if (priv->cfg->need_temp_offset_calib)
		hw_params(priv).calib_init_cfg |= BIT(IWL_CALIB_TEMP_OFFSET);

	return 0;
}

static struct iwl_lib_ops iwl2000_lib = {
	.set_hw_params = iwl2000_hw_set_hw_params,
	.nic_config = iwl2000_nic_config,
	.eeprom_ops = {
		.regulatory_bands = {
			EEPROM_REG_BAND_1_CHANNELS,
			EEPROM_REG_BAND_2_CHANNELS,
			EEPROM_REG_BAND_3_CHANNELS,
			EEPROM_REG_BAND_4_CHANNELS,
			EEPROM_REG_BAND_5_CHANNELS,
			EEPROM_6000_REG_BAND_24_HT40_CHANNELS,
			EEPROM_REGULATORY_BAND_NO_HT40,
		},
		.update_enhanced_txpower = iwl_eeprom_enhanced_txpower,
	},
	.temperature = iwlagn_temperature,
};

static struct iwl_lib_ops iwl2030_lib = {
	.set_hw_params = iwl2000_hw_set_hw_params,
	.bt_rx_handler_setup = iwlagn_bt_rx_handler_setup,
	.bt_setup_deferred_work = iwlagn_bt_setup_deferred_work,
	.cancel_deferred_work = iwlagn_bt_cancel_deferred_work,
	.nic_config = iwl2000_nic_config,
	.eeprom_ops = {
		.regulatory_bands = {
			EEPROM_REG_BAND_1_CHANNELS,
			EEPROM_REG_BAND_2_CHANNELS,
			EEPROM_REG_BAND_3_CHANNELS,
			EEPROM_REG_BAND_4_CHANNELS,
			EEPROM_REG_BAND_5_CHANNELS,
			EEPROM_6000_REG_BAND_24_HT40_CHANNELS,
			EEPROM_REGULATORY_BAND_NO_HT40,
		},
		.update_enhanced_txpower = iwl_eeprom_enhanced_txpower,
	},
	.temperature = iwlagn_temperature,
};

static struct iwl_base_params iwl2000_base_params = {
	.eeprom_size = OTP_LOW_IMAGE_SIZE,
	.num_of_queues = IWLAGN_NUM_QUEUES,
	.num_of_ampdu_queues = IWLAGN_NUM_AMPDU_QUEUES,
	.pll_cfg_val = 0,
	.max_ll_items = OTP_MAX_LL_ITEMS_2x00,
	.shadow_ram_support = true,
	.led_compensation = 51,
	.adv_thermal_throttle = true,
	.support_ct_kill_exit = true,
	.plcp_delta_threshold = IWL_MAX_PLCP_ERR_THRESHOLD_DEF,
	.chain_noise_scale = 1000,
	.wd_timeout = IWL_DEF_WD_TIMEOUT,
	.max_event_log_size = 512,
	.shadow_reg_enable = true,
	.hd_v2 = true,
};


static struct iwl_base_params iwl2030_base_params = {
	.eeprom_size = OTP_LOW_IMAGE_SIZE,
	.num_of_queues = IWLAGN_NUM_QUEUES,
	.num_of_ampdu_queues = IWLAGN_NUM_AMPDU_QUEUES,
	.pll_cfg_val = 0,
	.max_ll_items = OTP_MAX_LL_ITEMS_2x00,
	.shadow_ram_support = true,
	.led_compensation = 57,
	.adv_thermal_throttle = true,
	.support_ct_kill_exit = true,
	.plcp_delta_threshold = IWL_MAX_PLCP_ERR_THRESHOLD_DEF,
	.chain_noise_scale = 1000,
	.wd_timeout = IWL_LONG_WD_TIMEOUT,
	.max_event_log_size = 512,
	.shadow_reg_enable = true,
	.hd_v2 = true,
};

static struct iwl_ht_params iwl2000_ht_params = {
	.ht_greenfield_support = true,
	.use_rts_for_aggregation = true, /* use rts/cts protection */
};

static struct iwl_bt_params iwl2030_bt_params = {
	/* Due to bluetooth, we transmit 2.4 GHz probes only on antenna A */
	.advanced_bt_coexist = true,
	.agg_time_limit = BT_AGG_THRESHOLD_DEF,
	.bt_init_traffic_load = IWL_BT_COEX_TRAFFIC_LOAD_NONE,
	.bt_prio_boost = IWLAGN_BT_PRIO_BOOST_DEFAULT,
	.bt_sco_disable = true,
	.bt_session_2 = true,
};

#define IWL_DEVICE_2000						\
	.fw_name_pre = IWL2000_FW_PRE,				\
	.ucode_api_max = IWL2000_UCODE_API_MAX,			\
	.ucode_api_ok = IWL2000_UCODE_API_OK,			\
	.ucode_api_min = IWL2000_UCODE_API_MIN,			\
	.eeprom_ver = EEPROM_2000_EEPROM_VERSION,		\
	.eeprom_calib_ver = EEPROM_2000_TX_POWER_VERSION,	\
	.lib = &iwl2000_lib,					\
	.base_params = &iwl2000_base_params,			\
	.need_dc_calib = true,					\
	.need_temp_offset_calib = true,				\
	.temp_offset_v2 = true,					\
	.led_mode = IWL_LED_RF_STATE,				\
	.iq_invert = true					\

struct iwl_cfg iwl2000_2bgn_cfg = {
	.name = "2000 Series 2x2 BGN",
	IWL_DEVICE_2000,
	.ht_params = &iwl2000_ht_params,
};

struct iwl_cfg iwl2000_2bgn_d_cfg = {
	.name = "2000D Series 2x2 BGN",
	IWL_DEVICE_2000,
	.ht_params = &iwl2000_ht_params,
};

#define IWL_DEVICE_2030						\
	.fw_name_pre = IWL2030_FW_PRE,				\
	.ucode_api_max = IWL2030_UCODE_API_MAX,			\
	.ucode_api_ok = IWL2030_UCODE_API_OK,			\
	.ucode_api_min = IWL2030_UCODE_API_MIN,			\
	.eeprom_ver = EEPROM_2000_EEPROM_VERSION,		\
	.eeprom_calib_ver = EEPROM_2000_TX_POWER_VERSION,	\
	.lib = &iwl2030_lib,					\
	.base_params = &iwl2030_base_params,			\
	.bt_params = &iwl2030_bt_params,			\
	.need_dc_calib = true,					\
	.need_temp_offset_calib = true,				\
	.temp_offset_v2 = true,					\
	.led_mode = IWL_LED_RF_STATE,				\
	.adv_pm = true,						\
	.iq_invert = true					\

struct iwl_cfg iwl2030_2bgn_cfg = {
	.name = "2000 Series 2x2 BGN/BT",
	IWL_DEVICE_2030,
	.ht_params = &iwl2000_ht_params,
};

#define IWL_DEVICE_105						\
	.fw_name_pre = IWL105_FW_PRE,				\
	.ucode_api_max = IWL105_UCODE_API_MAX,			\
	.ucode_api_ok = IWL105_UCODE_API_OK,			\
	.ucode_api_min = IWL105_UCODE_API_MIN,			\
	.eeprom_ver = EEPROM_2000_EEPROM_VERSION,		\
	.eeprom_calib_ver = EEPROM_2000_TX_POWER_VERSION,	\
	.lib = &iwl2000_lib,					\
	.base_params = &iwl2000_base_params,			\
	.need_dc_calib = true,					\
	.need_temp_offset_calib = true,				\
	.temp_offset_v2 = true,					\
	.led_mode = IWL_LED_RF_STATE,				\
	.adv_pm = true,						\
	.rx_with_siso_diversity = true,				\
	.iq_invert = true					\

struct iwl_cfg iwl105_bgn_cfg = {
	.name = "105 Series 1x1 BGN",
	IWL_DEVICE_105,
	.ht_params = &iwl2000_ht_params,
};

struct iwl_cfg iwl105_bgn_d_cfg = {
	.name = "105D Series 1x1 BGN",
	IWL_DEVICE_105,
	.ht_params = &iwl2000_ht_params,
};

#define IWL_DEVICE_135						\
	.fw_name_pre = IWL135_FW_PRE,				\
	.ucode_api_max = IWL135_UCODE_API_MAX,			\
	.ucode_api_ok = IWL135_UCODE_API_OK,			\
	.ucode_api_min = IWL135_UCODE_API_MIN,			\
	.eeprom_ver = EEPROM_2000_EEPROM_VERSION,		\
	.eeprom_calib_ver = EEPROM_2000_TX_POWER_VERSION,	\
	.lib = &iwl2030_lib,					\
	.base_params = &iwl2030_base_params,			\
	.bt_params = &iwl2030_bt_params,			\
	.need_dc_calib = true,					\
	.need_temp_offset_calib = true,				\
	.temp_offset_v2 = true,					\
	.led_mode = IWL_LED_RF_STATE,				\
	.adv_pm = true,						\
	.rx_with_siso_diversity = true,				\
	.iq_invert = true					\

struct iwl_cfg iwl135_bgn_cfg = {
	.name = "135 Series 1x1 BGN/BT",
	IWL_DEVICE_135,
	.ht_params = &iwl2000_ht_params,
};

MODULE_FIRMWARE(IWL2000_MODULE_FIRMWARE(IWL2000_UCODE_API_MAX));
MODULE_FIRMWARE(IWL2030_MODULE_FIRMWARE(IWL2030_UCODE_API_MAX));
MODULE_FIRMWARE(IWL105_MODULE_FIRMWARE(IWL105_UCODE_API_MAX));
MODULE_FIRMWARE(IWL135_MODULE_FIRMWARE(IWL135_UCODE_API_MAX));
