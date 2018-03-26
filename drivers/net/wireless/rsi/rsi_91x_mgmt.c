/**
 * Copyright (c) 2014 Redpine Signals Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/etherdevice.h>
#include "rsi_mgmt.h"
#include "rsi_common.h"
#include "rsi_ps.h"
#include "rsi_hal.h"

static struct bootup_params boot_params_20 = {
	.magic_number = cpu_to_le16(0x5aa5),
	.crystal_good_time = 0x0,
	.valid = cpu_to_le32(VALID_20),
	.reserved_for_valids = 0x0,
	.bootup_mode_info = 0x0,
	.digital_loop_back_params = 0x0,
	.rtls_timestamp_en = 0x0,
	.host_spi_intr_cfg = 0x0,
	.device_clk_info = {{
		.pll_config_g = {
			.tapll_info_g = {
				.pll_reg_1 = cpu_to_le16((TA_PLL_N_VAL_20 << 8)|
					      (TA_PLL_M_VAL_20)),
				.pll_reg_2 = cpu_to_le16(TA_PLL_P_VAL_20),
			},
			.pll960_info_g = {
				.pll_reg_1 = cpu_to_le16((PLL960_P_VAL_20 << 8)|
							 (PLL960_N_VAL_20)),
				.pll_reg_2 = cpu_to_le16(PLL960_M_VAL_20),
				.pll_reg_3 = 0x0,
			},
			.afepll_info_g = {
				.pll_reg = cpu_to_le16(0x9f0),
			}
		},
		.switch_clk_g = {
			.switch_clk_info = cpu_to_le16(0xb),
			.bbp_lmac_clk_reg_val = cpu_to_le16(0x111),
			.umac_clock_reg_config = cpu_to_le16(0x48),
			.qspi_uart_clock_reg_config = cpu_to_le16(0x1211)
		}
	},
	{
		.pll_config_g = {
			.tapll_info_g = {
				.pll_reg_1 = cpu_to_le16((TA_PLL_N_VAL_20 << 8)|
							 (TA_PLL_M_VAL_20)),
				.pll_reg_2 = cpu_to_le16(TA_PLL_P_VAL_20),
			},
			.pll960_info_g = {
				.pll_reg_1 = cpu_to_le16((PLL960_P_VAL_20 << 8)|
							 (PLL960_N_VAL_20)),
				.pll_reg_2 = cpu_to_le16(PLL960_M_VAL_20),
				.pll_reg_3 = 0x0,
			},
			.afepll_info_g = {
				.pll_reg = cpu_to_le16(0x9f0),
			}
		},
		.switch_clk_g = {
			.switch_clk_info = 0x0,
			.bbp_lmac_clk_reg_val = 0x0,
			.umac_clock_reg_config = 0x0,
			.qspi_uart_clock_reg_config = 0x0
		}
	},
	{
		.pll_config_g = {
			.tapll_info_g = {
				.pll_reg_1 = cpu_to_le16((TA_PLL_N_VAL_20 << 8)|
							 (TA_PLL_M_VAL_20)),
				.pll_reg_2 = cpu_to_le16(TA_PLL_P_VAL_20),
			},
			.pll960_info_g = {
				.pll_reg_1 = cpu_to_le16((PLL960_P_VAL_20 << 8)|
							 (PLL960_N_VAL_20)),
				.pll_reg_2 = cpu_to_le16(PLL960_M_VAL_20),
				.pll_reg_3 = 0x0,
			},
			.afepll_info_g = {
				.pll_reg = cpu_to_le16(0x9f0),
			}
		},
		.switch_clk_g = {
			.switch_clk_info = 0x0,
			.bbp_lmac_clk_reg_val = 0x0,
			.umac_clock_reg_config = 0x0,
			.qspi_uart_clock_reg_config = 0x0
		}
	} },
	.buckboost_wakeup_cnt = 0x0,
	.pmu_wakeup_wait = 0x0,
	.shutdown_wait_time = 0x0,
	.pmu_slp_clkout_sel = 0x0,
	.wdt_prog_value = 0x0,
	.wdt_soc_rst_delay = 0x0,
	.dcdc_operation_mode = 0x0,
	.soc_reset_wait_cnt = 0x0,
	.waiting_time_at_fresh_sleep = 0x0,
	.max_threshold_to_avoid_sleep = 0x0,
	.beacon_resedue_alg_en = 0,
};

static struct bootup_params boot_params_40 = {
	.magic_number = cpu_to_le16(0x5aa5),
	.crystal_good_time = 0x0,
	.valid = cpu_to_le32(VALID_40),
	.reserved_for_valids = 0x0,
	.bootup_mode_info = 0x0,
	.digital_loop_back_params = 0x0,
	.rtls_timestamp_en = 0x0,
	.host_spi_intr_cfg = 0x0,
	.device_clk_info = {{
		.pll_config_g = {
			.tapll_info_g = {
				.pll_reg_1 = cpu_to_le16((TA_PLL_N_VAL_40 << 8)|
							 (TA_PLL_M_VAL_40)),
				.pll_reg_2 = cpu_to_le16(TA_PLL_P_VAL_40),
			},
			.pll960_info_g = {
				.pll_reg_1 = cpu_to_le16((PLL960_P_VAL_40 << 8)|
							 (PLL960_N_VAL_40)),
				.pll_reg_2 = cpu_to_le16(PLL960_M_VAL_40),
				.pll_reg_3 = 0x0,
			},
			.afepll_info_g = {
				.pll_reg = cpu_to_le16(0x9f0),
			}
		},
		.switch_clk_g = {
			.switch_clk_info = cpu_to_le16(0x09),
			.bbp_lmac_clk_reg_val = cpu_to_le16(0x1121),
			.umac_clock_reg_config = cpu_to_le16(0x48),
			.qspi_uart_clock_reg_config = cpu_to_le16(0x1211)
		}
	},
	{
		.pll_config_g = {
			.tapll_info_g = {
				.pll_reg_1 = cpu_to_le16((TA_PLL_N_VAL_40 << 8)|
							 (TA_PLL_M_VAL_40)),
				.pll_reg_2 = cpu_to_le16(TA_PLL_P_VAL_40),
			},
			.pll960_info_g = {
				.pll_reg_1 = cpu_to_le16((PLL960_P_VAL_40 << 8)|
							 (PLL960_N_VAL_40)),
				.pll_reg_2 = cpu_to_le16(PLL960_M_VAL_40),
				.pll_reg_3 = 0x0,
			},
			.afepll_info_g = {
				.pll_reg = cpu_to_le16(0x9f0),
			}
		},
		.switch_clk_g = {
			.switch_clk_info = 0x0,
			.bbp_lmac_clk_reg_val = 0x0,
			.umac_clock_reg_config = 0x0,
			.qspi_uart_clock_reg_config = 0x0
		}
	},
	{
		.pll_config_g = {
			.tapll_info_g = {
				.pll_reg_1 = cpu_to_le16((TA_PLL_N_VAL_40 << 8)|
							 (TA_PLL_M_VAL_40)),
				.pll_reg_2 = cpu_to_le16(TA_PLL_P_VAL_40),
			},
			.pll960_info_g = {
				.pll_reg_1 = cpu_to_le16((PLL960_P_VAL_40 << 8)|
							 (PLL960_N_VAL_40)),
				.pll_reg_2 = cpu_to_le16(PLL960_M_VAL_40),
				.pll_reg_3 = 0x0,
			},
			.afepll_info_g = {
				.pll_reg = cpu_to_le16(0x9f0),
			}
		},
		.switch_clk_g = {
			.switch_clk_info = 0x0,
			.bbp_lmac_clk_reg_val = 0x0,
			.umac_clock_reg_config = 0x0,
			.qspi_uart_clock_reg_config = 0x0
		}
	} },
	.buckboost_wakeup_cnt = 0x0,
	.pmu_wakeup_wait = 0x0,
	.shutdown_wait_time = 0x0,
	.pmu_slp_clkout_sel = 0x0,
	.wdt_prog_value = 0x0,
	.wdt_soc_rst_delay = 0x0,
	.dcdc_operation_mode = 0x0,
	.soc_reset_wait_cnt = 0x0,
	.waiting_time_at_fresh_sleep = 0x0,
	.max_threshold_to_avoid_sleep = 0x0,
	.beacon_resedue_alg_en = 0,
};

static u16 mcs[] = {13, 26, 39, 52, 78, 104, 117, 130};

/**
 * rsi_set_default_parameters() - This function sets default parameters.
 * @common: Pointer to the driver private structure.
 *
 * Return: none
 */
static void rsi_set_default_parameters(struct rsi_common *common)
{
	common->band = NL80211_BAND_2GHZ;
	common->channel_width = BW_20MHZ;
	common->rts_threshold = IEEE80211_MAX_RTS_THRESHOLD;
	common->channel = 1;
	common->min_rate = 0xffff;
	common->fsm_state = FSM_CARD_NOT_READY;
	common->iface_down = true;
	common->endpoint = EP_2GHZ_20MHZ;
	common->driver_mode = 1; /* End to end mode */
	common->lp_ps_handshake_mode = 0; /* Default no handShake mode*/
	common->ulp_ps_handshake_mode = 2; /* Default PKT handShake mode*/
	common->rf_power_val = 0; /* Default 1.9V */
	common->wlan_rf_power_mode = 0;
	common->obm_ant_sel_val = 2;
	common->beacon_interval = RSI_BEACON_INTERVAL;
	common->dtim_cnt = RSI_DTIM_COUNT;
}

