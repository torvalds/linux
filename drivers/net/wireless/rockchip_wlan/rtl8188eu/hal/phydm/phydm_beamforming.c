#include "mp_precomp.h"
#include "phydm_precomp.h"

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
#if WPP_SOFTWARE_TRACE
#include "phydm_beamforming.tmh"
#endif
#endif

#if (BEAMFORMING_SUPPORT == 1)

PRT_BEAMFORM_STAINFO
phydm_staInfoInit(
	IN PDM_ODM_T		pDM_Odm,
	IN u2Byte			staIdx
	)
{
	PRT_BEAMFORMING_INFO		pBeamInfo = &pDM_Odm->BeamformingInfo;
	PRT_BEAMFORM_STAINFO		pEntry = &(pBeamInfo->BeamformSTAinfo);
	PSTA_INFO_T					pSTA = pDM_Odm->pODM_StaInfo[staIdx];
	PADAPTER					Adapter = pDM_Odm->Adapter;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PMGNT_INFO					pMgntInfo = &Adapter->MgntInfo;
	PRT_HIGH_THROUGHPUT		pHTInfo = GET_HT_INFO(pMgntInfo);
	PRT_VERY_HIGH_THROUGHPUT	pVHTInfo = GET_VHT_INFO(pMgntInfo);

	ODM_MoveMemory(pDM_Odm, pEntry->MyMacAddr, Adapter->CurrentAddress, 6);
	
	pEntry->HtBeamformCap = pHTInfo->HtBeamformCap;
	pEntry->VhtBeamformCap = pVHTInfo->VhtBeamformCap;

	/*IBSS, AP mode*/
	if (staIdx != 0) {
		pEntry->AID = pSTA->AID;
		pEntry->RA = pSTA->MacAddr;
		pEntry->MacID = pSTA->AssociatedMacId;
		pEntry->WirelessMode = pSTA->WirelessMode;
		pEntry->BW = pSTA->BandWidth;
		pEntry->CurBeamform = pSTA->HTInfo.HtCurBeamform;
	} else {/*client mode*/
		pEntry->AID = pMgntInfo->mAId;
		pEntry->RA = pMgntInfo->Bssid;
		pEntry->MacID = pMgntInfo->mMacId;
		pEntry->WirelessMode = pMgntInfo->dot11CurrentWirelessMode;
		pEntry->BW = pMgntInfo->dot11CurrentChannelBandWidth;
		pEntry->CurBeamform = pHTInfo->HtCurBeamform;
	}	

	if ((pEntry->WirelessMode & WIRELESS_MODE_AC_5G) || (pEntry->WirelessMode & WIRELESS_MODE_AC_24G)) {
		if (staIdx != 0)
			pEntry->CurBeamformVHT = pSTA->VHTInfo.VhtCurBeamform;
		else
			pEntry->CurBeamformVHT = pVHTInfo->VhtCurBeamform;	
		}
	
	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("pSTA->wireless_mode = 0x%x, staidx = %d\n", pSTA->WirelessMode, staIdx));
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)

	if (!IS_STA_VALID(pSTA)) {
		rtw_warn_on(1);
		DBG_871X("%s => sta_info(mac_id:%d) failed\n", __func__, staIdx);
		return pEntry;
	}
	
	ODM_MoveMemory(pDM_Odm, pEntry->MyMacAddr, adapter_mac_addr(pSTA->padapter), 6);
	pEntry->HtBeamformCap = pSTA->htpriv.beamform_cap;

	pEntry->AID = pSTA->aid;
	pEntry->RA = pSTA->hwaddr;
	pEntry->MacID = pSTA->mac_id;
	pEntry->WirelessMode = pSTA->wireless_mode;
	pEntry->BW = pSTA->bw_mode;

	pEntry->CurBeamform = pSTA->htpriv.beamform_cap;
#if	ODM_IC_11AC_SERIES_SUPPORT
	if ((pEntry->WirelessMode & WIRELESS_MODE_AC_5G) || (pEntry->WirelessMode & WIRELESS_MODE_AC_24G)) {
		pEntry->CurBeamformVHT = pSTA->vhtpriv.beamform_cap;
		pEntry->VhtBeamformCap = pSTA->vhtpriv.beamform_cap;
	}
#endif
	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("pSTA->wireless_mode = 0x%x, staidx = %d\n", pSTA->wireless_mode, staIdx));
#endif
	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("pEntry->CurBeamform = 0x%x, pEntry->CurBeamformVHT = 0x%x\n", pEntry->CurBeamform, pEntry->CurBeamformVHT));
	return pEntry;

}
void phydm_staInfoUpdate(
	IN PDM_ODM_T			pDM_Odm,
	IN u2Byte				staIdx,
	PRT_BEAMFORMEE_ENTRY	pBeamformEntry
	)
{
	PSTA_INFO_T pSTA = pDM_Odm->pODM_StaInfo[staIdx];
	
	if (!IS_STA_VALID(pSTA))
		return;
	
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	pSTA->txbf_paid = pBeamformEntry->P_AID;
	pSTA->txbf_gid = pBeamformEntry->G_ID;
#endif	
}
	

