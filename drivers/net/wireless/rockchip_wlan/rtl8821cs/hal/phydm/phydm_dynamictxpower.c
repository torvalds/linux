/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2017  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

/*************************************************************
 * include files
 ************************************************************/
#include "mp_precomp.h"
#include "phydm_precomp.h"

#ifdef CONFIG_DYNAMIC_TX_TWR
#ifdef BB_RAM_SUPPORT
void phydm_rd_reg_pwr(void *dm_void, u32 *_used, char *output, u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 used = *_used;
	u32 out_len = *_out_len;
	boolean pwr_ofst0_en = false;
	boolean pwr_ofst1_en = false;
	s8 pwr_ofst0 = 0;
	s8 pwr_ofst1 = 0;

	pwr_ofst0_en = (boolean)odm_get_bb_reg(dm, R_0x1e70, BIT(23));
	pwr_ofst1_en = (boolean)odm_get_bb_reg(dm, R_0x1e70, BIT(31));
	pwr_ofst0 = (s8)odm_get_bb_reg(dm, R_0x1e70, 0x7f0000);
	pwr_ofst1 = (s8)odm_get_bb_reg(dm, R_0x1e70, 0x7f000000);

	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "reg0: en:%d, pwr_ofst:0x%x, reg1: en:%d, pwr_ofst:0x%x\n",
		 pwr_ofst0_en, pwr_ofst0, pwr_ofst1_en, pwr_ofst1);

	*_used = used;
	*_out_len = out_len;
};

void phydm_wt_reg_pwr(void *dm_void, boolean is_ofst1, boolean pwr_ofst_en,
		      s8 pwr_ofst)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_bb_ram_ctrl *bb_ctrl = &dm->p_bb_ram_ctrl;
	u8 reg_0x1e70 = 0;

	if (!is_ofst1) {
		bb_ctrl->tx_pwr_ofst_reg0_en = pwr_ofst_en;
		bb_ctrl->tx_pwr_ofst_reg0 = pwr_ofst;

		reg_0x1e70 |= (pwr_ofst_en << 7) + (pwr_ofst & 0x7f);
		odm_set_bb_reg(dm, R_0x1e70, 0x00ff0000, reg_0x1e70);
	} else {
		bb_ctrl->tx_pwr_ofst_reg1_en = pwr_ofst_en;
		bb_ctrl->tx_pwr_ofst_reg1 = pwr_ofst;

		reg_0x1e70 |= (pwr_ofst_en << 7) + (pwr_ofst & 0x7f);
		odm_set_bb_reg(dm, R_0x1e70, 0xff000000, reg_0x1e70);
	}
};

void phydm_rd_ram_pwr(void *dm_void, u8 macid, u32 *_used, char *output,
		      u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 used = *_used;
	u32 out_len = *_out_len;
	boolean pwr_ofst0_en = false;
	boolean pwr_ofst1_en = false;
	s8 pwr_ofst0 = 0;
	s8 pwr_ofst1 = 0;
	u32 reg_0x1e84 = 0;

	reg_0x1e84 |= (macid & 0x3f) << 24; /* macid*/
	reg_0x1e84 |= BIT(31); /* read_en*/
	odm_set_bb_reg(dm, R_0x1e84, MASKDWORD, reg_0x1e84);

	pwr_ofst0_en = (boolean)odm_get_bb_reg(dm, R_0x2de8, BIT(23));
	pwr_ofst1_en = (boolean)odm_get_bb_reg(dm, R_0x2de8, BIT(31));
	pwr_ofst0 = (s8)odm_get_bb_reg(dm, R_0x2de8, 0x7f0000);
	pwr_ofst1 = (s8)odm_get_bb_reg(dm, R_0x2de8, 0x7f000000);
	odm_set_bb_reg(dm, R_0x1e84, MASKDWORD, 0x0); /* disable rd/wt*/

	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "(macid:%d) ram0: en:%d, pwr_ofst:0x%x, ram1: en:%d, pwr_ofst:0x%x\n",
		 macid, pwr_ofst0_en, pwr_ofst0, pwr_ofst1_en, pwr_ofst1);

	*_used = used;
	*_out_len = out_len;
};

