/*
 * Marvell Wireless LAN device driver: station command response handling
 *
 * Copyright (C) 2011-2014, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

#include "decl.h"
#include "ioctl.h"
#include "util.h"
#include "fw.h"
#include "main.h"
#include "wmm.h"
#include "11n.h"
#include "11ac.h"


/*
 * This function handles the command response error case.
 *
 * For scan response error, the function cancels all the pending
 * scan commands and generates an event to inform the applications
 * of the scan completion.
 *
 * For Power Save command failure, we do not retry enter PS
 * command in case of Ad-hoc mode.
 *
 * For all other response errors, the current command buffer is freed
 * and returned to the free command queue.
 */
static void
mwifiex_process_cmdresp_error(struct mwifiex_private *priv,
			      struct host_cmd_ds_command *resp)
{
	struct mwifiex_adapter *adapter = priv->adapter;
	struct host_cmd_ds_802_11_ps_mode_enh *pm;
	unsigned long flags;

	mwifiex_dbg(adapter, ERROR,
		    "CMD_RESP: cmd %#x error, result=%#x\n",
		    resp->command, resp->result);

	if (adapter->curr_cmd->wait_q_enabled)
		adapter->cmd_wait_q.status = -1;

	switch (le16_to_cpu(resp->command)) {
	case HostCmd_CMD_802_11_PS_MODE_ENH:
		pm = &resp->params.psmode_enh;
		mwifiex_dbg(adapter, ERROR,
			    "PS_MODE_ENH cmd failed: result=0x%x action=0x%X\n",
			    resp->result, le16_to_cpu(pm->action));
		/* We do not re-try enter-ps command in ad-hoc mode. */
		if (le16_to_cpu(pm->action) == EN_AUTO_PS &&
		    (le16_to_cpu(pm->params.ps_bitmap) & BITMAP_STA_PS) &&
		    priv->bss_mode == NL80211_IFTYPE_ADHOC)
			adapter->ps_mode = MWIFIEX_802_11_POWER_MODE_CAM;

		break;
	case HostCmd_CMD_802_11_SCAN:
	case HostCmd_CMD_802_11_SCAN_EXT:
		mwifiex_cancel_pending_scan_cmd(adapter);

		spin_lock_irqsave(&adapter->mwifiex_cmd_lock, flags);
		adapter->scan_processing = false;
		spin_unlock_irqrestore(&adapter->mwifiex_cmd_lock, flags);
		break;

	case HostCmd_CMD_MAC_CONTROL:
		break;

	case HostCmd_CMD_SDIO_SP_RX_AGGR_CFG:
		mwifiex_dbg(adapter, MSG,
			    "SDIO RX single-port aggregation Not support\n");
		break;

	default:
		break;
	}
	/* Handling errors here */
	mwifiex_recycle_cmd_node(adapter, adapter->curr_cmd);

	spin_lock_irqsave(&adapter->mwifiex_cmd_lock, flags);
	adapter->curr_cmd = NULL;
	spin_unlock_irqrestore(&adapter->mwifiex_cmd_lock, flags);
}

/*
 * This function handles the command response of get RSSI info.
 *
 * Handling includes changing the header fields into CPU format
 * and saving the following parameters in driver -
 *      - Last data and beacon RSSI value
 *      - Average data and beacon RSSI value
 *      - Last data and beacon NF value
 *      - Average data and beacon NF value
 *
 * The parameters are send to the application as well, along with
 * calculated SNR values.
 */
static int mwifiex_ret_802_11_rssi_info(struct mwifiex_private *priv,
					struct host_cmd_ds_command *resp)
{
	struct host_cmd_ds_802_11_rssi_info_rsp *rssi_info_rsp =
						&resp->params.rssi_info_rsp;
	struct mwifiex_ds_misc_subsc_evt *subsc_evt =
						&priv->async_subsc_evt_storage;

	priv->data_rssi_last = le16_to_cpu(rssi_info_rsp->data_rssi_last);
	priv->data_nf_last = le16_to_cpu(rssi_info_rsp->data_nf_last);

	priv->data_rssi_avg = le16_to_cpu(rssi_info_rsp->data_rssi_avg);
	priv->data_nf_avg = le16_to_cpu(rssi_info_rsp->data_nf_avg);

	priv->bcn_rssi_last = le16_to_cpu(rssi_info_rsp->bcn_rssi_last);
	priv->bcn_nf_last = le16_to_cpu(rssi_info_rsp->bcn_nf_last);

	priv->bcn_rssi_avg = le16_to_cpu(rssi_info_rsp->bcn_rssi_avg);
	priv->bcn_nf_avg = le16_to_cpu(rssi_info_rsp->bcn_nf_avg);

	if (priv->subsc_evt_rssi_state == EVENT_HANDLED)
		return 0;

	memset(subsc_evt, 0x00, sizeof(struct mwifiex_ds_misc_subsc_evt));

	/* Resubscribe low and high rssi events with new thresholds */
	subsc_evt->events = BITMASK_BCN_RSSI_LOW | BITMASK_BCN_RSSI_HIGH;
	subsc_evt->action = HostCmd_ACT_BITWISE_SET;
	if (priv->subsc_evt_rssi_state == RSSI_LOW_RECVD) {
		subsc_evt->bcn_l_rssi_cfg.abs_value = abs(priv->bcn_rssi_avg -
				priv->cqm_rssi_hyst);
		subsc_evt->bcn_h_rssi_cfg.abs_value = abs(priv->cqm_rssi_thold);
	} else if (priv->subsc_evt_rssi_state == RSSI_HIGH_RECVD) {
		subsc_evt->bcn_l_rssi_cfg.abs_value = abs(priv->cqm_rssi_thold);
		subsc_evt->bcn_h_rssi_cfg.abs_value = abs(priv->bcn_rssi_avg +
				priv->cqm_rssi_hyst);
	}
	subsc_evt->bcn_l_rssi_cfg.evt_freq = 1;
	subsc_evt->bcn_h_rssi_cfg.evt_freq = 1;

	priv->subsc_evt_rssi_state = EVENT_HANDLED;

	mwifiex_send_cmd(priv, HostCmd_CMD_802_11_SUBSCRIBE_EVENT,
			 0, 0, subsc_evt, false);

	return 0;
}

/*
 * This function handles the command response of set/get SNMP
 * MIB parameters.
 *
 * Handling includes changing the header fields into CPU format
 * and saving the parameter in driver.
 *
 * The following parameters are supported -
 *      - Fragmentation threshold
 *      - RTS threshold
 *      - Short retry limit
 */
