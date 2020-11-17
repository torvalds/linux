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

/* ************************************************************
 * include files
 * ************************************************************ */

#include "mp_precomp.h"
#include "phydm_precomp.h"

/*******************************************************
 * when antenna test utility is on or some testing need to disable antenna diversity
 * call this function to disable all ODM related mechanisms which will switch antenna.
 ******************************************************/
#if (defined(CONFIG_SMART_ANTENNA))

#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
#if (RTL8198F_SUPPORT == 1)
void phydm_smt_ant_init_98f(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 val = 0;

	#if 0
	odm_set_bb_reg(dm, R_0x1da4, 0x3c, 4); /*6.25*4 = 25ms*/
	odm_set_bb_reg(dm, R_0x1da4, BIT(6), 1);
	odm_set_bb_reg(dm, R_0x1da4, BIT(7), 1);
	#endif
}
#endif
#endif

#if (defined(CONFIG_CUMITEK_SMART_ANTENNA))
void phydm_cumitek_smt_ant_mapping_table_8822b(
	void *dm_void,
	u8 *table_path_a,
	u8 *table_path_b)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 path_a_0to3_idx = 0;
	u32 path_b_0to3_idx = 0;
	u32 path_a_4to7_idx = 0;
	u32 path_b_4to7_idx = 0;

	path_a_0to3_idx = ((table_path_a[3] & 0xf) << 24) | ((table_path_a[2] & 0xf) << 16) | ((table_path_a[1] & 0xf) << 8) | (table_path_a[0] & 0xf);

	path_b_0to3_idx = ((table_path_b[3] & 0xf) << 28) | ((table_path_b[2] & 0xf) << 20) | ((table_path_b[1] & 0xf) << 12) | ((table_path_b[0] & 0xf) << 4);

	path_a_4to7_idx = ((table_path_a[7] & 0xf) << 24) | ((table_path_a[6] & 0xf) << 16) | ((table_path_a[5] & 0xf) << 8) | (table_path_a[4] & 0xf);

	path_b_4to7_idx = ((table_path_b[7] & 0xf) << 28) | ((table_path_b[6] & 0xf) << 20) | ((table_path_b[5] & 0xf) << 12) | ((table_path_b[4] & 0xf) << 4);

#if 0
	/*PHYDM_DBG(dm, DBG_SMT_ANT, "mapping table{A, B} = {0x%x, 0x%x}\n", path_a_0to3_idx, path_b_0to3_idx);*/
#endif

	/*pathA*/
	odm_set_bb_reg(dm, R_0xca4, MASKDWORD, path_a_0to3_idx); /*@ant map 1*/
	odm_set_bb_reg(dm, R_0xca8, MASKDWORD, path_a_4to7_idx); /*@ant map 2*/

	/*pathB*/
	odm_set_bb_reg(dm, R_0xea4, MASKDWORD, path_b_0to3_idx); /*@ant map 1*/
	odm_set_bb_reg(dm, R_0xea8, MASKDWORD, path_b_4to7_idx); /*@ant map 2*/
}

void phydm_cumitek_smt_ant_init_8822b(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct smt_ant *smtant_table = &dm->smtant_table;
	struct smt_ant_cumitek *cumi_smtant_table = &dm->smtant_table.cumi_smtant_table;
	u32 value32;

	PHYDM_DBG(dm, DBG_SMT_ANT, "[8822B Cumitek SmtAnt Int]\n");

	/*@========= MAC GPIO setting =================================*/

	/* Pin, pin_name, RFE_CTRL_NUM*/

	/* @A0, 55, 5G_TRSW, 3*/
	/* @A1, 52, 5G_TRSW, 0*/
	/* @A2, 25, 5G_TRSW, 8*/

	/* @B0, 16, 5G_TRSW, 4*/
	/* @B1, 13, 5G_TRSW, 11*/
	/* @B2, 24, 5G_TRSW, 9*/

	/*@for RFE_CTRL 8 & 9*/
	odm_set_mac_reg(dm, R_0x4c, BIT(24) | BIT(23), 2);
	odm_set_mac_reg(dm, R_0x44, BIT(27) | BIT(26), 0);

	/*@for RFE_CTRL 0*/
	odm_set_mac_reg(dm, R_0x4c, BIT(25), 0);
	odm_set_mac_reg(dm, R_0x64, BIT(29), 1);

	/*@for RFE_CTRL 2 & 3*/
	odm_set_mac_reg(dm, R_0x4c, BIT(26), 0);
	odm_set_mac_reg(dm, R_0x64, BIT(28), 1);

	/*@for RFE_CTRL 11*/
	odm_set_mac_reg(dm, R_0x40, BIT(3), 1);

	/*@0x604[25]=1 : 2bit mode for pathA&B&C&D*/
	/*@0x604[25]=0 : 3bit mode for pathA&B*/
	smtant_table->tx_desc_mode = 0;
	odm_set_mac_reg(dm, R_0x604, BIT(25), (u32)smtant_table->tx_desc_mode);

	/*@========= BB RFE setting =================================*/
#if 0
	/*path A*/
	odm_set_bb_reg(dm, R_0x1990, BIT(3), 0);		/*RFE_CTRL_3*/ /*A_0*/
	odm_set_bb_reg(dm, R_0xcbc, BIT(3), 0);		/*@inv*/
	odm_set_bb_reg(dm, R_0xcb0, 0xf000, 8);

	odm_set_bb_reg(dm, R_0x1990, BIT(0), 0);		/*RFE_CTRL_0*/ /*A_1*/
	odm_set_bb_reg(dm, R_0xcbc, BIT(0), 0);		/*@inv*/
	odm_set_bb_reg(dm, R_0xcb0, 0xf, 0x9);

	odm_set_bb_reg(dm, R_0x1990, BIT(8), 0);		/*RFE_CTRL_8*/ /*A_2*/
	odm_set_bb_reg(dm, R_0xcbc, BIT(8), 0);		/*@inv*/
	odm_set_bb_reg(dm, R_0xcb4, 0xf, 0xa);


	/*path B*/
	odm_set_bb_reg(dm, R_0x1990, BIT(4), 1);		/*RFE_CTRL_4*/	/*B_0*/
	odm_set_bb_reg(dm, R_0xdbc, BIT(4), 0);		/*@inv*/
	odm_set_bb_reg(dm, R_0xdb0, 0xf0000, 0xb);

	odm_set_bb_reg(dm, R_0x1990, BIT(11), 1);	/*RFE_CTRL_11*/	/*B_1*/
	odm_set_bb_reg(dm, R_0xdbc, BIT(11), 0);		/*@inv*/
	odm_set_bb_reg(dm, R_0xdb4, 0xf000, 0xc);

	odm_set_bb_reg(dm, R_0x1990, BIT(9), 1);		/*RFE_CTRL_9*/	/*B_2*/
	odm_set_bb_reg(dm, R_0xdbc, BIT(9), 0);		/*@inv*/
	odm_set_bb_reg(dm, R_0xdb4, 0xf0, 0xd);
#endif
	/*@========= BB SmtAnt setting =================================*/
	odm_set_mac_reg(dm, R_0x6d8, BIT(22) | BIT(21), 2); /*resp tx by register*/
	odm_set_mac_reg(dm, R_0x668, BIT(3), 1);
	odm_set_bb_reg(dm, R_0x804, BIT(4), 0); /*@lathch antsel*/
	odm_set_bb_reg(dm, R_0x818, 0xf00000, 0); /*@keep tx by rx*/
	odm_set_bb_reg(dm, R_0x900, BIT(19), 0); /*@fast train*/
	odm_set_bb_reg(dm, R_0x900, BIT(18), 1); /*@1: by TXDESC*/

	/*pathA*/
	odm_set_bb_reg(dm, R_0xca4, MASKDWORD, 0x03020100); /*@ant map 1*/
	odm_set_bb_reg(dm, R_0xca8, MASKDWORD, 0x07060504); /*@ant map 2*/
	odm_set_bb_reg(dm, R_0xcac, BIT(9), 0); /*@keep antsel map by GNT_BT*/

	/*pathB*/
	odm_set_bb_reg(dm, R_0xea4, MASKDWORD, 0x30201000); /*@ant map 1*/
	odm_set_bb_reg(dm, R_0xea8, MASKDWORD, 0x70605040); /*@ant map 2*/
	odm_set_bb_reg(dm, R_0xeac, BIT(9), 0); /*@keep antsel map by GNT_BT*/
}

void phydm_cumitek_smt_ant_init_8197f(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct smt_ant *smtant_table = &dm->smtant_table;
	struct smt_ant_cumitek *cumi_smtant_table = &dm->smtant_table.cumi_smtant_table;
	u32 value32;

	PHYDM_DBG(dm, DBG_SMT_ANT, "[8197F Cumitek SmtAnt Int]\n");

	/*@GPIO setting*/
}

void phydm_cumitek_smt_ant_init_8192f(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct smt_ant *smtant_table = &dm->smtant_table;
	struct smt_ant_cumitek *cumi_smtant_table = &dm->smtant_table.cumi_smtant_table;
	u32 value32;
	PHYDM_DBG(dm, DBG_SMT_ANT, "[8192F Cumitek SmtAnt Int]\n");

	/*@GPIO setting*/
}

void phydm_cumitek_smt_tx_ant_update(
	void *dm_void,
	u8 tx_ant_idx_path_a,
	u8 tx_ant_idx_path_b,
	u32 mac_id)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct smt_ant *smtant_table = &dm->smtant_table;
	struct smt_ant_cumitek *cumi_smtant_table = &dm->smtant_table.cumi_smtant_table;

	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "[Cumitek] Set TX-ANT[%d] = (( A:0x%x ,  B:0x%x ))\n", mac_id,
		  tx_ant_idx_path_a, tx_ant_idx_path_b);

	/*path-A*/
	cumi_smtant_table->tx_ant_idx[0][mac_id] = tx_ant_idx_path_a; /*@fill this value into TXDESC*/

	/*path-B*/
	cumi_smtant_table->tx_ant_idx[1][mac_id] = tx_ant_idx_path_b; /*@fill this value into TXDESC*/
}

void phydm_cumitek_smt_rx_default_ant_update(
	void *dm_void,
	u8 rx_ant_idx_path_a,
	u8 rx_ant_idx_path_b)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct smt_ant *smtant_table = &dm->smtant_table;
	struct smt_ant_cumitek *cumi_smtant_table = &dm->smtant_table.cumi_smtant_table;

	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "[Cumitek] Set RX-ANT = (( A:0x%x, B:0x%x ))\n",
		  rx_ant_idx_path_a, rx_ant_idx_path_b);

	/*path-A*/
	if (cumi_smtant_table->rx_default_ant_idx[0] != rx_ant_idx_path_a) {
		#if (RTL8822B_SUPPORT == 1)
		if (dm->support_ic_type == ODM_RTL8822B) {
			odm_set_bb_reg(dm, R_0xc08, BIT(21) | BIT(20) | BIT(19), rx_ant_idx_path_a); /*@default RX antenna*/
			odm_set_mac_reg(dm, R_0x6d8, BIT(2) | BIT(1) | BIT(0), rx_ant_idx_path_a); /*@default response TX antenna*/
		}
		#endif

		#if (RTL8197F_SUPPORT == 1)
		if (dm->support_ic_type == ODM_RTL8197F) {
		}
		#endif

		/*@jj add 20170822*/
		#if (RTL8192F_SUPPORT == 1)
		if (dm->support_ic_type == ODM_RTL8192F) {
		}
		#endif
		cumi_smtant_table->rx_default_ant_idx[0] = rx_ant_idx_path_a;
	}

	/*path-B*/
	if (cumi_smtant_table->rx_default_ant_idx[1] != rx_ant_idx_path_b) {
		#if (RTL8822B_SUPPORT == 1)
		if (dm->support_ic_type == ODM_RTL8822B) {
			odm_set_bb_reg(dm, R_0xe08, BIT(21) | BIT(20) | BIT(19), rx_ant_idx_path_b); /*@default antenna*/
			odm_set_mac_reg(dm, R_0x6d8, BIT(5) | BIT(4) | BIT(3), rx_ant_idx_path_b); /*@default response TX antenna*/
		}
		#endif

		#if (RTL8197F_SUPPORT == 1)
		if (dm->support_ic_type == ODM_RTL8197F) {
		}
		#endif

		/*@jj add 20170822*/
		#if (RTL8192F_SUPPORT == 1)
		if (dm->support_ic_type == ODM_RTL8192F) {
		}
		#endif
		cumi_smtant_table->rx_default_ant_idx[1] = rx_ant_idx_path_b;
	}
}

