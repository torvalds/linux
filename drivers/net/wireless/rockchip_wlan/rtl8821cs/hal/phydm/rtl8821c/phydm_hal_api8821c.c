/******************************************************************************
 *
 * Copyright(c) 2016 - 2017 Realtek Corporation.
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
 *****************************************************************************/

#include "mp_precomp.h"
#include "../phydm_precomp.h"

#if (RTL8821C_SUPPORT == 1)
#if (PHYDM_FW_API_ENABLE_8821C == 1)
/* ======================================================================== */
/* These following functions can be used for PHY DM only*/

u32 rega24_8821c;
u32 rega28_8821c;
u32 regaac_8821c;

enum channel_width bw_8821c;
u8 central_ch_8821c;

__iram_odm_func__
void phydm_igi_toggle_8821c(struct dm_struct *dm)
{
	u32 igi = 0x20;

	igi = odm_get_bb_reg(dm, R_0xc50, 0x7f);
	odm_set_bb_reg(dm, R_0xc50, 0x7f, (igi - 2));
	odm_set_bb_reg(dm, R_0xc50, 0x7f, igi);
}

__iram_odm_func__
s8 phydm_cck_rssi_8821c(struct dm_struct *dm, u8 lna_idx, u8 vga_idx)
{
	s8 rx_pwr_all = 0;
	s8 lna_gain = 0;
	/*only use lna2/3/5/7*/
	s8 lna_gain_table_0[8] = {22, 8, -6, -22, -31, -40, -46, -52};
	/*only use lna4/8/C/F*/
	s8 lna_gain_table_1[16] = {10, 6, 2, -2, -6, -10, -14, -17,
				   -20, -24, -28, -31, -34, -37, -40, -44};

	if (dm->cck_agc_report_type == 0)
		lna_gain = lna_gain_table_0[lna_idx];
	else
		lna_gain = lna_gain_table_1[lna_idx];

	rx_pwr_all = lna_gain - (2 * vga_idx);

	return rx_pwr_all;
}

__iram_odm_func__
boolean
phydm_rfe_8821c(struct dm_struct *dm, u8 channel)
{
#if 0
	/* Efuse is not wrote now */
	/* Need to check RFE type finally */
	/*if (dm->rfe_type == 1) {*/
	if (channel <= 14) {
		/* signal source */
		odm_set_bb_reg(dm, R_0xcb0, (MASKBYTE2 | MASKLWORD), 0x704570);
		odm_set_bb_reg(dm, R_0xeb0, (MASKBYTE2 | MASKLWORD), 0x704570);
		odm_set_bb_reg(dm, R_0xcb4, MASKBYTE1, 0x45);
		odm_set_bb_reg(dm, R_0xeb4, MASKBYTE1, 0x45);
	} else if (channel > 35) {
		odm_set_bb_reg(dm, R_0xcb0, (MASKBYTE2 | MASKLWORD), 0x174517);
		odm_set_bb_reg(dm, R_0xeb0, (MASKBYTE2 | MASKLWORD), 0x174517);
		odm_set_bb_reg(dm, R_0xcb4, MASKBYTE1, 0x45);
		odm_set_bb_reg(dm, R_0xeb4, MASKBYTE1, 0x45);
	} else
		return false;

	/* chip top mux */
	odm_set_bb_reg(dm, R_0x64, BIT(29) | BIT(28), 0x3);
	odm_set_bb_reg(dm, R_0x4c, BIT(26) | BIT(25), 0x0);
	odm_set_bb_reg(dm, R_0x40, BIT(2), 0x1);

	/* from s0 or s1 */
	odm_set_bb_reg(dm, R_0x1990, (BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0)), 0x30);
	odm_set_bb_reg(dm, R_0x1990, (BIT(11) | BIT(10)), 0x3);

	/* input or output */
	odm_set_bb_reg(dm, R_0x974, (BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0)), 0x3f);
	odm_set_bb_reg(dm, R_0x974, (BIT(11) | BIT(10)), 0x3);

	/* delay 400ns for PAPE */
	odm_set_bb_reg(dm, R_0x810, MASKBYTE3 | BIT(20) | BIT(21) | BIT(22) | BIT(23), 0x211);

	/* antenna switch table */
	odm_set_bb_reg(dm, R_0xca0, MASKLWORD, 0xa555);
	odm_set_bb_reg(dm, R_0xea0, MASKLWORD, 0xa555);

	/* inverse or not */
	odm_set_bb_reg(dm, R_0xcbc, (BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0)), 0x0);
	odm_set_bb_reg(dm, R_0xcbc, (BIT(11) | BIT(10)), 0x0);
	odm_set_bb_reg(dm, R_0xebc, (BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0)), 0x0);
	odm_set_bb_reg(dm, R_0xebc, (BIT(11) | BIT(10)), 0x0);
	/*}*/
#endif
	return true;
}

__iram_odm_func__
void phydm_ccapar_8821c(struct dm_struct *dm)
{
#if 0
	u32	cca_ifem[9][4] = {
		/*20M*/
		{0x75D97010, 0x75D97010, 0x75D97010, 0x75D97010}, /*Reg82C*/
		{0x00000000, 0x00000000, 0x00000000, 0x00000000}, /*Reg830*/
		{0x00000000, 0x00000000, 0x00000000, 0x00000000}, /*Reg838*/
		/*40M*/
		{0x75D97010, 0x75D97010, 0x75D97010, 0x75D97010}, /*Reg82C*/
		{0x00000000, 0x79a0ea28, 0x00000000, 0x79a0ea28}, /*Reg830*/
		{0x87765541, 0x87766341, 0x87765541, 0x87766341}, /*Reg838*/
		/*80M*/
		{0x75D97010, 0x75D97010, 0x75D97010, 0x75D97010}, /*Reg82C*/
		{0x00000000, 0x00000000, 0x00000000, 0x00000000}, /*Reg830*/
		{0x00000000, 0x87746641, 0x00000000, 0x87746641}
	}; /*Reg838*/

	u32	cca_efem[9][4] = {
		/*20M*/
		{0x75A76010, 0x75A76010, 0x75A76010, 0x75A75010}, /*Reg82C*/
		{0x00000000, 0x00000000, 0x00000000, 0x00000000}, /*Reg830*/
		{0x87766651, 0x87766431, 0x87766451, 0x87766431}, /*Reg838*/
		/*40M*/
		{0x75A75010, 0x75A75010, 0x75A75010, 0x75A75010}, /*Reg82C*/
		{0x00000000, 0x00000000, 0x00000000, 0x00000000}, /*Reg830*/
		{0x87766431, 0x87766431, 0x87766431, 0x87766431}, /*Reg838*/
		/*80M*/
		{0x75BA7010, 0x75BA7010, 0x75BA7010, 0x75BA7010}, /*Reg82C*/
		{0x00000000, 0x00000000, 0x00000000, 0x00000000}, /*Reg830*/
		{0x87766431, 0x87766431, 0x87766431, 0x87766431}
	}; /*Reg838*/

	u8	row, col;
	u32	reg82c, reg830, reg838;

	if (dm->cut_version != ODM_CUT_B)
		return;

	if (bw_8821c == CHANNEL_WIDTH_20)
		row = 0;
	else if (bw_8821c == CHANNEL_WIDTH_40)
		row = 3;
	else
		row = 6;

	if (central_ch_8821c <= 14) {
		if (dm->rx_ant_status == BB_PATH_A || dm->rx_ant_status == BB_PATH_B)
			col = 0;
		else
			col = 1;
	} else {
		if (dm->rx_ant_status == BB_PATH_A || dm->rx_ant_status == BB_PATH_B)
			col = 2;
		else
			col = 3;
	}

	if (dm->rfe_type == 0) {/*iFEM*/
		reg82c = (cca_ifem[row][col] != 0) ? cca_ifem[row][col] : reg82c_8821c;
		reg830 = (cca_ifem[row + 1][col] != 0) ? cca_ifem[row + 1][col] : reg830_8821c;
		reg838 = (cca_ifem[row + 2][col] != 0) ? cca_ifem[row + 2][col] : reg838_8821c;
	} else {/*eFEM*/
		reg82c = (cca_efem[row][col] != 0) ? cca_efem[row][col] : reg82c_8821c;
		reg830 = (cca_efem[row + 1][col] != 0) ? cca_efem[row + 1][col] : reg830_8821c;
		reg838 = (cca_efem[row + 2][col] != 0) ? cca_efem[row + 2][col] : reg838_8821c;
	}

	odm_set_bb_reg(dm, R_0x82c, MASKDWORD, reg82c);
	odm_set_bb_reg(dm, R_0x830, MASKDWORD, reg830);
	odm_set_bb_reg(dm, R_0x838, MASKDWORD, reg838);

	PHYDM_DBG(dm, ODM_PHY_CONFIG,
		  "[%s]: Update CCA parameters for Bcut (Pkt%d, Intf%d, RFE%d), row = %d, col = %d\n",
		  __func__, dm->package_type, dm->support_interface,
		  dm->rfe_type, row, col);
#endif
}

