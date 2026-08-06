// SPDX-License-Identifier: GPL-2.0-only
/*
 * nxpwifi 802.11n helpers
 * Copyright 2011-2024 NXP
 */

#include "cfg.h"
#include "util.h"
#include "fw.h"
#include "main.h"
#include "cmdevt.h"
#include "wmm.h"
#include "11n.h"
#include "11ax.h"

/*
 * Fills HT capability information field, AMPDU Parameters field, HT extended
 * capability field, and supported MCS set fields.
 *
 * HT capability information field, AMPDU Parameters field, supported MCS set
 * fields are retrieved from cfg80211 stack
 *
 * RD responder bit to set to clear in the extended capability header.
 */
int nxpwifi_fill_cap_info(struct nxpwifi_private *priv, u8 radio_type,
			  struct ieee80211_ht_cap *ht_cap)
{
	u16 ht_cap_info;
	u16 bcn_ht_cap = le16_to_cpu(ht_cap->cap_info);
	u16 ht_ext_cap = le16_to_cpu(ht_cap->extended_ht_cap_info);
	struct ieee80211_supported_band *sband =
		priv->wdev.wiphy->bands[radio_type];

	if (WARN_ON_ONCE(!sband)) {
		nxpwifi_dbg(priv->adapter, ERROR, "Invalid radio type!\n");
		return -EINVAL;
	}

	ht_cap->ampdu_params_info =
		(AMPDU_FACTOR_64K & IEEE80211_HT_AMPDU_PARM_FACTOR) |
		((priv->adapter->hw_mpdu_density <<
		 IEEE80211_HT_AMPDU_PARM_DENSITY_SHIFT) &
		 IEEE80211_HT_AMPDU_PARM_DENSITY);

	memcpy((u8 *)&ht_cap->mcs, &sband->ht_cap.mcs,
	       sizeof(sband->ht_cap.mcs));

	if (priv->bss_mode == NL80211_IFTYPE_STATION ||
	    (sband->ht_cap.cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40 &&
	     priv->adapter->sec_chan_offset != IEEE80211_HT_PARAM_CHA_SEC_NONE))
		/* Set MCS32 for infra mode or ad-hoc mode with 40MHz support */
		SETHT_MCS32(ht_cap->mcs.rx_mask);

	/* Clear RD responder bit */
	ht_ext_cap &= ~IEEE80211_HT_EXT_CAP_RD_RESPONDER;

	ht_cap_info = sband->ht_cap.cap;
	if (bcn_ht_cap) {
		if (!(bcn_ht_cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40))
			ht_cap_info &= ~IEEE80211_HT_CAP_SUP_WIDTH_20_40;
		if (!(bcn_ht_cap & IEEE80211_HT_CAP_SGI_40))
			ht_cap_info &= ~IEEE80211_HT_CAP_SGI_40;
		if (!(bcn_ht_cap & IEEE80211_HT_CAP_40MHZ_INTOLERANT))
			ht_cap_info &= ~IEEE80211_HT_CAP_40MHZ_INTOLERANT;
	}
	ht_cap->cap_info = cpu_to_le16(ht_cap_info);
	ht_cap->extended_ht_cap_info = cpu_to_le16(ht_ext_cap);

	if (ISSUPP_BEAMFORMING(priv->adapter->hw_dot_11n_dev_cap))
		ht_cap->tx_BF_cap_info = cpu_to_le32(NXPWIFI_DEF_11N_TX_BF_CAP);

	return 0;
}

/* Return BA stream entry that matches the requested status. */
static struct nxpwifi_tx_ba_stream_tbl *
nxpwifi_get_ba_status(struct nxpwifi_private *priv, int tid,
		      enum nxpwifi_ba_status ba_status)
{
	struct nxpwifi_tx_ba_stream_tbl *tx_ba_tsr_tbl, *found = NULL;

	guard(rcu)();
	list_for_each_entry_rcu(tx_ba_tsr_tbl, &priv->tx_ba_stream_tbl_ptr[tid], list) {
		if (tx_ba_tsr_tbl->ba_status == ba_status) {
			found = tx_ba_tsr_tbl;
			break;
		}
	}
	return found;
}

/* Handle DELBA command response (recreate or continue ADDBA as needed). */
int nxpwifi_ret_11n_delba(struct nxpwifi_private *priv,
			  struct host_cmd_ds_command *resp)
{
	int tid;
	struct nxpwifi_tx_ba_stream_tbl *tx_ba_tbl;
	struct host_cmd_ds_11n_delba *del_ba = &resp->params.del_ba;
	u16 del_ba_param_set = le16_to_cpu(del_ba->del_ba_param_set);

