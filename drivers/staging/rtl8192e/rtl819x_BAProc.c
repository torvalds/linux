// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Contact Information: wlanfae <wlanfae@realtek.com>
 */
#include <asm/byteorder.h>
#include <asm/unaligned.h>
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

static u8 tx_ts_delete_ba(struct rtllib_device *ieee, struct tx_ts_record *pTxTs)
{
	struct ba_record *admitted_ba = &pTxTs->TxAdmittedBARecord;
	struct ba_record *pending_ba = &pTxTs->TxPendingBARecord;
	u8 bSendDELBA = false;

	if (pending_ba->b_valid) {
		deactivate_ba_entry(ieee, pending_ba);
		bSendDELBA = true;
	}

	if (admitted_ba->b_valid) {
		deactivate_ba_entry(ieee, admitted_ba);
		bSendDELBA = true;
	}
	return bSendDELBA;
}

static u8 rx_ts_delete_ba(struct rtllib_device *ieee, struct rx_ts_record *ts)
{
	struct ba_record *ba = &ts->rx_admitted_ba_record;
	u8			bSendDELBA = false;

	if (ba->b_valid) {
		deactivate_ba_entry(ieee, ba);
		bSendDELBA = true;
	}

	return bSendDELBA;
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
	struct ieee80211_hdr_3addr *BAReq = NULL;
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

	BAReq = skb_put(skb, sizeof(struct ieee80211_hdr_3addr));

	ether_addr_copy(BAReq->addr1, dst);
	ether_addr_copy(BAReq->addr2, ieee->dev->dev_addr);

	ether_addr_copy(BAReq->addr3, ieee->current_network.bssid);
	BAReq->frame_control = cpu_to_le16(IEEE80211_STYPE_ACTION);

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
				    enum tr_select TxRxSelect, u16 reason_code)
{
	union delba_param_set DelbaParamSet;
	struct sk_buff *skb = NULL;
	struct ieee80211_hdr_3addr *Delba = NULL;
	u8 *tag = NULL;
	u16 len = 6 + ieee->tx_headroom;

	if (net_ratelimit())
		netdev_dbg(ieee->dev, "%s(): reason_code(%d) sentd to: %pM\n",
			   __func__, reason_code, dst);

	memset(&DelbaParamSet, 0, 2);

	DelbaParamSet.field.initiator = (TxRxSelect == TX_DIR) ? 1 : 0;
	DelbaParamSet.field.tid	= ba->ba_param_set.field.tid;

	skb = dev_alloc_skb(len + sizeof(struct ieee80211_hdr_3addr));
	if (!skb)
		return NULL;

	skb_reserve(skb, ieee->tx_headroom);

	Delba = skb_put(skb, sizeof(struct ieee80211_hdr_3addr));

	ether_addr_copy(Delba->addr1, dst);
	ether_addr_copy(Delba->addr2, ieee->dev->dev_addr);
	ether_addr_copy(Delba->addr3, ieee->current_network.bssid);
	Delba->frame_control = cpu_to_le16(IEEE80211_STYPE_ACTION);

	tag = skb_put(skb, 6);

	*tag++ = ACT_CAT_BA;
	*tag++ = ACT_DELBA;

	put_unaligned_le16(DelbaParamSet.short_data, tag);
	tag += 2;

	put_unaligned_le16(reason_code, tag);
	tag += 2;

#ifdef VERBOSE_DEBUG
	print_hex_dump_bytes("%s: ", DUMP_PREFIX_NONE, skb->data,
			     __func__, skb->len);
#endif
	return skb;
}

static void rtllib_send_ADDBAReq(struct rtllib_device *ieee, u8 *dst,
				 struct ba_record *ba)
{
	struct sk_buff *skb;

	skb = rtllib_ADDBA(ieee, dst, ba, 0, ACT_ADDBAREQ);

	if (skb)
		softmac_mgmt_xmit(skb, ieee);
	else
		netdev_dbg(ieee->dev, "Failed to generate ADDBAReq packet.\n");
}

