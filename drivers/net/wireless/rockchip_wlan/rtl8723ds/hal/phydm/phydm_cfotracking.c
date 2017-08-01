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
#include "mp_precomp.h"
#include "phydm_precomp.h"

VOID
odm_SetCrystalCap(
	IN		PVOID					pDM_VOID,
	IN		u1Byte					CrystalCap
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PCFO_TRACKING				pCfoTrack = (PCFO_TRACKING)PhyDM_Get_Structure( pDM_Odm, PHYDM_CFOTRACK);

	if(pCfoTrack->CrystalCap == CrystalCap)
		return;

	pCfoTrack->CrystalCap = CrystalCap;

	if (pDM_Odm->SupportICType & (ODM_RTL8188E | ODM_RTL8188F)) {
		/* write 0x24[22:17] = 0x24[16:11] = CrystalCap */
		CrystalCap = CrystalCap & 0x3F;
		ODM_SetBBReg(pDM_Odm, REG_AFE_XTAL_CTRL, 0x007ff800, (CrystalCap|(CrystalCap << 6)));
	} else if (pDM_Odm->SupportICType & ODM_RTL8812) {
		/* write 0x2C[30:25] = 0x2C[24:19] = CrystalCap */
		CrystalCap = CrystalCap & 0x3F;
		ODM_SetBBReg(pDM_Odm, REG_MAC_PHY_CTRL, 0x7FF80000, (CrystalCap|(CrystalCap << 6)));
	} else if ((pDM_Odm->SupportICType & (ODM_RTL8703B|ODM_RTL8723B|ODM_RTL8192E|ODM_RTL8821))) {
		/* 0x2C[23:18] = 0x2C[17:12] = CrystalCap */
		CrystalCap = CrystalCap & 0x3F;
		ODM_SetBBReg(pDM_Odm, REG_MAC_PHY_CTRL, 0x00FFF000, (CrystalCap|(CrystalCap << 6)));	
	}  else if (pDM_Odm->SupportICType & ODM_RTL8814A) {
		/* write 0x2C[26:21] = 0x2C[20:15] = CrystalCap */
		CrystalCap = CrystalCap & 0x3F;
		ODM_SetBBReg(pDM_Odm, REG_MAC_PHY_CTRL, 0x07FF8000, (CrystalCap|(CrystalCap << 6)));
	} else if (pDM_Odm->SupportICType & (ODM_RTL8822B|ODM_RTL8821C)) {
		/* write 0x24[30:25] = 0x28[6:1] = CrystalCap */
		CrystalCap = CrystalCap & 0x3F;
		ODM_SetBBReg(pDM_Odm, REG_AFE_XTAL_CTRL, 0x7e000000, CrystalCap);
		ODM_SetBBReg(pDM_Odm, REG_AFE_PLL_CTRL, 0x7e, CrystalCap);
	} else {
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CFO_TRACKING, ODM_DBG_LOUD, ("odm_SetCrystalCap(): Use default setting.\n"));
		ODM_SetBBReg(pDM_Odm, REG_MAC_PHY_CTRL, 0xFFF000, (CrystalCap|(CrystalCap << 6)));
	}

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CFO_TRACKING, ODM_DBG_LOUD, ("odm_SetCrystalCap(): CrystalCap = 0x%x\n", CrystalCap));
#endif
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
	PCFO_TRACKING				pCfoTrack = (PCFO_TRACKING)PhyDM_Get_Structure( pDM_Odm, PHYDM_CFOTRACK);

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
	PCFO_TRACKING				pCfoTrack = (PCFO_TRACKING)PhyDM_Get_Structure( pDM_Odm, PHYDM_CFOTRACK);

	pCfoTrack->DefXCap = odm_GetDefaultCrytaltalCap(pDM_Odm);
	pCfoTrack->bAdjust = TRUE;

	if(pCfoTrack->CrystalCap > pCfoTrack->DefXCap)
	{
		odm_SetCrystalCap(pDM_Odm, pCfoTrack->CrystalCap - 1);
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CFO_TRACKING, ODM_DBG_LOUD,
			("ODM_CfoTrackingReset(): approch default value (0x%x)\n", pCfoTrack->CrystalCap));
	} else if (pCfoTrack->CrystalCap < pCfoTrack->DefXCap)
	{
		odm_SetCrystalCap(pDM_Odm, pCfoTrack->CrystalCap + 1);
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CFO_TRACKING, ODM_DBG_LOUD,
			("ODM_CfoTrackingReset(): approch default value (0x%x)\n", pCfoTrack->CrystalCap));
	}

	#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
	odm_SetATCStatus(pDM_Odm, TRUE);
	#endif
}

