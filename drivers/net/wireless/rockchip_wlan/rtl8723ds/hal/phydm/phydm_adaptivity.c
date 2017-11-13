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

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	#if WPP_SOFTWARE_TRACE
		#include "PhyDM_Adaptivity.tmh"
	#endif
#endif

void
phydm_dig_up_bound_lmt_en(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_adaptivity_struct	*p_adaptivity = (struct phydm_adaptivity_struct *)phydm_get_structure(p_dm, PHYDM_ADAPTIVITY);

	if (!(p_dm->support_ability & ODM_BB_ADAPTIVITY) ||
		(p_dm->adaptivity_flag == false) ||
		(!p_dm->is_linked) ||
		(p_dm->adaptivity_enable == false)
	) {
		p_adaptivity->igi_up_bound_lmt_cnt = 0;
		p_adaptivity->igi_lmt_en = false;	
		return;
	}

	if (p_dm->total_tp > 1) {
		p_adaptivity->igi_lmt_en = true;			
		p_adaptivity->igi_up_bound_lmt_cnt = p_adaptivity->igi_up_bound_lmt_val;
		PHYDM_DBG(p_dm, DBG_ADPTVTY,
			("TP >1, Start limit IGI upper bound\n"));
	} else {
		if (p_adaptivity->igi_up_bound_lmt_cnt == 0)
			p_adaptivity->igi_lmt_en = false;
		else
			p_adaptivity->igi_up_bound_lmt_cnt--;
	}

	PHYDM_DBG(p_dm, DBG_ADPTVTY, ("IGI_lmt_cnt = %d\n", p_adaptivity->igi_up_bound_lmt_cnt));
}

void
phydm_check_adaptivity(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_adaptivity_struct	*adaptivity = (struct phydm_adaptivity_struct *)phydm_get_structure(p_dm, PHYDM_ADAPTIVITY);

	if (!(p_dm->support_ability & ODM_BB_ADAPTIVITY)) {
		p_dm->adaptivity_enable = false;
		return;
	}
	
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	if (p_dm->ap_total_num > adaptivity->ap_num_th) {
		p_dm->adaptivity_enable = false;
		PHYDM_DBG(p_dm, DBG_ADPTVTY, ("AP total num > %d!!, disable adaptivity\n", adaptivity->ap_num_th));
		return;
	}
#elif (DM_ODM_SUPPORT_TYPE == ODM_AP)
	if (adaptivity->dynamic_link_adaptivity) {
		if (p_dm->is_linked && adaptivity->is_check == false) {
			phydm_check_environment(p_dm);
		} else if (!p_dm->is_linked)
			adaptivity->is_check = false;

		return;
	}
#endif
	
	p_dm->adaptivity_enable = true;
}

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
boolean
phydm_check_channel_plan(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _ADAPTER		*p_adapter	= p_dm->adapter;
	PMGNT_INFO		p_mgnt_info = &(p_adapter->MgntInfo);

	if (p_mgnt_info->RegEnableAdaptivity == 2) {
		if (p_dm->carrier_sense_enable == false) {		/*check domain Code for adaptivity or CarrierSense*/
			if ((*p_dm->p_band_type == ODM_BAND_5G) &&
				!(p_dm->odm_regulation_5g == REGULATION_ETSI || p_dm->odm_regulation_5g == REGULATION_WW)) {
				PHYDM_DBG(p_dm, DBG_ADPTVTY, ("adaptivity skip 5G domain code : %d\n", p_dm->odm_regulation_5g));
				p_dm->adaptivity_enable = false;
				return true;
			} else if ((*p_dm->p_band_type == ODM_BAND_2_4G) &&
				!(p_dm->odm_regulation_2_4g == REGULATION_ETSI || p_dm->odm_regulation_2_4g == REGULATION_WW)) {
				PHYDM_DBG(p_dm, DBG_ADPTVTY, ("adaptivity skip 2.4G domain code : %d\n", p_dm->odm_regulation_2_4g));
				p_dm->adaptivity_enable = false;
				return true;

			} else if ((*p_dm->p_band_type != ODM_BAND_2_4G) && (*p_dm->p_band_type != ODM_BAND_5G)) {
				PHYDM_DBG(p_dm, DBG_ADPTVTY, ("adaptivity neither 2G nor 5G band, return\n"));
				p_dm->adaptivity_enable = false;
				return true;
			}
		} else {
			if ((*p_dm->p_band_type == ODM_BAND_5G) &&
				!(p_dm->odm_regulation_5g == REGULATION_MKK || p_dm->odm_regulation_5g == REGULATION_WW)) {
				PHYDM_DBG(p_dm, DBG_ADPTVTY, ("CarrierSense skip 5G domain code : %d\n", p_dm->odm_regulation_5g));
				p_dm->adaptivity_enable = false;
				return true;
			}

			else if ((*p_dm->p_band_type == ODM_BAND_2_4G) &&
				!(p_dm->odm_regulation_2_4g == REGULATION_MKK  || p_dm->odm_regulation_2_4g == REGULATION_WW)) {
				PHYDM_DBG(p_dm, DBG_ADPTVTY, ("CarrierSense skip 2.4G domain code : %d\n", p_dm->odm_regulation_2_4g));
				p_dm->adaptivity_enable = false;
				return true;

			} else if ((*p_dm->p_band_type != ODM_BAND_2_4G) && (*p_dm->p_band_type != ODM_BAND_5G)) {
				PHYDM_DBG(p_dm, DBG_ADPTVTY, ("CarrierSense neither 2G nor 5G band, return\n"));
				p_dm->adaptivity_enable = false;
				return true;
			}
		}
	}

	return false;

}
#endif

void
phydm_set_edcca_threshold(
	void	*p_dm_void,
	s8	H2L,
	s8	L2H
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	if (p_dm->support_ic_type & ODM_IC_11N_SERIES)
		odm_set_bb_reg(p_dm, REG_OFDM_0_ECCA_THRESHOLD, MASKBYTE2 | MASKBYTE0, (u32)((u8)L2H | (u8)H2L << 16));
#if (RTL8195A_SUPPORT == 0)
	else if (p_dm->support_ic_type & ODM_IC_11AC_SERIES)
		odm_set_bb_reg(p_dm, REG_FPGA0_XB_LSSI_READ_BACK, MASKLWORD, (u16)((u8)L2H | (u8)H2L << 8));
#endif

}

