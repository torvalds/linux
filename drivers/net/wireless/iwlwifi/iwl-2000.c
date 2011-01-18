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
#include "iwl-6000-hw.h"
#include "iwl-agn-led.h"
#include "iwl-agn-debugfs.h"

/* Highest firmware API version supported */
#define IWL2030_UCODE_API_MAX 5
#define IWL2000_UCODE_API_MAX 5
#define IWL200_UCODE_API_MAX 5

/* Lowest firmware API version supported */
#define IWL2030_UCODE_API_MIN 5
#define IWL2000_UCODE_API_MIN 5
#define IWL200_UCODE_API_MIN 5

#define IWL2030_FW_PRE "iwlwifi-2030-"
#define _IWL2030_MODULE_FIRMWARE(api) IWL2030_FW_PRE #api ".ucode"
#define IWL2030_MODULE_FIRMWARE(api) _IWL2030_MODULE_FIRMWARE(api)

#define IWL2000_FW_PRE "iwlwifi-2000-"
#define _IWL2000_MODULE_FIRMWARE(api) IWL2000_FW_PRE #api ".ucode"
#define IWL2000_MODULE_FIRMWARE(api) _IWL2000_MODULE_FIRMWARE(api)

#define IWL200_FW_PRE "iwlwifi-200-"
#define _IWL200_MODULE_FIRMWARE(api) IWL200_FW_PRE #api ".ucode"
#define IWL200_MODULE_FIRMWARE(api) _IWL200_MODULE_FIRMWARE(api)

static void iwl2000_set_ct_threshold(struct iwl_priv *priv)
{
	/* want Celsius */
	priv->hw_params.ct_kill_threshold = CT_KILL_THRESHOLD;
	priv->hw_params.ct_kill_exit_threshold = CT_KILL_EXIT_THRESHOLD;
}

