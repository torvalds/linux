// SPDX-License-Identifier: GPL-2.0-only
/*
 * nxpwifi: 802.11n RX Re-ordering
 *
 * Copyright 2011-2024 NXP
 */

#include "cfg.h"
#include "util.h"
#include "fw.h"
#include "main.h"
#include "cmdevt.h"
#include "wmm.h"
#include "11n.h"
#include "11n_rxreorder.h"
/* Dispatch A-MSDU to stack. */
static int nxpwifi_11n_dispatch_amsdu_pkt(struct nxpwifi_private *priv,
					  struct sk_buff *skb)
{
	struct rxpd *local_rx_pd = (struct rxpd *)(skb->data);
	int ret;

	if (le16_to_cpu(local_rx_pd->rx_pkt_type) == PKT_TYPE_AMSDU) {
		struct sk_buff_head list;
		struct sk_buff *rx_skb;

		__skb_queue_head_init(&list);

		skb_pull(skb, le16_to_cpu(local_rx_pd->rx_pkt_offset));
		skb_trim(skb, le16_to_cpu(local_rx_pd->rx_pkt_length));

		ieee80211_amsdu_to_8023s(skb, &list, priv->curr_addr,
					 priv->wdev.iftype, 0, NULL, NULL, false);

		while (!skb_queue_empty(&list)) {
			rx_skb = __skb_dequeue(&list);

			if (priv->bss_role == NXPWIFI_BSS_ROLE_UAP)
				ret = nxpwifi_uap_recv_packet(priv, rx_skb);
			else
				ret = nxpwifi_recv_packet(priv, rx_skb);
			if (ret)
				nxpwifi_dbg(priv->adapter, ERROR,
					    "Rx of A-MSDU failed");
		}
		return 0;
	}

	return -EINVAL;
}

/* Process RX packet and forward to stack. */
static int nxpwifi_11n_dispatch_pkt(struct nxpwifi_private *priv,
				    struct sk_buff *payload)
{
	int ret;

	if (!payload) {
		nxpwifi_dbg(priv->adapter, INFO, "info: fw drop data\n");
		return 0;
	}

	ret = nxpwifi_11n_dispatch_amsdu_pkt(priv, payload);
	if (!ret)
		return 0;

	if (priv->bss_role == NXPWIFI_BSS_ROLE_UAP)
		return nxpwifi_handle_uap_rx_forward(priv, payload);

	return nxpwifi_process_rx_packet(priv, payload);
}

/* Dispatch packets up to start_win. */
static void
nxpwifi_11n_dispatch_pkt_until_start_win(struct nxpwifi_private *priv,
					 struct nxpwifi_rx_reorder_tbl *tbl,
					 int start_win)
{
	struct sk_buff_head list;
	struct sk_buff *skb;
	int pkt_to_send, i, tid;

	tid = tbl->tid;
	__skb_queue_head_init(&list);
	spin_lock_bh(&priv->rx_reorder_tbl_lock[tid]);

	pkt_to_send = (start_win > tbl->start_win) ?
		      min((start_win - tbl->start_win), tbl->win_size) :
		      tbl->win_size;

	for (i = 0; i < pkt_to_send; ++i) {
		if (tbl->rx_reorder_ptr[i]) {
			skb = tbl->rx_reorder_ptr[i];
			__skb_queue_tail(&list, skb);
			tbl->rx_reorder_ptr[i] = NULL;
		}
	}

	/* Simulate circular buffer via rotation. */
	for (i = 0; i < tbl->win_size - pkt_to_send; ++i) {
		tbl->rx_reorder_ptr[i] = tbl->rx_reorder_ptr[pkt_to_send + i];
		tbl->rx_reorder_ptr[pkt_to_send + i] = NULL;
	}

	tbl->start_win = start_win;
	spin_unlock_bh(&priv->rx_reorder_tbl_lock[tid]);

	while ((skb = __skb_dequeue(&list)))
		nxpwifi_11n_dispatch_pkt(priv, skb);
}

/* Dispatch packets until a hole is found. */
static void
nxpwifi_11n_scan_and_dispatch(struct nxpwifi_private *priv,
			      struct nxpwifi_rx_reorder_tbl *tbl)
{
	struct sk_buff_head list;
	struct sk_buff *skb;
	int i, j, xchg, tid;

