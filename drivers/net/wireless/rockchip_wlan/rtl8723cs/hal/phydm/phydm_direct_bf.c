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
 ***************************************************************/

#include "mp_precomp.h"
#include "phydm_precomp.h"
#ifdef CONFIG_DIRECTIONAL_BF
#ifdef PHYDM_COMPILE_IC_2SS
void phydm_iq_gen_en(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	enum rf_path i = RF_PATH_A;
	enum rf_path path = RF_PATH_A;

	#if (ODM_IC_11AC_SERIES_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8822B) {
		for (i = RF_PATH_A; i <= RF_PATH_B; i++) {
			/*RF mode table write enable*/
			odm_set_rf_reg(dm, path, RF_0xef, BIT(19), 0x1);
			/*Select RX mode*/
			odm_set_rf_reg(dm, path, RF_0x33, 0xF, 3);
			/*Set Table data*/
			odm_set_rf_reg(dm, path, RF_0x3e, 0xfffff, 0x00036);
			/*Set Table data*/
			odm_set_rf_reg(dm, path, RF_0x3f, 0xfffff, 0x5AFCE);
			/*RF mode table write disable*/
			odm_set_rf_reg(dm, path, RF_0xef, BIT(19), 0x0);
		}
	}
	#endif

	#if (ODM_IC_11N_SERIES_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8192F) {
		/*RF mode table write enable*/
		odm_set_rf_reg(dm, RF_PATH_A, RF_0xef, 0x80000, 0x1);
		odm_set_rf_reg(dm, RF_PATH_B, RF_0xef, 0x80000, 0x1);
		/* Path A */
		odm_set_rf_reg(dm, RF_PATH_A, RF_0x30, 0xfffff, 0x08000);
		odm_set_rf_reg(dm, RF_PATH_A, RF_0x31, 0xfffff, 0x0005f);
		odm_set_rf_reg(dm, RF_PATH_A, RF_0x32, 0xfffff, 0x01042);
		odm_set_rf_reg(dm, RF_PATH_A, RF_0x30, 0xfffff, 0x18000);
		odm_set_rf_reg(dm, RF_PATH_A, RF_0x31, 0xfffff, 0x0004f);
		odm_set_rf_reg(dm, RF_PATH_A, RF_0x32, 0xfffff, 0x71fc2);
		/* Path B */
		odm_set_rf_reg(dm, RF_PATH_B, RF_0x30, 0xfffff, 0x08000);
		odm_set_rf_reg(dm, RF_PATH_B, RF_0x31, 0xfffff, 0x00050);
		odm_set_rf_reg(dm, RF_PATH_B, RF_0x32, 0xfffff, 0x01042);
		odm_set_rf_reg(dm, RF_PATH_B, RF_0x30, 0xfffff, 0x18000);
		odm_set_rf_reg(dm, RF_PATH_B, RF_0x31, 0xfffff, 0x00040);
		odm_set_rf_reg(dm, RF_PATH_B, RF_0x32, 0xfffff, 0x71fc2);
		/*RF mode table write disable*/
		odm_set_rf_reg(dm, RF_PATH_A, RF_0xef, 0x80000, 0x0);
		odm_set_rf_reg(dm, RF_PATH_B, RF_0xef, 0x80000, 0x0);
	}
	#endif
}

void phydm_dis_cdd(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	#if (ODM_IC_11AC_SERIES_SUPPORT)
	if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		odm_set_bb_reg(dm, R_0x808, 0x3ffff00, 0);
		odm_set_bb_reg(dm, R_0x9ac, 0x1fff, 0);
		odm_set_bb_reg(dm, R_0x9ac, BIT(13), 1);
	}
	#endif
	#if (ODM_IC_11N_SERIES_SUPPORT)
	if (dm->support_ic_type & ODM_IC_11N_SERIES) {
		odm_set_bb_reg(dm, R_0x90c, 0xffffffff, 0x83321333);
		/* Set Tx delay setting for CCK pathA,B*/
		odm_set_bb_reg(dm, R_0xa2c, 0xf0000000, 0);
		/*Enable Tx CDD for HT part when spatial expansion is applied*/
		odm_set_bb_reg(dm, R_0xd00, BIT(8), 0);
		/* Tx CDD for Legacy*/
		odm_set_bb_reg(dm, R_0xd04, 0xf0000, 0);
		/* Tx CDD for non-HT*/
		odm_set_bb_reg(dm, R_0xd0c, 0x3c0, 0);
		/* Tx CDD for HT SS1*/
		odm_set_bb_reg(dm, R_0xd0c, 0xf8000, 0);
	}
	#endif
}

