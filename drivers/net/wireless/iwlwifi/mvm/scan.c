/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
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
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
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

#include <linux/etherdevice.h>
#include <net/mac80211.h>

#include "mvm.h"
#include "iwl-eeprom-parse.h"
#include "fw-api-scan.h"

#define IWL_PLCP_QUIET_THRESH 1
#define IWL_ACTIVE_QUIET_TIME 10
#define IWL_DENSE_EBS_SCAN_RATIO 5
#define IWL_SPARSE_EBS_SCAN_RATIO 1

struct iwl_mvm_scan_params {
	u32 max_out_time;
	u32 suspend_time;
	bool passive_fragmented;
	struct _dwell {
		u16 passive;
		u16 active;
		u16 fragmented;
	} dwell[IEEE80211_NUM_BANDS];
};

enum iwl_umac_scan_uid_type {
	IWL_UMAC_SCAN_UID_REG_SCAN	= BIT(0),
	IWL_UMAC_SCAN_UID_SCHED_SCAN	= BIT(1),
	IWL_UMAC_SCAN_UID_ALL		= IWL_UMAC_SCAN_UID_REG_SCAN |
					  IWL_UMAC_SCAN_UID_SCHED_SCAN,
};

static int iwl_umac_scan_stop(struct iwl_mvm *mvm,
			      enum iwl_umac_scan_uid_type type, bool notify);

static u8 iwl_mvm_scan_rx_ant(struct iwl_mvm *mvm)
{
	if (mvm->scan_rx_ant != ANT_NONE)
		return mvm->scan_rx_ant;
	return iwl_mvm_get_valid_rx_ant(mvm);
}

static inline __le16 iwl_mvm_scan_rx_chain(struct iwl_mvm *mvm)
{
	u16 rx_chain;
	u8 rx_ant;

	rx_ant = iwl_mvm_scan_rx_ant(mvm);
	rx_chain = rx_ant << PHY_RX_CHAIN_VALID_POS;
	rx_chain |= rx_ant << PHY_RX_CHAIN_FORCE_MIMO_SEL_POS;
	rx_chain |= rx_ant << PHY_RX_CHAIN_FORCE_SEL_POS;
	rx_chain |= 0x1 << PHY_RX_CHAIN_DRIVER_FORCE_POS;
	return cpu_to_le16(rx_chain);
}

static __le32 iwl_mvm_scan_rxon_flags(enum ieee80211_band band)
{
	if (band == IEEE80211_BAND_2GHZ)
		return cpu_to_le32(PHY_BAND_24);
	else
		return cpu_to_le32(PHY_BAND_5);
}

static inline __le32
iwl_mvm_scan_rate_n_flags(struct iwl_mvm *mvm, enum ieee80211_band band,
			  bool no_cck)
{
	u32 tx_ant;

	mvm->scan_last_antenna_idx =
		iwl_mvm_next_antenna(mvm, iwl_mvm_get_valid_tx_ant(mvm),
				     mvm->scan_last_antenna_idx);
	tx_ant = BIT(mvm->scan_last_antenna_idx) << RATE_MCS_ANT_POS;

	if (band == IEEE80211_BAND_2GHZ && !no_cck)
		return cpu_to_le32(IWL_RATE_1M_PLCP | RATE_MCS_CCK_MSK |
				   tx_ant);
	else
		return cpu_to_le32(IWL_RATE_6M_PLCP | tx_ant);
}

/*
 * We insert the SSIDs in an inverted order, because the FW will
 * invert it back. The most prioritized SSID, which is first in the
 * request list, is not copied here, but inserted directly to the probe
 * request.
 */
static void iwl_mvm_scan_fill_ssids(struct iwl_ssid_ie *cmd_ssid,
				    struct cfg80211_ssid *ssids,
				    int n_ssids, int first)
{
	int fw_idx, req_idx;

	for (req_idx = n_ssids - 1, fw_idx = 0; req_idx >= first;
	     req_idx--, fw_idx++) {
		cmd_ssid[fw_idx].id = WLAN_EID_SSID;
		cmd_ssid[fw_idx].len = ssids[req_idx].ssid_len;
		memcpy(cmd_ssid[fw_idx].ssid,
		       ssids[req_idx].ssid,
		       ssids[req_idx].ssid_len);
	}
}

/*
 * If req->n_ssids > 0, it means we should do an active scan.
 * In case of active scan w/o directed scan, we receive a zero-length SSID
 * just to notify that this scan is active and not passive.
 * In order to notify the FW of the number of SSIDs we wish to scan (including
 * the zero-length one), we need to set the corresponding bits in chan->type,
 * one for each SSID, and set the active bit (first). If the first SSID is
 * already included in the probe template, so we need to set only
 * req->n_ssids - 1 bits in addition to the first bit.
 */
static u16 iwl_mvm_get_active_dwell(struct iwl_mvm *mvm,
				    enum ieee80211_band band, int n_ssids)
{
	if (mvm->fw->ucode_capa.api[0] & IWL_UCODE_TLV_API_BASIC_DWELL)
		return 10;
	if (band == IEEE80211_BAND_2GHZ)
		return 20  + 3 * (n_ssids + 1);
	return 10  + 2 * (n_ssids + 1);
}

static u16 iwl_mvm_get_passive_dwell(struct iwl_mvm *mvm,
				     enum ieee80211_band band)
{
	if (mvm->fw->ucode_capa.api[0] & IWL_UCODE_TLV_API_BASIC_DWELL)
			return 110;
	return band == IEEE80211_BAND_2GHZ ? 100 + 20 : 100 + 10;
}

static void iwl_mvm_scan_condition_iterator(void *data, u8 *mac,
					    struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int *global_cnt = data;

	if (vif->type != NL80211_IFTYPE_P2P_DEVICE && mvmvif->phy_ctxt &&
	    mvmvif->phy_ctxt->id < MAX_PHYS)
		*global_cnt += 1;
}

static void iwl_mvm_scan_calc_params(struct iwl_mvm *mvm,
				     struct ieee80211_vif *vif,
				     int n_ssids, u32 flags,
				     struct iwl_mvm_scan_params *params)
{
	int global_cnt = 0;
	enum ieee80211_band band;
	u8 frag_passive_dwell = 0;

	ieee80211_iterate_active_interfaces_atomic(mvm->hw,
					    IEEE80211_IFACE_ITER_NORMAL,
					    iwl_mvm_scan_condition_iterator,
					    &global_cnt);

	if (!global_cnt)
		goto not_bound;

	params->suspend_time = 30;
	params->max_out_time = 120;

	if (iwl_mvm_low_latency(mvm)) {
		if (mvm->fw->ucode_capa.api[0] &
		    IWL_UCODE_TLV_API_FRAGMENTED_SCAN) {
			params->suspend_time = 105;
			/*
			 * If there is more than one active interface make
			 * passive scan more fragmented.
			 */
			frag_passive_dwell = 40;
			params->max_out_time = frag_passive_dwell;
		} else {
			params->suspend_time = 120;
			params->max_out_time = 120;
		}
	}

	if (frag_passive_dwell && (mvm->fw->ucode_capa.api[0] &
				   IWL_UCODE_TLV_API_FRAGMENTED_SCAN)) {
		/*
		 * P2P device scan should not be fragmented to avoid negative
		 * impact on P2P device discovery. Configure max_out_time to be
		 * equal to dwell time on passive channel. Take a longest
		 * possible value, one that corresponds to 2GHz band
		 */
		if (vif->type == NL80211_IFTYPE_P2P_DEVICE) {
			u32 passive_dwell =
				iwl_mvm_get_passive_dwell(mvm,
							  IEEE80211_BAND_2GHZ);
			params->max_out_time = passive_dwell;
		} else {
			params->passive_fragmented = true;
		}
	}

	if (flags & NL80211_SCAN_FLAG_LOW_PRIORITY)
		params->max_out_time = 200;

not_bound:

	for (band = IEEE80211_BAND_2GHZ; band < IEEE80211_NUM_BANDS; band++) {
		if (params->passive_fragmented)
			params->dwell[band].fragmented = frag_passive_dwell;

		params->dwell[band].passive = iwl_mvm_get_passive_dwell(mvm,
									band);
		params->dwell[band].active = iwl_mvm_get_active_dwell(mvm, band,
								      n_ssids);
	}
}

