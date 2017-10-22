/******************************************************************************
 *
 * Copyright(c) 2007 - 2016  Realtek Corporation.
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
 * *************************************************************/
#include "mp_precomp.h"
#include "phydm_precomp.h"

void phydm_check_adaptivity(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct adaptivity_statistics *adaptivity =
		(struct adaptivity_statistics *)phydm_get_structure(
			dm, PHYDM_ADAPTIVITY);

	if (dm->support_ability & ODM_BB_ADAPTIVITY) {
		if (adaptivity->dynamic_link_adaptivity ||
		    adaptivity->acs_for_adaptivity) {
			if (dm->is_linked && !adaptivity->is_check) {
				phydm_nhm_counter_statistics(dm);
				phydm_check_environment(dm);
			} else if (!dm->is_linked) {
				adaptivity->is_check = false;
			}
		} else {
			dm->adaptivity_enable = true;

			if (dm->support_ic_type & (ODM_IC_11AC_GAIN_IDX_EDCCA |
						   ODM_IC_11N_GAIN_IDX_EDCCA))
				dm->adaptivity_flag = false;
			else
				dm->adaptivity_flag = true;
		}
	} else {
		dm->adaptivity_enable = false;
		dm->adaptivity_flag = false;
	}
}

void phydm_nhm_counter_statistics_init(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;

	if (dm->support_ic_type & ODM_IC_11N_SERIES) {
		/*PHY parameters initialize for n series*/

		/*0x894[31:16]=0x0xC350
		 *Time duration for NHM unit: us, 0xc350=200ms
		 */
		odm_write_2byte(dm, ODM_REG_CCX_PERIOD_11N + 2, 0xC350);
		/*0x890[31:16]=0xffff		th_9, th_10*/
		odm_write_2byte(dm, ODM_REG_NHM_TH9_TH10_11N + 2, 0xffff);
		/*0x898=0xffffff52		th_3, th_2, th_1, th_0*/
		odm_write_4byte(dm, ODM_REG_NHM_TH3_TO_TH0_11N, 0xffffff50);
		/*0x89c=0xffffffff		th_7, th_6, th_5, th_4*/
		odm_write_4byte(dm, ODM_REG_NHM_TH7_TO_TH4_11N, 0xffffffff);
		/*0xe28[7:0]=0xff		th_8*/
		odm_set_bb_reg(dm, ODM_REG_FPGA0_IQK_11N, MASKBYTE0, 0xff);
		/*0x890[10:8]=1		ignoreCCA ignore PHYTXON enable CCX*/
		odm_set_bb_reg(dm, ODM_REG_NHM_TH9_TH10_11N,
			       BIT(10) | BIT(9) | BIT(8), 0x1);
		/*0xc0c[7]=1			max power among all RX ants*/
		odm_set_bb_reg(dm, ODM_REG_OFDM_FA_RSTC_11N, BIT(7), 0x1);
	} else if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		/*PHY parameters initialize for ac series*/

		odm_write_2byte(dm, ODM_REG_CCX_PERIOD_11AC + 2, 0xC350);
		/*0x994[31:16]=0xffff		th_9, th_10*/
		odm_write_2byte(dm, ODM_REG_NHM_TH9_TH10_11AC + 2, 0xffff);
		/*0x998=0xffffff52		th_3, th_2, th_1, th_0*/
		odm_write_4byte(dm, ODM_REG_NHM_TH3_TO_TH0_11AC, 0xffffff50);
		/*0x99c=0xffffffff		th_7, th_6, th_5, th_4*/
		odm_write_4byte(dm, ODM_REG_NHM_TH7_TO_TH4_11AC, 0xffffffff);
		/*0x9a0[7:0]=0xff		th_8*/
		odm_set_bb_reg(dm, ODM_REG_NHM_TH8_11AC, MASKBYTE0, 0xff);
		/*0x994[10:8]=1		ignoreCCA ignore PHYTXON enable CCX*/
		odm_set_bb_reg(dm, ODM_REG_NHM_TH9_TH10_11AC,
			       BIT(8) | BIT(9) | BIT(10), 0x1);
		/*0x9e8[7]=1			max power among all RX ants*/
		odm_set_bb_reg(dm, ODM_REG_NHM_9E8_11AC, BIT(0), 0x1);
	}
}

void phydm_nhm_counter_statistics(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;

	if (!(dm->support_ability & ODM_BB_NHM_CNT))
		return;

	/*Get NHM report*/
	phydm_get_nhm_counter_statistics(dm);

	/*Reset NHM counter*/
	phydm_nhm_counter_statistics_reset(dm);
}

void phydm_get_nhm_counter_statistics(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	u32 value32 = 0;

	if (dm->support_ic_type & ODM_IC_11AC_SERIES)
		value32 = odm_get_bb_reg(dm, ODM_REG_NHM_CNT_11AC, MASKDWORD);
	else if (dm->support_ic_type & ODM_IC_11N_SERIES)
		value32 = odm_get_bb_reg(dm, ODM_REG_NHM_CNT_11N, MASKDWORD);

	dm->nhm_cnt_0 = (u8)(value32 & MASKBYTE0);
	dm->nhm_cnt_1 = (u8)((value32 & MASKBYTE1) >> 8);
}

