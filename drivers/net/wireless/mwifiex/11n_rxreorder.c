/*
 * Marvell Wireless LAN device driver: 802.11n RX Re-ordering
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
#include "11n_rxreorder.h"

/*
 * This function dispatches all packets in the Rx reorder table.
 *
 * There could be holes in the buffer, which are skipped by the function.
 * Since the buffer is linear, the function uses rotation to simulate
 * circular buffer.
 */
static void
mwifiex_11n_dispatch_pkt_until_start_win(struct mwifiex_private *priv,
					 struct mwifiex_rx_reorder_tbl
					 *rx_reor_tbl_ptr, int start_win)
{
	int no_pkt_to_send, i;
	void *rx_tmp_ptr;
	unsigned long flags;

	no_pkt_to_send = (start_win > rx_reor_tbl_ptr->start_win) ?
		min((start_win - rx_reor_tbl_ptr->start_win),
		    rx_reor_tbl_ptr->win_size) : rx_reor_tbl_ptr->win_size;

	for (i = 0; i < no_pkt_to_send; ++i) {
		spin_lock_irqsave(&priv->rx_pkt_lock, flags);
		rx_tmp_ptr = NULL;
		if (rx_reor_tbl_ptr->rx_reorder_ptr[i]) {
			rx_tmp_ptr = rx_reor_tbl_ptr->rx_reorder_ptr[i];
			rx_reor_tbl_ptr->rx_reorder_ptr[i] = NULL;
		}
		spin_unlock_irqrestore(&priv->rx_pkt_lock, flags);
		if (rx_tmp_ptr)
			mwifiex_process_rx_packet(priv->adapter, rx_tmp_ptr);
	}

	spin_lock_irqsave(&priv->rx_pkt_lock, flags);
	/*
	 * We don't have a circular buffer, hence use rotation to simulate
	 * circular buffer
	 */
	for (i = 0; i < rx_reor_tbl_ptr->win_size - no_pkt_to_send; ++i) {
		rx_reor_tbl_ptr->rx_reorder_ptr[i] =
			rx_reor_tbl_ptr->rx_reorder_ptr[no_pkt_to_send + i];
		rx_reor_tbl_ptr->rx_reorder_ptr[no_pkt_to_send + i] = NULL;
	}

	rx_reor_tbl_ptr->start_win = start_win;
	spin_unlock_irqrestore(&priv->rx_pkt_lock, flags);
}

/*
 * This function dispatches all packets in the Rx reorder table until
 * a hole is found.
 *
 * The start window is adjusted automatically when a hole is located.
 * Since the buffer is linear, the function uses rotation to simulate
 * circular buffer.
 */
static void
mwifiex_11n_scan_and_dispatch(struct mwifiex_private *priv,
			      struct mwifiex_rx_reorder_tbl *rx_reor_tbl_ptr)
{
	int i, j, xchg;
	void *rx_tmp_ptr;
	unsigned long flags;

	for (i = 0; i < rx_reor_tbl_ptr->win_size; ++i) {
		spin_lock_irqsave(&priv->rx_pkt_lock, flags);
		if (!rx_reor_tbl_ptr->rx_reorder_ptr[i]) {
			spin_unlock_irqrestore(&priv->rx_pkt_lock, flags);
			break;
		}
		rx_tmp_ptr = rx_reor_tbl_ptr->rx_reorder_ptr[i];
		rx_reor_tbl_ptr->rx_reorder_ptr[i] = NULL;
		spin_unlock_irqrestore(&priv->rx_pkt_lock, flags);
		mwifiex_process_rx_packet(priv->adapter, rx_tmp_ptr);
	}

	spin_lock_irqsave(&priv->rx_pkt_lock, flags);
	/*
	 * We don't have a circular buffer, hence use rotation to simulate
	 * circular buffer
	 */
	if (i > 0) {
		xchg = rx_reor_tbl_ptr->win_size - i;
		for (j = 0; j < xchg; ++j) {
			rx_reor_tbl_ptr->rx_reorder_ptr[j] =
				rx_reor_tbl_ptr->rx_reorder_ptr[i + j];
			rx_reor_tbl_ptr->rx_reorder_ptr[i + j] = NULL;
		}
	}
	rx_reor_tbl_ptr->start_win = (rx_reor_tbl_ptr->start_win + i)
		&(MAX_TID_VALUE - 1);
	spin_unlock_irqrestore(&priv->rx_pkt_lock, flags);
}