static int mwifiex_ret_802_11_snmp_mib(struct mwifiex_private *priv,
				       struct host_cmd_ds_command *resp,
				       u32 *data_buf)
{
	struct host_cmd_ds_802_11_snmp_mib *smib = &resp->params.smib;
	u16 oid = le16_to_cpu(smib->oid);
	u16 query_type = le16_to_cpu(smib->query_type);
	u32 ul_temp;

	mwifiex_dbg(priv->adapter, INFO,
		    "info: SNMP_RESP: oid value = %#x,\t"
		    "query_type = %#x, buf size = %#x\n",
		    oid, query_type, le16_to_cpu(smib->buf_size));
	if (query_type == HostCmd_ACT_GEN_GET) {
		ul_temp = get_unaligned_le16(smib->value);
		if (data_buf)
			*data_buf = ul_temp;
		switch (oid) {
		case FRAG_THRESH_I:
			mwifiex_dbg(priv->adapter, INFO,
				    "info: SNMP_RESP: FragThsd =%u\n",
				    ul_temp);
			break;
		case RTS_THRESH_I:
			mwifiex_dbg(priv->adapter, INFO,
				    "info: SNMP_RESP: RTSThsd =%u\n",
				    ul_temp);
			break;
		case SHORT_RETRY_LIM_I:
			mwifiex_dbg(priv->adapter, INFO,
				    "info: SNMP_RESP: TxRetryCount=%u\n",
				    ul_temp);
			break;
		case DTIM_PERIOD_I:
			mwifiex_dbg(priv->adapter, INFO,
				    "info: SNMP_RESP: DTIM period=%u\n",
				    ul_temp);
		default:
			break;
		}
	}

	return 0;
}

/*
 * This function handles the command response of get log request
 *
 * Handling includes changing the header fields into CPU format
 * and sending the received parameters to application.
 */
static int mwifiex_ret_get_log(struct mwifiex_private *priv,
			       struct host_cmd_ds_command *resp,
			       struct mwifiex_ds_get_stats *stats)
{
	struct host_cmd_ds_802_11_get_log *get_log =
		&resp->params.get_log;

	if (stats) {
		stats->mcast_tx_frame = le32_to_cpu(get_log->mcast_tx_frame);
		stats->failed = le32_to_cpu(get_log->failed);
		stats->retry = le32_to_cpu(get_log->retry);
		stats->multi_retry = le32_to_cpu(get_log->multi_retry);
		stats->frame_dup = le32_to_cpu(get_log->frame_dup);
		stats->rts_success = le32_to_cpu(get_log->rts_success);
		stats->rts_failure = le32_to_cpu(get_log->rts_failure);
		stats->ack_failure = le32_to_cpu(get_log->ack_failure);
		stats->rx_frag = le32_to_cpu(get_log->rx_frag);
		stats->mcast_rx_frame = le32_to_cpu(get_log->mcast_rx_frame);
		stats->fcs_error = le32_to_cpu(get_log->fcs_error);
		stats->tx_frame = le32_to_cpu(get_log->tx_frame);
		stats->wep_icv_error[0] =
			le32_to_cpu(get_log->wep_icv_err_cnt[0]);
		stats->wep_icv_error[1] =
			le32_to_cpu(get_log->wep_icv_err_cnt[1]);
		stats->wep_icv_error[2] =
			le32_to_cpu(get_log->wep_icv_err_cnt[2]);
		stats->wep_icv_error[3] =
			le32_to_cpu(get_log->wep_icv_err_cnt[3]);
		stats->bcn_rcv_cnt = le32_to_cpu(get_log->bcn_rcv_cnt);
		stats->bcn_miss_cnt = le32_to_cpu(get_log->bcn_miss_cnt);
	}

	return 0;
}

/*
 * This function handles the command response of set/get Tx rate
 * configurations.
 *
 * Handling includes changing the header fields into CPU format
 * and saving the following parameters in driver -
 *      - DSSS rate bitmap
 *      - OFDM rate bitmap
 *      - HT MCS rate bitmaps
 *
 * Based on the new rate bitmaps, the function re-evaluates if
 * auto data rate has been activated. If not, it sends another
 * query to the firmware to get the current Tx data rate.
 */
static int mwifiex_ret_tx_rate_cfg(struct mwifiex_private *priv,
				   struct host_cmd_ds_command *resp)
{
	struct host_cmd_ds_tx_rate_cfg *rate_cfg = &resp->params.tx_rate_cfg;
	struct mwifiex_rate_scope *rate_scope;
	struct mwifiex_ie_types_header *head;
	u16 tlv, tlv_buf_len, tlv_buf_left;
	u8 *tlv_buf;
	u32 i;

	tlv_buf = ((u8 *)rate_cfg) + sizeof(struct host_cmd_ds_tx_rate_cfg);
	tlv_buf_left = le16_to_cpu(resp->size) - S_DS_GEN - sizeof(*rate_cfg);

	while (tlv_buf_left >= sizeof(*head)) {
		head = (struct mwifiex_ie_types_header *)tlv_buf;
		tlv = le16_to_cpu(head->type);
		tlv_buf_len = le16_to_cpu(head->len);

		if (tlv_buf_left < (sizeof(*head) + tlv_buf_len))
			break;

		switch (tlv) {
		case TLV_TYPE_RATE_SCOPE:
			rate_scope = (struct mwifiex_rate_scope *) tlv_buf;
			priv->bitmap_rates[0] =
				le16_to_cpu(rate_scope->hr_dsss_rate_bitmap);
			priv->bitmap_rates[1] =
				le16_to_cpu(rate_scope->ofdm_rate_bitmap);
			for (i = 0;
			     i <
			     sizeof(rate_scope->ht_mcs_rate_bitmap) /
			     sizeof(u16); i++)
				priv->bitmap_rates[2 + i] =
					le16_to_cpu(rate_scope->
						    ht_mcs_rate_bitmap[i]);

			if (priv->adapter->fw_api_ver == MWIFIEX_FW_V15) {
				for (i = 0; i < ARRAY_SIZE(rate_scope->
							   vht_mcs_rate_bitmap);
				     i++)
					priv->bitmap_rates[10 + i] =
					    le16_to_cpu(rate_scope->
							vht_mcs_rate_bitmap[i]);
			}
			break;
			/* Add RATE_DROP tlv here */
		}

		tlv_buf += (sizeof(*head) + tlv_buf_len);
		tlv_buf_left -= (sizeof(*head) + tlv_buf_len);
	}

	priv->is_data_rate_auto = mwifiex_is_rate_auto(priv);

	if (priv->is_data_rate_auto)
		priv->data_rate = 0;
	else
		return mwifiex_send_cmd(priv, HostCmd_CMD_802_11_TX_RATE_QUERY,
					HostCmd_ACT_GEN_GET, 0, NULL, false);

	return 0;
}

/*
 * This function handles the command response of get Tx power level.
 *
 * Handling includes saving the maximum and minimum Tx power levels
 * in driver, as well as sending the values to user.
 */
