// SPDX-License-Identifier: GPL-2.0
#include "ieee80211.h"
#include <linux/etherdevice.h>
#include <linux/slab.h>
#include "rtl819x_TS.h"

static void TsSetupTimeOut(struct timer_list *unused)
{
	// Not implement yet
	// This is used for WMMSA and ACM , that would send ADDTSReq frame.
}

static void TsInactTimeout(struct timer_list *unused)
{
	// Not implement yet
	// This is used for WMMSA and ACM.
	// This function would be call when TS is no Tx/Rx for some period of time.
}

/********************************************************************************************************************
 *function:  I still not understand this function, so wait for further implementation
 *   input:  unsigned long	 data		//acturally we send TX_TS_RECORD or RX_TS_RECORD to these timer
 *  return:  NULL
 *  notice:
 ********************************************************************************************************************/
static void RxPktPendingTimeout(struct timer_list *t)
{
	PRX_TS_RECORD	pRxTs = from_timer(pRxTs, t, RxPktPendingTimer);
	struct ieee80211_device *ieee = container_of(pRxTs, struct ieee80211_device, RxTsRecord[pRxTs->num]);

	PRX_REORDER_ENTRY	pReorderEntry = NULL;

	//u32 flags = 0;
	unsigned long flags = 0;
	u8 index = 0;
	bool bPktInBuf = false;

	spin_lock_irqsave(&(ieee->reorder_spinlock), flags);
	IEEE80211_DEBUG(IEEE80211_DL_REORDER,"==================>%s()\n",__func__);
	if(pRxTs->RxTimeoutIndicateSeq != 0xffff) {
		// Indicate the pending packets sequentially according to SeqNum until meet the gap.
		while(!list_empty(&pRxTs->RxPendingPktList)) {
			pReorderEntry = (PRX_REORDER_ENTRY)list_entry(pRxTs->RxPendingPktList.prev,RX_REORDER_ENTRY,List);
			if(index == 0)
				pRxTs->RxIndicateSeq = pReorderEntry->SeqNum;

			if( SN_LESS(pReorderEntry->SeqNum, pRxTs->RxIndicateSeq) ||
				SN_EQUAL(pReorderEntry->SeqNum, pRxTs->RxIndicateSeq)	) {
				list_del_init(&pReorderEntry->List);

				if(SN_EQUAL(pReorderEntry->SeqNum, pRxTs->RxIndicateSeq))
					pRxTs->RxIndicateSeq = (pRxTs->RxIndicateSeq + 1) % 4096;

				IEEE80211_DEBUG(IEEE80211_DL_REORDER,"RxPktPendingTimeout(): IndicateSeq: %d\n", pReorderEntry->SeqNum);
				ieee->stats_IndicateArray[index] = pReorderEntry->prxb;
				index++;

				list_add_tail(&pReorderEntry->List, &ieee->RxReorder_Unused_List);
			} else {
				bPktInBuf = true;
				break;
			}
		}
	}

	if(index>0) {
		// Set RxTimeoutIndicateSeq to 0xffff to indicate no pending packets in buffer now.
		pRxTs->RxTimeoutIndicateSeq = 0xffff;

		// Indicate packets
		if(index > REORDER_WIN_SIZE) {
			IEEE80211_DEBUG(IEEE80211_DL_ERR, "RxReorderIndicatePacket(): Rx Reorder buffer full!! \n");
			spin_unlock_irqrestore(&(ieee->reorder_spinlock), flags);
			return;
		}
		ieee80211_indicate_packets(ieee, ieee->stats_IndicateArray, index);
	}

	if(bPktInBuf && (pRxTs->RxTimeoutIndicateSeq==0xffff)) {
		pRxTs->RxTimeoutIndicateSeq = pRxTs->RxIndicateSeq;
		mod_timer(&pRxTs->RxPktPendingTimer,
			  jiffies + msecs_to_jiffies(ieee->pHTInfo->RxReorderPendingTime));
	}
	spin_unlock_irqrestore(&(ieee->reorder_spinlock), flags);
}

/********************************************************************************************************************
 *function:  Add BA timer function
 *   input:  unsigned long	 data		//acturally we send TX_TS_RECORD or RX_TS_RECORD to these timer
 *  return:  NULL
 *  notice:
 ********************************************************************************************************************/
