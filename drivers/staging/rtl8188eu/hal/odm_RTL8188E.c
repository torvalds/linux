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
 ******************************************************************************/

#include "odm_precomp.h"
#include "phy.h"

static void dm_rx_hw_antena_div_init(struct odm_dm_struct *dm_odm)
{
	struct adapter *adapter = dm_odm->Adapter;
	u32 value32;

	if (*(dm_odm->mp_mode) == 1) {
		dm_odm->AntDivType = CGCS_RX_SW_ANTDIV;
		phy_set_bb_reg(adapter, ODM_REG_IGI_A_11N, BIT(7), 0);
		phy_set_bb_reg(adapter, ODM_REG_LNA_SWITCH_11N, BIT(31), 1);
		return;
	}

	/* MAC Setting */
	value32 = phy_query_bb_reg(adapter, ODM_REG_ANTSEL_PIN_11N, bMaskDWord);
	phy_set_bb_reg(adapter, ODM_REG_ANTSEL_PIN_11N, bMaskDWord,
		       value32|(BIT(23) | BIT(25)));
	/* Pin Settings */
	phy_set_bb_reg(adapter, ODM_REG_PIN_CTRL_11N, BIT(9) | BIT(8), 0);
	phy_set_bb_reg(adapter, ODM_REG_RX_ANT_CTRL_11N, BIT(10), 0);
	phy_set_bb_reg(adapter, ODM_REG_LNA_SWITCH_11N, BIT(22), 1);
	phy_set_bb_reg(adapter, ODM_REG_LNA_SWITCH_11N, BIT(31), 1);
	/* OFDM Settings */
	phy_set_bb_reg(adapter, ODM_REG_ANTDIV_PARA1_11N, bMaskDWord,
		       0x000000a0);
	/* CCK Settings */
	phy_set_bb_reg(adapter, ODM_REG_BB_PWR_SAV4_11N, BIT(7), 1);
	phy_set_bb_reg(adapter, ODM_REG_CCK_ANTDIV_PARA2_11N, BIT(4), 1);
	rtl88eu_dm_update_rx_idle_ant(dm_odm, MAIN_ANT);
	phy_set_bb_reg(adapter, ODM_REG_ANT_MAPPING1_11N, 0xFFFF, 0x0201);
}

static void dm_trx_hw_antenna_div_init(struct odm_dm_struct *dm_odm)
{
	struct adapter *adapter = dm_odm->Adapter;
	u32	value32;

	if (*(dm_odm->mp_mode) == 1) {
		dm_odm->AntDivType = CGCS_RX_SW_ANTDIV;
		phy_set_bb_reg(adapter, ODM_REG_IGI_A_11N, BIT(7), 0);
		phy_set_bb_reg(adapter, ODM_REG_RX_ANT_CTRL_11N,
			       BIT(5) | BIT(4) | BIT(3), 0);
		return;
	}

	/* MAC Setting */
	value32 = phy_query_bb_reg(adapter, ODM_REG_ANTSEL_PIN_11N, bMaskDWord);
	phy_set_bb_reg(adapter, ODM_REG_ANTSEL_PIN_11N, bMaskDWord,
		       value32|(BIT(23) | BIT(25)));
	/* Pin Settings */
	phy_set_bb_reg(adapter, ODM_REG_PIN_CTRL_11N, BIT(9) | BIT(8), 0);
	phy_set_bb_reg(adapter, ODM_REG_RX_ANT_CTRL_11N, BIT(10), 0);
	phy_set_bb_reg(adapter, ODM_REG_LNA_SWITCH_11N, BIT(22), 0);
	phy_set_bb_reg(adapter, ODM_REG_LNA_SWITCH_11N, BIT(31), 1);
	/* OFDM Settings */
	phy_set_bb_reg(adapter, ODM_REG_ANTDIV_PARA1_11N, bMaskDWord,
		       0x000000a0);
	/* CCK Settings */
	phy_set_bb_reg(adapter, ODM_REG_BB_PWR_SAV4_11N, BIT(7), 1);
	phy_set_bb_reg(adapter, ODM_REG_CCK_ANTDIV_PARA2_11N, BIT(4), 1);
	/* Tx Settings */
	phy_set_bb_reg(adapter, ODM_REG_TX_ANT_CTRL_11N, BIT(21), 0);
	rtl88eu_dm_update_rx_idle_ant(dm_odm, MAIN_ANT);

	/* antenna mapping table */
	if (!dm_odm->bIsMPChip) { /* testchip */
		phy_set_bb_reg(adapter, ODM_REG_RX_DEFUALT_A_11N,
			       BIT(10) | BIT(9) | BIT(8), 1);
		phy_set_bb_reg(adapter, ODM_REG_RX_DEFUALT_A_11N,
			       BIT(13) | BIT(12) | BIT(11), 2);
	} else { /* MPchip */
		phy_set_bb_reg(adapter, ODM_REG_ANT_MAPPING1_11N, bMaskDWord,
			       0x0201);
	}
}