void phydm_nhm_counter_statistics_reset(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;

	if (dm->support_ic_type & ODM_IC_11N_SERIES) {
		odm_set_bb_reg(dm, ODM_REG_NHM_TH9_TH10_11N, BIT(1), 0);
		odm_set_bb_reg(dm, ODM_REG_NHM_TH9_TH10_11N, BIT(1), 1);
	} else if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		odm_set_bb_reg(dm, ODM_REG_NHM_TH9_TH10_11AC, BIT(1), 0);
		odm_set_bb_reg(dm, ODM_REG_NHM_TH9_TH10_11AC, BIT(1), 1);
	}
}

void phydm_set_edcca_threshold(void *dm_void, s8 H2L, s8 L2H)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;

	if (dm->support_ic_type & ODM_IC_11N_SERIES)
		odm_set_bb_reg(dm, REG_OFDM_0_ECCA_THRESHOLD,
			       MASKBYTE2 | MASKBYTE0,
			       (u32)((u8)L2H | (u8)H2L << 16));
	else if (dm->support_ic_type & ODM_IC_11AC_SERIES)
		odm_set_bb_reg(dm, REG_FPGA0_XB_LSSI_READ_BACK, MASKLWORD,
			       (u16)((u8)L2H | (u8)H2L << 8));
}

static void phydm_set_lna(void *dm_void, enum phydm_set_lna type)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;

	if (dm->support_ic_type & (ODM_RTL8188E | ODM_RTL8192E)) {
		if (type == phydm_disable_lna) {
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0xef, 0x80000, 0x1);
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0x30, 0xfffff,
				       0x18000); /*select Rx mode*/
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0x31, 0xfffff,
				       0x0000f);
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0x32, 0xfffff,
				       0x37f82); /*disable LNA*/
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0xef, 0x80000, 0x0);
			if (dm->rf_type > ODM_1T1R) {
				odm_set_rf_reg(dm, ODM_RF_PATH_B, 0xef, 0x80000,
					       0x1);
				odm_set_rf_reg(dm, ODM_RF_PATH_B, 0x30, 0xfffff,
					       0x18000);
				odm_set_rf_reg(dm, ODM_RF_PATH_B, 0x31, 0xfffff,
					       0x0000f);
				odm_set_rf_reg(dm, ODM_RF_PATH_B, 0x32, 0xfffff,
					       0x37f82);
				odm_set_rf_reg(dm, ODM_RF_PATH_B, 0xef, 0x80000,
					       0x0);
			}
		} else if (type == phydm_enable_lna) {
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0xef, 0x80000, 0x1);
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0x30, 0xfffff,
				       0x18000); /*select Rx mode*/
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0x31, 0xfffff,
				       0x0000f);
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0x32, 0xfffff,
				       0x77f82); /*back to normal*/
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0xef, 0x80000, 0x0);
			if (dm->rf_type > ODM_1T1R) {
				odm_set_rf_reg(dm, ODM_RF_PATH_B, 0xef, 0x80000,
					       0x1);
				odm_set_rf_reg(dm, ODM_RF_PATH_B, 0x30, 0xfffff,
					       0x18000);
				odm_set_rf_reg(dm, ODM_RF_PATH_B, 0x31, 0xfffff,
					       0x0000f);
				odm_set_rf_reg(dm, ODM_RF_PATH_B, 0x32, 0xfffff,
					       0x77f82);
				odm_set_rf_reg(dm, ODM_RF_PATH_B, 0xef, 0x80000,
					       0x0);
			}
		}
	} else if (dm->support_ic_type & ODM_RTL8723B) {
		if (type == phydm_disable_lna) {
			/*S0*/
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0xef, 0x80000, 0x1);
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0x30, 0xfffff,
				       0x18000); /*select Rx mode*/
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0x31, 0xfffff,
				       0x0001f);
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0x32, 0xfffff,
				       0xe6137); /*disable LNA*/
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0xef, 0x80000, 0x0);
			/*S1*/
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0xed, 0x00020, 0x1);
			odm_set_rf_reg(
				dm, ODM_RF_PATH_A, 0x43, 0xfffff,
				0x3008d); /*select Rx mode and disable LNA*/
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0xed, 0x00020, 0x0);
		} else if (type == phydm_enable_lna) {
			/*S0*/
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0xef, 0x80000, 0x1);
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0x30, 0xfffff,
				       0x18000); /*select Rx mode*/
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0x31, 0xfffff,
				       0x0001f);
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0x32, 0xfffff,
				       0xe6177); /*disable LNA*/
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0xef, 0x80000, 0x0);
			/*S1*/
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0xed, 0x00020, 0x1);
			odm_set_rf_reg(
				dm, ODM_RF_PATH_A, 0x43, 0xfffff,
				0x300bd); /*select Rx mode and disable LNA*/
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0xed, 0x00020, 0x0);
		}

	} else if (dm->support_ic_type & ODM_RTL8812) {
		if (type == phydm_disable_lna) {
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0xef, 0x80000, 0x1);
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0x30, 0xfffff,
				       0x18000); /*select Rx mode*/
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0x31, 0xfffff,
				       0x3f7ff);
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0x32, 0xfffff,
				       0xc22bf); /*disable LNA*/
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0xef, 0x80000, 0x0);
			if (dm->rf_type > ODM_1T1R) {
				odm_set_rf_reg(dm, ODM_RF_PATH_B, 0xef, 0x80000,
					       0x1);
				odm_set_rf_reg(dm, ODM_RF_PATH_B, 0x30, 0xfffff,
					       0x18000); /*select Rx mode*/
				odm_set_rf_reg(dm, ODM_RF_PATH_B, 0x31, 0xfffff,
					       0x3f7ff);
				odm_set_rf_reg(dm, ODM_RF_PATH_B, 0x32, 0xfffff,
					       0xc22bf); /*disable LNA*/
				odm_set_rf_reg(dm, ODM_RF_PATH_B, 0xef, 0x80000,
					       0x0);
			}
		} else if (type == phydm_enable_lna) {
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0xef, 0x80000, 0x1);
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0x30, 0xfffff,
				       0x18000); /*select Rx mode*/
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0x31, 0xfffff,
				       0x3f7ff);
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0x32, 0xfffff,
				       0xc26bf); /*disable LNA*/
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0xef, 0x80000, 0x0);
			if (dm->rf_type > ODM_1T1R) {
				odm_set_rf_reg(dm, ODM_RF_PATH_B, 0xef, 0x80000,
					       0x1);
				odm_set_rf_reg(dm, ODM_RF_PATH_B, 0x30, 0xfffff,
					       0x18000); /*select Rx mode*/
				odm_set_rf_reg(dm, ODM_RF_PATH_B, 0x31, 0xfffff,
					       0x3f7ff);
				odm_set_rf_reg(dm, ODM_RF_PATH_B, 0x32, 0xfffff,
					       0xc26bf); /*disable LNA*/
				odm_set_rf_reg(dm, ODM_RF_PATH_B, 0xef, 0x80000,
					       0x0);
			}
		}
	} else if (dm->support_ic_type & (ODM_RTL8821 | ODM_RTL8881A)) {
		if (type == phydm_disable_lna) {
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0xef, 0x80000, 0x1);
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0x30, 0xfffff,
				       0x18000); /*select Rx mode*/
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0x31, 0xfffff,
				       0x0002f);
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0x32, 0xfffff,
				       0xfb09b); /*disable LNA*/
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0xef, 0x80000, 0x0);
		} else if (type == phydm_enable_lna) {
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0xef, 0x80000, 0x1);
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0x30, 0xfffff,
				       0x18000); /*select Rx mode*/
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0x31, 0xfffff,
				       0x0002f);
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0x32, 0xfffff,
				       0xfb0bb); /*disable LNA*/
			odm_set_rf_reg(dm, ODM_RF_PATH_A, 0xef, 0x80000, 0x0);
		}
	}
}

