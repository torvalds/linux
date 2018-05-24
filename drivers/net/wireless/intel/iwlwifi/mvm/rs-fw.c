/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2017        Intel Deutschland GmbH
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
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2017        Intel Deutschland GmbH
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
#include "rs.h"
#include "fw-api.h"
#include "sta.h"
#include "iwl-op-mode.h"
#include "mvm.h"

static u8 rs_fw_bw_from_sta_bw(struct ieee80211_sta *sta)
{
	switch (sta->bandwidth) {
	case IEEE80211_STA_RX_BW_160:
		return IWL_TLC_MNG_MAX_CH_WIDTH_160MHZ;
	case IEEE80211_STA_RX_BW_80:
		return IWL_TLC_MNG_MAX_CH_WIDTH_80MHZ;
	case IEEE80211_STA_RX_BW_40:
		return IWL_TLC_MNG_MAX_CH_WIDTH_40MHZ;
	case IEEE80211_STA_RX_BW_20:
	default:
		return IWL_TLC_MNG_MAX_CH_WIDTH_20MHZ;
	}
}

static u8 rs_fw_set_active_chains(u8 chains)
{
	u8 fw_chains = 0;

	if (chains & ANT_A)
		fw_chains |= IWL_TLC_MNG_CHAIN_A_MSK;
	if (chains & ANT_B)
		fw_chains |= IWL_TLC_MNG_CHAIN_B_MSK;
	if (chains & ANT_C)
		fw_chains |= IWL_TLC_MNG_CHAIN_C_MSK;

	return fw_chains;
}

static u8 rs_fw_sgi_cw_support(struct ieee80211_sta *sta)
{
	struct ieee80211_sta_ht_cap *ht_cap = &sta->ht_cap;
	struct ieee80211_sta_vht_cap *vht_cap = &sta->vht_cap;
	u8 supp = 0;

	if (ht_cap->cap & IEEE80211_HT_CAP_SGI_20)
		supp |= IWL_TLC_MNG_SGI_20MHZ_MSK;
	if (ht_cap->cap & IEEE80211_HT_CAP_SGI_40)
		supp |= IWL_TLC_MNG_SGI_40MHZ_MSK;
	if (vht_cap->cap & IEEE80211_VHT_CAP_SHORT_GI_80)
		supp |= IWL_TLC_MNG_SGI_80MHZ_MSK;
	if (vht_cap->cap & IEEE80211_VHT_CAP_SHORT_GI_160)
		supp |= IWL_TLC_MNG_SGI_160MHZ_MSK;

	return supp;
}

static u16 rs_fw_set_config_flags(struct iwl_mvm *mvm,
				  struct ieee80211_sta *sta)
{
	struct ieee80211_sta_ht_cap *ht_cap = &sta->ht_cap;
	struct ieee80211_sta_vht_cap *vht_cap = &sta->vht_cap;
	bool vht_ena = vht_cap && vht_cap->vht_supported;
	u16 flags = IWL_TLC_MNG_CFG_FLAGS_CCK_MSK |
		    IWL_TLC_MNG_CFG_FLAGS_DCM_MSK |
		    IWL_TLC_MNG_CFG_FLAGS_DD_MSK;

	if (mvm->cfg->ht_params->stbc &&
	    (num_of_ant(iwl_mvm_get_valid_tx_ant(mvm)) > 1) &&
	    ((ht_cap && (ht_cap->cap & IEEE80211_HT_CAP_RX_STBC)) ||
	     (vht_ena && (vht_cap->cap & IEEE80211_VHT_CAP_RXSTBC_MASK))))
		flags |= IWL_TLC_MNG_CFG_FLAGS_STBC_MSK;

	if (mvm->cfg->ht_params->ldpc &&
	    ((ht_cap && (ht_cap->cap & IEEE80211_HT_CAP_LDPC_CODING)) ||
	     (vht_ena && (vht_cap->cap & IEEE80211_VHT_CAP_RXLDPC))))
		flags |= IWL_TLC_MNG_CFG_FLAGS_LDPC_MSK;

	if (fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_BEAMFORMER) &&
	    (num_of_ant(iwl_mvm_get_valid_tx_ant(mvm)) > 1) &&
	    (vht_cap->cap & IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE))
		flags |= IWL_TLC_MNG_CFG_FLAGS_BF_MSK;

	return flags;
}

