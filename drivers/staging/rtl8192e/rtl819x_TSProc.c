/******************************************************************************
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
******************************************************************************/
#include "rtllib.h"
#include <linux/etherdevice.h>
#include "rtl819x_TS.h"
extern void _setup_timer( struct timer_list*, void*, unsigned long);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
#define list_for_each_entry_safe(pos, n, head, member) \
	for (pos = list_entry((head)->next, typeof(*pos), member), \
		n = list_entry(pos->member.next, typeof(*pos), member); \
		&pos->member != (head); \
		pos = n, n = list_entry(n->member.next, typeof(*n), member))
#endif
void TsSetupTimeOut(unsigned long data)
{
}

void TsInactTimeout(unsigned long data)
{
}

void RxPktPendingTimeout(unsigned long data)
{
	PRX_TS_RECORD	pRxTs = (PRX_TS_RECORD)data;
	struct rtllib_device *ieee = container_of(pRxTs, struct rtllib_device, RxTsRecord[pRxTs->num]);

	PRX_REORDER_ENTRY	pReorderEntry = NULL;

	unsigned long flags = 0;
	struct rtllib_rxb *stats_IndicateArray[REORDER_WIN_SIZE];
	u8 index = 0;
	bool bPktInBuf = false;

	spin_lock_irqsave(&(ieee->reorder_spinlock), flags);
	if (pRxTs->RxTimeoutIndicateSeq != 0xffff)
	{
		while(!list_empty(&pRxTs->RxPendingPktList))
		{
			pReorderEntry = (PRX_REORDER_ENTRY)list_entry(pRxTs->RxPendingPktList.prev,RX_REORDER_ENTRY,List);
			if (index == 0)
				pRxTs->RxIndicateSeq = pReorderEntry->SeqNum;

			if ( SN_LESS(pReorderEntry->SeqNum, pRxTs->RxIndicateSeq) ||
				SN_EQUAL(pReorderEntry->SeqNum, pRxTs->RxIndicateSeq)	)
			{
				list_del_init(&pReorderEntry->List);

				if (SN_EQUAL(pReorderEntry->SeqNum, pRxTs->RxIndicateSeq))
					pRxTs->RxIndicateSeq = (pRxTs->RxIndicateSeq + 1) % 4096;

				RTLLIB_DEBUG(RTLLIB_DL_REORDER,"%s(): Indicate SeqNum: %d\n",__func__, pReorderEntry->SeqNum);
				stats_IndicateArray[index] = pReorderEntry->prxb;
				index++;

				list_add_tail(&pReorderEntry->List, &ieee->RxReorder_Unused_List);
			}
			else
			{
				bPktInBuf = true;
				break;
			}
		}
	}

	if (index>0){
		pRxTs->RxTimeoutIndicateSeq = 0xffff;

		if (index > REORDER_WIN_SIZE){
			RTLLIB_DEBUG(RTLLIB_DL_ERR, "RxReorderIndicatePacket(): Rx Reorer buffer full!! \n");
			spin_unlock_irqrestore(&(ieee->reorder_spinlock), flags);
			return;
		}
		rtllib_indicate_packets(ieee, stats_IndicateArray, index);
		bPktInBuf = false;
	}

	if (bPktInBuf && (pRxTs->RxTimeoutIndicateSeq==0xffff)){
		pRxTs->RxTimeoutIndicateSeq = pRxTs->RxIndicateSeq;
		mod_timer(&pRxTs->RxPktPendingTimer,  jiffies + MSECS(ieee->pHTInfo->RxReorderPendingTime));
	}
	spin_unlock_irqrestore(&(ieee->reorder_spinlock), flags);
}

void TsAddBaProcess(unsigned long data)
{
	PTX_TS_RECORD	pTxTs = (PTX_TS_RECORD)data;
	u8 num = pTxTs->num;
	struct rtllib_device *ieee = container_of(pTxTs, struct rtllib_device, TxTsRecord[num]);

	TsInitAddBA(ieee, pTxTs, BA_POLICY_IMMEDIATE, false);
	RTLLIB_DEBUG(RTLLIB_DL_BA, "TsAddBaProcess(): ADDBA Req is started!! \n");
}