static inline bool iwl_mvm_rrm_scan_needed(struct iwl_mvm *mvm)
{
	/* require rrm scan whenever the fw supports it */
	return mvm->fw->ucode_capa.capa[0] &
	       IWL_UCODE_TLV_CAPA_DS_PARAM_SET_IE_SUPPORT;
}

static int iwl_mvm_max_scan_ie_fw_cmd_room(struct iwl_mvm *mvm,
					   bool is_sched_scan)
{
	int max_probe_len;

	max_probe_len = SCAN_OFFLOAD_PROBE_REQ_SIZE;

	/* we create the 802.11 header and SSID element */
	max_probe_len -= 24 + 2;

	/* DS parameter set element is added on 2.4GHZ band if required */
	if (iwl_mvm_rrm_scan_needed(mvm))
		max_probe_len -= 3;

	return max_probe_len;
}

int iwl_mvm_max_scan_ie_len(struct iwl_mvm *mvm, bool is_sched_scan)
{
	int max_ie_len = iwl_mvm_max_scan_ie_fw_cmd_room(mvm, is_sched_scan);

	/* TODO: [BUG] This function should return the maximum allowed size of
	 * scan IEs, however the LMAC scan api contains both 2GHZ and 5GHZ IEs
	 * in the same command. So the correct implementation of this function
	 * is just iwl_mvm_max_scan_ie_fw_cmd_room() / 2. Currently the scan
	 * command has only 512 bytes and it would leave us with about 240
	 * bytes for scan IEs, which is clearly not enough. So meanwhile
	 * we will report an incorrect value. This may result in a failure to
	 * issue a scan in unified_scan_lmac and unified_sched_scan_lmac
	 * functions with -ENOBUFS, if a large enough probe will be provided.
	 */
	return max_ie_len;
}

int iwl_mvm_rx_scan_offload_iter_complete_notif(struct iwl_mvm *mvm,
						struct iwl_rx_cmd_buffer *rxb,
						struct iwl_device_cmd *cmd)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_scan_complete_notif *notif = (void *)pkt->data;

	IWL_DEBUG_SCAN(mvm,
		       "Scan offload iteration complete: status=0x%x scanned channels=%d\n",
		       notif->status, notif->scanned_channels);
	return 0;
}

int iwl_mvm_rx_scan_offload_results(struct iwl_mvm *mvm,
				    struct iwl_rx_cmd_buffer *rxb,
				    struct iwl_device_cmd *cmd)
{
	IWL_DEBUG_SCAN(mvm, "Scheduled scan results\n");
	ieee80211_sched_scan_results(mvm->hw);

	return 0;
}

int iwl_mvm_rx_scan_offload_complete_notif(struct iwl_mvm *mvm,
					   struct iwl_rx_cmd_buffer *rxb,
					   struct iwl_device_cmd *cmd)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_periodic_scan_complete *scan_notif;

	scan_notif = (void *)pkt->data;

	/* scan status must be locked for proper checking */
	lockdep_assert_held(&mvm->mutex);

	IWL_DEBUG_SCAN(mvm,
		       "%s completed, status %s, EBS status %s\n",
		       mvm->scan_status == IWL_MVM_SCAN_SCHED ?
				"Scheduled scan" : "Scan",
		       scan_notif->status == IWL_SCAN_OFFLOAD_COMPLETED ?
				"completed" : "aborted",
		       scan_notif->ebs_status == IWL_SCAN_EBS_SUCCESS ?
				"success" : "failed");


	/* only call mac80211 completion if the stop was initiated by FW */
	if (mvm->scan_status == IWL_MVM_SCAN_SCHED) {
		mvm->scan_status = IWL_MVM_SCAN_NONE;
		ieee80211_sched_scan_stopped(mvm->hw);
	} else if (mvm->scan_status == IWL_MVM_SCAN_OS) {
		mvm->scan_status = IWL_MVM_SCAN_NONE;
		ieee80211_scan_completed(mvm->hw,
				scan_notif->status == IWL_SCAN_OFFLOAD_ABORTED);
		iwl_mvm_unref(mvm, IWL_MVM_REF_SCAN);
	}

	if (scan_notif->ebs_status)
		mvm->last_ebs_successful = false;

	return 0;
}

static int iwl_ssid_exist(u8 *ssid, u8 ssid_len, struct iwl_ssid_ie *ssid_list)
{
	int i;

	for (i = 0; i < PROBE_OPTION_MAX; i++) {
		if (!ssid_list[i].len)
			break;
		if (ssid_list[i].len == ssid_len &&
		    !memcmp(ssid_list->ssid, ssid, ssid_len))
			return i;
	}
	return -1;
}

static void iwl_scan_offload_build_ssid(struct cfg80211_sched_scan_request *req,
					struct iwl_ssid_ie *direct_scan,
					u32 *ssid_bitmap, bool basic_ssid)
{
	int i, j;
	int index;

	/*
	 * copy SSIDs from match list.
	 * iwl_config_sched_scan_profiles() uses the order of these ssids to
	 * config match list.
	 */
	for (i = 0; i < req->n_match_sets && i < PROBE_OPTION_MAX; i++) {
		/* skip empty SSID matchsets */
		if (!req->match_sets[i].ssid.ssid_len)
			continue;
		direct_scan[i].id = WLAN_EID_SSID;
		direct_scan[i].len = req->match_sets[i].ssid.ssid_len;
		memcpy(direct_scan[i].ssid, req->match_sets[i].ssid.ssid,
		       direct_scan[i].len);
	}

	/* add SSIDs from scan SSID list */
	*ssid_bitmap = 0;
	for (j = 0; j < req->n_ssids && i < PROBE_OPTION_MAX; j++) {
		index = iwl_ssid_exist(req->ssids[j].ssid,
				       req->ssids[j].ssid_len,
				       direct_scan);
		if (index < 0) {
			if (!req->ssids[j].ssid_len && basic_ssid)
				continue;
			direct_scan[i].id = WLAN_EID_SSID;
			direct_scan[i].len = req->ssids[j].ssid_len;
			memcpy(direct_scan[i].ssid, req->ssids[j].ssid,
			       direct_scan[i].len);
			*ssid_bitmap |= BIT(i + 1);
			i++;
		} else {
			*ssid_bitmap |= BIT(index + 1);
		}
	}
}

int iwl_mvm_config_sched_scan_profiles(struct iwl_mvm *mvm,
				       struct cfg80211_sched_scan_request *req)
{
	struct iwl_scan_offload_profile *profile;
	struct iwl_scan_offload_profile_cfg *profile_cfg;
	struct iwl_scan_offload_blacklist *blacklist;
	struct iwl_host_cmd cmd = {
		.id = SCAN_OFFLOAD_UPDATE_PROFILES_CMD,
		.len[1] = sizeof(*profile_cfg),
		.dataflags[0] = IWL_HCMD_DFL_NOCOPY,
		.dataflags[1] = IWL_HCMD_DFL_NOCOPY,
	};
	int blacklist_len;
	int i;
	int ret;

	if (WARN_ON(req->n_match_sets > IWL_SCAN_MAX_PROFILES))
			return -EIO;

	if (mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_SHORT_BL)
		blacklist_len = IWL_SCAN_SHORT_BLACKLIST_LEN;
	else
		blacklist_len = IWL_SCAN_MAX_BLACKLIST_LEN;

	blacklist = kzalloc(sizeof(*blacklist) * blacklist_len, GFP_KERNEL);
	if (!blacklist)
		return -ENOMEM;

	profile_cfg = kzalloc(sizeof(*profile_cfg), GFP_KERNEL);
	if (!profile_cfg) {
		ret = -ENOMEM;
		goto free_blacklist;
	}

	cmd.data[0] = blacklist;
	cmd.len[0] = sizeof(*blacklist) * blacklist_len;
	cmd.data[1] = profile_cfg;

	/* No blacklist configuration */

	profile_cfg->num_profiles = req->n_match_sets;
	profile_cfg->active_clients = SCAN_CLIENT_SCHED_SCAN;
	profile_cfg->pass_match = SCAN_CLIENT_SCHED_SCAN;
	profile_cfg->match_notify = SCAN_CLIENT_SCHED_SCAN;
	if (!req->n_match_sets || !req->match_sets[0].ssid.ssid_len)
		profile_cfg->any_beacon_notify = SCAN_CLIENT_SCHED_SCAN;

