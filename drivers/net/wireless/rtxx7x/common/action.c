/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************/


#include "rt_config.h"
#include "action.h"

extern UCHAR  ZeroSsid[32];

static VOID ReservedAction(
	IN PRTMP_ADAPTER pAd, 
	IN MLME_QUEUE_ELEM *Elem);


/*  
    ==========================================================================
    Description: 
        association state machine init, including state transition and timer init
    Parameters: 
        S - pointer to the association state machine
    Note:
        The state machine looks like the following 
        
                                    ASSOC_IDLE             
        MT2_MLME_DISASSOC_REQ    mlme_disassoc_req_action 
        MT2_PEER_DISASSOC_REQ    peer_disassoc_action     
        MT2_PEER_ASSOC_REQ       drop                     
        MT2_PEER_REASSOC_REQ     drop                     
        MT2_CLS3ERR              cls3err_action           
    ==========================================================================
 */
VOID ActionStateMachineInit(
    IN	PRTMP_ADAPTER	pAd, 
    IN  STATE_MACHINE *S, 
    OUT STATE_MACHINE_FUNC Trans[]) 
{
	StateMachineInit(S, (STATE_MACHINE_FUNC *)Trans, MAX_ACT_STATE, MAX_ACT_MSG, (STATE_MACHINE_FUNC)Drop, ACT_IDLE, ACT_MACHINE_BASE);

	StateMachineSetAction(S, ACT_IDLE, MT2_PEER_SPECTRUM_CATE, (STATE_MACHINE_FUNC)PeerSpectrumAction);
	StateMachineSetAction(S, ACT_IDLE, MT2_PEER_QOS_CATE, (STATE_MACHINE_FUNC)PeerQOSAction);

	StateMachineSetAction(S, ACT_IDLE, MT2_PEER_DLS_CATE, (STATE_MACHINE_FUNC)ReservedAction);
#ifdef QOS_DLS_SUPPORT
		StateMachineSetAction(S, ACT_IDLE, MT2_PEER_DLS_CATE, (STATE_MACHINE_FUNC)PeerDLSAction);
#endif /* QOS_DLS_SUPPORT */

#ifdef DOT11_N_SUPPORT
	StateMachineSetAction(S, ACT_IDLE, MT2_PEER_BA_CATE, (STATE_MACHINE_FUNC)PeerBAAction);
	StateMachineSetAction(S, ACT_IDLE, MT2_PEER_HT_CATE, (STATE_MACHINE_FUNC)PeerHTAction);
	StateMachineSetAction(S, ACT_IDLE, MT2_MLME_ADD_BA_CATE, (STATE_MACHINE_FUNC)MlmeADDBAAction);
	StateMachineSetAction(S, ACT_IDLE, MT2_MLME_ORI_DELBA_CATE, (STATE_MACHINE_FUNC)MlmeDELBAAction);
	StateMachineSetAction(S, ACT_IDLE, MT2_MLME_REC_DELBA_CATE, (STATE_MACHINE_FUNC)MlmeDELBAAction);
#endif /* DOT11_N_SUPPORT */

	StateMachineSetAction(S, ACT_IDLE, MT2_PEER_PUBLIC_CATE, (STATE_MACHINE_FUNC)PeerPublicAction);
	StateMachineSetAction(S, ACT_IDLE, MT2_PEER_RM_CATE, (STATE_MACHINE_FUNC)PeerRMAction);
	
	StateMachineSetAction(S, ACT_IDLE, MT2_MLME_QOS_CATE, (STATE_MACHINE_FUNC)MlmeQOSAction);
	StateMachineSetAction(S, ACT_IDLE, MT2_MLME_DLS_CATE, (STATE_MACHINE_FUNC)MlmeDLSAction);
	StateMachineSetAction(S, ACT_IDLE, MT2_ACT_INVALID, (STATE_MACHINE_FUNC)MlmeInvalidAction);





}

#ifdef DOT11_N_SUPPORT
VOID MlmeADDBAAction(
    IN PRTMP_ADAPTER pAd, 
    IN MLME_QUEUE_ELEM *Elem) 

{
	MLME_ADDBA_REQ_STRUCT *pInfo;
	UCHAR           Addr[6];
	PUCHAR         pOutBuffer = NULL;
	NDIS_STATUS     NStatus;
	ULONG		Idx;
	FRAME_ADDBA_REQ  Frame;
	ULONG		FrameLen;
	BA_ORI_ENTRY			*pBAEntry = NULL;

	pInfo = (MLME_ADDBA_REQ_STRUCT *)Elem->Msg;
	NdisZeroMemory(&Frame, sizeof(FRAME_ADDBA_REQ));
	
	if(MlmeAddBAReqSanity(pAd, Elem->Msg, Elem->MsgLen, Addr) &&
		VALID_WCID(pInfo->Wcid)) 
	{
		NStatus = MlmeAllocateMemory(pAd, &pOutBuffer);  /*Get an unused nonpaged memory*/
		if(NStatus != NDIS_STATUS_SUCCESS) 
		{
			DBGPRINT(RT_DEBUG_TRACE,("BA - MlmeADDBAAction() allocate memory failed \n"));
			return;
		}
		/* 1. find entry*/
		Idx = pAd->MacTab.Content[pInfo->Wcid].BAOriWcidArray[pInfo->TID];
		if (Idx == 0)
		{
			MlmeFreeMemory(pAd, pOutBuffer);
			DBGPRINT(RT_DEBUG_ERROR,("BA - MlmeADDBAAction() can't find BAOriEntry \n"));
			return;
		} 
		else
		{
			pBAEntry =&pAd->BATable.BAOriEntry[Idx];
		}
		
#ifdef CONFIG_STA_SUPPORT
		IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		{
			if (ADHOC_ON(pAd)
#ifdef QOS_DLS_SUPPORT
				|| (IS_ENTRY_DLS(&pAd->MacTab.Content[pInfo->Wcid]))
#endif /* QOS_DLS_SUPPORT */
				)
				ActHeaderInit(pAd, &Frame.Hdr, pInfo->pAddr, pAd->CurrentAddress, pAd->CommonCfg.Bssid);
			else
				ActHeaderInit(pAd, &Frame.Hdr, pAd->CommonCfg.Bssid, pAd->CurrentAddress, pInfo->pAddr);
		}
#endif /* CONFIG_STA_SUPPORT */ 

		Frame.Category = CATEGORY_BA;
		Frame.Action = ADDBA_REQ;
		Frame.BaParm.AMSDUSupported = 0;
		Frame.BaParm.BAPolicy = IMMED_BA;
		Frame.BaParm.TID = pInfo->TID;
		Frame.BaParm.BufSize = pInfo->BaBufSize;
		Frame.Token = pInfo->Token;
		Frame.TimeOutValue = pInfo->TimeOutValue;
		Frame.BaStartSeq.field.FragNum = 0;
		Frame.BaStartSeq.field.StartSeq = pAd->MacTab.Content[pInfo->Wcid].TxSeq[pInfo->TID];

		*(USHORT *)(&(Frame.BaParm)) = cpu2le16((*(USHORT *)(&(Frame.BaParm))));
		Frame.TimeOutValue = cpu2le16(Frame.TimeOutValue);
		Frame.BaStartSeq.word = cpu2le16(Frame.BaStartSeq.word);

		MakeOutgoingFrame(pOutBuffer,		   &FrameLen,
		              sizeof(FRAME_ADDBA_REQ), &Frame,
		              END_OF_ARGS);

		MiniportMMRequest(pAd, (MGMT_USE_QUEUE_FLAG | MapUserPriorityToAccessCategory[pInfo->TID]), pOutBuffer, FrameLen);

		MlmeFreeMemory(pAd, pOutBuffer);
		
		DBGPRINT(RT_DEBUG_TRACE, ("BA - Send ADDBA request. StartSeq = %x,  FrameLen = %ld. BufSize = %d\n", Frame.BaStartSeq.field.StartSeq, FrameLen, Frame.BaParm.BufSize));
    }
}