__iram_odm_func__
void phydm_ccapar_by_bw_8821c(struct dm_struct *dm,
			      enum channel_width bandwidth)
{
#if 0
	u32		reg82c;


	if (dm->cut_version != ODM_CUT_A)
		return;

	/* A-cut */
	reg82c = odm_get_bb_reg(dm, R_0x82c, MASKDWORD);

	if (bandwidth == CHANNEL_WIDTH_20) {
		/* 82c[15:12] = 4 */
		/* 82c[27:24] = 6 */

		reg82c &= (~(0x0f00f000));
		reg82c |= ((0x4) << 12);
		reg82c |= ((0x6) << 24);
	} else if (bandwidth == CHANNEL_WIDTH_40) {
		/* 82c[19:16] = 9 */
		/* 82c[27:24] = 6 */

		reg82c &= (~(0x0f0f0000));
		reg82c |= ((0x9) << 16);
		reg82c |= ((0x6) << 24);
	} else if (bandwidth == CHANNEL_WIDTH_80) {
		/* 82c[15:12] 7 */
		/* 82c[19:16] b */
		/* 82c[23:20] d */
		/* 82c[27:24] 3 */

		reg82c &= (~(0x0ffff000));
		reg82c |= ((0xdb7) << 12);
		reg82c |= ((0x3) << 24);
	}

	odm_set_bb_reg(dm, R_0x82c, MASKDWORD, reg82c);
	PHYDM_DBG(dm, ODM_PHY_CONFIG, "[%s]: Update CCA parameters for Acut\n",
		  __func__);
#endif
}

__iram_odm_func__
void phydm_ccapar_by_rxpath_8821c(struct dm_struct *dm)
{
#if 0
	if (dm->cut_version != ODM_CUT_A)
		return;

	if (dm->rx_ant_status == BB_PATH_A || dm->rx_ant_status == BB_PATH_B) {
		/* 838[7:4] = 8 */
		/* 838[11:8] = 7 */
		/* 838[15:12] = 6 */
		/* 838[19:16] = 7 */
		/* 838[23:20] = 7 */
		/* 838[27:24] = 7 */
		odm_set_bb_reg(dm, R_0x838, 0x0ffffff0, 0x777678);
	} else {
		/* 838[7:4] = 3 */
		/* 838[11:8] = 3 */
		/* 838[15:12] = 6 */
		/* 838[19:16] = 6 */
		/* 838[23:20] = 7 */
		/* 838[27:24] = 7 */
		odm_set_bb_reg(dm, R_0x838, 0x0ffffff0, 0x776633);
	}
	PHYDM_DBG(dm, ODM_PHY_CONFIG, "[%s]: Update CCA parameters for Acut\n",
		  __func__);
#endif
}

__iram_odm_func__
void phydm_rxdfirpar_by_bw_8821c(struct dm_struct *dm,
				 enum channel_width bandwidth)
{
	if (bandwidth == CHANNEL_WIDTH_40) {
		/* RX DFIR for BW40 */
		odm_set_bb_reg(dm, R_0x948, BIT(29) | BIT(28), 0x2);
		odm_set_bb_reg(dm, R_0x94c, BIT(29) | BIT(28), 0x2);
		odm_set_bb_reg(dm, R_0xc20, BIT(31), 0x0);
		odm_set_bb_reg(dm, R_0x8f0, BIT(31), 0x0);
	} else if (bandwidth == CHANNEL_WIDTH_80) {
		/* RX DFIR for BW80 */
		odm_set_bb_reg(dm, R_0x948, BIT(29) | BIT(28), 0x2);
		odm_set_bb_reg(dm, R_0x94c, BIT(29) | BIT(28), 0x1);
		odm_set_bb_reg(dm, R_0xc20, BIT(31), 0x0);
		odm_set_bb_reg(dm, R_0x8f0, BIT(31), 0x1);
	} else {
		/* RX DFIR for BW20, BW10 and BW5*/
		odm_set_bb_reg(dm, R_0x948, BIT(29) | BIT(28), 0x2);
		odm_set_bb_reg(dm, R_0x94c, BIT(29) | BIT(28), 0x2);
		odm_set_bb_reg(dm, R_0xc20, BIT(31), 0x1);
		odm_set_bb_reg(dm, R_0x8f0, BIT(31), 0x0);
	}
	/* PHYDM_DBG(dm, ODM_PHY_CONFIG, "phydm_rxdfirpar_by_bw_8821c\n");*/
}

__iram_odm_func__
boolean
phydm_write_txagc_1byte_8821c(struct dm_struct *dm, u32 power_index,
			      enum rf_path path, u8 hw_rate)
{
#if (PHYDM_FW_API_FUNC_ENABLE_8821C == 1)
	u32 offset_txagc[2] = {0x1d00, 0x1d80};
	u8 rate_idx = (hw_rate & 0xfc), i;
	u8 rate_offset = (hw_rate & 0x3);
	u32 txagc_content = 0x0;

	/* For debug command only!!!! */

	/* Error handling */
	if (path > RF_PATH_A || hw_rate > 0x53) {
		PHYDM_DBG(dm, ODM_PHY_CONFIG, "[%s]: unsupported path (%d)\n",
			  __func__, path);
		return false;
	}

#if 1
	/* For HW limitation, We can't write TXAGC once a byte. */
	for (i = 0; i < 4; i++) {
		if (i != rate_offset)
			txagc_content = txagc_content | (config_phydm_read_txagc_8821c(dm, path, rate_idx + i) << (i << 3));
		else
			txagc_content = txagc_content | ((power_index & 0x3f) << (i << 3));
	}
	odm_set_bb_reg(dm, (offset_txagc[path] + rate_idx), MASKDWORD, txagc_content);
#else
	odm_write_1byte(dm, (offset_txagc[path] + hw_rate), (power_index & 0x3f));
#endif

	PHYDM_DBG(dm, ODM_PHY_CONFIG,
		  "[%s]: path-%d rate index 0x%x (0x%x) = 0x%x\n", __func__,
		  path, hw_rate, (offset_txagc[path] + hw_rate), power_index);
	return true;
#else
	return false;
#endif
}

__iram_odm_func__
void phydm_init_hw_info_by_rfe_type_8821c(struct dm_struct *dm)
{
#if (PHYDM_FW_API_FUNC_ENABLE_8821C == 1)
	dm->is_init_hw_info_by_rfe = false;
	/*
	 * Let original variable rfe_type to be rfe_type_8821c.
	 * Varible rfe_type as symbol is used to identify PHY parameter.
	 */
	dm->rfe_type = dm->rfe_type_expand >> 3;

	/*2.4G default rf set with wlg or btg*/
	if (dm->rfe_type_expand == 2 || dm->rfe_type_expand == 4 || dm->rfe_type_expand == 7) {
		dm->default_rf_set_8821c = SWITCH_TO_BTG;
	} else if (dm->rfe_type_expand == 0 || dm->rfe_type_expand == 1 ||
		 dm->rfe_type_expand == 3 || dm->rfe_type_expand == 5 ||
		 dm->rfe_type_expand == 6) {
		dm->default_rf_set_8821c = SWITCH_TO_WLG;
	} else if (dm->rfe_type_expand == 0x22 || dm->rfe_type_expand == 0x24 ||
		 dm->rfe_type_expand == 0x27 || dm->rfe_type_expand == 0x2a ||
		 dm->rfe_type_expand == 0x2c || dm->rfe_type_expand == 0x2f) {
		dm->default_rf_set_8821c = SWITCH_TO_BTG;
		odm_cmn_info_init(dm, ODM_CMNINFO_PACKAGE_TYPE, 1);
	} else if (dm->rfe_type_expand == 0x20 || dm->rfe_type_expand == 0x21 ||
		   dm->rfe_type_expand == 0x23 || dm->rfe_type_expand == 0x25 ||
		   dm->rfe_type_expand == 0x26 || dm->rfe_type_expand == 0x28 ||
		   dm->rfe_type_expand == 0x29 || dm->rfe_type_expand == 0x2b ||
		   dm->rfe_type_expand == 0x2d || dm->rfe_type_expand == 0x2e) {
		dm->default_rf_set_8821c = SWITCH_TO_WLG;
		odm_cmn_info_init(dm, ODM_CMNINFO_PACKAGE_TYPE, 1);
	}

	if (dm->rfe_type_expand == 3 || dm->rfe_type_expand == 4 ||
	    dm->rfe_type_expand == 0x23 || dm->rfe_type_expand == 0x24 ||
	    dm->rfe_type_expand == 0x2b || dm->rfe_type_expand == 0x2c)
		dm->default_ant_num_8821c = SWITCH_TO_ANT2;
	else
		dm->default_ant_num_8821c = SWITCH_TO_ANT1;

	dm->is_init_hw_info_by_rfe = true;
	PHYDM_DBG(dm, ODM_PHY_CONFIG, "%s: RFE type (%d), rf set (%s)\n",
		  __FUNCTION__, dm->rfe_type_expand,
		  dm->default_rf_set_8821c == 0 ? "BTG" : "WLG");
#endif
}