void
phydm_set_lna(
	void				*p_dm_void,
	enum phydm_set_lna	type
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	if (p_dm->support_ic_type & (ODM_RTL8188E | ODM_RTL8192E)) {
		if (type == phydm_disable_lna) {
			odm_set_rf_reg(p_dm, RF_PATH_A, 0xef, 0x80000, 0x1);
			odm_set_rf_reg(p_dm, RF_PATH_A, 0x30, 0xfffff, 0x18000);	/*select Rx mode*/
			odm_set_rf_reg(p_dm, RF_PATH_A, 0x31, 0xfffff, 0x0000f);
			odm_set_rf_reg(p_dm, RF_PATH_A, 0x32, 0xfffff, 0x37f82);	/*disable LNA*/
			odm_set_rf_reg(p_dm, RF_PATH_A, 0xef, 0x80000, 0x0);
			if (p_dm->rf_type > RF_1T1R) {
				odm_set_rf_reg(p_dm, RF_PATH_B, 0xef, 0x80000, 0x1);
				odm_set_rf_reg(p_dm, RF_PATH_B, 0x30, 0xfffff, 0x18000);
				odm_set_rf_reg(p_dm, RF_PATH_B, 0x31, 0xfffff, 0x0000f);
				odm_set_rf_reg(p_dm, RF_PATH_B, 0x32, 0xfffff, 0x37f82);
				odm_set_rf_reg(p_dm, RF_PATH_B, 0xef, 0x80000, 0x0);
			}
		} else if (type == phydm_enable_lna) {
			odm_set_rf_reg(p_dm, RF_PATH_A, 0xef, 0x80000, 0x1);
			odm_set_rf_reg(p_dm, RF_PATH_A, 0x30, 0xfffff, 0x18000);	/*select Rx mode*/
			odm_set_rf_reg(p_dm, RF_PATH_A, 0x31, 0xfffff, 0x0000f);
			odm_set_rf_reg(p_dm, RF_PATH_A, 0x32, 0xfffff, 0x77f82);	/*back to normal*/
			odm_set_rf_reg(p_dm, RF_PATH_A, 0xef, 0x80000, 0x0);
			if (p_dm->rf_type > RF_1T1R) {
				odm_set_rf_reg(p_dm, RF_PATH_B, 0xef, 0x80000, 0x1);
				odm_set_rf_reg(p_dm, RF_PATH_B, 0x30, 0xfffff, 0x18000);
				odm_set_rf_reg(p_dm, RF_PATH_B, 0x31, 0xfffff, 0x0000f);
				odm_set_rf_reg(p_dm, RF_PATH_B, 0x32, 0xfffff, 0x77f82);
				odm_set_rf_reg(p_dm, RF_PATH_B, 0xef, 0x80000, 0x0);
			}
		}
	} else if (p_dm->support_ic_type & ODM_RTL8723B) {
		if (type == phydm_disable_lna) {
			/*S0*/
			odm_set_rf_reg(p_dm, RF_PATH_A, 0xef, 0x80000, 0x1);
			odm_set_rf_reg(p_dm, RF_PATH_A, 0x30, 0xfffff, 0x18000);	/*select Rx mode*/
			odm_set_rf_reg(p_dm, RF_PATH_A, 0x31, 0xfffff, 0x0001f);
			odm_set_rf_reg(p_dm, RF_PATH_A, 0x32, 0xfffff, 0xe6137);	/*disable LNA*/
			odm_set_rf_reg(p_dm, RF_PATH_A, 0xef, 0x80000, 0x0);
			/*S1*/
			odm_set_rf_reg(p_dm, RF_PATH_A, 0xed, 0x00020, 0x1);
			odm_set_rf_reg(p_dm, RF_PATH_A, 0x43, 0xfffff, 0x3008d);	/*select Rx mode and disable LNA*/
			odm_set_rf_reg(p_dm, RF_PATH_A, 0xed, 0x00020, 0x0);
		} else if (type == phydm_enable_lna) {
			/*S0*/
			odm_set_rf_reg(p_dm, RF_PATH_A, 0xef, 0x80000, 0x1);
			odm_set_rf_reg(p_dm, RF_PATH_A, 0x30, 0xfffff, 0x18000);	/*select Rx mode*/
			odm_set_rf_reg(p_dm, RF_PATH_A, 0x31, 0xfffff, 0x0001f);
			odm_set_rf_reg(p_dm, RF_PATH_A, 0x32, 0xfffff, 0xe6177);	/*disable LNA*/
			odm_set_rf_reg(p_dm, RF_PATH_A, 0xef, 0x80000, 0x0);
			/*S1*/
			odm_set_rf_reg(p_dm, RF_PATH_A, 0xed, 0x00020, 0x1);
			odm_set_rf_reg(p_dm, RF_PATH_A, 0x43, 0xfffff, 0x300bd);	/*select Rx mode and disable LNA*/
			odm_set_rf_reg(p_dm, RF_PATH_A, 0xed, 0x00020, 0x0);
		}

	} else if (p_dm->support_ic_type & ODM_RTL8812) {
		if (type == phydm_disable_lna) {
			odm_set_rf_reg(p_dm, RF_PATH_A, 0xef, 0x80000, 0x1);
			odm_set_rf_reg(p_dm, RF_PATH_A, 0x30, 0xfffff, 0x18000);	/*select Rx mode*/
			odm_set_rf_reg(p_dm, RF_PATH_A, 0x31, 0xfffff, 0x3f7ff);
			odm_set_rf_reg(p_dm, RF_PATH_A, 0x32, 0xfffff, 0xc22bf);	/*disable LNA*/
			odm_set_rf_reg(p_dm, RF_PATH_A, 0xef, 0x80000, 0x0);
			if (p_dm->rf_type > RF_1T1R) {
				odm_set_rf_reg(p_dm, RF_PATH_B, 0xef, 0x80000, 0x1);
				odm_set_rf_reg(p_dm, RF_PATH_B, 0x30, 0xfffff, 0x18000);	/*select Rx mode*/
				odm_set_rf_reg(p_dm, RF_PATH_B, 0x31, 0xfffff, 0x3f7ff);
				odm_set_rf_reg(p_dm, RF_PATH_B, 0x32, 0xfffff, 0xc22bf);	/*disable LNA*/
				odm_set_rf_reg(p_dm, RF_PATH_B, 0xef, 0x80000, 0x0);
			}
		} else if (type == phydm_enable_lna) {
			odm_set_rf_reg(p_dm, RF_PATH_A, 0xef, 0x80000, 0x1);
			odm_set_rf_reg(p_dm, RF_PATH_A, 0x30, 0xfffff, 0x18000);	/*select Rx mode*/
			odm_set_rf_reg(p_dm, RF_PATH_A, 0x31, 0xfffff, 0x3f7ff);
			odm_set_rf_reg(p_dm, RF_PATH_A, 0x32, 0xfffff, 0xc26bf);	/*disable LNA*/
			odm_set_rf_reg(p_dm, RF_PATH_A, 0xef, 0x80000, 0x0);
			if (p_dm->rf_type > RF_1T1R) {
				odm_set_rf_reg(p_dm, RF_PATH_B, 0xef, 0x80000, 0x1);
				odm_set_rf_reg(p_dm, RF_PATH_B, 0x30, 0xfffff, 0x18000);	/*select Rx mode*/
				odm_set_rf_reg(p_dm, RF_PATH_B, 0x31, 0xfffff, 0x3f7ff);
				odm_set_rf_reg(p_dm, RF_PATH_B, 0x32, 0xfffff, 0xc26bf);	/*disable LNA*/
				odm_set_rf_reg(p_dm, RF_PATH_B, 0xef, 0x80000, 0x0);
			}
		}
	} else if (p_dm->support_ic_type & (ODM_RTL8821 | ODM_RTL8881A)) {
		if (type == phydm_disable_lna) {
			odm_set_rf_reg(p_dm, RF_PATH_A, 0xef, 0x80000, 0x1);
			odm_set_rf_reg(p_dm, RF_PATH_A, 0x30, 0xfffff, 0x18000);	/*select Rx mode*/
			odm_set_rf_reg(p_dm, RF_PATH_A, 0x31, 0xfffff, 0x0002f);
			odm_set_rf_reg(p_dm, RF_PATH_A, 0x32, 0xfffff, 0xfb09b);	/*disable LNA*/
			odm_set_rf_reg(p_dm, RF_PATH_A, 0xef, 0x80000, 0x0);
		} else if (type == phydm_enable_lna) {
			odm_set_rf_reg(p_dm, RF_PATH_A, 0xef, 0x80000, 0x1);
			odm_set_rf_reg(p_dm, RF_PATH_A, 0x30, 0xfffff, 0x18000);	/*select Rx mode*/
			odm_set_rf_reg(p_dm, RF_PATH_A, 0x31, 0xfffff, 0x0002f);
			odm_set_rf_reg(p_dm, RF_PATH_A, 0x32, 0xfffff, 0xfb0bb);	/*disable LNA*/
			odm_set_rf_reg(p_dm, RF_PATH_A, 0xef, 0x80000, 0x0);
		}
	}
}