void phydm_cumitek_smt_ant_debug(
	void *dm_void,
	char input[][16],
	u32 *_used,
	char *output,
	u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct smt_ant *smtant_table = &dm->smtant_table;
	struct smt_ant_cumitek *cumi_smtant_table = &dm->smtant_table.cumi_smtant_table;
	u32 used = *_used;
	u32 out_len = *_out_len;
	char help[] = "-h";
	u32 dm_value[10] = {0};
	u8 i;

	PHYDM_SSCANF(input[1], DCMD_DECIMAL, &dm_value[0]);

	if (strcmp(input[1], help) == 0) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "{1} {PathA rx_ant_idx} {pathB rx_ant_idx}\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "{2} {PathA tx_ant_idx} {pathB tx_ant_idx} {macid}\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "{3} {PathA mapping table} {PathB mapping table}\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "{4} {txdesc_mode 0:3bit, 1:2bit}\n");

	} else if (dm_value[0] == 1) { /*@fix rx_idle pattern*/

		PHYDM_SSCANF(input[2], DCMD_DECIMAL, &dm_value[1]);
		PHYDM_SSCANF(input[3], DCMD_DECIMAL, &dm_value[2]);

		phydm_cumitek_smt_rx_default_ant_update(dm, (u8)dm_value[1], (u8)dm_value[2]);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "RX Ant{A, B}={%d, %d}\n", dm_value[1], dm_value[2]);

	} else if (dm_value[0] == 2) { /*@fix tx pattern*/

		for (i = 1; i < 4; i++) {
			if (input[i + 1])
				PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &dm_value[i]);
		}

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "STA[%d] TX Ant{A, B}={%d, %d}\n", dm_value[3],
			 dm_value[1], dm_value[2]);
		phydm_cumitek_smt_tx_ant_update(dm, (u8)dm_value[1], (u8)dm_value[2], (u8)dm_value[3]);

	} else if (dm_value[0] == 3) {
		u8 table_path_a[8] = {0};
		u8 table_path_b[8] = {0};

		for (i = 1; i < 4; i++) {
			if (input[i + 1])
				PHYDM_SSCANF(input[i + 1], DCMD_HEX, &dm_value[i]);
		}

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "Set Path-AB mapping table={%d, %d}\n", dm_value[1],
			 dm_value[2]);

		for (i = 0; i < 8; i++) {
			table_path_a[i] = (u8)((dm_value[1] >> (4 * i)) & 0xf);
			table_path_b[i] = (u8)((dm_value[2] >> (4 * i)) & 0xf);
		}

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "Ant_Table_A[7:0]={0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x}\n",
			 table_path_a[7], table_path_a[6], table_path_a[5],
			 table_path_a[4], table_path_a[3], table_path_a[2],
			 table_path_a[1], table_path_a[0]);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "Ant_Table_B[7:0]={0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x}\n",
			 table_path_b[7], table_path_b[6], table_path_b[5],
			 table_path_b[4], table_path_b[3], table_path_b[2],
			 table_path_b[1], table_path_b[0]);

		phydm_cumitek_smt_ant_mapping_table_8822b(dm, &table_path_a[0], &table_path_b[0]);

	} else if (dm_value[0] == 4) {
		smtant_table->tx_desc_mode = (u8)dm_value[1];
		odm_set_mac_reg(dm, R_0x604, BIT(25), (u32)smtant_table->tx_desc_mode);
	}
	*_used = used;
	*_out_len = out_len;
}

#endif

#if (defined(CONFIG_HL_SMART_ANTENNA))
#ifdef CONFIG_HL_SMART_ANTENNA_TYPE2

#if (RTL8822B_SUPPORT == 1)
void phydm_hl_smart_ant_type2_init_8822b(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct smt_ant_honbo *sat_tab = &dm->dm_sat_table;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;
	u8 j;
	u8 rfu_codeword_table_init_2g[SUPPORT_BEAM_SET_PATTERN_NUM][MAX_PATH_NUM_8822B] = {
		{1, 1}, /*@0*/
		{1, 2},
		{2, 1},
		{2, 2},
		{4, 0},
		{5, 0},
		{6, 0},
		{7, 0},
		{8, 0}, /*@8*/
		{9, 0},
		{0xa, 0},
		{0xb, 0},
		{0xc, 0},
		{0xd, 0},
		{0xe, 0},
		{0xf, 0}};
	u8 rfu_codeword_table_init_5g[SUPPORT_BEAM_SET_PATTERN_NUM][MAX_PATH_NUM_8822B] = {
#if 1
		{9, 1}, /*@0*/
		{9, 9},
		{1, 9},
		{9, 6},
		{2, 1},
		{2, 9},
		{9, 2},
		{2, 2}, /*@8*/
		{6, 1},
		{6, 9},
		{2, 9},
		{2, 2},
		{6, 2},
		{6, 6},
		{2, 6},
		{1, 1}
#else
		{1, 1}, /*@0*/
		{9, 1},
		{9, 9},
		{1, 9},
		{1, 2},
		{9, 2},
		{9, 6},
		{1, 6},
		{2, 1}, /*@8*/
		{6, 1},
		{6, 9},
		{2, 9},
		{2, 2},
		{6, 2},
		{6, 6},
		{2, 6}
#endif
	};

	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "***RTK 8822B SmartAnt_Init: Hong-Bo SmrtAnt Type2]\n");

	/* @---------------------------------------- */
	/* @GPIO 0-1 for Beam control */
	/* reg0x66[2:0]=0 */
	/* reg0x44[25:24] = 0 */
	/* reg0x44[23:16]  enable_output for P_GPIO[7:0] */
	/* reg0x44[15:8]  output_value for P_GPIO[7:0] */
	/* reg0x40[1:0] = 0  GPIO function */
	/* @------------------------------------------ */

	odm_move_memory(dm, sat_tab->rfu_codeword_table_2g, rfu_codeword_table_init_2g, (SUPPORT_BEAM_SET_PATTERN_NUM * MAX_PATH_NUM_8822B));
	odm_move_memory(dm, sat_tab->rfu_codeword_table_5g, rfu_codeword_table_init_5g, (SUPPORT_BEAM_SET_PATTERN_NUM * MAX_PATH_NUM_8822B));

	/*@GPIO setting*/
	odm_set_mac_reg(dm, R_0x64, (BIT(18) | BIT(17) | BIT(16)), 0);
	odm_set_mac_reg(dm, R_0x44, BIT(25) | BIT(24), 0); /*@config P_GPIO[3:2] to data port*/
	odm_set_mac_reg(dm, R_0x44, BIT(17) | BIT(16), 0x3); /*@enable_output for P_GPIO[3:2]*/
#if 0
	/*odm_set_mac_reg(dm, R_0x44, BIT(9)|BIT(8), 0);*/ /*P_GPIO[3:2] output value*/
#endif
	odm_set_mac_reg(dm, R_0x40, BIT(1) | BIT(0), 0); /*@GPIO function*/

	/*@Hong_lin smart antenna HW setting*/
	sat_tab->rfu_protocol_type = 2;
	sat_tab->rfu_protocol_delay_time = 45;

	sat_tab->rfu_codeword_total_bit_num = 16; /*@max=32bit*/
	sat_tab->rfu_each_ant_bit_num = 4;

	sat_tab->total_beam_set_num = 4;
	sat_tab->total_beam_set_num_2g = 4;
	sat_tab->total_beam_set_num_5g = 8;

#if DEV_BUS_TYPE == RT_SDIO_INTERFACE
	if (dm->support_interface == ODM_ITRF_SDIO)
		sat_tab->latch_time = 100; /*@mu sec*/
#endif
#if DEV_BUS_TYPE == RT_USB_INTERFACE
	if (dm->support_interface == ODM_ITRF_USB)
		sat_tab->latch_time = 100; /*@mu sec*/
#endif
	sat_tab->pkt_skip_statistic_en = 0;

	sat_tab->ant_num = 2;
	sat_tab->ant_num_total = MAX_PATH_NUM_8822B;
	sat_tab->first_train_ant = MAIN_ANT;

	sat_tab->fix_beam_pattern_en = 0;
	sat_tab->decision_holding_period = 0;

	/*@beam training setting*/
	sat_tab->pkt_counter = 0;
	sat_tab->per_beam_training_pkt_num = 10;

	/*set default beam*/
	sat_tab->fast_training_beam_num = 0;
	sat_tab->pre_fast_training_beam_num = sat_tab->fast_training_beam_num;

	for (j = 0; j < SUPPORT_BEAM_SET_PATTERN_NUM; j++) {
		sat_tab->beam_set_avg_rssi_pre[j] = 0;
		sat_tab->beam_set_train_val_diff[j] = 0;
		sat_tab->beam_set_train_cnt[j] = 0;
	}
	phydm_set_rfu_beam_pattern_type2(dm);
	fat_tab->fat_state = FAT_BEFORE_LINK_STATE;
}
#endif

u32 phydm_construct_hb_rfu_codeword_type2(
	void *dm_void,
	u32 beam_set_idx)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct smt_ant_honbo *sat_tab = &dm->dm_sat_table;
	u32 sync_codeword = 0x7f;
	u32 codeword = 0;
	u32 data_tmp = 0;
	u32 i;

	for (i = 0; i < sat_tab->ant_num_total; i++) {
		if (*dm->band_type == ODM_BAND_5G)
			data_tmp = sat_tab->rfu_codeword_table_5g[beam_set_idx][i];
		else
			data_tmp = sat_tab->rfu_codeword_table_2g[beam_set_idx][i];

		codeword |= (data_tmp << (i * sat_tab->rfu_each_ant_bit_num));
	}

	codeword = (codeword << 8) | sync_codeword;

	return codeword;
}

void phydm_update_beam_pattern_type2(
	void *dm_void,
	u32 codeword,
	u32 codeword_length)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct smt_ant_honbo *sat_tab = &dm->dm_sat_table;
	u8 i;
	boolean beam_ctrl_signal;
	u32 one = 0x1;
	u32 reg44_tmp_p, reg44_tmp_n, reg44_ori;
	u8 devide_num = 4;

	PHYDM_DBG(dm, DBG_ANT_DIV, "Set codeword = ((0x%x))\n", codeword);

	reg44_ori = odm_get_mac_reg(dm, R_0x44, MASKDWORD);
	reg44_tmp_p = reg44_ori;
#if 0
	/*PHYDM_DBG(dm, DBG_ANT_DIV, "reg44_ori =0x%x\n", reg44_ori);*/
#endif

	/*@devide_num = (sat_tab->rfu_protocol_type == 2) ? 8 : 4;*/

	for (i = 0; i <= (codeword_length - 1); i++) {
		beam_ctrl_signal = (boolean)((codeword & BIT(i)) >> i);

		#if 1
		if (dm->debug_components & DBG_ANT_DIV) {
			if (i == (codeword_length - 1))
				pr_debug("%d ]\n", beam_ctrl_signal);
			else if (i == 0)
				pr_debug("Start sending codeword[1:%d] ---> [ %d ", codeword_length, beam_ctrl_signal);
			else if ((i % devide_num) == (devide_num - 1))
				pr_debug("%d  |  ", beam_ctrl_signal);
			else
				pr_debug("%d ", beam_ctrl_signal);
		}
		#endif

		if (dm->support_ic_type == ODM_RTL8821) {
			#if (RTL8821A_SUPPORT == 1)
			reg44_tmp_p = reg44_ori & (~(BIT(11) | BIT(10))); /*@clean bit 10 & 11*/
			reg44_tmp_p |= ((1 << 11) | (beam_ctrl_signal << 10));
			reg44_tmp_n = reg44_ori & (~(BIT(11) | BIT(10)));

#if 0
			/*PHYDM_DBG(dm, DBG_ANT_DIV, "reg44_tmp_p =(( 0x%x )), reg44_tmp_n = (( 0x%x ))\n", reg44_tmp_p, reg44_tmp_n);*/
#endif
			odm_set_mac_reg(dm, R_0x44, MASKDWORD, reg44_tmp_p);
			odm_set_mac_reg(dm, R_0x44, MASKDWORD, reg44_tmp_n);
			#endif
		}
		#if (RTL8822B_SUPPORT == 1)
		else if (dm->support_ic_type == ODM_RTL8822B) {
			if (sat_tab->rfu_protocol_type == 2) {
				reg44_tmp_p = reg44_tmp_p & ~(BIT(8)); /*@clean bit 8*/
				reg44_tmp_p = reg44_tmp_p ^ BIT(9); /*@get new clk high/low, exclusive-or*/

				reg44_tmp_p |= (beam_ctrl_signal << 8);

				odm_set_mac_reg(dm, R_0x44, MASKDWORD, reg44_tmp_p);
				ODM_delay_us(sat_tab->rfu_protocol_delay_time);
#if 0
				/*PHYDM_DBG(dm, DBG_ANT_DIV, "reg44 =(( 0x%x )), reg44[9:8] = ((%x)), beam_ctrl_signal =((%x))\n", reg44_tmp_p, ((reg44_tmp_p & 0x300)>>8), beam_ctrl_signal);*/
#endif

			} else {
				reg44_tmp_p = reg44_ori & (~(BIT(9) | BIT(8))); /*@clean bit 9 & 8*/
				reg44_tmp_p |= ((1 << 9) | (beam_ctrl_signal << 8));
				reg44_tmp_n = reg44_ori & (~(BIT(9) | BIT(8)));

#if 0
				/*PHYDM_DBG(dm, DBG_ANT_DIV, "reg44_tmp_p =(( 0x%x )), reg44_tmp_n = (( 0x%x ))\n", reg44_tmp_p, reg44_tmp_n); */
#endif
				odm_set_mac_reg(dm, R_0x44, MASKDWORD, reg44_tmp_p);
				ODM_delay_us(10);
				odm_set_mac_reg(dm, R_0x44, MASKDWORD, reg44_tmp_n);
				ODM_delay_us(10);
			}
		}
		#endif
	}
}