__iram_odm_func__
void phydm_set_gnt_state_8821c(struct dm_struct *dm, boolean gnt_wl_state,
			       boolean gnt_bt_state)
{
#if (PHYDM_FW_API_FUNC_ENABLE_8821C == 1)
	u32 gnt_val = 0;

	odm_set_bb_reg(dm, R_0x70, BIT(26), 0x1);

	if (gnt_wl_state)
		gnt_val = 0x3300;
	else
		gnt_val = 0x1100;

	if (gnt_bt_state)
		gnt_val = gnt_val | 0xcc00;
	else
		gnt_val = gnt_val | 0x4400;

	odm_set_bb_reg(dm, R_0x1704, MASKLWORD, gnt_val);
	ODM_delay_us(50); /*waiting before access 0x1700 */
	odm_set_bb_reg(dm, R_0x1700, MASKDWORD, 0xc00f0038);
#endif
}

__iram_odm_func__
void
phydm_dynamic_spur_det_eliminate_8821c(struct dm_struct *dm)
{
#if (PHYDM_FW_API_FUNC_ENABLE_8821C == 1)

	u32 freq_5g[FREQ_PT_5G_NUM_8821C] = {0x3e0, 0x20, 0x3a0};
	u32 freq_5g_n1[FREQ_PT_5G_NUM_8821C] = {0};
	u32 freq_5g_p1[FREQ_PT_5G_NUM_8821C] = {0};
	u32 freq_pt_5g_final = 0;
	u32 max_ret_psd_final = 0;
	u32 max_ret_psd_2nd[PSD_SMP_NUM_8821C] = {0};
	u32 psd_set[PSD_VAL_NUM_8821C] = {0};
	u32 rank_psd_index_in[PSD_VAL_NUM_8821C] = {0};
	u32 rank_psd_index_out[PSD_VAL_NUM_8821C] = {0};
	u32 rank_sample_index_in[PSD_SMP_NUM_8821C] = {0};
	u32 rank_sample_index_out[PSD_SMP_NUM_8821C] = {0};
	u16 threshold_nbi = 0x11a;
	u8 j = 0, k = 0;
	u8 idx = 0;
	boolean	s_donbi_a = false;

	/*PSD parameters init*/
	odm_set_bb_reg(dm, R_0x910, 0xfffc00, 0x3f);

	/* Reset NBI everytime after changing channel/BW/band  */
	phydm_nbi_enable(dm, FUNC_DISABLE);

	/* 5G Channel Setting > 20M: 153; 40M: 151; 80M: 155 */
	switch (*dm->channel) {
	case 153:
		idx = 0;
		break;
	case 151:
		idx = 1;
		break;
	case 155:
		idx = 2;
		break;
	default:
		idx = 16;
		break;
	}

	if (idx > 13) {
		PHYDM_DBG(dm, ODM_PHY_CONFIG, "Not support dym spur det\n");
		return;
	}

	PHYDM_DBG(dm, ODM_PHY_CONFIG, "[%s] idx = %d, BW = %d, Channel = %d\n",
		  __func__, idx, *dm->band_width, *dm->channel);

	for (k = 0; k < FREQ_PT_5G_NUM_8821C; k++) {
		freq_5g_n1[k] = freq_5g[k] - 1;
		freq_5g_p1[k] = freq_5g[k] + 1;
	}

	for (k = 0; k < PSD_SMP_NUM_8821C; k++) {
		if (k == 0)
			freq_pt_5g_final = freq_5g_n1[idx];
		else if (k == 1)
			freq_pt_5g_final = freq_5g[idx];
		else if (k == 2)
			freq_pt_5g_final = freq_5g_p1[idx];

		odm_set_bb_reg(dm, R_0x910, 0x3ff, freq_pt_5g_final);

		for (j = 0; j < PSD_VAL_NUM_8821C; j++) {
			/*stop TRX*/
			if (phydm_stop_ic_trx(dm, PHYDM_SET) == PHYDM_SET_FAIL)
				return;

			ODM_delay_us(10);

			/*Stop 3-wires*/
			phydm_stop_3_wire(dm, PHYDM_SET);

			/* Start PSD */
			odm_set_bb_reg(dm, R_0x910, BIT(22), 0x1);

			ODM_delay_us(500);

			psd_set[j] = odm_get_bb_reg(dm, R_0xf44, 0xffffff);
			psd_set[j] = psd_set[j] >> 5;

			/* turn off PSD */
			odm_set_bb_reg(dm, R_0x910, BIT(22), 0x0);

			/*Start 3-wires*/
			phydm_stop_3_wire(dm, PHYDM_REVERT);

			ODM_delay_us(10);

			phydm_stop_ic_trx(dm, PHYDM_REVERT);

			phydm_igi_toggle_8821c(dm);
		}

		phydm_seq_sorting(dm, psd_set, rank_psd_index_in,
				  rank_psd_index_out, PSD_VAL_NUM_8821C);
		max_ret_psd_2nd[k] = psd_set[0];
	}

	phydm_seq_sorting(dm, max_ret_psd_2nd, rank_sample_index_in,
			  rank_sample_index_out, PSD_SMP_NUM_8821C);
	max_ret_psd_final = max_ret_psd_2nd[0];

	if (max_ret_psd_final >= threshold_nbi)
		s_donbi_a = true;
	else
		s_donbi_a = false;

	PHYDM_DBG(dm, ODM_PHY_CONFIG, "[%s] max_ret_psd_final = %d\n",
		  __func__, max_ret_psd_final);

	if (!s_donbi_a)
		return;

	if (*dm->band_width == CHANNEL_WIDTH_20 && *dm->channel == 153)
		phydm_nbi_setting(dm, FUNC_ENABLE, 153, 20, 5760,
				  PHYDM_DONT_CARE);
	else if (*dm->band_width == CHANNEL_WIDTH_40 && *dm->channel == 151)
		phydm_nbi_setting(dm, FUNC_ENABLE, 151, 40, 5760,
				  PHYDM_DONT_CARE);
	else if (*dm->band_width == CHANNEL_WIDTH_80 && *dm->channel == 155)
		phydm_nbi_setting(dm, FUNC_ENABLE, 155, 80, 5760,
				  PHYDM_DONT_CARE);

#endif	/*PHYDM_SPUR_CANCELL_ENABLE_8821C == 1*/
}
/* ======================================================================== */

/* ======================================================================== */
/* These following functions can be used by driver*/

__iram_odm_func__
u32 config_phydm_read_rf_reg_8821c(struct dm_struct *dm, enum rf_path path,
				   u32 reg_addr, u32 bit_mask)
{
	u32 readback_value, direct_addr;
	u32 offset_read_rf[2] = {0x2800, 0x2c00};

	/* Error handling.*/
	if (path > RF_PATH_A) {
		PHYDM_DBG(dm, ODM_PHY_CONFIG, "[%s]: unsupported path (%d)\n",
			  __func__, path);
		return INVALID_RF_DATA;
	}

	/* Calculate offset */
	reg_addr &= 0xff;
	direct_addr = offset_read_rf[path] + (reg_addr << 2);

	/* RF register only has 20bits */
	bit_mask &= RFREGOFFSETMASK;

	/* Read RF register directly */
	readback_value = odm_get_bb_reg(dm, direct_addr, bit_mask);
	PHYDM_DBG(dm, ODM_PHY_CONFIG,
		  "[%s]: RF-%d 0x%x = 0x%x, bit mask = 0x%x\n", __func__, path,
		  reg_addr, readback_value, bit_mask);
	return readback_value;
}