	tid = del_ba_param_set >> DELBA_TID_POS;
	if (del_ba->del_result == BA_RESULT_SUCCESS) {
		nxpwifi_del_ba_tbl(priv, tid, del_ba->peer_mac_addr,
				   TYPE_DELBA_SENT,
				   INITIATOR_BIT(del_ba_param_set));

		tx_ba_tbl = nxpwifi_get_ba_status(priv, tid, BA_SETUP_INPROGRESS);
		if (tx_ba_tbl)
			nxpwifi_send_addba(priv, tx_ba_tbl->tid,
					   tx_ba_tbl->ra);
	} else {
		/*
		 * In case of failure, recreate the deleted stream in case
		 * we initiated the DELBA
		 */
		if (!INITIATOR_BIT(del_ba_param_set))
			return 0;

		nxpwifi_create_ba_tbl(priv, del_ba->peer_mac_addr, tid,
				      BA_SETUP_INPROGRESS);

		tx_ba_tbl = nxpwifi_get_ba_status(priv, tid, BA_SETUP_INPROGRESS);

		if (tx_ba_tbl)
			nxpwifi_del_ba_tbl(priv, tx_ba_tbl->tid, tx_ba_tbl->ra,
					   TYPE_DELBA_SENT, true);
	}

	return 0;
}

/* Handle ADDBA response; delete BA stream on failure. */
int nxpwifi_ret_11n_addba_req(struct nxpwifi_private *priv,
			      struct host_cmd_ds_command *resp)
{
	int tid, tid_down;
	struct host_cmd_ds_11n_addba_rsp *add_ba_rsp = &resp->params.add_ba_rsp;
	struct nxpwifi_tx_ba_stream_tbl *tx_ba_tbl;
	struct nxpwifi_ra_list_tbl *ra_list;
	u16 block_ack_param_set = le16_to_cpu(add_ba_rsp->block_ack_param_set);

	add_ba_rsp->ssn = cpu_to_le16((le16_to_cpu(add_ba_rsp->ssn))
			& SSN_MASK);

	tid = u16_get_bits(block_ack_param_set, IEEE80211_ADDBA_PARAM_TID_MASK);

	tid_down = nxpwifi_wmm_downgrade_tid(priv, tid);
	ra_list = nxpwifi_wmm_get_ralist_node(priv, tid_down,
					      add_ba_rsp->peer_mac_addr);
	if (le16_to_cpu(add_ba_rsp->status_code) != BA_RESULT_SUCCESS) {
		if (ra_list) {
			ra_list->ba_status = BA_SETUP_NONE;
			ra_list->amsdu_in_ampdu = false;
		}
		nxpwifi_del_ba_tbl(priv, tid, add_ba_rsp->peer_mac_addr,
				   TYPE_DELBA_SENT, true);
		if (add_ba_rsp->add_rsp_result != BA_RESULT_TIMEOUT)
			priv->aggr_prio_tbl[tid].ampdu_ap =
				BA_STREAM_NOT_ALLOWED;
		return 0;
	}

	guard(rcu)();
	tx_ba_tbl = nxpwifi_get_ba_tbl(priv, tid, add_ba_rsp->peer_mac_addr);
	if (tx_ba_tbl) {
		nxpwifi_dbg(priv->adapter, EVENT, "info: BA stream complete\n");
		tx_ba_tbl->ba_status = BA_SETUP_COMPLETE;
		if ((block_ack_param_set & IEEE80211_ADDBA_PARAM_AMSDU_MASK) &&
		    priv->add_ba_param.tx_amsdu &&
		    priv->aggr_prio_tbl[tid].amsdu != BA_STREAM_NOT_ALLOWED)
			tx_ba_tbl->amsdu = true;
		else
			tx_ba_tbl->amsdu = false;
		if (ra_list) {
			ra_list->amsdu_in_ampdu = tx_ba_tbl->amsdu;
			ra_list->ba_status = BA_SETUP_COMPLETE;
		}
	} else {
		nxpwifi_dbg(priv->adapter, ERROR, "BA stream not created\n");
	}

	return 0;
}

/*
 * Reconfigure Tx buffer command.
 * Set command ID/action/size; set Tx buffer size on SET; ensure little-endian.
 */
