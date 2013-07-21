/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2013 Intel Corporation. All rights reserved.
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
 * BSD LICENSE
 *
 * Copyright(c) 2013 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/

#include <net/mac80211.h>

#include "fw-api-bt-coex.h"
#include "iwl-modparams.h"
#include "mvm.h"
#include "iwl-debug.h"

#define EVENT_PRIO_ANT(_evt, _prio, _shrd_ant)			\
	[(_evt)] = (((_prio) << BT_COEX_PRIO_TBL_PRIO_POS) |	\
		   ((_shrd_ant) << BT_COEX_PRIO_TBL_SHRD_ANT_POS))

static const u8 iwl_bt_prio_tbl[BT_COEX_PRIO_TBL_EVT_MAX] = {
	EVENT_PRIO_ANT(BT_COEX_PRIO_TBL_EVT_INIT_CALIB1,
		       BT_COEX_PRIO_TBL_PRIO_BYPASS, 0),
	EVENT_PRIO_ANT(BT_COEX_PRIO_TBL_EVT_INIT_CALIB2,
		       BT_COEX_PRIO_TBL_PRIO_BYPASS, 1),
	EVENT_PRIO_ANT(BT_COEX_PRIO_TBL_EVT_PERIODIC_CALIB_LOW1,
		       BT_COEX_PRIO_TBL_PRIO_LOW, 0),
	EVENT_PRIO_ANT(BT_COEX_PRIO_TBL_EVT_PERIODIC_CALIB_LOW2,
		       BT_COEX_PRIO_TBL_PRIO_LOW, 1),
	EVENT_PRIO_ANT(BT_COEX_PRIO_TBL_EVT_PERIODIC_CALIB_HIGH1,
		       BT_COEX_PRIO_TBL_PRIO_HIGH, 0),
	EVENT_PRIO_ANT(BT_COEX_PRIO_TBL_EVT_PERIODIC_CALIB_HIGH2,
		       BT_COEX_PRIO_TBL_PRIO_HIGH, 1),
	EVENT_PRIO_ANT(BT_COEX_PRIO_TBL_EVT_DTIM,
		       BT_COEX_PRIO_TBL_DISABLED, 0),
	EVENT_PRIO_ANT(BT_COEX_PRIO_TBL_EVT_SCAN52,
		       BT_COEX_PRIO_TBL_PRIO_COEX_OFF, 0),
	EVENT_PRIO_ANT(BT_COEX_PRIO_TBL_EVT_SCAN24,
		       BT_COEX_PRIO_TBL_PRIO_COEX_ON, 0),
	EVENT_PRIO_ANT(BT_COEX_PRIO_TBL_EVT_IDLE,
		       BT_COEX_PRIO_TBL_PRIO_COEX_IDLE, 0),
	0, 0, 0, 0, 0, 0,
};

#undef EVENT_PRIO_ANT

/* BT Antenna Coupling Threshold (dB) */
#define IWL_BT_ANTENNA_COUPLING_THRESHOLD	(35)
#define IWL_BT_LOAD_FORCE_SISO_THRESHOLD	(3)

#define BT_ENABLE_REDUCED_TXPOWER_THRESHOLD	(-62)
#define BT_DISABLE_REDUCED_TXPOWER_THRESHOLD	(-65)
#define BT_REDUCED_TX_POWER_BIT			BIT(7)

static inline bool is_loose_coex(void)
{
	return iwlwifi_mod_params.ant_coupling >
		IWL_BT_ANTENNA_COUPLING_THRESHOLD;
}

int iwl_send_bt_prio_tbl(struct iwl_mvm *mvm)
{
	return iwl_mvm_send_cmd_pdu(mvm, BT_COEX_PRIO_TABLE, CMD_SYNC,
				    sizeof(struct iwl_bt_coex_prio_tbl_cmd),
				    &iwl_bt_prio_tbl);
}

static int iwl_send_bt_env(struct iwl_mvm *mvm, u8 action, u8 type)
{
	struct iwl_bt_coex_prot_env_cmd env_cmd;
	int ret;

	env_cmd.action = action;
	env_cmd.type = type;
	ret = iwl_mvm_send_cmd_pdu(mvm, BT_COEX_PROT_ENV, CMD_SYNC,
				   sizeof(env_cmd), &env_cmd);
	if (ret)
		IWL_ERR(mvm, "failed to send BT env command\n");
	return ret;
}

