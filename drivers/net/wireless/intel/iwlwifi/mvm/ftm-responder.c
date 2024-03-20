// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2015-2017 Intel Deutschland GmbH
 * Copyright (C) 2018-2023 Intel Corporation
 */
#include <net/cfg80211.h>
#include <linux/etherdevice.h>
#include "mvm.h"
#include "constants.h"

struct iwl_mvm_pasn_sta {
	struct list_head list;
	struct iwl_mvm_int_sta int_sta;
	u8 addr[ETH_ALEN];
};

struct iwl_mvm_pasn_hltk_data {
	u8 *addr;
	u8 cipher;
	u8 *hltk;
};

static int iwl_mvm_ftm_responder_set_bw_v1(struct cfg80211_chan_def *chandef,
					   u8 *bw, u8 *ctrl_ch_position)
{
	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_20_NOHT:
		*bw = IWL_TOF_BW_20_LEGACY;
		break;
	case NL80211_CHAN_WIDTH_20:
		*bw = IWL_TOF_BW_20_HT;
		break;
	case NL80211_CHAN_WIDTH_40:
		*bw = IWL_TOF_BW_40;
		*ctrl_ch_position = iwl_mvm_get_ctrl_pos(chandef);
		break;
	case NL80211_CHAN_WIDTH_80:
		*bw = IWL_TOF_BW_80;
		*ctrl_ch_position = iwl_mvm_get_ctrl_pos(chandef);
		break;
	default:
		return -ENOTSUPP;
	}

	return 0;
}

static int iwl_mvm_ftm_responder_set_bw_v2(struct cfg80211_chan_def *chandef,
					   u8 *format_bw, u8 *ctrl_ch_position,
					   u8 cmd_ver)
{
	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_20_NOHT:
		*format_bw = IWL_LOCATION_FRAME_FORMAT_LEGACY;
		*format_bw |= IWL_LOCATION_BW_20MHZ << LOCATION_BW_POS;
		break;
	case NL80211_CHAN_WIDTH_20:
		*format_bw = IWL_LOCATION_FRAME_FORMAT_HT;
		*format_bw |= IWL_LOCATION_BW_20MHZ << LOCATION_BW_POS;
		break;
	case NL80211_CHAN_WIDTH_40:
		*format_bw = IWL_LOCATION_FRAME_FORMAT_HT;
		*format_bw |= IWL_LOCATION_BW_40MHZ << LOCATION_BW_POS;
		*ctrl_ch_position = iwl_mvm_get_ctrl_pos(chandef);
		break;
	case NL80211_CHAN_WIDTH_80:
		*format_bw = IWL_LOCATION_FRAME_FORMAT_VHT;
		*format_bw |= IWL_LOCATION_BW_80MHZ << LOCATION_BW_POS;
		*ctrl_ch_position = iwl_mvm_get_ctrl_pos(chandef);
		break;
	case NL80211_CHAN_WIDTH_160:
		if (cmd_ver >= 9) {
			*format_bw = IWL_LOCATION_FRAME_FORMAT_HE;
			*format_bw |= IWL_LOCATION_BW_160MHZ << LOCATION_BW_POS;
			*ctrl_ch_position = iwl_mvm_get_ctrl_pos(chandef);
			break;
		}
		fallthrough;
	default:
		return -ENOTSUPP;
	}

	return 0;
}

static void
iwl_mvm_ftm_responder_set_ndp(struct iwl_mvm *mvm,
			      struct iwl_tof_responder_config_cmd_v9 *cmd)
{
	/* Up to 2 R2I STS are allowed on the responder */
	u32 r2i_max_sts = IWL_MVM_FTM_R2I_MAX_STS < 2 ?
		IWL_MVM_FTM_R2I_MAX_STS : 1;

	cmd->r2i_ndp_params = IWL_MVM_FTM_R2I_MAX_REP |
		(r2i_max_sts << IWL_RESPONDER_STS_POS) |
		(IWL_MVM_FTM_R2I_MAX_TOTAL_LTF << IWL_RESPONDER_TOTAL_LTF_POS);
	cmd->i2r_ndp_params = IWL_MVM_FTM_I2R_MAX_REP |
		(IWL_MVM_FTM_I2R_MAX_STS << IWL_RESPONDER_STS_POS) |
		(IWL_MVM_FTM_I2R_MAX_TOTAL_LTF << IWL_RESPONDER_TOTAL_LTF_POS);
	cmd->cmd_valid_fields |=
		cpu_to_le32(IWL_TOF_RESPONDER_CMD_VALID_NDP_PARAMS);
}