/*
 * This function deletes the Rx reorder table and frees the memory.
 *
 * The function stops the associated timer and dispatches all the
 * pending packets in the Rx reorder table before deletion.
 */
static void
mwifiex_11n_delete_rx_reorder_tbl_entry(struct mwifiex_private *priv,
				       struct mwifiex_rx_reorder_tbl
				       *rx_reor_tbl_ptr)
{
	unsigned long flags;

	if (!rx_reor_tbl_ptr)
		return;

	mwifiex_11n_dispatch_pkt_until_start_win(priv, rx_reor_tbl_ptr,
						 (rx_reor_tbl_ptr->start_win +
						  rx_reor_tbl_ptr->win_size)
						 &(MAX_TID_VALUE - 1));

	del_timer(&rx_reor_tbl_ptr->timer_context.timer);

	spin_lock_irqsave(&priv->rx_reorder_tbl_lock, flags);
	list_del(&rx_reor_tbl_ptr->list);
	spin_unlock_irqrestore(&priv->rx_reorder_tbl_lock, flags);

	kfree(rx_reor_tbl_ptr->rx_reorder_ptr);
	kfree(rx_reor_tbl_ptr);
}

/*
 * This function returns the pointer to an entry in Rx reordering
 * table which matches the given TA/TID pair.
 */
static struct mwifiex_rx_reorder_tbl *
mwifiex_11n_get_rx_reorder_tbl(struct mwifiex_private *priv, int tid, u8 *ta)
{
	struct mwifiex_rx_reorder_tbl *rx_reor_tbl_ptr;
	unsigned long flags;

	spin_lock_irqsave(&priv->rx_reorder_tbl_lock, flags);
	list_for_each_entry(rx_reor_tbl_ptr, &priv->rx_reorder_tbl_ptr, list) {
		if ((!memcmp(rx_reor_tbl_ptr->ta, ta, ETH_ALEN))
		    && (rx_reor_tbl_ptr->tid == tid)) {
			spin_unlock_irqrestore(&priv->rx_reorder_tbl_lock,
					       flags);
			return rx_reor_tbl_ptr;
		}
	}
	spin_unlock_irqrestore(&priv->rx_reorder_tbl_lock, flags);

	return NULL;
}

/*
 * This function finds the last sequence number used in the packets
 * buffered in Rx reordering table.
 */
static int
mwifiex_11n_find_last_seq_num(struct mwifiex_rx_reorder_tbl *rx_reorder_tbl_ptr)
{
	int i;

	for (i = (rx_reorder_tbl_ptr->win_size - 1); i >= 0; --i)
		if (rx_reorder_tbl_ptr->rx_reorder_ptr[i])
			return i;

	return -1;
}

/*
 * This function flushes all the packets in Rx reordering table.
 *
 * The function checks if any packets are currently buffered in the
 * table or not. In case there are packets available, it dispatches
 * them and then dumps the Rx reordering table.
 */
static void
mwifiex_flush_data(unsigned long context)
{
	struct reorder_tmr_cnxt *reorder_cnxt =
		(struct reorder_tmr_cnxt *) context;
	int start_win;

	start_win = mwifiex_11n_find_last_seq_num(reorder_cnxt->ptr);
	if (start_win >= 0) {
		dev_dbg(reorder_cnxt->priv->adapter->dev,
				"info: flush data %d\n", start_win);
		mwifiex_11n_dispatch_pkt_until_start_win(reorder_cnxt->priv,
				reorder_cnxt->ptr,
				((reorder_cnxt->ptr->start_win +
				  start_win + 1) & (MAX_TID_VALUE - 1)));
	}
}

/*
 * This function creates an entry in Rx reordering table for the
 * given TA/TID.
 *
 * The function also initializes the entry with sequence number, window
 * size as well as initializes the timer.
 *
 * If the received TA/TID pair is already present, all the packets are
 * dispatched and the window size is moved until the SSN.
 */
