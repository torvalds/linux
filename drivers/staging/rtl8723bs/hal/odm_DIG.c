// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/

#include "odm_precomp.h"

#define ADAPTIVITY_VERSION "5.0"

void odm_NHMCounterStatisticsInit(void *pDM_VOID)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;

	/* PHY parameters initialize for n series */
	rtw_write16(pDM_Odm->Adapter, ODM_REG_NHM_TIMER_11N+2, 0x2710);	/* 0x894[31:16]= 0x2710	Time duration for NHM unit: 4us, 0x2710 =40ms */
	/* rtw_write16(pDM_Odm->Adapter, ODM_REG_NHM_TIMER_11N+2, 0x4e20);	0x894[31:16]= 0x4e20	Time duration for NHM unit: 4us, 0x4e20 =80ms */
	rtw_write16(pDM_Odm->Adapter, ODM_REG_NHM_TH9_TH10_11N+2, 0xffff);	/* 0x890[31:16]= 0xffff	th_9, th_10 */
	/* rtw_write32(pDM_Odm->Adapter, ODM_REG_NHM_TH3_TO_TH0_11N, 0xffffff5c);	0x898 = 0xffffff5c		th_3, th_2, th_1, th_0 */
	rtw_write32(pDM_Odm->Adapter, ODM_REG_NHM_TH3_TO_TH0_11N, 0xffffff52);	/* 0x898 = 0xffffff52		th_3, th_2, th_1, th_0 */
	rtw_write32(pDM_Odm->Adapter, ODM_REG_NHM_TH7_TO_TH4_11N, 0xffffffff);	/* 0x89c = 0xffffffff		th_7, th_6, th_5, th_4 */
	PHY_SetBBReg(pDM_Odm->Adapter, ODM_REG_FPGA0_IQK_11N, bMaskByte0, 0xff);		/* 0xe28[7:0]= 0xff		th_8 */
	PHY_SetBBReg(pDM_Odm->Adapter, ODM_REG_NHM_TH9_TH10_11N, BIT10|BIT9|BIT8, 0x7);	/* 0x890[9:8]=3			enable CCX */
	PHY_SetBBReg(pDM_Odm->Adapter, ODM_REG_OFDM_FA_RSTC_11N, BIT7, 0x1);		/* 0xc0c[7]= 1			max power among all RX ants */
}

void odm_NHMCounterStatistics(void *pDM_VOID)
{
	PDM_ODM_T pDM_Odm = (PDM_ODM_T)pDM_VOID;

	/*  Get NHM report */
	odm_GetNHMCounterStatistics(pDM_Odm);

	/*  Reset NHM counter */
	odm_NHMCounterStatisticsReset(pDM_Odm);
}

void odm_GetNHMCounterStatistics(void *pDM_VOID)
{
	PDM_ODM_T pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u32 value32 = 0;

	value32 = PHY_QueryBBReg(pDM_Odm->Adapter, ODM_REG_NHM_CNT_11N, bMaskDWord);

	pDM_Odm->NHM_cnt_0 = (u8)(value32 & bMaskByte0);
}

void odm_NHMCounterStatisticsReset(void *pDM_VOID)
{
	PDM_ODM_T pDM_Odm = (PDM_ODM_T)pDM_VOID;

	PHY_SetBBReg(pDM_Odm->Adapter, ODM_REG_NHM_TH9_TH10_11N, BIT1, 0);
	PHY_SetBBReg(pDM_Odm->Adapter, ODM_REG_NHM_TH9_TH10_11N, BIT1, 1);
}

void odm_NHMBBInit(void *pDM_VOID)
{
	PDM_ODM_T pDM_Odm = (PDM_ODM_T)pDM_VOID;

	pDM_Odm->adaptivity_flag = 0;
	pDM_Odm->tolerance_cnt = 3;
	pDM_Odm->NHMLastTxOkcnt = 0;
	pDM_Odm->NHMLastRxOkcnt = 0;
	pDM_Odm->NHMCurTxOkcnt = 0;
	pDM_Odm->NHMCurRxOkcnt = 0;
}

/*  */
void odm_NHMBB(void *pDM_VOID)
{
	PDM_ODM_T pDM_Odm = (PDM_ODM_T)pDM_VOID;
	/* u8 test_status; */
	/* Pfalse_ALARM_STATISTICS pFalseAlmCnt = &(pDM_Odm->FalseAlmCnt); */

	pDM_Odm->NHMCurTxOkcnt =
		*(pDM_Odm->pNumTxBytesUnicast)-pDM_Odm->NHMLastTxOkcnt;
	pDM_Odm->NHMCurRxOkcnt =
		*(pDM_Odm->pNumRxBytesUnicast)-pDM_Odm->NHMLastRxOkcnt;
	pDM_Odm->NHMLastTxOkcnt =
		*(pDM_Odm->pNumTxBytesUnicast);
	pDM_Odm->NHMLastRxOkcnt =
		*(pDM_Odm->pNumRxBytesUnicast);
	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_DIG,
		ODM_DBG_LOUD,
		(
			"NHM_cnt_0 =%d, NHMCurTxOkcnt = %llu, NHMCurRxOkcnt = %llu\n",
			pDM_Odm->NHM_cnt_0,
			pDM_Odm->NHMCurTxOkcnt,
			pDM_Odm->NHMCurRxOkcnt
		)
	);


	if ((pDM_Odm->NHMCurTxOkcnt) + 1 > (u64)(pDM_Odm->NHMCurRxOkcnt<<2) + 1) { /* Tx > 4*Rx possible for adaptivity test */
		if (pDM_Odm->NHM_cnt_0 >= 190 || pDM_Odm->adaptivity_flag == true) {
			/* Enable EDCCA since it is possible running Adaptivity testing */
			/* test_status = 1; */
			pDM_Odm->adaptivity_flag = true;
			pDM_Odm->tolerance_cnt = 0;
		} else {
			if (pDM_Odm->tolerance_cnt < 3)
				pDM_Odm->tolerance_cnt = pDM_Odm->tolerance_cnt + 1;
			else
				pDM_Odm->tolerance_cnt = 4;
			/* test_status = 5; */
			if (pDM_Odm->tolerance_cnt > 3) {
				/* test_status = 3; */
				pDM_Odm->adaptivity_flag = false;
			}
		}
	} else { /*  TX<RX */
		if (pDM_Odm->adaptivity_flag == true && pDM_Odm->NHM_cnt_0 <= 200) {
			/* test_status = 2; */
			pDM_Odm->tolerance_cnt = 0;
		} else {
			if (pDM_Odm->tolerance_cnt < 3)
				pDM_Odm->tolerance_cnt = pDM_Odm->tolerance_cnt + 1;
			else
				pDM_Odm->tolerance_cnt = 4;
			/* test_status = 5; */
			if (pDM_Odm->tolerance_cnt > 3) {
				/* test_status = 4; */
				pDM_Odm->adaptivity_flag = false;
			}
		}
	}

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("adaptivity_flag = %d\n ", pDM_Odm->adaptivity_flag));
}

