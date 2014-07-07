/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
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

struct iwl_mvm_scan_params {
	u32 max_out_time;
	u32 suspend_time;
	bool passive_fragmented;
	struct _dwell {
		u16 passive;
		u16 active;
	} dwell[IEEE80211_NUM_BANDS];
};

static inline __le16 iwl_mvm_scan_rx_chain(struct iwl_mvm *mvm)
{
	u16 rx_chain;
	u8 rx_ant;

	if (mvm->scan_rx_ant != ANT_NONE)
		rx_ant = mvm->scan_rx_ant;
	else
		rx_ant = mvm->fw->valid_rx_ant;
	rx_chain = rx_ant << PHY_RX_CHAIN_VALID_POS;
	rx_chain |= rx_ant << PHY_RX_CHAIN_FORCE_MIMO_SEL_POS;
	rx_chain |= rx_ant << PHY_RX_CHAIN_FORCE_SEL_POS;
	rx_chain |= 0x1 << PHY_RX_CHAIN_DRIVER_FORCE_POS;
	return cpu_to_le16(rx_chain);
}

static inline __le32
iwl_mvm_scan_rxon_flags(struct cfg80211_scan_request *req)
{
	if (req->channels[0]->band == IEEE80211_BAND_2GHZ)
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
		iwl_mvm_next_antenna(mvm, mvm->fw->valid_tx_ant,
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
static void iwl_mvm_scan_fill_ssids(struct iwl_scan_cmd *cmd,
				    struct cfg80211_scan_request *req,
				    int first)
{
	int fw_idx, req_idx;

	for (req_idx = req->n_ssids - 1, fw_idx = 0; req_idx >= first;
	     req_idx--, fw_idx++) {
		cmd->direct_scan[fw_idx].id = WLAN_EID_SSID;
		cmd->direct_scan[fw_idx].len = req->ssids[req_idx].ssid_len;
		memcpy(cmd->direct_scan[fw_idx].ssid,
		       req->ssids[req_idx].ssid,
		       req->ssids[req_idx].ssid_len);
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
static u16 iwl_mvm_get_active_dwell(enum ieee80211_band band, int n_ssids)
{
	if (band == IEEE80211_BAND_2GHZ)
		return 30  + 3 * (n_ssids + 1);
	return 20  + 2 * (n_ssids + 1);
}

static u16 iwl_mvm_get_passive_dwell(enum ieee80211_band band)
{
	return band == IEEE80211_BAND_2GHZ ? 100 + 20 : 100 + 10;
}

static void iwl_mvm_scan_fill_channels(struct iwl_scan_cmd *cmd,
				       struct cfg80211_scan_request *req,
				       bool basic_ssid,
				       struct iwl_mvm_scan_params *params)
{
	struct iwl_scan_channel *chan = (struct iwl_scan_channel *)
		(cmd->data + le16_to_cpu(cmd->tx_cmd.len));
	int i;
	int type = BIT(req->n_ssids) - 1;
	enum ieee80211_band band = req->channels[0]->band;

	if (!basic_ssid)
		type |= BIT(req->n_ssids);

	for (i = 0; i < cmd->channel_count; i++) {
		chan->channel = cpu_to_le16(req->channels[i]->hw_value);
		chan->type = cpu_to_le32(type);
		if (req->channels[i]->flags & IEEE80211_CHAN_NO_IR)
			chan->type &= cpu_to_le32(~SCAN_CHANNEL_TYPE_ACTIVE);
		chan->active_dwell = cpu_to_le16(params->dwell[band].active);
		chan->passive_dwell = cpu_to_le16(params->dwell[band].passive);
		chan->iteration_count = cpu_to_le16(1);
		chan++;
	}
}

/*
 * Fill in probe request with the following parameters:
 * TA is our vif HW address, which mac80211 ensures we have.
 * Packet is broadcasted, so this is both SA and DA.
 * The probe request IE is made out of two: first comes the most prioritized
 * SSID if a directed scan is requested. Second comes whatever extra
 * information was given to us as the scan request IE.
 */
static u16 iwl_mvm_fill_probe_req(struct ieee80211_mgmt *frame, const u8 *ta,
				  int n_ssids, const u8 *ssid, int ssid_len,
				  const u8 *ie, int ie_len,
				  int left)
{
	int len = 0;
	u8 *pos = NULL;

	/* Make sure there is enough space for the probe request,
	 * two mandatory IEs and the data */
	left -= 24;
	if (left < 0)
		return 0;

	frame->frame_control = cpu_to_le16(IEEE80211_STYPE_PROBE_REQ);
	eth_broadcast_addr(frame->da);
	memcpy(frame->sa, ta, ETH_ALEN);
	eth_broadcast_addr(frame->bssid);
	frame->seq_ctrl = 0;

	len += 24;

	/* for passive scans, no need to fill anything */
	if (n_ssids == 0)
		return (u16)len;

	/* points to the payload of the request */
	pos = &frame->u.probe_req.variable[0];

	/* fill in our SSID IE */
	left -= ssid_len + 2;
	if (left < 0)
		return 0;
	*pos++ = WLAN_EID_SSID;
	*pos++ = ssid_len;
	if (ssid && ssid_len) { /* ssid_len may be == 0 even if ssid is valid */
		memcpy(pos, ssid, ssid_len);
		pos += ssid_len;
	}

	len += ssid_len + 2;

	if (WARN_ON(left < ie_len))
		return len;

	if (ie && ie_len) {
		memcpy(pos, ie, ie_len);
		len += ie_len;
	}

	return (u16)len;
}

static void iwl_mvm_scan_condition_iterator(void *data, u8 *mac,
					    struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	bool *global_bound = data;

	if (mvmvif->phy_ctxt && mvmvif->phy_ctxt->id < MAX_PHYS)
		*global_bound = true;
}

static void iwl_mvm_scan_calc_params(struct iwl_mvm *mvm,
				     struct ieee80211_vif *vif,
				     int n_ssids,
				     struct iwl_mvm_scan_params *params)
{
	bool global_bound = false;
	enum ieee80211_band band;

	ieee80211_iterate_active_interfaces_atomic(mvm->hw,
					    IEEE80211_IFACE_ITER_NORMAL,
					    iwl_mvm_scan_condition_iterator,
					    &global_bound);

	if (!global_bound)
		goto not_bound;

	params->suspend_time = 100;
	params->max_out_time = 600;

	if (iwl_mvm_low_latency(mvm)) {
		params->suspend_time = 250;
		params->max_out_time = 250;
	}

not_bound:

	for (band = IEEE80211_BAND_2GHZ; band < IEEE80211_NUM_BANDS; band++) {
		params->dwell[band].passive = iwl_mvm_get_passive_dwell(band);
		params->dwell[band].active = iwl_mvm_get_active_dwell(band,
								      n_ssids);
	}
}

int iwl_mvm_scan_request(struct iwl_mvm *mvm,
			 struct ieee80211_vif *vif,
			 struct cfg80211_scan_request *req)
{
	struct iwl_host_cmd hcmd = {
		.id = SCAN_REQUEST_CMD,
		.len = { 0, },
		.data = { mvm->scan_cmd, },
		.dataflags = { IWL_HCMD_DFL_NOCOPY, },
	};
	struct iwl_scan_cmd *cmd = mvm->scan_cmd;
	int ret;
	u32 status;
	int ssid_len = 0;
	u8 *ssid = NULL;
	bool basic_ssid = !(mvm->fw->ucode_capa.flags &
			   IWL_UCODE_TLV_FLAGS_NO_BASIC_SSID);
	struct iwl_mvm_scan_params params = {};

	lockdep_assert_held(&mvm->mutex);

	/* we should have failed registration if scan_cmd was NULL */
	if (WARN_ON(mvm->scan_cmd == NULL))
		return -ENOMEM;

	IWL_DEBUG_SCAN(mvm, "Handling mac80211 scan request\n");
	mvm->scan_status = IWL_MVM_SCAN_OS;
	memset(cmd, 0, sizeof(struct iwl_scan_cmd) +
	       mvm->fw->ucode_capa.max_probe_length +
	       (MAX_NUM_SCAN_CHANNELS * sizeof(struct iwl_scan_channel)));

	cmd->channel_count = (u8)req->n_channels;
	cmd->quiet_time = cpu_to_le16(IWL_ACTIVE_QUIET_TIME);
	cmd->quiet_plcp_th = cpu_to_le16(IWL_PLCP_QUIET_THRESH);
	cmd->rxchain_sel_flags = iwl_mvm_scan_rx_chain(mvm);

	iwl_mvm_scan_calc_params(mvm, vif, req->n_ssids, &params);
	cmd->max_out_time = cpu_to_le32(params.max_out_time);
	cmd->suspend_time = cpu_to_le32(params.suspend_time);
	if (params.passive_fragmented)
		cmd->scan_flags |= SCAN_FLAGS_FRAGMENTED_SCAN;

	cmd->rxon_flags = iwl_mvm_scan_rxon_flags(req);
	cmd->filter_flags = cpu_to_le32(MAC_FILTER_ACCEPT_GRP |
					MAC_FILTER_IN_BEACON);

	if (vif->type == NL80211_IFTYPE_P2P_DEVICE)
		cmd->type = cpu_to_le32(SCAN_TYPE_DISCOVERY_FORCED);
	else
		cmd->type = cpu_to_le32(SCAN_TYPE_FORCED);

	cmd->repeats = cpu_to_le32(1);

	/*
	 * If the user asked for passive scan, don't change to active scan if
	 * you see any activity on the channel - remain passive.
	 */
	if (req->n_ssids > 0) {
		cmd->passive2active = cpu_to_le16(1);
		cmd->scan_flags |= SCAN_FLAGS_PASSIVE2ACTIVE;
		if (basic_ssid) {
			ssid = req->ssids[0].ssid;
			ssid_len = req->ssids[0].ssid_len;
		}
	} else {
		cmd->passive2active = 0;
		cmd->scan_flags &= ~SCAN_FLAGS_PASSIVE2ACTIVE;
	}

	iwl_mvm_scan_fill_ssids(cmd, req, basic_ssid ? 1 : 0);

	cmd->tx_cmd.tx_flags = cpu_to_le32(TX_CMD_FLG_SEQ_CTL |
					   TX_CMD_FLG_BT_DIS);
	cmd->tx_cmd.sta_id = mvm->aux_sta.sta_id;
	cmd->tx_cmd.life_time = cpu_to_le32(TX_CMD_LIFE_TIME_INFINITE);
	cmd->tx_cmd.rate_n_flags =
			iwl_mvm_scan_rate_n_flags(mvm, req->channels[0]->band,
						  req->no_cck);

	cmd->tx_cmd.len =
		cpu_to_le16(iwl_mvm_fill_probe_req(
			    (struct ieee80211_mgmt *)cmd->data,
			    vif->addr,
			    req->n_ssids, ssid, ssid_len,
			    req->ie, req->ie_len,
			    mvm->fw->ucode_capa.max_probe_length));

	iwl_mvm_scan_fill_channels(cmd, req, basic_ssid, &params);

	cmd->len = cpu_to_le16(sizeof(struct iwl_scan_cmd) +
		le16_to_cpu(cmd->tx_cmd.len) +
		(cmd->channel_count * sizeof(struct iwl_scan_channel)));
	hcmd.len[0] = le16_to_cpu(cmd->len);

	status = SCAN_RESPONSE_OK;
	ret = iwl_mvm_send_cmd_status(mvm, &hcmd, &status);
	if (!ret && status == SCAN_RESPONSE_OK) {
		IWL_DEBUG_SCAN(mvm, "Scan request was sent successfully\n");
	} else {
		/*
		 * If the scan failed, it usually means that the FW was unable
		 * to allocate the time events. Warn on it, but maybe we
		 * should try to send the command again with different params.
		 */
		IWL_ERR(mvm, "Scan failed! status 0x%x ret %d\n",
			status, ret);
		mvm->scan_status = IWL_MVM_SCAN_NONE;
		ret = -EIO;
	}
	return ret;
}

int iwl_mvm_rx_scan_response(struct iwl_mvm *mvm, struct iwl_rx_cmd_buffer *rxb,
			  struct iwl_device_cmd *cmd)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_cmd_response *resp = (void *)pkt->data;

	IWL_DEBUG_SCAN(mvm, "Scan response received. status 0x%x\n",
		       le32_to_cpu(resp->status));
	return 0;
}

int iwl_mvm_rx_scan_complete(struct iwl_mvm *mvm, struct iwl_rx_cmd_buffer *rxb,
			  struct iwl_device_cmd *cmd)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_scan_complete_notif *notif = (void *)pkt->data;

	lockdep_assert_held(&mvm->mutex);

	IWL_DEBUG_SCAN(mvm, "Scan complete: status=0x%x scanned channels=%d\n",
		       notif->status, notif->scanned_channels);

	if (mvm->scan_status == IWL_MVM_SCAN_OS)
		mvm->scan_status = IWL_MVM_SCAN_NONE;
	ieee80211_scan_completed(mvm->hw, notif->status != SCAN_COMP_STATUS_OK);

	iwl_mvm_unref(mvm, IWL_MVM_REF_SCAN);

	return 0;
}

int iwl_mvm_rx_sched_scan_results(struct iwl_mvm *mvm,
				  struct iwl_rx_cmd_buffer *rxb,
				  struct iwl_device_cmd *cmd)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_sched_scan_results *notif = (void *)pkt->data;

	if (notif->client_bitmap & SCAN_CLIENT_SCHED_SCAN) {
		IWL_DEBUG_SCAN(mvm, "Scheduled scan results\n");
		ieee80211_sched_scan_results(mvm->hw);
	}

	return 0;
}

static bool iwl_mvm_scan_abort_notif(struct iwl_notif_wait_data *notif_wait,
				     struct iwl_rx_packet *pkt, void *data)
{
	struct iwl_mvm *mvm =
		container_of(notif_wait, struct iwl_mvm, notif_wait);
	struct iwl_scan_complete_notif *notif;
	u32 *resp;

	switch (pkt->hdr.cmd) {
	case SCAN_ABORT_CMD:
		resp = (void *)pkt->data;
		if (*resp == CAN_ABORT_STATUS) {
			IWL_DEBUG_SCAN(mvm,
				       "Scan can be aborted, wait until completion\n");
			return false;
		}

		/*
		 * If scan cannot be aborted, it means that we had a
		 * SCAN_COMPLETE_NOTIFICATION in the pipe and it called
		 * ieee80211_scan_completed already.
		 */
		IWL_DEBUG_SCAN(mvm, "Scan cannot be aborted, exit now: %d\n",
			       *resp);
		return true;

	case SCAN_COMPLETE_NOTIFICATION:
		notif = (void *)pkt->data;
		IWL_DEBUG_SCAN(mvm, "Scan aborted: status 0x%x\n",
			       notif->status);
		return true;

	default:
		WARN_ON(1);
		return false;
	};
}

int iwl_mvm_cancel_scan(struct iwl_mvm *mvm)
{
	struct iwl_notification_wait wait_scan_abort;
	static const u8 scan_abort_notif[] = { SCAN_ABORT_CMD,
					       SCAN_COMPLETE_NOTIFICATION };
	int ret;

	if (mvm->scan_status == IWL_MVM_SCAN_NONE)
		return 0;

	if (iwl_mvm_is_radio_killed(mvm)) {
		ieee80211_scan_completed(mvm->hw, true);
		iwl_mvm_unref(mvm, IWL_MVM_REF_SCAN);
		mvm->scan_status = IWL_MVM_SCAN_NONE;
		return 0;
	}

	iwl_init_notification_wait(&mvm->notif_wait, &wait_scan_abort,
				   scan_abort_notif,
				   ARRAY_SIZE(scan_abort_notif),
				   iwl_mvm_scan_abort_notif, NULL);

	ret = iwl_mvm_send_cmd_pdu(mvm, SCAN_ABORT_CMD, 0, 0, NULL);
	if (ret) {
		IWL_ERR(mvm, "Couldn't send SCAN_ABORT_CMD: %d\n", ret);
		/* mac80211's state will be cleaned in the nic_restart flow */
		goto out_remove_notif;
	}

	return iwl_wait_notification(&mvm->notif_wait, &wait_scan_abort, HZ);

out_remove_notif:
	iwl_remove_notification(&mvm->notif_wait, &wait_scan_abort);
	return ret;
}

int iwl_mvm_rx_scan_offload_complete_notif(struct iwl_mvm *mvm,
					   struct iwl_rx_cmd_buffer *rxb,
					   struct iwl_device_cmd *cmd)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_scan_offload_complete *scan_notif = (void *)pkt->data;

	/* scan status must be locked for proper checking */
	lockdep_assert_held(&mvm->mutex);

	IWL_DEBUG_SCAN(mvm,
		       "Scheduled scan completed, status %s EBS status %s:%d\n",
		       scan_notif->status == IWL_SCAN_OFFLOAD_COMPLETED ?
		       "completed" : "aborted", scan_notif->ebs_status ==
		       IWL_SCAN_EBS_SUCCESS ? "success" : "failed",
		       scan_notif->ebs_status);


	/* only call mac80211 completion if the stop was initiated by FW */
	if (mvm->scan_status == IWL_MVM_SCAN_SCHED) {
		mvm->scan_status = IWL_MVM_SCAN_NONE;
		ieee80211_sched_scan_stopped(mvm->hw);
	}

	mvm->last_ebs_successful = !scan_notif->ebs_status;

	return 0;
}