/*
    ==========================================================================
    Description:
        send DELBA and delete BaEntry if any
    Parametrs:
        Elem - MLME message MLME_DELBA_REQ_STRUCT
        
	IRQL = DISPATCH_LEVEL

    ==========================================================================
 */
VOID MlmeDELBAAction(
    IN PRTMP_ADAPTER pAd, 
    IN MLME_QUEUE_ELEM *Elem) 
{
	MLME_DELBA_REQ_STRUCT *pInfo;
	PUCHAR         pOutBuffer = NULL;
	PUCHAR		   pOutBuffer2 = NULL;
	NDIS_STATUS     NStatus;
	ULONG		Idx;
	FRAME_DELBA_REQ  Frame;
	ULONG		FrameLen;
	FRAME_BAR	FrameBar;
	
	pInfo = (MLME_DELBA_REQ_STRUCT *)Elem->Msg;	
	/* must send back DELBA */
	NdisZeroMemory(&Frame, sizeof(FRAME_DELBA_REQ));
	DBGPRINT(RT_DEBUG_TRACE, ("==> MlmeDELBAAction(), Initiator(%d) \n", pInfo->Initiator));
	
	if(MlmeDelBAReqSanity(pAd, Elem->Msg, Elem->MsgLen) &&
		VALID_WCID(pInfo->Wcid)) 
	{
		NStatus = MlmeAllocateMemory(pAd, &pOutBuffer);  /*Get an unused nonpaged memory*/
		if(NStatus != NDIS_STATUS_SUCCESS) 
		{
			DBGPRINT(RT_DEBUG_ERROR,("BA - MlmeDELBAAction() allocate memory failed 1. \n"));
			return;
		}

		NStatus = MlmeAllocateMemory(pAd, &pOutBuffer2);  /*Get an unused nonpaged memory*/
		if(NStatus != NDIS_STATUS_SUCCESS) 
		{
			MlmeFreeMemory(pAd, pOutBuffer);
			DBGPRINT(RT_DEBUG_ERROR, ("BA - MlmeDELBAAction() allocate memory failed 2. \n"));
			return;
		}

		/* SEND BAR (Send BAR to refresh peer reordering buffer.)*/
		Idx = pAd->MacTab.Content[pInfo->Wcid].BAOriWcidArray[pInfo->TID];

#ifdef CONFIG_STA_SUPPORT
		IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
			BarHeaderInit(pAd, &FrameBar, pAd->MacTab.Content[pInfo->Wcid].Addr, pAd->CurrentAddress);
#endif /* CONFIG_STA_SUPPORT */

		FrameBar.StartingSeq.field.FragNum = 0; /* make sure sequence not clear in DEL funciton.*/
		FrameBar.StartingSeq.field.StartSeq = pAd->MacTab.Content[pInfo->Wcid].TxSeq[pInfo->TID]; /* make sure sequence not clear in DEL funciton.*/
		FrameBar.BarControl.TID = pInfo->TID; /* make sure sequence not clear in DEL funciton.*/
		FrameBar.BarControl.ACKPolicy = IMMED_BA; /* make sure sequence not clear in DEL funciton.*/
		FrameBar.BarControl.Compressed = 1; /* make sure sequence not clear in DEL funciton.*/
		FrameBar.BarControl.MTID = 0; /* make sure sequence not clear in DEL funciton.*/

		MakeOutgoingFrame(pOutBuffer2,				&FrameLen,
					  sizeof(FRAME_BAR),	  &FrameBar,
					  END_OF_ARGS);
		MiniportMMRequest(pAd, QID_AC_BE, pOutBuffer2, FrameLen);
		MlmeFreeMemory(pAd, pOutBuffer2);
		DBGPRINT(RT_DEBUG_TRACE,("BA - MlmeDELBAAction() . Send BAR to refresh peer reordering buffer \n"));

		/* SEND DELBA FRAME*/
		FrameLen = 0;
#ifdef CONFIG_STA_SUPPORT
		IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		{
			if (ADHOC_ON(pAd)
#ifdef QOS_DLS_SUPPORT
				|| (IS_ENTRY_DLS(&pAd->MacTab.Content[pInfo->Wcid]))
#endif /* QOS_DLS_SUPPORT */
				)
				ActHeaderInit(pAd, &Frame.Hdr, pAd->MacTab.Content[pInfo->Wcid].Addr, pAd->CurrentAddress, pAd->CommonCfg.Bssid);
			else
				ActHeaderInit(pAd, &Frame.Hdr,  pAd->CommonCfg.Bssid, pAd->CurrentAddress, pAd->MacTab.Content[pInfo->Wcid].Addr);
		}
#endif /* CONFIG_STA_SUPPORT */

		Frame.Category = CATEGORY_BA;
		Frame.Action = DELBA;
		Frame.DelbaParm.Initiator = pInfo->Initiator;
		Frame.DelbaParm.TID = pInfo->TID;
		Frame.ReasonCode = 39; /* Time Out*/
		*(USHORT *)(&Frame.DelbaParm) = cpu2le16(*(USHORT *)(&Frame.DelbaParm));
		Frame.ReasonCode = cpu2le16(Frame.ReasonCode);
		
		MakeOutgoingFrame(pOutBuffer,               &FrameLen,
		              sizeof(FRAME_DELBA_REQ),    &Frame,
		              END_OF_ARGS);
		MiniportMMRequest(pAd, QID_AC_BE, pOutBuffer, FrameLen);
		MlmeFreeMemory(pAd, pOutBuffer);
		DBGPRINT(RT_DEBUG_TRACE, ("BA - MlmeDELBAAction() . 3 DELBA sent. Initiator(%d)\n", pInfo->Initiator));
    	}
}
#endif /* DOT11_N_SUPPORT */

