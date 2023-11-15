// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Contact Information: wlanfae <wlanfae@realtek.com>
 */
#include "rtllib.h"
#include <linux/etherdevice.h>
#include "rtl819x_TS.h"

static void RxPktPendingTimeout(struct timer_list *t)
{
	struct rx_ts_record *ts = from_timer(ts, t, rx_pkt_pending_timer);
	struct rtllib_device *ieee = container_of(ts, struct rtllib_device,
						  RxTsRecord[ts->num]);

	struct rx_reorder_entry *pReorderEntry = NULL;

	unsigned long flags = 0;
	u8 index = 0;
	bool bPktInBuf = false;

	spin_lock_irqsave(&(ieee->reorder_spinlock), flags);
	if (ts->rx_timeout_indicate_seq != 0xffff) {
		while (!list_empty(&ts->rx_pending_pkt_list)) {
			pReorderEntry = (struct rx_reorder_entry *)
					list_entry(ts->rx_pending_pkt_list.prev,
					struct rx_reorder_entry, List);
			if (index == 0)
				ts->rx_indicate_seq = pReorderEntry->SeqNum;

			if (SN_LESS(pReorderEntry->SeqNum,
				    ts->rx_indicate_seq) ||
			    SN_EQUAL(pReorderEntry->SeqNum,
				     ts->rx_indicate_seq)) {
				list_del_init(&pReorderEntry->List);

				if (SN_EQUAL(pReorderEntry->SeqNum,
				    ts->rx_indicate_seq))
					ts->rx_indicate_seq =
					      (ts->rx_indicate_seq + 1) % 4096;

				netdev_dbg(ieee->dev,
					   "%s(): Indicate SeqNum: %d\n",
					   __func__, pReorderEntry->SeqNum);
				ieee->stats_IndicateArray[index] =
							 pReorderEntry->prxb;
				index++;

				list_add_tail(&pReorderEntry->List,
					      &ieee->RxReorder_Unused_List);
			} else {
				bPktInBuf = true;
				break;
			}
		}
	}

	if (index > 0) {
		ts->rx_timeout_indicate_seq = 0xffff;

		if (index > REORDER_WIN_SIZE) {
			netdev_warn(ieee->dev,
				    "%s(): Rx Reorder struct buffer full\n",
				    __func__);
			spin_unlock_irqrestore(&(ieee->reorder_spinlock),
					       flags);
			return;
		}
		rtllib_indicate_packets(ieee, ieee->stats_IndicateArray, index);
		bPktInBuf = false;
	}

	if (bPktInBuf && (ts->rx_timeout_indicate_seq == 0xffff)) {
		ts->rx_timeout_indicate_seq = ts->rx_indicate_seq;
		mod_timer(&ts->rx_pkt_pending_timer,  jiffies +
			  msecs_to_jiffies(ieee->ht_info->rx_reorder_pending_time)
			  );
	}
	spin_unlock_irqrestore(&(ieee->reorder_spinlock), flags);
}

static void TsAddBaProcess(struct timer_list *t)
{
	struct tx_ts_record *pTxTs = from_timer(pTxTs, t, TsAddBaTimer);
	u8 num = pTxTs->num;
	struct rtllib_device *ieee = container_of(pTxTs, struct rtllib_device,
				     TxTsRecord[num]);

	rtllib_ts_init_add_ba(ieee, pTxTs, BA_POLICY_IMMEDIATE, false);
	netdev_dbg(ieee->dev, "%s(): ADDBA Req is started\n", __func__);
}

static void ResetTsCommonInfo(struct ts_common_info *pTsCommonInfo)
{
	eth_zero_addr(pTsCommonInfo->addr);
	memset(&pTsCommonInfo->TSpec, 0, sizeof(struct qos_tsinfo));
}

static void ResetTxTsEntry(struct tx_ts_record *ts)
{
	ResetTsCommonInfo(&ts->TsCommonInfo);
	ts->TxCurSeq = 0;
	ts->bAddBaReqInProgress = false;
	ts->bAddBaReqDelayed = false;
	ts->bUsingBa = false;
	ts->bDisable_AddBa = false;
	rtllib_reset_ba_entry(&ts->TxAdmittedBARecord);
	rtllib_reset_ba_entry(&ts->TxPendingBARecord);
}

