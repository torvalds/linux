/******************************************************************************
 *
 * Copyright(c) 2008 - 2010 Intel Corporation. All rights reserved.
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
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/wireless.h>
#include <net/mac80211.h>
#include <linux/etherdevice.h>
#include <asm/unaligned.h>

#include "iwl-eeprom.h"
#include "iwl-dev.h"
#include "iwl-core.h"
#include "iwl-io.h"
#include "iwl-sta.h"
#include "iwl-agn.h"
#include "iwl-helpers.h"
#include "iwl-agn-hw.h"
#include "iwl-agn-led.h"
#include "iwl-agn-debugfs.h"

/* Highest firmware API version supported */
#define IWL1000_UCODE_API_MAX 3
#define IWL100_UCODE_API_MAX 5

/* Lowest firmware API version supported */
#define IWL1000_UCODE_API_MIN 1
#define IWL100_UCODE_API_MIN 5

#define IWL1000_FW_PRE "iwlwifi-1000-"
#define _IWL1000_MODULE_FIRMWARE(api) IWL1000_FW_PRE #api ".ucode"
#define IWL1000_MODULE_FIRMWARE(api) _IWL1000_MODULE_FIRMWARE(api)

#define IWL100_FW_PRE "iwlwifi-100-"
#define _IWL100_MODULE_FIRMWARE(api) IWL100_FW_PRE #api ".ucode"
#define IWL100_MODULE_FIRMWARE(api) _IWL100_MODULE_FIRMWARE(api)


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
	if (priv->cfg->mod_params->num_of_queues >= IWL_MIN_NUM_QUEUES &&
	    priv->cfg->mod_params->num_of_queues <= IWLAGN_NUM_QUEUES)
		priv->cfg->base_params->num_of_queues =
			priv->cfg->mod_params->num_of_queues;

	priv->hw_params.max_txq_num = priv->cfg->base_params->num_of_queues;
	priv->hw_params.dma_chnl_num = FH50_TCSR_CHNL_NUM;
	priv->hw_params.scd_bc_tbls_size =
			priv->cfg->base_params->num_of_queues *
			sizeof(struct iwlagn_scd_bc_tbl);
	priv->hw_params.tfd_size = sizeof(struct iwl_tfd);
	priv->hw_params.max_stations = IWLAGN_STATION_COUNT;
	priv->contexts[IWL_RXON_CTX_BSS].bcast_sta_id = IWLAGN_BROADCAST_ID;

	priv->hw_params.max_data_size = IWLAGN_RTC_DATA_SIZE;
	priv->hw_params.max_inst_size = IWLAGN_RTC_INST_SIZE;

	priv->hw_params.max_bsm_size = 0;
	priv->hw_params.ht40_channel =  BIT(IEEE80211_BAND_2GHZ) |
					BIT(IEEE80211_BAND_5GHZ);
	priv->hw_params.rx_wrt_ptr_reg = FH_RSCSR_CHNL0_WPTR;

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
	.txq_update_byte_cnt_tbl = iwlagn_txq_update_byte_cnt_tbl,
	.txq_inval_byte_cnt_tbl = iwlagn_txq_inval_byte_cnt_tbl,
	.txq_set_sched = iwlagn_txq_set_sched,
	.txq_agg_enable = iwlagn_txq_agg_enable,
	.txq_agg_disable = iwlagn_txq_agg_disable,
	.txq_attach_buf_to_tfd = iwl_hw_txq_attach_buf_to_tfd,
	.txq_free_tfd = iwl_hw_txq_free_tfd,
	.txq_init = iwl_hw_tx_queue_init,
	.rx_handler_setup = iwlagn_rx_handler_setup,
	.setup_deferred_work = iwlagn_setup_deferred_work,
	.is_valid_rtc_data_addr = iwlagn_hw_valid_rtc_data_addr,
	.load_ucode = iwlagn_load_ucode,
	.dump_nic_event_log = iwl_dump_nic_event_log,
	.dump_nic_error_log = iwl_dump_nic_error_log,
	.dump_csr = iwl_dump_csr,
	.dump_fh = iwl_dump_fh,
	.init_alive_start = iwlagn_init_alive_start,
	.alive_notify = iwlagn_alive_notify,
	.send_tx_power = iwlagn_send_tx_power,
	.update_chain_flags = iwl_update_chain_flags,
	.apm_ops = {
		.init = iwl_apm_init,
		.config = iwl1000_nic_config,
	},
	.eeprom_ops = {
		.regulatory_bands = {
			EEPROM_REG_BAND_1_CHANNELS,
			EEPROM_REG_BAND_2_CHANNELS,
			EEPROM_REG_BAND_3_CHANNELS,
			EEPROM_REG_BAND_4_CHANNELS,
			EEPROM_REG_BAND_5_CHANNELS,
			EEPROM_REG_BAND_24_HT40_CHANNELS,
			EEPROM_REG_BAND_52_HT40_CHANNELS
		},
		.acquire_semaphore = iwlcore_eeprom_acquire_semaphore,
		.release_semaphore = iwlcore_eeprom_release_semaphore,
		.calib_version	= iwlagn_eeprom_calib_version,
		.query_addr = iwlagn_eeprom_query_addr,
	},
	.isr_ops = {
		.isr = iwl_isr_ict,
		.free = iwl_free_isr_ict,
		.alloc = iwl_alloc_isr_ict,
		.reset = iwl_reset_ict,
		.disable = iwl_disable_ict,
	},
	.temp_ops = {
		.temperature = iwlagn_temperature,
	 },
	.debugfs_ops = {
		.rx_stats_read = iwl_ucode_rx_stats_read,
		.tx_stats_read = iwl_ucode_tx_stats_read,
		.general_stats_read = iwl_ucode_general_stats_read,
		.bt_stats_read = iwl_ucode_bt_stats_read,
		.reply_tx_error = iwl_reply_tx_error_read,
	},
	.check_plcp_health = iwl_good_plcp_health,
	.check_ack_health = iwl_good_ack_health,
	.txfifo_flush = iwlagn_txfifo_flush,
	.dev_txfifo_flush = iwlagn_dev_txfifo_flush,
	.tt_ops = {
		.lower_power_detection = iwl_tt_is_low_power_state,
		.tt_power_mode = iwl_tt_current_power_mode,
		.ct_kill_check = iwl_check_for_ct_kill,
	}
};