static void iwl_scan_offload_build_tx_cmd(struct iwl_mvm *mvm,
					  struct ieee80211_vif *vif,
					  struct ieee80211_sched_scan_ies *ies,
					  enum ieee80211_band band,
					  struct iwl_tx_cmd *cmd,
					  u8 *data)
{
	u16 cmd_len;

	cmd->tx_flags = cpu_to_le32(TX_CMD_FLG_SEQ_CTL);
	cmd->life_time = cpu_to_le32(TX_CMD_LIFE_TIME_INFINITE);
	cmd->sta_id = mvm->aux_sta.sta_id;

	cmd->rate_n_flags = iwl_mvm_scan_rate_n_flags(mvm, band, false);

	cmd_len = iwl_mvm_fill_probe_req((struct ieee80211_mgmt *)data,
					 vif->addr,
					 1, NULL, 0,
					 ies->ie[band], ies->len[band],
					 SCAN_OFFLOAD_PROBE_REQ_SIZE);
	cmd->len = cpu_to_le16(cmd_len);
}

static void iwl_build_scan_cmd(struct iwl_mvm *mvm,
			       struct ieee80211_vif *vif,
			       struct cfg80211_sched_scan_request *req,
			       struct iwl_scan_offload_cmd *scan,
			       struct iwl_mvm_scan_params *params)
{
	scan->channel_count =
		mvm->nvm_data->bands[IEEE80211_BAND_2GHZ].n_channels +
		mvm->nvm_data->bands[IEEE80211_BAND_5GHZ].n_channels;
	scan->quiet_time = cpu_to_le16(IWL_ACTIVE_QUIET_TIME);
	scan->quiet_plcp_th = cpu_to_le16(IWL_PLCP_QUIET_THRESH);
	scan->good_CRC_th = IWL_GOOD_CRC_TH_DEFAULT;
	scan->rx_chain = iwl_mvm_scan_rx_chain(mvm);