static int
iwl_mvm_ftm_responder_cmd(struct iwl_mvm *mvm,
			  struct ieee80211_vif *vif,
			  struct cfg80211_chan_def *chandef,
			  struct ieee80211_bss_conf *link_conf)
{
	u32 cmd_id = WIDE_ID(LOCATION_GROUP, TOF_RESPONDER_CONFIG_CMD);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	/*
	 * The command structure is the same for versions 6, 7 and 8 (only the
	 * field interpretation is different), so the same struct can be use
	 * for all cases.
	 */
	struct iwl_tof_responder_config_cmd_v9 cmd = {
		.channel_num = chandef->chan->hw_value,
		.cmd_valid_fields =
			cpu_to_le32(IWL_TOF_RESPONDER_CMD_VALID_CHAN_INFO |
				    IWL_TOF_RESPONDER_CMD_VALID_BSSID |
				    IWL_TOF_RESPONDER_CMD_VALID_STA_ID),
		.sta_id = mvmvif->link[link_conf->link_id]->bcast_sta.sta_id,
	};
	u8 cmd_ver = iwl_fw_lookup_cmd_ver(mvm->fw, cmd_id, 6);
	int err;
	int cmd_size;

	lockdep_assert_held(&mvm->mutex);

	/* Use a default of bss_color=1 for now */
	if (cmd_ver == 9) {
		cmd.cmd_valid_fields |=
			cpu_to_le32(IWL_TOF_RESPONDER_CMD_VALID_BSS_COLOR |
				    IWL_TOF_RESPONDER_CMD_VALID_MIN_MAX_TIME_BETWEEN_MSR);
		cmd.bss_color = 1;
		cmd.min_time_between_msr =
			cpu_to_le16(IWL_MVM_FTM_NON_TB_MIN_TIME_BETWEEN_MSR);
		cmd.max_time_between_msr =
			cpu_to_le16(IWL_MVM_FTM_NON_TB_MAX_TIME_BETWEEN_MSR);
		cmd_size = sizeof(struct iwl_tof_responder_config_cmd_v9);
	} else {
		/* All versions up to version 8 have the same size */
		cmd_size = sizeof(struct iwl_tof_responder_config_cmd_v8);
	}

	if (cmd_ver >= 8)
		iwl_mvm_ftm_responder_set_ndp(mvm, &cmd);

	if (cmd_ver >= 7)
		err = iwl_mvm_ftm_responder_set_bw_v2(chandef, &cmd.format_bw,
						      &cmd.ctrl_ch_position,
						      cmd_ver);
	else
		err = iwl_mvm_ftm_responder_set_bw_v1(chandef, &cmd.format_bw,
						      &cmd.ctrl_ch_position);

	if (err) {
		IWL_ERR(mvm, "Failed to set responder bandwidth\n");
		return err;
	}

	memcpy(cmd.bssid, vif->addr, ETH_ALEN);

	return iwl_mvm_send_cmd_pdu(mvm, cmd_id, 0, cmd_size, &cmd);
}

static int
iwl_mvm_ftm_responder_dyn_cfg_v2(struct iwl_mvm *mvm,
				 struct ieee80211_vif *vif,
				 struct ieee80211_ftm_responder_params *params)
{
	struct iwl_tof_responder_dyn_config_cmd_v2 cmd = {
		.lci_len = cpu_to_le32(params->lci_len + 2),
		.civic_len = cpu_to_le32(params->civicloc_len + 2),
	};
	u8 data[IWL_LCI_CIVIC_IE_MAX_SIZE] = {0};
	struct iwl_host_cmd hcmd = {
		.id = WIDE_ID(LOCATION_GROUP, TOF_RESPONDER_DYN_CONFIG_CMD),
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

static int
iwl_mvm_ftm_responder_dyn_cfg_v3(struct iwl_mvm *mvm,
				 struct ieee80211_vif *vif,
				 struct ieee80211_ftm_responder_params *params,
				 struct iwl_mvm_pasn_hltk_data *hltk_data)
{
	struct iwl_tof_responder_dyn_config_cmd cmd;
	struct iwl_host_cmd hcmd = {
		.id = WIDE_ID(LOCATION_GROUP, TOF_RESPONDER_DYN_CONFIG_CMD),
		.data[0] = &cmd,
		.len[0] = sizeof(cmd),
		/* may not be able to DMA from stack */
		.dataflags[0] = IWL_HCMD_DFL_DUP,
	};