void phydm_wt_ram_pwr(void *dm_void, u8 macid, boolean is_ofst1,
		      boolean pwr_ofst_en, s8 pwr_ofst)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_bb_ram_per_sta *dm_ram_per_sta = NULL;
	u32 reg_0x1e84 = 0;
	boolean pwr_ofst_ano_en = false;
	s8 pwr_ofst_ano = 0;

	if (macid > 63)
		macid = 63;

	dm_ram_per_sta = &dm->p_bb_ram_ctrl.pram_sta_ctrl[macid];
	reg_0x1e84 = (dm_ram_per_sta->hw_igi_en << 7) + dm_ram_per_sta->hw_igi;
	if (!is_ofst1) {
		dm_ram_per_sta->tx_pwr_offset0_en = pwr_ofst_en;
		dm_ram_per_sta->tx_pwr_offset0 = pwr_ofst;

		pwr_ofst_ano_en = dm_ram_per_sta->tx_pwr_offset1_en;
		pwr_ofst_ano = dm_ram_per_sta->tx_pwr_offset1;

		reg_0x1e84 |= (pwr_ofst_en << 15) + ((pwr_ofst & 0x7f) << 8) +
			      (pwr_ofst_ano_en << 23) +
			      ((pwr_ofst_ano & 0x7f) << 16);
	} else {
		dm_ram_per_sta->tx_pwr_offset1_en = pwr_ofst_en;
		dm_ram_per_sta->tx_pwr_offset1 = pwr_ofst;

		pwr_ofst_ano_en = dm_ram_per_sta->tx_pwr_offset0_en;
		pwr_ofst_ano = dm_ram_per_sta->tx_pwr_offset0;

		reg_0x1e84 |= (pwr_ofst_ano_en << 15) +
			      ((pwr_ofst_ano & 0x7f) << 8) +
			      (pwr_ofst_en << 23) +  ((pwr_ofst & 0x7f) << 16);
	}
	reg_0x1e84 |= (macid & 0x3f) << 24;/* macid*/
	reg_0x1e84 |= BIT(30); /* write_en*/
	odm_set_bb_reg(dm, R_0x1e84, MASKDWORD, reg_0x1e84);
	odm_set_bb_reg(dm, R_0x1e84, MASKDWORD, 0x80000000); /* read_en*/
	odm_set_bb_reg(dm, R_0x1e84, MASKDWORD, 0x0); /* disable rd/wt*/
};

void phydm_rst_ram_pwr(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_bb_ram_per_sta *dm_ram_per_sta = NULL;
	u32 reg_0x1e84 = 0;
	u8 i = 0;

	for (i = 0; i < 64; i++) {
		dm_ram_per_sta = &dm->p_bb_ram_ctrl.pram_sta_ctrl[i];
		dm_ram_per_sta->tx_pwr_offset0_en = false;
		dm_ram_per_sta->tx_pwr_offset1_en = false;
		dm_ram_per_sta->tx_pwr_offset0 = 0x0;
		dm_ram_per_sta->tx_pwr_offset1 = 0x0;
		reg_0x1e84 = (dm_ram_per_sta->hw_igi_en << 7) +
			     dm_ram_per_sta->hw_igi;
		reg_0x1e84 |= (i & 0x3f) << 24;
		reg_0x1e84 |= BIT(30);
		odm_set_bb_reg(dm, R_0x1e84, MASKDWORD, reg_0x1e84);
	}

	odm_set_bb_reg(dm, R_0x1e84, MASKDWORD, 0x80000000);
	odm_set_bb_reg(dm, R_0x1e84, MASKDWORD, 0x0);
};

u8 phydm_pwr_lv_mapping_2nd(u8 tx_pwr_lv)
{
	if (tx_pwr_lv == tx_high_pwr_level_level3)
		return PHYDM_2ND_OFFSET_MINUS_11DB;
	else if (tx_pwr_lv == tx_high_pwr_level_level2)
		return PHYDM_2ND_OFFSET_MINUS_7DB;
	else if (tx_pwr_lv == tx_high_pwr_level_level1)
		return PHYDM_2ND_OFFSET_MINUS_3DB;
	else
		return PHYDM_2ND_OFFSET_ZERO;
}