static void TsAddBaProcess(struct timer_list *t)
{
	PTX_TS_RECORD	pTxTs = from_timer(pTxTs, t, TsAddBaTimer);
	u8 num = pTxTs->num;
	struct ieee80211_device *ieee = container_of(pTxTs, struct ieee80211_device, TxTsRecord[num]);

	TsInitAddBA(ieee, pTxTs, BA_POLICY_IMMEDIATE, false);
	IEEE80211_DEBUG(IEEE80211_DL_BA, "TsAddBaProcess(): ADDBA Req is started!! \n");
}


static void ResetTsCommonInfo(struct ts_common_info *pTsCommonInfo)
{
	eth_zero_addr(pTsCommonInfo->Addr);
	memset(&pTsCommonInfo->TSpec, 0, sizeof(TSPEC_BODY));
	memset(&pTsCommonInfo->TClass, 0, sizeof(QOS_TCLAS)*TCLAS_NUM);
	pTsCommonInfo->TClasProc = 0;
	pTsCommonInfo->TClasNum = 0;
}

static void ResetTxTsEntry(PTX_TS_RECORD pTS)
{
	ResetTsCommonInfo(&pTS->TsCommonInfo);
	pTS->TxCurSeq = 0;
	pTS->bAddBaReqInProgress = false;
	pTS->bAddBaReqDelayed = false;
	pTS->bUsingBa = false;
	ResetBaEntry(&pTS->TxAdmittedBARecord); //For BA Originator
	ResetBaEntry(&pTS->TxPendingBARecord);
}

static void ResetRxTsEntry(PRX_TS_RECORD pTS)
{
	ResetTsCommonInfo(&pTS->TsCommonInfo);
	pTS->RxIndicateSeq = 0xffff; // This indicate the RxIndicateSeq is not used now!!
	pTS->RxTimeoutIndicateSeq = 0xffff; // This indicate the RxTimeoutIndicateSeq is not used now!!
	ResetBaEntry(&pTS->RxAdmittedBARecord);	  // For BA Recipient
}

void TSInitialize(struct ieee80211_device *ieee)
{
	PTX_TS_RECORD		pTxTS  = ieee->TxTsRecord;
	PRX_TS_RECORD		pRxTS  = ieee->RxTsRecord;
	PRX_REORDER_ENTRY	pRxReorderEntry = ieee->RxReorderEntry;
	u8				count = 0;
	IEEE80211_DEBUG(IEEE80211_DL_TS, "==========>%s()\n", __func__);
	// Initialize Tx TS related info.
	INIT_LIST_HEAD(&ieee->Tx_TS_Admit_List);
	INIT_LIST_HEAD(&ieee->Tx_TS_Pending_List);
	INIT_LIST_HEAD(&ieee->Tx_TS_Unused_List);

	for(count = 0; count < TOTAL_TS_NUM; count++) {
		//
		pTxTS->num = count;
		// The timers for the operation of Traffic Stream and Block Ack.
		// DLS related timer will be add here in the future!!
		timer_setup(&pTxTS->TsCommonInfo.SetupTimer, TsSetupTimeOut,
			    0);
		timer_setup(&pTxTS->TsCommonInfo.InactTimer, TsInactTimeout,
			    0);
		timer_setup(&pTxTS->TsAddBaTimer, TsAddBaProcess, 0);
		timer_setup(&pTxTS->TxPendingBARecord.Timer, BaSetupTimeOut,
			    0);
		timer_setup(&pTxTS->TxAdmittedBARecord.Timer,
			    TxBaInactTimeout, 0);
		ResetTxTsEntry(pTxTS);
		list_add_tail(&pTxTS->TsCommonInfo.List, &ieee->Tx_TS_Unused_List);
		pTxTS++;
	}

	// Initialize Rx TS related info.
	INIT_LIST_HEAD(&ieee->Rx_TS_Admit_List);
	INIT_LIST_HEAD(&ieee->Rx_TS_Pending_List);
	INIT_LIST_HEAD(&ieee->Rx_TS_Unused_List);
	for(count = 0; count < TOTAL_TS_NUM; count++) {
		pRxTS->num = count;
		INIT_LIST_HEAD(&pRxTS->RxPendingPktList);
		timer_setup(&pRxTS->TsCommonInfo.SetupTimer, TsSetupTimeOut,
			    0);
		timer_setup(&pRxTS->TsCommonInfo.InactTimer, TsInactTimeout,
			    0);
		timer_setup(&pRxTS->RxAdmittedBARecord.Timer,
			    RxBaInactTimeout, 0);
		timer_setup(&pRxTS->RxPktPendingTimer, RxPktPendingTimeout, 0);
		ResetRxTsEntry(pRxTS);
		list_add_tail(&pRxTS->TsCommonInfo.List, &ieee->Rx_TS_Unused_List);
		pRxTS++;
	}
	// Initialize unused Rx Reorder List.
	INIT_LIST_HEAD(&ieee->RxReorder_Unused_List);
//#ifdef TO_DO_LIST
	for(count = 0; count < REORDER_ENTRY_NUM; count++) {
		list_add_tail( &pRxReorderEntry->List,&ieee->RxReorder_Unused_List);
		if(count == (REORDER_ENTRY_NUM-1))
			break;
		pRxReorderEntry = &ieee->RxReorderEntry[count+1];
	}
//#endif
}