void ResetTsCommonInfo(PTS_COMMON_INFO	pTsCommonInfo)
{
	memset(pTsCommonInfo->Addr, 0, 6);
	memset(&pTsCommonInfo->TSpec, 0, sizeof(TSPEC_BODY));
	memset(&pTsCommonInfo->TClass, 0, sizeof(QOS_TCLAS)*TCLAS_NUM);
	pTsCommonInfo->TClasProc = 0;
	pTsCommonInfo->TClasNum = 0;
}

void ResetTxTsEntry(PTX_TS_RECORD pTS)
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

void ResetRxTsEntry(PRX_TS_RECORD pTS)
{
	ResetTsCommonInfo(&pTS->TsCommonInfo);
	pTS->RxIndicateSeq = 0xffff;
	pTS->RxTimeoutIndicateSeq = 0xffff;
	ResetBaEntry(&pTS->RxAdmittedBARecord);
}
#ifdef _RTL8192_EXT_PATCH_
void ResetAdmitTRStream(struct rtllib_device *ieee, u8 *Addr)
{
	u8	dir;
	bool				search_dir[4] = {0, 0, 0, 0};
	struct list_head*		psearch_list;
	PTS_COMMON_INFO	pRet = NULL;
	PRX_TS_RECORD pRxTS = NULL;
	PTX_TS_RECORD pTxTS = NULL;

	if (ieee->iw_mode != IW_MODE_MESH)
		return;

	search_dir[DIR_DOWN]	= true;
	psearch_list = &ieee->Rx_TS_Admit_List;
	for (dir = 0; dir <= DIR_BI_DIR; dir++)
	{
		if (search_dir[dir] ==false )
			continue;
		list_for_each_entry(pRet, psearch_list, List){
			if ((memcmp(pRet->Addr, Addr, 6) == 0) && (pRet->TSpec.f.TSInfo.field.ucDirection == dir))
			{
				pRxTS = (PRX_TS_RECORD)pRet;
				pRxTS->RxIndicateSeq = 0xffff;
				pRxTS->RxTimeoutIndicateSeq = 0xffff;
			}

		}
	}
	search_dir[DIR_UP]	= true;
	psearch_list = &ieee->Tx_TS_Admit_List;
	for (dir = 0; dir <= DIR_BI_DIR; dir++)
	{
		if (search_dir[dir] ==false )
			continue;
		list_for_each_entry(pRet, psearch_list, List){
			if ((memcmp(pRet->Addr, Addr, 6) == 0) && (pRet->TSpec.f.TSInfo.field.ucDirection == dir))
			{
				pTxTS = (PTX_TS_RECORD)pRet;
				pTxTS->TxCurSeq = 0xffff;
			}

		}
	}

	return;
}
#endif

