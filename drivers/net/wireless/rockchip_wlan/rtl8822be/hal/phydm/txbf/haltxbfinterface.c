/* SPDX-License-Identifier: GPL-2.0 */
//============================================================
// Description:
//
// This file is for TXBF interface mechanism
//
//============================================================
#include "mp_precomp.h"
#include "../phydm_precomp.h"

#if (BEAMFORMING_SUPPORT == 1)
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
VOID
Beamforming_GidPAid(
	PADAPTER	Adapter,
	PRT_TCB		pTcb
)
{
	u1Byte		Idx = 0;
	u1Byte		RA[6] ={0};
	pu1Byte		pHeader = GET_FRAME_OF_FIRST_FRAG(Adapter, pTcb);
	HAL_DATA_TYPE			*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T				pDM_Odm = &pHalData->DM_OutSrc;
	PRT_BEAMFORMING_INFO	pBeamInfo = &(pDM_Odm->BeamformingInfo);

	if (Adapter->HardwareType < HARDWARE_TYPE_RTL8192EE)
		return;
	else if (IS_WIRELESS_MODE_N(Adapter) == FALSE)
		return;

#if (SUPPORT_MU_BF == 1)
	if (pTcb->TxBFPktType == RT_BF_PKT_TYPE_BROADCAST_NDPA) { /* MU NDPA */
#else
	if (0) {
#endif
		/* Fill G_ID and P_AID */
		pTcb->G_ID = 63;
		if (pBeamInfo->FirstMUBFeeIndex < BEAMFORMEE_ENTRY_NUM) {
			pTcb->P_AID = pBeamInfo->BeamformeeEntry[pBeamInfo->FirstMUBFeeIndex].P_AID;			
			RT_DISP(FBEAM, FBEAM_FUN, ("[David]@%s End, G_ID=0x%X, P_AID=0x%X\n", __func__, pTcb->G_ID, pTcb->P_AID));
		}
	} else {
		GET_80211_HDR_ADDRESS1(pHeader, &RA);

		// VHT SU PPDU carrying one or more group addressed MPDUs or
		// Transmitting a VHT NDP intended for multiple recipients
		if (MacAddr_isBcst(RA) || MacAddr_isMulticast(RA)	|| pTcb->macId == MAC_ID_STATIC_FOR_BROADCAST_MULTICAST) {
			pTcb->G_ID = 63;
			pTcb->P_AID = 0;
		} else if (ACTING_AS_AP(Adapter)) {
			u2Byte	AID = (u2Byte) (MacIdGetOwnerAssociatedClientAID(Adapter, pTcb->macId) & 0x1ff);		/*AID[0:8]*/
	
			/*RT_DISP(FBEAM, FBEAM_FUN, ("@%s  pTcb->macId=0x%X, AID=0x%X\n", __func__, pTcb->macId, AID));*/
			pTcb->G_ID = 63;

			if (AID == 0)		/*A PPDU sent by an AP to a non associated STA*/
				pTcb->P_AID = 0;
			else {				/*Sent by an AP and addressed to a STA associated with that AP*/
				u2Byte	BSSID = 0;
				GET_80211_HDR_ADDRESS2(pHeader, &RA);
				BSSID = ((RA[5] & 0xf0) >> 4) ^ (RA[5] & 0xf);	/*BSSID[44:47] xor BSSID[40:43]*/
				pTcb->P_AID = (AID + BSSID *32) & 0x1ff;		/*(dec(A) + dec(B)*32) mod 512*/
			}
		} else if (ACTING_AS_IBSS(Adapter)) {
			pTcb->G_ID = 63;
			/*P_AID for infrasturcture mode; MACID for ad-hoc mode. */
			pTcb->P_AID = pTcb->macId;
		} else if (MgntLinkStatusQuery(Adapter)) {				/*Addressed to AP*/
			pTcb->G_ID = 0;
			GET_80211_HDR_ADDRESS1(pHeader, &RA);
			pTcb->P_AID =  RA[5];							/*RA[39:47]*/
			pTcb->P_AID = (pTcb->P_AID << 1) | (RA[4] >> 7 );
		} else {
			pTcb->G_ID = 63;
			pTcb->P_AID = 0;
		}
		/*RT_DISP(FBEAM, FBEAM_FUN, ("[David]@%s End, G_ID=0x%X, P_AID=0x%X\n", __func__, pTcb->G_ID, pTcb->P_AID));*/
	}
}


RT_STATUS
Beamforming_GetReportFrame(
	IN	PADAPTER		Adapter,
	IN	PRT_RFD			pRfd,
	IN	POCTET_STRING	pPduOS
	)
{
	HAL_DATA_TYPE				*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T					pDM_Odm = &pHalData->DM_OutSrc;
	PRT_BEAMFORMEE_ENTRY		pBeamformEntry = NULL;
	pu1Byte						pMIMOCtrlField, pCSIReport, pCSIMatrix;
	u1Byte						Idx, Nc, Nr, CH_W;
	u2Byte						CSIMatrixLen = 0;

	ACT_PKT_TYPE				pktType = ACT_PKT_TYPE_UNKNOWN;

	//Memory comparison to see if CSI report is the same with previous one
	pBeamformEntry = phydm_Beamforming_GetBFeeEntryByAddr(pDM_Odm, Frame_Addr2(*pPduOS), &Idx);

	if (pBeamformEntry == NULL) {
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("Beamforming_GetReportFrame: Cannot find entry by addr\n"));
		return RT_STATUS_FAILURE;
	}

	pktType = PacketGetActionFrameType(pPduOS);
	
	//-@ Modified by David
	if (pktType == ACT_PKT_VHT_COMPRESSED_BEAMFORMING) {
		pMIMOCtrlField = pPduOS->Octet + 26; 
		Nc = ((*pMIMOCtrlField) & 0x7) + 1;
		Nr = (((*pMIMOCtrlField) & 0x38) >> 3) + 1;
		CH_W =  (((*pMIMOCtrlField) & 0xC0) >> 6);
		pCSIMatrix = pMIMOCtrlField + 3 + Nc; //24+(1+1+3)+2  MAC header+(Category+ActionCode+MIMOControlField) +SNR(Nc=2)
		CSIMatrixLen = pPduOS->Length  - 26 -3 -Nc;
	} else if (pktType == ACT_PKT_HT_COMPRESSED_BEAMFORMING) {
		pMIMOCtrlField = pPduOS->Octet + 26; 
		Nc = ((*pMIMOCtrlField) & 0x3) + 1;
		Nr =  (((*pMIMOCtrlField) & 0xC) >> 2) + 1;
		CH_W =  (((*pMIMOCtrlField) & 0x10) >> 4);
		pCSIMatrix = pMIMOCtrlField + 6 + Nr;	//24+(1+1+6)+2  MAC header+(Category+ActionCode+MIMOControlField) +SNR(Nc=2)
		CSIMatrixLen = pPduOS->Length  - 26 -6 -Nr;
	} else
		return RT_STATUS_SUCCESS;	
	
	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] idx=%d, pkt type=%d, Nc=%d, Nr=%d, CH_W=%d\n", __func__, Idx, pktType, Nc, Nr, CH_W));		

	return RT_STATUS_SUCCESS;
}


