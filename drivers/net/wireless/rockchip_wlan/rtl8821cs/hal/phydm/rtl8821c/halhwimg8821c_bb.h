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

/*Image2HeaderVersion: R3 1.5.8*/
#if (RTL8821C_SUPPORT == 1)
#ifndef __INC_MP_BB_HW_IMG_8821C_H
#define __INC_MP_BB_HW_IMG_8821C_H

/******************************************************************************
 *                           agc_tab.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8821c_agc_tab(struct dm_struct *dm);
u32 odm_get_version_mp_8821c_agc_tab(void);

/******************************************************************************
 *                           agc_tab_diff.TXT
 ******************************************************************************/

extern const u32	array_mp_8821c_agc_tab_diff_wlg[780];
extern const u32	array_mp_8821c_agc_tab_diff_btg[780];
void
odm_read_and_config_mp_8821c_agc_tab_diff(struct dm_struct *dm,
					  const u32 array[],
					  u32 array_len);
u32 odm_get_version_mp_8821c_agc_tab_diff(void);

/******************************************************************************
 *                           phy_reg.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8821c_phy_reg(struct dm_struct *dm);
u32 odm_get_version_mp_8821c_phy_reg(void);

/******************************************************************************
 *                           phy_reg_mp.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8821c_phy_reg_mp(struct dm_struct *dm);
u32 odm_get_version_mp_8821c_phy_reg_mp(void);

/******************************************************************************
 *                           phy_reg_pg.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8821c_phy_reg_pg(struct dm_struct *dm);
u32 odm_get_version_mp_8821c_phy_reg_pg(void);

/******************************************************************************
 *                           phy_reg_pg_type0x28.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8821c_phy_reg_pg_type0x28(struct dm_struct *dm);
u32 odm_get_version_mp_8821c_phy_reg_pg_type0x28(void);

#endif
#endif /* end of HWIMG_SUPPORT*/

