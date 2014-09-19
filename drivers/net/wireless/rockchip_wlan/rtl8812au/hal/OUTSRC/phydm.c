/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *                                        
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/

//============================================================
// include files
//============================================================

#include "Mp_Precomp.h"
#include "phydm_precomp.h"


const u2Byte dB_Invert_Table[8][12] = {
	{	1,		1,		1,		2,		2,		2,		2,		3,		3,		3,		4,		4},
	{	4,		5,		6,		6,		7,		8,		9,		10,		11,		13,		14,		16},
	{	18,		20,		22,		25,		28,		32,		35,		40,		45,		50,		56,		63},
	{	71,		79,		89,		100,	112,	126,	141,	158,	178,	200,	224,	251},
	{	282,	316,	355,	398,	447,	501,	562,	631,	708,	794,	891,	1000},
	{	1122,	1259,	1413,	1585,	1778,	1995,	2239,	2512,	2818,	3162,	3548,	3981},
	{	4467,	5012,	5623,	6310,	7079,	7943,	8913,	10000,	11220,	12589,	14125,	15849},
	{	17783,	19953,	22387,	25119,	28184,	31623,	35481,	39811,	44668,	50119,	56234,	65535}};


//============================================================
// Local Function predefine.
//============================================================

VOID
odm_SwAntDetectInit(
	IN 		PDM_ODM_T 		pDM_Odm
	);





VOID
odm_AntennaDiversityInit(
	IN 		PDM_ODM_T		pDM_Odm 
);

VOID
odm_AntennaDiversity(
	IN 		PDM_ODM_T		pDM_Odm 
);

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
VOID
ODM_UpdateInitRateWorkItemCallback(
    IN PVOID            pContext
    );
#endif


VOID
odm_GlobalAdapterCheck(
	IN		VOID
	);

//Remove RAMask by RS_James



VOID
odm_IQCalibrate(
		IN	PDM_ODM_T	pDM_Odm 
		);

//remove PT by Yuchen

//Remove Edca by Yu Chen


VOID
odm_UpdatePowerTrainingState(
	IN	PDM_ODM_T	pDM_Odm
);

VOID
ODM_AsocEntry_Init(
	IN	PDM_ODM_T	pDM_Odm
	);

//============================================================
//3 Export Interface
//============================================================

VOID
ODM_InitMpDriverStatus(
	IN		PDM_ODM_T		pDM_Odm
)
{
#if(DM_ODM_SUPPORT_TYPE & ODM_WIN)

	// Decide when compile time
	#if(MP_DRIVER == 1)
	pDM_Odm->mp_mode = TRUE;
	#else
	pDM_Odm->mp_mode = FALSE;
	#endif

#elif(DM_ODM_SUPPORT_TYPE & ODM_CE)

	PADAPTER	Adapter =  pDM_Odm->Adapter;

	// Update information every period
	pDM_Odm->mp_mode = (BOOLEAN)Adapter->registrypriv.mp_mode;

#else

	// MP mode is always false at AP side
	pDM_Odm->mp_mode = FALSE;

#endif
}

VOID
ODM_UpdateMpDriverStatus(
	IN		PDM_ODM_T		pDM_Odm
)
{
#if(DM_ODM_SUPPORT_TYPE & ODM_WIN)

	// Do nothing.

#elif(DM_ODM_SUPPORT_TYPE & ODM_CE)
	PADAPTER	Adapter =  pDM_Odm->Adapter;

	// Update information erery period
	pDM_Odm->mp_mode = (BOOLEAN)Adapter->registrypriv.mp_mode;

#else

	// Do nothing.

#endif
}

VOID
odm_CommonInfoSelfInit(
	IN		PDM_ODM_T		pDM_Odm
	)
{
	pFAT_T			pDM_FatTable = &pDM_Odm->DM_FatTable;
	pDM_Odm->bCckHighPower = (BOOLEAN) ODM_GetBBReg(pDM_Odm, ODM_REG(CCK_RPT_FORMAT,pDM_Odm), ODM_BIT(CCK_RPT_FORMAT,pDM_Odm));		
	pDM_Odm->RFPathRxEnable = (u1Byte) ODM_GetBBReg(pDM_Odm, ODM_REG(BB_RX_PATH,pDM_Odm), ODM_BIT(BB_RX_PATH,pDM_Odm));
#if (DM_ODM_SUPPORT_TYPE != ODM_CE)	
	pDM_Odm->pbNet_closed = &pDM_Odm->BOOLEAN_temp;
#endif

	PHYDM_InitDebugSetting(pDM_Odm);
	ODM_InitMpDriverStatus(pDM_Odm);

	pDM_Odm->TxRate = 0xFF;

}

VOID
odm_CommonInfoSelfUpdate(
	IN		PDM_ODM_T		pDM_Odm
	)
{
	u1Byte	EntryCnt=0;
	u1Byte	i;
	PSTA_INFO_T   	pEntry;

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

	PADAPTER	Adapter =  pDM_Odm->Adapter;
	PMGNT_INFO	pMgntInfo = &Adapter->MgntInfo;

	pEntry = pDM_Odm->pODM_StaInfo[0];
	if(pMgntInfo->mAssoc)
	{
		pEntry->bUsed=TRUE;
		for (i=0; i<6; i++)
			pEntry->MacAddr[i] = pMgntInfo->Bssid[i];
	}
	else
	{
		pEntry->bUsed=FALSE;
		for (i=0; i<6; i++)
			pEntry->MacAddr[i] = 0;
	}

	//STA mode is linked to AP
	if(IS_STA_VALID(pDM_Odm->pODM_StaInfo[0]) && !ACTING_AS_AP(Adapter))
		pDM_Odm->bsta_state = TRUE;
	else
		pDM_Odm->bsta_state = FALSE;
#endif


	if(*(pDM_Odm->pBandWidth) == ODM_BW40M)
	{
		if(*(pDM_Odm->pSecChOffset) == 1)
			pDM_Odm->ControlChannel = *(pDM_Odm->pChannel) -2;
		else if(*(pDM_Odm->pSecChOffset) == 2)
			pDM_Odm->ControlChannel = *(pDM_Odm->pChannel) +2;
	}
	else
		pDM_Odm->ControlChannel = *(pDM_Odm->pChannel);

	for (i=0; i<ODM_ASSOCIATE_ENTRY_NUM; i++)
	{
		pEntry = pDM_Odm->pODM_StaInfo[i];
		if(IS_STA_VALID(pEntry))
			EntryCnt++;
	}
	
	if(EntryCnt == 1)
		pDM_Odm->bOneEntryOnly = TRUE;
	else
		pDM_Odm->bOneEntryOnly = FALSE;

	// Update MP driver status
	ODM_UpdateMpDriverStatus(pDM_Odm);
}

VOID
odm_CommonInfoSelfReset(
	IN		PDM_ODM_T		pDM_Odm
	)
{
#if( DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
	pDM_Odm->PhyDbgInfo.NumQryBeaconPkt = 0;
#endif
}

PVOID
PhyDM_Get_Structure(
	IN		PDM_ODM_T		pDM_Odm,
	IN		u1Byte			Structure_Type
)

{
	PVOID	pStruct = NULL;
#if RTL8195A_SUPPORT
	switch (Structure_Type){
		case	PhyDM_FalseAlmCnt:
			pStruct = &FalseAlmCnt;
		break;
		
		case	PhyDM_CfoTrack:
			pStruct = &DM_CfoTrack;
		break;
		
		default:
		break;
	}

#else
	switch (Structure_Type){
		case	PhyDM_FalseAlmCnt:
			pStruct = &(pDM_Odm->FalseAlmCnt);
		break;
		
		case	PhyDM_CfoTrack:
			pStruct = &(pDM_Odm->DM_CfoTrack);
		break;
		
		default:
		break;
	}

#endif
	return	pStruct;
}

VOID
odm_HWSetting(
	IN		PDM_ODM_T		pDM_Odm
	)
{
#if (RTL8821A_SUPPORT == 1)
	if(pDM_Odm->SupportICType & ODM_RTL8821)
		odm_HWSetting_8821A(pDM_Odm);
#endif

}

//
// 2011/09/21 MH Add to describe different team necessary resource allocate??
//
VOID
ODM_DMInit(
	IN		PDM_ODM_T		pDM_Odm
	)
{

	odm_CommonInfoSelfInit(pDM_Odm);
	odm_DIGInit(pDM_Odm);
	Phydm_NHMCounterStatisticsInit(pDM_Odm);
	Phydm_AdaptivityInit(pDM_Odm);
	odm_RateAdaptiveMaskInit(pDM_Odm);
	ODM_CfoTrackingInit(pDM_Odm);
	ODM_EdcaTurboInit(pDM_Odm);
	odm_RSSIMonitorInit(pDM_Odm);
	odm_TXPowerTrackingInit(pDM_Odm);
	odm_AntennaDiversityInit(pDM_Odm);
	odm_AutoChannelSelectInit(pDM_Odm);

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
	ODM_ClearTxPowerTrackingState(pDM_Odm);
	odm_PathDiversityInit(pDM_Odm);
#endif

	if(pDM_Odm->SupportICType & ODM_IC_11N_SERIES)
	{
		odm_DynamicBBPowerSavingInit(pDM_Odm);
		odm_DynamicTxPowerInit(pDM_Odm);

#if (RTL8188E_SUPPORT == 1)
		if(pDM_Odm->SupportICType==ODM_RTL8188E)
		{
			odm_PrimaryCCA_Init(pDM_Odm);
			ODM_RAInfo_Init_all(pDM_Odm);
		}
#endif

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
	
	#if (RTL8723B_SUPPORT == 1)
		if(pDM_Odm->SupportICType == ODM_RTL8723B)
			odm_SwAntDetectInit(pDM_Odm);
	#endif

	#if (RTL8192E_SUPPORT == 1)
		if(pDM_Odm->SupportICType==ODM_RTL8192E)
			odm_PrimaryCCA_Check_Init(pDM_Odm);
	#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	#if (RTL8723A_SUPPORT == 1)
		if(pDM_Odm->SupportICType == ODM_RTL8723A)
			odm_PSDMonitorInit(pDM_Odm);
	#endif

	#if (RTL8192D_SUPPORT == 1)
		if(pDM_Odm->SupportICType==ODM_RTL8192D)
			odm_PathDivInit_92D(pDM_Odm);
	#endif

	#if ((RTL8192C_SUPPORT == 1) || (RTL8192D_SUPPORT == 1))
		if(pDM_Odm->SupportICType & (ODM_RTL8192C|ODM_RTL8192D))
			odm_RXHPInit(pDM_Odm);
	#endif
#endif
#endif

	}

}

VOID
ODM_DMReset(
	IN		PDM_ODM_T		pDM_Odm
	)
{
        #if (defined(CONFIG_HW_ANTENNA_DIVERSITY))
	ODM_AntDivReset(pDM_Odm);
        #endif
}