	scan->max_out_time = cpu_to_le32(params->max_out_time);
	scan->suspend_time = cpu_to_le32(params->suspend_time);

	scan->filter_flags |= cpu_to_le32(MAC_FILTER_ACCEPT_GRP |
					  MAC_FILTER_IN_BEACON);
	scan->scan_type = cpu_to_le32(SCAN_TYPE_BACKGROUND);
	scan->rep_count = cpu_to_le32(1);

	if (params->passive_fragmented)
		scan->scan_flags |= SCAN_FLAGS_FRAGMENTED_SCAN;
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
					struct iwl_scan_offload_cmd *scan,
					u32 *ssid_bitmap)
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
		scan->direct_scan[i].id = WLAN_EID_SSID;
		scan->direct_scan[i].len = req->match_sets[i].ssid.ssid_len;
		memcpy(scan->direct_scan[i].ssid, req->match_sets[i].ssid.ssid,
		       scan->direct_scan[i].len);
	}

	/* add SSIDs from scan SSID list */
	*ssid_bitmap = 0;
	for (j = 0; j < req->n_ssids && i < PROBE_OPTION_MAX; j++) {
		index = iwl_ssid_exist(req->ssids[j].ssid,
				       req->ssids[j].ssid_len,
				       scan->direct_scan);
		if (index < 0) {
			if (!req->ssids[j].ssid_len)
				continue;
			scan->direct_scan[i].id = WLAN_EID_SSID;
			scan->direct_scan[i].len = req->ssids[j].ssid_len;
			memcpy(scan->direct_scan[i].ssid, req->ssids[j].ssid,
			       scan->direct_scan[i].len);
			*ssid_bitmap |= BIT(i + 1);
			i++;
		} else {
			*ssid_bitmap |= BIT(index + 1);
		}
	}
}