	for (i = 0; i < req->n_match_sets; i++) {
		profile = &profile_cfg->profiles[i];
		profile->ssid_index = i;
		/* Support any cipher and auth algorithm */
		profile->unicast_cipher = 0xff;
		profile->auth_alg = 0xff;
		profile->network_type = IWL_NETWORK_TYPE_ANY;
		profile->band_selection = IWL_SCAN_OFFLOAD_SELECT_ANY;
		profile->client_bitmap = SCAN_CLIENT_SCHED_SCAN;
	}

	IWL_DEBUG_SCAN(mvm, "Sending scheduled scan profile config\n");

	ret = iwl_mvm_send_cmd(mvm, &cmd);
	kfree(profile_cfg);
free_blacklist:
	kfree(blacklist);

	return ret;
}

static bool iwl_mvm_scan_pass_all(struct iwl_mvm *mvm,
				  struct cfg80211_sched_scan_request *req)
{
	if (req->n_match_sets && req->match_sets[0].ssid.ssid_len) {
		IWL_DEBUG_SCAN(mvm,
			       "Sending scheduled scan with filtering, n_match_sets %d\n",
			       req->n_match_sets);
		return false;
	}

	IWL_DEBUG_SCAN(mvm, "Sending Scheduled scan without filtering\n");
	return true;
}

int iwl_mvm_scan_offload_start(struct iwl_mvm *mvm,
			       struct ieee80211_vif *vif,
			       struct cfg80211_sched_scan_request *req,
			       struct ieee80211_scan_ies *ies)
{
	int ret;

	if (mvm->fw->ucode_capa.capa[0] & IWL_UCODE_TLV_CAPA_UMAC_SCAN) {
		ret = iwl_mvm_config_sched_scan_profiles(mvm, req);
		if (ret)
			return ret;
		ret = iwl_mvm_sched_scan_umac(mvm, vif, req, ies);
	} else {
		mvm->scan_status = IWL_MVM_SCAN_SCHED;
		ret = iwl_mvm_config_sched_scan_profiles(mvm, req);
		if (ret)
			return ret;
		ret = iwl_mvm_unified_sched_scan_lmac(mvm, vif, req, ies);
	}

	return ret;
}

static int iwl_mvm_send_scan_offload_abort(struct iwl_mvm *mvm)
{
	int ret;
	struct iwl_host_cmd cmd = {
		.id = SCAN_OFFLOAD_ABORT_CMD,
	};
	u32 status;

	/* Exit instantly with error when device is not ready
	 * to receive scan abort command or it does not perform
	 * scheduled scan currently */
	if (mvm->scan_status == IWL_MVM_SCAN_NONE)
		return -EIO;

	ret = iwl_mvm_send_cmd_status(mvm, &cmd, &status);
	if (ret)
		return ret;

	if (status != CAN_ABORT_STATUS) {
		/*
		 * The scan abort will return 1 for success or
		 * 2 for "failure".  A failure condition can be
		 * due to simply not being in an active scan which
		 * can occur if we send the scan abort before the
		 * microcode has notified us that a scan is completed.
		 */
		IWL_DEBUG_SCAN(mvm, "SCAN OFFLOAD ABORT ret %d.\n", status);
		ret = -ENOENT;
	}

	return ret;
}

int iwl_mvm_scan_offload_stop(struct iwl_mvm *mvm, bool notify)
{
	int ret;
	struct iwl_notification_wait wait_scan_done;
	static const u8 scan_done_notif[] = { SCAN_OFFLOAD_COMPLETE, };
	bool sched = mvm->scan_status == IWL_MVM_SCAN_SCHED;

	lockdep_assert_held(&mvm->mutex);

	if (mvm->fw->ucode_capa.capa[0] & IWL_UCODE_TLV_CAPA_UMAC_SCAN)
		return iwl_umac_scan_stop(mvm, IWL_UMAC_SCAN_UID_SCHED_SCAN,
					  notify);

	if (mvm->scan_status == IWL_MVM_SCAN_NONE)
		return 0;

	if (iwl_mvm_is_radio_killed(mvm)) {
		ret = 0;
		goto out;
	}

	iwl_init_notification_wait(&mvm->notif_wait, &wait_scan_done,
				   scan_done_notif,
				   ARRAY_SIZE(scan_done_notif),
				   NULL, NULL);

	ret = iwl_mvm_send_scan_offload_abort(mvm);
	if (ret) {
		IWL_DEBUG_SCAN(mvm, "Send stop %sscan failed %d\n",
			       sched ? "offloaded " : "", ret);
		iwl_remove_notification(&mvm->notif_wait, &wait_scan_done);
		goto out;
	}

	IWL_DEBUG_SCAN(mvm, "Successfully sent stop %sscan\n",
		       sched ? "offloaded " : "");

	ret = iwl_wait_notification(&mvm->notif_wait, &wait_scan_done, 1 * HZ);
out:
	/*
	 * Clear the scan status so the next scan requests will succeed. This
	 * also ensures the Rx handler doesn't do anything, as the scan was
	 * stopped from above. Since the rx handler won't do anything now,
	 * we have to release the scan reference here.
	 */
	if (mvm->scan_status == IWL_MVM_SCAN_OS)
		iwl_mvm_unref(mvm, IWL_MVM_REF_SCAN);

	mvm->scan_status = IWL_MVM_SCAN_NONE;

	if (notify) {
		if (sched)
			ieee80211_sched_scan_stopped(mvm->hw);
		else
			ieee80211_scan_completed(mvm->hw, true);
	}

	return ret;
}

static void iwl_mvm_unified_scan_fill_tx_cmd(struct iwl_mvm *mvm,
					     struct iwl_scan_req_tx_cmd *tx_cmd,
					     bool no_cck)
{
	tx_cmd[0].tx_flags = cpu_to_le32(TX_CMD_FLG_SEQ_CTL |
					 TX_CMD_FLG_BT_DIS);
	tx_cmd[0].rate_n_flags = iwl_mvm_scan_rate_n_flags(mvm,
							   IEEE80211_BAND_2GHZ,
							   no_cck);
	tx_cmd[0].sta_id = mvm->aux_sta.sta_id;

	tx_cmd[1].tx_flags = cpu_to_le32(TX_CMD_FLG_SEQ_CTL |
					 TX_CMD_FLG_BT_DIS);
	tx_cmd[1].rate_n_flags = iwl_mvm_scan_rate_n_flags(mvm,
							   IEEE80211_BAND_5GHZ,
							   no_cck);
	tx_cmd[1].sta_id = mvm->aux_sta.sta_id;
}

static void
iwl_mvm_lmac_scan_cfg_channels(struct iwl_mvm *mvm,
			       struct ieee80211_channel **channels,
			       int n_channels, u32 ssid_bitmap,
			       struct iwl_scan_req_unified_lmac *cmd)
{
	struct iwl_scan_channel_cfg_lmac *channel_cfg = (void *)&cmd->data;
	int i;

	for (i = 0; i < n_channels; i++) {
		channel_cfg[i].channel_num =
			cpu_to_le16(channels[i]->hw_value);
		channel_cfg[i].iter_count = cpu_to_le16(1);
		channel_cfg[i].iter_interval = 0;
		channel_cfg[i].flags =
			cpu_to_le32(IWL_UNIFIED_SCAN_CHANNEL_PARTIAL |
				    ssid_bitmap);
	}
}

static u8 *iwl_mvm_copy_and_insert_ds_elem(struct iwl_mvm *mvm, const u8 *ies,
					   size_t len, u8 *const pos)
{
	static const u8 before_ds_params[] = {
			WLAN_EID_SSID,
			WLAN_EID_SUPP_RATES,
			WLAN_EID_REQUEST,
			WLAN_EID_EXT_SUPP_RATES,
	};
	size_t offs;
	u8 *newpos = pos;

	if (!iwl_mvm_rrm_scan_needed(mvm)) {
		memcpy(newpos, ies, len);
		return newpos + len;
	}

	offs = ieee80211_ie_split(ies, len,
				  before_ds_params,
				  ARRAY_SIZE(before_ds_params),
				  0);

	memcpy(newpos, ies, offs);
	newpos += offs;

	/* Add a placeholder for DS Parameter Set element */
	*newpos++ = WLAN_EID_DS_PARAMS;
	*newpos++ = 1;
	*newpos++ = 0;

	memcpy(newpos, ies + offs, len - offs);
	newpos += len - offs;

	return newpos;
}