static void ResetRxTsEntry(struct rx_ts_record *ts)
{
	ResetTsCommonInfo(&ts->ts_common_info);
	ts->rx_indicate_seq = 0xffff;
	ts->rx_timeout_indicate_seq = 0xffff;
	rtllib_reset_ba_entry(&ts->rx_admitted_ba_record);
}

void rtllib_ts_init(struct rtllib_device *ieee)
{
	struct tx_ts_record *pTxTS  = ieee->TxTsRecord;
	struct rx_ts_record *rxts  = ieee->RxTsRecord;
	struct rx_reorder_entry *pRxReorderEntry = ieee->RxReorderEntry;
	u8				count = 0;

	INIT_LIST_HEAD(&ieee->Tx_TS_Admit_List);
	INIT_LIST_HEAD(&ieee->Tx_TS_Pending_List);
	INIT_LIST_HEAD(&ieee->Tx_TS_Unused_List);

	for (count = 0; count < TOTAL_TS_NUM; count++) {
		pTxTS->num = count;
		timer_setup(&pTxTS->TsAddBaTimer, TsAddBaProcess, 0);

		timer_setup(&pTxTS->TxPendingBARecord.timer, rtllib_ba_setup_timeout,
			    0);
		timer_setup(&pTxTS->TxAdmittedBARecord.timer,
			    rtllib_tx_ba_inact_timeout, 0);

		ResetTxTsEntry(pTxTS);
		list_add_tail(&pTxTS->TsCommonInfo.List,
				&ieee->Tx_TS_Unused_List);
		pTxTS++;
	}

	INIT_LIST_HEAD(&ieee->Rx_TS_Admit_List);
	INIT_LIST_HEAD(&ieee->Rx_TS_Pending_List);
	INIT_LIST_HEAD(&ieee->Rx_TS_Unused_List);
	for (count = 0; count < TOTAL_TS_NUM; count++) {
		rxts->num = count;
		INIT_LIST_HEAD(&rxts->rx_pending_pkt_list);
		timer_setup(&rxts->rx_admitted_ba_record.timer,
			    rtllib_rx_ba_inact_timeout, 0);

		timer_setup(&rxts->rx_pkt_pending_timer, RxPktPendingTimeout, 0);

		ResetRxTsEntry(rxts);
		list_add_tail(&rxts->ts_common_info.List,
			      &ieee->Rx_TS_Unused_List);
		rxts++;
	}
	INIT_LIST_HEAD(&ieee->RxReorder_Unused_List);
	for (count = 0; count < REORDER_ENTRY_NUM; count++) {
		list_add_tail(&pRxReorderEntry->List,
			      &ieee->RxReorder_Unused_List);
		if (count == (REORDER_ENTRY_NUM - 1))
			break;
		pRxReorderEntry = &ieee->RxReorderEntry[count + 1];
	}
}

static struct ts_common_info *SearchAdmitTRStream(struct rtllib_device *ieee,
						  u8 *addr, u8 TID,
						  enum tr_select TxRxSelect)
{
	u8	dir;
	bool	search_dir[4] = {0};
	struct list_head *psearch_list;
	struct ts_common_info *pRet = NULL;

	if (TxRxSelect == TX_DIR) {
		search_dir[DIR_UP] = true;
		search_dir[DIR_BI_DIR] = true;
		search_dir[DIR_DIRECT] = true;
	} else {
		search_dir[DIR_DOWN] = true;
		search_dir[DIR_BI_DIR] = true;
		search_dir[DIR_DIRECT] = true;
	}

	if (TxRxSelect == TX_DIR)
		psearch_list = &ieee->Tx_TS_Admit_List;
	else
		psearch_list = &ieee->Rx_TS_Admit_List;

	for (dir = 0; dir <= DIR_BI_DIR; dir++) {
		if (!search_dir[dir])
			continue;
		list_for_each_entry(pRet, psearch_list, List) {
			if (memcmp(pRet->addr, addr, 6) == 0 &&
			    pRet->TSpec.ucTSID == TID &&
			    pRet->TSpec.ucDirection == dir)
				break;
		}
		if (&pRet->List  != psearch_list)
			break;
	}

	if (pRet && &pRet->List  != psearch_list)
		return pRet;
	return NULL;
}

static void MakeTSEntry(struct ts_common_info *pTsCommonInfo, u8 *addr,
			struct qos_tsinfo *pTSPEC)
{
	if (!pTsCommonInfo)
		return;

	memcpy(pTsCommonInfo->addr, addr, 6);

