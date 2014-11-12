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

void iwl_mvm_recalc_tdls_state(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			       bool sta_added)
{
	int tdls_sta_cnt = iwl_mvm_tdls_sta_count(mvm, vif);

	/*
	 * Disable ps when the first TDLS sta is added and re-enable it
	 * when the last TDLS sta is removed
	 */
	if ((tdls_sta_cnt == 1 && sta_added) ||
	    (tdls_sta_cnt == 0 && !sta_added))
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