VOID
ConstructHTNDPAPacket(
	PADAPTER		Adapter,
	pu1Byte			RA,
	pu1Byte			Buffer,
	pu4Byte			pLength,
	CHANNEL_WIDTH	BW
	)
{
	u2Byte					Duration= 0;
	PMGNT_INFO				pMgntInfo = &(Adapter->MgntInfo);
	OCTET_STRING			pNDPAFrame,ActionContent;
	u1Byte					ActionHdr[4] = {ACT_CAT_VENDOR, 0x00, 0xe0, 0x4c};

	PlatformZeroMemory(Buffer, 32);

	SET_80211_HDR_FRAME_CONTROL(Buffer,0);

	SET_80211_HDR_ORDER(Buffer, 1);
	SET_80211_HDR_TYPE_AND_SUBTYPE(Buffer,Type_Action_No_Ack);

	SET_80211_HDR_ADDRESS1(Buffer, RA);
	SET_80211_HDR_ADDRESS2(Buffer, Adapter->CurrentAddress);
	SET_80211_HDR_ADDRESS3(Buffer, pMgntInfo->Bssid);

	Duration = 2*aSifsTime + 40;
	
	if (BW == CHANNEL_WIDTH_40)
		Duration+= 87;
	else	
		Duration+= 180;

	SET_80211_HDR_DURATION(Buffer, Duration);

	//HT control field
	SET_HT_CTRL_CSI_STEERING(Buffer+sMacHdrLng, 3);
	SET_HT_CTRL_NDP_ANNOUNCEMENT(Buffer+sMacHdrLng, 1);
	
	FillOctetString(pNDPAFrame, Buffer, sMacHdrLng+sHTCLng);

	FillOctetString(ActionContent, ActionHdr, 4);
	PacketAppendData(&pNDPAFrame, ActionContent);	

	*pLength = 32;
}




BOOLEAN
SendFWHTNDPAPacket(
	IN	PVOID			pDM_VOID,
	IN	pu1Byte			RA,
	IN	CHANNEL_WIDTH	BW
	)
{
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PADAPTER				Adapter = pDM_Odm->Adapter;
	PRT_TCB 				pTcb;
	PRT_TX_LOCAL_BUFFER 	pBuf;
	BOOLEAN 				ret = TRUE;
	u4Byte					BufLen;
	pu1Byte					BufAddr;
	u1Byte					DescLen = 0, Idx = 0, NDPTxRate;
	PADAPTER				pDefAdapter = GetDefaultAdapter(Adapter);
	PRT_BEAMFORMING_INFO	pBeamInfo = &pDM_Odm->BeamformingInfo;
	HAL_DATA_TYPE			*pHalData = GET_HAL_DATA(Adapter);
	PRT_BEAMFORMEE_ENTRY	pBeamformEntry = phydm_Beamforming_GetBFeeEntryByAddr(pDM_Odm, RA, &Idx);

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] Start!\n", __func__));

	if (pBeamformEntry == NULL)
		return FALSE;

	NDPTxRate = Beamforming_GetHTNDPTxRate(pDM_Odm, pBeamformEntry->CompSteeringNumofBFer);
	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] NDPTxRate =%d\n", __func__, NDPTxRate));
	PlatformAcquireSpinLock(Adapter, RT_TX_SPINLOCK);

	if (MgntGetFWBuffer(pDefAdapter, &pTcb, &pBuf)) {
#if(DEV_BUS_TYPE != RT_PCI_INTERFACE)
		DescLen = Adapter->HWDescHeadLength - pHalData->USBALLDummyLength;
#endif
		BufAddr = pBuf->Buffer.VirtualAddress + DescLen;

		ConstructHTNDPAPacket(
				Adapter, 
				RA,
				BufAddr, 
				&BufLen,
				BW
				);

		pTcb->PacketLength = BufLen + DescLen;

		pTcb->bTxEnableSwCalcDur = TRUE;
		
		pTcb->BWOfPacket = BW;

		if(ACTING_AS_IBSS(Adapter) || ACTING_AS_AP(Adapter))
			pTcb->G_ID = 63;

		pTcb->P_AID = pBeamformEntry->P_AID;
		pTcb->DataRate = NDPTxRate;	/*rate of NDP decide by Nr*/

		Adapter->HalFunc.CmdSendPacketHandler(Adapter, pTcb, pBuf, pTcb->PacketLength, DESC_PACKET_TYPE_NORMAL, FALSE);
	} else
		ret = FALSE;

	PlatformReleaseSpinLock(Adapter, RT_TX_SPINLOCK);
	
	if (ret)
		RT_DISP_DATA(FBEAM, FBEAM_DATA, "", pBuf->Buffer.VirtualAddress, pTcb->PacketLength);

	return ret;
}


BOOLEAN
SendSWHTNDPAPacket(
	IN	PVOID			pDM_VOID,
	IN	pu1Byte			RA,
	IN	CHANNEL_WIDTH	BW
	)
{
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PADAPTER				Adapter = pDM_Odm->Adapter;
	PRT_TCB					pTcb;
	PRT_TX_LOCAL_BUFFER		pBuf;
	BOOLEAN					ret = TRUE;
	u1Byte					Idx = 0, NDPTxRate = 0;
	PRT_BEAMFORMING_INFO	pBeamInfo = &pDM_Odm->BeamformingInfo;
	PRT_BEAMFORMEE_ENTRY	pBeamformEntry = phydm_Beamforming_GetBFeeEntryByAddr(pDM_Odm, RA, &Idx);

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] Start!\n", __func__));

	NDPTxRate = Beamforming_GetHTNDPTxRate(pDM_Odm, pBeamformEntry->CompSteeringNumofBFer);
	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] NDPTxRate =%d\n", __func__, NDPTxRate));
	
	PlatformAcquireSpinLock(Adapter, RT_TX_SPINLOCK);

	if (MgntGetBuffer(Adapter, &pTcb, &pBuf)) {
		ConstructHTNDPAPacket(
				Adapter, 
				RA,
				pBuf->Buffer.VirtualAddress, 
				&pTcb->PacketLength,
				BW
				);

		pTcb->bTxEnableSwCalcDur = TRUE;

		pTcb->BWOfPacket = BW;

		MgntSendPacket(Adapter, pTcb, pBuf, pTcb->PacketLength, NORMAL_QUEUE, NDPTxRate);
	} else
		ret = FALSE;
	
	PlatformReleaseSpinLock(Adapter, RT_TX_SPINLOCK);

	if (ret)
		RT_DISP_DATA(FBEAM, FBEAM_DATA, "", pBuf->Buffer.VirtualAddress, pTcb->PacketLength);

	return ret;
}



VOID
ConstructVHTNDPAPacket(
	IN PDM_ODM_T	pDM_Odm,
	pu1Byte			RA,
	u2Byte			AID,
	pu1Byte			Buffer,
	pu4Byte			pLength,
	CHANNEL_WIDTH	BW
	)
{
	u2Byte					Duration= 0;
	u1Byte					Sequence = 0;
	pu1Byte					pNDPAFrame = Buffer;
	RT_NDPA_STA_INFO		STAInfo;
	PADAPTER				Adapter = pDM_Odm->Adapter;
	u1Byte	Idx = 0;
	PRT_BEAMFORMEE_ENTRY	pBeamformEntry = phydm_Beamforming_GetBFeeEntryByAddr(pDM_Odm, RA, &Idx);
	// Frame control.
	SET_80211_HDR_FRAME_CONTROL(pNDPAFrame, 0);
	SET_80211_HDR_TYPE_AND_SUBTYPE(pNDPAFrame, Type_NDPA);

	SET_80211_HDR_ADDRESS1(pNDPAFrame, RA);
	SET_80211_HDR_ADDRESS2(pNDPAFrame, pBeamformEntry->MyMacAddr);

	Duration = 2*aSifsTime + 44;
	
	if (BW == CHANNEL_WIDTH_80)
		Duration += 40;
	else if(BW == CHANNEL_WIDTH_40)
		Duration+= 87;
	else	
		Duration+= 180;

	SET_80211_HDR_DURATION(pNDPAFrame, Duration);

	Sequence = *(pDM_Odm->pSoundingSeq) << 2;
	ODM_MoveMemory(pDM_Odm, pNDPAFrame+16, &Sequence, 1);

	if (phydm_actingDetermine(pDM_Odm, PhyDM_ACTING_AS_IBSS) || phydm_actingDetermine(pDM_Odm, PhyDM_ACTING_AS_AP) == FALSE)
		AID = 0;

	STAInfo.AID = AID;
	STAInfo.FeedbackType = 0;
	STAInfo.NcIndex = 0;
	
	ODM_MoveMemory(pDM_Odm, pNDPAFrame+17, (pu1Byte)&STAInfo, 2);

	*pLength = 19;
}