static void
mwifiex_11n_create_rx_reorder_tbl(struct mwifiex_private *priv, u8 *ta,
				 int tid, int win_size, int seq_num)
{
	int i;
	struct mwifiex_rx_reorder_tbl *rx_reor_tbl_ptr, *new_node;
	u16 last_seq = 0;
	unsigned long flags;

	/*
	 * If we get a TID, ta pair which is already present dispatch all the
	 * the packets and move the window size until the ssn
	 */
	rx_reor_tbl_ptr = mwifiex_11n_get_rx_reorder_tbl(priv, tid, ta);
	if (rx_reor_tbl_ptr) {
		mwifiex_11n_dispatch_pkt_until_start_win(priv, rx_reor_tbl_ptr,
							 seq_num);
		return;
	}
	/* if !rx_reor_tbl_ptr then create one */
	new_node = kzalloc(sizeof(struct mwifiex_rx_reorder_tbl), GFP_KERNEL);
	if (!new_node) {
		dev_err(priv->adapter->dev, "%s: failed to alloc new_node\n",
		       __func__);
		return;
	}

	INIT_LIST_HEAD(&new_node->list);
	new_node->tid = tid;
	memcpy(new_node->ta, ta, ETH_ALEN);
	new_node->start_win = seq_num;
	if (mwifiex_queuing_ra_based(priv))
		/* TODO for adhoc */
		dev_dbg(priv->adapter->dev,
			"info: ADHOC:last_seq=%d start_win=%d\n",
			last_seq, new_node->start_win);
	else
		last_seq = priv->rx_seq[tid];

	if (last_seq >= new_node->start_win)
		new_node->start_win = last_seq + 1;

	new_node->win_size = win_size;

	new_node->rx_reorder_ptr = kzalloc(sizeof(void *) * win_size,
					GFP_KERNEL);
	if (!new_node->rx_reorder_ptr) {
		kfree((u8 *) new_node);
		dev_err(priv->adapter->dev,
			"%s: failed to alloc reorder_ptr\n", __func__);
		return;
	}

	new_node->timer_context.ptr = new_node;
	new_node->timer_context.priv = priv;

	init_timer(&new_node->timer_context.timer);
	new_node->timer_context.timer.function = mwifiex_flush_data;
	new_node->timer_context.timer.data =
			(unsigned long) &new_node->timer_context;

	for (i = 0; i < win_size; ++i)
		new_node->rx_reorder_ptr[i] = NULL;

	spin_lock_irqsave(&priv->rx_reorder_tbl_lock, flags);
	list_add_tail(&new_node->list, &priv->rx_reorder_tbl_ptr);
	spin_unlock_irqrestore(&priv->rx_reorder_tbl_lock, flags);
}

/*
 * This function prepares command for adding a BA request.
 *
 * Preparation includes -
 *      - Setting command ID and proper size
 *      - Setting add BA request buffer
 *      - Ensuring correct endian-ness
 */
int mwifiex_cmd_11n_addba_req(struct host_cmd_ds_command *cmd, void *data_buf)
{
	struct host_cmd_ds_11n_addba_req *add_ba_req =
		(struct host_cmd_ds_11n_addba_req *)
		&cmd->params.add_ba_req;

	cmd->command = cpu_to_le16(HostCmd_CMD_11N_ADDBA_REQ);
	cmd->size = cpu_to_le16(sizeof(*add_ba_req) + S_DS_GEN);
	memcpy(add_ba_req, data_buf, sizeof(*add_ba_req));

	return 0;
}

/*
 * This function prepares command for adding a BA response.
 *
 * Preparation includes -
 *      - Setting command ID and proper size
 *      - Setting add BA response buffer
 *      - Ensuring correct endian-ness
 */
