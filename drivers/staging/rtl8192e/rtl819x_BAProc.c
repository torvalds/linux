// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Contact Information: wlanfae <wlanfae@realtek.com>
 */
#include <asm/byteorder.h>
#include <linux/unaligned.h>
#include <linux/etherdevice.h>
#include "rtllib.h"
#include "rtl819x_BA.h"

static void activate_ba_entry(struct ba_record *ba, u16 time)
{
	ba->b_valid = true;
	if (time != 0)
		mod_timer(&ba->timer, jiffies + msecs_to_jiffies(time));
}

static void deactivate_ba_entry(struct rtllib_device *ieee, struct ba_record *ba)
{
	ba->b_valid = false;
	del_timer_sync(&ba->timer);
}

static u8 tx_ts_delete_ba(struct rtllib_device *ieee, struct tx_ts_record *ts)
{
	struct ba_record *admitted_ba = &ts->tx_admitted_ba_record;
	struct ba_record *pending_ba = &ts->tx_pending_ba_record;
	u8 send_del_ba = false;

	if (pending_ba->b_valid) {
		deactivate_ba_entry(ieee, pending_ba);
		send_del_ba = true;
	}

	if (admitted_ba->b_valid) {
		deactivate_ba_entry(ieee, admitted_ba);
		send_del_ba = true;
	}
	return send_del_ba;
}

static u8 rx_ts_delete_ba(struct rtllib_device *ieee, struct rx_ts_record *ts)
{
	struct ba_record *ba = &ts->rx_admitted_ba_record;
	u8			send_del_ba = false;

	if (ba->b_valid) {
		deactivate_ba_entry(ieee, ba);
		send_del_ba = true;
	}

	return send_del_ba;
}

void rtllib_reset_ba_entry(struct ba_record *ba)
{
	ba->b_valid                      = false;
	ba->ba_param_set.short_data      = 0;
	ba->ba_timeout_value             = 0;
	ba->dialog_token                 = 0;
	ba->ba_start_seq_ctrl.short_data = 0;
}

static struct sk_buff *rtllib_ADDBA(struct rtllib_device *ieee, u8 *dst,
				    struct ba_record *ba,
				    u16 status_code, u8 type)
{
	struct sk_buff *skb = NULL;
	struct ieee80211_hdr_3addr *ba_req = NULL;
	u8 *tag = NULL;
	u16 len = ieee->tx_headroom + 9;

	netdev_dbg(ieee->dev, "%s(): frame(%d) sentd to: %pM, ieee->dev:%p\n",
		   __func__, type, dst, ieee->dev);

	if (!ba) {
		netdev_warn(ieee->dev, "ba is NULL\n");
		return NULL;
	}
	skb = dev_alloc_skb(len + sizeof(struct ieee80211_hdr_3addr));
	if (!skb)
		return NULL;

	memset(skb->data, 0, sizeof(struct ieee80211_hdr_3addr));

	skb_reserve(skb, ieee->tx_headroom);

	ba_req = skb_put(skb, sizeof(struct ieee80211_hdr_3addr));

	ether_addr_copy(ba_req->addr1, dst);
	ether_addr_copy(ba_req->addr2, ieee->dev->dev_addr);

	ether_addr_copy(ba_req->addr3, ieee->current_network.bssid);
	ba_req->frame_control = cpu_to_le16(IEEE80211_STYPE_ACTION);

	tag = skb_put(skb, 9);
	*tag++ = ACT_CAT_BA;
	*tag++ = type;
	*tag++ = ba->dialog_token;

	if (type == ACT_ADDBARSP) {
		put_unaligned_le16(status_code, tag);
		tag += 2;
	}

	put_unaligned_le16(ba->ba_param_set.short_data, tag);
	tag += 2;

	put_unaligned_le16(ba->ba_timeout_value, tag);
	tag += 2;

	if (type == ACT_ADDBAREQ) {
		memcpy(tag, (u8 *)&ba->ba_start_seq_ctrl, 2);
		tag += 2;
	}

#ifdef VERBOSE_DEBUG
	print_hex_dump_bytes("%s: ", DUMP_PREFIX_NONE, skb->data,
			     __func__, skb->len);
#endif
	return skb;
}

