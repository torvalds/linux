/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2015 - 2017 Intel Deutschland GmbH
 * Copyright (C) 2018 Intel Corporation
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
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 * Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2015 - 2017 Intel Deutschland GmbH
 * Copyright (C) 2018 Intel Corporation
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
#include <net/cfg80211.h>
#include <linux/etherdevice.h>
#include "mvm.h"
#include "constants.h"

static int
iwl_mvm_ftm_responder_cmd(struct iwl_mvm *mvm,
			  struct ieee80211_vif *vif,
			  struct cfg80211_chan_def *chandef)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_tof_responder_config_cmd cmd = {
		.channel_num = chandef->chan->hw_value,
		.cmd_valid_fields =
			cpu_to_le32(IWL_TOF_RESPONDER_CMD_VALID_CHAN_INFO |
				    IWL_TOF_RESPONDER_CMD_VALID_BSSID |
				    IWL_TOF_RESPONDER_CMD_VALID_STA_ID),
		.sta_id = mvmvif->bcast_sta.sta_id,
	};

	lockdep_assert_held(&mvm->mutex);

	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_20_NOHT:
		cmd.bandwidth = IWL_TOF_BW_20_LEGACY;
		break;
	case NL80211_CHAN_WIDTH_20:
		cmd.bandwidth = IWL_TOF_BW_20_HT;
		break;
	case NL80211_CHAN_WIDTH_40:
		cmd.bandwidth = IWL_TOF_BW_40;
		cmd.ctrl_ch_position = iwl_mvm_get_ctrl_pos(chandef);
		break;
	case NL80211_CHAN_WIDTH_80:
		cmd.bandwidth = IWL_TOF_BW_80;
		cmd.ctrl_ch_position = iwl_mvm_get_ctrl_pos(chandef);
		break;
	default:
		WARN_ON(1);
		return -EINVAL;
	}

	memcpy(cmd.bssid, vif->addr, ETH_ALEN);

	return iwl_mvm_send_cmd_pdu(mvm, iwl_cmd_id(TOF_RESPONDER_CONFIG_CMD,
						    LOCATION_GROUP, 0),
				    0, sizeof(cmd), &cmd);
}

static int
iwl_mvm_ftm_responder_dyn_cfg_cmd(struct iwl_mvm *mvm,
				  struct ieee80211_vif *vif,
				  struct ieee80211_ftm_responder_params *params)
{
	struct iwl_tof_responder_dyn_config_cmd cmd = {
		.lci_len = cpu_to_le32(params->lci_len + 2),
		.civic_len = cpu_to_le32(params->civicloc_len + 2),
	};
	u8 data[IWL_LCI_CIVIC_IE_MAX_SIZE] = {0};
	struct iwl_host_cmd hcmd = {
		.id = iwl_cmd_id(TOF_RESPONDER_DYN_CONFIG_CMD,
				 LOCATION_GROUP, 0),
		.data[0] = &cmd,
		.len[0] = sizeof(cmd),
		.data[1] = &data,
		/* .len[1] set later */
		/* may not be able to DMA from stack */
		.dataflags[1] = IWL_HCMD_DFL_DUP,
	};
	u32 aligned_lci_len = ALIGN(params->lci_len + 2, 4);
	u32 aligned_civicloc_len = ALIGN(params->civicloc_len + 2, 4);
	u8 *pos = data;

	lockdep_assert_held(&mvm->mutex);

	if (aligned_lci_len + aligned_civicloc_len > sizeof(data)) {
		IWL_ERR(mvm, "LCI/civicloc data too big (%zd + %zd)\n",
			params->lci_len, params->civicloc_len);
		return -ENOBUFS;
	}

	pos[0] = WLAN_EID_MEASURE_REPORT;
	pos[1] = params->lci_len;
	memcpy(pos + 2, params->lci, params->lci_len);

	pos += aligned_lci_len;
	pos[0] = WLAN_EID_MEASURE_REPORT;
	pos[1] = params->civicloc_len;
	memcpy(pos + 2, params->civicloc, params->civicloc_len);

	hcmd.len[1] = aligned_lci_len + aligned_civicloc_len;

	return iwl_mvm_send_cmd(mvm, &hcmd);
}

int iwl_mvm_ftm_start_responder(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct ieee80211_ftm_responder_params *params;
	struct ieee80211_chanctx_conf ctx, *pctx;
	u16 *phy_ctxt_id;
	struct iwl_mvm_phy_ctxt *phy_ctxt;
	int ret;

	params = vif->bss_conf.ftmr_params;

	lockdep_assert_held(&mvm->mutex);

	if (WARN_ON_ONCE(!vif->bss_conf.ftm_responder))
		return -EINVAL;

	if (vif->p2p || vif->type != NL80211_IFTYPE_AP ||
	    !mvmvif->ap_ibss_active) {
		IWL_ERR(mvm, "Cannot start responder, not in AP mode\n");
		return -EIO;
	}

	rcu_read_lock();
	pctx = rcu_dereference(vif->chanctx_conf);
	/* Copy the ctx to unlock the rcu and send the phy ctxt. We don't care
	 * about changes in the ctx after releasing the lock because the driver
	 * is still protected by the mutex. */
	ctx = *pctx;
	phy_ctxt_id  = (u16 *)pctx->drv_priv;
	rcu_read_unlock();

	phy_ctxt = &mvm->phy_ctxts[*phy_ctxt_id];
	ret = iwl_mvm_phy_ctxt_changed(mvm, phy_ctxt, &ctx.def,
				       ctx.rx_chains_static,
				       ctx.rx_chains_dynamic);
	if (ret)
		return ret;

	ret = iwl_mvm_ftm_responder_cmd(mvm, vif, &ctx.def);
	if (ret)
		return ret;

	if (params)
		ret = iwl_mvm_ftm_responder_dyn_cfg_cmd(mvm, vif, params);

	return ret;
}

void iwl_mvm_ftm_restart_responder(struct iwl_mvm *mvm,
				   struct ieee80211_vif *vif)
{
	if (!vif->bss_conf.ftm_responder)
		return;

	iwl_mvm_ftm_start_responder(mvm, vif);
}

void iwl_mvm_ftm_responder_stats(struct iwl_mvm *mvm,
				 struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_ftm_responder_stats *resp = (void *)pkt->data;
	struct cfg80211_ftm_responder_stats *stats = &mvm->ftm_resp_stats;
	u32 flags = le32_to_cpu(resp->flags);

	if (resp->success_ftm == resp->ftm_per_burst)
		stats->success_num++;
	else if (resp->success_ftm >= 2)
		stats->partial_num++;
	else
		stats->failed_num++;

	if ((flags & FTM_RESP_STAT_ASAP_REQ) &&
	    (flags & FTM_RESP_STAT_ASAP_RESP))
		stats->asap_num++;

	if (flags & FTM_RESP_STAT_NON_ASAP_RESP)
		stats->non_asap_num++;

	stats->total_duration_ms += le32_to_cpu(resp->duration) / USEC_PER_MSEC;

	if (flags & FTM_RESP_STAT_TRIGGER_UNKNOWN)
		stats->unknown_triggers_num++;

	if (flags & FTM_RESP_STAT_DUP)
		stats->reschedule_requests_num++;

	if (flags & FTM_RESP_STAT_NON_ASAP_OUT_WIN)
		stats->out_of_window_triggers_num++;
}