void phydm_update_rx_idle_beam_type2(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;
	struct smt_ant_honbo *sat_tab = &dm->dm_sat_table;
	u32 i;

	sat_tab->update_beam_codeword = phydm_construct_hb_rfu_codeword_type2(dm, sat_tab->rx_idle_beam_set_idx);
	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "[ Update Rx-Idle-Beam ] BeamSet idx = ((%d))\n",
		  sat_tab->rx_idle_beam_set_idx);

#if DEV_BUS_TYPE == RT_PCI_INTERFACE
	if (dm->support_interface == ODM_ITRF_PCIE)
		phydm_update_beam_pattern_type2(dm, sat_tab->update_beam_codeword, sat_tab->rfu_codeword_total_bit_num);
#endif
#if DEV_BUS_TYPE == RT_USB_INTERFACE || DEV_BUS_TYPE == RT_SDIO_INTERFACE
	if (dm->support_interface == ODM_ITRF_USB || dm->support_interface == ODM_ITRF_SDIO)
		odm_schedule_work_item(&sat_tab->hl_smart_antenna_workitem);
#if 0
	/*odm_stall_execution(1);*/
#endif
#endif

	sat_tab->pre_codeword = sat_tab->update_beam_codeword;
}

void phydm_hl_smt_ant_dbg_type2(
	void *dm_void,
	char input[][16],
	u32 *_used,
	char *output,
	u32 *_out_len
)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct smt_ant_honbo *sat_tab = &dm->dm_sat_table;
	u32 used = *_used;
	u32 out_len = *_out_len;
	u32 one = 0x1;
	u32 codeword_length = sat_tab->rfu_codeword_total_bit_num;
	u32 beam_ctrl_signal, i;
	u8 devide_num = 4;
	char help[] = "-h";
	u32 dm_value[10] = {0};

	PHYDM_SSCANF(input[1], DCMD_DECIMAL, &dm_value[0]);
	PHYDM_SSCANF(input[2], DCMD_DECIMAL, &dm_value[1]);
	PHYDM_SSCANF(input[3], DCMD_DECIMAL, &dm_value[2]);
	PHYDM_SSCANF(input[4], DCMD_DECIMAL, &dm_value[3]);
	PHYDM_SSCANF(input[5], DCMD_DECIMAL, &dm_value[4]);

	if (strcmp(input[1], help) == 0) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 " 1 {fix_en} {codeword(Hex)}\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 " 3 {Fix_training_num_en} {Per_beam_training_pkt_num} {Decision_holding_period}\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 " 5 {0:show, 1:2G, 2:5G} {beam_num} {idxA(Hex)} {idxB(Hex)}\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 " 7 {0:show, 1:2G, 2:5G} {total_beam_set_num}\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 " 8 {0:show, 1:set} {RFU delay time(us)}\n");

	} else if (dm_value[0] == 1) { /*@fix beam pattern*/

		sat_tab->fix_beam_pattern_en = dm_value[1];

		if (sat_tab->fix_beam_pattern_en == 1) {
			PHYDM_SSCANF(input[3], DCMD_HEX, &dm_value[2]);
			sat_tab->fix_beam_pattern_codeword = dm_value[2];

			if (sat_tab->fix_beam_pattern_codeword > (one << codeword_length)) {
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "[ SmartAnt ] Codeword overflow, Current codeword is ((0x%x)), and should be less than ((%d))bit\n",
					  sat_tab->fix_beam_pattern_codeword,
					  codeword_length);

				(sat_tab->fix_beam_pattern_codeword) &= 0xffffff;

				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "[ SmartAnt ] Auto modify to (0x%x)\n",
					  sat_tab->fix_beam_pattern_codeword);
			}

			sat_tab->update_beam_codeword = sat_tab->fix_beam_pattern_codeword;

			/*@---------------------------------------------------------*/
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "Fix Beam Pattern\n");

			/*@devide_num = (sat_tab->rfu_protocol_type == 2) ? 8 : 4;*/

			for (i = 0; i <= (codeword_length - 1); i++) {
				beam_ctrl_signal = (boolean)((sat_tab->update_beam_codeword & BIT(i)) >> i);

				if (i == (codeword_length - 1))
					PDM_SNPF(out_len, used,
						 output + used,
						 out_len - used,
						 "%d]\n",
						 beam_ctrl_signal);
				else if (i == 0)
					PDM_SNPF(out_len, used,
						 output + used,
						 out_len - used,
						 "Send Codeword[1:%d] to RFU -> [%d",
						 sat_tab->rfu_codeword_total_bit_num,
						 beam_ctrl_signal);
				else if ((i % devide_num) == (devide_num - 1))
					PDM_SNPF(out_len, used,
						 output + used,
						 out_len - used, "%d|",
						 beam_ctrl_signal);
				else
					PDM_SNPF(out_len, used,
						 output + used,
						 out_len - used, "%d",
						 beam_ctrl_signal);
			}
/*@---------------------------------------------------------*/

#if DEV_BUS_TYPE == RT_PCI_INTERFACE
			if (dm->support_interface == ODM_ITRF_PCIE)
				phydm_update_beam_pattern_type2(dm, sat_tab->update_beam_codeword, sat_tab->rfu_codeword_total_bit_num);
#endif
#if DEV_BUS_TYPE == RT_USB_INTERFACE || DEV_BUS_TYPE == RT_SDIO_INTERFACE
			if (dm->support_interface == ODM_ITRF_USB || dm->support_interface == ODM_ITRF_SDIO)
			odm_schedule_work_item(&sat_tab->hl_smart_antenna_workitem);
#if 0
			/*odm_stall_execution(1);*/
#endif
#endif
		} else if (sat_tab->fix_beam_pattern_en == 0)
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "[ SmartAnt ] Smart Antenna: Enable\n");

	} else if (dm_value[0] == 2) { /*set latch time*/

		sat_tab->latch_time = dm_value[1];
		PHYDM_DBG(dm, DBG_ANT_DIV, "[ SmartAnt ]  latch_time =0x%x\n",
			  sat_tab->latch_time);
	} else if (dm_value[0] == 3) {
		sat_tab->fix_training_num_en = dm_value[1];

		if (sat_tab->fix_training_num_en == 1) {
			sat_tab->per_beam_training_pkt_num = (u8)dm_value[2];
			sat_tab->decision_holding_period = (u8)dm_value[3];

			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "[SmtAnt] Fix_train_en = (( %d )), train_pkt_num = (( %d )), holding_period = (( %d )),\n",
				 sat_tab->fix_training_num_en,
				 sat_tab->per_beam_training_pkt_num,
				 sat_tab->decision_holding_period);

		} else if (sat_tab->fix_training_num_en == 0) {
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "[ SmartAnt ]  AUTO per_beam_training_pkt_num\n");
		}
	} else if (dm_value[0] == 4) {
		#if 0
		if (dm_value[1] == 1) {
			sat_tab->ant_num = 1;
			sat_tab->first_train_ant = MAIN_ANT;

		} else if (dm_value[1] == 2) {
			sat_tab->ant_num = 1;
			sat_tab->first_train_ant = AUX_ANT;

		} else if (dm_value[1] == 3) {
			sat_tab->ant_num = 2;
			sat_tab->first_train_ant = MAIN_ANT;
		}

		PDM_SNPF((output + used, out_len - used,
			 "[ SmartAnt ]  Set ant Num = (( %d )), first_train_ant = (( %d ))\n",
			 sat_tab->ant_num, (sat_tab->first_train_ant - 1)));
		#endif
	} else if (dm_value[0] == 5) { /*set beam set table*/

		PHYDM_SSCANF(input[4], DCMD_HEX, &dm_value[3]);
		PHYDM_SSCANF(input[5], DCMD_HEX, &dm_value[4]);

		if (dm_value[1] == 1) { /*@2G*/
			if (dm_value[2] < SUPPORT_BEAM_SET_PATTERN_NUM) {
				sat_tab->rfu_codeword_table_2g[dm_value[2]][0] = (u8)dm_value[3];
				sat_tab->rfu_codeword_table_2g[dm_value[2]][1] = (u8)dm_value[4];
				PDM_SNPF(out_len, used, output + used,
					 out_len - used,
					 "[SmtAnt] Set 2G Table[%d] = [A:0x%x, B:0x%x]\n",
					 dm_value[2], dm_value[3], dm_value[4]);
			}

		} else if (dm_value[1] == 2) { /*@5G*/
			if (dm_value[2] < SUPPORT_BEAM_SET_PATTERN_NUM) {
				sat_tab->rfu_codeword_table_5g[dm_value[2]][0] = (u8)dm_value[3];
				sat_tab->rfu_codeword_table_5g[dm_value[2]][1] = (u8)dm_value[4];
				PDM_SNPF(out_len, used, output + used,
					 out_len - used,
					 "[SmtAnt] Set5G Table[%d] = [A:0x%x, B:0x%x]\n",
					 dm_value[2], dm_value[3], dm_value[4]);
			}
		} else if (dm_value[1] == 0) {
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "[SmtAnt] 2G Beam Table==============>\n");
			for (i = 0; i < sat_tab->total_beam_set_num_2g; i++) {
				PDM_SNPF(out_len, used, output + used,
					 out_len - used,
					 "2G Table[%d] = [A:0x%x, B:0x%x]\n", i,
					 sat_tab->rfu_codeword_table_2g[i][0],
					 sat_tab->rfu_codeword_table_2g[i][1]);
			}
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "[SmtAnt] 5G Beam Table==============>\n");
			for (i = 0; i < sat_tab->total_beam_set_num_5g; i++) {
				PDM_SNPF(out_len, used, output + used,
					 out_len - used,
					 "5G Table[%d] = [A:0x%x, B:0x%x]\n", i,
					 sat_tab->rfu_codeword_table_5g[i][0],
					 sat_tab->rfu_codeword_table_5g[i][1]);
			}
		}

	} else if (dm_value[0] == 6) {
#if 0
		if (dm_value[1] == 0) {
			if (dm_value[2] < SUPPORT_BEAM_SET_PATTERN_NUM) {
				sat_tab->rfu_codeword_table_5g[dm_value[2] ][0] = (u8)dm_value[3];
				sat_tab->rfu_codeword_table_5g[dm_value[2] ][1] = (u8)dm_value[4];
				PDM_SNPF((output + used, out_len - used,
					 "[SmtAnt] Set5G Table[%d] = [A:0x%x, B:0x%x]\n",
					 dm_value[2], dm_value[3],
					 dm_value[4]));
			}
		} else {
			for (i = 0; i < sat_tab->total_beam_set_num_5g; i++) {
				PDM_SNPF((output + used, out_len - used,
					 "[SmtAnt] Read 5G Table[%d] = [A:0x%x, B:0x%x]\n",
					 i,
					 sat_tab->rfu_codeword_table_5g[i][0],
					 sat_tab->rfu_codeword_table_5g[i][1]));
			}
		}
#endif
	} else if (dm_value[0] == 7) {
		if (dm_value[1] == 1) {
			sat_tab->total_beam_set_num_2g = (u8)(dm_value[2]);
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "[ SmartAnt ] total_beam_set_num_2g = ((%d))\n",
				 sat_tab->total_beam_set_num_2g);

		} else if (dm_value[1] == 2) {
			sat_tab->total_beam_set_num_5g = (u8)(dm_value[2]);
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "[ SmartAnt ] total_beam_set_num_5g = ((%d))\n",
				 sat_tab->total_beam_set_num_5g);
		} else if (dm_value[1] == 0) {
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "[ SmartAnt ] Show total_beam_set_num{2g,5g} = {%d,%d}\n",
				 sat_tab->total_beam_set_num_2g,
				 sat_tab->total_beam_set_num_5g);
		}

	} else if (dm_value[0] == 8) {
		if (dm_value[1] == 1) {
			sat_tab->rfu_protocol_delay_time = (u16)(dm_value[2]);
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "[SmtAnt] Set rfu_protocol_delay_time = ((%d))\n",
				 sat_tab->rfu_protocol_delay_time);
		} else if (dm_value[1] == 0) {
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "[SmtAnt] Read rfu_protocol_delay_time = ((%d))\n",
				 sat_tab->rfu_protocol_delay_time);
		}
	}

	*_used = used;
	*_out_len = out_len;
}

