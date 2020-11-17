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

#ifdef PHYDM_AUTO_DEGBUG

void phydm_check_hang_reset(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_auto_dbg_struct *atd_t = &dm->auto_dbg_table;

	atd_t->dbg_step = 0;
	atd_t->auto_dbg_type = AUTO_DBG_STOP;
	phydm_pause_dm_watchdog(dm, PHYDM_RESUME);
	dm->debug_components &= (~ODM_COMP_API);
}

void phydm_check_hang_init(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_auto_dbg_struct *atd_t = &dm->auto_dbg_table;

	atd_t->dbg_step = 0;
	atd_t->auto_dbg_type = AUTO_DBG_STOP;
	phydm_pause_dm_watchdog(dm, PHYDM_RESUME);
}

#if (ODM_IC_11N_SERIES_SUPPORT == 1)
void phydm_auto_check_hang_engine_n(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_auto_dbg_struct *atd_t = &dm->auto_dbg_table;
	struct n_dbgport_803 dbgport_803 = {0};
	u32 value32_tmp = 0, value32_tmp_2 = 0;
	u8 i;
	u32 curr_dbg_port_val[DBGPORT_CHK_NUM];
	u16 curr_ofdm_t_cnt;
	u16 curr_ofdm_r_cnt;
	u16 curr_cck_t_cnt;
	u16 curr_cck_r_cnt;
	u16 curr_ofdm_crc_error_cnt;
	u16 curr_cck_crc_error_cnt;
	u16 diff_ofdm_t_cnt;
	u16 diff_ofdm_r_cnt;
	u16 diff_cck_t_cnt;
	u16 diff_cck_r_cnt;
	u16 diff_ofdm_crc_error_cnt;
	u16 diff_cck_crc_error_cnt;
	u8 rf_mode;

	if (atd_t->auto_dbg_type == AUTO_DBG_STOP)
		return;

	if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		phydm_check_hang_reset(dm);
		return;
	}

	if (atd_t->dbg_step == 0) {
		pr_debug("dbg_step=0\n\n");

		/*Reset all packet counter*/
		odm_set_bb_reg(dm, R_0xf14, BIT(16), 1);
		odm_set_bb_reg(dm, R_0xf14, BIT(16), 0);

	} else if (atd_t->dbg_step == 1) {
		pr_debug("dbg_step=1\n\n");

		/*Check packet counter Register*/
		atd_t->ofdm_t_cnt = (u16)odm_get_bb_reg(dm, R_0x9cc, MASKHWORD);
		atd_t->ofdm_r_cnt = (u16)odm_get_bb_reg(dm, R_0xf94, MASKLWORD);
		atd_t->ofdm_crc_error_cnt = (u16)odm_get_bb_reg(dm, R_0xf94,
								MASKHWORD);

		atd_t->cck_t_cnt = (u16)odm_get_bb_reg(dm, R_0x9d0, MASKHWORD);
		atd_t->cck_r_cnt = (u16)odm_get_bb_reg(dm, R_0xfa0, MASKHWORD);
		atd_t->cck_crc_error_cnt = (u16)odm_get_bb_reg(dm, R_0xf84,
							       0x3fff);

		/*Check Debug Port*/
		for (i = 0; i < DBGPORT_CHK_NUM; i++) {
			if (phydm_set_bb_dbg_port(dm, DBGPORT_PRI_3,
						  (u32)atd_t->dbg_port_table[i])
						  ) {
				atd_t->dbg_port_val[i] =
					phydm_get_bb_dbg_port_val(dm);
				phydm_release_bb_dbg_port(dm);
			}
		}

	} else if (atd_t->dbg_step == 2) {
		pr_debug("dbg_step=2\n\n");

		/*Check packet counter Register*/
		curr_ofdm_t_cnt = (u16)odm_get_bb_reg(dm, R_0x9cc, MASKHWORD);
		curr_ofdm_r_cnt = (u16)odm_get_bb_reg(dm, R_0xf94, MASKLWORD);
		curr_ofdm_crc_error_cnt = (u16)odm_get_bb_reg(dm, R_0xf94,
							      MASKHWORD);

		curr_cck_t_cnt = (u16)odm_get_bb_reg(dm, R_0x9d0, MASKHWORD);
		curr_cck_r_cnt = (u16)odm_get_bb_reg(dm, R_0xfa0, MASKHWORD);
		curr_cck_crc_error_cnt = (u16)odm_get_bb_reg(dm, R_0xf84,
							     0x3fff);

		/*Check Debug Port*/
		for (i = 0; i < DBGPORT_CHK_NUM; i++) {
			if (phydm_set_bb_dbg_port(dm, DBGPORT_PRI_3,
						  (u32)atd_t->dbg_port_table[i])
						  ) {
				curr_dbg_port_val[i] =
					phydm_get_bb_dbg_port_val(dm);
				phydm_release_bb_dbg_port(dm);
			}
		}

		/*=== Make check hang decision ===============================*/
		pr_debug("Check Hang Decision\n\n");

		/* ----- Check RF Register -----------------------------------*/
		for (i = 0; i < dm->num_rf_path; i++) {
			rf_mode = (u8)odm_get_rf_reg(dm, i, RF_0x0, 0xf0000);
			pr_debug("RF0x0[%d] = 0x%x\n", i, rf_mode);
			if (rf_mode > 3) {
				pr_debug("Incorrect RF mode\n");
				pr_debug("ReasonCode:RHN-1\n");
			}
		}
		value32_tmp = odm_get_rf_reg(dm, 0, RF_0xb0, 0xf0000);
		if (dm->support_ic_type == ODM_RTL8188E) {
			if (value32_tmp != 0xff8c8) {
				pr_debug("ReasonCode:RHN-3\n");
			}
		}
		/* ----- Check BB Register ----------------------------------*/
		/*BB mode table*/
		value32_tmp = odm_get_bb_reg(dm, R_0x824, 0xe);
		value32_tmp_2 = odm_get_bb_reg(dm, R_0x82c, 0xe);
		pr_debug("BB TX mode table {A, B}= {%d, %d}\n",
			 value32_tmp, value32_tmp_2);

		if (value32_tmp > 3 || value32_tmp_2 > 3) {
			pr_debug("ReasonCode:RHN-2\n");
		}

		value32_tmp = odm_get_bb_reg(dm, R_0x824, 0x700000);
		value32_tmp_2 = odm_get_bb_reg(dm, R_0x82c, 0x700000);
		pr_debug("BB RX mode table {A, B}= {%d, %d}\n", value32_tmp,
			 value32_tmp_2);

		if (value32_tmp > 3 || value32_tmp_2 > 3) {
			pr_debug("ReasonCode:RHN-2\n");
		}

		/*BB HW Block*/
		value32_tmp = odm_get_bb_reg(dm, R_0x800, MASKDWORD);

		if (!(value32_tmp & BIT(24))) {
			pr_debug("Reg0x800[24] = 0, CCK BLK is disabled\n");
			pr_debug("ReasonCode: THN-3\n");
		}

		if (!(value32_tmp & BIT(25))) {
			pr_debug("Reg0x800[24] = 0, OFDM BLK is disabled\n");
			pr_debug("ReasonCode:THN-3\n");
		}

		/*BB Continue TX*/
		value32_tmp = odm_get_bb_reg(dm, R_0xd00, 0x70000000);
		pr_debug("Continue TX=%d\n", value32_tmp);
		if (value32_tmp != 0) {
			pr_debug("ReasonCode: THN-4\n");
		}

		/* ----- Check Packet Counter --------------------------------*/
		diff_ofdm_t_cnt = curr_ofdm_t_cnt - atd_t->ofdm_t_cnt;
		diff_ofdm_r_cnt = curr_ofdm_r_cnt - atd_t->ofdm_r_cnt;
		diff_ofdm_crc_error_cnt = curr_ofdm_crc_error_cnt -
					  atd_t->ofdm_crc_error_cnt;

		diff_cck_t_cnt = curr_cck_t_cnt - atd_t->cck_t_cnt;
		diff_cck_r_cnt = curr_cck_r_cnt - atd_t->cck_r_cnt;
		diff_cck_crc_error_cnt = curr_cck_crc_error_cnt -
					 atd_t->cck_crc_error_cnt;

		pr_debug("OFDM[t=0~1] {TX, RX, CRC_error} = {%d, %d, %d}\n",
			 atd_t->ofdm_t_cnt, atd_t->ofdm_r_cnt,
			 atd_t->ofdm_crc_error_cnt);
		pr_debug("OFDM[t=1~2] {TX, RX, CRC_error} = {%d, %d, %d}\n",
			 curr_ofdm_t_cnt, curr_ofdm_r_cnt,
			 curr_ofdm_crc_error_cnt);
		pr_debug("OFDM_diff {TX, RX, CRC_error} = {%d, %d, %d}\n",
			 diff_ofdm_t_cnt, diff_ofdm_r_cnt,
			 diff_ofdm_crc_error_cnt);

		pr_debug("CCK[t=0~1] {TX, RX, CRC_error} = {%d, %d, %d}\n",
			 atd_t->cck_t_cnt, atd_t->cck_r_cnt,
			 atd_t->cck_crc_error_cnt);
		pr_debug("CCK[t=1~2] {TX, RX, CRC_error} = {%d, %d, %d}\n",
			 curr_cck_t_cnt, curr_cck_r_cnt,
			 curr_cck_crc_error_cnt);
		pr_debug("CCK_diff {TX, RX, CRC_error} = {%d, %d, %d}\n",
			 diff_cck_t_cnt, diff_cck_r_cnt,
			 diff_cck_crc_error_cnt);

		/* ----- Check Dbg Port --------------------------------*/

		for (i = 0; i < DBGPORT_CHK_NUM; i++) {
			pr_debug("Dbg_port=((0x%x))\n",
				 atd_t->dbg_port_table[i]);
			pr_debug("Val{pre, curr}={0x%x, 0x%x}\n",
				 atd_t->dbg_port_val[i], curr_dbg_port_val[i]);

			if (atd_t->dbg_port_table[i] == 0) {
				if (atd_t->dbg_port_val[i] ==
				    curr_dbg_port_val[i]) {
					pr_debug("BB state hang\n");
					pr_debug("ReasonCode:\n");
				}

			} else if (atd_t->dbg_port_table[i] == 0x803) {
				if (atd_t->dbg_port_val[i] ==
				    curr_dbg_port_val[i]) {
					/* dbgport_803 =  */
					/* (struct n_dbgport_803 )   */
					/* (atd_t->dbg_port_val[i]); */
					odm_move_memory(dm, &dbgport_803,
							&atd_t->dbg_port_val[i],
							sizeof(struct n_dbgport_803));
					pr_debug("RSTB{BB, GLB, OFDM}={%d, %d,%d}\n",
						 dbgport_803.bb_rst_b,
						 dbgport_803.glb_rst_b,
						 dbgport_803.ofdm_rst_b);
					pr_debug("{ofdm_tx_en, cck_tx_en, phy_tx_on}={%d, %d, %d}\n",
						 dbgport_803.ofdm_tx_en,
						 dbgport_803.cck_tx_en,
						 dbgport_803.phy_tx_on);
					pr_debug("CCA_PP{OFDM, CCK}={%d, %d}\n",
						 dbgport_803.ofdm_cca_pp,
						 dbgport_803.cck_cca_pp);

					if (dbgport_803.phy_tx_on)
						pr_debug("Maybe TX Hang\n");
					else if (dbgport_803.ofdm_cca_pp ||
						 dbgport_803.cck_cca_pp)
						pr_debug("Maybe RX Hang\n");
				}

			} else if (atd_t->dbg_port_table[i] == 0x208) {
				if ((atd_t->dbg_port_val[i] & BIT(30)) &&
				    (curr_dbg_port_val[i] & BIT(30))) {
					pr_debug("EDCCA Pause TX\n");
					pr_debug("ReasonCode: THN-2\n");
				}

			} else if (atd_t->dbg_port_table[i] == 0xab0) {
				/* atd_t->dbg_port_val[i] & 0xffffff == 0 */
				/* curr_dbg_port_val[i] & 0xffffff == 0 */
				if (((atd_t->dbg_port_val[i] &
				      MASK24BITS) == 0) ||
				    ((curr_dbg_port_val[i] &
				      MASK24BITS) == 0)) {
					pr_debug("Wrong L-SIG formate\n");
					pr_debug("ReasonCode: THN-1\n");
				}
			}
		}

		phydm_check_hang_reset(dm);
	}

	atd_t->dbg_step++;
}