static void AdmitTS(struct ieee80211_device *ieee,
		    struct ts_common_info *pTsCommonInfo, u32 InactTime)
{
	del_timer_sync(&pTsCommonInfo->SetupTimer);
	del_timer_sync(&pTsCommonInfo->InactTimer);

	if(InactTime!=0)
		mod_timer(&pTsCommonInfo->InactTimer,
			  jiffies + msecs_to_jiffies(InactTime));
}


static struct ts_common_info *SearchAdmitTRStream(struct ieee80211_device *ieee,
						  u8 *Addr, u8 TID,
						  enum tr_select TxRxSelect)
{
	//DIRECTION_VALUE	dir;
	u8	dir;
	bool				search_dir[4] = {0};
	struct list_head		*psearch_list; //FIXME
	struct ts_common_info	*pRet = NULL;
	if(ieee->iw_mode == IW_MODE_MASTER) { //ap mode
		if(TxRxSelect == TX_DIR) {
			search_dir[DIR_DOWN] = true;
			search_dir[DIR_BI_DIR]= true;
		} else {
			search_dir[DIR_UP]	= true;
			search_dir[DIR_BI_DIR]= true;
		}
	} else if(ieee->iw_mode == IW_MODE_ADHOC) {
		if(TxRxSelect == TX_DIR)
			search_dir[DIR_UP]	= true;
		else
			search_dir[DIR_DOWN] = true;
	} else {
		if(TxRxSelect == TX_DIR) {
			search_dir[DIR_UP]	= true;
			search_dir[DIR_BI_DIR]= true;
			search_dir[DIR_DIRECT]= true;
		} else {
			search_dir[DIR_DOWN] = true;
			search_dir[DIR_BI_DIR]= true;
			search_dir[DIR_DIRECT]= true;
		}
	}

	if(TxRxSelect == TX_DIR)
		psearch_list = &ieee->Tx_TS_Admit_List;
	else
		psearch_list = &ieee->Rx_TS_Admit_List;

	//for(dir = DIR_UP; dir <= DIR_BI_DIR; dir++)
	for(dir = 0; dir <= DIR_BI_DIR; dir++) {
		if (!search_dir[dir])
			continue;
		list_for_each_entry(pRet, psearch_list, List){
	//		IEEE80211_DEBUG(IEEE80211_DL_TS, "ADD:%pM, TID:%d, dir:%d\n", pRet->Addr, pRet->TSpec.f.TSInfo.field.ucTSID, pRet->TSpec.f.TSInfo.field.ucDirection);
			if (memcmp(pRet->Addr, Addr, 6) == 0)
				if (pRet->TSpec.f.TSInfo.field.ucTSID == TID)
					if(pRet->TSpec.f.TSInfo.field.ucDirection == dir) {
	//					printk("Bingo! got it\n");
						break;
					}
		}
		if(&pRet->List  != psearch_list)
			break;
	}

	if(&pRet->List  != psearch_list)
		return pRet ;
	else
		return NULL;
}

static void MakeTSEntry(struct ts_common_info *pTsCommonInfo, u8 *Addr,
			PTSPEC_BODY pTSPEC, PQOS_TCLAS pTCLAS, u8 TCLAS_Num,
			u8 TCLAS_Proc)
{
	u8	count;

	if(pTsCommonInfo == NULL)
		return;

	memcpy(pTsCommonInfo->Addr, Addr, 6);

	if(pTSPEC != NULL)
		memcpy((u8 *)(&(pTsCommonInfo->TSpec)), (u8 *)pTSPEC, sizeof(TSPEC_BODY));

	for(count = 0; count < TCLAS_Num; count++)
		memcpy((u8 *)(&(pTsCommonInfo->TClass[count])), (u8 *)pTCLAS, sizeof(QOS_TCLAS));

	pTsCommonInfo->TClasProc = TCLAS_Proc;
	pTsCommonInfo->TClasNum = TCLAS_Num;
}