void phydm_pwr_lv_ctrl(void *dm_void, u8 macid, u8 tx_pwr_lv)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	s8 pwr_offset = 0;

	if (tx_pwr_lv == tx_high_pwr_level_level3)
		pwr_offset = PHYDM_BBRAM_OFFSET_MINUS_11DB;
	else if (tx_pwr_lv == tx_high_pwr_level_level2)
		pwr_offset = PHYDM_BBRAM_OFFSET_MINUS_7DB;
	else if (tx_pwr_lv == tx_high_pwr_level_level1)
		pwr_offset = PHYDM_BBRAM_OFFSET_MINUS_3DB;
	else
		pwr_offset = PHYDM_BBRAM_OFFSET_ZERO;

	phydm_wt_ram_pwr(dm, macid, RAM_PWR_OFST0, true, pwr_offset);
}

void phydm_dtp_fill_cmninfo_2nd(void *dm_void, u8 sta_id, u8 dtp_lvl)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct cmn_sta_info *sta = dm->phydm_sta_info[sta_id];
	struct dtp_info *dtp = NULL;

	if (!is_sta_active(sta))
		return;

	dtp = &dm->phydm_sta_info[sta_id]->dtp_stat;
	dtp->dyn_tx_power = phydm_pwr_lv_mapping_2nd(dtp_lvl);
	phydm_pwr_lv_ctrl(dm, sta->mac_id, dtp_lvl);

	PHYDM_DBG(dm, DBG_DYN_TXPWR,
		  "Fill cmninfo TxPwr: sta_id=(%d), macid=(%d), PwrLv (%d)\n",
		  sta_id, sta->mac_id, dtp->dyn_tx_power);
}

void phydm_dtp_init_2nd(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (!(dm->support_ability & ODM_BB_DYNAMIC_TXPWR))
		return;

	#if (RTL8822C_SUPPORT || RTL8812F_SUPPORT)
	if (dm->support_ic_type & (ODM_RTL8822C | ODM_RTL8812F)) {
		phydm_rst_ram_pwr(dm);
		/* rsp tx use type 0*/
		odm_set_mac_reg(dm, R_0x6d8, BIT(19) | BIT(18), RAM_PWR_OFST0);
	}
	#endif
};
#endif

boolean
phydm_check_rates(void *dm_void, u8 rate_idx)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 check_rate_bitmap0 = 0x08080808; /* @check CCK11M, OFDM54M, MCS7, MCS15*/
	u32 check_rate_bitmap1 = 0x80200808; /* @check MCS23, MCS31, VHT1SS M9, VHT2SS M9*/
	u32 check_rate_bitmap2 = 0x00080200; /* @check VHT3SS M9, VHT4SS M9*/
	u32 bitmap_result;

#if (RTL8822B_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8822B) {
		check_rate_bitmap2 &= 0;
		check_rate_bitmap1 &= 0xfffff000;
		check_rate_bitmap0 &= 0x0fffffff;
	}
#endif
#if (RTL8197F_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8197F) {
		check_rate_bitmap2 &= 0;
		check_rate_bitmap1 &= 0;
		check_rate_bitmap0 &= 0x0fffffff;
	}
#endif
#if (RTL8192E_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8192E) {
		check_rate_bitmap2 &= 0;
		check_rate_bitmap1 &= 0;
		check_rate_bitmap0 &= 0x0fffffff;
	}
#endif
#if (RTL8192F_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8192F) {
		check_rate_bitmap2 &= 0;
		check_rate_bitmap1 &= 0;
		check_rate_bitmap0 &= 0x0fffffff;
	}
#endif
#if (RTL8721D_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8721D) {
		check_rate_bitmap2 &= 0;
		check_rate_bitmap1 &= 0;
		check_rate_bitmap0 &= 0x000fffff;
	}
#endif
#if (RTL8821C_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8821C) {
		check_rate_bitmap2 &= 0;
		check_rate_bitmap1 &= 0x003ff000;
		check_rate_bitmap0 &= 0x000fffff;
	}
#endif
	if (rate_idx >= 64)
		bitmap_result = BIT(rate_idx - 64) & check_rate_bitmap2;
	else if (rate_idx >= 32)
		bitmap_result = BIT(rate_idx - 32) & check_rate_bitmap1;
	else if (rate_idx <= 31)
		bitmap_result = BIT(rate_idx) & check_rate_bitmap0;

	if (bitmap_result != 0)
		return true;
	else
		return false;
}

