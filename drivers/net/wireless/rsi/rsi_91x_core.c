/**
 * Copyright (c) 2014 Redpine Signals Inc.
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

#include "rsi_mgmt.h"
#include "rsi_common.h"

/**
 * rsi_determine_min_weight_queue() - This function determines the queue with
 *				      the min weight.
 * @common: Pointer to the driver private structure.
 *
 * Return: q_num: Corresponding queue number.
 */
static u8 rsi_determine_min_weight_queue(struct rsi_common *common)
{
	struct wmm_qinfo *tx_qinfo = common->tx_qinfo;
	u32 q_len = 0;
	u8 ii = 0;

	for (ii = 0; ii < NUM_EDCA_QUEUES; ii++) {
		q_len = skb_queue_len(&common->tx_queue[ii]);
		if ((tx_qinfo[ii].pkt_contended) && q_len) {
			common->min_weight = tx_qinfo[ii].weight;
			break;
		}
	}
	return ii;
}

/**
 * rsi_recalculate_weights() - This function recalculates the weights
 *			       corresponding to each queue.
 * @common: Pointer to the driver private structure.
 *
 * Return: recontend_queue bool variable
 */
static bool rsi_recalculate_weights(struct rsi_common *common)
{
	struct wmm_qinfo *tx_qinfo = common->tx_qinfo;
	bool recontend_queue = false;
	u8 ii = 0;
	u32 q_len = 0;

	for (ii = 0; ii < NUM_EDCA_QUEUES; ii++) {
		q_len = skb_queue_len(&common->tx_queue[ii]);
		/* Check for the need of contention */
		if (q_len) {
			if (tx_qinfo[ii].pkt_contended) {
				tx_qinfo[ii].weight =
				((tx_qinfo[ii].weight > common->min_weight) ?
				 tx_qinfo[ii].weight - common->min_weight : 0);
			} else {
				tx_qinfo[ii].pkt_contended = 1;
				tx_qinfo[ii].weight = tx_qinfo[ii].wme_params;
				recontend_queue = true;
			}
		} else { /* No packets so no contention */
			tx_qinfo[ii].weight = 0;
			tx_qinfo[ii].pkt_contended = 0;
		}
	}

	return recontend_queue;
}

/**
 * rsi_core_determine_hal_queue() - This function determines the queue from
 *				    which packet has to be dequeued.
 * @common: Pointer to the driver private structure.
 *
 * Return: q_num: Corresponding queue number on success.
 */
static u8 rsi_core_determine_hal_queue(struct rsi_common *common)
{
	bool recontend_queue = false;
	u32 q_len = 0;
	u8 q_num = INVALID_QUEUE;
	u8 ii = 0, min = 0;

	if (skb_queue_len(&common->tx_queue[MGMT_SOFT_Q])) {
		if (!common->mgmt_q_block)
			q_num = MGMT_SOFT_Q;
		return q_num;
	}

	if (common->pkt_cnt != 0) {
		--common->pkt_cnt;
		return common->selected_qnum;
	}

get_queue_num:
	recontend_queue = false;

	q_num = rsi_determine_min_weight_queue(common);

	q_len = skb_queue_len(&common->tx_queue[ii]);
	ii = q_num;

	/* Selecting the queue with least back off */
	for (; ii < NUM_EDCA_QUEUES; ii++) {
		if (((common->tx_qinfo[ii].pkt_contended) &&
		     (common->tx_qinfo[ii].weight < min)) && q_len) {
			min = common->tx_qinfo[ii].weight;
			q_num = ii;
		}
	}

	if (q_num < NUM_EDCA_QUEUES)
		common->tx_qinfo[q_num].pkt_contended = 0;

	/* Adjust the back off values for all queues again */
	recontend_queue = rsi_recalculate_weights(common);

	q_len = skb_queue_len(&common->tx_queue[q_num]);
	if (!q_len) {
		/* If any queues are freshly contended and the selected queue
		 * doesn't have any packets
		 * then get the queue number again with fresh values
		 */
		if (recontend_queue)
			goto get_queue_num;

		q_num = INVALID_QUEUE;
		return q_num;
	}

	common->selected_qnum = q_num;
	q_len = skb_queue_len(&common->tx_queue[q_num]);

	switch (common->selected_qnum) {
	case VO_Q:
		if (q_len > MAX_CONTINUOUS_VO_PKTS)
			common->pkt_cnt = (MAX_CONTINUOUS_VO_PKTS - 1);
		else
			common->pkt_cnt = --q_len;
		break;

	case VI_Q:
		if (q_len > MAX_CONTINUOUS_VI_PKTS)
			common->pkt_cnt = (MAX_CONTINUOUS_VI_PKTS - 1);
		else
			common->pkt_cnt = --q_len;

		break;

	default:
		common->pkt_cnt = 0;
		break;
	}

	return q_num;
}

/**
 * rsi_core_queue_pkt() - This functions enqueues the packet to the queue
 *			  specified by the queue number.
 * @common: Pointer to the driver private structure.
 * @skb: Pointer to the socket buffer structure.
 *
 * Return: None.
 */
static void rsi_core_queue_pkt(struct rsi_common *common,
			       struct sk_buff *skb)
{
	u8 q_num = skb->priority;
	if (q_num >= NUM_SOFT_QUEUES) {
		rsi_dbg(ERR_ZONE, "%s: Invalid Queue Number: q_num = %d\n",
			__func__, q_num);
		dev_kfree_skb(skb);
		return;
	}

	skb_queue_tail(&common->tx_queue[q_num], skb);
}