static void iwl_build_channel_cfg(struct iwl_mvm *mvm,
				  struct cfg80211_sched_scan_request *req,
				  struct iwl_scan_channel_cfg *channels,
				  enum ieee80211_band band,
				  int *head, int *tail,
				  u32 ssid_bitmap,
				  struct iwl_mvm_scan_params *params)
{
	struct ieee80211_supported_band *s_band;
	int n_channels = req->n_channels;
	int i, j, index = 0;
	bool partial;

	/*
	 * We have to configure all supported channels, even if we don't want to
	 * scan on them, but we have to send channels in the order that we want
	 * to scan. So add requested channels to head of the list and others to
	 * the end.
	*/
	s_band = &mvm->nvm_data->bands[band];

	for (i = 0; i < s_band->n_channels && *head <= *tail; i++) {
		partial = false;
		for (j = 0; j < n_channels; j++)
			if (s_band->channels[i].center_freq ==
						req->channels[j]->center_freq) {
				index = *head;
				(*head)++;
				/*
				 * Channels that came with the request will be
				 * in partial scan .
				 */
				partial = true;
				break;
			}
		if (!partial) {
			index = *tail;
			(*tail)--;
		}
		channels->channel_number[index] =
			cpu_to_le16(ieee80211_frequency_to_channel(
					s_band->channels[i].center_freq));
		channels->dwell_time[index][0] = params->dwell[band].active;
		channels->dwell_time[index][1] = params->dwell[band].passive;

		channels->iter_count[index] = cpu_to_le16(1);
		channels->iter_interval[index] = 0;

		if (!(s_band->channels[i].flags & IEEE80211_CHAN_NO_IR))
			channels->type[index] |=
				cpu_to_le32(IWL_SCAN_OFFLOAD_CHANNEL_ACTIVE);

		channels->type[index] |=
				cpu_to_le32(IWL_SCAN_OFFLOAD_CHANNEL_FULL);
		if (partial)
			channels->type[index] |=
				cpu_to_le32(IWL_SCAN_OFFLOAD_CHANNEL_PARTIAL);

		if (s_band->channels[i].flags & IEEE80211_CHAN_NO_HT40)
			channels->type[index] |=
				cpu_to_le32(IWL_SCAN_OFFLOAD_CHANNEL_NARROW);

		/* scan for all SSIDs from req->ssids */
		channels->type[index] |= cpu_to_le32(ssid_bitmap);
	}
}

