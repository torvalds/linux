/* SPDX-License-Identifier: GPL-2.0 */
#include "mp_precomp.h"
#include "phydm_precomp.h"

#if WPP_SOFTWARE_TRACE
#include "phydm_beamforming.tmh"
#endif

#if (BEAMFORMING_SUPPORT == 1)

u1Byte
Beamforming_GetHTNDPTxRate(
	IN	PADAPTER	Adapter,
	u1Byte	CompSteeringNumofBFer
)
{
	u1Byte Nr_index = 0;
	u1Byte NDPTxRate;
	/*Find Nr*/
	
	if(IS_HARDWARE_TYPE_8814A(Adapter))
		Nr_index = TxBF_Nr(halTxbf8814A_GetNtx(Adapter), CompSteeringNumofBFer);
	else
		Nr_index = TxBF_Nr(1, CompSteeringNumofBFer);
	
	switch(Nr_index)
	{
		case 1:
		NDPTxRate = MGN_MCS8;
		break;

		case 2:
		NDPTxRate = MGN_MCS16;
		break;

		case 3:
		NDPTxRate = MGN_MCS24;
		break;
			
		default:
		NDPTxRate = MGN_MCS8;
		break;
	
	}

return NDPTxRate;

}

u1Byte
Beamforming_GetVHTNDPTxRate(
	IN	PADAPTER	Adapter,
	u1Byte	CompSteeringNumofBFer
)
{
	u1Byte Nr_index = 0;
	u1Byte NDPTxRate;
	/*Find Nr*/
	if(IS_HARDWARE_TYPE_8814A(Adapter))
		Nr_index = TxBF_Nr(halTxbf8814A_GetNtx(Adapter), CompSteeringNumofBFer);
	else
		Nr_index = TxBF_Nr(1, CompSteeringNumofBFer);
	
	switch(Nr_index)
	{
		case 1:
		NDPTxRate = MGN_VHT2SS_MCS0;
		break;

		case 2:
		NDPTxRate = MGN_VHT3SS_MCS0;
		break;

		case 3:
		NDPTxRate = MGN_VHT4SS_MCS0;
		break;
			
		default:
		NDPTxRate = MGN_VHT2SS_MCS0;
		break;
	
	}

return NDPTxRate;

}


PRT_BEAMFORMING_ENTRY
phydm_Beamforming_GetBFeeEntryByAddr(
	IN	PVOID		pDM_VOID,
	IN	pu1Byte		RA,
	OUT	pu1Byte		Idx
	)
{
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte	i = 0;
	PRT_BEAMFORMING_INFO pBeamInfo = &pDM_Odm->BeamformingInfo;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s ===>\n", __FUNCTION__));
	
	for(i = 0; i < BEAMFORMEE_ENTRY_NUM; i++)
	{
		if(	pBeamInfo->BeamformeeEntry[i].bUsed && 
			(eqMacAddr(RA,pBeamInfo->BeamformeeEntry[i].MacAddr) ))
		{
			*Idx = i;
			return &(pBeamInfo->BeamformeeEntry[i]);
		}
	}

	return NULL;
}

PRT_BEAMFORMER_ENTRY
phydm_Beamforming_GetBFerEntryByAddr(
	IN	PVOID	pDM_VOID,
	IN	pu1Byte 	TA,
	OUT	pu1Byte		Idx
	)
{
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte	i = 0;
	PRT_BEAMFORMING_INFO pBeamInfo = &pDM_Odm->BeamformingInfo;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s ===>\n", __FUNCTION__));
	
	for(i = 0; i < BEAMFORMER_ENTRY_NUM; i++)
	{
		if(	pBeamInfo->BeamformerEntry[i].bUsed && 
			(eqMacAddr(TA,pBeamInfo->BeamformerEntry[i].MacAddr) ))
		{
			*Idx = i;
			return &(pBeamInfo->BeamformerEntry[i]);
		}
	}

	return NULL;
}


PRT_BEAMFORMING_ENTRY
phydm_Beamforming_GetEntryByMacId(
	IN	PVOID		pDM_VOID,
	IN	u1Byte		MacId,
	OUT	pu1Byte		Idx
	)
{
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte	i = 0;
	PRT_BEAMFORMING_INFO pBeamInfo = &pDM_Odm->BeamformingInfo;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s ===>\n", __FUNCTION__));
	
	for(i = 0; i < BEAMFORMEE_ENTRY_NUM; i++)
	{
		if(	pBeamInfo->BeamformeeEntry[i].bUsed && 
			(MacId == pBeamInfo->BeamformeeEntry[i].MacId))
		{
			*Idx = i;
			return &(pBeamInfo->BeamformeeEntry[i]);
		}
	}

	return NULL;
}


BEAMFORMING_CAP
phydm_Beamforming_GetEntryBeamCapByMacId(
	IN	PVOID		pDM_VOID,
	IN	u1Byte		MacId
	)
{
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte	i = 0;
	PRT_BEAMFORMING_INFO	pBeamInfo = &pDM_Odm->BeamformingInfo;
	BEAMFORMING_CAP			BeamformEntryCap = BEAMFORMING_CAP_NONE;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s ===>\n", __FUNCTION__));
	
	for(i = 0; i < BEAMFORMEE_ENTRY_NUM; i++)
	{
		if(	pBeamInfo->BeamformeeEntry[i].bUsed && 
			(MacId == pBeamInfo->BeamformeeEntry[i].MacId))
		{
			BeamformEntryCap =  pBeamInfo->BeamformeeEntry[i].BeamformEntryCap;
			i = BEAMFORMEE_ENTRY_NUM;
		}
	}

	return BeamformEntryCap;
}


PRT_BEAMFORMING_ENTRY
phydm_Beamforming_GetFreeBFeeEntry(
	IN	PVOID		pDM_VOID,
	OUT	pu1Byte		Idx
	)
{
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte	i = 0;
	PRT_BEAMFORMING_INFO pBeamInfo = &pDM_Odm->BeamformingInfo;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s ===>\n", __FUNCTION__));

	for(i = 0; i < BEAMFORMEE_ENTRY_NUM; i++)
	{
		if(pBeamInfo->BeamformeeEntry[i].bUsed == FALSE)
		{
			*Idx = i;
			return &(pBeamInfo->BeamformeeEntry[i]);
		}	
	}
	return NULL;
}

PRT_BEAMFORMER_ENTRY
phydm_Beamforming_GetFreeBFerEntry(
	IN	PVOID		pDM_VOID,
	OUT	pu1Byte		Idx
	)
{
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte	i = 0;
	PRT_BEAMFORMING_INFO pBeamInfo = &pDM_Odm->BeamformingInfo;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s ===>\n", __FUNCTION__));

	for(i = 0; i < BEAMFORMER_ENTRY_NUM; i++)
	{
		if(pBeamInfo->BeamformerEntry[i].bUsed == FALSE)
		{
			*Idx = i;
			return &(pBeamInfo->BeamformerEntry[i]);
		}	
	}
	return NULL;
}


PRT_BEAMFORMING_ENTRY
Beamforming_AddBFeeEntry(
	IN	PADAPTER			Adapter,
	IN	pu1Byte				RA,
	IN	u2Byte				AID,
	IN	u2Byte				MacID,
	IN	CHANNEL_WIDTH		BW,
	IN	BEAMFORMING_CAP	BeamformCap,
	IN	u1Byte				NumofSoundingDim,
	IN	u1Byte				CompSteeringNumofBFer,
	OUT	pu1Byte				Idx
	)
{
	HAL_DATA_TYPE			*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T				pDM_Odm = &pHalData->DM_OutSrc;
	PMGNT_INFO				pMgntInfo = &(Adapter->MgntInfo);
	PRT_BEAMFORMING_ENTRY	pEntry = phydm_Beamforming_GetFreeBFeeEntry(pDM_Odm, Idx);

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s Start!\n", __FUNCTION__));

	if(pEntry != NULL)
	{	
		pEntry->bUsed = TRUE;
		pEntry->AID = AID;
		pEntry->MacId = MacID;
		pEntry->SoundBW = BW;
		if(ACTING_AS_AP(Adapter))
		{
			u2Byte BSSID = ((Adapter->CurrentAddress[5] & 0xf0) >> 4) ^ 
							(Adapter->CurrentAddress[5] & 0xf);	// BSSID[44:47] xor BSSID[40:43]
			pEntry->P_AID = (AID + BSSID * 32) & 0x1ff;		// (dec(A) + dec(B)*32) mod 512
			ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s: BFee P_AID addressed to STA=%d\n", __FUNCTION__,pEntry->P_AID));
		}		
		else if(ACTING_AS_IBSS(Adapter))	// Ad hoc mode
		{
			pEntry->P_AID = 0;
			ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s: BFee P_AID as IBSS=%d\n", __FUNCTION__,pEntry->P_AID));
		}
		else	// client mode
		{
			pEntry->P_AID =  RA[5];						// BSSID[39:47]
			pEntry->P_AID = (pEntry->P_AID << 1) | (RA[4] >> 7 );
			ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s: BFee P_AID addressed to AP=0x%X\n", __FUNCTION__,pEntry->P_AID));
		}
		cpMacAddr(pEntry->MacAddr, RA);
		pEntry->bTxBF = FALSE;
		pEntry->bSound = FALSE;

		//3 TODO SW/FW sound period
		pEntry->SoundPeriod = 400;
		pEntry->BeamformEntryCap = BeamformCap;
		pEntry->BeamformEntryState = BEAMFORMING_ENTRY_STATE_UNINITIALIZE;

/*		pEntry->LogSeq = 0xff;				// Move to Beamforming_AddBFerEntry*/
/*		pEntry->LogRetryCnt = 0;			// Move to Beamforming_AddBFerEntry*/
/*		pEntry->LogSuccessCnt = 0;		// Move to Beamforming_AddBFerEntry*/

		pEntry->LogStatusFailCnt = 0;

		pEntry->NumofSoundingDim = NumofSoundingDim;
		pEntry->CompSteeringNumofBFer = CompSteeringNumofBFer;

		return pEntry;
	}
	else
		return NULL;
}

PRT_BEAMFORMER_ENTRY
Beamforming_AddBFerEntry(
	IN	PADAPTER			Adapter,
	IN	pu1Byte				RA,
	IN	u2Byte				AID,
	IN	BEAMFORMING_CAP	BeamformCap,
	IN	u1Byte				NumofSoundingDim,
	OUT	pu1Byte				Idx
	)
{
	HAL_DATA_TYPE			*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T				pDM_Odm = &pHalData->DM_OutSrc;
	PMGNT_INFO				pMgntInfo = &(Adapter->MgntInfo);
	PRT_BEAMFORMER_ENTRY	pEntry = phydm_Beamforming_GetFreeBFerEntry(pDM_Odm, Idx);

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s Start!\n", __FUNCTION__));

	if(pEntry != NULL)
	{
		pEntry->bUsed = TRUE;
		if(ACTING_AS_AP(Adapter))
		{
			u2Byte BSSID = ((Adapter->CurrentAddress[5] & 0xf0) >> 4) ^ 
							(Adapter->CurrentAddress[5] & 0xf);	// BSSID[44:47] xor BSSID[40:43]
			pEntry->P_AID = (AID + BSSID * 32) & 0x1ff;		// (dec(A) + dec(B)*32) mod 512
		}		
		else if(ACTING_AS_IBSS(Adapter))
		{
			pEntry->P_AID = 0;
		}
		else
		{
			pEntry->P_AID =  RA[5];						// BSSID[39:47]
			pEntry->P_AID = (pEntry->P_AID << 1) | (RA[4] >> 7 );
		}
		
		cpMacAddr(pEntry->MacAddr, RA);
		pEntry->BeamformEntryCap = BeamformCap;
		
		pEntry->LogSeq = 0xff;			// Modified by Jeffery @2014-10-29
		pEntry->LogRetryCnt = 0;		// Modified by Jeffery @2014-10-29
		pEntry->LogSuccessCnt = 0;		// Modified by Jeffery @2014-10-29		

		pEntry->NumofSoundingDim = NumofSoundingDim;

		return pEntry;
	}
	else
		return NULL;
}


BOOLEAN
Beamforming_RemoveEntry(
	IN	PADAPTER			Adapter,
	IN	pu1Byte		RA,
	OUT	pu1Byte		Idx
	)
{
	HAL_DATA_TYPE			*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T				pDM_Odm = &pHalData->DM_OutSrc;

	PRT_BEAMFORMER_ENTRY	pBFerEntry = phydm_Beamforming_GetBFerEntryByAddr(pDM_Odm, RA, Idx);
	PRT_BEAMFORMING_ENTRY	pEntry = phydm_Beamforming_GetBFeeEntryByAddr(pDM_Odm, RA, Idx);
	BOOLEAN ret = FALSE;
    
	RT_DISP(FBEAM, FBEAM_FUN, ("[Beamforming]@%s Start!\n", __FUNCTION__) );
    RT_DISP(FBEAM, FBEAM_FUN, ("[Beamforming]@%s, pBFerEntry=0x%x \n", __FUNCTION__,pBFerEntry) );
    RT_DISP(FBEAM, FBEAM_FUN, ("[Beamforming]@%s, pEntry=0x%x \n", __FUNCTION__,pEntry) );
	
	if (pEntry != NULL) {	
		pEntry->bUsed = FALSE;
		pEntry->BeamformEntryCap = BEAMFORMING_CAP_NONE;
		/*pEntry->BeamformEntryState = BEAMFORMING_ENTRY_STATE_UNINITIALIZE;*/
		pEntry->bBeamformingInProgress = FALSE;
		ret = TRUE;
	} 
	if (pBFerEntry != NULL) {
		pBFerEntry->bUsed = FALSE;
		pBFerEntry->BeamformEntryCap = BEAMFORMING_CAP_NONE;
		ret = TRUE;
	}
	return ret;

}