u1Byte
Beamforming_GetHTNDPTxRate(
	IN	PVOID	pDM_VOID,
	u1Byte	CompSteeringNumofBFer
)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte Nr_index = 0;
	u1Byte NDPTxRate;
	/*Find Nr*/
	
	if (pDM_Odm->SupportICType & ODM_RTL8814A)
		Nr_index = TxBF_Nr(halTxbf8814A_GetNtx(pDM_Odm), CompSteeringNumofBFer);
	else
		Nr_index = TxBF_Nr(1, CompSteeringNumofBFer);
	
	switch (Nr_index) {
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
	IN	PVOID	pDM_VOID,
	u1Byte	CompSteeringNumofBFer
)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte Nr_index = 0;
	u1Byte NDPTxRate;
	/*Find Nr*/
	if (pDM_Odm->SupportICType & ODM_RTL8814A)
		Nr_index = TxBF_Nr(halTxbf8814A_GetNtx(pDM_Odm), CompSteeringNumofBFer);
	else
		Nr_index = TxBF_Nr(1, CompSteeringNumofBFer);
	
	switch (Nr_index) {
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


PRT_BEAMFORMEE_ENTRY
phydm_Beamforming_GetBFeeEntryByAddr(
	IN	PVOID		pDM_VOID,
	IN	pu1Byte		RA,
	OUT	pu1Byte		Idx
	)
{
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte	i = 0;
	PRT_BEAMFORMING_INFO pBeamInfo = &pDM_Odm->BeamformingInfo;
	
	for (i = 0; i < BEAMFORMEE_ENTRY_NUM; i++) {
		if (pBeamInfo->BeamformeeEntry[i].bUsed && (eqMacAddr(RA, pBeamInfo->BeamformeeEntry[i].MacAddr))) {
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
	OUT	pu1Byte	Idx
	)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte		i = 0;
	PRT_BEAMFORMING_INFO	pBeamInfo = &pDM_Odm->BeamformingInfo;
	
	for (i = 0; i < BEAMFORMER_ENTRY_NUM; i++) {
		if (pBeamInfo->BeamformerEntry[i].bUsed &&  (eqMacAddr(TA, pBeamInfo->BeamformerEntry[i].MacAddr))) {
			*Idx = i;
			return &(pBeamInfo->BeamformerEntry[i]);
		}
	}

	return NULL;
}


PRT_BEAMFORMEE_ENTRY
phydm_Beamforming_GetEntryByMacId(
	IN	PVOID		pDM_VOID,
	IN	u1Byte		MacId,
	OUT	pu1Byte		Idx
	)
{
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte	i = 0;
	PRT_BEAMFORMING_INFO pBeamInfo = &pDM_Odm->BeamformingInfo;
	
	for (i = 0; i < BEAMFORMEE_ENTRY_NUM; i++) {
		if (pBeamInfo->BeamformeeEntry[i].bUsed && (MacId == pBeamInfo->BeamformeeEntry[i].MacId)) {
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
	
	for (i = 0; i < BEAMFORMEE_ENTRY_NUM; i++) {
		if (pBeamInfo->BeamformeeEntry[i].bUsed && (MacId == pBeamInfo->BeamformeeEntry[i].MacId)) {
			BeamformEntryCap =  pBeamInfo->BeamformeeEntry[i].BeamformEntryCap;
			i = BEAMFORMEE_ENTRY_NUM;
		}
	}

	return BeamformEntryCap;
}


PRT_BEAMFORMEE_ENTRY
phydm_Beamforming_GetFreeBFeeEntry(
	IN	PVOID		pDM_VOID,
	OUT	pu1Byte		Idx
	)
{
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte	i = 0;
	PRT_BEAMFORMING_INFO pBeamInfo = &pDM_Odm->BeamformingInfo;

	for (i = 0; i < BEAMFORMEE_ENTRY_NUM; i++) {
		if (pBeamInfo->BeamformeeEntry[i].bUsed == FALSE) {
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

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s ===>\n", __func__));

	for (i = 0; i < BEAMFORMER_ENTRY_NUM; i++) {
		if (pBeamInfo->BeamformerEntry[i].bUsed == FALSE) {
			*Idx = i;
			return &(pBeamInfo->BeamformerEntry[i]);
		}	
	}
	return NULL;
}

/*
// Description: Get the first entry index of MU Beamformee.
//
// Return Value: Index of the first MU sta.
//
// 2015.05.25. Created by tynli.
//
*/
u1Byte
phydm_Beamforming_GetFirstMUBFeeEntryIdx(
	IN	PVOID		pDM_VOID
	)
{
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte					idx = 0xFF;
	PRT_BEAMFORMING_INFO	pBeamInfo = &pDM_Odm->BeamformingInfo;
	BOOLEAN					bFound = FALSE;

	for (idx = 0; idx < BEAMFORMEE_ENTRY_NUM; idx++) {
		if (pBeamInfo->BeamformeeEntry[idx].bUsed && pBeamInfo->BeamformeeEntry[idx].is_mu_sta) {			
			ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] idx=%d!\n", __func__, idx));
			bFound = TRUE;
			break;
		}	
	}

	if (!bFound)
		idx = 0xFF;

	return idx;
}


/*Add SU BFee and MU BFee*/
PRT_BEAMFORMEE_ENTRY
Beamforming_AddBFeeEntry(
	IN	PVOID				pDM_VOID,
	IN	PRT_BEAMFORM_STAINFO	pSTA,
	IN	BEAMFORMING_CAP	BeamformCap,
	IN	u1Byte				NumofSoundingDim,
	IN	u1Byte				CompSteeringNumofBFer,
	OUT	pu1Byte				Idx
	)
{
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PRT_BEAMFORMEE_ENTRY	pEntry = phydm_Beamforming_GetFreeBFeeEntry(pDM_Odm, Idx);

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s Start!\n", __func__));

	if (pEntry != NULL) {	
		pEntry->bUsed = TRUE;
		pEntry->AID = pSTA->AID;
		pEntry->MacId = pSTA->MacID;
		pEntry->SoundBW = pSTA->BW;
		ODM_MoveMemory(pDM_Odm, pEntry->MyMacAddr, pSTA->MyMacAddr, 6);
		
		if (phydm_actingDetermine(pDM_Odm, PhyDM_ACTING_AS_AP)) {
			/*BSSID[44:47] xor BSSID[40:43]*/
			u2Byte BSSID = ((pSTA->MyMacAddr[5] & 0xf0) >> 4) ^ (pSTA->MyMacAddr[5] & 0xf);
			/*(dec(A) + dec(B)*32) mod 512*/
			pEntry->P_AID = (pSTA->AID + BSSID * 32) & 0x1ff;
			pEntry->G_ID = 63;
			ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s: BFee P_AID addressed to STA=%d\n", __func__, pEntry->P_AID));
		} else if (phydm_actingDetermine(pDM_Odm, PhyDM_ACTING_AS_IBSS)) {
			/*ad hoc mode*/
			pEntry->P_AID = 0;
			pEntry->G_ID = 63;
			ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s: BFee P_AID as IBSS=%d\n", __func__, pEntry->P_AID));
		} else {
			/*client mode*/
			pEntry->P_AID =  pSTA->RA[5];
			/*BSSID[39:47]*/
			pEntry->P_AID = (pEntry->P_AID << 1) | (pSTA->RA[4] >> 7);
			pEntry->G_ID = 0;
			ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s: BFee P_AID addressed to AP=0x%X\n", __func__, pEntry->P_AID));
		}
		cpMacAddr(pEntry->MacAddr, pSTA->RA);
		pEntry->bTxBF = FALSE;
		pEntry->bSound = FALSE;
		pEntry->SoundPeriod = 400;
		pEntry->BeamformEntryCap = BeamformCap;
		pEntry->BeamformEntryState = BEAMFORMING_ENTRY_STATE_UNINITIALIZE;

/*		pEntry->LogSeq = 0xff;				Move to Beamforming_AddBFerEntry*/
/*		pEntry->LogRetryCnt = 0;			Move to Beamforming_AddBFerEntry*/
/*		pEntry->LogSuccessCnt = 0;		Move to Beamforming_AddBFerEntry*/

		pEntry->LogStatusFailCnt = 0;

		pEntry->NumofSoundingDim = NumofSoundingDim;
		pEntry->CompSteeringNumofBFer = CompSteeringNumofBFer;

		if (BeamformCap & BEAMFORMER_CAP_VHT_MU) {
			pDM_Odm->BeamformingInfo.beamformee_mu_cnt += 1;
			pEntry->is_mu_sta = TRUE;
			pDM_Odm->BeamformingInfo.FirstMUBFeeIndex = phydm_Beamforming_GetFirstMUBFeeEntryIdx(pDM_Odm);
		} else if  (BeamformCap & BEAMFORMER_CAP_VHT_SU) {
			pDM_Odm->BeamformingInfo.beamformee_su_cnt += 1;
			pEntry->is_mu_sta = FALSE;
		}

		return pEntry;
	}
	else
		return NULL;
}

/*Add SU BFee and MU BFer*/
PRT_BEAMFORMER_ENTRY
Beamforming_AddBFerEntry(
	IN	PVOID				pDM_VOID,
	IN	PRT_BEAMFORM_STAINFO	pSTA,
	IN	BEAMFORMING_CAP	BeamformCap,
	IN	u1Byte				NumofSoundingDim,
	OUT	pu1Byte				Idx
	)
{
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PRT_BEAMFORMER_ENTRY	pEntry = phydm_Beamforming_GetFreeBFerEntry(pDM_Odm, Idx);

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s Start!\n", __func__));

	if (pEntry != NULL) {
		pEntry->bUsed = TRUE;
		ODM_MoveMemory(pDM_Odm, pEntry->MyMacAddr, pSTA->MyMacAddr, 6);
		if (phydm_actingDetermine(pDM_Odm, PhyDM_ACTING_AS_AP)) {
			/*BSSID[44:47] xor BSSID[40:43]*/
			u2Byte BSSID = ((pSTA->MyMacAddr[5] & 0xf0) >> 4) ^ (pSTA->MyMacAddr[5] & 0xf);
			
			pEntry->P_AID = (pSTA->AID + BSSID * 32) & 0x1ff;
			pEntry->G_ID = 63;
			/*(dec(A) + dec(B)*32) mod 512*/
		} else if (phydm_actingDetermine(pDM_Odm, PhyDM_ACTING_AS_IBSS)) {
			pEntry->P_AID = 0;
			pEntry->G_ID = 63;
		} else {
			pEntry->P_AID =  pSTA->RA[5];
			/*BSSID[39:47]*/
			pEntry->P_AID = (pEntry->P_AID << 1) | (pSTA->RA[4] >> 7);
			pEntry->G_ID = 0;
			ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s: P_AID addressed to AP=0x%X\n", __func__, pEntry->P_AID));
		}
		
		cpMacAddr(pEntry->MacAddr, pSTA->RA);
		pEntry->BeamformEntryCap = BeamformCap;

		pEntry->PreLogSeq = 0;	/*Modified by Jeffery @2015-04-13*/
		pEntry->LogSeq = 0;		/*Modified by Jeffery @2014-10-29*/
		pEntry->LogRetryCnt = 0;	/*Modified by Jeffery @2014-10-29*/
		pEntry->LogSuccess = 0;	/*LogSuccess is NOT needed to be accumulated, so  LogSuccessCnt->LogSuccess, 2015-04-13, Jeffery*/
		pEntry->ClockResetTimes = 0;	/*Modified by Jeffery @2015-04-13*/

		pEntry->NumofSoundingDim = NumofSoundingDim;

		if (BeamformCap & BEAMFORMEE_CAP_VHT_MU) {
			pDM_Odm->BeamformingInfo.beamformer_mu_cnt += 1;
			pEntry->is_mu_ap = TRUE;
			pEntry->AID = pSTA->AID;
		} else if (BeamformCap & BEAMFORMEE_CAP_VHT_SU) {
			pDM_Odm->BeamformingInfo.beamformer_su_cnt += 1;
			pEntry->is_mu_ap = FALSE;
		}

		return pEntry;
	}
	else
		return NULL;
}

#if 0
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
	PRT_BEAMFORMEE_ENTRY	pEntry = phydm_Beamforming_GetBFeeEntryByAddr(pDM_Odm, RA, Idx);
	BOOLEAN ret = FALSE;
    
	RT_DISP(FBEAM, FBEAM_FUN, ("[Beamforming]@%s Start!\n", __func__));
	RT_DISP(FBEAM, FBEAM_FUN, ("[Beamforming]@%s, pBFerEntry=0x%x\n", __func__, pBFerEntry));
	RT_DISP(FBEAM, FBEAM_FUN, ("[Beamforming]@%s, pEntry=0x%x\n", __func__, pEntry));
	
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
#endif

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
	
	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s Start!\n", __func__));

	if (NDPARate == 0) {
		if(pDM_Odm->RSSI_Min > 30) // link RSSI > 30%
			NDPARate = ODM_RATE24M;
		else
			NDPARate = ODM_RATE6M;
	}

	if (NDPARate < ODM_RATEMCS0)
		BW = (CHANNEL_WIDTH)ODM_BW20M;

	NDPARate = (NDPARate << 8) | BW;
	HalComTxbf_Set(pDM_Odm, TXBF_SET_SOUNDING_RATE, (pu1Byte)&NDPARate);

}


/* Used for BeamformingStart_SW and  BeamformingStart_FW */
VOID
phydm_Beamforming_DymNDPARate(
	IN	PVOID		pDM_VOID
)
{
	u2Byte			NDPARate = ODM_RATE6M, BW;
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s Start!\n", __func__));

	if (pDM_Odm->RSSI_Min > 30)	/*link RSSI > 30%*/
		NDPARate = ODM_RATE24M;
	else
		NDPARate = ODM_RATE6M;

	BW = ODM_BW20M;
	NDPARate = NDPARate << 8 | BW;
	HalComTxbf_Set(pDM_Odm, TXBF_SET_SOUNDING_RATE, (pu1Byte)&NDPARate);
	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s End, NDPA Rate = 0x%X\n", __func__, NDPARate));
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

	PRT_BEAMFORMEE_ENTRY	pBeamformEntry;
	PRT_BEAMFORMING_INFO	pBeamInfo = &(pDM_Odm->BeamformingInfo);
	PRT_SOUNDING_INFO		pSoundInfo = &(pBeamInfo->SoundingInfo);

	PRT_BEAMFORMEE_ENTRY	pEntry = &(pBeamInfo->BeamformeeEntry[pBeamInfo->BeamformeeCurIdx]);

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] Start!\n", __func__));
	
	//3 TODO  per-client throughput caculation.

	if ((*(pDM_Odm->pCurrentTxTP) + *(pDM_Odm->pCurrentRxTP) > 2) && ((pEntry->LogStatusFailCnt <= 20) || status)) {
		SoundPeriod_SW = 40;	/* 40ms */
		SoundPeriod_FW = 40;	/* From  H2C cmd, unit = 10ms */
	} else {
		SoundPeriod_SW = 4000;/* 4s */
		SoundPeriod_FW = 400;
	}
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s]SoundPeriod_SW=%d, SoundPeriod_FW=%d\n",	__func__, SoundPeriod_SW, SoundPeriod_FW));

	for (Idx = 0; Idx < BEAMFORMEE_ENTRY_NUM; Idx++) {
		pBeamformEntry = pBeamInfo->BeamformeeEntry+Idx;
		
		if (pBeamformEntry->DefaultCSICnt > 20) {
			/*Modified by David*/
			SoundPeriod_SW = 4000;
			SoundPeriod_FW = 400;
		}
		
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] Period = %d\n", __func__, SoundPeriod_SW));		
		if (pBeamformEntry->BeamformEntryCap & (BEAMFORMER_CAP_HT_EXPLICIT | BEAMFORMER_CAP_VHT_SU)) {
			if (pSoundInfo->SoundMode == SOUNDING_FW_VHT_TIMER || pSoundInfo->SoundMode == SOUNDING_FW_HT_TIMER) {				
				if (pBeamformEntry->SoundPeriod != SoundPeriod_FW) {
					pBeamformEntry->SoundPeriod = SoundPeriod_FW;
					bChangePeriod = TRUE;		/*Only FW sounding need to send H2C packet to change sound period. */
				}
			} else if (pBeamformEntry->SoundPeriod != SoundPeriod_SW) {
				pBeamformEntry->SoundPeriod = SoundPeriod_SW;
			}
		}
	}

	if (bChangePeriod)
		HalComTxbf_Set(pDM_Odm, TXBF_SET_SOUNDING_FW_NDPA, (pu1Byte)&Idx);
}




BOOLEAN
Beamforming_SendHTNDPAPacket(
	IN	PVOID			pDM_VOID,
	IN	pu1Byte			RA,
	IN	CHANNEL_WIDTH	BW,
	IN	u1Byte			QIdx
	)
{
	BOOLEAN		ret = TRUE;
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;

	if (QIdx == BEACON_QUEUE)
		ret = SendFWHTNDPAPacket(pDM_Odm, RA, BW);
	else
		ret = SendSWHTNDPAPacket(pDM_Odm, RA, BW);

	return ret;
}



BOOLEAN
Beamforming_SendVHTNDPAPacket(
	IN	PVOID			pDM_VOID,
	IN	pu1Byte			RA,
	IN	u2Byte			AID,
	IN	CHANNEL_WIDTH	BW,
	IN	u1Byte			QIdx
	)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	BOOLEAN		ret = TRUE;
	PRT_BEAMFORMING_INFO	pBeamInfo = &(pDM_Odm->BeamformingInfo);

	HalComTxbf_Set(pDM_Odm, TXBF_SET_GET_TX_RATE, NULL);

	if ((pDM_Odm->TxBfDataRate >= ODM_RATEVHTSS3MCS7) && (pDM_Odm->TxBfDataRate <= ODM_RATEVHTSS3MCS9)) {
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("@%s: 3SS VHT 789 don't sounding\n", __func__));

	} else  {
		if (QIdx == BEACON_QUEUE) /* Send to reserved page => FW NDPA */
			ret = SendFWVHTNDPAPacket(pDM_Odm, RA, AID, BW);
		else {
#ifdef SUPPORT_MU_BF
		#if (SUPPORT_MU_BF == 1)
			pBeamInfo->is_mu_sounding = TRUE;
			ret = SendSWVHTMUNDPAPacket(pDM_Odm, BW);
		#else
			pBeamInfo->is_mu_sounding = FALSE;
			ret = SendSWVHTNDPAPacket(pDM_Odm, RA, AID, BW);
		#endif
#else
			pBeamInfo->is_mu_sounding = FALSE;
			ret = SendSWVHTNDPAPacket(pDM_Odm, RA, AID, BW);
#endif
		}
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

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s Start!\n", __func__));

	/*if(( Beamforming_GetBeamCap(pBeamInfo) & BEAMFORMER_CAP) == 0)*/
	/*bSounding = BEAMFORMING_NOTIFY_RESET;*/
	if (BeamOidInfo.SoundOidMode == SOUNDING_STOP_All_TIMER)
		bSounding = BEAMFORMING_NOTIFY_RESET;
	else {
		u1Byte i;

		for (i = 0 ; i < BEAMFORMEE_ENTRY_NUM ; i++) {
			ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("@%s: BFee Entry %d bUsed=%d, bSound=%d\n", __func__, i, pBeamInfo->BeamformeeEntry[i].bUsed, pBeamInfo->BeamformeeEntry[i].bSound));
			if (pBeamInfo->BeamformeeEntry[i].bUsed && (!pBeamInfo->BeamformeeEntry[i].bSound)) {
				ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s: Add BFee entry %d\n", __func__, i));
				*Idx = i;
				if (pBeamInfo->BeamformeeEntry[i].is_mu_sta)
					bSounding = BEAMFORMEE_NOTIFY_ADD_MU;
				else
					bSounding = BEAMFORMEE_NOTIFY_ADD_SU;
			}

			if ((!pBeamInfo->BeamformeeEntry[i].bUsed) && pBeamInfo->BeamformeeEntry[i].bSound) {
				ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s: Delete BFee entry %d\n", __func__, i));
				*Idx = i;
				if (pBeamInfo->BeamformeeEntry[i].is_mu_sta)
					bSounding = BEAMFORMEE_NOTIFY_DELETE_MU;
				else
					bSounding = BEAMFORMEE_NOTIFY_DELETE_SU;
			}
		}
	}

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s End, bSounding = %d\n", __func__, bSounding));
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
	RT_BEAMFORMEE_ENTRY	BeamEntry;
	RT_BEAMFORMING_OID_INFO	BeamOidInfo = pBeamInfo->BeamformingOidInfo;
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s Start!\n", __func__));

	if (BeamOidInfo.SoundOidMode == SOUNDING_SW_HT_TIMER || BeamOidInfo.SoundOidMode == SOUNDING_SW_VHT_TIMER ||
		BeamOidInfo.SoundOidMode == SOUNDING_HW_HT_TIMER || BeamOidInfo.SoundOidMode == SOUNDING_HW_VHT_TIMER)
		Idx = BeamOidInfo.SoundOidIdx;
	else {
		u1Byte	i;
		for (i = 0; i < BEAMFORMEE_ENTRY_NUM; i++) {
			if (pBeamInfo->BeamformeeEntry[i].bUsed && (FALSE == pBeamInfo->BeamformeeEntry[i].bSound)) {
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

	RT_BEAMFORMEE_ENTRY		BeamEntry = pBeamInfo->BeamformeeEntry[Idx];
	RT_BEAMFORMING_OID_INFO	BeamOidInfo = pBeamInfo->BeamformingOidInfo;
	SOUNDING_MODE				Mode = BeamOidInfo.SoundOidMode;

	if (BeamOidInfo.SoundOidMode == SOUNDING_SW_VHT_TIMER || BeamOidInfo.SoundOidMode == SOUNDING_HW_VHT_TIMER) {
		if (BeamEntry.BeamformEntryCap & BEAMFORMER_CAP_VHT_SU)
			Mode = BeamOidInfo.SoundOidMode;
		else 
			Mode = SOUNDING_STOP_All_TIMER;
	} else if (BeamOidInfo.SoundOidMode == SOUNDING_SW_HT_TIMER || BeamOidInfo.SoundOidMode == SOUNDING_HW_HT_TIMER) {
		if (BeamEntry.BeamformEntryCap & BEAMFORMER_CAP_HT_EXPLICIT)
			Mode = BeamOidInfo.SoundOidMode;
		else
			Mode = SOUNDING_STOP_All_TIMER;
	} else if (BeamEntry.BeamformEntryCap & BEAMFORMER_CAP_VHT_SU) {
		if ((SupportInterface == ODM_ITRF_USB) && !(pDM_Odm->SupportICType & (ODM_RTL8814A | ODM_RTL8822B)))
			Mode = SOUNDING_FW_VHT_TIMER;
		else
			Mode = SOUNDING_SW_VHT_TIMER;
	} else if (BeamEntry.BeamformEntryCap & BEAMFORMER_CAP_HT_EXPLICIT) {
		if ((SupportInterface == ODM_ITRF_USB) && !(pDM_Odm->SupportICType & (ODM_RTL8814A | ODM_RTL8822B)))
			Mode = SOUNDING_FW_HT_TIMER;
		else
			Mode = SOUNDING_SW_HT_TIMER;
	} else 
		Mode = SOUNDING_STOP_All_TIMER;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] SupportInterface=%d, Mode=%d\n", __func__, SupportInterface, Mode));

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
	RT_BEAMFORMEE_ENTRY		BeamEntry = pBeamInfo->BeamformeeEntry[Idx];
	RT_BEAMFORMING_OID_INFO	BeamOidInfo = pBeamInfo->BeamformingOidInfo;
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s Start!\n", __func__));

	if (Mode == SOUNDING_HW_HT_TIMER || Mode == SOUNDING_HW_VHT_TIMER)
		SoundingTime = BeamOidInfo.SoundOidPeriod * 32;
	else if (Mode == SOUNDING_SW_HT_TIMER || Mode == SOUNDING_SW_VHT_TIMER)
		/*Modified by David*/
		SoundingTime = BeamEntry.SoundPeriod;	/*BeamOidInfo.SoundOidPeriod;*/
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
	RT_BEAMFORMEE_ENTRY		BeamEntry = pBeamInfo->BeamformeeEntry[Idx];
	RT_BEAMFORMING_OID_INFO	BeamOidInfo = pBeamInfo->BeamformingOidInfo;
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;

	if (Mode == SOUNDING_HW_HT_TIMER || Mode == SOUNDING_HW_VHT_TIMER)
		SoundingBW = BeamOidInfo.SoundOidBW;
	else if (Mode == SOUNDING_SW_HT_TIMER || Mode == SOUNDING_SW_VHT_TIMER)
		/*Modified by David*/
		SoundingBW = BeamEntry.SoundBW;		/*BeamOidInfo.SoundOidBW;*/
	else 
		SoundingBW = BeamEntry.SoundBW;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s, SoundingBW=0x%X\n", __func__, SoundingBW));

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

	/*pEntry.bSound is different between first and latter NDPA, and should not be used as BFee entry selection*/
	/*BTW, latter modification should sync to the selection mechanism of AP/ADSL instead of the fixed SoundIdx.*/
	pSoundInfo->SoundIdx = phydm_beamforming_SoundingIdx(pDM_Odm, pBeamInfo);
	/*pSoundInfo->SoundIdx = 0;*/

	if (pSoundInfo->SoundIdx < BEAMFORMEE_ENTRY_NUM)
		pSoundInfo->SoundMode = phydm_beamforming_SoundingMode(pDM_Odm, pBeamInfo, pSoundInfo->SoundIdx);
	else
		pSoundInfo->SoundMode = SOUNDING_STOP_All_TIMER;
	
	if (SOUNDING_STOP_All_TIMER == pSoundInfo->SoundMode) {
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] Return because of SOUNDING_STOP_All_TIMER\n", __func__));
		return FALSE;
	} else {
		pSoundInfo->SoundBW = phydm_beamforming_SoundingBW(pDM_Odm, pBeamInfo, pSoundInfo->SoundMode, pSoundInfo->SoundIdx );
		pSoundInfo->SoundPeriod = phydm_beamforming_SoundingTime(pDM_Odm, pBeamInfo, pSoundInfo->SoundMode, pSoundInfo->SoundIdx );
		return TRUE;
	}
}