	tid = tbl->tid;
	__skb_queue_head_init(&list);
	spin_lock_bh(&priv->rx_reorder_tbl_lock[tid]);

	for (i = 0; i < tbl->win_size; ++i) {
		if (!tbl->rx_reorder_ptr[i])
			break;
		skb = tbl->rx_reorder_ptr[i];
		__skb_queue_tail(&list, skb);
		tbl->rx_reorder_ptr[i] = NULL;
	}

	/* Simulate circular buffer via rotation. */
	if (i > 0) {
		xchg = tbl->win_size - i;
		for (j = 0; j < xchg; ++j) {
			tbl->rx_reorder_ptr[j] = tbl->rx_reorder_ptr[i + j];
			tbl->rx_reorder_ptr[i + j] = NULL;
		}
	}
	tbl->start_win = (tbl->start_win + i) & (MAX_TID_VALUE - 1);

	spin_unlock_bh(&priv->rx_reorder_tbl_lock[tid]);

	while ((skb = __skb_dequeue(&list)))
		nxpwifi_11n_dispatch_pkt(priv, skb);
}

/* Delete RX reorder entry and flush pending packets. */
static void
nxpwifi_del_rx_reorder_entry(struct nxpwifi_private *priv,
			     struct nxpwifi_rx_reorder_tbl *tbl)
{
	int start_win, tid;

	if (!tbl)
		return;

	tid = tbl->tid;

	atomic_set(&priv->adapter->rx_ba_teardown_pending, 1);
	flush_workqueue(priv->adapter->rx_workqueue);

	start_win = (tbl->start_win + tbl->win_size) & (MAX_TID_VALUE - 1);
	nxpwifi_11n_dispatch_pkt_until_start_win(priv, tbl, start_win);

	timer_delete_sync(&tbl->timer_context.timer);
	tbl->timer_context.timer_is_set = false;

	spin_lock_bh(&priv->rx_reorder_tbl_lock[tid]);
	list_del_rcu(&tbl->list);
	spin_unlock_bh(&priv->rx_reorder_tbl_lock[tid]);

	kfree(tbl->rx_reorder_ptr);
	kfree_rcu(tbl, rcu);

	atomic_set(&priv->adapter->rx_ba_teardown_pending, 0);
}

/* Lookup RX reorder entry by TID/TA. */
struct nxpwifi_rx_reorder_tbl *
nxpwifi_11n_get_rx_reorder_tbl(struct nxpwifi_private *priv, int tid, u8 *ta)
{
	struct nxpwifi_rx_reorder_tbl *tbl, *found = NULL;

	guard(rcu)();

	list_for_each_entry_rcu(tbl, &priv->rx_reorder_tbl_ptr[tid], list) {
		if (!memcmp(tbl->ta, ta, ETH_ALEN) && tbl->tid == tid) {
			found = tbl;
			break;
		}
	}

	return found;
}

/* Delete RX reorder entries by TA. */
void nxpwifi_11n_del_rx_reorder_tbl_by_ta(struct nxpwifi_private *priv, u8 *ta)
{
	struct nxpwifi_rx_reorder_tbl *tbl, *tmp;
	LIST_HEAD(to_delete);
	int i;

	if (!ta)
		return;

	for (i = 0; i < MAX_NUM_TID; i++) {
		guard(rcu)();
		list_for_each_entry_rcu(tbl, &priv->rx_reorder_tbl_ptr[i], list) {
			if (!memcmp(tbl->ta, ta, ETH_ALEN)) {
				INIT_LIST_HEAD(&tbl->tmp_list);
				list_add_tail(&tbl->tmp_list, &to_delete);
			}
		}

		list_for_each_entry_safe(tbl, tmp, &to_delete, tmp_list)
			nxpwifi_del_rx_reorder_entry(priv, tbl);

		INIT_LIST_HEAD(&to_delete);
	}
}