static
int rs_fw_vht_highest_rx_mcs_index(struct ieee80211_sta_vht_cap *vht_cap,
				   int nss)
{
	u16 rx_mcs = le16_to_cpu(vht_cap->vht_mcs.rx_mcs_map) &
		(0x3 << (2 * (nss - 1)));
	rx_mcs >>= (2 * (nss - 1));

	switch (rx_mcs) {
	case IEEE80211_VHT_MCS_SUPPORT_0_7:
		return IWL_TLC_MNG_HT_RATE_MCS7;
	case IEEE80211_VHT_MCS_SUPPORT_0_8:
		return IWL_TLC_MNG_HT_RATE_MCS8;
	case IEEE80211_VHT_MCS_SUPPORT_0_9:
		return IWL_TLC_MNG_HT_RATE_MCS9;
	default:
		WARN_ON_ONCE(1);
		break;
	}

	return 0;
}

static void rs_fw_vht_set_enabled_rates(struct ieee80211_sta *sta,
					struct ieee80211_sta_vht_cap *vht_cap,
					struct iwl_tlc_config_cmd *cmd)
{
	u16 supp;
	int i, highest_mcs;

	for (i = 0; i < sta->rx_nss; i++) {
		if (i == MAX_RS_ANT_NUM)
			break;

		highest_mcs = rs_fw_vht_highest_rx_mcs_index(vht_cap, i + 1);
		if (!highest_mcs)
			continue;

		supp = BIT(highest_mcs + 1) - 1;
		if (sta->bandwidth == IEEE80211_STA_RX_BW_20)
			supp &= ~BIT(IWL_TLC_MNG_HT_RATE_MCS9);

		cmd->ht_supp_rates[i] = cpu_to_le16(supp);
	}
}

static void rs_fw_set_supp_rates(struct ieee80211_sta *sta,
				 struct ieee80211_supported_band *sband,
				 struct iwl_tlc_config_cmd *cmd)
{
	int i;
	unsigned long tmp;
	unsigned long supp; /* must be unsigned long for for_each_set_bit */
	struct ieee80211_sta_ht_cap *ht_cap = &sta->ht_cap;
	struct ieee80211_sta_vht_cap *vht_cap = &sta->vht_cap;

	/* non HT rates */
	supp = 0;
	tmp = sta->supp_rates[sband->band];
	for_each_set_bit(i, &tmp, BITS_PER_LONG)
		supp |= BIT(sband->bitrates[i].hw_value);

	cmd->non_ht_supp_rates = cpu_to_le16(supp);
	cmd->mode = IWL_TLC_MNG_MODE_NON_HT;

	/* HT/VHT rates */
	if (vht_cap && vht_cap->vht_supported) {
		cmd->mode = IWL_TLC_MNG_MODE_VHT;
		rs_fw_vht_set_enabled_rates(sta, vht_cap, cmd);
	} else if (ht_cap && ht_cap->ht_supported) {
		cmd->mode = IWL_TLC_MNG_MODE_HT;
		cmd->ht_supp_rates[0] = cpu_to_le16(ht_cap->mcs.rx_mask[0]);
		cmd->ht_supp_rates[1] = cpu_to_le16(ht_cap->mcs.rx_mask[1]);
	}
}

static void rs_fw_tlc_mng_notif_req_config(struct iwl_mvm *mvm, u8 sta_id)
{
	u32 cmd_id = iwl_cmd_id(TLC_MNG_NOTIF_REQ_CMD, DATA_PATH_GROUP, 0);
	struct iwl_tlc_notif_req_config_cmd cfg_cmd = {
		.sta_id = sta_id,
		.flags = cpu_to_le16(IWL_TLC_NOTIF_INIT_RATE_MSK),
		.interval = cpu_to_le16(IWL_TLC_NOTIF_REQ_INTERVAL),
	};
	int ret;

	ret = iwl_mvm_send_cmd_pdu(mvm, cmd_id, 0, sizeof(cfg_cmd), &cfg_cmd);
	if (ret)
		IWL_ERR(mvm, "Failed to send TLC notif request (%d)\n", ret);
}

