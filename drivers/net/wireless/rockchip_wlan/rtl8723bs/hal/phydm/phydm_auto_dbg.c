/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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

/* ************************************************************
 * include files
 * ************************************************************ */

#include "mp_precomp.h"
#include "phydm_precomp.h"

#ifdef PHYDM_AUTO_DEGBUG

void
phydm_check_hang_reset(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_auto_dbg_struc	*p_atd_t = &(p_dm->auto_dbg_table);

	dbg_print("%s ======>\n", __func__);

	p_atd_t->dbg_step = 0;
	p_atd_t->auto_dbg_type = AUTO_DBG_STOP;
	phydm_pause_dm_watchdog(p_dm, PHYDM_RESUME);
	p_dm->debug_components &= (~ODM_COMP_API);
}

#if (ODM_IC_11N_SERIES_SUPPORT == 1)
void
phydm_auto_check_hang_engine_n(
	void			*p_dm_void
)
{
	struct	PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct	phydm_auto_dbg_struc	*p_atd_t = &(p_dm->auto_dbg_table);
	struct	n_dbgport_803	dbgport_803 = {0};
	u32		value32_tmp = 0, value32_tmp_2 = 0;
	u8		i;
	u32		curr_dbg_port_val[DBGPORT_CHK_NUM];
	u16		curr_ofdm_t_cnt;
	u16		curr_ofdm_r_cnt;
	u16		curr_cck_t_cnt;
	u16		curr_cck_r_cnt;
	u16		curr_ofdm_crc_error_cnt;
	u16		curr_cck_crc_error_cnt;
	u16		diff_ofdm_t_cnt;
	u16		diff_ofdm_r_cnt;
	u16		diff_cck_t_cnt;
	u16		diff_cck_r_cnt;
	u16		diff_ofdm_crc_error_cnt;
	u16		diff_cck_crc_error_cnt;
	u8		rf_mode;


	if (p_atd_t->auto_dbg_type == AUTO_DBG_STOP)
		return;

	if (p_dm->support_ic_type & ODM_IC_11AC_SERIES) {

		phydm_check_hang_reset(p_dm);
		return;
	}

	if (p_atd_t->dbg_step == 0) {

		dbg_print("dbg_step=0\n\n");
		
		/*Reset all packet counter*/
		odm_set_bb_reg(p_dm, 0xf14, BIT(16), 1);
		odm_set_bb_reg(p_dm, 0xf14, BIT(16), 0);



	} else if (p_atd_t->dbg_step == 1)  {

		dbg_print("dbg_step=1\n\n");

		/*Check packet counter Register*/
		p_atd_t->ofdm_t_cnt = (u16)odm_get_bb_reg(p_dm, 0x9cc, MASKHWORD);
		p_atd_t->ofdm_r_cnt = (u16)odm_get_bb_reg(p_dm, 0xf94, MASKLWORD);
		p_atd_t->ofdm_crc_error_cnt = (u16)odm_get_bb_reg(p_dm, 0xf94, MASKHWORD);
		
		p_atd_t->cck_t_cnt = (u16)odm_get_bb_reg(p_dm, 0x9d0, MASKHWORD);;
		p_atd_t->cck_r_cnt = (u16)odm_get_bb_reg(p_dm, 0xfa0, MASKHWORD);
		p_atd_t->cck_crc_error_cnt = (u16)odm_get_bb_reg(p_dm, 0xf84, 0x3fff);	


		/*Check Debug Port*/
		for (i = 0; i < DBGPORT_CHK_NUM; i++) {
			
			if (phydm_set_bb_dbg_port(p_dm, BB_DBGPORT_PRIORITY_3, (u32)p_atd_t->dbg_port_table[i])) {
				p_atd_t->dbg_port_val[i] = phydm_get_bb_dbg_port_value(p_dm);
				phydm_release_bb_dbg_port(p_dm);
			}
		}
	
	} else if (p_atd_t->dbg_step == 2)  {

		dbg_print("dbg_step=2\n\n");

		/*Check packet counter Register*/
		curr_ofdm_t_cnt = (u16)odm_get_bb_reg(p_dm, 0x9cc, MASKHWORD);
		curr_ofdm_r_cnt = (u16)odm_get_bb_reg(p_dm, 0xf94, MASKLWORD);
		curr_ofdm_crc_error_cnt = (u16)odm_get_bb_reg(p_dm, 0xf94, MASKHWORD);
		
		curr_cck_t_cnt = (u16)odm_get_bb_reg(p_dm, 0x9d0, MASKHWORD);;
		curr_cck_r_cnt = (u16)odm_get_bb_reg(p_dm, 0xfa0, MASKHWORD);
		curr_cck_crc_error_cnt = (u16)odm_get_bb_reg(p_dm, 0xf84, 0x3fff);	

		/*Check Debug Port*/
		for (i = 0; i < DBGPORT_CHK_NUM; i++) {
			
			if (phydm_set_bb_dbg_port(p_dm, BB_DBGPORT_PRIORITY_3, (u32)p_atd_t->dbg_port_table[i])) {
				curr_dbg_port_val[i] = phydm_get_bb_dbg_port_value(p_dm);
				phydm_release_bb_dbg_port(p_dm);
			}
		}
	
		/*=== Make check hang decision ================================*/
		dbg_print("Check Hang Decision\n\n");

		/* ----- Check RF Register -----------------------------------*/
		for (i = 0; i < p_dm->num_rf_path; i++) {
		
			rf_mode = (u8)odm_get_rf_reg(p_dm, i, 0x0, 0xf0000);
				
			dbg_print("RF0x0[%d] = 0x%x\n", i, rf_mode);

			if (rf_mode > 3) {
				dbg_print("Incorrect RF mode\n");
				dbg_print("ReasonCode:RHN-1\n");

				
			}
		}

		value32_tmp = odm_get_rf_reg(p_dm, 0, 0xb0, 0xf0000);
			
		if (p_dm->support_ic_type == ODM_RTL8188E) {
			if (value32_tmp != 0xff8c8) {
				dbg_print("ReasonCode:RHN-3\n");
			}
		}

		/* ----- Check BB Register -----------------------------------*/
		
		/*BB mode table*/
		value32_tmp = odm_get_bb_reg(p_dm, 0x824, 0xe);
		value32_tmp_2 = odm_get_bb_reg(p_dm, 0x82c, 0xe);
		dbg_print("BB TX mode table {A, B}= {%d, %d}\n", value32_tmp, value32_tmp_2);

		if ((value32_tmp > 3) || (value32_tmp_2 > 3)) {
			
			dbg_print("ReasonCode:RHN-2\n");
		}

		value32_tmp = odm_get_bb_reg(p_dm, 0x824, 0x700000);
		value32_tmp_2 = odm_get_bb_reg(p_dm, 0x82c, 0x700000);
		dbg_print("BB RX mode table {A, B}= {%d, %d}\n", value32_tmp, value32_tmp_2);

		if ((value32_tmp > 3) || (value32_tmp_2 > 3)) {
			
			dbg_print("ReasonCode:RHN-2\n");
		}
		

		/*BB HW Block*/
		value32_tmp = odm_get_bb_reg(p_dm, 0x800, MASKDWORD);
		
		if (!(value32_tmp & BIT(24))) {
			dbg_print("Reg0x800[24] = 0, CCK BLK is disabled\n");
			dbg_print("ReasonCode: THN-3\n");
		}
		
		if (!(value32_tmp & BIT(25))) {
			dbg_print("Reg0x800[24] = 0, OFDM BLK is disabled\n");
			dbg_print("ReasonCode:THN-3\n");
		}

		/*BB Continue TX*/
		value32_tmp = odm_get_bb_reg(p_dm, 0xd00, 0x70000000);
		dbg_print("Continue TX=%d\n", value32_tmp);
		if (value32_tmp != 0) {
			dbg_print("ReasonCode: THN-4\n");
		}
		

		/* ----- Check Packet Counter --------------------------------*/
		diff_ofdm_t_cnt = curr_ofdm_t_cnt - p_atd_t->ofdm_t_cnt;
		diff_ofdm_r_cnt = curr_ofdm_r_cnt - p_atd_t->ofdm_r_cnt;
		diff_ofdm_crc_error_cnt = curr_ofdm_crc_error_cnt - p_atd_t->ofdm_crc_error_cnt;
		
		diff_cck_t_cnt = curr_cck_t_cnt - p_atd_t->cck_t_cnt;
		diff_cck_r_cnt = curr_cck_r_cnt - p_atd_t->cck_r_cnt;
		diff_cck_crc_error_cnt = curr_cck_crc_error_cnt - p_atd_t->cck_crc_error_cnt;

		dbg_print("OFDM[t=0~1] {TX, RX, CRC_error} = {%d, %d, %d}\n", 
			p_atd_t->ofdm_t_cnt, p_atd_t->ofdm_r_cnt, p_atd_t->ofdm_crc_error_cnt);
		dbg_print("OFDM[t=1~2] {TX, RX, CRC_error} = {%d, %d, %d}\n", 
			curr_ofdm_t_cnt, curr_ofdm_r_cnt, curr_ofdm_crc_error_cnt);
		dbg_print("OFDM_diff {TX, RX, CRC_error} = {%d, %d, %d}\n", 
			diff_ofdm_t_cnt, diff_ofdm_r_cnt, diff_ofdm_crc_error_cnt);

		dbg_print("CCK[t=0~1] {TX, RX, CRC_error} = {%d, %d, %d}\n", 
			p_atd_t->cck_t_cnt, p_atd_t->cck_r_cnt, p_atd_t->cck_crc_error_cnt);
		dbg_print("CCK[t=1~2] {TX, RX, CRC_error} = {%d, %d, %d}\n", 
			curr_cck_t_cnt, curr_cck_r_cnt, curr_cck_crc_error_cnt);
		dbg_print("CCK_diff {TX, RX, CRC_error} = {%d, %d, %d}\n", 
			diff_cck_t_cnt, diff_cck_r_cnt, diff_cck_crc_error_cnt);

		/* ----- Check Dbg Port --------------------------------*/

		for (i = 0; i < DBGPORT_CHK_NUM; i++) {

			dbg_print("Dbg_port=((0x%x))\n", p_atd_t->dbg_port_table[i]);
			dbg_print("Val{pre, curr}={0x%x, 0x%x}\n", p_atd_t->dbg_port_val[i], curr_dbg_port_val[i]);

			if ((p_atd_t->dbg_port_table[i]) == 0) {

				if (p_atd_t->dbg_port_val[i] == curr_dbg_port_val[i]) {
					
					dbg_print("BB state hang\n");
					dbg_print("ReasonCode:\n");
				}

			} else if (p_atd_t->dbg_port_table[i] == 0x803) {

				if (p_atd_t->dbg_port_val[i] == curr_dbg_port_val[i]) {

					//dbgport_803 = (struct n_dbgport_803 )(p_atd_t->dbg_port_val[i]);

					odm_move_memory(p_dm, &(dbgport_803),  &(p_atd_t->dbg_port_val[i]), sizeof(struct n_dbgport_803));

					dbg_print("RSTB{BB, GLB, OFDM}={%d, %d, %d}\n", dbgport_803.bb_rst_b, dbgport_803.glb_rst_b, dbgport_803.ofdm_rst_b);
					dbg_print("{ofdm_tx_en, cck_tx_en, phy_tx_on}={%d, %d, %d}\n", dbgport_803.ofdm_tx_en, dbgport_803.cck_tx_en, dbgport_803.phy_tx_on);
					dbg_print("CCA_PP{OFDM, CCK}={%d, %d}\n", dbgport_803.ofdm_cca_pp, dbgport_803.cck_cca_pp);

					if (dbgport_803.phy_tx_on)
						dbg_print("Maybe TX Hang\n");
					else if (dbgport_803.ofdm_cca_pp || dbgport_803.cck_cca_pp)
						dbg_print("Maybe RX Hang\n");	
				}

			} else if (p_atd_t->dbg_port_table[i] == 0x208) {

				if ((p_atd_t->dbg_port_val[i] & BIT(30)) && (curr_dbg_port_val[i] & BIT(30))) {
					
					dbg_print("EDCCA Pause TX\n");
					dbg_print("ReasonCode: THN-2\n");
				}

			} else if (p_atd_t->dbg_port_table[i] == 0xab0) {

				if (((p_atd_t->dbg_port_val[i] & 0xffffff) == 0) || 
					((curr_dbg_port_val[i] & 0xffffff) == 0)) {
					
					dbg_print("Wrong L-SIG formate\n");
					dbg_print("ReasonCode: THN-1\n");
				}
			}
		}
		
		phydm_check_hang_reset(p_dm);
	}

	p_atd_t->dbg_step++;
	
}