void phydm_set_rfu_beam_pattern_type2(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct smt_ant_honbo *sat_tab = &dm->dm_sat_table;

	if (dm->ant_div_type != HL_SW_SMART_ANT_TYPE2)
		return;

	PHYDM_DBG(dm, DBG_ANT_DIV, "Training beam_set index = (( 0x%x ))\n",
		  sat_tab->fast_training_beam_num);
	sat_tab->update_beam_codeword = phydm_construct_hb_rfu_codeword_type2(dm, sat_tab->fast_training_beam_num);

	#if DEV_BUS_TYPE == RT_PCI_INTERFACE
	if (dm->support_interface == ODM_ITRF_PCIE)
		phydm_update_beam_pattern_type2(dm, sat_tab->update_beam_codeword, sat_tab->rfu_codeword_total_bit_num);
	#endif
	#if DEV_BUS_TYPE == RT_USB_INTERFACE || DEV_BUS_TYPE == RT_SDIO_INTERFACE
	if (dm->support_interface == ODM_ITRF_USB || dm->support_interface == ODM_ITRF_SDIO)
		odm_schedule_work_item(&sat_tab->hl_smart_antenna_workitem);
#if 0
	/*odm_stall_execution(1);*/
#endif
	#endif
}

void phydm_fast_ant_training_hl_smart_antenna_type2(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct smt_ant_honbo *sat_tab = &dm->dm_sat_table;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;
	struct sw_antenna_switch *dm_swat_table = &dm->dm_swat_table;
	u32 codeword = 0;
	u8 i = 0, j = 0;
	u8 avg_rssi_tmp;
	u8 avg_rssi_tmp_ma;
	u8 max_beam_ant_rssi = 0;
	u8 rssi_target_beam = 0, target_beam_max_rssi = 0;
	u8 evm1ss_target_beam = 0, evm2ss_target_beam = 0;
	u32 target_beam_max_evm1ss = 0, target_beam_max_evm2ss = 0;
	u32 beam_tmp;
	u8 per_beam_val_diff_tmp = 0, training_pkt_num_offset;
	u32 avg_evm2ss[2] = {0}, avg_evm2ss_sum = 0;
	u32 avg_evm1ss = 0;
	u32 beam_path_evm_2ss_cnt_all = 0; /*sum of all 2SS-pattern cnt*/
	u32 beam_path_evm_1ss_cnt_all = 0; /*sum of all 1SS-pattern cnt*/
	u8 decision_type;

	if (!dm->is_linked) {
		PHYDM_DBG(dm, DBG_ANT_DIV, "[No Link!!!]\n");

		if (fat_tab->is_become_linked == true) {
			sat_tab->decision_holding_period = 0;
			PHYDM_DBG(dm, DBG_ANT_DIV, "Link->no Link\n");
			fat_tab->fat_state = FAT_BEFORE_LINK_STATE;
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "change to (( %d )) FAT_state\n",
				  fat_tab->fat_state);
			fat_tab->is_become_linked = dm->is_linked;
		}
		return;

	} else {
		if (fat_tab->is_become_linked == false) {
			PHYDM_DBG(dm, DBG_ANT_DIV, "[Linked !!!]\n");

			fat_tab->fat_state = FAT_PREPARE_STATE;
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "change to (( %d )) FAT_state\n",
				  fat_tab->fat_state);

			/*sat_tab->fast_training_beam_num = 0;*/
			/*phydm_set_rfu_beam_pattern_type2(dm);*/

			fat_tab->is_become_linked = dm->is_linked;
		}
	}

#if 0
	/*PHYDM_DBG(dm, DBG_ANT_DIV, "HL Smart ant Training: state (( %d ))\n", fat_tab->fat_state);*/
#endif

	/* @[DECISION STATE] */
	/*@=======================================================================================*/
	if (fat_tab->fat_state == FAT_DECISION_STATE) {
		PHYDM_DBG(dm, DBG_ANT_DIV, "[ 3. In Decision state]\n");

		/*@compute target beam in each antenna*/

		for (j = 0; j < (sat_tab->total_beam_set_num); j++) {
			/*@[Decision1: RSSI]-------------------------------------------------------------------*/
			if (sat_tab->statistic_pkt_cnt[j] == 0) { /*@if new RSSI = 0 -> MA_RSSI-=2*/
				avg_rssi_tmp = sat_tab->beam_set_avg_rssi_pre[j];
				avg_rssi_tmp = (avg_rssi_tmp >= 2) ? (avg_rssi_tmp - 2) : avg_rssi_tmp;
				avg_rssi_tmp_ma = avg_rssi_tmp;
			} else {
				avg_rssi_tmp = (u8)((sat_tab->beam_set_rssi_avg_sum[j]) / (sat_tab->statistic_pkt_cnt[j]));
				avg_rssi_tmp_ma = (avg_rssi_tmp + sat_tab->beam_set_avg_rssi_pre[j]) >> 1;
			}

			sat_tab->beam_set_avg_rssi_pre[j] = avg_rssi_tmp;

			if (avg_rssi_tmp > target_beam_max_rssi) {
				rssi_target_beam = j;
				target_beam_max_rssi = avg_rssi_tmp;
			}

			/*@[Decision2: EVM 2ss]-------------------------------------------------------------------*/
			if (sat_tab->beam_path_evm_2ss_cnt[j] != 0) {
				avg_evm2ss[0] = sat_tab->beam_path_evm_2ss_sum[j][0] / sat_tab->beam_path_evm_2ss_cnt[j];
				avg_evm2ss[1] = sat_tab->beam_path_evm_2ss_sum[j][1] / sat_tab->beam_path_evm_2ss_cnt[j];
				avg_evm2ss_sum = avg_evm2ss[0] + avg_evm2ss[1];
				beam_path_evm_2ss_cnt_all += sat_tab->beam_path_evm_2ss_cnt[j];

				sat_tab->beam_set_avg_evm_2ss_pre[j] = (u8)avg_evm2ss_sum;
			}

			if (avg_evm2ss_sum > target_beam_max_evm2ss) {
				evm2ss_target_beam = j;
				target_beam_max_evm2ss = avg_evm2ss_sum;
			}

			/*@[Decision3: EVM 1ss]-------------------------------------------------------------------*/
			if (sat_tab->beam_path_evm_1ss_cnt[j] != 0) {
				avg_evm1ss = sat_tab->beam_path_evm_1ss_sum[j] / sat_tab->beam_path_evm_1ss_cnt[j];
				beam_path_evm_1ss_cnt_all += sat_tab->beam_path_evm_1ss_cnt[j];

				sat_tab->beam_set_avg_evm_1ss_pre[j] = (u8)avg_evm1ss;
			}

			if (avg_evm1ss > target_beam_max_evm1ss) {
				evm1ss_target_beam = j;
				target_beam_max_evm1ss = avg_evm1ss;
			}

			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "Beam[%d] Pkt_cnt=(( %d )), avg{MA,rssi}={%d, %d}, EVM1={%d}, EVM2={%d, %d, %d}\n",
				  j, sat_tab->statistic_pkt_cnt[j],
				  avg_rssi_tmp_ma, avg_rssi_tmp, avg_evm1ss,
				  avg_evm2ss[0], avg_evm2ss[1], avg_evm2ss_sum);

			/*reset counter value*/
			sat_tab->beam_set_rssi_avg_sum[j] = 0;
			sat_tab->beam_path_rssi_sum[j][0] = 0;
			sat_tab->beam_path_rssi_sum[j][1] = 0;
			sat_tab->statistic_pkt_cnt[j] = 0;

			sat_tab->beam_path_evm_2ss_sum[j][0] = 0;
			sat_tab->beam_path_evm_2ss_sum[j][1] = 0;
			sat_tab->beam_path_evm_2ss_cnt[j] = 0;

			sat_tab->beam_path_evm_1ss_sum[j] = 0;
			sat_tab->beam_path_evm_1ss_cnt[j] = 0;
		}

		/*@[Joint Decision]-------------------------------------------------------------------*/
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "--->1.[RSSI]      Target Beam(( %d )) RSSI_max=((%d))\n",
			  rssi_target_beam, target_beam_max_rssi);
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "--->2.[Evm2SS] Target Beam(( %d )) EVM2SS_max=((%d))\n",
			  evm2ss_target_beam, target_beam_max_evm2ss);
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "--->3.[Evm1SS] Target Beam(( %d )) EVM1SS_max=((%d))\n",
			  evm1ss_target_beam, target_beam_max_evm1ss);

		if (target_beam_max_rssi <= 10) {
			sat_tab->rx_idle_beam_set_idx = rssi_target_beam;
			decision_type = 1;
		} else {
			if (beam_path_evm_2ss_cnt_all != 0) {
				sat_tab->rx_idle_beam_set_idx = evm2ss_target_beam;
				decision_type = 2;
			} else if (beam_path_evm_1ss_cnt_all != 0) {
				sat_tab->rx_idle_beam_set_idx = evm1ss_target_beam;
				decision_type = 3;
			} else {
				sat_tab->rx_idle_beam_set_idx = rssi_target_beam;
				decision_type = 1;
			}
		}

		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "---> Decision_type=((%d)), Final Target Beam(( %d ))\n",
			  decision_type, sat_tab->rx_idle_beam_set_idx);

		/*@Calculate packet counter offset*/
		for (j = 0; j < (sat_tab->total_beam_set_num); j++) {
			if (decision_type == 1) {
				per_beam_val_diff_tmp = target_beam_max_rssi - sat_tab->beam_set_avg_rssi_pre[j];

			} else if (decision_type == 2) {
				per_beam_val_diff_tmp = ((u8)target_beam_max_evm2ss - sat_tab->beam_set_avg_evm_2ss_pre[j]) >> 1;
			} else if (decision_type == 3) {
				per_beam_val_diff_tmp = (u8)target_beam_max_evm1ss - sat_tab->beam_set_avg_evm_1ss_pre[j];
			}
			sat_tab->beam_set_train_val_diff[j] = per_beam_val_diff_tmp;
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "Beam_Set[%d]: diff= ((%d))\n", j,
				  per_beam_val_diff_tmp);
		}

		/*set beam in each antenna*/
		phydm_update_rx_idle_beam_type2(dm);
		fat_tab->fat_state = FAT_PREPARE_STATE;
	}
	/* @[TRAINING STATE] */
	else if (fat_tab->fat_state == FAT_TRAINING_STATE) {
		PHYDM_DBG(dm, DBG_ANT_DIV, "[ 2. In Training state]\n");

		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "curr_beam_idx = (( %d )), pre_beam_idx = (( %d ))\n",
			  sat_tab->fast_training_beam_num,
			  sat_tab->pre_fast_training_beam_num);

		if (sat_tab->fast_training_beam_num > sat_tab->pre_fast_training_beam_num)

			sat_tab->force_update_beam_en = 0;

		else {
			sat_tab->force_update_beam_en = 1;

			sat_tab->pkt_counter = 0;
			beam_tmp = sat_tab->fast_training_beam_num;
			if (sat_tab->fast_training_beam_num >= ((u32)sat_tab->total_beam_set_num - 1)) {
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "[Timeout Update]  Beam_num (( %d )) -> (( decision ))\n",
					  sat_tab->fast_training_beam_num);
				fat_tab->fat_state = FAT_DECISION_STATE;
				phydm_fast_ant_training_hl_smart_antenna_type2(dm);

			} else {
				sat_tab->fast_training_beam_num++;

				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "[Timeout Update]  Beam_num (( %d )) -> (( %d ))\n",
					  beam_tmp,
					  sat_tab->fast_training_beam_num);
				phydm_set_rfu_beam_pattern_type2(dm);
				fat_tab->fat_state = FAT_TRAINING_STATE;
			}
		}
		sat_tab->pre_fast_training_beam_num = sat_tab->fast_training_beam_num;
		PHYDM_DBG(dm, DBG_ANT_DIV, "Update Pre_Beam =(( %d ))\n",
			  sat_tab->pre_fast_training_beam_num);
	}
	/*  @[Prepare state] */
	/*@=======================================================================================*/
	else if (fat_tab->fat_state == FAT_PREPARE_STATE) {
		PHYDM_DBG(dm, DBG_ANT_DIV, "\n\n[ 1. In Prepare state]\n");

		if (dm->pre_traffic_load == dm->traffic_load) {
			if (sat_tab->decision_holding_period != 0) {
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "Holding_period = (( %d )), return!!!\n",
					  sat_tab->decision_holding_period);
				sat_tab->decision_holding_period--;
				return;
			}
		}

		/* Set training packet number*/
		if (sat_tab->fix_training_num_en == 0) {
			switch (dm->traffic_load) {
			case TRAFFIC_HIGH:
				sat_tab->per_beam_training_pkt_num = 8;
				sat_tab->decision_holding_period = 2;
				break;
			case TRAFFIC_MID:
				sat_tab->per_beam_training_pkt_num = 6;
				sat_tab->decision_holding_period = 3;
				break;
			case TRAFFIC_LOW:
				sat_tab->per_beam_training_pkt_num = 3; /*ping 60000*/
				sat_tab->decision_holding_period = 4;
				break;
			case TRAFFIC_ULTRA_LOW:
				sat_tab->per_beam_training_pkt_num = 1;
				sat_tab->decision_holding_period = 6;
				break;
			default:
				break;
			}
		}

		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "TrafficLoad = (( %d )), Fix_beam = (( %d )), per_beam_training_pkt_num = (( %d )), decision_holding_period = ((%d))\n",
			  dm->traffic_load, sat_tab->fix_training_num_en,
			  sat_tab->per_beam_training_pkt_num,
			  sat_tab->decision_holding_period);

		/*@Beam_set number*/
		if (*dm->band_type == ODM_BAND_5G) {
			sat_tab->total_beam_set_num = sat_tab->total_beam_set_num_5g;
			PHYDM_DBG(dm, DBG_ANT_DIV, "5G beam_set num = ((%d))\n",
				  sat_tab->total_beam_set_num);
		} else {
			sat_tab->total_beam_set_num = sat_tab->total_beam_set_num_2g;
			PHYDM_DBG(dm, DBG_ANT_DIV, "2G beam_set num = ((%d))\n",
				  sat_tab->total_beam_set_num);
		}

		for (j = 0; j < (sat_tab->total_beam_set_num); j++) {
			training_pkt_num_offset = sat_tab->beam_set_train_val_diff[j];

			if (sat_tab->per_beam_training_pkt_num > training_pkt_num_offset)
				sat_tab->beam_set_train_cnt[j] = sat_tab->per_beam_training_pkt_num - training_pkt_num_offset;
			else
				sat_tab->beam_set_train_cnt[j] = 1;

			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "Beam_Set[ %d ] training_pkt_offset = ((%d)), training_pkt_num = ((%d))\n",
				  j, sat_tab->beam_set_train_val_diff[j],
				  sat_tab->beam_set_train_cnt[j]);
		}

		sat_tab->pre_beacon_counter = sat_tab->beacon_counter;
		sat_tab->update_beam_idx = 0;
		sat_tab->pkt_counter = 0;

		sat_tab->fast_training_beam_num = 0;
		phydm_set_rfu_beam_pattern_type2(dm);
		sat_tab->pre_fast_training_beam_num = sat_tab->fast_training_beam_num;
		fat_tab->fat_state = FAT_TRAINING_STATE;
	}
}

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

