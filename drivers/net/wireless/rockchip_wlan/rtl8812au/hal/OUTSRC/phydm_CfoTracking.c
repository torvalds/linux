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
#include "Mp_Precomp.h"
#include "phydm_precomp.h"

VOID
odm_SetCrystalCap(
	IN		PVOID					pDM_VOID,
	IN		u1Byte					CrystalCap
)
{
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PCFO_TRACKING				pCfoTrack = (PCFO_TRACKING)PhyDM_Get_Structure( pDM_Odm, PhyDM_CfoTrack);
	BOOLEAN 					bEEPROMCheck;
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
	PADAPTER					Adapter = pDM_Odm->Adapter;
	HAL_DATA_TYPE				*pHalData = GET_HAL_DATA(Adapter);

	bEEPROMCheck = (pHalData->EEPROMVersion >= 0x01)?TRUE:FALSE;
#else
	bEEPROMCheck = TRUE;
#endif

	if(pCfoTrack->CrystalCap == CrystalCap)
		return;

	pCfoTrack->CrystalCap = CrystalCap;

	if(pDM_Odm->SupportICType & ODM_RTL8192D)
	{
		ODM_SetBBReg(pDM_Odm, REG_AFE_XTAL_CTRL, 0x000000F0, CrystalCap & 0x0F);
		ODM_SetBBReg(pDM_Odm, REG_AFE_PLL_CTRL, 0xF0000000, ((CrystalCap & 0xF0) >> 4));
	}
	else if(pDM_Odm->SupportICType & ODM_RTL8188E)
	{
		// write 0x24[22:17] = 0x24[16:11] = CrystalCap
		CrystalCap = CrystalCap & 0x3F;
		ODM_SetBBReg(pDM_Odm, REG_AFE_XTAL_CTRL, 0x007ff800, (CrystalCap | (CrystalCap << 6)));
	}
	else if(pDM_Odm->SupportICType & ODM_RTL8812)
	{
		// write 0x2C[30:25] = 0x2C[24:19] = CrystalCap
		CrystalCap = CrystalCap & 0x3F;
		ODM_SetBBReg(pDM_Odm, REG_MAC_PHY_CTRL, 0x7FF80000, (CrystalCap | (CrystalCap << 6)));
	}	
	else if (((pDM_Odm->SupportICType & ODM_RTL8723A) && bEEPROMCheck) ||
		(pDM_Odm->SupportICType & ODM_RTL8723B) ||(pDM_Odm->SupportICType & ODM_RTL8192E) ||
		(pDM_Odm->SupportICType & ODM_RTL8821))
	{
		// 0x2C[23:18] = 0x2C[17:12] = CrystalCap
		CrystalCap = CrystalCap & 0x3F;
		ODM_SetBBReg(pDM_Odm, REG_MAC_PHY_CTRL, 0x00FFF000, (CrystalCap | (CrystalCap << 6)));	
	}
	else if(pDM_Odm->SupportICType & ODM_RTL8821B)
	{
		// write 0x28[6:1] = 0x24[30:25] = CrystalCap
		CrystalCap = CrystalCap & 0x3F;
		ODM_SetBBReg(pDM_Odm, REG_AFE_XTAL_CTRL, 0x7E000000, CrystalCap);
		ODM_SetBBReg(pDM_Odm, REG_AFE_PLL_CTRL, 0x7E, CrystalCap);	
	}
	else if(pDM_Odm->SupportICType & ODM_RTL8814A)
	{
		// write 0x2C[26:21] = 0x2C[20:15] = CrystalCap
		CrystalCap = CrystalCap & 0x3F;
		ODM_SetBBReg(pDM_Odm, REG_MAC_PHY_CTRL, 0x07FF8000, (CrystalCap | (CrystalCap << 6)));
	}
	else 
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CFO_TRACKING, ODM_DBG_LOUD, ("odm_SetCrystalCap(): Use default setting.\n"));
		ODM_SetBBReg(pDM_Odm, REG_MAC_PHY_CTRL, 0xFFF000, (CrystalCap | (CrystalCap << 6)));
	}

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CFO_TRACKING, ODM_DBG_LOUD, ("odm_SetCrystalCap(): CrystalCap = 0x%x\n", CrystalCap));
}