static void
iwl_mvm_build_unified_scan_probe(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
				 struct ieee80211_scan_ies *ies,
				 struct iwl_scan_probe_req *preq,
				 const u8 *mac_addr, const u8 *mac_addr_mask)
{
	struct ieee80211_mgmt *frame = (struct ieee80211_mgmt *)preq->buf;
	u8 *pos, *newpos;

	/*
	 * Unfortunately, right now the offload scan doesn't support randomising
	 * within the firmware, so until the firmware API is ready we implement
	 * it in the driver. This means that the scan iterations won't really be
	 * random, only when it's restarted, but at least that helps a bit.
	 */
	if (mac_addr)
		get_random_mask_addr(frame->sa, mac_addr, mac_addr_mask);
	else
		memcpy(frame->sa, vif->addr, ETH_ALEN);

	frame->frame_control = cpu_to_le16(IEEE80211_STYPE_PROBE_REQ);
	eth_broadcast_addr(frame->da);
	eth_broadcast_addr(frame->bssid);
	frame->seq_ctrl = 0;

	pos = frame->u.probe_req.variable;
	*pos++ = WLAN_EID_SSID;
	*pos++ = 0;

	preq->mac_header.offset = 0;
	preq->mac_header.len = cpu_to_le16(24 + 2);

	/* Insert ds parameter set element on 2.4 GHz band */
	newpos = iwl_mvm_copy_and_insert_ds_elem(mvm,
						 ies->ies[IEEE80211_BAND_2GHZ],
						 ies->len[IEEE80211_BAND_2GHZ],
						 pos);
	preq->band_data[0].offset = cpu_to_le16(pos - preq->buf);
	preq->band_data[0].len = cpu_to_le16(newpos - pos);
	pos = newpos;

	memcpy(pos, ies->ies[IEEE80211_BAND_5GHZ],
	       ies->len[IEEE80211_BAND_5GHZ]);
	preq->band_data[1].offset = cpu_to_le16(pos - preq->buf);
	preq->band_data[1].len = cpu_to_le16(ies->len[IEEE80211_BAND_5GHZ]);
	pos += ies->len[IEEE80211_BAND_5GHZ];

	memcpy(pos, ies->common_ies, ies->common_ie_len);
	preq->common_data.offset = cpu_to_le16(pos - preq->buf);
	preq->common_data.len = cpu_to_le16(ies->common_ie_len);
}

static void
iwl_mvm_build_generic_unified_scan_cmd(struct iwl_mvm *mvm,
				       struct iwl_scan_req_unified_lmac *cmd,
				       struct iwl_mvm_scan_params *params)
{
	memset(cmd, 0, ksize(cmd));
	cmd->active_dwell = params->dwell[IEEE80211_BAND_2GHZ].active;
	cmd->passive_dwell = params->dwell[IEEE80211_BAND_2GHZ].passive;
	if (params->passive_fragmented)
		cmd->fragmented_dwell =
				params->dwell[IEEE80211_BAND_2GHZ].fragmented;
	cmd->rx_chain_select = iwl_mvm_scan_rx_chain(mvm);
	cmd->max_out_time = cpu_to_le32(params->max_out_time);
	cmd->suspend_time = cpu_to_le32(params->suspend_time);
	cmd->scan_prio = cpu_to_le32(IWL_SCAN_PRIORITY_HIGH);
	cmd->iter_num = cpu_to_le32(1);

	if (iwl_mvm_rrm_scan_needed(mvm))
		cmd->scan_flags |=
			cpu_to_le32(IWL_MVM_LMAC_SCAN_FLAGS_RRM_ENABLED);
}

int iwl_mvm_unified_scan_lmac(struct iwl_mvm *mvm,
			      struct ieee80211_vif *vif,
			      struct ieee80211_scan_request *req)
{
	struct iwl_host_cmd hcmd = {
		.id = SCAN_OFFLOAD_REQUEST_CMD,
		.len = { sizeof(struct iwl_scan_req_unified_lmac) +
			 sizeof(struct iwl_scan_channel_cfg_lmac) *
				mvm->fw->ucode_capa.n_scan_channels +
			 sizeof(struct iwl_scan_probe_req), },
		.data = { mvm->scan_cmd, },
		.dataflags = { IWL_HCMD_DFL_NOCOPY, },
	};
	struct iwl_scan_req_unified_lmac *cmd = mvm->scan_cmd;
	struct iwl_scan_probe_req *preq;
	struct iwl_mvm_scan_params params = {};
	u32 flags;
	u32 ssid_bitmap = 0;
	int ret, i;

	lockdep_assert_held(&mvm->mutex);

	/* we should have failed registration if scan_cmd was NULL */
	if (WARN_ON(mvm->scan_cmd == NULL))
		return -ENOMEM;

	if (req->req.n_ssids > PROBE_OPTION_MAX ||
	    req->ies.common_ie_len + req->ies.len[NL80211_BAND_2GHZ] +
	    req->ies.len[NL80211_BAND_5GHZ] >
		iwl_mvm_max_scan_ie_fw_cmd_room(mvm, false) ||
	    req->req.n_channels > mvm->fw->ucode_capa.n_scan_channels)
		return -ENOBUFS;

	mvm->scan_status = IWL_MVM_SCAN_OS;

	iwl_mvm_scan_calc_params(mvm, vif, req->req.n_ssids, req->req.flags,
				 &params);

	iwl_mvm_build_generic_unified_scan_cmd(mvm, cmd, &params);

	cmd->n_channels = (u8)req->req.n_channels;

	flags = IWL_MVM_LMAC_SCAN_FLAG_PASS_ALL;

	if (req->req.n_ssids == 1 && req->req.ssids[0].ssid_len != 0)
		flags |= IWL_MVM_LMAC_SCAN_FLAG_PRE_CONNECTION;

	if (params.passive_fragmented)
		flags |= IWL_MVM_LMAC_SCAN_FLAG_FRAGMENTED;

	if (req->req.n_ssids == 0)
		flags |= IWL_MVM_LMAC_SCAN_FLAG_PASSIVE;

	cmd->scan_flags |= cpu_to_le32(flags);

	cmd->flags = iwl_mvm_scan_rxon_flags(req->req.channels[0]->band);
	cmd->filter_flags = cpu_to_le32(MAC_FILTER_ACCEPT_GRP |
					MAC_FILTER_IN_BEACON);
	iwl_mvm_unified_scan_fill_tx_cmd(mvm, cmd->tx_cmd, req->req.no_cck);
	iwl_mvm_scan_fill_ssids(cmd->direct_scan, req->req.ssids,
				req->req.n_ssids, 0);

	cmd->schedule[0].delay = 0;
	cmd->schedule[0].iterations = 1;
	cmd->schedule[0].full_scan_mul = 0;
	cmd->schedule[1].delay = 0;
	cmd->schedule[1].iterations = 0;
	cmd->schedule[1].full_scan_mul = 0;

	if (mvm->fw->ucode_capa.api[0] & IWL_UCODE_TLV_API_SINGLE_SCAN_EBS &&
	    mvm->last_ebs_successful) {
		cmd->channel_opt[0].flags =
			cpu_to_le16(IWL_SCAN_CHANNEL_FLAG_EBS |
				    IWL_SCAN_CHANNEL_FLAG_EBS_ACCURATE |
				    IWL_SCAN_CHANNEL_FLAG_CACHE_ADD);
		cmd->channel_opt[0].non_ebs_ratio =
			cpu_to_le16(IWL_DENSE_EBS_SCAN_RATIO);
		cmd->channel_opt[1].flags =
			cpu_to_le16(IWL_SCAN_CHANNEL_FLAG_EBS |
				    IWL_SCAN_CHANNEL_FLAG_EBS_ACCURATE |
				    IWL_SCAN_CHANNEL_FLAG_CACHE_ADD);
		cmd->channel_opt[1].non_ebs_ratio =
			cpu_to_le16(IWL_SPARSE_EBS_SCAN_RATIO);
	}

