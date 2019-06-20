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

/*@************************************************************
 * include files
 ************************************************************/

#include "mp_precomp.h"
#include "phydm_precomp.h"

#ifdef PHYDM_MP_SUPPORT
#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT

void phydm_mp_set_single_tone_jgr3(void *dm_void, boolean is_single_tone,
				   u8 path)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_mp *mp = &dm->dm_mp_table;
	u8 start = RF_PATH_A, end = RF_PATH_A;
	u8 i = 0;

	switch (path) {
	case RF_PATH_A:
	case RF_PATH_B:
	case RF_PATH_C:
	case RF_PATH_D:
		start = path;
		end = path;
		break;
	case RF_PATH_AB:
		start = RF_PATH_A;
		end = RF_PATH_B;
		break;
#if (RTL8814B_SUPPORT == 1 || RTL8198F_SUPPORT == 1)
	case RF_PATH_AC:
		start = RF_PATH_A;
		end = RF_PATH_C;
		break;
	case RF_PATH_AD:
		start = RF_PATH_A;
		end = RF_PATH_D;
		break;
	case RF_PATH_BC:
		start = RF_PATH_B;
		end = RF_PATH_C;
		break;
	case RF_PATH_BD:
		start = RF_PATH_B;
		end = RF_PATH_D;
		break;
	case RF_PATH_CD:
		start = RF_PATH_C;
		end = RF_PATH_D;
		break;
	case RF_PATH_ABC:
		start = RF_PATH_A;
		end = RF_PATH_C;
		break;
	case RF_PATH_ABD:
		start = RF_PATH_A;
		end = RF_PATH_D;
		break;
	case RF_PATH_ACD:
		start = RF_PATH_A;
		end = RF_PATH_D;
		break;
	case RF_PATH_BCD:
		start = RF_PATH_B;
		end = RF_PATH_D;
		break;
	case RF_PATH_ABCD:
		start = RF_PATH_A;
		end = RF_PATH_D;
		break;
#endif
	}
	if (is_single_tone) {
		mp->rf_reg0 = odm_get_rf_reg(dm, RF_PATH_A, RF_0x00, 0xfffff);
#if 0
		mp->rfe_sel_a_0 = odm_get_bb_reg(dm, R_0x1840, MASKDWORD);
		mp->rfe_sel_b_0 = odm_get_bb_reg(dm, R_0x4140, MASKDWORD);
		mp->rfe_sel_c_0 = odm_get_bb_reg(dm, R_0x5240, MASKDWORD);
		mp->rfe_sel_d_0 = odm_get_bb_reg(dm, R_0x5340, MASKDWORD);
		mp->rfe_sel_a_1 = odm_get_bb_reg(dm, R_0x1844, MASKDWORD);
		mp->rfe_sel_b_1 = odm_get_bb_reg(dm, R_0x4144, MASKDWORD);
		mp->rfe_sel_c_1 = odm_get_bb_reg(dm, R_0x5244, MASKDWORD);
		mp->rfe_sel_d_1 = odm_get_bb_reg(dm, R_0x5344, MASKDWORD);
#endif
		/* Disable CCK and OFDM */
		odm_set_bb_reg(dm, R_0x1c3c, 0x3, 0x0);
		for (i = start; i <= end; i++) {
			/* @Tx mode: RF0x00[19:16]=4'b0010 */
			odm_set_rf_reg(dm, i, RF_0x0, 0xF0000, 0x2);
			/* @Lowest RF gain index: RF_0x0[4:0] = 0*/
			odm_set_rf_reg(dm, i, RF_0x0, 0x1F, 0x0);
			/* @RF LO enabled */
			odm_set_rf_reg(dm, i, RF_0x58, BIT(1), 0x1);
		}
		#if (RTL8814B_SUPPORT)
		if (dm->support_ic_type & ODM_RTL8814B) {
			/* @Tx mode: RF0x00[19:16]=4'b0010 */
			config_phydm_write_rf_syn_8814b(dm, RF_SYN0, RF_0x0,
							0xF0000, 0x2);
			/* @Lowest RF gain index: RF_0x0[4:0] = 0*/
			config_phydm_write_rf_syn_8814b(dm, RF_SYN0, RF_0x0,
							0x1F, 0x0);
			/* @RF LO enabled */
			config_phydm_write_rf_syn_8814b(dm, RF_SYN0, RF_0x58,
							BIT(1), 0x1);
		}
		#endif
	} else {
		/* Eable CCK and OFDM */
		odm_set_bb_reg(dm, R_0x1c3c, 0x3, 0x3);
		if (!(dm->support_ic_type & ODM_RTL8814B)) {
			for (i = start; i <= end; i++) {
				odm_set_rf_reg(dm, i, RF_0x00, 0xfffff,
					       mp->rf_reg0);
				/* RF LO disabled */
				odm_set_rf_reg(dm, i, RF_0x58, BIT(1), 0x0);
			}
		}
#if 0
		odm_set_bb_reg(dm, R_0x1840, MASKDWORD, mp->rfe_sel_a_0);
		odm_set_bb_reg(dm, R_0x4140, MASKDWORD, mp->rfe_sel_b_0);
		odm_set_bb_reg(dm, R_0x5240, MASKDWORD, mp->rfe_sel_c_0);
		odm_set_bb_reg(dm, R_0x5340, MASKDWORD, mp->rfe_sel_d_0);
		odm_set_bb_reg(dm, R_0x1844, MASKDWORD, mp->rfe_sel_a_1);
		odm_set_bb_reg(dm, R_0x4144, MASKDWORD, mp->rfe_sel_b_1);
		odm_set_bb_reg(dm, R_0x5244, MASKDWORD, mp->rfe_sel_c_1);
		odm_set_bb_reg(dm, R_0x5344, MASKDWORD, mp->rfe_sel_d_1);
#endif
	}
}

