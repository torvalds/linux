/******************************************************************************
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2013 Intel Corporation. All rights reserved.
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
 *****************************************************************************/
#include <linux/etherdevice.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <net/mac80211.h>

#include "iwl-io.h"
#include "iwl-agn-hw.h"
#include "iwl-trans.h"
#include "iwl-modparams.h"

#include "dev.h"
#include "agn.h"

int iwlagn_hw_valid_rtc_data_addr(u32 addr)
{
	return (addr >= IWLAGN_RTC_DATA_LOWER_BOUND) &&
		(addr < IWLAGN_RTC_DATA_UPPER_BOUND);
}

int iwlagn_send_tx_power(struct iwl_priv *priv)
{
	struct iwlagn_tx_power_dbm_cmd tx_power_cmd;
	u8 tx_ant_cfg_cmd;

	if (WARN_ONCE(test_bit(STATUS_SCAN_HW, &priv->status),
		      "TX Power requested while scanning!\n"))
		return -EAGAIN;

	/* half dBm need to multiply */
	tx_power_cmd.global_lmt = (s8)(2 * priv->tx_power_user_lmt);

	if (tx_power_cmd.global_lmt > priv->nvm_data->max_tx_pwr_half_dbm) {
		/*
		 * For the newer devices which using enhanced/extend tx power
		 * table in EEPROM, the format is in half dBm. driver need to
		 * convert to dBm format before report to mac80211.
		 * By doing so, there is a possibility of 1/2 dBm resolution
		 * lost. driver will perform "round-up" operation before
		 * reporting, but it will cause 1/2 dBm tx power over the
		 * regulatory limit. Perform the checking here, if the
		 * "tx_power_user_lmt" is higher than EEPROM value (in
		 * half-dBm format), lower the tx power based on EEPROM
		 */
		tx_power_cmd.global_lmt =
			priv->nvm_data->max_tx_pwr_half_dbm;
	}
	tx_power_cmd.flags = IWLAGN_TX_POWER_NO_CLOSED;
	tx_power_cmd.srv_chan_lmt = IWLAGN_TX_POWER_AUTO;

	if (IWL_UCODE_API(priv->fw->ucode_ver) == 1)
		tx_ant_cfg_cmd = REPLY_TX_POWER_DBM_CMD_V1;
	else
		tx_ant_cfg_cmd = REPLY_TX_POWER_DBM_CMD;

	return iwl_dvm_send_cmd_pdu(priv, tx_ant_cfg_cmd, CMD_SYNC,
			sizeof(tx_power_cmd), &tx_power_cmd);
}

void iwlagn_temperature(struct iwl_priv *priv)
{
	lockdep_assert_held(&priv->statistics.lock);

	/* store temperature from correct statistics (in Celsius) */
	priv->temperature = le32_to_cpu(priv->statistics.common.temperature);
	iwl_tt_handler(priv);
}

int iwlagn_hwrate_to_mac80211_idx(u32 rate_n_flags, enum ieee80211_band band)
{
	int idx = 0;
	int band_offset = 0;

	/* HT rate format: mac80211 wants an MCS number, which is just LSB */
	if (rate_n_flags & RATE_MCS_HT_MSK) {
		idx = (rate_n_flags & 0xff);
		return idx;
	/* Legacy rate format, search for match in table */
	} else {
		if (band == IEEE80211_BAND_5GHZ)
			band_offset = IWL_FIRST_OFDM_RATE;
		for (idx = band_offset; idx < IWL_RATE_COUNT_LEGACY; idx++)
			if (iwl_rates[idx].plcp == (rate_n_flags & 0xFF))
				return idx - band_offset;
	}

	return -1;
}

int iwlagn_manage_ibss_station(struct iwl_priv *priv,
			       struct ieee80211_vif *vif, bool add)
{
	struct iwl_vif_priv *vif_priv = (void *)vif->drv_priv;

	if (add)
		return iwlagn_add_bssid_station(priv, vif_priv->ctx,
						vif->bss_conf.bssid,
						&vif_priv->ibss_bssid_sta_id);
	return iwl_remove_station(priv, vif_priv->ibss_bssid_sta_id,
				  vif->bss_conf.bssid);
}

/**
 * iwlagn_txfifo_flush: send REPLY_TXFIFO_FLUSH command to uCode
 *
 * pre-requirements:
 *  1. acquire mutex before calling
 *  2. make sure rf is on and not in exit state
 */
int iwlagn_txfifo_flush(struct iwl_priv *priv, u32 scd_q_msk)
{
	struct iwl_txfifo_flush_cmd flush_cmd;
	struct iwl_host_cmd cmd = {
		.id = REPLY_TXFIFO_FLUSH,
		.len = { sizeof(struct iwl_txfifo_flush_cmd), },
		.flags = CMD_SYNC,
		.data = { &flush_cmd, },
	};

	memset(&flush_cmd, 0, sizeof(flush_cmd));

	flush_cmd.queue_control = IWL_SCD_VO_MSK | IWL_SCD_VI_MSK |
				  IWL_SCD_BE_MSK | IWL_SCD_BK_MSK |
				  IWL_SCD_MGMT_MSK;
	if ((priv->valid_contexts != BIT(IWL_RXON_CTX_BSS)))
		flush_cmd.queue_control |= IWL_PAN_SCD_VO_MSK |
					   IWL_PAN_SCD_VI_MSK |
					   IWL_PAN_SCD_BE_MSK |
					   IWL_PAN_SCD_BK_MSK |
					   IWL_PAN_SCD_MGMT_MSK |
					   IWL_PAN_SCD_MULTICAST_MSK;

	if (priv->nvm_data->sku_cap_11n_enable)
		flush_cmd.queue_control |= IWL_AGG_TX_QUEUE_MSK;

	if (scd_q_msk)
		flush_cmd.queue_control = cpu_to_le32(scd_q_msk);

	IWL_DEBUG_INFO(priv, "queue control: 0x%x\n",
		       flush_cmd.queue_control);
	flush_cmd.flush_control = cpu_to_le16(IWL_DROP_ALL);

	return iwl_dvm_send_cmd(priv, &cmd);
}

void iwlagn_dev_txfifo_flush(struct iwl_priv *priv)
{
	mutex_lock(&priv->mutex);
	ieee80211_stop_queues(priv->hw);
	if (iwlagn_txfifo_flush(priv, 0)) {
		IWL_ERR(priv, "flush request fail\n");
		goto done;
	}
	IWL_DEBUG_INFO(priv, "wait transmit/flush all frames\n");
	iwl_trans_wait_tx_queue_empty(priv->trans);
done:
	ieee80211_wake_queues(priv->hw);
	mutex_unlock(&priv->mutex);
}

/*
 * BT coex
 */
