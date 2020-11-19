/* SPDX-License-Identifier: GPL-2.0 */
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

#include "../phydm_precomp.h"

#if (RTL8188E_SUPPORT == 1)

s8 phydm_cck_rssi_8188e(struct dm_struct *dm, u16 lna_idx, u8 vga_idx)
{
	s8 rx_pwr_all = 0;
	s8 lna_gain = 0;
	/*only use lna0/1/2/3/7*/
	s8 lna_gain_table_0[8] = {17, -1, -13, -29, -32, -35, -38, -41};
	/*only use lna3 /7*/
	s8 lna_gain_table_1[8] = {29, 20, 12, 3, -6, -15, -24, -33};
	/*only use lna1/3/5/7*/
	s8 lna_gain_table_2[8] = {17, -1, -13, -17, -32, -43, -38, -47};
	
	if (dm->cut_version >= ODM_CUT_I) { /*SMIC*/
		if (dm->ext_lna == 0x1) {
			switch (dm->type_glna) {
				case 0x2:	/*eLNA 14dB*/
					lna_gain = lna_gain_table_2[lna_idx];
					break;
				default:
					lna_gain = lna_gain_table_0[lna_idx];
					break;
			}
		} else {
			lna_gain = lna_gain_table_0[lna_idx];
		}		
	} else { /*TSMC*/
		lna_gain = lna_gain_table_1[lna_idx];
	}
	
	rx_pwr_all = lna_gain - (2 * vga_idx);

	return rx_pwr_all;
}
#endif