enum rf_path
phydm_check_paths(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	enum rf_path max_path = RF_PATH_A;

	if (dm->num_rf_path == 1)
		max_path = RF_PATH_A;
	if (dm->num_rf_path == 2)
		max_path = RF_PATH_B;
	if (dm->num_rf_path == 3)
		max_path = RF_PATH_C;
	if (dm->num_rf_path == 4)
		max_path = RF_PATH_D;

	return max_path;
}

#ifndef PHYDM_COMMON_API_SUPPORT
u8 phydm_dtp_get_txagc(void *dm_void, enum rf_path path, u8 hw_rate)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 ret = 0xff;

#if (RTL8192E_SUPPORT)
	ret = config_phydm_read_txagc_n(dm, path, hw_rate);
#endif
	return ret;
}
#endif

u8 phydm_search_min_power_index(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	enum rf_path path;
	enum rf_path max_path;
	u8 min_gain_index = 0x3f;
	u8 gain_index;
	u8 rate_idx;

	PHYDM_DBG(dm, DBG_DYN_TXPWR, "%s\n", __func__);
	max_path = phydm_check_paths(dm);
	for (path = 0; path <= max_path; path++)
		for (rate_idx = 0; rate_idx < 84; rate_idx++)
			if (phydm_check_rates(dm, rate_idx)) {
#ifdef PHYDM_COMMON_API_SUPPORT
				/*This is for API support IC : 97F,8822B,92F,8821C*/
				gain_index = phydm_api_get_txagc(dm, path, rate_idx);
#else
				/*This is for API non-support IC : 92E */
				gain_index = phydm_dtp_get_txagc(dm, path, rate_idx);
#endif
				if (gain_index == 0xff) {
					min_gain_index = 0x20;
					PHYDM_DBG(dm, DBG_DYN_TXPWR, 
						  "Error Gain idx!! Rewite to: ((%d))\n",
						  min_gain_index);
					break;
				}
				PHYDM_DBG(dm, DBG_DYN_TXPWR,
					  "Support Rate: ((%d)) -> Gain idx: ((%d))\n",
					  rate_idx, gain_index);
				if (gain_index < min_gain_index)
					min_gain_index = gain_index;
			}
	return min_gain_index;
}

void phydm_dynamic_tx_power_init(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 i = 0;

	dm->last_dtp_lvl = tx_high_pwr_level_normal;
	dm->dynamic_tx_high_power_lvl = tx_high_pwr_level_normal;

	switch (dm->ic_ip_series) {
	#ifdef BB_RAM_SUPPORT
	case PHYDM_IC_JGR3:
		dm->set_pwr_th[0] = TX_PWR_NEAR_FIELD_TH_JGR3_LVL1;
		dm->set_pwr_th[1] = TX_PWR_NEAR_FIELD_TH_JGR3_LVL2;
		dm->set_pwr_th[2] = TX_PWR_NEAR_FIELD_TH_JGR3_LVL3;
		phydm_dtp_init_2nd(dm);
		break;
	#endif
	default:
		for (i = 0; i < 3; i++)
			dm->enhance_pwr_th[i] = 0xff;

		dm->set_pwr_th[0] = TX_POWER_NEAR_FIELD_THRESH_LVL1;
		dm->set_pwr_th[1] = TX_POWER_NEAR_FIELD_THRESH_LVL2;
		dm->set_pwr_th[2] = 0xff;
		dm->min_power_index = phydm_search_min_power_index(dm);
		PHYDM_DBG(dm, DBG_DYN_TXPWR, "DTP init: Min Gain idx: ((%d))\n",
			  dm->min_power_index);
		break;
	}
}

void phydm_noisy_enhance_hp_th(void *dm_void, u8 noisy_state)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (noisy_state == 0) {
		dm->enhance_pwr_th[0] = dm->set_pwr_th[0];
		dm->enhance_pwr_th[1] = dm->set_pwr_th[1];
		dm->enhance_pwr_th[2] = dm->set_pwr_th[2];
	} else {
		dm->enhance_pwr_th[0] = dm->set_pwr_th[0] + 8;
		dm->enhance_pwr_th[1] = dm->set_pwr_th[1] + 5;
		dm->enhance_pwr_th[2] = dm->set_pwr_th[2];
	}
	PHYDM_DBG(dm, DBG_DYN_TXPWR,
		  "DTP hp_enhance_th: Lv1_th =%d ,Lv2_th = %d ,Lv3_th = %d\n",
		  dm->enhance_pwr_th[0], dm->enhance_pwr_th[1],
		  dm->enhance_pwr_th[2]);
}

