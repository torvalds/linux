// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2024-2025 Intel Corporation
 */
#include "agg.h"
#include "sta.h"
#include "hcmd.h"

static void
iwl_mld_reorder_release_frames(struct iwl_mld *mld, struct ieee80211_sta *sta,
			       struct napi_struct *napi,
			       struct iwl_mld_baid_data *baid_data,
			       struct iwl_mld_reorder_buffer *reorder_buf,
			       u16 nssn)
{
	struct iwl_mld_reorder_buf_entry *entries =
		&baid_data->entries[reorder_buf->queue *
				    baid_data->entries_per_queue];
	u16 ssn = reorder_buf->head_sn;

	while (ieee80211_sn_less(ssn, nssn)) {
		int index = ssn % baid_data->buf_size;
		struct sk_buff_head *skb_list = &entries[index].frames;
		struct sk_buff *skb;

		ssn = ieee80211_sn_inc(ssn);

		/* Empty the list. Will have more than one frame for A-MSDU.
		 * Empty list is valid as well since nssn indicates frames were
		 * received.
		 */
		while ((skb = __skb_dequeue(skb_list))) {
			iwl_mld_pass_packet_to_mac80211(mld, napi, skb,
							reorder_buf->queue,
							sta);
			reorder_buf->num_stored--;
		}
	}
	reorder_buf->head_sn = nssn;
}

static void iwl_mld_release_frames_from_notif(struct iwl_mld *mld,
					      struct napi_struct *napi,
					      u8 baid, u16 nssn, int queue)
{
	struct iwl_mld_reorder_buffer *reorder_buf;
	struct iwl_mld_baid_data *ba_data;
	struct ieee80211_link_sta *link_sta;
	u32 sta_id;

	IWL_DEBUG_HT(mld, "Frame release notification for BAID %u, NSSN %d\n",
		     baid, nssn);

	if (WARN_ON_ONCE(baid == IWL_RX_REORDER_DATA_INVALID_BAID ||
			 baid >= ARRAY_SIZE(mld->fw_id_to_ba)))
		return;

	rcu_read_lock();

	ba_data = rcu_dereference(mld->fw_id_to_ba[baid]);
	if (!ba_data) {
		IWL_DEBUG_HT(mld, "BAID %d not found in map\n", baid);
		goto out_unlock;
	}

	/* pick any STA ID to find the pointer */
	sta_id = ffs(ba_data->sta_mask) - 1;
	link_sta = rcu_dereference(mld->fw_id_to_link_sta[sta_id]);
	if (WARN_ON_ONCE(IS_ERR_OR_NULL(link_sta) || !link_sta->sta))
		goto out_unlock;

	reorder_buf = &ba_data->reorder_buf[queue];

	iwl_mld_reorder_release_frames(mld, link_sta->sta, napi, ba_data,
				       reorder_buf, nssn);
out_unlock:
	rcu_read_unlock();
}

void iwl_mld_handle_frame_release_notif(struct iwl_mld *mld,
					struct napi_struct *napi,
					struct iwl_rx_packet *pkt, int queue)
{
	struct iwl_frame_release *release = (void *)pkt->data;
	u32 pkt_len = iwl_rx_packet_payload_len(pkt);

	if (IWL_FW_CHECK(mld, pkt_len < sizeof(*release),
			 "Unexpected frame release notif size %u (expected %zu)\n",
			 pkt_len, sizeof(*release)))
		return;

	iwl_mld_release_frames_from_notif(mld, napi, release->baid,
					  le16_to_cpu(release->nssn),
					  queue);
}

