// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2025 Intel Corporation
 */

#include "mld.h"
#include "hcmd.h"
#include "ptp.h"
#include "time_sync.h"
#include <linux/ieee80211.h>

static int iwl_mld_init_time_sync(struct iwl_mld *mld, u32 protocols,
				  const u8 *addr)
{
	struct iwl_mld_time_sync_data *time_sync = kzalloc(sizeof(*time_sync),
							   GFP_KERNEL);

	if (!time_sync)
		return -ENOMEM;

	time_sync->active_protocols = protocols;
	ether_addr_copy(time_sync->peer_addr, addr);
	skb_queue_head_init(&time_sync->frame_list);
	rcu_assign_pointer(mld->time_sync, time_sync);

	return 0;
}

int iwl_mld_time_sync_fw_config(struct iwl_mld *mld)
{
	struct iwl_time_sync_cfg_cmd cmd = {};
	struct iwl_mld_time_sync_data *time_sync;
	int err;

	time_sync = wiphy_dereference(mld->wiphy, mld->time_sync);
	if (!time_sync)
		return -EINVAL;

	cmd.protocols = cpu_to_le32(time_sync->active_protocols);
	ether_addr_copy(cmd.peer_addr, time_sync->peer_addr);

	err = iwl_mld_send_cmd_pdu(mld,
				   WIDE_ID(DATA_PATH_GROUP,
					   WNM_80211V_TIMING_MEASUREMENT_CONFIG_CMD),
				   &cmd);
	if (err)
		IWL_ERR(mld, "Failed to send time sync cfg cmd: %d\n", err);

	return err;
}

int iwl_mld_time_sync_config(struct iwl_mld *mld, const u8 *addr, u32 protocols)
{
	struct iwl_mld_time_sync_data *time_sync;
	int err;

	time_sync = wiphy_dereference(mld->wiphy, mld->time_sync);

	/* The fw only supports one peer. We do allow reconfiguration of the
	 * same peer for cases of fw reset etc.
	 */
	if (time_sync && time_sync->active_protocols &&
	    !ether_addr_equal(addr, time_sync->peer_addr)) {
		IWL_DEBUG_INFO(mld, "Time sync: reject config for peer: %pM\n",
			       addr);
		return -ENOBUFS;
	}

	if (protocols & ~(IWL_TIME_SYNC_PROTOCOL_TM |
			  IWL_TIME_SYNC_PROTOCOL_FTM))
		return -EINVAL;

	IWL_DEBUG_INFO(mld, "Time sync: set peer addr=%pM\n", addr);

	iwl_mld_deinit_time_sync(mld);
	err = iwl_mld_init_time_sync(mld, protocols, addr);
	if (err)
		return err;

	err = iwl_mld_time_sync_fw_config(mld);
	return err;
}

void iwl_mld_deinit_time_sync(struct iwl_mld *mld)
{
	struct iwl_mld_time_sync_data *time_sync =
		wiphy_dereference(mld->wiphy, mld->time_sync);

	if (!time_sync)
		return;

	RCU_INIT_POINTER(mld->time_sync, NULL);
	skb_queue_purge(&time_sync->frame_list);
	kfree_rcu(time_sync, rcu_head);
}

bool iwl_mld_time_sync_frame(struct iwl_mld *mld, struct sk_buff *skb, u8 *addr)
{
	struct iwl_mld_time_sync_data *time_sync;

	rcu_read_lock();
	time_sync = rcu_dereference(mld->time_sync);
	if (time_sync && ether_addr_equal(time_sync->peer_addr, addr) &&
	    (ieee80211_is_timing_measurement(skb) || ieee80211_is_ftm(skb))) {
		skb_queue_tail(&time_sync->frame_list, skb);
		rcu_read_unlock();
		return true;
	}
	rcu_read_unlock();

	return false;
}

static bool iwl_mld_is_skb_match(struct sk_buff *skb, u8 *addr, u8 dialog_token)
{
	struct ieee80211_mgmt *mgmt = (void *)skb->data;
	u8 skb_dialog_token;

	if (ieee80211_is_timing_measurement(skb))
		skb_dialog_token = mgmt->u.action.u.wnm_timing_msr.dialog_token;
	else
		skb_dialog_token = mgmt->u.action.u.ftm.dialog_token;

	if ((ether_addr_equal(mgmt->sa, addr) ||
	     ether_addr_equal(mgmt->da, addr)) &&
	    skb_dialog_token == dialog_token)
		return true;

	return false;
}