u1Byte
odm_GetDefaultCrytaltalCap(
	IN		PVOID					pDM_VOID
)
{
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte						CrystalCap = 0x20;

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
	PADAPTER					Adapter = pDM_Odm->Adapter;
	HAL_DATA_TYPE				*pHalData = GET_HAL_DATA(Adapter);

	CrystalCap = pHalData->CrystalCap;
#else
	prtl8192cd_priv	priv		= pDM_Odm->priv;

	if(priv->pmib->dot11RFEntry.xcap > 0)
		CrystalCap = priv->pmib->dot11RFEntry.xcap;
#endif

	CrystalCap = CrystalCap & 0x3f;

	return CrystalCap;
}

VOID
odm_SetATCStatus(
	IN		PVOID					pDM_VOID,
	IN		BOOLEAN					ATCStatus
)
{
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PCFO_TRACKING				pCfoTrack = (PCFO_TRACKING)PhyDM_Get_Structure( pDM_Odm, PhyDM_CfoTrack);

	if(pCfoTrack->bATCStatus == ATCStatus)
		return;
	
	ODM_SetBBReg(pDM_Odm, ODM_REG(BB_ATC,pDM_Odm), ODM_BIT(BB_ATC,pDM_Odm), ATCStatus);
	pCfoTrack->bATCStatus = ATCStatus;
}

BOOLEAN
odm_GetATCStatus(
	IN		PVOID					pDM_VOID
)
{
	BOOLEAN						ATCStatus;
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;

	ATCStatus = (BOOLEAN)ODM_GetBBReg(pDM_Odm, ODM_REG(BB_ATC,pDM_Odm), ODM_BIT(BB_ATC,pDM_Odm));
	return ATCStatus;
}

VOID
ODM_CfoTrackingReset(
	IN		PVOID					pDM_VOID
)
{
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PCFO_TRACKING				pCfoTrack = (PCFO_TRACKING)PhyDM_Get_Structure( pDM_Odm, PhyDM_CfoTrack);
	u1Byte						CrystalCap;

	pCfoTrack->DefXCap = odm_GetDefaultCrytaltalCap(pDM_Odm);
	pCfoTrack->bAdjust = TRUE;
	
#if(DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
	odm_SetCrystalCap(pDM_Odm, pCfoTrack->DefXCap);
	odm_SetATCStatus(pDM_Odm, TRUE);
#else
	if(pCfoTrack->CrystalCap > pCfoTrack->DefXCap)
	{
		for(CrystalCap = pCfoTrack->CrystalCap; CrystalCap >= pCfoTrack->DefXCap; CrystalCap--)
			odm_SetCrystalCap(pDM_Odm, CrystalCap);
	}
	else
	{
		for(CrystalCap = pCfoTrack->CrystalCap; CrystalCap <= pCfoTrack->DefXCap; CrystalCap++)
			odm_SetCrystalCap(pDM_Odm, CrystalCap);
	}
#endif
}

VOID
ODM_CfoTrackingInit(
	IN		PVOID					pDM_VOID
)
{
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PCFO_TRACKING				pCfoTrack = (PCFO_TRACKING)PhyDM_Get_Structure( pDM_Odm, PhyDM_CfoTrack);
      
	pCfoTrack->DefXCap = pCfoTrack->CrystalCap = odm_GetDefaultCrytaltalCap(pDM_Odm);
	pCfoTrack->bATCStatus = odm_GetATCStatus(pDM_Odm);
	pCfoTrack->bAdjust = TRUE;
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CFO_TRACKING, ODM_DBG_LOUD, ("ODM_CfoTracking_init()=========> \n"));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CFO_TRACKING, ODM_DBG_LOUD, ("ODM_CfoTracking_init(): bATCStatus = %d, CrystalCap = 0x%x \n",pCfoTrack->bATCStatus, pCfoTrack->DefXCap));
}