void phydm_beam_switch_workitem_callback(
	void *context)
{
	void *adapter = (void *)context;
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(((PADAPTER)adapter));
	struct dm_struct *dm = &hal_data->DM_OutSrc;
	struct smt_ant_honbo *sat_tab = &dm->dm_sat_table;

#if DEV_BUS_TYPE != RT_PCI_INTERFACE
	sat_tab->pkt_skip_statistic_en = 1;
#endif
	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "[ SmartAnt ] Beam Switch Workitem Callback, pkt_skip_statistic_en = (( %d ))\n",
		  sat_tab->pkt_skip_statistic_en);

	phydm_update_beam_pattern_type2(dm, sat_tab->update_beam_codeword, sat_tab->rfu_codeword_total_bit_num);

#if DEV_BUS_TYPE != RT_PCI_INTERFACE
#if 0
	/*odm_stall_execution(sat_tab->latch_time);*/
#endif
	sat_tab->pkt_skip_statistic_en = 0;
#endif
	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "pkt_skip_statistic_en = (( %d )), latch_time = (( %d ))\n",
		  sat_tab->pkt_skip_statistic_en, sat_tab->latch_time);
}

void phydm_beam_decision_workitem_callback(
	void *context)
{
	void *adapter = (void *)context;
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(((PADAPTER)adapter));
	struct dm_struct *dm = &hal_data->DM_OutSrc;

	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "[ SmartAnt ] Beam decision Workitem Callback\n");
	phydm_fast_ant_training_hl_smart_antenna_type2(dm);
}
#endif

void phydm_process_rssi_for_hb_smtant_type2(
	void *dm_void,
	void *phy_info_void,
	void *pkt_info_void,
	u8 rssi_avg)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_phyinfo_struct *phy_info = (struct phydm_phyinfo_struct *)phy_info_void;
	struct phydm_perpkt_info_struct *pktinfo = (struct phydm_perpkt_info_struct *)pkt_info_void;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;
	struct smt_ant_honbo *sat_tab = &dm->dm_sat_table;
	u8 train_pkt_number;
	u32 beam_tmp;
	u8 rx_power_ant0 = phy_info->rx_mimo_signal_strength[0];
	u8 rx_power_ant1 = phy_info->rx_mimo_signal_strength[1];
	u8 rx_evm_ant0 = phy_info->rx_mimo_evm_dbm[0];
	u8 rx_evm_ant1 = phy_info->rx_mimo_evm_dbm[1];

	/*@[Beacon]*/
	if (pktinfo->is_packet_beacon) {
		sat_tab->beacon_counter++;
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "MatchBSSID_beacon_counter = ((%d))\n",
			  sat_tab->beacon_counter);

		if (sat_tab->beacon_counter >= sat_tab->pre_beacon_counter + 2) {
			sat_tab->update_beam_idx++;
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "pre_beacon_counter = ((%d)), pkt_counter = ((%d)), update_beam_idx = ((%d))\n",
				  sat_tab->pre_beacon_counter,
				  sat_tab->pkt_counter,
				  sat_tab->update_beam_idx);

			sat_tab->pre_beacon_counter = sat_tab->beacon_counter;
			sat_tab->pkt_counter = 0;
		}
	}
	/*@[data]*/
	else if (pktinfo->is_packet_to_self) {
		if (sat_tab->pkt_skip_statistic_en == 0) {
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "ID[%d] pkt_cnt=((%d)): Beam_set = ((%d)), RSSI{A,B,avg} = {%d, %d, %d}\n",
				  pktinfo->station_id, sat_tab->pkt_counter,
				  sat_tab->fast_training_beam_num,
				  rx_power_ant0, rx_power_ant1, rssi_avg);

			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "Rate_ss = ((%d)), EVM{A,B} = {%d, %d}, RX Rate =",
				  pktinfo->rate_ss, rx_evm_ant0, rx_evm_ant1);
			phydm_print_rate(dm, dm->rx_rate, DBG_ANT_DIV);

			if (sat_tab->pkt_counter >= 1) /*packet skip count*/
			{
				sat_tab->beam_set_rssi_avg_sum[sat_tab->fast_training_beam_num] += rssi_avg;
				sat_tab->statistic_pkt_cnt[sat_tab->fast_training_beam_num]++;

				sat_tab->beam_path_rssi_sum[sat_tab->fast_training_beam_num][0] += rx_power_ant0;
				sat_tab->beam_path_rssi_sum[sat_tab->fast_training_beam_num][1] += rx_power_ant1;

				if (pktinfo->rate_ss == 2) {
					sat_tab->beam_path_evm_2ss_sum[sat_tab->fast_training_beam_num][0] += rx_evm_ant0;
					sat_tab->beam_path_evm_2ss_sum[sat_tab->fast_training_beam_num][1] += rx_evm_ant1;
					sat_tab->beam_path_evm_2ss_cnt[sat_tab->fast_training_beam_num]++;
				} else {
					sat_tab->beam_path_evm_1ss_sum[sat_tab->fast_training_beam_num] += rx_evm_ant0;
					sat_tab->beam_path_evm_1ss_cnt[sat_tab->fast_training_beam_num]++;
				}
			}

			sat_tab->pkt_counter++;

			train_pkt_number = sat_tab->beam_set_train_cnt[sat_tab->fast_training_beam_num];

			if (sat_tab->pkt_counter >= train_pkt_number) {
				sat_tab->update_beam_idx++;
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "pre_beacon_counter = ((%d)), Update_new_beam = ((%d))\n",
					  sat_tab->pre_beacon_counter,
					  sat_tab->update_beam_idx);

				sat_tab->pre_beacon_counter = sat_tab->beacon_counter;
				sat_tab->pkt_counter = 0;
			}
		}
	}

	if (sat_tab->update_beam_idx > 0) {
		sat_tab->update_beam_idx = 0;

		if (sat_tab->fast_training_beam_num >= ((u32)sat_tab->total_beam_set_num - 1)) {
			fat_tab->fat_state = FAT_DECISION_STATE;

			#if DEV_BUS_TYPE == RT_PCI_INTERFACE
			if (dm->support_interface == ODM_ITRF_PCIE)
				phydm_fast_ant_training_hl_smart_antenna_type2(dm); /*@go to make decision*/
			#endif
			#if DEV_BUS_TYPE == RT_USB_INTERFACE || DEV_BUS_TYPE == RT_SDIO_INTERFACE
			if (dm->support_interface == ODM_ITRF_USB || dm->support_interface == ODM_ITRF_SDIO)
				odm_schedule_work_item(&sat_tab->hl_smart_antenna_decision_workitem);
			#endif

		} else {
			beam_tmp = sat_tab->fast_training_beam_num;
			sat_tab->fast_training_beam_num++;
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "Update Beam_num (( %d )) -> (( %d ))\n",
				  beam_tmp, sat_tab->fast_training_beam_num);
			phydm_set_rfu_beam_pattern_type2(dm);
			sat_tab->pre_fast_training_beam_num = sat_tab->fast_training_beam_num;

			fat_tab->fat_state = FAT_TRAINING_STATE;
		}
	}
}
#endif

#if (defined(CONFIG_HL_SMART_ANTENNA_TYPE1))

void phydm_hl_smart_ant_type1_init_8821a(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct smt_ant_honbo *sat_tab = &dm->dm_sat_table;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;
	u32 value32;

	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "***8821A SmartAnt_Init => ant_div_type=[Hong-Lin Smart ant Type1]\n");

#if 0
	/* @---------------------------------------- */
	/* @GPIO 2-3 for Beam control */
	/* reg0x66[2]=0 */
	/* reg0x44[27:26] = 0 */
	/* reg0x44[23:16]  enable_output for P_GPIO[7:0] */
	/* reg0x44[15:8]  output_value for P_GPIO[7:0] */
	/* reg0x40[1:0] = 0  GPIO function */
	/* @------------------------------------------ */
#endif

	/*@GPIO setting*/
	odm_set_mac_reg(dm, R_0x64, BIT(18), 0);
	odm_set_mac_reg(dm, R_0x44, BIT(27) | BIT(26), 0);
	odm_set_mac_reg(dm, R_0x44, BIT(19) | BIT(18), 0x3); /*@enable_output for P_GPIO[3:2]*/
#if 0
	/*odm_set_mac_reg(dm, R_0x44, BIT(11)|BIT(10), 0);*/ /*output value*/