	for (i = 1; i <= req->req.n_ssids; i++)
		ssid_bitmap |= BIT(i);

	iwl_mvm_lmac_scan_cfg_channels(mvm, req->req.channels,
				       req->req.n_channels, ssid_bitmap,
				       cmd);

	preq = (void *)(cmd->data + sizeof(struct iwl_scan_channel_cfg_lmac) *
			mvm->fw->ucode_capa.n_scan_channels);

	iwl_mvm_build_unified_scan_probe(mvm, vif, &req->ies, preq,
		req->req.flags & NL80211_SCAN_FLAG_RANDOM_ADDR ?
			req->req.mac_addr : NULL,
		req->req.mac_addr_mask);

	ret = iwl_mvm_send_cmd(mvm, &hcmd);
	if (!ret) {
		IWL_DEBUG_SCAN(mvm, "Scan request was sent successfully\n");
	} else {
		/*
		 * If the scan failed, it usually means that the FW was unable
		 * to allocate the time events. Warn on it, but maybe we
		 * should try to send the command again with different params.
		 */
		IWL_ERR(mvm, "Scan failed! ret %d\n", ret);
		mvm->scan_status = IWL_MVM_SCAN_NONE;
		ret = -EIO;
	}
	return ret;
}

int iwl_mvm_unified_sched_scan_lmac(struct iwl_mvm *mvm,
				    struct ieee80211_vif *vif,
				    struct cfg80211_sched_scan_request *req,
				    struct ieee80211_scan_ies *ies)
{
	struct iwl_host_cmd hcmd = {
		.id = SCAN_OFFLOAD_REQUEST_CMD,
		.len = { sizeof(struct iwl_scan_req_unified_lmac) +
			 sizeof(struct iwl_scan_channel_cfg_lmac) *
				mvm->fw->ucode_capa.n_scan_channels +
			 sizeof(struct iwl_scan_probe_req), },
		.data = { mvm->scan_cmd, },
		.dataflags = { IWL_HCMD_DFL_NOCOPY, },
	};
	struct iwl_scan_req_unified_lmac *cmd = mvm->scan_cmd;
	struct iwl_scan_probe_req *preq;
	struct iwl_mvm_scan_params params = {};
	int ret;
	u32 flags = 0, ssid_bitmap = 0;

	lockdep_assert_held(&mvm->mutex);

	/* we should have failed registration if scan_cmd was NULL */
	if (WARN_ON(mvm->scan_cmd == NULL))
		return -ENOMEM;

	if (req->n_ssids > PROBE_OPTION_MAX ||
	    ies->common_ie_len + ies->len[NL80211_BAND_2GHZ] +
	    ies->len[NL80211_BAND_5GHZ] >
		iwl_mvm_max_scan_ie_fw_cmd_room(mvm, true) ||
	    req->n_channels > mvm->fw->ucode_capa.n_scan_channels)
		return -ENOBUFS;

	iwl_mvm_scan_calc_params(mvm, vif, req->n_ssids, 0, &params);

	iwl_mvm_build_generic_unified_scan_cmd(mvm, cmd, &params);

	cmd->n_channels = (u8)req->n_channels;

	if (iwl_mvm_scan_pass_all(mvm, req))
		flags |= IWL_MVM_LMAC_SCAN_FLAG_PASS_ALL;
	else
		flags |= IWL_MVM_LMAC_SCAN_FLAG_MATCH;

	if (req->n_ssids == 1 && req->ssids[0].ssid_len != 0)
		flags |= IWL_MVM_LMAC_SCAN_FLAG_PRE_CONNECTION;

	if (params.passive_fragmented)
		flags |= IWL_MVM_LMAC_SCAN_FLAG_FRAGMENTED;

	if (req->n_ssids == 0)
		flags |= IWL_MVM_LMAC_SCAN_FLAG_PASSIVE;

#ifdef CONFIG_IWLWIFI_DEBUGFS
	if (mvm->scan_iter_notif_enabled)
		flags |= IWL_MVM_LMAC_SCAN_FLAG_ITER_COMPLETE;
#endif

	cmd->scan_flags |= cpu_to_le32(flags);

	cmd->flags = iwl_mvm_scan_rxon_flags(req->channels[0]->band);
	cmd->filter_flags = cpu_to_le32(MAC_FILTER_ACCEPT_GRP |
					MAC_FILTER_IN_BEACON);
	iwl_mvm_unified_scan_fill_tx_cmd(mvm, cmd->tx_cmd, false);
	iwl_scan_offload_build_ssid(req, cmd->direct_scan, &ssid_bitmap, false);

	cmd->schedule[0].delay = cpu_to_le16(req->interval / MSEC_PER_SEC);
	cmd->schedule[0].iterations = IWL_FAST_SCHED_SCAN_ITERATIONS;
	cmd->schedule[0].full_scan_mul = 1;

	cmd->schedule[1].delay = cpu_to_le16(req->interval / MSEC_PER_SEC);
	cmd->schedule[1].iterations = 0xff;
	cmd->schedule[1].full_scan_mul = IWL_FULL_SCAN_MULTIPLIER;

	if (mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_EBS_SUPPORT &&
	    mvm->last_ebs_successful) {
		cmd->channel_opt[0].flags =
			cpu_to_le16(IWL_SCAN_CHANNEL_FLAG_EBS |
				    IWL_SCAN_CHANNEL_FLAG_EBS_ACCURATE |
				    IWL_SCAN_CHANNEL_FLAG_CACHE_ADD);
		cmd->channel_opt[0].non_ebs_ratio =
			cpu_to_le16(IWL_DENSE_EBS_SCAN_RATIO);
		cmd->channel_opt[1].flags =
			cpu_to_le16(IWL_SCAN_CHANNEL_FLAG_EBS |
				    IWL_SCAN_CHANNEL_FLAG_EBS_ACCURATE |
				    IWL_SCAN_CHANNEL_FLAG_CACHE_ADD);
		cmd->channel_opt[1].non_ebs_ratio =
			cpu_to_le16(IWL_SPARSE_EBS_SCAN_RATIO);
	}

	iwl_mvm_lmac_scan_cfg_channels(mvm, req->channels, req->n_channels,
				       ssid_bitmap, cmd);

	preq = (void *)(cmd->data + sizeof(struct iwl_scan_channel_cfg_lmac) *
			mvm->fw->ucode_capa.n_scan_channels);

	iwl_mvm_build_unified_scan_probe(mvm, vif, ies, preq,
		req->flags & NL80211_SCAN_FLAG_RANDOM_ADDR ?
			req->mac_addr : NULL,
		req->mac_addr_mask);

	ret = iwl_mvm_send_cmd(mvm, &hcmd);
	if (!ret) {
		IWL_DEBUG_SCAN(mvm,
			       "Sched scan request was sent successfully\n");
	} else {
		/*
		 * If the scan failed, it usually means that the FW was unable
		 * to allocate the time events. Warn on it, but maybe we
		 * should try to send the command again with different params.
		 */
		IWL_ERR(mvm, "Sched scan failed! ret %d\n", ret);
		mvm->scan_status = IWL_MVM_SCAN_NONE;
		ret = -EIO;
	}
	return ret;
}


int iwl_mvm_cancel_scan(struct iwl_mvm *mvm)
{
	if (mvm->fw->ucode_capa.capa[0] & IWL_UCODE_TLV_CAPA_UMAC_SCAN)
		return iwl_umac_scan_stop(mvm, IWL_UMAC_SCAN_UID_REG_SCAN,
					  true);

	if (mvm->scan_status == IWL_MVM_SCAN_NONE)
		return 0;

	if (iwl_mvm_is_radio_killed(mvm)) {
		ieee80211_scan_completed(mvm->hw, true);
		iwl_mvm_unref(mvm, IWL_MVM_REF_SCAN);
		mvm->scan_status = IWL_MVM_SCAN_NONE;
		return 0;
	}

	return iwl_mvm_scan_offload_stop(mvm, true);
}

/* UMAC scan API */

struct iwl_umac_scan_done {
	struct iwl_mvm *mvm;
	enum iwl_umac_scan_uid_type type;
};