__iram_odm_func__
boolean
config_phydm_write_rf_reg_8821c(struct dm_struct *dm, enum rf_path path,
				u32 reg_addr, u32 bit_mask, u32 data)
{
	u32 data_and_addr = 0, data_original = 0;
	u32 offset_write_rf[2] = {0xc90, 0xe90};
	u8 bit_shift;

	/* Error handling.*/
	if (path > RF_PATH_A) {
		PHYDM_DBG(dm, ODM_PHY_CONFIG, "[%s]: unsupported path (%d)\n",
			  __func__, path);
		return false;
	}

	/* Read RF register content first */
	reg_addr &= 0xff;
	bit_mask = bit_mask & RFREGOFFSETMASK;

	if (bit_mask != RFREGOFFSETMASK) {
		data_original = config_phydm_read_rf_reg_8821c(dm, path, reg_addr, RFREGOFFSETMASK);

		/* Error handling. RF is disabled */
		if (config_phydm_read_rf_check_8821c(data_original) == false) {
			PHYDM_DBG(dm, ODM_PHY_CONFIG,
				  "[%s]: Write fail, RF is disable\n",
				  __func__);
			return false;
		}

		/* check bit mask */
		if (bit_mask != 0xfffff) {
			for (bit_shift = 0; bit_shift <= 19; bit_shift++) {
				if (((bit_mask >> bit_shift) & 0x1) == 1)
					break;
			}
			data = ((data_original) & (~bit_mask)) | (data << bit_shift);
		}
	}

	/* Put write addr in [27:20]  and write data in [19:00] */
	data_and_addr = ((reg_addr << 20) | (data & 0x000fffff)) & 0x0fffffff;

	/* Write operation */
	odm_set_bb_reg(dm, offset_write_rf[path], MASKDWORD, data_and_addr);
	PHYDM_DBG(dm, ODM_PHY_CONFIG,
		  "[%s]: RF-%d 0x%x = 0x%x (original: 0x%x), bit mask = 0x%x\n",
		  __func__, path, reg_addr, data, data_original, bit_mask);

#if (defined(CONFIG_RUN_IN_DRV))
	if (dm->support_interface == ODM_ITRF_PCIE)
		ODM_delay_us(13);
#elif (defined(CONFIG_RUN_IN_FW))
	ODM_delay_us(13);
#endif

	return true;
}

__iram_odm_func__
boolean
config_phydm_write_txagc_8821c(struct dm_struct *dm, u32 power_index,
			       enum rf_path path, u8 hw_rate)
{
#if (PHYDM_FW_API_FUNC_ENABLE_8821C == 1)
	u32 offset_txagc[2] = {0x1d00, 0x1d80};
	u8 rate_idx = (hw_rate & 0xfc);

	/* Input need to be HW rate index, not driver rate index!!!! */

	if (dm->is_disable_phy_api) {
		PHYDM_DBG(dm, ODM_PHY_CONFIG,
			  "[%s]: disable PHY API for debug!!\n", __func__);
		return true;
	}

	/* Error handling */
	if (path > RF_PATH_A || hw_rate > 0x53) {
		PHYDM_DBG(dm, ODM_PHY_CONFIG, "[%s]: unsupported path (%d)\n",
			  __func__, path);
		return false;
	}

	/* driver need to construct a 4-byte power index */
	odm_set_bb_reg(dm, (offset_txagc[path] + rate_idx), MASKDWORD, power_index);

	PHYDM_DBG(dm, ODM_PHY_CONFIG,
		  "[%s]: path-%d rate index 0x%x (0x%x) = 0x%x\n", __func__,
		  path, hw_rate, (offset_txagc[path] + hw_rate), power_index);
	return true;
#else
	return false;
#endif
}

__iram_odm_func__
u8 config_phydm_read_txagc_8821c(struct dm_struct *dm, enum rf_path path,
				 u8 hw_rate)
{
#if (PHYDM_FW_API_FUNC_ENABLE_8821C == 1)
	u8 read_back_data;

	/* Input need to be HW rate index, not driver rate index!!!! */

	/* Error handling */
	if (path > RF_PATH_A || hw_rate > 0x53) {
		PHYDM_DBG(dm, ODM_PHY_CONFIG, "[%s]: unsupported path (%d)\n",
			  __func__, path);
		return INVALID_TXAGC_DATA;
	}

	/* Disable TX AGC report */
	odm_set_bb_reg(dm, R_0x1998, BIT(16), 0x0); /* need to check */

	/* Set data rate index (bit0~6) and path index (bit7) */
	odm_set_bb_reg(dm, R_0x1998, MASKBYTE0, (hw_rate | (path << 7)));

	/* Enable TXAGC report */
	odm_set_bb_reg(dm, R_0x1998, BIT(16), 0x1);

	/* Read TX AGC report */
	read_back_data = (u8)odm_get_bb_reg(dm, R_0xd30, 0x7f0000);

	/* Driver have to disable TXAGC report after reading TXAGC (ref. user guide v11) */
	odm_set_bb_reg(dm, R_0x1998, BIT(16), 0x0);

	PHYDM_DBG(dm, ODM_PHY_CONFIG, "[%s]: path-%d rate index 0x%x = 0x%x\n",
		  __func__, path, hw_rate, read_back_data);
	return read_back_data;
#else
	return 0;
#endif
}

__iram_odm_func__
boolean
config_phydm_switch_band_8821c(struct dm_struct *dm, u8 central_ch)
{
	u32 rf_reg18;
	boolean rf_reg_status = true;

	PHYDM_DBG(dm, ODM_PHY_CONFIG, "[%s]======================>\n",
		  __func__);

	if (dm->is_disable_phy_api) {
		PHYDM_DBG(dm, ODM_PHY_CONFIG,
			  "[%s]: disable PHY API for debug!!\n", __func__);
		return true;
	}

	rf_reg18 = config_phydm_read_rf_reg_8821c(dm, RF_PATH_A, 0x18, RFREGOFFSETMASK);
	rf_reg_status = rf_reg_status & config_phydm_read_rf_check_8821c(rf_reg18);

	if (central_ch <= 14) {
		/* 2.4G */

		/* Enable CCK block */
		odm_set_bb_reg(dm, R_0x808, BIT(28), 0x1);

		/* Disable MAC CCK check */
		odm_set_bb_reg(dm, R_0x454, BIT(7), 0x0);

		/* Disable BB CCK check */
		odm_set_bb_reg(dm, R_0xa80, BIT(18), 0x0);

		/*CCA Mask*/
		odm_set_bb_reg(dm, R_0x814, 0x0000FC00, 15); /*default value*/

		/* RF band */
		rf_reg18 = (rf_reg18 & (~(BIT(16) | BIT(9) | BIT(8))));
		rf_reg18 = (rf_reg18 & (~(MASKBYTE0)));
		rf_reg18 = (rf_reg18 | central_ch);
#if (PHYDM_FW_API_FUNC_ENABLE_8821C == 1)
		/* Switch WLG/BTG*/
		if (dm->default_rf_set_8821c == SWITCH_TO_BTG)
			config_phydm_switch_rf_set_8821c(dm, SWITCH_TO_BTG);
		else if (dm->default_rf_set_8821c == SWITCH_TO_WLG)
			config_phydm_switch_rf_set_8821c(dm, SWITCH_TO_WLG);
#endif
		/*RF TXA_TANK LUT mode*/
		odm_set_rf_reg(dm, RF_PATH_A, RF_0xdf, BIT(6), 0x1);

		/*RF TXA_PA_TANK*/
		odm_set_rf_reg(dm, RF_PATH_A, RF_0x64, 0x0000f, 0xf);

	} else if (central_ch > 35) {
		/* 5G */

		/* Enable BB CCK check */
		odm_set_bb_reg(dm, R_0xa80, BIT(18), 0x1);

		/* Enable CCK check */
		odm_set_bb_reg(dm, R_0x454, BIT(7), 0x1);

		/* Disable CCK block */
		odm_set_bb_reg(dm, R_0x808, BIT(28), 0x0);

		/*CCA Mask*/
		odm_set_bb_reg(dm, R_0x814, 0x0000FC00, 15); /*default value*/
		/*odm_set_bb_reg(dm, R_0x814, 0x0000FC00, 34); CCA mask = 13.6us*/

		/* RF band */
		rf_reg18 = (rf_reg18 & (~(BIT(16) | BIT(9) | BIT(8))));
		rf_reg18 = (rf_reg18 | BIT(8) | BIT(16));
		rf_reg18 = (rf_reg18 & (~(MASKBYTE0)));
		rf_reg18 = (rf_reg18 | central_ch);
#if (PHYDM_FW_API_FUNC_ENABLE_8821C == 1)
		/* Switch WLA */
		config_phydm_switch_rf_set_8821c(dm, SWITCH_TO_WLA);
#endif
		/*RF TXA_TANK LUT mode*/
		odm_set_rf_reg(dm, RF_PATH_A, RF_0xdf, BIT(6), 0x0);

	} else {
		PHYDM_DBG(dm, ODM_PHY_CONFIG,
			  "[%s]: Fail to switch band (ch: %d)\n", __func__,
			  central_ch);
		return false;
	}

	phydm_stop_ic_trx(dm, PHYDM_SET);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x18, RFREGOFFSETMASK, rf_reg18);
	phydm_stop_ic_trx(dm, PHYDM_REVERT);

	if (phydm_rfe_8821c(dm, central_ch) == false)
		return false;

	if (!rf_reg_status) {
		PHYDM_DBG(dm, ODM_PHY_CONFIG,
			  "[%s]: Fail to switch band (ch: %d), because writing RF register is fail\n",
			  __func__, central_ch);
		return false;
	}

	PHYDM_DBG(dm, ODM_PHY_CONFIG, "[%s]: Success to switch band (ch: %d)\n",
		  __func__, central_ch);
	return true;
}

