// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#include "../include/drv_types.h"

static void odm_RX_HWAntDivInit(struct odm_dm_struct *dm_odm)
{
	struct adapter *adapter = dm_odm->Adapter;
	u32	value32;

	/* MAC Setting */
	value32 = rtl8188e_PHY_QueryBBReg(adapter, ODM_REG_ANTSEL_PIN_11N, bMaskDWord);
	rtl8188e_PHY_SetBBReg(adapter, ODM_REG_ANTSEL_PIN_11N, bMaskDWord, value32 | (BIT(23) | BIT(25))); /* Reg4C[25]=1, Reg4C[23]=1 for pin output */
	/* Pin Settings */
	rtl8188e_PHY_SetBBReg(adapter, ODM_REG_PIN_CTRL_11N, BIT(9) | BIT(8), 0);/* Reg870[8]=1'b0, Reg870[9]=1'b0	antsel antselb by HW */
	rtl8188e_PHY_SetBBReg(adapter, ODM_REG_RX_ANT_CTRL_11N, BIT(10), 0);	/* Reg864[10]=1'b0	antsel2 by HW */
	rtl8188e_PHY_SetBBReg(adapter, ODM_REG_LNA_SWITCH_11N, BIT(22), 1);	/* Regb2c[22]=1'b0	disable CS/CG switch */
	rtl8188e_PHY_SetBBReg(adapter, ODM_REG_LNA_SWITCH_11N, BIT(31), 1);	/* Regb2c[31]=1'b1	output at CG only */
	/* OFDM Settings */
	rtl8188e_PHY_SetBBReg(adapter, ODM_REG_ANTDIV_PARA1_11N, bMaskDWord, 0x000000a0);
	/* CCK Settings */
	rtl8188e_PHY_SetBBReg(adapter, ODM_REG_BB_PWR_SAV4_11N, BIT(7), 1); /* Fix CCK PHY status report issue */
	rtl8188e_PHY_SetBBReg(adapter, ODM_REG_CCK_ANTDIV_PARA2_11N, BIT(4), 1); /* CCK complete HW AntDiv within 64 samples */
	ODM_UpdateRxIdleAnt_88E(dm_odm, MAIN_ANT);
	rtl8188e_PHY_SetBBReg(adapter, ODM_REG_ANT_MAPPING1_11N, 0xFFFF, 0x0201);	/* antenna mapping table */
}

static void odm_TRX_HWAntDivInit(struct odm_dm_struct *dm_odm)
{
	struct adapter *adapter = dm_odm->Adapter;
	u32	value32;

	/* MAC Setting */
	value32 = rtl8188e_PHY_QueryBBReg(adapter, ODM_REG_ANTSEL_PIN_11N, bMaskDWord);
	rtl8188e_PHY_SetBBReg(adapter, ODM_REG_ANTSEL_PIN_11N, bMaskDWord, value32 | (BIT(23) | BIT(25))); /* Reg4C[25]=1, Reg4C[23]=1 for pin output */
	/* Pin Settings */
	rtl8188e_PHY_SetBBReg(adapter, ODM_REG_PIN_CTRL_11N, BIT(9) | BIT(8), 0);/* Reg870[8]=1'b0, Reg870[9]=1'b0		antsel antselb by HW */
	rtl8188e_PHY_SetBBReg(adapter, ODM_REG_RX_ANT_CTRL_11N, BIT(10), 0);	/* Reg864[10]=1'b0	antsel2 by HW */
	rtl8188e_PHY_SetBBReg(adapter, ODM_REG_LNA_SWITCH_11N, BIT(22), 0);	/* Regb2c[22]=1'b0	disable CS/CG switch */
	rtl8188e_PHY_SetBBReg(adapter, ODM_REG_LNA_SWITCH_11N, BIT(31), 1);	/* Regb2c[31]=1'b1	output at CG only */
	/* OFDM Settings */
	rtl8188e_PHY_SetBBReg(adapter, ODM_REG_ANTDIV_PARA1_11N, bMaskDWord, 0x000000a0);
	/* CCK Settings */
	rtl8188e_PHY_SetBBReg(adapter, ODM_REG_BB_PWR_SAV4_11N, BIT(7), 1); /* Fix CCK PHY status report issue */
	rtl8188e_PHY_SetBBReg(adapter, ODM_REG_CCK_ANTDIV_PARA2_11N, BIT(4), 1); /* CCK complete HW AntDiv within 64 samples */
	/* Tx Settings */
	rtl8188e_PHY_SetBBReg(adapter, ODM_REG_TX_ANT_CTRL_11N, BIT(21), 0); /* Reg80c[21]=1'b0		from TX Reg */
	ODM_UpdateRxIdleAnt_88E(dm_odm, MAIN_ANT);

	/* antenna mapping table */
	if (!dm_odm->bIsMPChip) { /* testchip */
		rtl8188e_PHY_SetBBReg(adapter, ODM_REG_RX_DEFUALT_A_11N, BIT(10) | BIT(9) | BIT(8), 1);	/* Reg858[10:8]=3'b001 */
		rtl8188e_PHY_SetBBReg(adapter, ODM_REG_RX_DEFUALT_A_11N, BIT(13) | BIT(12) | BIT(11), 2);	/* Reg858[13:11]=3'b010 */
	} else { /* MPchip */
		rtl8188e_PHY_SetBBReg(adapter, ODM_REG_ANT_MAPPING1_11N, bMaskDWord, 0x0201);	/* Reg914=3'b010, Reg915=3'b001 */
	}
}