/* Find last buffered sequence index. */
static int
nxpwifi_11n_find_last_seq_num(struct reorder_tmr_cnxt *ctx)
{
	struct nxpwifi_rx_reorder_tbl *rx_reorder_tbl_ptr = ctx->ptr;
	int i;

	guard(rcu)();
	for (i = rx_reorder_tbl_ptr->win_size - 1; i >= 0; --i) {
		if (rx_reorder_tbl_ptr->rx_reorder_ptr[i])
			return i;
	}

	return -EINVAL;
}

/* Flush and dispatch buffered packets on timer. */
static void
nxpwifi_flush_data(struct timer_list *t)
{
	struct reorder_tmr_cnxt *ctx =
		timer_container_of(ctx, t, timer);
	int start_win, seq_num;

	ctx->timer_is_set = false;
	seq_num = nxpwifi_11n_find_last_seq_num(ctx);

	if (seq_num < 0)
		return;

	nxpwifi_dbg(ctx->priv->adapter, INFO, "info: flush data %d\n", seq_num);
	start_win = (ctx->ptr->start_win + seq_num + 1) & (MAX_TID_VALUE - 1);
	nxpwifi_11n_dispatch_pkt_until_start_win(ctx->priv, ctx->ptr,
						 start_win);
}

/* Create RX reorder entry (TID/TA, SSN, winsize, timer). */
static void
nxpwifi_11n_create_rx_reorder_tbl(struct nxpwifi_private *priv, u8 *ta,
				  int tid, int win_size, int seq_num)
{
	int i;
	struct nxpwifi_rx_reorder_tbl *tbl, *new_node;
	u16 last_seq = 0;
	struct nxpwifi_sta_node *node;

	/* Existing TID/TA: flush and move window to SSN. */
	tbl = nxpwifi_11n_get_rx_reorder_tbl(priv, tid, ta);
	if (tbl) {
		nxpwifi_11n_dispatch_pkt_until_start_win(priv, tbl, seq_num);
		return;
	}
	/* if !tbl then create one */
	new_node = kzalloc_obj(*new_node, GFP_KERNEL);
	if (!new_node)
		return;

	INIT_LIST_HEAD(&new_node->list);
	new_node->tid = tid;
	memcpy(new_node->ta, ta, ETH_ALEN);
	new_node->start_win = seq_num;
	new_node->init_win = seq_num;
	new_node->flags = 0;

	if (nxpwifi_queuing_ra_based(priv)) {
		if (priv->bss_role == NXPWIFI_BSS_ROLE_UAP) {
			guard(rcu)();
			node = nxpwifi_get_sta_entry(priv, ta);
			if (node)
				last_seq = node->rx_seq[tid];
		}
	} else {
		guard(rcu)();
		node = nxpwifi_get_sta_entry(priv, ta);
		if (node)
			last_seq = node->rx_seq[tid];
		else
			last_seq = priv->rx_seq[tid];
	}

	nxpwifi_dbg(priv->adapter, INFO,
		    "info: last_seq=%d start_win=%d\n",
		    last_seq, new_node->start_win);

	if (last_seq != NXPWIFI_DEF_11N_RX_SEQ_NUM &&
	    last_seq >= new_node->start_win) {
		new_node->start_win = last_seq + 1;
		new_node->flags |= RXREOR_INIT_WINDOW_SHIFT;
	}

	new_node->win_size = win_size;

	new_node->rx_reorder_ptr = kcalloc(win_size, sizeof(void *),
					   GFP_KERNEL);
	if (!new_node->rx_reorder_ptr) {
		kfree(new_node);
		nxpwifi_dbg(priv->adapter, ERROR,
			    "%s: failed to alloc reorder_ptr\n", __func__);
		return;
	}

	new_node->timer_context.ptr = new_node;
	new_node->timer_context.priv = priv;
	new_node->timer_context.timer_is_set = false;

	timer_setup(&new_node->timer_context.timer, nxpwifi_flush_data, 0);

	for (i = 0; i < win_size; ++i)
		new_node->rx_reorder_ptr[i] = NULL;

	spin_lock_bh(&priv->rx_reorder_tbl_lock[tid]);
	list_add_tail_rcu(&new_node->list, &priv->rx_reorder_tbl_ptr[tid]);
	spin_unlock_bh(&priv->rx_reorder_tbl_lock[tid]);
}