__iram_odm_func__
boolean
config_phydm_switch_channel_8821c(struct dm_struct *dm, u8 central_ch)
{
	struct phydm_dig_struct *dig_t = &dm->dm_dig_table;
	u32 rf_reg18, rf_reg_b8 = 0;
	boolean rf_reg_status = true;

	PHYDM_DBG(dm, ODM_PHY_CONFIG, "[%s]====================>\n", __func__);

	if (dm->is_disable_phy_api) {
		PHYDM_DBG(dm, ODM_PHY_CONFIG,
			  "[%s]: disable PHY API for debug!!\n", __func__);
		return true;
	}

	central_ch_8821c = central_ch;
	rf_reg18 = config_phydm_read_rf_reg_8821c(dm, RF_PATH_A, 0x18, RFREGOFFSETMASK);
	rf_reg_status = rf_reg_status & config_phydm_read_rf_check_8821c(rf_reg18);

	if (dm->cut_version == ODM_CUT_A) {
		rf_reg_b8 = config_phydm_read_rf_reg_8821c(dm, RF_PATH_A, 0xb8, RFREGOFFSETMASK);
		rf_reg_status = rf_reg_status & config_phydm_read_rf_check_8821c(rf_reg_b8);
	}

	/* Switch band and channel */
	if (central_ch <= 14) {
		/* 2.4G */

		/* 1. RF band and channel*/
		rf_reg18 = (rf_reg18 & (~(BIT(18) | BIT(17) | MASKBYTE0)));
		rf_reg18 = (rf_reg18 | central_ch);

		/* 2. AGC table selection */
		odm_set_bb_reg(dm, R_0xc1c, 0x00000F00, 0x0);
		dig_t->agc_table_idx = 0x0;

		/* 3. Set central frequency for clock offset tracking */
		odm_set_bb_reg(dm, R_0x860, 0x1ffe0000, 0x96a);

		/* Fix A-cut LCK fail issue @ 5285MHz~5375MHz, 0xb8[19]=0x0 */
		if (dm->cut_version == ODM_CUT_A)
			rf_reg_b8 = rf_reg_b8 | BIT(19);

		/* CCK TX filter parameters */
		if (central_ch == 14) {
			odm_set_bb_reg(dm, R_0xa24, MASKDWORD, 0x0000b81c);
			odm_set_bb_reg(dm, R_0xa28, MASKLWORD, 0x0000);
			odm_set_bb_reg(dm, R_0xaac, MASKDWORD, 0x00003667);
		} else {
			odm_set_bb_reg(dm, R_0xa24, MASKDWORD, rega24_8821c);
			odm_set_bb_reg(dm, R_0xa28, MASKLWORD, (rega28_8821c & MASKLWORD));
			odm_set_bb_reg(dm, R_0xaac, MASKDWORD, regaac_8821c);
		}

	} else if (central_ch > 35) {
		/* 5G */

		/* 1. RF band and channel*/
		rf_reg18 = (rf_reg18 & (~(BIT(18) | BIT(17) | MASKBYTE0)));
		rf_reg18 = (rf_reg18 | central_ch);

		if (central_ch >= 36 && central_ch <= 64) {
			;
		} else if ((central_ch >= 100) && (central_ch <= 140)) {
			rf_reg18 = (rf_reg18 | BIT(17));
		} else if (central_ch > 140) {
			rf_reg18 = (rf_reg18 | BIT(18));
		} else {
			PHYDM_DBG(dm, ODM_PHY_CONFIG,
				  "[%s]: Fail to switch channel (RF18) (ch: %d)\n",
				  __func__, central_ch);
			return false;
		}

		/* 2. AGC table selection */
		if (central_ch >= 36 && central_ch <= 64) {
			odm_set_bb_reg(dm, R_0xc1c, 0x00000F00, 0x1);
			dig_t->agc_table_idx = 0x1;
		} else if ((central_ch >= 100) && (central_ch <= 144)) {
			odm_set_bb_reg(dm, R_0xc1c, 0x00000F00, 0x2);
			dig_t->agc_table_idx = 0x2;
		} else if (central_ch >= 149) {
			odm_set_bb_reg(dm, R_0xc1c, 0x00000F00, 0x3);
			dig_t->agc_table_idx = 0x3;
		} else {
			PHYDM_DBG(dm, ODM_PHY_CONFIG,
				  "[%s]: Fail to switch channel (AGC) (ch: %d)\n",
				  __func__, central_ch);
			return false;
		}

		/* 3. Set central frequency for clock offset tracking */
		if (central_ch >= 36 && central_ch <= 48) {
			odm_set_bb_reg(dm, R_0x860, 0x1ffe0000, 0x494);
		} else if ((central_ch >= 52) && (central_ch <= 64)) {
			odm_set_bb_reg(dm, R_0x860, 0x1ffe0000, 0x453);
		} else if ((central_ch >= 100) && (central_ch <= 116)) {
			odm_set_bb_reg(dm, R_0x860, 0x1ffe0000, 0x452);
		} else if ((central_ch >= 118) && (central_ch <= 177)) {
			odm_set_bb_reg(dm, R_0x860, 0x1ffe0000, 0x412);
		} else {
			PHYDM_DBG(dm, ODM_PHY_CONFIG,
				  "[%s]: Fail to switch channel (fc_area) (ch: %d)\n",
				  __func__, central_ch);
			return false;
		}

		/* Fix A-cut LCK fail issue @ 5285MHz~5375MHz, 0xb8[19]=0x0 */
		if (dm->cut_version == ODM_CUT_A) {
			if (central_ch >= 57 && central_ch <= 75)
				rf_reg_b8 = rf_reg_b8 & (~BIT(19));
			else
				rf_reg_b8 = rf_reg_b8 | BIT(19);
		}

#if (PHYDM_FW_API_FUNC_ENABLE_8821C == 1)
		/*notch 5760 spur by CSI_MASK*/
		if (central_ch == 153)
			phydm_csi_mask_setting(dm, FUNC_ENABLE, (u32)central_ch, 20, 5760, PHYDM_DONT_CARE);
		else if (central_ch == 151)
			phydm_csi_mask_setting(dm, FUNC_ENABLE, (u32)central_ch, 40, 5760, PHYDM_DONT_CARE);
		else if (central_ch == 155)
			phydm_csi_mask_setting(dm, FUNC_ENABLE, (u32)central_ch, 80, 5760, PHYDM_DONT_CARE);
		else
			phydm_csi_mask_setting(dm, FUNC_DISABLE, (u32)central_ch, 80, 5760, PHYDM_DONT_CARE);
#endif

	} else {
		PHYDM_DBG(dm, ODM_PHY_CONFIG,
			  "[%s]: Fail to switch band (ch: %d)\n", __func__,
			  central_ch);
		return false;
	}

	phydm_stop_ic_trx(dm, PHYDM_SET);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x18, RFREGOFFSETMASK, rf_reg18);
	phydm_stop_ic_trx(dm, PHYDM_REVERT);

	if (dm->cut_version == ODM_CUT_A)
		odm_set_rf_reg(dm, RF_PATH_A, RF_0xb8, RFREGOFFSETMASK, rf_reg_b8);

	if (!rf_reg_status) {
		PHYDM_DBG(dm, ODM_PHY_CONFIG,
			  "[%s]: Fail to switch channel (ch: %d), because writing RF register is fail\n",
			  __func__, central_ch);
		return false;
	}

	/* Dynamic spur detection by PSD and NBI mask */
	if (*dm->mp_mode)
		phydm_dynamic_spur_det_eliminate_8821c(dm);

	phydm_ccapar_8821c(dm);
	PHYDM_DBG(dm, ODM_PHY_CONFIG,
		  "[%s]: Success to switch channel (ch: %d)\n", __func__,
		  central_ch);
	return true;
}