int nxpwifi_cmd_recfg_tx_buf(struct nxpwifi_private *priv,
			     struct host_cmd_ds_command *cmd, int cmd_action,
			     u16 *buf_size)
{
	struct host_cmd_ds_txbuf_cfg *tx_buf = &cmd->params.tx_buf;
	u16 action = (u16)cmd_action;

	cmd->command = cpu_to_le16(HOST_CMD_RECONFIGURE_TX_BUFF);
	cmd->size =
		cpu_to_le16(sizeof(struct host_cmd_ds_txbuf_cfg) + S_DS_GEN);
	tx_buf->action = cpu_to_le16(action);
	switch (action) {
	case HOST_ACT_GEN_SET:
		nxpwifi_dbg(priv->adapter, CMD,
			    "cmd: set tx_buf=%d\n", *buf_size);
		tx_buf->buff_size = cpu_to_le16(*buf_size);
		break;
	case HOST_ACT_GEN_GET:
	default:
		tx_buf->buff_size = 0;
		break;
	}
	return 0;
}

/*
 * AMSDU aggregation control command.
 * Set ID/action/size; set AMSDU params on SET; ensure little-endian.
 */
int nxpwifi_cmd_amsdu_aggr_ctrl(struct host_cmd_ds_command *cmd,
				int cmd_action,
				struct nxpwifi_ds_11n_amsdu_aggr_ctrl *aa_ctrl)
{
	struct host_cmd_ds_amsdu_aggr_ctrl *amsdu_ctrl =
		&cmd->params.amsdu_aggr_ctrl;
	u16 action = (u16)cmd_action;

	cmd->command = cpu_to_le16(HOST_CMD_AMSDU_AGGR_CTRL);
	cmd->size = cpu_to_le16(sizeof(struct host_cmd_ds_amsdu_aggr_ctrl)
				+ S_DS_GEN);
	amsdu_ctrl->action = cpu_to_le16(action);
	switch (action) {
	case HOST_ACT_GEN_SET:
		amsdu_ctrl->enable = cpu_to_le16(aa_ctrl->enable);
		amsdu_ctrl->curr_buf_size = 0;
		break;
	case HOST_ACT_GEN_GET:
	default:
		amsdu_ctrl->curr_buf_size = 0;
		break;
	}
	return 0;
}

/*
 * 11n configuration command.
 * Set action, HT Tx capability/info, and misc config when 11ac HW is present.
 */
int nxpwifi_cmd_11n_cfg(struct nxpwifi_private *priv,
			struct host_cmd_ds_command *cmd, u16 cmd_action,
			struct nxpwifi_ds_11n_tx_cfg *txcfg)
{
	struct host_cmd_ds_11n_cfg *htcfg = &cmd->params.htcfg;

	cmd->command = cpu_to_le16(HOST_CMD_11N_CFG);
	cmd->size = cpu_to_le16(sizeof(struct host_cmd_ds_11n_cfg) + S_DS_GEN);
	htcfg->action = cpu_to_le16(cmd_action);
	htcfg->ht_tx_cap = cpu_to_le16(txcfg->tx_htcap);
	htcfg->ht_tx_info = cpu_to_le16(txcfg->tx_htinfo);

	if (priv->adapter->is_hw_11ac_capable)
		htcfg->misc_config = cpu_to_le16(txcfg->misc_config);

	return 0;
}

/*
 * Append 11n TLVs to the caller-owned buffer.
 * Caller allocates space; no size checks here.
 * May add: HT Cap, HT Operation + channel list, 20/40 BSS Coexistence,
 * and Extended Capabilities (HS2/TWT bits when applicable).
 */