int mwifiex_cmd_11n_addba_rsp_gen(struct mwifiex_private *priv,
				  struct host_cmd_ds_command *cmd,
				  struct host_cmd_ds_11n_addba_req
				  *cmd_addba_req)
{
	struct host_cmd_ds_11n_addba_rsp *add_ba_rsp =
		(struct host_cmd_ds_11n_addba_rsp *)
		&cmd->params.add_ba_rsp;
	u8 tid;
	int win_size;
	uint16_t block_ack_param_set;

	cmd->command = cpu_to_le16(HostCmd_CMD_11N_ADDBA_RSP);
	cmd->size = cpu_to_le16(sizeof(*add_ba_rsp) + S_DS_GEN);

	memcpy(add_ba_rsp->peer_mac_addr, cmd_addba_req->peer_mac_addr,
	       ETH_ALEN);
	add_ba_rsp->dialog_token = cmd_addba_req->dialog_token;
	add_ba_rsp->block_ack_tmo = cmd_addba_req->block_ack_tmo;
	add_ba_rsp->ssn = cmd_addba_req->ssn;

	block_ack_param_set = le16_to_cpu(cmd_addba_req->block_ack_param_set);
	tid = (block_ack_param_set & IEEE80211_ADDBA_PARAM_TID_MASK)
		>> BLOCKACKPARAM_TID_POS;
	add_ba_rsp->status_code = cpu_to_le16(ADDBA_RSP_STATUS_ACCEPT);
	block_ack_param_set &= ~IEEE80211_ADDBA_PARAM_BUF_SIZE_MASK;
	/* We donot support AMSDU inside AMPDU, hence reset the bit */
	block_ack_param_set &= ~BLOCKACKPARAM_AMSDU_SUPP_MASK;
	block_ack_param_set |= (priv->add_ba_param.rx_win_size <<
					     BLOCKACKPARAM_WINSIZE_POS);
	add_ba_rsp->block_ack_param_set = cpu_to_le16(block_ack_param_set);
	win_size = (le16_to_cpu(add_ba_rsp->block_ack_param_set)
					& IEEE80211_ADDBA_PARAM_BUF_SIZE_MASK)
					>> BLOCKACKPARAM_WINSIZE_POS;
	cmd_addba_req->block_ack_param_set = cpu_to_le16(block_ack_param_set);

	mwifiex_11n_create_rx_reorder_tbl(priv, cmd_addba_req->peer_mac_addr,
			    tid, win_size, le16_to_cpu(cmd_addba_req->ssn));
	return 0;
}

/*
 * This function prepares command for deleting a BA request.
 *
 * Preparation includes -
 *      - Setting command ID and proper size
 *      - Setting del BA request buffer
 *      - Ensuring correct endian-ness
 */
int mwifiex_cmd_11n_delba(struct host_cmd_ds_command *cmd, void *data_buf)
{
	struct host_cmd_ds_11n_delba *del_ba = (struct host_cmd_ds_11n_delba *)
		&cmd->params.del_ba;

	cmd->command = cpu_to_le16(HostCmd_CMD_11N_DELBA);
	cmd->size = cpu_to_le16(sizeof(*del_ba) + S_DS_GEN);
	memcpy(del_ba, data_buf, sizeof(*del_ba));

	return 0;
}

/*
 * This function identifies if Rx reordering is needed for a received packet.
 *
 * In case reordering is required, the function will do the reordering
 * before sending it to kernel.
 *
 * The Rx reorder table is checked first with the received TID/TA pair. If
 * not found, the received packet is dispatched immediately. But if found,
 * the packet is reordered and all the packets in the updated Rx reordering
 * table is dispatched until a hole is found.
 *
 * For sequence number less than the starting window, the packet is dropped.
 */