static int mwifiex_get_power_level(struct mwifiex_private *priv, void *data_buf)
{
	int length, max_power = -1, min_power = -1;
	struct mwifiex_types_power_group *pg_tlv_hdr;
	struct mwifiex_power_group *pg;

	if (!data_buf)
		return -1;

	pg_tlv_hdr = (struct mwifiex_types_power_group *)((u8 *)data_buf);
	pg = (struct mwifiex_power_group *)
		((u8 *) pg_tlv_hdr + sizeof(struct mwifiex_types_power_group));
	length = le16_to_cpu(pg_tlv_hdr->length);

	/* At least one structure required to update power */
	if (length < sizeof(struct mwifiex_power_group))
		return 0;

	max_power = pg->power_max;
	min_power = pg->power_min;
	length -= sizeof(struct mwifiex_power_group);

	while (length >= sizeof(struct mwifiex_power_group)) {
		pg++;
		if (max_power < pg->power_max)
			max_power = pg->power_max;

		if (min_power > pg->power_min)
			min_power = pg->power_min;

		length -= sizeof(struct mwifiex_power_group);
	}
	priv->min_tx_power_level = (u8) min_power;
	priv->max_tx_power_level = (u8) max_power;

	return 0;
}

/*
 * This function handles the command response of set/get Tx power
 * configurations.
 *
 * Handling includes changing the header fields into CPU format
 * and saving the current Tx power level in driver.
 */
static int mwifiex_ret_tx_power_cfg(struct mwifiex_private *priv,
				    struct host_cmd_ds_command *resp)
{
	struct mwifiex_adapter *adapter = priv->adapter;
	struct host_cmd_ds_txpwr_cfg *txp_cfg = &resp->params.txp_cfg;
	struct mwifiex_types_power_group *pg_tlv_hdr;
	struct mwifiex_power_group *pg;
	u16 action = le16_to_cpu(txp_cfg->action);
	u16 tlv_buf_left;

	pg_tlv_hdr = (struct mwifiex_types_power_group *)
		((u8 *)txp_cfg +
		 sizeof(struct host_cmd_ds_txpwr_cfg));

	pg = (struct mwifiex_power_group *)
		((u8 *)pg_tlv_hdr +
		 sizeof(struct mwifiex_types_power_group));

	tlv_buf_left = le16_to_cpu(resp->size) - S_DS_GEN - sizeof(*txp_cfg);
	if (tlv_buf_left <
			le16_to_cpu(pg_tlv_hdr->length) + sizeof(*pg_tlv_hdr))
		return 0;

	switch (action) {
	case HostCmd_ACT_GEN_GET:
		if (adapter->hw_status == MWIFIEX_HW_STATUS_INITIALIZING)
			mwifiex_get_power_level(priv, pg_tlv_hdr);

		priv->tx_power_level = (u16) pg->power_min;
		break;

	case HostCmd_ACT_GEN_SET:
		if (!le32_to_cpu(txp_cfg->mode))
			break;

		if (pg->power_max == pg->power_min)
			priv->tx_power_level = (u16) pg->power_min;
		break;
	default:
		mwifiex_dbg(adapter, ERROR,
			    "CMD_RESP: unknown cmd action %d\n",
			    action);
		return 0;
	}
	mwifiex_dbg(adapter, INFO,
		    "info: Current TxPower Level = %d, Max Power=%d, Min Power=%d\n",
		    priv->tx_power_level, priv->max_tx_power_level,
		    priv->min_tx_power_level);

	return 0;
}

/*
 * This function handles the command response of get RF Tx power.
 */
static int mwifiex_ret_rf_tx_power(struct mwifiex_private *priv,
				   struct host_cmd_ds_command *resp)
{
	struct host_cmd_ds_rf_tx_pwr *txp = &resp->params.txp;
	u16 action = le16_to_cpu(txp->action);

	priv->tx_power_level = le16_to_cpu(txp->cur_level);

	if (action == HostCmd_ACT_GEN_GET) {
		priv->max_tx_power_level = txp->max_power;
		priv->min_tx_power_level = txp->min_power;
	}

	mwifiex_dbg(priv->adapter, INFO,
		    "Current TxPower Level=%d, Max Power=%d, Min Power=%d\n",
		    priv->tx_power_level, priv->max_tx_power_level,
		    priv->min_tx_power_level);

	return 0;
}

/*
 * This function handles the command response of set rf antenna
 */
static int mwifiex_ret_rf_antenna(struct mwifiex_private *priv,
				  struct host_cmd_ds_command *resp)
{
	struct host_cmd_ds_rf_ant_mimo *ant_mimo = &resp->params.ant_mimo;
	struct host_cmd_ds_rf_ant_siso *ant_siso = &resp->params.ant_siso;
	struct mwifiex_adapter *adapter = priv->adapter;

	if (adapter->hw_dev_mcs_support == HT_STREAM_2X2) {
		priv->tx_ant = le16_to_cpu(ant_mimo->tx_ant_mode);
		priv->rx_ant = le16_to_cpu(ant_mimo->rx_ant_mode);
		mwifiex_dbg(adapter, INFO,
			    "RF_ANT_RESP: Tx action = 0x%x, Tx Mode = 0x%04x\t"
			    "Rx action = 0x%x, Rx Mode = 0x%04x\n",
			    le16_to_cpu(ant_mimo->action_tx),
			    le16_to_cpu(ant_mimo->tx_ant_mode),
			    le16_to_cpu(ant_mimo->action_rx),
			    le16_to_cpu(ant_mimo->rx_ant_mode));
	} else {
		priv->tx_ant = le16_to_cpu(ant_siso->ant_mode);
		priv->rx_ant = le16_to_cpu(ant_siso->ant_mode);
		mwifiex_dbg(adapter, INFO,
			    "RF_ANT_RESP: action = 0x%x, Mode = 0x%04x\n",
			    le16_to_cpu(ant_siso->action),
			    le16_to_cpu(ant_siso->ant_mode));
	}
	return 0;
}

/*
 * This function handles the command response of set/get MAC address.
 *
 * Handling includes saving the MAC address in driver.
 */
static int mwifiex_ret_802_11_mac_address(struct mwifiex_private *priv,
					  struct host_cmd_ds_command *resp)
{
	struct host_cmd_ds_802_11_mac_address *cmd_mac_addr =
							&resp->params.mac_addr;

	memcpy(priv->curr_addr, cmd_mac_addr->mac_addr, ETH_ALEN);

	mwifiex_dbg(priv->adapter, INFO,
		    "info: set mac address: %pM\n", priv->curr_addr);

	return 0;
}

/*
 * This function handles the command response of set/get MAC multicast
 * address.
 */
static int mwifiex_ret_mac_multicast_adr(struct mwifiex_private *priv,
					 struct host_cmd_ds_command *resp)
{
	return 0;
}