void phydm_mp_set_carrier_supp_jgr3(void *dm_void, boolean is_carrier_supp,
				    u32 rate_index)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_mp *mp = &dm->dm_mp_table;

	if (is_carrier_supp) {
		if (phydm_is_cck_rate(dm, (u8)rate_index)) {
			/* @if CCK block on? */
			if (!odm_get_bb_reg(dm, R_0x1c3c, BIT(1)))
				odm_set_bb_reg(dm, R_0x1c3c, BIT(1), 1);

			/* @Turn Off All Test mode */
			odm_set_bb_reg(dm, R_0x1ca4, 0x7, 0x0);

			/* @transmit mode */
			odm_set_bb_reg(dm, R_0x1a00, 0x3, 0x2);
			/* @turn off scramble setting */
			odm_set_bb_reg(dm, R_0x1a00, 0x8, 0x0);
			/* @Set CCK Tx Test Rate, set FTxRate to 1Mbps */
			odm_set_bb_reg(dm, R_0x1a00, 0x3000, 0x0);
		}
	} else { /* @Stop Carrier Suppression. */
		if (phydm_is_cck_rate(dm, (u8)rate_index)) {
			/* @normal mode */
			odm_set_bb_reg(dm, R_0x1a00, 0x3, 0x0);
			/* @turn on scramble setting */
			odm_set_bb_reg(dm, R_0x1a00, 0x8, 0x1);
			/* @BB Reset */
			odm_set_bb_reg(dm, R_0x1d0c, 0x10000, 0x0);
			odm_set_bb_reg(dm, R_0x1d0c, 0x10000, 0x1);
		}
	}
}
#endif

void phydm_mp_set_crystal_cap(void *dm_void, u8 crystal_cap)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	phydm_set_crystal_cap(dm, crystal_cap);
}

void phydm_mp_set_single_tone(void *dm_void, boolean is_single_tone, u8 path)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
		phydm_mp_set_single_tone_jgr3(dm, is_single_tone, path);
}

void phydm_mp_set_carrier_supp(void *dm_void, boolean is_carrier_supp,
			       u32 rate_index)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
		phydm_mp_set_carrier_supp_jgr3(dm, is_carrier_supp, rate_index);
}

void phydm_mp_set_single_carrier(void *dm_void, boolean is_single_carrier)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_mp *mp = &dm->dm_mp_table;

	if (is_single_carrier) {
		if (dm->support_ic_type & ODM_IC_JGR3_SERIES) {
			/* @1. if OFDM block on? */
			if (!odm_get_bb_reg(dm, R_0x1c3c, BIT(0)))
				odm_set_bb_reg(dm, R_0x1c3c, BIT(0), 1);

			/* @2. set CCK test mode off, set to CCK normal mode */
			odm_set_bb_reg(dm, R_0x1a00, 0x3, 0);

			/* @3. turn on scramble setting */
			odm_set_bb_reg(dm, R_0x1a00, 0x8, 1);

			/* @4. Turn On single carrier. */
			odm_set_bb_reg(dm, R_0x1ca4, 0x7, OFDM_SINGLE_CARRIER);
		} else {
			/* @1. if OFDM block on? */
			if (!odm_get_bb_reg(dm, R_0x800, 0x2000000))
				odm_set_bb_reg(dm, R_0x800, 0x2000000, 1);

			/* @2. set CCK test mode off, set to CCK normal mode */
			odm_set_bb_reg(dm, R_0xa00, 0x3, 0);

			/* @3. turn on scramble setting */
			odm_set_bb_reg(dm, R_0xa00, 0x8, 1);

			/* @4. Turn On single carrier. */
			if (dm->support_ic_type & ODM_IC_11AC_SERIES)
				odm_set_bb_reg(dm, R_0x914, 0x70000,
					       OFDM_SINGLE_CARRIER);
			else if (dm->support_ic_type & ODM_IC_11N_SERIES)
				odm_set_bb_reg(dm, R_0xd00, 0x70000000,
					       OFDM_SINGLE_CARRIER);
		}
	} else { /* @Stop Single Carrier. */
	    /* @Turn off all test modes. */
		if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
			odm_set_bb_reg(dm, R_0x1ca4, 0x7, OFDM_OFF);
		else if (dm->support_ic_type & ODM_IC_11AC_SERIES)
			odm_set_bb_reg(dm, R_0x914, 0x70000, OFDM_OFF);
		else if (dm->support_ic_type & ODM_IC_11N_SERIES)
			odm_set_bb_reg(dm, R_0xd00, 0x70000000, OFDM_OFF);
		/* @Delay 10 ms */
		ODM_delay_ms(10);

		/* @BB Reset */
		if (dm->support_ic_type & ODM_IC_JGR3_SERIES) {
			odm_set_bb_reg(dm, R_0x1d0c, 0x10000, 0x0);
			odm_set_bb_reg(dm, R_0x1d0c, 0x10000, 0x1);
		} else {
			odm_set_bb_reg(dm, R_0x100, 0x100, 0x0);
			odm_set_bb_reg(dm, R_0x100, 0x100, 0x1);
		}
	} 
}
void phydm_mp_reset_rx_counters_phy(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	phydm_reset_bb_hw_cnt(dm);
}