void
phydm_set_trx_mux(
	void				*p_dm_void,
	enum phydm_trx_mux_type	tx_mode,
	enum phydm_trx_mux_type	rx_mode
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	if (p_dm->support_ic_type & ODM_IC_11N_SERIES) {
		odm_set_bb_reg(p_dm, ODM_REG_CCK_RPT_FORMAT_11N, BIT(3) | BIT(2) | BIT(1), tx_mode);			/*set TXmod to standby mode to remove outside noise affect*/
		odm_set_bb_reg(p_dm, ODM_REG_CCK_RPT_FORMAT_11N, BIT(22) | BIT(21) | BIT(20), rx_mode);		/*set RXmod to standby mode to remove outside noise affect*/
		if (p_dm->rf_type > RF_1T1R) {
			odm_set_bb_reg(p_dm, ODM_REG_CCK_RPT_FORMAT_11N_B, BIT(3) | BIT(2) | BIT(1), tx_mode);		/*set TXmod to standby mode to remove outside noise affect*/
			odm_set_bb_reg(p_dm, ODM_REG_CCK_RPT_FORMAT_11N_B, BIT(22) | BIT(21) | BIT(20), rx_mode);	/*set RXmod to standby mode to remove outside noise affect*/
		}
	}
#if (RTL8195A_SUPPORT == 0)
	else if (p_dm->support_ic_type & ODM_IC_11AC_SERIES) {
		odm_set_bb_reg(p_dm, ODM_REG_TRMUX_11AC, BIT(11) | BIT(10) | BIT(9) | BIT(8), tx_mode);				/*set TXmod to standby mode to remove outside noise affect*/
		odm_set_bb_reg(p_dm, ODM_REG_TRMUX_11AC, BIT(7) | BIT(6) | BIT(5) | BIT(4), rx_mode);				/*set RXmod to standby mode to remove outside noise affect*/
		if (p_dm->rf_type > RF_1T1R) {
			odm_set_bb_reg(p_dm, ODM_REG_TRMUX_11AC_B, BIT(11) | BIT(10) | BIT(9) | BIT(8), tx_mode);		/*set TXmod to standby mode to remove outside noise affect*/
			odm_set_bb_reg(p_dm, ODM_REG_TRMUX_11AC_B, BIT(7) | BIT(6) | BIT(5) | BIT(4), rx_mode);			/*set RXmod to standby mode to remove outside noise affect*/
		}
	}
#endif

}

void
phydm_mac_edcca_state(
	void					*p_dm_void,
	enum phydm_mac_edcca_type		state
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	if (state == phydm_ignore_edcca) {
		odm_set_mac_reg(p_dm, REG_TX_PTCL_CTRL, BIT(15), 1);	/*ignore EDCCA	reg520[15]=1*/
		/*		odm_set_mac_reg(p_dm, REG_RD_CTRL, BIT(11), 0);			*/ /*reg524[11]=0*/
	} else {	/*don't set MAC ignore EDCCA signal*/
		odm_set_mac_reg(p_dm, REG_TX_PTCL_CTRL, BIT(15), 0);	/*don't ignore EDCCA	 reg520[15]=0*/
		/*		odm_set_mac_reg(p_dm, REG_RD_CTRL, BIT(11), 1);			*/ /*reg524[11]=1	*/
	}
	PHYDM_DBG(p_dm, DBG_ADPTVTY, ("EDCCA enable state = %d\n", state));

}

void
phydm_check_environment(
	void	*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_adaptivity_struct	*adaptivity = (struct phydm_adaptivity_struct *)phydm_get_structure(p_dm, PHYDM_ADAPTIVITY);
	boolean	is_clean_environment = false;

	is_clean_environment = phydm_cal_nhm_cnt(p_dm);

	if (is_clean_environment == true) {
		p_dm->th_l2h_ini = adaptivity->th_l2h_ini_backup;			/*adaptivity mode*/
		p_dm->th_edcca_hl_diff = adaptivity->th_edcca_hl_diff_backup;

		p_dm->adaptivity_enable = true;
	} else {
		p_dm->th_l2h_ini = p_dm->th_l2h_ini_mode2;			/*mode2*/
		p_dm->th_edcca_hl_diff = p_dm->th_edcca_hl_diff_mode2;

		p_dm->adaptivity_enable = false;
	}

	adaptivity->is_check = true;

}

