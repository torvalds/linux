// SPDX-License-Identifier: GPL-2.0-only
/*
 * NXP Wireless LAN device driver: 802.11n
 *
 * Copyright 2011-2020 NXP
 */

#include "decl.h"
#include "ioctl.h"
#include "util.h"
#include "fw.h"
#include "main.h"
#include "wmm.h"
#include "11n.h"

/*
 * Fills HT capability information field, AMPDU Parameters field, HT extended
 * capability field, and supported MCS set fields.
 *
 * HT capability information field, AMPDU Parameters field, supported MCS set
 * fields are retrieved from cfg80211 stack
 *
 * RD responder bit to set to clear in the extended capability header.
 */
int mwifiex_fill_cap_info(struct mwifiex_private *priv, u8 radio_type,
			  struct ieee80211_ht_cap *ht_cap)
{
	uint16_t ht_ext_cap = le16_to_cpu(ht_cap->extended_ht_cap_info);
	struct ieee80211_supported_band *sband =
					priv->wdev.wiphy->bands[radio_type];

	if (WARN_ON_ONCE(!sband)) {
		mwifiex_dbg(priv->adapter, ERROR, "Invalid radio type!\n");
		return -EINVAL;
	}

	ht_cap->ampdu_params_info =
		(sband->ht_cap.ampdu_factor &
		 IEEE80211_HT_AMPDU_PARM_FACTOR) |
		((sband->ht_cap.ampdu_density <<
		 IEEE80211_HT_AMPDU_PARM_DENSITY_SHIFT) &
		 IEEE80211_HT_AMPDU_PARM_DENSITY);

	memcpy((u8 *)&ht_cap->mcs, &sband->ht_cap.mcs,
	       sizeof(sband->ht_cap.mcs));

	if (priv->bss_mode == NL80211_IFTYPE_STATION ||
	    (sband->ht_cap.cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40 &&
	     (priv->adapter->sec_chan_offset !=
					IEEE80211_HT_PARAM_CHA_SEC_NONE)))
		/* Set MCS32 for infra mode or ad-hoc mode with 40MHz support */
		SETHT_MCS32(ht_cap->mcs.rx_mask);

	/* Clear RD responder bit */
	ht_ext_cap &= ~IEEE80211_HT_EXT_CAP_RD_RESPONDER;

	ht_cap->cap_info = cpu_to_le16(sband->ht_cap.cap);
	ht_cap->extended_ht_cap_info = cpu_to_le16(ht_ext_cap);

	if (ISSUPP_BEAMFORMING(priv->adapter->hw_dot_11n_dev_cap))
		ht_cap->tx_BF_cap_info = cpu_to_le32(MWIFIEX_DEF_11N_TX_BF_CAP);

	return 0;
}

/*
 * This function returns the pointer to an entry in BA Stream
 * table which matches the requested BA status.
 */
static struct mwifiex_tx_ba_stream_tbl *
mwifiex_get_ba_status(struct mwifiex_private *priv,
		      enum mwifiex_ba_status ba_status)
{
	struct mwifiex_tx_ba_stream_tbl *tx_ba_tsr_tbl;

	spin_lock_bh(&priv->tx_ba_stream_tbl_lock);
	list_for_each_entry(tx_ba_tsr_tbl, &priv->tx_ba_stream_tbl_ptr, list) {
		if (tx_ba_tsr_tbl->ba_status == ba_status) {
			spin_unlock_bh(&priv->tx_ba_stream_tbl_lock);
			return tx_ba_tsr_tbl;
		}
	}
	spin_unlock_bh(&priv->tx_ba_stream_tbl_lock);
	return NULL;
}

/*
 * This function handles the command response of delete a block
 * ack request.
 *
 * The function checks the response success status and takes action
 * accordingly (send an add BA request in case of success, or recreate
 * the deleted stream in case of failure, if the add BA was also
 * initiated by us).
 */
int mwifiex_ret_11n_delba(struct mwifiex_private *priv,
			  struct host_cmd_ds_command *resp)
{
	int tid;
	struct mwifiex_tx_ba_stream_tbl *tx_ba_tbl;
	struct host_cmd_ds_11n_delba *del_ba = &resp->params.del_ba;
	uint16_t del_ba_param_set = le16_to_cpu(del_ba->del_ba_param_set);