void odm_SearchPwdBLowerBound(void *pDM_VOID, u8 IGI_target)
{
	PDM_ODM_T pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u32 value32 = 0;
	u8 cnt, IGI;
	bool bAdjust = true;
	s8 TH_L2H_dmc, TH_H2L_dmc;
	s8 Diff;

	IGI = 0x50; /*  find H2L, L2H lower bound */
	ODM_Write_DIG(pDM_Odm, IGI);


	Diff = IGI_target-(s8)IGI;
	TH_L2H_dmc = pDM_Odm->TH_L2H_ini + Diff;
	if (TH_L2H_dmc > 10)
		TH_L2H_dmc = 10;
	TH_H2L_dmc = TH_L2H_dmc - pDM_Odm->TH_EDCCA_HL_diff;
	PHY_SetBBReg(pDM_Odm->Adapter, rOFDM0_ECCAThreshold, bMaskByte0, (u8)TH_L2H_dmc);
	PHY_SetBBReg(pDM_Odm->Adapter, rOFDM0_ECCAThreshold, bMaskByte2, (u8)TH_H2L_dmc);

	mdelay(5);

	while (bAdjust) {
		for (cnt = 0; cnt < 20; cnt++) {
			value32 = PHY_QueryBBReg(pDM_Odm->Adapter, ODM_REG_RPT_11N, bMaskDWord);

			if (value32 & BIT30)
				pDM_Odm->txEdcca1 = pDM_Odm->txEdcca1 + 1;
			else if (value32 & BIT29)
				pDM_Odm->txEdcca1 = pDM_Odm->txEdcca1 + 1;
			else
				pDM_Odm->txEdcca0 = pDM_Odm->txEdcca0 + 1;
		}
		/* DbgPrint("txEdcca1 = %d, txEdcca0 = %d\n", pDM_Odm->txEdcca1, pDM_Odm->txEdcca0); */

		if (pDM_Odm->txEdcca1 > 5) {
			IGI = IGI-1;
			TH_L2H_dmc = TH_L2H_dmc + 1;
			if (TH_L2H_dmc > 10)
				TH_L2H_dmc = 10;
			TH_H2L_dmc = TH_L2H_dmc - pDM_Odm->TH_EDCCA_HL_diff;
			PHY_SetBBReg(pDM_Odm->Adapter, rOFDM0_ECCAThreshold, bMaskByte0, (u8)TH_L2H_dmc);
			PHY_SetBBReg(pDM_Odm->Adapter, rOFDM0_ECCAThreshold, bMaskByte2, (u8)TH_H2L_dmc);

			pDM_Odm->TxHangFlg = true;
			pDM_Odm->txEdcca1 = 0;
			pDM_Odm->txEdcca0 = 0;

			if (TH_L2H_dmc == 10) {
				bAdjust = false;
				pDM_Odm->TxHangFlg = false;
				pDM_Odm->txEdcca1 = 0;
				pDM_Odm->txEdcca0 = 0;
				pDM_Odm->H2L_lb = TH_H2L_dmc;
				pDM_Odm->L2H_lb = TH_L2H_dmc;
				pDM_Odm->Adaptivity_IGI_upper = IGI;
			}
		} else {
			bAdjust = false;
			pDM_Odm->TxHangFlg = false;
			pDM_Odm->txEdcca1 = 0;
			pDM_Odm->txEdcca0 = 0;
			pDM_Odm->H2L_lb = TH_H2L_dmc;
			pDM_Odm->L2H_lb = TH_L2H_dmc;
			pDM_Odm->Adaptivity_IGI_upper = IGI;
		}
	}

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("IGI = 0x%x, H2L_lb = 0x%x, L2H_lb = 0x%x\n", IGI, pDM_Odm->H2L_lb, pDM_Odm->L2H_lb));
}

void odm_AdaptivityInit(void *pDM_VOID)
{
	PDM_ODM_T pDM_Odm = (PDM_ODM_T)pDM_VOID;

	if (pDM_Odm->Carrier_Sense_enable == false)
		pDM_Odm->TH_L2H_ini = 0xf7; /*  -7 */
	else
		pDM_Odm->TH_L2H_ini = 0xa;

	pDM_Odm->AdapEn_RSSI = 20;
	pDM_Odm->TH_EDCCA_HL_diff = 7;

	pDM_Odm->IGI_Base = 0x32;
	pDM_Odm->IGI_target = 0x1c;
	pDM_Odm->ForceEDCCA = 0;
	pDM_Odm->NHM_disable = false;
	pDM_Odm->TxHangFlg = true;
	pDM_Odm->txEdcca0 = 0;
	pDM_Odm->txEdcca1 = 0;
	pDM_Odm->H2L_lb = 0;
	pDM_Odm->L2H_lb = 0;
	pDM_Odm->Adaptivity_IGI_upper = 0;
	odm_NHMBBInit(pDM_Odm);

	PHY_SetBBReg(pDM_Odm->Adapter, REG_RD_CTRL, BIT11, 1); /*  stop counting if EDCCA is asserted */
}