void phydm_bb_auto_check_hang_start_n(
	void *dm_void,
	u32 *_used,
	char *output,
	u32 *_out_len)
{
	u32 value32 = 0;
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_auto_dbg_struct *atd_t = &dm->auto_dbg_table;
	u32 used = *_used;
	u32 out_len = *_out_len;

	if (dm->support_ic_type & ODM_IC_11AC_SERIES)
		return;

	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "PHYDM auto check hang (N-series) is started, Please check the system log\n");

	dm->debug_components |= ODM_COMP_API;
	atd_t->auto_dbg_type = AUTO_DBG_CHECK_HANG;
	atd_t->dbg_step = 0;

	phydm_pause_dm_watchdog(dm, PHYDM_PAUSE);

	*_used = used;
	*_out_len = out_len;
}

void phydm_dbg_port_dump_n(void *dm_void, u32 *_used, char *output,
			   u32 *_out_len)
{
	u32 value32 = 0;
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 used = *_used;
	u32 out_len = *_out_len;

	if (dm->support_ic_type & ODM_IC_11AC_SERIES)
		return;

	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "not support now\n");

	*_used = used;
	*_out_len = out_len;
}

#endif

#if (ODM_IC_11AC_SERIES_SUPPORT == 1)
void phydm_dbg_port_dump_ac(void *dm_void, u32 *_used, char *output,
			    u32 *_out_len)
{
	u32 value32 = 0;
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 used = *_used;
	u32 out_len = *_out_len;

	if (dm->support_ic_type & ODM_IC_11N_SERIES)
		return;

	value32 = odm_get_bb_reg(dm, R_0xf80, MASKDWORD);
	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "\r\n %-35s = 0x%x", "rptreg of sc/bw/ht/...", value32);

	if (dm->support_ic_type & ODM_RTL8822B)
		odm_set_bb_reg(dm, R_0x198c, BIT(2) | BIT(1) | BIT(0), 7);

	/* dbg_port = basic state machine */
	{
		odm_set_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0x000);
		value32 = odm_get_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = 0x%x", "0x8fc", value32);

		value32 = odm_get_bb_reg(dm, ODM_REG_RPT_11AC, MASKDWORD);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = 0x%x", "basic state machine", value32);
	}

	/* dbg_port = state machine */
	{
		odm_set_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0x007);
		value32 = odm_get_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = 0x%x", "0x8fc", value32);

		value32 = odm_get_bb_reg(dm, ODM_REG_RPT_11AC, MASKDWORD);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = 0x%x", "state machine", value32);
	}

	/* dbg_port = CCA-related*/
	{
		odm_set_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0x204);
		value32 = odm_get_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = 0x%x", "0x8fc", value32);

		value32 = odm_get_bb_reg(dm, ODM_REG_RPT_11AC, MASKDWORD);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = 0x%x", "CCA-related", value32);
	}

	/* dbg_port = edcca/rxd*/
	{
		odm_set_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0x278);
		value32 = odm_get_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = 0x%x", "0x8fc", value32);

		value32 = odm_get_bb_reg(dm, ODM_REG_RPT_11AC, MASKDWORD);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = 0x%x", "edcca/rxd", value32);
	}

	/* dbg_port = rx_state/mux_state/ADC_MASK_OFDM*/
	{
		odm_set_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0x290);
		value32 = odm_get_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = 0x%x", "0x8fc", value32);

		value32 = odm_get_bb_reg(dm, ODM_REG_RPT_11AC, MASKDWORD);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = 0x%x",
			 "rx_state/mux_state/ADC_MASK_OFDM", value32);
	}

	/* dbg_port = bf-related*/
	{
		odm_set_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0x2B2);
		value32 = odm_get_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = 0x%x", "0x8fc", value32);

		value32 = odm_get_bb_reg(dm, ODM_REG_RPT_11AC, MASKDWORD);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = 0x%x", "bf-related", value32);
	}

	/* dbg_port = bf-related*/
	{
		odm_set_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0x2B8);
		value32 = odm_get_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = 0x%x", "0x8fc", value32);

		value32 = odm_get_bb_reg(dm, ODM_REG_RPT_11AC, MASKDWORD);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = 0x%x", "bf-related", value32);
	}

	/* dbg_port = txon/rxd*/
	{
		odm_set_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0xA03);
		value32 = odm_get_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = 0x%x", "0x8fc", value32);

		value32 = odm_get_bb_reg(dm, ODM_REG_RPT_11AC, MASKDWORD);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = 0x%x", "txon/rxd", value32);
	}

	/* dbg_port = l_rate/l_length*/
	{
		odm_set_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0xA0B);
		value32 = odm_get_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = 0x%x", "0x8fc", value32);

		value32 = odm_get_bb_reg(dm, ODM_REG_RPT_11AC, MASKDWORD);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = 0x%x", "l_rate/l_length", value32);
	}

	/* dbg_port = rxd/rxd_hit*/
	{
		odm_set_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0xA0D);
		value32 = odm_get_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = 0x%x", "0x8fc", value32);

		value32 = odm_get_bb_reg(dm, ODM_REG_RPT_11AC, MASKDWORD);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = 0x%x", "rxd/rxd_hit", value32);
	}

	/* dbg_port = dis_cca*/
	{
		odm_set_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0xAA0);
		value32 = odm_get_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = 0x%x", "0x8fc", value32);

		value32 = odm_get_bb_reg(dm, ODM_REG_RPT_11AC, MASKDWORD);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = 0x%x", "dis_cca", value32);
	}

	/* dbg_port = tx*/
	{
		odm_set_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0xAB0);
		value32 = odm_get_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = 0x%x", "0x8fc", value32);

		value32 = odm_get_bb_reg(dm, ODM_REG_RPT_11AC, MASKDWORD);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = 0x%x", "tx", value32);
	}

	/* dbg_port = rx plcp*/
	{
		odm_set_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0xAD0);
		value32 = odm_get_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = 0x%x", "0x8fc", value32);

		value32 = odm_get_bb_reg(dm, ODM_REG_RPT_11AC, MASKDWORD);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = 0x%x", "rx plcp", value32);

		odm_set_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0xAD1);
		value32 = odm_get_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = 0x%x", "0x8fc", value32);

		value32 = odm_get_bb_reg(dm, ODM_REG_RPT_11AC, MASKDWORD);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = 0x%x", "rx plcp", value32);

		odm_set_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0xAD2);
		value32 = odm_get_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = 0x%x", "0x8fc", value32);

		value32 = odm_get_bb_reg(dm, ODM_REG_RPT_11AC, MASKDWORD);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = 0x%x", "rx plcp", value32);

		odm_set_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0xAD3);
		value32 = odm_get_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = 0x%x", "0x8fc", value32);

		value32 = odm_get_bb_reg(dm, ODM_REG_RPT_11AC, MASKDWORD);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = 0x%x", "rx plcp", value32);
	}
	*_used = used;
	*_out_len = out_len;
}
#endif