/* Notmal TDM */
static const __le32 iwlagn_def_3w_lookup[IWLAGN_BT_DECISION_LUT_SIZE] = {
	cpu_to_le32(0xaaaaaaaa),
	cpu_to_le32(0xaaaaaaaa),
	cpu_to_le32(0xaeaaaaaa),
	cpu_to_le32(0xaaaaaaaa),
	cpu_to_le32(0xcc00ff28),
	cpu_to_le32(0x0000aaaa),
	cpu_to_le32(0xcc00aaaa),
	cpu_to_le32(0x0000aaaa),
	cpu_to_le32(0xc0004000),
	cpu_to_le32(0x00004000),
	cpu_to_le32(0xf0005000),
	cpu_to_le32(0xf0005000),
};


/* Loose Coex */
static const __le32 iwlagn_loose_lookup[IWLAGN_BT_DECISION_LUT_SIZE] = {
	cpu_to_le32(0xaaaaaaaa),
	cpu_to_le32(0xaaaaaaaa),
	cpu_to_le32(0xaeaaaaaa),
	cpu_to_le32(0xaaaaaaaa),
	cpu_to_le32(0xcc00ff28),
	cpu_to_le32(0x0000aaaa),
	cpu_to_le32(0xcc00aaaa),
	cpu_to_le32(0x0000aaaa),
	cpu_to_le32(0x00000000),
	cpu_to_le32(0x00000000),
	cpu_to_le32(0xf0005000),
	cpu_to_le32(0xf0005000),
};

/* Full concurrency */
static const __le32 iwlagn_concurrent_lookup[IWLAGN_BT_DECISION_LUT_SIZE] = {
	cpu_to_le32(0xaaaaaaaa),
	cpu_to_le32(0xaaaaaaaa),
	cpu_to_le32(0xaaaaaaaa),
	cpu_to_le32(0xaaaaaaaa),
	cpu_to_le32(0xaaaaaaaa),
	cpu_to_le32(0xaaaaaaaa),
	cpu_to_le32(0xaaaaaaaa),
	cpu_to_le32(0xaaaaaaaa),
	cpu_to_le32(0x00000000),
	cpu_to_le32(0x00000000),
	cpu_to_le32(0x00000000),
	cpu_to_le32(0x00000000),
};

void iwlagn_send_advance_bt_config(struct iwl_priv *priv)
{
	struct iwl_basic_bt_cmd basic = {
		.max_kill = IWLAGN_BT_MAX_KILL_DEFAULT,
		.bt3_timer_t7_value = IWLAGN_BT3_T7_DEFAULT,
		.bt3_prio_sample_time = IWLAGN_BT3_PRIO_SAMPLE_DEFAULT,
		.bt3_timer_t2_value = IWLAGN_BT3_T2_DEFAULT,
	};
	struct iwl_bt_cmd_v1 bt_cmd_v1;
	struct iwl_bt_cmd_v2 bt_cmd_v2;
	int ret;

	BUILD_BUG_ON(sizeof(iwlagn_def_3w_lookup) !=
			sizeof(basic.bt3_lookup_table));

	if (priv->lib->bt_params) {
		/*
		 * newer generation of devices (2000 series and newer)
		 * use the version 2 of the bt command
		 * we need to make sure sending the host command
		 * with correct data structure to avoid uCode assert
		 */
		if (priv->lib->bt_params->bt_session_2) {
			bt_cmd_v2.prio_boost = cpu_to_le32(
				priv->lib->bt_params->bt_prio_boost);
			bt_cmd_v2.tx_prio_boost = 0;
			bt_cmd_v2.rx_prio_boost = 0;
		} else {
			/* older version only has 8 bits */
			WARN_ON(priv->lib->bt_params->bt_prio_boost & ~0xFF);
			bt_cmd_v1.prio_boost =
				priv->lib->bt_params->bt_prio_boost;
			bt_cmd_v1.tx_prio_boost = 0;
			bt_cmd_v1.rx_prio_boost = 0;
		}
	} else {
		IWL_ERR(priv, "failed to construct BT Coex Config\n");
		return;
	}

	/*
	 * Possible situations when BT needs to take over for receive,
	 * at the same time where STA needs to response to AP's frame(s),
	 * reduce the tx power of the required response frames, by that,
	 * allow the concurrent BT receive & WiFi transmit
	 * (BT - ANT A, WiFi -ANT B), without interference to one another
	 *
	 * Reduced tx power apply to control frames only (ACK/Back/CTS)
	 * when indicated by the BT config command
	 */
	basic.kill_ack_mask = priv->kill_ack_mask;
	basic.kill_cts_mask = priv->kill_cts_mask;
	if (priv->reduced_txpower)
		basic.reduce_txpower = IWLAGN_BT_REDUCED_TX_PWR;
	basic.valid = priv->bt_valid;

	/*
	 * Configure BT coex mode to "no coexistence" when the
	 * user disabled BT coexistence, we have no interface
	 * (might be in monitor mode), or the interface is in
	 * IBSS mode (no proper uCode support for coex then).
	 */
	if (!iwlwifi_mod_params.bt_coex_active ||
	    priv->iw_mode == NL80211_IFTYPE_ADHOC) {
		basic.flags = IWLAGN_BT_FLAG_COEX_MODE_DISABLED;
	} else {
		basic.flags = IWLAGN_BT_FLAG_COEX_MODE_3W <<
					IWLAGN_BT_FLAG_COEX_MODE_SHIFT;

		if (!priv->bt_enable_pspoll)
			basic.flags |= IWLAGN_BT_FLAG_SYNC_2_BT_DISABLE;
		else
			basic.flags &= ~IWLAGN_BT_FLAG_SYNC_2_BT_DISABLE;

		if (priv->bt_ch_announce)
			basic.flags |= IWLAGN_BT_FLAG_CHANNEL_INHIBITION;
		IWL_DEBUG_COEX(priv, "BT coex flag: 0X%x\n", basic.flags);
	}
	priv->bt_enable_flag = basic.flags;
	if (priv->bt_full_concurrent)
		memcpy(basic.bt3_lookup_table, iwlagn_concurrent_lookup,
			sizeof(iwlagn_concurrent_lookup));
	else
		memcpy(basic.bt3_lookup_table, iwlagn_def_3w_lookup,
			sizeof(iwlagn_def_3w_lookup));

	IWL_DEBUG_COEX(priv, "BT coex %s in %s mode\n",
		       basic.flags ? "active" : "disabled",
		       priv->bt_full_concurrent ?
		       "full concurrency" : "3-wire");

	if (priv->lib->bt_params->bt_session_2) {
		memcpy(&bt_cmd_v2.basic, &basic,
			sizeof(basic));
		ret = iwl_dvm_send_cmd_pdu(priv, REPLY_BT_CONFIG,
			CMD_SYNC, sizeof(bt_cmd_v2), &bt_cmd_v2);
	} else {
		memcpy(&bt_cmd_v1.basic, &basic,
			sizeof(basic));
		ret = iwl_dvm_send_cmd_pdu(priv, REPLY_BT_CONFIG,
			CMD_SYNC, sizeof(bt_cmd_v1), &bt_cmd_v1);
	}
	if (ret)
		IWL_ERR(priv, "failed to send BT Coex Config\n");

}