void odm_Adaptivity(void *pDM_VOID, u8 IGI)
{
	PDM_ODM_T pDM_Odm = (PDM_ODM_T)pDM_VOID;
	s8 TH_L2H_dmc, TH_H2L_dmc;
	s8 Diff, IGI_target;
	bool EDCCA_State = false;

	if (!(pDM_Odm->SupportAbility & ODM_BB_ADAPTIVITY)) {
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("Go to odm_DynamicEDCCA()\n"));
		return;
	}
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_Adaptivity() =====>\n"));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("ForceEDCCA =%d, IGI_Base = 0x%x, TH_L2H_ini = %d, TH_EDCCA_HL_diff = %d, AdapEn_RSSI = %d\n",
		pDM_Odm->ForceEDCCA, pDM_Odm->IGI_Base, pDM_Odm->TH_L2H_ini, pDM_Odm->TH_EDCCA_HL_diff, pDM_Odm->AdapEn_RSSI));

	if (*pDM_Odm->pBandWidth == ODM_BW20M) /* CHANNEL_WIDTH_20 */
		IGI_target = pDM_Odm->IGI_Base;
	else if (*pDM_Odm->pBandWidth == ODM_BW40M)
		IGI_target = pDM_Odm->IGI_Base + 2;
	else if (*pDM_Odm->pBandWidth == ODM_BW80M)
		IGI_target = pDM_Odm->IGI_Base + 2;
	else
		IGI_target = pDM_Odm->IGI_Base;
	pDM_Odm->IGI_target = (u8) IGI_target;

	/* Search pwdB lower bound */
	if (pDM_Odm->TxHangFlg == true) {
		PHY_SetBBReg(pDM_Odm->Adapter, ODM_REG_DBG_RPT_11N, bMaskDWord, 0x208);
		odm_SearchPwdBLowerBound(pDM_Odm, pDM_Odm->IGI_target);
	}

	if ((!pDM_Odm->bLinked) || (*pDM_Odm->pChannel > 149)) { /*  Band4 doesn't need adaptivity */
		PHY_SetBBReg(pDM_Odm->Adapter, rOFDM0_ECCAThreshold, bMaskByte0, 0x7f);
		PHY_SetBBReg(pDM_Odm->Adapter, rOFDM0_ECCAThreshold, bMaskByte2, 0x7f);
		return;
	}

	if (!pDM_Odm->ForceEDCCA) {
		if (pDM_Odm->RSSI_Min > pDM_Odm->AdapEn_RSSI)
			EDCCA_State = true;
		else if (pDM_Odm->RSSI_Min < (pDM_Odm->AdapEn_RSSI - 5))
			EDCCA_State = false;
	} else
		EDCCA_State = true;

	if (
		pDM_Odm->bLinked &&
		pDM_Odm->Carrier_Sense_enable == false &&
		pDM_Odm->NHM_disable == false &&
		pDM_Odm->TxHangFlg == false
	)
		odm_NHMBB(pDM_Odm);

	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_DIG,
		ODM_DBG_LOUD,
		(
			"BandWidth =%s, IGI_target = 0x%x, EDCCA_State =%d\n",
			(*pDM_Odm->pBandWidth == ODM_BW80M) ? "80M" :
			((*pDM_Odm->pBandWidth == ODM_BW40M) ? "40M" : "20M"),
			IGI_target,
			EDCCA_State
		)
	);

	if (EDCCA_State) {
		Diff = IGI_target-(s8)IGI;
		TH_L2H_dmc = pDM_Odm->TH_L2H_ini + Diff;
		if (TH_L2H_dmc > 10)
			TH_L2H_dmc = 10;

		TH_H2L_dmc = TH_L2H_dmc - pDM_Odm->TH_EDCCA_HL_diff;

		/* replace lower bound to prevent EDCCA always equal  */
		if (TH_H2L_dmc < pDM_Odm->H2L_lb)
			TH_H2L_dmc = pDM_Odm->H2L_lb;
		if (TH_L2H_dmc < pDM_Odm->L2H_lb)
			TH_L2H_dmc = pDM_Odm->L2H_lb;
	} else {
		TH_L2H_dmc = 0x7f;
		TH_H2L_dmc = 0x7f;
	}
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("IGI = 0x%x, TH_L2H_dmc = %d, TH_H2L_dmc = %d\n",
		IGI, TH_L2H_dmc, TH_H2L_dmc));
	PHY_SetBBReg(pDM_Odm->Adapter, rOFDM0_ECCAThreshold, bMaskByte0, (u8)TH_L2H_dmc);
	PHY_SetBBReg(pDM_Odm->Adapter, rOFDM0_ECCAThreshold, bMaskByte2, (u8)TH_H2L_dmc);
}

void ODM_Write_DIG(void *pDM_VOID, u8 CurrentIGI)
{
	PDM_ODM_T pDM_Odm = (PDM_ODM_T)pDM_VOID;
	pDIG_T pDM_DigTable = &pDM_Odm->DM_DigTable;

	if (pDM_DigTable->bStopDIG) {
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("Stop Writing IGI\n"));
		return;
	}

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_TRACE, ("ODM_REG(IGI_A, pDM_Odm) = 0x%x, ODM_BIT(IGI, pDM_Odm) = 0x%x\n",
		ODM_REG(IGI_A, pDM_Odm), ODM_BIT(IGI, pDM_Odm)));

	if (pDM_DigTable->CurIGValue != CurrentIGI) {
		/* 1 Check initial gain by upper bound */
		if (!pDM_DigTable->bPSDInProgress) {
			if (CurrentIGI > pDM_DigTable->rx_gain_range_max) {
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_TRACE, ("CurrentIGI(0x%02x) is larger than upper bound !!\n", pDM_DigTable->rx_gain_range_max));
				CurrentIGI = pDM_DigTable->rx_gain_range_max;
			}

		}

		/* 1 Set IGI value */
		PHY_SetBBReg(pDM_Odm->Adapter, ODM_REG(IGI_A, pDM_Odm), ODM_BIT(IGI, pDM_Odm), CurrentIGI);

		if (pDM_Odm->RFType > ODM_1T1R)
			PHY_SetBBReg(pDM_Odm->Adapter, ODM_REG(IGI_B, pDM_Odm), ODM_BIT(IGI, pDM_Odm), CurrentIGI);

		pDM_DigTable->CurIGValue = CurrentIGI;
	}

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_TRACE, ("CurrentIGI(0x%02x).\n", CurrentIGI));

}

void odm_PauseDIG(
	void *pDM_VOID,
	ODM_Pause_DIG_TYPE PauseType,
	u8 IGIValue
)
{
	PDM_ODM_T pDM_Odm = (PDM_ODM_T)pDM_VOID;
	pDIG_T pDM_DigTable = &pDM_Odm->DM_DigTable;
	static bool bPaused;

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_PauseDIG() =========>\n"));

	if (
		(pDM_Odm->SupportAbility & ODM_BB_ADAPTIVITY) &&
		pDM_Odm->TxHangFlg == true
	) {
		ODM_RT_TRACE(
			pDM_Odm,
			ODM_COMP_DIG,
			ODM_DBG_LOUD,
			("odm_PauseDIG(): Dynamic adjust threshold in progress !!\n")
		);
		return;
	}

	if (
		!bPaused && (!(pDM_Odm->SupportAbility & ODM_BB_DIG) ||
		!(pDM_Odm->SupportAbility & ODM_BB_FA_CNT))
	){
		ODM_RT_TRACE(
			pDM_Odm,
			ODM_COMP_DIG,
			ODM_DBG_LOUD,
			("odm_PauseDIG(): Return: SupportAbility ODM_BB_DIG or ODM_BB_FA_CNT is disabled\n")
		);
		return;
	}

	switch (PauseType) {
	/* 1 Pause DIG */
	case ODM_PAUSE_DIG:
		/* 2 Disable DIG */
		ODM_CmnInfoUpdate(pDM_Odm, ODM_CMNINFO_ABILITY, pDM_Odm->SupportAbility & (~ODM_BB_DIG));
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_PauseDIG(): Pause DIG !!\n"));

		/* 2 Backup IGI value */
		if (!bPaused) {
			pDM_DigTable->IGIBackup = pDM_DigTable->CurIGValue;
			bPaused = true;
		}
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_PauseDIG(): Backup IGI  = 0x%x\n", pDM_DigTable->IGIBackup));

		/* 2 Write new IGI value */
		ODM_Write_DIG(pDM_Odm, IGIValue);
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_PauseDIG(): Write new IGI = 0x%x\n", IGIValue));
		break;

	/* 1 Resume DIG */
	case ODM_RESUME_DIG:
		if (bPaused) {
			/* 2 Write backup IGI value */
			ODM_Write_DIG(pDM_Odm, pDM_DigTable->IGIBackup);
			bPaused = false;
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_PauseDIG(): Write original IGI = 0x%x\n", pDM_DigTable->IGIBackup));

			/* 2 Enable DIG */
			ODM_CmnInfoUpdate(pDM_Odm, ODM_CMNINFO_ABILITY, pDM_Odm->SupportAbility | ODM_BB_DIG);
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_PauseDIG(): Resume DIG !!\n"));
		}
		break;

	default:
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_PauseDIG(): Wrong  type !!\n"));
		break;
	}
}