void iwl_mld_handle_bar_frame_release_notif(struct iwl_mld *mld,
					    struct napi_struct *napi,
					    struct iwl_rx_packet *pkt,
					    int queue)
{
	struct iwl_bar_frame_release *release = (void *)pkt->data;
	struct iwl_mld_baid_data *baid_data;
	unsigned int baid, nssn, sta_id, tid;
	u32 pkt_len = iwl_rx_packet_payload_len(pkt);

	if (IWL_FW_CHECK(mld, pkt_len < sizeof(*release),
			 "Unexpected frame release notif size %u (expected %zu)\n",
			 pkt_len, sizeof(*release)))
		return;

	baid = le32_get_bits(release->ba_info,
			     IWL_BAR_FRAME_RELEASE_BAID_MASK);
	nssn = le32_get_bits(release->ba_info,
			     IWL_BAR_FRAME_RELEASE_NSSN_MASK);
	sta_id = le32_get_bits(release->sta_tid,
			       IWL_BAR_FRAME_RELEASE_STA_MASK);
	tid = le32_get_bits(release->sta_tid,
			    IWL_BAR_FRAME_RELEASE_TID_MASK);

	if (IWL_FW_CHECK(mld, baid >= ARRAY_SIZE(mld->fw_id_to_ba),
			 "BAR release: invalid BAID (%x)\n", baid))
		return;

	rcu_read_lock();
	baid_data = rcu_dereference(mld->fw_id_to_ba[baid]);
	if (!IWL_FW_CHECK(mld, !baid_data,
			  "Got valid BAID %d but not allocated, invalid BAR release!\n",
			  baid))
		goto out_unlock;

	if (IWL_FW_CHECK(mld, tid != baid_data->tid ||
			 sta_id > mld->fw->ucode_capa.num_stations ||
			 !(baid_data->sta_mask & BIT(sta_id)),
			 "BAID 0x%x is mapped to sta_mask:0x%x tid:%d, but BAR release received for sta:%d tid:%d\n",
			 baid, baid_data->sta_mask, baid_data->tid, sta_id,
			 tid))
		goto out_unlock;

	IWL_DEBUG_DROP(mld, "Received a BAR, expect packet loss: nssn %d\n",
		       nssn);

	iwl_mld_release_frames_from_notif(mld, napi, baid, nssn, queue);
out_unlock:
	rcu_read_unlock();
}

void iwl_mld_del_ba(struct iwl_mld *mld, int queue,
		    struct iwl_mld_delba_data *data)
{
	struct iwl_mld_baid_data *ba_data;
	struct iwl_mld_reorder_buffer *reorder_buf;
	struct ieee80211_link_sta *link_sta;
	u8 baid = data->baid;
	u32 sta_id;

	if (WARN_ONCE(baid >= IWL_MAX_BAID, "invalid BAID: %x\n", baid))
		return;

	rcu_read_lock();

	ba_data = rcu_dereference(mld->fw_id_to_ba[baid]);
	if (WARN_ON_ONCE(!ba_data))
		goto out_unlock;

	/* pick any STA ID to find the pointer */
	sta_id = ffs(ba_data->sta_mask) - 1;
	link_sta = rcu_dereference(mld->fw_id_to_link_sta[sta_id]);
	if (WARN_ON_ONCE(IS_ERR_OR_NULL(link_sta) || !link_sta->sta))
		goto out_unlock;

	reorder_buf = &ba_data->reorder_buf[queue];

	/* release all frames that are in the reorder buffer to the stack */
	iwl_mld_reorder_release_frames(mld, link_sta->sta, NULL,
				       ba_data, reorder_buf,
				       ieee80211_sn_add(reorder_buf->head_sn,
							ba_data->buf_size));
out_unlock:
	rcu_read_unlock();
}

/* Returns true if the MPDU was buffered\dropped, false if it should be passed
 * to upper layer.
 */
enum iwl_mld_reorder_result
iwl_mld_reorder(struct iwl_mld *mld, struct napi_struct *napi,
		int queue, struct ieee80211_sta *sta,
		struct sk_buff *skb, struct iwl_rx_mpdu_desc *desc)
{
	struct ieee80211_hdr *hdr = (void *)skb_mac_header(skb);
	struct iwl_mld_baid_data *baid_data;
	struct iwl_mld_reorder_buffer *buffer;
	struct iwl_mld_reorder_buf_entry *entries;
	struct iwl_mld_sta *mld_sta = iwl_mld_sta_from_mac80211(sta);
	struct iwl_mld_link_sta *mld_link_sta;
	u32 reorder = le32_to_cpu(desc->reorder_data);
	bool amsdu, last_subframe, is_old_sn, is_dup;
	u8 tid = ieee80211_get_tid(hdr);
	u8 baid;
	u16 nssn, sn;
	u32 sta_mask = 0;
	int index;
	u8 link_id;

	baid = u32_get_bits(reorder, IWL_RX_MPDU_REORDER_BAID_MASK);

	/* This also covers the case of receiving a Block Ack Request
	 * outside a BA session; we'll pass it to mac80211 and that
	 * then sends a delBA action frame.
	 * This also covers pure monitor mode, in which case we won't
	 * have any BA sessions.
	 */
	if (baid == IWL_RX_REORDER_DATA_INVALID_BAID)
		return IWL_MLD_PASS_SKB;