VOID MlmeQOSAction(
    IN PRTMP_ADAPTER pAd, 
    IN MLME_QUEUE_ELEM *Elem) 
{
}

VOID MlmeDLSAction(
    IN PRTMP_ADAPTER pAd, 
    IN MLME_QUEUE_ELEM *Elem) 
{
}

VOID MlmeInvalidAction(
    IN PRTMP_ADAPTER pAd, 
    IN MLME_QUEUE_ELEM *Elem) 
{
	/*PUCHAR		   pOutBuffer = NULL;*/
	/*Return the receiving frame except the MSB of category filed set to 1.  7.3.1.11*/
}

VOID PeerQOSAction(
	IN PRTMP_ADAPTER pAd, 
	IN MLME_QUEUE_ELEM *Elem) 
{
}

#ifdef QOS_DLS_SUPPORT
VOID PeerDLSAction(
	IN PRTMP_ADAPTER pAd, 
	IN MLME_QUEUE_ELEM *Elem) 
{
	UCHAR	Action = Elem->Msg[LENGTH_802_11+1];

	switch(Action)
	{
		case ACTION_DLS_REQUEST:
#ifdef CONFIG_STA_SUPPORT
			IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
			PeerDlsReqAction(pAd, Elem);
#endif /* CONFIG_STA_SUPPORT */
			break;

		case ACTION_DLS_RESPONSE:
#ifdef CONFIG_STA_SUPPORT
			IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
			PeerDlsRspAction(pAd, Elem);
#endif /* CONFIG_STA_SUPPORT */
			break;

		case ACTION_DLS_TEARDOWN:
#ifdef CONFIG_STA_SUPPORT
			IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
			PeerDlsTearDownAction(pAd, Elem);
#endif /* CONFIG_STA_SUPPORT */
			break;
	}
}
#endif /* QOS_DLS_SUPPORT */



#ifdef DOT11_N_SUPPORT
VOID PeerBAAction(
	IN PRTMP_ADAPTER pAd, 
	IN MLME_QUEUE_ELEM *Elem) 
{
	UCHAR	Action = Elem->Msg[LENGTH_802_11+1];
	
	switch(Action)
	{
		case ADDBA_REQ:
			PeerAddBAReqAction(pAd,Elem);
			break;
		case ADDBA_RESP:
			PeerAddBARspAction(pAd,Elem);
			break;
		case DELBA:
			PeerDelBAAction(pAd,Elem);
			break;
	}
}


#ifdef DOT11N_DRAFT3

#ifdef CONFIG_STA_SUPPORT
VOID StaPublicAction(
	IN PRTMP_ADAPTER pAd, 
	IN BSS_2040_COEXIST_IE *pBssCoexIE) 
{
	MLME_SCAN_REQ_STRUCT ScanReq;

	DBGPRINT(RT_DEBUG_TRACE,("ACTION - StaPeerPublicAction  Bss2040Coexist = %x\n", *((PUCHAR)pBssCoexIE)));

	/* AP asks Station to return a 20/40 BSS Coexistence mgmt frame.  So we first starts a scan, then send back 20/40 BSS Coexistence mgmt frame */
	if ((pBssCoexIE->field.InfoReq == 1) && (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_SCAN_2040)))
	{
		/* Clear record first.  After scan , will update those bit and send back to transmiter.*/
		pAd->CommonCfg.BSSCoexist2040.field.InfoReq = 1;
		pAd->CommonCfg.BSSCoexist2040.field.Intolerant40 = 0;
		pAd->CommonCfg.BSSCoexist2040.field.BSS20WidthReq = 0;
		/* Clear Trigger event table*/
		TriEventInit(pAd);
		/* Fill out stuff for scan request  and kick to scan*/
		ScanParmFill(pAd, &ScanReq, ZeroSsid, 0, BSS_ANY, SCAN_2040_BSS_COEXIST);
		MlmeEnqueue(pAd, SYNC_STATE_MACHINE, MT2_MLME_SCAN_REQ, sizeof(MLME_SCAN_REQ_STRUCT), &ScanReq, 0);
		pAd->Mlme.CntlMachine.CurrState = CNTL_WAIT_OID_LIST_SCAN;
		RTMP_MLME_HANDLER(pAd);
	}
}


/*
Description : Build Intolerant Channel Rerpot from Trigger event table.
return : how many bytes copied. 
*/
ULONG BuildIntolerantChannelRep(
	IN	PRTMP_ADAPTER	pAd,
	IN    PUCHAR  pDest) 
{
	ULONG			FrameLen = 0;
	ULONG			ReadOffset = 0;
	UCHAR			i, j, k, idx = 0;
	/*UCHAR			LastRegClass = 0xff;*/
	UCHAR			ChannelList[MAX_TRIGGER_EVENT];
	UCHAR			TmpRegClass;
	UCHAR			RegClassArray[7] = {0, 11,12, 32, 33, 54,55}; /* Those regulatory class has channel in 2.4GHz. See Annex J.*/


	RTMPZeroMemory(ChannelList, MAX_TRIGGER_EVENT);

	/* Find every regulatory class*/
	for ( k = 0;k < 7;k++)
	{
		TmpRegClass = RegClassArray[k];
		
		idx = 0;
		/* Find Channel report with the same regulatory class in 2.4GHz.*/
		for ( i = 0;i < pAd->CommonCfg.TriggerEventTab.EventANo;i++)
		{
			if (pAd->CommonCfg.TriggerEventTab.EventA[i].bValid == TRUE)
			{
				if (pAd->CommonCfg.TriggerEventTab.EventA[i].RegClass == TmpRegClass)
				{				
					for (j = 0;j < idx;j++)
					{
						if (ChannelList[j] == (UCHAR)pAd->CommonCfg.TriggerEventTab.EventA[i].Channel)
							break;
					}
					if ((j == idx))
					{
						ChannelList[idx] = (UCHAR)pAd->CommonCfg.TriggerEventTab.EventA[i].Channel;
						idx++;
					} 
					pAd->CommonCfg.TriggerEventTab.EventA[i].bValid = FALSE;
				}
				DBGPRINT(RT_DEBUG_ERROR,("ACT - BuildIntolerantChannelRep , Total Channel number = %d \n", idx));
			}
		}

		/* idx > 0 means this regulatory class has some channel report and need to copy to the pDest.*/
		if (idx > 0)
		{
			/* For each regaulatory IE report, contains all channels that has the same regulatory class.*/
			*(pDest + ReadOffset) = IE_2040_BSS_INTOLERANT_REPORT;  /* IE*/
			*(pDest + ReadOffset + 1) = 1+ idx;	/* Len = RegClass byte + channel byte.*/
			*(pDest + ReadOffset + 2) = TmpRegClass;	/* Len = RegClass byte + channel byte.*/
			RTMPMoveMemory(pDest + ReadOffset + 3, ChannelList, idx);

			FrameLen += (3 + idx);
			ReadOffset += (3 + idx);
		}
		
	}

	DBGPRINT(RT_DEBUG_ERROR,("ACT-BuildIntolerantChannelRep(Size=%ld)\n", FrameLen));
	hex_dump("ACT-pDestMsg", pDest, FrameLen);

	return FrameLen;
}