/*SU BFee Entry Only*/
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
	
	phydm_Beamforming_DymNDPARate(pDM_Odm);

	phydm_Beamforming_SelectBeamEntry(pDM_Odm, pBeamInfo);		// Modified

	if (pSoundInfo->SoundMode == SOUNDING_SW_VHT_TIMER || pSoundInfo->SoundMode == SOUNDING_SW_HT_TIMER)
		ODM_SetTimer(pDM_Odm, &pBeamInfo->BeamformingTimer, pSoundInfo->SoundPeriod);
	else if (pSoundInfo->SoundMode == SOUNDING_HW_VHT_TIMER || pSoundInfo->SoundMode == SOUNDING_HW_HT_TIMER ||
			pSoundInfo->SoundMode == SOUNDING_AUTO_VHT_TIMER || pSoundInfo->SoundMode == SOUNDING_AUTO_HT_TIMER) {
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
	} else if (pSoundInfo->SoundMode == SOUNDING_FW_VHT_TIMER || pSoundInfo->SoundMode == SOUNDING_FW_HT_TIMER)
		Ret = BeamformingStart_FW(pDM_Odm, pSoundInfo->SoundIdx);
	else
		Ret = FALSE;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] SoundIdx=%d, SoundMode=%d, SoundBW=%d, SoundPeriod=%d\n", __func__, 
			pSoundInfo->SoundIdx, pSoundInfo->SoundMode, pSoundInfo->SoundBW, pSoundInfo->SoundPeriod));

	return Ret;
}