static void rtllib_send_ADDBARsp(struct rtllib_device *ieee, u8 *dst,
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
			      struct ba_record *ba, enum tr_select TxRxSelect,
			      u16 reason_code)
{
	struct sk_buff *skb;

	skb = rtllib_DELBA(ieee, dst, ba, TxRxSelect, reason_code);
	if (skb)
		softmac_mgmt_xmit(skb, ieee);
	else
		netdev_dbg(ieee->dev, "Failed to generate DELBA packet.\n");
}

int rtllib_rx_ADDBAReq(struct rtllib_device *ieee, struct sk_buff *skb)
{
	struct ieee80211_hdr_3addr *req = NULL;
	u16 rc = 0;
	u8 *dst = NULL, *pDialogToken = NULL, *tag = NULL;
	struct ba_record *ba = NULL;
	union ba_param_set *pBaParamSet = NULL;
	u16 *pBaTimeoutVal = NULL;
	union sequence_control *pBaStartSeqCtrl = NULL;
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
	pDialogToken = tag + 2;
	pBaParamSet = (union ba_param_set *)(tag + 3);
	pBaTimeoutVal = (u16 *)(tag + 5);
	pBaStartSeqCtrl = (union sequence_control *)(req + 7);

	if (!ieee->current_network.qos_data.active ||
	    !ieee->ht_info->current_ht_support ||
	    (ieee->ht_info->iot_action & HT_IOT_ACT_REJECT_ADDBA_REQ)) {
		rc = ADDBA_STATUS_REFUSED;
		netdev_warn(ieee->dev,
			    "Failed to reply on ADDBA_REQ as some capability is not ready(%d, %d)\n",
			    ieee->current_network.qos_data.active,
			    ieee->ht_info->current_ht_support);
		goto OnADDBAReq_Fail;
	}
	if (!rtllib_get_ts(ieee, (struct ts_common_info **)&ts, dst,
		   (u8)(pBaParamSet->field.tid), RX_DIR, true)) {
		rc = ADDBA_STATUS_REFUSED;
		netdev_warn(ieee->dev, "%s(): can't get TS\n", __func__);
		goto OnADDBAReq_Fail;
	}
	ba = &ts->rx_admitted_ba_record;

	if (pBaParamSet->field.ba_policy == BA_POLICY_DELAYED) {
		rc = ADDBA_STATUS_INVALID_PARAM;
		netdev_warn(ieee->dev, "%s(): BA Policy is not correct\n",
			    __func__);
		goto OnADDBAReq_Fail;
	}

	rtllib_FlushRxTsPendingPkts(ieee, ts);

	deactivate_ba_entry(ieee, ba);
	ba->dialog_token = *pDialogToken;
	ba->ba_param_set = *pBaParamSet;
	ba->ba_timeout_value = *pBaTimeoutVal;
	ba->ba_start_seq_ctrl = *pBaStartSeqCtrl;

	if (ieee->GetHalfNmodeSupportByAPsHandler(ieee->dev) ||
	   (ieee->ht_info->iot_action & HT_IOT_ACT_ALLOW_PEER_AGG_ONE_PKT))
		ba->ba_param_set.field.buffer_size = 1;
	else
		ba->ba_param_set.field.buffer_size = 32;

	activate_ba_entry(ba, 0);
	rtllib_send_ADDBARsp(ieee, dst, ba, ADDBA_STATUS_SUCCESS);

	return 0;

OnADDBAReq_Fail:
	{
		struct ba_record BA;

		BA.ba_param_set = *pBaParamSet;
		BA.ba_timeout_value = *pBaTimeoutVal;
		BA.dialog_token = *pDialogToken;
		BA.ba_param_set.field.ba_policy = BA_POLICY_IMMEDIATE;
		rtllib_send_ADDBARsp(ieee, dst, &BA, rc);
		return 0;
	}
}