/*	
	==========================================================================
	Description: 
	After scan, Update 20/40 BSS Coexistence IE and send out.
	According to 802.11n D3.03 11.14.10
		
	Parameters: 
	==========================================================================
 */
VOID Update2040CoexistFrameAndNotify(
	IN	PRTMP_ADAPTER	pAd,
	IN    UCHAR  Wcid,
	IN	BOOLEAN	bAddIntolerantCha) 
{
	BSS_2040_COEXIST_IE		OldValue;

	DBGPRINT(RT_DEBUG_ERROR,("ACT - Update2040CoexistFrameAndNotify. BSSCoexist2040 = %x. EventANo = %d. \n", pAd->CommonCfg.BSSCoexist2040.word, pAd->CommonCfg.TriggerEventTab.EventANo));
	OldValue.word = pAd->CommonCfg.BSSCoexist2040.word;
	/* Reset value.*/
	pAd->CommonCfg.BSSCoexist2040.word = 0;

	if (pAd->CommonCfg.TriggerEventTab.EventBCountDown > 0)
		pAd->CommonCfg.BSSCoexist2040.field.BSS20WidthReq = 1;

	/* Need to check !!!!*/
	/* How STA will set Intolerant40 if implementation dependent. Now we don't set this bit first!!!!!*/
	/* So Only check BSS20WidthReq change.*/
	/*if (OldValue.field.BSS20WidthReq != pAd->CommonCfg.BSSCoexist2040.field.BSS20WidthReq)*/
	{
		Send2040CoexistAction(pAd, Wcid, bAddIntolerantCha);
	}
}

/*
Description : Send 20/40 BSS Coexistence Action frame If one trigger event is triggered.
*/
VOID Send2040CoexistAction(
	IN	PRTMP_ADAPTER	pAd,
	IN    UCHAR  Wcid,
	IN	BOOLEAN	bAddIntolerantCha) 
{
	PUCHAR			pOutBuffer = NULL;
	NDIS_STATUS 	NStatus;
	FRAME_ACTION_HDR	Frame;
	ULONG			FrameLen;
	UINT32			IntolerantChaRepLen;
	UCHAR			HtLen = 1;

	IntolerantChaRepLen = 0;
	NStatus = MlmeAllocateMemory(pAd, &pOutBuffer);  /*Get an unused nonpaged memory*/
	if(NStatus != NDIS_STATUS_SUCCESS) 
	{
		DBGPRINT(RT_DEBUG_ERROR,("ACT - Send2040CoexistAction() allocate memory failed \n"));
		return;
	}

	ActHeaderInit(pAd, &Frame.Hdr, pAd->MacTab.Content[Wcid].Addr, pAd->CurrentAddress, pAd->CommonCfg.Bssid);	

	Frame.Category = CATEGORY_PUBLIC;
	Frame.Action = ACTION_BSS_2040_COEXIST; /*COEXIST_2040_ACTION;*/
	
	MakeOutgoingFrame(pOutBuffer,				&FrameLen,
				  sizeof(FRAME_ACTION_HDR),	  &Frame,
				  1,                                &BssCoexistIe,
				  1,                                &HtLen,
				  1,                                &pAd->CommonCfg.BSSCoexist2040.word,
				  END_OF_ARGS);
	
	if (bAddIntolerantCha == TRUE)
		IntolerantChaRepLen = BuildIntolerantChannelRep(pAd, pOutBuffer + FrameLen);

	/*2009 PF#3: IOT issue with Motorola AP. It will not check the field of BSSCoexist2040.*/
	/*11.14.12 Switching between 40 MHz and 20 MHz*/
	DBGPRINT(RT_DEBUG_TRACE, ("IntolerantChaRepLen=%d, BSSCoexist2040=0x%x!\n", 
								IntolerantChaRepLen, pAd->CommonCfg.BSSCoexist2040.word));
	if (!((IntolerantChaRepLen == 0) && (pAd->CommonCfg.BSSCoexist2040.word == 0)))
		MiniportMMRequest(pAd, QID_AC_BE, pOutBuffer, FrameLen + IntolerantChaRepLen);
		
	MlmeFreeMemory(pAd, pOutBuffer);
	
	DBGPRINT(RT_DEBUG_TRACE,("ACT - Send2040CoexistAction( BSSCoexist2040 = 0x%x )  \n", pAd->CommonCfg.BSSCoexist2040.word));
}