u8 phydm_pwr_lvl_check(void *dm_void, u8 input_rssi)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 th0, th1, th2;

	if (dm->support_ic_type & ODM_IC_JGR3_SERIES) {
		th2 = dm->set_pwr_th[2];
		th1 = dm->set_pwr_th[1];
		th0 = dm->set_pwr_th[0];
		PHYDM_DBG(dm, DBG_DYN_TXPWR,
			  "DTP th: Lv1_th = %d, Lv2_th = %d, Lv3_th = %d\n",
			  th0, th1, th2);
	} else {
		th2 = dm->enhance_pwr_th[2];
		th1 = dm->enhance_pwr_th[1];
		th0 = dm->enhance_pwr_th[0];
	}

	if (input_rssi >= th2)
		return tx_high_pwr_level_level3;
	else if (input_rssi < (th2 - 3) && input_rssi >= th1)
		return tx_high_pwr_level_level2;
	else if (input_rssi < (th1 - 3) && input_rssi >= th0)
		return tx_high_pwr_level_level1;
	else if (input_rssi < (th0 - 3))
		return tx_high_pwr_level_normal;
	else
		return tx_high_pwr_level_unchange;
}

u8 phydm_pwr_lv_mapping(u8 tx_pwr_lv)
{
	if (tx_pwr_lv == tx_high_pwr_level_level3)
		return PHYDM_OFFSET_MINUS_11DB;
	else if (tx_pwr_lv == tx_high_pwr_level_level2)
		return PHYDM_OFFSET_MINUS_7DB;
	else if (tx_pwr_lv == tx_high_pwr_level_level1)
		return PHYDM_OFFSET_MINUS_3DB;
	else
		return PHYDM_OFFSET_ZERO;
}

void phydm_dynamic_response_power(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 rpwr = 0;

	if (!(dm->support_ability & ODM_BB_DYNAMIC_TXPWR))
		return;

	if (dm->dynamic_tx_high_power_lvl == tx_high_pwr_level_unchange) {
		dm->dynamic_tx_high_power_lvl = dm->last_dtp_lvl;
		PHYDM_DBG(dm, DBG_DYN_TXPWR, "RespPwr not change\n");
		return;
	}
	PHYDM_DBG(dm, DBG_DYN_TXPWR,
		  "RespPwr update_DTP_lv: ((%d)) -> ((%d))\n", dm->last_dtp_lvl,
		  dm->dynamic_tx_high_power_lvl);
	dm->last_dtp_lvl = dm->dynamic_tx_high_power_lvl;
	rpwr = phydm_pwr_lv_mapping(dm->dynamic_tx_high_power_lvl);
	odm_set_mac_reg(dm, ODM_REG_RESP_TX_11AC, BIT(20) | BIT(19) | BIT(18),
			rpwr);
	PHYDM_DBG(dm, DBG_DYN_TXPWR, "RespPwr Set TxPwr: Lv (%d)\n",
		  dm->dynamic_tx_high_power_lvl);
}

void phydm_dtp_fill_cmninfo(void *dm_void, u8 sta_id, u8 dtp_lvl)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct cmn_sta_info *sta = dm->phydm_sta_info[sta_id];
	struct dtp_info *dtp = NULL;

	if (!is_sta_active(sta))
		return;

	dtp = &sta->dtp_stat;
	dtp->dyn_tx_power = phydm_pwr_lv_mapping(dtp_lvl);
	PHYDM_DBG(dm, DBG_DYN_TXPWR,
		  "Fill cmninfo TxPwr: sta_id=(%d), macid=(%d), PwrLv (%d)\n",
		  sta_id, sta->mac_id, dtp->dyn_tx_power);
}