enum iwl_bt_kill_msk {
	BT_KILL_MSK_DEFAULT,
	BT_KILL_MSK_SCO_HID_A2DP,
	BT_KILL_MSK_REDUCED_TXPOW,
	BT_KILL_MSK_MAX,
};

static const u32 iwl_bt_ack_kill_msk[BT_KILL_MSK_MAX] = {
	[BT_KILL_MSK_DEFAULT] = 0xffff0000,
	[BT_KILL_MSK_SCO_HID_A2DP] = 0xffffffff,
	[BT_KILL_MSK_REDUCED_TXPOW] = 0,
};

static const u32 iwl_bt_cts_kill_msk[BT_KILL_MSK_MAX] = {
	[BT_KILL_MSK_DEFAULT] = 0xffff0000,
	[BT_KILL_MSK_SCO_HID_A2DP] = 0xffffffff,
	[BT_KILL_MSK_REDUCED_TXPOW] = 0,
};

#define IWL_BT_DEFAULT_BOOST (0xf0f0f0f0)

/* Tight Coex */
static const __le32 iwl_tight_lookup[BT_COEX_LUT_SIZE] = {
	cpu_to_le32(0xaaaaaaaa),
	cpu_to_le32(0xaaaaaaaa),
	cpu_to_le32(0xaeaaaaaa),
	cpu_to_le32(0xaaaaaaaa),
	cpu_to_le32(0xcc00ff28),
	cpu_to_le32(0x0000aaaa),
	cpu_to_le32(0xcc00aaaa),
	cpu_to_le32(0x0000aaaa),
	cpu_to_le32(0xc0004000),
	cpu_to_le32(0x00000000),
	cpu_to_le32(0xf0005000),
	cpu_to_le32(0xf0005000),
};