void iwlagn_bt_adjust_rssi_monitor(struct iwl_priv *priv, bool rssi_ena)
{
	struct iwl_rxon_context *ctx, *found_ctx = NULL;
	bool found_ap = false;

	lockdep_assert_held(&priv->mutex);

	/* Check whether AP or GO mode is active. */
	if (rssi_ena) {
		for_each_context(priv, ctx) {
			if (ctx->vif && ctx->vif->type == NL80211_IFTYPE_AP &&
			    iwl_is_associated_ctx(ctx)) {
				found_ap = true;
				break;
			}
		}
	}

	/*
	 * If disable was received or If GO/AP mode, disable RSSI
	 * measurements.
	 */
	if (!rssi_ena || found_ap) {
		if (priv->cur_rssi_ctx) {
			ctx = priv->cur_rssi_ctx;
			ieee80211_disable_rssi_reports(ctx->vif);
			priv->cur_rssi_ctx = NULL;
		}
		return;
	}

	/*
	 * If rssi measurements need to be enabled, consider all cases now.
	 * Figure out how many contexts are active.
	 */
	for_each_context(priv, ctx) {
		if (ctx->vif && ctx->vif->type == NL80211_IFTYPE_STATION &&
		    iwl_is_associated_ctx(ctx)) {
			found_ctx = ctx;
			break;
		}
	}

	/*
	 * rssi monitor already enabled for the correct interface...nothing
	 * to do.
	 */
	if (found_ctx == priv->cur_rssi_ctx)
		return;

	/*
	 * Figure out if rssi monitor is currently enabled, and needs
	 * to be changed. If rssi monitor is already enabled, disable
	 * it first else just enable rssi measurements on the
	 * interface found above.
	 */
	if (priv->cur_rssi_ctx) {
		ctx = priv->cur_rssi_ctx;
		if (ctx->vif)
			ieee80211_disable_rssi_reports(ctx->vif);
	}

	priv->cur_rssi_ctx = found_ctx;

	if (!found_ctx)
		return;

	ieee80211_enable_rssi_reports(found_ctx->vif,
			IWLAGN_BT_PSP_MIN_RSSI_THRESHOLD,
			IWLAGN_BT_PSP_MAX_RSSI_THRESHOLD);
}

static bool iwlagn_bt_traffic_is_sco(struct iwl_bt_uart_msg *uart_msg)
{
	return BT_UART_MSG_FRAME3SCOESCO_MSK & uart_msg->frame3 >>
			BT_UART_MSG_FRAME3SCOESCO_POS;
}

static void iwlagn_bt_traffic_change_work(struct work_struct *work)
{
	struct iwl_priv *priv =
		container_of(work, struct iwl_priv, bt_traffic_change_work);
	struct iwl_rxon_context *ctx;
	int smps_request = -1;

	if (priv->bt_enable_flag == IWLAGN_BT_FLAG_COEX_MODE_DISABLED) {
		/* bt coex disabled */
		return;
	}

	/*
	 * Note: bt_traffic_load can be overridden by scan complete and
	 * coex profile notifications. Ignore that since only bad consequence
	 * can be not matching debug print with actual state.
	 */
	IWL_DEBUG_COEX(priv, "BT traffic load changes: %d\n",
		       priv->bt_traffic_load);

	switch (priv->bt_traffic_load) {
	case IWL_BT_COEX_TRAFFIC_LOAD_NONE:
		if (priv->bt_status)
			smps_request = IEEE80211_SMPS_DYNAMIC;
		else
			smps_request = IEEE80211_SMPS_AUTOMATIC;
		break;
	case IWL_BT_COEX_TRAFFIC_LOAD_LOW:
		smps_request = IEEE80211_SMPS_DYNAMIC;
		break;
	case IWL_BT_COEX_TRAFFIC_LOAD_HIGH:
	case IWL_BT_COEX_TRAFFIC_LOAD_CONTINUOUS:
		smps_request = IEEE80211_SMPS_STATIC;
		break;
	default:
		IWL_ERR(priv, "Invalid BT traffic load: %d\n",
			priv->bt_traffic_load);
		break;
	}

	mutex_lock(&priv->mutex);

	/*
	 * We can not send command to firmware while scanning. When the scan
	 * complete we will schedule this work again. We do check with mutex
	 * locked to prevent new scan request to arrive. We do not check
	 * STATUS_SCANNING to avoid race when queue_work two times from
	 * different notifications, but quit and not perform any work at all.
	 */
	if (test_bit(STATUS_SCAN_HW, &priv->status))
		goto out;

	iwl_update_chain_flags(priv);

	if (smps_request != -1) {
		priv->current_ht_config.smps = smps_request;
		for_each_context(priv, ctx) {
			if (ctx->vif && ctx->vif->type == NL80211_IFTYPE_STATION)
				ieee80211_request_smps(ctx->vif, smps_request);
		}
	}

	/*
	 * Dynamic PS poll related functionality. Adjust RSSI measurements if
	 * necessary.
	 */
	iwlagn_bt_coex_rssi_monitor(priv);
out:
	mutex_unlock(&priv->mutex);
}

/*
 * If BT sco traffic, and RSSI monitor is enabled, move measurements to the
 * correct interface or disable it if this is the last interface to be
 * removed.
 */
void iwlagn_bt_coex_rssi_monitor(struct iwl_priv *priv)
{
	if (priv->bt_is_sco &&
	    priv->bt_traffic_load == IWL_BT_COEX_TRAFFIC_LOAD_CONTINUOUS)
		iwlagn_bt_adjust_rssi_monitor(priv, true);
	else
		iwlagn_bt_adjust_rssi_monitor(priv, false);
}

static void iwlagn_print_uartmsg(struct iwl_priv *priv,
				struct iwl_bt_uart_msg *uart_msg)
{
	IWL_DEBUG_COEX(priv, "Message Type = 0x%X, SSN = 0x%X, "
			"Update Req = 0x%X\n",
		(BT_UART_MSG_FRAME1MSGTYPE_MSK & uart_msg->frame1) >>
			BT_UART_MSG_FRAME1MSGTYPE_POS,
		(BT_UART_MSG_FRAME1SSN_MSK & uart_msg->frame1) >>
			BT_UART_MSG_FRAME1SSN_POS,
		(BT_UART_MSG_FRAME1UPDATEREQ_MSK & uart_msg->frame1) >>
			BT_UART_MSG_FRAME1UPDATEREQ_POS);