void TSInitialize(struct rtllib_device *ieee)
{
	PTX_TS_RECORD		pTxTS  = ieee->TxTsRecord;
	PRX_TS_RECORD		pRxTS  = ieee->RxTsRecord;
	PRX_REORDER_ENTRY	pRxReorderEntry = ieee->RxReorderEntry;
	u8				count = 0;
	RTLLIB_DEBUG(RTLLIB_DL_TS, "==========>%s()\n", __func__);
	INIT_LIST_HEAD(&ieee->Tx_TS_Admit_List);
	INIT_LIST_HEAD(&ieee->Tx_TS_Pending_List);
	INIT_LIST_HEAD(&ieee->Tx_TS_Unused_List);

	for (count = 0; count < TOTAL_TS_NUM; count++)
	{
		pTxTS->num = count;
		_setup_timer(&pTxTS->TsCommonInfo.SetupTimer,
			    TsSetupTimeOut,
			    (unsigned long) pTxTS);

		_setup_timer(&pTxTS->TsCommonInfo.InactTimer,
			    TsInactTimeout,
			    (unsigned long) pTxTS);

		_setup_timer(&pTxTS->TsAddBaTimer,
			    TsAddBaProcess,
			    (unsigned long) pTxTS);

		_setup_timer(&pTxTS->TxPendingBARecord.Timer,
			    BaSetupTimeOut,
			    (unsigned long) pTxTS);
		_setup_timer(&pTxTS->TxAdmittedBARecord.Timer,
			    TxBaInactTimeout,
			    (unsigned long) pTxTS);

		ResetTxTsEntry(pTxTS);
		list_add_tail(&pTxTS->TsCommonInfo.List,
				&ieee->Tx_TS_Unused_List);
		pTxTS++;
	}

	INIT_LIST_HEAD(&ieee->Rx_TS_Admit_List);
	INIT_LIST_HEAD(&ieee->Rx_TS_Pending_List);
	INIT_LIST_HEAD(&ieee->Rx_TS_Unused_List);
	for (count = 0; count < TOTAL_TS_NUM; count++)
	{
		pRxTS->num = count;
		INIT_LIST_HEAD(&pRxTS->RxPendingPktList);

		_setup_timer(&pRxTS->TsCommonInfo.SetupTimer,
			    TsSetupTimeOut,
			    (unsigned long) pRxTS);

		_setup_timer(&pRxTS->TsCommonInfo.InactTimer,
			    TsInactTimeout,
			    (unsigned long) pRxTS);

		_setup_timer(&pRxTS->RxAdmittedBARecord.Timer,
			    RxBaInactTimeout,
			    (unsigned long) pRxTS);

		_setup_timer(&pRxTS->RxPktPendingTimer,
			    RxPktPendingTimeout,
			    (unsigned long) pRxTS);

		ResetRxTsEntry(pRxTS);
		list_add_tail(&pRxTS->TsCommonInfo.List, &ieee->Rx_TS_Unused_List);
		pRxTS++;
	}
	INIT_LIST_HEAD(&ieee->RxReorder_Unused_List);
	for (count = 0; count < REORDER_ENTRY_NUM; count++)
	{
		list_add_tail( &pRxReorderEntry->List,&ieee->RxReorder_Unused_List);
		if (count == (REORDER_ENTRY_NUM-1))
			break;
		pRxReorderEntry = &ieee->RxReorderEntry[count+1];
	}

}

void AdmitTS(struct rtllib_device *ieee, PTS_COMMON_INFO pTsCommonInfo, u32 InactTime)
{
	del_timer_sync(&pTsCommonInfo->SetupTimer);
	del_timer_sync(&pTsCommonInfo->InactTimer);

	if (InactTime!=0)
		mod_timer(&pTsCommonInfo->InactTimer, jiffies + MSECS(InactTime));
}


PTS_COMMON_INFO SearchAdmitTRStream(struct rtllib_device *ieee, u8*	Addr, u8 TID, TR_SELECT	TxRxSelect)
{
	u8	dir;
	bool				search_dir[4] = {0, 0, 0, 0};
	struct list_head*		psearch_list;
	PTS_COMMON_INFO	pRet = NULL;
	if (ieee->iw_mode == IW_MODE_MASTER)
	{
		if (TxRxSelect == TX_DIR)
		{
			search_dir[DIR_DOWN] = true;
			search_dir[DIR_BI_DIR]= true;
		}
		else
		{
			search_dir[DIR_UP]	= true;
			search_dir[DIR_BI_DIR]= true;
		}
	}
	else if (ieee->iw_mode == IW_MODE_ADHOC)
	{
		if (TxRxSelect == TX_DIR)
			search_dir[DIR_UP]	= true;
		else
			search_dir[DIR_DOWN] = true;
	}
	else
	{
		if (TxRxSelect == TX_DIR)
		{
			search_dir[DIR_UP]	= true;
			search_dir[DIR_BI_DIR]= true;
			search_dir[DIR_DIRECT]= true;
		}
		else
		{
			search_dir[DIR_DOWN] = true;
			search_dir[DIR_BI_DIR]= true;
			search_dir[DIR_DIRECT]= true;
		}
	}

	if (TxRxSelect == TX_DIR)
		psearch_list = &ieee->Tx_TS_Admit_List;
	else
		psearch_list = &ieee->Rx_TS_Admit_List;

	for (dir = 0; dir <= DIR_BI_DIR; dir++)
	{
		if (search_dir[dir] ==false )
			continue;
		list_for_each_entry(pRet, psearch_list, List){
			if (memcmp(pRet->Addr, Addr, 6) == 0)
				if (pRet->TSpec.f.TSInfo.field.ucTSID == TID)
					if (pRet->TSpec.f.TSInfo.field.ucDirection == dir)
					{
						break;
					}

		}
		if (&pRet->List  != psearch_list)
			break;
	}

	if (&pRet->List  != psearch_list){
		return pRet ;
	}
	else
		return NULL;
}