/**
 * rsi_core_dequeue_pkt() - This functions dequeues the packet from the queue
 *			    specified by the queue number.
 * @common: Pointer to the driver private structure.
 * @q_num: Queue number.
 *
 * Return: Pointer to sk_buff structure.
 */
static struct sk_buff *rsi_core_dequeue_pkt(struct rsi_common *common,
					    u8 q_num)
{
	if (q_num >= NUM_SOFT_QUEUES) {
		rsi_dbg(ERR_ZONE, "%s: Invalid Queue Number: q_num = %d\n",
			__func__, q_num);
		return NULL;
	}

	return skb_dequeue(&common->tx_queue[q_num]);
}

/**
 * rsi_core_qos_processor() - This function is used to determine the wmm queue
 *			      based on the backoff procedure. Data packets are
 *			      dequeued from the selected hal queue and sent to
 *			      the below layers.
 * @common: Pointer to the driver private structure.
 *
 * Return: None.
 */
void rsi_core_qos_processor(struct rsi_common *common)
{
	struct rsi_hw *adapter = common->priv;
	struct sk_buff *skb;
	unsigned long tstamp_1, tstamp_2;
	u8 q_num;
	int status;

	tstamp_1 = jiffies;
	while (1) {
		q_num = rsi_core_determine_hal_queue(common);
		rsi_dbg(DATA_TX_ZONE,
			"%s: Queue number = %d\n", __func__, q_num);

		if (q_num == INVALID_QUEUE) {
			rsi_dbg(DATA_TX_ZONE, "%s: No More Pkt\n", __func__);
			break;
		}

		mutex_lock(&common->tx_rxlock);

		status = adapter->check_hw_queue_status(adapter, q_num);
		if ((status <= 0)) {
			mutex_unlock(&common->tx_rxlock);
			break;
		}

		if ((q_num < MGMT_SOFT_Q) &&
		    ((skb_queue_len(&common->tx_queue[q_num])) <=
		      MIN_DATA_QUEUE_WATER_MARK)) {
			if (ieee80211_queue_stopped(adapter->hw, WME_AC(q_num)))
				ieee80211_wake_queue(adapter->hw,
						     WME_AC(q_num));
		}

		skb = rsi_core_dequeue_pkt(common, q_num);
		if (skb == NULL) {
			mutex_unlock(&common->tx_rxlock);
			break;
		}

		if (q_num == MGMT_SOFT_Q)
			status = rsi_send_mgmt_pkt(common, skb);
		else
			status = rsi_send_data_pkt(common, skb);

		if (status) {
			mutex_unlock(&common->tx_rxlock);
			break;
		}

		common->tx_stats.total_tx_pkt_send[q_num]++;

		tstamp_2 = jiffies;
		mutex_unlock(&common->tx_rxlock);

		if (tstamp_2 > tstamp_1 + (300 * HZ / 1000))
			schedule();
	}
}

/**
 * rsi_core_xmit() - This function transmits the packets received from mac80211
 * @common: Pointer to the driver private structure.
 * @skb: Pointer to the socket buffer structure.
 *
 * Return: None.
 */
void rsi_core_xmit(struct rsi_common *common, struct sk_buff *skb)
{
	struct rsi_hw *adapter = common->priv;
	struct ieee80211_tx_info *info;
	struct skb_info *tx_params;
	struct ieee80211_hdr *tmp_hdr = NULL;
	u8 q_num, tid = 0;

	if ((!skb) || (!skb->len)) {
		rsi_dbg(ERR_ZONE, "%s: Null skb/zero Length packet\n",
			__func__);
		goto xmit_fail;
	}
	info = IEEE80211_SKB_CB(skb);
	tx_params = (struct skb_info *)info->driver_data;
	tmp_hdr = (struct ieee80211_hdr *)&skb->data[0];

	if (common->fsm_state != FSM_MAC_INIT_DONE) {
		rsi_dbg(ERR_ZONE, "%s: FSM state not open\n", __func__);
		goto xmit_fail;
	}

	if ((ieee80211_is_mgmt(tmp_hdr->frame_control)) ||
	    (ieee80211_is_ctl(tmp_hdr->frame_control))) {
		q_num = MGMT_SOFT_Q;
		skb->priority = q_num;
	} else {
		if (ieee80211_is_data_qos(tmp_hdr->frame_control)) {
			tid = (skb->data[24] & IEEE80211_QOS_TID);
			skb->priority = TID_TO_WME_AC(tid);
		} else {
			tid = IEEE80211_NONQOS_TID;
			skb->priority = BE_Q;
		}
		q_num = skb->priority;
		tx_params->tid = tid;
		tx_params->sta_id = 0;
	}

	if ((q_num != MGMT_SOFT_Q) &&
	    ((skb_queue_len(&common->tx_queue[q_num]) + 1) >=
	     DATA_QUEUE_WATER_MARK)) {
		if (!ieee80211_queue_stopped(adapter->hw, WME_AC(q_num)))
			ieee80211_stop_queue(adapter->hw, WME_AC(q_num));
		rsi_set_event(&common->tx_thread.event);
		goto xmit_fail;
	}

	rsi_core_queue_pkt(common, skb);
	rsi_dbg(DATA_TX_ZONE, "%s: ===> Scheduling TX thead <===\n", __func__);
	rsi_set_event(&common->tx_thread.event);

	return;

xmit_fail:
	rsi_dbg(ERR_ZONE, "%s: Failed to queue packet\n", __func__);
	/* Dropping pkt here */
	ieee80211_free_txskb(common->priv->hw, skb);
}