void
phydm_search_pwdb_lower_bound(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_adaptivity_struct	*adaptivity = (struct phydm_adaptivity_struct *)phydm_get_structure(p_dm, PHYDM_ADAPTIVITY);
	u32			value32 = 0, reg_value32 = 0;
	u8			cnt, try_count = 0;
	u8			tx_edcca1 = 0;
	boolean			is_adjust = true;
	s8			th_l2h_dmc, th_h2l_dmc, igi_target = 0x32;
	s8			diff;
	u8			IGI = adaptivity->igi_base + 30 + (u8)p_dm->th_l2h_ini - (u8)p_dm->th_edcca_hl_diff;

	if (p_dm->support_ic_type & (ODM_RTL8723B | ODM_RTL8188E | ODM_RTL8192E | ODM_RTL8812 | ODM_RTL8821 | ODM_RTL8881A))
		phydm_set_lna(p_dm, phydm_disable_lna);

	diff = igi_target - (s8)IGI;
	th_l2h_dmc = p_dm->th_l2h_ini + diff;
	if (th_l2h_dmc > 10)
		th_l2h_dmc = 10;

	th_h2l_dmc = th_l2h_dmc - p_dm->th_edcca_hl_diff;
	phydm_set_edcca_threshold(p_dm, th_h2l_dmc, th_l2h_dmc);
	ODM_delay_ms(30);

	while (is_adjust) {

		/*check CCA status*/
		if (phydm_set_bb_dbg_port(p_dm, BB_DBGPORT_PRIORITY_1, 0x0)) {/*set debug port to 0x0*/
			reg_value32 = phydm_get_bb_dbg_port_value(p_dm);

			while (reg_value32 & BIT(3) && try_count < 3) {
				ODM_delay_ms(3);
				try_count = try_count + 1;
				reg_value32 = phydm_get_bb_dbg_port_value(p_dm);
			}
			phydm_release_bb_dbg_port(p_dm);
			try_count = 0;
		}

		/*count EDCCA signal = 1 times*/
		for (cnt = 0; cnt < 20; cnt++) {

			if (phydm_set_bb_dbg_port(p_dm, BB_DBGPORT_PRIORITY_1, adaptivity->adaptivity_dbg_port)) {
				value32 = phydm_get_bb_dbg_port_value(p_dm);
				phydm_release_bb_dbg_port(p_dm);
			}

			if (value32 & BIT(30) && (p_dm->support_ic_type & (ODM_RTL8723B | ODM_RTL8188E)))
				tx_edcca1 = tx_edcca1 + 1;
			else if (value32 & BIT(29))
				tx_edcca1 = tx_edcca1 + 1;
		}

		if (tx_edcca1 > 1) {
			IGI = IGI - 1;
			th_l2h_dmc = th_l2h_dmc + 1;
			if (th_l2h_dmc > 10)
				th_l2h_dmc = 10;

			th_h2l_dmc = th_l2h_dmc - p_dm->th_edcca_hl_diff;
			phydm_set_edcca_threshold(p_dm, th_h2l_dmc, th_l2h_dmc);
			tx_edcca1 = 0;
			if (th_l2h_dmc == 10)
				is_adjust = false;

		} else
			is_adjust = false;

	}

	adaptivity->adapt_igi_up = IGI - p_dm->dc_backoff;
	adaptivity->h2l_lb = th_h2l_dmc + p_dm->dc_backoff;
	adaptivity->l2h_lb = th_l2h_dmc + p_dm->dc_backoff;

	if (p_dm->support_ic_type & (ODM_RTL8723B | ODM_RTL8188E | ODM_RTL8192E | ODM_RTL8812 | ODM_RTL8821 | ODM_RTL8881A))
		phydm_set_lna(p_dm, phydm_enable_lna);

	phydm_set_edcca_threshold(p_dm, 0x7f, 0x7f);				/*resume to no link state*/
}

boolean
phydm_re_search_condition(
	void				*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_adaptivity_struct	*adaptivity = (struct phydm_adaptivity_struct *)phydm_get_structure(p_dm, PHYDM_ADAPTIVITY);
	u8			adaptivity_igi_upper = adaptivity->adapt_igi_up + p_dm->dc_backoff;
	/*s8		TH_L2H_dmc, IGI_target = 0x32;*/
	/*s8		diff;*/

	/*TH_L2H_dmc = 10;*/

	/*diff = TH_L2H_dmc - p_dm->TH_L2H_ini;*/
	/*lowest_IGI_upper = IGI_target - diff;*/
	/*if ((adaptivity_igi_upper - lowest_IGI_upper) <= 5)*/

	if (adaptivity_igi_upper <= 0x26)
		return true;
	else
		return false;
}

void
phydm_adaptivity_info_init(
	void				*p_dm_void,
	enum phydm_adapinfo_e	cmn_info,
	u32				value
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_adaptivity_struct	*adaptivity = (struct phydm_adaptivity_struct *)phydm_get_structure(p_dm, PHYDM_ADAPTIVITY);

	switch (cmn_info)	{
	case PHYDM_ADAPINFO_CARRIER_SENSE_ENABLE:
		p_dm->carrier_sense_enable = (boolean)value;
		break;

	case PHYDM_ADAPINFO_DCBACKOFF:
		p_dm->dc_backoff = (u8)value;
		break;

	case PHYDM_ADAPINFO_DYNAMICLINKADAPTIVITY:
		adaptivity->dynamic_link_adaptivity = (boolean)value;
		break;

	case PHYDM_ADAPINFO_TH_L2H_INI:
		p_dm->th_l2h_ini = (s8)value;
		break;

	case PHYDM_ADAPINFO_TH_EDCCA_HL_DIFF:
		p_dm->th_edcca_hl_diff = (s8)value;
		break;

	case PHYDM_ADAPINFO_AP_NUM_TH:
		adaptivity->ap_num_th = (u8)value;
		break;

	default:
		break;

	}

}