void MakeTSEntry(
		PTS_COMMON_INFO	pTsCommonInfo,
		u8*		Addr,
		PTSPEC_BODY	pTSPEC,
		PQOS_TCLAS	pTCLAS,
		u8		TCLAS_Num,
		u8		TCLAS_Proc
	)
{
	u8	count;

	if (pTsCommonInfo == NULL)
		return;

	memcpy(pTsCommonInfo->Addr, Addr, 6);

	if (pTSPEC != NULL)
		memcpy((u8*)(&(pTsCommonInfo->TSpec)), (u8*)pTSPEC, sizeof(TSPEC_BODY));

	for (count = 0; count < TCLAS_Num; count++)
		memcpy((u8*)(&(pTsCommonInfo->TClass[count])), (u8*)pTCLAS, sizeof(QOS_TCLAS));

	pTsCommonInfo->TClasProc = TCLAS_Proc;
	pTsCommonInfo->TClasNum = TCLAS_Num;
}

#ifdef _RTL8192_EXT_PATCH_
void dump_ts_list(struct list_head * ts_list)
{
	PTS_COMMON_INFO	pRet = NULL;
	u8 i=0;
	list_for_each_entry(pRet, ts_list, List){
		printk("i=%d ADD:"MAC_FMT", TID:%d, dir:%d\n",i,MAC_ARG(pRet->Addr), pRet->TSpec.f.TSInfo.field.ucTSID, pRet->TSpec.f.TSInfo.field.ucDirection);
		i++;
	}

}
#endif