static void
nxpwifi_11n_rxreorder_timer_restart(struct nxpwifi_rx_reorder_tbl *tbl)
{
	u32 min_flush_time;

	if (tbl->win_size >= NXPWIFI_BA_WIN_SIZE_32)
		min_flush_time = MIN_FLUSH_TIMER_15_MS;
	else
		min_flush_time = MIN_FLUSH_TIMER_MS;

	mod_timer(&tbl->timer_context.timer,
		  jiffies + msecs_to_jiffies(min_flush_time * tbl->win_size));

	tbl->timer_context.timer_is_set = true;
}

/* Prepare ADDBA request. */
int nxpwifi_cmd_11n_addba_req(struct host_cmd_ds_command *cmd, void *data_buf)
{
	struct host_cmd_ds_11n_addba_req *add_ba_req = &cmd->params.add_ba_req;

	cmd->command = cpu_to_le16(HOST_CMD_11N_ADDBA_REQ);
	cmd->size = cpu_to_le16(sizeof(*add_ba_req) + S_DS_GEN);
	memcpy(add_ba_req, data_buf, sizeof(*add_ba_req));

	return 0;
}

/* Prepare ADDBA response and create RX reorder table. */
int nxpwifi_cmd_11n_addba_rsp_gen(struct nxpwifi_private *priv,
				  struct host_cmd_ds_command *cmd,
				  struct host_cmd_ds_11n_addba_req
				  *cmd_addba_req)
{
	struct host_cmd_ds_11n_addba_rsp *add_ba_rsp = &cmd->params.add_ba_rsp;
	u32 rx_win_size = priv->add_ba_param.rx_win_size;
	u8 tid;
	int win_size;
	u16 block_ack_param_set;

	cmd->command = cpu_to_le16(HOST_CMD_11N_ADDBA_RSP);
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

	/* If we don't support AMSDU inside AMPDU, reset the bit */
	if (!priv->add_ba_param.rx_amsdu ||
	    priv->aggr_prio_tbl[tid].amsdu == BA_STREAM_NOT_ALLOWED)
		block_ack_param_set &= ~IEEE80211_ADDBA_PARAM_AMSDU_MASK;
	block_ack_param_set |= rx_win_size << BLOCKACKPARAM_WINSIZE_POS;
	add_ba_rsp->block_ack_param_set = cpu_to_le16(block_ack_param_set);
	win_size = (le16_to_cpu(add_ba_rsp->block_ack_param_set)
		    & IEEE80211_ADDBA_PARAM_BUF_SIZE_MASK)
		   >> BLOCKACKPARAM_WINSIZE_POS;
	cmd_addba_req->block_ack_param_set = cpu_to_le16(block_ack_param_set);

	nxpwifi_11n_create_rx_reorder_tbl(priv, cmd_addba_req->peer_mac_addr,
					  tid, win_size,
					  le16_to_cpu(cmd_addba_req->ssn));
	return 0;
}

/* Prepare DELBA command. */
int nxpwifi_cmd_11n_delba(struct host_cmd_ds_command *cmd, void *data_buf)
{
	struct host_cmd_ds_11n_delba *del_ba = &cmd->params.del_ba;

	cmd->command = cpu_to_le16(HOST_CMD_11N_DELBA);
	cmd->size = cpu_to_le16(sizeof(*del_ba) + S_DS_GEN);
	memcpy(del_ba, data_buf, sizeof(*del_ba));

	return 0;
}