// Used after Beamforming_Leave, and will clear the setting of the "already deleted" entry
/*SU BFee Entry Only*/
VOID
phydm_beamforming_EndPeriod_SW(
	IN	PVOID				pDM_VOID
	)
{
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PADAPTER					Adapter = pDM_Odm->Adapter;
	u1Byte						Idx = 0;
	PRT_BEAMFORMEE_ENTRY		pBeamformEntry;
	PRT_BEAMFORMING_INFO 		pBeamInfo = &pDM_Odm->BeamformingInfo;
	PRT_SOUNDING_INFO			pSoundInfo = &(pBeamInfo->SoundingInfo);
	
	HAL_HW_TIMER_TYPE TimerType = HAL_TIMER_TXBF;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s Start!\n", __func__));

	if (pSoundInfo->SoundMode == SOUNDING_SW_VHT_TIMER || pSoundInfo->SoundMode == SOUNDING_SW_HT_TIMER)
		ODM_CancelTimer(pDM_Odm, &pBeamInfo->BeamformingTimer);
	else if (pSoundInfo->SoundMode == SOUNDING_HW_VHT_TIMER || pSoundInfo->SoundMode == SOUNDING_HW_HT_TIMER ||
				pSoundInfo->SoundMode == SOUNDING_AUTO_VHT_TIMER || pSoundInfo->SoundMode == SOUNDING_AUTO_HT_TIMER)
		/*HW timer stop: All IC has the same setting*/
		Adapter->HalFunc.SetHwRegHandler(Adapter, HW_VAR_HW_REG_TIMER_STOP,  (pu1Byte)(&TimerType));
		/*ODM_Write1Byte(pDM_Odm, 0x15F, 0);*/
}

VOID
phydm_beamforming_EndPeriod_FW(
	IN	PVOID				pDM_VOID
	)
{
	PDM_ODM_T			pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte				Idx = 0;

	HalComTxbf_Set(pDM_Odm, TXBF_SET_SOUNDING_FW_NDPA, (pu1Byte)&Idx);
	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s]\n", __func__));
}