bool odm_DigAbort(void *pDM_VOID)
{
	PDM_ODM_T pDM_Odm = (PDM_ODM_T)pDM_VOID;

	/* SupportAbility */
	if (!(pDM_Odm->SupportAbility & ODM_BB_FA_CNT)) {
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Return: SupportAbility ODM_BB_FA_CNT is disabled\n"));
		return	true;
	}

	/* SupportAbility */
	if (!(pDM_Odm->SupportAbility & ODM_BB_DIG)) {
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Return: SupportAbility ODM_BB_DIG is disabled\n"));
		return	true;
	}

	/* ScanInProcess */
	if (*(pDM_Odm->pbScanInProcess)) {
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Return: In Scan Progress\n"));
		return	true;
	}

	/* add by Neil Chen to avoid PSD is processing */
	if (pDM_Odm->bDMInitialGainEnable == false) {
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Return: PSD is Processing\n"));
		return	true;
	}

	return	false;
}

void odm_DIGInit(void *pDM_VOID)
{
	PDM_ODM_T pDM_Odm = (PDM_ODM_T)pDM_VOID;
	pDIG_T pDM_DigTable = &pDM_Odm->DM_DigTable;

	pDM_DigTable->bStopDIG = false;
	pDM_DigTable->bPSDInProgress = false;
	pDM_DigTable->CurIGValue = (u8) PHY_QueryBBReg(pDM_Odm->Adapter, ODM_REG(IGI_A, pDM_Odm), ODM_BIT(IGI, pDM_Odm));
	pDM_DigTable->RssiLowThresh	= DM_DIG_THRESH_LOW;
	pDM_DigTable->RssiHighThresh	= DM_DIG_THRESH_HIGH;
	pDM_DigTable->FALowThresh	= DMfalseALARM_THRESH_LOW;
	pDM_DigTable->FAHighThresh	= DMfalseALARM_THRESH_HIGH;
	pDM_DigTable->BackoffVal = DM_DIG_BACKOFF_DEFAULT;
	pDM_DigTable->BackoffVal_range_max = DM_DIG_BACKOFF_MAX;
	pDM_DigTable->BackoffVal_range_min = DM_DIG_BACKOFF_MIN;
	pDM_DigTable->PreCCK_CCAThres = 0xFF;
	pDM_DigTable->CurCCK_CCAThres = 0x83;
	pDM_DigTable->ForbiddenIGI = DM_DIG_MIN_NIC;
	pDM_DigTable->LargeFAHit = 0;
	pDM_DigTable->Recover_cnt = 0;
	pDM_DigTable->bMediaConnect_0 = false;
	pDM_DigTable->bMediaConnect_1 = false;

	/* To Initialize pDM_Odm->bDMInitialGainEnable == false to avoid DIG error */
	pDM_Odm->bDMInitialGainEnable = true;

	pDM_DigTable->DIG_Dynamic_MIN_0 = DM_DIG_MIN_NIC;
	pDM_DigTable->DIG_Dynamic_MIN_1 = DM_DIG_MIN_NIC;

	/* To Initi BT30 IGI */
	pDM_DigTable->BT30_CurIGI = 0x32;

	if (pDM_Odm->BoardType & (ODM_BOARD_EXT_PA|ODM_BOARD_EXT_LNA)) {
		pDM_DigTable->rx_gain_range_max = DM_DIG_MAX_NIC;
		pDM_DigTable->rx_gain_range_min = DM_DIG_MIN_NIC;
	} else {
		pDM_DigTable->rx_gain_range_max = DM_DIG_MAX_NIC;
		pDM_DigTable->rx_gain_range_min = DM_DIG_MIN_NIC;
	}

}