void phydm_pathb_q_matrix_rotate_en(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	phydm_iq_gen_en(dm);

	/*#ifdef PHYDM_COMMON_API_SUPPORT*/
	/*path selection is controlled by driver*/
	#if 0
	if (!phydm_api_trx_mode(dm, BB_PATH_AB, BB_PATH_AB, BB_PATH_AB))
		return;
	#endif

	phydm_dis_cdd(dm);
	phydm_pathb_q_matrix_rotate(dm, 0);

	#if (ODM_IC_11AC_SERIES_SUPPORT)
	if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		/*Set Q matrix r_v11 =1*/
		odm_set_bb_reg(dm, R_0x195c, MASKDWORD, 0x40000);
		/*Set Q matrix enable*/
		odm_set_bb_reg(dm, R_0x191c, BIT(7), 1);
	}
	#endif
}

void phydm_pathb_q_matrix_rotate(void *dm_void, u16 idx)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	#if (ODM_IC_11AC_SERIES_SUPPORT)
	u32 phase_table_0[ANGLE_NUM] = {0x40000, 0x376CF, 0x20000, 0x00000,
					0xFE0000, 0xFC8930, 0xFC0000,
					0xFC8930, 0xFDFFFF, 0x000000,
					0x020000, 0x0376CF};
	u32 phase_table_1[ANGLE_NUM] = {0x00000, 0x1FFFF, 0x376CF, 0x40000,
					0x0376CF, 0x01FFFF, 0x000000,
					0xFDFFFF, 0xFC8930, 0xFC0000,
					0xFC8930, 0xFDFFFF};
	#endif
	#if (ODM_IC_11N_SERIES_SUPPORT)
	u32 phase_table_n_0[ANGLE_NUM] = {0x00, 0x0B, 0x02, 0x00, 0x02, 0x02,
					  0x04, 0x02, 0x0D, 0x09, 0x04, 0x0B};
	u32 phase_table_n_1[ANGLE_NUM] = {0x40000100, 0x377F00DD, 0x201D8880,
					  0x00000000, 0xE01D8B80, 0xC8BF0322,
					  0xC000FF00, 0xC8BF0322, 0xDFE2777F,
					  0xFFC003FF, 0x20227480, 0x377F00DD};
	u32 phase_table_n_2[ANGLE_NUM] = {0x00, 0x1E, 0x3C, 0x4C, 0x3C, 0x1E,
					  0x0F, 0xD2, 0xC3, 0xC4, 0xC3, 0xD2};
	#endif
	if (idx >= ANGLE_NUM) {
		pr_debug("[%s]warning Phase Set Error: %d\n", __func__, idx);
		return;
	}

	switch (dm->ic_ip_series) {
	#if (ODM_IC_11AC_SERIES_SUPPORT == 1)
	case PHYDM_IC_AC:
		/*Set Q matrix r_v21*/
		odm_set_bb_reg(dm, R_0x1954, 0xffffff, phase_table_0[idx]);
		odm_set_bb_reg(dm, R_0x1950, 0xffffff, phase_table_1[idx]);
		break;
	#endif

	#if (ODM_IC_11N_SERIES_SUPPORT == 1)
	case PHYDM_IC_N:
		/*Set Q matrix r_v21*/
		odm_set_bb_reg(dm, R_0xc4c, 0xff000000, phase_table_n_0[idx]);
		odm_set_bb_reg(dm, R_0xc88, 0xffffffff, phase_table_n_1[idx]);
		odm_set_bb_reg(dm, R_0xc9c, 0xff000000, phase_table_n_2[idx]);
		break;
	#endif

	default:
		break;
	}
}

/*Before use this API, Fill correct Tx Des. and Disable STBC in advance*/
void phydm_set_direct_bfer(void *dm_void, u16 phs_idx, u8 su_idx)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
#if (RTL8822B_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8822B) {
#if 0
		u8 phi[13] = {0x0, 0x5, 0xa, 0xf, 0x15, 0x1a, 0x1f, 0x25,
			      0x2a, 0x2f, 0x35, 0x3a, 0x0};
		u8 psi[13] = {0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7,
			      0x7, 0x7, 0x7, 0x7};
		u16 psiphi[13] = {0x1c0, 0x1c5, 0x1ca, 0x1cf, 0x1d5, 0x1da,
				  0x1df, 0x1e5, 0x1ea, 0x1ef, 0x1f5, 0x1fa,
				  0x1c0}; //{Psi_4bit, Phi_6bit} of 0~360