/* Decide and perform RX reordering for a packet. */
int nxpwifi_11n_rx_reorder_pkt(struct nxpwifi_private *priv,
			       u16 seq_num, u16 tid,
			       u8 *ta, u8 pkt_type, void *payload)
{
	struct nxpwifi_rx_reorder_tbl *tbl;
	int prev_start_win, start_win, end_win, win_size;
	u16 pkt_index;
	bool init_window_shift = false;
	int ret = 0;

	tbl = nxpwifi_11n_get_rx_reorder_tbl(priv, tid, ta);
	if (!tbl) {
		if (pkt_type != PKT_TYPE_BAR)
			nxpwifi_11n_dispatch_pkt(priv, payload);
		return ret;
	}

	if (pkt_type == PKT_TYPE_AMSDU && !tbl->amsdu) {
		nxpwifi_11n_dispatch_pkt(priv, payload);
		return ret;
	}

	start_win = tbl->start_win;
	prev_start_win = start_win;
	win_size = tbl->win_size;
	end_win = ((start_win + win_size) - 1) & (MAX_TID_VALUE - 1);
	if (tbl->flags & RXREOR_INIT_WINDOW_SHIFT) {
		init_window_shift = true;
		tbl->flags &= ~RXREOR_INIT_WINDOW_SHIFT;
	}

	if (tbl->flags & RXREOR_FORCE_NO_DROP) {
		nxpwifi_dbg(priv->adapter, INFO,
			    "RXREOR_FORCE_NO_DROP when HS is activated\n");
		tbl->flags &= ~RXREOR_FORCE_NO_DROP;
	} else if (init_window_shift && seq_num < start_win &&
		   seq_num >= tbl->init_win) {
		nxpwifi_dbg(priv->adapter, INFO,
			    "Sender TID sequence number reset %d->%d for SSN %d\n",
			    start_win, seq_num, tbl->init_win);
		start_win = seq_num;
		tbl->start_win = start_win;
		end_win = ((start_win + win_size) - 1) & (MAX_TID_VALUE - 1);
	} else {
		/* Drop packet if seq_num < start_win. */
		if ((start_win + TWOPOW11) > (MAX_TID_VALUE - 1)) {
			if (seq_num >= ((start_win + TWOPOW11) &
					(MAX_TID_VALUE - 1)) &&
			    seq_num < start_win) {
				ret = -EINVAL;
				goto done;
			}
		} else if ((seq_num < start_win) ||
			   (seq_num >= (start_win + TWOPOW11))) {
			ret = -EINVAL;
			goto done;
		}
	}

	/* Adjust seq_num for BAR (WinStart = seq_num). */
	if (pkt_type == PKT_TYPE_BAR)
		seq_num = ((seq_num + win_size) - 1) & (MAX_TID_VALUE - 1);

	if ((end_win < start_win &&
	     seq_num < start_win && seq_num > end_win) ||
	    (end_win > start_win && (seq_num > end_win ||
				     seq_num < start_win))) {
		end_win = seq_num;
		if (((end_win - win_size) + 1) >= 0)
			start_win = (end_win - win_size) + 1;
		else
			start_win = (MAX_TID_VALUE - (win_size - end_win)) + 1;
		nxpwifi_11n_dispatch_pkt_until_start_win(priv, tbl, start_win);
	}

	if (pkt_type != PKT_TYPE_BAR) {
		if (seq_num >= start_win)
			pkt_index = seq_num - start_win;
		else
			pkt_index = (seq_num + MAX_TID_VALUE) - start_win;

		if (tbl->rx_reorder_ptr[pkt_index]) {
			ret = -EINVAL;
			goto done;
		}

		tbl->rx_reorder_ptr[pkt_index] = payload;
	}

	/* Dispatch sequentially until a hole; update start_win. */
	nxpwifi_11n_scan_and_dispatch(priv, tbl);

done:
	if (!tbl->timer_context.timer_is_set ||
	    prev_start_win != tbl->start_win)
		nxpwifi_11n_rxreorder_timer_restart(tbl);
	return ret;
}