VOID
ODM_CfoTrackingInit(
	IN		PVOID					pDM_VOID
)
{
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PCFO_TRACKING				pCfoTrack = (PCFO_TRACKING)PhyDM_Get_Structure( pDM_Odm, PHYDM_CFOTRACK);

	pCfoTrack->DefXCap = pCfoTrack->CrystalCap = odm_GetDefaultCrytaltalCap(pDM_Odm);
	pCfoTrack->bATCStatus = odm_GetATCStatus(pDM_Odm);
	pCfoTrack->bAdjust = TRUE;
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CFO_TRACKING, ODM_DBG_LOUD, ("ODM_CfoTracking_init()=========>\n"));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CFO_TRACKING, ODM_DBG_LOUD, ("ODM_CfoTracking_init(): bATCStatus = %d, CrystalCap = 0x%x\n", pCfoTrack->bATCStatus, pCfoTrack->DefXCap));

#if RTL8822B_SUPPORT
	/* Crystal cap. control by WiFi */
	if (pDM_Odm->SupportICType & ODM_RTL8822B)
		ODM_SetBBReg(pDM_Odm, 0x10, 0x40, 0x1);
#endif

#if RTL8821C_SUPPORT
	/* Crystal cap. control by WiFi */
	if (pDM_Odm->SupportICType & ODM_RTL8821C)
		ODM_SetBBReg(pDM_Odm, 0x10, 0x40, 0x1);
#endif
}