	IWL_DEBUG_COEX(priv, "Open connections = 0x%X, Traffic load = 0x%X, "
			"Chl_SeqN = 0x%X, In band = 0x%X\n",
		(BT_UART_MSG_FRAME2OPENCONNECTIONS_MSK & uart_msg->frame2) >>
			BT_UART_MSG_FRAME2OPENCONNECTIONS_POS,
		(BT_UART_MSG_FRAME2TRAFFICLOAD_MSK & uart_msg->frame2) >>
			BT_UART_MSG_FRAME2TRAFFICLOAD_POS,
		(BT_UART_MSG_FRAME2CHLSEQN_MSK & uart_msg->frame2) >>
			BT_UART_MSG_FRAME2CHLSEQN_POS,
		(BT_UART_MSG_FRAME2INBAND_MSK & uart_msg->frame2) >>
			BT_UART_MSG_FRAME2INBAND_POS);

	IWL_DEBUG_COEX(priv, "SCO/eSCO = 0x%X, Sniff = 0x%X, A2DP = 0x%X, "
			"ACL = 0x%X, Master = 0x%X, OBEX = 0x%X\n",
		(BT_UART_MSG_FRAME3SCOESCO_MSK & uart_msg->frame3) >>
			BT_UART_MSG_FRAME3SCOESCO_POS,
		(BT_UART_MSG_FRAME3SNIFF_MSK & uart_msg->frame3) >>
			BT_UART_MSG_FRAME3SNIFF_POS,
		(BT_UART_MSG_FRAME3A2DP_MSK & uart_msg->frame3) >>
			BT_UART_MSG_FRAME3A2DP_POS,
		(BT_UART_MSG_FRAME3ACL_MSK & uart_msg->frame3) >>
			BT_UART_MSG_FRAME3ACL_POS,
		(BT_UART_MSG_FRAME3MASTER_MSK & uart_msg->frame3) >>
			BT_UART_MSG_FRAME3MASTER_POS,
		(BT_UART_MSG_FRAME3OBEX_MSK & uart_msg->frame3) >>
			BT_UART_MSG_FRAME3OBEX_POS);

	IWL_DEBUG_COEX(priv, "Idle duration = 0x%X\n",
		(BT_UART_MSG_FRAME4IDLEDURATION_MSK & uart_msg->frame4) >>
			BT_UART_MSG_FRAME4IDLEDURATION_POS);

	IWL_DEBUG_COEX(priv, "Tx Activity = 0x%X, Rx Activity = 0x%X, "
			"eSCO Retransmissions = 0x%X\n",
		(BT_UART_MSG_FRAME5TXACTIVITY_MSK & uart_msg->frame5) >>
			BT_UART_MSG_FRAME5TXACTIVITY_POS,
		(BT_UART_MSG_FRAME5RXACTIVITY_MSK & uart_msg->frame5) >>
			BT_UART_MSG_FRAME5RXACTIVITY_POS,
		(BT_UART_MSG_FRAME5ESCORETRANSMIT_MSK & uart_msg->frame5) >>
			BT_UART_MSG_FRAME5ESCORETRANSMIT_POS);

	IWL_DEBUG_COEX(priv, "Sniff Interval = 0x%X, Discoverable = 0x%X\n",
		(BT_UART_MSG_FRAME6SNIFFINTERVAL_MSK & uart_msg->frame6) >>
			BT_UART_MSG_FRAME6SNIFFINTERVAL_POS,
		(BT_UART_MSG_FRAME6DISCOVERABLE_MSK & uart_msg->frame6) >>
			BT_UART_MSG_FRAME6DISCOVERABLE_POS);

	IWL_DEBUG_COEX(priv, "Sniff Activity = 0x%X, Page = "
			"0x%X, Inquiry = 0x%X, Connectable = 0x%X\n",
		(BT_UART_MSG_FRAME7SNIFFACTIVITY_MSK & uart_msg->frame7) >>
			BT_UART_MSG_FRAME7SNIFFACTIVITY_POS,
		(BT_UART_MSG_FRAME7PAGE_MSK & uart_msg->frame7) >>
			BT_UART_MSG_FRAME7PAGE_POS,
		(BT_UART_MSG_FRAME7INQUIRY_MSK & uart_msg->frame7) >>
			BT_UART_MSG_FRAME7INQUIRY_POS,
		(BT_UART_MSG_FRAME7CONNECTABLE_MSK & uart_msg->frame7) >>
			BT_UART_MSG_FRAME7CONNECTABLE_POS);
}

static bool iwlagn_set_kill_msk(struct iwl_priv *priv,
				struct iwl_bt_uart_msg *uart_msg)
{
	bool need_update = false;
	u8 kill_msk = IWL_BT_KILL_REDUCE;
	static const __le32 bt_kill_ack_msg[3] = {
		IWLAGN_BT_KILL_ACK_MASK_DEFAULT,
		IWLAGN_BT_KILL_ACK_CTS_MASK_SCO,
		IWLAGN_BT_KILL_ACK_CTS_MASK_REDUCE};
	static const __le32 bt_kill_cts_msg[3] = {
		IWLAGN_BT_KILL_CTS_MASK_DEFAULT,
		IWLAGN_BT_KILL_ACK_CTS_MASK_SCO,
		IWLAGN_BT_KILL_ACK_CTS_MASK_REDUCE};

	if (!priv->reduced_txpower)
		kill_msk = (BT_UART_MSG_FRAME3SCOESCO_MSK & uart_msg->frame3)
			? IWL_BT_KILL_OVERRIDE : IWL_BT_KILL_DEFAULT;
	if (priv->kill_ack_mask != bt_kill_ack_msg[kill_msk] ||
	    priv->kill_cts_mask != bt_kill_cts_msg[kill_msk]) {
		priv->bt_valid |= IWLAGN_BT_VALID_KILL_ACK_MASK;
		priv->kill_ack_mask = bt_kill_ack_msg[kill_msk];
		priv->bt_valid |= IWLAGN_BT_VALID_KILL_CTS_MASK;
		priv->kill_cts_mask = bt_kill_cts_msg[kill_msk];
		need_update = true;
	}
	return need_update;
}

/*
 * Upon RSSI changes, sends a bt config command with following changes
 *  1. enable/disable "reduced control frames tx power
 *  2. update the "kill)ack_mask" and "kill_cts_mask"
 *
 * If "reduced tx power" is enabled, uCode shall
 *  1. ACK/Back/CTS rate shall reduced to 6Mbps
 *  2. not use duplciate 20/40MHz mode
 */
static bool iwlagn_fill_txpower_mode(struct iwl_priv *priv,
				struct iwl_bt_uart_msg *uart_msg)
{
	bool need_update = false;
	struct iwl_rxon_context *ctx = &priv->contexts[IWL_RXON_CTX_BSS];
	int ave_rssi;

	if (!ctx->vif || (ctx->vif->type != NL80211_IFTYPE_STATION)) {
		IWL_DEBUG_INFO(priv, "BSS ctx not active or not in sta mode\n");
		return false;
	}