/*
 * This function handles the command response of get Tx rate query.
 *
 * Handling includes changing the header fields into CPU format
 * and saving the Tx rate and HT information parameters in driver.
 *
 * Both rate configuration and current data rate can be retrieved
 * with this request.
 */
static int mwifiex_ret_802_11_tx_rate_query(struct mwifiex_private *priv,
					    struct host_cmd_ds_command *resp)
{
	priv->tx_rate = resp->params.tx_rate.tx_rate;
	priv->tx_htinfo = resp->params.tx_rate.ht_info;
	if (!priv->is_data_rate_auto)
		priv->data_rate =
			mwifiex_index_to_data_rate(priv, priv->tx_rate,
						   priv->tx_htinfo);

	return 0;
}

/*
 * This function handles the command response of a deauthenticate
 * command.
 *
 * If the deauthenticated MAC matches the current BSS MAC, the connection
 * state is reset.
 */
static int mwifiex_ret_802_11_deauthenticate(struct mwifiex_private *priv,
					     struct host_cmd_ds_command *resp)
{
	struct mwifiex_adapter *adapter = priv->adapter;

	adapter->dbg.num_cmd_deauth++;
	if (!memcmp(resp->params.deauth.mac_addr,
		    &priv->curr_bss_params.bss_descriptor.mac_address,
		    sizeof(resp->params.deauth.mac_addr)))
		mwifiex_reset_connect_state(priv, WLAN_REASON_DEAUTH_LEAVING,
					    false);

	return 0;
}

/*
 * This function handles the command response of ad-hoc stop.
 *
 * The function resets the connection state in driver.
 */
static int mwifiex_ret_802_11_ad_hoc_stop(struct mwifiex_private *priv,
					  struct host_cmd_ds_command *resp)
{
	mwifiex_reset_connect_state(priv, WLAN_REASON_DEAUTH_LEAVING, false);
	return 0;
}

/*
 * This function handles the command response of set/get v1 key material.
 *
 * Handling includes updating the driver parameters to reflect the
 * changes.
 */
static int mwifiex_ret_802_11_key_material_v1(struct mwifiex_private *priv,
					      struct host_cmd_ds_command *resp)
{
	struct host_cmd_ds_802_11_key_material *key =
						&resp->params.key_material;

	if (le16_to_cpu(key->action) == HostCmd_ACT_GEN_SET) {
		if ((le16_to_cpu(key->key_param_set.key_info) & KEY_MCAST)) {
			mwifiex_dbg(priv->adapter, INFO,
				    "info: key: GTK is set\n");
			priv->wpa_is_gtk_set = true;
			priv->scan_block = false;
			priv->port_open = true;
		}
	}

	memset(priv->aes_key.key_param_set.key, 0,
	       sizeof(key->key_param_set.key));
	priv->aes_key.key_param_set.key_len = key->key_param_set.key_len;
	memcpy(priv->aes_key.key_param_set.key, key->key_param_set.key,
	       le16_to_cpu(priv->aes_key.key_param_set.key_len));

	return 0;
}

/*
 * This function handles the command response of set/get v2 key material.
 *
 * Handling includes updating the driver parameters to reflect the
 * changes.
 */
static int mwifiex_ret_802_11_key_material_v2(struct mwifiex_private *priv,
					      struct host_cmd_ds_command *resp)
{
	struct host_cmd_ds_802_11_key_material_v2 *key_v2;
	__le16 len;

	key_v2 = &resp->params.key_material_v2;
	if (le16_to_cpu(key_v2->action) == HostCmd_ACT_GEN_SET) {
		if ((le16_to_cpu(key_v2->key_param_set.key_info) & KEY_MCAST)) {
			mwifiex_dbg(priv->adapter, INFO, "info: key: GTK is set\n");
			priv->wpa_is_gtk_set = true;
			priv->scan_block = false;
			priv->port_open = true;
		}
	}

	if (key_v2->key_param_set.key_type != KEY_TYPE_ID_AES)
		return 0;

	memset(priv->aes_key_v2.key_param_set.key_params.aes.key, 0,
	       WLAN_KEY_LEN_CCMP);
	priv->aes_key_v2.key_param_set.key_params.aes.key_len =
				key_v2->key_param_set.key_params.aes.key_len;
	len = priv->aes_key_v2.key_param_set.key_params.aes.key_len;
	memcpy(priv->aes_key_v2.key_param_set.key_params.aes.key,
	       key_v2->key_param_set.key_params.aes.key, le16_to_cpu(len));

	return 0;
}

/* Wrapper function for processing response of key material command */
static int mwifiex_ret_802_11_key_material(struct mwifiex_private *priv,
					   struct host_cmd_ds_command *resp)
{
	if (priv->adapter->key_api_major_ver == KEY_API_VER_MAJOR_V2)
		return mwifiex_ret_802_11_key_material_v2(priv, resp);
	else
		return mwifiex_ret_802_11_key_material_v1(priv, resp);
}

/*
 * This function handles the command response of get 11d domain information.
 */
static int mwifiex_ret_802_11d_domain_info(struct mwifiex_private *priv,
					   struct host_cmd_ds_command *resp)
{
	struct host_cmd_ds_802_11d_domain_info_rsp *domain_info =
		&resp->params.domain_info_resp;
	struct mwifiex_ietypes_domain_param_set *domain = &domain_info->domain;
	u16 action = le16_to_cpu(domain_info->action);
	u8 no_of_triplet;

	no_of_triplet = (u8) ((le16_to_cpu(domain->header.len)
				- IEEE80211_COUNTRY_STRING_LEN)
			      / sizeof(struct ieee80211_country_ie_triplet));

	mwifiex_dbg(priv->adapter, INFO,
		    "info: 11D Domain Info Resp: no_of_triplet=%d\n",
		    no_of_triplet);

	if (no_of_triplet > MWIFIEX_MAX_TRIPLET_802_11D) {
		mwifiex_dbg(priv->adapter, FATAL,
			    "11D: invalid number of triplets %d returned\n",
			    no_of_triplet);
		return -1;
	}

	switch (action) {
	case HostCmd_ACT_GEN_SET:  /* Proc Set Action */
		break;
	case HostCmd_ACT_GEN_GET:
		break;
	default:
		mwifiex_dbg(priv->adapter, ERROR,
			    "11D: invalid action:%d\n", domain_info->action);
		return -1;
	}

	return 0;
}

/*
 * This function handles the command response of get extended version.
 *
 * Handling includes forming the extended version string and sending it
 * to application.
 */
static int mwifiex_ret_ver_ext(struct mwifiex_private *priv,
			       struct host_cmd_ds_command *resp,
			       struct host_cmd_ds_version_ext *version_ext)
{
	struct host_cmd_ds_version_ext *ver_ext = &resp->params.verext;