static void odm_FastAntTrainingInit(struct odm_dm_struct *dm_odm)
{
	struct adapter *adapter = dm_odm->Adapter;
	u32	value32;

	/* MAC Setting */
	value32 = rtl8188e_PHY_QueryBBReg(adapter, 0x4c, bMaskDWord);
	rtl8188e_PHY_SetBBReg(adapter, 0x4c, bMaskDWord, value32 | (BIT(23) | BIT(25))); /* Reg4C[25]=1, Reg4C[23]=1 for pin output */
	value32 = rtl8188e_PHY_QueryBBReg(adapter,  0x7B4, bMaskDWord);
	rtl8188e_PHY_SetBBReg(adapter, 0x7b4, bMaskDWord, value32 | (BIT(16) | BIT(17))); /* Reg7B4[16]=1 enable antenna training, Reg7B4[17]=1 enable A2 match */

	/* Match MAC ADDR */
	rtl8188e_PHY_SetBBReg(adapter, 0x7b4, 0xFFFF, 0);
	rtl8188e_PHY_SetBBReg(adapter, 0x7b0, bMaskDWord, 0);

	rtl8188e_PHY_SetBBReg(adapter, 0x870, BIT(9) | BIT(8), 0);/* Reg870[8]=1'b0, Reg870[9]=1'b0		antsel antselb by HW */
	rtl8188e_PHY_SetBBReg(adapter, 0x864, BIT(10), 0);	/* Reg864[10]=1'b0	antsel2 by HW */
	rtl8188e_PHY_SetBBReg(adapter, 0xb2c, BIT(22), 0);	/* Regb2c[22]=1'b0	disable CS/CG switch */
	rtl8188e_PHY_SetBBReg(adapter, 0xb2c, BIT(31), 1);	/* Regb2c[31]=1'b1	output at CG only */
	rtl8188e_PHY_SetBBReg(adapter, 0xca4, bMaskDWord, 0x000000a0);

	if (!dm_odm->bIsMPChip) { /* testchip */
		rtl8188e_PHY_SetBBReg(adapter, 0x858, BIT(10) | BIT(9) | BIT(8), 1);	/* Reg858[10:8]=3'b001 */
		rtl8188e_PHY_SetBBReg(adapter, 0x858, BIT(13) | BIT(12) | BIT(11), 2);	/* Reg858[13:11]=3'b010 */
	} else { /* MPchip */
		rtl8188e_PHY_SetBBReg(adapter, 0x914, bMaskByte0, 1);
		rtl8188e_PHY_SetBBReg(adapter, 0x914, bMaskByte1, 2);
	}

	/* Default Ant Setting when no fast training */
	rtl8188e_PHY_SetBBReg(adapter, 0x80c, BIT(21), 1); /* Reg80c[21]=1'b1		from TX Info */
	rtl8188e_PHY_SetBBReg(adapter, 0x864, BIT(5) | BIT(4) | BIT(3), 0);	/* Default RX */
	rtl8188e_PHY_SetBBReg(adapter, 0x864, BIT(8) | BIT(7) | BIT(6), 1);	/* Optional RX */

	/* Enter Training state */
	rtl8188e_PHY_SetBBReg(adapter, 0x864, BIT(2) | BIT(1) | BIT(0), 1);
	rtl8188e_PHY_SetBBReg(adapter, 0xc50, BIT(7), 1);	/* RegC50[7]=1'b1		enable HW AntDiv */
}