/* Used for BeamformingStart_V1  */
VOID
phydm_Beamforming_NDPARate(
	IN	PVOID		pDM_VOID,
	CHANNEL_WIDTH 	BW, 
	u1Byte			Rate
)
{
	u2Byte			NDPARate = Rate;
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s Start!\n", __FUNCTION__) );

	if(NDPARate == 0)
	{
		if(pDM_Odm->RSSI_Min > 30) // link RSSI > 30%
			NDPARate = ODM_RATE24M;
		else
			NDPARate = ODM_RATE6M;
	}

	if(NDPARate < ODM_RATEMCS0)
		BW = (CHANNEL_WIDTH)ODM_BW20M;

	NDPARate = (NDPARate << 8) | BW;
	HalComTxbf_Set(pDM_Odm->Adapter, TXBF_SET_SOUNDING_RATE, (pu1Byte)&NDPARate);

}


/* Used for BeamformingStart_SW and  BeamformingStart_FW */
VOID
phydm_Beamforming_DymNDPARate(
	IN	PVOID		pDM_VOID
)
{
	u2Byte			NDPARate = ODM_RATE6M, BW;
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s Start!\n", __FUNCTION__) );

	if(pDM_Odm->RSSI_Min > 30) // link RSSI > 30%
		NDPARate = ODM_RATE24M;
	else
		NDPARate = ODM_RATE6M;

	BW = ODM_BW20M;
	NDPARate = NDPARate << 8 | BW;
	HalComTxbf_Set(pDM_Odm->Adapter, TXBF_SET_SOUNDING_RATE, (pu1Byte)&NDPARate);

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s End, NDPA Rate = 0x%X \n", __FUNCTION__, NDPARate));
}

/*	
*	SW Sounding : SW Timer unit 1ms 
*				 HW Timer unit (1/32000) s  32k is clock. 
*	FW Sounding : FW Timer unit 10ms
*/
VOID
Beamforming_DymPeriod(
	IN	PVOID		pDM_VOID,
	IN  u8          status
)
{
	u1Byte 					Idx;
	BOOLEAN					bChangePeriod = FALSE;	
	u2Byte					SoundPeriod_SW, SoundPeriod_FW;
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PADAPTER				Adapter = pDM_Odm->Adapter;
	PHAL_DATA_TYPE			pHalData = GET_HAL_DATA(Adapter);

	PRT_BEAMFORMING_ENTRY	pBeamformEntry;
	PRT_BEAMFORMING_INFO 	pBeamInfo = GET_BEAMFORM_INFO((Adapter));
	PRT_SOUNDING_INFO		pSoundInfo = &(pBeamInfo->SoundingInfo);

    PRT_BEAMFORMING_ENTRY   pEntry = &(pBeamInfo->BeamformeeEntry[pBeamInfo->BeamformeeCurIdx]);

	RT_DISP(FBEAM, FBEAM_FUN, ("[David]@%s Start!\n", __FUNCTION__) );
	
	//3 TODO  per-client throughput caculation.

	if ((Adapter->TxStats.CurrentTxTP + Adapter->RxStats.CurrentRxTP > 2)&&((pEntry->LogStatusFailCnt <= 20) || status)){
		//-@ Modified by David
		SoundPeriod_SW = 40;	/* 32*20? */
		SoundPeriod_FW = 40;	/* From  H2C cmd, unit = 10ms */
		
		RT_DISP(FBEAM, FBEAM_FUN, ("[David]@%s, current TRX TP>2, SoundPeriod_SW=%d, SoundPeriod_FW=%d\n",
		__FUNCTION__,
		SoundPeriod_SW,
		SoundPeriod_FW) );
	} else{
		//-@ Modified by David
		SoundPeriod_SW = 4000;/* 32*2000? */
		SoundPeriod_FW = 400;
		
		RT_DISP(FBEAM, FBEAM_FUN, ("[David]@%s, current TRX TP<2, SoundPeriod_SW=%d, SoundPeriod_FW=%d\n",
		__FUNCTION__,
		SoundPeriod_SW,
		SoundPeriod_FW) );
	}

	for(Idx = 0; Idx < BEAMFORMEE_ENTRY_NUM; Idx++)
	{
		pBeamformEntry = pBeamInfo->BeamformeeEntry+Idx;
		
		if(pBeamformEntry->DefaultCSICnt > 20)
		{
			//-@ Modified by David
			SoundPeriod_SW = 4000;
			SoundPeriod_FW = 400;
		}
		
		RT_DISP(FBEAM, FBEAM_FUN, ("@%s Period = %d\n", __FUNCTION__, SoundPeriod_SW));		
		if(pBeamformEntry->BeamformEntryCap & (BEAMFORMER_CAP_HT_EXPLICIT |BEAMFORMER_CAP_VHT_SU))
		{
			if(pSoundInfo->SoundMode == SOUNDING_FW_VHT_TIMER || pSoundInfo->SoundMode == SOUNDING_FW_HT_TIMER)
			{				
				if(pBeamformEntry->SoundPeriod != SoundPeriod_FW)
				{
					pBeamformEntry->SoundPeriod = SoundPeriod_FW;
					bChangePeriod = TRUE;	// Only FW sounding need to send H2C packet to change sound period. 
				}
			}
			else if(pBeamformEntry->SoundPeriod != SoundPeriod_SW)
			{
				pBeamformEntry->SoundPeriod = SoundPeriod_SW;
			}
		}
	}

	if(bChangePeriod)
		HalComTxbf_Set(Adapter, TXBF_SET_SOUNDING_FW_NDPA, (pu1Byte)&Idx);
}


VOID
Beamforming_GidPAid(
	PADAPTER	Adapter,
	PRT_TCB		pTcb
)
{
	u1Byte		Idx = 0;
	u1Byte		RA[6] ={0};
	PMGNT_INFO	pMgntInfo = &(Adapter->MgntInfo);
	pu1Byte		pHeader = GET_FRAME_OF_FIRST_FRAG(Adapter, pTcb);

	if(Adapter->HardwareType < HARDWARE_TYPE_RTL8192EE)
		return;
	else if(IS_WIRELESS_MODE_N(Adapter) == FALSE)
		return;

	GET_80211_HDR_ADDRESS1(pHeader, &RA);

	// VHT SU PPDU carrying one or more group addressed MPDUs or
	// Transmitting a VHT NDP intended for multiple recipients
	if(	MacAddr_isBcst(RA) || MacAddr_isMulticast(RA)	||
		pTcb->macId == MAC_ID_STATIC_FOR_BROADCAST_MULTICAST)
	{
		pTcb->G_ID = 63;
		pTcb->P_AID = 0;
	}
	else if(ACTING_AS_AP(Adapter))
	{
		u2Byte	AID = (u2Byte) (MacIdGetOwnerAssociatedClientAID(Adapter, pTcb->macId) & 0x1ff); 		//AID[0:8]

		pTcb->G_ID = 63;

		if(AID == 0)	//A PPDU sent by an AP to a non associated STA
			pTcb->P_AID = 0;
		else
		{			//Sent by an AP and addressed to a STA associated with that AP
			u2Byte	BSSID = 0;
			GET_80211_HDR_ADDRESS2(pHeader, &RA);
			BSSID = ((RA[5] & 0xf0) >> 4) ^ (RA[5] & 0xf);	// BSSID[44:47] xor BSSID[40:43]
			pTcb->P_AID = (AID + BSSID * 32) & 0x1ff;		// (dec(A) + dec(B)*32) mod 512
		}
	}
	else if(ACTING_AS_IBSS(Adapter))
	{
		pTcb->G_ID = 63;
		// P_AID for infrasturcture mode; MACID for ad-hoc mode. 
		pTcb->P_AID = pTcb->macId;
	}
	else if(MgntLinkStatusQuery(Adapter))	
	{	// Addressed to AP
		pTcb->G_ID = 0;
		GET_80211_HDR_ADDRESS1(pHeader, &RA);
		pTcb->P_AID =  RA[5];						// RA[39:47]
		pTcb->P_AID = (pTcb->P_AID << 1) | (RA[4] >> 7 );
	}
	else
	{
		pTcb->G_ID = 63;
		pTcb->P_AID = 0;
	}

	//RT_DISP(FBEAM, FBEAM_FUN, ("[David]@%s End, G_ID=0x%X, P_AID=0x%X \n", __FUNCTION__, pTcb->G_ID, pTcb->P_AID) );
	
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
	PRT_BEAMFORMING_ENTRY		pBeamformEntry = NULL;
	PMGNT_INFO					pMgntInfo = &(Adapter->MgntInfo);
	pu1Byte						pMIMOCtrlField, pCSIReport, pCSIMatrix;
	u1Byte						Idx, Nc, Nr, CH_W;
	u2Byte						CSIMatrixLen = 0;

	ACT_PKT_TYPE				pktType = ACT_PKT_TYPE_UNKNOWN;

	//Memory comparison to see if CSI report is the same with previous one
	pBeamformEntry = phydm_Beamforming_GetBFeeEntryByAddr(pDM_Odm, Frame_Addr2(*pPduOS), &Idx);

	if( pBeamformEntry == NULL )
	{
		RT_DISP(FBEAM, FBEAM_DATA, ("Beamforming_GetReportFrame: Cannot find entry by addr\n"));
		return RT_STATUS_FAILURE;
	}

	pktType = PacketGetActionFrameType(pPduOS);

	RT_DISP(FBEAM, FBEAM_FUN, ("[David]@%s Start!\n", __FUNCTION__) );
	
	//-@ Modified by David
	if(pktType == ACT_PKT_VHT_COMPRESSED_BEAMFORMING)
	{
		pMIMOCtrlField = pPduOS->Octet + 26; 
		Nc = ((*pMIMOCtrlField) & 0x7) + 1;
		Nr = (((*pMIMOCtrlField) & 0x38) >> 3) + 1;
		CH_W =  (((*pMIMOCtrlField) & 0xC0) >> 6);
		pCSIMatrix = pMIMOCtrlField + 3 + Nc; //24+(1+1+3)+2  MAC header+(Category+ActionCode+MIMOControlField) +SNR(Nc=2)
		CSIMatrixLen = pPduOS->Length  - 26 -3 -Nc;
	}	
	else if(pktType == ACT_PKT_HT_COMPRESSED_BEAMFORMING)
	{
		pMIMOCtrlField = pPduOS->Octet + 26; 
		Nc = ((*pMIMOCtrlField) & 0x3) + 1;
		Nr =  (((*pMIMOCtrlField) & 0xC) >> 2) + 1;
		CH_W =  (((*pMIMOCtrlField) & 0x10) >> 4);
		pCSIMatrix = pMIMOCtrlField + 6 + Nr;	//24+(1+1+6)+2  MAC header+(Category+ActionCode+MIMOControlField) +SNR(Nc=2)
		CSIMatrixLen = pPduOS->Length  - 26 -6 -Nr;
	}
	else
		return RT_STATUS_SUCCESS;	
	
	if(pktType == ACT_PKT_VHT_COMPRESSED_BEAMFORMING)
	{
		RT_DISP(FBEAM, FBEAM_FUN, ("[David]@BF_GetReportFrame: idx=%d, pkt type=VHT_Cprssed_BF, Nc=%d, Nr=%d, CH_W=%d\n", Idx, Nc, Nr, CH_W));
	}	
	else if(pktType == ACT_PKT_HT_COMPRESSED_BEAMFORMING)
	{
		RT_DISP(FBEAM, FBEAM_FUN, ("[David]@BF_GetReportFrame: idx=%d, pkt type=HT_Cprssed_BF, Nc=%d, Nr=%d, CH_W=%d\n", Idx, Nc, Nr, CH_W));
	}

	
//	RT_DISP_DATA(FBEAM, FBEAM_DATA, "Beamforming_GetReportFrame \n", pMIMOCtrlField, pPduOS->Length - 26);

	//-@ Modified by David - CSI buffer is not big enough, and comparison would result in blue screen
	/*
	if(pBeamformEntry->CSIMatrixLen != CSIMatrixLen)
		pBeamformEntry->DefaultCSICnt = 0;
	else if(PlatformCompareMemory(pBeamformEntry->CSIMatrix, pCSIMatrix, CSIMatrixLen)) 
	{
		pBeamformEntry->DefaultCSICnt = 0;
		RT_DISP(FBEAM, FBEAM_FUN, ("%s CSI report is NOT the same with previos one\n", __FUNCTION__));
	}
	else	if(pBeamformEntry->DefaultCSICnt <= 20)
	{
		pBeamformEntry->DefaultCSICnt ++;
		RT_DISP(FBEAM, FBEAM_FUN, ("%s CSI report is the SAME with previos one\n", __FUNCTION__));
	}

	pBeamformEntry->CSIMatrixLen = CSIMatrixLen;
	PlatformMoveMemory(&pBeamformEntry->CSIMatrix, pCSIMatrix, CSIMatrixLen);
	*/

	return RT_STATUS_SUCCESS;
}