	lockdep_assert_held(&mvm->mutex);

	cmd.valid_flags = 0;

	if (params) {
		if (params->lci_len + 2 > sizeof(cmd.lci_buf) ||
		    params->civicloc_len + 2 > sizeof(cmd.civic_buf)) {
			IWL_ERR(mvm,
				"LCI/civic data too big (lci=%zd, civic=%zd)\n",
				params->lci_len, params->civicloc_len);
			return -ENOBUFS;
		}

		cmd.lci_buf[0] = WLAN_EID_MEASURE_REPORT;
		cmd.lci_buf[1] = params->lci_len;
		memcpy(cmd.lci_buf + 2, params->lci, params->lci_len);
		cmd.lci_len = params->lci_len + 2;

		cmd.civic_buf[0] = WLAN_EID_MEASURE_REPORT;
		cmd.civic_buf[1] = params->civicloc_len;
		memcpy(cmd.civic_buf + 2, params->civicloc,
		       params->civicloc_len);
		cmd.civic_len = params->civicloc_len + 2;

		cmd.valid_flags |= IWL_RESPONDER_DYN_CFG_VALID_LCI |
			IWL_RESPONDER_DYN_CFG_VALID_CIVIC;
	}

	if (hltk_data) {
		if (hltk_data->cipher > IWL_LOCATION_CIPHER_GCMP_256) {
			IWL_ERR(mvm, "invalid cipher: %u\n",
				hltk_data->cipher);
			return -EINVAL;
		}

		cmd.cipher = hltk_data->cipher;
		memcpy(cmd.addr, hltk_data->addr, sizeof(cmd.addr));
		memcpy(cmd.hltk_buf, hltk_data->hltk, sizeof(cmd.hltk_buf));
		cmd.valid_flags |= IWL_RESPONDER_DYN_CFG_VALID_PASN_STA;
	}

	return iwl_mvm_send_cmd(mvm, &hcmd);
}

static int
iwl_mvm_ftm_responder_dyn_cfg_cmd(struct iwl_mvm *mvm,
				  struct ieee80211_vif *vif,
				  struct ieee80211_ftm_responder_params *params)
{
	int ret;
	u8 cmd_ver = iwl_fw_lookup_cmd_ver(mvm->fw,
					   WIDE_ID(LOCATION_GROUP, TOF_RESPONDER_DYN_CONFIG_CMD),
					   2);

	switch (cmd_ver) {
	case 2:
		ret = iwl_mvm_ftm_responder_dyn_cfg_v2(mvm, vif,
						       params);
		break;
	case 3:
		ret = iwl_mvm_ftm_responder_dyn_cfg_v3(mvm, vif,
						       params, NULL);
		break;
	default:
		IWL_ERR(mvm, "Unsupported DYN_CONFIG_CMD version %u\n",
			cmd_ver);
		ret = -ENOTSUPP;
	}

	return ret;
}

static void iwl_mvm_resp_del_pasn_sta(struct iwl_mvm *mvm,
				      struct ieee80211_vif *vif,
				      struct iwl_mvm_pasn_sta *sta)
{
	list_del(&sta->list);

	if (iwl_mvm_has_mld_api(mvm->fw))
		iwl_mvm_mld_rm_sta_id(mvm, sta->int_sta.sta_id);
	else
		iwl_mvm_rm_sta_id(mvm, vif, sta->int_sta.sta_id);