/*SU BFee Entry Only*/
VOID 
phydm_beamforming_ClearEntry_SW(
	IN	PVOID			pDM_VOID,
	BOOLEAN				IsDelete,
	u1Byte				DeleteIdx
	)
{
	u1Byte						Idx = 0;
	PRT_BEAMFORMEE_ENTRY		pBeamformEntry = NULL;
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PRT_BEAMFORMING_INFO 		pBeamInfo = &pDM_Odm->BeamformingInfo;

	if (IsDelete) {
		if (DeleteIdx < BEAMFORMEE_ENTRY_NUM) {
			pBeamformEntry = pBeamInfo->BeamformeeEntry + DeleteIdx;
			if (!((!pBeamformEntry->bUsed) && pBeamformEntry->bSound)) {
				ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] SW DeleteIdx is wrong!!!!!\n", __func__));
				return;
			}
		}

		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] SW delete BFee entry %d\n", __func__, DeleteIdx));
		if (pBeamformEntry->BeamformEntryState == BEAMFORMING_ENTRY_STATE_PROGRESSING) {
			pBeamformEntry->bBeamformingInProgress = FALSE;
			pBeamformEntry->BeamformEntryState = BEAMFORMING_ENTRY_STATE_UNINITIALIZE;
		} else if (pBeamformEntry->BeamformEntryState == BEAMFORMING_ENTRY_STATE_PROGRESSED) {
			pBeamformEntry->BeamformEntryState  = BEAMFORMING_ENTRY_STATE_UNINITIALIZE;
			HalComTxbf_Set(pDM_Odm, TXBF_SET_SOUNDING_STATUS, (pu1Byte)&DeleteIdx);
		}
		pBeamformEntry->bSound = FALSE;
	} else {
		for (Idx = 0; Idx < BEAMFORMEE_ENTRY_NUM; Idx++) {
			pBeamformEntry = pBeamInfo->BeamformeeEntry+Idx;

			/*Used after bSounding=RESET, and will clear the setting of "ever sounded" entry, which is not necessarily be deleted.*/
			/*This function is mainly used in case "BeamOidInfo.SoundOidMode == SOUNDING_STOP_All_TIMER".*/
			/*However, setting oid doesn't delete entries (bUsed is still TRUE), new entries may fail to be added in.*/
		
			if (pBeamformEntry->bSound) {
				ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] SW reset BFee entry %d\n", __func__, Idx));
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
					HalComTxbf_Set(pDM_Odm, TXBF_SET_SOUNDING_STATUS, (pu1Byte)&Idx);
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
	PRT_BEAMFORMEE_ENTRY		pBeamformEntry = NULL;
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PRT_BEAMFORMING_INFO 		pBeamInfo = &pDM_Odm->BeamformingInfo;

	if (IsDelete) {
		if (DeleteIdx < BEAMFORMEE_ENTRY_NUM) {
			pBeamformEntry = pBeamInfo->BeamformeeEntry + DeleteIdx;

			if (!((!pBeamformEntry->bUsed) && pBeamformEntry->bSound)) {
				ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] FW DeleteIdx is wrong!!!!!\n", __func__));
				return;
			}
		}
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s: FW delete BFee entry %d\n", __func__, DeleteIdx));
		pBeamformEntry->BeamformEntryState = BEAMFORMING_ENTRY_STATE_UNINITIALIZE;
		pBeamformEntry->bSound = FALSE;
	} else {
		for (Idx = 0; Idx < BEAMFORMEE_ENTRY_NUM; Idx++) {
			pBeamformEntry = pBeamInfo->BeamformeeEntry+Idx;

			/*Used after bSounding=RESET, and will clear the setting of "ever sounded" entry, which is not necessarily be deleted.*/
			/*This function is mainly used in case "BeamOidInfo.SoundOidMode == SOUNDING_STOP_All_TIMER".*/
			/*However, setting oid doesn't delete entries (bUsed is still TRUE), new entries may fail to be added in.*/
		
			if (pBeamformEntry->bSound) {
				ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s]FW reset BFee entry %d\n", __func__, Idx));
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

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s Start!\n", __func__));

	bSounding = phydm_beamfomring_bSounding(pDM_Odm, pBeamInfo, &Idx);

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s, Before notify, bSounding=%d, Idx=%d\n", __func__, bSounding, Idx));
	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s: pBeamInfo->beamformee_su_cnt = %d\n", __func__, pBeamInfo->beamformee_su_cnt));
	

	switch (bSounding) {
	case BEAMFORMEE_NOTIFY_ADD_SU:
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s: BEAMFORMEE_NOTIFY_ADD_SU\n", __func__));
		phydm_beamforming_StartPeriod(pDM_Odm);
	break;

	case BEAMFORMEE_NOTIFY_DELETE_SU:
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s: BEAMFORMEE_NOTIFY_DELETE_SU\n", __func__));
		if (pSoundInfo->SoundMode == SOUNDING_FW_HT_TIMER || pSoundInfo->SoundMode == SOUNDING_FW_VHT_TIMER) {
			phydm_beamforming_ClearEntry_FW(pDM_Odm, TRUE, Idx);
			if (pBeamInfo->beamformee_su_cnt == 0) { /* For 2->1 entry, we should not cancel SW timer */
				phydm_beamforming_EndPeriod_FW(pDM_Odm);
				ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s: No BFee left\n", __func__));
			}
		} else {
			phydm_beamforming_ClearEntry_SW(pDM_Odm, TRUE, Idx);
			if (pBeamInfo->beamformee_su_cnt == 0) { /* For 2->1 entry, we should not cancel SW timer */
				phydm_beamforming_EndPeriod_SW(pDM_Odm);
				ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s: No BFee left\n", __func__));
			}
		}
	break;

	case BEAMFORMEE_NOTIFY_ADD_MU:
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s: BEAMFORMEE_NOTIFY_ADD_MU\n", __func__));
		if (pBeamInfo->beamformee_mu_cnt == 2) {
			/*if (pSoundInfo->SoundMode == SOUNDING_SW_VHT_TIMER || pSoundInfo->SoundMode == SOUNDING_SW_HT_TIMER)
				ODM_SetTimer(pDM_Odm, &pBeamInfo->BeamformingTimer, pSoundInfo->SoundPeriod);*/
			ODM_SetTimer(pDM_Odm, &pBeamInfo->BeamformingTimer, 1000); /*Do MU sounding every 1sec*/
		} else
			ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s: Less or larger than 2 MU STAs, not to set timer\n", __func__));
	break;

	case BEAMFORMEE_NOTIFY_DELETE_MU:
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s: BEAMFORMEE_NOTIFY_DELETE_MU\n", __func__));
		if (pBeamInfo->beamformee_mu_cnt == 1) {
			/*if (pSoundInfo->SoundMode == SOUNDING_SW_VHT_TIMER || pSoundInfo->SoundMode == SOUNDING_SW_HT_TIMER)*/{
				ODM_CancelTimer(pDM_Odm, &pBeamInfo->BeamformingTimer);
				ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s: Less than 2 MU STAs, stop sounding\n", __func__));
			}
		}
	break;

	case BEAMFORMING_NOTIFY_RESET:
		if (pSoundInfo->SoundMode == SOUNDING_FW_HT_TIMER || pSoundInfo->SoundMode == SOUNDING_FW_VHT_TIMER) {	
			phydm_beamforming_ClearEntry_FW(pDM_Odm, FALSE, Idx);
			phydm_beamforming_EndPeriod_FW(pDM_Odm);
		} else {
			phydm_beamforming_ClearEntry_SW(pDM_Odm, FALSE, Idx);
			phydm_beamforming_EndPeriod_SW(pDM_Odm);
		}

	break;

	default:
	break;
	}

}



BOOLEAN
Beamforming_InitEntry(
	IN	PVOID		pDM_VOID,
	IN	u2Byte		staIdx,
	pu1Byte			BFerBFeeIdx
	)
{
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PRT_BEAMFORMEE_ENTRY		pBeamformEntry = NULL;
	PRT_BEAMFORMER_ENTRY		pBeamformerEntry = NULL;
	PRT_BEAMFORM_STAINFO		pSTA = NULL;
	BEAMFORMING_CAP			BeamformCap = BEAMFORMING_CAP_NONE;
	u1Byte						BFerIdx=0xF, BFeeIdx=0xF;
	u1Byte						NumofSoundingDim = 0, CompSteeringNumofBFer = 0;

	pSTA = phydm_staInfoInit(pDM_Odm, staIdx);

	/*The current setting does not support Beaforming*/
	if (BEAMFORMING_CAP_NONE == pSTA->HtBeamformCap && BEAMFORMING_CAP_NONE == pSTA->VhtBeamformCap) {
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("The configuration disabled Beamforming! Skip...\n"));		
		return FALSE;
	}

	if (pSTA->WirelessMode < WIRELESS_MODE_N_24G)
		return FALSE;
	else {	/*HT*/
		/*We are Beamformee because the STA is Beamformer*/
		if (TEST_FLAG(pSTA->CurBeamform, BEAMFORMING_HT_BEAMFORMER_ENABLE)) {
			BeamformCap =(BEAMFORMING_CAP)(BeamformCap |BEAMFORMEE_CAP_HT_EXPLICIT);
			NumofSoundingDim = (pSTA->CurBeamform&BEAMFORMING_HT_BEAMFORMEE_CHNL_EST_CAP)>>6;
		}
		/*We are Beamformer because the STA is Beamformee*/
		if (TEST_FLAG(pSTA->CurBeamform, BEAMFORMING_HT_BEAMFORMEE_ENABLE) ||
			TEST_FLAG(pSTA->HtBeamformCap, BEAMFORMING_HT_BEAMFORMER_TEST)) {
			BeamformCap =(BEAMFORMING_CAP)(BeamformCap | BEAMFORMER_CAP_HT_EXPLICIT);
			CompSteeringNumofBFer = (pSTA->CurBeamform & BEAMFORMING_HT_BEAMFORMER_STEER_NUM)>>4;
		}
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] HT CurBeamform=0x%X, BeamformCap=0x%X\n", __func__, pSTA->CurBeamform, BeamformCap));
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] HT NumofSoundingDim=%d, CompSteeringNumofBFer=%d\n", __func__, NumofSoundingDim, CompSteeringNumofBFer));
#if	(ODM_IC_11AC_SERIES_SUPPORT == 1)
		if (pSTA->WirelessMode & WIRELESS_MODE_AC_5G || pSTA->WirelessMode & WIRELESS_MODE_AC_24G) {	/*VHT*/	

			/* We are Beamformee because the STA is SU Beamformer*/
			if (TEST_FLAG(pSTA->CurBeamformVHT, BEAMFORMING_VHT_BEAMFORMER_ENABLE)) {
				BeamformCap =(BEAMFORMING_CAP)(BeamformCap |BEAMFORMEE_CAP_VHT_SU);
				NumofSoundingDim = (pSTA->CurBeamformVHT & BEAMFORMING_VHT_BEAMFORMEE_SOUND_DIM)>>12;
			}
			/* We are Beamformer because the STA is SU Beamformee*/
			if (TEST_FLAG(pSTA->CurBeamformVHT, BEAMFORMING_VHT_BEAMFORMEE_ENABLE) ||
				TEST_FLAG(pSTA->VhtBeamformCap, BEAMFORMING_VHT_BEAMFORMER_TEST)) {
				BeamformCap =(BEAMFORMING_CAP)(BeamformCap |BEAMFORMER_CAP_VHT_SU);
				CompSteeringNumofBFer = (pSTA->CurBeamformVHT & BEAMFORMING_VHT_BEAMFORMER_STS_CAP)>>8;
			}
			/* We are Beamformee because the STA is MU Beamformer*/
			if (TEST_FLAG(pSTA->CurBeamformVHT, BEAMFORMING_VHT_MU_MIMO_AP_ENABLE)) {
				BeamformCap = (BEAMFORMING_CAP)(BeamformCap | BEAMFORMEE_CAP_VHT_MU);
				NumofSoundingDim = (pSTA->CurBeamformVHT & BEAMFORMING_VHT_BEAMFORMEE_SOUND_DIM)>>12;
			}
			/* We are Beamformer because the STA is MU Beamformee*/
			if (phydm_actingDetermine(pDM_Odm, PhyDM_ACTING_AS_AP)) { /* Only AP mode supports to act an MU beamformer */
				if (TEST_FLAG(pSTA->CurBeamformVHT, BEAMFORMING_VHT_MU_MIMO_STA_ENABLE) ||
					TEST_FLAG(pSTA->VhtBeamformCap, BEAMFORMING_VHT_BEAMFORMER_TEST)) {
					BeamformCap = (BEAMFORMING_CAP)(BeamformCap | BEAMFORMER_CAP_VHT_MU);
					CompSteeringNumofBFer = (pSTA->CurBeamformVHT & BEAMFORMING_VHT_BEAMFORMER_STS_CAP)>>8;
				}
			}
			ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s]VHT CurBeamformVHT=0x%X, BeamformCap=0x%X\n", __func__, pSTA->CurBeamformVHT, BeamformCap));
			ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s]VHT NumofSoundingDim=0x%X, CompSteeringNumofBFer=0x%X\n", __func__, NumofSoundingDim, CompSteeringNumofBFer));
			
		}