static int rate_to_scan_rate_flag(unsigned int rate)
{
	static const int rate_to_scan_rate[IWL_RATE_COUNT] = {
		[IWL_RATE_1M_INDEX]	= SCAN_CONFIG_RATE_1M,
		[IWL_RATE_2M_INDEX]	= SCAN_CONFIG_RATE_2M,
		[IWL_RATE_5M_INDEX]	= SCAN_CONFIG_RATE_5M,
		[IWL_RATE_11M_INDEX]	= SCAN_CONFIG_RATE_11M,
		[IWL_RATE_6M_INDEX]	= SCAN_CONFIG_RATE_6M,
		[IWL_RATE_9M_INDEX]	= SCAN_CONFIG_RATE_9M,
		[IWL_RATE_12M_INDEX]	= SCAN_CONFIG_RATE_12M,
		[IWL_RATE_18M_INDEX]	= SCAN_CONFIG_RATE_18M,
		[IWL_RATE_24M_INDEX]	= SCAN_CONFIG_RATE_24M,
		[IWL_RATE_36M_INDEX]	= SCAN_CONFIG_RATE_36M,
		[IWL_RATE_48M_INDEX]	= SCAN_CONFIG_RATE_48M,
		[IWL_RATE_54M_INDEX]	= SCAN_CONFIG_RATE_54M,
	};

	return rate_to_scan_rate[rate];
}

static __le32 iwl_mvm_scan_config_rates(struct iwl_mvm *mvm)
{
	struct ieee80211_supported_band *band;
	unsigned int rates = 0;
	int i;

	band = &mvm->nvm_data->bands[IEEE80211_BAND_2GHZ];
	for (i = 0; i < band->n_bitrates; i++)
		rates |= rate_to_scan_rate_flag(band->bitrates[i].hw_value);
	band = &mvm->nvm_data->bands[IEEE80211_BAND_5GHZ];
	for (i = 0; i < band->n_bitrates; i++)
		rates |= rate_to_scan_rate_flag(band->bitrates[i].hw_value);

	/* Set both basic rates and supported rates */
	rates |= SCAN_CONFIG_SUPPORTED_RATE(rates);

	return cpu_to_le32(rates);
}

int iwl_mvm_config_scan(struct iwl_mvm *mvm)
{

	struct iwl_scan_config *scan_config;
	struct ieee80211_supported_band *band;
	int num_channels =
		mvm->nvm_data->bands[IEEE80211_BAND_2GHZ].n_channels +
		mvm->nvm_data->bands[IEEE80211_BAND_5GHZ].n_channels;
	int ret, i, j = 0, cmd_size, data_size;
	struct iwl_host_cmd cmd = {
		.id = SCAN_CFG_CMD,
	};

	if (WARN_ON(num_channels > mvm->fw->ucode_capa.n_scan_channels))
		return -ENOBUFS;

	cmd_size = sizeof(*scan_config) + mvm->fw->ucode_capa.n_scan_channels;

	scan_config = kzalloc(cmd_size, GFP_KERNEL);
	if (!scan_config)
		return -ENOMEM;

	data_size = cmd_size - sizeof(struct iwl_mvm_umac_cmd_hdr);
	scan_config->hdr.size = cpu_to_le16(data_size);
	scan_config->flags = cpu_to_le32(SCAN_CONFIG_FLAG_ACTIVATE |
					 SCAN_CONFIG_FLAG_ALLOW_CHUB_REQS |
					 SCAN_CONFIG_FLAG_SET_TX_CHAINS |
					 SCAN_CONFIG_FLAG_SET_RX_CHAINS |
					 SCAN_CONFIG_FLAG_SET_ALL_TIMES |
					 SCAN_CONFIG_FLAG_SET_LEGACY_RATES |
					 SCAN_CONFIG_FLAG_SET_MAC_ADDR |
					 SCAN_CONFIG_FLAG_SET_CHANNEL_FLAGS|
					 SCAN_CONFIG_N_CHANNELS(num_channels));
	scan_config->tx_chains = cpu_to_le32(iwl_mvm_get_valid_tx_ant(mvm));
	scan_config->rx_chains = cpu_to_le32(iwl_mvm_scan_rx_ant(mvm));
	scan_config->legacy_rates = iwl_mvm_scan_config_rates(mvm);
	scan_config->out_of_channel_time = cpu_to_le32(170);
	scan_config->suspend_time = cpu_to_le32(30);
	scan_config->dwell_active = 20;
	scan_config->dwell_passive = 110;
	scan_config->dwell_fragmented = 20;

	memcpy(&scan_config->mac_addr, &mvm->addresses[0].addr, ETH_ALEN);

	scan_config->bcast_sta_id = mvm->aux_sta.sta_id;
	scan_config->channel_flags = IWL_CHANNEL_FLAG_EBS |
				     IWL_CHANNEL_FLAG_ACCURATE_EBS |
				     IWL_CHANNEL_FLAG_EBS_ADD |
				     IWL_CHANNEL_FLAG_PRE_SCAN_PASSIVE2ACTIVE;

	band = &mvm->nvm_data->bands[IEEE80211_BAND_2GHZ];
	for (i = 0; i < band->n_channels; i++, j++)
		scan_config->channel_array[j] = band->channels[i].hw_value;
	band = &mvm->nvm_data->bands[IEEE80211_BAND_5GHZ];
	for (i = 0; i < band->n_channels; i++, j++)
		scan_config->channel_array[j] = band->channels[i].hw_value;

	cmd.data[0] = scan_config;
	cmd.len[0] = cmd_size;
	cmd.dataflags[0] = IWL_HCMD_DFL_NOCOPY;

	IWL_DEBUG_SCAN(mvm, "Sending UMAC scan config\n");

	ret = iwl_mvm_send_cmd(mvm, &cmd);

	kfree(scan_config);
	return ret;
}

static int iwl_mvm_find_scan_uid(struct iwl_mvm *mvm, u32 uid)
{
	int i;

	for (i = 0; i < IWL_MVM_MAX_SIMULTANEOUS_SCANS; i++)
		if (mvm->scan_uid[i] == uid)
			return i;

	return i;
}

static int iwl_mvm_find_free_scan_uid(struct iwl_mvm *mvm)
{
	return iwl_mvm_find_scan_uid(mvm, 0);
}

static bool iwl_mvm_find_scan_type(struct iwl_mvm *mvm,
				   enum iwl_umac_scan_uid_type type)
{
	int i;

	for (i = 0; i < IWL_MVM_MAX_SIMULTANEOUS_SCANS; i++)
		if (mvm->scan_uid[i] & type)
			return true;

	return false;
}

static u32 iwl_generate_scan_uid(struct iwl_mvm *mvm,
				 enum iwl_umac_scan_uid_type type)
{
	u32 uid;

	/* make sure exactly one bit is on in scan type */
	WARN_ON(hweight8(type) != 1);

	/*
	 * Make sure scan uids are unique. If one scan lasts long time while
	 * others are completing frequently, the seq number will wrap up and
	 * we may have more than one scan with the same uid.
	 */
	do {
		uid = type | (mvm->scan_seq_num <<
			      IWL_UMAC_SCAN_UID_SEQ_OFFSET);
		mvm->scan_seq_num++;
	} while (iwl_mvm_find_scan_uid(mvm, uid) <
		 IWL_MVM_MAX_SIMULTANEOUS_SCANS);

	IWL_DEBUG_SCAN(mvm, "Generated scan UID %u\n", uid);

	return uid;
}

static void
iwl_mvm_build_generic_umac_scan_cmd(struct iwl_mvm *mvm,
				    struct iwl_scan_req_umac *cmd,
				    struct iwl_mvm_scan_params *params)
{
	memset(cmd, 0, ksize(cmd));
	cmd->hdr.size = cpu_to_le16(iwl_mvm_scan_size(mvm) -
				    sizeof(struct iwl_mvm_umac_cmd_hdr));
	cmd->active_dwell = params->dwell[IEEE80211_BAND_2GHZ].active;
	cmd->passive_dwell = params->dwell[IEEE80211_BAND_2GHZ].passive;
	if (params->passive_fragmented)
		cmd->fragmented_dwell =
				params->dwell[IEEE80211_BAND_2GHZ].fragmented;
	cmd->max_out_time = cpu_to_le32(params->max_out_time);
	cmd->suspend_time = cpu_to_le32(params->suspend_time);
	cmd->scan_priority = cpu_to_le32(IWL_SCAN_PRIORITY_HIGH);
}