	if (version_ext) {
		version_ext->version_str_sel = ver_ext->version_str_sel;
		memcpy(version_ext->version_str, ver_ext->version_str,
		       sizeof(char) * 128);
		memcpy(priv->version_str, ver_ext->version_str, 128);
	}
	return 0;
}

/*
 * This function handles the command response of remain on channel.
 */
static int
mwifiex_ret_remain_on_chan(struct mwifiex_private *priv,
			   struct host_cmd_ds_command *resp,
			   struct host_cmd_ds_remain_on_chan *roc_cfg)
{
	struct host_cmd_ds_remain_on_chan *resp_cfg = &resp->params.roc_cfg;

	if (roc_cfg)
		memcpy(roc_cfg, resp_cfg, sizeof(*roc_cfg));

	return 0;
}

/*
 * This function handles the command response of P2P mode cfg.
 */
static int
mwifiex_ret_p2p_mode_cfg(struct mwifiex_private *priv,
			 struct host_cmd_ds_command *resp,
			 void *data_buf)
{
	struct host_cmd_ds_p2p_mode_cfg *mode_cfg = &resp->params.mode_cfg;

	if (data_buf)
		put_unaligned_le16(le16_to_cpu(mode_cfg->mode), data_buf);

	return 0;
}

/* This function handles the command response of mem_access command
 */
static int
mwifiex_ret_mem_access(struct mwifiex_private *priv,
		       struct host_cmd_ds_command *resp, void *pioctl_buf)
{
	struct host_cmd_ds_mem_access *mem = (void *)&resp->params.mem;

	priv->mem_rw.addr = le32_to_cpu(mem->addr);
	priv->mem_rw.value = le32_to_cpu(mem->value);

	return 0;
}
/*
 * This function handles the command response of register access.
 *
 * The register value and offset are returned to the user. For EEPROM
 * access, the byte count is also returned.
 */
static int mwifiex_ret_reg_access(u16 type, struct host_cmd_ds_command *resp,
				  void *data_buf)
{
	struct mwifiex_ds_reg_rw *reg_rw;
	struct mwifiex_ds_read_eeprom *eeprom;
	union reg {
		struct host_cmd_ds_mac_reg_access *mac;
		struct host_cmd_ds_bbp_reg_access *bbp;
		struct host_cmd_ds_rf_reg_access *rf;
		struct host_cmd_ds_pmic_reg_access *pmic;
		struct host_cmd_ds_802_11_eeprom_access *eeprom;
	} r;

	if (!data_buf)
		return 0;

	reg_rw = data_buf;
	eeprom = data_buf;
	switch (type) {
	case HostCmd_CMD_MAC_REG_ACCESS:
		r.mac = &resp->params.mac_reg;
		reg_rw->offset = (u32) le16_to_cpu(r.mac->offset);
		reg_rw->value = le32_to_cpu(r.mac->value);
		break;
	case HostCmd_CMD_BBP_REG_ACCESS:
		r.bbp = &resp->params.bbp_reg;
		reg_rw->offset = (u32) le16_to_cpu(r.bbp->offset);
		reg_rw->value = (u32) r.bbp->value;
		break;

	case HostCmd_CMD_RF_REG_ACCESS:
		r.rf = &resp->params.rf_reg;
		reg_rw->offset = (u32) le16_to_cpu(r.rf->offset);
		reg_rw->value = (u32) r.bbp->value;
		break;
	case HostCmd_CMD_PMIC_REG_ACCESS:
		r.pmic = &resp->params.pmic_reg;
		reg_rw->offset = (u32) le16_to_cpu(r.pmic->offset);
		reg_rw->value = (u32) r.pmic->value;
		break;
	case HostCmd_CMD_CAU_REG_ACCESS:
		r.rf = &resp->params.rf_reg;
		reg_rw->offset = (u32) le16_to_cpu(r.rf->offset);
		reg_rw->value = (u32) r.rf->value;
		break;
	case HostCmd_CMD_802_11_EEPROM_ACCESS:
		r.eeprom = &resp->params.eeprom;
		pr_debug("info: EEPROM read len=%x\n",
				le16_to_cpu(r.eeprom->byte_count));
		if (eeprom->byte_count < le16_to_cpu(r.eeprom->byte_count)) {
			eeprom->byte_count = 0;
			pr_debug("info: EEPROM read length is too big\n");
			return -1;
		}
		eeprom->offset = le16_to_cpu(r.eeprom->offset);
		eeprom->byte_count = le16_to_cpu(r.eeprom->byte_count);
		if (eeprom->byte_count > 0)
			memcpy(&eeprom->value, &r.eeprom->value,
			       min((u16)MAX_EEPROM_DATA, eeprom->byte_count));
		break;
	default:
		return -1;
	}
	return 0;
}

/*
 * This function handles the command response of get IBSS coalescing status.
 *
 * If the received BSSID is different than the current one, the current BSSID,
 * beacon interval, ATIM window and ERP information are updated, along with
 * changing the ad-hoc state accordingly.
 */
static int mwifiex_ret_ibss_coalescing_status(struct mwifiex_private *priv,
					      struct host_cmd_ds_command *resp)
{
	struct host_cmd_ds_802_11_ibss_status *ibss_coal_resp =
					&(resp->params.ibss_coalescing);

	if (le16_to_cpu(ibss_coal_resp->action) == HostCmd_ACT_GEN_SET)
		return 0;

	mwifiex_dbg(priv->adapter, INFO,
		    "info: new BSSID %pM\n", ibss_coal_resp->bssid);

	/* If rsp has NULL BSSID, Just return..... No Action */
	if (is_zero_ether_addr(ibss_coal_resp->bssid)) {
		mwifiex_dbg(priv->adapter, FATAL, "new BSSID is NULL\n");
		return 0;
	}

	/* If BSSID is diff, modify current BSS parameters */
	if (!ether_addr_equal(priv->curr_bss_params.bss_descriptor.mac_address, ibss_coal_resp->bssid)) {
		/* BSSID */
		memcpy(priv->curr_bss_params.bss_descriptor.mac_address,
		       ibss_coal_resp->bssid, ETH_ALEN);

		/* Beacon Interval */
		priv->curr_bss_params.bss_descriptor.beacon_period
			= le16_to_cpu(ibss_coal_resp->beacon_interval);

		/* ERP Information */
		priv->curr_bss_params.bss_descriptor.erp_flags =
			(u8) le16_to_cpu(ibss_coal_resp->use_g_rate_protect);

		priv->adhoc_state = ADHOC_COALESCED;
	}

	return 0;
}
static int mwifiex_ret_tdls_oper(struct mwifiex_private *priv,
				 struct host_cmd_ds_command *resp)
{
	struct host_cmd_ds_tdls_oper *cmd_tdls_oper = &resp->params.tdls_oper;
	u16 reason = le16_to_cpu(cmd_tdls_oper->reason);
	u16 action = le16_to_cpu(cmd_tdls_oper->tdls_action);
	struct mwifiex_sta_node *node =
			   mwifiex_get_sta_entry(priv, cmd_tdls_oper->peer_mac);