__iram_odm_func__
boolean
config_phydm_switch_bandwidth_8821c(struct dm_struct *dm, u8 primary_ch_idx,
				    enum channel_width bandwidth)
{
	u32 rf_reg18;
	boolean rf_reg_status = true;
	u32 bb_reg8ac;

	PHYDM_DBG(dm, ODM_PHY_CONFIG, "[%s]===================>\n", __func__);

	if (dm->is_disable_phy_api) {
		PHYDM_DBG(dm, ODM_PHY_CONFIG,
			  "[%s]: disable PHY API for debug!!\n", __func__);
		return true;
	}

	/* Error handling */
	if (bandwidth >= CHANNEL_WIDTH_MAX || (bandwidth == CHANNEL_WIDTH_40 && primary_ch_idx > 2) || (bandwidth == CHANNEL_WIDTH_80 && primary_ch_idx > 4)) {
		PHYDM_DBG(dm, ODM_PHY_CONFIG,
			  "[%s]: Fail to switch bandwidth (bw: %d, primary ch: %d)\n",
			  __func__, bandwidth, primary_ch_idx);
		return false;
	}
	/*Make protection*/
	if (central_ch_8821c == 165 && !(*dm->mp_mode))
		bandwidth = CHANNEL_WIDTH_20;

	bw_8821c = bandwidth;
	rf_reg18 = config_phydm_read_rf_reg_8821c(dm, RF_PATH_A, 0x18, RFREGOFFSETMASK);
	rf_reg_status = rf_reg_status & config_phydm_read_rf_check_8821c(rf_reg18);

	/* Switch bandwidth */
	switch (bandwidth) {
	case CHANNEL_WIDTH_20: {
/* Small BW([7:6]) = 0, primary channel ([5:2]) = 0, rf mode([1:0]) = 20M */
#if 0
		odm_set_bb_reg(dm, R_0x8ac, MASKBYTE0, CHANNEL_WIDTH_20);

		/* ADC clock = 160M clock for BW20 */
		odm_set_bb_reg(dm, R_0x8ac, (BIT(9) | BIT(8)), 0x0);
		odm_set_bb_reg(dm, R_0x8ac, BIT(16), 0x1);

		/* DAC clock = 160M clock for BW20 */
		odm_set_bb_reg(dm, R_0x8ac, (BIT(21) | BIT(20)), 0x0);
		odm_set_bb_reg(dm, R_0x8ac, BIT(28), 0x1);
#endif
		bb_reg8ac = odm_get_bb_reg(dm, R_0x8ac, MASKDWORD);
		bb_reg8ac &= 0xffcffc00;
		bb_reg8ac |= 0x10010000;
		odm_set_bb_reg(dm, R_0x8ac, MASKDWORD, bb_reg8ac);

		/* ADC buffer clock */
		odm_set_bb_reg(dm, R_0x8c4, BIT(30), 0x1);

		/* RF bandwidth */
		rf_reg18 = (rf_reg18 | BIT(11) | BIT(10));

		break;
	}
	case CHANNEL_WIDTH_40: {
		/* Small BW([7:6]) = 0, primary channel ([5:2]) = sub-channel, rf mode([1:0]) = 40M */
		/*odm_set_bb_reg(dm, R_0x8ac, MASKBYTE0, (((primary_ch_idx & 0xf) << 2) | CHANNEL_WIDTH_40));*/

		/* CCK primary channel */
		if (primary_ch_idx == 1)
			odm_set_bb_reg(dm, R_0xa00, BIT(4), primary_ch_idx);
		else
			odm_set_bb_reg(dm, R_0xa00, BIT(4), 0);
#if 0
		/* ADC clock = 160M clock for BW40 */
		odm_set_bb_reg(dm, R_0x8ac, (BIT(11) | BIT(10)), 0x0);
		odm_set_bb_reg(dm, R_0x8ac, BIT(17), 0x1);

		/* DAC clock = 160M clock for BW20 */
		odm_set_bb_reg(dm, R_0x8ac, (BIT(23) | BIT(22)), 0x0);
		odm_set_bb_reg(dm, R_0x8ac, BIT(29), 0x1);
#endif
		bb_reg8ac = odm_get_bb_reg(dm, R_0x8ac, MASKDWORD);
		bb_reg8ac &= 0xff3ff300;
		bb_reg8ac |= 0x20020000 | ((primary_ch_idx & 0xf) << 2) | CHANNEL_WIDTH_40;
		odm_set_bb_reg(dm, R_0x8ac, MASKDWORD, bb_reg8ac);

		/* ADC buffer clock */
		odm_set_bb_reg(dm, R_0x8c4, BIT(30), 0x1);

		/* RF bandwidth */
		rf_reg18 = (rf_reg18 & (~(BIT(11) | BIT(10))));
		rf_reg18 = (rf_reg18 | BIT(11));

		break;
	}
	case CHANNEL_WIDTH_80: {
/* Small BW([7:6]) = 0, primary channel ([5:2]) = sub-channel, rf mode([1:0]) = 80M */
#if 0
		odm_set_bb_reg(dm, R_0x8ac, MASKBYTE0, (((primary_ch_idx & 0xf) << 2) | CHANNEL_WIDTH_80));

		/* ADC clock = 160M clock for BW80 */
		odm_set_bb_reg(dm, R_0x8ac, (BIT(13) | BIT(12)), 0x0);
		odm_set_bb_reg(dm, R_0x8ac, BIT(18), 0x1);

		/* DAC clock = 160M clock for BW20 */
		odm_set_bb_reg(dm, R_0x8ac, (BIT(25) | BIT(24)), 0x0);
		odm_set_bb_reg(dm, R_0x8ac, BIT(30), 0x1);
#endif
		bb_reg8ac = odm_get_bb_reg(dm, R_0x8ac, MASKDWORD);
		bb_reg8ac &= 0xfcffcf00;
		bb_reg8ac |= 0x40040000 | ((primary_ch_idx & 0xf) << 2) | CHANNEL_WIDTH_80;
		odm_set_bb_reg(dm, R_0x8ac, MASKDWORD, bb_reg8ac);

		/* ADC buffer clock */
		odm_set_bb_reg(dm, R_0x8c4, BIT(30), 0x1);

		/* RF bandwidth */
		rf_reg18 = (rf_reg18 & (~(BIT(11) | BIT(10))));
		rf_reg18 = (rf_reg18 | BIT(10));

		break;
	}
	case CHANNEL_WIDTH_5: {
/* Small BW([7:6]) = 1, primary channel ([5:2]) = 0, rf mode([1:0]) = 20M */
#if 0
		odm_set_bb_reg(dm, R_0x8ac, MASKBYTE0, (BIT(6) | CHANNEL_WIDTH_20));

		/* ADC clock = 40M clock */
		odm_set_bb_reg(dm, R_0x8ac, (BIT(9) | BIT(8)), 0x2);
		odm_set_bb_reg(dm, R_0x8ac, BIT(16), 0x0);

		/* DAC clock = 160M clock for BW20 */
		odm_set_bb_reg(dm, R_0x8ac, (BIT(21) | BIT(20)), 0x2);
		odm_set_bb_reg(dm, R_0x8ac, BIT(28), 0x0);
#endif
		bb_reg8ac = odm_get_bb_reg(dm, R_0x8ac, MASKDWORD);
		bb_reg8ac &= 0xefcefc00;
		bb_reg8ac |= (0x2 << 20) | (0x2 << 8) | BIT(6);
		odm_set_bb_reg(dm, R_0x8ac, MASKDWORD, bb_reg8ac);

		/* ADC buffer clock */
		odm_set_bb_reg(dm, R_0x8c4, BIT(30), 0x0);
		odm_set_bb_reg(dm, R_0x8c8, BIT(31), 0x1);

		/* RF bandwidth */
		rf_reg18 = (rf_reg18 | BIT(11) | BIT(10));

		break;
	}
	case CHANNEL_WIDTH_10: {
/* Small BW([7:6]) = 1, primary channel ([5:2]) = 0, rf mode([1:0]) = 20M */
#if 0
		odm_set_bb_reg(dm, R_0x8ac, MASKBYTE0, (BIT(7) | CHANNEL_WIDTH_20));

		/* ADC clock = 80M clock */
		odm_set_bb_reg(dm, R_0x8ac, (BIT(9) | BIT(8)), 0x3);
		odm_set_bb_reg(dm, R_0x8ac, BIT(16), 0x0);

		/* DAC clock = 160M clock for BW20 */
		odm_set_bb_reg(dm, R_0x8ac, (BIT(21) | BIT(20)), 0x3);
		odm_set_bb_reg(dm, R_0x8ac, BIT(28), 0x0);
#endif
		bb_reg8ac = odm_get_bb_reg(dm, R_0x8ac, MASKDWORD);
		bb_reg8ac &= 0xefcefc00;
		bb_reg8ac |= (0x3 << 20) | (0x3 << 8) | BIT(7);
		odm_set_bb_reg(dm, R_0x8ac, MASKDWORD, bb_reg8ac);

		/* ADC buffer clock */
		odm_set_bb_reg(dm, R_0x8c4, BIT(30), 0x0);
		odm_set_bb_reg(dm, R_0x8c8, BIT(31), 0x1);

		/* RF bandwidth */
		rf_reg18 = (rf_reg18 | BIT(11) | BIT(10));

		break;
	}
	default:
		PHYDM_DBG(dm, ODM_PHY_CONFIG,
			  "[%s]: Fail to switch bandwidth (bw: %d, primary ch: %d)\n",
			  __func__, bandwidth, primary_ch_idx);
	}