	ave_rssi = ieee80211_ave_rssi(ctx->vif);
	if (!ave_rssi) {
		/* no rssi data, no changes to reduce tx power */
		IWL_DEBUG_COEX(priv, "no rssi data available\n");
		return need_update;
	}
	if (!priv->reduced_txpower &&
	    !iwl_is_associated(priv, IWL_RXON_CTX_PAN) &&
	    (ave_rssi > BT_ENABLE_REDUCED_TXPOWER_THRESHOLD) &&
	    (uart_msg->frame3 & (BT_UART_MSG_FRAME3ACL_MSK |
	    BT_UART_MSG_FRAME3OBEX_MSK)) &&
	    !(uart_msg->frame3 & (BT_UART_MSG_FRAME3SCOESCO_MSK |
	    BT_UART_MSG_FRAME3SNIFF_MSK | BT_UART_MSG_FRAME3A2DP_MSK))) {
		/* enabling reduced tx power */
		priv->reduced_txpower = true;
		priv->bt_valid |= IWLAGN_BT_VALID_REDUCED_TX_PWR;
		need_update = true;
	} else if (priv->reduced_txpower &&
		   (iwl_is_associated(priv, IWL_RXON_CTX_PAN) ||
		   (ave_rssi < BT_DISABLE_REDUCED_TXPOWER_THRESHOLD) ||
		   (uart_msg->frame3 & (BT_UART_MSG_FRAME3SCOESCO_MSK |
		   BT_UART_MSG_FRAME3SNIFF_MSK | BT_UART_MSG_FRAME3A2DP_MSK)) ||
		   !(uart_msg->frame3 & (BT_UART_MSG_FRAME3ACL_MSK |
		   BT_UART_MSG_FRAME3OBEX_MSK)))) {
		/* disable reduced tx power */
		priv->reduced_txpower = false;
		priv->bt_valid |= IWLAGN_BT_VALID_REDUCED_TX_PWR;
		need_update = true;
	}

	return need_update;
}

int iwlagn_bt_coex_profile_notif(struct iwl_priv *priv,
				  struct iwl_rx_cmd_buffer *rxb,
				  struct iwl_device_cmd *cmd)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_bt_coex_profile_notif *coex = (void *)pkt->data;
	struct iwl_bt_uart_msg *uart_msg = &coex->last_bt_uart_msg;

	if (priv->bt_enable_flag == IWLAGN_BT_FLAG_COEX_MODE_DISABLED) {
		/* bt coex disabled */
		return 0;
	}

	IWL_DEBUG_COEX(priv, "BT Coex notification:\n");
	IWL_DEBUG_COEX(priv, "    status: %d\n", coex->bt_status);
	IWL_DEBUG_COEX(priv, "    traffic load: %d\n", coex->bt_traffic_load);
	IWL_DEBUG_COEX(priv, "    CI compliance: %d\n",
			coex->bt_ci_compliance);
	iwlagn_print_uartmsg(priv, uart_msg);

	priv->last_bt_traffic_load = priv->bt_traffic_load;
	priv->bt_is_sco = iwlagn_bt_traffic_is_sco(uart_msg);

	if (priv->iw_mode != NL80211_IFTYPE_ADHOC) {
		if (priv->bt_status != coex->bt_status ||
		    priv->last_bt_traffic_load != coex->bt_traffic_load) {
			if (coex->bt_status) {
				/* BT on */
				if (!priv->bt_ch_announce)
					priv->bt_traffic_load =
						IWL_BT_COEX_TRAFFIC_LOAD_HIGH;
				else
					priv->bt_traffic_load =
						coex->bt_traffic_load;
			} else {
				/* BT off */
				priv->bt_traffic_load =
					IWL_BT_COEX_TRAFFIC_LOAD_NONE;
			}
			priv->bt_status = coex->bt_status;
			queue_work(priv->workqueue,
				   &priv->bt_traffic_change_work);
		}
	}

	/* schedule to send runtime bt_config */
	/* check reduce power before change ack/cts kill mask */
	if (iwlagn_fill_txpower_mode(priv, uart_msg) ||
	    iwlagn_set_kill_msk(priv, uart_msg))
		queue_work(priv->workqueue, &priv->bt_runtime_config);


	/* FIXME: based on notification, adjust the prio_boost */

	priv->bt_ci_compliance = coex->bt_ci_compliance;
	return 0;
}

void iwlagn_bt_rx_handler_setup(struct iwl_priv *priv)
{
	priv->rx_handlers[REPLY_BT_COEX_PROFILE_NOTIF] =
		iwlagn_bt_coex_profile_notif;
}

void iwlagn_bt_setup_deferred_work(struct iwl_priv *priv)
{
	INIT_WORK(&priv->bt_traffic_change_work,
		  iwlagn_bt_traffic_change_work);
}

void iwlagn_bt_cancel_deferred_work(struct iwl_priv *priv)
{
	cancel_work_sync(&priv->bt_traffic_change_work);
}

static bool is_single_rx_stream(struct iwl_priv *priv)
{
	return priv->current_ht_config.smps == IEEE80211_SMPS_STATIC ||
	       priv->current_ht_config.single_chain_sufficient;
}

#define IWL_NUM_RX_CHAINS_MULTIPLE	3
#define IWL_NUM_RX_CHAINS_SINGLE	2
#define IWL_NUM_IDLE_CHAINS_DUAL	2
#define IWL_NUM_IDLE_CHAINS_SINGLE	1

/*
 * Determine how many receiver/antenna chains to use.
 *
 * More provides better reception via diversity.  Fewer saves power
 * at the expense of throughput, but only when not in powersave to
 * start with.
 *
 * MIMO (dual stream) requires at least 2, but works better with 3.
 * This does not determine *which* chains to use, just how many.
 */
static int iwl_get_active_rx_chain_count(struct iwl_priv *priv)
{
	if (priv->lib->bt_params &&
	    priv->lib->bt_params->advanced_bt_coexist &&
	    (priv->bt_full_concurrent ||
	     priv->bt_traffic_load >= IWL_BT_COEX_TRAFFIC_LOAD_HIGH)) {
		/*
		 * only use chain 'A' in bt high traffic load or
		 * full concurrency mode
		 */
		return IWL_NUM_RX_CHAINS_SINGLE;
	}
	/* # of Rx chains to use when expecting MIMO. */
	if (is_single_rx_stream(priv))
		return IWL_NUM_RX_CHAINS_SINGLE;
	else
		return IWL_NUM_RX_CHAINS_MULTIPLE;
}

/*
 * When we are in power saving mode, unless device support spatial
 * multiplexing power save, use the active count for rx chain count.
 */
static int iwl_get_idle_rx_chain_count(struct iwl_priv *priv, int active_cnt)
{
	/* # Rx chains when idling, depending on SMPS mode */
	switch (priv->current_ht_config.smps) {
	case IEEE80211_SMPS_STATIC:
	case IEEE80211_SMPS_DYNAMIC:
		return IWL_NUM_IDLE_CHAINS_SINGLE;
	case IEEE80211_SMPS_AUTOMATIC:
	case IEEE80211_SMPS_OFF:
		return active_cnt;
	default:
		WARN(1, "invalid SMPS mode %d",
		     priv->current_ht_config.smps);
		return active_cnt;
	}
}