void phydm_dtp_per_sta(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct cmn_sta_info *sta = NULL;
	struct dtp_info *dtp = NULL;
	struct rssi_info *rssi = NULL;
	u8 sta_cnt = 0;
	u8 i = 0;
	u8 curr_pwr_lv = 0;
	u8 last_pwr_lv = 0;

	for (i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++) {
		sta = dm->phydm_sta_info[i];
		if (is_sta_active(sta)) {
			sta_cnt++;

			dtp = &sta->dtp_stat;
			rssi = &sta->rssi_stat;
			last_pwr_lv = dtp->sta_last_dtp_lvl;
			curr_pwr_lv = phydm_pwr_lvl_check(dm, rssi->rssi);
			dtp->sta_tx_high_power_lvl = curr_pwr_lv;
			PHYDM_DBG(dm, DBG_DYN_TXPWR,
				  "STA_id=%d, MACID=%d , RSSI: %d , GetPwrLv: %d\n",
				  i, sta->mac_id, rssi->rssi, curr_pwr_lv);

			if (curr_pwr_lv == tx_high_pwr_level_unchange ||
			    curr_pwr_lv == last_pwr_lv) {
				dtp->sta_tx_high_power_lvl = last_pwr_lv;
				PHYDM_DBG(dm, DBG_DYN_TXPWR,
					  "DTP_lv not change: ((%d))\n",
					  curr_pwr_lv);
				return;
			}

			PHYDM_DBG(dm, DBG_DYN_TXPWR,
				  "DTP_lv update: ((%d)) -> ((%d))\n",
				  last_pwr_lv, curr_pwr_lv);

			dtp->sta_last_dtp_lvl = curr_pwr_lv;

			switch (dm->ic_ip_series) {
			#ifdef BB_RAM_SUPPORT
			case PHYDM_IC_JGR3:
				phydm_dtp_fill_cmninfo_2nd(dm, i, curr_pwr_lv);
				break;
			#endif
			default:
				phydm_dtp_fill_cmninfo(dm, i, curr_pwr_lv);
				break;
			}

			if (sta_cnt == dm->number_linked_client)
				break;
		}
	}
}

void odm_set_dyntxpwr(void *dm_void, u8 *desc, u8 sta_id)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct cmn_sta_info *sta = dm->phydm_sta_info[sta_id];
	struct dtp_info *dtp = NULL;

	if (!is_sta_active(sta))
		return;
	dtp = &sta->dtp_stat;

	if (!(dm->support_ability & ODM_BB_DYNAMIC_TXPWR))
		return;

	if (dm->fill_desc_dyntxpwr)
		dm->fill_desc_dyntxpwr(dm, desc, dtp->dyn_tx_power);
	else
		PHYDM_DBG(dm, DBG_DYN_TXPWR,
			  "%s: fill_desc_dyntxpwr is null!\n", __func__);

	if (dtp->last_tx_power != dtp->dyn_tx_power) {
		PHYDM_DBG(dm, DBG_DYN_TXPWR,
			  "%s: last_offset=%d, txpwr_offset=%d\n", __func__,
			  dtp->last_tx_power, dtp->dyn_tx_power);
		dtp->last_tx_power = dtp->dyn_tx_power;
	}
}

void phydm_dtp_debug(void *dm_void, char input[][16], u32 *_used, char *output,
			     u32 *_out_len)
{
	u32 used = *_used;
	u32 out_len = *_out_len;

	struct dm_struct *dm = (struct dm_struct *)dm_void;
	char help[] = "-h";
	u32 var1[7] = {0};
	u8 set_pwr_th1, set_pwr_th2, set_pwr_th3;
	u8 i = 0;
	#ifdef BB_RAM_SUPPORT
	s8 pwr_ofst_tmp = 0x0;
	#endif

	if ((strcmp(input[1], help) == 0)) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "Set DTP threhosld: {1} {Lv1_th} {Lv2_th} {Lv3_th}\n");
		#ifdef BB_RAM_SUPPORT
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "Set pwr_tx_offset: {2} {0:reg 1:macid} {en} {offset 0/1} {0:-, 1:+} {Pwr Offset} {macid}\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "Read pwr_tx_offset : {3} {0:reg 1:macid} {macid(0~63), 255:all}\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "Reset all ram pwr_tx_offset : {4}\n");
		#endif
	} else {
		for (i = 0; i < 7; i++) {
			if (input[i + 1])
				PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL,
					     &var1[i]);
		}
		switch (var1[0]) {
		case 1:
			for (i = 0; i < 3; i++) {
				if (var1[i] == 0 || var1[i] > 100)
					dm->set_pwr_th[i] = 0xff;
				else
					dm->set_pwr_th[i] = (u8)var1[1 + i];
			}

			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "DTP_TH[0:2] = {%d, %d, %d}\n",
				 dm->set_pwr_th[0], dm->set_pwr_th[1],
				 dm->set_pwr_th[2]);
			break;
		#ifdef BB_RAM_SUPPORT
		case 2:
			if ((boolean)var1[4])
				pwr_ofst_tmp = (s8)var1[5];
			else
				pwr_ofst_tmp = 0x0 - (s8)var1[5];

			if ((boolean)var1[1])
				phydm_wt_ram_pwr(dm, (u8)var1[6],
						 (boolean)var1[3],
						 (boolean)var1[2],
						 pwr_ofst_tmp);
			else
				phydm_wt_reg_pwr(dm, (boolean)var1[3],
						 (boolean)var1[2],
						 pwr_ofst_tmp);
			break;
		case 3:
			if ((boolean)var1[1]) {
				if ((u8)var1[2] == 0xff)
					for (i = 0; i < 64; i++)
						phydm_rd_ram_pwr(dm, i, &used,
								 output,
								 &out_len);
				else
					phydm_rd_ram_pwr(dm, (u8)var1[2], &used,
							 output, &out_len);
			} else {
				phydm_rd_reg_pwr(dm, &used, output, &out_len);
			}
			break;
		case 4:
			phydm_rst_ram_pwr(dm);
			break;
		#endif
		}
	}
	*_used = used;
	*_out_len = out_len;
}