//
// 2011/09/20 MH This is the entry pointer for all team to execute HW out source DM.
// You can not add any dummy function here, be care, you can only use DM structure
// to perform any new ODM_DM.
//
VOID
ODM_DMWatchdog(
	IN		PDM_ODM_T		pDM_Odm
	)
{
	ODM_AsocEntry_Init(pDM_Odm);
	odm_CommonInfoSelfUpdate(pDM_Odm);
	phydm_BasicDbgMessage(pDM_Odm);
	odm_HWSetting(pDM_Odm);
	odm_FalseAlarmCounterStatistics(pDM_Odm);
	odm_RSSIMonitorCheck(pDM_Odm);

	if(*(pDM_Odm->pbPowerSaving) == TRUE)
	{
		odm_DIGbyRSSI_LPS(pDM_Odm);
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("DMWatchdog in power saving mode\n"));
		return;
	}
	
	Phydm_CheckAdaptivity(pDM_Odm);
	odm_UpdatePowerTrainingState(pDM_Odm);
	odm_DIG(pDM_Odm);
	{
		pDIG_T	pDM_DigTable = &pDM_Odm->DM_DigTable;
		pDM_Odm->bAdaOn = Phydm_Adaptivity(pDM_Odm, pDM_DigTable->CurIGValue);
	}
	odm_CCKPacketDetectionThresh(pDM_Odm);
	odm_RefreshRateAdaptiveMask(pDM_Odm);
	odm_RefreshBasicRateMask(pDM_Odm);
	odm_DynamicBBPowerSaving(pDM_Odm);
	odm_EdcaTurboCheck(pDM_Odm);
	odm_PathDiversity(pDM_Odm);
	ODM_CfoTracking(pDM_Odm);
	odm_DynamicTxPower(pDM_Odm);
	odm_AntennaDiversity(pDM_Odm);

#if( DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))

	ODM_TXPowerTrackingCheck(pDM_Odm);

	if(pDM_Odm->SupportICType & ODM_IC_11AC_SERIES)
		odm_IQCalibrate(pDM_Odm);
	else 
#endif
	if(pDM_Odm->SupportICType & ODM_IC_11N_SERIES)
	{
#if (RTL8192D_SUPPORT == 1)
	        if(pDM_Odm->SupportICType==ODM_RTL8192D)
			ODM_DynamicEarlyMode(pDM_Odm);
#endif
	        
#if (RTL8188E_SUPPORT == 1)
	        if(pDM_Odm->SupportICType==ODM_RTL8188E)
	                odm_DynamicPrimaryCCA(pDM_Odm);	
#endif

#if( DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))

	#if (RTL8192E_SUPPORT == 1)
		if(pDM_Odm->SupportICType==ODM_RTL8192E)
			odm_DynamicPrimaryCCA_Check(pDM_Odm); 
	#endif

#if( DM_ODM_SUPPORT_TYPE == ODM_WIN)
	#if ((RTL8192C_SUPPORT == 1) || (RTL8192D_SUPPORT == 1))
		if(pDM_Odm->SupportICType & (ODM_RTL8192C|ODM_RTL8192D))
			odm_RXHP(pDM_Odm);
	#endif
#endif
#endif
	}

#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	odm_dtc(pDM_Odm);
#endif

	odm_CommonInfoSelfReset(pDM_Odm);
	
}


//
// Init /.. Fixed HW value. Only init time.
//
VOID
ODM_CmnInfoInit(
	IN		PDM_ODM_T		pDM_Odm,
	IN		ODM_CMNINFO_E	CmnInfo,
	IN		u4Byte			Value	
	)
{
	//
	// This section is used for init value
	//
	switch	(CmnInfo)
	{
		//
		// Fixed ODM value.
		//
		case	ODM_CMNINFO_ABILITY:
			pDM_Odm->SupportAbility = (u4Byte)Value;
			break;

		case	ODM_CMNINFO_RF_TYPE:
			pDM_Odm->RFType = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_PLATFORM:
			pDM_Odm->SupportPlatform = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_INTERFACE:
			pDM_Odm->SupportInterface = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_MP_TEST_CHIP:
			pDM_Odm->bIsMPChip= (u1Byte)Value;
			break;
            
		case	ODM_CMNINFO_IC_TYPE:
			pDM_Odm->SupportICType = Value;
			break;

		case	ODM_CMNINFO_CUT_VER:
			pDM_Odm->CutVersion = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_FAB_VER:
			pDM_Odm->FabVersion = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_RFE_TYPE:
			pDM_Odm->RFEType = (u1Byte)Value;
			break;

		case    ODM_CMNINFO_RF_ANTENNA_TYPE:
			pDM_Odm->AntDivType= (u1Byte)Value;
			break;

		case	ODM_CMNINFO_BOARD_TYPE:
			pDM_Odm->BoardType = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_PACKAGE_TYPE:
			pDM_Odm->PackageType = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_EXT_LNA:
			pDM_Odm->ExtLNA = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_5G_EXT_LNA:
			pDM_Odm->ExtLNA5G = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_EXT_PA:
			pDM_Odm->ExtPA = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_5G_EXT_PA:
			pDM_Odm->ExtPA5G = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_GPA:
			pDM_Odm->TypeGPA= (ODM_TYPE_GPA_E)Value;
			break;
		case	ODM_CMNINFO_APA:
			pDM_Odm->TypeAPA= (ODM_TYPE_APA_E)Value;
			break;
		case	ODM_CMNINFO_GLNA:
			pDM_Odm->TypeGLNA= (ODM_TYPE_GLNA_E)Value;
			break;
		case	ODM_CMNINFO_ALNA:
			pDM_Odm->TypeALNA= (ODM_TYPE_ALNA_E)Value;
			break;

		case	ODM_CMNINFO_EXT_TRSW:
			pDM_Odm->ExtTRSW = (u1Byte)Value;
			break;
		case 	ODM_CMNINFO_PATCH_ID:
			pDM_Odm->PatchID = (u1Byte)Value;
			break;
		case 	ODM_CMNINFO_BINHCT_TEST:
			pDM_Odm->bInHctTest = (BOOLEAN)Value;
			break;
		case 	ODM_CMNINFO_BWIFI_TEST:
			pDM_Odm->bWIFITest = (BOOLEAN)Value;
			break;	
		case	ODM_CMNINFO_SMART_CONCURRENT:
			pDM_Odm->bDualMacSmartConcurrent = (BOOLEAN )Value;
			break;
		case	ODM_CMNINFO_DOMAIN_CODE_2G:
			pDM_Odm->odm_Regulation2_4G = (u1Byte)Value;
			break;
		case	ODM_CMNINFO_DOMAIN_CODE_5G:
			pDM_Odm->odm_Regulation5G = (u1Byte)Value;
			break;
		case	ODM_CMNINFO_IQKFWOFFLOAD:
			pDM_Odm->IQKFWOffload = (u1Byte)Value;
			break;
		//To remove the compiler warning, must add an empty default statement to handle the other values.	
		default:
			//do nothing
			break;	
		
	}

}


VOID
ODM_CmnInfoHook(
	IN		PDM_ODM_T		pDM_Odm,
	IN		ODM_CMNINFO_E	CmnInfo,
	IN		PVOID			pValue	
	)
{
	//
	// Hook call by reference pointer.
	//
	switch	(CmnInfo)
	{
		//
		// Dynamic call by reference pointer.
		//
		case	ODM_CMNINFO_MAC_PHY_MODE:
			pDM_Odm->pMacPhyMode = (u1Byte *)pValue;
			break;
		
		case	ODM_CMNINFO_TX_UNI:
			pDM_Odm->pNumTxBytesUnicast = (u8Byte *)pValue;
			break;

		case	ODM_CMNINFO_RX_UNI:
			pDM_Odm->pNumRxBytesUnicast = (u8Byte *)pValue;
			break;

		case	ODM_CMNINFO_WM_MODE:
			pDM_Odm->pWirelessMode = (u1Byte *)pValue;
			break;

		case	ODM_CMNINFO_BAND:
			pDM_Odm->pBandType = (u1Byte *)pValue;
			break;

		case	ODM_CMNINFO_SEC_CHNL_OFFSET:
			pDM_Odm->pSecChOffset = (u1Byte *)pValue;
			break;

		case	ODM_CMNINFO_SEC_MODE:
			pDM_Odm->pSecurity = (u1Byte *)pValue;
			break;

		case	ODM_CMNINFO_BW:
			pDM_Odm->pBandWidth = (u1Byte *)pValue;
			break;

		case	ODM_CMNINFO_CHNL:
			pDM_Odm->pChannel = (u1Byte *)pValue;
			break;
		
		case	ODM_CMNINFO_DMSP_GET_VALUE:
			pDM_Odm->pbGetValueFromOtherMac = (BOOLEAN *)pValue;
			break;

		case	ODM_CMNINFO_BUDDY_ADAPTOR:
			pDM_Odm->pBuddyAdapter = (PADAPTER *)pValue;
			break;

		case	ODM_CMNINFO_DMSP_IS_MASTER:
			pDM_Odm->pbMasterOfDMSP = (BOOLEAN *)pValue;
			break;

		case	ODM_CMNINFO_SCAN:
			pDM_Odm->pbScanInProcess = (BOOLEAN *)pValue;
			break;

		case	ODM_CMNINFO_POWER_SAVING:
			pDM_Odm->pbPowerSaving = (BOOLEAN *)pValue;
			break;

		case	ODM_CMNINFO_ONE_PATH_CCA:
			pDM_Odm->pOnePathCCA = (u1Byte *)pValue;
			break;

		case	ODM_CMNINFO_DRV_STOP:
			pDM_Odm->pbDriverStopped =  (BOOLEAN *)pValue;
			break;

		case	ODM_CMNINFO_PNP_IN:
			pDM_Odm->pbDriverIsGoingToPnpSetPowerSleep =  (BOOLEAN *)pValue;
			break;

		case	ODM_CMNINFO_INIT_ON:
			pDM_Odm->pinit_adpt_in_progress =  (BOOLEAN *)pValue;
			break;

		case	ODM_CMNINFO_ANT_TEST:
			pDM_Odm->pAntennaTest =  (u1Byte *)pValue;
			break;

		case	ODM_CMNINFO_NET_CLOSED:
			pDM_Odm->pbNet_closed = (BOOLEAN *)pValue;
			break;

		case 	ODM_CMNINFO_FORCED_RATE:
			pDM_Odm->pForcedDataRate = (pu2Byte)pValue;
			break;

		case  ODM_CMNINFO_FORCED_IGI_LB:
			pDM_Odm->pu1ForcedIgiLb = (u1Byte *)pValue;
			break;

		case	ODM_CMNINFO_P2P_LINK:
			pDM_Odm->DM_DigTable.pbP2pLinkInProgress = (u1Byte *)pValue;
			break;

		case ODM_CMNINFO_FCS_MODE:
			pDM_Odm->pIsFcsModeEnable = (BOOLEAN *)pValue;
			break;
//sd7 only

		//case	ODM_CMNINFO_RTSTA_AID:
		//	pDM_Odm->pAidMap =  (u1Byte *)pValue;
		//	break;

		//case	ODM_CMNINFO_BT_COEXIST:
		//	pDM_Odm->BTCoexist = (BOOLEAN *)pValue;		

		//case	ODM_CMNINFO_STA_STATUS:
			//pDM_Odm->pODM_StaInfo[] = (PSTA_INFO_T)pValue;
			//break;

		//case	ODM_CMNINFO_PHY_STATUS:
		//	pDM_Odm->pPhyInfo = (ODM_PHY_INFO *)pValue;
		//	break;

		//case	ODM_CMNINFO_MAC_STATUS:
		//	pDM_Odm->pMacInfo = (ODM_MAC_INFO *)pValue;
		//	break;
		//To remove the compiler warning, must add an empty default statement to handle the other values.				
		default:
			//do nothing
			break;

	}

}