static struct sk_buff *rtllib_DELBA(struct rtllib_device *ieee, u8 *dst,
				    struct ba_record *ba,
				    enum tr_select tx_rx_select, u16 reason_code)
{
	union delba_param_set del_ba_param_set;
	struct sk_buff *skb = NULL;
	struct ieee80211_hdr_3addr *del_ba = NULL;
	u8 *tag = NULL;
	u16 len = 6 + ieee->tx_headroom;

	if (net_ratelimit())
		netdev_dbg(ieee->dev, "%s(): reason_code(%d) sentd to: %pM\n",
			   __func__, reason_code, dst);

	memset(&del_ba_param_set, 0, 2);

	del_ba_param_set.field.initiator = (tx_rx_select == TX_DIR) ? 1 : 0;
	del_ba_param_set.field.tid	= ba->ba_param_set.field.tid;

	skb = dev_alloc_skb(len + sizeof(struct ieee80211_hdr_3addr));
	if (!skb)
		return NULL;

	skb_reserve(skb, ieee->tx_headroom);

	del_ba = skb_put(skb, sizeof(struct ieee80211_hdr_3addr));

	ether_addr_copy(del_ba->addr1, dst);
	ether_addr_copy(del_ba->addr2, ieee->dev->dev_addr);
	ether_addr_copy(del_ba->addr3, ieee->current_network.bssid);
	del_ba->frame_control = cpu_to_le16(IEEE80211_STYPE_ACTION);

	tag = skb_put(skb, 6);

	*tag++ = ACT_CAT_BA;
	*tag++ = ACT_DELBA;

	put_unaligned_le16(del_ba_param_set.short_data, tag);
	tag += 2;

	put_unaligned_le16(reason_code, tag);
	tag += 2;

#ifdef VERBOSE_DEBUG
	print_hex_dump_bytes("%s: ", DUMP_PREFIX_NONE, skb->data,
			     __func__, skb->len);
#endif
	return skb;
}

static void rtllib_send_add_ba_req(struct rtllib_device *ieee, u8 *dst,
				   struct ba_record *ba)
{
	struct sk_buff *skb;

	skb = rtllib_ADDBA(ieee, dst, ba, 0, ACT_ADDBAREQ);

	if (skb)
		softmac_mgmt_xmit(skb, ieee);
	else
		netdev_dbg(ieee->dev, "Failed to generate ADDBAReq packet.\n");
}

static void rtllib_send_add_ba_rsp(struct rtllib_device *ieee, u8 *dst,
				   struct ba_record *ba, u16 status_code)
{
	struct sk_buff *skb;

	skb = rtllib_ADDBA(ieee, dst, ba, status_code, ACT_ADDBARSP);
	if (skb)
		softmac_mgmt_xmit(skb, ieee);
	else
		netdev_dbg(ieee->dev, "Failed to generate ADDBARsp packet.\n");
}

static void rtllib_send_DELBA(struct rtllib_device *ieee, u8 *dst,
			      struct ba_record *ba, enum tr_select tx_rx_select,
			      u16 reason_code)
{
	struct sk_buff *skb;

	skb = rtllib_DELBA(ieee, dst, ba, tx_rx_select, reason_code);
	if (skb)
		softmac_mgmt_xmit(skb, ieee);
	else
		netdev_dbg(ieee->dev, "Failed to generate DELBA packet.\n");
}

