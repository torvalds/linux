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
#include "mp_precomp.h"
#include "../phydm_precomp.h"

#if (RTL8821C_SUPPORT)
void phydm_dynamic_switch_htstf_agc_8821c(struct dm_struct *dm)
{
	u16 ndp_valid_cnt = 0;

	if (dm->bhtstfdisabled)
		return;

	/*This count will be reset every 2 seconds*/
	ndp_valid_cnt = (u16)odm_get_bb_reg(dm, R_0xf24, MASKLWORD);

	if (dm->total_tp == 0 || ndp_valid_cnt != 0) {
		odm_set_bb_reg(dm, R_0x8d8, BIT(17), 0x1);
		dm->no_ndp_cnts = 0;
	} else {
		dm->no_ndp_cnts++;

		if (dm->no_ndp_cnts == 3) {
			odm_set_bb_reg(dm, R_0x8d8, BIT(17), 0x0);
			dm->no_ndp_cnts = 0;
		}
	}
	dm->ndp_cnt_pre = ndp_valid_cnt;
}

void phydm_hwsetting_8821c(struct dm_struct *dm)
{
	/*phydm_dynamic_switch_htstf_agc_8821c(dm);*/
}
#endif
