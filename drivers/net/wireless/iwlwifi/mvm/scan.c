/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2013 Intel Corporation. All rights reserved.
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
 * Copyright(c) 2012 - 2013 Intel Corporation. All rights reserved.
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

static inline __le16 iwl_mvm_scan_rx_chain(struct iwl_mvm *mvm)
{
	u16 rx_chain;
	u8 rx_ant = iwl_fw_valid_rx_ant(mvm->fw);

	rx_chain = rx_ant << PHY_RX_CHAIN_VALID_POS;
	rx_chain |= rx_ant << PHY_RX_CHAIN_FORCE_MIMO_SEL_POS;
	rx_chain |= rx_ant << PHY_RX_CHAIN_FORCE_SEL_POS;
	rx_chain |= 0x1 << PHY_RX_CHAIN_DRIVER_FORCE_POS;
	return cpu_to_le16(rx_chain);
}

static inline __le32 iwl_mvm_scan_max_out_time(struct ieee80211_vif *vif)
{
	if (vif->bss_conf.assoc)
		return cpu_to_le32(200 * 1024);
	else
		return 0;
}

static inline __le32 iwl_mvm_scan_suspend_time(struct ieee80211_vif *vif)
{
	if (vif->bss_conf.assoc)
		return cpu_to_le32(vif->bss_conf.beacon_int);
	else
		return 0;
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
		iwl_mvm_next_antenna(mvm, iwl_fw_valid_tx_ant(mvm->fw),
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
				    struct cfg80211_scan_request *req)
{
	int fw_idx, req_idx;

	fw_idx = 0;
	for (req_idx = req->n_ssids - 1; req_idx > 0; req_idx--) {
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
 * one for each SSID, and set the active bit (first).
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
				       struct cfg80211_scan_request *req)
{
	u16 passive_dwell = iwl_mvm_get_passive_dwell(req->channels[0]->band);
	u16 active_dwell = iwl_mvm_get_active_dwell(req->channels[0]->band,
						    req->n_ssids);
	struct iwl_scan_channel *chan = (struct iwl_scan_channel *)
		(cmd->data + le16_to_cpu(cmd->tx_cmd.len));
	int i;
	__le32 chan_type_value;

	if (req->n_ssids > 0)
		chan_type_value = cpu_to_le32(BIT(req->n_ssids + 1) - 1);
	else
		chan_type_value = SCAN_CHANNEL_TYPE_PASSIVE;

	for (i = 0; i < cmd->channel_count; i++) {
		chan->channel = cpu_to_le16(req->channels[i]->hw_value);
		if (req->channels[i]->flags & IEEE80211_CHAN_PASSIVE_SCAN)
			chan->type = SCAN_CHANNEL_TYPE_PASSIVE;
		else
			chan->type = chan_type_value;
		chan->active_dwell = cpu_to_le16(active_dwell);
		chan->passive_dwell = cpu_to_le16(passive_dwell);
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

int iwl_mvm_scan_request(struct iwl_mvm *mvm,
			 struct ieee80211_vif *vif,
			 struct cfg80211_scan_request *req)
{
	struct iwl_host_cmd hcmd = {
		.id = SCAN_REQUEST_CMD,
		.len = { 0, },
		.data = { mvm->scan_cmd, },
		.flags = CMD_SYNC,
		.dataflags = { IWL_HCMD_DFL_NOCOPY, },
	};
	struct iwl_scan_cmd *cmd = mvm->scan_cmd;
	int ret;
	u32 status;
	int ssid_len = 0;
	u8 *ssid = NULL;

	lockdep_assert_held(&mvm->mutex);
	BUG_ON(mvm->scan_cmd == NULL);

	IWL_DEBUG_SCAN(mvm, "Handling mac80211 scan request\n");
	mvm->scan_status = IWL_MVM_SCAN_OS;
	memset(cmd, 0, sizeof(struct iwl_scan_cmd) +
	       mvm->fw->ucode_capa.max_probe_length +
	       (MAX_NUM_SCAN_CHANNELS * sizeof(struct iwl_scan_channel)));

	cmd->channel_count = (u8)req->n_channels;
	cmd->quiet_time = cpu_to_le16(IWL_ACTIVE_QUIET_TIME);
	cmd->quiet_plcp_th = cpu_to_le16(IWL_PLCP_QUIET_THRESH);
	cmd->rxchain_sel_flags = iwl_mvm_scan_rx_chain(mvm);
	cmd->max_out_time = iwl_mvm_scan_max_out_time(vif);
	cmd->suspend_time = iwl_mvm_scan_suspend_time(vif);
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
		ssid = req->ssids[0].ssid;
		ssid_len = req->ssids[0].ssid_len;
	} else {
		cmd->passive2active = 0;
	}

	iwl_mvm_scan_fill_ssids(cmd, req);

	cmd->tx_cmd.tx_flags = cpu_to_le32(TX_CMD_FLG_SEQ_CTL);
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

	iwl_mvm_scan_fill_channels(cmd, req);

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

	IWL_DEBUG_SCAN(mvm, "Scan complete: status=0x%x scanned channels=%d\n",
		       notif->status, notif->scanned_channels);

	mvm->scan_status = IWL_MVM_SCAN_NONE;
	ieee80211_scan_completed(mvm->hw, notif->status != SCAN_COMP_STATUS_OK);

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

void iwl_mvm_cancel_scan(struct iwl_mvm *mvm)
{
	struct iwl_notification_wait wait_scan_abort;
	static const u8 scan_abort_notif[] = { SCAN_ABORT_CMD,
					       SCAN_COMPLETE_NOTIFICATION };
	int ret;

	iwl_init_notification_wait(&mvm->notif_wait, &wait_scan_abort,
				   scan_abort_notif,
				   ARRAY_SIZE(scan_abort_notif),
				   iwl_mvm_scan_abort_notif, NULL);

	ret = iwl_mvm_send_cmd_pdu(mvm, SCAN_ABORT_CMD, CMD_SYNC, 0, NULL);
	if (ret) {
		IWL_ERR(mvm, "Couldn't send SCAN_ABORT_CMD: %d\n", ret);
		goto out_remove_notif;
	}

	ret = iwl_wait_notification(&mvm->notif_wait, &wait_scan_abort, 1 * HZ);
	if (ret)
		IWL_ERR(mvm, "%s - failed on timeout\n", __func__);

	return;

out_remove_notif:
	iwl_remove_notification(&mvm->notif_wait, &wait_scan_abort);
}