int rtllib_rx_ADDBARsp(struct rtllib_device *ieee, struct sk_buff *skb)
{
	struct ieee80211_hdr_3addr *rsp = NULL;
	struct ba_record *pending_ba, *pAdmittedBA;
	struct tx_ts_record *ts = NULL;
	u8 *dst = NULL, *pDialogToken = NULL, *tag = NULL;
	u16 *status_code = NULL, *pBaTimeoutVal = NULL;
	union ba_param_set *pBaParamSet = NULL;
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
	pDialogToken = tag + 2;
	status_code = (u16 *)(tag + 3);
	pBaParamSet = (union ba_param_set *)(tag + 5);
	pBaTimeoutVal = (u16 *)(tag + 7);

	if (!ieee->current_network.qos_data.active ||
	    !ieee->ht_info->current_ht_support ||
	    !ieee->ht_info->bCurrentAMPDUEnable) {
		netdev_warn(ieee->dev,
			    "reject to ADDBA_RSP as some capability is not ready(%d, %d, %d)\n",
			    ieee->current_network.qos_data.active,
			    ieee->ht_info->current_ht_support,
			    ieee->ht_info->bCurrentAMPDUEnable);
		reason_code = DELBA_REASON_UNKNOWN_BA;
		goto OnADDBARsp_Reject;
	}

	if (!rtllib_get_ts(ieee, (struct ts_common_info **)&ts, dst,
		   (u8)(pBaParamSet->field.tid), TX_DIR, false)) {
		netdev_warn(ieee->dev, "%s(): can't get TS\n", __func__);
		reason_code = DELBA_REASON_UNKNOWN_BA;
		goto OnADDBARsp_Reject;
	}

	ts->bAddBaReqInProgress = false;
	pending_ba = &ts->TxPendingBARecord;
	pAdmittedBA = &ts->TxAdmittedBARecord;

	if (pAdmittedBA->b_valid) {
		netdev_dbg(ieee->dev, "%s(): ADDBA response already admitted\n",
			   __func__);
		return -1;
	} else if (!pending_ba->b_valid ||
		   (*pDialogToken != pending_ba->dialog_token)) {
		netdev_warn(ieee->dev,
			    "%s(): ADDBA Rsp. BA invalid, DELBA!\n",
			    __func__);
		reason_code = DELBA_REASON_UNKNOWN_BA;
		goto OnADDBARsp_Reject;
	} else {
		netdev_dbg(ieee->dev,
			   "%s(): Recv ADDBA Rsp. BA is admitted! Status code:%X\n",
			   __func__, *status_code);
		deactivate_ba_entry(ieee, pending_ba);
	}

	if (*status_code == ADDBA_STATUS_SUCCESS) {
		if (pBaParamSet->field.ba_policy == BA_POLICY_DELAYED) {
			ts->bAddBaReqDelayed = true;
			deactivate_ba_entry(ieee, pAdmittedBA);
			reason_code = DELBA_REASON_END_BA;
			goto OnADDBARsp_Reject;
		}

		pAdmittedBA->dialog_token = *pDialogToken;
		pAdmittedBA->ba_timeout_value = *pBaTimeoutVal;
		pAdmittedBA->ba_start_seq_ctrl = pending_ba->ba_start_seq_ctrl;
		pAdmittedBA->ba_param_set = *pBaParamSet;
		deactivate_ba_entry(ieee, pAdmittedBA);
		activate_ba_entry(pAdmittedBA, *pBaTimeoutVal);
	} else {
		ts->bAddBaReqDelayed = true;
		ts->bDisable_AddBa = true;
		reason_code = DELBA_REASON_END_BA;
		goto OnADDBARsp_Reject;
	}

	return 0;

OnADDBARsp_Reject:
	{
		struct ba_record BA;

		BA.ba_param_set = *pBaParamSet;
		rtllib_send_DELBA(ieee, dst, &BA, TX_DIR, reason_code);
		return 0;
	}
}

int rtllib_rx_DELBA(struct rtllib_device *ieee, struct sk_buff *skb)
{
	struct ieee80211_hdr_3addr *delba = NULL;
	union delba_param_set *pDelBaParamSet = NULL;
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
	pDelBaParamSet = (union delba_param_set *)&delba->seq_ctrl + 2;

	if (pDelBaParamSet->field.initiator == 1) {
		struct rx_ts_record *ts;

		if (!rtllib_get_ts(ieee, (struct ts_common_info **)&ts, dst,
			   (u8)pDelBaParamSet->field.tid, RX_DIR, false)) {
			netdev_warn(ieee->dev,
				    "%s(): can't get TS for RXTS. dst:%pM TID:%d\n",
				    __func__, dst,
				    (u8)pDelBaParamSet->field.tid);
			return -1;
		}

		rx_ts_delete_ba(ieee, ts);
	} else {
		struct tx_ts_record *pTxTs;

		if (!rtllib_get_ts(ieee, (struct ts_common_info **)&pTxTs, dst,
			   (u8)pDelBaParamSet->field.tid, TX_DIR, false)) {
			netdev_warn(ieee->dev, "%s(): can't get TS for TXTS\n",
				    __func__);
			return -1;
		}

		pTxTs->bUsingBa = false;
		pTxTs->bAddBaReqInProgress = false;
		pTxTs->bAddBaReqDelayed = false;
		del_timer_sync(&pTxTs->TsAddBaTimer);
		tx_ts_delete_ba(ieee, pTxTs);
	}
	return 0;
}