int
nxpwifi_cmd_append_11n_tlv(struct nxpwifi_private *priv,
			   struct nxpwifi_bssdescriptor *bss_desc,
			   u8 **buffer)
{
	struct nxpwifi_ie_types_htcap *ht_cap;
	struct nxpwifi_ie_types_chan_list_param_set *chan_list;
	struct nxpwifi_chan_scan_param_set *chan_param;
	struct nxpwifi_ie_types_2040bssco *bss_co_2040;
	struct nxpwifi_ie_types_extcap *ext_cap;
	int ret_len = 0;
	struct ieee80211_supported_band *sband;
	struct element *hdr;
	u8 radio_type;

	if (!buffer || !*buffer)
		return ret_len;

	radio_type = nxpwifi_band_to_radio_type((u8)bss_desc->bss_band);
	sband = priv->wdev.wiphy->bands[radio_type];

	if (bss_desc->bcn_ht_cap) {
		ht_cap = (struct nxpwifi_ie_types_htcap *)*buffer;
		memset(ht_cap, 0, sizeof(struct nxpwifi_ie_types_htcap));
		ht_cap->header.type = cpu_to_le16(WLAN_EID_HT_CAPABILITY);
		ht_cap->header.len =
			cpu_to_le16(sizeof(struct ieee80211_ht_cap));
		memcpy((u8 *)ht_cap + sizeof(struct nxpwifi_ie_types_header),
		       (u8 *)bss_desc->bcn_ht_cap,
		       le16_to_cpu(ht_cap->header.len));

		nxpwifi_fill_cap_info(priv, radio_type, &ht_cap->ht_cap);
		/* Update HT40 capability from current channel. */
		if (bss_desc->bcn_ht_oper) {
			u8 ht_param = bss_desc->bcn_ht_oper->ht_param;
			u8 radio =
				nxpwifi_band_to_radio_type(bss_desc->bss_band);
			int freq =
				ieee80211_channel_to_frequency(bss_desc->channel,
							       radio);
			struct ieee80211_channel *chan =
				ieee80211_get_channel(priv->adapter->wiphy, freq);

			switch (ht_param & IEEE80211_HT_PARAM_CHA_SEC_OFFSET) {
			case IEEE80211_HT_PARAM_CHA_SEC_ABOVE:
				if (chan->flags & IEEE80211_CHAN_NO_HT40PLUS) {
					ht_cap->ht_cap.cap_info &=
					cpu_to_le16
					(~IEEE80211_HT_CAP_SUP_WIDTH_20_40);
					ht_cap->ht_cap.cap_info &=
					cpu_to_le16(~IEEE80211_HT_CAP_SGI_40);
				}
				break;
			case IEEE80211_HT_PARAM_CHA_SEC_BELOW:
				if (chan->flags & IEEE80211_CHAN_NO_HT40MINUS) {
					ht_cap->ht_cap.cap_info &=
					cpu_to_le16
					(~IEEE80211_HT_CAP_SUP_WIDTH_20_40);
					ht_cap->ht_cap.cap_info &=
					cpu_to_le16(~IEEE80211_HT_CAP_SGI_40);
				}
				break;
			}
		}

		*buffer += sizeof(struct nxpwifi_ie_types_htcap);
		ret_len += sizeof(struct nxpwifi_ie_types_htcap);
	}

	if (bss_desc->bcn_ht_oper) {
		chan_list =
			(struct nxpwifi_ie_types_chan_list_param_set *)*buffer;
		chan_param = chan_list->chan_scan_param;
		memset(chan_list, 0, struct_size(chan_list, chan_scan_param, 1));
		chan_list->header.type = cpu_to_le16(TLV_TYPE_CHANLIST);
		chan_list->header.len = cpu_to_le16(sizeof(*chan_param));
		chan_param->chan_number = bss_desc->bcn_ht_oper->primary_chan;
		chan_param->band_cfg =
			nxpwifi_band_to_radio_type((u8)bss_desc->bss_band);

		if (ISSUPP_11ACENABLED(priv->adapter->fw_cap_info) &&
		    bss_desc->bcn_vht_oper &&
		    bss_desc->bcn_vht_oper->chan_width ==
		    IEEE80211_VHT_CHANWIDTH_80MHZ) {
			SET_SECONDARYCHAN(chan_param->band_cfg,
					  (bss_desc->bcn_ht_oper->ht_param &
					   IEEE80211_HT_PARAM_CHA_SEC_OFFSET));
			chan_param->band_cfg |=
				((CHAN_BW_80MHZ <<
				  BAND_CFG_CHAN_WIDTH_SHIFT_BIT) &
				 BAND_CFG_CHAN_WIDTH_MASK);
		} else if (sband->ht_cap.cap &
			   IEEE80211_HT_CAP_SUP_WIDTH_20_40 &&
			   bss_desc->bcn_ht_oper->ht_param &
			   IEEE80211_HT_PARAM_CHAN_WIDTH_ANY) {
			SET_SECONDARYCHAN(chan_param->band_cfg,
					  (bss_desc->bcn_ht_oper->ht_param &
					   IEEE80211_HT_PARAM_CHA_SEC_OFFSET));
			chan_param->band_cfg |=
				((CHAN_BW_40MHZ <<
				  BAND_CFG_CHAN_WIDTH_SHIFT_BIT) &
				 BAND_CFG_CHAN_WIDTH_MASK);
		}

		*buffer += struct_size(chan_list, chan_scan_param, 1);
		ret_len += struct_size(chan_list, chan_scan_param, 1);
	}

	if (bss_desc->bcn_bss_co_2040) {
		bss_co_2040 = (struct nxpwifi_ie_types_2040bssco *)*buffer;
		memset(bss_co_2040, 0,
		       sizeof(struct nxpwifi_ie_types_2040bssco));
		bss_co_2040->header.type = cpu_to_le16(WLAN_EID_BSS_COEX_2040);
		bss_co_2040->header.len =
		       cpu_to_le16(sizeof(bss_co_2040->bss_co_2040));

		memcpy((u8 *)bss_co_2040 +
		       sizeof(struct nxpwifi_ie_types_header),
		       bss_desc->bcn_bss_co_2040 +
		       sizeof(struct element),
		       le16_to_cpu(bss_co_2040->header.len));

		*buffer += sizeof(struct nxpwifi_ie_types_2040bssco);
		ret_len += sizeof(struct nxpwifi_ie_types_2040bssco);
	}

	if (bss_desc->bcn_ext_cap) {
		u8 *ext_capab;

		hdr = (void *)bss_desc->bcn_ext_cap;

		ext_capab = (u8 *)cfg80211_find_ie(WLAN_EID_EXT_CAPABILITY, priv->gen_ie_buf,
				  priv->gen_ie_buf_len);
		if (ext_capab) {
			ext_capab += 2;
		} else {
			ext_cap = (struct nxpwifi_ie_types_extcap *)*buffer;
			memset(ext_cap, 0, sizeof(struct nxpwifi_ie_types_extcap) + hdr->datalen);
			ext_cap->header.type = cpu_to_le16(WLAN_EID_EXT_CAPABILITY);
			ext_cap->header.len = cpu_to_le16(hdr->datalen);
			ext_capab = ext_cap->ext_capab;
			*buffer += sizeof(struct nxpwifi_ie_types_extcap) + hdr->datalen;
			ret_len += sizeof(struct nxpwifi_ie_types_extcap) + hdr->datalen;
		}

		if (hdr->datalen > 3 &&
		    ext_capab[3] & WLAN_EXT_CAPA4_INTERWORKING_ENABLED)
			priv->hs2_enabled = true;
		else
			priv->hs2_enabled = false;

		if (nxpwifi_is_11ax_twt_supported(priv, bss_desc))
			ext_capab[9] |=
			WLAN_EXT_CAPA10_TWT_REQUESTER_SUPPORT;
	}
	return ret_len;
}