static void dm_fast_training_init(struct odm_dm_struct *dm_odm)
{
	struct adapter *adapter = dm_odm->Adapter;
	u32 value32, i;
	struct fast_ant_train *dm_fat_tbl = &dm_odm->DM_FatTable;
	u32 AntCombination = 2;

	if (*(dm_odm->mp_mode) == 1) {
		return;
	}

	for (i = 0; i < 6; i++) {
		dm_fat_tbl->Bssid[i] = 0;
		dm_fat_tbl->antSumRSSI[i] = 0;
		dm_fat_tbl->antRSSIcnt[i] = 0;
		dm_fat_tbl->antAveRSSI[i] = 0;
	}
	dm_fat_tbl->TrainIdx = 0;
	dm_fat_tbl->FAT_State = FAT_NORMAL_STATE;

	/* MAC Setting */
	value32 = phy_query_bb_reg(adapter, 0x4c, bMaskDWord);
	phy_set_bb_reg(adapter, 0x4c, bMaskDWord, value32|(BIT(23) | BIT(25)));
	value32 = phy_query_bb_reg(adapter,  0x7B4, bMaskDWord);
	phy_set_bb_reg(adapter, 0x7b4, bMaskDWord, value32|(BIT(16) | BIT(17)));

	/* Match MAC ADDR */
	phy_set_bb_reg(adapter, 0x7b4, 0xFFFF, 0);
	phy_set_bb_reg(adapter, 0x7b0, bMaskDWord, 0);

	phy_set_bb_reg(adapter, 0x870, BIT(9) | BIT(8), 0);
	phy_set_bb_reg(adapter, 0x864, BIT(10), 0);
	phy_set_bb_reg(adapter, 0xb2c, BIT(22), 0);
	phy_set_bb_reg(adapter, 0xb2c, BIT(31), 1);
	phy_set_bb_reg(adapter, 0xca4, bMaskDWord, 0x000000a0);

	/* antenna mapping table */
	if (AntCombination == 2) {
		if (!dm_odm->bIsMPChip) { /* testchip */
			phy_set_bb_reg(adapter, 0x858, BIT(10) | BIT(9) | BIT(8), 1);
			phy_set_bb_reg(adapter, 0x858, BIT(13) | BIT(12) | BIT(11), 2);
		} else { /* MPchip */
			phy_set_bb_reg(adapter, 0x914, bMaskByte0, 1);
			phy_set_bb_reg(adapter, 0x914, bMaskByte1, 2);
		}
	} else if (AntCombination == 7) {
		if (!dm_odm->bIsMPChip) { /* testchip */
			phy_set_bb_reg(adapter, 0x858, BIT(10) | BIT(9) | BIT(8), 0);
			phy_set_bb_reg(adapter, 0x858, BIT(13) | BIT(12) | BIT(11), 1);
			phy_set_bb_reg(adapter, 0x878, BIT(16), 0);
			phy_set_bb_reg(adapter, 0x858, BIT(15) | BIT(14), 2);
			phy_set_bb_reg(adapter, 0x878, BIT(19) | BIT(18) | BIT(17), 3);
			phy_set_bb_reg(adapter, 0x878, BIT(22) | BIT(21) | BIT(20), 4);
			phy_set_bb_reg(adapter, 0x878, BIT(25) | BIT(24) | BIT(23), 5);
			phy_set_bb_reg(adapter, 0x878, BIT(28) | BIT(27) | BIT(26), 6);
			phy_set_bb_reg(adapter, 0x878, BIT(31) | BIT(30) | BIT(29), 7);
		} else { /* MPchip */
			phy_set_bb_reg(adapter, 0x914, bMaskByte0, 0);
			phy_set_bb_reg(adapter, 0x914, bMaskByte1, 1);
			phy_set_bb_reg(adapter, 0x914, bMaskByte2, 2);
			phy_set_bb_reg(adapter, 0x914, bMaskByte3, 3);
			phy_set_bb_reg(adapter, 0x918, bMaskByte0, 4);
			phy_set_bb_reg(adapter, 0x918, bMaskByte1, 5);
			phy_set_bb_reg(adapter, 0x918, bMaskByte2, 6);
			phy_set_bb_reg(adapter, 0x918, bMaskByte3, 7);
		}
	}

	/* Default Ant Setting when no fast training */
	phy_set_bb_reg(adapter, 0x80c, BIT(21), 1);
	phy_set_bb_reg(adapter, 0x864, BIT(5) | BIT(4) | BIT(3), 0);
	phy_set_bb_reg(adapter, 0x864, BIT(8) | BIT(7) | BIT(6), 1);

	/* Enter Traing state */
	phy_set_bb_reg(adapter, 0x864, BIT(2) | BIT(1) | BIT(0), (AntCombination-1));
	phy_set_bb_reg(adapter, 0xc50, BIT(7), 1);
}