	iwl_mvm_dealloc_int_sta(mvm, &sta->int_sta);
	kfree(sta);
}

int iwl_mvm_ftm_respoder_add_pasn_sta(struct iwl_mvm *mvm,
				      struct ieee80211_vif *vif,
				      u8 *addr, u32 cipher, u8 *tk, u32 tk_len,
				      u8 *hltk, u32 hltk_len)
{
	int ret;
	struct iwl_mvm_pasn_sta *sta = NULL;
	struct iwl_mvm_pasn_hltk_data hltk_data = {
		.addr = addr,
		.hltk = hltk,
	};
	struct iwl_mvm_pasn_hltk_data *hltk_data_ptr = NULL;

	u8 cmd_ver = iwl_fw_lookup_cmd_ver(mvm->fw,
					   WIDE_ID(LOCATION_GROUP, TOF_RESPONDER_DYN_CONFIG_CMD),
					   2);

	lockdep_assert_held(&mvm->mutex);

	if (cmd_ver < 3) {
		IWL_ERR(mvm, "Adding PASN station not supported by FW\n");
		return -ENOTSUPP;
	}

	if ((!hltk || !hltk_len) && (!tk || !tk_len)) {
		IWL_ERR(mvm, "TK and HLTK not set\n");
		return -EINVAL;
	}

	if (hltk && hltk_len) {
		hltk_data.cipher = iwl_mvm_cipher_to_location_cipher(cipher);
		if (hltk_data.cipher == IWL_LOCATION_CIPHER_INVALID) {
			IWL_ERR(mvm, "invalid cipher: %u\n", cipher);
			return -EINVAL;
		}

		hltk_data_ptr = &hltk_data;
	}

	if (tk && tk_len) {
		sta = kzalloc(sizeof(*sta), GFP_KERNEL);
		if (!sta)
			return -ENOBUFS;

		ret = iwl_mvm_add_pasn_sta(mvm, vif, &sta->int_sta, addr,
					   cipher, tk, tk_len);
		if (ret) {
			kfree(sta);
			return ret;
		}

		memcpy(sta->addr, addr, ETH_ALEN);
		list_add_tail(&sta->list, &mvm->resp_pasn_list);
	}

	ret = iwl_mvm_ftm_responder_dyn_cfg_v3(mvm, vif, NULL, hltk_data_ptr);
	if (ret && sta)
		iwl_mvm_resp_del_pasn_sta(mvm, vif, sta);

	return ret;
}

int iwl_mvm_ftm_resp_remove_pasn_sta(struct iwl_mvm *mvm,
				     struct ieee80211_vif *vif, u8 *addr)
{
	struct iwl_mvm_pasn_sta *sta, *prev;

	lockdep_assert_held(&mvm->mutex);

	list_for_each_entry_safe(sta, prev, &mvm->resp_pasn_list, list) {
		if (!memcmp(sta->addr, addr, ETH_ALEN)) {
			iwl_mvm_resp_del_pasn_sta(mvm, vif, sta);
			return 0;
		}
	}

	IWL_ERR(mvm, "FTM: PASN station %pM not found\n", addr);
	return -EINVAL;
}

int iwl_mvm_ftm_start_responder(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
				struct ieee80211_bss_conf *bss_conf)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct ieee80211_ftm_responder_params *params;
	struct ieee80211_chanctx_conf ctx, *pctx;
	u16 *phy_ctxt_id;
	struct iwl_mvm_phy_ctxt *phy_ctxt;
	int ret;

	params = bss_conf->ftmr_params;

	lockdep_assert_held(&mvm->mutex);

	if (WARN_ON_ONCE(!bss_conf->ftm_responder))
		return -EINVAL;

	if (vif->p2p || vif->type != NL80211_IFTYPE_AP ||
	    !mvmvif->ap_ibss_active) {
		IWL_ERR(mvm, "Cannot start responder, not in AP mode\n");
		return -EIO;
	}

	rcu_read_lock();
	pctx = rcu_dereference(bss_conf->chanctx_conf);
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

	ret = iwl_mvm_ftm_responder_cmd(mvm, vif, &ctx.def, bss_conf);
	if (ret)
		return ret;

	if (params)
		ret = iwl_mvm_ftm_responder_dyn_cfg_cmd(mvm, vif, params);

	return ret;
}

void iwl_mvm_ftm_responder_clear(struct iwl_mvm *mvm,
				 struct ieee80211_vif *vif)
{
	struct iwl_mvm_pasn_sta *sta, *prev;

	lockdep_assert_held(&mvm->mutex);

	list_for_each_entry_safe(sta, prev, &mvm->resp_pasn_list, list)
		iwl_mvm_resp_del_pasn_sta(mvm, vif, sta);
}

void iwl_mvm_ftm_restart_responder(struct iwl_mvm *mvm,
				   struct ieee80211_vif *vif,
				   struct ieee80211_bss_conf *bss_conf)
{
	if (!bss_conf->ftm_responder)
		return;

	iwl_mvm_ftm_responder_clear(mvm, vif);
	iwl_mvm_ftm_start_responder(mvm, vif, bss_conf);
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