void ODM_AntennaDiversityInit_88E(struct odm_dm_struct *dm_odm)
{
	if (dm_odm->AntDivType == CGCS_RX_HW_ANTDIV)
		odm_RX_HWAntDivInit(dm_odm);
	else if (dm_odm->AntDivType == CG_TRX_HW_ANTDIV)
		odm_TRX_HWAntDivInit(dm_odm);
	else if (dm_odm->AntDivType == CG_TRX_SMART_ANTDIV)
		odm_FastAntTrainingInit(dm_odm);
}

void ODM_UpdateRxIdleAnt_88E(struct odm_dm_struct *dm_odm, u8 Ant)
{
	struct fast_ant_train *dm_fat_tbl = &dm_odm->DM_FatTable;
	struct adapter *adapter = dm_odm->Adapter;
	u32	DefaultAnt, OptionalAnt;

	if (dm_fat_tbl->RxIdleAnt != Ant) {
		if (Ant == MAIN_ANT) {
			DefaultAnt = (dm_odm->AntDivType == CG_TRX_HW_ANTDIV) ? MAIN_ANT_CG_TRX : MAIN_ANT_CGCS_RX;
			OptionalAnt = (dm_odm->AntDivType == CG_TRX_HW_ANTDIV) ? AUX_ANT_CG_TRX : AUX_ANT_CGCS_RX;
		} else {
			DefaultAnt = (dm_odm->AntDivType == CG_TRX_HW_ANTDIV) ? AUX_ANT_CG_TRX : AUX_ANT_CGCS_RX;
			OptionalAnt = (dm_odm->AntDivType == CG_TRX_HW_ANTDIV) ? MAIN_ANT_CG_TRX : MAIN_ANT_CGCS_RX;
		}

		if (dm_odm->AntDivType == CG_TRX_HW_ANTDIV) {
			rtl8188e_PHY_SetBBReg(adapter, ODM_REG_RX_ANT_CTRL_11N, BIT(5) | BIT(4) | BIT(3), DefaultAnt);	/* Default RX */
			rtl8188e_PHY_SetBBReg(adapter, ODM_REG_RX_ANT_CTRL_11N, BIT(8) | BIT(7) | BIT(6), OptionalAnt);		/* Optional RX */
			rtl8188e_PHY_SetBBReg(adapter, ODM_REG_ANTSEL_CTRL_11N, BIT(14) | BIT(13) | BIT(12), DefaultAnt);	/* Default TX */
			rtl8188e_PHY_SetBBReg(adapter, ODM_REG_RESP_TX_11N, BIT(6) | BIT(7), DefaultAnt);	/* Resp Tx */
		} else if (dm_odm->AntDivType == CGCS_RX_HW_ANTDIV) {
			rtl8188e_PHY_SetBBReg(adapter, ODM_REG_RX_ANT_CTRL_11N, BIT(5) | BIT(4) | BIT(3), DefaultAnt);	/* Default RX */
			rtl8188e_PHY_SetBBReg(adapter, ODM_REG_RX_ANT_CTRL_11N, BIT(8) | BIT(7) | BIT(6), OptionalAnt);		/* Optional RX */
		}
	}
	dm_fat_tbl->RxIdleAnt = Ant;
	if (Ant != MAIN_ANT)
		pr_info("RxIdleAnt=AUX_ANT\n");
}