	switch (action) {
	case ACT_TDLS_DELETE:
		if (reason) {
			if (!node || reason == TDLS_ERR_LINK_NONEXISTENT)
				mwifiex_dbg(priv->adapter, MSG,
					    "TDLS link delete for %pM failed: reason %d\n",
					    cmd_tdls_oper->peer_mac, reason);
			else
				mwifiex_dbg(priv->adapter, ERROR,
					    "TDLS link delete for %pM failed: reason %d\n",
					    cmd_tdls_oper->peer_mac, reason);
		} else {
			mwifiex_dbg(priv->adapter, MSG,
				    "TDLS link delete for %pM successful\n",
				    cmd_tdls_oper->peer_mac);
		}
		break;
	case ACT_TDLS_CREATE:
		if (reason) {
			mwifiex_dbg(priv->adapter, ERROR,
				    "TDLS link creation for %pM failed: reason %d",
				    cmd_tdls_oper->peer_mac, reason);
			if (node && reason != TDLS_ERR_LINK_EXISTS)
				node->tdls_status = TDLS_SETUP_FAILURE;
		} else {
			mwifiex_dbg(priv->adapter, MSG,
				    "TDLS link creation for %pM successful",
				    cmd_tdls_oper->peer_mac);
		}
		break;
	case ACT_TDLS_CONFIG:
		if (reason) {
			mwifiex_dbg(priv->adapter, ERROR,
				    "TDLS link config for %pM failed, reason %d\n",
				    cmd_tdls_oper->peer_mac, reason);
			if (node)
				node->tdls_status = TDLS_SETUP_FAILURE;
		} else {
			mwifiex_dbg(priv->adapter, MSG,
				    "TDLS link config for %pM successful\n",
				    cmd_tdls_oper->peer_mac);
		}
		break;
	default:
		mwifiex_dbg(priv->adapter, ERROR,
			    "Unknown TDLS command action response %d", action);
		return -1;
	}

	return 0;
}
/*
 * This function handles the command response for subscribe event command.
 */
static int mwifiex_ret_subsc_evt(struct mwifiex_private *priv,
				 struct host_cmd_ds_command *resp)
{
	struct host_cmd_ds_802_11_subsc_evt *cmd_sub_event =
		&resp->params.subsc_evt;

	/* For every subscribe event command (Get/Set/Clear), FW reports the
	 * current set of subscribed events*/
	mwifiex_dbg(priv->adapter, EVENT,
		    "Bitmap of currently subscribed events: %16x\n",
		    le16_to_cpu(cmd_sub_event->events));

	return 0;
}

static int mwifiex_ret_uap_sta_list(struct mwifiex_private *priv,
				    struct host_cmd_ds_command *resp)
{
	struct host_cmd_ds_sta_list *sta_list =
		&resp->params.sta_list;
	struct mwifiex_ie_types_sta_info *sta_info = (void *)&sta_list->tlv;
	int i;
	struct mwifiex_sta_node *sta_node;

	for (i = 0; i < (le16_to_cpu(sta_list->sta_count)); i++) {
		sta_node = mwifiex_get_sta_entry(priv, sta_info->mac);
		if (unlikely(!sta_node))
			continue;

		sta_node->stats.rssi = sta_info->rssi;
		sta_info++;
	}

	return 0;
}

/* This function handles the command response of set_cfg_data */
static int mwifiex_ret_cfg_data(struct mwifiex_private *priv,
				struct host_cmd_ds_command *resp)
{
	if (resp->result != HostCmd_RESULT_OK) {
		mwifiex_dbg(priv->adapter, ERROR, "Cal data cmd resp failed\n");
		return -1;
	}

	return 0;
}

/** This Function handles the command response of sdio rx aggr */
static int mwifiex_ret_sdio_rx_aggr_cfg(struct mwifiex_private *priv,
					struct host_cmd_ds_command *resp)
{
	struct mwifiex_adapter *adapter = priv->adapter;
	struct host_cmd_sdio_sp_rx_aggr_cfg *cfg =
				&resp->params.sdio_rx_aggr_cfg;

	adapter->sdio_rx_aggr_enable = cfg->enable;
	adapter->sdio_rx_block_size = le16_to_cpu(cfg->block_size);

	return 0;
}

static int mwifiex_ret_robust_coex(struct mwifiex_private *priv,
				   struct host_cmd_ds_command *resp,
				   bool *is_timeshare)
{
	struct host_cmd_ds_robust_coex *coex = &resp->params.coex;
	struct mwifiex_ie_types_robust_coex *coex_tlv;
	u16 action = le16_to_cpu(coex->action);
	u32 mode;

	coex_tlv = (struct mwifiex_ie_types_robust_coex
		    *)((u8 *)coex + sizeof(struct host_cmd_ds_robust_coex));
	if (action == HostCmd_ACT_GEN_GET) {
		mode = le32_to_cpu(coex_tlv->mode);
		if (mode == MWIFIEX_COEX_MODE_TIMESHARE)
			*is_timeshare = true;
		else
			*is_timeshare = false;
	}

	return 0;
}

static struct ieee80211_regdomain *
mwifiex_create_custom_regdomain(struct mwifiex_private *priv,
				u8 *buf, u16 buf_len)
{
	u16 num_chan = buf_len / 2;
	struct ieee80211_regdomain *regd;
	struct ieee80211_reg_rule *rule;
	bool new_rule;
	int regd_size, idx, freq, prev_freq = 0;
	u32 bw, prev_bw = 0;
	u8 chflags, prev_chflags = 0, valid_rules = 0;

	if (WARN_ON_ONCE(num_chan > NL80211_MAX_SUPP_REG_RULES))
		return ERR_PTR(-EINVAL);

	regd_size = sizeof(struct ieee80211_regdomain) +
		    num_chan * sizeof(struct ieee80211_reg_rule);

	regd = kzalloc(regd_size, GFP_KERNEL);
	if (!regd)
		return ERR_PTR(-ENOMEM);

	for (idx = 0; idx < num_chan; idx++) {
		u8 chan;
		enum nl80211_band band;

		chan = *buf++;
		if (!chan) {
			kfree(regd);
			return NULL;
		}
		chflags = *buf++;
		band = (chan <= 14) ? NL80211_BAND_2GHZ : NL80211_BAND_5GHZ;
		freq = ieee80211_channel_to_frequency(chan, band);
		new_rule = false;

		if (chflags & MWIFIEX_CHANNEL_DISABLED)
			continue;

		if (band == NL80211_BAND_5GHZ) {
			if (!(chflags & MWIFIEX_CHANNEL_NOHT80))
				bw = MHZ_TO_KHZ(80);
			else if (!(chflags & MWIFIEX_CHANNEL_NOHT40))
				bw = MHZ_TO_KHZ(40);
			else
				bw = MHZ_TO_KHZ(20);
		} else {
			if (!(chflags & MWIFIEX_CHANNEL_NOHT40))
				bw = MHZ_TO_KHZ(40);
			else
				bw = MHZ_TO_KHZ(20);
		}

		if (idx == 0 || prev_chflags != chflags || prev_bw != bw ||
		    freq - prev_freq > 20) {
			valid_rules++;
			new_rule = true;
		}

		rule = &regd->reg_rules[valid_rules - 1];

		rule->freq_range.end_freq_khz = MHZ_TO_KHZ(freq + 10);

		prev_chflags = chflags;
		prev_freq = freq;
		prev_bw = bw;

		if (!new_rule)
			continue;

		rule->freq_range.start_freq_khz = MHZ_TO_KHZ(freq - 10);
		rule->power_rule.max_eirp = DBM_TO_MBM(19);

		if (chflags & MWIFIEX_CHANNEL_PASSIVE)
			rule->flags = NL80211_RRF_NO_IR;

		if (chflags & MWIFIEX_CHANNEL_DFS)
			rule->flags = NL80211_RRF_DFS;

		rule->freq_range.max_bandwidth_khz = bw;
	}

	regd->n_reg_rules = valid_rules;
	regd->alpha2[0] = '9';
	regd->alpha2[1] = '9';

	return regd;
}