#endif
	}


	if(BeamformCap == BEAMFORMING_CAP_NONE)
		return FALSE;
	
	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] Self BF Entry Cap = 0x%02X\n", __func__, BeamformCap));

	/*We are BFee, so the entry is BFer*/
	if (BeamformCap & (BEAMFORMEE_CAP_VHT_MU | BEAMFORMEE_CAP_VHT_SU | BEAMFORMEE_CAP_HT_EXPLICIT)) {
		pBeamformerEntry = phydm_Beamforming_GetBFerEntryByAddr(pDM_Odm, pSTA->RA, &BFerIdx);
		
		if (pBeamformerEntry == NULL) {
			pBeamformerEntry = Beamforming_AddBFerEntry(pDM_Odm, pSTA, BeamformCap, NumofSoundingDim , &BFerIdx);
			if (pBeamformerEntry == NULL)
				ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s]Not enough BFer entry!!!!!\n", __func__));
		}
	}

	/*We are BFer, so the entry is BFee*/
	if (BeamformCap & (BEAMFORMER_CAP_VHT_MU | BEAMFORMER_CAP_VHT_SU | BEAMFORMER_CAP_HT_EXPLICIT)) {
		pBeamformEntry = phydm_Beamforming_GetBFeeEntryByAddr(pDM_Odm, pSTA->RA, &BFeeIdx);

		/*如果BFeeIdx = 0xF 則代表目前entry當中沒有相同的MACID在內*/
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] Get BFee entry 0x%X by address\n", __func__, BFeeIdx));
		if (pBeamformEntry == NULL) {
			pBeamformEntry = Beamforming_AddBFeeEntry(pDM_Odm, pSTA, BeamformCap, NumofSoundingDim, CompSteeringNumofBFer, &BFeeIdx);
			ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s]: pSTA->AID=%d, pSTA->MacID=%d\n", __func__, pSTA->AID, pSTA->MacID));

			ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s]: Add BFee entry %d\n", __func__, BFeeIdx));

			if (pBeamformEntry == NULL)
				return FALSE;
			else
				pBeamformEntry->BeamformEntryState = BEAMFORMING_ENTRY_STATE_INITIALIZEING;
		} else {
			/*Entry has been created. If entry is initialing or progressing then errors occur.*/
			if (pBeamformEntry->BeamformEntryState != BEAMFORMING_ENTRY_STATE_INITIALIZED && 
				pBeamformEntry->BeamformEntryState != BEAMFORMING_ENTRY_STATE_PROGRESSED) {
				return FALSE;
			} else
				pBeamformEntry->BeamformEntryState = BEAMFORMING_ENTRY_STATE_INITIALIZEING;
		}
		pBeamformEntry->BeamformEntryState = BEAMFORMING_ENTRY_STATE_INITIALIZED;
		phydm_staInfoUpdate(pDM_Odm, staIdx, pBeamformEntry);
	}

	*BFerBFeeIdx = (BFerIdx<<4) | BFeeIdx;
	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] End: BFerIdx=0x%X, BFeeIdx=0x%X, BFerBFeeIdx=0x%X\n", __func__, BFerIdx, BFeeIdx, *BFerBFeeIdx));

	return TRUE;
}


VOID
Beamforming_DeInitEntry(
	IN	PVOID		pDM_VOID,
	pu1Byte			RA
	)
{
	PDM_ODM_T			pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte				Idx = 0;

	PRT_BEAMFORMER_ENTRY	pBFerEntry = phydm_Beamforming_GetBFerEntryByAddr(pDM_Odm, RA, &Idx);
	PRT_BEAMFORMEE_ENTRY	pBFeeEntry = phydm_Beamforming_GetBFeeEntryByAddr(pDM_Odm, RA, &Idx);
	BOOLEAN ret = FALSE;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s Start!\n",  __func__));
	
	if (pBFeeEntry != NULL) {
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s, pBFeeEntry\n", __func__));
		pBFeeEntry->bUsed = FALSE;
		pBFeeEntry->BeamformEntryCap = BEAMFORMING_CAP_NONE;
		pBFeeEntry->bBeamformingInProgress = FALSE;
		if (pBFeeEntry->is_mu_sta) {
			pDM_Odm->BeamformingInfo.beamformee_mu_cnt -= 1;
			pDM_Odm->BeamformingInfo.FirstMUBFeeIndex = phydm_Beamforming_GetFirstMUBFeeEntryIdx(pDM_Odm);
		} else {
			pDM_Odm->BeamformingInfo.beamformee_su_cnt -= 1;
		}
		ret = TRUE;
	} 
	
	if (pBFerEntry != NULL) {
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s, pBFerEntry\n", __func__));
		pBFerEntry->bUsed = FALSE;
		pBFerEntry->BeamformEntryCap = BEAMFORMING_CAP_NONE;
		if (pBFerEntry->is_mu_ap)
			pDM_Odm->BeamformingInfo.beamformer_mu_cnt -= 1;
		else
			pDM_Odm->BeamformingInfo.beamformer_su_cnt -= 1;
		ret = TRUE;
	}

	if (ret == TRUE)
		HalComTxbf_Set(pDM_Odm, TXBF_SET_SOUNDING_LEAVE, (pu1Byte)&Idx);

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s End, Idx = 0x%X\n", __func__, Idx));
}


VOID
Beamforming_Reset(
	IN	PVOID				pDM_VOID
	)
{
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte					Idx = 0;
	PRT_BEAMFORMING_INFO	pBeamformingInfo = &(pDM_Odm->BeamformingInfo);

	for (Idx = 0; Idx < BEAMFORMEE_ENTRY_NUM; Idx++) {
		if (pBeamformingInfo->BeamformeeEntry[Idx].bUsed == TRUE) {
			pBeamformingInfo->BeamformeeEntry[Idx].bUsed = FALSE;
			pBeamformingInfo->BeamformeeEntry[Idx].BeamformEntryCap = BEAMFORMING_CAP_NONE;
			/*pBeamformingInfo->BeamformeeEntry[Idx].BeamformEntryState = BEAMFORMING_ENTRY_STATE_UNINITIALIZE;*/
			/*Modified by David*/
			pBeamformingInfo->BeamformeeEntry[Idx].bBeamformingInProgress = FALSE;
			HalComTxbf_Set(pDM_Odm, TXBF_SET_SOUNDING_LEAVE, (pu1Byte)&Idx);
		}
	}

	for (Idx = 0; Idx < BEAMFORMER_ENTRY_NUM; Idx++) {
		pBeamformingInfo->BeamformerEntry[Idx].bUsed = FALSE;
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] Idx=%d, bUsed=%d\n", __func__, Idx, pBeamformingInfo->BeamformerEntry[Idx].bUsed));
	}

}


BOOLEAN
BeamformingStart_V1(
	IN	PVOID		pDM_VOID,
	pu1Byte			RA,
	BOOLEAN			Mode,
	CHANNEL_WIDTH	BW,
	u1Byte			Rate
	)
{
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte					Idx = 0;
	PRT_BEAMFORMEE_ENTRY	pEntry;
	BOOLEAN					ret = TRUE;
	PRT_BEAMFORMING_INFO	pBeamInfo = &(pDM_Odm->BeamformingInfo);
	
	pEntry = phydm_Beamforming_GetBFeeEntryByAddr(pDM_Odm, RA, &Idx);

	if (pEntry->bUsed == FALSE) {
		pEntry->bBeamformingInProgress = FALSE;
		return FALSE;
	} else {
		if (pEntry->bBeamformingInProgress)
			return FALSE;

		pEntry->bBeamformingInProgress = TRUE;

		if (Mode == 1) {	
			if (!(pEntry->BeamformEntryCap & BEAMFORMER_CAP_HT_EXPLICIT)) {
				pEntry->bBeamformingInProgress = FALSE;
				return FALSE;
			}
		} else if (Mode == 0) {
			if (!(pEntry->BeamformEntryCap & BEAMFORMER_CAP_VHT_SU)) {
				pEntry->bBeamformingInProgress = FALSE;
				return FALSE;
			}
		}

		if (pEntry->BeamformEntryState != BEAMFORMING_ENTRY_STATE_INITIALIZED && pEntry->BeamformEntryState != BEAMFORMING_ENTRY_STATE_PROGRESSED) {
			pEntry->bBeamformingInProgress = FALSE;
			return FALSE;
		} else {
			pEntry->BeamformEntryState = BEAMFORMING_ENTRY_STATE_PROGRESSING;
			pEntry->bSound = TRUE;
		}
	}

	pEntry->SoundBW = BW;
	pBeamInfo->BeamformeeCurIdx = Idx;
	phydm_Beamforming_NDPARate(pDM_Odm, BW, Rate);
	HalComTxbf_Set(pDM_Odm, TXBF_SET_SOUNDING_STATUS, (pu1Byte)&Idx);

	if (Mode == 1)
		ret = Beamforming_SendHTNDPAPacket(pDM_Odm, RA, BW, NORMAL_QUEUE);	
	else
		ret = Beamforming_SendVHTNDPAPacket(pDM_Odm, RA, pEntry->AID, BW, NORMAL_QUEUE);

	if (ret == FALSE) {
		Beamforming_Leave(pDM_Odm, RA);
		pEntry->bBeamformingInProgress = FALSE;
		return FALSE;
	}

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s  Idx %d\n", __func__, Idx));
	return TRUE;
}