VOID
ODM_CmnInfoPtrArrayHook(
	IN		PDM_ODM_T		pDM_Odm,
	IN		ODM_CMNINFO_E	CmnInfo,
	IN		u2Byte			Index,
	IN		PVOID			pValue	
	)
{
	//
	// Hook call by reference pointer.
	//
	switch	(CmnInfo)
	{
		//
		// Dynamic call by reference pointer.
		//		
		case	ODM_CMNINFO_STA_STATUS:
			pDM_Odm->pODM_StaInfo[Index] = (PSTA_INFO_T)pValue;
			break;		
		//To remove the compiler warning, must add an empty default statement to handle the other values.				
		default:
			//do nothing
			break;
	}
	
}


//
// Update Band/CHannel/.. The values are dynamic but non-per-packet.
//
VOID
ODM_CmnInfoUpdate(
	IN		PDM_ODM_T		pDM_Odm,
	IN		u4Byte			CmnInfo,
	IN		u8Byte			Value	
	)
{
	//
	// This init variable may be changed in run time.
	//
	switch	(CmnInfo)
	{
		case ODM_CMNINFO_LINK_IN_PROGRESS:
			pDM_Odm->bLinkInProcess = (BOOLEAN)Value;
			break;
		
		case	ODM_CMNINFO_ABILITY:
			pDM_Odm->SupportAbility = (u4Byte)Value;
			break;

		case	ODM_CMNINFO_RF_TYPE:
			pDM_Odm->RFType = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_WIFI_DIRECT:
			pDM_Odm->bWIFI_Direct = (BOOLEAN)Value;
			break;

		case	ODM_CMNINFO_WIFI_DISPLAY:
			pDM_Odm->bWIFI_Display = (BOOLEAN)Value;
			break;

		case	ODM_CMNINFO_LINK:
			pDM_Odm->bLinked = (BOOLEAN)Value;
			break;
			
		case	ODM_CMNINFO_STATION_STATE:
			pDM_Odm->bsta_state = (BOOLEAN)Value;
			break;
			
		case	ODM_CMNINFO_RSSI_MIN:
			pDM_Odm->RSSI_Min= (u1Byte)Value;
			break;

		case	ODM_CMNINFO_DBG_COMP:
			pDM_Odm->DebugComponents = Value;
			break;

		case	ODM_CMNINFO_DBG_LEVEL:
			pDM_Odm->DebugLevel = (u4Byte)Value;
			break;
		case	ODM_CMNINFO_RA_THRESHOLD_HIGH:
			pDM_Odm->RateAdaptive.HighRSSIThresh = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_RA_THRESHOLD_LOW:
			pDM_Odm->RateAdaptive.LowRSSIThresh = (u1Byte)Value;
			break;
		// The following is for BT HS mode and BT coexist mechanism.
		case ODM_CMNINFO_BT_ENABLED:
			pDM_Odm->bBtEnabled = (BOOLEAN)Value;
			break;
			
		case ODM_CMNINFO_BT_HS_CONNECT_PROCESS:
			pDM_Odm->bBtConnectProcess = (BOOLEAN)Value;
			break;
		
		case ODM_CMNINFO_BT_HS_RSSI:
			pDM_Odm->btHsRssi = (u1Byte)Value;
			break;
			
		case	ODM_CMNINFO_BT_OPERATION:
			pDM_Odm->bBtHsOperation = (BOOLEAN)Value;
			break;

		case	ODM_CMNINFO_BT_LIMITED_DIG:
			pDM_Odm->bBtLimitedDig = (BOOLEAN)Value;
			break;	

		case	ODM_CMNINFO_BT_DISABLE_EDCA:
			pDM_Odm->bBtDisableEdcaTurbo = (BOOLEAN)Value;
			break;


#if(DM_ODM_SUPPORT_TYPE & ODM_AP)		// for repeater mode add by YuChen 2014.06.23
#ifdef UNIVERSAL_REPEATER
		case	ODM_CMNINFO_VXD_LINK:
			pDM_Odm->VXD_bLinked= (BOOLEAN)Value;
			break;
#endif
#endif
/*
		case	ODM_CMNINFO_OP_MODE:
			pDM_Odm->OPMode = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_WM_MODE:
			pDM_Odm->WirelessMode = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_BAND:
			pDM_Odm->BandType = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_SEC_CHNL_OFFSET:
			pDM_Odm->SecChOffset = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_SEC_MODE:
			pDM_Odm->Security = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_BW:
			pDM_Odm->BandWidth = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_CHNL:
			pDM_Odm->Channel = (u1Byte)Value;
			break;			
*/	
                default:
			//do nothing
			break;
	}

	
}


#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
VOID
ODM_InitAllWorkItems(IN PDM_ODM_T	pDM_Odm )
{
#if USE_WORKITEM
	PADAPTER		pAdapter = pDM_Odm->Adapter;

	ODM_InitializeWorkItem(	pDM_Odm, 
							&pDM_Odm->DM_SWAT_Table.SwAntennaSwitchWorkitem_8723B, 
							(RT_WORKITEM_CALL_BACK)ODM_SW_AntDiv_WorkitemCallback,
							(PVOID)pAdapter,
							"AntennaSwitchWorkitem");
	
	ODM_InitializeWorkItem(	pDM_Odm, 
							&pDM_Odm->DM_SWAT_Table.SwAntennaSwitchWorkitem, 
							(RT_WORKITEM_CALL_BACK)odm_SwAntDivChkAntSwitchWorkitemCallback,
							(PVOID)pAdapter,
							"AntennaSwitchWorkitem");
	

	ODM_InitializeWorkItem(
		pDM_Odm,
		&(pDM_Odm->PathDivSwitchWorkitem), 
		(RT_WORKITEM_CALL_BACK)odm_PathDivChkAntSwitchWorkitemCallback, 
		(PVOID)pAdapter,
		"SWAS_WorkItem");

	ODM_InitializeWorkItem(
		pDM_Odm,
		&(pDM_Odm->CCKPathDiversityWorkitem), 
		(RT_WORKITEM_CALL_BACK)odm_CCKTXPathDiversityWorkItemCallback, 
		(PVOID)pAdapter,
		"CCKTXPathDiversityWorkItem");

	ODM_InitializeWorkItem(
		pDM_Odm,
		&(pDM_Odm->MPT_DIGWorkitem), 
		(RT_WORKITEM_CALL_BACK)odm_MPT_DIGWorkItemCallback, 
		(PVOID)pAdapter,
		"MPT_DIGWorkitem");

	ODM_InitializeWorkItem(
		pDM_Odm,
		&(pDM_Odm->RaRptWorkitem), 
		(RT_WORKITEM_CALL_BACK)ODM_UpdateInitRateWorkItemCallback, 
		(PVOID)pAdapter,
		"RaRptWorkitem");
	
#if(defined(CONFIG_HW_ANTENNA_DIVERSITY))
#if (RTL8188E_SUPPORT == 1)
	ODM_InitializeWorkItem(
		pDM_Odm,
		&(pDM_Odm->FastAntTrainingWorkitem), 
		(RT_WORKITEM_CALL_BACK)odm_FastAntTrainingWorkItemCallback, 
		(PVOID)pAdapter,
		"FastAntTrainingWorkitem");
#endif
#endif
	ODM_InitializeWorkItem(
		pDM_Odm,
		&(pDM_Odm->DM_RXHP_Table.PSDTimeWorkitem), 
		(RT_WORKITEM_CALL_BACK)odm_PSD_RXHPWorkitemCallback, 
		(PVOID)pAdapter,
		"PSDRXHP_WorkItem");  
#endif
}

VOID
ODM_FreeAllWorkItems(IN PDM_ODM_T	pDM_Odm )
{
#if USE_WORKITEM
	ODM_FreeWorkItem(	&(pDM_Odm->DM_SWAT_Table.SwAntennaSwitchWorkitem_8723B));
	
	ODM_FreeWorkItem(	&(pDM_Odm->DM_SWAT_Table.SwAntennaSwitchWorkitem));

	ODM_FreeWorkItem(&(pDM_Odm->PathDivSwitchWorkitem));      

	ODM_FreeWorkItem(&(pDM_Odm->CCKPathDiversityWorkitem));
	
	ODM_FreeWorkItem(&(pDM_Odm->FastAntTrainingWorkitem));

	ODM_FreeWorkItem(&(pDM_Odm->MPT_DIGWorkitem));

	ODM_FreeWorkItem(&(pDM_Odm->RaRptWorkitem));

	ODM_FreeWorkItem((&pDM_Odm->DM_RXHP_Table.PSDTimeWorkitem));
#endif

}
#endif

/*
VOID
odm_FindMinimumRSSI(
	IN		PDM_ODM_T		pDM_Odm
	)
{
	u4Byte	i;
	u1Byte	RSSI_Min = 0xFF;

	for(i=0; i<ODM_ASSOCIATE_ENTRY_NUM; i++)
	{
//		if(pDM_Odm->pODM_StaInfo[i] != NULL)
		if(IS_STA_VALID(pDM_Odm->pODM_StaInfo[i]) )
		{
			if(pDM_Odm->pODM_StaInfo[i]->RSSI_Ave < RSSI_Min)
			{
				RSSI_Min = pDM_Odm->pODM_StaInfo[i]->RSSI_Ave;
			}
		}
	}

	pDM_Odm->RSSI_Min = RSSI_Min;

}

VOID
odm_IsLinked(
	IN		PDM_ODM_T		pDM_Odm
	)
{
	u4Byte i;
	BOOLEAN Linked = FALSE;
	
	for(i=0; i<ODM_ASSOCIATE_ENTRY_NUM; i++)
	{
			if(IS_STA_VALID(pDM_Odm->pODM_StaInfo[i]) )
			{			
				Linked = TRUE;
				break;
			}
		
	}

	pDM_Odm->bLinked = Linked;
}
*/


//3============================================================
//3 DIG
//3============================================================
/*-----------------------------------------------------------------------------
 * Function:	odm_DIGInit()
 *
 * Overview:	Set DIG scheme init value.
 *
 * Input:		NONE
 *
 * Output:		NONE
 *
 * Return:		NONE
 *
 * Revised History:
 *	When		Who		Remark
 *
 *---------------------------------------------------------------------------*/