void phydm_set_trx_mux(void *dm_void, enum phydm_trx_mux_type tx_mode,
		       enum phydm_trx_mux_type rx_mode)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;

	if (dm->support_ic_type & ODM_IC_11N_SERIES) {
		/*set TXmod to standby mode to remove outside noise affect*/
		odm_set_bb_reg(dm, ODM_REG_CCK_RPT_FORMAT_11N,
			       BIT(3) | BIT(2) | BIT(1), tx_mode);
		/*set RXmod to standby mode to remove outside noise affect*/
		odm_set_bb_reg(dm, ODM_REG_CCK_RPT_FORMAT_11N,
			       BIT(22) | BIT(21) | BIT(20), rx_mode);
		if (dm->rf_type > ODM_1T1R) {
			/*set TXmod to standby mode to rm outside noise affect*/
			odm_set_bb_reg(dm, ODM_REG_CCK_RPT_FORMAT_11N_B,
				       BIT(3) | BIT(2) | BIT(1), tx_mode);
			/*set RXmod to standby mode to rm outside noise affect*/
			odm_set_bb_reg(dm, ODM_REG_CCK_RPT_FORMAT_11N_B,
				       BIT(22) | BIT(21) | BIT(20), rx_mode);
		}
	} else if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		/*set TXmod to standby mode to remove outside noise affect*/
		odm_set_bb_reg(dm, ODM_REG_TRMUX_11AC,
			       BIT(11) | BIT(10) | BIT(9) | BIT(8), tx_mode);
		/*set RXmod to standby mode to remove outside noise affect*/
		odm_set_bb_reg(dm, ODM_REG_TRMUX_11AC,
			       BIT(7) | BIT(6) | BIT(5) | BIT(4), rx_mode);
		if (dm->rf_type > ODM_1T1R) {
			/*set TXmod to standby mode to rm outside noise affect*/
			odm_set_bb_reg(dm, ODM_REG_TRMUX_11AC_B,
				       BIT(11) | BIT(10) | BIT(9) | BIT(8),
				       tx_mode);
			/*set RXmod to standby mode to rm outside noise affect*/
			odm_set_bb_reg(dm, ODM_REG_TRMUX_11AC_B,
				       BIT(7) | BIT(6) | BIT(5) | BIT(4),
				       rx_mode);
		}
	}
}

void phydm_mac_edcca_state(void *dm_void, enum phydm_mac_edcca_type state)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;

	if (state == phydm_ignore_edcca) {
		/*ignore EDCCA	reg520[15]=1*/
		odm_set_mac_reg(dm, REG_TX_PTCL_CTRL, BIT(15), 1);
	} else { /*don't set MAC ignore EDCCA signal*/
		/*don't ignore EDCCA	 reg520[15]=0*/
		odm_set_mac_reg(dm, REG_TX_PTCL_CTRL, BIT(15), 0);
	}
	ODM_RT_TRACE(dm, PHYDM_COMP_ADAPTIVITY, "EDCCA enable state = %d\n",
		     state);
}