/* Delete BA entry for TID/TA. */
void
nxpwifi_del_ba_tbl(struct nxpwifi_private *priv, int tid, u8 *peer_mac,
		   u8 type, int initiator)
{
	struct nxpwifi_rx_reorder_tbl *tbl;
	struct nxpwifi_tx_ba_stream_tbl *ptx_tbl;
	struct nxpwifi_ra_list_tbl *ra_list;
	u8 cleanup_rx_reorder_tbl;
	int tid_down;

	if (type == TYPE_DELBA_RECEIVE)
		cleanup_rx_reorder_tbl = (initiator) ? true : false;
	else
		cleanup_rx_reorder_tbl = (initiator) ? false : true;

	nxpwifi_dbg(priv->adapter, EVENT, "event: DELBA: %pM tid=%d initiator=%d\n",
		    peer_mac, tid, initiator);

	if (cleanup_rx_reorder_tbl) {
		tbl = nxpwifi_11n_get_rx_reorder_tbl(priv, tid, peer_mac);
		if (!tbl) {
			nxpwifi_dbg(priv->adapter, EVENT,
				    "event: TID, TA not found in table\n");
			return;
		}
		nxpwifi_del_rx_reorder_entry(priv, tbl);
	} else {
		guard(rcu)();
		ptx_tbl = nxpwifi_get_ba_tbl(priv, tid, peer_mac);

		if (!ptx_tbl) {
			nxpwifi_dbg(priv->adapter, EVENT,
				    "event: TID, RA not found in table\n");
			return;
		}

		tid_down = nxpwifi_wmm_downgrade_tid(priv, tid);
		ra_list = nxpwifi_wmm_get_ralist_node(priv, tid_down, peer_mac);
		if (ra_list) {
			ra_list->amsdu_in_ampdu = false;
			ra_list->ba_status = BA_SETUP_NONE;
		}
		spin_lock_bh(&priv->tx_ba_stream_tbl_lock[tid]);
		nxpwifi_11n_delete_tx_ba_stream_tbl_entry(priv, ptx_tbl);
		spin_unlock_bh(&priv->tx_ba_stream_tbl_lock[tid]);
	}
}

/* Handle ADDBA response. */
int nxpwifi_ret_11n_addba_resp(struct nxpwifi_private *priv,
			       struct host_cmd_ds_command *resp)
{
	struct host_cmd_ds_11n_addba_rsp *add_ba_rsp = &resp->params.add_ba_rsp;
	int tid, win_size;
	struct nxpwifi_rx_reorder_tbl *tbl;
	u16 block_ack_param_set;

	block_ack_param_set = le16_to_cpu(add_ba_rsp->block_ack_param_set);

	tid = (block_ack_param_set & IEEE80211_ADDBA_PARAM_TID_MASK)
		>> BLOCKACKPARAM_TID_POS;
	/* Check if we had rejected the ADDBA, if yes then do not create the stream */
	if (le16_to_cpu(add_ba_rsp->status_code) != BA_RESULT_SUCCESS) {
		nxpwifi_dbg(priv->adapter, ERROR, "ADDBA RSP: failed %pM tid=%d)\n",
			    add_ba_rsp->peer_mac_addr, tid);

		tbl = nxpwifi_11n_get_rx_reorder_tbl(priv, tid,
						     add_ba_rsp->peer_mac_addr);
		if (tbl)
			nxpwifi_del_rx_reorder_entry(priv, tbl);

		return 0;
	}

	win_size = (block_ack_param_set & IEEE80211_ADDBA_PARAM_BUF_SIZE_MASK)
		    >> BLOCKACKPARAM_WINSIZE_POS;

	tbl = nxpwifi_11n_get_rx_reorder_tbl(priv, tid,
					     add_ba_rsp->peer_mac_addr);
	if (tbl) {
		if ((block_ack_param_set & IEEE80211_ADDBA_PARAM_AMSDU_MASK) &&
		    priv->add_ba_param.rx_amsdu &&
		    priv->aggr_prio_tbl[tid].amsdu != BA_STREAM_NOT_ALLOWED)
			tbl->amsdu = true;
		else
			tbl->amsdu = false;
	}

	nxpwifi_dbg(priv->adapter, CMD,
		    "cmd: ADDBA RSP: %pM tid=%d ssn=%d win_size=%d\n",
		    add_ba_rsp->peer_mac_addr, tid, add_ba_rsp->ssn, win_size);

	return 0;
}

/* Handle BA stream timeout: send DELBA. */
void nxpwifi_11n_ba_stream_timeout(struct nxpwifi_private *priv,
				   struct host_cmd_ds_11n_batimeout *event)
{
	struct host_cmd_ds_11n_delba delba;

	memset(&delba, 0, sizeof(struct host_cmd_ds_11n_delba));
	memcpy(delba.peer_mac_addr, event->peer_mac_addr, ETH_ALEN);

	delba.del_ba_param_set |=
		cpu_to_le16((u16)event->tid << DELBA_TID_POS);
	delba.del_ba_param_set |=
		cpu_to_le16((u16)event->origninator << DELBA_INITIATOR_POS);
	delba.reason_code = cpu_to_le16(WLAN_REASON_QSTA_TIMEOUT);
	nxpwifi_send_cmd(priv, HOST_CMD_11N_DELBA, 0, 0, &delba, false);
}