static int mwifiex_ret_chan_region_cfg(struct mwifiex_private *priv,
				       struct host_cmd_ds_command *resp)
{
	struct host_cmd_ds_chan_region_cfg *reg = &resp->params.reg_cfg;
	u16 action = le16_to_cpu(reg->action);
	u16 tlv, tlv_buf_len, tlv_buf_left;
	struct mwifiex_ie_types_header *head;
	struct ieee80211_regdomain *regd;
	u8 *tlv_buf;

	if (action != HostCmd_ACT_GEN_GET)
		return 0;

	tlv_buf = (u8 *)reg + sizeof(*reg);
	tlv_buf_left = le16_to_cpu(resp->size) - S_DS_GEN - sizeof(*reg);

	while (tlv_buf_left >= sizeof(*head)) {
		head = (struct mwifiex_ie_types_header *)tlv_buf;
		tlv = le16_to_cpu(head->type);
		tlv_buf_len = le16_to_cpu(head->len);

		if (tlv_buf_left < (sizeof(*head) + tlv_buf_len))
			break;

		switch (tlv) {
		case TLV_TYPE_CHAN_ATTR_CFG:
			mwifiex_dbg_dump(priv->adapter, CMD_D, "CHAN:",
					 (u8 *)head + sizeof(*head),
					 tlv_buf_len);
			regd = mwifiex_create_custom_regdomain(priv,
				(u8 *)head + sizeof(*head), tlv_buf_len);
			if (!IS_ERR(regd))
				priv->adapter->regd = regd;
			break;
		}

		tlv_buf += (sizeof(*head) + tlv_buf_len);
		tlv_buf_left -= (sizeof(*head) + tlv_buf_len);
	}

	return 0;
}

/*
 * This function handles the command responses.
 *
 * This is a generic function, which calls command specific
 * response handlers based on the command ID.
 */
int mwifiex_process_sta_cmdresp(struct mwifiex_private *priv, u16 cmdresp_no,
				struct host_cmd_ds_command *resp)
{
	int ret = 0;
	struct mwifiex_adapter *adapter = priv->adapter;
	void *data_buf = adapter->curr_cmd->data_buf;

