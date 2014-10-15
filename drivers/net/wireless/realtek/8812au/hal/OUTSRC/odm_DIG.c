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

#include "odm_precomp.h"

VOID
ODM_ChangeDynamicInitGainThresh(
	IN		PVOID					pDM_VOID,
	IN	u4Byte		DM_Type,
	IN	u4Byte		DM_Value
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	pDIG_T	pDM_DigTable = &pDM_Odm->DM_DigTable;

	if (DM_Type == DIG_TYPE_THRESH_HIGH)
	{
		pDM_DigTable->RssiHighThresh = DM_Value;		
	}
	else if (DM_Type == DIG_TYPE_THRESH_LOW)
	{
		pDM_DigTable->RssiLowThresh = DM_Value;
	}
	else if (DM_Type == DIG_TYPE_ENABLE)
	{
		pDM_DigTable->Dig_Enable_Flag	= TRUE;
	}	
	else if (DM_Type == DIG_TYPE_DISABLE)
	{
		pDM_DigTable->Dig_Enable_Flag = FALSE;
	}	
	else if (DM_Type == DIG_TYPE_BACKOFF)
	{
		if(DM_Value > 30)
			DM_Value = 30;
		pDM_DigTable->BackoffVal = (u1Byte)DM_Value;
	}
	else if(DM_Type == DIG_TYPE_RX_GAIN_MIN)
	{
		if(DM_Value == 0)
			DM_Value = 0x1;
		pDM_DigTable->rx_gain_range_min = (u1Byte)DM_Value;
	}
	else if(DM_Type == DIG_TYPE_RX_GAIN_MAX)
	{
		if(DM_Value > 0x50)
			DM_Value = 0x50;
		pDM_DigTable->rx_gain_range_max = (u1Byte)DM_Value;
	}
}	// DM_ChangeDynamicInitGainThresh //

VOID
odm_NHMBBInit(
	IN		PVOID			pDM_VOID
)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;

	pDM_Odm->adaptivity_flag = 0;
	pDM_Odm->tolerance_cnt = 3;
	pDM_Odm->NHMLastTxOkcnt = 0;
	pDM_Odm->NHMLastRxOkcnt = 0;
	pDM_Odm->NHMCurTxOkcnt = 0;
	pDM_Odm->NHMCurRxOkcnt = 0;

	if(pDM_Odm->SupportICType & ODM_IC_11AC_SERIES)
		{
		//PHY parameters initialize for ac series
		ODM_Write2Byte(pDM_Odm, ODM_REG_NHM_TIMER_11AC+2, 0x2710);	//0x990[31:16]=0x2710	Time duration for NHM unit: 4us, 0x2710=40ms
		ODM_Write2Byte(pDM_Odm, ODM_REG_NHM_TH9_TH10_11AC+2, 0xffff);	//0x994[31:16]=0xffff	th_9, th_10
		//ODM_Write4Byte(pDM_Odm, ODM_REG_NHM_TH3_TO_TH0_11AC, 0xffffff5c);	//0x998=0xffffff5c 		th_3, th_2, th_1, th_0
		ODM_Write4Byte(pDM_Odm, ODM_REG_NHM_TH3_TO_TH0_11AC, 0xffffff52);	//0x998=0xffffff52 		th_3, th_2, th_1, th_0
		ODM_Write4Byte(pDM_Odm, ODM_REG_NHM_TH7_TO_TH4_11AC, 0xffffffff);	//0x99c=0xffffffff		th_7, th_6, th_5, th_4
		ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH8_11AC, bMaskByte0, 0xff);		//0x9a0[7:0]=0xff		th_8
		ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH9_TH10_11AC, BIT8|BIT9|BIT10, 7);	//0x994[9:8]=3			enable CCX
		ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_9E8_11AC, BIT0, 1);		//0x9e8[7]=1			max power among all RX ants	
				
		//panic_printk("RTL8812AU phy parameters init %s,%d\n", __FUNCTION__, __LINE__);
		}
	else if (pDM_Odm->SupportICType & ODM_IC_11N_SERIES)
		{
		//PHY parameters initialize for n series
		ODM_Write2Byte(pDM_Odm, ODM_REG_NHM_TIMER_11N+2, 0x2710);	//0x894[31:16]=0x2710	Time duration for NHM unit: 4us, 0x2710=40ms
		//ODM_Write2Byte(pDM_Odm, ODM_REG_NHM_TIMER_11N+2, 0x4e20);	//0x894[31:16]=0x4e20	Time duration for NHM unit: 4us, 0x4e20=80ms
		ODM_Write2Byte(pDM_Odm, ODM_REG_NHM_TH9_TH10_11N+2, 0xffff);	//0x890[31:16]=0xffff	th_9, th_10
		//ODM_Write4Byte(pDM_Odm, ODM_REG_NHM_TH3_TO_TH0_11N, 0xffffff5c);	//0x898=0xffffff5c 		th_3, th_2, th_1, th_0
		ODM_Write4Byte(pDM_Odm, ODM_REG_NHM_TH3_TO_TH0_11N, 0xffffff52);	//0x898=0xffffff52 		th_3, th_2, th_1, th_0
		ODM_Write4Byte(pDM_Odm, ODM_REG_NHM_TH7_TO_TH4_11N, 0xffffffff);	//0x89c=0xffffffff		th_7, th_6, th_5, th_4
		ODM_SetBBReg(pDM_Odm, ODM_REG_FPGA0_IQK_11N, bMaskByte0, 0xff);		//0xe28[7:0]=0xff		th_8
		ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH9_TH10_11N, BIT10|BIT9|BIT8, 7);	//0x890[9:8]=3			enable CCX
		ODM_SetBBReg(pDM_Odm, ODM_REG_OFDM_FA_RSTC_11N, BIT7, 1);		//0xc0c[7]=1			max power among all RX ants				
		}

}

//
VOID
odm_NHMBB(
	IN		PVOID			pDM_VOID
)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte	NHM_cnt_0;//, NHM_cnt_1;
	u4Byte	value32 = 0;
	//u1Byte 	test_status;
	//PFALSE_ALARM_STATISTICS pFalseAlmCnt = &(pDM_Odm->FalseAlmCnt);
	
	if (pDM_Odm->SupportICType & ODM_IC_11AC_SERIES)
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_NHM_CNT_11AC, bMaskDWord);
	else if (pDM_Odm->SupportICType & ODM_IC_11N_SERIES)
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_NHM_CNT_11N, bMaskDWord);

	NHM_cnt_0= (u1Byte)(value32 & bMaskByte0);
	//NHM_cnt_1= (u1Byte)((value32 & bMaskByte1)>>8);

	pDM_Odm->NHMCurTxOkcnt = *(pDM_Odm->pNumTxBytesUnicast) - pDM_Odm->NHMLastTxOkcnt;
	pDM_Odm->NHMCurRxOkcnt = *(pDM_Odm->pNumRxBytesUnicast) - pDM_Odm->NHMLastRxOkcnt;
	pDM_Odm->NHMLastTxOkcnt = *(pDM_Odm->pNumTxBytesUnicast);
	pDM_Odm->NHMLastRxOkcnt = *(pDM_Odm->pNumRxBytesUnicast);	
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("NHM_cnt_0=%d, NHMCurTxOkcnt = %llu, NHMCurRxOkcnt = %llu\n", 
		NHM_cnt_0, pDM_Odm->NHMCurTxOkcnt, pDM_Odm->NHMCurRxOkcnt));

	
	if ( (pDM_Odm->NHMCurTxOkcnt) + 1 > (u8Byte)(pDM_Odm->NHMCurRxOkcnt<<2) + 1)		//Tx > 4*Rx possible for adaptivity test
	{
		if(NHM_cnt_0 >= 190 || pDM_Odm->adaptivity_flag == TRUE)
		{
			//Enable EDCCA since it is possible running Adaptivity testing
			//test_status = 1;
            		pDM_Odm->adaptivity_flag = TRUE;
            		ODM_SetMACReg(pDM_Odm, REG_TX_PTCL_CTRL, BIT15, 0);	//don't ignore EDCCA	 reg520[15]=0
			ODM_SetMACReg(pDM_Odm, REG_RD_CTRL, BIT11, 1);	//reg524[11]=1	
			pDM_Odm->tolerance_cnt = 0;
            	}
		else
		{
			if(pDM_Odm->tolerance_cnt<3)
				pDM_Odm->tolerance_cnt = pDM_Odm->tolerance_cnt + 1;
			else
				pDM_Odm->tolerance_cnt = 4;	
			//test_status = 5;
			if(pDM_Odm->tolerance_cnt > 3)
			{
				//test_status = 3;
				ODM_SetMACReg(pDM_Odm, REG_TX_PTCL_CTRL, BIT15, 1);	//ignore EDCCA	reg520[15]=1
            			ODM_SetMACReg(pDM_Odm, REG_RD_CTRL, BIT11, 0);		//reg524[11]=0
            			pDM_Odm->adaptivity_flag = FALSE;
			}
		}
	}
	else	// TX<RX 
	{
		if(pDM_Odm->adaptivity_flag == TRUE && NHM_cnt_0 <= 200)
		{
			//test_status = 2;
			ODM_SetMACReg(pDM_Odm, REG_TX_PTCL_CTRL, BIT15, 0);	//don't ignore EDCCA	 reg520[15]=0
			ODM_SetMACReg(pDM_Odm, REG_RD_CTRL, BIT11, 1);	//reg524[11]=1	
			pDM_Odm->tolerance_cnt = 0;
		}
		else
		{
			if(pDM_Odm->tolerance_cnt<3)
				pDM_Odm->tolerance_cnt = pDM_Odm->tolerance_cnt + 1;
			else
				pDM_Odm->tolerance_cnt = 4;	
			//test_status = 5;
			if(pDM_Odm->tolerance_cnt >3)
			{
				//test_status = 4;
				ODM_SetMACReg(pDM_Odm, REG_TX_PTCL_CTRL, BIT15, 1);	//ignore EDCCA	reg520[15]=1
            			ODM_SetMACReg(pDM_Odm, REG_RD_CTRL, BIT11, 0);		//reg524[11]=0
            			pDM_Odm->adaptivity_flag = FALSE;
			}
		}
	}
	 
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("adaptivity_flag = %d\n ", pDM_Odm->adaptivity_flag));

	if (pDM_Odm->SupportICType & ODM_IC_11AC_SERIES)	//disable enable NHX
	{           		
    		ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH9_TH10_11AC, BIT1, 0);
    		ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH9_TH10_11AC, BIT1, 1);
	}	
	else if (pDM_Odm->SupportICType & ODM_IC_11N_SERIES)
	{
    		ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH9_TH10_11N, BIT1, 0);
    		ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH9_TH10_11N, BIT1, 1);
	}
}