//Remove DIG by yuchen

//Remove DIG and FA check by Yu Chen


//3============================================================
//3 BB Power Save
//3============================================================

//Remove BB power saving by Yuchen

//3============================================================
//3 RATR MASK
//3============================================================
//3============================================================
//3 Rate Adaptive
//3============================================================

//Remove RAMask by RS_James

//3============================================================
//3 Dynamic Tx Power
//3============================================================

//Remove BY YuChen

//Remove  Rssimonitorcheck related function to odm_rssimonitorcheck.c 


VOID
ODM_InitAllTimers(
	IN PDM_ODM_T	pDM_Odm 
	)
{
#if(defined(CONFIG_HW_ANTENNA_DIVERSITY))
	ODM_AntDivTimers(pDM_Odm,INIT_ANTDIV_TIMMER);
#elif(defined(CONFIG_SW_ANTENNA_DIVERSITY))
	ODM_InitializeTimer(pDM_Odm,&pDM_Odm->DM_SWAT_Table.SwAntennaSwitchTimer,
		(RT_TIMER_CALL_BACK)odm_SwAntDivChkAntSwitchCallback, NULL, "SwAntennaSwitchTimer");
#endif
	
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	ODM_InitializeTimer(pDM_Odm, &pDM_Odm->PSDTimer, 
		(RT_TIMER_CALL_BACK)dm_PSDMonitorCallback, NULL, "PSDTimer");
	//
	//Path Diversity
	//Neil Chen--2011--06--16--  / 2012/02/23 MH Revise Arch.
	//
	ODM_InitializeTimer(pDM_Odm, &pDM_Odm->PathDivSwitchTimer, 
		(RT_TIMER_CALL_BACK)odm_PathDivChkAntSwitchCallback, NULL, "PathDivTimer");

	ODM_InitializeTimer(pDM_Odm, &pDM_Odm->CCKPathDiversityTimer, 
		(RT_TIMER_CALL_BACK)odm_CCKTXPathDiversityCallback, NULL, "CCKPathDiversityTimer");

	ODM_InitializeTimer(pDM_Odm, &pDM_Odm->MPT_DIGTimer, 
		(RT_TIMER_CALL_BACK)odm_MPT_DIGCallback, NULL, "MPT_DIGTimer");

	ODM_InitializeTimer(pDM_Odm, &pDM_Odm->DM_RXHP_Table.PSDTimer,
		(RT_TIMER_CALL_BACK)odm_PSD_RXHPCallback, NULL, "PSDRXHPTimer");  
#endif	
}

VOID
ODM_CancelAllTimers(
	IN PDM_ODM_T	pDM_Odm 
	)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	//
	// 2012/01/12 MH Temp BSOD fix. We need to find NIC allocate mem fail reason in 
	// win7 platform.
	//
	HAL_ADAPTER_STS_CHK(pDM_Odm)
#endif	

#if(defined(CONFIG_HW_ANTENNA_DIVERSITY))
	ODM_AntDivTimers(pDM_Odm,CANCEL_ANTDIV_TIMMER);
#elif(defined(CONFIG_SW_ANTENNA_DIVERSITY))
	ODM_CancelTimer(pDM_Odm,&pDM_Odm->DM_SWAT_Table.SwAntennaSwitchTimer);
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	ODM_CancelTimer(pDM_Odm, &pDM_Odm->PSDTimer);	
	//
	//Path Diversity
	//Neil Chen--2011--06--16--  / 2012/02/23 MH Revise Arch.
	//
	ODM_CancelTimer(pDM_Odm, &pDM_Odm->PathDivSwitchTimer);

	ODM_CancelTimer(pDM_Odm, &pDM_Odm->CCKPathDiversityTimer);

	ODM_CancelTimer(pDM_Odm, &pDM_Odm->MPT_DIGTimer);

	ODM_CancelTimer(pDM_Odm, &pDM_Odm->DM_RXHP_Table.PSDTimer);
#endif	
}


VOID
ODM_ReleaseAllTimers(
	IN PDM_ODM_T	pDM_Odm 
	)
{
#if(defined(CONFIG_HW_ANTENNA_DIVERSITY))
	ODM_AntDivTimers(pDM_Odm,RELEASE_ANTDIV_TIMMER);
#elif(defined(CONFIG_SW_ANTENNA_DIVERSITY))
	ODM_ReleaseTimer(pDM_Odm,&pDM_Odm->DM_SWAT_Table.SwAntennaSwitchTimer);
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

	ODM_ReleaseTimer(pDM_Odm, &pDM_Odm->PSDTimer);
	//
	//Path Diversity
	//Neil Chen--2011--06--16--  / 2012/02/23 MH Revise Arch.
	//
	ODM_ReleaseTimer(pDM_Odm, &pDM_Odm->PathDivSwitchTimer);

	ODM_ReleaseTimer(pDM_Odm, &pDM_Odm->CCKPathDiversityTimer);

	ODM_ReleaseTimer(pDM_Odm, &pDM_Odm->MPT_DIGTimer);

	ODM_ReleaseTimer(pDM_Odm, &pDM_Odm->DM_RXHP_Table.PSDTimer); 
#endif	
}


//3============================================================
//3 Tx Power Tracking
//3============================================================

VOID
odm_IQCalibrate(
		IN	PDM_ODM_T	pDM_Odm 
		)
{
	PADAPTER	Adapter = pDM_Odm->Adapter;

#if( DM_ODM_SUPPORT_TYPE == ODM_WIN)	
	if(*pDM_Odm->pIsFcsModeEnable)
		return;
#endif

	if(!IS_HARDWARE_TYPE_JAGUAR(Adapter))
		return;
	else if(IS_HARDWARE_TYPE_8812AU(Adapter))
		return;
#if (RTL8821A_SUPPORT == 1)
	if(pDM_Odm->bLinked)
	{
		if((*pDM_Odm->pChannel != pDM_Odm->preChannel) && (!*pDM_Odm->pbScanInProcess))
		{
			pDM_Odm->preChannel = *pDM_Odm->pChannel;
			pDM_Odm->LinkedInterval = 0;
		}

		if(pDM_Odm->LinkedInterval < 3)
			pDM_Odm->LinkedInterval++;
		
		if(pDM_Odm->LinkedInterval == 2)
		{
			// Mark out IQK flow to prevent tx stuck. by Maddest 20130306
			// Open it verified by James 20130715
			PHY_IQCalibrate_8821A(pDM_Odm, FALSE);
		}
	}
	else
		pDM_Odm->LinkedInterval = 0;
#endif
}



//antenna mapping info
// 1: right-side antenna
// 2/0: left-side antenna
//PDM_SWAT_Table->CCK_Ant1_Cnt /OFDM_Ant1_Cnt:  for right-side antenna:   Ant:1    RxDefaultAnt1
//PDM_SWAT_Table->CCK_Ant2_Cnt /OFDM_Ant2_Cnt:  for left-side antenna:     Ant:0    RxDefaultAnt2
// We select left antenna as default antenna in initial process, modify it as needed
//

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

// Only for 8723A SW ANT DIV INIT--2012--07--17
VOID
odm_SwAntDivInit_NIC_8723A(
	IN	PDM_ODM_T		pDM_Odm)
{
	pSWAT_T		pDM_SWAT_Table = &pDM_Odm->DM_SWAT_Table;
	PADAPTER		Adapter = pDM_Odm->Adapter;
	
	u1Byte 			btAntNum=BT_GetPgAntNum(Adapter);

	if(IS_HARDWARE_TYPE_8723A(Adapter))
	{
		pDM_SWAT_Table->ANTA_ON =TRUE;
		
		// Set default antenna B status by PG
		if(btAntNum == 2)
			pDM_SWAT_Table->ANTB_ON = TRUE;
		else if(btAntNum == 1)
			pDM_SWAT_Table->ANTB_ON = FALSE;
		else
			pDM_SWAT_Table->ANTB_ON = TRUE;
	}	
	
}

#endif //end #ifMP



//3============================================================
//3 SW Antenna Diversity
//3============================================================

VOID
odm_AntennaDiversityInit(
	IN 		PDM_ODM_T		pDM_Odm 
)
{
	if(pDM_Odm->mp_mode == TRUE)
		return;

	if(pDM_Odm->SupportICType & (ODM_OLD_IC_ANTDIV_SUPPORT))
	{
		#if (RTL8192C_SUPPORT==1) 
		ODM_OldIC_AntDiv_Init(pDM_Odm);
		#endif
	}
	else
	{
		#if(defined(CONFIG_HW_ANTENNA_DIVERSITY))
		ODM_AntDiv_Config(pDM_Odm);
		ODM_AntDivInit(pDM_Odm);
		#endif
	}
}

VOID
odm_AntennaDiversity(
	IN 		PDM_ODM_T		pDM_Odm 
)
{
	if(pDM_Odm->mp_mode == TRUE)
		return;

	if(pDM_Odm->SupportICType & (ODM_OLD_IC_ANTDIV_SUPPORT))
	{
		#if (RTL8192C_SUPPORT==1) 
		ODM_OldIC_AntDiv(pDM_Odm);
		#endif
	}
	else
	{
		#if(defined(CONFIG_HW_ANTENNA_DIVERSITY))
		ODM_AntDiv(pDM_Odm);
		#endif
	}
}


void
odm_SwAntDetectInit(
	IN		PDM_ODM_T		pDM_Odm
	)
{
	pSWAT_T		pDM_SWAT_Table = &pDM_Odm->DM_SWAT_Table;
#if (RTL8723B_SUPPORT == 1)
	pDM_SWAT_Table->SWAS_NoLink_BK_Reg92c = ODM_Read4Byte(pDM_Odm, rDPDT_control);
#endif
	pDM_SWAT_Table->PreAntenna = MAIN_ANT;
	pDM_SWAT_Table->CurAntenna = MAIN_ANT;
	pDM_SWAT_Table->SWAS_NoLink_State = 0;
}


//============================================================
//EDCA Turbo
//============================================================

//Remove Edca by Yuchen


#if( DM_ODM_SUPPORT_TYPE == ODM_WIN) 
//
// 2011/07/26 MH Add an API for testing IQK fail case.
//
BOOLEAN
ODM_CheckPowerStatus(
	IN	PADAPTER		Adapter)
{

	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T			pDM_Odm = &pHalData->DM_OutSrc;
	RT_RF_POWER_STATE 	rtState;
	PMGNT_INFO			pMgntInfo	= &(Adapter->MgntInfo);

	// 2011/07/27 MH We are not testing ready~~!! We may fail to get correct value when init sequence.
	if (pMgntInfo->init_adpt_in_progress == TRUE)
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("ODM_CheckPowerStatus Return TRUE, due to initadapter"));
		return	TRUE;
	}
	
	//
	//	2011/07/19 MH We can not execute tx pwoer tracking/ LLC calibrate or IQK.
	//
	Adapter->HalFunc.GetHwRegHandler(Adapter, HW_VAR_RF_STATE, (pu1Byte)(&rtState));	
	if(Adapter->bDriverStopped || Adapter->bDriverIsGoingToPnpSetPowerSleep || rtState == eRfOff)
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("ODM_CheckPowerStatus Return FALSE, due to %d/%d/%d\n", 
		Adapter->bDriverStopped, Adapter->bDriverIsGoingToPnpSetPowerSleep, rtState));
		return	FALSE;
	}
	return	TRUE;
}
#endif

