/* SPDX-License-Identifier: GPL-2.0 */
//============================================================
// Description:
//
// This file is for TXBF mechanism
//
//============================================================
#include "mp_precomp.h"
#include "../phydm_precomp.h"

#if (BEAMFORMING_SUPPORT == 1)
/*Beamforming halcomtxbf API create by YuChen 2015/05*/

VOID
halComTxbf_beamformInit(
	IN PVOID			pDM_VOID
	)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;

	if (pDM_Odm->SupportICType & ODM_RTL8822B)
		HalTxbf8822B_Init(pDM_Odm);
}

/*Only used for MU BFer Entry when get GID management frame (self is as MU STA)*/
VOID
halComTxbf_ConfigGtab(
	IN PVOID			pDM_VOID
)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;

	if (pDM_Odm->SupportICType & ODM_RTL8822B)
		HalTxbf8822B_ConfigGtab(pDM_Odm);
}

VOID
phydm_beamformSetSoundingEnter(
	IN PVOID			pDM_VOID
	)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PHAL_TXBF_INFO	pTxbfInfo = &pDM_Odm->BeamformingInfo.TxbfInfo;

	if (PlatformIsWorkItemScheduled(&(pTxbfInfo->Txbf_EnterWorkItem)) == FALSE)
		PlatformScheduleWorkItem(&(pTxbfInfo->Txbf_EnterWorkItem));
#else
	halComTxbf_EnterWorkItemCallback(pDM_Odm);
#endif
}

VOID
phydm_beamformSetSoundingLeave(
	IN PVOID			pDM_VOID
	)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PHAL_TXBF_INFO	pTxbfInfo = &pDM_Odm->BeamformingInfo.TxbfInfo;

	if (PlatformIsWorkItemScheduled(&(pTxbfInfo->Txbf_LeaveWorkItem)) == FALSE)
		PlatformScheduleWorkItem(&(pTxbfInfo->Txbf_LeaveWorkItem));
#else
	halComTxbf_LeaveWorkItemCallback(pDM_Odm);
#endif
}

VOID
phydm_beamformSetSoundingRate(
	IN PVOID			pDM_VOID
	)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PHAL_TXBF_INFO	pTxbfInfo = &pDM_Odm->BeamformingInfo.TxbfInfo;

	if (PlatformIsWorkItemScheduled(&(pTxbfInfo->Txbf_RateWorkItem)) == FALSE)
		PlatformScheduleWorkItem(&(pTxbfInfo->Txbf_RateWorkItem));
#else
	halComTxbf_RateWorkItemCallback(pDM_Odm);
#endif
}

VOID
phydm_beamformSetSoundingStatus(
	IN PVOID			pDM_VOID
	)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PHAL_TXBF_INFO	pTxbfInfo = &pDM_Odm->BeamformingInfo.TxbfInfo;

	if (PlatformIsWorkItemScheduled(&(pTxbfInfo->Txbf_StatusWorkItem)) == FALSE)
		PlatformScheduleWorkItem(&(pTxbfInfo->Txbf_StatusWorkItem));
#else
	halComTxbf_StatusWorkItemCallback(pDM_Odm);
#endif
}

VOID
phydm_beamformSetSoundingFwNdpa(
	IN PVOID			pDM_VOID
	)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PHAL_TXBF_INFO	pTxbfInfo = &pDM_Odm->BeamformingInfo.TxbfInfo;

	if (*pDM_Odm->pbFwDwRsvdPageInProgress)
		ODM_SetTimer(pDM_Odm, &(pTxbfInfo->Txbf_FwNdpaTimer), 5);
	else
		PlatformScheduleWorkItem(&(pTxbfInfo->Txbf_FwNdpaWorkItem));
#else
	halComTxbf_FwNdpaWorkItemCallback(pDM_Odm);
#endif
}

VOID
phydm_beamformSetSoundingClk(
	IN PVOID			pDM_VOID
	)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PHAL_TXBF_INFO	pTxbfInfo = &pDM_Odm->BeamformingInfo.TxbfInfo;

	if (PlatformIsWorkItemScheduled(&(pTxbfInfo->Txbf_ClkWorkItem)) == FALSE)
			PlatformScheduleWorkItem(&(pTxbfInfo->Txbf_ClkWorkItem));
#elif(DM_ODM_SUPPORT_TYPE == ODM_CE)
	PADAPTER	padapter = pDM_Odm->Adapter;

	rtw_run_in_thread_cmd(padapter, halComTxbf_ClkWorkItemCallback, padapter);
#else
	halComTxbf_ClkWorkItemCallback(pDM_Odm);
#endif
}