#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
void phydm_dbg_port_dump_jgr3(void *dm_void, u32 *_used, char *output,
			      u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 used = *_used;
	u32 out_len = *_out_len;
	/*u32 dbg_port_idx_all[3] = {0x000, 0x001, 0x002};*/
	u32 val = 0;
	u32 dbg_port_idx = 0;
	u32 i = 0;

	if (!(dm->support_ic_type & ODM_IC_JGR3_SERIES))
		return;

	PDM_VAST_SNPF(out_len, used, output + used, out_len - used,
		      "%-17s = %s\n", "DbgPort index", "Value");

#if 0
	/*0x000/0x001/0x002*/
	for (i = 0; i < 3; i++) {
		dbg_port_idx = dbg_port_idx_all[i];
		if (phydm_set_bb_dbg_port(dm, DBGPORT_PRI_3, dbg_port_idx)) {
			val = phydm_get_bb_dbg_port_val(dm);
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "0x%-15x = 0x%x\n", dbg_port_idx, val);
			phydm_release_bb_dbg_port(dm);
		}
	}
#endif
	for (dbg_port_idx = 0x0; dbg_port_idx <= 0xfff; dbg_port_idx++) {
		if (phydm_set_bb_dbg_port(dm, DBGPORT_PRI_3, dbg_port_idx)) {
			val = phydm_get_bb_dbg_port_val(dm);
			PDM_VAST_SNPF(out_len, used, output + used,
				      out_len - used,
				      "0x%-15x = 0x%x\n", dbg_port_idx, val);
			phydm_release_bb_dbg_port(dm);
		}
	}
	*_used = used;
	*_out_len = out_len;
}
#endif