int mwifiex_11n_rx_reorder_pkt(struct mwifiex_private *priv,
				u16 seq_num, u16 tid,
				u8 *ta, u8 pkt_type, void *payload)
{
	struct mwifiex_rx_reorder_tbl *rx_reor_tbl_ptr;
	int start_win, end_win, win_size;
	u16 pkt_index;

	rx_reor_tbl_ptr =
		mwifiex_11n_get_rx_reorder_tbl((struct mwifiex_private *) priv,
						tid, ta);
	if (!rx_reor_tbl_ptr) {
		if (pkt_type != PKT_TYPE_BAR)
			mwifiex_process_rx_packet(priv->adapter, payload);
		return 0;
	}
	start_win = rx_reor_tbl_ptr->start_win;
	win_size = rx_reor_tbl_ptr->win_size;
	end_win = ((start_win + win_size) - 1) & (MAX_TID_VALUE - 1);
	del_timer(&rx_reor_tbl_ptr->timer_context.timer);
	mod_timer(&rx_reor_tbl_ptr->timer_context.timer, jiffies
			+ (MIN_FLUSH_TIMER_MS * win_size * HZ) / 1000);

	/*
	 * If seq_num is less then starting win then ignore and drop the
	 * packet
	 */
	if ((start_win + TWOPOW11) > (MAX_TID_VALUE - 1)) {/* Wrap */
		if (seq_num >= ((start_win + (TWOPOW11)) & (MAX_TID_VALUE - 1))
				&& (seq_num < start_win))
			return -1;
	} else if ((seq_num < start_win)
			|| (seq_num > (start_win + (TWOPOW11)))) {
		return -1;
	}

	/*
	 * If this packet is a BAR we adjust seq_num as
	 * WinStart = seq_num
	 */
	if (pkt_type == PKT_TYPE_BAR)
		seq_num = ((seq_num + win_size) - 1) & (MAX_TID_VALUE - 1);

	if (((end_win < start_win)
	     && (seq_num < (TWOPOW11 - (MAX_TID_VALUE - start_win)))
	     && (seq_num > end_win)) || ((end_win > start_win)
	     && ((seq_num > end_win) || (seq_num < start_win)))) {
		end_win = seq_num;
		if (((seq_num - win_size) + 1) >= 0)
			start_win = (end_win - win_size) + 1;
		else
			start_win = (MAX_TID_VALUE - (win_size - seq_num)) + 1;
		mwifiex_11n_dispatch_pkt_until_start_win(priv,
						rx_reor_tbl_ptr, start_win);
	}

	if (pkt_type != PKT_TYPE_BAR) {
		if (seq_num >= start_win)
			pkt_index = seq_num - start_win;
		else
			pkt_index = (seq_num+MAX_TID_VALUE) - start_win;

		if (rx_reor_tbl_ptr->rx_reorder_ptr[pkt_index])
			return -1;

		rx_reor_tbl_ptr->rx_reorder_ptr[pkt_index] = payload;
	}

	/*
	 * Dispatch all packets sequentially from start_win until a
	 * hole is found and adjust the start_win appropriately
	 */
	mwifiex_11n_scan_and_dispatch(priv, rx_reor_tbl_ptr);

	return 0;
}

/*
 * This function deletes an entry for a given TID/TA pair.
 *
 * The TID/TA are taken from del BA event body.
 */
void
mwifiex_11n_delete_ba_stream_tbl(struct mwifiex_private *priv, int tid,
				u8 *peer_mac, u8 type, int initiator)
{
	struct mwifiex_rx_reorder_tbl *rx_reor_tbl_ptr;
	struct mwifiex_tx_ba_stream_tbl *ptx_tbl;
	u8 cleanup_rx_reorder_tbl;
	unsigned long flags;

	if (type == TYPE_DELBA_RECEIVE)
		cleanup_rx_reorder_tbl = (initiator) ? true : false;
	else
		cleanup_rx_reorder_tbl = (initiator) ? false : true;

	dev_dbg(priv->adapter->dev, "event: DELBA: %pM tid=%d, "
	       "initiator=%d\n", peer_mac, tid, initiator);

	if (cleanup_rx_reorder_tbl) {
		rx_reor_tbl_ptr = mwifiex_11n_get_rx_reorder_tbl(priv, tid,
								 peer_mac);
		if (!rx_reor_tbl_ptr) {
			dev_dbg(priv->adapter->dev,
					"event: TID, TA not found in table\n");
			return;
		}
		mwifiex_11n_delete_rx_reorder_tbl_entry(priv, rx_reor_tbl_ptr);
	} else {
		ptx_tbl = mwifiex_11n_get_tx_ba_stream_tbl(priv, tid, peer_mac);
		if (!ptx_tbl) {
			dev_dbg(priv->adapter->dev,
					"event: TID, RA not found in table\n");
			return;
		}

		spin_lock_irqsave(&priv->tx_ba_stream_tbl_lock, flags);
		mwifiex_11n_delete_tx_ba_stream_tbl_entry(priv, ptx_tbl);
		spin_unlock_irqrestore(&priv->tx_ba_stream_tbl_lock, flags);
	}
}