VOID
phydm_beamformSetResetTxPath(
	IN PVOID			pDM_VOID
	)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PHAL_TXBF_INFO	pTxbfInfo = &pDM_Odm->BeamformingInfo.TxbfInfo;

	if (PlatformIsWorkItemScheduled(&(pTxbfInfo->Txbf_ResetTxPathWorkItem)) == FALSE)
		PlatformScheduleWorkItem(&(pTxbfInfo->Txbf_ResetTxPathWorkItem));
#else
	halComTxbf_ResetTxPathWorkItemCallback(pDM_Odm);
#endif
}

VOID
phydm_beamformSetGetTxRate(
	IN PVOID			pDM_VOID
	)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PHAL_TXBF_INFO	pTxbfInfo = &pDM_Odm->BeamformingInfo.TxbfInfo;

	if (PlatformIsWorkItemScheduled(&(pTxbfInfo->Txbf_GetTxRateWorkItem)) == FALSE)
		PlatformScheduleWorkItem(&(pTxbfInfo->Txbf_GetTxRateWorkItem));
#else
	halComTxbf_GetTxRateWorkItemCallback(pDM_Odm);
#endif
}

VOID 
halComTxbf_EnterWorkItemCallback(
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	IN	PADAPTER		Adapter
#else
	IN PVOID			pDM_VOID
#endif
	)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
#else
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
#endif
	PHAL_TXBF_INFO	pTxbfInfo = &pDM_Odm->BeamformingInfo.TxbfInfo;
	u1Byte			Idx = pTxbfInfo->TXBFIdx;
	
	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] Start!\n", __func__));
	
	if (pDM_Odm->SupportICType & (ODM_RTL8812|ODM_RTL8821))
		HalTxbfJaguar_Enter(pDM_Odm, Idx);
	else if (pDM_Odm->SupportICType & ODM_RTL8192E)
		HalTxbf8192E_Enter(pDM_Odm, Idx);
	else if (pDM_Odm->SupportICType & ODM_RTL8814A)
		HalTxbf8814A_Enter(pDM_Odm, Idx);
	else if (pDM_Odm->SupportICType & ODM_RTL8821B)
		HalTxbf8821B_Enter(pDM_Odm, Idx);
	else if (pDM_Odm->SupportICType & ODM_RTL8822B)
		HalTxbf8822B_Enter(pDM_Odm, Idx);
}

VOID 
halComTxbf_LeaveWorkItemCallback(
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	IN	PADAPTER		Adapter
#else
	IN PVOID			pDM_VOID
#endif
	)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
#else
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
#endif
	PHAL_TXBF_INFO	pTxbfInfo = &pDM_Odm->BeamformingInfo.TxbfInfo;

	u1Byte			Idx = pTxbfInfo->TXBFIdx;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] Start!\n", __func__));

	if (pDM_Odm->SupportICType & (ODM_RTL8812|ODM_RTL8821))
		HalTxbfJaguar_Leave(pDM_Odm, Idx);
	else if (pDM_Odm->SupportICType & ODM_RTL8192E)
		HalTxbf8192E_Leave(pDM_Odm, Idx);
	else if (pDM_Odm->SupportICType & ODM_RTL8814A)
		HalTxbf8814A_Leave(pDM_Odm, Idx);
	else if (pDM_Odm->SupportICType & ODM_RTL8821B)
		HalTxbf8821B_Leave(pDM_Odm, Idx);
	else if (pDM_Odm->SupportICType & ODM_RTL8822B)
		HalTxbf8822B_Leave(pDM_Odm, Idx);
}


VOID 
halComTxbf_FwNdpaWorkItemCallback(
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	IN	PADAPTER		Adapter
#else
	IN PVOID			pDM_VOID
#endif
	)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
#else
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
#endif
	PHAL_TXBF_INFO	pTxbfInfo = &pDM_Odm->BeamformingInfo.TxbfInfo;
	u1Byte	Idx = pTxbfInfo->NdpaIdx;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] Start!\n", __func__));

	if (pDM_Odm->SupportICType & (ODM_RTL8812|ODM_RTL8821))
		HalTxbfJaguar_FwTxBF(pDM_Odm, Idx);
	else if (pDM_Odm->SupportICType & ODM_RTL8192E)
		HalTxbf8192E_FwTxBF(pDM_Odm, Idx);
	else if (pDM_Odm->SupportICType & ODM_RTL8814A)
		HalTxbf8814A_FwTxBF(pDM_Odm, Idx);
	else if (pDM_Odm->SupportICType & ODM_RTL8821B)
		HalTxbf8821B_FwTxBF(pDM_Odm, Idx);
	else if (pDM_Odm->SupportICType & ODM_RTL8822B)
		HalTxbf8822B_FwTxBF(pDM_Odm, Idx);
}