/* up to 4 chains */
static u8 iwl_count_chain_bitmap(u32 chain_bitmap)
{
	u8 res;
	res = (chain_bitmap & BIT(0)) >> 0;
	res += (chain_bitmap & BIT(1)) >> 1;
	res += (chain_bitmap & BIT(2)) >> 2;
	res += (chain_bitmap & BIT(3)) >> 3;
	return res;
}

/**
 * iwlagn_set_rxon_chain - Set up Rx chain usage in "staging" RXON image
 *
 * Selects how many and which Rx receivers/antennas/chains to use.
 * This should not be used for scan command ... it puts data in wrong place.
 */
void iwlagn_set_rxon_chain(struct iwl_priv *priv, struct iwl_rxon_context *ctx)
{
	bool is_single = is_single_rx_stream(priv);
	bool is_cam = !test_bit(STATUS_POWER_PMI, &priv->status);
	u8 idle_rx_cnt, active_rx_cnt, valid_rx_cnt;
	u32 active_chains;
	u16 rx_chain;

	/* Tell uCode which antennas are actually connected.
	 * Before first association, we assume all antennas are connected.
	 * Just after first association, iwl_chain_noise_calibration()
	 *    checks which antennas actually *are* connected. */
	if (priv->chain_noise_data.active_chains)
		active_chains = priv->chain_noise_data.active_chains;
	else
		active_chains = priv->nvm_data->valid_rx_ant;

	if (priv->lib->bt_params &&
	    priv->lib->bt_params->advanced_bt_coexist &&
	    (priv->bt_full_concurrent ||
	     priv->bt_traffic_load >= IWL_BT_COEX_TRAFFIC_LOAD_HIGH)) {
		/*
		 * only use chain 'A' in bt high traffic load or
		 * full concurrency mode
		 */
		active_chains = first_antenna(active_chains);
	}

	rx_chain = active_chains << RXON_RX_CHAIN_VALID_POS;

	/* How many receivers should we use? */
	active_rx_cnt = iwl_get_active_rx_chain_count(priv);
	idle_rx_cnt = iwl_get_idle_rx_chain_count(priv, active_rx_cnt);


	/* correct rx chain count according hw settings
	 * and chain noise calibration
	 */
	valid_rx_cnt = iwl_count_chain_bitmap(active_chains);
	if (valid_rx_cnt < active_rx_cnt)
		active_rx_cnt = valid_rx_cnt;

	if (valid_rx_cnt < idle_rx_cnt)
		idle_rx_cnt = valid_rx_cnt;

	rx_chain |= active_rx_cnt << RXON_RX_CHAIN_MIMO_CNT_POS;
	rx_chain |= idle_rx_cnt  << RXON_RX_CHAIN_CNT_POS;

	ctx->staging.rx_chain = cpu_to_le16(rx_chain);

	if (!is_single && (active_rx_cnt >= IWL_NUM_RX_CHAINS_SINGLE) && is_cam)
		ctx->staging.rx_chain |= RXON_RX_CHAIN_MIMO_FORCE_MSK;
	else
		ctx->staging.rx_chain &= ~RXON_RX_CHAIN_MIMO_FORCE_MSK;

	IWL_DEBUG_ASSOC(priv, "rx_chain=0x%X active=%d idle=%d\n",
			ctx->staging.rx_chain,
			active_rx_cnt, idle_rx_cnt);

	WARN_ON(active_rx_cnt == 0 || idle_rx_cnt == 0 ||
		active_rx_cnt < idle_rx_cnt);
}

u8 iwl_toggle_tx_ant(struct iwl_priv *priv, u8 ant, u8 valid)
{
	int i;
	u8 ind = ant;

	if (priv->band == IEEE80211_BAND_2GHZ &&
	    priv->bt_traffic_load >= IWL_BT_COEX_TRAFFIC_LOAD_HIGH)
		return 0;

	for (i = 0; i < RATE_ANT_NUM - 1; i++) {
		ind = (ind + 1) < RATE_ANT_NUM ?  ind + 1 : 0;
		if (valid & BIT(ind))
			return ind;
	}
	return ant;
}

#ifdef CONFIG_PM_SLEEP
static void iwlagn_convert_p1k(u16 *p1k, __le16 *out)
{
	int i;

	for (i = 0; i < IWLAGN_P1K_SIZE; i++)
		out[i] = cpu_to_le16(p1k[i]);
}

struct wowlan_key_data {
	struct iwl_rxon_context *ctx;
	struct iwlagn_wowlan_rsc_tsc_params_cmd *rsc_tsc;
	struct iwlagn_wowlan_tkip_params_cmd *tkip;
	const u8 *bssid;
	bool error, use_rsc_tsc, use_tkip;
};


