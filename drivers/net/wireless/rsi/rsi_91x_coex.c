/**
 * Copyright (c) 2018 Redpine Signals Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "rsi_main.h"
#include "rsi_coex.h"
#include "rsi_mgmt.h"
#include "rsi_hal.h"

static enum rsi_coex_queues rsi_coex_determine_coex_q
			(struct rsi_coex_ctrl_block *coex_cb)
{
	enum rsi_coex_queues q_num = RSI_COEX_Q_INVALID;

	if (skb_queue_len(&coex_cb->coex_tx_qs[RSI_COEX_Q_COMMON]) > 0)
		q_num = RSI_COEX_Q_COMMON;
	if (skb_queue_len(&coex_cb->coex_tx_qs[RSI_COEX_Q_BT]) > 0)
		q_num = RSI_COEX_Q_BT;
	if (skb_queue_len(&coex_cb->coex_tx_qs[RSI_COEX_Q_WLAN]) > 0)
		q_num = RSI_COEX_Q_WLAN;

	return q_num;
}

static void rsi_coex_sched_tx_pkts(struct rsi_coex_ctrl_block *coex_cb)
{
	enum rsi_coex_queues coex_q = RSI_COEX_Q_INVALID;
	struct sk_buff *skb;

	do {
		coex_q = rsi_coex_determine_coex_q(coex_cb);
		rsi_dbg(INFO_ZONE, "queue = %d\n", coex_q);

		if (coex_q == RSI_COEX_Q_BT) {
			skb = skb_dequeue(&coex_cb->coex_tx_qs[RSI_COEX_Q_BT]);
			rsi_send_bt_pkt(coex_cb->priv, skb);
		}
	} while (coex_q != RSI_COEX_Q_INVALID);
}

static void rsi_coex_scheduler_thread(struct rsi_common *common)
{
	struct rsi_coex_ctrl_block *coex_cb =
		(struct rsi_coex_ctrl_block *)common->coex_cb;
	u32 timeout = EVENT_WAIT_FOREVER;

	do {
		rsi_wait_event(&coex_cb->coex_tx_thread.event, timeout);
		rsi_reset_event(&coex_cb->coex_tx_thread.event);

		rsi_coex_sched_tx_pkts(coex_cb);
	} while (atomic_read(&coex_cb->coex_tx_thread.thread_done) == 0);

	complete_and_exit(&coex_cb->coex_tx_thread.completion, 0);
}

int rsi_coex_recv_pkt(struct rsi_common *common, u8 *msg)
{
	u8 msg_type = msg[RSI_RX_DESC_MSG_TYPE_OFFSET];

	switch (msg_type) {
	case COMMON_CARD_READY_IND:
		rsi_dbg(INFO_ZONE, "common card ready received\n");
		rsi_handle_card_ready(common, msg);
		break;
	case SLEEP_NOTIFY_IND:
		rsi_dbg(INFO_ZONE, "sleep notify received\n");
		rsi_mgmt_pkt_recv(common, msg);
		break;
	}

	return 0;
}

static inline int rsi_map_coex_q(u8 hal_queue)
{
	switch (hal_queue) {
	case RSI_COEX_Q:
		return RSI_COEX_Q_COMMON;
	case RSI_WLAN_Q:
		return RSI_COEX_Q_WLAN;
	case RSI_BT_Q:
		return RSI_COEX_Q_BT;
	}
	return RSI_COEX_Q_INVALID;
}

int rsi_coex_send_pkt(void *priv, struct sk_buff *skb, u8 hal_queue)
{
	struct rsi_common *common = (struct rsi_common *)priv;
	struct rsi_coex_ctrl_block *coex_cb =
		(struct rsi_coex_ctrl_block *)common->coex_cb;
	struct skb_info *tx_params = NULL;
	enum rsi_coex_queues coex_q;
	int status;

	coex_q = rsi_map_coex_q(hal_queue);
	if (coex_q == RSI_COEX_Q_INVALID) {
		rsi_dbg(ERR_ZONE, "Invalid coex queue\n");
		return -EINVAL;
	}
	if (coex_q != RSI_COEX_Q_COMMON &&
	    coex_q != RSI_COEX_Q_WLAN) {
		skb_queue_tail(&coex_cb->coex_tx_qs[coex_q], skb);
		rsi_set_event(&coex_cb->coex_tx_thread.event);
		return 0;
	}
	if (common->iface_down) {
		tx_params =
			(struct skb_info *)&IEEE80211_SKB_CB(skb)->driver_data;

		if (!(tx_params->flags & INTERNAL_MGMT_PKT)) {
			rsi_indicate_tx_status(common->priv, skb, -EINVAL);
			return 0;
		}
	}

	/* Send packet to hal */
	if (skb->priority == MGMT_SOFT_Q)
		status = rsi_send_mgmt_pkt(common, skb);
	else
		status = rsi_send_data_pkt(common, skb);

	return status;
}

int rsi_coex_attach(struct rsi_common *common)
{
	struct rsi_coex_ctrl_block *coex_cb;
	int cnt;

	coex_cb = kzalloc(sizeof(*coex_cb), GFP_KERNEL);
	if (!coex_cb)
		return -ENOMEM;

	common->coex_cb = (void *)coex_cb;
	coex_cb->priv = common;

	/* Initialize co-ex queues */
	for (cnt = 0; cnt < NUM_COEX_TX_QUEUES; cnt++)
		skb_queue_head_init(&coex_cb->coex_tx_qs[cnt]);
	rsi_init_event(&coex_cb->coex_tx_thread.event);

	/* Initialize co-ex thread */
	if (rsi_create_kthread(common,
			       &coex_cb->coex_tx_thread,
			       rsi_coex_scheduler_thread,
			       "Coex-Tx-Thread")) {
		rsi_dbg(ERR_ZONE, "%s: Unable to init tx thrd\n", __func__);
		return -EINVAL;
	}
	return 0;
}

void rsi_coex_detach(struct rsi_common *common)
{
	struct rsi_coex_ctrl_block *coex_cb =
		(struct rsi_coex_ctrl_block *)common->coex_cb;
	int cnt;

	rsi_kill_thread(&coex_cb->coex_tx_thread);

	for (cnt = 0; cnt < NUM_COEX_TX_QUEUES; cnt++)
		skb_queue_purge(&coex_cb->coex_tx_qs[cnt]);

	kfree(coex_cb);
}