/* Check if pointer is a valid Tx BA stream entry. */
static bool
nxpwifi_is_tx_ba_stream_ptr_valid(struct nxpwifi_private *priv,
				  struct nxpwifi_tx_ba_stream_tbl *tx_tbl_ptr)
{
	struct nxpwifi_tx_ba_stream_tbl *tx_ba_tsr_tbl;
	bool ret = false;
	int tid;

	tid = tx_tbl_ptr->tid;
	guard(rcu)();
	list_for_each_entry_rcu(tx_ba_tsr_tbl, &priv->tx_ba_stream_tbl_ptr[tid], list) {
		if (tx_ba_tsr_tbl == tx_tbl_ptr) {
			ret = true;
			break;
		}
	}
	return ret;
}

/* Delete a Tx BA stream entry (after validating pointer). */
void
nxpwifi_11n_delete_tx_ba_stream_tbl_entry(struct nxpwifi_private *priv,
					  struct nxpwifi_tx_ba_stream_tbl *tbl)
{
	if (!tbl || nxpwifi_is_tx_ba_stream_ptr_valid(priv, tbl))
		return;

	nxpwifi_dbg(priv->adapter, INFO,
		    "info: tx_ba_tsr_tbl %p\n", tbl);

	list_del_rcu(&tbl->list);
	kfree_rcu(tbl, rcu);
}

/* Delete all entries in Tx BA stream table. */
void nxpwifi_11n_delete_all_tx_ba_stream_tbl(struct nxpwifi_private *priv)
{
	int i;
	struct nxpwifi_tx_ba_stream_tbl *del_tbl_ptr, *tmp_node;

	for (i = 0; i < MAX_NUM_TID; i++) {
		spin_lock_bh(&priv->tx_ba_stream_tbl_lock[i]);
		list_for_each_entry_safe(del_tbl_ptr, tmp_node,
					 &priv->tx_ba_stream_tbl_ptr[i], list)
			nxpwifi_11n_delete_tx_ba_stream_tbl_entry(priv, del_tbl_ptr);
		spin_unlock_bh(&priv->tx_ba_stream_tbl_lock[i]);

		INIT_LIST_HEAD(&priv->tx_ba_stream_tbl_ptr[i]);

		priv->aggr_prio_tbl[i].ampdu_ap =
			priv->aggr_prio_tbl[i].ampdu_user;
	}
}

/* Return BA stream entry for given RA/TID. */
struct nxpwifi_tx_ba_stream_tbl *
nxpwifi_get_ba_tbl(struct nxpwifi_private *priv, int tid, u8 *ra)
{
	struct nxpwifi_tx_ba_stream_tbl *tx_ba_tsr_tbl = NULL;

	list_for_each_entry_rcu(tx_ba_tsr_tbl, &priv->tx_ba_stream_tbl_ptr[tid], list) {
		if (ether_addr_equal_unaligned(tx_ba_tsr_tbl->ra, ra) &&
		    tx_ba_tsr_tbl->tid == tid)
			return tx_ba_tsr_tbl;
	}
	return NULL;
}