bool phydm_cal_nhm_cnt(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	u16 base = 0;

	base = dm->nhm_cnt_0 + dm->nhm_cnt_1;

	if (base != 0) {
		dm->nhm_cnt_0 = ((dm->nhm_cnt_0) << 8) / base;
		dm->nhm_cnt_1 = ((dm->nhm_cnt_1) << 8) / base;
	}
	if ((dm->nhm_cnt_0 - dm->nhm_cnt_1) >= 100)
		return true; /*clean environment*/
	else
		return false; /*noisy environment*/
}

void phydm_check_environment(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct adaptivity_statistics *adaptivity =
		(struct adaptivity_statistics *)phydm_get_structure(
			dm, PHYDM_ADAPTIVITY);
	bool is_clean_environment = false;

	if (adaptivity->is_first_link) {
		if (dm->support_ic_type &
		    (ODM_IC_11AC_GAIN_IDX_EDCCA | ODM_IC_11N_GAIN_IDX_EDCCA))
			dm->adaptivity_flag = false;
		else
			dm->adaptivity_flag = true;

		adaptivity->is_first_link = false;
		return;
	}

	if (adaptivity->nhm_wait < 3) { /*Start enter NHM after 4 nhm_wait*/
		adaptivity->nhm_wait++;
		phydm_nhm_counter_statistics(dm);
		return;
	}

	phydm_nhm_counter_statistics(dm);
	is_clean_environment = phydm_cal_nhm_cnt(dm);

	if (is_clean_environment) {
		dm->th_l2h_ini =
			adaptivity->th_l2h_ini_backup; /*adaptivity mode*/
		dm->th_edcca_hl_diff = adaptivity->th_edcca_hl_diff_backup;

		dm->adaptivity_enable = true;

		if (dm->support_ic_type &
		    (ODM_IC_11AC_GAIN_IDX_EDCCA | ODM_IC_11N_GAIN_IDX_EDCCA))
			dm->adaptivity_flag = false;
		else
			dm->adaptivity_flag = true;
	} else {
		if (!adaptivity->acs_for_adaptivity) {
			dm->th_l2h_ini = dm->th_l2h_ini_mode2; /*mode2*/
			dm->th_edcca_hl_diff = dm->th_edcca_hl_diff_mode2;

			dm->adaptivity_flag = false;
			dm->adaptivity_enable = false;
		}
	}

	adaptivity->nhm_wait = 0;
	adaptivity->is_first_link = true;
	adaptivity->is_check = true;
}