	/* no sta yet */
	if (WARN_ONCE(!sta,
		      "Got valid BAID without a valid station assigned\n"))
		return IWL_MLD_PASS_SKB;

	/* not a data packet */
	if (!ieee80211_is_data_qos(hdr->frame_control) ||
	    is_multicast_ether_addr(hdr->addr1))
		return IWL_MLD_PASS_SKB;

	if (unlikely(!ieee80211_is_data_present(hdr->frame_control)))
		return IWL_MLD_PASS_SKB;

	baid_data = rcu_dereference(mld->fw_id_to_ba[baid]);
	if (!baid_data) {
		IWL_DEBUG_HT(mld,
			     "Got valid BAID but no baid allocated, bypass re-ordering (BAID=%d reorder=0x%x)\n",
			     baid, reorder);
		return IWL_MLD_PASS_SKB;
	}

	for_each_mld_link_sta(mld_sta, mld_link_sta, link_id)
		sta_mask |= BIT(mld_link_sta->fw_id);

	/* verify the BAID is correctly mapped to the sta and tid */
	if (IWL_FW_CHECK(mld,
			 tid != baid_data->tid ||
			 !(sta_mask & baid_data->sta_mask),
			 "BAID 0x%x is mapped to sta_mask:0x%x tid:%d, but was received for sta_mask:0x%x tid:%d\n",
			 baid, baid_data->sta_mask, baid_data->tid,
			 sta_mask, tid))
		return IWL_MLD_PASS_SKB;

	buffer = &baid_data->reorder_buf[queue];
	entries = &baid_data->entries[queue * baid_data->entries_per_queue];

	is_old_sn = !!(reorder & IWL_RX_MPDU_REORDER_BA_OLD_SN);

	if (!buffer->valid && is_old_sn)
		return IWL_MLD_PASS_SKB;

	buffer->valid = true;

	is_dup = !!(desc->status & cpu_to_le32(IWL_RX_MPDU_STATUS_DUPLICATE));

	/* drop any duplicated or outdated packets */
	if (is_dup || is_old_sn)
		return IWL_MLD_DROP_SKB;

	sn = u32_get_bits(reorder, IWL_RX_MPDU_REORDER_SN_MASK);
	nssn = u32_get_bits(reorder, IWL_RX_MPDU_REORDER_NSSN_MASK);
	amsdu = desc->mac_flags2 & IWL_RX_MPDU_MFLG2_AMSDU;
	last_subframe = desc->amsdu_info & IWL_RX_MPDU_AMSDU_LAST_SUBFRAME;

	/* release immediately if allowed by nssn and no stored frames */
	if (!buffer->num_stored && ieee80211_sn_less(sn, nssn)) {
		if (!amsdu || last_subframe)
			buffer->head_sn = nssn;
		return IWL_MLD_PASS_SKB;
	}

	/* release immediately if there are no stored frames, and the sn is
	 * equal to the head.
	 * This can happen due to reorder timer, where NSSN is behind head_sn.
	 * When we released everything, and we got the next frame in the
	 * sequence, according to the NSSN we can't release immediately,
	 * while technically there is no hole and we can move forward.
	 */
	if (!buffer->num_stored && sn == buffer->head_sn) {
		if (!amsdu || last_subframe)
			buffer->head_sn = ieee80211_sn_inc(buffer->head_sn);
		return IWL_MLD_PASS_SKB;
	}

	/* put in reorder buffer */
	index = sn % baid_data->buf_size;
	__skb_queue_tail(&entries[index].frames, skb);
	buffer->num_stored++;

	/* We cannot trust NSSN for AMSDU sub-frames that are not the last. The
	 * reason is that NSSN advances on the first sub-frame, and may cause
	 * the reorder buffer to advance before all the sub-frames arrive.
	 *
	 * Example: reorder buffer contains SN 0 & 2, and we receive AMSDU with
	 * SN 1. NSSN for first sub frame will be 3 with the result of driver
	 * releasing SN 0,1, 2. When sub-frame 1 arrives - reorder buffer is
	 * already ahead and it will be dropped.
	 * If the last sub-frame is not on this queue - we will get frame
	 * release notification with up to date NSSN.
	 */
	if (!amsdu || last_subframe)
		iwl_mld_reorder_release_frames(mld, sta, napi, baid_data,
					       buffer, nssn);

	return IWL_MLD_BUFFERED_SKB;
}
EXPORT_SYMBOL_IF_IWLWIFI_KUNIT(iwl_mld_reorder);