/* Create Tx BA stream entry for given RA/TID. */
void nxpwifi_create_ba_tbl(struct nxpwifi_private *priv, u8 *ra, int tid,
			   enum nxpwifi_ba_status ba_status)
{
	struct nxpwifi_tx_ba_stream_tbl *new_node;
	struct nxpwifi_ra_list_tbl *ra_list;
	int tid_down;
	struct nxpwifi_tx_ba_stream_tbl *tx_ba_tbl;

	guard(rcu)();
	tx_ba_tbl = nxpwifi_get_ba_tbl(priv, tid, ra);

	if (!tx_ba_tbl) {
		new_node = kzalloc_obj(*new_node, GFP_ATOMIC);
		if (!new_node)
			return;

		tid_down = nxpwifi_wmm_downgrade_tid(priv, tid);
		ra_list = nxpwifi_wmm_get_ralist_node(priv, tid_down, ra);
		if (ra_list) {
			ra_list->ba_status = ba_status;
			ra_list->amsdu_in_ampdu = false;
		}
		INIT_LIST_HEAD(&new_node->list);

		new_node->tid = tid;
		new_node->ba_status = ba_status;
		memcpy(new_node->ra, ra, ETH_ALEN);

		spin_lock_bh(&priv->tx_ba_stream_tbl_lock[tid]);
		list_add_tail_rcu(&new_node->list, &priv->tx_ba_stream_tbl_ptr[tid]);
		spin_unlock_bh(&priv->tx_ba_stream_tbl_lock[tid]);
	}
}

/* Send ADDBA request to the given TID/RA. */
int nxpwifi_send_addba(struct nxpwifi_private *priv, int tid, u8 *peer_mac)
{
	struct host_cmd_ds_11n_addba_req add_ba_req;
	u32 tx_win_size = priv->add_ba_param.tx_win_size;
	static u8 dialog_tok;
	u16 block_ack_param_set;

	nxpwifi_dbg(priv->adapter, CMD, "cmd: %s: tid %d\n", __func__, tid);

	memset(&add_ba_req, 0, sizeof(add_ba_req));

	block_ack_param_set = (u16)((tid << BLOCKACKPARAM_TID_POS) |
				    tx_win_size << BLOCKACKPARAM_WINSIZE_POS |
				    IMMEDIATE_BLOCK_ACK);

	/* enable AMSDU inside AMPDU */
	if (priv->add_ba_param.tx_amsdu &&
	    priv->aggr_prio_tbl[tid].amsdu != BA_STREAM_NOT_ALLOWED)
		block_ack_param_set |= IEEE80211_ADDBA_PARAM_AMSDU_MASK;

	add_ba_req.block_ack_param_set = cpu_to_le16(block_ack_param_set);
	add_ba_req.block_ack_tmo = cpu_to_le16((u16)priv->add_ba_param.timeout);

	++dialog_tok;

	if (dialog_tok == 0)
		dialog_tok = 1;

	add_ba_req.dialog_token = dialog_tok;
	memcpy(&add_ba_req.peer_mac_addr, peer_mac, ETH_ALEN);

	/* We don't wait for the response of this command */
	return nxpwifi_send_cmd(priv, HOST_CMD_11N_ADDBA_REQ,
			       0, 0, &add_ba_req, false);
}

/* Send DELBA request to the given TID/RA. */
int nxpwifi_send_delba(struct nxpwifi_private *priv, int tid, u8 *peer_mac,
		       int initiator)
{
	struct host_cmd_ds_11n_delba delba;
	u16 del_ba_param_set;

	memset(&delba, 0, sizeof(delba));

	del_ba_param_set = tid << DELBA_TID_POS;

	if (initiator)
		del_ba_param_set |= IEEE80211_DELBA_PARAM_INITIATOR_MASK;
	else
		del_ba_param_set &= ~IEEE80211_DELBA_PARAM_INITIATOR_MASK;

	delba.del_ba_param_set = cpu_to_le16(del_ba_param_set);
	memcpy(&delba.peer_mac_addr, peer_mac, ETH_ALEN);

	/* We don't wait for the response of this command */
	return nxpwifi_send_cmd(priv, HOST_CMD_11N_DELBA,
			       HOST_ACT_GEN_SET, 0, &delba, false);
}