#endif
		u16 ns[3] = {52, 108, 234}; //20/40/80 MHz subcarrier number
		u16 psiphi[13] = {0x1c0, 0x1c5, 0x1ca, 0x1cf, 0x1d5, 0x1da,
				  0x1df, 0x1e5, 0x1ea, 0x1ef, 0x1f5, 0x1fa,
				  0x1c0}; //{Psi_4bit, Phi_6bit} of 0~360
		u16 psiphiR;
		u8 i;
		u8 snr = 0x12; // for 1SS BF
		u8 nc = 0x0; //bit 2-0
		u8 nr = 0x1; //bit 5-3
		u8 ng = 0x0; //bit 7-6
		u8 cb = 0x1; //bit 9-8; 1 => phi:6, psi:4;
		u32 bw = odm_get_bb_reg(dm, R_0x8ac, 0x3); //bit 11-10
		u8 userid = su_idx; //bit 12
		u32 csi_report = 0x0;
		u32 ndp_bw = odm_get_bb_reg(dm, R_0x8ac, 0x3); //bit 11-10
		u8 ndp_sc = 0; //bit 11-10
		u32 ndp_info = 0x0;

		u16 mem_num = 0;
		u8 mem_move = 0;
		u8 mem_sel = 0;
		u16 mem_addr = 0;
		u32 dw0, dw1;
		u64 vm_info = 0;
		u64 temp = 0;
		u8 vm_cnt = 0;

		mem_num = ((8 + (6 + 4) * ns[bw]) >> 6) + 1; // SU codebook 1

		/* setting NDP BW/SC info*/
		ndp_info = (ndp_bw & 0x3)  | (ndp_bw & 0x3) << 6 |
			   (ndp_bw & 0x3) << 12 | (ndp_sc & 0xf) << 2 |
			   (ndp_sc & 0xf) << 8 | (ndp_sc & 0xf) << 14;
		odm_set_bb_reg(dm, R_0xb58, 0x000FFFFC, ndp_info);
		odm_set_bb_reg(dm, R_0x19f8, 0x00010000, 1);
		ODM_delay_ms(1); // delay 1ms
		odm_set_bb_reg(dm, R_0x19f8, 0x00010000, 0);

		/* setting CSI report info*/
		csi_report = (userid & 0x1) << 12 | (bw & 0x3) << 10 |
			     (cb & 0x3) << 8 | (ng & 0x3) << 6 |
			     (nr & 0x7) << 3 | (nc & 0x7);
		odm_set_bb_reg(dm, R_0x72c, 0x1FFF, csi_report);
		odm_set_bb_reg(dm, R_0x71c, 0x80000000, 1);
		PHYDM_DBG(dm, DBG_TXBF, "[%s] direct BF csi report 0x%x\n",
			  __func__, csi_report);
		/*========================*/

		odm_set_bb_reg(dm, R_0x19b8, 0x40, 1); //0x19b8[6]:1 to csi_rpt
		odm_set_bb_reg(dm, R_0x19e0, 0x3FC0, 0xFF); //gated_clk off
		odm_set_bb_reg(dm, R_0x9e8, 0x2000000, 1); //abnormal txbf
		odm_set_bb_reg(dm, R_0x9e8, 0x1000000, 0); //read phi psi
		odm_set_bb_reg(dm, R_0x9e8, 0x70000000, su_idx); //SU user 0
		odm_set_bb_reg(dm, R_0x1910, 0x8000, 0); //BFer

		dw0 = 0; // for 0x9ec
		dw1 = 0; // for 0x1900
		mem_addr = 0;
		mem_sel = 0;
		mem_move = 0;
		vm_info = vm_info | (snr & 0xff); //V matrix info
		vm_cnt = 8; // V matrix length counter
		psiphiR = (psiphi[phs_idx] & 0x3ff);

		while (mem_addr < mem_num) {
			while (vm_cnt <= 32) {
				// shift only max. 32 bit
				if (vm_cnt >= 20) {
					temp = psiphiR << 20;
					temp = temp << (vm_cnt - 20);
				} else {
					temp = psiphiR << vm_cnt;
				}
				vm_info |= temp;
				vm_cnt += 10;
			}
			if (mem_sel == 0) {
				dw0 = vm_info & 0xffffffff;
				vm_info = vm_info >> 32;
				vm_cnt -= 32;
				mem_sel = 1;
				mem_move = 0;
			} else {
				dw1 = vm_info & 0xffffffff;
				vm_info = vm_info >> 32;
				vm_cnt -= 32;
				mem_sel = 0;
				mem_move = 1;
			}
			if (mem_move == 1) {
				odm_set_bb_reg(dm, 0x9e8, 0x1000000, 0);
					       //read phi psi
				odm_set_bb_reg(dm, 0x1910, 0x3FF0000,
					       mem_addr);
				odm_set_bb_reg(dm, 0x09ec, 0xFFFFFFFF, dw0);
				odm_set_bb_reg(dm, 0x1900, 0xFFFFFFFF, dw1);
				odm_set_bb_reg(dm, 0x9e8, 0x1000000, 1);
					       //write phi psi
				mem_move = 0;
				mem_addr += 1;
			}
		}
		odm_set_bb_reg(dm, 0x9e8, 0x2000000, 0); //normal txbf
	}
#endif
} //end function
#endif
#endif