int rtllib_rx_add_ba_req(struct rtllib_device *ieee, struct sk_buff *skb)
{
	struct ieee80211_hdr_3addr *req = NULL;
	u16 rc = 0;
	u8 *dst = NULL, *dialog_token = NULL, *tag = NULL;
	struct ba_record *ba = NULL;
	union ba_param_set *ba_param_set = NULL;
	u16 *ba_timeout_value = NULL;
	union sequence_control *ba_start_seq_ctrl = NULL;
	struct rx_ts_record *ts = NULL;

	if (skb->len < sizeof(struct ieee80211_hdr_3addr) + 9) {
		netdev_warn(ieee->dev, "Invalid skb len in BAREQ(%d / %d)\n",
			    (int)skb->len,
			    (int)(sizeof(struct ieee80211_hdr_3addr) + 9));
		return -1;
	}

#ifdef VERBOSE_DEBUG
	print_hex_dump_bytes("%s: ", DUMP_PREFIX_NONE, __func__,
			     skb->data, skb->len);
#endif

	req = (struct ieee80211_hdr_3addr *)skb->data;
	tag = (u8 *)req;
	dst = (u8 *)(&req->addr2[0]);
	tag += sizeof(struct ieee80211_hdr_3addr);
	dialog_token = tag + 2;
	ba_param_set = (union ba_param_set *)(tag + 3);
	ba_timeout_value = (u16 *)(tag + 5);
	ba_start_seq_ctrl = (union sequence_control *)(req + 7);

	if (!ieee->current_network.qos_data.active ||
	    !ieee->ht_info->current_ht_support ||
	    (ieee->ht_info->iot_action & HT_IOT_ACT_REJECT_ADDBA_REQ)) {
		rc = ADDBA_STATUS_REFUSED;
		netdev_warn(ieee->dev,
			    "Failed to reply on ADDBA_REQ as some capability is not ready(%d, %d)\n",
			    ieee->current_network.qos_data.active,
			    ieee->ht_info->current_ht_support);
		goto on_add_ba_req_fail;
	}
	if (!rtllib_get_ts(ieee, (struct ts_common_info **)&ts, dst,
			   (u8)(ba_param_set->field.tid), RX_DIR, true)) {
		rc = ADDBA_STATUS_REFUSED;
		netdev_warn(ieee->dev, "%s(): can't get TS\n", __func__);
		goto on_add_ba_req_fail;
	}
	ba = &ts->rx_admitted_ba_record;

	if (ba_param_set->field.ba_policy == BA_POLICY_DELAYED) {
		rc = ADDBA_STATUS_INVALID_PARAM;
		netdev_warn(ieee->dev, "%s(): BA Policy is not correct\n",
			    __func__);
		goto on_add_ba_req_fail;
	}

	rtllib_flush_rx_ts_pending_pkts(ieee, ts);

	deactivate_ba_entry(ieee, ba);
	ba->dialog_token = *dialog_token;
	ba->ba_param_set = *ba_param_set;
	ba->ba_timeout_value = *ba_timeout_value;
	ba->ba_start_seq_ctrl = *ba_start_seq_ctrl;

	if (ieee->get_half_nmode_support_by_aps_handler(ieee->dev) ||
	    (ieee->ht_info->iot_action & HT_IOT_ACT_ALLOW_PEER_AGG_ONE_PKT))
		ba->ba_param_set.field.buffer_size = 1;
	else
		ba->ba_param_set.field.buffer_size = 32;

	activate_ba_entry(ba, 0);
	rtllib_send_add_ba_rsp(ieee, dst, ba, ADDBA_STATUS_SUCCESS);

	return 0;

on_add_ba_req_fail:
	{
		struct ba_record BA;

		BA.ba_param_set = *ba_param_set;
		BA.ba_timeout_value = *ba_timeout_value;
		BA.dialog_token = *dialog_token;
		BA.ba_param_set.field.ba_policy = BA_POLICY_IMMEDIATE;
		rtllib_send_add_ba_rsp(ieee, dst, &BA, rc);
		return 0;
	}
}