static void iwl_mld_rx_agg_session_expired(struct timer_list *t)
{
	struct iwl_mld_baid_data *data =
		from_timer(data, t, session_timer);
	struct iwl_mld_baid_data __rcu **rcu_ptr = data->rcu_ptr;
	struct iwl_mld_baid_data *ba_data;
	struct ieee80211_link_sta *link_sta;
	struct iwl_mld_sta *mld_sta;
	unsigned long timeout;
	unsigned int sta_id;

	rcu_read_lock();

	ba_data = rcu_dereference(*rcu_ptr);
	if (WARN_ON(!ba_data))
		goto unlock;

	if (WARN_ON(!ba_data->timeout))
		goto unlock;

	timeout = ba_data->last_rx_timestamp +
		  TU_TO_JIFFIES(ba_data->timeout * 2);
	if (time_is_after_jiffies(timeout)) {
		mod_timer(&ba_data->session_timer, timeout);
		goto unlock;
	}

	/* timer expired, pick any STA ID to find the pointer */
	sta_id = ffs(ba_data->sta_mask) - 1;
	link_sta = rcu_dereference(ba_data->mld->fw_id_to_link_sta[sta_id]);

	/* sta should be valid unless the following happens:
	 * The firmware asserts which triggers a reconfig flow, but
	 * the reconfig fails before we set the pointer to sta into
	 * the fw_id_to_link_sta pointer table. mac80211 can't stop
	 * A-MPDU and hence the timer continues to run. Then, the
	 * timer expires and sta is NULL.
	 */
	if (IS_ERR_OR_NULL(link_sta) || WARN_ON(!link_sta->sta))
		goto unlock;

	mld_sta = iwl_mld_sta_from_mac80211(link_sta->sta);
	ieee80211_rx_ba_timer_expired(mld_sta->vif, link_sta->sta->addr,
				      ba_data->tid);
unlock:
	rcu_read_unlock();
}

static int
iwl_mld_stop_ba_in_fw(struct iwl_mld *mld, struct ieee80211_sta *sta, int tid)
{
	struct iwl_rx_baid_cfg_cmd cmd = {
		.action = cpu_to_le32(IWL_RX_BAID_ACTION_REMOVE),
		.remove.sta_id_mask =
			cpu_to_le32(iwl_mld_fw_sta_id_mask(mld, sta)),
		.remove.tid = cpu_to_le32(tid),

	};
	int ret;

	ret = iwl_mld_send_cmd_pdu(mld,
				   WIDE_ID(DATA_PATH_GROUP,
					   RX_BAID_ALLOCATION_CONFIG_CMD),
				   &cmd);
	if (ret)
		return ret;

	IWL_DEBUG_HT(mld, "RX BA Session stopped in fw\n");

	return ret;
}

static int
iwl_mld_start_ba_in_fw(struct iwl_mld *mld, struct ieee80211_sta *sta,
		       int tid, u16 ssn, u16 buf_size)
{
	struct iwl_rx_baid_cfg_cmd cmd = {
		.action = cpu_to_le32(IWL_RX_BAID_ACTION_ADD),
		.alloc.sta_id_mask =
			cpu_to_le32(iwl_mld_fw_sta_id_mask(mld, sta)),
		.alloc.tid = tid,
		.alloc.ssn = cpu_to_le16(ssn),
		.alloc.win_size = cpu_to_le16(buf_size),
	};
	struct iwl_host_cmd hcmd = {
		.id = WIDE_ID(DATA_PATH_GROUP, RX_BAID_ALLOCATION_CONFIG_CMD),
		.flags = CMD_WANT_SKB,
		.len[0] = sizeof(cmd),
		.data[0] = &cmd,
	};
	struct iwl_rx_baid_cfg_resp *resp;
	struct iwl_rx_packet *pkt;
	u32 resp_len;
	int ret, baid;

	BUILD_BUG_ON(sizeof(*resp) != sizeof(baid));

	ret = iwl_mld_send_cmd(mld, &hcmd);
	if (ret)
		return ret;

	pkt = hcmd.resp_pkt;

	resp_len = iwl_rx_packet_payload_len(pkt);
	if (IWL_FW_CHECK(mld, resp_len != sizeof(*resp),
			 "BAID_ALLOC_CMD: unexpected response length %d\n",
			 resp_len)) {
		ret = -EIO;
		goto out;
	}

	IWL_DEBUG_HT(mld, "RX BA Session started in fw\n");

	resp = (void *)pkt->data;
	baid = le32_to_cpu(resp->baid);

	if (IWL_FW_CHECK(mld, baid < 0 || baid >= ARRAY_SIZE(mld->fw_id_to_ba),
			 "BAID_ALLOC_CMD: invalid BAID response %d\n", baid)) {
		ret = -EINVAL;
		goto out;
	}

	ret = baid;
out:
	iwl_free_resp(&hcmd);
	return ret;
}