void phydm_search_pwdb_lower_bound(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct adaptivity_statistics *adaptivity =
		(struct adaptivity_statistics *)phydm_get_structure(
			dm, PHYDM_ADAPTIVITY);
	u32 value32 = 0, reg_value32 = 0;
	u8 cnt, try_count = 0;
	u8 tx_edcca1 = 0, tx_edcca0 = 0;
	bool is_adjust = true;
	s8 th_l2h_dmc, th_h2l_dmc, igi_target = 0x32;
	s8 diff;
	u8 IGI = adaptivity->igi_base + 30 + (u8)dm->th_l2h_ini -
		 (u8)dm->th_edcca_hl_diff;

	if (dm->support_ic_type & (ODM_RTL8723B | ODM_RTL8188E | ODM_RTL8192E |
				   ODM_RTL8812 | ODM_RTL8821 | ODM_RTL8881A)) {
		phydm_set_lna(dm, phydm_disable_lna);
	} else {
		phydm_set_trx_mux(dm, phydm_standby_mode, phydm_standby_mode);
		odm_pause_dig(dm, PHYDM_PAUSE, PHYDM_PAUSE_LEVEL_0, 0x7e);
	}

	diff = igi_target - (s8)IGI;
	th_l2h_dmc = dm->th_l2h_ini + diff;
	if (th_l2h_dmc > 10)
		th_l2h_dmc = 10;
	th_h2l_dmc = th_l2h_dmc - dm->th_edcca_hl_diff;

	phydm_set_edcca_threshold(dm, th_h2l_dmc, th_l2h_dmc);
	ODM_delay_ms(30);

	while (is_adjust) {
		if (dm->support_ic_type & ODM_IC_11N_SERIES) {
			odm_set_bb_reg(dm, ODM_REG_DBG_RPT_11N, MASKDWORD, 0x0);
			reg_value32 =
				odm_get_bb_reg(dm, ODM_REG_RPT_11N, MASKDWORD);
		} else if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
			odm_set_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD,
				       0x0);
			reg_value32 =
				odm_get_bb_reg(dm, ODM_REG_RPT_11AC, MASKDWORD);
		}
		while (reg_value32 & BIT(3) && try_count < 3) {
			ODM_delay_ms(3);
			try_count = try_count + 1;
			if (dm->support_ic_type & ODM_IC_11N_SERIES)
				reg_value32 = odm_get_bb_reg(
					dm, ODM_REG_RPT_11N, MASKDWORD);
			else if (dm->support_ic_type & ODM_IC_11AC_SERIES)
				reg_value32 = odm_get_bb_reg(
					dm, ODM_REG_RPT_11AC, MASKDWORD);
		}
		try_count = 0;

		for (cnt = 0; cnt < 20; cnt++) {
			if (dm->support_ic_type & ODM_IC_11N_SERIES) {
				odm_set_bb_reg(dm, ODM_REG_DBG_RPT_11N,
					       MASKDWORD, 0x208);
				value32 = odm_get_bb_reg(dm, ODM_REG_RPT_11N,
							 MASKDWORD);
			} else if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
				odm_set_bb_reg(dm, ODM_REG_DBG_RPT_11AC,
					       MASKDWORD, 0x209);
				value32 = odm_get_bb_reg(dm, ODM_REG_RPT_11AC,
							 MASKDWORD);
			}
			if (value32 & BIT(30) &&
			    (dm->support_ic_type &
			     (ODM_RTL8723B | ODM_RTL8188E)))
				tx_edcca1 = tx_edcca1 + 1;
			else if (value32 & BIT(29))
				tx_edcca1 = tx_edcca1 + 1;
			else
				tx_edcca0 = tx_edcca0 + 1;
		}

		if (tx_edcca1 > 1) {
			IGI = IGI - 1;
			th_l2h_dmc = th_l2h_dmc + 1;
			if (th_l2h_dmc > 10)
				th_l2h_dmc = 10;
			th_h2l_dmc = th_l2h_dmc - dm->th_edcca_hl_diff;

			phydm_set_edcca_threshold(dm, th_h2l_dmc, th_l2h_dmc);
			if (th_l2h_dmc == 10) {
				is_adjust = false;
				adaptivity->h2l_lb = th_h2l_dmc;
				adaptivity->l2h_lb = th_l2h_dmc;
				dm->adaptivity_igi_upper = IGI;
			}

			tx_edcca1 = 0;
			tx_edcca0 = 0;

		} else {
			is_adjust = false;
			adaptivity->h2l_lb = th_h2l_dmc;
			adaptivity->l2h_lb = th_l2h_dmc;
			dm->adaptivity_igi_upper = IGI;
			tx_edcca1 = 0;
			tx_edcca0 = 0;
		}
	}

	dm->adaptivity_igi_upper = dm->adaptivity_igi_upper - dm->dc_backoff;
	adaptivity->h2l_lb = adaptivity->h2l_lb + dm->dc_backoff;
	adaptivity->l2h_lb = adaptivity->l2h_lb + dm->dc_backoff;

	if (dm->support_ic_type & (ODM_RTL8723B | ODM_RTL8188E | ODM_RTL8192E |
				   ODM_RTL8812 | ODM_RTL8821 | ODM_RTL8881A)) {
		phydm_set_lna(dm, phydm_enable_lna);
	} else {
		phydm_set_trx_mux(dm, phydm_tx_mode, phydm_rx_mode);
		odm_pause_dig(dm, PHYDM_RESUME, PHYDM_PAUSE_LEVEL_0, NONE);
	}

	phydm_set_edcca_threshold(dm, 0x7f, 0x7f); /*resume to no link state*/
}

static bool phydm_re_search_condition(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	u8 adaptivity_igi_upper;
	u8 count = 0;

	adaptivity_igi_upper = dm->adaptivity_igi_upper + dm->dc_backoff;

	if (adaptivity_igi_upper <= 0x26 && count < 3) {
		count = count + 1;
		return true;
	}

	return false;
}

void phydm_adaptivity_info_init(void *dm_void, enum phydm_adapinfo cmn_info,
				u32 value)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct adaptivity_statistics *adaptivity =
		(struct adaptivity_statistics *)phydm_get_structure(
			dm, PHYDM_ADAPTIVITY);

	switch (cmn_info) {
	case PHYDM_ADAPINFO_CARRIER_SENSE_ENABLE:
		dm->carrier_sense_enable = (bool)value;
		break;

	case PHYDM_ADAPINFO_DCBACKOFF:
		dm->dc_backoff = (u8)value;
		break;

	case PHYDM_ADAPINFO_DYNAMICLINKADAPTIVITY:
		adaptivity->dynamic_link_adaptivity = (bool)value;
		break;

	case PHYDM_ADAPINFO_TH_L2H_INI:
		dm->th_l2h_ini = (s8)value;
		break;

	case PHYDM_ADAPINFO_TH_EDCCA_HL_DIFF:
		dm->th_edcca_hl_diff = (s8)value;
		break;

	case PHYDM_ADAPINFO_AP_NUM_TH:
		adaptivity->ap_num_th = (u8)value;
		break;

	default:
		break;
	}
}