	/* If the command is not successful, cleanup and return failure */
	if (resp->result != HostCmd_RESULT_OK) {
		mwifiex_process_cmdresp_error(priv, resp);
		return -1;
	}
	/* Command successful, handle response */
	switch (cmdresp_no) {
	case HostCmd_CMD_GET_HW_SPEC:
		ret = mwifiex_ret_get_hw_spec(priv, resp);
		break;
	case HostCmd_CMD_CFG_DATA:
		ret = mwifiex_ret_cfg_data(priv, resp);
		break;
	case HostCmd_CMD_MAC_CONTROL:
		break;
	case HostCmd_CMD_802_11_MAC_ADDRESS:
		ret = mwifiex_ret_802_11_mac_address(priv, resp);
		break;
	case HostCmd_CMD_MAC_MULTICAST_ADR:
		ret = mwifiex_ret_mac_multicast_adr(priv, resp);
		break;
	case HostCmd_CMD_TX_RATE_CFG:
		ret = mwifiex_ret_tx_rate_cfg(priv, resp);
		break;
	case HostCmd_CMD_802_11_SCAN:
		ret = mwifiex_ret_802_11_scan(priv, resp);
		adapter->curr_cmd->wait_q_enabled = false;
		break;
	case HostCmd_CMD_802_11_SCAN_EXT:
		ret = mwifiex_ret_802_11_scan_ext(priv, resp);
		adapter->curr_cmd->wait_q_enabled = false;
		break;
	case HostCmd_CMD_802_11_BG_SCAN_QUERY:
		ret = mwifiex_ret_802_11_scan(priv, resp);
		cfg80211_sched_scan_results(priv->wdev.wiphy, 0);
		mwifiex_dbg(adapter, CMD,
			    "info: CMD_RESP: BG_SCAN result is ready!\n");
		break;
	case HostCmd_CMD_802_11_BG_SCAN_CONFIG:
		break;
	case HostCmd_CMD_TXPWR_CFG:
		ret = mwifiex_ret_tx_power_cfg(priv, resp);
		break;
	case HostCmd_CMD_RF_TX_PWR:
		ret = mwifiex_ret_rf_tx_power(priv, resp);
		break;
	case HostCmd_CMD_RF_ANTENNA:
		ret = mwifiex_ret_rf_antenna(priv, resp);
		break;
	case HostCmd_CMD_802_11_PS_MODE_ENH:
		ret = mwifiex_ret_enh_power_mode(priv, resp, data_buf);
		break;
	case HostCmd_CMD_802_11_HS_CFG_ENH:
		ret = mwifiex_ret_802_11_hs_cfg(priv, resp);
		break;
	case HostCmd_CMD_802_11_ASSOCIATE:
		ret = mwifiex_ret_802_11_associate(priv, resp);
		break;
	case HostCmd_CMD_802_11_DEAUTHENTICATE:
		ret = mwifiex_ret_802_11_deauthenticate(priv, resp);
		break;
	case HostCmd_CMD_802_11_AD_HOC_START:
	case HostCmd_CMD_802_11_AD_HOC_JOIN:
		ret = mwifiex_ret_802_11_ad_hoc(priv, resp);
		break;
	case HostCmd_CMD_802_11_AD_HOC_STOP:
		ret = mwifiex_ret_802_11_ad_hoc_stop(priv, resp);
		break;
	case HostCmd_CMD_802_11_GET_LOG:
		ret = mwifiex_ret_get_log(priv, resp, data_buf);
		break;
	case HostCmd_CMD_RSSI_INFO:
		ret = mwifiex_ret_802_11_rssi_info(priv, resp);
		break;
	case HostCmd_CMD_802_11_SNMP_MIB:
		ret = mwifiex_ret_802_11_snmp_mib(priv, resp, data_buf);
		break;
	case HostCmd_CMD_802_11_TX_RATE_QUERY:
		ret = mwifiex_ret_802_11_tx_rate_query(priv, resp);
		break;
	case HostCmd_CMD_VERSION_EXT:
		ret = mwifiex_ret_ver_ext(priv, resp, data_buf);
		break;
	case HostCmd_CMD_REMAIN_ON_CHAN:
		ret = mwifiex_ret_remain_on_chan(priv, resp, data_buf);
		break;
	case HostCmd_CMD_11AC_CFG:
		break;
	case HostCmd_CMD_P2P_MODE_CFG:
		ret = mwifiex_ret_p2p_mode_cfg(priv, resp, data_buf);
		break;
	case HostCmd_CMD_MGMT_FRAME_REG:
	case HostCmd_CMD_FUNC_INIT:
	case HostCmd_CMD_FUNC_SHUTDOWN:
		break;
	case HostCmd_CMD_802_11_KEY_MATERIAL:
		ret = mwifiex_ret_802_11_key_material(priv, resp);
		break;
	case HostCmd_CMD_802_11D_DOMAIN_INFO:
		ret = mwifiex_ret_802_11d_domain_info(priv, resp);
		break;
	case HostCmd_CMD_11N_ADDBA_REQ:
		ret = mwifiex_ret_11n_addba_req(priv, resp);
		break;
	case HostCmd_CMD_11N_DELBA:
		ret = mwifiex_ret_11n_delba(priv, resp);
		break;
	case HostCmd_CMD_11N_ADDBA_RSP:
		ret = mwifiex_ret_11n_addba_resp(priv, resp);
		break;
	case HostCmd_CMD_RECONFIGURE_TX_BUFF:
		if (0xffff == (u16)le16_to_cpu(resp->params.tx_buf.buff_size)) {
			if (adapter->iface_type == MWIFIEX_USB &&
			    adapter->usb_mc_setup) {
				if (adapter->if_ops.multi_port_resync)
					adapter->if_ops.
						multi_port_resync(adapter);
				adapter->usb_mc_setup = false;
				adapter->tx_lock_flag = false;
			}
			break;
		}
		adapter->tx_buf_size = (u16) le16_to_cpu(resp->params.
							     tx_buf.buff_size);
		adapter->tx_buf_size = (adapter->tx_buf_size
					/ MWIFIEX_SDIO_BLOCK_SIZE)
				       * MWIFIEX_SDIO_BLOCK_SIZE;
		adapter->curr_tx_buf_size = adapter->tx_buf_size;
		mwifiex_dbg(adapter, CMD, "cmd: curr_tx_buf_size=%d\n",
			    adapter->curr_tx_buf_size);

		if (adapter->if_ops.update_mp_end_port)
			adapter->if_ops.update_mp_end_port(adapter,
				le16_to_cpu(resp->params.tx_buf.mp_end_port));
		break;
	case HostCmd_CMD_AMSDU_AGGR_CTRL:
		break;
	case HostCmd_CMD_WMM_GET_STATUS:
		ret = mwifiex_ret_wmm_get_status(priv, resp);
		break;
	case HostCmd_CMD_802_11_IBSS_COALESCING_STATUS:
		ret = mwifiex_ret_ibss_coalescing_status(priv, resp);
		break;
	case HostCmd_CMD_MEM_ACCESS:
		ret = mwifiex_ret_mem_access(priv, resp, data_buf);
		break;
	case HostCmd_CMD_MAC_REG_ACCESS:
	case HostCmd_CMD_BBP_REG_ACCESS:
	case HostCmd_CMD_RF_REG_ACCESS:
	case HostCmd_CMD_PMIC_REG_ACCESS:
	case HostCmd_CMD_CAU_REG_ACCESS:
	case HostCmd_CMD_802_11_EEPROM_ACCESS:
		ret = mwifiex_ret_reg_access(cmdresp_no, resp, data_buf);
		break;
	case HostCmd_CMD_SET_BSS_MODE:
		break;
	case HostCmd_CMD_11N_CFG:
		break;
	case HostCmd_CMD_PCIE_DESC_DETAILS:
		break;
	case HostCmd_CMD_802_11_SUBSCRIBE_EVENT:
		ret = mwifiex_ret_subsc_evt(priv, resp);
		break;
	case HostCmd_CMD_UAP_SYS_CONFIG:
		break;
	case HOST_CMD_APCMD_STA_LIST:
		ret = mwifiex_ret_uap_sta_list(priv, resp);
		break;
	case HostCmd_CMD_UAP_BSS_START:
		adapter->tx_lock_flag = false;
		adapter->pps_uapsd_mode = false;
		adapter->delay_null_pkt = false;
		priv->bss_started = 1;
		break;
	case HostCmd_CMD_UAP_BSS_STOP:
		priv->bss_started = 0;
		break;
	case HostCmd_CMD_UAP_STA_DEAUTH:
		break;
	case HOST_CMD_APCMD_SYS_RESET:
		break;
	case HostCmd_CMD_MEF_CFG:
		break;
	case HostCmd_CMD_COALESCE_CFG:
		break;
	case HostCmd_CMD_TDLS_OPER:
		ret = mwifiex_ret_tdls_oper(priv, resp);
	case HostCmd_CMD_MC_POLICY:
		break;
	case HostCmd_CMD_CHAN_REPORT_REQUEST:
		break;
	case HostCmd_CMD_SDIO_SP_RX_AGGR_CFG:
		ret = mwifiex_ret_sdio_rx_aggr_cfg(priv, resp);
		break;
	case HostCmd_CMD_HS_WAKEUP_REASON:
		ret = mwifiex_ret_wakeup_reason(priv, resp, data_buf);
		break;
	case HostCmd_CMD_TDLS_CONFIG:
		break;
	case HostCmd_CMD_ROBUST_COEX:
		ret = mwifiex_ret_robust_coex(priv, resp, data_buf);
		break;
	case HostCmd_CMD_GTK_REKEY_OFFLOAD_CFG:
		break;
	case HostCmd_CMD_CHAN_REGION_CFG:
		ret = mwifiex_ret_chan_region_cfg(priv, resp);
		break;
	default:
		mwifiex_dbg(adapter, ERROR,
			    "CMD_RESP: unknown cmd response %#x\n",
			    resp->command);
		break;
	}

	return ret;
}