/**
 * rsi_set_contention_vals() - This function sets the contention values for the
 *			       backoff procedure.
 * @common: Pointer to the driver private structure.
 *
 * Return: None.
 */
static void rsi_set_contention_vals(struct rsi_common *common)
{
	u8 ii = 0;

	for (; ii < NUM_EDCA_QUEUES; ii++) {
		common->tx_qinfo[ii].wme_params =
			(((common->edca_params[ii].cw_min / 2) +
			  (common->edca_params[ii].aifs)) *
			  WMM_SHORT_SLOT_TIME + SIFS_DURATION);
		common->tx_qinfo[ii].weight = common->tx_qinfo[ii].wme_params;
		common->tx_qinfo[ii].pkt_contended = 0;
	}
}

/**
 * rsi_send_internal_mgmt_frame() - This function sends management frames to
 *				    firmware.Also schedules packet to queue
 *				    for transmission.
 * @common: Pointer to the driver private structure.
 * @skb: Pointer to the socket buffer structure.
 *
 * Return: 0 on success, -1 on failure.
 */
static int rsi_send_internal_mgmt_frame(struct rsi_common *common,
					struct sk_buff *skb)
{
	struct skb_info *tx_params;
	struct rsi_cmd_desc *desc;

	if (skb == NULL) {
		rsi_dbg(ERR_ZONE, "%s: Unable to allocate skb\n", __func__);
		return -ENOMEM;
	}
	desc = (struct rsi_cmd_desc *)skb->data;
	desc->desc_dword0.len_qno |= cpu_to_le16(DESC_IMMEDIATE_WAKEUP);
	skb->priority = MGMT_SOFT_Q;
	tx_params = (struct skb_info *)&IEEE80211_SKB_CB(skb)->driver_data;
	tx_params->flags |= INTERNAL_MGMT_PKT;
	skb_queue_tail(&common->tx_queue[MGMT_SOFT_Q], skb);
	rsi_set_event(&common->tx_thread.event);
	return 0;
}

/**
 * rsi_load_radio_caps() - This function is used to send radio capabilities
 *			   values to firmware.
 * @common: Pointer to the driver private structure.
 *
 * Return: 0 on success, corresponding negative error code on failure.
 */
static int rsi_load_radio_caps(struct rsi_common *common)
{
	struct rsi_radio_caps *radio_caps;
	struct rsi_hw *adapter = common->priv;
	u16 inx = 0;
	u8 ii;
	u8 radio_id = 0;
	u16 gc[20] = {0xf0, 0xf0, 0xf0, 0xf0,
		      0xf0, 0xf0, 0xf0, 0xf0,
		      0xf0, 0xf0, 0xf0, 0xf0,
		      0xf0, 0xf0, 0xf0, 0xf0,
		      0xf0, 0xf0, 0xf0, 0xf0};
	struct sk_buff *skb;
	u16 frame_len = sizeof(struct rsi_radio_caps);

	rsi_dbg(INFO_ZONE, "%s: Sending rate symbol req frame\n", __func__);

	skb = dev_alloc_skb(frame_len);

	if (!skb) {
		rsi_dbg(ERR_ZONE, "%s: Failed in allocation of skb\n",
			__func__);
		return -ENOMEM;
	}

	memset(skb->data, 0, frame_len);
	radio_caps = (struct rsi_radio_caps *)skb->data;

	radio_caps->desc_dword0.frame_type = RADIO_CAPABILITIES;
	radio_caps->channel_num = common->channel;
	radio_caps->rf_model = RSI_RF_TYPE;

	if (common->channel_width == BW_40MHZ) {
		radio_caps->radio_cfg_info = RSI_LMAC_CLOCK_80MHZ;
		radio_caps->radio_cfg_info |= RSI_ENABLE_40MHZ;

		if (common->fsm_state == FSM_MAC_INIT_DONE) {
			struct ieee80211_hw *hw = adapter->hw;
			struct ieee80211_conf *conf = &hw->conf;

			if (conf_is_ht40_plus(conf)) {
				radio_caps->radio_cfg_info =
					RSI_CMDDESC_LOWER_20_ENABLE;
				radio_caps->radio_info =
					RSI_CMDDESC_LOWER_20_ENABLE;
			} else if (conf_is_ht40_minus(conf)) {
				radio_caps->radio_cfg_info =
					RSI_CMDDESC_UPPER_20_ENABLE;
				radio_caps->radio_info =
					RSI_CMDDESC_UPPER_20_ENABLE;
			} else {
				radio_caps->radio_cfg_info =
					RSI_CMDDESC_40MHZ;
				radio_caps->radio_info =
					RSI_CMDDESC_FULL_40_ENABLE;
			}
		}
	}
	radio_caps->radio_info |= radio_id;

	radio_caps->sifs_tx_11n = cpu_to_le16(SIFS_TX_11N_VALUE);
	radio_caps->sifs_tx_11b = cpu_to_le16(SIFS_TX_11B_VALUE);
	radio_caps->slot_rx_11n = cpu_to_le16(SHORT_SLOT_VALUE);
	radio_caps->ofdm_ack_tout = cpu_to_le16(OFDM_ACK_TOUT_VALUE);
	radio_caps->cck_ack_tout = cpu_to_le16(CCK_ACK_TOUT_VALUE);
	radio_caps->preamble_type = cpu_to_le16(LONG_PREAMBLE);

	for (ii = 0; ii < MAX_HW_QUEUES; ii++) {
		radio_caps->qos_params[ii].cont_win_min_q = cpu_to_le16(3);
		radio_caps->qos_params[ii].cont_win_max_q = cpu_to_le16(0x3f);
		radio_caps->qos_params[ii].aifsn_val_q = cpu_to_le16(2);
		radio_caps->qos_params[ii].txop_q = 0;
	}

	for (ii = 0; ii < NUM_EDCA_QUEUES; ii++) {
		radio_caps->qos_params[ii].cont_win_min_q =
			cpu_to_le16(common->edca_params[ii].cw_min);
		radio_caps->qos_params[ii].cont_win_max_q =
			cpu_to_le16(common->edca_params[ii].cw_max);
		radio_caps->qos_params[ii].aifsn_val_q =
			cpu_to_le16((common->edca_params[ii].aifs) << 8);
		radio_caps->qos_params[ii].txop_q =
			cpu_to_le16(common->edca_params[ii].txop);
	}

	radio_caps->qos_params[BROADCAST_HW_Q].txop_q = cpu_to_le16(0xffff);
	radio_caps->qos_params[MGMT_HW_Q].txop_q = 0;
	radio_caps->qos_params[BEACON_HW_Q].txop_q = cpu_to_le16(0xffff);

	memcpy(&common->rate_pwr[0], &gc[0], 40);
	for (ii = 0; ii < 20; ii++)
		radio_caps->gcpd_per_rate[inx++] =
			cpu_to_le16(common->rate_pwr[ii]  & 0x00FF);

	rsi_set_len_qno(&radio_caps->desc_dword0.len_qno,
			(frame_len - FRAME_DESC_SZ), RSI_WIFI_MGMT_Q);

	skb_put(skb, frame_len);

	return rsi_send_internal_mgmt_frame(common, skb);
}

/**
 * rsi_mgmt_pkt_to_core() - This function is the entry point for Mgmt module.
 * @common: Pointer to the driver private structure.
 * @msg: Pointer to received packet.
 * @msg_len: Length of the recieved packet.
 * @type: Type of recieved packet.
 *
 * Return: 0 on success, -1 on failure.
 */
static int rsi_mgmt_pkt_to_core(struct rsi_common *common,
				u8 *msg,
				s32 msg_len)
{
	struct rsi_hw *adapter = common->priv;
	struct ieee80211_tx_info *info;
	struct skb_info *rx_params;
	u8 pad_bytes = msg[4];
	struct sk_buff *skb;

	if (!adapter->sc_nvifs)
		return -ENOLINK;

	msg_len -= pad_bytes;
	if (msg_len <= 0) {
		rsi_dbg(MGMT_RX_ZONE,
			"%s: Invalid rx msg of len = %d\n",
			__func__, msg_len);
		return -EINVAL;
	}

	skb = dev_alloc_skb(msg_len);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb,
		     (u8 *)(msg + FRAME_DESC_SZ + pad_bytes),
		     msg_len);

	info = IEEE80211_SKB_CB(skb);
	rx_params = (struct skb_info *)info->driver_data;
	rx_params->rssi = rsi_get_rssi(msg);
	rx_params->channel = rsi_get_channel(msg);
	rsi_indicate_pkt_to_os(common, skb);

	return 0;
}

/**
 * rsi_hal_send_sta_notify_frame() - This function sends the station notify
 *				     frame to firmware.
 * @common: Pointer to the driver private structure.
 * @opmode: Operating mode of device.
 * @notify_event: Notification about station connection.
 * @bssid: bssid.
 * @qos_enable: Qos is enabled.
 * @aid: Aid (unique for all STA).
 *
 * Return: status: 0 on success, corresponding negative error code on failure.
 */