	tid = del_ba_param_set >> DELBA_TID_POS;
	if (del_ba->del_result == BA_RESULT_SUCCESS) {
		mwifiex_del_ba_tbl(priv, tid, del_ba->peer_mac_addr,
				   TYPE_DELBA_SENT,
				   INITIATOR_BIT(del_ba_param_set));

		tx_ba_tbl = mwifiex_get_ba_status(priv, BA_SETUP_INPROGRESS);
		if (tx_ba_tbl)
			mwifiex_send_addba(priv, tx_ba_tbl->tid,
					   tx_ba_tbl->ra);
	} else { /*
		  * In case of failure, recreate the deleted stream in case
		  * we initiated the DELBA
		  */
		if (!INITIATOR_BIT(del_ba_param_set))
			return 0;

		mwifiex_create_ba_tbl(priv, del_ba->peer_mac_addr, tid,
				      BA_SETUP_INPROGRESS);

		tx_ba_tbl = mwifiex_get_ba_status(priv, BA_SETUP_INPROGRESS);

		if (tx_ba_tbl)
			mwifiex_del_ba_tbl(priv, tx_ba_tbl->tid, tx_ba_tbl->ra,
					   TYPE_DELBA_SENT, true);
	}

	return 0;
}

/*
 * This function handles the command response of add a block
 * ack request.
 *
 * Handling includes changing the header fields to CPU formats, checking
 * the response success status and taking actions accordingly (delete the
 * BA stream table in case of failure).
 */
int mwifiex_ret_11n_addba_req(struct mwifiex_private *priv,
			      struct host_cmd_ds_command *resp)
{
	int tid, tid_down;
	struct host_cmd_ds_11n_addba_rsp *add_ba_rsp = &resp->params.add_ba_rsp;
	struct mwifiex_tx_ba_stream_tbl *tx_ba_tbl;
	struct mwifiex_ra_list_tbl *ra_list;
	u16 block_ack_param_set = le16_to_cpu(add_ba_rsp->block_ack_param_set);

	add_ba_rsp->ssn = cpu_to_le16((le16_to_cpu(add_ba_rsp->ssn))
			& SSN_MASK);

	tid = (block_ack_param_set & IEEE80211_ADDBA_PARAM_TID_MASK)
	       >> BLOCKACKPARAM_TID_POS;

	tid_down = mwifiex_wmm_downgrade_tid(priv, tid);
	ra_list = mwifiex_wmm_get_ralist_node(priv, tid_down, add_ba_rsp->
		peer_mac_addr);
	if (le16_to_cpu(add_ba_rsp->status_code) != BA_RESULT_SUCCESS) {
		if (ra_list) {
			ra_list->ba_status = BA_SETUP_NONE;
			ra_list->amsdu_in_ampdu = false;
		}
		mwifiex_del_ba_tbl(priv, tid, add_ba_rsp->peer_mac_addr,
				   TYPE_DELBA_SENT, true);
		if (add_ba_rsp->add_rsp_result != BA_RESULT_TIMEOUT)
			priv->aggr_prio_tbl[tid].ampdu_ap =
				BA_STREAM_NOT_ALLOWED;
		return 0;
	}

	tx_ba_tbl = mwifiex_get_ba_tbl(priv, tid, add_ba_rsp->peer_mac_addr);
	if (tx_ba_tbl) {
		mwifiex_dbg(priv->adapter, EVENT, "info: BA stream complete\n");
		tx_ba_tbl->ba_status = BA_SETUP_COMPLETE;
		if ((block_ack_param_set & BLOCKACKPARAM_AMSDU_SUPP_MASK) &&
		    priv->add_ba_param.tx_amsdu &&
		    (priv->aggr_prio_tbl[tid].amsdu != BA_STREAM_NOT_ALLOWED))
			tx_ba_tbl->amsdu = true;
		else
			tx_ba_tbl->amsdu = false;
		if (ra_list) {
			ra_list->amsdu_in_ampdu = tx_ba_tbl->amsdu;
			ra_list->ba_status = BA_SETUP_COMPLETE;
		}
	} else {
		mwifiex_dbg(priv->adapter, ERROR, "BA stream not created\n");
	}

	return 0;
}

/*
 * This function prepares command of reconfigure Tx buffer.
 *
 * Preparation includes -
 *      - Setting command ID, action and proper size
 *      - Setting Tx buffer size (for SET only)
 *      - Ensuring correct endian-ness
 */