/* Loose Coex */
static const __le32 iwl_loose_lookup[BT_COEX_LUT_SIZE] = {
	cpu_to_le32(0xaaaaaaaa),
	cpu_to_le32(0xaaaaaaaa),
	cpu_to_le32(0xaaaaaaaa),
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
static const __le32 iwl_concurrent_lookup[BT_COEX_LUT_SIZE] = {
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

/* single shared antenna */
static const __le32 iwl_single_shared_ant_lookup[BT_COEX_LUT_SIZE] = {
	cpu_to_le32(0x40000000),
	cpu_to_le32(0x00000000),
	cpu_to_le32(0x44000000),
	cpu_to_le32(0x00000000),
	cpu_to_le32(0x40000000),
	cpu_to_le32(0x00000000),
	cpu_to_le32(0x44000000),
	cpu_to_le32(0x00000000),
	cpu_to_le32(0xC0004000),
	cpu_to_le32(0xF0005000),
	cpu_to_le32(0xC0004000),
	cpu_to_le32(0xF0005000),
};

int iwl_send_bt_init_conf(struct iwl_mvm *mvm)
{
	struct iwl_bt_coex_cmd *bt_cmd;
	struct iwl_host_cmd cmd = {
		.id = BT_CONFIG,
		.len = { sizeof(*bt_cmd), },
		.dataflags = { IWL_HCMD_DFL_NOCOPY, },
		.flags = CMD_SYNC,
	};
	int ret;

	/* go to CALIB state in internal BT-Coex state machine */
	ret = iwl_send_bt_env(mvm, BT_COEX_ENV_OPEN,
			      BT_COEX_PRIO_TBL_EVT_INIT_CALIB2);
	if (ret)
		return ret;

	ret  = iwl_send_bt_env(mvm, BT_COEX_ENV_CLOSE,
			       BT_COEX_PRIO_TBL_EVT_INIT_CALIB2);
	if (ret)
		return ret;

	bt_cmd = kzalloc(sizeof(*bt_cmd), GFP_KERNEL);
	if (!bt_cmd)
		return -ENOMEM;
	cmd.data[0] = bt_cmd;

	bt_cmd->max_kill = 5;
	bt_cmd->bt3_time_t7_value = 1;
	bt_cmd->bt3_prio_sample_time = 2;
	bt_cmd->bt3_timer_t2_value = 0xc;

	bt_cmd->flags = iwlwifi_mod_params.bt_coex_active ?
			BT_COEX_NW : BT_COEX_DISABLE;
	bt_cmd->flags |= BT_CH_PRIMARY_EN | BT_SYNC_2_BT_DISABLE;

	bt_cmd->valid_bit_msk = cpu_to_le16(BT_VALID_ENABLE |
					    BT_VALID_BT_PRIO_BOOST |
					    BT_VALID_MAX_KILL |
					    BT_VALID_3W_TMRS |
					    BT_VALID_KILL_ACK |
					    BT_VALID_KILL_CTS |
					    BT_VALID_REDUCED_TX_POWER |
					    BT_VALID_LUT);

	if (mvm->cfg->bt_shared_single_ant)
		memcpy(&bt_cmd->decision_lut, iwl_single_shared_ant_lookup,
		       sizeof(iwl_single_shared_ant_lookup));
	else if (is_loose_coex())
		memcpy(&bt_cmd->decision_lut, iwl_loose_lookup,
		       sizeof(iwl_tight_lookup));
	else
		memcpy(&bt_cmd->decision_lut, iwl_tight_lookup,
		       sizeof(iwl_tight_lookup));

	bt_cmd->bt_prio_boost = cpu_to_le32(IWL_BT_DEFAULT_BOOST);
	bt_cmd->kill_ack_msk =
		cpu_to_le32(iwl_bt_ack_kill_msk[BT_KILL_MSK_DEFAULT]);
	bt_cmd->kill_cts_msk =
		cpu_to_le32(iwl_bt_cts_kill_msk[BT_KILL_MSK_DEFAULT]);

	memset(&mvm->last_bt_notif, 0, sizeof(mvm->last_bt_notif));

	ret = iwl_mvm_send_cmd(mvm, &cmd);

	kfree(bt_cmd);
	return ret;
}

static int iwl_mvm_bt_udpate_ctrl_kill_msk(struct iwl_mvm *mvm,
					   bool reduced_tx_power)
{
	enum iwl_bt_kill_msk bt_kill_msk;
	struct iwl_bt_coex_cmd *bt_cmd;
	struct iwl_bt_coex_profile_notif *notif = &mvm->last_bt_notif;
	struct iwl_host_cmd cmd = {
		.id = BT_CONFIG,
		.data[0] = &bt_cmd,
		.len = { sizeof(*bt_cmd), },
		.dataflags = { IWL_HCMD_DFL_NOCOPY, },
		.flags = CMD_SYNC,
	};
	int ret = 0;

	lockdep_assert_held(&mvm->mutex);

	if (reduced_tx_power) {
		/* Reduced Tx power has precedence on the type of the profile */
		bt_kill_msk = BT_KILL_MSK_REDUCED_TXPOW;
	} else {
		/* Low latency BT profile is active: give higher prio to BT */
		if (BT_MBOX_MSG(notif, 3, SCO_STATE)  ||
		    BT_MBOX_MSG(notif, 3, A2DP_STATE) ||
		    BT_MBOX_MSG(notif, 3, SNIFF_STATE))
			bt_kill_msk = BT_KILL_MSK_SCO_HID_A2DP;
		else
			bt_kill_msk = BT_KILL_MSK_DEFAULT;
	}

	IWL_DEBUG_COEX(mvm,
		       "Update kill_msk: %d - SCO %sactive A2DP %sactive SNIFF %sactive\n",
		       bt_kill_msk,
		       BT_MBOX_MSG(notif, 3, SCO_STATE) ? "" : "in",
		       BT_MBOX_MSG(notif, 3, A2DP_STATE) ? "" : "in",
		       BT_MBOX_MSG(notif, 3, SNIFF_STATE) ? "" : "in");

	/* Don't send HCMD if there is no update */
	if (bt_kill_msk == mvm->bt_kill_msk)
		return 0;

	mvm->bt_kill_msk = bt_kill_msk;

	bt_cmd = kzalloc(sizeof(*bt_cmd), GFP_KERNEL);
	if (!bt_cmd)
		return -ENOMEM;
	cmd.data[0] = bt_cmd;

	bt_cmd->kill_ack_msk = cpu_to_le32(iwl_bt_ack_kill_msk[bt_kill_msk]);
	bt_cmd->kill_cts_msk = cpu_to_le32(iwl_bt_cts_kill_msk[bt_kill_msk]);
	bt_cmd->valid_bit_msk =
		cpu_to_le16(BT_VALID_KILL_ACK | BT_VALID_KILL_CTS);

	IWL_DEBUG_COEX(mvm, "bt_kill_msk = %d\n", bt_kill_msk);

	ret = iwl_mvm_send_cmd(mvm, &cmd);

	kfree(bt_cmd);
	return ret;
}

static int iwl_mvm_bt_coex_reduced_txp(struct iwl_mvm *mvm, u8 sta_id,
				       bool enable)
{
	struct iwl_bt_coex_cmd *bt_cmd;
	/* Send ASYNC since this can be sent from an atomic context */
	struct iwl_host_cmd cmd = {
		.id = BT_CONFIG,
		.len = { sizeof(*bt_cmd), },
		.dataflags = { IWL_HCMD_DFL_DUP, },
		.flags = CMD_ASYNC,
	};

	struct ieee80211_sta *sta;
	struct iwl_mvm_sta *mvmsta;
	int ret;

	/* This can happen if the station has been removed right now */
	if (sta_id == IWL_MVM_STATION_COUNT)
		return 0;

	sta = rcu_dereference_protected(mvm->fw_id_to_mac_id[sta_id],
					lockdep_is_held(&mvm->mutex));
	mvmsta = (void *)sta->drv_priv;

	/* nothing to do */
	if (mvmsta->bt_reduced_txpower == enable)
		return 0;

	bt_cmd = kzalloc(sizeof(*bt_cmd), GFP_ATOMIC);
	if (!bt_cmd)
		return -ENOMEM;
	cmd.data[0] = bt_cmd;

	bt_cmd->valid_bit_msk = cpu_to_le16(BT_VALID_REDUCED_TX_POWER),
	bt_cmd->bt_reduced_tx_power = sta_id;

	if (enable)
		bt_cmd->bt_reduced_tx_power |= BT_REDUCED_TX_POWER_BIT;

	IWL_DEBUG_COEX(mvm, "%sable reduced Tx Power for sta %d\n",
		       enable ? "en" : "dis", sta_id);

	mvmsta->bt_reduced_txpower = enable;

	ret = iwl_mvm_send_cmd(mvm, &cmd);

	kfree(bt_cmd);
	return ret;
}

struct iwl_bt_iterator_data {
	struct iwl_bt_coex_profile_notif *notif;
	struct iwl_mvm *mvm;
	u32 num_bss_ifaces;
	bool reduced_tx_power;
};

static void iwl_mvm_bt_notif_iterator(void *_data, u8 *mac,
				      struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_bt_iterator_data *data = _data;
	struct iwl_mvm *mvm = data->mvm;
	struct ieee80211_chanctx_conf *chanctx_conf;
	enum ieee80211_smps_mode smps_mode;
	enum ieee80211_band band;
	int ave_rssi;

	lockdep_assert_held(&mvm->mutex);
	if (vif->type != NL80211_IFTYPE_STATION)
		return;

	rcu_read_lock();
	chanctx_conf = rcu_dereference(vif->chanctx_conf);
	if (chanctx_conf && chanctx_conf->def.chan)
		band = chanctx_conf->def.chan->band;
	else
		band = -1;
	rcu_read_unlock();

	smps_mode = IEEE80211_SMPS_AUTOMATIC;

	/* non associated BSSes aren't to be considered */
	if (!vif->bss_conf.assoc)
		return;

	if (band != IEEE80211_BAND_2GHZ) {
		iwl_mvm_update_smps(mvm, vif, IWL_MVM_SMPS_REQ_BT_COEX,
				    smps_mode);
		return;
	}

	if (data->notif->bt_status)
		smps_mode = IEEE80211_SMPS_DYNAMIC;

	if (data->notif->bt_traffic_load >= IWL_BT_LOAD_FORCE_SISO_THRESHOLD)
		smps_mode = IEEE80211_SMPS_STATIC;

	IWL_DEBUG_COEX(data->mvm,
		       "mac %d: bt_status %d traffic_load %d smps_req %d\n",
		       mvmvif->id,  data->notif->bt_status,
		       data->notif->bt_traffic_load, smps_mode);

	iwl_mvm_update_smps(mvm, vif, IWL_MVM_SMPS_REQ_BT_COEX, smps_mode);

	/* don't reduce the Tx power if in loose scheme */
	if (is_loose_coex())
		return;

	data->num_bss_ifaces++;

	/* reduced Txpower only if there are open BT connections, so ...*/
	if (!BT_MBOX_MSG(data->notif, 3, OPEN_CON_2)) {
		/* ... cancel reduced Tx power ... */
		if (iwl_mvm_bt_coex_reduced_txp(mvm, mvmvif->ap_sta_id, false))
			IWL_ERR(mvm, "Couldn't send BT_CONFIG cmd\n");
		data->reduced_tx_power = false;

		/* ... and there is no need to get reports on RSSI any more. */
		mvmvif->bf_data.last_bt_coex_event = 0;
		mvmvif->bf_data.bt_coex_max_thold = 0;
		mvmvif->bf_data.bt_coex_min_thold = 0;
		return;
	}

	/* try to get the avg rssi from fw */
	ave_rssi = mvmvif->bf_data.ave_beacon_signal;

	/* if the RSSI isn't valid, fake it is very low */
	if (!ave_rssi)
		ave_rssi = -100;
	if (ave_rssi > BT_ENABLE_REDUCED_TXPOWER_THRESHOLD) {
		if (iwl_mvm_bt_coex_reduced_txp(mvm, mvmvif->ap_sta_id, true))
			IWL_ERR(mvm, "Couldn't send BT_CONFIG cmd\n");

		/*
		 * bt_kill_msk can be BT_KILL_MSK_REDUCED_TXPOW only if all the
		 * BSS / P2P clients have rssi above threshold.
		 * We set the bt_kill_msk to BT_KILL_MSK_REDUCED_TXPOW before
		 * the iteration, if one interface's rssi isn't good enough,
		 * bt_kill_msk will be set to default values.
		 */
	} else if (ave_rssi < BT_DISABLE_REDUCED_TXPOWER_THRESHOLD) {
		if (iwl_mvm_bt_coex_reduced_txp(mvm, mvmvif->ap_sta_id, false))
			IWL_ERR(mvm, "Couldn't send BT_CONFIG cmd\n");

		/*
		 * One interface hasn't rssi above threshold, bt_kill_msk must
		 * be set to default values.
		 */
		data->reduced_tx_power = false;
	}

	/* Begin to monitor the RSSI: it may influence the reduced Tx power */

	/* reset previous bt coex event tracking */
	mvmvif->bf_data.last_bt_coex_event = 0;
	mvmvif->bf_data.bt_coex_max_thold =
		BT_ENABLE_REDUCED_TXPOWER_THRESHOLD;
	mvmvif->bf_data.bt_coex_min_thold =
		BT_DISABLE_REDUCED_TXPOWER_THRESHOLD;
}

static void iwl_mvm_bt_coex_notif_handle(struct iwl_mvm *mvm)
{
	struct iwl_bt_iterator_data data = {
		.mvm = mvm,
		.notif = &mvm->last_bt_notif,
		.reduced_tx_power = true,
	};

	ieee80211_iterate_active_interfaces_atomic(
					mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
					iwl_mvm_bt_notif_iterator, &data);

	/*
	 * If there are no BSS / P2P client interfaces, reduced Tx Power is
	 * irrelevant since it is based on the RSSI coming from the beacon.
	 * Use BT_KILL_MSK_DEFAULT in that case.
	 */
	data.reduced_tx_power = data.reduced_tx_power && data.num_bss_ifaces;

	if (iwl_mvm_bt_udpate_ctrl_kill_msk(mvm, data.reduced_tx_power))
		IWL_ERR(mvm, "Failed to update the ctrl_kill_msk\n");
}

/* upon association, the fw will send in BT Coex notification */
int iwl_mvm_rx_bt_coex_notif(struct iwl_mvm *mvm,
			     struct iwl_rx_cmd_buffer *rxb,
			     struct iwl_device_cmd *dev_cmd)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_bt_coex_profile_notif *notif = (void *)pkt->data;


	IWL_DEBUG_COEX(mvm, "BT Coex Notification received\n");
	IWL_DEBUG_COEX(mvm, "\tBT %salive\n", notif->bt_status ? "" : "not ");
	IWL_DEBUG_COEX(mvm, "\tBT open conn %d\n", notif->bt_open_conn);
	IWL_DEBUG_COEX(mvm, "\tBT traffic load %d\n", notif->bt_traffic_load);
	IWL_DEBUG_COEX(mvm, "\tBT agg traffic load %d\n",
		       notif->bt_agg_traffic_load);
	IWL_DEBUG_COEX(mvm, "\tBT ci compliance %d\n", notif->bt_ci_compliance);

	/* remember this notification for future use: rssi fluctuations */
	memcpy(&mvm->last_bt_notif, notif, sizeof(mvm->last_bt_notif));

	iwl_mvm_bt_coex_notif_handle(mvm);

	/*
	 * This is an async handler for a notification, returning anything other
	 * than 0 doesn't make sense even if HCMD failed.
	 */
	return 0;
}

static void iwl_mvm_bt_rssi_iterator(void *_data, u8 *mac,
				   struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = (void *)vif->drv_priv;
	struct iwl_bt_iterator_data *data = _data;
	struct iwl_mvm *mvm = data->mvm;

	struct ieee80211_sta *sta;
	struct iwl_mvm_sta *mvmsta;

	if (vif->type != NL80211_IFTYPE_STATION ||
	    mvmvif->ap_sta_id == IWL_MVM_STATION_COUNT)
		return;

	sta = rcu_dereference_protected(mvm->fw_id_to_mac_id[mvmvif->ap_sta_id],
					lockdep_is_held(&mvm->mutex));
	mvmsta = (void *)sta->drv_priv;

	data->num_bss_ifaces++;

	/*
	 * This interface doesn't support reduced Tx power (because of low
	 * RSSI probably), then set bt_kill_msk to default values.
	 */
	if (!mvmsta->bt_reduced_txpower)
		data->reduced_tx_power = false;
	/* else - possibly leave it to BT_KILL_MSK_REDUCED_TXPOW */
}

void iwl_mvm_bt_rssi_event(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			   enum ieee80211_rssi_event rssi_event)
{
	struct iwl_mvm_vif *mvmvif = (void *)vif->drv_priv;
	struct iwl_bt_iterator_data data = {
		.mvm = mvm,
		.reduced_tx_power = true,
	};
	int ret;

	mutex_lock(&mvm->mutex);

	/* Rssi update while not associated ?! */
	if (WARN_ON_ONCE(mvmvif->ap_sta_id == IWL_MVM_STATION_COUNT))
		goto out_unlock;

	/* No open connection - reports should be disabled */
	if (!BT_MBOX_MSG(&mvm->last_bt_notif, 3, OPEN_CON_2))
		goto out_unlock;

	IWL_DEBUG_COEX(mvm, "RSSI for %pM is now %s\n", vif->bss_conf.bssid,
		       rssi_event == RSSI_EVENT_HIGH ? "HIGH" : "LOW");

	/*
	 * Check if rssi is good enough for reduced Tx power, but not in loose
	 * scheme.
	 */
	if (rssi_event == RSSI_EVENT_LOW || is_loose_coex())
		ret = iwl_mvm_bt_coex_reduced_txp(mvm, mvmvif->ap_sta_id,
						  false);
	else
		ret = iwl_mvm_bt_coex_reduced_txp(mvm, mvmvif->ap_sta_id, true);

	if (ret)
		IWL_ERR(mvm, "couldn't send BT_CONFIG HCMD upon RSSI event\n");

	ieee80211_iterate_active_interfaces_atomic(
		mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
		iwl_mvm_bt_rssi_iterator, &data);

	/*
	 * If there are no BSS / P2P client interfaces, reduced Tx Power is
	 * irrelevant since it is based on the RSSI coming from the beacon.
	 * Use BT_KILL_MSK_DEFAULT in that case.
	 */
	data.reduced_tx_power = data.reduced_tx_power && data.num_bss_ifaces;

	if (iwl_mvm_bt_udpate_ctrl_kill_msk(mvm, data.reduced_tx_power))
		IWL_ERR(mvm, "Failed to update the ctrl_kill_msk\n");

 out_unlock:
	mutex_unlock(&mvm->mutex);
}

void iwl_mvm_bt_coex_vif_assoc(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	iwl_mvm_bt_coex_notif_handle(mvm);
}