void
phydm_bb_auto_check_hang_start_n(
	void			*p_dm_void,
	u32			*_used,
	char			*output,
	u32			*_out_len
)
{
	u32	value32 = 0;
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_auto_dbg_struc	*p_atd_t = &(p_dm->auto_dbg_table);
	u32 used = *_used;
	u32 out_len = *_out_len;

	if (p_dm->support_ic_type & ODM_IC_11AC_SERIES)
		return;

	PHYDM_SNPRINTF((output + used, out_len - used, 
		"PHYDM auto check hang (N-series) is started, Please check the system log\n"));

	p_dm->debug_components |= ODM_COMP_API;
	p_atd_t->auto_dbg_type = AUTO_DBG_CHECK_HANG;
	p_atd_t->dbg_step = 0;
	

	phydm_pause_dm_watchdog(p_dm, PHYDM_PAUSE);


	
	*_used = used;
	*_out_len = out_len;
}

void
phydm_bb_rx_hang_info_n(
	void			*p_dm_void,
	u32			*_used,
	char			*output,
	u32			*_out_len
)
{
	u32	value32 = 0;
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32 used = *_used;
	u32 out_len = *_out_len;

	if (p_dm->support_ic_type & ODM_IC_11AC_SERIES)
		return;

	PHYDM_SNPRINTF((output + used, out_len - used, "not support now\n"));

	*_used = used;
	*_out_len = out_len;
}

