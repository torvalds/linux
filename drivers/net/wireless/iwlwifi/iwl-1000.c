/******************************************************************************
 *
 * Copyright(c) 2008-2009 Intel Corporation. All rights reserved.
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
#include "iwl-helpers.h"
#include "iwl-5000-hw.h"
#include "iwl-agn-led.h"

/* Highest firmware API version supported */
#define IWL1000_UCODE_API_MAX 3

/* Lowest firmware API version supported */
#define IWL1000_UCODE_API_MIN 1

#define IWL1000_FW_PRE "iwlwifi-1000-"
#define _IWL1000_MODULE_FIRMWARE(api) IWL1000_FW_PRE #api ".ucode"
#define IWL1000_MODULE_FIRMWARE(api) _IWL1000_MODULE_FIRMWARE(api)


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

static struct iwl_lib_ops iwl1000_lib = {
	.set_hw_params = iwl5000_hw_set_hw_params,
	.txq_update_byte_cnt_tbl = iwl5000_txq_update_byte_cnt_tbl,
	.txq_inval_byte_cnt_tbl = iwl5000_txq_inval_byte_cnt_tbl,
	.txq_set_sched = iwl5000_txq_set_sched,
	.txq_agg_enable = iwl5000_txq_agg_enable,
	.txq_agg_disable = iwl5000_txq_agg_disable,
	.txq_attach_buf_to_tfd = iwl_hw_txq_attach_buf_to_tfd,
	.txq_free_tfd = iwl_hw_txq_free_tfd,
	.txq_init = iwl_hw_tx_queue_init,
	.rx_handler_setup = iwl5000_rx_handler_setup,
	.setup_deferred_work = iwl5000_setup_deferred_work,
	.is_valid_rtc_data_addr = iwl5000_hw_valid_rtc_data_addr,
	.load_ucode = iwl5000_load_ucode,
	.dump_nic_event_log = iwl_dump_nic_event_log,
	.dump_nic_error_log = iwl_dump_nic_error_log,
	.init_alive_start = iwl5000_init_alive_start,
	.alive_notify = iwl5000_alive_notify,
	.send_tx_power = iwl5000_send_tx_power,
	.update_chain_flags = iwl_update_chain_flags,
	.apm_ops = {
		.init = iwl_apm_init,
		.stop = iwl_apm_stop,
		.config = iwl1000_nic_config,
		.set_pwr_src = iwl_set_pwr_src,
	},
	.eeprom_ops = {
		.regulatory_bands = {
			EEPROM_5000_REG_BAND_1_CHANNELS,
			EEPROM_5000_REG_BAND_2_CHANNELS,
			EEPROM_5000_REG_BAND_3_CHANNELS,
			EEPROM_5000_REG_BAND_4_CHANNELS,
			EEPROM_5000_REG_BAND_5_CHANNELS,
			EEPROM_5000_REG_BAND_24_HT40_CHANNELS,
			EEPROM_5000_REG_BAND_52_HT40_CHANNELS
		},
		.verify_signature  = iwlcore_eeprom_verify_signature,
		.acquire_semaphore = iwlcore_eeprom_acquire_semaphore,
		.release_semaphore = iwlcore_eeprom_release_semaphore,
		.calib_version	= iwl5000_eeprom_calib_version,
		.query_addr = iwl5000_eeprom_query_addr,
	},
	.post_associate = iwl_post_associate,
	.isr = iwl_isr_ict,
	.config_ap = iwl_config_ap,
	.temp_ops = {
		.temperature = iwl5000_temperature,
		.set_ct_kill = iwl1000_set_ct_threshold,
	 },
};

static struct iwl_ops iwl1000_ops = {
	.ucode = &iwl5000_ucode,
	.lib = &iwl1000_lib,
	.hcmd = &iwl5000_hcmd,
	.utils = &iwl5000_hcmd_utils,
	.led = &iwlagn_led_ops,
};

struct iwl_cfg iwl1000_bgn_cfg = {
	.name = "1000 Series BGN",
	.fw_name_pre = IWL1000_FW_PRE,
	.ucode_api_max = IWL1000_UCODE_API_MAX,
	.ucode_api_min = IWL1000_UCODE_API_MIN,
	.sku = IWL_SKU_G|IWL_SKU_N,
	.ops = &iwl1000_ops,
	.eeprom_size = OTP_LOW_IMAGE_SIZE,
	.eeprom_ver = EEPROM_1000_EEPROM_VERSION,
	.eeprom_calib_ver = EEPROM_5000_TX_POWER_VERSION,
	.num_of_queues = IWL50_NUM_QUEUES,
	.num_of_ampdu_queues = IWL50_NUM_AMPDU_QUEUES,
	.mod_params = &iwl50_mod_params,
	.valid_tx_ant = ANT_A,
	.valid_rx_ant = ANT_AB,
	.pll_cfg_val = CSR50_ANA_PLL_CFG_VAL,
	.set_l0s = true,
	.use_bsm = false,
	.max_ll_items = OTP_MAX_LL_ITEMS_1000,
	.shadow_ram_support = false,
	.ht_greenfield_support = true,
	.led_compensation = 51,
	.use_rts_for_ht = true, /* use rts/cts protection */
	.chain_noise_num_beacons = IWL_CAL_NUM_BEACONS,
	.support_ct_kill_exit = true,
	.sm_ps_mode = WLAN_HT_CAP_SM_PS_DISABLED,
};

struct iwl_cfg iwl1000_bg_cfg = {
	.name = "1000 Series BG",
	.fw_name_pre = IWL1000_FW_PRE,
	.ucode_api_max = IWL1000_UCODE_API_MAX,
	.ucode_api_min = IWL1000_UCODE_API_MIN,
	.sku = IWL_SKU_G,
	.ops = &iwl1000_ops,
	.eeprom_size = OTP_LOW_IMAGE_SIZE,
	.eeprom_ver = EEPROM_1000_EEPROM_VERSION,
	.eeprom_calib_ver = EEPROM_5000_TX_POWER_VERSION,
	.num_of_queues = IWL50_NUM_QUEUES,
	.num_of_ampdu_queues = IWL50_NUM_AMPDU_QUEUES,
	.mod_params = &iwl50_mod_params,
	.valid_tx_ant = ANT_A,
	.valid_rx_ant = ANT_AB,
	.pll_cfg_val = CSR50_ANA_PLL_CFG_VAL,
	.set_l0s = true,
	.use_bsm = false,
	.max_ll_items = OTP_MAX_LL_ITEMS_1000,
	.shadow_ram_support = false,
	.ht_greenfield_support = true,
	.led_compensation = 51,
	.chain_noise_num_beacons = IWL_CAL_NUM_BEACONS,
	.support_ct_kill_exit = true,
};

MODULE_FIRMWARE(IWL1000_MODULE_FIRMWARE(IWL1000_UCODE_API_MAX));