VOID
ODM_CfoTracking(
	IN		PVOID					pDM_VOID
)
{
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PCFO_TRACKING				pCfoTrack = (PCFO_TRACKING)PhyDM_Get_Structure( pDM_Odm, PhyDM_CfoTrack);
	int							CFO_kHz_A, CFO_kHz_B, CFO_ave = 0;
	int							CFO_ave_diff;
	int							CrystalCap = (int)pCfoTrack->CrystalCap;
	u1Byte						Adjust_Xtal = 1;

	//4 Support ability
	if(!(pDM_Odm->SupportAbility & ODM_BB_CFO_TRACKING))
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CFO_TRACKING, ODM_DBG_LOUD, ("ODM_CfoTracking(): Return: SupportAbility ODM_BB_CFO_TRACKING is disabled\n"));
		return;
	}

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CFO_TRACKING, ODM_DBG_LOUD, ("ODM_CfoTracking()=========> \n"));

	if(!pDM_Odm->bLinked || !pDM_Odm->bOneEntryOnly)
	{	
		//4 No link or more than one entry
		ODM_CfoTrackingReset(pDM_Odm);
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CFO_TRACKING, ODM_DBG_LOUD, ("ODM_CfoTracking(): Reset: bLinked = %d, bOneEntryOnly = %d\n", 
			pDM_Odm->bLinked, pDM_Odm->bOneEntryOnly));
	}
	else
	{
		//3 1. CFO Tracking
		//4 1.1 No new packet
		if(pCfoTrack->packetCount == pCfoTrack->packetCount_pre)
		{
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CFO_TRACKING, ODM_DBG_LOUD, ("ODM_CfoTracking(): packet counter doesn't change\n"));
			return;
		}
		pCfoTrack->packetCount_pre = pCfoTrack->packetCount;
	
		//4 1.2 Calculate CFO
		CFO_kHz_A =  (int)(pCfoTrack->CFO_tail[0] * 3125)  / 1280;
		CFO_kHz_B =  (int)(pCfoTrack->CFO_tail[1] * 3125)  / 1280;
		
		if(pDM_Odm->RFType < ODM_2T2R)
			CFO_ave = CFO_kHz_A;
		else
			CFO_ave = (int)(CFO_kHz_A + CFO_kHz_B) >> 1;
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CFO_TRACKING, ODM_DBG_LOUD, ("ODM_CfoTracking(): CFO_kHz_A = %dkHz, CFO_kHz_B = %dkHz, CFO_ave = %dkHz\n", 
						CFO_kHz_A, CFO_kHz_B, CFO_ave));

		//4 1.3 Avoid abnormal large CFO
		CFO_ave_diff = (pCfoTrack->CFO_ave_pre >= CFO_ave)?(pCfoTrack->CFO_ave_pre - CFO_ave):(CFO_ave - pCfoTrack->CFO_ave_pre);
		if(CFO_ave_diff > 20 && pCfoTrack->largeCFOHit == 0 && !pCfoTrack->bAdjust)
		{
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CFO_TRACKING, ODM_DBG_LOUD, ("ODM_CfoTracking(): first large CFO hit\n"));
			pCfoTrack->largeCFOHit = 1;
			return;
		}
		else
			pCfoTrack->largeCFOHit = 0;
		pCfoTrack->CFO_ave_pre = CFO_ave;

		//4 1.4 Dynamic Xtal threshold
		if(pCfoTrack->bAdjust == FALSE)
		{
			if(CFO_ave > CFO_TH_XTAL_HIGH || CFO_ave < (-CFO_TH_XTAL_HIGH))
				pCfoTrack->bAdjust = TRUE;
		}
		else
		{
			if(CFO_ave < CFO_TH_XTAL_LOW && CFO_ave > (-CFO_TH_XTAL_LOW))
				pCfoTrack->bAdjust = FALSE;
		}

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
		//4 1.5 BT case: Disable CFO tracking
		if(pDM_Odm->bBtEnabled)
		{
			pCfoTrack->bAdjust = FALSE;
			odm_SetCrystalCap(pDM_Odm, pCfoTrack->DefXCap);
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CFO_TRACKING, ODM_DBG_LOUD, ("ODM_CfoTracking(): Disable CFO tracking for BT!!\n"));
		}

		//4 1.6 Big jump 
		if(pCfoTrack->bAdjust)
		{
			if(CFO_ave > CFO_TH_XTAL_LOW)
				Adjust_Xtal =  Adjust_Xtal + ((CFO_ave - CFO_TH_XTAL_LOW) >> 2);
			else if(CFO_ave < (-CFO_TH_XTAL_LOW))
				Adjust_Xtal =  Adjust_Xtal + ((CFO_TH_XTAL_LOW - CFO_ave) >> 2);

			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CFO_TRACKING, ODM_DBG_LOUD, ("ODM_CfoTracking(): Crystal cap offset = %d\n", Adjust_Xtal));
		}