static int rsi_hal_send_sta_notify_frame(struct rsi_common *common,
					 enum opmode opmode,
					 u8 notify_event,
					 const unsigned char *bssid,
					 u8 qos_enable,
					 u16 aid,
					 u16 sta_id,
					 struct ieee80211_vif *vif)
{
	struct sk_buff *skb = NULL;
	struct rsi_peer_notify *peer_notify;
	u16 vap_id = ((struct vif_priv *)vif->drv_priv)->vap_id;
	int status;
	u16 frame_len = sizeof(struct rsi_peer_notify);

	rsi_dbg(MGMT_TX_ZONE, "%s: Sending sta notify frame\n", __func__);

	skb = dev_alloc_skb(frame_len);

	if (!skb) {
		rsi_dbg(ERR_ZONE, "%s: Failed in allocation of skb\n",
			__func__);
		return -ENOMEM;
	}

	memset(skb->data, 0, frame_len);
	peer_notify = (struct rsi_peer_notify *)skb->data;

	if (opmode == RSI_OPMODE_STA)
		peer_notify->command = cpu_to_le16(PEER_TYPE_AP << 1);
	else if (opmode == RSI_OPMODE_AP)
		peer_notify->command = cpu_to_le16(PEER_TYPE_STA << 1);

	switch (notify_event) {
	case STA_CONNECTED:
		peer_notify->command |= cpu_to_le16(RSI_ADD_PEER);
		break;
	case STA_DISCONNECTED:
		peer_notify->command |= cpu_to_le16(RSI_DELETE_PEER);
		break;
	default:
		break;
	}

	peer_notify->command |= cpu_to_le16((aid & 0xfff) << 4);
	ether_addr_copy(peer_notify->mac_addr, bssid);
	peer_notify->mpdu_density = cpu_to_le16(RSI_MPDU_DENSITY);
	peer_notify->sta_flags = cpu_to_le32((qos_enable) ? 1 : 0);

	rsi_set_len_qno(&peer_notify->desc.desc_dword0.len_qno,
			(frame_len - FRAME_DESC_SZ),
			RSI_WIFI_MGMT_Q);
	peer_notify->desc.desc_dword0.frame_type = PEER_NOTIFY;
	peer_notify->desc.desc_dword3.qid_tid = sta_id;
	peer_notify->desc.desc_dword3.sta_id = vap_id;

	skb_put(skb, frame_len);

	status = rsi_send_internal_mgmt_frame(common, skb);

	if ((vif->type == NL80211_IFTYPE_STATION) &&
	    (!status && qos_enable)) {
		rsi_set_contention_vals(common);
		status = rsi_load_radio_caps(common);
	}
	return status;
}

/**
 * rsi_send_aggregation_params_frame() - This function sends the ampdu
 *					 indication frame to firmware.
 * @common: Pointer to the driver private structure.
 * @tid: traffic identifier.
 * @ssn: ssn.
 * @buf_size: buffer size.
 * @event: notification about station connection.
 *
 * Return: 0 on success, corresponding negative error code on failure.
 */
int rsi_send_aggregation_params_frame(struct rsi_common *common,
				      u16 tid,
				      u16 ssn,
				      u8 buf_size,
				      u8 event,
				      u8 sta_id)
{
	struct sk_buff *skb = NULL;
	struct rsi_aggr_params *aggr_params;
	u16 frame_len = sizeof(struct rsi_aggr_params);

	skb = dev_alloc_skb(frame_len);

	if (!skb) {
		rsi_dbg(ERR_ZONE, "%s: Failed in allocation of skb\n",
			__func__);
		return -ENOMEM;
	}

	memset(skb->data, 0, frame_len);
	aggr_params = (struct rsi_aggr_params *)skb->data;

	rsi_dbg(MGMT_TX_ZONE, "%s: Sending AMPDU indication frame\n", __func__);

	rsi_set_len_qno(&aggr_params->desc_dword0.len_qno, 0, RSI_WIFI_MGMT_Q);
	aggr_params->desc_dword0.frame_type = AMPDU_IND;

	aggr_params->aggr_params = tid & RSI_AGGR_PARAMS_TID_MASK;
	aggr_params->peer_id = sta_id;
	if (event == STA_TX_ADDBA_DONE) {
		aggr_params->seq_start = cpu_to_le16(ssn);
		aggr_params->baw_size = cpu_to_le16(buf_size);
		aggr_params->aggr_params |= RSI_AGGR_PARAMS_START;
	} else if (event == STA_RX_ADDBA_DONE) {
		aggr_params->seq_start = cpu_to_le16(ssn);
		aggr_params->aggr_params |= (RSI_AGGR_PARAMS_START |
					     RSI_AGGR_PARAMS_RX_AGGR);
	} else if (event == STA_RX_DELBA) {
		aggr_params->aggr_params |= RSI_AGGR_PARAMS_RX_AGGR;
	}

	skb_put(skb, frame_len);

	return rsi_send_internal_mgmt_frame(common, skb);
}

/**
 * rsi_program_bb_rf() - This function starts base band and RF programming.
 *			 This is called after initial configurations are done.
 * @common: Pointer to the driver private structure.
 *
 * Return: 0 on success, corresponding negative error code on failure.
 */
static int rsi_program_bb_rf(struct rsi_common *common)
{
	struct sk_buff *skb;
	struct rsi_bb_rf_prog *bb_rf_prog;
	u16 frame_len = sizeof(struct rsi_bb_rf_prog);

	rsi_dbg(MGMT_TX_ZONE, "%s: Sending program BB/RF frame\n", __func__);

	skb = dev_alloc_skb(frame_len);
	if (!skb) {
		rsi_dbg(ERR_ZONE, "%s: Failed in allocation of skb\n",
			__func__);
		return -ENOMEM;
	}

	memset(skb->data, 0, frame_len);
	bb_rf_prog = (struct rsi_bb_rf_prog *)skb->data;

	rsi_set_len_qno(&bb_rf_prog->desc_dword0.len_qno, 0, RSI_WIFI_MGMT_Q);
	bb_rf_prog->desc_dword0.frame_type = BBP_PROG_IN_TA;
	bb_rf_prog->endpoint = common->endpoint;
	bb_rf_prog->rf_power_mode = common->wlan_rf_power_mode;

	if (common->rf_reset) {
		bb_rf_prog->flags =  cpu_to_le16(RF_RESET_ENABLE);
		rsi_dbg(MGMT_TX_ZONE, "%s: ===> RF RESET REQUEST SENT <===\n",
			__func__);
		common->rf_reset = 0;
	}
	common->bb_rf_prog_count = 1;
	bb_rf_prog->flags |= cpu_to_le16(PUT_BBP_RESET | BBP_REG_WRITE |
					 (RSI_RF_TYPE << 4));
	skb_put(skb, frame_len);

	return rsi_send_internal_mgmt_frame(common, skb);
}

/**
 * rsi_set_vap_capabilities() - This function send vap capability to firmware.
 * @common: Pointer to the driver private structure.
 * @opmode: Operating mode of device.
 *
 * Return: 0 on success, corresponding negative error code on failure.
 */
int rsi_set_vap_capabilities(struct rsi_common *common,
			     enum opmode mode,
			     u8 *mac_addr,
			     u8 vap_id,
			     u8 vap_status)
{
	struct sk_buff *skb = NULL;
	struct rsi_vap_caps *vap_caps;
	struct rsi_hw *adapter = common->priv;
	struct ieee80211_hw *hw = adapter->hw;
	struct ieee80211_conf *conf = &hw->conf;
	u16 frame_len = sizeof(struct rsi_vap_caps);

	rsi_dbg(MGMT_TX_ZONE, "%s: Sending VAP capabilities frame\n", __func__);

	skb = dev_alloc_skb(frame_len);
	if (!skb) {
		rsi_dbg(ERR_ZONE, "%s: Failed in allocation of skb\n",
			__func__);
		return -ENOMEM;
	}

	memset(skb->data, 0, frame_len);
	vap_caps = (struct rsi_vap_caps *)skb->data;

	rsi_set_len_qno(&vap_caps->desc_dword0.len_qno,
			(frame_len - FRAME_DESC_SZ), RSI_WIFI_MGMT_Q);
	vap_caps->desc_dword0.frame_type = VAP_CAPABILITIES;
	vap_caps->status = vap_status;
	vap_caps->vif_type = mode;
	vap_caps->channel_bw = common->channel_width;
	vap_caps->vap_id = vap_id;
	vap_caps->radioid_macid = ((common->mac_id & 0xf) << 4) |
				   (common->radio_id & 0xf);

	memcpy(vap_caps->mac_addr, mac_addr, IEEE80211_ADDR_LEN);
	vap_caps->keep_alive_period = cpu_to_le16(90);
	vap_caps->frag_threshold = cpu_to_le16(IEEE80211_MAX_FRAG_THRESHOLD);

	vap_caps->rts_threshold = cpu_to_le16(common->rts_threshold);

	if (common->band == NL80211_BAND_5GHZ) {
		vap_caps->default_ctrl_rate = cpu_to_le16(RSI_RATE_6);
		vap_caps->default_mgmt_rate = cpu_to_le32(RSI_RATE_6);
	} else {
		vap_caps->default_ctrl_rate = cpu_to_le16(RSI_RATE_1);
		vap_caps->default_mgmt_rate = cpu_to_le32(RSI_RATE_1);
	}
	if (conf_is_ht40(conf)) {
		if (conf_is_ht40_minus(conf))
			vap_caps->ctrl_rate_flags =
				cpu_to_le16(UPPER_20_ENABLE);
		else if (conf_is_ht40_plus(conf))
			vap_caps->ctrl_rate_flags =
				cpu_to_le16(LOWER_20_ENABLE);
		else
			vap_caps->ctrl_rate_flags =
				cpu_to_le16(FULL40M_ENABLE);
	}

	vap_caps->default_data_rate = 0;
	vap_caps->beacon_interval = cpu_to_le16(common->beacon_interval);
	vap_caps->dtim_period = cpu_to_le16(common->dtim_cnt);

	skb_put(skb, frame_len);

	return rsi_send_internal_mgmt_frame(common, skb);
}