static void
iwl_mvm_umac_scan_cfg_channels(struct iwl_mvm *mvm,
			       struct ieee80211_channel **channels,
			       int n_channels, u32 ssid_bitmap,
			       struct iwl_scan_req_umac *cmd)
{
	struct iwl_scan_channel_cfg_umac *channel_cfg = (void *)&cmd->data;
	int i;

	for (i = 0; i < n_channels; i++) {
		channel_cfg[i].flags = cpu_to_le32(ssid_bitmap);
		channel_cfg[i].channel_num = channels[i]->hw_value;
		channel_cfg[i].iter_count = 1;
		channel_cfg[i].iter_interval = 0;
	}
}

static u32 iwl_mvm_scan_umac_common_flags(struct iwl_mvm *mvm, int n_ssids,
					  struct cfg80211_ssid *ssids,
					  int fragmented)
{
	int flags = 0;

	if (n_ssids == 0)
		flags = IWL_UMAC_SCAN_GEN_FLAGS_PASSIVE;

	if (n_ssids == 1 && ssids[0].ssid_len != 0)
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_PRE_CONNECT;

	if (fragmented)
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_FRAGMENTED;

	if (iwl_mvm_rrm_scan_needed(mvm))
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_RRM_ENABLED;

	return flags;
}

int iwl_mvm_scan_umac(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
		      struct ieee80211_scan_request *req)
{
	struct iwl_host_cmd hcmd = {
		.id = SCAN_REQ_UMAC,
		.len = { iwl_mvm_scan_size(mvm), },
		.data = { mvm->scan_cmd, },
		.dataflags = { IWL_HCMD_DFL_NOCOPY, },
	};
	struct iwl_scan_req_umac *cmd = mvm->scan_cmd;
	struct iwl_scan_req_umac_tail *sec_part = (void *)&cmd->data +
		sizeof(struct iwl_scan_channel_cfg_umac) *
			mvm->fw->ucode_capa.n_scan_channels;
	struct iwl_mvm_scan_params params = {};
	u32 uid, flags;
	u32 ssid_bitmap = 0;
	int ret, i, uid_idx;

	lockdep_assert_held(&mvm->mutex);

	uid_idx = iwl_mvm_find_free_scan_uid(mvm);
	if (uid_idx >= IWL_MVM_MAX_SIMULTANEOUS_SCANS)
		return -EBUSY;

	/* we should have failed registration if scan_cmd was NULL */
	if (WARN_ON(mvm->scan_cmd == NULL))
		return -ENOMEM;

	if (WARN_ON(req->req.n_ssids > PROBE_OPTION_MAX ||
		    req->ies.common_ie_len +
		    req->ies.len[NL80211_BAND_2GHZ] +
		    req->ies.len[NL80211_BAND_5GHZ] + 24 + 2 >
		    SCAN_OFFLOAD_PROBE_REQ_SIZE || req->req.n_channels >
		    mvm->fw->ucode_capa.n_scan_channels))
		return -ENOBUFS;

	iwl_mvm_scan_calc_params(mvm, vif, req->req.n_ssids, req->req.flags,
				 &params);

	iwl_mvm_build_generic_umac_scan_cmd(mvm, cmd, &params);

	uid = iwl_generate_scan_uid(mvm, IWL_UMAC_SCAN_UID_REG_SCAN);
	mvm->scan_uid[uid_idx] = uid;
	cmd->uid = cpu_to_le32(uid);

	cmd->ooc_priority = cpu_to_le32(IWL_SCAN_PRIORITY_HIGH);

	flags = iwl_mvm_scan_umac_common_flags(mvm, req->req.n_ssids,
					       req->req.ssids,
					       params.passive_fragmented);

	flags |= IWL_UMAC_SCAN_GEN_FLAGS_PASS_ALL;

	cmd->general_flags = cpu_to_le32(flags);

	if (mvm->fw->ucode_capa.api[0] & IWL_UCODE_TLV_API_SINGLE_SCAN_EBS &&
	    mvm->last_ebs_successful)
		cmd->channel_flags = IWL_SCAN_CHANNEL_FLAG_EBS |
				     IWL_SCAN_CHANNEL_FLAG_EBS_ACCURATE |
				     IWL_SCAN_CHANNEL_FLAG_CACHE_ADD;

	cmd->n_channels = req->req.n_channels;

	for (i = 0; i < req->req.n_ssids; i++)
		ssid_bitmap |= BIT(i);

	iwl_mvm_umac_scan_cfg_channels(mvm, req->req.channels,
				       req->req.n_channels, ssid_bitmap, cmd);

	sec_part->schedule[0].iter_count = 1;
	sec_part->delay = 0;

	iwl_mvm_build_unified_scan_probe(mvm, vif, &req->ies, &sec_part->preq,
		req->req.flags & NL80211_SCAN_FLAG_RANDOM_ADDR ?
			req->req.mac_addr : NULL,
		req->req.mac_addr_mask);

	iwl_mvm_scan_fill_ssids(sec_part->direct_scan, req->req.ssids,
				req->req.n_ssids, 0);

	ret = iwl_mvm_send_cmd(mvm, &hcmd);
	if (!ret) {
		IWL_DEBUG_SCAN(mvm,
			       "Scan request was sent successfully\n");
	} else {
		/*
		 * If the scan failed, it usually means that the FW was unable
		 * to allocate the time events. Warn on it, but maybe we
		 * should try to send the command again with different params.
		 */
		IWL_ERR(mvm, "Scan failed! ret %d\n", ret);
	}
	return ret;
}

int iwl_mvm_sched_scan_umac(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			    struct cfg80211_sched_scan_request *req,
			    struct ieee80211_scan_ies *ies)
{

	struct iwl_host_cmd hcmd = {
		.id = SCAN_REQ_UMAC,
		.len = { iwl_mvm_scan_size(mvm), },
		.data = { mvm->scan_cmd, },
		.dataflags = { IWL_HCMD_DFL_NOCOPY, },
	};
	struct iwl_scan_req_umac *cmd = mvm->scan_cmd;
	struct iwl_scan_req_umac_tail *sec_part = (void *)&cmd->data +
		sizeof(struct iwl_scan_channel_cfg_umac) *
			mvm->fw->ucode_capa.n_scan_channels;
	struct iwl_mvm_scan_params params = {};
	u32 uid, flags;
	u32 ssid_bitmap = 0;
	int ret, uid_idx;

	lockdep_assert_held(&mvm->mutex);

	uid_idx = iwl_mvm_find_free_scan_uid(mvm);
	if (uid_idx >= IWL_MVM_MAX_SIMULTANEOUS_SCANS)
		return -EBUSY;

	/* we should have failed registration if scan_cmd was NULL */
	if (WARN_ON(mvm->scan_cmd == NULL))
		return -ENOMEM;

	if (WARN_ON(req->n_ssids > PROBE_OPTION_MAX ||
		    ies->common_ie_len + ies->len[NL80211_BAND_2GHZ] +
		    ies->len[NL80211_BAND_5GHZ] + 24 + 2 >
		    SCAN_OFFLOAD_PROBE_REQ_SIZE || req->n_channels >
		    mvm->fw->ucode_capa.n_scan_channels))
		return -ENOBUFS;

	iwl_mvm_scan_calc_params(mvm, vif, req->n_ssids, req->flags,
					 &params);

	iwl_mvm_build_generic_umac_scan_cmd(mvm, cmd, &params);

	cmd->flags = cpu_to_le32(IWL_UMAC_SCAN_FLAG_PREEMPTIVE);

	uid = iwl_generate_scan_uid(mvm, IWL_UMAC_SCAN_UID_SCHED_SCAN);
	mvm->scan_uid[uid_idx] = uid;
	cmd->uid = cpu_to_le32(uid);

	cmd->ooc_priority = cpu_to_le32(IWL_SCAN_PRIORITY_LOW);

	flags = iwl_mvm_scan_umac_common_flags(mvm, req->n_ssids, req->ssids,
					       params.passive_fragmented);

	flags |= IWL_UMAC_SCAN_GEN_FLAGS_PERIODIC;