bool GetTs(
	struct ieee80211_device		*ieee,
	struct ts_common_info		**ppTS,
	u8				*Addr,
	u8				TID,
	enum tr_select			TxRxSelect,  //Rx:1, Tx:0
	bool				bAddNewTs
	)
{
	u8	UP = 0;
	//
	// We do not build any TS for Broadcast or Multicast stream.
	// So reject these kinds of search here.
	//
	if (is_multicast_ether_addr(Addr)) {
		IEEE80211_DEBUG(IEEE80211_DL_ERR, "get TS for Broadcast or Multicast\n");
		return false;
	}

	if (ieee->current_network.qos_data.supported == 0) {
		UP = 0;
	} else {
		// In WMM case: we use 4 TID only
		if (!IsACValid(TID)) {
			IEEE80211_DEBUG(IEEE80211_DL_ERR, " in %s(), TID(%d) is not valid\n", __func__, TID);
			return false;
		}

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
		}
	}

	*ppTS = SearchAdmitTRStream(
			ieee,
			Addr,
			UP,
			TxRxSelect);
	if(*ppTS != NULL) {
		return true;
	} else {
		if (!bAddNewTs) {
			IEEE80211_DEBUG(IEEE80211_DL_TS, "add new TS failed(tid:%d)\n", UP);
			return false;
		} else {
			//
			// Create a new Traffic stream for current Tx/Rx
			// This is for EDCA and WMM to add a new TS.
			// For HCCA or WMMSA, TS cannot be addmit without negotiation.
			//
			TSPEC_BODY	TSpec;
			PQOS_TSINFO		pTSInfo = &TSpec.f.TSInfo;
			struct list_head	*pUnusedList =
								(TxRxSelect == TX_DIR)?
								(&ieee->Tx_TS_Unused_List):
								(&ieee->Rx_TS_Unused_List);

			struct list_head	*pAddmitList =
								(TxRxSelect == TX_DIR)?
								(&ieee->Tx_TS_Admit_List):
								(&ieee->Rx_TS_Admit_List);

			DIRECTION_VALUE		Dir =		(ieee->iw_mode == IW_MODE_MASTER)?
								((TxRxSelect==TX_DIR)?DIR_DOWN:DIR_UP):
								((TxRxSelect==TX_DIR)?DIR_UP:DIR_DOWN);
			IEEE80211_DEBUG(IEEE80211_DL_TS, "to add Ts\n");
			if(!list_empty(pUnusedList)) {
				(*ppTS) = list_entry(pUnusedList->next, struct ts_common_info, List);
				list_del_init(&(*ppTS)->List);
				if(TxRxSelect==TX_DIR) {
					PTX_TS_RECORD tmp = container_of(*ppTS, TX_TS_RECORD, TsCommonInfo);
					ResetTxTsEntry(tmp);
				} else {
					PRX_TS_RECORD tmp = container_of(*ppTS, RX_TS_RECORD, TsCommonInfo);
					ResetRxTsEntry(tmp);
				}

				IEEE80211_DEBUG(IEEE80211_DL_TS, "to init current TS, UP:%d, Dir:%d, addr:%pM\n", UP, Dir, Addr);
				// Prepare TS Info releated field
				pTSInfo->field.ucTrafficType = 0;			// Traffic type: WMM is reserved in this field
				pTSInfo->field.ucTSID = UP;			// TSID
				pTSInfo->field.ucDirection = Dir;			// Direction: if there is DirectLink, this need additional consideration.
				pTSInfo->field.ucAccessPolicy = 1;		// Access policy
				pTSInfo->field.ucAggregation = 0;		// Aggregation
				pTSInfo->field.ucPSB = 0;				// Aggregation
				pTSInfo->field.ucUP = UP;				// User priority
				pTSInfo->field.ucTSInfoAckPolicy = 0;		// Ack policy
				pTSInfo->field.ucSchedule = 0;			// Schedule

				MakeTSEntry(*ppTS, Addr, &TSpec, NULL, 0, 0);
				AdmitTS(ieee, *ppTS, 0);
				list_add_tail(&((*ppTS)->List), pAddmitList);
				// if there is DirectLink, we need to do additional operation here!!

				return true;
			} else {
				IEEE80211_DEBUG(IEEE80211_DL_ERR, "in function %s() There is not enough TS record to be used!!", __func__);
				return false;
			}
		}
	}
}