	/* Write RF register */
	phydm_stop_ic_trx(dm, PHYDM_SET);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x18, RFREGOFFSETMASK, rf_reg18);
	phydm_stop_ic_trx(dm, PHYDM_REVERT);

	if (!rf_reg_status) {
		PHYDM_DBG(dm, ODM_PHY_CONFIG,
			  "[%s]: Fail to switch bandwidth (bw: %d, primary ch: %d), because writing RF register is fail\n",
			  __func__, bandwidth, primary_ch_idx);
		return false;
	}

	/* Modify RX DFIR parameters */
	phydm_rxdfirpar_by_bw_8821c(dm, bandwidth);

	/* Modify CCA parameters */
	phydm_ccapar_by_bw_8821c(dm, bandwidth);
	phydm_ccapar_8821c(dm);

	/* Toggle RX path to avoid RX dead zone issue */
	/*odm_set_bb_reg(dm, R_0x808, MASKBYTE0, 0x0);*/
	/*odm_set_bb_reg(dm, R_0x808, MASKBYTE0, 0x11);*/

	/* Dynamic spur detection by PSD and NBI mask */
	if (*dm->mp_mode)
		phydm_dynamic_spur_det_eliminate_8821c(dm);

	PHYDM_DBG(dm, ODM_PHY_CONFIG,
		  "[%s]: Success to switch bandwidth (bw: %d, primary ch: %d)\n",
		  __func__, bandwidth, primary_ch_idx);
	return true;
}

__iram_odm_func__
boolean
config_phydm_switch_channel_bw_8821c(struct dm_struct *dm, u8 central_ch,
				     u8 primary_ch_idx,
				     enum channel_width bandwidth)
{
	/* Switch band */
	if (config_phydm_switch_band_8821c(dm, central_ch) == false)
		return false;

	/* Switch channel */
	if (config_phydm_switch_channel_8821c(dm, central_ch) == false)
		return false;

	/* Switch bandwidth */
	if (config_phydm_switch_bandwidth_8821c(dm, primary_ch_idx, bandwidth) == false)
		return false;

	return true;
}

__iram_odm_func__
boolean
config_phydm_trx_mode_8821c(struct dm_struct *dm, enum bb_path tx_path,
			    enum bb_path rx_path, boolean is_tx2_path)
{
	return true;
}

__iram_odm_func__
boolean
config_phydm_parameter_init_8821c(struct dm_struct *dm,
				  enum odm_parameter_init type)
{
	if (type == ODM_PRE_SETTING) {
		odm_set_bb_reg(dm, R_0x808, (BIT(28) | BIT(29)), 0x0);
		PHYDM_DBG(dm, ODM_PHY_CONFIG,
			  "[%s]: Pre setting: disable OFDM and CCK block\n",
			  __func__);
	} else if (type == ODM_POST_SETTING) {
		odm_set_bb_reg(dm, R_0x808, (BIT(28) | BIT(29)), 0x3);
		PHYDM_DBG(dm, ODM_PHY_CONFIG,
			  "[%s]: Post setting: enable OFDM and CCK block\n",
			  __func__);
		rega24_8821c = odm_get_bb_reg(dm, R_0xa24, MASKDWORD);
		rega28_8821c = odm_get_bb_reg(dm, R_0xa28, MASKDWORD);
		regaac_8821c = odm_get_bb_reg(dm, R_0xaac, MASKDWORD);
	} else {
		PHYDM_DBG(dm, ODM_PHY_CONFIG, "[%s]: Wrong type!!\n", __func__);
		return false;
	}

	return true;
}

__iram_odm_func__
void config_phydm_switch_rf_set_8821c(struct dm_struct *dm, u8 rf_set)
{
#if (PHYDM_FW_API_FUNC_ENABLE_8821C == 1)
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	void *adapter = dm->adapter;
	PMGNT_INFO mgnt_info = &(((PADAPTER)(adapter))->MgntInfo);
#endif

	u32 bb_reg32;

	odm_set_bb_reg(dm, R_0x1080, BIT(16), 0x1);
	odm_set_bb_reg(dm, R_0x00, BIT(26), 0x1);
	/*odm_set_mac_reg(dm, R_0x70, BIT(26), 0x1);*/
	/*odm_set_mac_reg(dm, R_0x1704, MASKLWORD, 0x4000);*/
	/*odm_set_mac_reg(dm, R_0x1700, (BIT(31) | BIT(30)), 0x3); */

	bb_reg32 = odm_get_bb_reg(dm, R_0xcb8, MASKDWORD);
	switch (rf_set) {
	case SWITCH_TO_BTG:

		dm->current_rf_set_8821c = SWITCH_TO_BTG;

		bb_reg32 = (bb_reg32 | BIT(16));
		bb_reg32 &= (~(BIT(18) | BIT(20) | BIT(21) | BIT(22) | BIT(23)));
		odm_set_bb_reg(dm, R_0xa84, MASKBYTE2, 0xe);
		odm_set_bb_reg(dm, R_0xa80, MASKLWORD, 0xfc84);

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
		if (*dm->mp_mode && mgnt_info->RegPHYParaFromFolder == 0)
#else
		if (*dm->mp_mode)
#endif
		{
			odm_set_bb_reg(dm, R_0xaa8, 0x1f0000, 0x14);
			odm_config_bb_with_header_file(dm, CONFIG_BB_AGC_TAB_DIFF);
			/*Toggle initial gain twice for valid gain table*/
			odm_set_bb_reg(dm, ODM_REG(IGI_A, dm), ODM_BIT(IGI, dm), 0x22);
			odm_set_bb_reg(dm, ODM_REG(IGI_A, dm), ODM_BIT(IGI, dm), 0x20);
		}
		break;
	case SWITCH_TO_WLG:

		dm->current_rf_set_8821c = SWITCH_TO_WLG;

		bb_reg32 = (bb_reg32 | BIT(20) | BIT(21) | BIT(22));
		bb_reg32 &= (~(BIT(16) | BIT(18) | BIT(23)));
		odm_set_bb_reg(dm, R_0xa84, MASKBYTE2, 0x12);
		odm_set_bb_reg(dm, R_0xa80, MASKLWORD, 0x7532);

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
		if (*dm->mp_mode && mgnt_info->RegPHYParaFromFolder == 0)
#else
		if (*dm->mp_mode)
#endif
		{
			odm_set_bb_reg(dm, R_0xaa8, 0x1f0000, 0x13);
			odm_config_bb_with_header_file(dm, CONFIG_BB_AGC_TAB_DIFF);
			/*Toggle initial gain twice for valid gain table*/
			odm_set_bb_reg(dm, ODM_REG(IGI_A, dm), ODM_BIT(IGI, dm), 0x22);
			odm_set_bb_reg(dm, ODM_REG(IGI_A, dm), ODM_BIT(IGI, dm), 0x20);
		}
		break;
	case SWITCH_TO_WLA:

		dm->current_rf_set_8821c = SWITCH_TO_WLA;

		bb_reg32 = (bb_reg32 | BIT(20) | BIT(22) | BIT(23));
		bb_reg32 &= (~(BIT(16) | BIT(18) | BIT(21)));

		break;
	case SWITCH_TO_BT:

		dm->current_rf_set_8821c = SWITCH_TO_BT;

		break;
	default:
		break;
	}

	odm_set_bb_reg(dm, R_0xcb8, MASKDWORD, bb_reg32);
#endif
}