// need to ODM CE Platform
//move to here for ANT detection mechanism using

#if ((DM_ODM_SUPPORT_TYPE == ODM_WIN)||(DM_ODM_SUPPORT_TYPE == ODM_CE))
u4Byte
GetPSDData(
	IN PDM_ODM_T	pDM_Odm,
	unsigned int 	point,
	u1Byte initial_gain_psd)
{
	//unsigned int	val, rfval;
	//int	psd_report;
	u4Byte	psd_report;
	
	//HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	//Debug Message
	//val = PHY_QueryBBReg(Adapter,0x908, bMaskDWord);
	//DbgPrint("Reg908 = 0x%x\n",val);
	//val = PHY_QueryBBReg(Adapter,0xDF4, bMaskDWord);
	//rfval = PHY_QueryRFReg(Adapter, ODM_RF_PATH_A, 0x00, bRFRegOffsetMask);
	//DbgPrint("RegDF4 = 0x%x, RFReg00 = 0x%x\n",val, rfval);
	//DbgPrint("PHYTXON = %x, OFDMCCA_PP = %x, CCKCCA_PP = %x, RFReg00 = %x\n",
		//(val&BIT25)>>25, (val&BIT14)>>14, (val&BIT15)>>15, rfval);

	//Set DCO frequency index, offset=(40MHz/SamplePts)*point
	ODM_SetBBReg(pDM_Odm, 0x808, 0x3FF, point);

	//Start PSD calculation, Reg808[22]=0->1
	ODM_SetBBReg(pDM_Odm, 0x808, BIT22, 1);
	//Need to wait for HW PSD report
	ODM_StallExecution(1000);
	ODM_SetBBReg(pDM_Odm, 0x808, BIT22, 0);
	//Read PSD report, Reg8B4[15:0]
	psd_report = ODM_GetBBReg(pDM_Odm,0x8B4, bMaskDWord) & 0x0000FFFF;
	
#if 1//(DEV_BUS_TYPE == RT_PCI_INTERFACE) && ( (RT_PLATFORM == PLATFORM_LINUX) || (RT_PLATFORM == PLATFORM_MACOSX))
	psd_report = (u4Byte) (ConvertTo_dB(psd_report))+(u4Byte)(initial_gain_psd-0x1c);
#else
	psd_report = (int) (20*log10((double)psd_report))+(int)(initial_gain_psd-0x1c);
#endif

	return psd_report;
	
}

u4Byte 
ConvertTo_dB(
	u4Byte 	Value)
{
	u1Byte i;
	u1Byte j;
	u4Byte dB;

	Value = Value & 0xFFFF;
	
	for (i=0;i<8;i++)
	{
		if (Value <= dB_Invert_Table[i][11])
		{
			break;
		}
	}

	if (i >= 8)
	{
		return (96);	// maximum 96 dB
	}

	for (j=0;j<12;j++)
	{
		if (Value <= dB_Invert_Table[i][j])
		{
			break;
		}
	}

	dB = i*12 + j + 1;

	return (dB);
}

#endif


#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN| ODM_CE))

VOID
odm_PHY_SaveAFERegisters(
	IN	PDM_ODM_T	pDM_Odm,
	IN	pu4Byte		AFEReg,
	IN	pu4Byte		AFEBackup,
	IN	u4Byte		RegisterNum
	)
{
	u4Byte	i;
	
	//RT_DISP(FINIT, INIT_IQK, ("Save ADDA parameters.\n"));
	for( i = 0 ; i < RegisterNum ; i++){
		AFEBackup[i] = ODM_GetBBReg(pDM_Odm, AFEReg[i], bMaskDWord);
	}
}

VOID
odm_PHY_ReloadAFERegisters(
	IN	PDM_ODM_T	pDM_Odm,
	IN	pu4Byte		AFEReg,
	IN	pu4Byte		AFEBackup,
	IN	u4Byte		RegiesterNum
	)
{
	u4Byte	i;

	//RT_DISP(FINIT, INIT_IQK, ("Reload ADDA power saving parameters !\n"));
	for(i = 0 ; i < RegiesterNum; i++)
	{
	
		ODM_SetBBReg(pDM_Odm, AFEReg[i], bMaskDWord, AFEBackup[i]);
	}
}

//
// Description:
//	Set Single/Dual Antenna default setting for products that do not do detection in advance.
//
// Added by Joseph, 2012.03.22
//
VOID
ODM_SingleDualAntennaDefaultSetting(
	IN		PDM_ODM_T		pDM_Odm
	)
{
	pSWAT_T		pDM_SWAT_Table = &pDM_Odm->DM_SWAT_Table;
	PADAPTER	pAdapter	 =  pDM_Odm->Adapter;
	u1Byte btAntNum = 2;
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
	btAntNum=BT_GetPgAntNum(pAdapter);
#elif (DM_ODM_SUPPORT_TYPE & (ODM_CE))
#ifdef CONFIG_BT_COEXIST
	btAntNum = hal_btcoex_GetPgAntNum(pAdapter);
#endif
#endif

	// Set default antenna A and B status
	if(btAntNum == 2)
	{
		pDM_SWAT_Table->ANTA_ON=TRUE;
		pDM_SWAT_Table->ANTB_ON=TRUE;
		//RT_TRACE(COMP_ANTENNA, DBG_LOUD, ("Dual antenna\n"));
	}
#ifdef CONFIG_BT_COEXIST
	else if(btAntNum == 1)
	{// Set antenna A as default
		pDM_SWAT_Table->ANTA_ON=TRUE;
		pDM_SWAT_Table->ANTB_ON=FALSE;
		//RT_TRACE(COMP_ANTENNA, DBG_LOUD, ("Single antenna\n"));
	}
	else
	{
		//RT_ASSERT(FALSE, ("Incorrect antenna number!!\n"));
	}
#endif
}



//2 8723A ANT DETECT
//
// Description:
//	Implement IQK single tone for RF DPK loopback and BB PSD scanning. 
//	This function is cooperated with BB team Neil. 
//
// Added by Roger, 2011.12.15
//
BOOLEAN
ODM_SingleDualAntennaDetection(
	IN		PDM_ODM_T		pDM_Odm,
	IN		u1Byte			mode
	)
{
	PADAPTER	pAdapter	 =  pDM_Odm->Adapter;
	pSWAT_T		pDM_SWAT_Table = &pDM_Odm->DM_SWAT_Table;
	u4Byte		CurrentChannel,RfLoopReg;
	u1Byte		n;
	u4Byte		Reg88c, Regc08, Reg874, Regc50, Reg948=0, Regb2c=0, Reg92c=0, AFE_rRx_Wait_CCA=0;
	u1Byte		initial_gain = 0x5a;
	u4Byte		PSD_report_tmp;
	u4Byte		AntA_report = 0x0, AntB_report = 0x0,AntO_report=0x0;
	BOOLEAN		bResult = TRUE;
	u4Byte		AFE_Backup[16];
	u4Byte		AFE_REG_8723A[16] = {
					rRx_Wait_CCA, 	rTx_CCK_RFON, 
					rTx_CCK_BBON, 	rTx_OFDM_RFON,
					rTx_OFDM_BBON, 	rTx_To_Rx,
					rTx_To_Tx, 		rRx_CCK, 
					rRx_OFDM, 		rRx_Wait_RIFS, 
					rRx_TO_Rx,		rStandby,
					rSleep,			rPMPD_ANAEN, 	
					rFPGA0_XCD_SwitchControl, rBlue_Tooth};

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SingleDualAntennaDetection()============> \n"));	

	
	if(!(pDM_Odm->SupportICType & (ODM_RTL8723A|ODM_RTL8192C|ODM_RTL8723B)))
		return bResult;

	// Retrieve antenna detection registry info, added by Roger, 2012.11.27.
	if(!IS_ANT_DETECT_SUPPORT_SINGLE_TONE(pAdapter))
		return bResult;

	if(pDM_Odm->SupportICType == ODM_RTL8192C)
	{
		//Which path in ADC/DAC is turnned on for PSD: both I/Q
		ODM_SetBBReg(pDM_Odm, 0x808, BIT10|BIT11, 0x3);
		//Ageraged number: 8
		ODM_SetBBReg(pDM_Odm, 0x808, BIT12|BIT13, 0x1);
		//pts = 128;
		ODM_SetBBReg(pDM_Odm, 0x808, BIT14|BIT15, 0x0);
	}

	//1 Backup Current RF/BB Settings	
	
	CurrentChannel = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, ODM_CHANNEL, bRFRegOffsetMask);
	RfLoopReg = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x00, bRFRegOffsetMask);
	if(!(pDM_Odm->SupportICType == ODM_RTL8723B))
	ODM_SetBBReg(pDM_Odm, rFPGA0_XA_RFInterfaceOE, ODM_DPDT, Antenna_A);  // change to Antenna A
#if (RTL8723B_SUPPORT == 1)
	else
	{
		Reg92c = ODM_GetBBReg(pDM_Odm, 0x92c, bMaskDWord);
		Reg948 = ODM_GetBBReg(pDM_Odm, rS0S1_PathSwitch, bMaskDWord);
		Regb2c = ODM_GetBBReg(pDM_Odm, AGC_table_select, bMaskDWord);
		ODM_SetBBReg(pDM_Odm, rDPDT_control, 0x3, 0x1);
		ODM_SetBBReg(pDM_Odm, rfe_ctrl_anta_src, 0xff, 0x77);
		ODM_SetBBReg(pDM_Odm, rS0S1_PathSwitch, 0x3ff, 0x000);
		ODM_SetBBReg(pDM_Odm, AGC_table_select, BIT31, 0x0);
	}
