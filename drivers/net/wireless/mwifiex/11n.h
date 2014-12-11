/*
 * Marvell Wireless LAN device driver: 802.11n
 *
 * Copyright (C) 2011-2014, Marvell International Ltd.
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

#ifndef _MWIFIEX_11N_H_
#define _MWIFIEX_11N_H_

#include "11n_aggr.h"
#include "11n_rxreorder.h"
#include "wmm.h"

int mwifiex_ret_11n_delba(struct mwifiex_private *priv,
			  struct host_cmd_ds_command *resp);
int mwifiex_ret_11n_addba_req(struct mwifiex_private *priv,
			      struct host_cmd_ds_command *resp);
int mwifiex_cmd_11n_cfg(struct mwifiex_private *priv,
			struct host_cmd_ds_command *cmd, u16 cmd_action,
			struct mwifiex_ds_11n_tx_cfg *txcfg);
int mwifiex_cmd_append_11n_tlv(struct mwifiex_private *priv,
			       struct mwifiex_bssdescriptor *bss_desc,
			       u8 **buffer);
int mwifiex_fill_cap_info(struct mwifiex_private *, u8 radio_type,
			  struct ieee80211_ht_cap *);
int mwifiex_set_get_11n_htcap_cfg(struct mwifiex_private *priv,
				  u16 action, int *htcap_cfg);
void mwifiex_11n_delete_tx_ba_stream_tbl_entry(struct mwifiex_private *priv,
					     struct mwifiex_tx_ba_stream_tbl
					     *tx_tbl);
void mwifiex_11n_delete_all_tx_ba_stream_tbl(struct mwifiex_private *priv);
struct mwifiex_tx_ba_stream_tbl *mwifiex_get_ba_tbl(struct
							     mwifiex_private
							     *priv, int tid,
							     u8 *ra);
void mwifiex_create_ba_tbl(struct mwifiex_private *priv, u8 *ra, int tid,
			   enum mwifiex_ba_status ba_status);
int mwifiex_send_addba(struct mwifiex_private *priv, int tid, u8 *peer_mac);
int mwifiex_send_delba(struct mwifiex_private *priv, int tid, u8 *peer_mac,
		       int initiator);
void mwifiex_11n_delete_ba_stream(struct mwifiex_private *priv, u8 *del_ba);
int mwifiex_get_rx_reorder_tbl(struct mwifiex_private *priv,
			      struct mwifiex_ds_rx_reorder_tbl *buf);
int mwifiex_get_tx_ba_stream_tbl(struct mwifiex_private *priv,
			       struct mwifiex_ds_tx_ba_stream_tbl *buf);
int mwifiex_cmd_recfg_tx_buf(struct mwifiex_private *priv,
			     struct host_cmd_ds_command *cmd,
			     int cmd_action, u16 *buf_size);
int mwifiex_cmd_amsdu_aggr_ctrl(struct host_cmd_ds_command *cmd,
				int cmd_action,
				struct mwifiex_ds_11n_amsdu_aggr_ctrl *aa_ctrl);
void mwifiex_del_tx_ba_stream_tbl_by_ra(struct mwifiex_private *priv, u8 *ra);
u8 mwifiex_get_sec_chan_offset(int chan);

static inline u8
mwifiex_is_station_ampdu_allowed(struct mwifiex_private *priv,
				 struct mwifiex_ra_list_tbl *ptr, int tid)
{
	struct mwifiex_sta_node *node = mwifiex_get_sta_entry(priv, ptr->ra);

	if (unlikely(!node))
		return false;

	return (node->ampdu_sta[tid] != BA_STREAM_NOT_ALLOWED) ? true : false;
}

/* This function checks whether AMSDU is allowed for BA stream. */
static inline u8
mwifiex_is_amsdu_in_ampdu_allowed(struct mwifiex_private *priv,
				  struct mwifiex_ra_list_tbl *ptr, int tid)
{
	struct mwifiex_tx_ba_stream_tbl *tx_tbl;

	if (is_broadcast_ether_addr(ptr->ra))
		return false;
	tx_tbl = mwifiex_get_ba_tbl(priv, tid, ptr->ra);
	if (tx_tbl)
		return tx_tbl->amsdu;

	return false;
}

