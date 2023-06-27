// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Contact Information: wlanfae <wlanfae@realtek.com>
 */
#include "rtllib.h"
#include <linux/etherdevice.h>
#include "rtl819x_TS.h"

static void TsSetupTimeOut(struct timer_list *unused)
{
}

static void TsInactTimeout(struct timer_list *unused)
{
}

static void RxPktPendingTimeout(struct timer_list *t)
{
	struct rx_ts_record *pRxTs = from_timer(pRxTs, t,
						     rx_pkt_pending_timer);
	struct rtllib_device *ieee = container_of(pRxTs, struct rtllib_device,
						  RxTsRecord[pRxTs->num]);

	struct rx_reorder_entry *pReorderEntry = NULL;

	unsigned long flags = 0;
	u8 index = 0;
	bool bPktInBuf = false;

	spin_lock_irqsave(&(ieee->reorder_spinlock), flags);
	if (pRxTs->rx_timeout_indicate_seq != 0xffff) {
		while (!list_empty(&pRxTs->rx_pending_pkt_list)) {
			pReorderEntry = (struct rx_reorder_entry *)
					list_entry(pRxTs->rx_pending_pkt_list.prev,
					struct rx_reorder_entry, List);
			if (index == 0)
				pRxTs->rx_indicate_seq = pReorderEntry->SeqNum;

			if (SN_LESS(pReorderEntry->SeqNum,
				    pRxTs->rx_indicate_seq) ||
			    SN_EQUAL(pReorderEntry->SeqNum,
				     pRxTs->rx_indicate_seq)) {
				list_del_init(&pReorderEntry->List);

				if (SN_EQUAL(pReorderEntry->SeqNum,
				    pRxTs->rx_indicate_seq))
					pRxTs->rx_indicate_seq =
					      (pRxTs->rx_indicate_seq + 1) % 4096;

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
		pRxTs->rx_timeout_indicate_seq = 0xffff;

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

	if (bPktInBuf && (pRxTs->rx_timeout_indicate_seq == 0xffff)) {
		pRxTs->rx_timeout_indicate_seq = pRxTs->rx_indicate_seq;
		mod_timer(&pRxTs->rx_pkt_pending_timer,  jiffies +
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

	TsInitAddBA(ieee, pTxTs, BA_POLICY_IMMEDIATE, false);
	netdev_dbg(ieee->dev, "%s(): ADDBA Req is started\n", __func__);
}

static void ResetTsCommonInfo(struct ts_common_info *pTsCommonInfo)
{
	eth_zero_addr(pTsCommonInfo->Addr);
	memset(&pTsCommonInfo->TSpec, 0, sizeof(union tspec_body));
	memset(&pTsCommonInfo->TClass, 0, sizeof(union qos_tclas) * TCLAS_NUM);
	pTsCommonInfo->TClasProc = 0;
	pTsCommonInfo->TClasNum = 0;
}

static void ResetTxTsEntry(struct tx_ts_record *pTS)
{
	ResetTsCommonInfo(&pTS->TsCommonInfo);
	pTS->TxCurSeq = 0;
	pTS->bAddBaReqInProgress = false;
	pTS->bAddBaReqDelayed = false;
	pTS->bUsingBa = false;
	pTS->bDisable_AddBa = false;
	ResetBaEntry(&pTS->TxAdmittedBARecord);
	ResetBaEntry(&pTS->TxPendingBARecord);
}

static void ResetRxTsEntry(struct rx_ts_record *pTS)
{
	ResetTsCommonInfo(&pTS->ts_common_info);
	pTS->rx_indicate_seq = 0xffff;
	pTS->rx_timeout_indicate_seq = 0xffff;
	ResetBaEntry(&pTS->rx_admitted_ba_record);
}

void TSInitialize(struct rtllib_device *ieee)
{
	struct tx_ts_record *pTxTS  = ieee->TxTsRecord;
	struct rx_ts_record *pRxTS  = ieee->RxTsRecord;
	struct rx_reorder_entry *pRxReorderEntry = ieee->RxReorderEntry;
	u8				count = 0;

	INIT_LIST_HEAD(&ieee->Tx_TS_Admit_List);
	INIT_LIST_HEAD(&ieee->Tx_TS_Pending_List);
	INIT_LIST_HEAD(&ieee->Tx_TS_Unused_List);

	for (count = 0; count < TOTAL_TS_NUM; count++) {
		pTxTS->num = count;
		timer_setup(&pTxTS->TsCommonInfo.SetupTimer, TsSetupTimeOut,
			    0);

		timer_setup(&pTxTS->TsCommonInfo.InactTimer, TsInactTimeout,
			    0);

		timer_setup(&pTxTS->TsAddBaTimer, TsAddBaProcess, 0);

		timer_setup(&pTxTS->TxPendingBARecord.timer, BaSetupTimeOut,
			    0);
		timer_setup(&pTxTS->TxAdmittedBARecord.timer,
			    TxBaInactTimeout, 0);

		ResetTxTsEntry(pTxTS);
		list_add_tail(&pTxTS->TsCommonInfo.List,
				&ieee->Tx_TS_Unused_List);
		pTxTS++;
	}

	INIT_LIST_HEAD(&ieee->Rx_TS_Admit_List);
	INIT_LIST_HEAD(&ieee->Rx_TS_Pending_List);
	INIT_LIST_HEAD(&ieee->Rx_TS_Unused_List);
	for (count = 0; count < TOTAL_TS_NUM; count++) {
		pRxTS->num = count;
		INIT_LIST_HEAD(&pRxTS->rx_pending_pkt_list);

		timer_setup(&pRxTS->ts_common_info.SetupTimer, TsSetupTimeOut,
			    0);

		timer_setup(&pRxTS->ts_common_info.InactTimer, TsInactTimeout,
			    0);

		timer_setup(&pRxTS->rx_admitted_ba_record.timer,
			    RxBaInactTimeout, 0);

		timer_setup(&pRxTS->rx_pkt_pending_timer, RxPktPendingTimeout, 0);

		ResetRxTsEntry(pRxTS);
		list_add_tail(&pRxTS->ts_common_info.List,
			      &ieee->Rx_TS_Unused_List);
		pRxTS++;
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

static void AdmitTS(struct rtllib_device *ieee,
		    struct ts_common_info *pTsCommonInfo, u32 InactTime)
{
	del_timer_sync(&pTsCommonInfo->SetupTimer);
	del_timer_sync(&pTsCommonInfo->InactTimer);

	if (InactTime != 0)
		mod_timer(&pTsCommonInfo->InactTimer, jiffies +
			  msecs_to_jiffies(InactTime));
}

static struct ts_common_info *SearchAdmitTRStream(struct rtllib_device *ieee,
						  u8 *Addr, u8 TID,
						  enum tr_select TxRxSelect)
{
	u8	dir;
	bool	search_dir[4] = {0};
	struct list_head *psearch_list;
	struct ts_common_info *pRet = NULL;

	if (ieee->iw_mode == IW_MODE_MASTER) {
		if (TxRxSelect == TX_DIR) {
			search_dir[DIR_DOWN] = true;
			search_dir[DIR_BI_DIR] = true;
		} else {
			search_dir[DIR_UP] = true;
			search_dir[DIR_BI_DIR] = true;
		}
	} else if (ieee->iw_mode == IW_MODE_ADHOC) {
		if (TxRxSelect == TX_DIR)
			search_dir[DIR_UP] = true;
		else
			search_dir[DIR_DOWN] = true;
	} else {
		if (TxRxSelect == TX_DIR) {
			search_dir[DIR_UP] = true;
			search_dir[DIR_BI_DIR] = true;
			search_dir[DIR_DIRECT] = true;
		} else {
			search_dir[DIR_DOWN] = true;
			search_dir[DIR_BI_DIR] = true;
			search_dir[DIR_DIRECT] = true;
		}
	}

	if (TxRxSelect == TX_DIR)
		psearch_list = &ieee->Tx_TS_Admit_List;
	else
		psearch_list = &ieee->Rx_TS_Admit_List;

	for (dir = 0; dir <= DIR_BI_DIR; dir++) {
		if (!search_dir[dir])
			continue;
		list_for_each_entry(pRet, psearch_list, List) {
			if (memcmp(pRet->Addr, Addr, 6) == 0 &&
			    pRet->TSpec.f.TSInfo.field.ucTSID == TID &&
			    pRet->TSpec.f.TSInfo.field.ucDirection == dir)
				break;
		}
		if (&pRet->List  != psearch_list)
			break;
	}

	if (pRet && &pRet->List  != psearch_list)
		return pRet;
	return NULL;
}

static void MakeTSEntry(struct ts_common_info *pTsCommonInfo, u8 *Addr,
			union tspec_body *pTSPEC, union qos_tclas *pTCLAS,
			u8 TCLAS_Num, u8 TCLAS_Proc)
{
	u8	count;

	if (!pTsCommonInfo)
		return;

	memcpy(pTsCommonInfo->Addr, Addr, 6);

	if (pTSPEC)
		memcpy((u8 *)(&(pTsCommonInfo->TSpec)), (u8 *)pTSPEC,
			sizeof(union tspec_body));

	for (count = 0; count < TCLAS_Num; count++)
		memcpy((u8 *)(&(pTsCommonInfo->TClass[count])),
		       (u8 *)pTCLAS, sizeof(union qos_tclas));

	pTsCommonInfo->TClasProc = TCLAS_Proc;
	pTsCommonInfo->TClasNum = TCLAS_Num;
}

bool GetTs(struct rtllib_device *ieee, struct ts_common_info **ppTS,
	   u8 *Addr, u8 TID, enum tr_select TxRxSelect, bool bAddNewTs)
{
	u8	UP = 0;
	union tspec_body TSpec;
	union qos_tsinfo *pTSInfo = &TSpec.f.TSInfo;
	struct list_head *pUnusedList;
	struct list_head *pAddmitList;
	enum direction_value Dir;

	if (is_multicast_ether_addr(Addr)) {
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

	*ppTS = SearchAdmitTRStream(ieee, Addr, UP, TxRxSelect);
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

	Dir = (ieee->iw_mode == IW_MODE_MASTER) ?
				((TxRxSelect == TX_DIR) ? DIR_DOWN : DIR_UP) :
				((TxRxSelect == TX_DIR) ? DIR_UP : DIR_DOWN);

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
			struct rx_ts_record *tmp =
				 container_of(*ppTS,
				 struct rx_ts_record,
				 ts_common_info);
			ResetRxTsEntry(tmp);
		}

		netdev_dbg(ieee->dev,
			   "to init current TS, UP:%d, Dir:%d, addr: %pM ppTs=%p\n",
			   UP, Dir, Addr, *ppTS);
		pTSInfo->field.ucTrafficType = 0;
		pTSInfo->field.ucTSID = UP;
		pTSInfo->field.ucDirection = Dir;
		pTSInfo->field.ucAccessPolicy = 1;
		pTSInfo->field.ucAggregation = 0;
		pTSInfo->field.ucPSB = 0;
		pTSInfo->field.ucUP = UP;
		pTSInfo->field.ucTSInfoAckPolicy = 0;
		pTSInfo->field.ucSchedule = 0;

		MakeTSEntry(*ppTS, Addr, &TSpec, NULL, 0, 0);
		AdmitTS(ieee, *ppTS, 0);
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
	del_timer_sync(&pTs->SetupTimer);
	del_timer_sync(&pTs->InactTimer);
	TsInitDelBA(ieee, pTs, TxRxSelect);

	if (TxRxSelect == RX_DIR) {
		struct rx_reorder_entry *pRxReorderEntry;
		struct rx_ts_record *pRxTS = (struct rx_ts_record *)pTs;

		if (timer_pending(&pRxTS->rx_pkt_pending_timer))
			del_timer_sync(&pRxTS->rx_pkt_pending_timer);

		while (!list_empty(&pRxTS->rx_pending_pkt_list)) {
			pRxReorderEntry = (struct rx_reorder_entry *)
					list_entry(pRxTS->rx_pending_pkt_list.prev,
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

void RemovePeerTS(struct rtllib_device *ieee, u8 *Addr)
{
	struct ts_common_info *pTS, *pTmpTS;

	netdev_info(ieee->dev, "===========>%s, %pM\n", __func__, Addr);

	list_for_each_entry_safe(pTS, pTmpTS, &ieee->Tx_TS_Pending_List, List) {
		if (memcmp(pTS->Addr, Addr, 6) == 0) {
			RemoveTsEntry(ieee, pTS, TX_DIR);
			list_del_init(&pTS->List);
			list_add_tail(&pTS->List, &ieee->Tx_TS_Unused_List);
		}
	}

	list_for_each_entry_safe(pTS, pTmpTS, &ieee->Tx_TS_Admit_List, List) {
		if (memcmp(pTS->Addr, Addr, 6) == 0) {
			netdev_info(ieee->dev,
				    "====>remove Tx_TS_admin_list\n");
			RemoveTsEntry(ieee, pTS, TX_DIR);
			list_del_init(&pTS->List);
			list_add_tail(&pTS->List, &ieee->Tx_TS_Unused_List);
		}
	}

	list_for_each_entry_safe(pTS, pTmpTS, &ieee->Rx_TS_Pending_List, List) {
		if (memcmp(pTS->Addr, Addr, 6) == 0) {
			RemoveTsEntry(ieee, pTS, RX_DIR);
			list_del_init(&pTS->List);
			list_add_tail(&pTS->List, &ieee->Rx_TS_Unused_List);
		}
	}

	list_for_each_entry_safe(pTS, pTmpTS, &ieee->Rx_TS_Admit_List, List) {
		if (memcmp(pTS->Addr, Addr, 6) == 0) {
			RemoveTsEntry(ieee, pTS, RX_DIR);
			list_del_init(&pTS->List);
			list_add_tail(&pTS->List, &ieee->Rx_TS_Unused_List);
		}
	}
}
EXPORT_SYMBOL(RemovePeerTS);

void RemoveAllTS(struct rtllib_device *ieee)
{
	struct ts_common_info *pTS, *pTmpTS;

	list_for_each_entry_safe(pTS, pTmpTS, &ieee->Tx_TS_Pending_List, List) {
		RemoveTsEntry(ieee, pTS, TX_DIR);
		list_del_init(&pTS->List);
		list_add_tail(&pTS->List, &ieee->Tx_TS_Unused_List);
	}

	list_for_each_entry_safe(pTS, pTmpTS, &ieee->Tx_TS_Admit_List, List) {
		RemoveTsEntry(ieee, pTS, TX_DIR);
		list_del_init(&pTS->List);
		list_add_tail(&pTS->List, &ieee->Tx_TS_Unused_List);
	}

	list_for_each_entry_safe(pTS, pTmpTS, &ieee->Rx_TS_Pending_List, List) {
		RemoveTsEntry(ieee, pTS, RX_DIR);
		list_del_init(&pTS->List);
		list_add_tail(&pTS->List, &ieee->Rx_TS_Unused_List);
	}

	list_for_each_entry_safe(pTS, pTmpTS, &ieee->Rx_TS_Admit_List, List) {
		RemoveTsEntry(ieee, pTS, RX_DIR);
		list_del_init(&pTS->List);
		list_add_tail(&pTS->List, &ieee->Rx_TS_Unused_List);
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