void odm_DIG(void *pDM_VOID)
{
	PDM_ODM_T pDM_Odm = (PDM_ODM_T)pDM_VOID;

	/*  Common parameters */
	pDIG_T pDM_DigTable = &pDM_Odm->DM_DigTable;
	Pfalse_ALARM_STATISTICS pFalseAlmCnt = &pDM_Odm->FalseAlmCnt;
	bool FirstConnect, FirstDisConnect;
	u8 DIG_MaxOfMin, DIG_Dynamic_MIN;
	u8 dm_dig_max, dm_dig_min;
	u8 CurrentIGI = pDM_DigTable->CurIGValue;
	u8 offset;
	u32 dm_FA_thres[3];
	u8 Adap_IGI_Upper = 0;
	u32 TxTp = 0, RxTp = 0;
	bool bDFSBand = false;
	bool bPerformance = true, bFirstTpTarget = false, bFirstCoverage = false;

	if (odm_DigAbort(pDM_Odm) == true)
		return;

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG() ===========================>\n\n"));

	if (pDM_Odm->adaptivity_flag == true)
		Adap_IGI_Upper = pDM_Odm->Adaptivity_IGI_upper;


	/* 1 Update status */
	DIG_Dynamic_MIN = pDM_DigTable->DIG_Dynamic_MIN_0;
	FirstConnect = (pDM_Odm->bLinked) && (pDM_DigTable->bMediaConnect_0 == false);
	FirstDisConnect = (!pDM_Odm->bLinked) && (pDM_DigTable->bMediaConnect_0 == true);

	/* 1 Boundary Decision */
	/* 2 For WIN\CE */
	dm_dig_max = 0x5A;
	dm_dig_min = DM_DIG_MIN_NIC;
	DIG_MaxOfMin = DM_DIG_MAX_AP;

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Absolutely upper bound = 0x%x, lower bound = 0x%x\n", dm_dig_max, dm_dig_min));

	/* 1 Adjust boundary by RSSI */
	if (pDM_Odm->bLinked && bPerformance) {
		/* 2 Modify DIG upper bound */
		/* 4 Modify DIG upper bound for 92E, 8723A\B, 8821 & 8812 BT */
		if (pDM_Odm->bBtLimitedDig == 1) {
			offset = 10;
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Coex. case: Force upper bound to RSSI + %d !!!!!!\n", offset));
		} else
			offset = 15;

		if ((pDM_Odm->RSSI_Min + offset) > dm_dig_max)
			pDM_DigTable->rx_gain_range_max = dm_dig_max;
		else if ((pDM_Odm->RSSI_Min + offset) < dm_dig_min)
			pDM_DigTable->rx_gain_range_max = dm_dig_min;
		else
			pDM_DigTable->rx_gain_range_max = pDM_Odm->RSSI_Min + offset;

		/* 2 Modify DIG lower bound */
		/* if (pDM_Odm->bOneEntryOnly) */
		{
			if (pDM_Odm->RSSI_Min < dm_dig_min)
				DIG_Dynamic_MIN = dm_dig_min;
			else if (pDM_Odm->RSSI_Min > DIG_MaxOfMin)
				DIG_Dynamic_MIN = DIG_MaxOfMin;
			else
				DIG_Dynamic_MIN = pDM_Odm->RSSI_Min;
		}
	} else {
		pDM_DigTable->rx_gain_range_max = dm_dig_max;
		DIG_Dynamic_MIN = dm_dig_min;
	}

	/* 1 Force Lower Bound for AntDiv */
	if (pDM_Odm->bLinked && !pDM_Odm->bOneEntryOnly) {
		if (pDM_Odm->SupportAbility & ODM_BB_ANT_DIV) {
			if (
				pDM_Odm->AntDivType == CG_TRX_HW_ANTDIV ||
				pDM_Odm->AntDivType == CG_TRX_SMART_ANTDIV ||
				pDM_Odm->AntDivType == S0S1_SW_ANTDIV
			) {
				if (pDM_DigTable->AntDiv_RSSI_max > DIG_MaxOfMin)
					DIG_Dynamic_MIN = DIG_MaxOfMin;
				else
					DIG_Dynamic_MIN = (u8) pDM_DigTable->AntDiv_RSSI_max;
				ODM_RT_TRACE(
					pDM_Odm,
					ODM_COMP_ANT_DIV,
					ODM_DBG_LOUD,
					(
						"odm_DIG(): Antenna diversity case: Force lower bound to 0x%x !!!!!!\n",
						DIG_Dynamic_MIN
					)
				);
				ODM_RT_TRACE(
					pDM_Odm,
					ODM_COMP_ANT_DIV,
					ODM_DBG_LOUD,
					(
						"odm_DIG(): Antenna diversity case: RSSI_max = 0x%x !!!!!!\n",
						pDM_DigTable->AntDiv_RSSI_max
					)
				);
			}
		}
	}
	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_DIG,
		ODM_DBG_LOUD,
		(
			"odm_DIG(): Adjust boundary by RSSI Upper bound = 0x%x, Lower bound = 0x%x\n",
			pDM_DigTable->rx_gain_range_max,
			DIG_Dynamic_MIN
		)
	);
	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_DIG,
		ODM_DBG_LOUD,
		(
			"odm_DIG(): Link status: bLinked = %d, RSSI = %d, bFirstConnect = %d, bFirsrDisConnect = %d\n\n",
			pDM_Odm->bLinked,
			pDM_Odm->RSSI_Min,
			FirstConnect,
			FirstDisConnect
		)
	);

	/* 1 Modify DIG lower bound, deal with abnormal case */
	/* 2 Abnormal false alarm case */
	if (FirstDisConnect) {
		pDM_DigTable->rx_gain_range_min = DIG_Dynamic_MIN;
		pDM_DigTable->ForbiddenIGI = DIG_Dynamic_MIN;
	} else
		pDM_DigTable->rx_gain_range_min =
			odm_ForbiddenIGICheck(pDM_Odm, DIG_Dynamic_MIN, CurrentIGI);

	if (pDM_Odm->bLinked && !FirstConnect) {
		if (
			(pDM_Odm->PhyDbgInfo.NumQryBeaconPkt < 5) &&
			pDM_Odm->bsta_state
		) {
			pDM_DigTable->rx_gain_range_min = dm_dig_min;
			ODM_RT_TRACE(
				pDM_Odm,
				ODM_COMP_DIG,
				ODM_DBG_LOUD,
				(
					"odm_DIG(): Abnrormal #beacon (%d) case in STA mode: Force lower bound to 0x%x !!!!!!\n\n",
					pDM_Odm->PhyDbgInfo.NumQryBeaconPkt,
					pDM_DigTable->rx_gain_range_min
				)
			);
		}
	}

	/* 2 Abnormal lower bound case */
	if (pDM_DigTable->rx_gain_range_min > pDM_DigTable->rx_gain_range_max) {
		pDM_DigTable->rx_gain_range_min = pDM_DigTable->rx_gain_range_max;
		ODM_RT_TRACE(
			pDM_Odm,
			ODM_COMP_DIG,
			ODM_DBG_LOUD,
			(
				"odm_DIG(): Abnrormal lower bound case: Force lower bound to 0x%x !!!!!!\n\n",
				pDM_DigTable->rx_gain_range_min
			)
		);
	}


	/* 1 False alarm threshold decision */
	odm_FAThresholdCheck(pDM_Odm, bDFSBand, bPerformance, RxTp, TxTp, dm_FA_thres);
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): False alarm threshold = %d, %d, %d\n\n", dm_FA_thres[0], dm_FA_thres[1], dm_FA_thres[2]));

	/* 1 Adjust initial gain by false alarm */
	if (pDM_Odm->bLinked && bPerformance) {
		/* 2 After link */
		ODM_RT_TRACE(
			pDM_Odm,
			ODM_COMP_DIG,
			ODM_DBG_LOUD,
			("odm_DIG(): Adjust IGI after link\n")
		);

		if (bFirstTpTarget || (FirstConnect && bPerformance)) {
			pDM_DigTable->LargeFAHit = 0;

			if (pDM_Odm->RSSI_Min < DIG_MaxOfMin) {
				if (CurrentIGI < pDM_Odm->RSSI_Min)
					CurrentIGI = pDM_Odm->RSSI_Min;
			} else {
				if (CurrentIGI < DIG_MaxOfMin)
					CurrentIGI = DIG_MaxOfMin;
			}

			ODM_RT_TRACE(
				pDM_Odm,
				ODM_COMP_DIG,
				ODM_DBG_LOUD,
				(
					"odm_DIG(): First connect case: IGI does on-shot to 0x%x\n",
					CurrentIGI
				)
			);

		} else {
			if (pFalseAlmCnt->Cnt_all > dm_FA_thres[2])
				CurrentIGI = CurrentIGI + 4;
			else if (pFalseAlmCnt->Cnt_all > dm_FA_thres[1])
				CurrentIGI = CurrentIGI + 2;
			else if (pFalseAlmCnt->Cnt_all < dm_FA_thres[0])
				CurrentIGI = CurrentIGI - 2;

			if (
				(pDM_Odm->PhyDbgInfo.NumQryBeaconPkt < 5) &&
				(pFalseAlmCnt->Cnt_all < DM_DIG_FA_TH1) &&
				(pDM_Odm->bsta_state)
			) {
				CurrentIGI = pDM_DigTable->rx_gain_range_min;
				ODM_RT_TRACE(
					pDM_Odm,
					ODM_COMP_DIG,
					ODM_DBG_LOUD,
					(
						"odm_DIG(): Abnormal #beacon (%d) case: IGI does one-shot to 0x%x\n",
						pDM_Odm->PhyDbgInfo.NumQryBeaconPkt,
						CurrentIGI
					)
				);
			}
		}
	} else {
		/* 2 Before link */
		ODM_RT_TRACE(
			pDM_Odm,
			ODM_COMP_DIG,
			ODM_DBG_LOUD,
			("odm_DIG(): Adjust IGI before link\n")
		);

		if (FirstDisConnect || bFirstCoverage) {
			CurrentIGI = dm_dig_min;
			ODM_RT_TRACE(
				pDM_Odm,
				ODM_COMP_DIG,
				ODM_DBG_LOUD,
				("odm_DIG(): First disconnect case: IGI does on-shot to lower bound\n")
			);
		} else {
			if (pFalseAlmCnt->Cnt_all > dm_FA_thres[2])
				CurrentIGI = CurrentIGI + 4;
			else if (pFalseAlmCnt->Cnt_all > dm_FA_thres[1])
				CurrentIGI = CurrentIGI + 2;
			else if (pFalseAlmCnt->Cnt_all < dm_FA_thres[0])
				CurrentIGI = CurrentIGI - 2;
		}
	}

	/* 1 Check initial gain by upper/lower bound */
	if (CurrentIGI < pDM_DigTable->rx_gain_range_min)
		CurrentIGI = pDM_DigTable->rx_gain_range_min;

	if (CurrentIGI > pDM_DigTable->rx_gain_range_max)
		CurrentIGI = pDM_DigTable->rx_gain_range_max;

	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_DIG,
		ODM_DBG_LOUD,
		(
			"odm_DIG(): CurIGValue = 0x%x, TotalFA = %d\n\n",
			CurrentIGI,
			pFalseAlmCnt->Cnt_all
		)
	);

	/* 1 Force upper bound and lower bound for adaptivity */
	if (
		pDM_Odm->SupportAbility & ODM_BB_ADAPTIVITY &&
		pDM_Odm->adaptivity_flag == true
	) {
		if (CurrentIGI > Adap_IGI_Upper)
			CurrentIGI = Adap_IGI_Upper;

		if (pDM_Odm->IGI_LowerBound != 0) {
			if (CurrentIGI < pDM_Odm->IGI_LowerBound)
				CurrentIGI = pDM_Odm->IGI_LowerBound;
		}
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Adaptivity case: Force upper bound to 0x%x !!!!!!\n", Adap_IGI_Upper));
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Adaptivity case: Force lower bound to 0x%x !!!!!!\n\n", pDM_Odm->IGI_LowerBound));
	}


	/* 1 Update status */
	if (pDM_Odm->bBtHsOperation) {
		if (pDM_Odm->bLinked) {
			if (pDM_DigTable->BT30_CurIGI > (CurrentIGI))
				ODM_Write_DIG(pDM_Odm, CurrentIGI);
			else
				ODM_Write_DIG(pDM_Odm, pDM_DigTable->BT30_CurIGI);

			pDM_DigTable->bMediaConnect_0 = pDM_Odm->bLinked;
			pDM_DigTable->DIG_Dynamic_MIN_0 = DIG_Dynamic_MIN;
		} else {
			if (pDM_Odm->bLinkInProcess)
				ODM_Write_DIG(pDM_Odm, 0x1c);
			else if (pDM_Odm->bBtConnectProcess)
				ODM_Write_DIG(pDM_Odm, 0x28);
			else
				ODM_Write_DIG(pDM_Odm, pDM_DigTable->BT30_CurIGI);/* ODM_Write_DIG(pDM_Odm, pDM_DigTable->CurIGValue); */
		}
	} else { /*  BT is not using */
		ODM_Write_DIG(pDM_Odm, CurrentIGI);/* ODM_Write_DIG(pDM_Odm, pDM_DigTable->CurIGValue); */
		pDM_DigTable->bMediaConnect_0 = pDM_Odm->bLinked;
		pDM_DigTable->DIG_Dynamic_MIN_0 = DIG_Dynamic_MIN;
	}
}