VOID UpdateBssScanParm(
	IN	PRTMP_ADAPTER	pAd,
	IN	OVERLAP_BSS_SCAN_IE	APBssScan) 
{									 
	pAd->CommonCfg.Dot11BssWidthChanTranDelayFactor = le2cpu16(APBssScan.DelayFactor); /*APBssScan.DelayFactor[1] * 256 + APBssScan.DelayFactor[0];*/
	/* out of range defined in MIB... So fall back to default value.*/
	if ((pAd->CommonCfg.Dot11BssWidthChanTranDelayFactor <5) || (pAd->CommonCfg.Dot11BssWidthChanTranDelayFactor > 100))
	{
		/*DBGPRINT(RT_DEBUG_ERROR,("ACT - UpdateBssScanParm( Dot11BssWidthChanTranDelayFactor out of range !!!!)  \n"));*/
		pAd->CommonCfg.Dot11BssWidthChanTranDelayFactor = 5;
	}

	pAd->CommonCfg.Dot11BssWidthTriggerScanInt = le2cpu16(APBssScan.TriggerScanInt); /*APBssScan.TriggerScanInt[1] * 256 + APBssScan.TriggerScanInt[0];*/
	/* out of range defined in MIB... So fall back to default value.*/
	if ((pAd->CommonCfg.Dot11BssWidthTriggerScanInt < 10) ||(pAd->CommonCfg.Dot11BssWidthTriggerScanInt > 900))
	{
		/*DBGPRINT(RT_DEBUG_ERROR,("ACT - UpdateBssScanParm( Dot11BssWidthTriggerScanInt out of range !!!!)  \n"));*/
		pAd->CommonCfg.Dot11BssWidthTriggerScanInt = 900;
	}
		
	pAd->CommonCfg.Dot11OBssScanPassiveDwell = le2cpu16(APBssScan.ScanPassiveDwell); /*APBssScan.ScanPassiveDwell[1] * 256 + APBssScan.ScanPassiveDwell[0];*/
	/* out of range defined in MIB... So fall back to default value.*/
	if ((pAd->CommonCfg.Dot11OBssScanPassiveDwell < 5) ||(pAd->CommonCfg.Dot11OBssScanPassiveDwell > 1000))
	{
		/*DBGPRINT(RT_DEBUG_ERROR,("ACT - UpdateBssScanParm( Dot11OBssScanPassiveDwell out of range !!!!)  \n"));*/
		pAd->CommonCfg.Dot11OBssScanPassiveDwell = 20;
	}
	
	pAd->CommonCfg.Dot11OBssScanActiveDwell = le2cpu16(APBssScan.ScanActiveDwell); /*APBssScan.ScanActiveDwell[1] * 256 + APBssScan.ScanActiveDwell[0];*/
	/* out of range defined in MIB... So fall back to default value.*/
	if ((pAd->CommonCfg.Dot11OBssScanActiveDwell < 10) ||(pAd->CommonCfg.Dot11OBssScanActiveDwell > 1000))
	{
		/*DBGPRINT(RT_DEBUG_ERROR,("ACT - UpdateBssScanParm( Dot11OBssScanActiveDwell out of range !!!!)  \n"));*/
		pAd->CommonCfg.Dot11OBssScanActiveDwell = 10;
	}
	
	pAd->CommonCfg.Dot11OBssScanPassiveTotalPerChannel = le2cpu16(APBssScan.PassiveTalPerChannel); /*APBssScan.PassiveTalPerChannel[1] * 256 + APBssScan.PassiveTalPerChannel[0];*/
	/* out of range defined in MIB... So fall back to default value.*/
	if ((pAd->CommonCfg.Dot11OBssScanPassiveTotalPerChannel < 200) ||(pAd->CommonCfg.Dot11OBssScanPassiveTotalPerChannel > 10000))
	{
		/*DBGPRINT(RT_DEBUG_ERROR,("ACT - UpdateBssScanParm( Dot11OBssScanPassiveTotalPerChannel out of range !!!!)  \n"));*/
		pAd->CommonCfg.Dot11OBssScanPassiveTotalPerChannel = 200;
	}
	
	pAd->CommonCfg.Dot11OBssScanActiveTotalPerChannel = le2cpu16(APBssScan.ActiveTalPerChannel); /*APBssScan.ActiveTalPerChannel[1] * 256 + APBssScan.ActiveTalPerChannel[0];*/
	/* out of range defined in MIB... So fall back to default value.*/
	if ((pAd->CommonCfg.Dot11OBssScanActiveTotalPerChannel < 20) ||(pAd->CommonCfg.Dot11OBssScanActiveTotalPerChannel > 10000))
	{
		/*DBGPRINT(RT_DEBUG_ERROR,("ACT - UpdateBssScanParm( Dot11OBssScanActiveTotalPerChannel out of range !!!!)  \n"));*/
		pAd->CommonCfg.Dot11OBssScanActiveTotalPerChannel = 20;
	}
	
	pAd->CommonCfg.Dot11OBssScanActivityThre = le2cpu16(APBssScan.ScanActThre); /*APBssScan.ScanActThre[1] * 256 + APBssScan.ScanActThre[0];*/
	/* out of range defined in MIB... So fall back to default value.*/
	if (pAd->CommonCfg.Dot11OBssScanActivityThre > 100)
	{
		/*DBGPRINT(RT_DEBUG_ERROR,("ACT - UpdateBssScanParm( Dot11OBssScanActivityThre out of range !!!!)  \n"));*/
		pAd->CommonCfg.Dot11OBssScanActivityThre = 25;
	}

	pAd->CommonCfg.Dot11BssWidthChanTranDelay = (pAd->CommonCfg.Dot11BssWidthTriggerScanInt * pAd->CommonCfg.Dot11BssWidthChanTranDelayFactor);
	/*DBGPRINT(RT_DEBUG_LOUD,("ACT - UpdateBssScanParm( Dot11BssWidthTriggerScanInt = %d )  \n", pAd->CommonCfg.Dot11BssWidthTriggerScanInt));*/
}

#endif /* CONFIG_STA_SUPPORT */


BOOLEAN ChannelSwitchSanityCheck(
	IN	PRTMP_ADAPTER	pAd,
	IN    UCHAR  Wcid,
	IN    UCHAR  NewChannel,
	IN    UCHAR  Secondary) 
{
	UCHAR		i;
	
	if (Wcid >= MAX_LEN_OF_MAC_TABLE)
		return FALSE;

	if ((NewChannel > 7) && (Secondary == 1))
		return FALSE;

	if ((NewChannel < 5) && (Secondary == 3))
		return FALSE;

	/* 0. Check if new channel is in the channellist.*/
	for (i = 0;i < pAd->ChannelListNum;i++)
	{
		if (pAd->ChannelList[i].Channel == NewChannel)
		{
			break;
		}
	}

	if (i == pAd->ChannelListNum)
		return FALSE;
	
	return TRUE;
}