int iwl_mvm_config_sched_scan(struct iwl_mvm *mvm,
			      struct ieee80211_vif *vif,
			      struct cfg80211_sched_scan_request *req,
			      struct ieee80211_sched_scan_ies *ies)
{
	int band_2ghz = mvm->nvm_data->bands[IEEE80211_BAND_2GHZ].n_channels;
	int band_5ghz = mvm->nvm_data->bands[IEEE80211_BAND_5GHZ].n_channels;
	int head = 0;
	int tail = band_2ghz + band_5ghz - 1;
	u32 ssid_bitmap;
	int cmd_len;
	int ret;

	struct iwl_scan_offload_cfg *scan_cfg;
	struct iwl_host_cmd cmd = {
		.id = SCAN_OFFLOAD_CONFIG_CMD,
	};
	struct iwl_mvm_scan_params params = {};

	lockdep_assert_held(&mvm->mutex);

	cmd_len = sizeof(struct iwl_scan_offload_cfg) +
		  2 * SCAN_OFFLOAD_PROBE_REQ_SIZE;

	scan_cfg = kzalloc(cmd_len, GFP_KERNEL);
	if (!scan_cfg)
		return -ENOMEM;

	iwl_mvm_scan_calc_params(mvm, vif, req->n_ssids, &params);
	iwl_build_scan_cmd(mvm, vif, req, &scan_cfg->scan_cmd, &params);
	scan_cfg->scan_cmd.len = cpu_to_le16(cmd_len);