VOID
Beamforming_GetNDPAFrame(
	IN	PADAPTER		Adapter,
	IN	OCTET_STRING	pduOS
)
{
	pu1Byte						TA ;
	u1Byte						Idx, Sequence;
	pu1Byte						pNDPAFrame = pduOS.Octet;
	PMGNT_INFO				pMgntInfo = &(Adapter->MgntInfo);
//	PRT_BEAMFORMING_ENTRY		pBeamformEntry = NULL;
	PRT_BEAMFORMER_ENTRY		pBeamformerEntry = NULL;		// Modified By Jeffery @2014-10-29
	HAL_DATA_TYPE				*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T					pDM_Odm = &pHalData->DM_OutSrc;

	RT_DISP(FBEAM, FBEAM_FUN, ("[David]@%s Start!\n", __FUNCTION__) );
	RT_DISP_DATA(FBEAM, FBEAM_DATA, "Beamforming_GetNDPAFrame\n", pduOS.Octet, pduOS.Length);

	if(IsCtrlNDPA(pNDPAFrame) == FALSE){
        RT_DISP(FBEAM, FBEAM_FUN, ("[Beamforming]@%s IsCtrlNDPA(pNDPAFrame) == FALSE\n", __FUNCTION__) );
		return;
        
    }
    
    else if( (IS_HARDWARE_TYPE_8812(Adapter) == FALSE)&&(IS_HARDWARE_TYPE_8821(Adapter) == FALSE) )
    {
        RT_DISP(FBEAM, FBEAM_FUN, ("[Beamforming]@%s IsCtrlNDPA(pNDPAFrame) == FALSE\n", __FUNCTION__) );
		return;
    }

	TA = Frame_Addr2(pduOS);
	// Remove signaling TA. 
	TA[0] = TA[0] & 0xFE; 
	
//	pBeamformEntry = Beamforming_GetBFeeEntryByAddr(Adapter, TA, &Idx);		
	pBeamformerEntry = phydm_Beamforming_GetBFerEntryByAddr(pDM_Odm, TA, &Idx);		// Modified By Jeffery @2014-10-29

        RT_DISP(FBEAM, FBEAM_FUN, ("[Beamforming]@%s After phydm_Beamforming_GetBFerEntryByAddr,pBeamformerEntry=0x%x\n", __FUNCTION__,pBeamformerEntry) );
    
	if(pBeamformerEntry == NULL)
		return;
	else if(!(pBeamformerEntry->BeamformEntryCap & BEAMFORMEE_CAP_VHT_SU))
		return;
//	else if(pBeamformerEntry->LogSuccessCnt > 1)			//暫時移除,避免永遠卡死在這個狀態
//		return;

/*    
	RT_DISP(FBEAM, FBEAM_FUN, ("[David]@%s begin, LogSeq=%d, LogRetryCnt=%d, LogSuccessCnt=%d\n",
	__FUNCTION__,	
	pBeamformerEntry->LogSeq,
	pBeamformerEntry->LogRetryCnt,
	pBeamformerEntry->LogSuccessCnt) );
*/

	Sequence = (pNDPAFrame[16]) >> 2;

    
	if(pBeamformerEntry->LogSeq != Sequence){
		/* Previous frame doesn't retry when meet new sequence number */
		if(pBeamformerEntry->LogSeq != 0xff && pBeamformerEntry->LogRetryCnt == 0)
			pBeamformerEntry->LogSuccessCnt++;
		
		pBeamformerEntry->LogSeq = Sequence;
        pBeamformerEntry->PreLogSeq = pBeamformerEntry->LogSeq;

        /* break option for clcok reset, 2015-03-30, Jeffery */
        if ((pBeamformerEntry->LogSeq != Sequence) && (pBeamformerEntry->PreLogSeq != pBeamformerEntry->LogSeq))
		pBeamformerEntry->LogRetryCnt = 0;
	} else { /* pBeamformerEntry->LogSeq == Sequence */  
        pBeamformerEntry->PreLogSeq = pBeamformerEntry->LogSeq;

        if (pBeamformerEntry->LogRetryCnt == 3){        
            RT_DISP(FBEAM, FBEAM_FUN, ("[Jeffery]@%s begin, Clock Reset!!!,PreLogSeq=%d, LogSeq=%d, LogRetryCnt=%d, LogSuccessCnt=%d\n",
            __FUNCTION__,   
            pBeamformerEntry->PreLogSeq,
            pBeamformerEntry->LogSeq,
            pBeamformerEntry->LogRetryCnt));
            pBeamformerEntry->LogRetryCnt = 0;
            HalComTxbf_Set(Adapter, TXBF_SET_SOUNDING_CLK, NULL);
	}
	else
            pBeamformerEntry->LogRetryCnt++;
    }



/*
	else
	{
		if(pBeamformerEntry->LogRetryCnt == 3)
			HalComTxbf_Set(Adapter, TXBF_SET_SOUNDING_CLK, NULL);
		else if(pBeamformerEntry->LogRetryCnt <= 3)
			pBeamformerEntry->LogRetryCnt++;
	}
*/


/*	RT_DISP(FBEAM, FBEAM_FUN, ("%s End, LogSeq=%d, LogRetryCnt=%d, LogSuccessCnt=%d\n", 
	__FUNCTION__,
	pBeamformerEntry->LogSeq,
	pBeamformerEntry->LogRetryCnt,
	pBeamformerEntry->LogSuccessCnt));*/
	
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
	PRT_HIGH_THROUGHPUT		pHTInfo = GET_HT_INFO(pMgntInfo);
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
	
	if(BW== CHANNEL_WIDTH_40)
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
	IN	PADAPTER		Adapter,
	IN	pu1Byte			RA,
	IN	CHANNEL_WIDTH	BW
	)
{
	PRT_TCB 				pTcb;
	PRT_TX_LOCAL_BUFFER 	pBuf;
	BOOLEAN 				ret = TRUE;
	u4Byte					BufLen;
	pu1Byte					BufAddr;
	u1Byte					DescLen = 0, Idx = 0, NDPTxRate;
	PADAPTER				pDefAdapter = GetDefaultAdapter(Adapter);
	PRT_BEAMFORMING_INFO 	pBeamInfo = GET_BEAMFORM_INFO(Adapter);
	HAL_DATA_TYPE			*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T				pDM_Odm = &pHalData->DM_OutSrc;
	PRT_BEAMFORMING_ENTRY	pBeamformEntry = phydm_Beamforming_GetBFeeEntryByAddr(pDM_Odm, RA, &Idx);

	RT_DISP(FBEAM, FBEAM_FUN, ("[David]@%s Start!\n", __FUNCTION__) );

	if(pBeamformEntry == NULL)
		return FALSE;

	NDPTxRate = Beamforming_GetHTNDPTxRate(Adapter, pBeamformEntry->CompSteeringNumofBFer);
	RT_DISP(FBEAM, FBEAM_FUN, ("%s, NDPTxRate =%d\n", __FUNCTION__, NDPTxRate));
	PlatformAcquireSpinLock(Adapter, RT_TX_SPINLOCK);

	if(MgntGetFWBuffer(pDefAdapter, &pTcb, &pBuf))
	{
#if(DEV_BUS_TYPE != RT_PCI_INTERFACE)
		HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(Adapter);

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
	}
	else
		ret = FALSE;

	PlatformReleaseSpinLock(Adapter, RT_TX_SPINLOCK);
	
	if(ret)
		RT_DISP_DATA(FBEAM, FBEAM_DATA, "", pBuf->Buffer.VirtualAddress, pTcb->PacketLength);

	return ret;
}


BOOLEAN
SendSWHTNDPAPacket(
	IN	PADAPTER		Adapter,
	IN	pu1Byte			RA,
	IN	CHANNEL_WIDTH	BW
	)
{
	PRT_TCB					pTcb;
	PRT_TX_LOCAL_BUFFER		pBuf;
	BOOLEAN					ret = TRUE;
	u1Byte					Idx = 0, NDPTxRate = 0;
	PRT_BEAMFORMING_INFO 	pBeamInfo = GET_BEAMFORM_INFO(Adapter);
	HAL_DATA_TYPE			*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T				pDM_Odm = &pHalData->DM_OutSrc;
	PRT_BEAMFORMING_ENTRY	pBeamformEntry = phydm_Beamforming_GetBFeeEntryByAddr(pDM_Odm, RA, &Idx);

	RT_DISP(FBEAM, FBEAM_FUN, ("[David]@%s Start!\n", __FUNCTION__) );

	NDPTxRate = Beamforming_GetHTNDPTxRate(Adapter, pBeamformEntry->CompSteeringNumofBFer);
	RT_DISP(FBEAM, FBEAM_FUN, ("%s, NDPTxRate =%d\n", __FUNCTION__, NDPTxRate));
	
	PlatformAcquireSpinLock(Adapter, RT_TX_SPINLOCK);

	if(MgntGetBuffer(Adapter, &pTcb, &pBuf))
	{
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
	}
	else
		ret = FALSE;
	
	PlatformReleaseSpinLock(Adapter, RT_TX_SPINLOCK);

	if(ret)
		RT_DISP_DATA(FBEAM, FBEAM_DATA, "", pBuf->Buffer.VirtualAddress, pTcb->PacketLength);

	return ret;
}




BOOLEAN
Beamforming_SendHTNDPAPacket(
	IN	PADAPTER		Adapter,
	IN	pu1Byte			RA,
	IN	CHANNEL_WIDTH	BW,
	IN	u1Byte			QIdx
	)
{
	BOOLEAN		ret = TRUE;

	if(QIdx == BEACON_QUEUE)
		ret = SendFWHTNDPAPacket(Adapter, RA, BW);
	else
		ret = SendSWHTNDPAPacket(Adapter, RA, BW);

	return ret;
}


VOID
ConstructVHTNDPAPacket(
	PADAPTER		Adapter,
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
	PMGNT_INFO				pMgntInfo = &(Adapter->MgntInfo);
	PRT_HIGH_THROUGHPUT		pHTInfo = GET_HT_INFO(pMgntInfo);
	RT_NDPA_STA_INFO		STAInfo;

	// Frame control.
	SET_80211_HDR_FRAME_CONTROL(pNDPAFrame, 0);
	SET_80211_HDR_TYPE_AND_SUBTYPE(pNDPAFrame, Type_NDPA);

	SET_80211_HDR_ADDRESS1(pNDPAFrame, RA);
	SET_80211_HDR_ADDRESS2(pNDPAFrame, Adapter->CurrentAddress);

	Duration = 2*aSifsTime + 44;
	
	if(BW == CHANNEL_WIDTH_80)
		Duration += 40;
	else if(BW == CHANNEL_WIDTH_40)
		Duration+= 87;
	else	
		Duration+= 180;

	SET_80211_HDR_DURATION(pNDPAFrame, Duration);

	Sequence = pMgntInfo->SoundingSequence << 2;
	PlatformMoveMemory(pNDPAFrame+16, &Sequence,1);

	if(	ACTING_AS_IBSS(Adapter) || 
		(ACTING_AS_AP(Adapter) == FALSE))
		AID = 0;

	STAInfo.AID = AID;
	STAInfo.FeedbackType = 0;
	STAInfo.NcIndex = 0;
	
	PlatformMoveMemory(pNDPAFrame+17, (pu1Byte)&STAInfo, 2);

	*pLength = 19;
}