VOID
odm_SearchPwdBLowerBound(
	IN		PVOID		pDM_VOID,
	IN		u1Byte		IGI_target
)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u4Byte			value32 =0;
	u1Byte			cnt, IGI;
	BOOLEAN			bAdjust=TRUE;
	s1Byte 			TH_L2H_dmc, TH_H2L_dmc;
	s1Byte 			TH_L2H, TH_H2L, Diff;

	IGI = 0x50; // find H2L, L2H lower bound
	ODM_Write_DIG(pDM_Odm, IGI);
	

	Diff = IGI_target -(s1Byte)IGI;
	TH_L2H_dmc = pDM_Odm->TH_L2H_ini + Diff;
		if(TH_L2H_dmc > 10) 	
			TH_L2H_dmc = 10;
	TH_H2L_dmc = TH_L2H_dmc - pDM_Odm->TH_EDCCA_HL_diff;
	if(pDM_Odm->SupportICType & ODM_IC_11N_SERIES)
		{
		ODM_SetBBReg(pDM_Odm,rOFDM0_ECCAThreshold, bMaskByte0, (u1Byte)TH_L2H_dmc);
		ODM_SetBBReg(pDM_Odm,rOFDM0_ECCAThreshold, bMaskByte2, (u1Byte)TH_H2L_dmc);
		}
	else
		ODM_SetBBReg(pDM_Odm, rFPGA0_XB_LSSIReadBack, 0xFFFF, ((u1Byte)TH_H2L_dmc<<8) | (u1Byte)TH_L2H_dmc);
					
		rtw_mdelay_os(5);
		
		while(bAdjust)
			{
			for(cnt=0; cnt<20; cnt ++)
				{
				if (pDM_Odm->SupportICType & ODM_IC_11N_SERIES)
					value32 = ODM_GetBBReg(pDM_Odm,ODM_REG_RPT_11N, bMaskDWord);
				else if(pDM_Odm->SupportICType & ODM_IC_11AC_SERIES)
					value32 = ODM_GetBBReg(pDM_Odm,ODM_REG_RPT_11AC, bMaskDWord);
			
				if (value32 & BIT30 && (pDM_Odm->SupportICType & (ODM_RTL8723A|ODM_RTL8723B|ODM_RTL8188E)))
					pDM_Odm->txEdcca1 = pDM_Odm->txEdcca1 + 1;
				else if(value32 & BIT29)
					pDM_Odm->txEdcca1 = pDM_Odm->txEdcca1 + 1;
				else
					pDM_Odm->txEdcca0 = pDM_Odm->txEdcca0 + 1;
				}
			//DbgPrint("txEdcca1 = %d, txEdcca0 = %d\n", pDM_Odm->txEdcca1, pDM_Odm->txEdcca0);
			
				if(pDM_Odm->txEdcca1 > 5 )
				{
					IGI = IGI -1;
					TH_L2H_dmc = TH_L2H_dmc + 1;
						if(TH_L2H_dmc > 10)
							TH_L2H_dmc = 10;
					TH_H2L_dmc = TH_L2H_dmc - pDM_Odm->TH_EDCCA_HL_diff;
					if(pDM_Odm->SupportICType & ODM_IC_11N_SERIES)
					{
						ODM_SetBBReg(pDM_Odm,rOFDM0_ECCAThreshold, bMaskByte0, (u1Byte)TH_L2H_dmc);
						ODM_SetBBReg(pDM_Odm,rOFDM0_ECCAThreshold, bMaskByte2, (u1Byte)TH_H2L_dmc);
					}
					else
						ODM_SetBBReg(pDM_Odm, rFPGA0_XB_LSSIReadBack, 0xFFFF, ((u1Byte)TH_H2L_dmc<<8) | (u1Byte)TH_L2H_dmc);

					pDM_Odm->TxHangFlg = TRUE;
					pDM_Odm->txEdcca1 = 0;
					pDM_Odm->txEdcca0 = 0;

					if(TH_L2H_dmc == 10)
						{
						bAdjust = FALSE;
						pDM_Odm->TxHangFlg = FALSE;
						pDM_Odm->txEdcca1 = 0;
						pDM_Odm->txEdcca0 = 0;
						pDM_Odm->H2L_lb = TH_H2L_dmc;
						pDM_Odm->L2H_lb = TH_L2H_dmc;
						pDM_Odm->Adaptivity_IGI_upper = IGI;
						}
					}
				else
				{
					bAdjust = FALSE;
					pDM_Odm->TxHangFlg = FALSE;
					pDM_Odm->txEdcca1 = 0;
					pDM_Odm->txEdcca0 = 0;
					pDM_Odm->H2L_lb = TH_H2L_dmc;
					pDM_Odm->L2H_lb = TH_L2H_dmc;	
					pDM_Odm->Adaptivity_IGI_upper = IGI;
				}
			}

							
ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("IGI = 0x%x, H2L_lb = 0x%x, L2H_lb = 0x%x\n", IGI, pDM_Odm->H2L_lb , pDM_Odm->L2H_lb));
		
}

VOID
odm_AdaptivityInit(
IN		PVOID					pDM_VOID
)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	if(pDM_Odm->SupportICType == ODM_RTL8723B)
	{
		pDM_Odm->TH_L2H_ini = 0xf8; // -8
	}
	if((pDM_Odm->SupportICType == ODM_RTL8192E)&&(pDM_Odm->SupportInterface == ODM_ITRF_PCIE))
	{
		pDM_Odm->TH_L2H_ini = 0xf0; // -16
	}
	else
	{
		pDM_Odm->TH_L2H_ini = 0xf7; // -9
	}
	
	pDM_Odm->TH_EDCCA_HL_diff = 7;
	pDM_Odm->IGI_Base = 0x32;
	pDM_Odm->IGI_target = 0x1c;
	pDM_Odm->ForceEDCCA = 0;
	pDM_Odm->AdapEn_RSSI = 20;

	pDM_Odm->NHM_disable = FALSE;
	pDM_Odm->TxHangFlg = TRUE;
	pDM_Odm->txEdcca0 = 0;
	pDM_Odm->txEdcca1 = 0;
	pDM_Odm->H2L_lb= 0;
	pDM_Odm->L2H_lb= 0;
	pDM_Odm->Adaptivity_IGI_upper = 0;
	odm_NHMBBInit(pDM_Odm);
	//Reg524[11]=0 is easily to transmit packets during adaptivity test

	ODM_SetBBReg(pDM_Odm, REG_RD_CTRL, BIT11, 1); // stop counting if EDCCA is asserted
}

// Add by Neil Chen to enable edcca to MP Platform 

VOID
odm_Adaptivity(
	IN		PVOID					pDM_VOID,
	IN		u1Byte			IGI
)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	s1Byte TH_L2H_dmc, TH_H2L_dmc;
	s1Byte Diff, IGI_target;
	BOOLEAN EDCCA_State = 0;

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PADAPTER		pAdapter	= pDM_Odm->Adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	BOOLEAN		bFwCurrentInPSMode=FALSE;	
	PMGNT_INFO				pMgntInfo = &(pAdapter->MgntInfo);
		
	pAdapter->HalFunc.GetHwRegHandler(pAdapter, HW_VAR_FW_PSMODE_STATUS, (pu1Byte)(&bFwCurrentInPSMode));	

	// Disable EDCCA mode while under LPS mode, added by Roger, 2012.09.14.
	if(bFwCurrentInPSMode)
		return;
#endif

	if(!(pDM_Odm->SupportAbility & ODM_BB_ADAPTIVITY))
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("Go to odm_DynamicEDCCA() \n"));
		// Add by Neil Chen to enable edcca to MP Platform 
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
		// Adjust EDCCA.
		if(pDM_Odm->SupportICType & ODM_IC_11N_SERIES)
			odm_DynamicEDCCA(pDM_Odm);
#endif
		return;
	}
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_Adaptivity() =====> \n"));

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("ForceEDCCA=%d, IGI_Base=0x%x, TH_L2H_ini = %d, TH_EDCCA_HL_diff = %d, AdapEn_RSSI = %d\n", 
		pDM_Odm->ForceEDCCA, pDM_Odm->IGI_Base, pDM_Odm->TH_L2H_ini, pDM_Odm->TH_EDCCA_HL_diff, pDM_Odm->AdapEn_RSSI));

	if(pDM_Odm->SupportICType & ODM_IC_11AC_SERIES)
		ODM_SetBBReg(pDM_Odm, 0x800, BIT10, 0); //ADC_mask enable
	
	if((!pDM_Odm->bLinked)||(*pDM_Odm->pChannel > 149)) // Band4 doesn't need adaptivity
	{
		if(pDM_Odm->SupportICType & ODM_IC_11N_SERIES)
		{
			ODM_SetBBReg(pDM_Odm,rOFDM0_ECCAThreshold, bMaskByte0, 0x7f);
			ODM_SetBBReg(pDM_Odm,rOFDM0_ECCAThreshold, bMaskByte2, 0x7f);
		}
		else
			ODM_SetBBReg(pDM_Odm, rFPGA0_XB_LSSIReadBack, 0xFFFF, (0x7f<<8) | 0x7f);
		return;
	}

#if (DM_ODM_SUPPORT_TYPE==ODM_WIN)	
		if(pMgntInfo->IOTPeer == HT_IOT_PEER_BROADCOM)
			ODM_Write1Byte(pDM_Odm, REG_TRX_SIFS_OFDM, 0x0a); 
		else
			ODM_Write1Byte(pDM_Odm, REG_TRX_SIFS_OFDM, 0x0e);
#endif
	if(!pDM_Odm->ForceEDCCA)
	{
		if(pDM_Odm->RSSI_Min > pDM_Odm->AdapEn_RSSI)
			EDCCA_State = 1;
		else if(pDM_Odm->RSSI_Min < (pDM_Odm->AdapEn_RSSI - 5))
			EDCCA_State = 0;
	}
	else
		EDCCA_State = 1;
	//if((pDM_Odm->SupportICType & ODM_IC_11AC_SERIES) && (*pDM_Odm->pBandType == BAND_ON_5G))
		//IGI_target = pDM_Odm->IGI_Base;
	//else
	{

		if(*pDM_Odm->pBandWidth == ODM_BW20M) //CHANNEL_WIDTH_20
			IGI_target = pDM_Odm->IGI_Base;
		else if(*pDM_Odm->pBandWidth == ODM_BW40M)
			IGI_target = pDM_Odm->IGI_Base + 2;
		else if(*pDM_Odm->pBandWidth == ODM_BW80M)
			IGI_target = pDM_Odm->IGI_Base + 6;
		else
			IGI_target = pDM_Odm->IGI_Base;
	}

	pDM_Odm->IGI_target = (u1Byte) IGI_target;

	//Search pwdB lower bound
	if(pDM_Odm->TxHangFlg == TRUE)
	{
		if (pDM_Odm->SupportICType & ODM_IC_11N_SERIES)
			ODM_SetBBReg(pDM_Odm,ODM_REG_DBG_RPT_11N, bMaskDWord, 0x208);
		else if(pDM_Odm->SupportICType & ODM_IC_11AC_SERIES)
			ODM_SetBBReg(pDM_Odm,ODM_REG_DBG_RPT_11AC, bMaskDWord, 0x209);

		odm_SearchPwdBLowerBound(pDM_Odm, IGI_target);
	}

	if(pDM_Odm->bLinked && pDM_Odm->TxHangFlg == FALSE &&pDM_Odm->NHM_disable == FALSE)
		odm_NHMBB(pDM_Odm);
	
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("BandWidth=%s, IGI_target=0x%x, EDCCA_State=%d\n",
		(*pDM_Odm->pBandWidth==ODM_BW80M)?"80M":((*pDM_Odm->pBandWidth==ODM_BW40M)?"40M":"20M"), IGI_target, EDCCA_State));

	if(EDCCA_State == 1)
	{
		Diff = IGI_target -(s1Byte)IGI;
		TH_L2H_dmc = pDM_Odm->TH_L2H_ini + Diff;
		if(TH_L2H_dmc > 10) 	TH_L2H_dmc = 10;
		TH_H2L_dmc = TH_L2H_dmc - pDM_Odm->TH_EDCCA_HL_diff;

		//replace lower bound to prevent EDCCA always equal 1
			if(TH_H2L_dmc < pDM_Odm->H2L_lb)				
				TH_H2L_dmc = pDM_Odm->H2L_lb;
			if(TH_L2H_dmc < pDM_Odm->L2H_lb)
				TH_L2H_dmc = pDM_Odm->L2H_lb;
	}
	else
	{
		TH_L2H_dmc = 0x7f;
		TH_H2L_dmc = 0x7f;
	}
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("IGI=0x%x, TH_L2H_dmc = %d, TH_H2L_dmc = %d\n", 
		IGI, TH_L2H_dmc, TH_H2L_dmc));

	if(pDM_Odm->SupportICType & ODM_IC_11N_SERIES)
	{
		ODM_SetBBReg(pDM_Odm,rOFDM0_ECCAThreshold, bMaskByte0, (u1Byte)TH_L2H_dmc);
		ODM_SetBBReg(pDM_Odm,rOFDM0_ECCAThreshold, bMaskByte2, (u1Byte)TH_H2L_dmc);
	}
	else
		ODM_SetBBReg(pDM_Odm, rFPGA0_XB_LSSIReadBack, 0xFFFF, ((u1Byte)TH_H2L_dmc<<8) | (u1Byte)TH_L2H_dmc);
}