/*
 * This function handles the command response of an add BA response.
 *
 * Handling includes changing the header fields into CPU format and
 * creating the stream, provided the add BA is accepted.
 */
int mwifiex_ret_11n_addba_resp(struct mwifiex_private *priv,
			       struct host_cmd_ds_command *resp)
{
	struct host_cmd_ds_11n_addba_rsp *add_ba_rsp =
		(struct host_cmd_ds_11n_addba_rsp *)
		&resp->params.add_ba_rsp;
	int tid, win_size;
	struct mwifiex_rx_reorder_tbl *rx_reor_tbl_ptr;
	uint16_t block_ack_param_set;

	block_ack_param_set = le16_to_cpu(add_ba_rsp->block_ack_param_set);

	tid = (block_ack_param_set & IEEE80211_ADDBA_PARAM_TID_MASK)
		>> BLOCKACKPARAM_TID_POS;
	/*
	 * Check if we had rejected the ADDBA, if yes then do not create
	 * the stream
	 */
	if (le16_to_cpu(add_ba_rsp->status_code) == BA_RESULT_SUCCESS) {
		win_size = (block_ack_param_set &
			IEEE80211_ADDBA_PARAM_BUF_SIZE_MASK)
			>> BLOCKACKPARAM_WINSIZE_POS;

		dev_dbg(priv->adapter->dev, "cmd: ADDBA RSP: %pM"
		       " tid=%d ssn=%d win_size=%d\n",
		       add_ba_rsp->peer_mac_addr,
		       tid, add_ba_rsp->ssn, win_size);
	} else {
		dev_err(priv->adapter->dev, "ADDBA RSP: failed %pM tid=%d)\n",
					add_ba_rsp->peer_mac_addr, tid);

		rx_reor_tbl_ptr = mwifiex_11n_get_rx_reorder_tbl(priv,
					tid, add_ba_rsp->peer_mac_addr);
		if (rx_reor_tbl_ptr)
			mwifiex_11n_delete_rx_reorder_tbl_entry(priv,
				rx_reor_tbl_ptr);
	}

	return 0;
}

/*
 * This function handles BA stream timeout event by preparing and sending
 * a command to the firmware.
 */
void mwifiex_11n_ba_stream_timeout(struct mwifiex_private *priv,
				   struct host_cmd_ds_11n_batimeout *event)
{
	struct host_cmd_ds_11n_delba delba;

	memset(&delba, 0, sizeof(struct host_cmd_ds_11n_delba));
	memcpy(delba.peer_mac_addr, event->peer_mac_addr, ETH_ALEN);

	delba.del_ba_param_set |=
		cpu_to_le16((u16) event->tid << DELBA_TID_POS);
	delba.del_ba_param_set |= cpu_to_le16(
		(u16) event->origninator << DELBA_INITIATOR_POS);
	delba.reason_code = cpu_to_le16(WLAN_REASON_QSTA_TIMEOUT);
	mwifiex_send_cmd_async(priv, HostCmd_CMD_11N_DELBA, 0, 0, &delba);
}

/*
 * This function cleans up the Rx reorder table by deleting all the entries
 * and re-initializing.
 */
void mwifiex_11n_cleanup_reorder_tbl(struct mwifiex_private *priv)
{
	struct mwifiex_rx_reorder_tbl *del_tbl_ptr, *tmp_node;
	unsigned long flags;

	spin_lock_irqsave(&priv->rx_reorder_tbl_lock, flags);
	list_for_each_entry_safe(del_tbl_ptr, tmp_node,
				 &priv->rx_reorder_tbl_ptr, list) {
		spin_unlock_irqrestore(&priv->rx_reorder_tbl_lock, flags);
		mwifiex_11n_delete_rx_reorder_tbl_entry(priv, del_tbl_ptr);
		spin_lock_irqsave(&priv->rx_reorder_tbl_lock, flags);
	}
	spin_unlock_irqrestore(&priv->rx_reorder_tbl_lock, flags);

	INIT_LIST_HEAD(&priv->rx_reorder_tbl_ptr);
	memset(priv->rx_seq, 0, sizeof(priv->rx_seq));
}