void odm_DIGbyRSSI_LPS(void *pDM_VOID)
{
	PDM_ODM_T pDM_Odm = (PDM_ODM_T)pDM_VOID;
	Pfalse_ALARM_STATISTICS pFalseAlmCnt = &pDM_Odm->FalseAlmCnt;

	u8 RSSI_Lower = DM_DIG_MIN_NIC;   /* 0x1E or 0x1C */
	u8 CurrentIGI = pDM_Odm->RSSI_Min;

	CurrentIGI = CurrentIGI+RSSI_OFFSET_DIG;

	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_DIG,
		ODM_DBG_LOUD,
		("odm_DIGbyRSSI_LPS() ==>\n")
	);

	/*  Using FW PS mode to make IGI */
	/* Adjust by  FA in LPS MODE */
	if (pFalseAlmCnt->Cnt_all > DM_DIG_FA_TH2_LPS)
		CurrentIGI = CurrentIGI+4;
	else if (pFalseAlmCnt->Cnt_all > DM_DIG_FA_TH1_LPS)
		CurrentIGI = CurrentIGI+2;
	else if (pFalseAlmCnt->Cnt_all < DM_DIG_FA_TH0_LPS)
		CurrentIGI = CurrentIGI-2;


	/* Lower bound checking */

	/* RSSI Lower bound check */
	if ((pDM_Odm->RSSI_Min-10) > DM_DIG_MIN_NIC)
		RSSI_Lower = pDM_Odm->RSSI_Min-10;
	else
		RSSI_Lower = DM_DIG_MIN_NIC;

	/* Upper and Lower Bound checking */
	if (CurrentIGI > DM_DIG_MAX_NIC)
		CurrentIGI = DM_DIG_MAX_NIC;
	else if (CurrentIGI < RSSI_Lower)
		CurrentIGI = RSSI_Lower;


	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_DIG,
		ODM_DBG_LOUD,
		("odm_DIGbyRSSI_LPS(): pFalseAlmCnt->Cnt_all = %d\n", pFalseAlmCnt->Cnt_all)
	);
	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_DIG,
		ODM_DBG_LOUD,
		("odm_DIGbyRSSI_LPS(): pDM_Odm->RSSI_Min = %d\n", pDM_Odm->RSSI_Min)
	);
	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_DIG,
		ODM_DBG_LOUD,
		("odm_DIGbyRSSI_LPS(): CurrentIGI = 0x%x\n", CurrentIGI)
	);

	ODM_Write_DIG(pDM_Odm, CurrentIGI);
	/* ODM_Write_DIG(pDM_Odm, pDM_DigTable->CurIGValue); */
}