static void RemoveTsEntry(struct ieee80211_device *ieee, struct ts_common_info *pTs,
			  enum tr_select TxRxSelect)
{
	//u32 flags = 0;
	unsigned long flags = 0;
	del_timer_sync(&pTs->SetupTimer);
	del_timer_sync(&pTs->InactTimer);
	TsInitDelBA(ieee, pTs, TxRxSelect);

	if(TxRxSelect == RX_DIR) {
//#ifdef TO_DO_LIST
		PRX_REORDER_ENTRY	pRxReorderEntry;
		PRX_TS_RECORD		pRxTS = (PRX_TS_RECORD)pTs;
		if(timer_pending(&pRxTS->RxPktPendingTimer))
			del_timer_sync(&pRxTS->RxPktPendingTimer);

		while(!list_empty(&pRxTS->RxPendingPktList)) {
			spin_lock_irqsave(&(ieee->reorder_spinlock), flags);
			//pRxReorderEntry = list_entry(&pRxTS->RxPendingPktList.prev,RX_REORDER_ENTRY,List);
			pRxReorderEntry = (PRX_REORDER_ENTRY)list_entry(pRxTS->RxPendingPktList.prev,RX_REORDER_ENTRY,List);
			list_del_init(&pRxReorderEntry->List);
			{
				int i = 0;
				struct ieee80211_rxb *prxb = pRxReorderEntry->prxb;
				if (unlikely(!prxb)) {
					spin_unlock_irqrestore(&(ieee->reorder_spinlock), flags);
					return;
				}
				for(i =0; i < prxb->nr_subframes; i++)
					dev_kfree_skb(prxb->subframes[i]);

				kfree(prxb);
				prxb = NULL;
			}
			list_add_tail(&pRxReorderEntry->List,&ieee->RxReorder_Unused_List);
			spin_unlock_irqrestore(&(ieee->reorder_spinlock), flags);
		}

//#endif
	} else {
		PTX_TS_RECORD pTxTS = (PTX_TS_RECORD)pTs;
		del_timer_sync(&pTxTS->TsAddBaTimer);
	}
}

void RemovePeerTS(struct ieee80211_device *ieee, u8 *Addr)
{
	struct ts_common_info	*pTS, *pTmpTS;

	printk("===========>RemovePeerTS,%pM\n", Addr);
	list_for_each_entry_safe(pTS, pTmpTS, &ieee->Tx_TS_Pending_List, List) {
		if (memcmp(pTS->Addr, Addr, 6) == 0) {
			RemoveTsEntry(ieee, pTS, TX_DIR);
			list_del_init(&pTS->List);
			list_add_tail(&pTS->List, &ieee->Tx_TS_Unused_List);
		}
	}

	list_for_each_entry_safe(pTS, pTmpTS, &ieee->Tx_TS_Admit_List, List) {
		if (memcmp(pTS->Addr, Addr, 6) == 0) {
			printk("====>remove Tx_TS_admin_list\n");
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

void RemoveAllTS(struct ieee80211_device *ieee)
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

void TsStartAddBaProcess(struct ieee80211_device *ieee, PTX_TS_RECORD	pTxTS)
{
	if(!pTxTS->bAddBaReqInProgress) {
		pTxTS->bAddBaReqInProgress = true;
		if(pTxTS->bAddBaReqDelayed)	{
			IEEE80211_DEBUG(IEEE80211_DL_BA, "TsStartAddBaProcess(): Delayed Start ADDBA after 60 sec!!\n");
			mod_timer(&pTxTS->TsAddBaTimer,
				  jiffies + msecs_to_jiffies(TS_ADDBA_DELAY));
		} else {
			IEEE80211_DEBUG(IEEE80211_DL_BA,"TsStartAddBaProcess(): Immediately Start ADDBA now!!\n");
			mod_timer(&pTxTS->TsAddBaTimer, jiffies+10); //set 10 ticks
		}
	} else {
		IEEE80211_DEBUG(IEEE80211_DL_ERR, "%s()==>BA timer is already added\n", __func__);
	}
}