static void iwlagn_wowlan_program_keys(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif,
			       struct ieee80211_sta *sta,
			       struct ieee80211_key_conf *key,
			       void *_data)
{
	struct iwl_priv *priv = IWL_MAC80211_GET_DVM(hw);
	struct wowlan_key_data *data = _data;
	struct iwl_rxon_context *ctx = data->ctx;
	struct aes_sc *aes_sc, *aes_tx_sc = NULL;
	struct tkip_sc *tkip_sc, *tkip_tx_sc = NULL;
	struct iwlagn_p1k_cache *rx_p1ks;
	u8 *rx_mic_key;
	struct ieee80211_key_seq seq;
	u32 cur_rx_iv32 = 0;
	u16 p1k[IWLAGN_P1K_SIZE];
	int ret, i;

	mutex_lock(&priv->mutex);

	if ((key->cipher == WLAN_CIPHER_SUITE_WEP40 ||
	     key->cipher == WLAN_CIPHER_SUITE_WEP104) &&
	     !sta && !ctx->key_mapping_keys)
		ret = iwl_set_default_wep_key(priv, ctx, key);
	else
		ret = iwl_set_dynamic_key(priv, ctx, key, sta);

	if (ret) {
		IWL_ERR(priv, "Error setting key during suspend!\n");
		data->error = true;
	}

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_TKIP:
		if (sta) {
			tkip_sc = data->rsc_tsc->all_tsc_rsc.tkip.unicast_rsc;
			tkip_tx_sc = &data->rsc_tsc->all_tsc_rsc.tkip.tsc;

			rx_p1ks = data->tkip->rx_uni;

			ieee80211_get_key_tx_seq(key, &seq);
			tkip_tx_sc->iv16 = cpu_to_le16(seq.tkip.iv16);
			tkip_tx_sc->iv32 = cpu_to_le32(seq.tkip.iv32);

			ieee80211_get_tkip_p1k_iv(key, seq.tkip.iv32, p1k);
			iwlagn_convert_p1k(p1k, data->tkip->tx.p1k);

			memcpy(data->tkip->mic_keys.tx,
			       &key->key[NL80211_TKIP_DATA_OFFSET_TX_MIC_KEY],
			       IWLAGN_MIC_KEY_SIZE);

			rx_mic_key = data->tkip->mic_keys.rx_unicast;
		} else {
			tkip_sc =
				data->rsc_tsc->all_tsc_rsc.tkip.multicast_rsc;
			rx_p1ks = data->tkip->rx_multi;
			rx_mic_key = data->tkip->mic_keys.rx_mcast;
		}

		/*
		 * For non-QoS this relies on the fact that both the uCode and
		 * mac80211 use TID 0 (as they need to to avoid replay attacks)
		 * for checking the IV in the frames.
		 */
		for (i = 0; i < IWLAGN_NUM_RSC; i++) {
			ieee80211_get_key_rx_seq(key, i, &seq);
			tkip_sc[i].iv16 = cpu_to_le16(seq.tkip.iv16);
			tkip_sc[i].iv32 = cpu_to_le32(seq.tkip.iv32);
			/* wrapping isn't allowed, AP must rekey */
			if (seq.tkip.iv32 > cur_rx_iv32)
				cur_rx_iv32 = seq.tkip.iv32;
		}

		ieee80211_get_tkip_rx_p1k(key, data->bssid, cur_rx_iv32, p1k);
		iwlagn_convert_p1k(p1k, rx_p1ks[0].p1k);
		ieee80211_get_tkip_rx_p1k(key, data->bssid,
					  cur_rx_iv32 + 1, p1k);
		iwlagn_convert_p1k(p1k, rx_p1ks[1].p1k);

		memcpy(rx_mic_key,
		       &key->key[NL80211_TKIP_DATA_OFFSET_RX_MIC_KEY],
		       IWLAGN_MIC_KEY_SIZE);

		data->use_tkip = true;
		data->use_rsc_tsc = true;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		if (sta) {
			u8 *pn = seq.ccmp.pn;

			aes_sc = data->rsc_tsc->all_tsc_rsc.aes.unicast_rsc;
			aes_tx_sc = &data->rsc_tsc->all_tsc_rsc.aes.tsc;

			ieee80211_get_key_tx_seq(key, &seq);
			aes_tx_sc->pn = cpu_to_le64(
					(u64)pn[5] |
					((u64)pn[4] << 8) |
					((u64)pn[3] << 16) |
					((u64)pn[2] << 24) |
					((u64)pn[1] << 32) |
					((u64)pn[0] << 40));
		} else
			aes_sc = data->rsc_tsc->all_tsc_rsc.aes.multicast_rsc;

		/*
		 * For non-QoS this relies on the fact that both the uCode and
		 * mac80211 use TID 0 for checking the IV in the frames.
		 */
		for (i = 0; i < IWLAGN_NUM_RSC; i++) {
			u8 *pn = seq.ccmp.pn;

			ieee80211_get_key_rx_seq(key, i, &seq);
			aes_sc->pn = cpu_to_le64(
					(u64)pn[5] |
					((u64)pn[4] << 8) |
					((u64)pn[3] << 16) |
					((u64)pn[2] << 24) |
					((u64)pn[1] << 32) |
					((u64)pn[0] << 40));
		}
		data->use_rsc_tsc = true;
		break;
	}

	mutex_unlock(&priv->mutex);
}

int iwlagn_send_patterns(struct iwl_priv *priv,
			struct cfg80211_wowlan *wowlan)
{
	struct iwlagn_wowlan_patterns_cmd *pattern_cmd;
	struct iwl_host_cmd cmd = {
		.id = REPLY_WOWLAN_PATTERNS,
		.dataflags[0] = IWL_HCMD_DFL_NOCOPY,
		.flags = CMD_SYNC,
	};
	int i, err;

	if (!wowlan->n_patterns)
		return 0;

	cmd.len[0] = sizeof(*pattern_cmd) +
		wowlan->n_patterns * sizeof(struct iwlagn_wowlan_pattern);

	pattern_cmd = kmalloc(cmd.len[0], GFP_KERNEL);
	if (!pattern_cmd)
		return -ENOMEM;

	pattern_cmd->n_patterns = cpu_to_le32(wowlan->n_patterns);

	for (i = 0; i < wowlan->n_patterns; i++) {
		int mask_len = DIV_ROUND_UP(wowlan->patterns[i].pattern_len, 8);

		memcpy(&pattern_cmd->patterns[i].mask,
			wowlan->patterns[i].mask, mask_len);
		memcpy(&pattern_cmd->patterns[i].pattern,
			wowlan->patterns[i].pattern,
			wowlan->patterns[i].pattern_len);
		pattern_cmd->patterns[i].mask_size = mask_len;
		pattern_cmd->patterns[i].pattern_size =
			wowlan->patterns[i].pattern_len;
	}

	cmd.data[0] = pattern_cmd;
	err = iwl_dvm_send_cmd(priv, &cmd);
	kfree(pattern_cmd);
	return err;
}