void
phydm_adaptivity_init(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_adaptivity_struct	*adaptivity = (struct phydm_adaptivity_struct *)phydm_get_structure(p_dm, PHYDM_ADAPTIVITY);
	s8	igi_target = 0x32;
	/*struct phydm_dig_struct* p_dig_t = &p_dm->dm_dig_table;*/

#if (DM_ODM_SUPPORT_TYPE & (ODM_CE | ODM_WIN))

	if (p_dm->carrier_sense_enable == false) {
		if (p_dm->th_l2h_ini == 0)
			phydm_set_l2h_th_ini(p_dm);
	} else
		p_dm->th_l2h_ini = 0xa;

	if (p_dm->th_edcca_hl_diff == 0)
		p_dm->th_edcca_hl_diff = 7;
#if (DM_ODM_SUPPORT_TYPE & (ODM_CE))
	if (p_dm->wifi_test == true || *(p_dm->p_mp_mode) == true)
#else
	if ((p_dm->wifi_test & RT_WIFI_LOGO) == true)
#endif
		p_dm->edcca_enable = false;		/*even no adaptivity, we still enable EDCCA, AP side use mib control*/
	else
		p_dm->edcca_enable = true;

#elif (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct rtl8192cd_priv	*priv = p_dm->priv;

	if (p_dm->carrier_sense_enable) {
		p_dm->th_l2h_ini = 0xa;
		p_dm->th_edcca_hl_diff = 7;
	} else {
		p_dm->th_l2h_ini = p_dm->TH_L2H_default;	/*set by mib*/
		p_dm->th_edcca_hl_diff = p_dm->th_edcca_hl_diff_default;
	}

	if (priv->pshare->rf_ft_var.adaptivity_enable == 2)
		adaptivity->dynamic_link_adaptivity = true;
	else
		adaptivity->dynamic_link_adaptivity = false;

#endif

	adaptivity->adapt_igi_up = 0;
	p_dm->adaptivity_enable = false;	/*use this flag to decide enable or disable*/

	p_dm->th_l2h_ini_mode2 = 20;
	p_dm->th_edcca_hl_diff_mode2 = 8;
	adaptivity->debug_mode = false;
	adaptivity->th_l2h_ini_backup = p_dm->th_l2h_ini;
	adaptivity->th_edcca_hl_diff_backup = p_dm->th_edcca_hl_diff;

	adaptivity->igi_base = 0x32;
	adaptivity->igi_target = 0x1c;
	adaptivity->h2l_lb = 0;
	adaptivity->l2h_lb = 0;
	adaptivity->is_check = false;
	adaptivity->adajust_igi_level = 0;
	adaptivity->is_stop_edcca = false;
	adaptivity->backup_h2l = 0;
	adaptivity->backup_l2h = 0;
	adaptivity->adaptivity_dbg_port = (p_dm->support_ic_type & ODM_IC_11N_SERIES) ? 0x208 : 0x209;

	phydm_mac_edcca_state(p_dm, phydm_dont_ignore_edcca);

	if (p_dm->support_ic_type & ODM_IC_11N_GAIN_IDX_EDCCA) {
		/*odm_set_bb_reg(p_dm, ODM_REG_EDCCA_DOWN_OPT_11N, BIT(12) | BIT(11) | BIT(10), 0x7);*/		/*interfernce need > 2^x us, and then EDCCA will be 1*/
		if (p_dm->support_ic_type & ODM_RTL8197F) {
			odm_set_bb_reg(p_dm, ODM_REG_PAGE_B1_97F, BIT(30), 0x1);								/*set to page B1*/
			odm_set_bb_reg(p_dm, ODM_REG_EDCCA_DCNF_97F, BIT(27) | BIT(26), 0x1);		/*0:rx_dfir, 1: dcnf_out, 2 :rx_iq, 3: rx_nbi_nf_out*/
			odm_set_bb_reg(p_dm, ODM_REG_PAGE_B1_97F, BIT(30), 0x0);
		} else
			odm_set_bb_reg(p_dm, ODM_REG_EDCCA_DCNF_11N, BIT(21) | BIT(20), 0x1);		/*0:rx_dfir, 1: dcnf_out, 2 :rx_iq, 3: rx_nbi_nf_out*/
	}
#if (RTL8195A_SUPPORT == 0)
	if (p_dm->support_ic_type & ODM_IC_11AC_GAIN_IDX_EDCCA) {		/*8814a no need to find pwdB lower bound, maybe*/
		/*odm_set_bb_reg(p_dm, ODM_REG_EDCCA_DOWN_OPT, BIT(30) | BIT(29) | BIT(28), 0x7);*/		/*interfernce need > 2^x us, and then EDCCA will be 1*/
		odm_set_bb_reg(p_dm, ODM_REG_ACBB_EDCCA_ENHANCE, BIT(29) | BIT(28), 0x1);		/*0:rx_dfir, 1: dcnf_out, 2 :rx_iq, 3: rx_nbi_nf_out*/
	}

	if (!(p_dm->support_ic_type & (ODM_IC_11AC_GAIN_IDX_EDCCA | ODM_IC_11N_GAIN_IDX_EDCCA))) {
		phydm_search_pwdb_lower_bound(p_dm);
		if (phydm_re_search_condition(p_dm))
			phydm_search_pwdb_lower_bound(p_dm);
	} else
		phydm_set_edcca_threshold(p_dm, 0x7f, 0x7f);				/*resume to no link state*/
#endif
	/*forgetting factor setting*/
	phydm_set_forgetting_factor(p_dm);

	/*pwdb mode setting with 0: mean, 1:max*/
	phydm_set_pwdb_mode(p_dm);

	/*we need to consider PwdB upper bound for 8814 later IC*/
	adaptivity->adajust_igi_level = (u8)((p_dm->th_l2h_ini + igi_target) - pwdb_upper_bound + dfir_loss);	/*IGI = L2H - PwdB - dfir_loss*/

	/*Check this later on Windows*/
	/*phydm_set_edcca_threshold_api(p_dm, p_dig_t->cur_ig_value);*/

	p_dm->adaptivity_flag = (p_dm->support_ic_type & ODM_IC_GAIN_IDX_EDCCA) ? false : true;
	
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	adaptivity->igi_up_bound_lmt_val = 180;
#else
	adaptivity->igi_up_bound_lmt_val = 90;
#endif
	adaptivity->igi_up_bound_lmt_cnt = 0;
	adaptivity->igi_lmt_en = false;

}