void phydm_adaptivity_init(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct adaptivity_statistics *adaptivity =
		(struct adaptivity_statistics *)phydm_get_structure(
			dm, PHYDM_ADAPTIVITY);
	s8 igi_target = 0x32;

	if (!dm->carrier_sense_enable) {
		if (dm->th_l2h_ini == 0)
			dm->th_l2h_ini = 0xf5;
	} else {
		dm->th_l2h_ini = 0xa;
	}

	if (dm->th_edcca_hl_diff == 0)
		dm->th_edcca_hl_diff = 7;
	if (dm->wifi_test || dm->mp_mode) {
		/*even no adaptivity, we still enable EDCCA, AP use mib ctrl*/
		dm->edcca_enable = false;
	} else {
		dm->edcca_enable = true;
	}

	dm->adaptivity_igi_upper = 0;
	dm->adaptivity_enable =
		false; /*use this flag to decide enable or disable*/

	dm->th_l2h_ini_mode2 = 20;
	dm->th_edcca_hl_diff_mode2 = 8;
	adaptivity->th_l2h_ini_backup = dm->th_l2h_ini;
	adaptivity->th_edcca_hl_diff_backup = dm->th_edcca_hl_diff;

	adaptivity->igi_base = 0x32;
	adaptivity->igi_target = 0x1c;
	adaptivity->h2l_lb = 0;
	adaptivity->l2h_lb = 0;
	adaptivity->nhm_wait = 0;
	adaptivity->is_check = false;
	adaptivity->is_first_link = true;
	adaptivity->adajust_igi_level = 0;
	adaptivity->is_stop_edcca = false;
	adaptivity->backup_h2l = 0;
	adaptivity->backup_l2h = 0;

	phydm_mac_edcca_state(dm, phydm_dont_ignore_edcca);

	/*Search pwdB lower bound*/
	if (dm->support_ic_type & ODM_IC_11N_SERIES)
		odm_set_bb_reg(dm, ODM_REG_DBG_RPT_11N, MASKDWORD, 0x208);
	else if (dm->support_ic_type & ODM_IC_11AC_SERIES)
		odm_set_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0x209);

	if (dm->support_ic_type & ODM_IC_11N_GAIN_IDX_EDCCA) {
		if (dm->support_ic_type & ODM_RTL8197F) {
			/*set to page B1*/
			odm_set_bb_reg(dm, ODM_REG_PAGE_B1_97F, BIT(30), 0x1);
			/*0:rx_dfir, 1: dcnf_out, 2 :rx_iq, 3: rx_nbi_nf_out*/
			odm_set_bb_reg(dm, ODM_REG_EDCCA_DCNF_97F,
				       BIT(27) | BIT(26), 0x1);
			odm_set_bb_reg(dm, ODM_REG_PAGE_B1_97F, BIT(30), 0x0);
		} else {
			/*0:rx_dfir, 1: dcnf_out, 2 :rx_iq, 3: rx_nbi_nf_out*/
			odm_set_bb_reg(dm, ODM_REG_EDCCA_DCNF_11N,
				       BIT(21) | BIT(20), 0x1);
		}
	}
	/*8814a no need to find pwdB lower bound, maybe*/
	if (dm->support_ic_type & ODM_IC_11AC_GAIN_IDX_EDCCA) {
		/*0:rx_dfir, 1: dcnf_out, 2 :rx_iq, 3: rx_nbi_nf_out*/
		odm_set_bb_reg(dm, ODM_REG_ACBB_EDCCA_ENHANCE,
			       BIT(29) | BIT(28), 0x1);
	}

	if (!(dm->support_ic_type &
	      (ODM_IC_11AC_GAIN_IDX_EDCCA | ODM_IC_11N_GAIN_IDX_EDCCA))) {
		phydm_search_pwdb_lower_bound(dm);
		if (phydm_re_search_condition(dm))
			phydm_search_pwdb_lower_bound(dm);
	}

	/*we need to consider PwdB upper bound for 8814 later IC*/
	adaptivity->adajust_igi_level =
		(u8)((dm->th_l2h_ini + igi_target) - pwdb_upper_bound +
		     dfir_loss); /*IGI = L2H - PwdB - dfir_loss*/

	ODM_RT_TRACE(
		dm, PHYDM_COMP_ADAPTIVITY,
		"th_l2h_ini = 0x%x, th_edcca_hl_diff = 0x%x, adaptivity->adajust_igi_level = 0x%x\n",
		dm->th_l2h_ini, dm->th_edcca_hl_diff,
		adaptivity->adajust_igi_level);

	/*Check this later on Windows*/
	/*phydm_set_edcca_threshold_api(dm, dig_tab->cur_ig_value);*/
}