BOOLEAN
SendFWVHTNDPAPacket(
	IN	PVOID			pDM_VOID,
	IN	pu1Byte			RA,
	IN	u2Byte			AID,
	IN	CHANNEL_WIDTH	BW
	)
{
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PADAPTER				Adapter = pDM_Odm->Adapter;
	PRT_TCB					pTcb;
	PRT_TX_LOCAL_BUFFER		pBuf;
	BOOLEAN					ret = TRUE;
	u4Byte					BufLen;
	pu1Byte					BufAddr;
	u1Byte					DescLen = 0, Idx = 0, NDPTxRate = 0;
	PADAPTER				pDefAdapter = GetDefaultAdapter(Adapter);
	PRT_BEAMFORMING_INFO	pBeamInfo = &pDM_Odm->BeamformingInfo;
	HAL_DATA_TYPE			*pHalData = GET_HAL_DATA(Adapter);
	PRT_BEAMFORMEE_ENTRY	pBeamformEntry =phydm_Beamforming_GetBFeeEntryByAddr(pDM_Odm, RA, &Idx);

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] Start!\n", __func__));

	if (pBeamformEntry == NULL)
		return FALSE;

	NDPTxRate = Beamforming_GetVHTNDPTxRate(pDM_Odm, pBeamformEntry->CompSteeringNumofBFer);
	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] NDPTxRate =%d\n", __func__, NDPTxRate));
	
	PlatformAcquireSpinLock(Adapter, RT_TX_SPINLOCK);

	if (MgntGetFWBuffer(pDefAdapter, &pTcb, &pBuf)) {
#if(DEV_BUS_TYPE != RT_PCI_INTERFACE)
		DescLen = Adapter->HWDescHeadLength - pHalData->USBALLDummyLength;
#endif
		BufAddr = pBuf->Buffer.VirtualAddress + DescLen;

		ConstructVHTNDPAPacket(
				pDM_Odm, 
				RA,
				AID,
				BufAddr, 
				&BufLen,
				BW
				);
		
		pTcb->PacketLength = BufLen + DescLen;

		pTcb->bTxEnableSwCalcDur = TRUE;
		
		pTcb->BWOfPacket = BW;

		if (phydm_actingDetermine(pDM_Odm, PhyDM_ACTING_AS_IBSS) || phydm_actingDetermine(pDM_Odm, PhyDM_ACTING_AS_AP))
			pTcb->G_ID = 63;

		pTcb->P_AID = pBeamformEntry->P_AID;
		pTcb->DataRate = NDPTxRate;	/*decide by Nr*/

		Adapter->HalFunc.CmdSendPacketHandler(Adapter, pTcb, pBuf, pTcb->PacketLength, DESC_PACKET_TYPE_NORMAL, FALSE);
	} else
		ret = FALSE;
	
	PlatformReleaseSpinLock(Adapter, RT_TX_SPINLOCK);	

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] End, ret=%d\n", __func__, ret));

	if (ret)
		RT_DISP_DATA(FBEAM, FBEAM_DATA, "", pBuf->Buffer.VirtualAddress, pTcb->PacketLength);

	return ret;
}



BOOLEAN
SendSWVHTNDPAPacket(
	IN	PVOID			pDM_VOID,
	IN	pu1Byte			RA,
	IN	u2Byte			AID,
	IN	CHANNEL_WIDTH	BW
	)
{
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PADAPTER				Adapter = pDM_Odm->Adapter;
	PRT_TCB					pTcb;
	PRT_TX_LOCAL_BUFFER		pBuf;
	BOOLEAN					ret = TRUE;
	u1Byte					Idx = 0, NDPTxRate = 0;
	PRT_BEAMFORMING_INFO	pBeamInfo = &(pDM_Odm->BeamformingInfo);
	PRT_BEAMFORMEE_ENTRY	pBeamformEntry = phydm_Beamforming_GetBFeeEntryByAddr(pDM_Odm, RA, &Idx);

	NDPTxRate = Beamforming_GetVHTNDPTxRate(pDM_Odm, pBeamformEntry->CompSteeringNumofBFer);
	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] NDPTxRate =%d\n", __func__, NDPTxRate));

	PlatformAcquireSpinLock(Adapter, RT_TX_SPINLOCK);

	if (MgntGetBuffer(Adapter, &pTcb, &pBuf)) {
		ConstructVHTNDPAPacket(
				pDM_Odm, 
				RA,
				AID,
				pBuf->Buffer.VirtualAddress, 
				&pTcb->PacketLength,
				BW
				);

		pTcb->bTxEnableSwCalcDur = TRUE;
		pTcb->BWOfPacket = BW;

		/*rate of NDP decide by Nr*/
		MgntSendPacket(Adapter, pTcb, pBuf, pTcb->PacketLength, NORMAL_QUEUE, NDPTxRate);
	} else
		ret = FALSE;
	
	PlatformReleaseSpinLock(Adapter, RT_TX_SPINLOCK);	

	if (ret)
		RT_DISP_DATA(FBEAM, FBEAM_DATA, "", pBuf->Buffer.VirtualAddress, pTcb->PacketLength);

	return ret;
}