static void iwl_mld_init_reorder_buffer(struct iwl_mld *mld,
					struct iwl_mld_baid_data *data,
					u16 ssn)
{
	for (int i = 0; i < mld->trans->num_rx_queues; i++) {
		struct iwl_mld_reorder_buffer *reorder_buf =
			&data->reorder_buf[i];
		struct iwl_mld_reorder_buf_entry *entries =
			&data->entries[i * data->entries_per_queue];

		reorder_buf->head_sn = ssn;
		reorder_buf->queue = i;

		for (int j = 0; j < data->buf_size; j++)
			__skb_queue_head_init(&entries[j].frames);
	}
}

static void iwl_mld_free_reorder_buffer(struct iwl_mld *mld,
					struct iwl_mld_baid_data *data)
{
	struct iwl_mld_delba_data delba_data = {
		.baid = data->baid,
	};

	iwl_mld_sync_rx_queues(mld, IWL_MLD_RXQ_NOTIF_DEL_BA,
			       &delba_data, sizeof(delba_data));

	for (int i = 0; i < mld->trans->num_rx_queues; i++) {
		struct iwl_mld_reorder_buffer *reorder_buf =
			&data->reorder_buf[i];
		struct iwl_mld_reorder_buf_entry *entries =
			&data->entries[i * data->entries_per_queue];

		if (likely(!reorder_buf->num_stored))
			continue;

		/* This shouldn't happen in regular DELBA since the RX queues
		 * sync internal DELBA notification should trigger a release
		 * of all frames in the reorder buffer.
		 */
		WARN_ON(1);

		for (int j = 0; j < data->buf_size; j++)
			__skb_queue_purge(&entries[j].frames);
	}
}

int iwl_mld_ampdu_rx_start(struct iwl_mld *mld, struct ieee80211_sta *sta,
			   int tid, u16 ssn, u16 buf_size, u16 timeout)
{
	struct iwl_mld_sta *mld_sta = iwl_mld_sta_from_mac80211(sta);
	struct iwl_mld_baid_data *baid_data = NULL;
	u32 reorder_buf_size = buf_size * sizeof(baid_data->entries[0]);
	int ret, baid;
	u32 sta_mask;

	lockdep_assert_wiphy(mld->wiphy);

	if (mld->num_rx_ba_sessions >= IWL_MAX_BAID) {
		IWL_DEBUG_HT(mld,
			     "Max num of RX BA sessions reached; blocking new session\n");
		return -ENOSPC;
	}

	sta_mask = iwl_mld_fw_sta_id_mask(mld, sta);
	if (WARN_ON(!sta_mask))
		return -EINVAL;

	/* sparse doesn't like the __align() so don't check */
#ifndef __CHECKER__
	/* The division below will be OK if either the cache line size
	 * can be divided by the entry size (ALIGN will round up) or if
	 * the entry size can be divided by the cache line size, in which
	 * case the ALIGN() will do nothing.
	 */
	BUILD_BUG_ON(SMP_CACHE_BYTES % sizeof(baid_data->entries[0]) &&
		     sizeof(baid_data->entries[0]) % SMP_CACHE_BYTES);
#endif

	/* Upward align the reorder buffer size to fill an entire cache
	 * line for each queue, to avoid sharing cache lines between
	 * different queues.
	 */
	reorder_buf_size = ALIGN(reorder_buf_size, SMP_CACHE_BYTES);

	/* Allocate here so if allocation fails we can bail out early
	 * before starting the BA session in the firmware
	 */
	baid_data = kzalloc(sizeof(*baid_data) +
			    mld->trans->num_rx_queues * reorder_buf_size,
			    GFP_KERNEL);
	if (!baid_data)
		return -ENOMEM;

	/* This division is why we need the above BUILD_BUG_ON(),
	 * if that doesn't hold then this will not be right.
	 */
	baid_data->entries_per_queue =
		reorder_buf_size / sizeof(baid_data->entries[0]);

	baid = iwl_mld_start_ba_in_fw(mld, sta, tid, ssn, buf_size);
	if (baid < 0) {
		ret = baid;
		goto out_free;
	}

	mld->num_rx_ba_sessions++;
	mld_sta->tid_to_baid[tid] = baid;

	baid_data->baid = baid;
	baid_data->mld = mld;
	baid_data->tid = tid;
	baid_data->buf_size = buf_size;
	baid_data->sta_mask = sta_mask;
	baid_data->timeout = timeout;
	baid_data->last_rx_timestamp = jiffies;
	baid_data->rcu_ptr = &mld->fw_id_to_ba[baid];

	iwl_mld_init_reorder_buffer(mld, baid_data, ssn);

	timer_setup(&baid_data->session_timer, iwl_mld_rx_agg_session_expired,
		    0);
	if (timeout)
		mod_timer(&baid_data->session_timer,
			  TU_TO_EXP_TIME(timeout * 2));

	IWL_DEBUG_HT(mld, "STA mask=0x%x (tid=%d) is assigned to BAID %d\n",
		     baid_data->sta_mask, tid, baid);

	/* protect the BA data with RCU to cover a case where our
	 * internal RX sync mechanism will timeout (not that it's
	 * supposed to happen) and we will free the session data while
	 * RX is being processed in parallel
	 */
	WARN_ON(rcu_access_pointer(mld->fw_id_to_ba[baid]));
	rcu_assign_pointer(mld->fw_id_to_ba[baid], baid_data);

	return 0;

out_free:
	kfree(baid_data);
	return ret;
}