VOID
halComTxbf_ClkWorkItemCallback(
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	IN	PADAPTER		Adapter
#else
	IN PVOID			pDM_VOID
#endif
	)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
#else
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
#endif

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] Start!\n", __func__));

	if (pDM_Odm->SupportICType & ODM_RTL8812)
		HalTxbfJaguar_Clk_8812A(pDM_Odm);
}



VOID
halComTxbf_RateWorkItemCallback(	
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	IN	PADAPTER		Adapter
#else
	IN PVOID			pDM_VOID
#endif
	)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
#else
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
#endif
	PHAL_TXBF_INFO	pTxbfInfo = &pDM_Odm->BeamformingInfo.TxbfInfo;
	u1Byte			BW = pTxbfInfo->BW;
	u1Byte			Rate = pTxbfInfo->Rate;	
	
	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] Start!\n", __func__));

	if (pDM_Odm->SupportICType & ODM_RTL8812)
		HalTxbf8812A_setNDPArate(pDM_Odm, BW, Rate);
	else if (pDM_Odm->SupportICType & ODM_RTL8192E)
		HalTxbf8192E_setNDPArate(pDM_Odm, BW, Rate);
	else if (pDM_Odm->SupportICType & ODM_RTL8814A)
		HalTxbf8814A_setNDPArate(pDM_Odm, BW, Rate);
	
}


#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
VOID 
halComTxbf_FwNdpaTimerCallback(
	IN	PRT_TIMER		pTimer
	)
{

	PADAPTER		Adapter = (PADAPTER)pTimer->Adapter;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;

	PHAL_TXBF_INFO	pTxbfInfo = &pDM_Odm->BeamformingInfo.TxbfInfo;

	
	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] Start!\n", __func__));

	if (*pDM_Odm->pbFwDwRsvdPageInProgress)
		ODM_SetTimer(pDM_Odm, &(pTxbfInfo->Txbf_FwNdpaTimer), 5);
	else
		PlatformScheduleWorkItem(&(pTxbfInfo->Txbf_FwNdpaWorkItem));
}
#endif


VOID
halComTxbf_StatusWorkItemCallback(
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	IN	PADAPTER		Adapter
#else
	IN PVOID			pDM_VOID
#endif
	)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
#else
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
#endif
	PHAL_TXBF_INFO	pTxbfInfo = &pDM_Odm->BeamformingInfo.TxbfInfo;

	u1Byte			Idx = pTxbfInfo->TXBFIdx;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] Start!\n", __func__));

	if (pDM_Odm->SupportICType & (ODM_RTL8812|ODM_RTL8821))
		HalTxbfJaguar_Status(pDM_Odm, Idx);
	else if (pDM_Odm->SupportICType & ODM_RTL8192E)
		HalTxbf8192E_Status(pDM_Odm, Idx);
	else if (pDM_Odm->SupportICType & ODM_RTL8814A)
		HalTxbf8814A_Status(pDM_Odm, Idx);
	else if (pDM_Odm->SupportICType & ODM_RTL8821B)
		HalTxbf8821B_Status(pDM_Odm, Idx);
	else if (pDM_Odm->SupportICType & ODM_RTL8822B)
		HalTxbf8822B_Status(pDM_Odm, Idx);
}

VOID
halComTxbf_ResetTxPathWorkItemCallback(
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	IN	PADAPTER		Adapter
#else
	IN PVOID			pDM_VOID
#endif
	)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
#else
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
#endif
	PHAL_TXBF_INFO	pTxbfInfo = &pDM_Odm->BeamformingInfo.TxbfInfo;

	u1Byte			Idx = pTxbfInfo->TXBFIdx;

	if (pDM_Odm->SupportICType & ODM_RTL8814A)
		HalTxbf8814A_ResetTxPath(pDM_Odm, Idx);
	
}

VOID
halComTxbf_GetTxRateWorkItemCallback(
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	IN	PADAPTER		Adapter
#else
	IN PVOID			pDM_VOID
#endif
	)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
#else
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
#endif
	
	if (pDM_Odm->SupportICType & ODM_RTL8814A)
		HalTxbf8814A_GetTxRate(pDM_Odm);
}