bool GetTs(
	struct rtllib_device*	ieee,
	PTS_COMMON_INFO			*ppTS,
	u8*				Addr,
	u8				TID,
	TR_SELECT			TxRxSelect,
	bool				bAddNewTs)
{
	u8	UP = 0;
	if (is_broadcast_ether_addr(Addr) || is_multicast_ether_addr(Addr))
	{
		RTLLIB_DEBUG(RTLLIB_DL_ERR, "ERR! get TS for Broadcast or Multicast\n");
		return false;
	}
	if (ieee->current_network.qos_data.supported == 0) {
		UP = 0;
	} else {
		if (!IsACValid(TID)) {
			RTLLIB_DEBUG(RTLLIB_DL_ERR, "ERR! in %s(), TID(%d) is not valid\n", __func__, TID);
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
	if (*ppTS != NULL)
	{
		return true;
	}
	else
	{
		if (bAddNewTs == false)
		{
			RTLLIB_DEBUG(RTLLIB_DL_TS, "add new TS failed(tid:%d)\n", UP);
			return false;
		}
		else
		{
			TSPEC_BODY	TSpec;
			PQOS_TSINFO		pTSInfo = &TSpec.f.TSInfo;
			struct list_head*	pUnusedList =
								(TxRxSelect == TX_DIR)?
								(&ieee->Tx_TS_Unused_List):
								(&ieee->Rx_TS_Unused_List);

			struct list_head*	pAddmitList =
								(TxRxSelect == TX_DIR)?
								(&ieee->Tx_TS_Admit_List):
								(&ieee->Rx_TS_Admit_List);

			DIRECTION_VALUE		Dir =		(ieee->iw_mode == IW_MODE_MASTER)?
								((TxRxSelect==TX_DIR)?DIR_DOWN:DIR_UP):
								((TxRxSelect==TX_DIR)?DIR_UP:DIR_DOWN);
			RTLLIB_DEBUG(RTLLIB_DL_TS, "to add Ts\n");
			if (!list_empty(pUnusedList))
			{
				(*ppTS) = list_entry(pUnusedList->next, TS_COMMON_INFO, List);
				list_del_init(&(*ppTS)->List);
				if (TxRxSelect==TX_DIR)
				{
					PTX_TS_RECORD tmp = container_of(*ppTS, TX_TS_RECORD, TsCommonInfo);
					ResetTxTsEntry(tmp);
				}
				else{
					PRX_TS_RECORD tmp = container_of(*ppTS, RX_TS_RECORD, TsCommonInfo);
					ResetRxTsEntry(tmp);
				}

				RTLLIB_DEBUG(RTLLIB_DL_TS, "to init current TS, UP:%d, Dir:%d, addr:"MAC_FMT" ppTs=%p\n", UP, Dir, MAC_ARG(Addr), *ppTS);
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
			else
			{
				RTLLIB_DEBUG(RTLLIB_DL_ERR, "ERR!!in function %s() There is not enough dir=%d(0=up down=1) TS record to be used!!", __func__,Dir);
				return false;
			}
		}
	}
}

void RemoveTsEntry(
	struct rtllib_device*	ieee,
	PTS_COMMON_INFO			pTs,
	TR_SELECT			TxRxSelect
	)
{
	del_timer_sync(&pTs->SetupTimer);
	del_timer_sync(&pTs->InactTimer);
	TsInitDelBA(ieee, pTs, TxRxSelect);

	if (TxRxSelect == RX_DIR)
	{
		PRX_REORDER_ENTRY	pRxReorderEntry;
		PRX_TS_RECORD		pRxTS = (PRX_TS_RECORD)pTs;

		if (timer_pending(&pRxTS->RxPktPendingTimer))
			del_timer_sync(&pRxTS->RxPktPendingTimer);

		while(!list_empty(&pRxTS->RxPendingPktList)){
			pRxReorderEntry = (PRX_REORDER_ENTRY)list_entry(pRxTS->RxPendingPktList.prev,RX_REORDER_ENTRY,List);
			RTLLIB_DEBUG(RTLLIB_DL_REORDER,"%s(): Delete SeqNum %d!\n",__func__, pRxReorderEntry->SeqNum);
			list_del_init(&pRxReorderEntry->List);
			{
				int i = 0;
				struct rtllib_rxb * prxb = pRxReorderEntry->prxb;
				if (unlikely(!prxb)){
					return;
				}
				for (i =0; i < prxb->nr_subframes; i++) {
					dev_kfree_skb(prxb->subframes[i]);
				}
				kfree(prxb);
				prxb = NULL;
			}
			list_add_tail(&pRxReorderEntry->List,&ieee->RxReorder_Unused_List);
		}
	}
	else{
		PTX_TS_RECORD pTxTS = (PTX_TS_RECORD)pTs;
		del_timer_sync(&pTxTS->TsAddBaTimer);
	}
}

void RemovePeerTS(struct rtllib_device* ieee, u8* Addr)
{
	PTS_COMMON_INFO	pTS, pTmpTS;
	printk("===========>RemovePeerTS,"MAC_FMT"\n", MAC_ARG(Addr));
#if 1
	list_for_each_entry_safe(pTS, pTmpTS, &ieee->Tx_TS_Pending_List, List)
	{
		if (memcmp(pTS->Addr, Addr, 6) == 0)
		{
			RemoveTsEntry(ieee, pTS, TX_DIR);
			list_del_init(&pTS->List);
			list_add_tail(&pTS->List, &ieee->Tx_TS_Unused_List);
		}
	}

	list_for_each_entry_safe(pTS, pTmpTS, &ieee->Tx_TS_Admit_List, List)
	{
		if (memcmp(pTS->Addr, Addr, 6) == 0)
		{
			printk("====>remove Tx_TS_admin_list\n");
			RemoveTsEntry(ieee, pTS, TX_DIR);
			list_del_init(&pTS->List);
			list_add_tail(&pTS->List, &ieee->Tx_TS_Unused_List);
		}
	}

	list_for_each_entry_safe(pTS, pTmpTS, &ieee->Rx_TS_Pending_List, List)
	{
		if (memcmp(pTS->Addr, Addr, 6) == 0)
		{
			RemoveTsEntry(ieee, pTS, RX_DIR);
			list_del_init(&pTS->List);
			list_add_tail(&pTS->List, &ieee->Rx_TS_Unused_List);
		}
	}

	list_for_each_entry_safe(pTS, pTmpTS, &ieee->Rx_TS_Admit_List, List)
	{
		if (memcmp(pTS->Addr, Addr, 6) == 0)
		{
			RemoveTsEntry(ieee, pTS, RX_DIR);
			list_del_init(&pTS->List);
			list_add_tail(&pTS->List, &ieee->Rx_TS_Unused_List);
		}
	}
#endif
}

void RemoveAllTS(struct rtllib_device* ieee)
{
	PTS_COMMON_INFO pTS, pTmpTS;
#if 1
	list_for_each_entry_safe(pTS, pTmpTS, &ieee->Tx_TS_Pending_List, List)
	{
		RemoveTsEntry(ieee, pTS, TX_DIR);
		list_del_init(&pTS->List);
		list_add_tail(&pTS->List, &ieee->Tx_TS_Unused_List);
	}

	list_for_each_entry_safe(pTS, pTmpTS, &ieee->Tx_TS_Admit_List, List)
	{
		RemoveTsEntry(ieee, pTS, TX_DIR);
		list_del_init(&pTS->List);
		list_add_tail(&pTS->List, &ieee->Tx_TS_Unused_List);
	}

	list_for_each_entry_safe(pTS, pTmpTS, &ieee->Rx_TS_Pending_List, List)
	{
		RemoveTsEntry(ieee, pTS, RX_DIR);
		list_del_init(&pTS->List);
		list_add_tail(&pTS->List, &ieee->Rx_TS_Unused_List);
	}

	list_for_each_entry_safe(pTS, pTmpTS, &ieee->Rx_TS_Admit_List, List)
	{
		RemoveTsEntry(ieee, pTS, RX_DIR);
		list_del_init(&pTS->List);
		list_add_tail(&pTS->List, &ieee->Rx_TS_Unused_List);
	}
#endif
}

void TsStartAddBaProcess(struct rtllib_device* ieee, PTX_TS_RECORD	pTxTS)
{
	if (pTxTS->bAddBaReqInProgress == false)
	{
		pTxTS->bAddBaReqInProgress = true;
#if 1
		if (pTxTS->bAddBaReqDelayed)
		{
			RTLLIB_DEBUG(RTLLIB_DL_BA, "TsStartAddBaProcess(): Delayed Start ADDBA after 60 sec!!\n");
			mod_timer(&pTxTS->TsAddBaTimer, jiffies + MSECS(TS_ADDBA_DELAY));
		}
		else
		{
			RTLLIB_DEBUG(RTLLIB_DL_BA,"TsStartAddBaProcess(): Immediately Start ADDBA now!!\n");
			mod_timer(&pTxTS->TsAddBaTimer, jiffies+10);
		}
#endif
	}
	else
		RTLLIB_DEBUG(RTLLIB_DL_BA, "%s()==>BA timer is already added\n", __func__);
}