int iwlagn_suspend(struct iwl_priv *priv, struct cfg80211_wowlan *wowlan)
{
	struct iwlagn_wowlan_wakeup_filter_cmd wakeup_filter_cmd;
	struct iwl_rxon_cmd rxon;
	struct iwl_rxon_context *ctx = &priv->contexts[IWL_RXON_CTX_BSS];
	struct iwlagn_wowlan_kek_kck_material_cmd kek_kck_cmd;
	struct iwlagn_wowlan_tkip_params_cmd tkip_cmd = {};
	struct iwlagn_d3_config_cmd d3_cfg_cmd = {
		/*
		 * Program the minimum sleep time to 10 seconds, as many
		 * platforms have issues processing a wakeup signal while
		 * still being in the process of suspending.
		 */
		.min_sleep_time = cpu_to_le32(10 * 1000 * 1000),
	};
	struct wowlan_key_data key_data = {
		.ctx = ctx,
		.bssid = ctx->active.bssid_addr,
		.use_rsc_tsc = false,
		.tkip = &tkip_cmd,
		.use_tkip = false,
	};
	int ret, i;
	u16 seq;

	key_data.rsc_tsc = kzalloc(sizeof(*key_data.rsc_tsc), GFP_KERNEL);
	if (!key_data.rsc_tsc)
		return -ENOMEM;

	memset(&wakeup_filter_cmd, 0, sizeof(wakeup_filter_cmd));

	/*
	 * We know the last used seqno, and the uCode expects to know that
	 * one, it will increment before TX.
	 */
	seq = le16_to_cpu(priv->last_seq_ctl) & IEEE80211_SCTL_SEQ;
	wakeup_filter_cmd.non_qos_seq = cpu_to_le16(seq);

	/*
	 * For QoS counters, we store the one to use next, so subtract 0x10
	 * since the uCode will add 0x10 before using the value.
	 */
	for (i = 0; i < IWL_MAX_TID_COUNT; i++) {
		seq = priv->tid_data[IWL_AP_ID][i].seq_number;
		seq -= 0x10;
		wakeup_filter_cmd.qos_seq[i] = cpu_to_le16(seq);
	}

	if (wowlan->disconnect)
		wakeup_filter_cmd.enabled |=
			cpu_to_le32(IWLAGN_WOWLAN_WAKEUP_BEACON_MISS |
				    IWLAGN_WOWLAN_WAKEUP_LINK_CHANGE);
	if (wowlan->magic_pkt)
		wakeup_filter_cmd.enabled |=
			cpu_to_le32(IWLAGN_WOWLAN_WAKEUP_MAGIC_PACKET);
	if (wowlan->gtk_rekey_failure)
		wakeup_filter_cmd.enabled |=
			cpu_to_le32(IWLAGN_WOWLAN_WAKEUP_GTK_REKEY_FAIL);
	if (wowlan->eap_identity_req)
		wakeup_filter_cmd.enabled |=
			cpu_to_le32(IWLAGN_WOWLAN_WAKEUP_EAP_IDENT_REQ);
	if (wowlan->four_way_handshake)
		wakeup_filter_cmd.enabled |=
			cpu_to_le32(IWLAGN_WOWLAN_WAKEUP_4WAY_HANDSHAKE);
	if (wowlan->n_patterns)
		wakeup_filter_cmd.enabled |=
			cpu_to_le32(IWLAGN_WOWLAN_WAKEUP_PATTERN_MATCH);

	if (wowlan->rfkill_release)
		d3_cfg_cmd.wakeup_flags |=
			cpu_to_le32(IWLAGN_D3_WAKEUP_RFKILL);

	iwl_scan_cancel_timeout(priv, 200);

	memcpy(&rxon, &ctx->active, sizeof(rxon));

	priv->ucode_loaded = false;
	iwl_trans_stop_device(priv->trans);

	priv->wowlan = true;

	ret = iwl_load_ucode_wait_alive(priv, IWL_UCODE_WOWLAN);
	if (ret)
		goto out;

	/* now configure WoWLAN ucode */
	ret = iwl_alive_start(priv);
	if (ret)
		goto out;

	memcpy(&ctx->staging, &rxon, sizeof(rxon));
	ret = iwlagn_commit_rxon(priv, ctx);
	if (ret)
		goto out;

	ret = iwl_power_update_mode(priv, true);
	if (ret)
		goto out;

	if (!iwlwifi_mod_params.sw_crypto) {
		/* mark all keys clear */
		priv->ucode_key_table = 0;
		ctx->key_mapping_keys = 0;

		/*
		 * This needs to be unlocked due to lock ordering
		 * constraints. Since we're in the suspend path
		 * that isn't really a problem though.
		 */
		mutex_unlock(&priv->mutex);
		ieee80211_iter_keys(priv->hw, ctx->vif,
				    iwlagn_wowlan_program_keys,
				    &key_data);
		mutex_lock(&priv->mutex);
		if (key_data.error) {
			ret = -EIO;
			goto out;
		}

		if (key_data.use_rsc_tsc) {
			struct iwl_host_cmd rsc_tsc_cmd = {
				.id = REPLY_WOWLAN_TSC_RSC_PARAMS,
				.flags = CMD_SYNC,
				.data[0] = key_data.rsc_tsc,
				.dataflags[0] = IWL_HCMD_DFL_NOCOPY,
				.len[0] = sizeof(*key_data.rsc_tsc),
			};

			ret = iwl_dvm_send_cmd(priv, &rsc_tsc_cmd);
			if (ret)
				goto out;
		}

		if (key_data.use_tkip) {
			ret = iwl_dvm_send_cmd_pdu(priv,
						 REPLY_WOWLAN_TKIP_PARAMS,
						 CMD_SYNC, sizeof(tkip_cmd),
						 &tkip_cmd);
			if (ret)
				goto out;
		}

		if (priv->have_rekey_data) {
			memset(&kek_kck_cmd, 0, sizeof(kek_kck_cmd));
			memcpy(kek_kck_cmd.kck, priv->kck, NL80211_KCK_LEN);
			kek_kck_cmd.kck_len = cpu_to_le16(NL80211_KCK_LEN);
			memcpy(kek_kck_cmd.kek, priv->kek, NL80211_KEK_LEN);
			kek_kck_cmd.kek_len = cpu_to_le16(NL80211_KEK_LEN);
			kek_kck_cmd.replay_ctr = priv->replay_ctr;

			ret = iwl_dvm_send_cmd_pdu(priv,
						 REPLY_WOWLAN_KEK_KCK_MATERIAL,
						 CMD_SYNC, sizeof(kek_kck_cmd),
						 &kek_kck_cmd);
			if (ret)
				goto out;
		}
	}

	ret = iwl_dvm_send_cmd_pdu(priv, REPLY_D3_CONFIG, CMD_SYNC,
				     sizeof(d3_cfg_cmd), &d3_cfg_cmd);
	if (ret)
		goto out;

	ret = iwl_dvm_send_cmd_pdu(priv, REPLY_WOWLAN_WAKEUP_FILTER,
				 CMD_SYNC, sizeof(wakeup_filter_cmd),
				 &wakeup_filter_cmd);
	if (ret)
		goto out;

	ret = iwlagn_send_patterns(priv, wowlan);
 out:
	kfree(key_data.rsc_tsc);
	return ret;
}
#endif

int iwl_dvm_send_cmd(struct iwl_priv *priv, struct iwl_host_cmd *cmd)
{
	if (iwl_is_rfkill(priv) || iwl_is_ctkill(priv)) {
		IWL_WARN(priv, "Not sending command - %s KILL\n",
			 iwl_is_rfkill(priv) ? "RF" : "CT");
		return -EIO;
	}

	if (test_bit(STATUS_FW_ERROR, &priv->status)) {
		IWL_ERR(priv, "Command %s failed: FW Error\n",
			iwl_dvm_get_cmd_string(cmd->id));
		return -EIO;
	}

	/*
	 * This can happen upon FW ASSERT: we clear the STATUS_FW_ERROR flag
	 * in iwl_down but cancel the workers only later.
	 */
	if (!priv->ucode_loaded) {
		IWL_ERR(priv, "Fw not loaded - dropping CMD: %x\n", cmd->id);
		return -EIO;
	}

	/*
	 * Synchronous commands from this op-mode must hold
	 * the mutex, this ensures we don't try to send two
	 * (or more) synchronous commands at a time.
	 */
	if (!(cmd->flags & CMD_ASYNC))
		lockdep_assert_held(&priv->mutex);

	return iwl_trans_send_cmd(priv->trans, cmd);
}

int iwl_dvm_send_cmd_pdu(struct iwl_priv *priv, u8 id,
			 u32 flags, u16 len, const void *data)
{
	struct iwl_host_cmd cmd = {
		.id = id,
		.len = { len, },
		.data = { data, },
		.flags = flags,
	};

	return iwl_dvm_send_cmd(priv, &cmd);
}