#endif
	ODM_StallExecution(10);
	
	//Store A Path Register 88c, c08, 874, c50
	Reg88c = ODM_GetBBReg(pDM_Odm, rFPGA0_AnalogParameter4, bMaskDWord);
	Regc08 = ODM_GetBBReg(pDM_Odm, rOFDM0_TRMuxPar, bMaskDWord);
	Reg874 = ODM_GetBBReg(pDM_Odm, rFPGA0_XCD_RFInterfaceSW, bMaskDWord);
	Regc50 = ODM_GetBBReg(pDM_Odm, rOFDM0_XAAGCCore1, bMaskDWord);	
	
	// Store AFE Registers
	if(pDM_Odm->SupportICType & (ODM_RTL8723A|ODM_RTL8192C))
	odm_PHY_SaveAFERegisters(pDM_Odm, AFE_REG_8723A, AFE_Backup, 16);	
	else if(pDM_Odm->SupportICType == ODM_RTL8723B)
		AFE_rRx_Wait_CCA = ODM_GetBBReg(pDM_Odm, rRx_Wait_CCA,bMaskDWord);
	
	//Set PSD 128 pts
	ODM_SetBBReg(pDM_Odm, rFPGA0_PSDFunction, BIT14|BIT15, 0x0);  //128 pts
	
	// To SET CH1 to do
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, ODM_CHANNEL, bRFRegOffsetMask, 0x7401);     //Channel 1
	
	// AFE all on step
	if(pDM_Odm->SupportICType & (ODM_RTL8723A|ODM_RTL8192C))
	{
		ODM_SetBBReg(pDM_Odm, rRx_Wait_CCA, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rTx_CCK_RFON, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rTx_CCK_BBON, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rTx_OFDM_RFON, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rTx_OFDM_BBON, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rTx_To_Rx, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rTx_To_Tx, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rRx_CCK, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rRx_OFDM, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rRx_Wait_RIFS, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rRx_TO_Rx, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rStandby, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rSleep, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rPMPD_ANAEN, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rFPGA0_XCD_SwitchControl, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rBlue_Tooth, bMaskDWord, 0x6FDB25A4);
	}
	else if(pDM_Odm->SupportICType == ODM_RTL8723B)
	{
		ODM_SetBBReg(pDM_Odm, rRx_Wait_CCA, bMaskDWord, 0x01c00016);
	}

	// 3 wire Disable
	ODM_SetBBReg(pDM_Odm, rFPGA0_AnalogParameter4, bMaskDWord, 0xCCF000C0);
	
	//BB IQK Setting
	ODM_SetBBReg(pDM_Odm, rOFDM0_TRMuxPar, bMaskDWord, 0x000800E4);
	ODM_SetBBReg(pDM_Odm, rFPGA0_XCD_RFInterfaceSW, bMaskDWord, 0x22208000);

	//IQK setting tone@ 4.34Mhz
	ODM_SetBBReg(pDM_Odm, rTx_IQK_Tone_A, bMaskDWord, 0x10008C1C);
	ODM_SetBBReg(pDM_Odm, rTx_IQK, bMaskDWord, 0x01007c00);	

	//Page B init
	ODM_SetBBReg(pDM_Odm, rConfig_AntA, bMaskDWord, 0x00080000);
	ODM_SetBBReg(pDM_Odm, rConfig_AntA, bMaskDWord, 0x0f600000);
	ODM_SetBBReg(pDM_Odm, rRx_IQK, bMaskDWord, 0x01004800);
	ODM_SetBBReg(pDM_Odm, rRx_IQK_Tone_A, bMaskDWord, 0x10008c1f);
	ODM_SetBBReg(pDM_Odm, rTx_IQK_PI_A, bMaskDWord, 0x82150008);
	ODM_SetBBReg(pDM_Odm, rRx_IQK_PI_A, bMaskDWord, 0x28150008);
	ODM_SetBBReg(pDM_Odm, rIQK_AGC_Rsp, bMaskDWord, 0x001028d0);	

	//RF loop Setting
	if(pDM_Odm->SupportICType & (ODM_RTL8723A|ODM_RTL8192C))
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x0, 0xFFFFF, 0x50008);	
	
	//IQK Single tone start
	ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, bMaskH3Bytes, 0x808000);
	ODM_SetBBReg(pDM_Odm, rIQK_AGC_Pts, bMaskDWord, 0xf9000000);
	ODM_SetBBReg(pDM_Odm, rIQK_AGC_Pts, bMaskDWord, 0xf8000000);
	
	ODM_StallExecution(10000);

	// PSD report of antenna A
	PSD_report_tmp=0x0;
	for (n=0;n<2;n++)
 	{
 		PSD_report_tmp =  GetPSDData(pDM_Odm, 14, initial_gain);	
		if(PSD_report_tmp >AntA_report)
			AntA_report=PSD_report_tmp;
	}

	 // change to Antenna B
	if(pDM_Odm->SupportICType & (ODM_RTL8723A|ODM_RTL8192C))
		ODM_SetBBReg(pDM_Odm, rFPGA0_XA_RFInterfaceOE, ODM_DPDT, Antenna_B); 
#if (RTL8723B_SUPPORT == 1)
	else if(pDM_Odm->SupportICType == ODM_RTL8723B)
		ODM_SetBBReg(pDM_Odm, rDPDT_control, 0x3, 0x2);
#endif

	ODM_StallExecution(10);	

	// PSD report of antenna B
	PSD_report_tmp=0x0;
	for (n=0;n<2;n++)
 	{
 		PSD_report_tmp =  GetPSDData(pDM_Odm, 14, initial_gain);	
		if(PSD_report_tmp > AntB_report)
			AntB_report=PSD_report_tmp;
	}

	// change to open case
	if(pDM_Odm->SupportICType & (ODM_RTL8723A|ODM_RTL8192C))
		ODM_SetBBReg(pDM_Odm, rFPGA0_XA_RFInterfaceOE, ODM_DPDT, 0);  // change to Antenna A
#if (RTL8723B_SUPPORT == 1)
	else if(pDM_Odm->SupportICType == ODM_RTL8723B)
		ODM_SetBBReg(pDM_Odm, rDPDT_control, 0x3, 0x0);
#endif

	ODM_StallExecution(10);	
	
	// PSD report of open case
	PSD_report_tmp=0x0;
	for (n=0;n<2;n++)
 	{
 		PSD_report_tmp =  GetPSDData(pDM_Odm, 14, initial_gain);	
		if(PSD_report_tmp > AntO_report)
			AntO_report=PSD_report_tmp;
	}

	//Close IQK Single Tone function
	ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, bMaskH3Bytes, 0x000000);	

	//1 Return to antanna A
	if(pDM_Odm->SupportICType & (ODM_RTL8723A|ODM_RTL8192C))
		ODM_SetBBReg(pDM_Odm, rFPGA0_XA_RFInterfaceOE, ODM_DPDT, Antenna_A);  // change to Antenna A
#if (RTL8723B_SUPPORT == 1)
	else if(pDM_Odm->SupportICType == ODM_RTL8723B)
	{
		// external DPDT
		ODM_SetBBReg(pDM_Odm, rDPDT_control, bMaskDWord, Reg92c);

		//internal S0/S1
		ODM_SetBBReg(pDM_Odm, rS0S1_PathSwitch, bMaskDWord, Reg948);
		ODM_SetBBReg(pDM_Odm, AGC_table_select, bMaskDWord, Regb2c);
	}
#endif
	ODM_SetBBReg(pDM_Odm, rFPGA0_AnalogParameter4, bMaskDWord, Reg88c);
	ODM_SetBBReg(pDM_Odm, rOFDM0_TRMuxPar, bMaskDWord, Regc08);
	ODM_SetBBReg(pDM_Odm, rFPGA0_XCD_RFInterfaceSW, bMaskDWord, Reg874);
	ODM_SetBBReg(pDM_Odm, rOFDM0_XAAGCCore1, 0x7F, 0x40);
	ODM_SetBBReg(pDM_Odm, rOFDM0_XAAGCCore1, bMaskDWord, Regc50);
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask,CurrentChannel);
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x00, bRFRegOffsetMask,RfLoopReg);

	//Reload AFE Registers
	if(pDM_Odm->SupportICType & (ODM_RTL8723A|ODM_RTL8192C))
	odm_PHY_ReloadAFERegisters(pDM_Odm, AFE_REG_8723A, AFE_Backup, 16);	
	else if(pDM_Odm->SupportICType == ODM_RTL8723B)
		ODM_SetBBReg(pDM_Odm, rRx_Wait_CCA, bMaskDWord, AFE_rRx_Wait_CCA);

	if(pDM_Odm->SupportICType == ODM_RTL8723A)
	{
		//2 Test Ant B based on Ant A is ON
		if(mode==ANTTESTB)
		{
			if(AntA_report >=	100)
			{
				if(AntB_report > (AntA_report+1))
				{
					pDM_SWAT_Table->ANTB_ON=FALSE;
							ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SingleDualAntennaDetection(): Single Antenna A\n"));		
				}	
				else
				{
					pDM_SWAT_Table->ANTB_ON=TRUE;
							ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SingleDualAntennaDetection(): Dual Antenna is A and B\n"));	
				}	
			}
			else
			{
							ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SingleDualAntennaDetection(): Need to check again\n"));
				pDM_SWAT_Table->ANTB_ON=FALSE; // Set Antenna B off as default 
				bResult = FALSE;
			}
		}	
		//2 Test Ant A and B based on DPDT Open
		else if(mode==ANTTESTALL)
		{
			if((AntO_report >=100) && (AntO_report <=118))
			{
				if(AntA_report > (AntO_report+1))
				{
					pDM_SWAT_Table->ANTA_ON=FALSE;
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("Ant A is OFF\n"));
				}	
				else
				{
					pDM_SWAT_Table->ANTA_ON=TRUE;
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("Ant A is ON\n"));
				}

				if(AntB_report > (AntO_report+2))
				{
					pDM_SWAT_Table->ANTB_ON=FALSE;
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("Ant B is OFF\n"));
				}	
				else
				{
					pDM_SWAT_Table->ANTB_ON=TRUE;
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("Ant B is ON\n"));
				}
				
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("psd_report_A[%d]= %d \n", 2416, AntA_report));	
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("psd_report_B[%d]= %d \n", 2416, AntB_report));	
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("psd_report_O[%d]= %d \n", 2416, AntO_report));
				
				pDM_Odm->AntDetectedInfo.bAntDetected= TRUE;
				pDM_Odm->AntDetectedInfo.dBForAntA = AntA_report;
				pDM_Odm->AntDetectedInfo.dBForAntB = AntB_report;
				pDM_Odm->AntDetectedInfo.dBForAntO = AntO_report;
				
				}
			else
				{
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("return FALSE!!\n"));
				bResult = FALSE;
			}
		}
	}
	else if(pDM_Odm->SupportICType == ODM_RTL8192C)
	{
		if(AntA_report >=	100)
		{
			if(AntB_report > (AntA_report+2))
			{
				pDM_SWAT_Table->ANTA_ON=FALSE;
				pDM_SWAT_Table->ANTB_ON=TRUE;
				ODM_SetBBReg(pDM_Odm,  rFPGA0_XA_RFInterfaceOE, 0x300, Antenna_B);
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SingleDualAntennaDetection(): Single Antenna B\n"));		
			}	
			else if(AntA_report > (AntB_report+2))
			{
				pDM_SWAT_Table->ANTA_ON=TRUE;
				pDM_SWAT_Table->ANTB_ON=FALSE;
				ODM_SetBBReg(pDM_Odm,  rFPGA0_XA_RFInterfaceOE, 0x300, Antenna_A);
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SingleDualAntennaDetection(): Single Antenna A\n"));
			}	
			else
			{
				pDM_SWAT_Table->ANTA_ON=TRUE;
				pDM_SWAT_Table->ANTB_ON=TRUE;
				RT_TRACE(COMP_ANTENNA, DBG_LOUD, ("ODM_SingleDualAntennaDetection(): Dual Antenna \n"));
			}
		}
		else
		{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SingleDualAntennaDetection(): Need to check again\n"));
			pDM_SWAT_Table->ANTA_ON=TRUE; // Set Antenna A on as default 
			pDM_SWAT_Table->ANTB_ON=FALSE; // Set Antenna B off as default 
			bResult = FALSE;
		}
	}
	else if(pDM_Odm->SupportICType == ODM_RTL8723B)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("psd_report_A[%d]= %d \n", 2416, AntA_report));	
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("psd_report_B[%d]= %d \n", 2416, AntB_report));	
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("psd_report_O[%d]= %d \n", 2416, AntO_report));
		
		//2 Test Ant B based on Ant A is ON
		if(mode==ANTTESTB)
		{
			if(AntA_report >=100 && AntA_report <= 116)
			{
				if(AntB_report >= (AntA_report+4) && AntB_report > 116)
				{
					pDM_SWAT_Table->ANTB_ON=FALSE;
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SingleDualAntennaDetection(): Single Antenna A\n"));		
				}	
				else if(AntB_report >=100 && AntB_report <= 116)
				{
					pDM_SWAT_Table->ANTB_ON=TRUE;
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SingleDualAntennaDetection(): Dual Antenna is A and B\n"));	
				}
				else
				{
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SingleDualAntennaDetection(): Need to check again\n"));
					pDM_SWAT_Table->ANTB_ON=FALSE; // Set Antenna B off as default 
					bResult = FALSE;
				}
			}
			else
			{
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SingleDualAntennaDetection(): Need to check again\n"));
				pDM_SWAT_Table->ANTB_ON=FALSE; // Set Antenna B off as default 
				bResult = FALSE;
			}
		}	
		//2 Test Ant A and B based on DPDT Open
		else if(mode==ANTTESTALL)
		{
			if((AntA_report >= 100) && (AntB_report >= 100) && (AntA_report <= 120) && (AntB_report <= 120))
			{
				if((AntA_report - AntB_report < 2) || (AntB_report - AntA_report < 2))
				{
					pDM_SWAT_Table->ANTA_ON=TRUE;
					pDM_SWAT_Table->ANTB_ON=TRUE;
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("ODM_SingleDualAntennaDetection(): Dual Antenna\n"));
				}
				else if(((AntA_report - AntB_report >= 2) && (AntA_report - AntB_report <= 4)) || 
					((AntB_report - AntA_report >= 2) && (AntB_report - AntA_report <= 4)))
				{
					pDM_SWAT_Table->ANTA_ON=FALSE;
					pDM_SWAT_Table->ANTB_ON=FALSE;
					bResult = FALSE;
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SingleDualAntennaDetection(): Need to check again\n"));
				}
				else
				{
					pDM_SWAT_Table->ANTA_ON = TRUE;
					pDM_SWAT_Table->ANTB_ON=FALSE;
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("ODM_SingleDualAntennaDetection(): Single Antenna A\n"));
				}
				
				pDM_Odm->AntDetectedInfo.bAntDetected= TRUE;
				pDM_Odm->AntDetectedInfo.dBForAntA = AntA_report;
				pDM_Odm->AntDetectedInfo.dBForAntB = AntB_report;
				pDM_Odm->AntDetectedInfo.dBForAntO = AntO_report;
				
			}
			else
			{
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("return FALSE!!\n"));
				bResult = FALSE;
			}
		}
	}
		
	return bResult;

}