BOOLEAN
HalComTxbf_Set(
	IN PVOID			pDM_VOID,
	IN	u1Byte			setType,
	IN	PVOID			pInBuf
	)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PBOOLEAN		pBoolean=(PBOOLEAN)pInBuf;
	pu1Byte			pU1Tmp=(pu1Byte)pInBuf;
	pu4Byte			pU4Tmp=(pu4Byte)pInBuf;
	PHAL_TXBF_INFO	pTxbfInfo = &pDM_Odm->BeamformingInfo.TxbfInfo;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] setType = 0x%X\n", __func__, setType));
	
	switch(setType){
	case TXBF_SET_SOUNDING_ENTER:
	pTxbfInfo->TXBFIdx = *pU1Tmp;
	phydm_beamformSetSoundingEnter(pDM_Odm);
	break;

	case TXBF_SET_SOUNDING_LEAVE:
	pTxbfInfo->TXBFIdx = *pU1Tmp;
	phydm_beamformSetSoundingLeave(pDM_Odm);
	break;

	case TXBF_SET_SOUNDING_RATE:
	pTxbfInfo->BW = pU1Tmp[0];
	pTxbfInfo->Rate = pU1Tmp[1];
	phydm_beamformSetSoundingRate(pDM_Odm);
	break;

	case TXBF_SET_SOUNDING_STATUS:
	pTxbfInfo->TXBFIdx = *pU1Tmp;
	phydm_beamformSetSoundingStatus(pDM_Odm);
	break;

	case TXBF_SET_SOUNDING_FW_NDPA:
	pTxbfInfo->NdpaIdx = *pU1Tmp;
	phydm_beamformSetSoundingFwNdpa(pDM_Odm);
	break;

	case TXBF_SET_SOUNDING_CLK:
	phydm_beamformSetSoundingClk(pDM_Odm);
	break;
		
	case TXBF_SET_TX_PATH_RESET:
	pTxbfInfo->TXBFIdx = *pU1Tmp;
	phydm_beamformSetResetTxPath(pDM_Odm);
	break;

	case TXBF_SET_GET_TX_RATE:
	phydm_beamformSetGetTxRate(pDM_Odm);
	break;
	
	}

	return TRUE;
}

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
BOOLEAN
HalComTxbf_Get(
	IN 	PADAPTER		Adapter,
	IN	u1Byte			getType,
	OUT	PVOID			pOutBuf
	)
{
	PHAL_DATA_TYPE		pHalData=GET_HAL_DATA(Adapter);
	PDM_ODM_T			pDM_Odm = &pHalData->DM_OutSrc;
	PBOOLEAN			pBoolean=(PBOOLEAN)pOutBuf;
	ps4Byte				pS4Tmp=(ps4Byte)pOutBuf;
	pu4Byte				pU4Tmp=(pu4Byte)pOutBuf;
	pu1Byte				pU1Tmp=(pu1Byte)pOutBuf;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] Start!\n", __func__));

	if (getType == TXBF_GET_EXPLICIT_BEAMFORMEE) {
		if (IS_HARDWARE_TYPE_OLDER_THAN_8812A(Adapter))
			*pBoolean = FALSE;
		else if (/*IS_HARDWARE_TYPE_8822B(Adapter)	||*/
				IS_HARDWARE_TYPE_8821B(Adapter) 	||
				IS_HARDWARE_TYPE_8192E(Adapter) 	||
				IS_HARDWARE_TYPE_JAGUAR(Adapter) || IS_HARDWARE_TYPE_JAGUAR_AND_JAGUAR2(Adapter))
			*pBoolean = TRUE;
		else
			*pBoolean = FALSE;
	} else if (getType == TXBF_GET_EXPLICIT_BEAMFORMER) {
		if (IS_HARDWARE_TYPE_OLDER_THAN_8812A(Adapter))
			*pBoolean = FALSE;		
		else	if (/*IS_HARDWARE_TYPE_8822B(Adapter)	||*/
				IS_HARDWARE_TYPE_8821B(Adapter) 	||
				IS_HARDWARE_TYPE_8192E(Adapter) 	||
				IS_HARDWARE_TYPE_JAGUAR(Adapter) || IS_HARDWARE_TYPE_JAGUAR_AND_JAGUAR2(Adapter)) {
			if(pHalData->RF_Type == RF_2T2R || pHalData->RF_Type == RF_3T3R)
				*pBoolean = TRUE;
			else
				*pBoolean = FALSE;
		} else
			*pBoolean = FALSE;
	} else if (getType == TXBF_GET_MU_MIMO_STA) {
#if (RTL8822B_SUPPORT == 1)
		if (/*pDM_Odm->SupportICType & (ODM_RTL8822B)*/
			IS_HARDWARE_TYPE_8822B(Adapter))
			*pBoolean = TRUE;
		else
#endif
			*pBoolean = FALSE;


	} else if (getType == TXBF_GET_MU_MIMO_AP) {
#if (RTL8822B_SUPPORT == 1)	
		if (/*pDM_Odm->SupportICType & (ODM_RTL8822B)*/
			IS_HARDWARE_TYPE_8822B(Adapter))
			*pBoolean = TRUE;
		else
#endif
			*pBoolean = FALSE;
	}
	
	return TRUE;
}	
#endif


#endif 