#endif

		//4 1.7 Adjust Crystal Cap.
		if(pCfoTrack->bAdjust)
		{
			if(CFO_ave > CFO_TH_XTAL_LOW)
				CrystalCap = CrystalCap + Adjust_Xtal;
			else if(CFO_ave < (-CFO_TH_XTAL_LOW))
				CrystalCap = CrystalCap - Adjust_Xtal;

			if(CrystalCap > 0x3f)
				CrystalCap = 0x3f;
			else if (CrystalCap < 0)
				CrystalCap = 0;

			odm_SetCrystalCap(pDM_Odm, (u1Byte)CrystalCap);
		}
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CFO_TRACKING, ODM_DBG_LOUD, ("ODM_CfoTracking(): Crystal cap = 0x%x, Default Crystal cap = 0x%x\n", 
			pCfoTrack->CrystalCap, pCfoTrack->DefXCap));

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
		if(pDM_Odm->SupportICType & ODM_IC_11AC_SERIES)
			return;
		
		//3 2. Dynamic ATC switch
		if(CFO_ave < CFO_TH_ATC && CFO_ave > -CFO_TH_ATC)
		{
			odm_SetATCStatus(pDM_Odm, FALSE);
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CFO_TRACKING, ODM_DBG_LOUD, ("ODM_CfoTracking(): Disable ATC!!\n"));
		}
		else
		{
			odm_SetATCStatus(pDM_Odm, TRUE);
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CFO_TRACKING, ODM_DBG_LOUD, ("ODM_CfoTracking(): Enable ATC!!\n"));
		}
#endif
	}
}

VOID
ODM_ParsingCFO(
	IN		PVOID			pDM_VOID,
	IN		PVOID			pPktinfo_VOID,
	IN		s1Byte* 			pcfotail
	)
{
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PODM_PACKET_INFO_T		pPktinfo = (PODM_PACKET_INFO_T)pPktinfo_VOID;
	PCFO_TRACKING			pCfoTrack = (PCFO_TRACKING)PhyDM_Get_Structure( pDM_Odm, PhyDM_CfoTrack);
	u1Byte					i;

	if(!(pDM_Odm->SupportAbility & ODM_BB_CFO_TRACKING))
		return;

#if(DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
	if(pPktinfo->bPacketMatchBSSID)
#else
	if(pPktinfo->StationID != 0)
#endif
	{				
		//3 Update CFO report for path-A & path-B
		// Only paht-A and path-B have CFO tail and short CFO
		for(i = ODM_RF_PATH_A; i <= ODM_RF_PATH_B; i++)   
		{
			pCfoTrack->CFO_tail[i] = (int)pcfotail[i];
	 	}

		//3 Update packet counter
		if(pCfoTrack->packetCount == 0xffffffff)
			pCfoTrack->packetCount = 0;
		else
	 		pCfoTrack->packetCount++;
	}
}

