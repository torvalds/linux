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
	u8 central_ch = 0;
	boolean is_2g_ch = false;

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
#if (defined(PHYDM_COMPILE_IC_4SS))
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

	central_ch = (u8)odm_get_rf_reg(dm, RF_PATH_A, RF_0x18, 0xff);
	is_2g_ch = (central_ch <= 14) ? true : false;

	if (is_single_tone) {
		/*Disable CCA*/
		if (is_2g_ch) { /*CCK RxIQ weighting = [0,0]*/
			if(dm->support_ic_type & ODM_RTL8723F) {
				odm_set_bb_reg(dm, R_0x2a24, BIT(13), 0x1); /*CCK*/
			} else {
				odm_set_bb_reg(dm, R_0x1a9c, BIT(20), 0x0);
				odm_set_bb_reg(dm, R_0x1a14, 0x300, 0x3);
			}
		}
		odm_set_bb_reg(dm, R_0x1d58, 0xff8, 0x1ff); /*OFDM*/
		if (dm->support_ic_type & ODM_RTL8723F) {
			odm_set_rf_reg(dm, RF_PATH_A, RF_0x5, BIT(0), 0x0);
			for (i = start; i <= end; i++) {
				mp->rf0[i] = odm_get_rf_reg(dm, i, RF_0x0, RFREG_MASK);
				/*Tx mode: RF0x00[19:16]=4'b0010 */
				odm_set_rf_reg(dm, i, RF_0x0, 0xF0000, 0x2);
				/*Lowest RF gain index: RF_0x1[5:0] TX power*/
				mp->rf1[i] = odm_get_rf_reg(dm, i, RF_0x1, RFREG_MASK);
				odm_set_rf_reg(dm, i, RF_0x1, 0x3f, 0x0);//TX power
				/*RF LO enabled */
				odm_set_rf_reg(dm, i, RF_0x58, BIT(1), 0x1);
			}
		} else {
			for (i = start; i <= end; i++) {
				mp->rf0[i] = odm_get_rf_reg(dm, i, RF_0x0, RFREG_MASK);
				/*Tx mode: RF0x00[19:16]=4'b0010 */
				odm_set_rf_reg(dm, i, RF_0x0, 0xF0000, 0x2);
				/*Lowest RF gain index: RF_0x0[4:0] = 0*/
				odm_set_rf_reg(dm, i, RF_0x0, 0x1f, 0x0);
				/*RF LO enabled */
				odm_set_rf_reg(dm, i, RF_0x58, BIT(1), 0x1);
			}
		}
		
		#if (RTL8814B_SUPPORT)
		if (dm->support_ic_type & ODM_RTL8814B) {
			mp->rf0_syn[RF_SYN0] = config_phydm_read_syn_reg_8814b(
					       dm, RF_SYN0, RF_0x0, RFREG_MASK);
			/*Lowest RF gain index: RF_0x0[4:0] = 0x0*/
			config_phydm_write_rf_syn_8814b(dm, RF_SYN0, RF_0x0,
							0x1f, 0x0);
			/*RF LO enabled */
			config_phydm_write_rf_syn_8814b(dm, RF_SYN0, RF_0x58,
							BIT(1), 0x1);
			/*SYN1*/
			if (*dm->band_width == CHANNEL_WIDTH_80_80) {
				mp->rf0_syn[RF_SYN1] = config_phydm_read_syn_reg_8814b(
						       dm, RF_SYN1, RF_0x0,
						       RFREG_MASK);
				config_phydm_write_rf_syn_8814b(dm, RF_SYN1,
								RF_0x0, 0x1f,
								0x0);
				config_phydm_write_rf_syn_8814b(dm, RF_SYN1,
								RF_0x58, BIT(1),
								0x1);
			}
		}
		#endif
	} else {
		/*Enable CCA*/
		if (is_2g_ch) { /*CCK RxIQ weighting = [1,1]*/
			if(dm->support_ic_type & ODM_RTL8723F) {
				odm_set_bb_reg(dm, R_0x2a24, BIT(13), 0x0); /*CCK*/ 
			} else {
				odm_set_bb_reg(dm, R_0x1a9c, BIT(20), 0x1);
				odm_set_bb_reg(dm, R_0x1a14, 0x300, 0x0);
			}
		}
		odm_set_bb_reg(dm, R_0x1d58, 0xff8, 0x0); /*OFDM*/

		if(dm->support_ic_type & ODM_RTL8723F) {
			for (i = start; i <= end; i++) {
				odm_set_rf_reg(dm, i, RF_0x0, RFREG_MASK, mp->rf0[i]);
				odm_set_rf_reg(dm, i, RF_0x1, RFREG_MASK, mp->rf1[i]);
				/*RF LO disabled */
				odm_set_rf_reg(dm, i, RF_0x58, BIT(1), 0x0);
			}
			odm_set_rf_reg(dm, RF_PATH_A, RF_0x5, BIT(0), 0x1);
		} else {
			for (i = start; i <= end; i++) {
				odm_set_rf_reg(dm, i, RF_0x0, RFREG_MASK, mp->rf0[i]);
				/*RF LO disabled */
				odm_set_rf_reg(dm, i, RF_0x58, BIT(1), 0x0);
			}
		}
		#if (RTL8814B_SUPPORT)
		if (dm->support_ic_type & ODM_RTL8814B) {
			config_phydm_write_rf_syn_8814b(dm, RF_SYN0, RF_0x0,
							RFREG_MASK,
							mp->rf0_syn[RF_SYN0]);
			config_phydm_write_rf_syn_8814b(dm, RF_SYN0, RF_0x58,
							BIT(1), 0x0);
			/*SYN1*/
			if (*dm->band_width == CHANNEL_WIDTH_80_80) {
				config_phydm_write_rf_syn_8814b(dm, RF_SYN1,
								RF_0x0,
								RFREG_MASK,
								mp->rf0_syn[RF_SYN1]);
				config_phydm_write_rf_syn_8814b(dm, RF_SYN1,
								RF_0x58, BIT(1),
								0x0);
			}
		}
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
			/*if CCK block on? */
			if (!odm_get_bb_reg(dm, R_0x1c3c, BIT(1)))
				odm_set_bb_reg(dm, R_0x1c3c, BIT(1), 1);

			if(dm->support_ic_type & ODM_RTL8723F){
				/* @Carrier suppress tx */
				odm_set_bb_reg(dm, R_0x2a08, BIT(18), 0x1);
				/*turn off scramble setting */
				odm_set_bb_reg(dm, R_0x2a04, BIT(5), 0x1);
				/*Set CCK Tx Test Rate, set TxRate to 2Mbps */
				odm_set_bb_reg(dm, R_0x2a08, 0x300000, 0x1);
				/* BB and PMAC cont tx */
				odm_set_bb_reg(dm, R_0x2a08, BIT(17), 0x1);
				odm_set_bb_reg(dm, R_0x2a00, BIT(28), 0x1);
				/* TX CCK ON */
				odm_set_bb_reg(dm, R_0x2a08, BIT(31), 0x0);
				odm_set_bb_reg(dm, R_0x2a08, BIT(31), 0x1);
			}
			else {
				/*Turn Off All Test mode */
				odm_set_bb_reg(dm, R_0x1ca4, 0x7, 0x0);
				
				/*transmit mode */
				odm_set_bb_reg(dm, R_0x1a00, 0x3, 0x2);	
				/*turn off scramble setting */
				odm_set_bb_reg(dm, R_0x1a00, BIT(3), 0x0);
				/*Set CCK Tx Test Rate, set TxRate to 1Mbps */
				odm_set_bb_reg(dm, R_0x1a00, 0x3000, 0x0);
			}
		}
	} else { /*Stop Carrier Suppression. */
		if (phydm_is_cck_rate(dm, (u8)rate_index)) {
			if(dm->support_ic_type & ODM_RTL8723F) {
				/* TX Stop */
				odm_set_bb_reg(dm, R_0x2a00, BIT(0), 0x1);
				/* Clear BB cont tx */
				odm_set_bb_reg(dm, R_0x2a00, BIT(28), 0x0);
				/* Clear PMAC cont tx */
				odm_set_bb_reg(dm, R_0x2a08, BIT(17), 0x0);
				/* Clear TX Stop */
				odm_set_bb_reg(dm, R_0x2a00, BIT(0), 0x0);
				/* normal mode */
				odm_set_bb_reg(dm, R_0x2a08, BIT(18), 0x0);
				/* turn on scramble setting */
				odm_set_bb_reg(dm, R_0x2a04, BIT(5), 0x0);
			}
			else {
				/*normal mode */
				odm_set_bb_reg(dm, R_0x1a00, 0x3, 0x0);			
				/*turn on scramble setting */
				odm_set_bb_reg(dm, R_0x1a00, BIT(3), 0x1);
			}
			/*BB Reset */
			odm_set_bb_reg(dm, R_0x1d0c, BIT(16), 0x0);
			odm_set_bb_reg(dm, R_0x1d0c, BIT(16), 0x1);
		}
	}
}