/* Cleanup all RX reorder entries. */
void nxpwifi_11n_cleanup_reorder_tbl(struct nxpwifi_private *priv)
{
	struct nxpwifi_rx_reorder_tbl *del_tbl_ptr, *tmp_node;
	LIST_HEAD(to_delete_list);
	int i;

	for (i = 0; i < MAX_NUM_TID; i++) {
		spin_lock_bh(&priv->rx_reorder_tbl_lock[i]);
		list_splice_init(&priv->rx_reorder_tbl_ptr[i], &to_delete_list);
		spin_unlock_bh(&priv->rx_reorder_tbl_lock[i]);

		list_for_each_entry_safe(del_tbl_ptr, tmp_node, &to_delete_list, list)
			nxpwifi_del_rx_reorder_entry(priv, del_tbl_ptr);

		INIT_LIST_HEAD(&to_delete_list);
	}

	nxpwifi_reset_11n_rx_seq_num(priv);
}

/* Update flags for all RX reorder tables. */
void nxpwifi_update_rxreor_flags(struct nxpwifi_adapter *adapter, u8 flags)
{
	struct nxpwifi_private *priv;
	struct nxpwifi_rx_reorder_tbl *tbl;
	int i, j;

	for (i = 0; i < adapter->priv_num; i++) {
		priv = adapter->priv[i];

		for (j = 0; j < MAX_NUM_TID; j++) {
			spin_lock_bh(&priv->rx_reorder_tbl_lock[j]);
			list_for_each_entry_rcu(tbl, &priv->rx_reorder_tbl_ptr[j], list)
				tbl->flags = flags;
			spin_unlock_bh(&priv->rx_reorder_tbl_lock[j]);
		}
	}
}

/* Update RX window size based on coex flag. */
static void nxpwifi_update_ampdu_rxwinsize(struct nxpwifi_adapter *adapter,
					   bool coex_flag)
{
	u8 i, j;
	u32 rx_win_size;
	struct nxpwifi_private *priv;

	nxpwifi_dbg(adapter, INFO, "Update rxwinsize %d\n", coex_flag);

	for (i = 0; i < adapter->priv_num; i++) {
		priv = adapter->priv[i];
		rx_win_size = priv->add_ba_param.rx_win_size;
		if (coex_flag) {
			if (priv->bss_type == NXPWIFI_BSS_TYPE_STA)
				priv->add_ba_param.rx_win_size =
					NXPWIFI_STA_COEX_AMPDU_DEF_RXWINSIZE;
			if (priv->bss_type == NXPWIFI_BSS_TYPE_UAP)
				priv->add_ba_param.rx_win_size =
					NXPWIFI_UAP_COEX_AMPDU_DEF_RXWINSIZE;
		} else {
			if (priv->bss_type == NXPWIFI_BSS_TYPE_STA)
				priv->add_ba_param.rx_win_size =
					NXPWIFI_STA_AMPDU_DEF_RXWINSIZE;
			if (priv->bss_type == NXPWIFI_BSS_TYPE_UAP)
				priv->add_ba_param.rx_win_size =
					NXPWIFI_UAP_AMPDU_DEF_RXWINSIZE;
		}

		if (adapter->coex_win_size && adapter->coex_rx_win_size)
			priv->add_ba_param.rx_win_size =
				adapter->coex_rx_win_size;

		if (rx_win_size != priv->add_ba_param.rx_win_size) {
			if (!priv->media_connected)
				continue;
			for (j = 0; j < MAX_NUM_TID; j++)
				nxpwifi_11n_delba(priv, j);
		}
	}
}