/* Send DELBA to specific TID. */
void nxpwifi_11n_delba(struct nxpwifi_private *priv, int tid)
{
	struct nxpwifi_rx_reorder_tbl *rx_reor_tbl_ptr;
	u8 ta[ETH_ALEN];
	bool found = false;

	rcu_read_lock();
	list_for_each_entry_rcu(rx_reor_tbl_ptr, &priv->rx_reorder_tbl_ptr[tid], list) {
		if (rx_reor_tbl_ptr->tid == tid) {
			memcpy(ta, rx_reor_tbl_ptr->ta, ETH_ALEN);
			found = true;
			break;
		}
	}
	rcu_read_unlock();

	if (found) {
		nxpwifi_dbg(priv->adapter, INFO,
			    "Send delba to tid=%d, %pM\n", tid, ta);
		nxpwifi_send_delba(priv, tid, ta, 0);
	}
}

/* Handle DELBA event; remove BA stream. */
void nxpwifi_11n_delete_ba_stream(struct nxpwifi_private *priv, u8 *del_ba)
{
	struct host_cmd_ds_11n_delba *cmd_del_ba =
		(struct host_cmd_ds_11n_delba *)del_ba;
	u16 del_ba_param_set = le16_to_cpu(cmd_del_ba->del_ba_param_set);
	int tid;

	tid = del_ba_param_set >> DELBA_TID_POS;

	nxpwifi_del_ba_tbl(priv, tid, cmd_del_ba->peer_mac_addr,
			   TYPE_DELBA_RECEIVE, INITIATOR_BIT(del_ba_param_set));
}

/* Retrieve Rx reordering table. */
int nxpwifi_get_rx_reorder_tbl(struct nxpwifi_private *priv,
			       struct nxpwifi_ds_rx_reorder_tbl *buf)
{
	int i, j;
	struct nxpwifi_ds_rx_reorder_tbl *rx_reo_tbl = buf;
	struct nxpwifi_rx_reorder_tbl *rx_reorder_tbl_ptr;
	int count = 0;

	guard(rcu)();
	for (j = 0; j < MAX_NUM_TID; j++) {
		list_for_each_entry_rcu(rx_reorder_tbl_ptr,
					&priv->rx_reorder_tbl_ptr[j],
					list) {
			rx_reo_tbl->tid = (u16)rx_reorder_tbl_ptr->tid;
			memcpy(rx_reo_tbl->ta, rx_reorder_tbl_ptr->ta, ETH_ALEN);
			rx_reo_tbl->start_win = rx_reorder_tbl_ptr->start_win;
			rx_reo_tbl->win_size = rx_reorder_tbl_ptr->win_size;
			for (i = 0; i < rx_reorder_tbl_ptr->win_size; ++i) {
				if (rx_reorder_tbl_ptr->rx_reorder_ptr[i])
					rx_reo_tbl->buffer[i] = true;
				else
					rx_reo_tbl->buffer[i] = false;
			}
			rx_reo_tbl++;
			count++;

			if (count >= NXPWIFI_MAX_RX_BASTREAM_SUPPORTED)
				return count;
		}
	}

	return count;
}

/* Retrieve Tx BA stream table. */
int nxpwifi_get_tx_ba_stream_tbl(struct nxpwifi_private *priv,
				 struct nxpwifi_ds_tx_ba_stream_tbl *buf)
{
	struct nxpwifi_tx_ba_stream_tbl *tx_ba_tsr_tbl;
	struct nxpwifi_ds_tx_ba_stream_tbl *rx_reo_tbl = buf;
	int count = 0;
	int i;

	guard(rcu)();
	for (i = 0; i < MAX_NUM_TID; i++) {
		list_for_each_entry_rcu(tx_ba_tsr_tbl, &priv->tx_ba_stream_tbl_ptr[i], list) {
			rx_reo_tbl->tid = (u16)tx_ba_tsr_tbl->tid;
			nxpwifi_dbg(priv->adapter, DATA, "data: %s tid=%d\n",
				    __func__, rx_reo_tbl->tid);
			memcpy(rx_reo_tbl->ra, tx_ba_tsr_tbl->ra, ETH_ALEN);
			rx_reo_tbl->amsdu = tx_ba_tsr_tbl->amsdu;
			rx_reo_tbl++;
			count++;
			if (count >= NXPWIFI_MAX_TX_BASTREAM_SUPPORTED)
				return count;
		}
	}

	return count;
}