VOID ChannelSwitchAction(
	IN	PRTMP_ADAPTER	pAd,
	IN    UCHAR  Wcid,
	IN    UCHAR  NewChannel,
	IN    UCHAR  Secondary) 
{
	UCHAR		BBPValue = 0;
	INT32		MACValue;
	
	DBGPRINT(RT_DEBUG_TRACE,("SPECTRUM - ChannelSwitchAction(NewChannel = %d , Secondary = %d)  \n", NewChannel, Secondary));

	if (ChannelSwitchSanityCheck(pAd, Wcid, NewChannel, Secondary) == FALSE)
		return;
	
	/* 1.  Switches to BW = 20.*/
	if (Secondary == 0)
	{
		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R4, &BBPValue);
		BBPValue&= (~0x18);
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R4, BBPValue);
		if (pAd->MACVersion == 0x28600100)
		{
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R69, 0x16);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R70, 0x08);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R73, 0x11);
			DBGPRINT(RT_DEBUG_TRACE, ("!!!rt2860C !!! \n" ));
		}
		pAd->CommonCfg.BBPCurrentBW = BW_20;
		pAd->CommonCfg.Channel = NewChannel;
		pAd->CommonCfg.CentralChannel = pAd->CommonCfg.Channel;
		AsicSwitchChannel(pAd, pAd->CommonCfg.Channel,FALSE);
		AsicLockChannel(pAd, pAd->CommonCfg.Channel);
		pAd->MacTab.Content[Wcid].HTPhyMode.field.BW = 0;
		pAd->CommonCfg.AddHTInfo.AddHtInfo.RecomWidth = 0;
                pAd->CommonCfg.AddHTInfo.AddHtInfo.ExtChanOffset = 0;		
		DBGPRINT(RT_DEBUG_TRACE, ("!!!20MHz   !!! \n" ));
	}
	/* 1.  Switches to BW = 40 And Station supports BW = 40.*/
	else if (((Secondary == 1) || (Secondary == 3)) && (pAd->CommonCfg.HtCapability.HtCapInfo.ChannelWidth == 1))
	{
		pAd->CommonCfg.Channel = NewChannel;

		if (Secondary == 1)
		{
			/* Secondary above.*/
			pAd->CommonCfg.CentralChannel = pAd->CommonCfg.Channel + 2;
			RTMP_IO_READ32(pAd, TX_BAND_CFG, &MACValue);
			MACValue &= 0xfe;
			RTMP_IO_WRITE32(pAd, TX_BAND_CFG, MACValue);
			RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R4, &BBPValue);
			BBPValue&= (~0x18);

			BBPValue|= (0x10);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R4, BBPValue);
			RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R3, &BBPValue);
			BBPValue&= (~0x20);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R3, BBPValue);
			DBGPRINT(RT_DEBUG_TRACE, ("!!!40MHz Lower LINK UP !!! Control Channel at Below. Central = %d \n", pAd->CommonCfg.CentralChannel ));
		}
		else
		{

			/* Secondary below.*/
			pAd->CommonCfg.CentralChannel = pAd->CommonCfg.Channel - 2;
			RTMP_IO_READ32(pAd, TX_BAND_CFG, &MACValue);
			MACValue &= 0xfe;
			MACValue |= 0x1;
			RTMP_IO_WRITE32(pAd, TX_BAND_CFG, MACValue);
			RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R4, &BBPValue);
			BBPValue&= (~0x18);
			BBPValue|= (0x10);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R4, BBPValue);
			RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R3, &BBPValue);
			BBPValue&= (~0x20);
			BBPValue|= (0x20);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R3, BBPValue);
			DBGPRINT(RT_DEBUG_TRACE, ("!!!40MHz Upper LINK UP !!! Control Channel at UpperCentral = %d \n", pAd->CommonCfg.CentralChannel ));
		}
		pAd->CommonCfg.BBPCurrentBW = BW_40;
		AsicSwitchChannel(pAd, pAd->CommonCfg.CentralChannel, FALSE);
		AsicLockChannel(pAd, pAd->CommonCfg.CentralChannel);
		pAd->MacTab.Content[Wcid].HTPhyMode.field.BW = 1;
	}
}
#endif /* DOT11N_DRAFT3 */
#endif /* DOT11_N_SUPPORT */

VOID PeerPublicAction(
	IN PRTMP_ADAPTER pAd, 
	IN MLME_QUEUE_ELEM *Elem) 
{
	UCHAR	Action = Elem->Msg[LENGTH_802_11+1];
	if (Elem->Wcid >= MAX_LEN_OF_MAC_TABLE)
		return;


	switch(Action)
	{
#ifdef DOT11_N_SUPPORT
#ifdef DOT11N_DRAFT3
		case ACTION_BSS_2040_COEXIST:	/* Format defined in IEEE 7.4.7a.1 in 11n Draf3.03*/
			{
				/*UCHAR	BssCoexist;*/
				BSS_2040_COEXIST_ELEMENT		*pCoexistInfo;
				BSS_2040_COEXIST_IE 			*pBssCoexistIe;
				BSS_2040_INTOLERANT_CH_REPORT	*pIntolerantReport = NULL;
				
				if (Elem->MsgLen <= (LENGTH_802_11 + sizeof(BSS_2040_COEXIST_ELEMENT)) )
				{
					DBGPRINT(RT_DEBUG_ERROR, ("ACTION - 20/40 BSS Coexistence Management Frame length too short! len = %ld!\n", Elem->MsgLen));
					break;
				}			
				DBGPRINT(RT_DEBUG_TRACE, ("ACTION - 20/40 BSS Coexistence Management action----> \n"));
				hex_dump("CoexistenceMgmtFrame", Elem->Msg, Elem->MsgLen);

				
				pCoexistInfo = (BSS_2040_COEXIST_ELEMENT *) &Elem->Msg[LENGTH_802_11+2];
				/*hex_dump("CoexistInfo", (PUCHAR)pCoexistInfo, sizeof(BSS_2040_COEXIST_ELEMENT));*/
				if (Elem->MsgLen >= (LENGTH_802_11 + sizeof(BSS_2040_COEXIST_ELEMENT) + sizeof(BSS_2040_INTOLERANT_CH_REPORT)))
				{
					pIntolerantReport = (BSS_2040_INTOLERANT_CH_REPORT *)((PUCHAR)pCoexistInfo + sizeof(BSS_2040_COEXIST_ELEMENT));
				}
				/*hex_dump("IntolerantReport ", (PUCHAR)pIntolerantReport, sizeof(BSS_2040_INTOLERANT_CH_REPORT));*/
				
				if(pAd->CommonCfg.bBssCoexEnable == FALSE || (pAd->CommonCfg.bForty_Mhz_Intolerant == TRUE))
				{
					DBGPRINT(RT_DEBUG_TRACE, ("20/40 BSS CoexMgmt=%d, bForty_Mhz_Intolerant=%d, ignore this action!!\n", 
												pAd->CommonCfg.bBssCoexEnable,
												pAd->CommonCfg.bForty_Mhz_Intolerant));
					break;
				}

				pBssCoexistIe = (BSS_2040_COEXIST_IE *)(&pCoexistInfo->BssCoexistIe);

#ifdef CONFIG_STA_SUPPORT
				IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
				{
					if (INFRA_ON(pAd))
					{
						StaPublicAction(pAd, pBssCoexistIe);
					}
				}
#endif /* CONFIG_STA_SUPPORT */

			}
			break;
#endif /* DOT11N_DRAFT3 */
#endif /* DOT11_N_SUPPORT */

	}


}	