void
phydm_adaptivity(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_dig_struct			*p_dig_t = &p_dm->dm_dig_table;
	u8			igi = p_dig_t->cur_ig_value;
	s8			th_l2h_dmc, th_h2l_dmc;
	s8			diff = 0, igi_target = 0x32;
	struct phydm_adaptivity_struct	*adaptivity = (struct phydm_adaptivity_struct *)phydm_get_structure(p_dm, PHYDM_ADAPTIVITY);
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct _ADAPTER		*p_adapter	= p_dm->adapter;
	u32			is_fw_current_in_ps_mode = false;
	u8			disable_ap_adapt_setting;

	p_adapter->HalFunc.GetHwRegHandler(p_adapter, HW_VAR_FW_PSMODE_STATUS, (u8 *)(&is_fw_current_in_ps_mode));

	/*Disable EDCCA mode while under LPS mode, added by Roger, 2012.09.14.*/
	if (is_fw_current_in_ps_mode)
		return;
#endif

	if ((p_dm->edcca_enable == false) || (adaptivity->is_stop_edcca == true)) {
		PHYDM_DBG(p_dm, DBG_ADPTVTY, ("Disable EDCCA!!!\n"));
		return;
	}

	phydm_check_adaptivity(p_dm);	/*Check adaptivity enable*/
	phydm_dig_up_bound_lmt_en(p_dm);

	if ((!(p_dm->support_ability & ODM_BB_ADAPTIVITY)) && adaptivity->debug_mode == false) {
		PHYDM_DBG(p_dm, DBG_ADPTVTY, ("adaptivity disable, enable EDCCA mode!!!\n"));
		p_dm->th_l2h_ini = p_dm->th_l2h_ini_mode2;
		p_dm->th_edcca_hl_diff = p_dm->th_edcca_hl_diff_mode2;
	}
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	else if (adaptivity->debug_mode == false) {
		disable_ap_adapt_setting = false;
		if (p_dm->p_soft_ap_mode != NULL) {
			if (*(p_dm->p_soft_ap_mode) != 0 && (p_dm->soft_ap_special_setting & BIT(0)))
				disable_ap_adapt_setting = true;
			PHYDM_DBG(p_dm, DBG_ADPTVTY, ("p_dm->soft_ap_special_setting = %x, *(p_dm->p_soft_ap_mode) = %d, disable_ap_adapt_setting = %d\n", p_dm->soft_ap_special_setting, *(p_dm->p_soft_ap_mode), disable_ap_adapt_setting));
		}
		if (phydm_check_channel_plan(p_dm) || (p_dm->ap_total_num > adaptivity->ap_num_th) || disable_ap_adapt_setting) {
			p_dm->th_l2h_ini = p_dm->th_l2h_ini_mode2;
			p_dm->th_edcca_hl_diff = p_dm->th_edcca_hl_diff_mode2;
		} else {
			p_dm->th_l2h_ini = adaptivity->th_l2h_ini_backup;
			p_dm->th_edcca_hl_diff = adaptivity->th_edcca_hl_diff_backup;
		}
	}
#endif
	else if (adaptivity->debug_mode == true) {
		p_dm->th_l2h_ini = adaptivity->th_l2h_ini_debug;
		p_dm->th_edcca_hl_diff = 7;
		adaptivity->adajust_igi_level = (u8)((p_dm->th_l2h_ini + igi_target) - pwdb_upper_bound + dfir_loss);	/*IGI = L2H - PwdB - dfir_loss*/
	}
	PHYDM_DBG(p_dm, DBG_ADPTVTY, ("odm_Adaptivity() =====>\n"));
	PHYDM_DBG(p_dm, DBG_ADPTVTY, ("igi_base=0x%x, th_l2h_ini = %d, th_edcca_hl_diff = %d\n",
		adaptivity->igi_base, p_dm->th_l2h_ini, p_dm->th_edcca_hl_diff));
#if (RTL8195A_SUPPORT == 0)
	if (p_dm->support_ic_type & ODM_IC_11AC_SERIES) {
		/*fix AC series when enable EDCCA hang issue*/
		odm_set_bb_reg(p_dm, 0x800, BIT(10), 1);	/*ADC_mask disable*/
		odm_set_bb_reg(p_dm, 0x800, BIT(10), 0);	/*ADC_mask enable*/
	}
#endif

	igi_target = adaptivity->igi_base;
	adaptivity->igi_target = (u8) igi_target;

	PHYDM_DBG(p_dm, DBG_ADPTVTY, ("band_width=%s, igi_target=0x%x, dynamic_link_adaptivity = %d\n",
		(*p_dm->p_band_width == CHANNEL_WIDTH_80) ? "80M" : ((*p_dm->p_band_width == CHANNEL_WIDTH_40) ? "40M" : "20M"), igi_target, adaptivity->dynamic_link_adaptivity));
	PHYDM_DBG(p_dm, DBG_ADPTVTY, ("adajust_igi_level= 0x%x, adaptivity_flag = %d, adaptivity_enable = %d\n",
		adaptivity->adajust_igi_level, p_dm->adaptivity_flag, p_dm->adaptivity_enable));

	if ((adaptivity->dynamic_link_adaptivity == true) && (!p_dm->is_linked) && (p_dm->adaptivity_enable == false)) {
		phydm_set_edcca_threshold(p_dm, 0x7f, 0x7f);
		PHYDM_DBG(p_dm, DBG_ADPTVTY, ("In DynamicLink mode(noisy) and No link, Turn off EDCCA!!\n"));
		return;
	}

	if (p_dm->support_ic_type & (ODM_IC_11AC_GAIN_IDX_EDCCA | ODM_IC_11N_GAIN_IDX_EDCCA)) {
		if ((adaptivity->adajust_igi_level > igi) && (p_dm->adaptivity_enable == true))
			diff = adaptivity->adajust_igi_level - igi;
		else if (p_dm->adaptivity_enable == false)
			diff = 0x3e - igi;

		th_l2h_dmc = p_dm->th_l2h_ini - diff + igi_target;
		th_h2l_dmc = th_l2h_dmc - p_dm->th_edcca_hl_diff;
	}
#if (RTL8195A_SUPPORT == 0)
	else	{
		diff = igi_target - (s8)igi;
		th_l2h_dmc = p_dm->th_l2h_ini + diff;
		if (th_l2h_dmc > 10 && (p_dm->adaptivity_enable == true))
			th_l2h_dmc = 10;

		th_h2l_dmc = th_l2h_dmc - p_dm->th_edcca_hl_diff;

		/*replace lower bound to prevent EDCCA always equal 1*/
		if (th_h2l_dmc < adaptivity->h2l_lb)
			th_h2l_dmc = adaptivity->h2l_lb;
		if (th_l2h_dmc < adaptivity->l2h_lb)
			th_l2h_dmc = adaptivity->l2h_lb;
	}
#endif
	adaptivity->th_l2h = th_l2h_dmc;
	adaptivity->th_h2l = th_h2l_dmc;
	PHYDM_DBG(p_dm, DBG_ADPTVTY, ("IGI=0x%x, th_l2h_dmc = %d, th_h2l_dmc = %d\n", igi, th_l2h_dmc, th_h2l_dmc));
	PHYDM_DBG(p_dm, DBG_ADPTVTY, ("adapt_igi_up=0x%x, h2l_lb = 0x%x, l2h_lb = 0x%x\n", adaptivity->adapt_igi_up, adaptivity->h2l_lb, adaptivity->l2h_lb));
	PHYDM_DBG(p_dm, DBG_ADPTVTY, ("debug_mode = %d\n", adaptivity->debug_mode));
	phydm_set_edcca_threshold(p_dm, th_h2l_dmc, th_l2h_dmc);

	if (p_dm->adaptivity_enable == true)
		odm_set_mac_reg(p_dm, REG_RD_CTRL, BIT(11), 1);

	return;
}