void iwl_mvm_tlc_update_notif(struct iwl_mvm *mvm, struct iwl_rx_packet *pkt)
{
	struct iwl_tlc_update_notif *notif;
	struct iwl_mvm_sta *mvmsta;
	struct iwl_lq_sta_rs_fw *lq_sta;

	rcu_read_lock();

	notif = (void *)pkt->data;
	mvmsta = iwl_mvm_sta_from_staid_rcu(mvm, notif->sta_id);

	if (!mvmsta) {
		IWL_ERR(mvm, "Invalid sta id (%d) in FW TLC notification\n",
			notif->sta_id);
		goto out;
	}

	lq_sta = &mvmsta->lq_sta.rs_fw;

	if (le16_to_cpu(notif->flags) & IWL_TLC_NOTIF_INIT_RATE_MSK) {
		lq_sta->last_rate_n_flags =
			le32_to_cpu(notif->values[IWL_TLC_NOTIF_INIT_RATE_POS]);
		IWL_DEBUG_RATE(mvm, "new rate_n_flags: 0x%X\n",
			       lq_sta->last_rate_n_flags);
	}
out:
	rcu_read_unlock();
}

void rs_fw_rate_init(struct iwl_mvm *mvm, struct ieee80211_sta *sta,
		     enum nl80211_band band)
{
	struct ieee80211_hw *hw = mvm->hw;
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
	struct iwl_lq_sta_rs_fw *lq_sta = &mvmsta->lq_sta.rs_fw;
	u32 cmd_id = iwl_cmd_id(TLC_MNG_CONFIG_CMD, DATA_PATH_GROUP, 0);
	struct ieee80211_supported_band *sband;
	struct iwl_tlc_config_cmd cfg_cmd = {
		.sta_id = mvmsta->sta_id,
		.max_supp_ch_width = rs_fw_bw_from_sta_bw(sta),
		.flags = cpu_to_le16(rs_fw_set_config_flags(mvm, sta)),
		.chains = rs_fw_set_active_chains(iwl_mvm_get_valid_tx_ant(mvm)),
		.max_supp_ss = sta->rx_nss,
		.max_ampdu_cnt = cpu_to_le32(mvmsta->max_agg_bufsize),
		.sgi_ch_width_supp = rs_fw_sgi_cw_support(sta),
	};
	int ret;

	memset(lq_sta, 0, offsetof(typeof(*lq_sta), pers));

#ifdef CONFIG_IWLWIFI_DEBUGFS
	iwl_mvm_reset_frame_stats(mvm);
#endif
	sband = hw->wiphy->bands[band];
	rs_fw_set_supp_rates(sta, sband, &cfg_cmd);

	ret = iwl_mvm_send_cmd_pdu(mvm, cmd_id, 0, sizeof(cfg_cmd), &cfg_cmd);
	if (ret)
		IWL_ERR(mvm, "Failed to send rate scale config (%d)\n", ret);

	rs_fw_tlc_mng_notif_req_config(mvm, cfg_cmd.sta_id);
}

int rs_fw_tx_protection(struct iwl_mvm *mvm, struct iwl_mvm_sta *mvmsta,
			bool enable)
{
	/* TODO: need to introduce a new FW cmd since LQ cmd is not relevant */
	IWL_DEBUG_RATE(mvm, "tx protection - not implemented yet.\n");
	return 0;
}

void iwl_mvm_rs_add_sta(struct iwl_mvm *mvm, struct iwl_mvm_sta *mvmsta)
{
	struct iwl_lq_sta_rs_fw *lq_sta = &mvmsta->lq_sta.rs_fw;

	IWL_DEBUG_RATE(mvm, "create station rate scale window\n");

	lq_sta->pers.drv = mvm;
	lq_sta->pers.sta_id = mvmsta->sta_id;
	lq_sta->pers.chains = 0;
	memset(lq_sta->pers.chain_signal, 0, sizeof(lq_sta->pers.chain_signal));
	lq_sta->pers.last_rssi = S8_MIN;
	lq_sta->last_rate_n_flags = 0;

#ifdef CONFIG_MAC80211_DEBUGFS
	lq_sta->pers.dbg_fixed_rate = 0;
#endif
}
