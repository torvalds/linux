/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2013 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2014 Intel Mobile Communications GmbH
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
 * Copyright(c) 2013 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2014 Intel Mobile Communications GmbH
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

/* For counting bound interfaces */
struct iwl_mvm_active_iface_iterator_data {
	struct ieee80211_vif *ignore_vif;
	u8 sta_vif_ap_sta_id;
	enum iwl_sf_state sta_vif_state;
	int num_active_macs;
};

/*
 * Count bound interfaces which are not p2p, besides data->ignore_vif.
 * data->station_vif will point to one bound vif of type station, if exists.
 */
static void iwl_mvm_bound_iface_iterator(void *_data, u8 *mac,
					 struct ieee80211_vif *vif)
{
	struct iwl_mvm_active_iface_iterator_data *data = _data;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	if (vif == data->ignore_vif || !mvmvif->phy_ctxt ||
	    vif->type == NL80211_IFTYPE_P2P_DEVICE)
		return;

	data->num_active_macs++;

	if (vif->type == NL80211_IFTYPE_STATION) {
		data->sta_vif_ap_sta_id = mvmvif->ap_sta_id;
		if (vif->bss_conf.assoc)
			data->sta_vif_state = SF_FULL_ON;
		else
			data->sta_vif_state = SF_INIT_OFF;
	}
}

/*
 * Aging and idle timeouts for the different possible scenarios
 * in SF_FULL_ON state.
 */
static const __le32 sf_full_timeout[SF_NUM_SCENARIO][SF_NUM_TIMEOUT_TYPES] = {
	{
		cpu_to_le32(SF_SINGLE_UNICAST_AGING_TIMER),
		cpu_to_le32(SF_SINGLE_UNICAST_IDLE_TIMER)
	},
	{
		cpu_to_le32(SF_AGG_UNICAST_AGING_TIMER),
		cpu_to_le32(SF_AGG_UNICAST_IDLE_TIMER)
	},
	{
		cpu_to_le32(SF_MCAST_AGING_TIMER),
		cpu_to_le32(SF_MCAST_IDLE_TIMER)
	},
	{
		cpu_to_le32(SF_BA_AGING_TIMER),
		cpu_to_le32(SF_BA_IDLE_TIMER)
	},
	{
		cpu_to_le32(SF_TX_RE_AGING_TIMER),
		cpu_to_le32(SF_TX_RE_IDLE_TIMER)
	},
};

static void iwl_mvm_fill_sf_command(struct iwl_sf_cfg_cmd *sf_cmd,
				    struct ieee80211_sta *sta)
{
	int i, j, watermark;

	sf_cmd->watermark[SF_LONG_DELAY_ON] = cpu_to_le32(SF_W_MARK_SCAN);

	/*
	 * If we are in association flow - check antenna configuration
	 * capabilities of the AP station, and choose the watermark accordingly.
	 */
	if (sta) {
		if (sta->ht_cap.ht_supported || sta->vht_cap.vht_supported) {
			switch (sta->rx_nss) {
			case 1:
				watermark = SF_W_MARK_SISO;
				break;
			case 2:
				watermark = SF_W_MARK_MIMO2;
				break;
			default:
				watermark = SF_W_MARK_MIMO3;
				break;
			}
		} else {
			watermark = SF_W_MARK_LEGACY;
		}
	/* default watermark value for unassociated mode. */
	} else {
		watermark = SF_W_MARK_MIMO2;
	}
	sf_cmd->watermark[SF_FULL_ON] = cpu_to_le32(watermark);

	for (i = 0; i < SF_NUM_SCENARIO; i++) {
		for (j = 0; j < SF_NUM_TIMEOUT_TYPES; j++) {
			sf_cmd->long_delay_timeouts[i][j] =
					cpu_to_le32(SF_LONG_DELAY_AGING_TIMER);
		}
	}
	BUILD_BUG_ON(sizeof(sf_full_timeout) !=
		     sizeof(__le32) * SF_NUM_SCENARIO * SF_NUM_TIMEOUT_TYPES);

	memcpy(sf_cmd->full_on_timeouts, sf_full_timeout,
	       sizeof(sf_full_timeout));
}