int getIGIForDiff(int value_IGI)
{
	#define ONERCCA_LOW_TH		0x30
	#define ONERCCA_LOW_DIFF	8

	if (value_IGI < ONERCCA_LOW_TH) {
		if ((ONERCCA_LOW_TH - value_IGI) < ONERCCA_LOW_DIFF)
			return ONERCCA_LOW_TH;
		else
			return value_IGI + ONERCCA_LOW_DIFF;
	} else {
		return value_IGI;
	}
}


VOID
ODM_Write_DIG(
	IN		PVOID					pDM_VOID,
	IN	u1Byte			CurrentIGI
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	pDIG_T	pDM_DigTable = &pDM_Odm->DM_DigTable;

	if(pDM_Odm->StopDIG)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("Stop Writing IGI\n"));
		return;
	}

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("ODM_REG(IGI_A,pDM_Odm)=0x%x, ODM_BIT(IGI,pDM_Odm)=0x%x \n",
		ODM_REG(IGI_A,pDM_Odm),ODM_BIT(IGI,pDM_Odm)));

	if(pDM_DigTable->CurIGValue != CurrentIGI)//if(pDM_DigTable->PreIGValue != CurrentIGI)
	{
		if(pDM_Odm->SupportPlatform & (ODM_CE|ODM_WIN))
		{ 
				ODM_SetBBReg(pDM_Odm, ODM_REG(IGI_A,pDM_Odm), ODM_BIT(IGI,pDM_Odm), CurrentIGI);
			if(pDM_Odm->RFType != ODM_1T1R)
				ODM_SetBBReg(pDM_Odm, ODM_REG(IGI_B,pDM_Odm), ODM_BIT(IGI,pDM_Odm), CurrentIGI);
			}
		else if(pDM_Odm->SupportPlatform & (ODM_AP|ODM_ADSL))
		{
			switch(*(pDM_Odm->pOnePathCCA))
			{
			case ODM_CCA_2R:
			ODM_SetBBReg(pDM_Odm, ODM_REG(IGI_A,pDM_Odm), ODM_BIT(IGI,pDM_Odm), CurrentIGI);
					if(pDM_Odm->RFType != ODM_1T1R)
					ODM_SetBBReg(pDM_Odm, ODM_REG(IGI_B,pDM_Odm), ODM_BIT(IGI,pDM_Odm), CurrentIGI);
				break;
			case ODM_CCA_1R_A:
				ODM_SetBBReg(pDM_Odm, ODM_REG(IGI_A,pDM_Odm), ODM_BIT(IGI,pDM_Odm), CurrentIGI);
					if(pDM_Odm->RFType != ODM_1T1R)
					ODM_SetBBReg(pDM_Odm, ODM_REG(IGI_B,pDM_Odm), ODM_BIT(IGI,pDM_Odm), getIGIForDiff(CurrentIGI));
				break;
			case ODM_CCA_1R_B:
				ODM_SetBBReg(pDM_Odm, ODM_REG(IGI_A,pDM_Odm), ODM_BIT(IGI,pDM_Odm), getIGIForDiff(CurrentIGI));
					if(pDM_Odm->RFType != ODM_1T1R)
					ODM_SetBBReg(pDM_Odm, ODM_REG(IGI_B,pDM_Odm), ODM_BIT(IGI,pDM_Odm), CurrentIGI);
					break;
				}
		}
		
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("CurrentIGI(0x%02x). \n",CurrentIGI));
		//pDM_DigTable->PreIGValue = pDM_DigTable->CurIGValue;
		pDM_DigTable->CurIGValue = CurrentIGI;
	}	
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("ODM_Write_DIG():CurrentIGI=0x%x \n",CurrentIGI));
	
}

VOID
odm_PauseDIG(
	IN		PVOID					pDM_VOID,
	IN		ODM_Pause_DIG_TYPE		PauseType,
	IN		u1Byte					IGIValue
)
{
	PDM_ODM_T			pDM_Odm = (PDM_ODM_T)pDM_VOID;
	pDIG_T				pDM_DigTable = &pDM_Odm->DM_DigTable;
	static	BOOLEAN		bPaused = FALSE;

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_PauseDIG()=========>\n"));

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)	
	if(*pDM_DigTable->pbP2pLinkInProgress)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("P2P in progress !!\n"));
		return;
	}
#endif

	if((pDM_Odm->SupportAbility & ODM_BB_ADAPTIVITY) && pDM_Odm->TxHangFlg == TRUE)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("Dynamic adjust threshold in progress !!\n"));
		return;
	}

	if(!bPaused && (!(pDM_Odm->SupportAbility & ODM_BB_DIG) || !(pDM_Odm->SupportAbility & ODM_BB_FA_CNT)))
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("Return: SupportAbility ODM_BB_DIG or ODM_BB_FA_CNT is disabled\n"));
		return;
	}
	
	switch(PauseType)
	{
		//1 Pause DIG
		case ODM_PAUSE_DIG:
			//2 Disable DIG
			ODM_CmnInfoUpdate(pDM_Odm, ODM_CMNINFO_ABILITY, pDM_Odm->SupportAbility & (~ODM_BB_DIG));
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("Pause DIG !!\n"));

			//2 Backup IGI value
			if(!bPaused)
			{
				pDM_DigTable->IGIBackup = pDM_DigTable->CurIGValue;
				bPaused = TRUE;
			}
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("Backup IGI  = 0x%x\n", pDM_DigTable->IGIBackup));

			//2 Write new IGI value
			ODM_Write_DIG(pDM_Odm, IGIValue);
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("Write new IGI = 0x%x\n", IGIValue));
			break;

		//1 Resume DIG
		case ODM_RESUME_DIG:
			if(bPaused)
			{
				//2 Write backup IGI value
				ODM_Write_DIG(pDM_Odm, pDM_DigTable->IGIBackup);
				bPaused = FALSE;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("Write original IGI = 0x%x\n", pDM_DigTable->IGIBackup));

				//2 Enable DIG
				ODM_CmnInfoUpdate(pDM_Odm, ODM_CMNINFO_ABILITY, pDM_Odm->SupportAbility | ODM_BB_DIG);	
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("Resume DIG !!\n"));
			}
			break;

		default:
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("Wrong  type !!\n"));
			break;
	}
}

BOOLEAN 
odm_DigAbort(
	IN		PVOID					pDM_VOID
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
// This should be moved out of OUTSRC
	PADAPTER		pAdapter	= pDM_Odm->Adapter;
	pRXHP_T			pRX_HP_Table  = &pDM_Odm->DM_RXHP_Table;
	
#if OS_WIN_FROM_WIN7(OS_VERSION)
	if(IsAPModeExist( pAdapter) && pAdapter->bInHctTest)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG() Return: Is AP mode or In HCT Test \n"));
	    	return	TRUE;
	}
#endif

	if(pRX_HP_Table->RXHP_flag == 1)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG() Return: In RXHP Operation \n"));
		return	TRUE;	
	}

	return	FALSE;
#else	// For Other team any special case for DIG?
	return	FALSE;
#endif
	

}

VOID
odm_DIGbyRSSI_LPS(
	IN		PVOID					pDM_VOID
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	//PADAPTER					pAdapter =pDM_Odm->Adapter;
	//pDIG_T						pDM_DigTable = &pDM_Odm->DM_DigTable;
	
	PFALSE_ALARM_STATISTICS		pFalseAlmCnt = &pDM_Odm->FalseAlmCnt;

#if 0		//and 2.3.5 coding rule
	struct mlme_priv	*pmlmepriv = &(pAdapter->mlmepriv);
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);	
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
#endif 

	u1Byte	RSSI_Lower=DM_DIG_MIN_NIC;   //0x1E or 0x1C
	u1Byte	CurrentIGI=pDM_Odm->RSSI_Min;

	CurrentIGI=CurrentIGI+RSSI_OFFSET_DIG;


	//ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG_LPS, ODM_DBG_LOUD, ("odm_DIG()==>\n"));

	// Using FW PS mode to make IGI

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("---Neil---odm_DIG is in LPS mode\n"));
	//Adjust by  FA in LPS MODE
	if(pFalseAlmCnt->Cnt_all> DM_DIG_FA_TH2_LPS)
		CurrentIGI = CurrentIGI+2;
	else if (pFalseAlmCnt->Cnt_all > DM_DIG_FA_TH1_LPS)
		CurrentIGI = CurrentIGI+1;
	else if(pFalseAlmCnt->Cnt_all < DM_DIG_FA_TH0_LPS)
		CurrentIGI = CurrentIGI-1;	


	//Lower bound checking

	//RSSI Lower bound check
	if((pDM_Odm->RSSI_Min-10) > DM_DIG_MIN_NIC)
		RSSI_Lower =(pDM_Odm->RSSI_Min-10);
	else
		RSSI_Lower =DM_DIG_MIN_NIC;

	//Upper and Lower Bound checking
	 if(CurrentIGI > DM_DIG_MAX_NIC)
	 	CurrentIGI=DM_DIG_MAX_NIC;
	 else if(CurrentIGI < RSSI_Lower)
		CurrentIGI =RSSI_Lower;

	ODM_Write_DIG(pDM_Odm, CurrentIGI);//ODM_Write_DIG(pDM_Odm, pDM_DigTable->CurIGValue);

}

VOID
odm_DIGInit(
	IN		PVOID					pDM_VOID
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	pDIG_T	pDM_DigTable = &pDM_Odm->DM_DigTable;

	//pDM_DigTable->Dig_Enable_Flag = TRUE;
	//pDM_DigTable->Dig_Ext_Port_Stage = DIG_EXT_PORT_STAGE_MAX;	
	pDM_DigTable->CurIGValue = (u1Byte) ODM_GetBBReg(pDM_Odm, ODM_REG(IGI_A,pDM_Odm), ODM_BIT(IGI,pDM_Odm));
	//pDM_DigTable->PreIGValue = 0x0;
	//pDM_DigTable->CurSTAConnectState = pDM_DigTable->PreSTAConnectState = DIG_STA_DISCONNECT;
	//pDM_DigTable->CurMultiSTAConnectState = DIG_MultiSTA_DISCONNECT;
	pDM_DigTable->RssiLowThresh 	= DM_DIG_THRESH_LOW;
	pDM_DigTable->RssiHighThresh 	= DM_DIG_THRESH_HIGH;
	pDM_DigTable->FALowThresh	= DM_FALSEALARM_THRESH_LOW;
	pDM_DigTable->FAHighThresh	= DM_FALSEALARM_THRESH_HIGH;
	if(pDM_Odm->BoardType & (ODM_BOARD_EXT_PA|ODM_BOARD_EXT_LNA))
	{
		pDM_DigTable->rx_gain_range_max = DM_DIG_MAX_NIC;
		pDM_DigTable->rx_gain_range_min = DM_DIG_MIN_NIC;
	}
	else
	{
		pDM_DigTable->rx_gain_range_max = DM_DIG_MAX_NIC;
		pDM_DigTable->rx_gain_range_min = DM_DIG_MIN_NIC;
	}
	pDM_DigTable->BackoffVal = DM_DIG_BACKOFF_DEFAULT;
	pDM_DigTable->BackoffVal_range_max = DM_DIG_BACKOFF_MAX;
	pDM_DigTable->BackoffVal_range_min = DM_DIG_BACKOFF_MIN;
	pDM_DigTable->PreCCK_CCAThres = 0xFF;
	pDM_DigTable->CurCCK_CCAThres = 0x83;
	pDM_DigTable->ForbiddenIGI = DM_DIG_MIN_NIC;
	pDM_DigTable->LargeFAHit = 0;
	pDM_DigTable->Recover_cnt = 0;
	pDM_DigTable->DIG_Dynamic_MIN_0 = DM_DIG_MIN_NIC;
	pDM_DigTable->DIG_Dynamic_MIN_1 = DM_DIG_MIN_NIC;
	pDM_DigTable->bMediaConnect_0 = FALSE;
	pDM_DigTable->bMediaConnect_1 = FALSE;
	
	//To Initialize pDM_Odm->bDMInitialGainEnable == FALSE to avoid DIG error
	pDM_Odm->bDMInitialGainEnable = TRUE;

	//To Initi BT30 IGI
	pDM_DigTable->BT30_CurIGI=0x32;

}