static VOID ReservedAction(
	IN PRTMP_ADAPTER pAd, 
	IN MLME_QUEUE_ELEM *Elem)
{
	UCHAR Category;

	if (Elem->MsgLen <= LENGTH_802_11)
	{
		return;
	}

	Category = Elem->Msg[LENGTH_802_11];
	DBGPRINT(RT_DEBUG_TRACE,("Rcv reserved category(%d) Action Frame\n", Category));
	hex_dump("Reserved Action Frame", &Elem->Msg[0], Elem->MsgLen);
}

VOID PeerRMAction(
	IN PRTMP_ADAPTER pAd, 
	IN MLME_QUEUE_ELEM *Elem) 

{
	return;
}

#ifdef DOT11_N_SUPPORT
static VOID respond_ht_information_exchange_action(
	IN PRTMP_ADAPTER pAd,
	IN MLME_QUEUE_ELEM *Elem) 
{
	PUCHAR			pOutBuffer = NULL;
	NDIS_STATUS		NStatus;
	ULONG			FrameLen;
	FRAME_HT_INFO	HTINFOframe, *pFrame;
	UCHAR   		*pAddr;


	/* 2. Always send back ADDBA Response */
	NStatus = MlmeAllocateMemory(pAd, &pOutBuffer);	 /*Get an unused nonpaged memory*/

	if (NStatus != NDIS_STATUS_SUCCESS)
	{
		DBGPRINT(RT_DEBUG_TRACE,("ACTION - respond_ht_information_exchange_action() allocate memory failed \n"));
		return;
	}

	/* get RA*/
	pFrame = (FRAME_HT_INFO *) &Elem->Msg[0];
	pAddr = pFrame->Hdr.Addr2;

	NdisZeroMemory(&HTINFOframe, sizeof(FRAME_HT_INFO));
	/* 2-1. Prepare ADDBA Response frame.*/
#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
	{
		if (ADHOC_ON(pAd))
			ActHeaderInit(pAd, &HTINFOframe.Hdr, pAddr, pAd->CurrentAddress, pAd->CommonCfg.Bssid);
		else
		ActHeaderInit(pAd, &HTINFOframe.Hdr, pAd->CommonCfg.Bssid, pAd->CurrentAddress, pAddr);
	}
#endif /* CONFIG_STA_SUPPORT */

	HTINFOframe.Category = CATEGORY_HT;
	HTINFOframe.Action = HT_INFO_EXCHANGE;
	HTINFOframe.HT_Info.Request = 0;
	HTINFOframe.HT_Info.Forty_MHz_Intolerant = pAd->CommonCfg.HtCapability.HtCapInfo.Forty_Mhz_Intolerant;
	HTINFOframe.HT_Info.STA_Channel_Width	 = pAd->CommonCfg.AddHTInfo.AddHtInfo.RecomWidth;	

	MakeOutgoingFrame(pOutBuffer,					&FrameLen,
					  sizeof(FRAME_HT_INFO),	&HTINFOframe,
					  END_OF_ARGS);

	MiniportMMRequest(pAd, QID_AC_BE, pOutBuffer, FrameLen);
	MlmeFreeMemory(pAd, pOutBuffer);
}




VOID PeerHTAction(
	IN PRTMP_ADAPTER pAd, 
	IN MLME_QUEUE_ELEM *Elem) 
{
	UCHAR	Action = Elem->Msg[LENGTH_802_11+1];
	
	if (Elem->Wcid >= MAX_LEN_OF_MAC_TABLE)
		return;
	
	switch(Action)
	{
		case NOTIFY_BW_ACTION:
			DBGPRINT(RT_DEBUG_TRACE,("ACTION - HT Notify Channel bandwidth action----> \n"));
#ifdef CONFIG_STA_SUPPORT
			if(pAd->StaActive.SupportedPhyInfo.bHtEnable == FALSE)
			{
				/* Note, this is to patch DIR-1353 AP. When the AP set to Wep, it will use legacy mode. But AP still keeps */
				/* sending BW_Notify Action frame, and cause us to linkup and linkdown. */
				/* In legacy mode, don't need to parse HT action frame.*/
				DBGPRINT(RT_DEBUG_TRACE,("ACTION -Ignore HT Notify Channel BW when link as legacy mode. BW = %d---> \n", 
								Elem->Msg[LENGTH_802_11+2] ));
				break;
			}
#endif /* CONFIG_STA_SUPPORT */

			if (Elem->Msg[LENGTH_802_11+2] == 0)	/* 7.4.8.2. if value is 1, keep the same as supported channel bandwidth. */
				pAd->MacTab.Content[Elem->Wcid].HTPhyMode.field.BW = 0;
			else 
			{
				pAd->MacTab.Content[Elem->Wcid].HTPhyMode.field.BW = 
					pAd->MacTab.Content[Elem->Wcid].MaxHTPhyMode.field.BW & pAd->CommonCfg.HtCapability.HtCapInfo.ChannelWidth;
			}
			
			break;

		case SMPS_ACTION:
			/* 7.3.1.25*/
 			DBGPRINT(RT_DEBUG_TRACE,("ACTION - SMPS action----> \n"));
			if (((Elem->Msg[LENGTH_802_11+2]&0x1) == 0))
			{
				pAd->MacTab.Content[Elem->Wcid].MmpsMode = MMPS_ENABLE;
			}
			else if (((Elem->Msg[LENGTH_802_11+2]&0x2) == 0))
			{
				pAd->MacTab.Content[Elem->Wcid].MmpsMode = MMPS_STATIC;
			}
			else
			{			
				pAd->MacTab.Content[Elem->Wcid].MmpsMode = MMPS_DYNAMIC;
			}

			DBGPRINT(RT_DEBUG_TRACE,("Aid(%d) MIMO PS = %d\n", Elem->Wcid, pAd->MacTab.Content[Elem->Wcid].MmpsMode));
			/* rt2860c : add something for smps change.*/
			break;
 
		case SETPCO_ACTION:
			break;
			
		case MIMO_CHA_MEASURE_ACTION:
			break;
			
		case HT_INFO_EXCHANGE:
			{			
				HT_INFORMATION_OCTET	*pHT_info;

				pHT_info = (HT_INFORMATION_OCTET *) &Elem->Msg[LENGTH_802_11+2];
    				/* 7.4.8.10*/
    				DBGPRINT(RT_DEBUG_TRACE,("ACTION - HT Information Exchange action----> \n"));
    				if (pHT_info->Request)
    				{
    					respond_ht_information_exchange_action(pAd, Elem);
    				}
			}
    		break;
	}
}