/**
 * rsi_hal_load_key() - This function is used to load keys within the firmware.
 * @common: Pointer to the driver private structure.
 * @data: Pointer to the key data.
 * @key_len: Key length to be loaded.
 * @key_type: Type of key: GROUP/PAIRWISE.
 * @key_id: Key index.
 * @cipher: Type of cipher used.
 *
 * Return: 0 on success, -1 on failure.
 */
int rsi_hal_load_key(struct rsi_common *common,
		     u8 *data,
		     u16 key_len,
		     u8 key_type,
		     u8 key_id,
		     u32 cipher,
		     s16 sta_id,
		     struct ieee80211_vif *vif)
{
	struct sk_buff *skb = NULL;
	struct rsi_set_key *set_key;
	u16 key_descriptor = 0;
	u16 frame_len = sizeof(struct rsi_set_key);

	rsi_dbg(MGMT_TX_ZONE, "%s: Sending load key frame\n", __func__);

	skb = dev_alloc_skb(frame_len);
	if (!skb) {
		rsi_dbg(ERR_ZONE, "%s: Failed in allocation of skb\n",
			__func__);
		return -ENOMEM;
	}

	memset(skb->data, 0, frame_len);
	set_key = (struct rsi_set_key *)skb->data;

	if (key_type == RSI_GROUP_KEY) {
		key_descriptor = RSI_KEY_TYPE_BROADCAST;
		if (vif->type == NL80211_IFTYPE_AP)
			key_descriptor |= RSI_KEY_MODE_AP;
	}
	if ((cipher == WLAN_CIPHER_SUITE_WEP40) ||
	    (cipher == WLAN_CIPHER_SUITE_WEP104)) {
		key_id = 0;
		key_descriptor |= RSI_WEP_KEY;
		if (key_len >= 13)
			key_descriptor |= RSI_WEP_KEY_104;
	} else if (cipher != KEY_TYPE_CLEAR) {
		key_descriptor |= RSI_CIPHER_WPA;
		if (cipher == WLAN_CIPHER_SUITE_TKIP)
			key_descriptor |= RSI_CIPHER_TKIP;
	}
	key_descriptor |= RSI_PROTECT_DATA_FRAMES;
	key_descriptor |= ((key_id << RSI_KEY_ID_OFFSET) & RSI_KEY_ID_MASK);

	rsi_set_len_qno(&set_key->desc_dword0.len_qno,
			(frame_len - FRAME_DESC_SZ), RSI_WIFI_MGMT_Q);
	set_key->desc_dword0.frame_type = SET_KEY_REQ;
	set_key->key_desc = cpu_to_le16(key_descriptor);
	set_key->sta_id = sta_id;

	if (data) {
		if ((cipher == WLAN_CIPHER_SUITE_WEP40) ||
		    (cipher == WLAN_CIPHER_SUITE_WEP104)) {
			memcpy(&set_key->key[key_id][1], data, key_len * 2);
		} else {
			memcpy(&set_key->key[0][0], data, key_len);
		}
		memcpy(set_key->tx_mic_key, &data[16], 8);
		memcpy(set_key->rx_mic_key, &data[24], 8);
	} else {
		memset(&set_key[FRAME_DESC_SZ], 0, frame_len - FRAME_DESC_SZ);
	}

	skb_put(skb, frame_len);

	return rsi_send_internal_mgmt_frame(common, skb);
}

/*
 * This function sends the common device configuration parameters to device.
 * This frame includes the useful information to make device works on
 * specific operating mode.
 */
static int rsi_send_common_dev_params(struct rsi_common *common)
{
	struct sk_buff *skb;
	u16 frame_len;
	struct rsi_config_vals *dev_cfgs;

	frame_len = sizeof(struct rsi_config_vals);

	rsi_dbg(MGMT_TX_ZONE, "Sending common device config params\n");
	skb = dev_alloc_skb(frame_len);
	if (!skb) {
		rsi_dbg(ERR_ZONE, "%s: Unable to allocate skb\n", __func__);
		return -ENOMEM;
	}

	memset(skb->data, 0, frame_len);

	dev_cfgs = (struct rsi_config_vals *)skb->data;
	memset(dev_cfgs, 0, (sizeof(struct rsi_config_vals)));

	rsi_set_len_qno(&dev_cfgs->len_qno, (frame_len - FRAME_DESC_SZ),
			RSI_COEX_Q);
	dev_cfgs->pkt_type = COMMON_DEV_CONFIG;

	dev_cfgs->lp_ps_handshake = common->lp_ps_handshake_mode;
	dev_cfgs->ulp_ps_handshake = common->ulp_ps_handshake_mode;

	dev_cfgs->unused_ulp_gpio = RSI_UNUSED_ULP_GPIO_BITMAP;
	dev_cfgs->unused_soc_gpio_bitmap =
				cpu_to_le32(RSI_UNUSED_SOC_GPIO_BITMAP);

	dev_cfgs->opermode = common->oper_mode;
	dev_cfgs->wlan_rf_pwr_mode = common->wlan_rf_power_mode;
	dev_cfgs->driver_mode = common->driver_mode;
	dev_cfgs->region_code = NL80211_DFS_FCC;
	dev_cfgs->antenna_sel_val = common->obm_ant_sel_val;

	skb_put(skb, frame_len);

	return rsi_send_internal_mgmt_frame(common, skb);
}

/*
 * rsi_load_bootup_params() - This function send bootup params to the firmware.
 * @common: Pointer to the driver private structure.
 *
 * Return: 0 on success, corresponding error code on failure.
 */
static int rsi_load_bootup_params(struct rsi_common *common)
{
	struct sk_buff *skb;
	struct rsi_boot_params *boot_params;

	rsi_dbg(MGMT_TX_ZONE, "%s: Sending boot params frame\n", __func__);
	skb = dev_alloc_skb(sizeof(struct rsi_boot_params));
	if (!skb) {
		rsi_dbg(ERR_ZONE, "%s: Failed in allocation of skb\n",
			__func__);
		return -ENOMEM;
	}

	memset(skb->data, 0, sizeof(struct rsi_boot_params));
	boot_params = (struct rsi_boot_params *)skb->data;

	rsi_dbg(MGMT_TX_ZONE, "%s:\n", __func__);

	if (common->channel_width == BW_40MHZ) {
		memcpy(&boot_params->bootup_params,
		       &boot_params_40,
		       sizeof(struct bootup_params));
		rsi_dbg(MGMT_TX_ZONE, "%s: Packet 40MHZ <=== %d\n", __func__,
			UMAC_CLK_40BW);
		boot_params->desc_word[7] = cpu_to_le16(UMAC_CLK_40BW);
	} else {
		memcpy(&boot_params->bootup_params,
		       &boot_params_20,
		       sizeof(struct bootup_params));
		if (boot_params_20.valid != cpu_to_le32(VALID_20)) {
			boot_params->desc_word[7] = cpu_to_le16(UMAC_CLK_20BW);
			rsi_dbg(MGMT_TX_ZONE,
				"%s: Packet 20MHZ <=== %d\n", __func__,
				UMAC_CLK_20BW);
		} else {
			boot_params->desc_word[7] = cpu_to_le16(UMAC_CLK_40MHZ);
			rsi_dbg(MGMT_TX_ZONE,
				"%s: Packet 20MHZ <=== %d\n", __func__,
				UMAC_CLK_40MHZ);
		}
	}

	/**
	 * Bit{0:11} indicates length of the Packet
	 * Bit{12:15} indicates host queue number
	 */
	boot_params->desc_word[0] = cpu_to_le16(sizeof(struct bootup_params) |
				    (RSI_WIFI_MGMT_Q << 12));
	boot_params->desc_word[1] = cpu_to_le16(BOOTUP_PARAMS_REQUEST);

	skb_put(skb, sizeof(struct rsi_boot_params));

	return rsi_send_internal_mgmt_frame(common, skb);
}

/**
 * rsi_send_reset_mac() - This function prepares reset MAC request and sends an
 *			  internal management frame to indicate it to firmware.
 * @common: Pointer to the driver private structure.
 *
 * Return: 0 on success, corresponding error code on failure.
 */