BOOLEAN
SendFWVHTNDPAPacket(
	IN	PADAPTER		Adapter,
	IN	pu1Byte			RA,
	IN	u2Byte			AID,
	IN	CHANNEL_WIDTH	BW
	)
{
	PRT_TCB					pTcb;
	PRT_TX_LOCAL_BUFFER		pBuf;
	BOOLEAN					ret = TRUE;
	u4Byte					BufLen;
	pu1Byte					BufAddr;
	u1Byte					DescLen = 0, Idx = 0, NDPTxRate = 0;
	PADAPTER				pDefAdapter = GetDefaultAdapter(Adapter);
	PRT_BEAMFORMING_INFO 	pBeamInfo = GET_BEAMFORM_INFO(Adapter);
	HAL_DATA_TYPE			*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T				pDM_Odm = &pHalData->DM_OutSrc;
	PRT_BEAMFORMING_ENTRY	pBeamformEntry =phydm_Beamforming_GetBFeeEntryByAddr(pDM_Odm, RA, &Idx);

	RT_DISP(FBEAM, FBEAM_FUN, ("[David]@%s Start!\n", __FUNCTION__) );

	if(pBeamformEntry == NULL)
		return FALSE;

	NDPTxRate = Beamforming_GetVHTNDPTxRate(Adapter, pBeamformEntry->CompSteeringNumofBFer);
	RT_DISP(FBEAM, FBEAM_FUN, ("%s, NDPTxRate =%d\n", __FUNCTION__, NDPTxRate));
	
	PlatformAcquireSpinLock(Adapter, RT_TX_SPINLOCK);

	if(MgntGetFWBuffer(pDefAdapter, &pTcb, &pBuf))
	{
#if(DEV_BUS_TYPE != RT_PCI_INTERFACE)
		HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(Adapter);

		DescLen = Adapter->HWDescHeadLength - pHalData->USBALLDummyLength;
#endif
		BufAddr = pBuf->Buffer.VirtualAddress + DescLen;

		ConstructVHTNDPAPacket(
				Adapter, 
				RA,
				AID,
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
		pTcb->DataRate = NDPTxRate;	/*decide by Nr*/

		Adapter->HalFunc.CmdSendPacketHandler(Adapter, pTcb, pBuf, pTcb->PacketLength, DESC_PACKET_TYPE_NORMAL, FALSE);
	}
	else
		ret = FALSE;
	
	PlatformReleaseSpinLock(Adapter, RT_TX_SPINLOCK);	

	RT_DISP(FBEAM, FBEAM_FUN, ("@%s End, ret=%d \n", __FUNCTION__,ret));

	if(ret)
		RT_DISP_DATA(FBEAM, FBEAM_DATA, "", pBuf->Buffer.VirtualAddress, pTcb->PacketLength);

	return ret;
}


BOOLEAN
SendSWVHTNDPAPacket(
	IN	PADAPTER		Adapter,
	IN	pu1Byte			RA,
	IN	u2Byte			AID,
	IN	CHANNEL_WIDTH	BW
	)
{
	PRT_TCB					pTcb;
	PRT_TX_LOCAL_BUFFER		pBuf;
	BOOLEAN					ret = TRUE;
	u1Byte					Idx = 0, NDPTxRate = 0;
	PRT_BEAMFORMING_INFO 	pBeamInfo = GET_BEAMFORM_INFO(Adapter);
	HAL_DATA_TYPE			*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T				pDM_Odm = &pHalData->DM_OutSrc;
	PRT_BEAMFORMING_ENTRY	pBeamformEntry = phydm_Beamforming_GetBFeeEntryByAddr(pDM_Odm, RA, &Idx);

	RT_DISP(FBEAM, FBEAM_FUN, ("[David]@%s Start!\n", __FUNCTION__) );

	NDPTxRate = Beamforming_GetVHTNDPTxRate(Adapter, pBeamformEntry->CompSteeringNumofBFer);
	RT_DISP(FBEAM, FBEAM_FUN, ("%s, NDPTxRate =%d\n", __FUNCTION__, NDPTxRate));

	PlatformAcquireSpinLock(Adapter, RT_TX_SPINLOCK);

	if(MgntGetBuffer(Adapter, &pTcb, &pBuf))
	{
		ConstructVHTNDPAPacket(
				Adapter, 
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
	}
	else
		ret = FALSE;
	
	PlatformReleaseSpinLock(Adapter, RT_TX_SPINLOCK);	

	RT_DISP(FBEAM, FBEAM_FUN, ("%s\n", __FUNCTION__));

	if(ret)
		RT_DISP_DATA(FBEAM, FBEAM_DATA, "", pBuf->Buffer.VirtualAddress, pTcb->PacketLength);

	return ret;
}



BOOLEAN
Beamforming_SendVHTNDPAPacket(
	IN	PADAPTER		Adapter,
	IN	pu1Byte			RA,
	IN	u2Byte			AID,
	IN	CHANNEL_WIDTH	BW,
	IN	u1Byte			QIdx
	)
{
	HAL_DATA_TYPE			*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T				pDM_Odm = &pHalData->DM_OutSrc;
	BOOLEAN					ret = TRUE;

	HalComTxbf_Set(Adapter, TXBF_SET_GET_TX_RATE, NULL);

	if ((pDM_Odm->TxBfDataRate >= ODM_RATEVHTSS3MCS7) && (pDM_Odm->TxBfDataRate <= ODM_RATEVHTSS3MCS9)) {
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("@%s: 3SS VHT 789 don't sounding\n", __func__));

	} else {
	if(QIdx == BEACON_QUEUE)
		ret = SendFWVHTNDPAPacket(Adapter, RA, AID, BW);
	else
		ret = SendSWVHTNDPAPacket(Adapter, RA, AID, BW);

}
		return ret;
}


BEAMFORMING_NOTIFY_STATE
phydm_beamfomring_bSounding(
	IN	PVOID				pDM_VOID,
	PRT_BEAMFORMING_INFO 	pBeamInfo,
	pu1Byte					Idx
	)
{
	BEAMFORMING_NOTIFY_STATE	bSounding = BEAMFORMING_NOTIFY_NONE;
	RT_BEAMFORMING_OID_INFO	BeamOidInfo = pBeamInfo->BeamformingOidInfo;
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s Start!\n", __FUNCTION__) );

//	if(( Beamforming_GetBeamCap(pBeamInfo) & BEAMFORMER_CAP) == 0)
//		bSounding = BEAMFORMING_NOTIFY_RESET;
	if(BeamOidInfo.SoundOidMode == SOUNDING_STOP_All_TIMER)
		bSounding = BEAMFORMING_NOTIFY_RESET;
	else 
	{
		u1Byte i;

		for(i=0;i<BEAMFORMEE_ENTRY_NUM;i++)
		{
			ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("@%s: BFee Entry %d bUsed=%d, bSound=%d \n", __FUNCTION__, i, pBeamInfo->BeamformeeEntry[i].bUsed, pBeamInfo->BeamformeeEntry[i].bSound));
			if(pBeamInfo->BeamformeeEntry[i].bUsed && (!pBeamInfo->BeamformeeEntry[i].bSound))
			{
				ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s: Add BFee entry %d\n", __FUNCTION__, i));
				*Idx = i;
				bSounding = BEAMFORMING_NOTIFY_ADD;
			}

			if((!pBeamInfo->BeamformeeEntry[i].bUsed) && pBeamInfo->BeamformeeEntry[i].bSound)
			{
				ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s: Delete BFee entry %d\n", __FUNCTION__, i));
				*Idx = i;
				bSounding = BEAMFORMING_NOTIFY_DELETE;
			}
		}
	}

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s End, bSounding = %d\n", __FUNCTION__, bSounding) );
	return bSounding;
}


//This function is unused
u1Byte
phydm_beamforming_SoundingIdx(
	IN	PVOID				pDM_VOID,
	PRT_BEAMFORMING_INFO 		pBeamInfo
	)
{
	u1Byte					Idx = 0;
	RT_BEAMFORMING_ENTRY	BeamEntry;
	RT_BEAMFORMING_OID_INFO	BeamOidInfo = pBeamInfo->BeamformingOidInfo;
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s Start!\n", __FUNCTION__) );

	if(	BeamOidInfo.SoundOidMode == SOUNDING_SW_HT_TIMER || BeamOidInfo.SoundOidMode == SOUNDING_SW_VHT_TIMER ||
		BeamOidInfo.SoundOidMode == SOUNDING_HW_HT_TIMER || BeamOidInfo.SoundOidMode == SOUNDING_HW_VHT_TIMER)
		Idx = BeamOidInfo.SoundOidIdx;
	else
	{
		u1Byte	i;
		for(i = 0; i < BEAMFORMEE_ENTRY_NUM; i++)
		{
			if( pBeamInfo->BeamformeeEntry[i].bUsed &&
				(FALSE == pBeamInfo->BeamformeeEntry[i].bSound))
			{
				Idx = i;
				break;
			}
		}
	}

	return Idx;
}


SOUNDING_MODE
phydm_beamforming_SoundingMode(
	IN	PVOID				pDM_VOID,
	PRT_BEAMFORMING_INFO 	pBeamInfo,
	u1Byte					Idx
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte 			SupportInterface = pDM_Odm->SupportInterface;

	RT_BEAMFORMING_ENTRY		BeamEntry = pBeamInfo->BeamformeeEntry[Idx];
	RT_BEAMFORMING_OID_INFO		BeamOidInfo = pBeamInfo->BeamformingOidInfo;
	SOUNDING_MODE				Mode = BeamOidInfo.SoundOidMode;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s Start!\n", __FUNCTION__) );
//	RT_DISP(FBEAM, FBEAM_FUN, ("[David]@%s: OID mode is %d\n", __FUNCTION__, BeamOidInfo.SoundOidMode));

	if(BeamOidInfo.SoundOidMode == SOUNDING_SW_VHT_TIMER || BeamOidInfo.SoundOidMode == SOUNDING_HW_VHT_TIMER)
	{
		if(BeamEntry.BeamformEntryCap & BEAMFORMER_CAP_VHT_SU)
			Mode = BeamOidInfo.SoundOidMode;
		else 
			Mode = SOUNDING_STOP_All_TIMER;
	}	
	else if(BeamOidInfo.SoundOidMode == SOUNDING_SW_HT_TIMER || BeamOidInfo.SoundOidMode == SOUNDING_HW_HT_TIMER)
	{
		if(BeamEntry.BeamformEntryCap & BEAMFORMER_CAP_HT_EXPLICIT)
			Mode = BeamOidInfo.SoundOidMode;
		else
			Mode = SOUNDING_STOP_All_TIMER;
	}	
	else if(BeamEntry.BeamformEntryCap & BEAMFORMER_CAP_VHT_SU)
	{
		if((SupportInterface == ODM_ITRF_USB) && (pDM_Odm->SupportICType!= ODM_RTL8814A))
			Mode = SOUNDING_FW_VHT_TIMER;
		else
			Mode = SOUNDING_SW_VHT_TIMER;
	}
	else if(BeamEntry.BeamformEntryCap & BEAMFORMER_CAP_HT_EXPLICIT)
	{
		if((SupportInterface == ODM_ITRF_USB) && (pDM_Odm->SupportICType != ODM_RTL8814A))
		Mode = SOUNDING_FW_HT_TIMER;
	else
		Mode = SOUNDING_SW_HT_TIMER;
	}
	else 
		Mode = SOUNDING_STOP_All_TIMER;

	RT_DISP(FBEAM, FBEAM_FUN, ("[David]@%s End, SupportInterface=%d, Mode=%d \n", __FUNCTION__, SupportInterface, Mode));

	return Mode;
}


u2Byte
phydm_beamforming_SoundingTime(
	IN	PVOID				pDM_VOID,
	PRT_BEAMFORMING_INFO 	pBeamInfo,
	SOUNDING_MODE			Mode,
	u1Byte					Idx
	)
{
	u2Byte						SoundingTime = 0xffff;
	RT_BEAMFORMING_ENTRY		BeamEntry = pBeamInfo->BeamformeeEntry[Idx];
	RT_BEAMFORMING_OID_INFO	BeamOidInfo = pBeamInfo->BeamformingOidInfo;
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s Start!\n", __FUNCTION__) );

	if(Mode == SOUNDING_HW_HT_TIMER || Mode == SOUNDING_HW_VHT_TIMER)
		SoundingTime = BeamOidInfo.SoundOidPeriod * 32;
	else if(Mode == SOUNDING_SW_HT_TIMER || Mode == SOUNDING_SW_VHT_TIMER)
		//-@ Modified by David
		SoundingTime = BeamEntry.SoundPeriod; //100*32;  //BeamOidInfo.SoundOidPeriod;
	else
		SoundingTime = BeamEntry.SoundPeriod;

	return SoundingTime;
}


CHANNEL_WIDTH
phydm_beamforming_SoundingBW(
	IN	PVOID				pDM_VOID,
	PRT_BEAMFORMING_INFO 	pBeamInfo,
	SOUNDING_MODE			Mode,
	u1Byte					Idx
	)
{
	CHANNEL_WIDTH				SoundingBW = CHANNEL_WIDTH_20;
	RT_BEAMFORMING_ENTRY		BeamEntry = pBeamInfo->BeamformeeEntry[Idx];
	RT_BEAMFORMING_OID_INFO	BeamOidInfo = pBeamInfo->BeamformingOidInfo;
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s Start!\n", __FUNCTION__) );

	if(Mode == SOUNDING_HW_HT_TIMER || Mode == SOUNDING_HW_VHT_TIMER)
		SoundingBW = BeamOidInfo.SoundOidBW;
	else if(Mode == SOUNDING_SW_HT_TIMER || Mode == SOUNDING_SW_VHT_TIMER)
		//-@ Modified by David
		SoundingBW = BeamEntry.SoundBW;   //BeamOidInfo.SoundOidBW;
	else 
		SoundingBW = BeamEntry.SoundBW;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s, SoundingBW=0x%X \n", __FUNCTION__, SoundingBW) );

	return SoundingBW;
}


BOOLEAN
phydm_Beamforming_SelectBeamEntry(
	IN	PVOID				pDM_VOID,
	PRT_BEAMFORMING_INFO 	pBeamInfo
	)
{
	PRT_SOUNDING_INFO		pSoundInfo = &(pBeamInfo->SoundingInfo);
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s Start!\n", __FUNCTION__) );

	// pEntry.bSound is different between first and latter NDPA, and should not be used as BFee entry selection
	// BTW, latter modification should sync to the selection mechanism of AP/ADSL instead of the fixed SoundIdx.
	//pSoundInfo->SoundIdx = phydm_beamforming_SoundingIdx(pDM_Odm, pBeamInfo);
	pSoundInfo->SoundIdx = 0;

	if(pSoundInfo->SoundIdx < BEAMFORMEE_ENTRY_NUM)
		pSoundInfo->SoundMode = phydm_beamforming_SoundingMode(pDM_Odm, pBeamInfo, pSoundInfo->SoundIdx);
	else
		pSoundInfo->SoundMode = SOUNDING_STOP_All_TIMER;
	
	if(SOUNDING_STOP_All_TIMER == pSoundInfo->SoundMode)
		return FALSE;
	else
	{
		pSoundInfo->SoundBW = phydm_beamforming_SoundingBW(pDM_Odm, pBeamInfo, pSoundInfo->SoundMode, pSoundInfo->SoundIdx );
		pSoundInfo->SoundPeriod = phydm_beamforming_SoundingTime(pDM_Odm, pBeamInfo, pSoundInfo->SoundMode, pSoundInfo->SoundIdx );
		return TRUE;
	}
}