/*This API is for solving USB can't Tx problem due to USB3.0 interference in 2.4G*/
void
phydm_pause_edcca(
	void	*p_dm_void,
	boolean	is_pasue_edcca
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_adaptivity_struct	*adaptivity = (struct phydm_adaptivity_struct *)phydm_get_structure(p_dm, PHYDM_ADAPTIVITY);
	struct phydm_dig_struct	*p_dig_t = &p_dm->dm_dig_table;
	u8	IGI = p_dig_t->cur_ig_value;
	s8	diff = 0;

	if (is_pasue_edcca) {
		adaptivity->is_stop_edcca = true;

		if (p_dm->support_ic_type & (ODM_IC_11AC_GAIN_IDX_EDCCA | ODM_IC_11N_GAIN_IDX_EDCCA)) {
			if (adaptivity->adajust_igi_level > IGI)
				diff = adaptivity->adajust_igi_level - IGI;

			adaptivity->backup_l2h = p_dm->th_l2h_ini - diff + adaptivity->igi_target;
			adaptivity->backup_h2l = adaptivity->backup_l2h - p_dm->th_edcca_hl_diff;
		}
#if (RTL8195A_SUPPORT == 0)
		else {
			diff = adaptivity->igi_target - (s8)IGI;
			adaptivity->backup_l2h = p_dm->th_l2h_ini + diff;
			if (adaptivity->backup_l2h > 10)
				adaptivity->backup_l2h = 10;

			adaptivity->backup_h2l = adaptivity->backup_l2h - p_dm->th_edcca_hl_diff;

			/*replace lower bound to prevent EDCCA always equal 1*/
			if (adaptivity->backup_h2l < adaptivity->h2l_lb)
				adaptivity->backup_h2l = adaptivity->h2l_lb;
			if (adaptivity->backup_l2h < adaptivity->l2h_lb)
				adaptivity->backup_l2h = adaptivity->l2h_lb;
		}
#endif
		PHYDM_DBG(p_dm, DBG_ADPTVTY, ("pauseEDCCA : L2Hbak = 0x%x, H2Lbak = 0x%x, IGI = 0x%x\n", adaptivity->backup_l2h, adaptivity->backup_h2l, IGI));

		/*Disable EDCCA*/
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
		if (odm_is_work_item_scheduled(&(adaptivity->phydm_pause_edcca_work_item)) == false)
			odm_schedule_work_item(&(adaptivity->phydm_pause_edcca_work_item));
#else
		phydm_pause_edcca_work_item_callback(p_dm);
#endif

	} else {

		adaptivity->is_stop_edcca = false;
		PHYDM_DBG(p_dm, DBG_ADPTVTY, ("resumeEDCCA : L2Hbak = 0x%x, H2Lbak = 0x%x, IGI = 0x%x\n", adaptivity->backup_l2h, adaptivity->backup_h2l, IGI));
		/*Resume EDCCA*/
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
		if (odm_is_work_item_scheduled(&(adaptivity->phydm_resume_edcca_work_item)) == false)
			odm_schedule_work_item(&(adaptivity->phydm_resume_edcca_work_item));
#else
		phydm_resume_edcca_work_item_callback(p_dm);
#endif

	}

}


void
phydm_pause_edcca_work_item_callback(
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct _ADAPTER		*adapter
#else
	void			*p_dm_void
#endif
)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PHAL_DATA_TYPE	p_hal_data = GET_HAL_DATA(adapter);
	struct PHY_DM_STRUCT		*p_dm = &p_hal_data->DM_OutSrc;
#else
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
#endif

	if (p_dm->support_ic_type & ODM_IC_11N_SERIES)
		odm_set_bb_reg(p_dm, REG_OFDM_0_ECCA_THRESHOLD, MASKBYTE2 | MASKBYTE0, (u32)(0x7f | 0x7f << 16));
#if (RTL8195A_SUPPORT == 0)
	else if (p_dm->support_ic_type & ODM_IC_11AC_SERIES)
		odm_set_bb_reg(p_dm, REG_FPGA0_XB_LSSI_READ_BACK, MASKLWORD, (u16)(0x7f | 0x7f << 8));
#endif

}

void
phydm_resume_edcca_work_item_callback(
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct _ADAPTER		*adapter
#else
	void			*p_dm_void
#endif
)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PHAL_DATA_TYPE	p_hal_data = GET_HAL_DATA(adapter);
	struct PHY_DM_STRUCT		*p_dm = &p_hal_data->DM_OutSrc;
#else
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
#endif
	struct phydm_adaptivity_struct	*adaptivity = (struct phydm_adaptivity_struct *)phydm_get_structure(p_dm, PHYDM_ADAPTIVITY);

	if (p_dm->support_ic_type & ODM_IC_11N_SERIES)
		odm_set_bb_reg(p_dm, REG_OFDM_0_ECCA_THRESHOLD, MASKBYTE2 | MASKBYTE0, (u32)((u8)adaptivity->backup_l2h | (u8)adaptivity->backup_h2l << 16));
#if (RTL8195A_SUPPORT == 0)
	else if (p_dm->support_ic_type & ODM_IC_11AC_SERIES)
		odm_set_bb_reg(p_dm, REG_FPGA0_XB_LSSI_READ_BACK, MASKLWORD, (u16)((u8)adaptivity->backup_l2h | (u8)adaptivity->backup_h2l << 8));
#endif

}