	iwl_scan_offload_build_ssid(req, &scan_cfg->scan_cmd, &ssid_bitmap);
	/* build tx frames for supported bands */
	if (band_2ghz) {
		iwl_scan_offload_build_tx_cmd(mvm, vif, ies,
					      IEEE80211_BAND_2GHZ,
					      &scan_cfg->scan_cmd.tx_cmd[0],
					      scan_cfg->data);
		iwl_build_channel_cfg(mvm, req, &scan_cfg->channel_cfg,
				      IEEE80211_BAND_2GHZ, &head, &tail,
				      ssid_bitmap, &params);
	}
	if (band_5ghz) {
		iwl_scan_offload_build_tx_cmd(mvm, vif, ies,
					      IEEE80211_BAND_5GHZ,
					      &scan_cfg->scan_cmd.tx_cmd[1],
					      scan_cfg->data +
						SCAN_OFFLOAD_PROBE_REQ_SIZE);
		iwl_build_channel_cfg(mvm, req, &scan_cfg->channel_cfg,
				      IEEE80211_BAND_5GHZ, &head, &tail,
				      ssid_bitmap, &params);
	}

	cmd.data[0] = scan_cfg;
	cmd.len[0] = cmd_len;
	cmd.dataflags[0] = IWL_HCMD_DFL_NOCOPY;