VOID
odm_DigForBtHsMode(
	IN		PVOID					pDM_VOID
	)
{
	
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	pDIG_T					pDM_DigTable=&pDM_Odm->DM_DigTable;
	u1Byte					digForBtHs=0;
	u1Byte					digUpBound=0x5a;
	
	if(pDM_Odm->bBtConnectProcess)
	{
		if(pDM_Odm->SupportICType&(ODM_RTL8723A))
			digForBtHs = 0x28;
		else
			digForBtHs = 0x22;
	}
	else
	{
		//
		// Decide DIG value by BT HS RSSI.
		//
		digForBtHs = pDM_Odm->btHsRssi+4;
		
		//DIG Bound
		if(pDM_Odm->SupportICType&(ODM_RTL8723A))
			digUpBound = 0x3e;
		
		if(digForBtHs > digUpBound)
			digForBtHs = digUpBound;
		if(digForBtHs < 0x1c)
			digForBtHs = 0x1c;

		// update Current IGI
		pDM_DigTable->BT30_CurIGI = digForBtHs;
	}
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DigForBtHsMode() : set DigValue=0x%x\n", digForBtHs));
#endif
}

VOID 
odm_DIG(
	IN		PVOID					pDM_VOID
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	pDIG_T						pDM_DigTable = &pDM_Odm->DM_DigTable;
	PFALSE_ALARM_STATISTICS		pFalseAlmCnt = &pDM_Odm->FalseAlmCnt;
	pRXHP_T						pRX_HP_Table  = &pDM_Odm->DM_RXHP_Table;
	u1Byte						DIG_Dynamic_MIN;
	u1Byte						DIG_MaxOfMin;
	BOOLEAN						FirstConnect, FirstDisConnect;
	u1Byte						dm_dig_max, dm_dig_min, offset;
	u1Byte						CurrentIGI = pDM_DigTable->CurIGValue;
	//u1Byte						Adap_IGI_Upper = pDM_Odm->IGI_target + 30 + (u1Byte) pDM_Odm->TH_L2H_ini -(u1Byte) pDM_Odm->TH_EDCCA_HL_diff;
	u1Byte						Adap_IGI_Upper =0;
	
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
// This should be moved out of OUTSRC
	PADAPTER		pAdapter	= pDM_Odm->Adapter;
#if OS_WIN_FROM_WIN7(OS_VERSION)
	if(IsAPModeExist( pAdapter) && pAdapter->bInHctTest)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG() Return: Is AP mode or In HCT Test \n"));
		return;
	}
#endif
//
//	if (pDM_Odm->SupportICType==ODM_RTL8723B)
//		return;
//
	if(pDM_Odm->adaptivity_flag == TRUE)
		Adap_IGI_Upper = pDM_Odm->Adaptivity_IGI_upper;
	else
		Adap_IGI_Upper = pDM_Odm->IGI_target + 30 + (u1Byte) pDM_Odm->TH_L2H_ini -(u1Byte) pDM_Odm->TH_EDCCA_HL_diff;
	
	if(pDM_Odm->bBtHsOperation)
	{
		odm_DigForBtHsMode(pDM_Odm);
	}
	
	if(!(pDM_Odm->SupportICType &(ODM_RTL8723A|ODM_RTL8188E)))
	{
		if(pRX_HP_Table->RXHP_flag == 1)
		{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG() Return: In RXHP Operation \n"));
			return;	
		}
	}	
#endif
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
#ifdef CONFIG_SPECIAL_SETTING_FOR_FUNAI_TV	
	if((pDM_Odm->bLinked) && (pDM_Odm->Adapter->registrypriv.force_igi !=0))
	{	
		printk("pDM_Odm->RSSI_Min=%d \n",pDM_Odm->RSSI_Min);
		ODM_Write_DIG(pDM_Odm,pDM_Odm->Adapter->registrypriv.force_igi);
		return;
	}
#endif
#endif
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	prtl8192cd_priv	priv			= pDM_Odm->priv;	
	if (!((priv->up_time > 5) && (priv->up_time % 2)) )
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG() Return: Not In DIG Operation Period \n"));
		return;
	}