static struct sk_buff *iwl_mld_time_sync_find_skb(struct iwl_mld *mld, u8 *addr,
						  u8 dialog_token)
{
	struct iwl_mld_time_sync_data *time_sync;
	struct sk_buff *skb;

	rcu_read_lock();

	time_sync = rcu_dereference(mld->time_sync);
	if (IWL_FW_CHECK(mld, !time_sync,
			 "Time sync notification but time sync is not initialized\n")) {
		rcu_read_unlock();
		return NULL;
	}

	/* The notifications are expected to arrive in the same order of the
	 * frames. If the incoming notification doesn't match the first SKB
	 * in the queue, it means there was no time sync notification for this
	 * SKB and it can be dropped.
	 */
	while ((skb = skb_dequeue(&time_sync->frame_list))) {
		if (iwl_mld_is_skb_match(skb, addr, dialog_token))
			break;

		kfree_skb(skb);
		skb = NULL;
		IWL_DEBUG_DROP(mld,
			       "Time sync: drop SKB without matching notification\n");
	}
	rcu_read_unlock();

	return skb;
}

static u64 iwl_mld_get_64_bit(__le32 high, __le32 low)
{
	return ((u64)le32_to_cpu(high) << 32) | le32_to_cpu(low);
}

void iwl_mld_handle_time_msmt_notif(struct iwl_mld *mld,
				    struct iwl_rx_packet *pkt)
{
	struct ptp_data *data = &mld->ptp_data;
	struct iwl_time_msmt_notify *notif = (void *)pkt->data;
	struct ieee80211_rx_status *rx_status;
	struct skb_shared_hwtstamps *shwt;
	u64 ts_10ns;
	struct sk_buff *skb =
		iwl_mld_time_sync_find_skb(mld, notif->peer_addr,
					   le32_to_cpu(notif->dialog_token));
	u64 adj_time;

	if (IWL_FW_CHECK(mld, !skb, "Time sync event but no pending skb\n"))
		return;

	spin_lock_bh(&data->lock);
	ts_10ns = iwl_mld_get_64_bit(notif->t2_hi, notif->t2_lo);
	adj_time = iwl_mld_ptp_get_adj_time(mld, ts_10ns * 10);
	shwt = skb_hwtstamps(skb);
	shwt->hwtstamp = ktime_set(0, adj_time);

	ts_10ns = iwl_mld_get_64_bit(notif->t3_hi, notif->t3_lo);
	adj_time = iwl_mld_ptp_get_adj_time(mld, ts_10ns * 10);
	rx_status = IEEE80211_SKB_RXCB(skb);
	rx_status->ack_tx_hwtstamp = ktime_set(0, adj_time);
	spin_unlock_bh(&data->lock);

	IWL_DEBUG_INFO(mld,
		       "Time sync: RX event - report frame t2=%llu t3=%llu\n",
		       ktime_to_ns(shwt->hwtstamp),
		       ktime_to_ns(rx_status->ack_tx_hwtstamp));
	ieee80211_rx_napi(mld->hw, NULL, skb, NULL);
}

void iwl_mld_handle_time_sync_confirm_notif(struct iwl_mld *mld,
					    struct iwl_rx_packet *pkt)
{
	struct ptp_data *data = &mld->ptp_data;
	struct iwl_time_msmt_cfm_notify *notif = (void *)pkt->data;
	struct ieee80211_tx_status status = {};
	struct skb_shared_hwtstamps *shwt;
	u64 ts_10ns, adj_time;

	status.skb =
		iwl_mld_time_sync_find_skb(mld, notif->peer_addr,
					   le32_to_cpu(notif->dialog_token));

	if (IWL_FW_CHECK(mld, !status.skb,
			 "Time sync confirm but no pending skb\n"))
		return;

	spin_lock_bh(&data->lock);
	ts_10ns = iwl_mld_get_64_bit(notif->t1_hi, notif->t1_lo);
	adj_time = iwl_mld_ptp_get_adj_time(mld, ts_10ns * 10);
	shwt = skb_hwtstamps(status.skb);
	shwt->hwtstamp = ktime_set(0, adj_time);

	ts_10ns = iwl_mld_get_64_bit(notif->t4_hi, notif->t4_lo);
	adj_time = iwl_mld_ptp_get_adj_time(mld, ts_10ns * 10);
	status.info = IEEE80211_SKB_CB(status.skb);
	status.ack_hwtstamp = ktime_set(0, adj_time);
	spin_unlock_bh(&data->lock);

	IWL_DEBUG_INFO(mld,
		       "Time sync: TX event - report frame t1=%llu t4=%llu\n",
		       ktime_to_ns(shwt->hwtstamp),
		       ktime_to_ns(status.ack_hwtstamp));
	ieee80211_tx_status_ext(mld->hw, &status);
}