void phydm_adaptivity(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct dig_thres *dig_tab = &dm->dm_dig_table;
	u8 IGI = dig_tab->cur_ig_value;
	s8 th_l2h_dmc, th_h2l_dmc;
	s8 diff = 0, igi_target;
	struct adaptivity_statistics *adaptivity =
		(struct adaptivity_statistics *)phydm_get_structure(
			dm, PHYDM_ADAPTIVITY);

	if (!dm->edcca_enable || adaptivity->is_stop_edcca) {
		ODM_RT_TRACE(dm, PHYDM_COMP_ADAPTIVITY, "Disable EDCCA!!!\n");
		return;
	}

	if (!(dm->support_ability & ODM_BB_ADAPTIVITY)) {
		ODM_RT_TRACE(dm, PHYDM_COMP_ADAPTIVITY,
			     "adaptivity disable, enable EDCCA mode!!!\n");
		dm->th_l2h_ini = dm->th_l2h_ini_mode2;
		dm->th_edcca_hl_diff = dm->th_edcca_hl_diff_mode2;
	}

	ODM_RT_TRACE(dm, PHYDM_COMP_ADAPTIVITY, "%s() =====>\n", __func__);
	ODM_RT_TRACE(dm, PHYDM_COMP_ADAPTIVITY,
		     "igi_base=0x%x, th_l2h_ini = %d, th_edcca_hl_diff = %d\n",
		     adaptivity->igi_base, dm->th_l2h_ini,
		     dm->th_edcca_hl_diff);
	if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		/*fix AC series when enable EDCCA hang issue*/
		odm_set_bb_reg(dm, 0x800, BIT(10), 1); /*ADC_mask disable*/
		odm_set_bb_reg(dm, 0x800, BIT(10), 0); /*ADC_mask enable*/
	}
	if (*dm->band_width == ODM_BW20M) /*CHANNEL_WIDTH_20*/
		igi_target = adaptivity->igi_base;
	else if (*dm->band_width == ODM_BW40M)
		igi_target = adaptivity->igi_base + 2;
	else if (*dm->band_width == ODM_BW80M)
		igi_target = adaptivity->igi_base + 2;
	else
		igi_target = adaptivity->igi_base;
	adaptivity->igi_target = (u8)igi_target;

	ODM_RT_TRACE(
		dm, PHYDM_COMP_ADAPTIVITY,
		"band_width=%s, igi_target=0x%x, dynamic_link_adaptivity = %d, acs_for_adaptivity = %d\n",
		(*dm->band_width == ODM_BW80M) ?
			"80M" :
			((*dm->band_width == ODM_BW40M) ? "40M" : "20M"),
		igi_target, adaptivity->dynamic_link_adaptivity,
		adaptivity->acs_for_adaptivity);
	ODM_RT_TRACE(
		dm, PHYDM_COMP_ADAPTIVITY,
		"rssi_min = %d, adaptivity->adajust_igi_level= 0x%x, adaptivity_flag = %d, adaptivity_enable = %d\n",
		dm->rssi_min, adaptivity->adajust_igi_level,
		dm->adaptivity_flag, dm->adaptivity_enable);

	if (adaptivity->dynamic_link_adaptivity && (!dm->is_linked) &&
	    !dm->adaptivity_enable) {
		phydm_set_edcca_threshold(dm, 0x7f, 0x7f);
		ODM_RT_TRACE(
			dm, PHYDM_COMP_ADAPTIVITY,
			"In DynamicLink mode(noisy) and No link, Turn off EDCCA!!\n");
		return;
	}

	if (dm->support_ic_type &
	    (ODM_IC_11AC_GAIN_IDX_EDCCA | ODM_IC_11N_GAIN_IDX_EDCCA)) {
		if ((adaptivity->adajust_igi_level > IGI) &&
		    dm->adaptivity_enable)
			diff = adaptivity->adajust_igi_level - IGI;

		th_l2h_dmc = dm->th_l2h_ini - diff + igi_target;
		th_h2l_dmc = th_l2h_dmc - dm->th_edcca_hl_diff;
	} else {
		diff = igi_target - (s8)IGI;
		th_l2h_dmc = dm->th_l2h_ini + diff;
		if (th_l2h_dmc > 10 && dm->adaptivity_enable)
			th_l2h_dmc = 10;

		th_h2l_dmc = th_l2h_dmc - dm->th_edcca_hl_diff;

		/*replace lower bound to prevent EDCCA always equal 1*/
		if (th_h2l_dmc < adaptivity->h2l_lb)
			th_h2l_dmc = adaptivity->h2l_lb;
		if (th_l2h_dmc < adaptivity->l2h_lb)
			th_l2h_dmc = adaptivity->l2h_lb;
	}
	ODM_RT_TRACE(dm, PHYDM_COMP_ADAPTIVITY,
		     "IGI=0x%x, th_l2h_dmc = %d, th_h2l_dmc = %d\n", IGI,
		     th_l2h_dmc, th_h2l_dmc);
	ODM_RT_TRACE(
		dm, PHYDM_COMP_ADAPTIVITY,
		"adaptivity_igi_upper=0x%x, h2l_lb = 0x%x, l2h_lb = 0x%x\n",
		dm->adaptivity_igi_upper, adaptivity->h2l_lb,
		adaptivity->l2h_lb);

	phydm_set_edcca_threshold(dm, th_h2l_dmc, th_l2h_dmc);

	if (dm->adaptivity_enable)
		odm_set_mac_reg(dm, REG_RD_CTRL, BIT(11), 1);
}