BOOLEAN
phydm_beamforming_StartPeriod(
	IN	PVOID				pDM_VOID
	)
{
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PADAPTER					Adapter = pDM_Odm->Adapter;
	BOOLEAN						Ret = TRUE;
	PRT_BEAMFORMING_INFO 		pBeamInfo = &pDM_Odm->BeamformingInfo;
	PRT_SOUNDING_INFO			pSoundInfo = &(pBeamInfo->SoundingInfo); 
	

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s Start!\n", __FUNCTION__) );

	phydm_Beamforming_DymNDPARate(pDM_Odm);

	phydm_Beamforming_SelectBeamEntry(pDM_Odm, pBeamInfo);		// Modified


	if(pSoundInfo->SoundMode == SOUNDING_SW_VHT_TIMER || pSoundInfo->SoundMode == SOUNDING_SW_HT_TIMER)
		ODM_SetTimer(pDM_Odm, &pBeamInfo->BeamformingTimer, pSoundInfo->SoundPeriod);
	else if(pSoundInfo->SoundMode == SOUNDING_HW_VHT_TIMER || pSoundInfo->SoundMode == SOUNDING_HW_HT_TIMER ||
			pSoundInfo->SoundMode == SOUNDING_AUTO_VHT_TIMER || pSoundInfo->SoundMode == SOUNDING_AUTO_HT_TIMER)
	{
		HAL_HW_TIMER_TYPE TimerType = HAL_TIMER_TXBF;
		u4Byte	val = (pSoundInfo->SoundPeriod | (TimerType<<16));

		//HW timer stop: All IC has the same setting
		Adapter->HalFunc.SetHwRegHandler(Adapter, HW_VAR_HW_REG_TIMER_STOP,  (pu1Byte)(&TimerType));
		//ODM_Write1Byte(pDM_Odm, 0x15F, 0);
		//HW timer init: All IC has the same setting, but 92E & 8812A only write 2 bytes
		Adapter->HalFunc.SetHwRegHandler(Adapter, HW_VAR_HW_REG_TIMER_INIT,  (pu1Byte)(&val));
		//ODM_Write1Byte(pDM_Odm, 0x164, 1);
		//ODM_Write4Byte(pDM_Odm, 0x15C, val);
		//HW timer start: All IC has the same setting
		Adapter->HalFunc.SetHwRegHandler(Adapter, HW_VAR_HW_REG_TIMER_START,  (pu1Byte)(&TimerType));
		//ODM_Write1Byte(pDM_Odm, 0x15F, 0x5);
	}	
	else if(pSoundInfo->SoundMode == SOUNDING_FW_VHT_TIMER || pSoundInfo->SoundMode == SOUNDING_FW_HT_TIMER)
	{
		Ret = BeamformingStart_FW(Adapter, pSoundInfo->SoundIdx);
	}
	else
		Ret = FALSE;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s: SoundIdx=%d, SoundMode=%d, SoundBW=%d, SoundPeriod=%d\n", __FUNCTION__, 
			pSoundInfo->SoundIdx, pSoundInfo->SoundMode, pSoundInfo->SoundBW, pSoundInfo->SoundPeriod));

	return Ret;
}

// Used after Beamforming_Leave, and will clear the setting of the "already deleted" entry
VOID
phydm_beamforming_EndPeriod_SW(
	IN	PVOID				pDM_VOID
	)
{
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PADAPTER					Adapter = pDM_Odm->Adapter;
	u1Byte						Idx = 0;
	PRT_BEAMFORMING_ENTRY		pBeamformEntry;
	PRT_BEAMFORMING_INFO 		pBeamInfo = &pDM_Odm->BeamformingInfo;
	PRT_SOUNDING_INFO			pSoundInfo = &(pBeamInfo->SoundingInfo);
	
	HAL_HW_TIMER_TYPE TimerType = HAL_TIMER_TXBF;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s Start!\n", __FUNCTION__) );

	if(pSoundInfo->SoundMode == SOUNDING_SW_VHT_TIMER || pSoundInfo->SoundMode == SOUNDING_SW_HT_TIMER)
		ODM_CancelTimer(pDM_Odm, &pBeamInfo->BeamformingTimer);
	else if(	pSoundInfo->SoundMode == SOUNDING_HW_VHT_TIMER || pSoundInfo->SoundMode == SOUNDING_HW_HT_TIMER ||
				pSoundInfo->SoundMode == SOUNDING_AUTO_VHT_TIMER || pSoundInfo->SoundMode == SOUNDING_AUTO_HT_TIMER)
		//HW timer stop: All IC has the same setting
		Adapter->HalFunc.SetHwRegHandler(Adapter, HW_VAR_HW_REG_TIMER_STOP,  (pu1Byte)(&TimerType));
		//ODM_Write1Byte(pDM_Odm, 0x15F, 0);
}

VOID
phydm_beamforming_EndPeriod_FW(
	IN	PVOID				pDM_VOID
	)
{
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;
	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s Start!\n", __FUNCTION__) );
	phydm_Beamforming_End_FW(pDM_Odm);
}



VOID 
phydm_beamforming_ClearEntry_SW(
	IN	PVOID			pDM_VOID,
	BOOLEAN				IsDelete,
	u1Byte				DeleteIdx
	)
{
	u1Byte						Idx = 0;
	PRT_BEAMFORMING_ENTRY		pBeamformEntry = NULL;
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PRT_BEAMFORMING_INFO 		pBeamInfo = &pDM_Odm->BeamformingInfo;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s Start!\n", __FUNCTION__) );

	if(IsDelete)
	{
		if(DeleteIdx<BEAMFORMEE_ENTRY_NUM)
		{
			pBeamformEntry = pBeamInfo->BeamformeeEntry + DeleteIdx;

			if(!((!pBeamformEntry->bUsed) && pBeamformEntry->bSound))
			{
				ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s: SW DeleteIdx is wrong!!!!! \n",__FUNCTION__));
				return;
			}
		}

		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s: SW delete BFee entry %d \n", __FUNCTION__, DeleteIdx));
		if(pBeamformEntry->BeamformEntryState == BEAMFORMING_ENTRY_STATE_PROGRESSING)
		{
			pBeamformEntry->bBeamformingInProgress = FALSE;
			pBeamformEntry->BeamformEntryState = BEAMFORMING_ENTRY_STATE_UNINITIALIZE;
		}
		else if(pBeamformEntry->BeamformEntryState == BEAMFORMING_ENTRY_STATE_PROGRESSED)
		{
			pBeamformEntry->BeamformEntryState  = BEAMFORMING_ENTRY_STATE_UNINITIALIZE;
			HalComTxbf_Set(pDM_Odm->Adapter, TXBF_SET_SOUNDING_STATUS, (pu1Byte)&DeleteIdx);
		}

		pBeamformEntry->bSound = FALSE;
	}
	else
	{
		for(Idx = 0; Idx < BEAMFORMEE_ENTRY_NUM; Idx++)
		{
			pBeamformEntry = pBeamInfo->BeamformeeEntry+Idx;

			// Used after bSounding=RESET, and will clear the setting of "ever sounded" entry, which is not necessarily be deleted.
			// This function is mainly used in case "BeamOidInfo.SoundOidMode == SOUNDING_STOP_All_TIMER".
			// However, setting oid doesn't delete entries (bUsed is still TRUE), new entries may fail to be added in.
		
			if(pBeamformEntry->bSound)
			{
				ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s: SW reset BFee entry %d \n", __FUNCTION__, Idx));
				/*	
				*	If End procedure is 
				*	1. Between (Send NDPA, C2H packet return), reset state to initialized.
				*	After C2H packet return , status bit will be set to zero. 
				*
				*	2. After C2H packet, then reset state to initialized and clear status bit.
				*/

				if (pBeamformEntry->BeamformEntryState == BEAMFORMING_ENTRY_STATE_PROGRESSING)
					phydm_Beamforming_End_SW(pDM_Odm, 0);
				else if (pBeamformEntry->BeamformEntryState == BEAMFORMING_ENTRY_STATE_PROGRESSED) {
					pBeamformEntry->BeamformEntryState  = BEAMFORMING_ENTRY_STATE_INITIALIZED;
					HalComTxbf_Set(pDM_Odm->Adapter, TXBF_SET_SOUNDING_STATUS, (pu1Byte)&Idx);
				}

				pBeamformEntry->bSound = FALSE;
			}
		}
	}
}

VOID
phydm_beamforming_ClearEntry_FW(
	IN	PVOID			pDM_VOID,
	BOOLEAN				IsDelete,
	u1Byte				DeleteIdx
	)
{
	u1Byte						Idx = 0;
	PRT_BEAMFORMING_ENTRY		pBeamformEntry = NULL;
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PRT_BEAMFORMING_INFO 		pBeamInfo = &pDM_Odm->BeamformingInfo;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s Start!\n", __FUNCTION__) );

	if(IsDelete)
	{
		if(DeleteIdx<BEAMFORMEE_ENTRY_NUM)
		{
			pBeamformEntry = pBeamInfo->BeamformeeEntry + DeleteIdx;

			if(!((!pBeamformEntry->bUsed) && pBeamformEntry->bSound))
			{
				ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s: FW DeleteIdx is wrong!!!!! \n",__FUNCTION__));
				return;
			}
		}
	
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s: FW delete BFee entry %d \n", __FUNCTION__, DeleteIdx));
		pBeamformEntry->BeamformEntryState = BEAMFORMING_ENTRY_STATE_UNINITIALIZE;
		pBeamformEntry->bSound = FALSE;
	}
	else
	{
		for(Idx = 0; Idx < BEAMFORMEE_ENTRY_NUM; Idx++)
		{
			pBeamformEntry = pBeamInfo->BeamformeeEntry+Idx;

			// Used after bSounding=RESET, and will clear the setting of "ever sounded" entry, which is not necessarily be deleted.
			// This function is mainly used in case "BeamOidInfo.SoundOidMode == SOUNDING_STOP_All_TIMER".
			// However, setting oid doesn't delete entries (bUsed is still TRUE), new entries may fail to be added in.
		
			if(pBeamformEntry->bSound)
			{
				ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s: FW reset BFee entry %d \n", __FUNCTION__, Idx));
				/*	
				*	If End procedure is 
				*	1. Between (Send NDPA, C2H packet return), reset state to initialized.
				*	After C2H packet return , status bit will be set to zero. 
				*
				*	2. After C2H packet, then reset state to initialized and clear status bit.
				*/
				
				pBeamformEntry->BeamformEntryState = BEAMFORMING_ENTRY_STATE_INITIALIZED;
				pBeamformEntry->bSound = FALSE;
			}
		}
	}
}

/*
* 	Called : 
*	1. Add and delete entry : Beamforming_Enter/Beamforming_Leave
*	2. FW trigger :  Beamforming_SetTxBFen
*	3. Set OID_RT_BEAMFORMING_PERIOD : BeamformingControl_V2
*/
VOID
phydm_Beamforming_Notify(
	IN	PVOID			pDM_VOID
	)
{
	u1Byte						Idx=BEAMFORMEE_ENTRY_NUM;
	BEAMFORMING_NOTIFY_STATE	bSounding = BEAMFORMING_NOTIFY_NONE;
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PRT_BEAMFORMING_INFO 		pBeamInfo = &pDM_Odm->BeamformingInfo;
	PRT_SOUNDING_INFO			pSoundInfo = &(pBeamInfo->SoundingInfo);

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[Beamforming]%s Start!\n", __FUNCTION__) );

	bSounding = phydm_beamfomring_bSounding(pDM_Odm, pBeamInfo, &Idx);

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[Beamforming]%s, Before notify, BeamformState=%d, bSounding=%d, Idx=%d\n", __FUNCTION__, pBeamInfo->BeamformState, bSounding, Idx));
	
	if(pBeamInfo->BeamformState == BEAMFORMING_STATE_END)
	{
		if(bSounding==BEAMFORMING_NOTIFY_ADD)
		{	
			if(phydm_beamforming_StartPeriod(pDM_Odm) == TRUE)
				pBeamInfo->BeamformState = BEAMFORMING_STATE_START_1BFee;
		}
	}
	else if(pBeamInfo->BeamformState == BEAMFORMING_STATE_START_1BFee)
	{
		if(bSounding==BEAMFORMING_NOTIFY_ADD)
		{
			if(phydm_beamforming_StartPeriod(pDM_Odm) == TRUE)
				pBeamInfo->BeamformState = BEAMFORMING_STATE_START_2BFee;
			else
				pBeamInfo->BeamformState = BEAMFORMING_STATE_START_1BFee;
		}
		else if(bSounding==BEAMFORMING_NOTIFY_DELETE)
		{
			if(pSoundInfo->SoundMode == SOUNDING_FW_HT_TIMER || pSoundInfo->SoundMode == SOUNDING_FW_VHT_TIMER)
			{
				phydm_beamforming_ClearEntry_FW(pDM_Odm, TRUE, Idx);		// Modified by Jeffery @ 2014-11-04
				phydm_beamforming_EndPeriod_FW(pDM_Odm);
			}
			else
			{
				phydm_beamforming_ClearEntry_SW(pDM_Odm, TRUE, Idx);		// Modified by Jeffery @ 2014-11-04
				phydm_beamforming_EndPeriod_SW(pDM_Odm);
			}

			pBeamInfo->BeamformState = BEAMFORMING_STATE_END;
		}
		else if(bSounding==BEAMFORMING_NOTIFY_RESET)
		{
			if(pSoundInfo->SoundMode == SOUNDING_FW_HT_TIMER || pSoundInfo->SoundMode == SOUNDING_FW_VHT_TIMER)
			{	
				phydm_beamforming_ClearEntry_FW(pDM_Odm, FALSE, Idx);		// Modified by Jeffery @ 2014-11-04
				phydm_beamforming_EndPeriod_FW(pDM_Odm);
			}
			else
			{
				phydm_beamforming_ClearEntry_SW(pDM_Odm, FALSE, Idx);		// Modified by Jeffery @ 2014-11-04
				phydm_beamforming_EndPeriod_SW(pDM_Odm);
			}
			
			pBeamInfo->BeamformState = BEAMFORMING_STATE_END;
		}
	}
	else if(pBeamInfo->BeamformState == BEAMFORMING_STATE_START_2BFee)
	{
		if(bSounding==BEAMFORMING_NOTIFY_ADD)
		{
			RT_ASSERT(FALSE, ("[David]@%s: Should be blocked at InitEntry!!!!! \n", __FUNCTION__));
		}
		else if(bSounding==BEAMFORMING_NOTIFY_DELETE)
		{
			if(pSoundInfo->SoundMode == SOUNDING_FW_HT_TIMER || pSoundInfo->SoundMode == SOUNDING_FW_VHT_TIMER)
			{
				phydm_beamforming_ClearEntry_FW(pDM_Odm, TRUE, Idx);		// Modified by Jeffery @ 2014-11-04
				phydm_beamforming_EndPeriod_FW(pDM_Odm);
			}
			else
			{
				// For 2->1 entry, we should not cancel SW timer
				phydm_beamforming_ClearEntry_SW(pDM_Odm, TRUE, Idx);
			}
		
			pBeamInfo->BeamformState = BEAMFORMING_STATE_START_1BFee;
		}
		else if(bSounding==BEAMFORMING_NOTIFY_RESET)
		{
			if(pSoundInfo->SoundMode == SOUNDING_FW_HT_TIMER || pSoundInfo->SoundMode == SOUNDING_FW_VHT_TIMER)
			{
				phydm_beamforming_ClearEntry_FW(pDM_Odm, FALSE, Idx);		// Modified by Jeffery @ 2014-11-04
				phydm_beamforming_EndPeriod_FW(pDM_Odm);
			}
			else
			{
				phydm_beamforming_ClearEntry_SW(pDM_Odm, FALSE, Idx);		// Modified by Jeffery @ 2014-11-04
				phydm_beamforming_EndPeriod_SW(pDM_Odm);
			}
			
			pBeamInfo->BeamformState = BEAMFORMING_STATE_END;
		}
	}
	else
		RT_ASSERT(FALSE, ("%s BeamformState %d\n", __FUNCTION__, pBeamInfo->BeamformState));
	
	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s End, after Notify, BeamformState =%d\n", __FUNCTION__, pBeamInfo->BeamformState));
}



