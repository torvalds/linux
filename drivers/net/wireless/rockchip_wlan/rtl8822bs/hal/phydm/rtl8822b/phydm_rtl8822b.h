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
#if (RTL8822B_SUPPORT == 1)
#ifndef __ODM_RTL8822B_H__
#define __ODM_RTL8822B_H__

#ifdef DYN_ANT_WEIGHTING_SUPPORT
void phydm_dynamic_ant_weighting_8822b(void *dm_void);
#endif

void phydm_1rcca_setting(struct dm_struct *dm, boolean enable_1rcca);

void phydm_somlrxhp_setting(struct dm_struct *dm, boolean switch_soml);

#ifdef CONFIG_DYNAMIC_BYPASS
void phydm_pw_sat_8822b(struct dm_struct *dm, u8 rssi_value);
#endif

void phydm_hwsetting_8822b(struct dm_struct *dm);

void phydm_config_tx2path_8822b(struct dm_struct *dm,
				enum wireless_set wireless_mode,
				boolean is_tx2_path);

#endif /* @#define __ODM_RTL8822B_H__ */
#endif