void
phydm_set_edcca_threshold_api(
	void	*p_dm_void,
	u8	IGI
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_adaptivity_struct	*adaptivity = (struct phydm_adaptivity_struct *)phydm_get_structure(p_dm, PHYDM_ADAPTIVITY);
	s8			th_l2h_dmc, th_h2l_dmc;
	s8			diff = 0, igi_target = 0x32;

	if (p_dm->support_ability & ODM_BB_ADAPTIVITY) {
		if (p_dm->support_ic_type & (ODM_IC_11AC_GAIN_IDX_EDCCA | ODM_IC_11N_GAIN_IDX_EDCCA)) {
			if (adaptivity->adajust_igi_level > IGI)
				diff = adaptivity->adajust_igi_level - IGI;

			th_l2h_dmc = p_dm->th_l2h_ini - diff + igi_target;
			th_h2l_dmc = th_l2h_dmc - p_dm->th_edcca_hl_diff;
		}
#if (RTL8195A_SUPPORT == 0)
		else	{
			diff = igi_target - (s8)IGI;
			th_l2h_dmc = p_dm->th_l2h_ini + diff;
			if (th_l2h_dmc > 10)
				th_l2h_dmc = 10;

			th_h2l_dmc = th_l2h_dmc - p_dm->th_edcca_hl_diff;

			/*replace lower bound to prevent EDCCA always equal 1*/
			if (th_h2l_dmc < adaptivity->h2l_lb)
				th_h2l_dmc = adaptivity->h2l_lb;
			if (th_l2h_dmc < adaptivity->l2h_lb)
				th_l2h_dmc = adaptivity->l2h_lb;
		}
#endif
		PHYDM_DBG(p_dm, DBG_ADPTVTY, ("API :IGI=0x%x, th_l2h_dmc = %d, th_h2l_dmc = %d\n", IGI, th_l2h_dmc, th_h2l_dmc));
		PHYDM_DBG(p_dm, DBG_ADPTVTY, ("API :adapt_igi_up=0x%x, h2l_lb = 0x%x, l2h_lb = 0x%x\n", adaptivity->adapt_igi_up, adaptivity->h2l_lb, adaptivity->l2h_lb));

		phydm_set_edcca_threshold(p_dm, th_h2l_dmc, th_l2h_dmc);
	}
}

void
phydm_adaptivity_debug(
	void		*p_dm_void,
	u32		*const dm_value,
	u32		*_used,
	char		*output,
	u32		*_out_len
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_adaptivity_struct	*adaptivity = (struct phydm_adaptivity_struct *)phydm_get_structure(p_dm, PHYDM_ADAPTIVITY);
	u32 used = *_used;
	u32 out_len = *_out_len;
	u32 reg_value32;
	s8 h2l_diff = 0;

	if (dm_value[0] == PHYDM_ADAPT_DEBUG) {
		PHYDM_SNPRINTF((output + used, out_len - used, "Adaptivity Debug Mode ===>\n"));
		adaptivity->debug_mode = true;
		adaptivity->th_l2h_ini_debug = (s8)dm_value[1];
		PHYDM_SNPRINTF((output + used, out_len - used, "th_l2h_ini_debug = %d\n", adaptivity->th_l2h_ini_debug));
	} else if (dm_value[0] == PHYDM_ADAPT_RESUME) {
		PHYDM_SNPRINTF((output + used, out_len - used, "===> Adaptivity Resume\n"));
		adaptivity->debug_mode = false;
	} else if (dm_value[0] == PHYDM_EDCCA_TH_PAUSE) {
		PHYDM_SNPRINTF((output + used, out_len - used, "EDCCA Threshold Pause\n"));
		p_dm->edcca_enable = false;
	} else if (dm_value[0] == PHYDM_EDCCA_RESUME) {
		PHYDM_SNPRINTF((output + used, out_len - used, "EDCCA Resume\n"));
		p_dm->edcca_enable = true;
	} else if (dm_value[0] == PHYDM_ADAPT_MSG) {
		PHYDM_SNPRINTF((output + used, out_len - used, "debug_mode = %s, th_l2h_ini = %d\n", (adaptivity->debug_mode ? "TRUE" : "FALSE"), p_dm->th_l2h_ini));
		if (p_dm->support_ic_type & ODM_IC_11N_SERIES) {
			reg_value32 = odm_get_bb_reg(p_dm, 0xc4c, MASKDWORD);
			h2l_diff = (s8)(0x000000ff & reg_value32) - (s8)((0x00ff0000 & reg_value32)>>16);
		}
#if (RTL8195A_SUPPORT == 0)
		else if (p_dm->support_ic_type & ODM_IC_11AC_SERIES) {
			reg_value32 = odm_get_bb_reg(p_dm, 0x8a4, MASKDWORD);
			h2l_diff = (s8)(0x000000ff & reg_value32) - (s8)((0x0000ff00 & reg_value32)>>8);
		}
#endif
		if (h2l_diff == 7)
			PHYDM_SNPRINTF((output + used, out_len - used, "adaptivity is enabled\n"));
		else
			PHYDM_SNPRINTF((output + used, out_len - used, "adaptivity is disabled\n"));
	}
	*_used = used;
	*_out_len = out_len;
}

void
phydm_set_l2h_th_ini(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	if (p_dm->support_ic_type & ODM_IC_11AC_SERIES) {
		if (p_dm->support_ic_type & (ODM_RTL8821C | ODM_RTL8822B | ODM_RTL8814A))
			p_dm->th_l2h_ini = 0xf2;
		else
			p_dm->th_l2h_ini = 0xef;
	} else
		p_dm->th_l2h_ini = 0xf5;
}

void
phydm_set_forgetting_factor(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	if (p_dm->support_ic_type & (ODM_RTL8821C | ODM_RTL8822B | ODM_RTL8814A))
		odm_set_bb_reg(p_dm, 0x8a0, BIT(1) | BIT(0), 0);
}

void
phydm_set_pwdb_mode(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	if (p_dm->support_ability & ODM_BB_ADAPTIVITY) {
		if (p_dm->support_ic_type & ODM_RTL8822B)
			odm_set_bb_reg(p_dm, 0x8dc, BIT(5), 0x1);
		else if (p_dm->support_ic_type & ODM_RTL8197F)
			odm_set_bb_reg(p_dm, 0xce8, BIT(13), 0x1);
	} else {
		if (p_dm->support_ic_type & ODM_RTL8822B)
			odm_set_bb_reg(p_dm, 0x8dc, BIT(5), 0x0);
		else if (p_dm->support_ic_type & ODM_RTL8197F)
			odm_set_bb_reg(p_dm, 0xce8, BIT(13), 0x0);
	}
}

void
phydm_set_edcca_val(
	void			*p_dm_void,
	u32			*val_buf,
	u8			val_len
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	if (val_len != 2) {
		PHYDM_DBG(p_dm, ODM_COMP_API, ("[Error][adaptivity]Need val_len = 2\n"));
		return;
	}
	if (p_dm->pause_ability & BIT(F13_ADPTVTY))
		p_dm->adaptivity.is_stop_edcca = true;
	else
		p_dm->adaptivity.is_stop_edcca = false;

	phydm_set_edcca_threshold(p_dm, (s8)val_buf[1], (s8)val_buf[0]);
}