/*This is for solving USB can't Tx problem due to USB3.0 interference in 2.4G*/
void phydm_pause_edcca(void *dm_void, bool is_pasue_edcca)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct adaptivity_statistics *adaptivity =
		(struct adaptivity_statistics *)phydm_get_structure(
			dm, PHYDM_ADAPTIVITY);
	struct dig_thres *dig_tab = &dm->dm_dig_table;
	u8 IGI = dig_tab->cur_ig_value;
	s8 diff = 0;

	if (is_pasue_edcca) {
		adaptivity->is_stop_edcca = true;

		if (dm->support_ic_type &
		    (ODM_IC_11AC_GAIN_IDX_EDCCA | ODM_IC_11N_GAIN_IDX_EDCCA)) {
			if (adaptivity->adajust_igi_level > IGI)
				diff = adaptivity->adajust_igi_level - IGI;

			adaptivity->backup_l2h =
				dm->th_l2h_ini - diff + adaptivity->igi_target;
			adaptivity->backup_h2l =
				adaptivity->backup_l2h - dm->th_edcca_hl_diff;
		} else {
			diff = adaptivity->igi_target - (s8)IGI;
			adaptivity->backup_l2h = dm->th_l2h_ini + diff;
			if (adaptivity->backup_l2h > 10)
				adaptivity->backup_l2h = 10;

			adaptivity->backup_h2l =
				adaptivity->backup_l2h - dm->th_edcca_hl_diff;

			/*replace lower bound to prevent EDCCA always equal 1*/
			if (adaptivity->backup_h2l < adaptivity->h2l_lb)
				adaptivity->backup_h2l = adaptivity->h2l_lb;
			if (adaptivity->backup_l2h < adaptivity->l2h_lb)
				adaptivity->backup_l2h = adaptivity->l2h_lb;
		}
		ODM_RT_TRACE(
			dm, PHYDM_COMP_ADAPTIVITY,
			"pauseEDCCA : L2Hbak = 0x%x, H2Lbak = 0x%x, IGI = 0x%x\n",
			adaptivity->backup_l2h, adaptivity->backup_h2l, IGI);

		/*Disable EDCCA*/
		phydm_pause_edcca_work_item_callback(dm);

	} else {
		adaptivity->is_stop_edcca = false;
		ODM_RT_TRACE(
			dm, PHYDM_COMP_ADAPTIVITY,
			"resumeEDCCA : L2Hbak = 0x%x, H2Lbak = 0x%x, IGI = 0x%x\n",
			adaptivity->backup_l2h, adaptivity->backup_h2l, IGI);
		/*Resume EDCCA*/
		phydm_resume_edcca_work_item_callback(dm);
	}
}

void phydm_pause_edcca_work_item_callback(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;

	if (dm->support_ic_type & ODM_IC_11N_SERIES)
		odm_set_bb_reg(dm, REG_OFDM_0_ECCA_THRESHOLD,
			       MASKBYTE2 | MASKBYTE0, (u32)(0x7f | 0x7f << 16));
	else if (dm->support_ic_type & ODM_IC_11AC_SERIES)
		odm_set_bb_reg(dm, REG_FPGA0_XB_LSSI_READ_BACK, MASKLWORD,
			       (u16)(0x7f | 0x7f << 8));
}

void phydm_resume_edcca_work_item_callback(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct adaptivity_statistics *adaptivity =
		(struct adaptivity_statistics *)phydm_get_structure(
			dm, PHYDM_ADAPTIVITY);

	if (dm->support_ic_type & ODM_IC_11N_SERIES)
		odm_set_bb_reg(dm, REG_OFDM_0_ECCA_THRESHOLD,
			       MASKBYTE2 | MASKBYTE0,
			       (u32)((u8)adaptivity->backup_l2h |
				     (u8)adaptivity->backup_h2l << 16));
	else if (dm->support_ic_type & ODM_IC_11AC_SERIES)
		odm_set_bb_reg(dm, REG_FPGA0_XB_LSSI_READ_BACK, MASKLWORD,
			       (u16)((u8)adaptivity->backup_l2h |
				     (u8)adaptivity->backup_h2l << 8));
}

void phydm_set_edcca_threshold_api(void *dm_void, u8 IGI)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct adaptivity_statistics *adaptivity =
		(struct adaptivity_statistics *)phydm_get_structure(
			dm, PHYDM_ADAPTIVITY);
	s8 th_l2h_dmc, th_h2l_dmc;
	s8 diff = 0, igi_target = 0x32;

	if (dm->support_ability & ODM_BB_ADAPTIVITY) {
		if (dm->support_ic_type &
		    (ODM_IC_11AC_GAIN_IDX_EDCCA | ODM_IC_11N_GAIN_IDX_EDCCA)) {
			if (adaptivity->adajust_igi_level > IGI)
				diff = adaptivity->adajust_igi_level - IGI;

			th_l2h_dmc = dm->th_l2h_ini - diff + igi_target;
			th_h2l_dmc = th_l2h_dmc - dm->th_edcca_hl_diff;
		} else {
			diff = igi_target - (s8)IGI;
			th_l2h_dmc = dm->th_l2h_ini + diff;
			if (th_l2h_dmc > 10)
				th_l2h_dmc = 10;

			th_h2l_dmc = th_l2h_dmc - dm->th_edcca_hl_diff;

			/*replace lower bound to prevent EDCCA always equal 1*/
			if (th_h2l_dmc < adaptivity->h2l_lb)
				th_h2l_dmc = adaptivity->h2l_lb;
			if (th_l2h_dmc < adaptivity->l2h_lb)
				th_l2h_dmc = adaptivity->l2h_lb;
		}
		ODM_RT_TRACE(
			dm, PHYDM_COMP_ADAPTIVITY,
			"API :IGI=0x%x, th_l2h_dmc = %d, th_h2l_dmc = %d\n",
			IGI, th_l2h_dmc, th_h2l_dmc);
		ODM_RT_TRACE(
			dm, PHYDM_COMP_ADAPTIVITY,
			"API :adaptivity_igi_upper=0x%x, h2l_lb = 0x%x, l2h_lb = 0x%x\n",
			dm->adaptivity_igi_upper, adaptivity->h2l_lb,
			adaptivity->l2h_lb);

		phydm_set_edcca_threshold(dm, th_h2l_dmc, th_l2h_dmc);
	}
}
