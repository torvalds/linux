// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2022 Intel Corporation
 */

#include "mvm.h"
#include "time-sync.h"
#include <linux/ieee80211.h>

void iwl_mvm_init_time_sync(struct iwl_time_sync_data *data)
{
	skb_queue_head_init(&data->frame_list);
}

static bool iwl_mvm_is_skb_match(struct sk_buff *skb, u8 *addr, u8 dialog_token)
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

static struct sk_buff *iwl_mvm_time_sync_find_skb(struct iwl_mvm *mvm, u8 *addr,
						  u8 dialog_token)
{
	struct sk_buff *skb;

	/* The queue is expected to have only one SKB. If there are other SKBs
	 * in the queue, they did not get a time sync notification and are
	 * probably obsolete by now, so drop them.
	 */
	while ((skb = skb_dequeue(&mvm->time_sync.frame_list))) {
		if (iwl_mvm_is_skb_match(skb, addr, dialog_token))
			break;

		kfree_skb(skb);
		skb = NULL;
	}

	return skb;
}

static u64 iwl_mvm_get_64_bit(__le32 high, __le32 low)
{
	return ((u64)le32_to_cpu(high) << 32) | le32_to_cpu(low);
}

void iwl_mvm_time_sync_msmt_event(struct iwl_mvm *mvm,
				  struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_time_msmt_notify *notif = (void *)pkt->data;
	struct ieee80211_rx_status *rx_status;
	struct skb_shared_hwtstamps *shwt;
	u64 ts_10ns;
	struct sk_buff *skb =
		iwl_mvm_time_sync_find_skb(mvm, notif->peer_addr,
					   le32_to_cpu(notif->dialog_token));
	u64 adj_time;

	if (!skb) {
		IWL_DEBUG_INFO(mvm, "Time sync event but no pending skb\n");
		return;
	}

	ts_10ns = iwl_mvm_get_64_bit(notif->t2_hi, notif->t2_lo);
	adj_time = iwl_mvm_ptp_get_adj_time(mvm, ts_10ns * 10);
	shwt = skb_hwtstamps(skb);
	shwt->hwtstamp = ktime_set(0, adj_time);

	ts_10ns = iwl_mvm_get_64_bit(notif->t3_hi, notif->t3_lo);
	adj_time = iwl_mvm_ptp_get_adj_time(mvm, ts_10ns * 10);
	rx_status = IEEE80211_SKB_RXCB(skb);
	rx_status->ack_tx_hwtstamp = ktime_set(0, adj_time);

	IWL_DEBUG_INFO(mvm,
		       "Time sync: RX event - report frame t2=%llu t3=%llu\n",
		       ktime_to_ns(shwt->hwtstamp),
		       ktime_to_ns(rx_status->ack_tx_hwtstamp));
	ieee80211_rx_napi(mvm->hw, NULL, skb, NULL);
}

void iwl_mvm_time_sync_msmt_confirm_event(struct iwl_mvm *mvm,
					  struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_time_msmt_cfm_notify *notif = (void *)pkt->data;
	struct ieee80211_tx_status status = {};
	struct skb_shared_hwtstamps *shwt;
	u64 ts_10ns, adj_time;

	status.skb =
		iwl_mvm_time_sync_find_skb(mvm, notif->peer_addr,
					   le32_to_cpu(notif->dialog_token));

	if (!status.skb) {
		IWL_DEBUG_INFO(mvm, "Time sync confirm but no pending skb\n");
		return;
	}

	ts_10ns = iwl_mvm_get_64_bit(notif->t1_hi, notif->t1_lo);
	adj_time = iwl_mvm_ptp_get_adj_time(mvm, ts_10ns * 10);
	shwt = skb_hwtstamps(status.skb);
	shwt->hwtstamp = ktime_set(0, adj_time);

	ts_10ns = iwl_mvm_get_64_bit(notif->t4_hi, notif->t4_lo);
	adj_time = iwl_mvm_ptp_get_adj_time(mvm, ts_10ns * 10);
	status.info = IEEE80211_SKB_CB(status.skb);
	status.ack_hwtstamp = ktime_set(0, adj_time);

	IWL_DEBUG_INFO(mvm,
		       "Time sync: TX event - report frame t1=%llu t4=%llu\n",
		       ktime_to_ns(shwt->hwtstamp),
		       ktime_to_ns(status.ack_hwtstamp));
	ieee80211_tx_status_ext(mvm->hw, &status);
}

int iwl_mvm_time_sync_config(struct iwl_mvm *mvm, const u8 *addr, u32 protocols)
{
	struct iwl_time_sync_cfg_cmd cmd = {};
	int err;

	lockdep_assert_held(&mvm->mutex);

	if (!fw_has_capa(&mvm->fw->ucode_capa,
			 IWL_UCODE_TLV_CAPA_TIME_SYNC_BOTH_FTM_TM))
		return -EINVAL;

	/* The fw only supports one peer. We do allow reconfiguration of the
	 * same peer for cases of fw reset etc.
	 */
	if (mvm->time_sync.active &&
	    !ether_addr_equal(addr, mvm->time_sync.peer_addr)) {
		IWL_DEBUG_INFO(mvm, "Time sync: reject config for peer: %pM\n",
			       addr);
		return -ENOBUFS;
	}

	if (protocols & ~(IWL_TIME_SYNC_PROTOCOL_TM |
			  IWL_TIME_SYNC_PROTOCOL_FTM))
		return -EINVAL;

	cmd.protocols = cpu_to_le32(protocols);

	ether_addr_copy(cmd.peer_addr, addr);

	err = iwl_mvm_send_cmd_pdu(mvm,
				   WIDE_ID(DATA_PATH_GROUP,
					   WNM_80211V_TIMING_MEASUREMENT_CONFIG_CMD),
				   0, sizeof(cmd), &cmd);
	if (err) {
		IWL_ERR(mvm, "Failed to send time sync cfg cmd: %d\n", err);
	} else {
		mvm->time_sync.active = protocols != 0;
		ether_addr_copy(mvm->time_sync.peer_addr, addr);
		IWL_DEBUG_INFO(mvm, "Time sync: set peer addr=%pM\n", addr);
	}

	if (!mvm->time_sync.active)
		skb_queue_purge(&mvm->time_sync.frame_list);

	return err;
}