static void odm_UpdateTxAnt_88E(struct odm_dm_struct *dm_odm, u8 Ant, u32 MacId)
{
	struct fast_ant_train *dm_fat_tbl = &dm_odm->DM_FatTable;
	u8	TargetAnt;

	if (Ant == MAIN_ANT)
		TargetAnt = MAIN_ANT_CG_TRX;
	else
		TargetAnt = AUX_ANT_CG_TRX;
	dm_fat_tbl->antsel_a[MacId] = TargetAnt & BIT(0);
	dm_fat_tbl->antsel_b[MacId] = (TargetAnt & BIT(1)) >> 1;
	dm_fat_tbl->antsel_c[MacId] = (TargetAnt & BIT(2)) >> 2;
}

void ODM_SetTxAntByTxInfo_88E(struct odm_dm_struct *dm_odm, u8 *pDesc, u8 macId)
{
	struct fast_ant_train *dm_fat_tbl = &dm_odm->DM_FatTable;

	if ((dm_odm->AntDivType == CG_TRX_HW_ANTDIV) || (dm_odm->AntDivType == CG_TRX_SMART_ANTDIV)) {
		SET_TX_DESC_ANTSEL_A_88E(pDesc, dm_fat_tbl->antsel_a[macId]);
		SET_TX_DESC_ANTSEL_B_88E(pDesc, dm_fat_tbl->antsel_b[macId]);
		SET_TX_DESC_ANTSEL_C_88E(pDesc, dm_fat_tbl->antsel_c[macId]);
	}
}

void ODM_AntselStatistics_88E(struct odm_dm_struct *dm_odm, u8 antsel_tr_mux, u32 MacId, u8 RxPWDBAll)
{
	struct fast_ant_train *dm_fat_tbl = &dm_odm->DM_FatTable;
	if (dm_odm->AntDivType == CG_TRX_HW_ANTDIV) {
		if (antsel_tr_mux == MAIN_ANT_CG_TRX) {
			dm_fat_tbl->MainAnt_Sum[MacId] += RxPWDBAll;
			dm_fat_tbl->MainAnt_Cnt[MacId]++;
		} else {
			dm_fat_tbl->AuxAnt_Sum[MacId] += RxPWDBAll;
			dm_fat_tbl->AuxAnt_Cnt[MacId]++;
		}
	} else if (dm_odm->AntDivType == CGCS_RX_HW_ANTDIV) {
		if (antsel_tr_mux == MAIN_ANT_CGCS_RX) {
			dm_fat_tbl->MainAnt_Sum[MacId] += RxPWDBAll;
			dm_fat_tbl->MainAnt_Cnt[MacId]++;
		} else {
			dm_fat_tbl->AuxAnt_Sum[MacId] += RxPWDBAll;
			dm_fat_tbl->AuxAnt_Cnt[MacId]++;
		}
	}
}