#ifdef SUPPORT_MU_BF
#if (SUPPORT_MU_BF == 1)
/*
// Description: On VHT GID management frame by an MU beamformee.
//
// 2015.05.20. Created by tynli.
*/
RT_STATUS
Beamforming_GetVHTGIDMgntFrame(
	IN	PADAPTER		Adapter,
	IN	PRT_RFD			pRfd,
	IN	POCTET_STRING	pPduOS
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	RT_STATUS		rtStatus = RT_STATUS_SUCCESS;
	pu1Byte			pBuffer = NULL;
	pu1Byte			pRaddr = NULL;
	u1Byte			MemStatus[8] = {0}, UserPos[16] = {0};
	u1Byte			idx;
	PRT_BEAMFORMING_INFO	pBeamInfo = &(pDM_Odm->BeamformingInfo);
	PRT_BEAMFORMER_ENTRY	pBeamformEntry = &pBeamInfo->BeamformerEntry[pBeamInfo->mu_ap_index];

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] On VHT GID mgnt frame!\n", __func__));		

	/* Check length*/
	if (pPduOS->Length < (FRAME_OFFSET_VHT_GID_MGNT_USER_POSITION_ARRAY+16)) {	
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("Beamforming_GetVHTGIDMgntFrame(): Invalid length (%d)\n", pPduOS->Length));
		return RT_STATUS_INVALID_LENGTH;
	}

	/* Check RA*/
	pRaddr = (pu1Byte)(pPduOS->Octet)+4;
	if (!eqMacAddr(pRaddr, Adapter->CurrentAddress)) {		
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("Beamforming_GetVHTGIDMgntFrame(): Drop because of RA error.\n"));
		return RT_STATUS_PKT_DROP;
	}

	RT_DISP_DATA(FBEAM, FBEAM_DATA, "On VHT GID Mgnt Frame ==>:\n", pPduOS->Octet, pPduOS->Length);

	/*Parsing Membership Status Array*/
	pBuffer = pPduOS->Octet + FRAME_OFFSET_VHT_GID_MGNT_MEMBERSHIP_STATUS_ARRAY;
	for (idx = 0; idx < 8; idx++) {
		MemStatus[idx] = GET_VHT_GID_MGNT_INFO_MEMBERSHIP_STATUS(pBuffer+idx);
		pBeamformEntry->gid_valid[idx] = GET_VHT_GID_MGNT_INFO_MEMBERSHIP_STATUS(pBuffer+idx);
	}

	RT_DISP_DATA(FBEAM, FBEAM_DATA, "MemStatus: ", MemStatus, 8);

	/* Parsing User Position Array*/
	pBuffer = pPduOS->Octet + FRAME_OFFSET_VHT_GID_MGNT_USER_POSITION_ARRAY;
	for (idx = 0; idx < 16; idx++) {
		UserPos[idx] = GET_VHT_GID_MGNT_INFO_USER_POSITION(pBuffer+idx);
		pBeamformEntry->user_position[idx] = GET_VHT_GID_MGNT_INFO_USER_POSITION(pBuffer+idx);
	}

	RT_DISP_DATA(FBEAM, FBEAM_DATA, "UserPos: ", UserPos, 16);

	/* Group ID detail printed*/
	{
		u1Byte	i, j;
		u1Byte	tmpVal;
		u2Byte	tmpVal2;

		for (i = 0; i < 8; i++) {
			tmpVal = MemStatus[i];
			tmpVal2 = ((UserPos[i*2 + 1] << 8) & 0xFF00) + (UserPos[i * 2] & 0xFF);
			for (j = 0; j < 8; j++) {
				if ((tmpVal >> j) & BIT0) {
					ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("Use Group ID (%d), User Position (%d)\n",
						(i*8+j), (tmpVal2 >> 2 * j)&0x3));
				}
			}
		}
	}

	/* Indicate GID frame to IHV service. */
	{
		u1Byte	Indibuffer[24] = {0};
		u1Byte	Indioffset = 0;
			
		PlatformMoveMemory(Indibuffer + Indioffset, pBeamformEntry->gid_valid, 8);
		Indioffset += 8;
		PlatformMoveMemory(Indibuffer + Indioffset, pBeamformEntry->user_position, 16);
		Indioffset += 16;

		PlatformIndicateCustomStatus(
			Adapter,
			RT_CUSTOM_EVENT_VHT_RECV_GID_MGNT_FRAME,
			RT_CUSTOM_INDI_TARGET_IHV,
			Indibuffer,
			Indioffset);
	}

	/* Config HW GID table */
	halComTxbf_ConfigGtab(pDM_Odm);

	return rtStatus;
}

/*
// Description: Construct VHT Group ID (GID) management frame.
//
// 2015.05.20. Created by tynli.
*/
VOID
ConstructVHTGIDMgntFrame(
	IN	PDM_ODM_T		pDM_Odm,
	IN	pu1Byte			RA,
	IN	PRT_BEAMFORMEE_ENTRY	pBeamformEntry,
	OUT	pu1Byte			Buffer,
	OUT	pu4Byte			pLength
	
)
{
	PRT_BEAMFORMING_INFO	pBeamInfo = &(pDM_Odm->BeamformingInfo);
	PADAPTER				Adapter = pBeamInfo->SourceAdapter;
	OCTET_STRING		osFTMFrame, tmp;

	FillOctetString(osFTMFrame, Buffer, 0);
	*pLength = 0;

	ConstructMaFrameHdr(
					Adapter, 
					RA, 
					ACT_CAT_VHT, 
					ACT_VHT_GROUPID_MANAGEMENT, 
					&osFTMFrame);

	/* Membership Status Array*/
	FillOctetString(tmp, pBeamformEntry->gid_valid, 8);
	PacketAppendData(&osFTMFrame, tmp);

	/* User Position Array*/
	FillOctetString(tmp, pBeamformEntry->user_position, 16);
	PacketAppendData(&osFTMFrame, tmp);

	*pLength = osFTMFrame.Length;

	RT_DISP_DATA(FBEAM, FBEAM_DATA, "ConstructVHTGIDMgntFrame():\n", Buffer, *pLength);
}

BOOLEAN
SendSWVHTGIDMgntFrame(
	IN	PVOID			pDM_VOID,
	IN	pu1Byte			RA,
	IN	u1Byte			Idx
	)
{
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PRT_TCB					pTcb;
	PRT_TX_LOCAL_BUFFER		pBuf;
	BOOLEAN					ret = TRUE;
	u1Byte					DataRate = 0;
	PRT_BEAMFORMING_INFO	pBeamInfo = &(pDM_Odm->BeamformingInfo);
	PRT_BEAMFORMEE_ENTRY	pBeamformEntry = &pBeamInfo->BeamformeeEntry[Idx];
	PADAPTER				Adapter = pBeamInfo->SourceAdapter;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] Start!\n", __func__));
	
	PlatformAcquireSpinLock(Adapter, RT_TX_SPINLOCK);

	if (MgntGetBuffer(Adapter, &pTcb, &pBuf)) {
		ConstructVHTGIDMgntFrame(
				pDM_Odm, 
				RA,
				pBeamformEntry,
				pBuf->Buffer.VirtualAddress, 
				&pTcb->PacketLength
				);

		pTcb->BWOfPacket = CHANNEL_WIDTH_20;
		DataRate = MGN_6M;
		MgntSendPacket(Adapter, pTcb, pBuf, pTcb->PacketLength, NORMAL_QUEUE, DataRate);
	} else
		ret = FALSE;
	
	PlatformReleaseSpinLock(Adapter, RT_TX_SPINLOCK);

	if (ret)
		RT_DISP_DATA(FBEAM, FBEAM_DATA, "", pBuf->Buffer.VirtualAddress, pTcb->PacketLength);

	return ret;
}


/*
// Description: Construct VHT beamforming report poll.
//
// 2015.05.20. Created by tynli.
*/
VOID
ConstructVHTBFReportPoll(
	IN	PDM_ODM_T		pDM_Odm,
	IN	pu1Byte			RA,
	OUT	pu1Byte			Buffer,
	OUT	pu4Byte			pLength
)
{
	PRT_BEAMFORMING_INFO	pBeamInfo = &(pDM_Odm->BeamformingInfo);
	PADAPTER				Adapter = pBeamInfo->SourceAdapter;
	pu1Byte			pBFRptPoll = Buffer;
	
	/* Frame control*/
	SET_80211_HDR_FRAME_CONTROL(pBFRptPoll, 0);
	SET_80211_HDR_TYPE_AND_SUBTYPE(pBFRptPoll, Type_Beamforming_Report_Poll);

	/* Duration*/	
	SET_80211_HDR_DURATION(pBFRptPoll, 100);

	/* RA*/
	SET_VHT_BF_REPORT_POLL_RA(pBFRptPoll, RA);

	/* TA*/
	SET_VHT_BF_REPORT_POLL_TA(pBFRptPoll, Adapter->CurrentAddress);

	/* Feedback Segment Retransmission Bitmap*/
	SET_VHT_BF_REPORT_POLL_FEEDBACK_SEG_RETRAN_BITMAP(pBFRptPoll, 0xFF);

	*pLength = 17;

	RT_DISP_DATA(FBEAM, FBEAM_DATA, "ConstructVHTBFReportPoll():\n", Buffer, *pLength);

}