int rtllib_rx_add_ba_rsp(struct rtllib_device *ieee, struct sk_buff *skb)
{
	struct ieee80211_hdr_3addr *rsp = NULL;
	struct ba_record *pending_ba, *admitted_ba;
	struct tx_ts_record *ts = NULL;
	u8 *dst = NULL, *dialog_token = NULL, *tag = NULL;
	u16 *status_code = NULL, *ba_timeout_value = NULL;
	union ba_param_set *ba_param_set = NULL;
	u16			reason_code;

	if (skb->len < sizeof(struct ieee80211_hdr_3addr) + 9) {
		netdev_warn(ieee->dev, "Invalid skb len in BARSP(%d / %d)\n",
			    (int)skb->len,
			    (int)(sizeof(struct ieee80211_hdr_3addr) + 9));
		return -1;
	}
	rsp = (struct ieee80211_hdr_3addr *)skb->data;
	tag = (u8 *)rsp;
	dst = (u8 *)(&rsp->addr2[0]);
	tag += sizeof(struct ieee80211_hdr_3addr);
	dialog_token = tag + 2;
	status_code = (u16 *)(tag + 3);
	ba_param_set = (union ba_param_set *)(tag + 5);
	ba_timeout_value = (u16 *)(tag + 7);

	if (!ieee->current_network.qos_data.active ||
	    !ieee->ht_info->current_ht_support ||
	    !ieee->ht_info->current_ampdu_enable) {
		netdev_warn(ieee->dev,
			    "reject to ADDBA_RSP as some capability is not ready(%d, %d, %d)\n",
			    ieee->current_network.qos_data.active,
			    ieee->ht_info->current_ht_support,
			    ieee->ht_info->current_ampdu_enable);
		reason_code = DELBA_REASON_UNKNOWN_BA;
		goto on_add_ba_rsp_reject;
	}

	if (!rtllib_get_ts(ieee, (struct ts_common_info **)&ts, dst,
			   (u8)(ba_param_set->field.tid), TX_DIR, false)) {
		netdev_warn(ieee->dev, "%s(): can't get TS\n", __func__);
		reason_code = DELBA_REASON_UNKNOWN_BA;
		goto on_add_ba_rsp_reject;
	}

	ts->add_ba_req_in_progress = false;
	pending_ba = &ts->tx_pending_ba_record;
	admitted_ba = &ts->tx_admitted_ba_record;

	if (admitted_ba->b_valid) {
		netdev_dbg(ieee->dev, "%s(): ADDBA response already admitted\n",
			   __func__);
		return -1;
	} else if (!pending_ba->b_valid ||
		   (*dialog_token != pending_ba->dialog_token)) {
		netdev_warn(ieee->dev,
			    "%s(): ADDBA Rsp. BA invalid, DELBA!\n",
			    __func__);
		reason_code = DELBA_REASON_UNKNOWN_BA;
		goto on_add_ba_rsp_reject;
	} else {
		netdev_dbg(ieee->dev,
			   "%s(): Recv ADDBA Rsp. BA is admitted! Status code:%X\n",
			   __func__, *status_code);
		deactivate_ba_entry(ieee, pending_ba);
	}

	if (*status_code == ADDBA_STATUS_SUCCESS) {
		if (ba_param_set->field.ba_policy == BA_POLICY_DELAYED) {
			ts->add_ba_req_delayed = true;
			deactivate_ba_entry(ieee, admitted_ba);
			reason_code = DELBA_REASON_END_BA;
			goto on_add_ba_rsp_reject;
		}

		admitted_ba->dialog_token = *dialog_token;
		admitted_ba->ba_timeout_value = *ba_timeout_value;
		admitted_ba->ba_start_seq_ctrl = pending_ba->ba_start_seq_ctrl;
		admitted_ba->ba_param_set = *ba_param_set;
		deactivate_ba_entry(ieee, admitted_ba);
		activate_ba_entry(admitted_ba, *ba_timeout_value);
	} else {
		ts->add_ba_req_delayed = true;
		ts->disable_add_ba = true;
		reason_code = DELBA_REASON_END_BA;
		goto on_add_ba_rsp_reject;
	}

	return 0;

on_add_ba_rsp_reject:
	{
		struct ba_record BA;

		BA.ba_param_set = *ba_param_set;
		rtllib_send_DELBA(ieee, dst, &BA, TX_DIR, reason_code);
		return 0;
	}
}

int rtllib_rx_DELBA(struct rtllib_device *ieee, struct sk_buff *skb)
{
	struct ieee80211_hdr_3addr *delba = NULL;
	union delba_param_set *del_ba_param_set = NULL;
	u8 *dst = NULL;

	if (skb->len < sizeof(struct ieee80211_hdr_3addr) + 6) {
		netdev_warn(ieee->dev, "Invalid skb len in DELBA(%d / %d)\n",
			    (int)skb->len,
			    (int)(sizeof(struct ieee80211_hdr_3addr) + 6));
		return -1;
	}

	if (!ieee->current_network.qos_data.active ||
	    !ieee->ht_info->current_ht_support) {
		netdev_warn(ieee->dev,
			    "received DELBA while QOS or HT is not supported(%d, %d)\n",
			    ieee->current_network. qos_data.active,
			    ieee->ht_info->current_ht_support);
		return -1;
	}

#ifdef VERBOSE_DEBUG
	print_hex_dump_bytes("%s: ", DUMP_PREFIX_NONE, skb->data,
			     __func__, skb->len);
#endif
	delba = (struct ieee80211_hdr_3addr *)skb->data;
	dst = (u8 *)(&delba->addr2[0]);
	del_ba_param_set = (union delba_param_set *)&delba->seq_ctrl + 2;

	if (del_ba_param_set->field.initiator == 1) {
		struct rx_ts_record *ts;

		if (!rtllib_get_ts(ieee, (struct ts_common_info **)&ts, dst,
				   (u8)del_ba_param_set->field.tid, RX_DIR, false)) {
			netdev_warn(ieee->dev,
				    "%s(): can't get TS for RXTS. dst:%pM TID:%d\n",
				    __func__, dst,
				    (u8)del_ba_param_set->field.tid);
			return -1;
		}

		rx_ts_delete_ba(ieee, ts);
	} else {
		struct tx_ts_record *ts;

		if (!rtllib_get_ts(ieee, (struct ts_common_info **)&ts, dst,
				   (u8)del_ba_param_set->field.tid, TX_DIR, false)) {
			netdev_warn(ieee->dev, "%s(): can't get TS for TXTS\n",
				    __func__);
			return -1;
		}

		ts->using_ba = false;
		ts->add_ba_req_in_progress = false;
		ts->add_ba_req_delayed = false;
		del_timer_sync(&ts->ts_add_ba_timer);
		tx_ts_delete_ba(ieee, ts);
	}
	return 0;
}