BOOLEAN
BeamformingStart_SW(
	IN	PVOID		pDM_VOID,
	u1Byte			Idx,
	u1Byte			Mode, 
	CHANNEL_WIDTH	BW
	)
{
	pu1Byte					RA = NULL;
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PRT_BEAMFORMEE_ENTRY	pEntry;
	BOOLEAN					ret = TRUE;
	PRT_BEAMFORMING_INFO	pBeamInfo = &(pDM_Odm->BeamformingInfo);

	pEntry = &(pBeamInfo->BeamformeeEntry[Idx]);

	if (pEntry->bUsed == FALSE) {
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("Skip Beamforming, no entry for Idx =%d\n", Idx));
		pEntry->bBeamformingInProgress = FALSE;
		return FALSE;
	} else {
		if (pEntry->bBeamformingInProgress) {
			ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("bBeamformingInProgress, skip...\n"));
			return FALSE;
		}

		pEntry->bBeamformingInProgress = TRUE;
		RA = pEntry->MacAddr;
		
		if (Mode == SOUNDING_SW_HT_TIMER || Mode == SOUNDING_HW_HT_TIMER || Mode == SOUNDING_AUTO_HT_TIMER) {	
			if (!(pEntry->BeamformEntryCap & BEAMFORMER_CAP_HT_EXPLICIT)) {
				pEntry->bBeamformingInProgress = FALSE;
				ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s Return by not support BEAMFORMER_CAP_HT_EXPLICIT <==\n", __func__));
				return FALSE;
			}
		} else if (Mode == SOUNDING_SW_VHT_TIMER || Mode == SOUNDING_HW_VHT_TIMER || Mode == SOUNDING_AUTO_VHT_TIMER) {
			if (!(pEntry->BeamformEntryCap & BEAMFORMER_CAP_VHT_SU)) {
				pEntry->bBeamformingInProgress = FALSE;
				ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s Return by not support BEAMFORMER_CAP_VHT_SU <==\n", __func__));
				return FALSE;
			}
		}
		if (pEntry->BeamformEntryState != BEAMFORMING_ENTRY_STATE_INITIALIZED && pEntry->BeamformEntryState != BEAMFORMING_ENTRY_STATE_PROGRESSED) {
			pEntry->bBeamformingInProgress = FALSE;
			ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s Return by incorrect BeamformEntryState(%d) <==\n", __func__, pEntry->BeamformEntryState));
			return FALSE;
		} else {
			pEntry->BeamformEntryState = BEAMFORMING_ENTRY_STATE_PROGRESSING;
			pEntry->bSound = TRUE;
		}
	}

	pBeamInfo->BeamformeeCurIdx = Idx;
	/*2014.12.22 Luke: Need to be checked*/
	/*GET_TXBF_INFO(Adapter)->fTxbfSet(Adapter, TXBF_SET_SOUNDING_STATUS, (pu1Byte)&Idx);*/

	if (Mode == SOUNDING_SW_HT_TIMER || Mode == SOUNDING_HW_HT_TIMER || Mode == SOUNDING_AUTO_HT_TIMER)
		ret = Beamforming_SendHTNDPAPacket(pDM_Odm, RA , BW, NORMAL_QUEUE);	
	else
		ret = Beamforming_SendVHTNDPAPacket(pDM_Odm, RA , pEntry->AID, BW, NORMAL_QUEUE);

	if (ret == FALSE) {
		Beamforming_Leave(pDM_Odm, RA);
		pEntry->bBeamformingInProgress = FALSE;
		return FALSE;
	}

	
	/*--------------------------
	// Send BF Report Poll for MU BF
	--------------------------*/
#ifdef SUPPORT_MU_BF
#if (SUPPORT_MU_BF == 1)
{
	u1Byte				idx, PollSTACnt = 0;
	BOOLEAN				bGetFirstBFee = FALSE;
	
	if (pBeamInfo->beamformee_mu_cnt > 1) { /* More than 1 MU STA*/
	
		for (idx = 0; idx < BEAMFORMEE_ENTRY_NUM; idx++) {
			pEntry = &(pBeamInfo->BeamformeeEntry[idx]);
			if (pEntry->is_mu_sta) {
				if (bGetFirstBFee) {
					PollSTACnt++;
					if (PollSTACnt == (pBeamInfo->beamformee_mu_cnt - 1))/* The last STA*/
						SendSWVHTBFReportPoll(pDM_Odm, pEntry->MacAddr, TRUE);
					else
						SendSWVHTBFReportPoll(pDM_Odm, pEntry->MacAddr, FALSE);
				} else {
					bGetFirstBFee = TRUE;
				}
			}
		}
	}
}
#endif
#endif
	return TRUE;
}


BOOLEAN
BeamformingStart_FW(
	IN PVOID		pDM_VOID,
	u1Byte			Idx
	)
{
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;
	pu1Byte					RA = NULL;
	PRT_BEAMFORMEE_ENTRY	pEntry;
	BOOLEAN					ret = TRUE;
	PRT_BEAMFORMING_INFO	pBeamInfo = &(pDM_Odm->BeamformingInfo);

	pEntry = &(pBeamInfo->BeamformeeEntry[Idx]);
	if (pEntry->bUsed == FALSE) {
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("Skip Beamforming, no entry for Idx =%d\n", Idx));
		return FALSE;
	}

	pEntry->BeamformEntryState = BEAMFORMING_ENTRY_STATE_PROGRESSING;
	pEntry->bSound = TRUE;
	HalComTxbf_Set(pDM_Odm, TXBF_SET_SOUNDING_FW_NDPA, (pu1Byte)&Idx);
	
	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] End, Idx=0x%X\n", __func__, Idx));
	return TRUE;
}

VOID
Beamforming_CheckSoundingSuccess(
	IN PVOID			pDM_VOID,
	BOOLEAN			Status	
)
{
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PRT_BEAMFORMING_INFO	pBeamInfo = &(pDM_Odm->BeamformingInfo);
	PRT_BEAMFORMEE_ENTRY	pEntry = &(pBeamInfo->BeamformeeEntry[pBeamInfo->BeamformeeCurIdx]);

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[David]@%s Start!\n", __func__));

	if (Status == 1) {
		if (pEntry->LogStatusFailCnt == 21)
			Beamforming_DymPeriod(pDM_Odm, Status);
		pEntry->LogStatusFailCnt = 0;
	} else if (pEntry->LogStatusFailCnt <= 20) {
		pEntry->LogStatusFailCnt++;
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s LogStatusFailCnt %d\n", __func__, pEntry->LogStatusFailCnt));
	}
	if (pEntry->LogStatusFailCnt > 20) {
		pEntry->LogStatusFailCnt = 21;
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s LogStatusFailCnt > 20, Stop SOUNDING\n", __func__));
		Beamforming_DymPeriod(pDM_Odm, Status);
	}
}

VOID
phydm_Beamforming_End_SW(
	IN PVOID		pDM_VOID,
	BOOLEAN			Status	
	)
{
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PRT_BEAMFORMING_INFO 	pBeamInfo = &pDM_Odm->BeamformingInfo;
	PRT_BEAMFORMEE_ENTRY	pEntry = &(pBeamInfo->BeamformeeEntry[pBeamInfo->BeamformeeCurIdx]);

	if (pEntry->BeamformEntryState != BEAMFORMING_ENTRY_STATE_PROGRESSING) {
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] BeamformStatus %d\n", __func__, pEntry->BeamformEntryState));
		return;
	}

	if ((pDM_Odm->TxBfDataRate >= ODM_RATEVHTSS3MCS7) && (pDM_Odm->TxBfDataRate <= ODM_RATEVHTSS3MCS9)) {
		ODM_RT_TRACE(pDM_Odm, BEAMFORMING_DEBUG, ODM_DBG_LOUD, ("[%s] VHT3SS 7,8,9, do not apply V matrix.\n", __func__));
		pEntry->BeamformEntryState = BEAMFORMING_ENTRY_STATE_INITIALIZED;
		HalComTxbf_Set(pDM_Odm, TXBF_SET_SOUNDING_STATUS, (pu1Byte)&(pBeamInfo->BeamformeeCurIdx));
	} else if (Status == 1) {
		pEntry->LogStatusFailCnt = 0;
		pEntry->BeamformEntryState = BEAMFORMING_ENTRY_STATE_PROGRESSED;
		HalComTxbf_Set(pDM_Odm, TXBF_SET_SOUNDING_STATUS, (pu1Byte)&(pBeamInfo->BeamformeeCurIdx));
	} else {
		pEntry->LogStatusFailCnt++;
		pEntry->BeamformEntryState = BEAMFORMING_ENTRY_STATE_INITIALIZED;
		HalComTxbf_Set(pDM_Odm, TXBF_SET_TX_PATH_RESET, (pu1Byte)&(pBeamInfo->BeamformeeCurIdx));
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] LogStatusFailCnt %d\n", __func__, pEntry->LogStatusFailCnt));
	}
	
	if (pEntry->LogStatusFailCnt > 30) {
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s LogStatusFailCnt > 50, Stop SOUNDING\n", __func__));
		pEntry->bSound = FALSE;
		Beamforming_DeInitEntry(pDM_Odm, pEntry->MacAddr); 

		/*Modified by David - Every action of deleting entry should follow by Notify*/
		phydm_Beamforming_Notify(pDM_Odm);
	}	
	pEntry->bBeamformingInProgress = FALSE;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s: Status=%d\n", __func__, Status));
}	