int mwifiex_cmd_recfg_tx_buf(struct mwifiex_private *priv,
			     struct host_cmd_ds_command *cmd, int cmd_action,
			     u16 *buf_size)
{
	struct host_cmd_ds_txbuf_cfg *tx_buf = &cmd->params.tx_buf;
	u16 action = (u16) cmd_action;

	cmd->command = cpu_to_le16(HostCmd_CMD_RECONFIGURE_TX_BUFF);
	cmd->size =
		cpu_to_le16(sizeof(struct host_cmd_ds_txbuf_cfg) + S_DS_GEN);
	tx_buf->action = cpu_to_le16(action);
	switch (action) {
	case HostCmd_ACT_GEN_SET:
		mwifiex_dbg(priv->adapter, CMD,
			    "cmd: set tx_buf=%d\n", *buf_size);
		tx_buf->buff_size = cpu_to_le16(*buf_size);
		break;
	case HostCmd_ACT_GEN_GET:
	default:
		tx_buf->buff_size = 0;
		break;
	}
	return 0;
}

/*
 * This function prepares command of AMSDU aggregation control.
 *
 * Preparation includes -
 *      - Setting command ID, action and proper size
 *      - Setting AMSDU control parameters (for SET only)
 *      - Ensuring correct endian-ness
 */
int mwifiex_cmd_amsdu_aggr_ctrl(struct host_cmd_ds_command *cmd,
				int cmd_action,
				struct mwifiex_ds_11n_amsdu_aggr_ctrl *aa_ctrl)
{
	struct host_cmd_ds_amsdu_aggr_ctrl *amsdu_ctrl =
		&cmd->params.amsdu_aggr_ctrl;
	u16 action = (u16) cmd_action;

	cmd->command = cpu_to_le16(HostCmd_CMD_AMSDU_AGGR_CTRL);
	cmd->size = cpu_to_le16(sizeof(struct host_cmd_ds_amsdu_aggr_ctrl)
				+ S_DS_GEN);
	amsdu_ctrl->action = cpu_to_le16(action);
	switch (action) {
	case HostCmd_ACT_GEN_SET:
		amsdu_ctrl->enable = cpu_to_le16(aa_ctrl->enable);
		amsdu_ctrl->curr_buf_size = 0;
		break;
	case HostCmd_ACT_GEN_GET:
	default:
		amsdu_ctrl->curr_buf_size = 0;
		break;
	}
	return 0;
}

/*
 * This function prepares 11n configuration command.
 *
 * Preparation includes -
 *      - Setting command ID, action and proper size
 *      - Setting HT Tx capability and HT Tx information fields
 *      - Ensuring correct endian-ness
 */
int mwifiex_cmd_11n_cfg(struct mwifiex_private *priv,
			struct host_cmd_ds_command *cmd, u16 cmd_action,
			struct mwifiex_ds_11n_tx_cfg *txcfg)
{
	struct host_cmd_ds_11n_cfg *htcfg = &cmd->params.htcfg;

	cmd->command = cpu_to_le16(HostCmd_CMD_11N_CFG);
	cmd->size = cpu_to_le16(sizeof(struct host_cmd_ds_11n_cfg) + S_DS_GEN);
	htcfg->action = cpu_to_le16(cmd_action);
	htcfg->ht_tx_cap = cpu_to_le16(txcfg->tx_htcap);
	htcfg->ht_tx_info = cpu_to_le16(txcfg->tx_htinfo);

	if (priv->adapter->is_hw_11ac_capable)
		htcfg->misc_config = cpu_to_le16(txcfg->misc_config);

	return 0;
}

/*
 * This function appends an 11n TLV to a buffer.
 *
 * Buffer allocation is responsibility of the calling
 * function. No size validation is made here.
 *
 * The function fills up the following sections, if applicable -
 *      - HT capability IE
 *      - HT information IE (with channel list)
 *      - 20/40 BSS Coexistence IE
 *      - HT Extended Capabilities IE
 */