static int rsi_send_reset_mac(struct rsi_common *common)
{
	struct sk_buff *skb;
	struct rsi_mac_frame *mgmt_frame;

	rsi_dbg(MGMT_TX_ZONE, "%s: Sending reset MAC frame\n", __func__);

	skb = dev_alloc_skb(FRAME_DESC_SZ);
	if (!skb) {
		rsi_dbg(ERR_ZONE, "%s: Failed in allocation of skb\n",
			__func__);
		return -ENOMEM;
	}

	memset(skb->data, 0, FRAME_DESC_SZ);
	mgmt_frame = (struct rsi_mac_frame *)skb->data;

	mgmt_frame->desc_word[0] = cpu_to_le16(RSI_WIFI_MGMT_Q << 12);
	mgmt_frame->desc_word[1] = cpu_to_le16(RESET_MAC_REQ);
	mgmt_frame->desc_word[4] = cpu_to_le16(RETRY_COUNT << 8);

	skb_put(skb, FRAME_DESC_SZ);

	return rsi_send_internal_mgmt_frame(common, skb);
}

/**
 * rsi_band_check() - This function programs the band
 * @common: Pointer to the driver private structure.
 *
 * Return: 0 on success, corresponding error code on failure.
 */
int rsi_band_check(struct rsi_common *common,
		   struct ieee80211_channel *curchan)
{
	struct rsi_hw *adapter = common->priv;
	struct ieee80211_hw *hw = adapter->hw;
	u8 prev_bw = common->channel_width;
	u8 prev_ep = common->endpoint;
	int status = 0;

	if (common->band != curchan->band) {
		common->rf_reset = 1;
		common->band = curchan->band;
	}

	if ((hw->conf.chandef.width == NL80211_CHAN_WIDTH_20_NOHT) ||
	    (hw->conf.chandef.width == NL80211_CHAN_WIDTH_20))
		common->channel_width = BW_20MHZ;
	else
		common->channel_width = BW_40MHZ;

	if (common->band == NL80211_BAND_2GHZ) {
		if (common->channel_width)
			common->endpoint = EP_2GHZ_40MHZ;
		else
			common->endpoint = EP_2GHZ_20MHZ;
	} else {
		if (common->channel_width)
			common->endpoint = EP_5GHZ_40MHZ;
		else
			common->endpoint = EP_5GHZ_20MHZ;
	}

	if (common->endpoint != prev_ep) {
		status = rsi_program_bb_rf(common);
		if (status)
			return status;
	}

	if (common->channel_width != prev_bw) {
		status = rsi_load_bootup_params(common);
		if (status)
			return status;

		status = rsi_load_radio_caps(common);
		if (status)
			return status;
	}

	return status;
}

/**
 * rsi_set_channel() - This function programs the channel.
 * @common: Pointer to the driver private structure.
 * @channel: Channel value to be set.
 *
 * Return: 0 on success, corresponding error code on failure.
 */
int rsi_set_channel(struct rsi_common *common,
		    struct ieee80211_channel *channel)
{
	struct sk_buff *skb = NULL;
	struct rsi_chan_config *chan_cfg;
	u16 frame_len = sizeof(struct rsi_chan_config);

	rsi_dbg(MGMT_TX_ZONE,
		"%s: Sending scan req frame\n", __func__);

	skb = dev_alloc_skb(frame_len);
	if (!skb) {
		rsi_dbg(ERR_ZONE, "%s: Failed in allocation of skb\n",
			__func__);
		return -ENOMEM;
	}

	if (!channel) {
		dev_kfree_skb(skb);
		return 0;
	}
	memset(skb->data, 0, frame_len);
	chan_cfg = (struct rsi_chan_config *)skb->data;

	rsi_set_len_qno(&chan_cfg->desc_dword0.len_qno, 0, RSI_WIFI_MGMT_Q);
	chan_cfg->desc_dword0.frame_type = SCAN_REQUEST;
	chan_cfg->channel_number = channel->hw_value;
	chan_cfg->antenna_gain_offset_2g = channel->max_antenna_gain;
	chan_cfg->antenna_gain_offset_5g = channel->max_antenna_gain;
	chan_cfg->region_rftype = (RSI_RF_TYPE & 0xf) << 4;

	if ((channel->flags & IEEE80211_CHAN_NO_IR) ||
	    (channel->flags & IEEE80211_CHAN_RADAR)) {
		chan_cfg->antenna_gain_offset_2g |= RSI_CHAN_RADAR;
	} else {
		if (common->tx_power < channel->max_power)
			chan_cfg->tx_power = cpu_to_le16(common->tx_power);
		else
			chan_cfg->tx_power = cpu_to_le16(channel->max_power);
	}
	chan_cfg->region_rftype |= (common->priv->dfs_region & 0xf);

	if (common->channel_width == BW_40MHZ)
		chan_cfg->channel_width = 0x1;

	common->channel = channel->hw_value;

	skb_put(skb, frame_len);

	return rsi_send_internal_mgmt_frame(common, skb);
}

/**
 * rsi_send_radio_params_update() - This function sends the radio
 *				parameters update to device
 * @common: Pointer to the driver private structure.
 * @channel: Channel value to be set.
 *
 * Return: 0 on success, corresponding error code on failure.
 */
int rsi_send_radio_params_update(struct rsi_common *common)
{
	struct rsi_mac_frame *cmd_frame;
	struct sk_buff *skb = NULL;

	rsi_dbg(MGMT_TX_ZONE,
		"%s: Sending Radio Params update frame\n", __func__);

	skb = dev_alloc_skb(FRAME_DESC_SZ);
	if (!skb) {
		rsi_dbg(ERR_ZONE, "%s: Failed in allocation of skb\n",
			__func__);
		return -ENOMEM;
	}

	memset(skb->data, 0, FRAME_DESC_SZ);
	cmd_frame = (struct rsi_mac_frame *)skb->data;

	cmd_frame->desc_word[0] = cpu_to_le16(RSI_WIFI_MGMT_Q << 12);
	cmd_frame->desc_word[1] = cpu_to_le16(RADIO_PARAMS_UPDATE);
	cmd_frame->desc_word[3] = cpu_to_le16(BIT(0));

	cmd_frame->desc_word[3] |= cpu_to_le16(common->tx_power << 8);

	skb_put(skb, FRAME_DESC_SZ);

	return rsi_send_internal_mgmt_frame(common, skb);
}

/* This function programs the threshold. */
int rsi_send_vap_dynamic_update(struct rsi_common *common)
{
	struct sk_buff *skb;
	struct rsi_dynamic_s *dynamic_frame;

	rsi_dbg(MGMT_TX_ZONE,
		"%s: Sending vap update indication frame\n", __func__);

	skb = dev_alloc_skb(sizeof(struct rsi_dynamic_s));
	if (!skb)
		return -ENOMEM;

	memset(skb->data, 0, sizeof(struct rsi_dynamic_s));
	dynamic_frame = (struct rsi_dynamic_s *)skb->data;
	rsi_set_len_qno(&dynamic_frame->desc_dword0.len_qno,
			sizeof(dynamic_frame->frame_body), RSI_WIFI_MGMT_Q);

	dynamic_frame->desc_dword0.frame_type = VAP_DYNAMIC_UPDATE;
	dynamic_frame->desc_dword2.pkt_info =
					cpu_to_le32(common->rts_threshold);

	if (common->wow_flags & RSI_WOW_ENABLED) {
		/* Beacon miss threshold */
		dynamic_frame->desc_dword3.token =
					cpu_to_le16(RSI_BCN_MISS_THRESHOLD);
		dynamic_frame->frame_body.keep_alive_period =
					cpu_to_le16(RSI_WOW_KEEPALIVE);
	} else {
		dynamic_frame->frame_body.keep_alive_period =
					cpu_to_le16(RSI_DEF_KEEPALIVE);
	}

	dynamic_frame->desc_dword3.sta_id = 0; /* vap id */

	skb_put(skb, sizeof(struct rsi_dynamic_s));

	return rsi_send_internal_mgmt_frame(common, skb);
}

/**
 * rsi_compare() - This function is used to compare two integers
 * @a: pointer to the first integer
 * @b: pointer to the second integer
 *
 * Return: 0 if both are equal, -1 if the first is smaller, else 1
 */
static int rsi_compare(const void *a, const void *b)
{
	u16 _a = *(const u16 *)(a);
	u16 _b = *(const u16 *)(b);

	if (_a > _b)
		return -1;

	if (_a < _b)
		return 1;

	return 0;
}

/**
 * rsi_map_rates() - This function is used to map selected rates to hw rates.
 * @rate: The standard rate to be mapped.
 * @offset: Offset that will be returned.
 *
 * Return: 0 if it is a mcs rate, else 1
 */
static bool rsi_map_rates(u16 rate, int *offset)
{
	int kk;
	for (kk = 0; kk < ARRAY_SIZE(rsi_mcsrates); kk++) {
		if (rate == mcs[kk]) {
			*offset = kk;
			return false;
		}
	}

	for (kk = 0; kk < ARRAY_SIZE(rsi_rates); kk++) {
		if (rate == rsi_rates[kk].bitrate / 5) {
			*offset = kk;
			break;
		}
	}
	return true;
}

/**
 * rsi_send_auto_rate_request() - This function is to set rates for connection
 *				  and send autorate request to firmware.
 * @common: Pointer to the driver private structure.
 *
 * Return: 0 on success, corresponding error code on failure.
 */