#endif

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG()==>\n"));
	//if(!(pDM_Odm->SupportAbility & (ODM_BB_DIG|ODM_BB_FA_CNT)))
	if((!(pDM_Odm->SupportAbility&ODM_BB_DIG)) ||(!(pDM_Odm->SupportAbility&ODM_BB_FA_CNT)))
	{
#if 0	
		if(pDM_Odm->SupportPlatform & (ODM_AP|ODM_ADSL))
		{
			if ((pDM_Odm->SupportICType == ODM_RTL8192C) && (pDM_Odm->ExtLNA == 1))
				CurrentIGI = 0x30; //pDM_DigTable->CurIGValue  = 0x30;
			else
				CurrentIGI = 0x20; //pDM_DigTable->CurIGValue  = 0x20;
			ODM_Write_DIG(pDM_Odm, CurrentIGI);//ODM_Write_DIG(pDM_Odm, pDM_DigTable->CurIGValue);
		}
#endif		
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG() Return: SupportAbility ODM_BB_DIG or ODM_BB_FA_CNT is disabled\n"));
		return;
	}
		
	if(*(pDM_Odm->pbScanInProcess))
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG() Return: In Scan Progress \n"));
	    	return;
	}

	//add by Neil Chen to avoid PSD is processing
	if(pDM_Odm->SupportICType==ODM_RTL8723A)
	{
	        if(pDM_Odm->bDMInitialGainEnable == FALSE)
	        {
		        ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG() Return: PSD is Processing \n"));
		        return;
	        }
	}
		
	if(pDM_Odm->SupportICType == ODM_RTL8192D)
	{
		if(*(pDM_Odm->pMacPhyMode) == ODM_DMSP)
		{
			if(*(pDM_Odm->pbMasterOfDMSP))
			{
				DIG_Dynamic_MIN = pDM_DigTable->DIG_Dynamic_MIN_0;
				FirstConnect = (pDM_Odm->bLinked) && (pDM_DigTable->bMediaConnect_0 == FALSE);	
				FirstDisConnect = (!pDM_Odm->bLinked) && (pDM_DigTable->bMediaConnect_0 == TRUE);
			}
			else
			{
				DIG_Dynamic_MIN = pDM_DigTable->DIG_Dynamic_MIN_1;
				FirstConnect = (pDM_Odm->bLinked) && (pDM_DigTable->bMediaConnect_1 == FALSE);	
				FirstDisConnect = (!pDM_Odm->bLinked) && (pDM_DigTable->bMediaConnect_1 == TRUE);
			}
		}
		else
		{
			if(*(pDM_Odm->pBandType) == ODM_BAND_5G)
			{
				DIG_Dynamic_MIN = pDM_DigTable->DIG_Dynamic_MIN_0;
				FirstConnect = (pDM_Odm->bLinked) && (pDM_DigTable->bMediaConnect_0 == FALSE);
				FirstDisConnect = (!pDM_Odm->bLinked) && (pDM_DigTable->bMediaConnect_0 == TRUE);
			}
			else
			{
				DIG_Dynamic_MIN = pDM_DigTable->DIG_Dynamic_MIN_1;
				FirstConnect = (pDM_Odm->bLinked) && (pDM_DigTable->bMediaConnect_1 == FALSE);
				FirstDisConnect = (!pDM_Odm->bLinked) && (pDM_DigTable->bMediaConnect_1 == TRUE);
			}
		}
	}
	else
	{	
		DIG_Dynamic_MIN = pDM_DigTable->DIG_Dynamic_MIN_0;
		FirstConnect = (pDM_Odm->bLinked) && (pDM_DigTable->bMediaConnect_0 == FALSE);
		FirstDisConnect = (!pDM_Odm->bLinked) && (pDM_DigTable->bMediaConnect_0 == TRUE);
	}
	
	//1 Boundary Decision
	if(pDM_Odm->SupportICType & (ODM_RTL8192C) &&(pDM_Odm->BoardType & (ODM_BOARD_EXT_LNA | ODM_BOARD_EXT_PA)))
	{
		if(pDM_Odm->SupportPlatform & (ODM_AP|ODM_ADSL))
		{

			dm_dig_max = DM_DIG_MAX_AP_HP;
			dm_dig_min = DM_DIG_MIN_AP_HP;
		}
		else
		{
			dm_dig_max = DM_DIG_MAX_NIC_HP;
			dm_dig_min = DM_DIG_MIN_NIC_HP;
		}
		DIG_MaxOfMin = DM_DIG_MAX_AP_HP;
	}
	else
	{
		if(pDM_Odm->SupportPlatform & (ODM_AP|ODM_ADSL))
		{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
#ifdef DFS
			if (!priv->pmib->dot11DFSEntry.disable_DFS &&
				(OPMODE & WIFI_AP_STATE) &&
				(((pDM_Odm->ControlChannel >= 52) &&
				(pDM_Odm->ControlChannel <= 64)) ||
				((pDM_Odm->ControlChannel >= 100) &&
				(pDM_Odm->ControlChannel <= 140))))
				dm_dig_max = 0x24;
			else
#endif
			if (priv->pmib->dot11RFEntry.tx2path) {
				if (*(pDM_Odm->pWirelessMode) == ODM_WM_B)//(priv->pmib->dot11BssType.net_work_type == WIRELESS_11B)
					dm_dig_max = 0x2A;
				else
					dm_dig_max = 0x32;
			}
			else
#endif				
			dm_dig_max = DM_DIG_MAX_AP;
			dm_dig_min = DM_DIG_MIN_AP;
			DIG_MaxOfMin = dm_dig_max;
		}
		else
		{
			if((pDM_Odm->SupportICType >= ODM_RTL8188E) && (pDM_Odm->SupportPlatform & (ODM_WIN|ODM_CE)))
				dm_dig_max = 0x5A;
			else
				dm_dig_max = DM_DIG_MAX_NIC;
			
			if(pDM_Odm->SupportICType != ODM_RTL8821)
				dm_dig_min = DM_DIG_MIN_NIC;
			else
				dm_dig_min = 0x1C;

			DIG_MaxOfMin = DM_DIG_MAX_AP;
		}
	}

	if(0 < *pDM_Odm->pu1ForcedIgiLb)
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): force IGI lb to: %u\n", *pDM_Odm->pu1ForcedIgiLb));
		dm_dig_min = *pDM_Odm->pu1ForcedIgiLb;
		dm_dig_max = (dm_dig_min <= dm_dig_max) ? (dm_dig_max) : (dm_dig_min + 1);
	}
		
	if(pDM_Odm->bLinked)
	{
		if(pDM_Odm->SupportICType&(ODM_RTL8723A))//|ODM_RTL8821
		{
			//2 Upper Bound
			if(( pDM_Odm->RSSI_Min + 10) > DM_DIG_MAX_NIC )
				pDM_DigTable->rx_gain_range_max = DM_DIG_MAX_NIC;
			else if(( pDM_Odm->RSSI_Min + 10) < DM_DIG_MIN_NIC )
				pDM_DigTable->rx_gain_range_max = DM_DIG_MIN_NIC;
			else
				pDM_DigTable->rx_gain_range_max = pDM_Odm->RSSI_Min + 10;

			//BT is Concurrent

			if(pDM_Odm->bBtLimitedDig)
			{
				if(pDM_Odm->RSSI_Min>10)
				{
					if((pDM_Odm->RSSI_Min - 10) > DM_DIG_MAX_NIC)
						DIG_Dynamic_MIN = DM_DIG_MAX_NIC;
					else if((pDM_Odm->RSSI_Min - 10) < DM_DIG_MIN_NIC)
						DIG_Dynamic_MIN = DM_DIG_MIN_NIC;
					else
						DIG_Dynamic_MIN = pDM_Odm->RSSI_Min - 10;
				}
				else
					DIG_Dynamic_MIN=DM_DIG_MIN_NIC;
			}
			else
			{
				if((pDM_Odm->RSSI_Min + 20) > dm_dig_max )
					pDM_DigTable->rx_gain_range_max = dm_dig_max;
				else if((pDM_Odm->RSSI_Min + 20) < dm_dig_min )
					pDM_DigTable->rx_gain_range_max = dm_dig_min;
				else
					pDM_DigTable->rx_gain_range_max = pDM_Odm->RSSI_Min + 20;
				
			}
		}
		else
		{
			if((pDM_Odm->SupportICType & (ODM_RTL8192E|ODM_RTL8723B|ODM_RTL8812|ODM_RTL8821)) && (pDM_Odm->bBtLimitedDig==1)){				
				//2 Modify DIG upper bound for 92E, 8723B, 8821 & 8812 BT
				if((pDM_Odm->RSSI_Min + 10) > dm_dig_max )
					pDM_DigTable->rx_gain_range_max = dm_dig_max;
				else if((pDM_Odm->RSSI_Min + 10) < dm_dig_min )
					pDM_DigTable->rx_gain_range_max = dm_dig_min;
				else
					pDM_DigTable->rx_gain_range_max = pDM_Odm->RSSI_Min + 10;
			}
			else{
		
			//2 Modify DIG upper bound
			//2013.03.19 Luke: Modified upper bound for Netgear rental house test
			if(pDM_Odm->SupportICType != ODM_RTL8821 && pDM_Odm->SupportICType != ODM_RTL8192E)
				offset = 20;
			else
				offset = 10;
			
			if((pDM_Odm->RSSI_Min + offset) > dm_dig_max )
				pDM_DigTable->rx_gain_range_max = dm_dig_max;
				else if((pDM_Odm->RSSI_Min + offset) < dm_dig_min )
					pDM_DigTable->rx_gain_range_max = dm_dig_min;
			else
				pDM_DigTable->rx_gain_range_max = pDM_Odm->RSSI_Min + offset;
			
			}

			//2 Modify DIG lower bound
		//
		//	if((pFalseAlmCnt->Cnt_all > 500)&&(DIG_Dynamic_MIN < 0x25))
		//		DIG_Dynamic_MIN++;
		//	else if(((pFalseAlmCnt->Cnt_all < 500)||(pDM_Odm->RSSI_Min < 8))&&(DIG_Dynamic_MIN > dm_dig_min))
		//		DIG_Dynamic_MIN--;
		//
			if(pDM_Odm->bOneEntryOnly)
			{	
				if(pDM_Odm->SupportICType != ODM_RTL8723B)
					offset = 0;
				else
					offset = 12;
				
				if(pDM_Odm->RSSI_Min - offset < dm_dig_min)
					DIG_Dynamic_MIN = dm_dig_min;
				else if (pDM_Odm->RSSI_Min - offset > DIG_MaxOfMin)
					DIG_Dynamic_MIN = DIG_MaxOfMin;
				else
					DIG_Dynamic_MIN = pDM_Odm->RSSI_Min - offset;

				ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG() : bOneEntryOnly=TRUE,  DIG_Dynamic_MIN=0x%x\n",DIG_Dynamic_MIN));
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG() : pDM_Odm->RSSI_Min=%d",pDM_Odm->RSSI_Min));
			}
			//1 Lower Bound for 88E AntDiv
#if (defined(CONFIG_HW_ANTENNA_DIVERSITY))
			else if( (pDM_Odm->SupportICType & ODM_ANTDIV_SUPPORT) &&(pDM_Odm->SupportAbility & ODM_BB_ANT_DIV) )
			//else if((pDM_Odm->SupportICType == ODM_RTL8188E)&&(pDM_Odm->SupportAbility & ODM_BB_ANT_DIV))
			{
				if((pDM_Odm->AntDivType == CG_TRX_HW_ANTDIV)||(pDM_Odm->AntDivType == CGCS_RX_HW_ANTDIV) ||pDM_Odm->AntDivType == S0S1_SW_ANTDIV)
				{
					DIG_Dynamic_MIN = (u1Byte) pDM_DigTable->AntDiv_RSSI_max;
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("odm_DIG(): pDM_DigTable->AntDiv_RSSI_max=%d \n",pDM_DigTable->AntDiv_RSSI_max));
				}
			}
#endif
			else
			{
				DIG_Dynamic_MIN=dm_dig_min;
			}
		}
	}
	else
	{
		pDM_DigTable->rx_gain_range_max = dm_dig_max;
		DIG_Dynamic_MIN = dm_dig_min;
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG() : No Link\n"));
	}
	
	//1 Modify DIG lower bound, deal with abnorally large false alarm
	if(pFalseAlmCnt->Cnt_all > 10000)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("dm_DIG(): Abnornally false alarm case. \n"));

		if(pDM_DigTable->LargeFAHit != 3)
		        pDM_DigTable->LargeFAHit++;
		if(pDM_DigTable->ForbiddenIGI < CurrentIGI)//if(pDM_DigTable->ForbiddenIGI < pDM_DigTable->CurIGValue)
		{
			pDM_DigTable->ForbiddenIGI = (u1Byte)CurrentIGI;//pDM_DigTable->ForbiddenIGI = pDM_DigTable->CurIGValue;
			pDM_DigTable->LargeFAHit = 1;
		}

		if(pDM_DigTable->LargeFAHit >= 3)
		{
			if((pDM_DigTable->ForbiddenIGI+1) >pDM_DigTable->rx_gain_range_max)
				pDM_DigTable->rx_gain_range_min = pDM_DigTable->rx_gain_range_max;
			else
				pDM_DigTable->rx_gain_range_min = (pDM_DigTable->ForbiddenIGI + 1);
			pDM_DigTable->Recover_cnt = 3600; //3600=2hr
		}

	}
	else
	{
		//Recovery mechanism for IGI lower bound
		if(pDM_DigTable->Recover_cnt != 0)
			pDM_DigTable->Recover_cnt --;
		else
		{
			if(pDM_DigTable->LargeFAHit < 3)
			{
				if((pDM_DigTable->ForbiddenIGI -1) < DIG_Dynamic_MIN) //DM_DIG_MIN)
				{
					pDM_DigTable->ForbiddenIGI = DIG_Dynamic_MIN; //DM_DIG_MIN;
					pDM_DigTable->rx_gain_range_min = DIG_Dynamic_MIN; //DM_DIG_MIN;
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Normal Case: At Lower Bound\n"));
				}
				else
				{
					pDM_DigTable->ForbiddenIGI --;
					pDM_DigTable->rx_gain_range_min = (pDM_DigTable->ForbiddenIGI + 1);
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Normal Case: Approach Lower Bound\n"));
				}
			}
			else
			{
				pDM_DigTable->LargeFAHit = 0;
			}
		}
	}
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): pDM_DigTable->LargeFAHit=%d\n",pDM_DigTable->LargeFAHit));

	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	if(IS_STA_VALID(pDM_Odm->pODM_StaInfo[0])) //STA mode is linked to AP
		pDM_Odm->bsta_state = _TRUE;
	#endif	
	
	if((pDM_Odm->SupportPlatform&(ODM_WIN|ODM_CE))&&(pDM_Odm->PhyDbgInfo.NumQryBeaconPkt < 2) && (pDM_Odm->bsta_state) )
	{		
			pDM_DigTable->rx_gain_range_min = dm_dig_min;
	}
	
	if(pDM_DigTable->rx_gain_range_min > pDM_DigTable->rx_gain_range_max)
		pDM_DigTable->rx_gain_range_min = pDM_DigTable->rx_gain_range_max;

	//1 Adjust initial gain by false alarm
	if(pDM_Odm->bLinked)
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): DIG AfterLink\n"));
		if(FirstConnect)
		{
			if(pDM_Odm->RSSI_Min <= DIG_MaxOfMin)
				CurrentIGI = pDM_Odm->RSSI_Min;
			else
				CurrentIGI = DIG_MaxOfMin;
			ODM_RT_TRACE(pDM_Odm,	ODM_COMP_DIG, ODM_DBG_LOUD, ("DIG: First Connect\n"));

			ODM_ConfigBBWithHeaderFile(pDM_Odm, CONFIG_BB_AGC_TAB_DIFF);		
		}
		else
		{
			if(pDM_Odm->SupportICType == ODM_RTL8192D)
			{
				if(pFalseAlmCnt->Cnt_all > DM_DIG_FA_TH2_92D)
					CurrentIGI = CurrentIGI + 4;//pDM_DigTable->CurIGValue = pDM_DigTable->PreIGValue+2;
				else if (pFalseAlmCnt->Cnt_all > DM_DIG_FA_TH1_92D)
					CurrentIGI = CurrentIGI + 2; //pDM_DigTable->CurIGValue = pDM_DigTable->PreIGValue+1;
				else if(pFalseAlmCnt->Cnt_all < DM_DIG_FA_TH0_92D)
					CurrentIGI = CurrentIGI - 2;//pDM_DigTable->CurIGValue =pDM_DigTable->PreIGValue-1;	
			}
			else
			{
				//FA for Combo IC--NeilChen--2012--09--28 
				if(pDM_Odm->SupportICType == ODM_RTL8723A)
				{
	     				//WLAN and BT ConCurrent
					if(pDM_Odm->bBtLimitedDig)
					{
						if(pFalseAlmCnt->Cnt_all > 0x300)
							CurrentIGI = CurrentIGI + 4;
						else if (pFalseAlmCnt->Cnt_all > 0x250)
							CurrentIGI = CurrentIGI + 2;
						else if(pFalseAlmCnt->Cnt_all < DM_DIG_FA_TH0)
							CurrentIGI = CurrentIGI -2;
					}
					else //Not Concurrent
					{
						if(pFalseAlmCnt->Cnt_all > DM_DIG_FA_TH2)
							CurrentIGI = CurrentIGI + 4;//pDM_DigTable->CurIGValue = pDM_DigTable->PreIGValue+2;
						else if (pFalseAlmCnt->Cnt_all > DM_DIG_FA_TH1)
							CurrentIGI = CurrentIGI + 2;//pDM_DigTable->CurIGValue = pDM_DigTable->PreIGValue+1;
						else if(pFalseAlmCnt->Cnt_all < DM_DIG_FA_TH0)
							CurrentIGI = CurrentIGI - 2;//pDM_DigTable->CurIGValue =pDM_DigTable->PreIGValue-1;	
					}
				}
				else
				{
					if(pFalseAlmCnt->Cnt_all > DM_DIG_FA_TH2)
						CurrentIGI = CurrentIGI + 4;//pDM_DigTable->CurIGValue = pDM_DigTable->PreIGValue+2;
					else if (pFalseAlmCnt->Cnt_all > DM_DIG_FA_TH1)
						CurrentIGI = CurrentIGI + 2;//pDM_DigTable->CurIGValue = pDM_DigTable->PreIGValue+1;
					else if(pFalseAlmCnt->Cnt_all < DM_DIG_FA_TH0)
						CurrentIGI = CurrentIGI - 2;//pDM_DigTable->CurIGValue =pDM_DigTable->PreIGValue-1;	

					#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
					if(IS_STA_VALID(pDM_Odm->pODM_StaInfo[0])) //STA mode is linked to AP
						pDM_Odm->bsta_state = _TRUE;
					#endif

					if((pDM_Odm->SupportPlatform&(ODM_WIN|ODM_CE))&&(pDM_Odm->PhyDbgInfo.NumQryBeaconPkt < 2)
						&&(pFalseAlmCnt->Cnt_all < DM_DIG_FA_TH1) && (pDM_Odm->bsta_state))
					{						
						CurrentIGI = pDM_DigTable->rx_gain_range_min;
						ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Beacon is less than 10 and FA is less than 768, IGI GOES TO 0x1E!!!!!!!!!!!!\n"));
					}
					//{
					//	u2Byte value16;
					//	value16 = (u2Byte) ODM_GetBBReg(pDM_Odm, 0x664, bMaskLWord);
					//	ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): NumQryBeaconPkt = %d, OFDM_OK_Cnt = %d\n", 
					//		pDM_Odm->PhyDbgInfo.NumQryBeaconPkt, value16));
					//}
				}
			}
		}
	}	
	else
	{
		//CurrentIGI = pDM_DigTable->rx_gain_range_min;//pDM_DigTable->CurIGValue = pDM_DigTable->rx_gain_range_min
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): DIG BeforeLink\n"));
		if(FirstDisConnect)
		{
				CurrentIGI = pDM_DigTable->rx_gain_range_min;
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): First DisConnect \n"));
		}
		else
		{
			//2012.03.30 LukeLee: enable DIG before link but with very high thresholds
	             if(pFalseAlmCnt->Cnt_all > 10000)
				CurrentIGI = CurrentIGI + 4;
			else if (pFalseAlmCnt->Cnt_all > 8000)
				CurrentIGI = CurrentIGI + 2;
			else if(pFalseAlmCnt->Cnt_all < 500)
				CurrentIGI = CurrentIGI - 2;
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): England DIG \n"));
		}
	}
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): DIG End Adjust IGI\n"));
	//1 Check initial gain by upper/lower bound

	if(CurrentIGI > pDM_DigTable->rx_gain_range_max)
		CurrentIGI = pDM_DigTable->rx_gain_range_max;
	if(CurrentIGI < pDM_DigTable->rx_gain_range_min)
		CurrentIGI = pDM_DigTable->rx_gain_range_min;

	if(pDM_Odm->SupportAbility & ODM_BB_ADAPTIVITY)
	{
		if(CurrentIGI > Adap_IGI_Upper)
			CurrentIGI = Adap_IGI_Upper;

		if(pDM_Odm->IGI_LowerBound != 0)
		{
			if(CurrentIGI < pDM_Odm->IGI_LowerBound)
				CurrentIGI = pDM_Odm->IGI_LowerBound;
		}
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): pDM_Odm->IGI_LowerBound = %d\n", pDM_Odm->IGI_LowerBound));
	}
	
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): rx_gain_range_max=0x%x, rx_gain_range_min=0x%x\n", 
		pDM_DigTable->rx_gain_range_max, pDM_DigTable->rx_gain_range_min));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): TotalFA=%d\n", pFalseAlmCnt->Cnt_all));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): CurIGValue=0x%x\n", CurrentIGI));

	//2 High power RSSI threshold
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)	
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pDM_Odm->Adapter);
	//PMGNT_INFO			pMgntInfo	= &(pAdapter->MgntInfo);	
	// for LC issue to dymanic modify DIG lower bound----------LC Mocca Issue
	u8Byte			curTxOkCnt=0, curRxOkCnt=0;
	static u8Byte		lastTxOkCnt=0, lastRxOkCnt=0;

	//u8Byte			OKCntAll=0;
	//static u8Byte		TXByteCnt_A=0, TXByteCnt_B=0, RXByteCnt_A=0, RXByteCnt_B=0;
	//u8Byte			CurByteCnt=0, PreByteCnt=0;
	
	curTxOkCnt = pAdapter->TxStats.NumTxBytesUnicast - lastTxOkCnt;
	curRxOkCnt =pAdapter->RxStats.NumRxBytesUnicast - lastRxOkCnt;
	lastTxOkCnt = pAdapter->TxStats.NumTxBytesUnicast;
	lastRxOkCnt = pAdapter->RxStats.NumRxBytesUnicast;
	//----------------------------------------------------------end for LC Mocca issue
	if((pDM_Odm->SupportICType == ODM_RTL8723A)&& (pHalData->UndecoratedSmoothedPWDB > DM_DIG_HIGH_PWR_THRESHOLD))
	{
		// High power IGI lower bound
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): UndecoratedSmoothedPWDB(%#x)\n", pHalData->UndecoratedSmoothedPWDB));
		if(CurrentIGI < DM_DIG_HIGH_PWR_IGI_LOWER_BOUND)
		{
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): CurIGValue(%#x)\n", pDM_DigTable->CurIGValue));
			//pDM_DigTable->CurIGValue = DM_DIG_HIGH_PWR_IGI_LOWER_BOUND;
			CurrentIGI=DM_DIG_HIGH_PWR_IGI_LOWER_BOUND;
		}
	}
	if((pDM_Odm->SupportICType & ODM_RTL8723A) && 
			IS_WIRELESS_MODE_G(pAdapter))
		{
			if(pHalData->UndecoratedSmoothedPWDB > 0x28)
			{
				if(CurrentIGI < DM_DIG_Gmode_HIGH_PWR_IGI_LOWER_BOUND)
				{
			 		//pDM_DigTable->CurIGValue = DM_DIG_Gmode_HIGH_PWR_IGI_LOWER_BOUND;
					CurrentIGI = DM_DIG_Gmode_HIGH_PWR_IGI_LOWER_BOUND;
				}	
			} 
		}	