	IWL_DEBUG_SCAN(mvm, "Sending scheduled scan config\n");

	ret = iwl_mvm_send_cmd(mvm, &cmd);
	kfree(scan_cfg);
	return ret;
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

int iwl_mvm_sched_scan_start(struct iwl_mvm *mvm,
			     struct cfg80211_sched_scan_request *req)
{
	struct iwl_scan_offload_req scan_req = {
		.watchdog = IWL_SCHED_SCAN_WATCHDOG,

		.schedule_line[0].iterations = IWL_FAST_SCHED_SCAN_ITERATIONS,
		.schedule_line[0].delay = req->interval / 1000,
		.schedule_line[0].full_scan_mul = 1,

		.schedule_line[1].iterations = 0xff,
		.schedule_line[1].delay = req->interval / 1000,
		.schedule_line[1].full_scan_mul = IWL_FULL_SCAN_MULTIPLIER,
	};

	if (req->n_match_sets && req->match_sets[0].ssid.ssid_len) {
		IWL_DEBUG_SCAN(mvm,
			       "Sending scheduled scan with filtering, filter len %d\n",
			       req->n_match_sets);
	} else {
		IWL_DEBUG_SCAN(mvm,
			       "Sending Scheduled scan without filtering\n");
		scan_req.flags |= cpu_to_le16(IWL_SCAN_OFFLOAD_FLAG_PASS_ALL);
	}

	if (mvm->last_ebs_successful &&
	    mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_EBS_SUPPORT)
		scan_req.flags |=
			cpu_to_le16(IWL_SCAN_OFFLOAD_FLAG_EBS_ACCURATE_MODE);

	return iwl_mvm_send_cmd_pdu(mvm, SCAN_OFFLOAD_REQUEST_CMD, 0,
				    sizeof(scan_req), &scan_req);
}

static int iwl_mvm_send_sched_scan_abort(struct iwl_mvm *mvm)
{
	int ret;
	struct iwl_host_cmd cmd = {
		.id = SCAN_OFFLOAD_ABORT_CMD,
	};
	u32 status;

	/* Exit instantly with error when device is not ready
	 * to receive scan abort command or it does not perform
	 * scheduled scan currently */
	if (mvm->scan_status != IWL_MVM_SCAN_SCHED)
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

int iwl_mvm_sched_scan_stop(struct iwl_mvm *mvm, bool notify)
{
	int ret;
	struct iwl_notification_wait wait_scan_done;
	static const u8 scan_done_notif[] = { SCAN_OFFLOAD_COMPLETE, };

	lockdep_assert_held(&mvm->mutex);

	if (mvm->scan_status != IWL_MVM_SCAN_SCHED) {
		IWL_DEBUG_SCAN(mvm, "No offloaded scan to stop\n");
		return 0;
	}

	iwl_init_notification_wait(&mvm->notif_wait, &wait_scan_done,
				   scan_done_notif,
				   ARRAY_SIZE(scan_done_notif),
				   NULL, NULL);

	ret = iwl_mvm_send_sched_scan_abort(mvm);
	if (ret) {
		IWL_DEBUG_SCAN(mvm, "Send stop offload scan failed %d\n", ret);
		iwl_remove_notification(&mvm->notif_wait, &wait_scan_done);
		return ret;
	}

	IWL_DEBUG_SCAN(mvm, "Successfully sent stop offload scan\n");

	ret = iwl_wait_notification(&mvm->notif_wait, &wait_scan_done, 1 * HZ);
	if (ret)
		return ret;

	/*
	 * Clear the scan status so the next scan requests will succeed. This
	 * also ensures the Rx handler doesn't do anything, as the scan was
	 * stopped from above.
	 */
	mvm->scan_status = IWL_MVM_SCAN_NONE;

	if (notify)
		ieee80211_sched_scan_stopped(mvm->hw);

	return 0;
}