void phydm_dynamic_tx_power(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct cmn_sta_info *sta = NULL;
	u8 i = 0;

	u8 rssi_min = dm->rssi_min;
	u8 rssi_tmp = 0;

	if (!(dm->support_ability & ODM_BB_DYNAMIC_TXPWR))
		return;

	if (!(dm->support_ic_type & ODM_IC_JGR3_SERIES)) {
		PHYDM_DBG(dm, DBG_DYN_TXPWR,
			  "[%s] RSSI_min = %d, Noisy_dec = %d\n", __func__,
			  rssi_min, dm->noisy_decision);
		phydm_noisy_enhance_hp_th(dm, dm->noisy_decision);
		/* Response Power */
		dm->dynamic_tx_high_power_lvl = phydm_pwr_lvl_check(dm,
								    rssi_min);
		phydm_dynamic_response_power(dm);
	}
	/* Per STA Tx power */
	phydm_dtp_per_sta(dm);
}
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

void phydm_dynamic_tx_power_init_win(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	void *adapter = dm->adapter;
	PMGNT_INFO mgnt_info = &((PADAPTER)adapter)->MgntInfo;
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA((PADAPTER)adapter);

	mgnt_info->bDynamicTxPowerEnable = false;
	#if DEV_BUS_TYPE == RT_USB_INTERFACE
	if (RT_GetInterfaceSelection((PADAPTER)adapter) ==
	    INTF_SEL1_USB_High_Power) {
		mgnt_info->bDynamicTxPowerEnable = true;
	}
	#endif

	hal_data->LastDTPLvl = tx_high_pwr_level_normal;
	hal_data->DynamicTxHighPowerLvl = tx_high_pwr_level_normal;

	PHYDM_DBG(dm, DBG_DYN_TXPWR, "[%s] DTP=%d\n", __func__,
		  mgnt_info->bDynamicTxPowerEnable);
}

void phydm_dynamic_tx_power_win(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (!(dm->support_ability & ODM_BB_DYNAMIC_TXPWR))
		return;

	#if (RTL8814A_SUPPORT)
	if (dm->support_ic_type == ODM_RTL8814A)
		odm_dynamic_tx_power_8814a(dm);
	#endif

	#if (RTL8821A_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8821) {
		void *adapter = dm->adapter;
		PMGNT_INFO mgnt_info = GetDefaultMgntInfo((PADAPTER)adapter);

		if (mgnt_info->RegRspPwr == 1) {
			if (dm->rssi_min > 60) {
				/*Resp TXAGC offset = -3dB*/
				odm_set_mac_reg(dm, R_0x6d8, 0x1C0000, 1);
			} else if (dm->rssi_min < 55) {
				/*Resp TXAGC offset = 0dB*/
				odm_set_mac_reg(dm, R_0x6d8, 0x1C0000, 0);
			}
		}
	}
	#endif
}
#endif /*@#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)*/
#endif /* @#ifdef CONFIG_DYNAMIC_TX_TWR */