/*
	==========================================================================
	Description:
		Retry sending ADDBA Reqest.
		
	IRQL = DISPATCH_LEVEL
	
	Parametrs:
	p8023Header: if this is already 802.3 format, p8023Header is NULL
	
	Return	: TRUE if put into rx reordering buffer, shouldn't indicaterxhere.
				FALSE , then continue indicaterx at this moment.
	==========================================================================
 */
VOID ORIBATimerTimeout(
	IN	PRTMP_ADAPTER	pAd) 
{
	MAC_TABLE_ENTRY	*pEntry;
	INT			i, total;
/*	FRAME_BAR			FrameBar;*/
/*	ULONG			FrameLen;*/
/*	NDIS_STATUS 	NStatus;*/
/*	PUCHAR			pOutBuffer = NULL;*/
/*	USHORT			Sequence;*/
	UCHAR			TID;

#ifdef RALINK_ATE
	if (ATE_ON(pAd))
		return;
#endif /* RALINK_ATE */

	total = pAd->MacTab.Size * NUM_OF_TID;

	for (i = 1; ((i <MAX_LEN_OF_BA_ORI_TABLE) && (total > 0)) ; i++)
	{
		if  (pAd->BATable.BAOriEntry[i].ORI_BA_Status == Originator_Done)
		{
			pEntry = &pAd->MacTab.Content[pAd->BATable.BAOriEntry[i].Wcid];
			TID = pAd->BATable.BAOriEntry[i].TID;

			ASSERT(pAd->BATable.BAOriEntry[i].Wcid < MAX_LEN_OF_MAC_TABLE);
		}
		total --;
	}
}


VOID SendRefreshBAR(
	IN	PRTMP_ADAPTER	pAd,
	IN	MAC_TABLE_ENTRY	*pEntry) 
{
	FRAME_BAR		FrameBar;
	ULONG			FrameLen;
	NDIS_STATUS 	NStatus;
	PUCHAR			pOutBuffer = NULL;
	USHORT			Sequence;
	UCHAR			i, TID;
	USHORT			idx;
	BA_ORI_ENTRY	*pBAEntry;

	for (i = 0; i <NUM_OF_TID; i++)
	{
		idx = pEntry->BAOriWcidArray[i];
		if (idx == 0)
		{
			continue;
		}
		pBAEntry = &pAd->BATable.BAOriEntry[idx];

		if  (pBAEntry->ORI_BA_Status == Originator_Done)
		{
			TID = pBAEntry->TID;

			ASSERT(pBAEntry->Wcid < MAX_LEN_OF_MAC_TABLE);

			NStatus = MlmeAllocateMemory(pAd, &pOutBuffer);  /*Get an unused nonpaged memory*/
			if(NStatus != NDIS_STATUS_SUCCESS) 
			{
				DBGPRINT(RT_DEBUG_ERROR,("BA - MlmeADDBAAction() allocate memory failed \n"));
				return;
			}
				
			Sequence = pEntry->TxSeq[TID];


#ifdef CONFIG_STA_SUPPORT
			IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
				BarHeaderInit(pAd, &FrameBar, pEntry->Addr, pAd->CurrentAddress);
#endif /* CONFIG_STA_SUPPORT */

			FrameBar.StartingSeq.field.FragNum = 0; /* make sure sequence not clear in DEL function.*/
			FrameBar.StartingSeq.field.StartSeq = Sequence; /* make sure sequence not clear in DEL funciton.*/
			FrameBar.BarControl.TID = TID; /* make sure sequence not clear in DEL funciton.*/

			MakeOutgoingFrame(pOutBuffer,				&FrameLen,
							  sizeof(FRAME_BAR),	  &FrameBar,
							  END_OF_ARGS);
			/*if (!(CLIENT_STATUS_TEST_FLAG(pEntry, fCLIENT_STATUS_RALINK_CHIPSET)))*/
			if (1)	/* Now we always send BAR.*/
			{
				/*MiniportMMRequestUnlock(pAd, 0, pOutBuffer, FrameLen);*/
				MiniportMMRequest(pAd, (MGMT_USE_QUEUE_FLAG | MapUserPriorityToAccessCategory[TID]), pOutBuffer, FrameLen);				

			}
			MlmeFreeMemory(pAd, pOutBuffer);
		}
	}
}
#endif /* DOT11_N_SUPPORT */

VOID ActHeaderInit(
    IN	PRTMP_ADAPTER	pAd, 
    IN OUT PHEADER_802_11 pHdr80211, 
    IN PUCHAR Addr1, 
    IN PUCHAR Addr2,
    IN PUCHAR Addr3) 
{
    NdisZeroMemory(pHdr80211, sizeof(HEADER_802_11));
    pHdr80211->FC.Type = BTYPE_MGMT;
    pHdr80211->FC.SubType = SUBTYPE_ACTION;

    COPY_MAC_ADDR(pHdr80211->Addr1, Addr1);
	COPY_MAC_ADDR(pHdr80211->Addr2, Addr2);
    COPY_MAC_ADDR(pHdr80211->Addr3, Addr3);
}

VOID BarHeaderInit(
	IN	PRTMP_ADAPTER	pAd, 
	IN OUT PFRAME_BAR pCntlBar, 
	IN PUCHAR pDA,
	IN PUCHAR pSA) 
{
/*	USHORT	Duration;*/

	NdisZeroMemory(pCntlBar, sizeof(FRAME_BAR));
	pCntlBar->FC.Type = BTYPE_CNTL;
	pCntlBar->FC.SubType = SUBTYPE_BLOCK_ACK_REQ;
   	pCntlBar->BarControl.MTID = 0;
	pCntlBar->BarControl.Compressed = 1;
	pCntlBar->BarControl.ACKPolicy = 0;


	pCntlBar->Duration = 16 + RTMPCalcDuration(pAd, RATE_1, sizeof(FRAME_BA));

	COPY_MAC_ADDR(pCntlBar->Addr1, pDA);
	COPY_MAC_ADDR(pCntlBar->Addr2, pSA);
}


/*
	==========================================================================
	Description:
		Insert Category and action code into the action frame.
		
	Parametrs:
		1. frame buffer pointer.
		2. frame length.
		3. category code of the frame.
		4. action code of the frame.
	
	Return	: None.
	==========================================================================
 */
VOID InsertActField(
	IN PRTMP_ADAPTER pAd,
	OUT PUCHAR pFrameBuf,
	OUT PULONG pFrameLen,
	IN UINT8 Category,
	IN UINT8 ActCode)
{
	ULONG TempLen;

	MakeOutgoingFrame(	pFrameBuf,		&TempLen,
						1,				&Category,
						1,				&ActCode,
						END_OF_ARGS);

	*pFrameLen = *pFrameLen + TempLen;

	return;
}