__iram_odm_func__
void config_phydm_set_ant_path(struct dm_struct *dm, u8 rf_set, u8 ant_num)
{
#if (PHYDM_FW_API_FUNC_ENABLE_8821C == 1)
	boolean switch_polarity_inverse = false;
	u8 regval_0xcb7 = 0;

	dm->current_ant_num_8821c = ant_num;
	config_phydm_switch_rf_set_8821c(dm, rf_set);

	if (rf_set == SWITCH_TO_BT)
		phydm_set_gnt_state_8821c(dm, false, true); /* GNT_WL=0, GNT_BT=1 for BT test */
	else
		phydm_set_gnt_state_8821c(dm, true, false); /* GNT_WL=1, GNT_BT=0 for WL test */

	/*switch does not exist*/
	if (dm->rfe_type_expand == 0x5 || dm->rfe_type_expand == 0x6 ||
	    dm->rfe_type_expand == 0x25 || dm->rfe_type_expand == 0x26 ||
	    dm->rfe_type_expand == 0x2a || dm->rfe_type_expand == 0x2d ||
	    dm->rfe_type_expand == 0x2e)
		return;

	if (dm->current_ant_num_8821c) /*Ant1 = 0, Ant2 = 1*/
		switch_polarity_inverse = !switch_polarity_inverse;

	if (rf_set == SWITCH_TO_WLG)
		switch_polarity_inverse = !switch_polarity_inverse;

	/*set antenna control by WL 0xcb4[29:28]*/
	odm_set_bb_reg(dm, R_0x4c, BIT(24) | BIT(23), 0x2);

	/*set RFE_ctrl8 and RFE_ctrl9 as antenna control pins by software*/
	odm_set_bb_reg(dm, R_0xcb4, 0x000000ff, 0x77);

	/*0xcb4[29:28] = 2b'01 for no switch_polatiry_inverse, DPDT_SEL_N =1, DPDT_SEL_P =0*/
	regval_0xcb7 = (!switch_polarity_inverse ? 0x1 : 0x2);

	odm_set_bb_reg(dm, R_0xcb4, 0x30000000, regval_0xcb7);
#endif
}

__iram_odm_func__
u32 query_phydm_trx_capability_8821c(struct dm_struct *dm)
{
#if (PHYDM_FW_API_FUNC_ENABLE_8821C == 1)
	u32 value32 = 0x00000000;

	PHYDM_DBG(dm, ODM_PHY_CONFIG, "[%s]: trx_capability = 0x%x\n", __func__,
		  value32);
	return value32;
#else
	return 0;
#endif
}

__iram_odm_func__
u32 query_phydm_stbc_capability_8821c(struct dm_struct *dm)
{
#if (PHYDM_FW_API_FUNC_ENABLE_8821C == 1)
	u32 value32 = 0x00010001;

	PHYDM_DBG(dm, ODM_PHY_CONFIG, "[%s]: stbc_capability = 0x%x\n",
		  __func__, value32);
	return value32;
#else
	return 0;
#endif
}

__iram_odm_func__
u32 query_phydm_ldpc_capability_8821c(struct dm_struct *dm)
{
#if (PHYDM_FW_API_FUNC_ENABLE_8821C == 1)
	u32 value32 = 0x01000100;

	PHYDM_DBG(dm, ODM_PHY_CONFIG, "[%s]: ldpc_capability = 0x%x\n",
		  __func__, value32);
	return value32;
#else
	return 0;
#endif
}

__iram_odm_func__
u32 query_phydm_txbf_parameters_8821c(struct dm_struct *dm)
{
#if (PHYDM_FW_API_FUNC_ENABLE_8821C == 1)
	u32 value32 = 0x00030003;

	PHYDM_DBG(dm, ODM_PHY_CONFIG, "[%s]: txbf_parameters = 0x%x\n",
		  __func__, value32);
	return value32;
#else
	return 0;
#endif
}

__iram_odm_func__
u32 query_phydm_txbf_capability_8821c(struct dm_struct *dm)
{
#if (PHYDM_FW_API_FUNC_ENABLE_8821C == 1)
	u32 value32 = 0x01010001;

	PHYDM_DBG(dm, ODM_PHY_CONFIG, "[%s]: txbf_capability = 0x%x\n",
		  __func__, value32);
	return value32;
#else
	return 0;
#endif
}

__iram_odm_func__
u8 query_phydm_default_rf_set_8821c(struct dm_struct *dm)
{
#if (PHYDM_FW_API_FUNC_ENABLE_8821C == 1)
	return dm->default_rf_set_8821c;
#else
	return 0;
#endif
}

__iram_odm_func__
u8 query_phydm_current_rf_set_8821c(struct dm_struct *dm)
{
#if (PHYDM_FW_API_FUNC_ENABLE_8821C == 1)
	return dm->current_rf_set_8821c;
#else
	return 0;
#endif
}

__iram_odm_func__
u8 query_phydm_rfetype_8821c(struct dm_struct *dm)
{
#if (PHYDM_FW_API_FUNC_ENABLE_8821C == 1)
	return dm->rfe_type_expand;
#else
	return 0;
#endif
}

__iram_odm_func__
u8 query_phydm_current_ant_num_8821c(struct dm_struct *dm)
{
#if (PHYDM_FW_API_FUNC_ENABLE_8821C == 1)
	u32 regval_0xcb4 = odm_get_bb_reg(dm, R_0xcb4, BIT(29) | BIT(28));

	if (dm->current_rf_set_8821c == SWITCH_TO_BTG || dm->current_rf_set_8821c == SWITCH_TO_WLA || dm->current_rf_set_8821c == SWITCH_TO_BT) {
		if (regval_0xcb4 == 1)
			dm->current_ant_num_8821c = SWITCH_TO_ANT1;
		else if (regval_0xcb4 == 2)
			dm->current_ant_num_8821c = SWITCH_TO_ANT2;
		else if (regval_0xcb4 == 1)
			dm->current_ant_num_8821c = SWITCH_TO_ANT2;
		else if (regval_0xcb4 == 2)
			dm->current_ant_num_8821c = SWITCH_TO_ANT1;
	}

	return dm->current_ant_num_8821c;
#else
	return 0;
#endif
}

__iram_odm_func__
u8 query_phydm_ant_num_map_8821c(struct dm_struct *dm)
{
#if (PHYDM_FW_API_FUNC_ENABLE_8821C == 1)
	u8 mapping_table = 0;

	/* mapping table meaning
	 * 1: choose ant1 or ant2
	 * 2: only ant1
	 * 3: only ant2
	 * 4: cannot choose
	 */

	if (dm->rfe_type_expand == 0 || dm->rfe_type_expand == 7 || dm->rfe_type_expand == 0x20 ||
	    dm->rfe_type_expand == 0x27 || dm->rfe_type_expand == 0x28 || dm->rfe_type_expand == 0x2f)
		mapping_table = 1;
	else if (dm->rfe_type_expand == 1 || dm->rfe_type_expand == 2 || dm->rfe_type_expand == 0x21 ||
		 dm->rfe_type_expand == 0x22 || dm->rfe_type_expand == 0x29 || dm->rfe_type_expand == 0x2a)
		mapping_table = 2;
	else if (dm->rfe_type_expand == 3 || dm->rfe_type_expand == 4 || dm->rfe_type_expand == 0x23 ||
		 dm->rfe_type_expand == 0x24 || dm->rfe_type_expand == 0x2b || dm->rfe_type_expand == 0x2c)
		mapping_table = 3;
	else if (dm->rfe_type_expand == 5 || dm->rfe_type_expand == 6 || dm->rfe_type_expand == 0x25 ||
		 dm->rfe_type_expand == 0x26 || dm->rfe_type_expand == 0x2d || dm->rfe_type_expand == 0x2e)
		mapping_table = 4;

	return mapping_table;
#else
	return 0;
#endif
}

/* ======================================================================== */
#endif /*PHYDM_FW_API_ENABLE_8821C == 1*/
#endif /* RTL8821C_SUPPORT == 1 */
