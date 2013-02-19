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
	0xffffffff,
	0xfffffc00,
	0,
};

static const u32 iwl_bt_cts_kill_msk[BT_KILL_MSK_MAX] = {
	0xffffffff,
	0xfffffc00,
	0,
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

/* BT Antenna Coupling Threshold (dB) */
#define IWL_BT_ANTENNA_COUPLING_THRESHOLD	(35)

int iwl_send_bt_init_conf(struct iwl_mvm *mvm)
{
	struct iwl_bt_coex_cmd cmd = {
		.max_kill = 5,
		.bt3_time_t7_value = 1,
		.bt3_prio_sample_time = 2,
		.bt3_timer_t2_value = 0xc,
	};
	int ret;

	cmd.flags = iwlwifi_mod_params.bt_coex_active ?
			BT_COEX_NW : BT_COEX_DISABLE;
	cmd.flags |= iwlwifi_mod_params.bt_ch_announce ?
			BT_CH_PRIMARY_EN | BT_CH_SECONDARY_EN : 0;
	cmd.flags |= BT_SYNC_2_BT_DISABLE;

	cmd.valid_bit_msk = cpu_to_le16(BT_VALID_ENABLE |
					BT_VALID_BT_PRIO_BOOST |
					BT_VALID_MAX_KILL |
					BT_VALID_3W_TMRS |
					BT_VALID_KILL_ACK |
					BT_VALID_KILL_CTS |
					BT_VALID_REDUCED_TX_POWER |
					BT_VALID_LUT);

	if (iwlwifi_mod_params.ant_coupling > IWL_BT_ANTENNA_COUPLING_THRESHOLD)
		memcpy(&cmd.decision_lut, iwl_loose_lookup,
		       sizeof(iwl_tight_lookup));
	else
		memcpy(&cmd.decision_lut, iwl_tight_lookup,
		       sizeof(iwl_tight_lookup));

	cmd.bt_prio_boost = cpu_to_le32(IWL_BT_DEFAULT_BOOST);
	cmd.kill_ack_msk =
		cpu_to_le32(iwl_bt_ack_kill_msk[BT_KILL_MSK_DEFAULT]);
	cmd.kill_cts_msk =
		cpu_to_le32(iwl_bt_cts_kill_msk[BT_KILL_MSK_DEFAULT]);

	/* go to CALIB state in internal BT-Coex state machine */
	ret = iwl_send_bt_env(mvm, BT_COEX_ENV_OPEN,
			      BT_COEX_PRIO_TBL_EVT_INIT_CALIB2);
	if (ret)
		return ret;

	ret  = iwl_send_bt_env(mvm, BT_COEX_ENV_CLOSE,
			       BT_COEX_PRIO_TBL_EVT_INIT_CALIB2);
	if (ret)
		return ret;

	return iwl_mvm_send_cmd_pdu(mvm, BT_CONFIG, CMD_SYNC,
				    sizeof(cmd), &cmd);
}

struct iwl_bt_notif_iterator_data {
	struct iwl_mvm *mvm;
	struct iwl_bt_coex_profile_notif *notif;
};

static void iwl_mvm_bt_notif_iterator(void *_data, u8 *mac,
				      struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_bt_notif_iterator_data *data = _data;
	struct ieee80211_chanctx_conf *chanctx_conf;
	enum ieee80211_smps_mode smps_mode;
	enum ieee80211_band band;

	if (vif->type != NL80211_IFTYPE_STATION)
		return;

	rcu_read_lock();
	chanctx_conf = rcu_dereference(vif->chanctx_conf);
	if (chanctx_conf && chanctx_conf->def.chan)
		band = chanctx_conf->def.chan->band;
	else
		band = -1;
	rcu_read_unlock();

	if (band != IEEE80211_BAND_2GHZ)
		return;

	smps_mode = IEEE80211_SMPS_AUTOMATIC;

	if (data->notif->bt_status)
		smps_mode = IEEE80211_SMPS_DYNAMIC;

	if (data->notif->bt_traffic_load)
		smps_mode = IEEE80211_SMPS_STATIC;

	IWL_DEBUG_COEX(data->mvm,
		       "mac %d: bt_status %d traffic_load %d smps_req %d\n",
		       mvmvif->id,  data->notif->bt_status,
		       data->notif->bt_traffic_load, smps_mode);

	ieee80211_request_smps(vif, smps_mode);
}

int iwl_mvm_rx_bt_coex_notif(struct iwl_mvm *mvm,
			     struct iwl_rx_cmd_buffer *rxb,
			     struct iwl_device_cmd *dev_cmd)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_bt_coex_profile_notif *notif = (void *)pkt->data;
	struct iwl_bt_notif_iterator_data data = {
		.mvm = mvm,
		.notif = notif,
	};
	struct iwl_bt_coex_cmd cmd = {};
	enum iwl_bt_kill_msk bt_kill_msk;

	IWL_DEBUG_COEX(mvm, "BT Coex Notification received\n");
	IWL_DEBUG_COEX(mvm, "\tBT %salive\n", notif->bt_status ? "" : "not ");
	IWL_DEBUG_COEX(mvm, "\tBT open conn %d\n", notif->bt_open_conn);
	IWL_DEBUG_COEX(mvm, "\tBT traffic load %d\n", notif->bt_traffic_load);
	IWL_DEBUG_COEX(mvm, "\tBT agg traffic load %d\n",
		       notif->bt_agg_traffic_load);
	IWL_DEBUG_COEX(mvm, "\tBT ci compliance %d\n", notif->bt_ci_compliance);

	/* remember this notification for future use: rssi fluctuations */
	memcpy(&mvm->last_bt_notif, notif, sizeof(mvm->last_bt_notif));

	ieee80211_iterate_active_interfaces_atomic(
					mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
					iwl_mvm_bt_notif_iterator, &data);

	/* Low latency BT profile is active: give higher prio to BT */
	if (BT_MBOX_MSG(notif, 3, SCO_STATE)  ||
	    BT_MBOX_MSG(notif, 3, A2DP_STATE) ||
	    BT_MBOX_MSG(notif, 3, SNIFF_STATE))
		bt_kill_msk = BT_KILL_MSK_SCO_HID_A2DP;
	else
		bt_kill_msk = BT_KILL_MSK_DEFAULT;

	/* Don't send HCMD if there is no update */
	if (bt_kill_msk == mvm->bt_kill_msk)
		return 0;

	IWL_DEBUG_COEX(mvm,
		       "Udpate kill_msk: %d\n\t SCO %sactive A2DP %sactive SNIFF %sactive\n",
		       bt_kill_msk,
		       BT_MBOX_MSG(notif, 3, SCO_STATE) ? "" : "in",
		       BT_MBOX_MSG(notif, 3, A2DP_STATE) ? "" : "in",
		       BT_MBOX_MSG(notif, 3, SNIFF_STATE) ? "" : "in");

	mvm->bt_kill_msk = bt_kill_msk;
	cmd.kill_ack_msk = cpu_to_le32(iwl_bt_ack_kill_msk[bt_kill_msk]);
	cmd.kill_cts_msk = cpu_to_le32(iwl_bt_cts_kill_msk[bt_kill_msk]);

	cmd.valid_bit_msk = cpu_to_le16(BT_VALID_KILL_ACK | BT_VALID_KILL_CTS);

	if (iwl_mvm_send_cmd_pdu(mvm, BT_CONFIG, CMD_SYNC, sizeof(cmd), &cmd))
		IWL_ERR(mvm, "Failed to sent BT Coex CMD\n");

	/* This handler is ASYNC */
	return 0;
}