int iwl_mld_ampdu_rx_stop(struct iwl_mld *mld, struct ieee80211_sta *sta,
			  int tid)
{
	struct iwl_mld_sta *mld_sta = iwl_mld_sta_from_mac80211(sta);
	int baid = mld_sta->tid_to_baid[tid];
	struct iwl_mld_baid_data *baid_data;
	int ret;

	lockdep_assert_wiphy(mld->wiphy);

	/* during firmware restart, do not send the command as the firmware no
	 * longer recognizes the session. instead, only clear the driver BA
	 * session data.
	 */
	if (!mld->fw_status.in_hw_restart) {
		ret = iwl_mld_stop_ba_in_fw(mld, sta, tid);
		if (ret)
			return ret;
	}

	if (!WARN_ON(mld->num_rx_ba_sessions == 0))
		mld->num_rx_ba_sessions--;

	baid_data = wiphy_dereference(mld->wiphy, mld->fw_id_to_ba[baid]);
	if (WARN_ON(!baid_data))
		return -EINVAL;

	if (timer_pending(&baid_data->session_timer))
		timer_shutdown_sync(&baid_data->session_timer);

	iwl_mld_free_reorder_buffer(mld, baid_data);

	RCU_INIT_POINTER(mld->fw_id_to_ba[baid], NULL);
	kfree_rcu(baid_data, rcu_head);

	IWL_DEBUG_HT(mld, "BAID %d is free\n", baid);

	return 0;
}

int iwl_mld_update_sta_baids(struct iwl_mld *mld,
			     u32 old_sta_mask,
			     u32 new_sta_mask)
{
	struct iwl_rx_baid_cfg_cmd cmd = {
		.action = cpu_to_le32(IWL_RX_BAID_ACTION_MODIFY),
		.modify.old_sta_id_mask = cpu_to_le32(old_sta_mask),
		.modify.new_sta_id_mask = cpu_to_le32(new_sta_mask),
	};
	u32 cmd_id = WIDE_ID(DATA_PATH_GROUP, RX_BAID_ALLOCATION_CONFIG_CMD);
	int baid;

	/* mac80211 will remove sessions later, but we ignore all that */
	if (mld->fw_status.in_hw_restart)
		return 0;

	BUILD_BUG_ON(sizeof(struct iwl_rx_baid_cfg_resp) != sizeof(baid));

	for (baid = 0; baid < ARRAY_SIZE(mld->fw_id_to_ba); baid++) {
		struct iwl_mld_baid_data *data;
		int ret;

		data = wiphy_dereference(mld->wiphy, mld->fw_id_to_ba[baid]);
		if (!data)
			continue;

		if (!(data->sta_mask & old_sta_mask))
			continue;

		WARN_ONCE(data->sta_mask != old_sta_mask,
			  "BAID data for %d corrupted - expected 0x%x found 0x%x\n",
			  baid, old_sta_mask, data->sta_mask);

		cmd.modify.tid = cpu_to_le32(data->tid);

		ret = iwl_mld_send_cmd_pdu(mld, cmd_id, &cmd);
		if (ret)
			return ret;
		data->sta_mask = new_sta_mask;
	}

	return 0;
}