static int iwl_mvm_sf_config(struct iwl_mvm *mvm, u8 sta_id,
			     enum iwl_sf_state new_state)
{
	struct iwl_sf_cfg_cmd sf_cmd = {
		.state = cpu_to_le32(new_state),
	};
	struct ieee80211_sta *sta;
	int ret = 0;

	/*
	 * If an associated AP sta changed its antenna configuration, the state
	 * will remain FULL_ON but SF parameters need to be reconsidered.
	 */
	if (new_state != SF_FULL_ON && mvm->sf_state == new_state)
		return 0;

	switch (new_state) {
	case SF_UNINIT:
		break;
	case SF_FULL_ON:
		if (sta_id == IWL_MVM_STATION_COUNT) {
			IWL_ERR(mvm,
				"No station: Cannot switch SF to FULL_ON\n");
			return -EINVAL;
		}
		rcu_read_lock();
		sta = rcu_dereference(mvm->fw_id_to_mac_id[sta_id]);
		if (IS_ERR_OR_NULL(sta)) {
			IWL_ERR(mvm, "Invalid station id\n");
			rcu_read_unlock();
			return -EINVAL;
		}
		iwl_mvm_fill_sf_command(&sf_cmd, sta);
		rcu_read_unlock();
		break;
	case SF_INIT_OFF:
		iwl_mvm_fill_sf_command(&sf_cmd, NULL);
		break;
	default:
		WARN_ONCE(1, "Invalid state: %d. not sending Smart Fifo cmd\n",
			  new_state);
		return -EINVAL;
	}

	ret = iwl_mvm_send_cmd_pdu(mvm, REPLY_SF_CFG_CMD, CMD_ASYNC,
				   sizeof(sf_cmd), &sf_cmd);
	if (!ret)
		mvm->sf_state = new_state;

	return ret;
}

/*
 * Update Smart fifo:
 * Count bound interfaces that are not to be removed, ignoring p2p devices,
 * and set new state accordingly.
 */
int iwl_mvm_sf_update(struct iwl_mvm *mvm, struct ieee80211_vif *changed_vif,
		      bool remove_vif)
{
	enum iwl_sf_state new_state;
	u8 sta_id = IWL_MVM_STATION_COUNT;
	struct iwl_mvm_vif *mvmvif = NULL;
	struct iwl_mvm_active_iface_iterator_data data = {
		.ignore_vif = changed_vif,
		.sta_vif_state = SF_UNINIT,
		.sta_vif_ap_sta_id = IWL_MVM_STATION_COUNT,
	};

	/*
	 * Ignore the call if we are in HW Restart flow, or if the handled
	 * vif is a p2p device.
	 */
	if (test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status) ||
	    (changed_vif && changed_vif->type == NL80211_IFTYPE_P2P_DEVICE))
		return 0;

	ieee80211_iterate_active_interfaces_atomic(mvm->hw,
						   IEEE80211_IFACE_ITER_NORMAL,
						   iwl_mvm_bound_iface_iterator,
						   &data);

	/* If changed_vif exists and is not to be removed, add to the count */
	if (changed_vif && !remove_vif)
		data.num_active_macs++;

	switch (data.num_active_macs) {
	case 0:
		/* If there are no active macs - change state to SF_INIT_OFF */
		new_state = SF_INIT_OFF;
		break;
	case 1:
		if (remove_vif) {
			/* The one active mac left is of type station
			 * and we filled the relevant data during iteration
			 */
			new_state = data.sta_vif_state;
			sta_id = data.sta_vif_ap_sta_id;
		} else {
			if (WARN_ON(!changed_vif))
				return -EINVAL;
			if (changed_vif->type != NL80211_IFTYPE_STATION) {
				new_state = SF_UNINIT;
			} else if (changed_vif->bss_conf.assoc &&
				   changed_vif->bss_conf.dtim_period) {
				mvmvif = iwl_mvm_vif_from_mac80211(changed_vif);
				sta_id = mvmvif->ap_sta_id;
				new_state = SF_FULL_ON;
			} else {
				new_state = SF_INIT_OFF;
			}
		}
		break;
	default:
		/* If there are multiple active macs - change to SF_UNINIT */
		new_state = SF_UNINIT;
	}
	return iwl_mvm_sf_config(mvm, sta_id, new_state);
}