int
mwifiex_cmd_append_11n_tlv(struct mwifiex_private *priv,
			   struct mwifiex_bssdescriptor *bss_desc,
			   u8 **buffer)
{
	struct mwifiex_ie_types_htcap *ht_cap;
	struct mwifiex_ie_types_htinfo *ht_info;
	struct mwifiex_ie_types_chan_list_param_set *chan_list;
	struct mwifiex_ie_types_2040bssco *bss_co_2040;
	struct mwifiex_ie_types_extcap *ext_cap;
	int ret_len = 0;
	struct ieee80211_supported_band *sband;
	struct ieee_types_header *hdr;
	u8 radio_type;

	if (!buffer || !*buffer)
		return ret_len;

	radio_type = mwifiex_band_to_radio_type((u8) bss_desc->bss_band);
	sband = priv->wdev.wiphy->bands[radio_type];

	if (bss_desc->bcn_ht_cap) {
		ht_cap = (struct mwifiex_ie_types_htcap *) *buffer;
		memset(ht_cap, 0, sizeof(struct mwifiex_ie_types_htcap));
		ht_cap->header.type = cpu_to_le16(WLAN_EID_HT_CAPABILITY);
		ht_cap->header.len =
				cpu_to_le16(sizeof(struct ieee80211_ht_cap));
		memcpy((u8 *) ht_cap + sizeof(struct mwifiex_ie_types_header),
		       (u8 *)bss_desc->bcn_ht_cap,
		       le16_to_cpu(ht_cap->header.len));

		mwifiex_fill_cap_info(priv, radio_type, &ht_cap->ht_cap);
		/* Update HT40 capability from current channel information */
		if (bss_desc->bcn_ht_oper) {
			u8 ht_param = bss_desc->bcn_ht_oper->ht_param;
			u8 radio =
			mwifiex_band_to_radio_type(bss_desc->bss_band);
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

		*buffer += sizeof(struct mwifiex_ie_types_htcap);
		ret_len += sizeof(struct mwifiex_ie_types_htcap);
	}

	if (bss_desc->bcn_ht_oper) {
		if (priv->bss_mode == NL80211_IFTYPE_ADHOC) {
			ht_info = (struct mwifiex_ie_types_htinfo *) *buffer;
			memset(ht_info, 0,
			       sizeof(struct mwifiex_ie_types_htinfo));
			ht_info->header.type =
					cpu_to_le16(WLAN_EID_HT_OPERATION);
			ht_info->header.len =
				cpu_to_le16(
					sizeof(struct ieee80211_ht_operation));

			memcpy((u8 *) ht_info +
			       sizeof(struct mwifiex_ie_types_header),
			       (u8 *)bss_desc->bcn_ht_oper,
			       le16_to_cpu(ht_info->header.len));

			if (!(sband->ht_cap.cap &
					IEEE80211_HT_CAP_SUP_WIDTH_20_40))
				ht_info->ht_oper.ht_param &=
					~(IEEE80211_HT_PARAM_CHAN_WIDTH_ANY |
					IEEE80211_HT_PARAM_CHA_SEC_OFFSET);

			*buffer += sizeof(struct mwifiex_ie_types_htinfo);
			ret_len += sizeof(struct mwifiex_ie_types_htinfo);
		}

		chan_list =
			(struct mwifiex_ie_types_chan_list_param_set *) *buffer;
		memset(chan_list, 0,
		       sizeof(struct mwifiex_ie_types_chan_list_param_set));
		chan_list->header.type = cpu_to_le16(TLV_TYPE_CHANLIST);
		chan_list->header.len = cpu_to_le16(
			sizeof(struct mwifiex_ie_types_chan_list_param_set) -
			sizeof(struct mwifiex_ie_types_header));
		chan_list->chan_scan_param[0].chan_number =
			bss_desc->bcn_ht_oper->primary_chan;
		chan_list->chan_scan_param[0].radio_type =
			mwifiex_band_to_radio_type((u8) bss_desc->bss_band);

		if (sband->ht_cap.cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40 &&
		    bss_desc->bcn_ht_oper->ht_param &
		    IEEE80211_HT_PARAM_CHAN_WIDTH_ANY)
			SET_SECONDARYCHAN(chan_list->chan_scan_param[0].
					  radio_type,
					  (bss_desc->bcn_ht_oper->ht_param &
					  IEEE80211_HT_PARAM_CHA_SEC_OFFSET));

		*buffer += sizeof(struct mwifiex_ie_types_chan_list_param_set);
		ret_len += sizeof(struct mwifiex_ie_types_chan_list_param_set);
	}

	if (bss_desc->bcn_bss_co_2040) {
		bss_co_2040 = (struct mwifiex_ie_types_2040bssco *) *buffer;
		memset(bss_co_2040, 0,
		       sizeof(struct mwifiex_ie_types_2040bssco));
		bss_co_2040->header.type = cpu_to_le16(WLAN_EID_BSS_COEX_2040);
		bss_co_2040->header.len =
		       cpu_to_le16(sizeof(bss_co_2040->bss_co_2040));

		memcpy((u8 *) bss_co_2040 +
		       sizeof(struct mwifiex_ie_types_header),
		       bss_desc->bcn_bss_co_2040 +
		       sizeof(struct ieee_types_header),
		       le16_to_cpu(bss_co_2040->header.len));

		*buffer += sizeof(struct mwifiex_ie_types_2040bssco);
		ret_len += sizeof(struct mwifiex_ie_types_2040bssco);
	}

	if (bss_desc->bcn_ext_cap) {
		hdr = (void *)bss_desc->bcn_ext_cap;
		ext_cap = (struct mwifiex_ie_types_extcap *) *buffer;
		memset(ext_cap, 0, sizeof(struct mwifiex_ie_types_extcap));
		ext_cap->header.type = cpu_to_le16(WLAN_EID_EXT_CAPABILITY);
		ext_cap->header.len = cpu_to_le16(hdr->len);

		memcpy((u8 *)ext_cap->ext_capab,
		       bss_desc->bcn_ext_cap + sizeof(struct ieee_types_header),
		       le16_to_cpu(ext_cap->header.len));

		if (hdr->len > 3 &&
		    ext_cap->ext_capab[3] & WLAN_EXT_CAPA4_INTERWORKING_ENABLED)
			priv->hs2_enabled = true;
		else
			priv->hs2_enabled = false;

		*buffer += sizeof(struct mwifiex_ie_types_extcap) + hdr->len;
		ret_len += sizeof(struct mwifiex_ie_types_extcap) + hdr->len;
	}

	return ret_len;
}

/*
 * This function checks if the given pointer is valid entry of
 * Tx BA Stream table.
 */
static int mwifiex_is_tx_ba_stream_ptr_valid(struct mwifiex_private *priv,
				struct mwifiex_tx_ba_stream_tbl *tx_tbl_ptr)
{
	struct mwifiex_tx_ba_stream_tbl *tx_ba_tsr_tbl;

	list_for_each_entry(tx_ba_tsr_tbl, &priv->tx_ba_stream_tbl_ptr, list) {
		if (tx_ba_tsr_tbl == tx_tbl_ptr)
			return true;
	}

	return false;
}

/*
 * This function deletes the given entry in Tx BA Stream table.
 *
 * The function also performs a validity check on the supplied
 * pointer before trying to delete.
 */
void mwifiex_11n_delete_tx_ba_stream_tbl_entry(struct mwifiex_private *priv,
				struct mwifiex_tx_ba_stream_tbl *tx_ba_tsr_tbl)
{
	if (!tx_ba_tsr_tbl &&
	    mwifiex_is_tx_ba_stream_ptr_valid(priv, tx_ba_tsr_tbl))
		return;

	mwifiex_dbg(priv->adapter, INFO,
		    "info: tx_ba_tsr_tbl %p\n", tx_ba_tsr_tbl);

	list_del(&tx_ba_tsr_tbl->list);

	kfree(tx_ba_tsr_tbl);
}

/*
 * This function deletes all the entries in Tx BA Stream table.
 */
void mwifiex_11n_delete_all_tx_ba_stream_tbl(struct mwifiex_private *priv)
{
	int i;
	struct mwifiex_tx_ba_stream_tbl *del_tbl_ptr, *tmp_node;

	spin_lock_bh(&priv->tx_ba_stream_tbl_lock);
	list_for_each_entry_safe(del_tbl_ptr, tmp_node,
				 &priv->tx_ba_stream_tbl_ptr, list)
		mwifiex_11n_delete_tx_ba_stream_tbl_entry(priv, del_tbl_ptr);
	spin_unlock_bh(&priv->tx_ba_stream_tbl_lock);

	INIT_LIST_HEAD(&priv->tx_ba_stream_tbl_ptr);

	for (i = 0; i < MAX_NUM_TID; ++i)
		priv->aggr_prio_tbl[i].ampdu_ap =
			priv->aggr_prio_tbl[i].ampdu_user;
}

/*
 * This function returns the pointer to an entry in BA Stream
 * table which matches the given RA/TID pair.
 */
struct mwifiex_tx_ba_stream_tbl *
mwifiex_get_ba_tbl(struct mwifiex_private *priv, int tid, u8 *ra)
{
	struct mwifiex_tx_ba_stream_tbl *tx_ba_tsr_tbl;

	spin_lock_bh(&priv->tx_ba_stream_tbl_lock);
	list_for_each_entry(tx_ba_tsr_tbl, &priv->tx_ba_stream_tbl_ptr, list) {
		if (ether_addr_equal_unaligned(tx_ba_tsr_tbl->ra, ra) &&
		    tx_ba_tsr_tbl->tid == tid) {
			spin_unlock_bh(&priv->tx_ba_stream_tbl_lock);
			return tx_ba_tsr_tbl;
		}
	}
	spin_unlock_bh(&priv->tx_ba_stream_tbl_lock);
	return NULL;
}

/*
 * This function creates an entry in Tx BA stream table for the
 * given RA/TID pair.
 */
void mwifiex_create_ba_tbl(struct mwifiex_private *priv, u8 *ra, int tid,
			   enum mwifiex_ba_status ba_status)
{
	struct mwifiex_tx_ba_stream_tbl *new_node;
	struct mwifiex_ra_list_tbl *ra_list;
	int tid_down;

	if (!mwifiex_get_ba_tbl(priv, tid, ra)) {
		new_node = kzalloc(sizeof(struct mwifiex_tx_ba_stream_tbl),
				   GFP_ATOMIC);
		if (!new_node)
			return;

		tid_down = mwifiex_wmm_downgrade_tid(priv, tid);
		ra_list = mwifiex_wmm_get_ralist_node(priv, tid_down, ra);
		if (ra_list) {
			ra_list->ba_status = ba_status;
			ra_list->amsdu_in_ampdu = false;
		}
		INIT_LIST_HEAD(&new_node->list);

		new_node->tid = tid;
		new_node->ba_status = ba_status;
		memcpy(new_node->ra, ra, ETH_ALEN);

		spin_lock_bh(&priv->tx_ba_stream_tbl_lock);
		list_add_tail(&new_node->list, &priv->tx_ba_stream_tbl_ptr);
		spin_unlock_bh(&priv->tx_ba_stream_tbl_lock);
	}
}

/*
 * This function sends an add BA request to the given TID/RA pair.
 */
int mwifiex_send_addba(struct mwifiex_private *priv, int tid, u8 *peer_mac)
{
	struct host_cmd_ds_11n_addba_req add_ba_req;
	u32 tx_win_size = priv->add_ba_param.tx_win_size;
	static u8 dialog_tok;
	int ret;
	u16 block_ack_param_set;

	mwifiex_dbg(priv->adapter, CMD, "cmd: %s: tid %d\n", __func__, tid);

	memset(&add_ba_req, 0, sizeof(add_ba_req));

	if ((GET_BSS_ROLE(priv) == MWIFIEX_BSS_ROLE_STA) &&
	    ISSUPP_TDLS_ENABLED(priv->adapter->fw_cap_info) &&
	    priv->adapter->is_hw_11ac_capable &&
	    memcmp(priv->cfg_bssid, peer_mac, ETH_ALEN)) {
		struct mwifiex_sta_node *sta_ptr;

		spin_lock_bh(&priv->sta_list_spinlock);
		sta_ptr = mwifiex_get_sta_entry(priv, peer_mac);
		if (!sta_ptr) {
			spin_unlock_bh(&priv->sta_list_spinlock);
			mwifiex_dbg(priv->adapter, ERROR,
				    "BA setup with unknown TDLS peer %pM!\n",
				    peer_mac);
			return -1;
		}
		if (sta_ptr->is_11ac_enabled)
			tx_win_size = MWIFIEX_11AC_STA_AMPDU_DEF_TXWINSIZE;
		spin_unlock_bh(&priv->sta_list_spinlock);
	}

	block_ack_param_set = (u16)((tid << BLOCKACKPARAM_TID_POS) |
				    tx_win_size << BLOCKACKPARAM_WINSIZE_POS |
				    IMMEDIATE_BLOCK_ACK);

	/* enable AMSDU inside AMPDU */
	if (priv->add_ba_param.tx_amsdu &&
	    (priv->aggr_prio_tbl[tid].amsdu != BA_STREAM_NOT_ALLOWED))
		block_ack_param_set |= BLOCKACKPARAM_AMSDU_SUPP_MASK;

	add_ba_req.block_ack_param_set = cpu_to_le16(block_ack_param_set);
	add_ba_req.block_ack_tmo = cpu_to_le16((u16)priv->add_ba_param.timeout);

	++dialog_tok;

	if (dialog_tok == 0)
		dialog_tok = 1;

	add_ba_req.dialog_token = dialog_tok;
	memcpy(&add_ba_req.peer_mac_addr, peer_mac, ETH_ALEN);

	/* We don't wait for the response of this command */
	ret = mwifiex_send_cmd(priv, HostCmd_CMD_11N_ADDBA_REQ,
			       0, 0, &add_ba_req, false);

	return ret;
}

/*
 * This function sends a delete BA request to the given TID/RA pair.
 */
int mwifiex_send_delba(struct mwifiex_private *priv, int tid, u8 *peer_mac,
		       int initiator)
{
	struct host_cmd_ds_11n_delba delba;
	int ret;
	uint16_t del_ba_param_set;

	memset(&delba, 0, sizeof(delba));

	del_ba_param_set = tid << DELBA_TID_POS;

	if (initiator)
		del_ba_param_set |= IEEE80211_DELBA_PARAM_INITIATOR_MASK;
	else
		del_ba_param_set &= ~IEEE80211_DELBA_PARAM_INITIATOR_MASK;

	delba.del_ba_param_set = cpu_to_le16(del_ba_param_set);
	memcpy(&delba.peer_mac_addr, peer_mac, ETH_ALEN);

	/* We don't wait for the response of this command */
	ret = mwifiex_send_cmd(priv, HostCmd_CMD_11N_DELBA,
			       HostCmd_ACT_GEN_SET, 0, &delba, false);

	return ret;
}

/*
 * This function sends delba to specific tid
 */
void mwifiex_11n_delba(struct mwifiex_private *priv, int tid)
{
	struct mwifiex_rx_reorder_tbl *rx_reor_tbl_ptr;

	spin_lock_bh(&priv->rx_reorder_tbl_lock);
	list_for_each_entry(rx_reor_tbl_ptr, &priv->rx_reorder_tbl_ptr, list) {
		if (rx_reor_tbl_ptr->tid == tid) {
			dev_dbg(priv->adapter->dev,
				"Send delba to tid=%d, %pM\n",
				tid, rx_reor_tbl_ptr->ta);
			mwifiex_send_delba(priv, tid, rx_reor_tbl_ptr->ta, 0);
			goto exit;
		}
	}
exit:
	spin_unlock_bh(&priv->rx_reorder_tbl_lock);
}

/*
 * This function handles the command response of a delete BA request.
 */
void mwifiex_11n_delete_ba_stream(struct mwifiex_private *priv, u8 *del_ba)
{
	struct host_cmd_ds_11n_delba *cmd_del_ba =
		(struct host_cmd_ds_11n_delba *) del_ba;
	uint16_t del_ba_param_set = le16_to_cpu(cmd_del_ba->del_ba_param_set);
	int tid;

	tid = del_ba_param_set >> DELBA_TID_POS;

	mwifiex_del_ba_tbl(priv, tid, cmd_del_ba->peer_mac_addr,
			   TYPE_DELBA_RECEIVE, INITIATOR_BIT(del_ba_param_set));
}

/*
 * This function retrieves the Rx reordering table.
 */
int mwifiex_get_rx_reorder_tbl(struct mwifiex_private *priv,
			       struct mwifiex_ds_rx_reorder_tbl *buf)
{
	int i;
	struct mwifiex_ds_rx_reorder_tbl *rx_reo_tbl = buf;
	struct mwifiex_rx_reorder_tbl *rx_reorder_tbl_ptr;
	int count = 0;

	spin_lock_bh(&priv->rx_reorder_tbl_lock);
	list_for_each_entry(rx_reorder_tbl_ptr, &priv->rx_reorder_tbl_ptr,
			    list) {
		rx_reo_tbl->tid = (u16) rx_reorder_tbl_ptr->tid;
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

		if (count >= MWIFIEX_MAX_RX_BASTREAM_SUPPORTED)
			break;
	}
	spin_unlock_bh(&priv->rx_reorder_tbl_lock);

	return count;
}

/*
 * This function retrieves the Tx BA stream table.
 */
int mwifiex_get_tx_ba_stream_tbl(struct mwifiex_private *priv,
				 struct mwifiex_ds_tx_ba_stream_tbl *buf)
{
	struct mwifiex_tx_ba_stream_tbl *tx_ba_tsr_tbl;
	struct mwifiex_ds_tx_ba_stream_tbl *rx_reo_tbl = buf;
	int count = 0;

	spin_lock_bh(&priv->tx_ba_stream_tbl_lock);
	list_for_each_entry(tx_ba_tsr_tbl, &priv->tx_ba_stream_tbl_ptr, list) {
		rx_reo_tbl->tid = (u16) tx_ba_tsr_tbl->tid;
		mwifiex_dbg(priv->adapter, DATA, "data: %s tid=%d\n",
			    __func__, rx_reo_tbl->tid);
		memcpy(rx_reo_tbl->ra, tx_ba_tsr_tbl->ra, ETH_ALEN);
		rx_reo_tbl->amsdu = tx_ba_tsr_tbl->amsdu;
		rx_reo_tbl++;
		count++;
		if (count >= MWIFIEX_MAX_TX_BASTREAM_SUPPORTED)
			break;
	}
	spin_unlock_bh(&priv->tx_ba_stream_tbl_lock);

	return count;
}

/*
 * This function retrieves the entry for specific tx BA stream table by RA and
 * deletes it.
 */
void mwifiex_del_tx_ba_stream_tbl_by_ra(struct mwifiex_private *priv, u8 *ra)
{
	struct mwifiex_tx_ba_stream_tbl *tbl, *tmp;

	if (!ra)
		return;

	spin_lock_bh(&priv->tx_ba_stream_tbl_lock);
	list_for_each_entry_safe(tbl, tmp, &priv->tx_ba_stream_tbl_ptr, list)
		if (!memcmp(tbl->ra, ra, ETH_ALEN))
			mwifiex_11n_delete_tx_ba_stream_tbl_entry(priv, tbl);
	spin_unlock_bh(&priv->tx_ba_stream_tbl_lock);

	return;
}

/* This function initializes the BlockACK setup information for given
 * mwifiex_private structure.
 */
void mwifiex_set_ba_params(struct mwifiex_private *priv)
{
	priv->add_ba_param.timeout = MWIFIEX_DEFAULT_BLOCK_ACK_TIMEOUT;

	if (GET_BSS_ROLE(priv) == MWIFIEX_BSS_ROLE_UAP) {
		priv->add_ba_param.tx_win_size =
						MWIFIEX_UAP_AMPDU_DEF_TXWINSIZE;
		priv->add_ba_param.rx_win_size =
						MWIFIEX_UAP_AMPDU_DEF_RXWINSIZE;
	} else {
		priv->add_ba_param.tx_win_size =
						MWIFIEX_STA_AMPDU_DEF_TXWINSIZE;
		priv->add_ba_param.rx_win_size =
						MWIFIEX_STA_AMPDU_DEF_RXWINSIZE;
	}

	priv->add_ba_param.tx_amsdu = true;
	priv->add_ba_param.rx_amsdu = true;

	return;
}

u8 mwifiex_get_sec_chan_offset(int chan)
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
		sec_offset = IEEE80211_HT_PARAM_CHA_SEC_BELOW;
		break;
	case 165:
	default:
		sec_offset = IEEE80211_HT_PARAM_CHA_SEC_NONE;
		break;
	}

	return sec_offset;
}