	if (pTSPEC)
		memcpy((u8 *)(&(pTsCommonInfo->TSpec)), (u8 *)pTSPEC,
			sizeof(struct qos_tsinfo));
}

bool rtllib_get_ts(struct rtllib_device *ieee, struct ts_common_info **ppTS,
	   u8 *addr, u8 TID, enum tr_select TxRxSelect, bool bAddNewTs)
{
	u8	UP = 0;
	struct qos_tsinfo TSpec;
	struct qos_tsinfo *ts_info = &TSpec;
	struct list_head *pUnusedList;
	struct list_head *pAddmitList;
	enum direction_value Dir;

	if (is_multicast_ether_addr(addr)) {
		netdev_warn(ieee->dev, "Get TS for Broadcast or Multicast\n");
		return false;
	}
	if (ieee->current_network.qos_data.supported == 0) {
		UP = 0;
	} else {
		switch (TID) {
		case 0:
		case 3:
			UP = 0;
			break;
		case 1:
		case 2:
			UP = 2;
			break;
		case 4:
		case 5:
			UP = 5;
			break;
		case 6:
		case 7:
			UP = 7;
			break;
		default:
			netdev_warn(ieee->dev, "%s(): TID(%d) is not valid\n",
				    __func__, TID);
			return false;
		}
	}

	*ppTS = SearchAdmitTRStream(ieee, addr, UP, TxRxSelect);
	if (*ppTS)
		return true;

	if (!bAddNewTs) {
		netdev_dbg(ieee->dev, "add new TS failed(tid:%d)\n", UP);
		return false;
	}

	pUnusedList = (TxRxSelect == TX_DIR) ?
				(&ieee->Tx_TS_Unused_List) :
				(&ieee->Rx_TS_Unused_List);

	pAddmitList = (TxRxSelect == TX_DIR) ?
				(&ieee->Tx_TS_Admit_List) :
				(&ieee->Rx_TS_Admit_List);

	Dir = ((TxRxSelect == TX_DIR) ? DIR_UP : DIR_DOWN);

	if (!list_empty(pUnusedList)) {
		(*ppTS) = list_entry(pUnusedList->next,
			  struct ts_common_info, List);
		list_del_init(&(*ppTS)->List);
		if (TxRxSelect == TX_DIR) {
			struct tx_ts_record *tmp =
				container_of(*ppTS,
				struct tx_ts_record,
				TsCommonInfo);
			ResetTxTsEntry(tmp);
		} else {
			struct rx_ts_record *ts =
				 container_of(*ppTS,
				 struct rx_ts_record,
				 ts_common_info);
			ResetRxTsEntry(ts);
		}

		netdev_dbg(ieee->dev,
			   "to init current TS, UP:%d, Dir:%d, addr: %pM ppTs=%p\n",
			   UP, Dir, addr, *ppTS);
		ts_info->ucTSID = UP;
		ts_info->ucDirection = Dir;

		MakeTSEntry(*ppTS, addr, &TSpec);
		list_add_tail(&((*ppTS)->List), pAddmitList);

		return true;
	}

	netdev_warn(ieee->dev,
		    "There is not enough dir=%d(0=up down=1) TS record to be used!",
		    Dir);
	return false;
}

static void RemoveTsEntry(struct rtllib_device *ieee,
			  struct ts_common_info *pTs, enum tr_select TxRxSelect)
{
	rtllib_ts_init_del_ba(ieee, pTs, TxRxSelect);

	if (TxRxSelect == RX_DIR) {
		struct rx_reorder_entry *pRxReorderEntry;
		struct rx_ts_record *ts = (struct rx_ts_record *)pTs;

		if (timer_pending(&ts->rx_pkt_pending_timer))
			del_timer_sync(&ts->rx_pkt_pending_timer);

		while (!list_empty(&ts->rx_pending_pkt_list)) {
			pRxReorderEntry = (struct rx_reorder_entry *)
					list_entry(ts->rx_pending_pkt_list.prev,
					struct rx_reorder_entry, List);
			netdev_dbg(ieee->dev,  "%s(): Delete SeqNum %d!\n",
				   __func__, pRxReorderEntry->SeqNum);
			list_del_init(&pRxReorderEntry->List);
			{
				int i = 0;
				struct rtllib_rxb *prxb = pRxReorderEntry->prxb;

				if (unlikely(!prxb))
					return;
				for (i = 0; i < prxb->nr_subframes; i++)
					dev_kfree_skb(prxb->subframes[i]);
				kfree(prxb);
				prxb = NULL;
			}
			list_add_tail(&pRxReorderEntry->List,
				      &ieee->RxReorder_Unused_List);
		}
	} else {
		struct tx_ts_record *pTxTS = (struct tx_ts_record *)pTs;

		del_timer_sync(&pTxTS->TsAddBaTimer);
	}
}

