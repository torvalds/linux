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

#define IWL_BT_LOAD_FORCE_SISO_THRESHOLD	(3)

#define BT_ENABLE_REDUCED_TXPOWER_THRESHOLD	(-62)
#define BT_DISABLE_REDUCED_TXPOWER_THRESHOLD	(-65)
#define BT_ANTENNA_COUPLING_THRESHOLD		(30)

static inline bool is_loose_coex(void)
{
	return iwlwifi_mod_params.ant_coupling >
		BT_ANTENNA_COUPLING_THRESHOLD;
}

int iwl_send_bt_prio_tbl(struct iwl_mvm *mvm)
{
	if (!(mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_NEWBT_COEX))
		return 0;

	return iwl_mvm_send_cmd_pdu(mvm, BT_COEX_PRIO_TABLE, CMD_SYNC,
				    sizeof(struct iwl_bt_coex_prio_tbl_cmd),
				    &iwl_bt_prio_tbl);
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

static const __le32 iwl_bt_prio_boost[BT_COEX_BOOST_SIZE] = {
	cpu_to_le32(0xf0f0f0f0),
	cpu_to_le32(0xc0c0c0c0),
	cpu_to_le32(0xfcfcfcfc),
	cpu_to_le32(0xff00ff00),
};

static const __le32 iwl_combined_lookup[BT_COEX_MAX_LUT][BT_COEX_LUT_SIZE] = {
	{
		/* Tight */
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
	},
	{
		/* Loose */
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
	},
	{
		/* Tx Tx disabled */
		cpu_to_le32(0xaaaaaaaa),
		cpu_to_le32(0xaaaaaaaa),
		cpu_to_le32(0xaaaaaaaa),
		cpu_to_le32(0xaaaaaaaa),
		cpu_to_le32(0xcc00ff28),
		cpu_to_le32(0x0000aaaa),
		cpu_to_le32(0xcc00aaaa),
		cpu_to_le32(0x0000aaaa),
		cpu_to_le32(0xC0004000),
		cpu_to_le32(0xC0004000),
		cpu_to_le32(0xF0005000),
		cpu_to_le32(0xF0005000),
	},
};

/* 20MHz / 40MHz below / 40Mhz above*/
static const __le64 iwl_ci_mask[][3] = {
	/* dummy entry for channel 0 */
	{cpu_to_le64(0), cpu_to_le64(0), cpu_to_le64(0)},
	{
		cpu_to_le64(0x0000001FFFULL),
		cpu_to_le64(0x0ULL),
		cpu_to_le64(0x00007FFFFFULL),
	},
	{
		cpu_to_le64(0x000000FFFFULL),
		cpu_to_le64(0x0ULL),
		cpu_to_le64(0x0003FFFFFFULL),
	},
	{
		cpu_to_le64(0x000003FFFCULL),
		cpu_to_le64(0x0ULL),
		cpu_to_le64(0x000FFFFFFCULL),
	},
	{
		cpu_to_le64(0x00001FFFE0ULL),
		cpu_to_le64(0x0ULL),
		cpu_to_le64(0x007FFFFFE0ULL),
	},
	{
		cpu_to_le64(0x00007FFF80ULL),
		cpu_to_le64(0x00007FFFFFULL),
		cpu_to_le64(0x01FFFFFF80ULL),
	},
	{
		cpu_to_le64(0x0003FFFC00ULL),
		cpu_to_le64(0x0003FFFFFFULL),
		cpu_to_le64(0x0FFFFFFC00ULL),
	},
	{
		cpu_to_le64(0x000FFFF000ULL),
		cpu_to_le64(0x000FFFFFFCULL),
		cpu_to_le64(0x3FFFFFF000ULL),
	},
	{
		cpu_to_le64(0x007FFF8000ULL),
		cpu_to_le64(0x007FFFFFE0ULL),
		cpu_to_le64(0xFFFFFF8000ULL),
	},
	{
		cpu_to_le64(0x01FFFE0000ULL),
		cpu_to_le64(0x01FFFFFF80ULL),
		cpu_to_le64(0xFFFFFE0000ULL),
	},
	{
		cpu_to_le64(0x0FFFF00000ULL),
		cpu_to_le64(0x0FFFFFFC00ULL),
		cpu_to_le64(0x0ULL),
	},
	{
		cpu_to_le64(0x3FFFC00000ULL),
		cpu_to_le64(0x3FFFFFF000ULL),
		cpu_to_le64(0x0)
	},
	{
		cpu_to_le64(0xFFFE000000ULL),
		cpu_to_le64(0xFFFFFF8000ULL),
		cpu_to_le64(0x0)
	},
	{
		cpu_to_le64(0xFFF8000000ULL),
		cpu_to_le64(0xFFFFFE0000ULL),
		cpu_to_le64(0x0)
	},
	{
		cpu_to_le64(0xFE00000000ULL),
		cpu_to_le64(0x0ULL),
		cpu_to_le64(0x0)
	},
};

static const __le32 iwl_bt_mprio_lut[BT_COEX_MULTI_PRIO_LUT_SIZE] = {
	cpu_to_le32(0x22002200),
	cpu_to_le32(0x33113311),
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
	u32 flags;

	if (!(mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_NEWBT_COEX))
		return 0;

	bt_cmd = kzalloc(sizeof(*bt_cmd), GFP_KERNEL);
	if (!bt_cmd)
		return -ENOMEM;
	cmd.data[0] = bt_cmd;

	bt_cmd->max_kill = 5;
	bt_cmd->bt4_antenna_isolation_thr = BT_ANTENNA_COUPLING_THRESHOLD,
	bt_cmd->bt4_antenna_isolation = iwlwifi_mod_params.ant_coupling,
	bt_cmd->bt4_tx_tx_delta_freq_thr = 15,
	bt_cmd->bt4_tx_rx_max_freq0 = 15,

	flags = iwlwifi_mod_params.bt_coex_active ?
			BT_COEX_NW : BT_COEX_DISABLE;
	flags |= BT_CH_PRIMARY_EN | BT_CH_SECONDARY_EN | BT_SYNC_2_BT_DISABLE;
	bt_cmd->flags = cpu_to_le32(flags);

	bt_cmd->valid_bit_msk = cpu_to_le32(BT_VALID_ENABLE |
					    BT_VALID_BT_PRIO_BOOST |
					    BT_VALID_MAX_KILL |
					    BT_VALID_3W_TMRS |
					    BT_VALID_KILL_ACK |
					    BT_VALID_KILL_CTS |
					    BT_VALID_REDUCED_TX_POWER |
					    BT_VALID_LUT |
					    BT_VALID_WIFI_RX_SW_PRIO_BOOST |
					    BT_VALID_WIFI_TX_SW_PRIO_BOOST |
					    BT_VALID_MULTI_PRIO_LUT |
					    BT_VALID_CORUN_LUT_20 |
					    BT_VALID_CORUN_LUT_40 |
					    BT_VALID_ANT_ISOLATION |
					    BT_VALID_ANT_ISOLATION_THRS |
					    BT_VALID_TXTX_DELTA_FREQ_THRS |
					    BT_VALID_TXRX_MAX_FREQ_0);

	memcpy(&bt_cmd->decision_lut, iwl_combined_lookup,
	       sizeof(iwl_combined_lookup));
	memcpy(&bt_cmd->bt_prio_boost, iwl_bt_prio_boost,
	       sizeof(iwl_bt_prio_boost));
	memcpy(&bt_cmd->bt4_multiprio_lut, iwl_bt_mprio_lut,
	       sizeof(iwl_bt_mprio_lut));
	bt_cmd->kill_ack_msk =
		cpu_to_le32(iwl_bt_ack_kill_msk[BT_KILL_MSK_DEFAULT]);
	bt_cmd->kill_cts_msk =
		cpu_to_le32(iwl_bt_cts_kill_msk[BT_KILL_MSK_DEFAULT]);

	memset(&mvm->last_bt_notif, 0, sizeof(mvm->last_bt_notif));
	memset(&mvm->last_bt_ci_cmd, 0, sizeof(mvm->last_bt_ci_cmd));

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
		cpu_to_le32(BT_VALID_KILL_ACK | BT_VALID_KILL_CTS);

	IWL_DEBUG_COEX(mvm, "ACK Kill msk = 0x%08x, CTS Kill msk = 0x%08x\n",
		       iwl_bt_ack_kill_msk[bt_kill_msk],
		       iwl_bt_cts_kill_msk[bt_kill_msk]);

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

	bt_cmd->valid_bit_msk = cpu_to_le32(BT_VALID_REDUCED_TX_POWER),
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
	struct ieee80211_chanctx_conf *primary;
	struct ieee80211_chanctx_conf *secondary;
};

/* must be called under rcu_read_lock */
static void iwl_mvm_bt_notif_iterator(void *_data, u8 *mac,
				      struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_bt_iterator_data *data = _data;
	struct iwl_mvm *mvm = data->mvm;
	struct ieee80211_chanctx_conf *chanctx_conf;
	enum ieee80211_smps_mode smps_mode;
	int ave_rssi;

	lockdep_assert_held(&mvm->mutex);

	if (vif->type != NL80211_IFTYPE_STATION &&
	    vif->type != NL80211_IFTYPE_AP)
		return;

	smps_mode = IEEE80211_SMPS_AUTOMATIC;

	chanctx_conf = rcu_dereference(vif->chanctx_conf);

	/* If channel context is invalid or not on 2.4GHz .. */
	if ((!chanctx_conf ||
	     chanctx_conf->def.chan->band != IEEE80211_BAND_2GHZ)) {
		/* ... and it is an associated STATION, relax constraints */
		if (vif->type == NL80211_IFTYPE_STATION && vif->bss_conf.assoc)
			iwl_mvm_update_smps(mvm, vif, IWL_MVM_SMPS_REQ_BT_COEX,
					    smps_mode);
		return;
	}

	/* SoftAP / GO will always be primary */
	if (vif->type == NL80211_IFTYPE_AP) {
		if (!mvmvif->ap_active)
			return;

		/* the Ack / Cts kill mask must be default if AP / GO */
		data->reduced_tx_power = false;

		if (chanctx_conf == data->primary)
			return;

		/* downgrade the current primary no matter what its type is */
		data->secondary = data->primary;
		data->primary = chanctx_conf;
		return;
	}

	/* we are now a STA / P2P Client, and take associated ones only */
	if (!vif->bss_conf.assoc)
		return;

	/* STA / P2P Client, try to be primary if first vif */
	if (!data->primary || data->primary == chanctx_conf)
		data->primary = chanctx_conf;
	else if (!data->secondary)
		/* if secondary is not NULL, it might be a GO */
		data->secondary = chanctx_conf;

	if (data->notif->bt_status)
		smps_mode = IEEE80211_SMPS_DYNAMIC;

	if (le32_to_cpu(data->notif->bt_activity_grading) >=
	    IWL_BT_LOAD_FORCE_SISO_THRESHOLD)
		smps_mode = IEEE80211_SMPS_STATIC;

	IWL_DEBUG_COEX(data->mvm,
		       "mac %d: bt_status %d bt_activity_grading %d smps_req %d\n",
		       mvmvif->id,  data->notif->bt_status,
		       data->notif->bt_activity_grading, smps_mode);

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
	struct iwl_bt_coex_ci_cmd cmd = {};
	u8 ci_bw_idx;

	rcu_read_lock();
	ieee80211_iterate_active_interfaces_atomic(
					mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
					iwl_mvm_bt_notif_iterator, &data);

	if (data.primary) {
		struct ieee80211_chanctx_conf *chan = data.primary;
		if (WARN_ON(!chan->def.chan)) {
			rcu_read_unlock();
			return;
		}

		if (chan->def.width < NL80211_CHAN_WIDTH_40) {
			ci_bw_idx = 0;
			cmd.co_run_bw_primary = 0;
		} else {
			cmd.co_run_bw_primary = 1;
			if (chan->def.center_freq1 >
			    chan->def.chan->center_freq)
				ci_bw_idx = 2;
			else
				ci_bw_idx = 1;
		}

		cmd.bt_primary_ci =
			iwl_ci_mask[chan->def.chan->hw_value][ci_bw_idx];
		cmd.primary_ch_phy_id = *((u16 *)data.primary->drv_priv);
	}

	if (data.secondary) {
		struct ieee80211_chanctx_conf *chan = data.secondary;
		if (WARN_ON(!data.secondary->def.chan)) {
			rcu_read_unlock();
			return;
		}

		if (chan->def.width < NL80211_CHAN_WIDTH_40) {
			ci_bw_idx = 0;
			cmd.co_run_bw_secondary = 0;
		} else {
			cmd.co_run_bw_secondary = 1;
			if (chan->def.center_freq1 >
			    chan->def.chan->center_freq)
				ci_bw_idx = 2;
			else
				ci_bw_idx = 1;
		}

		cmd.bt_secondary_ci =
			iwl_ci_mask[chan->def.chan->hw_value][ci_bw_idx];
		cmd.secondary_ch_phy_id = *((u16 *)data.primary->drv_priv);
	}

	rcu_read_unlock();

	/* Don't spam the fw with the same command over and over */
	if (memcmp(&cmd, &mvm->last_bt_ci_cmd, sizeof(cmd))) {
		if (iwl_mvm_send_cmd_pdu(mvm, BT_COEX_CI, CMD_SYNC,
					 sizeof(cmd), &cmd))
			IWL_ERR(mvm, "Failed to send BT_CI cmd");
		memcpy(&mvm->last_bt_ci_cmd, &cmd, sizeof(cmd));
	}

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
	IWL_DEBUG_COEX(mvm, "\tBT status: %s\n",
		       notif->bt_status ? "ON" : "OFF");
	IWL_DEBUG_COEX(mvm, "\tBT open conn %d\n", notif->bt_open_conn);
	IWL_DEBUG_COEX(mvm, "\tBT ci compliance %d\n", notif->bt_ci_compliance);
	IWL_DEBUG_COEX(mvm, "\tBT primary_ch_lut %d\n",
		       le32_to_cpu(notif->primary_ch_lut));
	IWL_DEBUG_COEX(mvm, "\tBT secondary_ch_lut %d\n",
		       le32_to_cpu(notif->secondary_ch_lut));
	IWL_DEBUG_COEX(mvm, "\tBT activity grading %d\n",
		       le32_to_cpu(notif->bt_activity_grading));
	IWL_DEBUG_COEX(mvm, "\tBT agg traffic load %d\n",
		       notif->bt_agg_traffic_load);

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

	lockdep_assert_held(&mvm->mutex);

	/* Rssi update while not associated ?! */
	if (WARN_ON_ONCE(mvmvif->ap_sta_id == IWL_MVM_STATION_COUNT))
		return;

	/* No open connection - reports should be disabled */
	if (!BT_MBOX_MSG(&mvm->last_bt_notif, 3, OPEN_CON_2))
		return;

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
}

void iwl_mvm_bt_coex_vif_change(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	if (!(mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_NEWBT_COEX))
		return;

	iwl_mvm_bt_coex_notif_handle(mvm);
}