BOOLEAN
Beamforming_InitEntry(
	PADAPTER		Adapter,
	PRT_WLAN_STA	pSTA,
	pu1Byte			BFerBFeeIdx
	)
{
	PMGNT_INFO					pMgntInfo = &Adapter->MgntInfo;
	PRT_HIGH_THROUGHPUT			pHTInfo = GET_HT_INFO(pMgntInfo);
	PRT_VERY_HIGH_THROUGHPUT	pVHTInfo = GET_VHT_INFO(pMgntInfo);
	PRT_BEAMFORMING_ENTRY		pBeamformEntry = NULL;
	PRT_BEAMFORMER_ENTRY		pBeamformerEntry = NULL;
	pu1Byte						RA; 
	u2Byte						AID, MacID;
	WIRELESS_MODE				WirelessMode;
	CHANNEL_WIDTH				BW = CHANNEL_WIDTH_20;
	BEAMFORMING_CAP				BeamformCap = BEAMFORMING_CAP_NONE;
	u1Byte						BFerIdx=0xF, BFeeIdx=0xF;
	u1Byte						NumofSoundingDim = 0, CompSteeringNumofBFer = 0;
	HAL_DATA_TYPE				*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T					pDM_Odm = &pHalData->DM_OutSrc;

	RT_DISP(FBEAM, FBEAM_FUN, ("[David]@%s Start!\n", __FUNCTION__));

	// The current setting does not support Beaforming
	if(BEAMFORMING_CAP_NONE == pHTInfo->HtBeamformCap && BEAMFORMING_CAP_NONE == pVHTInfo->VhtBeamformCap)
	{
		RT_DISP(FBEAM, FBEAM_ERROR, ("The configuration disabled Beamforming! Skip...\n"));		
		return FALSE;
	}

	// IBSS, AP mode
	if(pSTA != NULL)
	{
		AID = pSTA->AID;
		RA = pSTA->MacAddr;
		MacID = pSTA->AssociatedMacId;
		WirelessMode = pSTA->WirelessMode;
		BW = pSTA->BandWidth;
	}
	else	// Client mode
	{
		AID = pMgntInfo->mAId;
		RA = pMgntInfo->Bssid;
		MacID = pMgntInfo->mMacId;
		WirelessMode = pMgntInfo->dot11CurrentWirelessMode;
		BW = pMgntInfo->dot11CurrentChannelBandWidth;
	}

	if(WirelessMode < WIRELESS_MODE_N_24G)
		return FALSE;
	else 
	{
		//3 // HT
		u1Byte CurBeamform; 
		u2Byte CurBeamformVHT;

		if(pSTA != NULL)
			CurBeamform = pSTA->HTInfo.HtCurBeamform;
		else
			CurBeamform = pHTInfo->HtCurBeamform;

		// We are Beamformee because the STA is Beamformer
		if(TEST_FLAG(CurBeamform, BEAMFORMING_HT_BEAMFORMER_ENABLE))
		{
			BeamformCap =(BEAMFORMING_CAP)(BeamformCap |BEAMFORMEE_CAP_HT_EXPLICIT);
			NumofSoundingDim = (CurBeamform&BEAMFORMING_HT_BEAMFORMEE_CHNL_EST_CAP)>>6;
		}

		// We are Beamformer because the STA is Beamformee
		if(	TEST_FLAG(CurBeamform, BEAMFORMING_HT_BEAMFORMEE_ENABLE) ||
			TEST_FLAG(pHTInfo->HtBeamformCap, BEAMFORMING_HT_BEAMFORMER_TEST))
		{
			BeamformCap =(BEAMFORMING_CAP)(BeamformCap | BEAMFORMER_CAP_HT_EXPLICIT);
			CompSteeringNumofBFer = (CurBeamform & BEAMFORMING_HT_BEAMFORMER_STEER_NUM)>>4;
		}

		RT_DISP(FBEAM, FBEAM_FUN, ("[David]@%s, CurBeamform=0x%X, BeamformCap=0x%X\n", __FUNCTION__, CurBeamform, BeamformCap));
		RT_DISP(FBEAM, FBEAM_FUN, ("[David]@%s, NumofSoundingDim=%d, CompSteeringNumofBFer=%d\n", __FUNCTION__, NumofSoundingDim, CompSteeringNumofBFer));


		if(WirelessMode == WIRELESS_MODE_AC_5G || WirelessMode == WIRELESS_MODE_AC_24G)
		{
			//3 // VHT
			if(pSTA != NULL)
				CurBeamformVHT = pSTA->VHTInfo.VhtCurBeamform;
			else
				CurBeamformVHT = pVHTInfo->VhtCurBeamform;			

			// We are Beamformee because the STA is Beamformer
			if(TEST_FLAG(CurBeamformVHT, BEAMFORMING_VHT_BEAMFORMER_ENABLE))
			{
				BeamformCap =(BEAMFORMING_CAP)(BeamformCap |BEAMFORMEE_CAP_VHT_SU);
				NumofSoundingDim = (CurBeamformVHT & BEAMFORMING_VHT_BEAMFORMEE_SOUND_DIM)>>12;
			}
			// We are Beamformer because the STA is Beamformee
			if(	TEST_FLAG(CurBeamformVHT, BEAMFORMING_VHT_BEAMFORMEE_ENABLE) ||
				TEST_FLAG(pVHTInfo->VhtBeamformCap, BEAMFORMING_VHT_BEAMFORMER_TEST))
			{
				BeamformCap =(BEAMFORMING_CAP)(BeamformCap |BEAMFORMER_CAP_VHT_SU);
				CompSteeringNumofBFer = (CurBeamformVHT & BEAMFORMING_VHT_BEAMFORMER_STS_CAP)>>8;
			}

			RT_DISP(FBEAM, FBEAM_FUN, ("[David]@%s, CurBeamformVHT=0x%X, BeamformCap=0x%X\n", __FUNCTION__, CurBeamformVHT, BeamformCap));
			RT_DISP(FBEAM, FBEAM_FUN, ("%s, NumofSoundingDim=0x%X, CompSteeringNumofBFer=0x%X\n", __FUNCTION__, NumofSoundingDim, CompSteeringNumofBFer));
			
		}
	}


	if(BeamformCap == BEAMFORMING_CAP_NONE)
		return FALSE;
	
	RT_DISP(FBEAM, FBEAM_FUN, ("[David]@%s, Self BF Entry Cap = 0x%02X\n", __FUNCTION__, BeamformCap));

	// We are BFee, so the entry is BFer
	if((BeamformCap & BEAMFORMEE_CAP_VHT_SU) || (BeamformCap & BEAMFORMEE_CAP_HT_EXPLICIT))
	{
		pBeamformerEntry = phydm_Beamforming_GetBFerEntryByAddr(pDM_Odm, RA, &BFerIdx);
		
		if(pBeamformerEntry == NULL)
		{
			pBeamformerEntry = Beamforming_AddBFerEntry(Adapter, RA, AID, BeamformCap, NumofSoundingDim ,&BFerIdx);

			if(pBeamformerEntry == NULL)
				RT_DISP(FBEAM, FBEAM_FUN, ("[David]@%s: Not enough BFer entry!!!!! \n", __FUNCTION__));
		}	
	}

	// We are BFer, so the entry is BFee
	if((BeamformCap & BEAMFORMER_CAP_VHT_SU) || (BeamformCap & BEAMFORMER_CAP_HT_EXPLICIT))
	{
		pBeamformEntry = phydm_Beamforming_GetBFeeEntryByAddr(pDM_Odm, RA, &BFeeIdx);

		// 如果BFeeIdx = 0xF 則代表目前entry當中沒有相同的MACID在內
		RT_DISP(FBEAM, FBEAM_FUN, ("[David]@%s: Get BFee entry 0x%X by address \n", __FUNCTION__, BFeeIdx));
		if(pBeamformEntry == NULL)
		{
			pBeamformEntry = Beamforming_AddBFeeEntry(Adapter, RA, AID, MacID, BW, BeamformCap, NumofSoundingDim, CompSteeringNumofBFer, &BFeeIdx);

			RT_DISP(FBEAM, FBEAM_FUN, ("[David]@%s: Add BFee entry %d \n", __FUNCTION__, BFeeIdx));

			if(pBeamformEntry == NULL)
				return FALSE;
			else
				pBeamformEntry->BeamformEntryState = BEAMFORMING_ENTRY_STATE_INITIALIZEING;
		}
		else
		{
			// Entry has been created. If entry is initialing or progressing then errors occur.
			if(	pBeamformEntry->BeamformEntryState != BEAMFORMING_ENTRY_STATE_INITIALIZED && 
				pBeamformEntry->BeamformEntryState != BEAMFORMING_ENTRY_STATE_PROGRESSED)
			{
				RT_ASSERT(TRUE, ("Error State of Beamforming"));
				return FALSE;
			}	
			else
				pBeamformEntry->BeamformEntryState = BEAMFORMING_ENTRY_STATE_INITIALIZEING;
		}

		pBeamformEntry->BeamformEntryState = BEAMFORMING_ENTRY_STATE_INITIALIZED;

	}

	*BFerBFeeIdx = (BFerIdx<<4) | BFeeIdx;
	RT_DISP(FBEAM, FBEAM_FUN, ("[David]@%s End: BFerIdx=0x%X, BFeeIdx=0x%X, BFerBFeeIdx=0x%X \n", __FUNCTION__, BFerIdx, BFeeIdx, *BFerBFeeIdx));

	return TRUE;
}


VOID
Beamforming_DeInitEntry(
	PADAPTER		Adapter,
	pu1Byte			RA
	)
{
	u1Byte					Idx = 0;
	PMGNT_INFO				pMgntInfo = &(Adapter->MgntInfo);

        RT_DISP(FBEAM, FBEAM_FUN, ("[Beamforming]@%s Start!\n", __FUNCTION__) );

	if(Beamforming_RemoveEntry(Adapter, RA, &Idx) == TRUE)
	{
		HalComTxbf_Set(Adapter, TXBF_SET_SOUNDING_LEAVE, (pu1Byte)&Idx);
	}

	RT_DISP(FBEAM, FBEAM_FUN, ("[David]@%s End, Idx = 0x%X\n", __FUNCTION__, Idx));
}


VOID
Beamforming_Reset(
	PADAPTER		Adapter
	)
{
	u1Byte					Idx = 0;
	PMGNT_INFO				pMgntInfo = &(Adapter->MgntInfo);
	PRT_BEAMFORMING_INFO 	pBeamformingInfo = GET_BEAMFORM_INFO(Adapter);

	RT_DISP(FBEAM, FBEAM_FUN, ("[David]@%s Start!\n", __FUNCTION__) );

	for(Idx = 0; Idx < BEAMFORMEE_ENTRY_NUM; Idx++)
	{
		if(pBeamformingInfo->BeamformeeEntry[Idx].bUsed == TRUE)
		{
			pBeamformingInfo->BeamformeeEntry[Idx].bUsed = FALSE;
			pBeamformingInfo->BeamformeeEntry[Idx].BeamformEntryCap = BEAMFORMING_CAP_NONE;
			//pBeamformingInfo->BeamformeeEntry[Idx].BeamformEntryState = BEAMFORMING_ENTRY_STATE_UNINITIALIZE;
			//-@ Modified by David
			pBeamformingInfo->BeamformeeEntry[Idx].bBeamformingInProgress = FALSE;
			HalComTxbf_Set(Adapter, TXBF_SET_SOUNDING_LEAVE, (pu1Byte)&Idx);
		}
	}

	for(Idx = 0; Idx < BEAMFORMER_ENTRY_NUM; Idx++)
	{
		pBeamformingInfo->BeamformerEntry[Idx].bUsed = FALSE;
		RT_DISP(FBEAM, FBEAM_FUN, ("[David]@%s, Idx=%d, bUsed=%d \n", __FUNCTION__, Idx, pBeamformingInfo->BeamformerEntry[Idx].bUsed));
	}

}