#endif   // end odm_CE

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN| ODM_CE))

VOID
odm_Set_RA_DM_ARFB_by_Noisy(
	IN	PDM_ODM_T	pDM_Odm
)
{
	//DbgPrint("DM_ARFB ====> \n");
	if (pDM_Odm->bNoisyState){
		ODM_Write4Byte(pDM_Odm,0x430,0x00000000);
		ODM_Write4Byte(pDM_Odm,0x434,0x05040200);
		//DbgPrint("DM_ARFB ====> Noisy State\n");
	}
	else{
		ODM_Write4Byte(pDM_Odm,0x430,0x02010000);
		ODM_Write4Byte(pDM_Odm,0x434,0x07050403);
		//DbgPrint("DM_ARFB ====> Clean State\n");
	}
	
}

VOID
ODM_UpdateNoisyState(
	IN	PDM_ODM_T	pDM_Odm,
	IN 	BOOLEAN 	bNoisyStateFromC2H
	)
{
	//DbgPrint("Get C2H Command! NoisyState=0x%x\n ", bNoisyStateFromC2H);
	if(pDM_Odm->SupportICType == ODM_RTL8821  || pDM_Odm->SupportICType == ODM_RTL8812  || 
	   pDM_Odm->SupportICType == ODM_RTL8723B || pDM_Odm->SupportICType == ODM_RTL8192E || pDM_Odm->SupportICType == ODM_RTL8188E)
	{
		pDM_Odm->bNoisyState = bNoisyStateFromC2H;
	}
	odm_Set_RA_DM_ARFB_by_Noisy(pDM_Odm);
};

u4Byte
Set_RA_DM_Ratrbitmap_by_Noisy(
	IN	PDM_ODM_T	pDM_Odm,
	IN	WIRELESS_MODE	WirelessMode,
	IN	u4Byte			ratr_bitmap,
	IN	u1Byte			rssi_level
)
{
	u4Byte ret_bitmap = ratr_bitmap;
	switch (WirelessMode)
	{
		case WIRELESS_MODE_AC_24G :
		case WIRELESS_MODE_AC_5G :
		case WIRELESS_MODE_AC_ONLY:
			if (pDM_Odm->bNoisyState){ // in Noisy State
				if (rssi_level==1)
					ret_bitmap&=0xfe3f0e08;
				else if (rssi_level==2)
					ret_bitmap&=0xff3f8f8c;
				else if (rssi_level==3)
					ret_bitmap&=0xffffffcc ;
				else
					ret_bitmap&=0xffffffff ;
			}
			else{                                   // in SNR State
				if (rssi_level==1){
					ret_bitmap&=0xfc3e0c08;
				}
				else if (rssi_level==2){
					ret_bitmap&=0xfe3f0e08;
				}
				else if (rssi_level==3){
					ret_bitmap&=0xffbfefcc;
				}
				else{
					ret_bitmap&=0x0fffffff;
				}
			}
			break;
		case WIRELESS_MODE_B:
		case WIRELESS_MODE_A:
		case WIRELESS_MODE_G:
		case WIRELESS_MODE_N_24G:
		case WIRELESS_MODE_N_5G:
			if (pDM_Odm->bNoisyState){
				if (rssi_level==1)
					ret_bitmap&=0x0f0e0c08;
				else if (rssi_level==2)
					ret_bitmap&=0x0f8f0e0c;
				else if (rssi_level==3)
					ret_bitmap&=0x0fefefcc ;
				else
					ret_bitmap&=0xffffffff ;
			}
			else{
				if (rssi_level==1){
					ret_bitmap&=0x0f8f0e08;
				}
				else if (rssi_level==2){
					ret_bitmap&=0x0fcf8f8c;
				}
				else if (rssi_level==3){
					ret_bitmap&=0x0fffffcc;
				}
				else{
					ret_bitmap&=0x0fffffff;
				}
			}
			break;
		default:
			break;
	}
	//DbgPrint("DM_RAMask ====> rssi_LV = %d, BITMAP = %x \n", rssi_level, ret_bitmap);
	return ret_bitmap;

}



VOID
ODM_UpdateInitRate(
	IN	PDM_ODM_T	pDM_Odm,
	IN	u1Byte		Rate
	)
{
	u1Byte			p = 0;

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("Get C2H Command! Rate=0x%x\n", Rate));
	
	if(pDM_Odm->SupportICType == ODM_RTL8821  || pDM_Odm->SupportICType == ODM_RTL8812  || 
	   pDM_Odm->SupportICType == ODM_RTL8723B || pDM_Odm->SupportICType == ODM_RTL8192E || pDM_Odm->SupportICType == ODM_RTL8188E)
	{
		pDM_Odm->TxRate = Rate;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	#if DEV_BUS_TYPE==RT_PCI_INTERFACE
		#if USE_WORKITEM
		PlatformScheduleWorkItem(&pDM_Odm->RaRptWorkitem);
		#else
		if(pDM_Odm->SupportICType == ODM_RTL8821)
		{
			ODM_TxPwrTrackSetPwr8821A(pDM_Odm, MIX_MODE, ODM_RF_PATH_A, 0);
		}
		else if(pDM_Odm->SupportICType == ODM_RTL8812)
		{
			for (p = ODM_RF_PATH_A; p < MAX_PATH_NUM_8812A; p++) 		
			{
				ODM_TxPwrTrackSetPwr8812A(pDM_Odm, MIX_MODE, p, 0);
			}
		}
		else if(pDM_Odm->SupportICType == ODM_RTL8723B)
		{
			ODM_TxPwrTrackSetPwr_8723B(pDM_Odm, MIX_MODE, ODM_RF_PATH_A, 0);
		}
		else if(pDM_Odm->SupportICType == ODM_RTL8192E)
		{
			for (p = ODM_RF_PATH_A; p < MAX_PATH_NUM_8192E; p++) 		
			{
				ODM_TxPwrTrackSetPwr92E(pDM_Odm, MIX_MODE, p, 0);
			}
		}
		else if(pDM_Odm->SupportICType == ODM_RTL8188E)
		{
			ODM_TxPwrTrackSetPwr88E(pDM_Odm, MIX_MODE, ODM_RF_PATH_A, 0);
		}
		#endif
	#else
		PlatformScheduleWorkItem(&pDM_Odm->RaRptWorkitem);
	#endif	
#endif
	}
	else
		return;
}

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
VOID
ODM_UpdateInitRateWorkItemCallback(
    IN PVOID            pContext
    )
{
	PADAPTER	Adapter = (PADAPTER)pContext;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;

	u1Byte			p = 0;	

	if(pDM_Odm->SupportICType == ODM_RTL8821)
	{
		ODM_TxPwrTrackSetPwr8821A(pDM_Odm, MIX_MODE, ODM_RF_PATH_A, 0);
	}
	else if(pDM_Odm->SupportICType == ODM_RTL8812)
	{
		for (p = ODM_RF_PATH_A; p < MAX_PATH_NUM_8812A; p++)    //DOn't know how to include &c
		{
			ODM_TxPwrTrackSetPwr8812A(pDM_Odm, MIX_MODE, p, 0);
		}
	}
	else if(pDM_Odm->SupportICType == ODM_RTL8723B)
	{
			ODM_TxPwrTrackSetPwr_8723B(pDM_Odm, MIX_MODE, ODM_RF_PATH_A, 0);
	}
	else if(pDM_Odm->SupportICType == ODM_RTL8192E)
	{
		for (p = ODM_RF_PATH_A; p < MAX_PATH_NUM_8192E; p++)    //DOn't know how to include &c
		{
			ODM_TxPwrTrackSetPwr92E(pDM_Odm, MIX_MODE, p, 0);
		}
	}
	else if(pDM_Odm->SupportICType == ODM_RTL8188E)
	{
			ODM_TxPwrTrackSetPwr88E(pDM_Odm, MIX_MODE, ODM_RF_PATH_A, 0);
	}
}
#endif
#endif