void phydm_mp_set_single_carrier_jgr3(void *dm_void, boolean is_single_carrier)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_mp *mp = &dm->dm_mp_table;

	if (is_single_carrier) {
		/*1. if OFDM block on? */
		if (!odm_get_bb_reg(dm, R_0x1c3c, BIT(0)))
			odm_set_bb_reg(dm, R_0x1c3c, BIT(0), 1);
		
		if (dm->support_ic_type & ODM_RTL8723F) {
			/*3. turn on scramble setting */
			odm_set_bb_reg(dm, R_0x2a04, BIT(5), 0);
			/*4. Turn On single carrier. */
			odm_set_bb_reg(dm, R_0x1ca4, 0x7, OFDM_SINGLE_CARRIER);
		}
		else {
			/*2. set CCK test mode off, set to CCK normal mode */
			odm_set_bb_reg(dm, R_0x1a00, 0x3, 0);
			/*3. turn on scramble setting */
			odm_set_bb_reg(dm, R_0x1a00, BIT(3), 1);
			/*4. Turn On single carrier. */
			odm_set_bb_reg(dm, R_0x1ca4, 0x7, OFDM_SINGLE_CARRIER);
		}
	} else {
		/*Turn off all test modes. */
		odm_set_bb_reg(dm, R_0x1ca4, 0x7, OFDM_OFF);

		/*Delay 10 ms */
		ODM_delay_ms(10);

		/*BB Reset*/
		odm_set_bb_reg(dm, R_0x1d0c, BIT(16), 0x0);
		odm_set_bb_reg(dm, R_0x1d0c, BIT(16), 0x1);
	}
}