void RemovePeerTS(struct rtllib_device *ieee, u8 *addr)
{
	struct ts_common_info *ts, *pTmpTS;

	netdev_info(ieee->dev, "===========>%s, %pM\n", __func__, addr);

	list_for_each_entry_safe(ts, pTmpTS, &ieee->Tx_TS_Pending_List, List) {
		if (memcmp(ts->addr, addr, 6) == 0) {
			RemoveTsEntry(ieee, ts, TX_DIR);
			list_del_init(&ts->List);
			list_add_tail(&ts->List, &ieee->Tx_TS_Unused_List);
		}
	}

	list_for_each_entry_safe(ts, pTmpTS, &ieee->Tx_TS_Admit_List, List) {
		if (memcmp(ts->addr, addr, 6) == 0) {
			netdev_info(ieee->dev,
				    "====>remove Tx_TS_admin_list\n");
			RemoveTsEntry(ieee, ts, TX_DIR);
			list_del_init(&ts->List);
			list_add_tail(&ts->List, &ieee->Tx_TS_Unused_List);
		}
	}

	list_for_each_entry_safe(ts, pTmpTS, &ieee->Rx_TS_Pending_List, List) {
		if (memcmp(ts->addr, addr, 6) == 0) {
			RemoveTsEntry(ieee, ts, RX_DIR);
			list_del_init(&ts->List);
			list_add_tail(&ts->List, &ieee->Rx_TS_Unused_List);
		}
	}

	list_for_each_entry_safe(ts, pTmpTS, &ieee->Rx_TS_Admit_List, List) {
		if (memcmp(ts->addr, addr, 6) == 0) {
			RemoveTsEntry(ieee, ts, RX_DIR);
			list_del_init(&ts->List);
			list_add_tail(&ts->List, &ieee->Rx_TS_Unused_List);
		}
	}
}
EXPORT_SYMBOL(RemovePeerTS);

void RemoveAllTS(struct rtllib_device *ieee)
{
	struct ts_common_info *ts, *pTmpTS;

	list_for_each_entry_safe(ts, pTmpTS, &ieee->Tx_TS_Pending_List, List) {
		RemoveTsEntry(ieee, ts, TX_DIR);
		list_del_init(&ts->List);
		list_add_tail(&ts->List, &ieee->Tx_TS_Unused_List);
	}

	list_for_each_entry_safe(ts, pTmpTS, &ieee->Tx_TS_Admit_List, List) {
		RemoveTsEntry(ieee, ts, TX_DIR);
		list_del_init(&ts->List);
		list_add_tail(&ts->List, &ieee->Tx_TS_Unused_List);
	}

	list_for_each_entry_safe(ts, pTmpTS, &ieee->Rx_TS_Pending_List, List) {
		RemoveTsEntry(ieee, ts, RX_DIR);
		list_del_init(&ts->List);
		list_add_tail(&ts->List, &ieee->Rx_TS_Unused_List);
	}

	list_for_each_entry_safe(ts, pTmpTS, &ieee->Rx_TS_Admit_List, List) {
		RemoveTsEntry(ieee, ts, RX_DIR);
		list_del_init(&ts->List);
		list_add_tail(&ts->List, &ieee->Rx_TS_Unused_List);
	}
}

void TsStartAddBaProcess(struct rtllib_device *ieee, struct tx_ts_record *pTxTS)
{
	if (pTxTS->bAddBaReqInProgress == false) {
		pTxTS->bAddBaReqInProgress = true;

		if (pTxTS->bAddBaReqDelayed) {
			netdev_dbg(ieee->dev, "Start ADDBA after 60 sec!!\n");
			mod_timer(&pTxTS->TsAddBaTimer, jiffies +
				  msecs_to_jiffies(TS_ADDBA_DELAY));
		} else {
			netdev_dbg(ieee->dev, "Immediately Start ADDBA\n");
			mod_timer(&pTxTS->TsAddBaTimer, jiffies + 10);
		}
	} else {
		netdev_dbg(ieee->dev, "BA timer is already added\n");
	}
}