/* NIC configuration for 2000 series */
static void iwl2000_nic_config(struct iwl_priv *priv)
{
	u16 radio_cfg;

	radio_cfg = iwl_eeprom_query16(priv, EEPROM_RADIO_CONFIG);

	/* write radio config values to register */
	if (EEPROM_RF_CFG_TYPE_MSK(radio_cfg) <= EEPROM_RF_CONFIG_TYPE_MAX)
	iwl_set_bit(priv, CSR_HW_IF_CONFIG_REG,
			EEPROM_RF_CFG_TYPE_MSK(radio_cfg) |
			EEPROM_RF_CFG_STEP_MSK(radio_cfg) |
			EEPROM_RF_CFG_DASH_MSK(radio_cfg));

	/* set CSR_HW_CONFIG_REG for uCode use */
	iwl_set_bit(priv, CSR_HW_IF_CONFIG_REG,
		    CSR_HW_IF_CONFIG_REG_BIT_RADIO_SI |
		    CSR_HW_IF_CONFIG_REG_BIT_MAC_SI);

	if (priv->cfg->iq_invert)
		iwl_set_bit(priv, CSR_GP_DRIVER_REG,
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

	priv->hw_params.max_data_size = IWL60_RTC_DATA_SIZE;
	priv->hw_params.max_inst_size = IWL60_RTC_INST_SIZE;

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

	iwl2000_set_ct_threshold(priv);

	/* Set initial sensitivity parameters */
	/* Set initial calibration set */
	priv->hw_params.sens = &iwl2000_sensitivity;
	priv->hw_params.calib_init_cfg =
		BIT(IWL_CALIB_XTAL)             |
		BIT(IWL_CALIB_LO)               |
		BIT(IWL_CALIB_TX_IQ)            |
		BIT(IWL_CALIB_BASE_BAND);
	if (priv->cfg->need_dc_calib)
		priv->hw_params.calib_rt_cfg |= BIT(IWL_CALIB_CFG_DC_IDX);
	if (priv->cfg->need_temp_offset_calib)
		priv->hw_params.calib_init_cfg |= BIT(IWL_CALIB_TEMP_OFFSET);

	priv->hw_params.beacon_time_tsf_bits = IWLAGN_EXT_BEACON_TIME_POS;

	return 0;
}

static int iwl2030_hw_channel_switch(struct iwl_priv *priv,
                                    struct ieee80211_channel_switch *ch_switch)
{
	/*
	 * MULTI-FIXME
	 * See iwl_mac_channel_switch.
	 */
	struct iwl_rxon_context *ctx = &priv->contexts[IWL_RXON_CTX_BSS];
	struct iwl6000_channel_switch_cmd cmd;
	const struct iwl_channel_info *ch_info;
	u32 switch_time_in_usec, ucode_switch_time;
	u16 ch;
	u32 tsf_low;
	u8 switch_count;
	u16 beacon_interval = le16_to_cpu(ctx->timing.beacon_interval);
	struct ieee80211_vif *vif = ctx->vif;
	struct iwl_host_cmd hcmd = {
		.id = REPLY_CHANNEL_SWITCH,
		.len = sizeof(cmd),
		.flags = CMD_SYNC,
		.data = &cmd,
	};

	cmd.band = priv->band == IEEE80211_BAND_2GHZ;
	ch = ch_switch->channel->hw_value;
	IWL_DEBUG_11H(priv, "channel switch from %u to %u\n",
		ctx->active.channel, ch);
	cmd.channel = cpu_to_le16(ch);
	cmd.rxon_flags = ctx->staging.flags;
	cmd.rxon_filter_flags = ctx->staging.filter_flags;
	switch_count = ch_switch->count;
	tsf_low = ch_switch->timestamp & 0x0ffffffff;
	/*
	 * calculate the ucode channel switch time
	 * adding TSF as one of the factor for when to switch
	 */
	if ((priv->ucode_beacon_time > tsf_low) && beacon_interval) {
		if (switch_count > ((priv->ucode_beacon_time - tsf_low) /
		    beacon_interval)) {
			switch_count -= (priv->ucode_beacon_time -
				tsf_low) / beacon_interval;
		} else
			switch_count = 0;
	}
	if (switch_count <= 1)
		cmd.switch_time = cpu_to_le32(priv->ucode_beacon_time);
	else {
		switch_time_in_usec =
			vif->bss_conf.beacon_int * switch_count * TIME_UNIT;
		ucode_switch_time = iwl_usecs_to_beacons(priv,
						switch_time_in_usec,
						beacon_interval);
		cmd.switch_time = iwl_add_beacon_time(priv,
						priv->ucode_beacon_time,
						ucode_switch_time,
						beacon_interval);
	}
	IWL_DEBUG_11H(priv, "uCode time for the switch is 0x%x\n",
		      cmd.switch_time);
	ch_info = iwl_get_channel_info(priv, priv->band, ch);
	if (ch_info)
		cmd.expect_beacon = is_channel_radar(ch_info);
	else {
		IWL_ERR(priv, "invalid channel switch from %u to %u\n",
			ctx->active.channel, ch);
		return -EFAULT;
	}
	priv->switch_rxon.channel = cmd.channel;
	priv->switch_rxon.switch_in_progress = true;

	return iwl_send_cmd_sync(priv, &hcmd);
}

static struct iwl_lib_ops iwl2000_lib = {
	.set_hw_params = iwl2000_hw_set_hw_params,
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
	.set_channel_switch = iwl2030_hw_channel_switch,
	.apm_ops = {
		.init = iwl_apm_init,
		.config = iwl2000_nic_config,
	},
	.eeprom_ops = {
		.regulatory_bands = {
			EEPROM_REG_BAND_1_CHANNELS,
			EEPROM_REG_BAND_2_CHANNELS,
			EEPROM_REG_BAND_3_CHANNELS,
			EEPROM_REG_BAND_4_CHANNELS,
			EEPROM_REG_BAND_5_CHANNELS,
			EEPROM_6000_REG_BAND_24_HT40_CHANNELS,
			EEPROM_REG_BAND_52_HT40_CHANNELS
		},
		.acquire_semaphore = iwlcore_eeprom_acquire_semaphore,
		.release_semaphore = iwlcore_eeprom_release_semaphore,
		.calib_version  = iwlagn_eeprom_calib_version,
		.query_addr = iwlagn_eeprom_query_addr,
		.update_enhanced_txpower = iwlcore_eeprom_enhanced_txpower,
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

static const struct iwl_ops iwl2000_ops = {
	.lib = &iwl2000_lib,
	.hcmd = &iwlagn_hcmd,
	.utils = &iwlagn_hcmd_utils,
	.led = &iwlagn_led_ops,
	.ieee80211_ops = &iwlagn_hw_ops,
};

static const struct iwl_ops iwl2030_ops = {
	.lib = &iwl2000_lib,
	.hcmd = &iwlagn_bt_hcmd,
	.utils = &iwlagn_hcmd_utils,
	.led = &iwlagn_led_ops,
	.ieee80211_ops = &iwlagn_hw_ops,
};

static const struct iwl_ops iwl200_ops = {
	.lib = &iwl2000_lib,
	.hcmd = &iwlagn_hcmd,
	.utils = &iwlagn_hcmd_utils,
	.led = &iwlagn_led_ops,
	.ieee80211_ops = &iwlagn_hw_ops,
};

static const struct iwl_ops iwl230_ops = {
	.lib = &iwl2000_lib,
	.hcmd = &iwlagn_bt_hcmd,
	.utils = &iwlagn_hcmd_utils,
	.led = &iwlagn_led_ops,
	.ieee80211_ops = &iwlagn_hw_ops,
};

static struct iwl_base_params iwl2000_base_params = {
	.eeprom_size = OTP_LOW_IMAGE_SIZE,
	.num_of_queues = IWLAGN_NUM_QUEUES,
	.num_of_ampdu_queues = IWLAGN_NUM_AMPDU_QUEUES,
	.pll_cfg_val = 0,
	.set_l0s = true,
	.use_bsm = false,
	.max_ll_items = OTP_MAX_LL_ITEMS_2x00,
	.shadow_ram_support = true,
	.led_compensation = 51,
	.chain_noise_num_beacons = IWL_CAL_NUM_BEACONS,
	.supports_idle = true,
	.adv_thermal_throttle = true,
	.support_ct_kill_exit = true,
	.plcp_delta_threshold = IWL_MAX_PLCP_ERR_THRESHOLD_DEF,
	.chain_noise_scale = 1000,
	.wd_timeout = IWL_DEF_WD_TIMEOUT,
	.max_event_log_size = 512,
	.ucode_tracing = true,
	.sensitivity_calib_by_driver = true,
	.chain_noise_calib_by_driver = true,
	.shadow_reg_enable = true,
};


static struct iwl_base_params iwl2030_base_params = {
	.eeprom_size = OTP_LOW_IMAGE_SIZE,
	.num_of_queues = IWLAGN_NUM_QUEUES,
	.num_of_ampdu_queues = IWLAGN_NUM_AMPDU_QUEUES,
	.pll_cfg_val = 0,
	.set_l0s = true,
	.use_bsm = false,
	.max_ll_items = OTP_MAX_LL_ITEMS_2x00,
	.shadow_ram_support = true,
	.led_compensation = 57,
	.chain_noise_num_beacons = IWL_CAL_NUM_BEACONS,
	.supports_idle = true,
	.adv_thermal_throttle = true,
	.support_ct_kill_exit = true,
	.plcp_delta_threshold = IWL_MAX_PLCP_ERR_THRESHOLD_DEF,
	.chain_noise_scale = 1000,
	.wd_timeout = IWL_LONG_WD_TIMEOUT,
	.max_event_log_size = 512,
	.ucode_tracing = true,
	.sensitivity_calib_by_driver = true,
	.chain_noise_calib_by_driver = true,
	.shadow_reg_enable = true,
};

static struct iwl_ht_params iwl2000_ht_params = {
	.ht_greenfield_support = true,
	.use_rts_for_aggregation = true, /* use rts/cts protection */
};

static struct iwl_bt_params iwl2030_bt_params = {
	.bt_statistics = true,
	/* Due to bluetooth, we transmit 2.4 GHz probes only on antenna A */
	.advanced_bt_coexist = true,
	.agg_time_limit = BT_AGG_THRESHOLD_DEF,
	.bt_init_traffic_load = IWL_BT_COEX_TRAFFIC_LOAD_NONE,
	.bt_prio_boost = IWLAGN_BT_PRIO_BOOST_DEFAULT,
	.bt_sco_disable = true,
};

#define IWL_DEVICE_2000						\
	.fw_name_pre = IWL2000_FW_PRE,				\
	.ucode_api_max = IWL2000_UCODE_API_MAX,			\
	.ucode_api_min = IWL2000_UCODE_API_MIN,			\
	.eeprom_ver = EEPROM_2000_EEPROM_VERSION,		\
	.eeprom_calib_ver = EEPROM_2000_TX_POWER_VERSION,	\
	.ops = &iwl2000_ops,					\
	.mod_params = &iwlagn_mod_params,			\
	.base_params = &iwl2000_base_params,			\
	.need_dc_calib = true,					\
	.need_temp_offset_calib = true,				\
	.led_mode = IWL_LED_RF_STATE,				\
	.iq_invert = true					\

struct iwl_cfg iwl2000_2bgn_cfg = {
	.name = "2000 Series 2x2 BGN",
	IWL_DEVICE_2000,
	.ht_params = &iwl2000_ht_params,
};

struct iwl_cfg iwl2000_2bg_cfg = {
	.name = "2000 Series 2x2 BG",
	IWL_DEVICE_2000,
};

#define IWL_DEVICE_2030						\
	.fw_name_pre = IWL2030_FW_PRE,				\
	.ucode_api_max = IWL2030_UCODE_API_MAX,			\
	.ucode_api_min = IWL2030_UCODE_API_MIN,			\
	.eeprom_ver = EEPROM_2000_EEPROM_VERSION,		\
	.eeprom_calib_ver = EEPROM_2000_TX_POWER_VERSION,	\
	.ops = &iwl2030_ops,					\
	.mod_params = &iwlagn_mod_params,			\
	.base_params = &iwl2030_base_params,			\
	.bt_params = &iwl2030_bt_params,			\
	.need_dc_calib = true,					\
	.need_temp_offset_calib = true,				\
	.led_mode = IWL_LED_RF_STATE,				\
	.adv_pm = true,						\
	.iq_invert = true					\

struct iwl_cfg iwl2030_2bgn_cfg = {
	.name = "2000 Series 2x2 BGN/BT",
	IWL_DEVICE_2000,
	.ht_params = &iwl2000_ht_params,
};

struct iwl_cfg iwl2030_2bg_cfg = {
	.name = "2000 Series 2x2 BG/BT",
	IWL_DEVICE_2000,
};

#define IWL_DEVICE_6035						\
	.fw_name_pre = IWL2030_FW_PRE,				\
	.ucode_api_max = IWL2030_UCODE_API_MAX,			\
	.ucode_api_min = IWL2030_UCODE_API_MIN,			\
	.eeprom_ver = EEPROM_6035_EEPROM_VERSION,		\
	.eeprom_calib_ver = EEPROM_6035_TX_POWER_VERSION,	\
	.ops = &iwl2030_ops,					\
	.mod_params = &iwlagn_mod_params,			\
	.base_params = &iwl2030_base_params,			\
	.bt_params = &iwl2030_bt_params,			\
	.need_dc_calib = true,					\
	.need_temp_offset_calib = true,				\
	.led_mode = IWL_LED_RF_STATE,				\
	.adv_pm = true						\

struct iwl_cfg iwl6035_2agn_cfg = {
	.name = "2000 Series 2x2 AGN/BT",
	IWL_DEVICE_6035,
	.ht_params = &iwl2000_ht_params,
};

struct iwl_cfg iwl6035_2abg_cfg = {
	.name = "2000 Series 2x2 ABG/BT",
	IWL_DEVICE_6035,
};

struct iwl_cfg iwl6035_2bg_cfg = {
	.name = "2000 Series 2x2 BG/BT",
	IWL_DEVICE_6035,
};

#define IWL_DEVICE_200						\
	.fw_name_pre = IWL200_FW_PRE,				\
	.ucode_api_max = IWL200_UCODE_API_MAX,			\
	.ucode_api_min = IWL200_UCODE_API_MIN,			\
	.eeprom_ver = EEPROM_2000_EEPROM_VERSION,		\
	.eeprom_calib_ver = EEPROM_2000_TX_POWER_VERSION,	\
	.ops = &iwl200_ops,					\
	.mod_params = &iwlagn_mod_params,			\
	.base_params = &iwl2000_base_params,			\
	.need_dc_calib = true,					\
	.need_temp_offset_calib = true,				\
	.led_mode = IWL_LED_RF_STATE,				\
	.adv_pm = true,						\
	.rx_with_siso_diversity = true				\

struct iwl_cfg iwl200_bg_cfg = {
	.name = "200 Series 1x1 BG",
	IWL_DEVICE_200,
};

struct iwl_cfg iwl200_bgn_cfg = {
	.name = "200 Series 1x1 BGN",
	IWL_DEVICE_200,
	.ht_params = &iwl2000_ht_params,
};

#define IWL_DEVICE_230						\
	.fw_name_pre = IWL200_FW_PRE,				\
	.ucode_api_max = IWL200_UCODE_API_MAX,			\
	.ucode_api_min = IWL200_UCODE_API_MIN,			\
	.eeprom_ver = EEPROM_2000_EEPROM_VERSION,		\
	.eeprom_calib_ver = EEPROM_2000_TX_POWER_VERSION,	\
	.ops = &iwl230_ops,					\
	.mod_params = &iwlagn_mod_params,			\
	.base_params = &iwl2030_base_params,			\
	.bt_params = &iwl2030_bt_params,			\
	.need_dc_calib = true,					\
	.need_temp_offset_calib = true,				\
	.led_mode = IWL_LED_RF_STATE,				\
	.adv_pm = true,						\
	.rx_with_siso_diversity = true				\

struct iwl_cfg iwl230_bg_cfg = {
	.name = "200 Series 1x1 BG/BT",
	IWL_DEVICE_230,
};

struct iwl_cfg iwl230_bgn_cfg = {
	.name = "200 Series 1x1 BGN/BT",
	IWL_DEVICE_230,
	.ht_params = &iwl2000_ht_params,
};

MODULE_FIRMWARE(IWL2000_MODULE_FIRMWARE(IWL2000_UCODE_API_MAX));
MODULE_FIRMWARE(IWL2030_MODULE_FIRMWARE(IWL2030_UCODE_API_MAX));
MODULE_FIRMWARE(IWL200_MODULE_FIRMWARE(IWL200_UCODE_API_MAX));