/* Delete Tx BA stream entry by RA. */
void nxpwifi_del_tx_ba_stream_tbl_by_ra(struct nxpwifi_private *priv, u8 *ra)
{
	struct nxpwifi_tx_ba_stream_tbl *tbl, *tmp;
	int i;

	if (!ra)
		return;

	for (i = 0; i < MAX_NUM_TID; i++) {
		spin_lock_bh(&priv->tx_ba_stream_tbl_lock[i]);
		list_for_each_entry_safe(tbl, tmp,
					 &priv->tx_ba_stream_tbl_ptr[i], list)
			if (!memcmp(tbl->ra, ra, ETH_ALEN))
				nxpwifi_11n_delete_tx_ba_stream_tbl_entry(priv, tbl);

		spin_unlock_bh(&priv->tx_ba_stream_tbl_lock[i]);
	}
}

/* Initialize BlockAck parameters. */
void nxpwifi_set_ba_params(struct nxpwifi_private *priv)
{
	priv->add_ba_param.timeout = NXPWIFI_DEFAULT_BLOCK_ACK_TIMEOUT;

	if (GET_BSS_ROLE(priv) == NXPWIFI_BSS_ROLE_UAP) {
		priv->add_ba_param.tx_win_size =
			NXPWIFI_UAP_AMPDU_DEF_TXWINSIZE;
		priv->add_ba_param.rx_win_size =
			NXPWIFI_UAP_AMPDU_DEF_RXWINSIZE;
	} else {
		priv->add_ba_param.tx_win_size =
			NXPWIFI_STA_AMPDU_DEF_TXWINSIZE;
		priv->add_ba_param.rx_win_size =
			NXPWIFI_STA_AMPDU_DEF_RXWINSIZE;
	}

	priv->add_ba_param.tx_amsdu = true;
	priv->add_ba_param.rx_amsdu = true;
}

u8 nxpwifi_get_sec_chan_offset(int chan)
{
	u8 sec_offset;

	switch (chan) {
	case 36:
	case 44:
	case 52:
	case 60:
	case 100:
	case 108:
	case 116:
	case 124:
	case 132:
	case 140:
	case 149:
	case 157:
	case 173:
		sec_offset = IEEE80211_HT_PARAM_CHA_SEC_ABOVE;
		break;
	case 40:
	case 48:
	case 56:
	case 64:
	case 104:
	case 112:
	case 120:
	case 128:
	case 136:
	case 144:
	case 153:
	case 161:
	case 169:
	case 177:
		sec_offset = IEEE80211_HT_PARAM_CHA_SEC_BELOW;
		break;
	case 165:
	default:
		sec_offset = IEEE80211_HT_PARAM_CHA_SEC_NONE;
		break;
	}

	return sec_offset;
}

/* Send DELBA to entries in the Tx BA stream table. */
static void
nxpwifi_send_delba_txbastream_tbl(struct nxpwifi_private *priv, u8 tid)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct nxpwifi_tx_ba_stream_tbl *tx_ba_stream_tbl_ptr;

	guard(rcu)();
	list_for_each_entry_rcu(tx_ba_stream_tbl_ptr,
				&priv->tx_ba_stream_tbl_ptr[tid], list) {
		if (tx_ba_stream_tbl_ptr->ba_status == BA_SETUP_COMPLETE) {
			if (tid == tx_ba_stream_tbl_ptr->tid) {
				nxpwifi_dbg(adapter, INFO,
					    "Tx:Send delba to tid=%d, %pM\n", tid,
					    tx_ba_stream_tbl_ptr->ra);
				nxpwifi_send_delba(priv,
						   tx_ba_stream_tbl_ptr->tid,
						   tx_ba_stream_tbl_ptr->ra, 1);
				break;
			}
		}
	}
}

/*
 * Update tx_win_size for all interfaces and send DELBA when it changes.
 */
void nxpwifi_update_ampdu_txwinsize(struct nxpwifi_adapter *adapter)
{
	u8 i, j;
	u32 tx_win_size;
	struct nxpwifi_private *priv;

	for (i = 0; i < adapter->priv_num; i++) {
		priv = adapter->priv[i];
		tx_win_size = priv->add_ba_param.tx_win_size;

		if (priv->bss_type == NXPWIFI_BSS_TYPE_STA)
			priv->add_ba_param.tx_win_size =
				NXPWIFI_STA_AMPDU_DEF_TXWINSIZE;

		if (priv->bss_type == NXPWIFI_BSS_TYPE_UAP)
			priv->add_ba_param.tx_win_size =
				NXPWIFI_UAP_AMPDU_DEF_TXWINSIZE;

		if (adapter->coex_win_size) {
			if (adapter->coex_tx_win_size)
				priv->add_ba_param.tx_win_size =
					adapter->coex_tx_win_size;
		}

		if (tx_win_size != priv->add_ba_param.tx_win_size) {
			if (!priv->media_connected)
				continue;
			for (j = 0; j < MAX_NUM_TID; j++)
				nxpwifi_send_delba_txbastream_tbl(priv, j);
		}
	}
}