#endif	

#if (ODM_IC_11AC_SERIES_SUPPORT == 1)
void
phydm_bb_rx_hang_info_ac(
	void			*p_dm_void,
	u32			*_used,
	char			*output,
	u32			*_out_len
)
{
	u32	value32 = 0;
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32 used = *_used;
	u32 out_len = *_out_len;

	if (p_dm->support_ic_type & ODM_IC_11N_SERIES)
		return;

	value32 = odm_get_bb_reg(p_dm, 0xF80, MASKDWORD);
	PHYDM_SNPRINTF((output + used, out_len - used,  "\r\n %-35s = 0x%x", "rptreg of sc/bw/ht/...", value32));

	if (p_dm->support_ic_type & ODM_RTL8822B)
		odm_set_bb_reg(p_dm, 0x198c, BIT(2) | BIT(1) | BIT(0), 7);

	/* dbg_port = basic state machine */
	{
		odm_set_bb_reg(p_dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0x000);
		value32 = odm_get_bb_reg(p_dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = odm_get_bb_reg(p_dm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "basic state machine", value32));
	}

	/* dbg_port = state machine */
	{
		odm_set_bb_reg(p_dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0x007);
		value32 = odm_get_bb_reg(p_dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = odm_get_bb_reg(p_dm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "state machine", value32));
	}

	/* dbg_port = CCA-related*/
	{
		odm_set_bb_reg(p_dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0x204);
		value32 = odm_get_bb_reg(p_dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = odm_get_bb_reg(p_dm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "CCA-related", value32));
	}


	/* dbg_port = edcca/rxd*/
	{
		odm_set_bb_reg(p_dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0x278);
		value32 = odm_get_bb_reg(p_dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = odm_get_bb_reg(p_dm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "edcca/rxd", value32));
	}

	/* dbg_port = rx_state/mux_state/ADC_MASK_OFDM*/
	{
		odm_set_bb_reg(p_dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0x290);
		value32 = odm_get_bb_reg(p_dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = odm_get_bb_reg(p_dm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "rx_state/mux_state/ADC_MASK_OFDM", value32));
	}

	/* dbg_port = bf-related*/
	{
		odm_set_bb_reg(p_dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0x2B2);
		value32 = odm_get_bb_reg(p_dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = odm_get_bb_reg(p_dm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "bf-related", value32));
	}

	/* dbg_port = bf-related*/
	{
		odm_set_bb_reg(p_dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0x2B8);
		value32 = odm_get_bb_reg(p_dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = odm_get_bb_reg(p_dm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "bf-related", value32));
	}

	/* dbg_port = txon/rxd*/
	{
		odm_set_bb_reg(p_dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0xA03);
		value32 = odm_get_bb_reg(p_dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = odm_get_bb_reg(p_dm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "txon/rxd", value32));
	}

	/* dbg_port = l_rate/l_length*/
	{
		odm_set_bb_reg(p_dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0xA0B);
		value32 = odm_get_bb_reg(p_dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = odm_get_bb_reg(p_dm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "l_rate/l_length", value32));
	}

	/* dbg_port = rxd/rxd_hit*/
	{
		odm_set_bb_reg(p_dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0xA0D);
		value32 = odm_get_bb_reg(p_dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = odm_get_bb_reg(p_dm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "rxd/rxd_hit", value32));
	}

	/* dbg_port = dis_cca*/
	{
		odm_set_bb_reg(p_dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0xAA0);
		value32 = odm_get_bb_reg(p_dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = odm_get_bb_reg(p_dm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "dis_cca", value32));
	}


	/* dbg_port = tx*/
	{
		odm_set_bb_reg(p_dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0xAB0);
		value32 = odm_get_bb_reg(p_dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = odm_get_bb_reg(p_dm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "tx", value32));
	}

	/* dbg_port = rx plcp*/
	{
		odm_set_bb_reg(p_dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0xAD0);
		value32 = odm_get_bb_reg(p_dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = odm_get_bb_reg(p_dm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "rx plcp", value32));

		odm_set_bb_reg(p_dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0xAD1);
		value32 = odm_get_bb_reg(p_dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = odm_get_bb_reg(p_dm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "rx plcp", value32));

		odm_set_bb_reg(p_dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0xAD2);
		value32 = odm_get_bb_reg(p_dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = odm_get_bb_reg(p_dm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "rx plcp", value32));

		odm_set_bb_reg(p_dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0xAD3);
		value32 = odm_get_bb_reg(p_dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = odm_get_bb_reg(p_dm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "rx plcp", value32));
	}
	*_used = used;
	*_out_len = out_len;
}
#endif