//
// ODM multi-port consideration, added by Roger, 2013.10.01.
//
VOID
ODM_AsocEntry_Init(
	IN	PDM_ODM_T	pDM_Odm
	)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PADAPTER pLoopAdapter = GetDefaultAdapter(pDM_Odm->Adapter);
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pLoopAdapter);
	PDM_ODM_T		 pDM_OutSrc = &pHalData->DM_OutSrc;
	u1Byte	TotalAssocEntryNum = 0;
	u1Byte	index = 0;


	ODM_CmnInfoPtrArrayHook(pDM_OutSrc, ODM_CMNINFO_STA_STATUS, 0, &pLoopAdapter->MgntInfo.DefaultPort[0]);
	pLoopAdapter->MgntInfo.DefaultPort[0].MultiPortStationIdx = TotalAssocEntryNum;
		
	pLoopAdapter = GetNextExtAdapter(pLoopAdapter);
	TotalAssocEntryNum +=1;

	while(pLoopAdapter)
	{
		for (index = 0; index <ASSOCIATE_ENTRY_NUM; index++)
		{
			ODM_CmnInfoPtrArrayHook(pDM_OutSrc, ODM_CMNINFO_STA_STATUS, TotalAssocEntryNum+index, &pLoopAdapter->MgntInfo.AsocEntry[index]);
			pLoopAdapter->MgntInfo.AsocEntry[index].MultiPortStationIdx = TotalAssocEntryNum+index;				
		}
		
		TotalAssocEntryNum+= index;
		if(IS_HARDWARE_TYPE_8188E((pDM_Odm->Adapter)))
			pLoopAdapter->RASupport = TRUE;
		pLoopAdapter = GetNextExtAdapter(pLoopAdapter);
	}
#endif
}

#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
/* Justin: According to the current RRSI to adjust Response Frame TX power, 2012/11/05 */
void odm_dtc(PDM_ODM_T pDM_Odm)
{
#ifdef CONFIG_DM_RESP_TXAGC
	#define DTC_BASE            35	/* RSSI higher than this value, start to decade TX power */
	#define DTC_DWN_BASE       (DTC_BASE-5)	/* RSSI lower than this value, start to increase TX power */

	/* RSSI vs TX power step mapping: decade TX power */
	static const u8 dtc_table_down[]={
		DTC_BASE,
		(DTC_BASE+5),
		(DTC_BASE+10),
		(DTC_BASE+15),
		(DTC_BASE+20),
		(DTC_BASE+25)
	};

	/* RSSI vs TX power step mapping: increase TX power */
	static const u8 dtc_table_up[]={
		DTC_DWN_BASE,
		(DTC_DWN_BASE-5),
		(DTC_DWN_BASE-10),
		(DTC_DWN_BASE-15),
		(DTC_DWN_BASE-15),
		(DTC_DWN_BASE-20),
		(DTC_DWN_BASE-20),
		(DTC_DWN_BASE-25),
		(DTC_DWN_BASE-25),
		(DTC_DWN_BASE-30),
		(DTC_DWN_BASE-35)
	};

	u8 i;
	u8 dtc_steps=0;
	u8 sign;
	u8 resp_txagc=0;

	#if 0
	/* As DIG is disabled, DTC is also disable */
	if(!(pDM_Odm->SupportAbility & ODM_XXXXXX))
		return;
	#endif

	if (DTC_BASE < pDM_Odm->RSSI_Min) {
		/* need to decade the CTS TX power */
		sign = 1;
		for (i=0;i<ARRAY_SIZE(dtc_table_down);i++)
		{
			if ((dtc_table_down[i] >= pDM_Odm->RSSI_Min) || (dtc_steps >= 6))
				break;
			else
				dtc_steps++;
		}
	}
#if 0
	else if (DTC_DWN_BASE > pDM_Odm->RSSI_Min)
	{
		/* needs to increase the CTS TX power */
		sign = 0;
		dtc_steps = 1;
		for (i=0;i<ARRAY_SIZE(dtc_table_up);i++)
		{
			if ((dtc_table_up[i] <= pDM_Odm->RSSI_Min) || (dtc_steps>=10))
				break;
			else
				dtc_steps++;
		}
	}
#endif
	else
	{
		sign = 0;
		dtc_steps = 0;
	}

	resp_txagc = dtc_steps | (sign << 4);
	resp_txagc = resp_txagc | (resp_txagc << 5);
	ODM_Write1Byte(pDM_Odm, 0x06d9, resp_txagc);

	DBG_871X("%s RSSI_Min:%u, set RESP_TXAGC to %s %u\n", 
		__func__, pDM_Odm->RSSI_Min, sign?"minus":"plus", dtc_steps);
#endif /* CONFIG_RESP_TXAGC_ADJUST */
}

#endif /* #if (DM_ODM_SUPPORT_TYPE == ODM_CE) */

VOID
odm_UpdatePowerTrainingState(
	IN	PDM_ODM_T	pDM_Odm
	)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
	PFALSE_ALARM_STATISTICS 	FalseAlmCnt = (PFALSE_ALARM_STATISTICS)PhyDM_Get_Structure( pDM_Odm , PhyDM_FalseAlmCnt);
	pDIG_T						pDM_DigTable = &pDM_Odm->DM_DigTable;
	u4Byte						score = 0;

	if(!(pDM_Odm->SupportAbility & ODM_BB_PWR_TRAIN))
		return;

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_RA_MASK, ODM_DBG_LOUD,("odm_UpdatePowerTrainingState()============>\n"));
	pDM_Odm->bChangeState = FALSE;

	// Debug command
	if(pDM_Odm->ForcePowerTrainingState)
	{
		if(pDM_Odm->ForcePowerTrainingState == 1 && !pDM_Odm->bDisablePowerTraining)
		{
			pDM_Odm->bChangeState = TRUE;
			pDM_Odm->bDisablePowerTraining = TRUE;
		}
		else if(pDM_Odm->ForcePowerTrainingState == 2 && pDM_Odm->bDisablePowerTraining)
		{
			pDM_Odm->bChangeState = TRUE;
			pDM_Odm->bDisablePowerTraining = FALSE;
		}

		pDM_Odm->PT_score = 0;
		pDM_Odm->PhyDbgInfo.NumQryPhyStatusOFDM = 0;
		pDM_Odm->PhyDbgInfo.NumQryPhyStatusCCK = 0;
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_RA_MASK, ODM_DBG_LOUD,("odm_UpdatePowerTrainingState(): ForcePowerTrainingState = %d\n", 
			pDM_Odm->ForcePowerTrainingState));
		return;
	}
	
	if(!pDM_Odm->bLinked)
		return;
	
	// First connect
	if((pDM_Odm->bLinked) && (pDM_DigTable->bMediaConnect_0 == FALSE))
	{
		pDM_Odm->PT_score = 0;
		pDM_Odm->bChangeState = TRUE;
		pDM_Odm->PhyDbgInfo.NumQryPhyStatusOFDM = 0;
		pDM_Odm->PhyDbgInfo.NumQryPhyStatusCCK = 0;
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_RA_MASK, ODM_DBG_LOUD,("odm_UpdatePowerTrainingState(): First Connect\n"));
		return;
	}

	// Compute score
	if(pDM_Odm->NHM_cnt_0 >= 215)
		score = 2;
	else if(pDM_Odm->NHM_cnt_0 >= 190) 
		score = 1;							// unknow state
	else
	{
		u4Byte	RX_Pkt_Cnt;
		
		RX_Pkt_Cnt = (u4Byte)(pDM_Odm->PhyDbgInfo.NumQryPhyStatusOFDM) + (u4Byte)(pDM_Odm->PhyDbgInfo.NumQryPhyStatusCCK);
		
		if((FalseAlmCnt->Cnt_CCA_all > 31 && RX_Pkt_Cnt > 31) && (FalseAlmCnt->Cnt_CCA_all >= RX_Pkt_Cnt))
		{
			if((RX_Pkt_Cnt + (RX_Pkt_Cnt >> 1)) <= FalseAlmCnt->Cnt_CCA_all)
				score = 0;
			else if((RX_Pkt_Cnt + (RX_Pkt_Cnt >> 2)) <= FalseAlmCnt->Cnt_CCA_all)
				score = 1;
			else
				score = 2;
		}
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_RA_MASK, ODM_DBG_LOUD,("odm_UpdatePowerTrainingState(): RX_Pkt_Cnt = %d, Cnt_CCA_all = %d\n", 
			RX_Pkt_Cnt, FalseAlmCnt->Cnt_CCA_all));
	}
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_RA_MASK, ODM_DBG_LOUD,("odm_UpdatePowerTrainingState(): NumQryPhyStatusOFDM = %d, NumQryPhyStatusCCK = %d\n",
			(u4Byte)(pDM_Odm->PhyDbgInfo.NumQryPhyStatusOFDM), (u4Byte)(pDM_Odm->PhyDbgInfo.NumQryPhyStatusCCK)));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_RA_MASK, ODM_DBG_LOUD,("odm_UpdatePowerTrainingState(): NHM_cnt_0 = %d, score = %d\n", 
		pDM_Odm->NHM_cnt_0, score));

	// smoothing
	pDM_Odm->PT_score = (score << 4) + (pDM_Odm->PT_score>>1) + (pDM_Odm->PT_score>>2);
	score = (pDM_Odm->PT_score + 32) >> 6;
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_RA_MASK, ODM_DBG_LOUD,("odm_UpdatePowerTrainingState(): PT_score = %d, score after smoothing = %d\n", 
		pDM_Odm->PT_score, score));

	// Mode decision
	if(score == 2)
	{
		if(pDM_Odm->bDisablePowerTraining)
		{
			pDM_Odm->bChangeState = TRUE;
			pDM_Odm->bDisablePowerTraining = FALSE;
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_RA_MASK, ODM_DBG_LOUD,("odm_UpdatePowerTrainingState(): Change state\n"));
		}
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_RA_MASK, ODM_DBG_LOUD,("odm_UpdatePowerTrainingState(): Enable Power Training\n"));
	}
	else if(score == 0)
	{
		if(!pDM_Odm->bDisablePowerTraining)
		{
			pDM_Odm->bChangeState = TRUE;
			pDM_Odm->bDisablePowerTraining = TRUE;
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_RA_MASK, ODM_DBG_LOUD,("odm_UpdatePowerTrainingState(): Change state\n"));
		}
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_RA_MASK, ODM_DBG_LOUD,("odm_UpdatePowerTrainingState(): Disable Power Training\n"));
	}

	pDM_Odm->PhyDbgInfo.NumQryPhyStatusOFDM = 0;
	pDM_Odm->PhyDbgInfo.NumQryPhyStatusCCK = 0;
#endif
}