void rtllib_ts_init_add_ba(struct rtllib_device *ieee, struct tx_ts_record *ts,
			   u8 policy, u8	overwrite_pending)
{
	struct ba_record *ba = &ts->tx_pending_ba_record;

	if (ba->b_valid && !overwrite_pending)
		return;

	deactivate_ba_entry(ieee, ba);

	ba->dialog_token++;
	ba->ba_param_set.field.amsdu_support = 0;
	ba->ba_param_set.field.ba_policy = policy;
	ba->ba_param_set.field.tid = ts->ts_common_info.tspec.ts_id;
	ba->ba_param_set.field.buffer_size = 32;
	ba->ba_timeout_value = 0;
	ba->ba_start_seq_ctrl.field.seq_num = (ts->tx_cur_seq + 3) % 4096;

	activate_ba_entry(ba, BA_SETUP_TIMEOUT);

	rtllib_send_add_ba_req(ieee, ts->ts_common_info.addr, ba);
}

void rtllib_ts_init_del_ba(struct rtllib_device *ieee,
			   struct ts_common_info *ts_common_info,
			   enum tr_select tx_rx_select)
{
	if (tx_rx_select == TX_DIR) {
		struct tx_ts_record *ts =
			 (struct tx_ts_record *)ts_common_info;

		if (tx_ts_delete_ba(ieee, ts))
			rtllib_send_DELBA(ieee, ts_common_info->addr,
					  (ts->tx_admitted_ba_record.b_valid) ?
					 (&ts->tx_admitted_ba_record) :
					(&ts->tx_pending_ba_record),
					 tx_rx_select, DELBA_REASON_END_BA);
	} else if (tx_rx_select == RX_DIR) {
		struct rx_ts_record *ts =
				 (struct rx_ts_record *)ts_common_info;
		if (rx_ts_delete_ba(ieee, ts))
			rtllib_send_DELBA(ieee, ts_common_info->addr,
					  &ts->rx_admitted_ba_record,
					  tx_rx_select, DELBA_REASON_END_BA);
	}
}

void rtllib_ba_setup_timeout(struct timer_list *t)
{
	struct tx_ts_record *ts = from_timer(ts, t,
					      tx_pending_ba_record.timer);

	ts->add_ba_req_in_progress = false;
	ts->add_ba_req_delayed = true;
	ts->tx_pending_ba_record.b_valid = false;
}

void rtllib_tx_ba_inact_timeout(struct timer_list *t)
{
	struct tx_ts_record *ts = from_timer(ts, t,
					      tx_admitted_ba_record.timer);
	struct rtllib_device *ieee = container_of(ts, struct rtllib_device,
				     tx_ts_records[ts->num]);
	tx_ts_delete_ba(ieee, ts);
	rtllib_send_DELBA(ieee, ts->ts_common_info.addr,
			  &ts->tx_admitted_ba_record, TX_DIR,
			  DELBA_REASON_TIMEOUT);
}

void rtllib_rx_ba_inact_timeout(struct timer_list *t)
{
	struct rx_ts_record *ts = from_timer(ts, t,
					      rx_admitted_ba_record.timer);
	struct rtllib_device *ieee = container_of(ts, struct rtllib_device,
				     rx_ts_records[ts->num]);

	rx_ts_delete_ba(ieee, ts);
	rtllib_send_DELBA(ieee, ts->ts_common_info.addr,
			  &ts->rx_admitted_ba_record, RX_DIR,
			  DELBA_REASON_TIMEOUT);
}