void rtl88eu_dm_antenna_div_init(struct odm_dm_struct *dm_odm)
{
	if (dm_odm->AntDivType == CGCS_RX_HW_ANTDIV)
		dm_rx_hw_antena_div_init(dm_odm);
	else if (dm_odm->AntDivType == CG_TRX_HW_ANTDIV)
		dm_trx_hw_antenna_div_init(dm_odm);
	else if (dm_odm->AntDivType == CG_TRX_SMART_ANTDIV)
		dm_fast_training_init(dm_odm);
}

void rtl88eu_dm_update_rx_idle_ant(struct odm_dm_struct *dm_odm, u8 ant)
{
	struct fast_ant_train *dm_fat_tbl = &dm_odm->DM_FatTable;
	struct adapter *adapter = dm_odm->Adapter;
	u32 default_ant, optional_ant;

	if (dm_fat_tbl->RxIdleAnt != ant) {
		if (ant == MAIN_ANT) {
			default_ant = (dm_odm->AntDivType == CG_TRX_HW_ANTDIV) ?
				       MAIN_ANT_CG_TRX : MAIN_ANT_CGCS_RX;
			optional_ant = (dm_odm->AntDivType == CG_TRX_HW_ANTDIV) ?
					AUX_ANT_CG_TRX : AUX_ANT_CGCS_RX;
		} else {
			default_ant = (dm_odm->AntDivType == CG_TRX_HW_ANTDIV) ?
				       AUX_ANT_CG_TRX : AUX_ANT_CGCS_RX;
			optional_ant = (dm_odm->AntDivType == CG_TRX_HW_ANTDIV) ?
					MAIN_ANT_CG_TRX : MAIN_ANT_CGCS_RX;
		}

		if (dm_odm->AntDivType == CG_TRX_HW_ANTDIV) {
			phy_set_bb_reg(adapter, ODM_REG_RX_ANT_CTRL_11N,
				       BIT(5) | BIT(4) | BIT(3), default_ant);
			phy_set_bb_reg(adapter, ODM_REG_RX_ANT_CTRL_11N,
				       BIT(8) | BIT(7) | BIT(6), optional_ant);
			phy_set_bb_reg(adapter, ODM_REG_ANTSEL_CTRL_11N,
				       BIT(14) | BIT(13) | BIT(12), default_ant);
			phy_set_bb_reg(adapter, ODM_REG_RESP_TX_11N,
				       BIT(6) | BIT(7), default_ant);
		} else if (dm_odm->AntDivType == CGCS_RX_HW_ANTDIV) {
			phy_set_bb_reg(adapter, ODM_REG_RX_ANT_CTRL_11N,
				       BIT(5) | BIT(4) | BIT(3), default_ant);
			phy_set_bb_reg(adapter, ODM_REG_RX_ANT_CTRL_11N,
				       BIT(8) | BIT(7) | BIT(6), optional_ant);
		}
	}
	dm_fat_tbl->RxIdleAnt = ant;
}