BOOLEAN
SendSWVHTBFReportPoll(
	IN	PVOID			pDM_VOID,
	IN	pu1Byte			RA,
	IN	BOOLEAN			bFinalPoll
	)
{
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PRT_TCB					pTcb;
	PRT_TX_LOCAL_BUFFER		pBuf;
	BOOLEAN					ret = TRUE;
	u1Byte					Idx = 0, DataRate = 0;
	PRT_BEAMFORMING_INFO	pBeamInfo = &(pDM_Odm->BeamformingInfo);
	PRT_BEAMFORMEE_ENTRY	pBeamformEntry = phydm_Beamforming_GetBFeeEntryByAddr(pDM_Odm, RA, &Idx);
	PADAPTER				Adapter = pBeamInfo->SourceAdapter;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] Start!\n", __func__));

	PlatformAcquireSpinLock(Adapter, RT_TX_SPINLOCK);

	if (MgntGetBuffer(Adapter, &pTcb, &pBuf)) {
		ConstructVHTBFReportPoll(
				pDM_Odm, 
				RA,
				pBuf->Buffer.VirtualAddress, 
				&pTcb->PacketLength
				);

		pTcb->bTxEnableSwCalcDur = TRUE; /* <tynli_note> need?*/
		pTcb->BWOfPacket = CHANNEL_WIDTH_20;

		if (bFinalPoll)
			pTcb->TxBFPktType = RT_BF_PKT_TYPE_FINAL_BF_REPORT_POLL;
		else
			pTcb->TxBFPktType = RT_BF_PKT_TYPE_BF_REPORT_POLL;
		
		DataRate = MGN_6M;	/* Legacy OFDM rate*/
		MgntSendPacket(Adapter, pTcb, pBuf, pTcb->PacketLength, NORMAL_QUEUE, DataRate);
	} else
		ret = FALSE;
	
	PlatformReleaseSpinLock(Adapter, RT_TX_SPINLOCK);

	if (ret)
		RT_DISP_DATA(FBEAM, FBEAM_DATA, "SendSWVHTBFReportPoll():\n", pBuf->Buffer.VirtualAddress, pTcb->PacketLength);

	return ret;

}


/*
// Description: Construct VHT MU NDPA packet.
//	<Note> We should combine this function with ConstructVHTNDPAPacket() in the future.
//
// 2015.05.21. Created by tynli.
*/
VOID
ConstructVHTMUNDPAPacket(
	IN PDM_ODM_T		pDM_Odm,
	IN CHANNEL_WIDTH	BW,
	OUT pu1Byte			Buffer,
	OUT pu4Byte			pLength
	)
{	
	PRT_BEAMFORMING_INFO	pBeamInfo = &(pDM_Odm->BeamformingInfo);
	PADAPTER				Adapter = pBeamInfo->SourceAdapter;
	u2Byte					Duration = 0;
	u1Byte					Sequence = 0;
	pu1Byte					pNDPAFrame = Buffer;
	RT_NDPA_STA_INFO		STAInfo;
	u1Byte					idx;
	u1Byte					DestAddr[6] = {0};
	PRT_BEAMFORMEE_ENTRY	pEntry = NULL;

	/* Fill the first MU BFee entry (STA1) MAC addr to destination address then
	     HW will change A1 to broadcast addr. 2015.05.28. Suggested by SD1 Chunchu. */
	for (idx = 0; idx < BEAMFORMEE_ENTRY_NUM; idx++) {		
		pEntry = &(pBeamInfo->BeamformeeEntry[idx]);
		if (pEntry->is_mu_sta) {
			cpMacAddr(DestAddr, pEntry->MacAddr);
			break;
		}
	}
	if (pEntry == NULL)
		return;
	
	/* Frame control.*/
	SET_80211_HDR_FRAME_CONTROL(pNDPAFrame, 0);
	SET_80211_HDR_TYPE_AND_SUBTYPE(pNDPAFrame, Type_NDPA);

	SET_80211_HDR_ADDRESS1(pNDPAFrame, DestAddr);
	SET_80211_HDR_ADDRESS2(pNDPAFrame, pEntry->MyMacAddr);

	/*--------------------------------------------*/
	/* <Note> Need to modify "Duration" to MU consideration. */
	Duration = 2*aSifsTime + 44;
	
	if (BW == CHANNEL_WIDTH_80)
		Duration += 40;
	else if(BW == CHANNEL_WIDTH_40)
		Duration+= 87;
	else	
		Duration+= 180;
	/*--------------------------------------------*/

	SET_80211_HDR_DURATION(pNDPAFrame, Duration);

	Sequence = *(pDM_Odm->pSoundingSeq) << 2;
	ODM_MoveMemory(pDM_Odm, pNDPAFrame + 16, &Sequence, 1);

	*pLength = 17;

	/* Construct STA info. for multiple STAs*/
	for (idx = 0; idx < BEAMFORMEE_ENTRY_NUM; idx++) {		
		pEntry = &(pBeamInfo->BeamformeeEntry[idx]);
		if (pEntry->is_mu_sta) {
			STAInfo.AID = pEntry->AID;
			STAInfo.FeedbackType = 1; /* 1'b1: MU*/
			STAInfo.NcIndex = 0;

			ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] Get BeamformeeEntry idx(%d), AID =%d\n", __func__, idx, pEntry->AID));
			
			ODM_MoveMemory(pDM_Odm, pNDPAFrame+(*pLength), (pu1Byte)&STAInfo, 2);
			*pLength += 2;
		}
	}

}

BOOLEAN
SendSWVHTMUNDPAPacket(
	IN	PVOID			pDM_VOID,
	IN	CHANNEL_WIDTH	BW
	)
{
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PRT_TCB					pTcb;
	PRT_TX_LOCAL_BUFFER		pBuf;
	BOOLEAN					ret = TRUE;
	u1Byte					NDPTxRate = 0;
	PRT_BEAMFORMING_INFO	pBeamInfo = &(pDM_Odm->BeamformingInfo);
	PADAPTER				Adapter = pBeamInfo->SourceAdapter;

	NDPTxRate = MGN_VHT2SS_MCS0;
	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] NDPTxRate =%d\n", __func__, NDPTxRate));

	PlatformAcquireSpinLock(Adapter, RT_TX_SPINLOCK);

	if (MgntGetBuffer(Adapter, &pTcb, &pBuf)) {
		ConstructVHTMUNDPAPacket(
				pDM_Odm,
				BW,
				pBuf->Buffer.VirtualAddress, 
				&pTcb->PacketLength
				);

		pTcb->bTxEnableSwCalcDur = TRUE;
		pTcb->BWOfPacket = BW;
		pTcb->TxBFPktType = RT_BF_PKT_TYPE_BROADCAST_NDPA;

		/*rate of NDP decide by Nr*/
		MgntSendPacket(Adapter, pTcb, pBuf, pTcb->PacketLength, NORMAL_QUEUE, NDPTxRate);
	} else
		ret = FALSE;
	
	PlatformReleaseSpinLock(Adapter, RT_TX_SPINLOCK);	

	if (ret)
		RT_DISP_DATA(FBEAM, FBEAM_DATA, "", pBuf->Buffer.VirtualAddress, pTcb->PacketLength);

	return ret;
}