/* 3 ============================================================ */
/* 3 FASLE ALARM CHECK */
/* 3 ============================================================ */

void odm_FalseAlarmCounterStatistics(void *pDM_VOID)
{
	PDM_ODM_T pDM_Odm = (PDM_ODM_T)pDM_VOID;
	Pfalse_ALARM_STATISTICS FalseAlmCnt = &(pDM_Odm->FalseAlmCnt);
	u32 ret_value;

	if (!(pDM_Odm->SupportAbility & ODM_BB_FA_CNT))
		return;

	/* hold ofdm counter */
	/* hold page C counter */
	PHY_SetBBReg(pDM_Odm->Adapter, ODM_REG_OFDM_FA_HOLDC_11N, BIT31, 1);
	/* hold page D counter */
	PHY_SetBBReg(pDM_Odm->Adapter, ODM_REG_OFDM_FA_RSTD_11N, BIT31, 1);

	ret_value = PHY_QueryBBReg(
		pDM_Odm->Adapter, ODM_REG_OFDM_FA_TYPE1_11N, bMaskDWord
	);
	FalseAlmCnt->Cnt_Fast_Fsync = (ret_value&0xffff);
	FalseAlmCnt->Cnt_SB_Search_fail = ((ret_value&0xffff0000)>>16);

	ret_value = PHY_QueryBBReg(
		pDM_Odm->Adapter, ODM_REG_OFDM_FA_TYPE2_11N, bMaskDWord
	);
	FalseAlmCnt->Cnt_OFDM_CCA = (ret_value&0xffff);
	FalseAlmCnt->Cnt_Parity_Fail = ((ret_value&0xffff0000)>>16);

	ret_value = PHY_QueryBBReg(
		pDM_Odm->Adapter, ODM_REG_OFDM_FA_TYPE3_11N, bMaskDWord
	);
	FalseAlmCnt->Cnt_Rate_Illegal = (ret_value&0xffff);
	FalseAlmCnt->Cnt_Crc8_fail = ((ret_value&0xffff0000)>>16);

	ret_value = PHY_QueryBBReg(
		pDM_Odm->Adapter, ODM_REG_OFDM_FA_TYPE4_11N, bMaskDWord
	);
	FalseAlmCnt->Cnt_Mcs_fail = (ret_value&0xffff);

	FalseAlmCnt->Cnt_Ofdm_fail =
		FalseAlmCnt->Cnt_Parity_Fail +
		FalseAlmCnt->Cnt_Rate_Illegal +
		FalseAlmCnt->Cnt_Crc8_fail +
		FalseAlmCnt->Cnt_Mcs_fail +
		FalseAlmCnt->Cnt_Fast_Fsync +
		FalseAlmCnt->Cnt_SB_Search_fail;

	{
		/* hold cck counter */
		PHY_SetBBReg(pDM_Odm->Adapter, ODM_REG_CCK_FA_RST_11N, BIT12, 1);
		PHY_SetBBReg(pDM_Odm->Adapter, ODM_REG_CCK_FA_RST_11N, BIT14, 1);

		ret_value = PHY_QueryBBReg(
			pDM_Odm->Adapter, ODM_REG_CCK_FA_LSB_11N, bMaskByte0
		);
		FalseAlmCnt->Cnt_Cck_fail = ret_value;

		ret_value = PHY_QueryBBReg(
			pDM_Odm->Adapter, ODM_REG_CCK_FA_MSB_11N, bMaskByte3
		);
		FalseAlmCnt->Cnt_Cck_fail += (ret_value&0xff)<<8;

		ret_value = PHY_QueryBBReg(
			pDM_Odm->Adapter, ODM_REG_CCK_CCA_CNT_11N, bMaskDWord
		);
		FalseAlmCnt->Cnt_CCK_CCA =
			((ret_value&0xFF)<<8) | ((ret_value&0xFF00)>>8);
	}

	FalseAlmCnt->Cnt_all = (
		FalseAlmCnt->Cnt_Fast_Fsync +
		FalseAlmCnt->Cnt_SB_Search_fail +
		FalseAlmCnt->Cnt_Parity_Fail +
		FalseAlmCnt->Cnt_Rate_Illegal +
		FalseAlmCnt->Cnt_Crc8_fail +
		FalseAlmCnt->Cnt_Mcs_fail +
		FalseAlmCnt->Cnt_Cck_fail
	);

	FalseAlmCnt->Cnt_CCA_all =
		FalseAlmCnt->Cnt_OFDM_CCA + FalseAlmCnt->Cnt_CCK_CCA;

	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_FA_CNT,
		ODM_DBG_LOUD,
		("Enter odm_FalseAlarmCounterStatistics\n")
	);
	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_FA_CNT,
		ODM_DBG_LOUD,
		(
			"Cnt_Fast_Fsync =%d, Cnt_SB_Search_fail =%d\n",
			FalseAlmCnt->Cnt_Fast_Fsync,
			FalseAlmCnt->Cnt_SB_Search_fail
		)
	);
	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_FA_CNT,
		ODM_DBG_LOUD,
		(
			"Cnt_Parity_Fail =%d, Cnt_Rate_Illegal =%d\n",
			FalseAlmCnt->Cnt_Parity_Fail,
			FalseAlmCnt->Cnt_Rate_Illegal
		)
	);
	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_FA_CNT,
		ODM_DBG_LOUD,
		(
			"Cnt_Crc8_fail =%d, Cnt_Mcs_fail =%d\n",
			FalseAlmCnt->Cnt_Crc8_fail,
			FalseAlmCnt->Cnt_Mcs_fail
		)
	);

	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_FA_CNT,
		ODM_DBG_LOUD,
		("Cnt_OFDM_CCA =%d\n", FalseAlmCnt->Cnt_OFDM_CCA)
	);
	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_FA_CNT,
		ODM_DBG_LOUD,
		("Cnt_CCK_CCA =%d\n", FalseAlmCnt->Cnt_CCK_CCA)
	);
	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_FA_CNT,
		ODM_DBG_LOUD,
		("Cnt_CCA_all =%d\n", FalseAlmCnt->Cnt_CCA_all)
	);
	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_FA_CNT,
		ODM_DBG_LOUD,
		("Cnt_Ofdm_fail =%d\n",	FalseAlmCnt->Cnt_Ofdm_fail)
	);
	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_FA_CNT,
		ODM_DBG_LOUD,
		("Cnt_Cck_fail =%d\n",	FalseAlmCnt->Cnt_Cck_fail)
	);
	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_FA_CNT,
		ODM_DBG_LOUD,
		("Cnt_Ofdm_fail =%d\n",	FalseAlmCnt->Cnt_Ofdm_fail)
	);
	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_FA_CNT,
		ODM_DBG_LOUD,
		("Total False Alarm =%d\n",	FalseAlmCnt->Cnt_all)
	);
}