static void update_tx_ant_88eu(struct odm_dm_struct *dm_odm, u8 ant, u32 mac_id)
{
	struct fast_ant_train *dm_fat_tbl = &dm_odm->DM_FatTable;
	u8 target_ant;

	if (ant == MAIN_ANT)
		target_ant = MAIN_ANT_CG_TRX;
	else
		target_ant = AUX_ANT_CG_TRX;
	dm_fat_tbl->antsel_a[mac_id] = target_ant & BIT(0);
	dm_fat_tbl->antsel_b[mac_id] = (target_ant & BIT(1))>>1;
	dm_fat_tbl->antsel_c[mac_id] = (target_ant & BIT(2))>>2;
}

void rtl88eu_dm_set_tx_ant_by_tx_info(struct odm_dm_struct *dm_odm,
				      u8 *desc, u8 mac_id)
{
	struct fast_ant_train *dm_fat_tbl = &dm_odm->DM_FatTable;

	if ((dm_odm->AntDivType == CG_TRX_HW_ANTDIV) ||
	    (dm_odm->AntDivType == CG_TRX_SMART_ANTDIV)) {
		SET_TX_DESC_ANTSEL_A_88E(desc, dm_fat_tbl->antsel_a[mac_id]);
		SET_TX_DESC_ANTSEL_B_88E(desc, dm_fat_tbl->antsel_b[mac_id]);
		SET_TX_DESC_ANTSEL_C_88E(desc, dm_fat_tbl->antsel_c[mac_id]);
	}
}

void rtl88eu_dm_ant_sel_statistics(struct odm_dm_struct *dm_odm,
				   u8 antsel_tr_mux, u32 mac_id, u8 rx_pwdb_all)
{
	struct fast_ant_train *dm_fat_tbl = &dm_odm->DM_FatTable;

	if (dm_odm->AntDivType == CG_TRX_HW_ANTDIV) {
		if (antsel_tr_mux == MAIN_ANT_CG_TRX) {
			dm_fat_tbl->MainAnt_Sum[mac_id] += rx_pwdb_all;
			dm_fat_tbl->MainAnt_Cnt[mac_id]++;
		} else {
			dm_fat_tbl->AuxAnt_Sum[mac_id] += rx_pwdb_all;
			dm_fat_tbl->AuxAnt_Cnt[mac_id]++;
		}
	} else if (dm_odm->AntDivType == CGCS_RX_HW_ANTDIV) {
		if (antsel_tr_mux == MAIN_ANT_CGCS_RX) {
			dm_fat_tbl->MainAnt_Sum[mac_id] += rx_pwdb_all;
			dm_fat_tbl->MainAnt_Cnt[mac_id]++;
		} else {
			dm_fat_tbl->AuxAnt_Sum[mac_id] += rx_pwdb_all;
			dm_fat_tbl->AuxAnt_Cnt[mac_id]++;
		}
	}
}