static int rsi_send_auto_rate_request(struct rsi_common *common,
				      struct ieee80211_sta *sta,
				      u16 sta_id,
				      struct ieee80211_vif *vif)
{
	struct sk_buff *skb;
	struct rsi_auto_rate *auto_rate;
	int ii = 0, jj = 0, kk = 0;
	struct ieee80211_hw *hw = common->priv->hw;
	u8 band = hw->conf.chandef.chan->band;
	u8 num_supported_rates = 0;
	u8 rate_table_offset, rate_offset = 0;
	u32 rate_bitmap;
	u16 *selected_rates, min_rate;
	bool is_ht = false, is_sgi = false;
	u16 frame_len = sizeof(struct rsi_auto_rate);

	rsi_dbg(MGMT_TX_ZONE,
		"%s: Sending auto rate request frame\n", __func__);

	skb = dev_alloc_skb(frame_len);
	if (!skb) {
		rsi_dbg(ERR_ZONE, "%s: Failed in allocation of skb\n",
			__func__);
		return -ENOMEM;
	}

	selected_rates = kzalloc(2 * RSI_TBL_SZ, GFP_KERNEL);
	if (!selected_rates) {
		rsi_dbg(ERR_ZONE, "%s: Failed in allocation of mem\n",
			__func__);
		dev_kfree_skb(skb);
		return -ENOMEM;
	}

	auto_rate = (struct rsi_auto_rate *)skb->data;

	auto_rate->aarf_rssi = cpu_to_le16(((u16)3 << 6) | (u16)(18 & 0x3f));
	auto_rate->collision_tolerance = cpu_to_le16(3);
	auto_rate->failure_limit = cpu_to_le16(3);
	auto_rate->initial_boundary = cpu_to_le16(3);
	auto_rate->max_threshold_limt = cpu_to_le16(27);

	auto_rate->desc.desc_dword0.frame_type = AUTO_RATE_IND;

	if (common->channel_width == BW_40MHZ)
		auto_rate->desc.desc_dword3.qid_tid = BW_40MHZ;
	auto_rate->desc.desc_dword3.sta_id = sta_id;

	if (vif->type == NL80211_IFTYPE_STATION) {
		rate_bitmap = common->bitrate_mask[band];
		is_ht = common->vif_info[0].is_ht;
		is_sgi = common->vif_info[0].sgi;
	} else {
		rate_bitmap = sta->supp_rates[band];
		is_ht = sta->ht_cap.ht_supported;
		if ((sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_20) ||
		    (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_40))
			is_sgi = true;
	}

	if (band == NL80211_BAND_2GHZ) {
		if ((rate_bitmap == 0) && (is_ht))
			min_rate = RSI_RATE_MCS0;
		else
			min_rate = RSI_RATE_1;
		rate_table_offset = 0;
	} else {
		if ((rate_bitmap == 0) && (is_ht))
			min_rate = RSI_RATE_MCS0;
		else
			min_rate = RSI_RATE_6;
		rate_table_offset = 4;
	}

	for (ii = 0, jj = 0;
	     ii < (ARRAY_SIZE(rsi_rates) - rate_table_offset); ii++) {
		if (rate_bitmap & BIT(ii)) {
			selected_rates[jj++] =
			(rsi_rates[ii + rate_table_offset].bitrate / 5);
			rate_offset++;
		}
	}
	num_supported_rates = jj;

	if (is_ht) {
		for (ii = 0; ii < ARRAY_SIZE(mcs); ii++)
			selected_rates[jj++] = mcs[ii];
		num_supported_rates += ARRAY_SIZE(mcs);
		rate_offset += ARRAY_SIZE(mcs);
	}

	sort(selected_rates, jj, sizeof(u16), &rsi_compare, NULL);

	/* mapping the rates to RSI rates */
	for (ii = 0; ii < jj; ii++) {
		if (rsi_map_rates(selected_rates[ii], &kk)) {
			auto_rate->supported_rates[ii] =
				cpu_to_le16(rsi_rates[kk].hw_value);
		} else {
			auto_rate->supported_rates[ii] =
				cpu_to_le16(rsi_mcsrates[kk]);
		}
	}

	/* loading HT rates in the bottom half of the auto rate table */
	if (is_ht) {
		for (ii = rate_offset, kk = ARRAY_SIZE(rsi_mcsrates) - 1;
		     ii < rate_offset + 2 * ARRAY_SIZE(rsi_mcsrates); ii++) {
			if (is_sgi || conf_is_ht40(&common->priv->hw->conf))
				auto_rate->supported_rates[ii++] =
					cpu_to_le16(rsi_mcsrates[kk] | BIT(9));
			else
				auto_rate->supported_rates[ii++] =
					cpu_to_le16(rsi_mcsrates[kk]);
			auto_rate->supported_rates[ii] =
				cpu_to_le16(rsi_mcsrates[kk--]);
		}

		for (; ii < (RSI_TBL_SZ - 1); ii++) {
			auto_rate->supported_rates[ii] =
				cpu_to_le16(rsi_mcsrates[0]);
		}
	}

	for (; ii < RSI_TBL_SZ; ii++)
		auto_rate->supported_rates[ii] = cpu_to_le16(min_rate);

	auto_rate->num_supported_rates = cpu_to_le16(num_supported_rates * 2);
	auto_rate->moderate_rate_inx = cpu_to_le16(num_supported_rates / 2);
	num_supported_rates *= 2;

	rsi_set_len_qno(&auto_rate->desc.desc_dword0.len_qno,
			(frame_len - FRAME_DESC_SZ), RSI_WIFI_MGMT_Q);

	skb_put(skb, frame_len);
	kfree(selected_rates);

	return rsi_send_internal_mgmt_frame(common, skb);
}

/**
 * rsi_inform_bss_status() - This function informs about bss status with the
 *			     help of sta notify params by sending an internal
 *			     management frame to firmware.
 * @common: Pointer to the driver private structure.
 * @status: Bss status type.
 * @bssid: Bssid.
 * @qos_enable: Qos is enabled.
 * @aid: Aid (unique for all STAs).
 *
 * Return: None.
 */
void rsi_inform_bss_status(struct rsi_common *common,
			   enum opmode opmode,
			   u8 status,
			   const u8 *addr,
			   u8 qos_enable,
			   u16 aid,
			   struct ieee80211_sta *sta,
			   u16 sta_id,
			   struct ieee80211_vif *vif)
{
	if (status) {
		if (opmode == RSI_OPMODE_STA)
			common->hw_data_qs_blocked = true;
		rsi_hal_send_sta_notify_frame(common,
					      opmode,
					      STA_CONNECTED,
					      addr,
					      qos_enable,
					      aid, sta_id,
					      vif);
		if (common->min_rate == 0xffff)
			rsi_send_auto_rate_request(common, sta, sta_id, vif);
		if (opmode == RSI_OPMODE_STA) {
			if (!rsi_send_block_unblock_frame(common, false))
				common->hw_data_qs_blocked = false;
		}
	} else {
		if (opmode == RSI_OPMODE_STA)
			common->hw_data_qs_blocked = true;

		if (!(common->wow_flags & RSI_WOW_ENABLED))
			rsi_hal_send_sta_notify_frame(common, opmode,
						      STA_DISCONNECTED, addr,
						      qos_enable, aid, sta_id,
						      vif);
		if (opmode == RSI_OPMODE_STA)
			rsi_send_block_unblock_frame(common, true);
	}
}

/**
 * rsi_eeprom_read() - This function sends a frame to read the mac address
 *		       from the eeprom.
 * @common: Pointer to the driver private structure.
 *
 * Return: 0 on success, -1 on failure.
 */
static int rsi_eeprom_read(struct rsi_common *common)
{
	struct rsi_eeprom_read_frame *mgmt_frame;
	struct rsi_hw *adapter = common->priv;
	struct sk_buff *skb;

	rsi_dbg(MGMT_TX_ZONE, "%s: Sending EEPROM read req frame\n", __func__);

	skb = dev_alloc_skb(FRAME_DESC_SZ);
	if (!skb) {
		rsi_dbg(ERR_ZONE, "%s: Failed in allocation of skb\n",
			__func__);
		return -ENOMEM;
	}

	memset(skb->data, 0, FRAME_DESC_SZ);
	mgmt_frame = (struct rsi_eeprom_read_frame *)skb->data;

	/* FrameType */
	rsi_set_len_qno(&mgmt_frame->len_qno, 0, RSI_WIFI_MGMT_Q);
	mgmt_frame->pkt_type = EEPROM_READ;

	/* Number of bytes to read */
	mgmt_frame->pkt_info =
		cpu_to_le32((adapter->eeprom.length << RSI_EEPROM_LEN_OFFSET) &
			    RSI_EEPROM_LEN_MASK);
	mgmt_frame->pkt_info |= cpu_to_le32((3 << RSI_EEPROM_HDR_SIZE_OFFSET) &
					    RSI_EEPROM_HDR_SIZE_MASK);

	/* Address to read */
	mgmt_frame->eeprom_offset = cpu_to_le32(adapter->eeprom.offset);

	skb_put(skb, FRAME_DESC_SZ);

	return rsi_send_internal_mgmt_frame(common, skb);
}

/**
 * This function sends a frame to block/unblock
 * data queues in the firmware
 *
 * @param common Pointer to the driver private structure.
 * @param block event - block if true, unblock if false
 * @return 0 on success, -1 on failure.
 */