#if 0
	if((pDM_Odm->SupportICType & ODM_RTL8723A)&&(pMgntInfo->CustomerID = RT_CID_LENOVO_CHINA))
	{
		OKCntAll = (curTxOkCnt+curRxOkCnt);
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): CurIGValue(%#x)\n", CurrentIGI));
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): UndecoratedSmoothedPWDB(%#x)\n", pHalData->UndecoratedSmoothedPWDB));
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): OKCntAll(%#x)\n", OKCntAll));
		//8723AS_VAU
		if(pDM_Odm->SupportInterface==ODM_ITRF_USB)
		{
			if(pHalData->UndecoratedSmoothedPWDB < 12)
			{
				if(CurrentIGI > DM_DIG_MIN_NIC)
				{
					if(OKCntAll >= 1500000) 		 // >=6Mbps
						CurrentIGI=0x1B;
					else if(OKCntAll >= 1000000) 	 //4Mbps
						CurrentIGI=0x1A;
					else if(OKCntAll >= 500000)		 //2Mbps
						CurrentIGI=0x19;
					else if(OKCntAll >= 250000)		//1Mbps
						CurrentIGI=0x18;
					else
					{
						CurrentIGI=0x17;		//SCAN mode
					}
				}
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("Modify---->CurIGValue(%#x)\n", CurrentIGI));	
			}
		}
	}	
#endif	
}
#endif
		
#if (RTL8192D_SUPPORT==1) 
	if(pDM_Odm->SupportICType == ODM_RTL8192D)
	{
		//sherry  delete DualMacSmartConncurrent 20110517
		if(*(pDM_Odm->pMacPhyMode) == ODM_DMSP)
		{
			ODM_Write_DIG_DMSP(pDM_Odm, (u1Byte)CurrentIGI);//ODM_Write_DIG_DMSP(pDM_Odm, pDM_DigTable->CurIGValue);
			if(*(pDM_Odm->pbMasterOfDMSP))
			{
				pDM_DigTable->bMediaConnect_0 = pDM_Odm->bLinked;
				pDM_DigTable->DIG_Dynamic_MIN_0 = DIG_Dynamic_MIN;
			}
			else
			{
				pDM_DigTable->bMediaConnect_1 = pDM_Odm->bLinked;
				pDM_DigTable->DIG_Dynamic_MIN_1 = DIG_Dynamic_MIN;
			}
		}
		else
		{
			ODM_Write_DIG(pDM_Odm, CurrentIGI);//ODM_Write_DIG(pDM_Odm, pDM_DigTable->CurIGValue);
			if(*(pDM_Odm->pBandType) == ODM_BAND_5G)
			{
				pDM_DigTable->bMediaConnect_0 = pDM_Odm->bLinked;
				pDM_DigTable->DIG_Dynamic_MIN_0 = DIG_Dynamic_MIN;
			}
			else
			{
				pDM_DigTable->bMediaConnect_1 = pDM_Odm->bLinked;
				pDM_DigTable->DIG_Dynamic_MIN_1 = DIG_Dynamic_MIN;
			}
		}
	}
	else
#endif
	{
		if(pDM_Odm->bBtHsOperation)
		{
			if(pDM_Odm->bLinked)
			{
				if(pDM_DigTable->BT30_CurIGI > (CurrentIGI))
				{
					ODM_Write_DIG(pDM_Odm, CurrentIGI);
					
				}	
				else
				{
					ODM_Write_DIG(pDM_Odm, pDM_DigTable->BT30_CurIGI);
				}
				pDM_DigTable->bMediaConnect_0 = pDM_Odm->bLinked;
				pDM_DigTable->DIG_Dynamic_MIN_0 = DIG_Dynamic_MIN;
			}
			else
			{
				if(pDM_Odm->bLinkInProcess)
				{
					ODM_Write_DIG(pDM_Odm, 0x1c);
				}
				else if(pDM_Odm->bBtConnectProcess)
				{
					ODM_Write_DIG(pDM_Odm, 0x28);
				}
				else
				{
					ODM_Write_DIG(pDM_Odm, pDM_DigTable->BT30_CurIGI);//ODM_Write_DIG(pDM_Odm, pDM_DigTable->CurIGValue);	
				}
			}
		}	
		else		// BT is not using
		{
			ODM_Write_DIG(pDM_Odm, CurrentIGI);//ODM_Write_DIG(pDM_Odm, pDM_DigTable->CurIGValue);
			pDM_DigTable->bMediaConnect_0 = pDM_Odm->bLinked;
			pDM_DigTable->DIG_Dynamic_MIN_0 = DIG_Dynamic_MIN;
		}
	}
}



//3============================================================
//3 FASLE ALARM CHECK
//3============================================================