#endif
	odm_set_mac_reg(dm, R_0x40, BIT(1) | BIT(0), 0); /*@GPIO function*/

	/*@Hong_lin smart antenna HW setting*/
	sat_tab->rfu_codeword_total_bit_num = 24; /*@max=32*/
	sat_tab->rfu_each_ant_bit_num = 4;
	sat_tab->beam_patten_num_each_ant = 4;

#if DEV_BUS_TYPE == RT_SDIO_INTERFACE
	sat_tab->latch_time = 100; /*@mu sec*/
#elif DEV_BUS_TYPE == RT_USB_INTERFACE
	sat_tab->latch_time = 100; /*@mu sec*/
#endif
	sat_tab->pkt_skip_statistic_en = 0;

	sat_tab->ant_num = 1; /*@max=8*/
	sat_tab->ant_num_total = NUM_ANTENNA_8821A;
	sat_tab->first_train_ant = MAIN_ANT;

	sat_tab->rfu_codeword_table[0] = 0x0;
	sat_tab->rfu_codeword_table[1] = 0x4;
	sat_tab->rfu_codeword_table[2] = 0x8;
	sat_tab->rfu_codeword_table[3] = 0xc;

	sat_tab->rfu_codeword_table_5g[0] = 0x1;
	sat_tab->rfu_codeword_table_5g[1] = 0x2;
	sat_tab->rfu_codeword_table_5g[2] = 0x4;
	sat_tab->rfu_codeword_table_5g[3] = 0x8;

	sat_tab->fix_beam_pattern_en = 0;
	sat_tab->decision_holding_period = 0;

	/*@beam training setting*/
	sat_tab->pkt_counter = 0;
	sat_tab->per_beam_training_pkt_num = 10;

	/*set default beam*/
	sat_tab->fast_training_beam_num = 0;
	sat_tab->pre_fast_training_beam_num = sat_tab->fast_training_beam_num;
	phydm_set_all_ant_same_beam_num(dm);

	fat_tab->fat_state = FAT_BEFORE_LINK_STATE;

	odm_set_bb_reg(dm, R_0xca4, MASKDWORD, 0x01000100);
	odm_set_bb_reg(dm, R_0xca8, MASKDWORD, 0x01000100);

	/*@[BB] FAT setting*/
	odm_set_bb_reg(dm, R_0xc08, BIT(18) | BIT(17) | BIT(16), sat_tab->ant_num);
	odm_set_bb_reg(dm, R_0xc08, BIT(31), 0); /*@increase ant num every FAT period 0:+1, 1+2*/
	odm_set_bb_reg(dm, R_0x8c4, BIT(2) | BIT(1), 1); /*@change cca antenna timming threshold if no CCA occurred: 0:200ms / 1:100ms / 2:no use / 3: 300*/
	odm_set_bb_reg(dm, R_0x8c4, BIT(0), 1); /*@FAT_watchdog_en*/

	value32 = odm_get_mac_reg(dm, R_0x7b4, MASKDWORD);
	odm_set_mac_reg(dm, R_0x7b4, MASKDWORD, value32 | (BIT(16) | BIT(17))); /*Reg7B4[16]=1 enable antenna training */
	/*Reg7B4[17]=1 enable  match MAC addr*/
	odm_set_mac_reg(dm, R_0x7b4, 0xFFFF, 0); /*@Match MAC ADDR*/
	odm_set_mac_reg(dm, R_0x7b0, MASKDWORD, 0);
}

u32 phydm_construct_hl_beam_codeword(
	void *dm_void,
	u32 *beam_pattern_idx,
	u32 ant_num)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct smt_ant_honbo *sat_tab = &dm->dm_sat_table;
	u32 codeword = 0;
	u32 data_tmp;
	u32 i;
	u32 break_counter = 0;

	if (ant_num < 8) {
		for (i = 0; i < (sat_tab->ant_num_total); i++) {
#if 0
			/*PHYDM_DBG(dm,DBG_ANT_DIV, "beam_pattern_num[%x] = %x\n",i,beam_pattern_num[i] );*/
#endif
			if ((i < (sat_tab->first_train_ant - 1)) || break_counter >= sat_tab->ant_num) {
				data_tmp = 0;
			} else {
				break_counter++;

				if (beam_pattern_idx[i] == 0) {
					if (*dm->band_type == ODM_BAND_5G)
						data_tmp = sat_tab->rfu_codeword_table_5g[0];
					else
						data_tmp = sat_tab->rfu_codeword_table[0];

				} else if (beam_pattern_idx[i] == 1) {
					if (*dm->band_type == ODM_BAND_5G)
						data_tmp = sat_tab->rfu_codeword_table_5g[1];
					else
						data_tmp = sat_tab->rfu_codeword_table[1];

				} else if (beam_pattern_idx[i] == 2) {
					if (*dm->band_type == ODM_BAND_5G)
						data_tmp = sat_tab->rfu_codeword_table_5g[2];
					else
						data_tmp = sat_tab->rfu_codeword_table[2];

				} else if (beam_pattern_idx[i] == 3) {
					if (*dm->band_type == ODM_BAND_5G)
						data_tmp = sat_tab->rfu_codeword_table_5g[3];
					else
						data_tmp = sat_tab->rfu_codeword_table[3];
				}
			}

			codeword |= (data_tmp << (i * 4));
		}
	}

	return codeword;
}

void phydm_update_beam_pattern(
	void *dm_void,
	u32 codeword,
	u32 codeword_length)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct smt_ant_honbo *sat_tab = &dm->dm_sat_table;
	u8 i;
	boolean beam_ctrl_signal;
	u32 one = 0x1;
	u32 reg44_tmp_p, reg44_tmp_n, reg44_ori;
	u8 devide_num = 4;

	PHYDM_DBG(dm, DBG_ANT_DIV, "[ SmartAnt ] Set Beam Pattern =0x%x\n",
		  codeword);

	reg44_ori = odm_get_mac_reg(dm, R_0x44, MASKDWORD);
	reg44_tmp_p = reg44_ori;
#if 0
	/*PHYDM_DBG(dm, DBG_ANT_DIV, "reg44_ori =0x%x\n", reg44_ori);*/
#endif

	devide_num = (sat_tab->rfu_protocol_type == 2) ? 6 : 4;

	for (i = 0; i <= (codeword_length - 1); i++) {
		beam_ctrl_signal = (boolean)((codeword & BIT(i)) >> i);

		if (dm->debug_components & DBG_ANT_DIV) {
			if (i == (codeword_length - 1))
				pr_debug("%d ]\n", beam_ctrl_signal);
			else if (i == 0)
				pr_debug("Send codeword[1:%d] ---> [ %d ", codeword_length, beam_ctrl_signal);
			else if ((i % devide_num) == (devide_num - 1))
				pr_debug("%d  |  ", beam_ctrl_signal);
			else
				pr_debug("%d ", beam_ctrl_signal);
		}

		if (dm->support_ic_type == ODM_RTL8821) {
			#if (RTL8821A_SUPPORT == 1)
			reg44_tmp_p = reg44_ori & (~(BIT(11) | BIT(10))); /*@clean bit 10 & 11*/
			reg44_tmp_p |= ((1 << 11) | (beam_ctrl_signal << 10));
			reg44_tmp_n = reg44_ori & (~(BIT(11) | BIT(10)));

#if 0
			/*PHYDM_DBG(dm, DBG_ANT_DIV, "reg44_tmp_p =(( 0x%x )), reg44_tmp_n = (( 0x%x ))\n", reg44_tmp_p, reg44_tmp_n);*/
#endif
			odm_set_mac_reg(dm, R_0x44, MASKDWORD, reg44_tmp_p);
			odm_set_mac_reg(dm, R_0x44, MASKDWORD, reg44_tmp_n);
			#endif
		}
		#if (RTL8822B_SUPPORT == 1)
		else if (dm->support_ic_type == ODM_RTL8822B) {
			if (sat_tab->rfu_protocol_type == 2) {
				reg44_tmp_p = reg44_tmp_p & ~(BIT(8)); /*@clean bit 8*/
				reg44_tmp_p = reg44_tmp_p ^ BIT(9); /*@get new clk high/low, exclusive-or*/

				reg44_tmp_p |= (beam_ctrl_signal << 8);

				odm_set_mac_reg(dm, R_0x44, MASKDWORD, reg44_tmp_p);
				ODM_delay_us(10);
#if 0
				/*PHYDM_DBG(dm, DBG_ANT_DIV, "reg44 =(( 0x%x )), reg44[9:8] = ((%x)), beam_ctrl_signal =((%x))\n", reg44_tmp_p, ((reg44_tmp_p & 0x300)>>8), beam_ctrl_signal);*/
#endif

			} else {
				reg44_tmp_p = reg44_ori & (~(BIT(9) | BIT(8))); /*@clean bit 9 & 8*/
				reg44_tmp_p |= ((1 << 9) | (beam_ctrl_signal << 8));
				reg44_tmp_n = reg44_ori & (~(BIT(9) | BIT(8)));

#if 0
				/*PHYDM_DBG(dm, DBG_ANT_DIV, "reg44_tmp_p =(( 0x%x )), reg44_tmp_n = (( 0x%x ))\n", reg44_tmp_p, reg44_tmp_n); */
#endif
				odm_set_mac_reg(dm, R_0x44, MASKDWORD, reg44_tmp_p);
				ODM_delay_us(10);
				odm_set_mac_reg(dm, R_0x44, MASKDWORD, reg44_tmp_n);
				ODM_delay_us(10);
			}
		}
		#endif
	}
}

void phydm_update_rx_idle_beam(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;
	struct smt_ant_honbo *sat_tab = &dm->dm_sat_table;
	u32 i;

	sat_tab->update_beam_codeword = phydm_construct_hl_beam_codeword(dm,
									 &sat_tab->rx_idle_beam[0],
									 sat_tab->ant_num);
	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "Set target beam_pattern codeword = (( 0x%x ))\n",
		  sat_tab->update_beam_codeword);

	for (i = 0; i < (sat_tab->ant_num); i++)
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "[ Update Rx-Idle-Beam ] RxIdleBeam[%d] =%d\n", i,
			  sat_tab->rx_idle_beam[i]);

#if DEV_BUS_TYPE == RT_PCI_INTERFACE
	if (dm->support_interface == ODM_ITRF_PCIE)
		phydm_update_beam_pattern(dm, sat_tab->update_beam_codeword, sat_tab->rfu_codeword_total_bit_num);
#endif
#if DEV_BUS_TYPE == RT_USB_INTERFACE || DEV_BUS_TYPE == RT_SDIO_INTERFACE
	if (dm->support_interface == ODM_ITRF_USB || dm->support_interface == ODM_ITRF_SDIO)
		odm_schedule_work_item(&sat_tab->hl_smart_antenna_workitem);
#if 0
	/*odm_stall_execution(1);*/
#endif
#endif

	sat_tab->pre_codeword = sat_tab->update_beam_codeword;
}

void phydm_hl_smart_ant_debug(
	void *dm_void,
	char input[][16],
	u32 *_used,
	char *output,
	u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct smt_ant_honbo *sat_tab = &dm->dm_sat_table;
	u32 used = *_used;
	u32 out_len = *_out_len;
	u32 one = 0x1;
	u32 codeword_length = sat_tab->rfu_codeword_total_bit_num;
	u32 beam_ctrl_signal, i;
	u8 devide_num = 4;

	if (dm_value[0] == 1) { /*@fix beam pattern*/

		sat_tab->fix_beam_pattern_en = dm_value[1];

		if (sat_tab->fix_beam_pattern_en == 1) {
			sat_tab->fix_beam_pattern_codeword = dm_value[2];

			if (sat_tab->fix_beam_pattern_codeword > (one << codeword_length)) {
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "[ SmartAnt ] Codeword overflow, Current codeword is ((0x%x)), and should be less than ((%d))bit\n",
					  sat_tab->fix_beam_pattern_codeword,
					  codeword_length);

				(sat_tab->fix_beam_pattern_codeword) &= 0xffffff;

				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "[ SmartAnt ] Auto modify to (0x%x)\n",
					  sat_tab->fix_beam_pattern_codeword);
			}

			sat_tab->update_beam_codeword = sat_tab->fix_beam_pattern_codeword;

			/*@---------------------------------------------------------*/
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "Fix Beam Pattern\n");

			devide_num = (sat_tab->rfu_protocol_type == 2) ? 6 : 4;

			for (i = 0; i <= (codeword_length - 1); i++) {
				beam_ctrl_signal = (boolean)((sat_tab->update_beam_codeword & BIT(i)) >> i);

				if (i == (codeword_length - 1))
					PDM_SNPF(out_len, used,
						 output + used,
						 out_len - used,
						 "%d]\n",
						 beam_ctrl_signal);
				else if (i == 0)
					PDM_SNPF(out_len, used,
						 output + used,
						 out_len - used,
						 "Send Codeword[1:24] to RFU -> [%d",
						 beam_ctrl_signal);
				else if ((i % devide_num) == (devide_num - 1))
					PDM_SNPF(out_len, used,
						 output + used,
						 out_len - used, "%d|",
						 beam_ctrl_signal);
				else
					PDM_SNPF(out_len, used,
						 output + used,
						 out_len - used, "%d",
						 beam_ctrl_signal);
			}