/* This function checks whether AMPDU is allowed or not for a particular TID. */
static inline u8
mwifiex_is_ampdu_allowed(struct mwifiex_private *priv,
			 struct mwifiex_ra_list_tbl *ptr, int tid)
{
	if (is_broadcast_ether_addr(ptr->ra))
		return false;
	if (GET_BSS_ROLE(priv) == MWIFIEX_BSS_ROLE_UAP) {
		return mwifiex_is_station_ampdu_allowed(priv, ptr, tid);
	} else {
		if (ptr->tdls_link)
			return mwifiex_is_station_ampdu_allowed(priv, ptr, tid);

		return (priv->aggr_prio_tbl[tid].ampdu_ap !=
			BA_STREAM_NOT_ALLOWED) ? true : false;
	}
}

/*
 * This function checks whether AMSDU is allowed or not for a particular TID.
 */
static inline u8
mwifiex_is_amsdu_allowed(struct mwifiex_private *priv, int tid)
{
	return (((priv->aggr_prio_tbl[tid].amsdu != BA_STREAM_NOT_ALLOWED) &&
		 (priv->is_data_rate_auto || !(priv->bitmap_rates[2] & 0x03)))
		? true : false);
}

/*
 * This function checks whether a space is available for new BA stream or not.
 */
static inline u8 mwifiex_space_avail_for_new_ba_stream(
					struct mwifiex_adapter *adapter)
{
	struct mwifiex_private *priv;
	u8 i;
	u32 ba_stream_num = 0;

	for (i = 0; i < adapter->priv_num; i++) {
		priv = adapter->priv[i];
		if (priv)
			ba_stream_num += mwifiex_wmm_list_len(
				&priv->tx_ba_stream_tbl_ptr);
	}

	return ((ba_stream_num <
		 MWIFIEX_MAX_TX_BASTREAM_SUPPORTED) ? true : false);
}

/*
 * This function finds the correct Tx BA stream to delete.
 *
 * Upon successfully locating, both the TID and the RA are returned.
 */
static inline u8
mwifiex_find_stream_to_delete(struct mwifiex_private *priv, int ptr_tid,
			      int *ptid, u8 *ra)
{
	int tid;
	u8 ret = false;
	struct mwifiex_tx_ba_stream_tbl *tx_tbl;
	unsigned long flags;

	tid = priv->aggr_prio_tbl[ptr_tid].ampdu_user;

	spin_lock_irqsave(&priv->tx_ba_stream_tbl_lock, flags);
	list_for_each_entry(tx_tbl, &priv->tx_ba_stream_tbl_ptr, list) {
		if (tid > priv->aggr_prio_tbl[tx_tbl->tid].ampdu_user) {
			tid = priv->aggr_prio_tbl[tx_tbl->tid].ampdu_user;
			*ptid = tx_tbl->tid;
			memcpy(ra, tx_tbl->ra, ETH_ALEN);
			ret = true;
		}
	}
	spin_unlock_irqrestore(&priv->tx_ba_stream_tbl_lock, flags);

	return ret;
}

/*
 * This function checks whether BA stream is set up or not.
 */
static inline int
mwifiex_is_ba_stream_setup(struct mwifiex_private *priv,
			   struct mwifiex_ra_list_tbl *ptr, int tid)
{
	struct mwifiex_tx_ba_stream_tbl *tx_tbl;

	tx_tbl = mwifiex_get_ba_tbl(priv, tid, ptr->ra);
	if (tx_tbl && IS_BASTREAM_SETUP(tx_tbl))
		return true;

	return false;
}

/*
 * This function checks whether associated station is 11n enabled
 */
static inline int mwifiex_is_sta_11n_enabled(struct mwifiex_private *priv,
					     struct mwifiex_sta_node *node)
{

	if (!node || (priv->bss_role != MWIFIEX_BSS_ROLE_UAP) ||
	    !priv->ap_11n_enabled)
		return 0;

	return node->is_11n_enabled;
}

static inline u8
mwifiex_tdls_peer_11n_enabled(struct mwifiex_private *priv, const u8 *ra)
{
	struct mwifiex_sta_node *node = mwifiex_get_sta_entry(priv, ra);
	if (node)
		return node->is_11n_enabled;

	return false;
}
#endif /* !_MWIFIEX_11N_H_ */