int rsi_send_block_unblock_frame(struct rsi_common *common, bool block_event)
{
	struct rsi_block_unblock_data *mgmt_frame;
	struct sk_buff *skb;

	rsi_dbg(MGMT_TX_ZONE, "%s: Sending block/unblock frame\n", __func__);

	skb = dev_alloc_skb(FRAME_DESC_SZ);
	if (!skb) {
		rsi_dbg(ERR_ZONE, "%s: Failed in allocation of skb\n",
			__func__);
		return -ENOMEM;
	}

	memset(skb->data, 0, FRAME_DESC_SZ);
	mgmt_frame = (struct rsi_block_unblock_data *)skb->data;

	rsi_set_len_qno(&mgmt_frame->desc_dword0.len_qno, 0, RSI_WIFI_MGMT_Q);
	mgmt_frame->desc_dword0.frame_type = BLOCK_HW_QUEUE;
	mgmt_frame->host_quiet_info = QUIET_INFO_VALID;

	if (block_event) {
		rsi_dbg(INFO_ZONE, "blocking the data qs\n");
		mgmt_frame->block_q_bitmap = cpu_to_le16(0xf);
		mgmt_frame->block_q_bitmap |= cpu_to_le16(0xf << 4);
	} else {
		rsi_dbg(INFO_ZONE, "unblocking the data qs\n");
		mgmt_frame->unblock_q_bitmap = cpu_to_le16(0xf);
		mgmt_frame->unblock_q_bitmap |= cpu_to_le16(0xf << 4);
	}

	skb_put(skb, FRAME_DESC_SZ);

	return rsi_send_internal_mgmt_frame(common, skb);
}

/**
 * rsi_send_rx_filter_frame() - Sends a frame to filter the RX packets
 *
 * @common: Pointer to the driver private structure.
 * @rx_filter_word: Flags of filter packets
 *
 * @Return: 0 on success, -1 on failure.
 */
int rsi_send_rx_filter_frame(struct rsi_common *common, u16 rx_filter_word)
{
	struct rsi_mac_frame *cmd_frame;
	struct sk_buff *skb;

	rsi_dbg(MGMT_TX_ZONE, "Sending RX filter frame\n");

	skb = dev_alloc_skb(FRAME_DESC_SZ);
	if (!skb) {
		rsi_dbg(ERR_ZONE, "%s: Failed in allocation of skb\n",
			__func__);
		return -ENOMEM;
	}

	memset(skb->data, 0, FRAME_DESC_SZ);
	cmd_frame = (struct rsi_mac_frame *)skb->data;

	cmd_frame->desc_word[0] = cpu_to_le16(RSI_WIFI_MGMT_Q << 12);
	cmd_frame->desc_word[1] = cpu_to_le16(SET_RX_FILTER);
	cmd_frame->desc_word[4] = cpu_to_le16(rx_filter_word);

	skb_put(skb, FRAME_DESC_SZ);

	return rsi_send_internal_mgmt_frame(common, skb);
}

int rsi_send_ps_request(struct rsi_hw *adapter, bool enable,
			struct ieee80211_vif *vif)
{
	struct rsi_common *common = adapter->priv;
	struct ieee80211_bss_conf *bss = &vif->bss_conf;
	struct rsi_request_ps *ps;
	struct rsi_ps_info *ps_info;
	struct sk_buff *skb;
	int frame_len = sizeof(*ps);

	skb = dev_alloc_skb(frame_len);
	if (!skb)
		return -ENOMEM;
	memset(skb->data, 0, frame_len);

	ps = (struct rsi_request_ps *)skb->data;
	ps_info = &adapter->ps_info;

	rsi_set_len_qno(&ps->desc.desc_dword0.len_qno,
			(frame_len - FRAME_DESC_SZ), RSI_WIFI_MGMT_Q);
	ps->desc.desc_dword0.frame_type = WAKEUP_SLEEP_REQUEST;
	if (enable) {
		ps->ps_sleep.enable = RSI_PS_ENABLE;
		ps->desc.desc_dword3.token = cpu_to_le16(RSI_SLEEP_REQUEST);
	} else {
		ps->ps_sleep.enable = RSI_PS_DISABLE;
		ps->desc.desc_dword0.len_qno |= cpu_to_le16(RSI_PS_DISABLE_IND);
		ps->desc.desc_dword3.token = cpu_to_le16(RSI_WAKEUP_REQUEST);
	}

	ps->ps_uapsd_acs = common->uapsd_bitmap;

	ps->ps_sleep.sleep_type = ps_info->sleep_type;
	ps->ps_sleep.num_bcns_per_lis_int =
		cpu_to_le16(ps_info->num_bcns_per_lis_int);
	ps->ps_sleep.sleep_duration =
		cpu_to_le32(ps_info->deep_sleep_wakeup_period);

	if (bss->assoc)
		ps->ps_sleep.connected_sleep = RSI_CONNECTED_SLEEP;
	else
		ps->ps_sleep.connected_sleep = RSI_DEEP_SLEEP;

	ps->ps_listen_interval = cpu_to_le32(ps_info->listen_interval);
	ps->ps_dtim_interval_duration =
		cpu_to_le32(ps_info->dtim_interval_duration);

	if (ps_info->listen_interval > ps_info->dtim_interval_duration)
		ps->ps_listen_interval = cpu_to_le32(RSI_PS_DISABLE);

	ps->ps_num_dtim_intervals = cpu_to_le16(ps_info->num_dtims_per_sleep);
	skb_put(skb, frame_len);

	return rsi_send_internal_mgmt_frame(common, skb);
}

/**
 * rsi_set_antenna() - This fuction send antenna configuration request
 *		       to device
 *
 * @common: Pointer to the driver private structure.
 * @antenna: bitmap for tx antenna selection
 *
 * Return: 0 on Success, negative error code on failure
 */
int rsi_set_antenna(struct rsi_common *common, u8 antenna)
{
	struct rsi_ant_sel_frame *ant_sel_frame;
	struct sk_buff *skb;

	skb = dev_alloc_skb(FRAME_DESC_SZ);
	if (!skb) {
		rsi_dbg(ERR_ZONE, "%s: Failed in allocation of skb\n",
			__func__);
		return -ENOMEM;
	}

	memset(skb->data, 0, FRAME_DESC_SZ);

	ant_sel_frame = (struct rsi_ant_sel_frame *)skb->data;
	ant_sel_frame->desc_dword0.frame_type = ANT_SEL_FRAME;
	ant_sel_frame->sub_frame_type = ANTENNA_SEL_TYPE;
	ant_sel_frame->ant_value = cpu_to_le16(antenna & ANTENNA_MASK_VALUE);
	rsi_set_len_qno(&ant_sel_frame->desc_dword0.len_qno,
			0, RSI_WIFI_MGMT_Q);
	skb_put(skb, FRAME_DESC_SZ);

	return rsi_send_internal_mgmt_frame(common, skb);
}

static int rsi_send_beacon(struct rsi_common *common)
{
	struct sk_buff *skb = NULL;
	u8 dword_align_bytes = 0;

	skb = dev_alloc_skb(MAX_MGMT_PKT_SIZE);
	if (!skb)
		return -ENOMEM;

	memset(skb->data, 0, MAX_MGMT_PKT_SIZE);

	dword_align_bytes = ((unsigned long)skb->data & 0x3f);
	if (dword_align_bytes)
		skb_pull(skb, (64 - dword_align_bytes));
	if (rsi_prepare_beacon(common, skb)) {
		rsi_dbg(ERR_ZONE, "Failed to prepare beacon\n");
		return -EINVAL;
	}
	skb_queue_tail(&common->tx_queue[MGMT_BEACON_Q], skb);
	rsi_set_event(&common->tx_thread.event);
	rsi_dbg(DATA_TX_ZONE, "%s: Added to beacon queue\n", __func__);

	return 0;
}

#ifdef CONFIG_PM
int rsi_send_wowlan_request(struct rsi_common *common, u16 flags,
			    u16 sleep_status)
{
	struct rsi_wowlan_req *cmd_frame;
	struct sk_buff *skb;
	u8 length;

	rsi_dbg(ERR_ZONE, "%s: Sending wowlan request frame\n", __func__);

	length = sizeof(*cmd_frame);
	skb = dev_alloc_skb(length);
	if (!skb)
		return -ENOMEM;
	memset(skb->data, 0, length);
	cmd_frame = (struct rsi_wowlan_req *)skb->data;

	rsi_set_len_qno(&cmd_frame->desc.desc_dword0.len_qno,
			(length - FRAME_DESC_SZ),
			RSI_WIFI_MGMT_Q);
	cmd_frame->desc.desc_dword0.frame_type = WOWLAN_CONFIG_PARAMS;
	cmd_frame->host_sleep_status = sleep_status;
	if (common->secinfo.security_enable &&
	    common->secinfo.gtk_cipher)
		flags |= RSI_WOW_GTK_REKEY;
	if (sleep_status)
		cmd_frame->wow_flags = flags;
	rsi_dbg(INFO_ZONE, "Host_Sleep_Status : %d Flags : %d\n",
		cmd_frame->host_sleep_status, cmd_frame->wow_flags);

	skb_put(skb, length);

	return rsi_send_internal_mgmt_frame(common, skb);
}
#endif