static void odm_HWAntDiv(struct odm_dm_struct *dm_odm)
{
	u32	i, MinRSSI = 0xFF, AntDivMaxRSSI = 0, MaxRSSI = 0, LocalMinRSSI, LocalMaxRSSI;
	u32	Main_RSSI, Aux_RSSI;
	u8	RxIdleAnt = 0, TargetAnt = 7;
	struct fast_ant_train *dm_fat_tbl = &dm_odm->DM_FatTable;
	struct rtw_dig *pDM_DigTable = &dm_odm->DM_DigTable;
	struct sta_info *pEntry;

	for (i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++) {
		pEntry = dm_odm->pODM_StaInfo[i];
		if (IS_STA_VALID(pEntry)) {
			/* 2 Caculate RSSI per Antenna */
			Main_RSSI = (dm_fat_tbl->MainAnt_Cnt[i] != 0) ? (dm_fat_tbl->MainAnt_Sum[i] / dm_fat_tbl->MainAnt_Cnt[i]) : 0;
			Aux_RSSI = (dm_fat_tbl->AuxAnt_Cnt[i] != 0) ? (dm_fat_tbl->AuxAnt_Sum[i] / dm_fat_tbl->AuxAnt_Cnt[i]) : 0;
			TargetAnt = (Main_RSSI >= Aux_RSSI) ? MAIN_ANT : AUX_ANT;
			/* 2 Select MaxRSSI for DIG */
			LocalMaxRSSI = (Main_RSSI > Aux_RSSI) ? Main_RSSI : Aux_RSSI;
			if ((LocalMaxRSSI > AntDivMaxRSSI) && (LocalMaxRSSI < 40))
				AntDivMaxRSSI = LocalMaxRSSI;
			if (LocalMaxRSSI > MaxRSSI)
				MaxRSSI = LocalMaxRSSI;

			/* 2 Select RX Idle Antenna */
			if ((dm_fat_tbl->RxIdleAnt == MAIN_ANT) && (Main_RSSI == 0))
				Main_RSSI = Aux_RSSI;
			else if ((dm_fat_tbl->RxIdleAnt == AUX_ANT) && (Aux_RSSI == 0))
				Aux_RSSI = Main_RSSI;

			LocalMinRSSI = (Main_RSSI > Aux_RSSI) ? Aux_RSSI : Main_RSSI;
			if (LocalMinRSSI < MinRSSI) {
				MinRSSI = LocalMinRSSI;
				RxIdleAnt = TargetAnt;
			}
			/* 2 Select TRX Antenna */
			if (dm_odm->AntDivType == CG_TRX_HW_ANTDIV)
				odm_UpdateTxAnt_88E(dm_odm, TargetAnt, i);
		}
		dm_fat_tbl->MainAnt_Sum[i] = 0;
		dm_fat_tbl->AuxAnt_Sum[i] = 0;
		dm_fat_tbl->MainAnt_Cnt[i] = 0;
		dm_fat_tbl->AuxAnt_Cnt[i] = 0;
	}

	/* 2 Set RX Idle Antenna */
	ODM_UpdateRxIdleAnt_88E(dm_odm, RxIdleAnt);

	pDM_DigTable->AntDiv_RSSI_max = AntDivMaxRSSI;
	pDM_DigTable->RSSI_max = MaxRSSI;
}

void ODM_AntennaDiversity_88E(struct odm_dm_struct *dm_odm)
{
	struct fast_ant_train *dm_fat_tbl = &dm_odm->DM_FatTable;
	struct adapter *adapter = dm_odm->Adapter;

	if (!(dm_odm->SupportAbility & ODM_BB_ANT_DIV))
		return;
	if (!dm_odm->bLinked) {
		if (dm_fat_tbl->bBecomeLinked) {
			rtl8188e_PHY_SetBBReg(adapter, ODM_REG_IGI_A_11N, BIT(7), 0);	/* RegC50[7]=1'b1		enable HW AntDiv */
			rtl8188e_PHY_SetBBReg(adapter, ODM_REG_CCK_ANTDIV_PARA1_11N, BIT(15), 0); /* Enable CCK AntDiv */
			if (dm_odm->AntDivType == CG_TRX_HW_ANTDIV)
				rtl8188e_PHY_SetBBReg(adapter, ODM_REG_TX_ANT_CTRL_11N, BIT(21), 0); /* Reg80c[21]=1'b0		from TX Reg */
			dm_fat_tbl->bBecomeLinked = dm_odm->bLinked;
		}
		return;
	} else {
		if (!dm_fat_tbl->bBecomeLinked) {
			/* Because HW AntDiv is disabled before Link, we enable HW AntDiv after link */
			rtl8188e_PHY_SetBBReg(adapter, ODM_REG_IGI_A_11N, BIT(7), 1);	/* RegC50[7]=1'b1		enable HW AntDiv */
			rtl8188e_PHY_SetBBReg(adapter, ODM_REG_CCK_ANTDIV_PARA1_11N, BIT(15), 1); /* Enable CCK AntDiv */
			if (dm_odm->AntDivType == CG_TRX_HW_ANTDIV)
				rtl8188e_PHY_SetBBReg(adapter, ODM_REG_TX_ANT_CTRL_11N, BIT(21), 1); /* Reg80c[21]=1'b1		from TX Info */
			dm_fat_tbl->bBecomeLinked = dm_odm->bLinked;
		}
	}
	if ((dm_odm->AntDivType == CG_TRX_HW_ANTDIV) || (dm_odm->AntDivType == CGCS_RX_HW_ANTDIV))
		odm_HWAntDiv(dm_odm);
}