VOID 
odm_FalseAlarmCounterStatistics(
	IN		PVOID					pDM_VOID
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u4Byte ret_value;
	PFALSE_ALARM_STATISTICS FalseAlmCnt = &(pDM_Odm->FalseAlmCnt);

#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	prtl8192cd_priv priv		= pDM_Odm->priv;
	if( (priv->auto_channel != 0) && (priv->auto_channel != 2) )
		return;
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	if((pDM_Odm->SupportICType == ODM_RTL8192D) &&
		(*(pDM_Odm->pMacPhyMode)==ODM_DMSP)&&    ////modify by Guo.Mingzhi 2011-12-29
		(!(*(pDM_Odm->pbMasterOfDMSP))))
	{
		odm_FalseAlarmCounterStatistics_ForSlaveOfDMSP(pDM_Odm);
		return;
	}
#endif		

	if(!(pDM_Odm->SupportAbility & ODM_BB_FA_CNT))
		return;

	if(pDM_Odm->SupportICType & ODM_IC_11N_SERIES)
	{

	//hold ofdm counter
		ODM_SetBBReg(pDM_Odm, ODM_REG_OFDM_FA_HOLDC_11N, BIT31, 1); //hold page C counter
		ODM_SetBBReg(pDM_Odm, ODM_REG_OFDM_FA_RSTD_11N, BIT31, 1); //hold page D counter
	
		ret_value = ODM_GetBBReg(pDM_Odm, ODM_REG_OFDM_FA_TYPE1_11N, bMaskDWord);
		FalseAlmCnt->Cnt_Fast_Fsync = (ret_value&0xffff);
		FalseAlmCnt->Cnt_SB_Search_fail = ((ret_value&0xffff0000)>>16);		
		ret_value = ODM_GetBBReg(pDM_Odm, ODM_REG_OFDM_FA_TYPE2_11N, bMaskDWord);
		FalseAlmCnt->Cnt_OFDM_CCA = (ret_value&0xffff); 
		FalseAlmCnt->Cnt_Parity_Fail = ((ret_value&0xffff0000)>>16);	
		ret_value = ODM_GetBBReg(pDM_Odm, ODM_REG_OFDM_FA_TYPE3_11N, bMaskDWord);
		FalseAlmCnt->Cnt_Rate_Illegal = (ret_value&0xffff);
		FalseAlmCnt->Cnt_Crc8_fail = ((ret_value&0xffff0000)>>16);
		ret_value = ODM_GetBBReg(pDM_Odm, ODM_REG_OFDM_FA_TYPE4_11N, bMaskDWord);
		FalseAlmCnt->Cnt_Mcs_fail = (ret_value&0xffff);

		FalseAlmCnt->Cnt_Ofdm_fail = 	FalseAlmCnt->Cnt_Parity_Fail + FalseAlmCnt->Cnt_Rate_Illegal +
									FalseAlmCnt->Cnt_Crc8_fail + FalseAlmCnt->Cnt_Mcs_fail +
									FalseAlmCnt->Cnt_Fast_Fsync + FalseAlmCnt->Cnt_SB_Search_fail;

#if (RTL8188E_SUPPORT==1)
		if((pDM_Odm->SupportICType == ODM_RTL8188E)||(pDM_Odm->SupportICType == ODM_RTL8192E))
		{
				ret_value = ODM_GetBBReg(pDM_Odm, ODM_REG_SC_CNT_11N, bMaskDWord);
			FalseAlmCnt->Cnt_BW_LSC = (ret_value&0xffff);
			FalseAlmCnt->Cnt_BW_USC = ((ret_value&0xffff0000)>>16);
		}
#endif

#if (RTL8192D_SUPPORT==1) 
		if(pDM_Odm->SupportICType == ODM_RTL8192D)
		{
			odm_GetCCKFalseAlarm_92D(pDM_Odm);
		}
		else
#endif
		{
			//hold cck counter
				ODM_SetBBReg(pDM_Odm, ODM_REG_CCK_FA_RST_11N, BIT12, 1); 
				ODM_SetBBReg(pDM_Odm, ODM_REG_CCK_FA_RST_11N, BIT14, 1); 
		
				ret_value = ODM_GetBBReg(pDM_Odm, ODM_REG_CCK_FA_LSB_11N, bMaskByte0);
			FalseAlmCnt->Cnt_Cck_fail = ret_value;
				ret_value = ODM_GetBBReg(pDM_Odm, ODM_REG_CCK_FA_MSB_11N, bMaskByte3);
			FalseAlmCnt->Cnt_Cck_fail +=  (ret_value& 0xff)<<8;

				ret_value = ODM_GetBBReg(pDM_Odm, ODM_REG_CCK_CCA_CNT_11N, bMaskDWord);
			FalseAlmCnt->Cnt_CCK_CCA = ((ret_value&0xFF)<<8) |((ret_value&0xFF00)>>8);
		}
		
		FalseAlmCnt->Cnt_all = (	FalseAlmCnt->Cnt_Fast_Fsync + 
							FalseAlmCnt->Cnt_SB_Search_fail +
							FalseAlmCnt->Cnt_Parity_Fail +
							FalseAlmCnt->Cnt_Rate_Illegal +
							FalseAlmCnt->Cnt_Crc8_fail +
							FalseAlmCnt->Cnt_Mcs_fail +
							FalseAlmCnt->Cnt_Cck_fail);	

		FalseAlmCnt->Cnt_CCA_all = FalseAlmCnt->Cnt_OFDM_CCA + FalseAlmCnt->Cnt_CCK_CCA;

#if (RTL8192C_SUPPORT==1)
		if(pDM_Odm->SupportICType == ODM_RTL8192C)
			odm_ResetFACounter_92C(pDM_Odm);
#endif

#if (RTL8192D_SUPPORT==1)
		if(pDM_Odm->SupportICType == ODM_RTL8192D)
			odm_ResetFACounter_92D(pDM_Odm);
#endif

		if(pDM_Odm->SupportICType >=ODM_RTL8723A)
		{
			//reset false alarm counter registers
				ODM_SetBBReg(pDM_Odm, ODM_REG_OFDM_FA_RSTC_11N, BIT31, 1);
				ODM_SetBBReg(pDM_Odm, ODM_REG_OFDM_FA_RSTC_11N, BIT31, 0);
				ODM_SetBBReg(pDM_Odm, ODM_REG_OFDM_FA_RSTD_11N, BIT27, 1);
				ODM_SetBBReg(pDM_Odm, ODM_REG_OFDM_FA_RSTD_11N, BIT27, 0);
			//update ofdm counter
				ODM_SetBBReg(pDM_Odm, ODM_REG_OFDM_FA_HOLDC_11N, BIT31, 0); //update page C counter
				ODM_SetBBReg(pDM_Odm, ODM_REG_OFDM_FA_RSTD_11N, BIT31, 0); //update page D counter

			//reset CCK CCA counter
				ODM_SetBBReg(pDM_Odm, ODM_REG_CCK_FA_RST_11N, BIT13|BIT12, 0); 
				ODM_SetBBReg(pDM_Odm, ODM_REG_CCK_FA_RST_11N, BIT13|BIT12, 2); 
			//reset CCK FA counter
				ODM_SetBBReg(pDM_Odm, ODM_REG_CCK_FA_RST_11N, BIT15|BIT14, 0); 
				ODM_SetBBReg(pDM_Odm, ODM_REG_CCK_FA_RST_11N, BIT15|BIT14, 2); 
		}
			
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_FA_CNT, ODM_DBG_LOUD, ("Enter odm_FalseAlarmCounterStatistics\n"));
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_FA_CNT, ODM_DBG_LOUD, ("Cnt_Fast_Fsync=%d, Cnt_SB_Search_fail=%d\n",
			FalseAlmCnt->Cnt_Fast_Fsync, FalseAlmCnt->Cnt_SB_Search_fail));
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_FA_CNT, ODM_DBG_LOUD, ("Cnt_Parity_Fail=%d, Cnt_Rate_Illegal=%d\n",
			FalseAlmCnt->Cnt_Parity_Fail, FalseAlmCnt->Cnt_Rate_Illegal));
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_FA_CNT, ODM_DBG_LOUD, ("Cnt_Crc8_fail=%d, Cnt_Mcs_fail=%d\n",
			FalseAlmCnt->Cnt_Crc8_fail, FalseAlmCnt->Cnt_Mcs_fail));
	}
	else if(pDM_Odm->SupportICType & ODM_IC_11AC_SERIES)
	{
		u4Byte CCKenable;
		//read OFDM FA counter
		FalseAlmCnt->Cnt_Ofdm_fail = ODM_GetBBReg(pDM_Odm, ODM_REG_OFDM_FA_11AC, bMaskLWord);
		FalseAlmCnt->Cnt_Cck_fail = ODM_GetBBReg(pDM_Odm, ODM_REG_CCK_FA_11AC, bMaskLWord);
		
		CCKenable =  ODM_GetBBReg(pDM_Odm, ODM_REG_BB_RX_PATH_11AC, BIT28);
		if(CCKenable)//if(*pDM_Odm->pBandType == ODM_BAND_2_4G)
			FalseAlmCnt->Cnt_all = FalseAlmCnt->Cnt_Ofdm_fail + FalseAlmCnt->Cnt_Cck_fail;
		else
			FalseAlmCnt->Cnt_all = FalseAlmCnt->Cnt_Ofdm_fail;

		// reset OFDM FA coutner
		ODM_SetBBReg(pDM_Odm, ODM_REG_OFDM_FA_RST_11AC, BIT17, 1);
		ODM_SetBBReg(pDM_Odm, ODM_REG_OFDM_FA_RST_11AC, BIT17, 0);
		// reset CCK FA counter
		ODM_SetBBReg(pDM_Odm, ODM_REG_CCK_FA_RST_11AC, BIT15, 0);
		ODM_SetBBReg(pDM_Odm, ODM_REG_CCK_FA_RST_11AC, BIT15, 1);
	}
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_FA_CNT, ODM_DBG_LOUD, ("Cnt_Cck_fail=%d\n",	FalseAlmCnt->Cnt_Cck_fail));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_FA_CNT, ODM_DBG_LOUD, ("Cnt_Ofdm_fail=%d\n",	FalseAlmCnt->Cnt_Ofdm_fail));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_FA_CNT, ODM_DBG_LOUD, ("Total False Alarm=%d\n",	FalseAlmCnt->Cnt_all));
}

//3============================================================
//3 CCK Packet Detect Threshold
//3============================================================

VOID 
odm_CCKPacketDetectionThresh(
	IN		PVOID					pDM_VOID
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte	CurCCK_CCAThres;
	PFALSE_ALARM_STATISTICS FalseAlmCnt = &(pDM_Odm->FalseAlmCnt);

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
//modify by Guo.Mingzhi 2011-12-29
	if (pDM_Odm->bDualMacSmartConcurrent == TRUE)
//	if (pDM_Odm->bDualMacSmartConcurrent == FALSE)
		return;

	if(pDM_Odm->bBtHsOperation)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_CCKPacketDetectionThresh() write 0xcd for BT HS mode!!\n"));
		ODM_Write_CCK_CCA_Thres(pDM_Odm, 0xcd);
		return;
	}

#endif

	if(!(pDM_Odm->SupportAbility & (ODM_BB_CCK_PD|ODM_BB_FA_CNT)))
		return;

	if(pDM_Odm->ExtLNA)
		return;

	if(pDM_Odm->bLinked)
	{
		if(pDM_Odm->RSSI_Min > 25)
			CurCCK_CCAThres = 0xcd;
		else if((pDM_Odm->RSSI_Min <= 25) && (pDM_Odm->RSSI_Min > 10))
			CurCCK_CCAThres = 0x83;
		else
		{
			if(FalseAlmCnt->Cnt_Cck_fail > 1000)
				CurCCK_CCAThres = 0x83;
			else
				CurCCK_CCAThres = 0x40;
		}
	}
	else
	{
		if(FalseAlmCnt->Cnt_Cck_fail > 1000)
			CurCCK_CCAThres = 0x83;
		else
			CurCCK_CCAThres = 0x40;
	}
	
#if (RTL8192D_SUPPORT==1) 
	if((pDM_Odm->SupportICType == ODM_RTL8192D)&&(*pDM_Odm->pBandType == ODM_BAND_2_4G))
		ODM_Write_CCK_CCA_Thres_92D(pDM_Odm, CurCCK_CCAThres);
	else
#endif
		ODM_Write_CCK_CCA_Thres(pDM_Odm, CurCCK_CCAThres);
}

VOID
ODM_Write_CCK_CCA_Thres(
	IN		PVOID					pDM_VOID,
	IN	u1Byte			CurCCK_CCAThres
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	pDIG_T	pDM_DigTable = &pDM_Odm->DM_DigTable;

	if(pDM_DigTable->CurCCK_CCAThres!=CurCCK_CCAThres)		//modify by Guo.Mingzhi 2012-01-03
	{
		ODM_Write1Byte(pDM_Odm, ODM_REG(CCK_CCA,pDM_Odm), CurCCK_CCAThres);
	}
	pDM_DigTable->PreCCK_CCAThres = pDM_DigTable->CurCCK_CCAThres;
	pDM_DigTable->CurCCK_CCAThres = CurCCK_CCAThres;
	
}

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