static const struct iwl_ops iwl1000_ops = {
	.lib = &iwl1000_lib,
	.hcmd = &iwlagn_hcmd,
	.utils = &iwlagn_hcmd_utils,
	.led = &iwlagn_led_ops,
	.ieee80211_ops = &iwlagn_hw_ops,
};

static struct iwl_base_params iwl1000_base_params = {
	.num_of_queues = IWLAGN_NUM_QUEUES,
	.num_of_ampdu_queues = IWLAGN_NUM_AMPDU_QUEUES,
	.eeprom_size = OTP_LOW_IMAGE_SIZE,
	.pll_cfg_val = CSR50_ANA_PLL_CFG_VAL,
	.set_l0s = true,
	.use_bsm = false,
	.max_ll_items = OTP_MAX_LL_ITEMS_1000,
	.shadow_ram_support = false,
	.led_compensation = 51,
	.chain_noise_num_beacons = IWL_CAL_NUM_BEACONS,
	.support_ct_kill_exit = true,
	.plcp_delta_threshold = IWL_MAX_PLCP_ERR_EXT_LONG_THRESHOLD_DEF,
	.chain_noise_scale = 1000,
	.wd_timeout = IWL_DEF_WD_TIMEOUT,
	.max_event_log_size = 128,
	.ucode_tracing = true,
	.sensitivity_calib_by_driver = true,
	.chain_noise_calib_by_driver = true,
};
static struct iwl_ht_params iwl1000_ht_params = {
	.ht_greenfield_support = true,
	.use_rts_for_aggregation = true, /* use rts/cts protection */
};

struct iwl_cfg iwl1000_bgn_cfg = {
	.name = "Intel(R) Centrino(R) Wireless-N 1000 BGN",
	.fw_name_pre = IWL1000_FW_PRE,
	.ucode_api_max = IWL1000_UCODE_API_MAX,
	.ucode_api_min = IWL1000_UCODE_API_MIN,
	.eeprom_ver = EEPROM_1000_EEPROM_VERSION,
	.eeprom_calib_ver = EEPROM_1000_TX_POWER_VERSION,
	.ops = &iwl1000_ops,
	.mod_params = &iwlagn_mod_params,
	.base_params = &iwl1000_base_params,
	.ht_params = &iwl1000_ht_params,
	.led_mode = IWL_LED_BLINK,
};

struct iwl_cfg iwl1000_bg_cfg = {
	.name = "Intel(R) Centrino(R) Wireless-N 1000 BG",
	.fw_name_pre = IWL1000_FW_PRE,
	.ucode_api_max = IWL1000_UCODE_API_MAX,
	.ucode_api_min = IWL1000_UCODE_API_MIN,
	.eeprom_ver = EEPROM_1000_EEPROM_VERSION,
	.eeprom_calib_ver = EEPROM_1000_TX_POWER_VERSION,
	.ops = &iwl1000_ops,
	.mod_params = &iwlagn_mod_params,
	.base_params = &iwl1000_base_params,
	.led_mode = IWL_LED_BLINK,
};

struct iwl_cfg iwl100_bgn_cfg = {
	.name = "Intel(R) Centrino(R) Wireless-N 100 BGN",
	.fw_name_pre = IWL100_FW_PRE,
	.ucode_api_max = IWL100_UCODE_API_MAX,
	.ucode_api_min = IWL100_UCODE_API_MIN,
	.eeprom_ver = EEPROM_1000_EEPROM_VERSION,
	.eeprom_calib_ver = EEPROM_1000_TX_POWER_VERSION,
	.ops = &iwl1000_ops,
	.mod_params = &iwlagn_mod_params,
	.base_params = &iwl1000_base_params,
	.ht_params = &iwl1000_ht_params,
	.led_mode = IWL_LED_RF_STATE,
	.rx_with_siso_diversity = true,
};

struct iwl_cfg iwl100_bg_cfg = {
	.name = "Intel(R) Centrino(R) Wireless-N 100 BG",
	.fw_name_pre = IWL100_FW_PRE,
	.ucode_api_max = IWL100_UCODE_API_MAX,
	.ucode_api_min = IWL100_UCODE_API_MIN,
	.eeprom_ver = EEPROM_1000_EEPROM_VERSION,
	.eeprom_calib_ver = EEPROM_1000_TX_POWER_VERSION,
	.ops = &iwl1000_ops,
	.mod_params = &iwlagn_mod_params,
	.base_params = &iwl1000_base_params,
	.led_mode = IWL_LED_RF_STATE,
	.rx_with_siso_diversity = true,
};

MODULE_FIRMWARE(IWL1000_MODULE_FIRMWARE(IWL1000_UCODE_API_MAX));
MODULE_FIRMWARE(IWL100_MODULE_FIRMWARE(IWL100_UCODE_API_MAX));