VOID
DBG_ConstructVHTMUNDPAPacket(
	IN PDM_ODM_T		pDM_Odm,
	IN CHANNEL_WIDTH	BW,
	OUT pu1Byte			Buffer,
	OUT pu4Byte			pLength
	)
{	
	PRT_BEAMFORMING_INFO	pBeamInfo = &(pDM_Odm->BeamformingInfo);
	PADAPTER				Adapter = pBeamInfo->SourceAdapter;
	u2Byte					Duration = 0;
	u1Byte					Sequence = 0;
	pu1Byte					pNDPAFrame = Buffer;
	RT_NDPA_STA_INFO		STAInfo;
	u1Byte					idx;
	u1Byte					DestAddr[6] = {0};
	PRT_BEAMFORMEE_ENTRY	pEntry = NULL;

	BOOLEAN	is_STA1 = FALSE;


	/* Fill the first MU BFee entry (STA1) MAC addr to destination address then
	     HW will change A1 to broadcast addr. 2015.05.28. Suggested by SD1 Chunchu. */
	for (idx = 0; idx < BEAMFORMEE_ENTRY_NUM; idx++) {		
		pEntry = &(pBeamInfo->BeamformeeEntry[idx]);
		if (pEntry->is_mu_sta) {
			if (is_STA1 == FALSE) {
				is_STA1 = TRUE;
				continue;
			} else {
				cpMacAddr(DestAddr, pEntry->MacAddr);
				break;
			}
		}
	}

	/* Frame control.*/
	SET_80211_HDR_FRAME_CONTROL(pNDPAFrame, 0);
	SET_80211_HDR_TYPE_AND_SUBTYPE(pNDPAFrame, Type_NDPA);

	SET_80211_HDR_ADDRESS1(pNDPAFrame, DestAddr);
	SET_80211_HDR_ADDRESS2(pNDPAFrame, pDM_Odm->CurrentAddress);

	/*--------------------------------------------*/
	/* <Note> Need to modify "Duration" to MU consideration. */
	Duration = 2*aSifsTime + 44;
	
	if (BW == CHANNEL_WIDTH_80)
		Duration += 40;
	else if (BW == CHANNEL_WIDTH_40)
		Duration += 87;
	else	
		Duration += 180;
	/*--------------------------------------------*/

	SET_80211_HDR_DURATION(pNDPAFrame, Duration);

	Sequence = *(pDM_Odm->pSoundingSeq) << 2;
	ODM_MoveMemory(pDM_Odm, pNDPAFrame + 16, &Sequence, 1);

	*pLength = 17;

	/*STA2's STA Info*/
	STAInfo.AID = pEntry->AID;
	STAInfo.FeedbackType = 1; /* 1'b1: MU */
	STAInfo.NcIndex = 0;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] Get BeamformeeEntry idx(%d), AID =%d\n", __func__, idx, pEntry->AID));
	
	ODM_MoveMemory(pDM_Odm, pNDPAFrame+(*pLength), (pu1Byte)&STAInfo, 2);
	*pLength += 2;

}

BOOLEAN
DBG_SendSWVHTMUNDPAPacket(
	IN	PVOID			pDM_VOID,
	IN	CHANNEL_WIDTH	BW
	)
{
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PRT_TCB					pTcb;
	PRT_TX_LOCAL_BUFFER		pBuf;
	BOOLEAN					ret = TRUE;
	u1Byte					NDPTxRate = 0;
	PRT_BEAMFORMING_INFO	pBeamInfo = &(pDM_Odm->BeamformingInfo);
	PADAPTER				Adapter = pBeamInfo->SourceAdapter;

	NDPTxRate = MGN_VHT2SS_MCS0;
	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] NDPTxRate =%d\n", __func__, NDPTxRate));

	PlatformAcquireSpinLock(Adapter, RT_TX_SPINLOCK);

	if (MgntGetBuffer(Adapter, &pTcb, &pBuf)) {
		DBG_ConstructVHTMUNDPAPacket(
				pDM_Odm,
				BW,
				pBuf->Buffer.VirtualAddress, 
				&pTcb->PacketLength
				);

		pTcb->bTxEnableSwCalcDur = TRUE;
		pTcb->BWOfPacket = BW;
		pTcb->TxBFPktType = RT_BF_PKT_TYPE_UNICAST_NDPA;

		/*rate of NDP decide by Nr*/
		MgntSendPacket(Adapter, pTcb, pBuf, pTcb->PacketLength, NORMAL_QUEUE, NDPTxRate);
	} else
		ret = FALSE;
	
	PlatformReleaseSpinLock(Adapter, RT_TX_SPINLOCK);	

	if (ret)
		RT_DISP_DATA(FBEAM, FBEAM_DATA, "", pBuf->Buffer.VirtualAddress, pTcb->PacketLength);

	return ret;
}


#endif	/*#if (SUPPORT_MU_BF == 1)*/
#endif	/*#ifdef SUPPORT_MU_BF*/


#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)

u4Byte
Beamforming_GetReportFrame(
	IN	PVOID			pDM_VOID,
	union recv_frame *precv_frame
	)
{
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u4Byte					ret = _SUCCESS;
	PRT_BEAMFORMEE_ENTRY	pBeamformEntry = NULL;
	pu1Byte					pframe = precv_frame->u.hdr.rx_data;
	u4Byte					frame_len = precv_frame->u.hdr.len;
	pu1Byte					TA;
	u1Byte					Idx, offset;
	

	/*Memory comparison to see if CSI report is the same with previous one*/
	TA = GetAddr2Ptr(pframe);
	pBeamformEntry = phydm_Beamforming_GetBFeeEntryByAddr(pDM_Odm, TA, &Idx);
	if(pBeamformEntry->BeamformEntryCap & BEAMFORMER_CAP_VHT_SU)
		offset = 31;		/*24+(1+1+3)+2  MAC header+(Category+ActionCode+MIMOControlField)+SNR(Nc=2)*/
	else if(pBeamformEntry->BeamformEntryCap & BEAMFORMER_CAP_HT_EXPLICIT)
		offset = 34;		/*24+(1+1+6)+2  MAC header+(Category+ActionCode+MIMOControlField)+SNR(Nc=2)*/
	else
		return ret;

	
	return ret;
}