void phydm_dbg_port_dump(void *dm_void, u32 *_used, char *output, u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 used = *_used;
	u32 out_len = *_out_len;

	PDM_VAST_SNPF(out_len, used, output + used, out_len - used,
		      "------ BB debug port start ------\n");

	switch (dm->ic_ip_series) {
	#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
	case PHYDM_IC_JGR3:
		phydm_dbg_port_dump_jgr3(dm, &used, output, &out_len);
		break;
	#endif

	#if (ODM_IC_11AC_SERIES_SUPPORT == 1)
	case PHYDM_IC_AC:
		phydm_dbg_port_dump_ac(dm, &used, output, &out_len);
		break;
	#endif

	#if (ODM_IC_11N_SERIES_SUPPORT == 1)
	case PHYDM_IC_N:
		phydm_dbg_port_dump_n(dm, &used, output, &out_len);
		break;
	#endif

	default:
		break;
	}
	*_used = used;
	*_out_len = out_len;
}

void phydm_auto_dbg_console(
	void *dm_void,
	char input[][16],
	u32 *_used,
	char *output,
	u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	char help[] = "-h";
	u32 var1[10] = {0};
	u32 used = *_used;
	u32 out_len = *_out_len;

	PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);

	if ((strcmp(input[1], help) == 0)) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "hang: {1} {1:Show DbgPort, 2:Auto check hang}\n");
		return;
	} else if (var1[0] == 1) {
		PHYDM_SSCANF(input[2], DCMD_DECIMAL, &var1[1]);
		if (var1[1] == 1) {
			phydm_dbg_port_dump(dm, &used, output, &out_len);
		} else if (var1[1] == 2) {
			if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
				PDM_SNPF(out_len, used, output + used,
					 out_len - used, "Not support\n");
			} else {
				#if (ODM_IC_11N_SERIES_SUPPORT == 1)
				phydm_bb_auto_check_hang_start_n(dm, &used,
								 output,
								 &out_len);
				#else
				PDM_SNPF(out_len, used, output + used,
					 out_len - used, "Not support\n");
				#endif
			}
		}
	}

	*_used = used;
	*_out_len = out_len;
}