void odm_FAThresholdCheck(
	void *pDM_VOID,
	bool bDFSBand,
	bool bPerformance,
	u32 RxTp,
	u32 TxTp,
	u32 *dm_FA_thres
)
{
	PDM_ODM_T pDM_Odm = (PDM_ODM_T)pDM_VOID;

	if (pDM_Odm->bLinked && (bPerformance || bDFSBand)) {
		/*  For NIC */
		dm_FA_thres[0] = DM_DIG_FA_TH0;
		dm_FA_thres[1] = DM_DIG_FA_TH1;
		dm_FA_thres[2] = DM_DIG_FA_TH2;
	} else {
		dm_FA_thres[0] = 2000;
		dm_FA_thres[1] = 4000;
		dm_FA_thres[2] = 5000;
	}
	return;
}

u8 odm_ForbiddenIGICheck(void *pDM_VOID, u8 DIG_Dynamic_MIN, u8 CurrentIGI)
{
	PDM_ODM_T pDM_Odm = (PDM_ODM_T)pDM_VOID;
	pDIG_T pDM_DigTable = &pDM_Odm->DM_DigTable;
	Pfalse_ALARM_STATISTICS pFalseAlmCnt = &(pDM_Odm->FalseAlmCnt);
	u8 rx_gain_range_min = pDM_DigTable->rx_gain_range_min;

	if (pFalseAlmCnt->Cnt_all > 10000) {
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Abnormally false alarm case.\n"));

		if (pDM_DigTable->LargeFAHit != 3)
			pDM_DigTable->LargeFAHit++;

		/* if (pDM_DigTable->ForbiddenIGI < pDM_DigTable->CurIGValue) */
		if (pDM_DigTable->ForbiddenIGI < CurrentIGI) {
			pDM_DigTable->ForbiddenIGI = CurrentIGI;
			/* pDM_DigTable->ForbiddenIGI = pDM_DigTable->CurIGValue; */
			pDM_DigTable->LargeFAHit = 1;
		}

		if (pDM_DigTable->LargeFAHit >= 3) {
			if ((pDM_DigTable->ForbiddenIGI + 2) > pDM_DigTable->rx_gain_range_max)
				rx_gain_range_min = pDM_DigTable->rx_gain_range_max;
			else
				rx_gain_range_min = (pDM_DigTable->ForbiddenIGI + 2);
			pDM_DigTable->Recover_cnt = 1800;
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Abnormally false alarm case: Recover_cnt = %d\n", pDM_DigTable->Recover_cnt));
		}
	} else {
		if (pDM_DigTable->Recover_cnt != 0) {
			pDM_DigTable->Recover_cnt--;
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Normal Case: Recover_cnt = %d\n", pDM_DigTable->Recover_cnt));
		} else {
			if (pDM_DigTable->LargeFAHit < 3) {
				if ((pDM_DigTable->ForbiddenIGI - 2) < DIG_Dynamic_MIN) { /* DM_DIG_MIN) */
					pDM_DigTable->ForbiddenIGI = DIG_Dynamic_MIN; /* DM_DIG_MIN; */
					rx_gain_range_min = DIG_Dynamic_MIN; /* DM_DIG_MIN; */
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Normal Case: At Lower Bound\n"));
				} else {
					pDM_DigTable->ForbiddenIGI -= 2;
					rx_gain_range_min = (pDM_DigTable->ForbiddenIGI + 2);
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Normal Case: Approach Lower Bound\n"));
				}
			} else
				pDM_DigTable->LargeFAHit = 0;
		}
	}

	return rx_gain_range_min;

}

/* 3 ============================================================ */
/* 3 CCK Packet Detect Threshold */
/* 3 ============================================================ */

void odm_CCKPacketDetectionThresh(void *pDM_VOID)
{
	PDM_ODM_T pDM_Odm = (PDM_ODM_T)pDM_VOID;
	Pfalse_ALARM_STATISTICS FalseAlmCnt = &(pDM_Odm->FalseAlmCnt);
	u8 CurCCK_CCAThres;


	if (
		!(pDM_Odm->SupportAbility & ODM_BB_CCK_PD) ||
		!(pDM_Odm->SupportAbility & ODM_BB_FA_CNT)
	) {
		ODM_RT_TRACE(
			pDM_Odm,
			ODM_COMP_CCK_PD,
			ODM_DBG_LOUD,
			("odm_CCKPacketDetectionThresh()  return ==========\n")
		);
		return;
	}

	if (pDM_Odm->ExtLNA)
		return;

	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_CCK_PD,
		ODM_DBG_LOUD,
		("odm_CCKPacketDetectionThresh()  ==========>\n")
	);

	if (pDM_Odm->bLinked) {
		if (pDM_Odm->RSSI_Min > 25)
			CurCCK_CCAThres = 0xcd;
		else if ((pDM_Odm->RSSI_Min <= 25) && (pDM_Odm->RSSI_Min > 10))
			CurCCK_CCAThres = 0x83;
		else {
			if (FalseAlmCnt->Cnt_Cck_fail > 1000)
				CurCCK_CCAThres = 0x83;
			else
				CurCCK_CCAThres = 0x40;
		}
	} else {
		if (FalseAlmCnt->Cnt_Cck_fail > 1000)
			CurCCK_CCAThres = 0x83;
		else
			CurCCK_CCAThres = 0x40;
	}

	ODM_Write_CCK_CCA_Thres(pDM_Odm, CurCCK_CCAThres);

	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_CCK_PD,
		ODM_DBG_LOUD,
		(
			"odm_CCKPacketDetectionThresh()  CurCCK_CCAThres = 0x%x\n",
			CurCCK_CCAThres
		)
	);
}

void ODM_Write_CCK_CCA_Thres(void *pDM_VOID, u8 CurCCK_CCAThres)
{
	PDM_ODM_T pDM_Odm = (PDM_ODM_T)pDM_VOID;
	pDIG_T pDM_DigTable = &pDM_Odm->DM_DigTable;

	/* modify by Guo.Mingzhi 2012-01-03 */
	if (pDM_DigTable->CurCCK_CCAThres != CurCCK_CCAThres)
		rtw_write8(pDM_Odm->Adapter, ODM_REG(CCK_CCA, pDM_Odm), CurCCK_CCAThres);

	pDM_DigTable->PreCCK_CCAThres = pDM_DigTable->CurCCK_CCAThres;
	pDM_DigTable->CurCCK_CCAThres = CurCCK_CCAThres;
}