/*@---------------------------------------------------------*/

			#if DEV_BUS_TYPE == RT_PCI_INTERFACE
			if (dm->support_interface == ODM_ITRF_PCIE)
				phydm_update_beam_pattern(dm, sat_tab->update_beam_codeword, sat_tab->rfu_codeword_total_bit_num);
			#endif
			#if DEV_BUS_TYPE == RT_USB_INTERFACE || DEV_BUS_TYPE == RT_SDIO_INTERFACE
			if (dm->support_interface == ODM_ITRF_USB || dm->support_interface == ODM_ITRF_SDIO)
				odm_schedule_work_item(&sat_tab->hl_smart_antenna_workitem);
#if 0
			/*odm_stall_execution(1);*/
#endif
			#endif
		} else if (sat_tab->fix_beam_pattern_en == 0)
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "[ SmartAnt ] Smart Antenna: Enable\n");

	} else if (dm_value[0] == 2) { /*set latch time*/

		sat_tab->latch_time = dm_value[1];
		PHYDM_DBG(dm, DBG_ANT_DIV, "[ SmartAnt ]  latch_time =0x%x\n",
			  sat_tab->latch_time);
	} else if (dm_value[0] == 3) {
		sat_tab->fix_training_num_en = dm_value[1];

		if (sat_tab->fix_training_num_en == 1) {
			sat_tab->per_beam_training_pkt_num = (u8)dm_value[2];
			sat_tab->decision_holding_period = (u8)dm_value[3];

			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "[SmartAnt][Dbg] Fix_train_en = (( %d )), train_pkt_num = (( %d )), holding_period = (( %d )),\n",
				 sat_tab->fix_training_num_en,
				 sat_tab->per_beam_training_pkt_num,
				 sat_tab->decision_holding_period);

		} else if (sat_tab->fix_training_num_en == 0) {
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "[ SmartAnt ]  AUTO per_beam_training_pkt_num\n");
		}
	} else if (dm_value[0] == 4) {
		if (dm_value[1] == 1) {
			sat_tab->ant_num = 1;
			sat_tab->first_train_ant = MAIN_ANT;

		} else if (dm_value[1] == 2) {
			sat_tab->ant_num = 1;
			sat_tab->first_train_ant = AUX_ANT;

		} else if (dm_value[1] == 3) {
			sat_tab->ant_num = 2;
			sat_tab->first_train_ant = MAIN_ANT;
		}

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "[ SmartAnt ]  Set ant Num = (( %d )), first_train_ant = (( %d ))\n",
			 sat_tab->ant_num, (sat_tab->first_train_ant - 1));
	} else if (dm_value[0] == 5) {
		if (dm_value[1] <= 3) {
			sat_tab->rfu_codeword_table[dm_value[1]] = dm_value[2];
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "[ SmartAnt ] Set Beam_2G: (( %d )), RFU codeword table = (( 0x%x ))\n",
				 dm_value[1], dm_value[2]);
		} else {
			for (i = 0; i < 4; i++) {
				PDM_SNPF(out_len, used, output + used,
					 out_len - used,
					 "[ SmartAnt ] Show Beam_2G: (( %d )), RFU codeword table = (( 0x%x ))\n",
					 i, sat_tab->rfu_codeword_table[i]);
			}
		}
	} else if (dm_value[0] == 6) {
		if (dm_value[1] <= 3) {
			sat_tab->rfu_codeword_table_5g[dm_value[1]] = dm_value[2];
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "[ SmartAnt ] Set Beam_5G: (( %d )), RFU codeword table = (( 0x%x ))\n",
				 dm_value[1], dm_value[2]);
		} else {
			for (i = 0; i < 4; i++) {
				PDM_SNPF(out_len, used, output + used,
					 out_len - used,
					 "[ SmartAnt ] Show Beam_5G: (( %d )), RFU codeword table = (( 0x%x ))\n",
					 i, sat_tab->rfu_codeword_table_5g[i]);
			}
		}
	} else if (dm_value[0] == 7) {
		if (dm_value[1] <= 4) {
			sat_tab->beam_patten_num_each_ant = dm_value[1];
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "[ SmartAnt ] Set Beam number = (( %d ))\n",
				 sat_tab->beam_patten_num_each_ant);
		} else {
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "[ SmartAnt ] Show Beam number = (( %d ))\n",
				 sat_tab->beam_patten_num_each_ant);
		}
	}
	*_used = used;
	*_out_len = out_len;
}

void phydm_set_all_ant_same_beam_num(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct smt_ant_honbo *sat_tab = &dm->dm_sat_table;

	if (dm->ant_div_type == HL_SW_SMART_ANT_TYPE1) { /*@2ant for 8821A*/

		sat_tab->rx_idle_beam[0] = sat_tab->fast_training_beam_num;
		sat_tab->rx_idle_beam[1] = sat_tab->fast_training_beam_num;
	}

	sat_tab->update_beam_codeword = phydm_construct_hl_beam_codeword(dm,
									 &sat_tab->rx_idle_beam[0],
									 sat_tab->ant_num);

	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "[ SmartAnt ] Set all ant beam_pattern: codeword = (( 0x%x ))\n",
		  sat_tab->update_beam_codeword);

#if DEV_BUS_TYPE == RT_PCI_INTERFACE
	if (dm->support_interface == ODM_ITRF_PCIE)
		phydm_update_beam_pattern(dm, sat_tab->update_beam_codeword, sat_tab->rfu_codeword_total_bit_num);
#endif
#if DEV_BUS_TYPE == RT_USB_INTERFACE || DEV_BUS_TYPE == RT_SDIO_INTERFACE
	if (dm->support_interface == ODM_ITRF_USB || dm->support_interface == ODM_ITRF_SDIO)
		odm_schedule_work_item(&sat_tab->hl_smart_antenna_workitem);
/*odm_stall_execution(1);*/
#endif
}

void odm_fast_ant_training_hl_smart_antenna_type1(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct smt_ant_honbo *sat_tab = &dm->dm_sat_table;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;
	struct sw_antenna_switch *dm_swat_table = &dm->dm_swat_table;
	u32 codeword = 0, i, j;
	u32 target_ant;
	u32 avg_rssi_tmp, avg_rssi_tmp_ma;
	u32 target_ant_beam_max_rssi[SUPPORT_RF_PATH_NUM] = {0};
	u32 max_beam_ant_rssi = 0;
	u32 target_ant_beam[SUPPORT_RF_PATH_NUM] = {0};
	u32 beam_tmp;
	u8 next_ant;
	u32 rssi_sorting_seq[SUPPORT_BEAM_PATTERN_NUM] = {0};
	u32 rank_idx_seq[SUPPORT_BEAM_PATTERN_NUM] = {0};
	u32 rank_idx_out[SUPPORT_BEAM_PATTERN_NUM] = {0};
	u8 per_beam_rssi_diff_tmp = 0, training_pkt_num_offset;
	u32 break_counter = 0;
	u32 used_ant;

	if (!dm->is_linked) {
		PHYDM_DBG(dm, DBG_ANT_DIV, "[No Link!!!]\n");

		if (fat_tab->is_become_linked == true) {
			PHYDM_DBG(dm, DBG_ANT_DIV, "Link->no Link\n");
			fat_tab->fat_state = FAT_BEFORE_LINK_STATE;
			odm_ant_div_on_off(dm, ANTDIV_OFF, ANT_PATH_A);
			odm_tx_by_tx_desc_or_reg(dm, TX_BY_REG);
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "change to (( %d )) FAT_state\n",
				  fat_tab->fat_state);

			fat_tab->is_become_linked = dm->is_linked;
		}
		return;

	} else {
		if (fat_tab->is_become_linked == false) {
			PHYDM_DBG(dm, DBG_ANT_DIV, "[Linked !!!]\n");

			fat_tab->fat_state = FAT_PREPARE_STATE;
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "change to (( %d )) FAT_state\n",
				  fat_tab->fat_state);

#if 0
			/*sat_tab->fast_training_beam_num = 0;*/
			/*phydm_set_all_ant_same_beam_num(dm);*/
#endif

			fat_tab->is_become_linked = dm->is_linked;
		}
	}

	if (!(*fat_tab->p_force_tx_by_desc)) {
		if (dm->is_one_entry_only == true)
			odm_tx_by_tx_desc_or_reg(dm, TX_BY_REG);
		else
			odm_tx_by_tx_desc_or_reg(dm, TX_BY_DESC);
	}

#if 0
	/*PHYDM_DBG(dm, DBG_ANT_DIV, "HL Smart ant Training: state (( %d ))\n", fat_tab->fat_state);*/
#endif

	/* @[DECISION STATE] */
	/*@=======================================================================================*/
	if (fat_tab->fat_state == FAT_DECISION_STATE) {
		PHYDM_DBG(dm, DBG_ANT_DIV, "[ 3. In Decision state]\n");
		phydm_fast_training_enable(dm, FAT_OFF);

		break_counter = 0;
		/*@compute target beam in each antenna*/
		for (i = (sat_tab->first_train_ant - 1); i < sat_tab->ant_num_total; i++) {
			for (j = 0; j < (sat_tab->beam_patten_num_each_ant); j++) {
				if (sat_tab->pkt_rssi_cnt[i][j] == 0) {
					avg_rssi_tmp = sat_tab->pkt_rssi_pre[i][j];
					avg_rssi_tmp = (avg_rssi_tmp >= 2) ? (avg_rssi_tmp - 2) : avg_rssi_tmp;
					avg_rssi_tmp_ma = avg_rssi_tmp;
				} else {
					avg_rssi_tmp = (sat_tab->pkt_rssi_sum[i][j]) / (sat_tab->pkt_rssi_cnt[i][j]);
					avg_rssi_tmp_ma = (avg_rssi_tmp + sat_tab->pkt_rssi_pre[i][j]) >> 1;
				}

				rssi_sorting_seq[j] = avg_rssi_tmp;
				sat_tab->pkt_rssi_pre[i][j] = avg_rssi_tmp;

				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "ant[%d], Beam[%d]: pkt_cnt=(( %d )), avg_rssi_MA=(( %d )), avg_rssi=(( %d ))\n",
					  i, j, sat_tab->pkt_rssi_cnt[i][j],
					  avg_rssi_tmp_ma, avg_rssi_tmp);

				if (avg_rssi_tmp > target_ant_beam_max_rssi[i]) {
					target_ant_beam[i] = j;
					target_ant_beam_max_rssi[i] = avg_rssi_tmp;
				}

				/*reset counter value*/
				sat_tab->pkt_rssi_sum[i][j] = 0;
				sat_tab->pkt_rssi_cnt[i][j] = 0;
			}
			sat_tab->rx_idle_beam[i] = target_ant_beam[i];
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "---------> Target of ant[%d]: Beam_num-(( %d )) RSSI= ((%d))\n",
				  i, target_ant_beam[i],
				  target_ant_beam_max_rssi[i]);

#if 0
			/*sorting*/
			/*@
			PHYDM_DBG(dm, DBG_ANT_DIV, "[Pre]rssi_sorting_seq = [%d, %d, %d, %d]\n", rssi_sorting_seq[0], rssi_sorting_seq[1], rssi_sorting_seq[2], rssi_sorting_seq[3]);
			*/

			/*phydm_seq_sorting(dm, &rssi_sorting_seq[0], &rank_idx_seq[0], &rank_idx_out[0], SUPPORT_BEAM_PATTERN_NUM);*/

			/*@
			PHYDM_DBG(dm, DBG_ANT_DIV, "[Post]rssi_sorting_seq = [%d, %d, %d, %d]\n", rssi_sorting_seq[0], rssi_sorting_seq[1], rssi_sorting_seq[2], rssi_sorting_seq[3]);
			PHYDM_DBG(dm, DBG_ANT_DIV, "[Post]rank_idx_seq = [%d, %d, %d, %d]\n", rank_idx_seq[0], rank_idx_seq[1], rank_idx_seq[2], rank_idx_seq[3]);
			PHYDM_DBG(dm, DBG_ANT_DIV, "[Post]rank_idx_out = [%d, %d, %d, %d]\n", rank_idx_out[0], rank_idx_out[1], rank_idx_out[2], rank_idx_out[3]);
			*/