/* This function will send DELBA to entries in the priv's
 * Tx BA stream table
 */
static void
mwifiex_send_delba_txbastream_tbl(struct mwifiex_private *priv, u8 tid)
{
	struct mwifiex_adapter *adapter = priv->adapter;
	struct mwifiex_tx_ba_stream_tbl *tx_ba_stream_tbl_ptr;

	list_for_each_entry(tx_ba_stream_tbl_ptr,
			    &priv->tx_ba_stream_tbl_ptr, list) {
		if (tx_ba_stream_tbl_ptr->ba_status == BA_SETUP_COMPLETE) {
			if (tid == tx_ba_stream_tbl_ptr->tid) {
				dev_dbg(adapter->dev,
					"Tx:Send delba to tid=%d, %pM\n", tid,
					tx_ba_stream_tbl_ptr->ra);
				mwifiex_send_delba(priv,
						   tx_ba_stream_tbl_ptr->tid,
						   tx_ba_stream_tbl_ptr->ra, 1);
				return;
			}
		}
	}
}

/* This function updates all the tx_win_size
 */
void mwifiex_update_ampdu_txwinsize(struct mwifiex_adapter *adapter)
{
	u8 i, j;
	u32 tx_win_size;
	struct mwifiex_private *priv;

	for (i = 0; i < adapter->priv_num; i++) {
		if (!adapter->priv[i])
			continue;
		priv = adapter->priv[i];
		tx_win_size = priv->add_ba_param.tx_win_size;

		if (priv->bss_type == MWIFIEX_BSS_TYPE_STA)
			priv->add_ba_param.tx_win_size =
				MWIFIEX_STA_AMPDU_DEF_TXWINSIZE;

		if (priv->bss_type == MWIFIEX_BSS_TYPE_P2P)
			priv->add_ba_param.tx_win_size =
				MWIFIEX_STA_AMPDU_DEF_TXWINSIZE;

		if (priv->bss_type == MWIFIEX_BSS_TYPE_UAP)
			priv->add_ba_param.tx_win_size =
				MWIFIEX_UAP_AMPDU_DEF_TXWINSIZE;

		if (adapter->coex_win_size) {
			if (adapter->coex_tx_win_size)
				priv->add_ba_param.tx_win_size =
					adapter->coex_tx_win_size;
		}

		if (tx_win_size != priv->add_ba_param.tx_win_size) {
			if (!priv->media_connected)
				continue;
			for (j = 0; j < MAX_NUM_TID; j++)
				mwifiex_send_delba_txbastream_tbl(priv, j);
		}
	}
}