static void rtl88eu_dm_hw_ant_div(struct odm_dm_struct *dm_odm)
{
	struct fast_ant_train *dm_fat_tbl = &dm_odm->DM_FatTable;
	struct rtw_dig *dig_table = &dm_odm->DM_DigTable;
	struct sta_info *entry;
	u32 i, min_rssi = 0xFF, ant_div_max_rssi = 0, max_rssi = 0;
	u32 local_min_rssi, local_max_rssi;
	u32 main_rssi, aux_rssi;
	u8 RxIdleAnt = 0, target_ant = 7;

	for (i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++) {
		entry = dm_odm->pODM_StaInfo[i];
		if (IS_STA_VALID(entry)) {
			/* 2 Caculate RSSI per Antenna */
			main_rssi = (dm_fat_tbl->MainAnt_Cnt[i] != 0) ?
				     (dm_fat_tbl->MainAnt_Sum[i]/dm_fat_tbl->MainAnt_Cnt[i]) : 0;
			aux_rssi = (dm_fat_tbl->AuxAnt_Cnt[i] != 0) ?
				    (dm_fat_tbl->AuxAnt_Sum[i]/dm_fat_tbl->AuxAnt_Cnt[i]) : 0;
			target_ant = (main_rssi >= aux_rssi) ? MAIN_ANT : AUX_ANT;
			/* 2 Select max_rssi for DIG */
			local_max_rssi = max(main_rssi, aux_rssi);
			if ((local_max_rssi > ant_div_max_rssi) &&
			    (local_max_rssi < 40))
				ant_div_max_rssi = local_max_rssi;
			if (local_max_rssi > max_rssi)
				max_rssi = local_max_rssi;

			/* 2 Select RX Idle Antenna */
			if ((dm_fat_tbl->RxIdleAnt == MAIN_ANT) &&
			    (main_rssi == 0))
				main_rssi = aux_rssi;
			else if ((dm_fat_tbl->RxIdleAnt == AUX_ANT) &&
				 (aux_rssi == 0))
				aux_rssi = main_rssi;

			local_min_rssi = min(main_rssi, aux_rssi);
			if (local_min_rssi < min_rssi) {
				min_rssi = local_min_rssi;
				RxIdleAnt = target_ant;
			}
			/* 2 Select TRX Antenna */
			if (dm_odm->AntDivType == CG_TRX_HW_ANTDIV)
				update_tx_ant_88eu(dm_odm, target_ant, i);
		}
		dm_fat_tbl->MainAnt_Sum[i] = 0;
		dm_fat_tbl->AuxAnt_Sum[i] = 0;
		dm_fat_tbl->MainAnt_Cnt[i] = 0;
		dm_fat_tbl->AuxAnt_Cnt[i] = 0;
	}

	/* 2 Set RX Idle Antenna */
	rtl88eu_dm_update_rx_idle_ant(dm_odm, RxIdleAnt);

	dig_table->AntDiv_RSSI_max = ant_div_max_rssi;
	dig_table->RSSI_max = max_rssi;
}

void rtl88eu_dm_antenna_diversity(struct odm_dm_struct *dm_odm)
{
	struct fast_ant_train *dm_fat_tbl = &dm_odm->DM_FatTable;
	struct adapter *adapter = dm_odm->Adapter;

	if (!(dm_odm->SupportAbility & ODM_BB_ANT_DIV))
		return;
	if (!dm_odm->bLinked) {
		ODM_RT_TRACE(dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD,
			     ("ODM_AntennaDiversity_88E(): No Link.\n"));
		if (dm_fat_tbl->bBecomeLinked) {
			ODM_RT_TRACE(dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD,
				     ("Need to Turn off HW AntDiv\n"));
			phy_set_bb_reg(adapter, ODM_REG_IGI_A_11N, BIT(7), 0);
			phy_set_bb_reg(adapter, ODM_REG_CCK_ANTDIV_PARA1_11N,
				       BIT(15), 0);
			if (dm_odm->AntDivType == CG_TRX_HW_ANTDIV)
				phy_set_bb_reg(adapter, ODM_REG_TX_ANT_CTRL_11N,
					       BIT(21), 0);
			dm_fat_tbl->bBecomeLinked = dm_odm->bLinked;
		}
		return;
	} else {
		if (!dm_fat_tbl->bBecomeLinked) {
			ODM_RT_TRACE(dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD,
				     ("Need to Turn on HW AntDiv\n"));
			phy_set_bb_reg(adapter, ODM_REG_IGI_A_11N, BIT(7), 1);
			phy_set_bb_reg(adapter, ODM_REG_CCK_ANTDIV_PARA1_11N,
				       BIT(15), 1);
			if (dm_odm->AntDivType == CG_TRX_HW_ANTDIV)
				phy_set_bb_reg(adapter, ODM_REG_TX_ANT_CTRL_11N,
					       BIT(21), 1);
			dm_fat_tbl->bBecomeLinked = dm_odm->bLinked;
		}
	}
	if ((dm_odm->AntDivType == CG_TRX_HW_ANTDIV) ||
	    (dm_odm->AntDivType == CGCS_RX_HW_ANTDIV))
		rtl88eu_dm_hw_ant_div(dm_odm);
}