#endif

			if (target_ant_beam_max_rssi[i] > max_beam_ant_rssi) {
				target_ant = i;
				max_beam_ant_rssi = target_ant_beam_max_rssi[i];
#if
				/*PHYDM_DBG(dm, DBG_ANT_DIV, "Target of ant = (( %d )) max_beam_ant_rssi = (( %d ))\n",
					target_ant,  max_beam_ant_rssi);*/
#endif
			}
			break_counter++;
			if (break_counter >= sat_tab->ant_num)
				break;
		}

#ifdef CONFIG_FAT_PATCH
		break_counter = 0;
		for (i = (sat_tab->first_train_ant - 1); i < sat_tab->ant_num_total; i++) {
			for (j = 0; j < (sat_tab->beam_patten_num_each_ant); j++) {
				per_beam_rssi_diff_tmp = (u8)(max_beam_ant_rssi - sat_tab->pkt_rssi_pre[i][j]);
				sat_tab->beam_train_rssi_diff[i][j] = per_beam_rssi_diff_tmp;

				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "ant[%d], Beam[%d]: RSSI_diff= ((%d))\n",
					  i, j, per_beam_rssi_diff_tmp);
			}
			break_counter++;
			if (break_counter >= sat_tab->ant_num)
				break;
		}
#endif

		if (target_ant == 0)
			target_ant = MAIN_ANT;
		else if (target_ant == 1)
			target_ant = AUX_ANT;

		if (sat_tab->ant_num > 1) {
			/* @[ update RX ant ]*/
			odm_update_rx_idle_ant(dm, (u8)target_ant);

			/* @[ update TX ant ]*/
			odm_update_tx_ant(dm, (u8)target_ant, (fat_tab->train_idx));
		}

		/*set beam in each antenna*/
		phydm_update_rx_idle_beam(dm);

		odm_ant_div_on_off(dm, ANTDIV_ON, ANT_PATH_A);
		fat_tab->fat_state = FAT_PREPARE_STATE;
		return;
	}
	/* @[TRAINING STATE] */
	else if (fat_tab->fat_state == FAT_TRAINING_STATE) {
		PHYDM_DBG(dm, DBG_ANT_DIV, "[ 2. In Training state]\n");

		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "fat_beam_n = (( %d )), pre_fat_beam_n = (( %d ))\n",
			  sat_tab->fast_training_beam_num,
			  sat_tab->pre_fast_training_beam_num);

		if (sat_tab->fast_training_beam_num > sat_tab->pre_fast_training_beam_num)

			sat_tab->force_update_beam_en = 0;

		else {
			sat_tab->force_update_beam_en = 1;

			sat_tab->pkt_counter = 0;
			beam_tmp = sat_tab->fast_training_beam_num;
			if (sat_tab->fast_training_beam_num >= (sat_tab->beam_patten_num_each_ant - 1)) {
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "[Timeout Update]  Beam_num (( %d )) -> (( decision ))\n",
					  sat_tab->fast_training_beam_num);
				fat_tab->fat_state = FAT_DECISION_STATE;
				odm_fast_ant_training_hl_smart_antenna_type1(dm);

			} else {
				sat_tab->fast_training_beam_num++;

				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "[Timeout Update]  Beam_num (( %d )) -> (( %d ))\n",
					  beam_tmp,
					  sat_tab->fast_training_beam_num);
				phydm_set_all_ant_same_beam_num(dm);
				fat_tab->fat_state = FAT_TRAINING_STATE;
			}
		}
		sat_tab->pre_fast_training_beam_num = sat_tab->fast_training_beam_num;
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "[prepare state] Update Pre_Beam =(( %d ))\n",
			  sat_tab->pre_fast_training_beam_num);
	}
	/*  @[Prepare state] */
	/*@=======================================================================================*/
	else if (fat_tab->fat_state == FAT_PREPARE_STATE) {
		PHYDM_DBG(dm, DBG_ANT_DIV, "\n\n[ 1. In Prepare state]\n");

		if (dm->pre_traffic_load == dm->traffic_load) {
			if (sat_tab->decision_holding_period != 0) {
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "Holding_period = (( %d )), return!!!\n",
					  sat_tab->decision_holding_period);
				sat_tab->decision_holding_period--;
				return;
			}
		}

		/* Set training packet number*/
		if (sat_tab->fix_training_num_en == 0) {
			switch (dm->traffic_load) {
			case TRAFFIC_HIGH:
				sat_tab->per_beam_training_pkt_num = 8;
				sat_tab->decision_holding_period = 2;
				break;
			case TRAFFIC_MID:
				sat_tab->per_beam_training_pkt_num = 6;
				sat_tab->decision_holding_period = 3;
				break;
			case TRAFFIC_LOW:
				sat_tab->per_beam_training_pkt_num = 3; /*ping 60000*/
				sat_tab->decision_holding_period = 4;
				break;
			case TRAFFIC_ULTRA_LOW:
				sat_tab->per_beam_training_pkt_num = 1;
				sat_tab->decision_holding_period = 6;
				break;
			default:
				break;
			}
		}
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "Fix_training_en = (( %d )), training_pkt_num_base = (( %d )), holding_period = ((%d))\n",
			  sat_tab->fix_training_num_en,
			  sat_tab->per_beam_training_pkt_num,
			  sat_tab->decision_holding_period);

#ifdef CONFIG_FAT_PATCH
		break_counter = 0;
		for (i = (sat_tab->first_train_ant - 1); i < sat_tab->ant_num_total; i++) {
			for (j = 0; j < (sat_tab->beam_patten_num_each_ant); j++) {
				per_beam_rssi_diff_tmp = sat_tab->beam_train_rssi_diff[i][j];
				training_pkt_num_offset = per_beam_rssi_diff_tmp;

				if (sat_tab->per_beam_training_pkt_num > training_pkt_num_offset)
					sat_tab->beam_train_cnt[i][j] = sat_tab->per_beam_training_pkt_num - training_pkt_num_offset;
				else
					sat_tab->beam_train_cnt[i][j] = 1;

				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "ant[%d]: Beam_num-(( %d ))  training_pkt_num = ((%d))\n",
					  i, j, sat_tab->beam_train_cnt[i][j]);
			}
			break_counter++;
			if (break_counter >= sat_tab->ant_num)
				break;
		}

		phydm_fast_training_enable(dm, FAT_OFF);
		sat_tab->pre_beacon_counter = sat_tab->beacon_counter;
		sat_tab->update_beam_idx = 0;

		if (*dm->band_type == ODM_BAND_5G) {
			PHYDM_DBG(dm, DBG_ANT_DIV, "Set 5G ant\n");
			/*used_ant = (sat_tab->first_train_ant == MAIN_ANT) ? AUX_ANT : MAIN_ANT;*/
			used_ant = sat_tab->first_train_ant;
		} else {
			PHYDM_DBG(dm, DBG_ANT_DIV, "Set 2.4G ant\n");
			used_ant = sat_tab->first_train_ant;
		}

		odm_update_rx_idle_ant(dm, (u8)used_ant);

#else
		/* Set training MAC addr. of target */
		odm_set_next_mac_addr_target(dm);
		phydm_fast_training_enable(dm, FAT_ON);
#endif

		odm_ant_div_on_off(dm, ANTDIV_OFF, ANT_PATH_A);
		sat_tab->pkt_counter = 0;
		sat_tab->fast_training_beam_num = 0;
		phydm_set_all_ant_same_beam_num(dm);
		sat_tab->pre_fast_training_beam_num = sat_tab->fast_training_beam_num;
		fat_tab->fat_state = FAT_TRAINING_STATE;
	}
}

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

void phydm_beam_switch_workitem_callback(
	void *context)
{
	void *adapter = (void *)context;
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(((PADAPTER)adapter));
	struct dm_struct *dm = &hal_data->DM_OutSrc;
	struct smt_ant_honbo *sat_tab = &dm->dm_sat_table;

#if DEV_BUS_TYPE != RT_PCI_INTERFACE
	sat_tab->pkt_skip_statistic_en = 1;
#endif
	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "[ SmartAnt ] Beam Switch Workitem Callback, pkt_skip_statistic_en = (( %d ))\n",
		  sat_tab->pkt_skip_statistic_en);

	phydm_update_beam_pattern(dm, sat_tab->update_beam_codeword, sat_tab->rfu_codeword_total_bit_num);

#if DEV_BUS_TYPE != RT_PCI_INTERFACE
#if 0
	/*odm_stall_execution(sat_tab->latch_time);*/
#endif
	sat_tab->pkt_skip_statistic_en = 0;
#endif
	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "pkt_skip_statistic_en = (( %d )), latch_time = (( %d ))\n",
		  sat_tab->pkt_skip_statistic_en, sat_tab->latch_time);
}

void phydm_beam_decision_workitem_callback(
	void *context)
{
	void *adapter = (void *)context;
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(((PADAPTER)adapter));
	struct dm_struct *dm = &hal_data->DM_OutSrc;

	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "[ SmartAnt ] Beam decision Workitem Callback\n");
	odm_fast_ant_training_hl_smart_antenna_type1(dm);
}
#endif

#endif /*@#ifdef CONFIG_HL_SMART_ANTENNA_TYPE1*/

#endif /*@#ifdef CONFIG_HL_SMART_ANTENNA*/

void phydm_smt_ant_config(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct smt_ant *smtant_table = &dm->smtant_table;

#if (defined(CONFIG_CUMITEK_SMART_ANTENNA))

	dm->support_ability |= ODM_BB_SMT_ANT;
	smtant_table->smt_ant_vendor = SMTANT_CUMITEK;
	smtant_table->smt_ant_type = 1;
#if (RTL8822B_SUPPORT == 1)
	dm->rfe_type = SMTANT_TMP_RFE_TYPE;
#endif
#elif (defined(CONFIG_HL_SMART_ANTENNA))

	dm->support_ability |= ODM_BB_SMT_ANT;
	smtant_table->smt_ant_vendor = SMTANT_HON_BO;

#ifdef CONFIG_HL_SMART_ANTENNA_TYPE1
	smtant_table->smt_ant_type = 1;
#endif

#ifdef CONFIG_HL_SMART_ANTENNA_TYPE2
	smtant_table->smt_ant_type = 2;
#endif
#endif

	PHYDM_DBG(dm, DBG_SMT_ANT,
		  "[SmtAnt Config] Vendor=((%d)), Smt_ant_type =((%d))\n",
		  smtant_table->smt_ant_vendor, smtant_table->smt_ant_type);
}

void phydm_smt_ant_init(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct smt_ant *smtant_table = &dm->smtant_table;

	phydm_smt_ant_config(dm);

	if (smtant_table->smt_ant_vendor == SMTANT_CUMITEK) {
#if (defined(CONFIG_CUMITEK_SMART_ANTENNA))
#if (RTL8822B_SUPPORT == 1)
		if (dm->support_ic_type == ODM_RTL8822B)
			phydm_cumitek_smt_ant_init_8822b(dm);
#endif

#if (RTL8197F_SUPPORT == 1)
		if (dm->support_ic_type == ODM_RTL8197F)
			phydm_cumitek_smt_ant_init_8197f(dm);
#endif
/*@jj add 20170822*/
#if (RTL8192F_SUPPORT == 1)
		if (dm->support_ic_type == ODM_RTL8192F)
			phydm_cumitek_smt_ant_init_8192f(dm);
#endif
#endif /*@#if (defined(CONFIG_CUMITEK_SMART_ANTENNA))*/

	} else if (smtant_table->smt_ant_vendor == SMTANT_HON_BO) {
#if (defined(CONFIG_HL_SMART_ANTENNA))
#ifdef CONFIG_HL_SMART_ANTENNA_TYPE1
		if (dm->support_ic_type == ODM_RTL8821)
			phydm_hl_smart_ant_type1_init_8821a(dm);
#endif

#ifdef CONFIG_HL_SMART_ANTENNA_TYPE2
		if (dm->support_ic_type == ODM_RTL8822B)
			phydm_hl_smart_ant_type2_init_8822b(dm);
#endif
#endif /*@#if (defined(CONFIG_HL_SMART_ANTENNA))*/
	}
}
#endif