BOOLEAN
BeamformingStart_V1(
	PADAPTER		Adapter,
	pu1Byte			RA,
	BOOLEAN			Mode,
	CHANNEL_WIDTH	BW,
	u1Byte			Rate
	)
{
	u1Byte					Idx = 0;
	PRT_BEAMFORMING_ENTRY	pEntry;
	BOOLEAN					ret = TRUE;
	PMGNT_INFO				pMgntInfo = &(Adapter->MgntInfo);
	PRT_BEAMFORMING_INFO 	pBeamInfo = GET_BEAMFORM_INFO(Adapter);
	HAL_DATA_TYPE			*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T				pDM_Odm = &pHalData->DM_OutSrc;

	RT_DISP(FBEAM, FBEAM_FUN, ("[David]@%s Start!\n", __FUNCTION__) );

	pEntry = phydm_Beamforming_GetBFeeEntryByAddr(pDM_Odm, RA, &Idx);

	if(pEntry->bUsed == FALSE)
	{
		RT_DISP_ADDR(FBEAM, FBEAM_ERROR, ("Skip Beamforming, no entry for addr = "), RA);
		pEntry->bBeamformingInProgress = FALSE;
		return FALSE;
	}
	else
	{
		 if(pEntry->bBeamformingInProgress)
		 {
		 	RT_DISP(FBEAM, FBEAM_ERROR, ("bBeamformingInProgress, skip...\n"));
			return FALSE;
		 }

		pEntry->bBeamformingInProgress = TRUE;

		if(Mode == 1)
		{	
			if(!(pEntry->BeamformEntryCap & BEAMFORMER_CAP_HT_EXPLICIT))
			{
				RT_DISP_ADDR(FBEAM, FBEAM_ERROR, ("Skip Beamforming, entry without BEAMFORMEE_CAP_HT_EXPLICIT for addr = "), RA);
				pEntry->bBeamformingInProgress = FALSE;
				return FALSE;
			}
		}
		else if(Mode == 0)
		{
			if(!(pEntry->BeamformEntryCap & BEAMFORMER_CAP_VHT_SU))
			{
				RT_DISP_ADDR(FBEAM, FBEAM_ERROR, ("Skip Beamforming, entry without BEAMFORMEE_CAP_VHT_SU for addr = "), RA);
				pEntry->bBeamformingInProgress = FALSE;
				return FALSE;
			}

		}
		if(	pEntry->BeamformEntryState != BEAMFORMING_ENTRY_STATE_INITIALIZED &&
			pEntry->BeamformEntryState != BEAMFORMING_ENTRY_STATE_PROGRESSED)
		{
			RT_DISP_ADDR(FBEAM, FBEAM_ERROR, ("Skip Beamforming, state without (BEAMFORMING_STATE_INITIALIZED | BEAMFORMING_STATE_PROGRESSED) for addr = "), RA);
			pEntry->bBeamformingInProgress = FALSE;
			return FALSE;
		}	
		else
		{
			pEntry->BeamformEntryState = BEAMFORMING_ENTRY_STATE_PROGRESSING;
			pEntry->bSound = TRUE;
		}
	}

	pEntry->SoundBW = BW;
	pBeamInfo->BeamformeeCurIdx = Idx;
	phydm_Beamforming_NDPARate(pDM_Odm, BW, Rate);
	HalComTxbf_Set(Adapter, TXBF_SET_SOUNDING_STATUS, (pu1Byte)&Idx);

	if(Mode == 1)
		ret = Beamforming_SendHTNDPAPacket(Adapter,RA, BW, NORMAL_QUEUE);	
	else
		ret = Beamforming_SendVHTNDPAPacket(Adapter,RA, pEntry->AID, BW, NORMAL_QUEUE);

	if(ret == FALSE)
	{
		RT_DISP_ADDR(FBEAM, FBEAM_ERROR, ("Beamforming_RemoveEntry because of failure sending NDPA for addr = "), RA);
/*		Beamforming_RemoveEntry(Adapter, RA, &Idx);*/
        
                RT_DISP(FBEAM, FBEAM_FUN, ("[Beamforming]@%s calls Beamforming_Leave, RA=0x%x \n", __FUNCTION__,RA) );
		Beamforming_Leave(Adapter, RA);
		pEntry->bBeamformingInProgress = FALSE;
		return FALSE;
	}

	RT_DISP(FBEAM, FBEAM_FUN, ("%s  Idx %d\n", __FUNCTION__, Idx));
	return TRUE;
}


BOOLEAN
BeamformingStart_SW(
	PADAPTER		Adapter,
	u1Byte			Idx,
	u1Byte			Mode, 
	CHANNEL_WIDTH	BW
	)
{
	pu1Byte					RA = NULL;
	PRT_BEAMFORMING_ENTRY	pEntry;
	BOOLEAN					ret = TRUE;
	PMGNT_INFO				pMgntInfo = &(Adapter->MgntInfo);
	PRT_BEAMFORMING_INFO 	pBeamInfo = GET_BEAMFORM_INFO(Adapter);

	RT_DISP(FBEAM, FBEAM_FUN, ("[David]@%s Start!\n", __FUNCTION__) );

	pEntry = &(pBeamInfo->BeamformeeEntry[Idx]);

	if(pEntry->bUsed == FALSE)
	{
		RT_DISP(FBEAM, FBEAM_ERROR, ("Skip Beamforming, no entry for Idx =%d\n", Idx));
		pEntry->bBeamformingInProgress = FALSE;
		return FALSE;
	}
	else
	{
		 if(pEntry->bBeamformingInProgress)
		 {
		 	RT_DISP(FBEAM, FBEAM_ERROR, ("bBeamformingInProgress, skip...\n"));
			return FALSE;
		 }

		pEntry->bBeamformingInProgress = TRUE;
		
		RA = pEntry->MacAddr;
		
		if(Mode == SOUNDING_SW_HT_TIMER || Mode == SOUNDING_HW_HT_TIMER || Mode == SOUNDING_AUTO_HT_TIMER)
		{	
			if(!(pEntry->BeamformEntryCap & BEAMFORMER_CAP_HT_EXPLICIT))
			{
				RT_DISP_ADDR(FBEAM, FBEAM_ERROR, ("Skip Beamforming, entry without BEAMFORMEE_CAP_HT_EXPLICIT for addr = "), RA);
				pEntry->bBeamformingInProgress = FALSE;
				return FALSE;
			}
		}
		else if(Mode == SOUNDING_SW_VHT_TIMER || Mode == SOUNDING_HW_VHT_TIMER || Mode == SOUNDING_AUTO_VHT_TIMER)
		{
			if(!(pEntry->BeamformEntryCap & BEAMFORMER_CAP_VHT_SU))
			{
				RT_DISP_ADDR(FBEAM, FBEAM_ERROR, ("Skip Beamforming, entry without BEAMFORMEE_CAP_VHT_SU for addr = "), RA);
				pEntry->bBeamformingInProgress = FALSE;
				return FALSE;
			}

		}
		if(	pEntry->BeamformEntryState != BEAMFORMING_ENTRY_STATE_INITIALIZED &&
			pEntry->BeamformEntryState != BEAMFORMING_ENTRY_STATE_PROGRESSED)
		{
			RT_DISP_ADDR(FBEAM, FBEAM_ERROR, ("Skip Beamforming, state without (BEAMFORMING_STATE_INITIALIZED | BEAMFORMING_STATE_PROGRESSED) for addr = "), RA);
			pEntry->bBeamformingInProgress = FALSE;
			return FALSE;
		}	
		else
		{
			pEntry->BeamformEntryState = BEAMFORMING_ENTRY_STATE_PROGRESSING;
			pEntry->bSound = TRUE;
		}
	}

	pBeamInfo->BeamformeeCurIdx = Idx;
//2014.12.22 Luke: Need to be checked
	/*GET_TXBF_INFO(Adapter)->fTxbfSet(Adapter, TXBF_SET_SOUNDING_STATUS, (pu1Byte)&Idx);*/

	if(Mode == SOUNDING_SW_HT_TIMER || Mode == SOUNDING_HW_HT_TIMER || Mode == SOUNDING_AUTO_HT_TIMER)
		ret = Beamforming_SendHTNDPAPacket(Adapter,RA, BW, NORMAL_QUEUE);	
	else
		ret = Beamforming_SendVHTNDPAPacket(Adapter,RA, pEntry->AID, BW, NORMAL_QUEUE);

	if(ret == FALSE)
	{
		RT_DISP_ADDR(FBEAM, FBEAM_ERROR, ("Beamforming_RemoveEntry because of failure sending NDPA for addr = "), RA);
                RT_DISP(FBEAM, FBEAM_FUN, ("[Beamforming]@%s calls Beamforming_Leave, RA=0x%x \n", __FUNCTION__,RA) );
		Beamforming_Leave(Adapter, RA);
		pEntry->bBeamformingInProgress = FALSE;
		return FALSE;
	}

	if(Mode == SOUNDING_SW_HT_TIMER || Mode == SOUNDING_HW_HT_TIMER || Mode == SOUNDING_AUTO_HT_TIMER)
	{
		RT_DISP(FBEAM, FBEAM_FUN, ("[David]@%s: Send HT NDPA for current idx=%d\n", __FUNCTION__, Idx));
	}
	else
	{
		RT_DISP(FBEAM, FBEAM_FUN, ("[David]@%s: Send VHT NDPA for current idx=%d\n", __FUNCTION__, Idx));
	}
	
	return TRUE;
}


BOOLEAN
BeamformingStart_FW(
	PADAPTER		Adapter,
	u1Byte			Idx
	)
{
	pu1Byte					RA = NULL;
	PRT_BEAMFORMING_ENTRY	pEntry;
	BOOLEAN					ret = TRUE;
	PMGNT_INFO				pMgntInfo = &(Adapter->MgntInfo);
	PRT_BEAMFORMING_INFO 	pBeamInfo = GET_BEAMFORM_INFO(Adapter);

	pEntry = &(pBeamInfo->BeamformeeEntry[Idx]);
	if(pEntry->bUsed == FALSE)
	{
		RT_DISP(FBEAM, FBEAM_ERROR, ("Skip Beamforming, no entry for Idx =%d\n", Idx));
		return FALSE;
	}

	pEntry->BeamformEntryState = BEAMFORMING_ENTRY_STATE_PROGRESSING;
	pEntry->bSound = TRUE;
	HalComTxbf_Set(Adapter, TXBF_SET_SOUNDING_FW_NDPA, (pu1Byte)&Idx);
	
	RT_DISP(FBEAM, FBEAM_FUN, ("@%s End, Idx=0x%X \n", __FUNCTION__, Idx));
	return TRUE;
}

VOID
Beamforming_CheckSoundingSuccess(
	PADAPTER		Adapter,
	BOOLEAN			Status	
)
{
	PMGNT_INFO				pMgntInfo = &(Adapter->MgntInfo);
	PRT_BEAMFORMING_INFO 	pBeamInfo = GET_BEAMFORM_INFO(Adapter);
	PRT_BEAMFORMING_ENTRY	pEntry = &(pBeamInfo->BeamformeeEntry[pBeamInfo->BeamformeeCurIdx]);
	HAL_DATA_TYPE				*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T					pDM_Odm = &pHalData->DM_OutSrc;

	RT_DISP(FBEAM, FBEAM_FUN, ("[David]@%s Start!\n", __FUNCTION__) );

	if(Status == 1){
        if(pEntry->LogStatusFailCnt == 21)
            Beamforming_DymPeriod(pDM_Odm,Status);
        
		pEntry->LogStatusFailCnt = 0;
	}	
    else if(pEntry->LogStatusFailCnt <= 20){
		pEntry->LogStatusFailCnt++;
		RT_DISP(FBEAM, FBEAM_ERROR, ("%s LogStatusFailCnt %d\n", __FUNCTION__, pEntry->LogStatusFailCnt));
	}
	if(pEntry->LogStatusFailCnt > 20){
            pEntry->LogStatusFailCnt = 21;
		RT_DISP(FBEAM, FBEAM_ERROR, ("%s LogStatusFailCnt > 20, Stop SOUNDING\n", __FUNCTION__));
            Beamforming_DymPeriod(pDM_Odm,Status);
	}
}