	if (iwl_mvm_scan_pass_all(mvm, req))
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_PASS_ALL;
	else
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_MATCH;

	cmd->general_flags = cpu_to_le32(flags);

	if (mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_EBS_SUPPORT &&
	    mvm->last_ebs_successful)
		cmd->channel_flags = IWL_SCAN_CHANNEL_FLAG_EBS |
				     IWL_SCAN_CHANNEL_FLAG_EBS_ACCURATE |
				     IWL_SCAN_CHANNEL_FLAG_CACHE_ADD;

	cmd->n_channels = req->n_channels;

	iwl_scan_offload_build_ssid(req, sec_part->direct_scan, &ssid_bitmap,
				    false);

	/* This API uses bits 0-19 instead of 1-20. */
	ssid_bitmap = ssid_bitmap >> 1;

	iwl_mvm_umac_scan_cfg_channels(mvm, req->channels, req->n_channels,
				       ssid_bitmap, cmd);

	sec_part->schedule[0].interval =
				cpu_to_le16(req->interval / MSEC_PER_SEC);
	sec_part->schedule[0].iter_count = 0xff;

	sec_part->delay = 0;

	iwl_mvm_build_unified_scan_probe(mvm, vif, ies, &sec_part->preq,
		req->flags & NL80211_SCAN_FLAG_RANDOM_ADDR ?
			req->mac_addr : NULL,
		req->mac_addr_mask);

	ret = iwl_mvm_send_cmd(mvm, &hcmd);
	if (!ret) {
		IWL_DEBUG_SCAN(mvm,
			       "Sched scan request was sent successfully\n");
	} else {
		/*
		 * If the scan failed, it usually means that the FW was unable
		 * to allocate the time events. Warn on it, but maybe we
		 * should try to send the command again with different params.
		 */
		IWL_ERR(mvm, "Sched scan failed! ret %d\n", ret);
	}
	return ret;
}

int iwl_mvm_rx_umac_scan_complete_notif(struct iwl_mvm *mvm,
					struct iwl_rx_cmd_buffer *rxb,
					struct iwl_device_cmd *cmd)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_umac_scan_complete *notif = (void *)pkt->data;
	u32 uid = __le32_to_cpu(notif->uid);
	bool sched = !!(uid & IWL_UMAC_SCAN_UID_SCHED_SCAN);
	int uid_idx = iwl_mvm_find_scan_uid(mvm, uid);

	/*
	 * Scan uid may be set to zero in case of scan abort request from above.
	 */
	if (uid_idx >= IWL_MVM_MAX_SIMULTANEOUS_SCANS)
		return 0;

	IWL_DEBUG_SCAN(mvm,
		       "Scan completed, uid %u type %s, status %s, EBS status %s\n",
		       uid, sched ? "sched" : "regular",
		       notif->status == IWL_SCAN_OFFLOAD_COMPLETED ?
				"completed" : "aborted",
		       notif->ebs_status == IWL_SCAN_EBS_SUCCESS ?
				"success" : "failed");

	if (notif->ebs_status)
		mvm->last_ebs_successful = false;

	mvm->scan_uid[uid_idx] = 0;

	if (!sched) {
		ieee80211_scan_completed(mvm->hw,
					 notif->status ==
						IWL_SCAN_OFFLOAD_ABORTED);
		iwl_mvm_unref(mvm, IWL_MVM_REF_SCAN);
	} else if (!iwl_mvm_find_scan_type(mvm, IWL_UMAC_SCAN_UID_SCHED_SCAN)) {
		ieee80211_sched_scan_stopped(mvm->hw);
	} else {
		IWL_DEBUG_SCAN(mvm, "Another sched scan is running\n");
	}

	return 0;
}

static bool iwl_scan_umac_done_check(struct iwl_notif_wait_data *notif_wait,
				     struct iwl_rx_packet *pkt, void *data)
{
	struct iwl_umac_scan_done *scan_done = data;
	struct iwl_umac_scan_complete *notif = (void *)pkt->data;
	u32 uid = __le32_to_cpu(notif->uid);
	int uid_idx = iwl_mvm_find_scan_uid(scan_done->mvm, uid);

	if (WARN_ON(pkt->hdr.cmd != SCAN_COMPLETE_UMAC))
		return false;

	if (uid_idx >= IWL_MVM_MAX_SIMULTANEOUS_SCANS)
		return false;

	/*
	 * Clear scan uid of scans that was aborted from above and completed
	 * in FW so the RX handler does nothing. Set last_ebs_successful here if
	 * needed.
	 */
	scan_done->mvm->scan_uid[uid_idx] = 0;

	if (notif->ebs_status)
		scan_done->mvm->last_ebs_successful = false;

	return !iwl_mvm_find_scan_type(scan_done->mvm, scan_done->type);
}

static int iwl_umac_scan_abort_one(struct iwl_mvm *mvm, u32 uid)
{
	struct iwl_umac_scan_abort cmd = {
		.hdr.size = cpu_to_le16(sizeof(struct iwl_umac_scan_abort) -
					sizeof(struct iwl_mvm_umac_cmd_hdr)),
		.uid = cpu_to_le32(uid),
	};

	lockdep_assert_held(&mvm->mutex);

	IWL_DEBUG_SCAN(mvm, "Sending scan abort, uid %u\n", uid);

	return iwl_mvm_send_cmd_pdu(mvm, SCAN_ABORT_UMAC, 0, sizeof(cmd), &cmd);
}

static int iwl_umac_scan_stop(struct iwl_mvm *mvm,
			      enum iwl_umac_scan_uid_type type, bool notify)
{
	struct iwl_notification_wait wait_scan_done;
	static const u8 scan_done_notif[] = { SCAN_COMPLETE_UMAC, };
	struct iwl_umac_scan_done scan_done = {
		.mvm = mvm,
		.type = type,
	};
	int i, ret = -EIO;

	iwl_init_notification_wait(&mvm->notif_wait, &wait_scan_done,
				   scan_done_notif,
				   ARRAY_SIZE(scan_done_notif),
				   iwl_scan_umac_done_check, &scan_done);

	IWL_DEBUG_SCAN(mvm, "Preparing to stop scan, type %x\n", type);

	for (i = 0; i < IWL_MVM_MAX_SIMULTANEOUS_SCANS; i++) {
		if (mvm->scan_uid[i] & type) {
			int err;

			if (iwl_mvm_is_radio_killed(mvm) &&
			    (type & IWL_UMAC_SCAN_UID_REG_SCAN)) {
				ieee80211_scan_completed(mvm->hw, true);
				iwl_mvm_unref(mvm, IWL_MVM_REF_SCAN);
				break;
			}

			err = iwl_umac_scan_abort_one(mvm, mvm->scan_uid[i]);
			if (!err)
				ret = 0;
		}
	}

	if (ret) {
		IWL_DEBUG_SCAN(mvm, "Couldn't stop scan\n");
		iwl_remove_notification(&mvm->notif_wait, &wait_scan_done);
		return ret;
	}

	ret = iwl_wait_notification(&mvm->notif_wait, &wait_scan_done, 1 * HZ);
	if (ret)
		return ret;

	if (notify) {
		if (type & IWL_UMAC_SCAN_UID_SCHED_SCAN)
			ieee80211_sched_scan_stopped(mvm->hw);
		if (type & IWL_UMAC_SCAN_UID_REG_SCAN) {
			ieee80211_scan_completed(mvm->hw, true);
			iwl_mvm_unref(mvm, IWL_MVM_REF_SCAN);
		}
	}

	return ret;
}

int iwl_mvm_scan_size(struct iwl_mvm *mvm)
{
	if (mvm->fw->ucode_capa.capa[0] & IWL_UCODE_TLV_CAPA_UMAC_SCAN)
		return sizeof(struct iwl_scan_req_umac) +
			sizeof(struct iwl_scan_channel_cfg_umac) *
				mvm->fw->ucode_capa.n_scan_channels +
			sizeof(struct iwl_scan_req_umac_tail);

	return sizeof(struct iwl_scan_req_unified_lmac) +
		sizeof(struct iwl_scan_channel_cfg_lmac) *
		mvm->fw->ucode_capa.n_scan_channels +
		sizeof(struct iwl_scan_probe_req);
}
