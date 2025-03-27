/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2024 Intel Corporation
 */
#ifndef __iwl_agg_h__
#define __iwl_agg_h__

#include "mld.h"
#include "fw/api/rx.h"

/**
 * struct iwl_mld_reorder_buffer - per ra/tid/queue reorder buffer
 * @head_sn: reorder window head sequence number
 * @num_stored: number of MPDUs stored in the buffer
 * @queue: queue of this reorder buffer
 * @valid: true if reordering is valid for this queue
 */
struct iwl_mld_reorder_buffer {
	u16 head_sn;
	u16 num_stored;
	int queue;
	bool valid;
} ____cacheline_aligned_in_smp;

/**
 * struct iwl_mld_reorder_buf_entry - reorder buffer entry per-queue/per-seqno
 * @frames: list of skbs stored. a list is necessary because in an A-MSDU,
 *	all sub-frames share the same sequence number, so they are stored
 *	together in the same list.
 */
struct iwl_mld_reorder_buf_entry {
	struct sk_buff_head frames;
}
#ifndef __CHECKER__
/* sparse doesn't like this construct: "bad integer constant expression" */
__aligned(roundup_pow_of_two(sizeof(struct sk_buff_head)))
#endif
;

/**
 * struct iwl_mld_baid_data - Block Ack session data
 * @rcu_head: RCU head for freeing this data
 * @sta_mask: station mask for the BAID
 * @tid: tid of the session
 * @baid: baid of the session
 * @buf_size: the reorder buffer size as set by the last ADDBA request
 * @entries_per_queue: number of buffers per queue, this actually gets
 *	aligned up to avoid cache line sharing between queues
 * @timeout: the timeout value specified in the ADDBA request.
 * @last_rx_timestamp: timestamp of the last received packet (in jiffies). This
 *	value is updated only when the configured @timeout has passed since
 *	the last update to minimize cache bouncing between RX queues.
 * @session_timer: timer is set to expire after 2 * @timeout (since we want
 *	to minimize the cache bouncing by updating @last_rx_timestamp only once
 *	after @timeout has passed). If no packets are received within this
 *	period, it informs mac80211 to initiate delBA flow, terminating the
 *	BA session.
 * @rcu_ptr: BA data RCU protected access
 * @mld: mld pointer, needed for timer context
 * @reorder_buf: reorder buffer, allocated per queue
 * @entries: data
 */
struct iwl_mld_baid_data {
	struct rcu_head rcu_head;
	u32 sta_mask;
	u8 tid;
	u8 baid;
	u16 buf_size;
	u16 entries_per_queue;
	u16 timeout;
	struct timer_list session_timer;
	unsigned long last_rx_timestamp;
	struct iwl_mld_baid_data __rcu **rcu_ptr;
	struct iwl_mld *mld;
	struct iwl_mld_reorder_buffer reorder_buf[IWL_MAX_RX_HW_QUEUES];
	struct iwl_mld_reorder_buf_entry entries[] ____cacheline_aligned_in_smp;
};

/**
 * struct iwl_mld_delba_data - RX queue sync data for %IWL_MLD_RXQ_NOTIF_DEL_BA
 *
 * @baid: Block Ack id, used to identify the BA session to be removed
 */
struct iwl_mld_delba_data {
	u32 baid;
} __packed;

/**
 * enum iwl_mld_reorder_result - Possible return values for iwl_mld_reorder()
 * indicating how the caller should handle the skb based on the result.
 *
 * @IWL_MLD_PASS_SKB: skb should be passed to upper layer.
 * @IWL_MLD_BUFFERED_SKB: skb has been buffered, don't pass it to upper layer.
 * @IWL_MLD_DROP_SKB: skb should be dropped and freed by the caller.
 */
enum iwl_mld_reorder_result {
	IWL_MLD_PASS_SKB,
	IWL_MLD_BUFFERED_SKB,
	IWL_MLD_DROP_SKB
};

int iwl_mld_ampdu_rx_start(struct iwl_mld *mld, struct ieee80211_sta *sta,
			   int tid, u16 ssn, u16 buf_size, u16 timeout);
int iwl_mld_ampdu_rx_stop(struct iwl_mld *mld, struct ieee80211_sta *sta,
			  int tid);

enum iwl_mld_reorder_result
iwl_mld_reorder(struct iwl_mld *mld, struct napi_struct *napi,
		int queue, struct ieee80211_sta *sta,
		struct sk_buff *skb, struct iwl_rx_mpdu_desc *desc);

void iwl_mld_handle_frame_release_notif(struct iwl_mld *mld,
					struct napi_struct *napi,
					struct iwl_rx_packet *pkt, int queue);
void iwl_mld_handle_bar_frame_release_notif(struct iwl_mld *mld,
					    struct napi_struct *napi,
					    struct iwl_rx_packet *pkt,
					    int queue);

void iwl_mld_del_ba(struct iwl_mld *mld, int queue,
		    struct iwl_mld_delba_data *data);

int iwl_mld_update_sta_baids(struct iwl_mld *mld,
			     u32 old_sta_mask,
			     u32 new_sta_mask);

#endif /* __iwl_agg_h__ */