void rtllib_ts_init_add_ba(struct rtllib_device *ieee, struct tx_ts_record *ts,
			   u8 policy, u8	bOverwritePending)
{
	struct ba_record *ba = &ts->TxPendingBARecord;

	if (ba->b_valid && !bOverwritePending)
		return;

	deactivate_ba_entry(ieee, ba);

	ba->dialog_token++;
	ba->ba_param_set.field.amsdu_support = 0;
	ba->ba_param_set.field.ba_policy = policy;
	ba->ba_param_set.field.tid = ts->TsCommonInfo.TSpec.ucTSID;
	ba->ba_param_set.field.buffer_size = 32;
	ba->ba_timeout_value = 0;
	ba->ba_start_seq_ctrl.field.seq_num = (ts->TxCurSeq + 3) % 4096;

	activate_ba_entry(ba, BA_SETUP_TIMEOUT);

	rtllib_send_ADDBAReq(ieee, ts->TsCommonInfo.addr, ba);
}

void rtllib_ts_init_del_ba(struct rtllib_device *ieee,
			   struct ts_common_info *pTsCommonInfo,
			   enum tr_select TxRxSelect)
{
	if (TxRxSelect == TX_DIR) {
		struct tx_ts_record *pTxTs =
			 (struct tx_ts_record *)pTsCommonInfo;

		if (tx_ts_delete_ba(ieee, pTxTs))
			rtllib_send_DELBA(ieee, pTsCommonInfo->addr,
					  (pTxTs->TxAdmittedBARecord.b_valid) ?
					 (&pTxTs->TxAdmittedBARecord) :
					(&pTxTs->TxPendingBARecord),
					 TxRxSelect, DELBA_REASON_END_BA);
	} else if (TxRxSelect == RX_DIR) {
		struct rx_ts_record *ts =
				 (struct rx_ts_record *)pTsCommonInfo;
		if (rx_ts_delete_ba(ieee, ts))
			rtllib_send_DELBA(ieee, pTsCommonInfo->addr,
					  &ts->rx_admitted_ba_record,
					  TxRxSelect, DELBA_REASON_END_BA);
	}
}

void rtllib_ba_setup_timeout(struct timer_list *t)
{
	struct tx_ts_record *pTxTs = from_timer(pTxTs, t,
					      TxPendingBARecord.timer);

	pTxTs->bAddBaReqInProgress = false;
	pTxTs->bAddBaReqDelayed = true;
	pTxTs->TxPendingBARecord.b_valid = false;
}

void rtllib_tx_ba_inact_timeout(struct timer_list *t)
{
	struct tx_ts_record *pTxTs = from_timer(pTxTs, t,
					      TxAdmittedBARecord.timer);
	struct rtllib_device *ieee = container_of(pTxTs, struct rtllib_device,
				     TxTsRecord[pTxTs->num]);
	tx_ts_delete_ba(ieee, pTxTs);
	rtllib_send_DELBA(ieee, pTxTs->TsCommonInfo.addr,
			  &pTxTs->TxAdmittedBARecord, TX_DIR,
			  DELBA_REASON_TIMEOUT);
}

void rtllib_rx_ba_inact_timeout(struct timer_list *t)
{
	struct rx_ts_record *ts = from_timer(ts, t,
					      rx_admitted_ba_record.timer);
	struct rtllib_device *ieee = container_of(ts, struct rtllib_device,
				     RxTsRecord[ts->num]);

	rx_ts_delete_ba(ieee, ts);
	rtllib_send_DELBA(ieee, ts->ts_common_info.addr,
			  &ts->rx_admitted_ba_record, RX_DIR,
			  DELBA_REASON_TIMEOUT);
}