void
phydm_auto_dbg_console(
	void		*p_dm_void,
	char		input[][16],
	u32		*_used,
	char		*output,
	u32		*_out_len,
	u32		input_num
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	char		help[] = "-h";
	u32		var1[10] = {0};
	u32		used = *_used;
	u32		out_len = *_out_len;


	PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);

	if ((strcmp(input[1], help) == 0)) {
		PHYDM_SNPRINTF((output + used, out_len - used, "Show dbg port: {1} {1}\n"));
		PHYDM_SNPRINTF((output + used, out_len - used, "Auto check hang: {1} {2}\n"));
		return;
	} else if (var1[0] == 1) {

		PHYDM_SSCANF(input[2], DCMD_DECIMAL, &var1[1]);

		if (var1[1] == 1) {
			if (p_dm->support_ic_type & ODM_IC_11AC_SERIES) {
				#if (ODM_IC_11AC_SERIES_SUPPORT == 1)
				phydm_bb_rx_hang_info_ac(p_dm, &used, output, &out_len);
				#else
				PHYDM_SNPRINTF((output + used, out_len - used, "Not support\n"));
				#endif
			} else {
				#if (ODM_IC_11N_SERIES_SUPPORT == 1)
				phydm_bb_rx_hang_info_n(p_dm, &used, output, &out_len);
				#else
				PHYDM_SNPRINTF((output + used, out_len - used, "Not support\n"));
				#endif
			}
		} else if (var1[1] == 2) {
		
			if (p_dm->support_ic_type & ODM_IC_11AC_SERIES) {
				PHYDM_SNPRINTF((output + used, out_len - used, "Not support\n"));
			} else {
				#if (ODM_IC_11N_SERIES_SUPPORT == 1)
				phydm_bb_auto_check_hang_start_n(p_dm, &used, output, &out_len);
				#else
				PHYDM_SNPRINTF((output + used, out_len - used, "Not support\n"));
				#endif
			}
		}
	} 

	*_used = used;
	*_out_len = out_len;
}