VOID
Beamforming_TimerCallback(
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	IN PVOID			pDM_VOID
#elif(DM_ODM_SUPPORT_TYPE == ODM_CE)
	IN PVOID            pContext
#endif
	)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PADAPTER					Adapter = pDM_Odm->Adapter;
#elif(DM_ODM_SUPPORT_TYPE == ODM_CE)
	PADAPTER					Adapter = (PADAPTER)pContext;
	PHAL_DATA_TYPE				pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T					pDM_Odm = &pHalData->odmpriv;
#endif
	BOOLEAN						ret = FALSE;
	PRT_BEAMFORMING_INFO		pBeamInfo = &(pDM_Odm->BeamformingInfo);
	PRT_BEAMFORMEE_ENTRY		pEntry = &(pBeamInfo->BeamformeeEntry[pBeamInfo->BeamformeeCurIdx]);
	PRT_SOUNDING_INFO			pSoundInfo = &(pBeamInfo->SoundingInfo);

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s Start!\n", __func__));

	if (pEntry->bBeamformingInProgress) {
	 	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("bBeamformingInProgress, reset it\n"));
		phydm_Beamforming_End_SW(pDM_Odm, 0);
	 }

	ret = phydm_Beamforming_SelectBeamEntry(pDM_Odm, pBeamInfo);
#if (SUPPORT_MU_BF == 1)
	if (ret && pBeamInfo->beamformee_mu_cnt > 1)
		ret = 1;
	else
		ret = 0;
#endif
	if (ret)
		ret = BeamformingStart_SW(pDM_Odm, pSoundInfo->SoundIdx, pSoundInfo->SoundMode, pSoundInfo->SoundBW);
	else
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s, Error value return from BeamformingStart_V2\n", __func__));

	if ((pBeamInfo->beamformee_su_cnt != 0) || (pBeamInfo->beamformee_mu_cnt > 1)) {
		if (pSoundInfo->SoundMode == SOUNDING_SW_VHT_TIMER || pSoundInfo->SoundMode == SOUNDING_SW_HT_TIMER)
			ODM_SetTimer(pDM_Odm, &pBeamInfo->BeamformingTimer, pSoundInfo->SoundPeriod);
		else {
			u4Byte	val = (pSoundInfo->SoundPeriod << 16) | HAL_TIMER_TXBF;
			Adapter->HalFunc.SetHwRegHandler(Adapter, HW_VAR_HW_REG_TIMER_RESTART, (pu1Byte)(&val));
		}
	}
}


VOID
Beamforming_SWTimerCallback(
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PRT_TIMER		pTimer
#elif(DM_ODM_SUPPORT_TYPE == ODM_CE)
	void *FunctionContext
#endif
	)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PADAPTER		Adapter = (PADAPTER)pTimer->Adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	
	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] Start!\n", __func__));
	Beamforming_TimerCallback(pDM_Odm);
#elif(DM_ODM_SUPPORT_TYPE == ODM_CE)
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)FunctionContext;
	PADAPTER	Adapter = pDM_Odm->Adapter;

	if (Adapter->net_closed == TRUE)
		return;
	rtw_run_in_thread_cmd(Adapter, Beamforming_TimerCallback, Adapter);
#endif
	
}


VOID
phydm_Beamforming_Init(
	IN PVOID			pDM_VOID
	)
{
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PRT_BEAMFORMING_INFO 		pBeamInfo = &pDM_Odm->BeamformingInfo;
	PHAL_TXBF_INFO				pTxbfInfo = &pBeamInfo->TxbfInfo;
	PRT_BEAMFORMING_OID_INFO	pBeamOidInfo = &(pBeamInfo->BeamformingOidInfo);
	
	pBeamOidInfo->SoundOidMode = SOUNDING_STOP_OID_TIMER;
	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s Mode (%d)\n", __func__, pBeamOidInfo->SoundOidMode));

	pBeamInfo->beamformee_su_cnt = 0;
	pBeamInfo->beamformer_su_cnt = 0;
	pBeamInfo->beamformee_mu_cnt = 0;
	pBeamInfo->beamformer_mu_cnt = 0;
	pBeamInfo->beamformee_mu_reg_maping = 0;
	pBeamInfo->mu_ap_index = 0;
	pBeamInfo->is_mu_sounding = FALSE;
	pBeamInfo->FirstMUBFeeIndex = 0xFF;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)	
	pBeamInfo->SourceAdapter = pDM_Odm->Adapter;
#endif
	halComTxbf_beamformInit(pDM_Odm);
}	


VOID
Beamforming_Enter(
	IN PVOID			pDM_VOID,
	IN u2Byte		staIdx
)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte			BFerBFeeIdx = 0xff;
	
	if (Beamforming_InitEntry(pDM_Odm, staIdx, &BFerBFeeIdx))
		HalComTxbf_Set(pDM_Odm, TXBF_SET_SOUNDING_ENTER, (pu1Byte)&BFerBFeeIdx);

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] End!\n", __func__));
}


VOID
Beamforming_Leave(
	IN PVOID			pDM_VOID,
	pu1Byte			RA
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;

	if (RA == NULL)
		Beamforming_Reset(pDM_Odm);
	else
		Beamforming_DeInitEntry(pDM_Odm, RA);

	phydm_Beamforming_Notify(pDM_Odm);

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] End!!\n", __func__));
}

#if 0
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
	PRT_BEAMFORMEE_ENTRY	pEntry;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s Start!\n", __func__));

	pEntry = phydm_Beamforming_GetEntryByMacId(pDM_Odm, MacId, &Idx);

	if(pEntry == NULL)
		return;
	else
		pEntry->bTxBF = bTxBF;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s MacId %d TxBF %d\n", __func__, pEntry->MacId, pEntry->bTxBF));

	phydm_Beamforming_Notify(pDM_Odm);
}
#endif

BEAMFORMING_CAP
phydm_Beamforming_GetBeamCap(
	IN PVOID						pDM_VOID,
	IN PRT_BEAMFORMING_INFO 	pBeamInfo
	)
{
	u1Byte					i;
	BOOLEAN 				bSelfBeamformer = FALSE;
	BOOLEAN 				bSelfBeamformee = FALSE;
	RT_BEAMFORMEE_ENTRY	BeamformeeEntry;
	RT_BEAMFORMER_ENTRY	BeamformerEntry;
	BEAMFORMING_CAP 		BeamformCap = BEAMFORMING_CAP_NONE;
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] Start!\n", __func__));

	for (i = 0; i < BEAMFORMEE_ENTRY_NUM; i++) {
		BeamformeeEntry = pBeamInfo->BeamformeeEntry[i];

		if (BeamformeeEntry.bUsed) {
			bSelfBeamformer = TRUE;
			ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] BFee entry %d bUsed=TRUE\n", __func__, i));
			break;
		}
	}

	for (i = 0; i < BEAMFORMER_ENTRY_NUM; i++) {
		BeamformerEntry = pBeamInfo->BeamformerEntry[i];

		if (BeamformerEntry.bUsed) {
			bSelfBeamformee = TRUE;
			ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s]: BFer entry %d bUsed=TRUE\n", __func__, i));
			break;
		}
	}

	if (bSelfBeamformer)
		BeamformCap = (BEAMFORMING_CAP)(BeamformCap | BEAMFORMER_CAP);
	if (bSelfBeamformee)
		BeamformCap = (BEAMFORMING_CAP)(BeamformCap | BEAMFORMEE_CAP);

	return BeamformCap;
}


BOOLEAN
BeamformingControl_V1(
	IN PVOID			pDM_VOID,
	pu1Byte			RA,
	u1Byte			AID,
	u1Byte			Mode, 
	CHANNEL_WIDTH	BW,
	u1Byte			Rate
	)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	BOOLEAN		ret = TRUE;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s Start!\n", __func__));

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("AID (%d), Mode (%d), BW (%d)\n", AID, Mode, BW));

	switch (Mode) {	
	case 0:
	ret = BeamformingStart_V1(pDM_Odm, RA, 0, BW, Rate);
	break;
	case 1:
	ret = BeamformingStart_V1(pDM_Odm, RA, 1, BW, Rate);
	break;
	case 2:
	phydm_Beamforming_NDPARate(pDM_Odm, BW, Rate);
	ret = Beamforming_SendVHTNDPAPacket(pDM_Odm, RA, AID, BW, NORMAL_QUEUE);
	break;
	case 3:
	phydm_Beamforming_NDPARate(pDM_Odm, BW, Rate);
	ret = Beamforming_SendHTNDPAPacket(pDM_Odm, RA, BW, NORMAL_QUEUE);
	break;
	}
	return ret;
}

/*Only OID uses this function*/
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

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s Start!\n", __func__));
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

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_TRACE, ("%s Start!\n", __func__));

	if (pBeamInfo->beamformee_su_cnt == 0)
		return;

	Beamforming_DymPeriod(pDM_Odm,0);
	phydm_Beamforming_DymNDPARate(pDM_Odm);

}


#endif