void phydm_auto_dbg_engine(void *dm_void)
{
	u32 value32 = 0;

	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_auto_dbg_struct *atd_t = &dm->auto_dbg_table;

	if (atd_t->auto_dbg_type == AUTO_DBG_STOP)
		return;

	pr_debug("%s ======>\n", __func__);

	if (atd_t->auto_dbg_type == AUTO_DBG_CHECK_HANG) {
		if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
			pr_debug("Not Support\n");
		} else {
			#if (ODM_IC_11N_SERIES_SUPPORT == 1)
			phydm_auto_check_hang_engine_n(dm);
			#else
			pr_debug("Not Support\n");
			#endif
		}

	} else if (atd_t->auto_dbg_type == AUTO_DBG_CHECK_RA) {
		pr_debug("Not Support\n");
	}
}

void phydm_auto_dbg_engine_init(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_auto_dbg_struct *atd_t = &dm->auto_dbg_table;
	u16 dbg_port_table[DBGPORT_CHK_NUM] = {0x0, 0x803, 0x208, 0xab0,
					       0xab1, 0xab2};

	PHYDM_DBG(dm, ODM_COMP_API, "%s ======>n", __func__);

	odm_move_memory(dm, &atd_t->dbg_port_table[0],
			&dbg_port_table[0], (DBGPORT_CHK_NUM * 2));

	phydm_check_hang_init(dm);
}
#endif