VOID
ODM_CfoTracking(
	IN		PVOID					pDM_VOID
)
{
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PCFO_TRACKING				pCfoTrack = (PCFO_TRACKING)PhyDM_Get_Structure( pDM_Odm, PHYDM_CFOTRACK);
	s4Byte						CFO_ave = 0;
	u4Byte						CFO_rpt_sum, CFO_kHz_avg[4] = {0};
	s4Byte						CFO_ave_diff;
	s1Byte						CrystalCap = pCfoTrack->CrystalCap;
	u1Byte						Adjust_Xtal = 1, i, valid_path_cnt = 0;

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
		for (i = 0; i < pDM_Odm->num_rf_path; i++) {
		
			if (pCfoTrack->CFO_cnt[i] == 0)
				continue;

			valid_path_cnt++;
			CFO_rpt_sum = (u4Byte)((pCfoTrack->CFO_tail[i] < 0) ? (0 - pCfoTrack->CFO_tail[i]) :  pCfoTrack->CFO_tail[i]);
			CFO_kHz_avg[i] = CFO_HW_RPT_2_MHZ(CFO_rpt_sum) / pCfoTrack->CFO_cnt[i];
	
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CFO_TRACKING, ODM_DBG_LOUD, ("[Path %d] CFO_rpt_sum = (( %d )), CFO_cnt = (( %d )) , CFO_avg= (( %s%d )) kHz\n",
				i, CFO_rpt_sum, pCfoTrack->CFO_cnt[i],((pCfoTrack->CFO_tail[i] < 0) ? "-" : " ") ,CFO_kHz_avg[i]));
		}
		
		for (i = 0; i < valid_path_cnt; i++) {
			
			//ODM_RT_TRACE(pDM_Odm, ODM_COMP_CFO_TRACKING, ODM_DBG_LOUD, ("path [%d], pCfoTrack->CFO_tail = %d\n", i, pCfoTrack->CFO_tail[i]));	
			if (pCfoTrack->CFO_tail[i] < 0) {
				CFO_ave += (0-(s4Byte)CFO_kHz_avg[i]);
				//ODM_RT_TRACE(pDM_Odm, ODM_COMP_CFO_TRACKING, ODM_DBG_LOUD, ("CFO_ave = %d\n", CFO_ave));	
			}
			else
				CFO_ave += (s4Byte)CFO_kHz_avg[i];
		}

		if (valid_path_cnt >= 2)
			CFO_ave = CFO_ave / valid_path_cnt;
			
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CFO_TRACKING, ODM_DBG_LOUD, ("valid_path_cnt = ((%d)), CFO_ave = ((%d kHz))\n", valid_path_cnt, CFO_ave));

		/*reset counter*/
		for (i = 0; i < pDM_Odm->num_rf_path; i++) {
			pCfoTrack->CFO_tail[i] = 0;
			pCfoTrack->CFO_cnt[i] = 0;
		}

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
/*
		//4 1.6 Big jump 
		if(pCfoTrack->bAdjust)
		{
			if(CFO_ave > CFO_TH_XTAL_LOW)
				Adjust_Xtal =  Adjust_Xtal + ((CFO_ave - CFO_TH_XTAL_LOW) >> 2);
			else if(CFO_ave < (-CFO_TH_XTAL_LOW))
				Adjust_Xtal =  Adjust_Xtal + ((CFO_TH_XTAL_LOW - CFO_ave) >> 2);

			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CFO_TRACKING, ODM_DBG_LOUD, ("ODM_CfoTracking(): Crystal cap offset = %d\n", Adjust_Xtal));
		}
*/
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
	IN		s1Byte*			pcfotail,
	IN		u1Byte			num_ss
	)
{
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PODM_PACKET_INFO_T		pPktinfo = (PODM_PACKET_INFO_T)pPktinfo_VOID;
	PCFO_TRACKING			pCfoTrack = (PCFO_TRACKING)PhyDM_Get_Structure( pDM_Odm, PHYDM_CFOTRACK);
	u1Byte					i;

	if(!(pDM_Odm->SupportAbility & ODM_BB_CFO_TRACKING))
		return;

#if(DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
	if(pPktinfo->bPacketMatchBSSID)
#else
	if(pPktinfo->StationID != 0)
#endif
	{
		if (num_ss > pDM_Odm->num_rf_path) /*For fool proof*/
			num_ss = pDM_Odm->num_rf_path;

		/*ODM_RT_TRACE(pDM_Odm, ODM_COMP_CFO_TRACKING, ODM_DBG_LOUD, ("num_ss = ((%d)),  pDM_Odm->num_rf_path = ((%d))\n", num_ss,  pDM_Odm->num_rf_path));*/

		
		//3 Update CFO report for path-A & path-B
		// Only paht-A and path-B have CFO tail and short CFO
		for(i = 0; i < num_ss; i++)
		{
			pCfoTrack->CFO_tail[i] += pcfotail[i];
			pCfoTrack->CFO_cnt[i] ++;
			/*ODM_RT_TRACE(pDM_Odm, ODM_COMP_CFO_TRACKING, ODM_DBG_LOUD, ("[ID %d][path %d][Rate 0x%x] CFO_tail = ((%d)), CFO_tail_sum = ((%d)), CFO_cnt = ((%d))\n", 
				pPktinfo->StationID, i, pPktinfo->DataRate, pcfotail[i], pCfoTrack->CFO_tail[i], pCfoTrack->CFO_cnt[i]));
			*/
		}

		//3 Update packet counter
		if(pCfoTrack->packetCount == 0xffffffff)
			pCfoTrack->packetCount = 0;
		else
			pCfoTrack->packetCount++;
	}
}