#endif

void
phydm_auto_dbg_engine(
	void			*p_dm_void
)
{
#ifdef PHYDM_AUTO_DEGBUG
	u32	value32 = 0;
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_auto_dbg_struc	*p_atd_t = &(p_dm->auto_dbg_table);

	if (p_atd_t->auto_dbg_type == AUTO_DBG_STOP)
		return;

	dbg_print("%s ======>\n", __func__);
	
	if (p_atd_t->auto_dbg_type == AUTO_DBG_CHECK_HANG) {

		if (p_dm->support_ic_type & ODM_IC_11AC_SERIES) {
			dbg_print("Not Support\n");
		} else {
			#if (ODM_IC_11N_SERIES_SUPPORT == 1)
			phydm_auto_check_hang_engine_n(p_dm);
			#else
			dbg_print("Not Support\n");
			#endif
		}

	} else if (p_atd_t->auto_dbg_type == AUTO_DBG_CHECK_RA) {
	
		dbg_print("Not Support\n");

	}
#endif
}

void
phydm_auto_dbg_engine_init(
	void		*p_dm_void
)
{
#ifdef PHYDM_AUTO_DEGBUG
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_auto_dbg_struc	*p_atd_t = &(p_dm->auto_dbg_table);
	u16 dbg_port_table[DBGPORT_CHK_NUM] = {0x0, 0x803, 0x208, 0xab0, 0xab1, 0xab2};

	PHYDM_DBG(p_dm, ODM_COMP_API, ("%s ======>n", __func__));

	odm_move_memory(p_dm, &(p_atd_t->dbg_port_table[0]),  &(dbg_port_table[0]), (DBGPORT_CHK_NUM * 2));

	phydm_check_hang_reset(p_dm);
#endif
}