VOID
phydm_Beamforming_End_SW(
	IN	PVOID		pDM_VOID,
	BOOLEAN			Status	
	)
{
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PRT_BEAMFORMING_INFO 	pBeamInfo = &pDM_Odm->BeamformingInfo;
	PRT_BEAMFORMING_ENTRY	pEntry = &(pBeamInfo->BeamformeeEntry[pBeamInfo->BeamformeeCurIdx]);

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s Start!\n", __FUNCTION__) );

	if(pEntry->BeamformEntryState != BEAMFORMING_ENTRY_STATE_PROGRESSING)
	{
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s BeamformStatus %d\n", __FUNCTION__, pEntry->BeamformEntryState));
		return;
	}

	if ((pDM_Odm->TxBfDataRate >= ODM_RATEVHTSS3MCS7) && (pDM_Odm->TxBfDataRate <= ODM_RATEVHTSS3MCS9))
	{
		ODM_RT_TRACE(pDM_Odm, BEAMFORMING_DEBUG, ODM_DBG_LOUD, ("%s, VHT3SS 7,8,9, do not apply V matrix.\n", __func__));
		pEntry->BeamformEntryState = BEAMFORMING_ENTRY_STATE_INITIALIZED;
		HalComTxbf_Set(pDM_Odm->Adapter, TXBF_SET_SOUNDING_STATUS, (pu1Byte)&(pBeamInfo->BeamformeeCurIdx));
	} else if (Status == 1) {
		pEntry->LogStatusFailCnt = 0;
		pEntry->BeamformEntryState = BEAMFORMING_ENTRY_STATE_PROGRESSED;
		HalComTxbf_Set(pDM_Odm->Adapter, TXBF_SET_SOUNDING_STATUS, (pu1Byte)&(pBeamInfo->BeamformeeCurIdx));
	}	
	else
	{
		pEntry->LogStatusFailCnt++;
		pEntry->BeamformEntryState = BEAMFORMING_ENTRY_STATE_INITIALIZED;
		HalComTxbf_Set(pDM_Odm->Adapter, TXBF_SET_TX_PATH_RESET, (pu1Byte)&(pBeamInfo->BeamformeeCurIdx));
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s LogStatusFailCnt %d\n", __FUNCTION__, pEntry->LogStatusFailCnt));
	}	
	if(pEntry->LogStatusFailCnt > 50)
	{
		RT_DISP(FBEAM, FBEAM_ERROR, ("%s LogStatusFailCnt > 50, Stop SOUNDING\n", __FUNCTION__));
		pEntry->bSound = FALSE;
		Beamforming_DeInitEntry(pDM_Odm->Adapter, pEntry->MacAddr); 

		/*Modified by David - Every action of deleting entry should follow by Notify*/
		phydm_Beamforming_Notify(pDM_Odm);
	}	
	pEntry->bBeamformingInProgress = FALSE;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s: Status=%d\n", __FUNCTION__, Status));
}	


VOID
phydm_Beamforming_End_FW(
	IN	PVOID				pDM_VOID
	)
{
	u1Byte					Idx = 0;
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PADAPTER				Adapter = pDM_Odm->Adapter;
	PRT_BEAMFORMING_INFO 	pBeamInfo = &pDM_Odm->BeamformingInfo;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s Start! \n", __FUNCTION__) );
	HalComTxbf_Set(Adapter, TXBF_SET_SOUNDING_FW_NDPA, (pu1Byte)&Idx);

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s End! \n", __FUNCTION__));
}


VOID
Beamforming_TimerCallback(
	PADAPTER			Adapter
	)
{
	BOOLEAN						ret = FALSE;
	PMGNT_INFO					pMgntInfo = &(Adapter->MgntInfo);
	PRT_BEAMFORMING_INFO 		pBeamInfo = GET_BEAMFORM_INFO(Adapter);
	PRT_BEAMFORMING_ENTRY		pEntry = &(pBeamInfo->BeamformeeEntry[pBeamInfo->BeamformeeCurIdx]);
	PRT_SOUNDING_INFO			pSoundInfo = &(pBeamInfo->SoundingInfo);
	HAL_DATA_TYPE				*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T					pDM_Odm = &pHalData->DM_OutSrc;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s Start!\n", __FUNCTION__) );

	if(pEntry->bBeamformingInProgress)
	 {
	 	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("bBeamformingInProgress, reset it\n"));
		phydm_Beamforming_End_SW(pDM_Odm, 0);
	 }

	ret = phydm_Beamforming_SelectBeamEntry(pDM_Odm, pBeamInfo);

	if(ret)
		ret = BeamformingStart_SW(Adapter,pSoundInfo->SoundIdx, pSoundInfo->SoundMode, pSoundInfo->SoundBW);
	
	if(ret)
		;
	else
	{
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s, Error value return from BeamformingStart_V2 \n", __FUNCTION__));
	}

	if(pBeamInfo->BeamformState >= BEAMFORMING_STATE_START_1BFee)
	{
		if(pSoundInfo->SoundMode == SOUNDING_SW_VHT_TIMER || pSoundInfo->SoundMode == SOUNDING_SW_HT_TIMER)
			ODM_SetTimer(pDM_Odm, &pBeamInfo->BeamformingTimer, pSoundInfo->SoundPeriod);
		else
		{
			u4Byte	val = (pSoundInfo->SoundPeriod << 16) | HAL_TIMER_TXBF;
			Adapter->HalFunc.SetHwRegHandler(Adapter, HW_VAR_HW_REG_TIMER_RESTART, (pu1Byte)(&val));
		}
	}
}


VOID
Beamforming_SWTimerCallback(
	PRT_TIMER		pTimer
	)
{
	PADAPTER	Adapter=(PADAPTER)pTimer->Adapter;

	RT_DISP(FBEAM, FBEAM_FUN, ("[David]@%s Start!\n", __FUNCTION__) );
	
	Beamforming_TimerCallback(Adapter);
}


VOID
phydm_Beamforming_Init(
	IN	PVOID				pDM_VOID
	)
{
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PRT_BEAMFORMING_INFO 		pBeamInfo = &pDM_Odm->BeamformingInfo;
	PHAL_TXBF_INFO				pTxbfInfo = &pBeamInfo->TxbfInfo;
	PRT_BEAMFORMING_OID_INFO	pBeamOidInfo = &(pBeamInfo->BeamformingOidInfo);
	
	RT_DISP(FBEAM, FBEAM_FUN, ("%s Start!\n", __FUNCTION__) );
	
	pBeamOidInfo->SoundOidMode = SOUNDING_STOP_OID_TIMER;
	RT_DISP(FBEAM, FBEAM_FUN, ("%s Mode (%d)\n", __FUNCTION__, pBeamOidInfo->SoundOidMode));
}	


VOID
Beamforming_Enter(
	PADAPTER		Adapter,
	PRT_WLAN_STA	pSTA
)
{
	u1Byte	BFerBFeeIdx = 0xff;
	
	RT_DISP(FBEAM, FBEAM_FUN, ("[David]@%s Start! \n", __FUNCTION__) );

	if(Beamforming_InitEntry(Adapter, pSTA, &BFerBFeeIdx))
		HalComTxbf_Set(Adapter, TXBF_SET_SOUNDING_ENTER, (pu1Byte)&BFerBFeeIdx);

	RT_DISP(FBEAM, FBEAM_FUN, ("[David]@%s End! \n", __FUNCTION__) );
}


VOID
Beamforming_Leave(
	PADAPTER		Adapter,
	pu1Byte			RA
	)
{
	HAL_DATA_TYPE				*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T					pDM_Odm = &pHalData->DM_OutSrc;

	RT_DISP(FBEAM, FBEAM_FUN, ("[Beamforming]@%s Start! \n", __FUNCTION__) );

	if(RA == NULL)
		Beamforming_Reset(Adapter);
	else
		Beamforming_DeInitEntry(Adapter, RA);

	phydm_Beamforming_Notify(pDM_Odm);

	RT_DISP(FBEAM, FBEAM_FUN, ("[David]@%s End!! \n", __FUNCTION__));
}

//Nobody calls this function
VOID
phydm_Beamforming_SetTxBFen(
	IN	PVOID		pDM_VOID,
	u1Byte			MacId,
	BOOLEAN			bTxBF
	)
{
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte					Idx = 0;
	PRT_BEAMFORMING_ENTRY	pEntry;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s Start!\n", __FUNCTION__) );

	pEntry = phydm_Beamforming_GetEntryByMacId(pDM_Odm, MacId, &Idx);

	if(pEntry == NULL)
		return;
	else
		pEntry->bTxBF = bTxBF;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s MacId %d TxBF %d\n", __FUNCTION__, pEntry->MacId, pEntry->bTxBF));

	phydm_Beamforming_Notify(pDM_Odm);
}


BEAMFORMING_CAP
phydm_Beamforming_GetBeamCap(
	IN	PVOID		pDM_VOID,
	IN PRT_BEAMFORMING_INFO 	pBeamInfo
	)
{
	u1Byte					i;
	BOOLEAN 				bSelfBeamformer = FALSE;
	BOOLEAN 				bSelfBeamformee = FALSE;
	RT_BEAMFORMING_ENTRY	BeamformeeEntry;
	RT_BEAMFORMER_ENTRY	BeamformerEntry;
	BEAMFORMING_CAP 		BeamformCap = BEAMFORMING_CAP_NONE;
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[Beamforming]%s Start!\n", __FUNCTION__) );

	/*
	for(i = 0; i < BEAMFORMEE_ENTRY_NUM; i++)
	{
		BeamformEntry = pBeamInfo->BeamformeeEntry[i];

		if(BeamformEntry.bUsed)
		{
			if( (BeamformEntry.BeamformEntryCap & BEAMFORMEE_CAP_VHT_SU) ||
				(BeamformEntry.BeamformEntryCap & BEAMFORMEE_CAP_HT_EXPLICIT))
				bSelfBeamformee = TRUE;
			if( (BeamformEntry.BeamformEntryCap & BEAMFORMER_CAP_VHT_SU) ||
				(BeamformEntry.BeamformEntryCap & BEAMFORMER_CAP_HT_EXPLICIT))
				bSelfBeamformer = TRUE;
		}

		if(bSelfBeamformer && bSelfBeamformee)
			i = BEAMFORMEE_ENTRY_NUM;
	}
	*/

	for(i = 0; i < BEAMFORMEE_ENTRY_NUM; i++)
	{
		BeamformeeEntry = pBeamInfo->BeamformeeEntry[i];

		if(BeamformeeEntry.bUsed)
		{
			bSelfBeamformer = TRUE;
			ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[Beamforming]%s: BFee entry %d bUsed=TRUE \n", __FUNCTION__, i));
			break;
		}
		
	}

	for(i = 0; i < BEAMFORMER_ENTRY_NUM; i++)
	{
		BeamformerEntry = pBeamInfo->BeamformerEntry[i];

		if(BeamformerEntry.bUsed)
		{
			bSelfBeamformee = TRUE;
			ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[Beamforming]%s: BFer entry %d bUsed=TRUE \n", __FUNCTION__, i));
			break;
		}
	}

	if(bSelfBeamformer)
		BeamformCap = (BEAMFORMING_CAP)(BeamformCap | BEAMFORMER_CAP);
	if(bSelfBeamformee)
		BeamformCap = (BEAMFORMING_CAP)(BeamformCap | BEAMFORMEE_CAP);

	return BeamformCap;
}


BOOLEAN
BeamformingControl_V1(
	PADAPTER		Adapter,
	pu1Byte			RA,
	u1Byte			AID,
	u1Byte			Mode, 
	CHANNEL_WIDTH	BW,
	u1Byte			Rate
	)
{
	BOOLEAN		ret = TRUE;
	HAL_DATA_TYPE			*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T				pDM_Odm = &pHalData->DM_OutSrc;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s Start!\n", __FUNCTION__) );

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("AID (%d), Mode (%d), BW (%d)\n", AID, Mode, BW));

	RT_DISP_ADDR(FBEAM, FBEAM_FUN, ("Addr = "), RA);

	switch(Mode){	
	case 0:
		ret = BeamformingStart_V1(Adapter, RA, 0, BW, Rate);
		break;
	case 1:
		ret = BeamformingStart_V1(Adapter, RA, 1, BW, Rate);
		break;
	case 2:
		phydm_Beamforming_NDPARate(pDM_Odm, BW, Rate);
		ret = Beamforming_SendVHTNDPAPacket(Adapter,RA, AID, BW, NORMAL_QUEUE);
		break;
	case 3:
		phydm_Beamforming_NDPARate(pDM_Odm, BW, Rate);
		ret = Beamforming_SendHTNDPAPacket(Adapter, RA, BW, NORMAL_QUEUE);
		break;
	}
	return ret;
}

//Only OID uses this function
BOOLEAN
phydm_BeamformingControl_V2(
	IN	PVOID		pDM_VOID,
	u1Byte			Idx,
	u1Byte			Mode, 
	CHANNEL_WIDTH	BW,
	u2Byte			Period
	)
{
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PRT_BEAMFORMING_INFO		pBeamInfo =  &pDM_Odm->BeamformingInfo;
	PRT_BEAMFORMING_OID_INFO	pBeamOidInfo = &(pBeamInfo->BeamformingOidInfo);


	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s Start!\n", __FUNCTION__) );
	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("Idx (%d), Mode (%d), BW (%d), Period (%d)\n", Idx, Mode, BW, Period));

	pBeamOidInfo->SoundOidIdx = Idx;
	pBeamOidInfo->SoundOidMode = (SOUNDING_MODE) Mode;
	pBeamOidInfo->SoundOidBW = BW;
	pBeamOidInfo->SoundOidPeriod = Period;

	phydm_Beamforming_Notify(pDM_Odm);

	return TRUE;
}


VOID
phydm_Beamforming_Watchdog(
	IN	PVOID		pDM_VOID
)
{
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PRT_BEAMFORMING_INFO 		pBeamInfo = &pDM_Odm->BeamformingInfo;
	PADAPTER					Adapter = pDM_Odm->Adapter;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s Start!\n", __FUNCTION__) );

	if(pBeamInfo->BeamformState < BEAMFORMING_STATE_START_1BFee)
		return;

	Beamforming_DymPeriod(pDM_Odm,0);
	phydm_Beamforming_DymNDPARate(pDM_Odm);

}


#endif
