/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2014 Intel Mobile Communications GmbH
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
 * Copyright(c) 2014 Intel Mobile Communications GmbH
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

#include "mvm.h"
#include "time-event.h"

void iwl_mvm_teardown_tdls_peers(struct iwl_mvm *mvm)
{
	struct ieee80211_sta *sta;
	struct iwl_mvm_sta *mvmsta;
	int i;

	lockdep_assert_held(&mvm->mutex);

	for (i = 0; i < IWL_MVM_STATION_COUNT; i++) {
		sta = rcu_dereference_protected(mvm->fw_id_to_mac_id[i],
						lockdep_is_held(&mvm->mutex));
		if (!sta || IS_ERR(sta) || !sta->tdls)
			continue;

		mvmsta = iwl_mvm_sta_from_mac80211(sta);
		ieee80211_tdls_oper_request(mvmsta->vif, sta->addr,
				NL80211_TDLS_TEARDOWN,
				WLAN_REASON_TDLS_TEARDOWN_UNSPECIFIED,
				GFP_KERNEL);
	}
}

int iwl_mvm_tdls_sta_count(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct ieee80211_sta *sta;
	struct iwl_mvm_sta *mvmsta;
	int count = 0;
	int i;

	lockdep_assert_held(&mvm->mutex);

	for (i = 0; i < IWL_MVM_STATION_COUNT; i++) {
		sta = rcu_dereference_protected(mvm->fw_id_to_mac_id[i],
						lockdep_is_held(&mvm->mutex));
		if (!sta || IS_ERR(sta) || !sta->tdls)
			continue;

		if (vif) {
			mvmsta = iwl_mvm_sta_from_mac80211(sta);
			if (mvmsta->vif != vif)
				continue;
		}

		count++;
	}

	return count;
}

static void iwl_mvm_tdls_config(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct iwl_rx_packet *pkt;
	struct iwl_tdls_config_res *resp;
	struct iwl_tdls_config_cmd tdls_cfg_cmd = {};
	struct iwl_host_cmd cmd = {
		.id = TDLS_CONFIG_CMD,
		.flags = CMD_WANT_SKB,
		.data = { &tdls_cfg_cmd, },
		.len = { sizeof(struct iwl_tdls_config_cmd), },
	};
	struct ieee80211_sta *sta;
	int ret, i, cnt;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	lockdep_assert_held(&mvm->mutex);

	tdls_cfg_cmd.id_and_color =
		cpu_to_le32(FW_CMD_ID_AND_COLOR(mvmvif->id, mvmvif->color));
	tdls_cfg_cmd.tx_to_ap_tid = IWL_MVM_TDLS_FW_TID;
	tdls_cfg_cmd.tx_to_ap_ssn = cpu_to_le16(0); /* not used for now */

	/* for now the Tx cmd is empty and unused */

	/* populate TDLS peer data */
	cnt = 0;
	for (i = 0; i < IWL_MVM_STATION_COUNT; i++) {
		sta = rcu_dereference_protected(mvm->fw_id_to_mac_id[i],
						lockdep_is_held(&mvm->mutex));
		if (IS_ERR_OR_NULL(sta) || !sta->tdls)
			continue;

		tdls_cfg_cmd.sta_info[cnt].sta_id = i;
		tdls_cfg_cmd.sta_info[cnt].tx_to_peer_tid =
							IWL_MVM_TDLS_FW_TID;
		tdls_cfg_cmd.sta_info[cnt].tx_to_peer_ssn = cpu_to_le16(0);
		tdls_cfg_cmd.sta_info[cnt].is_initiator =
				cpu_to_le32(sta->tdls_initiator ? 1 : 0);

		cnt++;
	}

	tdls_cfg_cmd.tdls_peer_count = cnt;
	IWL_DEBUG_TDLS(mvm, "send TDLS config to FW for %d peers\n", cnt);

	ret = iwl_mvm_send_cmd(mvm, &cmd);
	if (WARN_ON_ONCE(ret))
		return;

	pkt = cmd.resp_pkt;
	if (pkt->hdr.flags & IWL_CMD_FAILED_MSK) {
		IWL_ERR(mvm, "Bad return from TDLS_CONFIG_COMMAND (0x%08X)\n",
			pkt->hdr.flags);
		goto exit;
	}

	if (WARN_ON_ONCE(iwl_rx_packet_payload_len(pkt) != sizeof(*resp)))
		goto exit;

	/* we don't really care about the response at this point */

exit:
	iwl_free_resp(&cmd);
}

void iwl_mvm_recalc_tdls_state(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			       bool sta_added)
{
	int tdls_sta_cnt = iwl_mvm_tdls_sta_count(mvm, vif);

	/* when the first peer joins, send a power update first */
	if (tdls_sta_cnt == 1 && sta_added)
		iwl_mvm_power_update_mac(mvm);

	/* configure the FW with TDLS peer info */
	iwl_mvm_tdls_config(mvm, vif);

	/* when the last peer leaves, send a power update last */
	if (tdls_sta_cnt == 0 && !sta_added)
		iwl_mvm_power_update_mac(mvm);
}

void iwl_mvm_mac_mgd_protect_tdls_discover(struct ieee80211_hw *hw,
					   struct ieee80211_vif *vif)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	u32 duration = 2 * vif->bss_conf.dtim_period * vif->bss_conf.beacon_int;

	/*
	 * iwl_mvm_protect_session() reads directly from the device
	 * (the system time), so make sure it is available.
	 */
	if (iwl_mvm_ref_sync(mvm, IWL_MVM_REF_PROTECT_TDLS))
		return;

	mutex_lock(&mvm->mutex);
	/* Protect the session to hear the TDLS setup response on the channel */
	iwl_mvm_protect_session(mvm, vif, duration, duration, 100, true);
	mutex_unlock(&mvm->mutex);

	iwl_mvm_unref(mvm, IWL_MVM_REF_PROTECT_TDLS);
}