BOOLEAN
SendFWHTNDPAPacket(
	IN	PVOID			pDM_VOID,
	IN	pu1Byte			RA,
	IN	CHANNEL_WIDTH	BW
	)
{
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PADAPTER				Adapter = pDM_Odm->Adapter;
	struct xmit_frame		*pmgntframe;
	struct pkt_attrib		*pattrib;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	struct xmit_priv		*pxmitpriv = &(Adapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u1Byte	ActionHdr[4] = {ACT_CAT_VENDOR, 0x00, 0xe0, 0x4c};
	u1Byte	*pframe;
	u2Byte	*fctrl;
	u2Byte	duration = 0;
	u1Byte	aSifsTime = 0, NDPTxRate = 0, Idx = 0;
	PRT_BEAMFORMING_INFO	pBeamInfo = &(pDM_Odm->BeamformingInfo);
	PRT_BEAMFORMEE_ENTRY	pBeamformEntry = phydm_Beamforming_GetBFeeEntryByAddr(pDM_Odm, RA, &Idx);

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	
	if (pmgntframe == NULL) {
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s, alloc mgnt frame fail\n", __func__));
		return _FALSE;
	}

	//update attribute
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(Adapter, pattrib);

	pattrib->qsel = QSLT_BEACON;
	NDPTxRate = Beamforming_GetHTNDPTxRate(pDM_Odm, pBeamformEntry->CompSteeringNumofBFer);
	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] NDPTxRate =%d\n", __func__, NDPTxRate));
	pattrib->rate = NDPTxRate;
	pattrib->bwmode = BW;
	pattrib->order = 1;
	pattrib->subtype = WIFI_ACTION_NOACK;

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;

	pwlanhdr = (struct rtw_ieee80211_hdr*)pframe;

	fctrl = &pwlanhdr->frame_ctl;
	*(fctrl) = 0;

	SetOrderBit(pframe);
	SetFrameSubType(pframe, WIFI_ACTION_NOACK);

	_rtw_memcpy(pwlanhdr->addr1, RA, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, pBeamformEntry->MyMacAddr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	if( pmlmeext->cur_wireless_mode == WIRELESS_11B)
		aSifsTime = 10;
	else
		aSifsTime = 16;

	duration = 2*aSifsTime + 40;
	
	if(BW == CHANNEL_WIDTH_40)
		duration+= 87;
	else	
		duration+= 180;

	SetDuration(pframe, duration);

	//HT control field
	SET_HT_CTRL_CSI_STEERING(pframe+24, 3);
	SET_HT_CTRL_NDP_ANNOUNCEMENT(pframe+24, 1);

	_rtw_memcpy(pframe+28, ActionHdr, 4);

	pattrib->pktlen = 32;

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(Adapter, pmgntframe);

	return _TRUE;
}


BOOLEAN
SendSWHTNDPAPacket(
	IN	PVOID			pDM_VOID,
	IN	pu1Byte			RA,
	IN	CHANNEL_WIDTH	BW
	)
{
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PADAPTER				Adapter = pDM_Odm->Adapter;
	struct xmit_frame		*pmgntframe;
	struct pkt_attrib		*pattrib;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	struct xmit_priv		*pxmitpriv = &(Adapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u1Byte	ActionHdr[4] = {ACT_CAT_VENDOR, 0x00, 0xe0, 0x4c};
	pu1Byte	pframe;
	pu2Byte	fctrl;
	u2Byte	duration = 0;
	u1Byte	aSifsTime = 0, NDPTxRate = 0, Idx = 0;
	PRT_BEAMFORMING_INFO	pBeamInfo = &(pDM_Odm->BeamformingInfo);
	PRT_BEAMFORMEE_ENTRY	pBeamformEntry = phydm_Beamforming_GetBFeeEntryByAddr(pDM_Odm, RA, &Idx);

	NDPTxRate = Beamforming_GetHTNDPTxRate(pDM_Odm, pBeamformEntry->CompSteeringNumofBFer);
	
	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	
	if (pmgntframe == NULL) {
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s, alloc mgnt frame fail\n", __func__));
		return _FALSE;
	}

	/*update attribute*/
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(Adapter, pattrib);
	pattrib->qsel = QSLT_MGNT;
	pattrib->rate = NDPTxRate;
	pattrib->bwmode = BW;
	pattrib->order = 1;
	pattrib->subtype = WIFI_ACTION_NOACK;

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;

	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_ctl;
	*(fctrl) = 0;

	SetOrderBit(pframe);
	SetFrameSubType(pframe, WIFI_ACTION_NOACK);

	_rtw_memcpy(pwlanhdr->addr1, RA, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, pBeamformEntry->MyMacAddr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	if (pmlmeext->cur_wireless_mode == WIRELESS_11B)
		aSifsTime = 10;
	else
		aSifsTime = 16;

	duration = 2*aSifsTime + 40;
	
	if (BW == CHANNEL_WIDTH_40)
		duration += 87;
	else	
		duration += 180;

	SetDuration(pframe, duration);

	/*HT control field*/
	SET_HT_CTRL_CSI_STEERING(pframe+24, 3);
	SET_HT_CTRL_NDP_ANNOUNCEMENT(pframe+24, 1);

	_rtw_memcpy(pframe+28, ActionHdr, 4);

	pattrib->pktlen = 32;

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(Adapter, pmgntframe);

	return _TRUE;
}


BOOLEAN
SendFWVHTNDPAPacket(
	IN	PVOID			pDM_VOID,
	IN	pu1Byte			RA,
	IN	u2Byte			AID,
	IN	CHANNEL_WIDTH	BW
	)
{
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PADAPTER				Adapter = pDM_Odm->Adapter;
	struct xmit_frame		*pmgntframe;
	struct pkt_attrib		*pattrib;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	struct xmit_priv		*pxmitpriv = &(Adapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct mlme_priv		*pmlmepriv = &(Adapter->mlmepriv);
	pu1Byte	pframe;
	pu2Byte	fctrl;
	u2Byte	duration = 0;
	u1Byte	sequence = 0, aSifsTime = 0, NDPTxRate= 0, Idx = 0;
	PRT_BEAMFORMING_INFO	pBeamInfo = &(pDM_Odm->BeamformingInfo);
	PRT_BEAMFORMEE_ENTRY	pBeamformEntry = phydm_Beamforming_GetBFeeEntryByAddr(pDM_Odm, RA, &Idx);
	RT_NDPA_STA_INFO	sta_info;

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	
	if (pmgntframe == NULL) {
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s, alloc mgnt frame fail\n", __func__));
		return _FALSE;
	}

	//update attribute
	pattrib = &pmgntframe->attrib;
	_rtw_memcpy(pattrib->ra, RA, ETH_ALEN);
	update_mgntframe_attrib(Adapter, pattrib);

	pattrib->qsel = QSLT_BEACON;
	NDPTxRate = Beamforming_GetVHTNDPTxRate(pDM_Odm, pBeamformEntry->CompSteeringNumofBFer);
	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] NDPTxRate =%d\n", __func__, NDPTxRate));
	pattrib->rate = NDPTxRate;
	pattrib->bwmode = BW;
	pattrib->subtype = WIFI_NDPA;

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;

	pwlanhdr = (struct rtw_ieee80211_hdr*)pframe;

	fctrl = &pwlanhdr->frame_ctl;
	*(fctrl) = 0;

	SetFrameSubType(pframe, WIFI_NDPA);

	_rtw_memcpy(pwlanhdr->addr1, RA, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, pBeamformEntry->MyMacAddr, ETH_ALEN);

	if (IsSupported5G(pmlmeext->cur_wireless_mode) || IsSupportedHT(pmlmeext->cur_wireless_mode))
		aSifsTime = 16;
	else
		aSifsTime = 10;

	duration = 2*aSifsTime + 44;
	
	if(BW == CHANNEL_WIDTH_80)
		duration += 40;
	else if(BW == CHANNEL_WIDTH_40)
		duration+= 87;
	else	
		duration+= 180;

	SetDuration(pframe, duration);

	sequence = pBeamInfo->SoundingSequence<< 2;
	if (pBeamInfo->SoundingSequence >= 0x3f)
		pBeamInfo->SoundingSequence = 0;
	else
		pBeamInfo->SoundingSequence++;

	_rtw_memcpy(pframe+16, &sequence,1);

	if (((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) || ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE))
		AID = 0;		

	sta_info.AID = AID;
	sta_info.FeedbackType = 0;
	sta_info.NcIndex= 0;
	
	_rtw_memcpy(pframe+17, (u8 *)&sta_info, 2);

	pattrib->pktlen = 19;

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(Adapter, pmgntframe);

	return _TRUE;
}



BOOLEAN
SendSWVHTNDPAPacket(
	IN	PVOID			pDM_VOID,
	IN	pu1Byte			RA,
	IN	u2Byte			AID,
	IN	CHANNEL_WIDTH	BW
	)
{
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PADAPTER				Adapter = pDM_Odm->Adapter;
	struct xmit_frame		*pmgntframe;
	struct pkt_attrib		*pattrib;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	struct xmit_priv		*pxmitpriv = &(Adapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct mlme_priv		*pmlmepriv = &(Adapter->mlmepriv);
	RT_NDPA_STA_INFO	ndpa_sta_info;
	u1Byte	NDPTxRate = 0, sequence = 0, aSifsTime = 0, Idx = 0;
	pu1Byte	pframe;
	pu2Byte	fctrl;
	u2Byte	duration = 0;
	PRT_BEAMFORMING_INFO	pBeamInfo = &(pDM_Odm->BeamformingInfo);
	PRT_BEAMFORMEE_ENTRY	pBeamformEntry = phydm_Beamforming_GetBFeeEntryByAddr(pDM_Odm, RA, &Idx);

	NDPTxRate = Beamforming_GetVHTNDPTxRate(pDM_Odm, pBeamformEntry->CompSteeringNumofBFer);
	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] NDPTxRate =%d\n", __func__, NDPTxRate));

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	
	if (pmgntframe == NULL) {
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s, alloc mgnt frame fail\n", __func__));
		return _FALSE;
	}
	
	/*update attribute*/
	pattrib = &pmgntframe->attrib;
	_rtw_memcpy(pattrib->ra, RA, ETH_ALEN);
	update_mgntframe_attrib(Adapter, pattrib);
	pattrib->qsel = QSLT_MGNT;
	pattrib->rate = NDPTxRate;
	pattrib->bwmode = BW;
	pattrib->subtype = WIFI_NDPA;

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;

	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_ctl;
	*(fctrl) = 0;

	SetFrameSubType(pframe, WIFI_NDPA);

	_rtw_memcpy(pwlanhdr->addr1, RA, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, pBeamformEntry->MyMacAddr, ETH_ALEN);

	if (IsSupported5G(pmlmeext->cur_wireless_mode) || IsSupportedHT(pmlmeext->cur_wireless_mode))
		aSifsTime = 16;
	else
		aSifsTime = 10;

	duration = 2*aSifsTime + 44;
	
	if (BW == CHANNEL_WIDTH_80)
		duration += 40;
	else if (BW == CHANNEL_WIDTH_40)
		duration += 87;
	else	
		duration += 180;

	SetDuration(pframe, duration);
	
	sequence = pBeamInfo->SoundingSequence << 2;
	if (pBeamInfo->SoundingSequence >= 0x3f)
		pBeamInfo->SoundingSequence = 0;
	else
		pBeamInfo->SoundingSequence++;

	_rtw_memcpy(pframe+16, &sequence, 1);
	if (((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) || ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE))
		AID = 0;		

	ndpa_sta_info.AID = AID;
	ndpa_sta_info.FeedbackType = 0;
	ndpa_sta_info.NcIndex = 0;
	
	_rtw_memcpy(pframe+17, (u8 *)&ndpa_sta_info, 2);

	pattrib->pktlen = 19;

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(Adapter, pmgntframe);
	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] [%d]\n", __func__, __LINE__));
	
	return _TRUE;
}


#endif


VOID
Beamforming_GetNDPAFrame(
	IN	PVOID			pDM_VOID,
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	IN	OCTET_STRING	pduOS
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	union recv_frame *precv_frame
#endif
)
{
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PADAPTER					Adapter = pDM_Odm->Adapter;
	pu1Byte						TA ;
	u1Byte						Idx, Sequence;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	pu1Byte						pNDPAFrame = pduOS.Octet;
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	pu1Byte						pNDPAFrame = precv_frame->u.hdr.rx_data;
#endif
	PRT_BEAMFORMER_ENTRY		pBeamformerEntry = NULL;		/*Modified By Jeffery @2014-10-29*/
	

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
		RT_DISP_DATA(FBEAM, FBEAM_DATA, "Beamforming_GetNDPAFrame\n", pduOS.Octet, pduOS.Length);
	if (IsCtrlNDPA(pNDPAFrame) == FALSE)
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	if (GetFrameSubType(pNDPAFrame) != WIFI_NDPA)
#endif
		return;
	else if (!(pDM_Odm->SupportICType & (ODM_RTL8812 | ODM_RTL8821))) {
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] not 8812 or 8821A, return\n", __func__));
		return;
	}
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	TA = Frame_Addr2(pduOS);
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	TA = GetAddr2Ptr(pNDPAFrame);
#endif
	/*Remove signaling TA. */
	TA[0] = TA[0] & 0xFE;
    
	pBeamformerEntry = phydm_Beamforming_GetBFerEntryByAddr(pDM_Odm, TA, &Idx);		// Modified By Jeffery @2014-10-29

	/*Break options for Clock Reset*/    
	if (pBeamformerEntry == NULL)
		return;
	else if (!(pBeamformerEntry->BeamformEntryCap & BEAMFORMEE_CAP_VHT_SU))
		return;
	/*LogSuccess: As long as 8812A receive NDPA and feedback CSI succeed once, clock reset is NO LONGER needed !2015-04-10, Jeffery*/
	/*ClockResetTimes: While BFer entry always doesn't receive our CSI, clock will reset again and again.So ClockResetTimes is limited to 5 times.2015-04-13, Jeffery*/
	else if ((pBeamformerEntry->LogSuccess == 1) || (pBeamformerEntry->ClockResetTimes == 5)) {
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] LogSeq=%d, PreLogSeq=%d, LogRetryCnt=%d, LogSuccess=%d, ClockResetTimes=%d, clock reset is no longer needed.\n", 
			__func__, pBeamformerEntry->LogSeq, pBeamformerEntry->PreLogSeq, pBeamformerEntry->LogRetryCnt, pBeamformerEntry->LogSuccess, pBeamformerEntry->ClockResetTimes));

        return;
	}

	Sequence = (pNDPAFrame[16]) >> 2;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] Start, Sequence=%d, LogSeq=%d, PreLogSeq=%d, LogRetryCnt=%d, ClockResetTimes=%d, LogSuccess=%d\n", 
		__func__, Sequence, pBeamformerEntry->LogSeq, pBeamformerEntry->PreLogSeq, pBeamformerEntry->LogRetryCnt, pBeamformerEntry->ClockResetTimes, pBeamformerEntry->LogSuccess));

	if ((pBeamformerEntry->LogSeq != 0) && (pBeamformerEntry->PreLogSeq != 0)) {
		/*Success condition*/
		if ((pBeamformerEntry->LogSeq != Sequence) && (pBeamformerEntry->PreLogSeq != pBeamformerEntry->LogSeq)) {
			/* break option for clcok reset, 2015-03-30, Jeffery */
			pBeamformerEntry->LogRetryCnt = 0;
			/*As long as 8812A receive NDPA and feedback CSI succeed once, clock reset is no longer needed.*/
			/*That is, LogSuccess is NOT needed to be reset to zero, 2015-04-13, Jeffery*/
			pBeamformerEntry->LogSuccess = 1;

		} else {/*Fail condition*/

			if (pBeamformerEntry->LogRetryCnt == 5) {
				pBeamformerEntry->ClockResetTimes++;
				pBeamformerEntry->LogRetryCnt = 0;

			ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] Clock Reset!!! ClockResetTimes=%d\n", 
				__func__, pBeamformerEntry->ClockResetTimes));
			HalComTxbf_Set(pDM_Odm, TXBF_SET_SOUNDING_CLK, NULL);

			} else
				pBeamformerEntry->LogRetryCnt++;
		}
	}

	/*Update LogSeq & PreLogSeq*/
	pBeamformerEntry->PreLogSeq = pBeamformerEntry->LogSeq;
	pBeamformerEntry->LogSeq = Sequence;
	
}



#endif