VOID
odm_EnableEDCCA(
	IN		PVOID					pDM_VOID
)
{

	// This should be moved out of OUTSRC
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PADAPTER		pAdapter	= pDM_Odm->Adapter;
	// Enable EDCCA. The value is suggested by SD3 Wilson.

	//
	// Revised for ASUS 11b/g performance issues, suggested by BB Neil, 2012.04.13.
	//
	if((pDM_Odm->SupportICType == ODM_RTL8723A)&&(IS_WIRELESS_MODE_G(pAdapter)))
	{
		//PlatformEFIOWrite1Byte(Adapter, rOFDM0_ECCAThreshold, 0x00);
		ODM_Write1Byte(pDM_Odm,rOFDM0_ECCAThreshold,0x00);
		ODM_Write1Byte(pDM_Odm,rOFDM0_ECCAThreshold+2,0xFD);
		
	}	
	else
	{
		//PlatformEFIOWrite1Byte(Adapter, rOFDM0_ECCAThreshold, 0x03);
		ODM_Write1Byte(pDM_Odm,rOFDM0_ECCAThreshold,0x03);
		ODM_Write1Byte(pDM_Odm,rOFDM0_ECCAThreshold+2,0x00);
	}	
	
	//PlatformEFIOWrite1Byte(Adapter, rOFDM0_ECCAThreshold+2, 0x00);
}

VOID
odm_DisableEDCCA(
	IN		PVOID					pDM_VOID
)
{	
	// Disable EDCCA..
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	ODM_Write1Byte(pDM_Odm, rOFDM0_ECCAThreshold, 0x7f);
	ODM_Write1Byte(pDM_Odm, rOFDM0_ECCAThreshold+2, 0x7f);
}

//
// Description: According to initial gain value to determine to enable or disable EDCCA.
//
// Suggested by SD3 Wilson. Added by tynli. 2011.11.25.
//
VOID
odm_DynamicEDCCA(
	IN		PVOID					pDM_VOID
)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PADAPTER		pAdapter	= pDM_Odm->Adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	u1Byte	RegC50, RegC58;
	BOOLEAN		bFwCurrentInPSMode=FALSE;	

	pAdapter->HalFunc.GetHwRegHandler(pAdapter, HW_VAR_FW_PSMODE_STATUS, (pu1Byte)(&bFwCurrentInPSMode));	

	// Disable EDCCA mode while under LPS mode, added by Roger, 2012.09.14.
	if(bFwCurrentInPSMode)
		return;
	
	RegC50 = (u1Byte)ODM_GetBBReg(pDM_Odm, rOFDM0_XAAGCCore1, bMaskByte0);
	RegC58 = (u1Byte)ODM_GetBBReg(pDM_Odm, rOFDM0_XBAGCCore1, bMaskByte0);


 	if((RegC50 > 0x28 && RegC58 > 0x28) ||
  		((pDM_Odm->SupportICType == ODM_RTL8723A && IS_WIRELESS_MODE_G(pAdapter) && RegC50>0x26)) ||
  		(pDM_Odm->SupportICType == ODM_RTL8188E && RegC50 > 0x28))
	{
		if(!pHalData->bPreEdccaEnable)
		{
			odm_EnableEDCCA(pDM_Odm);
			pHalData->bPreEdccaEnable = TRUE;
		}
		
	}
	else if((RegC50 < 0x25 && RegC58 < 0x25) || (pDM_Odm->SupportICType == ODM_RTL8188E && RegC50 < 0x25))
	{
		if(pHalData->bPreEdccaEnable)
		{
			odm_DisableEDCCA(pDM_Odm);
			pHalData->bPreEdccaEnable = FALSE;
		}
	}
}

// <20130108, Kordan> E.g., With LNA used, we make the Rx power smaller to have a better EVM. (Asked by Willis)
VOID
odm_RFEControl(
	IN	PDM_ODM_T	pDM_Odm,
	IN  u8Byte		RSSIVal
	)
{
	PADAPTER		Adapter = (PADAPTER)pDM_Odm->Adapter;
    HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	static u1Byte 	TRSW_HighPwr = 0;
	 
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, DBG_LOUD, ("===> odm_RFEControl, RSSI = %d, TRSW_HighPwr = 0x%X, pHalData->RFEType = %d\n",
		         RSSIVal, TRSW_HighPwr, pHalData->RFEType ));

    if (pHalData->RFEType == 3) {	   
		
        pDM_Odm->RSSI_TRSW = RSSIVal;

        if (pDM_Odm->RSSI_TRSW >= pDM_Odm->RSSI_TRSW_H) 
		{				 
            TRSW_HighPwr = 1; // Switch to
            PHY_SetBBReg(Adapter, r_ANTSEL_SW_Jaguar, BIT1|BIT0, 0x1);  // Set ANTSW=1/ANTSWB=0  for SW control
            PHY_SetBBReg(Adapter, r_ANTSEL_SW_Jaguar, BIT9|BIT8, 0x3);  // Set ANTSW=1/ANTSWB=0  for SW control
            
        } 
		else if (pDM_Odm->RSSI_TRSW <= pDM_Odm->RSSI_TRSW_L) 
        {	  
            TRSW_HighPwr = 0; // Switched back
            PHY_SetBBReg(Adapter, r_ANTSEL_SW_Jaguar, BIT1|BIT0, 0x1);  // Set ANTSW=1/ANTSWB=0  for SW control
            PHY_SetBBReg(Adapter, r_ANTSEL_SW_Jaguar, BIT9|BIT8, 0x0);  // Set ANTSW=1/ANTSWB=0  for SW control

        }
    }  

	
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, DBG_LOUD, ("(pDM_Odm->RSSI_TRSW_H, pDM_Odm->RSSI_TRSW_L) = (%d, %d)\n", pDM_Odm->RSSI_TRSW_H, pDM_Odm->RSSI_TRSW_L));		
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, DBG_LOUD, ("(RSSIVal, RSSIVal, pDM_Odm->RSSI_TRSW_iso) = (%d, %d, %d)\n", 
				 RSSIVal, pDM_Odm->RSSI_TRSW_iso, pDM_Odm->RSSI_TRSW));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, DBG_LOUD, ("<=== odm_RFEControl, RSSI = %d, TRSW_HighPwr = 0x%X\n", RSSIVal, TRSW_HighPwr));	
}



VOID
ODM_MPT_DIG(
	IN	PDM_ODM_T	pDM_Odm
	)
{
	pDIG_T						pDM_DigTable = &pDM_Odm->DM_DigTable;
	PFALSE_ALARM_STATISTICS 	pFalseAlmCnt = &pDM_Odm->FalseAlmCnt;
	u1Byte						CurrentIGI = (u1Byte)pDM_DigTable->CurIGValue;
	u1Byte						DIG_Upper = 0x40, DIG_Lower = 0x20, C50, E50;
	u8Byte						RXOK_cal;
	u1Byte						IGI_A = 0x20, IGI_B = 0x20;

#if ODM_FIX_2G_DIG
	IGI_A = 0x22;
	IGI_B = 0x24;		
#endif

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_MP, DBG_LOUD, ("===> ODM_MPT_DIG, pBandType = %d\n", *pDM_Odm->pBandType));

	odm_FalseAlarmCounterStatistics( pDM_Odm);
	pDM_Odm->LastNumQryPhyStatusAll = pDM_Odm->NumQryPhyStatusAll;
	pDM_Odm->NumQryPhyStatusAll = pDM_Odm->PhyDbgInfo.NumQryPhyStatusCCK + pDM_Odm->PhyDbgInfo.NumQryPhyStatusOFDM;
	RXOK_cal = pDM_Odm->NumQryPhyStatusAll - pDM_Odm->LastNumQryPhyStatusAll;
	
	if (RXOK_cal == 0)
		pDM_Odm->RxPWDBAve_final= 0;
	else
		pDM_Odm->RxPWDBAve_final= pDM_Odm->RxPWDBAve/RXOK_cal;

	pDM_Odm->RxPWDBAve = 0;
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, DBG_LOUD, ("RX OK = %d\n", RXOK_cal));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, DBG_LOUD, ("pDM_Odm->RxPWDBAve_final = %d\n", pDM_Odm->RxPWDBAve_final));
	
	// <20130315, Kordan> Except Cameo, we should always trun on 2.4G/5G DIG.
	// (Cameo fixes the IGI of 2.4G, so only DIG on 5G. Asked by James.)
#if ODM_FIX_2G_DIG
	if (*pDM_Odm->pBandType == BAND_ON_5G)	 // for 5G
#else
	if (1) // for both 2G/5G
#endif
		{
		pDM_Odm->MPDIG_2G = FALSE;
		pDM_Odm->Times_2G = 0;
	
		if (RXOK_cal >= 70 && pDM_Odm->RxPWDBAve_final<= 30)
		{
			if (CurrentIGI > 0x24){
				ODM_Write1Byte( pDM_Odm, rA_IGI_Jaguar, 0x24);
				ODM_Write1Byte( pDM_Odm, rB_IGI_Jaguar, 0x24);
			}
		}
		else
		{
			if(pFalseAlmCnt->Cnt_all > 1000){
				CurrentIGI = CurrentIGI + 8;
			}
			else if(pFalseAlmCnt->Cnt_all > 200){
				CurrentIGI = CurrentIGI + 4;
			}
			else if (pFalseAlmCnt->Cnt_all > 50){
				CurrentIGI = CurrentIGI + 2;
			}
			else if (pFalseAlmCnt->Cnt_all < 2){
				CurrentIGI = CurrentIGI - 2;
			}
			
			if (CurrentIGI < DIG_Lower ){
				CurrentIGI = DIG_Lower;
			}
			else if(CurrentIGI > DIG_Upper){
				CurrentIGI = DIG_Upper;
			}
			
			pDM_DigTable->CurIGValue = CurrentIGI;
			
			ODM_Write1Byte( pDM_Odm, rA_IGI_Jaguar, (u1Byte)CurrentIGI);
			ODM_Write1Byte( pDM_Odm, rB_IGI_Jaguar, (u1Byte)CurrentIGI);

			C50 = ODM_Read1Byte( pDM_Odm, 0xc50);
			E50 = ODM_Read1Byte( pDM_Odm, 0xe50);
			//pDM_Odm->MPDIG_2G = FALSE;
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_MP, DBG_LOUD, ("DIG = (%x, %x), Cnt_all = %d, Cnt_Ofdm_fail = %d, Cnt_Cck_fail = %d\n", C50, E50, pFalseAlmCnt->Cnt_all, pFalseAlmCnt->Cnt_Ofdm_fail, pFalseAlmCnt->Cnt_Cck_fail));
		}
			
	}
	else
	{	//2G
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_MP, DBG_LOUD, ("MPDIG_2G = %d,\n", pDM_Odm->MPDIG_2G));
		
		if(pDM_Odm->MPDIG_2G == FALSE){
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_MP, DBG_LOUD, ("===> Fix IGI\n"));
			ODM_Write1Byte( pDM_Odm, rA_IGI_Jaguar, (u1Byte)IGI_A);
			ODM_Write1Byte( pDM_Odm, rB_IGI_Jaguar, (u1Byte)IGI_B);
		}
		if (pDM_Odm->Times_2G == 2)
			pDM_Odm->MPDIG_2G = TRUE;
		pDM_Odm->Times_2G++;
	}
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, DBG_LOUD, ("pDM_Odm->RxPWDBAve_final = %d\n", pDM_Odm->RxPWDBAve_final));

	if (pDM_Odm->SupportICType == ODM_RTL8812)
		odm_RFEControl(pDM_Odm, pDM_Odm->RxPWDBAve_final);
	
	ODM_SetTimer(pDM_Odm, &pDM_Odm->MPT_DIGTimer, 700);
	
}		

VOID
odm_MPT_DIGCallback(
	PRT_TIMER		pTimer
)
{
	PADAPTER		Adapter = (PADAPTER)pTimer->Adapter;
       HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	  PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;


	#if DEV_BUS_TYPE==RT_PCI_INTERFACE
		#if USE_WORKITEM
			PlatformScheduleWorkItem(&pDM_Odm->MPT_DIGWorkitem);
		#else
			ODM_MPT_DIG(pDM_Odm);
		#endif
	#else
		PlatformScheduleWorkItem(&pDM_Odm->MPT_DIGWorkitem);
	#endif

}

VOID
odm_MPT_DIGWorkItemCallback(
    IN PVOID            pContext
    )
{
	PADAPTER	Adapter = (PADAPTER)pContext;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;

	ODM_MPT_DIG(pDM_Odm);
}

#endif

