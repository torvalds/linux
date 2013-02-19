/*
 * Marvell Wireless LAN device driver: 802.11n
 *
 * Copyright (C) 2011, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
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
void
mwifiex_fill_cap_info(struct mwifiex_private *priv, u8 radio_type,
		      struct mwifiex_ie_types_htcap *ht_cap)
{
	uint16_t ht_ext_cap = le16_to_cpu(ht_cap->ht_cap.extended_ht_cap_info);
	struct ieee80211_supported_band *sband =
					priv->wdev->wiphy->bands[radio_type];

	ht_cap->ht_cap.ampdu_params_info =
		(sband->ht_cap.ampdu_factor &
		 IEEE80211_HT_AMPDU_PARM_FACTOR) |
		((sband->ht_cap.ampdu_density <<
		 IEEE80211_HT_AMPDU_PARM_DENSITY_SHIFT) &
		 IEEE80211_HT_AMPDU_PARM_DENSITY);

	memcpy((u8 *) &ht_cap->ht_cap.mcs, &sband->ht_cap.mcs,
	       sizeof(sband->ht_cap.mcs));

	if (priv->bss_mode == NL80211_IFTYPE_STATION ||
	    (sband->ht_cap.cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40 &&
	     (priv->adapter->sec_chan_offset !=
					IEEE80211_HT_PARAM_CHA_SEC_NONE)))
		/* Set MCS32 for infra mode or ad-hoc mode with 40MHz support */
		SETHT_MCS32(ht_cap->ht_cap.mcs.rx_mask);

	/* Clear RD responder bit */
	ht_ext_cap &= ~IEEE80211_HT_EXT_CAP_RD_RESPONDER;

	ht_cap->ht_cap.cap_info = cpu_to_le16(sband->ht_cap.cap);
	ht_cap->ht_cap.extended_ht_cap_info = cpu_to_le16(ht_ext_cap);
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
	unsigned long flags;

	spin_lock_irqsave(&priv->tx_ba_stream_tbl_lock, flags);
	list_for_each_entry(tx_ba_tsr_tbl, &priv->tx_ba_stream_tbl_ptr, list) {
		if (tx_ba_tsr_tbl->ba_status == ba_status) {
			spin_unlock_irqrestore(&priv->tx_ba_stream_tbl_lock,
					       flags);
			return tx_ba_tsr_tbl;
		}
	}
	spin_unlock_irqrestore(&priv->tx_ba_stream_tbl_lock, flags);
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
		  * we initiated the ADDBA
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
	int tid;
	struct host_cmd_ds_11n_addba_rsp *add_ba_rsp = &resp->params.add_ba_rsp;
	struct mwifiex_tx_ba_stream_tbl *tx_ba_tbl;

	add_ba_rsp->ssn = cpu_to_le16((le16_to_cpu(add_ba_rsp->ssn))
			& SSN_MASK);

	tid = (le16_to_cpu(add_ba_rsp->block_ack_param_set)
		& IEEE80211_ADDBA_PARAM_TID_MASK)
		>> BLOCKACKPARAM_TID_POS;
	if (le16_to_cpu(add_ba_rsp->status_code) == BA_RESULT_SUCCESS) {
		tx_ba_tbl = mwifiex_get_ba_tbl(priv, tid,
						add_ba_rsp->peer_mac_addr);
		if (tx_ba_tbl) {
			dev_dbg(priv->adapter->dev, "info: BA stream complete\n");
			tx_ba_tbl->ba_status = BA_SETUP_COMPLETE;
		} else {
			dev_err(priv->adapter->dev, "BA stream not created\n");
		}
	} else {
		mwifiex_del_ba_tbl(priv, tid, add_ba_rsp->peer_mac_addr,
				   TYPE_DELBA_SENT, true);
		if (add_ba_rsp->add_rsp_result != BA_RESULT_TIMEOUT)
			priv->aggr_prio_tbl[tid].ampdu_ap =
				BA_STREAM_NOT_ALLOWED;
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
		dev_dbg(priv->adapter->dev, "cmd: set tx_buf=%d\n", *buf_size);
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
	u8 radio_type;

	if (!buffer || !*buffer)
		return ret_len;

	radio_type = mwifiex_band_to_radio_type((u8) bss_desc->bss_band);
	sband = priv->wdev->wiphy->bands[radio_type];

	if (bss_desc->bcn_ht_cap) {
		ht_cap = (struct mwifiex_ie_types_htcap *) *buffer;
		memset(ht_cap, 0, sizeof(struct mwifiex_ie_types_htcap));
		ht_cap->header.type = cpu_to_le16(WLAN_EID_HT_CAPABILITY);
		ht_cap->header.len =
				cpu_to_le16(sizeof(struct ieee80211_ht_cap));
		memcpy((u8 *) ht_cap + sizeof(struct mwifiex_ie_types_header),
		       (u8 *) bss_desc->bcn_ht_cap +
		       sizeof(struct ieee_types_header),
		       le16_to_cpu(ht_cap->header.len));

		mwifiex_fill_cap_info(priv, radio_type, ht_cap);

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
			       (u8 *) bss_desc->bcn_ht_oper +
			       sizeof(struct ieee_types_header),
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
		ext_cap = (struct mwifiex_ie_types_extcap *) *buffer;
		memset(ext_cap, 0, sizeof(struct mwifiex_ie_types_extcap));
		ext_cap->header.type = cpu_to_le16(WLAN_EID_EXT_CAPABILITY);
		ext_cap->header.len = cpu_to_le16(sizeof(ext_cap->ext_cap));

		memcpy((u8 *)ext_cap + sizeof(struct mwifiex_ie_types_header),
		       bss_desc->bcn_ext_cap + sizeof(struct ieee_types_header),
		       le16_to_cpu(ext_cap->header.len));

		*buffer += sizeof(struct mwifiex_ie_types_extcap);
		ret_len += sizeof(struct mwifiex_ie_types_extcap);
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

	dev_dbg(priv->adapter->dev, "info: tx_ba_tsr_tbl %p\n", tx_ba_tsr_tbl);

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
	unsigned long flags;

	spin_lock_irqsave(&priv->tx_ba_stream_tbl_lock, flags);
	list_for_each_entry_safe(del_tbl_ptr, tmp_node,
				 &priv->tx_ba_stream_tbl_ptr, list)
		mwifiex_11n_delete_tx_ba_stream_tbl_entry(priv, del_tbl_ptr);
	spin_unlock_irqrestore(&priv->tx_ba_stream_tbl_lock, flags);

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
	unsigned long flags;

	spin_lock_irqsave(&priv->tx_ba_stream_tbl_lock, flags);
	list_for_each_entry(tx_ba_tsr_tbl, &priv->tx_ba_stream_tbl_ptr, list) {
		if (!memcmp(tx_ba_tsr_tbl->ra, ra, ETH_ALEN) &&
		    tx_ba_tsr_tbl->tid == tid) {
			spin_unlock_irqrestore(&priv->tx_ba_stream_tbl_lock,
					       flags);
			return tx_ba_tsr_tbl;
		}
	}
	spin_unlock_irqrestore(&priv->tx_ba_stream_tbl_lock, flags);
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
	unsigned long flags;

	if (!mwifiex_get_ba_tbl(priv, tid, ra)) {
		new_node = kzalloc(sizeof(struct mwifiex_tx_ba_stream_tbl),
				   GFP_ATOMIC);
		if (!new_node)
			return;

		INIT_LIST_HEAD(&new_node->list);

		new_node->tid = tid;
		new_node->ba_status = ba_status;
		memcpy(new_node->ra, ra, ETH_ALEN);

		spin_lock_irqsave(&priv->tx_ba_stream_tbl_lock, flags);
		list_add_tail(&new_node->list, &priv->tx_ba_stream_tbl_ptr);
		spin_unlock_irqrestore(&priv->tx_ba_stream_tbl_lock, flags);
	}
}

/*
 * This function sends an add BA request to the given TID/RA pair.
 */
int mwifiex_send_addba(struct mwifiex_private *priv, int tid, u8 *peer_mac)
{
	struct host_cmd_ds_11n_addba_req add_ba_req;
	static u8 dialog_tok;
	int ret;

	dev_dbg(priv->adapter->dev, "cmd: %s: tid %d\n", __func__, tid);

	add_ba_req.block_ack_param_set = cpu_to_le16(
		(u16) ((tid << BLOCKACKPARAM_TID_POS) |
			 (priv->add_ba_param.
			  tx_win_size << BLOCKACKPARAM_WINSIZE_POS) |
			 IMMEDIATE_BLOCK_ACK));
	add_ba_req.block_ack_tmo = cpu_to_le16((u16)priv->add_ba_param.timeout);

	++dialog_tok;

	if (dialog_tok == 0)
		dialog_tok = 1;

	add_ba_req.dialog_token = dialog_tok;
	memcpy(&add_ba_req.peer_mac_addr, peer_mac, ETH_ALEN);

	/* We don't wait for the response of this command */
	ret = mwifiex_send_cmd_async(priv, HostCmd_CMD_11N_ADDBA_REQ,
				     0, 0, &add_ba_req);

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
	delba.del_ba_param_set = cpu_to_le16(tid << DELBA_TID_POS);

	del_ba_param_set = le16_to_cpu(delba.del_ba_param_set);
	if (initiator)
		del_ba_param_set |= IEEE80211_DELBA_PARAM_INITIATOR_MASK;
	else
		del_ba_param_set &= ~IEEE80211_DELBA_PARAM_INITIATOR_MASK;

	memcpy(&delba.peer_mac_addr, peer_mac, ETH_ALEN);

	/* We don't wait for the response of this command */
	ret = mwifiex_send_cmd_async(priv, HostCmd_CMD_11N_DELBA,
				     HostCmd_ACT_GEN_SET, 0, &delba);

	return ret;
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
	unsigned long flags;

	spin_lock_irqsave(&priv->rx_reorder_tbl_lock, flags);
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
	spin_unlock_irqrestore(&priv->rx_reorder_tbl_lock, flags);

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
	unsigned long flags;

	spin_lock_irqsave(&priv->tx_ba_stream_tbl_lock, flags);
	list_for_each_entry(tx_ba_tsr_tbl, &priv->tx_ba_stream_tbl_ptr, list) {
		rx_reo_tbl->tid = (u16) tx_ba_tsr_tbl->tid;
		dev_dbg(priv->adapter->dev, "data: %s tid=%d\n",
			__func__, rx_reo_tbl->tid);
		memcpy(rx_reo_tbl->ra, tx_ba_tsr_tbl->ra, ETH_ALEN);
		rx_reo_tbl++;
		count++;
		if (count >= MWIFIEX_MAX_TX_BASTREAM_SUPPORTED)
			break;
	}
	spin_unlock_irqrestore(&priv->tx_ba_stream_tbl_lock, flags);

	return count;
}

/*
 * This function retrieves the entry for specific tx BA stream table by RA and
 * deletes it.
 */
void mwifiex_del_tx_ba_stream_tbl_by_ra(struct mwifiex_private *priv, u8 *ra)
{
	struct mwifiex_tx_ba_stream_tbl *tbl, *tmp;
	unsigned long flags;

	if (!ra)
		return;

	spin_lock_irqsave(&priv->tx_ba_stream_tbl_lock, flags);
	list_for_each_entry_safe(tbl, tmp, &priv->tx_ba_stream_tbl_ptr, list) {
		if (!memcmp(tbl->ra, ra, ETH_ALEN)) {
			spin_unlock_irqrestore(&priv->tx_ba_stream_tbl_lock,
					       flags);
			mwifiex_11n_delete_tx_ba_stream_tbl_entry(priv, tbl);
			spin_lock_irqsave(&priv->tx_ba_stream_tbl_lock, flags);
		}
	}
	spin_unlock_irqrestore(&priv->tx_ba_stream_tbl_lock, flags);

	return;
}