/**
 * rsi_handle_ta_confirm_type() - This function handles the confirm frames.
 * @common: Pointer to the driver private structure.
 * @msg: Pointer to received packet.
 *
 * Return: 0 on success, -1 on failure.
 */
static int rsi_handle_ta_confirm_type(struct rsi_common *common,
				      u8 *msg)
{
	struct rsi_hw *adapter = common->priv;
	u8 sub_type = (msg[15] & 0xff);
	u16 msg_len = ((u16 *)msg)[0] & 0xfff;
	u8 offset;

	switch (sub_type) {
	case BOOTUP_PARAMS_REQUEST:
		rsi_dbg(FSM_ZONE, "%s: Boot up params confirm received\n",
			__func__);
		if (common->fsm_state == FSM_BOOT_PARAMS_SENT) {
			adapter->eeprom.length = (IEEE80211_ADDR_LEN +
						  WLAN_MAC_MAGIC_WORD_LEN +
						  WLAN_HOST_MODE_LEN);
			adapter->eeprom.offset = WLAN_MAC_EEPROM_ADDR;
			if (rsi_eeprom_read(common)) {
				common->fsm_state = FSM_CARD_NOT_READY;
				goto out;
			}
			common->fsm_state = FSM_EEPROM_READ_MAC_ADDR;
		} else {
			rsi_dbg(INFO_ZONE,
				"%s: Received bootup params cfm in %d state\n",
				 __func__, common->fsm_state);
			return 0;
		}
		break;

	case EEPROM_READ:
		rsi_dbg(FSM_ZONE, "EEPROM READ confirm received\n");
		if (msg_len <= 0) {
			rsi_dbg(FSM_ZONE,
				"%s: [EEPROM_READ] Invalid len %d\n",
				__func__, msg_len);
			goto out;
		}
		if (msg[16] != MAGIC_WORD) {
			rsi_dbg(FSM_ZONE,
				"%s: [EEPROM_READ] Invalid token\n", __func__);
			common->fsm_state = FSM_CARD_NOT_READY;
			goto out;
		}
		if (common->fsm_state == FSM_EEPROM_READ_MAC_ADDR) {
			offset = (FRAME_DESC_SZ + WLAN_HOST_MODE_LEN +
				  WLAN_MAC_MAGIC_WORD_LEN);
			memcpy(common->mac_addr, &msg[offset], ETH_ALEN);
			adapter->eeprom.length =
				((WLAN_MAC_MAGIC_WORD_LEN + 3) & (~3));
			adapter->eeprom.offset = WLAN_EEPROM_RFTYPE_ADDR;
			if (rsi_eeprom_read(common)) {
				rsi_dbg(ERR_ZONE,
					"%s: Failed reading RF band\n",
					__func__);
				common->fsm_state = FSM_CARD_NOT_READY;
				goto out;
			}
			common->fsm_state = FSM_EEPROM_READ_RF_TYPE;
		} else if (common->fsm_state == FSM_EEPROM_READ_RF_TYPE) {
			if ((msg[17] & 0x3) == 0x3) {
				rsi_dbg(INIT_ZONE, "Dual band supported\n");
				common->band = NL80211_BAND_5GHZ;
				common->num_supp_bands = 2;
			} else if ((msg[17] & 0x3) == 0x1) {
				rsi_dbg(INIT_ZONE,
					"Only 2.4Ghz band supported\n");
				common->band = NL80211_BAND_2GHZ;
				common->num_supp_bands = 1;
			}
			if (rsi_send_reset_mac(common))
				goto out;
			common->fsm_state = FSM_RESET_MAC_SENT;
		} else {
			rsi_dbg(ERR_ZONE, "%s: Invalid EEPROM read type\n",
				__func__);
			return 0;
		}
		break;

	case RESET_MAC_REQ:
		if (common->fsm_state == FSM_RESET_MAC_SENT) {
			rsi_dbg(FSM_ZONE, "%s: Reset MAC cfm received\n",
				__func__);

			if (rsi_load_radio_caps(common))
				goto out;
			else
				common->fsm_state = FSM_RADIO_CAPS_SENT;
		} else {
			rsi_dbg(ERR_ZONE,
				"%s: Received reset mac cfm in %d state\n",
				 __func__, common->fsm_state);
			return 0;
		}
		break;

	case RADIO_CAPABILITIES:
		if (common->fsm_state == FSM_RADIO_CAPS_SENT) {
			common->rf_reset = 1;
			if (rsi_program_bb_rf(common)) {
				goto out;
			} else {
				common->fsm_state = FSM_BB_RF_PROG_SENT;
				rsi_dbg(FSM_ZONE, "%s: Radio cap cfm received\n",
					__func__);
			}
		} else {
			rsi_dbg(INFO_ZONE,
				"%s: Received radio caps cfm in %d state\n",
				 __func__, common->fsm_state);
			return 0;
		}
		break;

	case BB_PROG_VALUES_REQUEST:
	case RF_PROG_VALUES_REQUEST:
	case BBP_PROG_IN_TA:
		rsi_dbg(FSM_ZONE, "%s: BB/RF cfm received\n", __func__);
		if (common->fsm_state == FSM_BB_RF_PROG_SENT) {
			common->bb_rf_prog_count--;
			if (!common->bb_rf_prog_count) {
				common->fsm_state = FSM_MAC_INIT_DONE;
				if (common->reinit_hw) {
					complete(&common->wlan_init_completion);
				} else {
					return rsi_mac80211_attach(common);
				}
			}
		} else {
			rsi_dbg(INFO_ZONE,
				"%s: Received bbb_rf cfm in %d state\n",
				 __func__, common->fsm_state);
			return 0;
		}
		break;
	case WAKEUP_SLEEP_REQUEST:
		rsi_dbg(INFO_ZONE, "Wakeup/Sleep confirmation.\n");
		return rsi_handle_ps_confirm(adapter, msg);
	default:
		rsi_dbg(INFO_ZONE, "%s: Invalid TA confirm pkt received\n",
			__func__);
		break;
	}
	return 0;
out:
	rsi_dbg(ERR_ZONE, "%s: Unable to send pkt/Invalid frame received\n",
		__func__);
	return -EINVAL;
}

int rsi_handle_card_ready(struct rsi_common *common, u8 *msg)
{
	switch (common->fsm_state) {
	case FSM_CARD_NOT_READY:
		rsi_dbg(INIT_ZONE, "Card ready indication from Common HAL\n");
		rsi_set_default_parameters(common);
		if (rsi_send_common_dev_params(common) < 0)
			return -EINVAL;
		common->fsm_state = FSM_COMMON_DEV_PARAMS_SENT;
		break;
	case FSM_COMMON_DEV_PARAMS_SENT:
		rsi_dbg(INIT_ZONE, "Card ready indication from WLAN HAL\n");

		/* Get usb buffer status register address */
		common->priv->usb_buffer_status_reg = *(u32 *)&msg[8];
		rsi_dbg(INFO_ZONE, "USB buffer status register = %x\n",
			common->priv->usb_buffer_status_reg);

		if (rsi_load_bootup_params(common)) {
			common->fsm_state = FSM_CARD_NOT_READY;
			return -EINVAL;
		}
		common->fsm_state = FSM_BOOT_PARAMS_SENT;
		break;
	default:
		rsi_dbg(ERR_ZONE,
			"%s: card ready indication in invalid state %d.\n",
			__func__, common->fsm_state);
		return -EINVAL;
	}

	return 0;
}

/**
 * rsi_mgmt_pkt_recv() - This function processes the management packets
 *			 recieved from the hardware.
 * @common: Pointer to the driver private structure.
 * @msg: Pointer to the received packet.
 *
 * Return: 0 on success, -1 on failure.
 */
int rsi_mgmt_pkt_recv(struct rsi_common *common, u8 *msg)
{
	s32 msg_len = (le16_to_cpu(*(__le16 *)&msg[0]) & 0x0fff);
	u16 msg_type = (msg[2]);

	rsi_dbg(FSM_ZONE, "%s: Msg Len: %d, Msg Type: %4x\n",
		__func__, msg_len, msg_type);

	switch (msg_type) {
	case TA_CONFIRM_TYPE:
		return rsi_handle_ta_confirm_type(common, msg);
	case CARD_READY_IND:
		common->hibernate_resume = false;
		rsi_dbg(FSM_ZONE, "%s: Card ready indication received\n",
			__func__);
		return rsi_handle_card_ready(common, msg);
	case TX_STATUS_IND:
		if (msg[15] == PROBEREQ_CONFIRM) {
			common->mgmt_q_block = false;
			rsi_dbg(FSM_ZONE, "%s: Probe confirm received\n",
				__func__);
		}
		break;
	case BEACON_EVENT_IND:
		rsi_dbg(INFO_ZONE, "Beacon event\n");
		if (common->fsm_state != FSM_MAC_INIT_DONE)
			return -1;
		if (common->iface_down)
			return -1;
		if (!common->beacon_enabled)
			return -1;
		rsi_send_beacon(common);
		break;
	case RX_DOT11_MGMT:
		return rsi_mgmt_pkt_to_core(common, msg, msg_len);
	default:
		rsi_dbg(INFO_ZONE, "Received packet type: 0x%x\n", msg_type);
	}
	return 0;
}