void phydm_mp_get_tx_ok(void *dm_void, u32 rate_index)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_mp *mp = &dm->dm_mp_table;

	if (dm->support_ic_type & ODM_IC_JGR3_SERIES) {
		if (phydm_is_cck_rate(dm, (u8)rate_index))
			mp->tx_phy_ok_cnt = odm_get_bb_reg(dm, R_0x2de4,
							   0xffff);
		else
			mp->tx_phy_ok_cnt = odm_get_bb_reg(dm, R_0x2de0,
							   0xffff);
	} else {
		if (phydm_is_cck_rate(dm, (u8)rate_index))
			mp->tx_phy_ok_cnt = odm_get_bb_reg(dm, R_0xf50,
							   0xffff);
		else
			mp->tx_phy_ok_cnt = odm_get_bb_reg(dm, R_0xf50,
							   0xffff0000);
	}
}

void phydm_mp_get_rx_ok(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_mp *mp = &dm->dm_mp_table;	

	u32 cck_ok = 0, ofdm_ok = 0, ht_ok = 0, vht_ok = 0;	
	u32 cck_err = 0, ofdm_err = 0, ht_err = 0, vht_err = 0;

	if (dm->support_ic_type & ODM_IC_JGR3_SERIES) {
		cck_ok = odm_get_bb_reg(dm, R_0x2c04, 0xffff);
		ofdm_ok = odm_get_bb_reg(dm, R_0x2c14, 0xffff);
		ht_ok = odm_get_bb_reg(dm, R_0x2c10, 0xffff);
		vht_ok = odm_get_bb_reg(dm, R_0x2c0c, 0xffff);
  
		cck_err = odm_get_bb_reg(dm, R_0x2c04, 0xffff0000);
		ofdm_err = odm_get_bb_reg(dm, R_0x2c14, 0xffff0000);
		ht_err = odm_get_bb_reg(dm, R_0x2c10, 0xffff0000);
		vht_err = odm_get_bb_reg(dm, R_0x2c0c, 0xffff0000);
	} else if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		cck_ok = odm_get_bb_reg(dm, R_0xf04, 0x3FFF);
		ofdm_ok = odm_get_bb_reg(dm, R_0xf14, 0x3FFF);
		ht_ok = odm_get_bb_reg(dm, R_0xf10, 0x3FFF);
		vht_ok = odm_get_bb_reg(dm, R_0xf0c, 0x3FFF);
  
		cck_err = odm_get_bb_reg(dm, R_0xf04, 0x3FFF0000);
		ofdm_err = odm_get_bb_reg(dm, R_0xf14, 0x3FFF0000);
		ht_err = odm_get_bb_reg(dm, R_0xf10, 0x3FFF0000);
		vht_err = odm_get_bb_reg(dm, R_0xf0c, 0x3FFF0000);
	} else if (dm->support_ic_type & ODM_IC_11N_SERIES) {
		cck_ok = odm_get_bb_reg(dm, R_0xf88, MASKDWORD);
		ofdm_ok = odm_get_bb_reg(dm, R_0xf94, 0xffff);
		ht_ok = odm_get_bb_reg(dm, R_0xf90, 0xffff);
	    
		cck_err = odm_get_bb_reg(dm, R_0xf84, MASKDWORD);
		ofdm_err = odm_get_bb_reg(dm, R_0xf94, 0xffff0000);
		ht_err = odm_get_bb_reg(dm, R_0xf90, 0xffff0000);
	}

	mp->rx_phy_ok_cnt = cck_ok + ofdm_ok + ht_ok + vht_ok;
	mp->rx_phy_crc_err_cnt = cck_err + ofdm_err + ht_err + vht_err;
	mp->io_value = (u32)mp->rx_phy_ok_cnt;
}
#endif