void phydm_mp_get_tx_ok_jgr3(void *dm_void, u32 rate_index)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_mp *mp = &dm->dm_mp_table;

	if (phydm_is_cck_rate(dm, (u8)rate_index))
		mp->tx_phy_ok_cnt = odm_get_bb_reg(dm, R_0x2de4, MASKLWORD);
	else
		mp->tx_phy_ok_cnt = odm_get_bb_reg(dm, R_0x2de0, MASKLWORD);
}

void phydm_mp_get_rx_ok_jgr3(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_mp *mp = &dm->dm_mp_table;

	u32 cck_ok = 0, ofdm_ok = 0, ht_ok = 0, vht_ok = 0;
	u32 cck_err = 0, ofdm_err = 0, ht_err = 0, vht_err = 0;
	if(dm->support_ic_type & ODM_RTL8723F)
		cck_ok = odm_get_bb_reg(dm, R_0x2aac, MASKLWORD);
	else
		cck_ok = odm_get_bb_reg(dm, R_0x2c04, MASKLWORD);
	ofdm_ok = odm_get_bb_reg(dm, R_0x2c14, MASKLWORD);
	ht_ok = odm_get_bb_reg(dm, R_0x2c10, MASKLWORD);
	vht_ok = odm_get_bb_reg(dm, R_0x2c0c, MASKLWORD);
	if(dm->support_ic_type & ODM_RTL8723F)
		cck_err = odm_get_bb_reg(dm, R_0x2aac, MASKHWORD);
	else
		cck_err = odm_get_bb_reg(dm, R_0x2c04, MASKHWORD);
	ofdm_err = odm_get_bb_reg(dm, R_0x2c14, MASKHWORD);
	ht_err = odm_get_bb_reg(dm, R_0x2c10, MASKHWORD);
	vht_err = odm_get_bb_reg(dm, R_0x2c0c, MASKHWORD);

	mp->rx_phy_ok_cnt = cck_ok + ofdm_ok + ht_ok + vht_ok;
	mp->rx_phy_crc_err_cnt = cck_err + ofdm_err + ht_err + vht_err;
	mp->io_value = (u32)mp->rx_phy_ok_cnt;
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

	if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
		phydm_mp_set_single_carrier_jgr3(dm, is_single_carrier);
}
void phydm_mp_reset_rx_counters_phy(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	phydm_reset_bb_hw_cnt(dm);
}

void phydm_mp_get_tx_ok(void *dm_void, u32 rate_index)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
		phydm_mp_get_tx_ok_jgr3(dm, rate_index);
}

void phydm_mp_get_rx_ok(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
		phydm_mp_get_rx_ok_jgr3(dm);
}
#endif