/* Check coex for RX BA. */
void nxpwifi_coex_ampdu_rxwinsize(struct nxpwifi_adapter *adapter)
{
	u8 i;
	struct nxpwifi_private *priv;
	u8 count = 0;

	for (i = 0; i < adapter->priv_num; i++) {
		priv = adapter->priv[i];
		if (GET_BSS_ROLE(priv) == NXPWIFI_BSS_ROLE_STA) {
			if (priv->media_connected)
				count++;
		}
		if (GET_BSS_ROLE(priv) == NXPWIFI_BSS_ROLE_UAP) {
			if (priv->bss_started)
				count++;
		}
		if (count >= NXPWIFI_BSS_COEX_COUNT)
			break;
	}
	if (count >= NXPWIFI_BSS_COEX_COUNT)
		nxpwifi_update_ampdu_rxwinsize(adapter, true);
	else
		nxpwifi_update_ampdu_rxwinsize(adapter, false);
}

/* Handle RXBA sync event. */
void nxpwifi_11n_rxba_sync_event(struct nxpwifi_private *priv,
				 u8 *event_buf, u16 len)
{
	struct nxpwifi_ie_types_rxba_sync *tlv_rxba = (void *)event_buf;
	u16 tlv_type, tlv_len;
	struct nxpwifi_rx_reorder_tbl *rx_reor_tbl_ptr;
	u8 i, j;
	u16 seq_num, tlv_seq_num, tlv_bitmap_len;
	int tlv_buf_left = len;
	int ret;
	u8 *tmp;

	nxpwifi_dbg_dump(priv->adapter, EVT_D, "RXBA_SYNC event:",
			 event_buf, len);
	while (tlv_buf_left > sizeof(*tlv_rxba)) {
		tlv_type = le16_to_cpu(tlv_rxba->header.type);
		tlv_len  = le16_to_cpu(tlv_rxba->header.len);
		if (size_add(sizeof(tlv_rxba->header), tlv_len) > tlv_buf_left) {
			nxpwifi_dbg(priv->adapter, WARN,
				    "TLV size (%zu) overflows event_buf buf_left=%d\n",
				    size_add(sizeof(tlv_rxba->header), tlv_len),
				    tlv_buf_left);
			return;
		}

		if (tlv_type != TLV_TYPE_RXBA_SYNC) {
			nxpwifi_dbg(priv->adapter, ERROR,
				    "Wrong TLV id=0x%x\n", tlv_type);
			return;
		}

		tlv_seq_num = le16_to_cpu(tlv_rxba->seq_num);
		tlv_bitmap_len = le16_to_cpu(tlv_rxba->bitmap_len);
		if (size_add(sizeof(*tlv_rxba), tlv_bitmap_len) > tlv_buf_left) {
			nxpwifi_dbg(priv->adapter, WARN,
				    "TLV size (%zu) overflows event_buf buf_left=%d\n",
				    size_add(sizeof(*tlv_rxba), tlv_bitmap_len),
				    tlv_buf_left);
			return;
		}

		nxpwifi_dbg(priv->adapter, INFO,
			    "%pM tid=%d seq_num=%d bitmap_len=%d\n",
			    tlv_rxba->mac, tlv_rxba->tid, tlv_seq_num,
			    tlv_bitmap_len);

		rx_reor_tbl_ptr =
			nxpwifi_11n_get_rx_reorder_tbl(priv, tlv_rxba->tid,
						       tlv_rxba->mac);
		if (!rx_reor_tbl_ptr) {
			nxpwifi_dbg(priv->adapter, ERROR,
				    "Can not find rx_reorder_tbl!");
			return;
		}

		for (i = 0; i < tlv_bitmap_len; i++) {
			for (j = 0 ; j < 8; j++) {
				if (tlv_rxba->bitmap[i] & (1 << j)) {
					seq_num = (MAX_TID_VALUE - 1) &
						(tlv_seq_num + i * 8 + j);

					nxpwifi_dbg(priv->adapter, ERROR,
						    "drop packet,seq=%d\n",
						    seq_num);

					ret = nxpwifi_11n_rx_reorder_pkt
					(priv, seq_num, tlv_rxba->tid,
					 tlv_rxba->mac, 0, NULL);

					if (ret)
						nxpwifi_dbg(priv->adapter,
							    ERROR,
							    "Fail to drop packet");
				}
			}
		}

		tlv_buf_left -= (sizeof(tlv_rxba->header) + tlv_len);
		tmp = (u8 *)tlv_rxba + sizeof(tlv_rxba->header) + tlv_len;
		tlv_rxba = (struct nxpwifi_ie_types_rxba_sync *)tmp;
	}
}
